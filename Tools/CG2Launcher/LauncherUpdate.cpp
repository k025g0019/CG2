#include "LauncherUpdate.h"

#include "HttpDownload.h"

#include "Source/Engine/Core/ProjectVersionManager.h"

#include <Windows.h>
#include <bcrypt.h>
#include <urlmon.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "urlmon.lib")

namespace {
	constexpr unsigned char kBom[] = {0xEFU, 0xBBU, 0xBFU};

	std::string TrimBom(std::string value) {
		if (value.size() >= 3U && static_cast<unsigned char>(value[0]) == kBom[0] &&
			static_cast<unsigned char>(value[1]) == kBom[1] && static_cast<unsigned char>(value[2]) == kBom[2]) value.erase(0U, 3U);
		return value;
	}

	std::vector<std::string> Split(const std::string& text, char separator) {
		std::vector<std::string> parts;
		std::size_t start = 0U;
		for (;;) {
			const std::size_t position = text.find(separator, start);
			parts.push_back(text.substr(start, position == std::string::npos ? position : position - start));
			if (position == std::string::npos) break;
			start = position + 1U;
		}
		return parts;
	}

	bool IsSafeRelativePath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
		for (const auto& part : path) if (part == ".." || part == ".") return false;
		return true;
	}

	bool WriteAtomic(const std::filesystem::path& path, const std::string& text, std::string& error) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) { error = ec.message(); return false; }
		auto temp = path; temp += ".tmp";
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) { error = "Cannot create " + temp.generic_string(); return false; }
		file.write(reinterpret_cast<const char*>(kBom), sizeof(kBom));
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.flush(); const bool ok = file.good(); file.close();
		if (!ok) { error = "Write failed: " + temp.generic_string(); return false; }
		if (!MoveFileExW(temp.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			error = "Atomic switch failed: Win32 " + std::to_string(GetLastError());
			std::filesystem::remove(temp, ec);
			return false;
		}
		return true;
	}

	bool SaveState(const std::filesystem::path& root, const LauncherState& state, std::string& error) {
		std::ostringstream text;
		text << "ManoLauncherState|1\r\nCurrentVersion|" << state.currentVersion
			<< "\r\nPreviousVersion|" << state.previousVersion << "\r\nChannel|"
			<< GetEngineUpdateChannelText(state.channel) << "\r\nManifestLocation|" << state.manifestLocation << "\r\n";
		return WriteAtomic(LauncherUpdate::GetLauncherStateDirectory(root) / "launcher.state", text.str(), error);
	}

	bool UsesLegacyLayout(const std::filesystem::path& root) {
		std::error_code ec;
		return std::filesystem::exists(root / "launcher.state", ec) &&
			!std::filesystem::exists(root / "LauncherState" / "launcher.state", ec);
	}

	bool CopyTree(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error) {
		std::error_code ec;
		if (!std::filesystem::exists(source, ec)) return true;
		std::filesystem::create_directories(destination, ec);
		for (const auto& entry : std::filesystem::recursive_directory_iterator(source,
			std::filesystem::directory_options::skip_permission_denied, ec)) {
			if (ec) { error = ec.message(); return false; }
			const auto relative = std::filesystem::relative(entry.path(), source, ec);
			const auto target = destination / relative;
			if (entry.is_directory(ec)) std::filesystem::create_directories(target, ec);
			else if (entry.is_regular_file(ec)) {
				std::filesystem::create_directories(target.parent_path(), ec);
				std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
			}
			if (ec) { error = "Copy failed: " + entry.path().generic_string(); return false; }
		}
		return true;
	}

	std::filesystem::path ResolveSource(const EngineUpdateManifest& manifest,
		const std::filesystem::path& manifestPath, const std::filesystem::path& relative) {
		if (manifest.baseUrl.empty()) return manifestPath.parent_path() / relative;
		return std::filesystem::path(manifest.baseUrl) / relative;
	}

	bool DownloadFile(const std::string& baseUrl, const std::filesystem::path& manifestPath,
		const std::filesystem::path& relative, const std::filesystem::path& destination, std::string& error) {
		std::error_code ec;
		std::filesystem::create_directories(destination.parent_path(), ec);
		if (ec) { error = ec.message(); return false; }
		if (baseUrl.starts_with("http://") || baseUrl.starts_with("https://")) {
			std::string url = baseUrl;
			if (!url.empty() && url.back() != '/') url.push_back('/');
			url += relative.generic_string();
			std::string downloadError;
			if (!DownloadHttpFile(url, destination, downloadError)) {
				error = "Download failed: " + url + " (" + downloadError + ")"; return false;
			}
			return true;
		}
		const std::filesystem::path source = baseUrl.empty()
			? manifestPath.parent_path() / relative : std::filesystem::path(baseUrl) / relative;
		std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) { error = "Download/copy failed: " + source.generic_string() + " (" + ec.message() + ")"; return false; }
		return true;
	}
}

std::string LauncherUpdate::CalculateSha256(const std::filesystem::path& path, std::string& error) {
	error.clear();
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) { error = "Cannot read " + path.generic_string(); return {}; }
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	DWORD objectSize = 0U, resultSize = 0U, hashSize = 0U;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0 ||
		BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0U) < 0 ||
		BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &resultSize, 0U) < 0) {
		error = "SHA-256 initialization failed"; if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0U); return {};
	}
	std::vector<UCHAR> object(objectSize), digest(hashSize);
	if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0U, 0U) < 0) {
		error = "SHA-256 create failed"; BCryptCloseAlgorithmProvider(algorithm, 0U); return {};
	}
	// Keep the 1 MiB streaming buffer off the executable's 1 MiB default stack.
	std::vector<char> buffer(1024U * 1024U);
	while (file) {
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const auto count = file.gcount();
		if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0U) < 0) {
			error = "SHA-256 read failed"; break;
		}
	}
	if (error.empty() && BCryptFinishHash(hash, digest.data(), hashSize, 0U) < 0) error = "SHA-256 finish failed";
	BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0U);
	if (!error.empty()) return {};
	std::ostringstream output; output << std::hex << std::setfill('0');
	for (UCHAR byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
	return output.str();
}

bool LauncherUpdate::LoadManifest(const std::filesystem::path& path, EngineUpdateManifest& manifest, std::string& error) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) { error = "Manifestを読めません: " + path.generic_string(); return false; }
	EngineUpdateManifest loaded{}; bool hasVersion = false; std::string line;
	while (std::getline(file, line)) {
		line = TrimBom(std::move(line)); if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto parts = Split(line, '|'); if (parts.size() < 2U) continue;
		try {
			if (parts[0] == "EngineVersion") hasVersion = EngineVersion::TryParse(parts[1], loaded.version);
			else if (parts[0] == "Channel" && !TryParseEngineUpdateChannel(parts[1], loaded.channel)) throw std::runtime_error("channel");
			else if (parts[0] == "RequiredProjectFormat") loaded.requiredProjectFormat = std::stoul(parts[1]);
			else if (parts[0] == "ScriptApiVersion") loaded.scriptApiVersion = std::stoul(parts[1]);
			else if (parts[0] == "BaseUrl") loaded.baseUrl = parts[1];
			else if (parts[0] == "File" && parts.size() >= 4U) {
				EngineManifestFile item{std::filesystem::path(parts[1]), std::stoull(parts[2]), parts[3]};
				if (!IsSafeRelativePath(item.relativePath) || item.sha256.size() != 64U) throw std::runtime_error("file");
				loaded.files.push_back(std::move(item));
			} else if (parts[0] == "RemovedFile" && IsSafeRelativePath(parts[1])) loaded.removedFiles.emplace_back(parts[1]);
		} catch (...) { error = "Manifest行が不正です: " + line; return false; }
	}
	if (!hasVersion || loaded.files.empty()) { error = "ManifestにVersionまたはFilesがありません"; return false; }
	manifest = std::move(loaded); return true;
}

bool LauncherUpdate::CreateManifest(const std::filesystem::path& packageDirectory,
	const std::filesystem::path& outputPath, const EngineUpdateManifest& settings, std::string& error) {
	std::ostringstream text;
	text << "ManoEngineUpdateManifest|1\r\nEngineVersion|" << settings.version.ToString()
		<< "\r\nChannel|" << GetEngineUpdateChannelText(settings.channel)
		<< "\r\nRequiredProjectFormat|" << settings.requiredProjectFormat << "\r\nBaseUrl|" << settings.baseUrl << "\r\n";
	// Script API is independent from Engine and Project format versions. Older manifests omit this optional line.
	text << "ScriptApiVersion|" << (settings.scriptApiVersion == 0U ? GetManoScriptApiVersion() : settings.scriptApiVersion) << "\r\n";
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(packageDirectory,
		std::filesystem::directory_options::skip_permission_denied, ec)) {
		if (!entry.is_regular_file(ec)) continue;
		const auto relative = std::filesystem::relative(entry.path(), packageDirectory, ec);
		const std::string hash = CalculateSha256(entry.path(), error); if (!error.empty()) return false;
		text << "File|" << relative.generic_string() << "|" << entry.file_size(ec) << "|" << hash << "\r\n";
	}
	for (const auto& removed : settings.removedFiles) text << "RemovedFile|" << removed.generic_string() << "\r\n";
	if (!WriteAtomic(outputPath, text.str(), error)) return false;
	error = "Manifest作成完了: " + outputPath.generic_string();
	return true;
}

bool LauncherUpdate::LoadState(const std::filesystem::path& root, LauncherState& state, std::string& error) {
	std::filesystem::path statePath = GetLauncherStateDirectory(root) / "launcher.state";
	std::error_code ec;
	if (!std::filesystem::exists(statePath, ec) && std::filesystem::exists(root / "launcher.state", ec)) {
		statePath = root / "launcher.state";
	}
	std::ifstream file(statePath, std::ios::binary);
	if (!file.is_open()) { error = "Engineは未導入です"; return false; }
	LauncherState loaded{}; std::string line;
	while (std::getline(file, line)) {
		line = TrimBom(std::move(line)); if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto parts = Split(line, '|'); if (parts.size() < 2U) continue;
		if (parts[0] == "CurrentVersion") loaded.currentVersion = parts[1];
		else if (parts[0] == "PreviousVersion") loaded.previousVersion = parts[1];
		else if (parts[0] == "Channel") TryParseEngineUpdateChannel(parts[1], loaded.channel);
		else if (parts[0] == "ManifestLocation") loaded.manifestLocation = parts[1];
	}
	if (loaded.currentVersion.empty()) { error = "launcher.stateが壊れています"; return false; }
	state = std::move(loaded); return true;
}

bool LauncherUpdate::Apply(const std::filesystem::path& manifestPath, const std::filesystem::path& installRoot,
	const std::filesystem::path& projectRoot, bool repairsCurrent, std::string& result, const LauncherProgress& progress) {
	if (progress) progress("Reading Manifest...");
	EngineUpdateManifest manifest{};
	if (!LoadManifest(manifestPath, manifest, result)) return false;
	LauncherState state{}; std::string stateError;
	const bool installed = LoadState(installRoot, state, stateError);
	ProjectVersionSettings project{}; std::string projectError;
	const bool hasProject = !projectRoot.empty() && ProjectVersionManager::Load(projectRoot, project, projectError);
	const EngineVersion target = manifest.version;
	if (hasProject) {
		if (project.updateChannel != manifest.channel) { result = "Project ChannelとManifest Channelが一致しません"; return false; }
		if (project.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned && project.requiredEngineVersion != target) {
			result = "ProjectはEngine " + project.requiredEngineVersion.ToString() + "に固定されています"; return false;
		}
		if (project.projectFormatVersion > manifest.requiredProjectFormat) {
			result = "ManifestのEngineはProject Formatに対応していません"; return false;
		}
	} else if (installed && state.channel != manifest.channel) {
		result = "選択ChannelとManifest Channelが一致しません"; return false;
	}
	const std::string versionText = target.ToString();
	const auto versionsRoot = GetEnginesDirectory(installRoot);
	const auto staging = installRoot / "Cache" / "Staging" / (versionText + ".pending");
	const auto targetDirectory = versionsRoot / versionText;
	std::error_code ec;
	if (progress) progress("Preparing Staging...");
	std::filesystem::create_directories(staging.parent_path(), ec);
	std::filesystem::remove_all(staging, ec);
	std::filesystem::create_directories(staging, ec);
	if (ec) { result = "Stagingを作成できません: " + ec.message(); return false; }
	if (installed) {
		const auto currentDirectory = versionsRoot / state.currentVersion;
		if (!CopyTree(currentDirectory, staging, result)) { std::filesystem::remove_all(staging, ec); return false; }
	}
	for (const auto& removed : manifest.removedFiles) std::filesystem::remove_all(staging / removed, ec);
	std::size_t processedFileCount = 0U;
	for (const EngineManifestFile& item : manifest.files) {
		const auto destination = staging / item.relativePath;
		bool needsDownload = true;
		if (std::filesystem::exists(destination, ec) && std::filesystem::file_size(destination, ec) == item.size) {
			std::string hashError; needsDownload = CalculateSha256(destination, hashError) != item.sha256;
		}
		if (needsDownload && !DownloadFile(manifest.baseUrl, manifestPath, item.relativePath, destination, result)) {
			std::filesystem::remove_all(staging, ec); result = "Update中断。現在Versionは維持されました。" + result; return false;
		}
		std::string hashError;
		if (std::filesystem::file_size(destination, ec) != item.size || CalculateSha256(destination, hashError) != item.sha256) {
			std::filesystem::remove_all(staging, ec); result = "Hash Verify失敗。現在Versionは維持されました: " + item.relativePath.generic_string(); return false;
		}
		++processedFileCount;
		if (progress && (processedFileCount == manifest.files.size() || processedFileCount % 10U == 0U))
			progress("Downloading / Verifying " + std::to_string(processedFileCount) + " / " + std::to_string(manifest.files.size()));
	}
	if (progress) progress("Installing...");
	std::filesystem::path repairBackup;
	if (std::filesystem::exists(targetDirectory, ec)) {
		if (!repairsCurrent) { std::filesystem::remove_all(staging, ec); result = "Versionは既にInstalledです"; return false; }
		repairBackup = installRoot / ".repair" / (versionText + ".previous");
		std::filesystem::create_directories(repairBackup.parent_path(), ec);
		std::filesystem::remove_all(repairBackup, ec);
		std::filesystem::rename(targetDirectory, repairBackup, ec);
		if (ec) { std::filesystem::remove_all(staging, ec); result = "Repair切替準備に失敗しました"; return false; }
	}
	std::filesystem::create_directories(versionsRoot, ec);
	std::filesystem::rename(staging, targetDirectory, ec);
	if (ec) {
		if (!repairBackup.empty() && std::filesystem::exists(repairBackup)) {
			std::error_code restoreError;
			std::filesystem::rename(repairBackup, targetDirectory, restoreError);
		}
		result = "Version切替に失敗しました。現在Versionは維持されました";
		return false;
	}
	LauncherState next = installed ? state : LauncherState{};
	if (next.currentVersion != versionText) next.previousVersion = next.currentVersion;
	next.currentVersion = versionText; next.channel = manifest.channel; next.manifestLocation = manifestPath.generic_string();
	if (!SaveState(installRoot, next, result)) return false;
	const auto installedManifest = GetInstalledManifestPath(installRoot, target);
	std::filesystem::create_directories(installedManifest.parent_path(), ec);
	const bool sameManifest = std::filesystem::exists(installedManifest, ec) &&
		std::filesystem::equivalent(manifestPath, installedManifest, ec);
	if (!sameManifest) {
		ec.clear();
		std::filesystem::copy_file(manifestPath, installedManifest,
			std::filesystem::copy_options::overwrite_existing, ec);
	}
	if (ec) {
		result = "Engineは導入されましたがInstalled Manifestを保存できません: " + ec.message();
		return false;
	}
	result = std::string(installed ? repairsCurrent ? "Repair完了: " : "Update完了: " : "Install完了: ") + versionText;
	return true;
}

bool LauncherUpdate::InstallOfflinePackage(const std::filesystem::path& packageDirectory,
	const std::filesystem::path& installRoot, std::string& result) {
	const auto manifestPath = packageDirectory / "engine.manifest";
	EngineUpdateManifest manifest{};
	if (!LoadManifest(manifestPath, manifest, result)) return false;

	const std::string versionText = manifest.version.ToString();
	const auto targetDirectory = GetEnginesDirectory(installRoot) / versionText;
	std::error_code ec;
	if (std::filesystem::exists(targetDirectory, ec)) {
		result = "Engine " + versionText + " は既に導入済みです";
		return false;
	}

	// ZIP内のfileをそのまま本番へ置かず、全Hash確認が終わるまで専用Stagingに隔離する。
	const auto staging = installRoot / "Cache" / "Offline" / (versionText + ".pending");
	std::filesystem::create_directories(staging.parent_path(), ec);
	std::filesystem::remove_all(staging, ec);
	std::filesystem::create_directories(staging, ec);
	if (ec) { result = "Offline導入用Stagingを作成できません: " + ec.message(); return false; }

	for (const EngineManifestFile& item : manifest.files) {
		const auto source = packageDirectory / item.relativePath;
		const auto destination = staging / item.relativePath;
		if (!std::filesystem::exists(source, ec) || !std::filesystem::is_regular_file(source, ec) ||
			std::filesystem::file_size(source, ec) != item.size) {
			std::filesystem::remove_all(staging, ec);
			result = "Portable Engine ZIPのfileが不足または破損しています: " + item.relativePath.generic_string();
			return false;
		}
		std::string hashError;
		if (CalculateSha256(source, hashError) != item.sha256) {
			std::filesystem::remove_all(staging, ec);
			result = "Portable Engine ZIPのHashが一致しません: " + item.relativePath.generic_string();
			return false;
		}
		std::filesystem::create_directories(destination.parent_path(), ec);
		std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) {
			std::filesystem::remove_all(staging, ec);
			result = "Offline Engineの展開に失敗しました: " + item.relativePath.generic_string();
			return false;
		}
	}

	std::filesystem::create_directories(targetDirectory.parent_path(), ec);
	std::filesystem::rename(staging, targetDirectory, ec);
	if (ec) { std::filesystem::remove_all(staging, ec); result = "Offline Engineの導入切替に失敗しました"; return false; }

	LauncherState oldState{};
	std::string stateError;
	const bool hadState = LoadState(installRoot, oldState, stateError);
	LauncherState next = hadState ? oldState : LauncherState{};
	if (next.currentVersion != versionText) next.previousVersion = next.currentVersion;
	// Offline packageを選んだ利用者の意図を尊重し、導入した版を現在版にする。
	next.currentVersion = versionText;
	next.channel = manifest.channel;
	const auto installedManifest = GetInstalledManifestPath(installRoot, manifest.version);
	next.manifestLocation = installedManifest.generic_string();
	std::filesystem::create_directories(installedManifest.parent_path(), ec);
	std::filesystem::copy_file(manifestPath, installedManifest,
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		std::filesystem::remove_all(targetDirectory, ec);
		result = "Offline EngineのManifest保存に失敗したため導入を取り消しました";
		return false;
	}
	if (!SaveState(installRoot, next, result)) {
		std::filesystem::remove_all(targetDirectory, ec);
		std::filesystem::remove(installedManifest, ec);
		return false;
	}
	result = "Offline Engine導入完了: " + versionText;
	return true;
}

bool LauncherUpdate::Verify(const std::filesystem::path& manifestPath, const std::filesystem::path& root, std::string& result) {
	EngineUpdateManifest manifest{}; if (!LoadManifest(manifestPath, manifest, result)) return false;
	LauncherState state{}; if (!LoadState(root, state, result)) return false;
	const auto directory = GetEnginesDirectory(root) / manifest.version.ToString();
	for (const auto& item : manifest.files) {
		const auto path = directory / item.relativePath; std::error_code ec; std::string hashError;
		if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) != item.size || CalculateSha256(path, hashError) != item.sha256) {
			result = "Verify失敗: " + item.relativePath.generic_string(); return false;
		}
	}
	for (const auto& removed : manifest.removedFiles) {
		std::error_code ec;
		if (std::filesystem::exists(directory / removed, ec)) {
			result = "Verify失敗(RemovedFile残存): " + removed.generic_string();
			return false;
		}
	}
	result = "Verify成功: " + state.currentVersion; return true;
}

bool LauncherUpdate::Rollback(const std::filesystem::path& root, std::string& result) {
	LauncherState state{}; if (!LoadState(root, state, result)) return false;
	if (state.previousVersion.empty() || !std::filesystem::exists(GetEnginesDirectory(root) / state.previousVersion)) {
		result = "Rollback可能な1世代前Versionがありません"; return false;
	}
	std::swap(state.currentVersion, state.previousVersion);
	if (!SaveState(root, state, result)) return false;
	result = "Rollback完了: " + state.currentVersion; return true;
}

bool LauncherUpdate::SetChannel(const std::filesystem::path& root, EngineUpdateChannel channel, std::string& result) {
	LauncherState state{};
	if (!LoadState(root, state, result)) return false;
	state.channel = channel;
	if (!SaveState(root, state, result)) return false;
	result = "Update Channel: " + std::string(GetEngineUpdateChannelText(channel));
	return true;
}

bool LauncherUpdate::OpenProject(const std::filesystem::path& root, const std::filesystem::path& projectRoot,
	std::string& result, const std::string& requestedVersion) {
	LauncherState state{}; if (!LoadState(root, state, result)) return false;
	ProjectVersionSettings project{}; std::string error;
	if (!ProjectVersionManager::Load(projectRoot, project, error)) { result = error; return false; }
	std::string selectedVersion = requestedVersion.empty() ? state.currentVersion : requestedVersion;
	if (project.engineVersionPolicy == ProjectEngineVersionPolicy::Pinned) {
		if (!requestedVersion.empty() && requestedVersion != project.requiredEngineVersion.ToString()) {
			result = "Project固定VersionとLauncher選択Versionが一致しません"; return false;
		}
		selectedVersion = project.requiredEngineVersion.ToString();
	} else {
		EngineVersion selected{};
		if (!EngineVersion::TryParse(selectedVersion, selected) || selected < project.requiredEngineVersion) {
			result = "選択EngineがProjectのRequired Versionを満たしません"; return false;
		}
	}
	auto executable = GetEnginesDirectory(root) / selectedVersion / "CG2.exe";
	if (!std::filesystem::exists(executable)) { result = "必要EngineがInstalledではありません: " + selectedVersion; return false; }
	std::wstring command = L"\"" + executable.wstring() + L"\" --project \"" + projectRoot.wstring() + L"\"";
	STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
	if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0U, nullptr,
		executable.parent_path().c_str(), &startup, &process)) { result = "Engineを起動できません"; return false; }
	CloseHandle(process.hThread);
	// CreateProcess成功はEditorの起動成功ではない。初期化直後に終了した場合は、
	// 成功ダイアログを出さず、終了コードとLogの場所を利用者へ返す。
	if (WaitForSingleObject(process.hProcess, 1500U) == WAIT_OBJECT_0) {
		DWORD exitCode = 0U;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		result = "Engineが初期化中に終了しました (終了コード " + std::to_string(exitCode) +
			")\nLog: " + (projectRoot / "logs").generic_string();
		return false;
	}
	CloseHandle(process.hProcess);
	result = "Project起動: Engine " + selectedVersion; return true;
}

std::filesystem::path LauncherUpdate::GetEnginesDirectory(const std::filesystem::path& installRoot) {
	return UsesLegacyLayout(installRoot) ? installRoot / "Versions" : installRoot / "Engines";
}

std::filesystem::path LauncherUpdate::GetLauncherStateDirectory(const std::filesystem::path& installRoot) {
	return UsesLegacyLayout(installRoot) ? installRoot : installRoot / "LauncherState";
}

std::filesystem::path LauncherUpdate::GetInstalledManifestPath(const std::filesystem::path& installRoot,
	const EngineVersion& version) {
	return GetLauncherStateDirectory(installRoot) / "Manifests" / (version.ToString() + ".manifest");
}
