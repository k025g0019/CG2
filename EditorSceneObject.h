#pragma once

#include "EditorCommonTypes.h"

#pragma warning(push, 0)
#include <d3d12.h>
#pragma warning(pop)

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// エディタ上で見た目を持つ SceneObject
//============================================================

enum class EditorSceneObjectType {
	// OBJ / FBX など 3D モデルとして描画する
	Model,
	// Texture を 2D スプライトとして描画する
	Sprite,
};

enum class EditorModelMeshType {
	// plane.obj または未対応モデルの仮表示
	Plane,
	// resources/UVCube.fbx に対応する立方体
	Cube,
	// resources/box.fbx に対応する横長 Box
	Box,
	// resources/cylinder.fbx に対応する円柱
	Cylinder,
	// resources/cone.fbx に対応する円錐
	Cone,
	// resources/to-tasu.fbx に対応するトーラス
	Torus,
	// resources/ICOCube.fbx に対応する低ポリゴン形状
	Ico,
	// resources/sphere.fbx に対応する球
	Sphere,
	// 内部配列の要素数
	Count,
};

struct EditorSceneObject {
	EditorSceneObjectType type;  // モデル描画かスプライト描画かを選ぶ種類
	EditorModelMeshType meshType;  // Model の場合に使う基本形メッシュ種類
	bool usesCustomMesh;  // true なら内部基本形ではなく、読み込んだ実メッシュの頂点バッファを使う
	int32_t gameObjectId;  // 対応する EditorGameObject の ID
	int32_t textureIndex;  // SRV 配列内で使う Texture 番号
	Transforms transform;  // SceneView 上で編集する Transform
	std::string name;  // Hierarchy / Project 表示用の名前
	std::string assetPath;  // この SceneObject が参照しているモデルアセットのパス
	ID3D12Resource* transformationResource;  // WVP / World を GPU へ渡す ConstantBuffer
	TransformationMatrix* transformationData;  // transformationResource を CPU から書き込むための Map 済みポインタ
	ID3D12Resource* gameTransformationResource;  // GameView 用の WVP / World を渡す ConstantBuffer
	TransformationMatrix* gameTransformationData;  // gameTransformationResource を CPU から書き込むための Map 済みポインタ
	ID3D12Resource* materialResource;  // GameObject ごとの色と Texture 使用有無を GPU へ渡す ConstantBuffer
	Material* materialData;  // materialResource を CPU から書き込むための Map 済みポインタ
	std::string textureAssetPath;  // Model / Sprite が明示的に使う画像パス
	ID3D12Resource* customTextureResource;  // 個別画像を GPU へ載せる Texture Resource
	ID3D12Resource* customTextureUploadResource;  // customTextureResource へ転送する中間 Upload Buffer
	D3D12_GPU_DESCRIPTOR_HANDLE customTextureSrvGpuHandle;  // Draw 時にそのまま渡す個別 Texture SRV
	int32_t customTextureDescriptorIndex;  // SRV Heap 内の割り当て番号
	ID3D12Resource* customMeshVertexResource;  // 読み込んだ実メッシュを GPU へ渡す頂点バッファ
	D3D12_VERTEX_BUFFER_VIEW customMeshVertexBufferView;  // 実メッシュ描画時に IASetVertexBuffers へ渡す View
	uint32_t customMeshVertexCount;  // DrawInstanced に渡す実メッシュの頂点数
};

#pragma warning(pop)
