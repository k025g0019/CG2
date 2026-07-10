#include "Vector.h"


#include <cmath>

//void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label) {
//	Novice::ScreenPrintf(x, y, "%0.02f", vector.x);
//	Novice::ScreenPrintf(x + 50, y, " %0.02f", vector.y);
//	Novice::ScreenPrintf(x + 100, y, "%0.02f %s", vector.z, label);
//}

Vector3 Add(const Vector3& v1, const Vector3& v2) {
	Vector3 result;  // result は v1 + v2 の成分ごとの加算結果。
	result.x = v1.x + v2.x;
	result.y = v1.y + v2.y;
	result.z = v1.z + v2.z;
	return result;
}

Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
	Vector3 result;  // result は v1 から v2 へ向かう差分ベクトル。
	result.x = v1.x - v2.x;
	result.y = v1.y - v2.y;
	result.z = v1.z - v2.z;
	return result;
}

Vector3 Multiply(float scalar, const Vector3& v) {
	Vector3 result;  // result は向きは同じで、長さだけ scalar 倍したベクトル。
	result.x = scalar * v.x;
	result.y = scalar * v.y;
	result.z = scalar * v.z;
	return result;
}

float Dot(const Vector3& v1, const Vector3& v2) {
	// 各軸の積を足し、向きの近さや射影量を求める。
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

Vector3 Cross(const Vector3& v1, const Vector3& v2) {
	Vector3 result;  // result は v1 と v2 の両方に垂直なベクトル。
	result.x = v1.y * v2.z - v1.z * v2.y;
	result.y = v1.z * v2.x - v1.x * v2.z;
	result.z = v1.x * v2.y - v1.y * v2.x;
	return result;
}

float Length(const Vector3& v) {
	// 自分自身との内積を平方根にすると、3D 空間でのベクトル長になる。
	return std::sqrt(Dot(v, v));
}

Vector3 Normalize(const Vector3& v) {
	float length = Length(v);  // length は v の現在の長さ。0 の場合は割り算できない。

	// ゼロベクトルは方向を持たないため、ゼロのまま返す。
	if (length == 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}

	// 1.0f / length を掛けることで、長さだけ 1.0f にそろえる。
	return Multiply(1.0f / length, v);
}
