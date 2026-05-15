#include "Vector&Matrix.h"

#include <algorithm>


Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;
	result.x = vector.x * matrix.matrix[0][0] + vector.y * matrix.matrix[1][0] + vector.z * matrix.matrix[2][0] + matrix
		.matrix[3][0];
	result.y = vector.x * matrix.matrix[0][1] + vector.y * matrix.matrix[1][1] + vector.z * matrix.matrix[2][1] + matrix
		.matrix[3][1];
	result.z = vector.x * matrix.matrix[0][2] + vector.y * matrix.matrix[1][2] + vector.z * matrix.matrix[2][2] + matrix
		.matrix[3][2];

	result.x /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];
	result.y /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];
	result.z /= vector.x * matrix.matrix[0][3] + vector.y * matrix.matrix[1][3] + vector.z * matrix.matrix[2][3] +
		matrix.matrix[3][3];

	return result;
}


Matrix4x4 MakeTranslationMatrix(const Vector3& translation) {
	Matrix4x4 MakeTransla;
	MakeTransla.matrix[0][0] = 1.0f;
	MakeTransla.matrix[0][1] = 0.0f;
	MakeTransla.matrix[0][2] = 0.0f;
	MakeTransla.matrix[0][3] = 0.0f;

	MakeTransla.matrix[1][0] = 0.0f;
	MakeTransla.matrix[1][1] = 1.0f;
	MakeTransla.matrix[1][2] = 0.0f;
	MakeTransla.matrix[1][3] = 0.0f;

	MakeTransla.matrix[2][0] = 0.0f;
	MakeTransla.matrix[2][1] = 0.0f;
	MakeTransla.matrix[2][2] = 1.0f;
	MakeTransla.matrix[2][3] = 0.0f;

	MakeTransla.matrix[3][0] = translation.x;
	MakeTransla.matrix[3][1] = translation.y;
	MakeTransla.matrix[3][2] = translation.z;
	MakeTransla.matrix[3][3] = 1.0f;

	return MakeTransla;
}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 MakeScale;
	MakeScale.matrix[0][0] = scale.x;
	MakeScale.matrix[0][1] = 0.0f;
	MakeScale.matrix[0][2] = 0.0f;
	MakeScale.matrix[0][3] = 0.0f;
	MakeScale.matrix[1][0] = 0.0f;
	MakeScale.matrix[1][1] = scale.y;
	MakeScale.matrix[1][2] = 0.0f;
	MakeScale.matrix[1][3] = 0.0f;
	MakeScale.matrix[2][0] = 0.0f;
	MakeScale.matrix[2][1] = 0.0f;
	MakeScale.matrix[2][2] = scale.z;
	MakeScale.matrix[2][3] = 0.0f;
	MakeScale.matrix[3][0] = 0.0f;
	MakeScale.matrix[3][1] = 0.0f;
	MakeScale.matrix[3][2] = 0.0f;
	MakeScale.matrix[3][3] = 1.0f;
	return MakeScale;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 affineMatrix;
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 rotateXMatrixYZMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 translateMatrix = MakeTranslationMatrix(translate);
	affineMatrix = Multiply(Multiply(scaleMatrix, rotateXMatrixYZMatrix), translateMatrix);
	return affineMatrix;
}

//void DrawSphere(const Sphere& sphere, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix,
//                uint32_t color) {
//	constexpr uint32_t kSegmentCount = 16;
//	constexpr float kLonEvery = 2.0f * 3.14159265f / static_cast<float>(kSegmentCount);
//	constexpr float kLatEvery = 3.14159265f / static_cast<float>(kSegmentCount);
//
//	for (uint32_t latIndex = 0; latIndex < kSegmentCount; ++latIndex) {
//		float lat = -3.14159265f / 2.0f + kLatEvery * latIndex;
//		for (uint32_t lonIndex = 0; lonIndex < kSegmentCount; ++lonIndex) {
//			float lon = kLonEvery * lonIndex;
//
//			Vector3 a{
//				sphere.center.x + sphere.radius * std::cos(lat) * std::cos(lon),
//				sphere.center.y + sphere.radius * std::sin(lat),
//				sphere.center.z + sphere.radius * std::cos(lat) * std::sin(lon),
//			};
//			Vector3 b{
//				sphere.center.x + sphere.radius * std::cos(lat + kLatEvery) * std::cos(lon),
//				sphere.center.y + sphere.radius * std::sin(lat + kLatEvery),
//				sphere.center.z + sphere.radius * std::cos(lat + kLatEvery) * std::sin(lon),
//			};
//			Vector3 c{
//				sphere.center.x + sphere.radius * std::cos(lat) * std::cos(lon + kLonEvery),
//				sphere.center.y + sphere.radius * std::sin(lat),
//				sphere.center.z + sphere.radius * std::cos(lat) * std::sin(lon + kLonEvery),
//			};
//
//			Vector3 screenA = Transform(Transform(a, viewProjectionMatrix), viewportMatrix);
//			Vector3 screenB = Transform(Transform(b, viewProjectionMatrix), viewportMatrix);
//			Vector3 screenC = Transform(Transform(c, viewProjectionMatrix), viewportMatrix);
//
//			Novice::DrawLine(static_cast<int>(screenA.x), static_cast<int>(screenA.y), static_cast<int>(screenB.x),
//			                 static_cast<int>(screenB.y), color);
//			Novice::DrawLine(static_cast<int>(screenA.x), static_cast<int>(screenA.y), static_cast<int>(screenC.x),
//			                 static_cast<int>(screenC.y), color);
//		}
//	}
//}

Vector3 Project(const Vector3& v1, const Vector3& v2) {
	float dotProduct = Dot(v1, v2);
	float lengthSquared = Dot(v2, v2);
	if (lengthSquared == 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}
	return Multiply(dotProduct / lengthSquared, v2);
}


Vector3 ClosestPoint(const Vector3& point, const Segment& segment) {
	Vector3 segmentVector = Subtract(segment.diff, segment.origin);
	Vector3 pointVector = Subtract(point, segment.origin);
	float t = Dot(pointVector, segmentVector) / Dot(segmentVector, segmentVector);

	return Add(segment.origin, Multiply(t, segmentVector));
}

bool IsCollision(const Sphere& s1, const Sphere& s2) {
	float distance = Length(Subtract(s1.center, s2.center));
	return distance <= s1.radius + s2.radius;
}

bool PlaneIsCollision(const Sphere& sphere, const Plane& plane) {
	float distance = Dot(sphere.center, plane.normal) - plane.distance;
	return std::fabs(distance) <= sphere.radius;
}

Vector3 Perpendicular(const Vector3& v) {
	if (v.x != 0.0f || v.y != 0.0f) {
		return {-v.y, v.x, 0.0f};
	}
	return {0.0f, -v.z, v.y};
}

//void DrawPlane(
//	const Plane& plane,
//	const Matrix4x4& viewProjectionMatrix,
//	const Matrix4x4& viewportMatrix,
//	uint32_t color
//) {
//	Vector3 center = Multiply(plane.distance, plane.normal);
//
//	Vector3 perpendicular[4];
//	perpendicular[0] = Perpendicular(plane.normal);
//	perpendicular[1] = {
//		-perpendicular[0].x,
//		-perpendicular[0].y,
//		-perpendicular[0].z
//	};
//
//	perpendicular[2] = Cross(plane.normal, perpendicular[0]);
//	perpendicular[3] = {
//		-perpendicular[2].x,
//		-perpendicular[2].y,
//		-perpendicular[2].z
//	};
//
//	Vector3 points[4];
//
//	for (int32_t index = 0; index < 4; ++index) {
//		Vector3 extend = Multiply(2.0f, perpendicular[index]);
//		Vector3 point = Add(center, extend);
//		points[index] = Transform(
//			Transform(point, viewProjectionMatrix),
//			viewportMatrix
//		);
//	}
//
//	Novice::DrawLine(
//		static_cast<int>(points[0].x),
//		static_cast<int>(points[0].y),
//		static_cast<int>(points[2].x),
//		static_cast<int>(points[2].y),
//		color
//	);
//
//	Novice::DrawLine(
//		static_cast<int>(points[2].x),
//		static_cast<int>(points[2].y),
//		static_cast<int>(points[1].x),
//		static_cast<int>(points[1].y),
//		color
//	);
//
//	Novice::DrawLine(
//		static_cast<int>(points[1].x),
//		static_cast<int>(points[1].y),
//		static_cast<int>(points[3].x),
//		static_cast<int>(points[3].y),
//		color
//	);
//
//	Novice::DrawLine(
//		static_cast<int>(points[3].x),
//		static_cast<int>(points[3].y),
//		static_cast<int>(points[0].x),
//		static_cast<int>(points[0].y),
//		color
//	);
//}
