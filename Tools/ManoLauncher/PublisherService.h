#pragma once

#include "LauncherUpdate.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

enum class PublisherHubMode : std::uint32_t {
	Local = 0U,
	Remote,
};

enum class CollaborationConnectionMode : std::uint32_t {
	Tailscale = 0U,
	Lan,
};

struct PublisherSettings {
	std::filesystem::path releaseSource;
	std::filesystem::path hubRoot;
	std::string publicHubAddress;
	EngineUpdateChannel defaultChannel = EngineUpdateChannel::Stable;
	std::filesystem::path collaborationServer;
	std::string collaborationProjectId = "my-game";
	std::string collaborationProjectName = "MyGame";
	std::string collaborationId;
	std::string ownerId;
	PublisherHubMode hubMode = PublisherHubMode::Local;
	std::uint16_t distributionPort = 8080U;
	std::uint16_t collaborationPort = 48000U;
	CollaborationConnectionMode collaborationMode = CollaborationConnectionMode::Tailscale;
	std::string collaborationHost;
	std::string collaborationOwnerUserId;
	bool collaborationAutoStart = false;
};

struct PublishPreview {
	EngineVersion version{};
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	std::uint64_t totalSize = 0U;
	std::uint32_t fileCount = 0U;
	std::uint32_t addedCount = 0U;
	std::uint32_t changedCount = 0U;
	std::uint32_t removedCount = 0U;
	std::string publicHub;
	std::vector<std::filesystem::path> includedFiles;
	std::vector<std::filesystem::path> removedFiles;
};

struct PublishHistoryEntry {
	std::string version;
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	std::string publishedAt;
	std::uint32_t fileCount = 0U;
	std::uint64_t totalSize = 0U;
	std::string result;
};

using PublisherProgress = std::function<void(const std::string&)>;

class PublisherService {
public:
	static PublisherSettings CreateDefaults(const std::filesystem::path& installRoot);
	static bool LoadSettings(const std::filesystem::path& installRoot, PublisherSettings& settings, std::string& error);
	static bool SaveSettings(const std::filesystem::path& installRoot, const PublisherSettings& settings, std::string& error);
	static bool CreatePreview(const PublisherSettings& settings, PublishPreview& preview, std::string& error,
		const PublisherProgress& progress = {});
	// リリース元からリポジトリを特定し、build 番号を一つ進めて Engine をビルドした後に公開内容を作る。
	// Launcher 自身は実行中に上書きできないため、この操作では ManoEngine だけをビルドする。
	static bool BuildNextVersionPreview(const PublisherSettings& settings, PublishPreview& preview, std::string& error,
		const PublisherProgress& progress = {});
	static bool CreatePreviewForVersion(const PublisherSettings& settings, const EngineVersion& version,
		PublishPreview& preview, std::string& error, const PublisherProgress& progress = {});
	static bool Publish(const PublisherSettings& settings, const PublishPreview& preview, std::string& result,
		const PublisherProgress& progress = {}, const std::filesystem::path& installRoot = {});
	static bool LoadHistory(const std::filesystem::path& installRoot, std::vector<PublishHistoryEntry>& history, std::string& error);
	static bool IsPublishFile(const std::filesystem::path& relativePath);
	static bool IsProjectSnapshotFile(const std::filesystem::path& relativePath);
	static bool PublishProjectSnapshot(const std::filesystem::path& projectRoot,
		const PublisherSettings& settings, std::string& result,
		const PublisherProgress& progress = {});
	static std::string PreviewText(const PublishPreview& preview);
};

struct ManagedServerStatus {
	bool running = false;
	bool reachable = false;
	std::uint16_t port = 0U;
	std::uint32_t latencyMilliseconds = 0U;
	std::string detail;
};

class PublisherServerService {
public:
	static bool Start(PublisherSettings& settings, const std::filesystem::path& installRoot, std::string& result);
	static bool Stop(const std::filesystem::path& installRoot, std::string& result);
	static bool Restart(PublisherSettings& settings, const std::filesystem::path& installRoot, std::string& result);
	static ManagedServerStatus GetStatus(const PublisherSettings& settings, const std::filesystem::path& installRoot);
	static bool RunDistributionServer(const std::filesystem::path& root, std::uint16_t port, std::string& error);
	// 招待や案内に載せるHubアドレスを決める。候補を順に接続確認し、実際に応答したものを返す。
	// preferredが繋がらない環境（Tailscale未接続でMagicDNS名が引けない等）でも、そのまま使えるアドレスを書き出すため。
	static std::string ResolveReachableHubAddress(const PublisherSettings& settings, const std::string& preferred);
	// 共同制作Serverの接続先ホスト。Hubアドレスと同じ考え方で、応答したものを優先する。
	static std::string ResolveCollaborationHost(const PublisherSettings& settings);
};
