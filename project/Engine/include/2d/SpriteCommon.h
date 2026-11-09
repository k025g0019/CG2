#pragma once

#include "base/DirectXCommon.h"
#include "math/Matrix.h"
#include "math/Vector.h"

// 全スプライトで共有するシェーダー・PSO・画面座標系を管理する。
class SpriteCommon {
public:
	void Initialize(DirectXCommon* dxCommon);
	// 3D描画後、スプライトをまとめて描く直前に呼ぶ。
	void CommonSetting();
	void Finalize();
	DirectXCommon* GetDxCommon() const { return dxCommon_; }
	void SetScreenSize(const Vector2& screenSize);
	const Vector2& GetScreenSize() const { return screenSize_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

private:
	void rootSignature();
	void graphicsPipeline();
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const wchar_t* path, const wchar_t* profile);

	DirectXCommon* dxCommon_ = nullptr;
	Vector2 screenSize_{};
	Matrix4x4 projectionMatrix_{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
