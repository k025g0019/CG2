#include "EditorSceneSynchronizer.h"

#include "EditorAnimationManager.h"
#include "EditorAssetUtility.h"
#include "EditorOceanSystem.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <unordered_map>

#pragma warning(disable : 5045)

namespace {
	constexpr float kOceanHorizonExpansion = 8.0f;

	struct SynchronizedGameObjectData {
		const EditorGameObject* gameObject = nullptr;
		const EditorComponent* modelRenderer = nullptr;
		const EditorComponent* skinnedMeshRenderer = nullptr;
		const EditorComponent* spriteRenderer = nullptr;
		const EditorComponent* meshFilter = nullptr;
		const EditorComponent* reflectionProbe = nullptr;
		const EditorComponent* ocean = nullptr;
		const EditorComponent* terrain = nullptr;
		const EditorComponent* foliage = nullptr;
		const EditorComponent* animation = nullptr;
		const EditorComponent* animator = nullptr;
	};

	SynchronizedGameObjectData CollectSynchronizedGameObjectData(const EditorGameObject& gameObject) {
		SynchronizedGameObjectData synchronizedData{};
		synchronizedData.gameObject = &gameObject;

		for (const EditorComponent& component : gameObject.components) {
			switch (component.type) {
			case EditorComponentType::ModelRenderer:
				synchronizedData.modelRenderer = &component;
				break;
			case EditorComponentType::SkinnedMeshRenderer:
				synchronizedData.skinnedMeshRenderer = &component;
				break;
			case EditorComponentType::SpriteRenderer:
				synchronizedData.spriteRenderer = &component;
				break;
			case EditorComponentType::MeshFilter:
				synchronizedData.meshFilter = &component;
				break;
			case EditorComponentType::ReflectionProbe:
				synchronizedData.reflectionProbe = &component;
				break;
			case EditorComponentType::Ocean:
				synchronizedData.ocean = &component;
				break;
			case EditorComponentType::Terrain:
				synchronizedData.terrain = &component;
				break;
			case EditorComponentType::Foliage:
				synchronizedData.foliage = &component;
				break;
			case EditorComponentType::Animation:
				synchronizedData.animation = &component;
				break;
			case EditorComponentType::Animator:
				synchronizedData.animator = &component;
				break;
			default:
				break;
			}
		}

		return synchronizedData;
	}

	float GetReflectionModeValue(const EditorComponent* reflectionProbeComponent) {
		if (reflectionProbeComponent == nullptr || !reflectionProbeComponent->isActive) {
			return 0.0f;
		}

		if (reflectionProbeComponent->assetPath == "Cubemap") {
			return 1.0f;
		}

		if (reflectionProbeComponent->assetPath == "Planar") {
			return 2.0f;
		}

		return 0.0f;
	}

	int32_t NormalizeOceanGridResolution(int32_t gridResolution) {
		const int32_t clampedResolution = (std::clamp)(gridResolution, 16, 2048);
		int32_t normalizedResolution = 16;

		while (normalizedResolution <= 1024 &&
			normalizedResolution * 2 <= clampedResolution) {
			normalizedResolution *= 2;
		}

		return normalizedResolution;
	}

	std::string BuildOceanMeshKey(
		int32_t gridResolution,
		float oceanSize,
		float maxWaveHeight) {
		const int32_t roundedSize = static_cast<int32_t>(std::round(oceanSize * 100.0f));
		const int32_t roundedMaxWaveHeight =
			static_cast<int32_t>(std::round(maxWaveHeight * 100.0f));
		return "builtin://Ocean/" + std::to_string(gridResolution) + "/" +
			std::to_string(roundedSize) + "/" + std::to_string(roundedMaxWaveHeight);
	}

	ModelData BuildOceanModelData(int32_t gridResolution, float oceanSize, float maxWaveHeight) {
		ModelData modelData{};
		const int32_t safeResolution = NormalizeOceanGridResolution(gridResolution);
		// 元FFT版の輪郭密度を保ちつつ、1024全面描画より軽い連続LODへ制限する。
		constexpr int32_t kMaximumPhysicalResolution = 512;
		const int32_t physicalResolution =
			(std::min)(safeResolution, kMaximumPhysicalResolution);
		const float safeSize = (std::max)(oceanSize, 1.0f);
		const float renderSize = safeSize * kOceanHorizonExpansion;
		const float halfSize = renderSize * 0.5f;
		const float nearDensityScale =
			static_cast<float>(physicalResolution) /
			static_cast<float>(safeResolution) /
			kOceanHorizonExpansion;
		const Vector3 upwardNormal{0.0f, 1.0f, 0.0f};
		const size_t vertexResolution = static_cast<size_t>(physicalResolution) + 1u;
		modelData.vertices.reserve(vertexResolution * vertexResolution);
		modelData.indices.reserve(
			static_cast<size_t>(physicalResolution) *
			static_cast<size_t>(physicalResolution) * 6u);

		auto makeVertex = [
			physicalResolution,
			halfSize,
			nearDensityScale,
			&upwardNormal](int32_t x, int32_t z) {
			const float u = static_cast<float>(x) / static_cast<float>(physicalResolution);
			const float v = 1.0f - static_cast<float>(z) / static_cast<float>(physicalResolution);
			const float normalizedX = u * 2.0f - 1.0f;
			const float normalizedZ = (1.0f - v) * 2.0f - 1.0f;
			const float squareRadius =
				(std::max)(std::abs(normalizedX), std::abs(normalizedZ));
			float warpScale = nearDensityScale;

			if (squareRadius > 0.000001f) {
				const float radiusSquared = squareRadius * squareRadius;
				const float warpedRadius =
					nearDensityScale * squareRadius +
					(1.0f - nearDensityScale) * radiusSquared * squareRadius;
				warpScale = warpedRadius / squareRadius;
			}

			return VertexData{
				{normalizedX * warpScale * halfSize, 0.0f,
				 normalizedZ * warpScale * halfSize, 1.0f},
				{u, v},
				upwardNormal};
		};

		// 近傍密度は指定値を維持し、外周だけを地平線方向へ広げた連続 LOD にする。
		for (int32_t z = 0; z <= physicalResolution; z++) {
			for (int32_t x = 0; x <= physicalResolution; x++) {
				modelData.vertices.push_back(makeVertex(x, z));
			}
		}

		const uint32_t rowVertexCount = static_cast<uint32_t>(physicalResolution + 1);

		for (int32_t z = 0; z < physicalResolution; z++) {
			for (int32_t x = 0; x < physicalResolution; x++) {
				const uint32_t topLeft =
					static_cast<uint32_t>(z) * rowVertexCount + static_cast<uint32_t>(x);
				const uint32_t bottomLeft = topLeft + rowVertexCount;
				const uint32_t topRight = topLeft + 1u;
				const uint32_t bottomRight = bottomLeft + 1u;

				modelData.indices.push_back(topLeft);
				modelData.indices.push_back(bottomLeft);
				modelData.indices.push_back(topRight);
				modelData.indices.push_back(topRight);
				modelData.indices.push_back(bottomLeft);
				modelData.indices.push_back(bottomRight);
			}
		}

		const float safeMaxWaveHeight = (std::max)(maxWaveHeight, 0.0f);
		modelData.localBoundsCenter = {0.0f, 0.0f, 0.0f};
		modelData.localBoundsSize = {renderSize, safeMaxWaveHeight * 2.0f, renderSize};
		return modelData;
	}

	std::string BuildTerrainMeshKey(
		int32_t gridResolution,
		const Vector3& terrainSize) {
		const int32_t roundedSizeX = static_cast<int32_t>(std::round(terrainSize.x * 100.0f));
		const int32_t roundedSizeY = static_cast<int32_t>(std::round(terrainSize.y * 100.0f));
		const int32_t roundedSizeZ = static_cast<int32_t>(std::round(terrainSize.z * 100.0f));
		return "builtin://Terrain/" + std::to_string(gridResolution) + "/" +
			std::to_string(roundedSizeX) + "/" +
			std::to_string(roundedSizeY) + "/" +
			std::to_string(roundedSizeZ);
	}

	ModelData BuildTerrainModelData(
		int32_t gridResolution,
		const Vector3& terrainSize,
		std::array<uint32_t, 3u>& lodVertexOffsets,
		std::array<uint32_t, 3u>& lodVertexCounts) {
		ModelData modelData{};
		const int32_t highResolution = (std::clamp)(gridResolution, 16, 256);
		const Vector3 safeTerrainSize = {
			(std::max)(terrainSize.x, 1.0f),
			(std::max)(terrainSize.y, 0.01f),
			(std::max)(terrainSize.z, 1.0f)};
		const Vector3 upwardNormal{0.0f, 1.0f, 0.0f};

		auto makeVertex = [&safeTerrainSize, &upwardNormal](
			int32_t x,
			int32_t z,
			int32_t resolution) {
			const float u = static_cast<float>(x) / static_cast<float>(resolution);
			const float v = static_cast<float>(z) / static_cast<float>(resolution);
			return VertexData{
				{(u - 0.5f) * safeTerrainSize.x,
				 0.0f,
				 (v - 0.5f) * safeTerrainSize.z,
				 1.0f},
				{u, 1.0f - v},
				upwardNormal};
		};

		for (size_t lodIndex = 0u; lodIndex < lodVertexCounts.size(); lodIndex++) {
			const int32_t lodResolution = (std::max)(
				highResolution >> static_cast<int32_t>(lodIndex),
				8);
			lodVertexOffsets[lodIndex] = static_cast<uint32_t>(modelData.vertices.size());

			for (int32_t z = 0; z < lodResolution; z++) {
				for (int32_t x = 0; x < lodResolution; x++) {
					const VertexData topLeft = makeVertex(x, z, lodResolution);
					const VertexData bottomLeft = makeVertex(x, z + 1, lodResolution);
					const VertexData topRight = makeVertex(x + 1, z, lodResolution);
					const VertexData bottomRight = makeVertex(x + 1, z + 1, lodResolution);
					modelData.vertices.push_back(topLeft);
					modelData.vertices.push_back(bottomLeft);
					modelData.vertices.push_back(topRight);
					modelData.vertices.push_back(topRight);
					modelData.vertices.push_back(bottomLeft);
					modelData.vertices.push_back(bottomRight);
				}
			}

			lodVertexCounts[lodIndex] =
				static_cast<uint32_t>(modelData.vertices.size()) - lodVertexOffsets[lodIndex];
		}

		modelData.localBoundsCenter = {0.0f, 0.0f, 0.0f};
		modelData.localBoundsSize = safeTerrainSize;
		return modelData;
	}

	bool AreOceanSpectrumInputsEqual(
		const EditorOceanRenderSettings& left,
		const EditorOceanRenderSettings& right) {
		return left.isEnabled == right.isEnabled &&
			left.waveHeight == right.waveHeight &&
			left.waveLength == right.waveLength &&
			left.primaryDirection.x == right.primaryDirection.x &&
			left.primaryDirection.y == right.primaryDirection.y &&
			left.secondaryDirection.x == right.secondaryDirection.x &&
			left.secondaryDirection.y == right.secondaryDirection.y &&
			left.secondaryWaveScale == right.secondaryWaveScale &&
			left.rippleScale == right.rippleScale &&
			left.rippleStrength == right.rippleStrength &&
			left.windSpeed == right.windSpeed &&
			left.waterDepth == right.waterDepth &&
			left.directionSpread == right.directionSpread &&
			left.swellStrength == right.swellStrength &&
			left.spectrumSeed == right.spectrumSeed;
	}

	bool AreOceanTransformationInputsEqual(
		const EditorOceanRenderSettings& left,
		const EditorOceanRenderSettings& right) {
		return AreOceanSpectrumInputsEqual(left, right) &&
			left.maxWaveHeight == right.maxWaveHeight &&
			left.waveSpeed == right.waveSpeed &&
			left.timeScale == right.timeScale &&
			left.choppiness == right.choppiness &&
			left.crestSharpness == right.crestSharpness;
	}

	void WriteOceanTransformationSettings(
		TransformationMatrix* transformationData,
		const EditorOceanRenderSettings& oceanSettings) {
		if (transformationData == nullptr) {
			return;
		}

		transformationData->oceanParams0 = {
			oceanSettings.isEnabled ? 1.0f : 0.0f,
			0.0f,
			oceanSettings.waveHeight,
			oceanSettings.maxWaveHeight};
		transformationData->oceanParams1 = {
			oceanSettings.primaryDirection.x,
			oceanSettings.primaryDirection.y,
			oceanSettings.waveLength,
			oceanSettings.waveSpeed};
		transformationData->oceanParams2 = {
			oceanSettings.secondaryDirection.x,
			oceanSettings.secondaryDirection.y,
			oceanSettings.secondaryWaveScale,
			oceanSettings.choppiness};
		transformationData->oceanParams3 = {
			oceanSettings.rippleScale,
			oceanSettings.rippleStrength,
			oceanSettings.timeScale,
			oceanSettings.size};
		transformationData->oceanParams4 = {
			oceanSettings.windSpeed,
			oceanSettings.waterDepth,
			oceanSettings.directionSpread,
			oceanSettings.swellStrength};
		transformationData->oceanParams5 = {
			oceanSettings.spectrumSeed,
			oceanSettings.crestSharpness,
			0.0f,
			0.0f};
		transformationData->oceanWaveData0 = oceanSettings.waveData0;
		transformationData->oceanWaveData1 = oceanSettings.waveData1;
	}

	void ApplyOceanSettings(EditorSceneObject& sceneObject, const EditorComponent* oceanComponent) {

		if (oceanComponent == nullptr || !oceanComponent->isActive) {
			if (sceneObject.ocean.isEnabled) {
				sceneObject.ocean = {};
				WriteOceanTransformationSettings(sceneObject.transformationData, sceneObject.ocean);
				WriteOceanTransformationSettings(sceneObject.gameTransformationData, sceneObject.ocean);
			}

			if (sceneObject.materialData != nullptr) {
				sceneObject.materialData->oceanEnabled = 0.0f;
			}
			return;
		}

		const EditorOceanRenderSettings previousOceanSettings = sceneObject.ocean;

		sceneObject.ocean.isEnabled = true;
		sceneObject.ocean.gridResolution = NormalizeOceanGridResolution(oceanComponent->oceanGridResolution);
		sceneObject.ocean.size = (std::max)(oceanComponent->oceanSize, 1.0f);
		sceneObject.ocean.waveHeight = (std::max)(oceanComponent->oceanWaveHeight, 0.0f);
		sceneObject.ocean.maxWaveHeight = (std::max)(oceanComponent->oceanMaxWaveHeight, 0.0f);
		sceneObject.ocean.waveLength = (std::max)(oceanComponent->oceanWaveLength, 0.1f);
		sceneObject.ocean.waveSpeed = oceanComponent->oceanWaveSpeed;
		sceneObject.ocean.timeScale = oceanComponent->oceanTimeScale;
		sceneObject.ocean.choppiness = oceanComponent->oceanChoppiness;
		sceneObject.ocean.primaryDirection = {
			oceanComponent->oceanPrimaryDirection.x,
			oceanComponent->oceanPrimaryDirection.y};
		sceneObject.ocean.secondaryDirection = {
			oceanComponent->oceanSecondaryDirection.x,
			oceanComponent->oceanSecondaryDirection.y};
		sceneObject.ocean.secondaryWaveScale = oceanComponent->oceanSecondaryWaveScale;
		sceneObject.ocean.rippleScale = oceanComponent->oceanRippleScale;
		sceneObject.ocean.rippleStrength = oceanComponent->oceanRippleStrength;
		sceneObject.ocean.windSpeed = (std::max)(oceanComponent->oceanWindSpeed, 0.1f);
		sceneObject.ocean.waterDepth = (std::max)(oceanComponent->oceanWaterDepth, 0.1f);
		sceneObject.ocean.directionSpread = (std::max)(oceanComponent->oceanDirectionSpread, 0.0f);
		sceneObject.ocean.swellStrength = (std::max)(oceanComponent->oceanSwellStrength, 0.0f);
		sceneObject.ocean.spectrumSeed = (std::max)(oceanComponent->oceanSpectrumSeed, 0.0f);
		sceneObject.ocean.crestSharpness =
			(std::clamp)(oceanComponent->oceanCrestSharpness, 0.0f, 1.0f);
		sceneObject.ocean.foamStrength = oceanComponent->oceanFoamStrength;
		sceneObject.ocean.foamThreshold =
			(std::clamp)(oceanComponent->oceanFoamThreshold, 0.0f, 1.0f);
		sceneObject.ocean.roughness = oceanComponent->oceanRoughness;
		sceneObject.ocean.reflectionStrength = oceanComponent->oceanReflectionStrength;
		sceneObject.ocean.detailNormalStrength =
			(std::max)(oceanComponent->oceanDetailNormalStrength, 0.0f);
		sceneObject.ocean.absorptionDistance =
			(std::max)(oceanComponent->oceanAbsorptionDistance, 0.1f);
		sceneObject.ocean.refractionDistortion =
			(std::max)(oceanComponent->oceanRefractionDistortion, 0.0f);
		sceneObject.ocean.transmission =
			(std::clamp)(oceanComponent->transmission, 0.0f, 1.0f);
		sceneObject.ocean.shallowColor = oceanComponent->oceanShallowColor;
		sceneObject.ocean.deepColor = oceanComponent->oceanDeepColor;

		if (!AreOceanSpectrumInputsEqual(previousOceanSettings, sceneObject.ocean)) {
			const EditorOceanSpectrumSettings spectrumSettings =
				BuildEditorOceanSpectrumSettings(*oceanComponent);
			std::array<EditorOceanWaveParameter, kEditorOceanWaveCount> waveParameters{};
			BuildEditorOceanSpectrum(spectrumSettings, waveParameters);

			for (size_t waveIndex = 0u; waveIndex < kEditorOceanWaveCount; waveIndex++) {
				const EditorOceanWaveParameter& waveParameter = waveParameters[waveIndex];
				sceneObject.ocean.waveData0[waveIndex] = {
					waveParameter.direction.x,
					waveParameter.direction.y,
					waveParameter.waveNumber,
					waveParameter.amplitude};
				sceneObject.ocean.waveData1[waveIndex] = {
					waveParameter.angularFrequency,
					waveParameter.phaseOffset,
					0.0f,
					0.0f};
			}
		}

		if (!AreOceanTransformationInputsEqual(previousOceanSettings, sceneObject.ocean)) {
			WriteOceanTransformationSettings(sceneObject.transformationData, sceneObject.ocean);
			WriteOceanTransformationSettings(sceneObject.gameTransformationData, sceneObject.ocean);
		}

		if (sceneObject.materialData == nullptr) {
			return;
		}

		sceneObject.materialData->color = {
			sceneObject.ocean.shallowColor.x,
			sceneObject.ocean.shallowColor.y,
			sceneObject.ocean.shallowColor.z,
			1.0f};
		sceneObject.materialData->enableLighting = 3;
		sceneObject.materialData->useTexture = FALSE;
		sceneObject.materialData->metallic = 0.0f;
		sceneObject.materialData->roughness = sceneObject.ocean.roughness;
		sceneObject.materialData->reflectance = sceneObject.ocean.reflectionStrength;
		sceneObject.materialData->ior = 1.333f;
		sceneObject.materialData->clearCoat = 1.0f;
		sceneObject.materialData->clearCoatRoughness = 0.05f;
		sceneObject.materialData->transmission =
			sceneObject.ocean.transmission;
		sceneObject.materialData->doubleSided = TRUE;
		sceneObject.materialData->oceanEnabled = 1.0f;
		sceneObject.materialData->oceanFoamStrength = sceneObject.ocean.foamStrength;
		sceneObject.materialData->oceanRoughness = sceneObject.ocean.roughness;
		sceneObject.materialData->oceanColorBlendScale = 1.0f;
		sceneObject.materialData->oceanDeepColor = sceneObject.ocean.deepColor;
		sceneObject.materialData->oceanDetailNormalStrength = sceneObject.ocean.detailNormalStrength;
		sceneObject.materialData->oceanFoamThreshold = sceneObject.ocean.foamThreshold;
		sceneObject.materialData->oceanAbsorptionDistance = sceneObject.ocean.absorptionDistance;
		sceneObject.materialData->oceanRefractionDistortion = sceneObject.ocean.refractionDistortion;
		sceneObject.materialData->oceanWaterDepth = sceneObject.ocean.waterDepth;
		sceneObject.materialData->oceanCrestSharpness = sceneObject.ocean.crestSharpness;
	}

	void ApplyRendererMaterial(
		EditorSceneObject& sceneObject,
		const EditorComponent* rendererComponent,
		const EditorComponent* reflectionProbeComponent,
		bool isModelRenderer) {
		if (sceneObject.materialData == nullptr) {
			return;
		}

		Vector3 rendererColor = {1.0f, 1.0f, 1.0f};  // Renderer が未設定でも Mesh は白色で表示する。
		float rendererIntensity = 1.0f;  // 強さは色へ掛ける簡易 Material パラメータ。

		if (rendererComponent != nullptr) {
			rendererColor = rendererComponent->color;
			rendererIntensity = rendererComponent->intensity;
		}

		sceneObject.materialData->color = {
			rendererColor.x * rendererIntensity,
			rendererColor.y * rendererIntensity,
			rendererColor.z * rendererIntensity,
			rendererComponent != nullptr ? rendererComponent->alpha : 1.0f};
		const int32_t lightingMode = rendererComponent != nullptr
			? (std::clamp)(rendererComponent->lightingMode, 0, 3)
			: 3;
		sceneObject.materialData->enableLighting = isModelRenderer ? lightingMode : 0;
		sceneObject.materialData->useTexture = isModelRenderer ? FALSE : TRUE;  // Mesh は初期状態を白い面、Sprite は画像表示にする。
		sceneObject.materialData->metallic = rendererComponent != nullptr ? rendererComponent->metallic : 0.0f;
		sceneObject.materialData->roughness = rendererComponent != nullptr ? rendererComponent->roughness : 0.5f;
		sceneObject.materialData->reflectance =
			rendererComponent != nullptr ? rendererComponent->reflectionStrength : 0.0f;
		sceneObject.materialData->ior = rendererComponent != nullptr ? rendererComponent->ior : 1.0f;
		sceneObject.materialData->emissionStrength = rendererComponent != nullptr ? rendererComponent->emissionStrength : 0.0f;
		sceneObject.materialData->emissionColor =
			rendererComponent != nullptr ? rendererComponent->emissionColor : Vector3{1.0f, 1.0f, 1.0f};
		sceneObject.materialData->normalScale = rendererComponent != nullptr ? rendererComponent->normalScale : 1.0f;
		sceneObject.materialData->ambientOcclusionStrength =
			rendererComponent != nullptr ? rendererComponent->ambientOcclusionStrength : 1.0f;
		sceneObject.materialData->heightScale = rendererComponent != nullptr ? rendererComponent->heightScale : 0.02f;
		sceneObject.materialData->alphaCutoff = rendererComponent != nullptr ? rendererComponent->alphaCutoff : 0.5f;
		sceneObject.materialData->clearCoat = rendererComponent != nullptr ? rendererComponent->clearCoat : 0.0f;
		sceneObject.materialData->clearCoatRoughness =
			rendererComponent != nullptr ? rendererComponent->clearCoatRoughness : 0.1f;
		sceneObject.materialData->transmission = rendererComponent != nullptr ? rendererComponent->transmission : 0.0f;
		sceneObject.materialData->subsurface = rendererComponent != nullptr ? rendererComponent->subsurface : 0.0f;
		sceneObject.materialData->anisotropy = rendererComponent != nullptr ? rendererComponent->anisotropy : 0.0f;
		sceneObject.materialData->anisotropyRotation =
			rendererComponent != nullptr ? rendererComponent->anisotropyRotation : 0.0f;
		sceneObject.materialData->specularTint = rendererComponent != nullptr ? rendererComponent->specularTint : 0.0f;
		sceneObject.materialData->sheen = rendererComponent != nullptr ? rendererComponent->sheen : 0.0f;
		sceneObject.materialData->sheenTint = rendererComponent != nullptr ? rendererComponent->sheenTint : 0.5f;
		sceneObject.materialData->alphaMode = rendererComponent != nullptr ? rendererComponent->alphaMode : 0;
		sceneObject.materialData->doubleSided =
			rendererComponent != nullptr && rendererComponent->doubleSided ? TRUE : FALSE;
		sceneObject.materialData->uvTiling = rendererComponent != nullptr
			? Vector2{rendererComponent->uvTiling.x, rendererComponent->uvTiling.y}
			: Vector2{1.0f, 1.0f};
		sceneObject.materialData->uvOffset = rendererComponent != nullptr
			? Vector2{rendererComponent->uvOffset.x, rendererComponent->uvOffset.y}
			: Vector2{0.0f, 0.0f};
		sceneObject.materialData->reflectionMode = GetReflectionModeValue(reflectionProbeComponent);
		sceneObject.materialData->reflectionProbeIntensity =
			reflectionProbeComponent != nullptr && reflectionProbeComponent->isActive
			? reflectionProbeComponent->intensity
			: 0.0f;
		sceneObject.materialData->reflectionReserved =
			reflectionProbeComponent != nullptr && reflectionProbeComponent->isActive
			? reflectionProbeComponent->roughness
			: 0.0f;

		//============================================================
		// Cubemap Reflection Probe の Box Projection 範囲
		//============================================================

		const bool isCubemapProbeActive =
			reflectionProbeComponent != nullptr &&
			reflectionProbeComponent->isActive &&
			reflectionProbeComponent->assetPath == "Cubemap";

		if (isCubemapProbeActive) {
			const Vector3& probeCenter = reflectionProbeComponent->colliderCenter;
			const Vector3& probeSize = reflectionProbeComponent->colliderSize;
			const Vector3& objectScale = sceneObject.transform.scale;
			sceneObject.materialData->reflectionProbeCenter = {
				sceneObject.transform.translate.x + probeCenter.x * objectScale.x,
				sceneObject.transform.translate.y + probeCenter.y * objectScale.y,
				sceneObject.transform.translate.z + probeCenter.z * objectScale.z};
			sceneObject.materialData->reflectionProbeExtent = {
				(std::max)(std::abs(probeSize.x * objectScale.x) * 0.5f, 0.01f),
				(std::max)(std::abs(probeSize.y * objectScale.y) * 0.5f, 0.01f),
				(std::max)(std::abs(probeSize.z * objectScale.z) * 0.5f, 0.01f)};
			sceneObject.materialData->reflectionProbeBoxProjection = 1.0f;
		}
		else {
			sceneObject.materialData->reflectionProbeCenter = {0.0f, 0.0f, 0.0f};
			sceneObject.materialData->reflectionProbeExtent = {1.0f, 1.0f, 1.0f};
			sceneObject.materialData->reflectionProbeBoxProjection = 0.0f;
		}
	}
}

void EditorSceneSynchronizer::Initialize(
	EditorScene* editorScene,
	EditorSceneObjectManager* sceneObjectManager,
	EditorAnimationManager* animationManager) {
	editorScene_ = editorScene;  // GameObject を正として、描画用 SceneObject を作るため保持する
	sceneObjectManager_ = sceneObjectManager;
	animationManager_ = animationManager;
}

void EditorSceneSynchronizer::Update(
	const std::vector<std::string>& textureFilePaths,
	int32_t& selectedPlacedSceneObjectIndex) {
	if (editorScene_ == nullptr || sceneObjectManager_ == nullptr) {
		return;
	}

	std::vector<EditorSceneObject>& sceneObjects = sceneObjectManager_->GetSceneObjects();  // 描画用 SceneObject 配列を直接編集する
	const std::vector<EditorGameObject>& gameObjects = editorScene_->GetGameObjects();
	std::vector<SynchronizedGameObjectData> synchronizedGameObjects;
	std::unordered_map<int32_t, size_t> synchronizedGameObjectIndices;
	synchronizedGameObjects.reserve(gameObjects.size());
	synchronizedGameObjectIndices.reserve(gameObjects.size());

	// Component 配列はここで 1 度だけ走査し、以降は ID 索引から参照する。
	for (const EditorGameObject& gameObject : gameObjects) {
		const size_t synchronizedIndex = synchronizedGameObjects.size();
		synchronizedGameObjects.push_back(CollectSynchronizedGameObjectData(gameObject));
		synchronizedGameObjectIndices.emplace(gameObject.id, synchronizedIndex);
	}

	// 後ろから削除することで erase 後の index ずれを避ける
	for (int32_t sceneObjectIndex = static_cast<int32_t>(sceneObjects.size()) - 1;
	     sceneObjectIndex >= 0;
	     sceneObjectIndex--) {
		const EditorSceneObject& sceneObject =
			sceneObjects[static_cast<size_t>(sceneObjectIndex)];
		// 紐づく GameObject が消えていないか ID 索引から確認する
		const auto synchronizedGameObjectIterator =
			synchronizedGameObjectIndices.find(sceneObject.gameObjectId);
		bool shouldRemove = synchronizedGameObjectIterator == synchronizedGameObjectIndices.end();

		// Renderer Component が外された SceneObject は描画対象から消す
		if (!shouldRemove) {
			const SynchronizedGameObjectData& synchronizedData =
				synchronizedGameObjects[synchronizedGameObjectIterator->second];

			if (sceneObject.type == EditorSceneObjectType::Model) {
				const bool hasActiveOcean =
					synchronizedData.gameObject->isActive &&
					synchronizedData.ocean != nullptr &&
					synchronizedData.ocean->isActive;
				const bool hasActiveTerrain =
					synchronizedData.gameObject->isActive &&
					synchronizedData.terrain != nullptr &&
					synchronizedData.terrain->isActive;
				shouldRemove = synchronizedData.modelRenderer == nullptr &&
					synchronizedData.skinnedMeshRenderer == nullptr &&
					!hasActiveOcean &&
					!hasActiveTerrain;
			}
			else {
				shouldRemove = synchronizedData.spriteRenderer == nullptr;
			}
		}

		// 消す必要がない SceneObject は残す
		if (!shouldRemove) {
			continue;
		}

		sceneObjectManager_->ReleaseObject(sceneObjectIndex);  // GPU Resource を解放して配列から消す
		sceneObjects.erase(sceneObjects.begin() + sceneObjectIndex);

		// 選択中の SceneObject が消えた場合は選択解除する
		if (selectedPlacedSceneObjectIndex == sceneObjectIndex) {
			selectedPlacedSceneObjectIndex = -1;
		}
		else if (selectedPlacedSceneObjectIndex > sceneObjectIndex) {
			selectedPlacedSceneObjectIndex--;
		}
	}

	std::unordered_map<int32_t, int32_t> sceneObjectIndices;
	sceneObjectIndices.reserve(sceneObjects.size() + synchronizedGameObjects.size());

	for (int32_t sceneObjectIndex = 0;
		 sceneObjectIndex < static_cast<int32_t>(sceneObjects.size());
		 sceneObjectIndex++) {
		sceneObjectIndices.emplace(
			sceneObjects[static_cast<size_t>(sceneObjectIndex)].gameObjectId,
			sceneObjectIndex);
	}

	// GameObject 側に Renderer があれば、対応する SceneObject を作る / 更新する
	for (const SynchronizedGameObjectData& synchronizedData : synchronizedGameObjects) {
		const EditorGameObject& gameObject = *synchronizedData.gameObject;
		const EditorComponent* skinnedMeshRenderer = synchronizedData.skinnedMeshRenderer;
		const EditorComponent* modelRenderer = synchronizedData.modelRenderer != nullptr
			? synchronizedData.modelRenderer
			: skinnedMeshRenderer;
		const EditorComponent* spriteRenderer = synchronizedData.spriteRenderer;
		const EditorComponent* meshFilter = synchronizedData.meshFilter;
		const EditorComponent* reflectionProbe = synchronizedData.reflectionProbe;
		const EditorComponent* ocean = synchronizedData.ocean;
		const EditorComponent* terrain = synchronizedData.terrain;
		const EditorComponent* foliage = synchronizedData.foliage;
		const EditorComponent* animation = synchronizedData.animation;
		const EditorComponent* animator = synchronizedData.animator;
		const bool hasOcean = gameObject.isActive && ocean != nullptr && ocean->isActive;
		const bool hasTerrain = gameObject.isActive && terrain != nullptr && terrain->isActive;
		const bool hasModelRenderer = modelRenderer != nullptr || hasOcean || hasTerrain;
		const bool hasSpriteRenderer = spriteRenderer != nullptr;

		if (!hasModelRenderer && !hasSpriteRenderer) {
			continue;
		}

		// ModelRenderer を優先し、なければ SpriteRenderer として扱う
		EditorSceneObjectType sceneObjectType =
			hasModelRenderer ? EditorSceneObjectType::Model : EditorSceneObjectType::Sprite;
		int32_t sceneObjectIndex = -1;

		// 既に GameObject と紐づく SceneObject があるか ID 索引から探す
		const auto sceneObjectIterator = sceneObjectIndices.find(gameObject.id);

		if (sceneObjectIterator != sceneObjectIndices.end()) {
			sceneObjectIndex = sceneObjectIterator->second;
		}

		if (sceneObjectIndex < 0) {
			int32_t textureIndex = 2;  // モデルは checker texture、スプライトは SpriteRenderer の texture を使う
			EditorModelMeshType meshType = EditorModelMeshType::Plane;  // ModelRenderer の assetPath から基本形を選ぶ
			if (sceneObjectType == EditorSceneObjectType::Sprite) {
				textureIndex = 0;
				if (spriteRenderer != nullptr && !spriteRenderer->assetPath.empty()) {
					// assetPath が登録済み texture にあればその番号を使う
					int32_t foundTextureIndex =
						EditorAssetUtility::GetTextureIndex(textureFilePaths, spriteRenderer->assetPath);
					if (foundTextureIndex >= 0) {
						textureIndex = foundTextureIndex;
					}
				}
			}
			else {
				if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
					meshType = EditorAssetUtility::GetModelMeshType(modelRenderer->assetPath);
				}
				else if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
					meshType = EditorAssetUtility::GetModelMeshType(meshFilter->assetPath);
				}
			}

			sceneObjectIndex = sceneObjectManager_->CreateObject(
				sceneObjectType,
				textureIndex,
				Transforms{gameObject.scale, gameObject.rotate, gameObject.translate},
				gameObject.name);
			if (sceneObjectIndex < 0) {
				continue;
			}

			sceneObjects[static_cast<size_t>(sceneObjectIndex)].gameObjectId = gameObject.id;  // 作成した SceneObject と GameObject を ID で紐づける
			sceneObjects[static_cast<size_t>(sceneObjectIndex)].meshType = meshType;
			sceneObjectIndices.emplace(gameObject.id, sceneObjectIndex);
		}

		// GameObject の Transform を描画用 SceneObject へコピーする
		EditorSceneObject& sceneObject =
			sceneObjects[static_cast<size_t>(sceneObjectIndex)];
		sceneObject.transform.translate = gameObject.translate;
		sceneObject.transform.rotate = gameObject.rotate;
		sceneObject.transform.scale = gameObject.scale;
		sceneObject.name = gameObject.name;
		sceneObject.surface = {};

		if (terrain != nullptr && terrain->isActive) {
			sceneObject.surface.mode = 1;
			sceneObject.surface.areaSize = {
				(std::max)(terrain->colliderSize.x, 1.0f),
				(std::max)(terrain->colliderSize.z, 1.0f)};
			sceneObject.surface.heightScale =
				(std::max)(terrain->colliderSize.y, 0.01f);
			sceneObject.surface.hasHeightOrDensityMap =
				!terrain->assetPath.empty() ||
				(modelRenderer != nullptr && !modelRenderer->heightTextureAssetPath.empty());
		}

		if (foliage != nullptr && foliage->isActive) {
			sceneObject.surface.mode = 2;
			sceneObject.surface.windStrength = (std::max)(foliage->oceanWaveHeight, 0.0f);
			sceneObject.surface.windSpeed = (std::max)(foliage->oceanWindSpeed, 0.0f) * 0.1f;
			sceneObject.surface.windSpatialScale =
				1.0f / (std::max)(foliage->oceanWaveLength, 0.01f);
			sceneObject.surface.timeScale = (std::max)(foliage->oceanTimeScale, 0.0f);
			sceneObject.surface.windDirection = {
				foliage->oceanPrimaryDirection.x,
				foliage->oceanPrimaryDirection.y};
			sceneObject.surface.areaSize = {
				(std::max)(foliage->colliderSize.x, 1.0f),
				(std::max)(foliage->colliderSize.z, 1.0f)};
			sceneObject.surface.density = (std::clamp)(foliage->intensity, 0.0f, 1.0f);
			sceneObject.surface.lodDistance = (std::max)(foliage->colliderRadius, 1.0f);
			sceneObject.surface.instanceCount = static_cast<uint32_t>((std::clamp)(
				foliage->particleMaxCount,
				1,
				65535));
			sceneObject.surface.hasHeightOrDensityMap = !foliage->assetPath.empty();
		}

		// Material ConstantBuffer の Map に失敗した SceneObject は描画更新を中止する。
		// ここを通したまま materialData を参照すると null 読み取りで落ちる。
		if (sceneObject.materialData == nullptr) {
			sceneObjectManager_->ClearCustomTexture(sceneObjectIndex);
			sceneObjectManager_->ClearAllMaterialTextures(sceneObjectIndex);
			continue;
		}

		sceneObject.materialData->surfaceMode = sceneObject.surface.mode;

		if (sceneObject.type == EditorSceneObjectType::Sprite) {
			// Sprite は SpriteRenderer の assetPath に合わせて textureIndex を更新する
			ApplyRendererMaterial(sceneObject, spriteRenderer, reflectionProbe, false);
			if (spriteRenderer != nullptr && !spriteRenderer->assetPath.empty()) {
				int32_t foundTextureIndex =
					EditorAssetUtility::GetTextureIndex(textureFilePaths, spriteRenderer->assetPath);
				if (foundTextureIndex >= 0) {
					sceneObject.textureIndex = foundTextureIndex;
				}
			}

			if (spriteRenderer != nullptr &&
				!spriteRenderer->textureAssetPath.empty()) {
				sceneObjectManager_->SetCustomTexture(sceneObjectIndex, spriteRenderer->textureAssetPath);
				sceneObject.materialData->useTexture = TRUE;
			}
			else {
				sceneObjectManager_->ClearCustomTexture(sceneObjectIndex);
			}
		}
		else {
			std::string modelAssetPath;  // 実メッシュ描画に使うモデルパス。ModelRenderer を優先し、なければ MeshFilter を使う。
			const ModelData* modelData = nullptr;  // キャッシュをコピーせず、描画に必要な Mesh / Material だけを参照する。
			ApplyRendererMaterial(sceneObject, modelRenderer, reflectionProbe, true);
			ApplyOceanSettings(sceneObject, hasOcean ? ocean : nullptr);
			sceneObject.textureIndex = 2;  // Shader には Texture SRV が必要なので渡すが、Model Material 側では Texture を無効化する。

			std::string rendererTexturePath;
			if (modelRenderer != nullptr && !modelRenderer->textureAssetPath.empty()) {
				rendererTexturePath = modelRenderer->textureAssetPath;
			}

			if (hasOcean) {
				modelAssetPath = BuildOceanMeshKey(
					sceneObject.ocean.gridResolution,
					sceneObject.ocean.size,
					sceneObject.ocean.maxWaveHeight);
				sceneObject.meshType = EditorModelMeshType::Plane;
			}
			else if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
				modelAssetPath = modelRenderer->assetPath;
				sceneObject.meshType = EditorAssetUtility::GetModelMeshType(modelRenderer->assetPath);
			}
			else if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
				modelAssetPath = meshFilter->assetPath;
				sceneObject.meshType = EditorAssetUtility::GetModelMeshType(meshFilter->assetPath);
			}
			else {
				sceneObject.meshType = EditorModelMeshType::Plane;
			}

			if (!modelAssetPath.empty() && !hasOcean) {
				modelData = EditorAssetUtility::GetSharedModelAssetData(
					modelAssetPath,
					skinnedMeshRenderer != nullptr);
			}

			// Renderer 側で画像未指定なら、FBX / OBJ が持つ元マテリアルの画像をそのまま使う。
			if (rendererTexturePath.empty() &&
				modelRenderer != nullptr &&
				modelRenderer->useImportedMaterialTextures &&
				modelData != nullptr &&
				!modelData->material.textureFilePath.empty()) {
				rendererTexturePath = modelData->material.textureFilePath;
			}

			if (!rendererTexturePath.empty() &&
				sceneObjectManager_->SetCustomTexture(sceneObjectIndex, rendererTexturePath)) {
				sceneObject.materialData->useTexture = TRUE;
			}
			else {
				sceneObjectManager_->ClearCustomTexture(sceneObjectIndex);
				sceneObject.materialData->useTexture = FALSE;
			}

			//============================================================
			// PBR Map を個別 SRV へ同期
			//============================================================

			auto synchronizeMaterialTexture = [this, sceneObjectIndex](
				EditorMaterialTextureSlot textureSlot,
				const std::string& textureAssetPath,
				int32_t& useTextureFlag) {
				if (!textureAssetPath.empty() &&
					sceneObjectManager_->SetMaterialTexture(sceneObjectIndex, textureSlot, textureAssetPath)) {
					useTextureFlag = TRUE;
					return;
				}

				sceneObjectManager_->ClearMaterialTexture(sceneObjectIndex, textureSlot);
				useTextureFlag = FALSE;
			};

			const bool useImportedMaterialTextures =
				modelRenderer != nullptr && modelRenderer->useImportedMaterialTextures;
			auto selectMaterialTexturePath = [useImportedMaterialTextures](
				const std::string& manualTexturePath,
				const std::string& importedTexturePath) -> const std::string& {
				// Inspector で指定した画像を優先し、空欄の場合だけ FBX 内の画像を使う。
				return !manualTexturePath.empty() || !useImportedMaterialTextures
					? manualTexturePath
					: importedTexturePath;
			};

			const EditorComponent emptyRenderer{};
			const EditorComponent& materialComponent = modelRenderer != nullptr ? *modelRenderer : emptyRenderer;
			const MaterialData emptyMaterial{};
			const MaterialData& importedMaterial = modelData != nullptr ? modelData->material : emptyMaterial;
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Normal,
				selectMaterialTexturePath(materialComponent.normalTextureAssetPath, importedMaterial.normalTextureFilePath),
				sceneObject.materialData->useNormalMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Metallic,
				selectMaterialTexturePath(materialComponent.metallicTextureAssetPath, importedMaterial.metallicTextureFilePath),
				sceneObject.materialData->useMetallicMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Roughness,
				selectMaterialTexturePath(materialComponent.roughnessTextureAssetPath, importedMaterial.roughnessTextureFilePath),
				sceneObject.materialData->useRoughnessMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::AmbientOcclusion,
				selectMaterialTexturePath(materialComponent.ambientOcclusionTextureAssetPath, importedMaterial.ambientOcclusionTextureFilePath),
				sceneObject.materialData->useAmbientOcclusionMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Emission,
				selectMaterialTexturePath(materialComponent.emissionTextureAssetPath, importedMaterial.emissionTextureFilePath),
				sceneObject.materialData->useEmissionMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Height,
				selectMaterialTexturePath(
					sceneObject.surface.mode == 1 && terrain != nullptr && !terrain->assetPath.empty()
						? terrain->assetPath
						: (sceneObject.surface.mode == 2 && foliage != nullptr && !foliage->assetPath.empty()
							? foliage->assetPath
							: materialComponent.heightTextureAssetPath),
					importedMaterial.heightTextureFilePath),
				sceneObject.materialData->useHeightMap);
			synchronizeMaterialTexture(
				EditorMaterialTextureSlot::Opacity,
				selectMaterialTexturePath(materialComponent.opacityTextureAssetPath, importedMaterial.opacityTextureFilePath),
				sceneObject.materialData->useOpacityMap);

			// Terrain / Foliage の t12 は頂点変位・密度専用。Pixel 視差との二重適用を止める。
			if (sceneObject.surface.mode != 0) {
				sceneObject.materialData->useHeightMap = FALSE;
			}

			if (hasOcean) {
				if (sceneObject.assetPath != modelAssetPath || !sceneObject.usesCustomMesh) {
					const ModelData oceanModelData = BuildOceanModelData(
						sceneObject.ocean.gridResolution,
						sceneObject.ocean.size,
						sceneObject.ocean.maxWaveHeight);
					sceneObjectManager_->SetCustomModelMesh(sceneObjectIndex, modelAssetPath, oceanModelData);
				}
			}
			else if (sceneObject.surface.mode == 1 && terrain != nullptr) {
				const int32_t terrainResolution = (std::clamp)(terrain->oceanGridResolution, 16, 256);
				uint32_t terrainVertexOffset = 0u;

				for (size_t lodIndex = 0u;
					lodIndex < sceneObject.surface.terrainLodVertexCounts.size();
					lodIndex++) {
					const int32_t lodResolution = (std::max)(
						terrainResolution >> static_cast<int32_t>(lodIndex),
						8);
					sceneObject.surface.terrainLodVertexOffsets[lodIndex] = terrainVertexOffset;
					sceneObject.surface.terrainLodVertexCounts[lodIndex] =
						static_cast<uint32_t>(lodResolution * lodResolution * 6);
					terrainVertexOffset += sceneObject.surface.terrainLodVertexCounts[lodIndex];
				}

				const Vector3 terrainSize = {
					sceneObject.surface.areaSize.x,
					sceneObject.surface.heightScale,
					sceneObject.surface.areaSize.y};
				const std::string terrainMeshKey = BuildTerrainMeshKey(
					terrainResolution,
					terrainSize);

				if (sceneObject.assetPath != terrainMeshKey || !sceneObject.usesCustomMesh) {
					ModelData terrainModelData = BuildTerrainModelData(
						terrainResolution,
						terrainSize,
						sceneObject.surface.terrainLodVertexOffsets,
						sceneObject.surface.terrainLodVertexCounts);
					sceneObjectManager_->SetCustomModelMesh(
						sceneObjectIndex,
						terrainMeshKey,
						terrainModelData);
				}
			}
			else if (!modelAssetPath.empty() &&
				!EditorAssetUtility::IsBuiltInPrimitiveAssetPath(modelAssetPath)) {
				if (sceneObject.assetPath != modelAssetPath || !sceneObject.usesCustomMesh) {
					if (modelData != nullptr) {
						sceneObjectManager_->SetCustomModelMesh(sceneObjectIndex, modelAssetPath, *modelData);
					}
					else {
						sceneObjectManager_->ClearCustomModelMesh(sceneObjectIndex);  // 読み込み失敗時に前回メッシュを残すと見た目だけ古いモデルが残る。
					}
				}
			}
			else if (sceneObject.usesCustomMesh) {
				sceneObjectManager_->ClearCustomModelMesh(sceneObjectIndex);
			}

			if (skinnedMeshRenderer != nullptr && modelData != nullptr && sceneObject.usesSkinning) {
				int32_t skinClipIndex = animation != nullptr ? animation->animationClipIndex : 0;
				float skinPlaybackTime = animationManager_ != nullptr
					? animationManager_->GetAnimationTime(gameObject.id)
					: 0.0f;

				if (animator != nullptr && animationManager_ != nullptr) {
					animationManager_->GetAnimatorSkinningState(
						gameObject.id,
						skinClipIndex,
						skinPlaybackTime);
				}

				sceneObjectManager_->UpdateSkinnedPose(
					sceneObjectIndex,
					*modelData,
					skinClipIndex,
					skinPlaybackTime);
			}

			sceneObject.cullMode = hasOcean
				? 2
				: (modelRenderer != nullptr && modelRenderer->doubleSided ? 2 : 0);
			const std::string& objectName = gameObject.name;
		if (objectName.find("SkyDome") != std::string::npos ||
			objectName.find("skydome") != std::string::npos ||
			objectName.find("tennkyuu") != std::string::npos ||
			objectName.find("天球") != std::string::npos) {
				sceneObject.cullMode = 1;
			}
		}
	}
}

void EditorSceneSynchronizer::Draw() {
}
