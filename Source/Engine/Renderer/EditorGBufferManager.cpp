#include "EditorGBufferManager.h"

#include <algorithm>

namespace {
	constexpr uint32_t kGBufferDescriptorStartIndex = 102u;
	constexpr D3D12_RESOURCE_STATES kGBufferReadState =
		static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	constexpr std::array<DXGI_FORMAT, EditorGBufferManager::kRenderTargetCount> kGBufferFormats = {
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
	};

	constexpr std::array<std::array<float, 4u>, EditorGBufferManager::kRenderTargetCount>
		kGBufferClearColors = {{
			{0.0f, 0.0f, 0.0f, 0.0f},
			{0.5f, 0.5f, 1.0f, 0.0f},
			{1.0f, 0.0f, 1.0f, 0.0f},
			{0.0f, 0.0f, 0.0f, 0.0f},
		}};
}

bool EditorGBufferManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	ID3D12RootSignature* objectRootSignature,
	IDxcBlob* vertexShaderBlob,
	IDxcBlob* pixelShaderBlob,
	const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
	UINT inputElementCount,
	uint32_t renderWidth,
	uint32_t renderHeight) {
	Finalize();

	if (device == nullptr ||
		srvDescriptorHeap == nullptr ||
		objectRootSignature == nullptr ||
		vertexShaderBlob == nullptr ||
		pixelShaderBlob == nullptr ||
		inputElementDescs == nullptr ||
		inputElementCount == 0u) {
		return false;
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;
	objectRootSignature_ = objectRootSignature;
	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = kRenderTargetCount;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	const HRESULT rtvHeapResult = device_->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(rtvDescriptorHeap_.ReleaseAndGetAddressOf()));

	if (FAILED(rtvHeapResult) || rtvDescriptorHeap_ == nullptr) {
		Finalize();
		return false;
	}

	if (!CreatePipelineStates(
		vertexShaderBlob,
		pixelShaderBlob,
		inputElementDescs,
		inputElementCount)) {
		Finalize();
		return false;
	}

	isInitialized_ = true;

	if (!CreateSizeDependentResources(renderWidth, renderHeight)) {
		Finalize();
		return false;
	}

	return true;
}

bool EditorGBufferManager::Resize(uint32_t renderWidth, uint32_t renderHeight) {
	if (!isInitialized_) {
		return false;
	}

	if (renderWidth_ == renderWidth && renderHeight_ == renderHeight) {
		return true;
	}

	ReleaseSizeDependentResources();
	return CreateSizeDependentResources(renderWidth, renderHeight);
}

bool EditorGBufferManager::Begin(
	ID3D12GraphicsCommandList* commandList,
	D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewHandle) {
	if (!IsReady() || commandList == nullptr || isRendering_) {
		return false;
	}

	std::array<D3D12_RESOURCE_BARRIER, kRenderTargetCount> barriers{};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kRenderTargetCount> renderTargetHandles{};

	for (uint32_t renderTargetIndex = 0u;
		renderTargetIndex < kRenderTargetCount;
		renderTargetIndex++) {
		D3D12_RESOURCE_BARRIER& barrier = barriers[renderTargetIndex];
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resources_[renderTargetIndex].Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = kGBufferReadState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		renderTargetHandles[renderTargetIndex] = GetRtvDescriptorHandle(renderTargetIndex);
	}

	commandList->ResourceBarrier(
		static_cast<UINT>(barriers.size()),
		barriers.data());

	for (uint32_t renderTargetIndex = 0u;
		renderTargetIndex < kRenderTargetCount;
		renderTargetIndex++) {
		commandList->ClearRenderTargetView(
			renderTargetHandles[renderTargetIndex],
			kGBufferClearColors[renderTargetIndex].data(),
			0u,
			nullptr);
	}

	commandList->OMSetRenderTargets(
		kRenderTargetCount,
		renderTargetHandles.data(),
		TRUE,
		&depthStencilViewHandle);
	commandList->SetGraphicsRootSignature(objectRootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	isRendering_ = true;
	return true;
}

void EditorGBufferManager::BindPipelineState(
	ID3D12GraphicsCommandList* commandList,
	bool isDoubleSided) const {
	if (!isRendering_ || commandList == nullptr) {
		return;
	}

	ID3D12PipelineState* targetPipelineState = isDoubleSided
		? doubleSidedPipelineState_.Get()
		: pipelineState_.Get();

	if (targetPipelineState != nullptr) {
		commandList->SetPipelineState(targetPipelineState);
	}
}

void EditorGBufferManager::End(ID3D12GraphicsCommandList* commandList) {
	if (!isRendering_ || commandList == nullptr) {
		return;
	}

	std::array<D3D12_RESOURCE_BARRIER, kRenderTargetCount> barriers{};

	for (uint32_t renderTargetIndex = 0u;
		renderTargetIndex < kRenderTargetCount;
		renderTargetIndex++) {
		D3D12_RESOURCE_BARRIER& barrier = barriers[renderTargetIndex];
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resources_[renderTargetIndex].Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = kGBufferReadState;
	}

	commandList->ResourceBarrier(
		static_cast<UINT>(barriers.size()),
		barriers.data());
	isRendering_ = false;
}

void EditorGBufferManager::Finalize() {
	ReleaseSizeDependentResources();
	doubleSidedPipelineState_.Reset();
	pipelineState_.Reset();
	objectRootSignature_.Reset();
	rtvDescriptorHeap_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	srvDescriptorSize_ = 0u;
	rtvDescriptorSize_ = 0u;
	isInitialized_ = false;
	isRendering_ = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetAlbedoSrvHandle() const {
	return srvHandles_[static_cast<size_t>(RenderTargetType::Albedo)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetNormalSrvHandle() const {
	return srvHandles_[static_cast<size_t>(RenderTargetType::Normal)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetMaterialSrvHandle() const {
	return srvHandles_[static_cast<size_t>(RenderTargetType::Material)];
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetEmissionSrvHandle() const {
	return srvHandles_[static_cast<size_t>(RenderTargetType::Emission)];
}

bool EditorGBufferManager::IsReady() const {
	if (!isInitialized_ || pipelineState_ == nullptr || doubleSidedPipelineState_ == nullptr) {
		return false;
	}

	for (const Microsoft::WRL::ComPtr<ID3D12Resource>& resource : resources_) {
		if (resource == nullptr) {
			return false;
		}
	}

	return true;
}

bool EditorGBufferManager::CreatePipelineStates(
	IDxcBlob* vertexShaderBlob,
	IDxcBlob* pixelShaderBlob,
	const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
	UINT inputElementCount) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
	pipelineStateDesc.pRootSignature = objectRootSignature_.Get();
	pipelineStateDesc.InputLayout.pInputElementDescs = inputElementDescs;
	pipelineStateDesc.InputLayout.NumElements = inputElementCount;
	pipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize(),
	};
	pipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize(),
	};

	for (uint32_t renderTargetIndex = 0u;
		renderTargetIndex < kRenderTargetCount;
		renderTargetIndex++) {
		pipelineStateDesc.BlendState.RenderTarget[renderTargetIndex].RenderTargetWriteMask =
			D3D12_COLOR_WRITE_ENABLE_ALL;
		pipelineStateDesc.RTVFormats[renderTargetIndex] = kGBufferFormats[renderTargetIndex];
	}

	pipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	pipelineStateDesc.RasterizerState.DepthClipEnable = TRUE;
	pipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
	pipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pipelineStateDesc.NumRenderTargets = kRenderTargetCount;
	pipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateDesc.SampleDesc.Count = 1u;
	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	HRESULT pipelineResult = device_->CreateGraphicsPipelineState(
		&pipelineStateDesc,
		IID_PPV_ARGS(pipelineState_.ReleaseAndGetAddressOf()));

	if (FAILED(pipelineResult) || pipelineState_ == nullptr) {
		return false;
	}

	pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pipelineResult = device_->CreateGraphicsPipelineState(
		&pipelineStateDesc,
		IID_PPV_ARGS(doubleSidedPipelineState_.ReleaseAndGetAddressOf()));
	return SUCCEEDED(pipelineResult) && doubleSidedPipelineState_ != nullptr;
}

bool EditorGBufferManager::CreateSizeDependentResources(
	uint32_t renderWidth,
	uint32_t renderHeight) {
	if (device_ == nullptr || rtvDescriptorHeap_ == nullptr || srvDescriptorHeap_ == nullptr) {
		return false;
	}

	renderWidth_ = (std::max)(renderWidth, 1u);
	renderHeight_ = (std::max)(renderHeight, 1u);

	for (uint32_t renderTargetIndex = 0u;
		renderTargetIndex < kRenderTargetCount;
		renderTargetIndex++) {
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = static_cast<UINT64>(renderWidth_);
		resourceDesc.Height = renderHeight_;
		resourceDesc.DepthOrArraySize = 1u;
		resourceDesc.MipLevels = 1u;
		resourceDesc.Format = kGBufferFormats[renderTargetIndex];
		resourceDesc.SampleDesc.Count = 1u;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format = kGBufferFormats[renderTargetIndex];
		std::copy(
			kGBufferClearColors[renderTargetIndex].begin(),
			kGBufferClearColors[renderTargetIndex].end(),
			clearValue.Color);

		const HRESULT resourceResult = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			kGBufferReadState,
			&clearValue,
			IID_PPV_ARGS(resources_[renderTargetIndex].ReleaseAndGetAddressOf()));

		if (FAILED(resourceResult) || resources_[renderTargetIndex] == nullptr) {
			ReleaseSizeDependentResources();
			return false;
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = kGBufferFormats[renderTargetIndex];
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		device_->CreateRenderTargetView(
			resources_[renderTargetIndex].Get(),
			&rtvDesc,
			GetRtvDescriptorHandle(renderTargetIndex));

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = kGBufferFormats[renderTargetIndex];
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1u;
		const uint32_t srvDescriptorIndex =
			kGBufferDescriptorStartIndex + renderTargetIndex;
		device_->CreateShaderResourceView(
			resources_[renderTargetIndex].Get(),
			&srvDesc,
			GetCpuSrvDescriptorHandle(srvDescriptorIndex));
		srvHandles_[renderTargetIndex] = GetGpuSrvDescriptorHandle(srvDescriptorIndex);
	}

	return true;
}

void EditorGBufferManager::ReleaseSizeDependentResources() {
	for (Microsoft::WRL::ComPtr<ID3D12Resource>& resource : resources_) {
		resource.Reset();
	}

	for (D3D12_GPU_DESCRIPTOR_HANDLE& srvHandle : srvHandles_) {
		srvHandle = {};
	}

	renderWidth_ = 0u;
	renderHeight_ = 0u;
	isRendering_ = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetCpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr +=
		static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetGpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr +=
		static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorGBufferManager::GetRtvDescriptorHandle(
	uint32_t renderTargetIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr +=
		static_cast<SIZE_T>(rtvDescriptorSize_) * static_cast<SIZE_T>(renderTargetIndex);
	return descriptorHandle;
}
