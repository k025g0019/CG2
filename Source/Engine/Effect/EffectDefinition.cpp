#include "EffectDefinition.h"

#include "ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"

#include <algorithm>
#include <cctype>

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

	Vector3 ReadVector3(const JsonValue& objectValue, const std::string& name, const Vector3& fallbackValue) {
		const JsonValue* value = FindMember(objectValue, name);
		if (value == nullptr || !value->is_object()) {
			return fallbackValue;
		}

		return {
			ReadFloat(*value, "x", fallbackValue.x),
			ReadFloat(*value, "y", fallbackValue.y),
			ReadFloat(*value, "z", fallbackValue.z)};
	}

	std::string ToLower(const std::string& value) {
		std::string lowered = value;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return lowered;
	}

	EffectNodeType ReadNodeType(const JsonValue& objectValue) {
		const std::string typeName = ToLower(ReadString(objectValue, "nodeType", "Billboard"));
		if (typeName == "ribbon" || typeName == "trail") {
			return EffectNodeType::Ribbon;
		}
		if (typeName == "ring") {
			return EffectNodeType::Ring;
		}
		if (typeName == "meshparticle") {
			return EffectNodeType::MeshParticle;
		}
		if (typeName == "decal") {
			return EffectNodeType::Decal;
		}
		return EffectNodeType::Billboard;
	}

	EffectBillboardMode ReadBillboardMode(const JsonValue& objectValue) {
		const std::string modeName = ToLower(ReadString(objectValue, "billboardMode", "CameraFacing"));
		if (modeName == "yaxis" || modeName == "yaxisbillboard") {
			return EffectBillboardMode::YAxisBillboard;
		}
		if (modeName == "directional") {
			return EffectBillboardMode::Directional;
		}
		if (modeName == "fixed") {
			return EffectBillboardMode::Fixed;
		}
		return EffectBillboardMode::CameraFacing;
	}

	EffectBlendMode ReadBlendMode(const JsonValue& objectValue) {
		const std::string modeName = ToLower(ReadString(objectValue, "blendMode", "AlphaBlend"));
		if (modeName == "additive") {
			return EffectBlendMode::Additive;
		}
		if (modeName == "premultiplied") {
			return EffectBlendMode::Premultiplied;
		}
		if (modeName == "multiply") {
			return EffectBlendMode::Multiply;
		}
		return EffectBlendMode::AlphaBlend;
	}

	EffectNodeDefinition ReadNode(const JsonValue& nodeValue) {
		EffectNodeDefinition node{};
		node.name = ReadString(nodeValue, "name", "Node");
		node.nodeType = ReadNodeType(nodeValue);
		node.texturePath = ReadString(nodeValue, "texture", "");
		node.blendMode = ReadBlendMode(nodeValue);
		node.useSoftParticle = ReadBool(nodeValue, "useSoftParticle", node.useSoftParticle);
		node.softParticleFadeDistance = (std::max)(ReadFloat(nodeValue, "softParticleFadeDistance", node.softParticleFadeDistance), 0.001f);
		node.useGpuSimulation = ReadBool(nodeValue, "useGpuSimulation", node.useGpuSimulation);
		node.meshAssetPath = ReadString(nodeValue, "meshAsset", node.meshAssetPath);
		node.billboardMode = ReadBillboardMode(nodeValue);
		node.direction = ReadVector3(nodeValue, "direction", node.direction);
		node.emissionRate = (std::max)(ReadFloat(nodeValue, "emissionRate", node.emissionRate), 0.0f);
		node.burstCount = (std::max)(ReadInt(nodeValue, "burstCount", node.burstCount), 0);
		node.maxCount = (std::max)(ReadInt(nodeValue, "maxCount", node.maxCount), 1);
		node.lifetime = (std::max)(ReadFloat(nodeValue, "lifetime", node.lifetime), 0.01f);
		node.lifetimeRandomness = (std::clamp)(ReadFloat(nodeValue, "lifetimeRandomness", node.lifetimeRandomness), 0.0f, 1.0f);
		node.speed = ReadFloat(nodeValue, "speed", node.speed);
		node.speedRandomness = (std::clamp)(ReadFloat(nodeValue, "speedRandomness", node.speedRandomness), 0.0f, 1.0f);
		node.gravity = ReadFloat(nodeValue, "gravity", node.gravity);
		node.drag = (std::max)(ReadFloat(nodeValue, "drag", node.drag), 0.0f);
		node.sizeStart = (std::max)(ReadFloat(nodeValue, "sizeStart", node.sizeStart), 0.0f);
		node.sizeEnd = (std::max)(ReadFloat(nodeValue, "sizeEnd", node.sizeEnd), 0.0f);
		node.sizeRandomness = (std::clamp)(ReadFloat(nodeValue, "sizeRandomness", node.sizeRandomness), 0.0f, 1.0f);
		node.rotationSpeedDegrees = ReadFloat(nodeValue, "rotationSpeed", node.rotationSpeedDegrees);
		node.stretchScale = (std::max)(ReadFloat(nodeValue, "stretchScale", node.stretchScale), 0.0f);
		node.shapeRadius = (std::max)(ReadFloat(nodeValue, "shapeRadius", node.shapeRadius), 0.0f);
		node.colorStart = ReadVector3(nodeValue, "colorStart", node.colorStart);
		node.colorEnd = ReadVector3(nodeValue, "colorEnd", node.colorEnd);
		node.alphaStart = (std::clamp)(ReadFloat(nodeValue, "alphaStart", node.alphaStart), 0.0f, 1.0f);
		node.alphaEnd = (std::clamp)(ReadFloat(nodeValue, "alphaEnd", node.alphaEnd), 0.0f, 1.0f);

		node.useFlipbook = ReadBool(nodeValue, "useFlipbook", node.useFlipbook);
		node.flipbookColumns = (std::max)(ReadInt(nodeValue, "flipbookColumns", node.flipbookColumns), 1);
		node.flipbookRows = (std::max)(ReadInt(nodeValue, "flipbookRows", node.flipbookRows), 1);
		node.flipbookStartFrame = (std::max)(ReadInt(nodeValue, "flipbookStartFrame", node.flipbookStartFrame), 0);
		node.flipbookEndFrame = (std::max)(
			ReadInt(nodeValue, "flipbookEndFrame", node.flipbookColumns * node.flipbookRows - 1),
			node.flipbookStartFrame);
		node.flipbookFps = (std::max)(ReadFloat(nodeValue, "flipbookFps", node.flipbookFps), 0.01f);
		node.flipbookLoop = ReadBool(nodeValue, "flipbookLoop", node.flipbookLoop);

		node.ribbonWidth = (std::max)(ReadFloat(nodeValue, "ribbonWidth", node.ribbonWidth), 0.001f);
		node.ribbonPointLifetime = (std::max)(ReadFloat(nodeValue, "ribbonPointLifetime", node.ribbonPointLifetime), 0.01f);
		node.ribbonMaxPoints = (std::clamp)(ReadInt(nodeValue, "ribbonMaxPoints", node.ribbonMaxPoints), 2, 512);
		node.ribbonMinVertexDistance = (std::max)(ReadFloat(nodeValue, "ribbonMinVertexDistance", node.ribbonMinVertexDistance), 0.0f);
		node.ribbonUvScrollSpeed = ReadFloat(nodeValue, "ribbonUvScrollSpeed", node.ribbonUvScrollSpeed);

		node.ringInnerRadiusStart = (std::max)(ReadFloat(nodeValue, "ringInnerRadiusStart", node.ringInnerRadiusStart), 0.0f);
		node.ringOuterRadiusStart = (std::max)(ReadFloat(nodeValue, "ringOuterRadiusStart", node.ringOuterRadiusStart), 0.0f);
		node.ringInnerRadiusEnd = (std::max)(ReadFloat(nodeValue, "ringInnerRadiusEnd", node.ringInnerRadiusEnd), 0.0f);
		node.ringOuterRadiusEnd = (std::max)(ReadFloat(nodeValue, "ringOuterRadiusEnd", node.ringOuterRadiusEnd), 0.0f);
		node.ringLifetime = (std::max)(ReadFloat(nodeValue, "ringLifetime", node.ringLifetime), 0.01f);
		node.ringSegments = (std::clamp)(ReadInt(nodeValue, "ringSegments", node.ringSegments), 3, 128);

		node.sortByDistance = ReadBool(
			nodeValue,
			"sortByDistance",
			node.blendMode != EffectBlendMode::Additive);
		return node;
	}
}

bool EffectDefinition::LoadFromJson(const std::string& filePath) {
	const auto [rootValue, isLoaded] = JsonValue::load(filePath);
	if (!isLoaded || !rootValue.is_object()) {
		return false;
	}

	EffectDefinition loadedDefinition{};
	loadedDefinition.id = ReadString(rootValue, "id", "");
	loadedDefinition.maxConcurrentInstances = (std::max)(
		ReadInt(rootValue, "maxConcurrentInstances", loadedDefinition.maxConcurrentInstances), 1);

	const JsonValue* lodValue = FindMember(rootValue, "lod");
	if (lodValue != nullptr && lodValue->is_array()) {
		for (const JsonValue& levelValue : lodValue->get<crude_json::array>()) {
			if (!levelValue.is_object()) {
				continue;
			}

			EffectLodLevel level{};
			level.distance = (std::max)(ReadFloat(levelValue, "distance", level.distance), 0.0f);
			level.spawnMultiplier = (std::clamp)(ReadFloat(levelValue, "spawnMultiplier", level.spawnMultiplier), 0.0f, 1.0f);
			loadedDefinition.lodLevels.push_back(level);
		}

		std::sort(
			loadedDefinition.lodLevels.begin(),
			loadedDefinition.lodLevels.end(),
			[](const EffectLodLevel& lhs, const EffectLodLevel& rhs) {
				return lhs.distance < rhs.distance;
			});
	}

	const JsonValue* nodesValue = FindMember(rootValue, "nodes");
	if (nodesValue != nullptr && nodesValue->is_array()) {
		for (const JsonValue& nodeValue : nodesValue->get<crude_json::array>()) {
			if (!nodeValue.is_object()) {
				continue;
			}

			loadedDefinition.nodes.push_back(ReadNode(nodeValue));
		}
	}

	*this = std::move(loadedDefinition);
	return true;
}
