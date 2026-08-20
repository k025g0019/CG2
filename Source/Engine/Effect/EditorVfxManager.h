#pragma once

#include "EffectDefinition.h"
#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Source/Engine/Renderer/EditorVfxRenderer.h"
#include "Vector.h"

#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class EditorScene;

#pragma warning(push)
#pragma warning(disable : 4820)

// Stage1 VFXシステムのCPU管理コア。
// EffectDefinition(Resource)をID指定で再生し、Billboard/Flipbook/Ribbon/Ringを
// CPUでSimulationしてEditorVfxRendererへ描画用頂点を渡す。
// GPU Compute Particle Update / Mesh Particle Instancing / Decal / Effekseer統合はStage2。
class EditorVfxManager {
public:
	// PlayEffectの戻り値。index/generationが一致する間だけ有効。
	struct EffectHandle {
		int32_t index = -1;
		uint32_t generation = 0u;
		bool IsValid() const { return index >= 0; }
	};

	struct DebugEffectEntry {
		std::string effectId;
		int32_t nodeCount = 0;
		int32_t particleCount = 0;
		std::string drawModeSummary;
		float lodSpawnMultiplier = 1.0f;
		bool isFollowing = false;
	};

	struct DebugStats {
		int32_t activeEffectCount = 0;
		int32_t activeEmitterCount = 0;
		int32_t activeParticleCount = 0;
		int32_t poolUsedCount = 0;
		int32_t poolCapacity = 0;
		std::vector<DebugEffectEntry> effects;
	};

	static constexpr int32_t kMaxEffectInstances = 256;

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);
	void Start();  // Play開始時にPoolを空へ戻す。
	void Update(float deltaTime);  // CPU側のParticle/Ribbon/Ring Simulationのみを進める。
	void Stop();  // Play停止時に全Instanceを解放する。

	// World固定座標で再生する。爆発・着弾・水しぶきなど。
	EffectHandle PlayEffect(const std::string& effectId, const Vector3& position);
	// World固定座標 + 法線で再生する。DecalNodeを含むEffect(弾痕・焦げ跡等)はこの法線でDecalの向きを決める。
	EffectHandle PlayEffect(const std::string& effectId, const Vector3& position, const Vector3& hitNormal);
	// GameObjectへ追従して再生する(Transform相当)。ミサイル曳光・銃口炎など。
	EffectHandle PlayEffect(const std::string& effectId, int32_t followGameObjectId, const Vector3& localOffset = {0.0f, 0.0f, 0.0f});
	void StopEffect(EffectHandle handle);  // 新規発生を止め、既存Particleは寿命まで残す(OneShotは自動回収)。
	void SetEffectPosition(EffectHandle handle, const Vector3& position);  // World固定Effectの位置を更新する。
	bool IsEffectPlaying(EffectHandle handle) const;

	// EditorRenderManagerが毎フレーム呼び、CPUで計算したVertexをRendererへ積んでBatchを作る。
	void BuildDrawBatches(
		EditorVfxRenderer& renderer,
		const Vector3& cameraPosition,
		const Vector3& cameraRight,
		const Vector3& cameraUp,
		const Vector3& cameraForward,
		std::vector<EditorVfxRenderer::VfxBatch>& outBatches);

	DebugStats GetDebugStats() const;

	// Stage2: GPU Compute Particle(useGpuSimulation)/MeshParticleの発生要求。
	// EditorRenderManagerがEditorEffectManager分と合成してEditorGpuParticleManager::Updateへ渡す。
	const std::vector<EditorEffectManager::GpuParticleSpawn>& GetPendingGpuParticleSpawns() const { return pendingGpuParticleSpawns_; }
	void ClearPendingGpuParticleSpawns() { pendingGpuParticleSpawns_.clear(); }
	// Play中に一度でもGPU Simulation Billboard/MeshParticleを発生させたか。
	// EditorGpuParticleManager::Drawを呼ぶかどうかの判定にEditorEffectManager分と合わせて使う。
	bool HasEverSpawnedGpuParticles() const { return hasEverSpawnedGpuParticles_; }

private:
	enum class NodeKind : int32_t { Billboard, GpuBillboard, MeshParticle, Decal, Ribbon, Ring, Unsupported };

	struct BillboardParticleRuntime {
		Vector3 position{0.0f, 0.0f, 0.0f};
		Vector3 velocity{0.0f, 0.0f, 0.0f};
		float age = 0.0f;
		float lifetime = 1.0f;
		float size = 0.2f;
		float rotation = 0.0f;
		float rotationSpeed = 0.0f;
	};

	struct RibbonPointRuntime {
		Vector3 position{0.0f, 0.0f, 0.0f};
		float age = 0.0f;
	};

	struct RingRuntime {
		bool isActive = false;
		float age = 0.0f;
	};

	struct NodeRuntime {
		const EffectNodeDefinition* definition = nullptr;
		NodeKind kind = NodeKind::Unsupported;
		float spawnAccumulator = 0.0f;
		bool burstDone = false;
		std::vector<BillboardParticleRuntime> particles;
		std::deque<RibbonPointRuntime> ribbonHistory;
		float ribbonDistanceAccumulator = 0.0f;
		float ribbonScrollOffset = 0.0f;
		RingRuntime ring;
	};

	struct EffectInstanceSlot {
		bool inUse = false;
		uint32_t generation = 0u;
		std::string effectId;
		const EffectDefinition* definition = nullptr;
		bool followGameObject = false;
		int32_t followGameObjectId = -1;
		Vector3 localOffset{0.0f, 0.0f, 0.0f};
		Vector3 fixedPosition{0.0f, 0.0f, 0.0f};
		Vector3 worldPosition{0.0f, 0.0f, 0.0f};
		Vector3 hitNormal{0.0f, 1.0f, 0.0f};
		float age = 0.0f;
		bool stopped = false;
		float lodSpawnMultiplier = 1.0f;
		std::vector<NodeRuntime> nodes;
	};

	// Stage2: DecalNodeが投影するDecalProjector GameObjectの生存管理(既存DecalProjector描画パスをそのまま再利用する)。
	struct DecalRuntime {
		int32_t gameObjectId = -1;
		float age = 0.0f;
		float lifetime = 4.0f;
		float alphaStart = 1.0f;
	};

	EditorScene* editorScene_ = nullptr;
	std::vector<std::string>* consoleMessages_ = nullptr;
	std::unordered_map<std::string, EffectDefinition> definitionCache_;
	std::vector<EffectInstanceSlot> instances_;   // Object Pool本体。Play中はサイズを変えない。
	std::vector<int32_t> freeIndices_;            // 再利用可能なInstance番号。
	std::unordered_map<std::string, int32_t> activeCountByEffectId_;  // maxConcurrentInstances判定用。
	std::mt19937 randomEngine_{0x564658u};
	bool isStarted_ = false;
	std::vector<EditorEffectManager::GpuParticleSpawn> pendingGpuParticleSpawns_;  // GPU Simulation Billboard / MeshParticleの今フレーム発生要求。
	bool hasEverSpawnedGpuParticles_ = false;  // Play中に一度でもGPU Simulation Particleを発生させたか。
	std::vector<DecalRuntime> decalRuntimes_;  // 再生中DecalProjectorの寿命管理。

	const EffectDefinition* ResolveDefinition(const std::string& effectId);
	int32_t AcquireInstanceSlot();
	void ReleaseInstanceSlot(int32_t index);
	void UpdateInstance(EffectInstanceSlot& instance, float deltaTime);
	void UpdateBillboardNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime);
	void UpdateGpuBillboardNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime);
	void UpdateMeshParticleNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime);
	void SpawnDecal(EffectInstanceSlot& instance, NodeRuntime& node);
	void UpdateDecals(float deltaTime);
	void UpdateRibbonNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime);
	void UpdateRingNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime);
	bool IsInstanceFinished(const EffectInstanceSlot& instance) const;
	float ComputeLodSpawnMultiplier(const EffectDefinition& definition, float distanceToCamera) const;
	float RandomRange(float minimumValue, float maximumValue);
	void PushConsoleMessage(const std::string& message);
};

#pragma warning(pop)
