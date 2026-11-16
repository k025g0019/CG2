#include "3d/Object3dCommon.h"
#include "3d/Model.h"
#include "math/Vector.h"

#include <filesystem>

namespace {
	using Microsoft::WRL::ComPtr;

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	ComPtr<IDxcBlob> Compile(const wchar_t* path, const wchar_t* profile,
	                         IDxcUtils* utils, IDxcCompiler3* compiler, IDxcIncludeHandler* includes) {
		ComPtr<IDxcBlobEncoding> source;
		if (FAILED(utils->LoadFile(path, nullptr, source.GetAddressOf()))) { return {}; }
		DxcBuffer buffer{source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8};
		LPCWSTR args[] = {path, L"-E", L"main", L"-T", profile, L"-Zpr"};
		ComPtr<IDxcResult> result;
		if (FAILED(compiler->Compile(&buffer, args, _countof(args), includes,
			IID_PPV_ARGS(result.GetAddressOf())))) { return {}; }
		HRESULT status = E_FAIL;
		if (FAILED(result->GetStatus(&status)) || FAILED(status)) { return {}; }
		ComPtr<IDxcBlob> shader;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT,
			IID_PPV_ARGS(shader.GetAddressOf()), nullptr))) { return {}; }
		return shader;
	}
}

bool Object3dCommon::Initialize(ID3D12Device* device, TextureManager* textures,
                                IDxcUtils* utils, IDxcCompiler3* compiler, IDxcIncludeHandler* includes) {
	if (!device || !textures || !utils || !compiler || !includes) { return false; }
	auto vs = Compile(L"resources/shaders/Object3d.VS.hlsl", L"vs_6_0", utils, compiler, includes);
	auto ps = Compile(L"resources/shaders/Object3d.PS.hlsl", L"ps_6_0", utils, compiler, includes);
	if (!vs || !ps) { return false; }

	D3D12_DESCRIPTOR_RANGE textureRange{};
	textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange.NumDescriptors = 1;
	textureRange.BaseShaderRegister = 0;
	textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	D3D12_ROOT_PARAMETER parameters[4]{};
	for (UINT i = 0; i < 3; ++i) {
		parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		parameters[i].Descriptor.ShaderRegister = i == 2 ? 1 : 0;
		parameters[i].ShaderVisibility = i == 1 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
	}
	parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	parameters[3].DescriptorTable.NumDescriptorRanges = 1;
	parameters[3].DescriptorTable.pDescriptorRanges = &textureRange;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.NumParameters = _countof(parameters);
	rootDesc.pParameters = parameters;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers = &sampler;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			signature.GetAddressOf(), error.GetAddressOf())) ||
		FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(),
			signature->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf())))) { return false; }

	D3D12_INPUT_ELEMENT_DESC layout[] = {
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		}
	};
	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.DepthClipEnable = TRUE;
	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	D3D12_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = rootSignature_.Get();
	pso.InputLayout = {layout, _countof(layout)};
	pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
	pso.RasterizerState = rasterizer;
	pso.BlendState = blend;
	pso.DepthStencilState = depth;
	pso.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	pso.SampleDesc.Count = 1;
	if (FAILED(device->CreateGraphicsPipelineState(&pso,
		IID_PPV_ARGS(pipelineState_.GetAddressOf())))) { return false; }

	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = 256;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(lightResource_.GetAddressOf())))) { return false; }
	DirectionalLight* light = nullptr;
	if (FAILED(lightResource_->Map(0, nullptr, reinterpret_cast<void**>(&light)))) { return false; }
	*light = {{1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, 1.0f};
	device_ = device;
	textures_ = textures;
	return true;
}

std::shared_ptr<const Model> Object3dCommon::LoadModel(const std::string& filePath) {
	if (!device_ || !textures_) { return {}; }
	const std::string key = std::filesystem::path(filePath).lexically_normal().generic_string();
	if (auto it = models_.find(key); it != models_.end()) { return it->second; }
	auto model = std::make_shared<Model>();
	if (!model->Initialize(device_, textures_, key)) { return {}; }
	models_.emplace(key, model);
	return model;
}

void Object3dCommon::ApplyDrawSettings(ID3D12GraphicsCommandList* commandList) const {
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->SetGraphicsRootConstantBufferView(2, lightResource_->GetGPUVirtualAddress());
}

void Object3dCommon::Finalize() {
	models_.clear();
	lightResource_.Reset();
	pipelineState_.Reset();
	rootSignature_.Reset();
	device_ = nullptr;
	textures_ = nullptr;
}
