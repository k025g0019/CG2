#include "Matrix.h"


#include <cmath>

#include "Vector&Matrix.h"


#pragma warning(push)
#pragma warning(disable : 5045)
Matrix4x4 Add(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix1.matrix[i][j] + matrix2.matrix[i][j];
		}
	}
	return result;
}
Matrix4x4 Subtract(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix1.matrix[i][j] - matrix2.matrix[i][j];
		}
	}
	return result;
}

Matrix4x4 Multiply(const Matrix4x4& matrix1, const Matrix4x4& matrix2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = 0.0f;
			for (int k = 0; k < 4; ++k) {
				result.matrix[i][j] += matrix1.matrix[i][k] * matrix2.matrix[k][j];
			}
		}
	}
	return result;
}

Matrix4x4 Inverse(const Matrix4x4& matrix) {
	Matrix4x4 result;
	float augmented[4][8] = {};

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			augmented[i][j] = matrix.matrix[i][j];
			augmented[i][j + 4] = (i == j) ? 1.0f : 0.0f;
		}
	}

	for (int i = 0; i < 4; ++i) {
		int pivotRow = i;
		for (int j = i + 1; j < 4; ++j) {
			if (std::fabs(augmented[j][i]) > std::fabs(augmented[pivotRow][i])) {
				pivotRow = j;
			}
		}

		if (std::fabs(augmented[pivotRow][i]) < 1.0e-6f) {
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					result.matrix[row][col] = 0.0f;
				}
			}
			return result;
		}

		if (pivotRow != i) {
			for (int j = 0; j < 8; ++j) {
				float temp = augmented[i][j];
				augmented[i][j] = augmented[pivotRow][j];
				augmented[pivotRow][j] = temp;
			}
		}

		float pivot = augmented[i][i];
		for (int j = 0; j < 8; ++j) {
			augmented[i][j] /= pivot;
		}

		for (int row = 0; row < 4; ++row) {
			if (row == i) {
				continue;
			}

			float factor = augmented[row][i];
			for (int col = 0; col < 8; ++col) {
				augmented[row][col] -= factor * augmented[i][col];
			}
		}
	}

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = augmented[i][j + 4];
		}
	}

	return result;
}
#pragma warning(pop)

Matrix4x4 Transpose(const Matrix4x4& matrix) {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = matrix.matrix[j][i];
		}
	}
	return result;
}

Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.matrix[i][j] = (i == j) ? 1.0f : 0.0f;
		}
	}
	return result;
}


Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = {0};
	float cosTheta = std::cos(radian);
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
	Matrix4x4 result = {0};
	float cosTheta = std::cos(radian);
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
	Matrix4x4 result = {0};
	float cosTheta = std::cos(radian);
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
	Matrix4x4 result = {0};
	float f = 1.0f / std::tan(fovY / 2.0f);
	result.matrix[0][0] = f / aspect;
	result.matrix[1][1] = f;
	result.matrix[2][2] = farZ / (farZ - nearZ);
	result.matrix[2][3] = 1.0f;
	result.matrix[3][2] = (-nearZ * farZ) / (farZ - nearZ);
	return result;
}

//正射影行列
// 正射影行列
Matrix4x4 MakeOrthographicMatrix(
	float left,
	float top,
	float right,
	float bottom,
	float nearClip,
	float farClip
) {
	Matrix4x4 result = {0};

	result.matrix[0][0] = 2.0f / (right - left);
	result.matrix[1][1] = 2.0f / (top - bottom);
	result.matrix[2][2] = 1.0f / (farClip - nearClip);
	result.matrix[3][0] = (left + right) / (left - right);
	result.matrix[3][1] = (top + bottom) / (bottom - top);
	result.matrix[3][2] = nearClip / (nearClip - farClip);
	result.matrix[3][3] = 1.0f;

	return result;
}

// ビューポート行列
// ビューポート行列
Matrix4x4 MakeViewportMatrix(
	float left,
	float top,
	float width,
	float height,
	float minDepth,
	float maxDepth
) {
	Matrix4x4 result = {0};

	result.matrix[0][0] = width / 2.0f;
	result.matrix[1][1] = -height / 2.0f;
	result.matrix[2][2] = maxDepth - minDepth;

	result.matrix[3][0] = left + width / 2.0f;
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
