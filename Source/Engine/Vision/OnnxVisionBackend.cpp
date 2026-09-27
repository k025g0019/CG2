#include "OnnxVisionBackend.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <onnxruntime_cxx_api.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
	constexpr float kNonMaximumSuppressionIou = 0.45f;  // 重複ボックスを落とす IoU しきい値。
	constexpr int32_t kMaximumDetections = 64;
	constexpr int32_t kMaximumClassifications = 5;

	std::wstring ToWideString(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}

		const int requiredLength = MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);

		if (requiredLength <= 0) {
			return std::wstring();
		}

		std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wideText.data(), requiredLength);
		return wideText;
	}

	float ComputeIou(const ObjectDetectionResult& left, const ObjectDetectionResult& right) {
		const float leftMaxX = left.x + left.width;
		const float leftMaxY = left.y + left.height;
		const float rightMaxX = right.x + right.width;
		const float rightMaxY = right.y + right.height;

		const float intersectionWidth =
			(std::min)(leftMaxX, rightMaxX) - (std::max)(left.x, right.x);
		const float intersectionHeight =
			(std::min)(leftMaxY, rightMaxY) - (std::max)(left.y, right.y);

		if (intersectionWidth <= 0.0f || intersectionHeight <= 0.0f) {
			return 0.0f;
		}

		const float intersectionArea = intersectionWidth * intersectionHeight;
		const float unionArea =
			left.width * left.height + right.width * right.height - intersectionArea;
		return unionArea > 0.0f ? intersectionArea / unionArea : 0.0f;
	}

	void ApplyNonMaximumSuppression(std::vector<ObjectDetectionResult>& detections) {
		std::sort(
			detections.begin(),
			detections.end(),
			[](const ObjectDetectionResult& left, const ObjectDetectionResult& right) {
				return left.confidence > right.confidence;
			});

		std::vector<ObjectDetectionResult> keptDetections;

		for (const ObjectDetectionResult& detection : detections) {
			bool isSuppressed = false;

			for (const ObjectDetectionResult& keptDetection : keptDetections) {
				if (keptDetection.label == detection.label &&
					ComputeIou(keptDetection, detection) > kNonMaximumSuppressionIou) {
					isSuppressed = true;
					break;
				}
			}

			if (!isSuppressed) {
				keptDetections.push_back(detection);
			}

			if (static_cast<int32_t>(keptDetections.size()) >= kMaximumDetections) {
				break;
			}
		}

		detections.swap(keptDetections);
	}
}

struct OnnxVisionBackend::Impl {
	Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "CG2EngineVision"};
	Ort::SessionOptions sessionOptions;
	Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	std::unique_ptr<Ort::Session> session;
	std::string inputName;
	std::vector<std::string> outputNames;
};

OnnxVisionBackend::~OnnxVisionBackend() {
	Shutdown();
}

bool OnnxVisionBackend::Initialize() {
	if (isInitialized_) {
		return true;
	}

	lastError_.Clear();

	try {
		impl_ = new Impl();
		impl_->sessionOptions.SetIntraOpNumThreads(1);
		impl_->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
	}
	catch (const Ort::Exception& ortException) {
		lastError_.code = static_cast<int32_t>(ortException.GetOrtErrorCode());
		lastError_.message = std::string("ONNX Runtime の初期化に失敗しました: ") + ortException.what();
		delete impl_;
		impl_ = nullptr;
		return false;
	}
	catch (const std::exception& standardException) {
		lastError_.code = -1;
		lastError_.message = std::string("ONNX Runtime の初期化に失敗しました: ") + standardException.what();
		delete impl_;
		impl_ = nullptr;
		return false;
	}

	isInitialized_ = true;
	return true;
}

void OnnxVisionBackend::Shutdown() {
	if (impl_ != nullptr) {
		impl_->session.reset();
		delete impl_;
		impl_ = nullptr;
	}

	isInitialized_ = false;
	loadedModelPath_.clear();
	labels_.clear();
	result_ = VisionResult{};
}

bool OnnxVisionBackend::ApplyConfig(const VisionConfig& config) {
	const bool hasModelChanged = config.modelAssetPath != config_.modelAssetPath;
	const bool hasLabelChanged = config.labelAssetPath != config_.labelAssetPath;
	config_ = config;

	if (hasModelChanged && impl_ != nullptr) {
		impl_->session.reset();
		loadedModelPath_.clear();
	}

	if (hasModelChanged || hasLabelChanged) {
		labels_.clear();
	}

	return SupportsMode(config_.mode);
}

bool OnnxVisionBackend::SupportsMode(VisionRecognitionMode mode) const {
	return mode == VisionRecognitionMode::ObjectDetection ||
		mode == VisionRecognitionMode::ImageClassification ||
		mode == VisionRecognitionMode::FaceDetection;
}

void OnnxVisionBackend::LoadLabels() {
	labels_.clear();
	std::string labelPath = config_.labelAssetPath;

	if (labelPath.empty() && !config_.modelAssetPath.empty()) {
		// モデル横の同名 .txt / .names を既定の ラベル一覧として探す。
		std::filesystem::path candidatePath(config_.modelAssetPath);
		candidatePath.replace_extension(".txt");

		if (std::filesystem::exists(candidatePath)) {
			labelPath = candidatePath.string();
		}
		else {
			candidatePath.replace_extension(".names");

			if (std::filesystem::exists(candidatePath)) {
				labelPath = candidatePath.string();
			}
		}
	}

	if (labelPath.empty()) {
		return;
	}

	std::ifstream file(labelPath, std::ios::binary);

	if (!file.is_open()) {
		return;
	}

	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (isFirstLine) {
			isFirstLine = false;

			if (line.size() >= 3u &&
				static_cast<unsigned char>(line[0]) == 0xEFu &&
				static_cast<unsigned char>(line[1]) == 0xBBu &&
				static_cast<unsigned char>(line[2]) == 0xBFu) {
				line.erase(0, 3);
			}
		}

		if (!line.empty()) {
			labels_.push_back(line);
		}
	}
}

bool OnnxVisionBackend::EnsureSession() {
	if (impl_ == nullptr) {
		return false;
	}

	if (impl_->session != nullptr && loadedModelPath_ == config_.modelAssetPath) {
		return true;
	}

	if (config_.modelAssetPath.empty()) {
		lastError_.code = -2;
		lastError_.message = "ONNX モデルが指定されていません。Inspector の Model を設定してください。";
		return false;
	}

	if (!std::filesystem::exists(config_.modelAssetPath)) {
		lastError_.code = -3;
		lastError_.message = "ONNX モデルが見つかりません: " + config_.modelAssetPath;
		return false;
	}

	try {
		const std::wstring wideModelPath = ToWideString(config_.modelAssetPath);
		impl_->session = std::make_unique<Ort::Session>(
			impl_->environment,
			wideModelPath.c_str(),
			impl_->sessionOptions);

		Ort::AllocatorWithDefaultOptions allocator;
		const Ort::AllocatedStringPtr inputNamePtr = impl_->session->GetInputNameAllocated(0, allocator);
		impl_->inputName = inputNamePtr.get();

		impl_->outputNames.clear();
		const size_t outputCount = impl_->session->GetOutputCount();

		for (size_t outputIndex = 0u; outputIndex < outputCount; ++outputIndex) {
			const Ort::AllocatedStringPtr outputNamePtr =
				impl_->session->GetOutputNameAllocated(outputIndex, allocator);
			impl_->outputNames.push_back(outputNamePtr.get());
		}

		// 入力サイズはモデル定義を優先し、動的(-1)なら Inspector 指定か既定値を使う。
		const Ort::TypeInfo inputTypeInfo = impl_->session->GetInputTypeInfo(0);
		const auto shapeInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
		const std::vector<int64_t> inputShape = shapeInfo.GetShape();

		int32_t modelWidth = 0;
		int32_t modelHeight = 0;

		if (inputShape.size() == 4u) {
			modelHeight = static_cast<int32_t>(inputShape[2]);
			modelWidth = static_cast<int32_t>(inputShape[3]);
		}

		inputWidth_ = modelWidth > 0 ? modelWidth
			: (config_.inferenceWidth > 0 ? config_.inferenceWidth : 640);
		inputHeight_ = modelHeight > 0 ? modelHeight
			: (config_.inferenceHeight > 0 ? config_.inferenceHeight : 640);

		loadedModelPath_ = config_.modelAssetPath;
		LoadLabels();
		lastError_.Clear();

		ExternalFeatureLog::Info(
			ExternalFeatureCategory::Vision,
			"ONNX モデルを読み込みました: " + config_.modelAssetPath +
				" (" + std::to_string(inputWidth_) + "x" + std::to_string(inputHeight_) + ")");
		return true;
	}
	catch (const Ort::Exception& ortException) {
		lastError_.code = static_cast<int32_t>(ortException.GetOrtErrorCode());
		lastError_.message = std::string("ONNX モデルを読み込めません: ") + ortException.what();
		impl_->session.reset();
		return false;
	}
	catch (const std::exception& standardException) {
		lastError_.code = -4;
		lastError_.message = std::string("ONNX モデルを読み込めません: ") + standardException.what();
		impl_->session.reset();
		return false;
	}
}

void OnnxVisionBackend::BuildInputTensor(const ImageFrame& frame) {
	const size_t elementCount =
		static_cast<size_t>(inputWidth_) * static_cast<size_t>(inputHeight_) * 3u;

	if (inputTensorData_.size() != elementCount) {
		inputTensorData_.assign(elementCount, 0.0f);
	}

	// Letterbox。縦横比を保ったまま入力サイズへ収め、余白は灰色で埋める。
	const float scaleX = static_cast<float>(inputWidth_) / static_cast<float>(frame.width);
	const float scaleY = static_cast<float>(inputHeight_) / static_cast<float>(frame.height);
	letterboxScale_ = (std::min)(scaleX, scaleY);
	const int32_t scaledWidth = static_cast<int32_t>(static_cast<float>(frame.width) * letterboxScale_);
	const int32_t scaledHeight = static_cast<int32_t>(static_cast<float>(frame.height) * letterboxScale_);
	letterboxOffsetX_ = static_cast<float>(inputWidth_ - scaledWidth) * 0.5f;
	letterboxOffsetY_ = static_cast<float>(inputHeight_ - scaledHeight) * 0.5f;
	sourceWidth_ = frame.width;
	sourceHeight_ = frame.height;

	const size_t planeSize = static_cast<size_t>(inputWidth_) * static_cast<size_t>(inputHeight_);
	std::fill(inputTensorData_.begin(), inputTensorData_.end(), 0.5f);

	for (int32_t destinationY = 0; destinationY < scaledHeight; ++destinationY) {
		const int32_t sourceY = (std::min)(
			frame.height - 1,
			static_cast<int32_t>(static_cast<float>(destinationY) / letterboxScale_));
		const int32_t paddedY = destinationY + static_cast<int32_t>(letterboxOffsetY_);

		if (paddedY < 0 || paddedY >= inputHeight_) {
			continue;
		}

		for (int32_t destinationX = 0; destinationX < scaledWidth; ++destinationX) {
			const int32_t sourceX = (std::min)(
				frame.width - 1,
				static_cast<int32_t>(static_cast<float>(destinationX) / letterboxScale_));
			const int32_t paddedX = destinationX + static_cast<int32_t>(letterboxOffsetX_);

			if (paddedX < 0 || paddedX >= inputWidth_) {
				continue;
			}

			const size_t sourceOffset =
				(static_cast<size_t>(sourceY) * static_cast<size_t>(frame.width) +
				 static_cast<size_t>(sourceX)) * 4u;
			const float blue = static_cast<float>(frame.pixels[sourceOffset + 0u]) / 255.0f;
			const float green = static_cast<float>(frame.pixels[sourceOffset + 1u]) / 255.0f;
			const float red = static_cast<float>(frame.pixels[sourceOffset + 2u]) / 255.0f;

			const size_t destinationOffset =
				static_cast<size_t>(paddedY) * static_cast<size_t>(inputWidth_) +
				static_cast<size_t>(paddedX);
			inputTensorData_[destinationOffset] = red;
			inputTensorData_[planeSize + destinationOffset] = green;
			inputTensorData_[planeSize * 2u + destinationOffset] = blue;
		}
	}
}

std::string OnnxVisionBackend::GetLabelName(int32_t classIndex) const {
	if (classIndex >= 0 && classIndex < static_cast<int32_t>(labels_.size())) {
		return labels_[static_cast<size_t>(classIndex)];
	}

	return "class_" + std::to_string(classIndex);
}

void OnnxVisionBackend::ParseClassification(const float* outputData, size_t elementCount) {
	if (outputData == nullptr || elementCount == 0u) {
		return;
	}

	// Softmax を掛けて確率へ直す。既に確率のモデルでも順位は変わらない。
	float maximumLogit = outputData[0];

	for (size_t elementIndex = 1u; elementIndex < elementCount; ++elementIndex) {
		maximumLogit = (std::max)(maximumLogit, outputData[elementIndex]);
	}

	float expSum = 0.0f;
	std::vector<float> probabilities(elementCount, 0.0f);

	for (size_t elementIndex = 0u; elementIndex < elementCount; ++elementIndex) {
		probabilities[elementIndex] = std::exp(outputData[elementIndex] - maximumLogit);
		expSum += probabilities[elementIndex];
	}

	if (expSum <= 0.0f) {
		return;
	}

	std::vector<std::pair<float, int32_t>> rankedProbabilities;
	rankedProbabilities.reserve(elementCount);

	for (size_t elementIndex = 0u; elementIndex < elementCount; ++elementIndex) {
		rankedProbabilities.emplace_back(
			probabilities[elementIndex] / expSum,
			static_cast<int32_t>(elementIndex));
	}

	std::sort(
		rankedProbabilities.begin(),
		rankedProbabilities.end(),
		[](const std::pair<float, int32_t>& left, const std::pair<float, int32_t>& right) {
			return left.first > right.first;
		});

	const int32_t keepCount =
		(std::min)(kMaximumClassifications, static_cast<int32_t>(rankedProbabilities.size()));

	for (int32_t rankIndex = 0; rankIndex < keepCount; ++rankIndex) {
		if (rankedProbabilities[static_cast<size_t>(rankIndex)].first < config_.confidenceThreshold) {
			continue;
		}

		ImageClassificationResult classification{};
		classification.confidence = rankedProbabilities[static_cast<size_t>(rankIndex)].first;
		classification.label = GetLabelName(rankedProbabilities[static_cast<size_t>(rankIndex)].second);
		result_.classifications.push_back(classification);
	}
}

void OnnxVisionBackend::ParseDetection(
	const float* outputData,
	int64_t boxCount,
	int64_t attributeCount,
	bool hasObjectness) {
	if (outputData == nullptr || boxCount <= 0 || attributeCount <= 4) {
		return;
	}

	const int64_t classOffset = hasObjectness ? 5 : 4;
	const int64_t classCount = attributeCount - classOffset;

	if (classCount <= 0) {
		return;
	}

	std::vector<ObjectDetectionResult> detections;

	for (int64_t boxIndex = 0; boxIndex < boxCount; ++boxIndex) {
		const float* boxData = outputData + boxIndex * attributeCount;
		const float objectness = hasObjectness ? boxData[4] : 1.0f;

		if (objectness < 0.01f) {
			continue;
		}

		int64_t bestClassIndex = 0;
		float bestClassScore = 0.0f;

		for (int64_t classIndex = 0; classIndex < classCount; ++classIndex) {
			const float classScore = boxData[classOffset + classIndex];

			if (classScore > bestClassScore) {
				bestClassScore = classScore;
				bestClassIndex = classIndex;
			}
		}

		const float confidence = bestClassScore * objectness;

		if (confidence < config_.confidenceThreshold) {
			continue;
		}

		// モデル入力座標系の中心・幅高さを、元フレームの正規化座標へ戻す。
		const float centerX = (boxData[0] - letterboxOffsetX_) / (std::max)(letterboxScale_, 0.0001f);
		const float centerY = (boxData[1] - letterboxOffsetY_) / (std::max)(letterboxScale_, 0.0001f);
		const float boxWidth = boxData[2] / (std::max)(letterboxScale_, 0.0001f);
		const float boxHeight = boxData[3] / (std::max)(letterboxScale_, 0.0001f);

		ObjectDetectionResult detection{};
		detection.confidence = (std::clamp)(confidence, 0.0f, 1.0f);
		detection.label = GetLabelName(static_cast<int32_t>(bestClassIndex));
		detection.x = (std::clamp)(
			(centerX - boxWidth * 0.5f) / static_cast<float>((std::max)(sourceWidth_, 1)), 0.0f, 1.0f);
		detection.y = (std::clamp)(
			(centerY - boxHeight * 0.5f) / static_cast<float>((std::max)(sourceHeight_, 1)), 0.0f, 1.0f);
		detection.width = (std::clamp)(
			boxWidth / static_cast<float>((std::max)(sourceWidth_, 1)), 0.0f, 1.0f);
		detection.height = (std::clamp)(
			boxHeight / static_cast<float>((std::max)(sourceHeight_, 1)), 0.0f, 1.0f);

		if (detection.width <= 0.0f || detection.height <= 0.0f) {
			continue;
		}

		detections.push_back(detection);
	}

	ApplyNonMaximumSuppression(detections);

	if (config_.mode == VisionRecognitionMode::FaceDetection) {
		// 顔検出モードでは顔ラベルのボックスだけを顔として返す。
		// ラベル一覧が無いモデル(顔専用モデル)は全ボックスを顔として扱う。
		for (const ObjectDetectionResult& detection : detections) {
			const bool isFaceLabel =
				labels_.empty() ||
				detection.label.find("face") != std::string::npos ||
				detection.label.find("顔") != std::string::npos ||
				detection.label.find("person") != std::string::npos;

			if (!isFaceLabel) {
				continue;
			}

			FaceDetectionResult face{};
			face.confidence = detection.confidence;
			face.x = detection.x;
			face.y = detection.y;
			face.width = detection.width;
			face.height = detection.height;
			result_.faces.push_back(face);
		}

		return;
	}

	result_.objects = detections;
}

void OnnxVisionBackend::ProcessFrame(const ImageFrame& frame) {
	result_ = VisionResult{};
	result_.mode = config_.mode;
	result_.frameIndex = frame.frameIndex;
	result_.timestampSeconds = frame.timestampSeconds;

	if (!SupportsMode(config_.mode)) {
		result_.state = ExternalFeatureState::Unavailable;
		result_.error.code = -10;
		result_.error.message =
			std::string("ONNX Backend は ") + ToDisplayString(config_.mode) + " に対応していません。";
		return;
	}

	if (!frame.IsValid()) {
		result_.state = ExternalFeatureState::Error;
		result_.error.code = -11;
		result_.error.message = "Camera フレームが無効です。";
		return;
	}

	if (!isInitialized_ && !Initialize()) {
		result_.state = ExternalFeatureState::Unavailable;
		result_.error = lastError_;
		return;
	}

	if (!EnsureSession()) {
		result_.state = ExternalFeatureState::Unavailable;
		result_.error = lastError_;
		return;
	}

	const auto startTime = std::chrono::steady_clock::now();

	try {
		BuildInputTensor(frame);

		const std::array<int64_t, 4> inputShape = {
			1,
			3,
			static_cast<int64_t>(inputHeight_),
			static_cast<int64_t>(inputWidth_)};
		Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
			impl_->memoryInfo,
			inputTensorData_.data(),
			inputTensorData_.size(),
			inputShape.data(),
			inputShape.size());

		const char* inputNames[] = {impl_->inputName.c_str()};
		std::vector<const char*> outputNames;
		outputNames.reserve(impl_->outputNames.size());

		for (const std::string& outputName : impl_->outputNames) {
			outputNames.push_back(outputName.c_str());
		}

		std::vector<Ort::Value> outputTensors = impl_->session->Run(
			Ort::RunOptions{nullptr},
			inputNames,
			&inputTensor,
			1,
			outputNames.data(),
			outputNames.size());

		if (outputTensors.empty() || !outputTensors[0].IsTensor()) {
			result_.state = ExternalFeatureState::Error;
			result_.error.code = -12;
			result_.error.message = "モデル出力を Tensor として読めません。";
			return;
		}

		const auto outputInfo = outputTensors[0].GetTensorTypeAndShapeInfo();
		const std::vector<int64_t> outputShape = outputInfo.GetShape();
		const float* outputData = outputTensors[0].GetTensorData<float>();
		const size_t outputElementCount = outputInfo.GetElementCount();

		if (config_.mode == VisionRecognitionMode::ImageClassification) {
			const size_t classCount = outputShape.size() >= 2u
				? static_cast<size_t>(outputShape[outputShape.size() - 1u])
				: outputElementCount;
			ParseClassification(outputData, (std::min)(classCount, outputElementCount));
		}
		else if (outputShape.size() == 3u) {
			// [1, A, B]。A < B なら YOLOv8 形式(属性 × ボックス)、そうでなければ v5 形式。
			const int64_t firstDimension = outputShape[1];
			const int64_t secondDimension = outputShape[2];

			if (firstDimension < secondDimension) {
				// 属性が先に並ぶ配置はボックス単位へ並べ替えてから解析する。
				const int64_t attributeCount = firstDimension;
				const int64_t boxCount = secondDimension;
				std::vector<float> transposedData(
					static_cast<size_t>(attributeCount) * static_cast<size_t>(boxCount), 0.0f);

				for (int64_t attributeIndex = 0; attributeIndex < attributeCount; ++attributeIndex) {
					for (int64_t boxIndex = 0; boxIndex < boxCount; ++boxIndex) {
						transposedData[static_cast<size_t>(boxIndex * attributeCount + attributeIndex)] =
							outputData[static_cast<size_t>(attributeIndex * boxCount + boxIndex)];
					}
				}

				ParseDetection(transposedData.data(), boxCount, attributeCount, false);
			}
			else {
				ParseDetection(outputData, firstDimension, secondDimension, true);
			}
		}
		else if (outputShape.size() == 2u) {
			ParseDetection(outputData, outputShape[0], outputShape[1], true);
		}
		else {
			result_.state = ExternalFeatureState::Error;
			result_.error.code = -13;
			result_.error.message = "対応していないモデル出力形状です。";
			return;
		}

		const auto endTime = std::chrono::steady_clock::now();
		result_.inferenceMilliseconds =
			std::chrono::duration<float, std::milli>(endTime - startTime).count();
		result_.isValid = true;
		result_.state = ExternalFeatureState::Running;
		lastError_.Clear();
	}
	catch (const Ort::Exception& ortException) {
		lastError_.code = static_cast<int32_t>(ortException.GetOrtErrorCode());
		lastError_.message = std::string("ONNX 推論に失敗しました: ") + ortException.what();
		result_.state = ExternalFeatureState::Error;
		result_.error = lastError_;
	}
	catch (const std::exception& standardException) {
		lastError_.code = -14;
		lastError_.message = std::string("ONNX 推論に失敗しました: ") + standardException.what();
		result_.state = ExternalFeatureState::Error;
		result_.error = lastError_;
	}
}

VisionResult OnnxVisionBackend::GetResult() {
	return result_;
}

const char* OnnxVisionBackend::GetName() const {
	return "ONNX Runtime";
}

ExternalFeatureError OnnxVisionBackend::GetLastError() const {
	return lastError_;
}
