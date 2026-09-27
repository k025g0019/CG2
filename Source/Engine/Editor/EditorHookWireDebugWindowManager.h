#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Hook / Wire デバッグ表示
//================================================================

// Wireパズルでは「どの物体のどこにHookを置いたか」がそのままレベルデザインになるため、
// 「Wireがおかしい」の実態がHookの設定ミス（力を伝えるRigidbody違い、Anchorずれ、Collider不足）
// であることが多い。それをScene編集中に即座に見つけるための検査Window。
class EditorHookWireDebugWindowManager {
public:
	void Initialize();
	void Update();
	void Draw();

private:
	void DrawHookList();  // Scene内のHookPoint構成と設定不備を一覧する。
	void DrawWireList();  // Play中のRuntime Wireを長さ・張力つきで一覧する。
};

#pragma warning(pop)
