#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include "GameScene.h"
#include "../Editor/EditorGameBuildManager.h"
#include "../Editor/EditorWaterRailShooterSceneBuilder.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {
	void SaveGameBuildCommandResult(const std::string& resultMessage) {
		std::error_code fileError;
		std::filesystem::create_directories("BuildLogs", fileError);

		if (fileError) {
			return;
		}

		std::ofstream resultFile(
			"BuildLogs/GameBuildResult.log",
			std::ios::binary | std::ios::trunc);

		if (!resultFile.is_open()) {
			return;
		}

		constexpr unsigned char utf8Bom[] = {0xEFu, 0xBBu, 0xBFu};
		resultFile.write(
			reinterpret_cast<const char*>(utf8Bom),
			static_cast<std::streamsize>(sizeof(utf8Bom)));
		resultFile << resultMessage << '\n';
	}
}

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

	//============================================================
	// ゲーム書き出しのコマンドライン実行
	//============================================================

	if (commandLineText.find("--build-game") != std::string::npos) {
		EditorGameBuildSettings buildSettings{};

		if (!EditorGameBuildManager::LoadProjectSettings(buildSettings)) {
			const std::string resultMessage =
				"Build: ProjectSettings/GameBuildSettings.cg2 を読み込めません";
			SaveGameBuildCommandResult(resultMessage);
			OutputDebugStringA((resultMessage + "\n").c_str());
			return 1;
		}

		std::string resultMessage;
		const bool isBuildSucceeded = EditorGameBuildManager::ExportReleaseGame(
			buildSettings,
			resultMessage);
		SaveGameBuildCommandResult(resultMessage);
		OutputDebugStringA((resultMessage + "\n").c_str());
		return isBuildSucceeded ? 0 : 1;
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
