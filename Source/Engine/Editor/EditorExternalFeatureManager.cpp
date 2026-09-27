#include "EditorExternalFeatureManager.h"

#include "EditorComponentUtility.h"
#include "EditorInputManager.h"
#include "EditorScriptManager.h"
#include "EditorSharedState.h"
#include "Source/Engine/Core/ProjectSettings.h"
#include "Source/Engine/Haptics/FeelKitHapticBackend.h"
#include "Source/Engine/Haptics/HapticSystem.h"
#include "Source/Engine/Online/OnlineService.h"
#include "Source/Engine/Online/WinHttpOnlineBackend.h"
#include "Source/Engine/Speech/SpeechSystem.h"
#include "Source/Engine/Vision/VisionSystem.h"

#include <algorithm>
#include <cctype>
#include <memory>

namespace {
	// Inspector の Label 比較は大小文字を無視した部分一致にする。
	bool ContainsLabel(const std::string& text, const std::string& keyword) {
		if (keyword.empty()) {
			return false;
		}

		std::string loweredText = text;
		std::string loweredKeyword = keyword;

		for (char& character : loweredText) {
			character = static_cast<char>(
				std::tolower(static_cast<unsigned char>(character)));
		}

		for (char& character : loweredKeyword) {
			character = static_cast<char>(
				std::tolower(static_cast<unsigned char>(character)));
		}

		return loweredText.find(loweredKeyword) != std::string::npos;
	}
}

void EditorExternalFeatureManager::Initialize(
	EditorScene* editorScene,
	EditorInputManager* inputManager,
	EditorScriptManager* scriptManager,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	inputManager_ = inputManager;
	scriptManager_ = scriptManager;
	consoleMessages_ = consoleMessages;
	ExternalFeatureLog::Initialize(consoleMessages);
}

void EditorExternalFeatureManager::Start() {
	if (editorScene_ == nullptr || isStarted_) {
		return;
	}

	isStarted_ = true;
	speechSessions_.clear();
	visionSessions_.clear();
	hapticSources_.clear();

	const ProjectSettingsData& projectSettings = ProjectSettings::Get().GetData();

	//============================================================
	// Haptics
	//============================================================
	HapticSystem& hapticSystem = HapticSystem::Get();
	hapticSystem.SetBackend(
		std::make_unique<FeelKitHapticBackend>(&EditorSharedState::g_feelKitHaptics));
	hapticSystem.Initialize();
	hapticSystem.SetMasterIntensity(projectSettings.hapticMasterIntensity);

	//============================================================
	// Online
	//============================================================
	OnlineConfig onlineConfig{};
	onlineConfig.isEnabled = projectSettings.onlineServicesEnabled;
	onlineConfig.providerName = projectSettings.onlineProviderName;
	onlineConfig.apiBaseUrl = projectSettings.onlineApiBaseUrl;
	onlineConfig.developmentBaseUrl = projectSettings.onlineDevelopmentBaseUrl;
	onlineConfig.gameId = projectSettings.onlineGameId;
	onlineConfig.clientKey = projectSettings.onlineClientKey;
	onlineConfig.environment = projectSettings.onlineEnvironment == 1
		? OnlineEnvironment::Production
		: OnlineEnvironment::Development;
	onlineConfig.timeoutSeconds = projectSettings.onlineTimeoutSeconds;
	onlineConfig.maximumPendingRequests = projectSettings.onlineMaximumPendingRequests;

	OnlineService& onlineService = OnlineService::Get();
	onlineService.SetBackend(std::make_unique<WinHttpOnlineBackend>());
	onlineService.Configure(onlineConfig);

	if (onlineConfig.isEnabled) {
		onlineService.Initialize();

		if (onlineConfig.playerId.empty()) {
			// Player ID の生成方式はゲーム側が決める(仕様書 50 項)。
			ExternalFeatureLog::Info(
				ExternalFeatureCategory::Online,
				"Player ID が未設定です。Script の Online::SetPlayerIdentity で設定してください。");
		}
	}

	//============================================================
	// Speech
	//============================================================
	SpeechBackendKind speechBackendKind = SpeechBackendKind::Auto;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* speechComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SpeechRecognizer);

		if (speechComponent != nullptr) {
			speechBackendKind = static_cast<SpeechBackendKind>(
				(std::clamp)(speechComponent->speechBackendKind, 0, 4));
			break;
		}
	}

	SpeechSystem& speechSystem = SpeechSystem::Get();
	speechSystem.SelectBackend(speechBackendKind);
	speechSystem.SetOnSpeechRecognized([this](int32_t gameObjectId, const SpeechResult& result) {
		const EditorComponent* component = FindComponent(
			gameObjectId,
			EditorComponentType::SpeechRecognizer);

		if (component != nullptr && !component->speechRecognizedActionName.empty() && result.isFinal) {
			QueueScriptAction(gameObjectId, component->speechRecognizedActionName, result.confidence);
		}
	});
	speechSystem.SetOnKeywordRecognized(
		[this](int32_t gameObjectId, const std::string& keyword, const SpeechResult& result) {
			static_cast<void>(result);
			ExternalFeatureLog::Info(
				ExternalFeatureCategory::Speech,
				"キーワードを認識しました: " + keyword);
		});
	speechSystem.SetOnSpeechError([](int32_t gameObjectId, const ExternalFeatureError& error) {
		static_cast<void>(gameObjectId);
		ExternalFeatureLog::Error(ExternalFeatureCategory::Speech, error);
	});

	//============================================================
	// Play On Start
	//============================================================
	SyncSessions();

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* speechComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SpeechRecognizer);

		if (speechComponent != nullptr && speechComponent->isActive && speechComponent->speechStartOnPlay) {
			StartSpeechRecognition(gameObject.id);
		}

		const EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraInput);

		if (cameraComponent != nullptr && cameraComponent->isActive && cameraComponent->cameraInputStartOnPlay) {
			StartCameraCapture(gameObject.id);
		}

		const EditorComponent* recognizerComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ImageRecognizer);

		if (recognizerComponent != nullptr && recognizerComponent->isActive &&
			recognizerComponent->visionStartOnPlay) {
			StartImageRecognition(gameObject.id);
		}

		const EditorComponent* hapticComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::HapticSource);

		if (hapticComponent != nullptr && hapticComponent->isActive && hapticComponent->audioPlayOnAwake) {
			PlayHapticSource(gameObject.id);
			hapticSources_[gameObject.id].hasPlayedOnStart = true;
		}
	}
}

void EditorExternalFeatureManager::Stop() {
	if (!isStarted_) {
		return;
	}

	SpeechSystem& speechSystem = SpeechSystem::Get();
	speechSystem.SetOnSpeechRecognized(SpeechSystem::ResultCallback());
	speechSystem.SetOnKeywordRecognized(SpeechSystem::KeywordCallback());
	speechSystem.SetOnSpeechError(SpeechSystem::ErrorCallback());
	speechSystem.Shutdown();

	VisionSystem::Get().Shutdown();
	HapticSystem::Get().StopAll();
	HapticSystem::Get().Shutdown();
	OnlineService::Get().Shutdown();
	ExternalFeatureLog::Flush();

	speechSessions_.clear();
	visionSessions_.clear();
	hapticSources_.clear();
	isStarted_ = false;
}

void EditorExternalFeatureManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return;
	}

	SyncSessions();

	SpeechSystem::Get().Update(deltaTime);
	VisionSystem::Get().Update(deltaTime);
	UpdateHapticSources(deltaTime);
	HapticSystem::Get().Update(deltaTime);
	OnlineService::Get().Update(deltaTime);

	ApplySpeechToInput();
	ApplyVisionToInput();

	// Backend からの Console ログは Main Thread のここでまとめて流す。
	ExternalFeatureLog::Flush();
}

const EditorComponent* EditorExternalFeatureManager::FindComponent(
	int32_t gameObjectId,
	EditorComponentType type) const {
	if (editorScene_ == nullptr) {
		return nullptr;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return nullptr;
	}

	return EditorComponentUtility::FindComponent(*gameObject, type);
}

SpeechConfig EditorExternalFeatureManager::MakeSpeechConfig(const EditorComponent& component) const {
	SpeechConfig config{};
	config.mode = component.speechRecognitionMode == 1
		? SpeechRecognitionMode::SpeechToText
		: SpeechRecognitionMode::Keyword;
	config.backendKind = static_cast<SpeechBackendKind>((std::clamp)(component.speechBackendKind, 0, 4));
	config.language = component.speechLanguage.empty() ? std::string("ja-JP") : component.speechLanguage;
	config.microphoneDeviceName = component.speechMicrophoneDevice;
	config.confidenceThreshold = (std::clamp)(component.speechConfidenceThreshold, 0.0f, 1.0f);
	config.isContinuous = component.speechContinuousRecognition;
	config.keywords = component.speechKeywords;
	config.modelAssetPath = component.speechModelAssetPath;
	config.whisperEndOnSilence = component.speechWhisperEndOnSilence;
	config.whisperMaximumCaptureSeconds = (std::clamp)(component.speechWhisperMaximumCaptureSeconds, 0.5f, 30.0f);
	config.whisperSilenceSeconds = (std::clamp)(component.speechWhisperSilenceSeconds, 0.1f, 3.0f);
	config.whisperVoiceThreshold = (std::clamp)(component.speechWhisperVoiceThreshold, 0.001f, 1.0f);
	return config;
}

CameraInputConfig EditorExternalFeatureManager::MakeCameraConfig(const EditorComponent& component) const {
	CameraInputConfig config{};
	config.deviceName = component.cameraInputDeviceName;
	config.requestedWidth = (std::clamp)(component.cameraInputWidth, 64, 4096);
	config.requestedHeight = (std::clamp)(component.cameraInputHeight, 64, 4096);
	config.frameRateLimit = (std::clamp)(component.cameraInputFrameRateLimit, 0, 240);
	return config;
}

VisionConfig EditorExternalFeatureManager::MakeVisionConfig(const EditorComponent& component) const {
	VisionConfig config{};
	config.mode = static_cast<VisionRecognitionMode>((std::clamp)(component.visionRecognitionMode, 0, 6));
	config.backendKind = static_cast<VisionBackendKind>((std::clamp)(component.visionBackendKind, 0, 5));
	config.modelAssetPath = component.visionModelAssetPath;
	config.labelAssetPath = component.visionLabelAssetPath;
	config.confidenceThreshold = (std::clamp)(component.visionConfidenceThreshold, 0.0f, 1.0f);
	config.recognitionIntervalSeconds = (std::clamp)(component.visionRecognitionInterval, 0.0f, 10.0f);
	config.targetColorR = component.visionTargetColor.x;
	config.targetColorG = component.visionTargetColor.y;
	config.targetColorB = component.visionTargetColor.z;
	config.colorTolerance = (std::clamp)(component.visionColorTolerance, 0.0f, 1.0f);
	config.minimumAreaRatio = (std::clamp)(component.visionMinimumAreaRatio, 0.0f, 1.0f);
	config.motionThreshold = (std::clamp)(component.visionMotionThreshold, 0.0f, 1.0f);
	return config;
}

HapticData EditorExternalFeatureManager::MakeHapticData(const EditorComponent& component) const {
	HapticData data{};
	HapticSystem& hapticSystem = HapticSystem::Get();
	HapticClipData clip{};

	if (!component.hapticClipAssetPath.empty() &&
		hapticSystem.LoadClip(component.hapticClipAssetPath, clip)) {
		// Clip Asset があればそちらの値を基準にする(仕様書 64〜65 項)。
		data = clip.ToHapticData();
	}
	else {
		data.intensity = (std::clamp)(component.hapticStrength, 0.0f, 1.0f);
		data.durationSeconds =
			(std::max)(static_cast<float>(component.hapticDurationMs) / 1000.0f, 0.01f);
		data.frequency = (std::clamp)(component.hapticFrequency, 0.0f, 200.0f);
		data.pattern = static_cast<HapticPattern>((std::clamp)(component.hapticPattern, 0, 4));
		data.channel = static_cast<HapticChannel>((std::clamp)(component.hapticChannel, 0, 2));
	}

	data.isLooping = component.hapticLoop;

	// Audio Reactive Haptics(仕様書 72〜73 項)。
	if (component.hapticAudioReactive && !component.assetPath.empty()) {
		HapticAudioAnalysis analysis{};

		if (hapticSystem.TryAnalyzeAudioFile(component.assetPath, analysis) && analysis.isValid) {
			data.intensity = HapticSystem::MakeIntensityFromAudio(
				analysis,
				(std::clamp)(component.hapticAudioFrequencyRange, 0, 2),
				component.hapticAudioSensitivity,
				component.hapticAudioIntensityScale);

			if (analysis.durationSeconds > 0.0f) {
				data.durationSeconds = (std::min)(analysis.durationSeconds, 4.0f);
			}
		}
	}

	return data;
}

void EditorExternalFeatureManager::SyncSessions() {
	if (editorScene_ == nullptr) {
		return;
	}

	SpeechSystem& speechSystem = SpeechSystem::Get();
	VisionSystem& visionSystem = VisionSystem::Get();

	std::vector<int32_t> liveSpeechIds;
	std::vector<int32_t> liveVisionIds;
	std::vector<int32_t> liveCameraIds;
	std::vector<int32_t> liveHapticIds;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const bool isObjectActive = gameObject.isActive;

		const EditorComponent* speechComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SpeechRecognizer);

		if (speechComponent != nullptr && speechComponent->isActive && isObjectActive) {
			speechSystem.RegisterSession(gameObject.id, MakeSpeechConfig(*speechComponent));
			liveSpeechIds.push_back(gameObject.id);
		}

		const EditorComponent* cameraComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::CameraInput);

		if (cameraComponent != nullptr && cameraComponent->isActive && isObjectActive) {
			liveCameraIds.push_back(gameObject.id);
		}

		const EditorComponent* recognizerComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ImageRecognizer);

		if (recognizerComponent != nullptr && recognizerComponent->isActive && isObjectActive) {
			visionSystem.RegisterRecognizer(
				gameObject.id,
				recognizerComponent->visionCameraGameObjectId,
				MakeVisionConfig(*recognizerComponent));
			liveVisionIds.push_back(gameObject.id);
		}

		const EditorComponent* hapticComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::HapticSource);

		if (hapticComponent != nullptr && hapticComponent->isActive && isObjectActive) {
			liveHapticIds.push_back(gameObject.id);
		}
	}

	// 無くなった / 無効になった Component の Session を片付ける。
	for (auto sessionIt = speechSessions_.begin(); sessionIt != speechSessions_.end();) {
		if (std::find(liveSpeechIds.begin(), liveSpeechIds.end(), sessionIt->first) == liveSpeechIds.end()) {
			speechSystem.UnregisterSession(sessionIt->first);
			sessionIt = speechSessions_.erase(sessionIt);
			continue;
		}

		++sessionIt;
	}

	for (const int32_t gameObjectId : liveSpeechIds) {
		speechSessions_.try_emplace(gameObjectId);
	}

	for (auto sessionIt = visionSessions_.begin(); sessionIt != visionSessions_.end();) {
		if (std::find(liveVisionIds.begin(), liveVisionIds.end(), sessionIt->first) == liveVisionIds.end()) {
			visionSystem.UnregisterRecognizer(sessionIt->first);
			sessionIt = visionSessions_.erase(sessionIt);
			continue;
		}

		++sessionIt;
	}

	for (const int32_t gameObjectId : liveVisionIds) {
		visionSessions_.try_emplace(gameObjectId);
	}

	for (const int32_t cameraGameObjectId : visionSystem.GetCameraGameObjectIds()) {
		if (std::find(liveCameraIds.begin(), liveCameraIds.end(), cameraGameObjectId) == liveCameraIds.end()) {
			visionSystem.CloseCamera(cameraGameObjectId);
		}
	}

	for (auto hapticIt = hapticSources_.begin(); hapticIt != hapticSources_.end();) {
		if (std::find(liveHapticIds.begin(), liveHapticIds.end(), hapticIt->first) == liveHapticIds.end()) {
			HapticSystem::Get().StopGameObject(hapticIt->first);
			hapticIt = hapticSources_.erase(hapticIt);
			continue;
		}

		++hapticIt;
	}
}

void EditorExternalFeatureManager::ApplySpeechToInput() {
	if (inputManager_ == nullptr) {
		return;
	}

	SpeechSystem& speechSystem = SpeechSystem::Get();

	for (const std::pair<const int32_t, SpeechSessionState>& sessionPair : speechSessions_) {
		const EditorComponent* component = FindComponent(
			sessionPair.first,
			EditorComponentType::SpeechRecognizer);

		if (component == nullptr) {
			continue;
		}

		// Keyword → Input Action。認識したフレームだけ押下として流す(仕様書 9 項)。
		for (size_t keywordIndex = 0u; keywordIndex < component->speechKeywords.size(); ++keywordIndex) {
			if (keywordIndex >= component->speechKeywordActionNames.size()) {
				break;
			}

			const std::string& actionName = component->speechKeywordActionNames[keywordIndex];

			if (actionName.empty()) {
				continue;
			}

			const std::string& keyword = component->speechKeywords[keywordIndex];
			const bool wasRecognized = speechSystem.WasKeywordRecognized(sessionPair.first, keyword);
			inputManager_->InjectActionButton(
				sessionPair.first,
				component->speechInputActionMapName,
				actionName,
				wasRecognized,
				"Speech/" + keyword);
		}
	}
}

void EditorExternalFeatureManager::ApplyVisionToInput() {
	VisionSystem& visionSystem = VisionSystem::Get();

	for (std::pair<const int32_t, VisionSessionState>& sessionPair : visionSessions_) {
		const EditorComponent* component = FindComponent(
			sessionPair.first,
			EditorComponentType::ImageRecognizer);

		if (component == nullptr) {
			continue;
		}

		VisionResult result{};

		if (!visionSystem.TryGetResult(sessionPair.first, result) || !result.isValid) {
			sessionPair.second.wasTriggerActive = false;
			continue;
		}

		bool isTriggerActive = false;
		float triggerValue = 0.0f;

		switch ((std::clamp)(component->visionInputTriggerMode, 0, 6)) {
		case 0: {
			// 検出ラベル一致(物体検出 / 画像分類 / 顔検出)。
			for (const ObjectDetectionResult& object : result.objects) {
				if (ContainsLabel(object.label, component->visionInputTriggerLabel)) {
					isTriggerActive = true;
					triggerValue = object.confidence;
					break;
				}
			}

			if (!isTriggerActive) {
				for (const ImageClassificationResult& classification : result.classifications) {
					if (ContainsLabel(classification.label, component->visionInputTriggerLabel)) {
						isTriggerActive = true;
						triggerValue = classification.confidence;
						break;
					}
				}
			}

			if (!isTriggerActive && !result.faces.empty() &&
				(component->visionInputTriggerLabel.empty() ||
				 ContainsLabel("face", component->visionInputTriggerLabel))) {
				isTriggerActive = true;
				triggerValue = result.faces.front().confidence;
			}

			break;
		}
		case 1:
			isTriggerActive = result.headPose.isValid &&
				result.headPose.yaw > component->visionInputAngleThreshold;
			triggerValue = result.headPose.yaw;
			break;
		case 2:
			isTriggerActive = result.headPose.isValid &&
				result.headPose.yaw < -component->visionInputAngleThreshold;
			triggerValue = result.headPose.yaw;
			break;
		case 3:
			isTriggerActive = result.headPose.isValid &&
				result.headPose.pitch > component->visionInputAngleThreshold;
			triggerValue = result.headPose.pitch;
			break;
		case 4:
			isTriggerActive = result.headPose.isValid &&
				result.headPose.pitch < -component->visionInputAngleThreshold;
			triggerValue = result.headPose.pitch;
			break;
		case 5:
			isTriggerActive = result.motion.motion;
			triggerValue = result.motion.motionMagnitude;
			break;
		case 6:
		default:
			isTriggerActive = result.colorTracking.isDetected;
			triggerValue = result.colorTracking.areaRatio;
			break;
		}

		if (inputManager_ != nullptr && !component->visionInputActionName.empty()) {
			inputManager_->InjectActionButton(
				sessionPair.first,
				component->visionInputActionMapName,
				component->visionInputActionName,
				isTriggerActive,
				"Vision/" + std::string(ToDisplayString(result.mode)));

			// 検出位置も Vector2 として流し、照準などへそのまま使えるようにする。
			if (isTriggerActive) {
				float positionX = 0.0f;
				float positionY = 0.0f;

				if (!result.objects.empty()) {
					positionX = result.objects.front().x + result.objects.front().width * 0.5f;
					positionY = result.objects.front().y + result.objects.front().height * 0.5f;
				}
				else if (!result.faces.empty()) {
					positionX = result.faces.front().x + result.faces.front().width * 0.5f;
					positionY = result.faces.front().y + result.faces.front().height * 0.5f;
				}
				else if (result.colorTracking.isDetected) {
					positionX = result.colorTracking.centerX;
					positionY = result.colorTracking.centerY;
				}
				else if (result.motion.motion) {
					positionX = result.motion.centerX;
					positionY = result.motion.centerY;
				}

				// 画面中央を原点にした -1〜1 へ直す。
				inputManager_->InjectActionVector2(
					sessionPair.first,
					component->visionInputActionMapName,
					component->visionInputActionName + "Position",
					positionX * 2.0f - 1.0f,
					1.0f - positionY * 2.0f,
					"Vision/Position");
			}
		}

		if (isTriggerActive && !sessionPair.second.wasTriggerActive &&
			!component->visionDetectedActionName.empty()) {
			QueueScriptAction(sessionPair.first, component->visionDetectedActionName, triggerValue);
		}

		sessionPair.second.wasTriggerActive = isTriggerActive;
	}
}

void EditorExternalFeatureManager::UpdateHapticSources(float deltaTime) {
	HapticSystem& hapticSystem = HapticSystem::Get();

	for (std::pair<const int32_t, HapticSourceState>& hapticPair : hapticSources_) {
		const EditorComponent* component = FindComponent(
			hapticPair.first,
			EditorComponentType::HapticSource);

		if (component == nullptr) {
			continue;
		}

		if (!component->hapticLoop) {
			continue;
		}

		// Loop 再生は Voice が消えていたら鳴らし直す。
		if (hapticSystem.IsPlaying(hapticPair.second.handle)) {
			continue;
		}

		hapticPair.second.loopTimerSeconds -= (std::max)(deltaTime, 0.0f);

		if (hapticPair.second.loopTimerSeconds > 0.0f) {
			continue;
		}

		hapticPair.second.loopTimerSeconds = 0.05f;
		hapticPair.second.handle = PlayHapticSource(hapticPair.first);
	}
}

void EditorExternalFeatureManager::QueueScriptAction(
	int32_t gameObjectId,
	const std::string& actionName,
	float value) const {
	if (scriptManager_ == nullptr || actionName.empty()) {
		return;
	}

	scriptManager_->QueueActionEvent(
		gameObjectId,
		actionName,
		EditorScriptInputValueTypeButton,
		value,
		EditorScriptVector2{});
}

//================================================================
// Script API / Inspector から使う操作
//================================================================

bool EditorExternalFeatureManager::StartSpeechRecognition(int32_t gameObjectId) {
	const EditorComponent* component = FindComponent(gameObjectId, EditorComponentType::SpeechRecognizer);

	if (component == nullptr) {
		return false;
	}

	SpeechSystem& speechSystem = SpeechSystem::Get();
	speechSystem.RegisterSession(gameObjectId, MakeSpeechConfig(*component));
	speechSessions_.try_emplace(gameObjectId);
	return speechSystem.StartRecognition(gameObjectId);
}

bool EditorExternalFeatureManager::StopSpeechRecognition(int32_t gameObjectId) {
	return SpeechSystem::Get().StopRecognition(gameObjectId);
}

bool EditorExternalFeatureManager::IsSpeechRecognizing(int32_t gameObjectId) const {
	return SpeechSystem::Get().IsRecognizing(gameObjectId);
}

bool EditorExternalFeatureManager::TryGetSpeechResult(
	int32_t gameObjectId,
	SpeechResult& outResult) const {
	return SpeechSystem::Get().TryGetLatestResult(gameObjectId, outResult);
}

bool EditorExternalFeatureManager::WasSpeechKeywordRecognized(
	int32_t gameObjectId,
	const std::string& keyword) const {
	return SpeechSystem::Get().WasKeywordRecognized(gameObjectId, keyword);
}

bool EditorExternalFeatureManager::StartCameraCapture(int32_t gameObjectId) {
	const EditorComponent* component = FindComponent(gameObjectId, EditorComponentType::CameraInput);

	if (component == nullptr) {
		return false;
	}

	return VisionSystem::Get().OpenCamera(gameObjectId, MakeCameraConfig(*component));
}

bool EditorExternalFeatureManager::StopCameraCapture(int32_t gameObjectId) {
	VisionSystem::Get().CloseCamera(gameObjectId);
	return true;
}

bool EditorExternalFeatureManager::StartImageRecognition(int32_t gameObjectId) {
	const EditorComponent* component = FindComponent(gameObjectId, EditorComponentType::ImageRecognizer);

	if (component == nullptr) {
		return false;
	}

	VisionSystem& visionSystem = VisionSystem::Get();
	visionSystem.RegisterRecognizer(
		gameObjectId,
		component->visionCameraGameObjectId,
		MakeVisionConfig(*component));
	visionSessions_.try_emplace(gameObjectId);
	return visionSystem.StartRecognition(gameObjectId);
}

bool EditorExternalFeatureManager::StopImageRecognition(int32_t gameObjectId) {
	return VisionSystem::Get().StopRecognition(gameObjectId);
}

bool EditorExternalFeatureManager::TryGetVisionResult(
	int32_t gameObjectId,
	VisionResult& outResult) const {
	return VisionSystem::Get().TryGetResult(gameObjectId, outResult);
}

HapticHandle EditorExternalFeatureManager::PlayHapticSource(int32_t gameObjectId) {
	const EditorComponent* component = FindComponent(gameObjectId, EditorComponentType::HapticSource);

	if (component == nullptr) {
		return kInvalidHapticHandle;
	}

	HapticSystem& hapticSystem = HapticSystem::Get();
	const HapticData data = MakeHapticData(*component);

	// Audio Reactive で音源が指定されている場合は Backend 固有変換を優先する。
	const HapticHandle handle = component->hapticAudioReactive && !component->assetPath.empty()
		? hapticSystem.PlayFromAudioFile(component->assetPath, data, gameObjectId)
		: hapticSystem.Play(data, gameObjectId, component->hapticClipAssetPath);

	HapticSourceState& state = hapticSources_[gameObjectId];
	state.handle = handle;
	return handle;
}

bool EditorExternalFeatureManager::StopHapticSource(int32_t gameObjectId) {
	HapticSystem::Get().StopGameObject(gameObjectId);
	const auto stateIt = hapticSources_.find(gameObjectId);

	if (stateIt != hapticSources_.end()) {
		stateIt->second.handle = kInvalidHapticHandle;
	}

	return true;
}

HapticHandle EditorExternalFeatureManager::PlayHapticFromImpulse(int32_t gameObjectId, float impulse) {
	const EditorComponent* component = FindComponent(gameObjectId, EditorComponentType::HapticSource);

	if (component == nullptr || !component->hapticPhysicsReactive) {
		return kInvalidHapticHandle;
	}

	HapticData data = MakeHapticData(*component);
	data.isLooping = false;
	data.intensity = HapticSystem::MakeIntensityFromImpulse(impulse, component->hapticMaximumImpulse);

	if (data.intensity <= 0.0f) {
		return kInvalidHapticHandle;
	}

	return HapticSystem::Get().Play(data, gameObjectId, "PhysicsImpact");
}

HapticHandle EditorExternalFeatureManager::PreviewHapticComponent(
	const EditorComponent& hapticComponent,
	int32_t gameObjectId) {
	HapticSystem& hapticSystem = HapticSystem::Get();

	// Play していない Editor でも試せるよう、必要なら Backend をここで開く(仕様書 80 項)。
	if (hapticSystem.GetState() == ExternalFeatureState::Unavailable) {
		hapticSystem.SetBackend(
			std::make_unique<FeelKitHapticBackend>(&EditorSharedState::g_feelKitHaptics));
		hapticSystem.Initialize();
	}

	HapticData data = MakeHapticData(hapticComponent);
	data.isLooping = false;
	return hapticSystem.Play(data, gameObjectId, "Preview");
}
