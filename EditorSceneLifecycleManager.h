#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSceneLifecycleManager {
public:
	//================================================================
	// EditorScene と Runtime の寿命を扱う Manager
	//================================================================

	void Initialize();  // Scene、Runtime、選択同期、各エディターパネルを初期化する。
	void Update();  // Play 中 Runtime と、GameObject / SceneObject の対応関係を更新する。
	void Draw();  // Play 中だけ Runtime 側の描画反映を実行する。
};

#pragma warning(pop)
