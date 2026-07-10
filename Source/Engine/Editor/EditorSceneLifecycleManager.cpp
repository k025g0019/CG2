#pragma warning(disable : 4189 4514)

#include "EditorSceneLifecycleManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorSceneLifecycleManager::Initialize() {
	// PlatformManager が成功していない場合は、Scene が GPU リソースを触らないようにする。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

	std::vector<EditorSceneObject>& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();  // SceneObject は描画用の配置データ。Selection / Synchronizer はこの配列を共有して参照する。

	//================================================================
	// エディター UI と Scene 補助クラスの初期化
	//================================================================

	// Manager 初期化は一度だけ。毎フレーム呼ぶと参照先や状態を上書きして選択が壊れる。
	if (!g_isEditorManagerInitialized) {
		g_editorSelectionManager.Initialize(&g_editorScene, &editorSceneObjects);  // 選択 Manager は GameObject と SceneObject の選択 ID を相互変換する。
		g_editorSceneSynchronizer.Initialize(&g_editorScene, &g_editorSceneObjectManager);  // Synchronizer は EditorScene の GameObject から描画用 SceneObject を作る。
		g_editorSceneCameraController.Initialize();  // Scene カメラはエディター専用の Transform を初期位置に戻す。

		// AssetFactory は Project からのドラッグ配置で GameObject と SceneObject を同時生成する。
		g_editorAssetFactory.Initialize(
			&g_editorScene,
			&g_editorSceneObjectManager,
			&g_editorTextureFilePaths,
			&g_editorConsoleMessages);

		g_editorMainMenuBar.Initialize(&g_editorScene, &g_editorRuntimeManager);  // MainMenu は Scene 保存と Play / Stop を操作する。

		// Hierarchy は GameObject の選択、生成、親子付けを操作する。
		g_editorHierarchyPanel.Initialize(
			&g_editorScene,
			&g_editorSelectionManager,
			&g_editorAssetFactory,
			&g_editorConsoleMessages);

		g_editorInspectorPanel.Initialize();  // Inspector は選択中 GameObject の Transform と Component を編集する。
		g_editorBottomPanel.Initialize();  // BottomPanel は Project アセット一覧と Console ログを表示する。
		g_isEditorManagerInitialized = true;  // 初期化済みフラグで、上記のポインタ設定を二重実行しないようにする。
	}

	//================================================================
	// 空 Scene と Play Runtime の初期化
	//================================================================

	// Scene の初期化は一度だけ。LoadScene 後の内容を毎フレーム空 Scene に戻さないための条件。
	if (!g_isEditorSceneInitialized) {
		g_editorScene.InitializeDefaultScene();  // 起動直後はユーザーが配置できる空 Scene と環境 GameObject だけを作る。
		g_editorRuntimeManager.Initialize(&g_editorScene, &g_editorConsoleMessages);  // RuntimeManager は Play 開始時に Scene をコピーして物理と Input を動かす。
		g_isEditorRuntimeInitialized = true;

		// 初期 Scene に GameObject がある場合は、Inspector に最初の要素を表示する。
		if (!g_editorScene.GetGameObjects().empty()) {
			g_selectedEditorGameObjectId = g_editorScene.GetGameObjects()[0].id;
			SetSingleSelectedGameObject(g_selectedEditorGameObjectId);
		}

		g_isEditorSceneInitialized = true;  // 初期 Scene の生成済みフラグ。二重生成による ID 重複を防ぐ。
	}
}

void EditorSceneLifecycleManager::Update() {
	// 初期化前や終了後は Runtime や同期処理が共有配列を触らないようにする。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

	//================================================================
	// Play 中の物理と Input Component 更新
	//================================================================

	// Play していない時は Inspector 編集値をそのまま保ち、物理で Transform を動かさない。
	if (g_editorRuntimeManager.IsPlaying()) {
		constexpr float editorPlayDeltaTime = 1.0f / 60.0f;  // editorPlayDeltaTime は 60FPS 固定の物理更新秒数。
		g_editorRuntimeManager.Update(g_key, editorPlayDeltaTime);  // RuntimeManager が InputManager と PhysicsManager を順番に更新する。
	}

	//================================================================
	// GameObject と DirectX 描画用 SceneObject の同期
	//================================================================

	g_editorSceneSynchronizer.Update(g_editorTextureFilePaths, g_selectedPlacedSceneObjectIndex);  // GameObject の ModelRenderer / SpriteRenderer から SceneObject を生成・削除・更新する。

	// 旧プレビュー用の選択番号と、新しい GameObject 選択 ID を同じ対象にそろえる。
	g_editorSelectionManager.SyncLegacySelection(
		g_selectedEditorGameObjectId,
		g_selectedSceneObject,
		g_selectedPlacedSceneObjectIndex);

}

void EditorSceneLifecycleManager::Draw() {
	// Play 中だけ Runtime の結果を描画前状態に反映する。編集停止中は Scene 値をそのまま描く。
	if (g_editorRuntimeManager.IsPlaying()) {
		g_editorRuntimeManager.Draw();
	}
}
