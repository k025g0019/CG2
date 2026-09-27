#pragma once

#include "AssetType.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// NotifyFileChanged / Invalidate / Reload の結果。
// 「何もしていない」を暗黙のfalseで返さず、理由を区別できるようにする。
enum class AssetReloadResult : int32_t {
	Applied = 0,            // Cacheを無効化・更新した。または元々Cacheを持たず既に最新。
	RequiresManualAction,   // 安全に自動反映できない。理由はAssetNotifyResult::reasonへ入る(例: Script, Animation)。
	NoHandler,               // このAssetTypeのAdapterがまだ登録されていない(段階移行中)。
	Unknown,                 // 拡張子からAssetTypeを判定できなかった。
};

struct AssetNotifyResult {
	AssetReloadResult result = AssetReloadResult::Unknown;
	std::string reason;  // Applied以外の時、状況を人が読める形で残す。Appliedなら空でよい。
};

// 各Asset種別の実処理(既存Manager)への接続点。AssetManager自体はどのManagerも
// 直接#includeしない。呼び出し側(Editor層など)がここへ関数を登録することで接続する。
struct AssetTypeHandler {
	// trueを返した時だけAppliedとして扱う。falseならRequiresManualAction扱いになる。
	std::function<bool(const std::string& path)> reload;
	// Cacheだけ破棄して次回参照時に読み直させたい場合に使う。reloadと分けているのは、
	// 「今すぐ読み直す」か「参照されるまで遅延する」かで既存Managerの実装が違うため。
	std::function<void(const std::string& path)> invalidate;
	std::function<void(const std::string& path)> unload;
	std::function<std::vector<std::string>(const std::string& path)> getDependencies;
	// falseの場合、reloadは呼ばずRequiresManualActionをそのまま返す。
	// 安全な自動反映ができない種別(Script/Animation)を明示するためのフラグ。
	bool supportsHotReload = false;
	std::string unsupportedReason;
};

// Model / Texture / Audio / VFX / Animation / Material / Prefab / InputAction / Script の
// Load / Reload / Unload / Invalidate / 変更検知 / 依存関係 / Hash を共通化する。
// Engine層に置き、Editor層の各種Managerには依存しない。既存Managerとの接続は
// RegisterHandlerによるAdapter登録のみで行い、既存キャッシュを置き換えない。
class AssetManager {
public:
	static AssetManager& Get();

	AssetManager(const AssetManager&) = delete;
	AssetManager& operator=(const AssetManager&) = delete;

	void RegisterHandler(AssetType assetType, AssetTypeHandler handler);
	bool HasHandler(AssetType assetType) const;

	// 共同制作・HotReload・Project監視など、ファイル変更を知った側が呼ぶ唯一の入口。
	AssetNotifyResult NotifyFileChanged(const std::string& path);
	void Invalidate(const std::string& path);
	void Unload(const std::string& path);

	// Scene共同同期などが「このAssetは何を参照しているか」を得るための共通API。
	// Adapter未登録・非対応の種別は空を返す。
	std::vector<std::string> GetDependencies(const std::string& path) const;

	// ファイル内容からHashを求める。ファイルサイズ・更新日時が前回と同じなら再計算しない。
	// 不要な大容量Asset再送防止の比較に使う想定。
	std::string GetHash(const std::string& path);

	AssetType DetermineAssetType(const std::string& path) const;

private:
	AssetManager() = default;

	struct HashCacheEntry {
		std::string hash;
		std::uint64_t fileSize = 0u;
		std::int64_t lastWriteTime = 0;
	};

	std::unordered_map<AssetType, AssetTypeHandler> handlers_;
	std::unordered_map<std::string, HashCacheEntry> hashCache_;
};
