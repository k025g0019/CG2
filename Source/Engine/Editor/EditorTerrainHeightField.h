#pragma once

#include "Source/Engine/Core/EditorCommonTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

// Terrain の高さは頂点シェーダ(SurfaceDeformation.hlsli::ApplyTerrainHeight)で
// HeightMap を Sample して作られるため、そのままでは CPU 側に高さが存在しない。
// 物理判定・接地判定・Script からの高さ取得はすべて CPU 側の値を必要とするので、
// ここで同じ HeightMap を CPU へ展開し、シェーダと同じ式で高さを再現する。
//
// シェーダ側の式:
//   heightUv   = saturate(localXZ / areaSize + 0.5)
//   localHeight = (SampleLevel(heightUv) - 0.5) * heightScale
// Sampler は線形補間のため、CPU 側も bilinear で合わせる。
class EditorTerrainHeightField {
public:
	// HeightMap を CPU へ読み込む。同じ Path は再利用し、読めない場合は false を返す。
	// 読み込みに失敗した Path も記録し、毎フレーム再読み込みしない。
	static const EditorTerrainHeightField* Acquire(const std::string& heightMapAssetPath);

	// 外部で HeightMap を差し替えた時に、次回 Acquire で読み直させる。
	static void Invalidate(const std::string& heightMapAssetPath);

	bool IsValid() const { return width_ > 0 && height_ > 0 && !heights_.empty(); }
	int32_t GetWidth() const { return width_; }
	int32_t GetHeight() const { return height_; }

	// 0〜1 に正規化した UV から高さ(0〜1)を bilinear で取る。
	float SampleNormalized(float u, float v) const;

	// Terrain ローカル座標 (x, z) の、Terrain 原点からの高さ(m)を返す。
	// areaSize は Terrain の XZ サイズ、heightScale は最大高低差。
	float SampleLocalHeight(
		float localX,
		float localZ,
		const Vector2& areaSize,
		float heightScale) const;

private:
	bool LoadFromFile(const std::string& heightMapAssetPath);

	std::vector<float> heights_;  // 行優先。値域0〜1
	int32_t width_ = 0;
	int32_t height_ = 0;
};

#pragma warning(pop)
