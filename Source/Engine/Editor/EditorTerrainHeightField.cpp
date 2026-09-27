#include "EditorTerrainHeightField.h"

#include "EditorSharedState.h"
#include "StringUtility.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {
	// Path ごとの読み込み結果。読めなかった Path も IsValid()==false のまま保持し、
	// 毎フレーム画像デコードを再試行しないようにする。
	std::map<std::string, EditorTerrainHeightField>& GetHeightFieldCache() {
		static std::map<std::string, EditorTerrainHeightField> heightFieldCache;
		return heightFieldCache;
	}
}

const EditorTerrainHeightField* EditorTerrainHeightField::Acquire(const std::string& heightMapAssetPath) {
	if (heightMapAssetPath.empty()) {
		return nullptr;
	}

	std::map<std::string, EditorTerrainHeightField>& cache = GetHeightFieldCache();
	const auto cachedIterator = cache.find(heightMapAssetPath);

	if (cachedIterator != cache.end()) {
		return cachedIterator->second.IsValid() ? &cachedIterator->second : nullptr;
	}

	EditorTerrainHeightField heightField{};
	heightField.LoadFromFile(heightMapAssetPath);
	const auto insertedIterator = cache.emplace(heightMapAssetPath, std::move(heightField)).first;
	return insertedIterator->second.IsValid() ? &insertedIterator->second : nullptr;
}

void EditorTerrainHeightField::Invalidate(const std::string& heightMapAssetPath) {
	GetHeightFieldCache().erase(heightMapAssetPath);
}

bool EditorTerrainHeightField::LoadFromFile(const std::string& heightMapAssetPath) {
	// HeightMap は色ではなく高さなので、sRGB 変換と Mipmap 生成を行わずに読む。
	// forceSrgb=true のまま読むと、同じ画像でも見た目の高さがシェーダ側とずれる。
	DirectX::ScratchImage loadedImage = EditorSharedState::LoadTexture(
		ConvertString(heightMapAssetPath), false, false, 0);
	const DirectX::Image* image = loadedImage.GetImage(0u, 0u, 0u);

	if (image == nullptr || image->pixels == nullptr || image->width == 0u || image->height == 0u) {
		return false;
	}

	// 形式差を吸収するため、いったん 32bit float RGBA へ揃えてから R成分を高さとして使う。
	DirectX::ScratchImage convertedImage{};

	if (FAILED(DirectX::Convert(
			*image,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DirectX::TEX_FILTER_DEFAULT,
			DirectX::TEX_THRESHOLD_DEFAULT,
			convertedImage))) {
		return false;
	}

	const DirectX::Image* floatImage = convertedImage.GetImage(0u, 0u, 0u);

	if (floatImage == nullptr || floatImage->pixels == nullptr) {
		return false;
	}

	width_ = static_cast<int32_t>(floatImage->width);
	height_ = static_cast<int32_t>(floatImage->height);
	heights_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0.0f);

	for (int32_t rowIndex = 0; rowIndex < height_; rowIndex++) {
		const float* rowPixels = reinterpret_cast<const float*>(
			floatImage->pixels + static_cast<size_t>(rowIndex) * floatImage->rowPitch);

		for (int32_t columnIndex = 0; columnIndex < width_; columnIndex++) {
			heights_[static_cast<size_t>(rowIndex) * static_cast<size_t>(width_) +
				static_cast<size_t>(columnIndex)] =
				(std::clamp)(rowPixels[static_cast<size_t>(columnIndex) * 4u], 0.0f, 1.0f);
		}
	}

	return true;
}

float EditorTerrainHeightField::SampleNormalized(float u, float v) const {
	if (!IsValid()) {
		return 0.0f;
	}

	// Shader の SamplerState と同じく、UV を 0〜1 へ丸めてから Texel 中心基準で線形補間する。
	const float clampedU = (std::clamp)(u, 0.0f, 1.0f);
	const float clampedV = (std::clamp)(v, 0.0f, 1.0f);
	const float texelX = clampedU * static_cast<float>(width_) - 0.5f;
	const float texelY = clampedV * static_cast<float>(height_) - 0.5f;
	const float flooredX = std::floor(texelX);
	const float flooredY = std::floor(texelY);
	const float fractionX = texelX - flooredX;
	const float fractionY = texelY - flooredY;

	auto readTexel = [this](int32_t x, int32_t y) {
		const int32_t clampedX = (std::clamp)(x, 0, width_ - 1);
		const int32_t clampedY = (std::clamp)(y, 0, height_ - 1);
		return heights_[static_cast<size_t>(clampedY) * static_cast<size_t>(width_) +
			static_cast<size_t>(clampedX)];
	};

	const int32_t baseX = static_cast<int32_t>(flooredX);
	const int32_t baseY = static_cast<int32_t>(flooredY);
	const float topLeft = readTexel(baseX, baseY);
	const float topRight = readTexel(baseX + 1, baseY);
	const float bottomLeft = readTexel(baseX, baseY + 1);
	const float bottomRight = readTexel(baseX + 1, baseY + 1);
	const float topRow = topLeft + (topRight - topLeft) * fractionX;
	const float bottomRow = bottomLeft + (bottomRight - bottomLeft) * fractionX;
	return topRow + (bottomRow - topRow) * fractionY;
}

float EditorTerrainHeightField::SampleLocalHeight(
	float localX,
	float localZ,
	const Vector2& areaSize,
	float heightScale) const {
	const float sizeX = (std::max)(areaSize.x, 1.0f);
	const float sizeZ = (std::max)(areaSize.y, 1.0f);
	// ApplyTerrainHeight と同じ式: saturate(localXZ / terrainSize + 0.5)
	const float u = (std::clamp)(localX / sizeX + 0.5f, 0.0f, 1.0f);
	const float v = (std::clamp)(localZ / sizeZ + 0.5f, 0.0f, 1.0f);
	return (SampleNormalized(u, v) - 0.5f) * (std::max)(heightScale, 0.0f);
}
