#pragma once
#include <cmath>

struct Matrix4x4 {
	float matrix[4][4];  // matrix[row][column] は 4x4 行列の各要素。
};


constexpr int kRowHeight = 20;  // 旧デバッグ表示で、1 行あたり何ピクセル下げるかを表す値。
constexpr int kColumnWidth = 60;  // 旧デバッグ表示で、1 列あたり何ピクセル右へずらすかを表す値。


void MatrixScreenPrintf(int x, int y, const Matrix4x4& matrix, const char* label);  // 旧デバッグ表示用に行列を画面へ表示する関数。
Matrix4x4 Add(const Matrix4x4& matrix1, const Matrix4x4& matrix2);  // 2 つの行列を要素ごとに加算する。
Matrix4x4 Subtract(const Matrix4x4& matrix1, const Matrix4x4& matrix2);  // matrix1 から matrix2 を要素ごとに減算する。
Matrix4x4 Multiply(const Matrix4x4& matrix1, const Matrix4x4& matrix2);  // 2 つの 4x4 行列を乗算する。
Matrix4x4 Inverse(const Matrix4x4& matrix);  // 4x4 行列の逆行列を作る。特異行列の場合はゼロ行列を返す。
Matrix4x4 Transpose(const Matrix4x4& matrix);  // 行と列を入れ替えた転置行列を作る。
Matrix4x4 MakeIdentity4x4();  // 対角成分だけ 1.0f の単位行列を作る。
Matrix4x4 MakeRotateXMatrix(float radian);  // X 軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);  // Y 軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);  // Z 軸回転行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspect, float nearZ, float farZ);  // 透視投影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);  // 正射影行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);  // ビューポート行列

void DrawGrid(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix);
