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
// 多段 Bloom と 3 パス SMAA を管理するクラス
//================================================================

class EditorPostProcessQualityManager {
public:
	static constexpr uint32_t kPipelineCount = 6u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* fullscreenVertexShaderBlob,
		const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs,
		uint32_t renderWidth,
		uint32_t renderHeight);

	bool Resize(uint32_t renderWidth, uint32_t renderHeight);
	bool ExecuteBloom(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle);
	bool ExecuteSmaa(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle);

	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetBloomSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSmaaOutputSrvHandle() const;

private:
	enum class ResourceType : uint32_t {
		BloomDown0,
		BloomDown1,
		BloomDown2,
		BloomDown3,
		BloomUp0,
		BloomUp1,
		BloomUp2,
		SmaaEdges,
		SmaaWeights,
		SmaaOutput,
		Count,
	};

	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* fullscreenVertexShaderBlob,
		const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs);
	bool CreateSizeDependentResources(uint32_t renderWidth, uint32_t renderHeight);
	void ReleaseSizeDependentResources();
	bool DrawPass(
		ID3D12GraphicsCommandList* commandList,
		uint32_t pipelineIndex,
		ResourceType destinationResourceType,
		D3D12_GPU_DESCRIPTOR_HANDLE source0SrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE source1SrvHandle,
		const std::array<float, 8u>& constants);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvDescriptorHandle(uint32_t resourceIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;
	UINT rtvDescriptorSize_ = 0u;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kPipelineCount> pipelineStates_{};
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, static_cast<size_t>(ResourceType::Count)> resources_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, static_cast<size_t>(ResourceType::Count)> srvHandles_{};
	std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceWidths_{};
	std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceHeights_{};
	uint32_t renderWidth_ = 0u;
	uint32_t renderHeight_ = 0u;
	bool isInitialized_ = false;
};
