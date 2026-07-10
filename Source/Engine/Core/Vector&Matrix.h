#pragma once
#include <cstdint>
#include "Matrix.h"
#include "Vector.h"

struct Sphere {
	Vector3 center;  // center は球の中心座標。
	float radius;  // radius は中心から表面までの距離。
};

struct Segment {
	Vector3 origin;  // origin は線分の開始点。
	Vector3 diff;  // diff は線分の終了点として使っている座標。
};

struct Plane {
	Vector3 normal;  // normal は平面の向きを表す法線ベクトル。
	float distance;  // distance は原点から normal 方向へどれだけ離れた平面かを表す距離。
};

Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);  // Vector3 を 4x4 行列で変換し、同次座標 w で割った座標を返す。
Matrix4x4 MakeTranslationMatrix(const Vector3& translation);  // translation を 4x4 の平行移動行列にする。
Matrix4x4 MakeScaleMatrix(const Vector3& scale);  // scale を 4x4 の拡大縮小行列にする。
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);  // scale、rotate、translate を 1 つの 3D アフィン変換行列にまとめる。

// 旧デバッグ表示用に Sphere を線で描く関数。
void DrawSphere(
	const Sphere& sphere,
	const Matrix4x4& viewProjectionMatrix,
	const Matrix4x4& viewportMatrix,
	uint32_t color);

Vector3 Project(const Vector3& v1, const Vector3& v2);  // v1 を v2 方向へ射影したベクトルを返す。
Vector3 ClosestPoint(const Vector3& point, const Segment& segment);  // point から segment 上で一番近い点を返す。
bool IsCollision(const Sphere& s1, const Sphere& s2);  // 2 つの球が重なっているかを返す。
bool PlaneIsCollision(const Sphere& sphere, const Plane& plane);  // 球と平面が接触または交差しているかを返す。
Vector3 Perpendicular(const Vector3& v);  // v に垂直なベクトルを 1 つ返す。

// 旧デバッグ表示用に Plane を線で描く関数。
void DrawPlane(
	const Plane& plane,
	const Matrix4x4& viewProjectionMatrix,
	const Matrix4x4& viewportMatrix,
	uint32_t color);
