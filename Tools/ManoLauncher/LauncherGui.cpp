#include "LauncherGui.h"

#include "LauncherExperience.h"
#include "PublisherGui.h"
#include "PublisherService.h"
#include "HttpDownload.h"
#include "Source/Engine/Core/EngineEnvironmentCheck.h"
#include "Source/Engine/Core/ProjectVersionManager.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
	constexpr UINT kUpdateAndOpenFinished = WM_APP + 11U;
	constexpr UINT kUpdateNotice = WM_APP + 12U;
	constexpr UINT kUpdateFound = WM_APP + 13U;
	constexpr UINT kJoinProjectFinished = WM_APP + 14U;

	// 画面の左に並べるページ。Unity Hubと同じ並びにして、迷わず行き来できるようにする。
	enum class HubPage : int {
		Projects = 0,
		Installs,
		Updates,
		Settings,
		Count,
	};

	constexpr int kPageCount = static_cast<int>(HubPage::Count);
	constexpr int kContentLeft = 208;
	// 文字が切れない余白を確保する。最小対応解像度でも操作部が横並びで収まる固定サイズ。
	constexpr int kContentWidth = 900;
	constexpr int kWindowWidth = 1160;
	constexpr int kWindowHeight = 750;

	struct UpdateAndOpenResult {
		bool succeeded = false;
		std::string projectId;
		std::string version;
		std::string message;
	};

	struct UpdateFound {
		std::string projectId;
		std::string text;
	};

	struct JoinProjectResult {
		bool succeeded = false;
		std::filesystem::path installedProjectRoot;
		std::string message;
	};

	enum ControlId : int {
		IdNavProjects = 100,
		IdNavInstalls,
		IdNavUpdates,
		IdNavSettings,

		IdProjects = 130,
		IdNewProjectEngine,
		IdOpen,
		IdNewProject,
		IdChangeProjectEngine,
		IdAddProject,
		IdInvite,
		IdCreateInvite,
		IdProjectLocation,
		IdOpenProjectFolder,
		IdRemoveProject,
		IdExportProjectZip,
		IdImportProjectZip,
		IdGitRemote,
		IdPrepareGit,
		IdUseGitFallback,
		IdUseRealtimeCollaboration,
		IdJoinCode,
		IdJoinByCode,
		IdShowJoinCode,
		IdPublishProjectToHub,

		IdEngines = 160,
		IdInstallEngine,
		IdCatalogHubHost,
		IdFetchCatalog,
		IdCatalogList,
		IdVerify,
		IdRepair,
		IdRollback,
		IdRemoveEngine,
		IdExportPortableEngine,
		IdInstallOfflineEngine,

		IdUpdatesList = 190,
		IdRecheckUpdates,
		IdUpdate,
		IdEngineUpdateChannel,
		IdEngineUpdateHubHost,
		IdSaveEngineUpdateSettings,
		IdCheckEngineUpdate,
		IdApplyEngineUpdate,

		IdDistributionServerStart = 220,
		IdDistributionServerStop,
		IdDistributionServerRestart,
		IdPublisher,

		IdStatus = 260,
	};

	struct WindowState {
		std::filesystem::path installRoot = LauncherExperience::DefaultInstallRoot();
		std::vector<RegisteredProject> projects;
		std::vector<std::string> updateProjectIds;
		std::vector<HWND> pageControls[kPageCount];
		HubPage page = HubPage::Projects;
		HWND projectsList = nullptr;
		HWND newProjectEngine = nullptr;
		HWND gitRemote = nullptr;
		HWND joinCode = nullptr;
		std::vector<std::string> newProjectEngineVersions;
		HWND enginesList = nullptr;
		std::vector<std::string> installedEngineVersions;
		HWND updatesList = nullptr;
		HWND engineUpdateChannel = nullptr;
		HWND engineUpdateHubHost = nullptr;
		HWND engineUpdateStatus = nullptr;
		EngineCatalogEntry availableEngineUpdate{};
		bool hasAvailableEngineUpdate = false;
		HWND catalogHubHost = nullptr;
		HWND catalogList = nullptr;
		std::vector<EngineCatalogEntry> catalog;
		HWND serverStatus = nullptr;
		HWND status = nullptr;
		bool busy = false;
	};

	std::wstring ToWide(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
		std::wstring result(count > 0 ? static_cast<std::size_t>(count) : 0U, L'\0');
		if (count > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	std::string ToUtf8Text(const std::wstring& value) {
		if (value.empty()) return {};
		const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string result(count > 0 ? static_cast<std::size_t>(count) : 0U, '\0');
		if (count > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
		return result;
	}

	std::string TrimAscii(std::string value) {
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.erase(value.begin());
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.pop_back();
		return value;
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	// HubのURLがTailscale MagicDNSを使う場合だけ、必要なTailnet suffixを取り出す。
	// LAN IPや一般公開URLではTailscaleへ一切触れず、従来どおり接続する。
	std::string TailscaleSuffixFromHub(const std::string& hubHost) {
		std::string authority = TrimAscii(hubHost);
		const std::size_t scheme = authority.find("://");
		if (scheme != std::string::npos) authority.erase(0U, scheme + 3U);
		const std::size_t path = authority.find_first_of("/?#");
		if (path != std::string::npos) authority.resize(path);
		const std::size_t userInfo = authority.rfind('@');
		if (userInfo != std::string::npos) authority.erase(0U, userInfo + 1U);
		if (!authority.empty() && authority.front() == '[') return {};
		const std::size_t port = authority.rfind(':');
		if (port != std::string::npos) authority.resize(port);

		authority = LowerAscii(TrimAscii(authority));
		while (!authority.empty() && authority.back() == '.') authority.pop_back();
		if (!authority.ends_with(".ts.net")) return {};
		const std::size_t firstDot = authority.find('.');
		return firstDot == std::string::npos ? std::string{} : authority.substr(firstDot + 1U);
	}

	std::filesystem::path FindTailscaleExecutable() {
		PWSTR programFiles = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFiles))) {
			const std::filesystem::path installed = std::filesystem::path(programFiles) / "Tailscale" / "tailscale.exe";
			CoTaskMemFree(programFiles);
			std::error_code error;
			if (std::filesystem::exists(installed, error)) return installed;
		}

		wchar_t found[32768]{};
		const DWORD length = SearchPathW(nullptr, L"tailscale.exe", nullptr, static_cast<DWORD>(std::size(found)), found, nullptr);
		return length > 0U && length < std::size(found) ? std::filesystem::path(found) : std::filesystem::path{};
	}

	bool RunTailscaleAndCapture(const std::filesystem::path& executable, const std::wstring& arguments,
		std::string& output, std::string& error) {
		wchar_t temporaryDirectory[32768]{};
		wchar_t temporaryFile[32768]{};
		if (GetTempPathW(static_cast<DWORD>(std::size(temporaryDirectory)), temporaryDirectory) == 0U ||
			GetTempFileNameW(temporaryDirectory, L"MTS", 0U, temporaryFile) == 0U) {
			error = "Tailscale確認用の一時ファイルを作成できません";
			return false;
		}

		const std::filesystem::path outputPath(temporaryFile);
		SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
		HANDLE outputHandle = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
			CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
		if (outputHandle == INVALID_HANDLE_VALUE) {
			DeleteFileW(outputPath.c_str());
			error = "Tailscale確認結果を保存できません";
			return false;
		}

		std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
		std::vector<wchar_t> writable(command.begin(), command.end());
		writable.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		startup.wShowWindow = SW_HIDE;
		startup.hStdOutput = outputHandle;
		startup.hStdError = outputHandle;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		CloseHandle(outputHandle);
		if (!created) {
			DeleteFileW(outputPath.c_str());
			error = "Tailscaleを起動できません（Windows " + std::to_string(GetLastError()) + "）";
			return false;
		}

		// status/switchが異常停止してもLauncherを永久に固めない。
		const DWORD waitResult = WaitForSingleObject(process.hProcess, 15000U);
		if (waitResult == WAIT_TIMEOUT) {
			TerminateProcess(process.hProcess, ERROR_TIMEOUT);
			WaitForSingleObject(process.hProcess, 2000U);
		}
		DWORD exitCode = 1U;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		std::ifstream file(outputPath, std::ios::binary);
		output.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		file.close();
		DeleteFileW(outputPath.c_str());
		if (waitResult == WAIT_TIMEOUT) {
			error = "Tailscaleの応答がありません";
			return false;
		}
		if (exitCode != 0U) {
			error = TrimAscii(output);
			if (error.empty()) error = "Tailscaleの確認に失敗しました（終了コード " + std::to_string(exitCode) + "）";
			return false;
		}
		return true;
	}

	std::string ReadJsonString(const std::string& json, const char* key) {
		const std::string quotedKey = std::string("\"") + key + "\"";
		const std::size_t keyPosition = json.find(quotedKey);
		if (keyPosition == std::string::npos) return {};
		const std::size_t colon = json.find(':', keyPosition + quotedKey.size());
		if (colon == std::string::npos) return {};
		const std::size_t beginQuote = json.find('"', colon + 1U);
		if (beginQuote == std::string::npos) return {};
		std::string value;
		for (std::size_t index = beginQuote + 1U; index < json.size(); ++index) {
			if (json[index] == '"') return value;
			if (json[index] == '\\' && index + 1U < json.size()) ++index;
			value.push_back(json[index]);
		}
		return {};
	}

	bool ReadCurrentTailscaleSuffix(const std::filesystem::path& executable,
		std::string& suffix, std::string& error) {
		std::string status;
		if (!RunTailscaleAndCapture(executable, L"status --json", status, error)) return false;
		suffix = LowerAscii(ReadJsonString(status, "MagicDNSSuffix"));
		while (!suffix.empty() && suffix.back() == '.') suffix.pop_back();
		if (suffix.empty()) {
			error = "現在のTailscaleネットワークを判定できません";
			return false;
		}
		return true;
	}

	struct TailscaleProfile {
		std::string id;
		bool active = false;
	};

	bool ReadTailscaleProfiles(const std::filesystem::path& executable,
		std::vector<TailscaleProfile>& profiles, std::string& error) {
		std::string listing;
		if (!RunTailscaleAndCapture(executable, L"switch --list", listing, error)) return false;
		std::istringstream lines(listing);
		std::string line;
		while (std::getline(lines, line)) {
			line = TrimAscii(line);
			if (line.empty() || line.starts_with("ID")) continue;
			std::istringstream columns(line);
			std::string id;
			columns >> id;
			const bool safeId = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char character) {
				return std::isalnum(character) != 0 || character == '-' || character == '_';
			});
			if (safeId) profiles.push_back({id, line.ends_with('*')});
		}
		if (profiles.empty()) {
			error = "このPCにTailscaleアカウントが登録されていません";
			return false;
		}
		return true;
	}

	bool SwitchToMatchingTailscaleProfile(const std::filesystem::path& executable,
		const std::string& expectedSuffix, std::string& switchedProfile, std::string& error) {
		// login直後は追加されたProfileが既にActiveになっていることがある。
		// その場合は不要な切替を行わず、そのまま成功とする。
		std::string currentSuffix;
		if (ReadCurrentTailscaleSuffix(executable, currentSuffix, error) && currentSuffix == expectedSuffix) {
			return true;
		}

		std::vector<TailscaleProfile> profiles;
		if (!ReadTailscaleProfiles(executable, profiles, error)) return false;
		const auto active = std::find_if(profiles.begin(), profiles.end(), [](const TailscaleProfile& profile) {
			return profile.active;
		});
		const std::string originalId = active == profiles.end() ? std::string{} : active->id;

		for (const TailscaleProfile& profile : profiles) {
			if (profile.active) continue;
			std::string ignored;
			if (!RunTailscaleAndCapture(executable, L"switch " + ToWide(profile.id), ignored, error)) continue;
			std::string suffix;
			if (ReadCurrentTailscaleSuffix(executable, suffix, error) && suffix == expectedSuffix) {
				switchedProfile = profile.id;
				return true;
			}
		}

		// 該当しなかった場合は、利用者が元々使っていたTailnetへ必ず戻す。
		if (!originalId.empty()) {
			std::string ignored;
			RunTailscaleAndCapture(executable, L"switch " + ToWide(originalId), ignored, error);
		}
		error = "参加先のTailscaleネットワークが、このPCにまだ登録されていません";
		return false;
	}

	bool PrepareTailscaleForHub(HWND window, const std::string& hubHost, std::string& result) {
		const std::string expectedSuffix = TailscaleSuffixFromHub(hubHost);
		if (expectedSuffix.empty()) return true;

		const std::filesystem::path executable = FindTailscaleExecutable();
		if (executable.empty()) {
			result = "この参加先にはTailscaleが必要ですが、Tailscaleが見つかりません。\r\n"
				"Tailscaleをインストールしてから、もう一度参加してください。";
			return false;
		}

		std::string currentSuffix;
		std::string detail;
		if (ReadCurrentTailscaleSuffix(executable, currentSuffix, detail) && currentSuffix == expectedSuffix) return true;

		const std::wstring confirmation = ToWide(
			"参加先はTailscaleネットワーク「" + expectedSuffix + "」を使用します。\r\n"
			"現在の接続先: " + (currentSuffix.empty() ? std::string("判定できません") : currentSuffix) + "\r\n\r\n"
			"登録済みアカウントから参加先を探して、自動的に切り替えます。\r\n"
			"現在のTailscale通信は切り替わります。続けますか？");
		if (MessageBoxW(window, confirmation.c_str(), L"Tailscale接続先の切替", MB_YESNO | MB_ICONQUESTION) != IDYES) {
			result = "Tailscale接続先の切替をキャンセルしました";
			return false;
		}

		std::string switchedProfile;
		if (SwitchToMatchingTailscaleProfile(executable, expectedSuffix, switchedProfile, detail)) {
			result = "Tailscaleを参加先へ切り替えました";
			return true;
		}

		const HINSTANCE launched = ShellExecuteW(window, L"open", executable.c_str(), L"login", nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(launched) <= 32) {
			result = detail + "\r\nTailscaleのブラウザ認証を開始できませんでした。";
			return false;
		}

		if (MessageBoxW(window,
			L"ブラウザでTailscaleの認証を完了し、参加先のネットワークを選んでください。\r\n\r\n"
			L"認証が完了したら［OK］を押すと、ManoEngineが自動的に接続先を探します。",
			L"Tailscale認証", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
			result = "Tailscale認証をキャンセルしました";
			return false;
		}

		if (!SwitchToMatchingTailscaleProfile(executable, expectedSuffix, switchedProfile, detail)) {
			result = detail + "\r\nブラウザで参加先Tailnetへの認証が完了しているか確認してください。";
			return false;
		}
		result = "Tailscaleを参加先へ切り替えました";
		return true;
	}

	std::wstring ReadText(HWND control) {
		const int length = GetWindowTextLengthW(control);
		if (length <= 0) return {};
		std::wstring value(static_cast<std::size_t>(length), L'\0');
		GetWindowTextW(control, value.data(), length + 1);
		return value;
	}

	const char* ChannelLabel(EngineUpdateChannel channel) {
		return channel == EngineUpdateChannel::Beta ? "ベータ版" : channel == EngineUpdateChannel::Dev ? "開発版" : "安定版";
	}

	std::string StatusLabel(const std::string& status) {
		if (status == "Ready") return "利用可能";
		if (status == "Needs Install") return "導入が必要";
		if (status == "Project Not Installed") return "プロジェクト未取得";
		if (status == "Environment Failed") return "環境確認失敗";
		return status;
	}

	void SetStatus(WindowState& state, const std::string& text) { SetWindowTextW(state.status, ToWide(text).c_str()); }

	void ShowResult(HWND window, WindowState& state, bool succeeded, const std::string& result) {
		SetStatus(state, result);
		MessageBoxW(window, ToWide(result).c_str(), succeeded ? L"ManoEngine Hub" : L"ManoEngine Hub - エラー",
			MB_OK | (succeeded ? MB_ICONINFORMATION : MB_ICONERROR));
	}

	bool OpenFolderInExplorer(HWND window, const std::filesystem::path& folder, std::string& error) {
		std::error_code filesystemError;
		if (folder.empty() || !std::filesystem::is_directory(folder, filesystemError)) {
			error = "プロジェクトフォルダーが見つかりません: " + ToUtf8Text(folder.wstring());
			return false;
		}
		const HINSTANCE opened = ShellExecuteW(window, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(opened) <= 32) {
			error = "Explorerでプロジェクトフォルダーを開けませんでした（Windows " +
				std::to_string(reinterpret_cast<INT_PTR>(opened)) + "）";
			return false;
		}
		return true;
	}

	std::filesystem::path PickInvite(HWND owner) {
		wchar_t path[32768]{};
		OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"ManoEngine 招待ファイル (*.mano-invite)\0*.mano-invite\0すべてのファイル\0*.*\0";
		dialog.lpstrFile = path; dialog.nMaxFile = _countof(path); dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		return GetOpenFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickInviteOutput(HWND owner, const std::string& projectName) {
		wchar_t path[32768]{};
		std::wstring defaultName = ToWide(projectName);
		for (wchar_t& character : defaultName) {
			if (std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos) character = L'_';
		}
		if (defaultName.empty()) defaultName = L"ManoEngine-Project";
		defaultName += L".mano-invite";
		wcsncpy_s(path, defaultName.c_str(), _TRUNCATE);

		OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"ManoEngine 招待ファイル (*.mano-invite)\0*.mano-invite\0すべてのファイル\0*.*\0";
		dialog.lpstrFile = path; dialog.nMaxFile = _countof(path); dialog.lpstrDefExt = L"mano-invite";
		dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		return GetSaveFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickZip(HWND owner) {
		wchar_t path[32768]{};
		OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"ZIPファイル (*.zip)\0*.zip\0すべてのファイル\0*.*\0";
		dialog.lpstrFile = path; dialog.nMaxFile = _countof(path);
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		return GetOpenFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickZipOutput(HWND owner, const std::string& suggestedName) {
		wchar_t path[32768]{};
		std::wstring defaultName = ToWide(suggestedName);
		for (wchar_t& character : defaultName) {
			if (std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos) character = L'_';
		}
		if (defaultName.empty()) defaultName = L"ManoEngine-package";
		if (!defaultName.ends_with(L".zip")) defaultName += L".zip";
		wcsncpy_s(path, defaultName.c_str(), _TRUNCATE);
		OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"ZIPファイル (*.zip)\0*.zip\0すべてのファイル\0*.*\0";
		dialog.lpstrFile = path; dialog.nMaxFile = _countof(path); dialog.lpstrDefExt = L"zip";
		dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		return GetSaveFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickExecutable(HWND owner) {
		wchar_t path[32768]{};
		OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"実行ファイル (*.exe)\0*.exe\0すべてのファイル\0*.*\0";
		dialog.lpstrFile = path; dialog.nMaxFile = _countof(path); dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		return GetOpenFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickFolder(HWND owner, const wchar_t* title) {
		BROWSEINFOW info{}; info.hwndOwner = owner; info.lpszTitle = title;
		info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info); if (!item) return {};
		wchar_t path[32768]{}; const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
		CoTaskMemFree(item); return ok ? std::filesystem::path(path) : std::filesystem::path{};
	}

	void Refresh(WindowState& state) {
		std::string error; LauncherExperience::LoadProjects(state.installRoot, state.projects, error);
		// 旧Projectにも不足分だけを補う。既存の.gitignoreや起動Scriptは上書きしない。
		for (const auto& project : state.projects) {
			std::string recoveryError;
			LauncherExperience::EnsureProjectRecoveryFiles(project.projectRoot, recoveryError);
		}
		SendMessageW(state.projectsList, LB_RESETCONTENT, 0, 0);
		for (const auto& project : state.projects) {
			const std::wstring text = ToWide(project.projectName + "    エンジン " + project.requiredEngineVersion + " " +
				ChannelLabel(project.updateChannel) + "    状態: " + StatusLabel(project.status));
			SendMessageW(state.projectsList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
		}
		if (!state.projects.empty()) SendMessageW(state.projectsList, LB_SETCURSEL, 0, 0);
		const std::vector<std::string> installedEngines = LauncherExperience::ListInstalledEngines(state.installRoot);
		SendMessageW(state.enginesList, LB_RESETCONTENT, 0, 0);
		SendMessageW(state.newProjectEngine, CB_RESETCONTENT, 0, 0);
		state.newProjectEngineVersions.clear();

		LauncherState launcherState{};
		std::string launcherStateError;
		LauncherUpdate::LoadState(state.installRoot, launcherState, launcherStateError);
		state.installedEngineVersions.clear();
		LRESULT selectedProjectEngine = CB_ERR;
		LRESULT newestProjectEngine = CB_ERR;
		EngineVersion newestVersion{};
		for (std::size_t index = 0; index < installedEngines.size(); ++index) {
			const auto& engine = installedEngines[index];
			const std::size_t separator = engine.find_first_of(" \t");
			const std::string versionText = engine.substr(0U, separator);
			std::string usage;
			if (versionText == launcherState.currentVersion) usage = "現在使用中";
			else if (versionText == launcherState.previousVersion) usage = "1世代前・ロールバック用";
			for (const RegisteredProject& project : state.projects) {
				if (project.requiredEngineVersion != versionText) continue;
				if (!usage.empty()) usage += " / ";
				usage += "Project: " + project.projectName;
			}
			const std::wstring installText = ToWide(engine + "    [" + (usage.empty() ? "未使用・削除可能" : usage) + "]");
			const std::wstring comboText = ToWide(engine);
			SendMessageW(state.enginesList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(installText.c_str()));
			SendMessageW(state.newProjectEngine, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(comboText.c_str()));
			state.installedEngineVersions.push_back(versionText);
			state.newProjectEngineVersions.push_back(versionText);
			if (versionText == launcherState.currentVersion) selectedProjectEngine = static_cast<LRESULT>(index);
			EngineVersion parsedVersion{};
			if (EngineVersion::TryParse(versionText, parsedVersion) &&
				(newestProjectEngine == CB_ERR || newestVersion < parsedVersion)) {
				newestVersion = parsedVersion;
				newestProjectEngine = static_cast<LRESULT>(index);
			}
		}
		// 現在使用中のEngineを初期選択する。状態が無い場合は最も新しい導入済み版を選ぶ。
		if (selectedProjectEngine == CB_ERR) selectedProjectEngine = newestProjectEngine;
		if (selectedProjectEngine != CB_ERR) SendMessageW(state.newProjectEngine, CB_SETCURSEL, selectedProjectEngine, 0);
		EnableWindow(state.newProjectEngine, !installedEngines.empty());
		if (!error.empty()) SetStatus(state, error);
		else {
			const auto notice = LauncherUpdate::GetLauncherStateDirectory(state.installRoot) / "launcher-update.notice";
			std::error_code ec;
			if (std::filesystem::exists(notice, ec)) SetStatus(state, "ランチャーの更新があります");
		}
	}

	RegisteredProject* Selected(WindowState& state) {
		const LRESULT selected = SendMessageW(state.projectsList, LB_GETCURSEL, 0, 0);
		return selected == LB_ERR || static_cast<std::size_t>(selected) >= state.projects.size() ? nullptr : &state.projects[static_cast<std::size_t>(selected)];
	}

	void ShowPage(WindowState& state, HubPage page) {
		state.page = page;
		for (int index = 0; index < kPageCount; ++index) {
			const int command = index == static_cast<int>(page) ? SW_SHOW : SW_HIDE;
			for (HWND control : state.pageControls[index]) ShowWindow(control, command);
		}
	}

	// ---------------------------------------------------------------- Engineカタログ (インストールページ)

	std::filesystem::path CatalogHubHistoryPath(const WindowState& state) {
		return LauncherUpdate::GetLauncherStateDirectory(state.installRoot) / "install-source.txt";
	}

	// 「どのHubからEngineを取るか」は、Unity Hubのようにここで毎回入力させたくない。
	// 1. 前回インストールに使ったHub 2. 選択中Projectの配布元Hub 3. このPCが配布者として使っているHub
	// の優先順で、初回だけ埋める。
	std::wstring DefaultCatalogHubHost(WindowState& state) {
		std::ifstream history(CatalogHubHistoryPath(state), std::ios::binary);
		std::string saved;
		if (std::getline(history, saved) && !saved.empty()) return ToWide(saved);

		if (const RegisteredProject* project = Selected(state); project != nullptr && !project->hubHost.empty()) {
			return ToWide(project->hubHost);
		}

		PublisherSettings settings{}; std::string settingsError;
		if (PublisherService::LoadSettings(state.installRoot, settings, settingsError) && !settings.publicHubAddress.empty()) {
			return ToWide(settings.publicHubAddress);
		}

		return {};
	}

	void RememberCatalogHubHost(WindowState& state, const std::string& hubHost) {
		const auto historyPath = CatalogHubHistoryPath(state);
		std::ifstream previousFile(historyPath, std::ios::binary);
		std::string previousHub;
		std::getline(previousFile, previousHub);

		std::ofstream file(historyPath, std::ios::binary | std::ios::trunc);
		if (file.is_open()) file << hubHost;

		// 接続確認できた新Hubへ切り替えた場合、同じ旧Hubを参照するProjectも移行する。
		bool changed = false;
		for (RegisteredProject& project : state.projects) {
			if (project.hubHost.empty() || (!previousHub.empty() && project.hubHost == previousHub)) {
				project.hubHost = hubHost;
				changed = true;
			}
		}
		if (changed) {
			std::string saveError;
			LauncherExperience::SaveProjects(state.installRoot, state.projects, saveError);
		}
	}

	EngineUpdateChannel SelectedEngineUpdateChannel(const WindowState& state) {
		const LRESULT selected = SendMessageW(state.engineUpdateChannel, CB_GETCURSEL, 0, 0);
		if (selected == 1) return EngineUpdateChannel::Beta;
		if (selected == 2) return EngineUpdateChannel::Dev;
		return EngineUpdateChannel::Stable;
	}

	void LoadEngineUpdateControls(WindowState& state) {
		LauncherState launcher{};
		std::string error;
		const bool loaded = LauncherUpdate::LoadState(state.installRoot, launcher, error);
		const LRESULT channelIndex = !loaded || launcher.channel == EngineUpdateChannel::Stable
			? 0 : launcher.channel == EngineUpdateChannel::Beta ? 1 : 2;
		SendMessageW(state.engineUpdateChannel, CB_SETCURSEL, channelIndex, 0);
		SetWindowTextW(state.engineUpdateHubHost, DefaultCatalogHubHost(state).c_str());
		const std::string currentVersion = loaded ? launcher.currentVersion : "未導入";
		SetWindowTextW(state.engineUpdateStatus,
			ToWide("現在のEngine: " + currentVersion + " / 更新チャンネル: " +
				GetEngineUpdateChannelText(loaded ? launcher.channel : EngineUpdateChannel::Stable)).c_str());
	}

	bool SaveEngineUpdateSettings(WindowState& state, std::string& result) {
		const std::string hubHost = ToUtf8Text(ReadText(state.engineUpdateHubHost));
		if (hubHost.empty()) { result = "Engine更新元Hubを入力してください"; return false; }
		if (!LauncherUpdate::SetChannel(state.installRoot, SelectedEngineUpdateChannel(state), result)) return false;
		std::ofstream history(CatalogHubHistoryPath(state), std::ios::binary | std::ios::trunc);
		if (!history) { result = "Engine更新元Hubを保存できません"; return false; }
		history << hubHost;
		SetWindowTextW(state.catalogHubHost, ToWide(hubHost).c_str());
		result = "Engine更新設定を保存しました";
		return true;
	}

	bool CheckEngineUpdate(WindowState& state, std::string& result) {
		const std::string hubHost = ToUtf8Text(ReadText(state.engineUpdateHubHost));
		if (hubHost.empty()) { result = "Engine更新元Hubを入力してください"; return false; }
		std::vector<EngineCatalogEntry> catalog;
		if (!LauncherExperience::FetchEngineCatalog(hubHost, state.installRoot, catalog, result)) return false;
		const EngineUpdateChannel channel = SelectedEngineUpdateChannel(state);
		const auto available = std::find_if(catalog.begin(), catalog.end(), [channel](const EngineCatalogEntry& entry) {
			return entry.channel == channel;
		});
		if (available == catalog.end()) {
			result = std::string(GetEngineUpdateChannelText(channel)) + "チャンネルのEngineが公開されていません";
			return false;
		}
		LauncherState launcher{};
		if (!LauncherUpdate::LoadState(state.installRoot, launcher, result)) return false;
		EngineVersion current{};
		if (!EngineVersion::TryParse(launcher.currentVersion, current)) {
			result = "現在のEngineバージョンを読み取れません: " + launcher.currentVersion;
			return false;
		}
		state.availableEngineUpdate = *available;
		state.hasAvailableEngineUpdate = current < available->version;
		RememberCatalogHubHost(state, hubHost);
		std::string saveResult;
		LauncherUpdate::SetChannel(state.installRoot, channel, saveResult);
		result = state.hasAvailableEngineUpdate
			? "Engine更新あり: " + launcher.currentVersion + " → " + available->version.ToString()
			: "Engineは最新です: " + launcher.currentVersion;
		SetWindowTextW(state.engineUpdateStatus, ToWide(result).c_str());
		return true;
	}

	void RefreshCatalogList(WindowState& state) {
		SendMessageW(state.catalogList, LB_RESETCONTENT, 0, 0);
		for (const auto& entry : state.catalog) {
			const char* channelName = entry.channel == EngineUpdateChannel::Beta ? "ベータ版"
				: entry.channel == EngineUpdateChannel::Dev ? "開発版" : "安定版";
			const std::wstring text = ToWide(std::string(channelName) + "    " + entry.version.ToString() +
				(entry.offlineArchivePath.empty() ? "" : "    [配布ZIP]") +
				(entry.isInstalled ? "    [導入済み]" : "    [未導入]"));
			SendMessageW(state.catalogList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
		}
		if (!state.catalog.empty()) SendMessageW(state.catalogList, LB_SETCURSEL, 0, 0);
	}

	void RefreshDistributionStatus(WindowState& state) {
		PublisherSettings settings{}; std::string error;
		if (!PublisherService::LoadSettings(state.installRoot, settings, error)) {
			SetWindowTextW(state.serverStatus, L"設定を読み込めません"); return;
		}
		const auto distribution = PublisherServerService::GetStatus(settings, state.installRoot);
		SetWindowTextW(state.serverStatus, ToWide("配布Hub: " + distribution.detail).c_str());
	}

	// 既にあるプロジェクトフォルダーを一覧へ登録する。
	bool AddExistingProject(const std::filesystem::path& projectRoot, const std::filesystem::path& installRoot,
		std::vector<RegisteredProject>& projects, std::string& result) {
		ProjectVersionSettings settings{};
		if (!ProjectVersionManager::Load(projectRoot, settings, result)) {
			result = "ManoEngineのプロジェクトではありません: " + result; return false;
		}
		for (const auto& existing : projects) {
			if (existing.projectRoot == projectRoot) { result = "既に登録されています"; return false; }
		}

		RegisteredProject project{};
		project.projectId = projectRoot.filename().string() + "-" + std::to_string(GetTickCount64());
		project.projectName = projectRoot.filename().string();
		project.updateChannel = settings.updateChannel;
		project.requiredEngineVersion = settings.requiredEngineVersion.ToString();
		project.projectRoot = projectRoot;
		std::error_code ec;
		project.status = std::filesystem::exists(LauncherUpdate::GetEnginesDirectory(installRoot) /
			project.requiredEngineVersion / "CG2.exe", ec) ? "Ready" : "Needs Install";
		projects.push_back(project);

		if (!LauncherExperience::SaveProjects(installRoot, projects, result)) return false;
		result = "プロジェクトを追加しました: " + project.projectName;
		return true;
	}

	void StartUpdateAndOpen(HWND window, WindowState& state, const RegisteredProject& project,
		const std::filesystem::path& manifestPath, const EngineUpdateManifest& manifest) {
		if (state.busy) return;
		state.busy = true; SetStatus(state, "ダウンロード・検証・インストール中...");
		const auto installRoot = state.installRoot; const auto projectRoot = project.projectRoot;
		const std::string projectId = project.projectId; const std::string version = manifest.version.ToString();
		std::thread([window, installRoot, projectRoot, manifestPath, projectId, version]() {
			auto* completed = new UpdateAndOpenResult{}; completed->projectId = projectId; completed->version = version;
			auto progress = [window](const std::string& value) { PostMessageW(window, kUpdateNotice, 0, reinterpret_cast<LPARAM>(new std::string(value))); };
			completed->succeeded = LauncherUpdate::Apply(manifestPath, installRoot, projectRoot, false, completed->message, progress);
			PostMessageW(window, kUpdateAndOpenFinished, 0, reinterpret_cast<LPARAM>(completed));
		}).detach();
	}

	// Project SnapshotとEngineは容量次第で取得に時間がかかるため、Window Procedureを塞がない。
	// 進捗と完了だけをWindow MessageでUI Threadへ返し、Window操作と再描画を継続する。
	void StartJoinProjectByCode(HWND window, WindowState& state,
		const std::string& joinCode, const std::string& hubHost) {
		if (state.busy) {
			return;
		}

		state.busy = true;
		SetStatus(state, "参加コードを照合し、エンジンとプロジェクトを取得中...");
		const std::filesystem::path installRoot = state.installRoot;

		std::thread([window, joinCode, hubHost, installRoot]() {
			const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			auto* completed = new JoinProjectResult{};
			auto progress = [window](const std::string& value) {
				PostMessageW(
					window,
					kUpdateNotice,
					0,
					reinterpret_cast<LPARAM>(new std::string(value)));
			};
			std::string tailscaleResult;
			if (!PrepareTailscaleForHub(window, hubHost, tailscaleResult)) {
				completed->message = tailscaleResult;
				PostMessageW(window, kJoinProjectFinished, 0, reinterpret_cast<LPARAM>(completed));
				if (SUCCEEDED(comResult)) CoUninitialize();
				return;
			}
			if (!tailscaleResult.empty()) progress(tailscaleResult + "。参加処理を続行します...");

			completed->succeeded = LauncherExperience::JoinProjectByCode(
				joinCode,
				hubHost,
				installRoot,
				completed->installedProjectRoot,
				completed->message,
				progress);
			PostMessageW(
				window,
				kJoinProjectFinished,
				0,
				reinterpret_cast<LPARAM>(completed));
			if (SUCCEEDED(comResult)) CoUninitialize();
		}).detach();
	}

	void CheckForUpdates(HWND window, const WindowState& state) {
		const auto projects = state.projects; const auto installRoot = state.installRoot;
		std::thread([window, projects, installRoot]() {
			if (projects.empty()) {
				auto* found = new UpdateFound{"", "更新対象のProjectが未登録です。Project画面から追加してください。"};
				PostMessageW(window, kUpdateFound, 0, reinterpret_cast<LPARAM>(found));
				return;
			}
			for (const auto& project : projects) {
				ProjectVersionSettings projectVersion{}; std::string metadataError;
				if (!project.projectRoot.empty() && ProjectVersionManager::Load(project.projectRoot, projectVersion, metadataError) &&
					projectVersion.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned) continue;
				ManoInvite invite{1U, project.projectId, project.projectName, project.hubHost, project.updateChannel, {}, project.projectEndpoint, {}};
				std::filesystem::path manifestPath; EngineUpdateManifest available{}; std::string error; EngineVersion installed{};
				if (EngineVersion::TryParse(project.requiredEngineVersion, installed) && LauncherExperience::ResolveManifest(invite, installRoot, manifestPath, available, error) && installed < available.version) {
					auto* found = new UpdateFound{ project.projectId,
						project.projectName + "    " + installed.ToString() + " → " + available.version.ToString() };
					PostMessageW(window, kUpdateFound, 0, reinterpret_cast<LPARAM>(found));
				}
			}
		}).detach();
	}

	void Setup(HWND window, WindowState& state, const std::filesystem::path& invitePath) {
		ManoInvite invite{}; std::string error;
		if (!LauncherExperience::LoadInvite(invitePath, invite, error)) { ShowResult(window, state, false, error); return; }
		const std::wstring summary = ToWide(invite.projectName + "\n\n配布サーバー:\n" + invite.hubHost + "\n\n更新種別:\n" +
			ChannelLabel(invite.updateChannel) + "\n\n必要なエンジン:\n" +
			(invite.requiredEngineVersion.empty() ? "配布元の指定に従う" : invite.requiredEngineVersion) + "\n\n参加とセットアップを開始しますか？");
		if (MessageBoxW(window, summary.c_str(), L"ManoEngine プロジェクト招待", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
		const auto projectRoot = PickFolder(window, L"取得済みのプロジェクトフォルダーを選択してください。未取得ならキャンセルできます。");
		SetStatus(state, "配布サーバー確認・エンジン導入・検証中..."); UpdateWindow(window);
		const bool ok = LauncherExperience::SetupInvite(invitePath, state.installRoot, projectRoot, error);

		Refresh(state); ShowResult(window, state, ok, error);
	}

	std::filesystem::path EnsureLauncherInstalled(const std::filesystem::path& installRoot) {
		wchar_t executable[32768]{}; if (GetModuleFileNameW(nullptr, executable, _countof(executable)) == 0U) return {};
		const std::filesystem::path source(executable);
		const auto destination = installRoot / "Launcher" / "ManoLauncher.exe";
		std::error_code ec; std::filesystem::create_directories(destination.parent_path(), ec);
		const bool same = std::filesystem::exists(destination, ec) && std::filesystem::equivalent(source, destination, ec);
		if (!same) std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
		return !ec && std::filesystem::exists(destination) ? destination : source;
	}

	void RegisterInviteAssociation(const std::filesystem::path& launcherPath) {
		const std::wstring executable = launcherPath.wstring(); if (executable.empty()) return;
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.mano-invite", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
			const wchar_t type[] = L"ManoEngine.ProjectInvite"; RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(type), sizeof(type)); RegCloseKey(key);
		}
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\ManoEngine.ProjectInvite\\shell\\open\\command", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
			const std::wstring command = L"\"" + executable + L"\" \"%1\"";
			RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1U) * sizeof(wchar_t))); RegCloseKey(key);
		}
	}

	// ---------------------------------------------------------------- 画面作成

	HWND Add(WindowState& state, HubPage page, HWND control) {
		state.pageControls[static_cast<int>(page)].push_back(control);
		return control;
	}

	HWND MakeLabel(HWND window, WindowState& state, HubPage page, const wchar_t* text, int x, int y, int width, int height = 22) {
		return Add(state, page, CreateWindowW(L"STATIC", text, WS_CHILD | SS_LEFT, x, y, width, height, window, nullptr, nullptr, nullptr));
	}

	HWND MakeButton(HWND window, WindowState& state, HubPage page, const wchar_t* text, int x, int y, int width, int id, int height = 32) {
		return Add(state, page, CreateWindowW(L"BUTTON", text, WS_CHILD, x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr));
	}

	HWND MakeEdit(HWND window, WindowState& state, HubPage page, int x, int y, int width, int id) {
		return Add(state, page, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL,
			x, y, width, 24, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr));
	}

	void BuildProjectsPage(HWND window, WindowState& state) {
		MakeLabel(window, state, HubPage::Projects, L"プロジェクト", kContentLeft, 20, 400, 24);
		state.projectsList = Add(state, HubPage::Projects, CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
			WS_CHILD | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, kContentLeft, 50, kContentWidth, 250, window, reinterpret_cast<HMENU>(IdProjects), nullptr, nullptr));

		MakeLabel(window, state, HubPage::Projects, L"使用するEngine", kContentLeft, 319, 120, 24);
		state.newProjectEngine = Add(state, HubPage::Projects, CreateWindowW(L"COMBOBOX", nullptr,
			WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, kContentLeft + 125, 314, 180, 180, window,
			reinterpret_cast<HMENU>(IdNewProjectEngine), nullptr, nullptr));
		MakeButton(window, state, HubPage::Projects, L"このEngineで新規作成", kContentLeft + 315, 312, 250, IdNewProject, 32);
		MakeButton(window, state, HubPage::Projects, L"選択ProjectをこのEngineへ変更",
			kContentLeft + 575, 312, 325, IdChangeProjectEngine, 32);

		MakeButton(window, state, HubPage::Projects, L"開く", kContentLeft, 362, 110, IdOpen);
		MakeButton(window, state, HubPage::Projects, L"プロジェクトを追加", kContentLeft + 118, 362, 160, IdAddProject);
		MakeButton(window, state, HubPage::Projects, L"招待を開く", kContentLeft + 286, 362, 120, IdInvite);
		MakeButton(window, state, HubPage::Projects, L"場所を変更", kContentLeft + 414, 362, 130, IdProjectLocation);
		MakeButton(window, state, HubPage::Projects, L"保存先を開く", kContentLeft + 552, 362, 150, IdOpenProjectFolder);
		MakeLabel(window, state, HubPage::Projects, L"参加コード", kContentLeft, 410, 100, 24);
		state.joinCode = MakeEdit(window, state, HubPage::Projects, kContentLeft + 105, 406, 200, IdJoinCode);
		MakeButton(window, state, HubPage::Projects, L"コードで参加", kContentLeft + 315, 404, 160, IdJoinByCode);
		MakeButton(window, state, HubPage::Projects, L"選択Projectのコードを表示",
			kContentLeft + 485, 404, 250, IdShowJoinCode);
		MakeLabel(window, state, HubPage::Projects,
			L"コードを入れるだけで、エンジン・プロジェクト・共同制作の接続先まで揃います。",
			kContentLeft, 442, kContentWidth, 24);

		MakeButton(window, state, HubPage::Projects, L"一覧から外す", kContentLeft, 472, 150, IdRemoveProject);
		MakeButton(window, state, HubPage::Projects, L"Hubへ公開", kContentLeft + 158, 472, 150, IdPublishProjectToHub);
		MakeButton(window, state, HubPage::Projects, L"招待を作成", kContentLeft + 316, 472, 140, IdCreateInvite);
		MakeLabel(window, state, HubPage::Projects,
			L"招待ファイル (.mano-invite) は、ドラッグ＆ドロップでも開けます。", kContentLeft + 466, 478, 430, 40);

		MakeButton(window, state, HubPage::Projects, L"ProjectをZIP出力", kContentLeft, 518, 170, IdExportProjectZip, 30);
		MakeButton(window, state, HubPage::Projects, L"Project ZIPを取込", kContentLeft + 180, 518, 170, IdImportProjectZip, 30);
		MakeLabel(window, state, HubPage::Projects, L"Git remote (任意)", kContentLeft, 560, 140, 24);
		state.gitRemote = MakeEdit(window, state, HubPage::Projects, kContentLeft + 145, 556, 500, IdGitRemote);
		MakeButton(window, state, HubPage::Projects, L"Git作成・接続", kContentLeft + 655, 554, 170, IdPrepareGit, 30);
		MakeButton(window, state, HubPage::Projects, L"Git共有へ切替", kContentLeft, 596, 170, IdUseGitFallback, 30);
		MakeButton(window, state, HubPage::Projects, L"リアルタイム共有へ戻す", kContentLeft + 180, 596, 210, IdUseRealtimeCollaboration, 30);
		MakeLabel(window, state, HubPage::Projects,
			L"Git切替では自動接続だけを切替えます。commit / pull / pushは勝手に実行しません。", kContentLeft, 636, kContentWidth, 32);
	}

	void BuildInstallsPage(HWND window, WindowState& state) {
		MakeLabel(window, state, HubPage::Installs, L"導入済みエンジン", kContentLeft, 20, 400, 24);
		state.enginesList = Add(state, HubPage::Installs, CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
			WS_CHILD | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, kContentLeft, 50, kContentWidth, 130, window, reinterpret_cast<HMENU>(IdEngines), nullptr, nullptr));

		MakeLabel(window, state, HubPage::Installs, L"Hubサーバー", kContentLeft, 196, 200);
		state.catalogHubHost = MakeEdit(window, state, HubPage::Installs, kContentLeft + 100, 192, 650, IdCatalogHubHost);
		MakeButton(window, state, HubPage::Installs, L"バージョンを取得", kContentLeft + 758, 190, 132, IdFetchCatalog, 28);

		MakeLabel(window, state, HubPage::Installs, L"導入できるバージョン", kContentLeft, 228, 400, 24);
		state.catalogList = Add(state, HubPage::Installs, CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
			WS_CHILD | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, kContentLeft, 252, kContentWidth, 100, window, reinterpret_cast<HMENU>(IdCatalogList), nullptr, nullptr));
		MakeButton(window, state, HubPage::Installs, L"選んだバージョンをインストール", kContentLeft, 358, 270, IdInstallEngine, 28);

		MakeButton(window, state, HubPage::Installs, L"検証", kContentLeft + 278, 358, 90, IdVerify, 28);
		MakeButton(window, state, HubPage::Installs, L"修復", kContentLeft + 376, 358, 90, IdRepair, 28);
		MakeButton(window, state, HubPage::Installs, L"以前の版に戻す", kContentLeft + 474, 358, 150, IdRollback, 28);
		MakeButton(window, state, HubPage::Installs, L"未使用の版を削除", kContentLeft + 632, 358, 170, IdRemoveEngine, 28);
		MakeLabel(window, state, HubPage::Installs,
			L"現在使用中・1世代前・登録Projectが使用中の版は削除できません。\r\nそれ以外は一覧で選び「未使用の版を削除」から整理できます。", kContentLeft, 396, kContentWidth, 40);
		MakeButton(window, state, HubPage::Installs, L"Portable Engine ZIPを出力", kContentLeft, 450, 240, IdExportPortableEngine, 32);
		MakeButton(window, state, HubPage::Installs, L"配布ZIPを一覧へ追加", kContentLeft + 250, 450, 210, IdInstallOfflineEngine, 32);
		MakeLabel(window, state, HubPage::Installs,
			L"受け取ったZIPを一覧へ追加し、通常のインストール操作で導入できます。導入時にHashを確認します。", kContentLeft, 492, kContentWidth, 32);
	}

	void BuildUpdatesPage(HWND window, WindowState& state) {
		MakeLabel(window, state, HubPage::Updates, L"Engine本体の更新", kContentLeft, 20, 400, 24);
		MakeLabel(window, state, HubPage::Updates, L"チャンネル", kContentLeft, 56, 100, 24);
		state.engineUpdateChannel = Add(state, HubPage::Updates, CreateWindowW(L"COMBOBOX", nullptr,
			WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, kContentLeft + 110, 52, 160, 120, window,
			reinterpret_cast<HMENU>(IdEngineUpdateChannel), nullptr, nullptr));
		for (const wchar_t* channel : {L"安定版", L"ベータ版", L"開発版"}) {
			SendMessageW(state.engineUpdateChannel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(channel));
		}
		MakeLabel(window, state, HubPage::Updates, L"更新元Hub", kContentLeft, 90, 100, 24);
		state.engineUpdateHubHost = MakeEdit(
			window, state, HubPage::Updates, kContentLeft + 110, 86, 650, IdEngineUpdateHubHost);
		MakeButton(window, state, HubPage::Updates, L"設定保存", kContentLeft, 122, 110, IdSaveEngineUpdateSettings);
		MakeButton(window, state, HubPage::Updates, L"更新を確認", kContentLeft + 118, 122, 130, IdCheckEngineUpdate);
		MakeButton(window, state, HubPage::Updates, L"Engineを更新", kContentLeft + 256, 122, 140, IdApplyEngineUpdate);
		state.engineUpdateStatus = MakeLabel(
			window, state, HubPage::Updates, L"現在のEngineを確認しています", kContentLeft, 164, kContentWidth, 28);

		MakeLabel(window, state, HubPage::Updates, L"ProjectごとのEngine更新", kContentLeft, 208, 400, 24);
		state.updatesList = Add(state, HubPage::Updates, CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
			WS_CHILD | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, kContentLeft, 238, kContentWidth, 180, window, reinterpret_cast<HMENU>(IdUpdatesList), nullptr, nullptr));
		MakeButton(window, state, HubPage::Updates, L"Project更新を再確認", kContentLeft, 430, 180, IdRecheckUpdates);
		MakeButton(window, state, HubPage::Updates, L"選んだProject更新を適用", kContentLeft + 188, 430, 220, IdUpdate);
		MakeLabel(window, state, HubPage::Updates,
			L"更新は登録済み Project ごとに表示します。Project が未登録なら、\r\nProject 画面から追加してください。固定中の Project は更新対象外です。", kContentLeft, 474, kContentWidth, 40);
	}

	void BuildSettingsPage(HWND window, WindowState& state) {
		MakeLabel(window, state, HubPage::Settings, L"エンジン配布・公開", kContentLeft, 20, kContentWidth, 24);
		MakeLabel(window, state, HubPage::Settings,
			L"共同制作ネットワークの設定は、エディタの『TEAM - 共同制作』へ移動しました。\r\nこのランチャーでは、エンジンとプロジェクトの配布Hubだけを管理します。",
			kContentLeft, 56, kContentWidth, 48);
		MakeButton(window, state, HubPage::Settings, L"配布・公開の設定", kContentLeft, 122, 180, IdPublisher);
		MakeButton(window, state, HubPage::Settings, L"配布Hubを開始", kContentLeft + 190, 122, 140, IdDistributionServerStart);
		MakeButton(window, state, HubPage::Settings, L"停止", kContentLeft + 340, 122, 90, IdDistributionServerStop);
		MakeButton(window, state, HubPage::Settings, L"再起動", kContentLeft + 440, 122, 100, IdDistributionServerRestart);
		state.serverStatus = MakeLabel(window, state, HubPage::Settings, L"配布Hub: 未確認", kContentLeft, 170, kContentWidth, 32);
		MakeLabel(window, state, HubPage::Settings,
			L"エンジンを配る人だけが使う設定です。ゲーム制作だけなら不要です。",
			kContentLeft, 210, 680, 28);
	}

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_CREATE) {
			state = reinterpret_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

			CreateWindowW(L"BUTTON", L"プロジェクト", WS_CHILD | WS_VISIBLE, 16, 20, 176, 40, window, reinterpret_cast<HMENU>(IdNavProjects), nullptr, nullptr);
			CreateWindowW(L"BUTTON", L"インストール", WS_CHILD | WS_VISIBLE, 16, 66, 176, 40, window, reinterpret_cast<HMENU>(IdNavInstalls), nullptr, nullptr);
			CreateWindowW(L"BUTTON", L"更新", WS_CHILD | WS_VISIBLE, 16, 112, 176, 40, window, reinterpret_cast<HMENU>(IdNavUpdates), nullptr, nullptr);
			CreateWindowW(L"BUTTON", L"設定", WS_CHILD | WS_VISIBLE, 16, 158, 176, 40, window, reinterpret_cast<HMENU>(IdNavSettings), nullptr, nullptr);
			CreateWindowW(L"STATIC", ToWide(std::string("ManoEngine Hub ") + kManoLauncherVersion).c_str(),
				WS_CHILD | WS_VISIBLE, 16, 214, 176, 40, window, nullptr, nullptr, nullptr);

			BuildProjectsPage(window, *state);
			BuildInstallsPage(window, *state);
			BuildUpdatesPage(window, *state);
			BuildSettingsPage(window, *state);
			LoadEngineUpdateControls(*state);

			// 実行結果は長くなり得る。折り返して読める編集欄にして、表示だけで情報を失わせない。
			state->status = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"準備完了",
				WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
				16, 620, kWindowWidth - 48, 70, window, reinterpret_cast<HMENU>(IdStatus), nullptr, nullptr);

			ShowPage(*state, HubPage::Projects);
			DragAcceptFiles(window, TRUE); Refresh(*state); CheckForUpdates(window, *state); return 0;
		}
		if (!state) return DefWindowProcW(window, message, wParam, lParam);
		if (message == kUpdateNotice) {
			std::unique_ptr<std::string> notice(reinterpret_cast<std::string*>(lParam)); SetStatus(*state, *notice); return 0;
		}
		if (message == kUpdateFound) {
			std::unique_ptr<UpdateFound> found(reinterpret_cast<UpdateFound*>(lParam));
			state->updateProjectIds.push_back(found->projectId);
			SendMessageW(state->updatesList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWide(found->text).c_str()));
			SendMessageW(state->updatesList, LB_SETCURSEL, 0, 0);
			SetStatus(*state, "更新があります: " + found->text); return 0;
		}
		if (message == kUpdateAndOpenFinished) {
			std::unique_ptr<UpdateAndOpenResult> completed(reinterpret_cast<UpdateAndOpenResult*>(lParam)); state->busy = false;
			if (!completed->succeeded) { ShowResult(window, *state, false, completed->message + "\n旧バージョンは維持されています。再試行できます。"); return 0; }
			for (auto& item : state->projects) if (item.projectId == completed->projectId) { item.requiredEngineVersion = completed->version; item.status = "Ready"; break; }
			std::string saveError; LauncherExperience::SaveProjects(state->installRoot, state->projects, saveError); Refresh(*state);
			for (std::size_t index = 0U; index < state->projects.size(); ++index) {
				if (state->projects[index].projectId == completed->projectId) { SendMessageW(state->projectsList, LB_SETCURSEL, index, 0); break; }
			}
			PostMessageW(window, WM_COMMAND, MAKEWPARAM(IdOpen, BN_CLICKED), 0); return 0;
		}
		if (message == kJoinProjectFinished) {
			std::unique_ptr<JoinProjectResult> completed(
				reinterpret_cast<JoinProjectResult*>(lParam));
			state->busy = false;
			Refresh(*state);
			if (!completed->succeeded) {
				ShowResult(window, *state, false, completed->message);
				return 0;
			}
			for (std::size_t index = 0U; index < state->projects.size(); ++index) {
				if (state->projects[index].projectRoot == completed->installedProjectRoot) {
					SendMessageW(state->projectsList, LB_SETCURSEL, index, 0);
					break;
				}
			}
			SetStatus(*state, completed->message);
			const std::wstring question = ToWide(completed->message +
				"\r\n\r\n保存先をExplorerで開きますか？");
			if (MessageBoxW(window, question.c_str(), L"ManoEngine Hub - 参加完了",
				MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				std::string openError;
				if (!OpenFolderInExplorer(window, completed->installedProjectRoot, openError)) {
					ShowResult(window, *state, false, openError);
				}
			}
			return 0;
		}
		if (message == WM_DROPFILES) {
			wchar_t path[32768]{}; DragQueryFileW(reinterpret_cast<HDROP>(wParam), 0, path, _countof(path)); DragFinish(reinterpret_cast<HDROP>(wParam)); Setup(window, *state, path); return 0;
		}
		if (message == WM_COMMAND) {
			const int id = LOWORD(wParam);
			const int notification = HIWORD(wParam);

			// 一覧や入力欄の通知は、選び直しただけでは何もしない。
			// 二重クリックだけを「開く」として扱う。
			if (id == IdProjects || id == IdEngines || id == IdUpdatesList || id == IdCatalogList ||
				id == IdNewProjectEngine ||
				id == IdCatalogHubHost || id == IdEngineUpdateChannel || id == IdEngineUpdateHubHost) {
				if (id == IdProjects && notification == LBN_DBLCLK) {
					PostMessageW(window, WM_COMMAND, MAKEWPARAM(IdOpen, BN_CLICKED), 0);
				}
				return 0;
			}

			if (id == IdNavProjects) { ShowPage(*state, HubPage::Projects); return 0; }
			if (id == IdNavInstalls) {
				if (GetWindowTextLengthW(state->catalogHubHost) == 0) {
					SetWindowTextW(state->catalogHubHost, DefaultCatalogHubHost(*state).c_str());
				}
				ShowPage(*state, HubPage::Installs); return 0;
			}
			if (id == IdNavUpdates) {
				LoadEngineUpdateControls(*state);
				ShowPage(*state, HubPage::Updates); return 0;
			}
			if (id == IdNavSettings) { ShowPage(*state, HubPage::Settings); RefreshDistributionStatus(*state); return 0; }

			if (state->busy) { SetStatus(*state, "処理中です..."); return 0; }

			// ---- 設定ページ ----
			if (id == IdDistributionServerStart || id == IdDistributionServerStop ||
				id == IdDistributionServerRestart) {
				PublisherSettings settings{}; std::string result;
				if (!PublisherService::LoadSettings(state->installRoot, settings, result)) { ShowResult(window, *state, false, result); return 0; }
				bool ok = false;
				if (id == IdDistributionServerStart) {
					ok = PublisherServerService::Start(settings, state->installRoot, result);
				}
				else if (id == IdDistributionServerStop) {
					ok = PublisherServerService::Stop(state->installRoot, result);
				}
				else {
					ok = PublisherServerService::Restart(settings, state->installRoot, result);
				}
				RefreshDistributionStatus(*state);
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdPublisher) { PublisherGui::Open(GetModuleHandleW(nullptr), window, state->installRoot); return 0; }

			// ---- プロジェクトページ ----
			if (id == IdInvite) { const auto invite = PickInvite(window); if (!invite.empty()) Setup(window, *state, invite); return 0; }
			if (id == IdNewProject) {
				const LRESULT selectedEngine = SendMessageW(state->newProjectEngine, CB_GETCURSEL, 0, 0);
				if (selectedEngine == CB_ERR || static_cast<std::size_t>(selectedEngine) >= state->newProjectEngineVersions.size()) {
					ShowResult(window, *state, false, "新規Projectに使うEngineを選択してください"); return 0;
				}
				const std::string& engineVersion = state->newProjectEngineVersions[static_cast<std::size_t>(selectedEngine)];
				const auto parent = PickFolder(window, L"新しいプロジェクトを作る場所を選んでください");
				if (parent.empty()) return 0;
				const std::string name = ToUtf8Text(parent.filename().wstring());
				std::string result;
				RegisteredProject created{};
				const bool ok = LauncherExperience::CreateProject(
					parent, name, engineVersion, "標準", state->installRoot, created, result);
				Refresh(*state); ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdChangeProjectEngine) {
				RegisteredProject* project = Selected(*state);
				if (project == nullptr || project->projectRoot.empty()) {
					ShowResult(window, *state, false, "Engineを変更するローカルProjectを選択してください"); return 0;
				}
				const LRESULT selectedEngine = SendMessageW(state->newProjectEngine, CB_GETCURSEL, 0, 0);
				if (selectedEngine == CB_ERR ||
					static_cast<std::size_t>(selectedEngine) >= state->newProjectEngineVersions.size()) {
					ShowResult(window, *state, false, "変更先のEngineを選択してください"); return 0;
				}
				const std::string targetVersionText =
					state->newProjectEngineVersions[static_cast<std::size_t>(selectedEngine)];
				if (project->requiredEngineVersion == targetVersionText) {
					ShowResult(window, *state, true, "このProjectは既にEngine " + targetVersionText + " を使用しています"); return 0;
				}

				EngineVersion targetVersion{};
				EngineUpdateManifest targetManifest{};
				ProjectVersionSettings projectSettings{};
				std::string result;
				if (!EngineVersion::TryParse(targetVersionText, targetVersion) ||
					!LauncherUpdate::LoadManifest(
						LauncherUpdate::GetInstalledManifestPath(state->installRoot, targetVersion),
						targetManifest, result)) {
					ShowResult(window, *state, false, result.empty()
						? "選択したEngineの管理情報を読み取れません" : result); return 0;
				}
				if (!ProjectVersionManager::Load(project->projectRoot, projectSettings, result)) {
					ShowResult(window, *state, false, "Projectのバージョン設定を読み取れません: " + result); return 0;
				}
				if (projectSettings.projectFormatVersion > targetManifest.requiredProjectFormat) {
					ShowResult(window, *state, false,
						"選択したEngineはこのProject形式に対応していません"); return 0;
				}

				const std::wstring confirmation = ToWide(
					project->projectName + "\nEngine " + project->requiredEngineVersion + " → " +
					targetVersionText + "\n\nProjectが使用するEngineを変更しますか？");
				if (MessageBoxW(window, confirmation.c_str(), L"ProjectのEngine変更",
						MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;

				const ProjectVersionSettings previousSettings = projectSettings;
				const RegisteredProject previousProject = *project;
				projectSettings.requiredEngineVersion = targetVersion;
				projectSettings.engineVersionPolicy = ProjectEngineVersionPolicy::Pinned;
				projectSettings.updateChannel = targetManifest.channel;
				if (targetManifest.scriptApiVersion != 0u) {
					projectSettings.requiredScriptApiVersion = targetManifest.scriptApiVersion;
				}
				if (!ProjectVersionManager::Save(project->projectRoot, projectSettings, result)) {
					ShowResult(window, *state, false, "ProjectのEngine設定を保存できません: " + result); return 0;
				}
				project->requiredEngineVersion = targetVersionText;
				project->updateChannel = targetManifest.channel;
				project->status = "Ready";
				if (!LauncherExperience::SaveProjects(state->installRoot, state->projects, result)) {
					*project = previousProject;
					std::string restoreError;
					ProjectVersionManager::Save(project->projectRoot, previousSettings, restoreError);
					ShowResult(window, *state, false, "LauncherのProject一覧を保存できません: " + result); return 0;
				}
				result = "ProjectのEngineを変更しました: " + previousProject.requiredEngineVersion +
					" → " + targetVersionText;
				// HubのProject Snapshot Manifestは再Publishまで旧Versionを持つため、
				// 共同制作Projectでは参加側が「Versionが一致しません」で止まる。
				if (!project->collaborationId.empty()) {
					result += "\n共同制作Projectです。Hubへ再Publishするまで、"
						"参加者が取得するSnapshotは旧Versionのままになります。";
				}
				Refresh(*state); ShowResult(window, *state, true, result); return 0;
			}
			if (id == IdAddProject) {
				const auto projectRoot = PickFolder(window, L"既にあるプロジェクトのフォルダーを選んでください");
				if (projectRoot.empty()) return 0;
				std::string result;
				const bool ok = AddExistingProject(projectRoot, state->installRoot, state->projects, result);
				Refresh(*state); ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdOpenProjectFolder) {
				const RegisteredProject* project = Selected(*state);
				if (project == nullptr || project->projectRoot.empty()) {
					ShowResult(window, *state, false, "保存先を開くプロジェクトを選択してください。");
					return 0;
				}
				std::string result;
				if (!OpenFolderInExplorer(window, project->projectRoot, result)) {
					ShowResult(window, *state, false, result);
				}
				else {
					SetStatus(*state, "保存先を開きました: " + ToUtf8Text(project->projectRoot.wstring()));
				}
				return 0;
			}
			if (id == IdJoinByCode) {
				const std::string code = ToUtf8Text(ReadText(state->joinCode));
				// 配布サーバーはEngineを入れたときのHubを流用する。参加者に毎回アドレスを聞かない。
				const std::string hubHost = ToUtf8Text(DefaultCatalogHubHost(*state));
				if (hubHost.empty()) {
					ShowResult(window, *state, false,
						"配布サーバーが分かりません。インストール画面でHubサーバーを一度指定してください。");
					return 0;
				}
				StartJoinProjectByCode(window, *state, code, hubHost);
				return 0;
			}
			if (id == IdPublishProjectToHub) {
				const RegisteredProject* target = Selected(*state);
				if (target == nullptr || target->projectRoot.empty()) {
					ShowResult(window, *state, false, "公開するプロジェクトを選択してください。"); return 0;
				}
				PublisherSettings hubSettings{};
				std::string result;
				if (!PublisherService::LoadSettings(state->installRoot, hubSettings, result)) {
					ShowResult(window, *state, false,
						"配布の設定を読めません。設定画面で配布フォルダーと公開アドレスを保存してください: " + result);
					return 0;
				}
				SetStatus(*state, "プロジェクトをHubへ公開中...");
				UpdateWindow(window);
				const bool ok = PublisherService::PublishProjectSnapshot(target->projectRoot, hubSettings, result);
				if (ok) {
					// 公開できたら参加コードもその場で出す。渡すものが1つに決まる。
					result += "\r\n\r\n参加コード: " +
						LauncherExperience::MakeProjectJoinCode(target->projectId);
				}
				Refresh(*state); ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdShowJoinCode) {
				const RegisteredProject* selectedProject = Selected(*state);
				if (selectedProject == nullptr) {
					ShowResult(window, *state, false, "コードを表示するプロジェクトを選択してください。"); return 0;
				}
				const std::string code = LauncherExperience::MakeProjectJoinCode(selectedProject->projectId);
				ShowResult(window, *state, true,
					"参加コード: " + code + "\r\n\r\n"
					"このコードを渡してください。受け取った人はプロジェクト画面でこれを入れるだけで参加できます。\r\n"
					"コードは変わらないので、公開し直しても渡し直す必要はありません。");
				return 0;
			}
			if (id == IdImportProjectZip) {
				const auto archive = PickZip(window);
				if (archive.empty()) return 0;
				const auto destination = PickFolder(window, L"Project ZIPを取り込む親フォルダーを選んでください");
				if (destination.empty()) return 0;
				RegisteredProject imported{};
				std::string result;
				const bool ok = LauncherExperience::ImportProjectZip(archive, destination,
					state->installRoot, imported, result);
				Refresh(*state); ShowResult(window, *state, ok, result); return 0;
			}

			// ---- インストールページ ----
			if (id == IdFetchCatalog) {
				const std::string hubHost = ToUtf8Text(ReadText(state->catalogHubHost));
				if (hubHost.empty()) { SetStatus(*state, "Hubサーバーのアドレスを入力してください"); return 0; }
				SetStatus(*state, "バージョン一覧を取得しています..."); UpdateWindow(window);
				std::string error;
				const bool ok = LauncherExperience::FetchEngineCatalog(hubHost, state->installRoot, state->catalog, error);
				RefreshCatalogList(*state);
				if (ok) { RememberCatalogHubHost(*state, hubHost); SetStatus(*state, "バージョン一覧を取得しました"); }
				else ShowResult(window, *state, false, error);
				return 0;
			}
			if (id == IdInstallEngine) {
				const LRESULT selected = SendMessageW(state->catalogList, LB_GETCURSEL, 0, 0);
				if (selected == LB_ERR || static_cast<std::size_t>(selected) >= state->catalog.size()) {
					SetStatus(*state, "Hubから取得するか、配布ZIPを一覧へ追加してから選んでください"); return 0;
				}
				const auto& entry = state->catalog[static_cast<std::size_t>(selected)];
				std::string result;
				SetStatus(*state, "エンジンを導入しています..."); UpdateWindow(window);
				const bool offline = !entry.offlineArchivePath.empty();
				const bool ok = offline
					? LauncherExperience::InstallOfflineEngine(entry.offlineArchivePath, state->installRoot, result)
					: LauncherUpdate::Apply(entry.manifestPath, state->installRoot, {}, false, result);
				Refresh(*state);
				if (ok) {
					if (offline) {
						state->catalog[static_cast<std::size_t>(selected)].isInstalled = true;
					} else {
						// Hub経由の一覧は導入直後に再取得して [導入済み] 表示へ更新する。
						const std::string hubHost = ToUtf8Text(ReadText(state->catalogHubHost));
						std::string catalogError;
						LauncherExperience::FetchEngineCatalog(hubHost, state->installRoot, state->catalog, catalogError);
					}
					RefreshCatalogList(*state);
				}
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdRemoveEngine) {
				const LRESULT selected = SendMessageW(state->enginesList, LB_GETCURSEL, 0, 0);
				if (selected == LB_ERR || static_cast<std::size_t>(selected) >= state->installedEngineVersions.size()) {
					SetStatus(*state, "削除する未使用のEngineを選んでください"); return 0;
				}
				const std::string versionText = state->installedEngineVersions[static_cast<std::size_t>(selected)];
				const std::wstring confirmation = ToWide(
					"Engine " + versionText + " をPCから削除します。\nこの操作は元に戻せません。\n\n削除しますか？");
				if (MessageBoxW(window, confirmation.c_str(), L"Engineの削除確認", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
				std::string result;
				const bool ok = LauncherExperience::RemoveInstalledEngine(state->installRoot, versionText, result);
				Refresh(*state);
				if (ok && !state->catalog.empty()) {
					const std::string hubHost = ToUtf8Text(ReadText(state->catalogHubHost));
					std::string catalogError;
					LauncherExperience::FetchEngineCatalog(hubHost, state->installRoot, state->catalog, catalogError);
					RefreshCatalogList(*state);
				}
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdExportPortableEngine) {
				const LRESULT selected = SendMessageW(state->enginesList, LB_GETCURSEL, 0, 0);
				if (selected == LB_ERR || static_cast<std::size_t>(selected) >= state->installedEngineVersions.size()) {
					ShowResult(window, *state, false, "Portable ZIPにする導入済みEngineを選んでください"); return 0;
				}
				const std::string version = state->installedEngineVersions[static_cast<std::size_t>(selected)];
				const auto output = PickZipOutput(window, "ManoEngine-" + version);
				if (output.empty()) return 0;
				std::string result;
				const bool ok = LauncherExperience::ExportPortableEngine(state->installRoot, version, output, result);
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdInstallOfflineEngine) {
				const auto archive = PickZip(window);
				if (archive.empty()) return 0;
				EngineCatalogEntry entry{}; std::string result;
				const bool ok = LauncherExperience::LoadOfflineEngineCatalogEntry(
					archive, state->installRoot, entry, result);
				if (ok) {
					std::erase_if(state->catalog, [&entry](const EngineCatalogEntry& existing) {
						return existing.version == entry.version;
					});
					state->catalog.push_back(std::move(entry));
					RefreshCatalogList(*state);
				}
				ShowResult(window, *state, ok, result); return 0;
			}

			// ---- 更新ページ ----
			if (id == IdSaveEngineUpdateSettings) {
				std::string result;
				const bool ok = SaveEngineUpdateSettings(*state, result);
				if (ok) {
					state->hasAvailableEngineUpdate = false;
					LoadEngineUpdateControls(*state);
				}
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdCheckEngineUpdate) {
				SetWindowTextW(state->engineUpdateStatus, L"Engine更新を確認しています...");
				SetStatus(*state, "Engine更新を確認しています...");
				UpdateWindow(window);
				std::string result;
				const bool ok = CheckEngineUpdate(*state, result);
				if (!ok) SetWindowTextW(state->engineUpdateStatus, ToWide(result).c_str());
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdApplyEngineUpdate) {
				if (!state->hasAvailableEngineUpdate) {
					SetWindowTextW(state->engineUpdateStatus, L"Engine更新を確認しています...");
					SetStatus(*state, "Engine更新を確認しています...");
					UpdateWindow(window);
					std::string checkResult;
					if (!CheckEngineUpdate(*state, checkResult)) {
						SetWindowTextW(state->engineUpdateStatus, ToWide(checkResult).c_str());
						ShowResult(window, *state, false, checkResult); return 0;
					}
					if (!state->hasAvailableEngineUpdate) {
						ShowResult(window, *state, true, checkResult); return 0;
					}
				}
				LauncherState launcher{};
				std::string result;
				if (!LauncherUpdate::LoadState(state->installRoot, launcher, result)) {
					ShowResult(window, *state, false, result); return 0;
				}
				const std::string targetVersion = state->availableEngineUpdate.version.ToString();
				const std::wstring confirmation = ToWide(
					"Engine " + launcher.currentVersion + " → " + targetVersion + "\n更新しますか？");
				if (MessageBoxW(window, confirmation.c_str(), L"Engine本体の更新", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;

				SetWindowTextW(state->engineUpdateStatus, L"Engineを更新しています...");
				SetStatus(*state, "Engineを更新しています...");
				UpdateWindow(window);
				const bool ok = LauncherUpdate::Apply(
					state->availableEngineUpdate.manifestPath, state->installRoot, {}, false, result);
				state->hasAvailableEngineUpdate = false;
				Refresh(*state);
				LoadEngineUpdateControls(*state);
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdRecheckUpdates) {
				SendMessageW(state->updatesList, LB_RESETCONTENT, 0, 0);
				state->updateProjectIds.clear();
				SetStatus(*state, "更新を確認しています...");
				CheckForUpdates(window, *state); return 0;
			}

			RegisteredProject* project = Selected(*state);

			// 更新ページの選択は、更新一覧側のプロジェクトを対象にする。
			if (id == IdUpdate) {
				const LRESULT selected = SendMessageW(state->updatesList, LB_GETCURSEL, 0, 0);
				if (selected == LB_ERR || static_cast<std::size_t>(selected) >= state->updateProjectIds.size()) {
					SetStatus(*state, "更新するプロジェクトを選んでください"); return 0;
				}
				const std::string& targetId = state->updateProjectIds[static_cast<std::size_t>(selected)];
				project = nullptr;
				for (auto& item : state->projects) if (item.projectId == targetId) { project = &item; break; }
			}

			if (!project) { SetStatus(*state, "プロジェクトを選択してください"); return 0; }
			std::string result; bool ok = false;

			if (id == IdExportProjectZip) {
				const auto output = PickZipOutput(window, project->projectName);
				if (output.empty()) return 0;
				ok = LauncherExperience::ExportProjectZip(*project, output, result);
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdPrepareGit) {
				ok = LauncherExperience::PrepareGitProject(project->projectRoot,
					ToUtf8Text(ReadText(state->gitRemote)), result);
				ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdUseGitFallback || id == IdUseRealtimeCollaboration) {
				ok = LauncherExperience::SetGitFallback(project->projectRoot,
					id == IdUseGitFallback, result);
				ShowResult(window, *state, ok, result); return 0;
			}

			if (id == IdCreateInvite) {
				PublisherSettings hubSettings{};
				if (!PublisherService::LoadSettings(state->installRoot, hubSettings, result)) {
					ShowResult(window, *state, false, result); return 0;
				}

				ManoInvite invite{};
				invite.formatVersion = 1U;
				// Projectフォルダーが持つMetadataのIDが正。配布者画面の文字入力を使うと、
				// Hubの公開先とProject自身のIDがずれて「Project IDが一致しません」になる。
				invite.projectId = !project->projectId.empty() ? project->projectId : hubSettings.collaborationProjectId;
				invite.projectName = project->projectName;
				// 登録時のHubがそのまま繋がるとは限らない（Tailscale未接続でMagicDNS名が引けない等）ので、
				// 実際に応答したアドレスを書き出す。受け取った側が手でアドレスを直さずに済む。
				invite.hubHost = PublisherServerService::ResolveReachableHubAddress(hubSettings,
					!project->hubHost.empty() ? project->hubHost : hubSettings.publicHubAddress);
				invite.updateChannel = project->updateChannel;
				invite.requiredEngineVersion = project->requiredEngineVersion;
				invite.projectEndpoint = project->projectEndpoint;
				// 共同制作Serverの接続先。これが空だとSetupInviteがTeamCollaboration.inviteを書かず、
				// 参加できてもEditorが同じ部屋へ入れない。
				invite.collaborationHost = PublisherServerService::ResolveCollaborationHost(hubSettings);
				invite.collaborationPort = hubSettings.collaborationPort;
				invite.collaborationId = !project->collaborationId.empty() ? project->collaborationId : hubSettings.collaborationId;
				invite.ownerId = !project->ownerId.empty() ? project->ownerId : hubSettings.ownerId;
				invite.snapshotRevision = project->lastSyncedRevision;

				if (invite.hubHost.empty()) {
					ShowResult(window, *state, false,
						"配布サーバーのアドレスが未設定です。設定の『配布・公開の設定』で公開アドレスを保存してください。");
					return 0;
				}
				const auto output = PickInviteOutput(window, project->projectName);
				if (output.empty()) return 0;
				ok = LauncherExperience::SaveInvite(output, invite, result);
				if (ok) result = "招待ファイルを作成しました:\n" + output.generic_string();
				ShowResult(window, *state, ok, result); return 0;
			}

			if (id == IdRemoveProject) {
				if (MessageBoxW(window, L"一覧から外します。フォルダーやファイルは消えません。",
						L"確認", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;
				const std::string targetId = project->projectId;
				state->projects.erase(std::remove_if(state->projects.begin(), state->projects.end(),
					[&targetId](const RegisteredProject& item) { return item.projectId == targetId; }), state->projects.end());
				ok = LauncherExperience::SaveProjects(state->installRoot, state->projects, result);
				if (ok) result = "一覧から外しました";
				Refresh(*state); ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdProjectLocation) {
				const auto root = PickFolder(window, L"プロジェクトフォルダーを選択");
				if (!root.empty()) {
					ProjectVersionSettings settings{};
					if (!ProjectVersionManager::Load(root, settings, result)) ok = false;
					else if (settings.updateChannel != project->updateChannel) result = "プロジェクトと招待ファイルの更新種別が一致しません";
					else {
						project->projectRoot = root; project->requiredEngineVersion = settings.requiredEngineVersion.ToString();
						std::error_code ec;
						project->status = std::filesystem::exists(LauncherUpdate::GetEnginesDirectory(state->installRoot) /
							project->requiredEngineVersion / "CG2.exe", ec) ? "Ready" : "Needs Install";
						ok = LauncherExperience::SaveProjects(state->installRoot, state->projects, result);
						if (ok) result = project->status == "Ready" ? "プロジェクトの場所を登録しました" : "プロジェクトを登録しました。必要なエンジンを導入してください";
					}
				}
				Refresh(*state); if (!root.empty()) ShowResult(window, *state, ok, result); return 0;
			}
			if (id == IdOpen) {
				if (project->projectRoot.empty()) { SetStatus(*state, "プロジェクトの取得または場所の登録が必要です"); return 0; }
				ProjectVersionSettings projectVersion{};
				if (!ProjectVersionManager::Load(project->projectRoot, projectVersion, result)) { ShowResult(window, *state, false, result); return 0; }
				EngineVersion selected{};
				if (!EngineVersion::TryParse(project->requiredEngineVersion, selected)) { ShowResult(window, *state, false, "プロジェクトのエンジンバージョンが不正です"); return 0; }
				ManoInvite updateInvite{1U, project->projectId, project->projectName, project->hubHost, project->updateChannel,
					projectVersion.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned ? projectVersion.requiredEngineVersion.ToString() : std::string{}, project->projectEndpoint, {}};
				std::error_code installedError; const bool installed = std::filesystem::exists(
					LauncherUpdate::GetInstalledManifestPath(state->installRoot, selected), installedError);
				std::filesystem::path availablePath; EngineUpdateManifest available{}; std::string updateError;
				const bool shouldCheckHub = !installed || projectVersion.engineVersionPolicy == ProjectEngineVersionPolicy::Minimum;
				const bool resolved = shouldCheckHub && LauncherExperience::ResolveManifest(updateInvite, state->installRoot, availablePath, available, updateError);
				if (!installed && !resolved) {
					// 必要なエンジンが無く、配布元にも届かない。何をすればよいかまで書く。
					ShowResult(window, *state, false,
						"このプロジェクトに必要なエンジン " + project->requiredEngineVersion + " が導入されていません。\n"
						"インストール画面からエンジンを導入してください。\n" + updateError);
					ShowPage(*state, HubPage::Installs); return 0;
				}
				if (resolved && (!installed || (projectVersion.engineVersionPolicy == ProjectEngineVersionPolicy::Minimum && selected < available.version))) {
					StartUpdateAndOpen(window, *state, *project, availablePath, available); return 0;
				}
				EngineUpdateManifest installedManifest{};
				if (!LauncherUpdate::LoadManifest(LauncherUpdate::GetInstalledManifestPath(state->installRoot, selected), installedManifest, result)) {
					ShowResult(window, *state, false, "必要なエンジンが未導入か、管理情報がありません。インストール画面から導入してください。\n" + result);
					ShowPage(*state, HubPage::Installs); return 0;
				}
				if (projectVersion.projectFormatVersion > installedManifest.requiredProjectFormat) {
					ShowResult(window, *state, false,
						"このプロジェクトは、いま入っているエンジンより新しい形式です。\n"
						"インストール画面から新しいエンジンを導入してください。");
					ShowPage(*state, HubPage::Installs); return 0;
				}
				if (installedManifest.scriptApiVersion != 0U && projectVersion.requiredScriptApiVersion != installedManifest.scriptApiVersion) {
					ShowResult(window, *state, false,
						"プロジェクトとエンジンのスクリプトAPIが一致しません。\n"
						"インストール画面から、このプロジェクトに合うエンジンを導入してください。");
					ShowPage(*state, HubPage::Installs); return 0;
				}
				if (projectVersion.projectFormatVersion < installedManifest.requiredProjectFormat) {
					if (MessageBoxW(window, L"プロジェクト形式が古いため、バックアップ後に移行しますか？", L"プロジェクト移行が必要です", MB_YESNO | MB_ICONQUESTION) != IDYES) {
						SetStatus(*state, "移行が完了するまでエディタを開けません"); return 0;
					}
					if (!ProjectVersionManager::MigrateProject(project->projectRoot, result)) { ShowResult(window, *state, false, result); return 0; }
				}
				const auto engine = LauncherUpdate::GetEnginesDirectory(state->installRoot) / project->requiredEngineVersion;
				const auto report = EngineEnvironmentCheck::Run(engine, EnvironmentCheckMode::EditorUser);
				if (report.HasRequiredFailure()) { ShowResult(window, *state, false, "必要DLLまたは環境が壊れています。修復を実行してください。\n" + report.ToText()); return 0; }
				ok = LauncherUpdate::OpenProject(state->installRoot, project->projectRoot, result, project->requiredEngineVersion);
			} else if (id == IdVerify || id == IdRepair) {
				EngineVersion version{};
				if (!EngineVersion::TryParse(project->requiredEngineVersion, version)) result = "プロジェクトのエンジンバージョンが不正です";
				else {
					const auto manifest = LauncherUpdate::GetInstalledManifestPath(state->installRoot, version);
					ok = id == IdVerify ? LauncherUpdate::Verify(manifest, state->installRoot, result)
						: LauncherUpdate::Apply(manifest, state->installRoot, project->projectRoot, true, result);
				}
			} else if (id == IdUpdate) {
				ManoInvite invite{1U, project->projectId, project->projectName, project->hubHost, project->updateChannel, {}, project->projectEndpoint, {}};
				ProjectVersionSettings settings{}; std::string metadataError;
				if (!project->projectRoot.empty() && ProjectVersionManager::Load(project->projectRoot, settings, metadataError) && settings.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned) {
					invite.requiredEngineVersion = settings.requiredEngineVersion.ToString();
					std::error_code ec;
					if (std::filesystem::exists(LauncherUpdate::GetEnginesDirectory(state->installRoot) / invite.requiredEngineVersion / "CG2.exe", ec)) {
						ok = true; result = "このプロジェクトはエンジン " + invite.requiredEngineVersion + " に固定され、導入済みです";
					} else {
						std::filesystem::path manifestPath; EngineUpdateManifest manifest{};
						if (LauncherExperience::ResolveManifest(invite, state->installRoot, manifestPath, manifest, result)) {
							ok = LauncherUpdate::Apply(manifestPath, state->installRoot, project->projectRoot, false, result);
							if (ok) { project->status = "Ready"; LauncherExperience::SaveProjects(state->installRoot, state->projects, metadataError); }
						}
					}
				} else {
					std::filesystem::path manifestPath; EngineUpdateManifest manifest{};
					if (LauncherExperience::ResolveManifest(invite, state->installRoot, manifestPath, manifest, result)) {
						if (manifest.version.ToString() == project->requiredEngineVersion) { ok = true; result = "最新です: " + project->requiredEngineVersion; }
						else if (MessageBoxW(window, ToWide(project->requiredEngineVersion + " → " + manifest.version.ToString() + "\n更新しますか？").c_str(), L"更新があります", MB_YESNO | MB_ICONQUESTION) == IDYES) {
							ok = LauncherUpdate::Apply(manifestPath, state->installRoot, project->projectRoot, false, result);
							if (ok) { project->requiredEngineVersion = manifest.version.ToString(); LauncherExperience::SaveProjects(state->installRoot, state->projects, metadataError); }
						}
					}
				}
				if (ok) {
					SendMessageW(state->updatesList, LB_RESETCONTENT, 0, 0);
					state->updateProjectIds.clear();
				}
			} else if (id == IdRollback) {
				LauncherState launcher{};
				if (LauncherUpdate::LoadState(state->installRoot, launcher, result) && !launcher.previousVersion.empty()) {
					EngineVersion previous{}; EngineVersion::TryParse(launcher.previousVersion, previous);
					EngineUpdateManifest oldManifest{}; const auto oldPath = LauncherUpdate::GetInstalledManifestPath(state->installRoot, previous);
					if (!LauncherUpdate::LoadManifest(oldPath, oldManifest, result)) ok = false;
					else {
						ProjectVersionSettings settings{}; std::string metadataError;
						if (!project->projectRoot.empty() && ProjectVersionManager::Load(project->projectRoot, settings, metadataError) && settings.projectFormatVersion > oldManifest.requiredProjectFormat) result = "旧エンジンは現在のプロジェクト形式に対応していません";
						else {
							ok = LauncherUpdate::Rollback(state->installRoot, result);
							if (ok) {
								project->requiredEngineVersion = previous.ToString();
								if (!project->projectRoot.empty() && ProjectVersionManager::Load(project->projectRoot, settings, metadataError) &&
									settings.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned) {
									settings.requiredEngineVersion = previous;
									ok = ProjectVersionManager::Save(project->projectRoot, settings, metadataError);
									if (!ok) result = "エンジン切替後、プロジェクトのバージョン設定保存に失敗: " + metadataError;
								}
								LauncherExperience::SaveProjects(state->installRoot, state->projects, metadataError);
							}
						}
					}
				}
			}
			Refresh(*state); if (!result.empty()) ShowResult(window, *state, ok, result); return 0;
		}
		if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
		return DefWindowProcW(window, message, wParam, lParam);
	}
}

int LauncherGui::Run(HINSTANCE instance, const std::filesystem::path& initialInvite) {
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const auto installRoot = LauncherExperience::DefaultInstallRoot();
	RegisterInviteAssociation(EnsureLauncherInstalled(installRoot));
	WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance; type.lpfnWndProc = WindowProc;
	type.lpszClassName = L"ManoLauncherWindow"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
	WindowState state{}; state.installRoot = installRoot;
	HWND window = CreateWindowExW(0, type.lpszClassName, L"ManoEngine Hub", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, &state);
	if (!window) return 1;
	ShowWindow(window, SW_SHOW); UpdateWindow(window);
	if (!initialInvite.empty()) Setup(window, state, initialInvite);
	MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
	CoUninitialize(); return static_cast<int>(message.wParam);
}
