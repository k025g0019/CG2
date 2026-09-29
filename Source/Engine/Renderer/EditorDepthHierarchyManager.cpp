#include "EditorDepthHierarchyManager.h"

#include "Source/Engine/Editor/EditorProfilerManager.h"

#include <algorithm>
#include <cstring>

//========================================
// 深度階層生成処理の構成
//========================================

// Depth Hierarchyは、Full ResolutionのDepthから段階的に縮小したMip列を作る。
// 1Texelが広い画面領域の代表Depthを持つため、大きなBounding Boxは粗いMipを
// 少数回Sampleするだけで「手前のDepthに隠れているか」を近似判定できる。
//
//   Scene Depth
//     -> Mip 0へ変換
//     -> 2x2領域を集約してMip 1, 2, ...を生成
//     -> GPU Occlusion CullingやSSR Ray Marchingが参照
//
// 同じDepthからView/World位置を復元し、隣接位置の差分からNormalも再構築する。
// GBuffer Normalを別に持たないPassでも画面空間Effectへ面方向を渡すためである。
namespace {
	constexpr uint32_t kDepthPyramidDescriptorStartIndex = 31u; // Level 0のSRVを置く固定Descriptor位置。
	constexpr uint32_t kDepthPyramidDescriptorStride = 2u;
	constexpr uint32_t kReconstructedNormalSrvDescriptorIndex = 55u;
	constexpr uint32_t kReconstructedNormalUavDescriptorIndex = 56u;
	constexpr uint32_t kComputeConstantCount = 20u;
	constexpr uint32_t kThreadGroupSize = 8u; // 8x8 Threadで1 Groupあたり64 Pixelを処理する。

	uint32_t GetDepthPyramidSrvDescriptorIndex(uint32_t levelIndex) {
		return kDepthPyramidDescriptorStartIndex + levelIndex * kDepthPyramidDescriptorStride;
	}

	uint32_t GetDepthPyramidUavDescriptorIndex(uint32_t levelIndex) {
		return GetDepthPyramidSrvDescriptorIndex(levelIndex) + 1u;
	}
}

//========================================
// 深度階層初期化処理
//========================================

bool EditorDepthHierarchyManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	IDxcBlob* depthPyramidShaderBlob,
	IDxcBlob* depthDownsampleShaderBlob,
	IDxcBlob* reconstructNormalShaderBlob,
	uint32_t renderWidth,
	uint32_t renderHeight) {

	//------------------------------
	// 初期化前提の検証
	//------------------------------

	if (device == nullptr ||
		srvDescriptorHeap == nullptr ||
		srvDescriptorSize == 0u ||
		depthPyramidShaderBlob == nullptr ||
		depthDownsampleShaderBlob == nullptr ||
		reconstructNormalShaderBlob == nullptr) {
		return false;
	}

	// DeviceはComPtrで保持してManagerの生存中に失効しないようにする。
	// Descriptor Heap本体の所有権はPlatform側にあり、ここでは非所有Pointerとして借りる。
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
	// 0 PixelのTextureはD3D12で作成できないため、最小化中等の無効Sizeを拒否する。
	if (!isInitialized_ || renderWidth == 0u || renderHeight == 0u) {
		return false;
	}

	if (renderWidth_ == renderWidth && renderHeight_ == renderHeight) {
		return true;
	}

	// 古いSizeのSRV/UAV Handleを残すと、解放済みResourceを後段が参照するため一度すべて無効化する。
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

	//------------------------------
	// 生成条件検証
	//------------------------------

	if (!isInitialized_ ||
		commandList == nullptr ||
		sceneDepthSrvHandle.ptr == 0u ||
		inverseViewProjectionMatrix == nullptr ||
		activeLevelCount_ == 0u) {
		return false;
	}

	//------------------------------
	// 深度ピラミッド生成
	//------------------------------

	commandList->SetComputeRootSignature(computeRootSignature_.Get());

	for (uint32_t levelIndex = 0u; levelIndex < activeLevelCount_; levelIndex++) {
		// 各Levelは独立Textureとして持ち、前LevelのSRVを現在LevelのUAVへ縮小する。
		ID3D12Resource* destinationResource = depthPyramidResources_[levelIndex].Get();

		D3D12_RESOURCE_BARRIER destinationBarrier{};
		destinationBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		destinationBarrier.Transition.pResource = destinationResource;
		destinationBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		destinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		destinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		commandList->ResourceBarrier(1u, &destinationBarrier);

		// Level 0だけはHardware DepthをLinearな階層用表現へ変換し、
		// Level 1以降は前Levelの2x2範囲を集約する別Shaderを使う。
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

		// Texture端の端数Pixelも処理するためGroup数を切り上げる。
		const uint32_t dispatchGroupX =
			(depthPyramidWidths_[levelIndex] + kThreadGroupSize - 1u) / kThreadGroupSize;
		const uint32_t dispatchGroupY =
			(depthPyramidHeights_[levelIndex] + kThreadGroupSize - 1u) / kThreadGroupSize;
		RecordEditorProfilerDispatch();
		commandList->Dispatch(dispatchGroupX, dispatchGroupY, 1u);

		// 次Levelが現在LevelをSRVとして読む前に、UAV書込完了を保証する。
		D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
		unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		unorderedAccessBarrier.UAV.pResource = destinationResource;
		commandList->ResourceBarrier(1u, &unorderedAccessBarrier);

		destinationBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		destinationBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1u, &destinationBarrier);
	}

	//------------------------------
	// 深度からワールド法線を再構築
	//------------------------------

	D3D12_RESOURCE_BARRIER normalBarrier{};
	normalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	normalBarrier.Transition.pResource = reconstructedNormalResource_.Get();
	normalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	normalBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	normalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	commandList->ResourceBarrier(1u, &normalBarrier);

	// 逆ViewProjectionはDepthとScreen UVからWorld位置を復元するために使う。
	// 隣接PixelのWorld位置差を外積し、面の向きを表すNormalを求める。
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
	RecordEditorProfilerDispatch();
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
	// Size依存Textureを先に解放してから、それを生成・利用するPSOとDevice参照を解放する。
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

	//------------------------------
	// Root Parameter定義
	//------------------------------

	// t0に入力Texture、u0に出力Texture、b0相当にSize/行列をRoot Constantsで渡す。
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

	// 3 ShaderはResource配置が同じなので、同一Root SignatureからPSOだけを作り分ける。
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
	//------------------------------
	// Level別Texture生成
	//------------------------------

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
		// 2 Channelには階層判定で必要なDepth範囲を保持する。
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

		// 1x1まで到達した時点で、それ以上縮小しても情報量が変わらない。
		if (levelWidth == 1u && levelHeight == 1u) {
			break;
		}

		// 奇数Sizeは切り上げ、最終行・列のDepthを階層から落とさない。
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
	// Normalの符号と小数精度を保つためUNORMではなく16bit Floatを使う。
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
	// Resource解放と同時にHandle/Sizeも0へ戻し、Resize失敗後に古い値を返さない。
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
