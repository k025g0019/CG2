#pragma once

#include "EditorSceneObject.h"

#pragma warning(push, 0)
#include <d3d12.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <unordered_map>
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
	bool UpdateSkinnedPose(
		int32_t sceneObjectIndex,
		const ModelData& modelData,
		int32_t clipIndex,
		float playbackTime,
		const std::string& avatarMaskAssetPath);  // 現在姿勢を更新し、AvatarMask と Motion Vector 用の前姿勢を適用する
	bool SetCustomTexture(int32_t sceneObjectIndex, const std::string& textureAssetPath);  // 任意画像を SceneObject 専用 Texture として GPU へ読み込む
	D3D12_GPU_DESCRIPTOR_HANDLE GetOrLoadUiTexture(const std::string& textureAssetPath);  // Game View UI 用画像を共有キャッシュから返す
	bool SetMaterialTexture(
		int32_t sceneObjectIndex,
		EditorMaterialTextureSlot textureSlot,
		const std::string& textureAssetPath);  // 指定した PBR Map を個別 SRV として読み込む
	void ClearCustomTexture(int32_t sceneObjectIndex);  // SceneObject の個別画像を解放し、固定テクスチャ利用へ戻す
	void ClearMaterialTexture(int32_t sceneObjectIndex, EditorMaterialTextureSlot textureSlot);  // 指定 PBR Map を解放する
	void ClearAllMaterialTextures(int32_t sceneObjectIndex);  // SceneObject が持つ全 PBR Map を解放する
	void ClearCustomModelMesh(int32_t sceneObjectIndex);  // SceneObject を内部基本形描画へ戻す
	void InvalidateAssetResources(const std::string& assetPath);  // 外部更新されたモデル・画像のGPU資源を破棄し次回同期で再読込させる
	void ClearSkinningResources(int32_t sceneObjectIndex);  // Bone 行列 Buffer と再生状態を解放する
	void ReleaseObject(int32_t sceneObjectIndex);  // 指定した描画用 SceneObject の GPU Resource を解放する
	void ReleaseAll();  // 全 SceneObject の GPU Resource を解放して配列を空にする
	std::vector<EditorSceneObject>& GetSceneObjects();  // SceneView / Render / Synchronizer が編集する SceneObject 配列
	const std::vector<EditorSceneObject>& GetSceneObjects() const;  // 読み取り専用の SceneObject 配列

private:
	// Model / Sprite が参照する画像を Path 単位で共有する。
	// 同じ画像を SceneObject ごとに読み直すと、Chunk 100個で SRV も VRAM も 100倍消費する。
	struct SharedModelTexture {
		ID3D12Resource* textureResource = nullptr;  // 共有する GPU Texture 本体
		ID3D12Resource* uploadResource = nullptr;  // 初回転送用 Upload Buffer。Cache が所有する
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};  // 参照側へ貸し出す SRV
		int32_t descriptorIndex = -1;  // 共通 SRV Heap 内の割り当て番号
		int32_t referenceCount = 0;  // この画像を参照している SceneObject 数
		bool loadFailed = false;  // 読み込み失敗を記憶し、毎フレームの再読込と GPU 全同期を止める
	};
	struct CachedUiTexture {
		ID3D12Resource* textureResource = nullptr;  // UI画像のGPU Texture本体
		ID3D12Resource* uploadResource = nullptr;  // 初回転送が完了するまで保持するUpload Buffer
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};  // ImGuiのImage描画へ渡すSRV
		int32_t descriptorIndex = -1;  // 共通SRV Heap内の割り当て番号
	};
	struct SharedModelMesh {
		ID3D12Resource* vertexResource = nullptr;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		uint32_t vertexCount = 0u;
		ID3D12Resource* indexResource = nullptr;
		D3D12_INDEX_BUFFER_VIEW indexBufferView{};
		uint32_t indexCount = 0u;
		int32_t referenceCount = 0;
	};
	struct MeshAtlasPage {
		ID3D12Resource* resource = nullptr;
		uint8_t* mappedData = nullptr;
		size_t capacity = 0u;
		size_t used = 0u;
	};

	ID3D12Device* device_ = nullptr;  // GPU Resource 作成に使う DirectX12 Device
	std::vector<EditorSceneObject> sceneObjects_;  // Scene 上に表示するモデル / スプライトの描画用データ
	std::unordered_map<std::string, CachedUiTexture> cachedUiTextures_;  // 同じUI画像をGameObjectごとに重複ロードしない
	std::unordered_map<std::string, SharedModelTexture> sharedModelTextures_;  // 同じModel画像をSceneObjectごとに重複ロードしない
	std::unordered_map<std::string, SharedModelMesh> sharedModelMeshes_;  // 同じModelの不変Vertex/Index Bufferを共有する
	std::vector<MeshAtlasPage> vertexAtlasPages_;  // 固有Meshも少数の大きなBufferへ詰め、Resource切替を減らす
	std::vector<MeshAtlasPage> indexAtlasPages_;
	static constexpr uint32_t kObjectBufferCapacity = 16384u;
	static constexpr size_t kConstantBufferAlignment = 256u;
	ID3D12Resource* transformationPoolResource_ = nullptr;
	ID3D12Resource* gameTransformationPoolResource_ = nullptr;
	ID3D12Resource* materialPoolResource_ = nullptr;
	uint8_t* transformationPoolData_ = nullptr;
	uint8_t* gameTransformationPoolData_ = nullptr;
	uint8_t* materialPoolData_ = nullptr;
	uint32_t nextObjectBufferSlot_ = 0u;
	std::vector<uint32_t> freeObjectBufferSlots_;
	std::vector<int32_t> freeCustomTextureDescriptorIndices_;  // 削除済み SceneObject から回収した SRV 番号
	int32_t nextCustomTextureDescriptorIndex_ = 205;  // EditorSharedState::kRuntimeReservedSrvDescriptorCount と同じ値。0-204は描画機能の予約。
	bool hasFreedTextureDescriptor_ = false;  // SRVが空いた直後だけ、上限超過で失敗した画像の再読込を1回許す
	ID3D12Resource* CreateVertexResource(size_t sizeInBytes) const;  // 実メッシュ頂点を書き込む Upload Buffer を作る
	ID3D12Resource* CreateTransformationResource() const;  // WVP / World 行列を書き込む Upload Buffer を作る
	ID3D12Resource* CreateMaterialResource() const;  // GameObject ごとの Material 値を書き込む Upload Buffer を作る
	bool AllocateMeshAtlasRange(
		std::vector<MeshAtlasPage>& atlasPages,
		const void* sourceData,
		size_t dataSize,
		size_t alignment,
		ID3D12Resource*& resource,
		size_t& byteOffset);  // 共有Upload Atlasへデータを追記する
	void ReleaseMeshAtlasPages(std::vector<MeshAtlasPage>& atlasPages);
	bool InitializeObjectBufferPools();
	bool AcquireObjectBufferSlot(uint32_t& slot);
	void ReleaseObjectBufferSlot(uint32_t slot);
	void ReleaseObjectBufferPools();
	int32_t AcquireCustomTextureDescriptorIndex();  // 個別画像用に SRV Heap 内の空き番号を確保する
	void ReleaseCustomTextureDescriptorIndex(int32_t descriptorIndex);  // 使い終わった SRV 番号を再利用用リストへ戻す
	bool AcquireSharedModelTexture(
		const std::string& textureAssetPath,
		ID3D12Resource*& textureResource,
		D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
		int32_t& descriptorIndex);  // 共有Cacheから画像を借り、無ければ1回だけ読み込む
	void ReleaseSharedModelTexture(const std::string& textureAssetPath);  // 参照を1つ返し、0になった画像だけ解放する
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
