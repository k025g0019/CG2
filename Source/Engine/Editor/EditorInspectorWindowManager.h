#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorInspectorWindowManager {
public:
	//================================================================
	// 選択対象の詳細編集を扱う Manager
	//================================================================

	void Initialize();  // InspectorPanel 本体は SceneLifecycleManager が初期化するため、ここでは空。
	void Draw();  // Inspector に必要な共有状態を Context に詰めて描画する。
};

#pragma warning(pop)
