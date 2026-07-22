#include "EditorGpuParticleManager.h"

#pragma warning(push, 0)
#include <d3dcompiler.h>
#pragma warning(pop)

#include "EditorAssetUtility.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>

namespace {
	D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 sizeInBytes, D3D12_RESOURCE_FLAGS flags) {
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0u;
		desc.Width = sizeInBytes;
		desc.Height = 1u;
		desc.DepthOrArraySize = 1u;
		desc.MipLevels = 1u;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1u;
		desc.SampleDesc.Quality = 0u;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = flags;
		return desc;
	}

	bool SerializeRootSignature(
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

bool EditorGpuParticleManager::Initialize(
	ID3D12Device* device,
	IDxcBlob* clearComputeShader,
	IDxcBlob* updateComputeShader,
	IDxcBlob* spawnComputeShader,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader,
	IDxcBlob* modelVertexShader,
	IDxcBlob* modelPixelShader,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat) {
	if (device == nullptr ||
		clearComputeShader == nullptr ||
		updateComputeShader == nullptr ||
		spawnComputeShader == nullptr ||
		vertexShader == nullptr ||
		pixelShader == nullptr ||
		modelVertexShader == nullptr ||
		modelPixelShader == nullptr) {
		return false;
	}

	Finalize();
	device_ = device;

	if (!CreateBuffers(device)) {
		return false;
	}

	if (!CreateComputePipeline(device, clearComputeShader, updateComputeShader, spawnComputeShader)) {
		return false;
	}

	if (!CreateGraphicsPipeline(
		device,
		vertexShader,
		pixelShader,
		modelVertexShader,
		modelPixelShader,
		renderTargetFormat,
		depthStencilFormat)) {
		return false;
	}

	RequestReset();
	return true;
}

void EditorGpuParticleManager::Finalize() {
	particleBuffer_.Reset();
	particleUploadBuffer_.Reset();
	aliveListBuffer_.Reset();
	deadListBuffer_.Reset();
	computeRootSignature_.Reset();
	clearPipelineState_.Reset();
	updatePipelineState_.Reset();
	spawnPipelineState_.Reset();
	graphicsRootSignature_.Reset();
	graphicsPipelineState_.Reset();
	modelGraphicsPipelineState_.Reset();
	modelMeshes_.clear();
	device_.Reset();
	nextRenderGroup_ = 1u;
	particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	aliveListState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	needsInitialClear_ = true;
	hasEverSpawned_ = false;
}

void EditorGpuParticleManager::RequestReset() {
	needsInitialClear_ = true;
	hasEverSpawned_ = false;
}

void EditorGpuParticleManager::Update(
	ID3D12GraphicsCommandList* commandList,
	const std::vector<EditorEffectManager::GpuParticleSpawn>& spawns,
	float deltaTime) {
	if (commandList == nullptr ||
		particleBuffer_ == nullptr ||
		aliveListBuffer_ == nullptr ||
		deadListBuffer_ == nullptr ||
		clearPipelineState_ == nullptr ||
		updatePipelineState_ == nullptr ||
		spawnPipelineState_ == nullptr) {
		return;
	}

	const bool isResetFrame = needsInitialClear_;
	if (isResetFrame) {
		UploadInitialClear(commandList);
	}

	if (!isResetFrame && hasEverSpawned_) {
		// 前フレームのリスト件数を消し、Particleの寿命からAlive/DeadをGPU上で再構築する。
		ParticleComputeConstants clearConstants{};
		clearConstants.maxParticleCount = kMaxParticleCount;
		clearConstants.commandValue = 0u;
		commandList->SetComputeRootSignature(computeRootSignature_.Get());
		commandList->SetPipelineState(clearPipelineState_.Get());
		commandList->SetComputeRoot32BitConstants(0, 4u, &clearConstants, 0u);
		commandList->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());
		commandList->SetComputeRootUnorderedAccessView(2, aliveListBuffer_->GetGPUVirtualAddress());
		commandList->SetComputeRootUnorderedAccessView(3, deadListBuffer_->GetGPUVirtualAddress());
		commandList->Dispatch(1u, 1u, 1u);

		// Counterを消したClearが完了してから、UpdateでAlive/Deadを再構築する。
		std::array<D3D12_RESOURCE_BARRIER, 2u> clearBarriers{};
		clearBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		clearBarriers[0].UAV.pResource = aliveListBuffer_.Get();
		clearBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		clearBarriers[1].UAV.pResource = deadListBuffer_.Get();
		commandList->ResourceBarrier(static_cast<UINT>(clearBarriers.size()), clearBarriers.data());

		ParticleComputeConstants updateConstants{};
		updateConstants.deltaTime = (std::max)(deltaTime, 0.0f);
		updateConstants.globalDamping = 0.0f;
		updateConstants.maxParticleCount = kMaxParticleCount;
		commandList->SetPipelineState(updatePipelineState_.Get());
		commandList->SetComputeRoot32BitConstants(0, 4u, &updateConstants, 0u);
		commandList->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());
		commandList->SetComputeRootUnorderedAccessView(2, aliveListBuffer_->GetGPUVirtualAddress());
		commandList->SetComputeRootUnorderedAccessView(3, deadListBuffer_->GetGPUVirtualAddress());
		commandList->Dispatch((kMaxParticleCount + 63u) / 64u, 1u, 1u);

		std::array<D3D12_RESOURCE_BARRIER, 3u> updateBarriers{};
		updateBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		updateBarriers[0].UAV.pResource = particleBuffer_.Get();
		updateBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		updateBarriers[1].UAV.pResource = aliveListBuffer_.Get();
		updateBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		updateBarriers[2].UAV.pResource = deadListBuffer_.Get();
		commandList->ResourceBarrier(static_cast<UINT>(updateBarriers.size()), updateBarriers.data());
	}

	if (!spawns.empty()) {
		for (const EditorEffectManager::GpuParticleSpawn& spawn : spawns) {
			EnsureModelMesh(spawn.renderAssetPath);
		}

		UploadSpawns(commandList, spawns);
		hasEverSpawned_ = true;
	}
}

void EditorGpuParticleManager::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewProjection) {
	if (!hasEverSpawned_ ||
		commandList == nullptr ||
		particleBuffer_ == nullptr ||
		aliveListBuffer_ == nullptr ||
		graphicsRootSignature_ == nullptr ||
		graphicsPipelineState_ == nullptr) {
		return;
	}

	TransitionResource(
		commandList,
		particleBuffer_.Get(),
		particleState_,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	particleState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	TransitionResource(
		commandList,
		aliveListBuffer_.Get(),
		aliveListState_,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	aliveListState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
	commandList->SetPipelineState(graphicsPipelineState_.Get());
	ParticleDrawConstants drawConstants{};
	drawConstants.viewProjection = viewProjection;
	drawConstants.renderGroup = 0u;
	commandList->SetGraphicsRoot32BitConstants(0, 20u, &drawConstants, 0u);
	commandList->SetGraphicsRootShaderResourceView(1, particleBuffer_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootShaderResourceView(2, aliveListBuffer_->GetGPUVirtualAddress());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 0, nullptr);
	commandList->DrawInstanced(6u, kMaxParticleCount, 0u, 0u);

	// FBX / OBJ は同じ Particle Buffer を使い、Mesh ごとに GPU インスタンシングする。
	commandList->SetPipelineState(modelGraphicsPipelineState_.Get());
	for (const auto& modelPair : modelMeshes_) {
		const ParticleModelMesh& modelMesh = modelPair.second;

		if (modelMesh.vertexResource == nullptr || modelMesh.vertexCount == 0u) {
			continue;
		}

		drawConstants.renderGroup = modelMesh.renderGroup;
		commandList->SetGraphicsRoot32BitConstants(0, 20u, &drawConstants, 0u);
		commandList->IASetVertexBuffers(0, 1u, &modelMesh.vertexBufferView);
		commandList->DrawInstanced(modelMesh.vertexCount, kMaxParticleCount, 0u, 0u);
	}

	TransitionResource(
		commandList,
		particleBuffer_.Get(),
		particleState_,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	TransitionResource(
		commandList,
		aliveListBuffer_.Get(),
		aliveListState_,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	aliveListState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool EditorGpuParticleManager::CreateBuffers(ID3D12Device* device) {
	const UINT64 particleBufferSize = sizeof(GpuParticleData) * kMaxParticleCount;
	const UINT64 listBufferSize = sizeof(uint32_t) * (kMaxParticleCount + 1u);

	D3D12_HEAP_PROPERTIES defaultHeap{};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_HEAP_PROPERTIES uploadHeap{};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC particleDesc = MakeBufferDesc(
		particleBufferSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HRESULT hr = device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&particleDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(particleBuffer_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_RESOURCE_DESC uploadDesc = MakeBufferDesc(particleBufferSize, D3D12_RESOURCE_FLAG_NONE);
	hr = device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(particleUploadBuffer_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_RESOURCE_DESC listDesc = MakeBufferDesc(
		listBufferSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	hr = device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&listDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(aliveListBuffer_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	hr = device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&listDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(deadListBuffer_.GetAddressOf()));
	return SUCCEEDED(hr);
}

bool EditorGpuParticleManager::CreateComputePipeline(
	ID3D12Device* device,
	IDxcBlob* clearComputeShader,
	IDxcBlob* updateComputeShader,
	IDxcBlob* spawnComputeShader) {
	D3D12_ROOT_PARAMETER rootParameters[5]{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[0].Constants.ShaderRegister = 0u;
	rootParameters[0].Constants.Num32BitValues = 4u;

	for (UINT parameterIndex = 1u; parameterIndex < 4u; parameterIndex++) {
		rootParameters[parameterIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		rootParameters[parameterIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[parameterIndex].Descriptor.ShaderRegister = parameterIndex - 1u;
	}
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[4].Descriptor.ShaderRegister = 0u;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	if (!SerializeRootSignature(rootSignatureDesc, signatureBlob)) {
		return false;
	}

	HRESULT hr = device->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(computeRootSignature_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC clearDesc{};
	clearDesc.pRootSignature = computeRootSignature_.Get();
	clearDesc.CS = {clearComputeShader->GetBufferPointer(), clearComputeShader->GetBufferSize()};
	hr = device->CreateComputePipelineState(&clearDesc, IID_PPV_ARGS(clearPipelineState_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC updateDesc{};
	updateDesc.pRootSignature = computeRootSignature_.Get();
	updateDesc.CS = {updateComputeShader->GetBufferPointer(), updateComputeShader->GetBufferSize()};
	hr = device->CreateComputePipelineState(&updateDesc, IID_PPV_ARGS(updatePipelineState_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC spawnDesc{};
	spawnDesc.pRootSignature = computeRootSignature_.Get();
	spawnDesc.CS = {spawnComputeShader->GetBufferPointer(), spawnComputeShader->GetBufferSize()};
	hr = device->CreateComputePipelineState(&spawnDesc, IID_PPV_ARGS(spawnPipelineState_.GetAddressOf()));
	return SUCCEEDED(hr);
}

bool EditorGpuParticleManager::CreateGraphicsPipeline(
	ID3D12Device* device,
	IDxcBlob* vertexShader,
	IDxcBlob* pixelShader,
	IDxcBlob* modelVertexShader,
	IDxcBlob* modelPixelShader,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat) {
	D3D12_ROOT_PARAMETER rootParameters[3]{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[0].Constants.ShaderRegister = 0u;
	rootParameters[0].Constants.Num32BitValues = 20u;
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0u;
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[2].Descriptor.ShaderRegister = 1u;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	if (!SerializeRootSignature(rootSignatureDesc, signatureBlob)) {
		return false;
	}

	HRESULT hr = device->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(graphicsRootSignature_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = graphicsRootSignature_.Get();
	psoDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	psoDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	psoDesc.BlendState = blendDesc;
	psoDesc.RasterizerState = rasterizerDesc;
	psoDesc.DepthStencilState = depthStencilDesc;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1u;
	psoDesc.RTVFormats[0] = renderTargetFormat;
	psoDesc.DSVFormat = depthStencilFormat;
	psoDesc.SampleDesc.Count = 1u;
	psoDesc.InputLayout.pInputElementDescs = nullptr;
	psoDesc.InputLayout.NumElements = 0u;

	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(graphicsPipelineState_.GetAddressOf()));
	if (FAILED(hr)) {
		return false;
	}

	const D3D12_INPUT_ELEMENT_DESC modelInputElements[] = {
		{"POSITION", 0u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
		{"TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u},
		{"NORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u}
	};
	psoDesc.VS = {modelVertexShader->GetBufferPointer(), modelVertexShader->GetBufferSize()};
	psoDesc.PS = {modelPixelShader->GetBufferPointer(), modelPixelShader->GetBufferSize()};
	psoDesc.InputLayout.pInputElementDescs = modelInputElements;
	psoDesc.InputLayout.NumElements = static_cast<UINT>(_countof(modelInputElements));
	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(modelGraphicsPipelineState_.GetAddressOf()));
	return SUCCEEDED(hr);
}

void EditorGpuParticleManager::UploadInitialClear(ID3D12GraphicsCommandList* commandList) {
	ParticleComputeConstants clearConstants{};
	clearConstants.maxParticleCount = kMaxParticleCount;
	clearConstants.commandValue = 1u;
	commandList->SetComputeRootSignature(computeRootSignature_.Get());
	commandList->SetPipelineState(clearPipelineState_.Get());
	commandList->SetComputeRoot32BitConstants(0, 4u, &clearConstants, 0u);
	commandList->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(2, aliveListBuffer_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(3, deadListBuffer_->GetGPUVirtualAddress());
	commandList->Dispatch((kMaxParticleCount + 63u) / 64u, 1u, 1u);

	std::array<D3D12_RESOURCE_BARRIER, 3u> clearBarriers{};
	clearBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	clearBarriers[0].UAV.pResource = particleBuffer_.Get();
	clearBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	clearBarriers[1].UAV.pResource = aliveListBuffer_.Get();
	clearBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	clearBarriers[2].UAV.pResource = deadListBuffer_.Get();
	commandList->ResourceBarrier(static_cast<UINT>(clearBarriers.size()), clearBarriers.data());
	needsInitialClear_ = false;
}

void EditorGpuParticleManager::UploadSpawns(
	ID3D12GraphicsCommandList* commandList,
	const std::vector<EditorEffectManager::GpuParticleSpawn>& spawns) {
	const uint32_t uploadCount = (std::min)(
		static_cast<uint32_t>(spawns.size()),
		kMaxParticleCount);
	if (uploadCount == 0u) {
		return;
	}

	void* mappedAddress = nullptr;
	const D3D12_RANGE readRange{0u, 0u};
	HRESULT hr = particleUploadBuffer_->Map(0u, &readRange, &mappedAddress);
	assert(SUCCEEDED(hr));
	GpuParticleData* uploadParticles = static_cast<GpuParticleData*>(mappedAddress);
	for (uint32_t spawnIndex = 0u; spawnIndex < uploadCount; spawnIndex++) {
		uploadParticles[spawnIndex] = ConvertSpawn(spawns[spawnIndex]);
	}
	particleUploadBuffer_->Unmap(0u, nullptr);

	ParticleComputeConstants spawnConstants{};
	spawnConstants.maxParticleCount = kMaxParticleCount;
	spawnConstants.commandValue = uploadCount;
	commandList->SetComputeRootSignature(computeRootSignature_.Get());
	commandList->SetPipelineState(spawnPipelineState_.Get());
	commandList->SetComputeRoot32BitConstants(0, 4u, &spawnConstants, 0u);
	commandList->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(2, aliveListBuffer_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(3, deadListBuffer_->GetGPUVirtualAddress());
	commandList->SetComputeRootShaderResourceView(4, particleUploadBuffer_->GetGPUVirtualAddress());
	commandList->Dispatch((uploadCount + 63u) / 64u, 1u, 1u);

	std::array<D3D12_RESOURCE_BARRIER, 3u> spawnBarriers{};
	spawnBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	spawnBarriers[0].UAV.pResource = particleBuffer_.Get();
	spawnBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	spawnBarriers[1].UAV.pResource = aliveListBuffer_.Get();
	spawnBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	spawnBarriers[2].UAV.pResource = deadListBuffer_.Get();
	commandList->ResourceBarrier(static_cast<UINT>(spawnBarriers.size()), spawnBarriers.data());
}

EditorGpuParticleManager::GpuParticleData EditorGpuParticleManager::ConvertSpawn(
	const EditorEffectManager::GpuParticleSpawn& spawn) {
	GpuParticleData particle{};
	particle.positionLifetime = {spawn.position.x, spawn.position.y, spawn.position.z, (std::max)(spawn.lifetime, 0.01f)};
	particle.velocitySize = {spawn.velocity.x, spawn.velocity.y, spawn.velocity.z, (std::max)(spawn.startSize, 0.001f)};
	particle.startColor = {
		spawn.startColor.x,
		spawn.startColor.y,
		spawn.startColor.z,
		(std::clamp)(spawn.startAlpha, 0.0f, 1.0f)};
	particle.endColor = {
		spawn.endColor.x,
		spawn.endColor.y,
		spawn.endColor.z,
		(std::clamp)(spawn.endAlpha, 0.0f, 1.0f)};
	particle.lifeSize = {
		0.0f,
		(std::max)(spawn.lifetime, 0.01f),
		(std::max)(spawn.startSize, 0.001f),
		(std::max)(spawn.endSize, 0.0f)};
	particle.physics = {
		spawn.gravity,
		(std::max)(spawn.drag, 0.0f),
		(std::max)(spawn.noiseStrength, 0.0f),
		(std::max)(spawn.noiseFrequency, 0.0001f)};
	particle.motion0 = {
		static_cast<float>((std::clamp)(spawn.motionType, 0, 6)),
		spawn.angularSpeed,
		spawn.radialAcceleration,
		spawn.waveAmplitude};
	particle.motion1 = {
		spawn.motionCenter.x,
		spawn.motionCenter.y,
		spawn.motionCenter.z,
		(std::max)(spawn.waveFrequency, 0.0f)};
	particle.motion2 = {
		(std::max)(spawn.attractorStrength, 0.0f),
		spawn.rotation,
		spawn.rotationSpeed,
		0.0f};
	particle.rendering = {
		static_cast<float>(EnsureModelMesh(spawn.renderAssetPath)),
		(std::max)(spawn.emissionStrength, 0.0f),
		0.0f,
		0.0f};
	return particle;
}

uint32_t EditorGpuParticleManager::EnsureModelMesh(const std::string& assetPath) {
	if (assetPath.empty() || device_ == nullptr) {
		return 0u;
	}

	const auto existingModelIterator = modelMeshes_.find(assetPath);
	if (existingModelIterator != modelMeshes_.end()) {
		return existingModelIterator->second.renderGroup;
	}

	ModelData modelData{};
	if (!EditorAssetUtility::LoadModelAsset(assetPath, modelData) || modelData.vertices.empty()) {
		return 0u;
	}

	const float maximumSize = (std::max)(
		(std::max)(std::abs(modelData.localBoundsSize.x), std::abs(modelData.localBoundsSize.y)),
		(std::max)(std::abs(modelData.localBoundsSize.z), 0.0001f));
	const float normalizationScale = 1.0f / maximumSize;
	for (VertexData& vertex : modelData.vertices) {
		vertex.position.x = (vertex.position.x - modelData.localBoundsCenter.x) * normalizationScale;
		vertex.position.y = (vertex.position.y - modelData.localBoundsCenter.y) * normalizationScale;
		vertex.position.z = (vertex.position.z - modelData.localBoundsCenter.z) * normalizationScale;
	}

	const UINT64 vertexBufferSize = sizeof(VertexData) * static_cast<UINT64>(modelData.vertices.size());
	D3D12_HEAP_PROPERTIES uploadHeap{};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	const D3D12_RESOURCE_DESC vertexBufferDesc = MakeBufferDesc(vertexBufferSize, D3D12_RESOURCE_FLAG_NONE);
	ParticleModelMesh modelMesh{};
	const HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&vertexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(modelMesh.vertexResource.GetAddressOf()));

	if (FAILED(createResult)) {
		return 0u;
	}

	void* mappedAddress = nullptr;
	const D3D12_RANGE readRange{0u, 0u};
	const HRESULT mapResult = modelMesh.vertexResource->Map(0u, &readRange, &mappedAddress);
	if (FAILED(mapResult) || mappedAddress == nullptr) {
		return 0u;
	}

	std::memcpy(mappedAddress, modelData.vertices.data(), static_cast<size_t>(vertexBufferSize));
	modelMesh.vertexResource->Unmap(0u, nullptr);
	modelMesh.vertexBufferView.BufferLocation = modelMesh.vertexResource->GetGPUVirtualAddress();
	modelMesh.vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
	modelMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);
	modelMesh.vertexCount = static_cast<uint32_t>(modelData.vertices.size());
	modelMesh.renderGroup = nextRenderGroup_;
	nextRenderGroup_++;
	const uint32_t renderGroup = modelMesh.renderGroup;
	modelMeshes_.emplace(assetPath, std::move(modelMesh));
	return renderGroup;
}

void EditorGpuParticleManager::TransitionResource(
	ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES beforeState,
	D3D12_RESOURCE_STATES afterState) {
	if (commandList == nullptr || resource == nullptr || beforeState == afterState) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	commandList->ResourceBarrier(1u, &barrier);
}
