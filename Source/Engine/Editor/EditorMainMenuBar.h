#pragma once

#pragma warning(disable : 5045)

#include "EditorRuntimeManager.h"
#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorMainMenuBar {
public:
	void Initialize(EditorScene* editorScene, EditorRuntimeManager* runtimeManager);  // Play / Stop 操作に使う Scene と RuntimeManager を受け取る
	void Update();  // 現時点では自動更新なし
	// メインメニューと Play ボタンを描画する
	void Draw(
		std::vector<std::string>& consoleMessages,
		bool& isRuntimeInitialized,
		int32_t& selectedPlacedSceneObjectIndex,
		int32_t& previousSelectedGameObjectId);

private:
	EditorScene* editorScene_ = nullptr;  // Play 対象の Scene
	EditorRuntimeManager* runtimeManager_ = nullptr;  // Play / Stop を切り替える RuntimeManager
	bool isAutoSaveEnabled_ = true;  // Project単位で自動保存を有効にする
	float autoSaveIntervalSeconds_ = 120.0f;  // 自動保存を試行する編集時間間隔
	float autoSaveElapsedSeconds_ = 0.0f;  // Play時間を除いた前回試行からの経過秒
	std::string observedScenePath_;  // Scene切替時にTimerを戻すための監視Path
	std::string lastAutoSaveStatus_;  // File Menuへ最後の結果を表示する

	void LoadAutoSaveSettings();  // ProjectSettingsから有効状態と間隔を読む
	void SaveAutoSaveSettings() const;  // UIで変更した設定をUTF-8 BOM付きで保存する
	void UpdateAutoSave(std::vector<std::string>& consoleMessages);  // 非Play時に変更Sceneだけを安全に保存する
};

#pragma warning(pop)
