#pragma once

#include "core/FeelKitCore.h"

class FeelKitHaptics;

namespace FeelKit {

struct GamepadVibrationRequest {
	float leftStrength = 0.0f;
	float rightStrength = 0.0f;
	int durationMs = 0;
	bool isEnabled = true;
};

struct HdRumbleRequest {
	float lowFrequency = 0.0f;
	float highFrequency = 0.0f;
	float amplitude = 0.0f;
	int durationMs = 0;
};

struct DualSenseHapticsRequest {
	float leftStrength = 0.0f;
	float rightStrength = 0.0f;
	int durationMs = 0;
};

struct TriggerFeedbackRequest {
	float leftResistance = 0.0f;
	float rightResistance = 0.0f;
	int durationMs = 0;
};

void SetHapticsBackend(FeelKitHaptics* haptics);
void SetVibrationCallback(const VibrationCallback& callback);
void SetVibrationPatternCallback(const VibrationPatternCallback& callback);
void SetVibrationFromSoundCallback(const VibrationFromSoundCallback& callback);
void SetGamepadVibrationCallback(const GamepadVibrationCallback& callback);
void SetHdRumbleCallback(const HdRumbleCallback& callback);
void SetDualSenseHapticsCallback(const DualSenseHapticsCallback& callback);
void SetTriggerFeedbackCallback(const TriggerFeedbackCallback& callback);

bool PlayGamepadVibration(const GamepadVibrationRequest& request);
bool PlayHdRumble(const HdRumbleRequest& request);
bool PlayDualSenseHaptics(const DualSenseHapticsRequest& request);
bool PlayTriggerFeedback(const TriggerFeedbackRequest& request);

void Vibrate(float power, float seconds);
void VibratePattern(const char* patternName);
void VibrateFromSound(const char* soundPath);

} // namespace FeelKit
