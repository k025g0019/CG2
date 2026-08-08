#pragma once

#include "EditorScriptApi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

//================================================================
// C++ Script 制作用の高水準 Runtime API
//================================================================

class EditorNativeScriptRuntime final {
public:
	static void SetRuntimeApi(const EditorScriptRuntimeApi* runtimeApi) {
		runtimeApi_ = runtimeApi;
	}

	static const EditorScriptRuntimeApi* GetRuntimeApi() {
		return runtimeApi_;
	}

private:
	inline static const EditorScriptRuntimeApi* runtimeApi_ = nullptr;
};

enum class KeyCode : int32_t {
	Escape = 0x01,
	Digit1 = 0x02,
	Digit2 = 0x03,
	Digit3 = 0x04,
	Digit4 = 0x05,
	Digit5 = 0x06,
	Digit6 = 0x07,
	Digit7 = 0x08,
	Digit8 = 0x09,
	Digit9 = 0x0A,
	Digit0 = 0x0B,
	Tab = 0x0F,
	Q = 0x10,
	W = 0x11,
	E = 0x12,
	R = 0x13,
	T = 0x14,
	Y = 0x15,
	U = 0x16,
	I = 0x17,
	O = 0x18,
	P = 0x19,
	Enter = 0x1C,
	LeftControl = 0x1D,
	A = 0x1E,
	S = 0x1F,
	D = 0x20,
	F = 0x21,
	G = 0x22,
	H = 0x23,
	J = 0x24,
	K = 0x25,
	L = 0x26,
	LeftShift = 0x2A,
	Z = 0x2C,
	X = 0x2D,
	C = 0x2E,
	V = 0x2F,
	B = 0x30,
	N = 0x31,
	M = 0x32,
	Space = 0x39,
	UpArrow = 0xC8,
	LeftArrow = 0xCB,
	RightArrow = 0xCD,
	DownArrow = 0xD0,
};

class Input final {
public:
	static bool GetKey(KeyCode keyCode) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->IsKeyDown == nullptr) {
			return false;
		}

		return runtimeApi->IsKeyDown(static_cast<int32_t>(keyCode));
	}

	static bool GetKeyDown(KeyCode keyCode) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->IsKeyPressed == nullptr) {
			return false;
		}

		return runtimeApi->IsKeyPressed(static_cast<int32_t>(keyCode));
	}
};

class SceneManager final {
public:
	static bool LoadScene(const char* scenePath) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->LoadScene == nullptr ||
			scenePath == nullptr || scenePath[0] == '\0') {
			return false;
		}

		return runtimeApi->LoadScene(scenePath);
	}

	static bool LoadScene(const std::string& scenePath) {
		return LoadScene(scenePath.c_str());
	}

	static bool LoadScene(int32_t sceneBuildIndex) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->LoadSceneByBuildIndex == nullptr) {
			return false;
		}

		return runtimeApi->LoadSceneByBuildIndex(sceneBuildIndex);
	}

	static bool LoadSceneAsync(const std::string& scenePath) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !scenePath.empty() && runtimeApi != nullptr && runtimeApi->LoadSceneAsync != nullptr &&
			runtimeApi->LoadSceneAsync(scenePath.c_str(), false);
	}

	static bool LoadSceneAdditiveAsync(const std::string& scenePath) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !scenePath.empty() && runtimeApi != nullptr && runtimeApi->LoadSceneAsync != nullptr &&
			runtimeApi->LoadSceneAsync(scenePath.c_str(), true);
	}

	static bool UnloadScene(const std::string& scenePath) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !scenePath.empty() && runtimeApi != nullptr && runtimeApi->UnloadScene != nullptr &&
			runtimeApi->UnloadScene(scenePath.c_str());
	}

	static float GetLoadProgress() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->GetSceneLoadProgress != nullptr
			? runtimeApi->GetSceneLoadProgress()
			: 0.0f;
	}

	static bool IsLoading() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->IsSceneLoading != nullptr &&
			runtimeApi->IsSceneLoading();
	}

	static bool IsLoaded(const std::string& scenePath) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !scenePath.empty() && runtimeApi != nullptr && runtimeApi->IsSceneLoaded != nullptr &&
			runtimeApi->IsSceneLoaded(scenePath.c_str());
	}

	static void SetFloat(const std::string& key, float value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (!key.empty() && runtimeApi != nullptr && runtimeApi->SetSceneFloat != nullptr) {
			runtimeApi->SetSceneFloat(key.c_str(), value);
		}
	}

	static bool GetFloat(const std::string& key, float& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !key.empty() && runtimeApi != nullptr && runtimeApi->GetSceneFloat != nullptr &&
			runtimeApi->GetSceneFloat(key.c_str(), &value);
	}

	static void SetString(const std::string& key, const std::string& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (!key.empty() && runtimeApi != nullptr && runtimeApi->SetSceneString != nullptr) {
			runtimeApi->SetSceneString(key.c_str(), value.c_str());
		}
	}

	static bool GetString(const std::string& key, std::string& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		char textBuffer[512]{};
		if (key.empty() || runtimeApi == nullptr || runtimeApi->GetSceneString == nullptr ||
			!runtimeApi->GetSceneString(key.c_str(), textBuffer, static_cast<int32_t>(sizeof(textBuffer)))) {
			return false;
		}

		value = textBuffer;
		return true;
	}
};

class GameObject final {
public:
	explicit GameObject(int32_t gameObjectId = -1)
		: gameObjectId_(gameObjectId) {
	}

	static GameObject Find(const char* gameObjectName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->FindGameObjectByName == nullptr ||
			gameObjectName == nullptr || gameObjectName[0] == '\0') {
			return GameObject{};
		}

		return GameObject{runtimeApi->FindGameObjectByName(gameObjectName)};
	}

	int32_t GetInstanceId() const {
		return gameObjectId_;
	}

	bool HasReference() const {
		return gameObjectId_ >= 0;
	}

	bool IsActive() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || runtimeApi == nullptr || runtimeApi->IsGameObjectActive == nullptr) {
			return false;
		}

		return runtimeApi->IsGameObjectActive(gameObjectId_);
	}

	bool SetActive(bool isActive) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || runtimeApi == nullptr || runtimeApi->SetGameObjectActive == nullptr) {
			return false;
		}

		return runtimeApi->SetGameObjectActive(gameObjectId_, isActive);
	}

	bool SetComponentActive(const char* componentTypeName, bool isActive) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || componentTypeName == nullptr || componentTypeName[0] == '\0' ||
			runtimeApi == nullptr || runtimeApi->SetComponentActive == nullptr) {
			return false;
		}

		return runtimeApi->SetComponentActive(gameObjectId_, componentTypeName, isActive);
	}

	bool IsComponentActive(const char* componentTypeName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return HasReference() && componentTypeName != nullptr && componentTypeName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->IsComponentActive != nullptr &&
			runtimeApi->IsComponentActive(gameObjectId_, componentTypeName);
	}

	bool HasComponent(const char* componentTypeName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return HasReference() && componentTypeName != nullptr && componentTypeName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->HasComponent != nullptr &&
			runtimeApi->HasComponent(gameObjectId_, componentTypeName);
	}

	bool InvokeAction(const char* functionName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return HasReference() && functionName != nullptr && functionName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->InvokeScriptAction != nullptr &&
			runtimeApi->InvokeScriptAction(gameObjectId_, functionName);
	}

	bool InvokeAction(const char* functionName, const EditorScriptActionPayload& payload) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && functionName != nullptr && functionName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->InvokeScriptActionPayload != nullptr &&
			runtimeApi->InvokeScriptActionPayload(gameObjectId_, functionName, &payload);
	}

	EditorScriptTransform GetTransform() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || runtimeApi == nullptr || runtimeApi->GetTransform == nullptr) {
			return EditorScriptTransform{};
		}

		return runtimeApi->GetTransform(gameObjectId_);
	}

	bool SetTransform(const EditorScriptTransform& transform) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || runtimeApi == nullptr || runtimeApi->SetTransform == nullptr) {
			return false;
		}

		runtimeApi->SetTransform(gameObjectId_, &transform);
		return true;
	}

private:
	int32_t gameObjectId_ = -1;
};

class RailFollower final {
public:
	explicit RailFollower(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit RailFollower(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	bool Pause() const {
		return SetPaused(true);
	}

	bool Resume() const {
		return SetPaused(false);
	}

	bool SetPaused(bool isPaused) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SetRailPaused == nullptr) {
			return false;
		}

		return runtimeApi->SetRailPaused(gameObjectId_, isPaused);
	}

	bool IsPaused() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->IsRailPaused != nullptr &&
			runtimeApi->IsRailPaused(gameObjectId_);
	}

	bool SetSpeed(float speed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRailSpeed != nullptr &&
			runtimeApi->SetRailSpeed(gameObjectId_, speed);
	}

	bool SetReverse(bool isReversed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRailReverse != nullptr &&
			runtimeApi->SetRailReverse(gameObjectId_, isReversed);
	}

	bool JumpTo(float normalizedProgress) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->SetRailNormalizedProgress != nullptr &&
			runtimeApi->SetRailNormalizedProgress(gameObjectId_, normalizedProgress);
	}

	bool SwitchRail(const GameObject& railPathGameObject, bool preservesProgress = true) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && railPathGameObject.HasReference() && runtimeApi != nullptr &&
			runtimeApi->SetRailPath != nullptr &&
			runtimeApi->SetRailPath(
				gameObjectId_,
				railPathGameObject.GetInstanceId(),
				preservesProgress);
	}

	bool SetMoveInput(const EditorScriptVector2& moveInput) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRailMoveInput != nullptr &&
			runtimeApi->SetRailMoveInput(gameObjectId_, &moveInput);
	}

	bool SetOffset(const EditorScriptVector2& offset) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRailOffset != nullptr &&
			runtimeApi->SetRailOffset(gameObjectId_, &offset);
	}

	bool ClearMoveInput() const {
		return SetMoveInput({0.0f, 0.0f});
	}

	bool GetOffset(EditorScriptVector2& offset) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailOffset != nullptr &&
			runtimeApi->GetRailOffset(gameObjectId_, &offset);
	}

	bool GetNormalizedProgress(float& normalizedProgress) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetRailNormalizedProgress != nullptr &&
			runtimeApi->GetRailNormalizedProgress(gameObjectId_, &normalizedProgress);
	}

	bool GetLength(float& railLength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailLength != nullptr &&
			runtimeApi->GetRailLength(gameObjectId_, &railLength);
	}

	bool GetPosition(float normalizedProgress, EditorScriptVector3& position) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailPosition != nullptr &&
			runtimeApi->GetRailPosition(gameObjectId_, normalizedProgress, &position);
	}

	bool GetDirection(float normalizedProgress, EditorScriptVector3& direction) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailDirection != nullptr &&
			runtimeApi->GetRailDirection(gameObjectId_, normalizedProgress, &direction);
	}

	bool SetDistance(float distance) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRailDistance != nullptr &&
			runtimeApi->SetRailDistance(gameObjectId_, distance);
	}

	bool GetState(EditorScriptRailState& state) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailState != nullptr &&
			runtimeApi->GetRailState(gameObjectId_, &state);
	}

	bool SetSpeedProfileEnabled(bool isEnabled) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->SetRailSpeedProfileEnabled != nullptr &&
			runtimeApi->SetRailSpeedProfileEnabled(gameObjectId_, isEnabled);
	}

	bool GetSpeedMultiplier(float& speedMultiplier) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetRailSpeedMultiplier != nullptr &&
			runtimeApi->GetRailSpeedMultiplier(gameObjectId_, &speedMultiplier);
	}

	std::string GetActiveZone() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		char zoneId[128]{};

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetRailActiveZone == nullptr ||
			!runtimeApi->GetRailActiveZone(
				gameObjectId_,
				zoneId,
				static_cast<int32_t>(sizeof(zoneId)))) {
			return {};
		}

		return zoneId;
	}

	bool RearmEventMarkers(const char* markerId = "") const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->RearmRailEventMarkers != nullptr && markerId != nullptr &&
			runtimeApi->RearmRailEventMarkers(gameObjectId_, markerId);
	}

	bool GetClosestProgress(
		const EditorScriptVector3& worldPosition,
		float& normalizedProgress) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetRailClosestProgress != nullptr &&
			runtimeApi->GetRailClosestProgress(
				gameObjectId_,
				&worldPosition,
				&normalizedProgress);
	}

	bool GetFrame(float normalizedProgress, EditorScriptRailFrame& frame) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetRailFrame != nullptr &&
			runtimeApi->GetRailFrame(gameObjectId_, normalizedProgress, &frame);
	}

	bool ConsumeEndReached() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->ConsumeRailEndReached != nullptr &&
			runtimeApi->ConsumeRailEndReached(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class SimulationLod final {
public:
	explicit SimulationLod(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetLevel(int32_t& lodLevel) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetSimulationLodLevel != nullptr &&
			runtimeApi->GetSimulationLodLevel(gameObjectId_, &lodLevel);
	}

private:
	int32_t gameObjectId_ = -1;
};

class Physics final {
public:
	static bool ViewportPointToRay(const EditorScriptVector2& normalizedPosition, EditorScriptRay& ray) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->ViewportPointToRay != nullptr &&
			runtimeApi->ViewportPointToRay(&normalizedPosition, &ray);
	}

	static bool GetAimRay(const GameObject& screenAimGameObject, EditorScriptRay& ray) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t screenAimGameObjectId = screenAimGameObject.HasReference()
			? screenAimGameObject.GetInstanceId()
			: -1;
		return runtimeApi != nullptr && runtimeApi->GetAimRay != nullptr &&
			runtimeApi->GetAimRay(screenAimGameObjectId, &ray);
	}

	static bool Raycast(const EditorScriptRay& ray, float distance, EditorScriptPhysicsHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->PhysicsRaycast != nullptr &&
			runtimeApi->PhysicsRaycast(&ray, distance, &hit);
	}

	static bool SphereCast(
		const EditorScriptRay& ray,
		float radius,
		float distance,
		EditorScriptPhysicsHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->PhysicsSphereCast != nullptr &&
			runtimeApi->PhysicsSphereCast(&ray, radius, distance, &hit);
	}

	static bool CapsuleCast(
		const EditorScriptRay& ray,
		float radius,
		float height,
		float distance,
		EditorScriptPhysicsHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->PhysicsCapsuleCast != nullptr &&
			runtimeApi->PhysicsCapsuleCast(&ray, radius, height, distance, &hit);
	}

	static bool SampleOceanSurface(
		const GameObject& queryGameObject,
		const EditorScriptVector3& worldPosition,
		EditorScriptOceanSurfaceHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t queryGameObjectId = queryGameObject.HasReference()
			? queryGameObject.GetInstanceId()
			: -1;
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 6U &&
			runtimeApi->SampleOceanSurface != nullptr &&
			 runtimeApi->SampleOceanSurface(queryGameObjectId, &worldPosition, &hit);
	}

	static int32_t AddExplosionImpulse(
		const EditorScriptVector3& center,
		float radius,
		float impulseStrength,
		float upwardModifier = 0.0f) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AddExplosionImpulse != nullptr
			? runtimeApi->AddExplosionImpulse(&center, radius, impulseStrength, upwardModifier)
			: 0;
	}
};

class Ocean final {
public:
	static bool Sample(
		const GameObject& queryGameObject,
		const EditorScriptVector3& worldPosition,
		EditorScriptOceanSurfaceHit& hit) {
		return Physics::SampleOceanSurface(queryGameObject, worldPosition, hit);
	}

	static bool SampleDetailed(
		const GameObject& queryGameObject,
		const EditorScriptVector3& worldPosition,
		EditorScriptOceanSurfaceHit& hit,
		float& foam) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->SampleOceanSurfaceDetailed != nullptr &&
			runtimeApi->SampleOceanSurfaceDetailed(
				queryGameObject.HasReference() ? queryGameObject.GetInstanceId() : -1,
				&worldPosition,
				&hit,
				&foam);
	}

	static bool SegmentCast(
		const GameObject& queryGameObject,
		const EditorScriptVector3& startPosition,
		const EditorScriptVector3& endPosition,
		EditorScriptOceanSegmentHit& hit,
		float clearance = 0.0f,
		const GameObject& oceanGameObject = GameObject{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->OceanSegmentCast != nullptr &&
			runtimeApi->OceanSegmentCast(
				queryGameObject.HasReference() ? queryGameObject.GetInstanceId() : -1,
				oceanGameObject.HasReference() ? oceanGameObject.GetInstanceId() : -1,
				&startPosition,
				&endPosition,
				clearance,
				&hit);
	}

	static bool Raycast(
		const GameObject& queryGameObject,
		const EditorScriptRay& ray,
		float maximumDistance,
		EditorScriptOceanSegmentHit& hit,
		float clearance = 0.0f,
		const GameObject& oceanGameObject = GameObject{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->OceanRaycast != nullptr &&
			runtimeApi->OceanRaycast(
				queryGameObject.HasReference() ? queryGameObject.GetInstanceId() : -1,
				oceanGameObject.HasReference() ? oceanGameObject.GetInstanceId() : -1,
				&ray,
				maximumDistance,
				clearance,
				&hit);
	}

	static bool IsOccluded(
		const GameObject& queryGameObject,
		const EditorScriptVector3& startPosition,
		const EditorScriptVector3& endPosition,
		EditorScriptOceanOcclusion& occlusion,
		float clearance = 0.0f,
		const GameObject& oceanGameObject = GameObject{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->QueryOceanOcclusion != nullptr &&
			runtimeApi->QueryOceanOcclusion(
				queryGameObject.HasReference() ? queryGameObject.GetInstanceId() : -1,
				oceanGameObject.HasReference() ? oceanGameObject.GetInstanceId() : -1,
				&startPosition,
				&endPosition,
				clearance,
				&occlusion);
	}
};

class Health final {
public:
	explicit Health(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Damage(float damage, const GameObject& sourceGameObject = GameObject{}) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t sourceGameObjectId = sourceGameObject.HasReference()
			? sourceGameObject.GetInstanceId()
			: -1;
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ApplyDamage != nullptr &&
			runtimeApi->ApplyDamage(gameObjectId_, damage, sourceGameObjectId);
	}

	bool Damage(EditorScriptDamageContext& damageContext) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		damageContext.targetGameObjectId = gameObjectId_;
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ApplyDamageContext != nullptr &&
			runtimeApi->ApplyDamageContext(&damageContext);
	}

	bool GetLastDamageContext(EditorScriptDamageContext& damageContext) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetLastDamageContext != nullptr &&
			runtimeApi->GetLastDamageContext(gameObjectId_, &damageContext);
	}

	bool Get(float& currentHealth, float& maximumHealth) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetHealth != nullptr &&
			runtimeApi->GetHealth(gameObjectId_, &currentHealth, &maximumHealth);
	}

	bool Set(float currentHealth) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetHealth != nullptr &&
			runtimeApi->SetHealth(gameObjectId_, currentHealth);
	}

private:
	int32_t gameObjectId_ = -1;
};

class ObjectPool final {
public:
	explicit ObjectPool(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	GameObject Spawn(
		const EditorScriptVector3& position,
		const EditorScriptVector3& rotation = EditorScriptVector3{}) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SpawnFromPool == nullptr) {
			return GameObject{};
		}

		return GameObject(runtimeApi->SpawnFromPool(gameObjectId_, &position, &rotation));
	}

	static bool Release(const GameObject& gameObject) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->ReleaseToPool != nullptr &&
			runtimeApi->ReleaseToPool(gameObject.GetInstanceId());
	}

private:
	int32_t gameObjectId_ = -1;
};

class Spawner final {
public:
	explicit Spawner(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	GameObject Spawn() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SpawnFromSpawner == nullptr) {
			return GameObject{};
		}

		return GameObject(runtimeApi->SpawnFromSpawner(gameObjectId_));
	}

private:
	int32_t gameObjectId_ = -1;
};

class WaveSpawner final {
public:
	explicit WaveSpawner(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Start() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->StartWaveSpawner != nullptr &&
			runtimeApi->StartWaveSpawner(gameObjectId_);
	}

	bool IsComplete(bool waitsForAllDefeated = true) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->IsWaveSpawnerComplete != nullptr &&
			runtimeApi->IsWaveSpawnerComplete(gameObjectId_, waitsForAllDefeated);
	}

private:
	int32_t gameObjectId_ = -1;
};

class Weapon final {
public:
	explicit Weapon(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool FireHitscan() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->FireHitscan != nullptr &&
			runtimeApi->FireHitscan(gameObjectId_);
	}

	bool FireProjectile() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->FireProjectile != nullptr &&
			runtimeApi->FireProjectile(gameObjectId_);
	}

	float GetAccuracySpread() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		float spreadDegrees = 0.0f;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetWeaponAccuracySpread == nullptr ||
			!runtimeApi->GetWeaponAccuracySpread(gameObjectId_, &spreadDegrees)) {
			return 0.0f;
		}

		return spreadDegrees;
	}

private:
	int32_t gameObjectId_ = -1;
};

class WeaponLoadout final {
public:
	explicit WeaponLoadout(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Select(int32_t slotIndex) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSelectSlot != nullptr &&
			runtimeApi->LoadoutSelectSlot(gameObjectId_, slotIndex);
	}

	bool Next() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSelectNext != nullptr &&
			runtimeApi->LoadoutSelectNext(gameObjectId_);
	}

	bool Previous() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSelectPrevious != nullptr &&
			runtimeApi->LoadoutSelectPrevious(gameObjectId_);
	}

	bool Fire() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutFire != nullptr &&
			runtimeApi->LoadoutFire(gameObjectId_);
	}

	bool Reload() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutReload != nullptr &&
			runtimeApi->LoadoutReload(gameObjectId_);
	}

	bool GetAmmo(int32_t& currentAmmo, int32_t& reserveAmmo) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutGetAmmo != nullptr &&
			runtimeApi->LoadoutGetAmmo(gameObjectId_, &currentAmmo, &reserveAmmo);
	}

	bool GetAmmoAtSlot(int32_t slotIndex, int32_t& currentAmmo, int32_t& reserveAmmo, int32_t& maximumAmmo) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutGetAmmoAtSlot != nullptr &&
			runtimeApi->LoadoutGetAmmoAtSlot(
				gameObjectId_,
				slotIndex,
				&currentAmmo,
				&reserveAmmo,
				&maximumAmmo);
	}

	bool AddMagazineAmmo(int32_t slotIndex, int32_t amount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutAddMagazineAmmo != nullptr &&
			runtimeApi->LoadoutAddMagazineAmmo(gameObjectId_, slotIndex, amount);
	}

	bool AddReserveAmmo(int32_t slotIndex, int32_t amount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutAddReserveAmmo != nullptr &&
			runtimeApi->LoadoutAddReserveAmmo(gameObjectId_, slotIndex, amount);
	}

	bool SetMagazineAmmo(int32_t slotIndex, int32_t amount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSetMagazineAmmo != nullptr &&
			runtimeApi->LoadoutSetMagazineAmmo(gameObjectId_, slotIndex, amount);
	}

	bool SetReserveAmmo(int32_t slotIndex, int32_t amount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSetReserveAmmo != nullptr &&
			runtimeApi->LoadoutSetReserveAmmo(gameObjectId_, slotIndex, amount);
	}

	bool SetMaximumAmmo(int32_t slotIndex, int32_t amount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutSetMaximumAmmo != nullptr &&
			runtimeApi->LoadoutSetMaximumAmmo(gameObjectId_, slotIndex, amount);
	}

	bool RefillMagazine(int32_t slotIndex = -1) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->LoadoutRefillMagazine != nullptr &&
			runtimeApi->LoadoutRefillMagazine(gameObjectId_, slotIndex);
	}

private:
	int32_t gameObjectId_ = -1;
};

class WeaponGroup final {
public:
	explicit WeaponGroup(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Fire() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->FireWeaponGroup != nullptr &&
			runtimeApi->FireWeaponGroup(gameObjectId_);
	}

	bool IsFiring() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->IsWeaponGroupFiring != nullptr &&
			runtimeApi->IsWeaponGroupFiring(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class TurretAim final {
public:
	explicit TurretAim(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetState(EditorScriptTurretAimState& state) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetTurretAimState != nullptr &&
			runtimeApi->GetTurretAimState(gameObjectId_, &state);
	}

private:
	int32_t gameObjectId_ = -1;
};

class FireLineCheck final {
public:
	explicit FireLineCheck(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetState(EditorScriptFireLineState& state) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetFireLineState != nullptr &&
			runtimeApi->GetFireLineState(gameObjectId_, &state);
	}

private:
	int32_t gameObjectId_ = -1;
};

class StatusEffectSet final {
public:
	explicit StatusEffectSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Apply(const char* effectId, const GameObject& sourceGameObject = GameObject{}) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t sourceGameObjectId = sourceGameObject.HasReference()
			? sourceGameObject.GetInstanceId()
			: -1;
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ApplyStatusEffect != nullptr &&
			effectId != nullptr && runtimeApi->ApplyStatusEffect(gameObjectId_, effectId, sourceGameObjectId);
	}

	bool Remove(const char* effectId) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->RemoveStatusEffect != nullptr &&
			effectId != nullptr && runtimeApi->RemoveStatusEffect(gameObjectId_, effectId);
	}

	bool Clear() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ClearStatusEffects != nullptr &&
			runtimeApi->ClearStatusEffects(gameObjectId_);
	}

	bool Has(const char* effectId) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->HasStatusEffect != nullptr &&
			effectId != nullptr && runtimeApi->HasStatusEffect(gameObjectId_, effectId);
	}

	std::vector<EditorScriptStatusEffectEntry> GetEntries() const {
		std::vector<EditorScriptStatusEffectEntry> entries;
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t effectCount = 0;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetStatusEffectCount == nullptr ||
			runtimeApi->GetStatusEffectEntry == nullptr ||
			!runtimeApi->GetStatusEffectCount(gameObjectId_, &effectCount)) {
			return entries;
		}

		entries.reserve(static_cast<size_t>((std::max)(effectCount, 0)));

		for (int32_t effectIndex = 0; effectIndex < effectCount; effectIndex++) {
			EditorScriptStatusEffectEntry entry{};

			if (runtimeApi->GetStatusEffectEntry(gameObjectId_, effectIndex, &entry)) {
				entries.push_back(entry);
			}
		}

		return entries;
	}

private:
	int32_t gameObjectId_ = -1;
};

class Targeting final {
public:
	explicit Targeting(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	GameObject GetCurrentTarget() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t targetGameObjectId = -1;

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->GetCurrentTarget == nullptr ||
			!runtimeApi->GetCurrentTarget(gameObjectId_, &targetGameObjectId)) {
			return GameObject{};
		}

		return GameObject(targetGameObjectId);
	}

	bool SetTarget(const GameObject& targetGameObject) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t targetGameObjectId = targetGameObject.HasReference()
			? targetGameObject.GetInstanceId()
			: -1;
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetExplicitTarget != nullptr &&
			runtimeApi->SetExplicitTarget(gameObjectId_, targetGameObjectId);
	}

	bool GetInterceptPrediction(EditorScriptVector3& position, float& timeSeconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetInterceptPrediction != nullptr &&
			runtimeApi->GetInterceptPrediction(gameObjectId_, &position, &timeSeconds);
	}

private:
	int32_t gameObjectId_ = -1;
};

class RuntimeProperty final {
public:
	static bool SetVector2(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, const EditorScriptVector2& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->SetRuntimeVector2 != nullptr &&
			runtimeApi->SetRuntimeVector2(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool GetVector2(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, EditorScriptVector2& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->GetRuntimeVector2 != nullptr &&
			runtimeApi->GetRuntimeVector2(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool SetFloat(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, float value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->SetRuntimeFloat != nullptr &&
			runtimeApi->SetRuntimeFloat(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), value);
	}

	static bool GetFloat(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, float& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->GetRuntimeFloat != nullptr &&
			runtimeApi->GetRuntimeFloat(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool SetInt(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, int32_t value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->SetRuntimeInt != nullptr &&
			runtimeApi->SetRuntimeInt(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), value);
	}

	static bool GetInt(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, int32_t& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->GetRuntimeInt != nullptr &&
			runtimeApi->GetRuntimeInt(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool SetBool(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, bool value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->SetRuntimeBool != nullptr &&
			runtimeApi->SetRuntimeBool(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), value);
	}

	static bool GetBool(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, bool& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->GetRuntimeBool != nullptr &&
			runtimeApi->GetRuntimeBool(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool SetVector3(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, const EditorScriptVector3& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->SetRuntimeVector3 != nullptr &&
			runtimeApi->SetRuntimeVector3(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}

	static bool GetVector3(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, EditorScriptVector3& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr && runtimeApi->GetRuntimeVector3 != nullptr &&
			runtimeApi->GetRuntimeVector3(gameObject.GetInstanceId(), componentName.c_str(), propertyName.c_str(), &value);
	}
};

class ActionPayload final {
public:
	static EditorScriptActionPayload GameObjectValue(const GameObject& gameObject) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeGameObject;
		payload.gameObjectId = gameObject.GetInstanceId();
		return payload;
	}

	static EditorScriptActionPayload Int(int32_t value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeInt;
		payload.intValue = value;
		return payload;
	}

	static EditorScriptActionPayload Float(float value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeFloat;
		payload.floatValue = value;
		return payload;
	}

	static EditorScriptActionPayload Bool(bool value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeBool;
		payload.boolValue = value;
		return payload;
	}

	static EditorScriptActionPayload Vector3(const EditorScriptVector3& value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeVector3;
		payload.vector3Value = value;
		return payload;
	}

	static EditorScriptActionPayload String(const std::string& value) {
		EditorScriptActionPayload payload{};
		payload.type = EditorScriptActionPayloadTypeString;
		const size_t copyLength = (std::min)(value.size(), sizeof(payload.stringValue) - 1u);
		std::copy_n(value.data(), copyLength, payload.stringValue);
		payload.stringValue[copyLength] = '\0';
		return payload;
	}
};

class Timer final {
public:
	explicit Timer(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Start() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->StartTimer != nullptr &&
			runtimeApi->StartTimer(gameObjectId_);
	}

	bool Pause(bool isPaused = true) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PauseTimer != nullptr &&
			runtimeApi->PauseTimer(gameObjectId_, isPaused);
	}

	bool Resume() const {
		return Pause(false);
	}

	bool GetRemaining(float& seconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetTimerRemaining != nullptr &&
			runtimeApi->GetTimerRemaining(gameObjectId_, &seconds);
	}

private:
	int32_t gameObjectId_ = -1;
};

class GenericStateMachine final {
public:
	explicit GenericStateMachine(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool ChangeState(const std::string& stateName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !stateName.empty() && runtimeApi != nullptr &&
			runtimeApi->ChangeGenericState != nullptr &&
			runtimeApi->ChangeGenericState(gameObjectId_, stateName.c_str());
	}

	bool GetState(std::string& stateName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		char stateNameBuffer[256]{};

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->GetGenericState == nullptr ||
			!runtimeApi->GetGenericState(
				gameObjectId_,
				stateNameBuffer,
				static_cast<int32_t>(sizeof(stateNameBuffer)))) {
			return false;
		}

		stateName = stateNameBuffer;
		return true;
	}

private:
	int32_t gameObjectId_ = -1;
};

class Attribute final {
public:
	explicit Attribute(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Set(float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetAttributeValue != nullptr &&
			runtimeApi->SetAttributeValue(gameObjectId_, value);
	}

	bool Get(float& current, float& maximum) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetAttributeValue != nullptr &&
			runtimeApi->GetAttributeValue(gameObjectId_, &current, &maximum);
	}

private:
	int32_t gameObjectId_ = -1;
};

class TargetLock final {
public:
	explicit TargetLock(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetState(float& progress, bool& isLocked, GameObject& target) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t targetGameObjectId = -1;

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->GetTargetLockState == nullptr ||
			!runtimeApi->GetTargetLockState(
				gameObjectId_,
				&progress,
				&isLocked,
				&targetGameObjectId)) {
			return false;
		}

		target = GameObject{targetGameObjectId};
		return true;
	}

private:
	int32_t gameObjectId_ = -1;
};

class MultiTargetLock final {
public:
	struct Entry {
		GameObject target;
		float progress = 0.0f;
		bool isLocked = false;
	};

	explicit MultiTargetLock(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	std::vector<Entry> GetEntries() const {
		std::vector<Entry> entries;
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t targetCount = 0;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetMultiTargetLockCount == nullptr ||
			runtimeApi->GetMultiTargetLockTarget == nullptr ||
			!runtimeApi->GetMultiTargetLockCount(gameObjectId_, &targetCount)) {
			return entries;
		}

		entries.reserve(static_cast<size_t>((std::max)(targetCount, 0)));

		for (int32_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
			int32_t targetGameObjectId = -1;
			float progress = 0.0f;
			bool isLocked = false;

			if (runtimeApi->GetMultiTargetLockTarget(
					gameObjectId_, targetIndex, &targetGameObjectId, &progress, &isLocked)) {
				entries.push_back(Entry{GameObject{targetGameObjectId}, progress, isLocked});
			}
		}

		return entries;
	}

private:
	int32_t gameObjectId_ = -1;
};

class AttributeSet final {
public:
	explicit AttributeSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Set(const std::string& name, float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !name.empty() && runtimeApi != nullptr &&
			runtimeApi->SetNamedAttributeValue != nullptr &&
			runtimeApi->SetNamedAttributeValue(gameObjectId_, name.c_str(), value);
	}

	bool Get(const std::string& name, float& current, float& maximum) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !name.empty() && runtimeApi != nullptr &&
			runtimeApi->GetNamedAttributeValue != nullptr &&
			runtimeApi->GetNamedAttributeValue(gameObjectId_, name.c_str(), &current, &maximum);
	}

	bool Add(const std::string& name, float deltaValue) const {
		float current = 0.0f;
		float maximum = 0.0f;
		return Get(name, current, maximum) && Set(name, current + deltaValue);
	}

private:
	int32_t gameObjectId_ = -1;
};

class GenericCounter final {
public:
	explicit GenericCounter(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Set(float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetCounterValue != nullptr &&
			runtimeApi->SetCounterValue(gameObjectId_, value);
	}

	bool Add(float deltaValue = 1.0f) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AddCounterValue != nullptr &&
			runtimeApi->AddCounterValue(gameObjectId_, deltaValue);
	}

	bool Get(float& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->GetCounterValue != nullptr &&
			runtimeApi->GetCounterValue(gameObjectId_, &value);
	}

private:
	int32_t gameObjectId_ = -1;
};

class GenericCondition final {
public:
	explicit GenericCondition(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Evaluate(bool& result) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->EvaluateGenericCondition != nullptr &&
			runtimeApi->EvaluateGenericCondition(gameObjectId_, &result);
	}

private:
	int32_t gameObjectId_ = -1;
};

class GameplayData final {
public:
	explicit GameplayData(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetString(const std::string& key, std::string& value) const {
		int32_t valueType = 0;
		return GetRaw(key, valueType, value);
	}

	bool GetInt(const std::string& key, int32_t& value) const {
		int32_t valueType = 0;
		std::string text;

		if (!GetRaw(key, valueType, text) || valueType != 1) {
			return false;
		}

		char* parseEnd = nullptr;
		const long parsedValue = std::strtol(text.c_str(), &parseEnd, 10);

		if (parseEnd == text.c_str() || *parseEnd != '\0') {
			return false;
		}

		value = static_cast<int32_t>(parsedValue);
		return true;
	}

	bool GetFloat(const std::string& key, float& value) const {
		int32_t valueType = 0;
		std::string text;

		if (!GetRaw(key, valueType, text) || valueType != 2) {
			return false;
		}

		char* parseEnd = nullptr;
		const float parsedValue = std::strtof(text.c_str(), &parseEnd);

		if (parseEnd == text.c_str() || *parseEnd != '\0') {
			return false;
		}

		value = parsedValue;
		return true;
	}

	bool GetBool(const std::string& key, bool& value) const {
		int32_t valueType = 0;
		std::string text;

		if (!GetRaw(key, valueType, text) || valueType != 3 ||
			(text != "true" && text != "false" && text != "1" && text != "0")) {
			return false;
		}

		value = text == "true" || text == "1";
		return true;
	}

private:
	bool GetRaw(const std::string& key, int32_t& valueType, std::string& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		char valueBuffer[512]{};

		if (gameObjectId_ < 0 || key.empty() || runtimeApi == nullptr ||
			runtimeApi->GetGameplayDataValue == nullptr ||
			!runtimeApi->GetGameplayDataValue(
				gameObjectId_, key.c_str(), &valueType, valueBuffer, static_cast<int32_t>(sizeof(valueBuffer)))) {
			return false;
		}

		value = valueBuffer;
		return true;
	}

	int32_t gameObjectId_ = -1;
};

class DamageTag final {
public:
	static int32_t Id(const std::string& damageTag) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !damageTag.empty() && runtimeApi != nullptr && runtimeApi->HashDamageTag != nullptr
			? runtimeApi->HashDamageTag(damageTag.c_str())
			: 0;
	}
};

class AreaDamage final {
public:
	explicit AreaDamage(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	int32_t Apply(const GameObject& instigator = GameObject{}) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t instigatorGameObjectId = instigator.HasReference()
			? instigator.GetInstanceId()
			: -1;

		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ApplyAreaDamage != nullptr
			? runtimeApi->ApplyAreaDamage(gameObjectId_, instigatorGameObjectId)
			: 0;
	}

private:
	int32_t gameObjectId_ = -1;
};

class ProjectileDetonator final {
public:
	explicit ProjectileDetonator(const GameObject& projectileGameObject)
		: gameObjectId_(projectileGameObject.GetInstanceId()) {
	}

	bool Detonate() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->DetonateProjectile != nullptr &&
			runtimeApi->DetonateProjectile(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class ThreatTracker final {
public:
	struct Entry {
		GameObject projectile;
		GameObject source;
		float distance = 0.0f;
		float closingSpeed = 0.0f;
		float estimatedArrivalSeconds = 0.0f;
	};

	explicit ThreatTracker(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	std::vector<Entry> GetEntries() const {
		std::vector<Entry> entries;
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t threatCount = 0;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetThreatTrackerCount == nullptr ||
			runtimeApi->GetThreatTrackerEntry == nullptr ||
			!runtimeApi->GetThreatTrackerCount(gameObjectId_, &threatCount)) {
			return entries;
		}

		entries.reserve(static_cast<size_t>((std::max)(threatCount, 0)));

		for (int32_t threatIndex = 0; threatIndex < threatCount; ++threatIndex) {
			EditorScriptThreatInfo threatInfo{};

			if (!runtimeApi->GetThreatTrackerEntry(gameObjectId_, threatIndex, &threatInfo)) {
				continue;
			}

			entries.push_back(Entry{
				GameObject{threatInfo.projectileGameObjectId},
				GameObject{threatInfo.sourceGameObjectId},
				threatInfo.distance,
				threatInfo.closingSpeed,
				threatInfo.estimatedArrivalSeconds});
		}

		return entries;
	}

private:
	int32_t gameObjectId_ = -1;
};

class CooldownSet final {
public:
	explicit CooldownSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Start(const std::string& cooldownName, float durationOverride = -1.0f) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !cooldownName.empty() && runtimeApi != nullptr &&
			runtimeApi->StartNamedCooldown != nullptr &&
			runtimeApi->StartNamedCooldown(gameObjectId_, cooldownName.c_str(), durationOverride);
	}

	bool Reset(const std::string& cooldownName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !cooldownName.empty() && runtimeApi != nullptr &&
			runtimeApi->ResetNamedCooldown != nullptr &&
			runtimeApi->ResetNamedCooldown(gameObjectId_, cooldownName.c_str());
	}

	bool Get(const std::string& cooldownName, float& remainingSeconds, bool& isReady) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !cooldownName.empty() && runtimeApi != nullptr &&
			runtimeApi->GetNamedCooldown != nullptr &&
			runtimeApi->GetNamedCooldown(
				gameObjectId_, cooldownName.c_str(), &remainingSeconds, &isReady);
	}

	bool IsReady(const std::string& cooldownName) const {
		float remainingSeconds = 0.0f;
		bool isReady = false;
		return Get(cooldownName, remainingSeconds, isReady) && isReady;
	}

private:
	int32_t gameObjectId_ = -1;
};

class RuntimeStateReset final {
public:
	explicit RuntimeStateReset(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Reset() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ResetRuntimeState != nullptr &&
			runtimeApi->ResetRuntimeState(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class TimeScale final {
public:
	explicit TimeScale(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Play(float scaleOverride = -1.0f, float durationOverride = -1.0f) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayTimeScale != nullptr &&
			runtimeApi->PlayTimeScale(gameObjectId_, scaleOverride, durationOverride);
	}

	bool HitStop(float durationSeconds) const {
		return Play(0.0f, durationSeconds);
	}

	static float GetCurrent() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->GetTimeScale != nullptr
			? runtimeApi->GetTimeScale()
			: 1.0f;
	}

private:
	int32_t gameObjectId_ = -1;
};

enum class ObjectiveState : int32_t {
	Inactive = 0,
	Active = 1,
	Completed = 2,
	Failed = 3,
};

class ObjectiveTracker final {
public:
	explicit ObjectiveTracker(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Set(
		const std::string& objectiveId,
		ObjectiveState state,
		float currentValue) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !objectiveId.empty() && runtimeApi != nullptr &&
			runtimeApi->SetObjective != nullptr &&
			runtimeApi->SetObjective(
				gameObjectId_,
				objectiveId.c_str(),
				static_cast<int32_t>(state),
				currentValue);
	}

	bool Get(
		const std::string& objectiveId,
		ObjectiveState& state,
		float& currentValue,
		float& targetValue) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t stateValue = 0;

		if (gameObjectId_ < 0 || objectiveId.empty() || runtimeApi == nullptr ||
			runtimeApi->GetObjective == nullptr ||
			!runtimeApi->GetObjective(
				gameObjectId_,
				objectiveId.c_str(),
				&stateValue,
				&currentValue,
				&targetValue)) {
			return false;
		}

		state = static_cast<ObjectiveState>(stateValue);
		return true;
	}

private:
	int32_t gameObjectId_ = -1;
};

class EncounterController final {
public:
	explicit EncounterController(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Start() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->StartEncounter != nullptr &&
			runtimeApi->StartEncounter(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class SpawnPointSet final {
public:
	explicit SpawnPointSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Resolve(EditorScriptVector3& position, EditorScriptVector3& rotation) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ResolveSpawnPoint != nullptr &&
			runtimeApi->ResolveSpawnPoint(gameObjectId_, &position, &rotation);
	}

private:
	int32_t gameObjectId_ = -1;
};

class DifficultyParameterSet final {
public:
	explicit DifficultyParameterSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Apply(int32_t difficultyIndex) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ApplyDifficulty != nullptr &&
			runtimeApi->ApplyDifficulty(gameObjectId_, difficultyIndex);
	}

private:
	int32_t gameObjectId_ = -1;
};

class DamageDirectionIndicator final {
public:
	explicit DamageDirectionIndicator(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Get(
		EditorScriptVector2& direction,
		float& alpha,
		GameObject& sourceGameObject) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t sourceGameObjectId = -1;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetDamageDirection == nullptr ||
			!runtimeApi->GetDamageDirection(
				gameObjectId_,
				&direction,
				&alpha,
				&sourceGameObjectId)) {
			return false;
		}

		sourceGameObject = GameObject(sourceGameObjectId);
		return true;
	}

private:
	int32_t gameObjectId_ = -1;
};

class BallisticPrediction final {
public:
	explicit BallisticPrediction(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Get(EditorScriptBallisticPrediction& prediction) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetBallisticPrediction != nullptr &&
			runtimeApi->GetBallisticPrediction(gameObjectId_, &prediction);
	}

	std::vector<EditorScriptVector3> GetTrajectoryPoints() const {
		std::vector<EditorScriptVector3> points;
		EditorScriptBallisticPrediction prediction{};

		if (!Get(prediction) || prediction.trajectoryPointCount <= 0) {
			return points;
		}

		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		points.reserve(static_cast<size_t>(prediction.trajectoryPointCount));

		for (int32_t pointIndex = 0; pointIndex < prediction.trajectoryPointCount; pointIndex++) {
			EditorScriptVector3 point{};

			if (runtimeApi->GetBallisticTrajectoryPoint != nullptr &&
				runtimeApi->GetBallisticTrajectoryPoint(gameObjectId_, pointIndex, &point)) {
				points.push_back(point);
			}
		}

		return points;
	}

private:
	int32_t gameObjectId_ = -1;
};

class DamageEventBuffer final {
public:
	explicit DamageEventBuffer(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	std::vector<EditorScriptDamageEvent> GetEntries() const {
		std::vector<EditorScriptDamageEvent> entries;
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		int32_t eventCount = 0;

		if (gameObjectId_ < 0 || runtimeApi == nullptr ||
			runtimeApi->GetDamageEventBufferCount == nullptr ||
			runtimeApi->GetDamageEventBufferEntry == nullptr ||
			!runtimeApi->GetDamageEventBufferCount(gameObjectId_, &eventCount)) {
			return entries;
		}

		entries.reserve(static_cast<size_t>((std::max)(eventCount, 0)));

		for (int32_t eventIndex = 0; eventIndex < eventCount; eventIndex++) {
			EditorScriptDamageEvent damageEvent{};

			if (runtimeApi->GetDamageEventBufferEntry(gameObjectId_, eventIndex, &damageEvent)) {
				entries.push_back(damageEvent);
			}
		}

		return entries;
	}

private:
	int32_t gameObjectId_ = -1;
};

class GamePause final {
public:
	explicit GamePause(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Pause() const {
		return Set(true);
	}

	bool Resume() const {
		return Set(false);
	}

	bool Set(bool isPaused) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetGamePaused != nullptr &&
			runtimeApi->SetGamePaused(gameObjectId_, isPaused);
	}

	static bool IsPaused() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->IsGamePaused != nullptr &&
			runtimeApi->IsGamePaused();
	}

private:
	int32_t gameObjectId_ = -1;
};

class SurfaceWakeEmitter final {
public:
	explicit SurfaceWakeEmitter(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetState(float& speed, float& intensity) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->GetSurfaceWakeState != nullptr &&
			runtimeApi->GetSurfaceWakeState(gameObjectId_, &speed, &intensity);
	}

private:
	int32_t gameObjectId_ = -1;
};

class WaterSurfaceState final {
public:
	explicit WaterSurfaceState(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Get(EditorScriptWaterSurfaceState& state) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->GetWaterSurfaceState != nullptr &&
			runtimeApi->GetWaterSurfaceState(gameObjectId_, &state);
	}

	bool GetFoam(float& foam) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->GetWaterSurfaceFoam != nullptr &&
			runtimeApi->GetWaterSurfaceFoam(gameObjectId_, &foam);
	}

private:
	int32_t gameObjectId_ = -1;
};

class OceanProbeSet final {
public:
	explicit OceanProbeSet(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool GetSample(int32_t probeIndex, EditorScriptOceanProbeSample& sample) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->GetOceanProbeSample != nullptr &&
			runtimeApi->GetOceanProbeSample(gameObjectId_, probeIndex, &sample);
	}

	bool GetFoam(int32_t probeIndex, float& foam) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->apiVersion >= 7U &&
			runtimeApi->GetOceanProbeFoam != nullptr &&
			runtimeApi->GetOceanProbeFoam(gameObjectId_, probeIndex, &foam);
	}

private:
	int32_t gameObjectId_ = -1;
};

class PropertyTween final {
public:
	explicit PropertyTween(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Play() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayPropertyTween != nullptr &&
			runtimeApi->PlayPropertyTween(gameObjectId_);
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->StopPropertyTween != nullptr &&
			runtimeApi->StopPropertyTween(gameObjectId_);
	}

	bool IsPlaying() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->IsPropertyTweenPlaying != nullptr &&
			runtimeApi->IsPropertyTweenPlaying(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class ActionRelay final {
public:
	explicit ActionRelay(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Relay() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->RelayAction != nullptr &&
			runtimeApi->RelayAction(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class CameraEffects final {
public:
	explicit CameraEffects(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool PlayBlend() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayCameraBlend != nullptr &&
			runtimeApi->PlayCameraBlend(gameObjectId_);
	}

	bool PlayShake() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayCameraShake != nullptr &&
			runtimeApi->PlayCameraShake(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class RailBranch final {
public:
	explicit RailBranch(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Trigger() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->TriggerRailBranch != nullptr &&
			runtimeApi->TriggerRailBranch(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class ActionSequence final {
public:
	explicit ActionSequence(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Play() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayActionSequence != nullptr &&
			runtimeApi->PlayActionSequence(gameObjectId_);
	}

	bool Pause(bool isPaused = true) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PauseActionSequence != nullptr &&
			runtimeApi->PauseActionSequence(gameObjectId_, isPaused);
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->StopActionSequence != nullptr &&
			runtimeApi->StopActionSequence(gameObjectId_);
	}

	bool Signal(const std::string& signalName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && !signalName.empty() && runtimeApi != nullptr &&
			runtimeApi->SignalActionSequence != nullptr &&
			runtimeApi->SignalActionSequence(gameObjectId_, signalName.c_str());
	}

	bool IsPlaying() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->IsActionSequencePlaying != nullptr &&
			runtimeApi->IsActionSequencePlaying(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

class SaveSystem final {
public:
	static bool Save(const std::string& slotName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !slotName.empty() && runtimeApi != nullptr && runtimeApi->SaveSlot != nullptr &&
			runtimeApi->SaveSlot(slotName.c_str());
	}

	static bool Load(const std::string& slotName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !slotName.empty() && runtimeApi != nullptr && runtimeApi->LoadSlot != nullptr &&
			runtimeApi->LoadSlot(slotName.c_str());
	}

	static bool Delete(const std::string& slotName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !slotName.empty() && runtimeApi != nullptr && runtimeApi->DeleteSlot != nullptr &&
			runtimeApi->DeleteSlot(slotName.c_str());
	}

	static bool Exists(const std::string& slotName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !slotName.empty() && runtimeApi != nullptr && runtimeApi->HasSlot != nullptr &&
			runtimeApi->HasSlot(slotName.c_str());
	}

	static void SetFloat(const std::string& key, float value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (!key.empty() && runtimeApi != nullptr && runtimeApi->SetSaveFloat != nullptr) {
			runtimeApi->SetSaveFloat(key.c_str(), value);
		}
	}

	static bool GetFloat(const std::string& key, float& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return !key.empty() && runtimeApi != nullptr && runtimeApi->GetSaveFloat != nullptr &&
			runtimeApi->GetSaveFloat(key.c_str(), &value);
	}

	static void SetString(const std::string& key, const std::string& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (!key.empty() && runtimeApi != nullptr && runtimeApi->SetSaveString != nullptr) {
			runtimeApi->SetSaveString(key.c_str(), value.c_str());
		}

	}

	static bool GetString(const std::string& key, std::string& value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		char textBuffer[512]{};
		if (key.empty() || runtimeApi == nullptr || runtimeApi->GetSaveString == nullptr ||
			!runtimeApi->GetSaveString(key.c_str(), textBuffer, static_cast<int32_t>(sizeof(textBuffer)))) {
			return false;
		}

		value = textBuffer;
		return true;
	}
};

class Checkpoint final {
public:
	explicit Checkpoint(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Save() const {
		return Activate(false);
	}

	bool Load() const {
		return Activate(true);
	}

private:
	int32_t gameObjectId_ = -1;

	bool Activate(bool shouldLoad) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->ActivateCheckpoint != nullptr &&
			runtimeApi->ActivateCheckpoint(gameObjectId_, shouldLoad);
	}
};

class Rigidbody final {
public:
	explicit Rigidbody(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit Rigidbody(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	EditorScriptVector3 GetVelocity() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->GetVelocity == nullptr) {
			return EditorScriptVector3{};
		}

		return runtimeApi->GetVelocity(gameObjectId_);
	}

	bool SetVelocity(const EditorScriptVector3& velocity) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SetVelocity == nullptr) {
			return false;
		}

		runtimeApi->SetVelocity(gameObjectId_, &velocity);
		return true;
	}

	bool AddForce(const EditorScriptVector3& force) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AddForce != nullptr &&
			runtimeApi->AddForce(gameObjectId_, &force);
	}

	bool AddForceAtPosition(
		const EditorScriptVector3& force,
		const EditorScriptVector3& worldPosition) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AddForceAtPosition != nullptr &&
			runtimeApi->AddForceAtPosition(gameObjectId_, &force, &worldPosition);
	}

	bool AddImpulse(const EditorScriptVector3& impulse) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AddImpulse != nullptr &&
			runtimeApi->AddImpulse(gameObjectId_, &impulse);
	}

	bool AddTorque(const EditorScriptVector3& torque) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AddTorque != nullptr &&
			runtimeApi->AddTorque(gameObjectId_, &torque);
	}

private:
	int32_t gameObjectId_ = -1;
};

class RopeConstraint final {
public:
	explicit RopeConstraint(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit RopeConstraint(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	bool Attach(
		const GameObject& targetGameObject,
		const EditorScriptVector3& ownerLocalAnchor,
		const EditorScriptVector3& targetLocalAnchor,
		float maximumLength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && targetGameObject.HasReference() && runtimeApi != nullptr &&
			runtimeApi->AttachRope != nullptr &&
			runtimeApi->AttachRope(
				gameObjectId_,
				targetGameObject.GetInstanceId(),
				&ownerLocalAnchor,
				&targetLocalAnchor,
				maximumLength);
	}

	bool AttachToWorld(
		const EditorScriptVector3& ownerLocalAnchor,
		const EditorScriptVector3& worldAnchor,
		float maximumLength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AttachRope != nullptr &&
			runtimeApi->AttachRope(
				gameObjectId_,
				-1,
				&ownerLocalAnchor,
				&worldAnchor,
				maximumLength);
	}

	bool Detach() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->DetachRope != nullptr &&
			runtimeApi->DetachRope(gameObjectId_);
	}

	bool SetLength(float maximumLength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRopeLength != nullptr &&
			runtimeApi->SetRopeLength(gameObjectId_, maximumLength);
	}

	bool Repair() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->RepairRope != nullptr &&
			runtimeApi->RepairRope(gameObjectId_);
	}

	EditorScriptRopeState GetState() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->GetRopeState == nullptr) {
			return EditorScriptRopeState{};
		}

		return runtimeApi->GetRopeState(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

//================================================================
// ユーザー C++ Script の共通基底クラス
//================================================================

class EditorNativeScript {
public:
	virtual ~EditorNativeScript() = default;

	//============================================================
	// GameObject ライフサイクル
	//============================================================

	virtual void Start(int32_t gameObjectId) {
		(void)gameObjectId;
	}

	virtual void Update(int32_t gameObjectId, float deltaTime) {
		(void)gameObjectId;
		(void)deltaTime;
	}

	virtual void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
		(void)gameObjectId;
		(void)fixedDeltaTime;
	}

	virtual void Stop(int32_t gameObjectId) {
		(void)gameObjectId;
	}

	//============================================================
	// 物理イベント
	//============================================================

	virtual void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	virtual void OnCollisionStay(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	virtual void OnCollisionExit(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	virtual void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	virtual void OnTriggerStay(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	virtual void OnTriggerExit(const EditorScriptPhysicsEvent& physicsEvent) {
		(void)physicsEvent;
	}

	void DispatchPhysicsEvent(const EditorScriptPhysicsEvent& physicsEvent) {
		switch (physicsEvent.type) {
		case EditorScriptPhysicsEventTypeCollisionEnter:
			OnCollisionEnter(physicsEvent);
			break;
		case EditorScriptPhysicsEventTypeCollisionStay:
			OnCollisionStay(physicsEvent);
			break;
		case EditorScriptPhysicsEventTypeCollisionExit:
			OnCollisionExit(physicsEvent);
			break;
		case EditorScriptPhysicsEventTypeTriggerEnter:
			OnTriggerEnter(physicsEvent);
			break;
		case EditorScriptPhysicsEventTypeTriggerStay:
			OnTriggerStay(physicsEvent);
			break;
		case EditorScriptPhysicsEventTypeTriggerExit:
			OnTriggerExit(physicsEvent);
			break;
		default:
			break;
		}
	}

	//============================================================
	// アニメーションイベント
	//============================================================

	virtual void OnAnimationEvent(const EditorScriptAnimationEvent& animationEvent) {
		(void)animationEvent;  // 必要な Script だけ override し、Clip の任意 Event を受け取る。
	}

	//============================================================
	// Inspector 公開変数
	//============================================================

	int32_t GetFieldCount() const {
		return static_cast<int32_t>(fieldBindings_.size());
	}

	bool GetFieldDescriptor(int32_t fieldIndex, EditorScriptFieldDescriptor& fieldDescriptor) const {
		if (fieldIndex < 0 || fieldIndex >= static_cast<int32_t>(fieldBindings_.size())) {
			return false;
		}

		fieldDescriptor = fieldBindings_[static_cast<size_t>(fieldIndex)].descriptor;
		return true;
	}

	bool GetFieldValue(const char* fieldName, EditorScriptFieldValue& fieldValue) const {
		const FieldBinding* fieldBinding = FindFieldBinding(fieldName);
		if (fieldBinding == nullptr || fieldBinding->readFunction == nullptr) {
			return false;
		}

		fieldValue = {};
		fieldValue.type = fieldBinding->descriptor.defaultValue.type;
		fieldBinding->readFunction(fieldValue);
		return true;
	}

	bool SetFieldValue(const char* fieldName, const EditorScriptFieldValue& fieldValue) {
		FieldBinding* fieldBinding = FindFieldBinding(fieldName);
		if (fieldBinding == nullptr || fieldBinding->writeFunction == nullptr) {
			return false;
		}

		if (fieldValue.type != fieldBinding->descriptor.defaultValue.type) {
			return false;
		}

		fieldBinding->writeFunction(fieldValue);
		return true;
	}

	bool InvokeAction(const char* functionName, const EditorScriptInputActionContext& inputContext) {
		if (functionName == nullptr || functionName[0] == '\0') {
			return false;
		}

		const auto actionIterator = actionFunctions_.find(functionName);
		if (actionIterator == actionFunctions_.end()) {
			return false;
		}

		actionIterator->second(inputContext);
		return true;
	}

	int32_t GetActionCount() const {
		return static_cast<int32_t>(actionNames_.size());
	}

	bool GetActionName(int32_t actionIndex, char* actionName, int32_t actionNameCapacity) const {
		if (actionIndex < 0 ||
			actionIndex >= static_cast<int32_t>(actionNames_.size()) ||
			actionName == nullptr ||
			actionNameCapacity <= 0) {
			return false;
		}

		CopyText(
			actionNames_[static_cast<size_t>(actionIndex)].c_str(),
			actionName,
			static_cast<size_t>(actionNameCapacity));
		return true;
	}

protected:
	using ActionFunction = std::function<void(const EditorScriptInputActionContext&)>;

	void ExposeBool(const char* name, const char* displayName, bool& value) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeBool;
		defaultValue.boolValue = value;
		AddField(
			name,
			displayName,
			defaultValue,
			false,
			0.0f,
			0.0f,
			1.0f,
			[&value](EditorScriptFieldValue& fieldValue) { fieldValue.boolValue = value; },
			[&value](const EditorScriptFieldValue& fieldValue) { value = fieldValue.boolValue; });
	}

	void ExposeInt32(const char* name, const char* displayName, int32_t& value, int32_t minValue, int32_t maxValue, int32_t step) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeInt32;
		defaultValue.intValue = value;
		AddField(
			name,
			displayName,
			defaultValue,
			true,
			static_cast<float>(minValue),
			static_cast<float>(maxValue),
			static_cast<float>((std::max)(step, 1)),
			[&value](EditorScriptFieldValue& fieldValue) { fieldValue.intValue = value; },
			[&value, minValue, maxValue](const EditorScriptFieldValue& fieldValue) {
				value = (std::clamp)(fieldValue.intValue, minValue, maxValue);
			});
	}

	void ExposeFloat(const char* name, const char* displayName, float& value, float minValue, float maxValue, float step) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeFloat;
		defaultValue.floatValue = value;
		AddField(
			name,
			displayName,
			defaultValue,
			true,
			minValue,
			maxValue,
			(std::max)(step, 0.001f),
			[&value](EditorScriptFieldValue& fieldValue) { fieldValue.floatValue = value; },
			[&value, minValue, maxValue](const EditorScriptFieldValue& fieldValue) {
				value = (std::clamp)(fieldValue.floatValue, minValue, maxValue);
			});
	}

	void ExposeVector2(const char* name, const char* displayName, EditorScriptVector2& value, float minValue, float maxValue, float step) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeVector2;
		defaultValue.vector2Value = value;
		AddField(
			name,
			displayName,
			defaultValue,
			true,
			minValue,
			maxValue,
			(std::max)(step, 0.001f),
			[&value](EditorScriptFieldValue& fieldValue) { fieldValue.vector2Value = value; },
			[&value, minValue, maxValue](const EditorScriptFieldValue& fieldValue) {
				value.x = (std::clamp)(fieldValue.vector2Value.x, minValue, maxValue);
				value.y = (std::clamp)(fieldValue.vector2Value.y, minValue, maxValue);
			});
	}

	void ExposeVector3(const char* name, const char* displayName, EditorScriptVector3& value, float minValue, float maxValue, float step) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeVector3;
		defaultValue.vector3Value = value;
		AddField(
			name,
			displayName,
			defaultValue,
			true,
			minValue,
			maxValue,
			(std::max)(step, 0.001f),
			[&value](EditorScriptFieldValue& fieldValue) { fieldValue.vector3Value = value; },
			[&value, minValue, maxValue](const EditorScriptFieldValue& fieldValue) {
				value.x = (std::clamp)(fieldValue.vector3Value.x, minValue, maxValue);
				value.y = (std::clamp)(fieldValue.vector3Value.y, minValue, maxValue);
				value.z = (std::clamp)(fieldValue.vector3Value.z, minValue, maxValue);
			});
	}

	void ExposeString(const char* name, const char* displayName, std::string& value) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeString;
		CopyText(value.c_str(), defaultValue.stringValue, sizeof(defaultValue.stringValue));
		AddField(
			name,
			displayName,
			defaultValue,
			false,
			0.0f,
			0.0f,
			1.0f,
			[&value](EditorScriptFieldValue& fieldValue) {
				CopyText(value.c_str(), fieldValue.stringValue, sizeof(fieldValue.stringValue));
			},
			[&value](const EditorScriptFieldValue& fieldValue) { value = fieldValue.stringValue; });
	}

	void ExposeGameObject(const char* name, const char* displayName, int32_t& gameObjectId) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeGameObject;
		defaultValue.intValue = gameObjectId;
		AddField(
			name,
			displayName,
			defaultValue,
			false,
			0.0f,
			0.0f,
			1.0f,
			[&gameObjectId](EditorScriptFieldValue& fieldValue) { fieldValue.intValue = gameObjectId; },
			[&gameObjectId](const EditorScriptFieldValue& fieldValue) { gameObjectId = fieldValue.intValue; });
	}

	void ExposeGameObject(const char* name, const char* displayName, GameObject& gameObject) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeGameObject;
		defaultValue.intValue = gameObject.GetInstanceId();
		AddField(
			name,
			displayName,
			defaultValue,
			false,
			0.0f,
			0.0f,
			1.0f,
			[&gameObject](EditorScriptFieldValue& fieldValue) {
				fieldValue.intValue = gameObject.GetInstanceId();
			},
			[&gameObject](const EditorScriptFieldValue& fieldValue) {
				gameObject = GameObject{fieldValue.intValue};
			});
	}

	void ExposeScene(const char* name, const char* displayName, std::string& scenePath) {
		EditorScriptFieldValue defaultValue{};
		defaultValue.type = EditorScriptFieldTypeSceneAsset;
		CopyText(scenePath.c_str(), defaultValue.stringValue, sizeof(defaultValue.stringValue));
		AddField(
			name,
			displayName,
			defaultValue,
			false,
			0.0f,
			0.0f,
			1.0f,
			[&scenePath](EditorScriptFieldValue& fieldValue) {
				CopyText(scenePath.c_str(), fieldValue.stringValue, sizeof(fieldValue.stringValue));
			},
			[&scenePath](const EditorScriptFieldValue& fieldValue) { scenePath = fieldValue.stringValue; });
	}

	void BindAction(const char* functionName, ActionFunction actionFunction) {
		if (functionName == nullptr || functionName[0] == '\0' || !actionFunction) {
			return;
		}

		if (actionFunctions_.find(functionName) == actionFunctions_.end()) {
			actionNames_.push_back(functionName);
		}

		actionFunctions_[functionName] = std::move(actionFunction);
	}

private:
	struct FieldBinding {
		EditorScriptFieldDescriptor descriptor{};
		std::function<void(EditorScriptFieldValue&)> readFunction;
		std::function<void(const EditorScriptFieldValue&)> writeFunction;
	};

	std::vector<FieldBinding> fieldBindings_;  // 登録順を Inspector の表示順として保持する。
	std::unordered_map<std::string, ActionFunction> actionFunctions_;  // Inspector の関数名から C++ メソッドへ振り分ける。
	std::vector<std::string> actionNames_;  // Script が公開した Action を Inspector の候補へ登録順で渡す。

	static void CopyText(const char* sourceText, char* destinationText, size_t destinationSize) {
		if (destinationText == nullptr || destinationSize == 0U) {
			return;
		}

		destinationText[0] = '\0';
		if (sourceText == nullptr) {
			return;
		}

		size_t characterIndex = 0U;
		while (sourceText[characterIndex] != '\0' && characterIndex + 1U < destinationSize) {
			destinationText[characterIndex] = sourceText[characterIndex];
			characterIndex++;
		}

		destinationText[characterIndex] = '\0';
	}

	void AddField(
		const char* name,
		const char* displayName,
		const EditorScriptFieldValue& defaultValue,
		bool hasRange,
		float minValue,
		float maxValue,
		float step,
		std::function<void(EditorScriptFieldValue&)> readFunction,
		std::function<void(const EditorScriptFieldValue&)> writeFunction) {
		if (name == nullptr || name[0] == '\0') {
			return;
		}

		FieldBinding fieldBinding{};
		CopyText(name, fieldBinding.descriptor.name, sizeof(fieldBinding.descriptor.name));
		CopyText(
			displayName == nullptr || displayName[0] == '\0' ? name : displayName,
			fieldBinding.descriptor.displayName,
			sizeof(fieldBinding.descriptor.displayName));
		fieldBinding.descriptor.defaultValue = defaultValue;
		fieldBinding.descriptor.minValue = minValue;
		fieldBinding.descriptor.maxValue = maxValue;
		fieldBinding.descriptor.step = step;
		fieldBinding.descriptor.hasRange = hasRange;
		fieldBinding.readFunction = std::move(readFunction);
		fieldBinding.writeFunction = std::move(writeFunction);
		fieldBindings_.push_back(std::move(fieldBinding));
	}

	FieldBinding* FindFieldBinding(const char* fieldName) {
		if (fieldName == nullptr) {
			return nullptr;
		}

		for (FieldBinding& fieldBinding : fieldBindings_) {
			if (std::string(fieldName) == fieldBinding.descriptor.name) {
				return &fieldBinding;
			}
		}

		return nullptr;
	}

	const FieldBinding* FindFieldBinding(const char* fieldName) const {
		if (fieldName == nullptr) {
			return nullptr;
		}

		for (const FieldBinding& fieldBinding : fieldBindings_) {
			if (std::string(fieldName) == fieldBinding.descriptor.name) {
				return &fieldBinding;
			}
		}

		return nullptr;
	}
};
