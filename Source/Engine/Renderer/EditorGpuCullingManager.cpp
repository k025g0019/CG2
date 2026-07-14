#include "EditorGpuCullingManager.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {
	constexpr uint32_t kObjectSrvDescriptorIndex = 83u;
	constexpr uint32_t kFrustumVisibilitySrvDescriptorIndex = 84u;
	constexpr uint32_t kFrustumVisibilityUavDescriptorIndex = 85u;
	constexpr uint32_t kVisibilitySrvDescriptorIndex = 86u;
	constexpr uint32_t kVisibilityUavDescriptorIndex = 87u;
	constexpr uint32_t kDrawArgumentsSrvDescriptorIndex = 88u;
	constexpr uint32_t kDrawArgumentsUavDescriptorIndex = 89u;
	constexpr uint32_t kComputeConstantCount = 24u;
	constexpr uint32_t kThreadGroupSize = 64u;
}

bool EditorGpuCullingManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	IDxcBlob* frustumCullingShaderBlob,
	IDxcBlob* occlusionCullingShaderBlob,
	IDxcBlob* buildIndirectArgsShaderBlob) {

	if (device == nullptr || srvDescriptorHeap == nullptr || srvDescriptorSize == 0u ||
		frustumCullingShaderBlob == nullptr || occlusionCullingShaderBlob == nullptr ||
		buildIndirectArgsShaderBlob == nullptr) {
		return false;
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;

	if (!CreateRootSignatureAndPipelineStates(
		frustumCullingShaderBlob,
		occlusionCullingShaderBlob,
		buildIndirectArgsShaderBlob)) {
		Finalize();
		return false;
	}

	if (!CreateBuffers()) {
		Finalize();
		return false;
	}

	isInitialized_ = true;
	return true;
}

bool EditorGpuCullingManager::Execute(
	ID3D12GraphicsCommandList* commandList,
	const std::vector<EditorGpuCullingInput>& cullingInputs,
	D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrvHandle,
	const float* viewProjectionMatrix,
	uint32_t depthPyramidWidth,
	uint32_t depthPyramidHeight) {

	if (!isInitialized_ || commandList == nullptr || depthPyramidSrvHandle.ptr == 0u ||
		viewProjectionMatrix == nullptr || depthPyramidWidth == 0u || depthPyramidHeight == 0u) {
		return false;
	}

	const uint32_t objectCount = (std::min)(
		static_cast<uint32_t>(cullingInputs.size()),
		kMaximumObjectCount);

	if (objectCount == 0u) {
		submittedObjectCount_ = 0u;
		hasPendingReadback_ = false;
		return true;
	}

	//================================================================
	// CPU で確定したワールド AABB を Upload Buffer へ書き込む
	//================================================================

	void* mappedObjectData = nullptr;
	D3D12_RANGE noReadRange{0u, 0u};
	HRESULT result = objectUploadResource_->Map(0u, &noReadRange, &mappedObjectData);

	if (FAILED(result) || mappedObjectData == nullptr) {
		return false;
	}

	std::memcpy(
		mappedObjectData,
		cullingInputs.data(),
		static_cast<size_t>(objectCount) * sizeof(EditorGpuCullingInput));
	objectUploadResource_->Unmap(0u, nullptr);

	submittedGameObjectIds_.resize(objectCount);

	for (uint32_t objectIndex = 0u; objectIndex < objectCount; objectIndex++) {
		submittedGameObjectIds_[objectIndex] = cullingInputs[objectIndex].gameObjectId;
	}

	std::array<uint32_t, kComputeConstantCount> constants{};
	constants[0] = objectCount;
	constants[1] = depthPyramidWidth;
	constants[2] = depthPyramidHeight;
	float depthBias = 0.0015f;
	std::memcpy(&constants[3], &depthBias, sizeof(float));
	std::memcpy(&constants[4], viewProjectionMatrix, sizeof(float) * 16u);

	commandList->SetComputeRootSignature(computeRootSignature_.Get());

	//================================================================
	// Pass 1: ワールド AABB を視錐台へ通し、画面外の物体を除外する
	//================================================================

	D3D12_RESOURCE_BARRIER frustumVisibilityBarrier{};
	frustumVisibilityBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	frustumVisibilityBarrier.Transition.pResource = frustumVisibilityResource_.Get();
	frustumVisibilityBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	frustumVisibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	frustumVisibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &frustumVisibilityBarrier);

	commandList->SetPipelineState(frustumCullingPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, objectSrvHandle_);
	commandList->SetComputeRootDescriptorTable(1u, depthPyramidSrvHandle);
	commandList->SetComputeRootDescriptorTable(2u, visibilitySrvHandle_);
	commandList->SetComputeRootDescriptorTable(3u, frustumVisibilityUavHandle_);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER frustumVisibilityUnorderedAccessBarrier{};
	frustumVisibilityUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	frustumVisibilityUnorderedAccessBarrier.UAV.pResource = frustumVisibilityResource_.Get();
	commandList->ResourceBarrier(1u, &frustumVisibilityUnorderedAccessBarrier);

	frustumVisibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	frustumVisibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &frustumVisibilityBarrier);

	//================================================================
	// Pass 2: 視錐台内の物体だけを Hi-Z 深度と比較する
	//================================================================

	D3D12_RESOURCE_BARRIER visibilityBarrier{};
	visibilityBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	visibilityBarrier.Transition.pResource = visibilityResource_.Get();
	visibilityBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	visibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	visibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &visibilityBarrier);

	commandList->SetPipelineState(occlusionCullingPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, objectSrvHandle_);
	commandList->SetComputeRootDescriptorTable(1u, depthPyramidSrvHandle);
	commandList->SetComputeRootDescriptorTable(2u, frustumVisibilitySrvHandle_);
	commandList->SetComputeRootDescriptorTable(3u, visibilityUavHandle_);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER visibilityUnorderedAccessBarrier{};
	visibilityUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	visibilityUnorderedAccessBarrier.UAV.pResource = visibilityResource_.Get();
	commandList->ResourceBarrier(1u, &visibilityUnorderedAccessBarrier);

	visibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	visibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &visibilityBarrier);

	//================================================================
	// Pass 3: 可視状態を D3D12_DRAW_ARGUMENTS の頂点数へ変換する
	//================================================================

	D3D12_RESOURCE_BARRIER drawArgumentsBarrier{};
	drawArgumentsBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	drawArgumentsBarrier.Transition.pResource = drawArgumentsResource_.Get();
	drawArgumentsBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	drawArgumentsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	drawArgumentsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &drawArgumentsBarrier);

	commandList->SetPipelineState(buildIndirectArgsPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, objectSrvHandle_);
	commandList->SetComputeRootDescriptorTable(1u, visibilitySrvHandle_);
	commandList->SetComputeRootDescriptorTable(2u, visibilitySrvHandle_);
	commandList->SetComputeRootDescriptorTable(3u, drawArgumentsUavHandle_);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER drawArgumentsUnorderedAccessBarrier{};
	drawArgumentsUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	drawArgumentsUnorderedAccessBarrier.UAV.pResource = drawArgumentsResource_.Get();
	commandList->ResourceBarrier(1u, &drawArgumentsUnorderedAccessBarrier);

	drawArgumentsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	drawArgumentsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	commandList->ResourceBarrier(1u, &drawArgumentsBarrier);
	commandList->CopyBufferRegion(
		drawArgumentsReadbackResource_.Get(),
		0u,
		drawArgumentsResource_.Get(),
		0u,
		static_cast<UINT64>(objectCount) * sizeof(DrawArguments));

	drawArgumentsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	drawArgumentsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &drawArgumentsBarrier);

	submittedObjectCount_ = objectCount;
	hasPendingReadback_ = true;
	return true;
}

void EditorGpuCullingManager::ResolveReadback() {
	if (!hasPendingReadback_ || submittedObjectCount_ == 0u || drawArgumentsReadbackResource_ == nullptr) {
		return;
	}

	const SIZE_T readbackSize = static_cast<SIZE_T>(submittedObjectCount_) * sizeof(DrawArguments);
	D3D12_RANGE readRange{0u, readbackSize};
	void* mappedDrawArguments = nullptr;
	HRESULT result = drawArgumentsReadbackResource_->Map(0u, &readRange, &mappedDrawArguments);

	if (FAILED(result) || mappedDrawArguments == nullptr) {
		return;
	}

	const DrawArguments* drawArguments = static_cast<const DrawArguments*>(mappedDrawArguments);
	visibilityByGameObjectId_.clear();

	for (uint32_t objectIndex = 0u; objectIndex < submittedObjectCount_; objectIndex++) {
		visibilityByGameObjectId_[submittedGameObjectIds_[objectIndex]] =
			drawArguments[objectIndex].vertexCountPerInstance > 0u;
	}

	D3D12_RANGE noWriteRange{0u, 0u};
	drawArgumentsReadbackResource_->Unmap(0u, &noWriteRange);
	hasPendingReadback_ = false;
}

void EditorGpuCullingManager::Finalize() {
	drawArgumentsReadbackResource_.Reset();
	drawArgumentsResource_.Reset();
	visibilityResource_.Reset();
	frustumVisibilityResource_.Reset();
	objectUploadResource_.Reset();
	buildIndirectArgsPipelineState_.Reset();
	occlusionCullingPipelineState_.Reset();
	frustumCullingPipelineState_.Reset();
	computeRootSignature_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	submittedGameObjectIds_.clear();
	visibilityByGameObjectId_.clear();
	submittedObjectCount_ = 0u;
	hasPendingReadback_ = false;
	isInitialized_ = false;
}

bool EditorGpuCullingManager::IsVisible(int32_t gameObjectId) const {
	const auto visibilityIterator = visibilityByGameObjectId_.find(gameObjectId);

	if (visibilityIterator == visibilityByGameObjectId_.end()) {
		return true;
	}

	return visibilityIterator->second;
}

bool EditorGpuCullingManager::CreateRootSignatureAndPipelineStates(
	IDxcBlob* frustumCullingShaderBlob,
	IDxcBlob* occlusionCullingShaderBlob,
	IDxcBlob* buildIndirectArgsShaderBlob) {

	std::array<D3D12_DESCRIPTOR_RANGE, 4u> descriptorRanges{};
	std::array<D3D12_ROOT_PARAMETER, 5u> rootParameters{};

	for (uint32_t descriptorIndex = 0u; descriptorIndex < descriptorRanges.size(); descriptorIndex++) {
		D3D12_DESCRIPTOR_RANGE& descriptorRange = descriptorRanges[descriptorIndex];
		descriptorRange.RangeType = descriptorIndex < 3u
			? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
			: D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		descriptorRange.NumDescriptors = 1u;
		descriptorRange.BaseShaderRegister = descriptorIndex < 3u ? descriptorIndex : 0u;
		descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER& rootParameter = rootParameters[descriptorIndex];
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameter.DescriptorTable.NumDescriptorRanges = 1u;
		rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRange;
	}

	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[4].Constants.ShaderRegister = 0u;
	rootParameters[4].Constants.Num32BitValues = kComputeConstantCount;

	D3D12_STATIC_SAMPLER_DESC pointSampler{};
	pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	pointSampler.ShaderRegister = 0u;
	pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	pointSampler.MaxLOD = D3D12_FLOAT32_MAX;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
	rootSignatureDescription.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDescription.pParameters = rootParameters.data();
	rootSignatureDescription.NumStaticSamplers = 1u;
	rootSignatureDescription.pStaticSamplers = &pointSampler;

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

	auto createPipelineState = [this](
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

	result = createPipelineState(frustumCullingShaderBlob, frustumCullingPipelineState_);

	if (FAILED(result) || frustumCullingPipelineState_ == nullptr) {
		return false;
	}

	result = createPipelineState(occlusionCullingShaderBlob, occlusionCullingPipelineState_);

	if (FAILED(result) || occlusionCullingPipelineState_ == nullptr) {
		return false;
	}

	result = createPipelineState(buildIndirectArgsShaderBlob, buildIndirectArgsPipelineState_);
	return SUCCEEDED(result) && buildIndirectArgsPipelineState_ != nullptr;
}

bool EditorGpuCullingManager::CreateBuffers() {
	const UINT64 objectBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(EditorGpuCullingInput);
	const UINT64 visibilityBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(uint32_t);
	const UINT64 drawArgumentsBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(DrawArguments);

	auto createBuffer = [this](
		UINT64 bufferSize,
		D3D12_HEAP_TYPE heapType,
		D3D12_RESOURCE_FLAGS resourceFlags,
		D3D12_RESOURCE_STATES initialState,
		Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = heapType;

		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDescription.Width = bufferSize;
		resourceDescription.Height = 1u;
		resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
		resourceDescription.MipLevels = static_cast<UINT16>(1u);
		resourceDescription.SampleDesc.Count = 1u;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDescription.Flags = resourceFlags;

		return device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			initialState,
			nullptr,
			IID_PPV_ARGS(resource.GetAddressOf()));
	};

	HRESULT result = createBuffer(
		objectBufferSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		objectUploadResource_);

	if (FAILED(result) || objectUploadResource_ == nullptr) {
		return false;
	}

	result = createBuffer(
		visibilityBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		frustumVisibilityResource_);

	if (FAILED(result) || frustumVisibilityResource_ == nullptr) {
		return false;
	}

	result = createBuffer(
		visibilityBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		visibilityResource_);

	if (FAILED(result) || visibilityResource_ == nullptr) {
		return false;
	}

	result = createBuffer(
		drawArgumentsBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		drawArgumentsResource_);

	if (FAILED(result) || drawArgumentsResource_ == nullptr) {
		return false;
	}

	result = createBuffer(
		drawArgumentsBufferSize,
		D3D12_HEAP_TYPE_READBACK,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COPY_DEST,
		drawArgumentsReadbackResource_);

	if (FAILED(result) || drawArgumentsReadbackResource_ == nullptr) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC objectSrvDescription{};
	objectSrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	objectSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	objectSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	objectSrvDescription.Buffer.NumElements = kMaximumObjectCount;
	objectSrvDescription.Buffer.StructureByteStride = sizeof(EditorGpuCullingInput);
	device_->CreateShaderResourceView(
		objectUploadResource_.Get(),
		&objectSrvDescription,
		GetCpuDescriptorHandle(kObjectSrvDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC frustumVisibilitySrvDescription{};
	frustumVisibilitySrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	frustumVisibilitySrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	frustumVisibilitySrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	frustumVisibilitySrvDescription.Buffer.NumElements = kMaximumObjectCount;
	frustumVisibilitySrvDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateShaderResourceView(
		frustumVisibilityResource_.Get(),
		&frustumVisibilitySrvDescription,
		GetCpuDescriptorHandle(kFrustumVisibilitySrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC frustumVisibilityUavDescription{};
	frustumVisibilityUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	frustumVisibilityUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	frustumVisibilityUavDescription.Buffer.NumElements = kMaximumObjectCount;
	frustumVisibilityUavDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateUnorderedAccessView(
		frustumVisibilityResource_.Get(),
		nullptr,
		&frustumVisibilityUavDescription,
		GetCpuDescriptorHandle(kFrustumVisibilityUavDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC visibilitySrvDescription{};
	visibilitySrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	visibilitySrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	visibilitySrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	visibilitySrvDescription.Buffer.NumElements = kMaximumObjectCount;
	visibilitySrvDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateShaderResourceView(
		visibilityResource_.Get(),
		&visibilitySrvDescription,
		GetCpuDescriptorHandle(kVisibilitySrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC visibilityUavDescription{};
	visibilityUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	visibilityUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	visibilityUavDescription.Buffer.NumElements = kMaximumObjectCount;
	visibilityUavDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateUnorderedAccessView(
		visibilityResource_.Get(),
		nullptr,
		&visibilityUavDescription,
		GetCpuDescriptorHandle(kVisibilityUavDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC drawArgumentsSrvDescription{};
	drawArgumentsSrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	drawArgumentsSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	drawArgumentsSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	drawArgumentsSrvDescription.Buffer.NumElements = kMaximumObjectCount;
	drawArgumentsSrvDescription.Buffer.StructureByteStride = sizeof(DrawArguments);
	device_->CreateShaderResourceView(
		drawArgumentsResource_.Get(),
		&drawArgumentsSrvDescription,
		GetCpuDescriptorHandle(kDrawArgumentsSrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC drawArgumentsUavDescription{};
	drawArgumentsUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	drawArgumentsUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	drawArgumentsUavDescription.Buffer.NumElements = kMaximumObjectCount;
	drawArgumentsUavDescription.Buffer.StructureByteStride = sizeof(DrawArguments);
	device_->CreateUnorderedAccessView(
		drawArgumentsResource_.Get(),
		nullptr,
		&drawArgumentsUavDescription,
		GetCpuDescriptorHandle(kDrawArgumentsUavDescriptorIndex));

	objectSrvHandle_ = GetGpuDescriptorHandle(kObjectSrvDescriptorIndex);
	frustumVisibilitySrvHandle_ = GetGpuDescriptorHandle(kFrustumVisibilitySrvDescriptorIndex);
	frustumVisibilityUavHandle_ = GetGpuDescriptorHandle(kFrustumVisibilityUavDescriptorIndex);
	visibilitySrvHandle_ = GetGpuDescriptorHandle(kVisibilitySrvDescriptorIndex);
	visibilityUavHandle_ = GetGpuDescriptorHandle(kVisibilityUavDescriptorIndex);
	drawArgumentsUavHandle_ = GetGpuDescriptorHandle(kDrawArgumentsUavDescriptorIndex);
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorGpuCullingManager::GetCpuDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGpuCullingManager::GetGpuDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr += static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}
