#pragma once

#include <cstdint>
#include <string>
#include <d3d12.h>
#include <wrl.h>
#include "math/Vector.h"

class TextureManager;

// 頂点とテクスチャを複数の Object3d で共有する。
class Model {
public:
	struct Vertex {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};
	bool Initialize(ID3D12Device* device, TextureManager* textures, const std::string& filePath);
	void Draw(ID3D12GraphicsCommandList* commandList) const;
	const std::string& GetPath() const { return path_; }
private:
	std::string path_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	TextureManager* textures_ = nullptr;
	uint32_t textureHandle_ = 0;
	UINT vertexCount_ = 0;
};
