#pragma once

#include "InputActionAsset.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// Input system updated from the game loop
//============================================================

class InputSystem {
public:
	bool LoadActionAsset(const std::string& filePath);  // Load one action asset from JSON and clear old callback handles
	bool EnableActionMap(const std::string& actionMapName);  // Enable one action map
	bool DisableActionMap(const std::string& actionMapName);  // Disable one action map
	InputBindingHandle Bind(const std::string& actionPath, InputPhase phase, InputCallback callback);  // Register one callback on Player/Submit style path
	bool Unbind(InputBindingHandle handle);  // Remove one registered callback
	void Update();  // Evaluate action phases and invoke callbacks
	void SetKeyState(const std::string& keyPath, bool isPressed);  // Feed the current key state from the platform layer
	void Clear();  // Reset asset, key state, and callback registrations

	InputActionAsset& GetActionAsset();  // Access the loaded action asset
	const InputActionAsset& GetActionAsset() const;  // Read-only version

private:
	struct BoundActionInfo {
		std::string actionPath;  // Action path used to find the target action on Unbind
		InputPhase phase = InputPhase::Started;  // Registered phase
	};

	InputActionAsset actionAsset_;  // Loaded action asset
	KeyboardState keyboardState_;  // Current keyboard state
	InputBindingHandle nextBindingHandle_ = 1;  // 0 is reserved as an invalid handle
	std::unordered_map<InputBindingHandle, BoundActionInfo> boundActionInfos_;  // Handle to action lookup table
};

namespace Engine {
	InputSystem& GetInputSystem();  // Shared input system instance
}

#pragma warning(pop)
