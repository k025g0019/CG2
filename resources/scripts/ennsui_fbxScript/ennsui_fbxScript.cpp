#include "ennsui_fbxScript.h"

void ennsui_fbxScript::Start() {
	isStarted_ = true;
}

void ennsui_fbxScript::Update(float deltaTime) {
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

	if (runtimeApi->IsKeyDown(EditorScriptKeyCodeQ)) {
		transform.rotation.y -= rotateSpeed_ * deltaTime;
	}

	EditorScriptVector3 force{10.0f, 0.0f, 0.0f};
	runtimeApi->AddForce(gameObjectId, &force);
	runtimeApi->SetTransform(gameObjectId, &transform);
}

void ennsui_fbxScript::FixedUpdate(float fixedDeltaTime) {
	(void)fixedDeltaTime;
}

void ennsui_fbxScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}

void ennsui_fbxScript::Stop() {
	isStarted_ = false;
}
