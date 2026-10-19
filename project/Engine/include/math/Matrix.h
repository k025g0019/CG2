#pragma once
#include <cmath>

struct Matrix4x4 {
	float matrix[4][4];
};


constexpr int kRowHeight = 20;
constexpr int kColumnWidth = 60;


void MatrixScreenPrintf(int x, int y, const Matrix4x4& matrix, const char* label);
Matrix4x4 Add(const Matrix4x4& matrix1, const Matrix4x4& matrix2);
Matrix4x4 Subtract(const Matrix4x4& matrix1, const Matrix4x4& matrix2);
Matrix4x4 Multiply(const Matrix4x4& matrix1, const Matrix4x4& matrix2);
Matrix4x4 Inverse(const Matrix4x4& matrix);
Matrix4x4 Transpose(const Matrix4x4& matrix);
Matrix4x4 MakeIdentity4x4();

// X 軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);

// Y 軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);

// Z 軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspect, float nearZ, float farZ);

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

// ビューポート行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

void DrawGrid(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix);
