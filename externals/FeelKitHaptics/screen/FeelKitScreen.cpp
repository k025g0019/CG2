#include "FeelKitScreen.h"

#include <cmath>

namespace FeelKit {

namespace {

constexpr float kScreenFramesPerSecond = 60.0f;

float durationMsToSeconds(int durationMs) {
	if (durationMs <= 0) {
		return 0.0f;
	}

	return static_cast<float>(durationMs) / 1000.0f;
}

float frameCountToSeconds(int frameCount) {
	if (frameCount <= 0) {
		return 0.0f;
	}

	return static_cast<float>(frameCount) / kScreenFramesPerSecond;
}

} // namespace

void SetScreenShakeCallback(const ScreenShakeCallback& callback) {
	Internal::g_screenShakeCallback = callback;
}

void SetScreenZoomCallback(const ScreenZoomCallback& callback) {
	Internal::g_screenZoomCallback = callback;
}

void SetHitStopFrameCallback(const HitStopFrameCallback& callback) {
	Internal::g_hitStopFrameCallback = callback;
}

void SetScreenFlashCallback(const ScreenFlashCallback& callback) {
	Internal::g_screenFlashCallback = callback;
}

void SetScreenColorChangeCallback(const ScreenColorChangeCallback& callback) {
	Internal::g_screenColorChangeCallback = callback;
}

void SetSlowMotionFrameCallback(const SlowMotionFrameCallback& callback) {
	Internal::g_slowMotionFrameCallback = callback;
}

bool PlayScreenShake(const ScreenShakeRequest& request) {
	if (Internal::g_screenShakeCallback) {
		Internal::g_screenShakeCallback(request.power, request.durationMs);
		return true;
	}

	Shake(request.power, durationMsToSeconds(request.durationMs));
	return true;
}

bool PlayScreenZoom(const ScreenZoomRequest& request) {
	if (Internal::g_screenZoomCallback) {
		Internal::g_screenZoomCallback(request.zoomScale, request.durationMs);
		return true;
	}

	Zoom(request.zoomScale, durationMsToSeconds(request.durationMs));
	return true;
}

bool PlayHitStop(const HitStopRequest& request) {
	if (Internal::g_hitStopFrameCallback) {
		Internal::g_hitStopFrameCallback(request.frameCount);
		return true;
	}

	HitStop(frameCountToSeconds(request.frameCount));
	return true;
}

bool PlayScreenFlash(const ScreenFlashRequest& request) {
	if (Internal::g_screenFlashCallback) {
		Internal::g_screenFlashCallback(
			request.colorRgba,
			request.strength,
			request.durationMs);
		return true;
	}

	Flash(
		request.colorRgba,
		durationMsToSeconds(request.durationMs),
		request.strength);
	return true;
}

bool PlayScreenColorChange(const ScreenColorChangeRequest& request) {
	if (!Internal::g_screenColorChangeCallback) {
		return false;
	}

	Internal::g_screenColorChangeCallback(
		request.colorRgba,
		request.strength,
		request.durationMs);
	return true;
}

bool PlaySlowMotion(const SlowMotionRequest& request) {
	if (Internal::g_slowMotionFrameCallback) {
		Internal::g_slowMotionFrameCallback(
			request.timeScale,
			request.durationMs);
		return true;
	}

	SlowMotion(request.timeScale, durationMsToSeconds(request.durationMs));
	return true;
}

void Shake(float power, float seconds) {
	Internal::g_shakePower = Internal::clampFloat(power, 0.0f, 100000.0f);
	Internal::g_shakeRemaining = (seconds > 0.0f) ? seconds : 0.0f;
	Internal::g_shakeTotal = Internal::g_shakeRemaining;
}

void Flash(unsigned int color, float seconds) {
	Flash(color, seconds, 1.0f);
}

void Flash(unsigned int color, float seconds, float strength) {
	Internal::g_flashColor = color;
	Internal::g_flashRemaining = (seconds > 0.0f) ? seconds : 0.0f;
	Internal::g_flashTotal = Internal::g_flashRemaining;
	Internal::g_flashStrength = Internal::clampFloat(strength, 0.0f, 1.0f);
}

void Zoom(float scale, float seconds) {
	Internal::g_zoomScale = scale > 0.0f ? scale : 0.0f;
	Internal::g_zoomRemaining = (seconds > 0.0f) ? seconds : 0.0f;
	Internal::g_zoomTotal = Internal::g_zoomRemaining;
}

void HitStop(float seconds) {
	Internal::g_hitStopRemaining = (seconds > 0.0f) ? seconds : 0.0f;
}

void SlowMotion(float scale, float seconds) {
	Internal::g_slowMoScale = Internal::clampFloat(scale, 0.0f, 1.0f);
	Internal::g_slowMoRemaining = (seconds > 0.0f) ? seconds : 0.0f;
	Internal::g_slowMoTotal = Internal::g_slowMoRemaining;
}

ScreenState GetScreenState() {
	ScreenState state = {};

	if (Internal::g_shakeRemaining > 0.0f) {
		const float shakeTime = Internal::g_shakeRemaining * 64.0f;
		state.shakeOffset.x = std::sin(shakeTime) * Internal::g_shakePower;
		state.shakeOffset.y = std::cos(shakeTime * 1.37f) * Internal::g_shakePower;
		state.shakeRotation = std::sin(shakeTime * 0.7f) * Internal::g_shakePower * 0.02f;
	}

	if (Internal::g_zoomRemaining > 0.0f) {
		state.zoomScale = Internal::g_zoomScale;
	}

	if (Internal::g_slowMoRemaining > 0.0f) {
		state.slowMotionScale = Internal::g_slowMoScale;
	}

	if (Internal::g_flashRemaining > 0.0f && Internal::g_flashTotal > 0.0f) {
		state.flashColor = Internal::g_flashColor;
		state.flashAlpha =
			(Internal::g_flashRemaining / Internal::g_flashTotal) * Internal::g_flashStrength;
	}

	state.flashStrength = Internal::g_flashStrength;
	state.isHitStop = Internal::g_hitStopRemaining > 0.0f;
	return state;
}

} // namespace FeelKit
