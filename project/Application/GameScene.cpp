#include "Application/GameScene.h"
#include "Application/Fireworks/FireworkPresets.h"
#include "2d/SpriteCommon.h"
#include "base/TextureManager.h"
#include "motion/StringUtility.h"

#include <cstring>
#include <stdexcept>
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

void GameScene::InitializeSprites(SpriteCommon* spriteCommon, TextureManager* textureManager) {
	if (spriteCommon == nullptr || textureManager == nullptr || spriteCommon_ != nullptr) {
		throw std::logic_error("GameScene::InitializeSprites: invalid initialization");
	}
	spriteCommon_ = spriteCommon;
	// 同じパスで2枚作っても、TextureManagerは画像の読込・GPU転送を繰り返さない。
	sprites_[0].Initialize(spriteCommon, textureManager, "resources/uvChecker.png");
	sprites_[0].SetPosition({32.0f, 32.0f});
	sprites_[0].SetSize({256.0f, 256.0f});

	sprites_[1].Initialize(spriteCommon, textureManager, "resources/uvChecker.png");
	sprites_[1].SetTextureRect({256.0f, 0.0f}, {256.0f, 256.0f});
	sprites_[1].SetSize({192.0f, 192.0f});
	sprites_[1].SetAnchorPoint({0.5f, 0.5f});
	sprites_[1].SetPosition({420.0f, 160.0f});
	sprites_[1].SetRotation(0.25f);
	sprites_[1].SetIsFlipX(true);
	sprites_[1].SetIsFlipY(true);
	sprites_[1].SetColor({0.75f, 1.0f, 1.0f, 0.8f});

	sprites_[2].Initialize(spriteCommon, textureManager, "resources/monsterBall.png");
	sprites_[2].SetPosition({568.0f, 32.0f});
	sprites_[2].SetSize({240.0f, 120.0f});
	for (Sprite& sprite : sprites_) {
		sprite.Update();
	}
}

void GameScene::Update(const Matrix4x4& cameraMatrix, const Matrix4x4& viewProjection) {
	if (spritesVisible_ && spriteCommon_ != nullptr) {
		for (Sprite& sprite : sprites_) {
			sprite.Update();
		}
	}
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
	if (spritesVisible_ && spriteCommon_ != nullptr) {
		// 花火の3Dパイプラインから、2D専用のルートシグネチャとPSOへ切り替える。
		spriteCommon_->CommonSetting();
		for (const Sprite& sprite : sprites_) {
			sprite.Draw();
		}
	}
}

void GameScene::Finalize() {
	audio_.StopAll();
	for (Sprite& sprite : sprites_) {
		sprite.Finalize();
	}
	spriteCommon_ = nullptr;
	spritesVisible_ = false;
}

void GameScene::TogglePlayback() {
	player_.TogglePlayback(show_);
}

void GameScene::ToggleSprites() {
	spritesVisible_ = !spritesVisible_;
}

#ifdef USE_IMGUI
void GameScene::ToggleEditor() {
	editor_.ToggleVisible();
}
#endif
