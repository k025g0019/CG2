#pragma once

#include "Source/Engine/Core/EngineVersion.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct EngineManifestFile {
	std::filesystem::path relativePath;
	std::uint64_t size = 0U;
	std::string sha256;
};

struct EngineUpdateManifest {
	EngineVersion version{};
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	std::uint32_t requiredProjectFormat = 0U;
	std::uint32_t scriptApiVersion = 0U;
	std::string baseUrl;
	std::vector<EngineManifestFile> files;
	std::vector<std::filesystem::path> removedFiles;
};

struct LauncherState {
	std::string currentVersion;
	std::string previousVersion;
	EngineUpdateChannel channel = EngineUpdateChannel::Stable;
	std::string manifestLocation;
};

using LauncherProgress = std::function<void(const std::string&)>;

class LauncherUpdate {
public:
	static bool LoadManifest(const std::filesystem::path& path, EngineUpdateManifest& manifest, std::string& error);
	static bool CreateManifest(const std::filesystem::path& packageDirectory,
		const std::filesystem::path& outputPath, const EngineUpdateManifest& settings, std::string& error);
	static bool LoadState(const std::filesystem::path& installRoot, LauncherState& state, std::string& error);
	static bool Apply(const std::filesystem::path& manifestPath, const std::filesystem::path& installRoot,
		const std::filesystem::path& projectRoot, bool repairsCurrent, std::string& result,
		const LauncherProgress& progress = {});
	// Hubへ接続せず、展開済みのPortable Engine packageから導入する。
	static bool InstallOfflinePackage(const std::filesystem::path& packageDirectory,
		const std::filesystem::path& installRoot, std::string& result);
	static bool Verify(const std::filesystem::path& manifestPath, const std::filesystem::path& installRoot,
		std::string& result);
	static bool Rollback(const std::filesystem::path& installRoot, std::string& result);
	static bool SetChannel(const std::filesystem::path& installRoot, EngineUpdateChannel channel, std::string& result);
	static bool OpenProject(const std::filesystem::path& installRoot, const std::filesystem::path& projectRoot,
		std::string& result, const std::string& requestedVersion = {});
	static std::filesystem::path GetEnginesDirectory(const std::filesystem::path& installRoot);
	static std::filesystem::path GetLauncherStateDirectory(const std::filesystem::path& installRoot);
	static std::filesystem::path GetInstalledManifestPath(const std::filesystem::path& installRoot,
		const EngineVersion& version);
	static std::string CalculateSha256(const std::filesystem::path& path, std::string& error);
};
