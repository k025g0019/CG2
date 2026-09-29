#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#pragma warning(pop)

#include <cstdint>
#include <iosfwd>

class WinApp;

#pragma warning(push)
#pragma warning(disable : 4820)

//========================================
// DirectX12 基盤のクラス化
//========================================

// DirectX12 を動かすために「どの描画機能を作っても必ず要る」ものだけを持つ。
//
//   Device          … GPU Resource を作る窓口
//   Command 3 種    … Queue / Allocator / List
//   SwapChain       … Window へ出す Back Buffer 列と、その RTV
//   Depth Stencil   … 画面と同じ大きさの深度
//   Descriptor Heap … RTV / SRV / DSV の 3 本
//   Fence           … CPU が GPU の完了を待つための同期
//
// Bloom や SSAO など機能ごとの Render Target はここには置かない。
// それらは Device を借りて各機能が作る。
//
// すべて ComPtr で持つため、このクラスの中にも外にも Release を書かない。
// 参照を手放す順番だけは Finalize で明示する。
class DirectXCommon {
public:
	//----------------------------------------
	// クラスの定数
	//----------------------------------------

	static constexpr uint32_t kBackBufferCount = 2u;  // FLIP_DISCARD の Back Buffer 枚数
	static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;  // Back Buffer の形式
	static constexpr DXGI_FORMAT kDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;  // 深度の形式

	//----------------------------------------
	// 初期化・終了
	//----------------------------------------

	// Adapter 選択から Fence 生成までを順番に行う。
	// SwapChain の生成に Window Handle が要るため、WinApp のポインタを受け取る。
	bool Initialize(WinApp* winApp, std::ostream& logStream);

	void Finalize();  // GPU の完了を待ってから、生成と逆順で参照を手放す

	//----------------------------------------
	// 毎フレームの描画の前処理・後処理
	//----------------------------------------

	// 描画の前処理。Command Allocator と List を巻き戻して、このフレームの記録を始める。
	// 失敗したら false を返す。呼び出し側はそのフレームの描画を諦める。
	bool BeginFrame(ID3D12PipelineState* initialPipelineState);

	// Back Buffer へ書き始める直前の状態遷移。PRESENT -> RENDER_TARGET。
	void BeginBackBufferPass();

	// Back Buffer へ書き終えた直後の状態遷移。RENDER_TARGET -> PRESENT。
	void EndBackBufferPass();

	// 記録した命令を GPU へ送る。Present はまだ行わない。失敗したら false。
	bool SubmitCommandList(std::ostream& logStream);

	// 描画の後処理。Back Buffer を Window へ出し、GPU の完了を待つ。
	// presentSyncInterval は 1 で垂直同期あり、0 でなし。失敗したら false。
	bool EndFrame(uint32_t presentSyncInterval, std::ostream& logStream);

	// CPU 側で GPU の完了を待つ。Resource を作り直す前などに使う。
	void WaitForGpu();

	// Window サイズが変わったときに Back Buffer を作り直す。失敗したら false。
	bool ResizeSwapChain(uint32_t renderWidth, uint32_t renderHeight, std::ostream& logStream);

	//----------------------------------------
	// 条件判定
	//----------------------------------------

	bool IsInitialized() const { return device_ != nullptr; }  // Device を作れているか
	bool HasProfilerResources() const;  // GPU 時間計測用の QueryHeap がそろっているか

	//----------------------------------------
	// getter
	//----------------------------------------

	const Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() const { return device_; }
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& GetCommandList() const { return commandList_; }
	const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& GetCommandQueue() const { return commandQueue_; }
	const Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& GetCommandAllocator() const { return commandAllocator_; }
	const Microsoft::WRL::ComPtr<IDXGISwapChain4>& GetSwapChain() const { return swapChain_; }
	const Microsoft::WRL::ComPtr<IDXGIFactory7>& GetDxgiFactory() const { return dxgiFactory_; }
	const Microsoft::WRL::ComPtr<IDXGIAdapter4>& GetAdapter() const { return useAdapter_; }
	const Microsoft::WRL::ComPtr<ID3D12Fence>& GetFence() const { return fence_; }

	// ComPtr から取り出した生ポインタ。呼び出し側が所有権を持たないことを型で示す。
	ID3D12Device* GetDevicePointer() const { return device_.Get(); }
	ID3D12GraphicsCommandList* GetCommandListPointer() const { return commandList_.Get(); }

	ID3D12DescriptorHeap* GetRtvDescriptorHeap() const { return rtvDescriptorHeap_.Get(); }
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
	ID3D12DescriptorHeap* GetDsvDescriptorHeap() const { return dsvDescriptorHeap_.Get(); }

	ID3D12Resource* GetBackBuffer(uint32_t bufferIndex) const;  // 範囲外なら nullptr
	D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle(uint32_t bufferIndex) const;
	uint32_t GetCurrentBackBufferIndex() const;  // SwapChain が今書いてよいと言っている番号

	const DXGI_SWAP_CHAIN_DESC1& GetSwapChainDesc() const { return swapChainDesc_; }

	// GPU 時間計測。Profiler 以外は触らない。
	const Microsoft::WRL::ComPtr<ID3D12QueryHeap>& GetTimestampQueryHeap() const { return renderTimestampQueryHeap_; }
	const Microsoft::WRL::ComPtr<ID3D12Resource>& GetTimestampReadback() const { return renderTimestampReadback_; }
	uint64_t GetTimestampFrequency() const { return renderTimestampFrequency_; }

private:
	//----------------------------------------
	// Device と Command
	//----------------------------------------

	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;  // Adapter 列挙と SwapChain 生成
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter_;  // 採用した物理 GPU
	Microsoft::WRL::ComPtr<ID3D12Device> device_;  // GPU Resource を作る窓口
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;  // GPU へ CommandList を送る
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;  // 記録した命令の置き場
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;  // 1 フレーム分の命令を記録する

	//----------------------------------------
	// SwapChain と Back Buffer
	//----------------------------------------

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;  // Window へ出す Back Buffer 列
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};  // Resize のときに幅と高さだけ差し替えて使い回す
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBackBufferCount];  // SwapChain から借りた実体
	D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandles_[kBackBufferCount]{};  // 上の RTV
	uint32_t currentBackBufferIndex_ = 0u;  // BeginBackBufferPass で決めた今フレームの番号

	//----------------------------------------
	// Descriptor Heap
	//----------------------------------------

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;  // Render Target View
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;  // Shader Resource / CBV / UAV
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;  // Depth Stencil View

	//----------------------------------------
	// 同期
	//----------------------------------------

	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;  // GPU がどこまで進んだかを表す値
	uint64_t fenceValue_ = 0u;  // Signal するたびに増やす
	HANDLE fenceEvent_ = nullptr;  // 完了を待つための Event。Finalize で CloseHandle する

	//----------------------------------------
	// GPU 時間計測
	//----------------------------------------

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> renderTimestampQueryHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> renderTimestampReadback_;
	uint64_t renderTimestampFrequency_ = 0u;  // Timestamp を秒へ直す分母。0 なら計測できない

	//----------------------------------------
	// 初期化の内訳
	//----------------------------------------

	bool CreateDeviceAndCommands(std::ostream& logStream);
	bool CreateSwapChainAndHeaps(WinApp* winApp, std::ostream& logStream);
	bool CreateBackBufferViews();
	bool CreateFenceObjects();
	bool CreateTimestampResources(std::ostream& logStream);
};

#pragma warning(pop)
