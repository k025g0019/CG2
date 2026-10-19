#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl.h>

class WinApp;

class DirectXCommon {
public:
	//------------------------------
	// 定数
	//------------------------------

	static constexpr uint32_t kSwapChainBufferCount = 2;
	static constexpr uint32_t kMaxSRVCount = 128;


	//------------------------------
	// 初期化・終了
	//------------------------------

	void Initialize(WinApp* winApp);
	void Finalize();


	//------------------------------
	// 毎フレーム描画処理
	//------------------------------

	void PreDraw();
	void PostDraw();


	//------------------------------
	// DirectXオブジェクト取得
	//------------------------------

	ID3D12Device* GetDevice() const {
		return device_.Get();
	}

	ID3D12GraphicsCommandList* GetCommandList() const {
		return commandList_.Get();
	}

	ID3D12CommandQueue* GetCommandQueue() const {
		return commandQueue_.Get();
	}

	ID3D12DescriptorHeap* GetSRVDescriptorHeap() const {
		return srvDescriptorHeap_.Get();
	}

	IDXGISwapChain4* GetSwapChain() const {
		return swapChain_.Get();
	}


	//------------------------------
	// DXCコンパイラ取得
	//------------------------------

	IDxcUtils* GetDXCUtils() const {
		return dxcUtils_.Get();
	}

	IDxcCompiler3* GetDXCCompiler() const {
		return dxcCompiler_.Get();
	}

	IDxcIncludeHandler* GetIncludeHandler() const {
		return includeHandler_.Get();
	}


	//------------------------------
	// ディスクリプタハンドル取得
	//------------------------------

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(
		uint32_t index) const;

	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(
		uint32_t index) const;

	uint32_t GetSRVDescriptorSize() const {
		return srvDescriptorSize_;
	}

	void ResetCommandList();
	void ExecuteCommandListAndWait();

private:
	//------------------------------
	// 基盤初期化
	//------------------------------

	void InitializeDevice();
	void InitializeCommand();
	void InitializeSwapChain();
	void InitializeDescriptorHeaps();
	void InitializeRenderTargetViews();
	void InitializeDepthBuffer();
	void InitializeFence();
	void InitializeViewport();
	void InitializeDXCCompiler();
	void InitializeImGui();


	//------------------------------
	// FPS固定処理
	//------------------------------

	void InitializeFixFPS();
	void UpdateFixFPS();


	//------------------------------
	// GPU同期処理
	//------------------------------

	void WaitForGPU();


	//------------------------------
	// 内部生成処理
	//------------------------------

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
	CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		uint32_t numDescriptors,
		bool shaderVisible);

	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t descriptorSize,
		uint32_t index);

	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t descriptorSize,
		uint32_t index);


	//------------------------------
	// WindowsAPI
	//------------------------------

	WinApp* winApp_ = nullptr;


	//------------------------------
	// DirectX12デバイス
	//------------------------------

	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;


	//------------------------------
	// コマンド
	//------------------------------

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;


	//------------------------------
	// スワップチェーン
	//------------------------------

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

	std::array<
		Microsoft::WRL::ComPtr<ID3D12Resource>,
		kSwapChainBufferCount>
	swapChainResources_{};


	//------------------------------
	// ディスクリプタヒープ
	//------------------------------

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

	uint32_t rtvDescriptorSize_ = 0;
	uint32_t srvDescriptorSize_ = 0;
	uint32_t dsvDescriptorSize_ = 0;

	std::array<
		D3D12_CPU_DESCRIPTOR_HANDLE,
		kSwapChainBufferCount>
	rtvHandles_{};

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};


	//------------------------------
	// 深度バッファ
	//------------------------------

	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;


	//------------------------------
	// 描画範囲
	//------------------------------

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};


	//------------------------------
	// GPU同期
	//------------------------------

	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;


	//------------------------------
	// DXCコンパイラ
	//------------------------------

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;


	//------------------------------
	// FPS固定
	//------------------------------

	std::chrono::steady_clock::time_point referenceTime_{};


	//------------------------------
	// 状態管理
	//------------------------------

	bool isInitialized_ = false;
	bool isImGuiInitialized_ = false;
};
