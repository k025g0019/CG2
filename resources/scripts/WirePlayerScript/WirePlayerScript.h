#pragma once

#include "EditorNativeScript.h"

//================================================================
// WirePlayerScript - 必要なゲーム処理だけを追加する C++ Script
//================================================================

class WirePlayerScript final : public Script {
public:
	void Start() override;
	void Update(float deltaTime) override;
	void Stop() override;
};
