#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorFrameInputManager {
public:
	//================================================================
	// 入力状態と描画サイズを更新する Manager
	//================================================================

	void Initialize();  // 共有リソースは PlatformManager が作るため、この Manager では追加初期化を持たない。
	void Update();  // キーボード、エディターカメラ、SwapChain サイズを 1 フレーム分更新する。
	void Draw();  // 入力担当なので描画処理は持たない。
};

#pragma warning(pop)
