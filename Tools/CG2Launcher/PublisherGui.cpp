#include "PublisherGui.h"

#include "LauncherExperience.h"
#include "PublisherService.h"
#include "Source/Engine/Core/EngineVersion.h"

#include <commdlg.h>
#include <shlobj.h>

#include <cctype>
#include <cwchar>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {
	constexpr UINT kProgressMessage = WM_APP + 41U;
	constexpr UINT kFinishedMessage = WM_APP + 42U;

	enum ControlId : int {
		IdRelease = 2000, IdReleaseBrowse, IdHubRoot, IdHubBrowse, IdPublicHub, IdChannel, IdMode,
		IdProjectId, IdProjectName, IdDistributionPort,
		IdSave, IdPreview, IdPublish, IdBuildAndPublish, IdPublishProject, IdCheck, IdCreateInvite, IdDistributionStart, IdDistributionStop, IdDistributionRestart,
		IdHistory, IdServerStatus, IdProgress,
	};

	enum class AsyncOperation { Preview, PreviewForPublish, BuildNextVersionForPublish, Publish, ProjectPublish };

	struct AsyncResult {
		AsyncOperation operation = AsyncOperation::Preview;
		bool succeeded = false;
		std::string message;
		PublishPreview preview{};
	};

	struct PublisherWindowState {
		std::filesystem::path installRoot;
		PublisherSettings settings{};
		PublishPreview pendingPreview{};
		bool busy = false;
		HWND release = nullptr; HWND hubRoot = nullptr; HWND publicHub = nullptr; HWND channel = nullptr; HWND mode = nullptr;
		HWND projectId = nullptr; HWND projectName = nullptr; HWND distributionPort = nullptr;
		HWND history = nullptr; HWND serverStatus = nullptr; HWND progress = nullptr; HWND previewButton = nullptr; HWND publishButton = nullptr; HWND buildPublishButton = nullptr; HWND projectPublishButton = nullptr;
	};

	std::wstring ToWide(const std::string& value) {
		if (value.empty()) return {};
		const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
		std::wstring result(count > 0 ? static_cast<std::size_t>(count) : 0U, L'\0');
		if (count > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
		return result;
	}

	std::string ToUtf8(const std::wstring& value) {
		if (value.empty()) return {};
		const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string result(count > 0 ? static_cast<std::size_t>(count) : 0U, '\0');
		if (count > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
		return result;
	}

	std::wstring ControlText(HWND control) {
		const int length = GetWindowTextLengthW(control); std::wstring value(static_cast<std::size_t>(length), L'\0');
		if (length > 0) GetWindowTextW(control, value.data(), length + 1); return value;
	}

	void SetControlText(HWND control, const std::filesystem::path& path) { SetWindowTextW(control, path.c_str()); }

	std::filesystem::path PickFolder(HWND owner, const wchar_t* title) {
		BROWSEINFOW info{}; info.hwndOwner = owner; info.lpszTitle = title; info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info); if (!item) return {};
		wchar_t path[32768]{}; const bool ok = SHGetPathFromIDListW(item, path) != FALSE; CoTaskMemFree(item);
		return ok ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::filesystem::path PickInviteOutput(HWND owner) {
		wchar_t path[32768] = L"MyGame.cg2-invite"; OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
		dialog.lpstrFilter = L"CG2Engine 招待ファイル (*.cg2-invite)\0*.cg2-invite\0"; dialog.lpstrFile = path; dialog.nMaxFile = _countof(path);
		dialog.lpstrDefExt = L"cg2-invite"; dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		return GetSaveFileNameW(&dialog) ? std::filesystem::path(path) : std::filesystem::path{};
	}

	std::string ChannelLabel(EngineUpdateChannel channel) {
		if (channel == EngineUpdateChannel::Beta) return "ベータ版";
		if (channel == EngineUpdateChannel::Dev) return "開発版";
		return "安定版";
	}

	// 子Controlには独自Fontを設定していないため、既定のSYSTEM_FONTで文字の実寸を測る。
	// 固定幅のまま日本語を並べると、環境のFont次第で末尾が切れる。
	int MeasureControlTextWidth(HWND window, const wchar_t* text) {
		HDC deviceContext = GetDC(window);
		if (deviceContext == nullptr) return 0;
		HGDIOBJ previousFont = SelectObject(deviceContext, GetStockObject(SYSTEM_FONT));
		SIZE size{};
		const bool measured = GetTextExtentPoint32W(deviceContext, text, static_cast<int>(std::wcslen(text)), &size) != FALSE;
		SelectObject(deviceContext, previousFont);
		ReleaseDC(window, deviceContext);
		return measured ? static_cast<int>(size.cx) : 0;
	}

	HWND AddLabel(HWND window, const wchar_t* text, int x, int y, int width = 150) {
		// 折り返すと高さ22からはみ出して2行目が丸ごと消えるため、折り返しを禁止して実寸まで広げる。
		const int required = MeasureControlTextWidth(window, text) + 6;
		return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX,
			x, y, required > width ? required : width, 22, window, nullptr, nullptr, nullptr);
	}

	// xを実際に使った幅だけ進めるので、文字が長くても次のButtonと重ならない。
	HWND AddButton(HWND window, const wchar_t* text, int id, int& x, int y, int width) {
		const int required = MeasureControlTextWidth(window, text) + 18;
		const int actual = required > width ? required : width;
		HWND button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE, x, y, actual, 32,
			window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
		x += actual + 10;
		return button;
	}

	HWND AddEdit(HWND window, int id, int x, int y, int width) {
		return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, width, 24,
			window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
	}

	std::uint16_t PortValue(HWND control, std::uint16_t fallback) {
		try { const int value = std::stoi(ControlText(control)); return value >= 1 && value <= 65535 ? static_cast<std::uint16_t>(value) : fallback; }
		catch (...) { return fallback; }
	}

	// Button行の長さは環境のFontで変わるため、Window幅を決め打ちにすると末尾が切れる。
	// 実際に使った右端までWindowを広げ、横いっぱいのControlもそこへ合わせる。
	void FitWindowToContent(HWND window, std::initializer_list<HWND> fullWidthControls, int contentRight) {
		const int clientWidth = contentRight + 20;
		for (HWND control : fullWidthControls) {
			RECT bounds{};
			if (control == nullptr || GetWindowRect(control, &bounds) == FALSE) continue;
			SetWindowPos(control, nullptr, 0, 0, clientWidth - 40, bounds.bottom - bounds.top,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		RECT frame{0, 0, clientWidth, 680};
		AdjustWindowRect(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE);
		SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void LoadControls(PublisherWindowState& state) {
		SetControlText(state.release, state.settings.releaseSource); SetControlText(state.hubRoot, state.settings.hubRoot);
		SetWindowTextW(state.publicHub, ToWide(state.settings.publicHubAddress).c_str());
		SetWindowTextW(state.projectId, ToWide(state.settings.collaborationProjectId).c_str());
		SetWindowTextW(state.projectName, ToWide(state.settings.collaborationProjectName).c_str());
		SetWindowTextW(state.distributionPort, std::to_wstring(state.settings.distributionPort).c_str());
		SendMessageW(state.channel, CB_SETCURSEL, static_cast<WPARAM>(state.settings.defaultChannel), 0);
		SendMessageW(state.mode, CB_SETCURSEL, state.settings.hubMode == PublisherHubMode::Local ? 0 : 1, 0);
	}

	void ReadControls(PublisherWindowState& state) {
		state.settings.releaseSource = ControlText(state.release); state.settings.hubRoot = ControlText(state.hubRoot);
		state.settings.publicHubAddress = ToUtf8(ControlText(state.publicHub));
		state.settings.collaborationProjectId = ToUtf8(ControlText(state.projectId));
		state.settings.collaborationProjectName = ToUtf8(ControlText(state.projectName));
		state.settings.distributionPort = PortValue(state.distributionPort, 8080U);
		const LRESULT channel = SendMessageW(state.channel, CB_GETCURSEL, 0, 0); state.settings.defaultChannel = channel == 1 ? EngineUpdateChannel::Beta : channel == 2 ? EngineUpdateChannel::Dev : EngineUpdateChannel::Stable;
		state.settings.hubMode = SendMessageW(state.mode, CB_GETCURSEL, 0, 0) == 1 ? PublisherHubMode::Remote : PublisherHubMode::Local;
	}

	std::string PublishedVersion(const PublisherSettings& settings, EngineUpdateChannel channel) {
		std::string name = GetEngineUpdateChannelText(channel); for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		EngineUpdateManifest manifest{}; std::string error;
		return LauncherUpdate::LoadManifest(settings.hubRoot / "update" / name / "engine.manifest", manifest, error) ? manifest.version.ToString() : "未公開";
	}

	void RefreshInfo(PublisherWindowState& state) {
		const auto distribution = PublisherServerService::GetStatus(state.settings, state.installRoot);
		std::ostringstream text; text << "エンジン " << GetCG2EngineDisplayVersion() << "    プロジェクト形式 " << GetCG2ProjectFormatVersion()
			<< "    スクリプトAPI " << GetCG2ScriptApiVersion() << "\r\n安定版: " << PublishedVersion(state.settings, EngineUpdateChannel::Stable)
			<< "    ベータ版: " << PublishedVersion(state.settings, EngineUpdateChannel::Beta) << "    開発版: " << PublishedVersion(state.settings, EngineUpdateChannel::Dev)
			<< "\r\n配布サーバー: " << distribution.detail;
		SetWindowTextW(state.serverStatus, ToWide(text.str()).c_str());
		std::vector<PublishHistoryEntry> history; std::string error; PublisherService::LoadHistory(state.installRoot, history, error);
		SendMessageW(state.history, LB_RESETCONTENT, 0, 0);
		for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator) {
			const std::string line = iterator->publishedAt + "  " + iterator->version + "  " + ChannelLabel(iterator->channel)
				+ "  " + std::to_string(iterator->fileCount) + " ファイル  " + std::to_string(iterator->totalSize) + " バイト  " + iterator->result;
			const std::wstring wide = ToWide(line); SendMessageW(state.history, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
		}
	}

	void SetBusy(PublisherWindowState& state, bool busy) {
		state.busy = busy; EnableWindow(state.previewButton, !busy); EnableWindow(state.publishButton, !busy);
		EnableWindow(state.buildPublishButton, !busy); EnableWindow(state.projectPublishButton, !busy);
	}

	void StartPreview(HWND window, PublisherWindowState& state, AsyncOperation operation) {
		ReadControls(state); std::string saveError;
		if (!PublisherService::SaveSettings(state.installRoot, state.settings, saveError)) {
			MessageBoxW(window, ToWide(saveError).c_str(), L"配布者", MB_OK | MB_ICONERROR); return;
		}
		SetBusy(state, true); SetWindowTextW(state.progress, L"リリース成果物を確認中...");
		const PublisherSettings settings = state.settings;
		std::thread([window, settings, operation]() {
			auto progress = [window](const std::string& value) { PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(new std::string(value))); };
			auto* result = new AsyncResult{}; result->operation = operation;
			result->succeeded = PublisherService::CreatePreview(settings, result->preview, result->message, progress);
			if (result->succeeded) result->message = PublisherService::PreviewText(result->preview);
			PostMessageW(window, kFinishedMessage, 0, reinterpret_cast<LPARAM>(result));
		}).detach();
	}

	void StartPublish(HWND window, PublisherWindowState& state) {
		SetBusy(state, true); SetWindowTextW(state.progress, L"ファイルを収集中..."); const PublisherSettings settings = state.settings;
		const PublishPreview preview = state.pendingPreview; const auto installRoot = state.installRoot;
		std::thread([window, settings, preview, installRoot]() {
			auto progress = [window](const std::string& value) { PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(new std::string(value))); };
			auto* result = new AsyncResult{}; result->operation = AsyncOperation::Publish; result->preview = preview;
			result->succeeded = PublisherService::Publish(settings, preview, result->message, progress, installRoot);
			PostMessageW(window, kFinishedMessage, 0, reinterpret_cast<LPARAM>(result));
		}).detach();
	}

	void StartBuildNextVersion(HWND window, PublisherWindowState& state) {
		ReadControls(state); std::string saveError;
		if (!PublisherService::SaveSettings(state.installRoot, state.settings, saveError)) {
			MessageBoxW(window, ToWide(saveError).c_str(), L"次のBuildを作成して公開", MB_OK | MB_ICONERROR); return;
		}
		SetBusy(state, true); SetWindowTextW(state.progress, L"次のBuild番号を準備中...");
		const PublisherSettings settings = state.settings;
		std::thread([window, settings]() {
			auto progress = [window](const std::string& value) { PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(new std::string(value))); };
			auto* result = new AsyncResult{}; result->operation = AsyncOperation::BuildNextVersionForPublish;
			result->succeeded = PublisherService::BuildNextVersionPreview(settings, result->preview, result->message, progress);
			if (result->succeeded) result->message = PublisherService::PreviewText(result->preview);
			PostMessageW(window, kFinishedMessage, 0, reinterpret_cast<LPARAM>(result));
		}).detach();
	}

	void StartProjectPublish(HWND window, PublisherWindowState& state, const std::filesystem::path& projectRoot) {
		ReadControls(state); std::string saveError;
		if (!PublisherService::SaveSettings(state.installRoot, state.settings, saveError)) {
			MessageBoxW(window, ToWide(saveError).c_str(), L"Project公開", MB_OK | MB_ICONERROR); return;
		}
		SetBusy(state, true); SetWindowTextW(state.progress, L"プロジェクトSnapshotを作成中...");
		const PublisherSettings settings = state.settings;
		std::thread([window, settings, projectRoot]() {
			auto progress = [window](const std::string& value) {
				PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(new std::string(value)));
			};
			auto* result = new AsyncResult{}; result->operation = AsyncOperation::ProjectPublish;
			result->succeeded = PublisherService::PublishProjectSnapshot(projectRoot, settings, result->message, progress);
			PostMessageW(window, kFinishedMessage, 0, reinterpret_cast<LPARAM>(result));
		}).detach();
	}

	void RunServerAction(HWND window, PublisherWindowState& state, int action) {
		ReadControls(state); std::string result; bool ok = false;
		if (action == 0) ok = PublisherServerService::Start(state.settings, state.installRoot, result);
		else if (action == 1) ok = PublisherServerService::Stop(state.installRoot, result);
		else ok = PublisherServerService::Restart(state.settings, state.installRoot, result);
		if (ok && action != 1) {
			SetWindowTextW(state.publicHub, ToWide(state.settings.publicHubAddress).c_str());
			SetWindowTextW(state.distributionPort, std::to_wstring(state.settings.distributionPort).c_str());
		}
		RefreshInfo(state); MessageBoxW(window, ToWide(result).c_str(), L"配布サーバー管理", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
	}

	LRESULT CALLBACK PublisherProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		auto* state = reinterpret_cast<PublisherWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_CREATE) {
			state = reinterpret_cast<PublisherWindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			AddLabel(window, L"リリース元", 20, 18); state->release = AddEdit(window, IdRelease, 180, 16, 684);
			CreateWindowW(L"BUTTON", L"参照", WS_CHILD | WS_VISIBLE, 874, 15, 90, 27, window, reinterpret_cast<HMENU>(IdReleaseBrowse), nullptr, nullptr);
			AddLabel(window, L"配布フォルダー", 20, 52); state->hubRoot = AddEdit(window, IdHubRoot, 180, 50, 684);
			CreateWindowW(L"BUTTON", L"参照", WS_CHILD | WS_VISIBLE, 874, 49, 90, 27, window, reinterpret_cast<HMENU>(IdHubBrowse), nullptr, nullptr);
			AddLabel(window, L"公開アドレス", 20, 86); state->publicHub = AddEdit(window, IdPublicHub, 180, 84, 784);
			AddLabel(window, L"既定の公開先", 20, 120); state->channel = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 180, 118, 160, 120, window, reinterpret_cast<HMENU>(IdChannel), nullptr, nullptr);
			for (const wchar_t* item : {L"安定版", L"ベータ版", L"開発版"}) SendMessageW(state->channel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
			AddLabel(window, L"配布方法", 370, 120, 100); state->mode = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 480, 118, 150, 90, window, reinterpret_cast<HMENU>(IdMode), nullptr, nullptr);
			for (const wchar_t* item : {L"このPC", L"外部サーバー"}) SendMessageW(state->mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
			AddLabel(window, L"配布ポート", 700, 120, 120); state->distributionPort = AddEdit(window, IdDistributionPort, 874, 118, 90);
			AddLabel(window, L"配布プロジェクトID", 20, 154, 155); state->projectId = AddEdit(window, IdProjectId, 180, 152, 250);
			AddLabel(window, L"プロジェクト名", 450, 154, 120); state->projectName = AddEdit(window, IdProjectName, 580, 152, 384);
			int buttonX = 20;
			AddButton(window, L"設定を保存", IdSave, buttonX, 198, 110);
			state->previewButton = AddButton(window, L"公開内容を確認", IdPreview, buttonX, 198, 130);
			state->publishButton = AddButton(window, L"現在のBuildを公開", IdPublish, buttonX, 198, 130);
			state->buildPublishButton = AddButton(window, L"次のBuildを作成して公開", IdBuildAndPublish, buttonX, 198, 170);
			state->projectPublishButton = AddButton(window, L"プロジェクトを公開", IdPublishProject, buttonX, 198, 120);
			AddButton(window, L"確認", IdCheck, buttonX, 198, 50);
			AddButton(window, L"招待", IdCreateInvite, buttonX, 198, 50);
			state->progress = CreateWindowW(L"STATIC", L"準備完了", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX, 20, 238, 944, 24, window, reinterpret_cast<HMENU>(IdProgress), nullptr, nullptr);
			state->serverStatus = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 265, 944, 72, window, reinterpret_cast<HMENU>(IdServerStatus), nullptr, nullptr);
			AddLabel(window, L"配布サーバー", 20, 355, 160);
			int serverButtonX = 190;
			AddButton(window, L"開始", IdDistributionStart, serverButtonX, 347, 80);
			AddButton(window, L"停止", IdDistributionStop, serverButtonX, 347, 80);
			AddButton(window, L"再起動", IdDistributionRestart, serverButtonX, 347, 80);
			AddLabel(window, L"公開履歴", 20, 395, 300); state->history = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
				20, 420, 944, 220, window, reinterpret_cast<HMENU>(IdHistory), nullptr, nullptr);
			FitWindowToContent(window, {state->progress, state->serverStatus, state->history},
				buttonX - 10 > 964 ? buttonX - 10 : 964);
			LoadControls(*state); RefreshInfo(*state); return 0;
		}
		if (!state) return DefWindowProcW(window, message, wParam, lParam);
		if (message == kProgressMessage) {
			std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lParam)); SetWindowTextW(state->progress, ToWide(*text).c_str()); return 0;
		}
		if (message == kFinishedMessage) {
			std::unique_ptr<AsyncResult> result(reinterpret_cast<AsyncResult*>(lParam)); SetBusy(*state, false);
			SetWindowTextW(state->progress, ToWide(result->succeeded ? "完了" : result->message).c_str());
			if (!result->succeeded) { MessageBoxW(window, ToWide(result->message + "\n\n旧公開バージョンは維持されています。設定を直して再試行できます。").c_str(), L"公開エラー", MB_OK | MB_ICONERROR); return 0; }
			if (result->operation == AsyncOperation::PreviewForPublish || result->operation == AsyncOperation::BuildNextVersionForPublish) {
				state->pendingPreview = result->preview;
				const std::wstring title = result->operation == AsyncOperation::BuildNextVersionForPublish ? L"Buildと公開内容の確認" : L"公開内容の確認";
				if (MessageBoxW(window, ToWide(result->message + "\n\nこの内容を公開しますか？").c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) StartPublish(window, *state);
			} else {
				const wchar_t* title = result->operation == AsyncOperation::Preview ? L"公開内容の確認"
					: result->operation == AsyncOperation::ProjectPublish ? L"Project公開完了" : L"公開完了";
				MessageBoxW(window, ToWide(result->message).c_str(), title, MB_OK | MB_ICONINFORMATION);
			}
			RefreshInfo(*state); return 0;
		}
		if (message == WM_COMMAND) {
			const int id = LOWORD(wParam);
			if (id == IdReleaseBrowse || id == IdHubBrowse) { const auto path = PickFolder(window, id == IdReleaseBrowse ? L"リリース元" : L"配布フォルダー"); if (!path.empty()) SetControlText(id == IdReleaseBrowse ? state->release : state->hubRoot, path); return 0; }
			if (id == IdSave) { ReadControls(*state); std::string error; const bool ok = PublisherService::SaveSettings(state->installRoot, state->settings, error); if (ok) error = "配布設定を保存しました"; MessageBoxW(window, ToWide(error).c_str(), L"配布者", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR)); return 0; }
			if (id == IdPreview) { StartPreview(window, *state, AsyncOperation::Preview); return 0; }
			if (id == IdPublish) { StartPreview(window, *state, AsyncOperation::PreviewForPublish); return 0; }
			if (id == IdBuildAndPublish) { StartBuildNextVersion(window, *state); return 0; }
			if (id == IdPublishProject) {
				const auto projectRoot = PickFolder(window, L"配布するProjectフォルダーを選択してください");
				if (!projectRoot.empty()) StartProjectPublish(window, *state, projectRoot);
				return 0;
			}
			if (id == IdCheck) { ReadControls(*state); RefreshInfo(*state); SetWindowTextW(state->progress, L"サーバー確認完了"); return 0; }
			if (id == IdCreateInvite) {
				ReadControls(*state); const auto output = PickInviteOutput(window); if (output.empty()) return 0;
				CG2Invite invite{}; invite.formatVersion = 1U; invite.projectId = state->settings.collaborationProjectId;
				invite.projectName = state->settings.collaborationProjectName;
				// 設定どおりのアドレスが必ず繋がるとは限らないため、応答した候補を選んで書き出す。
				invite.hubHost = PublisherServerService::ResolveReachableHubAddress(state->settings, state->settings.publicHubAddress);
				invite.updateChannel = state->settings.defaultChannel; invite.requiredEngineVersion = GetCG2EngineDisplayVersion();
				invite.projectEndpoint = "/projects/" + state->settings.collaborationProjectId + "/project.manifest";
				// 共同制作Serverの接続先が無いと、参加してもEditorが同じ部屋へ入れない。
				invite.collaborationHost = PublisherServerService::ResolveCollaborationHost(state->settings);
				invite.collaborationPort = state->settings.collaborationPort;
				invite.collaborationId = state->settings.collaborationId;
				invite.ownerId = state->settings.ownerId;
				std::string result; const bool ok = LauncherExperience::SaveInvite(output, invite, result);
				if (ok) result = "招待ファイルを作成しました: " + output.generic_string();
				MessageBoxW(window, ToWide(result).c_str(), L"招待ファイル作成", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR)); return 0;
			}
			if (id >= IdDistributionStart && id <= IdDistributionRestart) { RunServerAction(window, *state, id - IdDistributionStart); return 0; }
		}
		if (message == WM_CLOSE && state->busy) { MessageBoxW(window, L"公開処理または確認が完了するまで、この画面は閉じられません。", L"配布者", MB_OK | MB_ICONWARNING); return 0; }
		if (message == WM_DESTROY) { delete state; return 0; }
		return DefWindowProcW(window, message, wParam, lParam);
	}
}

void PublisherGui::Open(HINSTANCE instance, HWND owner, const std::filesystem::path& installRoot) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance; type.lpfnWndProc = PublisherProc;
		type.lpszClassName = L"CG2PublisherWindow"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		registered = RegisterClassExW(&type) != 0U || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
	auto* state = new PublisherWindowState{}; state->installRoot = installRoot; std::string error; PublisherService::LoadSettings(installRoot, state->settings, error);
	HWND window = CreateWindowExW(WS_EX_APPWINDOW, L"CG2PublisherWindow", L"CG2Engine ランチャー - 配布者", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 960, 740, owner, nullptr, instance, state);
	if (!window) { delete state; return; } ShowWindow(window, SW_SHOW); UpdateWindow(window);
}
