#include "EditorVfxRenderer.h"

#include "Source/Engine/Core/EditorSharedState.h"
#include "Source/Engine/Core/StringUtility.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

namespace {
	bool SerializeVfxRootSignature(
		const D3D12_ROOT_SIGNATURE_DESC& desc,
		Microsoft::WRL::ComPtr<ID3DBlob>& signatureBlob) {
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		const HRESULT hr = D3D12SerializeRootSignature(
			&desc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			signatureBlob.GetAddressOf(),
			errorBlob.GetAddressOf());
		return SUCCEEDED(hr) && signatureBlob != nullptr;
	}
}

bool EditorVfxRenderer::Initialize(
	ID3D12Device* device,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat) {
	if (device == nullptr || vertexShader == nullptr || pixelShader == nullptr) {
		return false;
	}

	device_ = device;

	if (!CreateDescriptorHeap(device)) {
		return false;
	}

	if (!CreateRootSignatureAndPipelines(device, vertexShader, pixelShader, renderTargetFormat, depthStencilFormat)) {
		return false;
	}

	if (!CreateVertexBuffer(device)) {
		return false;
	}

	if (!LoadFallbackTexture(device)) {
		return false;
	}

	return true;
}

void EditorVfxRenderer::Finalize() {
	if (vertexUploadBuffer_ != nullptr && mappedVertices_ != nullptr) {
		vertexUploadBuffer_->Unmap(0u, nullptr);
	}

	mappedVertices_ = nullptr;
	vertexUploadBuffer_.Reset();
	textureCache_.clear();
	srvDescriptorHeap_.Reset();
	alphaBlendPipelineState_.Reset();
	additivePipelineState_.Reset();
	rootSignature_.Reset();
	device_.Reset();
	nextTextureDescriptorIndex_ = 0;
	uploadedVertexCount_ = 0u;
}

bool EditorVfxRenderer::CreateDescriptorHeap(ID3D12Device* device) {
	using namespace EditorSharedState;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	// 末尾1枚をSoft Particle用のOpaque Depth Copy SRV専用Slotとして予約する(kMaxTextureCount番目)。
	heapDesc.NumDescriptors = static_cast<UINT>(kMaxTextureCount) + 1u;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	const HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(srvDescriptorHeap_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	srvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	depthSrvHandleCPU_ = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), srvDescriptorSize_, static_cast<UINT>(kMaxTextureCount));
	depthSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), srvDescriptorSize_, static_cast<UINT>(kMaxTextureCount));
	return true;
}

bool EditorVfxRenderer::CreateRootSignatureAndPipelines(
	ID3D12Device* device,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat) {
	D3D12_DESCRIPTOR_RANGE textureRange{};
	textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange.NumDescriptors = 1u;
	textureRange.BaseShaderRegister = 0u;
	textureRange.RegisterSpace = 0u;
	textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// Soft Particle用: 既存のOpaque Depth Copy(gWaterSceneDepthと同じ資源)をt1で読む。
	D3D12_DESCRIPTOR_RANGE depthTextureRange{};
	depthTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	depthTextureRange.NumDescriptors = 1u;
	depthTextureRange.BaseShaderRegister = 1u;
	depthTextureRange.RegisterSpace = 0u;
	depthTextureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	std::array<D3D12_ROOT_PARAMETER, 5u> rootParameters{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[0].Constants.ShaderRegister = 0u;
	rootParameters[0].Constants.Num32BitValues = 16u; // 4x4 ViewProjection行列。
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1u;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = 1u;
	rootParameters[2].DescriptorTable.pDescriptorRanges = &depthTextureRange;
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Constants.ShaderRegister = 1u;
	rootParameters[3].Constants.Num32BitValues = 20u; // 4x4 逆ViewProjection(16) + Viewport UV Scale/Offset(4)。OceanSurfaceのgWaterViewと同じ構成。
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].Constants.ShaderRegister = 2u;
	rootParameters[4].Constants.Num32BitValues = 2u; // useSoftParticle(0/1) + softParticleFadeDistance。Batch毎に切り替える。

	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0u;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDesc.pParameters = rootParameters.data();
	rootSignatureDesc.NumStaticSamplers = 1u;
	rootSignatureDesc.pStaticSamplers = &staticSampler;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	if (!SerializeVfxRootSignature(rootSignatureDesc, signatureBlob)) {
		return false;
	}

	HRESULT hr = device->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		{"POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u, 0u, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
		{"TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 12u, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
		{"COLOR", 0u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u, 20u, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
	};

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;

	// Particleの基本方針: Depth Test ON / Depth Write OFF。
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	psoDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	psoDesc.RasterizerState = rasterizerDesc;
	psoDesc.DepthStencilState = depthStencilDesc;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1u;
	psoDesc.RTVFormats[0] = renderTargetFormat;
	psoDesc.DSVFormat = depthStencilFormat;
	psoDesc.SampleDesc.Count = 1u;
	psoDesc.InputLayout.pInputElementDescs = inputElements;
	psoDesc.InputLayout.NumElements = static_cast<UINT>(_countof(inputElements));

	// Alpha Blend: Sortingが必要な系統(煙・炎・破片等)。
	D3D12_BLEND_DESC alphaBlendDesc{};
	alphaBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	alphaBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	alphaBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	alphaBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	alphaBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	alphaBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	alphaBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	alphaBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.BlendState = alphaBlendDesc;

	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(alphaBlendPipelineState_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	// Additive: 曳光・爆発発光等。Sorting省略可能。
	D3D12_BLEND_DESC additiveBlendDesc = alphaBlendDesc;
	additiveBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	additiveBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState = additiveBlendDesc;

	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(additivePipelineState_.GetAddressOf()));
	return SUCCEEDED(hr);
}

bool EditorVfxRenderer::CreateVertexBuffer(ID3D12Device* device) {
	using namespace EditorSharedState;

	const size_t bufferSize = sizeof(VfxVertex) * static_cast<size_t>(kMaxDynamicVertexCount);
	ID3D12Resource* buffer = CreateBufferResource(device, bufferSize);
	if (buffer == nullptr) {
		return false;
	}

	vertexUploadBuffer_.Attach(buffer);

	const D3D12_RANGE readRange{0u, 0u};
	const HRESULT hr = vertexUploadBuffer_->Map(0u, &readRange, reinterpret_cast<void**>(&mappedVertices_));
	if (FAILED(hr)) {
		return false;
	}

	vertexBufferView_.BufferLocation = vertexUploadBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(bufferSize);
	vertexBufferView_.StrideInBytes = static_cast<UINT>(sizeof(VfxVertex));
	return true;
}

bool EditorVfxRenderer::UploadTextureImmediate(const std::string& texturePath, TextureEntry& entry) {
	using namespace EditorSharedState;

	if (device_ == nullptr ||
		texturePath.empty() ||
		!std::filesystem::exists(texturePath) ||
		g_commandAllocator == nullptr ||
		g_commandList == nullptr ||
		g_commandQueue == nullptr ||
		g_fence == nullptr) {
		return false;
	}

	if (nextTextureDescriptorIndex_ >= kMaxTextureCount) {
		return false;
	}

	DirectX::ScratchImage mipImages = LoadTexture(ConvertString(texturePath));
	const DirectX::TexMetadata textureMetadata = mipImages.GetMetadata();
	ID3D12Resource* textureResource = CreateTextureResource(device_.Get(), textureMetadata);
	if (textureResource == nullptr) {
		return false;
	}

	entry.textureResource.Attach(textureResource);

	HRESULT hr = g_commandAllocator->Reset();
	if (FAILED(hr)) {
		return false;
	}

	hr = g_commandList->Reset(g_commandAllocator.Get(), nullptr);
	if (FAILED(hr)) {
		return false;
	}

	ID3D12Resource* uploadResource = UploadTextureData(device_.Get(), g_commandList.Get(), entry.textureResource.Get(), mipImages);
	if (uploadResource == nullptr) {
		return false;
	}

	entry.uploadResource.Attach(uploadResource);

	hr = g_commandList->Close();
	if (FAILED(hr)) {
		return false;
	}

	ID3D12CommandList* commandLists[] = {g_commandList.Get()};
	g_commandQueue->ExecuteCommandLists(1u, commandLists);
	g_fenceValue++;
	hr = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
	if (FAILED(hr)) {
		return false;
	}

	if (g_fence->GetCompletedValue() < g_fenceValue) {
		g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}

	entry.descriptorIndex = nextTextureDescriptorIndex_;
	nextTextureDescriptorIndex_++;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureMetadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadata.mipLevels);

	const D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = GetCPUDescriptorHandle(
		srvDescriptorHeap_.Get(), srvDescriptorSize_, static_cast<UINT>(entry.descriptorIndex));
	entry.srvGpuHandle = GetGPUDescriptorHandle(
		srvDescriptorHeap_.Get(), srvDescriptorSize_, static_cast<UINT>(entry.descriptorIndex));
	device_->CreateShaderResourceView(entry.textureResource.Get(), &srvDesc, srvCpuHandle);
	return true;
}

bool EditorVfxRenderer::LoadFallbackTexture(ID3D12Device* device) {
	(void)device;
	// resources/editorDefault配下の白TextureをFallbackとして使う。無ければ後段でTexture未設定として扱う。
	const std::string fallbackPath = "resources/editorDefault/uvChecker.png";
	TextureEntry entry{};
	if (UploadTextureImmediate(fallbackPath, entry)) {
		fallbackTextureHandle_ = entry.srvGpuHandle;
		fallbackTextureHandleCPU_ = EditorSharedState::GetCPUDescriptorHandle(
			srvDescriptorHeap_.Get(), srvDescriptorSize_, static_cast<UINT>(entry.descriptorIndex));

		// Soft Particle用Depth SRV Slotは、実際のOpaque Depth Copyが初めて渡されるまでこのFallback Textureで埋めておく。
		// (Draw前提: Root SignatureはPixel Shaderがt1を静的参照するため、常に有効な記述子を指しておく必要がある)
		if (device_ != nullptr) {
			device_->CopyDescriptorsSimple(
				1u,
				depthSrvHandleCPU_,
				fallbackTextureHandleCPU_,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		textureCache_.emplace(fallbackPath, std::move(entry));
	}
	return true;
}

bool EditorVfxRenderer::EnsureTexture(const std::string& texturePath) {
	if (texturePath.empty()) {
		return false;
	}

	if (textureCache_.find(texturePath) != textureCache_.end()) {
		return true;
	}

	TextureEntry entry{};
	if (!UploadTextureImmediate(texturePath, entry)) {
		return false;
	}

	textureCache_.emplace(texturePath, std::move(entry));
	return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorVfxRenderer::GetTextureHandle(const std::string& texturePath) const {
	const auto textureIterator = textureCache_.find(texturePath);
	if (textureIterator != textureCache_.end() && textureIterator->second.srvGpuHandle.ptr != 0u) {
		return textureIterator->second.srvGpuHandle;
	}

	return fallbackTextureHandle_;
}

void EditorVfxRenderer::BeginFrame() {
	uploadedVertexCount_ = 0u;
}

uint32_t EditorVfxRenderer::AppendVertices(const VfxVertex* vertices, uint32_t vertexCount) {
	if (vertices == nullptr || vertexCount == 0u || mappedVertices_ == nullptr) {
		return UINT32_MAX;
	}

	if (uploadedVertexCount_ + vertexCount > kMaxDynamicVertexCount) {
		return UINT32_MAX;
	}

	const uint32_t firstVertex = uploadedVertexCount_;
	std::memcpy(mappedVertices_ + firstVertex, vertices, sizeof(VfxVertex) * static_cast<size_t>(vertexCount));
	uploadedVertexCount_ += vertexCount;
	return firstVertex;
}

void EditorVfxRenderer::EndFrame() {
}

void EditorVfxRenderer::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewProjection,
	const std::vector<VfxBatch>& batches,
	const SoftParticleViewContext& softParticleViewContext) {
	if (commandList == nullptr || batches.empty() || uploadedVertexCount_ == 0u ||
		rootSignature_ == nullptr || alphaBlendPipelineState_ == nullptr) {
		return;
	}

	// Soft Particle用Depth SRV Slotへ、既存のOpaque Depth Copy(EditorSharedState::g_opaqueDepthCopyResource)を都度反映する。
	// 資源自体は起動後常に有効なため、isSceneDepthValid=falseのCall(Planar Reflection等)でもCopy自体は行って構わない。
	// 実際にShaderがSampleするかどうかはBatch毎のuseSoftParticle(isSceneDepthValidでCPU側にClampした値)で制御する。
	if (device_ != nullptr && EditorSharedState::g_opaqueDepthCopyResource != nullptr &&
		EditorSharedState::g_opaqueDepthCopySrvHandleCPU.ptr != 0u) {
		device_->CopyDescriptorsSimple(
			1u,
			depthSrvHandleCPU_,
			EditorSharedState::g_opaqueDepthCopySrvHandleCPU,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	ID3D12DescriptorHeap* heaps[] = {srvDescriptorHeap_.Get()};
	commandList->SetDescriptorHeaps(1u, heaps);
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetGraphicsRoot32BitConstants(0, 16u, &viewProjection, 0u);
	commandList->SetGraphicsRootDescriptorTable(2, depthSrvHandleGPU_);

	std::array<float, 20u> sceneDepthViewConstants{};
	std::memcpy(sceneDepthViewConstants.data(), &softParticleViewContext.inverseViewProjection.matrix[0][0], sizeof(float) * 16u);
	sceneDepthViewConstants[16] = softParticleViewContext.viewportUvScaleX;
	sceneDepthViewConstants[17] = softParticleViewContext.viewportUvScaleY;
	sceneDepthViewConstants[18] = softParticleViewContext.viewportUvOffsetX;
	sceneDepthViewConstants[19] = softParticleViewContext.viewportUvOffsetY;
	commandList->SetGraphicsRoot32BitConstants(3, static_cast<UINT>(sceneDepthViewConstants.size()), sceneDepthViewConstants.data(), 0u);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0u, 1u, &vertexBufferView_);

	ID3D12PipelineState* lastPipelineState = nullptr;
	for (const VfxBatch& batch : batches) {
		if (batch.vertexCount == 0u) {
			continue;
		}

		ID3D12PipelineState* pipelineState = batch.blendVariant == BlendVariant::Additive
			? additivePipelineState_.Get()
			: alphaBlendPipelineState_.Get();
		if (pipelineState != lastPipelineState) {
			commandList->SetPipelineState(pipelineState);
			lastPipelineState = pipelineState;
		}

		const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = GetTextureHandle(batch.texturePath);
		commandList->SetGraphicsRootDescriptorTable(1, textureHandle);

		const bool effectiveUseSoftParticle = softParticleViewContext.isSceneDepthValid && batch.useSoftParticle;
		const std::array<float, 2u> softParticleConstants{
			effectiveUseSoftParticle ? 1.0f : 0.0f,
			(std::max)(batch.softParticleFadeDistance, 0.001f)};
		commandList->SetGraphicsRoot32BitConstants(4, static_cast<UINT>(softParticleConstants.size()), softParticleConstants.data(), 0u);

		commandList->DrawInstanced(batch.vertexCount, 1u, batch.firstVertex, 0u);
	}
}
