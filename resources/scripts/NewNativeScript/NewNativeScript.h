#pragma once

#include "EditorScriptApi.h"

//================================================================
// NewNativeScript のユーザー編集用データ
//================================================================

class NewNativeScript {
public:
	float moveSpeed = 3.0f;  // Update で移動量を決める基本速度。
	float rotateSpeed = 1.0f;  // Update で回転量を決める基本速度。
	bool isStarted = false;  // Start が呼ばれたかどうかの状態。
};
