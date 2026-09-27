#pragma once

#include "VisionTypes.h"

//================================================================
// 画像認識 Backend 抽象(仕様書 28 項)
//================================================================
// ONNX Runtime / OpenCV / MediaPipe / 独自モデルへ差し替えても
// ゲーム側は Backend を意識しない(仕様書 29 項)。

class IVisionBackend {
public:
	virtual ~IVisionBackend() = default;

	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual bool ApplyConfig(const VisionConfig& config) = 0;  // Mode / Model / しきい値を反映する。
	virtual void ProcessFrame(const ImageFrame& frame) = 0;     // 1 フレーム推論する。
	virtual VisionResult GetResult() = 0;                       // 直近の結果を返す。

	// そのモードに対応していないなら false。対応しない機能は勝手に別処理へ
	// 置き換えず、VisionResult の state を Unavailable にする(仕様書 85 項)。
	virtual bool SupportsMode(VisionRecognitionMode mode) const = 0;
	virtual const char* GetName() const = 0;
	virtual ExternalFeatureError GetLastError() const = 0;
};
