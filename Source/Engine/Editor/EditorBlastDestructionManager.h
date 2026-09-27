#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

struct NvBlastActor;
struct NvBlastAsset;
struct NvBlastFamily;

class EditorPhysicsManager;
class EditorScriptManager;
class EditorDamageManager;
class EditorEffectManager;

// NVIDIA Blast 1.1.5の構造破壊を、CG2Engineの自動Fracture CacheとJoltへ接続する。
// 通常はSource Meshだけを入力にし、直下の子Chunk方式はAdvanced互換としてのみ残す。
class EditorBlastDestructionManager final {
public:
	EditorBlastDestructionManager() = default;
	~EditorBlastDestructionManager();
	EditorBlastDestructionManager(const EditorBlastDestructionManager&) = delete;
	EditorBlastDestructionManager& operator=(const EditorBlastDestructionManager&) = delete;

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, EditorScriptManager* scriptManager, std::vector<std::string>* consoleMessages, EditorDamageManager* damageManager = nullptr, EditorEffectManager* effectManager = nullptr);
	void Start();
	void Update(float deltaTime);
	void Stop();

	bool ApplyDamage(int32_t gameObjectId, const Vector3& worldPosition, float radius, float damage, float impulse);
	bool FractureAll(int32_t gameObjectId, float impulse);
	bool IsFractured(int32_t gameObjectId) const;
	int32_t GetChunkCount(int32_t gameObjectId) const;
	int32_t GetActorCount(int32_t gameObjectId) const;
	int32_t GetBondCount(int32_t gameObjectId) const;
	bool GetChunkGameObjectId(int32_t gameObjectId, int32_t chunkIndex, int32_t& outChunkGameObjectId) const;
	bool IsChunkDetached(int32_t gameObjectId, int32_t chunkIndex) const;

private:
	// 破片1個の描画情報。GPU破片の見た目と、物理化するChunkの選び方に使う。
	struct ChunkVisual {
		std::string meshAssetPath;  // GPU破片としてInstancingするChunk Mesh
		float renderScale = 1.0f;  // 最大辺1へ正規化されたMeshを実寸へ戻す倍率
		float volume = 0.0f;  // 大きいChunkを優先して物理化するための体積目安
		int32_t volumeRank = 0;  // volume降順の順位。物理数制限の判定に使う
		Vector3 color{1.0f, 1.0f, 1.0f};  // GPU破片の色。元Rendererの色をそのまま使う
		float alpha = 1.0f;  // GPU破片の不透明度
		float emissionStrength = 0.0f;  // GPU破片の発光。元Rendererの発光を引き継ぐ
	};
	// 短時間だけ物理化するChunk。残り秒数が尽きたらDynamicを解除する。
	struct TimedPhysicsChunk {
		int32_t chunkGameObjectId = -1;
		float remainingSeconds = 0.0f;
	};
	// 一定時間後に沈めてGameObjectごと破棄する破片。Draw CallとSRVを取り戻す。
	struct SinkingChunkRuntime {
		int32_t chunkGameObjectId = -1;
		Vector3 basePosition{};  // 沈み始めた瞬間のローカル位置
		float elapsedSeconds = 0.0f;
		float delaySeconds = 0.0f;
		float durationSeconds = 1.5f;
		float distance = 1.0f;
		bool hasFrozen = false;  // 沈める間は物理を止めてこちらが姿勢を持つ
	};
	// Cluster破壊で物理を担当するChunk。追従Chunkはこの子として運ばれる。
	struct ClusterCarrierRuntime {
		int32_t chunkGameObjectId = -1;
		uint32_t chunkIndex = 0U;
		int32_t followerCount = 0;  // clusterSizeとの比較に使う現在の追従数
	};
	// Cluster破壊でCarrier Chunkへ追従する破片。遅れて視覚的にだけばらける。
	struct ClusterFollowerRuntime {
		int32_t chunkGameObjectId = -1;
		Vector3 basePosition{};  // 親付け直後のローカル位置
		Vector3 scatterOffset{};  // 最終的に足すローカルOffset
		float elapsedSeconds = 0.0f;
		float delaySeconds = 0.0f;
	};
	// Componentから切り出した破片予算設定。Scene配列が再確保されても参照が切れない値だけを持つ。
	struct DebrisSettings {
		bool optimizeEnabled = false;
		int32_t maxPhysicsChunks = 5;
		bool useCluster = true;
		int32_t clusterSize = 5;
		float clusterScatterDelay = 2.0f;
		float clusterScatterDistance = 0.15f;
		float physicsLifetime = 0.0f;
		float debrisSinkDelay = 0.0f;  // 破片軽量化のOnOffに関係なく効く後片付け設定
		float debrisSinkDuration = 1.5f;
		float debrisSinkDistance = 1.0f;
		bool useGpuDebris = true;
		float gpuDebrisLifetime = 4.0f;
		float gpuDebrisGravity = 9.8f;
		float gpuDebrisDrag = 0.2f;
		float gpuDebrisWind = 0.5f;
		int32_t gpuDebrisMotionType = 0;  // Component値。0=直線、1=爆発、2=渦
		float gpuDebrisRadialAcceleration = 0.0f;
		float gpuDebrisUpdraft = 0.0f;
		float gpuDebrisAngularSpeed = 90.0f;
		float gpuDebrisSpin = 180.0f;
		int32_t gpuDebrisMeshLimit = 4;
		bool useDistanceLod = false;
		float lodNearDistance = 25.0f;
		float lodFarDistance = 80.0f;
		int32_t lodFarDebrisCount = 8;
		float chunkMass = 1.0f;
		float defaultImpulse = 12.0f;  // GPU破片はあとからScriptで押せないため、Impulse 0のときこの値で飛ばす
	};
	struct BondRuntime {
		uint32_t chunkIndex0 = 0U;
		uint32_t chunkIndex1 = 0U;
		uint32_t nodeIndex0 = 0U;
		uint32_t nodeIndex1 = 0U;
		Vector3 centroid{};
	};

	struct DestructibleRuntime {
		int32_t ownerGameObjectId = -1;
		std::vector<int32_t> chunkGameObjectIds;
		std::vector<Vector3> chunkWorldPositions;
		std::vector<BondRuntime> bonds;
		void* assetMemory = nullptr;
		void* familyMemory = nullptr;
		NvBlastAsset* asset = nullptr;
		NvBlastFamily* family = nullptr;
		std::vector<NvBlastActor*> actors;
		std::unordered_set<int32_t> dynamicChunkGameObjectIds;
		std::vector<int32_t> generatedChunkGameObjectIds;
		float chunkMass = 1.0f;
		std::vector<ChunkVisual> chunkVisuals;
		std::vector<TimedPhysicsChunk> timedPhysicsChunks;
		std::vector<ClusterFollowerRuntime> clusterFollowers;
		std::vector<ClusterCarrierRuntime> clusterCarriers;
		std::vector<SinkingChunkRuntime> sinkingChunks;
		int32_t resolvedMaxPhysicsChunks = 256;  // 距離LODを含めて確定した物理破片数の上限
		int32_t remainingGpuDebrisBudget = -1;  // 遠距離LODで飛ばせる残りGPU破片数。-1は無制限
		bool resolvedOptimizeEnabled = false;  // 最適化OnOffの確定値。falseなら全て物理破片にする
		bool resolvedUseCluster = false;  // 上限超過分を物理Chunkの子として運ぶ
		bool resolvedUseGpuDebris = false;  // 物理にもClusterにも回らない分をGPU破片にする
		bool hasResolvedDebrisPlan = false;  // 最初の分裂時のCamera距離だけで組み合わせを決める
		bool hasSplit = false;
		uint64_t consumedDamageSequence = 0U;
		bool receivedLocalizedDamage = false;
	};
	struct CachedChunk {
		std::string assetPath;
		Vector3 localCenter{};
	};

	bool BuildDestructible(EditorGameObject& owner, EditorComponent& component);
	bool PrepareAutomaticChunks(EditorGameObject& owner, EditorComponent& component, std::vector<int32_t>& chunkIds, std::vector<std::pair<uint32_t, uint32_t>>& bondPairs);
	bool LoadOrBakeCache(const EditorGameObject& owner, EditorComponent& component, std::vector<CachedChunk>& chunks, std::vector<std::pair<uint32_t, uint32_t>>& bondPairs);
	bool BakeCache(const std::string& sourceAssetPath, const EditorComponent& component, const std::string& cacheKey, std::vector<CachedChunk>& chunks, std::vector<std::pair<uint32_t, uint32_t>>& bondPairs, std::string& error);
	bool LoadCacheManifest(const std::string& cacheKey, std::vector<CachedChunk>& chunks, std::vector<std::pair<uint32_t, uint32_t>>& bondPairs, std::string& error) const;
	bool CreateGeneratedChunkObjects(const EditorGameObject& owner, const EditorComponent& component, const std::vector<CachedChunk>& chunks, std::vector<int32_t>& chunkIds);
	std::string ResolveSourceMeshPath(const EditorGameObject& owner) const;
	std::string ComputeCacheKey(const std::string& sourceAssetPath, const EditorComponent& component) const;
	void SetBakeFailure(EditorComponent& component, const std::string& status, const std::string& reason) const;
	bool ApplyDamage(DestructibleRuntime& runtime, const Vector3& worldPosition, float radius, float damage, float impulse);
	void ApplySplitToScene(DestructibleRuntime& runtime, const Vector3& worldPosition, float impulse);
	static DebrisSettings MakeDebrisSettings(const EditorComponent& component);
	void BuildChunkVisuals(DestructibleRuntime& runtime);
	void ResolveDebrisPlan(const DebrisSettings& settings, const Vector3& ownerWorldPosition, DestructibleRuntime& runtime) const;
	int32_t FindClusterCarrier(DestructibleRuntime& runtime, uint32_t chunkIndex, const DebrisSettings& settings, bool& outHasRoom) const;
	void DetachChunkAsPhysics(DestructibleRuntime& runtime, const DebrisSettings& settings, uint32_t chunkIndex, const Vector3& blastCenter, float impulse);
	bool SpawnGpuDebris(DestructibleRuntime& runtime, const DebrisSettings& settings, uint32_t chunkIndex, const Vector3& blastCenter, float impulse);
	void AttachClusterFollower(DestructibleRuntime& runtime, const DebrisSettings& settings, uint32_t chunkIndex, int32_t carrierGameObjectId);
	void UpdateTimedPhysicsChunks(DestructibleRuntime& runtime, float deltaTime);
	void UpdateSinkingChunks(DestructibleRuntime& runtime, float deltaTime);
	void UpdateClusterFollowers(DestructibleRuntime& runtime, float deltaTime);
	void FreezeChunkPhysics(int32_t chunkGameObjectId);
	bool HasScenePhysicsDebrisBudget() const;  // ProjectSettingsのScene全体上限に空きがあるか
	void HideChunkObject(int32_t chunkGameObjectId);
	float NextRandomUnit();
	void PrepareChunkPhysics(int32_t chunkGameObjectId, float mass);
	void DisableIntactRoot(int32_t ownerGameObjectId);
	void PushConsoleMessage(const std::string& message) const;
	static void ReleaseAlignedMemory(void*& memory);

	EditorScene* editorScene_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	EditorDamageManager* damageManager_ = nullptr;
	EditorEffectManager* effectManager_ = nullptr;  // GPU破片をGPU Particleとして飛ばす発生先
	std::vector<std::string>* consoleMessages_ = nullptr;
	std::unordered_map<int32_t, DestructibleRuntime> destructibles_;
	std::unordered_set<int32_t> physicsDebrisGameObjectIds_;  // Scene全体で今Rigidbody化している破片。二重加算を避けるため集合で持つ
	std::mt19937 debrisRandomEngine_{0x424C5354u};  // GPU破片の散り方をPlayごとに再現可能にする
};

#pragma warning(pop)
