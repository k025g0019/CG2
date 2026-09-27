#pragma once

#include "IVisionBackend.h"

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 追加ライブラリ無しで動く内蔵 Vision Backend
//================================================================
// Color Tracking(仕様書 27 項)と Motion Detection(仕様書 26 項)を
// CPU で計算する。物体検出・分類・顔検出・ランドマーク・頭部方向は
// モデルが必要なため対応せず、Unavailable を返す(仕様書 85 項)。

class BuiltinVisionBackend final : public IVisionBackend {
public:
	BuiltinVisionBackend() = default;
	~BuiltinVisionBackend() override = default;

	bool Initialize() override;
	void Shutdown() override;
	bool ApplyConfig(const VisionConfig& config) override;
	void ProcessFrame(const ImageFrame& frame) override;
	VisionResult GetResult() override;
	bool SupportsMode(VisionRecognitionMode mode) const override;
	const char* GetName() const override;
	ExternalFeatureError GetLastError() const override;

private:
	static constexpr int32_t kMotionGridWidth = 64;   // 動き検出用の縮小解像度。
	static constexpr int32_t kMotionGridHeight = 48;

	void ProcessColorTracking(const ImageFrame& frame);
	void ProcessMotionDetection(const ImageFrame& frame);

	VisionConfig config_{};
	VisionResult result_{};
	ExternalFeatureError lastError_{};
	std::vector<float> previousLuminance_;  // 前フレームの縮小輝度。
	bool hasPreviousLuminance_ = false;
	bool isInitialized_ = false;
};

#pragma warning(pop)
