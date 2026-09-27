#include "VisionSystem.h"

#include "BuiltinVisionBackend.h"
#include "MediaFoundationCameraSource.h"
#include "OnnxVisionBackend.h"

#include <algorithm>

VisionSystem& VisionSystem::Get() {
	static VisionSystem instance;
	return instance;
}

void VisionSystem::Shutdown() {
	for (std::pair<const int32_t, RecognizerEntry>& recognizerPair : recognizers_) {
		if (recognizerPair.second.backend != nullptr) {
			recognizerPair.second.backend->Shutdown();
		}
	}

	recognizers_.clear();

	for (std::pair<const int32_t, CameraEntry>& cameraPair : cameras_) {
		if (cameraPair.second.source != nullptr) {
			cameraPair.second.source->Close();
		}
	}

	cameras_.clear();
	state_ = ExternalFeatureState::Unavailable;
}

bool VisionSystem::OpenCamera(int32_t gameObjectId, const CameraInputConfig& config) {
	CameraEntry& camera = cameras_[gameObjectId];

	// 同じ設定で既に開いていれば何もしない。
	if (camera.source != nullptr && camera.source->IsOpen() &&
		camera.config.deviceName == config.deviceName &&
		camera.config.requestedWidth == config.requestedWidth &&
		camera.config.requestedHeight == config.requestedHeight &&
		camera.config.frameRateLimit == config.frameRateLimit) {
		return true;
	}

	if (camera.source != nullptr) {
		camera.source->Close();
	}

	camera.source = std::make_unique<MediaFoundationCameraSource>();
	camera.config = config;
	camera.capturedFrameCount = 0;
	camera.fpsFrameCount = 0;
	camera.fpsTimerSeconds = 0.0f;
	camera.measuredFps = 0.0f;

	if (!camera.source->Open(config)) {
		camera.lastError = camera.source->GetLastError();
		camera.state = ExternalFeatureState::Unavailable;
		lastError_ = camera.lastError;
		ExternalFeatureLog::Error(ExternalFeatureCategory::Vision, camera.lastError);
		camera.source.reset();
		return false;
	}

	camera.state = ExternalFeatureState::Running;
	camera.lastError.Clear();
	ExternalFeatureLog::Info(
		ExternalFeatureCategory::Vision,
		"Camera を開きました: " + camera.source->GetActiveDeviceName());
	return true;
}

void VisionSystem::CloseCamera(int32_t gameObjectId) {
	const auto cameraIt = cameras_.find(gameObjectId);

	if (cameraIt == cameras_.end()) {
		return;
	}

	if (cameraIt->second.source != nullptr) {
		cameraIt->second.source->Close();
	}

	cameras_.erase(cameraIt);
}

bool VisionSystem::IsCameraOpen(int32_t gameObjectId) const {
	const auto cameraIt = cameras_.find(gameObjectId);
	return cameraIt != cameras_.end() &&
		cameraIt->second.source != nullptr &&
		cameraIt->second.source->IsOpen();
}

bool VisionSystem::HasCamera(int32_t gameObjectId) const {
	return cameras_.find(gameObjectId) != cameras_.end();
}

const ImageFrame* VisionSystem::GetLatestFrame(int32_t gameObjectId) const {
	const auto cameraIt = cameras_.find(gameObjectId);

	if (cameraIt == cameras_.end() || !cameraIt->second.latestFrame.IsValid()) {
		return nullptr;
	}

	return &cameraIt->second.latestFrame;
}

void VisionSystem::EnumerateCameraDevices(std::vector<CameraDeviceInfo>& outDevices) const {
	// Device 列挙は Camera を開かなくても行えるようにする(Inspector の一覧用)。
	MediaFoundationCameraSource enumerationSource;
	enumerationSource.EnumerateDevices(outDevices);
}

std::vector<int32_t> VisionSystem::GetCameraGameObjectIds() const {
	std::vector<int32_t> gameObjectIds;
	gameObjectIds.reserve(cameras_.size());

	for (const std::pair<const int32_t, CameraEntry>& cameraPair : cameras_) {
		gameObjectIds.push_back(cameraPair.first);
	}

	std::sort(gameObjectIds.begin(), gameObjectIds.end());
	return gameObjectIds;
}

VisionRuntimeStatus VisionSystem::GetCameraStatus(int32_t gameObjectId) const {
	VisionRuntimeStatus status{};
	const auto cameraIt = cameras_.find(gameObjectId);

	if (cameraIt == cameras_.end()) {
		return status;
	}

	const CameraEntry& camera = cameraIt->second;
	status.cameraState = camera.state;
	status.cameraDeviceName = camera.source != nullptr
		? camera.source->GetActiveDeviceName()
		: std::string();
	status.frameWidth = camera.latestFrame.width;
	status.frameHeight = camera.latestFrame.height;
	status.capturedFrameCount = camera.capturedFrameCount;
	status.captureFps = camera.measuredFps;
	status.lastError = camera.lastError;
	return status;
}

std::unique_ptr<IVisionBackend> VisionSystem::CreateBackend(const VisionConfig& config) const {
	const bool needsModel =
		config.mode == VisionRecognitionMode::ObjectDetection ||
		config.mode == VisionRecognitionMode::ImageClassification ||
		config.mode == VisionRecognitionMode::FaceDetection;

	switch (config.backendKind) {
	case VisionBackendKind::Builtin:
		return std::make_unique<BuiltinVisionBackend>();
	case VisionBackendKind::OnnxRuntime:
		return std::make_unique<OnnxVisionBackend>();
	case VisionBackendKind::Auto:
		// モデルが要るモードは ONNX、色/動きは内蔵実装を使う。
		if (needsModel) {
			return std::make_unique<OnnxVisionBackend>();
		}

		return std::make_unique<BuiltinVisionBackend>();
	case VisionBackendKind::OpenCv:
	case VisionBackendKind::MediaPipe:
	case VisionBackendKind::None:
	default:
		// 未実装 Backend は別実装へ勝手に置き換えない(仕様書 85 項)。
		return nullptr;
	}
}

void VisionSystem::RegisterRecognizer(
	int32_t gameObjectId,
	int32_t cameraGameObjectId,
	const VisionConfig& config) {
	RecognizerEntry& recognizer = recognizers_[gameObjectId];
	recognizer.cameraGameObjectId = cameraGameObjectId;

	const bool needsNewBackend =
		recognizer.backend == nullptr ||
		recognizer.config.backendKind != config.backendKind ||
		(recognizer.config.mode != config.mode &&
		 !recognizer.backend->SupportsMode(config.mode));

	recognizer.config = config;

	if (needsNewBackend) {
		if (recognizer.backend != nullptr) {
			recognizer.backend->Shutdown();
		}

		recognizer.backend = CreateBackend(config);
		recognizer.hasBackendFailed = false;

		if (recognizer.backend != nullptr && !recognizer.backend->Initialize()) {
			lastError_ = recognizer.backend->GetLastError();
			recognizer.hasBackendFailed = true;
			ExternalFeatureLog::Error(ExternalFeatureCategory::Vision, lastError_);
		}
	}

	if (recognizer.backend != nullptr && !recognizer.hasBackendFailed) {
		recognizer.backend->ApplyConfig(config);
	}
}

void VisionSystem::UnregisterRecognizer(int32_t gameObjectId) {
	const auto recognizerIt = recognizers_.find(gameObjectId);

	if (recognizerIt == recognizers_.end()) {
		return;
	}

	if (recognizerIt->second.backend != nullptr) {
		recognizerIt->second.backend->Shutdown();
	}

	recognizers_.erase(recognizerIt);
}

bool VisionSystem::StartRecognition(int32_t gameObjectId) {
	const auto recognizerIt = recognizers_.find(gameObjectId);

	if (recognizerIt == recognizers_.end()) {
		return false;
	}

	recognizerIt->second.isRecognizing = true;
	recognizerIt->second.intervalTimerSeconds = 0.0f;
	return true;
}

bool VisionSystem::StopRecognition(int32_t gameObjectId) {
	const auto recognizerIt = recognizers_.find(gameObjectId);

	if (recognizerIt == recognizers_.end()) {
		return false;
	}

	recognizerIt->second.isRecognizing = false;
	return true;
}

bool VisionSystem::IsRecognizing(int32_t gameObjectId) const {
	const auto recognizerIt = recognizers_.find(gameObjectId);
	return recognizerIt != recognizers_.end() && recognizerIt->second.isRecognizing;
}

bool VisionSystem::HasRecognizer(int32_t gameObjectId) const {
	return recognizers_.find(gameObjectId) != recognizers_.end();
}

bool VisionSystem::TryGetResult(int32_t gameObjectId, VisionResult& outResult) const {
	const auto recognizerIt = recognizers_.find(gameObjectId);

	if (recognizerIt == recognizers_.end()) {
		return false;
	}

	outResult = recognizerIt->second.result;
	return true;
}

VisionRuntimeStatus VisionSystem::GetRecognizerStatus(int32_t gameObjectId) const {
	VisionRuntimeStatus status{};
	const auto recognizerIt = recognizers_.find(gameObjectId);

	if (recognizerIt == recognizers_.end()) {
		return status;
	}

	const RecognizerEntry& recognizer = recognizerIt->second;
	status.backendName = recognizer.backend != nullptr ? recognizer.backend->GetName() : "なし";
	status.recognitionState = recognizer.backend == nullptr
		? ExternalFeatureState::Unavailable
		: recognizer.result.state;
	status.recognizedFrameCount = recognizer.recognizedFrameCount;
	status.inferenceMilliseconds = recognizer.result.inferenceMilliseconds;
	status.lastError = recognizer.result.error.HasError()
		? recognizer.result.error
		: (recognizer.backend != nullptr ? recognizer.backend->GetLastError() : lastError_);

	const CameraEntry* camera = FindCameraForRecognizer(recognizer);

	if (camera != nullptr) {
		status.cameraState = camera->state;
		status.cameraDeviceName = camera->source != nullptr
			? camera->source->GetActiveDeviceName()
			: std::string();
		status.frameWidth = camera->latestFrame.width;
		status.frameHeight = camera->latestFrame.height;
		status.capturedFrameCount = camera->capturedFrameCount;
		status.captureFps = camera->measuredFps;
	}

	return status;
}

std::vector<int32_t> VisionSystem::GetRecognizerGameObjectIds() const {
	std::vector<int32_t> gameObjectIds;
	gameObjectIds.reserve(recognizers_.size());

	for (const std::pair<const int32_t, RecognizerEntry>& recognizerPair : recognizers_) {
		gameObjectIds.push_back(recognizerPair.first);
	}

	std::sort(gameObjectIds.begin(), gameObjectIds.end());
	return gameObjectIds;
}

const VisionSystem::CameraEntry* VisionSystem::FindCameraForRecognizer(
	const RecognizerEntry& recognizer) const {
	if (recognizer.cameraGameObjectId >= 0) {
		const auto cameraIt = cameras_.find(recognizer.cameraGameObjectId);
		return cameraIt == cameras_.end() ? nullptr : &cameraIt->second;
	}

	// Camera 指定が無い場合は、開いている Camera のうち ID が小さいものを使う。
	const CameraEntry* selectedCamera = nullptr;
	int32_t selectedGameObjectId = 0;

	for (const std::pair<const int32_t, CameraEntry>& cameraPair : cameras_) {
		if (cameraPair.second.source == nullptr || !cameraPair.second.source->IsOpen()) {
			continue;
		}

		if (selectedCamera == nullptr || cameraPair.first < selectedGameObjectId) {
			selectedCamera = &cameraPair.second;
			selectedGameObjectId = cameraPair.first;
		}
	}

	return selectedCamera;
}

void VisionSystem::Update(float deltaTime) {
	const float advanceSeconds = (std::max)(deltaTime, 0.0f);
	bool hasRunningFeature = false;

	for (std::pair<const int32_t, CameraEntry>& cameraPair : cameras_) {
		CameraEntry& camera = cameraPair.second;

		if (camera.source == nullptr) {
			camera.state = ExternalFeatureState::Unavailable;
			continue;
		}

		ImageFrame frame{};

		if (camera.source->TryGetFrame(frame)) {
			camera.latestFrame = std::move(frame);
			camera.capturedFrameCount += 1;
			camera.fpsFrameCount += 1;
		}

		camera.fpsTimerSeconds += advanceSeconds;

		if (camera.fpsTimerSeconds >= 1.0f) {
			camera.measuredFps = static_cast<float>(camera.fpsFrameCount) / camera.fpsTimerSeconds;
			camera.fpsFrameCount = 0;
			camera.fpsTimerSeconds = 0.0f;
		}

		const ExternalFeatureError cameraError = camera.source->GetLastError();

		if (cameraError.HasError()) {
			camera.lastError = cameraError;
			camera.state = ExternalFeatureState::Error;
		}
		else {
			camera.state = camera.source->IsOpen()
				? ExternalFeatureState::Running
				: ExternalFeatureState::Ready;
		}

		hasRunningFeature = hasRunningFeature || camera.state == ExternalFeatureState::Running;
	}

	for (std::pair<const int32_t, RecognizerEntry>& recognizerPair : recognizers_) {
		RecognizerEntry& recognizer = recognizerPair.second;

		if (!recognizer.isRecognizing) {
			continue;
		}

		if (recognizer.backend == nullptr) {
			// Backend が用意できない場合は Unavailable を返し続ける。
			recognizer.result = VisionResult{};
			recognizer.result.mode = recognizer.config.mode;
			recognizer.result.state = ExternalFeatureState::Unavailable;
			recognizer.result.error.code = -20;
			recognizer.result.error.message =
				std::string("選択した Backend (") + ToDisplayString(recognizer.config.backendKind) +
				") はまだ利用できません。";
			continue;
		}

		recognizer.intervalTimerSeconds -= advanceSeconds;

		if (recognizer.intervalTimerSeconds > 0.0f) {
			continue;
		}

		recognizer.intervalTimerSeconds =
			(std::max)(recognizer.config.recognitionIntervalSeconds, 0.0f);

		const CameraEntry* camera = FindCameraForRecognizer(recognizer);

		if (camera == nullptr || !camera->latestFrame.IsValid()) {
			recognizer.result.state = ExternalFeatureState::Ready;
			recognizer.result.error.code = -21;
			recognizer.result.error.message = "Camera 映像がまだ取得できていません。";
			continue;
		}

		if (camera->latestFrame.frameIndex == recognizer.processedFrameIndex) {
			continue;  // 同じフレームを二度推論しない。
		}

		recognizer.processedFrameIndex = camera->latestFrame.frameIndex;
		recognizer.backend->ProcessFrame(camera->latestFrame);
		recognizer.result = recognizer.backend->GetResult();
		recognizer.recognizedFrameCount += 1;

		if (recognizer.result.error.HasError()) {
			lastError_ = recognizer.result.error;
		}

		hasRunningFeature = hasRunningFeature ||
			recognizer.result.state == ExternalFeatureState::Running;
	}

	if (hasRunningFeature) {
		state_ = ExternalFeatureState::Running;
	}
	else if (!cameras_.empty() || !recognizers_.empty()) {
		state_ = ExternalFeatureState::Ready;
	}
	else {
		state_ = ExternalFeatureState::Unavailable;
	}
}

ExternalFeatureState VisionSystem::GetState() const {
	return state_;
}

ExternalFeatureError VisionSystem::GetLastError() const {
	return lastError_;
}
