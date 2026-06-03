#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorImguiFrameManager {
public:
	//================================================================
	// ImGui / ImGuizmo のフレーム境界を扱う Manager
	//================================================================

	void Initialize();  // ImGui のデバイス初期化は PlatformManager 済みなので、この Manager は追加初期化しない。
	void Update();  // ImGui と ImGuizmo の新しい 1 フレームを開始する。
	void Draw();  // ImGui の描画データを確定し、Renderer に描画可能フラグを渡す。
};

#pragma warning(pop)
