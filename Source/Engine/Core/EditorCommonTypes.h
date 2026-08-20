#pragma once

#include "Matrix.h"
#include "Vector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// エディタと描画で共有する基本データ
//============================================================

struct Vector4 {
	float x;  // 4D 座標または RGBA の X / R
	float y;  // 4D 座標または RGBA の Y / G
	float z;  // 4D 座標または RGBA の Z / B
	float w;  // 同次座標 W または Alpha
};

struct Vector2 {
	float x;  // 2D 座標または UV の X / U
	float y;  // 2D 座標または UV の Y / V
};

struct Transforms {
	Vector3 scale;  // 拡縮値
	Vector3 rotate;  // 回転値。各軸ラジアン
	Vector3 translate;  // 平行移動値
};

struct VertexData {
	Vector4 position;  // 頂点のローカル座標
	Vector2 texcoord;  // テクスチャ参照用 UV
	Vector3 normal;  // ライティングに使う法線
	std::array<uint32_t, 4u> boneIndices{};  // FBX Skin Cluster を参照する最大 4 本の Bone Index
	Vector4 boneWeights{};  // boneIndices と対になる正規化済み Bone Weight
};

struct Material {
	Vector4 color;  // 描画色
	int32_t enableLighting;  // 0=Lightingなし、1=Lambert、2=Half Lambert、3=PBR
	int32_t useTexture;  // 0 なら Texture を使わず、Component の色だけで描画する
	float metallic;  // 金属感。0 は非金属、1 は金属
	float roughness;  // 粗さ。0 は鏡面、1 は粗い
	float reflectance;  // 材質の基礎反射率。Reflection Probe の寄与率とは分離する
	float ior;  // 屈折率。ガラスや水の見た目調整に使う値
	float emissionStrength;  // 放射の強さ。0 なら自発光しない
	float reflectionMode;  // 0: SSR / 1: Cubemap / 2: Planar
	float reflectionProbeIntensity;  // Reflection Probe の反射像を混ぜる寄与率
	float reflectionReserved;  // Reflection Probe の反射像へ適用する粗さ
	float materialPadding0;  // HLSL cbuffer の 16byte 境界合わせ
	float materialPadding1;  // HLSL cbuffer の 16byte 境界合わせ
	Vector3 reflectionProbeCenter;  // Box Projection に使う Reflection Probe のワールド中心
	float reflectionProbeBoxProjection;  // 1.0f なら Box Projection でキューブマップ方向を補正する
	Vector3 reflectionProbeExtent;  // Reflection Probe のワールド半径。各軸の Box 半分サイズ
	float materialPadding2;  // HLSL cbuffer の 16byte 境界合わせ
	Matrix4x4 uvTransform;  // UV の移動 / 回転 / 拡縮行列
	float normalScale;  // 法線マップの凹凸強度
	float ambientOcclusionStrength;  // AO マップが間接光を暗くする強度
	float heightScale;  // Height マップから作る視差量
	float alphaCutoff;  // Mask 描画で破棄する Alpha の境界
	float clearCoat;  // 表面を覆う透明なクリアコート層の強度
	float clearCoatRoughness;  // クリアコート層だけに使う粗さ
	float transmission;  // 環境光を透過・屈折させる割合
	float subsurface;  // 薄い材質へ回り込む拡散光の割合
	float anisotropy;  // 接線方向へ伸びる異方性反射の強度
	float anisotropyRotation;  // 異方性反射の接線方向を回す量
	float specularTint;  // 非金属の鏡面色へベースカラーを混ぜる割合
	float sheen;  // 布の縁に出る柔らかい反射の強度
	Vector3 emissionColor;  // 放射マップと放射強度へ掛ける色
	float sheenTint;  // Sheen の色へベースカラーを混ぜる割合
	int32_t useNormalMap;  // 0 以外なら t7 の Normal Map を使う
	int32_t useMetallicMap;  // 0 以外なら t8 の Metallic Map を使う
	int32_t useRoughnessMap;  // 0 以外なら t9 の Roughness Map を使う
	int32_t useAmbientOcclusionMap;  // 0 以外なら t10 の AO Map を使う
	int32_t useEmissionMap;  // 0 以外なら t11 の Emission Map を使う
	int32_t useHeightMap;  // 0 以外なら t12 の Height Map を使う
	int32_t useOpacityMap;  // 0 以外なら t13 の Opacity Map を使う
	int32_t alphaMode;  // 0=Opaque、1=Masked、2=Transparent
	int32_t doubleSided;  // 0 以外なら Inspector 上で両面材質として扱う
	float materialThickness;  // 透過と表面下散乱へ使う光路長
	float materialWetness;  // 濡れによる色と粗さの変化量
	float materialWaterlineHeight;  // World Yの水際中心
	Vector2 uvTiling;  // UV の繰り返し回数
	Vector2 uvOffset;  // UV の開始位置
	float oceanEnabled;  // 1.0f なら Ocean 専用の海面材質を使う
	float oceanFoamStrength;  // 波の急斜面へ加える泡の強さ
	float oceanRoughness;  // Ocean 専用の反射粗さ
	float oceanColorBlendScale;  // 浅瀬色と深海色の混合幅
	Vector3 oceanDeepColor;  // 海面の深い部分へ使う色
	float oceanMaterialPadding;  // HLSL cbuffer の 16byte 境界合わせ
	float oceanDetailNormalStrength;  // ピクセル単位の細波法線強度
	float oceanFoamThreshold;  // 波面圧縮から泡を出す閾値
	float oceanAbsorptionDistance;  // Beer-Lambert 近似へ使う吸収距離
	float oceanRefractionDistortion;  // 細波による屈折方向の歪み
	float oceanWaterDepth;  // 色吸収へ使う海面の水深
	float oceanCrestSharpness;  // 泡の波頭判定へ使う尖り
	float oceanMaterialPadding1;  // HLSL cbuffer の 16byte 境界合わせ
	float oceanMaterialPadding2;  // HLSL cbuffer の 16byte 境界合わせ
	int32_t surfaceMode;  // 0=通常、1=Terrain、2=Foliage
	float materialWaterlineWidth;  // 水際の濡れ遷移幅
	float surfaceMaterialPadding1;  // HLSL cbuffer の 16byte 境界合わせ
	float surfaceMaterialPadding2;  // HLSL cbuffer の 16byte 境界合わせ
	// Ocean Sun Lighting / Glitter 設定。OceanSurface.PS.hlsl だけが読む拡張領域
	float oceanSunDiffuseInfluence;  // 波面法線とSUN方向から出す明暗差の影響率
	float oceanSunSpecularInfluence;  // SUNの鏡面ハイライトの影響率
	float oceanSunGlitterInfluence;  // Sun Glitterの影響率
	float oceanSkyReflectionInfluence;  // 空/画面反射の影響率
	float oceanAmbientInfluence;  // Ambient / Sky Fillの影響率
	float oceanDiffuseFloor;  // directional diffuseの最低値
	float oceanGlitterIntensity;  // グリッター全体の強さ
	float oceanGlitterSharpness;  // グリッター粒の鋭さ
	float oceanGlitterDensity;  // グリッター粒の分散・密度
	float oceanGlitterThreshold;  // グリッターが出始める反射整列の閾値
	float oceanGlitterMaxClamp;  // グリッターの最大輝度クランプ
	float oceanLightingExtensionPadding0;  // HLSL cbuffer の 16byte 境界合わせ
	// Ocean 大波形状の光学表現。SUN強度とは独立して昼間の波形を読みやすくする
	float oceanMacroReflectionInfluence;  // Sky Reflectionへ使うLarge/Medium Normalの混合率
	float oceanCurvatureInfluence;  // 符号付き曲率から波頭と谷を抽出する感度
	float oceanTroughOcclusionStrength;  // 谷のSky Ambientを弱める最大量
	float oceanCrestHazeStrength;  // Foam直前の青白い波頭散乱
	float oceanCrestDetailBoost;  // 波頭でFine Normalを増やす量
	float oceanSlopeRefractionInfluence;  // 急斜面で屈折を強める量
	float oceanMediumWaveStrength;  // Large Waveへ重ねるMedium Normalの強さ
	float oceanWaveColorSeparation;  // 曲率による波頭と谷の水色色差
	float oceanShapeRoughnessVariation;  // 波頭と谷の反射粗さの差
	float oceanDetailFilterSharpness;  // 近距離でMedium/Fine Normalを保持する範囲
	float oceanGrazingShapeVisibility;  // 浅い視線角でも曲率色を残す割合
	float oceanShapeLightingPadding0;  // HLSL cbuffer の 16byte 境界合わせ
};

static_assert(offsetof(Material, uvTransform) == 96u, "Material と HLSL cbuffer の uvTransform 開始位置が一致していません。");
static_assert(sizeof(Material) == 464u, "Material と HLSL cbuffer のサイズが一致していません。");

constexpr int32_t kMaxEmissiveLights = 8;

struct EmissiveLight {
	Vector3 position;  // 放射オブジェクトのワールド位置
	float intensity;  // 放射の強さ
	Vector3 color;  // 放射色
	float range;  // 影響範囲
};

struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
	Vector3 position;
	float range;
	Vector3 skyUpperColor;
	float skyIntensity;
	Vector3 skyLowerColor;
	float skyEmission;
	float ambientIntensity;
	float horizonSharpness;
	float reflectionIntensity;
	float spotCosInner;
	float spotCosOuter;
	int32_t lightType;
	float areaRadius;
	Vector3 cameraPosition;
	float padding3;
	float environmentTextureEnabled;
	float environmentTextureIntensity;
	float environmentTextureRotation;
	float environmentTextureMipBias;
	float shadowTileIndex;  // -1 = no shadow, 0-3 = atlas tile
	float shadowTileUvScaleX;
	float shadowTileUvScaleY;
	float shadowTileUvBiasX;
	float shadowTileUvBiasY;
	float shadowEnabled;
	float shadowPadding0, shadowPadding1, shadowPadding2;
	Matrix4x4 shadowVP;
	Vector4 shadowCascadeSplits;
	float shadowCascadeCount;
	float shadowCascadePadding0;
	float shadowCascadePadding1;
	float shadowCascadePadding2;
	std::array<Matrix4x4, 4u> shadowCascadeVP;
	std::array<Vector4, 4u> shadowCascadeAtlas;
};

struct EmissiveLightArray {
	int32_t count;  // 有効な放射光源の数
	float padding0;
	float padding1;
	float padding2;
	EmissiveLight lights[kMaxEmissiveLights];  // 放射光源配列
};

struct TransformationMatrix {
	Matrix4x4 WVP;  // World * View * Projection の合成行列
	Matrix4x4 World;  // World 座標へ変換する行列
	Matrix4x4 lightWVP;  // 平行光源から見た World * View * Projection。影判定に使う
	Vector4 reflectionClipPlane;  // SV_ClipDistance0 に使うクリップ平面 (normal.xyz, d)
	Vector4 reflectionClipParams;  // x=1.0 でクリップ有効、0.0 で無効
	Vector4 oceanParams0;  // x=有効、y=時刻、z=波高、w=最大波高
	Vector4 oceanParams1;  // xy=主波方向、z=波長、w=速度
	Vector4 oceanParams2;  // xy=副波方向、z=副波強度、w=choppiness
	Vector4 oceanParams3;  // x=細波波長比、y=細波強度、z=時間倍率、w=近傍波LODの基準寸法
	Vector4 oceanParams4;  // x=風速、y=水深、z=方向分散、w=うねり強度
	Vector4 oceanParams5;  // x=スペクトルシード、y=波頭の尖り、zw=カメラ追従 LOD のローカル XZ 中心
	std::array<Vector4, 16u> oceanWaveData0;  // xy=方向、z=波数、w=振幅
	std::array<Vector4, 16u> oceanWaveData1;  // x=角周波数、y=位相、zw=予約
	Vector4 surfaceParams0;  // x=描画種別、y=時刻、z=風変位量、w=風速
	Vector4 surfaceParams1;  // xy=風向き、z=空間周波数、w=Height/Density map 有効
	Vector4 temporalParams;  // x=現在の波時刻、y=前フレームの波時刻、zw=Viewport / RenderTarget 比率
	Matrix4x4 previousWVP;  // 前フレームの位置を再投影し、Object / Skinned Motion Vector を作る
};

static_assert(
	sizeof(TransformationMatrix) == 944u,
	"TransformationMatrix と Ocean HLSL cbuffer のサイズが一致していません。");

struct Sprite {
	Vector2 position;  // スプライトの左上基準位置
	Vector2 size;  // スプライトの幅と高さ
};

struct MaterialData {
	std::string name;  // マテリアル名。FBX / MTL に名前があれば保持する
	std::string textureFilePath;  // mtl / FBX から読んだ texture ファイルパス
	std::string normalTextureFilePath;  // FBX から読んだ法線マップのファイルパス
	std::string metallicTextureFilePath;  // FBX から読んだメタリックマップのファイルパス
	std::string roughnessTextureFilePath;  // FBX から読んだ粗さマップのファイルパス
	std::string ambientOcclusionTextureFilePath;  // FBX から読んだ AO マップのファイルパス
	std::string emissionTextureFilePath;  // FBX から読んだ放射マップのファイルパス
	std::string heightTextureFilePath;  // FBX から読んだ高さ・変位マップのファイルパス
	std::string opacityTextureFilePath;  // FBX から読んだ不透明度マップのファイルパス
	std::string uvLayoutTextureFilePath;  // UV 確認用画像や UV 展開画像のパス
	Vector3 baseColor;  // 元アセットが持つ基本色。なければ白
	float intensity;  // 元アセットが持つ強さ。なければ 1
	float metallic;  // 元アセットが持つ金属感。なければ 0
	float roughness;  // 元アセットが持つ粗さ。なければ 0.5
	float reflectance;  // 元アセットが持つ反射強度。なければ 0
	float ior;  // 元アセットが持つ屈折率。なければ 1
	float alpha;  // 元アセットが持つ透明度。1 なら不透明
};

struct ModelAnimationKeyframeData {
	float timeSeconds;  // クリップ開始からの経過秒
	Vector3 translation;  // FBX Node のローカル平行移動
	Vector3 rotation;  // FBX Node のローカル回転。各軸ラジアン
	Vector3 scale;  // FBX Node のローカル拡縮
};

struct ModelSkinPoseFrameData {
	float timeSeconds;  // クリップ開始からの経過秒
	std::vector<Matrix4x4> boneMatrices;  // Mesh ローカル頂点を現在姿勢へ変換する Bone 行列
};

struct ModelAnimationClipData {
	std::string name;  // FBX の AnimationStack 名や Clip 名
	float durationSeconds;  // クリップ長。取得できない場合は 0
	std::string animatedNodeName;  // Transform Key を取得した FBX Node 名
	std::vector<ModelAnimationKeyframeData> keyframes;  // クリップを時間順にサンプリングした Transform Key
	std::vector<ModelSkinPoseFrameData> skinPoseFrames;  // GBuffer と通常描画で共有するスキン姿勢
};

struct ModelData {
	std::vector<VertexData> vertices;  // OBJ から展開した頂点配列
	std::vector<uint32_t> indices;  // 空なら従来の頂点列、値があれば共有頂点の三角形 Index 配列
	MaterialData material;  // 後方互換用の先頭マテリアル情報
	std::vector<MaterialData> materials;  // モデルが持つマテリアル一覧
	std::vector<ModelAnimationClipData> animationClips;  // モデルが持つアニメーションクリップ一覧
	std::vector<std::string> skinBoneNames;  // boneIndices が参照する FBX Cluster / Bone 名
	std::vector<Matrix4x4> defaultSkinMatrices;  // Animation 停止時に使う FBX 初期姿勢
	Vector3 localBoundsCenter;  // モデル原点基準のローカル包囲中心
	Vector3 localBoundsSize;  // モデル原点基準のローカル包囲サイズ
};

struct EditorRenderTextureAsset {
	int32_t width;  // RenderTexture の横解像度
	int32_t height;  // RenderTexture の縦解像度
	bool useHdr;  // true なら HDR 用 R16G16B16A16_FLOAT、false なら LDR 用 R8G8B8A8_UNORM
	bool useDepth;  // true なら Camera 描画時に DepthTexture も持つ
	Vector4 clearColor;  // 描画前に塗りつぶす色
};

#pragma warning(pop)
