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
// 多段 Bloom、複数 Glare、3 パス SMAA を管理するクラス
//================================================================

class EditorPostProcessQualityManager {
public:
	static constexpr uint32_t kPipelineCount = 8u;
	static constexpr uint32_t kRootConstantCount = 16u;

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
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		float bloomIntensity = 1.0f,
		float bloomThreshold = 1.0f,
		float bloomSoftKnee = 0.5f,
		float bloomScatter = 0.72f);
	bool ExecuteSmaa(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		float threshold = 0.10f,
		float cornerRounding = 25.0f);
	bool ExecuteGlare(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		int32_t glareMode,
		float intensity,
		float size,
		float angleDegrees,
		int32_t streakCount,
		float fade,
		float colorModulation,
		float centerX,
		float centerY,
		float colorR,
		float colorG,
		float colorB,
		bool preserveSource);
	bool ExecuteFilter(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		int32_t filterMode,
		float strength,
		float colorR,
		float colorG,
		float colorB);

	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetBloomSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGlareSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetFilterSrvHandle() const;
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
		GlareOutputA,
		GlareOutputB,
		FilterOutputA,
		FilterOutputB,
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
		const std::array<float, kRootConstantCount>& constants);

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
	ResourceType lastGlareOutputResourceType_ = ResourceType::GlareOutputA;  // 複数 Glare の最後に書いた出力を保持する
	ResourceType lastFilterOutputResourceType_ = ResourceType::FilterOutputA;  // 複数 Filter の最後に書いた出力を保持する
	bool isInitialized_ = false;
};
