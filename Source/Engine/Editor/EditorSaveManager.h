#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager;
class EditorScriptManager;

class EditorSaveManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorPhysicsManager* physicsManager,
		EditorScriptManager* scriptManager,
		std::vector<std::string>* consoleMessages);  // 保存対象Sceneと通知先を接続する
	void Start();  // CheckpointのPlay開始設定を処理する
	void Stop();  // Scene切替を含むRuntime停止を処理する
	void ResetSessionValues();  // Play開始・終了時だけカスタム保存値を初期化する
	void ResetSceneState();  // 主Scene切替時にCheckpoint初期化履歴だけを破棄する

	bool SaveSlot(const std::string& slotName);  // 全Saveableをバージョン付きSlotへ保存する
	bool LoadSlot(const std::string& slotName);  // Slotから登録済みSaveableだけ復元する
	bool DeleteSlot(const std::string& slotName);  // 指定Slotファイルを削除する
	bool HasSlot(const std::string& slotName) const;  // Slotファイルが存在するか返す
	bool ActivateCheckpoint(int32_t checkpointGameObjectId, bool shouldLoad);  // Checkpoint設定のSlotを保存または復元する

	void SetFloat(const std::string& key, float value);  // Script固有のfloatを次回Slot保存へ登録する
	bool GetFloat(const std::string& key, float& value) const;  // 読込済みfloatを取得する
	void SetString(const std::string& key, const std::string& value);  // Script固有文字列を次回Slot保存へ登録する
	bool GetString(const std::string& key, std::string& value) const;  // 読込済み文字列を取得する

private:
	EditorScene* editorScene_ = nullptr;  // 保存・復元対象Scene
	EditorPhysicsManager* physicsManager_ = nullptr;  // 復元Transformと速度をJoltへ同期する
	EditorScriptManager* scriptManager_ = nullptr;  // Checkpoint完了Actionの通知先
	std::vector<std::string>* consoleMessages_ = nullptr;  // 保存結果のConsole出力先
	std::unordered_map<std::string, float> floatValues_;  // Script登録float
	std::unordered_map<std::string, std::string> stringValues_;  // Script登録文字列
	std::unordered_set<int32_t> initializedCheckpointIds_;  // Additive再構築で開始処理を重複させない

	std::string MakeSlotPath(const std::string& slotName) const;  // 安全なファイル名へ変換してSaveData配下を返す
	std::string ResolveSaveableKey(const EditorGameObject& gameObject, const EditorComponent& saveable) const;  // 空KeyをGameObject名で補完する
	void QueueCheckpointAction(
		const EditorGameObject& checkpointGameObject,
		const EditorComponent& checkpointComponent,
		bool wasLoaded) const;  // 保存または復元完了Actionを通知する
	void PushConsoleMessage(const std::string& message) const;  // Consoleが有効な時だけ追加する
};

#pragma warning(pop)
