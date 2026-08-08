#include "EditorWeaponLoadoutManager.h"

#include "EditorComponentUtility.h"
#include "EditorScriptManager.h"
#include "EditorWeaponManager.h"

#include <algorithm>
#include <limits>

void EditorWeaponLoadoutManager::Initialize(
	EditorScene* editorScene,
	EditorWeaponManager* weaponManager,
	EditorScriptManager* scriptManager) {
	editorScene_ = editorScene;
	weaponManager_ = weaponManager;
	scriptManager_ = scriptManager;
}

void EditorWeaponLoadoutManager::Start() {
	loadoutRuntimes_.clear();

	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		EditorComponent* loadoutComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::WeaponLoadout);

		if (!gameObject.isActive || loadoutComponent == nullptr || !loadoutComponent->isActive) {
			continue;
		}

		LoadoutRuntime runtime{};

		for (const int32_t childGameObjectId : gameObject.children) {
			if (FindSlotComponent(childGameObjectId) != nullptr) {
				runtime.slotGameObjectIds.push_back(childGameObjectId);
			}
		}

		if (!runtime.slotGameObjectIds.empty()) {
			loadoutComponent->weaponLoadoutSelectedSlotIndex = (std::clamp)(
				loadoutComponent->weaponLoadoutSelectedSlotIndex,
				0,
				static_cast<int32_t>(runtime.slotGameObjectIds.size()) - 1);
		}

		loadoutRuntimes_.emplace(gameObject.id, std::move(runtime));
		UpdateVisuals(gameObject.id);
	}
}

void EditorWeaponLoadoutManager::Update(float deltaTime) {
	if (deltaTime <= 0.0f) {
		return;
	}

	for (auto& [loadoutGameObjectId, runtime] : loadoutRuntimes_) {
		if (runtime.reloadingSlotGameObjectId < 0) {
			EditorComponent* selectedSlot = FindSelectedSlot(loadoutGameObjectId);

			if (selectedSlot != nullptr && selectedSlot->weaponSlotAutoReload &&
				selectedSlot->weaponSlotCurrentAmmo <= 0 &&
				selectedSlot->weaponSlotReserveAmmo != 0) {
				Reload(loadoutGameObjectId);
			}

			continue;
		}

		runtime.reloadRemainingSeconds -= deltaTime;

		if (runtime.reloadRemainingSeconds > 0.0f) {
			continue;
		}

		EditorComponent* slotComponent = FindSlotComponent(runtime.reloadingSlotGameObjectId);

		if (slotComponent != nullptr) {
			const int32_t missingAmmo = (std::max)(
				slotComponent->weaponSlotMaximumAmmo - slotComponent->weaponSlotCurrentAmmo,
				0);
			const int32_t loadedAmmo = slotComponent->weaponSlotReserveAmmo < 0
				? missingAmmo
				: (std::min)(missingAmmo, slotComponent->weaponSlotReserveAmmo);
			slotComponent->weaponSlotCurrentAmmo += loadedAmmo;

			if (slotComponent->weaponSlotReserveAmmo >= 0) {
				slotComponent->weaponSlotReserveAmmo -= loadedAmmo;
			}
		}

		runtime.reloadingSlotGameObjectId = -1;
		runtime.reloadRemainingSeconds = 0.0f;
		EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

		if (loadoutComponent != nullptr) {
			QueueLoadoutAction(loadoutGameObjectId, loadoutComponent->weaponLoadoutReloadedActionName);
		}
	}
}

void EditorWeaponLoadoutManager::Stop() {
	loadoutRuntimes_.clear();
}

bool EditorWeaponLoadoutManager::SelectSlot(int32_t loadoutGameObjectId, int32_t slotIndex) {
	auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || loadoutComponent == nullptr ||
		slotIndex < 0 || slotIndex >= static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size())) {
		return false;
	}

	if (loadoutComponent->weaponLoadoutSelectedSlotIndex == slotIndex) {
		return true;
	}

	loadoutComponent->weaponLoadoutSelectedSlotIndex = slotIndex;
	runtimeIterator->second.reloadingSlotGameObjectId = -1;
	runtimeIterator->second.reloadRemainingSeconds = 0.0f;
	UpdateVisuals(loadoutGameObjectId);
	QueueLoadoutAction(loadoutGameObjectId, loadoutComponent->weaponLoadoutChangedActionName);
	return true;
}

bool EditorWeaponLoadoutManager::SelectNext(int32_t loadoutGameObjectId) {
	const auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || loadoutComponent == nullptr ||
		runtimeIterator->second.slotGameObjectIds.empty()) {
		return false;
	}

	const int32_t slotCount = static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size());
	return SelectSlot(loadoutGameObjectId, (loadoutComponent->weaponLoadoutSelectedSlotIndex + 1) % slotCount);
}

bool EditorWeaponLoadoutManager::SelectPrevious(int32_t loadoutGameObjectId) {
	const auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || loadoutComponent == nullptr ||
		runtimeIterator->second.slotGameObjectIds.empty()) {
		return false;
	}

	const int32_t slotCount = static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size());
	const int32_t previousSlot =
		(loadoutComponent->weaponLoadoutSelectedSlotIndex + slotCount - 1) % slotCount;
	return SelectSlot(loadoutGameObjectId, previousSlot);
}

bool EditorWeaponLoadoutManager::FireSelected(int32_t loadoutGameObjectId) {
	EditorComponent* slotComponent = FindSelectedSlot(loadoutGameObjectId);

	if (slotComponent == nullptr || weaponManager_ == nullptr ||
		slotComponent->weaponSlotCurrentAmmo <= 0) {
		return false;
	}

	const EditorGameObject* weaponGameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(slotComponent->weaponSlotWeaponGameObjectId)
		: nullptr;

	if (weaponGameObject == nullptr || !weaponGameObject->isActive) {
		return false;
	}

	bool wasFired = false;

	if (EditorComponentUtility::FindComponent(*weaponGameObject, EditorComponentType::ProjectileEmitter) != nullptr) {
		wasFired = weaponManager_->FireProjectile(weaponGameObject->id);
	}
	else if (EditorComponentUtility::FindComponent(*weaponGameObject, EditorComponentType::HitscanWeapon) != nullptr) {
		wasFired = weaponManager_->FireHitscan(weaponGameObject->id);
	}

	if (wasFired) {
		slotComponent->weaponSlotCurrentAmmo--;
	}

	return wasFired;
}

bool EditorWeaponLoadoutManager::Reload(int32_t loadoutGameObjectId) {
	auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	EditorComponent* slotComponent = FindSelectedSlot(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || slotComponent == nullptr ||
		runtimeIterator->second.reloadingSlotGameObjectId >= 0 ||
		slotComponent->weaponSlotCurrentAmmo >= slotComponent->weaponSlotMaximumAmmo ||
		slotComponent->weaponSlotReserveAmmo == 0) {
		return false;
	}

	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);
	const int32_t selectedSlotIndex = loadoutComponent != nullptr
		? loadoutComponent->weaponLoadoutSelectedSlotIndex
		: -1;

	if (selectedSlotIndex < 0 ||
		selectedSlotIndex >= static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size())) {
		return false;
	}

	runtimeIterator->second.reloadingSlotGameObjectId =
		runtimeIterator->second.slotGameObjectIds[static_cast<size_t>(selectedSlotIndex)];
	runtimeIterator->second.reloadRemainingSeconds =
		(std::max)(slotComponent->weaponSlotReloadSeconds, 0.0f);
	return true;
}

bool EditorWeaponLoadoutManager::GetSelectedWeapon(
	int32_t loadoutGameObjectId,
	int32_t& weaponGameObjectId) const {
	const EditorComponent* slotComponent = FindSelectedSlot(loadoutGameObjectId);

	if (slotComponent == nullptr || slotComponent->weaponSlotWeaponGameObjectId < 0) {
		return false;
	}

	weaponGameObjectId = slotComponent->weaponSlotWeaponGameObjectId;
	return true;
}

bool EditorWeaponLoadoutManager::GetAmmo(
	int32_t loadoutGameObjectId,
	int32_t& currentAmmo,
	int32_t& reserveAmmo) const {
	const EditorComponent* slotComponent = FindSelectedSlot(loadoutGameObjectId);

	if (slotComponent == nullptr) {
		return false;
	}

	currentAmmo = slotComponent->weaponSlotCurrentAmmo;
	reserveAmmo = slotComponent->weaponSlotReserveAmmo;
	return true;
}

bool EditorWeaponLoadoutManager::GetAmmoAtSlot(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t& currentAmmo,
	int32_t& reserveAmmo,
	int32_t& maximumAmmo) const {
	const EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	currentAmmo = slotComponent->weaponSlotCurrentAmmo;
	reserveAmmo = slotComponent->weaponSlotReserveAmmo;
	maximumAmmo = slotComponent->weaponSlotMaximumAmmo;
	return true;
}

bool EditorWeaponLoadoutManager::AddMagazineAmmo(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	const int64_t addedAmmo = static_cast<int64_t>(slotComponent->weaponSlotCurrentAmmo) + amount;
	slotComponent->weaponSlotCurrentAmmo = static_cast<int32_t>((std::clamp)(
		addedAmmo,
		int64_t{0},
		static_cast<int64_t>((std::max)(slotComponent->weaponSlotMaximumAmmo, 0))));
	return true;
}

bool EditorWeaponLoadoutManager::AddReserveAmmo(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	if (slotComponent->weaponSlotReserveAmmo < 0) {
		return true;
	}

	const int64_t addedAmmo = static_cast<int64_t>(slotComponent->weaponSlotReserveAmmo) + amount;
	slotComponent->weaponSlotReserveAmmo = static_cast<int32_t>((std::clamp)(
		addedAmmo,
		int64_t{0},
		static_cast<int64_t>((std::numeric_limits<int32_t>::max)())));
	return true;
}

bool EditorWeaponLoadoutManager::SetMagazineAmmo(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	slotComponent->weaponSlotCurrentAmmo = (std::clamp)(
		amount,
		0,
		(std::max)(slotComponent->weaponSlotMaximumAmmo, 0));
	return true;
}

bool EditorWeaponLoadoutManager::SetReserveAmmo(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	slotComponent->weaponSlotReserveAmmo = (std::max)(amount, -1);
	return true;
}

bool EditorWeaponLoadoutManager::SetMaximumAmmo(
	int32_t loadoutGameObjectId,
	int32_t slotIndex,
	int32_t amount) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	slotComponent->weaponSlotMaximumAmmo = (std::max)(amount, 0);
	slotComponent->weaponSlotCurrentAmmo = (std::min)(
		slotComponent->weaponSlotCurrentAmmo,
		slotComponent->weaponSlotMaximumAmmo);
	return true;
}

bool EditorWeaponLoadoutManager::RefillMagazine(
	int32_t loadoutGameObjectId,
	int32_t slotIndex) {
	EditorComponent* slotComponent = FindSlotByIndex(loadoutGameObjectId, slotIndex);

	if (slotComponent == nullptr) {
		return false;
	}

	slotComponent->weaponSlotCurrentAmmo = (std::max)(slotComponent->weaponSlotMaximumAmmo, 0);
	return true;
}

EditorComponent* EditorWeaponLoadoutManager::FindLoadoutComponent(int32_t loadoutGameObjectId) const {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(loadoutGameObjectId)
		: nullptr;
	return gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::WeaponLoadout)
		: nullptr;
}

EditorComponent* EditorWeaponLoadoutManager::FindSlotComponent(int32_t slotGameObjectId) const {
	EditorGameObject* gameObject = editorScene_ != nullptr
		? editorScene_->FindGameObject(slotGameObjectId)
		: nullptr;
	return gameObject != nullptr
		? EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::WeaponLoadoutSlot)
		: nullptr;
}

const EditorComponent* EditorWeaponLoadoutManager::FindSlotByIndex(
	int32_t loadoutGameObjectId,
	int32_t slotIndex) const {
	if (slotIndex < 0) {
		return FindSelectedSlot(loadoutGameObjectId);
	}

	const auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() ||
		slotIndex >= static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size())) {
		return nullptr;
	}

	return FindSlotComponent(
		runtimeIterator->second.slotGameObjectIds[static_cast<size_t>(slotIndex)]);
}

EditorComponent* EditorWeaponLoadoutManager::FindSlotByIndex(
	int32_t loadoutGameObjectId,
	int32_t slotIndex) {
	return const_cast<EditorComponent*>(
		static_cast<const EditorWeaponLoadoutManager*>(this)->FindSlotByIndex(
			loadoutGameObjectId,
			slotIndex));
}

const EditorComponent* EditorWeaponLoadoutManager::FindSelectedSlot(int32_t loadoutGameObjectId) const {
	const auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || loadoutComponent == nullptr ||
		loadoutComponent->weaponLoadoutSelectedSlotIndex < 0 ||
		loadoutComponent->weaponLoadoutSelectedSlotIndex >=
			static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size())) {
		return nullptr;
	}

	return FindSlotComponent(runtimeIterator->second.slotGameObjectIds[
		static_cast<size_t>(loadoutComponent->weaponLoadoutSelectedSlotIndex)]);
}

EditorComponent* EditorWeaponLoadoutManager::FindSelectedSlot(int32_t loadoutGameObjectId) {
	return const_cast<EditorComponent*>(
		static_cast<const EditorWeaponLoadoutManager*>(this)->FindSelectedSlot(loadoutGameObjectId));
}

void EditorWeaponLoadoutManager::UpdateVisuals(int32_t loadoutGameObjectId) {
	const auto runtimeIterator = loadoutRuntimes_.find(loadoutGameObjectId);
	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (runtimeIterator == loadoutRuntimes_.end() || loadoutComponent == nullptr || editorScene_ == nullptr) {
		return;
	}

	for (int32_t slotIndex = 0;
		slotIndex < static_cast<int32_t>(runtimeIterator->second.slotGameObjectIds.size());
		++slotIndex) {
		const EditorComponent* slotComponent = FindSlotComponent(
			runtimeIterator->second.slotGameObjectIds[static_cast<size_t>(slotIndex)]);

		if (slotComponent == nullptr || slotComponent->weaponSlotVisualGameObjectId < 0) {
			continue;
		}

		EditorGameObject* visualGameObject = editorScene_->FindGameObject(
			slotComponent->weaponSlotVisualGameObjectId);

		if (visualGameObject != nullptr) {
			const bool isSelected = slotIndex == loadoutComponent->weaponLoadoutSelectedSlotIndex;

			if (scriptManager_ != nullptr) {
				scriptManager_->SetGameObjectActive(visualGameObject->id, isSelected);
			}
			else {
				visualGameObject->isActive = isSelected;
			}
		}
	}
}

void EditorWeaponLoadoutManager::QueueLoadoutAction(
	int32_t loadoutGameObjectId,
	const std::string& actionName) {
	const EditorComponent* loadoutComponent = FindLoadoutComponent(loadoutGameObjectId);

	if (scriptManager_ == nullptr || loadoutComponent == nullptr || actionName.empty()) {
		return;
	}

	const int32_t actionTargetGameObjectId = loadoutComponent->weaponLoadoutActionTargetGameObjectId >= 0
		? loadoutComponent->weaponLoadoutActionTargetGameObjectId
		: loadoutGameObjectId;
	scriptManager_->QueueActionEvent(actionTargetGameObjectId, actionName);
}
