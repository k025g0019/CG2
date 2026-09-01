#include "TrailLinkPlayer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

	//============================================================
	// Playerごとの実行中状態
	//============================================================

	struct PlayerRuntimeState {
		// 0番はPlayer自身。
		// 以降は取得順のBlock。
		std::vector<int32_t> linkedGameObjectIds;

		// linkedGameObjectIds[0]-[1]
		// linkedGameObjectIds[1]-[2]
		// ...
		// に対応するSpringJoint。
		std::vector<JointHandle> springJointHandles;

		// Spaceを押すごとに左右交互へ振る。
		bool attackRight = true;
	};

	std::unordered_map<int32_t, PlayerRuntimeState> playerStates;

	//============================================================
	// Inspector公開設定
	//
	// .hを変更しないため、このcpp内で保持する。
	// Playerは1体想定。
	//============================================================

	float turnSpeed = 2.5f;

	float springMinDistance = 0.8f;
	float springMaxDistance = 1.2f;
	float springFrequency = 5.0f;
	float springDamping = 0.7f;

	float attackWindowSeconds = 0.35f;



	//============================================================
	// 指定Blockから後ろを切断
	//============================================================

	void BreakLinkFrom(
		int32_t playerId,
		int32_t brokenBlockId) {

		auto stateIt =
			playerStates.find(playerId);

		if (stateIt == playerStates.end()) {
			return;
		}

		PlayerRuntimeState& state =
			stateIt->second;

		auto blockIt =
			std::find(
				state.linkedGameObjectIds.begin(),
				state.linkedGameObjectIds.end(),
				brokenBlockId);

		if (blockIt ==
			state.linkedGameObjectIds.end()) {
			return;
		}

		const size_t blockIndex =
			static_cast<size_t>(
				std::distance(
					state.linkedGameObjectIds.begin(),
					blockIt));

		// index 0 はPlayer自身。
		if (blockIndex == 0) {
			return;
		}

		// Block index 1 に対応するJointは index 0。
		const size_t firstJointIndex =
			blockIndex - 1;

		//--------------------------------------------------------
		// 切断位置から後ろのJointを全部破棄
		//--------------------------------------------------------

		for (size_t jointIndex = firstJointIndex;
			jointIndex < state.springJointHandles.size();
			++jointIndex) {

			const JointHandle jointHandle =
				state.springJointHandles[jointIndex];

			if (Physics::IsJointValid(jointHandle)) {
				Physics::DestroyJoint(jointHandle);
			}
		}

		//--------------------------------------------------------
		// 外れたBlockへ通知
		//--------------------------------------------------------

		for (size_t block = blockIndex;
			block < state.linkedGameObjectIds.size();
			++block) {

			GameObject{
				state.linkedGameObjectIds[block]
			}.InvokeAction("OnUnlinked");
		}

		//--------------------------------------------------------
		// 管理配列から削除
		//--------------------------------------------------------

		state.linkedGameObjectIds.erase(
			state.linkedGameObjectIds.begin()
			+ blockIndex,
			state.linkedGameObjectIds.end());

		state.springJointHandles.erase(
			state.springJointHandles.begin()
			+ firstJointIndex,
			state.springJointHandles.end());
	}
}


//================================================================
// ユーザーが編集する C++ Component 本体
//================================================================

TrailLinkPlayer::TrailLinkPlayer() {
	//------------------------------------------------------------
	// 移動
	//------------------------------------------------------------

	ExposeFloat(
		"moveSpeed",
		"前後移動速度",
		moveSpeed_,
		0.0f,
		100.0f,
		0.1f);

	ExposeFloat(
		"turnSpeed",
		"旋回速度(rad/s)",
		turnSpeed,
		0.0f,
		20.0f,
		0.1f);

	//------------------------------------------------------------
	// 攻撃
	//
	// .hを触らないため、元テンプレートのjumpImpulse_を
	// 攻撃Impulseとして使用する。
	//------------------------------------------------------------

	ExposeFloat(
		"attackImpulse",
		"攻撃Impulse",
		jumpImpulse_,
		0.0f,
		100.0f,
		0.1f);

	ExposeFloat(
		"attackWindowSeconds",
		"攻撃判定時間",
		attackWindowSeconds,
		0.01f,
		2.0f,
		0.01f);

	//------------------------------------------------------------
	// Spring
	//------------------------------------------------------------

	ExposeFloat(
		"springMinDistance",
		"Spring 最小距離",
		springMinDistance,
		0.0f,
		20.0f,
		0.05f);

	ExposeFloat(
		"springMaxDistance",
		"Spring 最大距離",
		springMaxDistance,
		0.0f,
		20.0f,
		0.05f);

	ExposeFloat(
		"springFrequency",
		"Spring 周波数",
		springFrequency,
		0.0f,
		50.0f,
		0.1f);

	ExposeFloat(
		"springDamping",
		"Spring 減衰",
		springDamping,
		0.0f,
		20.0f,
		0.1f);

	//------------------------------------------------------------
	// PlayerInput
	//------------------------------------------------------------

	BindAction(
		"OnMove",
		[this](
			const EditorScriptInputActionContext& inputContext) {

				OnMove(inputContext);
		});

	BindAction(
		"OnFire",
		[this](
			const EditorScriptInputActionContext& inputContext) {

				OnFire(inputContext);
		});

	//------------------------------------------------------------
	// Block取得
	//
	// TrailLinkBlockから
	//
	// OnPickupBlock(GameObject)
	//
	// が送られてくる。
	//------------------------------------------------------------

	BindAction(
		"OnPickupBlock",
		[](
			const EditorScriptInputActionContext& context) {

				if (context.payloadType !=
					EditorScriptActionPayloadTypeGameObject) {
					return;
				}

				const int32_t playerId =
					context.gameObjectId;

				const int32_t newBlockId =
					context.payloadGameObjectId;

				if (playerId < 0 ||
					newBlockId < 0) {
					return;
				}

				auto stateIt =
					playerStates.find(playerId);

				if (stateIt ==
					playerStates.end()) {
					return;
				}

				PlayerRuntimeState& state =
					stateIt->second;

				if (state.linkedGameObjectIds.empty()) {
					return;
				}

				//----------------------------------------------------
				// 二重取得防止
				//----------------------------------------------------

				if (std::find(
					state.linkedGameObjectIds.begin(),
					state.linkedGameObjectIds.end(),
					newBlockId)
					!= state.linkedGameObjectIds.end()) {

					return;
				}

				//----------------------------------------------------
				// 現在の最後尾
				//----------------------------------------------------

				const int32_t lastGameObjectId =
					state.linkedGameObjectIds.back();

				//----------------------------------------------------
				// Spring設定
				//----------------------------------------------------

				SpringJointDesc springJointDesc{};

				springJointDesc.minDistance =
					springMinDistance;

				springJointDesc.maxDistance =
					springMaxDistance;

				springJointDesc.frequency =
					springFrequency;

				springJointDesc.damping =
					springDamping;

				//----------------------------------------------------
				// 最後尾と新しいBlockをSpring接続
				//----------------------------------------------------

				const JointHandle jointHandle =
					Physics::CreateSpringJoint(
						newBlockId,
						lastGameObjectId,
						springJointDesc);

				if (!Physics::IsJointValid(
					jointHandle)) {

					return;
				}

				//----------------------------------------------------
				// 管理配列へ追加
				//----------------------------------------------------

				state.linkedGameObjectIds.push_back(
					newBlockId);

				state.springJointHandles.push_back(
					jointHandle);

				//----------------------------------------------------
				// Blockへ「連結された」と通知
				//
				// PayloadにはPlayer自身を渡す。
				//----------------------------------------------------

				GameObject{ newBlockId }.InvokeAction(
					"OnLinked",
					ActionPayload::GameObjectValue(
						GameObject{ playerId }));
		});

	//------------------------------------------------------------
	// Block切断要求
	//------------------------------------------------------------

	BindAction(
		"OnBreakLink",
		[](
			const EditorScriptInputActionContext& context) {

				if (context.payloadType !=
					EditorScriptActionPayloadTypeGameObject) {
					return;
				}

				BreakLinkFrom(
					context.gameObjectId,
					context.payloadGameObjectId);
		});

	//------------------------------------------------------------
	// Player場外
	//------------------------------------------------------------

	BindAction(
		"OnOutOfBounds",
		[](
			const EditorScriptInputActionContext& context) {

				GameObject{
					context.gameObjectId
				}.SetActive(false);
		});
}


//================================================================
// Start
//================================================================

void TrailLinkPlayer::Start(
	int32_t gameObjectId) {

	EditorNativeScriptRuntime::GetRuntimeApi()->Log("TrailLinkPlayer Start");

	PlayerRuntimeState state{};

	// 最初はPlayer自身だけ。
	state.linkedGameObjectIds.push_back(
		gameObjectId);

	playerStates[gameObjectId] =
		std::move(state);
}


//================================================================
// Update
//================================================================

void TrailLinkPlayer::Update(
	int32_t gameObjectId,
	float deltaTime) {

	(void)gameObjectId;
	(void)deltaTime;
}


//================================================================
// FixedUpdate
//
// W/S = 前後移動
// A/D = Y軸旋回
//================================================================

void TrailLinkPlayer::FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {

	(void)fixedDeltaTime;

	if (EditorNativeScriptRuntime::GetRuntimeApi() == nullptr) {
		return;
	}

	//------------------------------------------------------------
	// moveInput_.x
	//
	// A = -1
	// D = +1
	//
	// moveInput_.y
	//
	// S = -1
	// W = +1
	//------------------------------------------------------------

	const float turnInput =
		moveInput_.x;

	const float forwardInput =
		moveInput_.y;

	//------------------------------------------------------------
	// 現在のPlayer向きを取得
	//
	// CG2のローカル前方は +Z。
	//------------------------------------------------------------

	const GameObject player{
		gameObjectId
	};

	const EditorScriptTransform transform =
		player.GetTransform();

	const float yaw =
		transform.rotation.y;

	const float forwardX =
		std::sin(yaw);

	const float forwardZ =
		std::cos(yaw);

	//------------------------------------------------------------
	// W / S
	//
	// 現在向いている方向へ前後移動
	//------------------------------------------------------------

	Rigidbody rigidbody{
		gameObjectId
	};

	EditorScriptVector3 velocity =
		rigidbody.GetVelocity();

	velocity.x =
		forwardX *
		forwardInput *
		moveSpeed_;

	velocity.z =
		forwardZ *
		forwardInput *
		moveSpeed_;

	// Y速度は重力や落下を残す。
	rigidbody.SetVelocity(
		velocity);

	//------------------------------------------------------------
	// A / D
	//
	// RigidbodyのY軸角速度を設定
	//
	// X/Z角速度は0にして、
	// Playerが鎖に引っ張られて横倒しするのを抑える。
	//------------------------------------------------------------

	EditorScriptVector3 angularVelocity{
		0.0f,
		turnInput * turnSpeed,
		0.0f
	};

	EditorNativeScriptRuntime::GetRuntimeApi()->SetAngularVelocity(
		gameObjectId,
		&angularVelocity);
}


//================================================================
// Physics Event
//================================================================

void TrailLinkPlayer::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	// Block側からOnPickupBlockを送る方式なので、
	// Player自身のCollisionでは取得処理をしない。
	(void)physicsEvent;
}

void TrailLinkPlayer::OnTriggerEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	(void)physicsEvent;
}


//================================================================
// Stop
//================================================================

void TrailLinkPlayer::Stop(
	int32_t gameObjectId) {

	moveInput_ = {};

	auto stateIt =
		playerStates.find(gameObjectId);

	if (stateIt ==
		playerStates.end()) {

		return;
	}

	//------------------------------------------------------------
	// Runtime Jointを全部破棄
	//------------------------------------------------------------

	for (const JointHandle jointHandle :
	stateIt->second.springJointHandles) {

		if (Physics::IsJointValid(
			jointHandle)) {

			Physics::DestroyJoint(
				jointHandle);
		}
	}

	playerStates.erase(
		stateIt);
}


//================================================================
// Move Input
//================================================================

void TrailLinkPlayer::OnMove(
	const EditorScriptInputActionContext& inputContext) {

	if (inputContext.phase ==
		EditorScriptInputPhaseCanceled) {

		moveInput_ =
			EditorScriptVector2{};

		return;
	}

	moveInput_ =
		inputContext.vector2Value;
}


//================================================================
// Jump
//
// TRAIL LINKでは使用しない。
//================================================================

void TrailLinkPlayer::OnJump(
	const EditorScriptInputActionContext& inputContext) {

	(void)inputContext;
}


//================================================================
// Space Attack
//================================================================

void TrailLinkPlayer::OnFire(
	const EditorScriptInputActionContext& inputContext) {

	if (inputContext.phase !=
		EditorScriptInputPhasePerformed) {

		return;
	}

	auto stateIt =
		playerStates.find(
			inputContext.gameObjectId);

	if (stateIt ==
		playerStates.end()) {

		return;
	}

	PlayerRuntimeState& state =
		stateIt->second;

	//------------------------------------------------------------
	// Playerしかいないなら攻撃しない
	//------------------------------------------------------------

	if (state.linkedGameObjectIds.size()
		<= 1) {

		return;
	}

	//------------------------------------------------------------
	// 現在のPlayer向き
	//------------------------------------------------------------

	const EditorScriptTransform transform =
		GameObject{
			inputContext.gameObjectId
	}.GetTransform();

	const float yaw =
		transform.rotation.y;

	//------------------------------------------------------------
	// Player右方向
	//
	// Forward
	// (sin(yaw), 0, cos(yaw))
	//
	// Right
	// (cos(yaw), 0, -sin(yaw))
	//------------------------------------------------------------

	float attackX =
		std::cos(yaw);

	float attackZ =
		-std::sin(yaw);

	//------------------------------------------------------------
	// Spaceを押すたび
	//
	// 右 → 左 → 右 → 左
	//
	// と振る。
	//------------------------------------------------------------

	if (!state.attackRight) {
		attackX =
			-attackX;

		attackZ =
			-attackZ;
	}

	state.attackRight =
		!state.attackRight;

	//------------------------------------------------------------
	// Block数
	//------------------------------------------------------------

	const size_t blockCount =
		state.linkedGameObjectIds.size()
		- 1;

	//------------------------------------------------------------
	// 全BlockへImpulse
	//------------------------------------------------------------

	for (size_t index = 1;
		index <
		state.linkedGameObjectIds.size();
		++index) {

		const int32_t blockId =
			state.linkedGameObjectIds[index];

		//--------------------------------------------------------
		// 根元は弱く
		// 末尾ほど強く
		//--------------------------------------------------------

		const float ratio =
			static_cast<float>(index) /
			static_cast<float>(blockCount);

		const float impulsePower =
			jumpImpulse_ *
			(
				0.3f +
				0.7f * ratio
				);

		//--------------------------------------------------------
		// Blockへ攻撃判定開始通知
		//--------------------------------------------------------

		GameObject{ blockId }.InvokeAction(
			"BeginAttack",
			ActionPayload::Float(
				attackWindowSeconds));

		//--------------------------------------------------------
		// 横方向へImpulse
		//--------------------------------------------------------

		Rigidbody{
			blockId
		}.AddImpulse(
			EditorScriptVector3{
				attackX *
					impulsePower,

				0.0f,

				attackZ *
					impulsePower
			});
	}
}


//================================================================
// 未使用Action
//================================================================

void TrailLinkPlayer::OnClick(
	const EditorScriptInputActionContext& inputContext) {

	(void)inputContext;
}

void TrailLinkPlayer::OnValueChanged(
	const EditorScriptInputActionContext& inputContext) {

	(void)inputContext;
}
