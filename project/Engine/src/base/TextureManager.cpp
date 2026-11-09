#include "base/TextureManager.h"

#include "base/DirectXCommon.h"
#include "motion/StringUtility.h"
#pragma warning(push, 0)
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#pragma warning(pop)

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
	void CheckResult(HRESULT result, const char* operation) {
		if (FAILED(result)) {
			throw std::runtime_error(operation);
		}
	}

	// 描画中のコマンドリストをResetしないよう、転送専用のリストを使用する。
	// 同じキュー上で完了を待ち、転送元バッファがGPUより先に解放されるのを防ぐ。
	void UploadTexture(DirectXCommon* dxCommon, ID3D12Resource* texture,
	                   const DirectX::ScratchImage& image) {
		auto* device = dxCommon->GetDevice();
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
		CheckResult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(allocator.GetAddressOf())), "TextureManager: create upload allocator failed");
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
		CheckResult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			IID_PPV_ARGS(commandList.GetAddressOf())), "TextureManager: create upload list failed");

		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		for (size_t index = 0; index < image.GetImageCount(); ++index) {
			const auto& mip = image.GetImages()[index];
			subresources.push_back({mip.pixels, static_cast<LONG_PTR>(mip.rowPitch),
				static_cast<LONG_PTR>(mip.slicePitch)});
		}
		const UINT count = static_cast<UINT>(subresources.size());
		const UINT64 uploadSize = GetRequiredIntermediateSize(texture, 0, count);
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
		const auto bufferDescription = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
		CheckResult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDescription,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(uploadResource.GetAddressOf())),
			"TextureManager: create upload buffer failed");
		if (UpdateSubresources(commandList.Get(), texture, uploadResource.Get(), 0, 0, count,
			subresources.data()) == 0) {
			throw std::runtime_error("TextureManager: copy texture data failed");
		}
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		commandList->ResourceBarrier(1, &barrier);
		CheckResult(commandList->Close(), "TextureManager: close upload list failed");

		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		CheckResult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())),
			"TextureManager: create upload fence failed");
		std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
		if (!event) {
			throw std::runtime_error("TextureManager: create upload event failed");
		}
		CheckResult(fence->SetEventOnCompletion(1, event.get()), "TextureManager: set upload event failed");
		ID3D12CommandList* lists[] = {commandList.Get()};
		dxCommon->GetCommandQueue()->ExecuteCommandLists(1, lists);
		CheckResult(dxCommon->GetCommandQueue()->Signal(fence.Get(), 1), "TextureManager: signal upload fence failed");
		if (WaitForSingleObject(event.get(), INFINITE) != WAIT_OBJECT_0 ||
			fence->GetCompletedValue() == UINT64_MAX) {
			throw std::runtime_error("TextureManager: wait for upload failed");
		}
	}
}

void TextureManager::Initialize(DirectXCommon* dxCommon, uint32_t firstSrvIndex, uint32_t maxTextures) {
	if (dxCommon == nullptr || dxCommon_ != nullptr) {
		throw std::logic_error("TextureManager::Initialize: invalid initialization");
	}
	const UINT capacity = dxCommon->GetSRVDescriptorHeap()->GetDesc().NumDescriptors;
	if (firstSrvIndex >= capacity || maxTextures == 0 || maxTextures > capacity - firstSrvIndex) {
		throw std::invalid_argument("TextureManager: invalid SRV range");
	}
	textures_.reserve(maxTextures);
	dxCommon_ = dxCommon;
	firstSrvIndex_ = firstSrvIndex;
	maxTextures_ = maxTextures;
}

uint32_t TextureManager::LoadTexture(const std::string& filePath) {
	if (dxCommon_ == nullptr) {
		throw std::logic_error("TextureManager is not initialized");
	}
	if (filePath.empty()) {
		throw std::invalid_argument("TextureManager: texture path is empty");
	}
	const auto path = std::filesystem::weakly_canonical(
		std::filesystem::absolute(std::filesystem::path(StringUtility::ConvertString(filePath))));
	std::wstring key = path.generic_wstring();
	// 相対/絶対パス、区切り文字、英字の大小が違っても同じ画像を共有する。
	std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) {
		return static_cast<wchar_t>(std::towlower(c));
	});
	const auto existing = textureHandles_.find(key);
	if (existing != textureHandles_.end()) {
		return existing->second;
	}
	if (textures_.size() >= maxTextures_) {
		throw std::runtime_error("TextureManager: reserved SRV range is full");
	}

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage source;
	const HRESULT loaded = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, &metadata, source);
	if (FAILED(loaded)) {
		throw std::runtime_error("TextureManager: could not load " + filePath);
	}
	DirectX::ScratchImage mipImages;
	CheckResult(DirectX::GenerateMipMaps(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
		DirectX::TEX_FILTER_SRGB, 0, mipImages), "TextureManager: generate mipmaps failed");
	metadata = mipImages.GetMetadata();

	TextureData texture;
	texture.filePath = StringUtility::ConvertString(path.generic_wstring());
	texture.width = static_cast<uint32_t>(metadata.width);
	texture.height = static_cast<uint32_t>(metadata.height);
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	description.Width = metadata.width;
	description.Height = texture.height;
	description.DepthOrArraySize = 1;
	description.MipLevels = static_cast<UINT16>(metadata.mipLevels);
	description.Format = metadata.format;
	description.SampleDesc.Count = 1;
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	CheckResult(dxCommon_->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture.resource.GetAddressOf())),
		"TextureManager: create texture resource failed");
	UploadTexture(dxCommon_, texture.resource.Get(), mipImages);

	const uint32_t handle = static_cast<uint32_t>(textures_.size());
	const uint32_t srvIndex = firstSrvIndex_ + handle;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
	srvDescription.Format = metadata.format;
	srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescription.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
	dxCommon_->GetDevice()->CreateShaderResourceView(texture.resource.Get(), &srvDescription,
		dxCommon_->GetSRVCPUDescriptorHandle(srvIndex));
	texture.srvHandleGPU = dxCommon_->GetSRVGPUDescriptorHandle(srvIndex);
	textures_.push_back(std::move(texture));
	textureHandles_.emplace(std::move(key), handle);
	return handle;
}

const TextureManager::TextureData& TextureManager::GetTexture(uint32_t textureHandle) const {
	if (dxCommon_ == nullptr) {
		throw std::logic_error("TextureManager is not initialized");
	}
	return textures_.at(textureHandle);
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSRVGPUDescriptorHandle(uint32_t textureHandle) const {
	return GetTexture(textureHandle).srvHandleGPU;
}

void TextureManager::Finalize() {
	textureHandles_.clear();
	textures_.clear();
	dxCommon_ = nullptr;
	firstSrvIndex_ = 0;
	maxTextures_ = 0;
}
