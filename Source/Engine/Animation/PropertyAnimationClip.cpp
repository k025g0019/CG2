#include "PropertyAnimationClip.h"

#include "ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {
	using JsonValue = crude_json::value;

	constexpr float kMinimumDuration = 0.00001f;

	const JsonValue* FindMember(const JsonValue& objectValue, const std::string& name) {
		if (!objectValue.is_object() || !objectValue.contains(name)) {
			return nullptr;
		}

		return &objectValue[name];
	}

	std::string ReadString(
		const JsonValue& objectValue,
		const std::string& name,
		const std::string& fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_string()
			? value->get<crude_json::string>()
			: fallbackValue;
	}

	float ReadFloat(const JsonValue& objectValue, const std::string& name, float fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_number()
			? static_cast<float>(value->get<crude_json::number>())
			: fallbackValue;
	}

	bool ReadBool(const JsonValue& objectValue, const std::string& name, bool fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		return value != nullptr && value->is_boolean()
			? value->get<crude_json::boolean>()
			: fallbackValue;
	}

	AnimationPropertyTarget ParsePropertyTarget(const std::string& propertyName) {
		if (propertyName == "Transform.LocalPositionX") return AnimationPropertyTarget::TransformPositionX;
		if (propertyName == "Transform.LocalPositionY") return AnimationPropertyTarget::TransformPositionY;
		if (propertyName == "Transform.LocalPositionZ") return AnimationPropertyTarget::TransformPositionZ;
		if (propertyName == "Transform.LocalRotationX") return AnimationPropertyTarget::TransformRotationXDegrees;
		if (propertyName == "Transform.LocalRotationY") return AnimationPropertyTarget::TransformRotationYDegrees;
		if (propertyName == "Transform.LocalRotationZ") return AnimationPropertyTarget::TransformRotationZDegrees;
		if (propertyName == "Transform.LocalScaleX") return AnimationPropertyTarget::TransformScaleX;
		if (propertyName == "Transform.LocalScaleY") return AnimationPropertyTarget::TransformScaleY;
		if (propertyName == "Transform.LocalScaleZ") return AnimationPropertyTarget::TransformScaleZ;
		if (propertyName == "Light.Intensity") return AnimationPropertyTarget::LightIntensity;
		if (propertyName == "Light.Range") return AnimationPropertyTarget::LightRange;
		if (propertyName == "Material.BaseColorR") return AnimationPropertyTarget::MaterialBaseColorR;
		if (propertyName == "Material.BaseColorG") return AnimationPropertyTarget::MaterialBaseColorG;
		if (propertyName == "Material.BaseColorB") return AnimationPropertyTarget::MaterialBaseColorB;
		if (propertyName == "Material.Metallic") return AnimationPropertyTarget::MaterialMetallic;
		if (propertyName == "Material.Roughness") return AnimationPropertyTarget::MaterialRoughness;
		if (propertyName == "Material.Alpha") return AnimationPropertyTarget::MaterialAlpha;
		if (propertyName == "Material.EmissionStrength") return AnimationPropertyTarget::MaterialEmissionStrength;
		if (propertyName == "Material.EmissionColorR") return AnimationPropertyTarget::MaterialEmissionColorR;
		if (propertyName == "Material.EmissionColorG") return AnimationPropertyTarget::MaterialEmissionColorG;
		if (propertyName == "Material.EmissionColorB") return AnimationPropertyTarget::MaterialEmissionColorB;

		return AnimationPropertyTarget::TransformPositionY;
	}

	AnimationCurveInterpolation ParseInterpolation(const std::string& interpolationName) {
		if (interpolationName == "Step") {
			return AnimationCurveInterpolation::Step;
		}

		if (interpolationName == "CubicHermite" || interpolationName == "Cubic") {
			return AnimationCurveInterpolation::CubicHermite;
		}

		return AnimationCurveInterpolation::Linear;
	}

	AnimationPropertyWriteMode ParseWriteMode(const std::string& writeModeName) {
		if (writeModeName == "Additive") {
			return AnimationPropertyWriteMode::Additive;
		}

		if (writeModeName == "Multiply") {
			return AnimationPropertyWriteMode::Multiply;
		}

		return AnimationPropertyWriteMode::Override;
	}

	float CubicHermite(
		float startValue,
		float startTangent,
		float endValue,
		float endTangent,
		float segmentDuration,
		float rate) {
		const float rateSquared = rate * rate;
		const float rateCubed = rateSquared * rate;
		const float startBasis = 2.0f * rateCubed - 3.0f * rateSquared + 1.0f;
		const float startTangentBasis = rateCubed - 2.0f * rateSquared + rate;
		const float endBasis = -2.0f * rateCubed + 3.0f * rateSquared;
		const float endTangentBasis = rateCubed - rateSquared;

		return startBasis * startValue +
			startTangentBasis * startTangent * segmentDuration +
			endBasis * endValue +
			endTangentBasis * endTangent * segmentDuration;
	}

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

bool PropertyAnimationClip::LoadFromJson(const std::string& filePath) {
	const auto [rootValue, isLoaded] = JsonValue::load(filePath);

	if (!isLoaded || !rootValue.is_object()) {
		return false;
	}

	PropertyAnimationClip loadedClip{};
	loadedClip.name = ReadString(rootValue, "name", loadedClip.name);
	loadedClip.durationSeconds = (std::max)(
		ReadFloat(rootValue, "duration", loadedClip.durationSeconds),
		kMinimumDuration);
	loadedClip.sampleRate = (std::clamp)(ReadFloat(rootValue, "sampleRate", loadedClip.sampleRate), 1.0f, 240.0f);
	loadedClip.loop = ReadBool(rootValue, "loop", loadedClip.loop);

	const JsonValue* trackArrayValue = FindMember(rootValue, "tracks");
	if (trackArrayValue != nullptr && trackArrayValue->is_array()) {
		for (const JsonValue& trackValue : trackArrayValue->get<crude_json::array>()) {
			if (!trackValue.is_object()) {
				continue;
			}

			PropertyAnimationTrack track{};
			track.target = ParsePropertyTarget(ReadString(trackValue, "property", "Transform.LocalPositionY"));
			track.writeMode = ParseWriteMode(ReadString(trackValue, "writeMode", "Override"));
			const JsonValue* keyArrayValue = FindMember(trackValue, "keys");

			if (keyArrayValue == nullptr || !keyArrayValue->is_array()) {
				continue;
			}

			for (const JsonValue& keyValue : keyArrayValue->get<crude_json::array>()) {
				if (!keyValue.is_object()) {
					continue;
				}

				PropertyAnimationKeyframe keyframe{};
				keyframe.time = (std::clamp)(ReadFloat(keyValue, "time", 0.0f), 0.0f, loadedClip.durationSeconds);
				keyframe.value = ReadFloat(keyValue, "value", 0.0f);
				keyframe.inTangent = ReadFloat(keyValue, "inTangent", 0.0f);
				keyframe.outTangent = ReadFloat(keyValue, "outTangent", 0.0f);
				keyframe.interpolation = ParseInterpolation(ReadString(keyValue, "interpolation", "Linear"));
				track.keyframes.push_back(keyframe);
			}

			std::sort(
				track.keyframes.begin(),
				track.keyframes.end(),
				[](const PropertyAnimationKeyframe& leftKey, const PropertyAnimationKeyframe& rightKey) {
					return leftKey.time < rightKey.time;
				});

			if (!track.keyframes.empty()) {
				loadedClip.tracks.push_back(std::move(track));
			}
		}
	}

	const JsonValue* eventArrayValue = FindMember(rootValue, "events");
	if (eventArrayValue != nullptr && eventArrayValue->is_array()) {
		for (const JsonValue& eventValue : eventArrayValue->get<crude_json::array>()) {
			if (!eventValue.is_object()) {
				continue;
			}

			PropertyAnimationEvent animationEvent{};
			animationEvent.time = (std::clamp)(
				ReadFloat(eventValue, "time", 0.0f),
				0.0f,
				loadedClip.durationSeconds);
			animationEvent.name = ReadString(eventValue, "name", "AnimationEvent");
			animationEvent.effectAssetPath = ReadString(eventValue, "effect", "");
			animationEvent.localOffset = {
				ReadFloat(eventValue, "offsetX", 0.0f),
				ReadFloat(eventValue, "offsetY", 0.0f),
				ReadFloat(eventValue, "offsetZ", 0.0f)};
			loadedClip.events.push_back(std::move(animationEvent));
		}
	}

	*this = std::move(loadedClip);
	return true;
}

bool PropertyAnimationClip::SaveToJson(const std::string& filePath) const {
	std::ofstream outputFile(filePath, std::ios::binary | std::ios::trunc);

	if (!outputFile.is_open()) {
		return false;
	}

	// エディタが作るテキストアセットは、プロジェクト規約に合わせて UTF-8 BOM 付きで保存する。
	outputFile.write("\xEF\xBB\xBF", 3);
	outputFile << std::fixed << std::setprecision(6);
	outputFile << "{\n";
	outputFile << "  \"name\": \"" << EscapeJsonString(name) << "\",\n";
	outputFile << "  \"duration\": " << (std::max)(durationSeconds, kMinimumDuration) << ",\n";
	outputFile << "  \"sampleRate\": " << (std::clamp)(sampleRate, 1.0f, 240.0f) << ",\n";
	outputFile << "  \"loop\": " << (loop ? "true" : "false") << ",\n";
	outputFile << "  \"tracks\": [\n";

	for (size_t trackIndex = 0u; trackIndex < tracks.size(); ++trackIndex) {
		const PropertyAnimationTrack& track = tracks[trackIndex];
		outputFile << "    {\n";
		outputFile << "      \"property\": \"" << GetAnimationPropertyTargetPath(track.target) << "\",\n";
		outputFile << "      \"writeMode\": \"" << GetAnimationWriteModeName(track.writeMode) << "\",\n";
		outputFile << "      \"keys\": [\n";

		for (size_t keyIndex = 0u; keyIndex < track.keyframes.size(); ++keyIndex) {
			const PropertyAnimationKeyframe& keyframe = track.keyframes[keyIndex];
			outputFile << "        { \"time\": " << keyframe.time;
			outputFile << ", \"value\": " << keyframe.value;
			outputFile << ", \"inTangent\": " << keyframe.inTangent;
			outputFile << ", \"outTangent\": " << keyframe.outTangent;
			outputFile << ", \"interpolation\": \"" << GetAnimationInterpolationName(keyframe.interpolation) << "\" }";
			outputFile << (keyIndex + 1u < track.keyframes.size() ? ",\n" : "\n");
		}

		outputFile << "      ]\n";
		outputFile << "    }" << (trackIndex + 1u < tracks.size() ? ",\n" : "\n");
	}

	outputFile << "  ],\n";
	outputFile << "  \"events\": [\n";

	for (size_t eventIndex = 0u; eventIndex < events.size(); ++eventIndex) {
		const PropertyAnimationEvent& animationEvent = events[eventIndex];
		outputFile << "    {\n";
		outputFile << "      \"time\": " << animationEvent.time << ",\n";
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

float PropertyAnimationClip::SampleTrack(
	const PropertyAnimationTrack& track,
	float playbackTime) const {
	if (track.keyframes.empty()) {
		return 0.0f;
	}

	if (track.keyframes.size() == 1u) {
		return track.keyframes.front().value;
	}

	float sampleTime = (std::max)(playbackTime, 0.0f);
	if (loop && durationSeconds > kMinimumDuration && sampleTime > durationSeconds) {
		sampleTime = std::fmod(sampleTime, durationSeconds);
	}
	else {
		sampleTime = (std::min)(sampleTime, durationSeconds);
	}

	if (sampleTime <= track.keyframes.front().time) {
		return track.keyframes.front().value;
	}

	if (sampleTime >= track.keyframes.back().time) {
		return track.keyframes.back().value;
	}

	const auto endKeyIterator = std::upper_bound(
		track.keyframes.begin(),
		track.keyframes.end(),
		sampleTime,
		[](float currentTime, const PropertyAnimationKeyframe& keyframe) {
			return currentTime < keyframe.time;
		});
	const PropertyAnimationKeyframe& endKey = *endKeyIterator;
	const PropertyAnimationKeyframe& startKey = *(endKeyIterator - 1);
	const float segmentDuration = (std::max)(endKey.time - startKey.time, kMinimumDuration);
	const float interpolationRate = (std::clamp)(
		(sampleTime - startKey.time) / segmentDuration,
		0.0f,
		1.0f);

	if (startKey.interpolation == AnimationCurveInterpolation::Step) {
		return startKey.value;
	}

	if (startKey.interpolation == AnimationCurveInterpolation::CubicHermite) {
		return CubicHermite(
			startKey.value,
			startKey.outTangent,
			endKey.value,
			endKey.inTangent,
			segmentDuration,
			interpolationRate);
	}

	return startKey.value + (endKey.value - startKey.value) * interpolationRate;
}

const char* GetAnimationPropertyTargetName(AnimationPropertyTarget target) {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX: return "Transform / 位置 X";
	case AnimationPropertyTarget::TransformPositionY: return "Transform / 位置 Y";
	case AnimationPropertyTarget::TransformPositionZ: return "Transform / 位置 Z";
	case AnimationPropertyTarget::TransformRotationXDegrees: return "Transform / 回転 X (度)";
	case AnimationPropertyTarget::TransformRotationYDegrees: return "Transform / 回転 Y (度)";
	case AnimationPropertyTarget::TransformRotationZDegrees: return "Transform / 回転 Z (度)";
	case AnimationPropertyTarget::TransformScaleX: return "Transform / スケール X";
	case AnimationPropertyTarget::TransformScaleY: return "Transform / スケール Y";
	case AnimationPropertyTarget::TransformScaleZ: return "Transform / スケール Z";
	case AnimationPropertyTarget::LightIntensity: return "ライト / 強さ";
	case AnimationPropertyTarget::LightRange: return "ライト / 範囲";
	case AnimationPropertyTarget::MaterialBaseColorR: return "マテリアル / ベースカラー R";
	case AnimationPropertyTarget::MaterialBaseColorG: return "マテリアル / ベースカラー G";
	case AnimationPropertyTarget::MaterialBaseColorB: return "マテリアル / ベースカラー B";
	case AnimationPropertyTarget::MaterialMetallic: return "マテリアル / メタリック";
	case AnimationPropertyTarget::MaterialRoughness: return "マテリアル / 粗さ";
	case AnimationPropertyTarget::MaterialAlpha: return "マテリアル / アルファ";
	case AnimationPropertyTarget::MaterialEmissionStrength: return "マテリアル / 放射の強さ";
	case AnimationPropertyTarget::MaterialEmissionColorR: return "マテリアル / 放射色 R";
	case AnimationPropertyTarget::MaterialEmissionColorG: return "マテリアル / 放射色 G";
	case AnimationPropertyTarget::MaterialEmissionColorB: return "マテリアル / 放射色 B";
	default: return "不明なプロパティ";
	}
}

const char* GetAnimationPropertyTargetPath(AnimationPropertyTarget target) {
	switch (target) {
	case AnimationPropertyTarget::TransformPositionX: return "Transform.LocalPositionX";
	case AnimationPropertyTarget::TransformPositionY: return "Transform.LocalPositionY";
	case AnimationPropertyTarget::TransformPositionZ: return "Transform.LocalPositionZ";
	case AnimationPropertyTarget::TransformRotationXDegrees: return "Transform.LocalRotationX";
	case AnimationPropertyTarget::TransformRotationYDegrees: return "Transform.LocalRotationY";
	case AnimationPropertyTarget::TransformRotationZDegrees: return "Transform.LocalRotationZ";
	case AnimationPropertyTarget::TransformScaleX: return "Transform.LocalScaleX";
	case AnimationPropertyTarget::TransformScaleY: return "Transform.LocalScaleY";
	case AnimationPropertyTarget::TransformScaleZ: return "Transform.LocalScaleZ";
	case AnimationPropertyTarget::LightIntensity: return "Light.Intensity";
	case AnimationPropertyTarget::LightRange: return "Light.Range";
	case AnimationPropertyTarget::MaterialBaseColorR: return "Material.BaseColorR";
	case AnimationPropertyTarget::MaterialBaseColorG: return "Material.BaseColorG";
	case AnimationPropertyTarget::MaterialBaseColorB: return "Material.BaseColorB";
	case AnimationPropertyTarget::MaterialMetallic: return "Material.Metallic";
	case AnimationPropertyTarget::MaterialRoughness: return "Material.Roughness";
	case AnimationPropertyTarget::MaterialAlpha: return "Material.Alpha";
	case AnimationPropertyTarget::MaterialEmissionStrength: return "Material.EmissionStrength";
	case AnimationPropertyTarget::MaterialEmissionColorR: return "Material.EmissionColorR";
	case AnimationPropertyTarget::MaterialEmissionColorG: return "Material.EmissionColorG";
	case AnimationPropertyTarget::MaterialEmissionColorB: return "Material.EmissionColorB";
	default: return "Transform.LocalPositionY";
	}
}

const char* GetAnimationInterpolationName(AnimationCurveInterpolation interpolation) {
	switch (interpolation) {
	case AnimationCurveInterpolation::Step: return "Step";
	case AnimationCurveInterpolation::CubicHermite: return "CubicHermite";
	case AnimationCurveInterpolation::Linear:
	default: return "Linear";
	}
}

const char* GetAnimationWriteModeName(AnimationPropertyWriteMode writeMode) {
	switch (writeMode) {
	case AnimationPropertyWriteMode::Additive: return "Additive";
	case AnimationPropertyWriteMode::Multiply: return "Multiply";
	case AnimationPropertyWriteMode::Override:
	default: return "Override";
	}
}
