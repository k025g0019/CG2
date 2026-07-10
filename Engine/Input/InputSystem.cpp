#include "InputSystem.h"

#include <utility>

namespace {
	InputSystem g_inputSystem;  // アプリ全体で共有する入力システム
}

bool InputSystem::LoadActionAsset(const std::string& filePath) {
	InputActionAsset loadedAsset;
	if (!loadedAsset.LoadFromJson(filePath)) {
		return false;
	}

	actionAsset_ = std::move(loadedAsset);
	boundActionInfos_.clear();
	nextBindingHandle_ = 1;
	return true;
}

bool InputSystem::EnableActionMap(const std::string& actionMapName) {
	InputActionMap* actionMap = actionAsset_.FindActionMap(actionMapName);
	if (actionMap == nullptr) {
		return false;
	}

	actionMap->enabled = true;
	actionMap->ResetRuntimeState();
	return true;
}

bool InputSystem::DisableActionMap(const std::string& actionMapName) {
	InputActionMap* actionMap = actionAsset_.FindActionMap(actionMapName);
	if (actionMap == nullptr) {
		return false;
	}

	actionMap->enabled = false;
	actionMap->ResetRuntimeState();
	return true;
}

InputBindingHandle InputSystem::Bind(const std::string& actionPath, InputPhase phase, InputCallback callback) {
	InputAction* action = actionAsset_.FindAction(actionPath);
	if (action == nullptr || !callback) {
		return 0;
	}

	const InputBindingHandle handle = nextBindingHandle_;
	nextBindingHandle_ += 1;
	if (action->AddCallback(phase, handle, std::move(callback)) == 0) {
		return 0;
	}

	BoundActionInfo boundActionInfo{};
	boundActionInfo.actionPath = actionPath;
	boundActionInfo.phase = phase;
	boundActionInfos_[handle] = std::move(boundActionInfo);
	return handle;
}

bool InputSystem::Unbind(InputBindingHandle handle) {
	const auto handleIterator = boundActionInfos_.find(handle);
	if (handleIterator == boundActionInfos_.end()) {
		return false;
	}

	InputAction* action = actionAsset_.FindAction(handleIterator->second.actionPath);
	const bool isRemoved = action != nullptr && action->RemoveCallback(handle);
	boundActionInfos_.erase(handleIterator);
	return isRemoved;
}

void InputSystem::Update() {
	for (InputActionMap& actionMap : actionAsset_.actionMaps) {
		if (!actionMap.enabled) {
			continue;
		}

		for (InputAction& action : actionMap.actions) {
			action.Update(actionMap.name, keyboardState_);
		}
	}
}

void InputSystem::SetKeyState(const std::string& keyPath, bool isPressed) {
	keyboardState_.SetKeyState(keyPath, isPressed);
}

void InputSystem::Clear() {
	actionAsset_.Clear();
	keyboardState_.Clear();
	boundActionInfos_.clear();
	nextBindingHandle_ = 1;
}

InputActionAsset& InputSystem::GetActionAsset() {
	return actionAsset_;
}

const InputActionAsset& InputSystem::GetActionAsset() const {
	return actionAsset_;
}

namespace Engine {
	InputSystem& GetInputSystem() {
		return g_inputSystem;
	}
}
