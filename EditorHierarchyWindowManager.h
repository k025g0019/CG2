#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorHierarchyWindowManager {
public:
	//================================================================
	// GameObject ツリー表示を扱う Manager
	//================================================================

	void Initialize();  // HierarchyPanel 本体は SceneLifecycleManager が参照付きで初期化するため、ここでは空。
	void Update();  // 選択やドラッグ親子付けは Draw 中の ImGui 入力で処理するため、Update は空。
	void Draw();  // 左側の Hierarchy ウィンドウを作り、GameObject ツリーを描画する。
};

#pragma warning(pop)
