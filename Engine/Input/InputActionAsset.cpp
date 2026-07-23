#include "InputActionAsset.h"

#include "KeyboardState.h"

#pragma warning(disable : 5045)
#pragma warning(push)
#pragma warning(disable : 4464)
#pragma warning(push, 0)
#include "../../ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"
#pragma warning(pop)
#pragma warning(pop)

#include <fstream>
#include <sstream>
#include <utility>

namespace {
	std::string ReadUtf8TextFile(const std::string& filePath) {
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open()) {
			return "";
		}

		std::ostringstream stream;
		stream << file.rdbuf();
		std::string text = stream.str();
		if (text.size() >= 3 &&
			static_cast<unsigned char>(text[0]) == 0xEFu &&
			static_cast<unsigned char>(text[1]) == 0xBBu &&
			static_cast<unsigned char>(text[2]) == 0xBFu) {
			text.erase(0, 3);
		}

		return text;
	}

	bool TryGetString(const crude_json::value& objectValue, const char* key, std::string& outputValue) {
		if (!objectValue.is_object() || !objectValue.contains(key)) {
			return false;
		}

		const crude_json::value& value = objectValue[key];
		if (!value.is_string()) {
			return false;
		}

		outputValue = value.get<crude_json::string>();
		return true;
	}

	float GetNumberOrDefault(const crude_json::value& objectValue, const char* key, float defaultValue) {
		if (!objectValue.is_object() || !objectValue.contains(key)) {
			return defaultValue;
		}

		const crude_json::value& value = objectValue[key];
		if (!value.is_number()) {
			return defaultValue;
		}

		return static_cast<float>(value.get<crude_json::number>());
	}
}

bool InputActionAsset::LoadFromJson(const std::string& filePath) {
	Clear();

	const std::string jsonText = ReadUtf8TextFile(filePath);
	if (jsonText.empty()) {
		lastError_ = "Failed to open JSON file.";
		return false;
	}

	const crude_json::value rootValue = crude_json::value::parse(jsonText);
	if (rootValue.is_discarded() || !rootValue.is_object()) {
		lastError_ = "Failed to parse JSON.";
		return false;
	}

	if (!rootValue.contains("actionMaps") || !rootValue["actionMaps"].is_array()) {
		lastError_ = "actionMaps array is missing.";
		return false;
	}

	for (const crude_json::value& actionMapValue : rootValue["actionMaps"].get<crude_json::array>()) {
		if (!actionMapValue.is_object()) {
			continue;
		}

		std::string actionMapName;
		if (!TryGetString(actionMapValue, "name", actionMapName)) {
			lastError_ = "Failed to read action map name.";
			return false;
		}

		InputActionMap actionMap(actionMapName);
		if (!actionMapValue.contains("actions") || !actionMapValue["actions"].is_array()) {
			lastError_ = "actions array is missing.";
			return false;
		}

		for (const crude_json::value& actionValue : actionMapValue["actions"].get<crude_json::array>()) {
			if (!actionValue.is_object()) {
				continue;
			}

			std::string actionName;
			std::string actionType;
			if (!TryGetString(actionValue, "name", actionName) ||
				!TryGetString(actionValue, "type", actionType)) {
				lastError_ = "Failed to read action name or type.";
				return false;
			}

			if (actionType != "Button") {
				lastError_ = "Only Button actions are supported right now.";
				return false;
			}

			InputAction action(actionName, actionType);
			if (!actionValue.contains("bindings") || !actionValue["bindings"].is_array()) {
				lastError_ = "bindings array is missing.";
				return false;
			}

			for (const crude_json::value& bindingValue : actionValue["bindings"].get<crude_json::array>()) {
				if (!bindingValue.is_object()) {
					continue;
				}

				InputBinding binding{};
				if (!TryGetString(bindingValue, "path", binding.path)) {
					lastError_ = "Failed to read binding path.";
					return false;
				}

				binding.path = KeyboardState::ToBindingPath(binding.path);
				binding.pressPoint = GetNumberOrDefault(bindingValue, "pressPoint", 0.5f);
				action.bindings.push_back(binding);
			}

			actionMap.actions.push_back(std::move(action));
		}

		actionMaps.push_back(std::move(actionMap));
	}

	return true;
}

InputAction* InputActionAsset::FindAction(const std::string& actionPath) {
	const size_t delimiterPosition = actionPath.find('/');
	if (delimiterPosition == std::string::npos) {
		return nullptr;
	}

	const std::string actionMapName = actionPath.substr(0, delimiterPosition);
	const std::string actionName = actionPath.substr(delimiterPosition + 1);
	InputActionMap* actionMap = FindActionMap(actionMapName);
	if (actionMap == nullptr) {
		return nullptr;
	}

	return actionMap->FindAction(actionName);
}

const InputAction* InputActionAsset::FindAction(const std::string& actionPath) const {
	const size_t delimiterPosition = actionPath.find('/');
	if (delimiterPosition == std::string::npos) {
		return nullptr;
	}

	const std::string actionMapName = actionPath.substr(0, delimiterPosition);
	const std::string actionName = actionPath.substr(delimiterPosition + 1);
	const InputActionMap* actionMap = FindActionMap(actionMapName);
	if (actionMap == nullptr) {
		return nullptr;
	}

	return actionMap->FindAction(actionName);
}

InputActionMap* InputActionAsset::FindActionMap(const std::string& actionMapName) {
	for (InputActionMap& actionMap : actionMaps) {
		if (actionMap.name == actionMapName) {
			return &actionMap;
		}
	}

	return nullptr;
}

const InputActionMap* InputActionAsset::FindActionMap(const std::string& actionMapName) const {
	for (const InputActionMap& actionMap : actionMaps) {
		if (actionMap.name == actionMapName) {
			return &actionMap;
		}
	}

	return nullptr;
}

const std::string& InputActionAsset::GetLastError() const {
	return lastError_;
}

void InputActionAsset::Clear() {
	actionMaps.clear();
	lastError_.clear();
}
