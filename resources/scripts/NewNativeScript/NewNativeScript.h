#pragma once

#include "EditorNativeScript.h"

#include <string>

//================================================================
// NewNativeScript - GameObject へ追加する C++ Component
//================================================================

class NewNativeScript final : public EditorNativeScript {
public:
	NewNativeScript();  // 公開変数と Input Action 関数を登録する。

	void Start(int32_t gameObjectId) override;
	void Update(int32_t gameObjectId, float deltaTime) override;
	void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) override;
	void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void Stop(int32_t gameObjectId) override;

private:
	float moveSpeed_ = 3.0f;  // Inspector から編集する移動速度。
	float jumpImpulse_ = 5.0f;  // Inspector から編集するジャンプの瞬間力。
	std::string startMessage_ = "NewNativeScript::Start";  // Inspector から編集する開始ログ。
	EditorScriptVector2 moveInput_{};  // OnMove が受けた入力を Update まで保持する。

	void OnMove(const EditorScriptInputActionContext& inputContext);
	void OnJump(const EditorScriptInputActionContext& inputContext);
	void OnFire(const EditorScriptInputActionContext& inputContext);
};
