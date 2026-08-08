#include "ennsui_fbxScript.h"

#include <string>
#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す C++ 実行 API を保持する。
	std::unordered_map<int32_t, ennsui_fbxScript> scriptStates;  // GameObject ごとの実行状態を保持する。

	ennsui_fbxScript& GetState(int32_t gameObjectId) {
		return scriptStates[gameObjectId];
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

	ennsui_fbxScript& state = GetState(gameObjectId);  // この GameObject 専用の状態を取り出す。
	state.isStarted = true;
}

extern "C" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	ennsui_fbxScript& state = GetState(gameObjectId);  // Update 内で使う速度などを持つ状態。
	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);  // Editor 側の Transform を取得する。
	const EditorScriptVector2 moveInput = runtimeApi->GetActionVector2(gameObjectId, "Player", "Move");  // .inputactions の Player/Move を読む。

	EditorSpriptVector3 forward = { 1.0f, 0.0f, 0.0f };
	//============================================================
	// C++ で書き換える場所
	// ここには基礎だけ書いてあるので、詳細な API は component-reference.html を見ること
	// 例: 追加 Action は WasActionJustPressed(gameObjectId, "Player", "Jump") のように読む
	// 例: AI センサーは GetAiSensorState(gameObjectId, EditorScriptAiSensorKindVision) のように読む
	//============================================================
	transform.position.x += moveInput.x * state.moveSpeed * deltaTime;
	transform.position.z += moveInput.y * state.moveSpeed * deltaTime;

	if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Jump")) {
		// ここへジャンプや発射処理を書く。
	}

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeQ)) {
		transform.rotation.y -= state.rotateSpeed * deltaTime;  // 旧来のキー入力も必要なら併用できる。
	}
	if(runtimeApi->IsKeyDown(EditorScriptKeyCodeSpace)||runtimeApi->IsKeyPressed(EditorScriptKeyCodeSpace)) {

	}
	runtimeApi->AddForce(gameObjectId, { 10.0f, 0.0f, 0.0f });  // Rigidbody に上方向の速度を設定する。
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
		(void)gameObjectId;  // 衝突相手 ID や法線を見て処理を分岐する場所。
	}
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	scriptStates.erase(gameObjectId);  // Play 停止や DLL 差し替え時に、この GameObject の一時状態を破棄する。
	(void)gameObjectId;
}
