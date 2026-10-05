#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include "WinApp.h"

class DirectXCommon {
public:
	void Initialize();

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

	std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 2> descriptorHeaps;

private:
	void deviceInitialize(); // DirectX12デバイス初期化
	void commandInitialize(); // コマンド関連初期化
	void swapChain(); // スワップチェーン初期化
	void depthBuffer(); // 深度バッファ初期化
	void descriptorHeap(); // デスクリプタヒープ初期化
	void renderTargetView(); // レンダーターゲットビュー初期化
	void depthStencilView(); //	デプスステンシルビュー初期化
	void fenceInitialize(); // フェンス初期化
	void viewPortRectangleInitialize(); // ビューポートとシザー矩形初期化
	void scissoringRectangleInitialize(); // シザー矩形初期化
	void DXCcompiler(); // DXCコンパイラ初期化
	void ImGuiInitialize(); // ImGui初期化


	// DirectX12デバイス	
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
	// DirectX12デバイス
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index);
	//DirectX12デバイス
	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// DXGIファクトリー
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;

	//デスクリプタHeapを生成する
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	                                                                  UINT numDescriptors, bool shaderVisible);
	// WindowsAPI
	WinApp* winApp = nullptr;
};
