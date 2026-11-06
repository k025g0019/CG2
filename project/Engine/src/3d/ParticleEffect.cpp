#include "3d/ParticleEffect.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numbers>

#include "math/Vector&Matrix.h"

ParticleEffect::ParticleEffect() : randomEngine_(std::random_device{}()) {}

ParticleEffect::~ParticleEffect() {
	if (instanceResource_ != nullptr && instanceData_ != nullptr) {
		instanceResource_->Unmap(0, nullptr);
	}
}

bool ParticleEffect::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvHeap,
	UINT descriptorSize,
	UINT srvIndex,
	const Settings& settings) {
	if (device == nullptr || srvHeap == nullptr || settings.maxParticles == 0 ||
		settings.maxParticles > (std::numeric_limits<UINT>::max)()) {
		return false;
	}

	settings_ = settings;
	particles_.reserve(settings_.maxParticles);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(ParticleInstanceData) * settings_.maxParticles;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(instanceResource_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	hr = instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(settings_.maxParticles);
	srvDesc.Buffer.StructureByteStride = sizeof(ParticleInstanceData);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += static_cast<SIZE_T>(descriptorSize) * srvIndex;
	instanceSrvHandle_ = srvHeap->GetGPUDescriptorHandleForHeapStart();
	instanceSrvHandle_.ptr += static_cast<UINT64>(descriptorSize) * srvIndex;
	device->CreateShaderResourceView(instanceResource_.Get(), &srvDesc, cpuHandle);
	return true;
}

bool ParticleEffect::Spawn(const ParticleParameters& parameters) {
	if (instanceData_ == nullptr || particles_.size() >= settings_.maxParticles ||
		parameters.lifeTime <= 0.0f) {
		return false;
	}

	Particle particle{};
	particle.position = parameters.position;
	particle.velocity = parameters.velocity;
	particle.acceleration = parameters.acceleration;
	particle.scale = parameters.scale;
	particle.color = parameters.color;
	particle.startAlpha = parameters.color.w;
	particle.lifeTime = parameters.lifeTime;
	particles_.push_back(particle);
	return true;
}

std::size_t ParticleEffect::Emit(const EmitParameters& parameters) {
	std::uniform_real_distribution<float> signedRandom(-1.0f, 1.0f);
	auto vary = [this, &signedRandom](float value, float jitter) {
		if (jitter == 0.0f) {
			return value;
		}
		return value + signedRandom(randomEngine_) * jitter;
	};

	std::size_t emitted = 0;
	while (emitted < parameters.count && particles_.size() < settings_.maxParticles) {
		ParticleParameters particle = parameters.particle;
		particle.position = {
			vary(particle.position.x, parameters.positionJitter.x),
			vary(particle.position.y, parameters.positionJitter.y),
			vary(particle.position.z, parameters.positionJitter.z)};
		particle.velocity = {
			vary(particle.velocity.x, parameters.velocityJitter.x),
			vary(particle.velocity.y, parameters.velocityJitter.y),
			vary(particle.velocity.z, parameters.velocityJitter.z)};
		particle.scale = {
			vary(particle.scale.x, parameters.scaleJitter.x),
			vary(particle.scale.y, parameters.scaleJitter.y),
			vary(particle.scale.z, parameters.scaleJitter.z)};
		particle.color = {
			vary(particle.color.x, parameters.colorJitter.x),
			vary(particle.color.y, parameters.colorJitter.y),
			vary(particle.color.z, parameters.colorJitter.z),
			vary(particle.color.w, parameters.colorJitter.w)};
		if (!Spawn(particle)) {
			break;
		}
		++emitted;
	}
	return emitted;
}

void ParticleEffect::Update(float deltaTime, const Matrix4x4& cameraMatrix,
	const Matrix4x4& viewProjectionMatrix, bool useBillboard) {
	if (instanceData_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	// ビルボードにはカメラの回転だけを使い、カメラの移動は粒子へ加えない。
	Matrix4x4 billboardMatrix = MakeIdentity4x4();
	if (useBillboard) {
		billboardMatrix = Multiply(
			MakeRotateYMatrix(std::numbers::pi_v<float>), cameraMatrix);
		billboardMatrix.matrix[3][0] = 0.0f;
		billboardMatrix.matrix[3][1] = 0.0f;
		billboardMatrix.matrix[3][2] = 0.0f;
	}

	// 消えた粒子を先に詰め、以降の配列順を GPU のインスタンス番号と一致させる。
	std::erase_if(particles_, [deltaTime](Particle& particle) {
		particle.currentTime += deltaTime;
		return particle.currentTime >= particle.lifeTime;
	});

	for (std::size_t index = 0; index < particles_.size(); ++index) {
		Particle& particle = particles_[index];

		particle.velocity += particle.acceleration * deltaTime;
		particle.position += particle.velocity * deltaTime;
		particle.color.w = particle.startAlpha *
			(1.0f - particle.currentTime / particle.lifeTime);

		Matrix4x4 scaleMatrix = MakeAffineMatrix(
			particle.scale, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
		Matrix4x4 translateMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, particle.position);
		Matrix4x4 worldMatrix = Multiply(
			Multiply(scaleMatrix, billboardMatrix), translateMatrix);
		instanceData_[index].WVP = Multiply(worldMatrix, viewProjectionMatrix);
		instanceData_[index].World = worldMatrix;
		instanceData_[index].color = particle.color;
	}
}

void ParticleEffect::Draw(
	ID3D12GraphicsCommandList* commandList, const DrawContext& context) const {
	if (commandList == nullptr || context.material == nullptr ||
		context.light == nullptr || particles_.empty()) {
		return;
	}

	// ルートシグネチャ、PSO、プリミティブ種別は呼び出し元が設定する。
	commandList->SetGraphicsRootConstantBufferView(
		0, context.material->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(1, instanceSrvHandle_);
	commandList->SetGraphicsRootConstantBufferView(
		2, context.light->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(3, context.texture);
	commandList->IASetVertexBuffers(0, 1, &context.vertexBuffer);
	commandList->DrawInstanced(context.vertexCount,
		static_cast<UINT>(particles_.size()), 0, 0);
}

std::size_t ParticleEffect::GetParticleCount() const {
	return particles_.size();
}
