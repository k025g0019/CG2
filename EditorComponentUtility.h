#pragma once

#include "EditorScene.h"

class EditorComponentUtility {
public:
	static EditorComponent* FindComponent(EditorGameObject& gameObject, EditorComponentType type);  // 編集可能な GameObject から指定種類の Component を探す
	static const EditorComponent* FindComponent(const EditorGameObject& gameObject, EditorComponentType type);  // 読み取り専用 GameObject から指定種類の Component を探す
};
