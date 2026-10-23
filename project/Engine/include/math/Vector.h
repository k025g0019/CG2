#pragma once

struct Vector2 {
	float x;
	float y;
};

struct Vector3 {
	float x;
	float y;
	float z;
};

struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

// ベクトル同士は加減算し、乗除算はスカラーに対して行う。
Vector2 operator+(const Vector2& lhs, const Vector2& rhs);
Vector2 operator-(const Vector2& lhs, const Vector2& rhs);
Vector2 operator-(const Vector2& vector);
Vector2 operator*(const Vector2& vector, float scalar);
Vector2 operator*(float scalar, const Vector2& vector);
Vector2 operator/(const Vector2& vector, float scalar);
Vector2& operator+=(Vector2& lhs, const Vector2& rhs);
Vector2& operator-=(Vector2& lhs, const Vector2& rhs);
Vector2& operator*=(Vector2& vector, float scalar);
Vector2& operator/=(Vector2& vector, float scalar);

Vector3 operator+(const Vector3& lhs, const Vector3& rhs);
Vector3 operator-(const Vector3& lhs, const Vector3& rhs);
Vector3 operator-(const Vector3& vector);
Vector3 operator*(const Vector3& vector, float scalar);
Vector3 operator*(float scalar, const Vector3& vector);
Vector3 operator/(const Vector3& vector, float scalar);
Vector3& operator+=(Vector3& lhs, const Vector3& rhs);
Vector3& operator-=(Vector3& lhs, const Vector3& rhs);
Vector3& operator*=(Vector3& vector, float scalar);
Vector3& operator/=(Vector3& vector, float scalar);

Vector4 operator+(const Vector4& lhs, const Vector4& rhs);
Vector4 operator-(const Vector4& lhs, const Vector4& rhs);
Vector4 operator-(const Vector4& vector);
Vector4 operator*(const Vector4& vector, float scalar);
Vector4 operator*(float scalar, const Vector4& vector);
Vector4 operator/(const Vector4& vector, float scalar);
Vector4& operator+=(Vector4& lhs, const Vector4& rhs);
Vector4& operator-=(Vector4& lhs, const Vector4& rhs);
Vector4& operator*=(Vector4& vector, float scalar);
Vector4& operator/=(Vector4& vector, float scalar);


void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label);
Vector3 Add(const Vector3& v1, const Vector3& v2);
Vector3 Subtract(const Vector3& v1, const Vector3& v2);
Vector3 Multiply(float scalar, const Vector3& v);
float Dot(const Vector3& v1, const Vector3& v2);
Vector3 Cross(const Vector3& v1, const Vector3& v2);
float Length(const Vector3& v);
Vector3 Normalize(const Vector3& v);
