#pragma once

#include "ICameraSource.h"
#include "IVisionBackend.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// VisionSystem
//================================================================
// CameraInputComponent(映像取得)と ImageRecognizerComponent(認識)を
// GameObject ID ごとに管理する。
//   ・Camera は Component 1 つに対して 1 つの取得元を開く
//   ・認識は Component ごとに Backend を持つ(モデルが別々になるため)
//   ・推論は Recognition Interval で間引く(仕様書 31 項)

class VisionSystem {
public:
	static VisionSystem& Get();

	VisionSystem(const VisionSystem&) = delete;
	VisionSystem& operator=(const VisionSystem&) = delete;

	void Shutdown();
	void Update(float deltaTime);

	//============================================================
	// Camera(仕様書 17 項)
	//============================================================
	bool OpenCamera(int32_t gameObjectId, const CameraInputConfig& config);
	void CloseCamera(int32_t gameObjectId);
	bool IsCameraOpen(int32_t gameObjectId) const;
	bool HasCamera(int32_t gameObjectId) const;
	const ImageFrame* GetLatestFrame(int32_t gameObjectId) const;  // Debug Preview 用。
	void EnumerateCameraDevices(std::vector<CameraDeviceInfo>& outDevices) const;
	std::vector<int32_t> GetCameraGameObjectIds() const;
	VisionRuntimeStatus GetCameraStatus(int32_t gameObjectId) const;

	//============================================================
	// 認識(仕様書 18〜27 項)
	//============================================================
	// cameraGameObjectId が -1 の場合は、開いている Camera のうち先頭を使う。
	void RegisterRecognizer(int32_t gameObjectId, int32_t cameraGameObjectId, const VisionConfig& config);
	void UnregisterRecognizer(int32_t gameObjectId);
	bool StartRecognition(int32_t gameObjectId);
	bool StopRecognition(int32_t gameObjectId);
	bool IsRecognizing(int32_t gameObjectId) const;
	bool HasRecognizer(int32_t gameObjectId) const;
	bool TryGetResult(int32_t gameObjectId, VisionResult& outResult) const;
	VisionRuntimeStatus GetRecognizerStatus(int32_t gameObjectId) const;
	std::vector<int32_t> GetRecognizerGameObjectIds() const;

	ExternalFeatureState GetState() const;
	ExternalFeatureError GetLastError() const;

private:
	VisionSystem() = default;
	~VisionSystem() = default;

	struct CameraEntry {
		std::unique_ptr<ICameraSource> source;
		CameraInputConfig config{};
		ImageFrame latestFrame{};
		int32_t capturedFrameCount = 0;
		float fpsTimerSeconds = 0.0f;
		int32_t fpsFrameCount = 0;
		float measuredFps = 0.0f;
		ExternalFeatureState state = ExternalFeatureState::Unavailable;
		ExternalFeatureError lastError{};
	};

	struct RecognizerEntry {
		std::unique_ptr<IVisionBackend> backend;
		VisionConfig config{};
		VisionResult result{};
		int32_t cameraGameObjectId = -1;
		int32_t processedFrameIndex = -1;
		int32_t recognizedFrameCount = 0;
		float intervalTimerSeconds = 0.0f;
		bool isRecognizing = false;
		bool hasBackendFailed = false;
	};

	std::unique_ptr<IVisionBackend> CreateBackend(const VisionConfig& config) const;  // Kind から Backend を作る。
	const CameraEntry* FindCameraForRecognizer(const RecognizerEntry& recognizer) const;

	std::unordered_map<int32_t, CameraEntry> cameras_;
	std::unordered_map<int32_t, RecognizerEntry> recognizers_;
	ExternalFeatureState state_ = ExternalFeatureState::Unavailable;
	ExternalFeatureError lastError_{};
};

#pragma warning(pop)
