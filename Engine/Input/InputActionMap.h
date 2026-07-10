#pragma once

#include "InputAction.h"

#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// 同じ用途の Action をまとめる ActionMap
//============================================================

class InputActionMap {
public:
	InputActionMap() = default;
	explicit InputActionMap(const std::string& actionMapName);

	InputAction* FindAction(const std::string& actionName);  // Action 名で編集用 Action を返す
	const InputAction* FindAction(const std::string& actionName) const;  // Action 名で読み取り専用 Action を返す
	void ResetRuntimeState();  // Enable / Disable 切替時にすべての Action 押下履歴を初期化する

	std::string name;  // ActionMap 名。例: Player
	bool enabled = false;  // true の時だけ Update で Action 判定する
	std::vector<InputAction> actions;  // この ActionMap に属する Action 一覧
};

#pragma warning(pop)
