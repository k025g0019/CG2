#pragma warning(disable : 4189 4514)

#include "EditorInspectorWindowManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

namespace {
	// 旧プレビュー選択で使う固定表示名。0:Model、1:Sprite、2:Light、3:Camera に対応する。
	const char* kObjectNames[] = {
		"モデル",
		"スプライト",
		"ライト",
		"デバッグカメラ",
	};
}

void EditorInspectorWindowManager::Initialize() {
}

void EditorInspectorWindowManager::Update() {
}

void EditorInspectorWindowManager::Draw() {
#ifdef USE_IMGUI
	//================================================================
	// InspectorPanel に渡す表示・編集用 Context の組み立て
	//================================================================

	constexpr float editorMenuHeight = 20.0f;  // editorMenuHeight は Inspector の初期位置と高さ計算に使う上部メニュー高さ。
	constexpr ImGuiWindowFlags dockableWindowFlags = ImGuiWindowFlags_NoCollapse;  // dockableWindowFlags は Docking 後も折りたたみ禁止にするためのフラグ。
	std::vector<EditorSceneObject>& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();  // editorSceneObjects は DirectX 描画用の配置配列。Inspector から Transform を直接編集する。

	// inspectorContext は InspectorPanel が必要な共有状態を 1 箇所にまとめた受け渡し用構造体。
	EditorInspectorPanelContext inspectorContext{
		.editorSceneX = g_editorSceneX,
		.editorSceneWidth = g_editorSceneWidth,
		.editorMenuHeight = editorMenuHeight,
		.editorRightWidth = g_editorRightWidth,
		.editorWindowHeight = g_editorWindowHeight,
		.dockableWindowFlags = dockableWindowFlags,
		.editorScene = g_editorScene,
		.selectionManager = g_editorSelectionManager,
		.sceneSynchronizer = g_editorSceneSynchronizer,
		.runtimeManager = g_editorRuntimeManager,
		.textureFilePaths = g_editorTextureFilePaths,
		.textureSrvHandlesGPU = g_textureSrvHandlesGPU,
		.textureCount = _countof(g_textureFilePaths),
		.sceneObjects = editorSceneObjects,
		.objectNames = kObjectNames,
		.objectNameCount = _countof(kObjectNames),
		.selectedSceneObject = g_selectedSceneObject,
		.selectedPlacedSceneObjectIndex = g_selectedPlacedSceneObjectIndex,
		.selectedEditorGameObjectId = g_selectedEditorGameObjectId,
		.previousSelectedEditorGameObjectId = g_previousSelectedEditorGameObjectId,
		.selectedAddComponentIndex = g_selectedAddComponentIndex,
		.selectedGameObjectName = g_selectedGameObjectName,
		.selectedGameObjectNameSize = _countof(g_selectedGameObjectName),
		.isLegacyPreviewVisible = g_isLegacyPreviewVisible,
		.sceneClearColor = g_sceneClearColor,
		.isSceneGizmoVisible = g_isSceneGizmoVisible,
		.isLightGizmoVisible = g_isLightGizmoVisible,
		.isCameraGizmoVisible = g_isCameraGizmoVisible,
		.sphereMaterialData = g_sphereMaterialData,
		.spriteMaterialData = g_spriteMaterialData,
		.directionalLightData = g_directionalLightData,
		.isGizmoLocalMode = g_isGizmoLocalMode,
		.isGizmoSnapEnabled = g_isGizmoSnapEnabled,
		.gizmoSnapValues = g_gizmoSnapValues,
		.activeEditorTool = g_activeEditorTool,
		.isSceneAssistVisible = g_isSceneAssistVisible,
		.editorCameraMoveSpeed = g_editorCameraMoveSpeed,
		.editorCameraRotateSpeed = g_editorCameraRotateSpeed,
		.editorCameraWheelMoveSpeed = g_editorCameraWheelMoveSpeed,
		.editorCameraPanSpeed = g_editorCameraPanSpeed,
		.editorCameraFastRate = g_editorCameraFastRate,
		.modelTransform = g_transform,
		.spriteTransform = g_spriteTransform,
		.uvTransform = g_uvTransform,
		.cameraTransform = g_cameraTransform,
		.directionalLightIconPosition = g_directionalLightIconPosition,
		.selectedAssetPath = g_selectedAssetPath,
	};

	g_editorInspectorPanel.Draw(inspectorContext);  // InspectorPanel はこの Context を参照して、選択中の Transform / Component / 環境設定を描画する。
#endif
}
