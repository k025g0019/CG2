#pragma once

#include "Source/Engine/External/ExternalFeature.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 画像認識モジュールの公開型(仕様書 15〜32 項)
//================================================================
// OpenCV / ONNX / MediaPipe など Backend 側の型はここへ持ち込まない。
// 人物そのものを識別する機能(個人特定)はこのモジュールでは扱わない。

// Camera から受け取る 1 フレーム。Backend へはこの形だけを渡す。
struct ImageFrame {
	int32_t width = 0;
	int32_t height = 0;
	int32_t frameIndex = 0;
	double timestampSeconds = 0.0;
	// 8bit BGRA が width * height * 4 バイト並ぶ。空なら無効フレーム。
	std::vector<uint8_t> pixels;

	bool IsValid() const {
		return width > 0 && height > 0 &&
			pixels.size() >= static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
	}
};

// 認識モード(仕様書 19 項)。
enum class VisionRecognitionMode : int32_t {
	ObjectDetection = 0,
	ImageClassification,
	FaceDetection,
	FaceLandmark,
	HeadPose,
	ColorTracking,
	MotionDetection,
};

// Backend 候補(仕様書 29 項)。
enum class VisionBackendKind : int32_t {
	Auto = 0,        // 使えるものを自動選択する。
	Builtin,         // 追加ライブラリ無しの内蔵実装(色追跡 / 動き検出)。
	OnnxRuntime,     // ONNX Runtime + ユーザー指定モデル。
	OpenCv,          // OpenCV(未実装)。
	MediaPipe,       // MediaPipe(未実装)。
	None,
};

const char* ToDisplayString(VisionRecognitionMode mode);
const char* ToDisplayString(VisionBackendKind backendKind);

// 物体検出の 1 件(仕様書 20 項)。座標は 0.0〜1.0 の正規化値で持つ。
struct ObjectDetectionResult {
	std::string label;
	float confidence = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

// 画像分類(仕様書 21 項)。
struct ImageClassificationResult {
	std::string label;
	float confidence = 0.0f;
};

// 顔検出(仕様書 22 項)。
struct FaceDetectionResult {
	float confidence = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

// 顔ランドマーク(仕様書 23 項)。
struct FaceLandmarkPoint {
	std::string name;  // "leftEye" / "nose" / "mouth" など。
	float x = 0.0f;
	float y = 0.0f;
};

// 顔の向き(仕様書 24 項)。
struct HeadPoseResult {
	bool isValid = false;
	float yaw = 0.0f;
	float pitch = 0.0f;
	float roll = 0.0f;
};

// 動体検出(仕様書 26 項)。
struct MotionResult {
	bool motion = false;
	float motionMagnitude = 0.0f;
	// 動きの中心。0.0〜1.0 の正規化画面座標。
	float centerX = 0.5f;
	float centerY = 0.5f;
};

// 色追跡(仕様書 27 項)。
struct ColorTrackingResult {
	bool isDetected = false;
	float centerX = 0.5f;
	float centerY = 0.5f;
	float areaRatio = 0.0f;  // 画面全体に対する検出面積比。
	float boundsX = 0.0f;
	float boundsY = 0.0f;
	float boundsWidth = 0.0f;
	float boundsHeight = 0.0f;
};

// Backend が返す集約結果(仕様書 28 項)。
struct VisionResult {
	bool isValid = false;
	int32_t frameIndex = 0;
	double timestampSeconds = 0.0;
	VisionRecognitionMode mode = VisionRecognitionMode::ObjectDetection;
	ExternalFeatureState state = ExternalFeatureState::Unavailable;
	ExternalFeatureError error{};

	std::vector<ObjectDetectionResult> objects;
	std::vector<ImageClassificationResult> classifications;
	std::vector<FaceDetectionResult> faces;
	std::vector<FaceLandmarkPoint> faceLandmarks;
	HeadPoseResult headPose{};
	MotionResult motion{};
	ColorTrackingResult colorTracking{};
	float inferenceMilliseconds = 0.0f;
};

// Inspector から渡す設定(仕様書 30 項)。
struct VisionConfig {
	VisionRecognitionMode mode = VisionRecognitionMode::MotionDetection;
	VisionBackendKind backendKind = VisionBackendKind::Auto;
	std::string modelAssetPath;   // ONNX モデルのパス。
	std::string labelAssetPath;   // ラベル一覧(1 行 1 ラベル)。空ならモデル横の .txt を探す。
	float confidenceThreshold = 0.5f;
	float recognitionIntervalSeconds = 0.2f;  // 毎フレーム推論しない(仕様書 31 項)。
	int32_t inferenceWidth = 0;   // 0 ならモデル既定または Frame 解像度を使う。
	int32_t inferenceHeight = 0;

	// Color Tracking 設定。
	float targetColorR = 1.0f;
	float targetColorG = 0.0f;
	float targetColorB = 0.0f;
	float colorTolerance = 0.25f;
	float minimumAreaRatio = 0.002f;

	// Motion Detection 設定。
	float motionThreshold = 0.06f;  // これを超える変化量で motion = true。
};

// Camera 設定(仕様書 17 項)。
struct CameraInputConfig {
	std::string deviceName;  // 空なら既定 Camera。
	int32_t requestedWidth = 640;
	int32_t requestedHeight = 480;
	int32_t frameRateLimit = 30;  // 0 で無制限。
};

struct CameraDeviceInfo {
	std::string deviceName;
	std::string deviceId;
	bool isDefault = false;
};

// Debug 表示用(仕様書 32 項)。
struct VisionRuntimeStatus {
	ExternalFeatureState cameraState = ExternalFeatureState::Unavailable;
	ExternalFeatureState recognitionState = ExternalFeatureState::Unavailable;
	std::string cameraDeviceName;
	std::string backendName;
	int32_t frameWidth = 0;
	int32_t frameHeight = 0;
	int32_t capturedFrameCount = 0;
	int32_t recognizedFrameCount = 0;
	float captureFps = 0.0f;
	float inferenceMilliseconds = 0.0f;
	ExternalFeatureError lastError{};
};

#pragma warning(pop)
