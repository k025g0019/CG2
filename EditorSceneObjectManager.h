#pragma once

#include "EditorSceneObject.h"

#pragma warning(push, 0)
#include <d3d12.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorSceneObjectManager {
public:
	void Initialize(ID3D12Device* device);  // Transformation 用 Resource を作るための D3D12Device を受け取る
	void Update();  // 現時点では自動更新なし
	void Draw();  // 現時点では直接描画なし。描画は EditorRenderManager が担当する

	// 描画用 SceneObject を 1 つ作成し、配列番号を返す
	int32_t CreateObject(
		EditorSceneObjectType type,
		int32_t textureIndex,
		const Transforms& initialTransform,
		const std::string& name);
	bool SetCustomModelMesh(int32_t sceneObjectIndex, const std::string& assetPath, const ModelData& modelData);  // 実メッシュ頂点を SceneObject の GPU バッファへ設定する
	void ClearCustomModelMesh(int32_t sceneObjectIndex);  // SceneObject を内部基本形描画へ戻す
	void ReleaseObject(int32_t sceneObjectIndex);  // 指定した描画用 SceneObject の GPU Resource を解放する
	void ReleaseAll();  // 全 SceneObject の GPU Resource を解放して配列を空にする
	std::vector<EditorSceneObject>& GetSceneObjects();  // SceneView / Render / Synchronizer が編集する SceneObject 配列
	const std::vector<EditorSceneObject>& GetSceneObjects() const;  // 読み取り専用の SceneObject 配列

private:
	ID3D12Device* device_ = nullptr;  // GPU Resource 作成に使う DirectX12 Device
	std::vector<EditorSceneObject> sceneObjects_;  // Scene 上に表示するモデル / スプライトの描画用データ
	ID3D12Resource* CreateVertexResource(size_t sizeInBytes) const;  // 実メッシュ頂点を書き込む Upload Buffer を作る
	ID3D12Resource* CreateTransformationResource() const;  // WVP / World 行列を書き込む Upload Buffer を作る
	ID3D12Resource* CreateMaterialResource() const;  // GameObject ごとの Material 値を書き込む Upload Buffer を作る
};

#pragma warning(pop)
