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
		std::size_t maxParticles = 5000000;
	};

	// 1粒の初期状態。打ち上げ・開花などの軌道は呼び出し側が値を決める。
	struct ParticleParameters {
		Vector3 position{};
		Vector3 velocity{};
		Vector3 acceleration{};
		Vector3 scale = {1.0f, 1.0f, 1.0f};
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
		float lifeTime = 1.0f;
		float trailSeconds = 0.02f;
		// ここまでは明るさと太さを保ち、その後で寿命まで徐々に消す。0 なら従来の減衰。
		float fadeStartSeconds = 0.0f;
		// 指定時刻から水平方向だけ減速する。0 なら通常の放物運動。
		float horizontalDrag = 0.0f;
		float dragStartSeconds = 0.0f;
		// 0 より大きいとき、移動速度とは独立した固定方向・長さの光の筋を描く。
		Vector3 lineDirection{};
		float lineLength = 0.0f;
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
	// 一定間隔で更新した粒子を、指定フレームまで進んだ状態で生成する。
	bool Spawn(const ParticleParameters& parameters, int elapsedSteps, float stepSeconds);
	std::size_t Emit(const EmitParameters& parameters);
	std::size_t Emit(const EmitParameters& parameters, std::mt19937& randomEngine,
	                 int elapsedSteps, float stepSeconds);
	void Clear();
	void Advance(float deltaTime);
	void PrepareDraw(const Matrix4x4& cameraMatrix,
	                 const Matrix4x4& viewProjectionMatrix, bool useBillboard);
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
		float lifeTime = 1.0f;
		double currentTime = 0.0;
		float trailSeconds = 0.02f;
		float fadeStartSeconds = 0.0f;
		float horizontalDrag = 0.0f;
		float dragStartSeconds = 0.0f;
		Vector3 lineDirection{};
		float lineLength = 0.0f;
	};
	static void AdvanceOneParticle(Particle& particle, float deltaTime);

	Settings settings_{};
	std::vector<Particle> particles_;
	std::mt19937 randomEngine_;
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	ParticleInstanceData* instanceData_ = nullptr;
	std::size_t preparedCount_ = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvHandle_{};
};
