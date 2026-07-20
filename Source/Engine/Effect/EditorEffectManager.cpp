#include "EditorEffectManager.h"

#include "EditorComponentUtility.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace {
	constexpr float kPi = 3.14159265f;
	constexpr float kMinimumLength = 0.00001f;

	float GetEffectVectorLength(const Vector3& value) {
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	Vector3 NormalizeEffectVector(const Vector3& value) {
		const float length = GetEffectVectorLength(value);

		if (length <= kMinimumLength) {
			return {0.0f, 1.0f, 0.0f};
		}

		return {value.x / length, value.y / length, value.z / length};
	}

	Vector3 LerpVector3(const Vector3& startValue, const Vector3& endValue, float rate) {
		return {
			startValue.x + (endValue.x - startValue.x) * rate,
			startValue.y + (endValue.y - startValue.y) * rate,
			startValue.z + (endValue.z - startValue.z) * rate};
	}

	bool IsModelAssetPath(const std::string& assetPath) {
		const size_t extensionOffset = assetPath.find_last_of('.');

		if (extensionOffset == std::string::npos) {
			return false;
		}

		std::string extension = assetPath.substr(extensionOffset);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".fbx" || extension == ".obj";
	}

	bool IsEffectAssetPath(const std::string& assetPath) {
		const size_t extensionOffset = assetPath.find_last_of('.');

		if (extensionOffset == std::string::npos) {
			return false;
		}

		std::string extension = assetPath.substr(extensionOffset);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".effect";
	}
}

void EditorEffectManager::Initialize(
	EditorScene* editorScene,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	consoleMessages_ = consoleMessages;
}

void EditorEffectManager::Start() {
	emitterRuntimes_.clear();
	effectAssetCache_.clear();
	particles_.clear();
	particleSerial_ = 0u;
	randomEngine_.seed(0x434732u);
	isStarted_ = true;

	for (const EmitterSnapshot& emitter : CollectEmitterSnapshots()) {
		EmitterRuntime& runtime = emitterRuntimes_[emitter.key];
		runtime.isPlaying = emitter.component.animationPlayOnAwake;

		if (runtime.isPlaying && emitter.component.particleLooping && emitter.component.particlePrewarm) {
			PrewarmEmitter(emitter);
		}
	}

	// 生成直後にも寿命位置と Material を反映し、最初の描画だけ原点へ固まる状態を防ぐ。
	UpdateParticles(0.0001f);
}

void EditorEffectManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	// Particle を先に進めることで、このフレームで生成した Particle の寿命を減らさない。
	UpdateParticles(deltaTime);
	const std::vector<EmitterSnapshot> emitters = CollectEmitterSnapshots();
	UpdateEmitters(emitters, deltaTime);
}

void EditorEffectManager::Stop() {
	if (editorScene_ != nullptr) {
		for (const ParticleRuntime& particle : particles_) {
			editorScene_->DeleteGameObject(particle.particleGameObjectId);
		}
	}

	particles_.clear();
	emitterRuntimes_.clear();
	effectAssetCache_.clear();
	isStarted_ = false;
}

void EditorEffectManager::Draw() {
}

bool EditorEffectManager::PlayEffect(int32_t gameObjectId) {
	bool hasEmitter = false;

	for (const EmitterSnapshot& emitter : CollectEmitterSnapshots()) {
		if (emitter.ownerGameObjectId != gameObjectId) {
			continue;
		}

		EmitterRuntime& runtime = emitterRuntimes_[emitter.key];
		runtime = {};
		runtime.isPlaying = true;
		hasEmitter = true;
	}

	return hasEmitter;
}

bool EditorEffectManager::PlayEffectAt(
	int32_t gameObjectId,
	const std::string& effectAssetPath,
	const Vector3& localOffset) {
	if (!isStarted_ || editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(gameObjectId);
	if (ownerGameObject == nullptr) {
		return false;
	}

	EmitterSnapshot eventEmitter{};
	eventEmitter.key = MakeEmitterKey(gameObjectId, EditorComponentType::VisualEffect);
	eventEmitter.ownerGameObjectId = gameObjectId;
	eventEmitter.ownerPosition = {
		ownerGameObject->translate.x + localOffset.x,
		ownerGameObject->translate.y + localOffset.y,
		ownerGameObject->translate.z + localOffset.z};
	eventEmitter.component.isActive = true;
	eventEmitter.component.type = EditorComponentType::VisualEffect;
	eventEmitter.component.assetPath = effectAssetPath;
	eventEmitter.component.color = {1.0f, 1.0f, 1.0f};
	eventEmitter.component.particleEndColor = {1.0f, 0.35f, 0.05f};
	eventEmitter.component.particleLifetime = 0.8f;
	eventEmitter.component.particleSpeed = 2.0f;
	eventEmitter.component.particleSize = 0.2f;
	eventEmitter.component.particleEndSize = 0.0f;
	eventEmitter.component.particleBurstCount = 12;
	eventEmitter.component.particleMaxCount = 128;
	eventEmitter.component.particleShape = 1;
	eventEmitter.component.particleShapeRadius = 0.25f;
	eventEmitter.component.particleDirection = {0.0f, 1.0f, 0.0f};
	ApplyEffectAsset(eventEmitter.component);
	SpawnParticles(eventEmitter, eventEmitter.component.particleBurstCount);
	return true;
}

void EditorEffectManager::StopEffect(int32_t gameObjectId) {
	for (auto& emitterPair : emitterRuntimes_) {
		const int32_t ownerGameObjectId = static_cast<int32_t>(emitterPair.first >> 32u);

		if (ownerGameObjectId == gameObjectId) {
			emitterPair.second.isPlaying = false;
		}
	}
}

bool EditorEffectManager::IsEffectPlaying(int32_t gameObjectId) const {
	for (const auto& emitterPair : emitterRuntimes_) {
		const int32_t ownerGameObjectId = static_cast<int32_t>(emitterPair.first >> 32u);

		if (ownerGameObjectId == gameObjectId && emitterPair.second.isPlaying) {
			return true;
		}
	}

	return GetAliveParticleCount(gameObjectId) > 0;
}

int32_t EditorEffectManager::GetAliveParticleCount(int32_t gameObjectId) const {
	return static_cast<int32_t>(std::count_if(
		particles_.begin(),
		particles_.end(),
		[gameObjectId](const ParticleRuntime& particle) {
			return particle.ownerGameObjectId == gameObjectId;
		}));
}

uint64_t EditorEffectManager::MakeEmitterKey(
	int32_t gameObjectId,
	EditorComponentType componentType) {
	const uint64_t ownerValue = static_cast<uint64_t>(static_cast<uint32_t>(gameObjectId));
	const uint64_t componentValue = static_cast<uint64_t>(static_cast<uint32_t>(componentType));
	return (ownerValue << 32u) | componentValue;
}

std::vector<EditorEffectManager::EmitterSnapshot> EditorEffectManager::CollectEmitterSnapshots() {
	std::vector<EmitterSnapshot> emitters;

	if (editorScene_ == nullptr) {
		return emitters;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive || gameObject.name.rfind("__EffectParticle_", 0u) == 0u) {
			continue;
		}

		for (const EditorComponent& component : gameObject.components) {
			const bool isEffectComponent =
				component.type == EditorComponentType::ParticleSystem ||
				component.type == EditorComponentType::VisualEffect;

			if (!isEffectComponent || !component.isActive) {
				continue;
			}

			EmitterSnapshot emitter{};
			emitter.key = MakeEmitterKey(gameObject.id, component.type);
			emitter.ownerGameObjectId = gameObject.id;
			emitter.ownerPosition = gameObject.translate;
			emitter.component = component;
			ApplyEffectAsset(emitter.component);
			emitters.push_back(emitter);
		}
	}

	return emitters;
}

bool EditorEffectManager::ApplyEffectAsset(EditorComponent& component) {
	if (!IsEffectAssetPath(component.assetPath)) {
		return false;
	}

	const std::string effectAssetPath = component.assetPath;
	auto effectAssetIterator = effectAssetCache_.find(effectAssetPath);
	if (effectAssetIterator == effectAssetCache_.end()) {
		EffectAsset effectAsset{};

		if (!effectAsset.LoadFromJson(effectAssetPath)) {
			PushConsoleMessage("Effect: Effect Asset を読み込めません: " + effectAssetPath);
			return false;
		}

		effectAssetIterator = effectAssetCache_.emplace(effectAssetPath, std::move(effectAsset)).first;
	}

	effectAssetIterator->second.ApplyToComponent(component);
	return true;
}

void EditorEffectManager::UpdateEmitters(
	const std::vector<EmitterSnapshot>& emitters,
	float deltaTime) {
	for (const EmitterSnapshot& emitter : emitters) {
		EmitterRuntime& runtime = emitterRuntimes_[emitter.key];

		if (!runtime.isPlaying) {
			continue;
		}

		runtime.elapsedTime += deltaTime;
		const float startDelay = (std::max)(emitter.component.particleStartDelay, 0.0f);

		if (runtime.elapsedTime < startDelay) {
			continue;
		}

		const float duration = (std::max)(emitter.component.particleDuration, 0.01f);
		float activeTime = runtime.elapsedTime - startDelay;

		if (activeTime > duration) {
			if (!emitter.component.particleLooping) {
				runtime.isPlaying = false;
				continue;
			}

			runtime.elapsedTime = startDelay + std::fmod(activeTime, duration);
			runtime.isBurstEmitted = false;
			activeTime = runtime.elapsedTime - startDelay;
		}

		if (!runtime.isBurstEmitted && emitter.component.particleBurstCount > 0) {
			SpawnParticles(emitter, emitter.component.particleBurstCount);
			runtime.isBurstEmitted = true;
		}

		const float spawnRate = (std::max)(emitter.component.particleRate, 0.0f);
		runtime.spawnAccumulator += spawnRate * deltaTime;
		const int32_t spawnCount = static_cast<int32_t>(runtime.spawnAccumulator);

		if (spawnCount > 0) {
			runtime.spawnAccumulator -= static_cast<float>(spawnCount);
			SpawnParticles(emitter, spawnCount);
		}
	}
}

void EditorEffectManager::UpdateParticles(float deltaTime) {
	std::vector<int32_t> expiredGameObjectIds;

	for (ParticleRuntime& particle : particles_) {
		particle.age += deltaTime;

		if (particle.age >= particle.lifetime) {
			expiredGameObjectIds.push_back(particle.particleGameObjectId);
			continue;
		}

		EditorGameObject* particleGameObject = editorScene_->FindGameObject(particle.particleGameObjectId);
		if (particleGameObject == nullptr) {
			particle.age = particle.lifetime;
			continue;
		}

		particle.velocity.y -= particle.gravity * deltaTime;
		const float noiseTime = particle.age * particle.noiseFrequency;
		particle.velocity.x += std::sin(noiseTime + particle.noisePhase.x) * particle.noiseStrength * deltaTime;
		particle.velocity.y += std::sin(noiseTime * 1.17f + particle.noisePhase.y) * particle.noiseStrength * deltaTime;
		particle.velocity.z += std::sin(noiseTime * 0.83f + particle.noisePhase.z) * particle.noiseStrength * deltaTime;
		const float dragRate = (std::clamp)(1.0f - particle.drag * deltaTime, 0.0f, 1.0f);
		particle.velocity.x *= dragRate;
		particle.velocity.y *= dragRate;
		particle.velocity.z *= dragRate;
		const float lifetimeRate = (std::clamp)(particle.age / particle.lifetime, 0.0f, 1.0f);
		const float speedMultiplier = 1.0f + (particle.endSpeedMultiplier - 1.0f) * lifetimeRate;
		particle.localPosition.x += particle.velocity.x * speedMultiplier * deltaTime;
		particle.localPosition.y += particle.velocity.y * speedMultiplier * deltaTime;
		particle.localPosition.z += particle.velocity.z * speedMultiplier * deltaTime;

		Vector3 worldPosition = particle.localPosition;
		if (particle.useLocalSpace) {
			const EditorGameObject* ownerGameObject = editorScene_->FindGameObject(particle.ownerGameObjectId);

			if (ownerGameObject != nullptr) {
				worldPosition.x += ownerGameObject->translate.x;
				worldPosition.y += ownerGameObject->translate.y;
				worldPosition.z += ownerGameObject->translate.z;
			}
		}

		if (particle.useCollision && worldPosition.y < 0.0f) {
			worldPosition.y = 0.0f;
			particle.localPosition.y = particle.useLocalSpace
				? worldPosition.y - (editorScene_->FindGameObject(particle.ownerGameObjectId) != nullptr
					? editorScene_->FindGameObject(particle.ownerGameObjectId)->translate.y
					: 0.0f)
				: 0.0f;
			particle.velocity.y = std::abs(particle.velocity.y) * particle.collisionBounce;
			const float horizontalSpeedRate = 1.0f - particle.collisionFriction;
			particle.velocity.x *= horizontalSpeedRate;
			particle.velocity.z *= horizontalSpeedRate;
		}

		const float size = particle.startSize + (particle.endSize - particle.startSize) * lifetimeRate;
		particleGameObject->translate = worldPosition;
		particleGameObject->rotate.y += particle.rotationSpeed * deltaTime;
		particleGameObject->scale = {size, size, size};

		EditorComponent* rendererComponent = EditorComponentUtility::FindComponent(
			*particleGameObject,
			EditorComponentType::ModelRenderer);

		if (rendererComponent != nullptr) {
			rendererComponent->color = LerpVector3(particle.startColor, particle.endColor, lifetimeRate);
			rendererComponent->alpha = particle.startAlpha + (particle.endAlpha - particle.startAlpha) * lifetimeRate;
			rendererComponent->emissionColor = rendererComponent->color;
			rendererComponent->emissionStrength = particle.emissionStrength;
		}
	}

	for (int32_t gameObjectId : expiredGameObjectIds) {
		editorScene_->DeleteGameObject(gameObjectId);
	}

	particles_.erase(
		std::remove_if(
			particles_.begin(),
			particles_.end(),
			[](const ParticleRuntime& particle) {
				return particle.age >= particle.lifetime;
			}),
		particles_.end());
}

void EditorEffectManager::PrewarmEmitter(const EmitterSnapshot& emitter) {
	const float simulationDuration = (std::min)(
		(std::max)(emitter.component.particleDuration, 0.01f),
		(std::max)(emitter.component.particleLifetime, 0.01f));
	const int32_t continuousCount = static_cast<int32_t>(
		(std::max)(emitter.component.particleRate, 0.0f) * simulationDuration);
	const int32_t prewarmCount = continuousCount + (std::max)(emitter.component.particleBurstCount, 0);
	const size_t firstParticleIndex = particles_.size();
	SpawnParticles(emitter, prewarmCount);

	for (size_t particleIndex = firstParticleIndex; particleIndex < particles_.size(); particleIndex++) {
		ParticleRuntime& particle = particles_[particleIndex];
		if (particle.emitterKey != emitter.key) {
			continue;
		}

		particle.age = RandomRange(0.0f, particle.lifetime * 0.95f);
		const float dragRate = std::exp(-particle.drag * particle.age);
		particle.localPosition.x += particle.velocity.x * particle.age * dragRate;
		particle.localPosition.y +=
			particle.velocity.y * particle.age - 0.5f * particle.gravity * particle.age * particle.age;
		particle.localPosition.z += particle.velocity.z * particle.age * dragRate;
		particle.velocity.x *= dragRate;
		particle.velocity.y = (particle.velocity.y - particle.gravity * particle.age) * dragRate;
		particle.velocity.z *= dragRate;
	}
}

void EditorEffectManager::SpawnParticles(
	const EmitterSnapshot& emitter,
	int32_t spawnCount) {
	const int32_t aliveCount = GetAliveParticleCount(emitter.ownerGameObjectId);
	const int32_t maximumCount = (std::clamp)(emitter.component.particleMaxCount, 1, 4096);
	const int32_t allowedSpawnCount = (std::clamp)(spawnCount, 0, maximumCount - aliveCount);

	for (int32_t particleIndex = 0; particleIndex < allowedSpawnCount; particleIndex++) {
		if (!SpawnParticle(emitter)) {
			PushConsoleMessage("Effect: Particle GameObject の生成に失敗しました。");
			break;
		}
	}
}

bool EditorEffectManager::SpawnParticle(const EmitterSnapshot& emitter) {
	const Vector3 spawnOffset = MakeSpawnOffset(emitter.component);
	const Vector3 spawnDirection = MakeSpawnDirection(emitter.component);
	const float speedRandomScale = 1.0f + RandomRange(
		-emitter.component.particleSpeedRandomness,
		emitter.component.particleSpeedRandomness);
	const float lifetimeRandomScale = 1.0f + RandomRange(
		-emitter.component.particleLifetimeRandomness,
		emitter.component.particleLifetimeRandomness);
	const float sizeRandomScale = 1.0f + RandomRange(
		-emitter.component.particleSizeRandomness,
		emitter.component.particleSizeRandomness);
	const float startSize = (std::max)(emitter.component.particleSize * sizeRandomScale, 0.001f);
	const bool useLocalSpace = emitter.component.particleSimulationSpace == 1;
	const Vector3 initialPosition = useLocalSpace
		? spawnOffset
		: Vector3{
			emitter.ownerPosition.x + spawnOffset.x,
			emitter.ownerPosition.y + spawnOffset.y,
			emitter.ownerPosition.z + spawnOffset.z};
	const std::string modelAssetPath = IsModelAssetPath(emitter.component.assetPath)
		? emitter.component.assetPath
		: "resources/en.fbx";
	const std::string particleName =
		"__EffectParticle_" + std::to_string(emitter.ownerGameObjectId) + "_" + std::to_string(particleSerial_++);
	const int32_t particleGameObjectId = editorScene_->CreateGameObject(particleName);

	if (!editorScene_->AddComponent(particleGameObjectId, EditorComponentType::MeshFilter) ||
		!editorScene_->AddComponent(particleGameObjectId, EditorComponentType::ModelRenderer)) {
		editorScene_->DeleteGameObject(particleGameObjectId);
		return false;
	}

	EditorGameObject* particleGameObject = editorScene_->FindGameObject(particleGameObjectId);
	if (particleGameObject == nullptr) {
		return false;
	}

	particleGameObject->translate = useLocalSpace
		? Vector3{
			emitter.ownerPosition.x + initialPosition.x,
			emitter.ownerPosition.y + initialPosition.y,
			emitter.ownerPosition.z + initialPosition.z}
		: initialPosition;
	particleGameObject->scale = {startSize, startSize, startSize};
	EditorComponent* meshFilter = EditorComponentUtility::FindComponent(
		*particleGameObject,
		EditorComponentType::MeshFilter);
	EditorComponent* modelRenderer = EditorComponentUtility::FindComponent(
		*particleGameObject,
		EditorComponentType::ModelRenderer);

	if (meshFilter != nullptr) {
		meshFilter->assetPath = modelAssetPath;
	}

	if (modelRenderer != nullptr) {
		modelRenderer->assetPath = modelAssetPath;
		modelRenderer->color = emitter.component.color;
		modelRenderer->emissionColor = emitter.component.color;
		modelRenderer->emissionStrength = emitter.component.particleEmissionStrength;
		modelRenderer->alpha = emitter.component.particleStartAlpha;
		modelRenderer->roughness = 0.65f;
		modelRenderer->alphaMode = 2;
		modelRenderer->doubleSided = true;
	}

	ParticleRuntime particle{};
	particle.emitterKey = emitter.key;
	particle.ownerGameObjectId = emitter.ownerGameObjectId;
	particle.particleGameObjectId = particleGameObjectId;
	particle.lifetime = (std::max)(emitter.component.particleLifetime * lifetimeRandomScale, 0.01f);
	particle.startSize = startSize;
	particle.endSize = (std::max)(emitter.component.particleEndSize, 0.0f);
	particle.rotationSpeed = emitter.component.particleRotationSpeed * (kPi / 180.0f);
	particle.gravity = emitter.component.particleGravity;
	particle.drag = (std::max)(emitter.component.particleDrag, 0.0f);
	particle.startAlpha = (std::clamp)(emitter.component.particleStartAlpha, 0.0f, 1.0f);
	particle.endAlpha = (std::clamp)(emitter.component.particleEndAlpha, 0.0f, 1.0f);
	particle.emissionStrength = (std::max)(emitter.component.particleEmissionStrength, 0.0f);
	particle.endSpeedMultiplier = (std::max)(emitter.component.particleEndSpeedMultiplier, 0.0f);
	particle.noiseStrength = (std::max)(emitter.component.particleNoiseStrength, 0.0f);
	particle.noiseFrequency = (std::max)(emitter.component.particleNoiseFrequency, 0.0f);
	particle.collisionBounce = (std::clamp)(emitter.component.particleCollisionBounce, 0.0f, 1.0f);
	particle.collisionFriction = (std::clamp)(emitter.component.particleCollisionFriction, 0.0f, 1.0f);
	particle.useCollision = emitter.component.particleCollision;
	particle.useLocalSpace = useLocalSpace;
	particle.localPosition = initialPosition;
	particle.velocity = {
		spawnDirection.x * emitter.component.particleSpeed * speedRandomScale,
		spawnDirection.y * emitter.component.particleSpeed * speedRandomScale,
		spawnDirection.z * emitter.component.particleSpeed * speedRandomScale};
	particle.noisePhase = {
		RandomRange(0.0f, kPi * 2.0f),
		RandomRange(0.0f, kPi * 2.0f),
		RandomRange(0.0f, kPi * 2.0f)};
	particle.startColor = emitter.component.color;
	particle.endColor = emitter.component.particleEndColor;
	particles_.push_back(particle);
	return true;
}

Vector3 EditorEffectManager::MakeSpawnOffset(const EditorComponent& component) {
	if (component.particleShape == 1 || component.particleShape == 2) {
		Vector3 randomDirection{
			RandomRange(-1.0f, 1.0f),
			RandomRange(-1.0f, 1.0f),
			RandomRange(-1.0f, 1.0f)};
		randomDirection = NormalizeEffectVector(randomDirection);
		const float radius = RandomRange(0.0f, (std::max)(component.particleShapeRadius, 0.0f));
		return {
			randomDirection.x * radius,
			randomDirection.y * radius,
			randomDirection.z * radius};
	}

	if (component.particleShape == 3) {
		return {
			RandomRange(-component.particleBoxSize.x * 0.5f, component.particleBoxSize.x * 0.5f),
			RandomRange(-component.particleBoxSize.y * 0.5f, component.particleBoxSize.y * 0.5f),
			RandomRange(-component.particleBoxSize.z * 0.5f, component.particleBoxSize.z * 0.5f)};
	}

	return {0.0f, 0.0f, 0.0f};
}

Vector3 EditorEffectManager::MakeSpawnDirection(const EditorComponent& component) {
	Vector3 direction = NormalizeEffectVector(component.particleDirection);

	if (component.particleShape == 1) {
		direction = NormalizeEffectVector({
			RandomRange(-1.0f, 1.0f),
			RandomRange(-1.0f, 1.0f),
			RandomRange(-1.0f, 1.0f)});
	}
	else if (component.particleShape == 2) {
		const float spread = std::tan((std::clamp)(component.particleShapeAngle, 0.0f, 89.0f) * kPi / 180.0f);
		direction = NormalizeEffectVector({
			direction.x + RandomRange(-spread, spread),
			direction.y,
			direction.z + RandomRange(-spread, spread)});
	}

	return direction;
}

float EditorEffectManager::RandomRange(float minimumValue, float maximumValue) {
	if (maximumValue < minimumValue) {
		std::swap(minimumValue, maximumValue);
	}

	std::uniform_real_distribution<float> distribution(minimumValue, maximumValue);
	return distribution(randomEngine_);
}

void EditorEffectManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}
