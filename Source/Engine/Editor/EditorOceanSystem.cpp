#include "EditorOceanSystem.h"

#include "EditorComponentUtility.h"
#include "Source/Engine/Core/EditorSharedState.h"
#include "Source/Engine/Core/Vector&Matrix.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {
	constexpr float kOceanPi2 = 6.28318530718f;
	constexpr float kOceanGravity = 9.81f;
	constexpr float kOceanQueryEpsilon = 0.0001f;
	constexpr float kOceanGpuTransitionDuration = 0.12f;
	constexpr size_t kMaximumOceanSampleTransitionCount = 4096u;
	constexpr size_t kMaximumOceanSpectrumCacheCount = 256u;
	uint32_t HashOceanValue(uint32_t value);

	struct OceanSampleTransition {
		EditorOceanSurfaceSample fallbackSample{};
		float transitionStartTime = 0.0f;
		bool hasStarted = false;
	};

	struct CachedOceanSpectrum {
		EditorOceanSpectrumSettings settings{};
		std::array<EditorOceanWaveParameter, kEditorOceanWaveCount> waveParameters{};
		bool isValid = false;
	};

	std::unordered_map<uint64_t, OceanSampleTransition> oceanSampleTransitions;
	std::unordered_map<int32_t, CachedOceanSpectrum> cachedOceanSpectrums;

	bool HasSameOceanSpectrumSettings(
		const EditorOceanSpectrumSettings& firstSettings,
		const EditorOceanSpectrumSettings& secondSettings) {
		return firstSettings.waveHeight == secondSettings.waveHeight &&
			firstSettings.waveLength == secondSettings.waveLength &&
			firstSettings.windSpeed == secondSettings.windSpeed &&
			firstSettings.waterDepth == secondSettings.waterDepth &&
			firstSettings.directionSpread == secondSettings.directionSpread &&
			firstSettings.swellStrength == secondSettings.swellStrength &&
			firstSettings.rippleScale == secondSettings.rippleScale &&
			firstSettings.rippleStrength == secondSettings.rippleStrength &&
			firstSettings.secondaryWaveScale == secondSettings.secondaryWaveScale &&
			firstSettings.spectrumSeed == secondSettings.spectrumSeed &&
			firstSettings.primaryDirection.x == secondSettings.primaryDirection.x &&
			firstSettings.primaryDirection.y == secondSettings.primaryDirection.y &&
			firstSettings.secondaryDirection.x == secondSettings.secondaryDirection.x &&
			firstSettings.secondaryDirection.y == secondSettings.secondaryDirection.y;
	}

	const std::array<EditorOceanWaveParameter, kEditorOceanWaveCount>& ResolveOceanSpectrum(
		int32_t oceanGameObjectId,
		const EditorComponent& oceanComponent) {
		const EditorOceanSpectrumSettings currentSettings =
			BuildEditorOceanSpectrumSettings(oceanComponent);
		auto spectrumIterator = cachedOceanSpectrums.find(oceanGameObjectId);

		if (spectrumIterator == cachedOceanSpectrums.end()) {
			if (cachedOceanSpectrums.size() >= kMaximumOceanSpectrumCacheCount) {
				cachedOceanSpectrums.clear();
			}

			spectrumIterator = cachedOceanSpectrums.emplace(
				oceanGameObjectId,
				CachedOceanSpectrum{}).first;
		}

		CachedOceanSpectrum& cachedSpectrum = spectrumIterator->second;

		if (!cachedSpectrum.isValid ||
			!HasSameOceanSpectrumSettings(cachedSpectrum.settings, currentSettings)) {
			cachedSpectrum.settings = currentSettings;
			BuildEditorOceanSpectrum(
				cachedSpectrum.settings,
				cachedSpectrum.waveParameters);
			cachedSpectrum.isValid = true;
		}

		return cachedSpectrum.waveParameters;
	}

	EditorOceanRenderSettings BuildOceanRenderSettingsForQuery(
		const EditorComponent& oceanComponent) {
		EditorOceanRenderSettings oceanSettings{};
		oceanSettings.isEnabled = oceanComponent.isActive;
		oceanSettings.gridResolution = oceanComponent.oceanGridResolution;
		oceanSettings.size = (std::max)(oceanComponent.oceanSize, 1.0f);
		oceanSettings.waveHeight = (std::max)(oceanComponent.oceanWaveHeight, 0.0f);
		oceanSettings.maxWaveHeight = (std::max)(oceanComponent.oceanMaxWaveHeight, 0.0f);
		oceanSettings.waveLength = (std::max)(oceanComponent.oceanWaveLength, 0.1f);
		oceanSettings.waveSpeed = oceanComponent.oceanWaveSpeed;
		oceanSettings.timeScale = oceanComponent.oceanTimeScale;
		oceanSettings.choppiness = oceanComponent.oceanChoppiness;
		oceanSettings.primaryDirection = {
			oceanComponent.oceanPrimaryDirection.x,
			oceanComponent.oceanPrimaryDirection.y};
		oceanSettings.secondaryDirection = {
			oceanComponent.oceanSecondaryDirection.x,
			oceanComponent.oceanSecondaryDirection.y};
		oceanSettings.secondaryWaveScale = oceanComponent.oceanSecondaryWaveScale;
		oceanSettings.rippleScale = oceanComponent.oceanRippleScale;
		oceanSettings.rippleStrength = oceanComponent.oceanRippleStrength;
		oceanSettings.windSpeed = (std::max)(oceanComponent.oceanWindSpeed, 0.1f);
		oceanSettings.waterDepth = (std::max)(oceanComponent.oceanWaterDepth, 0.1f);
		oceanSettings.directionSpread =
			(std::max)(oceanComponent.oceanDirectionSpread, 0.0f);
		oceanSettings.swellStrength =
			(std::max)(oceanComponent.oceanSwellStrength, 0.0f);
		oceanSettings.spectrumSeed =
			(std::max)(oceanComponent.oceanSpectrumSeed, 0.0f);
		oceanSettings.crestSharpness =
			(std::clamp)(oceanComponent.oceanCrestSharpness, 0.0f, 1.0f);
		oceanSettings.foamStrength = oceanComponent.oceanFoamStrength;
		oceanSettings.foamThreshold =
			(std::clamp)(oceanComponent.oceanFoamThreshold, 0.0f, 1.0f);
		return oceanSettings;
	}

	Vector3 AddOceanVector(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	Vector3 SubtractOceanVector(const Vector3& firstValue, const Vector3& secondValue) {
		return {
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 MultiplyOceanVector(float scalar, const Vector3& value) {
		return {scalar * value.x, scalar * value.y, scalar * value.z};
	}

	Vector3 LerpOceanVector(
		const Vector3& startValue,
		const Vector3& endValue,
		float interpolation) {
		return AddOceanVector(
			startValue,
			MultiplyOceanVector(
				interpolation,
				SubtractOceanVector(endValue, startValue)));
	}

	void ResolveOceanGpuTransition(
		uint64_t gpuSurfaceSampleKey,
		float oceanElapsedTime,
		bool hasFftSurfaceSample,
		EditorOceanSurfaceSample& surfaceSample) {
		if (!hasFftSurfaceSample) {
			auto transitionIterator = oceanSampleTransitions.find(gpuSurfaceSampleKey);

			if (transitionIterator == oceanSampleTransitions.end()) {
				if (oceanSampleTransitions.size() >= kMaximumOceanSampleTransitionCount) {
					return;
				}

				transitionIterator = oceanSampleTransitions.emplace(
					gpuSurfaceSampleKey,
					OceanSampleTransition{}).first;
			}

			transitionIterator->second.fallbackSample = surfaceSample;
			transitionIterator->second.hasStarted = false;
			return;
		}

		auto transitionIterator = oceanSampleTransitions.find(gpuSurfaceSampleKey);

		if (transitionIterator == oceanSampleTransitions.end()) {
			return;
		}

		OceanSampleTransition& transition = transitionIterator->second;

		if (!transition.hasStarted) {
			transition.transitionStartTime = oceanElapsedTime;
			transition.hasStarted = true;
		}

		const float transitionRatio = (std::clamp)(
			(oceanElapsedTime - transition.transitionStartTime) /
				kOceanGpuTransitionDuration,
			0.0f,
			1.0f);
		const float smoothTransitionRatio =
			transitionRatio * transitionRatio * (3.0f - 2.0f * transitionRatio);
		surfaceSample.position = LerpOceanVector(
			transition.fallbackSample.position,
			surfaceSample.position,
			smoothTransitionRatio);
		surfaceSample.normal = Normalize(LerpOceanVector(
			transition.fallbackSample.normal,
			surfaceSample.normal,
			smoothTransitionRatio));
		surfaceSample.velocity = LerpOceanVector(
			transition.fallbackSample.velocity,
			surfaceSample.velocity,
			smoothTransitionRatio);
		surfaceSample.foam = transition.fallbackSample.foam +
			(surfaceSample.foam - transition.fallbackSample.foam) * smoothTransitionRatio;

		if (transitionRatio >= 1.0f) {
			oceanSampleTransitions.erase(transitionIterator);
		}
	}

	float GetOceanVectorLength(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	uint64_t BuildOceanQuerySampleKey(uint64_t baseKey, uint32_t sampleIndex) {
		const uint32_t foldedBaseKey =
			static_cast<uint32_t>(baseKey & 0xffffffffull) ^
			static_cast<uint32_t>(baseKey >> 32u);
		const uint32_t mixedKey = HashOceanValue(
			foldedBaseKey ^
			(sampleIndex + 1u) * 0x9e3779b9u);
		return static_cast<uint64_t>(mixedKey);
	}

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
		const EditorScene& editorScene,
		const EditorGameObject& oceanGameObject,
		const EditorComponent& oceanComponent,
		const Vector3& worldPosition,
		uint64_t surfaceSampleKey,
		float oceanElapsedTime,
		EditorOceanSurfaceSample& surfaceSample) {
		const Matrix4x4 oceanWorldMatrix = editorScene.GetWorldMatrix(oceanGameObject.id);
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
		const EditorOceanRenderSettings queryOceanSettings =
			BuildOceanRenderSettingsForQuery(oceanComponent);

		const bool hasFftSurfaceSample =
			EditorSharedState::g_oceanFftManager.IsReadyFor(queryOceanSettings) &&
			EditorSharedState::g_oceanFftManager.QueueSurfaceSample(
				gpuSurfaceSampleKey,
				{targetLocalPosition.x, targetLocalPosition.z},
				gpuSurfaceSample) &&
			gpuSurfaceSample.isValid;

		Vector3 oceanWorldScale = oceanGameObject.scale;
		Vector3 oceanWorldRotation = oceanGameObject.rotate;
		Vector3 oceanWorldPosition = oceanGameObject.translate;
		editorScene.GetWorldTransform(
			oceanGameObject.id,
			oceanWorldScale,
			oceanWorldRotation,
			oceanWorldPosition);
		const Matrix4x4 oceanRotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			oceanWorldRotation,
			{0.0f, 0.0f, 0.0f});
		const Matrix4x4 oceanDirectionMatrix = MakeAffineMatrix(
			oceanWorldScale,
			oceanWorldRotation,
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
			surfaceSample.foam = (std::clamp)(gpuSurfaceSample.foam, 0.0f, 1.0f);
		}
		else {
			// FFT Readback待ちも同じOcean設定の有限水深スペクトルを評価し、平面へ落とさない。
			const std::array<EditorOceanWaveParameter, kEditorOceanWaveCount>& waveParameters =
				ResolveOceanSpectrum(oceanGameObject.id, oceanComponent);
			const float waveTime = oceanElapsedTime *
				oceanComponent.oceanTimeScale * oceanComponent.oceanWaveSpeed;
			const float choppiness = oceanComponent.oceanChoppiness;
			float localHeight = 0.0f;
			float localDisplacementX = 0.0f;
			float localDisplacementZ = 0.0f;
			float localSlopeX = 0.0f;
			float localSlopeZ = 0.0f;
			float displacementDerivativeXx = 0.0f;
			float displacementDerivativeXz = 0.0f;
			float displacementDerivativeZx = 0.0f;
			float displacementDerivativeZz = 0.0f;
			Vector3 localVelocity{};

			for (const EditorOceanWaveParameter& waveParameter : waveParameters) {
				const float phase = waveParameter.waveNumber *
					(waveParameter.direction.x * targetLocalPosition.x +
					 waveParameter.direction.y * targetLocalPosition.z) -
					waveParameter.angularFrequency * waveTime +
					waveParameter.phaseOffset;
				const float sinePhase = std::sin(phase);
				const float cosinePhase = std::cos(phase);
				const float horizontalAmplitude =
					choppiness * waveParameter.amplitude;
				const float phaseVelocity =
					waveParameter.angularFrequency *
					oceanComponent.oceanTimeScale * oceanComponent.oceanWaveSpeed;

				localHeight += waveParameter.amplitude * sinePhase;
				localDisplacementX +=
					waveParameter.direction.x * horizontalAmplitude * cosinePhase;
				localDisplacementZ +=
					waveParameter.direction.y * horizontalAmplitude * cosinePhase;
				localSlopeX += waveParameter.direction.x * waveParameter.waveNumber *
					waveParameter.amplitude * cosinePhase;
				localSlopeZ += waveParameter.direction.y * waveParameter.waveNumber *
					waveParameter.amplitude * cosinePhase;
				const float displacementDerivative =
					-horizontalAmplitude * waveParameter.waveNumber * sinePhase;
				displacementDerivativeXx += displacementDerivative *
					waveParameter.direction.x * waveParameter.direction.x;
				displacementDerivativeXz += displacementDerivative *
					waveParameter.direction.x * waveParameter.direction.y;
				displacementDerivativeZx += displacementDerivative *
					waveParameter.direction.y * waveParameter.direction.x;
				displacementDerivativeZz += displacementDerivative *
					waveParameter.direction.y * waveParameter.direction.y;
				localVelocity.x += waveParameter.direction.x * horizontalAmplitude *
					phaseVelocity * sinePhase;
				localVelocity.y -= waveParameter.amplitude * phaseVelocity * cosinePhase;
				localVelocity.z += waveParameter.direction.y * horizontalAmplitude *
					phaseVelocity * sinePhase;
			}

			localHeight = (std::clamp)(
				localHeight,
				-oceanComponent.oceanMaxWaveHeight,
				oceanComponent.oceanMaxWaveHeight);
			const Vector3 localSurfacePosition{
				targetLocalPosition.x + localDisplacementX,
				localHeight,
				targetLocalPosition.z + localDisplacementZ};
			const Vector3 localSurfaceNormal = Normalize(
				Vector3{-localSlopeX, 1.0f, -localSlopeZ});
			surfaceSample.position = Transform(
				localSurfacePosition,
				oceanWorldMatrix);
			surfaceSample.normal = Normalize(Transform(
				localSurfaceNormal,
				oceanRotationMatrix));
			surfaceSample.velocity = Transform(
				localVelocity,
				oceanDirectionMatrix);
			const float horizontalJacobian =
				(1.0f + displacementDerivativeXx) *
				(1.0f + displacementDerivativeZz) -
				displacementDerivativeXz * displacementDerivativeZx;
			surfaceSample.foam = (std::clamp)(
				(0.98f - horizontalJacobian) / 0.38f,
				0.0f,
				1.0f);
		}

		// Readback開始時だけCPU近似面からGPU FFT面へ補間し、定常時の応答には遅延を足さない。
		ResolveOceanGpuTransition(
			gpuSurfaceSampleKey,
			oceanElapsedTime,
			hasFftSurfaceSample,
			surfaceSample);

		// SurfaceWakeEmitterの局所波は描画頂点と同じ減衰・位相でCPU Sampleへ加える。
		// 浮力やOcean Castが航跡の見た目だけをすり抜けないよう、最大2点に制限して軽量に保つ。
		uint32_t interactionCount = 0u;

		for (const EditorGameObject& interactionObject : editorScene.GetGameObjects()) {
			if (!interactionObject.isActive || interactionCount >= 2u) {
				continue;
			}

			const EditorComponent* wakeComponent = EditorComponentUtility::FindComponent(
				interactionObject,
				EditorComponentType::SurfaceWakeEmitter);

			if (wakeComponent == nullptr ||
				!wakeComponent->isActive ||
				wakeComponent->surfaceWakeCurrentIntensity <= 0.0001f ||
				(wakeComponent->surfaceWakeOceanGameObjectId >= 0 &&
				 wakeComponent->surfaceWakeOceanGameObjectId != surfaceSample.oceanGameObjectId)) {
				continue;
			}

			Vector3 interactionScale{};
			Vector3 interactionRotation{};
			Vector3 interactionPosition{};

			if (!editorScene.GetWorldTransform(
				interactionObject.id,
				interactionScale,
				interactionRotation,
				interactionPosition)) {
				continue;
			}

			const float interactionRadius = (std::max)(wakeComponent->surfaceWakeWidth * 4.0f, 0.5f);
			const float interactionAmplitude =
				wakeComponent->surfaceWakeCurrentIntensity *
				(std::max)(wakeComponent->surfaceWakeWidth, 0.1f) *
				0.10f;
			const float offsetX = surfaceSample.position.x - interactionPosition.x;
			const float offsetZ = surfaceSample.position.z - interactionPosition.z;
			const float interactionDistance = std::sqrt(offsetX * offsetX + offsetZ * offsetZ);

			if (interactionDistance < interactionRadius) {
				const float inverseDistance = 1.0f / (std::max)(interactionDistance, 0.0001f);
				const float normalizedDistance = interactionDistance / interactionRadius;
				const float envelope = (1.0f - normalizedDistance) * (1.0f - normalizedDistance);
				const float envelopeDerivative = -2.0f * (1.0f - normalizedDistance) / interactionRadius;
				const float waveNumber = 6.28318530718f / (std::max)(interactionRadius * 0.35f, 0.5f);
				const float phase = interactionDistance * waveNumber - oceanElapsedTime * 4.0f;
				const float waveSin = std::sin(phase);
				const float waveCos = std::cos(phase);
				const float radialSlope = interactionAmplitude *
					(waveNumber * waveCos * envelope + waveSin * envelopeDerivative);
				surfaceSample.position.y += interactionAmplitude * waveSin * envelope;
				surfaceSample.normal = Normalize(Vector3{
					surfaceSample.normal.x - offsetX * inverseDistance * radialSlope,
					surfaceSample.normal.y,
					surfaceSample.normal.z - offsetZ * inverseDistance * radialSlope});
				surfaceSample.velocity.y += -4.0f * interactionAmplitude * waveCos * envelope;
				surfaceSample.foam = (std::max)(
					surfaceSample.foam,
					(std::clamp)(std::fabs(radialSlope) * 0.35f, 0.0f, 1.0f));
			}

			interactionCount++;
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
			editorScene,
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
				editorScene,
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

bool QueryEditorOceanOcclusion(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& startPosition,
	const Vector3& endPosition,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanOcclusionResult& occlusionResult) {
	occlusionResult = {};
	const Vector3 segment = SubtractOceanVector(endPosition, startPosition);
	const float segmentLength = GetOceanVectorLength(segment);

	if (segmentLength <= kOceanQueryEpsilon) {
		return false;
	}

	const int32_t safeCoarseStepCount = (std::clamp)(coarseStepCount, 2, 64);
	const int32_t safeRefinementCount = (std::clamp)(refinementCount, 0, 12);
	float previousRatio = 0.0f;
	float previousClearance = (std::numeric_limits<float>::max)();
	bool hasPreviousSample = false;
	bool hasMaximumHeight = false;
	int32_t sampleSequence = 0;
	EditorOceanSurfaceSample firstBlockedSample{};
	float firstBlockedRatio = 0.0f;
	float firstVisibleRatio = 0.0f;

	auto sampleSegment = [&](float normalizedDistance, EditorOceanSurfaceSample& surfaceSample, float& signedClearance) {
		const Vector3 queryPosition = AddOceanVector(
			startPosition,
			MultiplyOceanVector(normalizedDistance, segment));
		const uint64_t queryKey = BuildOceanQuerySampleKey(
			surfaceSampleKey,
			static_cast<uint32_t>(sampleSequence++));

		if (!SampleEditorOceanSurface(
				editorScene,
				preferredOceanGameObjectId,
				queryPosition,
				queryKey,
				oceanElapsedTime,
				surfaceSample)) {
			return false;
		}

		signedClearance = Dot(
			SubtractOceanVector(queryPosition, surfaceSample.position),
			surfaceSample.normal) - clearance;
		occlusionResult.hasOcean = true;
		occlusionResult.minimumClearance = hasMaximumHeight
			? (std::min)(occlusionResult.minimumClearance, signedClearance)
			: signedClearance;
		occlusionResult.maximumSurfaceHeight = hasMaximumHeight
			? (std::max)(occlusionResult.maximumSurfaceHeight, surfaceSample.position.y)
			: surfaceSample.position.y;
		hasMaximumHeight = true;
		return true;
	};

	for (int32_t stepIndex = 0; stepIndex <= safeCoarseStepCount; stepIndex++) {
		const float normalizedDistance =
			static_cast<float>(stepIndex) / static_cast<float>(safeCoarseStepCount);
		EditorOceanSurfaceSample surfaceSample{};
		float signedClearance = 0.0f;

		if (!sampleSegment(normalizedDistance, surfaceSample, signedClearance)) {
			hasPreviousSample = false;
			continue;
		}

		if (signedClearance <= 0.0f) {
			occlusionResult.isBlocked = true;
			firstBlockedSample = surfaceSample;
			firstBlockedRatio = normalizedDistance;
			firstVisibleRatio = hasPreviousSample && previousClearance > 0.0f
				? previousRatio
				: normalizedDistance;
			break;
		}

		previousRatio = normalizedDistance;
		previousClearance = signedClearance;
		hasPreviousSample = true;
	}

	if (!occlusionResult.hasOcean) {
		return false;
	}

	if (!occlusionResult.isBlocked) {
		return true;
	}

	float visibleRatio = firstVisibleRatio;
	float blockedRatio = firstBlockedRatio;
	EditorOceanSurfaceSample refinedBlockedSample = firstBlockedSample;

	for (int32_t refinementIndex = 0; refinementIndex < safeRefinementCount && visibleRatio < blockedRatio; refinementIndex++) {
		const float middleRatio = (visibleRatio + blockedRatio) * 0.5f;
		EditorOceanSurfaceSample middleSample{};
		float middleClearance = 0.0f;

		if (!sampleSegment(middleRatio, middleSample, middleClearance)) {
			break;
		}

		if (middleClearance <= 0.0f) {
			blockedRatio = middleRatio;
			refinedBlockedSample = middleSample;
		}
		else {
			visibleRatio = middleRatio;
		}
	}

	occlusionResult.intersection.isValid = true;
	occlusionResult.intersection.oceanGameObjectId = refinedBlockedSample.oceanGameObjectId;
	occlusionResult.intersection.position = refinedBlockedSample.position;
	occlusionResult.intersection.normal = refinedBlockedSample.normal;
	occlusionResult.intersection.surfaceVelocity = refinedBlockedSample.velocity;
	occlusionResult.intersection.distance = segmentLength * blockedRatio;
	occlusionResult.intersection.normalizedDistance = blockedRatio;
	return true;
}

bool CastEditorOceanSegment(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& startPosition,
	const Vector3& endPosition,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanSegmentHit& segmentHit) {
	EditorOceanOcclusionResult occlusionResult{};
	segmentHit = {};

	if (!QueryEditorOceanOcclusion(
			editorScene,
			preferredOceanGameObjectId,
			startPosition,
			endPosition,
			clearance,
			surfaceSampleKey,
			oceanElapsedTime,
			coarseStepCount,
			refinementCount,
			occlusionResult) ||
		!occlusionResult.isBlocked) {
		return false;
	}

	segmentHit = occlusionResult.intersection;
	return segmentHit.isValid;
}

bool RaycastEditorOceanSurface(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& rayOrigin,
	const Vector3& rayDirection,
	float maximumDistance,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanSegmentHit& segmentHit) {
	segmentHit = {};
	const float directionLength = GetOceanVectorLength(rayDirection);
	const float safeMaximumDistance = (std::max)(maximumDistance, 0.0f);

	if (directionLength <= kOceanQueryEpsilon || safeMaximumDistance <= kOceanQueryEpsilon) {
		return false;
	}

	const Vector3 normalizedDirection = MultiplyOceanVector(1.0f / directionLength, rayDirection);
	const Vector3 endPosition = AddOceanVector(
		rayOrigin,
		MultiplyOceanVector(safeMaximumDistance, normalizedDirection));
	return CastEditorOceanSegment(
		editorScene,
		preferredOceanGameObjectId,
		rayOrigin,
		endPosition,
		clearance,
		surfaceSampleKey,
		oceanElapsedTime,
		coarseStepCount,
		refinementCount,
		segmentHit);
}
