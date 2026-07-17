#include "EditorSceneObjectManager.h"

#include "EditorSharedState.h"
#include "Matrix.h"
#include "StringUtility.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>

#pragma warning(disable : 5045)

void EditorSceneObjectManager::Initialize(ID3D12Device* device) {
	device_ = device;  // CreateObject で ConstantBuffer を作るため Device を保持する
}

void EditorSceneObjectManager::Update() {
}

void EditorSceneObjectManager::Draw() {
}

int32_t EditorSceneObjectManager::CreateObject(
	EditorSceneObjectType type,
	int32_t textureIndex,
	const Transforms& initialTransform,
	const std::string& name) {
	// GPU Resource を作れないため、Device 未設定なら生成失敗
	if (device_ == nullptr) {
		return -1;
	}

	// 描画用オブジェクトの初期データ
	EditorSceneObject sceneObject{};
	sceneObject.type = type;
	sceneObject.meshType = EditorModelMeshType::Plane;
	sceneObject.usesCustomMesh = false;
	sceneObject.gameObjectId = -1;
	sceneObject.textureIndex = textureIndex;
	sceneObject.transform = initialTransform;
	sceneObject.name = name;
	sceneObject.assetPath.clear();
	sceneObject.textureAssetPath.clear();
	sceneObject.transformationResource = CreateTransformationResource();
	sceneObject.transformationData = nullptr;
	sceneObject.gameTransformationResource = CreateTransformationResource();
	sceneObject.gameTransformationData = nullptr;
	sceneObject.materialResource = CreateMaterialResource();
	sceneObject.materialData = nullptr;
	sceneObject.customTextureResource = nullptr;
	sceneObject.customTextureUploadResource = nullptr;
	sceneObject.customTextureSrvGpuHandle = {};
	sceneObject.customTextureDescriptorIndex = -1;
	sceneObject.materialTextureAssetPaths.fill("");
	sceneObject.materialTextureResources.fill(nullptr);
	sceneObject.materialTextureUploadResources.fill(nullptr);
	sceneObject.materialTextureSrvGpuHandles.fill({});
	sceneObject.materialTextureDescriptorIndices.fill(-1);
	sceneObject.customMeshVertexResource = nullptr;
	sceneObject.customMeshVertexBufferView = {};
	sceneObject.customMeshVertexCount = 0u;
	sceneObject.customMeshLocalBoundsCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.customMeshLocalBoundsSize = {1.0f, 1.0f, 1.0f};

	// 行列用 ConstantBuffer の作成に失敗した場合は無効番号を返す
	if (sceneObject.transformationResource == nullptr ||
	    sceneObject.gameTransformationResource == nullptr ||
	    sceneObject.materialResource == nullptr) {
		if (sceneObject.transformationResource != nullptr) {
			sceneObject.transformationResource->Release();
			sceneObject.transformationResource = nullptr;
		}
		if (sceneObject.gameTransformationResource != nullptr) {
			sceneObject.gameTransformationResource->Release();
			sceneObject.gameTransformationResource = nullptr;
		}
		if (sceneObject.materialResource != nullptr) {
			sceneObject.materialResource->Release();
			sceneObject.materialResource = nullptr;
		}
		return -1;
	}

	// CPU から毎フレーム WVP / World を書き込むため Map する
	HRESULT mapResult = sceneObject.transformationResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&sceneObject.transformationData));
	if (FAILED(mapResult) || sceneObject.transformationData == nullptr) {
		sceneObject.transformationResource->Release();  // Map に失敗した Resource は使わないので解放する
		sceneObject.gameTransformationResource->Release();
		sceneObject.materialResource->Release();
		return -1;
	}

	mapResult = sceneObject.gameTransformationResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&sceneObject.gameTransformationData));
	if (FAILED(mapResult) || sceneObject.gameTransformationData == nullptr) {
		sceneObject.transformationResource->Release();  // 片方だけ Map 成功した場合も両方解放して中途半端な Object を残さない
		sceneObject.gameTransformationResource->Release();
		sceneObject.materialResource->Release();
		return -1;
	}

	mapResult = sceneObject.materialResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&sceneObject.materialData));
	if (FAILED(mapResult) || sceneObject.materialData == nullptr) {
		sceneObject.transformationResource->Release();  // Material まで Map できた Object だけを Scene に残す
		sceneObject.gameTransformationResource->Release();
		sceneObject.materialResource->Release();
		return -1;
	}

	sceneObject.transformationData->WVP = MakeIdentity4x4();  // 初回描画前の行列を単位行列にしておく
	sceneObject.transformationData->World = MakeIdentity4x4();
	sceneObject.transformationData->lightWVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->WVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->World = MakeIdentity4x4();
	sceneObject.gameTransformationData->lightWVP = MakeIdentity4x4();
	sceneObject.materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};  // Mesh の初期色は白。Inspector の Renderer 色で上書きされる。
	sceneObject.materialData->enableLighting =
		type == EditorSceneObjectType::Model ? TRUE : FALSE;  // Model はライトあり、Sprite は Texture 色をそのまま出す。
	sceneObject.materialData->useTexture =
		type == EditorSceneObjectType::Sprite ? TRUE : FALSE;  // Model は白い形状として始め、Sprite だけ Texture を使う。
	sceneObject.materialData->metallic = 0.0f;
	sceneObject.materialData->roughness = 0.5f;
	sceneObject.materialData->reflectance = 0.0f;
	sceneObject.materialData->ior = 1.0f;
	sceneObject.materialData->emissionStrength = 0.0f;
	sceneObject.materialData->reflectionMode = 0.0f;
	sceneObject.materialData->reflectionProbeIntensity = 0.0f;
	sceneObject.materialData->reflectionReserved = 0.0f;
	sceneObject.materialData->materialPadding0 = 0.0f;
	sceneObject.materialData->materialPadding1 = 0.0f;
	sceneObject.materialData->reflectionProbeCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.materialData->reflectionProbeBoxProjection = 0.0f;
	sceneObject.materialData->reflectionProbeExtent = {1.0f, 1.0f, 1.0f};
	sceneObject.materialData->materialPadding2 = 0.0f;
	sceneObject.materialData->uvTransform = MakeIdentity4x4();
	sceneObject.materialData->normalScale = 1.0f;
	sceneObject.materialData->ambientOcclusionStrength = 1.0f;
	sceneObject.materialData->heightScale = 0.02f;
	sceneObject.materialData->alphaCutoff = 0.5f;
	sceneObject.materialData->clearCoat = 0.0f;
	sceneObject.materialData->clearCoatRoughness = 0.1f;
	sceneObject.materialData->transmission = 0.0f;
	sceneObject.materialData->subsurface = 0.0f;
	sceneObject.materialData->anisotropy = 0.0f;
	sceneObject.materialData->anisotropyRotation = 0.0f;
	sceneObject.materialData->specularTint = 0.0f;
	sceneObject.materialData->sheen = 0.0f;
	sceneObject.materialData->emissionColor = {1.0f, 1.0f, 1.0f};
	sceneObject.materialData->sheenTint = 0.5f;
	sceneObject.materialData->useNormalMap = FALSE;
	sceneObject.materialData->useMetallicMap = FALSE;
	sceneObject.materialData->useRoughnessMap = FALSE;
	sceneObject.materialData->useAmbientOcclusionMap = FALSE;
	sceneObject.materialData->useEmissionMap = FALSE;
	sceneObject.materialData->useHeightMap = FALSE;
	sceneObject.materialData->useOpacityMap = FALSE;
	sceneObject.materialData->alphaMode = 0;
	sceneObject.materialData->doubleSided = FALSE;
	sceneObject.materialData->materialExtensionPadding0 = 0.0f;
	sceneObject.materialData->materialExtensionPadding1 = 0.0f;
	sceneObject.materialData->materialExtensionPadding2 = 0.0f;
	sceneObject.materialData->uvTiling = {1.0f, 1.0f};
	sceneObject.materialData->uvOffset = {0.0f, 0.0f};
	sceneObject.cullMode = 0;
	sceneObjects_.push_back(sceneObject);

	// 追加した配列番号を呼び出し側の選択管理へ返す
	return static_cast<int32_t>(sceneObjects_.size() - 1);
}

bool EditorSceneObjectManager::SetCustomTexture(int32_t sceneObjectIndex, const std::string& textureAssetPath) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		device_ == nullptr ||
		textureAssetPath.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	if (sceneObject.textureAssetPath == textureAssetPath &&
		sceneObject.customTextureResource != nullptr &&
		sceneObject.customTextureSrvGpuHandle.ptr != 0u) {
		return true;
	}

	ClearCustomTexture(sceneObjectIndex);
	const bool isLoaded = LoadTextureResource(
		textureAssetPath,
		sceneObject.customTextureResource,
		sceneObject.customTextureUploadResource,
		sceneObject.customTextureSrvGpuHandle,
		sceneObject.customTextureDescriptorIndex);

	if (isLoaded) {
		sceneObject.textureAssetPath = textureAssetPath;
	}

	return isLoaded;
}

bool EditorSceneObjectManager::SetMaterialTexture(
	int32_t sceneObjectIndex,
	EditorMaterialTextureSlot textureSlot,
	const std::string& textureAssetPath) {
	const int32_t textureSlotIndex = static_cast<int32_t>(textureSlot);
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		textureSlotIndex < 0 ||
		textureSlotIndex >= static_cast<int32_t>(EditorMaterialTextureSlot::Count) ||
		textureAssetPath.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	const size_t textureSlotArrayIndex = static_cast<size_t>(textureSlotIndex);
	if (sceneObject.materialTextureAssetPaths[textureSlotArrayIndex] == textureAssetPath &&
		sceneObject.materialTextureResources[textureSlotArrayIndex] != nullptr &&
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex].ptr != 0u) {
		return true;
	}

	ClearMaterialTexture(sceneObjectIndex, textureSlot);
	const bool isLoaded = LoadTextureResource(
		textureAssetPath,
		sceneObject.materialTextureResources[textureSlotArrayIndex],
		sceneObject.materialTextureUploadResources[textureSlotArrayIndex],
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex],
		sceneObject.materialTextureDescriptorIndices[textureSlotArrayIndex]);

	if (isLoaded) {
		sceneObject.materialTextureAssetPaths[textureSlotArrayIndex] = textureAssetPath;
	}

	return isLoaded;
}

void EditorSceneObjectManager::ClearCustomTexture(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	ReleaseTextureResource(
		sceneObject.customTextureResource,
		sceneObject.customTextureUploadResource,
		sceneObject.customTextureSrvGpuHandle,
		sceneObject.customTextureDescriptorIndex);
	sceneObject.textureAssetPath.clear();
}

void EditorSceneObjectManager::ClearMaterialTexture(
	int32_t sceneObjectIndex,
	EditorMaterialTextureSlot textureSlot) {
	const int32_t textureSlotIndex = static_cast<int32_t>(textureSlot);
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		textureSlotIndex < 0 ||
		textureSlotIndex >= static_cast<int32_t>(EditorMaterialTextureSlot::Count)) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	const size_t textureSlotArrayIndex = static_cast<size_t>(textureSlotIndex);
	ReleaseTextureResource(
		sceneObject.materialTextureResources[textureSlotArrayIndex],
		sceneObject.materialTextureUploadResources[textureSlotArrayIndex],
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex],
		sceneObject.materialTextureDescriptorIndices[textureSlotArrayIndex]);
	sceneObject.materialTextureAssetPaths[textureSlotArrayIndex].clear();
}

void EditorSceneObjectManager::ClearAllMaterialTextures(int32_t sceneObjectIndex) {
	for (int32_t textureSlotIndex = 0;
		 textureSlotIndex < static_cast<int32_t>(EditorMaterialTextureSlot::Count);
		 textureSlotIndex++) {
		ClearMaterialTexture(sceneObjectIndex, static_cast<EditorMaterialTextureSlot>(textureSlotIndex));
	}
}

bool EditorSceneObjectManager::LoadTextureResource(
	const std::string& textureAssetPath,
	ID3D12Resource*& textureResource,
	ID3D12Resource*& uploadResource,
	D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
	int32_t& descriptorIndex) {
	if (device_ == nullptr || textureAssetPath.empty() || !std::filesystem::exists(textureAssetPath)) {
		return false;
	}

	descriptorIndex = AcquireCustomTextureDescriptorIndex();
	if (descriptorIndex < 0) {
		return false;
	}

	using namespace EditorSharedState;

	DirectX::ScratchImage mipImages = LoadTexture(ConvertString(textureAssetPath));
	const DirectX::TexMetadata textureMetadata = mipImages.GetMetadata();
	textureResource = CreateTextureResource(device_, textureMetadata);
	if (textureResource == nullptr) {
		ReleaseCustomTextureDescriptorIndex(descriptorIndex);
		descriptorIndex = -1;
		return false;
	}

	HRESULT commandResult = g_commandAllocator->Reset();
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	commandResult = g_commandList->Reset(g_commandAllocator.Get(), nullptr);
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	uploadResource = UploadTextureData(device_, g_commandList.Get(), textureResource, mipImages);
	if (uploadResource == nullptr) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	commandResult = g_commandList->Close();
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	ID3D12CommandList* commandLists[] = {g_commandList.Get()};
	g_commandQueue->ExecuteCommandLists(1, commandLists);
	g_fenceValue++;
	commandResult = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	if (g_fence->GetCompletedValue() < g_fenceValue) {
		commandResult = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
		if (FAILED(commandResult)) {
			ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
			return false;
		}

		WaitForSingleObject(g_fenceEvent, INFINITE);
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureMetadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadata.mipLevels);

	const D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle =
		GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, static_cast<UINT>(descriptorIndex));
	srvGpuHandle =
		GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, static_cast<UINT>(descriptorIndex));
	device_->CreateShaderResourceView(textureResource, &srvDesc, srvCpuHandle);
	return true;
}

void EditorSceneObjectManager::ReleaseTextureResource(
	ID3D12Resource*& textureResource,
	ID3D12Resource*& uploadResource,
	D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
	int32_t& descriptorIndex) {
	if (uploadResource != nullptr) {
		uploadResource->Release();
		uploadResource = nullptr;
	}

	if (textureResource != nullptr) {
		textureResource->Release();
		textureResource = nullptr;
	}

	if (descriptorIndex >= 0) {
		ReleaseCustomTextureDescriptorIndex(descriptorIndex);
	}

	srvGpuHandle = {};
	descriptorIndex = -1;
}

bool EditorSceneObjectManager::SetCustomModelMesh(
	int32_t sceneObjectIndex,
	const std::string& assetPath,
	const ModelData& modelData) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		modelData.vertices.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	ClearCustomModelMesh(sceneObjectIndex);

	const size_t vertexBufferSize = sizeof(VertexData) * modelData.vertices.size();
	sceneObject.customMeshVertexResource = CreateVertexResource(vertexBufferSize);
	if (sceneObject.customMeshVertexResource == nullptr) {
		return false;
	}

	VertexData* mappedVertexData = nullptr;
	const HRESULT mapResult = sceneObject.customMeshVertexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedVertexData));
	if (FAILED(mapResult) || mappedVertexData == nullptr) {
		sceneObject.customMeshVertexResource->Release();
		sceneObject.customMeshVertexResource = nullptr;
		return false;
	}

	std::memcpy(mappedVertexData, modelData.vertices.data(), vertexBufferSize);
	sceneObject.customMeshVertexBufferView.BufferLocation =
		sceneObject.customMeshVertexResource->GetGPUVirtualAddress();
	sceneObject.customMeshVertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
	sceneObject.customMeshVertexBufferView.StrideInBytes = sizeof(VertexData);
	sceneObject.customMeshVertexCount = static_cast<uint32_t>(modelData.vertices.size());
	sceneObject.customMeshLocalBoundsCenter = modelData.localBoundsCenter;
	sceneObject.customMeshLocalBoundsSize = modelData.localBoundsSize;
	sceneObject.assetPath = assetPath;
	sceneObject.usesCustomMesh = true;
	return true;
}

void EditorSceneObjectManager::ClearCustomModelMesh(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	if (sceneObject.customMeshVertexResource != nullptr) {
		sceneObject.customMeshVertexResource->Release();
		sceneObject.customMeshVertexResource = nullptr;
	}
	sceneObject.customMeshVertexBufferView = {};
	sceneObject.customMeshVertexCount = 0u;
	sceneObject.customMeshLocalBoundsCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.customMeshLocalBoundsSize = {1.0f, 1.0f, 1.0f};
	sceneObject.usesCustomMesh = false;
	sceneObject.assetPath.clear();
}

void EditorSceneObjectManager::ReleaseObject(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];  // 指定番号の描画用 GPU Resource を解放する
	ClearCustomTexture(sceneObjectIndex);
	ClearAllMaterialTextures(sceneObjectIndex);
	ClearCustomModelMesh(sceneObjectIndex);
	if (sceneObject.transformationResource != nullptr) {
		sceneObject.transformationResource->Release();
		sceneObject.transformationResource = nullptr;
		sceneObject.transformationData = nullptr;
	}
	if (sceneObject.gameTransformationResource != nullptr) {
		sceneObject.gameTransformationResource->Release();
		sceneObject.gameTransformationResource = nullptr;
		sceneObject.gameTransformationData = nullptr;
	}
	if (sceneObject.materialResource != nullptr) {
		sceneObject.materialResource->Release();
		sceneObject.materialResource = nullptr;
		sceneObject.materialData = nullptr;
	}
}

void EditorSceneObjectManager::ReleaseAll() {
	// 各要素の GPU Resource を先に解放する
	for (int32_t sceneObjectIndex = 0;
	     sceneObjectIndex < static_cast<int32_t>(sceneObjects_.size());
	     sceneObjectIndex++) {
		ReleaseObject(sceneObjectIndex);
	}

	sceneObjects_.clear();  // Resource 解放後に配列自体を空にする
}

std::vector<EditorSceneObject>& EditorSceneObjectManager::GetSceneObjects() {
	return sceneObjects_;
}

const std::vector<EditorSceneObject>& EditorSceneObjectManager::GetSceneObjects() const {
	return sceneObjects_;
}

int32_t EditorSceneObjectManager::AcquireCustomTextureDescriptorIndex() {
	if (!freeCustomTextureDescriptorIndices_.empty()) {
		int32_t descriptorIndex = freeCustomTextureDescriptorIndices_.back();
		freeCustomTextureDescriptorIndices_.pop_back();
		return descriptorIndex;
	}

	if (nextCustomTextureDescriptorIndex_ >= 1024) {
		return -1;
	}

	int32_t descriptorIndex = nextCustomTextureDescriptorIndex_;
	nextCustomTextureDescriptorIndex_++;
	return descriptorIndex;
}

void EditorSceneObjectManager::ReleaseCustomTextureDescriptorIndex(int32_t descriptorIndex) {
	if (descriptorIndex < 128) {
		return;
	}

	if (std::find(
		    freeCustomTextureDescriptorIndices_.begin(),
		    freeCustomTextureDescriptorIndices_.end(),
		    descriptorIndex) != freeCustomTextureDescriptorIndices_.end()) {
		return;
	}

	freeCustomTextureDescriptorIndices_.push_back(descriptorIndex);
}

ID3D12Resource* EditorSceneObjectManager::CreateVertexResource(size_t sizeInBytes) const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr || sizeInBytes == 0u) {
		return nullptr;
	}

	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	const HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}

ID3D12Resource* EditorSceneObjectManager::CreateTransformationResource() const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr) {
		return nullptr;
	}

	// CPU から書き込むため Upload Heap に作る
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// TransformationMatrix 1 個分だけ入る Buffer Resource
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(TransformationMatrix);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	// ConstantBuffer 用の Upload Buffer を作成する
	HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}

ID3D12Resource* EditorSceneObjectManager::CreateMaterialResource() const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr) {
		return nullptr;
	}

	// CPU から毎フレーム色や Texture 使用設定を書き換えるため Upload Heap に作る
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// Material 1 個分だけ入る Buffer Resource
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(Material);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	// PixelShader の b0 へ渡す Material 用 Upload Buffer を作成する
	HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}
