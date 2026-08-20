#pragma warning(disable : 4189 4514)

#include "EditorRenderManager.h"
#include "EditorOceanSystem.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "EditorPlanarReflectionManager.h"
#include "Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <vector>

using namespace EditorSharedState;

namespace {
	constexpr UINT kCurrentSkinMatrixRootParameter = 20u;
	constexpr UINT kPreviousSkinMatrixRootParameter = 21u;
	constexpr UINT kOceanTessellationTransformRootParameter = 25u;

	void BindSceneObjectSkinningResources(
		ID3D12GraphicsCommandList* commandList,
		const EditorSceneObject* sceneObject) {
		if (commandList == nullptr || g_identitySkinMatrixResource == nullptr) {
			return;
		}

		ID3D12Resource* currentSkinResource = g_identitySkinMatrixResource;
		ID3D12Resource* previousSkinResource = g_identitySkinMatrixResource;

		if (sceneObject != nullptr && sceneObject->usesSkinning &&
			sceneObject->currentSkinMatrixResource != nullptr &&
			sceneObject->previousSkinMatrixResource != nullptr) {
			currentSkinResource = sceneObject->currentSkinMatrixResource;
			previousSkinResource = sceneObject->previousSkinMatrixResource;
		}

		commandList->SetGraphicsRootShaderResourceView(
			kCurrentSkinMatrixRootParameter,
			currentSkinResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootShaderResourceView(
			kPreviousSkinMatrixRootParameter,
			previousSkinResource->GetGPUVirtualAddress());
	}

	void AppendHashBytes(std::uint64_t& hash, const void* data, size_t byteCount) {
		constexpr std::uint64_t kFnvPrime = 1099511628211ull;
		const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);

		for (size_t byteIndex = 0u; byteIndex < byteCount; byteIndex++) {
			hash ^= static_cast<std::uint64_t>(bytes[byteIndex]);
			hash *= kFnvPrime;
		}
	}

	void DrawCustomSceneMesh(
		ID3D12GraphicsCommandList* commandList,
		const EditorSceneObject& sceneObject,
		const Vector3* lodCameraPosition = nullptr,
		bool useShadowLod = false) {
		if (commandList == nullptr) {
			return;
		}

		BindSceneObjectSkinningResources(commandList, &sceneObject);

		commandList->IASetVertexBuffers(0, 1, &sceneObject.customMeshVertexBufferView);
		const UINT instanceCount = sceneObject.surface.mode == 2
			? (std::max)(sceneObject.surface.instanceCount, 1u)
			: 1u;

		if (sceneObject.customMeshIndexResource != nullptr &&
			sceneObject.customMeshIndexCount > 0u) {
			commandList->IASetIndexBuffer(&sceneObject.customMeshIndexBufferView);
			commandList->DrawIndexedInstanced(
				sceneObject.customMeshIndexCount,
				instanceCount,
				0,
				0,
				0);
			return;
		}

		UINT vertexCount = sceneObject.customMeshVertexCount;
		UINT startVertexLocation = 0u;

		if (sceneObject.surface.mode == 1 && lodCameraPosition != nullptr) {
			const Vector3 cameraOffset = Subtract(
				*lodCameraPosition,
				sceneObject.transform.translate);
			const float cameraDistance = Length(cameraOffset);
			const float terrainRadius = (std::max)(
				(std::max)(sceneObject.surface.areaSize.x, sceneObject.surface.areaSize.y) * 0.5f,
				1.0f);
			size_t lodIndex = cameraDistance > terrainRadius * 4.0f
				? 2u
				: (cameraDistance > terrainRadius * 1.5f ? 1u : 0u);

			if (useShadowLod) {
				lodIndex = (std::min)(lodIndex + 1u, static_cast<size_t>(2u));
			}

			vertexCount = sceneObject.surface.terrainLodVertexCounts[lodIndex];
			startVertexLocation = sceneObject.surface.terrainLodVertexOffsets[lodIndex];
		}

		commandList->DrawInstanced(
			vertexCount,
			instanceCount,
			startVertexLocation,
			0u);
	}

	UINT GetSceneObjectInstanceCount(const EditorSceneObject& sceneObject) {
		return sceneObject.surface.mode == 2
			? (std::max)(sceneObject.surface.instanceCount, 1u)
			: 1u;
	}

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

	void CollectParticleCollisionProxies(
		std::vector<EditorGpuParticleManager::CollisionProxy>& collisionProxies) {
		collisionProxies.clear();
		collisionProxies.reserve(EditorGpuParticleManager::kMaxCollisionProxyCount);
		constexpr float kParticleCollisionMaximumDistance = 500.0f;
		constexpr float kParticleCollisionMaximumDistanceSquared =
			kParticleCollisionMaximumDistance * kParticleCollisionMaximumDistance;

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			const Vector3 cameraOffset = Subtract(
				gameObject.translate,
				g_gameCameraPosition);

			if (Dot(cameraOffset, cameraOffset) > kParticleCollisionMaximumDistanceSquared) {
				continue;
			}

			const Matrix4x4 worldMatrix = MakeAffineMatrix(
				gameObject.scale,
				gameObject.rotate,
				gameObject.translate);
			const Vector3 absoluteScale = {
				std::fabs(gameObject.scale.x),
				std::fabs(gameObject.scale.y),
				std::fabs(gameObject.scale.z)};

			for (const EditorComponent& component : gameObject.components) {
				if (!component.isActive || component.isTrigger) {
					continue;
				}

				const bool isBoxProxy =
					component.type == EditorComponentType::BoxCollider ||
					component.type == EditorComponentType::MeshCollider ||
					component.type == EditorComponentType::AutoConvexCollision ||
					component.type == EditorComponentType::TerrainCollider;
				const bool isSphereProxy = component.type == EditorComponentType::SphereCollider;
				const bool isCapsuleProxy = component.type == EditorComponentType::CapsuleCollider;

				if (!isBoxProxy && !isSphereProxy && !isCapsuleProxy) {
					continue;
				}

				EditorGpuParticleManager::CollisionProxy collisionProxy{};
				collisionProxy.center = Transform(component.colliderCenter, worldMatrix);

				if (isSphereProxy) {
					collisionProxy.type = 1.0f;
					const float maximumScale = (std::max)(
						absoluteScale.x,
						(std::max)(absoluteScale.y, absoluteScale.z));
					collisionProxy.extent.x =
						(std::max)(component.colliderRadius * maximumScale, 0.001f);
				}
				else if (isCapsuleProxy) {
					collisionProxy.type = 2.0f;
					const float radiusScale = (std::max)(absoluteScale.x, absoluteScale.z);
					collisionProxy.extent.x =
						(std::max)(component.colliderRadius * radiusScale, 0.001f);
					collisionProxy.extent.y = (std::max)(
						component.colliderSize.y * absoluteScale.y * 0.5f,
						collisionProxy.extent.x);
				}
				else {
					collisionProxy.type = 0.0f;
					collisionProxy.extent = {
						(std::max)(component.colliderSize.x * absoluteScale.x * 0.5f, 0.001f),
						(std::max)(component.colliderSize.y * absoluteScale.y * 0.5f, 0.001f),
						(std::max)(component.colliderSize.z * absoluteScale.z * 0.5f, 0.001f)};
				}

				collisionProxies.push_back(collisionProxy);

				if (collisionProxies.size() >= EditorGpuParticleManager::kMaxCollisionProxyCount) {
					return;
				}
			}
		}
	}

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

	struct ClipSpacePoint {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	ClipSpacePoint TransformToClipSpace(const Vector3& position, const Matrix4x4& matrix) {
		return {
			position.x * matrix.matrix[0][0] +
				position.y * matrix.matrix[1][0] +
				position.z * matrix.matrix[2][0] +
				matrix.matrix[3][0],
			position.x * matrix.matrix[0][1] +
				position.y * matrix.matrix[1][1] +
				position.z * matrix.matrix[2][1] +
				matrix.matrix[3][1],
			position.x * matrix.matrix[0][2] +
				position.y * matrix.matrix[1][2] +
				position.z * matrix.matrix[2][2] +
				matrix.matrix[3][2],
			position.x * matrix.matrix[0][3] +
				position.y * matrix.matrix[1][3] +
				position.z * matrix.matrix[2][3] +
				matrix.matrix[3][3]};
	}

	bool IsSceneObjectInsideViewFrustum(
		const EditorSceneObject& sceneObject,
		const Matrix4x4& viewProjectionMatrix) {
		const Vector3 localBoundsCenter = GetPlanarReflectionLocalMeshCenter(sceneObject);
		const Vector3 localBoundsSize = GetPlanarReflectionLocalMeshSize(sceneObject);
		const Vector3 localBoundsExtent = {
			localBoundsSize.x * 0.5f,
			localBoundsSize.y * 0.5f,
			localBoundsSize.z * 0.5f};
		const Matrix4x4 worldViewProjectionMatrix = Multiply(
			sceneObject.worldMatrix,
			viewProjectionMatrix);

		bool isOutsideLeft = true;
		bool isOutsideRight = true;
		bool isOutsideBottom = true;
		bool isOutsideTop = true;
		bool isOutsideNear = true;
		bool isOutsideFar = true;

		// AABBの8頂点が同じClip平面の外側にある時だけ除外する。
		for (int32_t cornerIndex = 0; cornerIndex < 8; cornerIndex++) {
			const Vector3 localCorner = {
				localBoundsCenter.x + ((cornerIndex & 1) != 0 ? localBoundsExtent.x : -localBoundsExtent.x),
				localBoundsCenter.y + ((cornerIndex & 2) != 0 ? localBoundsExtent.y : -localBoundsExtent.y),
				localBoundsCenter.z + ((cornerIndex & 4) != 0 ? localBoundsExtent.z : -localBoundsExtent.z)};
			const ClipSpacePoint clipPoint = TransformToClipSpace(
				localCorner,
				worldViewProjectionMatrix);

			isOutsideLeft = isOutsideLeft && clipPoint.x < -clipPoint.w;
			isOutsideRight = isOutsideRight && clipPoint.x > clipPoint.w;
			isOutsideBottom = isOutsideBottom && clipPoint.y < -clipPoint.w;
			isOutsideTop = isOutsideTop && clipPoint.y > clipPoint.w;
			isOutsideNear = isOutsideNear && clipPoint.z < 0.0f;
			isOutsideFar = isOutsideFar && clipPoint.z > clipPoint.w;
		}

		return !isOutsideLeft && !isOutsideRight && !isOutsideBottom &&
			!isOutsideTop && !isOutsideNear && !isOutsideFar;
	}

	void ClearDisabledSceneLight(DirectionalLight& light) {
		light = {};
		light.color = {0.0f, 0.0f, 0.0f, 1.0f};  // 無効ライトは色の寄与を持たせない。
		light.direction = {0.0f, -1.0f, 0.0f};  // 方向ベクトルだけは正規化不能を避けるため保持する。
		light.intensity = 0.0f;  // Light コンポーネントがない時は直接光を完全に消す。
		light.position = {0.0f, 0.0f, 0.0f};
		light.range = 0.0f;
		light.skyUpperColor = {0.0f, 0.0f, 0.0f};  // Environment コンポーネントがない時の空色。
		light.skyIntensity = 0.0f;
		light.skyLowerColor = {0.0f, 0.0f, 0.0f};
		light.skyEmission = 0.0f;
		light.ambientIntensity = 0.0f;  // 環境光未配置時にモデル固定の明るさが出ないようにする。
		light.horizonSharpness = 1.0f;
		light.reflectionIntensity = 0.0f;
		light.spotCosInner = std::cos(20.0f * (3.14159265f / 180.0f));
		light.spotCosOuter = std::cos(30.0f * (3.14159265f / 180.0f));
		light.lightType = 0;
		light.areaRadius = 0.0f;
		light.cameraPosition = {0.0f, 0.0f, -5.0f};
		light.environmentTextureEnabled = 0.0f;  // HDRI / IBL は Environment で明示した時だけ使う。
		light.environmentTextureIntensity = 0.0f;
		light.environmentTextureRotation = 0.0f;
		light.environmentTextureMipBias = 0.0f;
		light.shadowTileIndex = -1.0f;
		light.shadowTileUvScaleX = 0.0f;
		light.shadowTileUvScaleY = 0.0f;
		light.shadowTileUvBiasX = 0.0f;
		light.shadowTileUvBiasY = 0.0f;
		light.shadowEnabled = -1.0f;  // Shader 側でライト列の終端として扱う。
	}

	// 色温度(Kelvin)からsRGB相当のRGB色へ変換する。Tanner Helland近似式を1.0基準へ正規化して使う。
	// 5500K～6500K付近が白、それより低いと橙～赤、高いと青白くなる。
	Vector3 KelvinToRgb(float temperatureKelvin) {
		const float temperature = (std::clamp)(temperatureKelvin, 1000.0f, 40000.0f) / 100.0f;
		float red;
		float green;
		float blue;

		if (temperature <= 66.0f) {
			red = 255.0f;
			green = 99.4708025861f * std::log(temperature) - 161.1195681661f;

			if (temperature <= 19.0f) {
				blue = 0.0f;
			} else {
				blue = 138.5177312231f * std::log(temperature - 10.0f) - 305.0447927307f;
			}
		} else {
			red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
			green = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
			blue = 255.0f;
		}

		return Vector3{
			(std::clamp)(red, 0.0f, 255.0f) / 255.0f,
			(std::clamp)(green, 0.0f, 255.0f) / 255.0f,
			(std::clamp)(blue, 0.0f, 255.0f) / 255.0f};
	}

	// 太陽高度からKelvinを自動推定する。夕方(低高度)ほど暖色、昼(高高度)ほど白～青白い方向へ寄せる。
	float EstimateSunKelvinFromElevation(float elevationDegrees) {
		const float t = (std::clamp)(elevationDegrees, -5.0f, 90.0f);
		const float lowElevationKelvin = 2000.0f;   // 地平線付近。かなり橙。
		const float midElevationKelvin = 4500.0f;   // 中程度の高さ。やや暖色。
		const float highElevationKelvin = 6500.0f;  // 高い昼。青白い。

		if (t <= 10.0f) {
			const float blend = (std::clamp)((t + 5.0f) / 15.0f, 0.0f, 1.0f);
			return lowElevationKelvin + (midElevationKelvin - lowElevationKelvin) * blend;
		}

		const float blend = (std::clamp)((t - 10.0f) / 55.0f, 0.0f, 1.0f);
		return midElevationKelvin + (highElevationKelvin - midElevationKelvin) * blend;
	}

	// 太陽方位角(度、0=+Z、90=+Xで時計回り)と高度(度、90=真上)から、
	// Light.directionと同じ「光が進む向き」のワールド方向ベクトルを作る。
	Vector3 BuildSunDirectionFromAzimuthElevation(float azimuthDegrees, float elevationDegrees) {
		const float azimuthRadian = azimuthDegrees * (3.14159265f / 180.0f);
		const float elevationRadian = elevationDegrees * (3.14159265f / 180.0f);
		const float horizontalRadius = std::cos(elevationRadian);
		const Vector3 towardSun{
			horizontalRadius * std::sin(azimuthRadian),
			std::sin(elevationRadian),
			horizontalRadius * std::cos(azimuthRadian)};
		// dl.directionは「光源からどちらへ進むか」なので、太陽が居る向きの反対を返す。
		return Vector3{-towardSun.x, -towardSun.y, -towardSun.z};
	}

	int32_t CollectSceneLights(DirectionalLight* lightsOut) {
		for (int32_t i = 0; i < kMaxShadowLights; i++) {
			ClearDisabledSceneLight(lightsOut[i]);
		}

		struct SceneLightCandidate {
			const EditorGameObject* gameObject = nullptr;
			const EditorComponent* component = nullptr;
			float cameraDistanceSquared = 0.0f;
			bool isSun = false;
		};

		static std::vector<SceneLightCandidate> lightCandidates;
		lightCandidates.clear();
		lightCandidates.reserve(g_editorScene.GetGameObjects().size());

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}

			for (const EditorComponent& component : gameObject.components) {
				if (component.type != EditorComponentType::Light || !component.isActive) {
					continue;
				}

				const Vector3 cameraOffset = Subtract(
					gameObject.translate,
					g_gameCameraPosition);
				lightCandidates.push_back(SceneLightCandidate{
					&gameObject,
					&component,
					Dot(cameraOffset, cameraOffset),
					component.assetPath == "Sun"});
				break;
			}
		}

		std::stable_sort(
			lightCandidates.begin(),
			lightCandidates.end(),
			[](const SceneLightCandidate& left, const SceneLightCandidate& right) {
				if (left.isSun != right.isSun) {
					return left.isSun;
				}

				return left.cameraDistanceSquared < right.cameraDistanceSquared;
			});

		const int32_t count = static_cast<int32_t>((std::min)(
			lightCandidates.size(),
			static_cast<size_t>(kMaxShadowLights)));

		for (int32_t lightIndex = 0; lightIndex < count; lightIndex++) {
			const SceneLightCandidate& candidate = lightCandidates[static_cast<size_t>(lightIndex)];
			const EditorGameObject& gameObject = *candidate.gameObject;
			const EditorComponent& component = *candidate.component;
			DirectionalLight& dl = lightsOut[lightIndex];
			dl = {};
			dl.color = {component.color.x, component.color.y, component.color.z, 1.0f};
			dl.intensity = component.intensity;
			dl.position = gameObject.translate;
			dl.range = (std::max)(component.colliderRadius, 0.01f);
			dl.areaRadius = (std::max)(component.colliderSize.z, 0.01f);
			dl.lightType = GetLightTypeFromComponent(component);

			const float innerAngleRadian = component.colliderSize.x * (3.14159265f / 180.0f);
			const float outerAngleRadian = component.colliderSize.y * (3.14159265f / 180.0f);
			dl.spotCosInner = std::cos(innerAngleRadian);
			dl.spotCosOuter = std::cos(outerAngleRadian);

			Vector3 forwardDirection = GetForwardDirectionFromRotation(gameObject.rotate);

			if (Length(forwardDirection) <= 0.0001f) {
				forwardDirection = {0.0f, -1.0f, 0.0f};
			}

			dl.direction = Normalize(forwardDirection);
			dl.shadowEnabled = 1.0f;

			// 太陽(Sun)は方位角/高度と色温度から、Directional Lightの上位概念として
			// sunDirection / sunColor を作れるようにする。両方とも既定はOFFで、
			// 既存Sceneの見た目(Transform回転とcolorフィールド)をそのまま維持する。
			if (component.assetPath == "Sun") {
				if (component.sunUseAzimuthElevation) {
					dl.direction = BuildSunDirectionFromAzimuthElevation(
						component.sunAzimuthDegrees,
						component.sunElevationDegrees);
				}

				if (component.sunUseColorTemperature) {
					const float elevationDegrees = component.sunUseAzimuthElevation
						? component.sunElevationDegrees
						: std::asin((std::clamp)(-dl.direction.y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);
					const float temperatureKelvin = component.sunAutoTemperatureFromElevation
						? EstimateSunKelvinFromElevation(elevationDegrees)
						: component.sunTemperatureKelvin;
					const Vector3 temperatureColor = KelvinToRgb(temperatureKelvin);
					dl.color = {temperatureColor.x, temperatureColor.y, temperatureColor.z, 1.0f};
				}
			}
		}

		return count;
	}

	void ApplyEnvironmentComponent(DirectionalLight* directionalLightData) {
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}
			const EditorComponent* env =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Environment);
			if (env == nullptr || !env->isActive) {
				continue;
			}
			directionalLightData->skyUpperColor = env->color;
			directionalLightData->skyIntensity = env->intensity;
			directionalLightData->skyLowerColor = env->skyLowerColor;
			directionalLightData->horizonSharpness = (std::max)(env->roughness, 0.0001f);
			directionalLightData->skyEmission = env->emissionStrength;
			directionalLightData->reflectionIntensity = env->reflectionStrength;
			directionalLightData->ambientIntensity = env->metallic;
			directionalLightData->environmentTextureEnabled = env->environmentTextureEnabled ? 1.0f : 0.0f;
			directionalLightData->environmentTextureIntensity = env->intensity;
			directionalLightData->environmentTextureRotation = env->environmentTextureRotation;
			directionalLightData->environmentTextureMipBias = env->environmentTextureMipBias;
			break;
		}
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
		Vector3 shadowCenter = CalculateShadowCenter(
			editorSceneObjects,
			legacyTransform,
			isLegacyPreviewVisible);
		float shadowRadius = CalculateShadowRadius(
			editorSceneObjects,
			legacyTransform,
			shadowCenter,
			isLegacyPreviewVisible);

		Vector3 lightEye;
		Vector3 lightTarget = shadowCenter;

		if (directionalLightData != nullptr && directionalLightData->lightType == 0) {
			// Sun は巨大な Ocean を含む Scene 全体ではなく、現在のカメラ近傍へ解像度を集中する。
			const Vector3 lightDirection = GetSafeLightDirection(directionalLightData);
			const float cameraFocusBlend = 0.72f;
			shadowCenter = Add(
				Multiply(1.0f - cameraFocusBlend, shadowCenter),
				Multiply(cameraFocusBlend, directionalLightData->cameraPosition));
			shadowRadius = (std::clamp)(shadowRadius * 0.58f, 8.0f, 96.0f);

			// 光空間の中心を atlas の 1 texel 単位へ固定し、カメラ移動時の影の揺れを止める。
			Vector3 lightRight = Normalize(Cross(Vector3{0.0f, 1.0f, 0.0f}, lightDirection));

			if (Length(lightRight) <= 0.0001f) {
				lightRight = Normalize(Cross(Vector3{1.0f, 0.0f, 0.0f}, lightDirection));
			}

			const Vector3 lightUp = Normalize(Cross(lightDirection, lightRight));
			const float shadowTileResolution =
				static_cast<float>(kRuntimeShadowMapSize) /
				static_cast<float>(kShadowAtlasTiles);
			const float worldUnitsPerTexel =
				shadowRadius * 2.0f /
				(std::max)(shadowTileResolution, 1.0f);
			const float centerAlongRight = Dot(shadowCenter, lightRight);
			const float centerAlongUp = Dot(shadowCenter, lightUp);
			const float snappedRight = std::floor(
				centerAlongRight / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
			const float snappedUp = std::floor(
				centerAlongUp / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
			shadowCenter = Add(
				shadowCenter,
				Add(
					Multiply(snappedRight - centerAlongRight, lightRight),
					Multiply(snappedUp - centerAlongUp, lightUp)));
			lightTarget = shadowCenter;
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

	Matrix4x4 MakeSunCascadeViewProjectionMatrix(
		const DirectionalLight& light,
		const Vector3& cameraPosition,
		const Vector3& cameraForward,
		const Matrix4x4& cameraProjectionMatrix,
		float cascadeNearDistance,
		float cascadeFarDistance) {
		const float safeProjectionScaleX = (std::max)(
			std::fabs(cameraProjectionMatrix.matrix[0][0]),
			0.0001f);
		const float safeProjectionScaleY = (std::max)(
			std::fabs(cameraProjectionMatrix.matrix[1][1]),
			0.0001f);
		const float farHalfWidth = cascadeFarDistance / safeProjectionScaleX;
		const float farHalfHeight = cascadeFarDistance / safeProjectionScaleY;
		const float halfDepth = (cascadeFarDistance - cascadeNearDistance) * 0.5f;
		float cascadeRadius = std::sqrt(
			farHalfWidth * farHalfWidth +
			farHalfHeight * farHalfHeight +
			halfDepth * halfDepth);
		cascadeRadius = (std::max)(cascadeRadius, 4.0f);

		const float cascadeCenterDistance =
			(cascadeNearDistance + cascadeFarDistance) * 0.5f;
		Vector3 cascadeCenter = Add(
			cameraPosition,
			Multiply(cascadeCenterDistance, cameraForward));
		const Vector3 lightDirection = GetSafeLightDirection(&light);
		Vector3 lightRight = Normalize(Cross(Vector3{0.0f, 1.0f, 0.0f}, lightDirection));

		if (Length(lightRight) <= 0.0001f) {
			lightRight = Normalize(Cross(Vector3{1.0f, 0.0f, 0.0f}, lightDirection));
		}

		const Vector3 lightUp = Normalize(Cross(lightDirection, lightRight));
		const float shadowTileResolution =
			static_cast<float>(kRuntimeShadowMapSize) /
			static_cast<float>(kShadowAtlasTiles);
		const float worldUnitsPerTexel =
			cascadeRadius * 2.0f /
			(std::max)(shadowTileResolution, 1.0f);
		const float centerAlongRight = Dot(cascadeCenter, lightRight);
		const float centerAlongUp = Dot(cascadeCenter, lightUp);
		const float snappedRight = std::floor(
			centerAlongRight / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		const float snappedUp = std::floor(
			centerAlongUp / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
		cascadeCenter = Add(
			cascadeCenter,
			Add(
				Multiply(snappedRight - centerAlongRight, lightRight),
				Multiply(snappedUp - centerAlongUp, lightUp)));

		const Vector3 lightEye = Subtract(
			cascadeCenter,
			Multiply(cascadeRadius * 2.0f, lightDirection));
		const Matrix4x4 lightViewMatrix = MakeLookAtMatrix(
			lightEye,
			cascadeCenter,
			Vector3{0.0f, 1.0f, 0.0f});
		const Matrix4x4 lightProjectionMatrix = MakeOrthographicMatrix(
			-cascadeRadius,
			cascadeRadius,
			cascadeRadius,
			-cascadeRadius,
			0.1f,
			cascadeRadius * 4.0f + 80.0f);
		return Multiply(lightViewMatrix, lightProjectionMatrix);
	}

	float MakeHaltonValue(uint32_t sampleIndex, uint32_t baseValue) {
		float result = 0.0f;
		float fraction = 1.0f;

		while (sampleIndex > 0u) {
			fraction /= static_cast<float>(baseValue);
			result += fraction * static_cast<float>(sampleIndex % baseValue);
			sampleIndex /= baseValue;
		}

		return result;
	}

	Vector2 MakeTemporalJitterNdc(uint32_t frameIndex, float viewportWidth, float viewportHeight) {
		const uint32_t sampleIndex = frameIndex % 16u + 1u;
		const float jitterPixelX = MakeHaltonValue(sampleIndex, 2u) - 0.5f;
		const float jitterPixelY = MakeHaltonValue(sampleIndex, 3u) - 0.5f;

		return {
			jitterPixelX * 2.0f / (std::max)(viewportWidth, 1.0f),
			-jitterPixelY * 2.0f / (std::max)(viewportHeight, 1.0f)
		};
	}

	Matrix4x4 ApplyTemporalProjectionJitter(
		const Matrix4x4& projectionMatrix,
		const Vector2& jitterNdc) {
		Matrix4x4 jitteredProjectionMatrix = projectionMatrix;
		const bool isPerspectiveProjection =
			std::abs(jitteredProjectionMatrix.matrix[2][3]) >= 0.5f;

		if (isPerspectiveProjection) {
			jitteredProjectionMatrix.matrix[2][0] += jitterNdc.x;
			jitteredProjectionMatrix.matrix[2][1] += jitterNdc.y;
		}
		else {
			jitteredProjectionMatrix.matrix[3][0] += jitterNdc.x;
			jitteredProjectionMatrix.matrix[3][1] += jitterNdc.y;
		}

		return jitteredProjectionMatrix;
	}

	struct PostProcessSettings {
		bool hasPostProcessComponent = false;  // PostProcess コンポーネントがある時だけ任意エフェクトを有効にする。
		float bloomIntensity = 0.0f;
		float bloomThreshold = 1.0f;
		float bloomSoftKnee = 0.5f;
		float bloomScatter = 0.72f;
		float finalBrightness = 1.0f;
		float sharpenStrength = 0.0f;
		int32_t aaMode = 0;
		float smaaThreshold = 0.10f;
		float smaaCornerRounding = 25.0f;
		float temporalSharpness = 0.08f;
		float temporalBlendRatio = 0.90f;
		int32_t glareModeMask = 0;
		float glareIntensity = 0.0f;
		float glareSize = 1.0f;
		float glareAngle = 0.0f;
		int32_t glareStreakCount = 4;
		float glareFade = 0.85f;
		float glareColorModulation = 0.15f;
		Vector3 glareCenter = {0.5f, 0.5f, 0.0f};
		std::array<float, 8> glareIntensityByMode{};
		std::array<float, 8> glareSizeByMode{};
		std::array<float, 8> glareAngleByMode{};
		std::array<int32_t, 8> glareStreakCountByMode{};
		std::array<float, 8> glareFadeByMode{};
		std::array<float, 8> glareColorModulationByMode{};
		std::array<Vector3, 8> glareCenterByMode{};
		std::array<Vector3, 8> glareColorByMode{};
		int32_t filterMode = 0;
		int32_t filterModeMask = 0;
		float filterStrength = 1.0f;
		std::array<float, 9> filterStrengthByMode{};
		std::array<Vector3, 9> filterColorByMode{};
		bool ssrEnabled = false;
		float compositeExposure = 1.0f;
		float compositeWhitePoint = 3.0f;
		int32_t compositeToneMappingMode = 3;
		float compositeBloomIntensity = 0.0f;
		float compositeSaturation = 1.0f;
		float compositeContrast = 1.0f;
		float compositeVignetteStrength = 0.0f;
		float compositeVignetteRadius = 0.92f;
		float compositeFilmGrain = 0.0f;
		float compositeChromaticAberration = 0.0f;
		float compositeAmbientOcclusionStrength = 0.0f;
		bool compositeAutoExposureEnabled = false;
		float compositeMinimumExposure = 0.35f;
		float compositeMaximumExposure = 3.0f;
		float compositeExposureAdaptationSpeed = 1.8f;
		float compositeTargetLuminance = 0.18f;
		float compositeTemperature = 0.0f;
		float compositeTint = 0.0f;
		Vector3 compositeLift = {0.0f, 0.0f, 0.0f};
		float compositeGamma = 1.0f;
		Vector3 compositeGain = {1.0f, 1.0f, 1.0f};
		float compositeLocalContrast = 0.0f;
		float compositeOutputDither = 0.0f;
		bool compositeSsgiEnabled = false;
		float compositeSsgiIntensity = 0.0f;
		float compositeSsgiRadiusPixels = 18.0f;
		std::string compositeColorLutAssetPath;
		float compositeColorLutStrength = 1.0f;
		int32_t compositeDebugView = 0;
		bool cameraDofEnabled = false;
		float cameraDofFocusDistance = 10.0f;
		float cameraDofAperture = 0.1f;
		float cameraDofFocalLength = 50.0f;
		bool cameraMotionBlurEnabled = false;
		float cameraMotionBlurIntensity = 0.5f;
		float cameraNearClip = 0.3f;
		float cameraFarClip = 1000.0f;
		float environmentHeatIntensity = 0.0f;
		float environmentHeatHorizonCenter = 0.46f;
		float environmentHeatHorizonWidth = 0.16f;
		float environmentHeatSunInfluence = 0.55f;
		float environmentHeatDistortionScale = 0.65f;
	};

	void InitializePostProcessModeDefaults(PostProcessSettings& settings) {
		settings.glareIntensityByMode.fill(settings.glareIntensity);
		settings.glareSizeByMode.fill(settings.glareSize);
		settings.glareAngleByMode.fill(settings.glareAngle);
		settings.glareStreakCountByMode.fill(settings.glareStreakCount);
		settings.glareFadeByMode.fill(settings.glareFade);
		settings.glareColorModulationByMode.fill(settings.glareColorModulation);
		settings.glareCenterByMode.fill(settings.glareCenter);
		settings.glareColorByMode.fill({1.0f, 1.0f, 1.0f});
		settings.filterStrengthByMode.fill(settings.filterStrength);
		settings.filterColorByMode.fill({1.0f, 1.0f, 1.0f});
	}

	PostProcessSettings GetPostProcessSettings() {
		PostProcessSettings settings;
		InitializePostProcessModeDefaults(settings);
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}
			const EditorComponent* environment =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Environment);

			if (environment != nullptr && environment->isActive) {
				settings.environmentHeatIntensity = environment->environmentHeatIntensity;
				settings.environmentHeatHorizonCenter = environment->environmentHeatHorizonCenter;
				settings.environmentHeatHorizonWidth = environment->environmentHeatHorizonWidth;
				settings.environmentHeatSunInfluence = environment->environmentHeatSunInfluence;
				settings.environmentHeatDistortionScale = environment->environmentHeatDistortionScale;
			}

			const EditorComponent* pp =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::PostProcess);
			if (pp == nullptr || !pp->isActive) {
				pp = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Volume);
			}
			if (pp != nullptr && pp->isActive) {
				const bool isVolume = pp->type == EditorComponentType::Volume;
				const float volumeWeight = isVolume
					? (std::clamp)(pp->intensity, 0.0f, 1.0f)
					: 1.0f;
				settings.hasPostProcessComponent = true;
				settings.bloomIntensity = pp->bloomIntensity;
				settings.bloomThreshold = pp->bloomThreshold;
				settings.bloomSoftKnee = pp->bloomSoftKnee;
				settings.bloomScatter = pp->bloomScatter;
				settings.finalBrightness = pp->finalBrightness;
				settings.aaMode = pp->aaMode;
				settings.smaaThreshold = pp->smaaThreshold;
				settings.smaaCornerRounding = pp->smaaCornerRounding;
				settings.temporalSharpness = pp->temporalSharpness;
				settings.temporalBlendRatio = pp->temporalBlendRatio;
				settings.glareModeMask = pp->glareModeMask;
				settings.glareIntensity = pp->glareIntensity;
				settings.glareSize = pp->glareSize;
				settings.glareAngle = pp->glareAngle;
				settings.glareStreakCount = pp->glareStreakCount;
				settings.glareFade = pp->glareFade;
				settings.glareColorModulation = pp->glareColorModulation;
				settings.glareCenter = pp->glareCenter;
				settings.glareIntensityByMode = pp->glareIntensityByMode;
				settings.glareSizeByMode = pp->glareSizeByMode;
				settings.glareAngleByMode = pp->glareAngleByMode;
				settings.glareStreakCountByMode = pp->glareStreakCountByMode;
				settings.glareFadeByMode = pp->glareFadeByMode;
				settings.glareColorModulationByMode = pp->glareColorModulationByMode;
				settings.glareCenterByMode = pp->glareCenterByMode;
				settings.glareColorByMode = pp->glareColorByMode;
				settings.filterMode = pp->filterMode;
				settings.filterModeMask = pp->filterModeMask;
				settings.filterStrength = pp->filterStrength;
				settings.filterStrengthByMode = pp->filterStrengthByMode;
				settings.filterColorByMode = pp->filterColorByMode;
				settings.ssrEnabled = pp->ssrEnabled;
				settings.compositeExposure = pp->compositeExposure;
				settings.compositeWhitePoint = pp->compositeWhitePoint;
				settings.compositeToneMappingMode = pp->compositeToneMappingMode;
				settings.compositeBloomIntensity = pp->compositeBloomIntensity;
				settings.compositeSaturation = pp->compositeSaturation;
				settings.compositeContrast = pp->compositeContrast;
				settings.compositeVignetteStrength = pp->compositeVignetteStrength;
				settings.compositeVignetteRadius = pp->compositeVignetteRadius;
				settings.compositeFilmGrain = pp->compositeFilmGrain;
				settings.compositeChromaticAberration = pp->compositeChromaticAberration;
				settings.compositeAmbientOcclusionStrength = pp->compositeAmbientOcclusionStrength;
				settings.compositeAutoExposureEnabled = pp->compositeAutoExposureEnabled;
				settings.compositeMinimumExposure = pp->compositeMinimumExposure;
				settings.compositeMaximumExposure = pp->compositeMaximumExposure;
				settings.compositeExposureAdaptationSpeed = pp->compositeExposureAdaptationSpeed;
				settings.compositeTargetLuminance = pp->compositeTargetLuminance;
				settings.compositeTemperature = pp->compositeTemperature;
				settings.compositeTint = pp->compositeTint;
				settings.compositeLift = pp->compositeLift;
				settings.compositeGamma = pp->compositeGamma;
				settings.compositeGain = pp->compositeGain;
				settings.compositeLocalContrast = pp->compositeLocalContrast;
				settings.compositeOutputDither = pp->compositeOutputDither;
				settings.compositeSsgiEnabled = pp->compositeSsgiEnabled;
				settings.compositeSsgiIntensity = pp->compositeSsgiIntensity;
				settings.compositeSsgiRadiusPixels = pp->compositeSsgiRadiusPixels;
				settings.compositeColorLutAssetPath = pp->compositeColorLutAssetPath;
				settings.compositeColorLutStrength = pp->compositeColorLutStrength;
				settings.compositeDebugView = pp->compositeDebugView;

				if (isVolume) {
					settings.bloomIntensity *= volumeWeight;
					settings.sharpenStrength *= volumeWeight;
					settings.glareIntensity *= volumeWeight;
					settings.compositeBloomIntensity *= volumeWeight;
					settings.compositeVignetteStrength *= volumeWeight;
					settings.compositeFilmGrain *= volumeWeight;
					settings.compositeChromaticAberration *= volumeWeight;
					settings.compositeAmbientOcclusionStrength *= volumeWeight;
					settings.compositeLocalContrast *= volumeWeight;
					settings.compositeOutputDither *= volumeWeight;
					settings.compositeSsgiIntensity *= volumeWeight;
					settings.compositeExposure = 1.0f + (settings.compositeExposure - 1.0f) * volumeWeight;
					settings.compositeSaturation = 1.0f + (settings.compositeSaturation - 1.0f) * volumeWeight;
					settings.compositeContrast = 1.0f + (settings.compositeContrast - 1.0f) * volumeWeight;
					settings.compositeGamma = 1.0f + (settings.compositeGamma - 1.0f) * volumeWeight;
					settings.compositeTemperature *= volumeWeight;
					settings.compositeTint *= volumeWeight;
					settings.compositeLift = {
						settings.compositeLift.x * volumeWeight,
						settings.compositeLift.y * volumeWeight,
						settings.compositeLift.z * volumeWeight};
					settings.compositeGain = {
						1.0f + (settings.compositeGain.x - 1.0f) * volumeWeight,
						1.0f + (settings.compositeGain.y - 1.0f) * volumeWeight,
						1.0f + (settings.compositeGain.z - 1.0f) * volumeWeight};
				}
			}
			const EditorComponent* camera =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
			if (camera != nullptr && camera->isActive) {
				settings.cameraDofEnabled = camera->cameraDofEnabled;
				settings.cameraDofFocusDistance = camera->cameraDofFocusDistance;
				settings.cameraDofAperture = camera->cameraDofAperture;
				settings.cameraDofFocalLength = camera->cameraDofFocalLength;
				settings.cameraMotionBlurEnabled = camera->cameraMotionBlurEnabled;
				settings.cameraMotionBlurIntensity = camera->cameraMotionBlurIntensity;
				settings.cameraNearClip = camera->cameraNearClip;
				settings.cameraFarClip = camera->cameraFarClip;
			}
		}
		return settings;
	}
}

void EditorRenderManager::Initialize() {
}

void EditorRenderManager::Update() {
}

void EditorRenderManager::Draw() {
	static bool hasLoggedFirstRenderEnter = false; // 隴崢陋ｻ譏ｴ繝ｻ Draw 邵�E�E�E�・�E�E�E�陷茨�E�E�E��E�E�E�郢�E�E�E�蠕娯螺邵�E�E�E�荵晢�E�E�E�・1 陜玲�E�E�E��E�E�E�笁E�E��E�邵�E�E�E�鬘鯉ｽ�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	static bool hasLoggedFirstPresent = false;
	static bool hasSubmittedShadowMap = false;
	static std::uint64_t submittedShadowStateHash = 0u;
	static bool hasSubmittedPlanarReflection = false;
	static std::uint64_t submittedPlanarReflectionStateHash = 0u;
	static ID3D12Resource* submittedPlanarReflectionTarget = nullptr;
	static uint32_t oceanReflectionUpdateFrameIndex = 0u;
	static uint32_t temporalJitterFrameIndex = 0u;
	// Editor待機中は波面を静止させ、同じFFTを毎フレーム22 Dispatchしない。Play中は物理と同じ共通時刻を使う。
	const float oceanElapsedTime = g_editorRuntimeManager.IsPlaying()
		? GetEditorOceanElapsedTime()
		: 0.0f;
	// 隴崢陋ｻ譏ｴ繝ｻ Present 邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�陋ｻ・�E�E�E�鬩墓鱒�E�E�E�E�邵�E�E�E�貁E�E��E��E�郢�E�E�E�繝ｻ1 陜玲�E�E�E��E�E�E�笁E�E��E�邵�E�E�E�鬘鯉ｽ�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ

	auto& hr = g_hr; // hr 邵�E�E�E�・�E�E�E� DirectX API 邵�E�E�E�・�E�E�E�隰御�E�E�E�吝℡郢�E�E�E�雋槫�E�E�E��E�E�E�邵�E�E�E�螟ｧ蜿咏ｹ�E�E�E�蜿�E�E�E�繝ｻ隴帙�EHRESULT邵�E�E�E�繝ｻ
	auto& device = g_device;
	// device 邵�E�E�E�・�E�E�E�闔蛾宦・�E�E�E�蠕後�E隰�E�E�E�蜀怜�E隲�E�E�E�・�E�E�E�陟托�E�E�E��E�E�E�邵�E�E�E�・�E�E�E� Resource 郢�E�E�E�螳夲�E�E�E��E�E�E�・�E�E�E�陷会｣�E�E�E�闖ｴ諛医・邵�E�E�E�蜷�E�E�E�・玖ｭ弱�E�E�E�E�E��E��E�邵�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�陷�E�E�E�繧峨・邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	auto& commandQueue = g_commandQueue;
	// commandQueue / Allocator / List 邵�E�E�E�・�E�E�E� GPU 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E陷�E�E�E�・�E�E�E�闔会ｽ�E�E�E�郢�E�E�E�蟶敖竏夲�E�E�E�狗ｸ�E�E�E�貁E�E��E��E�E�E�∫�E�E�E��E�E�E�・�E�E�E�闖ｴ・�E�E�E�邵�E�E�E�繝ｻ�E�E�E�繝ｻ
	auto& commandAllocator = g_commandAllocator;
	auto& commandList = g_commandList;
	auto& renderTimestampQueryHeap = g_renderTimestampQueryHeap;
	auto& renderTimestampReadback = g_renderTimestampReadback;
	auto& renderTimestampFrequency = g_renderTimestampFrequency;
	auto& renderProfile = g_renderProfile;
	auto& useAdapter = g_useAdapter;

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
	auto& opaqueDepthCopyResource = g_opaqueDepthCopyResource;
	auto& opaqueDepthCopySrvHandleGPU = g_opaqueDepthCopySrvHandleGPU;

	auto& rootSignature = g_rootSignature;
	// rootSignature / graphicsPipelineState 邵�E�E�E�・�E�E�E� Shader 邵�E�E�E�・�E�E�E� RenderState 邵�E�E�E�・�E�E�E�陜暦�E�E�E��E�E�E�陞ｳ螟奁E�E��E��E�E�E�・�E�E�E�陞ｳ螢�E�E�E��E�E�E�繝ｻ
	auto& graphicsPipelineState = g_graphicsPipelineState;
	auto& planarScenePipelineState = g_planarScenePipelineState;
	auto& planarSurfacePipelineState = g_planarSurfacePipelineState;
	auto& objectReflectionMaskPipelineState = g_objectReflectionMaskPipelineState;
	auto& shadowPipelineState = g_shadowPipelineState;
	auto& shadowCullNonePipelineState = g_shadowCullNonePipelineState;
	auto& alphaCutoutShadowPipelineState = g_alphaCutoutShadowPipelineState;
	auto& alphaCutoutShadowCullNonePipelineState = g_alphaCutoutShadowCullNonePipelineState;
	auto& waterSurfacePipelineState = g_waterSurfacePipelineState;
	auto& waterTessellationPipelineState = g_waterTessellationPipelineState;
	auto& refractiveSurfacePipelineState = g_refractiveSurfacePipelineState;
	auto& refractiveSurfaceCullNonePipelineState = g_refractiveSurfaceCullNonePipelineState;
	// shadowPipelineState 邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晞メ・�E�E�E�荵溘○邵�E�E�E�・�E�E�E� DepthTexture 郢�E�E�E�蜑�E�E�E�E��E�E�E�諛奁E�E��E�玖氣繧臥舁EPSO邵�E�E�E�繝ｻ
	auto& shadowMapResource = g_shadowMapResource;
	auto& shadowDsvHandle = g_shadowDsvHandle;
	auto& shadowMapSrvGpuHandle = g_shadowMapSrvGpuHandle;

	auto& hdrRenderTarget = g_hdrRenderTarget;
	auto& hdrRtvHandle = g_hdrRtvHandle;
	auto& hdrSrvHandleGPU = g_hdrSrvHandleGPU;
	auto& postProcessRootSignature = g_postProcessRootSignature;
	auto& bloomSrvHandlesGPU = g_bloomSrvHandlesGPU;
	auto& postProcessRenderTarget = g_postProcessRenderTarget;
	auto& postProcessRtvHandle = g_postProcessRtvHandle;
	auto& postProcessSrvHandleGPU = g_postProcessSrvHandleGPU;
	auto& fxaaPipelineState = g_fxaaPipelineState;
	auto& passthroughPipelineState = g_passthroughPipelineState;
	auto& dofPipelineState = g_depthOfFieldPipelineState;
	auto& motionBlurPipelineState = g_motionBlurPipelineState;
	auto& weightedOitCompositePipelineState = g_weightedOitCompositePipelineState;
	auto& underwaterCausticsPipelineState = g_underwaterCausticsPipelineState;
	auto& ssaoRenderTargets = g_ssaoRenderTargets;
	auto& ssaoRtvHandles = g_ssaoRtvHandles;
	auto& ssaoSrvHandlesGPU = g_ssaoSrvHandlesGPU;
	auto& ssaoPipelineState = g_ssaoPipelineState;
	auto& ssaoBlurPipelineState = g_ssaoBlurPipelineState;
	auto& ssgiPipelineState = g_ssgiPipelineState;
	auto& hdrCompositeRenderTarget = g_hdrCompositeRenderTarget;
	auto& hdrCompositeRtvHandle = g_hdrCompositeRtvHandle;
	auto& hdrCompositeSrvHandleGPU = g_hdrCompositeSrvHandleGPU;
	auto& skyboxPipelineState = g_skyboxPipelineState;
	auto& planarReflectionPipelineState = g_planarReflectionPipelineState;
	auto& sharpenPipelineState = g_sharpenPipelineState;
	auto& finalCompositePipelineState = g_finalCompositePipelineState;
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
	auto& colorGradingLutSrvHandleGPU = g_colorGradingLutSrvHandleGPU;
	auto& colorGradingLutSrvHandleCPU = g_colorGradingLutSrvHandleCPU;
	auto& identityColorGradingLut = g_colorGradingLut;
	auto& customColorGradingLutResource = g_customColorGradingLutResource;
	auto& customColorGradingLutUploadResource = g_customColorGradingLutUploadResource;
	auto& loadedColorGradingLutAssetPath = g_loadedColorGradingLutAssetPath;
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
	renderProfile.sceneObjectCount = static_cast<uint32_t>((std::min)(
		editorSceneObjects.size(),
		static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
	std::uint64_t profiledInstanceCount = 0u;

	for (const EditorSceneObject& sceneObject : editorSceneObjects) {
		profiledInstanceCount += sceneObject.surface.mode == 2
			? static_cast<std::uint64_t>((std::max)(sceneObject.surface.instanceCount, 1u))
			: 1u;
	}

	renderProfile.instanceCount = static_cast<uint32_t>((std::min)(
		profiledInstanceCount,
		static_cast<std::uint64_t>((std::numeric_limits<uint32_t>::max)())));

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

	int32_t lightCount = CollectSceneLights(directionalLightData);
	// Environment は背景画像の読み込み判定より前に反映し、Skybox と PBR が同じ有効状態を使う。
	ApplyEnvironmentComponent(directionalLightData);

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
		emissiveLightData->lights[emissiveIndex].position = {
			emissiveObject.worldMatrix.matrix[3][0],
			emissiveObject.worldMatrix.matrix[3][1],
			emissiveObject.worldMatrix.matrix[3][2]};
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
	const Vector3 activeCameraPosition = g_isSceneViewVisible
		? cameraTransform.translate
		: g_gameCameraPosition;

	for (int32_t lightIndex = 0; lightIndex < kMaxShadowLights; lightIndex++) {
		directionalLightData[lightIndex].cameraPosition = activeCameraPosition;
	}
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
	const PostProcessSettings ppSettings = GetPostProcessSettings();
	const bool shouldApplyTemporalJitter =
		ppSettings.hasPostProcessComponent &&
		(g_isSceneViewVisible || g_isGameViewVisible) &&
		ppSettings.aaMode == 3;
	Matrix4x4 sceneRenderProjectionMatrix = projectionMatrix;
	Matrix4x4 gameRenderProjectionMatrix = g_gameProjectionMatrix;

	if (shouldApplyTemporalJitter) {
		if (g_isSceneViewVisible) {
			sceneRenderProjectionMatrix = ApplyTemporalProjectionJitter(
				projectionMatrix,
				MakeTemporalJitterNdc(
					temporalJitterFrameIndex,
					g_editorSceneWidth,
					g_editorSceneHeight));
		}

		if (g_isGameViewVisible) {
			gameRenderProjectionMatrix = ApplyTemporalProjectionJitter(
				g_gameProjectionMatrix,
				MakeTemporalJitterNdc(
					temporalJitterFrameIndex,
					g_editorGameWidth,
					g_editorGameHeight));
		}

		temporalJitterFrameIndex = (temporalJitterFrameIndex + 1u) % 16u;
	}
	else {
		temporalJitterFrameIndex = 0u;
	}

	Matrix4x4 sceneViewProjectionMatrix = Multiply(viewMatrix, sceneRenderProjectionMatrix);
	Matrix4x4 inverseViewProjectionMatrix = Inverse(sceneViewProjectionMatrix);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, sceneViewProjectionMatrix);
	// worldViewProjectionMatrix 邵�E�E�E�・�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�繝ｻSceneView 邵�E�E�E�・�E�E�E�隰壼供�E�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・・WVP邵�E�E�E�繝ｻ
	Matrix4x4 gameViewProjectionMatrix = Multiply(g_gameViewMatrix, gameRenderProjectionMatrix);
	Matrix4x4 inverseGameViewProjectionMatrix = Inverse(gameViewProjectionMatrix);

	//================================================================
	// 描画機能ごとの必要リソース判定
	//================================================================
	const bool shouldRenderAmbientOcclusion =
		ppSettings.hasPostProcessComponent &&
		ppSettings.compositeAmbientOcclusionStrength > 0.0f;
	const bool shouldRenderSsgi =
		ppSettings.hasPostProcessComponent &&
		ppSettings.compositeSsgiEnabled &&
		ppSettings.compositeSsgiIntensity > 0.0f;
	// 履歴テクスチャは共有するが、行列と履歴有効状態は Scene / Game の Viewport ごとに分離する。
	const bool shouldExecuteTemporalOrSsr =
		ppSettings.hasPostProcessComponent &&
		(ppSettings.aaMode == 3 || ppSettings.ssrEnabled);

	size_t gpuCullingCandidateCount = 0u;

	for (const EditorSceneObject& sceneObject : editorSceneObjects) {
		if (sceneObject.type == EditorSceneObjectType::Model &&
			!sceneObject.ocean.isEnabled &&
			sceneObject.transformationData != nullptr) {
			gpuCullingCandidateCount++;
		}
	}

	// 少数オブジェクトでは全画面 Hi-Z 生成と readback の方が高コストになる。
	constexpr size_t kGpuCullingMinimumObjectCount = 256u;
	const bool shouldUseGpuCulling =
		gpuCullingCandidateCount >= kGpuCullingMinimumObjectCount;

	EditorPlanarReflectionManager planarManager;
	planarManager.CollectProbes(g_editorScene, editorSceneObjects);
	planarManager.UpdateCameras(
		cameraMatrix,
		viewMatrix,
		projectionMatrix,
		g_gameCameraMatrix,
		g_gameViewMatrix,
		g_gameProjectionMatrix);

	auto& planarViews = planarManager.GetViews();
	const EditorPlanarReflectionManager::ProbeView* scenePlanarView =
		planarManager.FindNearestView(cameraTransform.translate);
	const EditorPlanarReflectionManager::ProbeView* gamePlanarView =
		planarManager.FindNearestView(g_gameCameraPosition);
	const bool shouldRenderMaterialMask =
		planarManager.HasCompositeProbes() || shouldExecuteTemporalOrSsr;
	const bool shouldRenderGBuffer =
		shouldRenderAmbientOcclusion || shouldRenderSsgi || shouldExecuteTemporalOrSsr;
	const bool shouldUpdateGpuParticles =
		g_editorRuntimeManager.IsPlaying() &&
		g_editorRuntimeManager.GetEffectManager().HasLiveGpuParticles();
	const bool shouldBuildDepthHierarchy =
		shouldUseGpuCulling || shouldExecuteTemporalOrSsr || shouldUpdateGpuParticles;
	bool hasPlanarReflectionCapture = false;
	bool hasPlanarReflectionComposite = false;

	//================================================================
	// Sun CSM とローカルライト影の Atlas 配置
	//================================================================

	constexpr uint32_t kSunCascadeCount = 4u;
	constexpr uint32_t kMaxShadowRenderPassCount =
		kSunCascadeCount + static_cast<uint32_t>(kMaxShadowLights) - 1u;
	struct ShadowRenderPass {
		Matrix4x4 viewProjection;
		uint32_t tileIndex;
		int32_t lightIndex;
	};
	std::array<ShadowRenderPass, kMaxShadowRenderPassCount> shadowRenderPasses{};
	uint32_t shadowRenderPassCount = 0u;
	std::array<Matrix4x4, kMaxShadowLights> lightViewProjectionMatrixPerLight{};

	for (Matrix4x4& lightViewProjectionMatrix : lightViewProjectionMatrixPerLight) {
		lightViewProjectionMatrix = MakeIdentity4x4();
	}

	int32_t sunLightIndex = -1;

	for (int32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
		if (directionalLightData[lightIndex].lightType == 0) {
			sunLightIndex = lightIndex;
			break;
		}
	}

	const Matrix4x4& activeCameraMatrix = g_isSceneViewVisible
		? cameraMatrix
		: g_gameCameraMatrix;
	const Matrix4x4& activeCameraProjectionMatrix = g_isSceneViewVisible
		? projectionMatrix
		: g_gameProjectionMatrix;
	Vector3 activeCameraForward = Normalize(Vector3{
		activeCameraMatrix.matrix[2][0],
		activeCameraMatrix.matrix[2][1],
		activeCameraMatrix.matrix[2][2]});

	if (Length(activeCameraForward) <= 0.0001f) {
		activeCameraForward = {0.0f, 0.0f, 1.0f};
	}

	const float cascadeNearClip = (std::max)(ppSettings.cameraNearClip, 0.05f);
	const float cascadeFarClip = (std::clamp)(
		ppSettings.cameraFarClip,
		cascadeNearClip + 1.0f,
		240.0f);
	std::array<float, kSunCascadeCount> cascadeSplits{};
	constexpr float kCascadeLogarithmicWeight = 0.68f;

	for (uint32_t cascadeIndex = 0u; cascadeIndex < kSunCascadeCount; cascadeIndex++) {
		const float cascadeRatio =
			static_cast<float>(cascadeIndex + 1u) /
			static_cast<float>(kSunCascadeCount);
		const float logarithmicSplit = cascadeNearClip * std::pow(
			cascadeFarClip / cascadeNearClip,
			cascadeRatio);
		const float uniformSplit = cascadeNearClip +
			(cascadeFarClip - cascadeNearClip) * cascadeRatio;
		cascadeSplits[cascadeIndex] =
			logarithmicSplit * kCascadeLogarithmicWeight +
			uniformSplit * (1.0f - kCascadeLogarithmicWeight);
	}

	const float tileScale = 1.0f / static_cast<float>(kShadowAtlasTiles);
	uint32_t nextLocalShadowTileIndex = sunLightIndex >= 0 ? kSunCascadeCount : 0u;

	for (int32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
		DirectionalLight& light = directionalLightData[lightIndex];
		light.shadowCascadeSplits = {};
		light.shadowCascadeCount = 0.0f;
		light.shadowCascadeVP.fill({});
		light.shadowCascadeAtlas.fill({});

		if (lightIndex == sunLightIndex) {
			float previousCascadeSplit = cascadeNearClip;

			for (uint32_t cascadeIndex = 0u;
				cascadeIndex < kSunCascadeCount;
				cascadeIndex++) {
				const Matrix4x4 cascadeViewProjection = MakeSunCascadeViewProjectionMatrix(
					light,
					activeCameraPosition,
					activeCameraForward,
					activeCameraProjectionMatrix,
					previousCascadeSplit,
					cascadeSplits[cascadeIndex]);
				const uint32_t tileIndex = cascadeIndex;
				const uint32_t tileX = tileIndex % static_cast<uint32_t>(kShadowAtlasTiles);
				const uint32_t tileY = tileIndex / static_cast<uint32_t>(kShadowAtlasTiles);
				const Vector4 atlasTransform = {
					tileScale,
					tileScale,
					static_cast<float>(tileX) * tileScale,
					static_cast<float>(tileY) * tileScale
				};
				light.shadowCascadeVP[cascadeIndex] = cascadeViewProjection;
				light.shadowCascadeAtlas[cascadeIndex] = atlasTransform;
				shadowRenderPasses[shadowRenderPassCount] = {
					cascadeViewProjection,
					tileIndex,
					lightIndex
				};
				shadowRenderPassCount++;
				previousCascadeSplit = cascadeSplits[cascadeIndex];
			}

			light.shadowCascadeSplits = {
				cascadeSplits[0],
				cascadeSplits[1],
				cascadeSplits[2],
				cascadeSplits[3]
			};
			light.shadowCascadeCount = static_cast<float>(kSunCascadeCount);
			light.shadowVP = light.shadowCascadeVP[0];
			light.shadowTileIndex = 0.0f;
			light.shadowTileUvScaleX = light.shadowCascadeAtlas[0].x;
			light.shadowTileUvScaleY = light.shadowCascadeAtlas[0].y;
			light.shadowTileUvBiasX = light.shadowCascadeAtlas[0].z;
			light.shadowTileUvBiasY = light.shadowCascadeAtlas[0].w;
			lightViewProjectionMatrixPerLight[lightIndex] = light.shadowVP;
			continue;
		}

		const Matrix4x4 lightViewProjection = MakeLightViewProjectionMatrix(
			editorSceneObjects,
			transform,
			&light,
			isLegacyPreviewVisible);
		const uint32_t tileIndex = nextLocalShadowTileIndex;
		const uint32_t tileX = tileIndex % static_cast<uint32_t>(kShadowAtlasTiles);
		const uint32_t tileY = tileIndex / static_cast<uint32_t>(kShadowAtlasTiles);
		light.shadowVP = lightViewProjection;
		light.shadowTileIndex = static_cast<float>(tileIndex);
		light.shadowTileUvScaleX = tileScale;
		light.shadowTileUvScaleY = tileScale;
		light.shadowTileUvBiasX = static_cast<float>(tileX) * tileScale;
		light.shadowTileUvBiasY = static_cast<float>(tileY) * tileScale;
		lightViewProjectionMatrixPerLight[lightIndex] = lightViewProjection;
		shadowRenderPasses[shadowRenderPassCount] = {
			lightViewProjection,
			tileIndex,
			lightIndex
		};
		shadowRenderPassCount++;
		nextLocalShadowTileIndex++;
	}

	//================================================================
	// シャドウマップの更新判定
	//================================================================
	constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
	std::uint64_t shadowStateHash = kFnvOffsetBasis;
	AppendHashBytes(shadowStateHash, &lightCount, sizeof(lightCount));
	AppendHashBytes(shadowStateHash, &shadowRenderPassCount, sizeof(shadowRenderPassCount));

	for (uint32_t shadowPassIndex = 0u;
		shadowPassIndex < shadowRenderPassCount;
		shadowPassIndex++) {
		AppendHashBytes(
			shadowStateHash,
			&shadowRenderPasses[shadowPassIndex].viewProjection,
			sizeof(Matrix4x4));
		AppendHashBytes(
			shadowStateHash,
			&shadowRenderPasses[shadowPassIndex].tileIndex,
			sizeof(uint32_t));
	}

	for (const EditorSceneObject& sceneObject : editorSceneObjects) {
		if (sceneObject.type != EditorSceneObjectType::Model ||
			sceneObject.ocean.isEnabled ||
			sceneObject.transformationResource == nullptr ||
			sceneObject.transformationData == nullptr) {
			continue;
		}

		AppendHashBytes(shadowStateHash, &sceneObject.gameObjectId, sizeof(sceneObject.gameObjectId));
		const Matrix4x4 shadowCasterWorldMatrix = sceneObject.worldMatrix;
		AppendHashBytes(
			shadowStateHash,
			&shadowCasterWorldMatrix,
			sizeof(shadowCasterWorldMatrix));

		if (sceneObject.usesCustomMesh &&
			sceneObject.customMeshVertexResource != nullptr &&
			sceneObject.customMeshVertexCount > 0u) {
			AppendHashBytes(
				shadowStateHash,
				&sceneObject.customMeshVertexBufferView,
				sizeof(D3D12_VERTEX_BUFFER_VIEW));
			AppendHashBytes(
				shadowStateHash,
				&sceneObject.customMeshVertexCount,
				sizeof(sceneObject.customMeshVertexCount));

			if (sceneObject.customMeshIndexResource != nullptr &&
				sceneObject.customMeshIndexCount > 0u) {
				AppendHashBytes(
					shadowStateHash,
					&sceneObject.customMeshIndexBufferView,
					sizeof(D3D12_INDEX_BUFFER_VIEW));
				AppendHashBytes(
					shadowStateHash,
					&sceneObject.customMeshIndexCount,
					sizeof(sceneObject.customMeshIndexCount));
			}
		}
		else {
			size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);

			if (meshTypeIndex >= kEditorModelMeshTypeCount ||
				primitiveVertexCounts[meshTypeIndex] == 0u) {
				meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
			}

			AppendHashBytes(shadowStateHash, &meshTypeIndex, sizeof(meshTypeIndex));
			AppendHashBytes(
				shadowStateHash,
				&primitiveVertexBufferViews[meshTypeIndex],
				sizeof(D3D12_VERTEX_BUFFER_VIEW));
			AppendHashBytes(
				shadowStateHash,
				&primitiveVertexCounts[meshTypeIndex],
				sizeof(primitiveVertexCounts[meshTypeIndex]));
		}
	}

	AppendHashBytes(
		shadowStateHash,
		&isLegacyPreviewVisible,
		sizeof(isLegacyPreviewVisible));

	if (isLegacyPreviewVisible) {
		AppendHashBytes(shadowStateHash, &worldMatrix, sizeof(worldMatrix));
		AppendHashBytes(
			shadowStateHash,
			&modelVertexBufferView,
			sizeof(D3D12_VERTEX_BUFFER_VIEW));
	}

	const bool shouldRenderShadowMap =
		!hasSubmittedShadowMap || shadowStateHash != submittedShadowStateHash;
	bool hasRecordedShadowMapUpdate = false;

	Matrix4x4 uvTransformMatrix = MakeAffineMatrix(uvTransform.scale, uvTransform.rotate, uvTransform.translate);
	// uvTransformMatrix 邵�E�E�E�・�E�E�E� Material 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�繝ｻUV 陞溽判驪�E�E�E�髯�E�E�E�謔溘�E邵�E�E�E�繝ｻ
	spriteTransformationMatrixData->previousWVP = spriteTransformationMatrixData->WVP;
	spriteTransformationMatrixData->WVP = spriteWorldViewProjectionMatrix;
	// 隴鯉ｽ�E�E�E�郢晏干�E�E�E�樒ｹ晁侭�E�E�E�礼�E�E�E�晢�E�E�E��E�E�E�騾匁E�E��E��E�E�E�邵�E�E�E�・�E�E�E�陞ｳ螢�E�E�E�辟夂ｹ晁E��E繝｣郢晁E�E��E�斐＜邵�E�E�E�・�E�E�E�闔�E�E�E�E��E�E�E�繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�髯�E�E�E�謔溘�E郢�E�E�E�蜻亥�E�E�E�檎ｸ�E�E�E�蟠趣�E�E�E��E�E�E�・�E�E�E�郢�E�E�E��E�E�E�邵�E�E�E�繝ｻ
	spriteTransformationMatrixData->World = spriteWorldMatrix;
	spriteTransformationMatrixData->lightWVP = Multiply(spriteWorldMatrix, lightViewProjectionMatrixPerLight[0]);
	sphereTransformationMatrixData->previousWVP = sphereTransformationMatrixData->WVP;
	sphereTransformationMatrixData->WVP = worldViewProjectionMatrix;
	sphereTransformationMatrixData->World = worldMatrix;
	sphereTransformationMatrixData->lightWVP = Multiply(worldMatrix, lightViewProjectionMatrixPerLight[0]);
	spriteTransformationMatrixData->oceanParams0 = {};
	spriteTransformationMatrixData->oceanParams1 = {};
	spriteTransformationMatrixData->oceanParams2 = {};
	spriteTransformationMatrixData->oceanParams3 = {};
	spriteTransformationMatrixData->oceanParams4 = {};
	spriteTransformationMatrixData->oceanParams5 = {};
	spriteTransformationMatrixData->surfaceParams0 = {};
	spriteTransformationMatrixData->surfaceParams1 = {};
	spriteTransformationMatrixData->temporalParams = {};
	sphereTransformationMatrixData->oceanParams0 = {};
	sphereTransformationMatrixData->oceanParams1 = {};
	sphereTransformationMatrixData->oceanParams2 = {};
	sphereTransformationMatrixData->oceanParams3 = {};
	sphereTransformationMatrixData->oceanParams4 = {};
	sphereTransformationMatrixData->oceanParams5 = {};
	sphereTransformationMatrixData->surfaceParams0 = {};
	sphereTransformationMatrixData->surfaceParams1 = {};
	sphereTransformationMatrixData->temporalParams = {};

	spriteMaterialData->uvTransform = uvTransformMatrix;
	// Sprite 邵�E�E�E�・�E�E�E� 3D 郢晢�E�E�E��E�E�E�郢昴・�E�E�E�晉�E�E��E�E�E�・�E�E�E� Material 邵�E�E�E�・�E�E�E�陷�E�E�E�蠕個ｧ UV 陞溽判驪�E�E�E�郢�E�E�E�雋樊ｸ夊ｭ擾�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	sphereMaterialData->uvTransform = uvTransformMatrix;
	spriteMaterialData->oceanEnabled = 0.0f;
	sphereMaterialData->oceanEnabled = 0.0f;

	auto updateSceneObjectMatrices = [&](
			const Matrix4x4& targetViewProjectionMatrix,
			bool isGameViewTarget,
			const Vector3& targetCameraPosition,
			bool updateMotionHistory,
			bool reflectionClipEnabled = false,
			Vector4 reflectionClipPlane = {0.0f, 0.0f, 0.0f, 0.0f}) {
		for (EditorSceneObject& sceneObject : editorSceneObjects) {
			const Matrix4x4 sceneObjectWorldMatrix = sceneObject.worldMatrix;

			const Matrix4x4 sceneObjectProjectionMatrix =
				sceneObject.type == EditorSceneObjectType::Sprite
					? spriteProjectionMatrix
					: targetViewProjectionMatrix;
			TransformationMatrix* targetTransformationData = isGameViewTarget
				? sceneObject.gameTransformationData
				: sceneObject.transformationData;

			if (targetTransformationData != nullptr) {
				if (updateMotionHistory) {
					targetTransformationData->previousWVP = targetTransformationData->WVP;
					targetTransformationData->temporalParams.y =
						targetTransformationData->temporalParams.x;
				}

				targetTransformationData->WVP = Multiply(
					sceneObjectWorldMatrix,
					sceneObjectProjectionMatrix);
				targetTransformationData->World = sceneObjectWorldMatrix;
				targetTransformationData->lightWVP = Multiply(
					sceneObjectWorldMatrix,
					lightViewProjectionMatrixPerLight[0]);
				targetTransformationData->reflectionClipPlane = reflectionClipPlane;
				targetTransformationData->reflectionClipParams = {
					reflectionClipEnabled ? 1.0f : 0.0f,
					0.0f,
					0.0f,
					0.0f
				};
				const float targetViewportWidth = isGameViewTarget
					? g_editorGameWidth
					: g_editorSceneWidth;
				const float targetViewportHeight = isGameViewTarget
					? g_editorGameHeight
					: g_editorSceneHeight;
				targetTransformationData->temporalParams.x = oceanElapsedTime;
				targetTransformationData->temporalParams.z =
					targetViewportWidth /
					static_cast<float>((std::max)(g_renderWidth, 1u));
				targetTransformationData->temporalParams.w =
					targetViewportHeight /
					static_cast<float>((std::max)(g_renderHeight, 1u));
				targetTransformationData->surfaceParams0 = {
					static_cast<float>(sceneObject.surface.mode),
					oceanElapsedTime * sceneObject.surface.timeScale,
					sceneObject.surface.windStrength,
					sceneObject.surface.windSpeed};
				targetTransformationData->surfaceParams1 = {
					sceneObject.surface.windDirection.x,
					sceneObject.surface.windDirection.y,
					sceneObject.surface.windSpatialScale,
					sceneObject.surface.hasHeightOrDensityMap ? 1.0f : 0.0f};
				targetTransformationData->oceanRenderParams = sceneObject.ocean.isEnabled
					? Vector4{
						sceneObject.ocean.gpuTessellationEnabled ? 1.0f : 0.0f,
						sceneObject.ocean.tessellationTargetPixels,
						sceneObject.ocean.tessellationMaximumFactor,
						targetViewportHeight}
					: Vector4{};

				if (sceneObject.surface.mode == 1) {
					targetTransformationData->oceanParams4 = {
						sceneObject.surface.hasHeightOrDensityMap ? 1.0f : 0.0f,
						sceneObject.surface.heightScale,
						sceneObject.surface.areaSize.x,
						sceneObject.surface.areaSize.y};
				}
				else if (sceneObject.surface.mode == 2) {
					targetTransformationData->oceanParams4 = {
						sceneObject.surface.density,
						sceneObject.surface.lodDistance,
						sceneObject.surface.areaSize.x,
						sceneObject.surface.areaSize.y};
				}

				if (sceneObject.ocean.isEnabled || sceneObject.surface.mode != 0) {
					// 波の不変値は同期時だけ書き、描画パスごとに時刻と LOD 中心だけ更新する。
					const Matrix4x4 inverseWorldMatrix = Inverse(sceneObjectWorldMatrix);
					const Vector3 localCameraPosition =
						Transform(targetCameraPosition, inverseWorldMatrix);

					if (sceneObject.ocean.isEnabled) {
						targetTransformationData->oceanParams0.y = oceanElapsedTime;
						const float finestCellSize = (std::max)(
							sceneObject.ocean.size /
								static_cast<float>((std::max)(sceneObject.ocean.gridResolution, 16)),
							0.0001f);
						targetTransformationData->oceanParams5.z =
							std::round(localCameraPosition.x / finestCellSize) * finestCellSize;
						targetTransformationData->oceanParams5.w =
							std::round(localCameraPosition.z / finestCellSize) * finestCellSize;
					}
					else {
						targetTransformationData->oceanParams5.z = localCameraPosition.x;
						targetTransformationData->oceanParams5.w = localCameraPosition.z;
					}
				}
			}

		}
	};

	updateSceneObjectMatrices(
		sceneViewProjectionMatrix,
		false,
		cameraTransform.translate,
		true);
	updateSceneObjectMatrices(
		gameViewProjectionMatrix,
		true,
		g_gameCameraPosition,
		true);

	hr = commandAllocator->Reset();
	// CommandAllocator / CommandList 郢�E�E�E�蜑�E�E�E�E��E�E�E�鄙ｫ繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E髫�E�E�E�蛟ｬ鮖ｸ騾匁E�E��E��E�E�E�邵�E�E�E�・�E�E�E� Reset 邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	if (FAILED(hr)) {
		Log(g_logStream, std::format("CommandAllocator Reset failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		g_isDrawRequested = false;
		return;
	}

	hr = commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get());
	if (FAILED(hr)) {
		Log(g_logStream, std::format("CommandList Reset failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		g_isDrawRequested = false;
		return;
	}

	if (renderTimestampQueryHeap != nullptr && renderTimestampReadback != nullptr) {
		commandList->EndQuery(
			renderTimestampQueryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			0u);
	}

	//================================================================
	// Ocean FFT 更新
	// Scene に Ocean がある時だけGPU波面を更新し、同一設定の海面へ共有する。
	//================================================================

	EditorSceneObject* primaryOceanSceneObject = nullptr;
	struct OceanInteractionSource {
		Vector3 worldPosition{};
		float amplitude = 0.0f;
		float radius = 0.0f;
		int32_t oceanGameObjectId = -1;
	};
	std::array<OceanInteractionSource, 32u> oceanInteractionSources{};
	uint32_t oceanInteractionCount = 0u;

	for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		if (!gameObject.isActive || oceanInteractionCount >= oceanInteractionSources.size()) {
			continue;
		}

		const EditorComponent* wakeComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::SurfaceWakeEmitter);

		if (wakeComponent == nullptr ||
			!wakeComponent->isActive ||
			!wakeComponent->surfaceWakeAffectOceanSurface ||
			wakeComponent->surfaceWakeCurrentIntensity <= 0.0001f) {
			continue;
		}

		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};

		if (!g_editorScene.GetWorldTransform(
			gameObject.id,
			worldScale,
			worldRotation,
			worldPosition)) {
			continue;
		}

		OceanInteractionSource& interactionSource = oceanInteractionSources[oceanInteractionCount];
		interactionSource.worldPosition = worldPosition;
		interactionSource.amplitude =
			wakeComponent->surfaceWakeCurrentIntensity *
			(std::max)(wakeComponent->surfaceWakeWidth, 0.1f) *
			(std::max)(wakeComponent->surfaceWakeWaveAmplitudeScale, 0.0f);
		interactionSource.radius = (std::max)(
			wakeComponent->surfaceWakeWidth * wakeComponent->surfaceWakeWaveRadiusScale,
			0.5f);
		interactionSource.oceanGameObjectId = wakeComponent->surfaceWakeOceanGameObjectId;
		oceanInteractionCount++;
	}

	for (EditorSceneObject& sceneObject : editorSceneObjects) {
		if (!sceneObject.ocean.isEnabled) {
			continue;
		}

		if (sceneObject.transformationData != nullptr) {
			sceneObject.transformationData->oceanParams0.x = 1.0f;
		}

		if (sceneObject.gameTransformationData != nullptr) {
			sceneObject.gameTransformationData->oceanParams0.x = 1.0f;
		}

		const Matrix4x4 inverseOceanWorld = sceneObject.transformationData != nullptr
			? Inverse(sceneObject.transformationData->World)
			: MakeIdentity4x4();
		Vector4 interactionParameters[2] = {};

		uint32_t selectedInteractionCount = 0u;

		for (uint32_t interactionIndex = 0u;
			interactionIndex < oceanInteractionCount &&
			selectedInteractionCount < 2u;
			interactionIndex++) {
			const OceanInteractionSource& interactionSource = oceanInteractionSources[interactionIndex];

			if (interactionSource.oceanGameObjectId >= 0 &&
				interactionSource.oceanGameObjectId != sceneObject.gameObjectId) {
				continue;
			}

			const Vector3 localInteractionPosition = Transform(
				interactionSource.worldPosition,
				inverseOceanWorld);
			interactionParameters[selectedInteractionCount] = {
				localInteractionPosition.x,
				localInteractionPosition.z,
				interactionSource.amplitude,
				interactionSource.radius};
			selectedInteractionCount++;
		}

		if (sceneObject.transformationData != nullptr) {
			sceneObject.transformationData->surfaceParams0 = interactionParameters[0];
			sceneObject.transformationData->surfaceParams1 = interactionParameters[1];
		}

		if (sceneObject.gameTransformationData != nullptr) {
			sceneObject.gameTransformationData->surfaceParams0 = interactionParameters[0];
			sceneObject.gameTransformationData->surfaceParams1 = interactionParameters[1];
		}

		if (primaryOceanSceneObject == nullptr) {
			primaryOceanSceneObject = &sceneObject;
		}
	}

	if (primaryOceanSceneObject != nullptr &&
		g_oceanFftManager.Execute(
			commandList.Get(),
			primaryOceanSceneObject->ocean,
			oceanElapsedTime)) {
		for (EditorSceneObject& sceneObject : editorSceneObjects) {
			g_oceanFftManager.ApplyToSceneObject(sceneObject);
		}
	}

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
	const float skyEnvironmentTextureEnabled =
		hasEnvironmentTexture ? userRequestedEnvironmentTexture : 0.0f;

	// 背景と PBR 反射は同じ環境画像を使う。読み込みに失敗した画像を有効扱いにせず、
	// Shader 側の Environment グラデーションへ確実にフォールバックさせる。
	directionalLightData->environmentTextureEnabled = skyEnvironmentTextureEnabled;

	auto restoreIdentityColorGradingLut = [&]() {
		if (identityColorGradingLut == nullptr) {
			return;
		}

		const D3D12_RESOURCE_DESC identityDescription = identityColorGradingLut->GetDesc();
		D3D12_SHADER_RESOURCE_VIEW_DESC identitySrvDescription{};
		identitySrvDescription.Format = identityDescription.Format;
		identitySrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		identitySrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		identitySrvDescription.Texture2D.MipLevels = identityDescription.MipLevels;
		device->CreateShaderResourceView(
			identityColorGradingLut,
			&identitySrvDescription,
			colorGradingLutSrvHandleCPU);
	};

	auto ensureColorGradingLut = [&]() {
		if (ppSettings.compositeColorLutAssetPath == loadedColorGradingLutAssetPath) {
			return;
		}

		if (customColorGradingLutUploadResource != nullptr) {
			customColorGradingLutUploadResource->Release();
			customColorGradingLutUploadResource = nullptr;
		}

		if (customColorGradingLutResource != nullptr) {
			customColorGradingLutResource->Release();
			customColorGradingLutResource = nullptr;
		}

		loadedColorGradingLutAssetPath.clear();
		restoreIdentityColorGradingLut();

		if (ppSettings.compositeColorLutAssetPath.empty() ||
			!std::filesystem::exists(ppSettings.compositeColorLutAssetPath)) {
			loadedColorGradingLutAssetPath = ppSettings.compositeColorLutAssetPath;
			return;
		}

		DirectX::ScratchImage lutImages = LoadTexture(ConvertString(ppSettings.compositeColorLutAssetPath));
		const DirectX::TexMetadata& lutMetadata = lutImages.GetMetadata();
		const bool isValidStrip =
			lutMetadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D &&
			lutMetadata.height >= 2u &&
			lutMetadata.width == lutMetadata.height * lutMetadata.height;

		if (!isValidStrip) {
			Log(g_logStream, "Color LUT ignored: 画像幅は高さ x 高さの2D strip形式である必要があります。");
			loadedColorGradingLutAssetPath = ppSettings.compositeColorLutAssetPath;
			return;
		}

		customColorGradingLutResource = CreateTextureResource(device.Get(), lutMetadata);
		customColorGradingLutUploadResource = UploadTextureData(
			device.Get(),
			commandList.Get(),
			customColorGradingLutResource,
			lutImages);
		D3D12_SHADER_RESOURCE_VIEW_DESC lutSrvDescription{};
		lutSrvDescription.Format = lutMetadata.format;
		lutSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		lutSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		lutSrvDescription.Texture2D.MipLevels = static_cast<UINT>(lutMetadata.mipLevels);
		device->CreateShaderResourceView(
			customColorGradingLutResource,
			&lutSrvDescription,
			colorGradingLutSrvHandleCPU);
		loadedColorGradingLutAssetPath = ppSettings.compositeColorLutAssetPath;
	};
	ensureColorGradingLut();

	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	// backBufferIndex 邵�E�E�E�・�E�E�E�闔蛾宦螻楢�E�E�E��E�E�E�蜀怜�E邵�E�E�E�蜷�E�E�E�・・SwapChain buffer 邵�E�E�E�・�E�E�E�騾�E�E�E�・�E�E�E�陷�E�E�E�・�E�E�E�邵�E�E�E�繝ｻ

	auto drawShadowObjects = [&]() {
		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type != EditorSceneObjectType::Model ||
				sceneObject.ocean.isEnabled ||
				sceneObject.transformationResource == nullptr ||
				(sceneObject.materialData != nullptr && sceneObject.materialData->alphaMode == 2)) {
				continue;
			}

			const bool isAlphaCutout =
				sceneObject.materialData != nullptr &&
				sceneObject.materialData->alphaMode == 1;
			const bool isDoubleSided =
				sceneObject.cullMode == 2 ||
				(sceneObject.materialData != nullptr && sceneObject.materialData->doubleSided != 0);

			if (isAlphaCutout && isDoubleSided) {
				commandList->SetPipelineState(alphaCutoutShadowCullNonePipelineState.Get());
			}
			else if (isAlphaCutout) {
				commandList->SetPipelineState(alphaCutoutShadowPipelineState.Get());
			}
			else if (isDoubleSided) {
				commandList->SetPipelineState(shadowCullNonePipelineState.Get());
			}
			else {
				commandList->SetPipelineState(shadowPipelineState.Get());
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
			const D3D12_GPU_DESCRIPTOR_HANDLE fallbackHeightHandle =
				sceneObject.customTextureSrvGpuHandle.ptr != 0u
				? sceneObject.customTextureSrvGpuHandle
				: textureSrvHandlesGPU[2];
			const D3D12_GPU_DESCRIPTOR_HANDLE heightHandle =
				sceneObject.materialTextureSrvGpuHandles[
					static_cast<size_t>(EditorMaterialTextureSlot::Height)].ptr != 0u
				? sceneObject.materialTextureSrvGpuHandles[
					static_cast<size_t>(EditorMaterialTextureSlot::Height)]
				: fallbackHeightHandle;
			commandList->SetGraphicsRootDescriptorTable(16, heightHandle);

			if (isAlphaCutout && sceneObject.materialResource != nullptr) {
				const D3D12_GPU_DESCRIPTOR_HANDLE baseColorHandle =
					sceneObject.customTextureSrvGpuHandle.ptr != 0u
					? sceneObject.customTextureSrvGpuHandle
					: textureSrvHandlesGPU[2];
				const D3D12_GPU_DESCRIPTOR_HANDLE opacityHandle =
					sceneObject.materialTextureSrvGpuHandles[
						static_cast<size_t>(EditorMaterialTextureSlot::Opacity)].ptr != 0u
					? sceneObject.materialTextureSrvGpuHandles[
						static_cast<size_t>(EditorMaterialTextureSlot::Opacity)]
					: baseColorHandle;

				commandList->SetGraphicsRootConstantBufferView(
					0,
					sceneObject.materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(3, baseColorHandle);
				commandList->SetGraphicsRootDescriptorTable(17, opacityHandle);
			}

			if (sceneObject.usesCustomMesh &&
				sceneObject.customMeshVertexResource != nullptr &&
				sceneObject.customMeshVertexCount > 0u) {
				DrawCustomSceneMesh(
					commandList.Get(),
					sceneObject,
					&cameraTransform.translate,
					true);
			}
			else {
				commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
				commandList->DrawInstanced(
					primitiveVertexCounts[meshTypeIndex],
					GetSceneObjectInstanceCount(sceneObject),
					0,
					0);
			}
		}

		if (isLegacyPreviewVisible) {
			commandList->SetPipelineState(shadowPipelineState.Get());
			commandList->SetGraphicsRootConstantBufferView(
				1,
				sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
		}
	};

	if (shouldRenderShadowMap &&
		shadowRenderPassCount > 0u &&
		shadowMapResource != nullptr &&
		shadowPipelineState != nullptr) {
		D3D12_RESOURCE_BARRIER shadowBarrier{};
		shadowBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		shadowBarrier.Transition.pResource = shadowMapResource;
		shadowBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		commandList->ResourceBarrier(1, &shadowBarrier);

		const float tileSize = static_cast<float>(kRuntimeShadowMapSize) / static_cast<float>(kShadowAtlasTiles);
		D3D12_VIEWPORT shadowViewport{};
		shadowViewport.Width = tileSize;
		shadowViewport.Height = tileSize;
		shadowViewport.MinDepth = 0.0f;
		shadowViewport.MaxDepth = 1.0f;

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		BindSceneObjectSkinningResources(commandList.Get(), nullptr);
		commandList->SetPipelineState(shadowPipelineState.Get());
		ID3D12DescriptorHeap* shadowDescriptorHeaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, shadowDescriptorHeaps);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		for (uint32_t shadowPassIndex = 0u;
			shadowPassIndex < shadowRenderPassCount;
			shadowPassIndex++) {
			const ShadowRenderPass& shadowRenderPass = shadowRenderPasses[shadowPassIndex];
			const uint32_t tileX =
				shadowRenderPass.tileIndex % static_cast<uint32_t>(kShadowAtlasTiles);
			const uint32_t tileY =
				shadowRenderPass.tileIndex / static_cast<uint32_t>(kShadowAtlasTiles);
			shadowViewport.TopLeftX = static_cast<float>(tileX) * tileSize;
			shadowViewport.TopLeftY = static_cast<float>(tileY) * tileSize;
			D3D12_RECT shadowScissorRect{
				static_cast<LONG>(shadowViewport.TopLeftX),
				static_cast<LONG>(shadowViewport.TopLeftY),
				static_cast<LONG>(shadowViewport.TopLeftX + tileSize),
				static_cast<LONG>(shadowViewport.TopLeftY + tileSize)
			};
			commandList->RSSetViewports(1, &shadowViewport);
			commandList->RSSetScissorRects(1, &shadowScissorRect);
			commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsvHandle);

			D3D12_RECT clearRects[] = {shadowScissorRect};
			commandList->ClearDepthStencilView(shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, clearRects);

			for (EditorSceneObject& sceneObject : editorSceneObjects) {
				if (sceneObject.transformationData != nullptr) {
					sceneObject.transformationData->lightWVP = Multiply(
						sceneObject.transformationData->World,
						shadowRenderPass.viewProjection);
				}
			}
			sphereTransformationMatrixData->lightWVP = Multiply(
				worldMatrix,
				shadowRenderPass.viewProjection);
			drawShadowObjects();
		}

		for (EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.transformationData != nullptr) {
				sceneObject.transformationData->lightWVP = Multiply(
					sceneObject.transformationData->World,
					lightViewProjectionMatrixPerLight[0]);
			}
		}
		sphereTransformationMatrixData->lightWVP = Multiply(worldMatrix, lightViewProjectionMatrixPerLight[0]);

		shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &shadowBarrier);
		hasRecordedShadowMapUpdate = true;
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
	BindSceneObjectSkinningResources(commandList.Get(), nullptr);
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

	D3D12_RESOURCE_BARRIER materialMaskBarrier{};
	if (shouldRenderMaterialMask && materialMaskRenderTarget != nullptr) {
		materialMaskBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		materialMaskBarrier.Transition.pResource = materialMaskRenderTarget;
		materialMaskBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		materialMaskBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		materialMaskBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &materialMaskBarrier);

		float materialMaskClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		commandList->ClearRenderTargetView(materialMaskRtvHandle, materialMaskClearColor, 0, nullptr);
	}

	struct SkyCloudSettings {
		float enabled = 0.0f;
		float coverage = 0.0f;
		float density = 0.0f;
		float scale = 0.001f;
		float speed = 0.0f;
		float height = 1000.0f;
		float thickness = 500.0f;
		float lightAbsorption = 1.0f;
		float silverLining = 0.0f;
		Vector3 color{1.0f, 1.0f, 1.0f};
	};
	SkyCloudSettings skyCloudSettings{};

	for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* environmentComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Environment);

		if (environmentComponent == nullptr || !environmentComponent->isActive) {
			continue;
		}

		skyCloudSettings.enabled = environmentComponent->volumetricCloudEnabled ? 1.0f : 0.0f;
		skyCloudSettings.coverage = (std::clamp)(environmentComponent->volumetricCloudCoverage, 0.0f, 1.0f);
		skyCloudSettings.density = (std::max)(environmentComponent->volumetricCloudDensity, 0.0f);
		skyCloudSettings.scale = (std::max)(environmentComponent->volumetricCloudScale, 0.0001f);
		skyCloudSettings.speed = environmentComponent->volumetricCloudSpeed;
		skyCloudSettings.height = environmentComponent->volumetricCloudHeight;
		skyCloudSettings.thickness = (std::max)(environmentComponent->volumetricCloudThickness, 1.0f);
		skyCloudSettings.lightAbsorption = (std::max)(
			environmentComponent->volumetricCloudLightAbsorption,
			0.0f);
		skyCloudSettings.silverLining = (std::max)(
			environmentComponent->volumetricCloudSilverLining,
			0.0f);
		skyCloudSettings.color = environmentComponent->volumetricCloudColor;
		break;
	}

	auto drawSkybox = [&](
			const D3D12_VIEWPORT& targetViewport,
			const D3D12_RECT& targetScissorRect,
			const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtvHandle,
			const Matrix4x4& targetInverseViewProjectionMatrix,
			const Vector3& targetCameraPosition) {
		if (skyboxPipelineState == nullptr) {
			return;
		}

		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(skyboxPipelineState.Get());
		commandList->RSSetViewports(1, &targetViewport);
		commandList->RSSetScissorRects(1, &targetScissorRect);
		commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, environmentSrvHandleGPU);
		float skyboxParams[48] = {};
		std::memcpy(
			&skyboxParams[0],
			&targetInverseViewProjectionMatrix.matrix[0][0],
			sizeof(float) * 16u);
		skyboxParams[16] = directionalLightData->skyUpperColor.x;
		skyboxParams[17] = directionalLightData->skyUpperColor.y;
		skyboxParams[18] = directionalLightData->skyUpperColor.z;
		skyboxParams[19] = (std::max)(directionalLightData->skyIntensity, 0.0f);
		skyboxParams[20] = directionalLightData->skyLowerColor.x;
		skyboxParams[21] = directionalLightData->skyLowerColor.y;
		skyboxParams[22] = directionalLightData->skyLowerColor.z;
		skyboxParams[23] = (std::max)(directionalLightData->horizonSharpness, 0.0001f);
		skyboxParams[24] = directionalLightData->direction.x;
		skyboxParams[25] = directionalLightData->direction.y;
		skyboxParams[26] = directionalLightData->direction.z;
		skyboxParams[27] = (std::max)(directionalLightData->skyEmission, 0.0f);
		skyboxParams[28] = skyEnvironmentTextureEnabled;
		skyboxParams[29] = directionalLightData->environmentTextureIntensity;
		skyboxParams[30] = directionalLightData->environmentTextureRotation;
		skyboxParams[31] = directionalLightData->environmentTextureMipBias;
		skyboxParams[32] = targetCameraPosition.x;
		skyboxParams[33] = targetCameraPosition.y;
		skyboxParams[34] = targetCameraPosition.z;
		skyboxParams[35] = oceanElapsedTime;
		skyboxParams[36] = skyCloudSettings.enabled;
		skyboxParams[37] = skyCloudSettings.coverage;
		skyboxParams[38] = skyCloudSettings.density;
		skyboxParams[39] = skyCloudSettings.scale;
		skyboxParams[40] = skyCloudSettings.speed;
		skyboxParams[41] = skyCloudSettings.height;
		skyboxParams[42] = skyCloudSettings.thickness;
		skyboxParams[43] = skyCloudSettings.lightAbsorption;
		skyboxParams[44] = skyCloudSettings.silverLining;
		skyboxParams[45] = skyCloudSettings.color.x;
		skyboxParams[46] = skyCloudSettings.color.y;
		skyboxParams[47] = skyCloudSettings.color.z;
		commandList->SetGraphicsRoot32BitConstants(2, 48, skyboxParams, 0);
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
	auto bindMaterialTextureHandles = [
		commandList,
		planarReflectionSrvHandleGPU](
		const EditorSceneObject& sceneObject,
		D3D12_GPU_DESCRIPTOR_HANDLE fallbackTextureHandle) {
		for (int32_t materialMapIndex = 0;
			 materialMapIndex < static_cast<int32_t>(EditorMaterialTextureSlot::Count);
			 materialMapIndex++) {
			const D3D12_GPU_DESCRIPTOR_HANDLE materialMapHandle =
				sceneObject.materialTextureSrvGpuHandles[static_cast<size_t>(materialMapIndex)].ptr != 0u
				? sceneObject.materialTextureSrvGpuHandles[static_cast<size_t>(materialMapIndex)]
				: fallbackTextureHandle;
			commandList->SetGraphicsRootDescriptorTable(11 + materialMapIndex, materialMapHandle);
		}

		const bool isOcean =
			sceneObject.ocean.isEnabled ||
			(sceneObject.materialData != nullptr && sceneObject.materialData->oceanEnabled >= 0.5f);

		if (isOcean && planarReflectionSrvHandleGPU.ptr != 0u) {
			// Ocean 専用 PS は未使用の Opacity Map slot(t13)を平面反射 Capture として読む。
			commandList->SetGraphicsRootDescriptorTable(17, planarReflectionSrvHandleGPU);
		}
	};
	const Matrix4x4* cullingOverrideViewProjectionMatrix = nullptr;

	enum class SceneObjectDrawFilter {
		All,
		Opaque,
		Water,
		Refractive,
		Transparent
	};

	auto drawSceneObjects = [&](
			bool isGameViewPass,
			const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtvHandle,
			const Vector3& targetCameraPosition,
			int32_t skipGameObjectId,
			int32_t planarSurfaceGameObjectId = -1,
			bool useWeightedOit = false,
			SceneObjectDrawFilter drawFilter = SceneObjectDrawFilter::All) {
		const bool usesDepthAttachment =
			drawFilter != SceneObjectDrawFilter::Refractive;

		if (usesDepthAttachment) {
			commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);
		}
		else {
			commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
		}

		g_oceanFftManager.BindGraphicsResources(commandList.Get());
		const bool isPlanarReflectionDraw = targetRtvHandle.ptr == planarReflectionRtvHandle.ptr;
		const bool useGpuCullingForPass = shouldUseGpuCulling &&
			!isPlanarReflectionDraw &&
			((!isGameViewPass && g_isSceneViewVisible) ||
			(isGameViewPass && !g_isSceneViewVisible && g_isGameViewVisible));
		const Matrix4x4& activeViewProjectionMatrix =
			cullingOverrideViewProjectionMatrix != nullptr
				? *cullingOverrideViewProjectionMatrix
				: (isGameViewPass ? gameViewProjectionMatrix : sceneViewProjectionMatrix);
		// 描画パスごとの並べ替え領域は容量を保持し、毎フレームの再確保を避ける。
		static std::vector<const EditorSceneObject*> orderedSceneObjects;
		static std::vector<const EditorSceneObject*> transparentSceneObjects;
		orderedSceneObjects.clear();
		transparentSceneObjects.clear();
		orderedSceneObjects.reserve(editorSceneObjects.size());
		transparentSceneObjects.reserve(editorSceneObjects.size());

		// 不透明・Masked を先に描き、Transparent は Alpha Blend が成立するよう遠方から並べる。
		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			const bool isOcean =
				sceneObject.ocean.isEnabled ||
				(sceneObject.materialData != nullptr && sceneObject.materialData->oceanEnabled >= 0.5f);
			const bool isTransparent =
				!isOcean &&
				sceneObject.materialData != nullptr &&
				sceneObject.materialData->alphaMode == 2;
			const bool isRefractive =
				isTransparent &&
				sceneObject.materialData->transmission > 0.0001f;
			const bool shouldDrawObject =
				drawFilter == SceneObjectDrawFilter::All ||
				(drawFilter == SceneObjectDrawFilter::Opaque && !isOcean && !isTransparent) ||
				(drawFilter == SceneObjectDrawFilter::Water && isOcean) ||
				(drawFilter == SceneObjectDrawFilter::Refractive && isRefractive) ||
				(drawFilter == SceneObjectDrawFilter::Transparent && isTransparent && !isRefractive);

			// 反射 Capture 中に別 Ocean が同じ反射 RT を読む再帰参照を防ぐ。
			if (!shouldDrawObject || (isPlanarReflectionDraw && isOcean)) {
				continue;
			}

			if (isTransparent) {
				transparentSceneObjects.push_back(&sceneObject);
			}
			else {
				orderedSceneObjects.push_back(&sceneObject);
			}
		}

		// 不透明物は PSO、Mesh、Texture の近い順へまとめ、描画結果を変えず状態変更を減らす。
		std::sort(
			orderedSceneObjects.begin(),
			orderedSceneObjects.end(),
			[](const EditorSceneObject* left, const EditorSceneObject* right) {
				const std::uintptr_t leftMeshResource = reinterpret_cast<std::uintptr_t>(
					left->customMeshVertexResource);
				const std::uintptr_t rightMeshResource = reinterpret_cast<std::uintptr_t>(
					right->customMeshVertexResource);
				const std::uintptr_t leftTextureHandle = left->customTextureSrvGpuHandle.ptr;
				const std::uintptr_t rightTextureHandle = right->customTextureSrvGpuHandle.ptr;

				if (left->type != right->type) {
					return left->type < right->type;
				}

				if (left->cullMode != right->cullMode) {
					return left->cullMode < right->cullMode;
				}

				if (left->usesCustomMesh != right->usesCustomMesh) {
					return left->usesCustomMesh < right->usesCustomMesh;
				}

				if (leftMeshResource != rightMeshResource) {
					return leftMeshResource < rightMeshResource;
				}

				if (left->meshType != right->meshType) {
					return left->meshType < right->meshType;
				}

				if (leftTextureHandle != rightTextureHandle) {
					return leftTextureHandle < rightTextureHandle;
				}

				return left->gameObjectId < right->gameObjectId;
			});

		std::sort(
			transparentSceneObjects.begin(),
			transparentSceneObjects.end(),
			[&targetCameraPosition](const EditorSceneObject* left, const EditorSceneObject* right) {
				const Vector3 leftOffset = Subtract(left->transform.translate, targetCameraPosition);
				const Vector3 rightOffset = Subtract(right->transform.translate, targetCameraPosition);
				return Dot(leftOffset, leftOffset) > Dot(rightOffset, rightOffset);
			});
		orderedSceneObjects.insert(
			orderedSceneObjects.end(),
			transparentSceneObjects.begin(),
			transparentSceneObjects.end());

		for (const EditorSceneObject* sceneObjectPointer : orderedSceneObjects) {
			const EditorSceneObject& sceneObject = *sceneObjectPointer;
			const bool isOcean =
				sceneObject.ocean.isEnabled ||
				(sceneObject.materialData != nullptr && sceneObject.materialData->oceanEnabled >= 0.5f);
			const bool isTransparent =
				!isOcean &&
				sceneObject.materialData != nullptr &&
				sceneObject.materialData->alphaMode == 2;
			const bool isRefractive =
				isTransparent &&
				sceneObject.materialData->transmission > 0.0001f;
			const bool isDedicatedRefractivePass =
				drawFilter == SceneObjectDrawFilter::Refractive &&
				isRefractive;
			if (skipGameObjectId >= 0 && sceneObject.gameObjectId == skipGameObjectId) {
				continue;
			}

			// Main Cameraと平面反射Cameraの両方で視錐台・Far Clip外を除外する。
			if (sceneObject.type == EditorSceneObjectType::Model &&
				!isOcean &&
				!IsSceneObjectInsideViewFrustum(sceneObject, activeViewProjectionMatrix)) {
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

			const bool hasGpuPredicate = useGpuCullingForPass &&
				!sceneObject.ocean.isEnabled &&
				g_gpuCullingManager.BeginPredication(commandList.Get(), sceneObject.gameObjectId);

			if (sceneObject.type == EditorSceneObjectType::Sprite) {
				// 平行投影で頂点の表裏が反転しても Sprite 全体が破棄されないよう、両面 PSO を使う。
				if (isOcean && waterSurfacePipelineState != nullptr) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
					commandList->SetPipelineState(waterSurfacePipelineState.Get());
				}
				else if (isDedicatedRefractivePass && refractiveSurfaceCullNonePipelineState != nullptr) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
					commandList->SetPipelineState(refractiveSurfaceCullNonePipelineState.Get());
				}
				else if (isTransparent && useWeightedOit && g_weightedOitCullNonePipelineState != nullptr) {
					commandList->OMSetRenderTargets(2, g_oitRtvHandles, FALSE, &dsvHandle);
					commandList->SetPipelineState(g_weightedOitCullNonePipelineState.Get());
				}
				else if (g_cullNonePipelineState != nullptr) {
					commandList->SetPipelineState(g_cullNonePipelineState.Get());
				}
				else {
					commandList->SetPipelineState(defaultDrawPso);
				}

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
				bindMaterialTextureHandles(sceneObject, textureHandle);
				commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);
				commandList->IASetIndexBuffer(&spriteIndexBufferView);
				commandList->DrawIndexedInstanced(_countof(spriteIndices), 1, 0, 0, 0);
			}
			else {
				const bool useOceanTessellation =
					isOcean &&
					sceneObject.ocean.gpuTessellationEnabled &&
					waterTessellationPipelineState != nullptr;
				size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);
				if (meshTypeIndex >= kEditorModelMeshTypeCount ||
					primitiveVertexCounts[meshTypeIndex] == 0u) {
					meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
				}

				if (useOceanTessellation) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);
					commandList->SetPipelineState(waterTessellationPipelineState.Get());
				}
				else if (isOcean && waterSurfacePipelineState != nullptr) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);
					commandList->SetPipelineState(waterSurfacePipelineState.Get());
				}
				else if (isDedicatedRefractivePass && sceneObject.cullMode == 2 &&
					refractiveSurfaceCullNonePipelineState != nullptr) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
					commandList->SetPipelineState(refractiveSurfaceCullNonePipelineState.Get());
				}
				else if (isDedicatedRefractivePass && refractiveSurfacePipelineState != nullptr) {
					commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
					commandList->SetPipelineState(refractiveSurfacePipelineState.Get());
				}
				else if (isTransparent && useWeightedOit && sceneObject.cullMode == 2 &&
					g_weightedOitCullNonePipelineState != nullptr) {
					commandList->OMSetRenderTargets(2, g_oitRtvHandles, FALSE, &dsvHandle);
					commandList->SetPipelineState(g_weightedOitCullNonePipelineState.Get());
				}
				else if (isTransparent && useWeightedOit && g_weightedOitPipelineState != nullptr) {
					commandList->OMSetRenderTargets(2, g_oitRtvHandles, FALSE, &dsvHandle);
					commandList->SetPipelineState(g_weightedOitPipelineState.Get());
				}
				else if (sceneObject.gameObjectId == planarSurfaceGameObjectId && planarSurfacePipelineState != nullptr) {
					commandList->SetPipelineState(planarSurfacePipelineState.Get());
				}
				else if (sceneObject.materialData != nullptr &&
					sceneObject.materialData->alphaMode == 2 &&
					sceneObject.cullMode == 2 &&
					g_transparentCullNonePipelineState != nullptr) {
					commandList->SetPipelineState(g_transparentCullNonePipelineState.Get());
				}
				else if (sceneObject.materialData != nullptr &&
					sceneObject.materialData->alphaMode == 2 &&
					g_transparentPipelineState != nullptr) {
					commandList->SetPipelineState(g_transparentPipelineState.Get());
				}
				else if (sceneObject.cullMode == 1 && g_cullFrontPipelineState != nullptr) {
					commandList->SetPipelineState(g_cullFrontPipelineState.Get());
				}
				else if (sceneObject.cullMode == 2 && g_cullNonePipelineState != nullptr) {
					commandList->SetPipelineState(g_cullNonePipelineState.Get());
				}
				else {
					commandList->SetPipelineState(defaultDrawPso);
				}

				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationResource->GetGPUVirtualAddress());

				if (useOceanTessellation) {
					commandList->SetGraphicsRootConstantBufferView(
						kOceanTessellationTransformRootParameter,
						transformationResource->GetGPUVirtualAddress());
					commandList->IASetPrimitiveTopology(
						D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
				}
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
				bindMaterialTextureHandles(sceneObject, textureHandle);
				if (sceneObject.usesCustomMesh &&
					sceneObject.customMeshVertexResource != nullptr &&
					sceneObject.customMeshVertexCount > 0u) {
					DrawCustomSceneMesh(
						commandList.Get(),
						sceneObject,
						&targetCameraPosition,
						false);
				}
				else {
					commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
					commandList->DrawInstanced(
						primitiveVertexCounts[meshTypeIndex],
						GetSceneObjectInstanceCount(sceneObject),
						0,
						0);
				}

				if (useOceanTessellation) {
					commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				}
			}

			if (hasGpuPredicate) {
				g_gpuCullingManager.EndPredication(commandList.Get());
			}
		}

		if (usesDepthAttachment) {
			commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);
		}
		else {
			commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
		}
	};

	auto drawReflectionMaskObjects = [&](bool isGameViewPass, int32_t planarMaskGameObjectId) {
		if (!shouldRenderMaterialMask ||
			materialMaskRenderTarget == nullptr ||
			objectReflectionMaskPipelineState == nullptr) {
			return;
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(objectReflectionMaskPipelineState.Get());
		g_oceanFftManager.BindGraphicsResources(commandList.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->OMSetRenderTargets(1, &materialMaskRtvHandle, FALSE, &dsvHandle);

		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type == EditorSceneObjectType::Sprite) {
				continue;
			}

			// 水面は専用パス内で正しい波面法線と不透明Depthを使ってSSRする。
			// 汎用SSRへ渡すと背後オブジェクトのDepth/Normalで二重反射されるため除外する。
			if (sceneObject.ocean.isEnabled ||
				(sceneObject.materialData != nullptr &&
				 sceneObject.materialData->oceanEnabled >= 0.5f)) {
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
			const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
				sceneObject.customTextureSrvGpuHandle.ptr != 0u
				? sceneObject.customTextureSrvGpuHandle
				: textureSrvHandlesGPU[2];
			commandList->SetGraphicsRootDescriptorTable(3, textureHandle);
			bindMaterialTextureHandles(sceneObject, textureHandle);

			if (sceneObject.usesCustomMesh &&
				sceneObject.customMeshVertexResource != nullptr &&
				sceneObject.customMeshVertexCount > 0u) {
				const Vector3& maskCameraPosition = isGameViewPass
					? g_gameCameraPosition
					: cameraTransform.translate;
				DrawCustomSceneMesh(
					commandList.Get(),
					sceneObject,
					&maskCameraPosition,
					false);
			}
			else {
				commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
				commandList->DrawInstanced(
					primitiveVertexCounts[meshTypeIndex],
					GetSceneObjectInstanceCount(sceneObject),
					0,
					0);
			}
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->OMSetRenderTargets(1, &hdrRtvHandle, FALSE, &dsvHandle);
	};

	auto drawEffects = [&](
		const Matrix4x4& targetViewProjectionMatrix,
		const Matrix4x4& targetViewMatrix,
		const Matrix4x4& targetProjectionMatrix,
		const Vector3& targetCameraPosition,
		const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtvHandle,
		const Matrix4x4& targetInverseViewProjection,
		const D3D12_VIEWPORT& targetSoftParticleViewport,
		bool canUseSoftParticleDepth) {
		if (!g_editorRuntimeManager.IsPlaying()) {
			g_gpuParticleManager.RequestReset();
			return;
		}

		commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);

		if (g_editorRuntimeManager.GetEffectManager().HasLiveGpuParticles()) {
			g_gpuParticleManager.Draw(commandList.Get(), targetViewProjectionMatrix, targetViewMatrix);
		}

		g_editorRuntimeManager.GetEffekseerManager().Draw(
			commandList.Get(),
			targetViewMatrix,
			targetProjectionMatrix,
			targetCameraPosition);

		// Stage1 VFX(Billboard / Flipbook / Ribbon / Ring)。EditorGpuParticleManagerとは独立したCPU管理・単純描画。
		{
			const Matrix4x4 vfxCameraMatrix = Inverse(targetViewMatrix);
			const Vector3 vfxCameraRight{vfxCameraMatrix.matrix[0][0], vfxCameraMatrix.matrix[0][1], vfxCameraMatrix.matrix[0][2]};
			const Vector3 vfxCameraUp{vfxCameraMatrix.matrix[1][0], vfxCameraMatrix.matrix[1][1], vfxCameraMatrix.matrix[1][2]};
			const Vector3 vfxCameraForward{vfxCameraMatrix.matrix[2][0], vfxCameraMatrix.matrix[2][1], vfxCameraMatrix.matrix[2][2]};
			static std::vector<EditorVfxRenderer::VfxBatch> vfxBatches;
			g_editorRuntimeManager.GetVfxManager().BuildDrawBatches(
				g_vfxRenderer,
				targetCameraPosition,
				vfxCameraRight,
				vfxCameraUp,
				vfxCameraForward,
				vfxBatches);

			// Soft Particle: Water Passが作るOpaque Depth Copyを再利用する(新規Depth資源は作らない)。
			// Planar Reflection等、Depth CopyのCameraと一致しないPassではcanUseSoftParticleDepth=falseにして無効化する。
			EditorVfxRenderer::SoftParticleViewContext softParticleViewContext{};
			softParticleViewContext.isSceneDepthValid = canUseSoftParticleDepth;
			softParticleViewContext.inverseViewProjection = targetInverseViewProjection;
			const float inverseSoftParticleRenderWidth = 1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			const float inverseSoftParticleRenderHeight = 1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			softParticleViewContext.viewportUvOffsetX = targetSoftParticleViewport.TopLeftX * inverseSoftParticleRenderWidth;
			softParticleViewContext.viewportUvOffsetY = targetSoftParticleViewport.TopLeftY * inverseSoftParticleRenderHeight;
			softParticleViewContext.viewportUvScaleX = targetSoftParticleViewport.Width * inverseSoftParticleRenderWidth;
			softParticleViewContext.viewportUvScaleY = targetSoftParticleViewport.Height * inverseSoftParticleRenderHeight;

			g_vfxRenderer.Draw(commandList.Get(), targetViewProjectionMatrix, vfxBatches, softParticleViewContext);
		}

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, &dsvHandle);
	};

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

	const float planarClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	const bool hasPlanarReflectionResources =
		planarManager.HasProbes() &&
		planarReflectionRenderTarget != nullptr &&
		planarScenePipelineState != nullptr &&
		planarReflectionPipelineState != nullptr &&
		materialMaskRenderTarget != nullptr &&
		(g_isSceneViewVisible || g_isGameViewVisible);
	std::uint64_t planarReflectionStateHash = 14695981039346656037ull;
	AppendHashBytes(
		planarReflectionStateHash,
		&g_isSceneViewVisible,
		sizeof(g_isSceneViewVisible));
	AppendHashBytes(
		planarReflectionStateHash,
		&g_isGameViewVisible,
		sizeof(g_isGameViewVisible));

	if (scenePlanarView != nullptr) {
		AppendHashBytes(
			planarReflectionStateHash,
			&scenePlanarView->sourceId,
			sizeof(scenePlanarView->sourceId));
		AppendHashBytes(
			planarReflectionStateHash,
			&scenePlanarView->sceneCam.viewProjection,
			sizeof(Matrix4x4));
	}

	if (gamePlanarView != nullptr) {
		AppendHashBytes(
			planarReflectionStateHash,
			&gamePlanarView->sourceId,
			sizeof(gamePlanarView->sourceId));
		AppendHashBytes(
			planarReflectionStateHash,
			&gamePlanarView->gameCam.viewProjection,
			sizeof(Matrix4x4));
	}

	AppendHashBytes(planarReflectionStateHash, &lightCount, sizeof(lightCount));
	AppendHashBytes(
		planarReflectionStateHash,
		&environmentSrvHandleGPU.ptr,
		sizeof(environmentSrvHandleGPU.ptr));
	AppendHashBytes(
		planarReflectionStateHash,
		&oceanElapsedTime,
		sizeof(oceanElapsedTime));

	for (int32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
		AppendHashBytes(
			planarReflectionStateHash,
			&directionalLightData[lightIndex],
			sizeof(DirectionalLight));
	}

	for (const EditorSceneObject& sceneObject : editorSceneObjects) {
		AppendHashBytes(
			planarReflectionStateHash,
			&sceneObject.gameObjectId,
			sizeof(sceneObject.gameObjectId));
		AppendHashBytes(
			planarReflectionStateHash,
			&sceneObject.worldMatrix,
			sizeof(Matrix4x4));

		if (sceneObject.materialData != nullptr) {
			AppendHashBytes(
				planarReflectionStateHash,
				sceneObject.materialData,
				sizeof(*sceneObject.materialData));
		}
	}

	if (!hasPlanarReflectionResources) {
		hasSubmittedPlanarReflection = false;
		submittedPlanarReflectionTarget = nullptr;
	}

	const bool isPlaying = g_editorRuntimeManager.IsPlaying();
	const bool isAutomaticOceanReflection =
		!planarViews.empty() && !planarManager.HasCompositeProbes();
	const bool shouldThrottleOceanReflection =
		isPlaying &&
		isAutomaticOceanReflection &&
		renderProfile.gpuFrameMilliseconds > 14.0f;
	const bool isOceanReflectionUpdateFrame =
		!shouldThrottleOceanReflection ||
		(oceanReflectionUpdateFrameIndex % 2u) == 0u;
	const bool hasPlanarReflectionTargetChanged =
		!hasSubmittedPlanarReflection ||
		submittedPlanarReflectionTarget != planarReflectionRenderTarget;
	const bool shouldRenderPlanarReflection =
		hasPlanarReflectionResources &&
		(hasPlanarReflectionTargetChanged ||
		 (!isPlaying && submittedPlanarReflectionStateHash != planarReflectionStateHash) ||
		 (isPlaying && isOceanReflectionUpdateFrame));

	if (isPlaying && isAutomaticOceanReflection) {
		oceanReflectionUpdateFrameIndex++;
	} else {
		oceanReflectionUpdateFrameIndex = 0u;
	}

	if (shouldRenderPlanarReflection) {

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

		//============================================================
		// 平面反射のキャプチャ
		//============================================================
		// 反射 RT は Viewport を分けて共有し、Scene / Game それぞれに最寄りの反射面を描画する。
		// 合成はメインシーンの色・深度・同じ反射面 ID のマスクが完成した後に行う。
		commandList->ClearRenderTargetView(planarReflectionRtvHandle, hdrClearColor, 0, nullptr);

		if (g_isSceneViewVisible && scenePlanarView != nullptr) {
			const EditorPlanarReflectionManager::ProbeView& planarView = *scenePlanarView;
			const PlanarReflectionCamera& reflectionCamera = planarView.sceneCam;
			const Vector3 savedCameraPosition = directionalLightData->cameraPosition;
			directionalLightData->cameraPosition = reflectionCamera.position;

			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissorRect);
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			drawSkybox(
				viewport,
				scissorRect,
				planarReflectionRtvHandle,
				reflectionCamera.inverseViewProjection,
				reflectionCamera.position);

			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(planarScenePipelineState.Get());
			commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
			commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			updateSceneObjectMatrices(
				reflectionCamera.viewProjection,
				false,
				reflectionCamera.position,
				false,
				true,
				reflectionCamera.clipPlane);
			defaultDrawPso = planarScenePipelineState.Get();
			cullingOverrideViewProjectionMatrix = &reflectionCamera.viewProjection;
			drawSceneObjects(
				false,
				planarReflectionRtvHandle,
				reflectionCamera.position,
				planarView.sourceId);
			cullingOverrideViewProjectionMatrix = nullptr;
			drawEffects(
				reflectionCamera.viewProjection,
				reflectionCamera.viewMatrix,
				reflectionCamera.projectionMatrix,
				reflectionCamera.position,
				planarReflectionRtvHandle,
				reflectionCamera.viewProjection,
				viewport,
				false);  // Planar ReflectionはOpaque Depth CopyのCameraと一致しないためSoft Particleを無効化する。
			defaultDrawPso = graphicsPipelineState.Get();

			updateSceneObjectMatrices(
				sceneViewProjectionMatrix,
				false,
				cameraTransform.translate,
				false);
			directionalLightData->cameraPosition = savedCameraPosition;
		}

		if (g_isGameViewVisible && gamePlanarView != nullptr) {
			const EditorPlanarReflectionManager::ProbeView& planarView = *gamePlanarView;
			const PlanarReflectionCamera& reflectionCamera = planarView.gameCam;
			const Vector3 savedCameraPosition = directionalLightData->cameraPosition;
			directionalLightData->cameraPosition = reflectionCamera.position;

			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &gameScissorRect);
			commandList->RSSetViewports(1, &gameViewport);
			commandList->RSSetScissorRects(1, &gameScissorRect);
			drawSkybox(
				gameViewport,
				gameScissorRect,
				planarReflectionRtvHandle,
				reflectionCamera.inverseViewProjection,
				reflectionCamera.position);

			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(planarScenePipelineState.Get());
			commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
			commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			updateSceneObjectMatrices(
				reflectionCamera.viewProjection,
				true,
				reflectionCamera.position,
				false,
				true,
				reflectionCamera.clipPlane);
			defaultDrawPso = planarScenePipelineState.Get();
			cullingOverrideViewProjectionMatrix = &reflectionCamera.viewProjection;
			drawSceneObjects(
				true,
				planarReflectionRtvHandle,
				reflectionCamera.position,
				planarView.sourceId);
			cullingOverrideViewProjectionMatrix = nullptr;
			drawEffects(
				reflectionCamera.viewProjection,
				reflectionCamera.viewMatrix,
				reflectionCamera.projectionMatrix,
				reflectionCamera.position,
				planarReflectionRtvHandle,
				reflectionCamera.viewProjection,
				gameViewport,
				false);  // Planar ReflectionはOpaque Depth CopyのCameraと一致しないためSoft Particleを無効化する。
			defaultDrawPso = graphicsPipelineState.Get();

			updateSceneObjectMatrices(
				gameViewProjectionMatrix,
				true,
				g_gameCameraPosition,
				false);
			directionalLightData->cameraPosition = savedCameraPosition;
		}

		// Final planar RT transition back to PIXEL_SHADER_RESOURCE
		planarReflectionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		planarReflectionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &planarReflectionBarrier);
		hasSubmittedPlanarReflection = true;
		submittedPlanarReflectionStateHash = planarReflectionStateHash;
		submittedPlanarReflectionTarget = planarReflectionRenderTarget;

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPipelineState.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	hasPlanarReflectionCapture =
		hasPlanarReflectionResources &&
		hasSubmittedPlanarReflection &&
		submittedPlanarReflectionTarget == planarReflectionRenderTarget;

	const bool shouldRenderWeightedOit =
		g_oitAccumulationRenderTarget != nullptr &&
		g_oitRevealageRenderTarget != nullptr &&
		g_weightedOitPipelineState != nullptr &&
		g_weightedOitCullNonePipelineState != nullptr &&
		weightedOitCompositePipelineState != nullptr &&
		std::any_of(
			editorSceneObjects.begin(),
			editorSceneObjects.end(),
			[](const EditorSceneObject& sceneObject) {
				return sceneObject.materialData != nullptr &&
					!sceneObject.ocean.isEnabled &&
					sceneObject.materialData->oceanEnabled < 0.5f &&
					sceneObject.materialData->alphaMode == 2 &&
					sceneObject.materialData->transmission <= 0.0001f;
			});
	const bool shouldRenderRefractiveSurface =
		refractiveSurfacePipelineState != nullptr &&
		refractiveSurfaceCullNonePipelineState != nullptr &&
		hdrCompositeRenderTarget != nullptr &&
		depthStencilResource != nullptr &&
		std::any_of(
			editorSceneObjects.begin(),
			editorSceneObjects.end(),
			[](const EditorSceneObject& sceneObject) {
				return sceneObject.materialData != nullptr &&
					!sceneObject.ocean.isEnabled &&
					sceneObject.materialData->oceanEnabled < 0.5f &&
					sceneObject.materialData->alphaMode == 2 &&
					sceneObject.materialData->transmission > 0.0001f;
			});

	if (shouldRenderWeightedOit) {
		std::array<D3D12_RESOURCE_BARRIER, 2u> oitBarriers{};
		ID3D12Resource* oitResources[2] = {
			g_oitAccumulationRenderTarget,
			g_oitRevealageRenderTarget};

		for (uint32_t oitTargetIndex = 0u; oitTargetIndex < oitBarriers.size(); ++oitTargetIndex) {
			oitBarriers[oitTargetIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			oitBarriers[oitTargetIndex].Transition.pResource = oitResources[oitTargetIndex];
			oitBarriers[oitTargetIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			oitBarriers[oitTargetIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			oitBarriers[oitTargetIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		}

		commandList->ResourceBarrier(static_cast<UINT>(oitBarriers.size()), oitBarriers.data());
		const float accumulationClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		const float revealageClearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		commandList->ClearRenderTargetView(g_oitRtvHandles[0], accumulationClearColor, 0, nullptr);
		commandList->ClearRenderTargetView(g_oitRtvHandles[1], revealageClearColor, 0, nullptr);
	}

	if (g_isSceneViewVisible || g_isGameViewVisible) {
		if (depthStencilResource != nullptr && !hasPlanarReflectionCapture) {
			D3D12_RESOURCE_BARRIER mainDepthBarrier{};
			mainDepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			mainDepthBarrier.Transition.pResource = depthStencilResource;
			mainDepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			mainDepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			mainDepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			commandList->ResourceBarrier(1, &mainDepthBarrier);
		}
	}

	if (g_isSceneViewVisible) {
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissorRect);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);
		drawSkybox(
			viewport,
			scissorRect,
			hdrRtvHandle,
			inverseViewProjectionMatrix,
			cameraTransform.translate);

		if (isLegacyPreviewVisible) {
			commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1,
				sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
		}

		const int32_t firstReflectorId = scenePlanarView == nullptr
			? -1
			: scenePlanarView->sourceId;
		drawSceneObjects(
			false,
			hdrRtvHandle,
			cameraTransform.translate,
			-1,
			firstReflectorId,
			false,
			SceneObjectDrawFilter::Opaque);
	}

	if (g_isGameViewVisible) {
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &gameScissorRect);
		commandList->RSSetViewports(1, &gameViewport);
		commandList->RSSetScissorRects(1, &gameScissorRect);
		drawSkybox(
			gameViewport,
			gameScissorRect,
			hdrRtvHandle,
			inverseGameViewProjectionMatrix,
		g_gameCameraPosition);
		const int32_t firstReflectorId = gamePlanarView == nullptr
			? -1
			: gamePlanarView->sourceId;
		drawSceneObjects(
			true,
			hdrRtvHandle,
			g_gameCameraPosition,
			-1,
			firstReflectorId,
			false,
			SceneObjectDrawFilter::Opaque);
	}

	//================================================================
	// Water Surface: Opaque Color / Depth を参照する専用パス
	//================================================================

	const bool shouldRenderWaterSurface =
		primaryOceanSceneObject != nullptr &&
		waterSurfacePipelineState != nullptr &&
		hdrCompositeRenderTarget != nullptr &&
		depthStencilResource != nullptr &&
		opaqueDepthCopyResource != nullptr;

	if (shouldRenderWaterSurface) {
		const auto bindWaterViewConstants = [&commandList, &oceanElapsedTime, primaryOceanSceneObject](
			const Matrix4x4& targetInverseViewProjection,
			const Matrix4x4& targetViewMatrix,
			const Matrix4x4& targetProjectionMatrix,
			const Vector3& targetCameraPosition,
			uint64_t targetSurfaceSampleKey,
			const D3D12_VIEWPORT& targetViewport) {
			std::array<float, 29u> waterViewConstants{};
			std::memcpy(
				waterViewConstants.data(),
				&targetInverseViewProjection.matrix[0][0],
				sizeof(float) * 16u);
			const float inverseRenderWidth =
				1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			const float inverseRenderHeight =
				1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			waterViewConstants[16] = targetViewport.TopLeftX * inverseRenderWidth;
			waterViewConstants[17] = targetViewport.TopLeftY * inverseRenderHeight;
			waterViewConstants[18] = targetViewport.Width * inverseRenderWidth;
			waterViewConstants[19] = targetViewport.Height * inverseRenderHeight;
			waterViewConstants[20] = targetViewMatrix.matrix[0][0];
			waterViewConstants[21] = targetViewMatrix.matrix[1][0];
			waterViewConstants[22] = targetViewMatrix.matrix[2][0];
			waterViewConstants[23] = targetProjectionMatrix.matrix[0][0];
			waterViewConstants[24] = targetViewMatrix.matrix[0][1];
			waterViewConstants[25] = targetViewMatrix.matrix[1][1];
			waterViewConstants[26] = targetViewMatrix.matrix[2][1];
			waterViewConstants[27] = targetProjectionMatrix.matrix[1][1];
			// Scene/Game CameraごとにFFT水面を1回だけ取得し、描画すべき表裏を決める。
			// 高さ0固定ではなく描画・浮力と同じOcean Queryを使うため、大波を横切っても反転が遅れない。
			EditorOceanSurfaceSample cameraSurfaceSample{};
			const bool hasCameraSurfaceSample = SampleEditorOceanSurface(
				g_editorScene,
				primaryOceanSceneObject->gameObjectId,
				targetCameraPosition,
				targetSurfaceSampleKey,
				oceanElapsedTime,
				cameraSurfaceSample);
			const float fallbackSurfaceHeight =
				primaryOceanSceneObject->worldMatrix.matrix[3][1];
			const float cameraSurfaceDistance = hasCameraSurfaceSample
				? Dot(
					Subtract(targetCameraPosition, cameraSurfaceSample.position),
					cameraSurfaceSample.normal)
				: targetCameraPosition.y - fallbackSurfaceHeight;
			const float cameraSurfaceSide = cameraSurfaceDistance >= 0.0f ? 1.0f : -1.0f;
			waterViewConstants[28] =
				std::abs(targetProjectionMatrix.matrix[2][3]) * cameraSurfaceSide;
			commandList->SetGraphicsRoot32BitConstants(
				24u,
				static_cast<UINT>(waterViewConstants.size()),
				waterViewConstants.data(),
				0u);
		};

		std::array<D3D12_RESOURCE_BARRIER, 2u> opaqueCopyBarriers{};
		opaqueCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		opaqueCopyBarriers[0].Transition.pResource = hdrRenderTarget;
		opaqueCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		opaqueCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		opaqueCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		opaqueCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		opaqueCopyBarriers[1].Transition.pResource = hdrCompositeRenderTarget;
		opaqueCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		opaqueCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		opaqueCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		commandList->ResourceBarrier(
			static_cast<UINT>(opaqueCopyBarriers.size()),
			opaqueCopyBarriers.data());
		commandList->CopyResource(hdrCompositeRenderTarget, hdrRenderTarget);

		opaqueCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		opaqueCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		opaqueCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		opaqueCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(
			static_cast<UINT>(opaqueCopyBarriers.size()),
			opaqueCopyBarriers.data());

		std::array<D3D12_RESOURCE_BARRIER, 2u> opaqueDepthCopyBarriers{};
		opaqueDepthCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		opaqueDepthCopyBarriers[0].Transition.pResource = depthStencilResource;
		opaqueDepthCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		opaqueDepthCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		opaqueDepthCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		opaqueDepthCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		opaqueDepthCopyBarriers[1].Transition.pResource = opaqueDepthCopyResource;
		opaqueDepthCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		opaqueDepthCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		opaqueDepthCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		commandList->ResourceBarrier(
			static_cast<UINT>(opaqueDepthCopyBarriers.size()),
			opaqueDepthCopyBarriers.data());
		commandList->CopyResource(opaqueDepthCopyResource, depthStencilResource);

		opaqueDepthCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		opaqueDepthCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		opaqueDepthCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		opaqueDepthCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(
			static_cast<UINT>(opaqueDepthCopyBarriers.size()),
			opaqueDepthCopyBarriers.data());

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(22, hdrCompositeSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(23, opaqueDepthCopySrvHandleGPU);

		if (g_isSceneViewVisible) {
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			bindWaterViewConstants(
				inverseViewProjectionMatrix,
				viewMatrix,
				sceneRenderProjectionMatrix,
				cameraTransform.translate,
				0x5343454e45574154ull,
				viewport);
			const int32_t firstReflectorId = scenePlanarView == nullptr
				? -1
				: scenePlanarView->sourceId;
			drawSceneObjects(
				false,
				hdrRtvHandle,
				cameraTransform.translate,
				-1,
				firstReflectorId,
				false,
				SceneObjectDrawFilter::Water);
		}

		if (g_isGameViewVisible) {
			commandList->RSSetViewports(1, &gameViewport);
			commandList->RSSetScissorRects(1, &gameScissorRect);
			bindWaterViewConstants(
				inverseGameViewProjectionMatrix,
				g_gameViewMatrix,
				gameRenderProjectionMatrix,
				g_gameCameraPosition,
				0x47414d4557415445ull,
				gameViewport);
			const int32_t firstReflectorId = gamePlanarView == nullptr
				? -1
				: gamePlanarView->sourceId;
			drawSceneObjects(
				true,
				hdrRtvHandle,
				g_gameCameraPosition,
				-1,
				firstReflectorId,
				false,
				SceneObjectDrawFilter::Water);
		}

	}

	//================================================================
	// Refractive Surface: 水面合成後のColor / 不透明Depthを参照するガラス専用パス
	//================================================================

	if (shouldRenderRefractiveSurface) {
		const auto bindRefractiveViewConstants = [&commandList](
			const Matrix4x4& targetInverseViewProjection,
			const D3D12_VIEWPORT& targetViewport) {
			std::array<float, 20u> waterViewConstants{};
			std::memcpy(
				waterViewConstants.data(),
				&targetInverseViewProjection.matrix[0][0],
				sizeof(float) * 16u);
			const float inverseRenderWidth =
				1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			const float inverseRenderHeight =
				1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			waterViewConstants[16] = targetViewport.TopLeftX * inverseRenderWidth;
			waterViewConstants[17] = targetViewport.TopLeftY * inverseRenderHeight;
			waterViewConstants[18] = targetViewport.Width * inverseRenderWidth;
			waterViewConstants[19] = targetViewport.Height * inverseRenderHeight;
			commandList->SetGraphicsRoot32BitConstants(
				24u,
				static_cast<UINT>(waterViewConstants.size()),
				waterViewConstants.data(),
				0u);
		};

		std::array<D3D12_RESOURCE_BARRIER, 2u> refractiveCopyBarriers{};
		refractiveCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		refractiveCopyBarriers[0].Transition.pResource = hdrRenderTarget;
		refractiveCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		refractiveCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		refractiveCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		refractiveCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		refractiveCopyBarriers[1].Transition.pResource = hdrCompositeRenderTarget;
		refractiveCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		refractiveCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		refractiveCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		commandList->ResourceBarrier(
			static_cast<UINT>(refractiveCopyBarriers.size()),
			refractiveCopyBarriers.data());
		commandList->CopyResource(hdrCompositeRenderTarget, hdrRenderTarget);

		refractiveCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		refractiveCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		refractiveCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		refractiveCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(
			static_cast<UINT>(refractiveCopyBarriers.size()),
			refractiveCopyBarriers.data());

		D3D12_RESOURCE_BARRIER refractiveDepthBarrier{};
		refractiveDepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		refractiveDepthBarrier.Transition.pResource = depthStencilResource;
		refractiveDepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		refractiveDepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		refractiveDepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &refractiveDepthBarrier);

		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootConstantBufferView(5, emissiveLightResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, shadowMapSrvGpuHandle);
		commandList->SetGraphicsRootDescriptorTable(6, environmentSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(22, hdrCompositeSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(23, depthSrvHandleGPU);

		if (g_isSceneViewVisible) {
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			bindRefractiveViewConstants(inverseViewProjectionMatrix, viewport);
			const int32_t firstReflectorId = scenePlanarView == nullptr
				? -1
				: scenePlanarView->sourceId;
			drawSceneObjects(
				false,
				hdrRtvHandle,
				cameraTransform.translate,
				-1,
				firstReflectorId,
				false,
				SceneObjectDrawFilter::Refractive);
		}

		if (g_isGameViewVisible) {
			commandList->RSSetViewports(1, &gameViewport);
			commandList->RSSetScissorRects(1, &gameScissorRect);
			bindRefractiveViewConstants(inverseGameViewProjectionMatrix, gameViewport);
			const int32_t firstReflectorId = gamePlanarView == nullptr
				? -1
				: gamePlanarView->sourceId;
			drawSceneObjects(
				true,
				hdrRtvHandle,
				g_gameCameraPosition,
				-1,
				firstReflectorId,
				false,
				SceneObjectDrawFilter::Refractive);
		}

		refractiveDepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		refractiveDepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		commandList->ResourceBarrier(1, &refractiveDepthBarrier);
	}

	// 通常半透明と Effect は水面の後に描き、Weighted OIT の対象を水と分離する。
	if (g_isSceneViewVisible) {
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);
		const int32_t firstReflectorId = scenePlanarView == nullptr
			? -1
			: scenePlanarView->sourceId;
		drawSceneObjects(
			false,
			hdrRtvHandle,
			cameraTransform.translate,
			-1,
			firstReflectorId,
			shouldRenderWeightedOit,
			SceneObjectDrawFilter::Transparent);
		drawEffects(
			sceneViewProjectionMatrix,
			viewMatrix,
			sceneRenderProjectionMatrix,
			cameraTransform.translate,
			hdrRtvHandle,
			inverseViewProjectionMatrix,
			viewport,
			true);  // Water Passで作ったOpaque Depth Copyと同じCameraなのでSoft Particleを有効化できる。
		drawReflectionMaskObjects(false, firstReflectorId);
	}

	if (g_isGameViewVisible) {
		commandList->RSSetViewports(1, &gameViewport);
		commandList->RSSetScissorRects(1, &gameScissorRect);
		const int32_t firstReflectorId = gamePlanarView == nullptr
			? -1
			: gamePlanarView->sourceId;
		drawSceneObjects(
			true,
			hdrRtvHandle,
			g_gameCameraPosition,
			-1,
			firstReflectorId,
			shouldRenderWeightedOit,
			SceneObjectDrawFilter::Transparent);
		drawEffects(
			gameViewProjectionMatrix,
			g_gameViewMatrix,
			gameRenderProjectionMatrix,
			g_gameCameraPosition,
			hdrRtvHandle,
			inverseGameViewProjectionMatrix,
			gameViewport,
			true);  // Water Passで作ったOpaque Depth Copyと同じCameraなのでSoft Particleを有効化できる。
		drawReflectionMaskObjects(true, firstReflectorId);
	}

	//================================================================
	// GBuffer: 不透明モデルの材質値と法線マップ適用後の法線を保存
	//================================================================

	auto drawGBufferObjects = [&](bool isGameViewPass) {
		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			if (sceneObject.type != EditorSceneObjectType::Model ||
				sceneObject.cullMode == 1 ||
				sceneObject.ocean.isEnabled ||
				sceneObject.materialData == nullptr ||
				sceneObject.materialData->oceanEnabled >= 0.5f ||
				sceneObject.materialData->alphaMode == 2) {
				continue;
			}

			ID3D12Resource* transformationResource = isGameViewPass
				? sceneObject.gameTransformationResource
				: sceneObject.transformationResource;

			if (transformationResource == nullptr || sceneObject.materialResource == nullptr) {
				continue;
			}

			size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);

			if (meshTypeIndex >= kEditorModelMeshTypeCount ||
				primitiveVertexCounts[meshTypeIndex] == 0u) {
				meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
			}

			g_gBufferManager.BindPipelineState(
				commandList.Get(),
				sceneObject.cullMode == 2);
			commandList->SetGraphicsRootConstantBufferView(
				0,
				sceneObject.materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1,
				transformationResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				2,
				directionalLightResource->GetGPUVirtualAddress());

			const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
				sceneObject.customTextureSrvGpuHandle.ptr != 0u
				? sceneObject.customTextureSrvGpuHandle
				: textureSrvHandlesGPU[2];
			commandList->SetGraphicsRootDescriptorTable(3, textureHandle);
			bindMaterialTextureHandles(sceneObject, textureHandle);

			if (sceneObject.usesCustomMesh &&
				sceneObject.customMeshVertexResource != nullptr &&
				sceneObject.customMeshVertexCount > 0u) {
				const Vector3& gBufferCameraPosition = isGameViewPass
					? g_gameCameraPosition
					: cameraTransform.translate;
				DrawCustomSceneMesh(
					commandList.Get(),
					sceneObject,
					&gBufferCameraPosition,
					false);
			}
			else {
				commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
				commandList->DrawInstanced(
					primitiveVertexCounts[meshTypeIndex],
					GetSceneObjectInstanceCount(sceneObject),
					0,
					0);
			}
		}
	};

	if (shouldRenderGBuffer &&
		(g_isSceneViewVisible || g_isGameViewVisible) &&
		g_gBufferManager.Begin(commandList.Get(), dsvHandle)) {
		commandList->SetDescriptorHeaps(1, descriptorHeaps);
		BindSceneObjectSkinningResources(commandList.Get(), nullptr);
		g_oceanFftManager.BindGraphicsResources(commandList.Get());

		if (g_isSceneViewVisible) {
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			drawGBufferObjects(false);
		}

		if (g_isGameViewVisible) {
			commandList->RSSetViewports(1, &gameViewport);
			commandList->RSSetScissorRects(1, &gameScissorRect);
			drawGBufferObjects(true);
		}

		g_gBufferManager.End(commandList.Get());
	}

	if (shouldRenderMaterialMask && materialMaskRenderTarget != nullptr) {
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
		(!planarViews.empty() || g_isSceneViewVisible || g_isGameViewVisible)) {
		D3D12_RESOURCE_BARRIER depthBarrier{};
		depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		depthBarrier.Transition.pResource = depthStencilResource;
		depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &depthBarrier);
	}

	//================================================================
	// 平面反射の合成
	//================================================================
	// メインシーンを描いた後の色・深度・反射面マスクを使う。
	// Scene View と Game View は異なるカメラなので、各 View を別々に射影する。

	if (hasPlanarReflectionCapture &&
		planarManager.HasCompositeProbes() &&
		hdrCompositeRenderTarget != nullptr &&
		planarReflectionPipelineState != nullptr &&
		materialMaskRenderTarget != nullptr &&
		depthStencilResource != nullptr) {
		D3D12_RESOURCE_BARRIER compositeBarrier{};
		compositeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		compositeBarrier.Transition.pResource = hdrCompositeRenderTarget;
		compositeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		compositeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		compositeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &compositeBarrier);

		commandList->ClearRenderTargetView(hdrCompositeRtvHandle, planarClearColor, 0, nullptr);
		commandList->OMSetRenderTargets(1, &hdrCompositeRtvHandle, FALSE, nullptr);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(planarReflectionPipelineState.Get());
		ID3D12DescriptorHeap* planarDescriptorHeaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, planarDescriptorHeaps);
		commandList->SetGraphicsRootDescriptorTable(0, hdrSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, depthSrvHandleGPU);
		// t2 と t3 は連続配置されているため、反射面マスクの先頭を設定する。
		commandList->SetGraphicsRootDescriptorTable(3, materialMaskSrvHandleGPU);

		auto drawPlanarComposite = [&] (
			const D3D12_VIEWPORT& targetViewport,
			const D3D12_RECT& targetScissorRect,
			const Matrix4x4& targetInverseViewProjection,
			const Matrix4x4& targetReflectionViewProjection,
			const Vector3& targetCameraPosition,
			const Vector4& targetReflectionPlane) {
			float reflectionParams[48] = {};
			reflectionParams[0] = 1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			reflectionParams[1] = 1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			reflectionParams[2] = 12.0f;
			reflectionParams[3] = 1.0f;
			std::memcpy(&reflectionParams[4], &targetInverseViewProjection.matrix[0][0], sizeof(float) * 16u);
			std::memcpy(&reflectionParams[20], &targetReflectionViewProjection.matrix[0][0], sizeof(float) * 16u);
			reflectionParams[36] = targetViewport.TopLeftX;
			reflectionParams[37] = targetViewport.TopLeftY;
			reflectionParams[38] = targetViewport.Width;
			reflectionParams[39] = targetViewport.Height;
			reflectionParams[40] = targetCameraPosition.x;
			reflectionParams[41] = targetCameraPosition.y;
			reflectionParams[42] = targetCameraPosition.z;
			reflectionParams[44] = targetReflectionPlane.x;
			reflectionParams[45] = targetReflectionPlane.y;
			reflectionParams[46] = targetReflectionPlane.z;

			commandList->RSSetViewports(1, &targetViewport);
			commandList->RSSetScissorRects(1, &targetScissorRect);
			commandList->SetGraphicsRoot32BitConstants(2, 48, reflectionParams, 0);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->DrawInstanced(3, 1, 0, 0);
		};

		if (g_isSceneViewVisible && scenePlanarView != nullptr) {
			const EditorPlanarReflectionManager::ProbeView& planarView = *scenePlanarView;
			drawPlanarComposite(
				viewport,
				scissorRect,
				inverseViewProjectionMatrix,
				planarView.sceneCam.viewProjection,
				cameraTransform.translate,
				planarView.sceneCam.clipPlane);
		}

		if (g_isGameViewVisible && gamePlanarView != nullptr) {
			const EditorPlanarReflectionManager::ProbeView& planarView = *gamePlanarView;
			drawPlanarComposite(
				gameViewport,
				gameScissorRect,
				inverseGameViewProjectionMatrix,
				planarView.gameCam.viewProjection,
				g_gameCameraPosition,
				planarView.gameCam.clipPlane);
		}

		compositeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		compositeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &compositeBarrier);
		hasPlanarReflectionComposite = true;
	}

	//================================================================
	// Compute: 深度ピラミッドとワールド法線の生成
	//================================================================

	if (shouldBuildDepthHierarchy &&
		depthStencilResource != nullptr &&
		(!planarViews.empty() || g_isSceneViewVisible || g_isGameViewVisible)) {
		const D3D12_RESOURCE_STATES computeReadableDepthState = static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		D3D12_RESOURCE_BARRIER depthComputeBarrier{};
		depthComputeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		depthComputeBarrier.Transition.pResource = depthStencilResource;
		depthComputeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		depthComputeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		depthComputeBarrier.Transition.StateAfter = computeReadableDepthState;
		commandList->ResourceBarrier(1u, &depthComputeBarrier);

		ID3D12DescriptorHeap* computeDescriptorHeaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1u, computeDescriptorHeaps);
		if (shouldUpdateGpuParticles) {
			EditorEffectManager& effectManager = g_editorRuntimeManager.GetEffectManager();
			EditorVfxManager& vfxManagerForGpuSpawns = g_editorRuntimeManager.GetVfxManager();
			static std::vector<EditorGpuParticleManager::CollisionProxy> collisionProxies;
			CollectParticleCollisionProxies(collisionProxies);
			const bool useGameCollisionCamera = g_isGameViewVisible;
			const Matrix4x4& collisionViewProjection = useGameCollisionCamera
				? gameViewProjectionMatrix
				: sceneViewProjectionMatrix;
			const Matrix4x4& collisionInverseViewProjection = useGameCollisionCamera
				? inverseGameViewProjectionMatrix
				: inverseViewProjectionMatrix;
			const D3D12_VIEWPORT& collisionViewport = useGameCollisionCamera
				? gameViewport
				: viewport;
			// Stage2: 新VFX(useGpuSimulation Billboard / MeshParticle)の発生要求もEditorEffectManager分と合成し、
			// 同じEditorGpuParticleManager(既存GPU Compute Particle Pipeline)へまとめて渡す。
			static std::vector<EditorEffectManager::GpuParticleSpawn> combinedGpuParticleSpawns;
			combinedGpuParticleSpawns.clear();
			const auto& effectManagerSpawns = effectManager.GetPendingGpuParticleSpawns();
			const auto& vfxManagerSpawns = vfxManagerForGpuSpawns.GetPendingGpuParticleSpawns();
			combinedGpuParticleSpawns.reserve(effectManagerSpawns.size() + vfxManagerSpawns.size());
			combinedGpuParticleSpawns.insert(combinedGpuParticleSpawns.end(), effectManagerSpawns.begin(), effectManagerSpawns.end());
			combinedGpuParticleSpawns.insert(combinedGpuParticleSpawns.end(), vfxManagerSpawns.begin(), vfxManagerSpawns.end());
			g_gpuParticleManager.Update(
				commandList.Get(),
				combinedGpuParticleSpawns,
				effectManager.GetLastDeltaTime(),
				depthSrvHandleGPU,
				collisionViewProjection,
				collisionInverseViewProjection,
				g_renderWidth,
				g_renderHeight,
				collisionViewport,
				collisionProxies);
			effectManager.ClearPendingGpuParticleSpawns();
			vfxManagerForGpuSpawns.ClearPendingGpuParticleSpawns();
		}

		const Matrix4x4& depthInverseViewProjection = g_isSceneViewVisible
			? inverseViewProjectionMatrix
			: inverseGameViewProjectionMatrix;
		g_depthHierarchyManager.Generate(
			commandList.Get(),
			depthSrvHandleGPU,
			&depthInverseViewProjection.matrix[0][0]);

		if (shouldUseGpuCulling) {
			//============================================================
			// GPU Occlusion Culling 用のワールド AABB を作る
			//============================================================

			static std::vector<EditorGpuCullingInput> gpuCullingInputs;
			gpuCullingInputs.clear();
			gpuCullingInputs.reserve(gpuCullingCandidateCount);

			for (const EditorSceneObject& sceneObject : editorSceneObjects) {
				if (sceneObject.type != EditorSceneObjectType::Model ||
					sceneObject.ocean.isEnabled ||
					sceneObject.transformationData == nullptr) {
					continue;
				}

				const Vector3 localBoundsCenter = GetPlanarReflectionLocalMeshCenter(sceneObject);
				const Vector3 localBoundsSize = GetPlanarReflectionLocalMeshSize(sceneObject);
				const Vector3 localBoundsExtent = {
					localBoundsSize.x * 0.5f,
					localBoundsSize.y * 0.5f,
					localBoundsSize.z * 0.5f
				};
				Vector3 worldMinimum = {
					(std::numeric_limits<float>::max)(),
					(std::numeric_limits<float>::max)(),
					(std::numeric_limits<float>::max)()
				};
				Vector3 worldMaximum = {
					-(std::numeric_limits<float>::max)(),
					-(std::numeric_limits<float>::max)(),
					-(std::numeric_limits<float>::max)()
				};

				for (uint32_t cornerIndex = 0u; cornerIndex < 8u; cornerIndex++) {
					const Vector3 localCorner = {
						localBoundsCenter.x + ((cornerIndex & 1u) != 0u ? localBoundsExtent.x : -localBoundsExtent.x),
						localBoundsCenter.y + ((cornerIndex & 2u) != 0u ? localBoundsExtent.y : -localBoundsExtent.y),
						localBoundsCenter.z + ((cornerIndex & 4u) != 0u ? localBoundsExtent.z : -localBoundsExtent.z)
					};
					const Vector3 worldCorner = Transform(localCorner, sceneObject.transformationData->World);
					worldMinimum.x = (std::min)(worldMinimum.x, worldCorner.x);
					worldMinimum.y = (std::min)(worldMinimum.y, worldCorner.y);
					worldMinimum.z = (std::min)(worldMinimum.z, worldCorner.z);
					worldMaximum.x = (std::max)(worldMaximum.x, worldCorner.x);
					worldMaximum.y = (std::max)(worldMaximum.y, worldCorner.y);
					worldMaximum.z = (std::max)(worldMaximum.z, worldCorner.z);
				}

				uint32_t vertexCount = sceneObject.customMeshVertexCount;

				if (!sceneObject.usesCustomMesh || vertexCount == 0u) {
					size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);

					if (meshTypeIndex >= kEditorModelMeshTypeCount) {
						meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
					}

					vertexCount = primitiveVertexCounts[meshTypeIndex];
				}

				gpuCullingInputs.push_back({
					(worldMinimum.x + worldMaximum.x) * 0.5f,
					(worldMinimum.y + worldMaximum.y) * 0.5f,
					(worldMinimum.z + worldMaximum.z) * 0.5f,
					(worldMaximum.x - worldMinimum.x) * 0.5f,
					(worldMaximum.y - worldMinimum.y) * 0.5f,
					(worldMaximum.z - worldMinimum.z) * 0.5f,
					sceneObject.gameObjectId,
					vertexCount
				});
			}

			const uint32_t depthLevelCount = g_depthHierarchyManager.GetActiveLevelCount();

			if (depthLevelCount > 0u) {
				const uint32_t cullingDepthLevel = (std::min)(4u, depthLevelCount - 1u);
				const Matrix4x4& cullingViewProjection = g_isSceneViewVisible
					? sceneViewProjectionMatrix
					: gameViewProjectionMatrix;
				const D3D12_VIEWPORT& cullingViewport = g_isSceneViewVisible
					? viewport
					: gameViewport;
				const float inverseRenderWidth = 1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
				const float inverseRenderHeight = 1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));

				g_gpuCullingManager.Execute(
					commandList.Get(),
					gpuCullingInputs,
					g_depthHierarchyManager.GetDepthPyramidSrvHandle(cullingDepthLevel),
					&cullingViewProjection.matrix[0][0],
					g_depthHierarchyManager.GetDepthPyramidWidth(cullingDepthLevel),
					g_depthHierarchyManager.GetDepthPyramidHeight(cullingDepthLevel),
					cullingViewport.TopLeftX * inverseRenderWidth,
					cullingViewport.TopLeftY * inverseRenderHeight,
					cullingViewport.Width * inverseRenderWidth,
					cullingViewport.Height * inverseRenderHeight);
			}
		}

		depthComputeBarrier.Transition.StateBefore = computeReadableDepthState;
		depthComputeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1u, &depthComputeBarrier);
	}

	//================================================================
	// Post-process: Bloom + ToneMapping
	//================================================================

	D3D12_VIEWPORT fullViewport{};
	fullViewport.Width = static_cast<float>(g_renderWidth);
	fullViewport.Height = static_cast<float>(g_renderHeight);
	fullViewport.MaxDepth = 1.0f;
	D3D12_RECT fullScissor{};
	fullScissor.right = static_cast<LONG>(g_renderWidth);
	fullScissor.bottom = static_cast<LONG>(g_renderHeight);

	float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	D3D12_GPU_DESCRIPTOR_HANDLE hdrPostSourceSrvHandle = hasPlanarReflectionComposite
		? hdrCompositeSrvHandleGPU : hdrSrvHandleGPU;
	ID3D12Resource* hdrPostSourceResource = hasPlanarReflectionComposite
		? hdrCompositeRenderTarget : hdrRenderTarget;

	//================================================================
	// SSR (Screen Space Reflection)

	commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// SSAO: Scene Depth 遶翫・SSAO A 遶翫・SSAO B
	if (shouldRenderAmbientOcclusion &&
		depthStencilResource != nullptr &&
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
		commandList->SetGraphicsRootDescriptorTable(
			1,
			g_gBufferManager.GetNormalSrvHandle());
		float ssaoParams[8] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			7.0f,
			1.10f,
			0.015f,
			1.35f,
			(std::max)(ppSettings.cameraNearClip, 0.01f),
			(std::max)(ppSettings.cameraFarClip, ppSettings.cameraNearClip + 0.01f)
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
			1.5f,
			1800.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 4, ssaoBlurParams, 0);
		commandList->DrawInstanced(3, 1, 0, 0);

		ssaoBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		ssaoBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &ssaoBarrier);
	}

	//================================================================
	// SSGI: DepthとGBufferから近傍面の色・放射を集め、HDRへ加算する
	//================================================================

	if (shouldRenderSsgi &&
		hdrPostSourceResource != nullptr &&
		depthStencilResource != nullptr &&
		g_gBufferManager.IsReady() &&
		ssgiPipelineState != nullptr) {
		D3D12_RESOURCE_BARRIER ssgiTargetBarrier{};
		ssgiTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		ssgiTargetBarrier.Transition.pResource = hdrPostSourceResource;
		ssgiTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		ssgiTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		ssgiTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &ssgiTargetBarrier);

		const D3D12_CPU_DESCRIPTOR_HANDLE ssgiTargetRtv = hasPlanarReflectionComposite
			? hdrCompositeRtvHandle
			: hdrRtvHandle;
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(ssgiPipelineState.Get());
		commandList->OMSetRenderTargets(1, &ssgiTargetRtv, FALSE, nullptr);
		commandList->SetGraphicsRootDescriptorTable(0, depthSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(1, g_gBufferManager.GetAlbedoSrvHandle());
		// MaterialとEmissionはGBuffer内で連続したSRVなので、t2の先頭だけを設定する。
		commandList->SetGraphicsRootDescriptorTable(3, g_gBufferManager.GetMaterialSrvHandle());

		auto drawSsgiViewport = [&](
			const D3D12_VIEWPORT& targetViewport,
			const D3D12_RECT& targetScissor,
			const Matrix4x4& targetInverseViewProjection) {
			float ssgiParams[24] = {};
			ssgiParams[0] = 1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			ssgiParams[1] = 1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			ssgiParams[2] = (std::max)(ppSettings.compositeSsgiIntensity, 0.0f);
			ssgiParams[3] = (std::clamp)(ppSettings.compositeSsgiRadiusPixels, 1.0f, 128.0f);
			std::memcpy(
				&ssgiParams[4],
				&targetInverseViewProjection.matrix[0][0],
				sizeof(float) * 16u);
			ssgiParams[20] = targetViewport.TopLeftX;
			ssgiParams[21] = targetViewport.TopLeftY;
			ssgiParams[22] = targetViewport.Width;
			ssgiParams[23] = targetViewport.Height;
			commandList->RSSetViewports(1, &targetViewport);
			commandList->RSSetScissorRects(1, &targetScissor);
			commandList->SetGraphicsRoot32BitConstants(2, 24, ssgiParams, 0);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->DrawInstanced(3, 1, 0, 0);
		};

		if (g_isSceneViewVisible) {
			drawSsgiViewport(viewport, scissorRect, inverseViewProjectionMatrix);
		}

		if (g_isGameViewVisible) {
			drawSsgiViewport(gameViewport, gameScissorRect, inverseGameViewProjectionMatrix);
		}

		ssgiTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		ssgiTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &ssgiTargetBarrier);
	}

	//================================================================
	// Compute: SSR と時間方向の履歴解決
	//================================================================

	if (shouldExecuteTemporalOrSsr &&
		hdrPostSourceResource != nullptr &&
		depthStencilResource != nullptr &&
		materialMaskRenderTarget != nullptr &&
		(g_isSceneViewVisible || g_isGameViewVisible)) {
		const D3D12_RESOURCE_STATES shaderReadState = static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		std::array<D3D12_RESOURCE_BARRIER, 3u> temporalInputBarriers{};
		ID3D12Resource* temporalInputResources[3] = {
			hdrPostSourceResource,
			depthStencilResource,
			materialMaskRenderTarget,
		};

		for (uint32_t barrierIndex = 0u; barrierIndex < temporalInputBarriers.size(); barrierIndex++) {
			D3D12_RESOURCE_BARRIER& temporalInputBarrier = temporalInputBarriers[barrierIndex];
			temporalInputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			temporalInputBarrier.Transition.pResource = temporalInputResources[barrierIndex];
			temporalInputBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			temporalInputBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			temporalInputBarrier.Transition.StateAfter = shaderReadState;
		}

		commandList->ResourceBarrier(
			static_cast<UINT>(temporalInputBarriers.size()),
			temporalInputBarriers.data());

		const uint32_t depthLevelCount = g_depthHierarchyManager.GetActiveLevelCount();
		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5u> ssrDepthPyramidSrvHandles{};

		for (uint32_t depthLevelIndex = 0u;
			depthLevelIndex < ssrDepthPyramidSrvHandles.size();
			depthLevelIndex++) {
			const uint32_t sourceDepthLevel = depthLevelCount > 0u
				? (std::min)(depthLevelIndex, depthLevelCount - 1u)
				: 0u;
			ssrDepthPyramidSrvHandles[depthLevelIndex] =
				g_depthHierarchyManager.GetDepthPyramidSrvHandle(sourceDepthLevel);
		}

		ID3D12DescriptorHeap* temporalDescriptorHeaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1u, temporalDescriptorHeaps);
		const auto executeTemporalView = [
			&](
				const Matrix4x4& targetInverseViewProjection,
				const Matrix4x4& targetViewProjection,
				const Vector3& targetCameraPosition,
				const D3D12_VIEWPORT& targetViewport,
				uint32_t viewHistoryIndex,
				bool advanceHistoryFrame) {
			return g_temporalRenderingManager.Execute(
				commandList.Get(),
				hdrPostSourceSrvHandle,
				depthSrvHandleGPU,
				g_gBufferManager.GetMotionVectorSrvHandle(),
				g_gBufferManager.IsReady()
					? g_gBufferManager.GetNormalSrvHandle()
					: g_depthHierarchyManager.GetReconstructedNormalSrvHandle(),
				ssrDepthPyramidSrvHandles,
				materialMaskSrvHandleGPU,
				&targetInverseViewProjection.matrix[0][0],
				&targetViewProjection.matrix[0][0],
				&targetCameraPosition.x,
				targetViewport.TopLeftX,
				targetViewport.TopLeftY,
				targetViewport.Width,
				targetViewport.Height,
				ppSettings.ssrEnabled,
				ppSettings.aaMode == 3,
				viewHistoryIndex,
				advanceHistoryFrame,
				ppSettings.temporalSharpness,
				ppSettings.temporalBlendRatio);
		};

		bool isTemporalRenderingExecuted = false;

		if (g_isSceneViewVisible) {
			isTemporalRenderingExecuted = executeTemporalView(
				inverseViewProjectionMatrix,
				sceneViewProjectionMatrix,
				cameraTransform.translate,
				viewport,
				0u,
				true);
		}

		if (g_isGameViewVisible) {
			isTemporalRenderingExecuted = executeTemporalView(
				inverseGameViewProjectionMatrix,
				gameViewProjectionMatrix,
				g_gameCameraPosition,
				gameViewport,
				1u,
				true) || isTemporalRenderingExecuted;
		}

		for (D3D12_RESOURCE_BARRIER& temporalInputBarrier : temporalInputBarriers) {
			temporalInputBarrier.Transition.StateBefore = shaderReadState;
			temporalInputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}

		commandList->ResourceBarrier(
			static_cast<UINT>(temporalInputBarriers.size()),
			temporalInputBarriers.data());

		if (isTemporalRenderingExecuted) {
			hdrPostSourceSrvHandle = g_temporalRenderingManager.GetOutputSrvHandle();
			hdrPostSourceResource = g_temporalRenderingManager.GetOutputResource();
		}
	}

	//================================================================
	// Weighted Blended OIT 合成
	//================================================================

	if (shouldRenderWeightedOit) {
		std::array<D3D12_RESOURCE_BARRIER, 2u> oitReadBarriers{};
		ID3D12Resource* oitResources[2] = {
			g_oitAccumulationRenderTarget,
			g_oitRevealageRenderTarget};

		for (uint32_t oitTargetIndex = 0u; oitTargetIndex < oitReadBarriers.size(); ++oitTargetIndex) {
			oitReadBarriers[oitTargetIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			oitReadBarriers[oitTargetIndex].Transition.pResource = oitResources[oitTargetIndex];
			oitReadBarriers[oitTargetIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			oitReadBarriers[oitTargetIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			oitReadBarriers[oitTargetIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}

		commandList->ResourceBarrier(static_cast<UINT>(oitReadBarriers.size()), oitReadBarriers.data());
		const bool isOitSourceHdr = hdrPostSourceSrvHandle.ptr == hdrSrvHandleGPU.ptr;
		ID3D12Resource* oitDestinationResource = isOitSourceHdr
			? hdrCompositeRenderTarget
			: hdrRenderTarget;
		const D3D12_CPU_DESCRIPTOR_HANDLE oitDestinationRtvHandle = isOitSourceHdr
			? hdrCompositeRtvHandle
			: hdrRtvHandle;
		const D3D12_GPU_DESCRIPTOR_HANDLE oitDestinationSrvHandle = isOitSourceHdr
			? hdrCompositeSrvHandleGPU
			: hdrSrvHandleGPU;

		D3D12_RESOURCE_BARRIER oitDestinationBarrier{};
		oitDestinationBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		oitDestinationBarrier.Transition.pResource = oitDestinationResource;
		oitDestinationBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		oitDestinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		oitDestinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &oitDestinationBarrier);

		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &oitDestinationRtvHandle, FALSE, nullptr);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(weightedOitCompositePipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, g_oitSrvHandlesGPU[0]);
		commandList->SetGraphicsRootDescriptorTable(3, g_oitSrvHandlesGPU[1]);
		const float oitCompositeParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		commandList->SetGraphicsRoot32BitConstants(2, 4, oitCompositeParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		oitDestinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		oitDestinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &oitDestinationBarrier);
		hdrPostSourceSrvHandle = oitDestinationSrvHandle;
		hdrPostSourceResource = oitDestinationResource;
	}

	//================================================================
	// Underwater / Caustics
	// 海面がある時だけ、Scene と Game の各カメラに対応する深度復元を行う。
	//================================================================

	if (primaryOceanSceneObject != nullptr &&
		underwaterCausticsPipelineState != nullptr &&
		depthStencilResource != nullptr &&
		hdrRenderTarget != nullptr &&
		hdrCompositeRenderTarget != nullptr) {
		const bool isUnderwaterSourceHdr = hdrPostSourceSrvHandle.ptr == hdrSrvHandleGPU.ptr;
		ID3D12Resource* underwaterDestinationResource = isUnderwaterSourceHdr
			? hdrCompositeRenderTarget
			: hdrRenderTarget;
		const D3D12_CPU_DESCRIPTOR_HANDLE underwaterDestinationRtvHandle = isUnderwaterSourceHdr
			? hdrCompositeRtvHandle
			: hdrRtvHandle;
		const D3D12_GPU_DESCRIPTOR_HANDLE underwaterDestinationSrvHandle = isUnderwaterSourceHdr
			? hdrCompositeSrvHandleGPU
			: hdrSrvHandleGPU;

		D3D12_RESOURCE_BARRIER underwaterBarrier{};
		underwaterBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		underwaterBarrier.Transition.pResource = underwaterDestinationResource;
		underwaterBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		underwaterBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		underwaterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &underwaterBarrier);

		const float underwaterClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		commandList->OMSetRenderTargets(1, &underwaterDestinationRtvHandle, FALSE, nullptr);
		commandList->ClearRenderTargetView(
			underwaterDestinationRtvHandle,
			underwaterClearColor,
			0,
			nullptr);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(underwaterCausticsPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, depthSrvHandleGPU);
		g_oceanFftManager.BindPostProcessDisplacement(commandList.Get(), 4u);

		auto findUnderwaterOcean = [&](const Vector3& targetCameraPosition) {
			EditorSceneObject* nearestOceanSceneObject = nullptr;
			float nearestOceanScore = (std::numeric_limits<float>::max)();

			for (EditorSceneObject& candidateSceneObject : editorSceneObjects) {
				if (!candidateSceneObject.ocean.isEnabled ||
					candidateSceneObject.transformationData == nullptr ||
					!g_oceanFftManager.IsReadyFor(candidateSceneObject.ocean)) {
					continue;
				}

				const Matrix4x4 candidateWorldToLocal =
					Inverse(candidateSceneObject.worldMatrix);
				const Vector3 localCameraPosition = Transform(
					targetCameraPosition,
					candidateWorldToLocal);
				const float localHalfExtent =
					(std::max)(candidateSceneObject.ocean.size, 1.0f) * 4.0f;
				const float overflowX = (std::max)(
					std::abs(localCameraPosition.x) - localHalfExtent,
					0.0f);
				const float overflowZ = (std::max)(
					std::abs(localCameraPosition.z) - localHalfExtent,
					0.0f);
				const float horizontalOverflowSquared =
					overflowX * overflowX + overflowZ * overflowZ;
				const float candidateScore = horizontalOverflowSquared * 1024.0f +
					std::abs(localCameraPosition.y);

				if (candidateScore < nearestOceanScore) {
					nearestOceanScore = candidateScore;
					nearestOceanSceneObject = &candidateSceneObject;
				}
			}

			return nearestOceanSceneObject;
		};

		auto drawUnderwaterViewport = [&](
				const D3D12_VIEWPORT& targetViewport,
				const D3D12_RECT& targetScissor,
				const Matrix4x4& targetInverseViewProjection,
				const Vector3& targetCameraPosition,
				const EditorSceneObject& oceanSceneObject) {
			const Matrix4x4 oceanWorldToLocalMatrix =
				Inverse(oceanSceneObject.worldMatrix);
			const bool isOceanFftReady =
				oceanSceneObject.transformationData != nullptr &&
				g_oceanFftManager.IsReadyFor(oceanSceneObject.ocean);
			const float oceanWorldVerticalScale = std::sqrt(
				oceanSceneObject.worldMatrix.matrix[1][0] *
					oceanSceneObject.worldMatrix.matrix[1][0] +
				oceanSceneObject.worldMatrix.matrix[1][1] *
					oceanSceneObject.worldMatrix.matrix[1][1] +
				oceanSceneObject.worldMatrix.matrix[1][2] *
					oceanSceneObject.worldMatrix.matrix[1][2]);
			float underwaterParams[48] = {};
			std::memcpy(
				underwaterParams,
				&targetInverseViewProjection.matrix[0][0],
				sizeof(float) * 16u);
			underwaterParams[16] = targetCameraPosition.x;
			underwaterParams[17] = targetCameraPosition.y;
			underwaterParams[18] = targetCameraPosition.z;
			underwaterParams[19] = oceanSceneObject.worldMatrix.matrix[3][1];
			underwaterParams[20] = oceanSceneObject.ocean.deepColor.x;
			underwaterParams[21] = oceanSceneObject.ocean.deepColor.y;
			underwaterParams[22] = oceanSceneObject.ocean.deepColor.z;
			underwaterParams[23] =
				oceanSceneObject.ocean.detailNormalStrength * 1.25f;
			underwaterParams[24] = oceanSceneObject.ocean.shallowColor.x;
			underwaterParams[25] = oceanSceneObject.ocean.shallowColor.y;
			underwaterParams[26] = oceanSceneObject.ocean.shallowColor.z;
			underwaterParams[27] = oceanSceneObject.ocean.absorptionDistance;
			underwaterParams[28] = 1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
			underwaterParams[29] = 1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
			underwaterParams[30] = oceanElapsedTime;
			underwaterParams[31] = oceanSceneObject.ocean.refractionDistortion;
			underwaterParams[32] = targetViewport.TopLeftX / static_cast<float>(g_renderWidth);
			underwaterParams[33] = targetViewport.TopLeftY / static_cast<float>(g_renderHeight);
			underwaterParams[34] = targetViewport.Width / static_cast<float>(g_renderWidth);
			underwaterParams[35] = targetViewport.Height / static_cast<float>(g_renderHeight);
			underwaterParams[36] = oceanWorldToLocalMatrix.matrix[0][0];
			underwaterParams[37] = oceanWorldToLocalMatrix.matrix[1][0];
			underwaterParams[38] = oceanWorldToLocalMatrix.matrix[2][0];
			underwaterParams[39] = oceanWorldToLocalMatrix.matrix[3][0];
			underwaterParams[40] = oceanWorldToLocalMatrix.matrix[0][2];
			underwaterParams[41] = oceanWorldToLocalMatrix.matrix[1][2];
			underwaterParams[42] = oceanWorldToLocalMatrix.matrix[2][2];
			underwaterParams[43] = oceanWorldToLocalMatrix.matrix[3][2];
			underwaterParams[44] = isOceanFftReady
				? oceanSceneObject.transformationData->oceanWaveData1[15u].x
				: 1.0f;
			underwaterParams[45] = isOceanFftReady
				? oceanSceneObject.transformationData->oceanWaveData1[15u].z
				: 1.0f;
			underwaterParams[46] = oceanSceneObject.ocean.size * 4.0f;
			underwaterParams[47] = (std::max)(oceanWorldVerticalScale, 0.001f);

			commandList->RSSetViewports(1, &targetViewport);
			commandList->RSSetScissorRects(1, &targetScissor);
			commandList->SetGraphicsRoot32BitConstants(2, 48, underwaterParams, 0);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->DrawInstanced(3, 1, 0, 0);
		};

		if (g_isSceneViewVisible) {
			EditorSceneObject* sceneOceanSceneObject =
				findUnderwaterOcean(cameraTransform.translate);

			if (sceneOceanSceneObject != nullptr) {
				drawUnderwaterViewport(
					viewport,
					scissorRect,
					inverseViewProjectionMatrix,
					cameraTransform.translate,
					*sceneOceanSceneObject);
			}
		}

		if (g_isGameViewVisible) {
			EditorSceneObject* gameOceanSceneObject =
				findUnderwaterOcean(g_gameCameraPosition);

			if (gameOceanSceneObject != nullptr) {
				drawUnderwaterViewport(
					gameViewport,
					gameScissorRect,
					inverseGameViewProjectionMatrix,
					g_gameCameraPosition,
					*gameOceanSceneObject);
			}
		}

		underwaterBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		underwaterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &underwaterBarrier);
		hdrPostSourceSrvHandle = underwaterDestinationSrvHandle;
		hdrPostSourceResource = underwaterDestinationResource;
	}

	//================================================================
	// 多段Bloomを実行し、失敗時は既存Bloomを最終合成に使う
	//================================================================
	D3D12_GPU_DESCRIPTOR_HANDLE finalBloomSrvHandle = bloomSrvHandlesGPU[0];
	const size_t bloomModeIndex = 1u;
	const float bloomOutputIntensity = ppSettings.bloomIntensity * ppSettings.glareIntensityByMode[bloomModeIndex];
	const bool isQualityBloomExecuted =
		ppSettings.hasPostProcessComponent &&
		ppSettings.glareModeMask != 0 &&
		bloomOutputIntensity > 0.0f &&
		g_postProcessQualityManager.ExecuteBloom(
			commandList.Get(),
			hdrPostSourceSrvHandle,
			bloomOutputIntensity,
			ppSettings.bloomThreshold,
			ppSettings.bloomSoftKnee,
			ppSettings.glareSizeByMode[bloomModeIndex]);

	if (isQualityBloomExecuted) {
		finalBloomSrvHandle = g_postProcessQualityManager.GetBloomSrvHandle();
	}

	//================================================================
	// Blender 風 Glare: Bloom 明部を Ghosts / Streaks / Fog Glow 等へ変換する
	//================================================================

	if (isQualityBloomExecuted) {
		bool preserveGlareSource = (ppSettings.glareModeMask & (1 << 1)) != 0;

		for (int32_t glareModeIndex = 2; glareModeIndex <= 7; glareModeIndex++) {
			if ((ppSettings.glareModeMask & (1 << glareModeIndex)) == 0) {
				continue;
			}

			const size_t glareArrayIndex = static_cast<size_t>(glareModeIndex);
			const bool isGlareExecuted = g_postProcessQualityManager.ExecuteGlare(
				commandList.Get(),
				finalBloomSrvHandle,
				glareModeIndex,
				ppSettings.glareIntensityByMode[glareArrayIndex],
				ppSettings.glareSizeByMode[glareArrayIndex],
				ppSettings.glareAngleByMode[glareArrayIndex],
				ppSettings.glareStreakCountByMode[glareArrayIndex],
				ppSettings.glareFadeByMode[glareArrayIndex],
				ppSettings.glareColorModulationByMode[glareArrayIndex],
				ppSettings.glareCenterByMode[glareArrayIndex].x,
				ppSettings.glareCenterByMode[glareArrayIndex].y,
				ppSettings.glareColorByMode[glareArrayIndex].x,
				ppSettings.glareColorByMode[glareArrayIndex].y,
				ppSettings.glareColorByMode[glareArrayIndex].z,
				preserveGlareSource);

			if (isGlareExecuted) {
				finalBloomSrvHandle = g_postProcessQualityManager.GetGlareSrvHandle();
				preserveGlareSource = true;
			}
		}
	}

	//================================================================
	// Depth of Field: 現在の HDR 結果と depth を元に被写界深度ブラー
	// 入力と同じ RenderTarget へ書かないよう、HDR と Composite を交互に使う。
	//================================================================
	const float inverseRenderWidth = 1.0f / (std::max)(static_cast<float>(g_renderWidth), 1.0f);
	const float inverseRenderHeight = 1.0f / (std::max)(static_cast<float>(g_renderHeight), 1.0f);
	const float gameViewportOriginU = g_editorGameX * inverseRenderWidth;
	const float gameViewportOriginV = g_editorGameY * inverseRenderHeight;
	const float gameViewportWidthUv = g_editorGameWidth * inverseRenderWidth;
	const float gameViewportHeightUv = g_editorGameHeight * inverseRenderHeight;

	if (g_isGameViewVisible &&
		ppSettings.cameraDofEnabled &&
		dofPipelineState != nullptr &&
		hdrRenderTarget != nullptr &&
		hdrCompositeRenderTarget != nullptr) {
		const bool isDofSourceComposite =
			hdrPostSourceSrvHandle.ptr == hdrCompositeSrvHandleGPU.ptr;
		ID3D12Resource* dofDestinationResource = isDofSourceComposite
			? hdrRenderTarget
			: hdrCompositeRenderTarget;
		const D3D12_CPU_DESCRIPTOR_HANDLE dofDestinationRtvHandle = isDofSourceComposite
			? hdrRtvHandle
			: hdrCompositeRtvHandle;
		const D3D12_GPU_DESCRIPTOR_HANDLE dofDestinationSrvHandle = isDofSourceComposite
			? hdrSrvHandleGPU
			: hdrCompositeSrvHandleGPU;

		D3D12_RESOURCE_BARRIER dofBarrier{};
		dofBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		dofBarrier.Transition.pResource = dofDestinationResource;
		dofBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		dofBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		dofBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &dofBarrier);

		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &dofDestinationRtvHandle, FALSE, nullptr);
		float dofClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		commandList->ClearRenderTargetView(dofDestinationRtvHandle, dofClearColor, 0, nullptr);
		ID3D12DescriptorHeap* heaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(dofPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, depthSrvHandleGPU);
		float dofParams[12] = {
			ppSettings.cameraDofFocusDistance,
			ppSettings.cameraDofAperture,
			ppSettings.cameraNearClip,
			ppSettings.cameraFarClip,
			ppSettings.cameraDofFocalLength,
			24.0f,
			inverseRenderWidth,
			inverseRenderHeight,
			gameViewportOriginU,
			gameViewportOriginV,
			gameViewportWidthUv,
			gameViewportHeightUv
		};
		commandList->SetGraphicsRoot32BitConstants(2u, 12u, dofParams, 0u);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		dofBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		dofBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &dofBarrier);
		hdrPostSourceSrvHandle = dofDestinationSrvHandle;
		hdrPostSourceResource = dofDestinationResource;
	}

	//================================================================
	// Motion Blur: velocity を使って移動ブラー
	// 現在の HDR 入力とは別の RenderTarget へ書き、読み書き競合を防ぐ。
	//================================================================
	if (g_isGameViewVisible &&
		ppSettings.cameraMotionBlurEnabled &&
		ppSettings.aaMode == 3 &&
		motionBlurPipelineState != nullptr &&
		hdrRenderTarget != nullptr &&
		hdrCompositeRenderTarget != nullptr) {
		const bool isMotionBlurSourceHdr =
			hdrPostSourceSrvHandle.ptr == hdrSrvHandleGPU.ptr;
		ID3D12Resource* motionBlurDestinationResource = isMotionBlurSourceHdr
			? hdrCompositeRenderTarget
			: hdrRenderTarget;
		const D3D12_CPU_DESCRIPTOR_HANDLE motionBlurDestinationRtvHandle = isMotionBlurSourceHdr
			? hdrCompositeRtvHandle
			: hdrRtvHandle;
		const D3D12_GPU_DESCRIPTOR_HANDLE motionBlurDestinationSrvHandle = isMotionBlurSourceHdr
			? hdrCompositeSrvHandleGPU
			: hdrSrvHandleGPU;

		D3D12_RESOURCE_BARRIER mbBarrier{};
		mbBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		mbBarrier.Transition.pResource = motionBlurDestinationResource;
		mbBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		mbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		mbBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &mbBarrier);

		commandList->RSSetViewports(1, &fullViewport);
		commandList->RSSetScissorRects(1, &fullScissor);
		commandList->OMSetRenderTargets(1, &motionBlurDestinationRtvHandle, FALSE, nullptr);
		float mbClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		commandList->ClearRenderTargetView(motionBlurDestinationRtvHandle, mbClearColor, 0, nullptr);
		ID3D12DescriptorHeap* heaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(postProcessRootSignature.Get());
		commandList->SetPipelineState(motionBlurPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, g_temporalRenderingManager.GetVelocitySrvHandle());
		commandList->SetGraphicsRootDescriptorTable(3, depthSrvHandleGPU);
		float mbParams[12] = {
			ppSettings.cameraMotionBlurIntensity,
			12.0f,
			24.0f,
			48.0f,
			ppSettings.cameraNearClip,
			ppSettings.cameraFarClip,
			inverseRenderWidth,
			inverseRenderHeight,
			gameViewportOriginU,
			gameViewportOriginV,
			gameViewportWidthUv,
			gameViewportHeightUv
		};
		commandList->SetGraphicsRoot32BitConstants(2u, 12u, mbParams, 0u);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		mbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		mbBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &mbBarrier);
		hdrPostSourceSrvHandle = motionBlurDestinationSrvHandle;
		hdrPostSourceResource = motionBlurDestinationResource;
	}

	//================================================================
	// 1x1 の履歴 Texture へ画面平均露出を更新する
	//================================================================

	bool isAutoExposureExecuted = false;

	if (ppSettings.hasPostProcessComponent && ppSettings.compositeAutoExposureEnabled) {
		static std::chrono::steady_clock::time_point previousExposureTime =
			std::chrono::steady_clock::now();
		const std::chrono::steady_clock::time_point currentExposureTime =
			std::chrono::steady_clock::now();
		const float exposureDeltaTime = (std::clamp)(
			std::chrono::duration<float>(currentExposureTime - previousExposureTime).count(),
			1.0f / 240.0f,
			0.1f);
		previousExposureTime = currentExposureTime;
		const D3D12_VIEWPORT& exposureViewport = g_isGameViewVisible
			? gameViewport
			: viewport;
		const float inverseRenderWidth =
			1.0f / static_cast<float>((std::max)(g_renderWidth, 1u));
		const float inverseRenderHeight =
			1.0f / static_cast<float>((std::max)(g_renderHeight, 1u));
		isAutoExposureExecuted = g_postProcessQualityManager.ExecuteAutoExposure(
			commandList.Get(),
			hdrPostSourceSrvHandle,
			hdrPostSourceResource,
			ppSettings.compositeMinimumExposure,
			ppSettings.compositeMaximumExposure,
			ppSettings.compositeExposureAdaptationSpeed,
			ppSettings.compositeTargetLuminance,
			exposureDeltaTime,
			exposureViewport.TopLeftX * inverseRenderWidth,
			exposureViewport.TopLeftY * inverseRenderHeight,
			exposureViewport.Width * inverseRenderWidth,
			exposureViewport.Height * inverseRenderHeight);
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
		commandList->SetPipelineState(finalCompositePipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, hdrPostSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, finalBloomSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(3, ssaoSrvHandlesGPU[1]);
		commandList->SetGraphicsRootDescriptorTable(
			5u,
			g_postProcessQualityManager.GetAutoExposureSrvHandle());
		commandList->SetGraphicsRootDescriptorTable(6u, colorGradingLutSrvHandleGPU);
		commandList->SetGraphicsRootDescriptorTable(7u, depthSrvHandleGPU);

		static const std::chrono::steady_clock::time_point heatStartTime =
			std::chrono::steady_clock::now();
		const float heatElapsedTime = std::chrono::duration<float>(
			std::chrono::steady_clock::now() - heatStartTime).count();
		Vector3 sunScreenPosition = {0.5f, ppSettings.environmentHeatHorizonCenter, 0.0f};

		if (sunLightIndex >= 0) {
			const Vector3 sunWorldDirection = Multiply(
				-1.0f,
				directionalLightData[sunLightIndex].direction);
			const Vector3 sunWorldPosition = Add(
				activeCameraPosition,
				Multiply(10000.0f, sunWorldDirection));
			const Matrix4x4& activeViewProjectionMatrix = g_isSceneViewVisible
				? sceneViewProjectionMatrix
				: gameViewProjectionMatrix;
			const ClipSpacePoint sunClipPosition = TransformToClipSpace(
				sunWorldPosition,
				activeViewProjectionMatrix);

			if (sunClipPosition.w > 0.0001f) {
				const float inverseSunW = 1.0f / sunClipPosition.w;
				sunScreenPosition.x = sunClipPosition.x * inverseSunW * 0.5f + 0.5f;
				sunScreenPosition.y = 0.5f - sunClipPosition.y * inverseSunW * 0.5f;
			}
		}

		float finalCompositeParams[40] = {
			ppSettings.compositeExposure * ppSettings.finalBrightness,
			ppSettings.compositeWhitePoint,
			static_cast<float>(ppSettings.compositeToneMappingMode),
			isQualityBloomExecuted ? ppSettings.compositeBloomIntensity : 0.0f,
			ppSettings.compositeSaturation,
			ppSettings.compositeContrast,
			ppSettings.compositeVignetteStrength,
			ppSettings.compositeVignetteRadius,
			ppSettings.compositeFilmGrain,
			ppSettings.compositeChromaticAberration,
			ppSettings.compositeAmbientOcclusionStrength,
			static_cast<float>(ppSettings.compositeDebugView),
			ppSettings.glareColorByMode[bloomModeIndex].x,
			ppSettings.glareColorByMode[bloomModeIndex].y,
			ppSettings.glareColorByMode[bloomModeIndex].z,
			// Ocean Debug専用の後段停止は通常描画の反射改善と無関係なため、
			// FinalCompositeへは常に無効値を渡す。
			0.0f,
			isAutoExposureExecuted ? 1.0f : 0.0f,
			ppSettings.compositeTemperature,
			ppSettings.compositeTint,
			ppSettings.compositeGamma,
			ppSettings.compositeLift.x,
			ppSettings.compositeLift.y,
			ppSettings.compositeLift.z,
			0.0f,
			ppSettings.compositeGain.x,
			ppSettings.compositeGain.y,
			ppSettings.compositeGain.z,
			0.0f,
			(std::clamp)(ppSettings.compositeColorLutStrength, 0.0f, 1.0f),
			0.45f,
			ppSettings.compositeLocalContrast,
			ppSettings.compositeOutputDither,
			ppSettings.environmentHeatIntensity,
			ppSettings.environmentHeatHorizonCenter,
			ppSettings.environmentHeatHorizonWidth,
			ppSettings.environmentHeatSunInfluence,
			ppSettings.environmentHeatDistortionScale,
			heatElapsedTime,
			sunScreenPosition.x,
			sunScreenPosition.y
		};
		commandList->SetGraphicsRoot32BitConstants(2u, 40u, finalCompositeParams, 0u);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		postProcessBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		postProcessBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &postProcessBarrier);
	}

	//================================================================
	// Blender 風 Filter: ToneMapping 後の画面へ 3x3 畳み込みを適用する
	//================================================================

	D3D12_GPU_DESCRIPTOR_HANDLE filteredPostProcessSrvHandle = postProcessSrvHandleGPU;
	const int32_t filterModeMask = ppSettings.filterModeMask != 0
		? ppSettings.filterModeMask
		: (ppSettings.filterMode > 0 ? 1 << ppSettings.filterMode : 0);
	for (int32_t filterModeIndex = 1; filterModeIndex <= 8; filterModeIndex++) {
		if ((filterModeMask & (1 << filterModeIndex)) == 0) {
			continue;
		}

		const bool isFilterExecuted = g_postProcessQualityManager.ExecuteFilter(
			commandList.Get(),
			filteredPostProcessSrvHandle,
			filterModeIndex,
			ppSettings.filterStrengthByMode[static_cast<size_t>(filterModeIndex)],
			ppSettings.filterColorByMode[static_cast<size_t>(filterModeIndex)].x,
			ppSettings.filterColorByMode[static_cast<size_t>(filterModeIndex)].y,
			ppSettings.filterColorByMode[static_cast<size_t>(filterModeIndex)].z);

		if (isFilterExecuted) {
			filteredPostProcessSrvHandle = g_postProcessQualityManager.GetFilterSrvHandle();
		}
	}

	//================================================================
	// 闕ｳ・�E�E�E�: Sharpen
	// ToneMapping 陟募�E�E�E�後�E騾匁E�E��E��E�E�E�陷剁E�E��E�奁E�E��E�定氣莉｣・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�螟ｧ・�E�E�E�霈披�E�E�E�驍ｱ・�E�E�E�郢�E�E�E�竏壺�E�E�E�邵�E�E�E�竏ｵ諤咎お繝ｻFXAA 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜷�E�E�E��E�E�E�繝ｻ
	//================================================================
	bool isSharpenExecuted = false;

	if (ppSettings.hasPostProcessComponent &&
		ppSettings.sharpenStrength > 0.0f &&
		hdrCompositeRenderTarget != nullptr &&
		sharpenPipelineState != nullptr) {
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
		commandList->SetGraphicsRootDescriptorTable(0, filteredPostProcessSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, finalBloomSrvHandle);
		float sharpenParams[4] = {
			1.0f / static_cast<float>(g_renderWidth),
			1.0f / static_cast<float>(g_renderHeight),
			ppSettings.sharpenStrength,
			0.0f
		};
		commandList->SetGraphicsRoot32BitConstants(2, 4, sharpenParams, 0);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		sharpenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		sharpenBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &sharpenBarrier);
		isSharpenExecuted = true;
	}

	//================================================================
	// SMAA 3パスで輪郭検出、重み計算、近傍合成を順番に行う
	//================================================================
	D3D12_GPU_DESCRIPTOR_HANDLE finalAntialiasSourceSrvHandle = isSharpenExecuted
		? hdrCompositeSrvHandleGPU
		: filteredPostProcessSrvHandle;
	bool isSmaaExecuted = false;
	if (ppSettings.aaMode == 2) {
		isSmaaExecuted = g_postProcessQualityManager.ExecuteSmaa(
			commandList.Get(),
			finalAntialiasSourceSrvHandle,
			ppSettings.smaaThreshold,
			ppSettings.smaaCornerRounding);
	}

	if (isSmaaExecuted) {
		finalAntialiasSourceSrvHandle = g_postProcessQualityManager.GetSmaaOutputSrvHandle();
	}

	// Back buffer に出力（AAモードに応じてパスを排他制御）
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
		commandList->SetPipelineState(ppSettings.aaMode == 1 ? fxaaPipelineState.Get() : passthroughPipelineState.Get());
		commandList->SetGraphicsRootDescriptorTable(0, finalAntialiasSourceSrvHandle);
		commandList->SetGraphicsRootDescriptorTable(1, finalBloomSrvHandle);
		float fxaaParams[4];
		if (ppSettings.aaMode == 1) {
			fxaaParams[0] = 1.0f / static_cast<float>(g_renderWidth);
			fxaaParams[1] = 1.0f / static_cast<float>(g_renderHeight);
			fxaaParams[2] = 0.65f;
			fxaaParams[3] = 0.0312f;
		} else {
			fxaaParams[0] = 1.0f / static_cast<float>(g_renderWidth);
			fxaaParams[1] = 1.0f / static_cast<float>(g_renderHeight);
			fxaaParams[2] = 0.0f;
			fxaaParams[3] = 10.0f;
		}
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

	if (renderTimestampQueryHeap != nullptr && renderTimestampReadback != nullptr) {
		commandList->EndQuery(
			renderTimestampQueryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			1u);
		commandList->ResolveQueryData(
			renderTimestampQueryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			0u,
			2u,
			renderTimestampReadback.Get(),
			0u);
	}

	hr = commandList->Close(); // CommandList 郢�E�E�E�蟶晏陶邵�E�E�E�蛟･�E�E�E�・娜U 邵�E�E�E�・�E�E�E�陞ｳ貁E�E��E�・�E�E�E�蠕後堤�E�E�E��E�E�E�髦�E�E�E�・玖ｿ�E�E�E�・�E�E�E�隲�E�E�E�荵昶・驕抵�E�E�E��E�E�E�陞ｳ螢�E�E�E�笘�E�E�E�E��E�E�E�荵敖繝ｻ
	if (FAILED(hr)) {
		Log(g_logStream, std::format("CommandList Close failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		g_isDrawRequested = false;
		return;
	}

	// commandLists 邵�E�E�E�・�E�E�E� ExecuteCommandLists 邵�E�E�E�・�E�E�E�雋ゑ�E�E�E��E�E�E�邵�E�E�E�蜻守ｷ帝匁E�E��E��E�E�E� CommandList 鬩滓ｦ翫・邵�E�E�E�繝ｻ
	ID3D12CommandList* commandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, commandLists);

	if (hasRecordedShadowMapUpdate) {
		submittedShadowStateHash = shadowStateHash;
		hasSubmittedShadowMap = true;
	}

	hr = swapChain->Present(1, 0); // Present 邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E雋ょ現竏ｩ back buffer 郢�E�E�E�繝ｻWindow 邵�E�E�E�・�E�E�E�髯�E�E�E�・�E�E�E�驕会ｽ�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	if (FAILED(hr)) {
		Log(g_logStream, std::format("SwapChain Present failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		g_isDrawRequested = false;
		return;
	}

	if (!hasLoggedFirstPresent) {
		Log(g_logStream, "EditorRenderManager first present completed");
		hasLoggedFirstPresent = true;
	}

	fenceValue++; // fenceValue 郢�E�E�E�蟶敖・�E�E�E�郢�E�E�E�竏堋竏ｽ・�E�E�E�髮∝ｱ鍋ｸ�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E陞ｳ蠕｡・�E�E�E�繝ｻ・�E�E�E�蜥�E�E�E�・�E�E�E�・�E�E�E�郢�E�E�E�繝ｻGPU 邵�E�E�E�・�E�E�E�髫�E�E�E�蛟ｬ鮖ｸ邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	if (FAILED(hr)) {
		Log(g_logStream, std::format("CommandQueue Signal failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		g_isDrawRequested = false;
		return;
	}

	// GPU 邵�E�E�E�蠕｡・�E�E�E�髮∝ｱ鍋ｸ�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E郢�E�E�E�蝣�E�E�E�・�E�E�E�繧・斡郢�E�E�E�荵昶穐邵�E�E�E�・�E�E�E�陟輔�E笁E�E��E�邵�E�E�E�竏ｵ・�E�E�E�・�E�E�E�郢晁E�E��E�釁E�E��E��E�晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢晢�E�E�E��E�E�E�郢�E�E�E�・�E�E�E�郢�E�E�E�蜻亥�E�E�E�檎ｸ�E�E�E�閧�E�E�E�驪�E�E�E�邵�E�E�E�蛹�E�E�E�窶�E�E�E�郢�E�E�E�繧・�E�E�E��E�E�E�迚吶・邵�E�E�E�・�E�E�E�邵�E�E�E�蜷�E�E�E�・狗ｸ�E�E�E�繝ｻ
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		if (FAILED(hr)) {
			Log(g_logStream, std::format("Fence SetEventOnCompletion failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
			g_isDrawRequested = false;
			return;
		}
		if (fenceEvent != nullptr) {
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	}

	// GPU が完了したため、次フレームで使う可視結果を安全に読み戻す。
	if (renderTimestampReadback != nullptr && renderTimestampFrequency > 0u) {
		const D3D12_RANGE readRange{0u, sizeof(std::uint64_t) * 2u};
		std::uint64_t* timestampData = nullptr;

		if (SUCCEEDED(renderTimestampReadback->Map(
			0u,
			&readRange,
			reinterpret_cast<void**>(&timestampData))) && timestampData != nullptr) {
			if (timestampData[1] >= timestampData[0]) {
				const double elapsedTicks = static_cast<double>(timestampData[1] - timestampData[0]);
				const float measuredMilliseconds = static_cast<float>(
					elapsedTicks * 1000.0 / static_cast<double>(renderTimestampFrequency));
				renderProfile.gpuFrameMilliseconds = renderProfile.gpuFrameMilliseconds <= 0.0f
					? measuredMilliseconds
					: renderProfile.gpuFrameMilliseconds * 0.90f + measuredMilliseconds * 0.10f;
			}

			const D3D12_RANGE writeRange{0u, 0u};
			renderTimestampReadback->Unmap(0u, &writeRange);
		}
	}

	static int32_t videoMemoryQueryFrameTimer = 0;

	if (videoMemoryQueryFrameTimer <= 0 && useAdapter != nullptr) {
		DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo{};

		if (SUCCEEDED(useAdapter->QueryVideoMemoryInfo(
			0u,
			DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
			&videoMemoryInfo))) {
			renderProfile.localVideoMemoryUsage = videoMemoryInfo.CurrentUsage;
			renderProfile.localVideoMemoryBudget = videoMemoryInfo.Budget;
		}

		videoMemoryQueryFrameTimer = 30;
	}
	else {
		videoMemoryQueryFrameTimer--;
	}

	g_gpuCullingManager.ResolveReadback();
	g_oceanFftManager.ResolveReadback();

	g_isDrawRequested = false;
	// 闔�E�E�E�E��E�E�E�繝ｵ郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�郢晢�E�E�E��E�E�E�邵�E�E�E�・�E�E�E�隰�E�E�E�蜀怜�E髫補扱・�E�E�E�繧・�E�E�E�定ｱ�E�E�E�驛�E�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�蜉ｱ笳・�E�E�E��E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E�邵�E�E�E�竏ｵ・�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� ImGui::Render 邵�E�E�E�・�E�E�E�邵�E�E�E�・�E�E�E� Renderer 郢�E�E�E�蜻茨�E�E�E��E�E�E�・�E�E�E�郢�E�E�E�竏夲�E�E�E�狗ｸ�E�E�E�繝ｻ
}
