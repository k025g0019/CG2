#include "LauncherUpdate.h"

#include "LauncherExperience.h"
#include "LauncherGui.h"
#include "HttpDownload.h"
#include "PublisherService.h"

#include "Source/Engine/Core/EngineEnvironmentCheck.h"
#include "Source/Engine/Core/ProjectVersionManager.h"

#include <Windows.h>
#include <urlmon.h>

#include <filesystem>
#include <algorithm>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "urlmon.lib")

namespace {
	void HideConsoleForGui() {
		if (const HWND console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);
	}

	std::string ToUtf8(const std::wstring& value) {
		if (value.empty()) return {};
		const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (byteCount <= 0) return {};
		std::string result(static_cast<std::size_t>(byteCount), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), result.data(), byteCount, nullptr, nullptr);
		return result;
	}

	std::filesystem::path Value(const std::vector<std::wstring>& args, const wchar_t* key) {
		for (std::size_t i = 1U; i + 1U < args.size(); ++i) if (args[i] == key) return args[i + 1U];
		return {};
	}
	std::wstring WideValue(const std::vector<std::wstring>& args, const wchar_t* key) {
		for (std::size_t i = 1U; i + 1U < args.size(); ++i) if (args[i] == key) return args[i + 1U];
		return {};
	}
	std::filesystem::path ResolveManifest(const std::vector<std::wstring>& args, std::string& error) {
		const std::wstring url = WideValue(args, L"--manifest-url");
		if (url.empty()) return Value(args, L"--manifest");
		wchar_t temporaryRoot[MAX_PATH]{};
		if (GetTempPathW(_countof(temporaryRoot), temporaryRoot) == 0U) { error = "Temp Pathを取得できません"; return {}; }
		const std::filesystem::path cache = std::filesystem::path(temporaryRoot) / "ManoLauncher" / "engine.manifest";
		std::error_code ec; std::filesystem::create_directories(cache.parent_path(), ec);
		const std::string urlText = ToUtf8(url);
		if (ec || !DownloadHttpFile(urlText, cache, error)) {
			error = "Update ManifestをDownloadできません: " + error; return {};
		}
		return cache;
	}
	bool Has(const std::vector<std::wstring>& args, const wchar_t* key) {
		return std::find(args.begin(), args.end(), key) != args.end();
	}
	std::uint16_t PortValue(const std::vector<std::wstring>& args, const wchar_t* key, std::uint16_t fallback) {
		const std::wstring text = WideValue(args, key); if (text.empty()) return fallback;
		try { const int value = std::stoi(text); return value >= 1 && value <= 65535 ? static_cast<std::uint16_t>(value) : fallback; }
		catch (...) { return fallback; }
	}
	void PrintHelp() {
		std::cout << "ManoLauncher " << kManoLauncherVersion << " / Engine tools " << GetManoEngineDisplayVersion() << "\n"
			<< "  install|update|repair --manifest <engine.manifest> --root <EngineRoot> [--project <Project>]\n"
			<< "    Remote manifest: --manifest-url https://server/channel/engine.manifest\n"
			<< "  verify --manifest <engine.manifest> --root <EngineRoot>\n"
			<< "  rollback --root <EngineRoot>\n  open --root <EngineRoot> --project <Project>\n"
			<< "  status --root <EngineRoot>\n  set-channel --root <EngineRoot> --channel Stable|Beta|Dev\n"
			<< "  check-update --manifest/--manifest-url ... --root <EngineRoot> [--project <Project>]\n"
			<< "  migrate --project <Project>\n  check-peer --project <Project> --peer-engine <Version> --peer-format <N> --peer-api <N> --peer-channel <Channel>\n"
			<< "  env --root <EngineFolder> [--developer]\n"
			<< "  create-manifest --package <Folder> --output <File> --version 0.9.4+152 --channel Stable --base <URL/Folder>\n"
			<< "  publish-engine --release <x64/Release> --output <HubFolder> --version <Version> --channel <Channel> --hub <Base URL>\n"
			<< "  publish-project --project <ProjectFolder> [--root <InstallRoot>]\n"
			<< "  publisher-config|publisher-preview|publisher-publish [Publisher settings]\n"
			<< "  publisher-server --action start|stop|restart|status\n"
			<< "  create-invite --output <File.mano-invite> --project-id <Id> --project-name <Name> --hub <Host> --channel <Channel> [--required-engine <Version>]\n"
			<< "  setup-invite --invite <File.mano-invite> [--root <InstallRoot>] [--project <ProjectFolder>]\n";
	}
}

int wmain(int argc, wchar_t** argv) {
	std::vector<std::wstring> args(argv, argv + argc);
	if (argc < 2) { HideConsoleForGui(); return LauncherGui::Run(GetModuleHandleW(nullptr), {}); }
	if (args[1] == L"--gui") { HideConsoleForGui(); return LauncherGui::Run(GetModuleHandleW(nullptr), {}); }
	if (std::filesystem::path(args[1]).extension() == L".mano-invite") {
		HideConsoleForGui(); return LauncherGui::Run(GetModuleHandleW(nullptr), args[1]);
	}
	const std::wstring command = args[1];
	auto root = Value(args, L"--root");
	if (root.empty()) root = LauncherExperience::DefaultInstallRoot();
	if (command == L"serve-hub") {
		std::string serverError;
		const bool served = PublisherServerService::RunDistributionServer(Value(args, L"--output"), PortValue(args, L"--port", 8080U), serverError);
		if (!served) std::cout << serverError << '\n'; return served ? 0 : 1;
	}
	std::string manifestError;
	const auto manifest = ResolveManifest(args, manifestError);
	// Manifest取得の失敗理由は、この後のコマンドがresultを上書きすると消えてしまう。先に出して終わる。
	if (!manifestError.empty()) { std::cout << manifestError << '\n'; return 1; }
	const auto project = Value(args, L"--project");
	std::string result = manifestError; bool succeeded = false;
	if (command == L"publisher-config") {
		PublisherSettings settings = PublisherService::CreateDefaults(root);
		settings.releaseSource = Value(args, L"--release"); settings.hubRoot = Value(args, L"--output"); settings.publicHubAddress = ToUtf8(WideValue(args, L"--hub"));
		settings.collaborationProjectId = ToUtf8(WideValue(args, L"--project-id"));
		const std::string projectName = ToUtf8(WideValue(args, L"--project-name")); if (!projectName.empty()) settings.collaborationProjectName = projectName;
		settings.distributionPort = PortValue(args, L"--distribution-port", 8080U);
		settings.hubMode = WideValue(args, L"--mode") == L"Remote" ? PublisherHubMode::Remote : PublisherHubMode::Local;
		const std::string channel = Value(args, L"--channel").string();
		if (!TryParseEngineUpdateChannel(channel, settings.defaultChannel)) result = "Channelが不正です";
		else succeeded = PublisherService::SaveSettings(root, settings, result);
		if (succeeded) result = "Publisher設定を保存しました";
	} else if (command == L"publisher-preview" || command == L"publisher-publish") {
		PublisherSettings settings{}; PublishPreview preview{};
		if (!PublisherService::LoadSettings(root, settings, result)) succeeded = false;
		else if (!PublisherService::CreatePreview(settings, preview, result)) succeeded = false;
		else if (command == L"publisher-preview") { succeeded = true; result = PublisherService::PreviewText(preview); }
		else succeeded = PublisherService::Publish(settings, preview, result, {}, root);
	} else if (command == L"publisher-server") {
		PublisherSettings settings{}; const std::wstring action = WideValue(args, L"--action");
		if (!PublisherService::LoadSettings(root, settings, result)) succeeded = false;
		else if (action == L"start") succeeded = PublisherServerService::Start(settings, root, result);
		else if (action == L"stop") succeeded = PublisherServerService::Stop(root, result);
		else if (action == L"restart") succeeded = PublisherServerService::Restart(settings, root, result);
		else if (action == L"status") { const auto status = PublisherServerService::GetStatus(settings, root); succeeded = true; result = status.detail; }
		else result = "Actionはstart/stop/restart/statusです";
	} else if (command == L"install" || command == L"update" || command == L"repair")
		succeeded = LauncherUpdate::Apply(manifest, root, project, command == L"repair", result);
	else if (command == L"verify") succeeded = LauncherUpdate::Verify(manifest, root, result);
	else if (command == L"check-update") {
		EngineUpdateManifest available{}; LauncherState state{};
		if (!LauncherUpdate::LoadManifest(manifest, available, result) || !LauncherUpdate::LoadState(root, state, result)) {
			succeeded = false;
		} else {
			EngineVersion installed{};
			EngineVersion::TryParse(state.currentVersion, installed);
			ProjectVersionSettings projectSettings{}; std::string projectError;
			const bool hasProject = !project.empty() && ProjectVersionManager::Load(project, projectSettings, projectError);
			if ((hasProject && projectSettings.updateChannel != available.channel) || (!hasProject && state.channel != available.channel))
				result = "選択Channelに一致しないManifestです";
			else if (hasProject && projectSettings.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned && projectSettings.requiredEngineVersion != available.version) {
				result = "Projectは" + projectSettings.requiredEngineVersion.ToString() + "に固定されています"; succeeded = true;
			} else {
				succeeded = true;
				result = installed < available.version ? "Updateあり: " + state.currentVersion + " -> " + available.version.ToString()
					: "最新です: " + state.currentVersion;
			}
		}
	}
	else if (command == L"rollback") succeeded = LauncherUpdate::Rollback(root, result);
	else if (command == L"status") {
		LauncherState state{};
		succeeded = LauncherUpdate::LoadState(root, state, result);
		if (succeeded) result = "Current: " + state.currentVersion + "\nPrevious: " + state.previousVersion +
			"\nChannel: " + GetEngineUpdateChannelText(state.channel);
	}
	else if (command == L"set-channel") {
		const auto channelValue = Value(args, L"--channel");
		EngineUpdateChannel channel{};
		const std::string channelText = channelValue.string();
		if (!TryParseEngineUpdateChannel(channelText, channel)) result = "ChannelはStable/Beta/Devです";
		else succeeded = LauncherUpdate::SetChannel(root, channel, result);
	}
	else if (command == L"open") succeeded = LauncherUpdate::OpenProject(root, project, result);
	else if (command == L"migrate") succeeded = ProjectVersionManager::MigrateProject(project, result);
	else if (command == L"check-peer") {
		ProjectVersionSettings local{}; std::string error;
		if (!ProjectVersionManager::Load(project, local, error)) result = error;
		else {
			const std::wstring peerEngineWide = WideValue(args, L"--peer-engine");
			const std::wstring peerChannelWide = WideValue(args, L"--peer-channel");
			const std::wstring peerFormatWide = WideValue(args, L"--peer-format");
			const std::wstring peerApiWide = WideValue(args, L"--peer-api");
			succeeded = CheckManoEnginePeerCompatibility(
				ToUtf8(peerEngineWide),
				static_cast<std::uint32_t>(std::wcstoul(peerFormatWide.c_str(), nullptr, 10)),
				static_cast<std::uint32_t>(std::wcstoul(peerApiWide.c_str(), nullptr, 10)),
				ToUtf8(peerChannelWide),
				local.projectFormatVersion, local.updateChannel, result);
			if (succeeded) result = "Peer互換性あり";
		}
	}
	else if (command == L"env") {
		const auto report = EngineEnvironmentCheck::Run(root, Has(args, L"--developer")
			? EnvironmentCheckMode::EngineDeveloper : EnvironmentCheckMode::EditorUser);
		result = report.ToText(); succeeded = !report.HasRequiredFailure();
	} else if (command == L"check-project") {
		ProjectVersionSettings settings{}; std::string error;
		if (ProjectVersionManager::Load(project, settings, error)) {
			const auto compatibility = ProjectVersionManager::Evaluate(settings);
			result = compatibility.message; succeeded = compatibility.canOpen && compatibility.canSave;
		} else result = error;
	} else if (command == L"create-manifest") {
		EngineUpdateManifest settings{};
		const auto version = Value(args, L"--version");
		const auto channel = Value(args, L"--channel");
		const auto base = Value(args, L"--base");
		const std::string versionText = version.string();
		const std::string channelText = channel.string();
		settings.baseUrl = base.string();
		settings.requiredProjectFormat = GetManoProjectFormatVersion();
		settings.scriptApiVersion = GetManoScriptApiVersion();
		if (!EngineVersion::TryParse(versionText, settings.version) || !TryParseEngineUpdateChannel(channelText, settings.channel)) result = "Version/Channelが不正です";
		else succeeded = LauncherUpdate::CreateManifest(Value(args, L"--package"), Value(args, L"--output"), settings, result);
	} else if (command == L"publish-engine") {
		EngineUpdateManifest settings{};
		settings.requiredProjectFormat = GetManoProjectFormatVersion(); settings.scriptApiVersion = GetManoScriptApiVersion();
		const std::string versionText = Value(args, L"--version").string();
		const std::string channelText = Value(args, L"--channel").string();
		if (!EngineVersion::TryParse(versionText, settings.version) || !TryParseEngineUpdateChannel(channelText, settings.channel)) result = "Version/Channelが不正です";
		else succeeded = LauncherExperience::PublishEngine(Value(args, L"--release"), Value(args, L"--output"), settings,
			ToUtf8(WideValue(args, L"--hub")), result);
	} else if (command == L"create-invite") {
		ManoInvite invite{}; invite.formatVersion = 1U;
		invite.projectId = ToUtf8(WideValue(args, L"--project-id")); invite.projectName = ToUtf8(WideValue(args, L"--project-name"));
		invite.hubHost = ToUtf8(WideValue(args, L"--hub")); invite.requiredEngineVersion = ToUtf8(WideValue(args, L"--required-engine"));
		invite.projectEndpoint = ToUtf8(WideValue(args, L"--project-endpoint"));
		invite.engineManifestEndpoint = ToUtf8(WideValue(args, L"--engine-manifest-endpoint"));
		// GUIの「招待を作成」と同じ扱いにする。--hubが繋がらないときは応答したアドレスへ寄せ、
		// 共同制作Serverの接続先も埋める。ここが空のままだと参加してもEditorが同じ部屋へ入れない。
		PublisherSettings inviteSettings{}; std::string inviteSettingsError;
		if (PublisherService::LoadSettings(root, inviteSettings, inviteSettingsError)) {
			invite.hubHost = PublisherServerService::ResolveReachableHubAddress(inviteSettings, invite.hubHost);
			invite.collaborationHost = PublisherServerService::ResolveCollaborationHost(inviteSettings);
			invite.collaborationPort = inviteSettings.collaborationPort;
			invite.collaborationId = inviteSettings.collaborationId;
			invite.ownerId = inviteSettings.ownerId;
		}
		if (!TryParseEngineUpdateChannel(Value(args, L"--channel").string(), invite.updateChannel)) result = "Channelが不正です";
		else succeeded = LauncherExperience::SaveInvite(Value(args, L"--output"), invite, result);
		if (succeeded) result = "Invite作成完了: " + Value(args, L"--output").generic_string();
	} else if (command == L"publish-project") {
		// EditorがHostを開始したときに子プロセスとして呼ぶ。公開処理はPublisherServiceが持っているので、
		// ここは入口だけ。Editor側へ公開のロジックを複製しないための経路。
		PublisherSettings projectPublishSettings{};
		auto projectFolder = project;
		if (projectFolder.empty()) projectFolder = std::filesystem::current_path();
		if (!PublisherService::LoadSettings(root, projectPublishSettings, result)) succeeded = false;
		else succeeded = PublisherService::PublishProjectSnapshot(projectFolder, projectPublishSettings, result);
	} else if (command == L"setup-invite") {
		succeeded = LauncherExperience::SetupInvite(Value(args, L"--invite"), root, project, result);
	} else { PrintHelp(); return 2; }
	std::cout << result << '\n';
	return succeeded ? 0 : 1;
}
