#include "TrailLinkOutZone.h"


namespace {

}

TrailLinkOutZone::TrailLinkOutZone() {
}

void TrailLinkOutZone::Start(
	int32_t gameObjectId) {

	(void)gameObjectId;
}

void TrailLinkOutZone::Update(
	int32_t gameObjectId,
	float deltaTime) {

	(void)gameObjectId;
	(void)deltaTime;
}

void TrailLinkOutZone::FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {

	(void)gameObjectId;
	(void)fixedDeltaTime;
}

void TrailLinkOutZone::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}

void TrailLinkOutZone::OnTriggerEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	if (physicsEvent.otherGameObjectId < 0) {
		return;
	}

	GameObject{
		physicsEvent.otherGameObjectId
	}.InvokeAction("OnOutOfBounds");
}

void TrailLinkOutZone::Stop(
	int32_t gameObjectId) {

	(void)gameObjectId;
}
