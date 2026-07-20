#include "GameScene.h"

void GameScene::Initialize(_In_ HINSTANCE instanceHandle) {
	//================================================================
	// Win32 / DirectX / 入力デバイスの初期化
	//================================================================

	platformManager_.Initialize(instanceHandle);  // instanceHandle は CreateWindow と DirectInput 生成に使うアプリ実体ハンドル。

	// ウィンドウ生成や DirectX 初期化が失敗した場合は、後続 Manager が未生成リソースを触らないように止める。
	if (platformManager_.HasInitializationFailed()) {
		return;
	}

	//================================================================
	// エディター機能ごとの初期化
	//================================================================

	sceneLifecycleManager_.Initialize();  // Scene / Runtime / 選択同期を初期化して、空 Scene を編集できる状態にする。
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
	mainMenuManager_.Update();  // メインメニューは Draw で表示するだけなので Update は空実装。
	dockingManager_.Update();  // Docking は Draw 時に DockSpace を確保するため、Update は空実装。
	sceneViewManager_.Update();  // SceneView の入力判定は Draw 中の ImGui 座標が必要なので Update は空実装。
	gameViewManager_.Update();  // GameView の Camera 行列は Draw 中の矩形から作るため Update は空実装。
	hierarchyWindowManager_.Update();  // Hierarchy は Draw 中に選択・ドラッグを処理するため Update は空実装。
	inspectorWindowManager_.Update();  // Inspector は Draw 中に Component 値を編集するため Update は空実装。
	bottomPanelWindowManager_.Update();  // BottomPanel は Draw 中に Project / Console を操作するため Update は空実装。
	animationWindowManager_.Update();  // Timeline Preview の時間進行と Record 中の Key 化を更新する。
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
	mainMenuManager_.Draw();  // 上部メニューと Play / Stop の UI を構築する。
	dockingManager_.Draw();  // 各ウィンドウをドラッグ移動・ドッキングできる DockSpace を構築する。
	sceneViewManager_.Draw();  // Scene タブ、グリッド、ギズモ、ドラッグ配置、範囲選択を描画する。
	gameViewManager_.Draw();  // GameView の独立ウィンドウと Camera Component 出力範囲を描画する。
	hierarchyWindowManager_.Draw();  // GameObject 階層を描画し、選択や親子付けの入力を処理する。
	inspectorWindowManager_.Draw();  // 選択中 GameObject の Transform / Component / 環境設定を描画する。
	bottomPanelWindowManager_.Draw();  // Project アセット一覧と Console ログを描画する。
	animationWindowManager_.Draw();  // Animation Clip の Timeline、Track、Keyframe、Event を描画する。
	imguiFrameManager_.Draw();  // ImGui の DrawData を確定し、Renderer が GPU に送れる状態にする。
	renderManager_.Draw();  // 3D/2D オブジェクト、ImGui、Present、Fence 待ちまでを実行する。
}

int GameScene::Finalize() {
	// DirectX / ImGui / Win32 / 音声リソースを解放し、WinMain に返す終了コードを受け取る。
	return platformManager_.Finalize();
}

bool GameScene::IsEndRequested() const {
	// WM_QUIT や初期化失敗で立つ終了フラグを WinMain のループ条件に使う。
	return platformManager_.IsEndRequested();
}
