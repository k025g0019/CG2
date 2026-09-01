#pragma once

// 汎用ログ・監視システムの「System」対象(GameObjectに属さないManager/Global State)を
// 提供するProvider一覧。
//
// 拡張方法: 新しいSystem値を追加したい場合は、対象Managerに読み取り専用の軽量Getterを
// 1つ足し、EditorLogSystemProviders.cpp の kFixedSystemFieldProviders へ1エントリ追加する
// だけでよい。対象Managerの内部ロジックは一切変更しない。
// Profiler由来のCPU時間はEditorProfilerManagerの既存カテゴリをそのまま列挙するため、
// EditorRuntimeManager.cpp のprofileUpdate呼び出しが増えるとここも自動で追従する。

#include <string>
#include <utility>
#include <vector>

class EditorScene;
class EditorProfilerManager;
class EditorWeaponManager;
class EditorRuntimePropertyManager;

struct LogSystemFieldContext {
	EditorScene* editorScene = nullptr;
	EditorProfilerManager* profilerManager = nullptr;
	EditorWeaponManager* weaponManager = nullptr;
	EditorRuntimePropertyManager* runtimePropertyManager = nullptr;
};

// 選択可能なSystem Fieldの一覧を {category, name} で返す(UI表示用)。
std::vector<std::pair<std::string, std::string>> ListLogSystemFields(const LogSystemFieldContext& context);

// UI表示用の日本語名。Logファイルの列と保存済みPresetの互換を壊さないため、
// 内部キー(category/name)は英語のまま変更せず、表示だけを日本語へ差し替える。
const char* GetLogSystemCategoryDisplayName(const std::string& category);
const char* GetLogSystemFieldDisplayName(const std::string& category, const std::string& name);

// 指定したcategory/nameの現在値を文字列化して返す。対象が見つからなければfalse。
bool ResolveLogSystemFieldValue(
	const LogSystemFieldContext& context,
	const std::string& category,
	const std::string& name,
	std::string& outValue);
