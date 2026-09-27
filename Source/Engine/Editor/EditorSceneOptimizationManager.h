#pragma once

#include "EditorScene.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorObjectPoolManager;

class EditorSceneOptimizationManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorPhysicsManager* physicsManager,
		EditorObjectPoolManager* objectPoolManager);  // 距離判定対象Scene、Physics停止、Pool貸出状態を接続する。
	void Start();  // 編集時のActive状態を保存してRuntime LODを初期化する。
	void Update();  // 距離を二乗値で比較し、Object実体とComponent系統を切り替える。
	void Stop();  // Play中に変更したActive状態を保存値へ戻す。

private:
	EditorScene* editorScene_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	EditorObjectPoolManager* objectPoolManager_ = nullptr;  // Pool ItemはScene Activeではなく貸出中状態を実体化要求として扱う。
	std::unordered_map<int32_t, bool> originalGameObjectActiveStates_;
	std::unordered_map<int32_t, bool> runtimeRequestedActiveStates_;  // PoolやScriptが要求したRuntime上のActive状態。
	std::unordered_map<int32_t, bool> lastManagedActiveStates_;  // LODが最後に適用したActive状態。外部変更との判別に使う。
	std::unordered_map<int32_t, std::vector<bool>> originalComponentActiveStates_;
	std::unordered_map<int32_t, std::vector<bool>> runtimeRequestedComponentActiveStates_;  // Game側が要求したComponent状態。
	std::unordered_map<int32_t, std::vector<bool>> lastManagedComponentActiveStates_;  // LOD適用値と外部変更を区別する。
	std::size_t updateCursor_ = 0u;  // 大規模Sceneの距離判定を複数Frameへ分散する開始位置。
	bool isStarted_ = false;

	bool ResolveReferencePosition(int32_t referenceGameObjectId, Vector3& position) const;  // 未設定時は最高Priority Cameraを使う。
	void SetHierarchyActive(int32_t gameObjectId, bool isActive, bool affectsHierarchy);  // 元の子Active状態を壊さず実体化を切り替える。
	void ApplySimulationLod(
		int32_t gameObjectId,
		const EditorComponent& simulationLod,
		int32_t lodLevel,
		bool affectsHierarchy);  // Far停止対象だけを元状態から差分適用する。
	void RestoreComponentStates(int32_t gameObjectId, bool affectsHierarchy);  // Play終了時に編集時のComponent Activeへ戻す。
	static bool IsAiComponent(EditorComponentType componentType);
	static bool IsAnimationComponent(EditorComponentType componentType);
	static bool IsEffectComponent(EditorComponentType componentType);
};

#pragma warning(pop)
