#include "EditorPostProcessQualityManager.h"

#include <algorithm>

namespace {
	constexpr uint32_t kPostProcessDescriptorStartIndex = 90u;
	constexpr uint32_t kBloomPrefilterPipelineIndex = 0u;
	constexpr uint32_t kBloomDownsamplePipelineIndex = 1u;
	constexpr uint32_t kBloomUpsamplePipelineIndex = 2u;
	constexpr uint32_t kSmaaEdgePipelineIndex = 3u;
	constexpr uint32_t kSmaaWeightPipelineIndex = 4u;
	constexpr uint32_t kSmaaNeighborhoodPipelineIndex = 5u;
	constexpr uint32_t kRootConstantCount = 8u;


	DXGI_FORMAT GetResourceFormat(uint32_t resourceIndex) {
		constexpr uint32_t kSmaaEdgeResourceIndex = 7u;
		constexpr uint32_t kSmaaWeightResourceIndex = 8u;

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
	uint32_t renderWidth,
	uint32_t renderHeight) {

	//================================================================
	// 描画パスの固定リソースを作成する
	//================================================================

	if (device == nullptr || srvDescriptorHeap == nullptr || srvDescriptorSize == 0u ||
		fullscreenVertexShaderBlob == nullptr) {
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

	if (!CreateRootSignatureAndPipelineStates(fullscreenVertexShaderBlob, pixelShaderBlobs)) {
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

	std::array<float, 8u> constants{};
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
	D3D12_GPU_DESCRIPTOR_HANDLE sourceColorSrvHandle) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u) {
		return false;
	}

	std::array<float, 8u> constants{};
	constants[0] = 1.0f / static_cast<float>(renderWidth_);
	constants[1] = 1.0f / static_cast<float>(renderHeight_);
	constants[2] = 0.075f;
	constants[3] = 0.25f;

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

void EditorPostProcessQualityManager::Finalize() {
	ReleaseSizeDependentResources();

	for (Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState : pipelineStates_) {
		pipelineState.Reset();
	}

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

D3D12_GPU_DESCRIPTOR_HANDLE EditorPostProcessQualityManager::GetSmaaOutputSrvHandle() const {
	return srvHandles_[static_cast<size_t>(ResourceType::SmaaOutput)];
}

bool EditorPostProcessQualityManager::CreateRootSignatureAndPipelineStates(
	IDxcBlob* fullscreenVertexShaderBlob,
	const std::array<IDxcBlob*, kPipelineCount>& pixelShaderBlobs) {

	std::array<D3D12_DESCRIPTOR_RANGE, 2u> descriptorRanges{};
	std::array<D3D12_ROOT_PARAMETER, 3u> rootParameters{};

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

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].Constants.ShaderRegister = 0u;
	rootParameters[2].Constants.Num32BitValues = kRootConstantCount;

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
		const uint32_t descriptorIndex = kPostProcessDescriptorStartIndex + resourceIndex;
		device_->CreateShaderResourceView(
			resources_[resourceIndex].Get(),
			&srvDescription,
			GetCpuSrvDescriptorHandle(descriptorIndex));
		srvHandles_[resourceIndex] = GetGpuSrvDescriptorHandle(descriptorIndex);
	}

	return true;
}

void EditorPostProcessQualityManager::ReleaseSizeDependentResources() {
	for (Microsoft::WRL::ComPtr<ID3D12Resource>& resource : resources_) {
		resource.Reset();
	}

	srvHandles_.fill({});
	resourceWidths_.fill(0u);
	resourceHeights_.fill(0u);
	renderWidth_ = 0u;
	renderHeight_ = 0u;
}

bool EditorPostProcessQualityManager::DrawPass(
	ID3D12GraphicsCommandList* commandList,
	uint32_t pipelineIndex,
	ResourceType destinationResourceType,
	D3D12_GPU_DESCRIPTOR_HANDLE source0SrvHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE source1SrvHandle,
	const std::array<float, 8u>& constants) {

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
	commandList->SetGraphicsRoot32BitConstants(2u, kRootConstantCount, constants.data(), 0u);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
