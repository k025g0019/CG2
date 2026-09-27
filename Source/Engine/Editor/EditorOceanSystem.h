#pragma once

#include "EditorScene.h"

#include <array>
#include <cstddef>
#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// Ocean の描画波と物理波を共有する水面サンプル
//============================================================

constexpr size_t kEditorOceanWaveCount = 16u;

struct EditorOceanSpectrumSettings {
	float waveHeight = 0.0f;
	float waveLength = 0.1f;
	float windSpeed = 0.1f;
	float waterDepth = 0.1f;
	float directionSpread = 0.0f;
	float swellStrength = 0.0f;
	float rippleScale = 0.02f;
	float rippleStrength = 0.0f;
	float secondaryWaveScale = 0.0f;
	float spectrumSeed = 0.0f;
	EditorScriptVector2 primaryDirection{1.0f, 0.0f};
	EditorScriptVector2 secondaryDirection{0.0f, 1.0f};
};

struct EditorOceanWaveParameter {
	EditorScriptVector2 direction{1.0f, 0.0f};  // 正規化済みの XZ 波方向
	float waveNumber = 0.0f;  // 2PI / 波長
	float amplitude = 0.0f;  // スペクトル重みを適用した振幅
	float angularFrequency = 0.0f;  // 有限水深の分散則から得た角周波数
	float phaseOffset = 0.0f;  // シードから固定生成した位相
};

struct EditorOceanSurfaceSample {
	bool isValid = false;  // Ocean 範囲内の水面を取得できた場合だけ true
	int32_t oceanGameObjectId = -1;  // サンプルに使った Ocean GameObject
	Vector3 position{0.0f, 0.0f, 0.0f};  // World 空間の水面位置
	Vector3 normal{0.0f, 1.0f, 0.0f};  // World 空間の水面法線
	Vector3 velocity{0.0f, 0.0f, 0.0f};  // World 空間での波粒子速度
	float foam = 0.0f;  // 0..1 の砕波・圧縮泡率。航跡や着水Effectの強度へ利用する
};

struct EditorOceanSegmentHit {
	bool isValid = false;  // 線分が水面へ到達した場合だけ true
	int32_t oceanGameObjectId = -1;  // 交差した Ocean GameObject
	Vector3 position{0.0f, 0.0f, 0.0f};  // World 空間の交点
	Vector3 normal{0.0f, 1.0f, 0.0f};  // 交点の水面法線
	Vector3 surfaceVelocity{0.0f, 0.0f, 0.0f};  // 交点の水面速度
	float distance = 0.0f;  // 線分始点から交点までの距離
	float normalizedDistance = 0.0f;  // 線分上の 0～1 位置
};

struct EditorOceanOcclusionResult {
	bool hasOcean = false;  // Query 区間を覆う Ocean Sampleを1点以上取得できた
	bool isBlocked = false;  // clearanceを含む水面より区間が下へ入った
	float minimumClearance = 0.0f;  // 区間と水面の最小符号付き距離
	float maximumSurfaceHeight = 0.0f;  // 走査点で得た最大水面World Y
	EditorOceanSegmentHit intersection{};  // 最初の水面交点
};

float GetEditorOceanElapsedTime();  // 描画と物理が同じ波位相を使うための共通時刻

EditorOceanSpectrumSettings BuildEditorOceanSpectrumSettings(
	const EditorComponent& oceanComponent);  // Component の編集値を事前計算用の安全な値へ変換

void BuildEditorOceanSpectrum(
	const EditorOceanSpectrumSettings& spectrumSettings,
	std::array<EditorOceanWaveParameter, kEditorOceanWaveCount>& waveParameters);  // 設定変更時だけ16波を生成

bool SampleEditorOceanSurface(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& worldPosition,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	EditorOceanSurfaceSample& surfaceSample);  // 指定位置を覆う Ocean の水面を返す。ID=-1 は自動検索

bool CastEditorOceanSegment(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& startPosition,
	const Vector3& endPosition,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanSegmentHit& segmentHit);  // 線分とFFT水面の最初の交点を返す

bool RaycastEditorOceanSurface(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& rayOrigin,
	const Vector3& rayDirection,
	float maximumDistance,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanSegmentHit& segmentHit);  // Ray方向を正規化してOcean線分判定へ渡す

bool QueryEditorOceanOcclusion(
	const EditorScene& editorScene,
	int32_t preferredOceanGameObjectId,
	const Vector3& startPosition,
	const Vector3& endPosition,
	float clearance,
	uint64_t surfaceSampleKey,
	float oceanElapsedTime,
	int32_t coarseStepCount,
	int32_t refinementCount,
	EditorOceanOcclusionResult& occlusionResult);  // 遮蔽判定と最小Clearanceを同時に返す

#pragma warning(pop)
