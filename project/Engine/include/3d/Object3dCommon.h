#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

class Model;
class TextureManager;

// 3D オブジェクト共通の描画設定と、ファイル単位のモデルキャッシュ。
class Object3dCommon {
public:
	bool Initialize(ID3D12Device* device, TextureManager* textures,
		IDxcUtils* utils, IDxcCompiler3* compiler, IDxcIncludeHandler* includes);
	std::shared_ptr<const Model> LoadModel(const std::string& filePath);
	void ApplyDrawSettings(ID3D12GraphicsCommandList* commandList) const;
	void Finalize();

private:
	ID3D12Device* device_ = nullptr;
	TextureManager* textures_ = nullptr;
	std::unordered_map<std::string, std::shared_ptr<Model>> models_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> lightResource_;
};
