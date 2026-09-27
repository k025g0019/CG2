#pragma once

#include "Source/Engine/External/ExternalFeature.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 音声認識モジュールの公開型(仕様書 2〜14 項)
//================================================================
// Windows Speech API / Whisper / ONNX など Backend 側の型はここへ持ち込まない。

// 認識結果(仕様書 5 項)。
struct SpeechAlternative {
	std::string text;  // 同じ音声に対する別候補。
	float confidence = 0.0f;
};

struct SpeechResult {
	std::string text;        // 認識した文字列。
	float confidence = 0.0f;  // 0.0〜1.0。Backend が出せない場合は 0。
	bool isFinal = false;     // 確定結果なら true。途中結果(Hypothesis)なら false。

	// 必要に応じて保持する付加情報。
	float startSeconds = 0.0f;  // 認識開始時刻(Play 開始からの秒)。
	float endSeconds = 0.0f;    // 認識終了時刻。
	std::string language;       // 使用言語("ja-JP" など)。
	std::string backendName;    // 認識した Backend 名。
	std::string matchedKeyword;  // Keyword Mode で一致した登録語。一致なしなら空。
	std::vector<SpeechAlternative> alternatives;  // 音響認識の2位以下。未対応Backendは空。
};

// 認識モード(仕様書 6 項)。
enum class SpeechRecognitionMode : int32_t {
	Keyword = 0,     // 事前登録した言葉だけを認識する。
	SpeechToText,    // 発話内容全体を文字列へ変換する。
};

// Backend 候補(仕様書 11 項)。実際に使えるかは Backend 側が判断する。
enum class SpeechBackendKind : int32_t {
	Auto = 0,           // 使えるものを自動選択する。
	WindowsSpeechApi,   // Windows Speech API (SAPI)。
	Whisper,            // Whisper 系。
	OnnxLocalModel,     // ONNX Runtime のローカルモデル。
	None,               // 明示的に使わない。常に Unavailable を返す。
};

const char* ToDisplayString(SpeechRecognitionMode mode);
const char* ToDisplayString(SpeechBackendKind backendKind);

// Inspector から渡す設定(仕様書 12 項)。
struct SpeechConfig {
	SpeechRecognitionMode mode = SpeechRecognitionMode::Keyword;
	SpeechBackendKind backendKind = SpeechBackendKind::Auto;
	std::string language = "ja-JP";
	std::string microphoneDeviceName;  // 空なら既定のマイクを使う。
	float confidenceThreshold = 0.5f;   // これ未満の結果は捨てる。
	bool isContinuous = true;           // false なら 1 回認識したら停止する。
	std::vector<std::string> keywords;  // Keyword Mode の登録語。
	std::string modelAssetPath;         // Whisper / ONNX Backend が使うモデルパス。
	bool whisperEndOnSilence = true;  // 発話後の無音を検出したら最大録音秒を待たずに推論する。
	float whisperMaximumCaptureSeconds = 4.0f;  // 無音を検出できない時もこの秒数で区切る。
	float whisperSilenceSeconds = 0.45f;  // 発話終了とみなす連続無音秒数。
	float whisperVoiceThreshold = 0.01f;  // 発話開始/継続とみなす表示音量(0〜1)。
};

// マイク Device 一覧の 1 件。
struct SpeechDeviceInfo {
	std::string deviceName;
	std::string deviceId;
	bool isDefault = false;
};

// Editor のデバッグ表示用(仕様書 14 項)。
struct SpeechRuntimeStatus {
	ExternalFeatureState state = ExternalFeatureState::Unavailable;
	bool isMicrophoneActive = false;  // マイク入力が開いているか。
	bool isRecognizing = false;       // 現在認識中か。
	bool isSpeaking = false;          // Backendが発話区間を検出しているか。
	bool isProcessing = false;        // 録音済み音声を推論しているか。
	float audioLevel = 0.0f;          // 0.0〜1.0 の音量。
	std::string lastText;             // 直近の認識文字列。
	float lastConfidence = 0.0f;
	std::string lastKeyword;
	std::string deviceName;
	std::string backendName;
	int32_t recognizedCount = 0;
	ExternalFeatureError lastError{};
};

#pragma warning(pop)
