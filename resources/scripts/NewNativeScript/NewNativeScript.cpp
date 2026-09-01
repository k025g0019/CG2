#include "NewNativeScript.h"

#include <string>

//================================================================
// ユーザーが編集する C++ Component 本体
//================================================================

NewNativeScript::NewNativeScript() {
	ExposeFloat("moveSpeed", "移動速度", moveSpeed_, 0.0f, 100.0f, 0.1f);
	ExposeFloat("jumpImpulse", "ジャンプ力", jumpImpulse_, 0.0f, 100.0f, 0.1f);
	ExposeString("startMessage", "開始メッセージ", startMessage_);

	BindAction("OnMove", [this](const EditorScriptInputActionContext& inputContext) { OnMove(inputContext); });
	BindAction("OnJump", [this](const EditorScriptInputActionContext& inputContext) { OnJump(inputContext); });
	BindAction("OnFire", [this](const EditorScriptInputActionContext& inputContext) { OnFire(inputContext); });
	BindAction("OnClick", [this](const EditorScriptInputActionContext& inputContext) { OnClick(inputContext); });
	BindAction("OnValueChanged", [this](const EditorScriptInputActionContext& inputContext) { OnValueChanged(inputContext); });
}

void NewNativeScript::Start() {
	if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr) {
		EditorNativeScriptRuntime::GetRuntimeApi()->Log(startMessage_.c_str());
	}
}

void NewNativeScript::Update(float deltaTime) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() == nullptr) {
		return;
	}

	const int32_t gameObjectId = GetGameObjectId();
	EditorScriptTransform transform = EditorNativeScriptRuntime::GetRuntimeApi()->GetTransform(gameObjectId);
	transform.position.x += moveInput_.x * moveSpeed_ * deltaTime;
	transform.position.z += moveInput_.y * moveSpeed_ * deltaTime;
	EditorNativeScriptRuntime::GetRuntimeApi()->SetTransform(gameObjectId, &transform);
}

void NewNativeScript::FixedUpdate(float fixedDeltaTime) {
	(void)fixedDeltaTime;  // AddForce など周期を固定した物理処理を書く。
}

void NewNativeScript::OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr) {
		const std::string message = "OnCollisionEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		EditorNativeScriptRuntime::GetRuntimeApi()->Log(message.c_str());
	}
}

void NewNativeScript::OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr) {
		const std::string message = "OnTriggerEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		EditorNativeScriptRuntime::GetRuntimeApi()->Log(message.c_str());
	}
}

void NewNativeScript::Stop() {
	moveInput_ = {};
}

void NewNativeScript::OnMove(const EditorScriptInputActionContext& inputContext) {
	moveInput_ = inputContext.phase == EditorScriptInputPhaseCanceled
		? EditorScriptVector2{}
		: inputContext.vector2Value;
}

void NewNativeScript::OnJump(const EditorScriptInputActionContext& inputContext) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	const EditorScriptVector3 jumpImpulse{0.0f, jumpImpulse_, 0.0f};
	EditorNativeScriptRuntime::GetRuntimeApi()->AddImpulse(inputContext.gameObjectId, &jumpImpulse);
}

void NewNativeScript::OnFire(const EditorScriptInputActionContext& inputContext) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {
		EditorNativeScriptRuntime::GetRuntimeApi()->Log("OnFire");
	}
}

void NewNativeScript::OnClick(const EditorScriptInputActionContext& inputContext) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {
		EditorNativeScriptRuntime::GetRuntimeApi()->Log("OnClick");
	}
}

void NewNativeScript::OnValueChanged(const EditorScriptInputActionContext& inputContext) {
	if (EditorNativeScriptRuntime::GetRuntimeApi() == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	if (inputContext.valueType == EditorScriptInputValueTypeButton) {
		EditorNativeScriptRuntime::GetRuntimeApi()->Log(inputContext.buttonValue > 0.5f ? "OnValueChanged: ON" : "OnValueChanged: OFF");
		return;
	}

	const std::string message = "OnValueChanged: " + std::to_string(inputContext.vector2Value.x);
	EditorNativeScriptRuntime::GetRuntimeApi()->Log(message.c_str());
}
