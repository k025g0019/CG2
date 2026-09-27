#pragma once

#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

enum class ProjectWindowMode : int32_t {
	Windowed = 0,             // 枠付きウィンドウ。
	BorderlessFullscreen = 1,  // 枠なしでモニター全体へ広げる。Alt+Tab が安定するため排他Fullscreenは使わない。
};

// Project 単位の実行設定。Editor の Play と Standalone Player の両方が同じ値を使う。
// 「UIにあるのにRuntimeへ反映されない設定」を作らないため、ここには実際に接続済みの項目だけを置く。
struct ProjectSettingsData {
	int32_t gameWidth = 1920;   // 起動時のクライアント領域幅。SwapChain 解像度もこの値から決まる。
	int32_t gameHeight = 1080;  // 起動時のクライアント領域高さ。
	ProjectWindowMode windowMode = ProjectWindowMode::Windowed;
	bool vsyncEnabled = true;    // false なら Present の同期間隔を 0 にする。
	int32_t frameRateLimit = 0;  // 0 = 無制限。1以上ならフレーム末尾で待って上限を守る。

	// Audio の初期音量。Play 開始時に EditorAudioManager へ流し込む。
	float masterVolume = 1.0f;
	float sfxVolume = 1.0f;
	float bgmVolume = 0.70f;
	float ambienceVolume = 0.80f;
	float uiVolume = 1.0f;
	float voiceVolume = 1.0f;

	// Gamepad 入力の調整。GamepadInput が実際に読み取り値へ適用する。
	float gamepadStickDeadZone = 0.24f;      // これ未満のStick傾きは0として捨てる(XInput既定値相当)。
	float gamepadTriggerThreshold = 0.12f;   // Trigger をボタンとして扱うときのしきい値。
	float gamepadLookSensitivity = 1.0f;     // Stick 値へ掛ける感度。

	// Online Services。Play 開始時に OnlineService へ流し込む(外部連携仕様書 51 項)。
	// 秘密情報はここへ置かない。重要な認証情報は Cloudflare Worker 側が持つ。
	bool onlineServicesEnabled = false;
	std::string onlineProviderName = "Cloudflare";
	std::string onlineApiBaseUrl;             // 本番用 Worker の URL。
	std::string onlineDevelopmentBaseUrl;     // 開発用 Worker の URL。空なら本番 URL を使う。
	std::string onlineGameId;
	std::string onlineClientKey;              // クライアントが持ってよい公開鍵だけ。
	int32_t onlineEnvironment = 0;            // 0=Development, 1=Production
	int32_t onlineTimeoutSeconds = 10;
	int32_t onlineMaximumPendingRequests = 64;

	// 破壊破片のうち同時にRigidbody化できるScene全体の上限。EditorBlastDestructionManagerが数える。
	// これを超えた破片はCluster追従かGPU破片へ回る。0なら無制限。
	int32_t maxScenePhysicsDebris = 100;

	// 振動全体の強さ。HapticSystem の Master Scale へ入れる。
	float hapticMasterIntensity = 1.0f;
};

// ProjectSettings/ProjectSettings.cg2 を読み書きする。
// 既存の EditorSettings.cg2 / GameBuildSettings.cg2 と同じ
// 「1行1項目、Key|Value」形式に合わせている。
class ProjectSettings {
public:
	static ProjectSettings& Get();

	ProjectSettings(const ProjectSettings&) = delete;
	ProjectSettings& operator=(const ProjectSettings&) = delete;

	void Load();  // 未読込なら読む。File が無ければ既定値のまま。
	void Save() const;

	ProjectSettingsData& GetMutableData();
	const ProjectSettingsData& GetData() const;

private:
	ProjectSettings() = default;

	mutable ProjectSettingsData data_{};
	mutable bool isLoaded_ = false;
};

#pragma warning(pop)
