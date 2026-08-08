#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorObjectPoolManager;
class EditorRailMovementManager;
class EditorScriptManager;
struct EditorScriptActionPayload;

class EditorWaveSpawnerManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorPhysicsManager* physicsManager,
		EditorObjectPoolManager* objectPoolManager,
		EditorScriptManager* scriptManager);  // 汎用WaveのScene、開始条件、任意Script通知を接続する。
	void Start();  // Pool生成または互換子方式のWave実行状態を構築する。
	void Update(float deltaTime);  // 条件成立後、指定数のObjectを編隊配置で生成する。
	void Stop();  // Play中の進行状態を破棄する。
	bool StartWave(int32_t waveSpawnerGameObjectId);
	bool IsWaveComplete(int32_t waveSpawnerGameObjectId, bool waitsForAllDefeated) const;
	bool StartEncounter(int32_t encounterGameObjectId);
	bool ResolveSpawnPoint(int32_t spawnPointSetGameObjectId, Vector3& position, Vector3& rotation);

private:
	struct SpawnRecord {
		int32_t gameObjectId = -1;
		uint64_t poolSpawnVersion = 0u;
		bool usesObjectPool = false;
		int32_t spawnIndex = 0;  // 編隊位相と開始進行率を再現する番号。
		Vector3 formationOffset{0.0f, 0.0f, 0.0f};  // Wave Motionを加える前の基準Offset。
		float elapsedTime = 0.0f;  // 生成後の運動時間。
	};

	struct WaveRuntime {
		std::vector<int32_t> childGameObjectIds;
		std::vector<SpawnRecord> spawnRecords;
		int32_t nextSpawnIndex = 0;
		int32_t targetSpawnCount = 0;
		float spawnTimer = 0.0f;
		bool usesObjectPool = false;
		bool hasTriggered = false;
		bool hasSpawnCompleted = false;
		bool hasAllDefeatedCompleted = false;
	};
	struct EncounterRuntime { int32_t currentIndex = 0; float delayRemaining = 0.0f; bool isPlaying = false; bool hasStartedCurrent = false; };

	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorPhysicsManager* physicsManager_ = nullptr;
	EditorObjectPoolManager* objectPoolManager_ = nullptr;
	EditorScriptManager* scriptManager_ = nullptr;
	std::unordered_map<int32_t, WaveRuntime> waveRuntimes_;
	std::unordered_map<int32_t, EncounterRuntime> encounterRuntimes_;
	std::unordered_map<int32_t, int32_t> spawnPointIndices_;
	std::unordered_map<int32_t, int32_t> spawnPointLastIndices_;
	std::unordered_map<int32_t, float> originalRailSpeeds_;  // Pool再利用で速度倍率が累積しない基準値。
	uint32_t spawnRandomState_ = 0x53504157u;
	std::unordered_map<int32_t, bool> originalActiveStates_;  // 待機前の子階層Active状態をSpawn時に復元する
	bool isStarted_ = false;

	bool IsTriggerSatisfied(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component) const;  // Play開始、Rail進行率、距離、外部開始を判定する。
	void SetRuntimeActiveRecursive(int32_t gameObjectId, bool isActive);  // 子階層の描画とPhysicsを同じ状態へ切り替える。
	int32_t SpawnNext(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		WaveRuntime& waveRuntime);  // Poolまたは互換子から次の1体を生成する。
	Vector3 CalculateFormationOffset(
		const EditorComponent& component,
		int32_t spawnIndex,
		int32_t spawnCount) const;  // 編隊番号から基準点に対するローカル位置を計算する。
	void ResetRailMovement(
		int32_t gameObjectId,
		const Vector3& formationOffset,
		float railStartNormalizedOverride) const;  // Pool再利用時のレール進行と編隊Offsetを初期化する。OverrideはWave側の設定。-1なら生成物自身の値
	void ApplySpawnedObjectSetup(
		int32_t ownerGameObjectId,
		int32_t spawnedGameObjectId,
		int32_t spawnIndex);  // Wave所有設定を生成個体の汎用Componentへ適用する。Spawn()後に呼ばれるためownerはIDで引く。
	void UpdateWaveMotion(
		const EditorGameObject& ownerGameObject,
		WaveRuntime& waveRuntime,
		float deltaTime);  // 生成個体へ基準編隊を壊さない周期Offsetを加える。
	bool IsSpawnRecordDefeated(const SpawnRecord& spawnRecord) const;  // Health死亡、非Active、Pool返却を同じ完了条件として扱う。
	void QueueAction(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		const std::string& actionName,
		const EditorScriptActionPayload& payload) const;  // 型付きPayloadで登録されたScript Actionを通知する。
	void CompleteWaveIfNeeded(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		WaveRuntime& waveRuntime);  // 全生成と全撃破を別々に一度だけ通知する。
	void UpdateEncounters(float deltaTime);
};

#pragma warning(pop)
