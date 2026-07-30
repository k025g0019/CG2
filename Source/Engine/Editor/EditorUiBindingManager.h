#pragma once

#include "EditorScene.h"

class EditorRailMovementManager;

class EditorUiBindingManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorRailMovementManager* railMovementManager);  // UIと汎用値Sourceを接続する。
	void Update();  // Play中の値をTextとSliderへ反映する。

private:
	EditorScene* editorScene_ = nullptr;
	EditorRailMovementManager* railMovementManager_ = nullptr;

	bool ReadValue(
		const EditorGameObject& ownerGameObject,
		const EditorComponent& component,
		float& value) const;  // Health、Rail、Activeのいずれかを数値化する。
};
