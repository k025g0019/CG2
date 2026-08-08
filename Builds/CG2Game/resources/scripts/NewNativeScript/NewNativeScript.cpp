#include "NewNativeScript.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す実行 API。
	std::unordered_map<int32_t, std::unique_ptr<NewNativeScript>> scriptStates;  // GameObject ごとの C++ Component。

	NewNativeScript& GetState(int32_t gameObjectId) {
		std::unique_ptr<NewNativeScript>& scriptState = scriptStates[gameObjectId];

		if (scriptState == nullptr) {
			scriptState = std::make_unique<NewNativeScript>();
		}

		return *scriptState;
	}

	NewNativeScript& GetMetadataState() {
		static NewNativeScript metadataState;  // Play 前の Inspector が型情報だけを取得する。
		return metadataState;
	}
}

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

void NewNativeScript::Start(int32_t gameObjectId) {
	(void)gameObjectId;

	if (runtimeApi != nullptr) {
		runtimeApi->Log(startMessage_.c_str());
	}
}

void NewNativeScript::Update(int32_t gameObjectId, float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);
	transform.position.x += moveInput_.x * moveSpeed_ * deltaTime;
	transform.position.z += moveInput_.y * moveSpeed_ * deltaTime;
	runtimeApi->SetTransform(gameObjectId, &transform);
}

void NewNativeScript::FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	(void)gameObjectId;
	(void)fixedDeltaTime;  // AddForce など周期を固定した物理処理を書く。
}

void NewNativeScript::OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnCollisionEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void NewNativeScript::OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnTriggerEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void NewNativeScript::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	moveInput_ = {};
}

void NewNativeScript::OnMove(const EditorScriptInputActionContext& inputContext) {
	moveInput_ = inputContext.phase == EditorScriptInputPhaseCanceled
		? EditorScriptVector2{}
		: inputContext.vector2Value;
}

void NewNativeScript::OnJump(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	const EditorScriptVector3 jumpImpulse{0.0f, jumpImpulse_, 0.0f};
	runtimeApi->AddImpulse(inputContext.gameObjectId, &jumpImpulse);
}

void NewNativeScript::OnFire(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {
		runtimeApi->Log("OnFire");
	}
}

void NewNativeScript::OnClick(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {
		runtimeApi->Log("OnClick");
	}
}

void NewNativeScript::OnValueChanged(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	if (inputContext.valueType == EditorScriptInputValueTypeButton) {
		runtimeApi->Log(inputContext.buttonValue > 0.5f ? "OnValueChanged: ON" : "OnValueChanged: OFF");
		return;
	}

	const std::string message = "OnValueChanged: " + std::to_string(inputContext.vector2Value.x);
	runtimeApi->Log(message.c_str());
}

//================================================================
// Editor と C++ Component を接続する DLL ABI
//================================================================

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	scriptStates.clear();
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	GetState(gameObjectId).Start(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {
	GetState(gameObjectId).Update(gameObjectId, deltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	GetState(gameObjectId).FixedUpdate(gameObjectId, fixedDeltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(
	int32_t gameObjectId,
	const EditorScriptPhysicsEvent* physicsEvent) {
	if (physicsEvent == nullptr) {
		return;
	}

	GetState(gameObjectId).DispatchPhysicsEvent(*physicsEvent);
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	const auto scriptStateIt = scriptStates.find(gameObjectId);

	if (scriptStateIt != scriptStates.end()) {
		scriptStateIt->second->Stop(gameObjectId);
		scriptStates.erase(scriptStateIt);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {
	return fieldDescriptor != nullptr && GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr && GetState(gameObjectId).GetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_SetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	const EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr && GetState(gameObjectId).SetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_InvokeAction(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {
	return inputContext != nullptr && GetState(gameObjectId).InvokeAction(functionName, *inputContext);
}
