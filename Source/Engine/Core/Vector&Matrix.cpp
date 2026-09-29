#include "Vector&Matrix.h"

#include <algorithm>


Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;  // result は matrix を掛けた後の座標。

	// X/Y/Z は行列の各列との積和に、平行移動成分を足して求める。
	result.x = vector.x * matrix.matrix[0][0] + vector.y * matrix.matrix[1][0] + vector.z * matrix.matrix[2][0] + matrix
		.matrix[3][0];
	result.y = vector.x * matrix.matrix[0][1] + vector.y * matrix.matrix[1][1] + vector.z * matrix.matrix[2][1] + matrix
		.matrix[3][1];
	result.z = vector.x * matrix.matrix[0][2] + vector.y * matrix.matrix[1][2] + vector.z * matrix.matrix[2][2] + matrix
		.matrix[3][2];

	// 同次座標 w で割ることで、透視投影後の正しい 3D 座標へ戻す。
	result.x /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];
	result.y /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];
	result.z /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];

	return result;
}


Matrix4x4 MakeTranslationMatrix(const Vector3& translation) {
	Matrix4x4 translationMatrix;  // translationMatrix は translate 成分だけを持つ 4x4 行列。
	translationMatrix.matrix[0][0] = 1.0f;
	translationMatrix.matrix[0][1] = 0.0f;
	translationMatrix.matrix[0][2] = 0.0f;
	translationMatrix.matrix[0][3] = 0.0f;

	translationMatrix.matrix[1][0] = 0.0f;
	translationMatrix.matrix[1][1] = 1.0f;
	translationMatrix.matrix[1][2] = 0.0f;
	translationMatrix.matrix[1][3] = 0.0f;

	translationMatrix.matrix[2][0] = 0.0f;
	translationMatrix.matrix[2][1] = 0.0f;
	translationMatrix.matrix[2][2] = 1.0f;
	translationMatrix.matrix[2][3] = 0.0f;

	translationMatrix.matrix[3][0] = translation.x;
	translationMatrix.matrix[3][1] = translation.y;
	translationMatrix.matrix[3][2] = translation.z;
	translationMatrix.matrix[3][3] = 1.0f;

	return translationMatrix;
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 scaleMatrix;  // scaleMatrix は各軸の倍率だけを持つ 4x4 行列。
	scaleMatrix.matrix[0][0] = scale.x;
	scaleMatrix.matrix[0][1] = 0.0f;
	scaleMatrix.matrix[0][2] = 0.0f;
	scaleMatrix.matrix[0][3] = 0.0f;
	scaleMatrix.matrix[1][0] = 0.0f;
	scaleMatrix.matrix[1][1] = scale.y;
	scaleMatrix.matrix[1][2] = 0.0f;
	scaleMatrix.matrix[1][3] = 0.0f;
	scaleMatrix.matrix[2][0] = 0.0f;
	scaleMatrix.matrix[2][1] = 0.0f;
	scaleMatrix.matrix[2][2] = scale.z;
	scaleMatrix.matrix[2][3] = 0.0f;
	scaleMatrix.matrix[3][0] = 0.0f;
	scaleMatrix.matrix[3][1] = 0.0f;
	scaleMatrix.matrix[3][2] = 0.0f;
	scaleMatrix.matrix[3][3] = 1.0f;
	return scaleMatrix;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 affineMatrix;  // affineMatrix は scale、rotate、translate をまとめた最終的なワールド行列。
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);  // rotateX/Y/ZMatrix は各軸ごとの回転行列。
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);

	Matrix4x4 rotateXMatrixYZMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));  // rotateXMatrixYZMatrix は X -> Y -> Z の順で合成した回転行列。
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);  // scaleMatrix と translateMatrix は Transform の拡縮・平行移動を行列化したもの。
	Matrix4x4 translateMatrix = MakeTranslationMatrix(translate);

	affineMatrix = Multiply(Multiply(scaleMatrix, rotateXMatrixYZMatrix), translateMatrix);  // SRT の順に合成して、ローカル座標をワールド座標へ変換する。
	return affineMatrix;
}


Vector3 Project(const Vector3& v1, const Vector3& v2) {
	float dotProduct = Dot(v1, v2);  // dotProduct は v1 が v2 方向にどれだけ向いているかを表す値。
	float lengthSquared = Dot(v2, v2);  // lengthSquared は v2 の長さの 2 乗。射影の割り算に使う。

	// v2 がゼロベクトルなら射影方向がないため、ゼロベクトルを返す。
	if (lengthSquared == 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}

	// dotProduct / lengthSquared は v2 に掛ける射影係数。
	return Multiply(dotProduct / lengthSquared, v2);
}


Vector3 ClosestPoint(const Vector3& point, const Segment& segment) {
	Vector3 segmentVector = Subtract(segment.diff, segment.origin);  // segmentVector は線分の始点から終点へ向かうベクトル。
	Vector3 pointVector = Subtract(point, segment.origin);  // pointVector は線分の始点から調べたい点へ向かうベクトル。
	float t = Dot(pointVector, segmentVector) / Dot(segmentVector, segmentVector);  // t は point を線分方向へ射影した時の、segmentVector 上の倍率。

	// origin に t 倍した segmentVector を足して、線分上の最近点を作る。
	return Add(segment.origin, Multiply(t, segmentVector));
}

bool IsCollision(const Sphere& s1, const Sphere& s2) {
	float distance = Length(Subtract(s1.center, s2.center));  // distance は 2 つの球の中心間距離。

	// 中心間距離が半径の合計以下なら、球同士は接触または重なっている。
	return distance <= s1.radius + s2.radius;
}

bool PlaneIsCollision(const Sphere& sphere, const Plane& plane) {
	float distance = Dot(sphere.center, plane.normal) - plane.distance;  // distance は球中心から平面までの符号付き距離。

	// 距離の絶対値が半径以下なら、球が平面に触れている。
	return std::fabs(distance) <= sphere.radius;
}

Vector3 Perpendicular(const Vector3& v) {
	// X/Y 成分がある場合は、Z を使わず XY 平面上で垂直なベクトルを作る。
	if (v.x != 0.0f || v.y != 0.0f) {
		return {-v.y, v.x, 0.0f};
	}

	// X/Y が 0 の縦方向ベクトルには、Y/Z 成分を入れ替えて垂直なベクトルを作る。
	return {0.0f, -v.z, v.y};
}

