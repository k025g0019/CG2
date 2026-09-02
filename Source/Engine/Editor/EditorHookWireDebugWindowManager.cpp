#pragma warning(disable : 4189 4514)

#include "EditorHookWireDebugWindowManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <cmath>
#include <string>
#include <vector>

using namespace EditorSharedState;

namespace {
	// Hookの選択判定に使えるCollider種別。物体本体の衝突用Colliderとは役割が別だが、
	// 種類そのものは同じものを使うため、いずれか1つでも付いていれば選択可能とみなす。
	bool HasAnyCollider(const EditorGameObject& gameObject) {
		constexpr EditorComponentType colliderTypes[] = {
			EditorComponentType::BoxCollider,
			EditorComponentType::SphereCollider,
			EditorComponentType::CapsuleCollider,
			EditorComponentType::MeshCollider};

		for (const EditorComponentType colliderType : colliderTypes) {
			const EditorComponent* collider =
				EditorComponentUtility::FindComponent(gameObject, colliderType);
			if (collider != nullptr && collider->isActive) {
				return true;
			}
		}
		return false;
	}

	bool HasAnyRenderer(const EditorGameObject& gameObject) {
		const EditorComponent* modelRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && modelRenderer->isActive) {
			return true;
		}

		const EditorComponent* skinnedRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SkinnedMeshRenderer);
		return skinnedRenderer != nullptr && skinnedRenderer->isActive;
	}

	const char* ResolveGameObjectName(int32_t gameObjectId) {
		const EditorGameObject* gameObject = g_editorScene.FindGameObject(gameObjectId);
		return gameObject != nullptr ? gameObject->name.c_str() : "(見つかりません)";
	}

	Vector3 ResolveWorldPosition(const EditorGameObject& gameObject) {
		Vector3 scale = gameObject.scale;
		Vector3 rotate = gameObject.rotate;
		Vector3 translate = gameObject.translate;
		g_editorScene.GetWorldTransform(gameObject.id, scale, rotate, translate);
		return translate;
	}

	void DrawWarningText(const char* message) {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.15f, 1.0f), "%s", message);
	}
}  // namespace

void EditorHookWireDebugWindowManager::Initialize() {
}

void EditorHookWireDebugWindowManager::Update() {
}

void EditorHookWireDebugWindowManager::Draw() {
#ifdef USE_IMGUI
	if (!g_isHookWireDebugWindowVisible) {
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Hook / Wire デバッグ###HookWireDebug", &g_isHookWireDebugWindowVisible)) {
		ImGui::End();
		return;
	}

	ImGui::Checkbox("SceneViewへHookの構成を重ねる", &g_isHookWireSceneGizmoVisible);
	ImGui::TextDisabled("Hook→力を伝えるRigidbodyの線と、Anchorの実位置を表示します。");

	if (ImGui::BeginTabBar("HookWireDebugTabs")) {
		if (ImGui::BeginTabItem("Hook構成")) {
			DrawHookList();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Runtime Wire")) {
			DrawWireList();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
#endif
}

void EditorHookWireDebugWindowManager::DrawHookList() {
#ifdef USE_IMGUI
	int32_t hookCount = 0;

	for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		const EditorComponent* hookComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::WireConnectable);
		if (hookComponent == nullptr) {
			continue;
		}

		hookCount++;
		ImGui::PushID(gameObject.id);

		// 設定不備を先に数えて見出しへ出す。畳んだままでも問題のあるHookに気付けるようにする。
		const int32_t physicsBodyGameObjectId =
			hookComponent->wireConnectablePhysicsBodyGameObjectId >= 0
				? hookComponent->wireConnectablePhysicsBodyGameObjectId
				: gameObject.id;
		const EditorGameObject* physicsBodyGameObject =
			g_editorScene.FindGameObject(physicsBodyGameObjectId);
		const EditorComponent* physicsBodyRigidBody = physicsBodyGameObject != nullptr
			? EditorComponentUtility::FindComponent(*physicsBodyGameObject, EditorComponentType::RigidBody)
			: nullptr;

		const bool hasRenderer = HasAnyRenderer(gameObject);
		const bool hasCollider = HasAnyCollider(gameObject);
		// Raycastで狙えるかはCollider(FindMainCollider)の有無だけで決まり、Rigidbodyは無関係。
		// 親を持つ子Hookは力を親へ渡すだけの選択点で、Rigidbodyは不要（無いのが正しい構成）。
		const EditorComponent* hookRigidBody =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
		const bool hasHookRigidBody = hookRigidBody != nullptr && hookRigidBody->isActive;
		const bool hasValidPhysicsBody = physicsBodyGameObject != nullptr;
		const bool hasPhysicsBodyRigidBody = physicsBodyRigidBody != nullptr;
		const bool isSelfPhysicsBody = physicsBodyGameObjectId == gameObject.id;
		const bool hasParent = gameObject.parentId >= 0;
		// FindBestHookはRaycastの命中GameObjectがHook自身と一致するかで選択判定する。
		// 親のローカル原点(0,0,0)へ置いたままだと親自身の当たり判定に埋まり、
		// Rayが親の表面で止まってHookへ絶対に届かない。
		const bool isEmbeddedAtParentOrigin =
			hasParent &&
			std::fabs(gameObject.translate.x) < 0.0001f &&
			std::fabs(gameObject.translate.y) < 0.0001f &&
			std::fabs(gameObject.translate.z) < 0.0001f;

		int32_t issueCount = 0;
		if (!hasRenderer) issueCount++;
		if (!hasCollider) issueCount++;
		if (hasHookRigidBody && hasParent) issueCount++;
		if (!hasValidPhysicsBody) issueCount++;
		if (!hasPhysicsBodyRigidBody) issueCount++;
		if (isSelfPhysicsBody && hasParent) issueCount++;
		if (isSelfPhysicsBody && !hasHookRigidBody) issueCount++;
		if (isEmbeddedAtParentOrigin) issueCount++;

		const std::string headerLabel = issueCount > 0
			? gameObject.name + "  [要確認 " + std::to_string(issueCount) + "]"
			: gameObject.name;

		if (ImGui::CollapsingHeader(headerLabel.c_str(), issueCount > 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
			if (ImGui::SmallButton("このHookを選択")) {
				SetSingleSelectedGameObject(gameObject.id);
			}

			ImGui::Text("親: %s", hasParent ? ResolveGameObjectName(gameObject.parentId) : "(なし)");
			ImGui::Text(
				"力を伝えるRigidbody: %s%s",
				ResolveGameObjectName(physicsBodyGameObjectId),
				isSelfPhysicsBody ? "（Hook自身）" : "");

			const Vector3 hookWorldPosition = ResolveWorldPosition(gameObject);
			ImGui::Text(
				"Hook World位置: %.2f, %.2f, %.2f",
				hookWorldPosition.x,
				hookWorldPosition.y,
				hookWorldPosition.z);
			ImGui::Text(
				"Anchor(ローカル): %.2f, %.2f, %.2f%s",
				hookComponent->wireConnectableLocalAnchor.x,
				hookComponent->wireConnectableLocalAnchor.y,
				hookComponent->wireConnectableLocalAnchor.z,
				hookComponent->wireConnectableUseHitPoint ? "  ※命中点を使う設定のため未使用" : "");
			ImGui::Text(
				"選択可能: %s / 最大接続本数: %s",
				hookComponent->wireConnectableAllowSelection ? "はい" : "いいえ",
				hookComponent->wireConnectableMaximumConnections > 0
					? std::to_string(hookComponent->wireConnectableMaximumConnections).c_str()
					: "無制限");

			if (g_editorRuntimeManager.IsPlaying()) {
				const int32_t connectedWireCount =
					g_editorRuntimeManager.GetPhysicsManager().GetWireCountForGameObject(gameObject.id);
				ImGui::Text("現在の接続Wire数: %d", connectedWireCount);
			}

			if (!hasRenderer) {
				DrawWarningText("Rendererがありません。Hookの見た目と状態色を表示できません。");
			}
			if (!hasCollider) {
				DrawWarningText("Colliderがありません。狙って選択できません（FindBestHookに当たりません）。");
			}
			if (hasHookRigidBody && hasParent) {
				DrawWarningText("子HookにRigidbodyが付いています。親から力を受け取らずJoltが独自に動かしてしまうため、外してください。");
			}
			if (isSelfPhysicsBody && !hasHookRigidBody) {
				DrawWarningText("このHookは力の伝達先が自分自身なのにRigidbodyがありません。Wireで引いても動きません。");
			}
			if (isEmbeddedAtParentOrigin) {
				DrawWarningText("親のローカル原点(0,0,0)にあります。親の当たり判定に埋まっているとRayが親で止まり、選択できません。表面へずらしてください。");
			}
			if (!hasValidPhysicsBody) {
				DrawWarningText("力を伝えるRigidbodyの参照先が見つかりません。Wireの力が伝わりません。");
			}
			else if (!hasPhysicsBodyRigidBody) {
				DrawWarningText("力を伝える先にRigidbodyがありません。引いても動きません（Static扱い）。");
			}
			if (isSelfPhysicsBody && hasParent) {
				DrawWarningText("子Hookなのに力の伝達先がHook自身です。親の物体を指定してください。");
			}
		}

		ImGui::PopID();
	}

	if (hookCount == 0) {
		ImGui::TextDisabled("HookPoint（WireConnectable）を持つGameObjectがありません。");
		ImGui::TextDisabled("Hierarchyの「作成 > Hook（選択物体の子）」で追加できます。");
	}
#endif
}

void EditorHookWireDebugWindowManager::DrawWireList() {
#ifdef USE_IMGUI
	if (!g_editorRuntimeManager.IsPlaying()) {
		ImGui::TextDisabled("Play中だけRuntime Wireを表示します。");
		return;
	}

	const auto& runtimeWires = g_editorRuntimeManager.GetPhysicsManager().GetRuntimeWires();

	if (runtimeWires.empty()) {
		ImGui::TextDisabled("接続中のWireはありません。");
		return;
	}

	for (const auto& [wireHandle, wireState] : runtimeWires) {
		ImGui::PushID(static_cast<int32_t>(wireHandle));
		ImGui::SeparatorText(
			(std::string("Wire #") + std::to_string(wireHandle) +
				(wireState.isBroken ? "  [破断]" : "")).c_str());

		ImGui::Text("Hook A: %s", ResolveGameObjectName(wireState.desc.firstGameObjectId));
		ImGui::Text("Hook B: %s", ResolveGameObjectName(wireState.desc.secondGameObjectId));
		ImGui::Text(
			"長さ: %.2f m  （最小 %.2f / 現在の上限 %.2f）",
			wireState.currentLength,
			wireState.desc.minimumLength,
			wireState.desc.maximumLength);
		ImGui::Text("張力: %.1f", wireState.currentTension);

		if (wireState.desc.breakingTension > 0.0f) {
			ImGui::Text("破断張力: %.1f", wireState.desc.breakingTension);
		}

		ImGui::Text("収縮速度: %.2f m/s", wireState.desc.shrinkSpeed);

		if (!wireState.isActive) {
			DrawWarningText("非Active（接続先を見失っています）。");
		}

		ImGui::PopID();
	}
#endif
}
