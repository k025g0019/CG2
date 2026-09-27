#include "SunCycle.h"

#include <cmath>

//============================================================
// Inspector公開変数
//============================================================

SCRIPT_FIELD_GAMEOBJECT(sunObject, "Sun Object", -1)

SCRIPT_FIELD_FLOAT(
	startAzimuth,
	"開始方位角",
	0.0f,
	-360.0f,
	360.0f,
	1.0f)

SCRIPT_FIELD_FLOAT(
	azimuthSpeed,
	"方位角速度（度/秒）",
	2.0f,
	-360.0f,
	360.0f,
	0.1f)

SCRIPT_FIELD_FLOAT(
	minElevation,
	"最低高度",
	-5.0f,
	-10.0f,
	90.0f,
	0.5f)

SCRIPT_FIELD_FLOAT(
	maxElevation,
	"最高高度",
	70.0f,
	-10.0f,
	90.0f,
	0.5f)

SCRIPT_FIELD_FLOAT(
	cycleSeconds,
	"1周期の秒数",
	120.0f,
	0.1f,
	86400.0f,
	1.0f)

SunCycle::SunCycle() {
	BindStart([this]() {
		const GameObject sun = FieldGameObject("sunObject");
		if (!sun.HasReference()) {
			return;
		}

		RuntimeProperty::SetBool(
			sun,
			"Light",
			"sunUseAzimuthElevation",
			true);
	});

	// 経過時間はUpdateの登録処理自身が保持するため、.hへのメンバー追加は不要。
	BindUpdate([this, elapsedSeconds = 0.0f](float deltaTime) mutable {
		const GameObject sun = FieldGameObject("sunObject");
		if (!sun.HasReference()) {
			return;
		}

		elapsedSeconds += deltaTime;

		float azimuth =
			FieldFloat("startAzimuth") +
			FieldFloat("azimuthSpeed") * elapsedSeconds;
		azimuth = std::fmod(azimuth, 360.0f);
		if (azimuth < 0.0f) {
			azimuth += 360.0f;
		}

		const float minimumElevation = std::fmin(
			FieldFloat("minElevation"),
			FieldFloat("maxElevation"));
		const float maximumElevation = std::fmax(
			FieldFloat("minElevation"),
			FieldFloat("maxElevation"));
		const float cycleSeconds = std::fmax(FieldFloat("cycleSeconds"), 0.1f);
		constexpr float kTwoPi = 6.28318530718f;
		const float cyclePosition =
			std::fmod(elapsedSeconds, cycleSeconds) / cycleSeconds;
		const float elevationRatio =
			(1.0f - std::cos(cyclePosition * kTwoPi)) * 0.5f;
		const float elevation = minimumElevation +
			(maximumElevation - minimumElevation) * elevationRatio;

		RuntimeProperty::SetFloat(
			sun,
			"Light",
			"sunAzimuthDegrees",
			azimuth);
		RuntimeProperty::SetFloat(
			sun,
			"Light",
			"sunElevationDegrees",
			elevation);
	});
}
