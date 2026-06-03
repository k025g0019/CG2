#pragma once

#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// 1 つの Action に紐づく入力 Path
//============================================================

struct InputBinding {
	std::string path;  // Keyboard/Space のような Binding Path
	float pressPoint = 0.5f;  // 将来のアナログ入力拡張に備えた押下しきい値
};

#pragma warning(pop)
