#include "EditorLightProbeManager.h"

#pragma warning(push, 0)
#include <algorithm>
#include <cmath>
#include <cstring>
#pragma warning(pop)

#include "../Core/Matrix.h"
#include "../Core/EditorSharedState.h"

using Microsoft::WRL::ComPtr;

namespace {
	// SRV Heap の 57-62 番を Light Probe 用に予約している。
	// 0-56 は既存の描画機能、123 以降は ImGui / Temporal / 動的テクスチャ。
	constexpr uint32_t kProbeShSrvDescriptorIndex = 57u;
	constexpr uint32_t kProbeVisibilitySrvDescriptorIndex = 58u;
	constexpr uint32_t kProbeShUavDescriptorIndex = 59u;
	constexpr uint32_t kProbeVisibilityUavDescriptorIndex = 60u;
	constexpr uint32_t kCaptureRadianceSrvDescriptorIndex = 61u;
	constexpr uint32_t kCaptureDistanceSrvDescriptorIndex = 62u;

	constexpr uint32_t kBakeConstantCount = 12u;
	constexpr DXGI_FORMAT kCaptureRadianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	constexpr DXGI_FORMAT kCaptureDistanceFormat = DXGI_FORMAT_R32_FLOAT;
	constexpr DXGI_FORMAT kCaptureDepthFormat = DXGI_FORMAT_D32_FLOAT;
	constexpr DXGI_FORMAT kProbeVisibilityFormat = DXGI_FORMAT_R16G16_FLOAT;

	Vector3 GetProbeCubeFaceForward(uint32_t faceIndex) {
		switch (faceIndex) {
			case 0u: return {1.0f, 0.0f, 0.0f};
			case 1u: return {-1.0f, 0.0f, 0.0f};
			case 2u: return {0.0f, 1.0f, 0.0f};
			case 3u: return {0.0f, -1.0f, 0.0f};
			case 4u: return {0.0f, 0.0f, 1.0f};
			default: return {0.0f, 0.0f, -1.0f};
		}
	}

	Vector3 GetProbeCubeFaceUp(uint32_t faceIndex) {
		// +Y/-Y面はforwardと同じ軸のupを使えないため、そこだけ別軸のupにする。
		if (faceIndex == 2u) {
			return {0.0f, 0.0f, -1.0f};
		}

		if (faceIndex == 3u) {
			return {0.0f, 0.0f, 1.0f};
		}

		return {0.0f, 1.0f, 0.0f};
	}

	Vector3 SubtractVector(const Vector3& left, const Vector3& right) {
		return {left.x - right.x, left.y - right.y, left.z - right.z};
	}

	Vector3 CrossVector(const Vector3& left, const Vector3& right) {
		return {
			left.y * right.z - left.z * right.y,
			left.z * right.x - left.x * right.z,
			left.x * right.y - left.y * right.x};
	}

	float DotVector(const Vector3& left, const Vector3& right) {
		return left.x * right.x + left.y * right.y + left.z * right.z;
	}

	float LengthVector(const Vector3& value) {
		return std::sqrt(DotVector(value, value));
	}

	Vector3 NormalizeVector(const Vector3& value) {
		const float length = LengthVector(value);

		if (length <= 0.0001f) {
			return {0.0f, 0.0f, 1.0f};
		}

		return {value.x / length, value.y / length, value.z / length};
	}

	// EditorRenderManager の MakeLookAtMatrix と同じ左手系の View 行列。
	Matrix4x4 MakeProbeLookAtMatrix(const Vector3& eye, const Vector3& forward, const Vector3& up) {
		const Vector3 zAxis = NormalizeVector(forward);
		Vector3 xAxis = NormalizeVector(CrossVector(up, zAxis));

		if (LengthVector(xAxis) <= 0.0001f) {
			xAxis = NormalizeVector(CrossVector(Vector3{1.0f, 0.0f, 0.0f}, zAxis));
		}

		const Vector3 yAxis = CrossVector(zAxis, xAxis);

		Matrix4x4 viewMatrix{};
		viewMatrix.matrix[0][0] = xAxis.x;
		viewMatrix.matrix[0][1] = yAxis.x;
		viewMatrix.matrix[0][2] = zAxis.x;
		viewMatrix.matrix[0][3] = 0.0f;
		viewMatrix.matrix[1][0] = xAxis.y;
		viewMatrix.matrix[1][1] = yAxis.y;
		viewMatrix.matrix[1][2] = zAxis.y;
		viewMatrix.matrix[1][3] = 0.0f;
		viewMatrix.matrix[2][0] = xAxis.z;
		viewMatrix.matrix[2][1] = yAxis.z;
		viewMatrix.matrix[2][2] = zAxis.z;
		viewMatrix.matrix[2][3] = 0.0f;
		viewMatrix.matrix[3][0] = -DotVector(xAxis, eye);
		viewMatrix.matrix[3][1] = -DotVector(yAxis, eye);
		viewMatrix.matrix[3][2] = -DotVector(zAxis, eye);
		viewMatrix.matrix[3][3] = 1.0f;
		return viewMatrix;
	}
}

bool EditorLightProbeManager::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT srvDescriptorSize,
	ID3D12RootSignature* objectRootSignature,
	IDxcBlob* captureVertexShaderBlob,
	IDxcBlob* capturePixelShaderBlob,
	IDxcBlob* shProjectionComputeShaderBlob,
	IDxcBlob* visibilityComputeShaderBlob,
	const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
	UINT inputElementCount) {
	if (device == nullptr || srvDescriptorHeap == nullptr || objectRootSignature == nullptr) {
		return false;
	}

	device_ = device;
	srvDescriptorHeap_ = srvDescriptorHeap;
	srvDescriptorSize_ = srvDescriptorSize;
	objectRootSignature_ = objectRootSignature;
	rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	dsvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	if (!CreateComputeRootSignature()) {
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC clearHeapDesc{};
	clearHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	clearHeapDesc.NumDescriptors = 2u;
	clearHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	if (FAILED(device_->CreateDescriptorHeap(
		&clearHeapDesc,
		IID_PPV_ARGS(clearUavDescriptorHeap_.GetAddressOf())))) {
		return false;
	}

	if (!CreatePipelineStates(
		captureVertexShaderBlob,
		capturePixelShaderBlob,
		shProjectionComputeShaderBlob,
		visibilityComputeShaderBlob,
		inputElementDescs,
		inputElementCount)) {
		return false;
	}

	if (!CreateCaptureResources()) {
		return false;
	}

	// GIが無効でも Object3d は t20/t21 を束縛するため、
	// 常に有効なDescriptorが在るよう最小構成を先に作っておく。
	if (!CreateProbeResources(1u)) {
		return false;
	}

	isInitialized_ = true;
	return true;
}

bool EditorLightProbeManager::CreateComputeRootSignature() {
	// b0 = Bake定数、b1 = DirectionalLight、t0/t1 = キャプチャ、u0/u1 = Probe出力。
	D3D12_DESCRIPTOR_RANGE captureRange[1] = {};
	captureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	captureRange[0].BaseShaderRegister = 0u;
	captureRange[0].NumDescriptors = 2u;
	captureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE probeOutputRange[1] = {};
	probeOutputRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	probeOutputRange[0].BaseShaderRegister = 0u;
	probeOutputRange[0].NumDescriptors = 2u;
	probeOutputRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[0].Constants.ShaderRegister = 0u;
	rootParameters[0].Constants.RegisterSpace = 0u;
	rootParameters[0].Constants.Num32BitValues = kBakeConstantCount;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[1].Descriptor.ShaderRegister = 1u;
	rootParameters[1].Descriptor.RegisterSpace = 0u;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = captureRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(captureRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[3].DescriptorTable.pDescriptorRanges = probeOutputRange;
	rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(probeOutputRange);

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ComPtr<ID3DBlob> signatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr) || signatureBlob == nullptr) {
		return false;
	}

	hr = device_->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(computeRootSignature_.GetAddressOf()));
	return SUCCEEDED(hr) && computeRootSignature_ != nullptr;
}

bool EditorLightProbeManager::CreatePipelineStates(
	IDxcBlob* captureVertexShaderBlob,
	IDxcBlob* capturePixelShaderBlob,
	IDxcBlob* shProjectionComputeShaderBlob,
	IDxcBlob* visibilityComputeShaderBlob,
	const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
	UINT inputElementCount) {
	if (captureVertexShaderBlob == nullptr ||
		capturePixelShaderBlob == nullptr ||
		shProjectionComputeShaderBlob == nullptr ||
		visibilityComputeShaderBlob == nullptr) {
		return false;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC captureDesc{};
	captureDesc.pRootSignature = objectRootSignature_.Get();
	captureDesc.VS = {
		captureVertexShaderBlob->GetBufferPointer(),
		captureVertexShaderBlob->GetBufferSize()};
	captureDesc.PS = {
		capturePixelShaderBlob->GetBufferPointer(),
		capturePixelShaderBlob->GetBufferSize()};
	captureDesc.InputLayout.pInputElementDescs = inputElementDescs;
	captureDesc.InputLayout.NumElements = inputElementCount;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	captureDesc.BlendState = blendDesc;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.DepthClipEnable = TRUE;
	captureDesc.RasterizerState = rasterizerDesc;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	captureDesc.DepthStencilState = depthStencilDesc;
	captureDesc.DSVFormat = kCaptureDepthFormat;

	captureDesc.NumRenderTargets = 2u;
	captureDesc.RTVFormats[0] = kCaptureRadianceFormat;
	captureDesc.RTVFormats[1] = kCaptureDistanceFormat;
	captureDesc.SampleDesc.Count = 1u;
	captureDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	captureDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	HRESULT hr = device_->CreateGraphicsPipelineState(
		&captureDesc,
		IID_PPV_ARGS(capturePipelineState_.GetAddressOf()));

	if (FAILED(hr) || capturePipelineState_ == nullptr) {
		return false;
	}

	captureDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	hr = device_->CreateGraphicsPipelineState(
		&captureDesc,
		IID_PPV_ARGS(captureDoubleSidedPipelineState_.GetAddressOf()));

	if (FAILED(hr) || captureDoubleSidedPipelineState_ == nullptr) {
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
	computeDesc.pRootSignature = computeRootSignature_.Get();
	computeDesc.CS = {
		shProjectionComputeShaderBlob->GetBufferPointer(),
		shProjectionComputeShaderBlob->GetBufferSize()};
	hr = device_->CreateComputePipelineState(
		&computeDesc,
		IID_PPV_ARGS(shProjectionPipelineState_.GetAddressOf()));

	if (FAILED(hr) || shProjectionPipelineState_ == nullptr) {
		return false;
	}

	computeDesc.CS = {
		visibilityComputeShaderBlob->GetBufferPointer(),
		visibilityComputeShaderBlob->GetBufferSize()};
	hr = device_->CreateComputePipelineState(
		&computeDesc,
		IID_PPV_ARGS(visibilityPipelineState_.GetAddressOf()));
	return SUCCEEDED(hr) && visibilityPipelineState_ != nullptr;
}

bool EditorLightProbeManager::CreateCaptureResources() {
	const uint32_t captureWidth = kCaptureFaceSize * kCubeFaceCount;
	const uint32_t captureHeight = kCaptureFaceSize * kBakeBatchSize;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = captureWidth;
	resourceDesc.Height = captureHeight;
	resourceDesc.DepthOrArraySize = 1u;
	resourceDesc.MipLevels = 1u;
	resourceDesc.SampleDesc.Count = 1u;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE radianceClearValue{};
	radianceClearValue.Format = kCaptureRadianceFormat;
	resourceDesc.Format = kCaptureRadianceFormat;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&radianceClearValue,
		IID_PPV_ARGS(captureRadianceResource_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	// 何にも当たらなかった方向を「遠方」として判別するため、遠クリップで塗る。
	D3D12_CLEAR_VALUE distanceClearValue{};
	distanceClearValue.Format = kCaptureDistanceFormat;
	distanceClearValue.Color[0] = 1.0e6f;
	resourceDesc.Format = kCaptureDistanceFormat;
	hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&distanceClearValue,
		IID_PPV_ARGS(captureDistanceResource_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = kCaptureDepthFormat;
	depthClearValue.DepthStencil.Depth = 1.0f;
	resourceDesc.Format = kCaptureDepthFormat;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(captureDepthResource_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 2u;
	hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(captureRtvHeap_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NumDescriptors = 1u;
	hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(captureDsvHeap_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = captureRtvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = kCaptureRadianceFormat;
	device_->CreateRenderTargetView(captureRadianceResource_.Get(), &rtvDesc, rtvHandle);
	rtvHandle.ptr += rtvDescriptorSize_;
	rtvDesc.Format = kCaptureDistanceFormat;
	device_->CreateRenderTargetView(captureDistanceResource_.Get(), &rtvDesc, rtvHandle);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = kCaptureDepthFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(
		captureDepthResource_.Get(),
		&dsvDesc,
		captureDsvHeap_->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1u;
	srvDesc.Format = kCaptureRadianceFormat;
	device_->CreateShaderResourceView(
		captureRadianceResource_.Get(),
		&srvDesc,
		GetCpuSrvDescriptorHandle(kCaptureRadianceSrvDescriptorIndex));
	srvDesc.Format = kCaptureDistanceFormat;
	device_->CreateShaderResourceView(
		captureDistanceResource_.Get(),
		&srvDesc,
		GetCpuSrvDescriptorHandle(kCaptureDistanceSrvDescriptorIndex));
	return true;
}

bool EditorLightProbeManager::CreateProbeResources(uint32_t probeCount) {
	ReleaseProbeResources();

	if (probeCount == 0u) {
		return false;
	}

	const uint32_t coefficientCount = probeCount * kShCoefficientCount;
	const uint32_t shStride = static_cast<uint32_t>(sizeof(float) * 4u);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = static_cast<UINT64>(coefficientCount) * shStride;
	bufferDesc.Height = 1u;
	bufferDesc.DepthOrArraySize = 1u;
	bufferDesc.MipLevels = 1u;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1u;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(probeShResource_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	// 八面体タイルを正方形に近い形へ並べ、アトラスの無駄を減らす。
	visibilityTilesPerRow_ = static_cast<uint32_t>(
		std::ceil(std::sqrt(static_cast<float>(probeCount))));
	visibilityTilesPerRow_ = (std::max)(visibilityTilesPerRow_, 1u);
	const uint32_t tileRowCount =
		(probeCount + visibilityTilesPerRow_ - 1u) / visibilityTilesPerRow_;
	visibilityAtlasWidth_ = visibilityTilesPerRow_ * kVisibilityTileSize;
	visibilityAtlasHeight_ = tileRowCount * kVisibilityTileSize;

	D3D12_RESOURCE_DESC visibilityDesc{};
	visibilityDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	visibilityDesc.Width = visibilityAtlasWidth_;
	visibilityDesc.Height = visibilityAtlasHeight_;
	visibilityDesc.DepthOrArraySize = 1u;
	visibilityDesc.MipLevels = 1u;
	visibilityDesc.Format = kProbeVisibilityFormat;
	visibilityDesc.SampleDesc.Count = 1u;
	visibilityDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&visibilityDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(probeVisibilityResource_.GetAddressOf()));

	if (FAILED(hr)) {
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC shSrvDesc{};
	shSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	shSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	shSrvDesc.Buffer.NumElements = coefficientCount;
	shSrvDesc.Buffer.StructureByteStride = shStride;
	device_->CreateShaderResourceView(
		probeShResource_.Get(),
		&shSrvDesc,
		GetCpuSrvDescriptorHandle(kProbeShSrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC shUavDesc{};
	shUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	shUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	shUavDesc.Buffer.NumElements = coefficientCount;
	shUavDesc.Buffer.StructureByteStride = shStride;
	device_->CreateUnorderedAccessView(
		probeShResource_.Get(),
		nullptr,
		&shUavDesc,
		GetCpuSrvDescriptorHandle(kProbeShUavDescriptorIndex));

	// クリア用に Shader 非可視ヒープへも同じ UAV を作る。
	D3D12_CPU_DESCRIPTOR_HANDLE clearHandle =
		clearUavDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	device_->CreateUnorderedAccessView(probeShResource_.Get(), nullptr, &shUavDesc, clearHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC visibilitySrvDesc{};
	visibilitySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	visibilitySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	visibilitySrvDesc.Format = kProbeVisibilityFormat;
	visibilitySrvDesc.Texture2D.MipLevels = 1u;
	device_->CreateShaderResourceView(
		probeVisibilityResource_.Get(),
		&visibilitySrvDesc,
		GetCpuSrvDescriptorHandle(kProbeVisibilitySrvDescriptorIndex));

	D3D12_UNORDERED_ACCESS_VIEW_DESC visibilityUavDesc{};
	visibilityUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	visibilityUavDesc.Format = kProbeVisibilityFormat;
	device_->CreateUnorderedAccessView(
		probeVisibilityResource_.Get(),
		nullptr,
		&visibilityUavDesc,
		GetCpuSrvDescriptorHandle(kProbeVisibilityUavDescriptorIndex));

	clearHandle.ptr += srvDescriptorSize_;
	device_->CreateUnorderedAccessView(
		probeVisibilityResource_.Get(),
		nullptr,
		&visibilityUavDesc,
		clearHandle);

	probeCount_ = probeCount;
	nextBakeProbeIndex_ = 0u;
	needsFullRebake_ = true;
	// 作りたてはゴミ値なので、描画で読まれる前に必ず0クリアする。
	probeResourcesNeedClear_ = true;
	probeResourceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	return true;
}

void EditorLightProbeManager::PrepareFrame(ID3D12GraphicsCommandList* commandList) {
	if (commandList == nullptr || !HasResources()) {
		return;
	}

	const D3D12_RESOURCE_STATES readState = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	if (probeResourcesNeedClear_) {
		// ClearUnorderedAccessView は Shader 可視ヒープが束縛されている必要がある。
		if (srvDescriptorHeap_ != nullptr) {
			ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap_};
			commandList->SetDescriptorHeaps(1u, descriptorHeaps);
		}

		// クリアは UAV 状態でしか行えない。
		TransitionProbeResources(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		const UINT zeroUint[4] = {0u, 0u, 0u, 0u};
		D3D12_CPU_DESCRIPTOR_HANDLE clearHandle =
			clearUavDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
		// StructuredBuffer は format が UNKNOWN なので Uint 版でしかクリアできない。
		// 0 のビットパターンは 0.0f と同じなので、これで float も 0 になる。
		commandList->ClearUnorderedAccessViewUint(
			GetGpuSrvDescriptorHandle(kProbeShUavDescriptorIndex),
			clearHandle,
			probeShResource_.Get(),
			zeroUint,
			0u,
			nullptr);

		clearHandle.ptr += srvDescriptorSize_;
		const float zeroFloat[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		// 可視性を0にしておくと Chebyshev の重みが0になり、
		// 未Bakeのプローブは自動的に「GI無し」として扱われる。
		commandList->ClearUnorderedAccessViewFloat(
			GetGpuSrvDescriptorHandle(kProbeVisibilityUavDescriptorIndex),
			clearHandle,
			probeVisibilityResource_.Get(),
			zeroFloat,
			0u,
			nullptr);

		probeResourcesNeedClear_ = false;
	}

	// 描画パスが SRV として読むので、読み取り状態へ戻しておく。
	TransitionProbeResources(commandList, readState);
}

void EditorLightProbeManager::TransitionProbeResources(
	ID3D12GraphicsCommandList* commandList,
	D3D12_RESOURCE_STATES afterState) {
	if (commandList == nullptr || !HasResources() || probeResourceState_ == afterState) {
		return;
	}

	std::array<D3D12_RESOURCE_BARRIER, 2u> barriers{};
	ID3D12Resource* resources[2] = {
		probeShResource_.Get(),
		probeVisibilityResource_.Get()};

	for (uint32_t barrierIndex = 0u; barrierIndex < barriers.size(); barrierIndex++) {
		barriers[barrierIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[barrierIndex].Transition.pResource = resources[barrierIndex];
		barriers[barrierIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[barrierIndex].Transition.StateBefore = probeResourceState_;
		barriers[barrierIndex].Transition.StateAfter = afterState;
	}

	commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	probeResourceState_ = afterState;
}

void EditorLightProbeManager::ReleaseProbeResources() {
	probeShResource_.Reset();
	probeVisibilityResource_.Reset();
	probeCount_ = 0u;
	visibilityTilesPerRow_ = 0u;
	visibilityAtlasWidth_ = 0u;
	visibilityAtlasHeight_ = 0u;
	nextBakeProbeIndex_ = 0u;
}

void EditorLightProbeManager::UpdateGrid(const GridSettings& settings) {
	if (device_ == nullptr) {
		return;
	}

	// GI無効時もDescriptorは生かしたままにする。実体を最小へ落とすだけ。
	const auto fallbackToDummyProbe = [this]() {
		if (probeCount_ != 1u) {
			CreateProbeResources(1u);
		}
	};

	const bool isGridShapeChanged =
		settings.countX != settings_.countX ||
		settings.countY != settings_.countY ||
		settings.countZ != settings_.countZ;
	const bool isPlacementChanged =
		std::fabs(settings.origin.x - settings_.origin.x) > 0.0001f ||
		std::fabs(settings.origin.y - settings_.origin.y) > 0.0001f ||
		std::fabs(settings.origin.z - settings_.origin.z) > 0.0001f ||
		std::fabs(settings.spacing.x - settings_.spacing.x) > 0.0001f ||
		std::fabs(settings.spacing.y - settings_.spacing.y) > 0.0001f ||
		std::fabs(settings.spacing.z - settings_.spacing.z) > 0.0001f;

	settings_ = settings;

	if (!settings.isEnabled ||
		settings.countX <= 0 ||
		settings.countY <= 0 ||
		settings.countZ <= 0) {
		fallbackToDummyProbe();
		return;
	}

	const uint64_t requestedProbeCount =
		static_cast<uint64_t>(settings.countX) *
		static_cast<uint64_t>(settings.countY) *
		static_cast<uint64_t>(settings.countZ);

	if (requestedProbeCount == 0u || requestedProbeCount > kMaxProbeCount) {
		fallbackToDummyProbe();
		return;
	}

	if (isGridShapeChanged || probeShResource_ == nullptr) {
		CreateProbeResources(static_cast<uint32_t>(requestedProbeCount));
		return;
	}

	if (isPlacementChanged) {
		// 位置だけ変わった場合はリソースを保ったまま焼き直す。
		nextBakeProbeIndex_ = 0u;
		needsFullRebake_ = true;
	}
}

bool EditorLightProbeManager::PrepareBakeBatch(
	uint32_t& outBaseProbeIndex,
	uint32_t& outProbeCount) {
	outBaseProbeIndex = 0u;
	outProbeCount = 0u;

	if (!IsReady()) {
		return false;
	}

	if (nextBakeProbeIndex_ >= probeCount_) {
		// 一巡したら先頭へ戻り、動く光にも追従し続ける。
		nextBakeProbeIndex_ = 0u;
		needsFullRebake_ = false;
	}

	outBaseProbeIndex = nextBakeProbeIndex_;
	outProbeCount = (std::min)(kBakeBatchSize, probeCount_ - nextBakeProbeIndex_);
	nextBakeProbeIndex_ += outProbeCount;
	return outProbeCount > 0u;
}

void EditorLightProbeManager::BeginCapture(ID3D12GraphicsCommandList* commandList) {
	if (commandList == nullptr || !IsReady()) {
		return;
	}

	std::array<D3D12_RESOURCE_BARRIER, 2u> barriers{};
	ID3D12Resource* resources[2] = {
		captureRadianceResource_.Get(),
		captureDistanceResource_.Get()};

	for (uint32_t barrierIndex = 0u; barrierIndex < barriers.size(); barrierIndex++) {
		barriers[barrierIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[barrierIndex].Transition.pResource = resources[barrierIndex];
		barriers[barrierIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[barrierIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barriers[barrierIndex].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	isCapturing_ = true;
}

Matrix4x4 EditorLightProbeManager::BeginCaptureFace(
	ID3D12GraphicsCommandList* commandList,
	uint32_t batchSlot,
	uint32_t faceIndex,
	uint32_t probeIndex) const {
	const Vector3 probePosition = GetProbeWorldPosition(probeIndex);
	const Matrix4x4 viewMatrix = MakeProbeLookAtMatrix(
		probePosition,
		GetProbeCubeFaceForward(faceIndex),
		GetProbeCubeFaceUp(faceIndex));
	// 90度FOVの正方形Frustumを6面並べればキューブ全体を隙間なく覆える。
	const Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		1.5707963f,
		1.0f,
		0.05f,
		(std::max)(settings_.captureFarDistance, 1.0f));
	const Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

	if (commandList == nullptr) {
		return viewProjectionMatrix;
	}

	const float faceSize = static_cast<float>(kCaptureFaceSize);
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = static_cast<float>(faceIndex) * faceSize;
	viewport.TopLeftY = static_cast<float>(batchSlot) * faceSize;
	viewport.Width = faceSize;
	viewport.Height = faceSize;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	D3D12_RECT scissorRect{
		static_cast<LONG>(viewport.TopLeftX),
		static_cast<LONG>(viewport.TopLeftY),
		static_cast<LONG>(viewport.TopLeftX + faceSize),
		static_cast<LONG>(viewport.TopLeftY + faceSize)};

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {
		captureRtvHeap_->GetCPUDescriptorHandleForHeapStart(),
		captureRtvHeap_->GetCPUDescriptorHandleForHeapStart()};
	rtvHandles[1].ptr += rtvDescriptorSize_;
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		captureDsvHeap_->GetCPUDescriptorHandleForHeapStart();

	commandList->RSSetViewports(1u, &viewport);
	commandList->RSSetScissorRects(1u, &scissorRect);
	commandList->OMSetRenderTargets(2u, rtvHandles, FALSE, &dsvHandle);

	const float radianceClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	// 距離は「何にも当たらなかった」を示す遠方値で初期化する。
	const float distanceClearColor[4] = {1.0e6f, 0.0f, 0.0f, 0.0f};
	D3D12_RECT clearRects[1] = {scissorRect};
	commandList->ClearRenderTargetView(rtvHandles[0], radianceClearColor, 1u, clearRects);
	commandList->ClearRenderTargetView(rtvHandles[1], distanceClearColor, 1u, clearRects);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 1u, clearRects);
	return viewProjectionMatrix;
}

void EditorLightProbeManager::BindCapturePipelineState(
	ID3D12GraphicsCommandList* commandList,
	bool isDoubleSided) const {
	if (commandList == nullptr) {
		return;
	}

	commandList->SetPipelineState(isDoubleSided
		? captureDoubleSidedPipelineState_.Get()
		: capturePipelineState_.Get());
}

void EditorLightProbeManager::EndCapture(ID3D12GraphicsCommandList* commandList) {
	if (commandList == nullptr || !isCapturing_) {
		return;
	}

	std::array<D3D12_RESOURCE_BARRIER, 2u> barriers{};
	ID3D12Resource* resources[2] = {
		captureRadianceResource_.Get(),
		captureDistanceResource_.Get()};

	for (uint32_t barrierIndex = 0u; barrierIndex < barriers.size(); barrierIndex++) {
		barriers[barrierIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[barrierIndex].Transition.pResource = resources[barrierIndex];
		barriers[barrierIndex].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barriers[barrierIndex].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barriers[barrierIndex].Transition.StateAfter =
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}

	commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	isCapturing_ = false;
}

void EditorLightProbeManager::DispatchBake(
	ID3D12GraphicsCommandList* commandList,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	D3D12_GPU_VIRTUAL_ADDRESS directionalLightConstantBufferAddress,
	uint32_t baseProbeIndex,
	uint32_t probeCount) {
	if (commandList == nullptr || !IsReady() || probeCount == 0u) {
		return;
	}

	int32_t bakeConstants[kBakeConstantCount] = {};
	bakeConstants[0] = static_cast<int32_t>(kCaptureFaceSize);
	bakeConstants[1] = static_cast<int32_t>(baseProbeIndex);
	bakeConstants[2] = static_cast<int32_t>(probeCount);
	const float hysteresis = needsFullRebake_
		? 0.0f
		: (std::clamp)(settings_.hysteresis, 0.0f, 0.99f);
	std::memcpy(&bakeConstants[3], &hysteresis, sizeof(float));
	const float farDistance = (std::max)(settings_.captureFarDistance, 1.0f);
	std::memcpy(&bakeConstants[4], &farDistance, sizeof(float));
	bakeConstants[5] = settings_.captureSky ? 1 : 0;
	bakeConstants[6] = static_cast<int32_t>(visibilityTilesPerRow_);
	bakeConstants[7] = static_cast<int32_t>(kVisibilityTileSize);

	if (srvDescriptorHeap != nullptr) {
		ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
		commandList->SetDescriptorHeaps(1u, descriptorHeaps);
	}

	// Compute が書き込む間だけ UAV 状態にする。
	TransitionProbeResources(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commandList->SetComputeRootSignature(computeRootSignature_.Get());
	commandList->SetComputeRoot32BitConstants(0u, kBakeConstantCount, bakeConstants, 0u);

	if (directionalLightConstantBufferAddress != 0u) {
		commandList->SetComputeRootConstantBufferView(1u, directionalLightConstantBufferAddress);
	}

	commandList->SetComputeRootDescriptorTable(
		2u,
		GetGpuSrvDescriptorHandle(kCaptureRadianceSrvDescriptorIndex));
	commandList->SetComputeRootDescriptorTable(
		3u,
		GetGpuSrvDescriptorHandle(kProbeShUavDescriptorIndex));

	commandList->SetPipelineState(shProjectionPipelineState_.Get());
	commandList->Dispatch(probeCount, 1u, 1u);

	// SHと可視性は別リソースなので並列に走ってよいが、
	// 次フレームのキャプチャが読むまでに書き終える必要がある。
	commandList->SetPipelineState(visibilityPipelineState_.Get());
	const uint32_t visibilityGroupCount = (kVisibilityTileSize + 7u) / 8u;
	commandList->Dispatch(visibilityGroupCount, visibilityGroupCount, probeCount);

	std::array<D3D12_RESOURCE_BARRIER, 2u> uavBarriers{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = probeShResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = probeVisibilityResource_.Get();
	commandList->ResourceBarrier(static_cast<UINT>(uavBarriers.size()), uavBarriers.data());

	// 描画パスが SRV として読むので、書き終えたら読み取り状態へ戻す。
	TransitionProbeResources(
		commandList,
		static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	std::array<D3D12_RESOURCE_BARRIER, 2u> captureBarriers{};
	ID3D12Resource* captureResources[2] = {
		captureRadianceResource_.Get(),
		captureDistanceResource_.Get()};

	for (uint32_t barrierIndex = 0u; barrierIndex < captureBarriers.size(); barrierIndex++) {
		captureBarriers[barrierIndex].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		captureBarriers[barrierIndex].Transition.pResource = captureResources[barrierIndex];
		captureBarriers[barrierIndex].Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		captureBarriers[barrierIndex].Transition.StateBefore =
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		captureBarriers[barrierIndex].Transition.StateAfter =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	commandList->ResourceBarrier(
		static_cast<UINT>(captureBarriers.size()),
		captureBarriers.data());
}

Vector3 EditorLightProbeManager::GetProbeWorldPosition(uint32_t probeIndex) const {
	if (settings_.countX <= 0 || settings_.countY <= 0) {
		return settings_.origin;
	}

	const uint32_t countX = static_cast<uint32_t>(settings_.countX);
	const uint32_t countY = static_cast<uint32_t>(settings_.countY);
	const uint32_t coordX = probeIndex % countX;
	const uint32_t coordY = (probeIndex / countX) % countY;
	const uint32_t coordZ = probeIndex / (countX * countY);
	return Vector3{
		settings_.origin.x + static_cast<float>(coordX) * settings_.spacing.x,
		settings_.origin.y + static_cast<float>(coordY) * settings_.spacing.y,
		settings_.origin.z + static_cast<float>(coordZ) * settings_.spacing.z};
}

uint32_t EditorLightProbeManager::GetProbeCount() const {
	return probeCount_;
}

void EditorLightProbeManager::FillGridData(LightProbeGridData& gridDataOut) const {
	gridDataOut = LightProbeGridData{};

	if (!IsReady()) {
		return;
	}

	gridDataOut.gridOrigin = settings_.origin;
	gridDataOut.normalBias = settings_.normalBias;
	gridDataOut.gridSpacing = settings_.spacing;
	gridDataOut.intensity = settings_.intensity;
	gridDataOut.gridCountX = settings_.countX;
	gridDataOut.gridCountY = settings_.countY;
	gridDataOut.gridCountZ = settings_.countZ;
	gridDataOut.visibilityTilesPerRow = static_cast<int32_t>(visibilityTilesPerRow_);
	gridDataOut.visibilityInverseAtlasWidth = visibilityAtlasWidth_ > 0u
		? 1.0f / static_cast<float>(visibilityAtlasWidth_)
		: 0.0f;
	gridDataOut.visibilityInverseAtlasHeight = visibilityAtlasHeight_ > 0u
		? 1.0f / static_cast<float>(visibilityAtlasHeight_)
		: 0.0f;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorLightProbeManager::GetProbeResourceTableHandle() const {
	return GetGpuSrvDescriptorHandle(kProbeShSrvDescriptorIndex);
}

// Descriptorが有効かどうか。GIが無効でもBind自体は行うため常に真であってほしい。
bool EditorLightProbeManager::HasResources() const {
	return probeShResource_ != nullptr && probeVisibilityResource_ != nullptr;
}

// GIとして実際に機能しているか。Bakeと実行時参照の可否はこちらで判定する。
bool EditorLightProbeManager::IsReady() const {
	return isInitialized_ &&
		settings_.isEnabled &&
		HasResources() &&
		probeCount_ > 0u;
}

void EditorLightProbeManager::Finalize() {
	ReleaseProbeResources();
	captureRadianceResource_.Reset();
	captureDistanceResource_.Reset();
	captureDepthResource_.Reset();
	captureRtvHeap_.Reset();
	captureDsvHeap_.Reset();
	capturePipelineState_.Reset();
	captureDoubleSidedPipelineState_.Reset();
	shProjectionPipelineState_.Reset();
	visibilityPipelineState_.Reset();
	computeRootSignature_.Reset();
	objectRootSignature_.Reset();
	device_.Reset();
	srvDescriptorHeap_ = nullptr;
	isInitialized_ = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE EditorLightProbeManager::GetCpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr +=
		static_cast<SIZE_T>(srvDescriptorSize_) * static_cast<SIZE_T>(descriptorIndex);
	return descriptorHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorLightProbeManager::GetGpuSrvDescriptorHandle(
	uint32_t descriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle =
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	descriptorHandle.ptr +=
		static_cast<UINT64>(srvDescriptorSize_) * static_cast<UINT64>(descriptorIndex);
	return descriptorHandle;
}
