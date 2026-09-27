#pragma once

#include "IVisionBackend.h"

#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// ONNX Runtime + ユーザー指定モデルの Vision Backend
//================================================================
// 物体検出(YOLO 系出力)と画像分類に対応する。顔検出は「顔として学習した
// 検出モデル」を指定した場合に使える。顔ランドマークと頭部方向は専用モデル
// 構造に依存するため、このモジュールでは対応せず Unavailable を返す
// (勝手に別処理へ置き換えない / 仕様書 85 項)。
//
// 対応する出力レイアウト:
//   ・分類     [1, クラス数]
//   ・検出(v8) [1, 4 + クラス数, ボックス数]
//   ・検出(v5) [1, ボックス数, 5 + クラス数]

class OnnxVisionBackend final : public IVisionBackend {
public:
	OnnxVisionBackend() = default;
	~OnnxVisionBackend() override;

	OnnxVisionBackend(const OnnxVisionBackend&) = delete;
	OnnxVisionBackend& operator=(const OnnxVisionBackend&) = delete;

	bool Initialize() override;
	void Shutdown() override;
	bool ApplyConfig(const VisionConfig& config) override;
	void ProcessFrame(const ImageFrame& frame) override;
	VisionResult GetResult() override;
	bool SupportsMode(VisionRecognitionMode mode) const override;
	const char* GetName() const override;
	ExternalFeatureError GetLastError() const override;

private:
	struct Impl;  // Ort の型を Header へ出さないため隠す。

	bool EnsureSession();  // モデル未読込なら読み込む。
	void LoadLabels();
	// BGRA フレームをモデル入力サイズへ Letterbox 縮小し、RGB float NCHW へ詰める。
	void BuildInputTensor(const ImageFrame& frame);
	void ParseClassification(const float* outputData, size_t elementCount);
	void ParseDetection(
		const float* outputData,
		int64_t firstDimension,
		int64_t secondDimension,
		bool hasObjectness);
	std::string GetLabelName(int32_t classIndex) const;

	Impl* impl_ = nullptr;
	VisionConfig config_{};
	VisionResult result_{};
	ExternalFeatureError lastError_{};
	std::vector<float> inputTensorData_;
	std::vector<std::string> labels_;
	std::string loadedModelPath_;
	int32_t inputWidth_ = 640;
	int32_t inputHeight_ = 640;
	float letterboxScale_ = 1.0f;
	float letterboxOffsetX_ = 0.0f;
	float letterboxOffsetY_ = 0.0f;
	int32_t sourceWidth_ = 0;
	int32_t sourceHeight_ = 0;
	bool isInitialized_ = false;
};

#pragma warning(pop)
