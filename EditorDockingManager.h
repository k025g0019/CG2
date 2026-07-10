#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorDockingManager {
public:
	//================================================================
	// Unity 風 Docking レイアウトを扱う Manager
	//================================================================

	void Initialize();  // Docking の実体は Draw 時の DockSpace 作成で行うため、初期化処理は持たない。
	void Update();  // Docking は ImGui の Draw 中にレイアウト確定するため、Update 処理は持たない。
	void Draw();  // メイン Viewport 全体に DockSpace を作り、初回だけ標準配置を組む。
};

#pragma warning(pop)
