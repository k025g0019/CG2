#pragma once

#include "InputBinding.h"
#include "InputContext.h"
#include "KeyboardState.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

using InputBindingHandle = uint64_t;  // 各コールバック登録を一意に識別するハンドル
using InputCallback = std::function<void(const InputContext&)>;  // Action 発火時に呼ぶ関数型

//============================================================
// 1 つの Action とそのコールバック群
//============================================================

class InputAction {
public:
	InputAction() = default;
	InputAction(const std::string& actionName, const std::string& actionType);

	InputBindingHandle AddCallback(InputPhase phase, InputBindingHandle handle, InputCallback callback);  // phase ごとのコールバックを追加する
	bool RemoveCallback(InputBindingHandle handle);  // 登録済みコールバックをハンドルで削除する
	void ResetRuntimeState();  // Enable / Disable 切替時に押下履歴を初期化する
	void Update(const std::string& actionMapName, const KeyboardState& keyboardState);  // 現在のキーボード状態から phase 判定してコールバックを呼ぶ

	std::string name;  // Action 名。例: Submit
	std::string type;  // 現時点では "Button" のみ使う
	std::vector<InputBinding> bindings;  // この Action を発火できる Binding 一覧
	bool previousPressed = false;  // 前フレームの Action 押下状態
	bool currentPressed = false;  // 今フレームの Action 押下状態

private:
	struct CallbackEntry {
		InputBindingHandle handle = 0;  // Unbind 時に消す対象を見分けるハンドル
		InputCallback callback;  // 実際に呼ぶ関数
	};

	void DispatchCallbacks(
		InputPhase phase,
		const std::string& actionPath,
		float value,
		const std::string& bindingPath);  // 対象 phase のコールバックを安全に列挙する
	const std::vector<CallbackEntry>& GetCallbacks(InputPhase phase) const;  // phase 別配列を読み取り専用で返す
	std::vector<CallbackEntry>& GetCallbacks(InputPhase phase);  // phase 別配列を編集用で返す
	bool HasCallback(InputPhase phase, InputBindingHandle handle) const;  // Dispatch 中に削除済みかを判定する

	std::vector<CallbackEntry> startedCallbacks_;  // Started 用コールバック一覧
	std::vector<CallbackEntry> performedCallbacks_;  // Performed 用コールバック一覧
	std::vector<CallbackEntry> canceledCallbacks_;  // Canceled 用コールバック一覧
	std::string previousBindingPath_;  // 前フレーム押されていた Binding Path
	std::string currentBindingPath_;  // 今フレーム押されている Binding Path
};

#pragma warning(pop)
