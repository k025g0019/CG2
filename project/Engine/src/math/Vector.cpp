#include "math/Vector.h"


#include <cmath>

Vector2 operator+(const Vector2& lhs, const Vector2& rhs) {
	return {lhs.x + rhs.x, lhs.y + rhs.y};
}

Vector2 operator-(const Vector2& lhs, const Vector2& rhs) {
	return {lhs.x - rhs.x, lhs.y - rhs.y};
}

Vector2 operator-(const Vector2& vector) {
	return {-vector.x, -vector.y};
}

Vector2 operator*(const Vector2& vector, float scalar) {
	return {vector.x * scalar, vector.y * scalar};
}

Vector2 operator*(float scalar, const Vector2& vector) {
	return vector * scalar;
}

Vector2 operator/(const Vector2& vector, float scalar) {
	return {vector.x / scalar, vector.y / scalar};
}

Vector2& operator+=(Vector2& lhs, const Vector2& rhs) {
	lhs = lhs + rhs;
	return lhs;
}

Vector2& operator-=(Vector2& lhs, const Vector2& rhs) {
	lhs = lhs - rhs;
	return lhs;
}

Vector2& operator*=(Vector2& vector, float scalar) {
	vector = vector * scalar;
	return vector;
}

Vector2& operator/=(Vector2& vector, float scalar) {
	vector = vector / scalar;
	return vector;
}

Vector3 operator+(const Vector3& lhs, const Vector3& rhs) {
	return Add(lhs, rhs);
}

Vector3 operator-(const Vector3& lhs, const Vector3& rhs) {
	return Subtract(lhs, rhs);
}

Vector3 operator-(const Vector3& vector) {
	return Multiply(-1.0f, vector);
}

Vector3 operator*(const Vector3& vector, float scalar) {
	return Multiply(scalar, vector);
}

Vector3 operator*(float scalar, const Vector3& vector) {
	return Multiply(scalar, vector);
}

Vector3 operator/(const Vector3& vector, float scalar) {
	return {vector.x / scalar, vector.y / scalar, vector.z / scalar};
}

Vector3& operator+=(Vector3& lhs, const Vector3& rhs) {
	lhs = lhs + rhs;
	return lhs;
}

Vector3& operator-=(Vector3& lhs, const Vector3& rhs) {
	lhs = lhs - rhs;
	return lhs;
}

Vector3& operator*=(Vector3& vector, float scalar) {
	vector = vector * scalar;
	return vector;
}

Vector3& operator/=(Vector3& vector, float scalar) {
	vector = vector / scalar;
	return vector;
}

Vector4 operator+(const Vector4& lhs, const Vector4& rhs) {
	return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

Vector4 operator-(const Vector4& lhs, const Vector4& rhs) {
	return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w};
}

Vector4 operator-(const Vector4& vector) {
	return {-vector.x, -vector.y, -vector.z, -vector.w};
}

Vector4 operator*(const Vector4& vector, float scalar) {
	return {vector.x * scalar, vector.y * scalar, vector.z * scalar, vector.w * scalar};
}

Vector4 operator*(float scalar, const Vector4& vector) {
	return vector * scalar;
}

Vector4 operator/(const Vector4& vector, float scalar) {
	return {vector.x / scalar, vector.y / scalar, vector.z / scalar, vector.w / scalar};
}

Vector4& operator+=(Vector4& lhs, const Vector4& rhs) {
	lhs = lhs + rhs;
	return lhs;
}

Vector4& operator-=(Vector4& lhs, const Vector4& rhs) {
	lhs = lhs - rhs;
	return lhs;
}

Vector4& operator*=(Vector4& vector, float scalar) {
	vector = vector * scalar;
	return vector;
}

Vector4& operator/=(Vector4& vector, float scalar) {
	vector = vector / scalar;
	return vector;
}

//void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label) {
//	Novice::ScreenPrintf(x, y, "%0.02f", vector.x);
//	Novice::ScreenPrintf(x + 50, y, " %0.02f", vector.y);
//	Novice::ScreenPrintf(x + 100, y, "%0.02f %s", vector.z, label);
//}

Vector3 Add(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.x + v2.x;
	result.y = v1.y + v2.y;
	result.z = v1.z + v2.z;
	return result;
}

Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.x - v2.x;
	result.y = v1.y - v2.y;
	result.z = v1.z - v2.z;
	return result;
}

Vector3 Multiply(float scalar, const Vector3& v) {
	Vector3 result;
	result.x = scalar * v.x;
	result.y = scalar * v.y;
	result.z = scalar * v.z;
	return result;
}

float Dot(const Vector3& v1, const Vector3& v2) {
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

Vector3 Cross(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.y * v2.z - v1.z * v2.y;
	result.y = v1.z * v2.x - v1.x * v2.z;
	result.z = v1.x * v2.y - v1.y * v2.x;
	return result;
}

float Length(const Vector3& v) {
	return std::sqrt(Dot(v, v));
}

Vector3 Normalize(const Vector3& v) {
	float length = Length(v);
	if (length == 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}
	return Multiply(1.0f / length, v);
}
