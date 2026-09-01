#include "WirePlayerScript.h"

#include <cmath>
#include <vector>

//================================================================
// Inspector設定
// .hへメンバーを追加せず、この.cppだけで公開変数を定義する。
//================================================================

SCRIPT_FIELD_FLOAT(moveSpeed, "移動速度", 6.0f, 0.0f, 100.0f, 0.1f)
SCRIPT_FIELD_FLOAT(hookSelectionDistance, "フック選択距離", 1000.0f, 0.1f, 10000.0f, 0.1f)
SCRIPT_FIELD_FLOAT(hookSelectionAngle, "フック選択角度", 4.0f, 0.0f, 45.0f, 0.1f)
SCRIPT_FIELD_FLOAT(minimumWireLength, "ワイヤー最小長", 0.5f, 0.0f, 100.0f, 0.05f)
SCRIPT_FIELD_FLOAT(shrinkSpeed, "収縮速度", 3.0f, 0.0f, 100.0f, 0.1f)
SCRIPT_FIELD_FLOAT(wireStiffness, "張力係数", 1200.0f, 0.0f, 1000000.0f, 10.0f)
SCRIPT_FIELD_FLOAT(wireDamping, "減衰", 80.0f, 0.0f, 100000.0f, 1.0f)
SCRIPT_FIELD_FLOAT(maximumTension, "張力上限", 0.0f, 0.0f, 1000000000.0f, 10.0f)
SCRIPT_FIELD_FLOAT(breakingTension, "破断張力", 0.0f, 0.0f, 1000000000.0f, 10.0f)

namespace {

	constexpr float kInitialTensionOffset = 0.05f;

	int32_t selectedFirstTargetId = -1;
	int32_t targetedHookId = -1;
	EditorScriptVector3 selectedFirstHitWorld{};
	std::vector<Wire> activeWires;

	float Distance(
		const EditorScriptVector3& firstPosition,
		const EditorScriptVector3& secondPosition) {
		const float distanceX = secondPosition.x - firstPosition.x;
		const float distanceY = secondPosition.y - firstPosition.y;
		const float distanceZ = secondPosition.z - firstPosition.z;
		return std::sqrt(
			distanceX * distanceX +
			distanceY * distanceY +
			distanceZ * distanceZ);
	}

	EditorScriptVector3 ResolveHookWorldAnchor(
		const GameObject& hookGameObject,
		const EditorScriptVector3& hitWorldPosition) {
		const Component hookComponent = hookGameObject.GetComponent("WireConnectable");
		bool useHitPoint = false;
		hookComponent.GetBool("wireConnectableUseHitPoint", useHitPoint);

		if (useHitPoint) {
			return hitWorldPosition;
		}

		EditorScriptVector3 localAnchor{};
		EditorScriptVector3 worldAnchor = hitWorldPosition;
		hookComponent.GetVector3("wireConnectableLocalAnchor", localAnchor);
		hookGameObject.LocalToWorldPoint(localAnchor, worldAnchor);
		return worldAnchor;
	}

	void ResetSelection() {
		if (selectedFirstTargetId >= 0) {
			const GameObject selectedHook{selectedFirstTargetId};
			HookPoint{selectedHook}.SetVisualState(
				Wire::GetCount(selectedHook) > 0
				? HookVisualState::Connected
				: HookVisualState::Normal);
		}

		selectedFirstTargetId = -1;
		selectedFirstHitWorld = {};
	}

	void RemoveInvalidWires() {
		for (auto wireIterator = activeWires.begin(); wireIterator != activeWires.end();) {
			if (!wireIterator->IsValid()) {
				wireIterator = activeWires.erase(wireIterator);
			}
			else {
				++wireIterator;
			}
		}
	}

	void SetRestingHookVisual(int32_t hookGameObjectId) {
		if (hookGameObjectId < 0 || hookGameObjectId == selectedFirstTargetId) {
			return;
		}

		const GameObject hookGameObject{hookGameObjectId};
		HookPoint{hookGameObject}.SetVisualState(
			Wire::GetCount(hookGameObject) > 0
			? HookVisualState::Connected
			: HookVisualState::Normal);
	}
}

//================================================================
// Start / Stop
//================================================================

void WirePlayerScript::Start() {
	activeWires.clear();
	targetedHookId = -1;
	selectedFirstTargetId = -1;
	selectedFirstHitWorld = {};

	for (const GameObject& hookGameObject : GameObject::FindAllWithComponent("WireConnectable")) {
		HookPoint{hookGameObject}.SetVisualState(HookVisualState::Normal);
	}
}

void WirePlayerScript::Stop() {
	for (const Wire& wire : activeWires) {
		wire.Destroy();
	}

	activeWires.clear();
	targetedHookId = -1;
	selectedFirstTargetId = -1;
	selectedFirstHitWorld = {};
}

//================================================================
// Update
//================================================================

void WirePlayerScript::Update(float deltaTime) {
	const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

	if (runtimeApi == nullptr) {
		return;
	}

	const GameObject player = GetGameObject();

	if (!player.HasReference()) {
		return;
	}

	// WireRendererは実行時に自動追加するため、Playerへ事前配置する必要はない。
	player.GetOrAddComponent<WireRenderer>();
	RemoveInvalidWires();

	//============================================================
	// Hook照準表示
	//============================================================

	EditorScriptRay aimRay{};
	EditorScriptPhysicsHit aimedHookHit{};
	const bool hasAimRay = Physics::GetAimRay(player, aimRay);
	const bool hasAimedHook = hasAimRay && Physics::FindBestHook(
		aimRay,
		FieldFloat("hookSelectionDistance"),
		FieldFloat("hookSelectionAngle"),
		aimedHookHit);
	const int32_t nextTargetedHookId = hasAimedHook ? aimedHookHit.gameObjectId : -1;

	if (targetedHookId != nextTargetedHookId) {
		SetRestingHookVisual(targetedHookId);
		targetedHookId = nextTargetedHookId;
	}

	if (targetedHookId >= 0 && targetedHookId != selectedFirstTargetId) {
		HookPoint{targetedHookId}.SetVisualState(HookVisualState::Targeted);
	}

	//============================================================
	// Player移動
	//============================================================

	if (runtimeApi->GetActionVector2 != nullptr) {
		EditorScriptVector2 moveInput = runtimeApi->GetActionVector2(
			player.GetInstanceId(),
			"Player",
			"Move");
		const float inputLength = std::sqrt(
			moveInput.x * moveInput.x +
			moveInput.y * moveInput.y);

		if (inputLength > 0.001f) {
			if (inputLength > 1.0f) {
				moveInput.x /= inputLength;
				moveInput.y /= inputLength;
			}

			EditorScriptTransform playerTransform = player.GetTransform();
			playerTransform.position.x += moveInput.x * FieldFloat("moveSpeed") * deltaTime;
			playerTransform.position.z += moveInput.y * FieldFloat("moveSpeed") * deltaTime;
			player.SetTransform(playerTransform);
		}
	}

	//============================================================
	// 選択解除 / 最新Wire削除
	//============================================================

	if (runtimeApi->WasActionJustPressed != nullptr &&
		runtimeApi->WasActionJustPressed(player.GetInstanceId(), "Player", "Cancel")) {
		ResetSelection();
	}

	if (runtimeApi->WasActionJustPressed != nullptr &&
		runtimeApi->WasActionJustPressed(player.GetInstanceId(), "Player", "Detach") &&
		!activeWires.empty()) {
		Wire::State detachedWireState{};
		activeWires.back().GetState(detachedWireState);
		activeWires.back().Destroy();
		activeWires.pop_back();
		SetRestingHookVisual(detachedWireState.firstGameObjectId);
		SetRestingHookVisual(detachedWireState.secondGameObjectId);
	}

	//============================================================
	// 2点選択とWire生成
	//============================================================

	if (runtimeApi->WasActionJustPressed != nullptr &&
		runtimeApi->WasActionJustPressed(player.GetInstanceId(), "Player", "Connect")) {
		if (!hasAimRay) {
			runtimeApi->Log("Wire: AIM RAY FAILED");
			ResetSelection();
			return;
		}

		if (!hasAimedHook || aimedHookHit.gameObjectId < 0) {
			runtimeApi->Log("Wire: NO CONNECTABLE TARGET");
			ResetSelection();
			return;
		}

		if (selectedFirstTargetId < 0) {
			selectedFirstTargetId = aimedHookHit.gameObjectId;
			selectedFirstHitWorld = ResolveHookWorldAnchor(
				GameObject{selectedFirstTargetId},
				aimedHookHit.point);
			HookPoint{selectedFirstTargetId}.SetVisualState(HookVisualState::Selected);
			runtimeApi->Log("Wire: FIRST TARGET");
			return;
		}

		if (aimedHookHit.gameObjectId == selectedFirstTargetId) {
			runtimeApi->Log("Wire: SAME TARGET");
			return;
		}

		const GameObject firstTarget{selectedFirstTargetId};
		const GameObject secondTarget{aimedHookHit.gameObjectId};
		const EditorScriptVector3 secondHookWorldAnchor = ResolveHookWorldAnchor(
			secondTarget,
			aimedHookHit.point);
		const float selectedDistance = Distance(selectedFirstHitWorld, secondHookWorldAnchor);
		EditorScriptVector3 firstLocalAnchor{};
		EditorScriptVector3 secondLocalAnchor{};

		if (!firstTarget.WorldToLocalPoint(selectedFirstHitWorld, firstLocalAnchor) ||
			!secondTarget.WorldToLocalPoint(secondHookWorldAnchor, secondLocalAnchor)) {
			runtimeApi->Log("Wire: ANCHOR CONVERSION FAILED");
			ResetSelection();
			return;
		}

		const float minimumLength = FieldFloat("minimumWireLength");
		const float initialLength = (std::max)(
			selectedDistance - kInitialTensionOffset,
			minimumLength);
		EditorScriptWireDesc wireDescription{};
		wireDescription.firstGameObjectId = firstTarget.GetInstanceId();
		wireDescription.secondGameObjectId = secondTarget.GetInstanceId();
		wireDescription.ownerGameObjectId = player.GetInstanceId();
		wireDescription.rendererSettingsGameObjectId = player.GetInstanceId();
		wireDescription.firstLocalAnchor = firstLocalAnchor;
		wireDescription.secondLocalAnchor = secondLocalAnchor;
		wireDescription.maximumLength = initialLength;
		wireDescription.minimumLength = minimumLength;
		wireDescription.stiffness = FieldFloat("wireStiffness");
		wireDescription.damping = FieldFloat("wireDamping");
		wireDescription.maximumTension = FieldFloat("maximumTension");
		wireDescription.breakingTension = FieldFloat("breakingTension");
		wireDescription.shrinkSpeed = 0.0f;
		wireDescription.applyReaction = true;
		wireDescription.requireConnectable = true;

		Wire newWire = Wire::Create(wireDescription);

		if (!newWire.IsValid()) {
			runtimeApi->Log("Wire: CREATE FAILED");
			ResetSelection();
			return;
		}

		activeWires.push_back(newWire);
		HookPoint{firstTarget}.SetVisualState(HookVisualState::Connected);
		HookPoint{secondTarget}.SetVisualState(HookVisualState::Connected);
		runtimeApi->Log("Wire: CONNECTED");
		ResetSelection();
	}

	//============================================================
	// 全Wireの収縮
	//============================================================

	const bool isShrinking = runtimeApi->IsActionPressed != nullptr &&
		runtimeApi->IsActionPressed(player.GetInstanceId(), "Player", "Shrink");
	const float currentShrinkSpeed = isShrinking ? FieldFloat("shrinkSpeed") : 0.0f;

	for (const Wire& wire : activeWires) {
		wire.SetShrinkSpeed(currentShrinkSpeed);
	}
}
