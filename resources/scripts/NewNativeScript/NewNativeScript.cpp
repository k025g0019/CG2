#include "NewNativeScript.h"

#include <string>
#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す C++ 実行 API を保持する。
	std::unordered_map<int32_t, NewNativeScript> scriptStates;  // GameObject ごとの実行状態を保持する。

	NewNativeScript& GetState(int32_t gameObjectId) {
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


	NewNativeScript& state = GetState(gameObjectId);  // この GameObject 専用の状態を取り出す。
	state.isStarted = true;

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
	// ここには基礎だけ書いてあるので、詳細な API は component-reference.html を見ること
	// 例: 追加 Action は WasActionJustPressed(gameObjectId, "Player", "Jump") のように読む
	// 例: AI センサーは GetAiSensorState(gameObjectId, EditorScriptAiSensorKindVision) のように読む
	//============================================================
	transform.position.x += moveInput.x * state.moveSpeed * deltaTime;
	transform.position.z += moveInput.y * state.moveSpeed * deltaTime;
	const EditorScriptAiSensorState objectSensor =
		runtimeApi->GetAiSensorState(gameObjectId, EditorScriptAiSensorKindObjectDetection);

	const float torqueSpeed = 10.0f;
	EditorScriptVector3 torque{};

	EditorScriptVector3 force{};

	if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Jump")) {
		// ここへジャンプや発射処理を書く。
	}


	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeW)) {
		torque.x += torqueSpeed;
	}

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeS)) {
		torque.x -= torqueSpeed;
	}

	if(runtimeApi->IsKeyDown(EditorScriptKeyCodeA)) {
		torque.y += torqueSpeed;
	}
	if(runtimeApi->IsKeyDown(EditorScriptKeyCodeD)) {
		torque.y -= torqueSpeed;
	}

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeSpace)) {
		force.y += torqueSpeed;
	}
	if (objectSensor.hasComponent && objectSensor.isDetected) {
		force.y += torqueSpeed;
	}

	const EditorScriptVector3 center{ 0.0f, 0.0f, 0.0f };

	const float targetRadius = 5.0f;      // 円の半径
	const float radialPower = 20.0f;      // 中心から離れた時に戻す強さ
	const float tangentPower = 8.0f;      // 円周方向に進ませる力

	EditorScriptVector3 toObject{};
	toObject.x = transform.position.x - center.x;
	toObject.y = 0.0f;
	toObject.z = transform.position.z - center.z;

	const float r2 = toObject.x * toObject.x + toObject.z * toObject.z;

	if (r2 > 0.0001f) {
		const float r = sqrtf(r2);

		EditorScriptVector3 outward{};
		outward.x = toObject.x / r;
		outward.y = 0.0f;
		outward.z = toObject.z / r;

		// 半径を targetRadius に近づける力
		// r が大きすぎる → 中心へ
		// r が小さすぎる → 外側へ
		const float radiusError = targetRadius - r;

		EditorScriptVector3 radialForce{};
		radialForce.x = outward.x * radiusError * radialPower;
		radialForce.y = 0.0f;
		radialForce.z = outward.z * radiusError * radialPower;

		// 円周方向の力
		// Y軸回りに回すため、XZ上で90度回転した方向を使う
		EditorScriptVector3 tangent{};
		tangent.x = -outward.z;
		tangent.y = 0.0f;
		tangent.z = outward.x;

		EditorScriptVector3 tangentForce{};
		tangentForce.x = tangent.x * tangentPower;
		tangentForce.y = 0.0f;
		tangentForce.z = tangent.z * tangentPower;

		force.x += radialForce.x + tangentForce.x;
		force.y += radialForce.y + tangentForce.y;
		force.z += radialForce.z + tangentForce.z;
	}
	runtimeApi->AddTorque(gameObjectId, &torque);  // Torque を加えると Rigidbody が回転する。
	runtimeApi->AddForce(gameObjectId, &force);  // Force を加えると Rigidbody が移動する。
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
