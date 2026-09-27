#pragma warning(disable : 4514)

#include "EditorAudioManager.h"

#include "EditorComponentUtility.h"
#include "EditorPhysicsManager.h"
#include "EditorSharedState.h"
#include "Source/Engine/Asset/AssetImportSettings.h"
#include "Source/Engine/Asset/AssetRegistry.h"
#include "Source/Engine/Core/ProjectSettings.h"

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
	isPaused_ = false;
	lastPlaybackTimeByGameObjectId_.clear();

	// Project Settings の初期音量を Play 開始時に反映する。
	// Play 中に Inspector から変更した値は、次の Play で再びここから開始する。
	const ProjectSettingsData& projectSettings = ProjectSettings::Get().GetData();
	masterVolume_ = projectSettings.masterVolume;
	busVolumes_[static_cast<size_t>(EditorAudioBus::Sfx)] = projectSettings.sfxVolume;
	busVolumes_[static_cast<size_t>(EditorAudioBus::Bgm)] = projectSettings.bgmVolume;
	busVolumes_[static_cast<size_t>(EditorAudioBus::Ambience)] = projectSettings.ambienceVolume;
	busVolumes_[static_cast<size_t>(EditorAudioBus::Ui)] = projectSettings.uiVolume;
	busVolumes_[static_cast<size_t>(EditorAudioBus::Voice)] = projectSettings.voiceVolume;

	// Import Settingsでpreload指定されたAudio ClipをPlay開始時に一度先読みし、
	// 初回再生時のFile読込による遅延を減らす(音は鳴らさない)。
	for (const auto& gameObject : editorScene_->GetGameObjects()) {
		const auto* preloadAudioSource = EditorComponentUtility::FindComponent(
			gameObject, EditorComponentType::AudioSource);

		if (preloadAudioSource == nullptr || preloadAudioSource->assetPath.empty()) {
			continue;
		}

		const AssetRecord* preloadRecord = AssetRegistry::Get().FindByPath(preloadAudioSource->assetPath);
		if (preloadRecord == nullptr) {
			continue;
		}

		const AssetImportMetadata* preloadMetadata = AssetImportSettingsStore::Get().Find(preloadRecord->id);
		if (preloadMetadata != nullptr && preloadMetadata->audio.preloadOnPlayStart) {
			PreloadClip(preloadAudioSource->assetPath);
		}
	}

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
	return PlayWithHandle(gameObjectId) != kInvalidAudioVoiceHandle;
}

AudioVoiceHandle EditorAudioManager::PlayWithHandle(int32_t gameObjectId) {
	if (editorScene_ == nullptr || xAudio2_ == nullptr) {
		return kInvalidAudioVoiceHandle;
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
		return kInvalidAudioVoiceHandle;
	}

	const float retriggerInterval = (std::max)(audioSource->audioRetriggerInterval, 0.0f);
	const auto lastPlaybackIterator = lastPlaybackTimeByGameObjectId_.find(gameObjectId);

	if (lastPlaybackIterator != lastPlaybackTimeByGameObjectId_.end() &&
		playbackClock_ - lastPlaybackIterator->second < retriggerInterval) {
		return kInvalidAudioVoiceHandle;
	}

	AudioClip* clip = LoadClip(audioSource->assetPath);

	if (clip == nullptr ||
		clip->pBuffer == nullptr ||
		clip->bufferSize == 0u ||
		clip->channelCount == 0u ||
		clip->sampleRate == 0u ||
		clip->blockAlign == 0u ||
		(clip->formatTag != WAVE_FORMAT_PCM && clip->formatTag != WAVE_FORMAT_IEEE_FLOAT)) {
		return kInvalidAudioVoiceHandle;
	}

	ActiveAudio active{};
	active.gameObjectId = gameObjectId;
	active.clip = clip;
	active.volume = audioSource->audioVolume;
	active.pitch = audioSource->audioPitch;
	active.spatialBlend = audioSource->audioSpatialBlend;
	active.minDistance = audioSource->audioMinDistance;
	active.maxDistance = audioSource->audioMaxDistance;
	active.audioBus = (std::clamp)(
		audioSource->audioBus, 0, static_cast<int32_t>(EditorAudioBus::Count) - 1);
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

	// 同じGameObjectからの再生数はaudioMaxVoicesで抑えられるが、多数のGameObjectが
	// それぞれ同じSEを1回ずつ鳴らすケース(弾着弾の乱発等)はここでは防げない。
	// Scene全体のVoice総数をここで別途上限し、一番古いVoiceを追い出して無制限増加を防ぐ。
	if (static_cast<int32_t>(activeAudios_.size()) >= maxGlobalVoiceCount_) {
		auto globalOldestIterator = activeAudios_.begin();

		for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end(); activeIterator++) {
			if (activeIterator->playbackAge > globalOldestIterator->playbackAge) {
				globalOldestIterator = activeIterator;
			}
		}

		if (globalOldestIterator != activeAudios_.end()) {
			StopClip(*globalOldestIterator);
			activeAudios_.erase(globalOldestIterator);
		}
	}

	const AudioVoiceHandle handle = StartVoice(active, audioSource->audioPitch);

	if (handle == kInvalidAudioVoiceHandle) {
		return kInvalidAudioVoiceHandle;
	}

	lastPlaybackTimeByGameObjectId_[gameObjectId] = playbackClock_;
	return handle;
}

AudioVoiceHandle EditorAudioManager::StartVoice(ActiveAudio& prepared, float initialPitch) {
	AudioClip* clip = prepared.clip;

	if (xAudio2_ == nullptr || clip == nullptr) {
		return kInvalidAudioVoiceHandle;
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
		return kInvalidAudioVoiceHandle;
	}

	prepared.voice = voice;
	XAUDIO2_BUFFER buffer{};
	buffer.AudioBytes = clip->bufferSize;
	buffer.pAudioData = static_cast<const BYTE*>(clip->pBuffer);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.LoopCount = prepared.loop ? XAUDIO2_LOOP_INFINITE : 0u;
	hr = voice->SubmitSourceBuffer(&buffer);

	if (FAILED(hr)) {
		voice->DestroyVoice();
		prepared.voice = nullptr;
		return kInvalidAudioVoiceHandle;
	}

	prepared.pitch = initialPitch;
	prepared.handle = nextVoiceHandle_++;
	voice->SetVolume(GetMixedVolume(prepared));
	voice->SetFrequencyRatio((std::clamp)(initialPitch, 0.01f, 2.0f));

	if (!isPaused_) {
		voice->Start(0u);
	}

	activeAudios_.push_back(prepared);
	return prepared.handle;
}

AudioVoiceHandle EditorAudioManager::PlayClipAtPosition(
	const std::string& assetPath,
	const Vector3& position,
	int32_t audioBus,
	float volume,
	bool loop,
	float spatialBlend) {
	if (xAudio2_ == nullptr || assetPath.empty()) {
		return kInvalidAudioVoiceHandle;
	}

	AudioClip* clip = LoadClip(assetPath);

	if (clip == nullptr ||
		clip->pBuffer == nullptr ||
		clip->bufferSize == 0u ||
		clip->channelCount == 0u ||
		clip->sampleRate == 0u ||
		clip->blockAlign == 0u ||
		(clip->formatTag != WAVE_FORMAT_PCM && clip->formatTag != WAVE_FORMAT_IEEE_FLOAT)) {
		return kInvalidAudioVoiceHandle;
	}

	// Scene全体のVoice上限はAudioSource経由と同じ基準で守る。
	if (static_cast<int32_t>(activeAudios_.size()) >= maxGlobalVoiceCount_) {
		auto globalOldestIterator = activeAudios_.begin();

		for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end(); activeIterator++) {
			if (activeIterator->playbackAge > globalOldestIterator->playbackAge) {
				globalOldestIterator = activeIterator;
			}
		}

		if (globalOldestIterator != activeAudios_.end()) {
			StopClip(*globalOldestIterator);
			activeAudios_.erase(globalOldestIterator);
		}
	}

	ActiveAudio active{};
	active.gameObjectId = -1;
	active.clip = clip;
	active.isDetached = true;
	active.detachedPosition = position;
	active.volume = (std::clamp)(volume, 0.0f, 1.0f);
	active.hasScriptVolume = true;
	active.hasScriptPitch = true;
	active.spatialBlend = (std::clamp)(spatialBlend, 0.0f, 1.0f);
	active.minDistance = 1.0f;
	active.maxDistance = 50.0f;
	active.audioBus = (std::clamp)(audioBus, 0, static_cast<int32_t>(EditorAudioBus::Count) - 1);
	active.loop = loop;
	active.filterComponentId = -1;
	return StartVoice(active, 1.0f);
}

void EditorAudioManager::Update(float deltaTime) {
	if (isPaused_) {
		return;
	}

	playbackClock_ += (std::max)(deltaTime, 0.0f);

	if (editorScene_ == nullptr || xAudio2_ == nullptr) {
		return;
	}

	for (auto it = activeAudios_.begin(); it != activeAudios_.end();) {
		it->playbackAge += (std::max)(deltaTime, 0.0f);
		UpdateVoiceFade(*it, deltaTime);

		if (it->voice == nullptr) {
			// Fade Out完了で停止済み。Slotだけ回収する。
			it = activeAudios_.erase(it);
			continue;
		}

		if (it->isDetached) {
			XAUDIO2_VOICE_STATE detachedState;
			it->voice->GetState(&detachedState);

			if (!it->loop && !it->isVoicePaused && detachedState.BuffersQueued == 0) {
				StopClip(*it);
				it = activeAudios_.erase(it);
				continue;
			}

			UpdateDetachedVoice(*it);
			++it;
			continue;
		}

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

		// Scriptが個別音量を指定したVoiceは、Component値で毎フレーム上書きしない。
		if (!it->hasScriptVolume && it->volume != audioSource->audioVolume) {
			it->volume = audioSource->audioVolume;
		}

		it->spatialBlend = (std::clamp)(audioSource->audioSpatialBlend, 0.0f, 1.0f);
		it->minDistance = (std::max)(audioSource->audioMinDistance, 0.01f);
		it->maxDistance = (std::max)(audioSource->audioMaxDistance, it->minDistance + 0.01f);
		it->audioBus = (std::clamp)(
			audioSource->audioBus, 0, static_cast<int32_t>(EditorAudioBus::Count) - 1);

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
			if (!it->hasScriptPitch) {
				it->pitch = audioSource->audioPitch;
			}
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
			if (!it->hasScriptPitch) {
				it->pitch = audioSource->audioPitch;
			}
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
	isPaused_ = false;
}

void EditorAudioManager::InvalidateClip(const std::string& assetPath) {
	const auto clipIterator = clips_.find(assetPath);
	if (clipIterator == clips_.end()) {
		return;
	}

	AudioClip* invalidatedClip = &clipIterator->second;
	for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end();) {
		if (activeIterator->clip != invalidatedClip) {
			++activeIterator;
			continue;
		}

		StopClip(*activeIterator);
		activeIterator = activeAudios_.erase(activeIterator);
	}

	delete[] static_cast<unsigned char*>(clipIterator->second.pBuffer);
	clipIterator->second.pBuffer = nullptr;
	clips_.erase(clipIterator);
}

int32_t EditorAudioManager::GetActiveVoiceCount() const {
	return static_cast<int32_t>(activeAudios_.size());
}

int32_t EditorAudioManager::GetLoadedClipCount() const {
	return static_cast<int32_t>(clips_.size());
}

AudioClipInfo EditorAudioManager::GetClipInfo(const std::string& path) {
	AudioClipInfo info{};
	const AudioClip* clip = LoadClip(path);

	if (clip == nullptr || clip->pBuffer == nullptr) {
		return info;
	}

	info.isLoaded = true;
	info.channelCount = static_cast<int32_t>(clip->channelCount);
	info.sampleRate = clip->sampleRate;
	info.bitsPerSample = clip->bitsPerSample;
	info.durationSeconds = clip->averageBytesPerSecond > 0u
		? static_cast<float>(clip->bufferSize) / static_cast<float>(clip->averageBytesPerSecond)
		: 0.0f;
	return info;
}

bool EditorAudioManager::PreloadClip(const std::string& path) {
	return LoadClip(path) != nullptr;
}

void EditorAudioManager::SetPaused(bool isPaused) {
	if (isPaused_ == isPaused) {
		return;
	}

	isPaused_ = isPaused;

	for (ActiveAudio& audio : activeAudios_) {
		if (audio.voice == nullptr) {
			continue;
		}

		if (isPaused_) {
			audio.voice->Stop(0u);
		}
		else if (!audio.isVoicePaused) {
			// Scriptが個別Pauseしたままのvoiceは、全体Resumeでも鳴らさない。
			audio.voice->Start(0u);
		}
	}
}

bool EditorAudioManager::IsPaused() const {
	return isPaused_;
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

void EditorAudioManager::SetMasterMute(bool isMuted) {
	masterMuted_ = isMuted;
}

bool EditorAudioManager::IsMasterMuted() const {
	return masterMuted_;
}

void EditorAudioManager::SetBusMute(EditorAudioBus audioBus, bool isMuted) {
	const size_t audioBusIndex = static_cast<size_t>(audioBus);

	if (audioBusIndex >= busMutes_.size()) {
		return;
	}

	busMutes_[audioBusIndex] = isMuted;
}

bool EditorAudioManager::IsBusMuted(EditorAudioBus audioBus) const {
	const size_t audioBusIndex = static_cast<size_t>(audioBus);
	return audioBusIndex < busMutes_.size() && busMutes_[audioBusIndex];
}

void EditorAudioManager::SetMaxGlobalVoiceCount(int32_t maxVoiceCount) {
	maxGlobalVoiceCount_ = (std::max)(maxVoiceCount, 1);
}

int32_t EditorAudioManager::GetMaxGlobalVoiceCount() const {
	return maxGlobalVoiceCount_;
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
	const auto* echo = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioEchoFilter);
	const auto* distortion = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioDistortionFilter);
	const auto* chorus = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioChorusFilter);
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

	if (distortion != nullptr && distortion->isActive) {
		targetMode = AudioFilterMode::LowPass;
		cutoff = (std::min)(cutoff, 1.0f - (std::clamp)(distortion->intensity, 0.0f, 1.0f) * 0.45f);
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

	if (echo != nullptr && echo->isActive) {
		reverbAmount = (std::max)(
			reverbAmount,
			(std::clamp)(echo->intensity * 0.90f, 0.0f, 1.0f));
	}

	if (chorus != nullptr && chorus->isActive) {
		reverbAmount = (std::max)(
			reverbAmount,
			(std::clamp)(chorus->intensity * 0.35f, 0.0f, 1.0f));
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

	if (distortion != nullptr && distortion->isActive) {
		float currentVolume = 1.0f;
		audio.voice->GetVolume(&currentVolume);
		const float drive = 1.0f + (std::clamp)(distortion->intensity, 0.0f, 1.0f) * 2.5f;
		audio.voice->SetVolume((std::min)(currentVolume * drive, 4.0f));
	}

	if (chorus != nullptr && chorus->isActive) {
		float currentFrequencyRatio = 1.0f;
		audio.voice->GetFrequencyRatio(&currentFrequencyRatio);
		const float chorusDepth = (std::clamp)(chorus->intensity, 0.0f, 1.0f) * 0.025f;
		const float chorusRatio = 1.0f + std::sin(playbackClock_ * 5.02654825f) * chorusDepth;
		audio.voice->SetFrequencyRatio((std::clamp)(currentFrequencyRatio * chorusRatio, 0.01f, 2.0f));
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
	if (masterMuted_) {
		return 0.0f;
	}

	const size_t audioBusIndex = static_cast<size_t>((std::clamp)(
		audio.audioBus, 0, static_cast<int32_t>(EditorAudioBus::Count) - 1));

	if (busMutes_[audioBusIndex]) {
		return 0.0f;
	}

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

ActiveAudio* EditorAudioManager::FindVoice(AudioVoiceHandle handle) {
	if (handle == kInvalidAudioVoiceHandle) {
		return nullptr;
	}

	for (ActiveAudio& audio : activeAudios_) {
		if (audio.handle == handle && audio.voice != nullptr) {
			return &audio;
		}
	}

	return nullptr;
}

const ActiveAudio* EditorAudioManager::FindVoice(AudioVoiceHandle handle) const {
	if (handle == kInvalidAudioVoiceHandle) {
		return nullptr;
	}

	for (const ActiveAudio& audio : activeAudios_) {
		if (audio.handle == handle && audio.voice != nullptr) {
			return &audio;
		}
	}

	return nullptr;
}

void EditorAudioManager::UpdateDetachedVoice(ActiveAudio& audio) {
	if (audio.voice == nullptr) {
		return;
	}

	const float mixedVolume = GetMixedVolume(audio);

	if (audio.spatialBlend <= 0.0f) {
		audio.voice->SetVolume(mixedVolume);
		audio.voice->SetFrequencyRatio((std::clamp)(audio.pitch, 0.01f, 2.0f));
		return;
	}

	const Vector3 listenerPosition = GetListenerPosition();
	const float dx = audio.detachedPosition.x - listenerPosition.x;
	const float dy = audio.detachedPosition.y - listenerPosition.y;
	const float dz = audio.detachedPosition.z - listenerPosition.z;
	const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	const float minimumDistance = (std::max)(audio.minDistance, 0.01f);
	const float maximumDistance = (std::max)(audio.maxDistance, minimumDistance + 0.01f);
	const float normalized = (std::clamp)(
		(distance - minimumDistance) / (maximumDistance - minimumDistance), 0.0f, 1.0f);
	const float smoothDistance = normalized * normalized * (3.0f - 2.0f * normalized);
	const float distanceAttenuation = 1.0f - smoothDistance;
	const float volume3d = mixedVolume * distanceAttenuation;
	audio.voice->SetVolume(mixedVolume * (1.0f - audio.spatialBlend) + volume3d * audio.spatialBlend);
	audio.voice->SetFrequencyRatio((std::clamp)(audio.pitch, 0.01f, 2.0f));

	if (audio.clip != nullptr && audio.clip->channelCount == 1u) {
		const Vector3 listenerRight = GetListenerRight();
		const float horizontalLength = std::sqrt(dx * dx + dz * dz);
		const float pan = horizontalLength > 0.0001f
			? (std::clamp)((dx * listenerRight.x + dz * listenerRight.z) / horizontalLength, -1.0f, 1.0f)
			: 0.0f;
		const float leftFactor = std::sqrt((1.0f - pan) * 0.5f);
		const float rightFactor = std::sqrt((1.0f + pan) * 0.5f);
		const float matrix[2] = {
			1.0f * (1.0f - audio.spatialBlend) + leftFactor * audio.spatialBlend,
			1.0f * (1.0f - audio.spatialBlend) + rightFactor * audio.spatialBlend};
		audio.voice->SetOutputMatrix(nullptr, 1, 2, matrix);
	}
}

void EditorAudioManager::UpdateVoiceFade(ActiveAudio& audio, float deltaTime) {
	if (audio.fadeRemainingSeconds <= 0.0f || audio.voice == nullptr) {
		return;
	}

	audio.fadeRemainingSeconds -= (std::max)(deltaTime, 0.0f);

	if (audio.fadeRemainingSeconds <= 0.0f) {
		audio.fadeRemainingSeconds = 0.0f;
		audio.volume = audio.fadeTargetVolume;

		// Fade Outし切ったVoiceは鳴らし続けても無音なので解放する。
		if (audio.fadeTargetVolume <= 0.0f) {
			StopClip(audio);
			return;
		}
	}
	else {
		const float elapsedRatio = audio.fadeTotalSeconds > 0.0f
			? (std::clamp)(1.0f - audio.fadeRemainingSeconds / audio.fadeTotalSeconds, 0.0f, 1.0f)
			: 1.0f;
		audio.volume = audio.fadeStartVolume + (audio.fadeTargetVolume - audio.fadeStartVolume) * elapsedRatio;
	}

	audio.voice->SetVolume(GetMixedVolume(audio));
}

bool EditorAudioManager::StopVoice(AudioVoiceHandle handle) {
	for (auto activeIterator = activeAudios_.begin(); activeIterator != activeAudios_.end(); ++activeIterator) {
		if (activeIterator->handle != handle || handle == kInvalidAudioVoiceHandle) {
			continue;
		}

		StopClip(*activeIterator);
		activeAudios_.erase(activeIterator);
		return true;
	}

	return false;
}

bool EditorAudioManager::SetVoicePaused(AudioVoiceHandle handle, bool isPaused) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	audio->isVoicePaused = isPaused;

	// 全体Pause中は個別Resumeで鳴らさない(再開はSetPaused(false)側でまとめて行う)。
	if (isPaused) {
		audio->voice->Stop(0u);
	}
	else if (!isPaused_) {
		audio->voice->Start(0u);
	}

	return true;
}

bool EditorAudioManager::IsVoicePlaying(AudioVoiceHandle handle) const {
	const ActiveAudio* audio = FindVoice(handle);
	return audio != nullptr && !audio->isVoicePaused;
}

bool EditorAudioManager::IsVoiceValid(AudioVoiceHandle handle) const {
	return FindVoice(handle) != nullptr;
}

bool EditorAudioManager::SetVoiceVolume(AudioVoiceHandle handle, float volume) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	audio->volume = (std::clamp)(volume, 0.0f, 1.0f);
	audio->hasScriptVolume = true;
	audio->fadeRemainingSeconds = 0.0f;  // 明示指定はFadeより優先する
	audio->voice->SetVolume(GetMixedVolume(*audio));
	return true;
}

bool EditorAudioManager::GetVoiceVolume(AudioVoiceHandle handle, float& volume) const {
	const ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	volume = audio->volume;
	return true;
}

bool EditorAudioManager::SetVoicePitch(AudioVoiceHandle handle, float pitch) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	audio->pitch = (std::clamp)(pitch, 0.01f, 2.0f);
	audio->hasScriptPitch = true;
	audio->voice->SetFrequencyRatio(audio->pitch);
	return true;
}

bool EditorAudioManager::GetVoicePitch(AudioVoiceHandle handle, float& pitch) const {
	const ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	pitch = audio->pitch;
	return true;
}

bool EditorAudioManager::SetVoiceLoop(AudioVoiceHandle handle, bool loop) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	// XAudio2のLoopCountは投入済みBufferの属性なので、Loop解除は「今の再生が終わったら止まる」、
	// Loop開始は「今の再生が終わった後は続かない」。区別が要る場面ではStop後に再生し直す。
	if (!loop && audio->loop) {
		audio->voice->ExitLoop(0u);
	}

	audio->loop = loop;
	return true;
}

bool EditorAudioManager::GetVoiceLoop(AudioVoiceHandle handle, bool& loop) const {
	const ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	loop = audio->loop;
	return true;
}

bool EditorAudioManager::SetVoicePosition(AudioVoiceHandle handle, const Vector3& position) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr || !audio->isDetached) {
		// AudioSource経由のVoiceはGameObjectのTransformが位置なので、ここでは動かさない。
		return false;
	}

	audio->detachedPosition = position;
	UpdateDetachedVoice(*audio);
	return true;
}

bool EditorAudioManager::SetVoiceBus(AudioVoiceHandle handle, int32_t audioBus) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	audio->audioBus = (std::clamp)(audioBus, 0, static_cast<int32_t>(EditorAudioBus::Count) - 1);
	audio->voice->SetVolume(GetMixedVolume(*audio));
	return true;
}

bool EditorAudioManager::GetVoicePlaybackPosition(AudioVoiceHandle handle, float& seconds) const {
	const ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr || audio->clip == nullptr || audio->clip->sampleRate == 0u) {
		return false;
	}

	XAUDIO2_VOICE_STATE state;
	audio->voice->GetState(&state);
	seconds = static_cast<float>(state.SamplesPlayed) / static_cast<float>(audio->clip->sampleRate);
	return true;
}

bool EditorAudioManager::SetVoicePlaybackPosition(AudioVoiceHandle handle, float seconds) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr || audio->clip == nullptr || audio->clip->blockAlign == 0u ||
		audio->clip->sampleRate == 0u) {
		return false;
	}

	const uint32_t totalSampleCount = audio->clip->bufferSize / audio->clip->blockAlign;
	const float clampedSeconds = (std::max)(seconds, 0.0f);
	const uint32_t requestedSample = static_cast<uint32_t>(
		clampedSeconds * static_cast<float>(audio->clip->sampleRate));

	if (totalSampleCount == 0u || requestedSample >= totalSampleCount) {
		return false;
	}

	// XAudio2は再生中Bufferの途中シークを持たないため、投入し直して開始位置を指定する。
	audio->voice->Stop(0u);
	audio->voice->FlushSourceBuffers();

	XAUDIO2_BUFFER buffer{};
	buffer.AudioBytes = audio->clip->bufferSize;
	buffer.pAudioData = static_cast<const BYTE*>(audio->clip->pBuffer);
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.PlayBegin = requestedSample;
	buffer.LoopCount = audio->loop ? XAUDIO2_LOOP_INFINITE : 0u;

	if (FAILED(audio->voice->SubmitSourceBuffer(&buffer))) {
		return false;
	}

	if (!isPaused_ && !audio->isVoicePaused) {
		audio->voice->Start(0u);
	}

	return true;
}

bool EditorAudioManager::GetVoiceDuration(AudioVoiceHandle handle, float& seconds) const {
	const ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr || audio->clip == nullptr || audio->clip->blockAlign == 0u ||
		audio->clip->sampleRate == 0u) {
		return false;
	}

	const uint32_t totalSampleCount = audio->clip->bufferSize / audio->clip->blockAlign;
	seconds = static_cast<float>(totalSampleCount) / static_cast<float>(audio->clip->sampleRate);
	return true;
}

bool EditorAudioManager::FadeVoiceTo(AudioVoiceHandle handle, float targetVolume, float durationSeconds) {
	ActiveAudio* audio = FindVoice(handle);

	if (audio == nullptr) {
		return false;
	}

	const float clampedTarget = (std::clamp)(targetVolume, 0.0f, 1.0f);
	audio->hasScriptVolume = true;

	if (durationSeconds <= 0.0f) {
		audio->fadeRemainingSeconds = 0.0f;
		audio->volume = clampedTarget;
		audio->voice->SetVolume(GetMixedVolume(*audio));
		return true;
	}

	audio->fadeStartVolume = audio->volume;
	audio->fadeTargetVolume = clampedTarget;
	audio->fadeTotalSeconds = durationSeconds;
	audio->fadeRemainingSeconds = durationSeconds;
	return true;
}

void EditorAudioManager::StopClip(ActiveAudio& audio) {
	if (audio.voice != nullptr) {
		audio.voice->Stop(0);
		audio.voice->FlushSourceBuffers();
		audio.voice->DestroyVoice();
		audio.voice = nullptr;
	}
}
