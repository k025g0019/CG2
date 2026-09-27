#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

enum class EditorReplayMode : int32_t {
	Idle = 0,
	Recording,
	Playback,
};

class EditorReplayManager {
public:
	static constexpr std::size_t kKeyStateCount = 256u;

	void StartRecording();
	void Stop();
	bool StartPlayback();
	const uint8_t* ResolveFrameInput(
		const uint8_t* liveKeyState,
		float& deltaTime);  // Recordingは入力を保存し、Playbackは記録入力とdeltaTimeを返す。
	bool Save(const std::string& filePath) const;
	bool Load(const std::string& filePath);
	EditorReplayMode GetMode() const;
	std::size_t GetFrameCount() const;
	std::size_t GetPlaybackFrameIndex() const;

private:
	struct ReplayFrame {
		std::array<uint8_t, kKeyStateCount> keyState{};
		float deltaTime = 0.0f;
	};

	std::vector<ReplayFrame> frames_;
	std::size_t playbackFrameIndex_ = 0u;
	EditorReplayMode mode_ = EditorReplayMode::Idle;
};

#pragma warning(pop)
