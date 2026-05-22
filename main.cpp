#pragma warning(push, 0)
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <ctime>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include "externals/DirectXTex/DirectXTex.h"
#pragma warning(push)
#pragma warning(disable : 5045)
#include "externals/DirectXTex/d3dx12.h"
#pragma warning(pop)
#include <filesystem>
#include <format>
#include <fstream>
#include <sdkddkver.h>
#include <string>
#include <vector>
#pragma warning(pop)

#include "ApplicationWindow.h"
#include "CrashHandler.h"
#include "Imgui.h"
#include "Log.h"
#include "Matrix.h"
#include "StringUtility.h"
#include "Vector&Matrix.h"
#include "Vector.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")


#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#pragma warning(pop)
#endif

namespace {
	struct Vector4 {
		float x;
		float y;
		float z;
		float w;
	};

	struct Vector2 {
		float x;
		float y;
	};

	struct Transforms {
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
	};

	struct Material {
		Vector4 color;
	};

	struct Sprite {
		Vector2 position;
		Vector2 size;
	};

	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);

	DirectX::ScratchImage LoadTexture(const std::wstring& filePath) {
		DirectX::TexMetadata metadata{};
		DirectX::ScratchImage image{};
		HRESULT hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, &metadata, image);
		assert(SUCCEEDED(hr));

		DirectX::ScratchImage mipImages{};
		hr = DirectX::GenerateMipMaps(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			DirectX::TEX_FILTER_SRGB,
			0,
			mipImages);
		assert(SUCCEEDED(hr));

		return mipImages;
	}

	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = static_cast<UINT>(metadata.width);
		resourceDesc.Height = static_cast<UINT>(metadata.height);
		resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
		resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
		resourceDesc.Format = metadata.format;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));

		return resource;
	}

	ID3D12Resource* UploadTextureData(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages) {
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		subresources.reserve(mipImages.GetImageCount());
		const DirectX::Image* images = mipImages.GetImages();
		for (size_t index = 0; index < mipImages.GetImageCount(); ++index) {
			D3D12_SUBRESOURCE_DATA subresource{};
			subresource.pData = images[index].pixels;
			subresource.RowPitch = static_cast<LONG_PTR>(images[index].rowPitch);
			subresource.SlicePitch = static_cast<LONG_PTR>(images[index].slicePitch);
			subresources.push_back(subresource);
		}

		UINT64 intermediateSize = GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subresources.size()));
		ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);

		UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0,
			0,
			static_cast<UINT>(subresources.size()),
			subresources.data());

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		commandList->ResourceBarrier(1, &barrier);

		return intermediateResource;
	}

	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = sizeInBytes;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));

		return resource;
	}

	IDxcBlob* CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ofstream& logStream) {
		Log(logStream, std::format("Begin CompileShader, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		IDxcBlobEncoding* shaderSource = nullptr;
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
		assert(SUCCEEDED(hr));

		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		LPCWSTR arguments[] = {
			filePath.c_str(),
			L"-E", L"main",
			L"-T", profile,
			L"-Zi", L"-Qembed_debug",
			L"-Od",
			L"-Zpr"
		};

		IDxcResult* shaderResult = nullptr;
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments,
			_countof(arguments),
			includeHandler,
			IID_PPV_ARGS(&shaderResult));
		assert(SUCCEEDED(hr));


		IDxcBlobUtf8* shaderError = nullptr;
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(shaderError->GetStringPointer());
			assert(false);
		}

		IDxcBlob* shaderBlob = nullptr;
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
		assert(SUCCEEDED(hr));

		Log(logStream, std::format("Compile Succeeded, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		if (shaderError != nullptr) {
			shaderError->Release();
		}
		shaderResult->Release();
		shaderSource->Release();

		return shaderBlob;
	}
}

#pragma warning(push)
#pragma warning(disable : 5045)
// Windows アプリケーションのエントリーポイント
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	InstallCrashHandler();

	// 実行ログを保存するために logs フォルダとログファイルを作成する
	std::filesystem::create_directory("logs");
	std::time_t now = std::time(nullptr);
	std::tm localTime{};
	localtime_s(&localTime, &now);
	std::string dateString = std::format(
		"{:04}{:02}{:02}_{:02}{:02}{:02}",
		localTime.tm_year + 1900,
		localTime.tm_mon + 1,
		localTime.tm_mday,
		localTime.tm_hour,
		localTime.tm_min,
		localTime.tm_sec);
	auto logFilePath = std::string("logs/" + dateString + ".Log");
	std::ofstream logStream(logFilePath);
	if (!logStream) {
		return 1;
	}

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	if (windowHandle == nullptr) {
		return 1;
	}

#ifdef _DEBUG
	// DebugLayer は DX12 初期化より前に有効化する必要がある
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// CPU 側で不正な API 呼び出しを検出しやすくする
		debugController->EnableDebugLayer();
		// GPU 実行時の不正も拾いやすくする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	MSG message{};
	Log(logStream, "main loop started");


	// DXGI Factory を生成する
	IDXGIFactory7* dxgiFactory = nullptr;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	// 利用する GPU アダプターを高性能優先で 1 つ選択する
	IDXGIAdapter4* useAdapter = nullptr;
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		IDXGIAdapter4* candidateAdapter = nullptr;
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&candidateAdapter)) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			candidateAdapter->Release();
			continue;
		}

		useAdapter = candidateAdapter;
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	// Direct3D 12 のデバイスを生成する
	ID3D12Device* device = nullptr;
	hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
	if (SUCCEEDED(hr)) {
		Log(logStream, "FeatureLevel:12.2");
	}
	else {
		hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device));
		if (SUCCEEDED(hr)) {
			Log(logStream, "FeatureLevel:12.1");
		}
		else {
			hr = D3D12CreateDevice(useAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
			if (SUCCEEDED(hr)) {
				Log(logStream, "FeatureLevel:12.0");
			}
		}
	}
	assert(device != nullptr);
	Log(logStream, "Complete create D3D12Device!!!");

#ifdef _DEBUG
	// Device 作成直後に InfoQueue を取得し、重大な問題で停止するようにする
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// GPU が壊れる可能性のある致命的エラーは即停止する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// API の使い方を間違えたエラーも即停止する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// Warning も見逃さないように停止対象にする
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// Windows 側の既知ノイズと、確認不要な INFO メッセージは抑制する
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);

		infoQueue->Release();
	}
#endif

	// 描画コマンドを GPU へ送るためのコマンドキューを作成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandQueue == nullptr) {
		return 1;
	}

	// コマンドリストが使用するメモリ管理用のアロケータを作成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandAllocator == nullptr) {
		return 1;
	}

	// 実際の描画命令を記録するコマンドリストを作成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandList == nullptr) {
		return 1;
	}
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	// 画面表示に使うスワップチェーンを作成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue, windowHandle, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || swapChain == nullptr) {
		return 1;
	}

	// RTV 用ヒープと SRV 用ヒープを作成する
	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// スワップチェーンのバックバッファごとに RTV を作成する
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = rtvHandle;
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1].ptr =
		rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	// HLSL コンパイルに使う DXC 関連オブジェクトを初期化する
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	// 頂点シェーダーとピクセルシェーダーをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler,
	                                           logStream);
	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler,
	                                          logStream);

	// RootSignature を組み立てる
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[1].Descriptor.RegisterSpace = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = &staticSampler;
	descriptionRootSignature.NumStaticSamplers = 1;

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	// マテリアル定数バッファには三角形に掛ける色を入れる
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));
	Material* materialData = nullptr;


	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};


	// WVP 定数バッファには座標変換行列を書き込む
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(Matrix4x4));
	Matrix4x4* wvpData = nullptr;
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	*wvpData = MakeIdentity4x4();

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	// 頂点レイアウトを GPU に伝える InputLayout を設定する
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	// 描画結果をそのまま書き込む BlendState を設定する
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 裏表の扱いや塗りつぶし方法を決める RasterizerState を設定する
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	// ここまでの設定をまとめて PSO を作成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature;
	graphicsPipelineStateDesc.InputLayout.pInputElementDescs = inputElementDescs;
	graphicsPipelineStateDesc.InputLayout.NumElements = _countof(inputElementDescs);
	graphicsPipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		return 1;
	}

	// スプライト 1 枚を 2 枚の三角形で描画するため、6 頂点を用意する
	VertexData vertices[6] = {
		{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
		{{640.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
		{{0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
		{{0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
		{{640.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
		{{640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}
	};

	// スプライトの表示位置とサイズ
	Sprite sprite{
		.position = {100.0f, 50.0f},
		.size = {640.0f, 360.0f}
	};

	// スプライトは頂点をローカル座標で持ち、translate で画面上へ移動する
	Transforms transform{
		.scale = {sprite.size.x / 640.0f, sprite.size.y / 360.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {sprite.position.x, sprite.position.y, 0.0f}
	};
	// 頂点バッファ用のリソースを作成する
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(vertices));

	// CPU 側から頂点データを書き込めるようにマップする
	VertexData* mappedVertexData = nullptr;
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedVertexData, vertices, sizeof(vertices));

	// 頂点バッファの見え方を GPU に伝えるための View を設定する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = sizeof(vertices);
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// 画面全体を描画対象にする Viewport を設定する
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kClientWidth);
	viewport.Height = static_cast<float>(kClientHeight);
	viewport.MaxDepth = 1.0f;

	// Viewport と同じ範囲を描画する ScissorRect を設定する
	D3D12_RECT scissorRect{};
	scissorRect.right = kClientWidth;
	scissorRect.bottom = kClientHeight;

	// テクスチャ画像を読み込み、GPU リソースを作成する
	DirectX::ScratchImage mipImages = LoadTexture(L"resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);

	// スプライトは画面座標系で扱いたいので正射影行列を使う
	Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		static_cast<float>(kClientWidth),
		static_cast<float>(kClientHeight),
		0.0f,
		100.0f);

	// GPU 完了待ちに使う Fence とイベントを作成する
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);
	if (fenceEvent == nullptr) {
		return 1;
	}

	// テクスチャを GPU へ転送するための一時コマンドを記録する
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator, nullptr);
	assert(SUCCEEDED(hr));

	ID3D12Resource* intermediateResource = UploadTextureData(device, commandList, textureResource, mipImages);

	// SRV ヒープの 0 番は ImGui が使うため、1 番にテクスチャ SRV を配置する
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	textureSrvHandleCPU.ptr += srvDescriptorSize;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	textureSrvHandleGPU.ptr += srvDescriptorSize;
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

	hr = commandList->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* uploadCommandLists[] = {commandList};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++;
	hr = commandQueue->Signal(fence, fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

#ifdef USE_IMGUI
	// ImGui はメインループへ入る前に初期化しておく
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowHandle);
	ImGui_ImplDX12_Init(
		device,
		static_cast<int>(swapChainDesc.BufferCount),
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif
	while (message.message != WM_QUIT) {
		if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		else {
#ifdef USE_IMGUI
			// ImGui に新しいフレームの開始を通知し、デモウィンドウを描画対象へ追加する
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::ShowDemoWindow();
			ImGui::Render();
#endif

			// スプライト用のワールド行列と正射影行列を掛け合わせて WVP を作る
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, projectionMatrix);
			*wvpData = worldViewProjectionMatrix;
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, graphicsPipelineState);
			assert(SUCCEEDED(hr));

			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			// Present 状態のバックバッファをレンダーターゲットとして使える状態へ遷移する
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			commandList->ResourceBarrier(1, &barrier);

			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);

			// 描画に必要な状態を順にコマンドリストへ設定する
			commandList->SetGraphicsRootSignature(rootSignature);
			commandList->SetPipelineState(graphicsPipelineState);
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, nullptr);

			// まずは画面を青系の色でクリアしてからスプライトを描画する
			float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			commandList->DrawInstanced(6, 1, 0, 0);
#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			ID3D12CommandList* commandLists[] = {commandList};
			commandQueue->ExecuteCommandLists(1, commandLists);

			hr = swapChain->Present(1, 0);
			assert(SUCCEEDED(hr));

			fenceValue++;
			hr = commandQueue->Signal(fence, fenceValue);
			assert(SUCCEEDED(hr));

			if (fence->GetCompletedValue() < fenceValue) {
				hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
				assert(SUCCEEDED(hr));
				if (fenceEvent != nullptr) {
					WaitForSingleObject(fenceEvent, INFINITE);
				}
			}
		}
	}

	Log(logStream, "application finished");

#ifdef USE_IMGUI
	// ImGui は初期化と逆順で終了処理を行う
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// 生成順を意識して後始末すると、解放漏れを追いやすい
	if (fenceEvent != nullptr) {
		CloseHandle(fenceEvent);
	}

	// 作成した DirectX 関連オブジェクトを最後にすべて解放する
	fence->Release();
	intermediateResource->Release();
	textureResource->Release();
	materialResource->Release();
	vertexResource->Release();
	graphicsPipelineState->Release();
	rootSignature->Release();
	signatureBlob->Release();
	if (errorBlob != nullptr) {
		errorBlob->Release();
	}
	vertexShaderBlob->Release();
	pixelShaderBlob->Release();
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();
	srvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();

#ifdef _DEBUG
	// すべて解放したあとに、残っている DX12 オブジェクトがないかを確認する
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

	if (debugController != nullptr) {
		debugController->Release();
	}
#endif

	return static_cast<int>(message.wParam);
}
#pragma warning(pop)
