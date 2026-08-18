#pragma once

#include <cstdint>
#include <functional>

#pragma warning(push)
#pragma warning(disable : 4820)

// 汎用ログ・監視システムの選択UI。Scene/GameObject/Component/FieldとSystem一覧から
// ログ対象を選び、取得Mode(毎Frame/間隔/変化時)を設定する。
// EditorDiagnosticsWindowManagerと同じく、表示/非表示はメニューのチェック項目
// (EditorSharedState::g_isLogMonitorWindowVisible)で切り替える自己完結Window。
class EditorLogMonitorWindowManager {
public:
	void Initialize();
	void Update();
	void Draw();

private:
	int32_t selectedGameObjectId_ = -1;  // Tree で選択中のGameObject
	char nameFilter_[128] = {};  // GameObject名の部分一致検索
	char presetNameBuffer_[128] = {};  // Preset保存/読込に使う名前の入力欄

	void DrawPresetBar();
	void DrawTargetTab();
	void DrawGameObjectTree();
	void DrawGameObjectNode(int32_t gameObjectId, int32_t depth);
	// Vector3系Field用。全体+X/Y/Zの4Checkboxをまとめて描画する共通部品。
	void DrawVectorFieldCheckboxes(
		const char* label,
		const char* idSuffix,
		const std::function<int32_t(int32_t)>& findEntryIndex,
		const std::function<void(int32_t)>& addEntry);
	void DrawSelectedGameObjectFields();
	void DrawSystemTab();
	void DrawWatchListTab();
};

#pragma warning(pop)
