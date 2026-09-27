#include "ProjectVersionManager.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFU, 0xBBU, 0xBFU};
	constexpr char kMetadataRelativePath[] = "ProjectSettings/ProjectVersion.cg2";
	std::atomic_bool g_projectWriteAllowed{true};

	std::string TrimBom(std::string text) {
		if (text.size() >= 3U && static_cast<unsigned char>(text[0]) == kUtf8Bom[0] &&
			static_cast<unsigned char>(text[1]) == kUtf8Bom[1] &&
			static_cast<unsigned char>(text[2]) == kUtf8Bom[2]) text.erase(0U, 3U);
		return text;
	}

	bool WriteAtomic(const std::filesystem::path& path, const std::string& text, std::string& error) {
		std::error_code fileError;
		std::filesystem::create_directories(path.parent_path(), fileError);
		if (fileError) { error = "Directory作成失敗: " + fileError.message(); return false; }
		std::filesystem::path temporaryPath = path;
		temporaryPath += ".migrationtmp";
		std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) { error = "一時Fileを作成できません: " + temporaryPath.generic_string(); return false; }
		file.write(reinterpret_cast<const char*>(kUtf8Bom), sizeof(kUtf8Bom));
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.flush();
		const bool written = file.good();
		file.close();
		if (!written) { error = "一時Fileの書き込みに失敗しました"; return false; }
		if (!MoveFileExW(temporaryPath.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			error = "File切替に失敗しました: Win32 " + std::to_string(GetLastError());
			std::filesystem::remove(temporaryPath, fileError);
			return false;
		}
		return true;
	}

	bool ReadText(const std::filesystem::path& path, std::string& text) {
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) return false;
		text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		text = TrimBom(std::move(text));
		return true;
	}

	std::uint32_t ReadFormatVersion(const std::filesystem::path& path, const char* kind) {
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) return UINT32_MAX;
		std::string line;
		for (int lineIndex = 0; lineIndex < 8 && std::getline(file, line); ++lineIndex) {
			line = TrimBom(std::move(line));
			const std::string prefix = std::string("FormatVersion|") + kind + "|";
			if (!line.starts_with(prefix)) continue;
			try { return static_cast<std::uint32_t>(std::stoul(line.substr(prefix.size()))); }
			catch (...) { return UINT32_MAX; }
		}
		return 0U;
	}

	bool MigrateAssetHeader(const std::filesystem::path& path, const char* kind,
		std::uint32_t oldVersion, std::uint32_t newVersion, std::string& error) {
		if (oldVersion > newVersion) { error = "新しい形式を古い形式へ変換できません"; return false; }
		std::string text;
		if (!ReadText(path, text)) { error = "Assetを読めません: " + path.generic_string(); return false; }
		const std::string newHeader = std::string("FormatVersion|") + kind + "|" + std::to_string(newVersion);
		const std::size_t headerPosition = text.find(std::string("FormatVersion|") + kind + "|");
		if (headerPosition == 0U) {
			const std::size_t lineEnd = text.find('\n');
			text.replace(0U, lineEnd == std::string::npos ? text.size() : lineEnd, newHeader);
		} else {
			text = newHeader + "\n" + text;
		}
		if (text.empty()) { error = "Migration後Assetが空です"; return false; }
		return WriteAtomic(path, text, error);
	}

	std::string CreateBackupFolderName() {
		SYSTEMTIME time{};
		GetLocalTime(&time);
		char buffer[64]{};
		std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u_%02u%02u%02u_%03u",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
			time.wMilliseconds);
		return buffer;
	}
}

std::filesystem::path ProjectVersionManager::GetMetadataPath(const std::filesystem::path& projectRoot) {
	return projectRoot / kMetadataRelativePath;
}

ProjectVersionSettings ProjectVersionManager::CreateCurrentDefaults() {
	ProjectVersionSettings settings{};
	settings.requiredEngineVersion = GetManoEngineVersion();
	settings.updateChannel = GetManoEngineUpdateChannel();
	settings.projectFormatVersion = GetManoProjectFormatVersion();
	settings.sceneFormatVersion = GetManoSceneFormatVersion();
	settings.prefabFormatVersion = GetManoPrefabFormatVersion();
	settings.requiredScriptApiVersion = GetManoScriptApiVersion();
	return settings;
}

bool ProjectVersionManager::Load(const std::filesystem::path& projectRoot,
	ProjectVersionSettings& settings, std::string& error) {
	error.clear();
	std::ifstream file(GetMetadataPath(projectRoot), std::ios::binary);
	if (!file.is_open()) { error = "Project Version Metadataがありません"; return false; }
	ProjectVersionSettings loaded{};
	// Version 1初期のMetadataにはこのFieldが無いため、現行APIを既定値として後方互換にする。
	loaded.requiredScriptApiVersion = GetManoScriptApiVersion();
	std::string line;
	bool recognized = false;
	while (std::getline(file, line)) {
		line = TrimBom(std::move(line));
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const std::size_t separator = line.find('|');
		if (separator == std::string::npos) continue;
		const std::string key = line.substr(0U, separator);
		const std::string value = line.substr(separator + 1U);
		try {
			if (key == "RequiredEngineVersion") recognized = EngineVersion::TryParse(value, loaded.requiredEngineVersion);
			else if (key == "EngineVersionPolicy") loaded.engineVersionPolicy = value == "Pinned" ? ProjectEngineVersionPolicy::Pinned : ProjectEngineVersionPolicy::Minimum;
			else if (key == "UpdateChannel") { if (!TryParseEngineUpdateChannel(value, loaded.updateChannel)) throw std::runtime_error("channel"); }
			else if (key == "ProjectFormatVersion") loaded.projectFormatVersion = std::stoul(value);
			else if (key == "SceneFormatVersion") loaded.sceneFormatVersion = std::stoul(value);
			else if (key == "PrefabFormatVersion") loaded.prefabFormatVersion = std::stoul(value);
			else if (key == "RequiredScriptApiVersion") loaded.requiredScriptApiVersion = std::stoul(value);
		} catch (...) { error = "Project Version Metadataが壊れています: " + key; return false; }
	}
	if (!recognized) { error = "RequiredEngineVersionがありません"; return false; }
	settings = loaded;
	return true;
}

bool ProjectVersionManager::Save(const std::filesystem::path& projectRoot,
	const ProjectVersionSettings& settings, std::string& error) {
	std::ostringstream text;
	text << "ManoEngineProjectVersion|1\r\n"
		<< "RequiredEngineVersion|" << settings.requiredEngineVersion.ToString() << "\r\n"
		<< "EngineVersionPolicy|" << (settings.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned ? "Pinned" : "Minimum") << "\r\n"
		<< "UpdateChannel|" << GetEngineUpdateChannelText(settings.updateChannel) << "\r\n"
		<< "ProjectFormatVersion|" << settings.projectFormatVersion << "\r\n"
		<< "SceneFormatVersion|" << settings.sceneFormatVersion << "\r\n"
		<< "PrefabFormatVersion|" << settings.prefabFormatVersion << "\r\n"
		<< "RequiredScriptApiVersion|" << settings.requiredScriptApiVersion << "\r\n";
	return WriteAtomic(GetMetadataPath(projectRoot), text.str(), error);
}

ProjectCompatibilityResult ProjectVersionManager::Evaluate(const ProjectVersionSettings& settings) {
	const EngineVersion current = GetManoEngineVersion();
	if (settings.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned && settings.requiredEngineVersion != current)
		return {current < settings.requiredEngineVersion ? ProjectCompatibilityStatus::NeedsEngineUpdate : ProjectCompatibilityStatus::PinnedEngineMismatch,
			"ProjectはEngine " + settings.requiredEngineVersion.ToString() + "に固定されています。現在: " + current.ToString(), false, false};
	if (current < settings.requiredEngineVersion)
		return {ProjectCompatibilityStatus::NeedsEngineUpdate, "このProjectにはManoEngine " + settings.requiredEngineVersion.ToString() + "以上が必要です。現在: " + current.ToString(), false, false};
	// Projectの方が新しいScript APIを要求する場合は、このEngineでは開けない。Launcherで新しいEngineへ切り替える。
	if (settings.requiredScriptApiVersion > GetManoScriptApiVersion())
		return {ProjectCompatibilityStatus::ScriptApiMismatch, "ProjectのScript API " + std::to_string(settings.requiredScriptApiVersion) +
			"はこのEngineのScript API " + std::to_string(GetManoScriptApiVersion()) + "より新しいため開けません", false, false};
	// Projectの方が古い場合はFormat Versionと同じくMigrationで追従できる。Launcherへ案内して行き止まりにしない。
	if (settings.requiredScriptApiVersion < GetManoScriptApiVersion())
		return {ProjectCompatibilityStatus::NeedsMigration, "ProjectのScript API " + std::to_string(settings.requiredScriptApiVersion) +
			"をEngineのScript API " + std::to_string(GetManoScriptApiVersion()) + "へMigrationする必要があります。", true, false};
	if (settings.projectFormatVersion > GetManoProjectFormatVersion())
		return {ProjectCompatibilityStatus::ProjectTooNew, "Project Format " + std::to_string(settings.projectFormatVersion) + "はこのEditorでは未対応です。保存を拒否します。", false, false};
	if (settings.projectFormatVersion < GetManoProjectFormatVersion() ||
		settings.sceneFormatVersion < GetManoSceneFormatVersion() ||
		settings.prefabFormatVersion < GetManoPrefabFormatVersion())
		return {ProjectCompatibilityStatus::NeedsMigration, "Project DataをFormat " + std::to_string(GetManoProjectFormatVersion()) + "へMigrationする必要があります。", true, false};
	return {ProjectCompatibilityStatus::Compatible, "互換性があります", true, true};
}

bool ProjectVersionManager::MigrateScene(const std::filesystem::path& path, std::uint32_t oldVersion,
	std::uint32_t newVersion, std::string& error) { return MigrateAssetHeader(path, "Scene", oldVersion, newVersion, error); }
bool ProjectVersionManager::MigratePrefab(const std::filesystem::path& path, std::uint32_t oldVersion,
	std::uint32_t newVersion, std::string& error) { return MigrateAssetHeader(path, "Prefab", oldVersion, newVersion, error); }

bool ProjectVersionManager::MigrateProject(const std::filesystem::path& projectRoot, std::string& resultMessage) {
	ProjectVersionSettings oldSettings{};
	std::string loadError;
	const bool hasMetadata = Load(projectRoot, oldSettings, loadError);
	if (!hasMetadata) oldSettings = {};
	if (oldSettings.projectFormatVersion > GetManoProjectFormatVersion()) {
		resultMessage = "Migration Failed: Projectの方が新しいため変更しません"; return false;
	}

	const std::filesystem::path backupRoot = projectRoot / "Library" / "MigrationBackups" / CreateBackupFolderName();
	std::vector<std::pair<std::filesystem::path, std::filesystem::path>> backups;
	std::vector<std::filesystem::path> assets;
	std::error_code fileError;
	const std::filesystem::path assetsRoot = projectRoot / "Assets";
	if (std::filesystem::exists(assetsRoot, fileError)) {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot,
			std::filesystem::directory_options::skip_permission_denied, fileError)) {
			if (entry.is_regular_file(fileError) && (entry.path().extension() == ".scene" || entry.path().extension() == ".prefab")) assets.push_back(entry.path());
		}
	}
	if (hasMetadata) assets.push_back(GetMetadataPath(projectRoot));
	for (const auto& source : assets) {
		const auto relative = std::filesystem::relative(source, projectRoot, fileError);
		const auto backup = backupRoot / relative;
		std::filesystem::create_directories(backup.parent_path(), fileError);
		std::filesystem::copy_file(source, backup, std::filesystem::copy_options::overwrite_existing, fileError);
		if (fileError) { resultMessage = "Migration Failed: Backup作成失敗 " + source.generic_string(); return false; }
		backups.emplace_back(source, backup);
	}

	std::string migrationError;
	bool succeeded = true;
	for (const auto& path : assets) {
		if (path == GetMetadataPath(projectRoot)) continue;
		const bool prefab = path.extension() == ".prefab";
		const std::uint32_t oldVersion = ReadFormatVersion(path, prefab ? "Prefab" : "Scene");
		if (oldVersion == UINT32_MAX || !(prefab
			? MigratePrefab(path, oldVersion, GetManoPrefabFormatVersion(), migrationError)
			: MigrateScene(path, oldVersion, GetManoSceneFormatVersion(), migrationError))) { succeeded = false; break; }
		if (!IsAssetFormatSupported(path, prefab, &migrationError)) { succeeded = false; break; }
	}
	ProjectVersionSettings current = CreateCurrentDefaults();
	current.engineVersionPolicy = hasMetadata ? oldSettings.engineVersionPolicy : ProjectEngineVersionPolicy::Minimum;
	current.updateChannel = hasMetadata ? oldSettings.updateChannel : GetManoEngineUpdateChannel();
	if (succeeded) succeeded = Save(projectRoot, current, migrationError);
	if (!succeeded) {
		for (const auto& [target, backup] : backups) {
			fileError.clear();
			std::filesystem::copy_file(backup, target, std::filesystem::copy_options::overwrite_existing, fileError);
		}
		if (!hasMetadata) std::filesystem::remove(GetMetadataPath(projectRoot), fileError);
		resultMessage = "Migration Failed: " + migrationError + "。元データを復元しました";
		return false;
	}
	resultMessage = "Migration成功。Backup: " + backupRoot.generic_string();
	return true;
}

bool ProjectVersionManager::IsAssetFormatSupported(const std::filesystem::path& path, bool prefab, std::string* error) {
	const std::uint32_t found = ReadFormatVersion(path, prefab ? "Prefab" : "Scene");
	const std::uint32_t supported = prefab ? GetManoPrefabFormatVersion() : GetManoSceneFormatVersion();
	if (found == UINT32_MAX || found > supported) {
		if (error != nullptr) *error = std::string(prefab ? "Prefab" : "Scene") + " Format " +
			(found == UINT32_MAX ? "Invalid" : std::to_string(found)) + "は未対応です";
		return false;
	}
	return true;
}

void ProjectVersionManager::SetCurrentProjectWriteAllowed(bool allowed) { g_projectWriteAllowed.store(allowed); }
bool ProjectVersionManager::IsCurrentProjectWriteAllowed() { return g_projectWriteAllowed.load(); }
