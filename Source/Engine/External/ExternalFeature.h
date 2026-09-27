#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 外部認識・オンライン連携機能の共通型
//================================================================
// Speech / Vision / Online / Haptics の 4 モジュールが同じ状態表現と
// 同じログ経路を使うための共通層。Backend 実装や外部 SDK の型は
// ここへ一切持ち込まない(仕様書 82〜89 項)。

// Editor から機能の生死を 1 目で判断するための共通状態。
enum class ExternalFeatureState : int32_t {
	Unavailable = 0,  // Device または Backend が使えない。勝手に別機能で代替しない。
	Ready,            // 初期化済みで開始待ち。
	Running,          // 実行中。
	Error,            // 失敗して停止した。lastError に理由が入る。
};

const char* ToString(ExternalFeatureState state);  // Debug Window 表示用の英字名。
const char* ToDisplayString(ExternalFeatureState state);  // Inspector / Debug Window 表示用の日本語名。

// 失敗理由。最低限 code と message を持つ(仕様書 88 項)。
struct ExternalFeatureError {
	int32_t code = 0;  // 0 は「エラーなし」。モジュールごとの意味は各 Backend が決める。
	std::string message;  // 人が読む理由。Console と Debug Window の両方へ出す。

	bool HasError() const {
		return code != 0 || !message.empty();
	}

	void Clear() {
		code = 0;
		message.clear();
	}
};

// 外部機能が属するモジュール。ログの発生元表示に使う。
enum class ExternalFeatureCategory : int32_t {
	Speech = 0,
	Vision,
	Online,
	Haptics,
};

const char* ToString(ExternalFeatureCategory category);

//================================================================
// 外部機能専用の Console ログ
//================================================================
// Backend は Worker Thread から呼ぶことがあるため、直接 Console の
// std::vector へ push せず、ここでキューへ積んで Flush() で Main Thread
// から流し込む。毎フレーム大量に出さないよう、同一文言は一定間隔まで
// 抑制する(仕様書 89 項)。
class ExternalFeatureLog {
public:
	static void Initialize(std::vector<std::string>* consoleMessages);  // Console 出力先を受け取る。
	static void Shutdown();  // Console 出力先を外し、キューを捨てる。

	static void Info(ExternalFeatureCategory category, const std::string& message);
	static void Warning(ExternalFeatureCategory category, const std::string& message);
	static void Error(ExternalFeatureCategory category, const ExternalFeatureError& error);
	static void Error(ExternalFeatureCategory category, const std::string& message);

	static void Flush();  // Main Thread から Console へ反映する。毎フレーム 1 回呼ぶ。

private:
	struct PendingMessage {
		std::string text;  // Console へ出す 1 行。
	};

	static bool ShouldSuppress(const std::string& key);  // 同一文言の連打を抑える。

	static inline std::mutex mutex_{};
	static inline std::vector<PendingMessage> pendingMessages_{};
	static inline std::vector<std::pair<std::string, double>> recentKeys_{};
	static inline std::vector<std::string>* consoleMessages_ = nullptr;
};

#pragma warning(pop)
