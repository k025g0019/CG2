#include "AnimationGraph.h"

#include "ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
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

	// PropertyAnimationClip.cpp と同じ最小限の実装。Animation 層から共通ヘッダを増やさないため
	// 小さな関数だけ重複させている。
	std::string EscapeJsonString(const std::string& sourceText) {
		std::ostringstream escapedText;

		for (const char character : sourceText) {
			switch (character) {
			case '\"': escapedText << "\\\""; break;
			case '\\': escapedText << "\\\\"; break;
			case '\n': escapedText << "\\n"; break;
			case '\r': escapedText << "\\r"; break;
			case '\t': escapedText << "\\t"; break;
			default: escapedText << character; break;
			}
		}

		return escapedText.str();
	}
}

const char* GetAnimatorParameterTypeName(AnimatorParameterType parameterType) {
	switch (parameterType) {
	case AnimatorParameterType::Int: return "Int";
	case AnimatorParameterType::Bool: return "Bool";
	case AnimatorParameterType::Trigger: return "Trigger";
	case AnimatorParameterType::Vector2: return "Vector2";
	case AnimatorParameterType::Vector3: return "Vector3";
	case AnimatorParameterType::Float:
	default:
		return "Float";
	}
}

const char* GetAnimationConditionOperatorName(AnimationConditionOperator conditionOperator) {
	switch (conditionOperator) {
	case AnimationConditionOperator::Less: return "Less";
	case AnimationConditionOperator::Equal: return "Equal";
	case AnimationConditionOperator::NotEqual: return "NotEqual";
	case AnimationConditionOperator::True: return "True";
	case AnimationConditionOperator::False: return "False";
	case AnimationConditionOperator::Triggered: return "Triggered";
	case AnimationConditionOperator::Greater:
	default:
		return "Greater";
	}
}

const char* GetAnimationBlendTreeTypeName(AnimationBlendTreeType blendTreeType) {
	switch (blendTreeType) {
	case AnimationBlendTreeType::Blend1D: return "Blend1D";
	case AnimationBlendTreeType::Blend2DDirectional: return "Blend2DDirectional";
	case AnimationBlendTreeType::Blend2DCartesian: return "Blend2DCartesian";
	case AnimationBlendTreeType::Direct: return "Direct";
	case AnimationBlendTreeType::Clip:
	default:
		return "Clip";
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

bool AnimationGraph::SaveToJson(const std::string& filePath) const {
	// State が 1 つも無い Graph は LoadFromJson 側が失敗扱いにするため、書き出しても読み直せない。
	// 壊れた .animgraph を作らないよう、保存前に弾く。
	if (states.empty()) {
		return false;
	}

	std::ofstream outputFile(filePath, std::ios::binary | std::ios::trunc);

	if (!outputFile.is_open()) {
		return false;
	}

	// エディタが作るテキストアセットは、プロジェクト規約に合わせて UTF-8 BOM 付きで保存する。
	outputFile.write("\xEF\xBB\xBF", 3);
	outputFile << std::fixed << std::setprecision(6);
	outputFile << "{\n";
	outputFile << "  \"entryState\": "
		<< (std::clamp)(entryState, 0, static_cast<int32_t>(states.size()) - 1) << ",\n";

	outputFile << "  \"parameters\": [\n";
	for (size_t parameterIndex = 0u; parameterIndex < parameters.size(); ++parameterIndex) {
		const AnimationGraphParameter& parameter = parameters[parameterIndex];
		outputFile << "    { \"name\": \"" << EscapeJsonString(parameter.name) << "\"";
		outputFile << ", \"type\": \"" << GetAnimatorParameterTypeName(parameter.defaultValue.type) << "\"";
		outputFile << ", \"float\": " << parameter.defaultValue.floatValue;
		outputFile << ", \"int\": " << parameter.defaultValue.intValue;
		outputFile << ", \"bool\": " << (parameter.defaultValue.boolValue ? "true" : "false");

		// Vector2 / Vector3 は Load 側が x / y / z を共有して読むため、同じキー名で書き出す。
		if (parameter.defaultValue.type == AnimatorParameterType::Vector2) {
			outputFile << ", \"x\": " << parameter.defaultValue.vector2Value.x;
			outputFile << ", \"y\": " << parameter.defaultValue.vector2Value.y;
		}
		else if (parameter.defaultValue.type == AnimatorParameterType::Vector3) {
			outputFile << ", \"x\": " << parameter.defaultValue.vector3Value.x;
			outputFile << ", \"y\": " << parameter.defaultValue.vector3Value.y;
			outputFile << ", \"z\": " << parameter.defaultValue.vector3Value.z;
		}

		outputFile << " }" << (parameterIndex + 1u < parameters.size() ? ",\n" : "\n");
	}
	outputFile << "  ],\n";

	outputFile << "  \"states\": [\n";
	for (size_t stateIndex = 0u; stateIndex < states.size(); ++stateIndex) {
		const AnimationGraphState& state = states[stateIndex];
		outputFile << "    {\n";
		outputFile << "      \"name\": \"" << EscapeJsonString(state.name) << "\",\n";
		outputFile << "      \"clip\": " << state.clipIndex << ",\n";
		outputFile << "      \"speed\": " << state.playbackSpeed << ",\n";
		outputFile << "      \"loop\": " << (state.loop ? "true" : "false") << ",\n";
		outputFile << "      \"blendType\": \"" << GetAnimationBlendTreeTypeName(state.blendTreeType) << "\",\n";
		outputFile << "      \"parameter\": \"" << EscapeJsonString(state.blendParameter) << "\",\n";
		outputFile << "      \"parameterX\": \"" << EscapeJsonString(state.blendParameterX) << "\",\n";
		outputFile << "      \"parameterY\": \"" << EscapeJsonString(state.blendParameterY) << "\",\n";
		outputFile << "      \"samples\": [\n";

		for (size_t sampleIndex = 0u; sampleIndex < state.blendSamples.size(); ++sampleIndex) {
			const AnimationBlendSample& sample = state.blendSamples[sampleIndex];
			outputFile << "        { \"clip\": " << sample.clipIndex;
			outputFile << ", \"x\": " << sample.position.x;
			outputFile << ", \"y\": " << sample.position.y;
			outputFile << ", \"speed\": " << sample.playbackSpeed;
			outputFile << ", \"weightParameter\": \"" << EscapeJsonString(sample.weightParameter) << "\" }";
			outputFile << (sampleIndex + 1u < state.blendSamples.size() ? ",\n" : "\n");
		}

		outputFile << "      ]\n";
		outputFile << "    }" << (stateIndex + 1u < states.size() ? ",\n" : "\n");
	}
	outputFile << "  ],\n";

	outputFile << "  \"transitions\": [\n";
	for (size_t transitionIndex = 0u; transitionIndex < transitions.size(); ++transitionIndex) {
		const AnimationGraphTransition& transition = transitions[transitionIndex];
		outputFile << "    {\n";
		outputFile << "      \"source\": " << transition.sourceState << ",\n";
		outputFile << "      \"destination\": " << transition.destinationState << ",\n";
		outputFile << "      \"blendDuration\": " << (std::max)(transition.blendDuration, 0.0f) << ",\n";
		outputFile << "      \"exitTime\": " << transition.exitTime << ",\n";
		outputFile << "      \"hasExitTime\": " << (transition.hasExitTime ? "true" : "false") << ",\n";
		outputFile << "      \"canInterrupt\": " << (transition.canInterrupt ? "true" : "false") << ",\n";
		outputFile << "      \"conditions\": [\n";

		for (size_t conditionIndex = 0u; conditionIndex < transition.conditions.size(); ++conditionIndex) {
			const AnimationTransitionCondition& condition = transition.conditions[conditionIndex];
			outputFile << "        { \"parameter\": \"" << EscapeJsonString(condition.parameterName) << "\"";
			outputFile << ", \"operator\": \"" << GetAnimationConditionOperatorName(condition.conditionOperator) << "\"";
			outputFile << ", \"float\": " << condition.floatThreshold;
			outputFile << ", \"int\": " << condition.intThreshold << " }";
			outputFile << (conditionIndex + 1u < transition.conditions.size() ? ",\n" : "\n");
		}

		outputFile << "      ]\n";
		outputFile << "    }" << (transitionIndex + 1u < transitions.size() ? ",\n" : "\n");
	}
	outputFile << "  ],\n";

	outputFile << "  \"events\": [\n";
	for (size_t eventIndex = 0u; eventIndex < events.size(); ++eventIndex) {
		const AnimationGraphEvent& animationEvent = events[eventIndex];
		outputFile << "    {\n";
		outputFile << "      \"clip\": " << animationEvent.clipIndex << ",\n";
		outputFile << "      \"time\": " << (std::max)(animationEvent.time, 0.0f) << ",\n";
		outputFile << "      \"name\": \"" << EscapeJsonString(animationEvent.name) << "\",\n";
		outputFile << "      \"effect\": \"" << EscapeJsonString(animationEvent.effectAssetPath) << "\",\n";
		outputFile << "      \"offsetX\": " << animationEvent.localOffset.x << ",\n";
		outputFile << "      \"offsetY\": " << animationEvent.localOffset.y << ",\n";
		outputFile << "      \"offsetZ\": " << animationEvent.localOffset.z << "\n";
		outputFile << "    }" << (eventIndex + 1u < events.size() ? ",\n" : "\n");
	}
	outputFile << "  ]\n";
	outputFile << "}\n";
	return outputFile.good();
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
