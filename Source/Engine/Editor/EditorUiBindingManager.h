#pragma once

#include "EditorScene.h"

#include <string>

class EditorRailMovementManager;
class EditorWeaponLoadoutManager;

class EditorUiBindingManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager,
		EditorWeaponLoadoutManager* weaponLoadoutManager);  // UIと汎用値Sourceを接続する。
	void Update();  // Play中の値をTextとSliderへ反映する。

private:
	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;
	EditorWeaponLoadoutManager* weaponLoadoutManager_ = nullptr;

	bool ReadValue(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		float& value) const;  // Health、Rail、Activeのいずれかを数値化する。
	bool ReadTextValue(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		std::string& value) const;  // 選択中の武器名など文字列Sourceを取得する。
};
