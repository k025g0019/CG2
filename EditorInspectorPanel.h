#pragma once

#include "EditorCommonTypes.h"
#include "EditorRuntimeManager.h"
#include "EditorScene.h"
#include "EditorSceneObject.h"
#include "EditorSceneSynchronizer.h"
#include "EditorSelectionManager.h"

#pragma warning(push, 0)
#include <d3d12.h>
#include "externals/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)
#pragma warning(disable : 4623)
#pragma warning(disable : 4626)
#pragma warning(disable : 5027)

struct EditorInspectorPanelContext {
	float editorSceneX;  // SceneView の左端 X
	float editorSceneWidth;  // SceneView の幅
	float editorMenuHeight;  // メニューバーの高さ
	float editorRightWidth;  // Inspector の初期幅
	float editorWindowHeight;  // アプリウィンドウ全体の高さ
	ImGuiWindowFlags dockableWindowFlags;  // Docking 可能ウィンドウの ImGui フラグ
	EditorScene& editorScene;  // Inspector が編集する Scene
	EditorSelectionManager& selectionManager;  // 選択同期を担当する Manager
	EditorSceneSynchronizer& sceneSynchronizer;  // SceneObject と GameObject の同期を担当する Manager
	EditorRuntimeManager& runtimeManager;  // Play 中の Script / Animation 状態を参照する Runtime
	const std::vector<std::string>& textureFilePaths;  // 登録済み Texture パス配列
	const D3D12_GPU_DESCRIPTOR_HANDLE* textureSrvHandlesGPU;  // Texture Preview に使う SRV GPU Handle 配列
	size_t textureCount;  // textureSrvHandlesGPU の有効数
	std::vector<EditorSceneObject>& sceneObjects;  // SceneView に表示する描画用オブジェクト配列
	const char* const* objectNames;  // 旧 UI 用の選択表示名配列
	size_t objectNameCount;  // objectNames の要素数
	int& selectedSceneObject;  // 旧 UI の選択種類。Model=0, Sprite=1, Light=2, Camera=3
	int32_t& selectedPlacedSceneObjectIndex;  // SceneObject 配列の選択 index
	int32_t& selectedEditorGameObjectId;  // EditorScene の GameObject 選択 ID
	int32_t& previousSelectedEditorGameObjectId;  // 名前入力欄の同期判定に使う前回選択 ID
	int32_t& selectedAddComponentIndex;  // Component 追加 ComboBox の選択 index
	char* selectedGameObjectName;  // GameObject 名編集用バッファ
	size_t selectedGameObjectNameSize;  // selectedGameObjectName のバッファサイズ
	bool& isLegacyPreviewVisible;  // 旧プレビュー表示を使うか
	float* sceneClearColor;  // DirectX ClearColor へ渡す背景色
	bool& isSceneGizmoVisible;  // SceneView のギズモを表示するか
	bool& isLightGizmoVisible;  // ライトアイコンを表示するか
	bool& isCameraGizmoVisible;  // カメラアイコンを表示するか
	Material* sphereMaterialData;  // モデル描画用 Material ConstantBuffer の CPU 書き込み先
	Material* spriteMaterialData;  // スプライト描画用 Material ConstantBuffer の CPU 書き込み先
	DirectionalLight* directionalLightData;  // DirectionalLight ConstantBuffer の CPU 書き込み先
	bool& isGizmoLocalMode;  // ギズモを Local 座標で動かすか
	bool& isGizmoSnapEnabled;  // ギズモの Snap を有効にするか
	float* gizmoSnapValues;  // ギズモ Snap の X/Y/Z 値
	int& activeEditorTool;  // 移動 / 回転 / 拡縮 / 統合のツール番号
	bool& isSceneAssistVisible;  // SceneView 左上の操作ヘルプを表示するか
	float& editorCameraMoveSpeed;  // キーボードカメラ移動速度
	float& editorCameraRotateSpeed;  // マウス / キーボードカメラ回転速度
	float& editorCameraWheelMoveSpeed;  // ホイール前後移動速度
	float& editorCameraPanSpeed;  // 中ボタン平行移動速度
	float& editorCameraFastRate;  // Shift 高速移動倍率
	Transforms& modelTransform;  // 旧モデルプレビュー用 Transform
	Transforms& spriteTransform;  // 旧スプライトプレビュー用 Transform
	Transforms& uvTransform;  // UV 変換用 Transform
	Transforms& cameraTransform;  // Scene カメラ Transform
	Vector3& directionalLightIconPosition;  // DirectionalLight アイコンの SceneView 位置
	std::string& selectedAssetPath;  // Project で選択中の AssetPath
};

class EditorInspectorPanel {
public:
	void Initialize();  // Inspector 初期化。現時点では保持状態なし
	void Update();  // Inspector 更新。現時点では自動更新なし
	void Draw(EditorInspectorPanelContext& context);  // 選択中 GameObject / Component / 環境設定を描画する

private:
	const char* GetSelectedObjectLabel(const EditorInspectorPanelContext& context) const;  // 旧選択種類から表示名を返す
	void SyncSelection(EditorInspectorPanelContext& context) const;  // GameObject 選択を SceneView 選択へ同期する
};

#pragma warning(pop)
