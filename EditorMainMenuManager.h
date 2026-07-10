#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorMainMenuManager {
public:
	//================================================================
	// 上部メニューを描画する Manager
	//================================================================

	void Initialize();  // MainMenuBar 本体は SceneLifecycleManager が参照付きで初期化するため、ここでは空。
	void Update();  // メニューの状態変更は Draw 中の ImGui 操作で起きるため、Update は空。
	void Draw();  // ファイル操作、Play / Stop、選択解除などのメニュー UI を描画する。
};

#pragma warning(pop)
