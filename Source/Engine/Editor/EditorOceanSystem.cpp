#include "EditorOceanSystem.h"

#include "Source/Engine/Core/EditorSharedState.h"
#include "Source/Engine/Core/Vector&Matrix.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {
	constexpr float kOceanPi2 = 6.28318530718f;
	constexpr float kOceanGravity = 9.81f;

	uint32_t HashOceanValue(uint32_t value) {
		value ^= value >> 16u;
		value *= 0x7feb352du;
		value ^= value >> 15u;
		value *= 0x846ca68bu;
		value ^= value >> 16u;
		return value;
	}

	float OceanHash01(uint32_t value) {
		return static_cast<float>(HashOceanValue(value) & 0x00ffffffu) / 16777215.0f;
	}

	EditorScriptVector2 NormalizeOceanDirection(
		const EditorScriptVector2& direction,
		const EditorScriptVector2& fallbackDirection) {
		const float directionLength = std::sqrt(
			direction.x * direction.x + direction.y * direction.y);

		if (directionLength <= 0.0001f) {
			return fallbackDirection;
		}

		return {
			direction.x / directionLength,
			direction.y / directionLength};
	}

	EditorScriptVector2 RotateOceanDirection(
		const EditorScriptVector2& direction,
		float angle) {
		const float angleSin = std::sin(angle);
		const float angleCos = std::cos(angle);
		return {
			direction.x * angleCos - direction.y * angleSin,
			direction.x * angleSin + direction.y * angleCos};
	}

	void BuildOceanWave(
		int32_t waveIndex,
		uint32_t spectrumSeed,
		const EditorOceanSpectrumSettings& spectrumSettings,
		const EditorScriptVector2& primaryDirection,
		const EditorScriptVector2& secondaryDirection,
		EditorScriptVector2& waveDirection,
		float& waveLength,
		float& amplitude,
		float& phaseOffset) {
		const uint32_t unsignedWaveIndex = static_cast<uint32_t>(waveIndex);
		const uint32_t hashBase = spectrumSeed + unsignedWaveIndex * 1013u;
		const float directionNoise = OceanHash01(hashBase + 17u) * 2.0f - 1.0f;
		const float amplitudeNoise = 0.78f + (1.22f - 0.78f) * OceanHash01(hashBase + 43u);
		const float lengthNoise = 0.92f + (1.08f - 0.92f) * OceanHash01(hashBase + 71u);
		float bandPosition = 0.0f;
		float lengthExponent = 0.0f;
		float amplitudeScale = 0.0f;
		float spreadScale = 1.0f;
		float secondaryBlend = 0.0f;
		const float secondaryScale =
			(std::clamp)(spectrumSettings.secondaryWaveScale, 0.0f, 1.0f);

		if (waveIndex < 4) {
			bandPosition = (static_cast<float>(waveIndex) + 0.5f) / 4.0f;
			lengthExponent = 1.0f + (1.75f - 1.0f) * bandPosition;
			amplitudeScale = 0.29f * (std::max)(spectrumSettings.swellStrength, 0.0f);
			spreadScale = 0.32f;
			secondaryBlend = secondaryScale * 0.12f;
		}
		else if (waveIndex < 12) {
			bandPosition = (static_cast<float>(waveIndex - 4) + 0.5f) / 8.0f;
			lengthExponent = -0.75f + (0.60f - -0.75f) * bandPosition;
			amplitudeScale = 0.254558f;
			spreadScale = 1.0f;
			secondaryBlend = secondaryScale * OceanHash01(hashBase + 89u) * 0.65f;
		}
		else {
			bandPosition = (static_cast<float>(waveIndex - 12) + 0.5f) / 4.0f;
			lengthExponent = std::log2((std::max)(spectrumSettings.rippleScale, 0.02f)) +
				(-0.55f + (0.45f - -0.55f) * bandPosition);
			amplitudeScale = 0.17f * (std::max)(spectrumSettings.rippleStrength, 0.0f);
			spreadScale = 1.65f;
			secondaryBlend = secondaryScale * 0.75f;
		}

		const EditorScriptVector2 bandDirection = NormalizeOceanDirection(
			{
				primaryDirection.x + (secondaryDirection.x - primaryDirection.x) * secondaryBlend,
				primaryDirection.y + (secondaryDirection.y - primaryDirection.y) * secondaryBlend},
			primaryDirection);
		waveDirection = RotateOceanDirection(
			bandDirection,
			directionNoise * (std::max)(spectrumSettings.directionSpread, 0.0f) * spreadScale);
		waveLength = (std::max)(
			spectrumSettings.waveLength * std::exp2(lengthExponent) * lengthNoise,
			0.1f);

		const float waveNumber = kOceanPi2 / waveLength;
		const float windSpeed = (std::max)(spectrumSettings.windSpeed, 0.1f);
		const float windLength = (std::max)(windSpeed * windSpeed / kOceanGravity, 0.1f);
		const float inverseWindWave = 1.0f / (std::max)(waveNumber * windLength, 0.001f);
		const float longWaveAttenuation = std::exp(-inverseWindWave * inverseWindWave);
		const float spectrumWeight = 0.35f + (1.0f - 0.35f) *
			std::sqrt((std::clamp)(longWaveAttenuation, 0.0f, 1.0f));
		amplitude = (std::max)(spectrumSettings.waveHeight, 0.0f) *
			amplitudeScale * amplitudeNoise * spectrumWeight;
		phaseOffset = OceanHash01(hashBase + 131u) * kOceanPi2;
	}

	const EditorComponent* FindActiveOceanComponent(const EditorGameObject& gameObject) {
		if (!gameObject.isActive) {
			return nullptr;
		}

		for (const EditorComponent& component : gameObject.components) {
			if (component.type == EditorComponentType::Ocean && component.isActive) {
				return &component;
			}
		}

		return nullptr;
	}

	bool SampleOceanGameObject(
		const EditorGameObject& oceanGameObject,
		const EditorComponent& oceanComponent,
		const Vector3& worldPosition,
		uint64_t surfaceSampleKey,
		float oceanElapsedTime,
		EditorOceanSurfaceSample& surfaceSample) {
		(void)oceanElapsedTime;
		const Matrix4x4 oceanWorldMatrix = MakeAffineMatrix(
			oceanGameObject.scale,
			oceanGameObject.rotate,
			oceanGameObject.translate);
		const Matrix4x4 inverseOceanWorldMatrix = Inverse(oceanWorldMatrix);
		const Vector3 targetLocalPosition = Transform(worldPosition, inverseOceanWorldMatrix);
		const float localHalfExtent =
			(std::max)(oceanComponent.oceanSize, 1.0f) * 4.0f;

		if (std::abs(targetLocalPosition.x) > localHalfExtent ||
			std::abs(targetLocalPosition.z) > localHalfExtent) {
			return false;
		}

		const uint64_t gpuSurfaceSampleKey =
			(static_cast<uint64_t>(static_cast<uint32_t>(oceanGameObject.id)) << 32u) |
			(surfaceSampleKey & 0xffffffffull);
		EditorOceanFftManager::SurfaceSample gpuSurfaceSample{};

		const bool hasFftSurfaceSample =
			EditorSharedState::g_oceanFftManager.QueueSurfaceSample(
				gpuSurfaceSampleKey,
				{targetLocalPosition.x, targetLocalPosition.z},
				gpuSurfaceSample) &&
			gpuSurfaceSample.isValid;

		const Matrix4x4 oceanRotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			oceanGameObject.rotate,
			{0.0f, 0.0f, 0.0f});
		const Matrix4x4 oceanDirectionMatrix = MakeAffineMatrix(
			oceanGameObject.scale,
			oceanGameObject.rotate,
			{0.0f, 0.0f, 0.0f});

		surfaceSample.isValid = true;
		surfaceSample.oceanGameObjectId = oceanGameObject.id;

		if (hasFftSurfaceSample) {
			surfaceSample.position = Transform(gpuSurfaceSample.localPosition, oceanWorldMatrix);
			surfaceSample.normal = Normalize(Transform(
				gpuSurfaceSample.localNormal,
				oceanRotationMatrix));
			surfaceSample.velocity = Transform(
				gpuSurfaceSample.localVelocity,
				oceanDirectionMatrix);
		}
		else {
			// FFT Readback が届くまで平均水面で支え、開始直後だけ物体が海面を貫通する状態を防ぐ。
			surfaceSample.position = Transform(
				{targetLocalPosition.x, 0.0f, targetLocalPosition.z},
				oceanWorldMatrix);
			surfaceSample.normal = Normalize(Transform(
				{0.0f, 1.0f, 0.0f},
				oceanRotationMatrix));
			surfaceSample.velocity = {0.0f, 0.0f, 0.0f};
		}

		return true;
	}
}

EditorOceanSpectrumSettings BuildEditorOceanSpectrumSettings(
	const EditorComponent& oceanComponent) {
	EditorOceanSpectrumSettings spectrumSettings{};
	spectrumSettings.waveHeight = (std::max)(oceanComponent.oceanWaveHeight, 0.0f);
	spectrumSettings.waveLength = (std::max)(oceanComponent.oceanWaveLength, 0.1f);
	spectrumSettings.windSpeed = (std::max)(oceanComponent.oceanWindSpeed, 0.1f);
	spectrumSettings.waterDepth = (std::max)(oceanComponent.oceanWaterDepth, 0.1f);
	spectrumSettings.directionSpread =
		(std::max)(oceanComponent.oceanDirectionSpread, 0.0f);
	spectrumSettings.swellStrength = (std::max)(oceanComponent.oceanSwellStrength, 0.0f);
	spectrumSettings.rippleScale = (std::max)(oceanComponent.oceanRippleScale, 0.02f);
	spectrumSettings.rippleStrength = (std::max)(oceanComponent.oceanRippleStrength, 0.0f);
	spectrumSettings.secondaryWaveScale =
		(std::max)(oceanComponent.oceanSecondaryWaveScale, 0.0f);
	spectrumSettings.spectrumSeed = (std::max)(oceanComponent.oceanSpectrumSeed, 0.0f);
	spectrumSettings.primaryDirection = oceanComponent.oceanPrimaryDirection;
	spectrumSettings.secondaryDirection = oceanComponent.oceanSecondaryDirection;
	return spectrumSettings;
}

void BuildEditorOceanSpectrum(
	const EditorOceanSpectrumSettings& spectrumSettings,
	std::array<EditorOceanWaveParameter, kEditorOceanWaveCount>& waveParameters) {
	const EditorScriptVector2 primaryDirection = NormalizeOceanDirection(
		spectrumSettings.primaryDirection,
		{1.0f, 0.0f});
	const EditorScriptVector2 secondaryDirection = NormalizeOceanDirection(
		spectrumSettings.secondaryDirection,
		{0.0f, 1.0f});
	const uint32_t spectrumSeed = static_cast<uint32_t>((std::max)(
		std::floor(spectrumSettings.spectrumSeed),
		0.0f));

	for (size_t waveIndex = 0u; waveIndex < kEditorOceanWaveCount; waveIndex++) {
		EditorScriptVector2 waveDirection{};
		float waveLength = 0.0f;
		float amplitude = 0.0f;
		float phaseOffset = 0.0f;
		BuildOceanWave(
			static_cast<int32_t>(waveIndex),
			spectrumSeed,
			spectrumSettings,
			primaryDirection,
			secondaryDirection,
			waveDirection,
			waveLength,
			amplitude,
			phaseOffset);

		const float waveNumber = kOceanPi2 / (std::max)(waveLength, 0.1f);
		EditorOceanWaveParameter& waveParameter = waveParameters[waveIndex];
		waveParameter.direction = {waveDirection.x, waveDirection.y};
		waveParameter.waveNumber = waveNumber;
		waveParameter.amplitude = amplitude;
		waveParameter.angularFrequency = std::sqrt(
			kOceanGravity * waveNumber *
			std::tanh(waveNumber * (std::max)(spectrumSettings.waterDepth, 0.1f)));
		waveParameter.phaseOffset = phaseOffset;
	}
}

float GetEditorOceanElapsedTime() {
	static const std::chrono::steady_clock::time_point oceanStartTime =
		std::chrono::steady_clock::now();
	return std::chrono::duration<float>(
		std::chrono::steady_clock::now() - oceanStartTime).count();
}

bool SampleEditorOceanSurface(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& worldPosition,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	EditorOceanSurfaceSample& surfaceSample) {
	surfaceSample = {};

	if (preferredOceanGameObjectId >= 0) {
		const EditorGameObject* oceanGameObject =
			editorScene.FindGameObject(preferredOceanGameObjectId);

		if (oceanGameObject == nullptr) {
			return false;
		}

		const EditorComponent* oceanComponent =
			FindActiveOceanComponent(*oceanGameObject);
		return oceanComponent != nullptr && SampleOceanGameObject(
			*oceanGameObject,
			*oceanComponent,
			worldPosition,
			surfaceSampleKey,
			oceanElapsedTime,
			surfaceSample);
	}

	float nearestVerticalDistance = (std::numeric_limits<float>::max)();
	EditorOceanSurfaceSample nearestSurfaceSample{};

	for (const EditorGameObject& oceanGameObject : editorScene.GetGameObjects()) {
		const EditorComponent* oceanComponent =
			FindActiveOceanComponent(oceanGameObject);

		if (oceanComponent == nullptr) {
			continue;
		}

		EditorOceanSurfaceSample candidateSurfaceSample{};

		if (!SampleOceanGameObject(
				oceanGameObject,
				*oceanComponent,
				worldPosition,
				surfaceSampleKey,
				oceanElapsedTime,
				candidateSurfaceSample)) {
			continue;
		}

		const float verticalDistance =
			std::fabs(worldPosition.y - candidateSurfaceSample.position.y);

		if (verticalDistance >= nearestVerticalDistance) {
			continue;
		}

		nearestVerticalDistance = verticalDistance;
		nearestSurfaceSample = candidateSurfaceSample;
	}

	if (!nearestSurfaceSample.isValid) {
		return false;
	}

	surfaceSample = nearestSurfaceSample;
	return true;
}
