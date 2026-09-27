#include "MediaFoundationCameraSource.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#pragma warning(pop)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
	std::string ToUtf8String(const wchar_t* wideText) {
		if (wideText == nullptr || wideText[0] == L'\0') {
			return std::string();
		}

		const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, wideText, -1, nullptr, 0, nullptr, nullptr);

		if (requiredLength <= 1) {
			return std::string();
		}

		std::string utf8Text(static_cast<size_t>(requiredLength - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wideText, -1, utf8Text.data(), requiredLength, nullptr, nullptr);
		return utf8Text;
	}

	ExternalFeatureError MakeComError(const char* functionName, HRESULT result) {
		ExternalFeatureError error{};
		error.code = static_cast<int32_t>(result);
		char buffer[16] = {};
		sprintf_s(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(result));
		error.message = std::string(functionName) + " が失敗しました (HRESULT=0x" + buffer + ")";
		return error;
	}
}

struct MediaFoundationCameraSource::Impl {
	Microsoft::WRL::ComPtr<IMFMediaSource> mediaSource;
	Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
	int32_t frameWidth = 0;
	int32_t frameHeight = 0;
	bool hasStartedMediaFoundation = false;
	bool hasInitializedCom = false;
};

MediaFoundationCameraSource::~MediaFoundationCameraSource() {
	Close();
}

bool MediaFoundationCameraSource::Open(const CameraInputConfig& config) {
	Close();

	config_ = config;
	impl_ = new Impl();
	lastError_.Clear();

	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	if (SUCCEEDED(comResult)) {
		impl_->hasInitializedCom = true;
	}
	else if (comResult != RPC_E_CHANGED_MODE) {
		lastError_ = MakeComError("CoInitializeEx", comResult);
		Close();
		return false;
	}

	HRESULT result = MFStartup(MF_VERSION, MFSTARTUP_LITE);

	if (FAILED(result)) {
		lastError_ = MakeComError("MFStartup", result);
		Close();
		return false;
	}

	impl_->hasStartedMediaFoundation = true;

	// Camera Device を列挙して、指定名に一致するもの(無ければ先頭)を使う。
	Microsoft::WRL::ComPtr<IMFAttributes> enumerateAttributes;
	result = MFCreateAttributes(&enumerateAttributes, 1);

	if (FAILED(result)) {
		lastError_ = MakeComError("MFCreateAttributes", result);
		Close();
		return false;
	}

	enumerateAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	IMFActivate** deviceActivates = nullptr;
	UINT32 deviceCount = 0;
	result = MFEnumDeviceSources(enumerateAttributes.Get(), &deviceActivates, &deviceCount);

	if (FAILED(result) || deviceCount == 0u) {
		lastError_.code = result == S_OK ? -1 : static_cast<int32_t>(result);
		lastError_.message = "利用できる Camera が見つかりません。";

		if (deviceActivates != nullptr) {
			CoTaskMemFree(deviceActivates);
		}

		Close();
		return false;
	}

	UINT32 selectedIndex = 0u;
	std::string selectedDeviceName;

	for (UINT32 deviceIndex = 0u; deviceIndex < deviceCount; ++deviceIndex) {
		WCHAR* friendlyName = nullptr;
		UINT32 friendlyNameLength = 0u;

		if (SUCCEEDED(deviceActivates[deviceIndex]->GetAllocatedString(
				MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &friendlyNameLength)) &&
			friendlyName != nullptr) {
			const std::string deviceName = ToUtf8String(friendlyName);
			CoTaskMemFree(friendlyName);

			if (deviceIndex == 0u) {
				selectedDeviceName = deviceName;
			}

			if (!config_.deviceName.empty() && deviceName.find(config_.deviceName) != std::string::npos) {
				selectedIndex = deviceIndex;
				selectedDeviceName = deviceName;
				break;
			}
		}
	}

	result = deviceActivates[selectedIndex]->ActivateObject(
		IID_PPV_ARGS(impl_->mediaSource.GetAddressOf()));

	for (UINT32 deviceIndex = 0u; deviceIndex < deviceCount; ++deviceIndex) {
		deviceActivates[deviceIndex]->Release();
	}

	CoTaskMemFree(deviceActivates);

	if (FAILED(result)) {
		lastError_ = MakeComError("IMFActivate::ActivateObject", result);
		Close();
		return false;
	}

	// 色変換とサイズ変換を Media Foundation 側へ任せ、BGRA 固定で受け取る。
	Microsoft::WRL::ComPtr<IMFAttributes> readerAttributes;
	result = MFCreateAttributes(&readerAttributes, 2);

	if (SUCCEEDED(result)) {
		readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
		readerAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
	}

	result = MFCreateSourceReaderFromMediaSource(
		impl_->mediaSource.Get(),
		readerAttributes.Get(),
		&impl_->sourceReader);

	if (FAILED(result)) {
		lastError_ = MakeComError("MFCreateSourceReaderFromMediaSource", result);
		Close();
		return false;
	}

	Microsoft::WRL::ComPtr<IMFMediaType> outputMediaType;
	result = MFCreateMediaType(&outputMediaType);

	if (FAILED(result)) {
		lastError_ = MakeComError("MFCreateMediaType", result);
		Close();
		return false;
	}

	outputMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	outputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

	if (config_.requestedWidth > 0 && config_.requestedHeight > 0) {
		MFSetAttributeSize(
			outputMediaType.Get(),
			MF_MT_FRAME_SIZE,
			static_cast<UINT32>(config_.requestedWidth),
			static_cast<UINT32>(config_.requestedHeight));
	}

	result = impl_->sourceReader->SetCurrentMediaType(
		static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
		nullptr,
		outputMediaType.Get());

	if (FAILED(result)) {
		// 解像度指定が通らない Camera もあるため、サイズ指定を外して再試行する。
		Microsoft::WRL::ComPtr<IMFMediaType> fallbackMediaType;

		if (SUCCEEDED(MFCreateMediaType(&fallbackMediaType))) {
			fallbackMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			fallbackMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			result = impl_->sourceReader->SetCurrentMediaType(
				static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
				nullptr,
				fallbackMediaType.Get());
		}
	}

	if (FAILED(result)) {
		lastError_ = MakeComError("IMFSourceReader::SetCurrentMediaType", result);
		Close();
		return false;
	}

	Microsoft::WRL::ComPtr<IMFMediaType> currentMediaType;

	if (SUCCEEDED(impl_->sourceReader->GetCurrentMediaType(
			static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentMediaType))) {
		UINT32 frameWidth = 0u;
		UINT32 frameHeight = 0u;

		if (SUCCEEDED(MFGetAttributeSize(currentMediaType.Get(), MF_MT_FRAME_SIZE, &frameWidth, &frameHeight))) {
			impl_->frameWidth = static_cast<int32_t>(frameWidth);
			impl_->frameHeight = static_cast<int32_t>(frameHeight);
		}
	}

	if (impl_->frameWidth <= 0 || impl_->frameHeight <= 0) {
		impl_->frameWidth = (std::max)(config_.requestedWidth, 320);
		impl_->frameHeight = (std::max)(config_.requestedHeight, 240);
	}

	activeDeviceName_ = selectedDeviceName.empty() ? std::string("既定の Camera") : selectedDeviceName;
	isOpen_ = true;
	isStopRequested_.store(false);
	capturedFrameCount_.store(0);
	deliveredFrameIndex_ = -1;
	captureThread_ = std::thread(&MediaFoundationCameraSource::CaptureMain, this);
	return true;
}

void MediaFoundationCameraSource::Close() {
	isStopRequested_.store(true);

	if (captureThread_.joinable()) {
		captureThread_.join();
	}

	if (impl_ != nullptr) {
		impl_->sourceReader.Reset();

		if (impl_->mediaSource) {
			impl_->mediaSource->Shutdown();
			impl_->mediaSource.Reset();
		}

		if (impl_->hasStartedMediaFoundation) {
			MFShutdown();
		}

		if (impl_->hasInitializedCom) {
			CoUninitialize();
		}

		delete impl_;
		impl_ = nullptr;
	}

	const std::lock_guard<std::mutex> lock(mutex_);
	latestFrame_ = ImageFrame{};
	isOpen_ = false;
	activeDeviceName_.clear();
}

bool MediaFoundationCameraSource::IsOpen() const {
	const std::lock_guard<std::mutex> lock(mutex_);
	return isOpen_;
}

void MediaFoundationCameraSource::CaptureMain() {
	// Worker Thread でも COM / Media Foundation を使うため、この Thread でも初期化する。
	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool hasInitializedCom = SUCCEEDED(comResult);

	const int32_t frameRateLimit = config_.frameRateLimit;
	const auto frameInterval = frameRateLimit > 0
		? std::chrono::microseconds(1000000 / frameRateLimit)
		: std::chrono::microseconds(0);
	int32_t frameIndex = 0;

	while (!isStopRequested_.load()) {
		IMFSourceReader* sourceReader = nullptr;
		int32_t frameWidth = 0;
		int32_t frameHeight = 0;

		{
			const std::lock_guard<std::mutex> lock(mutex_);

			if (impl_ == nullptr || !impl_->sourceReader) {
				break;
			}

			sourceReader = impl_->sourceReader.Get();
			frameWidth = impl_->frameWidth;
			frameHeight = impl_->frameHeight;
		}

		const auto readStartTime = std::chrono::steady_clock::now();
		DWORD streamFlags = 0;
		LONGLONG timestamp = 0;
		Microsoft::WRL::ComPtr<IMFSample> sample;

		const HRESULT readResult = sourceReader->ReadSample(
			static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
			0,
			nullptr,
			&streamFlags,
			&timestamp,
			&sample);

		if (FAILED(readResult)) {
			const std::lock_guard<std::mutex> lock(mutex_);
			lastError_ = MakeComError("IMFSourceReader::ReadSample", readResult);
			break;
		}

		if ((streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
			break;
		}

		if (sample) {
			Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;

			if (SUCCEEDED(sample->ConvertToContiguousBuffer(&mediaBuffer)) && mediaBuffer) {
				Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
				BYTE* scanline0 = nullptr;
				LONG pitch = 0;
				bool wasLocked = false;
				bool wasLocked2D = false;
				DWORD currentLength = 0;

				if (SUCCEEDED(mediaBuffer.As(&buffer2D)) &&
					SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch))) {
					wasLocked = true;
					wasLocked2D = true;
				}
				else if (SUCCEEDED(mediaBuffer->Lock(&scanline0, nullptr, &currentLength))) {
					wasLocked = true;
					pitch = frameWidth * 4;
				}

				if (wasLocked && scanline0 != nullptr && frameWidth > 0 && frameHeight > 0) {
					ImageFrame frame{};
					frame.width = frameWidth;
					frame.height = frameHeight;
					frame.frameIndex = frameIndex;
					frame.timestampSeconds = static_cast<double>(timestamp) / 10000000.0;
					frame.pixels.resize(
						static_cast<size_t>(frameWidth) * static_cast<size_t>(frameHeight) * 4u);

					// RGB32 は下から上へ並ぶ場合がある(pitch が負)。上から並べ直す。
					const BYTE* sourceRow = scanline0;
					const LONG rowPitch = pitch;

					for (int32_t rowIndex = 0; rowIndex < frameHeight; ++rowIndex) {
						const BYTE* row = sourceRow + static_cast<ptrdiff_t>(rowPitch) * rowIndex;
						uint8_t* destinationRow =
							frame.pixels.data() + static_cast<size_t>(rowIndex) * static_cast<size_t>(frameWidth) * 4u;
						std::memcpy(destinationRow, row, static_cast<size_t>(frameWidth) * 4u);
					}

					{
						const std::lock_guard<std::mutex> lock(mutex_);
						latestFrame_ = std::move(frame);
					}

					capturedFrameCount_.fetch_add(1);
					++frameIndex;
				}

				if (wasLocked2D) {
					buffer2D->Unlock2D();
				}
				else if (wasLocked) {
					mediaBuffer->Unlock();
				}
			}
		}

		if (frameInterval.count() > 0) {
			const auto elapsedTime = std::chrono::steady_clock::now() - readStartTime;

			if (elapsedTime < frameInterval) {
				std::this_thread::sleep_for(frameInterval - elapsedTime);
			}
		}
	}

	if (hasInitializedCom) {
		CoUninitialize();
	}
}

bool MediaFoundationCameraSource::TryGetFrame(ImageFrame& outFrame) {
	const std::lock_guard<std::mutex> lock(mutex_);

	if (!isOpen_ || !latestFrame_.IsValid() || latestFrame_.frameIndex == deliveredFrameIndex_) {
		return false;
	}

	outFrame = latestFrame_;
	deliveredFrameIndex_ = latestFrame_.frameIndex;
	return true;
}

void MediaFoundationCameraSource::EnumerateDevices(std::vector<CameraDeviceInfo>& outDevices) const {
	outDevices.clear();

	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool hasInitializedCom = SUCCEEDED(comResult);
	const HRESULT startupResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	const bool hasStartedMediaFoundation = SUCCEEDED(startupResult);

	Microsoft::WRL::ComPtr<IMFAttributes> enumerateAttributes;

	if (SUCCEEDED(MFCreateAttributes(&enumerateAttributes, 1))) {
		enumerateAttributes->SetGUID(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

		IMFActivate** deviceActivates = nullptr;
		UINT32 deviceCount = 0u;

		if (SUCCEEDED(MFEnumDeviceSources(enumerateAttributes.Get(), &deviceActivates, &deviceCount)) &&
			deviceActivates != nullptr) {
			for (UINT32 deviceIndex = 0u; deviceIndex < deviceCount; ++deviceIndex) {
				CameraDeviceInfo deviceInfo{};
				WCHAR* friendlyName = nullptr;
				UINT32 friendlyNameLength = 0u;

				if (SUCCEEDED(deviceActivates[deviceIndex]->GetAllocatedString(
						MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &friendlyNameLength)) &&
					friendlyName != nullptr) {
					deviceInfo.deviceName = ToUtf8String(friendlyName);
					CoTaskMemFree(friendlyName);
				}

				WCHAR* symbolicLink = nullptr;
				UINT32 symbolicLinkLength = 0u;

				if (SUCCEEDED(deviceActivates[deviceIndex]->GetAllocatedString(
						MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
						&symbolicLink,
						&symbolicLinkLength)) &&
					symbolicLink != nullptr) {
					deviceInfo.deviceId = ToUtf8String(symbolicLink);
					CoTaskMemFree(symbolicLink);
				}

				deviceInfo.isDefault = deviceIndex == 0u;

				if (!deviceInfo.deviceName.empty()) {
					outDevices.push_back(deviceInfo);
				}

				deviceActivates[deviceIndex]->Release();
			}

			CoTaskMemFree(deviceActivates);
		}
	}

	if (hasStartedMediaFoundation) {
		MFShutdown();
	}

	if (hasInitializedCom) {
		CoUninitialize();
	}
}

std::string MediaFoundationCameraSource::GetActiveDeviceName() const {
	const std::lock_guard<std::mutex> lock(mutex_);
	return activeDeviceName_;
}

ExternalFeatureError MediaFoundationCameraSource::GetLastError() const {
	const std::lock_guard<std::mutex> lock(mutex_);
	return lastError_;
}

const char* MediaFoundationCameraSource::GetName() const {
	return "Media Foundation";
}
