#pragma once
#include <d3d12.h>
ID3D12DescriptorHeap * CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
