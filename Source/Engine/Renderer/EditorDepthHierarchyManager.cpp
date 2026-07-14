#include "EditorDepthHierarchyManager.h"

#include <algorithm>
#include <cstring>

namespace {
	constexpr uint32_t kDepthPyramidDescriptorStartIndex = 31u;
	constexpr uint32_t kDepthPyramidDescriptorStride = 2u;
	constexpr uint32_t kReconstructedNormalSrvDescriptorIndex = 55u;
	constexpr uint32_t kReconstructedNormalUavDescriptorIndex = 56u;
	constexpr uint32_t kComputeConstantCount = 20u;
	constexpr uint32_t kThreadGroupSize = 8u;

	uint32_t GetDepthPyramidSrvDescriptorIndex(uint32_t levelIndex) {
		return kDepthPyramidDescriptorStartIndex + levelIndex * kDepthPyramidDescriptorStride;
	}

	uint32_t GetDepthPyramidUavDescriptorIndex(uint32_t levelIndex) {
		return GetDepthPyramidSrvDescriptorIndex(levelIndex) + 1u;
	}
}

bool EditorDepthHierarchyManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	IDxcBlob* depthPyramidShaderBlob,
	IDxcBlob* depthDownsampleShaderBlob,
	IDxcBlob* reconstructNormalShaderBlob,
	uint32_t renderWidth,
	uint32_t renderHeight) {

	//================================================================
	// 初期化前提の検証
	//================================================================

	if (device == nullptr ||
		srvDescriptorHeap == nullptr ||
		srvDescriptorSize == 0u ||
		depthPyramidShaderBlob == nullptr ||
		depthDownsampleShaderBlob == nullptr ||
		reconstructNormalShaderBlob == nullptr) {
		return false;
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;

	if (!CreateRootSignatureAndPipelineStates(
		depthPyramidShaderBlob,
		depthDownsampleShaderBlob,
		reconstructNormalShaderBlob)) {
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

bool EditorDepthHierarchyManager::Resize(uint32_t renderWidth, uint32_t renderHeight) {
	if (!isInitialized_ || renderWidth == 0u || renderHeight == 0u) {
		return false;
	}

	if (renderWidth_ == renderWidth && renderHeight_ == renderHeight) {
		return true;
	}

	ReleaseSizeDependentResources();
	renderWidth_ = renderWidth;
	renderHeight_ = renderHeight;

	if (!CreateDepthPyramidResources(renderWidth_, renderHeight_)) {
		ReleaseSizeDependentResources();
		return false;
	}

	if (!CreateReconstructedNormalResource(renderWidth_, renderHeight_)) {
		ReleaseSizeDependentResources();
		return false;
	}

	return true;
}

bool EditorDepthHierarchyManager::Generate(
	ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvHandle,
	const float* inverseViewProjectionMatrix) {

	if (!isInitialized_ ||
		commandList == nullptr ||
		sceneDepthSrvHandle.ptr == 0u ||
		inverseViewProjectionMatrix == nullptr ||
		activeLevelCount_ == 0u) {
		return false;
	}

	//================================================================
	// 深度ピラミッド生成
	//================================================================

	commandList->SetComputeRootSignature(computeRootSignature_.Get());

	for (uint32_t levelIndex = 0u; levelIndex < activeLevelCount_; levelIndex++) {
		ID3D12Resource* destinationResource = depthPyramidResources_[levelIndex].Get();

		D3D12_RESOURCE_BARRIER destinationBarrier{};
		destinationBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		destinationBarrier.Transition.pResource = destinationResource;
		destinationBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		destinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		destinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		commandList->ResourceBarrier(1u, &destinationBarrier);

		const bool isFirstLevel = levelIndex == 0u;
		const uint32_t sourceWidth = isFirstLevel ? renderWidth_ : depthPyramidWidths_[levelIndex - 1u];
		const uint32_t sourceHeight = isFirstLevel ? renderHeight_ : depthPyramidHeights_[levelIndex - 1u];
		const D3D12_GPU_DESCRIPTOR_HANDLE sourceSrvHandle = isFirstLevel
			? sceneDepthSrvHandle
			: depthPyramidSrvHandles_[levelIndex - 1u];

		uint32_t depthConstants[4] = {
			sourceWidth,
			sourceHeight,
			depthPyramidWidths_[levelIndex],
			depthPyramidHeights_[levelIndex]
		};

		commandList->SetPipelineState(
			isFirstLevel
				? depthPyramidPipelineState_.Get()
				: depthDownsamplePipelineState_.Get());
		commandList->SetComputeRootDescriptorTable(0u, sourceSrvHandle);
		commandList->SetComputeRootDescriptorTable(1u, depthPyramidUavHandles_[levelIndex]);
		commandList->SetComputeRoot32BitConstants(2u, 4u, depthConstants, 0u);

		const uint32_t dispatchGroupX =
			(depthPyramidWidths_[levelIndex] + kThreadGroupSize - 1u) / kThreadGroupSize;
		const uint32_t dispatchGroupY =
			(depthPyramidHeights_[levelIndex] + kThreadGroupSize - 1u) / kThreadGroupSize;
		commandList->Dispatch(dispatchGroupX, dispatchGroupY, 1u);

		D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
		unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		unorderedAccessBarrier.UAV.pResource = destinationResource;
		commandList->ResourceBarrier(1u, &unorderedAccessBarrier);

		destinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		destinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1u, &destinationBarrier);
	}

	//================================================================
	// 深度からワールド法線を再構築
	//================================================================

	D3D12_RESOURCE_BARRIER normalBarrier{};
	normalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	normalBarrier.Transition.pResource = reconstructedNormalResource_.Get();
	normalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	normalBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	normalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &normalBarrier);

	std::array<uint32_t, kComputeConstantCount> normalConstants{};
	normalConstants[0] = renderWidth_;
	normalConstants[1] = renderHeight_;
	normalConstants[2] = renderWidth_;
	normalConstants[3] = renderHeight_;
	std::memcpy(&normalConstants[4], inverseViewProjectionMatrix, sizeof(float) * 16u);

	commandList->SetPipelineState(reconstructNormalPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, sceneDepthSrvHandle);
	commandList->SetComputeRootDescriptorTable(1u, reconstructedNormalUavHandle_);
	commandList->SetComputeRoot32BitConstants(
		2u,
		kComputeConstantCount,
		normalConstants.data(),
		0u);
	commandList->Dispatch(
		(renderWidth_ + kThreadGroupSize - 1u) / kThreadGroupSize,
		(renderHeight_ + kThreadGroupSize - 1u) / kThreadGroupSize,
		1u);

	D3D12_RESOURCE_BARRIER normalUnorderedAccessBarrier{};
	normalUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	normalUnorderedAccessBarrier.UAV.pResource = reconstructedNormalResource_.Get();
	commandList->ResourceBarrier(1u, &normalUnorderedAccessBarrier);

	normalBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	normalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &normalBarrier);

	return true;
}

void EditorDepthHierarchyManager::Finalize() {
	ReleaseSizeDependentResources();
	reconstructNormalPipelineState_.Reset();
	depthDownsamplePipelineState_.Reset();
	depthPyramidPipelineState_.Reset();
	computeRootSignature_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	isInitialized_ = false;
}

uint32_t EditorDepthHierarchyManager::GetActiveLevelCount() const {
	return activeLevelCount_;
}

uint32_t EditorDepthHierarchyManager::GetDepthPyramidWidth(uint32_t levelIndex) const {
	return levelIndex < activeLevelCount_ ? depthPyramidWidths_[levelIndex] : 0u;
}

uint32_t EditorDepthHierarchyManager::GetDepthPyramidHeight(uint32_t levelIndex) const {
	return levelIndex < activeLevelCount_ ? depthPyramidHeights_[levelIndex] : 0u;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorDepthHierarchyManager::GetDepthPyramidSrvHandle(uint32_t levelIndex) const {
	if (levelIndex >= activeLevelCount_) {
		return {};
	}

	return depthPyramidSrvHandles_[levelIndex];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorDepthHierarchyManager::GetReconstructedNormalSrvHandle() const {
	return reconstructedNormalSrvHandle_;
}

bool EditorDepthHierarchyManager::CreateRootSignatureAndPipelineStates(
	IDxcBlob* depthPyramidShaderBlob,
	IDxcBlob* depthDownsampleShaderBlob,
	IDxcBlob* reconstructNormalShaderBlob) {

	D3D12_DESCRIPTOR_RANGE srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1u;
	srvRange.BaseShaderRegister = 0u;
	srvRange.RegisterSpace = 0u;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE uavRange{};
	uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange.NumDescriptors = 1u;
	uavRange.BaseShaderRegister = 0u;
	uavRange.RegisterSpace = 0u;
	uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[3]{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1u;
	rootParameters[0].DescriptorTable.pDescriptorRanges = &srvRange;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1u;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &uavRange;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[2].Constants.ShaderRegister = 0u;
	rootParameters[2].Constants.RegisterSpace = 0u;
	rootParameters[2].Constants.Num32BitValues = kComputeConstantCount;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
	rootSignatureDescription.NumParameters = static_cast<UINT>(std::size(rootParameters));
	rootSignatureDescription.pParameters = rootParameters;
	rootSignatureDescription.NumStaticSamplers = 0u;
	rootSignatureDescription.pStaticSamplers = nullptr;
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

	auto createComputePipelineState = [this](
		IDxcBlob* shaderBlob,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) {

		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature = computeRootSignature_.Get();
		pipelineStateDescription.CS = {
			shaderBlob->GetBufferPointer(),
			shaderBlob->GetBufferSize()
		};

		return device_->CreateComputePipelineState(
			&pipelineStateDescription,
			IID_PPV_ARGS(pipelineState.GetAddressOf()));
	};

	result = createComputePipelineState(depthPyramidShaderBlob, depthPyramidPipelineState_);

	if (FAILED(result) || depthPyramidPipelineState_ == nullptr) {
		return false;
	}

	result = createComputePipelineState(depthDownsampleShaderBlob, depthDownsamplePipelineState_);

	if (FAILED(result) || depthDownsamplePipelineState_ == nullptr) {
		return false;
	}

	result = createComputePipelineState(reconstructNormalShaderBlob, reconstructNormalPipelineState_);

	return SUCCEEDED(result) && reconstructNormalPipelineState_ != nullptr;
}

bool EditorDepthHierarchyManager::CreateDepthPyramidResources(uint32_t renderWidth, uint32_t renderHeight) {
	uint32_t levelWidth = renderWidth;
	uint32_t levelHeight = renderHeight;
	activeLevelCount_ = 0u;

	for (uint32_t levelIndex = 0u; levelIndex < kMaxDepthPyramidLevelCount; levelIndex++) {
		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Alignment = 0u;
		resourceDescription.Width = static_cast<UINT64>(levelWidth);
		resourceDescription.Height = levelHeight;
		resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
		resourceDescription.MipLevels = static_cast<UINT16>(1u);
		resourceDescription.Format = DXGI_FORMAT_R32G32_FLOAT;
		resourceDescription.SampleDesc.Count = 1u;
		resourceDescription.SampleDesc.Quality = 0u;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		HRESULT result = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(depthPyramidResources_[levelIndex].GetAddressOf()));

		if (FAILED(result) || depthPyramidResources_[levelIndex] == nullptr) {
			return false;
		}

		const uint32_t srvDescriptorIndex = GetDepthPyramidSrvDescriptorIndex(levelIndex);
		const uint32_t uavDescriptorIndex = GetDepthPyramidUavDescriptorIndex(levelIndex);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = DXGI_FORMAT_R32G32_FLOAT;
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescription.Texture2D.MostDetailedMip = 0u;
		srvDescription.Texture2D.MipLevels = 1u;
		device_->CreateShaderResourceView(
			depthPyramidResources_[levelIndex].Get(),
			&srvDescription,
			GetCpuDescriptorHandle(srvDescriptorIndex));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
		uavDescription.Format = DXGI_FORMAT_R32G32_FLOAT;
		uavDescription.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDescription.Texture2D.MipSlice = 0u;
		device_->CreateUnorderedAccessView(
			depthPyramidResources_[levelIndex].Get(),
			nullptr,
			&uavDescription,
			GetCpuDescriptorHandle(uavDescriptorIndex));

		depthPyramidSrvHandles_[levelIndex] = GetGpuDescriptorHandle(srvDescriptorIndex);
		depthPyramidUavHandles_[levelIndex] = GetGpuDescriptorHandle(uavDescriptorIndex);
		depthPyramidWidths_[levelIndex] = levelWidth;
		depthPyramidHeights_[levelIndex] = levelHeight;
		activeLevelCount_++;

		if (levelWidth == 1u && levelHeight == 1u) {
			break;
		}

		levelWidth = (std::max)(1u, (levelWidth + 1u) / 2u);
		levelHeight = (std::max)(1u, (levelHeight + 1u) / 2u);
	}

	return true;
}

bool EditorDepthHierarchyManager::CreateReconstructedNormalResource(
	uint32_t renderWidth,
	uint32_t renderHeight) {

	D3D12_RESOURCE_DESC resourceDescription{};
	resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDescription.Alignment = 0u;
	resourceDescription.Width = static_cast<UINT64>(renderWidth);
	resourceDescription.Height = renderHeight;
	resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
	resourceDescription.MipLevels = static_cast<UINT16>(1u);
	resourceDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	resourceDescription.SampleDesc.Count = 1u;
	resourceDescription.SampleDesc.Quality = 0u;
	resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT result = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDescription,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(reconstructedNormalResource_.GetAddressOf()));

	if (FAILED(result) || reconstructedNormalResource_ == nullptr) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
	srvDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescription.Texture2D.MostDetailedMip = 0u;
	srvDescription.Texture2D.MipLevels = 1u;
	device_->CreateShaderResourceView(
		reconstructedNormalResource_.Get(),
		&srvDescription,
		GetCpuDescriptorHandle(kReconstructedNormalSrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
	uavDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	uavDescription.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDescription.Texture2D.MipSlice = 0u;
	device_->CreateUnorderedAccessView(
		reconstructedNormalResource_.Get(),
		nullptr,
		&uavDescription,
		GetCpuDescriptorHandle(kReconstructedNormalUavDescriptorIndex));

	reconstructedNormalSrvHandle_ = GetGpuDescriptorHandle(kReconstructedNormalSrvDescriptorIndex);
	reconstructedNormalUavHandle_ = GetGpuDescriptorHandle(kReconstructedNormalUavDescriptorIndex);
	return true;
}

void EditorDepthHierarchyManager::ReleaseSizeDependentResources() {
	for (Microsoft::WRL::ComPtr<ID3D12Resource>& depthPyramidResource : depthPyramidResources_) {
		depthPyramidResource.Reset();
	}

	reconstructedNormalResource_.Reset();
	depthPyramidSrvHandles_.fill({});
	depthPyramidUavHandles_.fill({});
	depthPyramidWidths_.fill(0u);
	depthPyramidHeights_.fill(0u);
	reconstructedNormalSrvHandle_ = {};
	reconstructedNormalUavHandle_ = {};
	renderWidth_ = 0u;
	renderHeight_ = 0u;
	activeLevelCount_ = 0u;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorDepthHierarchyManager::GetCpuDescriptorHandle(uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorDepthHierarchyManager::GetGpuDescriptorHandle(uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}
