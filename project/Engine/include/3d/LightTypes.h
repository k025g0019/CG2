#pragma once
#pragma once


#include <cstdint>

#include "math/MathUtil.h"

#include "math/Vector.h"

//========================================
// ライトの種類
//========================================

// どれも Shader の cbuffer と同じ並びで持つ。
// HLSL は配列の要素を 16 byte 境界へ置くため、各構造体の大きさは
// 16 の倍数に揃えてある。padding を消すと配列の 2 個目以降がずれる。

//------------------------------
// 平行光源
//------------------------------

// 太陽のように、位置を持たず向きだけを持つ光。
// 影を落とせるのはこの光だけで、シーンに 1 灯だけ置ける。
struct DirectionalLightData {
	Vector4 color{1.0f, 1.0f, 1.0f, 1.0f}; // 光の色

	Vector3 direction{0.3f, -1.0f, 0.5f}; // 光が進む向き。正規化して送られる
	float intensity = 1.0f; // 光の強さ

	int32_t isEnabled = 1; // 0なら強さを失わず一時消灯する
	float padding[3]{};
};

//------------------------------
// 点光源
//------------------------------

// 電球のように、1 点から全方向へ広がる光。
struct PointLightData {
	Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};

	Vector3 position{0.0f, 0.0f, 0.0f}; // ワールド座標
	float intensity = 1.0f;

	// この距離を超えると届かない。狭くするほど、遠くの面の計算を早く打ち切れる。
	float range = 10.0f;

	// 距離に対する減り方。1 で直線的、2 で現実に近い逆二乗。
	// 大きいほど光源の近くだけが明るくなる。
	float decay = 2.0f;

	int32_t isEnabled = 1; // 0なら強さを失わず一時消灯する
	float padding = 0.0f; // 16 byte 境界合わせ
};

//------------------------------
// スポットライト
//------------------------------

// 懐中電灯のように、1 点から円錐状に広がる光。
//
// 角度は内側と外側の 2 つを持つ。内側までは減衰せず、そこから外側へ向かって
// 滑らかに暗くなる。2 つを同じ値にすると縁がくっきり出る。
struct SpotLightData {
	Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};

	Vector3 position{0.0f, 0.0f, 0.0f};
	float intensity = 1.0f;

	Vector3 direction{0.0f, -1.0f, 0.0f}; // 光が進む向き
	float range = 15.0f;

	float decay = 2.0f;

	// Shader 側は角度ではなく余弦で比較する。毎ピクセルで acos を避けるため、
	// 送る直前に LightGroup が角度から変換する。
	float cosOuterAngle = 0.0f;
	float cosInnerAngle = 0.0f;

	int32_t isEnabled = 1; // 0なら強さを失わず一時消灯する
};

//------------------------------
// スポットライトの角度指定
//------------------------------

// SpotLightData は余弦を持つため、角度で指定したいときはこちらを使う。
// LightGroup::SetSpotLightAngle が余弦へ直す。
struct SpotLightAngle {
	float innerAngle = ToRadian(20.0f); // ここまでは減衰しない
	float outerAngle = ToRadian(30.0f); // ここで 0 になる
};

//------------------------------
// 環境光
//------------------------------

// どの面にも一様に当たる光。影の中が真っ黒になるのを防ぐ。
// 本来は周囲からの反射だが、それを計算せず一定値で置き換えている。
struct AmbientLightData {
	Vector3 color{1.0f, 1.0f, 1.0f};
	float intensity = 0.15f;
};

// HLSL配列のStrideと一致しない変更をCompile時に検出する。
static_assert(sizeof(DirectionalLightData) == 48);
static_assert(sizeof(PointLightData) == 48);
static_assert(sizeof(SpotLightData) == 64);
static_assert(sizeof(AmbientLightData) == 16);
