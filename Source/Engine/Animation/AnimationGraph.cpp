#include "AnimationGraph.h"

#include "ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
	using JsonValue = crude_json::value;

	const JsonValue* FindMember(const JsonValue& objectValue, const std::string& name) {
		if (!objectValue.is_object() || !objectValue.contains(name)) {
			return nullptr;
		}

		return &objectValue[name];
	}

	std::string ReadString(const JsonValue& objectValue, const std::string& name, const std::string& fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_string() ? value->get<crude_json::string>() : fallbackValue;
	}

	float ReadFloat(const JsonValue& objectValue, const std::string& name, float fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_number()
			? static_cast<float>(value->get<crude_json::number>())
			: fallbackValue;
	}

	int32_t ReadInt(const JsonValue& objectValue, const std::string& name, int32_t fallbackValue) {
		return static_cast<int32_t>(ReadFloat(objectValue, name, static_cast<float>(fallbackValue)));
	}

	bool ReadBool(const JsonValue& objectValue, const std::string& name, bool fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_boolean() ? value->get<crude_json::boolean>() : fallbackValue;
	}

	AnimatorParameterType ParseParameterType(const std::string& typeName) {
		if (typeName == "Int") {
			return AnimatorParameterType::Int;
		}
		if (typeName == "Bool") {
			return AnimatorParameterType::Bool;
		}
		if (typeName == "Trigger") {
			return AnimatorParameterType::Trigger;
		}
		if (typeName == "Vector2") {
			return AnimatorParameterType::Vector2;
		}
		if (typeName == "Vector3") {
			return AnimatorParameterType::Vector3;
		}

		return AnimatorParameterType::Float;
	}

	AnimationConditionOperator ParseConditionOperator(const std::string& operatorName) {
		if (operatorName == "Less") {
			return AnimationConditionOperator::Less;
		}
		if (operatorName == "Equal") {
			return AnimationConditionOperator::Equal;
		}
		if (operatorName == "NotEqual") {
			return AnimationConditionOperator::NotEqual;
		}
		if (operatorName == "True") {
			return AnimationConditionOperator::True;
		}
		if (operatorName == "False") {
			return AnimationConditionOperator::False;
		}
		if (operatorName == "Triggered") {
			return AnimationConditionOperator::Triggered;
		}

		return AnimationConditionOperator::Greater;
	}

	AnimationBlendTreeType ParseBlendTreeType(const std::string& typeName) {
		if (typeName == "Blend1D") {
			return AnimationBlendTreeType::Blend1D;
		}
		if (typeName == "Blend2DDirectional") {
			return AnimationBlendTreeType::Blend2DDirectional;
		}
		if (typeName == "Blend2DCartesian") {
			return AnimationBlendTreeType::Blend2DCartesian;
		}
		if (typeName == "Direct") {
			return AnimationBlendTreeType::Direct;
		}

		return AnimationBlendTreeType::Clip;
	}
}

bool AnimationGraph::LoadFromJson(const std::string& filePath) {
	const auto [rootValue, isLoaded] = JsonValue::load(filePath);
	if (!isLoaded || !rootValue.is_object()) {
		return false;
	}

	AnimationGraph loadedGraph{};
	loadedGraph.entryState = ReadInt(rootValue, "entryState", 0);

	const JsonValue* parameterArrayValue = FindMember(rootValue, "parameters");
	if (parameterArrayValue != nullptr && parameterArrayValue->is_array()) {
		for (const JsonValue& parameterValue : parameterArrayValue->get<crude_json::array>()) {
			if (!parameterValue.is_object()) {
				continue;
			}

			AnimationGraphParameter parameter{};
			parameter.name = ReadString(parameterValue, "name", "Parameter");
			parameter.defaultValue.type = ParseParameterType(ReadString(parameterValue, "type", "Float"));
			parameter.defaultValue.floatValue = ReadFloat(parameterValue, "float", 0.0f);
			parameter.defaultValue.intValue = ReadInt(parameterValue, "int", 0);
			parameter.defaultValue.boolValue = ReadBool(parameterValue, "bool", false);
			parameter.defaultValue.vector2Value = {
				ReadFloat(parameterValue, "x", 0.0f),
				ReadFloat(parameterValue, "y", 0.0f)};
			parameter.defaultValue.vector3Value = {
				ReadFloat(parameterValue, "x", 0.0f),
				ReadFloat(parameterValue, "y", 0.0f),
				ReadFloat(parameterValue, "z", 0.0f)};
			loadedGraph.parameters.push_back(parameter);
		}
	}

	const JsonValue* stateArrayValue = FindMember(rootValue, "states");
	if (stateArrayValue != nullptr && stateArrayValue->is_array()) {
		for (const JsonValue& stateValue : stateArrayValue->get<crude_json::array>()) {
			if (!stateValue.is_object()) {
				continue;
			}

			AnimationGraphState state{};
			state.name = ReadString(stateValue, "name", "State");
			state.clipIndex = ReadInt(stateValue, "clip", 0);
			state.playbackSpeed = ReadFloat(stateValue, "speed", 1.0f);
			state.loop = ReadBool(stateValue, "loop", true);
			state.blendTreeType = ParseBlendTreeType(ReadString(stateValue, "blendType", "Clip"));
			if (ReadBool(stateValue, "directionalBlend", false)) {
				state.blendTreeType = AnimationBlendTreeType::Blend2DDirectional;
			}
			state.blendParameter = ReadString(stateValue, "parameter", "Speed");
			state.blendParameterX = ReadString(stateValue, "parameterX", "MoveX");
			state.blendParameterY = ReadString(stateValue, "parameterY", "MoveY");

			const JsonValue* sampleArrayValue = FindMember(stateValue, "samples");
			if (sampleArrayValue != nullptr && sampleArrayValue->is_array()) {
				for (const JsonValue& sampleValue : sampleArrayValue->get<crude_json::array>()) {
					if (!sampleValue.is_object()) {
						continue;
					}

					AnimationBlendSample sample{};
					sample.clipIndex = ReadInt(sampleValue, "clip", 0);
					sample.position = {
						ReadFloat(sampleValue, "x", 0.0f),
						ReadFloat(sampleValue, "y", 0.0f)};
					sample.playbackSpeed = ReadFloat(sampleValue, "speed", 1.0f);
					sample.weightParameter = ReadString(sampleValue, "weightParameter", "");
					state.blendSamples.push_back(sample);
				}
			}

			loadedGraph.states.push_back(state);
		}
	}

	const JsonValue* transitionArrayValue = FindMember(rootValue, "transitions");
	if (transitionArrayValue != nullptr && transitionArrayValue->is_array()) {
		for (const JsonValue& transitionValue : transitionArrayValue->get<crude_json::array>()) {
			if (!transitionValue.is_object()) {
				continue;
			}

			AnimationGraphTransition transition{};
			transition.sourceState = ReadInt(transitionValue, "source", 0);
			transition.destinationState = ReadInt(transitionValue, "destination", 0);
			transition.blendDuration = (std::max)(ReadFloat(transitionValue, "blendDuration", 0.15f), 0.0f);
			transition.exitTime = ReadFloat(transitionValue, "exitTime", 1.0f);
			transition.hasExitTime = ReadBool(transitionValue, "hasExitTime", false);
			transition.canInterrupt = ReadBool(transitionValue, "canInterrupt", true);

			const JsonValue* conditionArrayValue = FindMember(transitionValue, "conditions");
			if (conditionArrayValue != nullptr && conditionArrayValue->is_array()) {
				for (const JsonValue& conditionValue : conditionArrayValue->get<crude_json::array>()) {
					if (!conditionValue.is_object()) {
						continue;
					}

					AnimationTransitionCondition condition{};
					condition.parameterName = ReadString(conditionValue, "parameter", "");
					condition.conditionOperator = ParseConditionOperator(ReadString(conditionValue, "operator", "Greater"));
					condition.floatThreshold = ReadFloat(conditionValue, "float", 0.0f);
					condition.intThreshold = ReadInt(conditionValue, "int", 0);
					transition.conditions.push_back(condition);
				}
			}

			loadedGraph.transitions.push_back(transition);
		}
	}

	const JsonValue* eventArrayValue = FindMember(rootValue, "events");
	if (eventArrayValue != nullptr && eventArrayValue->is_array()) {
		for (const JsonValue& eventValue : eventArrayValue->get<crude_json::array>()) {
			if (!eventValue.is_object()) {
				continue;
			}

			AnimationGraphEvent animationEvent{};
			animationEvent.clipIndex = ReadInt(eventValue, "clip", 0);
			animationEvent.time = (std::max)(ReadFloat(eventValue, "time", 0.0f), 0.0f);
			animationEvent.name = ReadString(eventValue, "name", "AnimationEvent");
			animationEvent.effectAssetPath = ReadString(eventValue, "effect", "");
			animationEvent.localOffset = {
				ReadFloat(eventValue, "offsetX", 0.0f),
				ReadFloat(eventValue, "offsetY", 0.0f),
				ReadFloat(eventValue, "offsetZ", 0.0f)};
			loadedGraph.events.push_back(animationEvent);
		}
	}

	if (loadedGraph.states.empty()) {
		return false;
	}

	loadedGraph.entryState = (std::clamp)(loadedGraph.entryState, 0, static_cast<int32_t>(loadedGraph.states.size()) - 1);
	*this = std::move(loadedGraph);
	return true;
}

void AnimationGraph::BuildDefaultDirectionalGraph(
	int32_t idleClipIndex,
	int32_t forwardClipIndex,
	int32_t backwardClipIndex,
	int32_t leftClipIndex,
	int32_t rightClipIndex) {
	entryState = 0;
	parameters.clear();
	states.clear();
	transitions.clear();
	events.clear();

	AnimationGraphParameter moveX{};
	moveX.name = "MoveX";
	moveX.defaultValue.type = AnimatorParameterType::Float;
	parameters.push_back(moveX);

	AnimationGraphParameter moveY{};
	moveY.name = "MoveY";
	moveY.defaultValue.type = AnimatorParameterType::Float;
	parameters.push_back(moveY);

	AnimationGraphParameter speed{};
	speed.name = "Speed";
	speed.defaultValue.type = AnimatorParameterType::Float;
	parameters.push_back(speed);

	AnimationGraphState locomotion{};
	locomotion.name = "Locomotion";
	locomotion.clipIndex = idleClipIndex;
	locomotion.blendTreeType = AnimationBlendTreeType::Blend2DDirectional;
	locomotion.blendSamples = {
		{idleClipIndex, {0.0f, 0.0f}, 1.0f},
		{forwardClipIndex, {0.0f, 1.0f}, 1.0f},
		{backwardClipIndex, {0.0f, -1.0f}, 1.0f},
		{leftClipIndex, {-1.0f, 0.0f}, 1.0f},
		{rightClipIndex, {1.0f, 0.0f}, 1.0f}};
	states.push_back(locomotion);
}

bool EvaluateAnimationTransitionCondition(
	const AnimationTransitionCondition& condition,
	const std::unordered_map<std::string, AnimatorParameterValue>& parameters) {
	const auto parameterIterator = parameters.find(condition.parameterName);
	if (parameterIterator == parameters.end()) {
		return false;
	}

	const AnimatorParameterValue& parameter = parameterIterator->second;
	switch (condition.conditionOperator) {
	case AnimationConditionOperator::Greater:
		return parameter.floatValue > condition.floatThreshold;
	case AnimationConditionOperator::Less:
		return parameter.floatValue < condition.floatThreshold;
	case AnimationConditionOperator::Equal:
		return parameter.type == AnimatorParameterType::Int
			? parameter.intValue == condition.intThreshold
			: std::fabs(parameter.floatValue - condition.floatThreshold) <= 0.0001f;
	case AnimationConditionOperator::NotEqual:
		return parameter.type == AnimatorParameterType::Int
			? parameter.intValue != condition.intThreshold
			: std::fabs(parameter.floatValue - condition.floatThreshold) > 0.0001f;
	case AnimationConditionOperator::True:
		return parameter.boolValue;
	case AnimationConditionOperator::False:
		return !parameter.boolValue;
	case AnimationConditionOperator::Triggered:
		return parameter.boolValue;
	default:
		return false;
	}
}
