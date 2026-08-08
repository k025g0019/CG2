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
// 画面空間反射と時間方向履歴を管理する GPU Compute クラス
//================================================================

class EditorTemporalRenderingManager {
public:
	static constexpr uint32_t kPipelineCount = 11u;

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		const std::array<IDxcBlob*, kPipelineCount>& computeShaderBlobs,
		uint32_t renderWidth,
		uint32_t renderHeight);

	bool Resize(uint32_t renderWidth, uint32_t renderHeight);

	bool Execute(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE objectMotionVectorSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE reconstructedNormalSrvHandle,
		const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5u>& depthPyramidSrvHandles,
		D3D12_GPU_DESCRIPTOR_HANDLE materialMaskSrvHandle,
		const float* inverseViewProjectionMatrix,
		const float* viewProjectionMatrix,
		const float* cameraPosition,
		float viewportX,
		float viewportY,
		float viewportWidth,
		float viewportHeight,
		bool ssrEnabled,
		bool temporalEnabled,
		uint32_t viewHistoryIndex,
		bool advanceHistoryFrame,
		float sharpness = 0.08f,
		float blendRatio = 0.90f);

	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSrvHandle() const;
	ID3D12Resource* GetOutputResource() const;  // Auto Exposure等が正しいTemporal出力を遷移できるよう実Resourceを返す。
	D3D12_GPU_DESCRIPTOR_HANDLE GetVelocitySrvHandle() const;

private:
	enum class ResourceType : uint32_t {
		Velocity,
		DilatedVelocity,
		PreviousDepthScene,
		PreviousDepthGame,
		DisocclusionMask,
		ReactiveMask,
		SsrTrace,
		SsrCurrent,
		SsrHistoryScene0,
		SsrHistoryScene1,
		SsrHistoryGame0,
		SsrHistoryGame1,
		SsrDenoised,
		ReflectionComposite,
		ColorHistoryScene0,
		ColorHistoryScene1,
		ColorHistoryGame0,
		ColorHistoryGame1,
		TemporalOutput,
		Count,
	};

	bool CreateRootSignatureAndPipelineStates(
		const std::array<IDxcBlob*, kPipelineCount>& computeShaderBlobs);
	bool CreateSizeDependentResources(uint32_t renderWidth, uint32_t renderHeight);
	void ReleaseSizeDependentResources();

	bool Dispatch(
		ID3D12GraphicsCommandList* commandList,
		uint32_t pipelineIndex,
		ResourceType destinationResourceType,
		const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4u>& sourceSrvHandles,
		const std::array<uint32_t, 44u>& constants,
		const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4u>* additionalSourceSrvHandles = nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(uint32_t descriptorIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kPipelineCount> pipelineStates_{};
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, static_cast<size_t>(ResourceType::Count)> resources_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, static_cast<size_t>(ResourceType::Count)> srvHandles_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, static_cast<size_t>(ResourceType::Count)> uavHandles_{};

	static constexpr uint32_t kViewHistoryCount = 2u;
	std::array<std::array<float, 16u>, kViewHistoryCount> previousViewProjectionMatrices_{};
	std::array<std::array<float, 4u>, kViewHistoryCount> previousViewportRects_{};
	D3D12_GPU_DESCRIPTOR_HANDLE outputSrvHandle_{};
	ResourceType outputResourceType_ = ResourceType::TemporalOutput;
	uint32_t renderWidth_ = 0u;
	uint32_t renderHeight_ = 0u;
	std::array<uint32_t, kViewHistoryCount> historyWriteIndices_{};
	std::array<bool, kViewHistoryCount> isHistoryValid_{};
	std::array<bool, kViewHistoryCount> lastSsrEnabled_{};
	std::array<bool, kViewHistoryCount> lastTemporalEnabled_{};
	bool isInitialized_ = false;
};
