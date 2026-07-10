#include "EditorMeshCollision.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#pragma warning(disable : 4820 5045)

namespace {
	constexpr int32_t kMeshCollisionLeafTriangleCount = 8;  // Leaf 内で総当たりする最大三角形数
	constexpr int32_t kMeshCollisionMaxDepth = 32;  // 壊れたメッシュで再帰が深くなりすぎるのを防ぐ
	constexpr float kMeshCollisionMinimumTolerance = 0.02f;  // 小さいモデルでも接触点を拾う最低幅
	constexpr float kMeshCollisionMaximumTolerance = 0.15f;  // 穴を塞がないため、許容距離は大きくしすぎない

	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 MultiplyVector3(const Vector3& vector, float scale) {
		return Vector3{
			vector.x * scale,
			vector.y * scale,
			vector.z * scale};
	}

	float DotVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return
			firstValue.x * secondValue.x +
			firstValue.y * secondValue.y +
			firstValue.z * secondValue.z;
	}

	float LengthSqVector3(const Vector3& vector) {
		return DotVector3(vector, vector);
	}

	Vector3 MinVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			(std::min)(firstValue.x, secondValue.x),
			(std::min)(firstValue.y, secondValue.y),
			(std::min)(firstValue.z, secondValue.z)};
	}

	Vector3 MaxVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			(std::max)(firstValue.x, secondValue.x),
			(std::max)(firstValue.y, secondValue.y),
			(std::max)(firstValue.z, secondValue.z)};
	}

	Vector3 MakeTriangleCentroid(const EditorMeshCollisionTriangle& triangle) {
		return Vector3{
			(triangle.a.x + triangle.b.x + triangle.c.x) / 3.0f,
			(triangle.a.y + triangle.b.y + triangle.c.y) / 3.0f,
			(triangle.a.z + triangle.b.z + triangle.c.z) / 3.0f};
	}

	float GetAxisValue(const Vector3& vector, int32_t axisIndex) {
		if (axisIndex == 0) {
			return vector.x;
		}
		if (axisIndex == 1) {
			return vector.y;
		}

		return vector.z;
	}

	int32_t GetLargestAxis(const Vector3& size) {
		if (size.x >= size.y && size.x >= size.z) {
			return 0;
		}
		if (size.y >= size.z) {
			return 1;
		}

		return 2;
	}

	bool IsAabbOverlapped(
		const Vector3& firstMin,
		const Vector3& firstMax,
		const Vector3& secondMin,
		const Vector3& secondMax) {
		return
			firstMin.x <= secondMax.x &&
			firstMax.x >= secondMin.x &&
			firstMin.y <= secondMax.y &&
			firstMax.y >= secondMin.y &&
			firstMin.z <= secondMax.z &&
			firstMax.z >= secondMin.z;
	}

	Vector3 ClosestPointOnTriangle(
		const Vector3& point,
		const Vector3& a,
		const Vector3& b,
		const Vector3& c) {
		const Vector3 ab = SubtractVector3(b, a);
		const Vector3 ac = SubtractVector3(c, a);
		const Vector3 ap = SubtractVector3(point, a);
		const float d1 = DotVector3(ab, ap);
		const float d2 = DotVector3(ac, ap);
		if (d1 <= 0.0f && d2 <= 0.0f) {
			return a;
		}

		const Vector3 bp = SubtractVector3(point, b);
		const float d3 = DotVector3(ab, bp);
		const float d4 = DotVector3(ac, bp);
		if (d3 >= 0.0f && d4 <= d3) {
			return b;
		}

		const float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
			const float v = d1 / (d1 - d3);
			return AddVector3(a, MultiplyVector3(ab, v));
		}

		const Vector3 cp = SubtractVector3(point, c);
		const float d5 = DotVector3(ab, cp);
		const float d6 = DotVector3(ac, cp);
		if (d6 >= 0.0f && d5 <= d6) {
			return c;
		}

		const float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
			const float w = d2 / (d2 - d6);
			return AddVector3(a, MultiplyVector3(ac, w));
		}

		const float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
			const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			return AddVector3(b, MultiplyVector3(SubtractVector3(c, b), w));
		}

		const float denominator = 1.0f / (va + vb + vc);
		const float v = vb * denominator;
		const float w = vc * denominator;
		return AddVector3(
			a,
			AddVector3(MultiplyVector3(ab, v), MultiplyVector3(ac, w)));
	}

	EditorMeshCollisionTriangle MakeTriangle(
		const VertexData& firstVertex,
		const VertexData& secondVertex,
		const VertexData& thirdVertex,
		const Vector3& colliderCenter,
		const Vector3& objectScale,
		const Vector3& shapeCenterOfMass) {
		EditorMeshCollisionTriangle triangle{};
		triangle.a = {
			(firstVertex.position.x - colliderCenter.x) * objectScale.x - shapeCenterOfMass.x,
			(firstVertex.position.y - colliderCenter.y) * objectScale.y - shapeCenterOfMass.y,
			(firstVertex.position.z - colliderCenter.z) * objectScale.z - shapeCenterOfMass.z};
		triangle.b = {
			(secondVertex.position.x - colliderCenter.x) * objectScale.x - shapeCenterOfMass.x,
			(secondVertex.position.y - colliderCenter.y) * objectScale.y - shapeCenterOfMass.y,
			(secondVertex.position.z - colliderCenter.z) * objectScale.z - shapeCenterOfMass.z};
		triangle.c = {
			(thirdVertex.position.x - colliderCenter.x) * objectScale.x - shapeCenterOfMass.x,
			(thirdVertex.position.y - colliderCenter.y) * objectScale.y - shapeCenterOfMass.y,
			(thirdVertex.position.z - colliderCenter.z) * objectScale.z - shapeCenterOfMass.z};
		triangle.aabbMin = MinVector3(triangle.a, MinVector3(triangle.b, triangle.c));
		triangle.aabbMax = MaxVector3(triangle.a, MaxVector3(triangle.b, triangle.c));
		return triangle;
	}
}

bool EditorMeshCollisionMesh::BuildFromModelData(
	const ModelData& modelData,
	const Vector3& colliderCenter,
	const Vector3& objectScale,
	const Vector3& shapeCenterOfMass) {
	triangles_.clear();
	triangleIndices_.clear();
	nodes_.clear();

	if (modelData.vertices.size() < 3u) {
		return false;
	}

	for (size_t vertexIndex = 0; vertexIndex + 2u < modelData.vertices.size(); vertexIndex += 3u) {
		EditorMeshCollisionTriangle triangle = MakeTriangle(
			modelData.vertices[vertexIndex + 0u],
			modelData.vertices[vertexIndex + 1u],
			modelData.vertices[vertexIndex + 2u],
			colliderCenter,
			objectScale,
			shapeCenterOfMass);

		const Vector3 edgeAB = SubtractVector3(triangle.b, triangle.a);
		const Vector3 edgeAC = SubtractVector3(triangle.c, triangle.a);
		if (LengthSqVector3(edgeAB) <= 0.000001f ||
			LengthSqVector3(edgeAC) <= 0.000001f) {
			continue;
		}

		triangles_.push_back(triangle);
	}

	if (triangles_.empty()) {
		return false;
	}

	triangleIndices_.resize(triangles_.size());
	std::iota(triangleIndices_.begin(), triangleIndices_.end(), 0);

	Vector3 meshMin = triangles_[0].aabbMin;
	Vector3 meshMax = triangles_[0].aabbMax;
	for (const EditorMeshCollisionTriangle& triangle : triangles_) {
		meshMin = MinVector3(meshMin, triangle.aabbMin);
		meshMax = MaxVector3(meshMax, triangle.aabbMax);
	}

	const Vector3 meshSize = SubtractVector3(meshMax, meshMin);
	const float maximumExtent = (std::max)((std::max)(meshSize.x, meshSize.y), meshSize.z);
	contactTolerance_ = (std::clamp)(
		maximumExtent * 0.03f,
		kMeshCollisionMinimumTolerance,
		kMeshCollisionMaximumTolerance);

	BuildNode(0, static_cast<int32_t>(triangleIndices_.size()), 0);
	return !nodes_.empty();
}

bool EditorMeshCollisionMesh::IsPointNearSurface(const Vector3& localPoint) const {
	return IsPointNearSurface(localPoint, 0.0f);
}

bool EditorMeshCollisionMesh::IsPointNearSurface(const Vector3& localPoint, float additionalTolerance) const {
	if (!IsValid()) {
		return false;
	}

	const float queryTolerance = (std::max)(contactTolerance_ + additionalTolerance, contactTolerance_);
	const Vector3 queryMin = {
		localPoint.x - queryTolerance,
		localPoint.y - queryTolerance,
		localPoint.z - queryTolerance};
	const Vector3 queryMax = {
		localPoint.x + queryTolerance,
		localPoint.y + queryTolerance,
		localPoint.z + queryTolerance};
	return QueryNode(0, queryMin, queryMax, localPoint, queryTolerance);
}

bool EditorMeshCollisionMesh::IsValid() const {
	return !triangles_.empty() && !nodes_.empty();
}

float EditorMeshCollisionMesh::GetContactTolerance() const {
	return contactTolerance_;
}

size_t EditorMeshCollisionMesh::GetTriangleCount() const {
	return triangles_.size();
}

int32_t EditorMeshCollisionMesh::BuildNode(
	int32_t firstTriangleIndex,
	int32_t triangleCount,
	int32_t depth) {
	const int32_t nodeIndex = static_cast<int32_t>(nodes_.size());
	nodes_.push_back({});

	Vector3 nodeMin = triangles_[static_cast<size_t>(triangleIndices_[static_cast<size_t>(firstTriangleIndex)])].aabbMin;
	Vector3 nodeMax = triangles_[static_cast<size_t>(triangleIndices_[static_cast<size_t>(firstTriangleIndex)])].aabbMax;
	for (int32_t triangleOffset = 0; triangleOffset < triangleCount; ++triangleOffset) {
		const int32_t triangleIndex = triangleIndices_[static_cast<size_t>(firstTriangleIndex + triangleOffset)];
		const EditorMeshCollisionTriangle& triangle = triangles_[static_cast<size_t>(triangleIndex)];
		nodeMin = MinVector3(nodeMin, triangle.aabbMin);
		nodeMax = MaxVector3(nodeMax, triangle.aabbMax);
	}

	EditorMeshCollisionBvhNode& node = nodes_[static_cast<size_t>(nodeIndex)];
	node.aabbMin = nodeMin;
	node.aabbMax = nodeMax;
	node.leftChildIndex = -1;
	node.rightChildIndex = -1;
	node.firstTriangleIndex = firstTriangleIndex;
	node.triangleCount = triangleCount;

	if (triangleCount <= kMeshCollisionLeafTriangleCount ||
		depth >= kMeshCollisionMaxDepth) {
		return nodeIndex;
	}

	Vector3 centroidMin = MakeTriangleCentroid(triangles_[static_cast<size_t>(triangleIndices_[static_cast<size_t>(firstTriangleIndex)])]);
	Vector3 centroidMax = centroidMin;
	for (int32_t triangleOffset = 0; triangleOffset < triangleCount; ++triangleOffset) {
		const int32_t triangleIndex = triangleIndices_[static_cast<size_t>(firstTriangleIndex + triangleOffset)];
		const Vector3 centroid = MakeTriangleCentroid(triangles_[static_cast<size_t>(triangleIndex)]);
		centroidMin = MinVector3(centroidMin, centroid);
		centroidMax = MaxVector3(centroidMax, centroid);
	}

	const int32_t splitAxis = GetLargestAxis(SubtractVector3(centroidMax, centroidMin));
	auto beginIterator = triangleIndices_.begin() + firstTriangleIndex;
	auto endIterator = beginIterator + triangleCount;
	std::sort(
		beginIterator,
		endIterator,
		[this, splitAxis](int32_t firstTriangleIndexValue, int32_t secondTriangleIndexValue) {
			return GetAxisValue(
				MakeTriangleCentroid(triangles_[static_cast<size_t>(firstTriangleIndexValue)]),
				splitAxis) <
				GetAxisValue(
					MakeTriangleCentroid(triangles_[static_cast<size_t>(secondTriangleIndexValue)]),
					splitAxis);
		});

	const int32_t leftTriangleCount = triangleCount / 2;
	const int32_t rightTriangleCount = triangleCount - leftTriangleCount;
	if (leftTriangleCount <= 0 || rightTriangleCount <= 0) {
		return nodeIndex;
	}

	node.leftChildIndex = BuildNode(firstTriangleIndex, leftTriangleCount, depth + 1);
	node.rightChildIndex = BuildNode(firstTriangleIndex + leftTriangleCount, rightTriangleCount, depth + 1);
	node.firstTriangleIndex = -1;
	node.triangleCount = 0;
	return nodeIndex;
}

bool EditorMeshCollisionMesh::QueryNode(
	int32_t nodeIndex,
	const Vector3& queryMin,
	const Vector3& queryMax,
	const Vector3& localPoint,
	float tolerance) const {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes_.size())) {
		return false;
	}

	const EditorMeshCollisionBvhNode& node = nodes_[static_cast<size_t>(nodeIndex)];
	if (!IsAabbOverlapped(node.aabbMin, node.aabbMax, queryMin, queryMax)) {
		return false;
	}

	if (node.leftChildIndex < 0 && node.rightChildIndex < 0) {
		const float toleranceSq = tolerance * tolerance;
		for (int32_t triangleOffset = 0; triangleOffset < node.triangleCount; ++triangleOffset) {
			const int32_t triangleIndex = triangleIndices_[static_cast<size_t>(node.firstTriangleIndex + triangleOffset)];
			const EditorMeshCollisionTriangle& triangle = triangles_[static_cast<size_t>(triangleIndex)];
			if (!IsAabbOverlapped(triangle.aabbMin, triangle.aabbMax, queryMin, queryMax)) {
				continue;
			}

			const Vector3 closestPoint = ClosestPointOnTriangle(localPoint, triangle.a, triangle.b, triangle.c);
			if (LengthSqVector3(SubtractVector3(localPoint, closestPoint)) <= toleranceSq) {
				return true;
			}
		}

		return false;
	}

	return
		QueryNode(node.leftChildIndex, queryMin, queryMax, localPoint, tolerance) ||
		QueryNode(node.rightChildIndex, queryMin, queryMax, localPoint, tolerance);
}
