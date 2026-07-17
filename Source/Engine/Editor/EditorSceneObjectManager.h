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
	bool SetCustomTexture(int32_t sceneObjectIndex, const std::string& textureAssetPath);  // 任意画像を SceneObject 専用 Texture として GPU へ読み込む
	bool SetMaterialTexture(
		int32_t sceneObjectIndex,
		EditorMaterialTextureSlot textureSlot,
		const std::string& textureAssetPath);  // 指定した PBR Map を個別 SRV として読み込む
	void ClearCustomTexture(int32_t sceneObjectIndex);  // SceneObject の個別画像を解放し、固定テクスチャ利用へ戻す
	void ClearMaterialTexture(int32_t sceneObjectIndex, EditorMaterialTextureSlot textureSlot);  // 指定 PBR Map を解放する
	void ClearAllMaterialTextures(int32_t sceneObjectIndex);  // SceneObject が持つ全 PBR Map を解放する
	void ClearCustomModelMesh(int32_t sceneObjectIndex);  // SceneObject を内部基本形描画へ戻す
	void ReleaseObject(int32_t sceneObjectIndex);  // 指定した描画用 SceneObject の GPU Resource を解放する
	void ReleaseAll();  // 全 SceneObject の GPU Resource を解放して配列を空にする
	std::vector<EditorSceneObject>& GetSceneObjects();  // SceneView / Render / Synchronizer が編集する SceneObject 配列
	const std::vector<EditorSceneObject>& GetSceneObjects() const;  // 読み取り専用の SceneObject 配列

private:
	ID3D12Device* device_ = nullptr;  // GPU Resource 作成に使う DirectX12 Device
	std::vector<EditorSceneObject> sceneObjects_;  // Scene 上に表示するモデル / スプライトの描画用データ
	std::vector<int32_t> freeCustomTextureDescriptorIndices_;  // 削除済み SceneObject から回収した SRV 番号
	int32_t nextCustomTextureDescriptorIndex_ = 128;  // 0-127 は描画機能、128 以降は GameObject ごとの画像 SRV に使う
	ID3D12Resource* CreateVertexResource(size_t sizeInBytes) const;  // 実メッシュ頂点を書き込む Upload Buffer を作る
	ID3D12Resource* CreateTransformationResource() const;  // WVP / World 行列を書き込む Upload Buffer を作る
	ID3D12Resource* CreateMaterialResource() const;  // GameObject ごとの Material 値を書き込む Upload Buffer を作る
	int32_t AcquireCustomTextureDescriptorIndex();  // 個別画像用に SRV Heap 内の空き番号を確保する
	void ReleaseCustomTextureDescriptorIndex(int32_t descriptorIndex);  // 使い終わった SRV 番号を再利用用リストへ戻す
	bool LoadTextureResource(
		const std::string& textureAssetPath,
		ID3D12Resource*& textureResource,
		ID3D12Resource*& uploadResource,
		D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
		int32_t& descriptorIndex);  // 画像を GPU へ転送し、割り当てた SRV を返す
	void ReleaseTextureResource(
		ID3D12Resource*& textureResource,
		ID3D12Resource*& uploadResource,
		D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
		int32_t& descriptorIndex);  // Texture と SRV 番号をまとめて解放する
};

#pragma warning(pop)
