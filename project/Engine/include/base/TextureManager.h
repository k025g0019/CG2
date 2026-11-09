#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

// スプライトと3D描画が同じ画像リソース・SRVを参照できる共有管理クラス。
// Initializeで指定したSRV範囲は、このインスタンス専用に予約しておく。
class TextureManager {
public:
	struct TextureData {
		std::string filePath;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
		uint32_t width = 0;
		uint32_t height = 0;
	};

	TextureManager() = default;
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator=(const TextureManager&) = delete;

	void Initialize(DirectXCommon* dxCommon, uint32_t firstSrvIndex, uint32_t maxTextures);
	// 初回のみファイル読込・GPU転送を行い、完了まで待つ。同じパスには同じハンドルを返す。
	uint32_t LoadTexture(const std::string& filePath);
	const TextureData& GetTexture(uint32_t textureHandle) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t textureHandle) const;
	std::size_t GetTextureCount() const { return textures_.size(); }
	// 全利用者の描画完了後に呼ぶ。既存のハンドルはすべて無効になる。
	void Finalize();

private:
	DirectXCommon* dxCommon_ = nullptr;
	uint32_t firstSrvIndex_ = 0;
	uint32_t maxTextures_ = 0;
	std::vector<TextureData> textures_;
	std::unordered_map<std::wstring, uint32_t> textureHandles_;
};
