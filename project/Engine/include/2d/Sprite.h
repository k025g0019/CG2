#pragma once

#include <cstdint>
#include <string>
#include <d3d12.h>
#include <wrl.h>

#include "math/Matrix.h"
#include "math/Vector.h"

class SpriteCommon;
class TextureManager;

// 1枚分の表示設定と描画用バッファを持つ。画像本体は TextureManager が共有する。
class Sprite {
public:
	Sprite() = default;
	Sprite(const Sprite&) = delete;
	Sprite& operator=(const Sprite&) = delete;

	void Initialize(SpriteCommon* spriteCommon, TextureManager* textureManager,
	                const std::string& textureFilePath);
	void Update();
	void Draw() const;
	// GPUがこのスプライトの描画を終えてから呼ぶ。
	void Finalize();

	void SetPosition(const Vector2& position);
	const Vector2& GetPosition() const { return position_; }
	// 回転角はラジアン。画面のY軸が下向きなので、正の値で時計回りに回る。
	void SetRotation(float rotation);
	float GetRotation() const { return rotation_; }
	void SetColor(const Vector4& color);
	const Vector4& GetColor() const { return color_; }
	void SetSize(const Vector2& size);
	const Vector2& GetSize() const { return size_; }
	void SetAnchorPoint(const Vector2& anchorPoint);
	const Vector2& GetAnchorPoint() const { return anchorPoint_; }
	void SetIsFlipX(bool isFlipX) { isFlipX_ = isFlipX; }
	bool GetIsFlipX() const { return isFlipX_; }
	void SetIsFlipY(bool isFlipY) { isFlipY_ = isFlipY; }
	bool GetIsFlipY() const { return isFlipY_; }

	// 切り抜きは画像左上を原点とするピクセル単位。表示サイズとは独立する。
	void SetTextureRect(const Vector2& leftTop, const Vector2& size);
	void SetTextureLeftTop(const Vector2& leftTop);
	const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
	void SetTextureSize(const Vector2& size);
	const Vector2& GetTextureSize() const { return textureSize_; }

	// 画像変更時は切り抜き範囲を画像全体へ戻す。表示サイズは維持する。
	void SetTexture(uint32_t textureHandle);
	void SetTextureFilePath(const std::string& textureFilePath);
	uint32_t GetTextureHandle() const { return textureHandle_; }
	const std::string& GetTextureFilePath() const;

private:
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
	};
	void CreateResources();
	void UpdateVertices();

	SpriteCommon* spriteCommon_ = nullptr;
	TextureManager* textureManager_ = nullptr;
	uint32_t textureHandle_ = 0;
	Vector2 position_{};
	float rotation_ = 0.0f;
	Vector4 color_{1.0f, 1.0f, 1.0f, 1.0f};
	Vector2 size_{};
	Vector2 anchorPoint_{};
	bool isFlipX_ = false;
	bool isFlipY_ = false;
	Vector2 textureLeftTop_{};
	Vector2 textureSize_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> colorResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
	VertexData* vertexData_ = nullptr;
	Vector4* colorData_ = nullptr;
	Matrix4x4* transformData_ = nullptr;
};
