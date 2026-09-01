#include "TrailLinkBlock.h"

#include <unordered_map>

namespace {

	struct BlockRuntimeState {
		bool linked = false;
		int32_t playerId = -1;
		float attackRemaining = 0.0f;
	};

	std::unordered_map<
		int32_t,
		BlockRuntimeState> blockStates;

}

TrailLinkBlock::TrailLinkBlock() {
	BindAction(
		"OnLinked",
		[](const EditorScriptInputActionContext& context) {
			if (context.payloadType !=
				EditorScriptActionPayloadTypeGameObject) {
				return;
			}

			BlockRuntimeState& state =
				blockStates[context.gameObjectId];

			state.linked = true;

			state.playerId =
				context.payloadGameObjectId;
		});

	BindAction(
		"OnUnlinked",
		[](const EditorScriptInputActionContext& context) {
			BlockRuntimeState& state =
				blockStates[context.gameObjectId];

			state.linked = false;
			state.playerId = -1;
			state.attackRemaining = 0.0f;
		});

	BindAction(
		"BeginAttack",
		[](const EditorScriptInputActionContext& context) {
			if (context.payloadType !=
				EditorScriptActionPayloadTypeFloat) {
				return;
			}

			BlockRuntimeState& state =
				blockStates[context.gameObjectId];

			state.attackRemaining =
				context.payloadFloat;
		});

	BindAction(
		"OnEnemyHit",
		[](const EditorScriptInputActionContext& context) {
			BlockRuntimeState& state =
				blockStates[context.gameObjectId];

			if (!state.linked) {
				return;
			}

			// 自分が攻撃中ならEnemy接触で
			// 自分の鎖を切らない。
			if (state.attackRemaining > 0.0f) {
				return;
			}

			if (state.playerId < 0) {
				return;
			}

			GameObject{
				state.playerId
			}.InvokeAction(
				"OnBreakLink",
				ActionPayload::GameObjectValue(
					GameObject{
						context.gameObjectId
					}));
		});

	BindAction(
		"OnOutOfBounds",
		[](const EditorScriptInputActionContext& context) {
			BlockRuntimeState& state =
				blockStates[context.gameObjectId];

			if (state.linked &&
				state.playerId >= 0) {

				GameObject{
					state.playerId
				}.InvokeAction(
					"OnBreakLink",
					ActionPayload::GameObjectValue(
						GameObject{
							context.gameObjectId
						}));
			}

			state.linked = false;
			state.playerId = -1;
			state.attackRemaining = 0.0f;

			ObjectPool::Release(
				GameObject{
					context.gameObjectId
				});
		});
}

void TrailLinkBlock::Start(
	int32_t gameObjectId) {

	// Blockを生成した時点で自動的にWire選択対象へ登録する。
	blockStates[gameObjectId] = {};
}

void TrailLinkBlock::Update(
	int32_t gameObjectId,
	float deltaTime) {

	BlockRuntimeState& state =
		blockStates[gameObjectId];

	if (state.attackRemaining <= 0.0f) {
		return;
	}

	state.attackRemaining -= deltaTime;

	if (state.attackRemaining < 0.0f) {
		state.attackRemaining = 0.0f;
	}
}

void TrailLinkBlock::FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {

	(void)gameObjectId;
	(void)fixedDeltaTime;
}

void TrailLinkBlock::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	const int32_t selfId =
		physicsEvent.selfGameObjectId;

	const int32_t otherId =
		physicsEvent.otherGameObjectId;

	if (selfId < 0 || otherId < 0) {
		return;
	}

	BlockRuntimeState& state =
		blockStates[selfId];

	// 未連結なら接触した相手へ取得要求。
	// OnPickupBlockを持つPlayerだけが受信する。
	if (!state.linked) {
		GameObject{ otherId }.InvokeAction(
			"OnPickupBlock",
			ActionPayload::GameObjectValue(
				GameObject{ selfId }));

		return;
	}

	// Space攻撃時間外。
	if (state.attackRemaining <= 0.0f) {
		return;
	}

	const EditorScriptVector3 velocity =
		Rigidbody{ selfId }.GetVelocity();

	// OnLinkHitを持つEnemyだけが受信する。
	GameObject{ otherId }.InvokeAction(
		"OnLinkHit",
		ActionPayload::Vector3(
			velocity));
}

void TrailLinkBlock::OnTriggerEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}

void TrailLinkBlock::Stop(
	int32_t gameObjectId) {

	blockStates.erase(gameObjectId);
}
