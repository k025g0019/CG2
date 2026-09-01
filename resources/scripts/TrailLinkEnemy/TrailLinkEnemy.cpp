#include "TrailLinkEnemy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

	std::unordered_map<int32_t, float>
		stunTimers;

	float knockbackScale = 1.5f;
	float minKnockback = 3.0f;
	float maxKnockback = 20.0f;
	float upwardKnockback = 1.0f;
	float stunSeconds = 0.4f;

}

TrailLinkEnemy::TrailLinkEnemy() {
	ExposeFloat(
		"knockbackScale",
		"吹っ飛び倍率",
		knockbackScale,
		0.0f,
		100.0f,
		0.1f);

	ExposeFloat(
		"minKnockback",
		"最小吹っ飛び",
		minKnockback,
		0.0f,
		100.0f,
		0.1f);

	ExposeFloat(
		"maxKnockback",
		"最大吹っ飛び",
		maxKnockback,
		0.0f,
		200.0f,
		0.1f);

	ExposeFloat(
		"upwardKnockback",
		"上向き吹っ飛び",
		upwardKnockback,
		0.0f,
		100.0f,
		0.1f);

	ExposeFloat(
		"stunSeconds",
		"吹っ飛び硬直",
		stunSeconds,
		0.0f,
		5.0f,
		0.01f);

	BindAction(
		"OnLinkHit",
		[](const EditorScriptInputActionContext& context) {
			if (context.payloadType !=
				EditorScriptActionPayloadTypeVector3) {
				return;
			}

			EditorScriptVector3 velocity =
				context.payloadVector3;

			const float speed =
				std::sqrt(
					velocity.x * velocity.x +
					velocity.y * velocity.y +
					velocity.z * velocity.z);

			if (speed <= 0.001f) {
				return;
			}

			velocity.x /= speed;
			velocity.y /= speed;
			velocity.z /= speed;

			const float power =
				std::clamp(
					speed * knockbackScale,
					minKnockback,
					maxKnockback);

			const int32_t enemyId =
				context.gameObjectId;

			// 追跡Forceと吹っ飛びを競合させない。
			GameObject{ enemyId }
				.SetComponentActive(
					"TargetSteering",
					false);

			stunTimers[enemyId] =
				stunSeconds;

			Rigidbody{ enemyId }.AddImpulse(
				EditorScriptVector3{
					velocity.x * power,
					velocity.y * power +
						upwardKnockback,
					velocity.z * power
				});
		});

	BindAction(
		"OnOutOfBounds",
		[](const EditorScriptInputActionContext& context) {
			GameObject{
				context.gameObjectId
			}.SetActive(false);
		});
}

void TrailLinkEnemy::Start(
	int32_t gameObjectId) {

	// Enemy Scriptが付いた個体は自動でWire選択対象になる。
	stunTimers[gameObjectId] = 0.0f;
}

void TrailLinkEnemy::Update(
	int32_t gameObjectId,
	float deltaTime) {

	auto timerIt =
		stunTimers.find(gameObjectId);

	if (timerIt == stunTimers.end()) {
		return;
	}

	if (timerIt->second <= 0.0f) {
		return;
	}

	timerIt->second -= deltaTime;

	if (timerIt->second > 0.0f) {
		return;
	}

	timerIt->second = 0.0f;

	GameObject{ gameObjectId }
		.SetComponentActive(
			"TargetSteering",
			true);
}

void TrailLinkEnemy::FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {

	(void)gameObjectId;
	(void)fixedDeltaTime;
}

void TrailLinkEnemy::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	if (physicsEvent.otherGameObjectId < 0) {
		return;
	}

	// BlockだけがOnEnemyHitをBindしているので
	// 他Objectへ送っても何も起こらない。
	GameObject{
		physicsEvent.otherGameObjectId
	}.InvokeAction("OnEnemyHit");
}

void TrailLinkEnemy::OnTriggerEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}

void TrailLinkEnemy::Stop(
	int32_t gameObjectId) {

	stunTimers.erase(gameObjectId);
}
