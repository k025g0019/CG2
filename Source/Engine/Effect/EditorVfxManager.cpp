#include "EditorVfxManager.h"

#include "Source/Engine/Core/EditorSharedState.h"
#include "Source/Engine/Editor/EditorScene.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
	constexpr float kPi = 3.14159265f;
	constexpr float kMinimumLength = 0.00001f;

	Vector3 SafeNormalize(const Vector3& value) {
		const float length = Length(value);
		if (length <= kMinimumLength) {
			return {0.0f, 1.0f, 0.0f};
		}
		return Normalize(value);
	}

	Vector3 LerpVector3(const Vector3& startValue, const Vector3& endValue, float rate) {
		return {
			startValue.x + (endValue.x - startValue.x) * rate,
			startValue.y + (endValue.y - startValue.y) * rate,
			startValue.z + (endValue.z - startValue.z) * rate};
	}

	float LerpFloat(float startValue, float endValue, float rate) {
		return startValue + (endValue - startValue) * rate;
	}

	// Flipbook: 経過秒からFrame番号を求め、Column/Rowから左上UVと右下UVを返す。
	void ComputeFlipbookUv(
		const EffectNodeDefinition& definition,
		float age,
		float& outU0,
		float& outV0,
		float& outU1,
		float& outV1) {
		const int32_t frameCount = (std::max)(definition.flipbookEndFrame - definition.flipbookStartFrame + 1, 1);
		const float frameDuration = 1.0f / (std::max)(definition.flipbookFps, 0.01f);
		int32_t frameOffset = static_cast<int32_t>(age / frameDuration);

		if (definition.flipbookLoop) {
			frameOffset = frameOffset % frameCount;
		}
		else {
			frameOffset = (std::min)(frameOffset, frameCount - 1);
		}

		const int32_t frame = definition.flipbookStartFrame + frameOffset;
		const int32_t column = frame % (std::max)(definition.flipbookColumns, 1);
		const int32_t row = frame / (std::max)(definition.flipbookColumns, 1);
		const float cellWidth = 1.0f / static_cast<float>((std::max)(definition.flipbookColumns, 1));
		const float cellHeight = 1.0f / static_cast<float>((std::max)(definition.flipbookRows, 1));
		outU0 = static_cast<float>(column) * cellWidth;
		outV0 = static_cast<float>(row) * cellHeight;
		outU1 = outU0 + cellWidth;
		outV1 = outV0 + cellHeight;
	}

	EditorVfxRenderer::VfxBatch* FindOrAddBatch(
		std::vector<EditorVfxRenderer::VfxBatch>& batches,
		const std::string& texturePath,
		EditorVfxRenderer::BlendVariant blendVariant,
		uint32_t firstVertex,
		bool useSoftParticle,
		float softParticleFadeDistance) {
		if (!batches.empty()) {
			EditorVfxRenderer::VfxBatch& lastBatch = batches.back();
			if (lastBatch.texturePath == texturePath &&
				lastBatch.blendVariant == blendVariant &&
				lastBatch.useSoftParticle == useSoftParticle &&
				(!useSoftParticle || lastBatch.softParticleFadeDistance == softParticleFadeDistance) &&
				lastBatch.firstVertex + lastBatch.vertexCount == firstVertex) {
				return &lastBatch;
			}
		}

		EditorVfxRenderer::VfxBatch newBatch{};
		newBatch.texturePath = texturePath;
		newBatch.blendVariant = blendVariant;
		newBatch.firstVertex = firstVertex;
		newBatch.vertexCount = 0u;
		newBatch.useSoftParticle = useSoftParticle;
		newBatch.softParticleFadeDistance = softParticleFadeDistance;
		batches.push_back(newBatch);
		return &batches.back();
	}
}

void EditorVfxManager::Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	consoleMessages_ = consoleMessages;
}

void EditorVfxManager::Start() {
	instances_.clear();
	instances_.resize(static_cast<size_t>(kMaxEffectInstances));
	freeIndices_.clear();
	freeIndices_.reserve(static_cast<size_t>(kMaxEffectInstances));
	for (int32_t index = kMaxEffectInstances - 1; index >= 0; index--) {
		freeIndices_.push_back(index);
	}
	activeCountByEffectId_.clear();
	pendingGpuParticleSpawns_.clear();
	decalRuntimes_.clear();
	isStarted_ = true;
}

void EditorVfxManager::Stop() {
	instances_.clear();
	freeIndices_.clear();
	activeCountByEffectId_.clear();
	pendingGpuParticleSpawns_.clear();

	if (editorScene_ != nullptr) {
		for (const DecalRuntime& decal : decalRuntimes_) {
			editorScene_->DeleteGameObject(decal.gameObjectId);
		}
	}
	decalRuntimes_.clear();
	isStarted_ = false;
}

const EffectDefinition* EditorVfxManager::ResolveDefinition(const std::string& effectId) {
	const auto cacheIterator = definitionCache_.find(effectId);
	if (cacheIterator != definitionCache_.end()) {
		return &cacheIterator->second;
	}

	std::string filePath = effectId;
	if (filePath.find('/') == std::string::npos && filePath.find('\\') == std::string::npos) {
		filePath = "Assets/Effects/" + effectId + ".effectdef";
	}

	if (!std::filesystem::exists(filePath)) {
		PushConsoleMessage("Vfx: Effect Definition が見つかりません: " + filePath);
		return nullptr;
	}

	EffectDefinition definition{};
	if (!definition.LoadFromJson(filePath)) {
		PushConsoleMessage("Vfx: Effect Definition を読み込めません: " + filePath);
		return nullptr;
	}

	if (definition.id.empty()) {
		definition.id = effectId;
	}

	return &definitionCache_.emplace(effectId, std::move(definition)).first->second;
}

int32_t EditorVfxManager::AcquireInstanceSlot() {
	if (freeIndices_.empty()) {
		return -1;
	}

	const int32_t index = freeIndices_.back();
	freeIndices_.pop_back();
	return index;
}

void EditorVfxManager::ReleaseInstanceSlot(int32_t index) {
	if (index < 0 || index >= static_cast<int32_t>(instances_.size())) {
		return;
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(index)];
	instance.inUse = false;
	instance.generation++;
	instance.nodes.clear();
	instance.nodes.shrink_to_fit();
	freeIndices_.push_back(index);
}

EditorVfxManager::EffectHandle EditorVfxManager::PlayEffect(const std::string& effectId, const Vector3& position) {
	if (!isStarted_) {
		return {};
	}

	const EffectDefinition* definition = ResolveDefinition(effectId);
	if (definition == nullptr) {
		return {};
	}

	int32_t& activeCount = activeCountByEffectId_[effectId];
	if (activeCount >= definition->maxConcurrentInstances) {
		return {};
	}

	const int32_t index = AcquireInstanceSlot();
	if (index < 0) {
		PushConsoleMessage("Vfx: Effect Instance Pool が枯渇しました: " + effectId);
		return {};
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(index)];
	instance.inUse = true;
	instance.effectId = effectId;
	instance.definition = definition;
	instance.followGameObject = false;
	instance.followGameObjectId = -1;
	instance.localOffset = {0.0f, 0.0f, 0.0f};
	instance.fixedPosition = position;
	instance.worldPosition = position;
	instance.age = 0.0f;
	instance.stopped = false;
	instance.lodSpawnMultiplier = 1.0f;
	instance.hitNormal = {0.0f, 1.0f, 0.0f};
	instance.nodes.clear();
	instance.nodes.reserve(definition->nodes.size());

	for (const EffectNodeDefinition& nodeDefinition : definition->nodes) {
		NodeRuntime nodeRuntime{};
		nodeRuntime.definition = &nodeDefinition;
		switch (nodeDefinition.nodeType) {
			case EffectNodeType::Billboard:
				if (nodeDefinition.useGpuSimulation) {
					// Stage2: EditorGpuParticleManagerへ発生要求を積むだけなので、CPU側Particle配列は使わない。
					nodeRuntime.kind = NodeKind::GpuBillboard;
				}
				else {
					nodeRuntime.kind = NodeKind::Billboard;
					nodeRuntime.particles.reserve(static_cast<size_t>((std::max)(nodeDefinition.maxCount, 1)));
				}
				break;
			case EffectNodeType::Ribbon:
				nodeRuntime.kind = NodeKind::Ribbon;
				break;
			case EffectNodeType::Ring:
				nodeRuntime.kind = NodeKind::Ring;
				break;
			case EffectNodeType::MeshParticle:
				// Stage2: 破片・薬莢等。EditorGpuParticleManagerのMesh Instancing描画パスを再利用する。
				nodeRuntime.kind = NodeKind::MeshParticle;
				break;
			case EffectNodeType::Decal:
				// Stage2: 既存DecalProjector Component描画パスを再利用したDecal投影。
				nodeRuntime.kind = NodeKind::Decal;
				break;
			default:
				nodeRuntime.kind = NodeKind::Unsupported;
				PushConsoleMessage("Vfx: " + nodeDefinition.name + " は未対応のNode Typeです。");
				break;
		}
		instance.nodes.push_back(std::move(nodeRuntime));
	}

	activeCount++;
	return {index, instance.generation};
}

EditorVfxManager::EffectHandle EditorVfxManager::PlayEffect(
	const std::string& effectId,
	const Vector3& position,
	const Vector3& hitNormal) {
	const EffectHandle handle = PlayEffect(effectId, position);
	if (!handle.IsValid()) {
		return handle;
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(handle.index)];
	instance.hitNormal = SafeNormalize(hitNormal);

	// Decal Nodeは連続発生ではなく着弾1回分の投影なので、この場でまとめてDecalを生成する。
	for (NodeRuntime& node : instance.nodes) {
		if (node.kind == NodeKind::Decal) {
			SpawnDecal(instance, node);
		}
	}

	return handle;
}

EditorVfxManager::EffectHandle EditorVfxManager::PlayEffect(
	const std::string& effectId,
	int32_t followGameObjectId,
	const Vector3& localOffset) {
	Vector3 initialPosition = localOffset;
	if (editorScene_ != nullptr) {
		Vector3 worldScale{1.0f, 1.0f, 1.0f};
		Vector3 worldRotation{0.0f, 0.0f, 0.0f};
		Vector3 worldPosition{0.0f, 0.0f, 0.0f};
		if (editorScene_->GetWorldTransform(followGameObjectId, worldScale, worldRotation, worldPosition)) {
			initialPosition = Add(worldPosition, localOffset);
		}
	}

	const EffectHandle handle = PlayEffect(effectId, initialPosition);
	if (!handle.IsValid()) {
		return handle;
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(handle.index)];
	instance.followGameObject = true;
	instance.followGameObjectId = followGameObjectId;
	instance.localOffset = localOffset;
	return handle;
}

void EditorVfxManager::StopEffect(EffectHandle handle) {
	if (!handle.IsValid() || handle.index >= static_cast<int32_t>(instances_.size())) {
		return;
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(handle.index)];
	if (!instance.inUse || instance.generation != handle.generation) {
		return;
	}

	instance.stopped = true;
}

void EditorVfxManager::SetEffectPosition(EffectHandle handle, const Vector3& position) {
	if (!handle.IsValid() || handle.index >= static_cast<int32_t>(instances_.size())) {
		return;
	}

	EffectInstanceSlot& instance = instances_[static_cast<size_t>(handle.index)];
	if (!instance.inUse || instance.generation != handle.generation || instance.followGameObject) {
		return;
	}

	instance.fixedPosition = position;
}

bool EditorVfxManager::IsEffectPlaying(EffectHandle handle) const {
	if (!handle.IsValid() || handle.index >= static_cast<int32_t>(instances_.size())) {
		return false;
	}

	const EffectInstanceSlot& instance = instances_[static_cast<size_t>(handle.index)];
	return instance.inUse && instance.generation == handle.generation;
}

float EditorVfxManager::ComputeLodSpawnMultiplier(const EffectDefinition& definition, float distanceToCamera) const {
	if (definition.lodLevels.empty()) {
		return 1.0f;
	}

	for (const EffectLodLevel& level : definition.lodLevels) {
		if (distanceToCamera <= level.distance) {
			return level.spawnMultiplier;
		}
	}

	// 最遠段階を超えたらEffectをSpawnしない(Very Far: Effect無し)。
	return 0.0f;
}

bool EditorVfxManager::IsInstanceFinished(const EffectInstanceSlot& instance) const {
	for (const NodeRuntime& node : instance.nodes) {
		if (node.kind == NodeKind::Billboard && !node.particles.empty()) {
			return false;
		}
		if (node.kind == NodeKind::Ribbon && !node.ribbonHistory.empty()) {
			return false;
		}
		if (node.kind == NodeKind::Ring && node.ring.isActive) {
			return false;
		}
	}
	return true;
}

void EditorVfxManager::Update(float deltaTime) {
	if (!isStarted_ || deltaTime <= 0.0f) {
		return;
	}

	using namespace EditorSharedState;

	for (int32_t index = 0; index < static_cast<int32_t>(instances_.size()); index++) {
		EffectInstanceSlot& instance = instances_[static_cast<size_t>(index)];
		if (!instance.inUse) {
			continue;
		}

		if (instance.followGameObject) {
			Vector3 worldScale{1.0f, 1.0f, 1.0f};
			Vector3 worldRotation{0.0f, 0.0f, 0.0f};
			Vector3 worldPosition{0.0f, 0.0f, 0.0f};
			if (editorScene_ != nullptr &&
				editorScene_->GetWorldTransform(instance.followGameObjectId, worldScale, worldRotation, worldPosition)) {
				instance.worldPosition = Add(worldPosition, instance.localOffset);
			}
			else {
				// 追従先GameObjectが消えたら新規発生を止め、寿命が尽き次第Poolへ戻す。
				instance.stopped = true;
			}
		}
		else {
			instance.worldPosition = instance.fixedPosition;
		}

		const float distanceToCamera = Length(Subtract(instance.worldPosition, g_gameCameraPosition));
		instance.lodSpawnMultiplier = instance.definition != nullptr
			? ComputeLodSpawnMultiplier(*instance.definition, distanceToCamera)
			: 1.0f;
		instance.age += deltaTime;

		for (NodeRuntime& node : instance.nodes) {
			switch (node.kind) {
				case NodeKind::Billboard:
					UpdateBillboardNode(instance, node, deltaTime);
					break;
				case NodeKind::GpuBillboard:
					UpdateGpuBillboardNode(instance, node, deltaTime);
					break;
				case NodeKind::MeshParticle:
					UpdateMeshParticleNode(instance, node, deltaTime);
					break;
				case NodeKind::Ribbon:
					UpdateRibbonNode(instance, node, deltaTime);
					break;
				case NodeKind::Ring:
					UpdateRingNode(instance, node, deltaTime);
					break;
				default:
					break;
			}
		}

		if (instance.stopped && IsInstanceFinished(instance)) {
			auto activeCountIterator = activeCountByEffectId_.find(instance.effectId);
			if (activeCountIterator != activeCountByEffectId_.end()) {
				activeCountIterator->second = (std::max)(activeCountIterator->second - 1, 0);
			}
			ReleaseInstanceSlot(index);
		}
	}

	UpdateDecals(deltaTime);
}

void EditorVfxManager::UpdateBillboardNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime) {
	const EffectNodeDefinition& definition = *node.definition;

	for (BillboardParticleRuntime& particle : node.particles) {
		particle.age += deltaTime;
		particle.velocity.y -= definition.gravity * deltaTime;
		const float dragScale = (std::max)(1.0f - definition.drag * deltaTime, 0.0f);
		particle.velocity = Multiply(dragScale, particle.velocity);
		particle.position = Add(particle.position, Multiply(deltaTime, particle.velocity));
		particle.rotation += particle.rotationSpeed * deltaTime;
	}

	node.particles.erase(
		std::remove_if(
			node.particles.begin(),
			node.particles.end(),
			[](const BillboardParticleRuntime& particle) { return particle.age >= particle.lifetime; }),
		node.particles.end());

	if (instance.stopped || instance.lodSpawnMultiplier <= 0.0f) {
		return;
	}

	const Vector3 baseDirection = SafeNormalize(definition.direction);
	int32_t spawnCount = 0;

	if (!node.burstDone) {
		spawnCount += definition.burstCount;
		node.burstDone = true;
	}

	node.spawnAccumulator += definition.emissionRate * instance.lodSpawnMultiplier * deltaTime;
	const int32_t continuousCount = static_cast<int32_t>(node.spawnAccumulator);
	if (continuousCount > 0) {
		node.spawnAccumulator -= static_cast<float>(continuousCount);
		spawnCount += continuousCount;
	}

	const int32_t remainingCapacity = (std::max)(definition.maxCount - static_cast<int32_t>(node.particles.size()), 0);
	spawnCount = (std::clamp)(spawnCount, 0, remainingCapacity);

	for (int32_t spawnIndex = 0; spawnIndex < spawnCount; spawnIndex++) {
		BillboardParticleRuntime particle{};
		Vector3 randomOffset{0.0f, 0.0f, 0.0f};
		if (definition.shapeRadius > 0.0f) {
			const Vector3 randomDirection = SafeNormalize({
				RandomRange(-1.0f, 1.0f),
				RandomRange(-1.0f, 1.0f),
				RandomRange(-1.0f, 1.0f)});
			const float radius = RandomRange(0.0f, definition.shapeRadius);
			randomOffset = Multiply(radius, randomDirection);
		}

		particle.position = Add(instance.worldPosition, randomOffset);
		const float speedScale = 1.0f + RandomRange(-definition.speedRandomness, definition.speedRandomness);
		particle.velocity = Multiply(definition.speed * speedScale, baseDirection);
		particle.age = 0.0f;
		particle.lifetime = (std::max)(
			definition.lifetime * (1.0f + RandomRange(-definition.lifetimeRandomness, definition.lifetimeRandomness)),
			0.01f);
		particle.size = 1.0f + RandomRange(-definition.sizeRandomness, definition.sizeRandomness);
		particle.rotation = RandomRange(0.0f, kPi * 2.0f);
		particle.rotationSpeed = definition.rotationSpeedDegrees * (kPi / 180.0f);
		node.particles.push_back(particle);
	}
}

// Stage2: GPU Compute Particle。CPU側はSpawnタイミング(Burst/EmissionRate/LOD)だけ計算し、
// Position/Velocity/Lifetime積分はEditorGpuParticleManagerのCompute Shaderへ委ねる(既存EditorEffectManager経路を再利用)。
void EditorVfxManager::UpdateGpuBillboardNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime) {
	const EffectNodeDefinition& definition = *node.definition;

	if (instance.stopped || instance.lodSpawnMultiplier <= 0.0f) {
		return;
	}

	const Vector3 baseDirection = SafeNormalize(definition.direction);
	int32_t spawnCount = 0;

	if (!node.burstDone) {
		spawnCount += definition.burstCount;
		node.burstDone = true;
	}

	node.spawnAccumulator += definition.emissionRate * instance.lodSpawnMultiplier * deltaTime;
	const int32_t continuousCount = static_cast<int32_t>(node.spawnAccumulator);
	if (continuousCount > 0) {
		node.spawnAccumulator -= static_cast<float>(continuousCount);
		spawnCount += continuousCount;
	}

	spawnCount = (std::clamp)(spawnCount, 0, (std::max)(definition.maxCount, 0));

	for (int32_t spawnIndex = 0; spawnIndex < spawnCount; spawnIndex++) {
		Vector3 randomOffset{0.0f, 0.0f, 0.0f};
		if (definition.shapeRadius > 0.0f) {
			const Vector3 randomDirection = SafeNormalize({
				RandomRange(-1.0f, 1.0f),
				RandomRange(-1.0f, 1.0f),
				RandomRange(-1.0f, 1.0f)});
			const float radius = RandomRange(0.0f, definition.shapeRadius);
			randomOffset = Multiply(radius, randomDirection);
		}

		const float speedScale = 1.0f + RandomRange(-definition.speedRandomness, definition.speedRandomness);
		const float lifetime = (std::max)(
			definition.lifetime * (1.0f + RandomRange(-definition.lifetimeRandomness, definition.lifetimeRandomness)),
			0.01f);
		const float sizeScale = 1.0f + RandomRange(-definition.sizeRandomness, definition.sizeRandomness);

		EditorEffectManager::GpuParticleSpawn spawn{};
		spawn.position = Add(instance.worldPosition, randomOffset);
		spawn.velocity = Multiply(definition.speed * speedScale, baseDirection);
		spawn.lifetime = lifetime;
		spawn.startSize = definition.sizeStart * sizeScale;
		spawn.endSize = definition.sizeEnd * sizeScale;
		spawn.startColor = definition.colorStart;
		spawn.endColor = definition.colorEnd;
		spawn.startAlpha = definition.alphaStart;
		spawn.endAlpha = definition.alphaEnd;
		spawn.gravity = definition.gravity;
		spawn.drag = definition.drag;
		spawn.motionType = 0;  // 直線運動。CPU側と同じ挙動(既存Compute Shaderが対応するMode 0)。
		spawn.billboardMode = static_cast<int32_t>(definition.billboardMode);
		spawn.billboardStretch = definition.stretchScale;
		spawn.rotationSpeed = definition.rotationSpeedDegrees * (kPi / 180.0f);
		// renderAssetPathは空のまま(=板ParticleとしてGPU側で描画)。
		pendingGpuParticleSpawns_.push_back(spawn);
		hasEverSpawnedGpuParticles_ = true;
	}
}

// Stage2: Mesh Particle(破片・薬莢等)。BillboardのGPU Simulationと同じSpawnタイミング計算を使い、
// renderAssetPathへMesh Assetを積むことでEditorGpuParticleManagerのGPUインスタンシング描画パスに乗せる。
void EditorVfxManager::UpdateMeshParticleNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime) {
	const EffectNodeDefinition& definition = *node.definition;

	if (definition.meshAssetPath.empty()) {
		return;
	}

	if (instance.stopped || instance.lodSpawnMultiplier <= 0.0f) {
		return;
	}

	const Vector3 baseDirection = SafeNormalize(definition.direction);
	int32_t spawnCount = 0;

	if (!node.burstDone) {
		spawnCount += definition.burstCount;
		node.burstDone = true;
	}

	node.spawnAccumulator += definition.emissionRate * instance.lodSpawnMultiplier * deltaTime;
	const int32_t continuousCount = static_cast<int32_t>(node.spawnAccumulator);
	if (continuousCount > 0) {
		node.spawnAccumulator -= static_cast<float>(continuousCount);
		spawnCount += continuousCount;
	}

	spawnCount = (std::clamp)(spawnCount, 0, (std::max)(definition.maxCount, 0));

	for (int32_t spawnIndex = 0; spawnIndex < spawnCount; spawnIndex++) {
		// 破片らしいランダム散乱方向(baseDirectionを中心にshapeRadius分だけ球状に散らす)。
		Vector3 scatterDirection = baseDirection;
		if (definition.shapeRadius > 0.0f) {
			scatterDirection = SafeNormalize(Add(baseDirection, Vector3{
				RandomRange(-definition.shapeRadius, definition.shapeRadius),
				RandomRange(-definition.shapeRadius, definition.shapeRadius),
				RandomRange(-definition.shapeRadius, definition.shapeRadius)}));
		}

		const float speedScale = 1.0f + RandomRange(-definition.speedRandomness, definition.speedRandomness);
		const float lifetime = (std::max)(
			definition.lifetime * (1.0f + RandomRange(-definition.lifetimeRandomness, definition.lifetimeRandomness)),
			0.01f);
		const float sizeScale = 1.0f + RandomRange(-definition.sizeRandomness, definition.sizeRandomness);

		EditorEffectManager::GpuParticleSpawn spawn{};
		spawn.position = instance.worldPosition;
		spawn.velocity = Multiply(definition.speed * speedScale, scatterDirection);
		spawn.lifetime = lifetime;
		spawn.startSize = definition.sizeStart * sizeScale;
		spawn.endSize = definition.sizeEnd * sizeScale;
		spawn.startColor = definition.colorStart;
		spawn.endColor = definition.colorEnd;
		spawn.startAlpha = definition.alphaStart;
		spawn.endAlpha = definition.alphaEnd;
		spawn.gravity = definition.gravity;
		spawn.drag = definition.drag;
		spawn.motionType = 0;
		spawn.rotation = RandomRange(0.0f, kPi * 2.0f);
		spawn.rotationSpeed = definition.rotationSpeedDegrees * (kPi / 180.0f);
		spawn.renderAssetPath = definition.meshAssetPath;  // FBX / OBJをEditorGpuParticleManagerがGPUインスタンシング描画する。
		pendingGpuParticleSpawns_.push_back(spawn);
		hasEverSpawnedGpuParticles_ = true;
	}
}

// Stage2: Decal投影。GBuffer合成パスは新設せず、既存DecalProjector Componentの描画パス(SpriteRenderer相当のQuad投影)を
// 動的に生成したGameObjectで再利用する(EditorSceneSynchronizer / EditorRenderManagerの既存経路は無改修)。
void EditorVfxManager::SpawnDecal(EffectInstanceSlot& instance, NodeRuntime& node) {
	if (editorScene_ == nullptr || node.definition == nullptr) {
		return;
	}

	const EffectNodeDefinition& definition = *node.definition;

	// Decal Poolの上限(同時に投影できるDecal数)。過剰生成を避けるため古いものから解放する。
	constexpr size_t kMaxDecalCount = 64u;
	while (decalRuntimes_.size() >= kMaxDecalCount) {
		const DecalRuntime& oldest = decalRuntimes_.front();
		if (editorScene_->FindGameObject(oldest.gameObjectId) != nullptr) {
			editorScene_->DeleteGameObject(oldest.gameObjectId);
		}
		decalRuntimes_.erase(decalRuntimes_.begin());
	}

	const int32_t decalGameObjectId = editorScene_->CreateGameObject("VfxDecal_" + definition.name);
	if (decalGameObjectId < 0) {
		return;
	}

	if (!editorScene_->AddComponent(decalGameObjectId, EditorComponentType::DecalProjector)) {
		editorScene_->DeleteGameObject(decalGameObjectId);
		return;
	}

	EditorGameObject* decalObject = editorScene_->FindGameObject(decalGameObjectId);
	if (decalObject == nullptr) {
		return;
	}

	decalObject->translate = instance.worldPosition;
	// 法線の水平成分からYawだけ合わせる(既存Engineには法線→3軸姿勢の変換Utilityが無く、
	// Pitch/Rollまで含めた完全な基底合わせはRenderer/GBuffer統合と合わせて要実機確認のため今回は見送り)。
	decalObject->rotate = {0.0f, std::atan2(instance.hitNormal.x, instance.hitNormal.z), 0.0f};
	const float decalSize = (std::max)(definition.sizeStart, 0.01f);
	decalObject->scale = {1.0f, 1.0f, 1.0f};

	for (EditorComponent& component : decalObject->components) {
		if (component.type == EditorComponentType::DecalProjector) {
			component.assetPath = definition.texturePath;
			component.colliderSize = {decalSize, decalSize, decalSize};
			component.alpha = definition.alphaStart;
			break;
		}
	}

	DecalRuntime runtime{};
	runtime.gameObjectId = decalGameObjectId;
	runtime.age = 0.0f;
	runtime.lifetime = (std::max)(definition.lifetime, 0.1f);
	runtime.alphaStart = definition.alphaStart;
	decalRuntimes_.push_back(runtime);
}

void EditorVfxManager::UpdateDecals(float deltaTime) {
	if (editorScene_ == nullptr) {
		return;
	}

	for (size_t index = 0; index < decalRuntimes_.size();) {
		DecalRuntime& decal = decalRuntimes_[index];
		decal.age += deltaTime;

		EditorGameObject* decalObject = editorScene_->FindGameObject(decal.gameObjectId);
		if (decalObject == nullptr) {
			decalRuntimes_.erase(decalRuntimes_.begin() + static_cast<std::ptrdiff_t>(index));
			continue;
		}

		if (decal.age >= decal.lifetime) {
			editorScene_->DeleteGameObject(decal.gameObjectId);
			decalRuntimes_.erase(decalRuntimes_.begin() + static_cast<std::ptrdiff_t>(index));
			continue;
		}

		// 寿命の後半でFade Outする(焦げ跡・弾痕が急に消えないように)。
		const float fadeStartRate = 0.6f;
		const float lifeRate = decal.age / decal.lifetime;
		if (lifeRate > fadeStartRate) {
			const float fadeRate = (std::clamp)((lifeRate - fadeStartRate) / (1.0f - fadeStartRate), 0.0f, 1.0f);
			for (EditorComponent& component : decalObject->components) {
				if (component.type == EditorComponentType::DecalProjector) {
					component.alpha = decal.alphaStart * (1.0f - fadeRate);
					break;
				}
			}
		}

		index++;
	}
}

void EditorVfxManager::UpdateRibbonNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime) {
	const EffectNodeDefinition& definition = *node.definition;

	for (RibbonPointRuntime& point : node.ribbonHistory) {
		point.age += deltaTime;
	}

	while (!node.ribbonHistory.empty() && node.ribbonHistory.front().age >= definition.ribbonPointLifetime) {
		node.ribbonHistory.pop_front();
	}

	node.ribbonScrollOffset += definition.ribbonUvScrollSpeed * deltaTime;

	if (instance.stopped) {
		return;
	}

	const bool shouldAddPoint = node.ribbonHistory.empty() ||
		Length(Subtract(instance.worldPosition, node.ribbonHistory.back().position)) >= definition.ribbonMinVertexDistance;

	if (shouldAddPoint) {
		RibbonPointRuntime point{};
		point.position = instance.worldPosition;
		point.age = 0.0f;
		node.ribbonHistory.push_back(point);

		while (static_cast<int32_t>(node.ribbonHistory.size()) > definition.ribbonMaxPoints) {
			node.ribbonHistory.pop_front();
		}
	}
}

void EditorVfxManager::UpdateRingNode(EffectInstanceSlot& instance, NodeRuntime& node, float deltaTime) {
	const EffectNodeDefinition& definition = *node.definition;

	if (!node.burstDone && !instance.stopped && instance.lodSpawnMultiplier > 0.0f) {
		node.ring.isActive = true;
		node.ring.age = 0.0f;
		node.burstDone = true;
	}

	if (node.ring.isActive) {
		node.ring.age += deltaTime;
		if (node.ring.age >= definition.ringLifetime) {
			node.ring.isActive = false;
		}
	}
}

float EditorVfxManager::RandomRange(float minimumValue, float maximumValue) {
	if (maximumValue < minimumValue) {
		std::swap(minimumValue, maximumValue);
	}
	std::uniform_real_distribution<float> distribution(minimumValue, maximumValue);
	return distribution(randomEngine_);
}

void EditorVfxManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}

namespace {
	struct PendingGroup {
		std::vector<EditorVfxRenderer::VfxVertex> vertices;
		std::string texturePath;
		EditorVfxRenderer::BlendVariant blendVariant = EditorVfxRenderer::BlendVariant::AlphaBlend;
		float distance = 0.0f;
		bool useSoftParticle = false;
		float softParticleFadeDistance = 1.0f;
	};

	void AppendQuad(
		std::vector<EditorVfxRenderer::VfxVertex>& vertices,
		const Vector3& center,
		const Vector3& right,
		const Vector3& up,
		float u0, float v0, float u1, float v1,
		float r, float g, float b, float a) {
		const Vector3 c0 = Subtract(Subtract(center, right), up);
		const Vector3 c1 = Subtract(Add(center, right), up);
		const Vector3 c2 = Add(Add(center, right), up);
		const Vector3 c3 = Add(Subtract(center, right), up);

		EditorVfxRenderer::VfxVertex v0v{c0, u0, v1, r, g, b, a};
		EditorVfxRenderer::VfxVertex v1v{c1, u1, v1, r, g, b, a};
		EditorVfxRenderer::VfxVertex v2v{c2, u1, v0, r, g, b, a};
		EditorVfxRenderer::VfxVertex v3v{c3, u0, v0, r, g, b, a};

		vertices.push_back(v0v);
		vertices.push_back(v1v);
		vertices.push_back(v2v);
		vertices.push_back(v0v);
		vertices.push_back(v2v);
		vertices.push_back(v3v);
	}

	EditorVfxRenderer::BlendVariant ResolveBlendVariant(EffectBlendMode blendMode) {
		return blendMode == EffectBlendMode::Additive
			? EditorVfxRenderer::BlendVariant::Additive
			: EditorVfxRenderer::BlendVariant::AlphaBlend;
	}
}

void EditorVfxManager::BuildDrawBatches(
	EditorVfxRenderer& renderer,
	const Vector3& cameraPosition,
	const Vector3& cameraRight,
	const Vector3& cameraUp,
	const Vector3& cameraForward,
	std::vector<EditorVfxRenderer::VfxBatch>& outBatches) {
	outBatches.clear();
	renderer.BeginFrame();

	std::vector<PendingGroup> alphaGroups;
	std::vector<PendingGroup> additiveGroups;

	for (const EffectInstanceSlot& instance : instances_) {
		if (!instance.inUse || instance.definition == nullptr) {
			continue;
		}

		const float instanceDistance = Length(Subtract(instance.worldPosition, cameraPosition));

		for (const NodeRuntime& node : instance.nodes) {
			if (node.definition == nullptr) {
				continue;
			}

			const EffectNodeDefinition& definition = *node.definition;
			PendingGroup group{};
			group.texturePath = definition.texturePath;
			group.blendVariant = ResolveBlendVariant(definition.blendMode);
			group.distance = instanceDistance;
			group.useSoftParticle = definition.useSoftParticle;
			group.softParticleFadeDistance = definition.softParticleFadeDistance;

			if (node.kind == NodeKind::Billboard) {
				for (const BillboardParticleRuntime& particle : node.particles) {
					const float lifeRate = (std::clamp)(particle.age / (std::max)(particle.lifetime, 0.001f), 0.0f, 1.0f);
					const float currentSize = LerpFloat(definition.sizeStart, definition.sizeEnd, lifeRate) * particle.size;
					const Vector3 color = LerpVector3(definition.colorStart, definition.colorEnd, lifeRate);
					const float alpha = LerpFloat(definition.alphaStart, definition.alphaEnd, lifeRate);

					float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
					if (definition.useFlipbook) {
						ComputeFlipbookUv(definition, particle.age, u0, v0, u1, v1);
					}

					Vector3 right{0.0f, 0.0f, 0.0f};
					Vector3 up{0.0f, 0.0f, 0.0f};

					switch (definition.billboardMode) {
						case EffectBillboardMode::YAxisBillboard: {
							const Vector3 worldUp{0.0f, 1.0f, 0.0f};
							Vector3 axisRight = SafeNormalize(Cross(worldUp, cameraForward));
							right = Multiply(currentSize * 0.5f, axisRight);
							up = Multiply(currentSize * 0.5f, worldUp);
							break;
						}
						case EffectBillboardMode::Directional: {
							const Vector3 axisDirection = SafeNormalize(particle.velocity);
							const Vector3 sideAxis = SafeNormalize(Cross(axisDirection, cameraForward));
							right = Multiply(currentSize * 0.5f * definition.stretchScale, axisDirection);
							up = Multiply(currentSize * 0.35f, sideAxis);
							break;
						}
						case EffectBillboardMode::Fixed: {
							right = {currentSize * 0.5f, 0.0f, 0.0f};
							up = {0.0f, currentSize * 0.5f, 0.0f};
							break;
						}
						case EffectBillboardMode::CameraFacing:
						default: {
							right = Multiply(currentSize * 0.5f, SafeNormalize(cameraRight));
							up = Multiply(currentSize * 0.5f, SafeNormalize(cameraUp));
							break;
						}
					}

					// Rotated Billboard: Camera Facing / FixedのRoll回転をここでまとめて適用する。
					if (definition.billboardMode == EffectBillboardMode::CameraFacing ||
						definition.billboardMode == EffectBillboardMode::Fixed) {
						const float cosAngle = std::cos(particle.rotation);
						const float sinAngle = std::sin(particle.rotation);
						const Vector3 rotatedRight = Add(Multiply(cosAngle, right), Multiply(sinAngle, up));
						const Vector3 rotatedUp = Add(Multiply(-sinAngle, right), Multiply(cosAngle, up));
						right = rotatedRight;
						up = rotatedUp;
					}

					AppendQuad(group.vertices, particle.position, right, up, u0, v0, u1, v1, color.x, color.y, color.z, alpha);
				}
			}
			else if (node.kind == NodeKind::Ribbon && node.ribbonHistory.size() >= 2u) {
				const size_t pointCount = node.ribbonHistory.size();
				std::vector<Vector3> leftPoints(pointCount);
				std::vector<Vector3> rightPoints(pointCount);
				std::vector<float> alphaPoints(pointCount);

				for (size_t pointIndex = 0u; pointIndex < pointCount; pointIndex++) {
					const RibbonPointRuntime& point = node.ribbonHistory[pointIndex];
					Vector3 tangent{0.0f, 0.0f, 1.0f};
					if (pointIndex + 1u < pointCount) {
						tangent = SafeNormalize(Subtract(node.ribbonHistory[pointIndex + 1u].position, point.position));
					}
					else if (pointIndex > 0u) {
						tangent = SafeNormalize(Subtract(point.position, node.ribbonHistory[pointIndex - 1u].position));
					}

					const Vector3 perpendicular = SafeNormalize(Cross(tangent, cameraForward));
					const float lifeRate = (std::clamp)(point.age / (std::max)(definition.ribbonPointLifetime, 0.001f), 0.0f, 1.0f);
					const float halfWidth = definition.ribbonWidth * 0.5f * (1.0f - lifeRate);
					leftPoints[pointIndex] = Subtract(point.position, Multiply(halfWidth, perpendicular));
					rightPoints[pointIndex] = Add(point.position, Multiply(halfWidth, perpendicular));
					alphaPoints[pointIndex] = LerpFloat(definition.alphaStart, definition.alphaEnd, lifeRate);
				}

				for (size_t segmentIndex = 0u; segmentIndex + 1u < pointCount; segmentIndex++) {
					const float u0 = static_cast<float>(segmentIndex) / static_cast<float>(pointCount - 1u) + node.ribbonScrollOffset;
					const float u1 = static_cast<float>(segmentIndex + 1u) / static_cast<float>(pointCount - 1u) + node.ribbonScrollOffset;

					EditorVfxRenderer::VfxVertex vertexLeft0{
						leftPoints[segmentIndex], u0, 0.0f,
						definition.colorStart.x, definition.colorStart.y, definition.colorStart.z, alphaPoints[segmentIndex]};
					EditorVfxRenderer::VfxVertex vertexRight0{
						rightPoints[segmentIndex], u0, 1.0f,
						definition.colorStart.x, definition.colorStart.y, definition.colorStart.z, alphaPoints[segmentIndex]};
					EditorVfxRenderer::VfxVertex vertexLeft1{
						leftPoints[segmentIndex + 1u], u1, 0.0f,
						definition.colorStart.x, definition.colorStart.y, definition.colorStart.z, alphaPoints[segmentIndex + 1u]};
					EditorVfxRenderer::VfxVertex vertexRight1{
						rightPoints[segmentIndex + 1u], u1, 1.0f,
						definition.colorStart.x, definition.colorStart.y, definition.colorStart.z, alphaPoints[segmentIndex + 1u]};

					group.vertices.push_back(vertexLeft0);
					group.vertices.push_back(vertexRight0);
					group.vertices.push_back(vertexRight1);
					group.vertices.push_back(vertexLeft0);
					group.vertices.push_back(vertexRight1);
					group.vertices.push_back(vertexLeft1);
				}
			}
			else if (node.kind == NodeKind::Ring && node.ring.isActive) {
				const float lifeRate = (std::clamp)(node.ring.age / (std::max)(definition.ringLifetime, 0.001f), 0.0f, 1.0f);
				const float innerRadius = LerpFloat(definition.ringInnerRadiusStart, definition.ringInnerRadiusEnd, lifeRate);
				const float outerRadius = LerpFloat(definition.ringOuterRadiusStart, definition.ringOuterRadiusEnd, lifeRate);
				const float alpha = LerpFloat(definition.alphaStart, definition.alphaEnd, lifeRate);
				const Vector3 color = LerpVector3(definition.colorStart, definition.colorEnd, lifeRate);
				const int32_t segments = (std::max)(definition.ringSegments, 3);

				for (int32_t segmentIndex = 0; segmentIndex < segments; segmentIndex++) {
					const float angle0 = (static_cast<float>(segmentIndex) / static_cast<float>(segments)) * kPi * 2.0f;
					const float angle1 = (static_cast<float>(segmentIndex + 1) / static_cast<float>(segments)) * kPi * 2.0f;
					const Vector3 innerPoint0 = Add(instance.worldPosition, Vector3{innerRadius * std::cos(angle0), 0.0f, innerRadius * std::sin(angle0)});
					const Vector3 outerPoint0 = Add(instance.worldPosition, Vector3{outerRadius * std::cos(angle0), 0.0f, outerRadius * std::sin(angle0)});
					const Vector3 innerPoint1 = Add(instance.worldPosition, Vector3{innerRadius * std::cos(angle1), 0.0f, innerRadius * std::sin(angle1)});
					const Vector3 outerPoint1 = Add(instance.worldPosition, Vector3{outerRadius * std::cos(angle1), 0.0f, outerRadius * std::sin(angle1)});
					const float u0 = static_cast<float>(segmentIndex) / static_cast<float>(segments);
					const float u1 = static_cast<float>(segmentIndex + 1) / static_cast<float>(segments);

					EditorVfxRenderer::VfxVertex innerVertex0{innerPoint0, u0, 0.0f, color.x, color.y, color.z, alpha};
					EditorVfxRenderer::VfxVertex outerVertex0{outerPoint0, u0, 1.0f, color.x, color.y, color.z, alpha};
					EditorVfxRenderer::VfxVertex innerVertex1{innerPoint1, u1, 0.0f, color.x, color.y, color.z, alpha};
					EditorVfxRenderer::VfxVertex outerVertex1{outerPoint1, u1, 1.0f, color.x, color.y, color.z, alpha};

					group.vertices.push_back(innerVertex0);
					group.vertices.push_back(outerVertex0);
					group.vertices.push_back(outerVertex1);
					group.vertices.push_back(innerVertex0);
					group.vertices.push_back(outerVertex1);
					group.vertices.push_back(innerVertex1);
				}
			}

			if (group.vertices.empty()) {
				continue;
			}

			renderer.EnsureTexture(group.texturePath);

			if (group.blendVariant == EditorVfxRenderer::BlendVariant::Additive || !definition.sortByDistance) {
				additiveGroups.push_back(std::move(group));
			}
			else {
				alphaGroups.push_back(std::move(group));
			}
		}
	}

	// Additiveや非Sorting指定はSort不要。AlphaBlendのみNode単位(=荒い粒度)でPainter's Algorithm用に距離Sortする。
	// 全Particleを個別にSortしないことでCPU負荷を抑える。
	std::sort(
		alphaGroups.begin(),
		alphaGroups.end(),
		[](const PendingGroup& lhs, const PendingGroup& rhs) { return lhs.distance > rhs.distance; });

	for (const PendingGroup& group : alphaGroups) {
		const uint32_t firstVertex = renderer.AppendVertices(group.vertices.data(), static_cast<uint32_t>(group.vertices.size()));
		if (firstVertex == UINT32_MAX) {
			continue;
		}
		EditorVfxRenderer::VfxBatch* batch = FindOrAddBatch(
			outBatches, group.texturePath, group.blendVariant, firstVertex,
			group.useSoftParticle, group.softParticleFadeDistance);
		batch->vertexCount += static_cast<uint32_t>(group.vertices.size());
	}

	for (const PendingGroup& group : additiveGroups) {
		const uint32_t firstVertex = renderer.AppendVertices(group.vertices.data(), static_cast<uint32_t>(group.vertices.size()));
		if (firstVertex == UINT32_MAX) {
			continue;
		}
		EditorVfxRenderer::VfxBatch* batch = FindOrAddBatch(
			outBatches, group.texturePath, group.blendVariant, firstVertex,
			group.useSoftParticle, group.softParticleFadeDistance);
		batch->vertexCount += static_cast<uint32_t>(group.vertices.size());
	}

	renderer.EndFrame();
}

EditorVfxManager::DebugStats EditorVfxManager::GetDebugStats() const {
	DebugStats stats{};
	stats.poolCapacity = kMaxEffectInstances;
	stats.poolUsedCount = kMaxEffectInstances - static_cast<int32_t>(freeIndices_.size());

	for (const EffectInstanceSlot& instance : instances_) {
		if (!instance.inUse) {
			continue;
		}

		stats.activeEffectCount++;
		DebugEffectEntry entry{};
		entry.effectId = instance.effectId;
		entry.nodeCount = static_cast<int32_t>(instance.nodes.size());
		entry.lodSpawnMultiplier = instance.lodSpawnMultiplier;
		entry.isFollowing = instance.followGameObject;

		for (const NodeRuntime& node : instance.nodes) {
			stats.activeEmitterCount++;
			int32_t particleCount = 0;
			std::string modeName = "Unsupported";

			switch (node.kind) {
				case NodeKind::Billboard:
					particleCount = static_cast<int32_t>(node.particles.size());
					modeName = node.definition != nullptr && node.definition->useFlipbook ? "Flipbook" : "Billboard";
					break;
				case NodeKind::GpuBillboard:
					modeName = "Billboard(GPU)";
					break;
				case NodeKind::MeshParticle:
					modeName = "MeshParticle(GPU)";
					break;
				case NodeKind::Decal:
					modeName = "Decal";
					break;
				case NodeKind::Ribbon:
					particleCount = static_cast<int32_t>(node.ribbonHistory.size());
					modeName = "Ribbon";
					break;
				case NodeKind::Ring:
					particleCount = node.ring.isActive ? 1 : 0;
					modeName = "Ring";
					break;
				default:
					break;
			}

			stats.activeParticleCount += particleCount;
			if (!entry.drawModeSummary.empty()) {
				entry.drawModeSummary += "/";
			}
			entry.drawModeSummary += modeName;
			entry.particleCount += particleCount;
		}

		stats.effects.push_back(std::move(entry));
	}

	return stats;
}
