#pragma once

#include "EditorNativeScript.h"

//================================================================
// WirePuzzlePlayer - 必要なゲーム処理だけを追加する C++ Script
//================================================================

class WirePuzzlePlayer final : public Script {
public:

	void Update(float deltaTime) override;

};