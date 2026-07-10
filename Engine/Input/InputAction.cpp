#include "InputAction.h"

#include <algorithm>
#include <utility>

InputAction::InputAction(const std::string& actionName, const std::string& actionType)
	: name(actionName)
	, type(actionType) {
}

InputBindingHandle InputAction::AddCallback(InputPhase phase, InputBindingHandle handle, InputCallback callback) {
	if (!callback) {
		return 0;
	}

	CallbackEntry callbackEntry{};
	callbackEntry.handle = handle;
	callbackEntry.callback = std::move(callback);
	GetCallbacks(phase).push_back(std::move(callbackEntry));
	return handle;
}

bool InputAction::RemoveCallback(InputBindingHandle handle) {
	bool isRemoved = false;
	auto eraseByHandle = [&handle, &isRemoved](std::vector<CallbackEntry>& callbackEntries) {
		const auto removeBegin = std::remove_if(
			callbackEntries.begin(),
			callbackEntries.end(),
			[&handle](const CallbackEntry& callbackEntry) {
				return callbackEntry.handle == handle;
			});
		if (removeBegin != callbackEntries.end()) {
			callbackEntries.erase(removeBegin, callbackEntries.end());
			isRemoved = true;
		}
	};

	eraseByHandle(startedCallbacks_);
	eraseByHandle(performedCallbacks_);
	eraseByHandle(canceledCallbacks_);
	return isRemoved;
}

void InputAction::ResetRuntimeState() {
	previousPressed = false;
	currentPressed = false;
	previousBindingPath_.clear();
	currentBindingPath_.clear();
}

void InputAction::Update(const std::string& actionMapName, const KeyboardState& keyboardState) {
	previousPressed = currentPressed;
	previousBindingPath_ = currentBindingPath_;
	currentPressed = false;
	currentBindingPath_.clear();

	for (const InputBinding& binding : bindings) {
		if (type != "Button") {
			continue;
		}

		const bool isPressed = keyboardState.IsPressed(binding.path);
		const float value = isPressed ? 1.0f : 0.0f;
		if (value >= binding.pressPoint) {
			currentPressed = true;
			currentBindingPath_ = binding.path;
			break;
		}
	}

	const std::string actionPath = actionMapName + "/" + name;
	if (!previousPressed && currentPressed) {
		DispatchCallbacks(InputPhase::Started, actionPath, 1.0f, currentBindingPath_);
		DispatchCallbacks(InputPhase::Performed, actionPath, 1.0f, currentBindingPath_);
	}

	if (previousPressed && !currentPressed) {
		DispatchCallbacks(InputPhase::Canceled, actionPath, 0.0f, previousBindingPath_);
	}
}

void InputAction::DispatchCallbacks(
	InputPhase phase,
	const std::string& actionPath,
	float value,
	const std::string& bindingPath) {
	const std::vector<CallbackEntry> callbackSnapshot = GetCallbacks(phase);
	for (const CallbackEntry& callbackEntry : callbackSnapshot) {
		if (!HasCallback(phase, callbackEntry.handle)) {
			continue;
		}

		InputContext inputContext{};
		inputContext.actionPath = actionPath;
		inputContext.phase = phase;
		inputContext.value = value;
		inputContext.bindingPath = bindingPath;
		callbackEntry.callback(inputContext);
	}
}

const std::vector<InputAction::CallbackEntry>& InputAction::GetCallbacks(InputPhase phase) const {
	switch (phase) {
	case InputPhase::Started:
		return startedCallbacks_;
	case InputPhase::Performed:
		return performedCallbacks_;
	case InputPhase::Canceled:
	default:
		return canceledCallbacks_;
	}
}

std::vector<InputAction::CallbackEntry>& InputAction::GetCallbacks(InputPhase phase) {
	switch (phase) {
	case InputPhase::Started:
		return startedCallbacks_;
	case InputPhase::Performed:
		return performedCallbacks_;
	case InputPhase::Canceled:
	default:
		return canceledCallbacks_;
	}
}

bool InputAction::HasCallback(InputPhase phase, InputBindingHandle handle) const {
	const std::vector<CallbackEntry>& callbackEntries = GetCallbacks(phase);
	const auto callbackIterator = std::find_if(
		callbackEntries.begin(),
		callbackEntries.end(),
		[&handle](const CallbackEntry& callbackEntry) {
			return callbackEntry.handle == handle;
		});
	return callbackIterator != callbackEntries.end();
}
