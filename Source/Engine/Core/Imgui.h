#pragma once
#include <d3d12.h>

// DirectX12 の RTV / SRV / DSV 用 DescriptorHeap を作る共通関数。
ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
