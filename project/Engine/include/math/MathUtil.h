#pragma once


#include <algorithm>
#include <cmath>

#include "math/Vector.h"


//========================================
// 数学の共通値と補助関数
//========================================

//------------------------------
// 数学定数
//------------------------------

inline constexpr float kPi = 3.14159265358979323846f; // 円周率
inline constexpr float kTwoPi = kPi * 2.0f; // 1 回転分のラジアン
inline constexpr float kHalfPi = kPi * 0.5f; // 直角のラジアン

//------------------------------
// 角度の変換
//------------------------------

// Inspector と Map Editor は度で表示し、行列生成はラジアンで受ける。
// その境界で使う変換なので、Engine 側と Game 側の両方から呼べるよう公開する。
constexpr float ToRadian(float degree) { return degree * (kPi / 180.0f); }
constexpr float ToDegree(float radian) { return radian * (180.0f / kPi); }

//------------------------------
// 範囲の制限
//------------------------------

inline float Clamp(float value, float minimum, float maximum) { return std::clamp(value, minimum, maximum); }

//------------------------------
// 補間処理
//------------------------------

inline float Lerp(float from, float to, float t) { return from + (to - from) * t; }

// value を入力範囲から出力範囲へ写す。入力幅が 0 の場合は outputMinimum を返す。
inline float Remap(
	float value,
	float inputMinimum,
	float inputMaximum,
	float outputMinimum,
	float outputMaximum) {
	const float inputRange = inputMaximum - inputMinimum;
	if (std::abs(inputRange) < 1.0e-6f) {
		return outputMinimum;
	}
	const float t = (value - inputMinimum) / inputRange;
	return Lerp(outputMinimum, outputMaximum, t);
}

// 表記を Math:: に統一したいGame側向けの別名。既存の自由関数もそのまま使える。
namespace Math {
	inline float Lerp(float from, float to, float t) { return Lerp(from, to, t); }

	inline float Clamp(float value, float minimum, float maximum) {
		return Clamp(value, minimum, maximum);
	}

	inline float Remap(float value, float inMin, float inMax, float outMin, float outMax) {
		return Remap(value, inMin, inMax, outMin, outMax);
	}

	constexpr float DegToRad(float degree) { return ToRadian(degree); }
	constexpr float RadToDeg(float radian) { return ToDegree(radian); }
} // namespace Math

// t を 0〜1 に丸めてから補間する。Camera の追従など、行き過ぎてほしくない用途に使う。
inline float LerpClamped(float from, float to, float t) { return Lerp(from, to, Clamp(t, 0.0f, 1.0f)); }

// -π〜π の範囲へ畳む。回転角を足し続けても値が無限に増えないようにする。
inline float WrapRadian(float radian) {
	float wrapped = std::fmod(radian + kPi, kTwoPi);
	if (wrapped < 0.0f) {
		wrapped += kTwoPi;
	}
	return wrapped - kPi;
}

// 角度を最短方向で補間する。0 と 2π をまたぐときに逆回りしないよう、
// 差分を -π〜π へ畳んでから進める。
inline float LerpRadian(float from, float to, float t) { return from + WrapRadian(to - from) * t; }

//------------------------------
// 進行方向から回転角を作る
//------------------------------

// 進行方向ベクトルを Y 回転（左右の向き）と X 回転（上下の傾き）へ直す。
// Z 回転は方向だけでは決まらないため 0 のままにする。
inline Vector3 DirectionToEulerAngle(const Vector3& direction) {
	const Vector3 normalized = Normalize(direction);
	const float horizontalLength = std::sqrt(normalized.x * normalized.x + normalized.z * normalized.z);
	return {
		std::atan2(-normalized.y, horizontalLength),
		std::atan2(normalized.x, normalized.z),
		0.0f,
	};
}
