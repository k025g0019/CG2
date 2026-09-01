#include "EditorObjectPoolManager.h"

#include "EditorComponentUtility.h"
#include "EditorDamageManager.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {
	struct PoolSpecification {
		int32_t ownerGameObjectId = -1;
		int32_t templateGameObjectId = -1;
		int32_t initialCapacity = 0;
		bool allowExpand = false;
	};
}

void EditorObjectPoolManager::Initialize(
	EditorScene* editorScene,
	EditorPhysicsManager* physicsManager,
	EditorDamageManager* damageManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	damageManager_ = damageManager;
	scriptManager_ = scriptManager;
	poolRuntimes_.clear();
	poolOwnerByItemId_.clear();
	spawnVersions_.clear();
	originalActiveStates_.clear();
	spawnerRuntimes_.clear();
	isStarted_ = false;
	runtimeResetCallback_ = {};
}

void EditorObjectPoolManager::PreparePools() {
	poolRuntimes_.clear();
	poolOwnerByItemId_.clear();
	spawnVersions_.clear();
	originalActiveStates_.clear();
	spawnerRuntimes_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	std::vector<PoolSpecification> poolSpecifications;
	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* poolComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::ObjectPool);

		if (!gameObject.isActive || poolComponent == nullptr || !poolComponent->isActive) {
			continue;
		}

		PoolSpecification poolSpecification{};
		poolSpecification.ownerGameObjectId = gameObject.id;
		poolSpecification.templateGameObjectId = poolComponent->objectPoolTemplateGameObjectId;
		poolSpecification.initialCapacity = (std::clamp)(poolComponent->objectPoolInitialSize, 1, 1024);
		poolSpecification.allowExpand = poolComponent->objectPoolAllowExpand;
		poolSpecifications.push_back(poolSpecification);
	}

	for (const PoolSpecification& poolSpecification : poolSpecifications) {
		EditorGameObject* templateGameObject = editorScene_->FindGameObject(
			poolSpecification.templateGameObjectId);

		if (templateGameObject == nullptr || templateGameObject->id == poolSpecification.ownerGameObjectId) {
			continue;
		}

		PoolRuntime poolRuntime{};
		const int32_t templateGameObjectId = templateGameObject->id;
		poolRuntime.templateGameObjectId = templateGameObjectId;
		poolRuntime.initialCapacity = poolSpecification.initialCapacity;
		poolRuntime.allowExpand = poolSpecification.allowExpand;
		templateGameObject->isActive = true;  // Physics開始時にTemplate Bodyも作り、直後に待機へ移す。
		poolRuntime.itemGameObjectIds.push_back(templateGameObject->id);

		for (const int32_t itemGameObjectId : poolRuntime.itemGameObjectIds) {
			poolOwnerByItemId_[itemGameObjectId] = poolSpecification.ownerGameObjectId;
			spawnVersions_[itemGameObjectId] = 0u;
		}

		poolRuntimes_[poolSpecification.ownerGameObjectId] = std::move(poolRuntime);
	}
}

void EditorObjectPoolManager::Start() {
	isStarted_ = true;
	spawnerRuntimes_.clear();

	for (auto& poolRuntimePair : poolRuntimes_) {
		PoolRuntime& poolRuntime = poolRuntimePair.second;
		poolRuntime.activeGameObjectIds.clear();

		for (const int32_t itemGameObjectId : poolRuntime.itemGameObjectIds) {
			SetItemActive(itemGameObjectId, false);
		}
	}

	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* spawnerComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::PrefabSpawner);

		if (!gameObject.isActive || spawnerComponent == nullptr || !spawnerComponent->isActive) {
			continue;
		}

		SpawnerRuntime spawnerRuntime{};
		spawnerRuntime.timer = (std::max)(spawnerComponent->prefabSpawnerInterval, 0.0f);
		spawnerRuntimes_[gameObject.id] = spawnerRuntime;
	}
}

// Physics/Script のStartが済んだ直後に呼ぶ。CreatePoolItemは物理BodyとScript Bindingを
// 登録するため、それらのManagerがStart済みである点が旧来の遅延生成経路と同じ条件になる。
void EditorObjectPoolManager::PrewarmAllPools() {
	for (auto& poolRuntimePair : poolRuntimes_) {
		PrewarmPool(poolRuntimePair.second, poolRuntimePair.first);
	}
}

void EditorObjectPoolManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime < 0.0f) {
		return;
	}

	for (auto& spawnerRuntimePair : spawnerRuntimes_) {
		EditorGameObject* spawnerGameObject = editorScene_->FindGameObject(spawnerRuntimePair.first);

		if (spawnerGameObject == nullptr || !spawnerGameObject->isActive) {
			continue;
		}

		const EditorComponent* spawnerComponent = EditorComponentUtility::FindComponent(
			*spawnerGameObject,
			EditorComponentType::PrefabSpawner);

		if (spawnerComponent == nullptr || !spawnerComponent->isActive) {
			continue;
		}

		SpawnerRuntime& spawnerRuntime = spawnerRuntimePair.second;

		if (spawnerComponent->prefabSpawnerMode == 1) {
			if (!spawnerRuntime.hasSpawnedOnStart) {
				SpawnFromSpawner(spawnerGameObject->id);
				spawnerRuntime.hasSpawnedOnStart = true;
			}

			continue;
		}

		if (spawnerComponent->prefabSpawnerMode != 2) {
			continue;
		}

		const float spawnInterval = (std::max)(spawnerComponent->prefabSpawnerInterval, 0.01f);
		spawnerRuntime.timer -= deltaTime;

		while (spawnerRuntime.timer <= 0.0f) {
			SpawnFromSpawner(spawnerGameObject->id);
			spawnerRuntime.timer += spawnInterval;
		}
	}
}

void EditorObjectPoolManager::Stop() {
	poolRuntimes_.clear();
	poolOwnerByItemId_.clear();
	spawnVersions_.clear();
	originalActiveStates_.clear();
	spawnerRuntimes_.clear();
	isStarted_ = false;
}

// Templateを1体複製して「待機中のPool Item」を実体化する。
// Play中に呼ぶとDuplicateGameObject + 物理/Script登録が同一フレームで走りHitchになるため、
// 通常はStart()のPrewarmPoolから事前に呼び切っておき、Play中はこの経路へ来ないようにする。
int32_t EditorObjectPoolManager::CreatePoolItem(PoolRuntime& poolRuntime, int32_t ownerGameObjectId) {
	if (editorScene_ == nullptr) {
		return -1;
	}

	const EditorGameObject* templateGameObject = editorScene_->FindGameObject(poolRuntime.templateGameObjectId);
	const std::string templateName = templateGameObject != nullptr
		? templateGameObject->name
		: "PooledObject";
	const int32_t itemIndex = static_cast<int32_t>(poolRuntime.itemGameObjectIds.size());
	const bool isTemplateInUse =
		poolRuntime.activeGameObjectIds.contains(poolRuntime.templateGameObjectId);

	// Templateの本来のActive状態を一時復元して複製し、子階層の有効・無効もそのまま引き継ぐ。
	if (!isTemplateInUse) {
		SetItemActive(poolRuntime.templateGameObjectId, true);
	}

	const int32_t itemGameObjectId = editorScene_->DuplicateGameObject(poolRuntime.templateGameObjectId);

	if (!isTemplateInUse) {
		SetItemActive(poolRuntime.templateGameObjectId, false);
	}

	if (itemGameObjectId < 0) {
		return -1;
	}

	EditorGameObject* duplicatedGameObject = editorScene_->FindGameObject(itemGameObjectId);

	if (duplicatedGameObject != nullptr) {
		duplicatedGameObject->name = templateName + "_Pool_" + std::to_string(itemIndex);

		// Templateは待機位置(地下)にあるため距離LOD等で非Activeにされていることがあり、
		// その状態を複製すると複製個体も非Activeで生まれる。
		// 非Activeのまま物理登録すると「非Activeで登録スキップ」となってBodyが作られず、
		// 見た目は動くのにRay/ShapeCastへ一切引っかからない個体が量産される
		// (実測: Pool個体は全てNoBodyで、弾がすり抜けていた)。
		// 登録が確実に通るよう、ここでActiveへ揃えてから物理/Scriptを登録する。
		// 直後のSetItemActive(false)がこのActive状態を「本来の状態」として記録するので、
		// 貸出時のSetItemActive(true)でも正しくActiveへ戻る。
		duplicatedGameObject->isActive = true;
	}

	// 物理Bodyを作ってから待機へ戻す。Script Bindingは非Activeのまま登録し、初回貸出時にStartする。
	if (physicsManager_ != nullptr) {
		physicsManager_->RegisterRuntimeHierarchy(itemGameObjectId);
	}

	SetItemActive(itemGameObjectId, false);

	if (scriptManager_ != nullptr) {
		scriptManager_->RegisterRuntimeHierarchy(itemGameObjectId);
	}

	poolRuntime.itemGameObjectIds.push_back(itemGameObjectId);
	poolOwnerByItemId_[itemGameObjectId] = ownerGameObjectId;
	spawnVersions_[itemGameObjectId] = 0u;
	return itemGameObjectId;
}

// initialCapacity分のItemをPlay開始時に作り切る。
// 以前はSpawn()の初回貸出時に1体ずつ複製していたため、
//   ・序盤(Poolがまだ空)ほど敵出現のたびにDuplicateGameObjectでカクついた
//   ・敵を倒して補充が湧く瞬間にも同じ複製が走り、単発のHitchになっていた
//   ・終盤はItemが揃って複製が起きないので軽くなる、という非対称な重さになっていた
// ここで先に払っておくことで、Play中の複製コストを無くす。
void EditorObjectPoolManager::PrewarmPool(PoolRuntime& poolRuntime, int32_t ownerGameObjectId) {
	if (editorScene_ == nullptr) {
		return;
	}

	while (static_cast<int32_t>(poolRuntime.itemGameObjectIds.size()) < poolRuntime.initialCapacity) {
		if (CreatePoolItem(poolRuntime, ownerGameObjectId) < 0) {
			break;
		}
	}
}

int32_t EditorObjectPoolManager::Spawn(
	int32_t poolGameObjectId,
	const Vector3& position,
	const Vector3& rotation) {
	const auto poolRuntimeIterator = poolRuntimes_.find(poolGameObjectId);

	if (!isStarted_ || editorScene_ == nullptr || poolRuntimeIterator == poolRuntimes_.end()) {
		return -1;
	}

	PoolRuntime& poolRuntime = poolRuntimeIterator->second;
	int32_t itemGameObjectId = -1;

	for (const int32_t candidateGameObjectId : poolRuntime.itemGameObjectIds) {
		if (!poolRuntime.activeGameObjectIds.contains(candidateGameObjectId)) {
			itemGameObjectId = candidateGameObjectId;
			break;
		}
	}

	const bool hasUnusedInitialCapacity =
		static_cast<int32_t>(poolRuntime.itemGameObjectIds.size()) < poolRuntime.initialCapacity;

	if (itemGameObjectId < 0 && (hasUnusedInitialCapacity || poolRuntime.allowExpand)) {
		itemGameObjectId = CreatePoolItem(poolRuntime, poolGameObjectId);
	}

	EditorGameObject* itemGameObject = editorScene_->FindGameObject(itemGameObjectId);
	if (itemGameObject == nullptr) {
		return -1;
	}

	itemGameObject->translate = position;
	itemGameObject->rotate = rotation;
	poolRuntime.activeGameObjectIds.insert(itemGameObjectId);
	spawnVersions_[itemGameObjectId]++;
	ResetItemRuntimeState(itemGameObjectId);
	SetItemActive(itemGameObjectId, true);

	// 貸出直後、ItemがActiveになった状態で物理Bodyの登録を必ず試す。
	// 複製直後(CreatePoolItem)の登録は、その時点のActive状態やManagerの初期化順に
	// 左右されて失敗し得る。実測でPool複製個体だけBodyが存在せず(NoBody)、
	// 敵が物理世界に居ないため弾が一切当たらない状態になっていた。
	// RegisterRuntimeGameObjectはBody登録済みなら即trueを返すので、ここでの再呼び出しは安全。
	if (physicsManager_ != nullptr) {
		physicsManager_->RegisterRuntimeHierarchy(itemGameObjectId);
		// Bodyを作り直した場合に備え、貸出姿勢をJolt側へも反映しておく。
		Vector3 worldScale{};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		editorScene_->GetWorldTransform(itemGameObjectId, worldScale, worldRotation, worldPosition);
		(void)worldScale;
		physicsManager_->SetGameObjectTransform(itemGameObjectId, worldPosition, worldRotation);
	}

	return itemGameObjectId;
}

int32_t EditorObjectPoolManager::SpawnFromSpawner(int32_t spawnerGameObjectId) {
	if (editorScene_ == nullptr) {
		return -1;
	}

	EditorGameObject* spawnerGameObject = editorScene_->FindGameObject(spawnerGameObjectId);
	if (spawnerGameObject == nullptr || !spawnerGameObject->isActive) {
		return -1;
	}

	const EditorComponent* spawnerComponent = EditorComponentUtility::FindComponent(
		*spawnerGameObject,
		EditorComponentType::PrefabSpawner);

	if (spawnerComponent == nullptr || !spawnerComponent->isActive) {
		return -1;
	}

	const EditorGameObject* spawnPointGameObject = spawnerComponent->prefabSpawnerPointGameObjectId >= 0
		? editorScene_->FindGameObject(spawnerComponent->prefabSpawnerPointGameObjectId)
		: spawnerGameObject;

	if (spawnPointGameObject == nullptr) {
		return -1;
	}

	Vector3 spawnScale = spawnPointGameObject->scale;
	Vector3 spawnRotation = spawnPointGameObject->rotate;
	Vector3 spawnPosition = spawnPointGameObject->translate;
	editorScene_->GetWorldTransform(
		spawnPointGameObject->id,
		spawnScale,
		spawnRotation,
		spawnPosition);
	(void)spawnScale;
	const int32_t spawnedGameObjectId = Spawn(
		spawnerComponent->prefabSpawnerPoolGameObjectId,
		spawnPosition,
		spawnRotation);

	if (spawnedGameObjectId >= 0) {
		// Spawn() のプール拡張でシーンの GameObject 配列が再確保されるため、
		// ポインタを取り直してから使う。
		spawnerGameObject = editorScene_->FindGameObject(spawnerGameObjectId);
		spawnerComponent = spawnerGameObject != nullptr
			? EditorComponentUtility::FindComponent(
				*spawnerGameObject,
				EditorComponentType::PrefabSpawner)
			: nullptr;

		if (spawnerGameObject == nullptr || spawnerComponent == nullptr) {
			return -1;
		}

		QueueSpawnAction(*spawnerGameObject, *spawnerComponent, spawnedGameObjectId);
	}

	return spawnedGameObjectId;
}

bool EditorObjectPoolManager::Release(int32_t gameObjectId) {
	const auto ownerIterator = poolOwnerByItemId_.find(gameObjectId);

	if (ownerIterator == poolOwnerByItemId_.end()) {
		return false;
	}

	const auto poolRuntimeIterator = poolRuntimes_.find(ownerIterator->second);

	if (poolRuntimeIterator == poolRuntimes_.end()) {
		return false;
	}

	PoolRuntime& poolRuntime = poolRuntimeIterator->second;
	poolRuntime.activeGameObjectIds.erase(gameObjectId);
	SetItemActive(gameObjectId, false);
	return true;
}

bool EditorObjectPoolManager::IsPooledObject(int32_t gameObjectId) const {
	return poolOwnerByItemId_.contains(gameObjectId);
}

bool EditorObjectPoolManager::IsPooledObjectActive(int32_t gameObjectId) const {
	const auto ownerIterator = poolOwnerByItemId_.find(gameObjectId);

	if (ownerIterator == poolOwnerByItemId_.end()) {
		return false;
	}

	const auto poolRuntimeIterator = poolRuntimes_.find(ownerIterator->second);
	return
		poolRuntimeIterator != poolRuntimes_.end() &&
		poolRuntimeIterator->second.activeGameObjectIds.contains(gameObjectId);
}

bool EditorObjectPoolManager::HasPool(int32_t poolGameObjectId) const {
	return isStarted_ && poolRuntimes_.contains(poolGameObjectId);
}

uint64_t EditorObjectPoolManager::GetSpawnVersion(int32_t gameObjectId) const {
	const auto spawnVersionIterator = spawnVersions_.find(gameObjectId);

	if (spawnVersionIterator == spawnVersions_.end()) {
		return 0u;
	}

	return spawnVersionIterator->second;
}

bool EditorObjectPoolManager::IsSpawnLeaseActive(int32_t gameObjectId, uint64_t spawnVersion) const {
	const auto ownerIterator = poolOwnerByItemId_.find(gameObjectId);
	const auto spawnVersionIterator = spawnVersions_.find(gameObjectId);

	if (ownerIterator == poolOwnerByItemId_.end() ||
		spawnVersionIterator == spawnVersions_.end() ||
		spawnVersionIterator->second != spawnVersion) {
		return false;
	}

	const auto poolRuntimeIterator = poolRuntimes_.find(ownerIterator->second);

	return
		poolRuntimeIterator != poolRuntimes_.end() &&
		poolRuntimeIterator->second.activeGameObjectIds.contains(gameObjectId);
}

void EditorObjectPoolManager::SetRuntimeResetCallback(std::function<void(int32_t)> callback) {
	runtimeResetCallback_ = std::move(callback);
}

void EditorObjectPoolManager::ResetObjectRuntimeState(int32_t gameObjectId) {
	ResetItemRuntimeState(gameObjectId);
}

void EditorObjectPoolManager::SetItemActive(int32_t gameObjectId, bool isActive) {
	if (editorScene_ == nullptr) {
		return;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return;
	}

	const std::vector<int32_t> childGameObjectIds = gameObject->children;

	if (!isActive) {
		originalActiveStates_.try_emplace(gameObjectId, gameObject->isActive);
	}

	const auto originalActiveIterator = originalActiveStates_.find(gameObjectId);
	const bool targetActive = isActive
		? (originalActiveIterator != originalActiveStates_.end() ? originalActiveIterator->second : true)
		: false;
	gameObject->isActive = targetActive;

	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(gameObjectId, targetActive);

		if (targetActive) {
			Vector3 worldScale = gameObject->scale;
			Vector3 worldRotation = gameObject->rotate;
			Vector3 worldPosition = gameObject->translate;
			editorScene_->GetWorldTransform(
				gameObjectId,
				worldScale,
				worldRotation,
				worldPosition);
			(void)worldScale;
			physicsManager_->SetGameObjectTransform(gameObjectId, worldPosition, worldRotation);
		}
	}

	for (const int32_t childGameObjectId : childGameObjectIds) {
		SetItemActive(childGameObjectId, isActive);
	}
}

void EditorObjectPoolManager::ResetItemRuntimeState(int32_t gameObjectId) {
	if (editorScene_ == nullptr) {
		return;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);

	if (gameObject == nullptr) {
		return;
	}

	if (runtimeResetCallback_) {
		runtimeResetCallback_(gameObjectId);
	}
	else if (damageManager_ != nullptr) {
		damageManager_->ResetRuntimeState(gameObjectId);
	}

	for (const int32_t childGameObjectId : gameObject->children) {
		ResetItemRuntimeState(childGameObjectId);
	}
}

void EditorObjectPoolManager::QueueSpawnAction(
	const EditorGameObject& spawnerGameObject,
	const EditorComponent& spawnerComponent,
	int32_t spawnedGameObjectId) const {
	if (scriptManager_ == nullptr || spawnerComponent.prefabSpawnerSpawnedActionName.empty()) {
		return;
	}

	const int32_t actionTargetGameObjectId = spawnerComponent.prefabSpawnerActionTargetGameObjectId >= 0
		? spawnerComponent.prefabSpawnerActionTargetGameObjectId
		: spawnerGameObject.id;
	EditorScriptActionPayload payload{};
	payload.type = EditorScriptActionPayloadTypeGameObject;
	payload.gameObjectId = spawnedGameObjectId;
	scriptManager_->QueueActionPayload(
		actionTargetGameObjectId,
		spawnerComponent.prefabSpawnerSpawnedActionName,
		payload);
}
