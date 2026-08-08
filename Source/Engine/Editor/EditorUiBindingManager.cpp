#include "EditorUiBindingManager.h"

#include "EditorComponentUtility.h"
#include "EditorRailMovementManager.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

void EditorUiBindingManager::Initialize(
	EditorScene* editorScene,
	EditorRailMovementManager* railMovementManager) {
	editorScene_ = editorScene;
	railMovementManager_ = railMovementManager;
}

void EditorUiBindingManager::Update() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (EditorGameObject& uiGameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* bindingComponent = EditorComponentUtility::FindComponent(
			uiGameObject,
			EditorComponentType::UIValueBinding);

		if (!uiGameObject.isActive || bindingComponent == nullptr || !bindingComponent->isActive) {
			continue;
		}

		float sourceValue = 0.0f;

		if (!ReadValue(uiGameObject, *bindingComponent, sourceValue)) {
			continue;
		}

		EditorComponent* textComponent = EditorComponentUtility::FindComponent(
			uiGameObject,
			EditorComponentType::TextMeshProUGUI);

		if (textComponent == nullptr) {
			textComponent = EditorComponentUtility::FindComponent(
				uiGameObject,
				EditorComponentType::Text);
		}

		if (textComponent != nullptr && textComponent->isActive) {
			std::ostringstream textStream;
			textStream << bindingComponent->uiBindingPrefix;
			textStream << std::fixed;
			textStream << std::setprecision((std::clamp)(bindingComponent->uiBindingPrecision, 0, 6));
			textStream << sourceValue * bindingComponent->uiBindingScale;
			textComponent->buttonLabel = textStream.str();
		}

		EditorComponent* sliderComponent = EditorComponentUtility::FindComponent(
			uiGameObject,
			EditorComponentType::Slider);

		if (sliderComponent != nullptr && sliderComponent->isActive) {
			const float minimumValue = (std::min)(sliderComponent->sliderMinValue, sliderComponent->sliderMaxValue);
			const float maximumValue = (std::max)(sliderComponent->sliderMinValue, sliderComponent->sliderMaxValue);
			sliderComponent->sliderValue = (std::clamp)(sourceValue, minimumValue, maximumValue);
		}
	}
}

bool EditorUiBindingManager::ReadValue(
	const EditorGameObject& ownerGameObject,
	const EditorComponent& component,
	float& value) const {
	const int32_t sourceGameObjectId = component.uiBindingSourceGameObjectId >= 0
		? component.uiBindingSourceGameObjectId
		: ownerGameObject.id;
	const EditorGameObject* sourceGameObject = editorScene_->FindGameObject(sourceGameObjectId);

	if (sourceGameObject == nullptr) {
		return false;
	}

	if (component.uiBindingValueType == 0 || component.uiBindingValueType == 1) {
		const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::Health);

		if (healthComponent == nullptr) {
			return false;
		}

		if (component.uiBindingValueType == 0) {
			value = healthComponent->healthCurrent;
			return true;
		}

		if (healthComponent->healthMaximum <= 0.0f) {
			return false;
		}

		value = (std::clamp)(
			healthComponent->healthCurrent / healthComponent->healthMaximum,
			0.0f,
			1.0f);
		return true;
	}

	if (component.uiBindingValueType == 2 && railMovementManager_ != nullptr) {
		return railMovementManager_->GetNormalizedProgress(sourceGameObjectId, value);
	}

	if (component.uiBindingValueType == 3) {
		value = sourceGameObject->isActive ? 1.0f : 0.0f;
		return true;
	}

	if (component.uiBindingValueType == 4) {
		const EditorComponent* counterComponent = EditorComponentUtility::FindComponent(
			*sourceGameObject,
			EditorComponentType::GenericCounter);

		if (counterComponent == nullptr || !counterComponent->isActive) {
			return false;
		}

		value = counterComponent->counterCurrentValue;
		return true;
	}

	return false;
}
