#include "DirectXCommon.h"

#include "Logger.h"
#include "StringUtility.h"
#include "WinApp.h"

#include <cassert>
#include <format>
#include <string>
#include <thread>

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#pragma warning(pop)
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;


//========================================
// 初期化処理
//========================================

void DirectXCommon::Initialize(WinApp* winApp) {
	assert(winApp != nullptr);
	assert(!isInitialized_);

	//------------------------------
	// WinApp保持
	//------------------------------

	winApp_ = winApp;


	//------------------------------
	// DirectX基盤初期化
	//------------------------------

	InitializeFixFPS();
	InitializeDevice();
	InitializeCommand();
	InitializeSwapChain();
	InitializeDescriptorHeaps();
	InitializeRenderTargetViews();
	InitializeDepthBuffer();
	InitializeFence();
	InitializeViewport();
	InitializeDXCCompiler();
	InitializeImGui();

	isInitialized_ = true;
}


//========================================
// 終了処理
//========================================

void DirectXCommon::Finalize() {
	if (!isInitialized_) {
		return;
	}

	//------------------------------
	// GPU処理完了待機
	//------------------------------

	WaitForGPU();


	//------------------------------
	// ImGui終了
	//------------------------------

#ifdef USE_IMGUI
	if (isImGuiInitialized_) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		isImGuiInitialized_ = false;
	}
#endif


	//------------------------------
	// イベントハンドル解放
	//------------------------------

	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	// DirectXオブジェクトはComPtrが自動的に解放する。
	winApp_ = nullptr;
	isInitialized_ = false;
}


//========================================
// DirectX12デバイス初期化
//========================================

void DirectXCommon::InitializeDevice() {
	HRESULT hr = S_OK;


	//------------------------------
	// デバッグレイヤー初期化
	//------------------------------

#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController;

	if (SUCCEEDED(D3D12GetDebugInterface(
		IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
	}
#endif


	//------------------------------
	// DXGIファクトリー生成
	//------------------------------

	hr = CreateDXGIFactory1(
		IID_PPV_ARGS(dxgiFactory_.GetAddressOf()));
	assert(SUCCEEDED(hr));


	//------------------------------
	// 使用アダプター選択
	//------------------------------

	ComPtr<IDXGIAdapter4> useAdapter;

	for (uint32_t adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter;

		if (dxgiFactory_->EnumAdapterByGpuPreference(
				adapterIndex,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(candidateAdapter.GetAddressOf()))
			== DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};

		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアアダプターは描画用GPUとして使用しない。
		if ((adapterDesc.Flags &
			DXGI_ADAPTER_FLAG3_SOFTWARE) != 0) {
			continue;
		}

		useAdapter = candidateAdapter;

		Logger::Log(std::format(
			"Use Adapter:{}",
			StringUtility::ConvertString(
				std::wstring{adapterDesc.Description})));

		break;
	}

	assert(useAdapter != nullptr);


	//------------------------------
	// DirectX12デバイス生成
	//------------------------------

	constexpr D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
	};

	const char* featureLevelNames[] = {
		"12.2",
		"12.1",
		"12.0",
	};

	for (uint32_t index = 0;
	     index < _countof(featureLevels);
	     ++index) {
		hr = D3D12CreateDevice(
			useAdapter.Get(),
			featureLevels[index],
			IID_PPV_ARGS(device_.ReleaseAndGetAddressOf()));

		if (SUCCEEDED(hr)) {
			Logger::Log(std::format(
				"FeatureLevel:{}",
				featureLevelNames[index]));

			break;
		}
	}

	assert(device_ != nullptr);

	Logger::Log("Complete create D3D12Device");


	//------------------------------
	// DirectX12デバッグメッセージ設定
	//------------------------------

#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue;

	if (SUCCEEDED(device_->QueryInterface(
		IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_CORRUPTION,
			true);

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_ERROR,
			true);

		infoQueue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_WARNING,
			true);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		D3D12_MESSAGE_SEVERITY denySeverities[] = {
			D3D12_MESSAGE_SEVERITY_INFO,
		};

		D3D12_INFO_QUEUE_FILTER filter{};

		filter.DenyList.NumIDs =
			static_cast<UINT>(_countof(denyIds));

		filter.DenyList.pIDList = denyIds;

		filter.DenyList.NumSeverities =
			static_cast<UINT>(_countof(denySeverities));

		filter.DenyList.pSeverityList =
			denySeverities;

		infoQueue->PushStorageFilter(&filter);
	}
#endif
}


//========================================
// コマンド初期化
//========================================

void DirectXCommon::InitializeCommand() {
	HRESULT hr = S_OK;


	//------------------------------
	// コマンドキュー生成
	//------------------------------

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

	hr = device_->CreateCommandQueue(
		&commandQueueDesc,
		IID_PPV_ARGS(commandQueue_.GetAddressOf()));
	assert(SUCCEEDED(hr));


	//------------------------------
	// コマンドアロケータ生成
	//------------------------------

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(commandAllocator_.GetAddressOf()));
	assert(SUCCEEDED(hr));


	//------------------------------
	// コマンドリスト生成
	//------------------------------

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator_.Get(),
		nullptr,
		IID_PPV_ARGS(commandList_.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CreateCommandList直後は記録中なので、一度閉じておく。
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));
}


//========================================
// スワップチェーン初期化
//========================================

void DirectXCommon::InitializeSwapChain() {
	//------------------------------
	// スワップチェーン設定
	//------------------------------

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	swapChainDesc.Width =
		static_cast<UINT>(WinApp::kClientWidth);

	swapChainDesc.Height =
		static_cast<UINT>(WinApp::kClientHeight);

	swapChainDesc.Format =
		DXGI_FORMAT_R8G8B8A8_UNORM;

	swapChainDesc.SampleDesc.Count = 1;

	swapChainDesc.BufferUsage =
		DXGI_USAGE_RENDER_TARGET_OUTPUT;

	swapChainDesc.BufferCount =
		kSwapChainBufferCount;

	swapChainDesc.SwapEffect =
		DXGI_SWAP_EFFECT_FLIP_DISCARD;


	//------------------------------
	// スワップチェーン生成
	//------------------------------

	ComPtr<IDXGISwapChain1> swapChain1;

	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),
		winApp_->GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		swapChain1.GetAddressOf());

	assert(SUCCEEDED(hr));

	hr = swapChain1.As(&swapChain_);
	assert(SUCCEEDED(hr));
}


//========================================
// ディスクリプタヒープ初期化
//========================================

void DirectXCommon::InitializeDescriptorHeaps() {
	//------------------------------
	// ディスクリプタサイズ取得
	//------------------------------

	rtvDescriptorSize_ =
		device_->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	srvDescriptorSize_ =
		device_->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	dsvDescriptorSize_ =
		device_->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV);


	//------------------------------
	// ディスクリプタヒープ生成
	//------------------------------

	rtvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		kSwapChainBufferCount,
		false);

	srvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		kMaxSRVCount,
		true);

	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		1,
		false);
}


//========================================
// レンダーターゲット初期化
//========================================

void DirectXCommon::InitializeRenderTargetViews() {
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	rtvDesc.Format =
		DXGI_FORMAT_R8G8B8A8_UNORM;

	rtvDesc.ViewDimension =
		D3D12_RTV_DIMENSION_TEXTURE2D;

	for (uint32_t index = 0;
	     index < kSwapChainBufferCount;
	     ++index) {
		HRESULT hr = swapChain_->GetBuffer(
			index,
			IID_PPV_ARGS(
				swapChainResources_[index].GetAddressOf()));

		assert(SUCCEEDED(hr));

		rtvHandles_[index] = GetCPUDescriptorHandle(
			rtvDescriptorHeap_.Get(),
			rtvDescriptorSize_,
			index);

		device_->CreateRenderTargetView(
			swapChainResources_[index].Get(),
			&rtvDesc,
			rtvHandles_[index]);
	}
}


//========================================
// 深度バッファ初期化
//========================================

void DirectXCommon::InitializeDepthBuffer() {
	//------------------------------
	// 深度バッファ設定
	//------------------------------

	D3D12_RESOURCE_DESC resourceDesc{};

	resourceDesc.Width =
		static_cast<UINT64>(WinApp::kClientWidth);

	resourceDesc.Height =
		static_cast<UINT>(WinApp::kClientHeight);

	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;

	resourceDesc.Format =
		DXGI_FORMAT_D24_UNORM_S8_UINT;

	resourceDesc.SampleDesc.Count = 1;

	resourceDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	resourceDesc.Flags =
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


	//------------------------------
	// ヒープ設定
	//------------------------------

	D3D12_HEAP_PROPERTIES heapProperties{};

	heapProperties.Type =
		D3D12_HEAP_TYPE_DEFAULT;


	//------------------------------
	// 深度クリア値
	//------------------------------

	D3D12_CLEAR_VALUE clearValue{};

	clearValue.Format =
		DXGI_FORMAT_D24_UNORM_S8_UINT;

	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;


	//------------------------------
	// 深度バッファ生成
	//------------------------------

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(
			depthStencilResource_.GetAddressOf()));

	assert(SUCCEEDED(hr));


	//------------------------------
	// DSV生成
	//------------------------------

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

	dsvDesc.Format =
		DXGI_FORMAT_D24_UNORM_S8_UINT;

	dsvDesc.ViewDimension =
		D3D12_DSV_DIMENSION_TEXTURE2D;

	dsvHandle_ = GetCPUDescriptorHandle(
		dsvDescriptorHeap_.Get(),
		dsvDescriptorSize_,
		0);

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvHandle_);
}


//========================================
// フェンス初期化
//========================================

void DirectXCommon::InitializeFence() {
	fenceValue_ = 0;

	HRESULT hr = device_->CreateFence(
		fenceValue_,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(fence_.GetAddressOf()));

	assert(SUCCEEDED(hr));

	fenceEvent_ = CreateEvent(
		nullptr,
		FALSE,
		FALSE,
		nullptr);

	assert(fenceEvent_ != nullptr);
}


//========================================
// Viewport・Scissor初期化
//========================================

void DirectXCommon::InitializeViewport() {
	//------------------------------
	// Viewport設定
	//------------------------------

	viewport_.Width =
		static_cast<float>(WinApp::kClientWidth);

	viewport_.Height =
		static_cast<float>(WinApp::kClientHeight);

	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;


	//------------------------------
	// Scissor設定
	//------------------------------

	scissorRect_.left = 0;
	scissorRect_.top = 0;
	scissorRect_.right =
		WinApp::kClientWidth;

	scissorRect_.bottom =
		WinApp::kClientHeight;
}


//========================================
// DXCコンパイラ初期化
//========================================

void DirectXCommon::InitializeDXCCompiler() {
	HRESULT hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(dxcUtils_.GetAddressOf()));

	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(dxcCompiler_.GetAddressOf()));

	assert(SUCCEEDED(hr));

	hr = dxcUtils_->CreateDefaultIncludeHandler(
		includeHandler_.GetAddressOf());

	assert(SUCCEEDED(hr));
}


//========================================
// ImGui初期化
//========================================

void DirectXCommon::InitializeImGui() {
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();

	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	const bool win32Result =
		ImGui_ImplWin32_Init(winApp_->GetHwnd());

	assert(win32Result);

	const bool dx12Result =
		ImGui_ImplDX12_Init(
			device_.Get(),
			static_cast<int>(kSwapChainBufferCount),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			srvDescriptorHeap_.Get(),
			GetSRVCPUDescriptorHandle(0),
			GetSRVGPUDescriptorHandle(0));

	assert(dx12Result);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

	isImGuiInitialized_ = true;
#endif
}


//========================================
// 描画前処理
//========================================

void DirectXCommon::PreDraw() {
	assert(isInitialized_);

	//------------------------------
	// コマンド記録開始
	//------------------------------

	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList_->Reset(
		commandAllocator_.Get(),
		nullptr);

	assert(SUCCEEDED(hr));


	//------------------------------
	// バックバッファを描画先へ変更
	//------------------------------

	const uint32_t backBufferIndex =
		swapChain_->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};

	barrier.Type =
		D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

	barrier.Flags =
		D3D12_RESOURCE_BARRIER_FLAG_NONE;

	barrier.Transition.pResource =
		swapChainResources_[backBufferIndex].Get();

	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	barrier.Transition.StateBefore =
		D3D12_RESOURCE_STATE_PRESENT;

	barrier.Transition.StateAfter =
		D3D12_RESOURCE_STATE_RENDER_TARGET;

	commandList_->ResourceBarrier(
		1,
		&barrier);


	//------------------------------
	// 描画範囲設定
	//------------------------------

	commandList_->RSSetViewports(
		1,
		&viewport_);

	commandList_->RSSetScissorRects(
		1,
		&scissorRect_);


	//------------------------------
	// ディスクリプタヒープ設定
	//------------------------------

	ID3D12DescriptorHeap* descriptorHeaps[] = {
		srvDescriptorHeap_.Get(),
	};

	commandList_->SetDescriptorHeaps(
		1,
		descriptorHeaps);


	//------------------------------
	// 描画先設定
	//------------------------------

	commandList_->OMSetRenderTargets(
		1,
		&rtvHandles_[backBufferIndex],
		FALSE,
		&dsvHandle_);


	//------------------------------
	// 描画先クリア
	//------------------------------

	constexpr float clearColor[] = {
		0.1f,
		0.25f,
		0.5f,
		1.0f,
	};

	commandList_->ClearRenderTargetView(
		rtvHandles_[backBufferIndex],
		clearColor,
		0,
		nullptr);

	commandList_->ClearDepthStencilView(
		dsvHandle_,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr);
}


//========================================
// 描画後処理
//========================================

void DirectXCommon::PostDraw() {
	assert(isInitialized_);

	//------------------------------
	// バックバッファを表示用へ戻す
	//------------------------------

	const uint32_t backBufferIndex =
		swapChain_->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};

	barrier.Type =
		D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

	barrier.Flags =
		D3D12_RESOURCE_BARRIER_FLAG_NONE;

	barrier.Transition.pResource =
		swapChainResources_[backBufferIndex].Get();

	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	barrier.Transition.StateBefore =
		D3D12_RESOURCE_STATE_RENDER_TARGET;

	barrier.Transition.StateAfter =
		D3D12_RESOURCE_STATE_PRESENT;

	commandList_->ResourceBarrier(
		1,
		&barrier);


	//------------------------------
	// コマンド記録終了
	//------------------------------

	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));


	//------------------------------
	// コマンド実行
	//------------------------------

	ID3D12CommandList* commandLists[] = {
		commandList_.Get(),
	};

	commandQueue_->ExecuteCommandLists(
		1,
		commandLists);


	//------------------------------
	// 画面表示
	//------------------------------

	hr = swapChain_->Present(1, 0);
	assert(SUCCEEDED(hr));


	//------------------------------
	// GPU完了待機
	//------------------------------

	WaitForGPU();


	//------------------------------
	// FPS固定
	//------------------------------

	UpdateFixFPS();
}


//========================================
// GPU同期処理
//========================================

void DirectXCommon::WaitForGPU() {
	assert(commandQueue_ != nullptr);
	assert(fence_ != nullptr);
	assert(fenceEvent_ != nullptr);

	++fenceValue_;

	HRESULT hr = commandQueue_->Signal(
		fence_.Get(),
		fenceValue_);

	assert(SUCCEEDED(hr));

	if (fence_->GetCompletedValue() < fenceValue_) {
		hr = fence_->SetEventOnCompletion(
			fenceValue_,
			fenceEvent_);

		assert(SUCCEEDED(hr));

		WaitForSingleObject(
			fenceEvent_,
			INFINITE);
	}
}


//========================================
// ディスクリプタヒープ生成処理
//========================================

ComPtr<ID3D12DescriptorHeap>
DirectXCommon::CreateDescriptorHeap(
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	uint32_t numDescriptors,
	bool shaderVisible) {
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};

	descriptorHeapDesc.Type = heapType;

	descriptorHeapDesc.NumDescriptors =
		numDescriptors;

	descriptorHeapDesc.Flags =
		shaderVisible
			? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
			: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc,
		IID_PPV_ARGS(
			descriptorHeap.GetAddressOf()));

	assert(SUCCEEDED(hr));

	return descriptorHeap;
}


//========================================
// SRVハンドル取得処理
//========================================

D3D12_CPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSRVCPUDescriptorHandle(
	uint32_t index) const {
	return GetCPUDescriptorHandle(
		srvDescriptorHeap_.Get(),
		srvDescriptorSize_,
		index);
}


D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSRVGPUDescriptorHandle(
	uint32_t index) const {
	return GetGPUDescriptorHandle(
		srvDescriptorHeap_.Get(),
		srvDescriptorSize_,
		index);
}


//========================================
// ディスクリプタハンドル計算処理
//========================================

D3D12_CPU_DESCRIPTOR_HANDLE
DirectXCommon::GetCPUDescriptorHandle(
	ID3D12DescriptorHeap* descriptorHeap,
	uint32_t descriptorSize,
	uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	handle.ptr +=
		static_cast<SIZE_T>(descriptorSize) * index;

	return handle;
}


D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetGPUDescriptorHandle(
	ID3D12DescriptorHeap* descriptorHeap,
	uint32_t descriptorSize,
	uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handle =
		descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	handle.ptr +=
		static_cast<UINT64>(descriptorSize) * index;

	return handle;
}


//========================================
// FPS固定初期化
//========================================

void DirectXCommon::InitializeFixFPS() {
	referenceTime_ =
		std::chrono::steady_clock::now();
}


//========================================
// FPS固定更新
//========================================

void DirectXCommon::UpdateFixFPS() {
	using namespace std::chrono;

	constexpr microseconds kMinTime(
		1'000'000 / 60);

	constexpr microseconds kMinCheckTime(
		1'000'000 / 65);

	auto elapsed =
		duration_cast<microseconds>(
			steady_clock::now() - referenceTime_);

	// 処理が十分に速かった場合だけ、60FPS相当まで待機する。
	if (elapsed < kMinCheckTime) {
		while (steady_clock::now() - referenceTime_
			< kMinTime) {
			std::this_thread::sleep_for(
				microseconds(1));
		}
	}

	referenceTime_ =
		steady_clock::now();
}

//========================================
// コマンド記録開始処理
//========================================

void DirectXCommon::ResetCommandList() {
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList_->Reset(
		commandAllocator_.Get(),
		nullptr);

	assert(SUCCEEDED(hr));
}


//========================================
// コマンド実行・完了待機処理
//========================================

void DirectXCommon::ExecuteCommandListAndWait() {
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = {
		commandList_.Get(),
	};

	commandQueue_->ExecuteCommandLists(
		1,
		commandLists);

	WaitForGPU();
}
