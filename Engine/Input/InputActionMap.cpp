#include "InputActionMap.h"

InputActionMap::InputActionMap(const std::string& actionMapName)
	: name(actionMapName) {
}

InputAction* InputActionMap::FindAction(const std::string& actionName) {
	for (InputAction& action : actions) {
		if (action.name == actionName) {
			return &action;
		}
	}

	return nullptr;
}

const InputAction* InputActionMap::FindAction(const std::string& actionName) const {
	for (const InputAction& action : actions) {
		if (action.name == actionName) {
			return &action;
		}
	}

	return nullptr;
}

void InputActionMap::ResetRuntimeState() {
	for (InputAction& action : actions) {
		action.ResetRuntimeState();
	}
}
