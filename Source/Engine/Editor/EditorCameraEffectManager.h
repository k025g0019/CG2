#pragma once

#include "Source/Engine/Core/EditorCommonTypes.h"
#include "EditorScene.h"

#include <cstdint>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorCameraEffectManager {
public:
	void Initialize(EditorScene* editorScene);  // Camera Blend / Shake対象Sceneを接続する
	void Start();  // Play On Start設定を開始し、Runtime Camera差分を初期化する
	void Update(float deltaTime, const uint8_t* keyState);  // 入力Camera、Blend、ShakeをGame View共有状態へ反映する
	void Stop();  // Camera上書きと振動差分を解除する
	bool PlayBlend(int32_t componentOwnerGameObjectId);  // 指定ObjectのCameraBlendを再生する
	bool PlayShake(int32_t componentOwnerGameObjectId);  // 指定ObjectのCameraShakeを再生する

private:
	struct BlendRuntime {
		Transforms sourceTransform{};  // 再生開始時に固定した開始姿勢
		Transforms targetTransform{};  // 再生開始時に固定した終了姿勢
		float elapsedTime = 0.0f;  // 補間済み秒数
		float duration = 0.0f;  // 補間時間秒
		int32_t easing = 0;  // 0=Linear、1=SmoothStep
		bool isActive = false;  // 補間中ならtrue
	};

	struct ShakeRuntime {
		Vector3 positionAmplitude{0.0f, 0.0f, 0.0f};  // 開始時に固定した位置振幅
		Vector3 rotationAmplitude{0.0f, 0.0f, 0.0f};  // 開始時に固定した回転振幅
		float frequency = 0.0f;  // 波形Hz
		float duration = 0.0f;  // 全継続秒数
		float elapsedTime = 0.0f;  // 再生済み秒数
		int32_t priority = 0;
	};

	EditorScene* editorScene_ = nullptr;  // Camera GameObjectと設定Componentを検索するScene
	BlendRuntime blendRuntime_{};  // 同時に有効なCamera Blendは最後に開始した1件
	std::vector<ShakeRuntime> shakeRuntimes_;  // 複数Shakeは位置・回転差分を加算する
	bool cameraControllerWasActive_ = false;  // 入力CameraまたはComposer無効化時だけ自身のCamera上書きを解除する。
	int32_t cameraInputOwnerGameObjectId_ = -1;  // 入力状態を初期化したCamera GameObject。
	Transforms cameraInputRuntimeTransform_{};  // FreeLook／Orbitで更新する実行時Camera姿勢。
	float cameraInputYaw_ = 0.0f;
	float cameraInputPitch_ = 0.0f;
	float cameraInputOrbitDistance_ = 0.0f;
	bool cameraInputInitialized_ = false;
	bool cameraInputOwnsCursorLock_ = false;  // Scriptのカーソル固定を誤って解除しないためCamera側の所有を記録する。
	bool cameraInputOwnsCursorVisibility_ = false;  // Camera側が非表示へ変更した時だけStop時に表示へ戻す。
	bool isStarted_ = false;  // Play中だけ共有Camera状態を書き換える

	Transforms ResolveWorldTransform(const EditorGameObject& gameObject) const;  // 親Transformを含む簡易World姿勢を返す
	bool FindHighestPriorityCameraTransform(Transforms& cameraTransform) const;  // 現在選択されるCamera姿勢を返す
	EditorGameObject* FindHighestPriorityCameraGameObject() const;  // SpeedFeedbackと追従合成で共通利用するCameraを返す
	bool UpdateCameraInput(float deltaTime, const uint8_t* keyState);  // 標準FreeLook／Orbitを最高Priority Cameraへ適用する
	void ReleaseCameraInputCursor();  // Camera操作が終わった時にカーソル固定と非表示を解除する
	bool UpdateFollowComposer(float deltaTime);  // 対象姿勢、速度先読み、減衰を合成してGame Cameraへ反映する
	float UpdateSpeedFeedback(float deltaTime);  // 実速度からFOV、Motion Blur、Camera演出強度を更新する
	static Transforms LerpTransform(
		const Transforms& sourceTransform,
		const Transforms& targetTransform,
		float ratio);  // 位置・回転・Scaleを線形補間する
};

#pragma warning(pop)
