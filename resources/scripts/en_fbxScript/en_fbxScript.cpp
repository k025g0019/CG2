#include "en_fbxScript.h"

void en_fbxScript::Start() {
	isStarted_ = true;
}

void en_fbxScript::Update(float deltaTime) {
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

	if (runtimeApi == nullptr) {
		return;
	}

	const int32_t gameObjectId = GetGameObjectId();
	EditorScriptTransform transform =
		runtimeApi->GetTransform(gameObjectId);
	const EditorScriptVector2 moveInput =
		runtimeApi->GetActionVector2(
			gameObjectId,
			"Player",
			"Move");

	transform.position.x +=
		moveInput.x * moveSpeed_ * deltaTime;
	transform.position.z +=
		moveInput.y * moveSpeed_ * deltaTime;

	const float torqueSpeed = rotateSpeed_;
	EditorScriptVector3 torque{};
	EditorScriptVector3 force{};
	torque.y = torqueSpeed;
	force.y = -100.0f;

	runtimeApi->AddForce(gameObjectId, &force);
	runtimeApi->AddTorque(gameObjectId, &torque);
	runtimeApi->SetTransform(gameObjectId, &transform);
}

void en_fbxScript::FixedUpdate(float fixedDeltaTime) {
	(void)fixedDeltaTime;
}

void en_fbxScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}

void en_fbxScript::Stop() {
	isStarted_ = false;
}
