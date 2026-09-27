#pragma once

#include "EditorNativeScript.h"

//================================================================
// TestBlast - 必要なゲーム処理だけを追加する C++ Script
//================================================================

class TestBlast final : public Script {
public:
	TestBlast();  // 必要な処理だけ .cpp 側で Bind する。
};
