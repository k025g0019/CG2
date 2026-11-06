#pragma once

#include <cstddef>
#include <random>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "math/Matrix.h"
#include "math/Vector.h"

// 既存の Particle.VS.hlsl の StructuredBuffer と同じ並びにする。
struct ParticleInstanceData {
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

class ParticleEffect {
public:
	struct Settings {
		std::size_t maxParticles = 500000;
	};

	// 1粒の初期状態。打ち上げ・開花などの軌道は呼び出し側が値を決める。
	struct ParticleParameters {
		Vector3 position{};
		Vector3 velocity{};
		Vector3 acceleration{};
		Vector3 scale = {1.0f, 1.0f, 1.0f};
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
		float lifeTime = 1.0f;
	};

	// 同時に発生する粒子へ、各成分のランダム幅を指定する。
	struct EmitParameters {
		std::size_t count = 1;
		ParticleParameters particle{};
		Vector3 positionJitter{};
		Vector3 velocityJitter{};
		Vector3 scaleJitter{};
		Vector4 colorJitter{};
	};

	struct DrawContext {
		ID3D12Resource* material = nullptr;
		ID3D12Resource* light = nullptr;
		D3D12_GPU_DESCRIPTOR_HANDLE texture{};
		D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
		UINT vertexCount = 0;
	};

	ParticleEffect();
	~ParticleEffect();
	ParticleEffect(const ParticleEffect&) = delete;
	ParticleEffect& operator=(const ParticleEffect&) = delete;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvHeap,
		UINT descriptorSize,
		UINT srvIndex,
		const Settings& settings);
	bool Spawn(const ParticleParameters& parameters);
	std::size_t Emit(const EmitParameters& parameters);
	void Update(float deltaTime, const Matrix4x4& cameraMatrix,
	            const Matrix4x4& viewProjectionMatrix, bool useBillboard);
	void Draw(ID3D12GraphicsCommandList* commandList, const DrawContext& context) const;
	std::size_t GetParticleCount() const;

private:
	struct Particle {
		Vector3 scale;
		Vector3 position;
		Vector3 velocity;
		Vector3 acceleration;
		Vector4 color;
		float startAlpha = 1.0f;
		float lifeTime = 1.0f;
		float currentTime = 0.0f;
	};

	Settings settings_{};
	std::vector<Particle> particles_;
	std::mt19937 randomEngine_;
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	ParticleInstanceData* instanceData_ = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvHandle_{};
};
