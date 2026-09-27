#pragma once

#include "HapticTypes.h"

//================================================================
// 触覚 Device 抽象(仕様書 75 項)
//================================================================
// FeelKit を直接固定せず、この Interface だけを HapticSystem が使う。
// 別 Device を追加しても CG2Engine 側の API は変えない。

class IHapticBackend {
public:
	virtual ~IHapticBackend() = default;

	virtual bool Initialize() = 0;  // Device を開く。使えない場合は false。
	virtual void Shutdown() = 0;    // Device を閉じる。
	virtual void Update(float deltaTime) = 0;  // Device 側の時間進行。

	virtual void Play(const HapticData& data) = 0;  // 1 回分の振動を出す。
	virtual void Stop() = 0;                        // 今出している振動を止める。
	virtual void SetIntensity(float value) = 0;     // 全体強度(Master Scale)を変える。

	virtual HapticDeviceInfo GetDeviceInfo() const = 0;  // 接続状態を返す。
	virtual bool RefreshDevice() = 0;                    // 再接続を試す。
	virtual ExternalFeatureError GetLastError() const = 0;

	// Audio Reactive Haptics 用。対応しない Backend は false を返すだけでよい。
	virtual bool TryAnalyzeAudioFile(const std::string& audioFilePath, HapticAudioAnalysis& outAnalysis) {
		static_cast<void>(audioFilePath);
		static_cast<void>(outAnalysis);
		return false;
	}

	// Backend 固有の「音から振動」再生。対応しない Backend は false を返す。
	virtual bool TryPlayFromAudioFile(const std::string& audioFilePath, const HapticData& data) {
		static_cast<void>(audioFilePath);
		static_cast<void>(data);
		return false;
	}
};
