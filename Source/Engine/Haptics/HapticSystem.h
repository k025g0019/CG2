#pragma once

#include "IHapticBackend.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// HapticSystem(仕様書 61 項)
//================================================================
// 触覚再生、停止、強度変更、Device 管理、Pattern 管理を 1 か所へ集める。
// Game / Component 側はこのクラスと HapticTypes だけを使い、FeelKit へは
// 直接触らない。Device が無い場合は Play を黙って捨て、ゲームロジックは
// 止めない(仕様書 78 項)。

class HapticSystem {
public:
	static HapticSystem& Get();

	HapticSystem(const HapticSystem&) = delete;
	HapticSystem& operator=(const HapticSystem&) = delete;

	//============================================================
	// Device / Backend
	//============================================================
	void SetBackend(std::unique_ptr<IHapticBackend> backend);  // Backend を差し替える。
	bool Initialize();  // Backend を開く。Backend 未設定なら Unavailable のまま false。
	void Shutdown();
	void Update(float deltaTime);  // 再生中 Clip の時間を進め、Device へ合成強度を出す。

	ExternalFeatureState GetState() const;
	ExternalFeatureError GetLastError() const;
	HapticDeviceInfo GetDeviceInfo() const;
	bool RefreshDevice();  // 再接続を試す。
	const char* GetBackendName() const;

	//============================================================
	// 再生 API(仕様書 66〜70 項)
	//============================================================
	HapticHandle Play(const HapticData& data, int32_t ownerGameObjectId = -1, const std::string& displayName = std::string());
	HapticHandle PlayClip(const HapticClipData& clip, int32_t ownerGameObjectId = -1);
	HapticHandle PlayClipAsset(const std::string& clipAssetPath, int32_t ownerGameObjectId = -1);
	// Audio File から Backend 固有変換で再生する。対応していなければ data のまま再生する。
	HapticHandle PlayFromAudioFile(const std::string& audioFilePath, const HapticData& data, int32_t ownerGameObjectId = -1);

	bool Stop(HapticHandle handle);
	void StopGameObject(int32_t ownerGameObjectId);
	void StopAll();
	bool IsPlaying(HapticHandle handle) const;

	bool SetIntensity(HapticHandle handle, float intensity);
	bool SetFrequency(HapticHandle handle, float frequency);
	bool SetPlaybackSpeed(HapticHandle handle, float playbackSpeed);
	bool SetLooping(HapticHandle handle, bool isLooping);

	void SetMasterIntensity(float masterIntensity);  // 全体倍率。Project の振動量調整に使う。
	float GetMasterIntensity() const;

	//============================================================
	// Clip Asset(仕様書 64〜65 項)
	//============================================================
	bool LoadClip(const std::string& clipAssetPath, HapticClipData& outClip);  // 読込済みなら Cache を返す。
	bool SaveClip(const std::string& clipAssetPath, const HapticClipData& clip);
	void ClearClipCache();

	//============================================================
	// Audio / Physics 連携(仕様書 72〜74 項)
	//============================================================
	bool TryAnalyzeAudioFile(const std::string& audioFilePath, HapticAudioAnalysis& outAnalysis);
	// Audio 解析結果から強度を作る。frequencyRange は 0=低域 / 1=全域 / 2=高域。
	static float MakeIntensityFromAudio(
		const HapticAudioAnalysis& analysis,
		int32_t frequencyRange,
		float sensitivity,
		float intensityScale);
	// 衝突 Impulse から強度を作る。
	static float MakeIntensityFromImpulse(float impulse, float maximumImpulse);

	//============================================================
	// Debug 表示(仕様書 81 項)
	//============================================================
	float GetCurrentOutputIntensity() const;
	void GetPlaybackStatus(std::vector<HapticPlaybackStatus>& outStatus) const;
	int32_t GetActiveVoiceCount() const;

private:
	HapticSystem() = default;
	~HapticSystem() = default;

	struct Voice {
		HapticHandle handle = kInvalidHapticHandle;
		HapticData data{};
		std::string displayName;
		int32_t ownerGameObjectId = -1;
		float elapsedSeconds = 0.0f;
		bool isFinished = false;
	};

	float EvaluateVoiceLevel(const Voice& voice) const;  // Pattern から現在の強度を出す。
	Voice* FindVoice(HapticHandle handle);
	const Voice* FindVoice(HapticHandle handle) const;
	void SendToDevice(float leftLevel, float rightLevel, float deltaTime);

	std::unique_ptr<IHapticBackend> backend_;
	std::vector<Voice> voices_;
	std::unordered_map<std::string, HapticClipData> clipCache_;
	ExternalFeatureState state_ = ExternalFeatureState::Unavailable;
	ExternalFeatureError lastError_{};
	HapticHandle nextHandle_ = 1u;
	float masterIntensity_ = 1.0f;
	float currentOutputIntensity_ = 0.0f;  // 直近フレームに Device へ出した強度。
	float deviceRefreshTimer_ = 0.0f;      // 同じ強度を出し続ける時の再送間隔。
	bool isInitialized_ = false;
};

#pragma warning(pop)
