#pragma once

#include <array>
#include <chrono>

#include "2d/Sprite.h"
#include "3d/ParticleEffect.h"
#include "Application/Fireworks/FireworkAudio.h"
#include "Application/Fireworks/FireworkPlayer.h"
#ifdef USE_IMGUI
#include "Application/Fireworks/FireworkEditor.h"
#endif

class SpriteCommon;
class TextureManager;

class GameScene {
public:
	bool Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap,
	                UINT descriptorSize, UINT particleSrvIndex, IXAudio2* audioEngine);
	void InitializeSprites(SpriteCommon* spriteCommon, TextureManager* textureManager);
	void Update(const Matrix4x4& cameraMatrix, const Matrix4x4& viewProjection);
	void Draw(ID3D12GraphicsCommandList* commandList, const ParticleEffect::DrawContext& context) const;
	void Finalize();
	void TogglePlayback();
	void ToggleSprites();
#ifdef USE_IMGUI
	void ToggleEditor();
#endif

private:
	using Clock = std::chrono::steady_clock;

	fireworks::FireworkShow show_;
	ParticleEffect particles_;
	fireworks::FireworkPlayer player_{particles_};
	fireworks::FireworkAudio audio_;
#ifdef USE_IMGUI
	fireworks::FireworkEditor editor_{show_, player_};
#endif
	Clock::time_point previousTime_{};
	Matrix4x4 previousViewProjection_{};
	bool useBillboard_ = true;
	bool previousBillboard_ = true;
	// F2で表示する使用例。全画像・同じ画像の切り抜き・別画像を個別に設定する。
	SpriteCommon* spriteCommon_ = nullptr;
	std::array<Sprite, 3> sprites_;
	bool spritesVisible_ = false;
};
