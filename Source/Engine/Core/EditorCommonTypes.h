#pragma once

#include "Matrix.h"
#include "Vector.h"

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
};

struct Material {
	Vector4 color;  // 描画色
	int32_t enableLighting;  // 0 ならライト無効、0 以外ならライト有効
	int32_t useTexture;  // 0 なら Texture を使わず、Component の色だけで描画する
	float metallic;  // 金属感。0 は非金属、1 は金属
	float roughness;  // 粗さ。0 は鏡面、1 は粗い
	float reflectance;  // 反射の強さ
	float ior;  // 屈折率。ガラスや水の見た目調整に使う値
	float emissionStrength;  // 放射の強さ。0 なら自発光しない
	float reflectionMode;  // 0: SSR / 1: Cubemap / 2: Planar
	float reflectionProbeIntensity;  // 反射コンポーネント側で上書きする強さ
	float reflectionReserved;  // 反射コンポーネント側の粗さ上書き値
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
	float materialExtensionPadding0;  // HLSL cbuffer の 16byte 境界合わせ
	float materialExtensionPadding1;  // HLSL cbuffer の 16byte 境界合わせ
	float materialExtensionPadding2;  // HLSL cbuffer の 16byte 境界合わせ
	Vector2 uvTiling;  // UV の繰り返し回数
	Vector2 uvOffset;  // UV の開始位置
};

static_assert(offsetof(Material, uvTransform) == 96u, "Material と HLSL cbuffer の uvTransform 開始位置が一致していません。");
static_assert(sizeof(Material) == 288u, "Material と HLSL cbuffer のサイズが一致していません。");

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
};

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

struct ModelAnimationClipData {
	std::string name;  // FBX の AnimationStack 名や Clip 名
	float durationSeconds;  // クリップ長。取得できない場合は 0
	std::string animatedNodeName;  // Transform Key を取得した FBX Node 名
	std::vector<ModelAnimationKeyframeData> keyframes;  // クリップを時間順にサンプリングした Transform Key
};

struct ModelData {
	std::vector<VertexData> vertices;  // OBJ から展開した頂点配列
	MaterialData material;  // 後方互換用の先頭マテリアル情報
	std::vector<MaterialData> materials;  // モデルが持つマテリアル一覧
	std::vector<ModelAnimationClipData> animationClips;  // モデルが持つアニメーションクリップ一覧
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
