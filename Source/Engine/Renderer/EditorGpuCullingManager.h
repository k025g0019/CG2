#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#pragma warning(pop)

//========================================
// GPU可視判定入力
//========================================

struct EditorGpuCullingInput {
	float boundsCenterX;  // World AABB中心。Local座標のBoundsをそのまま渡さない。
	float boundsCenterY;
	float boundsCenterZ;
	float boundsExtentX;  // 中心から各軸端までの半径。GPU側のPlane判定に使用する。
	float boundsExtentY;
	float boundsExtentZ;
	int32_t gameObjectId; // GPU上の配列IndexとScene上のObjectを対応付ける安定ID。
	uint32_t vertexCount; // 非Index描画用の頂点数。不可視時は間接引数側で0になる。
	uint32_t indexCount;
	uint32_t isIndexed;   // 0はDraw、1はDrawIndexed用の引数を生成する。
};

enum class EditorGpuCullingView : uint32_t {
	Scene = 0u,
	Game,
	Count
};

//========================================
// GPU可視判定管理
//========================================

class EditorGpuCullingManager {
public:
	static constexpr uint32_t kMaximumObjectCount = 16384u;

	//------------------------------
	// 固定GPUリソース初期化
	//------------------------------

	// 3種類のCompute ShaderからPSOを作り、Scene/Game ViewごとのBufferと
	// Draw/DrawIndexed用Command Signatureを生成する。途中で1つでも失敗した場合は、
	// 作成済みResourceをFinalizeで戻し、利用可能な状態を残さずfalseを返す。
	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* frustumCullingShaderBlob,
		IDxcBlob* occlusionCullingShaderBlob,
		IDxcBlob* buildIndirectArgsShaderBlob);

	//------------------------------
	// Frame可視判定
	//------------------------------

	// CPUで確定したWorld AABBをUpload Bufferへ転送し、Frustum判定、Hi-Z遮蔽判定、
	// Indirect Argument生成の3 Passを同じCommand Listへ記録する。
	// depthPyramidは現在ViewのScene Depthから生成済みであることが前提。
	// 戻り値trueはCommand記録に成功したことを表し、全Objectが可視という意味ではない。
	bool Execute(
		ID3D12GraphicsCommandList* commandList,
		EditorGpuCullingView view,
		const std::vector<EditorGpuCullingInput>& cullingInputs,
		D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrvHandle,
		const float* viewProjectionMatrix,
		uint32_t depthPyramidWidth,
		uint32_t depthPyramidHeight,
		float viewportUvOffsetX,
		float viewportUvOffsetY,
		float viewportUvScaleX,
		float viewportUvScaleY);

	//------------------------------
	// 終了・描画利用
	//------------------------------

	void ResolveReadback();  // 旧呼出互換。GPU Predication方式ではCPU読戻しを行わない。
	void Finalize();

	// CPUへ可視結果を戻さない設計なので、CPU側の互換APIは常に可視として扱う。
	// 実際の描画抑制はBeginPredicationまたはExecuteIndirectDrawでGPU上から行う。
	bool IsVisible(EditorGpuCullingView view, int32_t gameObjectId) const;

	// Scene上のIDを、最後にExecuteへ提出したGPU配列Indexへ変換する。
	// 未提出、上限超過、別ViewのIDではfalseを返す。
	bool GetSubmittedObjectIndex(
		EditorGpuCullingView view,
		int32_t gameObjectId,
		uint32_t& objectIndex) const;
	// 可視Flag Bufferの先頭GPU Addressを返す。0は未初期化または無効Viewを表す。
	D3D12_GPU_VIRTUAL_ADDRESS GetVisibilityGpuAddress(EditorGpuCullingView view) const;

	// 対象Objectの可視FlagをD3D12 Predication条件として設定する。
	// 成功後は必ずEndPredicationを呼び、後続Objectへ条件を漏らさない。
	bool BeginPredication(
		ID3D12GraphicsCommandList* commandList,
		EditorGpuCullingView view,
		int32_t gameObjectId) const;  // View固有のGPU結果を現在Drawの条件に設定する。
	void EndPredication(ID3D12GraphicsCommandList* commandList) const;  // 後続Drawへ条件を漏らさない。
	// GPUが生成した頂点数・Instance数をCPUで読み戻さず、そのままDrawへ使用する。
	// gameObjectIdに対応するArgumentが無い場合はCommandを記録せずfalseを返す。
	bool ExecuteIndirectDraw(
		ID3D12GraphicsCommandList* commandList,
		EditorGpuCullingView view,
		int32_t gameObjectId) const;  // 可視時だけD3D12_DRAW_ARGUMENTSをGPUから実行する。
	bool ExecuteIndirectDrawIndexed(
		ID3D12GraphicsCommandList* commandList,
		EditorGpuCullingView view,
		int32_t gameObjectId) const;

private:
	struct IndirectArguments {
		uint32_t vertexOrIndexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startVertexOrIndexLocation;
		int32_t baseVertexLocation;
		uint32_t startInstanceLocation;
		uint32_t padding;  // SetPredicationの8-byte offset alignmentを保つ
	};

	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* frustumCullingShaderBlob,
		IDxcBlob* occlusionCullingShaderBlob,
		IDxcBlob* buildIndirectArgsShaderBlob);
	struct ViewResources {
		Microsoft::WRL::ComPtr<ID3D12Resource> objectUploadResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> frustumVisibilityResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> visibilityResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> drawArgumentsResource;
		D3D12_GPU_DESCRIPTOR_HANDLE objectSrvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE frustumVisibilitySrvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE frustumVisibilityUavHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE visibilitySrvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE visibilityUavHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE drawArgumentsUavHandle{};
		std::vector<int32_t> submittedGameObjectIds;
		std::unordered_map<int32_t, uint32_t> submittedObjectIndexByGameObjectId;
		uint32_t submittedObjectCount = 0u;
	};

	bool CreateBuffers(EditorGpuCullingView view);
	ViewResources* GetViewResources(EditorGpuCullingView view);
	const ViewResources* GetViewResources(EditorGpuCullingView view) const;

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(uint32_t descriptorIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> frustumCullingPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> occlusionCullingPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> buildIndirectArgsPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCommandSignature_;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawIndexedCommandSignature_;
	std::array<ViewResources, static_cast<size_t>(EditorGpuCullingView::Count)> viewResources_;
	bool isInitialized_ = false;
};
