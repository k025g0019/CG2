// このファイルは Engine が自動生成します。ユーザーコードは WirePlayerScript.cpp へ記述してください。
#include "WirePlayerScript.h"

#include <new>
#include <type_traits>

namespace {
	template <typename ScriptType>
	void AttachScriptToGameObject(ScriptType& script, int32_t gameObjectId) {
		if constexpr (std::is_base_of_v<Script, ScriptType>) {
			script.AttachToGameObject(gameObjectId);
		}
	}

	struct ScriptInstance {
		explicit ScriptInstance(int32_t ownerGameObjectId)
			: gameObjectId(ownerGameObjectId) {

			AttachScriptToGameObject(script, ownerGameObjectId);
		}

		int32_t gameObjectId = -1;
		WirePlayerScript script;
	};

	WirePlayerScript& GetMetadataState() {
		static WirePlayerScript metadataState;
		return metadataState;
	}

	ScriptInstance* GetScriptInstance(void* instance) {
		return static_cast<ScriptInstance*>(instance);
	}
}

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* runtimeApi) {

	if (apiVersion != kEditorScriptApiVersion || runtimeApi == nullptr) {
		return false;
	}

	EditorNativeScriptRuntime::SetRuntimeApi(runtimeApi);
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);
}

extern "C" __declspec(dllexport) void* EditorScript_CreateInstance(int32_t gameObjectId) {
	return new (std::nothrow) ScriptInstance(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_DestroyInstance(void* instance) {
	delete GetScriptInstance(instance);
}

extern "C" __declspec(dllexport) void EditorScript_StartInstance(void* instance) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).Start(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) void EditorScript_UpdateInstance(void* instance, float deltaTime) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).Update(
			scriptInstance->gameObjectId,
			deltaTime);
	}
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdateInstance(
	void* instance,
	float fixedDeltaTime) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).FixedUpdate(
			scriptInstance->gameObjectId,
			fixedDeltaTime);
	}
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEventInstance(
	void* instance,
	const EditorScriptPhysicsEvent* physicsEvent) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance == nullptr || physicsEvent == nullptr) {
		return;
	}

	scriptInstance->script.DispatchPhysicsEvent(*physicsEvent);
}

extern "C" __declspec(dllexport) void EditorScript_OnWireEventInstance(
	void* instance,
	const EditorScriptWireEvent* wireEvent) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	if (scriptInstance == nullptr || wireEvent == nullptr) {
		return;
	}
	scriptInstance->script.DispatchWireEvent(*wireEvent);
}

extern "C" __declspec(dllexport) void EditorScript_OnAnimationEventInstance(
	void* instance,
	const EditorScriptAnimationEvent* animationEvent) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance == nullptr || animationEvent == nullptr) {
		return;
	}

	scriptInstance->script.OnAnimationEvent(*animationEvent);
}

extern "C" __declspec(dllexport) void EditorScript_StopInstance(void* instance) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).Stop(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {

	return fieldDescriptor != nullptr &&
		GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldValueInstance(
	void* instance,
	const char* fieldName,
	EditorScriptFieldValue* fieldValue) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && fieldValue != nullptr &&
		scriptInstance->script.GetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_SetFieldValueInstance(
	void* instance,
	const char* fieldName,
	const EditorScriptFieldValue* fieldValue) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && fieldValue != nullptr &&
		scriptInstance->script.SetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_InvokeActionInstance(
	void* instance,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && inputContext != nullptr &&
		scriptInstance->script.InvokeAction(functionName, *inputContext);
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetActionCount() {
	return GetMetadataState().GetActionCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetActionName(
	int32_t actionIndex,
	char* actionName,
	int32_t actionNameCapacity) {

	return GetMetadataState().GetActionName(actionIndex, actionName, actionNameCapacity);
}
