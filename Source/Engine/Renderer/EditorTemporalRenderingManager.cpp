#include "EditorTemporalRenderingManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
	// 0-159 は固定描画機能と ImGui が使用する。Temporal は履歴を View ごとに
	// 分離するため、動的Texture領域の直前に連続した専用範囲を確保する。
	constexpr uint32_t kTemporalDescriptorStartIndex = 160u;
	constexpr uint32_t kDescriptorStride = 2u;
	constexpr uint32_t kComputeConstantCount = 44u;
	constexpr uint32_t kThreadGroupSize = 8u;

	constexpr D3D12_RESOURCE_STATES kShaderReadState = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	uint32_t GetSrvDescriptorIndex(uint32_t resourceIndex) {
		return kTemporalDescriptorStartIndex + resourceIndex * kDescriptorStride;
	}

	uint32_t GetUavDescriptorIndex(uint32_t resourceIndex) {
		return GetSrvDescriptorIndex(resourceIndex) + 1u;
	}
}

bool EditorTemporalRenderingManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	const std::array<IDxcBlob*, kPipelineCount>& computeShaderBlobs,
	uint32_t renderWidth,
	uint32_t renderHeight) {

	//================================================================
	// 初期化に必要な DirectX 12 オブジェクトを検証
	//================================================================

	if (device == nullptr || srvDescriptorHeap == nullptr || srvDescriptorSize == 0u) {
		return false;
	}

	for (IDxcBlob* computeShaderBlob : computeShaderBlobs) {
		if (computeShaderBlob == nullptr) {
			return false;
		}
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;

	if (!CreateRootSignatureAndPipelineStates(computeShaderBlobs)) {
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

bool EditorTemporalRenderingManager::Resize(uint32_t renderWidth, uint32_t renderHeight) {
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

bool EditorTemporalRenderingManager::Execute(
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
	float sharpness,
	float blendRatio) {

	if (!isInitialized_ || commandList == nullptr || sourceColorSrvHandle.ptr == 0u ||
		sceneDepthSrvHandle.ptr == 0u || objectMotionVectorSrvHandle.ptr == 0u ||
		reconstructedNormalSrvHandle.ptr == 0u ||
		depthPyramidSrvHandles[0].ptr == 0u || materialMaskSrvHandle.ptr == 0u ||
		inverseViewProjectionMatrix == nullptr || viewProjectionMatrix == nullptr ||
		cameraPosition == nullptr || viewHistoryIndex >= kViewHistoryCount) {
		return false;
	}

	if (!ssrEnabled && !temporalEnabled) {
		return false;
	}

	if (ssrEnabled) {
		for (const D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrvHandle : depthPyramidSrvHandles) {
			if (depthPyramidSrvHandle.ptr == 0u) {
				return false;
			}
		}
	}

	const float viewportRect[4] = {
		viewportX,
		viewportY,
		(std::max)(viewportWidth, 1.0f),
		(std::max)(viewportHeight, 1.0f)
	};
	bool hasViewportChanged = false;

	for (uint32_t viewportElementIndex = 0u; viewportElementIndex < 4u; viewportElementIndex++) {
		hasViewportChanged = hasViewportChanged ||
			std::fabs(
				previousViewportRects_[viewHistoryIndex][viewportElementIndex] -
				viewportRect[viewportElementIndex]) > 0.5f;
	}

	if (ssrEnabled != lastSsrEnabled_[viewHistoryIndex] ||
		temporalEnabled != lastTemporalEnabled_[viewHistoryIndex] ||
		hasViewportChanged) {
		isHistoryValid_[viewHistoryIndex] = false;
	}

	commandList->SetComputeRootSignature(computeRootSignature_.Get());

	std::array<uint32_t, kComputeConstantCount> constants{};
	constants[0] = renderWidth_;
	constants[1] = renderHeight_;
	const float inverseRenderWidth = 1.0f / static_cast<float>(renderWidth_);
	const float inverseRenderHeight = 1.0f / static_cast<float>(renderHeight_);
	std::memcpy(&constants[2], &inverseRenderWidth, sizeof(float));
	std::memcpy(&constants[3], &inverseRenderHeight, sizeof(float));
	std::memcpy(&constants[4], inverseViewProjectionMatrix, sizeof(float) * 16u);
	std::memcpy(
		&constants[20],
		isHistoryValid_[viewHistoryIndex]
			? previousViewProjectionMatrices_[viewHistoryIndex].data()
			: viewProjectionMatrix,
		sizeof(float) * 16u);
	std::memcpy(&constants[40], viewportRect, sizeof(viewportRect));

	const bool isGameViewHistory = viewHistoryIndex == 1u;
	const uint32_t historyWriteIndex = historyWriteIndices_[viewHistoryIndex];
	const ResourceType previousDepthType = isGameViewHistory
		? ResourceType::PreviousDepthGame
		: ResourceType::PreviousDepthScene;
	const ResourceType ssrHistoryReadType = isGameViewHistory
		? (historyWriteIndex == 0u
			? ResourceType::SsrHistoryGame1
			: ResourceType::SsrHistoryGame0)
		: (historyWriteIndex == 0u
			? ResourceType::SsrHistoryScene1
			: ResourceType::SsrHistoryScene0);
	const ResourceType ssrHistoryWriteType = isGameViewHistory
		? (historyWriteIndex == 0u
			? ResourceType::SsrHistoryGame0
			: ResourceType::SsrHistoryGame1)
		: (historyWriteIndex == 0u
			? ResourceType::SsrHistoryScene0
			: ResourceType::SsrHistoryScene1);
	const ResourceType colorHistoryReadType = isGameViewHistory
		? (historyWriteIndex == 0u
			? ResourceType::ColorHistoryGame1
			: ResourceType::ColorHistoryGame0)
		: (historyWriteIndex == 0u
			? ResourceType::ColorHistoryScene1
			: ResourceType::ColorHistoryScene0);
	const ResourceType colorHistoryWriteType = isGameViewHistory
		? (historyWriteIndex == 0u
			? ResourceType::ColorHistoryGame0
			: ResourceType::ColorHistoryGame1)
		: (historyWriteIndex == 0u
			? ResourceType::ColorHistoryScene0
			: ResourceType::ColorHistoryScene1);

	const auto getSrvHandle = [this](ResourceType resourceType) {
		return srvHandles_[static_cast<size_t>(resourceType)];
	};

	//================================================================
	// カメラ移動量と非連続領域を求める
	//================================================================

	float historyValidValue = isHistoryValid_[viewHistoryIndex] ? 1.0f : 0.0f;
	std::memcpy(&constants[36], &historyValidValue, sizeof(float));

	if (!Dispatch(
		commandList,
		0u,
		ResourceType::Velocity,
		{sceneDepthSrvHandle, objectMotionVectorSrvHandle, sceneDepthSrvHandle, sceneDepthSrvHandle},
		constants)) {
		return false;
	}

	if (!Dispatch(
		commandList,
		1u,
		ResourceType::DilatedVelocity,
		{sceneDepthSrvHandle, getSrvHandle(ResourceType::Velocity), sceneDepthSrvHandle, sceneDepthSrvHandle},
		constants)) {
		return false;
	}

	float disocclusionThreshold = 0.0025f;
	std::memcpy(&constants[37], &disocclusionThreshold, sizeof(float));

	if (!Dispatch(
		commandList,
		2u,
		ResourceType::DisocclusionMask,
		{sceneDepthSrvHandle, getSrvHandle(previousDepthType), getSrvHandle(ResourceType::DilatedVelocity), sceneDepthSrvHandle},
		constants)) {
		return false;
	}

	if (!Dispatch(
		commandList,
		3u,
		ResourceType::ReactiveMask,
		{sourceColorSrvHandle, materialMaskSrvHandle, getSrvHandle(ResourceType::DisocclusionMask), getSrvHandle(ResourceType::DilatedVelocity)},
		constants)) {
		return false;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE resolvedColorSrvHandle = sourceColorSrvHandle;

	if (ssrEnabled) {
		//============================================================
		// Hi-Z を使って SSR を追跡し、前フレーム結果と安定化する
		//============================================================

		std::memcpy(&constants[36], &cameraPosition[0], sizeof(float) * 3u);
		float reflectionDistance = 80.0f;
		std::memcpy(&constants[39], &reflectionDistance, sizeof(float));
		std::memcpy(&constants[20], viewProjectionMatrix, sizeof(float) * 16u);

		const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4u> additionalDepthPyramidSrvHandles = {
			depthPyramidSrvHandles[1],
			depthPyramidSrvHandles[2],
			depthPyramidSrvHandles[3],
			depthPyramidSrvHandles[4]
		};

		if (!Dispatch(
			commandList,
			4u,
			ResourceType::SsrTrace,
			{materialMaskSrvHandle, sceneDepthSrvHandle, reconstructedNormalSrvHandle, depthPyramidSrvHandles[0]},
			constants,
			&additionalDepthPyramidSrvHandles)) {
			return false;
		}

		if (!Dispatch(
			commandList,
			5u,
			ResourceType::SsrCurrent,
			{sourceColorSrvHandle, getSrvHandle(ResourceType::SsrTrace), materialMaskSrvHandle, reconstructedNormalSrvHandle},
			constants)) {
			return false;
		}

		std::memcpy(&constants[36], &historyValidValue, sizeof(float));

		if (!Dispatch(
			commandList,
			6u,
			ssrHistoryWriteType,
			{getSrvHandle(ResourceType::SsrCurrent), getSrvHandle(ssrHistoryReadType), getSrvHandle(ResourceType::DilatedVelocity), getSrvHandle(ResourceType::DisocclusionMask)},
			constants)) {
			return false;
		}

		if (!Dispatch(
			commandList,
			7u,
			ResourceType::SsrDenoised,
			{getSrvHandle(ssrHistoryWriteType), sceneDepthSrvHandle, reconstructedNormalSrvHandle, materialMaskSrvHandle},
			constants)) {
			return false;
		}

		if (!Dispatch(
			commandList,
			8u,
			ResourceType::ReflectionComposite,
			{sourceColorSrvHandle, getSrvHandle(ResourceType::SsrDenoised), materialMaskSrvHandle, getSrvHandle(ResourceType::DisocclusionMask)},
			constants)) {
			return false;
		}

		resolvedColorSrvHandle = getSrvHandle(ResourceType::ReflectionComposite);
	}

	//================================================================
	// 色履歴を近傍色へ制限してゴーストを抑え、次フレーム深度を保存
	//================================================================

	if (temporalEnabled) {
		const float temporalHistoryBlend = (std::clamp)(blendRatio, 0.0f, 0.98f);
		const float temporalSharpness = (std::clamp)(sharpness, 0.0f, 1.0f);
		std::memcpy(&constants[36], &historyValidValue, sizeof(float));
		std::memcpy(&constants[37], &temporalHistoryBlend, sizeof(float));
		std::memcpy(&constants[38], &temporalSharpness, sizeof(float));
		constants[39] = 0u;

		if (!Dispatch(
			commandList,
			9u,
			colorHistoryWriteType,
			{resolvedColorSrvHandle, getSrvHandle(colorHistoryReadType), getSrvHandle(ResourceType::DilatedVelocity), getSrvHandle(ResourceType::ReactiveMask)},
			constants)) {
			return false;
		}

		// Scene / Game の履歴は完全分離し、表示用TextureだけをViewport矩形で共有する。
		// これにより一方のカメラ履歴がもう一方の前フレーム色として読まれない。
		ID3D12Resource* colorHistoryResource =
			resources_[static_cast<size_t>(colorHistoryWriteType)].Get();
		ID3D12Resource* temporalOutputResource =
			resources_[static_cast<size_t>(ResourceType::TemporalOutput)].Get();

		if (colorHistoryResource == nullptr || temporalOutputResource == nullptr) {
			return false;
		}

		D3D12_RESOURCE_BARRIER copyBarriers[2]{};
		copyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		copyBarriers[0].Transition.pResource = colorHistoryResource;
		copyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		copyBarriers[0].Transition.StateBefore = kShaderReadState;
		copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		copyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		copyBarriers[1].Transition.pResource = temporalOutputResource;
		copyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		copyBarriers[1].Transition.StateBefore = kShaderReadState;
		copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		commandList->ResourceBarrier(2u, copyBarriers);

		const uint32_t viewportLeft = (std::min)(
			static_cast<uint32_t>((std::max)(viewportX, 0.0f)),
			renderWidth_);
		const uint32_t viewportTop = (std::min)(
			static_cast<uint32_t>((std::max)(viewportY, 0.0f)),
			renderHeight_);
		const uint32_t viewportRight = (std::min)(
			viewportLeft + static_cast<uint32_t>((std::max)(viewportWidth, 1.0f)),
			renderWidth_);
		const uint32_t viewportBottom = (std::min)(
			viewportTop + static_cast<uint32_t>((std::max)(viewportHeight, 1.0f)),
			renderHeight_);

		if (viewportLeft < viewportRight && viewportTop < viewportBottom) {
			D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
			destinationLocation.pResource = temporalOutputResource;
			destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destinationLocation.SubresourceIndex = 0u;
			D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
			sourceLocation.pResource = colorHistoryResource;
			sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			sourceLocation.SubresourceIndex = 0u;
			D3D12_BOX sourceBox{};
			sourceBox.left = viewportLeft;
			sourceBox.top = viewportTop;
			sourceBox.front = 0u;
			sourceBox.right = viewportRight;
			sourceBox.bottom = viewportBottom;
			sourceBox.back = 1u;
			commandList->CopyTextureRegion(
				&destinationLocation,
				viewportLeft,
				viewportTop,
				0u,
				&sourceLocation,
				&sourceBox);
		}

		copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		copyBarriers[0].Transition.StateAfter = kShaderReadState;
		copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		copyBarriers[1].Transition.StateAfter = kShaderReadState;
		commandList->ResourceBarrier(2u, copyBarriers);
		outputSrvHandle_ = getSrvHandle(ResourceType::TemporalOutput);
		outputResourceType_ = ResourceType::TemporalOutput;
	}
	else {
		outputSrvHandle_ = resolvedColorSrvHandle;
		outputResourceType_ = ResourceType::ReflectionComposite;
	}

	if (!Dispatch(
		commandList,
		10u,
		previousDepthType,
		{sceneDepthSrvHandle, sceneDepthSrvHandle, sceneDepthSrvHandle, sceneDepthSrvHandle},
		constants)) {
		return false;
	}

	std::memcpy(
		previousViewProjectionMatrices_[viewHistoryIndex].data(),
		viewProjectionMatrix,
		sizeof(float) * 16u);
	std::memcpy(
		previousViewportRects_[viewHistoryIndex].data(),
		viewportRect,
		sizeof(viewportRect));
	isHistoryValid_[viewHistoryIndex] = true;
	lastSsrEnabled_[viewHistoryIndex] = ssrEnabled;
	lastTemporalEnabled_[viewHistoryIndex] = temporalEnabled;

	if (advanceHistoryFrame) {
		historyWriteIndices_[viewHistoryIndex] = 1u - historyWriteIndex;
	}

	return true;
}

void EditorTemporalRenderingManager::Finalize() {
	ReleaseSizeDependentResources();

	for (Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState : pipelineStates_) {
		pipelineState.Reset();
	}

	computeRootSignature_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	isInitialized_ = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorTemporalRenderingManager::GetOutputSrvHandle() const {
	return outputSrvHandle_;
}

ID3D12Resource* EditorTemporalRenderingManager::GetOutputResource() const {
	return resources_[static_cast<size_t>(outputResourceType_)].Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorTemporalRenderingManager::GetVelocitySrvHandle() const {
	return srvHandles_[static_cast<size_t>(ResourceType::DilatedVelocity)];
}

bool EditorTemporalRenderingManager::CreateRootSignatureAndPipelineStates(
	const std::array<IDxcBlob*, kPipelineCount>& computeShaderBlobs) {

	std::array<D3D12_DESCRIPTOR_RANGE, 9u> descriptorRanges{};
	std::array<D3D12_ROOT_PARAMETER, 10u> rootParameters{};

	for (uint32_t descriptorIndex = 0u;
		descriptorIndex < static_cast<uint32_t>(descriptorRanges.size());
		descriptorIndex++) {
		D3D12_DESCRIPTOR_RANGE& descriptorRange = descriptorRanges[descriptorIndex];
		descriptorRange.RangeType = descriptorIndex < 8u
			? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
			: D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		descriptorRange.NumDescriptors = 1u;
		descriptorRange.BaseShaderRegister = descriptorIndex < 8u ? descriptorIndex : 0u;
		descriptorRange.RegisterSpace = 0u;
		descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER& rootParameter = rootParameters[descriptorIndex];
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameter.DescriptorTable.NumDescriptorRanges = 1u;
		rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRange;
	}

	rootParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[9].Constants.ShaderRegister = 0u;
	rootParameters[9].Constants.RegisterSpace = 0u;
	rootParameters[9].Constants.Num32BitValues = kComputeConstantCount;

	std::array<D3D12_STATIC_SAMPLER_DESC, 2u> samplers{};
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].ShaderRegister = 0u;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[1] = samplers[0];
	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[1].ShaderRegister = 1u;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
	rootSignatureDescription.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDescription.pParameters = rootParameters.data();
	rootSignatureDescription.NumStaticSamplers = static_cast<UINT>(samplers.size());
	rootSignatureDescription.pStaticSamplers = samplers.data();
	rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

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
		IID_PPV_ARGS(computeRootSignature_.GetAddressOf()));

	if (FAILED(result) || computeRootSignature_ == nullptr) {
		return false;
	}

	for (uint32_t pipelineIndex = 0u; pipelineIndex < kPipelineCount; pipelineIndex++) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature = computeRootSignature_.Get();
		pipelineStateDescription.CS = {
			computeShaderBlobs[pipelineIndex]->GetBufferPointer(),
			computeShaderBlobs[pipelineIndex]->GetBufferSize()
		};

		result = device_->CreateComputePipelineState(
			&pipelineStateDescription,
			IID_PPV_ARGS(pipelineStates_[pipelineIndex].GetAddressOf()));

		if (FAILED(result) || pipelineStates_[pipelineIndex] == nullptr) {
			return false;
		}
	}

	return true;
}

bool EditorTemporalRenderingManager::CreateSizeDependentResources(
	uint32_t renderWidth,
	uint32_t renderHeight) {

	const std::array<DXGI_FORMAT, static_cast<size_t>(ResourceType::Count)> resourceFormats = {
		DXGI_FORMAT_R16G16_FLOAT,  // Velocity
		DXGI_FORMAT_R16G16_FLOAT,  // DilatedVelocity
		DXGI_FORMAT_R32_FLOAT,  // PreviousDepthScene
		DXGI_FORMAT_R32_FLOAT,  // PreviousDepthGame
		DXGI_FORMAT_R16_FLOAT,  // DisocclusionMask
		DXGI_FORMAT_R16_FLOAT,  // ReactiveMask
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrTrace
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrCurrent
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrHistoryScene0
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrHistoryScene1
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrHistoryGame0
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrHistoryGame1
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // SsrDenoised
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // ReflectionComposite
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // ColorHistoryScene0
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // ColorHistoryScene1
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // ColorHistoryGame0
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // ColorHistoryGame1
		DXGI_FORMAT_R16G16B16A16_FLOAT,  // TemporalOutput
	};

	for (uint32_t resourceIndex = 0u;
		resourceIndex < static_cast<uint32_t>(ResourceType::Count);
		resourceIndex++) {
		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = static_cast<UINT64>(renderWidth);
		resourceDescription.Height = renderHeight;
		resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
		resourceDescription.MipLevels = static_cast<UINT16>(1u);
		resourceDescription.Format = resourceFormats[resourceIndex];
		resourceDescription.SampleDesc.Count = 1u;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		HRESULT result = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			kShaderReadState,
			nullptr,
			IID_PPV_ARGS(resources_[resourceIndex].GetAddressOf()));

		if (FAILED(result) || resources_[resourceIndex] == nullptr) {
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = resourceFormats[resourceIndex];
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescription.Texture2D.MipLevels = 1u;
		device_->CreateShaderResourceView(
			resources_[resourceIndex].Get(),
			&srvDescription,
			GetCpuDescriptorHandle(GetSrvDescriptorIndex(resourceIndex)));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
		uavDescription.Format = resourceFormats[resourceIndex];
		uavDescription.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device_->CreateUnorderedAccessView(
			resources_[resourceIndex].Get(),
			nullptr,
			&uavDescription,
			GetCpuDescriptorHandle(GetUavDescriptorIndex(resourceIndex)));

		srvHandles_[resourceIndex] = GetGpuDescriptorHandle(GetSrvDescriptorIndex(resourceIndex));
		uavHandles_[resourceIndex] = GetGpuDescriptorHandle(GetUavDescriptorIndex(resourceIndex));
	}

	historyWriteIndices_.fill(0u);
	isHistoryValid_.fill(false);
	outputSrvHandle_ = {};
	outputResourceType_ = ResourceType::TemporalOutput;
	lastSsrEnabled_.fill(false);
	lastTemporalEnabled_.fill(false);

	for (std::array<float, 16u>& previousMatrix : previousViewProjectionMatrices_) {
		previousMatrix.fill(0.0f);
	}

	for (std::array<float, 4u>& previousViewport : previousViewportRects_) {
		previousViewport.fill(0.0f);
	}

	return true;
}

void EditorTemporalRenderingManager::ReleaseSizeDependentResources() {
	for (Microsoft::WRL::ComPtr<ID3D12Resource>& resource : resources_) {
		resource.Reset();
	}

	srvHandles_.fill({});
	uavHandles_.fill({});
	renderWidth_ = 0u;
	renderHeight_ = 0u;
	historyWriteIndices_.fill(0u);
	isHistoryValid_.fill(false);
	previousViewportRects_.fill({});
	outputSrvHandle_ = {};
	outputResourceType_ = ResourceType::TemporalOutput;
}

bool EditorTemporalRenderingManager::Dispatch(
	ID3D12GraphicsCommandList* commandList,
	uint32_t pipelineIndex,
	ResourceType destinationResourceType,
	const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4u>& sourceSrvHandles,
	const std::array<uint32_t, 44u>& constants,
	const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4u>* additionalSourceSrvHandles) {

	if (pipelineIndex >= pipelineStates_.size()) {
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
	transitionBarrier.Transition.StateBefore = kShaderReadState;
	transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &transitionBarrier);

	commandList->SetPipelineState(pipelineStates_[pipelineIndex].Get());

	for (uint32_t sourceIndex = 0u; sourceIndex < sourceSrvHandles.size(); sourceIndex++) {
		commandList->SetComputeRootDescriptorTable(sourceIndex, sourceSrvHandles[sourceIndex]);
	}

	for (uint32_t sourceIndex = 0u; sourceIndex < sourceSrvHandles.size(); sourceIndex++) {
		const D3D12_GPU_DESCRIPTOR_HANDLE additionalSourceSrvHandle =
			additionalSourceSrvHandles == nullptr
			? sourceSrvHandles[3]
			: (*additionalSourceSrvHandles)[sourceIndex];
		commandList->SetComputeRootDescriptorTable(4u + sourceIndex, additionalSourceSrvHandle);
	}

	commandList->SetComputeRootDescriptorTable(8u, uavHandles_[destinationIndex]);
	commandList->SetComputeRoot32BitConstants(9u, kComputeConstantCount, constants.data(), 0u);
	float viewportWidth = 1.0f;
	float viewportHeight = 1.0f;
	std::memcpy(&viewportWidth, &constants[42], sizeof(float));
	std::memcpy(&viewportHeight, &constants[43], sizeof(float));
	const uint32_t dispatchWidth = static_cast<uint32_t>(std::ceil((std::max)(viewportWidth, 1.0f)));
	const uint32_t dispatchHeight = static_cast<uint32_t>(std::ceil((std::max)(viewportHeight, 1.0f)));
	commandList->Dispatch(
		(dispatchWidth + kThreadGroupSize - 1u) / kThreadGroupSize,
		(dispatchHeight + kThreadGroupSize - 1u) / kThreadGroupSize,
		1u);

	D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
	unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	unorderedAccessBarrier.UAV.pResource = destinationResource;
	commandList->ResourceBarrier(1u, &unorderedAccessBarrier);

	transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	transitionBarrier.Transition.StateAfter = kShaderReadState;
	commandList->ResourceBarrier(1u, &transitionBarrier);
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorTemporalRenderingManager::GetCpuDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorTemporalRenderingManager::GetGpuDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}
