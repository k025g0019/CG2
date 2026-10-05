#include "DirectXCommon.h"
#include <cassert>
#include <format>

#include "Logger.h"
#include "StringUtility.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

void DirectXCommon::Initialize(WinApp* winApp) {
	//NULL検出
	assert(winApp);
	//メンバ変数に記録
	this->winApp = winApp;

	deviceInitialize();
	commandInitialize();
	swapChain();
	depthBuffer();
	descriptorHeap();
	renderTargetView();
	depthStencilView();
	fenceInitialize();
	viewPortRectangleInitialize();
	scissoringRectangleInitialize();
	DXCcompiler();
	ImGuiInitialize();
}

void DirectXCommon::deviceInitialize() {
	HRESULT hr;
	//------------------------------
	// DirectXデバッグ初期化
	//------------------------------
#ifdef _DEBUG
	// DebugビルドではDirectXの不正なAPI利用を検出できるようにする。
	ComPtr<ID3D12Debug1> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		// GPU検証はd3d12SDKLayers内でスタックオーバーフローするため、現在は無効化している。
		// debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	ComPtr<IDXGIFactory7> dxgiFactory;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));


	ComPtr<IDXGIAdapter4> useAdapter;
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter;
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter;
		Logger::Log(logStream, std::format("Use Adapter:{}",
		                                   StringUtility::ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	ComPtr<ID3D12Device> device;
	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(device.GetAddressOf()));
	if (SUCCEEDED(hr)) {
		Logger::Log(logStream, "FeatureLevel:12.2");
	}
	else {
		hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
		if (SUCCEEDED(hr)) {
			Logger::Log(logStream, "FeatureLevel:12.1");
		}
		else {
			hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()));
			if (SUCCEEDED(hr)) {
				Logger::Log(logStream, "FeatureLevel:12.0");
			}
		}
	}
	assert(device != nullptr);
	Logger::Log(logStream, "Complete create D3D12Device!!!");
}

void DirectXCommon::commandInitialize() {
	//------------------------------
	// テクスチャ転送・SRV初期化
	//------------------------------
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	std::array<ComPtr<ID3D12Resource>, _countof(textureFilePaths)> intermediateResources{};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		intermediateResources[textureIndex].Attach(UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex].Get(), mipImages[textureIndex]));
	}


	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), srvDescriptorSize,
		                                                            textureIndex + 1);
		textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), srvDescriptorSize,
		                                                            textureIndex + 1);
		device->CreateShaderResourceView(
			textureResources[textureIndex].Get(), &srvDesc, textureSrvHandlesCPU[textureIndex]);
	}

	hr = commandList->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}
}

void DirectXCommon::swapChain() {
	//------------------------------
	// コマンド・スワップチェーン初期化
	//------------------------------
	ComPtr<ID3D12CommandQueue> commandQueue;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandQueue == nullptr) {
		return 1;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandAllocator == nullptr) {
		return 1;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandList == nullptr) {
		return 1;
	}
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	ComPtr<IDXGISwapChain4> swapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || swapChain == nullptr) {
		return 1;
	}
}

void DirectXCommon::depthBuffer() {
	//------------------------------
	// 描画先・深度バッファ初期化
	//------------------------------
	ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	rtvDescriptorHeap.Attach(CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false));
	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	srvDescriptorHeap.Attach(CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true));
	ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
	dsvDescriptorHeap.Attach(CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false));

	std::array<ComPtr<ID3D12Resource>, 2> swapChainResources{};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(swapChainResources[0].GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(swapChainResources[1].GetAddressOf()));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(
		rtvDescriptorHeap.Get(), device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 0);
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(
		rtvDescriptorHeap.Get(), device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 1);
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

	D3D12_RESOURCE_DESC depthStencilResourceDesc{};
	depthStencilResourceDesc.Width = kClientWidth;
	depthStencilResourceDesc.Height = kClientHeight;
	depthStencilResourceDesc.MipLevels = 1;
	depthStencilResourceDesc.DepthOrArraySize = 1;
	depthStencilResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilResourceDesc.SampleDesc.Count = 1;
	depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
	depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	ComPtr<ID3D12Resource> depthStencilResource;
	hr = device->CreateCommittedResource(
		&depthStencilHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilResourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(depthStencilResource.GetAddressOf()));
	assert(SUCCEEDED(hr));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandle);
};

void DirectXCommon::descriptorHeap() {
}
