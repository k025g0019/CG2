#include "FeelKitVibration.h"

#include "../FeelKitHaptics.h"

namespace FeelKit {

void SetHapticsBackend(FeelKitHaptics* haptics) {
	Internal::g_hapticsBackend = haptics;
}

void SetVibrationCallback(const VibrationCallback& callback) {
	Internal::g_vibrationCallback = callback;
}

void SetVibrationPatternCallback(const VibrationPatternCallback& callback) {
	Internal::g_vibrationPatternCallback = callback;
}

void SetVibrationFromSoundCallback(const VibrationFromSoundCallback& callback) {
	Internal::g_vibrationFromSoundCallback = callback;
}

void SetGamepadVibrationCallback(const GamepadVibrationCallback& callback) {
	Internal::g_gamepadVibrationCallback = callback;
}

void SetHdRumbleCallback(const HdRumbleCallback& callback) {
	Internal::g_hdRumbleCallback = callback;
}

void SetDualSenseHapticsCallback(const DualSenseHapticsCallback& callback) {
	Internal::g_dualSenseHapticsCallback = callback;
}

void SetTriggerFeedbackCallback(const TriggerFeedbackCallback& callback) {
	Internal::g_triggerFeedbackCallback = callback;
}

bool PlayGamepadVibration(const GamepadVibrationRequest& request) {
	if (!request.isEnabled) {
		return false;
	}

	if (Internal::g_hapticsBackend != nullptr) {
		return Internal::g_hapticsBackend->playOneShot(
			request.leftStrength,
			request.rightStrength,
			request.durationMs,
			request.isEnabled);
	}

	if (!Internal::g_gamepadVibrationCallback) {
		return false;
	}

	Internal::g_gamepadVibrationCallback(
		request.leftStrength,
		request.rightStrength,
		request.durationMs);
	return true;
}

bool PlayHdRumble(const HdRumbleRequest& request) {
	if (!Internal::g_hdRumbleCallback) {
		return false;
	}

	Internal::g_hdRumbleCallback(
		request.lowFrequency,
		request.highFrequency,
		request.amplitude,
		request.durationMs);
	return true;
}

bool PlayDualSenseHaptics(const DualSenseHapticsRequest& request) {
	if (!Internal::g_dualSenseHapticsCallback) {
		return false;
	}

	Internal::g_dualSenseHapticsCallback(
		request.leftStrength,
		request.rightStrength,
		request.durationMs);
	return true;
}

bool PlayTriggerFeedback(const TriggerFeedbackRequest& request) {
	if (!Internal::g_triggerFeedbackCallback) {
		return false;
	}

	Internal::g_triggerFeedbackCallback(
		request.leftResistance,
		request.rightResistance,
		request.durationMs);
	return true;
}

void Vibrate(float power, float seconds) {
	if (!Internal::g_vibrationCallback) {
		return;
	}

	Internal::g_vibrationCallback(
		Internal::clampFloat(power, 0.0f, 1.0f),
		(seconds > 0.0f) ? seconds : 0.0f);
}

void VibratePattern(const char* patternName) {
	if (!Internal::g_vibrationPatternCallback) {
		return;
	}

	Internal::g_vibrationPatternCallback(patternName);
}

void VibrateFromSound(const char* soundPath) {
	if (!Internal::g_vibrationFromSoundCallback) {
		return;
	}

	Internal::g_vibrationFromSoundCallback(soundPath);
}

} // namespace FeelKit
