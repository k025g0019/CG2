#pragma warning(disable : 4514)

#include "EditorAudioManager.h"

#include "EditorComponentUtility.h"
#include "EditorPhysicsManager.h"
#include "EditorSharedState.h"

#include <cmath>
#include <cstring>
#include <fstream>

#pragma warning(push, 0)
#include <xaudio2.h>
#include <xaudio2fx.h>
#pragma warning(pop)

using namespace EditorSharedState;

namespace {
	struct WaveChunkHeader {
		char id[4];
		uint32_t size;
	};

	struct WaveRiffHeader {
		WaveChunkHeader chunk;
		char type[4];
	};

	struct PcmWaveFormat {
		uint16_t formatTag;
		uint16_t channelCount;
		uint32_t sampleRate;
		uint32_t averageBytesPerSecond;
		uint16_t blockAlign;
		uint16_t bitsPerSample;
	};
}

EditorAudioManager::~EditorAudioManager() {
	Stop();

	for (auto& clipPair : clips_) {
		delete[] static_cast<unsigned char*>(clipPair.second.pBuffer);
		clipPair.second.pBuffer = nullptr;
		clipPair.second.bufferSize = 0u;
	}

	clips_.clear();
}

void EditorAudioManager::Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager) {
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	xAudio2_ = g_xAudio2;

	if (xAudio2_ != nullptr) {
		HRESULT hr = xAudio2_->CreateSubmixVoice(&reverbSubmix_, 2, 44100, 0, 0, nullptr, nullptr);
		if (SUCCEEDED(hr) && reverbSubmix_ != nullptr) {
			hr = XAudio2CreateReverb(&reverbEffect_, 0);
			if (SUCCEEDED(hr) && reverbEffect_ != nullptr) {
				XAUDIO2_EFFECT_DESCRIPTOR fxDesc{};
				fxDesc.InitialState = TRUE;
				fxDesc.OutputChannels = 2;
				fxDesc.pEffect = reverbEffect_;
				XAUDIO2_EFFECT_CHAIN fxChain{};
				fxChain.EffectCount = 1;
				fxChain.pEffectDescriptors = &fxDesc;
				reverbSubmix_->SetEffectChain(&fxChain);

				XAUDIO2FX_REVERB_PARAMETERS reverbParams{};
				reverbParams.ReflectionsDelay = XAUDIO2FX_REVERB_DEFAULT_REFLECTIONS_DELAY;
				reverbParams.ReverbDelay = XAUDIO2FX_REVERB_DEFAULT_REVERB_DELAY;
				reverbParams.RearDelay = XAUDIO2FX_REVERB_DEFAULT_REAR_DELAY;
				reverbParams.PositionLeft = XAUDIO2FX_REVERB_DEFAULT_POSITION;
				reverbParams.PositionRight = XAUDIO2FX_REVERB_DEFAULT_POSITION;
				reverbParams.PositionMatrixLeft = XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX;
				reverbParams.PositionMatrixRight = XAUDIO2FX_REVERB_DEFAULT_POSITION_MATRIX;
				reverbParams.EarlyDiffusion = XAUDIO2FX_REVERB_DEFAULT_EARLY_DIFFUSION;
				reverbParams.LateDiffusion = XAUDIO2FX_REVERB_DEFAULT_LATE_DIFFUSION;
				reverbParams.LowEQGain = XAUDIO2FX_REVERB_DEFAULT_LOW_EQ_GAIN;
				reverbParams.LowEQCutoff = XAUDIO2FX_REVERB_DEFAULT_LOW_EQ_CUTOFF;
				reverbParams.HighEQGain = XAUDIO2FX_REVERB_DEFAULT_HIGH_EQ_GAIN;
				reverbParams.HighEQCutoff = XAUDIO2FX_REVERB_DEFAULT_HIGH_EQ_CUTOFF;
				reverbParams.RoomFilterFreq = XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_FREQ;
				reverbParams.RoomFilterMain = XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_MAIN;
				reverbParams.RoomFilterHF = XAUDIO2FX_REVERB_DEFAULT_ROOM_FILTER_HF;
				reverbParams.ReflectionsGain = XAUDIO2FX_REVERB_DEFAULT_REFLECTIONS_GAIN;
				reverbParams.ReverbGain = XAUDIO2FX_REVERB_DEFAULT_REVERB_GAIN;
				reverbParams.DecayTime = XAUDIO2FX_REVERB_DEFAULT_DECAY_TIME;
				reverbParams.Density = XAUDIO2FX_REVERB_DEFAULT_DENSITY;
				reverbParams.RoomSize = XAUDIO2FX_REVERB_DEFAULT_ROOM_SIZE;
				reverbParams.WetDryMix = XAUDIO2FX_REVERB_DEFAULT_WET_DRY_MIX;
				reverbSubmix_->SetEffectParameters(0, &reverbParams, sizeof(reverbParams));
			}
		}
	}
}

void EditorAudioManager::Start() {
	if (editorScene_ == nullptr || xAudio2_ == nullptr) {
		return;
	}

	playbackClock_ = 0.0f;
	lastPlaybackTimeByGameObjectId_.clear();

	for (auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const auto* audioSource = EditorComponentUtility::FindComponent(
			gameObject, EditorComponentType::AudioSource);

		if (audioSource == nullptr ||
			!audioSource->isActive ||
			!audioSource->audioPlayOnAwake) {
			continue;
		}

		Play(gameObject.id);
	}
}

bool EditorAudioManager::Play(int32_t gameObjectId) {
	if (editorScene_ == nullptr || xAudio2_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	const EditorComponent* audioSource = gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::AudioSource)
		: nullptr;

	if (gameObject == nullptr ||
		!gameObject->isActive ||
		audioSource == nullptr ||
		!audioSource->isActive ||
		audioSource->assetPath.empty()) {
		return false;
	}

	const float retriggerInterval = (std::max)(audioSource->audioRetriggerInterval, 0.0f);
	const auto lastPlaybackIterator = lastPlaybackTimeByGameObjectId_.find(gameObjectId);

	if (lastPlaybackIterator != lastPlaybackTimeByGameObjectId_.end() &&
		playbackClock_ - lastPlaybackIterator->second < retriggerInterval) {
		return false;
	}

	AudioClip* clip = LoadClip(audioSource->assetPath);

	if (clip == nullptr ||
		clip->pBuffer == nullptr ||
		clip->bufferSize == 0u ||
		clip->channelCount == 0u ||
		clip->sampleRate == 0u ||
		clip->blockAlign == 0u ||
		(clip->formatTag != WAVE_FORMAT_PCM && clip->formatTag != WAVE_FORMAT_IEEE_FLOAT)) {
		return false;
	}

	ActiveAudio active{};
	active.gameObjectId = gameObjectId;
	active.clip = clip;
	active.volume = audioSource->audioVolume;
	active.pitch = audioSource->audioPitch;
	active.spatialBlend = audioSource->audioSpatialBlend;
	active.minDistance = audioSource->audioMinDistance;
	active.maxDistance = audioSource->audioMaxDistance;
	active.audioBus = (std::clamp)(audioSource->audioBus, 0, 3);
	active.loop = audioSource->audioLoop;
	active.filterComponentId = -1;

	const int32_t maximumVoices = (std::clamp)(audioSource->audioMaxVoices, 1, 32);
	int32_t activeVoiceCount = 0;
	auto oldestVoiceIterator = activeAudios_.end();

	for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end(); activeIterator++) {
		if (activeIterator->gameObjectId != gameObjectId) {
			continue;
		}

		activeVoiceCount++;

		if (oldestVoiceIterator == activeAudios_.end() ||
			activeIterator->playbackAge > oldestVoiceIterator->playbackAge) {
			oldestVoiceIterator = activeIterator;
		}
	}

	if (activeVoiceCount >= maximumVoices && oldestVoiceIterator != activeAudios_.end()) {
		StopClip(*oldestVoiceIterator);
		activeAudios_.erase(oldestVoiceIterator);
	}

	WAVEFORMATEX format{};
	format.wFormatTag = clip->formatTag;
	format.nChannels = clip->channelCount;
	format.nSamplesPerSec = clip->sampleRate;
	format.wBitsPerSample = clip->bitsPerSample;
	format.nBlockAlign = clip->blockAlign;
	format.nAvgBytesPerSec = clip->averageBytesPerSecond;
	format.cbSize = 0;

	IXAudio2SourceVoice* voice = nullptr;
	HRESULT hr = xAudio2_->CreateSourceVoice(&voice, &format, XAUDIO2_VOICE_USEFILTER);

	if (FAILED(hr) || voice == nullptr) {
		return false;
	}

	active.voice = voice;
	XAUDIO2_BUFFER buffer{};
	buffer.AudioBytes = clip->bufferSize;
	buffer.pAudioData = static_cast<const BYTE*>(clip->pBuffer);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.LoopCount = active.loop ? XAUDIO2_LOOP_INFINITE : 0u;
	hr = voice->SubmitSourceBuffer(&buffer);

	if (FAILED(hr)) {
		voice->DestroyVoice();
		return false;
	}

	voice->SetVolume(GetMixedVolume(active));
	voice->SetFrequencyRatio((std::clamp)(audioSource->audioPitch, 0.01f, 2.0f));
	voice->Start(0u);
	activeAudios_.push_back(active);
	lastPlaybackTimeByGameObjectId_[gameObjectId] = playbackClock_;
	return true;
}

void EditorAudioManager::Update(float deltaTime) {
	playbackClock_ += (std::max)(deltaTime, 0.0f);

	if (editorScene_ == nullptr || xAudio2_ == nullptr) {
		return;
	}

	for (auto it = activeAudios_.begin(); it != activeAudios_.end();) {
		it->playbackAge += (std::max)(deltaTime, 0.0f);
		const auto* gameObject = editorScene_->FindGameObject(it->gameObjectId);
		const auto* audioSource = gameObject != nullptr
			? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::AudioSource)
			: nullptr;

		if (gameObject == nullptr || !gameObject->isActive ||
			audioSource == nullptr || !audioSource->isActive) {
			StopClip(*it);
			it = activeAudios_.erase(it);
			continue;
		}

		XAUDIO2_VOICE_STATE state;
		it->voice->GetState(&state);
		if (!it->loop && state.BuffersQueued == 0) {
			StopClip(*it);
			it = activeAudios_.erase(it);
			continue;
		}

		if (it->volume != audioSource->audioVolume) {
			it->volume = audioSource->audioVolume;
		}

		it->spatialBlend = (std::clamp)(audioSource->audioSpatialBlend, 0.0f, 1.0f);
		it->minDistance = (std::max)(audioSource->audioMinDistance, 0.01f);
		it->maxDistance = (std::max)(audioSource->audioMaxDistance, it->minDistance + 0.01f);
		it->audioBus = (std::clamp)(audioSource->audioBus, 0, 3);

		// 3D音源は距離、方向、遮蔽、相対速度、左右定位を同じ経路で評価する。
		if (it->spatialBlend > 0.0f && it->voice != nullptr) {
			const Vector3 listenerPos = GetListenerPosition();
			const int32_t listenerGameObjectId = GetListenerGameObjectId();
			const float dx = gameObject->translate.x - listenerPos.x;
			const float dy = gameObject->translate.y - listenerPos.y;
			const float dz = gameObject->translate.z - listenerPos.z;
			const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

			const float minDist = std::max(it->minDistance, 0.01f);
			const float maxDist = std::max(it->maxDistance, minDist + 0.01f);
			const float t = std::min(std::max((distance - minDist) / (maxDist - minDist), 0.0f), 1.0f);
			const float smoothDistance = t * t * (3.0f - 2.0f * t);
			const float distanceAtten = 1.0f - smoothDistance;

			const float inverseDistance = distance > 0.0001f ? 1.0f / distance : 0.0f;
			const Vector3 sourceToListener{
				-dx * inverseDistance,
				-dy * inverseDistance,
				-dz * inverseDistance};
			const float cosinePitch = std::cos(gameObject->rotate.x);
			const Vector3 sourceForward{
				std::sin(gameObject->rotate.y) * cosinePitch,
				-std::sin(gameObject->rotate.x),
				std::cos(gameObject->rotate.y) * cosinePitch};
			const float directionCosine = (std::clamp)(
				sourceForward.x * sourceToListener.x +
				sourceForward.y * sourceToListener.y +
				sourceForward.z * sourceToListener.z,
				-1.0f,
				1.0f);
			const float directionAngle = std::acos(directionCosine) * 57.2957795f;
			const float innerHalfAngle = (std::clamp)(audioSource->audioConeInnerAngle * 0.5f, 0.0f, 180.0f);
			const float outerHalfAngle = (std::clamp)(
				audioSource->audioConeOuterAngle * 0.5f,
				innerHalfAngle,
				180.0f);
			const float coneRatio = outerHalfAngle > innerHalfAngle
				? (std::clamp)((directionAngle - innerHalfAngle) / (outerHalfAngle - innerHalfAngle), 0.0f, 1.0f)
				: (directionAngle <= innerHalfAngle ? 0.0f : 1.0f);
			const float coneVolume =
				1.0f + ((std::clamp)(audioSource->audioConeOuterVolume, 0.0f, 1.0f) - 1.0f) * coneRatio;

			it->occlusion = 0.0f;
			if (physicsManager_ != nullptr && distance > 0.5f) {
				const Vector3 rayDirection{dx * inverseDistance, dy * inverseDistance, dz * inverseDistance};
				const Vector3 rayOrigin{
					listenerPos.x + rayDirection.x * 0.25f,
					listenerPos.y + rayDirection.y * 0.25f,
					listenerPos.z + rayDirection.z * 0.25f};
				EditorJoltPhysicsManager::PhysicsHit physicsHit{};
				const bool hasBlockingHit = physicsManager_->Raycast(
					rayOrigin,
					rayDirection,
					distance - 0.35f,
					physicsHit);

				if (hasBlockingHit &&
					physicsHit.gameObjectId != gameObject->id &&
					physicsHit.gameObjectId != listenerGameObjectId) {
					it->occlusion = (std::clamp)(audioSource->audioOcclusionStrength, 0.0f, 1.0f);
				}
			}

			const float mixedVolume = GetMixedVolume(*it);
			const float occlusionVolume = 1.0f - it->occlusion * 0.72f;
			const float volume3d = mixedVolume * distanceAtten * coneVolume * occlusionVolume;
			const float volume2d = mixedVolume;
			const float finalVolume = volume2d * (1.0f - it->spatialBlend) + volume3d * it->spatialBlend;
			it->voice->SetVolume(finalVolume);

			float dopplerRatio = 1.0f;
			if (it->hasPreviousSpatialPosition && deltaTime > 0.0001f && distance > 0.0001f) {
				const Vector3 sourceVelocity{
					(gameObject->translate.x - it->previousSourcePosition.x) / deltaTime,
					(gameObject->translate.y - it->previousSourcePosition.y) / deltaTime,
					(gameObject->translate.z - it->previousSourcePosition.z) / deltaTime};
				const Vector3 listenerVelocity{
					(listenerPos.x - it->previousListenerPosition.x) / deltaTime,
					(listenerPos.y - it->previousListenerPosition.y) / deltaTime,
					(listenerPos.z - it->previousListenerPosition.z) / deltaTime};
				const float relativeRadialVelocity =
					(listenerVelocity.x - sourceVelocity.x) * (dx * inverseDistance) +
					(listenerVelocity.y - sourceVelocity.y) * (dy * inverseDistance) +
					(listenerVelocity.z - sourceVelocity.z) * (dz * inverseDistance);
				constexpr float speedOfSound = 343.0f;
				dopplerRatio = (std::clamp)(
					speedOfSound / (speedOfSound + relativeRadialVelocity),
					0.5f,
					2.0f);
			}

			it->previousSourcePosition = gameObject->translate;
			it->previousListenerPosition = listenerPos;
			it->hasPreviousSpatialPosition = true;
			it->pitch = audioSource->audioPitch;
			const float dopplerLevel = (std::clamp)(audioSource->audioDopplerLevel, 0.0f, 5.0f);
			const float dopplerPitch = 1.0f + (dopplerRatio - 1.0f) * dopplerLevel;
			it->voice->SetFrequencyRatio((std::clamp)(it->pitch * dopplerPitch, 0.01f, 2.0f));

			// Mono 音源は等電力パンにし、中央で音量が落ちすぎないようにする。
			if (it->clip != nullptr && it->clip->channelCount == 1u) {
				const Vector3 listenerRight = GetListenerRight();
				const float horizontalLength = std::sqrt(dx * dx + dz * dz);
				float pan = horizontalLength > 0.0001f
					? (std::clamp)((dx * listenerRight.x + dz * listenerRight.z) / horizontalLength, -1.0f, 1.0f)
					: 0.0f;
				const float spreadRatio = (std::clamp)(audioSource->audioSpread / 360.0f, 0.0f, 1.0f);
				pan *= 1.0f - spreadRatio * 0.5f;
				const float leftFactor = std::sqrt((1.0f - pan) * 0.5f);
				const float rightFactor = std::sqrt((1.0f + pan) * 0.5f);
				const float finalLeft = 1.0f * (1.0f - it->spatialBlend) + leftFactor * it->spatialBlend;
				const float finalRight = 1.0f * (1.0f - it->spatialBlend) + rightFactor * it->spatialBlend;
				const float matrix[2] = {finalLeft, finalRight};
				it->voice->SetOutputMatrix(nullptr, 1, 2, matrix);
			}
		}
		else {
			it->voice->SetVolume(GetMixedVolume(*it));
			it->pitch = audioSource->audioPitch;
			it->voice->SetFrequencyRatio((std::clamp)(it->pitch, 0.01f, 2.0f));
			it->occlusion = 0.0f;
		}

		ApplyVoiceFilter(*it, gameObject->id);
		++it;
	}
}

void EditorAudioManager::Stop() {
	for (auto& audio : activeAudios_) {
		StopClip(audio);
	}
	activeAudios_.clear();
}

void EditorAudioManager::Stop(int32_t gameObjectId) {
	for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end();) {
		if (activeIterator->gameObjectId != gameObjectId) {
			activeIterator++;
			continue;
		}

		StopClip(*activeIterator);
		activeIterator = activeAudios_.erase(activeIterator);
	}
}

void EditorAudioManager::Draw() {
}

void EditorAudioManager::SetMasterVolume(float volume) {
	masterVolume_ = (std::clamp)(volume, 0.0f, 1.0f);
}

float EditorAudioManager::GetMasterVolume() const {
	return masterVolume_;
}

void EditorAudioManager::SetBusVolume(EditorAudioBus audioBus, float volume) {
	const size_t audioBusIndex = static_cast<size_t>(audioBus);

	if (audioBusIndex >= busVolumes_.size()) {
		return;
	}

	busVolumes_[audioBusIndex] = (std::clamp)(volume, 0.0f, 1.0f);
}

float EditorAudioManager::GetBusVolume(EditorAudioBus audioBus) const {
	const size_t audioBusIndex = static_cast<size_t>(audioBus);

	if (audioBusIndex >= busVolumes_.size()) {
		return 1.0f;
	}

	return busVolumes_[audioBusIndex];
}

void EditorAudioManager::ApplyVoiceFilter(ActiveAudio& audio, int32_t filterGameObjectId) const {
	const auto* gameObject = editorScene_->FindGameObject(filterGameObjectId);

	if (gameObject == nullptr || audio.voice == nullptr) {
		return;
	}

	const auto* lowPass = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioLowPassFilter);
	const auto* highPass = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioHighPassFilter);
	const auto* reverb = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioReverbFilter);
	const auto* audioSource = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioSource);

	AudioFilterMode targetMode = AudioFilterMode::None;
	float cutoff = 1.0f;

	if (lowPass != nullptr && lowPass->isActive) {
		targetMode = AudioFilterMode::LowPass;
		cutoff = 1.0f - lowPass->intensity * 0.95f;
	} else if (highPass != nullptr && highPass->isActive) {
		targetMode = AudioFilterMode::HighPass;
		cutoff = highPass->intensity * 0.95f + 0.05f;
	}

	if (audio.occlusion > 0.0f) {
		targetMode = AudioFilterMode::LowPass;
		cutoff = (std::min)(cutoff, 1.0f - audio.occlusion * 0.78f);
	}

	if (audio.filterMode != targetMode || audio.filterCutoff != cutoff) {
		audio.filterMode = targetMode;
		audio.filterCutoff = cutoff;

		if (targetMode != AudioFilterMode::None) {
			XAUDIO2_FILTER_PARAMETERS filterParams{};
			filterParams.Frequency = cutoff;
			filterParams.OneOverQ = 1.0f;
			filterParams.Type = (targetMode == AudioFilterMode::LowPass)
				? LowPassFilter : HighPassFilter;
			audio.voice->SetFilterParameters(&filterParams);
		}
		else {
			XAUDIO2_FILTER_PARAMETERS filterParams{};
			filterParams.Frequency = 1.0f;
			filterParams.OneOverQ = 1.0f;
			filterParams.Type = LowPassFilter;
			audio.voice->SetFilterParameters(&filterParams);
		}
	}

	// 乾音をMasterへ残し、残響と初期反射だけをReverb Submixへ並列送信する。
	float reverbAmount = GetListenerReverbAmount();

	if (audioSource != nullptr) {
		reverbAmount = (std::max)(
			reverbAmount,
			(std::clamp)(audioSource->audioReverbSend + audioSource->audioReflectionStrength * 0.65f, 0.0f, 1.0f));
	}

	if (reverb != nullptr && reverb->isActive) {
		reverbAmount = (std::max)(reverbAmount, (std::clamp)(reverb->intensity, 0.0f, 1.0f));
	}

	if (reverbAmount > 0.001f && reverbSubmix_ != nullptr && g_masterVoice != nullptr) {
		XAUDIO2_SEND_DESCRIPTOR sendDescriptors[2]{};
		sendDescriptors[0].Flags = 0u;
		sendDescriptors[0].pOutputVoice = g_masterVoice;
		sendDescriptors[1].Flags = 0u;
		sendDescriptors[1].pOutputVoice = reverbSubmix_;
		XAUDIO2_VOICE_SENDS sendList{};
		sendList.SendCount = 2u;
		sendList.pSends = sendDescriptors;
		audio.voice->SetOutputVoices(&sendList);

		const uint32_t sourceChannelCount = audio.clip != nullptr
			? static_cast<uint32_t>(audio.clip->channelCount)
			: 1u;
		if (sourceChannelCount == 1u) {
			const float reverbMatrix[2] = {reverbAmount, reverbAmount};
			audio.voice->SetOutputMatrix(reverbSubmix_, 1u, 2u, reverbMatrix);
		}
	}
	else {
		audio.voice->SetOutputVoices(nullptr);
	}
}

int32_t EditorAudioManager::GetListenerGameObjectId() const {
	if (editorScene_ == nullptr) {
		return -1;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* listener = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::AudioListener);

		if (listener != nullptr && listener->isActive) {
			return gameObject.id;
		}
	}

	return -1;
}

Vector3 EditorAudioManager::GetListenerPosition() const {
	if (editorScene_ == nullptr) return {0.0f, 0.0f, 0.0f};

	for (const auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;
		const auto* listener = EditorComponentUtility::FindComponent(
			gameObject, EditorComponentType::AudioListener);
		if (listener != nullptr && listener->isActive) {
			return gameObject.translate;
		}
	}

	return {0.0f, 0.0f, 0.0f};
}

Vector3 EditorAudioManager::GetListenerRight() const {
	if (editorScene_ == nullptr) {
		return {1.0f, 0.0f, 0.0f};
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* listener = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::AudioListener);

		if (listener == nullptr || !listener->isActive) {
			continue;
		}

		return {
			std::cos(gameObject.rotate.y),
			0.0f,
			-std::sin(gameObject.rotate.y)};
	}

	return {1.0f, 0.0f, 0.0f};
}

float EditorAudioManager::GetListenerReverbAmount() const {
	if (editorScene_ == nullptr) {
		return 0.0f;
	}

	const Vector3 listenerPosition = GetListenerPosition();
	float strongestReverbAmount = 0.0f;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		const EditorComponent* reverbZone = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::AudioReverbZone);

		if (reverbZone == nullptr || !reverbZone->isActive) {
			continue;
		}

		const float dx = listenerPosition.x - gameObject.translate.x;
		const float dy = listenerPosition.y - gameObject.translate.y;
		const float dz = listenerPosition.z - gameObject.translate.z;
		const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		const float innerDistance = (std::max)(reverbZone->colliderRadius, 0.0f);
		const float outerDistance = (std::max)(reverbZone->colliderSize.x, innerDistance + 0.01f);

		if (distance >= outerDistance) {
			continue;
		}

		const float distanceRatio = distance <= innerDistance
			? 1.0f
			: 1.0f - (distance - innerDistance) / (outerDistance - innerDistance);
		strongestReverbAmount = (std::max)(
			strongestReverbAmount,
			(std::clamp)(distanceRatio * reverbZone->intensity, 0.0f, 1.0f));
	}

	return strongestReverbAmount;
}

float EditorAudioManager::GetMixedVolume(const ActiveAudio& audio) const {
	const size_t audioBusIndex = static_cast<size_t>((std::clamp)(audio.audioBus, 0, 3));
	return
		(std::clamp)(audio.volume, 0.0f, 1.0f) *
		masterVolume_ *
		busVolumes_[audioBusIndex];
}

AudioClip* EditorAudioManager::CreateBuiltinClip(const std::string& path) {
	constexpr uint32_t sampleRate = 44100u;
	constexpr uint16_t channelCount = 1u;
	constexpr uint16_t bitsPerSample = 16u;
	constexpr float pi = 3.14159265358979323846f;
	float duration = 0.0f;

	if (path == "builtin://rail-cannon") {
		duration = 0.20f;
	}
	else if (path == "builtin://water-impact") {
		duration = 0.70f;
	}
	else if (path == "builtin://ocean-ambience") {
		duration = 4.0f;
	}
	else if (path == "builtin://stage-start") {
		duration = 0.45f;
	}
	else if (path == "builtin://stage-complete") {
		duration = 1.20f;
	}
	else {
		return nullptr;
	}

	const uint32_t sampleCount = static_cast<uint32_t>(duration * static_cast<float>(sampleRate));
	const uint32_t bufferSize = sampleCount * sizeof(int16_t);
	AudioClip clip{};
	clip.pBuffer = new unsigned char[static_cast<size_t>(bufferSize)];
	clip.bufferSize = bufferSize;
	clip.formatTag = WAVE_FORMAT_PCM;
	clip.channelCount = channelCount;
	clip.sampleRate = sampleRate;
	clip.averageBytesPerSecond = static_cast<uint32_t>(sampleRate * sizeof(int16_t));
	clip.blockAlign = static_cast<uint16_t>(sizeof(int16_t));
	clip.bitsPerSample = bitsPerSample;
	int16_t* samples = reinterpret_cast<int16_t*>(clip.pBuffer);
	uint32_t randomState = 0x6D2B79F5u;
	float filteredNoise = 0.0f;

	for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; sampleIndex++) {
		const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRate);
		randomState = randomState * 1664525u + 1013904223u;
		const float randomValue =
			static_cast<float>((randomState >> 8u) & 0x00FFFFFFu) /
			static_cast<float>(0x007FFFFFu) - 1.0f;
		float sampleValue = 0.0f;

		if (path == "builtin://rail-cannon") {
			const float envelope = std::exp(-18.0f * time);
			const float body = std::sin(2.0f * pi * (92.0f - time * 180.0f) * time);
			sampleValue = (body * 0.72f + randomValue * 0.48f) * envelope;
		}
		else if (path == "builtin://water-impact") {
			filteredNoise += (randomValue - filteredNoise) * 0.08f;
			const float envelope = std::exp(-4.8f * time);
			const float lowBody = std::sin(2.0f * pi * 48.0f * time) * std::exp(-9.0f * time);
			sampleValue = filteredNoise * envelope * 1.6f + lowBody * 0.42f;
		}
		else if (path == "builtin://ocean-ambience") {
			const float normalizedTime = time / duration;
			const float swell =
				std::sin(2.0f * pi * normalizedTime * 2.0f) * 0.34f +
				std::sin(2.0f * pi * normalizedTime * 5.0f) * 0.20f +
				std::sin(2.0f * pi * normalizedTime * 11.0f) * 0.10f;
			const float wash =
				std::sin(2.0f * pi * normalizedTime * 53.0f) * 0.06f +
				std::sin(2.0f * pi * normalizedTime * 97.0f) * 0.035f;
			sampleValue = swell + wash;
		}
		else if (path == "builtin://stage-start") {
			const float normalizedTime = time / duration;
			const float envelope = std::sin(pi * normalizedTime);
			const float frequency = 360.0f + normalizedTime * 520.0f;
			sampleValue = std::sin(2.0f * pi * frequency * time) * envelope * 0.55f;
		}
		else {
			const float normalizedTime = time / duration;
			const float envelope = std::sin(pi * (std::min)(normalizedTime * 1.15f, 1.0f));
			sampleValue =
				(std::sin(2.0f * pi * 523.25f * time) * 0.30f +
					std::sin(2.0f * pi * 659.25f * time) * 0.24f +
					std::sin(2.0f * pi * 783.99f * time) * 0.20f) * envelope;
		}

		const float clampedSample = (std::clamp)(sampleValue, -1.0f, 1.0f);
		samples[sampleIndex] = static_cast<int16_t>(clampedSample * 32767.0f);
	}

	auto result = clips_.emplace(path, std::move(clip));
	return &result.first->second;
}

AudioClip* EditorAudioManager::LoadClip(const std::string& path) {
	auto it = clips_.find(path);

	if (it != clips_.end()) {
		return &it->second;
	}

	if (path.starts_with("builtin://")) {
		return CreateBuiltinClip(path);
	}

	std::ifstream file(path, std::ios_base::binary);

	if (!file.is_open()) {
		return nullptr;
	}

	file.seekg(0, std::ios_base::end);
	const std::streamoff fileSize = file.tellg();
	file.seekg(0, std::ios_base::beg);

	if (fileSize < static_cast<std::streamoff>(sizeof(WaveRiffHeader))) {
		return nullptr;
	}

	WaveRiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

	if (std::strncmp(riff.chunk.id, "RIFF", 4) != 0 ||
		std::strncmp(riff.type, "WAVE", 4) != 0 ||
		!file) {
		return nullptr;
	}

	PcmWaveFormat waveFormat{};
	bool hasWaveFormat = false;
	AudioClip clip{};

	while (file) {
		const std::streamoff chunkHeaderPosition = file.tellg();

		if (chunkHeaderPosition < 0 ||
			chunkHeaderPosition + static_cast<std::streamoff>(sizeof(WaveChunkHeader)) > fileSize) {
			break;
		}

		WaveChunkHeader chunk{};
		file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));

		if (!file) {
			return nullptr;
		}

		const std::streamoff chunkDataPosition = file.tellg();
		const std::streamoff chunkDataSize = static_cast<std::streamoff>(chunk.size);
		const std::streamoff chunkEndPosition = chunkDataPosition + chunkDataSize;

		if (chunkDataSize < 0 || chunkEndPosition < chunkDataPosition || chunkEndPosition > fileSize) {
			return nullptr;
		}

		if (std::strncmp(chunk.id, "fmt ", 4) == 0) {
			if (chunk.size < sizeof(PcmWaveFormat)) {
				return nullptr;
			}

			file.read(reinterpret_cast<char*>(&waveFormat), sizeof(waveFormat));

			if (!file ||
				(waveFormat.formatTag != WAVE_FORMAT_PCM &&
					waveFormat.formatTag != WAVE_FORMAT_IEEE_FLOAT) ||
				waveFormat.channelCount == 0u ||
				waveFormat.sampleRate == 0u ||
				waveFormat.averageBytesPerSecond == 0u ||
				waveFormat.blockAlign == 0u ||
				waveFormat.bitsPerSample == 0u) {
				return nullptr;
			}

			hasWaveFormat = true;
		}
		else if (std::strncmp(chunk.id, "data", 4) == 0) {
			if (!hasWaveFormat ||
				chunk.size == 0u) {
				return nullptr;
			}

			clip.pBuffer = new unsigned char[static_cast<size_t>(chunk.size)];
			file.read(
				reinterpret_cast<char*>(clip.pBuffer),
				static_cast<std::streamsize>(chunk.size));

			if (!file) {
				delete[] static_cast<unsigned char*>(clip.pBuffer);
				clip.pBuffer = nullptr;
				return nullptr;
			}

			clip.bufferSize = chunk.size;
			clip.formatTag = waveFormat.formatTag;
			clip.channelCount = waveFormat.channelCount;
			clip.sampleRate = waveFormat.sampleRate;
			clip.averageBytesPerSecond = waveFormat.averageBytesPerSecond;
			clip.blockAlign = waveFormat.blockAlign;
			clip.bitsPerSample = waveFormat.bitsPerSample;
			break;
		}

		const std::streamoff paddedChunkEndPosition =
			chunkEndPosition + static_cast<std::streamoff>(chunk.size & 1u);

		if (paddedChunkEndPosition > fileSize) {
			return nullptr;
		}

		file.seekg(paddedChunkEndPosition, std::ios_base::beg);
	}

	if (clip.pBuffer == nullptr || clip.bufferSize == 0u) {
		return nullptr;
	}

	auto result = clips_.emplace(path, std::move(clip));
	return &result.first->second;
}

void EditorAudioManager::StopClip(ActiveAudio& audio) {
	if (audio.voice != nullptr) {
		audio.voice->Stop(0);
		audio.voice->FlushSourceBuffers();
		audio.voice->DestroyVoice();
		audio.voice = nullptr;
	}
}
