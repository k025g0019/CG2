#include "2d/Sprite.h"

#include "2d/SpriteCommon.h"
#include "base/TextureManager.h"
#include "math/Vector&Matrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
	bool IsFinite(const Vector2& value) {
		return std::isfinite(value.x) && std::isfinite(value.y);
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, UINT64 size) {
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC description{};
		description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		description.Width = size;
		description.Height = 1;
		description.DepthOrArraySize = 1;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;
		description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(resource.GetAddressOf())))) {
			throw std::runtime_error("Sprite: create buffer failed");
		}
		return resource;
	}

	void MapBuffer(ID3D12Resource* resource, void** data) {
		const D3D12_RANGE readRange{0, 0};
		if (FAILED(resource->Map(0, &readRange, data))) {
			throw std::runtime_error("Sprite: map buffer failed");
		}
	}
}

void Sprite::Initialize(SpriteCommon* spriteCommon, TextureManager* textureManager,
                        const std::string& textureFilePath) {
	if (spriteCommon == nullptr || spriteCommon->GetDxCommon() == nullptr ||
		textureManager == nullptr || spriteCommon_ != nullptr) {
		throw std::logic_error("Sprite::Initialize: invalid initialization");
	}
	spriteCommon_ = spriteCommon;
	textureManager_ = textureManager;
	SetTextureFilePath(textureFilePath);
	// 初期表示サイズは読み込んだ画像そのもののピクセル数。
	size_ = textureSize_;
	CreateResources();
	Update();
}

void Sprite::CreateResources() {
	auto* device = spriteCommon_->GetDxCommon()->GetDevice();
	vertexResource_ = CreateBuffer(device, sizeof(VertexData) * 4);
	indexResource_ = CreateBuffer(device, sizeof(uint32_t) * 6);
	// Root CBVのGPUアドレス/確保サイズは256バイト境界に揃える。
	colorResource_ = CreateBuffer(device, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	transformResource_ = CreateBuffer(device, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	MapBuffer(vertexResource_.Get(), reinterpret_cast<void**>(&vertexData_));
	MapBuffer(colorResource_.Get(), reinterpret_cast<void**>(&colorData_));
	MapBuffer(transformResource_.Get(), reinterpret_cast<void**>(&transformData_));
	uint32_t* indices = nullptr;
	MapBuffer(indexResource_.Get(), reinterpret_cast<void**>(&indices));
	const uint32_t indexData[] = {0, 1, 2, 1, 3, 2};
	std::memcpy(indices, indexData, sizeof(indexData));
	indexResource_->Unmap(0, nullptr);
	vertexBufferView_ = {vertexResource_->GetGPUVirtualAddress(), sizeof(VertexData) * 4, sizeof(VertexData)};
	indexBufferView_ = {indexResource_->GetGPUVirtualAddress(), sizeof(indexData), DXGI_FORMAT_R32_UINT};
}

void Sprite::UpdateVertices() {
	// アンカーの位置を回転の中心にする。反転はUVだけに適用し、表示位置を動かさない。
	const float left = -anchorPoint_.x * size_.x;
	const float right = (1.0f - anchorPoint_.x) * size_.x;
	const float top = -anchorPoint_.y * size_.y;
	const float bottom = (1.0f - anchorPoint_.y) * size_.y;
	const auto& texture = textureManager_->GetTexture(textureHandle_);
	const float width = static_cast<float>(texture.width);
	const float height = static_cast<float>(texture.height);
	// ピクセル座標からUV(0～1)へ変換する。切り抜きサイズを表示サイズへ混ぜない。
	float u0 = textureLeftTop_.x / width;
	float u1 = (textureLeftTop_.x + textureSize_.x) / width;
	float v0 = textureLeftTop_.y / height;
	float v1 = (textureLeftTop_.y + textureSize_.y) / height;
	if (isFlipX_) {
		std::swap(u0, u1);
	}
	if (isFlipY_) {
		std::swap(v0, v1);
	}
	vertexData_[0] = {{left, bottom, 0.0f, 1.0f}, {u0, v1}};
	vertexData_[1] = {{left, top, 0.0f, 1.0f}, {u0, v0}};
	vertexData_[2] = {{right, bottom, 0.0f, 1.0f}, {u1, v1}};
	vertexData_[3] = {{right, top, 0.0f, 1.0f}, {u1, v0}};
}

void Sprite::Update() {
	if (vertexData_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	UpdateVertices();
	*colorData_ = color_;
	const Matrix4x4 world = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, rotation_},
		{position_.x, position_.y, 0.0f});
	*transformData_ = Multiply(world, spriteCommon_->GetProjectionMatrix());
}

void Sprite::Draw() const {
	if (vertexData_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	if (size_.x == 0.0f || size_.y == 0.0f) {
		return;
	}
	auto* commandList = spriteCommon_->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(0, colorResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, textureManager_->GetSRVGPUDescriptorHandle(textureHandle_));
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::Finalize() {
	vertexData_ = nullptr;
	colorData_ = nullptr;
	transformData_ = nullptr;
	vertexResource_.Reset();
	indexResource_.Reset();
	colorResource_.Reset();
	transformResource_.Reset();
	spriteCommon_ = nullptr;
	textureManager_ = nullptr;
}

void Sprite::SetPosition(const Vector2& position) {
	if (!IsFinite(position)) {
		throw std::invalid_argument("Sprite: position must be finite");
	}
	position_ = position;
}

void Sprite::SetRotation(float rotation) {
	if (!std::isfinite(rotation)) {
		throw std::invalid_argument("Sprite: rotation must be finite");
	}
	rotation_ = rotation;
}

void Sprite::SetColor(const Vector4& color) {
	if (!std::isfinite(color.x) || !std::isfinite(color.y) ||
		!std::isfinite(color.z) || !std::isfinite(color.w)) {
		throw std::invalid_argument("Sprite: color must be finite");
	}
	color_ = color;
}

void Sprite::SetSize(const Vector2& size) {
	if (!IsFinite(size) || size.x < 0.0f || size.y < 0.0f) {
		throw std::invalid_argument("Sprite: size must be non-negative");
	}
	size_ = size;
}

void Sprite::SetAnchorPoint(const Vector2& anchorPoint) {
	if (!IsFinite(anchorPoint)) {
		throw std::invalid_argument("Sprite: anchor point must be finite");
	}
	anchorPoint_ = anchorPoint;
}

void Sprite::SetTextureRect(const Vector2& leftTop, const Vector2& size) {
	if (textureManager_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	const auto& texture = textureManager_->GetTexture(textureHandle_);
	if (!IsFinite(leftTop) || !IsFinite(size) || leftTop.x < 0.0f || leftTop.y < 0.0f ||
		size.x <= 0.0f || size.y <= 0.0f ||
		leftTop.x + size.x > static_cast<float>(texture.width) ||
		leftTop.y + size.y > static_cast<float>(texture.height)) {
		throw std::invalid_argument("Sprite: texture rectangle is outside the image");
	}
	textureLeftTop_ = leftTop;
	textureSize_ = size;
}

void Sprite::SetTextureLeftTop(const Vector2& leftTop) {
	SetTextureRect(leftTop, textureSize_);
}

void Sprite::SetTextureSize(const Vector2& size) {
	SetTextureRect(textureLeftTop_, size);
}

void Sprite::SetTexture(uint32_t textureHandle) {
	if (textureManager_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	const auto& texture = textureManager_->GetTexture(textureHandle);
	textureHandle_ = textureHandle;
	textureLeftTop_ = {};
	textureSize_ = {static_cast<float>(texture.width), static_cast<float>(texture.height)};
}

void Sprite::SetTextureFilePath(const std::string& textureFilePath) {
	if (textureManager_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	SetTexture(textureManager_->LoadTexture(textureFilePath));
}

const std::string& Sprite::GetTextureFilePath() const {
	if (textureManager_ == nullptr) {
		throw std::logic_error("Sprite is not initialized");
	}
	return textureManager_->GetTexture(textureHandle_).filePath;
}
