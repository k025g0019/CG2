#pragma once

#include "EditorScene.h"

#pragma warning(push)
#pragma warning(disable : 4820)

class SphereCollision {
public:
	void Initialize(EditorScene* editorScene);  // SphereCollider が参照する Scene を受け取る
	void Update();  // SphereCollider と床の接触を解決する
	void Draw();  // 現時点ではデバッグ描画なし

private:
	EditorScene* editorScene_ = nullptr;  // SphereCollider を持つ GameObject を探す対象 Scene
};

#pragma warning(pop)
