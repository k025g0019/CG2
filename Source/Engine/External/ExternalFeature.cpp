#include "ExternalFeature.h"

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include <algorithm>
#include <chrono>

namespace {
	constexpr double kSuppressSeconds = 3.0;  // 同じ文言はこの秒数まで再出力しない。
	constexpr size_t kMaxPendingMessages = 64u;  // 1 フレームで Console へ流す上限。

	double GetMonotonicSeconds() {
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		return std::chrono::duration<double>(now).count();
	}
}

const char* ToString(ExternalFeatureState state) {
	switch (state) {
	case ExternalFeatureState::Unavailable:
		return "Unavailable";
	case ExternalFeatureState::Ready:
		return "Ready";
	case ExternalFeatureState::Running:
		return "Running";
	case ExternalFeatureState::Error:
		return "Error";
	default:
		return "Unavailable";
	}
}

const char* ToDisplayString(ExternalFeatureState state) {
	switch (state) {
	case ExternalFeatureState::Unavailable:
		return "利用不可";
	case ExternalFeatureState::Ready:
		return "準備完了";
	case ExternalFeatureState::Running:
		return "実行中";
	case ExternalFeatureState::Error:
		return "エラー";
	default:
		return "利用不可";
	}
}

const char* ToString(ExternalFeatureCategory category) {
	switch (category) {
	case ExternalFeatureCategory::Speech:
		return "Speech";
	case ExternalFeatureCategory::Vision:
		return "Vision";
	case ExternalFeatureCategory::Online:
		return "Online";
	case ExternalFeatureCategory::Haptics:
		return "Haptics";
	default:
		return "External";
	}
}

void ExternalFeatureLog::Initialize(std::vector<std::string>* consoleMessages) {
	const std::lock_guard<std::mutex> lock(mutex_);
	consoleMessages_ = consoleMessages;
}

void ExternalFeatureLog::Shutdown() {
	const std::lock_guard<std::mutex> lock(mutex_);
	consoleMessages_ = nullptr;
	pendingMessages_.clear();
	recentKeys_.clear();
}

bool ExternalFeatureLog::ShouldSuppress(const std::string& key) {
	// mutex_ は呼び出し側で確保済み。
	const double now = GetMonotonicSeconds();

	recentKeys_.erase(
		std::remove_if(
			recentKeys_.begin(),
			recentKeys_.end(),
			[now](const std::pair<std::string, double>& entry) {
				return now - entry.second > kSuppressSeconds;
			}),
		recentKeys_.end());

	for (const std::pair<std::string, double>& entry : recentKeys_) {
		if (entry.first == key) {
			return true;
		}
	}

	recentKeys_.emplace_back(key, now);
	return false;
}

void ExternalFeatureLog::Info(ExternalFeatureCategory category, const std::string& message) {
	const std::lock_guard<std::mutex> lock(mutex_);
	const std::string line = std::string("[") + ToString(category) + "] " + message;

	if (ShouldSuppress(line) || pendingMessages_.size() >= kMaxPendingMessages) {
		return;
	}

	pendingMessages_.push_back(PendingMessage{line});
}

void ExternalFeatureLog::Warning(ExternalFeatureCategory category, const std::string& message) {
	const std::lock_guard<std::mutex> lock(mutex_);
	const std::string line = std::string("[") + ToString(category) + "] 警告: " + message;

	if (ShouldSuppress(line) || pendingMessages_.size() >= kMaxPendingMessages) {
		return;
	}

	pendingMessages_.push_back(PendingMessage{line});
}

void ExternalFeatureLog::Error(ExternalFeatureCategory category, const ExternalFeatureError& error) {
	Error(category, "code=" + std::to_string(error.code) + " " + error.message);
}

void ExternalFeatureLog::Error(ExternalFeatureCategory category, const std::string& message) {
	const std::lock_guard<std::mutex> lock(mutex_);
	const std::string line = std::string("[") + ToString(category) + "] エラー: " + message;

	if (ShouldSuppress(line) || pendingMessages_.size() >= kMaxPendingMessages) {
		return;
	}

	pendingMessages_.push_back(PendingMessage{line});
}

void ExternalFeatureLog::Flush() {
	std::vector<PendingMessage> flushedMessages;

	{
		const std::lock_guard<std::mutex> lock(mutex_);

		if (pendingMessages_.empty()) {
			return;
		}

		flushedMessages.swap(pendingMessages_);
	}

	std::vector<std::string>* consoleMessages = nullptr;

	{
		const std::lock_guard<std::mutex> lock(mutex_);
		consoleMessages = consoleMessages_;
	}

	for (const PendingMessage& message : flushedMessages) {
		if (consoleMessages != nullptr) {
			consoleMessages->push_back(message.text);
		}

		OutputDebugStringA((message.text + "\n").c_str());
	}
}
