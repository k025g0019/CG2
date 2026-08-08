#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include "GameScene.h"
#include "../Editor/EditorWaterRailShooterSceneBuilder.h"

#include <string>

int WINAPI WinMain(
	_In_ HINSTANCE instanceHandle,
	_In_opt_ HINSTANCE,
	_In_ LPSTR commandLine,
	_In_ int) {
	const std::string commandLineText = commandLine != nullptr ? commandLine : "";

	//============================================================
	// 開発用の非表示Scene生成
	//============================================================

	if (commandLineText.find("--generate-water-rail-shooter-0817") != std::string::npos) {
		std::string resultMessage;
		const bool isGenerated = EditorWaterRailShooterSceneBuilder::Generate(resultMessage);
		OutputDebugStringA((resultMessage + "\n").c_str());
		return isGenerated ? 0 : 1;
	}

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
