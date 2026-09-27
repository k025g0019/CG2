#pragma once

#include "LauncherUpdate.h"

#include <filesystem>
#include <string>
#include <vector>

inline constexpr const char* kCG2LauncherVersion = "1.1.0+3";

struct CG2Invite {
	std::uint32_t formatVersion = 0U;
	std::string projectId;
	std::string projectName;
	std::string hubHost;
	EngineUpdateChannel updateChannel = EngineUpdateChannel::Stable;
	std::string requiredEngineVersion;
	std::string projectEndpoint;
	std::string engineManifestEndpoint;
	// 共同制作Serverの接続先。Hub(配布用HTTP)とは別で、CG2TeamServerのTCP接続先を指す。
	// Tailscale利用時は 100.x.x.x ではなく MagicDNS hostname を入れる。
	std::string collaborationHost;
	std::uint16_t collaborationPort = 0U;
	std::string collaborationId;
	std::string ownerId;
	std::uint64_t snapshotRevision = 0U;
};

// Hubに公開されたProject一覧の1件。Launcherへ配布先を手入力しなくても、
// 内蔵HubからProjectを見つけて参加するために使う。
struct HubProjectCatalogEntry {
	std::string projectId;
	std::string projectName;
	std::string requiredEngineVersion;
	EngineUpdateChannel updateChannel = EngineUpdateChannel::Stable;
	std::string projectManifestEndpoint;
	std::string collaborationHost;
	std::uint16_t collaborationPort = 48000U;
	std::string collaborationId;
	std::string ownerId;
	std::uint64_t snapshotRevision = 0U;
};

// Hubが公開しているEngine Versionの一覧に出す1行。
struct EngineCatalogEntry {
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	EngineVersion version{};
	std::filesystem::path manifestPath;
	// Portable Engine ZIPから一覧へ追加した場合の導入元。空ならHub経由。
	std::filesystem::path offlineArchivePath;
	bool isInstalled = false;
};

struct RegisteredProject {
	std::string projectId;
	std::string projectName;
	std::string hubHost;
	EngineUpdateChannel updateChannel = EngineUpdateChannel::Stable;
	std::string requiredEngineVersion;
	std::string projectEndpoint;
	std::filesystem::path projectRoot;
	std::string status;
	std::string collaborationId;
	std::string ownerId;
	std::uint64_t lastSyncedRevision = 0U;
};

class LauncherExperience {
public:
	static std::filesystem::path DefaultInstallRoot();
	static std::string DefaultHubAddress();
	static bool LoadInvite(const std::filesystem::path& path, CG2Invite& invite, std::string& error);
	static bool SaveInvite(const std::filesystem::path& path, const CG2Invite& invite, std::string& error);
	static bool ResolveManifest(const CG2Invite& invite, const std::filesystem::path& installRoot,
		std::filesystem::path& manifestPath, EngineUpdateManifest& manifest, std::string& error);
	static bool SetupInvite(const std::filesystem::path& invitePath, const std::filesystem::path& installRoot,
		const std::filesystem::path& projectRoot, std::string& result);
	static bool FetchProjectCatalog(const std::string& hubHost, const std::filesystem::path& installRoot,
		std::vector<HubProjectCatalogEntry>& projects, std::string& error);
	static bool JoinProject(const HubProjectCatalogEntry& project, const std::string& hubHost,
		const std::filesystem::path& installRoot, std::filesystem::path& installedProjectRoot,
		std::string& result, const LauncherProgress& progress = {});
	// 招待ファイルを配らずに参加できるようにする短いCode。Project IDから毎回同じ値を作るので、
	// Engineを公開し直してもProjectを公開し直してもCodeは変わらない。Hub側に対応表も要らない。
	static std::string MakeProjectJoinCode(const std::string& projectId);
	// Codeに一致するProjectをHubのカタログから探し、そのまま参加する。
	// Engine導入・Project取得・共同制作設定の書き込み・Editor起動までJoinProjectが面倒を見る。
	static bool JoinProjectByCode(const std::string& joinCode, const std::string& hubHost,
		const std::filesystem::path& installRoot, std::filesystem::path& installedProjectRoot,
		std::string& result, const LauncherProgress& progress = {});
	// Projectと無関係に、Hubが公開しているChannelごとのEngine一覧を取得する。
	static bool FetchEngineCatalog(const std::string& hubHost, const std::filesystem::path& installRoot,
		std::vector<EngineCatalogEntry>& entries, std::string& error);
	static bool CreateProject(const std::filesystem::path& projectRoot,
		const std::string& projectName, const std::string& engineVersion,
		const std::string& templateName, const std::filesystem::path& installRoot,
		RegisteredProject& registeredProject, std::string& result);
	static bool LoadProjects(const std::filesystem::path& installRoot,
		std::vector<RegisteredProject>& projects, std::string& error);
	static bool SaveProjects(const std::filesystem::path& installRoot,
		const std::vector<RegisteredProject>& projects, std::string& error);
	static bool GetProjectManifest(const RegisteredProject& project, const std::filesystem::path& installRoot,
		std::filesystem::path& manifestPath, EngineUpdateManifest& manifest, std::string& error);
	static bool PublishEngine(const std::filesystem::path& releaseDirectory,
		const std::filesystem::path& publicationRoot, const EngineUpdateManifest& settings,
		const std::string& hubBaseUrl, std::string& result);
	static std::vector<std::string> ListInstalledEngines(const std::filesystem::path& installRoot);
	// 現在版・ロールバック版・登録Projectが参照中の版は保護し、未使用の版だけ削除する。
	static bool RemoveInstalledEngine(const std::filesystem::path& installRoot,
		const std::string& versionText, std::string& result);
	static bool ExportProjectZip(const RegisteredProject& project,
		const std::filesystem::path& outputZip, std::string& result);
	static bool ImportProjectZip(const std::filesystem::path& archivePath,
		const std::filesystem::path& destinationParent, const std::filesystem::path& installRoot,
		RegisteredProject& importedProject, std::string& result);
	static bool PrepareGitProject(const std::filesystem::path& projectRoot,
		const std::string& remoteUrl, std::string& result);
	static bool SetGitFallback(const std::filesystem::path& projectRoot, bool enabled, std::string& result);
	static bool EnsureProjectRecoveryFiles(const std::filesystem::path& projectRoot, std::string& result);
	static bool ExportPortableEngine(const std::filesystem::path& installRoot,
		const std::string& versionText, const std::filesystem::path& outputZip, std::string& result);
	// ZIPをすぐ導入せず、manifestだけを読み取ってインストール候補へ追加する。
	static bool LoadOfflineEngineCatalogEntry(const std::filesystem::path& archivePath,
		const std::filesystem::path& installRoot, EngineCatalogEntry& entry, std::string& result);
	static bool InstallOfflineEngine(const std::filesystem::path& archivePath,
		const std::filesystem::path& installRoot, std::string& result);
};
