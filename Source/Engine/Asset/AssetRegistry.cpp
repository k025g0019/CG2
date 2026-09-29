#include "AssetRegistry.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <objbase.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#pragma comment(lib, "ole32.lib")

//========================================
// Asset Registry処理の構成
//========================================

// RegistryはPathと永続AssetIdの対応、Hash、依存関係、Reload状態を保持する。
// Pathを直接IDにしないことで、明示的なMove通知では参照Identityを維持できる。
// 保存FileにはPathとIDだけを残し、Hashや依存は現行Fileから再構築する。
namespace {
	constexpr const char* kAssetRegistryPath = "ProjectSettings/AssetRegistry.txt";
	// std::filesystem::pathはconstexprコンストラクタを持たないため、生文字列の配列にしている。
	constexpr const char* kScanRootNames[] = {"Assets", "resources"};

	// EditorTeamUuid.cpp(Editor層)と同じCoCreateGuidベースの実装。Engine層からEditor層へ
	// 依存を増やさないため、小さな関数を重複させている。
	std::string GenerateGuidText() {
		GUID guid{};

		if (FAILED(CoCreateGuid(&guid))) {
			return {};
		}

		std::array<char, 37> guidText{};
		const int32_t writtenLength = std::snprintf(
			guidText.data(),
			guidText.size(),
			"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
			guid.Data1,
			guid.Data2,
			guid.Data3,
			guid.Data4[0],
			guid.Data4[1],
			guid.Data4[2],
			guid.Data4[3],
			guid.Data4[4],
			guid.Data4[5],
			guid.Data4[6],
			guid.Data4[7]);

		return writtenLength == 36 ? std::string(guidText.data()) : std::string{};
	}
}

//========================================
// Registry読込・保存処理
//========================================

AssetId AssetRegistry::GenerateAssetId() {
	return GenerateGuidText();
}

AssetRegistry& AssetRegistry::Get() {
	static AssetRegistry instance;
	return instance;
}

void AssetRegistry::Load() {
	// 1 Session中の重複読込でMemory上の変更を上書きしないよう、一度だけ読む。
	if (isLoaded_) {
		return;
	}

	isLoaded_ = true;
	std::ifstream file(kAssetRegistryPath, std::ios::binary);

	if (!file.is_open()) {
		return;
	}

	std::string line;

	// 1行を「Project相対Path|AssetId」として復元する。不正行はRegistry全体を止めず読み飛ばす。
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		const std::size_t separatorPosition = line.find('|');

		if (separatorPosition == std::string::npos) {
			continue;
		}

		const std::string path = line.substr(0u, separatorPosition);
		const std::string id = line.substr(separatorPosition + 1u);

		if (path.empty() || id.empty() || pathToId_.contains(path)) {
			continue;
		}

		pathToId_[path] = id;
		AssetRecord record{};
		record.id = id;
		record.path = path;
		record.type = DetermineAssetTypeFromPath(path);
		records_[id] = std::move(record);
	}
}

void AssetRegistry::Save() const {
	const std::filesystem::path filePath(kAssetRegistryPath);
	std::error_code directoryError;
	std::filesystem::create_directories(filePath.parent_path(), directoryError);

	// 現在のRegistryを全件書き直す。途中失敗時のAtomic Replaceは今後の改善対象。
	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		return;
	}

	// Hash/依存関係/更新時刻は永続化しない(セッションごとに再計算するため)。
	// ここではPath→AssetId対応だけを書き出す。
	for (const auto& pathIdPair : pathToId_) {
		file << pathIdPair.first << "|" << pathIdPair.second << "\r\n";
	}
}

AssetRecord& AssetRegistry::GetOrCreateRecord(const std::string& path) {
	// 既存Pathには同じRecordを返し、通知のたびにAssetIdが変わることを防ぐ。
	const auto pathIterator = pathToId_.find(path);

	if (pathIterator != pathToId_.end()) {
		return records_[pathIterator->second];
	}

	AssetId newId = GenerateAssetId();

	if (newId.empty()) {
		// GUID生成に失敗した場合でもRegistryが機能を止めないよう、Pathそのものを
		// 代替IDとして使う(移動追跡はできないが、Path参照としては最低限機能する)。
		newId = path;
	}

	pathToId_[path] = newId;
	AssetRecord& record = records_[newId];
	record.id = newId;
	record.path = path;
	record.type = DetermineAssetTypeFromPath(path);
	return record;
}

void AssetRegistry::RefreshRecordFromFile(AssetRecord& record) const {
	//------------------------------
	// File由来情報更新
	//------------------------------

	// Hashと依存を現行Fileから再計算する。AssetIdはIdentityなので変更しない。
	std::error_code fileError;
	const bool exists = std::filesystem::exists(record.path, fileError);

	if (!exists) {
		record.loadState = AssetLoadState::Unknown;
		return;
	}

	record.type = DetermineAssetTypeFromPath(record.path);
	record.hash = AssetManager::Get().GetHash(record.path);
	record.dependencies = AssetManager::Get().GetDependencies(record.path);
	record.lastModifiedTime = static_cast<std::int64_t>(
		std::filesystem::last_write_time(record.path, fileError).time_since_epoch().count());

	// 参照先の解決は全Recordが揃ってからでないと正しくできないため、ここでは索引を汚すだけにする。
	isDependencyIndexDirty_ = true;
}

const AssetRecord* AssetRegistry::NotifyAssetAdded(const std::string& path) {
	// 追加通知時点でFile情報も読み、RegistryにPathだけの不完全Recordを残さない。
	AssetRecord& record = GetOrCreateRecord(path);
	RefreshRecordFromFile(record);

	if (record.loadState == AssetLoadState::Unknown) {
		record.loadState = AssetLoadState::NotLoaded;
	}

	return &record;
}

const AssetRecord* AssetRegistry::NotifyAssetChanged(const std::string& path) {
	// 既存Pathであれば更新、未登録なら追加として扱う。呼び出し側が
	// 「追加か変更か」を毎回判定せずに済むようにしている。
	return NotifyAssetAdded(path);
}

void AssetRegistry::NotifyAssetRemoved(const std::string& path) {
	const auto pathIterator = pathToId_.find(path);

	if (pathIterator == pathToId_.end()) {
		return;
	}

	// Path索引とID索引を対で削除する。片方だけ残すとFindByPath/FindByIdの結果が矛盾する。
	records_.erase(pathIterator->second);
	pathToId_.erase(pathIterator);
	isDependencyIndexDirty_ = true;
}

const AssetRecord* AssetRegistry::NotifyAssetMoved(
	const std::string& oldPath,
	const std::string& newPath) {
	const auto pathIterator = pathToId_.find(oldPath);

	if (pathIterator == pathToId_.end()) {
		// 移動元をRegistryが知らない場合は、新規追加として扱う
		// (何もない状態からIDを作る以外に安全な選択肢がない)。
		return NotifyAssetAdded(newPath);
	}

	// Moveでは同じAssetIdを新Pathへ付け替え、Prefab等が保持するID参照を維持する。
	const AssetId id = pathIterator->second;
	pathToId_.erase(pathIterator);
	pathToId_[newPath] = id;

	AssetRecord& record = records_[id];
	record.path = newPath;
	RefreshRecordFromFile(record);

	// Pathが変わると、このAssetを参照している側の「Path解決」が全てずれる可能性がある。
	// 逆引き索引はAssetId基準なので関係は保たれるが、Missing判定をやり直すため作り直す。
	isDependencyIndexDirty_ = true;
	return &record;
}

void AssetRegistry::UpdateReloadState(
	const std::string& path,
	const AssetNotifyResult& result) {
	const auto pathIterator = pathToId_.find(path);

	if (pathIterator == pathToId_.end()) {
		return;
	}

	AssetRecord& record = records_[pathIterator->second];
	// Diagnosticsが直近の反映結果と理由を表示できるよう、成功/手動対応/失敗をRecordへ残す。
	record.lastReloadResult = result.result;
	record.lastReloadReason = result.reason;
	record.loadState = result.result == AssetReloadResult::Applied
		? AssetLoadState::Loaded
		: record.loadState;
}

const AssetRecord* AssetRegistry::FindByPath(const std::string& path) const {
	const auto pathIterator = pathToId_.find(path);

	if (pathIterator == pathToId_.end()) {
		return nullptr;
	}

	const auto recordIterator = records_.find(pathIterator->second);
	return recordIterator != records_.end() ? &recordIterator->second : nullptr;
}

const AssetRecord* AssetRegistry::FindById(const AssetId& id) const {
	const auto recordIterator = records_.find(id);
	return recordIterator != records_.end() ? &recordIterator->second : nullptr;
}

std::vector<const AssetRecord*> AssetRegistry::GetAllRecords() const {
	std::vector<const AssetRecord*> allRecords;
	allRecords.reserve(records_.size());

	for (const auto& idRecordPair : records_) {
		allRecords.push_back(&idRecordPair.second);
	}

	return allRecords;
}

//========================================
// Project Asset全体走査処理
//========================================

void AssetRegistry::RefreshFromDisk() {
	Load();

	std::error_code iteratorError;

	// Assetsとresourcesを再帰走査し、実在FileのRecordを更新する。
	for (const char* scanRootName : kScanRootNames) {
		const std::filesystem::path scanRoot(scanRootName);

		if (!std::filesystem::exists(scanRoot, iteratorError)) {
			continue;
		}

		for (const std::filesystem::directory_entry& entry :
			std::filesystem::recursive_directory_iterator(
				scanRoot,
				std::filesystem::directory_options::skip_permission_denied,
				iteratorError)) {
			if (iteratorError) {
				iteratorError.clear();
				continue;
			}

			if (!entry.is_regular_file(iteratorError)) {
				continue;
			}

			const std::string assetPath = entry.path().lexically_normal().generic_string();

			// 移動の自動推測はしない(同一内容の別Fileを誤って同一Assetとみなす事故を避けるため)。
			// 新規Pathは新しいAssetIdとして登録する。
			AssetRecord& record = GetOrCreateRecord(assetPath);
			RefreshRecordFromFile(record);
		}
	}

	// 実在しないPathをまとめて除去する(前回セッションから削除されたFile分)。
	std::vector<std::string> missingPaths;

	for (const auto& pathIdPair : pathToId_) {
		if (!std::filesystem::exists(pathIdPair.first, iteratorError)) {
			missingPaths.push_back(pathIdPair.first);
		}
	}

	for (const std::string& missingPath : missingPaths) {
		NotifyAssetRemoved(missingPath);
	}

	// 全Recordが揃った後で依存PathをIDへ解決し、逆引き索引を作る。
	RebuildDependencyIndex();
	Save();
}

//========================================
// Asset依存索引再構築処理
//========================================

void AssetRegistry::RebuildDependencyIndex() {
	// reverseDependencies_は「このAssetを参照しているAsset」を高速に引くための逆索引。
	reverseDependencies_.clear();

	for (auto& idRecordPair : records_) {
		AssetRecord& record = idRecordPair.second;
		record.dependencyLinks.clear();
		record.dependencyLinks.reserve(record.dependencies.size());

		// Forward依存のPathをRegistryのAssetIdへ解決する。未登録PathはIDを空のまま残し、
		// Missing Dependencyとして利用者へ報告できるようにする。
		for (const std::string& dependencyPath : record.dependencies) {
			AssetDependencyLink link{};
			link.path = dependencyPath;

			const auto dependencyIterator = pathToId_.find(dependencyPath);
			if (dependencyIterator != pathToId_.end()) {
				link.id = dependencyIterator->second;
			}

			// 自分自身への参照は依存として数えない(Scene内に自分のPathが書かれている場合など)。
			if (link.id == record.id) {
				continue;
			}

			record.dependencyLinks.push_back(link);

			if (!link.id.empty()) {
				std::vector<AssetId>& dependents = reverseDependencies_[link.id];

				// 同じAssetを複数箇所から参照していても、参照元は1回だけ数える。
				if (std::find(dependents.begin(), dependents.end(), record.id) == dependents.end()) {
					dependents.push_back(record.id);
				}
			}
		}
	}

	isDependencyIndexDirty_ = false;
}

std::vector<AssetDependencyLink> AssetRegistry::GetForwardDependencies(const AssetId& id) const {
	// 遅延再構築により、複数File変更のたびに全索引を作り直すCostをまとめる。
	if (isDependencyIndexDirty_) {
		const_cast<AssetRegistry*>(this)->RebuildDependencyIndex();
	}

	const auto recordIterator = records_.find(id);
	return recordIterator != records_.end() ? recordIterator->second.dependencyLinks : std::vector<AssetDependencyLink>{};
}

std::vector<AssetId> AssetRegistry::GetReverseDependencies(const AssetId& id) const {
	if (isDependencyIndexDirty_) {
		const_cast<AssetRegistry*>(this)->RebuildDependencyIndex();
	}

	const auto dependentIterator = reverseDependencies_.find(id);
	return dependentIterator != reverseDependencies_.end() ? dependentIterator->second : std::vector<AssetId>{};
}

std::vector<std::string> AssetRegistry::GetMissingDependencies(const AssetId& id) const {
	std::vector<std::string> missingPaths;

	for (const AssetDependencyLink& link : GetForwardDependencies(id)) {
		if (link.id.empty()) {
			missingPaths.push_back(link.path);
		}
	}

	return missingPaths;
}

void AssetRegistry::RefreshDependencies(const std::string& path) {
	const auto pathIterator = pathToId_.find(path);

	if (pathIterator == pathToId_.end()) {
		return;
	}

	AssetRecord& record = records_[pathIterator->second];
	record.dependencies = AssetManager::Get().GetDependencies(path);
	RebuildDependencyIndex();
}
