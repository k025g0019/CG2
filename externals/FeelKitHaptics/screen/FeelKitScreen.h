#pragma once

#include "core/FeelKitCore.h"

namespace FeelKit {

struct ScreenShakeRequest {
	float power = 0.0f;
	int durationMs = 0;
};

struct ScreenZoomRequest {
	float zoomScale = 1.0f;
	int durationMs = 0;
};

struct HitStopRequest {
	int frameCount = 0;
};

struct ScreenFlashRequest {
	unsigned int colorRgba = 0xFFFFFFFFu;
	float strength = 0.0f;
	int durationMs = 0;
};

struct ScreenColorChangeRequest {
	unsigned int colorRgba = 0xFFFFFFFFu;
	float strength = 0.0f;
	int durationMs = 0;
};

struct SlowMotionRequest {
	float timeScale = 1.0f;
	int durationMs = 0;
};

void SetScreenShakeCallback(const ScreenShakeCallback& callback);
void SetScreenZoomCallback(const ScreenZoomCallback& callback);
void SetHitStopFrameCallback(const HitStopFrameCallback& callback);
void SetScreenFlashCallback(const ScreenFlashCallback& callback);
void SetScreenColorChangeCallback(const ScreenColorChangeCallback& callback);
void SetSlowMotionFrameCallback(const SlowMotionFrameCallback& callback);

bool PlayScreenShake(const ScreenShakeRequest& request);
bool PlayScreenZoom(const ScreenZoomRequest& request);
bool PlayHitStop(const HitStopRequest& request);
bool PlayScreenFlash(const ScreenFlashRequest& request);
bool PlayScreenColorChange(const ScreenColorChangeRequest& request);
bool PlaySlowMotion(const SlowMotionRequest& request);

void Shake(float power, float seconds);
void Flash(unsigned int color, float seconds);
void Flash(unsigned int color, float seconds, float strength);
void Zoom(float scale, float seconds);
void HitStop(float seconds);
void SlowMotion(float scale, float seconds);

ScreenState GetScreenState();

} // namespace FeelKit
