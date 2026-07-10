#pragma once

#include "Matrix.h"
#include "Vector.h"

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
	float padding0;  // 16byte 境界合わせ用
	float padding1;  // 16byte 境界合わせ用
	float padding2;  // 16byte 境界合わせ用
	Matrix4x4 uvTransform;  // UV の移動 / 回転 / 拡縮行列
};

struct DirectionalLight {
	Vector4 color;  // ライト色
	Vector3 direction;  // 光が向かう方向
	float intensity;  // 光の強さ
	Vector3 skyUpperColor;  // 天頂側の空色
	float skyIntensity;  // 天球全体の明るさ
	Vector3 skyLowerColor;  // 地平線 / 下側の空色
	float skyEmission;  // 天球が自発光として足す強さ
	float ambientIntensity;  // 影側へ足す環境光の強さ
	float horizonSharpness;  // 上下の空色の切り替わり具合
	float reflectionIntensity;  // 反射へ混ぜる環境光の強さ
	float padding;  // 定数バッファ 16byte 境界合わせ用
};

struct TransformationMatrix {
	Matrix4x4 WVP;  // World * View * Projection の合成行列
	Matrix4x4 World;  // World 座標へ変換する行列
	Matrix4x4 lightWVP;  // 平行光源から見た World * View * Projection。影判定に使う
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
};

#pragma warning(pop)
