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
	constexpr const wchar_t* kReleaseGameBuildLogPath = L"BuildLogs/ReleaseGameBuild.log";

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

	std::filesystem::path FindMsBuildExecutable() {
		std::wstring searchedPath(32768u, L'\0');
		const DWORD searchedLength = SearchPathW(
			nullptr,
			L"MSBuild.exe",
			nullptr,
			static_cast<DWORD>(searchedPath.size()),
			searchedPath.data(),
			nullptr);

		if (searchedLength > 0u &&
			static_cast<size_t>(searchedLength) < searchedPath.size()) {
			searchedPath.resize(static_cast<size_t>(searchedLength));
			return std::filesystem::path(searchedPath);
		}

		wchar_t programFilesPath[MAX_PATH]{};
		const DWORD programFilesLength = GetEnvironmentVariableW(
			L"ProgramFiles",
			programFilesPath,
			static_cast<DWORD>(_countof(programFilesPath)));

		if (programFilesLength == 0u ||
			static_cast<size_t>(programFilesLength) >= _countof(programFilesPath)) {
			return {};
		}

		const std::filesystem::path visualStudioRoot =
			std::filesystem::path(programFilesPath) / "Microsoft Visual Studio";
		const std::vector<std::string> visualStudioVersions = {"18", "17"};
		const std::vector<std::string> visualStudioEditions = {
			"Community",
			"Professional",
			"Enterprise",
			"BuildTools",
		};
		std::error_code fileError;

		for (const std::string& visualStudioVersion : visualStudioVersions) {
			for (const std::string& visualStudioEdition : visualStudioEditions) {
				const std::filesystem::path msBuildPath =
					visualStudioRoot / visualStudioVersion / visualStudioEdition /
					"MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe";
				fileError.clear();

				if (std::filesystem::exists(msBuildPath, fileError)) {
					return msBuildPath;
				}
			}
		}

		return {};
	}

	std::wstring QuoteCommandArgument(const std::filesystem::path& argumentPath) {
		return L"\"" + argumentPath.wstring() + L"\"";
	}

	bool RunBuildProcess(
		const std::filesystem::path& executablePath,
		const std::wstring& processArguments,
		const std::filesystem::path& workingDirectory,
		const std::filesystem::path& logPath,
		DWORD& processExitCode) {
		processExitCode = ERROR_PROCESS_ABORTED;
		std::error_code fileError;
		std::filesystem::create_directories(logPath.parent_path(), fileError);

		if (fileError) {
			return false;
		}

		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES));
		securityAttributes.bInheritHandle = TRUE;
		const HANDLE logFileHandle = CreateFileW(
			logPath.c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			&securityAttributes,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (logFileHandle == INVALID_HANDLE_VALUE) {
			return false;
		}

		STARTUPINFOW startupInfo{};
		startupInfo.cb = static_cast<DWORD>(sizeof(STARTUPINFOW));
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = logFileHandle;
		startupInfo.hStdError = logFileHandle;
		PROCESS_INFORMATION processInformation{};
		std::wstring commandLine =
			QuoteCommandArgument(executablePath) + L" " + processArguments;

		const BOOL processCreated = CreateProcessW(
			executablePath.c_str(),
			commandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			workingDirectory.c_str(),
			&startupInfo,
			&processInformation);
		CloseHandle(logFileHandle);

		if (processCreated == FALSE) {
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, INFINITE);
		const BOOL exitCodeRead = GetExitCodeProcess(
			processInformation.hProcess,
			&processExitCode);
		CloseHandle(processInformation.hThread);
		CloseHandle(processInformation.hProcess);
		return waitResult == WAIT_OBJECT_0 && exitCodeRead != FALSE;
	}

	bool BuildReleasePlayer(
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& releaseDirectory,
		std::string& resultMessage) {
		const std::filesystem::path msBuildPath = FindMsBuildExecutable();

		if (msBuildPath.empty()) {
			resultMessage = "Build: MSBuild.exe が見つかりません";
			return false;
		}

		const std::filesystem::path buildLogPath = projectRoot / kReleaseGameBuildLogPath;
		std::error_code fileError;
		std::filesystem::create_directories(releaseDirectory, fileError);

		if (fileError) {
			resultMessage = "Build: ReleaseGame用フォルダーを作成できません";
			return false;
		}

		std::wstring processArguments;
		processArguments += QuoteCommandArgument(projectRoot / "CG2.vcxproj");
		processArguments += L" /t:Build";
		processArguments += L" /p:Configuration=Release";
		processArguments += L" /p:Platform=x64";
		processArguments += L" \"/p:OutDir=" + releaseDirectory.wstring() + L"/\"";
		processArguments += L" /m:1 /nodeReuse:false /nologo /verbosity:minimal";
		DWORD processExitCode = ERROR_PROCESS_ABORTED;

		if (!RunBuildProcess(
				msBuildPath,
				processArguments,
				projectRoot,
				buildLogPath,
				processExitCode) ||
			processExitCode != ERROR_SUCCESS) {
			resultMessage =
				"Build: ReleaseGame本体のビルドに失敗しました。ログ: " +
				buildLogPath.generic_string();
			return false;
		}

		const std::filesystem::path releaseExecutablePath = releaseDirectory / "CG2.exe";
		fileError.clear();

		if (!std::filesystem::exists(releaseExecutablePath, fileError)) {
			resultMessage = "Build: ビルド成功後のCG2.exeが見つかりません";
			return false;
		}

		return true;
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

	std::wstring Utf8ToWide(const std::string& text) {
		if (text.empty()) {
			return {};
		}

		const int32_t convertedSize = MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			text.data(),
			static_cast<int32_t>(text.size()),
			nullptr,
			0);

		if (convertedSize <= 0) {
			return {};
		}

		std::wstring convertedText(static_cast<size_t>(convertedSize), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			text.data(),
			static_cast<int32_t>(text.size()),
			convertedText.data(),
			convertedSize);
		return convertedText;
	}

	std::string WideToUtf8(const std::wstring& text) {
		if (text.empty()) {
			return {};
		}

		const int32_t convertedSize = WideCharToMultiByte(
			CP_UTF8,
			0,
			text.data(),
			static_cast<int32_t>(text.size()),
			nullptr,
			0,
			nullptr,
			nullptr);

		if (convertedSize <= 0) {
			return {};
		}

		std::string convertedText(static_cast<size_t>(convertedSize), '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			text.data(),
			static_cast<int32_t>(text.size()),
			convertedText.data(),
			convertedSize,
			nullptr,
			nullptr);
		return convertedText;
	}

	std::filesystem::path Utf8Path(const std::string& text) {
		return std::filesystem::path(Utf8ToWide(text));
	}

	std::filesystem::path MakeExecutableName(const std::string& productName) {
		const std::wstring sourceName = Utf8ToWide(productName);
		std::wstring executableName;

		for (const wchar_t sourceCharacter : sourceName) {
			const bool isAllowedCharacter =
				sourceCharacter >= L' ' &&
				sourceCharacter != L'<' && sourceCharacter != L'>' &&
				sourceCharacter != L':' && sourceCharacter != L'"' &&
				sourceCharacter != L'/' && sourceCharacter != L'\\' &&
				sourceCharacter != L'|' && sourceCharacter != L'?' &&
				sourceCharacter != L'*';

			if (isAllowedCharacter) {
				executableName.push_back(sourceCharacter);
			}
		}

		// Windowsでは末尾の空白とピリオドをファイル名へ使用できない。
		while (!executableName.empty() &&
			(executableName.back() == L' ' || executableName.back() == L'.')) {
			executableName.pop_back();
		}

		if (executableName.empty()) {
			executableName = L"CG2Game";
		}

		return std::filesystem::path(executableName + L".exe");
	}

	bool CopyFileWithRetry(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& destinationPath,
		std::error_code& fileError) {
		constexpr int32_t maximumAttemptCount = 100;
		constexpr DWORD retryWaitMilliseconds = 100u;

		for (int32_t attemptIndex = 0;
			 attemptIndex < maximumAttemptCount;
			 attemptIndex++) {
			fileError.clear();
			std::filesystem::copy_file(
				sourcePath,
				destinationPath,
				std::filesystem::copy_options::overwrite_existing,
				fileError);

			if (!fileError) {
				return true;
			}

			const int32_t errorValue = static_cast<int32_t>(fileError.value());
			const bool isTemporaryFileLock =
				errorValue == static_cast<int32_t>(ERROR_ACCESS_DENIED) ||
				errorValue == static_cast<int32_t>(ERROR_SHARING_VIOLATION) ||
				errorValue == static_cast<int32_t>(ERROR_LOCK_VIOLATION) ||
				errorValue == static_cast<int32_t>(ERROR_USER_MAPPED_FILE);

			if (!isTemporaryFileLock) {
				return false;
			}

			Sleep(retryWaitMilliseconds);
		}

		return false;
	}

	bool CopyDirectory(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& destinationPath,
		std::error_code& fileError,
		std::filesystem::path& failedCopyPath) {
		fileError.clear();

		if (!std::filesystem::exists(sourcePath, fileError)) {
			return true;
		}

		std::filesystem::create_directories(destinationPath, fileError);

		if (fileError) {
			failedCopyPath = destinationPath;
			return false;
		}

		std::filesystem::recursive_directory_iterator sourceIterator(sourcePath, fileError);
		const std::filesystem::recursive_directory_iterator endIterator;

		if (fileError) {
			failedCopyPath = sourcePath;
			return false;
		}

		for (; sourceIterator != endIterator; sourceIterator.increment(fileError)) {
			if (fileError) {
				failedCopyPath = sourcePath;
				return false;
			}

			const std::filesystem::directory_entry& sourceEntry = *sourceIterator;
			const std::filesystem::path relativePath =
				sourceEntry.path().lexically_relative(sourcePath);
			const std::filesystem::path destinationEntryPath =
				destinationPath / relativePath;
			const bool isSourceDirectory = sourceEntry.is_directory(fileError);

			if (fileError) {
				failedCopyPath = sourceEntry.path();
				return false;
			}

			// Shaderライブラリ内のGit管理情報は実行時に不要であり、
			// Visual StudioやGitの監視中はobjectsがメモリマップされるため書き出さない。
			if (isSourceDirectory && sourceEntry.path().filename() == ".git") {
				sourceIterator.disable_recursion_pending();
				continue;
			}

			if (isSourceDirectory) {
				std::filesystem::create_directories(destinationEntryPath, fileError);
			}
			else if (sourceEntry.is_regular_file(fileError)) {
				std::filesystem::create_directories(
					destinationEntryPath.parent_path(),
					fileError);

				if (!fileError) {
					CopyFileWithRetry(
						sourceEntry.path(),
						destinationEntryPath,
						fileError);
				}
			}

			if (fileError) {
				failedCopyPath = destinationEntryPath;
				return false;
			}
		}

		return true;
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

		CopyFileWithRetry(
			sourcePath,
			destinationPath,
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
			extension == ".effectdef" ||
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
			R"(((?:Assets|resources)[/\\][^|"\r\n<>]*?\.(?:scene|prefab|material|fbx|obj|gltf|glb|png|jpg|jpeg|dds|tga|bmp|wav|mp3|ogg|flac|dll|animclip|animgraph|effect|effectdef|efk|efkefc|inputactions|gamedata|json|xml)))",
			std::regex_constants::icase);

		for (std::sregex_iterator iterator(sourceText.begin(), sourceText.end(), assetReferencePattern), endIterator;
			 iterator != endIterator;
			 ++iterator) {
			pendingAssets.push(std::filesystem::path((*iterator)[1].str()).lexically_normal());
		}
	}

	void CollectReferencedNativeScriptBuildFiles(
		const std::filesystem::path& projectRoot,
		const EditorGameBuildSettings& buildSettings,
		std::vector<std::filesystem::path>& buildScriptPaths) {
		std::queue<std::filesystem::path> pendingAssets;
		std::set<std::string> checkedAssets;
		std::set<std::string> foundBuildScripts;

		for (const std::string& scenePath : buildSettings.scenePaths) {
			pendingAssets.push(std::filesystem::path(scenePath).lexically_normal());
		}

		while (!pendingAssets.empty()) {
			const std::filesystem::path relativePath = pendingAssets.front().lexically_normal();
			pendingAssets.pop();
			const std::string relativeKey = relativePath.generic_string();

			if (relativePath.is_absolute() ||
				relativeKey.find("..") != std::string::npos ||
				!checkedAssets.insert(relativeKey).second) {
				continue;
			}

			std::queue<std::filesystem::path> referencedAssets;
			QueueDependenciesFromFile(projectRoot / relativePath, referencedAssets);

			while (!referencedAssets.empty()) {
				const std::filesystem::path referencedPath =
					referencedAssets.front().lexically_normal();
				referencedAssets.pop();
				std::string extension = referencedPath.extension().string();
				std::transform(
					extension.begin(),
					extension.end(),
					extension.begin(),
					[](unsigned char character) {
						return static_cast<char>(std::tolower(character));
					});

				if (extension == ".dll") {
					const std::string referencedKey = referencedPath.generic_string();

					if (referencedKey.rfind("resources/scripts/", 0u) == 0u) {
						const std::filesystem::path scriptDirectory =
							referencedPath.parent_path().parent_path().parent_path();
						const std::filesystem::path buildScriptPath =
							(projectRoot / scriptDirectory / "build_release.bat").lexically_normal();
						std::error_code fileError;

						if (std::filesystem::exists(buildScriptPath, fileError) &&
							foundBuildScripts.insert(buildScriptPath.generic_string()).second) {
							buildScriptPaths.push_back(buildScriptPath);
						}
					}
				}

				if (IsRecursiveDependencyAsset(referencedPath)) {
					pendingAssets.push(referencedPath);
				}
			}
		}
	}

	bool BuildReferencedNativeScripts(
		const std::filesystem::path& projectRoot,
		const EditorGameBuildSettings& buildSettings,
		std::string& resultMessage) {
		wchar_t systemDirectoryPath[MAX_PATH]{};
		const UINT systemDirectoryLength = GetSystemDirectoryW(
			systemDirectoryPath,
			static_cast<UINT>(_countof(systemDirectoryPath)));

		if (systemDirectoryLength == 0u ||
			static_cast<size_t>(systemDirectoryLength) >= _countof(systemDirectoryPath)) {
			resultMessage = "Build: cmd.exeの場所を取得できません";
			return false;
		}

		const std::filesystem::path commandInterpreterPath =
			std::filesystem::path(systemDirectoryPath) / "cmd.exe";
		std::vector<std::filesystem::path> buildScriptPaths;
		CollectReferencedNativeScriptBuildFiles(
			projectRoot,
			buildSettings,
			buildScriptPaths);

		for (const std::filesystem::path& buildScriptPath : buildScriptPaths) {
			const std::filesystem::path logPath =
				projectRoot / "BuildLogs" / "NativeScripts" /
				(buildScriptPath.parent_path().filename().wstring() + L".log");
			const std::wstring processArguments =
				L"/d /s /c \"\"" + buildScriptPath.wstring() + L"\"\"";
			DWORD processExitCode = ERROR_PROCESS_ABORTED;

			if (!RunBuildProcess(
					commandInterpreterPath,
					processArguments,
					buildScriptPath.parent_path(),
					logPath,
					processExitCode) ||
				processExitCode != ERROR_SUCCESS) {
				resultMessage =
					"Build: C++ゲームスクリプトのビルドに失敗しました。ログ: " +
					logPath.generic_string();
				return false;
			}
		}

		return true;
	}

	bool CopyReferencedRuntimeAssets(
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& outputDirectory,
		const EditorGameBuildSettings& buildSettings,
		std::error_code& fileError,
		std::filesystem::path& failedAssetPath,
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

				failedAssetPath = relativePath;
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
			// 起動確認用サウンド(EditorPlatformManager.cppでハードコードパス読込)は
			// どのSceneからも参照されないため、依存関係スキャンでは見つからない。
			{projectRoot / "resources" / "sound", "resources/sound"},
		};

		for (const auto& [sourcePath, destinationName] : commonDirectories) {
			if (!CopyDirectory(
					sourcePath,
					outputDirectory / destinationName,
					fileError,
					failedAssetPath)) {
				return false;
			}
		}

		return true;
	}
}

bool EditorGameBuildManager::LoadProjectSettings(EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath = FindProjectRoot() / kProjectSettingsPath;
	return LoadSettingsFile(settingsPath, buildSettings);
}

bool EditorGameBuildManager::SaveProjectSettings(
	const EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath = FindProjectRoot() / kProjectSettingsPath;
	return SaveSettingsFile(settingsPath, buildSettings);
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
	// x64/Release は Editor 自身がそこから起動している場合があり(Release で開いていないと重いため)、
	// そこから書き出そうとすると再ビルド時にファイルロックで失敗する。
	// 書き出し専用に独立した x64/ReleaseGame ビルドを別途用意し、そこから読む。
	const std::filesystem::path releaseDirectory = projectRoot / "x64" / "ReleaseGame";
	const std::filesystem::path releaseExecutablePath = releaseDirectory / "CG2.exe";
	std::error_code fileError;

	for (const std::string& scenePath : buildSettings.scenePaths) {
		fileError.clear();
		if (!std::filesystem::exists(projectRoot / Utf8Path(scenePath), fileError)) {
			resultMessage = "Build: シーンがありません " + scenePath;
			return false;
		}
	}

	if (!BuildReferencedNativeScripts(projectRoot, buildSettings, resultMessage)) {
		return false;
	}

	if (!BuildReleasePlayer(projectRoot, releaseDirectory, resultMessage)) {
		return false;
	}

	std::filesystem::path outputDirectory = buildSettings.outputDirectory.empty()
		? std::filesystem::path("Builds/CG2Game")
		: Utf8Path(buildSettings.outputDirectory);

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
	CopyFileWithRetry(
		releaseExecutablePath,
		gameExecutablePath,
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
		CopyFileWithRetry(
			entry.path(),
			outputDirectory / entry.path().filename(),
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
		std::filesystem::path failedCopyPath;

		if (!CopyDirectory(
				sourcePath,
				outputDirectory / destinationName,
				fileError,
				failedCopyPath)) {
			resultMessage =
				"Build: " + destinationName + " をコピーできません " +
				failedCopyPath.generic_string();
			return false;
		}
	}

	size_t copiedAssetCount = 0u;

	if (buildSettings.includeOnlyReferencedAssets) {
		std::filesystem::path failedAssetPath;

		if (!CopyReferencedRuntimeAssets(
				projectRoot,
				outputDirectory,
				buildSettings,
				fileError,
				failedAssetPath,
				copiedAssetCount)) {
			resultMessage =
				"Build: 参照Assetまたは依存Assetをコピーできません " +
				failedAssetPath.generic_string();

			if (fileError) {
				resultMessage += " (" + fileError.message() + ")";
			}
			return false;
		}
	}
	else {
		const std::vector<std::pair<std::filesystem::path, std::string>> allAssetDirectories = {
			{projectRoot / "Assets", "Assets"},
			{projectRoot / "resources", "resources"},
		};

		for (const auto& [sourcePath, destinationName] : allAssetDirectories) {
			std::filesystem::path failedCopyPath;

			if (!CopyDirectory(
					sourcePath,
					outputDirectory / destinationName,
					fileError,
					failedCopyPath)) {
				resultMessage =
					"Build: " + destinationName + " をコピーできません " +
					failedCopyPath.generic_string();
				return false;
			}
		}
	}

	if (!SaveSettingsFile(outputDirectory / kStandaloneManifestName, buildSettings)) {
		resultMessage = "Build: game.build を保存できません";
		return false;
	}

	resultMessage = "Build: ゲームを書き出しました " + WideToUtf8(gameExecutablePath.generic_wstring());

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

	if (fileError || !LoadSettingsFile(manifestPath, buildSettings)) {
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
	const std::filesystem::path& filePath,
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
	const std::filesystem::path& filePath,
	const EditorGameBuildSettings& buildSettings) {
	const std::filesystem::path settingsPath = filePath;
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
