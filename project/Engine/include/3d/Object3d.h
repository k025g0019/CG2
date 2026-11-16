#pragma once

#include <d3d12.h>
#include <memory>
#include <wrl.h>
#include "math/Matrix.h"
#include "math/Vector.h"

class Model;

// モデルは共有し、座標と GPU 用のワールド行列はオブジェクトごとに持つ。
class Object3d {
public:
	bool Initialize(ID3D12Device* device, std::shared_ptr<const Model> model);
	void SetModel(std::shared_ptr<const Model> model);
	void SetScale(const Vector3& value) { scale_ = value; }
	void SetRotation(const Vector3& value) { rotation_ = value; }
	void SetPosition(const Vector3& value) { position_ = value; }
	const Vector3& GetPosition() const { return position_; }
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	const Model* GetModel() const { return model_.get(); }
	void Update(const Matrix4x4& viewProjection);
	void Draw(ID3D12GraphicsCommandList* commandList) const;

private:
	struct TransformationMatrix {
		Matrix4x4 wvp;
		Matrix4x4 world;
	};

	Vector3 scale_{1.0f, 1.0f, 1.0f};
	Vector3 rotation_{0.0f, 0.0f, 0.0f};
	Vector3 position_{1.0f, 1.0f, 1.0f};
	Matrix4x4 worldMatrix_{};
	std::shared_ptr<const Model> model_;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	TransformationMatrix* transformData_ = nullptr;
};
