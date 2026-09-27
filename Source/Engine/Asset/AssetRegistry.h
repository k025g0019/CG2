#pragma once

#include "AssetType.h"
#include "AssetManager.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Assetの永続識別子。Pathが変わっても同じ実体を指し続けるために使う
// (例: Assets/Models/Ship.fbx を Assets/Models/Vehicle/Ship.fbx へ移動しても、
// AssetIdを保持している側は参照を追える)。UUID文字列そのもの。
using AssetId = std::string;

enum class AssetLoadState : int32_t {
	Unknown = 0,   // Registryにまだ記録がない
	NotLoaded,     // 記録はあるが、対応するManager側Cacheへ読み込まれたことをまだ確認していない
	Loaded,        // AssetManager::NotifyFileChangedがAppliedを返したことがある
};

// Asset から Asset への参照1本。Pathは「Asset内に書かれている参照文字列」、
// idは「その時点でRegistryが解決できたAssetId」。idが空なら参照先が存在しない(Missing)。
// Move/Rename後もidを持っている側は追跡でき、id未解決のものだけをMissingとして表示できる。
struct AssetDependencyLink {
	std::string path;
	AssetId id;
};

struct AssetRecord {
	AssetId id;
	std::string path;
	AssetType type = AssetType::Unknown;
	std::string hash;
	std::vector<std::string> dependencies;  // 参照先Pathの一覧(抽出そのままの形)。
	std::vector<AssetDependencyLink> dependencyLinks;  // 上をAssetIdまで解決した形。Missing判定に使う。
	std::int64_t lastModifiedTime = 0;
	AssetLoadState loadState = AssetLoadState::Unknown;
	// 直近のAssetManager::NotifyFileChanged結果。RequiresManualAction等の理由をUIへ出す用途。
	AssetReloadResult lastReloadResult = AssetReloadResult::Unknown;
	std::string lastReloadReason;
};

// プロジェクト内Assetの状態(Path/Type/Hash/依存関係/更新時刻/読み込み状態)を把握する層。
// AssetManagerが「要求が来たら処理する」役割なのに対し、AssetRegistryは「今何がある状態か」を
// 保持する。Engine層に置き、Editor層には依存しない。
//
// 永続化するのはPath→AssetId対応だけにしている。Hash/依存関係/更新時刻はセッションごとに
// AssetManager経由で再計算するため、永続化データが実ファイルとズレて古い情報を返す心配がない。
class AssetRegistry {
public:
	static AssetRegistry& Get();

	AssetRegistry(const AssetRegistry&) = delete;
	AssetRegistry& operator=(const AssetRegistry&) = delete;

	// Path→AssetId対応をFileから読み込む。未呼び出しでも他のAPIは空の状態から動作する。
	void Load();
	void Save() const;

	// 新規File追加の通知。既にPathを知っていれば既存Recordを返す(二重登録しない)。
	const AssetRecord* NotifyAssetAdded(const std::string& path);
	// 既存Fileの内容変更通知。Hash・依存関係・更新時刻を計算し直す。Path未登録なら追加として扱う。
	const AssetRecord* NotifyAssetChanged(const std::string& path);
	// File削除の通知。AssetId自体もRegistryから失う(移動ではなく削除の場合はIDを使い回さない)。
	void NotifyAssetRemoved(const std::string& path);
	// 既知のAssetIdをPathだけ差し替えて維持する。呼び出し側が「移動である」と分かっている場合に使う
	// (RefreshFromDiskは移動を自動推測しない。同一内容でも別Assetとして先に登録される可能性がある)。
	const AssetRecord* NotifyAssetMoved(const std::string& oldPath, const std::string& newPath);

	// AssetManager::NotifyFileChangedの結果をRecordへ反映する。
	void UpdateReloadState(const std::string& path, const AssetNotifyResult& result);

	const AssetRecord* FindByPath(const std::string& path) const;
	const AssetRecord* FindById(const AssetId& id) const;
	std::vector<const AssetRecord*> GetAllRecords() const;

	//================================================================
	// 依存関係
	//================================================================
	// 参照Pathをその時点のAssetIdへ解決し直し、逆引き索引を作り直す。
	// Path変更(Move/Rename)や新規追加の後に呼ぶ。索引が汚れていれば各Getterが自動で呼ぶ。
	void RebuildDependencyIndex();
	// このAssetが参照しているAsset(Forward)。idが空の要素は参照先が見つからないMissing。
	std::vector<AssetDependencyLink> GetForwardDependencies(const AssetId& id) const;
	// このAssetを参照しているAsset(Reverse)。削除前の影響確認に使う。
	std::vector<AssetId> GetReverseDependencies(const AssetId& id) const;
	// このAssetが参照している中で、実体を解決できなかったPathだけを返す。
	std::vector<std::string> GetMissingDependencies(const AssetId& id) const;
	// 1つのAssetだけ参照抽出をやり直す(Reimport直後など)。
	void RefreshDependencies(const std::string& path);

	// Assets/ resources/ 配下を実際に走査し、新規・更新・消失をまとめて反映する。
	// Project Window等が起動時や明示的な再スキャン操作から呼ぶことを想定している
	// (常時Pollingする常駐Watcherではない)。
	void RefreshFromDisk();

private:
	AssetRegistry() = default;

	AssetRecord& GetOrCreateRecord(const std::string& path);
	void RefreshRecordFromFile(AssetRecord& record) const;
	static AssetId GenerateAssetId();

	std::unordered_map<std::string, AssetId> pathToId_;
	std::unordered_map<AssetId, AssetRecord> records_;
	// 参照先AssetId -> それを参照しているAssetIdの一覧。RebuildDependencyIndexが作り直す。
	std::unordered_map<AssetId, std::vector<AssetId>> reverseDependencies_;
	bool isLoaded_ = false;
	mutable bool isDependencyIndexDirty_ = true;
};
