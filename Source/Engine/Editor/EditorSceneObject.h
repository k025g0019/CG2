#pragma once

#include "EditorCommonTypes.h"

#pragma warning(push, 0)
#include <d3d12.h>
#pragma warning(pop)

#include <cstdint>
#include <array>
#include <string>
#include <vector>

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

//============================================================
// PBR Material が追加で参照する画像スロット
//============================================================

enum class EditorMaterialTextureSlot : int32_t {
	Normal = 0,
	Metallic,
	Roughness,
	AmbientOcclusion,
	Emission,
	Height,
	Opacity,
	Count,
};

constexpr size_t kEditorMaterialTextureSlotCount =
	static_cast<size_t>(EditorMaterialTextureSlot::Count);

//============================================================
// Ocean Component から描画側へ渡す軽量設定
//============================================================

struct EditorOceanRenderSettings {
	bool isEnabled = false;  // true の SceneObject だけ海面変形する
	int32_t gridResolution = 2048;  // カメラ近傍の仮想分割数。実頂点数は連続 LOD で抑える
	float size = 240.0f;  // 近傍 LOD の基準寸法。描画外周は地平線方向へ自動拡張する
	float waveHeight = 1.8f;  // 主波の高さ
	float maxWaveHeight = 4.5f;  // 合成後の高さ上限
	float waveLength = 28.0f;  // 主波の波長
	float waveSpeed = 1.0f;  // 波位相の進行速度
	float timeScale = 1.0f;  // 海面時間の倍率
	float choppiness = 0.65f;  // 水平変位量
	Vector2 primaryDirection{1.0f, 0.28f};  // 主波の XZ 方向
	Vector2 secondaryDirection{-0.45f, 1.0f};  // 副波の XZ 方向
	float secondaryWaveScale = 0.45f;  // 副波レイヤの強さ
	float rippleScale = 0.22f;  // 細波の波長比
	float rippleStrength = 0.12f;  // 細波の高さ比
	float windSpeed = 14.0f;  // スペクトルへ与える風速
	float waterDepth = 80.0f;  // 有限水深の分散計算に使う水深
	float directionSpread = 0.35f;  // 波方向の散らばり
	float swellStrength = 0.65f;  // 長いうねり帯域の強さ
	float spectrumSeed = 7.0f;  // 波成分を固定生成するシード
	float crestSharpness = 0.65f;  // 波頭の尖り
	float foamStrength = 1.0f;  // 泡の強さ
	float foamThreshold = 0.58f;  // 圧縮泡が出始める閾値
	float roughness = 0.12f;  // 海面反射の粗さ
	float reflectionStrength = 0.85f;  // 環境反射強度
	float detailNormalStrength = 0.45f;  // ピクセル単位の微細波法線強度
	float absorptionDistance = 18.0f;  // 水中の吸収距離
	float refractionDistortion = 0.08f;  // 屈折方向の歪み
	float transmission = 0.18f;  // 水面材質の透過・屈折量
	Vector3 shallowColor{0.04f, 0.34f, 0.46f};  // 浅い部分の色
	Vector3 deepColor{0.005f, 0.045f, 0.11f};  // 深い部分の色
	std::array<Vector4, 16u> waveData0{};  // xy=方向、z=波数、w=振幅
	std::array<Vector4, 16u> waveData1{};  // x=角周波数、y=位相、zw=予約
};

//============================================================
// Terrain / Foliage の軽量描画設定
//============================================================

struct EditorSurfaceRenderSettings {
	int32_t mode = 0;  // 0=通常、1=Terrain、2=Foliage
	float windStrength = 0.0f;  // Foliage 頂点の最大変位量
	float windSpeed = 0.0f;  // 風位相の進行速度
	float windSpatialScale = 1.0f;  // ワールド座標に掛ける風の空間周波数
	float timeScale = 1.0f;  // Editor 共通時刻へ掛ける倍率
	Vector2 windDirection{1.0f, 0.0f};  // 風が流れる XZ 方向
	Vector2 areaSize{1.0f, 1.0f};  // Terrain / Foliage を配置するローカル XZ 範囲
	float heightScale = 1.0f;  // Terrain HeightMap の最大高低差
	float density = 1.0f;  // DensityMap へ掛ける Foliage 全体密度
	float lodDistance = 80.0f;  // Foliage の描画終了距離
	uint32_t instanceCount = 1u;  // Foliage の GPU Instance 上限
	bool hasHeightOrDensityMap = false;  // t12 に実画像が割り当てられている時だけ true
	std::array<uint32_t, 3u> terrainLodVertexOffsets{};  // 近・中・遠 LOD の先頭頂点
	std::array<uint32_t, 3u> terrainLodVertexCounts{};  // 近・中・遠 LOD の頂点数
};

struct EditorSceneObject {
	EditorSceneObjectType type;  // モデル描画かスプライト描画かを選ぶ種類
	EditorModelMeshType meshType;  // Model の場合に使う基本形メッシュ種類
	bool usesCustomMesh;  // true なら内部基本形ではなく、読み込んだ実メッシュの頂点バッファを使う
	int32_t gameObjectId;  // 対応する EditorGameObject の ID
	int32_t textureIndex;  // SRV 配列内で使う Texture 番号
	Transforms transform;  // SceneView ギズモで編集する分解済みワールドTransform
	Matrix4x4 worldMatrix;  // 親子SRTを合成した描画用ワールド行列
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
	std::array<std::string, kEditorMaterialTextureSlotCount> materialTextureAssetPaths;  // PBR Map ごとの画像パス
	std::array<ID3D12Resource*, kEditorMaterialTextureSlotCount> materialTextureResources;  // PBR Map ごとの Texture Resource
	std::array<ID3D12Resource*, kEditorMaterialTextureSlotCount> materialTextureUploadResources;  // PBR Map 転送用 Upload Buffer
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kEditorMaterialTextureSlotCount> materialTextureSrvGpuHandles;  // PBR Map ごとの SRV
	std::array<int32_t, kEditorMaterialTextureSlotCount> materialTextureDescriptorIndices;  // PBR Map ごとの SRV 番号
	ID3D12Resource* customMeshVertexResource;  // 読み込んだ実メッシュを GPU へ渡す頂点バッファ
	D3D12_VERTEX_BUFFER_VIEW customMeshVertexBufferView;  // 実メッシュ描画時に IASetVertexBuffers へ渡す View
	uint32_t customMeshVertexCount;  // DrawInstanced に渡す実メッシュの頂点数
	ID3D12Resource* customMeshIndexResource;  // 共有頂点メッシュの Index を GPU へ渡すバッファ
	D3D12_INDEX_BUFFER_VIEW customMeshIndexBufferView;  // DrawIndexedInstanced 用の Index Buffer View
	uint32_t customMeshIndexCount;  // 0 なら従来どおり非 Index 描画する
	ID3D12Resource* currentSkinMatrixResource;  // 現在フレームの Bone 行列を頂点 Shader へ渡す StructuredBuffer
	Matrix4x4* currentSkinMatrixData;  // currentSkinMatrixResource の Map 済み書き込み先
	ID3D12Resource* previousSkinMatrixResource;  // 前フレームの Bone 行列を Motion Vector へ渡す StructuredBuffer
	Matrix4x4* previousSkinMatrixData;  // previousSkinMatrixResource の Map 済み書き込み先
	uint32_t skinMatrixCount;  // GPU へ確保済みの Bone 行列数
	int32_t currentSkinClipIndex;  // 最後に書き込んだ Animation Clip 番号
	float currentSkinTime;  // 同一姿勢を複数描画パスで進めないための最終再生秒
	std::string currentSkinMaskPath;  // 現在適用中の AvatarMask アセットまたは Bone 名リスト
	std::vector<std::string> currentSkinMaskBoneNames;  // AvatarMask が Animation を許可する Bone 名
	bool usesSkinMask = false;  // 読み込み済みの AvatarMask が有効なら true
	bool usesSkinning;  // Bone Weight と姿勢 Buffer が有効なモデルだけ true
	Vector3 customMeshLocalBoundsCenter;  // 実メッシュのローカル包囲中心
	Vector3 customMeshLocalBoundsSize;  // 実メッシュのローカル包囲サイズ
	int32_t cullMode = 0;  // 0=Default(BACK), 1=Front(反転メッシュ), 2=None(両面)
	EditorOceanRenderSettings ocean;  // Ocean Component の描画値。通常モデルでは無効
	EditorSurfaceRenderSettings surface;  // Terrain / Foliage の頂点・材質切り替え値
};

#pragma warning(pop)
