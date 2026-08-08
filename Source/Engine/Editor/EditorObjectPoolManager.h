#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorScriptManager;
class EditorDamageManager;

class EditorObjectPoolManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorPhysicsManager* physicsManager,
		EditorDamageManager* damageManager,
		EditorScriptManager* scriptManager);  // Pool、物理Body、生成Actionを接続する
	void PreparePools();  // Physics開始前はTemplateだけを登録し、残りの容量は初回貸出時まで実体化しない
	void Start();  // 全Pool Itemを待機状態へ移し、SpawnerのRuntimeを初期化する
	void Update(float deltaTime);  // Play開始または一定間隔Spawnerを進める
	void Stop();  // RuntimeのPool対応表を破棄する
	int32_t Spawn(int32_t poolGameObjectId, const Vector3& position, const Vector3& rotation);  // 空きItemを指定姿勢で有効化する
	int32_t SpawnFromSpawner(int32_t spawnerGameObjectId);  // PrefabSpawner設定からPool Itemを生成する
	bool Release(int32_t gameObjectId);  // 使用中Itemを元のPoolへ戻す
	bool IsPooledObject(int32_t gameObjectId) const;  // Projectile等がRelease可能か調べる
	bool IsPooledObjectActive(int32_t gameObjectId) const;  // LOD等がScene Activeと貸出状態を混同しないため、現在の貸出状態を返す
	bool HasPool(int32_t poolGameObjectId) const;  // Play中に生成可能なPool設定か調べる
	uint64_t GetSpawnVersion(int32_t gameObjectId) const;  // 同じPool Itemが再利用されたか識別する貸出世代を返す
	bool IsSpawnLeaseActive(int32_t gameObjectId, uint64_t spawnVersion) const;  // 指定した世代の貸出が現在も有効か調べる
	void SetRuntimeResetCallback(std::function<void(int32_t)> callback);  // Pool Item再利用時の共通Runtime Reset契約を接続する
	void ResetObjectRuntimeState(int32_t gameObjectId);  // Script等から同じReset契約を明示実行する

private:
	struct PoolRuntime {
		int32_t templateGameObjectId = -1;  // 遅延複製時に使うTemplate
		int32_t initialCapacity = 1;  // 設定された初期容量。未使用分はGameObject化しない
		bool allowExpand = false;  // 初期容量を超えるRuntime拡張を許すか
		std::vector<int32_t> itemGameObjectIds;  // 実際に一度以上必要になったItemだけを保持する
		std::unordered_set<int32_t> activeGameObjectIds;  // 現在貸し出し中のItem
	};

	struct SpawnerRuntime {
		float timer = 0.0f;  // 次の自動生成までの残り秒数
		bool hasSpawnedOnStart = false;  // Play開始Modeの重複生成を防ぐ
	};

	EditorScene* editorScene_ = nullptr;  // Template複製とActive切替を行うScene
	EditorPhysicsManager* physicsManager_ = nullptr;  // 待機ItemのBody出し入れと姿勢同期を行う
	EditorDamageManager* damageManager_ = nullptr;  // 再利用ItemのHealthと死亡状態を初期化する
	EditorScriptManager* scriptManager_ = nullptr;  // PrefabSpawnerの生成Action通知先
	std::unordered_map<int32_t, PoolRuntime> poolRuntimes_;  // ObjectPool所有GameObject IDごとの実行状態
	std::unordered_map<int32_t, int32_t> poolOwnerByItemId_;  // Item IDから返却先Poolを引く索引
	std::unordered_map<int32_t, uint64_t> spawnVersions_;  // Item再利用を別の生成として区別する貸出世代
	std::unordered_map<int32_t, bool> originalActiveStates_;  // Pool待機前のTemplate階層Active状態
	std::unordered_map<int32_t, SpawnerRuntime> spawnerRuntimes_;  // PrefabSpawner所有者ごとのTimer
	bool isStarted_ = false;  // Play中だけ自動生成を進める
	std::function<void(int32_t)> runtimeResetCallback_;  // RuntimeManagerが各Systemの状態初期化をまとめる

	void SetItemActive(int32_t gameObjectId, bool isActive);  // Template階層のScene、姿勢、Jolt Activeを同時に変更する
	void ResetItemRuntimeState(int32_t gameObjectId);  // Template階層にあるHealthと死亡状態を再貸出用に戻す
	void QueueSpawnAction(
		const EditorGameObject& spawnerGameObject,
		const EditorComponent& spawnerComponent,
		int32_t spawnedGameObjectId) const;  // 生成IDを任意Script Actionへ通知する
};

#pragma warning(pop)
