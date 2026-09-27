#pragma once

#include "AssetRegistry.h"
#include "AssetType.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

// Reimport要求やEditor起動直後に、Assetが今どの状態にあるかを表す。
// Project Window / Inspectorはこの値だけを見てPASS/FAILの表示を切り替えられる。
enum class AssetImportState : int32_t {
	Imported = 0,   // 最新のSourceでImport済み。
	NeedsReimport,  // Source変更を検知した、またはImporter Versionが古く、まだ反映されていない。
	Importing,      // Reimport実行中(現状は同期実行のみだが、将来の非同期化に備えて区別する)。
	Failed,         // Import処理そのものが失敗した(File破損・Parse失敗等)。
	MissingSource,  // Source Fileが見つからない。
	Unsupported,    // このAssetTypeにはImport Settingsが無い、または拡張子を判定できない。
};

// Model(FBX/OBJ)用のImport設定。既存Loaderが安全に対応できる範囲だけを持つ
// (Tangent生成は現在のVertexDataにTangentスロットが無く、Rendererへ安全に反映できないため
// 意図的に含めない)。
struct ModelImportSettings {
	float importScale = 1.0f;      // Import時に頂点座標へ掛ける倍率。
	bool generateNormals = false;  // trueならFile内Normalを無視し、三角形から再計算する。
	bool flipUVs = false;          // trueならUV.yを反転する。
	bool importAnimation = true;   // FBX Animationも読み込むか(既存includeAnimation引数と対応)。
};

// Texture用のImport設定。Wrap Mode / Filter ModeはRendererのSamplerがShader単位の
// Static Samplerで共有されており、Texture単位へ安全に反映する経路が無いため含めない。
struct TextureImportSettings {
	bool srgb = true;             // trueならsRGBとして読み込む(Base Color等)。
	bool isNormalMap = false;     // trueならsrgb設定を無視し、常にLinearとして読み込む。
	bool generateMipmaps = true;
	int32_t maxSize = 0;          // 0 = 制限なし。指定した場合、長辺がこれを超えないようDownscaleする。
};

// Audio用のImport設定。Streamingは現在のXAudio2再生経路が全体読込前提のため、
// 大規模な再構築なしには安全に実装できず含めない。
struct AudioImportSettings {
	bool preloadOnPlayStart = false;  // trueならPlay開始時に一度先読みし、初回再生の遅延を減らす。
};

// PropertyAnimationClip自体がname/durationSeconds/loopを持つ自己記述的Assetのため、
// 現状Animation固有のImport設定は無い。将来の拡張点として構造体だけ用意する。
struct AnimationImportSettings {
	int32_t reserved = 0;
};

struct AssetImportMetadata {
	AssetId assetId;
	AssetType assetType = AssetType::Unknown;
	int32_t importerVersion = 0;
	std::string sourceHash;  // 最後にImportへ成功した時点のAssetManager::GetHash結果。
	AssetImportState state = AssetImportState::Imported;
	std::string lastErrorReason;

	ModelImportSettings model;
	TextureImportSettings texture;
	AudioImportSettings audio;
	AnimationImportSettings animation;
};

// 各Importerの現在仕様を表すVersion。仕様(既定値やImport時の変換内容)を変えたら
// ここを上げる。保存済みimporterVersionと不一致ならNeedsReimportとして扱う。
constexpr int32_t kCurrentModelImporterVersion = 1;
constexpr int32_t kCurrentTextureImporterVersion = 1;
constexpr int32_t kCurrentAudioImporterVersion = 1;
constexpr int32_t kCurrentAnimationImporterVersion = 1;

int32_t GetCurrentImporterVersion(AssetType assetType);

// AssetId単位でImport設定・状態を永続化する。AssetRegistry.txtはPath<->AssetId対応だけを
// 扱う設計のため、Asset種別ごとに列数が異なるImport設定を混ぜず、ProjectSettings配下へ
// 別Fileとして持つ。Keyは常にAssetId(Pathではない)にするため、Move/Renameで
// AssetIdが維持されればImport設定もそのまま残る。
class AssetImportSettingsStore {
public:
	static AssetImportSettingsStore& Get();

	AssetImportSettingsStore(const AssetImportSettingsStore&) = delete;
	AssetImportSettingsStore& operator=(const AssetImportSettingsStore&) = delete;

	void Load();
	void Save() const;

	// 既存Entryを返す。無ければAssetTypeに応じた既定値で新規作成する(assetIdが空なら失敗としてnullptrを返す)。
	AssetImportMetadata* GetOrCreate(const AssetId& assetId, AssetType assetType);
	const AssetImportMetadata* Find(const AssetId& assetId) const;

	void MarkImported(const AssetId& assetId, const std::string& sourceHash);
	void MarkFailed(const AssetId& assetId, const std::string& reason);
	void MarkMissingSource(const AssetId& assetId);
	void MarkNeedsReimport(const AssetId& assetId);

	void ResetToDefault(const AssetId& assetId, AssetType assetType);

private:
	AssetImportSettingsStore() = default;

	std::unordered_map<AssetId, AssetImportMetadata> entries_;
	bool isLoaded_ = false;
};

#pragma warning(pop)
