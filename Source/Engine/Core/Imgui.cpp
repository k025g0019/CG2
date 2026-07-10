#include <cassert>
#include <d3d12.h>
ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	ID3D12DescriptorHeap* descriptorHeap = nullptr;  // descriptorHeap は CreateDescriptorHeap が生成して返す DirectX12 のハンドル置き場。

	// descriptorHeapDesc は Heap 種類、確保数、Shader 参照可否を GPU に伝える設定。
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;

	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // shaderVisible が true の Heap だけ、Shader から GPU descriptor handle で参照できる。
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));  // device が実際の DescriptorHeap を生成し、失敗時は assert で開発中に止める。
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}
