#pragma once

#include "VisionTypes.h"

//================================================================
// Camera 入力抽象(仕様書 17 項)
//================================================================
// Media Foundation / OpenCV / 静止画など、映像の取得元を差し替えても
// CameraInputComponent から上の扱いは変えない。

class ICameraSource {
public:
	virtual ~ICameraSource() = default;

	virtual bool Open(const CameraInputConfig& config) = 0;
	virtual void Close() = 0;
	virtual bool IsOpen() const = 0;

	// 新しいフレームがあれば outFrame へ書いて true を返す。無ければ false。
	virtual bool TryGetFrame(ImageFrame& outFrame) = 0;

	virtual void EnumerateDevices(std::vector<CameraDeviceInfo>& outDevices) const = 0;
	virtual std::string GetActiveDeviceName() const = 0;
	virtual ExternalFeatureError GetLastError() const = 0;
	virtual const char* GetName() const = 0;
};
