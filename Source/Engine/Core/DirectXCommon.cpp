#include "DirectXCommon.h"

#include "EditorHrCheck.h"
#include "EditorProfilerManager.h"
#include "Imgui.h"
#include "Log.h"
#include "StringUtility.h"
#include "WinApp.h"

#include <cassert>
#include <format>

using Microsoft::WRL::ComPtr;

namespace {
	//----------------------------------------
	// Descriptor Heap の容量
	//----------------------------------------

	// RTV は SwapChain 2 枚に加えて HDR / Bloom / PostProcess / SSAO / Composite /
	// Mask / Planar / OIT / SSGI が使う。SRV は Texture と各種中間 Buffer を同じ Heap へ並べる。
	constexpr uint32_t kRtvDescriptorCount = 17u;
	constexpr uint32_t kSrvDescriptorCount = 65536u;
	constexpr uint32_t kDsvDescriptorCount = 2u;
}  // namespace

//========================================
// 初期化処理
//========================================

bool DirectXCommon::Initialize(WinApp* winApp, std::ostream& logStream) {
	if (winApp == nullptr || !winApp->HasWindow()) {
		return false;  // SwapChain の生成に Window Handle が要る
	}

	if (!CreateDeviceAndCommands(logStream)) {
		return false;
	}

	if (!CreateTimestampResources(logStream)) {
		return false;
	}

	if (!CreateSwapChainAndHeaps(winApp, logStream)) {
		return false;
	}

	if (!CreateBackBufferViews()) {
		return false;
	}

	return CreateFenceObjects();
}

bool DirectXCommon::CreateDeviceAndCommands(std::ostream& logStream) {
	//----------------------------------------
	// DXGI Factory と Adapter
	//----------------------------------------

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || dxgiFactory_ == nullptr) {
		return false;
	}

	// 高性能 GPU から順に見て、Software Adapter は飛ばす。
	for (UINT adapterIndex = 0u;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter;

		if (dxgiFactory_->EnumAdapterByGpuPreference(
				adapterIndex,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		EDITOR_HR_VERIFY(hr);

		if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0) {
			continue;
		}

		useAdapter_ = candidateAdapter;  // 最初に見つかった物理 GPU を採用する
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}

	if (useAdapter_ == nullptr) {
		Log(logStream, "no hardware adapter found");
		return false;
	}

	//----------------------------------------
	// Device
	//----------------------------------------

	// 新しい Feature Level から順に試し、通ったところで確定する。
	const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
	};
	const char* featureLevelNames[] = {"FeatureLevel:12.2", "FeatureLevel:12.1", "FeatureLevel:12.0"};

	for (size_t levelIndex = 0u; levelIndex < _countof(featureLevels); ++levelIndex) {
		hr = D3D12CreateDevice(useAdapter_.Get(), featureLevels[levelIndex], IID_PPV_ARGS(device_.GetAddressOf()));

		if (SUCCEEDED(hr)) {
			Log(logStream, featureLevelNames[levelIndex]);
			break;
		}
	}

	if (device_ == nullptr) {
		Log(logStream, "D3D12CreateDevice failed for every feature level");
		return false;
	}

	Log(logStream, "Complete create D3D12Device!!!");

#ifdef _DEBUG
	//----------------------------------------
	// Debug Layer の Message 設定
	//----------------------------------------

	ComPtr<ID3D12InfoQueue> infoQueue;

	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		// Debug Layer の Message は出力ウィンドウへ残しつつ、0x0000087A で止めない。
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

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
	}
#endif

	//----------------------------------------
	// Command Queue / Allocator / List
	//----------------------------------------

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandQueue_ == nullptr) {
		return false;
	}

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(commandAllocator_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandAllocator_ == nullptr) {
		return false;
	}

	hr = device_->CreateCommandList(
		0u,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator_.Get(),
		nullptr,
		IID_PPV_ARGS(commandList_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandList_ == nullptr) {
		return false;
	}

	// 作った直後は記録中の状態なので、いったん閉じて BeginFrame の Reset と対にする。
	hr = commandList_->Close();
	EDITOR_HR_VERIFY(hr);
	return SUCCEEDED(hr);
}

bool DirectXCommon::CreateTimestampResources(std::ostream& logStream) {
	//----------------------------------------
	// GPU 時間計測用の QueryHeap と Readback
	//----------------------------------------

	D3D12_QUERY_HEAP_DESC queryHeapDesc{};
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.Count = kEditorProfilerTimestampQueryCapacity;
	HRESULT hr = device_->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(renderTimestampQueryHeap_.GetAddressOf()));

	if (SUCCEEDED(hr)) {
		// Timestamp の目盛りを秒へ直すための分母。Queue ごとに違う。
		hr = commandQueue_->GetTimestampFrequency(&renderTimestampFrequency_);
	}

	D3D12_HEAP_PROPERTIES readbackHeapProperties{};
	readbackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;  // GPU が書いて CPU が読む

	D3D12_RESOURCE_DESC readbackDesc{};
	readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readbackDesc.Width = sizeof(uint64_t) * static_cast<uint64_t>(kEditorProfilerTimestampQueryCapacity);
	readbackDesc.Height = 1u;
	readbackDesc.DepthOrArraySize = 1u;
	readbackDesc.MipLevels = 1u;
	readbackDesc.SampleDesc.Count = 1u;
	readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (SUCCEEDED(hr)) {
		hr = device_->CreateCommittedResource(
			&readbackHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&readbackDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(renderTimestampReadback_.GetAddressOf()));
	}

	if (FAILED(hr) || !HasProfilerResources()) {
		Log(logStream, std::format(
			"Render profiler resource creation failed. hr=0x{:08X}",
			static_cast<uint32_t>(hr)));
		return false;
	}

	return true;
}

bool DirectXCommon::CreateSwapChainAndHeaps(WinApp* winApp, std::ostream& logStream) {
	//----------------------------------------
	// SwapChain
	//----------------------------------------

	uint32_t renderWidth = 1u;
	uint32_t renderHeight = 1u;
	winApp->GetClientSize(renderWidth, renderHeight);

	swapChainDesc_ = DXGI_SWAP_CHAIN_DESC1{};
	swapChainDesc_.Width = renderWidth;
	swapChainDesc_.Height = renderHeight;
	swapChainDesc_.Format = kBackBufferFormat;
	swapChainDesc_.SampleDesc.Count = 1u;
	swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc_.BufferCount = kBackBufferCount;
	// FLIP_DISCARD は表示済みの中身を捨てる代わりにコピーを挟まない。
	swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),
		winApp->GetHwnd(),
		&swapChainDesc_,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || swapChain_ == nullptr) {
		Log(logStream, "CreateSwapChainForHwnd failed");
		return false;
	}

	//----------------------------------------
	// Descriptor Heap 3 本
	//----------------------------------------

	// SRV だけ Shader から見える必要がある。RTV と DSV は CPU 側の書き込み先。
	rtvDescriptorHeap_.Attach(
		CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvDescriptorCount, false));
	srvDescriptorHeap_.Attach(
		CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvDescriptorCount, true));
	dsvDescriptorHeap_.Attach(
		CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvDescriptorCount, false));

	return rtvDescriptorHeap_ != nullptr && srvDescriptorHeap_ != nullptr && dsvDescriptorHeap_ != nullptr;
}

bool DirectXCommon::CreateBackBufferViews() {
	//----------------------------------------
	// Back Buffer と その RTV
	//----------------------------------------

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = kBackBufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	const UINT rtvDescriptorSize =
		device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapStart =
		rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	for (uint32_t bufferIndex = 0u; bufferIndex < kBackBufferCount; ++bufferIndex) {
		backBuffers_[bufferIndex].Reset();
		const HRESULT hr = swapChain_->GetBuffer(
			bufferIndex,
			IID_PPV_ARGS(backBuffers_[bufferIndex].GetAddressOf()));
		EDITOR_HR_VERIFY(hr);

		if (FAILED(hr) || backBuffers_[bufferIndex] == nullptr) {
			return false;
		}

		// RTV Heap の先頭から Back Buffer の枚数分を、順番に割り当てる。
		backBufferRtvHandles_[bufferIndex].ptr =
			rtvHeapStart.ptr + static_cast<SIZE_T>(rtvDescriptorSize) * bufferIndex;
		device_->CreateRenderTargetView(
			backBuffers_[bufferIndex].Get(),
			&rtvDesc,
			backBufferRtvHandles_[bufferIndex]);
	}

	return true;
}

bool DirectXCommon::CreateFenceObjects() {
	//----------------------------------------
	// Fence と待機 Event
	//----------------------------------------

	fenceValue_ = 0u;
	const HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || fence_ == nullptr) {
		return false;
	}

	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	return fenceEvent_ != nullptr;
}

//========================================
// 毎フレームの描画の前処理
//========================================

bool DirectXCommon::BeginFrame(ID3D12PipelineState* initialPipelineState) {
	if (commandAllocator_ == nullptr || commandList_ == nullptr) {
		return false;
	}

	// Allocator を巻き戻してから List を巻き戻す。順番が逆だと、まだ GPU が読んでいる
	// 命令の置き場を先に消してしまう。
	HRESULT hr = commandAllocator_->Reset();

	if (FAILED(hr)) {
		return false;
	}

	hr = commandList_->Reset(commandAllocator_.Get(), initialPipelineState);
	return SUCCEEDED(hr);
}

void DirectXCommon::BeginBackBufferPass() {
	currentBackBufferIndex_ = GetCurrentBackBufferIndex();

	ID3D12Resource* backBuffer = GetBackBuffer(currentBackBufferIndex_);

	if (backBuffer == nullptr || commandList_ == nullptr) {
		return;
	}

	// Present 用に置かれている Back Buffer を、描き込める状態へ移す。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1u, &barrier);
}

void DirectXCommon::EndBackBufferPass() {
	ID3D12Resource* backBuffer = GetBackBuffer(currentBackBufferIndex_);

	if (backBuffer == nullptr || commandList_ == nullptr) {
		return;
	}

	// 描き終えたので Present できる状態へ戻す。ここを忘れると Present が失敗する。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList_->ResourceBarrier(1u, &barrier);
}

//========================================
// 毎フレームの描画の後処理
//========================================

bool DirectXCommon::SubmitCommandList(std::ostream& logStream) {
	if (commandList_ == nullptr || commandQueue_ == nullptr) {
		return false;
	}

	// Close を忘れると ExecuteCommandLists が失敗する。記録の終わりはここで確定する。
	const HRESULT hr = commandList_->Close();

	if (FAILED(hr)) {
		Log(logStream, std::format("CommandList Close failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		return false;
	}

	ID3D12CommandList* commandLists[] = {commandList_.Get()};
	commandQueue_->ExecuteCommandLists(1u, commandLists);
	return true;
}

bool DirectXCommon::EndFrame(uint32_t presentSyncInterval, std::ostream& logStream) {
	if (swapChain_ == nullptr) {
		return false;
	}

	//----------------------------------------
	// 表示
	//----------------------------------------

	// presentSyncInterval は Project Settings の VSync 設定をそのまま渡す。
	const HRESULT hr = swapChain_->Present(presentSyncInterval, 0u);

	if (FAILED(hr)) {
		Log(logStream, std::format("SwapChain Present failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		return false;
	}

	//----------------------------------------
	// GPU の完了待ち
	//----------------------------------------

	// 次のフレームで Command Allocator を巻き戻すため、ここで待たないと
	// まだ GPU が読んでいる命令の置き場を消してしまう。
	WaitForGpu();
	return true;
}

void DirectXCommon::WaitForGpu() {
	if (commandQueue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr) {
		return;
	}

	// 今送った分まで進んだら値が届く、という約束を Queue へ積む。
	++fenceValue_;

	if (FAILED(commandQueue_->Signal(fence_.Get(), fenceValue_))) {
		return;
	}

	if (fence_->GetCompletedValue() < fenceValue_) {
		// まだ届いていないので、届いたら Event を立ててもらって待つ。
		if (SUCCEEDED(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_))) {
			WaitForSingleObject(fenceEvent_, INFINITE);
		}
	}
}

//========================================
// SwapChain の作り直し
//========================================

bool DirectXCommon::ResizeSwapChain(uint32_t renderWidth, uint32_t renderHeight, std::ostream& logStream) {
	if (swapChain_ == nullptr) {
		return false;
	}

	// Back Buffer を掴んだまま ResizeBuffers はできないので、先に参照を手放す。
	// GPU がまだ読んでいる可能性があるため、その前に完了を待つ。
	WaitForGpu();

	for (uint32_t bufferIndex = 0u; bufferIndex < kBackBufferCount; ++bufferIndex) {
		backBuffers_[bufferIndex].Reset();
	}

	const HRESULT hr = swapChain_->ResizeBuffers(
		kBackBufferCount,
		renderWidth,
		renderHeight,
		kBackBufferFormat,
		0u);

	if (FAILED(hr)) {
		Log(logStream, std::format("SwapChain ResizeBuffers failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		return false;
	}

	swapChainDesc_.Width = renderWidth;
	swapChainDesc_.Height = renderHeight;
	return CreateBackBufferViews();
}

//========================================
// 終了処理
//========================================

void DirectXCommon::Finalize() {
	// GPU がまだ使っている Resource を手放さないよう、完了を待ってから解放する。
	WaitForGpu();

	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	// ComPtr なので Release は書かない。生成と逆順に参照を手放す。
	fence_.Reset();
	renderTimestampReadback_.Reset();
	renderTimestampQueryHeap_.Reset();

	for (uint32_t bufferIndex = 0u; bufferIndex < kBackBufferCount; ++bufferIndex) {
		backBuffers_[bufferIndex].Reset();
	}

	dsvDescriptorHeap_.Reset();
	srvDescriptorHeap_.Reset();
	rtvDescriptorHeap_.Reset();
	swapChain_.Reset();
	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();
	device_.Reset();
	useAdapter_.Reset();
	dxgiFactory_.Reset();
}

//========================================
// 条件判定と getter
//========================================

bool DirectXCommon::HasProfilerResources() const {
	return renderTimestampQueryHeap_ != nullptr
		&& renderTimestampReadback_ != nullptr
		&& renderTimestampFrequency_ != 0u;
}

ID3D12Resource* DirectXCommon::GetBackBuffer(uint32_t bufferIndex) const {
	if (bufferIndex >= kBackBufferCount) {
		return nullptr;
	}

	return backBuffers_[bufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle(uint32_t bufferIndex) const {
	if (bufferIndex >= kBackBufferCount) {
		return D3D12_CPU_DESCRIPTOR_HANDLE{};
	}

	return backBufferRtvHandles_[bufferIndex];
}

uint32_t DirectXCommon::GetCurrentBackBufferIndex() const {
	if (swapChain_ == nullptr) {
		return 0u;
	}

	return swapChain_->GetCurrentBackBufferIndex();
}
