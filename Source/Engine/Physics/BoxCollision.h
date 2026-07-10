#pragma once

#include "EditorScene.h"

#pragma warning(push)
#pragma warning(disable : 4820)

class BoxCollision {
public:
	void Initialize(EditorScene* editorScene);  // BoxCollider が参照する Scene を受け取る
	void Update();  // BoxCollider と床、BoxCollider 同士の接触を解決する
	void Draw();  // 現時点ではデバッグ描画なし

private:
	EditorScene* editorScene_ = nullptr;  // BoxCollider を持つ GameObject を探す対象 Scene
};

#pragma warning(pop)
