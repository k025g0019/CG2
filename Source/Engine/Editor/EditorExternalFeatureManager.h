#pragma once

#include "EditorScene.h"
#include "Source/Engine/Haptics/HapticTypes.h"
#include "Source/Engine/Speech/SpeechTypes.h"
#include "Source/Engine/Vision/VisionTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class EditorInputManager;
class EditorScriptManager;

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 外部認識・オンライン連携 Component の実行担当
//================================================================
// SpeechRecognizer / CameraInput / ImageRecognizer / HapticSource の
// Component 設定を、Speech / Vision / Haptics / Online の各 System へ
// 渡して実行する。外部ライブラリや Backend はここでも直接触らない
// (Game Component → Engine API → Interface → Backend の順を守る)。

class EditorExternalFeatureManager {
public:
	EditorExternalFeatureManager() = default;
	~EditorExternalFeatureManager() = default;
	EditorExternalFeatureManager(const EditorExternalFeatureManager&) = delete;
	EditorExternalFeatureManager& operator=(const EditorExternalFeatureManager&) = delete;
	EditorExternalFeatureManager(EditorExternalFeatureManager&&) = delete;
	EditorExternalFeatureManager& operator=(EditorExternalFeatureManager&&) = delete;

	void Initialize(
		EditorScene* editorScene,
		EditorInputManager* inputManager,
		EditorScriptManager* scriptManager,
		std::vector<std::string>* consoleMessages);
	void Start();  // Play 開始。Project Settings を各 System へ流し、Play On Start を処理する。
	void Update(float deltaTime);  // Session 更新、結果取得、Input Action 反映、Script 通知。
	void Stop();   // Play 停止。Device を離し、Session を捨てる。

	//============================================================
	// Script API / Inspector から使う操作
	//============================================================
	bool StartSpeechRecognition(int32_t gameObjectId);
	bool StopSpeechRecognition(int32_t gameObjectId);
	bool IsSpeechRecognizing(int32_t gameObjectId) const;
	bool TryGetSpeechResult(int32_t gameObjectId, SpeechResult& outResult) const;
	bool WasSpeechKeywordRecognized(int32_t gameObjectId, const std::string& keyword) const;

	bool StartCameraCapture(int32_t gameObjectId);
	bool StopCameraCapture(int32_t gameObjectId);
	bool StartImageRecognition(int32_t gameObjectId);
	bool StopImageRecognition(int32_t gameObjectId);
	bool TryGetVisionResult(int32_t gameObjectId, VisionResult& outResult) const;

	HapticHandle PlayHapticSource(int32_t gameObjectId);  // Component 設定から振動を再生する。
	bool StopHapticSource(int32_t gameObjectId);
	// 衝突 Impulse から振動させる(仕様書 74 項)。Physics Reactive が有効な Component だけが鳴る。
	HapticHandle PlayHapticFromImpulse(int32_t gameObjectId, float impulse);

	// Editor Preview 用。Play 中でなくても Component 設定で 1 回鳴らす(仕様書 80 項)。
	HapticHandle PreviewHapticComponent(const EditorComponent& hapticComponent, int32_t gameObjectId);

private:
	struct SpeechSessionState {
		bool hasStarted = false;  // Play On Start を 1 回だけ処理するための印。
	};

	struct VisionSessionState {
		bool hasStarted = false;
		bool wasTriggerActive = false;  // Input Action の押下エッジ判定用。
	};

	struct HapticSourceState {
		HapticHandle handle = kInvalidHapticHandle;
		float loopTimerSeconds = 0.0f;
		bool hasPlayedOnStart = false;
	};

	SpeechConfig MakeSpeechConfig(const EditorComponent& component) const;
	CameraInputConfig MakeCameraConfig(const EditorComponent& component) const;
	VisionConfig MakeVisionConfig(const EditorComponent& component) const;
	HapticData MakeHapticData(const EditorComponent& component) const;

	void SyncSessions();  // Scene の Component 構成と Session を合わせる。
	void ApplySpeechToInput();
	void ApplyVisionToInput();
	void UpdateHapticSources(float deltaTime);
	void QueueScriptAction(int32_t gameObjectId, const std::string& actionName, float value) const;
	const EditorComponent* FindComponent(int32_t gameObjectId, EditorComponentType type) const;

	EditorScene* editorScene_ = nullptr;
	EditorInputManager* inputManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	std::vector<std::string>* consoleMessages_ = nullptr;
	std::unordered_map<int32_t, SpeechSessionState> speechSessions_;
	std::unordered_map<int32_t, VisionSessionState> visionSessions_;
	std::unordered_map<int32_t, HapticSourceState> hapticSources_;
	bool isStarted_ = false;
};

#pragma warning(pop)
