#include "3d/Model.h"
#include "base/TextureManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace {
uint32_t ResolveIndex(const std::string& value, size_t count) {
	const int index = std::stoi(value);
	const int resolved = index > 0 ? index - 1 : static_cast<int>(count) + index;
	if (resolved < 0 || static_cast<size_t>(resolved) >= count) {
		throw std::runtime_error("OBJ index is out of range");
	}
	return static_cast<uint32_t>(resolved);
}

Model::Vertex ReadFaceVertex(const std::string& token,
	const std::vector<Vector4>& positions,
	const std::vector<Vector2>& texcoords,
	const std::vector<Vector3>& normals) {
	std::istringstream stream(token);
	std::string p, t, n;
	std::getline(stream, p, '/');
	std::getline(stream, t, '/');
	std::getline(stream, n, '/');
	if (p.empty() || t.empty() || n.empty()) {
		throw std::runtime_error("OBJ faces require position, UV and normal indices");
	}
	return {positions[ResolveIndex(p, positions.size())],
		texcoords[ResolveIndex(t, texcoords.size())],
		normals[ResolveIndex(n, normals.size())]};
}
}

bool Model::Initialize(ID3D12Device* device, TextureManager* textures, const std::string& filePath) {
	if (!device || !textures) { return false; }
	const std::filesystem::path path(filePath);
	std::ifstream file(path);
	if (!file) { return false; }
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;
	std::vector<Vertex> vertices;
	std::filesystem::path materialPath;
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream input(line);
		std::string kind;
		input >> kind;
		if (kind == "v") {
			Vector4 v{};
			input >> v.x >> v.y >> v.z;
			v.x = -v.x; v.w = 1.0f;
			positions.push_back(v);
		} else if (kind == "vt") {
			Vector2 v{};
			input >> v.x >> v.y;
			v.y = 1.0f - v.y;
			texcoords.push_back(v);
		} else if (kind == "vn") {
			Vector3 v{};
			input >> v.x >> v.y >> v.z;
			v.x = -v.x;
			normals.push_back(v);
		} else if (kind == "mtllib") {
			std::string name;
			input >> name;
			materialPath = path.parent_path() / name;
		} else if (kind == "f") {
			std::vector<Vertex> face;
			std::string token;
			while (input >> token) {
				face.push_back(ReadFaceVertex(token, positions, texcoords, normals));
			}
			// 左手系への変換で面の向きが反転するため頂点順を反転する。
			for (size_t i = 1; i + 1 < face.size(); ++i) {
				vertices.push_back(face[i + 1]);
				vertices.push_back(face[i]);
				vertices.push_back(face[0]);
			}
		}
	}
	if (vertices.empty() || materialPath.empty()) { return false; }
	std::ifstream material(materialPath);
	if (!material) { return false; }
	std::filesystem::path texturePath;
	while (std::getline(material, line)) {
		std::istringstream input(line);
		std::string kind, name;
		input >> kind;
		if (kind == "map_Kd") {
			input >> name;
			texturePath = materialPath.parent_path() / name;
			break;
		}
	}
	if (texturePath.empty()) { return false; }
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeof(Vertex) * vertices.size();
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertexResource_.GetAddressOf())))) {
		return false;
	}
	void* mapped = nullptr;
	if (FAILED(vertexResource_->Map(0, nullptr, &mapped))) { return false; }
	std::memcpy(mapped, vertices.data(), static_cast<size_t>(desc.Width));
	vertexResource_->Unmap(0, nullptr);
	vertexBufferView_ = {vertexResource_->GetGPUVirtualAddress(),
		static_cast<UINT>(desc.Width), sizeof(Vertex)};
	vertexCount_ = static_cast<UINT>(vertices.size());
	textureHandle_ = textures->LoadTexture(texturePath.generic_string());
	textures_ = textures;
	path_ = path.generic_string();
	return true;
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) const {
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->SetGraphicsRootDescriptorTable(3, textures_->GetSRVGPUDescriptorHandle(textureHandle_));
	commandList->DrawInstanced(vertexCount_, 1, 0, 0);
}
