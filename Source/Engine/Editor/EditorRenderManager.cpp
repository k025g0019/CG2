#pragma warning(disable : 4189 4514)

#include "EditorRenderManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorSharedState.h"

#include <cmath>
#include <cstring>

using namespace EditorSharedState;

namespace {
	int32_t GetLightTypeFromComponent(const EditorComponent& component) {
		if (component.assetPath == "Sun") {
			return 0;
		}

		if (component.assetPath == "Spot") {
			return 2;
		}

		if (component.assetPath == "Area") {
			return 3;
		}

		return 1; // 隴幢�E�E�E��E�E�E�髫�E�E�E�・�E�E�E�陞ｳ螢�E�E�E�・・Point 邵�E�E�E�・�E�E�E� Point Light 邵�E�E�E�・�E�E�E�邵�E�E�E�蜉ｱ窶�E�E�E�隰・�E�E�E��E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	}

	Vector3 GetForwardDirectionFromRotation(const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		// 陜玲�E�E�E�・�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�莉｣繝ｻ髯�E�E�E�謔溘�E郢�E�E�E�蜑�E�E�E�E��E�E�E�諛奁E�E��E�顔ｸ�E�E�E�竏墁E�E��E��E�E�E�晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�陷題ざ蟀�E�E�E�郢晏生縺醍ｹ晏現�E�E�E�晉�E�E��E�E�E�蛛ｵ�E�E�E��E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晏ｳ�E�E�E�竏郁惺莉｣・�E�E�E�郢�E�E�E�荵敖繝ｻ
		const Vector3 worldForward = Transform({0.0f, 0.0f, 1.0f}, rotationMatrix);
		return Normalize(worldForward);
	}

	Vector3 GetUpDirectionFromRotation(const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 worldUp = Transform({0.0f, 1.0f, 0.0f}, rotationMatrix);
		return Normalize(worldUp);
	}

	Vector3 GetRightDirectionFromRotation(const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		const Vector3 worldRight = Transform({1.0f, 0.0f, 0.0f}, rotationMatrix);
		return Normalize(worldRight);
	}

	Vector3 ReflectPointAcrossPlane(const Vector3& point, const Vector3& planePoint, const Vector3& planeNormal) {
		const Vector3 offset = Subtract(point, planePoint);
		const float signedDistance = Dot(offset, planeNormal);
		return Subtract(point, Multiply(2.0f * signedDistance, planeNormal));
	}

	Vector3 ReflectDirectionAcrossPlane(const Vector3& direction, const Vector3& planeNormal) {
		const float signedDistance = Dot(direction, planeNormal);
		return Normalize(Subtract(direction, Multiply(2.0f * signedDistance, planeNormal)));
	}

	Matrix4x4 MakePlanarReflectionMatrix(const Vector3& planePoint, const Vector3& planeNormal) {
		const Vector3 normal = Normalize(planeNormal);
		const float planeDistance = Dot(planePoint, normal);

		Matrix4x4 reflectionMatrix = MakeIdentity4x4();
		reflectionMatrix.matrix[0][0] = 1.0f - 2.0f * normal.x * normal.x;
		reflectionMatrix.matrix[0][1] = -2.0f * normal.x * normal.y;
		reflectionMatrix.matrix[0][2] = -2.0f * normal.x * normal.z;
		reflectionMatrix.matrix[1][0] = -2.0f * normal.y * normal.x;
		reflectionMatrix.matrix[1][1] = 1.0f - 2.0f * normal.y * normal.y;
		reflectionMatrix.matrix[1][2] = -2.0f * normal.y * normal.z;
		reflectionMatrix.matrix[2][0] = -2.0f * normal.z * normal.x;
		reflectionMatrix.matrix[2][1] = -2.0f * normal.z * normal.y;
		reflectionMatrix.matrix[2][2] = 1.0f - 2.0f * normal.z * normal.z;
		reflectionMatrix.matrix[3][0] = 2.0f * planeDistance * normal.x;
		reflectionMatrix.matrix[3][1] = 2.0f * planeDistance * normal.y;
		reflectionMatrix.matrix[3][2] = 2.0f * planeDistance * normal.z;

		return reflectionMatrix;
	}

	//================================================================
	// 平面反射に使う面の情報
	//================================================================

	enum class PlanarReflectionAxis {
		X,
		Y,
		Z,
	};

	struct PlanarReflectionSurface {
		Vector3 normal;  // normal はカメラ側を向く反射面のワールド法線。
		Vector3 tangent;  // tangent は反射面の横方向。
		Vector3 bitangent;  // bitangent は反射面の縦方向。
		Vector3 point;  // point は実際に反射させる表面上のワールド座標。
		float halfExtentTangent;  // halfExtentTangent は横方向の反射範囲の半分。
		float halfExtentBitangent;  // halfExtentBitangent は縦方向の反射範囲の半分。
	};

	Vector3 GetPlanarReflectionLocalMeshSize(const EditorSceneObject& sceneObject) {
		// FBX / OBJ は読み込み時に計算した実メッシュのローカル AABB を使う。
		if (sceneObject.usesCustomMesh &&
			Length(sceneObject.customMeshLocalBoundsSize) > 0.0001f) {
			return sceneObject.customMeshLocalBoundsSize;
		}

		// 内部基本形は生成時のローカル寸法と同じ値を返す。
		switch (sceneObject.meshType) {
		case EditorModelMeshType::Plane:
			return {1.0f, 1.0f, 0.0f};
		case EditorModelMeshType::Box:
			return {1.6f, 0.7f, 1.0f};
		case EditorModelMeshType::Cube:
		case EditorModelMeshType::Cylinder:
		case EditorModelMeshType::Cone:
		case EditorModelMeshType::Torus:
		case EditorModelMeshType::Ico:
		case EditorModelMeshType::Sphere:
		case EditorModelMeshType::Count:
		default:
			return {1.0f, 1.0f, 1.0f};
		}
	}

	Vector3 GetPlanarReflectionLocalMeshCenter(const EditorSceneObject& sceneObject) {
		if (sceneObject.usesCustomMesh &&
			Length(sceneObject.customMeshLocalBoundsSize) > 0.0001f) {
			return sceneObject.customMeshLocalBoundsCenter;
		}

		return {0.0f, 0.0f, 0.0f};
	}

	PlanarReflectionAxis GetPlanarReflectionAxis(
		const EditorSceneObject& sceneObject,
		const EditorGameObject& gameObject) {
		const Vector3 localMeshSize = GetPlanarReflectionLocalMeshSize(sceneObject);
		const Vector3 worldMeshSize = {
			std::abs(localMeshSize.x * gameObject.scale.x),
			std::abs(localMeshSize.y * gameObject.scale.y),
			std::abs(localMeshSize.z * gameObject.scale.z)
		};

		// 同じ厚さなら床として最も一般的な Y 面を優先する。
		PlanarReflectionAxis reflectionAxis = PlanarReflectionAxis::Y;
		float minimumThickness = worldMeshSize.y;

		if (worldMeshSize.x < minimumThickness) {
			reflectionAxis = PlanarReflectionAxis::X;
			minimumThickness = worldMeshSize.x;
		}

		if (worldMeshSize.z < minimumThickness) {
			reflectionAxis = PlanarReflectionAxis::Z;
		}

		return reflectionAxis;
	}

	PlanarReflectionSurface BuildPlanarReflectionSurface(
		const EditorSceneObject& sceneObject,
		const EditorGameObject& gameObject,
		const EditorComponent& reflectionProbeComponent,
		const Vector3& cameraPosition) {
		const Vector3 localMeshSize = GetPlanarReflectionLocalMeshSize(sceneObject);
		const Vector3 localMeshCenter = GetPlanarReflectionLocalMeshCenter(sceneObject);
		const PlanarReflectionAxis reflectionAxis = GetPlanarReflectionAxis(sceneObject, gameObject);
		const Matrix4x4 objectWorldMatrix = MakeAffineMatrix(
			gameObject.scale,
			gameObject.rotate,
			gameObject.translate);
		const Vector3 localReflectionCenter = Add(
			localMeshCenter,
			reflectionProbeComponent.colliderCenter);
		const Vector3 reflectionVolumeCenter = Transform(
			localReflectionCenter,
			objectWorldMatrix);

		const Vector3 objectRight = GetRightDirectionFromRotation(gameObject.rotate);
		const Vector3 objectUp = GetUpDirectionFromRotation(gameObject.rotate);
		const Vector3 objectForward = GetForwardDirectionFromRotation(gameObject.rotate);

		PlanarReflectionSurface reflectionSurface{};
		float surfaceHalfThickness = 0.0f;

		switch (reflectionAxis) {
		case PlanarReflectionAxis::X:
			reflectionSurface.normal = objectRight;
			reflectionSurface.tangent = objectUp;
			reflectionSurface.bitangent = objectForward;
			reflectionSurface.halfExtentTangent = std::abs(
				localMeshSize.y * gameObject.scale.y) * 0.5f;
			reflectionSurface.halfExtentBitangent = std::abs(
				localMeshSize.z * gameObject.scale.z) * 0.5f;
			surfaceHalfThickness = std::abs(localMeshSize.x * gameObject.scale.x) * 0.5f;
			break;

		case PlanarReflectionAxis::Z:
			reflectionSurface.normal = objectForward;
			reflectionSurface.tangent = objectRight;
			reflectionSurface.bitangent = objectUp;
			reflectionSurface.halfExtentTangent = std::abs(
				localMeshSize.x * gameObject.scale.x) * 0.5f;
			reflectionSurface.halfExtentBitangent = std::abs(
				localMeshSize.y * gameObject.scale.y) * 0.5f;
			surfaceHalfThickness = std::abs(localMeshSize.z * gameObject.scale.z) * 0.5f;
			break;

		case PlanarReflectionAxis::Y:
		default:
			reflectionSurface.normal = objectUp;
			reflectionSurface.tangent = objectRight;
			reflectionSurface.bitangent = objectForward;
			reflectionSurface.halfExtentTangent = std::abs(
				localMeshSize.x * gameObject.scale.x) * 0.5f;
			reflectionSurface.halfExtentBitangent = std::abs(
				localMeshSize.z * gameObject.scale.z) * 0.5f;
			surfaceHalfThickness = std::abs(localMeshSize.y * gameObject.scale.y) * 0.5f;
			break;
		}

		// 薄い Box を床や壁にした場合は、カメラに近い外側の面を反射面にする。
		const Vector3 cameraFromCenter = Subtract(cameraPosition, reflectionVolumeCenter);
		if (Dot(cameraFromCenter, reflectionSurface.normal) < 0.0f) {
			reflectionSurface.normal = Multiply(-1.0f, reflectionSurface.normal);
		}

		reflectionSurface.point = Add(
			reflectionVolumeCenter,
			Multiply(surfaceHalfThickness, reflectionSurface.normal));
		reflectionSurface.halfExtentTangent = (std::max)(reflectionSurface.halfExtentTangent, 0.05f);
		reflectionSurface.halfExtentBitangent = (std::max)(reflectionSurface.halfExtentBitangent, 0.05f);

		return reflectionSurface;
	}

	void ApplySceneLightToDirectionalBuffer(DirectionalLight& directionalLightData) {
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			for (const EditorComponent& component : gameObject.components) {
				if (component.type != EditorComponentType::Light || !component.isActive) {
					continue;
				}

				directionalLightData.color = {
					component.color.x,
					component.color.y,
					component.color.z,
					1.0f
				};
				directionalLightData.intensity = component.intensity;
				directionalLightData.position = gameObject.translate;
				directionalLightData.range = (std::max)(component.colliderRadius, 0.01f);
				directionalLightData.areaRadius = (std::max)(component.colliderSize.z, 0.01f);
				directionalLightData.lightType = GetLightTypeFromComponent(component);

				const float innerAngleRadian = component.colliderSize.x * (3.14159265f / 180.0f);
				const float outerAngleRadian = component.colliderSize.y * (3.14159265f / 180.0f);
				directionalLightData.spotCosInner = std::cos(innerAngleRadian);
				directionalLightData.spotCosOuter = std::cos(outerAngleRadian);

				Vector3 forwardDirection = GetForwardDirectionFromRotation(gameObject.rotate);
				if (Length(forwardDirection) <= 0.0001f) {
					forwardDirection = {0.0f, -1.0f, 0.0f};
				}

				directionalLightData.direction = Normalize(forwardDirection);
				return;
			}
		}

		directionalLightData.color = {1.0f, 1.0f, 1.0f, 1.0f};
		directionalLightData.direction = Normalize({0.35f, -1.0f, 0.25f});
		directionalLightData.intensity = 0.0f;
		directionalLightData.position = {0.0f, 3.0f, -3.0f};
		directionalLightData.range = 25.0f;
		directionalLightData.lightType = 0;
		directionalLightData.areaRadius = 1.0f;
	}

	

	float GetMaxAbsScale(const Vector3& scale) {
		float maxScale = (std::max)(std::fabs(scale.x), std::fabs(scale.y));
		maxScale = (std::max)(maxScale, std::fabs(scale.z));
		return (std::max)(maxScale, 0.01f);
	}

	float GetObjectShadowRadius(const EditorSceneObject& sceneObject) {
		if (sceneObject.usesCustomMesh) {
			const Vector3 localHalfSize = {
				sceneObject.customMeshLocalBoundsSize.x * 0.5f,
				sceneObject.customMeshLocalBoundsSize.y * 0.5f,
				sceneObject.customMeshLocalBoundsSize.z * 0.5f
			}; // 陞ｳ貁E�E��E�鍋ｹ昴・縺咏ｹ晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�陷企宦・�E�E�E�繝ｻ繝ｻ邵�E�E�E�竏ｬ・�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�髴趣�E�E�E��E�E�E�郢�E�E�E�阮吮味 AABB 邵�E�E�E�荵晢�E�E�E�臥�E�E�E��E�E�E�譏ｴ繝ｻ邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�髴大床�E�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
			const Vector3 scaledHalfSize = {
				std::fabs(localHalfSize.x * sceneObject.transform.scale.x),
				std::fabs(localHalfSize.y * sceneObject.transform.scale.y),
				std::fabs(localHalfSize.z * sceneObject.transform.scale.z)
			};
			const float worldRadius = std::sqrt(
				scaledHalfSize.x * scaledHalfSize.x +
				scaledHalfSize.y * scaledHalfSize.y +
				scaledHalfSize.z * scaledHalfSize.z);
			return (std::max)(worldRadius, 0.75f);
		}

		float meshRadius = 1.25f;
		// 陜難�E�E�E��E�E�E�隴幢�E�E�E��E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E�驕擾�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢�E�E�E�・�E�E�E�郢�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�竏晢�E�E�E��E�E�E�謐ｺ謫らｸ�E�E�E�・�E�E�E�邵�E�E�E�鄙ｫ・企怕�E�E�E�E�邵�E�E�E�繝ｻ・�E�E�E�蜿�E�E�E�・�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�郢�E�E�E�蛹�E�E�E�・樒ｸ�E�E�E�繝ｻ
		return GetMaxAbsScale(sceneObject.transform.scale) * meshRadius;
	}

	Vector3 GetSafeLightDirection(const DirectionalLight* directionalLightData) {
		Vector3 lightDirection = {0.35f, -1.0f, 0.25f};
		// 郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晏沺謔ｴ騾墓ｻ薙�E隴弱�E�E�E�E�E��E�堤�E�E�E��E�E�E�繧井ｸ・�E�E�E��E�E�E�竏ｽ・�E�E�E�鄙ｫ�E�E�E��E�郢�E�E�E�閾�E�E�E�繝ｻ郢�E�E�E�蟲�E�E�E�笘�E�E�E�E�鯉ｽ�E�E�E�陞ｳ螢�E�E�E�蟀�E�E�E�陷�E�E�E�莉｣�E�E�E�繝ｻ
		if (directionalLightData != nullptr) {
			lightDirection = directionalLightData->direction;
		}

		if (Length(lightDirection) <= 0.0001f) {
			return Normalize(Vector3{0.35f, -1.0f, 0.25f});
		}

		return Normalize(lightDirection);
	}

	Vector3 CalculateShadowCenter(
		const std::vector<EditorSceneObject>& editorSceneObjects,
		const Transforms& legacyTransform,
		bool isLegacyPreviewVisible) {
		Vector3 center{}; // center 邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晏現繝ｻ雎�E�E�E�E��E�E�E�陝�E・・�E�E�E�・�E�E�E�郢�E�E�E�雋樣�E�E�E�・�E�E�E��E�E�E�莉｣・・Scene 邵�E�E�E�・�E�E�E�闕ｳ・�E�E�E�陟｢繝ｻ・�E�E�E�蜥�E�E�E�・�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
		int32_t modelCount = 0;

		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type != EditorSceneObjectType::Model) {
				continue;
			}

			center = Add(center, sceneObject.transform.translate);
			modelCount++;
		}

		if (isLegacyPreviewVisible) {
			center = Add(center, legacyTransform.translate);
			modelCount++;
		}

		if (modelCount <= 0) {
			return {0.0f, 0.0f, 0.0f};
		}

		float inverseModelCount = 1.0f / static_cast<float>(modelCount);
		return Multiply(inverseModelCount, center);
	}

	float CalculateShadowRadius(
		const std::vector<EditorSceneObject>& editorSceneObjects,
		const Transforms& legacyTransform,
		const Vector3& shadowCenter,
		bool isLegacyPreviewVisible) {
		float shadowRadius = 6.0f;
		// 隴崢闖ｴ螳茨�E�E�E��E�E�E�繝ｻ蟲・�E�E�E��E�E�E�雋橸�E�E�E��E�E�E�荳奁E�E��E�・�E�E�E��E�E�E�荳奁E�E��E��E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�竏ｬ・�E�E�E�鬘鯉ｽ�E�E�E�譎槫�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晁E�E��E�縺夂ｹ�E�E�E�・�E�E�E�郢�E�E�E�・�E�E�E�郢晏現繝ｻ陟厄�E�E�E��E�E�E�髫暦�E�E�E��E�E�E�陷剁E�E��E�橸�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�蝣�E�E�E�・�E�E�E�・�E�E�E�闖ｫ譏ｴ笘�E�E�E�E��E�E�E�荵敖繝ｻ

		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type != EditorSceneObjectType::Model) {
				continue;
			}

			float distanceFromCenter = Length(Subtract(sceneObject.transform.translate, shadowCenter));
			shadowRadius = (std::max)(shadowRadius, distanceFromCenter + GetObjectShadowRadius(sceneObject));
		}

		if (isLegacyPreviewVisible) {
			float legacyRadius = GetMaxAbsScale(legacyTransform.scale) * 2.0f;
			float distanceFromCenter = Length(Subtract(legacyTransform.translate, shadowCenter));
			shadowRadius = (std::max)(shadowRadius, distanceFromCenter + legacyRadius);
		}

		// 陟弱・笘�E�E�E�E��E�E�E�蠑ｱ・玖厁E�E��E��E�E�E�驕ｽ繝ｻ蟲・�E�E�E��E�E�E�・�E�E�E�髫暦�E�E�E��E�E�E�陷剁E�E��E�橸�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�蜻茨�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�笳・�E�E�E��E�E�E�竏堋繝ｾceneView 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E鬮�E�E�E�蜊�E・邵�E�E�E�・�E�E�E�陷�E�E�E�蛹�E�E�E�・冗ｸ�E�E�E�蟶吮�E�E�E�闕ｳ莨∝応郢�E�E�E�蜻域亜邵�E�E�E�貁E�E��E�雷郢�E�E�E�荵敖繝ｻ
		return (std::clamp)(shadowRadius + 3.0f, 6.0f, 180.0f);
	}

	Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up) {
		Vector3 zAxis = Normalize(Subtract(target, eye)); // zAxis 邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晏現縺咲�E�E�E�晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�謔滁E�E��E�・�E�E�E��E�E�E�荳樒�E隴・�E�E�E��E�E�E�陷�E�E�E�莉｣�E�E�E�繝ｻ
		if (Length(zAxis) <= 0.0001f) {
			zAxis = {0.0f, 0.0f, 1.0f};
		}

		Vector3 xAxis = Normalize(Cross(up, zAxis));
		// xAxis 邵�E�E�E�・�E�E�E�騾匁E�E��E��E�E�E�鬮�E�E�E�・�E�E�E�陷�E�E�E�・�E�E�E�隴・�E�E�E��E�E�E�陷�E�E�E�莉｣�E�E�E�・�E�E�E�p 邵�E�E�E�・�E�E�E�陝ｷ・�E�E�E�髯�E�E�E�蠕娯・郢�E�E�E�謌托�E�E�E��E�E�E�・�E�E�E�隴厁E�E��E��E�E�E� up 郢�E�E�E�蜑�E�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
		if (Length(xAxis) <= 0.0001f) {
			xAxis = Normalize(Cross(Vector3{1.0f, 0.0f, 0.0f}, zAxis));
		}

		Vector3 yAxis = Cross(zAxis, xAxis); // yAxis 邵�E�E�E�・�E�E�E�陷題ざ蟀�E�E�E�陷�E�E�E�莉｣竊定愾・�E�E�E�隴・�E�E�E��E�E�E�陷�E�E�E�莉｣�E�E�E��E�郢�E�E�E�謌托�E�E�E��E�E�E�諛奁E�E��E�玖叉鬁E�E��E��E�E�E��E�E�E�陷�E�E�E�莉｣�E�E�E�繝ｻ

		Matrix4x4 viewMatrix{};
		viewMatrix.matrix[0][0] = xAxis.x;
		viewMatrix.matrix[0][1] = yAxis.x;
		viewMatrix.matrix[0][2] = zAxis.x;
		viewMatrix.matrix[0][3] = 0.0f;
		viewMatrix.matrix[1][0] = xAxis.y;
		viewMatrix.matrix[1][1] = yAxis.y;
		viewMatrix.matrix[1][2] = zAxis.y;
		viewMatrix.matrix[1][3] = 0.0f;
		viewMatrix.matrix[2][0] = xAxis.z;
		viewMatrix.matrix[2][1] = yAxis.z;
		viewMatrix.matrix[2][2] = zAxis.z;
		viewMatrix.matrix[2][3] = 0.0f;
		viewMatrix.matrix[3][0] = -Dot(xAxis, eye);
		viewMatrix.matrix[3][1] = -Dot(yAxis, eye);
		viewMatrix.matrix[3][2] = -Dot(zAxis, eye);
		viewMatrix.matrix[3][3] = 1.0f;

		return viewMatrix;
	}

	Matrix4x4 MakeLightViewProjectionMatrix(
		const std::vector<EditorSceneObject>& editorSceneObjects,
		const Transforms& legacyTransform,
		const DirectionalLight* directionalLightData,
		bool isLegacyPreviewVisible) {
		Vector3 shadowCenter = CalculateShadowCenter(editorSceneObjects, legacyTransform, isLegacyPreviewVisible);
		float shadowRadius = CalculateShadowRadius(
			editorSceneObjects,
			legacyTransform,
			shadowCenter,
			isLegacyPreviewVisible);

		Vector3 lightEye;
		Vector3 lightTarget = shadowCenter;

		if (directionalLightData != nullptr && directionalLightData->lightType == 0) {
			// Sun: direction-based, place eye far behind center
			Vector3 lightDirection = GetSafeLightDirection(directionalLightData);
			lightEye = Subtract(shadowCenter, Multiply(shadowRadius * 2.0f, lightDirection));
		} else {
			// Point/Spot/Area: position-based, look from light toward center
			Vector3 lightPos = directionalLightData != nullptr
				? directionalLightData->position
				: Vector3{0.0f, 3.0f, -3.0f};
			lightEye = lightPos;
			lightTarget = shadowCenter;
		}

		Matrix4x4 lightViewMatrix = MakeLookAtMatrix(lightEye, lightTarget, Vector3{0.0f, 1.0f, 0.0f});
		Matrix4x4 lightProjectionMatrix = MakeOrthographicMatrix(
			-shadowRadius,
			shadowRadius,
			shadowRadius,
			-shadowRadius,
			0.1f,
			shadowRadius * 4.0f + 50.0f);

		return Multiply(lightViewMatrix, lightProjectionMatrix);
	}
}

void EditorRenderManager::Initialize() {
}

void EditorRenderManager::Update() {
}

void EditorRenderManager::Draw() {
	static bool hasLoggedFirstRenderEnter = false; // 隴崢陋ｻ譏ｴ繝ｻ Draw 邵�E�E�E�・�E�E�E�陷茨�E�E�E��E�E�E�郢�E�E�E�蠕娯螺邵�E�E�E�荵晢�E�E�E�・1 陜玲�E�E�E��E�E�E�笁E�E��E�邵�E�E�E�鬘鯉ｽ�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	static bool hasLoggedFirstPresent = false;
	// 隴崢陋ｻ譏ｴ繝ｻ Present 邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�陋ｻ・�E�E�E�鬩墓鱒�E�E�E�E�邵�E�E�E�貁E�E��E��E�郢�E�E�E�繝ｻ1 陜玲�E�E�E��E�E�E�笁E�E��E�邵�E�E�E�鬘鯉ｽ�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ

	auto& hr = g_hr; // hr 邵�E�E�E�・�E�E�E� DirectX API 邵�E�E�E�・�E�E�E�隰御�E�E�E�吝℡郢�E�E�E�雋槫�E�E�E��E�E�E�邵�E�E�E�螟ｧ蜿咏ｹ�E�E�E�蜿�E�E�E�繝ｻ隴帙�EHRESULT邵�E�E�E�繝ｻ
	auto& device = g_device;
	// device 邵�E�E�E�・�E�E�E�闔蛾宦・�E�E�E�蠕後�E隰�E�E�E�蜀怜�E隲�E�E�E�・�E�E�E�陟托�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� Resource 郢�E�E�E�螳夲�E�E�E��E�E�E�・�E�E�E�陷会｣�E�E�E�闖ｴ諛医・邵�E�E�E�蜷�E�E�E�・玖ｭ弱�E�E�E�E�E��E��E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�陷�E�E�E�繧峨・邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	auto& commandQueue = g_commandQueue;
	// commandQueue / Allocator / List 邵�E�E�E�・�E�E�E� GPU 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E陷�E�E�E�・�E�E�E�闔会ｽ�E�E�E�郢�E�E�E�蟶敖竏夲�E�E�E�狗ｸ�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& commandAllocator = g_commandAllocator;
	auto& commandList = g_commandList;

	auto& swapChain = g_swapChain;
	// swapChain 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E邵�E�E�E�蜉ｱ笳・back buffer 郢�E�E�E�繝ｻWindow 邵�E�E�E�・�E�E�E� Present 邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& srvDescriptorHeap = g_srvDescriptorHeap;
	// srvDescriptorHeap 邵�E�E�E�・�E�E�E� Texture SRV 邵�E�E�E�・�E�E�E� ImGui SRV 郢�E�E�E�繝ｻShader 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�繝ｻDescriptorHeap邵�E�E�E�繝ｻ
	auto& swapChainResources = g_swapChainResources;
	// swapChainResources 邵�E�E�E�・�E�E�E� Barrier 陝�E�E�E�E��E�E�E�髮趣�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� back buffer 陞ｳ貊会ｽ�E�E�E�阮卍繝ｻ
	auto& rtvHandles = g_rtvHandles;
	// rtvHandles / dsvHandle 邵�E�E�E�・�E�E�E� RenderTarget 邵�E�E�E�・�E�E�E� DepthStencil 郢�E�E�E�繝ｻOMSetRenderTargets 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E��E�E�E�繝ｻ
	auto& dsvHandle = g_dsvHandle;
	auto& depthStencilResource = g_depthStencilResource;
	auto& depthSrvHandleGPU = g_depthSrvHandleGPU;

	auto& rootSignature = g_rootSignature;
	// rootSignature / graphicsPipelineState 邵�E�E�E�・�E�E�E� Shader 邵�E�E�E�・�E�E�E� RenderState 邵�E�E�E�・�E�E�E�陜暦�E�E�E��E�E�E�陞ｳ螟奁E�E��E��E�E�E�・�E�E�E�陞ｳ螢�E�E�E��E�E�E�繝ｻ
	auto& graphicsPipelineState = g_graphicsPipelineState;
	auto& planarScenePipelineState = g_planarScenePipelineState;
	auto& objectReflectionMaskPipelineState = g_objectReflectionMaskPipelineState;
	auto& shadowPipelineState = g_shadowPipelineState;
	// shadowPipelineState 邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晞メ・�E�E�E�荵溘○邵�E�E�E�・�E�E�E� DepthTexture 郢�E�E�E�蜑�E�E�E�E��E�E�E�諛奁E�E��E�玖氣繧臥舁EPSO邵�E�E�E�繝ｻ
	auto& shadowMapResource = g_shadowMapResource;
	auto& shadowDsvHandle = g_shadowDsvHandle;
	auto& shadowMapSrvGpuHandle = g_shadowMapSrvGpuHandle;

	auto& hdrRenderTarget = g_hdrRenderTarget;
	auto& hdrRtvHandle = g_hdrRtvHandle;
	auto& hdrSrvHandleGPU = g_hdrSrvHandleGPU;
	auto& postProcessRootSignature = g_postProcessRootSignature;
	auto& toneMappingPipelineState = g_toneMappingPipelineState;
	auto& bloomRenderTargets = g_bloomRenderTargets;
	auto& bloomRtvHandles = g_bloomRtvHandles;
	auto& bloomSrvHandlesGPU = g_bloomSrvHandlesGPU;
	auto& bloomExtractPipelineState = g_bloomExtractPipelineState;
	auto& bloomBlurPipelineState = g_bloomBlurPipelineState;
	auto& postProcessRenderTarget = g_postProcessRenderTarget;
	auto& postProcessRtvHandle = g_postProcessRtvHandle;
	auto& postProcessSrvHandleGPU = g_postProcessSrvHandleGPU;
	auto& fxaaPipelineState = g_fxaaPipelineState;
	auto& ssaoRenderTargets = g_ssaoRenderTargets;
	auto& ssaoRtvHandles = g_ssaoRtvHandles;
	auto& ssaoSrvHandlesGPU = g_ssaoSrvHandlesGPU;
	auto& ssaoPipelineState = g_ssaoPipelineState;
	auto& ssaoBlurPipelineState = g_ssaoBlurPipelineState;
	auto& hdrCompositeRenderTarget = g_hdrCompositeRenderTarget;
	auto& hdrCompositeRtvHandle = g_hdrCompositeRtvHandle;
	auto& hdrCompositeSrvHandleGPU = g_hdrCompositeSrvHandleGPU;
	auto& skyboxPipelineState = g_skyboxPipelineState;
	auto& planarReflectionPipelineState = g_planarReflectionPipelineState;
	auto& sharpenPipelineState = g_sharpenPipelineState;
	auto& materialMaskRenderTarget = g_materialMaskRenderTarget;
	auto& materialMaskRtvHandle = g_materialMaskRtvHandle;
	auto& materialMaskSrvHandleGPU = g_materialMaskSrvHandleGPU;
	auto& planarReflectionRenderTarget = g_planarReflectionRenderTarget;
	auto& planarReflectionRtvHandle = g_planarReflectionRtvHandle;
	auto& planarReflectionSrvHandleGPU = g_planarReflectionSrvHandleGPU;
	auto& iblIrradianceSrvHandleGPU = g_iblIrradianceSrvHandleGPU;
	auto& iblPrefilterSrvHandleGPU = g_iblPrefilterSrvHandleGPU;
	auto& iblEnvironmentSrvHandleGPU = g_iblEnvironmentSrvHandleGPU;
	auto& iblBrdfLutSrvHandleGPU = g_iblBrdfLutSrvHandleGPU;
	auto& spriteMaterialResource = g_spriteMaterialResource;
	// spriteMaterialResource / sphereMaterialResource 邵�E�E�E�・�E�E�E� PixelShader 邵�E�E�E�・�E�E�E� Material CBV邵�E�E�E�繝ｻ
	auto& spriteMaterialData = g_spriteMaterialData;
	auto& sphereMaterialResource = g_sphereMaterialResource;
	auto& sphereMaterialData = g_sphereMaterialData;

	auto& directionalLightResource = g_directionalLightResource;
	auto& emissiveLightResource = g_emissiveLightResource;
	auto& emissiveLightData = g_emissiveLightData; // directionalLightResource 邵�E�E�E�・�E�E�E� PixelShader 邵�E�E�E�・�E�E�E�陝ｷ・�E�E�E�髯�E�E�E�謔溘�E雋�E・CBV邵�E�E�E�繝ｻ
	auto& directionalLightData = g_directionalLightData;
	// directionalLightData 邵�E�E�E�・�E�E�E�陟厄�E�E�E��E�E�E�髯�E�E�E�謔溘�E郢�E�E�E�蛛ｵ�E�E�E�帷�E�E�E��E�E�E�・�E�E�E�郢晏沺蟀�E�E�E�陷�E�E�E�莉｣竏郁惺蛹�E�E�E�・冗ｸ�E�E�E�蟶呻�E�E�E�狗ｸ�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�繧・�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& spriteTransformationMatrixData = g_spriteTransformationMatrixData;
	// spriteTransformationMatrixData 邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E� Sprite 郢晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�騾匁E�E��E��E�E�E� WVP / World 邵�E�E�E�・�E�E�E�隴厁E�E��E��E�E�E�邵�E�E�E�蟠趣�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�陷亥現�E�E�E�繝ｻ
	auto& sphereTransformationMatrixResource = g_sphereTransformationMatrixResource;
	// sphereTransformationMatrixResource / Data 邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E� 3D 郢晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�騾匁E�E��E��E�E�E� WVP / World邵�E�E�E�繝ｻ
	auto& sphereTransformationMatrixData = g_sphereTransformationMatrixData;

	auto& modelData = g_modelData;
	// modelData 邵�E�E�E�・�E�E�E� plane.obj 邵�E�E�E�・�E�E�E�鬯・�E�E�E�縺幁E�E��E��E�E�E�・�E�E�E�郢�E�E�E�繝ｻDrawInstanced 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E�笳・�E�E�E��E�E�E�竏壺・闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& primitiveVertexBufferViews = g_editorPrimitiveVertexBufferViews;
	// primitiveVertexBufferViews 邵�E�E�E�・�E�E�E�陜難�E�E�E��E�E�E�隴幢�E�E�E��E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�譁絶・邵�E�E�E�・�E�E�E� GPU 鬯・�E�E�E�縺・Buffer邵�E�E�E�繝ｻ
	auto& primitiveVertexCounts = g_editorPrimitiveVertexCounts;
	// primitiveVertexCounts 邵�E�E�E�・�E�E�E�陜難�E�E�E��E�E�E�隴幢�E�E�E��E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�譁絶・邵�E�E�E�・�E�E�E� DrawInstanced 鬯・�E�E�E�縺幁E�E��E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
	auto& spriteIndices = g_spriteIndices;
	// spriteIndices 邵�E�E�E�・�E�E�E� Sprite 邵�E�E�E�・�E�E�E� DrawIndexedInstanced 邵�E�E�E�・�E�E�E� index 隰�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& transform = g_transform;
	// transform / spriteTransform / cameraTransform / uvTransform 邵�E�E�E�・�E�E�E�闔�E�E�E�E��E�E�E�繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�髯�E�E�E�謔溘�E闖ｴ諛医・陷医・�E�E�E�繝ｻ
	auto& spriteTransform = g_spriteTransform;
	auto& cameraTransform = g_cameraTransform;
	auto& uvTransform = g_uvTransform;

	auto& modelVertexBufferView = g_modelVertexBufferView;
	// model / sprite BufferView 邵�E�E�E�・�E�E�E� IASetVertexBuffers 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�繝ｻGPU 鬯・�E�E�E�縺幁E�E��E��E�E�E�繝ｻ・�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
	auto& spriteVertexBufferView = g_spriteVertexBufferView;

	auto& spriteIndexBufferView = g_spriteIndexBufferView;
	// spriteIndexBufferView 邵�E�E�E�・�E�E�E� Sprite 陜怜ｹ・�E�E�E��E�E�E�雋橸�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� IndexBuffer邵�E�E�E�繝ｻ
	auto& viewport = g_viewport; // viewport / scissorRect 邵�E�E�E�・�E�E�E� SceneView 陷繝ｻ笁E�E��E�邵�E�E�E�莉｣竏郁�E�E�E��E�E�E�荳奁E�E��E��E�E�E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E驕擾�E�E�E��E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�繝ｻ
	auto& scissorRect = g_scissorRect;

	auto& cameraMatrix = g_cameraMatrix;
	// camera/view/projection 髯�E�E�E�謔溘�E邵�E�E�E�・�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�繝ｻSceneView 邵�E�E�E�・�E�E�E�隰壼供�E�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& viewMatrix = g_viewMatrix;
	auto& projectionMatrix = g_projectionMatrix;

	auto& spriteProjectionMatrix = g_spriteProjectionMatrix;
	// spriteProjectionMatrix 邵�E�E�E�・�E�E�E� Sprite 郢�E�E�E�繝ｻ2D 陟趣�E�E�E��E�E�E�隶灘生縲定ｬ�E�E�E�荳奁E�E��E��E�E�E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�雎�E�E�E�E��E�E�E�陝�E・・�E�E�E�・�E�E�E�髯�E�E�E�謔溘�E邵�E�E�E�繝ｻ
	auto& sceneClearColor = g_sceneClearColor; // sceneClearColor 邵�E�E�E�・�E�E�E� RenderTarget 郢�E�E�E�雋橸�E�E�E��E�E�E�蜉ｱ・顔ｸ�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�蜻�E�E�E�繝ｬ隴趣�E�E�E��E�E�E�豼�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
	auto& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();
	// editorSceneObjects 邵�E�E�E�・�E�E�E�鬩溷調・�E�E�E�・�E�E�E�雋ょ現竏ｩ GameObject 邵�E�E�E�・�E�E�E�陝�E�E�E�E��E�E�E�陟｢諛岩・郢�E�E�E�繝ｻDirectX 隰�E�E�E�蜀怜�E陝�E�E�E�E��E�E�E�髮趣�E�E�E��E�E�E�邵�E�E�E�繝ｻ
	auto& fence = g_fence;
	// fence / fenceValue / fenceEvent 邵�E�E�E�・�E�E�E� Present 陟募�E�E�E�娯・ GPU 陞ｳ蠕｡・�E�E�E�繝ｻ・定輔�E笁E�E��E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�陷�E�E�E�譴�E�E�E�謔�E叉ﾂ陟台�E�E�E�環繝ｻ
	auto& fenceValue = g_fenceValue;
	auto& fenceEvent = g_fenceEvent;

	auto& textureFilePaths = g_textureFilePaths; // textureFilePaths 邵�E�E�E�・�E�E�E� Texture 隰�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�闕ｳ莨∝応陋�E�E�E�・�E�E�E�陞ｳ螢�E�E�E�竊楢抁E�E�E�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& textureSrvHandlesGPU = g_textureSrvHandlesGPU;
	// textureSrvHandlesGPU 邵�E�E�E�・�E�E�E� Shader 邵�E�E�E�・�E�E�E� Texture SRV 郢�E�E�E�蜻茨�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻGPU 郢昜ｸ莞ｦ郢晏ｳ�E�E�E��E�E�E�晉�E�E��E�E�E�繝ｻ
	auto& isLegacyPreviewVisible = g_isLegacyPreviewVisible;
	// isLegacyPreviewVisible 邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E�郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E�晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�郢�E�E�E�蜻育�E�E�E�堤�E�E�E��E�E�E�荳環�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ�E�E�E��E�邵�E�E�E�・�E�E�E�郢晁E�E��E�釁E�E��E��E�E�E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻ

	// 陋ｻ譎�E�E�E�E�E�E��E��E�E�E�髢辯慕ｸ�E�E�E�竏ｫ・�E�E�E�繧・�E�E�E��E�E�E�繝ｻ・�E�E�E�蠕個・�E�E�E�Gui::Render 陷鷹亂繝ｻ郢晁E�E��E�釁E�E��E��E�晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� GPU 隰�E�E�E�蜀怜�E郢�E�E�E�螳夲�E�E�E��E�E�E�蠕鯉ｽ冗ｸ�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	if (!g_isInitialized || g_isFinalized || !g_isDrawRequested) {
		return;
	}

	if (!hasLoggedFirstRenderEnter) {
		Log(g_logStream, "EditorRenderManager first draw entered");
		hasLoggedFirstRenderEnter = true;
	}

	ApplySceneLightToDirectionalBuffer(*directionalLightData);
	// Inspector 邵�E�E�E�・�E�E�E�驍ｱ・�E�E�E�鬮�E�E�E�繝ｻ・�E�E�E�邵�E�E�E�繝ｻLight Component 郢�E�E�E�蜑�E�E�E�E��E�E�E�鄙ｫ繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�陞ｳ貁E�E��E�帷�E�E�E��E�E�E�・�E�E�E�郢晏現竏郁愾閧�E�E�E�荳十E�E��E��E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ

	emissiveLightData->count = 0;
	for (const EditorSceneObject& emissiveObject : editorSceneObjects) {
		if (emissiveObject.type != EditorSceneObjectType::Model || emissiveObject.materialData == nullptr) {
			continue;
		}
		float emissiveStrength = emissiveObject.materialData->emissionStrength;
		if (emissiveStrength <= 0.001f) {
			continue;
		}
		if (emissiveLightData->count >= kMaxEmissiveLights) {
			break;
		}
		int32_t emissiveIndex = emissiveLightData->count;
		emissiveLightData->lights[emissiveIndex].position = emissiveObject.transform.translate;
		emissiveLightData->lights[emissiveIndex].intensity = emissiveStrength * 8.0f;
		Vector4 emissiveColor = emissiveObject.materialData->color;
		emissiveLightData->lights[emissiveIndex].color = {emissiveColor.x, emissiveColor.y, emissiveColor.z};
		emissiveLightData->lights[emissiveIndex].range = (std::max)(emissiveStrength * 5.0f, 2.0f);
		emissiveLightData->count++;
	}

	cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
	// cameraMatrix 邵�E�E�E�・�E�E�E� SceneView 郢�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� Transform 邵�E�E�E�荵晢�E�E�E�芽抁E�E��E�奁E�E��E�狗ｹ晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晁歓�E�E�E�E�謔溘�E邵�E�E�E�繝ｻ
	viewMatrix = Inverse(cameraMatrix);
	// viewMatrix 邵�E�E�E�・�E�E�E� cameraMatrix 邵�E�E�E�・�E�E�E�鬨�E�E�E�繝ｻ・�E�E�E�謔溘�E邵�E�E�E�繝ｻD 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�蛛ｵ縺咲�E�E�E�晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�驕ｨ・�E�E�E�鬮�E�E�E�阮吮・驕假�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E��E�E�E�繝ｻ
	directionalLightData->cameraPosition = g_isSceneViewVisible
		                                       ? cameraTransform.translate
		                                       : g_gameCameraPosition;
	// PBR 邵�E�E�E�・�E�E�E�髫穂ｹ滂ｽ�E�E�E�螢�E�E�E�蟀�E�E�E�陷�E�E�E�莉｣�E�E�E�・把eneView 陷・�E�E�E��E�E�E�陷亥現�E�E�E�・嫗meView 陷雁E�E��E�E�E�蟲�E�E�E�隴弱�E�E�E�E�E��E��E� Camera Component 郢�E�E�E�蜑�E�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ

	// spriteWorldMatrix 邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E� Sprite 郢晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� Transform 郢�E�E�E�螳夲�E�E�E��E�E�E�謔溘�E陋ｹ謔ｶ・�E�E�E�邵�E�E�E�貁E�E��E��E�E�E�らｸ�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
	Matrix4x4 spriteWorldMatrix = MakeAffineMatrix(
		spriteTransform.scale,
		spriteTransform.rotate,
		spriteTransform.translate);

	Matrix4x4 spriteWorldViewProjectionMatrix = Multiply(spriteWorldMatrix, spriteProjectionMatrix);
	// spriteWorldViewProjectionMatrix 邵�E�E�E�・�E�E�E� Sprite 邵�E�E�E�・�E�E�E� World 邵�E�E�E�・�E�E�E� 2D 雎�E�E�E�E��E�E�E�陝�E・・�E�E�E�・�E�E�E�郢�E�E�E�雋樒ｲ玖ｬ瑚�E・�E�E�E�邵�E�E�E�繝ｻWVP邵�E�E�E�繝ｻ
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	// worldMatrix 邵�E�E�E�・�E�E�E�隴鯉ｽ�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E�晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� Transform 郢�E�E�E�螳夲�E�E�E��E�E�E�謔溘�E陋ｹ謔ｶ・�E�E�E�邵�E�E�E�貁E�E��E��E�E�E�らｸ�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
	Matrix4x4 sceneViewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
	Matrix4x4 inverseViewProjectionMatrix = Inverse(sceneViewProjectionMatrix);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, sceneViewProjectionMatrix);
	// worldViewProjectionMatrix 邵�E�E�E�・�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�繝ｻSceneView 邵�E�E�E�・�E�E�E�隰壼供�E�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・・WVP邵�E�E�E�繝ｻ
	Matrix4x4 gameViewProjectionMatrix = Multiply(g_gameViewMatrix, g_gameProjectionMatrix);
	Matrix4x4 lightViewProjectionMatrix = MakeLightViewProjectionMatrix(
		editorSceneObjects,
		transform,
		directionalLightData,
		isLegacyPreviewVisible);
	// lightViewProjectionMatrix 邵�E�E�E�・�E�E�E�陟厄�E�E�E��E�E�E�騾匁E�E��E��E�E�E� DepthTexture 邵�E�E�E�・�E�E�E�隰�E�E�E�荳奁E�E��E��E�E�E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晞メ・�E�E�E�荵溘○髯�E�E�E�謔溘�E邵�E�E�E�繝ｻ
	Matrix4x4 uvTransformMatrix = MakeAffineMatrix(uvTransform.scale, uvTransform.rotate, uvTransform.translate);
	// uvTransformMatrix 邵�E�E�E�・�E�E�E� Material 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�繝ｻUV 陞溽判驪�E�E�E�髯�E�E�E�謔溘�E邵�E�E�E�繝ｻ
	spriteTransformationMatrixData->WVP = spriteWorldViewProjectionMatrix;
	// 隴鯉ｽ�E�E�E�郢晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�騾匁E�E��E��E�E�E�邵�E�E�E�・�E�E�E�陞ｳ螢�E�E�E�辟夂ｹ晁E��E繝｣郢晁E�E��E�斐＜邵�E�E�E�・�E�E�E�闔�E�E�E�E��E�E�E�繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�髯�E�E�E�謔溘�E郢�E�E�E�蜻亥�E�E�E�檎ｸ�E�E�E�蟠趣�E�E�E��E�E�E�・�E�E�E�郢�E�E�E��E�E�E�邵�E�E�E�繝ｻ
	spriteTransformationMatrixData->World = spriteWorldMatrix;
	spriteTransformationMatrixData->lightWVP = Multiply(spriteWorldMatrix, lightViewProjectionMatrix);
	sphereTransformationMatrixData->WVP = worldViewProjectionMatrix;
	sphereTransformationMatrixData->World = worldMatrix;
	sphereTransformationMatrixData->lightWVP = Multiply(worldMatrix, lightViewProjectionMatrix);

	spriteMaterialData->uvTransform = uvTransformMatrix;
	// Sprite 邵�E�E�E�・�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�・�E�E�E� Material 邵�E�E�E�・�E�E�E�陷�E�E�E�蠕個ｧ UV 陞溽判驪�E�E�E�郢�E�E�E�雋樊ｸ夊ｭ擾�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	sphereMaterialData->uvTransform = uvTransformMatrix;

	auto updateSceneObjectMatrices = [&](const Matrix4x4& targetSceneViewProjectionMatrix) {
		for (EditorSceneObject& sceneObject : editorSceneObjects) {
			Matrix4x4 sceneObjectWorldMatrix = MakeAffineMatrix(
				sceneObject.transform.scale,
				sceneObject.transform.rotate,
				sceneObject.transform.translate);

			Matrix4x4 sceneObjectProjectionMatrix =
				sceneObject.type == EditorSceneObjectType::Sprite
					? spriteProjectionMatrix
					: targetSceneViewProjectionMatrix;

			if (sceneObject.transformationData != nullptr) {
				sceneObject.transformationData->WVP = Multiply(sceneObjectWorldMatrix, sceneObjectProjectionMatrix);
				sceneObject.transformationData->World = sceneObjectWorldMatrix;
				sceneObject.transformationData->lightWVP = Multiply(sceneObjectWorldMatrix, lightViewProjectionMatrix);
			}

			Matrix4x4 gameObjectProjectionMatrix =
				sceneObject.type == EditorSceneObjectType::Sprite
					? spriteProjectionMatrix
					: gameViewProjectionMatrix;

			if (sceneObject.gameTransformationData != nullptr) {
				sceneObject.gameTransformationData->WVP = Multiply(sceneObjectWorldMatrix, gameObjectProjectionMatrix);
				sceneObject.gameTransformationData->World = sceneObjectWorldMatrix;
				sceneObject.gameTransformationData->lightWVP = Multiply(sceneObjectWorldMatrix, lightViewProjectionMatrix);
			}

			if (sceneObject.materialData != nullptr) {
				sceneObject.materialData->uvTransform = uvTransformMatrix;
			}
		}
	};

	updateSceneObjectMatrices(sceneViewProjectionMatrix);

	hr = commandAllocator->Reset();
	// CommandAllocator / CommandList 郢�E�E�E�蜑�E�E�E�E��E�E�E�鄙ｫ繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E髫�E�E�E�蛟ｬ鮖ｸ騾匁E�E��E��E�E�E�邵�E�E�E�・�E�E�E� Reset 邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get());
	assert(SUCCEEDED(hr));

	D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvHandleGPU =
		g_environmentTextureSrvHandleGPU.ptr != 0u
			? g_environmentTextureSrvHandleGPU
			: textureSrvHandlesGPU[2];
	auto ensureEnvironmentTexture = [&]() {
		if (!g_isEnvironmentTextureDirty &&
			g_environmentTextureAssetPath == g_loadedEnvironmentTextureAssetPath) {
			return;
		}

		if (g_environmentTextureUploadResource != nullptr) {
			g_environmentTextureUploadResource->Release();
			g_environmentTextureUploadResource = nullptr;
		}
		if (g_environmentTextureResource != nullptr) {
			g_environmentTextureResource->Release();
			g_environmentTextureResource = nullptr;
		}

		g_loadedEnvironmentTextureAssetPath.clear();
		g_isEnvironmentTextureDirty = false;
		if (g_environmentTextureAssetPath.empty()) {
			return;
		}

		if (!std::filesystem::exists(g_environmentTextureAssetPath)) {
			return;
		}

		DirectX::ScratchImage environmentMipImages = LoadTexture(ConvertString(g_environmentTextureAssetPath));
		const DirectX::TexMetadata& environmentMetadata = environmentMipImages.GetMetadata();
		g_environmentTextureResource = CreateTextureResource(device.Get(), environmentMetadata);
		g_environmentTextureUploadResource = UploadTextureData(
			device.Get(),
			commandList.Get(),
			g_environmentTextureResource,
			environmentMipImages);

		D3D12_SHADER_RESOURCE_VIEW_DESC environmentSrvDesc{};
		environmentSrvDesc.Format = environmentMetadata.format;
		environmentSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		environmentSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		environmentSrvDesc.Texture2D.MipLevels = static_cast<UINT>(environmentMetadata.mipLevels);
		device->CreateShaderResourceView(
			g_environmentTextureResource,
			&environmentSrvDesc,
			g_environmentTextureSrvHandleCPU);

		g_loadedEnvironmentTextureAssetPath = g_environmentTextureAssetPath;
		environmentSrvHandleGPU = g_environmentTextureSrvHandleGPU;
	};
	ensureEnvironmentTexture();
	if (g_environmentTextureResource != nullptr) {
		environmentSrvHandleGPU = g_environmentTextureSrvHandleGPU;
	}
	const bool hasEnvironmentTexture =
		g_environmentTextureResource != nullptr &&
		!g_loadedEnvironmentTextureAssetPath.empty();
	const float userRequestedEnvironmentTexture =
		directionalLightData->environmentTextureEnabled >= 0.5f ? 1.0f : 0.0f;
	const float environmentTextureEnabledForFrame =
		hasEnvironmentTexture ? userRequestedEnvironmentTexture : 0.0f;
	directionalLightData->environmentTextureEnabled = environmentTextureEnabledForFrame;

	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	// backBufferIndex 邵�E�E�E�・�E�E�E�闔蛾宦螻楢�E�E�E��E�E�E�蜀怜�E邵�E�E�E�蜷�E�E�E�・・SwapChain buffer 邵�E�E�E�・�E�E�E�騾�E�E�E�・�E�E�E�陷�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ

	auto drawShadowObjects = [&]() {
		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type != EditorSceneObjectType::Model ||
				sceneObject.transformationResource == nullptr) {
				continue;
			}

			size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);
			// meshTypeIndex 邵�E�E�E�・�E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隰�E�E�E�荳奁E�E��E��E�E�E�陜難�E�E�E��E�E�E�隴幢�E�E�E��E�E�E�陟厄�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� VertexBuffer 騾�E�E�E�・�E�E�E�陷�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ
			if (meshTypeIndex >= kEditorModelMeshTypeCount ||
				primitiveVertexCounts[meshTypeIndex] == 0u) {
				meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
			}

			commandList->SetGraphicsRootConstantBufferView(
				1,
				sceneObject.transformationResource->GetGPUVirtualAddress());

			if (sceneObject.usesCustomMesh &&
				sceneObject.customMeshVertexResource != nullptr &&
				sceneObject.customMeshVertexCount > 0u) {
				commandList->IASetVertexBuffers(0, 1, &sceneObject.customMeshVertexBufferView);
				commandList->DrawInstanced(sceneObject.customMeshVertexCount, 1, 0, 0);
			}
			else {
				commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
				commandList->DrawInstanced(primitiveVertexCounts[meshTypeIndex], 1, 0, 0);
			}
		}

		if (isLegacyPreviewVisible) {
			commandList->SetGraphicsRootConstantBufferView(
				1,
				sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
		}
	};

	if (shadowMapResource != nullptr && shadowPipelineState != nullptr) {
		// 陟厄�E�E�E��E�E�E�郢昜ｻ�E�E�E�縺帷�E�E�E��E�E�E�・�E�E�E� color 郢�E�E�E�蜻亥�E�E�E�檎ｸ�E�E�E�荵昶・邵�E�E�E�竏墁E�E��E��E�E�E��E�E�E�・�E�E�E�郢晞メ・�E�E�E�荵溘○邵�E�E�E�・�E�E�E� depth 邵�E�E�E�・�E�E�E�邵�E�E�E�莉｣・・shadowMapResource 邵�E�E�E�・�E�E�E�髫�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
		D3D12_RESOURCE_BARRIER shadowBarrier{};
		shadowBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		shadowBarrier.Transition.pResource = shadowMapResource;
		shadowBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		commandList->ResourceBarrier(1, &shadowBarrier);

		D3D12_VIEWPORT shadowViewport{};
		shadowViewport.Width = static_cast<float>(kRuntimeShadowMapSize);
		shadowViewport.Height = static_cast<float>(kRuntimeShadowMapSize);
		shadowViewport.MinDepth = 0.0f;
		shadowViewport.MaxDepth = 1.0f;

		D3D12_RECT shadowScissorRect{};
		shadowScissorRect.right = static_cast<LONG>(kRuntimeShadowMapSize);
		shadowScissorRect.bottom = static_cast<LONG>(kRuntimeShadowMapSize);

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(shadowPipelineState.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->RSSetViewports(1, &shadowViewport);
		commandList->RSSetScissorRects(1, &shadowScissorRect);
		commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsvHandle);
		commandList->ClearDepthStencilView(shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		drawShadowObjects();

		shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &shadowBarrier);
	}

	//================================================================
	// Scene rendering to HDR RT
	//================================================================

	// HDR RT 郢�E�E�E�繝ｻPIXEL_SHADER_RESOURCE 遶翫・RENDER_TARGET 邵�E�E�E�・�E�E�E�鬩匁E�E��E��E�E�E�驕假�E�E�E��E�E�E� (陷鷹亂繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� post-process 陟募�E�E�E�後�E PS 霑･・�E�E�E�隲�E�E�E�繝ｻ
	D3D12_RESOURCE_BARRIER hdrBarrier{};
	hdrBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	hdrBarrier.Transition.pResource = hdrRenderTarget;
	hdrBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	hdrBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	hdrBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &hdrBarrier);

	commandList->SetGraphicsRootSignature(rootSignature.Get());
	commandList->SetPipelineState(graphicsPipelineState.Get());
	commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
	ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
	commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->OMSetRenderTargets(1, &hdrRtvHandle, FALSE, &dsvHandle);
	float hdrClearColor[4] = {
		sceneClearColor[0],
		sceneClearColor[1],
		sceneClearColor[2],
		sceneClearColor[3]
	}; // HDR RenderTexture 邵�E�E�E�・�E�E�E�髢�E�E�E�譴�E�E�E�蜍ｹ郢�E�E�E�繝ｻInspector 邵�E�E�E�・�E�E�E�髢�E�E�E�譴�E�E�E�蜍ｹ豼�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�闕ｳ�E�E�E�髢�E�E�E�・�E�E�E�邵�E�E�E�霈披雷郢�E�E�E�荵敖繝ｻ
	commandList->ClearRenderTargetView(hdrRtvHandle, hdrClearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	D3D12_RESOURCE_BARRIER materialMaskBarrier{};
	if (materialMaskRenderTarget != nullptr) {
		materialMaskBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		materialMaskBarrier.Transition.pResource = materialMaskRenderTarget;
		materialMaskBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		materialMaskBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		materialMaskBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &materialMaskBarrier);

		float materialMaskClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		commandList->ClearRenderTargetView(materialMaskRtvHandle, materialMaskClearColor, 0, nullptr);
	}

	auto drawSkybox = [&](const D3D12_VIEWPORT& targetViewport, const D3D12_RECT& targetScissorRect, const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtvHandle) {
		if (skyboxPipelineState == nullptr) {
			return;
		}

		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(skyboxPipelineState.Get());
		commandList->RSSetViewports(1, &targetViewport);
		commandList->RSSetScissorRects(1, &targetScissorRect);
		commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, environmentSrvHandleGPU);
		float skyboxParams[16] = {
			directionalLightData->skyUpperColor.x,
			directionalLightData->skyUpperColor.y,
			directionalLightData->skyUpperColor.z,
			(std::max)(directionalLightData->skyIntensity, 0.0f),
			directionalLightData->skyLowerColor.x,
			directionalLightData->skyLowerColor.y,
			directionalLightData->skyLowerColor.z,
			(std::max)(directionalLightData->horizonSharpness, 0.0001f),
			directionalLightData->direction.x,
			directionalLightData->direction.y,
			directionalLightData->direction.z,
			(std::max)(directionalLightData->skyEmission, 0.0f),
			environmentTextureEnabledForFrame,
			directionalLightData->environmentTextureIntensity,
			directionalLightData->environmentTextureRotation,
			directionalLightData->environmentTextureMipBias
		};
		commandList->SetGraphicsRoot32BitConstants(2, 16, skyboxParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	};

	ID3D12PipelineState* defaultDrawPso = graphicsPipelineState.Get();
	auto drawSceneObjects = [&](bool isGameViewPass, const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtvHandle, int32_t skipGameObjectId) {
		commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);

		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (skipGameObjectId >= 0 && sceneObject.gameObjectId == skipGameObjectId) {
				continue;
			}

			ID3D12Resource* transformationResource = isGameViewPass
				                                         ? sceneObject.gameTransformationResource
				                                         : sceneObject.transformationResource;
			if (transformationResource == nullptr) {
				continue;
			}

			ID3D12Resource* materialResource = sceneObject.materialResource;
			if (materialResource == nullptr) {
				materialResource = sceneObject.type == EditorSceneObjectType::Sprite
					                   ? spriteMaterialResource
					                   : sphereMaterialResource;
			}

			if (sceneObject.type == EditorSceneObjectType::Sprite) {
				int32_t textureIndex =
					(std::clamp)(sceneObject.textureIndex, 0, static_cast<int32_t>(_countof(textureFilePaths)) - 1);
				D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
					sceneObject.customTextureSrvGpuHandle.ptr != 0u
						? sceneObject.customTextureSrvGpuHandle
						: textureSrvHandlesGPU[textureIndex];
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(3, textureHandle);
				commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(7, iblIrradianceSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(8, iblPrefilterSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(9, iblEnvironmentSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(10, iblBrdfLutSrvHandleGPU);
				commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);
				commandList->IASetIndexBuffer(&spriteIndexBufferView);
				commandList->DrawIndexedInstanced(_countof(spriteIndices), 1, 0, 0, 0);
			}
			else {
				size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);
				if (meshTypeIndex >= kEditorModelMeshTypeCount ||
					primitiveVertexCounts[meshTypeIndex] == 0u) {
					meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
				}

				if (sceneObject.cullMode == 1 && g_cullFrontPipelineState != nullptr) {
					commandList->SetPipelineState(g_cullFrontPipelineState.Get());
				}
				else {
					commandList->SetPipelineState(defaultDrawPso);
				}

				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationResource->GetGPUVirtualAddress());
				D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
					sceneObject.customTextureSrvGpuHandle.ptr != 0u
						? sceneObject.customTextureSrvGpuHandle
						: textureSrvHandlesGPU[2];
				commandList->SetGraphicsRootDescriptorTable(3, textureHandle);
				commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(7, iblIrradianceSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(8, iblPrefilterSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(9, iblEnvironmentSrvHandleGPU);
				commandList->SetGraphicsRootDescriptorTable(10, iblBrdfLutSrvHandleGPU);
				if (sceneObject.usesCustomMesh &&
					sceneObject.customMeshVertexResource != nullptr &&
					sceneObject.customMeshVertexCount > 0u) {
					commandList->IASetVertexBuffers(0, 1, &sceneObject.customMeshVertexBufferView);
					commandList->DrawInstanced(sceneObject.customMeshVertexCount, 1, 0, 0);
				}
				else {
					commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
					commandList->DrawInstanced(primitiveVertexCounts[meshTypeIndex], 1, 0, 0);
				}
			}
		}
	};

	auto drawReflectionMaskObjects = [&](bool isGameViewPass, int32_t planarMaskGameObjectId) {
		if (materialMaskRenderTarget == nullptr || objectReflectionMaskPipelineState == nullptr) {
			return;
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(objectReflectionMaskPipelineState.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->OMSetRenderTargets(1, &materialMaskRtvHandle, FALSE, &dsvHandle);

		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type == EditorSceneObjectType::Sprite) {
				continue;
			}

			if (planarMaskGameObjectId >= 0 &&
				sceneObject.gameObjectId != planarMaskGameObjectId) {
				continue;
			}

			ID3D12Resource* transformationResource = isGameViewPass
				                                         ? sceneObject.gameTransformationResource
				                                         : sceneObject.transformationResource;
			if (transformationResource == nullptr) {
				continue;
			}

			ID3D12Resource* materialResource = sceneObject.materialResource;
			if (materialResource == nullptr) {
				materialResource = sphereMaterialResource;
			}

			size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);
			if (meshTypeIndex >= kEditorModelMeshTypeCount ||
				primitiveVertexCounts[meshTypeIndex] == 0u) {
				meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
			}

			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, transformationResource->GetGPUVirtualAddress());

			if (sceneObject.usesCustomMesh &&
				sceneObject.customMeshVertexResource != nullptr &&
				sceneObject.customMeshVertexCount > 0u) {
				commandList->IASetVertexBuffers(0, 1, &sceneObject.customMeshVertexBufferView);
				commandList->DrawInstanced(sceneObject.customMeshVertexCount, 1, 0, 0);
			}
			else {
				commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
				commandList->DrawInstanced(primitiveVertexCounts[meshTypeIndex], 1, 0, 0);
			}
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->OMSetRenderTargets(1, &hdrRtvHandle, FALSE, &dsvHandle);
	};

	bool hasPlanarReflectionPass = false;
	Matrix4x4 planarReflectionViewProjectionMatrix = MakeIdentity4x4();
	Vector3 planarReflectionPlaneNormal = {0.0f, 1.0f, 0.0f};
	Vector3 planarReflectionPlanePoint = {0.0f, 0.0f, 0.0f};
	Vector3 planarReflectionPlaneTangent = {1.0f, 0.0f, 0.0f};
	Vector3 planarReflectionPlaneBitangent = {0.0f, 0.0f, 1.0f};
	float planarReflectionHalfExtentX = 1.0f;
	float planarReflectionHalfExtentZ = 1.0f;
	float planarReflectionIntensity = 0.0f;
	float planarReflectionFadeDistance = 0.25f;
	int32_t planarReflectionSourceGameObjectId = -1;
	Vector3 planarReflectionCameraPosition = directionalLightData->cameraPosition;

	for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* reflectionProbeComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ReflectionProbe);
		if (reflectionProbeComponent == nullptr ||
			!reflectionProbeComponent->isActive ||
			reflectionProbeComponent->assetPath != "Planar") {
			continue;
		}

		const EditorSceneObject* reflectionSceneObject = nullptr;
		for (const EditorSceneObject& candidateSceneObject : editorSceneObjects) {
			if (candidateSceneObject.gameObjectId == gameObject.id) {
				reflectionSceneObject = &candidateSceneObject;
				break;
			}
		}

		if (reflectionSceneObject == nullptr) {
			continue;
		}

		//============================================================
		// 反射面のワールド座標と向きを決定
		//============================================================

		const PlanarReflectionSurface reflectionSurface = BuildPlanarReflectionSurface(
			*reflectionSceneObject,
			gameObject,
			*reflectionProbeComponent,
			cameraTransform.translate);
		planarReflectionPlaneNormal = reflectionSurface.normal;
		planarReflectionPlanePoint = reflectionSurface.point;
		planarReflectionPlaneTangent = reflectionSurface.tangent;
		planarReflectionPlaneBitangent = reflectionSurface.bitangent;
		planarReflectionHalfExtentX = reflectionSurface.halfExtentTangent;
		planarReflectionHalfExtentZ = reflectionSurface.halfExtentBitangent;
		planarReflectionIntensity = (std::max)(reflectionProbeComponent->intensity, 0.0f);
		planarReflectionFadeDistance = (std::max)(
			(std::min)(planarReflectionHalfExtentX, planarReflectionHalfExtentZ) * 0.02f,
			0.01f);
		planarReflectionSourceGameObjectId = gameObject.id;

		const Matrix4x4 planarReflectionMatrix = MakePlanarReflectionMatrix(
			planarReflectionPlanePoint,
			planarReflectionPlaneNormal);
		planarReflectionCameraPosition = Transform(cameraTransform.translate, planarReflectionMatrix);
		planarReflectionViewProjectionMatrix = Multiply(planarReflectionMatrix, sceneViewProjectionMatrix);
		hasPlanarReflectionPass = planarReflectionRenderTarget != nullptr &&
			planarScenePipelineState != nullptr &&
			planarReflectionPipelineState != nullptr;
		break;
	}

	if (hasPlanarReflectionPass) {
		const Vector3 savedCameraPosition = directionalLightData->cameraPosition;
		directionalLightData->cameraPosition = planarReflectionCameraPosition;

		D3D12_VIEWPORT planarReflectionViewport{};
		planarReflectionViewport.Width = static_cast<float>(g_renderWidth);
		planarReflectionViewport.Height = static_cast<float>(g_renderHeight);
		planarReflectionViewport.MinDepth = 0.0f;
		planarReflectionViewport.MaxDepth = 1.0f;

		D3D12_RECT planarReflectionScissorRect{};
		planarReflectionScissorRect.right = static_cast<LONG>(g_renderWidth);
		planarReflectionScissorRect.bottom = static_cast<LONG>(g_renderHeight);

		D3D12_RESOURCE_BARRIER planarReflectionBarrier{};
		planarReflectionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		planarReflectionBarrier.Transition.pResource = planarReflectionRenderTarget;
		planarReflectionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		planarReflectionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		planarReflectionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &planarReflectionBarrier);

		if (depthStencilResource != nullptr) {
			D3D12_RESOURCE_BARRIER reflectionDepthBarrier{};
			reflectionDepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			reflectionDepthBarrier.Transition.pResource = depthStencilResource;
			reflectionDepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			reflectionDepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			reflectionDepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			commandList->ResourceBarrier(1, &reflectionDepthBarrier);
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(planarScenePipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
	commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
	commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->RSSetViewports(1, &planarReflectionViewport);
		commandList->RSSetScissorRects(1, &planarReflectionScissorRect);
		commandList->OMSetRenderTargets(1, &planarReflectionRtvHandle, FALSE, &dsvHandle);
		commandList->ClearRenderTargetView(planarReflectionRtvHandle, hdrClearColor, 0, nullptr);
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		drawSkybox(planarReflectionViewport, planarReflectionScissorRect, planarReflectionRtvHandle);
		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(planarScenePipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		updateSceneObjectMatrices(planarReflectionViewProjectionMatrix);
		defaultDrawPso = planarScenePipelineState.Get();
		drawSceneObjects(false, planarReflectionRtvHandle, planarReflectionSourceGameObjectId);
		defaultDrawPso = graphicsPipelineState.Get();
		updateSceneObjectMatrices(sceneViewProjectionMatrix);

		directionalLightData->cameraPosition = savedCameraPosition;

		planarReflectionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		planarReflectionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &planarReflectionBarrier);

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	if (g_isSceneViewVisible || g_isGameViewVisible) {
		if (depthStencilResource != nullptr && !hasPlanarReflectionPass) {
			D3D12_RESOURCE_BARRIER mainDepthBarrier{};
			mainDepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			mainDepthBarrier.Transition.pResource = depthStencilResource;
			mainDepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			mainDepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			mainDepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			commandList->ResourceBarrier(1, &mainDepthBarrier);
		}
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	if (g_isSceneViewVisible) {
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);
		drawSkybox(viewport, scissorRect, hdrRtvHandle);

		if (isLegacyPreviewVisible) {
			commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1,
				sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
		}

		drawSceneObjects(false, hdrRtvHandle, -1);
		drawReflectionMaskObjects(false, planarReflectionSourceGameObjectId);
	}

	if (g_isGameViewVisible) {
		D3D12_VIEWPORT gameViewport{};
		gameViewport.TopLeftX = g_editorGameX;
		gameViewport.TopLeftY = g_editorGameY;
		gameViewport.Width = g_editorGameWidth;
		gameViewport.Height = g_editorGameHeight;
		gameViewport.MinDepth = 0.0f;
		gameViewport.MaxDepth = 1.0f;

		D3D12_RECT gameScissorRect{};
		gameScissorRect.left = static_cast<LONG>(g_editorGameX);
		gameScissorRect.top = static_cast<LONG>(g_editorGameY);
		gameScissorRect.right = static_cast<LONG>(g_editorGameX + g_editorGameWidth);
		gameScissorRect.bottom = static_cast<LONG>(g_editorGameY + g_editorGameHeight);

		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		commandList->RSSetViewports(1, &gameViewport);
		commandList->RSSetScissorRects(1, &gameScissorRect);
		drawSkybox(gameViewport, gameScissorRect, hdrRtvHandle);
		drawSceneObjects(true, hdrRtvHandle, -1);
		drawReflectionMaskObjects(true, planarReflectionSourceGameObjectId);
	}

	if (materialMaskRenderTarget != nullptr) {
		materialMaskBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		materialMaskBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &materialMaskBarrier);
	}

	// HDR RT 郢�E�E�E�繝ｻRENDER_TARGET 遶翫・PIXEL_SHADER_RESOURCE 邵�E�E�E�・�E�E�E�鬩匁E�E��E��E�E�E�驕假�E�E�E��E�E�E� (ToneMapping 邵�E�E�E�・�E�E�E�髫�E�E�E�・�E�E�E�郢�E�E�E�竏夲�E�E�E�狗ｹ�E�E�E�蛹�E�E�E�竕ｧ邵�E�E�E�・�E�E�E�)
	hdrBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	hdrBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1, &hdrBarrier);

	// Depth 郢�E�E�E�繝ｻDEPTH_WRITE 遶翫・PIXEL_SHADER_RESOURCE 邵�E�E�E�・�E�E�E�鬩匁E�E��E��E�E�E�驕假�E�E�E��E�E�E� (SSR 邵�E�E�E�・�E�E�E� SSAO 邵�E�E�E�迹夲�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�竏夲�E�E�E�狗ｹ�E�E�E�蛹�E�E�E�竕ｧ邵�E�E�E�・�E�E�E�)
	if (depthStencilResource != nullptr &&
		(hasPlanarReflectionPass || g_isSceneViewVisible || g_isGameViewVisible)) {
		D3D12_RESOURCE_BARRIER depthBarrier{};
		depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		depthBarrier.Transition.pResource = depthStencilResource;
		depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &depthBarrier);
	}

	//================================================================
	// Post-process: Bloom + ToneMapping
	//================================================================

	uint32_t bloomWidth = (std::max)(1u, g_renderWidth / 4);
	uint32_t bloomHeight = (std::max)(1u, g_renderHeight / 4);

	D3D12_VIEWPORT bloomViewport{};
	bloomViewport.Width = static_cast<float>(bloomWidth);
	bloomViewport.Height = static_cast<float>(bloomHeight);
	bloomViewport.MaxDepth = 1.0f;
	D3D12_RECT bloomScissor{};
	bloomScissor.right = static_cast<LONG>(bloomWidth);
	bloomScissor.bottom = static_cast<LONG>(bloomHeight);

	D3D12_VIEWPORT fullViewport{};
	fullViewport.Width = static_cast<float>(g_renderWidth);
	fullViewport.Height = static_cast<float>(g_renderHeight);
	fullViewport.MaxDepth = 1.0f;
	D3D12_RECT fullScissor{};
	fullScissor.right = static_cast<LONG>(g_renderWidth);
	fullScissor.bottom = static_cast<LONG>(g_renderHeight);

	float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	D3D12_GPU_DESCRIPTOR_HANDLE hdrPostSourceSrvHandle = hdrSrvHandleGPU;

	//================================================================
	// SSR (Screen Space Reflection)

	commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// SSAO: Scene Depth 遶翫・SSAO A 遶翫・SSAO B
	if (depthStencilResource != nullptr &&
		ssaoRenderTargets[0] != nullptr &&
		ssaoRenderTargets[1] != nullptr &&
		ssaoPipelineState != nullptr &&
		ssaoBlurPipelineState != nullptr) {
		D3D12_RESOURCE_BARRIER ssaoBarrier{};
		ssaoBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		ssaoBarrier.Transition.pResource = ssaoRenderTargets[0];
		ssaoBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		ssaoBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		ssaoBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &ssaoBarrier);

		commandList->SetPipelineState(ssaoPipelineState.Get());
		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &ssaoRtvHandles[0], FALSE, nullptr);
		float ssaoClearColor[4] = {1.0f, 0.0f, 0.0f, 0.0f};
		commandList->ClearRenderTargetView(ssaoRtvHandles[0], ssaoClearColor, 0, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, depthSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, shadowMapSrvGpuHandle);
		float ssaoParams[8] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			6.0f,
			1.20f,
			0.0f,
			0.0f,
			0.0f,
			0.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 8, ssaoParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		ssaoBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		ssaoBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &ssaoBarrier);

		ssaoBarrier.Transition.pResource = ssaoRenderTargets[1];
		ssaoBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		ssaoBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &ssaoBarrier);

		commandList->SetPipelineState(ssaoBlurPipelineState.Get());
		commandList->OMSetRenderTargets(1, &ssaoRtvHandles[1], FALSE, nullptr);
		commandList->ClearRenderTargetView(ssaoRtvHandles[1], ssaoClearColor, 0, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, ssaoSrvHandlesGPU[0]);
		commandList->SetGraphicsRootDescriptorTable(1, depthSrvHandleGPU);
		float ssaoBlurParams[4] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			0.0f,
			0.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 4, ssaoBlurParams, 0);
		commandList->DrawInstanced(3, 1, 0, 0);

		ssaoBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		ssaoBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &ssaoBarrier);
	}

	// Planar Reflection
	if (hasPlanarReflectionPass && hdrCompositeRenderTarget != nullptr) {
		D3D12_RESOURCE_BARRIER compositeBarrier{};
		compositeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		compositeBarrier.Transition.pResource = hdrCompositeRenderTarget;
		compositeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		compositeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		compositeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &compositeBarrier);

		commandList->SetPipelineState(planarReflectionPipelineState.Get());
		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &hdrCompositeRtvHandle, FALSE, nullptr);
		commandList->ClearRenderTargetView(hdrCompositeRtvHandle, clearColor, 0, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, hdrSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, depthSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(3, materialMaskSrvHandleGPU);
		float reflectionParams[48] = {};
		std::memcpy(&reflectionParams[0], &inverseViewProjectionMatrix.matrix[0][0], sizeof(float) * 16u);
		std::memcpy(&reflectionParams[16], &planarReflectionViewProjectionMatrix.matrix[0][0], sizeof(float) * 16u);
		reflectionParams[32] = planarReflectionPlaneNormal.x;
		reflectionParams[33] = planarReflectionPlaneNormal.y;
		reflectionParams[34] = planarReflectionPlaneNormal.z;
		reflectionParams[35] = planarReflectionFadeDistance;
		reflectionParams[36] = planarReflectionPlanePoint.x;
		reflectionParams[37] = planarReflectionPlanePoint.y;
		reflectionParams[38] = planarReflectionPlanePoint.z;
		reflectionParams[39] = planarReflectionIntensity;
		reflectionParams[40] = planarReflectionPlaneTangent.x;
		reflectionParams[41] = planarReflectionPlaneTangent.y;
		reflectionParams[42] = planarReflectionPlaneTangent.z;
		reflectionParams[43] = planarReflectionHalfExtentX;
		reflectionParams[44] = planarReflectionPlaneBitangent.x;
		reflectionParams[45] = planarReflectionPlaneBitangent.y;
		reflectionParams[46] = planarReflectionPlaneBitangent.z;
		reflectionParams[47] = planarReflectionHalfExtentZ;
		commandList->SetGraphicsRoot32BitConstants(2, 48, reflectionParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		compositeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		compositeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &compositeBarrier);
		hdrPostSourceSrvHandle = hdrCompositeSrvHandleGPU;
	}

	// Bloom extract: HDR RT 遶翫・BloomA
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = bloomRenderTargets[0];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &barrier);

		commandList->SetPipelineState(bloomExtractPipelineState.Get());
		commandList->RSSetViewports(1, &bloomViewport);
		commandList->RSSetScissorRects(1, &bloomScissor);
		commandList->OMSetRenderTargets(1, &bloomRtvHandles[0], FALSE, nullptr);
		commandList->ClearRenderTargetView(bloomRtvHandles[0], clearColor, 0, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, shadowMapSrvGpuHandle);
		float extractParams[4] = {2.0f, 0.5f, 1.0f, 0.0f};
		commandList->SetGraphicsRoot32BitConstants(2, 4, extractParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Bloom blur horizontal: BloomA 遶翫・BloomB
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = bloomRenderTargets[1];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &barrier);

		commandList->SetPipelineState(bloomBlurPipelineState.Get());
		commandList->RSSetViewports(1, &bloomViewport);
		commandList->RSSetScissorRects(1, &bloomScissor);
		commandList->OMSetRenderTargets(1, &bloomRtvHandles[1], FALSE, nullptr);
		commandList->ClearRenderTargetView(bloomRtvHandles[1], clearColor, 0, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, bloomSrvHandlesGPU[0]);
		commandList->SetGraphicsRootDescriptorTable(1, shadowMapSrvGpuHandle);
		float blurHParams[4] = {1.0f / static_cast<float>(bloomWidth), 0.0f, 0.0f, 0.0f};
		commandList->SetGraphicsRoot32BitConstants(2, 4, blurHParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Bloom blur vertical: BloomB 遶翫・BloomA
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = bloomRenderTargets[0];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &barrier);

		commandList->SetPipelineState(bloomBlurPipelineState.Get());
		commandList->RSSetViewports(1, &bloomViewport);
		commandList->RSSetScissorRects(1, &bloomScissor);
		commandList->OMSetRenderTargets(1, &bloomRtvHandles[0], FALSE, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, bloomSrvHandlesGPU[1]);
		commandList->SetGraphicsRootDescriptorTable(1, shadowMapSrvGpuHandle);
		float blurVParams[4] = {0.0f, 1.0f / static_cast<float>(bloomHeight), 1.0f, 0.0f};
		commandList->SetGraphicsRoot32BitConstants(2, 4, blurVParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Final tone mapping + bloom composite: HDR RT + BloomA 遶翫・LDR RT
	{
		D3D12_RESOURCE_BARRIER postProcessBarrier{};
		postProcessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		postProcessBarrier.Transition.pResource = postProcessRenderTarget;
		postProcessBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		postProcessBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		postProcessBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &postProcessBarrier);

		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &postProcessRtvHandle, FALSE, nullptr);
		float postProcessClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		commandList->ClearRenderTargetView(postProcessRtvHandle, postProcessClearColor, 0, nullptr);
		ID3D12DescriptorHeap* heaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(toneMappingPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, bloomSrvHandlesGPU[0]);
		commandList->SetGraphicsRootDescriptorTable(3, ssaoSrvHandlesGPU[1]);
		float toneMappingParams[12] = {
			1.0f,
			1.0f,
			3.0f,
			0.70f,
			1.08f,
			1.05f,
			0.18f,
			0.92f,
			0.25f,
			0.15f,
			0.65f,
			0.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 12, toneMappingParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		postProcessBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		postProcessBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &postProcessBarrier);
	}

	//================================================================
	// 闕ｳ・�E�E�E�: Sharpen
	// ToneMapping 陟募�E�E�E�後�E騾匁E�E��E��E�E�E�陷剁E�E��E�奁E�E��E�定氣莉｣・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�螟ｧ・�E�E�E�霈披�E�E�E�驍ｱ・�E�E�E�郢�E�E�E�竏壺�E�E�E�邵�E�E�E�竏ｵ諤咎お繝ｻFXAA 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E��E�E�E�繝ｻ
	//================================================================
	if (hdrCompositeRenderTarget != nullptr && sharpenPipelineState != nullptr) {
		D3D12_RESOURCE_BARRIER sharpenBarrier{};
		sharpenBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		sharpenBarrier.Transition.pResource = hdrCompositeRenderTarget;
		sharpenBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		sharpenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		sharpenBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &sharpenBarrier);

		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &hdrCompositeRtvHandle, FALSE, nullptr);
		commandList->ClearRenderTargetView(hdrCompositeRtvHandle, clearColor, 0, nullptr);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(sharpenPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, postProcessSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, bloomSrvHandlesGPU[0]);
		float sharpenParams[4] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			0.06f,
			0.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 4, sharpenParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		sharpenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		sharpenBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &sharpenBarrier);
	}

	// FXAA: LDR RT 遶翫・back buffer
	D3D12_RESOURCE_BARRIER backBufferBarrier{};
	backBufferBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	backBufferBarrier.Transition.pResource = swapChainResources[backBufferIndex];
	backBufferBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	backBufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	backBufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &backBufferBarrier);

	{
		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, nullptr);
		float backBufferClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], backBufferClearColor, 0, nullptr);
		ID3D12DescriptorHeap* heaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(fxaaPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrCompositeSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, bloomSrvHandlesGPU[0]);
		float fxaaParams[4] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			0.65f,
			0.0312f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 4, fxaaParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);
	}

#ifdef USE_IMGUI
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

	backBufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	backBufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &backBufferBarrier);

	hr = commandList->Close(); // CommandList 郢�E�E�E�蟶晏陶邵�E�E�E�蛟･�E�E�E�・娜U 邵�E�E�E�・�E�E�E�陞ｳ貁E�E��E�・�E�E�E�蠕後堤�E�E�E��E�E�E�髦�E�E�E�・玖ｿ�E�E�E�・�E�E�E�隲�E�E�E�荵昶・驕抵�E�E�E��E�E�E�陞ｳ螢�E�E�E�笘�E�E�E�E��E�E�E�荵敖繝ｻ
	assert(SUCCEEDED(hr));

	// commandLists 邵�E�E�E�・�E�E�E� ExecuteCommandLists 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜻守ｷ帝匁E�E��E��E�E�E� CommandList 鬩滓ｦ翫・邵�E�E�E�繝ｻ
	ID3D12CommandList* commandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, commandLists);

	hr = swapChain->Present(1, 0); // Present 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E雋ょ現竏ｩ back buffer 郢�E�E�E�繝ｻWindow 邵�E�E�E�・�E�E�E�髯�E�E�E�・�E�E�E�驕会ｽ�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	assert(SUCCEEDED(hr));

	if (!hasLoggedFirstPresent) {
		Log(g_logStream, "EditorRenderManager first present completed");
		hasLoggedFirstPresent = true;
	}

	fenceValue++; // fenceValue 郢�E�E�E�蟶敖・�E�E�E�郢�E�E�E�竏堋竏ｽ・�E�E�E�髮∝ｱ鍋ｸ�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E陞ｳ蠕｡・�E�E�E�繝ｻ・�E�E�E�蜥�E�E�E�・�E�E�E�・�E�E�E�郢�E�E�E�繝ｻGPU 邵�E�E�E�・�E�E�E�髫�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));

	// GPU 邵�E�E�E�蠕｡・�E�E�E�髮∝ｱ鍋ｸ�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E郢�E�E�E�蝣�E�E�E�・�E�E�E�繧・斡郢�E�E�E�荵昶穐邵�E�E�E�・�E�E�E�陟輔�E笁E�E��E�邵�E�E�E�竏ｵ・�E�E�E�・�E�E�E�郢晁E�E��E�釁E�E��E��E�晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢�E�E�E�蜻亥�E�E�E�檎ｸ�E�E�E�閧�E�E�E�驪�E�E�E�邵�E�E�E�蛹�E�E�E�窶�E�E�E�郢�E�E�E�繧・�E�E�E��E�E�E�迚吶・邵�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		if (fenceEvent != nullptr) {
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	}

	g_isDrawRequested = false;
	// 闔�E�E�E�E��E�E�E�繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E髫補扱・�E�E�E�繧・�E�E�E�定ｱ�E�E�E�驛�E�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�蜉ｱ笳・�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�竏ｵ・�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� ImGui::Render 邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� Renderer 郢�E�E�E�蜻茨�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�竏夲�E�E�E�狗ｸ�E�E�E�繝ｻ
}
