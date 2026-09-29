#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#pragma warning(pop)

//========================================
// 高品質Post Process管理
//========================================

class EditorPostProcessQualityManager {
public:
	static constexpr uint32_t kPipelineCount = 9u;
	static constexpr uint32_t kRootConstantCount = 16u;

	//------------------------------
	// 固定・解像度依存リソース初期化
	//------------------------------

	// Full-screen描画用PSO群とHistogram Compute PSOを作り、Bloom/SMAA/Glare等の
	// 中間Render Targetを指定解像度から生成する。RTV HeapはこのManagerが所有する。
	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		IDxcBlob* fullscreenVertexShaderBlob,
		const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs,
		IDxcBlob* histogramComputeShaderBlob,
		uint32_t renderWidth,
		uint32_t renderHeight);

	// Render Sizeに依存する中間Textureを再生成する。Exposure履歴も新しいSizeへ合わせる。
	bool Resize(uint32_t renderWidth, uint32_t renderHeight);

	//------------------------------
	// Bloom処理
	//------------------------------

	// HDR色からThreshold/Soft Kneeで高輝度成分を抽出し、4段Downsampleと
	// 3段Upsampleで異なる広がりを合成する。戻り値false時は呼出側が旧Bloom等へFallbackできる。
	bool ExecuteBloom(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		float bloomIntensity = 1.0f,
		float bloomThreshold = 1.0f,
		float bloomSoftKnee = 0.5f,
		float bloomScatter = 0.72f);
	//------------------------------
	// Anti-Aliasing処理
	//------------------------------

	// 色差からEdgeを検出し、Edge形状からBlend Weightを求め、近傍色を混合する3 Pass SMAA。
	// thresholdはEdge検出感度、cornerRoundingは角を過度に丸めないための調整値。
	bool ExecuteSmaa(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		float threshold = 0.10f,
		float cornerRounding = 25.0f);
	//------------------------------
	// Glare・Color Filter処理
	//------------------------------

	// 光源中心から指定方向へStreakをSampleし、Glare用Textureへ書く。
	// preserveSource=trueでは元画像を残してEffectを加算し、複数Glareを連結できる。
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
		bool preserveSource,
		float sampleRatio);
	// 色調FilterをFull-screen Passで適用する。複数回呼ぶ場合はA/B Targetを交互に使い、
	// 同じTextureを同時に入力SRVと出力RTVへしない。
	bool ExecuteFilter(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		int32_t filterMode,
		float strength,
		float colorR,
		float colorG,
		float colorB);
	//------------------------------
	// 自動露出処理
	//------------------------------

	// 指定Viewport内の輝度HistogramをComputeで集計し、目標輝度との差から露出を更新する。
	// adaptationSpeedとdeltaTimeで前Frame露出へ徐々に追従させ、急な明暗変化を抑える。
	// sourceColorResourceはSRV Handleだけでは行えないResource State遷移に使用する。
	bool ExecuteAutoExposure(
		ID3D12GraphicsCommandList* commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
		ID3D12Resource* sourceColorResource,
		float minimumExposure,
		float maximumExposure,
		float adaptationSpeed,
		float targetLuminance,
		float deltaTime,
		float viewportUvX,
		float viewportUvY,
		float viewportUvWidth,
		float viewportUvHeight);

	//------------------------------
	// 出力取得・終了処理
	//------------------------------

	void Finalize();

	D3D12_GPU_DESCRIPTOR_HANDLE GetBloomSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGlareSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetFilterSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSmaaOutputSrvHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetAutoExposureSrvHandle() const;
	const std::array<float, 256u>& GetHistogramNormalized() const;
	bool HasHistogramData() const;

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
		Exposure0,
		Exposure1,
		Count,
	};

	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* fullscreenVertexShaderBlob,
		const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs,
		IDxcBlob* histogramComputeShaderBlob);
	bool CreateSizeDependentResources(uint32_t renderWidth, uint32_t renderHeight);
	void ReleaseSizeDependentResources();
	bool DrawPass(
		ID3D12GraphicsCommandList* commandList,
		uint32_t pipelineIndex,
		ResourceType destinationResourceType,
		D3D12_GPU_DESCRIPTOR_HANDLE source0SrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE source1SrvHandle,
		const std::array<float, kRootConstantCount>& constants,
		D3D12_GPU_DESCRIPTOR_HANDLE source2SrvHandle = {});

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvDescriptorHandle(uint32_t resourceIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;
	UINT rtvDescriptorSize_ = 0u;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> histogramRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> histogramPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> histogramResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> histogramReadbackResource_;
	D3D12_CPU_DESCRIPTOR_HANDLE histogramUavCpuHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE histogramSrvHandle_{};
	D3D12_GPU_DESCRIPTOR_HANDLE histogramUavGpuHandle_{};
	std::array<float, 256u> histogramNormalized_{};
	bool isHistogramReadbackPending_ = false;
	bool hasHistogramData_ = false;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kPipelineCount> pipelineStates_{};
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, static_cast<size_t>(ResourceType::Count)> resources_{};
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, static_cast<size_t>(ResourceType::Count)> srvHandles_{};
	std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceWidths_{};
	std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceHeights_{};
	uint32_t renderWidth_ = 0u;
	uint32_t renderHeight_ = 0u;
	ResourceType lastGlareOutputResourceType_ = ResourceType::GlareOutputA;  // 複数 Glare の最後に書いた出力を保持する
	ResourceType lastFilterOutputResourceType_ = ResourceType::FilterOutputA;  // 複数 Filter の最後に書いた出力を保持する
	ResourceType lastExposureOutputResourceType_ = ResourceType::Exposure0;
	bool isExposureHistoryValid_ = false;
	bool isInitialized_ = false;
};
