#pragma once

#include "EditorActionSequenceManager.h"
#include "EditorAnimationManager.h"
#include "EditorAIManager.h"
#include "EditorAudioManager.h"
#include "EditorCameraEffectManager.h"
#include "EditorFreeTransformManager.h"
#include "EditorConstraintManager.h"
#include "EditorDamageManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Source/Engine/Effect/EditorEffekseerManager.h"
#include "EditorInputManager.h"
#include "EditorLocalMoveManager.h"
#include "EditorLogMonitorManager.h"
#include "EditorNavigationManager.h"
#include "EditorObjectPoolManager.h"
#include "EditorPhysicsManager.h"
#include "EditorProfilerManager.h"
#include "EditorReplayManager.h"
#include "EditorRailBranchManager.h"
#include "EditorRailMovementManager.h"
#include "EditorGameplayEventManager.h"
#include "EditorRollingMoveManager.h"
#include "EditorRuntimePropertyManager.h"
#include "EditorSaveManager.h"
#include "EditorSceneOptimizationManager.h"
#include "EditorScene.h"
#include "EditorScriptManager.h"
#include "EditorTargetingManager.h"
#include "EditorUiBindingManager.h"
#include "EditorWaveSpawnerManager.h"
#include "EditorWeaponManager.h"
#include "EditorWeaponLoadoutManager.h"

#include <cstdint>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRuntimeManager {
public:
	EditorRuntimeManager() = default;  // 共有状態として 1 つだけ保持する
	~EditorRuntimeManager() = default;  // 所有 Manager の破棄に任せる
	EditorRuntimeManager(const EditorRuntimeManager&) = delete;  // Play 状態と Jolt World を二重所有しないためコピー禁止
	EditorRuntimeManager& operator=(const EditorRuntimeManager&) = delete;  // Play 状態と Jolt World を二重所有しないためコピー代入禁止
	EditorRuntimeManager(EditorRuntimeManager&&) = delete;  // 共有状態の所有先を動かさない
	EditorRuntimeManager& operator=(EditorRuntimeManager&&) = delete;  // 共有状態の所有先を動かさない

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // Play 実行時に操作する Scene と Console 出力先を受け取る
	void Update(const uint8_t* keyState, float deltaTime);  // Play 中だけ Input と Physics を進める
	void Draw();  // Play 中のデバッグ描画を呼ぶ
	void TogglePlay();  // Play / Stop を切り替える
	bool IsPlaying() const;  // 現在 Play 中かを返す
	EditorScriptManager& GetScriptManager();  // Inspector から Script デバッグ状態を見るために返す
	const EditorScriptManager& GetScriptManager() const;  // 読み取り専用版
	EditorAnimationManager& GetAnimationManager();  // Inspector から Animation 状態を見るために返す
	const EditorAnimationManager& GetAnimationManager() const;  // 読み取り専用版
	EditorEffectManager& GetEffectManager();  // Inspector / Script から Effect の実行状態を操作するために返す
	const EditorEffectManager& GetEffectManager() const;  // 読み取り専用版
	EditorEffekseerManager& GetEffekseerManager();  // Platform / Renderer から公式 Effekseer Runtime を操作する。
	const EditorEffekseerManager& GetEffekseerManager() const;  // 読み取り専用版。
	EditorAudioManager& GetAudioManager();  // Audio Mixer とイベント再生を操作するために返す。
	const EditorAudioManager& GetAudioManager() const;  // 読み取り専用版。
	EditorRailMovementManager& GetRailMovementManager();  // Spline Editor と Script から実行状態を操作するために返す。
	const EditorRailMovementManager& GetRailMovementManager() const;  // 読み取り専用のレール実行状態を返す。
	EditorPhysicsManager& GetPhysicsManager();  // SceneView が接触点と Cast 履歴を可視化するために返す。
	const EditorPhysicsManager& GetPhysicsManager() const;  // 読み取り専用の物理デバッグ情報を返す。
	EditorProfilerManager& GetProfilerManager();  // Diagnostics WindowへRuntime各系統のCPU時間を公開する。
	const EditorProfilerManager& GetProfilerManager() const;  // 読み取り専用Profiler。
	EditorLogMonitorManager& GetLogMonitorManager();  // Log Monitor PanelがWatch Entryを編集するために返す。
	const EditorLogMonitorManager& GetLogMonitorManager() const;  // 読み取り専用版。
	EditorReplayManager& GetReplayManager();  // Diagnostics Windowから入力記録・再生を操作する。
	const EditorReplayManager& GetReplayManager() const;  // 読み取り専用Replay状態。
	bool PlayEffect(int32_t gameObjectId);  // .effect と .efk を拡張子に応じて再生する。
	void StopEffect(int32_t gameObjectId);  // 内蔵 GPU Particle と Effekseer の両方を停止する。
	int32_t GetAliveEffectCount(int32_t gameObjectId) const;  // 両実行系の生存数を合算する。
	bool RequestSceneLoad(const std::string& scenePath);  // Scene Button から Script 不要で安全な遷移を要求する。
	bool RequestSceneLoadAsync(const std::string& scenePath, bool isAdditive);  // Worker ThreadでSceneを読み、フレーム境界で反映する。
	bool RequestSceneUnload(const std::string& scenePath);  // Additive読込したSceneのObject群を破棄する。
	float GetSceneLoadProgress() const;  // 非同期読込の0～1進捗を返す。
	bool IsSceneLoading() const;  // 非同期読込が完了待ちならtrue。
	bool IsSceneLoaded(const std::string& scenePath) const;  // PrimaryまたはAdditive Sceneが有効か返す。

private:
	EditorScene* editorScene_ = nullptr;  // Play 実行対象の Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // Play 中の物理 / Script ログを出す Console
	EditorScene sceneBackup_;  // Stop 時に編集前状態へ戻すための Scene バックアップ
	std::string sceneBackupPath_;  // Stop 時に編集前の Scene パスも戻す。
	EditorAnimationManager animationManager_;  // Animation Component の実行担当
	EditorAIManager aiManager_;  // AI Component の実行担当
	EditorAudioManager audioManager_;  // AudioSource Component の実行担当
	EditorFreeTransformManager freeTransformManager_;  // FreeTransform Component の実行担当
	EditorConstraintManager constraintManager_;  // Constraint 系 Component の実行担当
	EditorEffectManager effectManager_;  // ParticleSystem / VisualEffect と Animation Event の実行担当
	EditorEffekseerManager effekseerManager_;  // .efk / .efkefc の公式 DX12 Runtime 実行担当
	EditorScriptManager scriptManager_;  // Script / MonoBehaviour Component の実行入口
	EditorInputManager inputManager_;  // Input Component の実行担当
	EditorTargetingManager targetingManager_;  // 画面照準とGame View Ray変換の実行担当
	EditorDamageManager damageManager_;  // HealthとDamageReceiverの実行担当
	EditorObjectPoolManager objectPoolManager_;  // 事前生成Objectの貸出・返却とSpawnerの実行担当
	EditorWeaponManager weaponManager_;  // 汎用Ray射撃と弾発射の実行担当
	EditorWeaponLoadoutManager weaponLoadoutManager_;  // 可変Weapon Slotと弾薬・Reloadの実行担当
	EditorRuntimePropertyManager runtimePropertyManager_;  // 型付きProperty、Tween、ActionRelayの実行担当
	EditorCameraEffectManager cameraEffectManager_;  // Camera BlendとShakeの実行担当
	EditorLocalMoveManager localMoveManager_;  // ローカル移動 Component の実行担当
	EditorRailMovementManager railMovementManager_;  // 子ウェイポイントを通るレール移動の実行担当
	EditorRailBranchManager railBranchManager_;  // RailFollowerの接続先切替を実行する汎用分岐担当
	EditorActionSequenceManager actionSequenceManager_;  // 子Stepの順次・並列・待機・分岐を実行する担当
	EditorWaveSpawnerManager waveSpawnerManager_;  // Pool生成、編隊配置、全撃破判定を行う汎用Wave担当
	EditorGameplayEventManager gameplayEventManager_;  // TimelineとThresholdから任意Script Actionを通知する担当
	EditorUiBindingManager uiBindingManager_;  // 汎用値をCanvasのTextとSliderへ反映する担当
	EditorRollingMoveManager rollingMoveManager_;  // 転がり移動 Component の実行担当
	EditorNavigationManager navigationManager_;  // NavigationAgent / NavMesh 系 Component の実行担当
	EditorPhysicsManager physicsManager_;  // RigidBody / Collider の実行担当
	EditorProfilerManager profilerManager_;  // Runtime各系統のCPU時間、平均、Peakを保持する担当
	EditorLogMonitorManager logMonitorManager_;  // GameObject/Component/System横断の汎用ログ・監視担当
	EditorReplayManager replayManager_;  // Keyboard入力とdeltaTimeを記録し、同じScene開始状態から再生する担当
	EditorSaveManager saveManager_;  // Saveable登録、Slot、Checkpointの保存・復元担当
	EditorSceneOptimizationManager sceneOptimizationManager_;  // 距離実体化とSimulation LODの統合担当
	std::unordered_map<int32_t, float> hapticLoopTimers_;  // ループ振動を GameObject ごとに管理する。
	bool isPlaying_ = false;  // Play 中なら true
	bool hasSceneBackup_ = false;  // sceneBackup_ が有効なら true

	struct AsyncSceneLoadResult {
		bool wasLoaded = false;  // ファイル解析に成功したか
		EditorScene loadedScene;  // Worker Threadで構築したScene
	};

	struct AdditiveSceneRecord {
		std::string scenePath;  // 読み込んだScene Asset
		std::vector<int32_t> gameObjectIds;  // Merge時に割り当てたObject ID
	};

	std::future<AsyncSceneLoadResult> pendingSceneLoadFuture_;  // Worker ThreadのScene解析結果
	std::vector<AdditiveSceneRecord> additiveScenes_;  // 現在追加読込されているScene群
	std::string pendingScenePath_;  // 読込中Scene
	bool pendingSceneIsAdditive_ = false;  // 追加読込ならtrue
	bool isSceneLoading_ = false;  // Future完了待ちならtrue
	float sceneLoadProgress_ = 0.0f;  // 0=未開始、0.1=解析中、1=適用完了

	void StartRuntimeSystems(bool shouldReinitializeScript);  // 現在 Scene の各 Runtime を開始する。
	void StopRuntimeSystems();  // Scene 切替前または Play 停止時に各 Runtime を止める。
	void StartHapticSources();  // 自動再生が有効な HapticSource を開始する。
	void UpdateHapticSources(float deltaTime);  // ループ指定の HapticSource を再発生させる。
	void StopHapticSources();  // Play 停止時に振動を確実に止める。
	bool PlayHapticSource(const EditorGameObject& gameObject, const EditorComponent& component);  // 1 つの触覚設定を再生する。
	bool LoadSceneForPlay(const std::string& scenePath);  // Play 状態を維持したまま Scene を安全に差し替える。
	bool UpdateSceneLoading();  // Future完了時にSceneを反映し、反映したフレームはtrueを返す。
	bool ApplyLoadedScene(const std::string& scenePath, bool isAdditive, EditorScene&& loadedScene);  // 読込済みSceneを安全なフレーム境界で適用する。
	void CancelPendingSceneLoad();  // Play停止時にWorkerを待って結果を破棄する。
	void PublishSceneRuntimeState();  // C++ Script APIへ進捗と読込済みScene一覧を公開する。
	bool UpdateAutomaticSceneStreaming();  // 距離条件からAdditive Sceneを1Frame最大1件ロードまたは破棄する。
	bool ResolveStreamingReferencePosition(int32_t gameObjectId, Vector3& position) const;  // 指定Objectまたは最高Priority CameraのWorld位置を返す。
};

#pragma warning(pop)
