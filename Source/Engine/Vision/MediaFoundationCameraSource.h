#pragma once

#include "ICameraSource.h"

#include <atomic>
#include <mutex>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Media Foundation を使う Camera 入力
//================================================================
// 取り込みは Worker Thread が行い、Main Thread は TryGetFrame で
// 最新フレームを 1 枚受け取るだけにする(描画フレームを止めない)。
// COM Interface を Header へ出さないため、実体は Impl 側へ隠す。

class MediaFoundationCameraSource final : public ICameraSource {
public:
	MediaFoundationCameraSource() = default;
	~MediaFoundationCameraSource() override;

	MediaFoundationCameraSource(const MediaFoundationCameraSource&) = delete;
	MediaFoundationCameraSource& operator=(const MediaFoundationCameraSource&) = delete;

	bool Open(const CameraInputConfig& config) override;
	void Close() override;
	bool IsOpen() const override;
	bool TryGetFrame(ImageFrame& outFrame) override;
	void EnumerateDevices(std::vector<CameraDeviceInfo>& outDevices) const override;
	std::string GetActiveDeviceName() const override;
	ExternalFeatureError GetLastError() const override;
	const char* GetName() const override;

private:
	struct Impl;

	void CaptureMain();  // Worker Thread の本体。

	Impl* impl_ = nullptr;
	mutable std::mutex mutex_;
	std::thread captureThread_;
	CameraInputConfig config_{};
	ImageFrame latestFrame_{};
	ExternalFeatureError lastError_{};
	std::string activeDeviceName_;
	std::atomic<bool> isStopRequested_{false};
	std::atomic<int32_t> capturedFrameCount_{0};
	int32_t deliveredFrameIndex_ = -1;  // TryGetFrame が最後に渡した frameIndex。
	bool isOpen_ = false;
};

#pragma warning(pop)
