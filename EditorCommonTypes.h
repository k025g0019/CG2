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
	float padding[2];  // ConstantBuffer の 16 byte アラインメント用
	Matrix4x4 uvTransform;  // UV の移動 / 回転 / 拡縮行列
};

struct DirectionalLight {
	Vector4 color;  // ライト色
	Vector3 direction;  // 光が向かう方向
	float intensity;  // 光の強さ
};

struct TransformationMatrix {
	Matrix4x4 WVP;  // World * View * Projection の合成行列
	Matrix4x4 World;  // World 座標へ変換する行列
};

struct Sprite {
	Vector2 position;  // スプライトの左上基準位置
	Vector2 size;  // スプライトの幅と高さ
};

struct MaterialData {
	std::string textureFilePath;  // mtl から読んだ texture ファイルパス
};

struct ModelData {
	std::vector<VertexData> vertices;  // OBJ から展開した頂点配列
	MaterialData material;  // OBJ に紐づく Material 情報
};

#pragma warning(pop)
