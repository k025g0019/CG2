#pragma once

#include "EditorCommonTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

enum class AnimatorParameterType : int32_t {
	Float = 0,
	Int = 1,
	Bool = 2,
	Trigger = 3,
	Vector2 = 4,
	Vector3 = 5,
};

enum class AnimationConditionOperator : int32_t {
	Greater = 0,
	Less = 1,
	Equal = 2,
	NotEqual = 3,
	True = 4,
	False = 5,
	Triggered = 6,
};

enum class AnimationBlendTreeType : int32_t {
	Clip = 0,
	Blend1D = 1,
	Blend2DDirectional = 2,
	Blend2DCartesian = 3,
	Direct = 4,
};

struct AnimatorParameterValue {
	AnimatorParameterType type = AnimatorParameterType::Float;
	float floatValue = 0.0f;
	int32_t intValue = 0;
	bool boolValue = false;
	Vector2 vector2Value{0.0f, 0.0f};
	Vector3 vector3Value{0.0f, 0.0f, 0.0f};
};

struct AnimationGraphParameter {
	std::string name;
	AnimatorParameterValue defaultValue;
};

struct AnimationBlendSample {
	int32_t clipIndex = 0;
	Vector2 position{0.0f, 0.0f};
	float playbackSpeed = 1.0f;
	std::string weightParameter;  // Direct Blend でこの Sample のウェイトを読む Parameter 名。
};

struct AnimationGraphState {
	std::string name;
	int32_t clipIndex = 0;
	float playbackSpeed = 1.0f;
	bool loop = true;
	AnimationBlendTreeType blendTreeType = AnimationBlendTreeType::Clip;
	std::string blendParameter = "Speed";
	std::string blendParameterX = "MoveX";
	std::string blendParameterY = "MoveY";
	std::vector<AnimationBlendSample> blendSamples;
};

struct AnimationTransitionCondition {
	std::string parameterName;
	AnimationConditionOperator conditionOperator = AnimationConditionOperator::Greater;
	float floatThreshold = 0.0f;
	int32_t intThreshold = 0;
};

struct AnimationGraphTransition {
	int32_t sourceState = 0;
	int32_t destinationState = 0;
	float blendDuration = 0.15f;
	float exitTime = 1.0f;
	bool hasExitTime = false;
	bool canInterrupt = true;
	std::vector<AnimationTransitionCondition> conditions;
};

struct AnimationGraphEvent {
	int32_t clipIndex = 0;
	float time = 0.0f;
	std::string name;
	std::string effectAssetPath;
	Vector3 localOffset{0.0f, 0.0f, 0.0f};
};

class AnimationGraph {
public:
	bool LoadFromJson(const std::string& filePath);  // .animgraph JSON を読み込み、成功した場合だけ内容を置き換える。
	void BuildDefaultDirectionalGraph(
		int32_t idleClipIndex,
		int32_t forwardClipIndex,
		int32_t backwardClipIndex,
		int32_t leftClipIndex,
		int32_t rightClipIndex);  // Graph 未設定時にも方向ブレンドを使える既定 Graph を作る。

	int32_t entryState = 0;
	std::vector<AnimationGraphParameter> parameters;
	std::vector<AnimationGraphState> states;
	std::vector<AnimationGraphTransition> transitions;
	std::vector<AnimationGraphEvent> events;
};

bool EvaluateAnimationTransitionCondition(
	const AnimationTransitionCondition& condition,
	const std::unordered_map<std::string, AnimatorParameterValue>& parameters);  // Runtime Parameter と遷移条件を比較する。

#pragma warning(pop)
