#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include "EditorBottomPanelWindowManager.h"
#include "EditorAnimationWindowManager.h"
#include "EditorDockingManager.h"
#include "EditorFrameInputManager.h"
#include "EditorGameViewManager.h"
#include "EditorHierarchyWindowManager.h"
#include "EditorImguiFrameManager.h"
#include "EditorInspectorWindowManager.h"
#include "EditorMainMenuManager.h"
#include "EditorPlatformManager.h"
#include "EditorRenderManager.h"
#include "EditorSceneLifecycleManager.h"
#include "EditorSceneViewManager.h"

#pragma warning(push)
#pragma warning(disable : 4820)

class GameScene {
public:
	//================================================================
	// WinMain から呼ばれる Scene 管理の入口
	//================================================================

	void Initialize(_In_ HINSTANCE instanceHandle);  // アプリ実体ハンドルを使って、OS / DirectX / Editor の初期化順を決める。
	void Update();  // 1フレーム分の入力、Scene 同期、UI フレーム開始を順番に進める。
	void Draw();  // UI 構築と DirectX12 描画コマンド発行を順番に行う。
	int Finalize();  // OS / GPU / ImGui / 音声の終了処理をまとめて実行し、終了コードを返す。
	bool IsEndRequested() const;  // WinMain の while 条件で使う終了要求フラグを返す。

private:
	EditorPlatformManager platformManager_;  // Win32 ウィンドウ、DirectX12、DirectInput、XAudio2 の土台を扱う Manager。
	EditorSceneLifecycleManager sceneLifecycleManager_;  // EditorScene、Play Runtime、選択同期、SceneObject 同期を扱う Manager。
	EditorFrameInputManager frameInputManager_;  // キーボード状態、カメラ操作、ウィンドウサイズ更新を扱う Manager。
	EditorImguiFrameManager imguiFrameManager_;  // ImGui / ImGuizmo のフレーム開始と DrawData 確定を扱う Manager。
	EditorMainMenuManager mainMenuManager_;  // 上部メニューと Play / Stop 操作を扱う Manager。
	EditorDockingManager dockingManager_;  // Docking レイアウトのホストウィンドウと初期配置を扱う Manager。
	EditorSceneViewManager sceneViewManager_;  // Scene タブ、グリッド、ギズモ、ドラッグ配置を扱う Manager。
	EditorGameViewManager gameViewManager_;  // GameView ウィンドウと Camera Component 出力を扱う Manager。
	EditorHierarchyWindowManager hierarchyWindowManager_;  // Hierarchy ウィンドウと GameObject ツリー操作を扱う Manager。
	EditorInspectorWindowManager inspectorWindowManager_;  // Inspector ウィンドウと Component 編集を扱う Manager。
	EditorBottomPanelWindowManager bottomPanelWindowManager_;  // Project / Console の下部パネル表示を扱う Manager。
	EditorAnimationWindowManager animationWindowManager_;  // Timeline、Keyframe、Preview、Animation Event の編集を扱う Manager。
	EditorRenderManager renderManager_;  // SceneObject / Sprite / ImGui を GPU に描画する Manager。
};

#pragma warning(pop)
