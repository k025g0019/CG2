#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// EditorTerrainHeightField の高さ計算が、頂点シェーダ
// (Assets/Shaders/Common/SurfaceDeformation.hlsli::ApplyTerrainHeight) と一致するかを確認する。
// 画像デコードやDirectXTexに依存せず、式そのものだけを検証する。
//   heightUv    = saturate(localXZ / areaSize + 0.5)
//   localHeight = (Sample(heightUv) - 0.5) * heightScale
// この一致が崩れると「見た目は山、当たり判定は別の形」になり、地形ゲームが成立しなくなる。

namespace {
	int32_t g_failureCount = 0;

	void Check(bool condition, const char* label) {
		if (!condition) {
			std::cout << "FAIL: " << label << "\n";
			g_failureCount++;
		}
	}

	void CheckNear(float actual, float expected, float tolerance, const char* label) {
		if (std::fabs(actual - expected) > tolerance) {
			std::cout << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
			g_failureCount++;
		}
	}

	// EditorTerrainHeightField::SampleNormalized と同じ bilinear 補間。
	struct HeightField {
		std::vector<float> heights;
		int32_t width = 0;
		int32_t height = 0;

		float ReadTexel(int32_t x, int32_t y) const {
			const int32_t clampedX = (std::clamp)(x, 0, width - 1);
			const int32_t clampedY = (std::clamp)(y, 0, height - 1);
			return heights[static_cast<size_t>(clampedY) * static_cast<size_t>(width) +
				static_cast<size_t>(clampedX)];
		}

		float SampleNormalized(float u, float v) const {
			const float clampedU = (std::clamp)(u, 0.0f, 1.0f);
			const float clampedV = (std::clamp)(v, 0.0f, 1.0f);
			const float texelX = clampedU * static_cast<float>(width) - 0.5f;
			const float texelY = clampedV * static_cast<float>(height) - 0.5f;
			const float flooredX = std::floor(texelX);
			const float flooredY = std::floor(texelY);
			const float fractionX = texelX - flooredX;
			const float fractionY = texelY - flooredY;
			const int32_t baseX = static_cast<int32_t>(flooredX);
			const int32_t baseY = static_cast<int32_t>(flooredY);
			const float topRow = ReadTexel(baseX, baseY) +
				(ReadTexel(baseX + 1, baseY) - ReadTexel(baseX, baseY)) * fractionX;
			const float bottomRow = ReadTexel(baseX, baseY + 1) +
				(ReadTexel(baseX + 1, baseY + 1) - ReadTexel(baseX, baseY + 1)) * fractionX;
			return topRow + (bottomRow - topRow) * fractionY;
		}

		float SampleLocalHeight(float localX, float localZ, float sizeX, float sizeZ, float heightScale) const {
			const float u = (std::clamp)(localX / (std::max)(sizeX, 1.0f) + 0.5f, 0.0f, 1.0f);
			const float v = (std::clamp)(localZ / (std::max)(sizeZ, 1.0f) + 0.5f, 0.0f, 1.0f);
			return (SampleNormalized(u, v) - 0.5f) * (std::max)(heightScale, 0.0f);
		}
	};
}

int main() {
	// 4x4 の Height Map。左半分が低地(0.0)、右半分が高地(1.0)。
	HeightField heightField{};
	heightField.width = 4;
	heightField.height = 4;
	heightField.heights = {
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f};

	constexpr float kAreaSize = 100.0f;
	constexpr float kHeightScale = 20.0f;

	// 1. 高さ0.0の領域は -0.5 * heightScale、1.0の領域は +0.5 * heightScale になる。
	//    シェーダの (height - 0.5) * heightScale と一致すること。
	const float lowHeight = heightField.SampleLocalHeight(-45.0f, 0.0f, kAreaSize, kAreaSize, kHeightScale);
	const float highHeight = heightField.SampleLocalHeight(45.0f, 0.0f, kAreaSize, kAreaSize, kHeightScale);
	CheckNear(lowHeight, -0.5f * kHeightScale, 0.01f, "low area height");
	CheckNear(highHeight, 0.5f * kHeightScale, 0.01f, "high area height");

	// 2. 低地と高地の高低差が heightScale と一致する。
	CheckNear(highHeight - lowHeight, kHeightScale, 0.01f, "height difference equals heightScale");

	// 3. 境界は線形補間され、中間の高さになる(段差ではない)。
	const float middleHeight = heightField.SampleLocalHeight(0.0f, 0.0f, kAreaSize, kAreaSize, kHeightScale);
	Check(middleHeight > lowHeight && middleHeight < highHeight, "boundary is interpolated");

	// 4. 範囲外はClampされ、端の高さを返す(範囲外で高さが跳ねない)。
	const float farOutsideHeight =
		heightField.SampleLocalHeight(9999.0f, 0.0f, kAreaSize, kAreaSize, kHeightScale);
	CheckNear(farOutsideHeight, highHeight, 0.01f, "outside is clamped to edge");

	// 5. heightScale=0 は完全に平坦(高さ0)。旧来の平地Sceneと同じ結果になる。
	const float flatHeight = heightField.SampleLocalHeight(45.0f, 0.0f, kAreaSize, kAreaSize, 0.0f);
	CheckNear(flatHeight, 0.0f, 0.0001f, "zero heightScale is flat");

	// 6. Collider格子の頂点が、同じ式で必ず有効な範囲に収まる。
	constexpr int32_t kGridResolution = 32;
	float minimumHeight = 1e9f;
	float maximumHeight = -1e9f;

	for (int32_t rowIndex = 0; rowIndex <= kGridResolution; rowIndex++) {
		for (int32_t columnIndex = 0; columnIndex <= kGridResolution; columnIndex++) {
			const float localX =
				(static_cast<float>(columnIndex) / kGridResolution - 0.5f) * kAreaSize;
			const float localZ =
				(static_cast<float>(rowIndex) / kGridResolution - 0.5f) * kAreaSize;
			const float sampled =
				heightField.SampleLocalHeight(localX, localZ, kAreaSize, kAreaSize, kHeightScale);
			minimumHeight = (std::min)(minimumHeight, sampled);
			maximumHeight = (std::max)(maximumHeight, sampled);
			Check(std::isfinite(sampled), "grid vertex height is finite");
		}
	}

	CheckNear(minimumHeight, -0.5f * kHeightScale, 0.01f, "grid minimum height");
	CheckNear(maximumHeight, 0.5f * kHeightScale, 0.01f, "grid maximum height");

	if (g_failureCount != 0) {
		std::cout << "FAILED: " << g_failureCount << " check(s)\n";
		return EXIT_FAILURE;
	}

	std::cout << "OK: terrain height matches the vertex shader formula "
	          << "(collider will follow the visible terrain)\n";
	return EXIT_SUCCESS;
}
