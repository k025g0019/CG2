#include "EditorPostProcessQualityManager.h"

#include "Source/Engine/Editor/EditorProfilerManager.h"

#include <algorithm>
#include <cstring>

namespace {
	constexpr uint32_t kPostProcessDescriptorStartIndex = 90u;
	constexpr uint32_t kExposureDescriptorStartIndex = 109u;
	constexpr uint32_t kHistogramSrvDescriptorIndex = 111u;
	constexpr uint32_t kHistogramUavDescriptorIndex = 112u;
	constexpr uint32_t kHistogramBinCount = 256u;
	constexpr uint32_t kHistogramConstantCount = 12u;
	constexpr uint32_t kBloomPrefilterPipelineIndex = 0u;
	constexpr uint32_t kBloomDownsamplePipelineIndex = 1u;
	constexpr uint32_t kBloomUpsamplePipelineIndex = 2u;
	constexpr uint32_t kSmaaEdgePipelineIndex = 3u;
	constexpr uint32_t kSmaaWeightPipelineIndex = 4u;
	constexpr uint32_t kSmaaNeighborhoodPipelineIndex = 5u;
	constexpr uint32_t kGlarePipelineIndex = 6u;
	constexpr uint32_t kFilterPipelineIndex = 7u;
	constexpr uint32_t kAutoExposurePipelineIndex = 8u;
	constexpr uint32_t kRootConstantCount = EditorPostProcessQualityManager::kRootConstantCount;


	DXGI_FORMAT GetResourceFormat(uint32_t resourceIndex) {
		constexpr uint32_t kSmaaEdgeResourceIndex = 11u;
		constexpr uint32_t kSmaaWeightResourceIndex = 12u;

		if (resourceIndex == kSmaaEdgeResourceIndex) {
			return DXGI_FORMAT_R8G8_UNORM;
		}

		if (resourceIndex == kSmaaWeightResourceIndex) {
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	}
}

bool EditorPostProcessQualityManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	IDxcBlob* fullscreenVertexShaderBlob,
	const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs,
	IDxcBlob* histogramComputeShaderBlob,
	uint32_t renderWidth,
	uint32_t renderHeight) {

	//================================================================
	// 描画パスの固定リソースを作成する
	//================================================================

	if (device == nullptr || srvDescriptorHeap == nullptr || srvDescriptorSize == 0u ||
		fullscreenVertexShaderBlob == nullptr || histogramComputeShaderBlob == nullptr) {
		return false;
	}

	for (IDxcBlob* pixelShaderBlob : pixelShaderBlobs) {
		if (pixelShaderBlob == nullptr) {
			return false;
		}
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;
	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
	rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDescription.NumDescriptors = static_cast<UINT>(ResourceType::Count);
	HRESULT result = device_->CreateDescriptorHeap(
		&rtvHeapDescription,
		IID_PPV_ARGS(rtvDescriptorHeap_.GetAddressOf()));

	if (FAILED(result) || rtvDescriptorHeap_ == nullptr) {
		Finalize();
		return false;
	}

	if (!CreateRootSignatureAndPipelineStates(
		fullscreenVertexShaderBlob,
		pixelShaderBlobs,
		histogramComputeShaderBlob)) {
		Finalize();
		return false;
	}

	isInitialized_ = true;

	if (!Resize(renderWidth, renderHeight)) {
		Finalize();
		return false;
	}

	return true;
}

bool EditorPostProcessQualityManager::Resize(uint32_t renderWidth, uint32_t renderHeight) {
	if (!isInitialized_ || renderWidth == 0u || renderHeight == 0u) {
		return false;
	}

	if (renderWidth_ == renderWidth && renderHeight_ == renderHeight) {
		return true;
	}

	ReleaseSizeDependentResources();
	renderWidth_ = renderWidth;
	renderHeight_ = renderHeight;

	if (!CreateSizeDependentResources(renderWidth_, renderHeight_)) {
		ReleaseSizeDependentResources();
		return false;
	}

	return true;
}

bool EditorPostProcessQualityManager::ExecuteBloom(
	ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
	float bloomIntensity,
	float bloomThreshold,
	float bloomSoftKnee,
	float bloomScatter) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u) {
		return false;
	}

	const auto getSrvHandle = [this](ResourceType resourceType) {
		return srvHandles_[static_cast<size_t>(resourceType)];
	};
	const auto getInverseSize = [this](ResourceType resourceType) {
		const size_t resourceIndex = static_cast<size_t>(resourceType);
		return std::array<float, 2u>{
			1.0f / static_cast<float>(resourceWidths_[resourceIndex]),
			1.0f / static_cast<float>(resourceHeights_[resourceIndex]),
		};
	};

	//================================================================
	// HDR の明部を抽出し、4 段の解像度へ縮小する
	//================================================================

	std::array<float, kRootConstantCount> constants{};
	constants[0] = 1.0f / static_cast<float>(renderWidth_);
	constants[1] = 1.0f / static_cast<float>(renderHeight_);
	constants[2] = bloomThreshold;
	constants[3] = bloomSoftKnee;

	if (!DrawPass(
		commandList,
		kBloomPrefilterPipelineIndex,
		ResourceType::BloomDown0,
		sourceColorSrvHandle,
		sourceColorSrvHandle,
		constants)) {
		return false;
	}

	constexpr std::array<ResourceType, 3u> downsampleSources = {
		ResourceType::BloomDown0,
		ResourceType::BloomDown1,
		ResourceType::BloomDown2,
	};
	constexpr std::array<ResourceType, 3u> downsampleDestinations = {
		ResourceType::BloomDown1,
		ResourceType::BloomDown2,
		ResourceType::BloomDown3,
	};

	for (uint32_t passIndex = 0u; passIndex < downsampleSources.size(); passIndex++) {
		const ResourceType sourceType = downsampleSources[passIndex];
		const std::array<float, 2u> inverseSourceSize = getInverseSize(sourceType);
		constants.fill(0.0f);
		constants[0] = inverseSourceSize[0];
		constants[1] = inverseSourceSize[1];

		if (!DrawPass(
			commandList,
			kBloomDownsamplePipelineIndex,
			downsampleDestinations[passIndex],
			getSrvHandle(sourceType),
			getSrvHandle(sourceType),
			constants)) {
			return false;
		}
	}

	//================================================================
	// 小さいMipからTent Filterで戻し、広い光のにじみを合成する
	//================================================================

	constexpr std::array<ResourceType, 3u> lowResolutionSources = {
		ResourceType::BloomDown3,
		ResourceType::BloomUp2,
		ResourceType::BloomUp1,
	};
	constexpr std::array<ResourceType, 3u> highResolutionSources = {
		ResourceType::BloomDown2,
		ResourceType::BloomDown1,
		ResourceType::BloomDown0,
	};
	constexpr std::array<ResourceType, 3u> upsampleDestinations = {
		ResourceType::BloomUp2,
		ResourceType::BloomUp1,
		ResourceType::BloomUp0,
	};

	for (uint32_t passIndex = 0u; passIndex < lowResolutionSources.size(); passIndex++) {
		const ResourceType lowResolutionType = lowResolutionSources[passIndex];
		const std::array<float, 2u> inverseLowResolutionSize = getInverseSize(lowResolutionType);
		constants.fill(0.0f);
		constants[0] = inverseLowResolutionSize[0];
		constants[1] = inverseLowResolutionSize[1];
		constants[2] = bloomScatter;
		constants[3] = passIndex == lowResolutionSources.size() - 1u
			? bloomIntensity
			: 1.0f;

		if (!DrawPass(
			commandList,
			kBloomUpsamplePipelineIndex,
			upsampleDestinations[passIndex],
			getSrvHandle(lowResolutionType),
			getSrvHandle(highResolutionSources[passIndex]),
			constants)) {
			return false;
		}
	}

	return true;
}

bool EditorPostProcessQualityManager::ExecuteSmaa(
	ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
	float threshold,
	float cornerRounding) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u) {
		return false;
	}

	std::array<float, kRootConstantCount> constants{};
	constants[0] = 1.0f / static_cast<float>(renderWidth_);
	constants[1] = 1.0f / static_cast<float>(renderHeight_);
	constants[2] = (std::clamp)(threshold, 0.001f, 0.5f);
	constants[3] = (std::clamp)(cornerRounding, 0.0f, 100.0f);

	//================================================================
	// Edge Detection -> Blend Weight -> Neighborhood Blend
	//================================================================

	if (!DrawPass(
		commandList,
		kSmaaEdgePipelineIndex,
		ResourceType::SmaaEdges,
		sourceColorSrvHandle,
		sourceColorSrvHandle,
		constants)) {
		return false;
	}

	if (!DrawPass(
		commandList,
		kSmaaWeightPipelineIndex,
		ResourceType::SmaaWeights,
		srvHandles_[static_cast<size_t>(ResourceType::SmaaEdges)],
		srvHandles_[static_cast<size_t>(ResourceType::SmaaEdges)],
		constants)) {
		return false;
	}

	if (!DrawPass(
		commandList,
		kSmaaNeighborhoodPipelineIndex,
		ResourceType::SmaaOutput,
		sourceColorSrvHandle,
		srvHandles_[static_cast<size_t>(ResourceType::SmaaWeights)],
		constants)) {
		return false;
	}

	return true;
}

bool EditorPostProcessQualityManager::ExecuteGlare(
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
	bool preserveSource) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u || glareMode <= 1) {
		return false;
	}

	//================================================================
	// Bloom で抽出済みの明部から、選択した Glare 形状を作る
	//================================================================

	std::array<float, kRootConstantCount> constants{};
	constants[0] = 1.0f / static_cast<float>(renderWidth_);
	constants[1] = 1.0f / static_cast<float>(renderHeight_);
	constants[2] = static_cast<float>(glareMode);
	constants[3] = (std::max)(intensity, 0.0f);
	constants[4] = (std::clamp)(size, 0.1f, 8.0f);
	constants[5] = angleDegrees * 3.1415926535f / 180.0f;
	constants[6] = static_cast<float>((std::clamp)(streakCount, 2, 8));
	constants[7] = (std::clamp)(fade, 0.0f, 1.0f);
	constants[8] = (std::clamp)(colorModulation, 0.0f, 1.0f);
	constants[9] = (std::clamp)(centerX, 0.0f, 1.0f);
	constants[10] = (std::clamp)(centerY, 0.0f, 1.0f);
	constants[11] = preserveSource ? 1.0f : 0.0f;
	constants[12] = (std::max)(colorR, 0.0f);
	constants[13] = (std::max)(colorG, 0.0f);
	constants[14] = (std::max)(colorB, 0.0f);

	const ResourceType destinationResourceType =
		sourceColorSrvHandle.ptr == srvHandles_[static_cast<size_t>(ResourceType::GlareOutputA)].ptr
		? ResourceType::GlareOutputB
		: ResourceType::GlareOutputA;

	const bool isGlareExecuted = DrawPass(
		commandList,
		kGlarePipelineIndex,
		destinationResourceType,
		sourceColorSrvHandle,
		sourceColorSrvHandle,
		constants);

	if (isGlareExecuted) {
		lastGlareOutputResourceType_ = destinationResourceType;
	}

	return isGlareExecuted;
}

bool EditorPostProcessQualityManager::ExecuteFilter(
	ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle,
	int32_t filterMode,
	float strength,
	float colorR,
	float colorG,
	float colorB) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u || filterMode <= 0) {
		return false;
	}

	//================================================================
	// Blender の Filter ノード相当の 3x3 畳み込みを適用する
	//================================================================

	std::array<float, kRootConstantCount> constants{};
	constants[0] = 1.0f / static_cast<float>(renderWidth_);
	constants[1] = 1.0f / static_cast<float>(renderHeight_);
	constants[2] = static_cast<float>((std::clamp)(filterMode, 1, 8));
	constants[3] = (std::clamp)(strength, 0.0f, 2.0f);
	constants[4] = (std::max)(colorR, 0.0f);
	constants[5] = (std::max)(colorG, 0.0f);
	constants[6] = (std::max)(colorB, 0.0f);

	const ResourceType destinationResourceType =
		sourceColorSrvHandle.ptr == srvHandles_[static_cast<size_t>(ResourceType::FilterOutputA)].ptr
		? ResourceType::FilterOutputB
		: ResourceType::FilterOutputA;

	const bool isFilterExecuted = DrawPass(
		commandList,
		kFilterPipelineIndex,
		destinationResourceType,
		sourceColorSrvHandle,
		sourceColorSrvHandle,
		constants);

	if (isFilterExecuted) {
		lastFilterOutputResourceType_ = destinationResourceType;
	}

	return isFilterExecuted;
}

bool EditorPostProcessQualityManager::ExecuteAutoExposure(
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
	float viewportUvHeight) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u ||
		sourceColorResource == nullptr) {
		return false;
	}

	if (histogramResource_ == nullptr || histogramRootSignature_ == nullptr ||
		histogramPipelineState_ == nullptr || histogramSrvHandle_.ptr == 0u ||
		histogramUavGpuHandle_.ptr == 0u || histogramUavCpuHandle_.ptr == 0u) {
		return false;
	}

	if (isHistogramReadbackPending_ && histogramReadbackResource_ != nullptr) {
		void* histogramMappedAddress = nullptr;
		const D3D12_RANGE readRange{0u, static_cast<SIZE_T>(kHistogramBinCount * sizeof(uint32_t))};

		if (SUCCEEDED(histogramReadbackResource_->Map(
			0u,
			&readRange,
			&histogramMappedAddress)) &&
			histogramMappedAddress != nullptr) {
			const uint32_t* histogramValues = static_cast<const uint32_t*>(histogramMappedAddress);
			uint32_t maximumBinValue = 1u;

			for (uint32_t binIndex = 0u; binIndex < kHistogramBinCount; binIndex++) {
				maximumBinValue = (std::max)(maximumBinValue, histogramValues[binIndex]);
			}

			const float inverseMaximumBinValue = 1.0f / static_cast<float>(maximumBinValue);

			for (uint32_t binIndex = 0u; binIndex < kHistogramBinCount; binIndex++) {
				histogramNormalized_[binIndex] =
					static_cast<float>(histogramValues[binIndex]) * inverseMaximumBinValue;
			}

			histogramReadbackResource_->Unmap(0u, nullptr);
			hasHistogramData_ = true;
		}

		isHistogramReadbackPending_ = false;
	}

	//================================================================
	// HDR対数輝度を256 binへ集計する
	//================================================================

	ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap_};
	commandList->SetDescriptorHeaps(1u, descriptorHeaps);
	D3D12_RESOURCE_BARRIER sourceColorTransitionBarrier{};
	sourceColorTransitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	sourceColorTransitionBarrier.Transition.pResource = sourceColorResource;
	sourceColorTransitionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	sourceColorTransitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	sourceColorTransitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &sourceColorTransitionBarrier);
	D3D12_RESOURCE_BARRIER histogramTransitionBarrier{};
	histogramTransitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	histogramTransitionBarrier.Transition.pResource = histogramResource_.Get();
	histogramTransitionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	histogramTransitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	histogramTransitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &histogramTransitionBarrier);

	constexpr UINT clearHistogramValues[4] = {0u, 0u, 0u, 0u};
	commandList->ClearUnorderedAccessViewUint(
		histogramUavGpuHandle_,
		histogramUavCpuHandle_,
		histogramResource_.Get(),
		clearHistogramValues,
		0u,
		nullptr);

	constexpr uint32_t kHistogramSampleWidth = 256u;
	constexpr uint32_t kHistogramSampleHeight = 144u;
	constexpr float kMinimumLogLuminance = -12.0f;
	constexpr float kMaximumLogLuminance = 8.0f;
	std::array<uint32_t, kHistogramConstantCount> histogramConstants{};
	histogramConstants[0] = kHistogramSampleWidth;
	histogramConstants[1] = kHistogramSampleHeight;
	const float histogramFloatConstants[6] = {
		(std::clamp)(viewportUvX, 0.0f, 1.0f),
		(std::clamp)(viewportUvY, 0.0f, 1.0f),
		(std::clamp)(viewportUvWidth, 0.001f, 1.0f),
		(std::clamp)(viewportUvHeight, 0.001f, 1.0f),
		kMinimumLogLuminance,
		kMaximumLogLuminance
	};
	std::memcpy(
		&histogramConstants[2],
		histogramFloatConstants,
		sizeof(histogramFloatConstants));
	commandList->SetComputeRootSignature(histogramRootSignature_.Get());
	commandList->SetPipelineState(histogramPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, sourceColorSrvHandle);
	commandList->SetComputeRootDescriptorTable(1u, histogramUavGpuHandle_);
	commandList->SetComputeRoot32BitConstants(
		2u,
		kHistogramConstantCount,
		histogramConstants.data(),
		0u);
	RecordEditorProfilerDispatch();
	commandList->Dispatch(
		(kHistogramSampleWidth + 7u) / 8u,
		(kHistogramSampleHeight + 7u) / 8u,
		1u);

	D3D12_RESOURCE_BARRIER histogramUavBarrier{};
	histogramUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	histogramUavBarrier.UAV.pResource = histogramResource_.Get();
	commandList->ResourceBarrier(1u, &histogramUavBarrier);
	histogramTransitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	histogramTransitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &histogramTransitionBarrier);
	sourceColorTransitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	sourceColorTransitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &sourceColorTransitionBarrier);

	const ResourceType destinationResourceType =
		lastExposureOutputResourceType_ == ResourceType::Exposure0
		? ResourceType::Exposure1
		: ResourceType::Exposure0;
	const D3D12_GPU_DESCRIPTOR_HANDLE previousExposureHandle =
		srvHandles_[static_cast<size_t>(lastExposureOutputResourceType_)];
	std::array<float, kRootConstantCount> constants{};
	constants[0] = (std::max)(minimumExposure, 0.01f);
	constants[1] = (std::max)(maximumExposure, constants[0]);
	constants[2] = (std::max)(adaptationSpeed, 0.0f);
	constants[3] = (std::clamp)(deltaTime, 0.0f, 0.25f);
	constants[4] = (std::max)(targetLuminance, 0.001f);
	constants[5] = isExposureHistoryValid_ ? 1.0f : 0.0f;
	constants[6] = (std::clamp)(viewportUvX, 0.0f, 1.0f);
	constants[7] = (std::clamp)(viewportUvY, 0.0f, 1.0f);
	constants[8] = (std::clamp)(viewportUvWidth, 0.001f, 1.0f);
	constants[9] = (std::clamp)(viewportUvHeight, 0.001f, 1.0f);
	constants[10] = kMinimumLogLuminance;
	constants[11] = kMaximumLogLuminance;
	// 鏡面反射や太陽が画面へ入っただけで全体露出が変動しないよう、
	// ヒストグラムの最暗部と最明部を外して中間輝度を測光する。
	constants[12] = 0.05f;
	constants[13] = 0.95f;

	const bool isExposureExecuted = DrawPass(
		commandList,
		kAutoExposurePipelineIndex,
		destinationResourceType,
		sourceColorSrvHandle,
		previousExposureHandle,
		constants,
		histogramSrvHandle_);

	if (isExposureExecuted) {
		lastExposureOutputResourceType_ = destinationResourceType;
		isExposureHistoryValid_ = true;
	}

	if (histogramReadbackResource_ != nullptr) {
		D3D12_RESOURCE_BARRIER histogramCopyBarrier{};
		histogramCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		histogramCopyBarrier.Transition.pResource = histogramResource_.Get();
		histogramCopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		histogramCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		histogramCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		commandList->ResourceBarrier(1u, &histogramCopyBarrier);
		commandList->CopyResource(histogramReadbackResource_.Get(), histogramResource_.Get());
		histogramCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		histogramCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1u, &histogramCopyBarrier);
		isHistogramReadbackPending_ = true;
	}

	return isExposureExecuted;
}

void EditorPostProcessQualityManager::Finalize() {
	ReleaseSizeDependentResources();

	for (Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState : pipelineStates_) {
		pipelineState.Reset();
	}

	histogramPipelineState_.Reset();
	histogramRootSignature_.Reset();
	rootSignature_.Reset();
	rtvDescriptorHeap_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	rtvDescriptorSize_ = 0u;
	isInitialized_ = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetBloomSrvHandle() const {
	return srvHandles_[static_cast<size_t>(ResourceType::BloomUp0)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetGlareSrvHandle() const {
	return srvHandles_[static_cast<size_t>(lastGlareOutputResourceType_)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetFilterSrvHandle() const {
	return srvHandles_[static_cast<size_t>(lastFilterOutputResourceType_)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetSmaaOutputSrvHandle() const {
	return srvHandles_[static_cast<size_t>(ResourceType::SmaaOutput)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetAutoExposureSrvHandle() const {
	return srvHandles_[static_cast<size_t>(lastExposureOutputResourceType_)];
}

const std::array<float, 256u>& EditorPostProcessQualityManager::GetHistogramNormalized() const {
	return histogramNormalized_;
}

bool EditorPostProcessQualityManager::HasHistogramData() const {
	return hasHistogramData_;
}

bool EditorPostProcessQualityManager::CreateRootSignatureAndPipelineStates(
	IDxcBlob* fullscreenVertexShaderBlob,
	const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs,
	IDxcBlob* histogramComputeShaderBlob) {

	std::array<D3D12_DESCRIPTOR_RANGE, 3u> descriptorRanges{};
	std::array<D3D12_ROOT_PARAMETER, 4u> rootParameters{};

	for (uint32_t descriptorIndex = 0u; descriptorIndex < descriptorRanges.size(); descriptorIndex++) {
		D3D12_DESCRIPTOR_RANGE& descriptorRange = descriptorRanges[descriptorIndex];
		descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descriptorRange.NumDescriptors = 1u;
		descriptorRange.BaseShaderRegister = descriptorIndex;
		descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER& rootParameter = rootParameters[descriptorIndex];
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameter.DescriptorTable.NumDescriptorRanges = 1u;
		rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRange;
	}

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Constants.ShaderRegister = 0u;
	rootParameters[3].Constants.Num32BitValues = kRootConstantCount;

	D3D12_STATIC_SAMPLER_DESC linearSampler{};
	linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	linearSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	linearSampler.ShaderRegister = 0u;
	linearSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	linearSampler.MaxLOD = D3D12_FLOAT32_MAX;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
	rootSignatureDescription.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDescription.pParameters = rootParameters.data();
	rootSignatureDescription.NumStaticSamplers = 1u;
	rootSignatureDescription.pStaticSamplers = &linearSampler;
	rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT result = D3D12SerializeRootSignature(
		&rootSignatureDescription,
		D3D_ROOT_SIGNATURE_VERSION_1,
		signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(result) || signatureBlob == nullptr) {
		return false;
	}

	result = device_->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature_.GetAddressOf()));

	if (FAILED(result) || rootSignature_ == nullptr) {
		return false;
	}

	const std::array<DXGI_FORMAT, kPipelineCount> pipelineFormats = {
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R8G8_UNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
	};

	for (uint32_t pipelineIndex = 0u; pipelineIndex < kPipelineCount; pipelineIndex++) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature = rootSignature_.Get();
		pipelineStateDescription.VS = {
			fullscreenVertexShaderBlob->GetBufferPointer(),
			fullscreenVertexShaderBlob->GetBufferSize(),
		};
		pipelineStateDescription.PS = {
			pixelShaderBlobs[pipelineIndex]->GetBufferPointer(),
			pixelShaderBlobs[pipelineIndex]->GetBufferSize(),
		};
		D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDescription{};
		renderTargetBlendDescription.BlendEnable = FALSE;
		renderTargetBlendDescription.LogicOpEnable = FALSE;
		renderTargetBlendDescription.SrcBlend = D3D12_BLEND_ONE;
		renderTargetBlendDescription.DestBlend = D3D12_BLEND_ZERO;
		renderTargetBlendDescription.BlendOp = D3D12_BLEND_OP_ADD;
		renderTargetBlendDescription.SrcBlendAlpha = D3D12_BLEND_ONE;
		renderTargetBlendDescription.DestBlendAlpha = D3D12_BLEND_ZERO;
		renderTargetBlendDescription.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		renderTargetBlendDescription.LogicOp = D3D12_LOGIC_OP_NOOP;
		renderTargetBlendDescription.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pipelineStateDescription.BlendState.AlphaToCoverageEnable = FALSE;
		pipelineStateDescription.BlendState.IndependentBlendEnable = FALSE;
		pipelineStateDescription.BlendState.RenderTarget[0] = renderTargetBlendDescription;
		pipelineStateDescription.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineStateDescription.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pipelineStateDescription.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		pipelineStateDescription.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		pipelineStateDescription.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		pipelineStateDescription.RasterizerState.DepthClipEnable = TRUE;
		pipelineStateDescription.DepthStencilState.DepthEnable = FALSE;
		pipelineStateDescription.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		pipelineStateDescription.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pipelineStateDescription.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		pipelineStateDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateDescription.NumRenderTargets = 1u;
		pipelineStateDescription.RTVFormats[0] = pipelineFormats[pipelineIndex];
		pipelineStateDescription.SampleDesc.Count = 1u;

		result = device_->CreateGraphicsPipelineState(
			&pipelineStateDescription,
			IID_PPV_ARGS(pipelineStates_[pipelineIndex].GetAddressOf()));

		if (FAILED(result) || pipelineStates_[pipelineIndex] == nullptr) {
			return false;
		}
	}

	//================================================================
	// 輝度Histogram専用Compute Root Signature / PSO
	//================================================================

	std::array<D3D12_DESCRIPTOR_RANGE, 2u> histogramDescriptorRanges{};
	histogramDescriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	histogramDescriptorRanges[0].NumDescriptors = 1u;
	histogramDescriptorRanges[0].BaseShaderRegister = 0u;
	histogramDescriptorRanges[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	histogramDescriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	histogramDescriptorRanges[1].NumDescriptors = 1u;
	histogramDescriptorRanges[1].BaseShaderRegister = 0u;
	histogramDescriptorRanges[1].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	std::array<D3D12_ROOT_PARAMETER, 3u> histogramRootParameters{};

	for (uint32_t descriptorIndex = 0u;
		descriptorIndex < static_cast<uint32_t>(histogramDescriptorRanges.size());
		descriptorIndex++) {
		histogramRootParameters[descriptorIndex].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		histogramRootParameters[descriptorIndex].ShaderVisibility =
			D3D12_SHADER_VISIBILITY_ALL;
		histogramRootParameters[descriptorIndex].DescriptorTable.NumDescriptorRanges = 1u;
		histogramRootParameters[descriptorIndex].DescriptorTable.pDescriptorRanges =
			&histogramDescriptorRanges[descriptorIndex];
	}

	histogramRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	histogramRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	histogramRootParameters[2].Constants.ShaderRegister = 0u;
	histogramRootParameters[2].Constants.Num32BitValues = kHistogramConstantCount;
	D3D12_ROOT_SIGNATURE_DESC histogramRootSignatureDescription{};
	histogramRootSignatureDescription.NumParameters =
		static_cast<UINT>(histogramRootParameters.size());
	histogramRootSignatureDescription.pParameters = histogramRootParameters.data();
	histogramRootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	signatureBlob.Reset();
	errorBlob.Reset();
	result = D3D12SerializeRootSignature(
		&histogramRootSignatureDescription,
		D3D_ROOT_SIGNATURE_VERSION_1,
		signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(result) || signatureBlob == nullptr) {
		return false;
	}

	result = device_->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(histogramRootSignature_.GetAddressOf()));

	if (FAILED(result) || histogramRootSignature_ == nullptr) {
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC histogramPipelineDescription{};
	histogramPipelineDescription.pRootSignature = histogramRootSignature_.Get();
	histogramPipelineDescription.CS = {
		histogramComputeShaderBlob->GetBufferPointer(),
		histogramComputeShaderBlob->GetBufferSize()
	};
	result = device_->CreateComputePipelineState(
		&histogramPipelineDescription,
		IID_PPV_ARGS(histogramPipelineState_.GetAddressOf()));

	if (FAILED(result) || histogramPipelineState_ == nullptr) {
		return false;
	}

	return true;
}

bool EditorPostProcessQualityManager::CreateSizeDependentResources(
	uint32_t renderWidth,
	uint32_t renderHeight) {

	const uint32_t bloomWidth0 = (std::max)(1u, renderWidth / 2u);
	const uint32_t bloomHeight0 = (std::max)(1u, renderHeight / 2u);
	const std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceWidths = {
		bloomWidth0,
		(std::max)(1u, bloomWidth0 / 2u),
		(std::max)(1u, bloomWidth0 / 4u),
		(std::max)(1u, bloomWidth0 / 8u),
		bloomWidth0,
		(std::max)(1u, bloomWidth0 / 2u),
		(std::max)(1u, bloomWidth0 / 4u),
		renderWidth,
		renderWidth,
		renderWidth,
		renderWidth,
		renderWidth,
		renderWidth,
		renderWidth,
		1u,
		1u,
	};
	const std::array<uint32_t, static_cast<size_t>(ResourceType::Count)> resourceHeights = {
		bloomHeight0,
		(std::max)(1u, bloomHeight0 / 2u),
		(std::max)(1u, bloomHeight0 / 4u),
		(std::max)(1u, bloomHeight0 / 8u),
		bloomHeight0,
		(std::max)(1u, bloomHeight0 / 2u),
		(std::max)(1u, bloomHeight0 / 4u),
		renderHeight,
		renderHeight,
		renderHeight,
		renderHeight,
		renderHeight,
		renderHeight,
		renderHeight,
		1u,
		1u,
	};
	resourceWidths_ = resourceWidths;
	resourceHeights_ = resourceHeights;

	for (uint32_t resourceIndex = 0u;
		resourceIndex < static_cast<uint32_t>(ResourceType::Count);
		resourceIndex++) {
		const DXGI_FORMAT resourceFormat = GetResourceFormat(resourceIndex);
		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = static_cast<UINT64>(resourceWidths_[resourceIndex]);
		resourceDescription.Height = resourceHeights_[resourceIndex];
		resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
		resourceDescription.MipLevels = static_cast<UINT16>(1u);
		resourceDescription.Format = resourceFormat;
		resourceDescription.SampleDesc.Count = 1u;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format = resourceFormat;
		clearValue.Color[3] = 1.0f;
		HRESULT result = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&clearValue,
			IID_PPV_ARGS(resources_[resourceIndex].GetAddressOf()));

		if (FAILED(result) || resources_[resourceIndex] == nullptr) {
			return false;
		}

		device_->CreateRenderTargetView(
			resources_[resourceIndex].Get(),
			nullptr,
			GetRtvDescriptorHandle(resourceIndex));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = resourceFormat;
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescription.Texture2D.MipLevels = 1u;
		const uint32_t exposure0Index = static_cast<uint32_t>(ResourceType::Exposure0);
		const uint32_t descriptorIndex = resourceIndex >= exposure0Index
			? kExposureDescriptorStartIndex + resourceIndex - exposure0Index
			: kPostProcessDescriptorStartIndex + resourceIndex;
		device_->CreateShaderResourceView(
			resources_[resourceIndex].Get(),
			&srvDescription,
			GetCpuSrvDescriptorHandle(descriptorIndex));
		srvHandles_[resourceIndex] = GetGpuSrvDescriptorHandle(descriptorIndex);
	}

	D3D12_RESOURCE_DESC histogramResourceDescription{};
	histogramResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	histogramResourceDescription.Width =
		static_cast<UINT64>(kHistogramBinCount) *
		static_cast<UINT64>(sizeof(uint32_t));
	histogramResourceDescription.Height = 1u;
	histogramResourceDescription.DepthOrArraySize = 1u;
	histogramResourceDescription.MipLevels = 1u;
	histogramResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
	histogramResourceDescription.SampleDesc.Count = 1u;
	histogramResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	histogramResourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_HEAP_PROPERTIES histogramHeapProperties{};
	histogramHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	HRESULT histogramResult = device_->CreateCommittedResource(
		&histogramHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&histogramResourceDescription,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(histogramResource_.GetAddressOf()));

	if (FAILED(histogramResult) || histogramResource_ == nullptr) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC histogramSrvDescription{};
	histogramSrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	histogramSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	histogramSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	histogramSrvDescription.Buffer.FirstElement = 0u;
	histogramSrvDescription.Buffer.NumElements = kHistogramBinCount;
	histogramSrvDescription.Buffer.StructureByteStride =
		static_cast<UINT>(sizeof(uint32_t));
	device_->CreateShaderResourceView(
		histogramResource_.Get(),
		&histogramSrvDescription,
		GetCpuSrvDescriptorHandle(kHistogramSrvDescriptorIndex));
	D3D12_UNORDERED_ACCESS_VIEW_DESC histogramUavDescription{};
	histogramUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	histogramUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	histogramUavDescription.Buffer.FirstElement = 0u;
	histogramUavDescription.Buffer.NumElements = kHistogramBinCount;
	histogramUavDescription.Buffer.StructureByteStride =
		static_cast<UINT>(sizeof(uint32_t));
	histogramUavCpuHandle_ = GetCpuSrvDescriptorHandle(kHistogramUavDescriptorIndex);
	device_->CreateUnorderedAccessView(
		histogramResource_.Get(),
		nullptr,
		&histogramUavDescription,
		histogramUavCpuHandle_);
	histogramSrvHandle_ = GetGpuSrvDescriptorHandle(kHistogramSrvDescriptorIndex);
	histogramUavGpuHandle_ = GetGpuSrvDescriptorHandle(kHistogramUavDescriptorIndex);

	D3D12_RESOURCE_DESC histogramReadbackDescription = histogramResourceDescription;
	histogramReadbackDescription.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_HEAP_PROPERTIES histogramReadbackHeapProperties{};
	histogramReadbackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	const HRESULT histogramReadbackResult = device_->CreateCommittedResource(
		&histogramReadbackHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&histogramReadbackDescription,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(histogramReadbackResource_.GetAddressOf()));

	if (FAILED(histogramReadbackResult) || histogramReadbackResource_ == nullptr) {
		return false;
	}

	return true;
}

void EditorPostProcessQualityManager::ReleaseSizeDependentResources() {
	for (Microsoft::WRL::ComPtr<ID3D12Resource>& resource : resources_) {
		resource.Reset();
	}

	histogramResource_.Reset();
	histogramReadbackResource_.Reset();
	histogramUavCpuHandle_ = {};
	histogramSrvHandle_ = {};
	histogramUavGpuHandle_ = {};
	histogramNormalized_.fill(0.0f);
	isHistogramReadbackPending_ = false;
	hasHistogramData_ = false;
	srvHandles_.fill({});
	resourceWidths_.fill(0u);
	resourceHeights_.fill(0u);
	lastGlareOutputResourceType_ = ResourceType::GlareOutputA;
	lastFilterOutputResourceType_ = ResourceType::FilterOutputA;
	lastExposureOutputResourceType_ = ResourceType::Exposure0;
	isExposureHistoryValid_ = false;
	renderWidth_ = 0u;
	renderHeight_ = 0u;
}

bool EditorPostProcessQualityManager::DrawPass(
	ID3D12GraphicsCommandList* commandList,
	uint32_t pipelineIndex,
	ResourceType destinationResourceType,
	D3D12_GPU_DESCRIPTOR_HANDLE source0SrvHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE source1SrvHandle,
	const std::array<float, kRootConstantCount>& constants,
	D3D12_GPU_DESCRIPTOR_HANDLE source2SrvHandle) {

	if (pipelineIndex >= pipelineStates_.size() || source0SrvHandle.ptr == 0u ||
		source1SrvHandle.ptr == 0u) {
		return false;
	}

	const size_t destinationIndex = static_cast<size_t>(destinationResourceType);
	ID3D12Resource* destinationResource = resources_[destinationIndex].Get();

	if (destinationResource == nullptr) {
		return false;
	}

	D3D12_RESOURCE_BARRIER transitionBarrier{};
	transitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	transitionBarrier.Transition.pResource = destinationResource;
	transitionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1u, &transitionBarrier);

	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(resourceWidths_[destinationIndex]);
	viewport.Height = static_cast<float>(resourceHeights_[destinationIndex]);
	viewport.MaxDepth = 1.0f;
	D3D12_RECT scissorRectangle{};
	scissorRectangle.right = static_cast<LONG>(resourceWidths_[destinationIndex]);
	scissorRectangle.bottom = static_cast<LONG>(resourceHeights_[destinationIndex]);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = GetRtvDescriptorHandle(
		static_cast<uint32_t>(destinationIndex));
	constexpr float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	commandList->RSSetViewports(1u, &viewport);
	commandList->RSSetScissorRects(1u, &scissorRectangle);
	commandList->OMSetRenderTargets(1u, &rtvHandle, FALSE, nullptr);
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0u, nullptr);
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineStates_[pipelineIndex].Get());
	commandList->SetGraphicsRootDescriptorTable(0u, source0SrvHandle);
	commandList->SetGraphicsRootDescriptorTable(1u, source1SrvHandle);
	commandList->SetGraphicsRootDescriptorTable(
		2u,
		source2SrvHandle.ptr == 0u ? source1SrvHandle : source2SrvHandle);
	commandList->SetGraphicsRoot32BitConstants(3u, kRootConstantCount, constants.data(), 0u);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	RecordEditorProfilerDrawCall();
	commandList->DrawInstanced(3u, 1u, 0u, 0u);

	transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &transitionBarrier);
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetCpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetGpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetRtvDescriptorHandle(
	uint32_t resourceIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<SIZE_T>(rtvDescriptorSize_) * static_cast<SIZE_T>(resourceIndex);
	return descriptorHandle;
}
