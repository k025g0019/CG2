#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorScriptManager;
class EditorObjectPoolManager;
class EditorPhysicsManager;

class EditorDamageManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorScriptManager* scriptManager,
		EditorPhysicsManager* physicsManager,
		EditorObjectPoolManager* objectPoolManager);  // Health、物理Body、Pool返却、Script通知を接続する
	void Start();  // Healthを最大値へ戻し、無敵時間と死亡状態を初期化する
	void Update(float deltaTime);  // 無敵時間を進め、死亡通知後の無効化を確定する
	void Stop();  // Runtime状態を破棄する
	bool ApplyDamage(int32_t targetGameObjectId, float damage, int32_t sourceGameObjectId);  // DamageReceiverを通してHealthを減らす
	bool ApplyDamage(EditorScriptDamageContext& damageContext);  // 命中位置・攻撃者・Impulseを含むDamageを適用する
	int32_t ApplyAreaDamage(int32_t areaDamageGameObjectId, int32_t instigatorGameObjectId = -1);  // Component所有位置を中心に範囲Damageを発生させる
	int32_t ApplyAreaDamage(int32_t areaDamageGameObjectId, const Vector3& center, int32_t instigatorGameObjectId);  // Projectile命中点など任意World位置で発生させる
	static int32_t HashDamageTag(std::string_view damageTag);  // 文字列Tagを固定Enumに依存しない安定IDへ変換する
	bool GetLastDamageContext(int32_t targetGameObjectId, EditorScriptDamageContext& damageContext) const;  // 対象が最後に受けたDamage情報を返す
	uint64_t GetDamageSequence(int32_t targetGameObjectId) const;  // 同値Damageの連続発生も区別する単調増加番号
	bool GetDamageEventCount(int32_t gameObjectId, int32_t& eventCount) const;  // 有効期間内の複数被弾件数を返す
	bool GetDamageEvent(int32_t gameObjectId, int32_t eventIndex, EditorDamageEventRuntimeEntry& damageEvent) const;  // 指定被弾履歴を返す
	bool GetHealth(int32_t gameObjectId, float& currentHealth, float& maximumHealth) const;  // ScriptやUIからHealthを読む
	const std::string& GetLastApplyResult() const { return lastApplyResult_; }  // 直前のDamage適用結果。射撃判定Logから失敗理由を参照する
	bool SetHealth(int32_t gameObjectId, float currentHealth);  // 回復やCheckpointからHealthを設定する
	void ResetRuntimeState(int32_t gameObjectId);  // Pool再利用時にHealth・無敵・死亡状態を初期化する

private:
	EditorScene* editorScene_ = nullptr;  // HealthとDamageReceiverを検索するScene
	EditorScriptManager* scriptManager_ = nullptr;  // 被弾・死亡Actionの通知先
	EditorPhysicsManager* physicsManager_ = nullptr;  // 死亡無効化時にCollider Bodyも止める
	EditorObjectPoolManager* objectPoolManager_ = nullptr;  // Pool Itemなら無効化ではなく返却する
	std::unordered_map<int32_t, float> invulnerabilityTimers_;  // 対象GameObjectごとの残り無敵秒数
	std::unordered_set<int32_t> deadGameObjectIds_;  // 死亡Actionの重複通知を防ぐ集合
	std::unordered_set<int32_t> pendingDeactivationIds_;  // 死亡ActionをScriptへ渡した次フレームに無効化する集合
	std::unordered_map<int32_t, EditorScriptDamageContext> lastDamageContexts_;  // 被弾Actionから参照できる最後のDamage情報
	std::unordered_map<int32_t, uint64_t> damageSequences_;
	std::string lastApplyResult_ = "NotApplied";  // ApplyDamageが最後に成功または失敗した理由

	void QueueAction(
		const EditorGameObject& ownerGameObject,
		const EditorComponent* damageReceiver,
		const std::string& actionName,
		float value) const;  // 任意Actionを所有者または指定先Scriptへ通知する
	int32_t ResolveDamageTarget(int32_t hitGameObjectId, float& hitZoneMultiplier) const;  // HitZoneと親階層からHealth所有Objectと部位倍率を解決する
	float ResolveDamageTagMultiplier(const EditorGameObject& targetGameObject, int32_t damageTagId) const;  // Tag耐性・弱点倍率を返す
	void RecordDamageEvent(const EditorScriptDamageContext& damageContext);  // Damage確定値を複数方向HUD用履歴へ保存する
};

#pragma warning(pop)
