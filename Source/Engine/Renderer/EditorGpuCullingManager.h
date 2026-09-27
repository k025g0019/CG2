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

//================================================================
// GPU へ渡す GameObject のワールド AABB と描画情報
//================================================================

struct EditorGpuCullingInput {
	float boundsCenterX;
	float boundsCenterY;
	float boundsCenterZ;
	float boundsExtentX;
	float boundsExtentY;
	float boundsExtentZ;
	int32_t gameObjectId;
	uint32_t vertexCount;
	uint32_t indexCount;
	uint32_t isIndexed;
};

enum class EditorGpuCullingView : uint32_t {
	Scene = 0u,
	Game,
	Count
};

//================================================================
// Hi-Z 遮蔽判定と間接描画引数生成を管理するクラス
//================================================================

class EditorGpuCullingManager {
public:
	static constexpr uint32_t kMaximumObjectCount = 16384u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* frustumCullingShaderBlob,
		IDxcBlob* occlusionCullingShaderBlob,
		IDxcBlob* buildIndirectArgsShaderBlob);

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

	void ResolveReadback();  // 旧呼出互換。GPU Predication方式ではCPU読戻しを行わない。
	void Finalize();

	bool IsVisible(EditorGpuCullingView view, int32_t gameObjectId) const;
	bool GetSubmittedObjectIndex(
		EditorGpuCullingView view,
		int32_t gameObjectId,
		uint32_t& objectIndex) const;
	D3D12_GPU_VIRTUAL_ADDRESS GetVisibilityGpuAddress(EditorGpuCullingView view) const;
	bool BeginPredication(
		ID3D12GraphicsCommandList* commandList,
		EditorGpuCullingView view,
		int32_t gameObjectId) const;  // View固有のGPU結果を現在Drawの条件に設定する。
	void EndPredication(ID3D12GraphicsCommandList* commandList) const;  // 後続Drawへ条件を漏らさない。
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
