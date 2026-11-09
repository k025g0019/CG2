#include "2d/SpriteCommon.h"

#include "base/WinApp.h"
#include "debug/Logger.h"

#include <cmath>
#include <stdexcept>

namespace {
	void CheckResult(HRESULT result, const char* operation) {
		if (FAILED(result)) {
			throw std::runtime_error(operation);
		}
	}
}

void SpriteCommon::Initialize(DirectXCommon* dxCommon) {
	if (dxCommon == nullptr || dxCommon_ != nullptr) {
		throw std::logic_error("SpriteCommon::Initialize: invalid initialization");
	}
	dxCommon_ = dxCommon;
	SetScreenSize({static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight)});
	rootSignature();
	graphicsPipeline();
}

void SpriteCommon::SetScreenSize(const Vector2& screenSize) {
	if (!std::isfinite(screenSize.x) || !std::isfinite(screenSize.y) ||
	    screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		throw std::invalid_argument("SpriteCommon: screen size must be positive");
	}
	screenSize_ = screenSize;
	// 左上(0,0)、右向きX・下向きYのピクセル座標をクリップ座標へ変換する。
	projectionMatrix_ = MakeOrthographicMatrix(0.0f, 0.0f, screenSize.x, screenSize.y, 0.0f, 1.0f);
}

void SpriteCommon::CommonSetting() {
	if (pipelineState_ == nullptr) {
		throw std::logic_error("SpriteCommon is not initialized");
	}
	auto* commandList = dxCommon_->GetCommandList();
	ID3D12DescriptorHeap* heaps[] = {dxCommon_->GetSRVDescriptorHeap()};
	commandList->SetDescriptorHeaps(1, heaps);
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpriteCommon::Finalize() {
	pipelineState_.Reset();
	rootSignature_.Reset();
	dxCommon_ = nullptr;
}

void SpriteCommon::rootSignature() {
	D3D12_DESCRIPTOR_RANGE textureRange{};
	textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange.NumDescriptors = 1;
	textureRange.BaseShaderRegister = 0;
	textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// PS:b0=色、VS:b0=変換行列、PS:t0=テクスチャ。
	D3D12_ROOT_PARAMETER parameters[3]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	parameters[1].Descriptor.ShaderRegister = 0;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	parameters[2].DescriptorTable.NumDescriptorRanges = 1;
	parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;

	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC description{};
	description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	description.NumParameters = _countof(parameters);
	description.pParameters = parameters;
	description.NumStaticSamplers = 1;
	description.pStaticSamplers = &sampler;
	Microsoft::WRL::ComPtr<ID3DBlob> signature;
	Microsoft::WRL::ComPtr<ID3DBlob> error;
	const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
		signature.GetAddressOf(), error.GetAddressOf());
	if (error != nullptr) {
		Logger::Log(static_cast<const char*>(error->GetBufferPointer()));
	}
	CheckResult(result, "SpriteCommon: serialize root signature failed");
	CheckResult(dxCommon_->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(),
		signature->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf())),
		"SpriteCommon: create root signature failed");
}

Microsoft::WRL::ComPtr<IDxcBlob> SpriteCommon::CompileShader(const wchar_t* path, const wchar_t* profile) {
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
	CheckResult(dxCommon_->GetDXCUtils()->LoadFile(path, nullptr, source.GetAddressOf()),
		"SpriteCommon: load shader failed");
	DxcBuffer buffer{source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8};
	const wchar_t* arguments[] = {path, L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Zpr"};
	Microsoft::WRL::ComPtr<IDxcResult> result;
	CheckResult(dxCommon_->GetDXCCompiler()->Compile(&buffer, arguments, _countof(arguments),
		dxCommon_->GetIncludeHandler(), IID_PPV_ARGS(result.GetAddressOf())),
		"SpriteCommon: invoke shader compiler failed");
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
	CheckResult(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr),
		"SpriteCommon: read shader diagnostics failed");
	if (errors != nullptr && errors->GetStringLength() != 0) {
		Logger::Log(errors->GetStringPointer());
	}
	HRESULT status = E_FAIL;
	CheckResult(result->GetStatus(&status), "SpriteCommon: read shader status failed");
	CheckResult(status, "SpriteCommon: shader compilation failed");
	Microsoft::WRL::ComPtr<IDxcBlob> shader;
	CheckResult(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shader.GetAddressOf()), nullptr),
		"SpriteCommon: get compiled shader failed");
	return shader;
}

void SpriteCommon::graphicsPipeline() {
	const auto vertexShader = CompileShader(L"resources/shaders/Sprite.VS.hlsl", L"vs_6_0");
	const auto pixelShader = CompileShader(L"resources/shaders/Sprite.PS.hlsl", L"ps_6_0");
	const D3D12_INPUT_ELEMENT_DESC elements[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
	description.pRootSignature = rootSignature_.Get();
	description.InputLayout = {elements, _countof(elements)};
	description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	// 通常のアルファ合成。奥行き判定は使わず、後から描いたスプライトを前面にする。
	auto& blend = description.BlendState.RenderTarget[0];
	blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blend.BlendEnable = TRUE;
	blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.BlendOp = D3D12_BLEND_OP_ADD;
	blend.SrcBlendAlpha = D3D12_BLEND_ONE;
	blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	description.RasterizerState.DepthClipEnable = TRUE;
	description.DepthStencilState.DepthEnable = FALSE;
	description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	description.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	description.NumRenderTargets = 1;
	description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	description.SampleDesc.Count = 1;
	CheckResult(dxCommon_->GetDevice()->CreateGraphicsPipelineState(&description,
		IID_PPV_ARGS(pipelineState_.GetAddressOf())), "SpriteCommon: create pipeline failed");
}
