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
	Matrix4x4 uvTransform;  // UV の移動 / 回転 / 拡縮行列
};

static_assert(offsetof(Material, uvTransform) == 64u, "Material と HLSL cbuffer の uvTransform 開始位置が一致していません。");
static_assert(sizeof(Material) == 128u, "Material と HLSL cbuffer のサイズが一致していません。");

constexpr int32_t kMaxEmissiveLights = 8;

struct EmissiveLight {
	Vector3 position;  // 放射オブジェクトのワールド位置
	float intensity;  // 放射の強さ
	Vector3 color;  // 放射色
	float range;  // 影響範囲
};

struct DirectionalLight {
	Vector4 color;  // ライト色
	Vector3 direction;  // 光が向かう方向
	float intensity;  // 光の強さ
	Vector3 position;  // Point / Spot / Area 用のライト位置
	float range;  // Point / Spot / Area の影響距離
	Vector3 skyUpperColor;  // 天頂側の空色
	float skyIntensity;  // 天球全体の明るさ
	Vector3 skyLowerColor;  // 地平線 / 下側の空色
	float skyEmission;  // 天球が自発光として足す強さ
	float ambientIntensity;  // 影側へ足す環境光の強さ
	float horizonSharpness;  // 上下の空色の切り替わり具合
	float reflectionIntensity;  // 反射へ混ぜる環境光の強さ
	float spotCosInner;  // Spot の内側コーン。cos 値で持つ
	float spotCosOuter;  // Spot の外側コーン。cos 値で持つ
	int32_t lightType;  // 0: Sun / 1: Point / 2: Spot / 3: Area
	float areaRadius;  // Area の見た目上の広がり。今は減衰の柔らかさへ使う
	Vector3 cameraPosition;  // PBR の反射とハイライトを計算する時のカメラ位置
	float padding3;  // 16byte 境界合わせ用
	float environmentTextureEnabled;  // 1 なら環境テクスチャを Skybox / IBL に使う
	float environmentTextureIntensity;  // 環境テクスチャの寄与
	float environmentTextureRotation;  // 水平回転角。ラジアン
	float environmentTextureMipBias;  // 反射時の mip バイアス
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
	std::string uvLayoutTextureFilePath;  // UV 確認用画像や UV 展開画像のパス
	Vector3 baseColor;  // 元アセットが持つ基本色。なければ白
	float intensity;  // 元アセットが持つ強さ。なければ 1
	float metallic;  // 元アセットが持つ金属感。なければ 0
	float roughness;  // 元アセットが持つ粗さ。なければ 0.5
	float reflectance;  // 元アセットが持つ反射強度。なければ 0
	float ior;  // 元アセットが持つ屈折率。なければ 1
	float alpha;  // 元アセットが持つ透明度。1 なら不透明
};

struct ModelAnimationClipData {
	std::string name;  // FBX の AnimationStack 名や Clip 名
	float durationSeconds;  // クリップ長。取得できない場合は 0
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
