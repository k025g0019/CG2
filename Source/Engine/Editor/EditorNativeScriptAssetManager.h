#pragma once

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorNativeScriptAssetResult {
	bool isSucceeded = false;  // 生成が最後まで成功したかどうか。
	std::string sanitizedScriptName;  // クラス名と DLL 名に使えるよう整形した名前。
	std::string scriptDirectoryPath;  // 生成した script フォルダ。
	std::string headerFilePath;  // 生成した .h の相対パス。
	std::string sourceFilePath;  // 生成した .cpp の相対パス。
	std::string generatedSourceFilePath;  // Engine ABI だけを持つ自動生成 .Generated.cpp の相対パス。
	std::string buildDebugFilePath;  // 生成した Debug ビルド bat の相対パス。
	std::string buildReleaseFilePath;  // 生成した Release ビルド bat の相対パス。
	std::string dllFilePath;  // Script Component に割り当てる想定 DLL パス。
	std::string message;  // GUI に出す成否メッセージ。
};

enum class EditorNativeScriptTemplate {
	Empty,
	PlayerController,
	RailPlayer,
	EnemyController,
	TurretController,
	HomingController,
	BossController,
	StageController,
	LoadoutController,
	PhysicsController,
	HealthDamageController,
	SpawnPoolController,
	CameraEffectsController,
	AnimationEffectController,
	AudioController,
	UiController,
	ActionEventController,
	SaveCheckpointController,
	OceanBuoyancyController,
	NavigationAiController,
	RuntimePropertyController,
	ScoreController,
	ComboController,
	StageResultController,
	RailEventController,
	SimulationLodController,
	Count,
};

struct EditorNativeScriptTemplateInfo {
	EditorNativeScriptTemplate type = EditorNativeScriptTemplate::Empty;
	const char* category = "基本";  // 選択UIでまとめるComponentカテゴリ。
	const char* displayName = "空のスクリプト";  // Inspectorへ表示する日本語名。
	const char* description = "最小構成から処理を書きます。";  // 生成される処理の用途。
	const char* recommendedComponents = "Script";  // 組み合わせる既存Componentの目安。
};

class EditorNativeScriptAssetManager {
public:
	static EditorNativeScriptAssetResult CreateNativeScriptAsset(
		const std::string& requestedScriptName,
		bool isDebugBuild,
		EditorNativeScriptTemplate scriptTemplate = EditorNativeScriptTemplate::Empty);  // 選択した用途別雛形をまとめて生成する。
	static int32_t GetTemplateCount();  // Inspectorへ公開するテンプレート数を返す。
	static const EditorNativeScriptTemplateInfo& GetTemplateInfo(int32_t templateIndex);  // 用途・推奨Componentを含む選択情報を返す。

private:
	static std::string SanitizeScriptName(const std::string& requestedScriptName);  // クラス名や DLL 名に使えない文字を除去する。
	static std::string MakeHeaderText(
		const std::string& scriptName,
		EditorNativeScriptTemplate scriptTemplate);  // DLL 側の状態クラスを生成する用途別 .h テンプレート。
	static std::string MakeSourceText(const std::string& scriptName, EditorNativeScriptTemplate scriptTemplate);  // EditorScriptApi を使う用途別 .cpp テンプレート。
	static std::string MakeGeneratedSourceText(const std::string& scriptName);  // ユーザーコードから分離した DLL ABI ブリッジ。
	static std::string MakeBuildScriptText(const std::string& scriptName, bool isDebug);  // DLL を cl /LD で作る bat テンプレート。
	static bool WriteUtf8BomFile(const std::string& filePath, const std::string& text);  // 文字化け防止のため UTF-8 BOM 付きで保存する。
};

#pragma warning(pop)
