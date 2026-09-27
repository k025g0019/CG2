#pragma once

#include "Vector.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Effect Definition (Effect Resource)
//================================================================
// 1つの EffectDefinition が複数の EffectNodeDefinition (Emitter) を持つ。
// 例: ExplosionLarge = Fire(Billboard+Flipbook) + Smoke(Billboard) + Spark(DirectionalBillboard) + Shockwave(Ring)
// コードへハードコードせず、Assets/Effects/*.effectdef (JSON) から読み込む。

// Stage1で描画パスを実装するNodeType、MeshParticleとDecalはStage2実装用に列挙のみ行う。
enum class EffectNodeType : int32_t {
	Billboard = 0,      // カメラ追従/固定軸/進行方向Billboard。Flipbookはこのnode上のフラグで有効化する。
	Ribbon = 1,         // 位置履歴から生成するRibbon / Trail。
	Ring = 2,           // 拡大しながらFadeするRing。
	MeshParticle = 3,   // Stage2: GPUインスタンシングMesh Particle。Stage1では描画スタブ。
	Decal = 4,          // Stage2: Decal投影。Stage1では描画スタブ。
};

enum class EffectBillboardMode : int32_t {
	CameraFacing = 0,   // 常にカメラを向くQuad。煙・炎・爆発・砂埃・水滴・泡向け。
	YAxisBillboard = 1, // Y軸だけ固定し、水平方向はカメラを向く。
	Directional = 2,    // Velocity/指定Directionへ伸びるQuad。曳光弾・火花向け。
	Fixed = 3,          // カメラ追従せず常に固定姿勢。
};

enum class EffectBlendMode : int32_t {
	AlphaBlend = 0,      // 半透明合成。Sorting対象。
	Additive = 1,        // 加算合成。Sorting省略可能。
	Premultiplied = 2,   // 拡張用。Stage1ではAlphaBlendと同じPSOにfallbackする。
	Multiply = 3,        // 拡張用。Stage1ではAlphaBlendと同じPSOにfallbackする。
};

// カメラ距離に応じてSpawn数を間引くLOD段階。distance未満ならこの段階のspawnMultiplierを使う。
struct EffectLodLevel {
	float distance = 50.0f;       // このLevelを適用する上限距離。
	float spawnMultiplier = 1.0f; // 0.0でSpawn無し(=Effect無し)、1.0で通常Spawn数。
};

// 1つのEmitter/Node設定。NodeTypeにより使うフィールドが変わる。
struct EffectNodeDefinition {
	std::string name;                                  // Inspector / DebugUI表示名。
	EffectNodeType nodeType = EffectNodeType::Billboard;
	std::string texturePath;                            // 既存ResourceManager経由でCacheするTexture Asset。
	EffectBlendMode blendMode = EffectBlendMode::AlphaBlend;
	bool useSoftParticle = false;                        // Stage2: 深度比較Fadeを有効にする(EffectPrimitive.PS.hlslで実装)。
	float softParticleFadeDistance = 1.0f;               // Soft Particle: Sceneとの距離差(World単位)がこれ未満でAlphaをFadeする。
	bool useGpuSimulation = false;                        // Stage2: BillboardのPosition/Velocity/LifetimeをEditorGpuParticleManager(Compute)側で更新する。
	std::string meshAssetPath;                            // Stage2: MeshParticleが参照するFBX/OBJ Asset(EditorGpuParticleManagerでGPUインスタンシング)。

	// Billboard / Flipbook / Directional共通。
	EffectBillboardMode billboardMode = EffectBillboardMode::CameraFacing;
	Vector3 direction{0.0f, 1.0f, 0.0f};                 // Directional Billboardの基準方向、または初速方向。
	float emissionRate = 10.0f;                          // 1秒あたりの継続発生数。
	int32_t burstCount = 0;                              // 再生開始時にまとめて発生する数。
	int32_t maxCount = 64;                                // このNodeが同時に保持できる最大Particle数(Pool容量)。
	float lifetime = 1.0f;
	float lifetimeRandomness = 0.0f;
	float speed = 1.0f;
	float speedRandomness = 0.0f;
	float gravity = 0.0f;
	float drag = 0.0f;
	float sizeStart = 0.3f;
	float sizeEnd = 0.0f;
	float sizeRandomness = 0.0f;
	float rotationSpeedDegrees = 0.0f;                    // Camera Facing / Fixed時のみ使うRoll回転速度。
	float stretchScale = 1.0f;                            // Directional Billboardの進行方向伸縮率。
	float shapeRadius = 0.0f;                             // 発生位置に加える球状ランダムオフセット半径。
	Vector3 colorStart{1.0f, 1.0f, 1.0f};
	Vector3 colorEnd{1.0f, 1.0f, 1.0f};
	float alphaStart = 1.0f;
	float alphaEnd = 0.0f;

	// Flipbook (Billboardに重ねて使う)。
	bool useFlipbook = false;
	int32_t flipbookColumns = 1;
	int32_t flipbookRows = 1;
	int32_t flipbookStartFrame = 0;
	int32_t flipbookEndFrame = 0;
	float flipbookFps = 16.0f;
	bool flipbookLoop = false;

	// Ribbon / Trail。
	float ribbonWidth = 0.2f;
	float ribbonPointLifetime = 1.0f;                     // 履歴点が消えるまでの秒数。
	int32_t ribbonMaxPoints = 32;
	float ribbonMinVertexDistance = 0.05f;                // これ未満の移動では新しい履歴点を追加しない。
	float ribbonUvScrollSpeed = 0.0f;

	// Ring。
	float ringInnerRadiusStart = 0.1f;
	float ringOuterRadiusStart = 0.3f;
	float ringInnerRadiusEnd = 1.0f;
	float ringOuterRadiusEnd = 1.5f;
	float ringLifetime = 0.8f;
	int32_t ringSegments = 32;

	bool sortByDistance = true;                           // Additive等はfalseにしてSortingを省略できる。
};

// 1つのEffect ResourceがまとまるEmitter/Node群。
struct EffectDefinition {
	std::string id;                                       // PlayEffectへ渡すEffectID(通常はファイル名)。
	std::vector<EffectNodeDefinition> nodes;
	int32_t maxConcurrentInstances = 16;                   // 同時再生できるEffect Instance数(超過分は再生しない)。
	std::vector<EffectLodLevel> lodLevels;                 // distance昇順。最後の段階を超えたらEffectをSpawnしない。

	bool LoadFromJson(const std::string& filePath);        // .effectdef JSONを読み込む。
};

#pragma warning(pop)
