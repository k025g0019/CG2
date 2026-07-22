#pragma once

#include "EditorAnimationManager.h"
#include "EditorAIManager.h"
#include "EditorAudioManager.h"
#include "EditorFreeTransformManager.h"
#include "EditorConstraintManager.h"
#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Source/Engine/Effect/EditorEffekseerManager.h"
#include "EditorInputManager.h"
#include "EditorLocalMoveManager.h"
#include "EditorNavigationManager.h"
#include "EditorPhysicsManager.h"
#include "EditorRollingMoveManager.h"
#include "EditorScene.h"
#include "EditorScriptManager.h"

#include <cstdint>
#include <string>
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
	bool PlayEffect(int32_t gameObjectId);  // .effect と .efk を拡張子に応じて再生する。
	void StopEffect(int32_t gameObjectId);  // 内蔵 GPU Particle と Effekseer の両方を停止する。
	int32_t GetAliveEffectCount(int32_t gameObjectId) const;  // 両実行系の生存数を合算する。

private:
	EditorScene* editorScene_ = nullptr;  // Play 実行対象の Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // Play 中の物理 / Script ログを出す Console
	EditorScene sceneBackup_;  // Stop 時に編集前状態へ戻すための Scene バックアップ
	EditorAnimationManager animationManager_;  // Animation Component の実行担当
	EditorAIManager aiManager_;  // AI Component の実行担当
	EditorAudioManager audioManager_;  // AudioSource Component の実行担当
	EditorFreeTransformManager freeTransformManager_;  // FreeTransform Component の実行担当
	EditorConstraintManager constraintManager_;  // Constraint 系 Component の実行担当
	EditorEffectManager effectManager_;  // ParticleSystem / VisualEffect と Animation Event の実行担当
	EditorEffekseerManager effekseerManager_;  // .efk / .efkefc の公式 DX12 Runtime 実行担当
	EditorScriptManager scriptManager_;  // Script / MonoBehaviour Component の実行入口
	EditorInputManager inputManager_;  // Input Component の実行担当
	EditorLocalMoveManager localMoveManager_;  // ローカル移動 Component の実行担当
	EditorRollingMoveManager rollingMoveManager_;  // 転がり移動 Component の実行担当
	EditorNavigationManager navigationManager_;  // NavigationAgent / NavMesh 系 Component の実行担当
	EditorPhysicsManager physicsManager_;  // RigidBody / Collider の実行担当
	bool isPlaying_ = false;  // Play 中なら true
	bool hasSceneBackup_ = false;  // sceneBackup_ が有効なら true
};

#pragma warning(pop)
