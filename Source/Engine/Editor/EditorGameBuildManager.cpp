#include "EditorGameBuildManager.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <queue>
#include <regex>
#include <set>
#include <system_error>
#include <utility>

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};
	constexpr const char* kProjectSettingsPath = "ProjectSettings/GameBuildSettings.cg2";
	constexpr const char* kStandaloneManifestName = "game.build";

	std::filesystem::path GetExecutableDirectory() {
		std::wstring executablePath(MAX_PATH, L'\0');
		const DWORD pathLength = GetModuleFileNameW(
			nullptr,
			executablePath.data(),
			static_cast<DWORD>(executablePath.size()));

		if (pathLength == 0u ||
			static_cast<size_t>(pathLength) >= executablePath.size()) {
			return std::filesystem::current_path();
		}

		executablePath.resize(static_cast<size_t>(pathLength));
		return std::filesystem::path(executablePath).parent_path();
	}

	std::filesystem::path FindProjectRoot() {
		std::filesystem::path searchPath = GetExecutableDirectory();
		std::error_code fileError;

		for (int32_t parentIndex = 0; parentIndex < 6; parentIndex++) {
			fileError.clear();

			if (std::filesystem::exists(searchPath / "CG2.sln", fileError)) {
				return searchPath;
			}

			if (!searchPath.has_parent_path()) {
				break;
			}

			const std::filesystem::path parentPath = searchPath.parent_path();

			if (parentPath == searchPath) {
				break;
			}

			searchPath = parentPath;
		}

		return std::filesystem::current_path();
	}

	std::string TrimUtf8Bom(const std::string& text) {
		if (text.size() >= 3u &&
			static_cast<unsigned char>(text[0]) == kUtf8Bom[0] &&
			static_cast<unsigned char>(text[1]) == kUtf8Bom[1] &&
			static_cast<unsigned char>(text[2]) == kUtf8Bom[2]) {
			return text.substr(3u);
		}

		return text;
	}

	std::string MakeExecutableName(const std::string& productName) {
		std::string executableName;

		for (const char sourceCharacter : productName) {
			const unsigned char character =
				static_cast<unsigned char>(sourceCharacter);
			const bool isAllowedCharacter =
				character >= 0x80u ||
				(std::iscntrl(character) == 0 &&
					character != '<' && character != '>' && character != ':' &&
					character != '"' && character != '/' && character != '\\' &&
					character != '|' && character != '?' && character != '*');

			if (isAllowedCharacter) {
				executableName.push_back(sourceCharacter);
			}
		}

		if (executableName.empty()) {
			executableName = "CG2Game";
		}

		return executableName + ".exe";
	}

	bool CopyDirectory(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& destinationPath,
		std::error_code& fileError) {
		fileError.clear();

		if (!std::filesystem::exists(sourcePath, fileError)) {
			return true;
		}

		std::filesystem::create_directories(destinationPath, fileError);

		if (fileError) {
			return false;
		}

		std::filesystem::copy(
			sourcePath,
			destinationPath,
			std::filesystem::copy_options::recursive |
				std::filesystem::copy_options::overwrite_existing,
			fileError);
		return !fileError;
	}

	bool IsSceneIncluded(
		const EditorGameBuildSettings& buildSettings,
		const std::string& scenePath) {
		return std::find(
			buildSettings.scenePaths.begin(),
			buildSettings.scenePaths.end(),
			scenePath) != buildSettings.scenePaths.end();
	}

	bool CopyFileWithRelativePath(
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& outputDirectory,
		const std::filesystem::path& relativePath,
		std::error_code& fileError) {
		fileError.clear();
		const std::filesystem::path sourcePath = (projectRoot / relativePath).lexically_normal();

		if (!std::filesystem::exists(sourcePath, fileError) ||
			!std::filesystem::is_regular_file(sourcePath, fileError)) {
			return false;
		}

		const std::filesystem::path destinationPath = outputDirectory / relativePath;
		std::filesystem::create_directories(destinationPath.parent_path(), fileError);

		if (fileError) {
			return false;
		}

		std::filesystem::copy_file(
			sourcePath,
			destinationPath,
			std::filesystem::copy_options::overwrite_existing,
			fileError);
		return !fileError;
	}

	bool IsRecursiveDependencyAsset(const std::filesystem::path& assetPath) {
		std::string extension = assetPath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".scene" || extension == ".prefab" ||
			extension == ".material" || extension == ".animclip" ||
			extension == ".animgraph" || extension == ".effect" ||
			extension == ".inputactions" || extension == ".gamedata" ||
			extension == ".json" || extension == ".xml" || extension == ".txt";
	}

	bool IsModelAsset(const std::filesystem::path& assetPath) {
		std::string extension = assetPath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".fbx" || extension == ".obj" ||
			extension == ".gltf" || extension == ".glb";
	}

	void QueueDependenciesFromFile(
		const std::filesystem::path& sourcePath,
		std::queue<std::filesystem::path>& pendingAssets) {
		std::ifstream sourceFile(sourcePath, std::ios::binary);

		if (!sourceFile.is_open()) {
			return;
		}

		const std::string sourceText(
			(std::istreambuf_iterator<char>(sourceFile)),
			std::istreambuf_iterator<char>());
		const std::regex assetReferencePattern(
			R"(((?:Assets|resources)[/\\][^|"\r\n<>]*?\.(?:scene|prefab|fbx|obj|gltf|glb|png|jpg|jpeg|dds|tga|bmp|wav|mp3|ogg|dll|animclip|animgraph|effect|efk|efkefc|inputactions|gamedata|json|xml)))",
			std::regex_constants::icase);

		for (std::sregex_iterator iterator(sourceText.begin(), sourceText.end(), assetReferencePattern), endIterator;
			 iterator != endIterator;
			 ++iterator) {
			pendingAssets.push(std::filesystem::path((*iterator)[1].str()).lexically_normal());
		}
	}

	bool CopyReferencedRuntimeAssets(
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& outputDirectory,
		const EditorGameBuildSettings& buildSettings,
		std::error_code& fileError,
		size_t& copiedAssetCount) {
		std::queue<std::filesystem::path> pendingAssets;
		std::set<std::string> copiedAssets;
		copiedAssetCount = 0u;

		for (const std::string& scenePath : buildSettings.scenePaths) {
			pendingAssets.push(std::filesystem::path(scenePath).lexically_normal());
		}

		pendingAssets.push(std::filesystem::path("resources/editorScene.scene"));

		while (!pendingAssets.empty()) {
			const std::filesystem::path relativePath = pendingAssets.front().lexically_normal();
			pendingAssets.pop();
			const std::string relativeKey = relativePath.generic_string();

			if (relativePath.is_absolute() || relativeKey.find("..") != std::string::npos ||
				!copiedAssets.insert(relativeKey).second) {
				continue;
			}

			if (!CopyFileWithRelativePath(projectRoot, outputDirectory, relativePath, fileError)) {
				if (relativePath == std::filesystem::path("resources/editorScene.scene")) {
					fileError.clear();
					continue;
				}

				return false;
			}

			copiedAssetCount++;

			if (IsRecursiveDependencyAsset(relativePath)) {
				QueueDependenciesFromFile(projectRoot / relativePath, pendingAssets);
			}

			if (IsModelAsset(relativePath)) {
				const std::filesystem::path modelDirectory = projectRoot / relativePath.parent_path();

				for (const std::filesystem::directory_entry& siblingEntry :
					 std::filesystem::directory_iterator(modelDirectory, fileError)) {
					if (fileError || !siblingEntry.is_regular_file(fileError)) {
						continue;
					}

					std::string siblingExtension = siblingEntry.path().extension().string();
					std::transform(
						siblingExtension.begin(),
						siblingExtension.end(),
						siblingExtension.begin(),
						[](unsigned char character) { return static_cast<char>(std::tolower(character)); });

					if (siblingExtension == ".png" || siblingExtension == ".jpg" ||
						siblingExtension == ".jpeg" || siblingExtension == ".dds" ||
						siblingExtension == ".tga" || siblingExtension == ".bmp") {
						pendingAssets.push(relativePath.parent_path() / siblingEntry.path().filename());
					}
				}

				fileError.clear();
			}
		}

		const std::vector<std::pair<std::filesystem::path, std::string>> commonDirectories = {
			{projectRoot / "Assets" / "Shaders", "Assets/Shaders"},
			{projectRoot / "resources" / "editorDefault", "resources/editorDefault"},
		};

		for (const auto& [sourcePath, destinationName] : commonDirectories) {
			if (!CopyDirectory(sourcePath, outputDirectory / destinationName, fileError)) {
				return false;
			}
		}

		return true;
	}
}

bool EditorGameBuildManager::LoadProjectSettings(EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath = FindProjectRoot() / kProjectSettingsPath;
	return LoadSettingsFile(settingsPath.generic_string(), buildSettings);
}

bool EditorGameBuildManager::SaveProjectSettings(
	const EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath = FindProjectRoot() / kProjectSettingsPath;
	return SaveSettingsFile(settingsPath.generic_string(), buildSettings);
}

bool EditorGameBuildManager::ExportReleaseGame(
	const EditorGameBuildSettings& buildSettings,
	std::string& resultMessage) {
	resultMessage.clear();

	if (buildSettings.startupScenePath.empty() ||
		!IsSceneIncluded(buildSettings, buildSettings.startupScenePath)) {
		resultMessage = "Build: 起動シーンをビルド対象のシーン一覧から選択してください";
		return false;
	}

	const std::filesystem::path projectRoot = FindProjectRoot();
	const std::filesystem::path releaseDirectory = projectRoot / "x64" / "Release";
	const std::filesystem::path releaseExecutablePath = releaseDirectory / "CG2.exe";
	std::error_code fileError;

	if (!std::filesystem::exists(releaseExecutablePath, fileError)) {
		resultMessage = "Build: x64/Release/CG2.exe がありません。先に Release をビルドしてください";
		return false;
	}

	for (const std::string& scenePath : buildSettings.scenePaths) {
		fileError.clear();
		if (!std::filesystem::exists(projectRoot / scenePath, fileError)) {
			resultMessage = "Build: シーンがありません " + scenePath;
			return false;
		}
	}

	std::filesystem::path outputDirectory = buildSettings.outputDirectory.empty()
		? std::filesystem::path("Builds/CG2Game")
		: std::filesystem::path(buildSettings.outputDirectory);

	if (outputDirectory.is_relative()) {
		outputDirectory = projectRoot / outputDirectory;
	}

	outputDirectory = outputDirectory.lexically_normal();
	std::filesystem::create_directories(outputDirectory, fileError);

	if (fileError) {
		resultMessage = "Build: 出力フォルダーを作成できません";
		return false;
	}

	fileError.clear();
	if (std::filesystem::equivalent(releaseDirectory, outputDirectory, fileError)) {
		resultMessage = "Build: x64/Release 自体は出力先に指定できません";
		return false;
	}

	const std::filesystem::path gameExecutablePath =
		outputDirectory / MakeExecutableName(buildSettings.productName);
	fileError.clear();
	std::filesystem::copy_file(
		releaseExecutablePath,
		gameExecutablePath,
		std::filesystem::copy_options::overwrite_existing,
		fileError);

	if (fileError) {
		resultMessage = "Build: ゲーム実行ファイルをコピーできません";
		return false;
	}

	for (const std::filesystem::directory_entry& entry :
		 std::filesystem::directory_iterator(releaseDirectory, fileError)) {
		if (fileError) {
			break;
		}

		if (!entry.is_regular_file(fileError) || entry.path().extension() != ".dll") {
			continue;
		}

		fileError.clear();
		std::filesystem::copy_file(
			entry.path(),
			outputDirectory / entry.path().filename(),
			std::filesystem::copy_options::overwrite_existing,
			fileError);

		if (fileError) {
			resultMessage = "Build: 実行用 DLL をコピーできません";
			return false;
		}
	}

	const std::vector<std::pair<std::filesystem::path, std::string>> runtimeDirectories = {
		{releaseDirectory / "ThirdParty", "ThirdParty"},
	};

	for (const auto& [sourcePath, destinationName] : runtimeDirectories) {
		if (!CopyDirectory(
				sourcePath,
				outputDirectory / destinationName,
				fileError)) {
			resultMessage = "Build: " + destinationName + " をコピーできません";
			return false;
		}
	}

	size_t copiedAssetCount = 0u;

	if (buildSettings.includeOnlyReferencedAssets) {
		if (!CopyReferencedRuntimeAssets(
				projectRoot,
				outputDirectory,
				buildSettings,
				fileError,
				copiedAssetCount)) {
			resultMessage = "Build: 参照Assetまたは依存Assetをコピーできません";
			return false;
		}
	}
	else {
		const std::vector<std::pair<std::filesystem::path, std::string>> allAssetDirectories = {
			{projectRoot / "Assets", "Assets"},
			{projectRoot / "resources", "resources"},
		};

		for (const auto& [sourcePath, destinationName] : allAssetDirectories) {
			if (!CopyDirectory(sourcePath, outputDirectory / destinationName, fileError)) {
				resultMessage = "Build: " + destinationName + " をコピーできません";
				return false;
			}
		}
	}

	if (!SaveSettingsFile(
			(outputDirectory / kStandaloneManifestName).generic_string(),
			buildSettings)) {
		resultMessage = "Build: game.build を保存できません";
		return false;
	}

	resultMessage = "Build: ゲームを書き出しました " + gameExecutablePath.generic_string();

	if (buildSettings.includeOnlyReferencedAssets) {
		resultMessage += " (参照Asset " + std::to_string(copiedAssetCount) + "件 + 共通Shader)";
	}
	return true;
}

bool EditorGameBuildManager::TryLoadStandaloneManifest(
	EditorGameBuildSettings& buildSettings,
	std::string& resultMessage) {
	resultMessage.clear();
	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	const std::filesystem::path manifestPath =
		executableDirectory / kStandaloneManifestName;

	if (!std::filesystem::exists(manifestPath)) {
		return false;
	}

	std::error_code fileError;
	std::filesystem::current_path(executableDirectory, fileError);

	if (fileError || !LoadSettingsFile(manifestPath.generic_string(), buildSettings)) {
		resultMessage = "GameBuild: game.build を読み込めません";
		return false;
	}

	if (buildSettings.startupScenePath.empty() ||
		!std::filesystem::exists(buildSettings.startupScenePath)) {
		resultMessage = "GameBuild: 起動シーンがありません";
		return false;
	}

	resultMessage = "GameBuild: " + buildSettings.startupScenePath;
	return true;
}

bool EditorGameBuildManager::LoadSettingsFile(
	const std::string& filePath,
	EditorGameBuildSettings& buildSettings) {
	std::ifstream file(filePath, std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	EditorGameBuildSettings loadedSettings{};
	loadedSettings.scenePaths.clear();
	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (isFirstLine) {
			line = TrimUtf8Bom(line);
			isFirstLine = false;
		}

		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		const size_t separatorPosition = line.find('|');

		if (separatorPosition == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0u, separatorPosition);
		const std::string value = line.substr(separatorPosition + 1u);

		if (key == "ProductName") {
			loadedSettings.productName = value;
		}
		else if (key == "OutputDirectory") {
			loadedSettings.outputDirectory = value;
		}
		else if (key == "StartupScene") {
			loadedSettings.startupScenePath = value;
		}
		else if (key == "ReferencedAssetsOnly") {
			loadedSettings.includeOnlyReferencedAssets = value != "0";
		}
		else if (key == "Scene" && !value.empty() &&
			std::find(
				loadedSettings.scenePaths.begin(),
				loadedSettings.scenePaths.end(),
				value) == loadedSettings.scenePaths.end()) {
			loadedSettings.scenePaths.push_back(value);
		}
	}

	buildSettings = loadedSettings;
	return true;
}

bool EditorGameBuildManager::SaveSettingsFile(
	const std::string& filePath,
	const EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath(filePath);
	const std::filesystem::path parentPath = settingsPath.parent_path();
	std::error_code fileError;

	if (!parentPath.empty()) {
		std::filesystem::create_directories(parentPath, fileError);
	}

	if (fileError) {
		return false;
	}

	std::ofstream file(settingsPath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		return false;
	}

	file.write(
		reinterpret_cast<const char*>(kUtf8Bom),
		static_cast<std::streamsize>(sizeof(kUtf8Bom)));
	file << "CG2GameBuild|1\r\n";
	file << "ProductName|" << buildSettings.productName << "\r\n";
	file << "OutputDirectory|" << buildSettings.outputDirectory << "\r\n";
	file << "StartupScene|" << buildSettings.startupScenePath << "\r\n";
	file << "ReferencedAssetsOnly|" << (buildSettings.includeOnlyReferencedAssets ? 1 : 0) << "\r\n";

	for (const std::string& scenePath : buildSettings.scenePaths) {
		file << "Scene|" << scenePath << "\r\n";
	}

	return file.good();
}
