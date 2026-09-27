#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include "GameScene.h"
#include "EngineEnvironmentCheck.h"
#include "EngineVersion.h"
#include "ProjectVersionManager.h"
#include "../Editor/EditorGameBuildManager.h"
#include "../Editor/EditorWaterRailShooterSceneBuilder.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <shellapi.h>
#include <string>
#include <vector>

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

	std::vector<std::wstring> GetWideArguments() {
		int argumentCount = 0;
		LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
		std::vector<std::wstring> result;
		if (arguments != nullptr) {
			for (int index = 0; index < argumentCount; ++index) result.emplace_back(arguments[index]);
			LocalFree(arguments);
		}
		return result;
	}

	bool HasArgument(const std::vector<std::wstring>& arguments, const wchar_t* expected) {
		return std::find(arguments.begin(), arguments.end(), expected) != arguments.end();
	}

	std::wstring Utf8ToWide(const std::string& text) {
		if (text.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (count <= 0) return L"文字列変換に失敗しました";
		std::wstring result(static_cast<std::size_t>(count), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
			static_cast<int>(text.size()), result.data(), count);
		return result;
	}

	std::filesystem::path GetExecutableDirectory() {
		std::wstring path(32768U, L'\0');
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		path.resize(length);
		return std::filesystem::path(path).parent_path();
	}

	bool PrepareProjectCompatibility(const std::filesystem::path& projectRoot, bool allowsPrompt) {
		ProjectVersionSettings settings{};
		std::string error;
		if (!ProjectVersionManager::Load(projectRoot, settings, error)) {
			if (!allowsPrompt || MessageBoxW(nullptr,
				L"Version Metadataのない旧Projectです。Backupを作成してMigrationしますか？",
				L"ManoEngine Project Migration", MB_YESNO | MB_ICONWARNING) != IDYES) {
				ProjectVersionManager::SetCurrentProjectWriteAllowed(false);
				return false;
			}
			if (!ProjectVersionManager::MigrateProject(projectRoot, error)) {
				const std::wstring wideError = Utf8ToWide(error);
				MessageBoxW(nullptr, wideError.c_str(), L"Migration Failed", MB_OK | MB_ICONERROR);
				return false;
			}
			ProjectVersionManager::Load(projectRoot, settings, error);
		}

		ProjectCompatibilityResult compatibility = ProjectVersionManager::Evaluate(settings);
		if (compatibility.status == ProjectCompatibilityStatus::NeedsMigration && allowsPrompt) {
			const std::wstring prompt = Utf8ToWide(
				compatibility.message + "\nBackup後にMigrationを実行しますか？");
			const int answer = MessageBoxW(nullptr, prompt.c_str(),
				L"ManoEngine Project Migration", MB_YESNO | MB_ICONWARNING);
			if (answer == IDYES && ProjectVersionManager::MigrateProject(projectRoot, error)) {
				ProjectVersionManager::Load(projectRoot, settings, error);
				compatibility = ProjectVersionManager::Evaluate(settings);
			} else if (answer == IDYES) {
				const std::wstring wideError = Utf8ToWide(error);
				MessageBoxW(nullptr, wideError.c_str(), L"Migration Failed", MB_OK | MB_ICONERROR);
			}
		}
		ProjectVersionManager::SetCurrentProjectWriteAllowed(compatibility.canSave);
		if (!compatibility.canOpen || !compatibility.canSave) {
			const std::wstring prompt = Utf8ToWide(
				compatibility.message + "\nManoLauncherから必要VersionのInstall/切替を行ってください。");
			MessageBoxW(nullptr, prompt.c_str(),
				L"ManoEngine Version Compatibility", MB_OK | MB_ICONERROR);
			return false;
		}
		return true;
	}
}

int WINAPI WinMain(
	_In_ HINSTANCE instanceHandle,
	_In_opt_ HINSTANCE,
	_In_ LPSTR commandLine,
	_In_ int) {
	const std::string commandLineText = commandLine != nullptr ? commandLine : "";
	const std::vector<std::wstring> wideArguments = GetWideArguments();
	for (std::size_t index = 1U; index + 1U < wideArguments.size(); ++index) {
		if (wideArguments[index] == L"--project") {
			std::error_code directoryError;
			std::filesystem::current_path(std::filesystem::path(wideArguments[index + 1U]), directoryError);
			if (directoryError) {
				MessageBoxW(nullptr, L"Project Folderを開けません", L"ManoEngine", MB_OK | MB_ICONERROR);
				return 2;
			}
			break;
		}
	}

	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	const bool isStandalonePackage = std::filesystem::exists(executableDirectory / "game.build");
	if (HasArgument(wideArguments, L"--migrate-project")) {
		std::string migrationResult;
		const bool succeeded = ProjectVersionManager::MigrateProject(std::filesystem::current_path(), migrationResult);
		SaveGameBuildCommandResult(migrationResult);
		return succeeded ? 0 : 3;
	}
	if (HasArgument(wideArguments, L"--check-environment")) {
		const EnvironmentCheckMode mode = HasArgument(wideArguments, L"--developer-mode")
			? EnvironmentCheckMode::EngineDeveloper : EnvironmentCheckMode::EditorUser;
		const EnvironmentCheckReport report = EngineEnvironmentCheck::Run(executableDirectory, mode);
		SaveGameBuildCommandResult(report.ToText());
		return report.HasRequiredFailure() ? 4 : 0;
	}
	if (!isStandalonePackage && !PrepareProjectCompatibility(std::filesystem::current_path(), true)) return 5;

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
