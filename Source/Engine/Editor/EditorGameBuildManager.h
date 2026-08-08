#pragma once

#include <string>
#include <vector>

//================================================================
// ゲームビルド設定
//================================================================

struct EditorGameBuildSettings {
	std::string productName = "CG2Game";
	std::string outputDirectory = "Builds/CG2Game";
	std::string startupScenePath;
	std::vector<std::string> scenePaths;
	bool includeOnlyReferencedAssets = true;  // trueならScene依存AssetとEngine共通Shaderだけを出力する。
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
		const std::string& filePath,
		EditorGameBuildSettings& buildSettings);
	static bool SaveSettingsFile(
		const std::string& filePath,
		const EditorGameBuildSettings& buildSettings);
};
