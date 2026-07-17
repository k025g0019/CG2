#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorInputManager {
public:
	struct ActionVector2State {
		float x = 0.0f;  // Move などの 2D 入力値 X。
		float y = 0.0f;  // Move などの 2D 入力値 Y。
		bool isActive = false;  // その Action がこのフレーム有効なら true。
		std::string bindingPath;  // Action を発生させた Composite Binding の説明。
	};

	struct ActionButtonState {
		bool isPressed = false;  // このフレーム押されているなら true。
		bool wasJustPressed = false;  // このフレーム押した瞬間なら true。
		bool wasJustReleased = false;  // このフレーム離した瞬間なら true。
		bool isActive = false;  // その Action がこのフレーム有効なら true。
		std::string bindingPath;  // Keyboard/Space など実際の Binding Path。
	};

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // Input / PlayerInput Component を持つ GameObject を動かす Scene と Console を受け取る
	void Update(const uint8_t* keyState, float deltaTime);  // キー状態から移動方向とジャンプを反映する
	void Draw();  // 現時点では入力のデバッグ描画なし
	bool TryGetActionVector2(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName, float& x, float& y) const;  // DLL Script から Move などの 2D 値を取得する。
	bool IsActionPressed(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName) const;  // DLL Script から Button Action の押下中判定を取得する。
	bool WasActionJustPressed(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName) const;  // DLL Script から Button Action の押した瞬間判定を取得する。
	bool WasActionJustReleased(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName) const;  // DLL Script から Button Action を離した瞬間判定を取得する。
	std::string GetActionBindingPath(int32_t gameObjectId, const std::string& actionMapName, const std::string& actionName) const;  // InputContext へ渡す Binding Path を返す。

private:
	EditorScene* editorScene_ = nullptr;  // Input Component を検索する対象 Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // Fire などの入力イベントを Console に流す出力先
	std::unordered_map<std::string, bool> actionPressedStates_;  // started / performed / canceled 判定用の前フレーム押下状態
	std::unordered_map<std::string, ActionVector2State> actionVector2States_;  // PlayerInput の Move など 2D 入力値をフレームごとに保持する。
	std::unordered_map<std::string, ActionButtonState> actionButtonStates_;  // PlayerInput の Submit / LeftClick など Button 入力状態をフレームごとに保持する。
	bool IsKeyPressed(const uint8_t* keyState, int32_t keyIndex) const;  // DirectInput のキー配列から 1 つのキーが押されているか調べる
	float GetColliderBottomOffset(const EditorGameObject& gameObject) const;  // 地面判定に使う Collider の下端オフセットを返す
};

#pragma warning(pop)
