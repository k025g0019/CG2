#pragma once

#include <chrono>

#include "3d/ParticleEffect.h"
#include "Application/Fireworks/FireworkAudio.h"
#include "Application/Fireworks/FireworkPlayer.h"
#ifdef USE_IMGUI
#include "Application/Fireworks/FireworkEditor.h"
#endif

class GameScene {
public:
	bool Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap,
	                UINT descriptorSize, UINT particleSrvIndex, IXAudio2* audioEngine);
	void Update(const Matrix4x4& cameraMatrix, const Matrix4x4& viewProjection);
	void Draw(ID3D12GraphicsCommandList* commandList, const ParticleEffect::DrawContext& context) const;
	void Finalize();
	void TogglePlayback();
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
};
