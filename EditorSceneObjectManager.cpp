#include "EditorSceneObjectManager.h"

#include "Matrix.h"

#include <cstddef>
#include <cstring>

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
	sceneObject.transformationResource = CreateTransformationResource();
	sceneObject.transformationData = nullptr;
	sceneObject.gameTransformationResource = CreateTransformationResource();
	sceneObject.gameTransformationData = nullptr;
	sceneObject.materialResource = CreateMaterialResource();
	sceneObject.materialData = nullptr;
	sceneObject.customMeshVertexResource = nullptr;
	sceneObject.customMeshVertexBufferView = {};
	sceneObject.customMeshVertexCount = 0u;

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
	sceneObject.gameTransformationData->WVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->World = MakeIdentity4x4();
	sceneObject.materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};  // Mesh の初期色は白。Inspector の Renderer 色で上書きされる。
	sceneObject.materialData->enableLighting =
		type == EditorSceneObjectType::Model ? TRUE : FALSE;  // Model はライトあり、Sprite は Texture 色をそのまま出す。
	sceneObject.materialData->useTexture =
		type == EditorSceneObjectType::Sprite ? TRUE : FALSE;  // Model は白い形状として始め、Sprite だけ Texture を使う。
	sceneObject.materialData->padding[0] = 0.0f;
	sceneObject.materialData->padding[1] = 0.0f;
	sceneObject.materialData->uvTransform = MakeIdentity4x4();
	sceneObjects_.push_back(sceneObject);

	// 追加した配列番号を呼び出し側の選択管理へ返す
	return static_cast<int32_t>(sceneObjects_.size() - 1);
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
	sceneObject.usesCustomMesh = false;
	sceneObject.assetPath.clear();
}

void EditorSceneObjectManager::ReleaseObject(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];  // 指定番号の描画用 GPU Resource を解放する
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
