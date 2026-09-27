#pragma once

#include "SpeechTypes.h"

//================================================================
// 音声認識 Backend 抽象(仕様書 10 項)
//================================================================
// Windows Speech API、Whisper 系、ONNX ローカルモデル、外部 API を
// 差し替えても CG2Engine 側の API は変えない(仕様書 11 項)。

class ISpeechBackend {
public:
	virtual ~ISpeechBackend() = default;

	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual void StartRecognition() = 0;
	virtual void StopRecognition() = 0;
	virtual void Update() = 0;
	virtual std::vector<SpeechResult> GetResults() = 0;  // 取り出した結果は Backend 側から消える。

	virtual bool ApplyConfig(const SpeechConfig& config) = 0;  // Mode / 言語 / Keyword を反映する。
	virtual bool IsRecognizing() const = 0;
	// 録音待機と実際の発話、録音後の推論をUIで区別するための状態。
	// 対応しないBackendはfalseのままにし、音量から状態を推測しない。
	virtual bool IsSpeaking() const { return false; }
	virtual bool IsProcessing() const { return false; }
	virtual float GetAudioLevel() const = 0;  // 0.0〜1.0。取得できない Backend は 0 を返す。
	virtual void EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const = 0;
	virtual std::string GetActiveDeviceName() const = 0;
	virtual const char* GetName() const = 0;
	virtual ExternalFeatureError GetLastError() const = 0;
};
