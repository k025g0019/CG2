#pragma once

#include "EditorCommonTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// FBX / OBJ 実メッシュ当たり判定
//============================================================

struct EditorMeshCollisionTriangle {
	Vector3 a;  // 三角形の 1 点目。MeshCollider のローカル座標で保持する。
	Vector3 b;  // 三角形の 2 点目。
	Vector3 c;  // 三角形の 3 点目。
	Vector3 aabbMin;  // BVH Query 用の三角形最小座標。
	Vector3 aabbMax;  // BVH Query 用の三角形最大座標。
};

struct EditorMeshCollisionBvhNode {
	Vector3 aabbMin;  // この Node が覆う三角形群の最小座標。
	Vector3 aabbMax;  // この Node が覆う三角形群の最大座標。
	int32_t leftChildIndex;  // 子 Node。Leaf なら -1。
	int32_t rightChildIndex;  // 子 Node。Leaf なら -1。
	int32_t firstTriangleIndex;  // Leaf が見る triangleIndices_ の開始位置。
	int32_t triangleCount;  // Leaf が見る三角形数。
};

class EditorMeshCollisionMesh {
public:
	bool BuildFromModelData(
		const ModelData& modelData,
		const Vector3& colliderCenter,
		const Vector3& objectScale,
		const Vector3& shapeCenterOfMass);  // Jolt の重心ローカル座標に合わせて三角形と BVH を作る

	bool IsPointNearSurface(const Vector3& localPoint) const;  // 点が実三角形の近くにあるかを BVH で調べる
	bool IsPointNearSurface(const Vector3& localPoint, float additionalTolerance) const;  // 継続接触中だけ許容距離を少し広げて接触振動を抑える
	bool IsValid() const;  // 三角形と BVH が作れているか

	float GetContactTolerance() const;  // 接触点と三角形表面を同一扱いする距離
	size_t GetTriangleCount() const;  // デバッグ表示やログ用の三角形数

private:
	int32_t BuildNode(int32_t firstTriangleIndex, int32_t triangleCount, int32_t depth);
	bool QueryNode(int32_t nodeIndex, const Vector3& queryMin, const Vector3& queryMax, const Vector3& localPoint, float tolerance) const;

	std::vector<EditorMeshCollisionTriangle> triangles_;  // ModelData から展開した三角形一覧
	std::vector<int32_t> triangleIndices_;  // BVH の並び替え用。三角形本体は動かさない
	std::vector<EditorMeshCollisionBvhNode> nodes_;  // BVH Node 配列
	float contactTolerance_ = 0.05f;  // ConvexHull の接触点を実メッシュ表面と照合する許容距離
};

#pragma warning(pop)
