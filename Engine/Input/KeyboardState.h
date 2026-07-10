#pragma once

#include <string>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// キーボードの現在押下状態
//============================================================

class KeyboardState {
public:
	void SetKeyState(const std::string& keyPath, bool isPressed);  // Keyboard/Space のような Path ごとに現在押下状態を更新する
	bool IsPressed(const std::string& keyPath) const;  // 指定した Path が今フレーム押下中なら true を返す
	void Clear();  // すべてのキー状態を初期化する

	static std::string ToBindingPath(const std::string& keyName);  // Space などの短いキー名を Keyboard/Space に変換する

private:
	std::unordered_map<std::string, bool> keyStates_;  // 現在押下中のキー状態
};

#pragma warning(pop)
