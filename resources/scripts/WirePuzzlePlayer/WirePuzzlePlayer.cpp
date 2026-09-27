#include "WirePuzzlePlayer.h"

#include <algorithm>
#include <cmath>
#include <vector>

//================================================================
// Player移動設定
//================================================================

SCRIPT_FIELD_FLOAT(
	horizontalHopImpulse,
	"横方向の跳ね力",
	2.5f,
	0.0f,
	50.0f,
	0.1f)

	SCRIPT_FIELD_FLOAT(
		verticalHopImpulse,
		"上方向の跳ね力",
		1.5f,
		0.0f,
		50.0f,
		0.1f)

	SCRIPT_FIELD_FLOAT(
		hopInterval,
		"跳ねる間隔",
		0.30f,
		0.05f,
		2.0f,
		0.01f)

	SCRIPT_FIELD_FLOAT(
		maxHorizontalSpeed,
		"最大水平速度",
		6.0f,
		0.1f,
		50.0f,
		0.1f)

	//================================================================
	// Wire設定
	//================================================================

	SCRIPT_FIELD_FLOAT(
		hookSelectionDistance,
		"フック選択距離",
		1000.0f,
		0.1f,
		10000.0f,
		0.1f)

	SCRIPT_FIELD_FLOAT(
		hookSelectionAngle,
		"フック選択角度",
		4.0f,
		0.0f,
		45.0f,
		0.1f)

	SCRIPT_FIELD_FLOAT(
		minimumWireLength,
		"ワイヤー最小長",
		0.5f,
		0.0f,
		100.0f,
		0.05f)

	SCRIPT_FIELD_FLOAT(
		shrinkSpeed,
		"収縮速度",
		3.0f,
		0.0f,
		100.0f,
		0.1f)

	SCRIPT_FIELD_FLOAT(
		wireStiffness,
		"引っ張る強さ",
		1200.0f,
		0.0f,
		1000000.0f,
		10.0f)

	SCRIPT_FIELD_FLOAT(
		wireDamping,
		"揺れにくさ",
		80.0f,
		0.0f,
		100000.0f,
		1.0f)

	SCRIPT_FIELD_FLOAT(
		maximumTension,
		"最大の引っ張り力",
		0.0f,
		0.0f,
		1000000000.0f,
		10.0f)

	SCRIPT_FIELD_FLOAT(
		breakingTension,
		"切れる強さ",
		0.0f,
		0.0f,
		1000000000.0f,
		10.0f)

	//================================================================
	// Runtime状態
	//================================================================

	namespace {

	bool initialized = false;

	float hopCooldown = 0.0f;

	int32_t selectedFirstHookId = -1;
	int32_t targetedHookId = -1;

	EditorScriptVector3 selectedFirstLocalAnchor{};

	std::vector<Wire> activeWires;

	//============================================================
	// 2点間距離
	//============================================================

	float Distance(
		const EditorScriptVector3& firstPosition,
		const EditorScriptVector3& secondPosition)
	{
		const float x =
			secondPosition.x - firstPosition.x;

		const float y =
			secondPosition.y - firstPosition.y;

		const float z =
			secondPosition.z - firstPosition.z;

		return std::sqrt(
			x * x +
			y * y +
			z * z);
	}

	//============================================================
	// HookのAnchorをローカル座標で取得
	//============================================================

	bool ResolveHookLocalAnchor(
		const GameObject& hookGameObject,
		const EditorScriptVector3& hitWorldPosition,
		EditorScriptVector3& localAnchor)
	{
		const Component hookComponent =
			hookGameObject.GetComponent(
				"WireConnectable");

		if (!hookComponent.IsValid()) {
			return false;
		}

		bool useHitPoint = false;

		hookComponent.GetBool(
			"wireConnectableUseHitPoint",
			useHitPoint);

		if (useHitPoint) {
			return hookGameObject.WorldToLocalPoint(
				hitWorldPosition,
				localAnchor);
		}

		return hookComponent.GetVector3(
			"wireConnectableLocalAnchor",
			localAnchor);
	}

	//============================================================
	// Hookが接続可能か
	//============================================================

	bool CanConnectHook(
		int32_t hookGameObjectId)
	{
		if (hookGameObjectId < 0) {
			return false;
		}

		const GameObject hookGameObject{
			hookGameObjectId
		};

		if (!hookGameObject.HasReference()) {
			return false;
		}

		const HookPoint hookPoint{
			hookGameObject
		};

		if (!hookPoint.IsValid()) {
			return false;
		}

		return hookPoint.CanConnect();
	}

	//============================================================
	// Hookを通常状態へ戻す
	//============================================================

	void SetRestingHookVisual(
		int32_t hookGameObjectId)
	{
		if (hookGameObjectId < 0) {
			return;
		}

		if (hookGameObjectId ==
			selectedFirstHookId) {
			return;
		}

		const GameObject hookGameObject{
			hookGameObjectId
		};

		if (!hookGameObject.HasReference()) {
			return;
		}

		HookPoint{
			hookGameObject
		}.SetVisualState(
			Wire::GetCount(hookGameObject) > 0
			? HookVisualState::Connected
			: HookVisualState::Normal);
	}

	//============================================================
// 1個目に選択したHookのSelected表示を維持
// 視点から外れても解除しない
//============================================================

	void KeepSelectedHookVisual()
	{
		if (selectedFirstHookId < 0) {
			return;
		}

		const GameObject selectedHook{
			selectedFirstHookId
		};

		if (!selectedHook.HasReference()) {
			selectedFirstHookId = -1;
			selectedFirstLocalAnchor = {};
			return;
		}

		HookPoint{
			selectedHook
		}.SetVisualState(
			HookVisualState::Selected);
	}


	//============================================================
	// 選択解除
	//============================================================

	void ResetSelection()
	{
		if (selectedFirstHookId >= 0) {

			const GameObject selectedHook{
				selectedFirstHookId
			};

			if (selectedHook.HasReference()) {

				HookPoint{
					selectedHook
				}.SetVisualState(
					Wire::GetCount(selectedHook) > 0
					? HookVisualState::Connected
					: HookVisualState::Normal);
			}
		}

		selectedFirstHookId = -1;
		selectedFirstLocalAnchor = {};
	}

	//============================================================
	// 無効Wire除去
	//============================================================

	void RemoveInvalidWires()
	{
		for (
			auto wireIterator =
			activeWires.begin();

			wireIterator !=
			activeWires.end();)
		{
			if (!wireIterator->IsValid()) {

				wireIterator =
					activeWires.erase(
						wireIterator);
			}
			else {
				++wireIterator;
			}
		}
	}

	//============================================================
	// 初回初期化
	//============================================================

	void Initialize()
	{
		activeWires.clear();

		hopCooldown = 0.0f;

		selectedFirstHookId = -1;
		targetedHookId = -1;
		selectedFirstLocalAnchor = {};

		for (
			const GameObject& hookGameObject :
			GameObject::FindAllWithComponent(
				"WireConnectable"))
		{
			HookPoint{
				hookGameObject
			}.SetVisualState(
				HookVisualState::Normal);
		}

		initialized = true;
	}
}

//================================================================
// Update
//================================================================

void WirePuzzlePlayer::Update(
	float deltaTime)
{
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

	if (runtimeApi == nullptr) {
		return;
	}

	const GameObject player =
		GetGameObject();

	if (!player.HasReference()) {
		return;
	}

	//============================================================
	// 初回初期化
	//============================================================

	if (!initialized) {

		Initialize();

		runtimeApi->Log(
			"WirePuzzlePlayer: READY");
	}

	//============================================================
	// Wire描画設定
	//============================================================

	player.GetOrAddComponent<WireRenderer>();

	RemoveInvalidWires();

	//============================================================
	// Player
	// スライム風の物理移動
	//
	// Transformは一切変更しない。
	// WASD入力中、一定間隔で
	// 横方向 + 上方向へImpulseを加える。
	//============================================================

	float moveX = 0.0f;
	float moveZ = 0.0f;

	if (Input::GetKey(KeyCode::W)) {
		moveZ += 1.0f;
	}

	if (Input::GetKey(KeyCode::S)) {
		moveZ -= 1.0f;
	}

	if (Input::GetKey(KeyCode::A)) {
		moveX -= 1.0f;
	}

	if (Input::GetKey(KeyCode::D)) {
		moveX += 1.0f;
	}

	const float moveLength =
		std::sqrt(
			moveX * moveX +
			moveZ * moveZ);

	const bool hasMoveInput =
		moveLength > 0.001f;

	if (hasMoveInput) {

		if (moveLength > 1.0f) {
			moveX /= moveLength;
			moveZ /= moveLength;
		}

		hopCooldown -= deltaTime;

		if (hopCooldown <= 0.0f) {

			Rigidbody playerBody{
				player
			};

			const EditorScriptVector3 velocity =
				playerBody.GetVelocity();

			const float horizontalSpeed =
				std::sqrt(
					velocity.x * velocity.x +
					velocity.z * velocity.z);

			float horizontalImpulse =
				FieldFloat(
					"horizontalHopImpulse");

			//----------------------------------------------------
			// 既に速すぎる時は
			// さらに横へ加速させない
			//----------------------------------------------------

			if (
				horizontalSpeed >=
				FieldFloat(
					"maxHorizontalSpeed"))
			{
				horizontalImpulse =
					0.0f;
			}

			const EditorScriptVector3 impulse{
				moveX * horizontalImpulse,
				FieldFloat(
					"verticalHopImpulse"),
				moveZ * horizontalImpulse
			};

			playerBody.AddImpulse(
				impulse);

			hopCooldown =
				FieldFloat(
					"hopInterval");
		}
	}
	else {

		//--------------------------------------------------------
		// 止まってから再び入力した時は
		// すぐ次のジャンプを出せるようにする
		//--------------------------------------------------------

		hopCooldown = 0.0f;
	}

	//============================================================
	// Hook照準
	//============================================================

	EditorScriptRay aimRay{};
	EditorScriptPhysicsHit aimedHookHit{};

	const bool hasAimRay =
		Physics::GetAimRay(
			player,
			aimRay);

	//============================================================
// カメラ方向へPlayerを向ける
// 上下方向は無視してY軸だけ回転
//============================================================

	if (hasAimRay) {

		const float cameraDirectionLength =
			std::sqrt(
				aimRay.direction.x * aimRay.direction.x +
				aimRay.direction.z * aimRay.direction.z);

		if (cameraDirectionLength > 0.001f) {

			const float targetDirectionX =
				aimRay.direction.x /
				cameraDirectionLength;

			const float targetDirectionZ =
				aimRay.direction.z /
				cameraDirectionLength;

			const EditorScriptTransform playerTransform =
				player.GetTransform();

			const float playerYaw =
				playerTransform.rotation.y;

			const float playerYawRadians =
				playerYaw *
				3.1415926535f /
				180.0f;

			const float playerForwardX =
				std::sin(
					playerYawRadians);

			const float playerForwardZ =
				std::cos(
					playerYawRadians);

			// Y軸まわりの符号付きずれ
			const float turnError =
				playerForwardZ *
				targetDirectionX -
				playerForwardX *
				targetDirectionZ;

			Rigidbody{
				player
			}.AddTorque(
				EditorScriptVector3{
					0.0f,
					turnError *
						FieldFloat(
							"cameraTurnStrength"),
					0.0f
				});
		}
	}


	const bool hasAimedHook =
		hasAimRay &&
		Physics::FindBestHook(
			aimRay,
			FieldFloat(
				"hookSelectionDistance"),
			FieldFloat(
				"hookSelectionAngle"),
			aimedHookHit);

	int32_t nextTargetedHookId = -1;

	if (
		hasAimedHook &&
		aimedHookHit.gameObjectId >= 0)
	{
		const int32_t candidateHookId =
			aimedHookHit.gameObjectId;

		if (
			candidateHookId ==
			selectedFirstHookId ||
			CanConnectHook(
				candidateHookId))
		{
			nextTargetedHookId =
				candidateHookId;
		}
	}

	//============================================================
	// 照準対象変更
	//============================================================

	if (
		targetedHookId !=
		nextTargetedHookId)
	{
		SetRestingHookVisual(
			targetedHookId);

		targetedHookId =
			nextTargetedHookId;
	}

	//============================================================
	// 照準中表示
	//============================================================

	//============================================================
// 照準中表示
//============================================================

	if (
		targetedHookId >= 0 &&
		targetedHookId !=
		selectedFirstHookId)
	{
		HookPoint{
			targetedHookId
		}.SetVisualState(
			HookVisualState::Targeted);
	}

	//============================================================
	// 1個目のHookは視点から外れてもSelectedを維持
	//============================================================

	KeepSelectedHookVisual();

	//============================================================
	// 右クリック
	// 選択キャンセル
	//============================================================

	if (
		Input::GetMouseButtonDown(
			MouseButton::Right))
	{
		ResetSelection();

		return;
	}

	//============================================================
	// E
	// 最後のWireを解除
	//============================================================

	if (
		Input::GetKeyDown(KeyCode::E) &&
		!activeWires.empty())
	{
		Wire::State detachedWireState{};

		const bool hasWireState =
			activeWires.back().GetState(
				detachedWireState);

		activeWires.back().Destroy();

		activeWires.pop_back();

		if (hasWireState) {

			SetRestingHookVisual(
				detachedWireState
				.firstGameObjectId);

			SetRestingHookVisual(
				detachedWireState
				.secondGameObjectId);
		}
	}

	//============================================================
	// 左クリック
	// Hook選択 / Wire接続
	//============================================================

	if (
		Input::GetMouseButtonDown(
			MouseButton::Left))
	{
		if (!hasAimRay) {
			return;
		}

		if (
			!hasAimedHook ||
			aimedHookHit.gameObjectId < 0)
		{
			runtimeApi->Log(
				"Wire: NO HOOK TARGET");

			return;
		}

		const int32_t clickedHookId =
			aimedHookHit.gameObjectId;

		const GameObject clickedHook{
			clickedHookId
		};

		//--------------------------------------------------------
		// 1個目
		//--------------------------------------------------------

		if (selectedFirstHookId < 0) {

			if (!CanConnectHook(
				clickedHookId))
			{
				return;
			}

			EditorScriptVector3
				firstLocalAnchor{};

			if (!ResolveHookLocalAnchor(
				clickedHook,
				aimedHookHit.point,
				firstLocalAnchor))
			{
				return;
			}

			selectedFirstHookId =
				clickedHookId;

			selectedFirstLocalAnchor =
				firstLocalAnchor;

			HookPoint{
				clickedHook
			}.SetVisualState(
				HookVisualState::Selected);

			return;
		}

		//--------------------------------------------------------
		// 同じHookは不可
		//--------------------------------------------------------

		if (
			clickedHookId ==
			selectedFirstHookId)
		{
			return;
		}

		//--------------------------------------------------------
		// 接続可能確認
		//--------------------------------------------------------

		if (
			!CanConnectHook(
				selectedFirstHookId) ||
			!CanConnectHook(
				clickedHookId))
		{
			ResetSelection();

			return;
		}

		const GameObject firstHook{
			selectedFirstHookId
		};

		const GameObject secondHook{
			clickedHookId
		};

		//--------------------------------------------------------
		// 2個目Anchor
		//--------------------------------------------------------

		EditorScriptVector3
			secondLocalAnchor{};

		if (!ResolveHookLocalAnchor(
			secondHook,
			aimedHookHit.point,
			secondLocalAnchor))
		{
			ResetSelection();

			return;
		}

		//--------------------------------------------------------
		// Anchorを現在World座標へ変換
		//--------------------------------------------------------

		EditorScriptVector3
			firstWorldAnchor{};

		EditorScriptVector3
			secondWorldAnchor{};

		if (
			!firstHook.LocalToWorldPoint(
				selectedFirstLocalAnchor,
				firstWorldAnchor) ||
			!secondHook.LocalToWorldPoint(
				secondLocalAnchor,
				secondWorldAnchor))
		{
			ResetSelection();

			return;
		}

		//--------------------------------------------------------
		// 初期Wire長
		//--------------------------------------------------------

		const float currentDistance =
			Distance(
				firstWorldAnchor,
				secondWorldAnchor);

		const float minimumLength =
			FieldFloat(
				"minimumWireLength");

		const float initialLength =
			(std::max)(
				currentDistance,
				minimumLength);

		//--------------------------------------------------------
		// Wire生成
		//--------------------------------------------------------

		EditorScriptWireDesc
			wireDescription{};

		wireDescription
			.firstGameObjectId =
			firstHook.GetInstanceId();

		wireDescription
			.secondGameObjectId =
			secondHook.GetInstanceId();

		wireDescription
			.ownerGameObjectId =
			player.GetInstanceId();

		wireDescription
			.rendererSettingsGameObjectId =
			player.GetInstanceId();

		wireDescription
			.firstLocalAnchor =
			selectedFirstLocalAnchor;

		wireDescription
			.secondLocalAnchor =
			secondLocalAnchor;

		wireDescription
			.maximumLength =
			initialLength;

		wireDescription
			.minimumLength =
			minimumLength;

		wireDescription
			.stiffness =
			FieldFloat(
				"wireStiffness");

		wireDescription
			.damping =
			FieldFloat(
				"wireDamping");

		wireDescription
			.maximumTension =
			FieldFloat(
				"maximumTension");

		wireDescription
			.breakingTension =
			FieldFloat(
				"breakingTension");

		wireDescription
			.shrinkSpeed =
			0.0f;

		wireDescription
			.applyReaction =
			true;

		wireDescription
			.requireConnectable =
			true;

		Wire newWire =
			Wire::Create(
				wireDescription);

		if (!newWire.IsValid()) {

			runtimeApi->Log(
				"Wire: CREATE FAILED");

			ResetSelection();

			return;
		}

		//--------------------------------------------------------
		// 接続成功
		//--------------------------------------------------------

		activeWires.push_back(
			newWire);

		HookPoint{
			firstHook
		}.SetVisualState(
			HookVisualState::Connected);

		HookPoint{
			secondHook
		}.SetVisualState(
			HookVisualState::Connected);

		ResetSelection();
	}

	//============================================================
	// Space保持
	// 全Wire収縮
	//============================================================

	const bool isShrinking =
		Input::GetKey(
			KeyCode::Space);

	const float currentShrinkSpeed =
		isShrinking
		? FieldFloat(
			"shrinkSpeed")
		: 0.0f;

	for (
		const Wire& wire :
		activeWires)
	{
		wire.SetShrinkSpeed(
			currentShrinkSpeed);
	}
}