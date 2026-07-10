#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include "GameScene.h"

int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	GameScene gameScene;  // GameScene は main から直接呼ぶ唯一の Scene 管理クラス。
	gameScene.Initialize(instanceHandle);  // instanceHandle は Window 作成と DirectInput 初期化に必要な Windows アプリの実体。

	// 終了要求が来るまで、1フレームごとに更新と描画を明確に分けて呼ぶ。
	while (!gameScene.IsEndRequested()) {
		gameScene.Update();
		gameScene.Draw();
	}

	// Finalize が返す終了コードを、そのまま Windows アプリの戻り値にする。
	return gameScene.Finalize();
}
