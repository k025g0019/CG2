#include "KeyboardState.h"

#include <cctype>

namespace {
	std::string ToUpperAscii(const std::string& text) {
		std::string upperText = text;
		for (char& character : upperText) {
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}

		return upperText;
	}
}

void KeyboardState::SetKeyState(const std::string& keyPath, bool isPressed) {
	if (keyPath.empty()) {
		return;
	}

	keyStates_[keyPath] = isPressed;
}

bool KeyboardState::IsPressed(const std::string& keyPath) const {
	const auto keyStateIterator = keyStates_.find(keyPath);
	if (keyStateIterator == keyStates_.end()) {
		return false;
	}

	return keyStateIterator->second;
}

void KeyboardState::Clear() {
	keyStates_.clear();
}

std::string KeyboardState::ToBindingPath(const std::string& keyName) {
	if (keyName.empty()) {
		return "";
	}

	if (keyName.rfind("Keyboard/", 0) == 0) {
		return keyName;
	}

	const std::string upperKeyName = ToUpperAscii(keyName);
	if (upperKeyName == "SPACE") {
		return "Keyboard/Space";
	}

	if (upperKeyName == "ENTER") {
		return "Keyboard/Enter";
	}

	if (upperKeyName == "ESCAPE") {
		return "Keyboard/Escape";
	}

	if (upperKeyName == "LEFT") {
		return "Keyboard/Left";
	}

	if (upperKeyName == "RIGHT") {
		return "Keyboard/Right";
	}

	if (upperKeyName == "UP") {
		return "Keyboard/Up";
	}

	if (upperKeyName == "DOWN") {
		return "Keyboard/Down";
	}

	if (upperKeyName.size() == 1) {
		const char character = upperKeyName[0];
		const bool isAlphabet = character >= 'A' && character <= 'Z';
		const bool isNumber = character >= '0' && character <= '9';
		if (isAlphabet || isNumber) {
			return "Keyboard/" + std::string(1, character);
		}
	}

	return "Keyboard/" + keyName;
}
