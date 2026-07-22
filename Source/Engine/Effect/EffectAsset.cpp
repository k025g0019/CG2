#include "EffectAsset.h"

#include "ThirdParty/imgui-node-editor-master/imgui-node-editor-master/crude_json.h"

#include <algorithm>
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
}

bool EffectAsset::LoadFromJson(const std::string& filePath) {
	const auto [rootValue, isLoaded] = JsonValue::load(filePath);
	if (!isLoaded || !rootValue.is_object()) {
		return false;
	}

	EffectAsset loadedAsset{};
	loadedAsset.renderAssetPath = ReadString(rootValue, "renderAsset", "");
	loadedAsset.duration = (std::max)(ReadFloat(rootValue, "duration", loadedAsset.duration), 0.01f);
	loadedAsset.startDelay = (std::max)(ReadFloat(rootValue, "startDelay", loadedAsset.startDelay), 0.0f);
	loadedAsset.emissionRate = (std::max)(ReadFloat(rootValue, "emissionRate", loadedAsset.emissionRate), 0.0f);
	loadedAsset.lifetime = (std::max)(ReadFloat(rootValue, "lifetime", loadedAsset.lifetime), 0.01f);
	loadedAsset.speed = (std::max)(ReadFloat(rootValue, "speed", loadedAsset.speed), 0.0f);
	loadedAsset.startSize = (std::max)(ReadFloat(rootValue, "startSize", loadedAsset.startSize), 0.001f);
	loadedAsset.endSize = (std::max)(ReadFloat(rootValue, "endSize", loadedAsset.endSize), 0.0f);
	loadedAsset.gravity = ReadFloat(rootValue, "gravity", loadedAsset.gravity);
	loadedAsset.drag = (std::max)(ReadFloat(rootValue, "drag", loadedAsset.drag), 0.0f);
	loadedAsset.rotationSpeed = ReadFloat(rootValue, "rotationSpeed", loadedAsset.rotationSpeed);
	loadedAsset.shapeRadius = (std::max)(ReadFloat(rootValue, "shapeRadius", loadedAsset.shapeRadius), 0.0f);
	loadedAsset.shapeAngleDegrees = (std::clamp)(ReadFloat(rootValue, "shapeAngle", loadedAsset.shapeAngleDegrees), 0.0f, 179.0f);
	loadedAsset.speedRandomness = (std::clamp)(ReadFloat(rootValue, "speedRandomness", loadedAsset.speedRandomness), 0.0f, 1.0f);
	loadedAsset.lifetimeRandomness = (std::clamp)(ReadFloat(rootValue, "lifetimeRandomness", loadedAsset.lifetimeRandomness), 0.0f, 1.0f);
	loadedAsset.sizeRandomness = (std::clamp)(ReadFloat(rootValue, "sizeRandomness", loadedAsset.sizeRandomness), 0.0f, 1.0f);
	loadedAsset.startAlpha = (std::clamp)(ReadFloat(rootValue, "startAlpha", loadedAsset.startAlpha), 0.0f, 1.0f);
	loadedAsset.endAlpha = (std::clamp)(ReadFloat(rootValue, "endAlpha", loadedAsset.endAlpha), 0.0f, 1.0f);
	loadedAsset.emissionStrength = (std::max)(ReadFloat(rootValue, "emissionStrength", loadedAsset.emissionStrength), 0.0f);
	loadedAsset.endSpeedMultiplier = (std::max)(ReadFloat(rootValue, "endSpeedMultiplier", loadedAsset.endSpeedMultiplier), 0.0f);
	loadedAsset.noiseStrength = (std::max)(ReadFloat(rootValue, "noiseStrength", loadedAsset.noiseStrength), 0.0f);
	loadedAsset.noiseFrequency = (std::max)(ReadFloat(rootValue, "noiseFrequency", loadedAsset.noiseFrequency), 0.0f);
	loadedAsset.collisionBounce = (std::clamp)(ReadFloat(rootValue, "collisionBounce", loadedAsset.collisionBounce), 0.0f, 1.0f);
	loadedAsset.collisionFriction = (std::clamp)(ReadFloat(rootValue, "collisionFriction", loadedAsset.collisionFriction), 0.0f, 1.0f);
	loadedAsset.angularSpeed = ReadFloat(rootValue, "angularSpeed", loadedAsset.angularSpeed);
	loadedAsset.radialAcceleration = ReadFloat(rootValue, "radialAcceleration", loadedAsset.radialAcceleration);
	loadedAsset.waveAmplitude = ReadFloat(rootValue, "waveAmplitude", loadedAsset.waveAmplitude);
	loadedAsset.waveFrequency = (std::max)(ReadFloat(rootValue, "waveFrequency", loadedAsset.waveFrequency), 0.0f);
	loadedAsset.attractorStrength = (std::max)(ReadFloat(rootValue, "attractorStrength", loadedAsset.attractorStrength), 0.0f);
	loadedAsset.maxCount = (std::max)(ReadInt(rootValue, "maxCount", loadedAsset.maxCount), 1);
	loadedAsset.burstCount = (std::clamp)(ReadInt(rootValue, "burstCount", loadedAsset.burstCount), 0, loadedAsset.maxCount);
	loadedAsset.shape = (std::clamp)(ReadInt(rootValue, "shape", loadedAsset.shape), 0, 3);
	loadedAsset.simulationSpace = (std::clamp)(ReadInt(rootValue, "simulationSpace", loadedAsset.simulationSpace), 0, 1);
	loadedAsset.motionType = (std::clamp)(ReadInt(rootValue, "motionType", loadedAsset.motionType), 0, 6);
	loadedAsset.playOnAwake = ReadBool(rootValue, "playOnAwake", loadedAsset.playOnAwake);
	loadedAsset.looping = ReadBool(rootValue, "looping", loadedAsset.looping);
	loadedAsset.collision = ReadBool(rootValue, "collision", loadedAsset.collision);
	loadedAsset.prewarm = ReadBool(rootValue, "prewarm", loadedAsset.prewarm);
	loadedAsset.startColor = ReadVector3(rootValue, "startColor", loadedAsset.startColor);
	loadedAsset.endColor = ReadVector3(rootValue, "endColor", loadedAsset.endColor);
	loadedAsset.direction = ReadVector3(rootValue, "direction", loadedAsset.direction);
	loadedAsset.boxSize = ReadVector3(rootValue, "boxSize", loadedAsset.boxSize);
	loadedAsset.motionCenter = ReadVector3(rootValue, "motionCenter", loadedAsset.motionCenter);
	*this = std::move(loadedAsset);
	return true;
}

void EffectAsset::ApplyToComponent(EditorComponent& component) const {
	component.animationPlayOnAwake = playOnAwake;
	component.particleLooping = looping;
	component.particleCollision = collision;
	component.particlePrewarm = prewarm;
	component.particleDuration = duration;
	component.particleStartDelay = startDelay;
	component.particleRate = emissionRate;
	component.particleLifetime = lifetime;
	component.particleSpeed = speed;
	component.particleSize = startSize;
	component.particleEndSize = endSize;
	component.particleGravity = gravity;
	component.particleDrag = drag;
	component.particleRotationSpeed = rotationSpeed;
	component.particleShapeRadius = shapeRadius;
	component.particleShapeAngle = shapeAngleDegrees;
	component.particleSpeedRandomness = speedRandomness;
	component.particleLifetimeRandomness = lifetimeRandomness;
	component.particleSizeRandomness = sizeRandomness;
	component.particleStartAlpha = startAlpha;
	component.particleEndAlpha = endAlpha;
	component.particleEmissionStrength = emissionStrength;
	component.particleEndSpeedMultiplier = endSpeedMultiplier;
	component.particleNoiseStrength = noiseStrength;
	component.particleNoiseFrequency = noiseFrequency;
	component.particleCollisionBounce = collisionBounce;
	component.particleCollisionFriction = collisionFriction;
	component.particleMotionType = motionType;
	component.particleMotionCenter = motionCenter;
	component.particleAngularSpeed = angularSpeed;
	component.particleRadialAcceleration = radialAcceleration;
	component.particleWaveAmplitude = waveAmplitude;
	component.particleWaveFrequency = waveFrequency;
	component.particleAttractorStrength = attractorStrength;
	component.particleMaxCount = maxCount;
	component.particleBurstCount = burstCount;
	component.particleShape = shape;
	component.particleSimulationSpace = simulationSpace;
	component.color = startColor;
	component.particleEndColor = endColor;
	component.particleDirection = direction;
	component.particleBoxSize = boxSize;
	component.particleRenderAssetPath = renderAssetPath;
}
