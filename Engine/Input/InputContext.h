#pragma once

#include "InputPhase.h"

#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// コールバックへ渡す入力結果
//============================================================

struct InputContext {
	std::string actionPath;  // Player/Submit のような ActionMap/Action 名
	InputPhase phase = InputPhase::Started;  // このフレームで発生した入力段階
	float value = 0.0f;  // Button 入力では 0.0f または 1.0f を入れる
	std::string bindingPath;  // Keyboard/Space のような実入力 Path
};

#pragma warning(pop)
