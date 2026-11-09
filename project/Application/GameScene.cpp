#include "Application/GameScene.h"
#include "Application/Fireworks/FireworkPresets.h"
#include "motion/StringUtility.h"

#include <cstring>
#include <utility>

bool GameScene::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap,
                           UINT descriptorSize, UINT particleSrvIndex, IXAudio2* audioEngine) {
	ParticleEffect::Settings settings{};
	settings.maxParticles = fireworks::kMaxLiveParticles;
	if (!particles_.Initialize(device, srvHeap, descriptorSize, particleSrvIndex, settings)) {
		return false;
	}
	audio_.Initialize(audioEngine);

	std::string message;
	std::string error;
	std::error_code fileError;
	const bool hasSavedShow = std::filesystem::exists(fireworks::kSavedShowPath, fileError);
	if (hasSavedShow && fireworks::LoadShow(fireworks::kSavedShowPath, show_, error)) {
		message = "保存した演出を読み込みました";
	} else {
		if (fileError) {
			message = "保存先にアクセスできません。 ";
		} else if (hasSavedShow) {
			message = "保存した演出を読み込めませんでした。 " + error + " ";
		}
		if (fireworks::LoadShow(fireworks::kExampleShowPath, show_, error)) {
			message += "見本を読み込みました。";
		} else {
			message += "見本を読み込めませんでした。吹き上げを表示します。";
			show_.cues.push_back(fireworks::MakeCue(fireworks::GetPresets().front(), 0, 1));
		}
	}
	OutputDebugStringW(StringUtility::ConvertString(message + "\n").c_str());
#ifdef USE_IMGUI
	editor_.SetStatus(std::move(message));
#endif
	return true;
}

void GameScene::Update(const Matrix4x4& cameraMatrix, const Matrix4x4& viewProjection) {
	const auto now = Clock::now();
	const double deltaSeconds = previousTime_ == Clock::time_point{}
		? 0.0 : std::chrono::duration<double>(now - previousTime_).count();
	previousTime_ = now;

#ifdef USE_IMGUI
	editor_.Draw(viewProjection, useBillboard_, particles_.GetParticleCount());
#endif
	const bool changed = player_.Update(show_, deltaSeconds);
	if (player_.TimelineJumped()) {
		audio_.StopAll();
	}
	audio_.SetPaused(!player_.IsPlaying());
	for (const fireworks::FireworkPlayer::SoundCue& soundCue : player_.SoundCues()) {
		const fireworks::FireworkCue& cue = show_.cues[soundCue.cueIndex];
		audio_.Play(soundCue.burst ? cue.burstSound : cue.launchSound);
	}
	audio_.UpdateFinished();
#ifdef USE_IMGUI
	const std::string audioError = audio_.TakeError();
	if (!audioError.empty()) {
		editor_.SetStatus(audioError);
	}
#else
	audio_.TakeError();
#endif
	const bool viewChanged = std::memcmp(&previousViewProjection_, &viewProjection, sizeof(viewProjection)) != 0;
	if (changed || viewChanged || useBillboard_ != previousBillboard_) {
		particles_.PrepareDraw(cameraMatrix, viewProjection, useBillboard_);
		previousViewProjection_ = viewProjection;
		previousBillboard_ = useBillboard_;
	}
}

void GameScene::Draw(ID3D12GraphicsCommandList* commandList,
                     const ParticleEffect::DrawContext& context) const {
	particles_.Draw(commandList, context);
}

void GameScene::Finalize() {
	audio_.StopAll();
}

void GameScene::TogglePlayback() {
	player_.TogglePlayback(show_);
}

#ifdef USE_IMGUI
void GameScene::ToggleEditor() {
	editor_.ToggleVisible();
}
#endif
