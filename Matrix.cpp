#include "Matrix.h"

#include <cmath>

#include "Vector&Matrix.h"


#pragma warning(push)
#pragma warning(disable : 5045)
Matrix4x4 Add(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;  // result は matrix1 + matrix2 の要素ごとの加算結果。
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix1.matrix[i][j] + matrix2.matrix[i][j];
		}
	}
	return result;
}

Matrix4x4 Subtract(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;  // result は matrix1 - matrix2 の要素ごとの差分。
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix1.matrix[i][j] - matrix2.matrix[i][j];
		}
	}
	return result;
}

Matrix4x4 Multiply(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;  // result は matrix1 の行と matrix2 の列の内積で作る乗算結果。
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = 0.0f;  // 1 要素を作る前に 0.0f にして、k ループで積和を足し込む。
			for (int k = 0; k < 4; ++k) {
				result.matrix[i][j] += matrix1.matrix[i][k] * matrix2.matrix[k][j];
			}
		}
	}
	return result;
}

Matrix4x4 Inverse(const Matrix4x4& matrix) {
	Matrix4x4 result;  // result は最終的に返す逆行列。

	// augmented は左半分に元行列、右半分に単位行列を置く拡大行列。
	float augmented[4][8] = {};

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			augmented[i][j] = matrix.matrix[i][j];  // 左側 4 列に元行列、右側 4 列に単位行列を入れる。
			augmented[i][j + 4] = (i == j) ? 1.0f : 0.0f;
		}
	}

	for (int i = 0; i < 4; ++i) {
		int pivotRow = i;  // pivotRow は現在列で絶対値が最大の行。数値誤差を減らすために探す。
		for (int j = i + 1; j < 4; ++j) {
			if (std::fabs(augmented[j][i]) > std::fabs(augmented[pivotRow][i])) {
				pivotRow = j;
			}
		}

		// pivot がほぼ 0 なら逆行列を作れないため、ゼロ行列を返す。
		if (std::fabs(augmented[pivotRow][i]) < 1.0e-6f) {
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					result.matrix[row][col] = 0.0f;
				}
			}
			return result;
		}

		// 最大 pivot の行を現在行へ交換し、割り算の安定性を上げる。
		if (pivotRow != i) {
			for (int j = 0; j < 8; ++j) {
				float temp = augmented[i][j];
				augmented[i][j] = augmented[pivotRow][j];
				augmented[pivotRow][j] = temp;
			}
		}

		float pivot = augmented[i][i];  // pivot は現在行を 1.0f に正規化するための除数。
		for (int j = 0; j < 8; ++j) {
			augmented[i][j] /= pivot;
		}

		for (int row = 0; row < 4; ++row) {
			// pivot 行自身は消去対象ではないため飛ばす。
			if (row == i) {
				continue;
			}

			float factor = augmented[row][i];  // factor は現在列を 0.0f にするために pivot 行へ掛ける係数。
			for (int col = 0; col < 8; ++col) {
				augmented[row][col] -= factor * augmented[i][col];
			}
		}
	}

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = augmented[i][j + 4];  // ガウスジョルダン消去後、右半分が逆行列になっている。
		}
	}

	return result;
}
#pragma warning(pop)

Matrix4x4 Transpose(const Matrix4x4& matrix) {
	Matrix4x4 result;  // result は matrix の行と列を入れ替えた行列。
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix.matrix[j][i];
		}
	}
	return result;
}

Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result;  // result は変換を何も変えない単位行列。
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return result;
}

Matrix4x4 MakeRotateXMatrix(float radian) {
	// result は X 軸を中心に radian 分だけ回転させる行列。
	Matrix4x4 result = {0};

	float cosTheta = std::cos(radian);  // cosTheta / sinTheta は X 軸回転の Y/Z 成分を作る値。
	float sinTheta = std::sin(radian);
	result.matrix[1][1] = cosTheta;
	result.matrix[1][2] = sinTheta;
	result.matrix[2][1] = -sinTheta;
	result.matrix[2][2] = cosTheta;
	result.matrix[0][0] = 1.0f;
	result.matrix[3][3] = 1.0f;
	return result;
}

Matrix4x4 MakeRotateYMatrix(float radian) {
	// result は Y 軸を中心に radian 分だけ回転させる行列。
	Matrix4x4 result = {0};

	float cosTheta = std::cos(radian);  // cosTheta / sinTheta は Y 軸回転の X/Z 成分を作る値。
	float sinTheta = std::sin(radian);
	result.matrix[0][0] = cosTheta;
	result.matrix[0][2] = -sinTheta;
	result.matrix[2][0] = sinTheta;
	result.matrix[2][2] = cosTheta;
	result.matrix[1][1] = 1.0f;
	result.matrix[3][3] = 1.0f;
	return result;
}

Matrix4x4 MakeRotateZMatrix(float radian) {
	// result は Z 軸を中心に radian 分だけ回転させる行列。
	Matrix4x4 result = {0};

	float cosTheta = std::cos(radian);  // cosTheta / sinTheta は Z 軸回転の X/Y 成分を作る値。
	float sinTheta = std::sin(radian);
	result.matrix[0][0] = cosTheta;
	result.matrix[0][1] = sinTheta;
	result.matrix[1][0] = -sinTheta;
	result.matrix[1][1] = cosTheta;
	result.matrix[2][2] = 1.0f;
	result.matrix[3][3] = 1.0f;
	return result;
}

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspect, float nearZ, float farZ) {
	// result は 3D 座標を遠近感付きでクリップ空間へ変換する行列。
	Matrix4x4 result = {0};

	float f = 1.0f / std::tan(fovY / 2.0f);  // f は縦方向の視野角から求める焦点距離係数。
	result.matrix[0][0] = f / aspect;
	result.matrix[1][1] = f;
	result.matrix[2][2] = farZ / (farZ - nearZ);
	result.matrix[2][3] = 1.0f;
	result.matrix[3][2] = (-nearZ * farZ) / (farZ - nearZ);
	return result;
}

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
) {
	// result は 2D Sprite のように遠近感を付けない投影行列。
	Matrix4x4 result = {0};

	result.matrix[0][0] = 2.0f / (right - left);  // right-left と top-bottom で、表示範囲を -1.0f から 1.0f の NDC に正規化する。
	result.matrix[1][1] = 2.0f / (top - bottom);
	result.matrix[2][2] = 1.0f / (farClip - nearClip);
	result.matrix[3][0] = (left + right) / (left - right);
	result.matrix[3][1] = (top + bottom) / (bottom - top);
	result.matrix[3][2] = nearClip / (nearClip - farClip);
	result.matrix[3][3] = 1.0f;

	return result;
}

// ビューポート行列
Matrix4x4 MakeViewportMatrix(
	float left,
	float top,
	float width,
	float height,
	float minDepth,
	float maxDepth
) {
	// result は NDC 座標を画面ピクセル座標へ変換する行列。
	Matrix4x4 result = {0};

	result.matrix[0][0] = width / 2.0f;  // X は幅の半分、Y は画面座標に合わせて上下反転した高さの半分を掛ける。
	result.matrix[1][1] = -height / 2.0f;
	result.matrix[2][2] = maxDepth - minDepth;

	result.matrix[3][0] = left + width / 2.0f;  // left/top に半幅・半高を足して、NDC 原点を viewport 中央へ移す。
	result.matrix[3][1] = top + height / 2.0f;
	result.matrix[3][2] = minDepth;
	result.matrix[3][3] = 1.0f;

	return result;
}

//void DrawGrid(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix) {
//	constexpr float kGridHalfWidth = 2.0f;
//	constexpr uint32_t kSubdivision = 10;
//	constexpr float kGridEvery = (kGridHalfWidth * 2.0f) / static_cast<float>(kSubdivision);
//
//	for (uint32_t xIndex = 0; xIndex <= kSubdivision; ++xIndex) {
//		float x = -kGridHalfWidth + xIndex * kGridEvery;
//		Vector3 start{x, 0.0f, -kGridHalfWidth};
//		Vector3 end{x, 0.0f, kGridHalfWidth};
//
//		Vector3 ndcStart = Transform(start, viewProjectionMatrix);
//		Vector3 ndcEnd = Transform(end, viewProjectionMatrix);
//		Vector3 screenStart = Transform(ndcStart, viewportMatrix);
//		Vector3 screenEnd = Transform(ndcEnd, viewportMatrix);
//
//		uint32_t color = (std::fabs(x) < 0.0001f) ? 0xFFFFFFFF : 0xAAAAAAFF;
//		Novice::DrawLine(
//			static_cast<int>(screenStart.x), static_cast<int>(screenStart.y), static_cast<int>(screenEnd.x),
//			static_cast<int>(screenEnd.y), color);
//	}
//
//	for (uint32_t zIndex = 0; zIndex <= kSubdivision; ++zIndex) {
//		float z = -kGridHalfWidth + zIndex * kGridEvery;
//		Vector3 start{-kGridHalfWidth, 0.0f, z};
//		Vector3 end{kGridHalfWidth, 0.0f, z};
//
//		Vector3 ndcStart = Transform(start, viewProjectionMatrix);
//		Vector3 ndcEnd = Transform(end, viewProjectionMatrix);
//		Vector3 screenStart = Transform(ndcStart, viewportMatrix);
//		Vector3 screenEnd = Transform(ndcEnd, viewportMatrix);
//
//		uint32_t color = (std::fabs(z) < 0.0001f) ? 0xFFFFFFFF : 0xAAAAAAFF;
//		Novice::DrawLine(
//			static_cast<int>(screenStart.x), static_cast<int>(screenStart.y), static_cast<int>(screenEnd.x),
//			static_cast<int>(screenEnd.y), color);
//	}
//}
