#include "EditorReplayManager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
	constexpr char kReplayHeader[] = "CG2REPLAY2";
}

void EditorReplayManager::StartRecording() {
	frames_.clear();
	frames_.reserve(3600u);
	playbackFrameIndex_ = 0u;
	mode_ = EditorReplayMode::Recording;
}

void EditorReplayManager::Stop() {
	mode_ = EditorReplayMode::Idle;
	playbackFrameIndex_ = 0u;
}

bool EditorReplayManager::StartPlayback() {
	if (frames_.empty()) {
		return false;
	}

	playbackFrameIndex_ = 0u;
	mode_ = EditorReplayMode::Playback;
	return true;
}

const uint8_t* EditorReplayManager::ResolveFrameInput(
	const uint8_t* liveKeyState,
	float& deltaTime) {
	if (liveKeyState == nullptr) {
		return liveKeyState;
	}

	if (mode_ == EditorReplayMode::Recording) {
		ReplayFrame replayFrame{};
		std::memcpy(replayFrame.keyState.data(), liveKeyState, replayFrame.keyState.size());
		replayFrame.deltaTime = (std::clamp)(deltaTime, 0.0f, 0.25f);
		frames_.push_back(replayFrame);
		return liveKeyState;
	}

	if (mode_ != EditorReplayMode::Playback || playbackFrameIndex_ >= frames_.size()) {
		if (mode_ == EditorReplayMode::Playback) {
			Stop();
		}

		return liveKeyState;
	}

	const ReplayFrame& replayFrame = frames_[playbackFrameIndex_];
	playbackFrameIndex_++;
	deltaTime = replayFrame.deltaTime;
	return replayFrame.keyState.data();
}

bool EditorReplayManager::Save(const std::string& filePath) const {
	if (filePath.empty() || frames_.empty()) {
		return false;
	}

	const std::filesystem::path replayPath(filePath);
	std::error_code fileError;
	std::filesystem::create_directories(replayPath.parent_path(), fileError);

	if (fileError) {
		return false;
	}

	std::ofstream replayFile(replayPath, std::ios::binary | std::ios::trunc);

	if (!replayFile.is_open()) {
		return false;
	}

	replayFile.write(kReplayHeader, static_cast<std::streamsize>(sizeof(kReplayHeader)));
	const std::uint64_t frameCount = static_cast<std::uint64_t>(frames_.size());
	replayFile.write(reinterpret_cast<const char*>(&frameCount), sizeof(frameCount));

	for (const ReplayFrame& replayFrame : frames_) {
		replayFile.write(
			reinterpret_cast<const char*>(replayFrame.keyState.data()),
			static_cast<std::streamsize>(replayFrame.keyState.size()));
		replayFile.write(reinterpret_cast<const char*>(&replayFrame.deltaTime), sizeof(replayFrame.deltaTime));
	}

	return replayFile.good();
}

bool EditorReplayManager::Load(const std::string& filePath) {
	std::ifstream replayFile(filePath, std::ios::binary);

	if (!replayFile.is_open()) {
		return false;
	}

	std::array<char, sizeof(kReplayHeader)> replayHeader{};
	replayFile.read(replayHeader.data(), static_cast<std::streamsize>(replayHeader.size()));

	if (!replayFile.good() || std::memcmp(
			replayHeader.data(),
			kReplayHeader,
			replayHeader.size()) != 0) {
		return false;
	}

	std::uint64_t frameCount = 0u;
	replayFile.read(reinterpret_cast<char*>(&frameCount), sizeof(frameCount));
	constexpr std::uint64_t maximumReplayFrameCount = 60u * 60u * 60u;

	if (!replayFile.good() || frameCount > maximumReplayFrameCount) {
		return false;
	}

	std::vector<ReplayFrame> loadedFrames(static_cast<std::size_t>(frameCount));

	for (ReplayFrame& replayFrame : loadedFrames) {
		replayFile.read(
			reinterpret_cast<char*>(replayFrame.keyState.data()),
			static_cast<std::streamsize>(replayFrame.keyState.size()));
		replayFile.read(reinterpret_cast<char*>(&replayFrame.deltaTime), sizeof(replayFrame.deltaTime));

		if (!replayFile.good()) {
			return false;
		}

		replayFrame.deltaTime = (std::clamp)(replayFrame.deltaTime, 0.0f, 0.25f);
	}

	frames_ = std::move(loadedFrames);
	playbackFrameIndex_ = 0u;
	mode_ = EditorReplayMode::Idle;
	return true;
}

EditorReplayMode EditorReplayManager::GetMode() const {
	return mode_;
}

std::size_t EditorReplayManager::GetFrameCount() const {
	return frames_.size();
}

std::size_t EditorReplayManager::GetPlaybackFrameIndex() const {
	return playbackFrameIndex_;
}
