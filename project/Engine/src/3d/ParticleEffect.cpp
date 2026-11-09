#include "3d/ParticleEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include "math/Vector&Matrix.h"

ParticleEffect::ParticleEffect() : randomEngine_(std::random_device{}()) {
}

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
	return Spawn(parameters, 0, 0.0f);
}

bool ParticleEffect::Spawn(const ParticleParameters& parameters, int elapsedSteps, float stepSeconds) {
	const double age = static_cast<double>(stepSeconds) * elapsedSteps;
	if (instanceData_ == nullptr || particles_.size() >= settings_.maxParticles ||
		elapsedSteps < 0 || !(age >= 0.0 && age < parameters.lifeTime)) {
		return false;
	}

	Particle particle{};
	particle.position = parameters.position;
	particle.velocity = parameters.velocity;
	particle.acceleration = parameters.acceleration;
	particle.scale = parameters.scale;
	particle.color = parameters.color;
	particle.lifeTime = parameters.lifeTime;
	particle.currentTime = parameters.horizontalDrag > 0.0f ? 0.0 : age;
	particle.trailSeconds = parameters.trailSeconds;
	particle.fadeStartSeconds = parameters.fadeStartSeconds;
	particle.horizontalDrag = parameters.horizontalDrag;
	particle.dragStartSeconds = parameters.dragStartSeconds;
	particle.lineDirection = parameters.lineDirection;
	particle.lineLength = parameters.lineLength;

	if (elapsedSteps > 0) {
		if (particle.horizontalDrag > 0.0f) {
			// 冠菊の減速は毎フレーム非線形に変わる。再生時と同じ順に進めてシーク位置を揃える。
			for (int step = 0; step < elapsedSteps; ++step) {
				particle.currentTime += stepSeconds;
				AdvanceOneParticle(particle, stepSeconds);
			}
		} else {
			// 通常の等加速度運動はフレーム数から直接復元する。
			const float elapsed = static_cast<float>(age);
			particle.position += parameters.velocity * elapsed
				+ parameters.acceleration * (0.5f * elapsed * (elapsed + stepSeconds));
			particle.velocity += parameters.acceleration * elapsed;
		}
	}

	particles_.push_back(particle);
	return true;
}

std::size_t ParticleEffect::Emit(const EmitParameters& parameters) {
	return Emit(parameters, randomEngine_, 0, 0.0f);
}

std::size_t ParticleEffect::Emit(const EmitParameters& parameters, std::mt19937& randomEngine,
                                int elapsedSteps, float stepSeconds) {
	std::uniform_real_distribution<float> signedRandom(-1.0f, 1.0f);
	auto vary = [&randomEngine, &signedRandom](float value, float jitter) {
		if (jitter == 0.0f) {
			return value;
		}
		return value + signedRandom(randomEngine) * jitter;
	};

	std::size_t emitted = 0;
	while (emitted < parameters.count && particles_.size() < settings_.maxParticles) {
		ParticleParameters particle = parameters.particle;
		particle.position = {
			vary(particle.position.x, parameters.positionJitter.x),
			vary(particle.position.y, parameters.positionJitter.y),
			vary(particle.position.z, parameters.positionJitter.z)
		};
		particle.velocity = {
			vary(particle.velocity.x, parameters.velocityJitter.x),
			vary(particle.velocity.y, parameters.velocityJitter.y),
			vary(particle.velocity.z, parameters.velocityJitter.z)
		};
		particle.scale = {
			vary(particle.scale.x, parameters.scaleJitter.x),
			vary(particle.scale.y, parameters.scaleJitter.y),
			vary(particle.scale.z, parameters.scaleJitter.z)
		};
		particle.color = {
			vary(particle.color.x, parameters.colorJitter.x),
			vary(particle.color.y, parameters.colorJitter.y),
			vary(particle.color.z, parameters.colorJitter.z),
			vary(particle.color.w, parameters.colorJitter.w)
		};
		if (!Spawn(particle, elapsedSteps, stepSeconds)) {
			break;
		}
		++emitted;
	}
	return emitted;
}

void ParticleEffect::Clear() {
	particles_.clear();
	preparedCount_ = 0;
}

void ParticleEffect::AdvanceOneParticle(Particle& particle, float deltaTime) {
	if (particle.horizontalDrag > 0.0f
		&& particle.currentTime >= particle.dragStartSeconds) {
		// 球状に開いた後、横の勢いだけを失わせて重力で垂らす。
		const float damping = std::exp(-particle.horizontalDrag * deltaTime);
		particle.velocity.x *= damping;
		particle.velocity.z *= damping;
	}
	particle.velocity += particle.acceleration * deltaTime;
	particle.position += particle.velocity * deltaTime;
}

void ParticleEffect::Advance(float deltaTime) {
	if (!(deltaTime > 0.0f)) {
		return;
	}

	// 消えた粒子を先に詰め、以降の配列順を GPU のインスタンス番号と一致させる。
	std::erase_if(particles_, [deltaTime](Particle& particle) {
		particle.currentTime += deltaTime;
		return particle.currentTime >= particle.lifeTime;
	});
	for (Particle& particle : particles_) {
		AdvanceOneParticle(particle, deltaTime);
	}
}

void ParticleEffect::PrepareDraw(const Matrix4x4& cameraMatrix,
                                 const Matrix4x4& viewProjectionMatrix, bool useBillboard) {
	preparedCount_ = 0;
	if (instanceData_ == nullptr || particles_.empty()) {
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

	// カメラから見た板ポリの横・上方向は全粒子で共通。
	const Vector3 right{
		billboardMatrix.matrix[0][0],
		billboardMatrix.matrix[0][1],
		billboardMatrix.matrix[0][2]
	};
	const Vector3 up{
		billboardMatrix.matrix[1][0],
		billboardMatrix.matrix[1][1],
		billboardMatrix.matrix[1][2]
	};

	for (std::size_t index = 0; index < particles_.size(); ++index) {
		const Particle& particle = particles_[index];
		const float fadeStart = std::clamp(particle.fadeStartSeconds, 0.0f, particle.lifeTime);
		const float fadeDuration = particle.lifeTime - fadeStart;
		const float fade = fadeDuration > 0.0f
			? std::clamp(static_cast<float>((particle.lifeTime - particle.currentTime) / fadeDuration),
				0.0f, 1.0f)
			: 1.0f;

		// 固定の光の筋は移動と描画方向を分ける。通常の火花は速度に沿って描く。
		const bool fixedLine = particle.lineLength > 0.0f;
		const Vector3 direction = fixedLine ? particle.lineDirection : particle.velocity;
		float vx = Dot(direction, right);
		float vy = Dot(direction, up);
		float speedOnScreen = std::sqrt(vx * vx + vy * vy);
		float angle = speedOnScreen > 0.001f ? std::atan2(-vx, vy) : 0.0f;

		float width = particle.scale.x * fade;
		float trailTime = particle.trailSeconds * fade;
		// 固定線もカメラの奥行き方向へ傾いた分だけ短く見せる。
		const float worldDirectionLength = fixedLine ? Length(direction) : 0.0f;
		float length = fixedLine && worldDirectionLength > 0.0f
			? particle.lineLength * speedOnScreen / worldDirectionLength
			: speedOnScreen * trailTime;
		if (length < width) {
			length = width;
		}

		// 固定の筋は指定した中点に置き、通常の火花は移動方向の後ろへずらす。
		Vector3 center = fixedLine ? particle.position
			: particle.position - particle.velocity * (trailTime * 0.5f);

		// plane.obj は縦横 -1～1 の板なので、全幅・全長の半分を指定
		Matrix4x4 worldMatrix = Multiply(
			Multiply(
				Multiply(
					MakeScaleMatrix({width * 0.5f, length * 0.5f, 1.0f}),
					MakeRotateZMatrix(angle)),
				billboardMatrix),
			MakeTranslationMatrix(center));
		instanceData_[index].WVP = Multiply(worldMatrix, viewProjectionMatrix);
		instanceData_[index].World = worldMatrix;
		instanceData_[index].color = {
			particle.color.x, particle.color.y, particle.color.z, particle.color.w * fade
		};
	}
	preparedCount_ = particles_.size();
}

void ParticleEffect::Update(float deltaTime, const Matrix4x4& cameraMatrix,
                            const Matrix4x4& viewProjectionMatrix, bool useBillboard) {
	Advance(deltaTime);
	PrepareDraw(cameraMatrix, viewProjectionMatrix, useBillboard);
}

void ParticleEffect::Draw(
	ID3D12GraphicsCommandList* commandList, const DrawContext& context) const {
	if (commandList == nullptr || context.material == nullptr ||
		context.light == nullptr || preparedCount_ == 0) {
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
	                           static_cast<UINT>(preparedCount_), 0, 0);
}

std::size_t ParticleEffect::GetParticleCount() const {
	return particles_.size();
}
