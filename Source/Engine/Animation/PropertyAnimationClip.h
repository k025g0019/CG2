#pragma once

#include "EditorCommonTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Property Animation Clip
//================================================================

// Animation Window で扱う 1 本のカーブが、どの Component の値を書き換えるかを表す。
// Rotation は Inspector と同じ「度」で保存し、Runtime でラジアンへ変換する。
enum class AnimationPropertyTarget : int32_t {
	TransformPositionX = 0,
	TransformPositionY,
	TransformPositionZ,
	TransformRotationXDegrees,
	TransformRotationYDegrees,
	TransformRotationZDegrees,
	TransformScaleX,
	TransformScaleY,
	TransformScaleZ,
	LightIntensity,
	LightRange,
	MaterialBaseColorR,
	MaterialBaseColorG,
	MaterialBaseColorB,
	MaterialMetallic,
	MaterialRoughness,
	MaterialAlpha,
	MaterialEmissionStrength,
	MaterialEmissionColorR,
	MaterialEmissionColorG,
	MaterialEmissionColorB,
};

enum class AnimationCurveInterpolation : int32_t {
	Step = 0,  // 次の Key まで値を変えない。ON/OFF や瞬間切り替え向け。
	Linear = 1,  // 2 Key 間を一定速度で補間する。
	CubicHermite = 2,  // 入出力接線を使い、滑らかな加減速を作る。
};

enum class AnimationPropertyWriteMode : int32_t {
	Override = 0,  // カーブ値で元の値を置き換える。
	Additive = 1,  // Play 開始時の値へカーブ値を加える。
	Multiply = 2,  // Play 開始時の値へカーブ値を掛ける。
};

struct PropertyAnimationKeyframe {
	float time = 0.0f;  // Clip 開始からの秒数。
	float value = 0.0f;  // この Key の値。Rotation Track のみ度。
	float inTangent = 0.0f;  // Cubic Hermite で Key へ入る傾き（値/秒）。
	float outTangent = 0.0f;  // Cubic Hermite で Key から出る傾き（値/秒）。
	AnimationCurveInterpolation interpolation = AnimationCurveInterpolation::Linear;  // この Key から次 Key までの補間。
};

struct PropertyAnimationTrack {
	AnimationPropertyTarget target = AnimationPropertyTarget::TransformPositionY;
	AnimationPropertyWriteMode writeMode = AnimationPropertyWriteMode::Override;
	std::vector<PropertyAnimationKeyframe> keyframes;
};

struct PropertyAnimationEvent {
	float time = 0.0f;  // 前フレームからこの時刻を通過した瞬間に 1 回発火する。
	std::string name;  // C++ Script の EditorScript_OnAnimationEvent へ渡すイベント名。
	std::string effectAssetPath;  // 空でなければ EffectSystem がこの .effect を再生する。
	Vector3 localOffset{0.0f, 0.0f, 0.0f};  // Event 所有 GameObject からの発生位置。
};

class PropertyAnimationClip {
public:
	bool LoadFromJson(const std::string& filePath);  // .animclip を読み込み、成功した場合だけ内容を置き換える。
	bool SaveToJson(const std::string& filePath) const;  // Animation Window で編集した内容を .animclip へ保存する。
	float SampleTrack(const PropertyAnimationTrack& track, float playbackTime) const;  // Track の補間方式に従って値を返す。

	std::string name = "AnimationClip";
	float durationSeconds = 1.0f;
	float sampleRate = 30.0f;  // Editor の時間目盛りと将来の Bake に使う。Runtime は連続時間で評価する。
	bool loop = true;
	std::vector<PropertyAnimationTrack> tracks;
	std::vector<PropertyAnimationEvent> events;
};

const char* GetAnimationPropertyTargetName(AnimationPropertyTarget target);  // Inspector へ日本語の対象名を表示する。
const char* GetAnimationPropertyTargetPath(AnimationPropertyTarget target);  // JSON に保存する安定した Property Path を返す。
const char* GetAnimationInterpolationName(AnimationCurveInterpolation interpolation);  // JSON と UI で共有する補間名を返す。
const char* GetAnimationWriteModeName(AnimationPropertyWriteMode writeMode);  // JSON と UI で共有する書き込み方式名を返す。

#pragma warning(pop)
