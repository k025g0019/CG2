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
// 後段の AO / Reflection が参照する GBuffer を管理するクラス
//================================================================

class EditorGBufferManager {
public:
	static constexpr uint32_t kRenderTargetCount = 4u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		ID3D12RootSignature* objectRootSignature,
		IDxcBlob* vertexShaderBlob,
		IDxcBlob* pixelShaderBlob,
		const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
		UINT inputElementCount,
		uint32_t renderWidth,
		uint32_t renderHeight);

	bool Resize(uint32_t renderWidth, uint32_t renderHeight);
	bool Begin(
		ID3D12GraphicsCommandList* commandList,
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewHandle);
	void BindPipelineState(ID3D12GraphicsCommandList* commandList, bool isDoubleSided) const;
	void End(ID3D12GraphicsCommandList* commandList);
	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetEmissionSrvHandle() const;
	bool IsReady() const;

private:
	enum class RenderTargetType : uint32_t {
		Albedo,
		Normal,
		Material,
		Emission,
		Count,
	};

	bool CreatePipelineStates(
		IDxcBlob* vertexShaderBlob,
		IDxcBlob* pixelShaderBlob,
		const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
		UINT inputElementCount);
	bool CreateSizeDependentResources(uint32_t renderWidth, uint32_t renderHeight);
	void ReleaseSizeDependentResources();
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvDescriptorHandle(uint32_t renderTargetIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;
	UINT rtvDescriptorSize_ = 0u;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> objectRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> doubleSidedPipelineState_;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kRenderTargetCount> resources_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kRenderTargetCount> srvHandles_{};
	uint32_t renderWidth_ = 0u;
	uint32_t renderHeight_ = 0u;
	bool isInitialized_ = false;
	bool isRendering_ = false;
};
