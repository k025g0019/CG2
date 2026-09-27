#include "EditorGpuCullingManager.h"

#include "Source/Engine/Editor/EditorProfilerManager.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {
	constexpr uint32_t kSceneViewDescriptorStartIndex = 83u;
	// Temporalは160～197を使うため、その直後をGame View専用領域にする。
	constexpr uint32_t kGameViewDescriptorStartIndex = 198u;
	constexpr uint32_t kDescriptorsPerView = 7u;
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

	if (!CreateBuffers(EditorGpuCullingView::Scene) ||
		!CreateBuffers(EditorGpuCullingView::Game)) {
		Finalize();
		return false;
	}

	D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
	indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDescription{};
	commandSignatureDescription.ByteStride = sizeof(IndirectArguments);
	commandSignatureDescription.NumArgumentDescs = 1u;
	commandSignatureDescription.pArgumentDescs = &indirectArgument;
	if (FAILED(device_->CreateCommandSignature(
		&commandSignatureDescription,
		nullptr,
		IID_PPV_ARGS(drawCommandSignature_.ReleaseAndGetAddressOf())))) {
		Finalize();
		return false;
	}
	indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	if (FAILED(device_->CreateCommandSignature(
		&commandSignatureDescription,
		nullptr,
		IID_PPV_ARGS(drawIndexedCommandSignature_.ReleaseAndGetAddressOf())))) {
		Finalize();
		return false;
	}

	isInitialized_ = true;
	return true;
}

bool EditorGpuCullingManager::Execute(
	ID3D12GraphicsCommandList* commandList,
	EditorGpuCullingView view,
	const std::vector<EditorGpuCullingInput>& cullingInputs,
	D3D12_GPU_DESCRIPTOR_HANDLE depthPyramidSrvHandle,
	const float* viewProjectionMatrix,
	uint32_t depthPyramidWidth,
	uint32_t depthPyramidHeight,
	float viewportUvOffsetX,
	float viewportUvOffsetY,
	float viewportUvScaleX,
	float viewportUvScaleY) {

	ViewResources* resources = GetViewResources(view);
	if (!isInitialized_ || resources == nullptr || commandList == nullptr || depthPyramidSrvHandle.ptr == 0u ||
		viewProjectionMatrix == nullptr || depthPyramidWidth == 0u || depthPyramidHeight == 0u ||
		viewportUvScaleX <= 0.0f || viewportUvScaleY <= 0.0f) {
		return false;
	}

	const uint32_t objectCount = (std::min)(
		static_cast<uint32_t>(cullingInputs.size()),
		kMaximumObjectCount);

	if (objectCount == 0u) {
		resources->submittedObjectCount = 0u;
		resources->submittedObjectIndexByGameObjectId.clear();
		return true;
	}

	//================================================================
	// CPU で確定したワールド AABB を Upload Buffer へ書き込む
	//================================================================

	void* mappedObjectData = nullptr;
	D3D12_RANGE noReadRange{0u, 0u};
	HRESULT result = resources->objectUploadResource->Map(0u, &noReadRange, &mappedObjectData);

	if (FAILED(result) || mappedObjectData == nullptr) {
		return false;
	}

	std::memcpy(
		mappedObjectData,
		cullingInputs.data(),
		static_cast<size_t>(objectCount) * sizeof(EditorGpuCullingInput));
	resources->objectUploadResource->Unmap(0u, nullptr);

	resources->submittedGameObjectIds.resize(objectCount);
	resources->submittedObjectIndexByGameObjectId.clear();
	resources->submittedObjectIndexByGameObjectId.reserve(objectCount);

	for (uint32_t objectIndex = 0u; objectIndex < objectCount; objectIndex++) {
		resources->submittedGameObjectIds[objectIndex] = cullingInputs[objectIndex].gameObjectId;
		resources->submittedObjectIndexByGameObjectId[cullingInputs[objectIndex].gameObjectId] = objectIndex;
	}

	std::array<uint32_t, kComputeConstantCount> constants{};
	constants[0] = objectCount;
	constants[1] = depthPyramidWidth;
	constants[2] = depthPyramidHeight;
	float depthBias = 0.0015f;
	std::memcpy(&constants[3], &depthBias, sizeof(float));
	std::memcpy(&constants[4], viewProjectionMatrix, sizeof(float) * 16u);

	// Depth Pyramid はウィンドウ全体を持つため、Scene / Game View の局所 UV を全体 UV へ変換する。
	const std::array<float, 4u> viewportUvTransform = {
		viewportUvOffsetX,
		viewportUvOffsetY,
		viewportUvScaleX,
		viewportUvScaleY
	};
	std::memcpy(&constants[20], viewportUvTransform.data(), sizeof(float) * viewportUvTransform.size());

	commandList->SetComputeRootSignature(computeRootSignature_.Get());

	//================================================================
	// Pass 1: ワールド AABB を視錐台へ通し、画面外の物体を除外する
	//================================================================

	D3D12_RESOURCE_BARRIER frustumVisibilityBarrier{};
	frustumVisibilityBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	frustumVisibilityBarrier.Transition.pResource = resources->frustumVisibilityResource.Get();
	frustumVisibilityBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	frustumVisibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	frustumVisibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &frustumVisibilityBarrier);

	commandList->SetPipelineState(frustumCullingPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, resources->objectSrvHandle);
	commandList->SetComputeRootDescriptorTable(1u, depthPyramidSrvHandle);
	commandList->SetComputeRootDescriptorTable(2u, resources->visibilitySrvHandle);
	commandList->SetComputeRootDescriptorTable(3u, resources->frustumVisibilityUavHandle);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	RecordEditorProfilerDispatch();
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER frustumVisibilityUnorderedAccessBarrier{};
	frustumVisibilityUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	frustumVisibilityUnorderedAccessBarrier.UAV.pResource = resources->frustumVisibilityResource.Get();
	commandList->ResourceBarrier(1u, &frustumVisibilityUnorderedAccessBarrier);

	frustumVisibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	frustumVisibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &frustumVisibilityBarrier);

	//================================================================
	// Pass 2: 視錐台内の物体だけを Hi-Z 深度と比較する
	//================================================================

	D3D12_RESOURCE_BARRIER visibilityBarrier{};
	visibilityBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	visibilityBarrier.Transition.pResource = resources->visibilityResource.Get();
	visibilityBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	visibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	visibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &visibilityBarrier);

	commandList->SetPipelineState(occlusionCullingPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, resources->objectSrvHandle);
	commandList->SetComputeRootDescriptorTable(1u, depthPyramidSrvHandle);
	commandList->SetComputeRootDescriptorTable(2u, resources->frustumVisibilitySrvHandle);
	commandList->SetComputeRootDescriptorTable(3u, resources->visibilityUavHandle);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	RecordEditorProfilerDispatch();
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER visibilityUnorderedAccessBarrier{};
	visibilityUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	visibilityUnorderedAccessBarrier.UAV.pResource = resources->visibilityResource.Get();
	commandList->ResourceBarrier(1u, &visibilityUnorderedAccessBarrier);

	visibilityBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	visibilityBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1u, &visibilityBarrier);

	//================================================================
	// Pass 3: 可視状態を D3D12_DRAW_ARGUMENTS の頂点数へ変換する
	//================================================================

	D3D12_RESOURCE_BARRIER drawArgumentsBarrier{};
	drawArgumentsBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	drawArgumentsBarrier.Transition.pResource = resources->drawArgumentsResource.Get();
	drawArgumentsBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	drawArgumentsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PREDICATION;
	drawArgumentsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &drawArgumentsBarrier);

	commandList->SetPipelineState(buildIndirectArgsPipelineState_.Get());
	commandList->SetComputeRootDescriptorTable(0u, resources->objectSrvHandle);
	commandList->SetComputeRootDescriptorTable(1u, resources->visibilitySrvHandle);
	commandList->SetComputeRootDescriptorTable(2u, resources->visibilitySrvHandle);
	commandList->SetComputeRootDescriptorTable(3u, resources->drawArgumentsUavHandle);
	commandList->SetComputeRoot32BitConstants(4u, kComputeConstantCount, constants.data(), 0u);
	RecordEditorProfilerDispatch();
	commandList->Dispatch((objectCount + kThreadGroupSize - 1u) / kThreadGroupSize, 1u, 1u);

	D3D12_RESOURCE_BARRIER drawArgumentsUnorderedAccessBarrier{};
	drawArgumentsUnorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	drawArgumentsUnorderedAccessBarrier.UAV.pResource = resources->drawArgumentsResource.Get();
	commandList->ResourceBarrier(1u, &drawArgumentsUnorderedAccessBarrier);

	drawArgumentsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	drawArgumentsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PREDICATION;
	commandList->ResourceBarrier(1u, &drawArgumentsBarrier);

	resources->submittedObjectCount = objectCount;
	return true;
}

void EditorGpuCullingManager::ResolveReadback() {
	// GPU結果は次FrameのSetPredicationから直接参照するため、CPU Mapは不要。
}

void EditorGpuCullingManager::Finalize() {
	for (ViewResources& resources : viewResources_) {
		resources = {};
	}
	drawCommandSignature_.Reset();
	drawIndexedCommandSignature_.Reset();
	buildIndirectArgsPipelineState_.Reset();
	occlusionCullingPipelineState_.Reset();
	frustumCullingPipelineState_.Reset();
	computeRootSignature_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	isInitialized_ = false;
}

bool EditorGpuCullingManager::IsVisible(EditorGpuCullingView view, int32_t gameObjectId) const {
	(void)view;
	(void)gameObjectId;
	return true;
}

bool EditorGpuCullingManager::GetSubmittedObjectIndex(
	EditorGpuCullingView view,
	int32_t gameObjectId,
	uint32_t& objectIndex) const {
	const ViewResources* resources = GetViewResources(view);
	if (!isInitialized_ || resources == nullptr) {
		return false;
	}
	const auto iterator = resources->submittedObjectIndexByGameObjectId.find(gameObjectId);
	if (iterator == resources->submittedObjectIndexByGameObjectId.end()) {
		return false;
	}
	objectIndex = iterator->second;
	return true;
}

D3D12_GPU_VIRTUAL_ADDRESS EditorGpuCullingManager::GetVisibilityGpuAddress(
	EditorGpuCullingView view) const {
	const ViewResources* resources = GetViewResources(view);
	return resources != nullptr && resources->visibilityResource != nullptr
		? resources->visibilityResource->GetGPUVirtualAddress()
		: 0u;
}

bool EditorGpuCullingManager::BeginPredication(
	ID3D12GraphicsCommandList* commandList,
	EditorGpuCullingView view,
	int32_t gameObjectId) const {
	const ViewResources* resources = GetViewResources(view);
	if (!isInitialized_ || resources == nullptr || commandList == nullptr ||
		resources->drawArgumentsResource == nullptr) {
		return false;
	}

	const auto objectIndexIterator = resources->submittedObjectIndexByGameObjectId.find(gameObjectId);

	if (objectIndexIterator == resources->submittedObjectIndexByGameObjectId.end()) {
		return false;
	}

	const UINT64 predicateOffset =
		static_cast<UINT64>(objectIndexIterator->second) * sizeof(IndirectArguments);
	commandList->SetPredication(
		resources->drawArgumentsResource.Get(),
		predicateOffset,
		D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
	return true;
}

bool EditorGpuCullingManager::ExecuteIndirectDraw(
	ID3D12GraphicsCommandList* commandList,
	EditorGpuCullingView view,
	int32_t gameObjectId) const {
	const ViewResources* resources = GetViewResources(view);
	if (!isInitialized_ || resources == nullptr || commandList == nullptr ||
		drawCommandSignature_ == nullptr || resources->drawArgumentsResource == nullptr) {
		return false;
	}

	const auto objectIndexIterator = resources->submittedObjectIndexByGameObjectId.find(gameObjectId);
	if (objectIndexIterator == resources->submittedObjectIndexByGameObjectId.end()) {
		return false;
	}

	const UINT64 argumentOffset =
		static_cast<UINT64>(objectIndexIterator->second) * sizeof(IndirectArguments);
	commandList->ExecuteIndirect(
		drawCommandSignature_.Get(),
		1u,
		resources->drawArgumentsResource.Get(),
		argumentOffset,
		nullptr,
		0u);
	return true;
}

bool EditorGpuCullingManager::ExecuteIndirectDrawIndexed(
	ID3D12GraphicsCommandList* commandList,
	EditorGpuCullingView view,
	int32_t gameObjectId) const {
	const ViewResources* resources = GetViewResources(view);
	if (!isInitialized_ || resources == nullptr || commandList == nullptr ||
		drawIndexedCommandSignature_ == nullptr || resources->drawArgumentsResource == nullptr) {
		return false;
	}
	const auto objectIndexIterator = resources->submittedObjectIndexByGameObjectId.find(gameObjectId);
	if (objectIndexIterator == resources->submittedObjectIndexByGameObjectId.end()) {
		return false;
	}
	const UINT64 argumentOffset =
		static_cast<UINT64>(objectIndexIterator->second) * sizeof(IndirectArguments);
	commandList->ExecuteIndirect(
		drawIndexedCommandSignature_.Get(),
		1u,
		resources->drawArgumentsResource.Get(),
		argumentOffset,
		nullptr,
		0u);
	return true;
}

void EditorGpuCullingManager::EndPredication(ID3D12GraphicsCommandList* commandList) const {
	if (commandList != nullptr) {
		commandList->SetPredication(nullptr, 0u, D3D12_PREDICATION_OP_EQUAL_ZERO);
	}
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

bool EditorGpuCullingManager::CreateBuffers(EditorGpuCullingView view) {
	ViewResources* resources = GetViewResources(view);
	if (resources == nullptr) {
		return false;
	}

	const uint32_t descriptorStartIndex = view == EditorGpuCullingView::Scene
		? kSceneViewDescriptorStartIndex
		: kGameViewDescriptorStartIndex;
	const uint32_t objectSrvDescriptorIndex = descriptorStartIndex;
	const uint32_t frustumVisibilitySrvDescriptorIndex = descriptorStartIndex + 1u;
	const uint32_t frustumVisibilityUavDescriptorIndex = descriptorStartIndex + 2u;
	const uint32_t visibilitySrvDescriptorIndex = descriptorStartIndex + 3u;
	const uint32_t visibilityUavDescriptorIndex = descriptorStartIndex + 4u;
	const uint32_t drawArgumentsSrvDescriptorIndex = descriptorStartIndex + 5u;
	const uint32_t drawArgumentsUavDescriptorIndex = descriptorStartIndex + 6u;
	static_assert(kDescriptorsPerView == 7u);

	const UINT64 objectBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(EditorGpuCullingInput);
	const UINT64 visibilityBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(uint32_t);
	const UINT64 drawArgumentsBufferSize =
		static_cast<UINT64>(kMaximumObjectCount) * sizeof(IndirectArguments);

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
		resources->objectUploadResource);

	if (FAILED(result) || resources->objectUploadResource == nullptr) {
		return false;
	}

	result = createBuffer(
		visibilityBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		resources->frustumVisibilityResource);

	if (FAILED(result) || resources->frustumVisibilityResource == nullptr) {
		return false;
	}

	result = createBuffer(
		visibilityBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		resources->visibilityResource);

	if (FAILED(result) || resources->visibilityResource == nullptr) {
		return false;
	}

	result = createBuffer(
		drawArgumentsBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PREDICATION,
		resources->drawArgumentsResource);

	if (FAILED(result) || resources->drawArgumentsResource == nullptr) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC objectSrvDescription{};
	objectSrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	objectSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	objectSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	objectSrvDescription.Buffer.NumElements = kMaximumObjectCount;
	objectSrvDescription.Buffer.StructureByteStride = sizeof(EditorGpuCullingInput);
	device_->CreateShaderResourceView(
		resources->objectUploadResource.Get(),
		&objectSrvDescription,
		GetCpuDescriptorHandle(objectSrvDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC frustumVisibilitySrvDescription{};
	frustumVisibilitySrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	frustumVisibilitySrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	frustumVisibilitySrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	frustumVisibilitySrvDescription.Buffer.NumElements = kMaximumObjectCount;
	frustumVisibilitySrvDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateShaderResourceView(
		resources->frustumVisibilityResource.Get(),
		&frustumVisibilitySrvDescription,
		GetCpuDescriptorHandle(frustumVisibilitySrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC frustumVisibilityUavDescription{};
	frustumVisibilityUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	frustumVisibilityUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	frustumVisibilityUavDescription.Buffer.NumElements = kMaximumObjectCount;
	frustumVisibilityUavDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateUnorderedAccessView(
		resources->frustumVisibilityResource.Get(),
		nullptr,
		&frustumVisibilityUavDescription,
		GetCpuDescriptorHandle(frustumVisibilityUavDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC visibilitySrvDescription{};
	visibilitySrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	visibilitySrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	visibilitySrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	visibilitySrvDescription.Buffer.NumElements = kMaximumObjectCount;
	visibilitySrvDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateShaderResourceView(
		resources->visibilityResource.Get(),
		&visibilitySrvDescription,
		GetCpuDescriptorHandle(visibilitySrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC visibilityUavDescription{};
	visibilityUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	visibilityUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	visibilityUavDescription.Buffer.NumElements = kMaximumObjectCount;
	visibilityUavDescription.Buffer.StructureByteStride = sizeof(uint32_t);
	device_->CreateUnorderedAccessView(
		resources->visibilityResource.Get(),
		nullptr,
		&visibilityUavDescription,
		GetCpuDescriptorHandle(visibilityUavDescriptorIndex));

	D3D12_SHADER_RESOURCE_VIEW_DESC drawArgumentsSrvDescription{};
	drawArgumentsSrvDescription.Format = DXGI_FORMAT_UNKNOWN;
	drawArgumentsSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	drawArgumentsSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	drawArgumentsSrvDescription.Buffer.NumElements = kMaximumObjectCount;
	drawArgumentsSrvDescription.Buffer.StructureByteStride = sizeof(IndirectArguments);
	device_->CreateShaderResourceView(
		resources->drawArgumentsResource.Get(),
		&drawArgumentsSrvDescription,
		GetCpuDescriptorHandle(drawArgumentsSrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC drawArgumentsUavDescription{};
	drawArgumentsUavDescription.Format = DXGI_FORMAT_UNKNOWN;
	drawArgumentsUavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	drawArgumentsUavDescription.Buffer.NumElements = kMaximumObjectCount;
	drawArgumentsUavDescription.Buffer.StructureByteStride = sizeof(IndirectArguments);
	device_->CreateUnorderedAccessView(
		resources->drawArgumentsResource.Get(),
		nullptr,
		&drawArgumentsUavDescription,
		GetCpuDescriptorHandle(drawArgumentsUavDescriptorIndex));

	resources->objectSrvHandle = GetGpuDescriptorHandle(objectSrvDescriptorIndex);
	resources->frustumVisibilitySrvHandle = GetGpuDescriptorHandle(frustumVisibilitySrvDescriptorIndex);
	resources->frustumVisibilityUavHandle = GetGpuDescriptorHandle(frustumVisibilityUavDescriptorIndex);
	resources->visibilitySrvHandle = GetGpuDescriptorHandle(visibilitySrvDescriptorIndex);
	resources->visibilityUavHandle = GetGpuDescriptorHandle(visibilityUavDescriptorIndex);
	resources->drawArgumentsUavHandle = GetGpuDescriptorHandle(drawArgumentsUavDescriptorIndex);
	return true;
}

EditorGpuCullingManager::ViewResources* EditorGpuCullingManager::GetViewResources(
	EditorGpuCullingView view) {
	const size_t viewIndex = static_cast<size_t>(view);
	return viewIndex < viewResources_.size() ? &viewResources_[viewIndex] : nullptr;
}

const EditorGpuCullingManager::ViewResources* EditorGpuCullingManager::GetViewResources(
	EditorGpuCullingView view) const {
	const size_t viewIndex = static_cast<size_t>(view);
	return viewIndex < viewResources_.size() ? &viewResources_[viewIndex] : nullptr;
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
