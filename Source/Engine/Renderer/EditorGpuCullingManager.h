#pragma once

#pragma warning(push, 0)
#include <Windows.h>
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
};

//================================================================
// Hi-Z 遮蔽判定と間接描画引数生成を管理するクラス
//================================================================

class EditorGpuCullingManager {
public:
	static constexpr uint32_t kMaximumObjectCount = 2048u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* frustumCullingShaderBlob,
		IDxcBlob* occlusionCullingShaderBlob,
		IDxcBlob* buildIndirectArgsShaderBlob);

	bool Execute(
		ID3D12GraphicsCommandList* commandList,
		const std::vector<EditorGpuCullingInput>& cullingInputs,
		D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrvHandle,
		const float* viewProjectionMatrix,
		uint32_t depthPyramidWidth,
		uint32_t depthPyramidHeight);

	void ResolveReadback();
	void Finalize();

	bool IsVisible(int32_t gameObjectId) const;

private:
	struct DrawArguments {
		uint32_t vertexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startVertexLocation;
		uint32_t startInstanceLocation;
	};

	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* frustumCullingShaderBlob,
		IDxcBlob* occlusionCullingShaderBlob,
		IDxcBlob* buildIndirectArgsShaderBlob);
	bool CreateBuffers();

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(uint32_t descriptorIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> frustumCullingPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> occlusionCullingPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> buildIndirectArgsPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> objectUploadResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> frustumVisibilityResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> visibilityResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> drawArgumentsResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> drawArgumentsReadbackResource_;

	D3D12_GPU_DESCRIPTOR_HANDLE objectSrvHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE frustumVisibilitySrvHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE frustumVisibilityUavHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE visibilitySrvHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE visibilityUavHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE drawArgumentsUavHandle_{};
	std::vector<int32_t> submittedGameObjectIds_;
	std::unordered_map<int32_t, bool> visibilityByGameObjectId_;
	uint32_t submittedObjectCount_ = 0u;
	bool hasPendingReadback_ = false;
	bool isInitialized_ = false;
};
