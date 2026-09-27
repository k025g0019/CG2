#pragma once

#include "EngineVersion.h"

#include <cstdint>
#include <filesystem>
#include <string>

enum class ProjectEngineVersionPolicy : std::uint32_t {
	Minimum = 0U,
	Pinned,
};

struct ProjectVersionSettings {
	EngineVersion requiredEngineVersion{};
	ProjectEngineVersionPolicy engineVersionPolicy = ProjectEngineVersionPolicy::Minimum;
	EngineUpdateChannel updateChannel = EngineUpdateChannel::Stable;
	std::uint32_t projectFormatVersion = 0U;
	std::uint32_t sceneFormatVersion = 0U;
	std::uint32_t prefabFormatVersion = 0U;
	std::uint32_t requiredScriptApiVersion = 0U;
};

enum class ProjectCompatibilityStatus : std::uint32_t {
	Compatible = 0U,
	NeedsEngineUpdate,
	NeedsMigration,
	ProjectTooNew,
	PinnedEngineMismatch,
	ScriptApiMismatch,
	InvalidMetadata,
};

struct ProjectCompatibilityResult {
	ProjectCompatibilityStatus status = ProjectCompatibilityStatus::InvalidMetadata;
	std::string message;
	bool canOpen = false;
	bool canSave = false;
};

class ProjectVersionManager {
public:
	static std::filesystem::path GetMetadataPath(const std::filesystem::path& projectRoot);
	static bool Load(const std::filesystem::path& projectRoot, ProjectVersionSettings& settings, std::string& error);
	static bool Save(const std::filesystem::path& projectRoot, const ProjectVersionSettings& settings, std::string& error);
	static ProjectVersionSettings CreateCurrentDefaults();
	static ProjectCompatibilityResult Evaluate(const ProjectVersionSettings& settings);
	static bool MigrateProject(const std::filesystem::path& projectRoot, std::string& resultMessage);
	static bool MigrateScene(const std::filesystem::path& scenePath, std::uint32_t oldVersion,
		std::uint32_t newVersion, std::string& error);
	static bool MigratePrefab(const std::filesystem::path& prefabPath, std::uint32_t oldVersion,
		std::uint32_t newVersion, std::string& error);
	static bool IsAssetFormatSupported(const std::filesystem::path& assetPath, bool isPrefab,
		std::string* error = nullptr);
	static void SetCurrentProjectWriteAllowed(bool isAllowed);
	static bool IsCurrentProjectWriteAllowed();
};
