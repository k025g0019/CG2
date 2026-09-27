#include "BuiltinVisionBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
	constexpr int32_t kColorSampleStep = 2;  // 色追跡は 2 ピクセル飛ばしで走査する。
}

const char* ToDisplayString(VisionRecognitionMode mode) {
	switch (mode) {
	case VisionRecognitionMode::ObjectDetection:
		return "物体検出";
	case VisionRecognitionMode::ImageClassification:
		return "画像分類";
	case VisionRecognitionMode::FaceDetection:
		return "顔検出";
	case VisionRecognitionMode::FaceLandmark:
		return "顔ランドマーク";
	case VisionRecognitionMode::HeadPose:
		return "頭部方向";
	case VisionRecognitionMode::ColorTracking:
		return "色追跡";
	case VisionRecognitionMode::MotionDetection:
		return "動体検出";
	default:
		return "物体検出";
	}
}

const char* ToDisplayString(VisionBackendKind backendKind) {
	switch (backendKind) {
	case VisionBackendKind::Auto:
		return "自動選択";
	case VisionBackendKind::Builtin:
		return "内蔵(色/動き)";
	case VisionBackendKind::OnnxRuntime:
		return "ONNX Runtime";
	case VisionBackendKind::OpenCv:
		return "OpenCV";
	case VisionBackendKind::MediaPipe:
		return "MediaPipe";
	case VisionBackendKind::None:
		return "使用しない";
	default:
		return "自動選択";
	}
}

bool BuiltinVisionBackend::Initialize() {
	isInitialized_ = true;
	lastError_.Clear();
	previousLuminance_.assign(
		static_cast<size_t>(kMotionGridWidth) * static_cast<size_t>(kMotionGridHeight),
		0.0f);
	hasPreviousLuminance_ = false;
	return true;
}

void BuiltinVisionBackend::Shutdown() {
	isInitialized_ = false;
	hasPreviousLuminance_ = false;
	previousLuminance_.clear();
	result_ = VisionResult{};
}

bool BuiltinVisionBackend::ApplyConfig(const VisionConfig& config) {
	if (config.mode != config_.mode) {
		hasPreviousLuminance_ = false;
	}

	config_ = config;
	return SupportsMode(config_.mode);
}

bool BuiltinVisionBackend::SupportsMode(VisionRecognitionMode mode) const {
	return mode == VisionRecognitionMode::ColorTracking ||
		mode == VisionRecognitionMode::MotionDetection;
}

void BuiltinVisionBackend::ProcessFrame(const ImageFrame& frame) {
	result_ = VisionResult{};
	result_.mode = config_.mode;
	result_.frameIndex = frame.frameIndex;
	result_.timestampSeconds = frame.timestampSeconds;

	if (!frame.IsValid()) {
		result_.state = ExternalFeatureState::Error;
		result_.error.code = -1;
		result_.error.message = "Camera フレームが無効です。";
		return;
	}

	if (!SupportsMode(config_.mode)) {
		// 対応していないモードは別処理で代替せず Unavailable を返す。
		result_.state = ExternalFeatureState::Unavailable;
		result_.error.code = -2;
		result_.error.message =
			std::string("内蔵 Backend は ") + ToDisplayString(config_.mode) +
			" に対応していません。ONNX Backend とモデルを設定してください。";
		return;
	}

	const auto startTime = std::chrono::steady_clock::now();

	if (config_.mode == VisionRecognitionMode::ColorTracking) {
		ProcessColorTracking(frame);
	}
	else {
		ProcessMotionDetection(frame);
	}

	const auto endTime = std::chrono::steady_clock::now();
	result_.inferenceMilliseconds =
		std::chrono::duration<float, std::milli>(endTime - startTime).count();
	result_.isValid = true;
	result_.state = ExternalFeatureState::Running;
}

void BuiltinVisionBackend::ProcessColorTracking(const ImageFrame& frame) {
	const float targetRed = (std::clamp)(config_.targetColorR, 0.0f, 1.0f);
	const float targetGreen = (std::clamp)(config_.targetColorG, 0.0f, 1.0f);
	const float targetBlue = (std::clamp)(config_.targetColorB, 0.0f, 1.0f);
	const float tolerance = (std::clamp)(config_.colorTolerance, 0.0f, 1.0f);
	// RGB 空間の距離しきい値。tolerance 1.0 で全色一致になる。
	const float toleranceDistance = tolerance * std::sqrt(3.0f);

	int32_t matchedCount = 0;
	int32_t sampledCount = 0;
	float sumX = 0.0f;
	float sumY = 0.0f;
	int32_t minimumX = frame.width;
	int32_t minimumY = frame.height;
	int32_t maximumX = 0;
	int32_t maximumY = 0;

	for (int32_t pixelY = 0; pixelY < frame.height; pixelY += kColorSampleStep) {
		for (int32_t pixelX = 0; pixelX < frame.width; pixelX += kColorSampleStep) {
			const size_t pixelOffset =
				(static_cast<size_t>(pixelY) * static_cast<size_t>(frame.width) +
				 static_cast<size_t>(pixelX)) * 4u;
			// Camera からは BGRA で受け取る。
			const float blue = static_cast<float>(frame.pixels[pixelOffset + 0u]) / 255.0f;
			const float green = static_cast<float>(frame.pixels[pixelOffset + 1u]) / 255.0f;
			const float red = static_cast<float>(frame.pixels[pixelOffset + 2u]) / 255.0f;
			++sampledCount;

			const float differenceRed = red - targetRed;
			const float differenceGreen = green - targetGreen;
			const float differenceBlue = blue - targetBlue;
			const float distance = std::sqrt(
				differenceRed * differenceRed +
				differenceGreen * differenceGreen +
				differenceBlue * differenceBlue);

			if (distance > toleranceDistance) {
				continue;
			}

			++matchedCount;
			sumX += static_cast<float>(pixelX);
			sumY += static_cast<float>(pixelY);
			minimumX = (std::min)(minimumX, pixelX);
			minimumY = (std::min)(minimumY, pixelY);
			maximumX = (std::max)(maximumX, pixelX);
			maximumY = (std::max)(maximumY, pixelY);
		}
	}

	ColorTrackingResult colorResult{};

	if (matchedCount > 0 && sampledCount > 0) {
		colorResult.areaRatio =
			static_cast<float>(matchedCount) / static_cast<float>(sampledCount);
		colorResult.isDetected = colorResult.areaRatio >= (std::max)(config_.minimumAreaRatio, 0.0f);
		colorResult.centerX =
			sumX / static_cast<float>(matchedCount) / static_cast<float>(frame.width);
		colorResult.centerY =
			sumY / static_cast<float>(matchedCount) / static_cast<float>(frame.height);
		colorResult.boundsX = static_cast<float>(minimumX) / static_cast<float>(frame.width);
		colorResult.boundsY = static_cast<float>(minimumY) / static_cast<float>(frame.height);
		colorResult.boundsWidth =
			static_cast<float>(maximumX - minimumX) / static_cast<float>(frame.width);
		colorResult.boundsHeight =
			static_cast<float>(maximumY - minimumY) / static_cast<float>(frame.height);
	}

	result_.colorTracking = colorResult;
}

void BuiltinVisionBackend::ProcessMotionDetection(const ImageFrame& frame) {
	const size_t gridSize =
		static_cast<size_t>(kMotionGridWidth) * static_cast<size_t>(kMotionGridHeight);

	if (previousLuminance_.size() != gridSize) {
		previousLuminance_.assign(gridSize, 0.0f);
		hasPreviousLuminance_ = false;
	}

	std::vector<float> currentLuminance(gridSize, 0.0f);

	for (int32_t gridY = 0; gridY < kMotionGridHeight; ++gridY) {
		for (int32_t gridX = 0; gridX < kMotionGridWidth; ++gridX) {
			const int32_t pixelX = (std::min)(
				frame.width - 1,
				gridX * frame.width / kMotionGridWidth);
			const int32_t pixelY = (std::min)(
				frame.height - 1,
				gridY * frame.height / kMotionGridHeight);
			const size_t pixelOffset =
				(static_cast<size_t>(pixelY) * static_cast<size_t>(frame.width) +
				 static_cast<size_t>(pixelX)) * 4u;
			const float blue = static_cast<float>(frame.pixels[pixelOffset + 0u]) / 255.0f;
			const float green = static_cast<float>(frame.pixels[pixelOffset + 1u]) / 255.0f;
			const float red = static_cast<float>(frame.pixels[pixelOffset + 2u]) / 255.0f;
			currentLuminance[static_cast<size_t>(gridY) * static_cast<size_t>(kMotionGridWidth) +
				static_cast<size_t>(gridX)] = 0.299f * red + 0.587f * green + 0.114f * blue;
		}
	}

	MotionResult motionResult{};

	if (hasPreviousLuminance_) {
		float differenceSum = 0.0f;
		float weightedX = 0.0f;
		float weightedY = 0.0f;
		float weightSum = 0.0f;

		for (int32_t gridY = 0; gridY < kMotionGridHeight; ++gridY) {
			for (int32_t gridX = 0; gridX < kMotionGridWidth; ++gridX) {
				const size_t gridIndex =
					static_cast<size_t>(gridY) * static_cast<size_t>(kMotionGridWidth) +
					static_cast<size_t>(gridX);
				const float difference =
					std::fabs(currentLuminance[gridIndex] - previousLuminance_[gridIndex]);
				differenceSum += difference;

				if (difference > 0.04f) {
					weightedX += static_cast<float>(gridX) * difference;
					weightedY += static_cast<float>(gridY) * difference;
					weightSum += difference;
				}
			}
		}

		motionResult.motionMagnitude =
			(std::clamp)(differenceSum / static_cast<float>(gridSize) * 4.0f, 0.0f, 1.0f);
		motionResult.motion = motionResult.motionMagnitude > (std::max)(config_.motionThreshold, 0.0f);

		if (weightSum > 0.0f) {
			motionResult.centerX =
				weightedX / weightSum / static_cast<float>(kMotionGridWidth - 1);
			motionResult.centerY =
				weightedY / weightSum / static_cast<float>(kMotionGridHeight - 1);
		}
	}

	previousLuminance_.swap(currentLuminance);
	hasPreviousLuminance_ = true;
	result_.motion = motionResult;
}

VisionResult BuiltinVisionBackend::GetResult() {
	return result_;
}

const char* BuiltinVisionBackend::GetName() const {
	return "内蔵 Vision";
}

ExternalFeatureError BuiltinVisionBackend::GetLastError() const {
	return lastError_;
}
