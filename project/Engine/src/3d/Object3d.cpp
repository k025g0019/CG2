#include "3d/Object3d.h"
#include "3d/Model.h"
#include "math/Vector&Matrix.h"

#include <cstdint>
#include <utility>

namespace {
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

bool CreateUploadBuffer(ID3D12Device* device, size_t bytes,
	Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = (bytes + 255) & ~size_t(255);
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(resource.GetAddressOf())));
}
}

bool Object3d::Initialize(ID3D12Device* device, std::shared_ptr<const Model> model) {
	if (!device || !model || !CreateUploadBuffer(device, sizeof(TransformationMatrix), transformResource_) ||
		!CreateUploadBuffer(device, sizeof(Material), materialResource_)) { return false; }
	if (FAILED(transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_)))) {
		return false;
	}
	Material* material = nullptr;
	if (FAILED(materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material)))) {
		return false;
	}
	*material = {{1.0f, 1.0f, 1.0f, 1.0f}, TRUE, {}, MakeIdentity4x4()};
	model_ = std::move(model);
	Update(MakeIdentity4x4());
	return true;
}

void Object3d::SetModel(std::shared_ptr<const Model> model) {
	if (model) { model_ = std::move(model); }
}

void Object3d::Update(const Matrix4x4& viewProjection) {
	worldMatrix_ = MakeAffineMatrix(scale_, rotation_, position_);
	if (transformData_) {
		transformData_->world = worldMatrix_;
		transformData_->wvp = Multiply(worldMatrix_, viewProjection);
	}
}

void Object3d::Draw(ID3D12GraphicsCommandList* commandList) const {
	if (!model_ || !transformData_) { return; }
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
	model_->Draw(commandList);
}
