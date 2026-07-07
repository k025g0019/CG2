#pragma warning(disable : 4514)

#include "EditorAudioManager.h"

#include "EditorComponentUtility.h"
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
		int32_t size;
	};

	struct WaveRiffHeader {
		WaveChunkHeader chunk;
		char type[4];
	};

	struct WaveFormatChunk {
		WaveChunkHeader chunk;
		WAVEFORMATEX format;
	};
}

void EditorAudioManager::Initialize(EditorScene* editorScene) {
	editorScene_ = editorScene;
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
	if (editorScene_ == nullptr || xAudio2_ == nullptr) return;

	for (auto& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) continue;

		const auto* audioSource = EditorComponentUtility::FindComponent(
			gameObject, EditorComponentType::AudioSource);
		if (audioSource == nullptr || !audioSource->isActive) continue;
		if (!audioSource->audioPlayOnAwake) continue;
		if (audioSource->assetPath.empty()) continue;

		AudioClip* clip = LoadClip(audioSource->assetPath);
		if (clip == nullptr) continue;

		ActiveAudio active;
		active.gameObjectId = gameObject.id;
		active.clip = clip;
		active.volume = audioSource->audioVolume;
		active.loop = audioSource->audioLoop;
		active.filterComponentId = -1;

		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = clip->channelCount;
		format.nSamplesPerSec = clip->sampleRate;
		format.wBitsPerSample = clip->bitsPerSample;
		format.nBlockAlign = static_cast<WORD>((clip->channelCount * clip->bitsPerSample) / 8);
		format.nAvgBytesPerSec = static_cast<DWORD>(static_cast<uint32_t>(format.nSamplesPerSec) * format.nBlockAlign);
		format.cbSize = 0;

		IXAudio2SourceVoice* voice = nullptr;
		HRESULT hr = xAudio2_->CreateSourceVoice(&voice, &format);
		if (FAILED(hr) || voice == nullptr) continue;

		active.voice = voice;

		XAUDIO2_BUFFER buffer{};
		buffer.AudioBytes = clip->bufferSize;
		buffer.pAudioData = static_cast<const BYTE*>(clip->pBuffer);
		buffer.LoopCount = active.loop ? XAUDIO2_LOOP_INFINITE : 0;

		hr = voice->SubmitSourceBuffer(&buffer);
		if (FAILED(hr)) {
			voice->DestroyVoice();
			continue;
		}

		voice->SetVolume(active.volume);
		voice->Start(0);
		activeAudios_.push_back(active);
	}
}

void EditorAudioManager::Update(float deltaTime) {
	(void)deltaTime;
	if (editorScene_ == nullptr || xAudio2_ == nullptr) return;

	for (auto it = activeAudios_.begin(); it != activeAudios_.end();) {
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
			it->voice->SetVolume(it->volume);
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

void EditorAudioManager::Draw() {
}

void EditorAudioManager::ApplyVoiceFilter(ActiveAudio& audio, int32_t filterGameObjectId) const {
	const auto* gameObject = editorScene_->FindGameObject(filterGameObjectId);
	if (gameObject == nullptr) return;

	const auto* lowPass = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioLowPassFilter);
	const auto* highPass = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioHighPassFilter);
	const auto* reverbZone = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioReverbZone);
	const auto* reverb = EditorComponentUtility::FindComponent(
		*gameObject, EditorComponentType::AudioReverbFilter);

	AudioFilterMode targetMode = AudioFilterMode::None;
	float cutoff = 1.0f;

	if (lowPass != nullptr && lowPass->isActive) {
		targetMode = AudioFilterMode::LowPass;
		cutoff = 1.0f - lowPass->intensity * 0.95f;
	} else if (highPass != nullptr && highPass->isActive) {
		targetMode = AudioFilterMode::HighPass;
		cutoff = highPass->intensity * 0.95f + 0.05f;
	}

	if (reverbZone != nullptr && reverbZone->isActive) {
		Vector3 listenerPos = GetListenerPosition();
		float dx = listenerPos.x - gameObject->translate.x;
		float dy = listenerPos.y - gameObject->translate.y;
		float dz = listenerPos.z - gameObject->translate.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		float minDist = reverbZone->colliderRadius;
		float maxDist = reverbZone->colliderSize.x;
		if (maxDist <= minDist) maxDist = minDist + 1.0f;

		if (distance < minDist) {
		} else if (distance < maxDist) {
			float t = (distance - minDist) / (maxDist - minDist);
			audio.voice->SetVolume(audio.volume * (1.0f - t * 0.5f));
			targetMode = AudioFilterMode::LowPass;
			cutoff = 1.0f - t * 0.8f;
		} else {
			audio.voice->SetVolume(audio.volume * 0.5f);
			targetMode = AudioFilterMode::LowPass;
			cutoff = 0.2f;
		}
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
	}

	// Apply reverb: route voice through reverb submix if reverb filter is active
	if (reverb != nullptr && reverb->isActive && reverbSubmix_ != nullptr) {
		XAUDIO2_SEND_DESCRIPTOR sendDesc{};
		sendDesc.Flags = 0;
		sendDesc.pOutputVoice = reverbSubmix_;
		XAUDIO2_VOICE_SENDS sendList{};
		sendList.SendCount = 1;
		sendList.pSends = &sendDesc;
		audio.voice->SetOutputVoices(&sendList);

		float wetDry = reverb->intensity;
		if (wetDry < 0.01f) wetDry = 0.01f;
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
		reverbParams.WetDryMix = wetDry;
		reverbSubmix_->SetEffectParameters(0, &reverbParams, sizeof(reverbParams));
	} else {
		// No reverb: route directly to master
		audio.voice->SetOutputVoices(nullptr);
	}
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

AudioClip* EditorAudioManager::LoadClip(const std::string& path) {
	auto it = clips_.find(path);
	if (it != clips_.end()) return &it->second;

	AudioClip clip{};
	std::ifstream file(path, std::ios_base::binary);
	if (!file.is_open()) return nullptr;

	WaveRiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
	if (std::strncmp(riff.chunk.id, "RIFF", 4) != 0 ||
		std::strncmp(riff.type, "WAVE", 4) != 0) return nullptr;

	WaveFormatChunk format{};
	file.read(reinterpret_cast<char*>(&format), sizeof(WaveChunkHeader));
	if (std::strncmp(format.chunk.id, "fmt ", 4) != 0) return nullptr;
	file.read(reinterpret_cast<char*>(&format.format), format.chunk.size);

	WaveChunkHeader data{};
	file.read(reinterpret_cast<char*>(&data), sizeof(data));
	while (std::strncmp(data.id, "data", 4) != 0) {
		file.seekg(data.size, std::ios_base::cur);
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
	}

	clip.channelCount = format.format.nChannels;
	clip.sampleRate = format.format.nSamplesPerSec;
	clip.bitsPerSample = format.format.wBitsPerSample;
	clip.bufferSize = static_cast<uint32_t>(data.size);
	clip.pBuffer = new unsigned char[static_cast<size_t>(data.size)];
	file.read(reinterpret_cast<char*>(clip.pBuffer), static_cast<std::streamsize>(data.size));

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
