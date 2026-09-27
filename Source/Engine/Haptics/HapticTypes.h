#pragma once

#include "Source/Engine/External/ExternalFeature.h"

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Haptics(触覚)モジュールの公開型
//================================================================
// FeelKit などの外部 SDK の型は一切含めない。Game / Component 側は
// この型と HapticSystem だけを使う(仕様書 59〜81 項)。

// 振動 Handle。0 は無効。Play の戻り値を使って Runtime 中の値を変える。
using HapticHandle = uint32_t;
constexpr HapticHandle kInvalidHapticHandle = 0u;

// 振動の時間変化。Clip と Runtime 再生の両方で使う。
enum class HapticPattern : int32_t {
	Constant = 0,  // 一定強度。
	Pulse,         // 短い振動を周期的に繰り返す。
	RampUp,        // 徐々に強くする。
	RampDown,      // 徐々に弱くする。
	Burst,         // 立ち上がり最大から急減衰。着弾・衝突向け。
};

// 左右どちらへ出すか。片側 Motor しか無い Device では Both と同じ扱いになる。
enum class HapticChannel : int32_t {
	Both = 0,
	Left,
	Right,
};

// Device の接続状態(仕様書 77 項)。
enum class HapticDeviceState : int32_t {
	Unavailable = 0,  // Backend 自体が無い。
	Disconnected,     // Backend はあるが Device が繋がっていない。
	Connected,        // 再生できる。
	Error,            // 失敗した。
};

const char* ToDisplayString(HapticPattern pattern);
const char* ToDisplayString(HapticChannel channel);
const char* ToDisplayString(HapticDeviceState state);

// Backend へ渡す 1 回分の振動指示。
struct HapticData {
	float intensity = 1.0f;        // 0.0〜1.0 の強度。
	float frequency = 0.0f;        // Pulse / Burst の 1 秒あたり回数。0 は Pattern 既定。
	float durationSeconds = 0.12f;  // 継続時間。Loop 時は 1 周の長さ。
	float playbackSpeed = 1.0f;    // 時間進行の倍率。
	HapticPattern pattern = HapticPattern::Constant;
	HapticChannel channel = HapticChannel::Both;
	bool isLooping = false;
};

// Asset として扱う振動データ(仕様書 64〜65 項)。
struct HapticClipData {
	std::string name;         // 表示名。空なら File 名を使う。
	std::string assetPath;    // 読み込み元 .haptic パス。
	float durationSeconds = 0.20f;
	float intensity = 1.0f;
	float frequency = 12.0f;
	HapticPattern pattern = HapticPattern::Constant;
	HapticChannel channel = HapticChannel::Both;
	bool isLooping = false;

	HapticData ToHapticData() const {
		HapticData data{};
		data.intensity = intensity;
		data.frequency = frequency;
		data.durationSeconds = durationSeconds;
		data.pattern = pattern;
		data.channel = channel;
		data.isLooping = isLooping;
		return data;
	}
};

// Device の情報。Debug Window と Inspector が表示する。
struct HapticDeviceInfo {
	HapticDeviceState state = HapticDeviceState::Unavailable;
	std::string deviceName;   // "XInput Gamepad" など Backend が付ける名前。
	std::string backendName;  // "FeelKit" など。
};

// Audio 解析結果。Audio Reactive Haptics の入力(仕様書 72〜73 項)。
struct HapticAudioAnalysis {
	bool isValid = false;
	float durationSeconds = 0.0f;
	float averageAmplitude = 0.0f;
	float peakAmplitude = 0.0f;
	float lowBandEnergy = 0.0f;
	float highBandEnergy = 0.0f;
	float attackStrength = 0.0f;
};

// Debug 表示用の再生状態(仕様書 81 項)。
struct HapticPlaybackStatus {
	HapticHandle handle = kInvalidHapticHandle;
	std::string clipName;
	float intensity = 0.0f;
	float frequency = 0.0f;
	float remainingSeconds = 0.0f;
	bool isLooping = false;
	int32_t ownerGameObjectId = -1;
};

#pragma warning(pop)
