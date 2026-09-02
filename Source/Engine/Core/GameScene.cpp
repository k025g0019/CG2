#include "GameScene.h"

#include "ApplicationWindow.h"
#include "EditorSharedState.h"

using namespace EditorSharedState;

void GameScene::Initialize(_In_ HINSTANCE instanceHandle) {
	std::string gameBuildMessage;
	isStandaloneGame_ = EditorGameBuildManager::TryLoadStandaloneManifest(
		gameBuildSettings_,
		gameBuildMessage);
	hasStandaloneInitializationFailed_ =
		!isStandaloneGame_ && !gameBuildMessage.empty();

	if (!isStandaloneGame_ && !hasStandaloneInitializationFailed_) {
		// Editor の Play 中も Player と同じ Build Index で Scene を切り替えられるようにする。
		EditorGameBuildManager::LoadProjectSettings(gameBuildSettings_);
	}

	g_isStandaloneGame = isStandaloneGame_;
	g_gameBuildScenePaths = gameBuildSettings_.scenePaths;

	if (isStandaloneGame_) {
		SetStandaloneWindowTitle(gameBuildSettings_.productName);
	}

	//================================================================
	// Win32 / DirectX / 入力デバイスの初期化
	//================================================================

	platformManager_.Initialize(instanceHandle);  // instanceHandle は CreateWindow と DirectInput 生成に使うアプリ実体ハンドル。

	// ウィンドウ生成や DirectX 初期化が失敗した場合は、後続 Manager が未生成リソースを触らないように止める。
	if (platformManager_.HasInitializationFailed()) {
		return;
	}

	if (hasStandaloneInitializationFailed_) {
		MessageBoxA(
			nullptr,
			gameBuildMessage.c_str(),
			"CG2 Game Build Error",
			MB_OK | MB_ICONERROR);
		PostQuitMessage(1);
		return;
	}

	//================================================================
	// エディター機能ごとの初期化
	//================================================================

	sceneLifecycleManager_.Initialize();  // Scene / Runtime / 選択同期を初期化して、空 Scene を編集できる状態にする。

	if (isStandaloneGame_) {
		if (!g_editorScene.LoadScene(gameBuildSettings_.startupScenePath)) {
			MessageBoxA(
				nullptr,
				"起動シーンを読み込めません",
				"CG2 Game Build Error",
				MB_OK | MB_ICONERROR);
			PostQuitMessage(1);
			return;
		}

		g_currentScenePath = gameBuildSettings_.startupScenePath;
		g_editorSceneSynchronizer.Update(
			g_editorTextureFilePaths,
			g_selectedPlacedSceneObjectIndex);
		g_editorRuntimeManager.TogglePlay();
	}
	frameInputManager_.Initialize();  // キーボード入力とウィンドウサイズ追従を使える状態にする。
	imguiFrameManager_.Initialize();  // ImGui のフレーム制御担当。現在は実体初期化済みリソースを使うだけなので空実装。
	mainMenuManager_.Initialize();  // ファイルメニューや Play ボタンの表示担当。状態は共有 Scene を直接参照する。
	dockingManager_.Initialize();  // Unity 風の DockSpace を作る担当。初回 Draw で DockBuilder を使う。
	sceneViewManager_.Initialize();  // Scene タブ、ギズモ、ガイド線、ドラッグ配置を使うための担当。
	gameViewManager_.Initialize();  // GameView は Camera Component の出力確認を使うための担当。
	hierarchyWindowManager_.Initialize();  // GameObject ツリーを表示する Hierarchy 担当。
	inspectorWindowManager_.Initialize();  // 選択中 GameObject / Component を編集する Inspector 担当。
	bottomPanelWindowManager_.Initialize();  // Project と Console を下部にまとめて表示する BottomPanel 担当。
	animationWindowManager_.Initialize();  // Property Animation Clip を Timeline で編集する独立 Window 担当。
	gameplayToolsWindowManager_.Initialize();  // 汎用Spline、Event Timeline、State Graphを初期化する。
	diagnosticsWindowManager_.Initialize();  // Runtime負荷とScene設定不足を検査できる状態にする。
	logMonitorWindowManager_.Initialize();  // 汎用ログ・監視の選択UIを使うための担当。
	hookWireDebugWindowManager_.Initialize();  // Hook構成とWireの検査Windowを使うための担当。
	pvShootWindowManager_.Initialize();  // PV撮影モードのCamera/PostProcess/TimeScale調整を使うための担当。

	if (!isStandaloneGame_) {
		teamCollaborationManager_.Initialize(&g_editorScene, &g_editorConsoleMessages);
	}

	renderManager_.Initialize();  // DirectX12 の描画コマンドを積む Renderer 担当。
}

void GameScene::Update() {
	//================================================================
	// OS メッセージと終了要求の更新
	//================================================================

	platformManager_.Update();  // Windows メッセージを処理し、WM_QUIT が来たら終了フラグを立てる。

	// 終了要求があるフレームでは、入力や UI が破棄済みリソースを触らないようにする。
	if (IsEndRequested()) {
		return;
	}

	//================================================================
	// フレーム中に変化する編集状態の更新
	//================================================================

	frameInputManager_.Update();  // DIK キー状態、カメラ操作、ウィンドウリサイズ後の描画サイズを更新する。
	sceneLifecycleManager_.Update();  // Play 中の物理・Input Component と、SceneObject / GameObject の同期を更新する。
	imguiFrameManager_.Update();  // ImGui / ImGuizmo の新しいフレームを開始する。

	if (isStandaloneGame_) {
		gameViewManager_.Update();
		renderManager_.Update();
		return;
	}

	mainMenuManager_.Update();  // メインメニューは Draw で表示するだけなので Update は空実装。
	dockingManager_.Update();  // Docking は Draw 時に DockSpace を確保するため、Update は空実装。
	sceneViewManager_.Update();  // SceneView の入力判定は Draw 中の ImGui 座標が必要なので Update は空実装。
	gameViewManager_.Update();  // GameView の Camera 行列は Draw 中の矩形から作るため Update は空実装。
	hierarchyWindowManager_.Update();  // Hierarchy は Draw 中に選択・ドラッグを処理するため Update は空実装。
	inspectorWindowManager_.Update();  // Inspector は Draw 中に Component 値を編集するため Update は空実装。
	bottomPanelWindowManager_.Update();  // BottomPanel は Draw 中に Project / Console を操作するため Update は空実装。
	animationWindowManager_.Update();  // Timeline Preview の時間進行と Record 中の Key 化を更新する。
	gameplayToolsWindowManager_.Update();  // Gameplay編集WindowはDraw時編集のため状態維持だけを行う。
	diagnosticsWindowManager_.Update();  // 表示中だけ一定間隔でScene構成を静的検査する。
	logMonitorWindowManager_.Update();  // 選択UIはDraw中に編集するためUpdateは空実装。
	hookWireDebugWindowManager_.Update();  // 検査はDraw中に行うためUpdateは空実装。
	pvShootWindowManager_.Update();  // PV撮影モードのTimeScale倍率をRuntimeへ反映する。
	teamCollaborationManager_.Update(1.0f / 60.0f, g_editorRuntimeManager.IsPlaying());
	renderManager_.Update();  // Renderer は Draw で GPU コマンドを発行するため Update は空実装。
}

void GameScene::Draw() {
	// 終了処理中のフレームでは、SwapChain や ImGui の描画を行わない。
	if (IsEndRequested()) {
		return;
	}

	//================================================================
	// UI と DirectX12 描画コマンドの発行
	//================================================================

	platformManager_.Draw();  // 描画フラグをフレーム先頭で下げ、ImGui Draw 後だけ Renderer が実行されるようにする。
	sceneLifecycleManager_.Draw();  // Play 中だけ Runtime 用 Draw を呼び、物理状態の反映を描画前に完了させる。

	if (isStandaloneGame_) {
		gameViewManager_.Draw();
		imguiFrameManager_.Draw();
		renderManager_.Draw();
		return;
	}

	mainMenuManager_.Draw();  // 上部メニューと Play / Stop の UI を構築する。
	pvShootWindowManager_.Draw();  // PV撮影モードの切り替えチェックボックスと調整パネルを描画する。

	if (!pvShootWindowManager_.IsActive()) {
		dockingManager_.Draw();  // 各ウィンドウをドラッグ移動・ドッキングできる DockSpace を構築する。
		sceneViewManager_.Draw();  // Scene タブ、グリッド、ギズモ、ドラッグ配置、範囲選択を描画する。
	}

	gameViewManager_.Draw();  // GameView の独立ウィンドウと Camera Component 出力範囲を描画する。

	if (!pvShootWindowManager_.IsActive()) {
		hierarchyWindowManager_.Draw();  // GameObject 階層を描画し、選択や親子付けの入力を処理する。
		inspectorWindowManager_.Draw();  // 選択中 GameObject の Transform / Component / 環境設定を描画する。
		bottomPanelWindowManager_.Draw();  // Project アセット一覧と Console ログを描画する。
		animationWindowManager_.Draw();  // Animation Clip の Timeline、Track、Keyframe、Event を描画する。
		gameplayToolsWindowManager_.Draw();  // 汎用Spline、Event Timeline、State Graphを描画する。
		diagnosticsWindowManager_.Draw();  // ProfilerとScene Validatorを独立Windowへ描画する。
		logMonitorWindowManager_.Draw();  // 汎用ログ・監視の選択UIを独立Windowへ描画する。
		teamCollaborationManager_.Draw(&g_isTeamCollaborationWindowVisible);
		hookWireDebugWindowManager_.Draw();  // Hook構成の設定不備とRuntime Wireを独立Windowへ描画する。
	}

	imguiFrameManager_.Draw();  // ImGui の DrawData を確定し、Renderer が GPU に送れる状態にする。
	renderManager_.Draw();  // 3D/2D オブジェクト、ImGui、Present、Fence 待ちまでを実行する。
}

int GameScene::Finalize() {
	// DirectX / ImGui / Win32 / 音声リソースを解放し、WinMain に返す終了コードを受け取る。
	teamCollaborationManager_.Finalize();
	return platformManager_.Finalize();
}

bool GameScene::IsEndRequested() const {
	// WM_QUIT や初期化失敗で立つ終了フラグを WinMain のループ条件に使う。
	return platformManager_.IsEndRequested();
}
