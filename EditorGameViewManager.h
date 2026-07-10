#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorGameViewManager {
public:
	//================================================================
	// Play 実行時の Camera 出力確認用 GameView
	//================================================================

	void Initialize();  // GameView は共有 Scene を参照するため、追加初期化は持たない。
	void Update();  // GameView の矩形と Camera 行列は Draw 中の ImGui 座標から更新する。
	void Draw();  // 独立 Dock ウィンドウとして GameView を表示し、DirectX 用矩形を更新する。
};

#pragma warning(pop)
