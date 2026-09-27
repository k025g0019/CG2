#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Assetの種別。拡張子から判定できない種別を増やす余地は DetermineAssetTypeFromPath 側に
// 閉じ込め、この列挙自体は「AssetManagerが扱える種別の集合」として最小限に保つ。
enum class AssetType : int32_t {
	Unknown = 0,
	Model,
	Texture,
	Audio,
	Vfx,
	Animation,
	Material,
	Prefab,
	InputAction,
	Script,
};

// pathの拡張子からAssetTypeを判定する。共同制作・Project・HotReloadなど呼び出し側ごとに
// 別々の拡張子判定ロジックを持たせないための唯一の入口。
AssetType DetermineAssetTypeFromPath(const std::string& path);

// ログ・エラーメッセージ表示用。
const char* ToString(AssetType assetType);

// Scene/Prefabのようなテキスト形式Assetの中身から "Assets/" "resources/" 配下への
// パス参照を抜き出す。共同制作のScene共同同期が保存前の編集中テキストにも使うため、
// File Pathではなくテキスト本体を直接受け取る形にしている
// (共同制作Manager内に同種の抽出処理を重複させないための共通実装)。
std::vector<std::string> ExtractProjectPathReferences(const std::string& text);
