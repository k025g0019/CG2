#include "NewNativeScript.h"

#include <string>
#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す C++ 実行 API を保持する。
	std::unordered_map<int32_t, NewNativeScript> scriptStates;  // GameObject ごとの実行状態を保持する。

	NewNativeScript& GetState(int32_t gameObjectId) {
		return scriptStates[gameObjectId];
	}

	void LogActionPressed(int32_t gameObjectId, const char* actionMapName, const char* actionName) {
		std::string logText = "NewNativeScript::Action self=" + std::to_string(gameObjectId) +
			" map=" + actionMapName + " action=" + actionName;
		runtimeApi->Log(logText.c_str());  // 任意の Action が押された時の確認用ログ。
	}
}

extern "C" __declspec(dllexport) bool EditorScript_Load(uint32_t apiVersion, const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;  // 以後の Start / Update / FixedUpdate から使えるよう保存する。
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	scriptStates.clear();  // DLL 再読込前に GameObject ごとの一時状態を破棄する。
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	if (runtimeApi == nullptr) {
		return;
	}

	NewNativeScript& state = GetState(gameObjectId);  // この GameObject 専用の設定を取り出す。
	state.isStarted = true;
	runtimeApi->Log("NewNativeScript::Start");
}

extern "C" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	NewNativeScript& state = GetState(gameObjectId);  // Update 内で使う速度などを持つ状態。
	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);  // Editor 側の Transform を取得する。
	const EditorScriptVector2 moveInput = runtimeApi->GetActionVector2(gameObjectId, "Player", "Move");  // .inputactions の Player/Move を読む。

	//============================================================
	// C++ で書き換える場所
	// 例1: Player/Move を使って XZ 平面を移動する
	// 例2: .inputactions に Action|Player|Dash|Button|Key|LeftShift を追加したら
	//       runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Dash") で受ける
	// 例3: マウス座標が必要な時は runtimeApi->GetMousePosition() を使う
	//============================================================
	transform.position.x += moveInput.x * state.moveSpeed * deltaTime;
	transform.position.z += moveInput.y * state.moveSpeed * deltaTime;

	if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Jump")) {
		LogActionPressed(gameObjectId, "Player", "Jump");  // 既定で入る Jump の受け取り例。
	}

	if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Fire")) {
		const EditorScriptVector2 mousePosition = runtimeApi->GetMousePosition();  // 既定で入る Fire とマウス位置の利用例。
		std::string logText = "NewNativeScript::Fire mouse=(" + std::to_string(mousePosition.x) + ", " + std::to_string(mousePosition.y) + ")";
		runtimeApi->Log(logText.c_str());
	}

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeQ)) {
		transform.rotation.y -= state.rotateSpeed * deltaTime;  // 旧来のキー直書き入力も必要なら併用できる。
	}

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeE)) {
		transform.rotation.y += state.rotateSpeed * deltaTime;
	}

	runtimeApi->SetTransform(gameObjectId, &transform);  // 計算後の Transform を Editor 側へ戻す。
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	(void)gameObjectId;  // 今は未使用だが、AddForce や SetVelocity を書く時に使う。
	(void)fixedDeltaTime;
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(int32_t gameObjectId, const EditorScriptPhysicsEvent* physicsEvent) {
	if (runtimeApi == nullptr || physicsEvent == nullptr) {
		return;
	}

	if (physicsEvent->type == EditorScriptPhysicsEventTypeCollisionEnter) {
		std::string logText = "NewNativeScript::CollisionEnter self=" + std::to_string(gameObjectId);
		runtimeApi->Log(logText.c_str());
	}
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	scriptStates.erase(gameObjectId);  // Play 停止や DLL 差し替え時に、この GameObject の一時状態を破棄する。
	(void)gameObjectId;
}
