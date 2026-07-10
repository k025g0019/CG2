#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorBottomPanelWindowManager {
public:
	//================================================================
	// Project / Console の下部パネルを扱う Manager
	//================================================================

	void Initialize();  // BottomPanel 本体は SceneLifecycleManager で初期化するため、ここでは空。
	void Update();  // Project / Console の入力は Draw 中の ImGui 操作で処理するため、Update は空。
	void Draw();  // 下部 Dock ウィンドウに Project アセット一覧と Console ログを描画する。
};

#pragma warning(pop)
