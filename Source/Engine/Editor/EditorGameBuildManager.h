#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

//================================================================
// ゲームビルド設定
//================================================================

enum class EditorGameBuildConfiguration : int32_t {
	Development = 0,  // Debug情報とConsoleを残し、動作確認・不具合調査に使う。
	Release = 1,      // 配布用の最適化ビルド。
};

struct EditorGameBuildSettings {
	std::string productName = "CG2EngineGame";
	std::string outputDirectory = "Builds/CG2EngineGame";
	std::string startupScenePath;
	std::vector<std::string> scenePaths;
	EditorGameBuildConfiguration configuration = EditorGameBuildConfiguration::Release;
	bool includeOnlyReferencedAssets = true;  // trueならScene依存AssetとEngine共通Shaderだけを出力する。
	std::string engineVersion;  // 書き出しに使用したEditor本体のVersion。
	std::string engineChannel;
	std::uint32_t projectFormatVersion = 0U;
	std::uint32_t scriptApiVersion = 0U;
};

//================================================================
// Release Player の書き出しと起動設定の読み込み
//================================================================

class EditorGameBuildManager {
public:
	static bool LoadProjectSettings(EditorGameBuildSettings& buildSettings);
	static bool SaveProjectSettings(const EditorGameBuildSettings& buildSettings);
	static bool ExportReleaseGame(
		const EditorGameBuildSettings& buildSettings,
		std::string& resultMessage);
	static bool TryLoadStandaloneManifest(
		EditorGameBuildSettings& buildSettings,
		std::string& resultMessage);

private:
	static bool LoadSettingsFile(
		const std::filesystem::path& filePath,
		EditorGameBuildSettings& buildSettings);
	static bool SaveSettingsFile(
		const std::filesystem::path& filePath,
		const EditorGameBuildSettings& buildSettings);
};
