#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSceneViewManager {
public:
	//================================================================
	// Scene タブ上の編集操作を扱う Manager
	//================================================================

	void Initialize();  // SceneView は ImGui の現在座標を使うため、追加初期化は持たない。
	void Update();  // SceneView の入力判定は Draw 中の ImGui Item 状態が必要なので、Update は空。
	void Draw();  // Scene タブ、ガイド線、ギズモ、アセットドロップ、範囲選択を描画・処理する。
};

#pragma warning(pop)
