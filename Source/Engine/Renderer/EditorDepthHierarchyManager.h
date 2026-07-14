#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#pragma warning(pop)

//================================================================
// Scene 深度を GPU の後段処理で再利用するための管理クラス
//================================================================

class EditorDepthHierarchyManager {
public:
	static constexpr uint32_t kMaxDepthPyramidLevelCount = 12u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* depthPyramidShaderBlob,
		IDxcBlob* depthDownsampleShaderBlob,
		IDxcBlob* reconstructNormalShaderBlob,
		uint32_t renderWidth,
		uint32_t renderHeight);

	bool Resize(uint32_t renderWidth, uint32_t renderHeight);

	bool Generate(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvHandle,
		const float* inverseViewProjectionMatrix);

	void Finalize();

	uint32_t GetActiveLevelCount() const;
	uint32_t GetDepthPyramidWidth(uint32_t levelIndex) const;
	uint32_t GetDepthPyramidHeight(uint32_t levelIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetDepthPyramidSrvHandle(uint32_t levelIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetReconstructedNormalSrvHandle() const;

private:
	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* depthPyramidShaderBlob,
		IDxcBlob* depthDownsampleShaderBlob,
		IDxcBlob* reconstructNormalShaderBlob);

	bool CreateDepthPyramidResources(uint32_t renderWidth, uint32_t renderHeight);
	bool CreateReconstructedNormalResource(uint32_t renderWidth, uint32_t renderHeight);
	void ReleaseSizeDependentResources();

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(uint32_t descriptorIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPyramidPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> depthDownsamplePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> reconstructNormalPipelineState_;

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxDepthPyramidLevelCount> depthPyramidResources_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMaxDepthPyramidLevelCount> depthPyramidSrvHandles_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kMaxDepthPyramidLevelCount> depthPyramidUavHandles_{};
	std::array<uint32_t, kMaxDepthPyramidLevelCount> depthPyramidWidths_{};
	std::array<uint32_t, kMaxDepthPyramidLevelCount> depthPyramidHeights_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> reconstructedNormalResource_;
	D3D12_GPU_DESCRIPTOR_HANDLE reconstructedNormalSrvHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE reconstructedNormalUavHandle_{};

	uint32_t renderWidth_ = 0u;
	uint32_t renderHeight_ = 0u;
	uint32_t activeLevelCount_ = 0u;
	bool isInitialized_ = false;
};
