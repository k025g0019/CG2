#pragma once

#include "EditorScriptApi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
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

enum class MouseButton : int32_t {
	Left = 0,
	Right = 1,
	Middle = 2,
};

// NVIDIA Blast 1.1.5で作成されたDestructiblePartをScriptから破断する高水準API。
// gameObjectIdにはBlastを有効にしたDestructiblePart所有Objectを渡す。
class BlastDestruction final {
public:
	static bool ApplyDamage(
		int32_t gameObjectId,
		const EditorScriptVector3& worldPosition,
		float radius,
		float damage,
		float impulse = 0.0f) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastApplyDamage != nullptr &&
			runtimeApi->BlastApplyDamage(gameObjectId, &worldPosition, radius, damage, impulse);
	}

	// Inspectorの「既定Damage半径」「既定分離Impulse」を使う簡略版。
	static bool ApplyDamage(
		int32_t gameObjectId,
		const EditorScriptVector3& worldPosition,
		float damage) {
		return ApplyDamage(gameObjectId, worldPosition, 0.0f, damage, -1.0f);
	}

	static bool FractureAll(int32_t gameObjectId, float impulse = 0.0f) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastFractureAll != nullptr &&
			runtimeApi->BlastFractureAll(gameObjectId, impulse);
	}

	static bool IsFractured(int32_t gameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastIsFractured != nullptr &&
			runtimeApi->BlastIsFractured(gameObjectId);
	}

	static int32_t GetChunkCount(int32_t gameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastGetChunkCount != nullptr
			? runtimeApi->BlastGetChunkCount(gameObjectId)
			: 0;
	}

	static int32_t GetActorCount(int32_t gameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastGetActorCount != nullptr
			? runtimeApi->BlastGetActorCount(gameObjectId)
			: 0;
	}

	static int32_t GetBondCount(int32_t gameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastGetBondCount != nullptr
			? runtimeApi->BlastGetBondCount(gameObjectId)
			: 0;
	}

	// chunkIndexは0からGetChunkCount()未満。分裂後の演出・スコア加算等を個々のChunkへ紐付けるために使う。
	static bool GetChunkGameObjectId(int32_t gameObjectId, int32_t chunkIndex, int32_t& chunkGameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastGetChunkGameObjectId != nullptr &&
			runtimeApi->BlastGetChunkGameObjectId(gameObjectId, chunkIndex, &chunkGameObjectId);
	}

	// そのChunkが本体からすでに分離済み(動的Rigidbody化済み)かどうか。
	static bool IsChunkDetached(int32_t gameObjectId, int32_t chunkIndex) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->BlastIsChunkDetached != nullptr &&
			runtimeApi->BlastIsChunkDetached(gameObjectId, chunkIndex);
	}
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

	static EditorScriptVector2 GetMousePosition() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->GetMousePosition == nullptr) {
			return EditorScriptVector2{};
		}

		return runtimeApi->GetMousePosition();
	}

	static EditorScriptVector2 GetMouseDelta() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->GetMouseDelta == nullptr) {
			return EditorScriptVector2{};
		}

		return runtimeApi->GetMouseDelta();
	}

	static bool GetMouseButton(MouseButton mouseButton) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return runtimeApi != nullptr && runtimeApi->IsMouseButtonDown != nullptr &&
			runtimeApi->IsMouseButtonDown(static_cast<int32_t>(mouseButton));
	}

	static bool GetMouseButtonDown(MouseButton mouseButton) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return runtimeApi != nullptr && runtimeApi->WasMouseButtonPressed != nullptr &&
			runtimeApi->WasMouseButtonPressed(static_cast<int32_t>(mouseButton));
	}

	static bool GetMouseButtonUp(MouseButton mouseButton) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return runtimeApi != nullptr && runtimeApi->WasMouseButtonReleased != nullptr &&
			runtimeApi->WasMouseButtonReleased(static_cast<int32_t>(mouseButton));
	}

	static void SetCursorLocked(bool isLocked) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->SetCursorLocked != nullptr) {
			runtimeApi->SetCursorLocked(isLocked);
		}
	}

	static bool IsCursorLocked() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return runtimeApi != nullptr && runtimeApi->IsCursorLocked != nullptr &&
			runtimeApi->IsCursorLocked();
	}

	static void SetCursorVisible(bool isVisible) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->SetCursorVisible != nullptr) {
			runtimeApi->SetCursorVisible(isVisible);
		}
	}

	static bool IsCursorVisible() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return runtimeApi == nullptr || runtimeApi->IsCursorVisible == nullptr ||
			runtimeApi->IsCursorVisible();
	}
};

// パズル1エリア分だけを初期状態へ戻す。Scene全体のReloadと違い、
// 他エリアの進行、接続済みWire、Playerの位置を巻き戻さない。
// エリアの起点GameObjectを渡すと、その子孫すべてが対象になる。
class PuzzleArea final {
public:
	// 戻す先の状態を控える。通常はPlay開始直後に1回呼ぶ。
	static bool CaptureInitialState(int32_t areaRootGameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CaptureAreaState != nullptr &&
			runtimeApi->CaptureAreaState(areaRootGameObjectId);
	}

	// Transform、速度、角速度、Active、Animation再生位置を戻し、
	// このエリアのHookに繋がっているRuntime Wireを破棄する。
	static bool Reset(int32_t areaRootGameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->ResetArea != nullptr &&
			runtimeApi->ResetArea(areaRootGameObjectId);
	}

	// CaptureInitialState済みならtrue。未CaptureのエリアへResetを呼んでも何も起きない。
	static bool HasInitialState(int32_t areaRootGameObjectId) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->HasAreaState != nullptr &&
			runtimeApi->HasAreaState(areaRootGameObjectId);
	}
};

class SceneManager final {
public:
	static bool Reload() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->ReloadPrimaryScene != nullptr &&
			runtimeApi->ReloadPrimaryScene();
	}

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

class Component;

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

	static std::vector<GameObject> FindAllWithComponent(const char* componentTypeName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		std::vector<GameObject> gameObjects;

		if (runtimeApi == nullptr || runtimeApi->FindGameObjectsWithComponent == nullptr ||
			componentTypeName == nullptr || componentTypeName[0] == '\0') {
			return gameObjects;
		}

		const int32_t gameObjectCount = runtimeApi->FindGameObjectsWithComponent(
			componentTypeName,
			nullptr,
			0);

		if (gameObjectCount <= 0) {
			return gameObjects;
		}

		std::vector<int32_t> gameObjectIds(static_cast<size_t>(gameObjectCount), -1);
		const int32_t resolvedCount = runtimeApi->FindGameObjectsWithComponent(
			componentTypeName,
			gameObjectIds.data(),
			gameObjectCount);
		gameObjects.reserve(static_cast<size_t>((std::min)(resolvedCount, gameObjectCount)));

		for (int32_t gameObjectIndex = 0;
			gameObjectIndex < resolvedCount && gameObjectIndex < gameObjectCount;
			gameObjectIndex++) {
			gameObjects.emplace_back(gameObjectIds[static_cast<size_t>(gameObjectIndex)]);
		}

		return gameObjects;
	}

	static GameObject Create(const char* gameObjectName = "GameObject") {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->CreateGameObject == nullptr) {
			return GameObject{};
		}

		return GameObject{runtimeApi->CreateGameObject(gameObjectName)};
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

	Component GetComponent(const char* componentTypeName) const;

	bool AddComponent(const char* componentTypeName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && componentTypeName != nullptr && componentTypeName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->AddComponent != nullptr &&
			runtimeApi->AddComponent(gameObjectId_, componentTypeName);
	}

	bool RemoveComponent(const char* componentTypeName) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && componentTypeName != nullptr && componentTypeName[0] != '\0' &&
			runtimeApi != nullptr && runtimeApi->RemoveComponent != nullptr &&
			runtimeApi->RemoveComponent(gameObjectId_, componentTypeName);
	}

	template <typename ComponentType>
	ComponentType AddComponent() const {
		AddComponent(ComponentType::TypeName());
		return ComponentType{gameObjectId_};
	}

	template <typename ComponentType>
	ComponentType GetOrAddComponent() const {
		if (!HasComponent(ComponentType::TypeName())) {
			AddComponent(ComponentType::TypeName());
		}

		return ComponentType{gameObjectId_};
	}

	template <typename ComponentType>
	bool RemoveComponent() const {
		return RemoveComponent(ComponentType::TypeName());
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

	bool WorldToLocalPoint(const EditorScriptVector3& worldPoint, EditorScriptVector3& localPoint) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->WorldToLocalPoint != nullptr &&
			runtimeApi->WorldToLocalPoint(gameObjectId_, &worldPoint, &localPoint);
	}

	bool LocalToWorldPoint(const EditorScriptVector3& localPoint, EditorScriptVector3& worldPoint) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->LocalToWorldPoint != nullptr &&
			runtimeApi->LocalToWorldPoint(gameObjectId_, &localPoint, &worldPoint);
	}

	bool WorldToLocalDirection(const EditorScriptVector3& worldDirection, EditorScriptVector3& localDirection) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->WorldToLocalDirection != nullptr &&
			runtimeApi->WorldToLocalDirection(gameObjectId_, &worldDirection, &localDirection);
	}

	bool LocalToWorldDirection(const EditorScriptVector3& localDirection, EditorScriptVector3& worldDirection) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->LocalToWorldDirection != nullptr &&
			runtimeApi->LocalToWorldDirection(gameObjectId_, &localDirection, &worldDirection);
	}

	GameObject Instantiate(
		const EditorScriptVector3& position,
		const EditorScriptVector3& rotation = EditorScriptVector3{}) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (!HasReference() || runtimeApi == nullptr || runtimeApi->InstantiateGameObject == nullptr) {
			return GameObject{};
		}

		return GameObject{runtimeApi->InstantiateGameObject(gameObjectId_, &position, &rotation)};
	}

	bool Destroy() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->DestroyGameObject != nullptr &&
			runtimeApi->DestroyGameObject(gameObjectId_);
	}

	GameObject GetParent() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->GetParentGameObject != nullptr
			? GameObject{runtimeApi->GetParentGameObject(gameObjectId_)}
			: GameObject{};
	}

	bool SetParent(const GameObject& parent, bool preserveWorldTransform = false) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->SetParentGameObject != nullptr &&
			runtimeApi->SetParentGameObject(
				gameObjectId_,
				parent.GetInstanceId(),
				preserveWorldTransform);
	}

	int32_t GetChildCount() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->GetChildGameObjectCount != nullptr
			? runtimeApi->GetChildGameObjectCount(gameObjectId_)
			: 0;
	}

	GameObject GetChild(int32_t childIndex) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return HasReference() && runtimeApi != nullptr && runtimeApi->GetChildGameObject != nullptr
			? GameObject{runtimeApi->GetChildGameObject(gameObjectId_, childIndex)}
			: GameObject{};
	}

private:
	int32_t gameObjectId_ = -1;
};

//============================================================
// 既存Component参照
//============================================================

class Component final {
public:
	Component(const GameObject& ownerGameObject, const char* componentTypeName)
		: ownerGameObject_(ownerGameObject),
		componentTypeName_(componentTypeName != nullptr ? componentTypeName : "") {
	}

	bool IsValid() const {
		return !componentTypeName_.empty() &&
			ownerGameObject_.HasComponent(componentTypeName_.c_str());
	}

	bool SetActive(bool isActive) const {
		return ownerGameObject_.HasReference() && !componentTypeName_.empty() &&
			ownerGameObject_.SetComponentActive(componentTypeName_.c_str(), isActive);
	}

	bool IsActive() const {
		return ownerGameObject_.HasReference() && !componentTypeName_.empty() &&
			ownerGameObject_.IsComponentActive(componentTypeName_.c_str());
	}

	bool SetFloat(const char* propertyName, float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->SetRuntimeFloat != nullptr &&
			runtimeApi->SetRuntimeFloat(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				value);
	}

	bool GetFloat(const char* propertyName, float& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->GetRuntimeFloat != nullptr &&
			runtimeApi->GetRuntimeFloat(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool SetInt(const char* propertyName, int32_t value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->SetRuntimeInt != nullptr &&
			runtimeApi->SetRuntimeInt(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				value);
	}

	bool GetInt(const char* propertyName, int32_t& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->GetRuntimeInt != nullptr &&
			runtimeApi->GetRuntimeInt(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool SetBool(const char* propertyName, bool value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->SetRuntimeBool != nullptr &&
			runtimeApi->SetRuntimeBool(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				value);
	}

	bool GetBool(const char* propertyName, bool& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->GetRuntimeBool != nullptr &&
			runtimeApi->GetRuntimeBool(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool SetVector2(const char* propertyName, const EditorScriptVector2& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->SetRuntimeVector2 != nullptr &&
			runtimeApi->SetRuntimeVector2(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool GetVector2(const char* propertyName, EditorScriptVector2& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->GetRuntimeVector2 != nullptr &&
			runtimeApi->GetRuntimeVector2(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool SetVector3(const char* propertyName, const EditorScriptVector3& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->SetRuntimeVector3 != nullptr &&
			runtimeApi->SetRuntimeVector3(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool GetVector3(const char* propertyName, EditorScriptVector3& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		return CanAccessProperty(propertyName) && runtimeApi != nullptr &&
			runtimeApi->GetRuntimeVector3 != nullptr &&
			runtimeApi->GetRuntimeVector3(
				ownerGameObject_.GetInstanceId(),
				componentTypeName_.c_str(),
				propertyName,
				&value);
	}

	bool SetGameObject(const char* propertyName, const GameObject& value) const {
		return SetInt(propertyName, value.GetInstanceId());
	}

	bool GetGameObject(const char* propertyName, GameObject& value) const {
		int32_t referencedGameObjectId = -1;

		if (!GetInt(propertyName, referencedGameObjectId)) {
			return false;
		}

		value = GameObject{referencedGameObjectId};
		return true;
	}

private:
	bool CanAccessProperty(const char* propertyName) const {
		return ownerGameObject_.HasReference() && !componentTypeName_.empty() &&
			propertyName != nullptr && propertyName[0] != '\0';
	}

	GameObject ownerGameObject_{};
	std::string componentTypeName_{};
};

inline Component GameObject::GetComponent(const char* componentTypeName) const {
	return Component{*this, componentTypeName};
}

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

using JointHandle = EditorScriptJointHandle;
using SpringJointDesc = EditorScriptSpringJointDesc;
using JointType = EditorScriptJointType;
using JointDesc = EditorScriptJointDesc;

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

	// ignoreHierarchyRoot自身およびその子孫を丸ごと無視してRaycastする(自機や自機に載っている
	// 武器・部品を狙点判定へ誤検出させないためのAPI)。
	static bool RaycastIgnoringHierarchy(
		const EditorScriptRay& ray,
		float distance,
		const GameObject& ignoreHierarchyRoot,
		EditorScriptPhysicsHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->PhysicsRaycastIgnoringHierarchy != nullptr &&
			runtimeApi->PhysicsRaycastIgnoringHierarchy(
				&ray,
				distance,
				ignoreHierarchyRoot.GetInstanceId(),
				&hit);
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

	static bool RaycastFiltered(
		const EditorScriptRay& ray,
		float distance,
		uint32_t physicsLayerMask,
		bool includeTriggers,
		const char* requiredComponentTypeName,
		EditorScriptPhysicsHit& hit) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->PhysicsRaycastFiltered != nullptr &&
			runtimeApi->PhysicsRaycastFiltered(
				&ray,
				distance,
				physicsLayerMask,
				includeTriggers,
				requiredComponentTypeName,
				&hit);
	}

	static bool RaycastConnectable(
		const EditorScriptRay& ray,
		float distance,
		EditorScriptPhysicsHit& hit,
		uint32_t physicsLayerMask = 0xFFFFFFFFU) {
		return RaycastFiltered(
			ray,
			distance,
			physicsLayerMask,
			false,
			"WireConnectable",
			hit);
	}

	// 照準Rayを中心とする円錐内から、角度が最も近く遮蔽されていないHookを選ぶ。
	static bool FindBestHook(
		const EditorScriptRay& aimRay,
		float maximumDistance,
		float maximumAngleDegrees,
		EditorScriptPhysicsHit& hit) {
		const float directionLength = std::sqrt(
			aimRay.direction.x * aimRay.direction.x +
			aimRay.direction.y * aimRay.direction.y +
			aimRay.direction.z * aimRay.direction.z);

		if (directionLength <= 0.0001f || maximumDistance <= 0.0f) {
			return false;
		}

		const EditorScriptVector3 normalizedAimDirection{
			aimRay.direction.x / directionLength,
			aimRay.direction.y / directionLength,
			aimRay.direction.z / directionLength};
		const float minimumDot = std::cos(
			(std::clamp)(maximumAngleDegrees, 0.0f, 180.0f) * 0.017453292519943295f);
		float bestDot = -2.0f;
		float bestDistance = maximumDistance;
		bool foundHook = false;

		for (const GameObject& hookGameObject : GameObject::FindAllWithComponent("WireConnectable")) {
			EditorScriptVector3 hookWorldPosition{};
			EditorScriptVector3 localAnchor{};
			const Component hookComponent = hookGameObject.GetComponent("WireConnectable");
			hookComponent.GetVector3("wireConnectableLocalAnchor", localAnchor);

			if (!hookGameObject.LocalToWorldPoint(localAnchor, hookWorldPosition)) {
				continue;
			}

			const EditorScriptVector3 toHook{
				hookWorldPosition.x - aimRay.origin.x,
				hookWorldPosition.y - aimRay.origin.y,
				hookWorldPosition.z - aimRay.origin.z};
			const float hookDistance = std::sqrt(
				toHook.x * toHook.x + toHook.y * toHook.y + toHook.z * toHook.z);

			if (hookDistance <= 0.0001f || hookDistance > maximumDistance) {
				continue;
			}

			const EditorScriptVector3 hookDirection{
				toHook.x / hookDistance,
				toHook.y / hookDistance,
				toHook.z / hookDistance};
			const float directionDot =
				normalizedAimDirection.x * hookDirection.x +
				normalizedAimDirection.y * hookDirection.y +
				normalizedAimDirection.z * hookDirection.z;

			if (directionDot < minimumDot ||
				(directionDot < bestDot && hookDistance >= bestDistance)) {
				continue;
			}

			EditorScriptRay hookRay{};
			hookRay.origin = aimRay.origin;
			hookRay.direction = hookDirection;
			EditorScriptPhysicsHit candidateHit{};

			// 通常Raycastを使い、壁や別物体の裏にあるHookを選ばない。
			if (!Raycast(hookRay, hookDistance + 0.1f, candidateHit) ||
				candidateHit.gameObjectId != hookGameObject.GetInstanceId()) {
				continue;
			}

			bestDot = directionDot;
			bestDistance = hookDistance;
			hit = candidateHit;
			foundHook = true;
		}

		return foundHook;
	}

	static JointHandle CreateSpringJoint(
		const GameObject& ownerGameObject,
		const GameObject& connectedGameObject,
		const SpringJointDesc& springJointDesc) {
		if (!ownerGameObject.HasReference() || !connectedGameObject.HasReference()) {
			return kInvalidEditorScriptJointHandle;
		}

		return CreateSpringJoint(
			ownerGameObject.GetInstanceId(),
			connectedGameObject.GetInstanceId(),
			springJointDesc);
	}

	static JointHandle CreateSpringJoint(
		int32_t ownerGameObjectId,
		int32_t connectedGameObjectId,
		const SpringJointDesc& springJointDesc) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (ownerGameObjectId < 0 || connectedGameObjectId < 0 ||
			runtimeApi == nullptr || runtimeApi->CreateSpringJoint == nullptr) {
			return kInvalidEditorScriptJointHandle;
		}

		return runtimeApi->CreateSpringJoint(
			ownerGameObjectId,
			connectedGameObjectId,
			&springJointDesc);
	}

	static bool DestroyJoint(JointHandle jointHandle) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return jointHandle != kInvalidEditorScriptJointHandle && runtimeApi != nullptr &&
			runtimeApi->DestroyJoint != nullptr && runtimeApi->DestroyJoint(jointHandle);
	}

	static bool SetSpringJointSettings(
		JointHandle jointHandle,
		const SpringJointDesc& springJointDesc) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return jointHandle != kInvalidEditorScriptJointHandle && runtimeApi != nullptr &&
			runtimeApi->SetSpringJointSettings != nullptr &&
			runtimeApi->SetSpringJointSettings(jointHandle, &springJointDesc);
	}

	static bool IsJointValid(JointHandle jointHandle) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return jointHandle != kInvalidEditorScriptJointHandle && runtimeApi != nullptr &&
			runtimeApi->IsJointValid != nullptr && runtimeApi->IsJointValid(jointHandle);
	}

	static JointHandle CreateJoint(
		JointType jointType,
		int32_t ownerGameObjectId,
		int32_t connectedGameObjectId,
		const JointDesc& jointDesc) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		if (ownerGameObjectId < 0 || connectedGameObjectId < 0 ||
			runtimeApi == nullptr || runtimeApi->CreateJoint == nullptr) {
			return kInvalidEditorScriptJointHandle;
		}

		return runtimeApi->CreateJoint(
			jointType,
			ownerGameObjectId,
			connectedGameObjectId,
			&jointDesc);
	}

	static JointHandle CreateJoint(
		JointType jointType,
		const GameObject& ownerGameObject,
		const GameObject& connectedGameObject,
		const JointDesc& jointDesc) {
		if (!ownerGameObject.HasReference() || !connectedGameObject.HasReference()) {
			return kInvalidEditorScriptJointHandle;
		}

		return CreateJoint(
			jointType,
			ownerGameObject.GetInstanceId(),
			connectedGameObject.GetInstanceId(),
			jointDesc);
	}

	static bool SetJointSettings(JointHandle jointHandle, const JointDesc& jointDesc) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return jointHandle != kInvalidEditorScriptJointHandle && runtimeApi != nullptr &&
			runtimeApi->SetJointSettings != nullptr &&
			runtimeApi->SetJointSettings(jointHandle, &jointDesc);
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

// AudioSource を持つ GameObject を Script から鳴らす。
// 例: Audio{GameObject::Find("SFX Explosion Small")}.Play();
class Audio final {
public:
	explicit Audio(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Play() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->PlayAudio != nullptr &&
			runtimeApi->PlayAudio(gameObjectId_);
	}

	void Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->StopAudio != nullptr) {
			runtimeApi->StopAudio(gameObjectId_);
		}
	}

	// Bus は 0=SFX / 1=BGM / 2=Ambience / 3=UI。BGM ダッキング等に使う。
	static void SetBusVolume(int32_t audioBus, float volume) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->SetAudioBusVolume != nullptr) {
			runtimeApi->SetAudioBusVolume(audioBus, volume);
		}
	}

	static float GetBusVolume(int32_t audioBus) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->GetAudioBusVolume != nullptr
			? runtimeApi->GetAudioBusVolume(audioBus)
			: 0.0f;
	}

	static void SetMasterVolume(float volume) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->SetAudioMasterVolume != nullptr) {
			runtimeApi->SetAudioMasterVolume(volume);
		}
	}

	static float GetMasterVolume() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->GetAudioMasterVolume != nullptr
			? runtimeApi->GetAudioMasterVolume()
			: 0.0f;
	}

	// 個別Voice操作用のHandleを返す再生。戻り値をAudioVoiceへ渡して音量・Pitch等を後から変える。
	// 使い方: AudioVoice voice = Audio(gameObject).PlayVoice(); voice.SetVolume(0.5f);
	EditorScriptAudioHandle PlayVoice() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->AudioPlayWithHandle != nullptr
			? runtimeApi->AudioPlayWithHandle(gameObjectId_)
			: kInvalidEditorScriptAudioHandle;
	}

	// AudioSource Componentを用意せず、Clipを直接World座標へ鳴らす(着弾音・足音など)。
	static EditorScriptAudioHandle PlayAtPosition(
		const char* clipAssetPath,
		const EditorScriptVector3& position,
		int32_t audioBus = 0,
		float volume = 1.0f,
		bool loop = false) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioPlayClipAtPosition != nullptr
			? runtimeApi->AudioPlayClipAtPosition(clipAssetPath, &position, audioBus, volume, loop)
			: kInvalidEditorScriptAudioHandle;
	}

	// 距離減衰もPanもしない2D再生(BGM、UI SE)。
	static EditorScriptAudioHandle Play2D(
		const char* clipAssetPath,
		int32_t audioBus = 0,
		float volume = 1.0f,
		bool loop = false) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioPlayClip2D != nullptr
			? runtimeApi->AudioPlayClip2D(clipAssetPath, audioBus, volume, loop)
			: kInvalidEditorScriptAudioHandle;
	}

private:
	int32_t gameObjectId_ = -1;
};

// 再生中のAudio Voice 1本を操作する。再生が終わったVoiceのHandleは無効になり、
// 以降の操作は全てfalseを返すだけで何もしない(Crashしない)。
class AudioVoice final {
public:
	using Handle = EditorScriptAudioHandle;

	explicit AudioVoice(Handle audioHandle = kInvalidEditorScriptAudioHandle)
		: audioHandle_(audioHandle) {
	}

	Handle GetHandle() const {
		return audioHandle_;
	}

	// 再生が続いているか(Pause中も含む)。
	bool IsValid() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioIsHandleValid != nullptr &&
			runtimeApi->AudioIsHandleValid(audioHandle_);
	}

	// 今実際に鳴っているか(Pause中はfalse)。
	bool IsPlaying() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioIsPlayingHandle != nullptr &&
			runtimeApi->AudioIsPlayingHandle(audioHandle_);
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioStopHandle != nullptr &&
			runtimeApi->AudioStopHandle(audioHandle_);
	}

	bool Pause() const {
		return SetPaused(true);
	}

	bool Resume() const {
		return SetPaused(false);
	}

	bool SetPaused(bool isPaused) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetPaused != nullptr &&
			runtimeApi->AudioSetPaused(audioHandle_, isPaused);
	}

	bool SetVolume(float volume) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetVolumeHandle != nullptr &&
			runtimeApi->AudioSetVolumeHandle(audioHandle_, volume);
	}

	bool GetVolume(float& volume) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioGetVolumeHandle != nullptr &&
			runtimeApi->AudioGetVolumeHandle(audioHandle_, &volume);
	}

	bool SetPitch(float pitch) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetPitch != nullptr &&
			runtimeApi->AudioSetPitch(audioHandle_, pitch);
	}

	bool GetPitch(float& pitch) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioGetPitch != nullptr &&
			runtimeApi->AudioGetPitch(audioHandle_, &pitch);
	}

	bool SetLoop(bool loop) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetLoop != nullptr &&
			runtimeApi->AudioSetLoop(audioHandle_, loop);
	}

	bool GetLoop(bool& loop) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioGetLoop != nullptr &&
			runtimeApi->AudioGetLoop(audioHandle_, &loop);
	}

	// Audio::PlayAtPositionで鳴らしたVoiceの位置を更新する。
	// AudioSource経由のVoiceはGameObjectのTransformが位置なのでfalseを返す。
	bool SetPosition(const EditorScriptVector3& position) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetPositionHandle != nullptr &&
			runtimeApi->AudioSetPositionHandle(audioHandle_, &position);
	}

	bool SetBus(int32_t audioBus) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetBus != nullptr &&
			runtimeApi->AudioSetBus(audioHandle_, audioBus);
	}

	bool GetPlaybackPosition(float& seconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioGetPlaybackPosition != nullptr &&
			runtimeApi->AudioGetPlaybackPosition(audioHandle_, &seconds);
	}

	bool SetPlaybackPosition(float seconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioSetPlaybackPosition != nullptr &&
			runtimeApi->AudioSetPlaybackPosition(audioHandle_, seconds);
	}

	bool GetDuration(float& seconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioGetDuration != nullptr &&
			runtimeApi->AudioGetDuration(audioHandle_, &seconds);
	}

	// durationSeconds秒かけてtargetVolumeへ寄せる。targetVolume=0なら到達時に自動停止する。
	bool FadeTo(float targetVolume, float durationSeconds) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->AudioFadeTo != nullptr &&
			runtimeApi->AudioFadeTo(audioHandle_, targetVolume, durationSeconds);
	}

	bool FadeIn(float durationSeconds, float targetVolume = 1.0f) const {
		return SetVolume(0.0f) && FadeTo(targetVolume, durationSeconds);
	}

	bool FadeOut(float durationSeconds) const {
		return FadeTo(0.0f, durationSeconds);
	}

private:
	Handle audioHandle_ = kInvalidEditorScriptAudioHandle;
};

// GameObjectを介さず任意のWorld座標へEffekseer(.efk/.efkefc)を再生する。
// 爆発・着弾・水しぶきなど、Effect専用GameObjectを予め置けない使い捨て演出向け。
// 例: EffectManager::PlayEffekseer("Assets/Effects/Explosion.efkefc", position);
class EffectManager final {
public:
	// 戻り値はSetPosition/Stopに渡すHandle(失敗時-1)。位置追従が不要なら戻り値を捨ててよい。
	static int32_t PlayEffekseer(
		const std::string& effectAssetPath,
		const EditorScriptVector3& position,
		const EditorScriptVector3& rotationEuler = EditorScriptVector3{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (effectAssetPath.empty() || runtimeApi == nullptr || runtimeApi->PlayEffekseerAtPosition == nullptr) {
			return -1;
		}

		return runtimeApi->PlayEffekseerAtPosition(effectAssetPath.c_str(), &position, &rotationEuler);
	}

	static bool SetPosition(int32_t effekseerPlaybackHandle, const EditorScriptVector3& position) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return effekseerPlaybackHandle >= 0 && runtimeApi != nullptr && runtimeApi->SetEffekseerEffectPosition != nullptr &&
			runtimeApi->SetEffekseerEffectPosition(effekseerPlaybackHandle, &position);
	}

	static void Stop(int32_t effekseerPlaybackHandle) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (effekseerPlaybackHandle >= 0 && runtimeApi != nullptr && runtimeApi->StopEffekseerEffectAtPosition != nullptr) {
			runtimeApi->StopEffekseerEffectAtPosition(effekseerPlaybackHandle);
		}
	}
};

// 再生中のVFX Instance 1個を操作する。.effectdef(Effect ID)と Effekseer(.efk/.efkefc)の
// どちらで再生したかはHandleが覚えているため、Script側は同じ操作APIを使える。
// 再生が終わったInstanceのHandleは無効になり、以降の操作は全てfalseを返す(Crashしない)。
class VfxInstance final {
public:
	using Handle = EditorScriptVfxHandle;

	explicit VfxInstance(Handle vfxHandle = kInvalidEditorScriptVfxHandle)
		: vfxHandle_(vfxHandle) {
	}

	// World座標へ発生させる。effectIdOrAssetPathが.efk/.efkefcならEffekseer、
	// それ以外は.effectdefのEffect IDとして再生する。
	static VfxInstance Spawn(
		const char* effectIdOrAssetPath,
		const EditorScriptVector3& position,
		const EditorScriptVector3& rotationEuler = EditorScriptVector3{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSpawn != nullptr
			? VfxInstance{runtimeApi->VfxSpawn(effectIdOrAssetPath, &position, &rotationEuler)}
			: VfxInstance{};
	}

	// GameObjectへ追従させて発生させる(銃口炎・ミサイル曳光など)。
	// Effekseerは追従を持たないため、発生時点のWorld座標へ固定される。
	static VfxInstance SpawnAttached(
		const char* effectIdOrAssetPath,
		const GameObject& followGameObject,
		const EditorScriptVector3& localOffset = EditorScriptVector3{}) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSpawnAttached != nullptr
			? VfxInstance{runtimeApi->VfxSpawnAttached(
				  effectIdOrAssetPath, followGameObject.GetInstanceId(), &localOffset)}
			: VfxInstance{};
	}

	Handle GetHandle() const {
		return vfxHandle_;
	}

	bool IsPlaying() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxIsPlayingHandle != nullptr &&
			runtimeApi->VfxIsPlayingHandle(vfxHandle_);
	}

	// 新規発生を止める。既に出ているParticleは寿命まで残る。
	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxStopHandle != nullptr &&
			runtimeApi->VfxStopHandle(vfxHandle_);
	}

	bool Pause() const {
		return SetPaused(true);
	}

	bool Resume() const {
		return SetPaused(false);
	}

	bool SetPaused(bool isPaused) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSetPausedHandle != nullptr &&
			runtimeApi->VfxSetPausedHandle(vfxHandle_, isPaused);
	}

	// 既存Particleを捨てて最初から再生し直す。
	bool Restart() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxRestartHandle != nullptr &&
			runtimeApi->VfxRestartHandle(vfxHandle_);
	}

	bool SetPosition(const EditorScriptVector3& position) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSetPositionHandle != nullptr &&
			runtimeApi->VfxSetPositionHandle(vfxHandle_, &position);
	}

	bool GetPosition(EditorScriptVector3& position) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxGetPositionHandle != nullptr &&
			runtimeApi->VfxGetPositionHandle(vfxHandle_, &position);
	}

	bool SetPlaybackSpeed(float playbackSpeed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSetPlaybackSpeed != nullptr &&
			runtimeApi->VfxSetPlaybackSpeed(vfxHandle_, playbackSpeed);
	}

	// Effekseerは設定値を読み戻せないため、Effekseer Instanceではfalseを返す。
	bool GetPlaybackSpeed(float& playbackSpeed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxGetPlaybackSpeed != nullptr &&
			runtimeApi->VfxGetPlaybackSpeed(vfxHandle_, &playbackSpeed);
	}

	// .effectdef Instanceの生存Particle数。Effekseerは非対応でfalse。
	bool GetParticleCount(int32_t& particleCount) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxGetParticleCountHandle != nullptr &&
			runtimeApi->VfxGetParticleCountHandle(vfxHandle_, &particleCount);
	}

	// 回転・拡縮はEffekseer Instanceのみ対応する(.effectdefはInstance Transformを持たない)。
	bool SetRotation(const EditorScriptVector3& rotationEuler) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSetRotationHandle != nullptr &&
			runtimeApi->VfxSetRotationHandle(vfxHandle_, &rotationEuler);
	}

	bool SetScale(const EditorScriptVector3& scale) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->VfxSetScaleHandle != nullptr &&
			runtimeApi->VfxSetScaleHandle(vfxHandle_, &scale);
	}

private:
	Handle vfxHandle_ = kInvalidEditorScriptVfxHandle;
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

// Terrain Componentを持つGameObjectの高さを問い合わせる。
// 描画(頂点シェーダ)・Collider・このAPIは同じHeightMapと同じ式を使うため、値が一致する。
// Raycastを撃たずに接地高さが要る場面(設置、AIの経路判断、カメラ追従)で使う。
class Terrain final {
public:
	explicit Terrain(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	// World XZ における地表のY(World)を返す。Terrain範囲外でも端の高さでClampして返す。
	bool GetHeightAt(float worldX, float worldZ, float& worldHeight) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->TerrainGetHeightAtWorld != nullptr &&
			runtimeApi->TerrainGetHeightAtWorld(gameObjectId_, worldX, worldZ, &worldHeight);
	}

	bool GetHeightAt(const EditorScriptVector3& worldPosition, float& worldHeight) const {
		return GetHeightAt(worldPosition.x, worldPosition.z, worldHeight);
	}

	// そのWorld XZがTerrainのXZ範囲内か。範囲外を弾きたい場合はGetHeightAtの前に確認する。
	bool Contains(float worldX, float worldZ) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->TerrainContainsWorldPosition != nullptr &&
			runtimeApi->TerrainContainsWorldPosition(gameObjectId_, worldX, worldZ);
	}

private:
	int32_t gameObjectId_ = -1;
};

// Text / Button / Toggle / Slider などのUI Componentを持つGameObjectを操作する。
// 表示文字列はstd::stringのため汎用Field API(GetFloat/SetFloat等)では扱えず、このClassが唯一の経路である。
// 対象GameObjectにUI Componentが無い場合は全て false を返す。
class Ui final {
public:
	explicit Ui(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool SetText(const char* text) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetText != nullptr &&
			runtimeApi->UiSetText(gameObjectId_, text);
	}

	bool SetText(const std::string& text) const {
		return SetText(text.c_str());
	}

	bool GetText(std::string& text) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->UiGetText == nullptr) {
			return false;
		}

		char buffer[1024] = {};

		if (!runtimeApi->UiGetText(gameObjectId_, buffer, static_cast<int32_t>(sizeof(buffer)))) {
			return false;
		}

		text = buffer;
		return true;
	}

	bool SetColor(const EditorScriptVector3& color, float alpha = 1.0f) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetTextColor != nullptr &&
			runtimeApi->UiSetTextColor(gameObjectId_, &color, alpha);
	}

	bool GetColor(EditorScriptVector3& color, float& alpha) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiGetTextColor != nullptr &&
			runtimeApi->UiGetTextColor(gameObjectId_, &color, &alpha);
	}

	// 0以下を渡すとRect高さから決める自動サイズへ戻る。
	bool SetFontSize(float fontSize) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetFontSize != nullptr &&
			runtimeApi->UiSetFontSize(gameObjectId_, fontSize);
	}

	bool GetFontSize(float& fontSize) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiGetFontSize != nullptr &&
			runtimeApi->UiGetFontSize(gameObjectId_, &fontSize);
	}

	bool SetInteractable(bool isInteractable) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetInteractable != nullptr &&
			runtimeApi->UiSetInteractable(gameObjectId_, isInteractable);
	}

	bool GetInteractable(bool& isInteractable) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiGetInteractable != nullptr &&
			runtimeApi->UiGetInteractable(gameObjectId_, &isInteractable);
	}

	bool SetSliderValue(float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetSliderValue != nullptr &&
			runtimeApi->UiSetSliderValue(gameObjectId_, value);
	}

	bool GetSliderValue(float& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiGetSliderValue != nullptr &&
			runtimeApi->UiGetSliderValue(gameObjectId_, &value);
	}

	bool SetToggleValue(bool value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiSetToggleValue != nullptr &&
			runtimeApi->UiSetToggleValue(gameObjectId_, value);
	}

	bool GetToggleValue(bool& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->UiGetToggleValue != nullptr &&
			runtimeApi->UiGetToggleValue(gameObjectId_, &value);
	}

private:
	int32_t gameObjectId_ = -1;
};

// Camera / CinemachineCamera Componentを持つGameObjectの投影・向き・優先度をScriptから操作する。
// Blend / Shakeの再生は従来通りCameraEffectsを使う。
class Camera final {
public:
	explicit Camera(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit Camera(int32_t gameObjectId = -1)
		: gameObjectId_(gameObjectId) {
	}

	// Game Viewが今使っているCamera(有効なうち最大Priority)。無ければ無効なCameraを返す。
	static Camera GetActive() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetActive != nullptr
			? Camera{runtimeApi->CameraGetActive()}
			: Camera{};
	}

	int32_t GetInstanceId() const {
		return gameObjectId_;
	}

	bool IsValid() const {
		float fieldOfView = 0.0f;
		return GetFieldOfView(fieldOfView);
	}

	bool GetFieldOfView(float& fieldOfViewDegrees) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetFieldOfView != nullptr &&
			runtimeApi->CameraGetFieldOfView(gameObjectId_, &fieldOfViewDegrees);
	}

	bool SetFieldOfView(float fieldOfViewDegrees) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetFieldOfView != nullptr &&
			runtimeApi->CameraSetFieldOfView(gameObjectId_, fieldOfViewDegrees);
	}

	bool GetNearClip(float& nearClip) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetNearClip != nullptr &&
			runtimeApi->CameraGetNearClip(gameObjectId_, &nearClip);
	}

	bool SetNearClip(float nearClip) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetNearClip != nullptr &&
			runtimeApi->CameraSetNearClip(gameObjectId_, nearClip);
	}

	bool GetFarClip(float& farClip) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetFarClip != nullptr &&
			runtimeApi->CameraGetFarClip(gameObjectId_, &farClip);
	}

	bool SetFarClip(float farClip) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetFarClip != nullptr &&
			runtimeApi->CameraSetFarClip(gameObjectId_, farClip);
	}

	// 0=Perspective、1=Orthographic。
	bool GetProjectionMode(int32_t& projectionMode) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetProjectionMode != nullptr &&
			runtimeApi->CameraGetProjectionMode(gameObjectId_, &projectionMode);
	}

	bool SetProjectionMode(int32_t projectionMode) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetProjectionMode != nullptr &&
			runtimeApi->CameraSetProjectionMode(gameObjectId_, projectionMode);
	}

	bool GetOrthographicSize(float& orthographicSize) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetOrthographicSize != nullptr &&
			runtimeApi->CameraGetOrthographicSize(gameObjectId_, &orthographicSize);
	}

	bool SetOrthographicSize(float orthographicSize) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetOrthographicSize != nullptr &&
			runtimeApi->CameraSetOrthographicSize(gameObjectId_, orthographicSize);
	}

	bool GetPriority(int32_t& priority) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetPriority != nullptr &&
			runtimeApi->CameraGetPriority(gameObjectId_, &priority);
	}

	bool SetPriority(int32_t priority) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetPriority != nullptr &&
			runtimeApi->CameraSetPriority(gameObjectId_, priority);
	}

	bool GetEnabled(bool& isEnabled) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetEnabled != nullptr &&
			runtimeApi->CameraGetEnabled(gameObjectId_, &isEnabled);
	}

	bool SetEnabled(bool isEnabled) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetEnabled != nullptr &&
			runtimeApi->CameraSetEnabled(gameObjectId_, isEnabled);
	}

	// 他の有効Cameraより高いPriorityを与えて、このCameraへ切り替える。
	bool SetActive() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetActive != nullptr &&
			runtimeApi->CameraSetActive(gameObjectId_);
	}

	bool LookAt(const EditorScriptVector3& targetPosition) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraLookAt != nullptr &&
			runtimeApi->CameraLookAt(gameObjectId_, &targetPosition);
	}

	bool LookAt(const GameObject& targetGameObject) const {
		if (!targetGameObject.HasReference()) {
			return false;
		}

		return LookAt(targetGameObject.GetTransform().position);
	}

	bool GetLookDirection(EditorScriptVector3& direction) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraGetLookDirection != nullptr &&
			runtimeApi->CameraGetLookDirection(gameObjectId_, &direction);
	}

	bool SetLookDirection(const EditorScriptVector3& direction) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->CameraSetLookDirection != nullptr &&
			runtimeApi->CameraSetLookDirection(gameObjectId_, &direction);
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
	static constexpr const char* TypeName() {
		return "RigidBody";
	}

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
	static constexpr const char* TypeName() {
		return "RopeConstraint";
	}

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
// 複数接続対応 Wire
//================================================================

class WireConnectable final {
public:
	static constexpr const char* TypeName() {
		return "WireConnectable";
	}

	explicit WireConnectable(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit WireConnectable(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	bool IsValid() const {
		return GameObject{gameObjectId_}.HasComponent(TypeName());
	}

	bool CanConnect() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->CanConnectWire != nullptr &&
			runtimeApi->CanConnectWire(gameObjectId_);
	}

private:
	int32_t gameObjectId_ = -1;
};

enum class HookVisualState : int32_t {
	Normal = 0,
	Targeted = 1,
	Selected = 2,
	Connected = 3,
};

class Wire;

// Editor上の互換Component名はWireConnectableのまま保ち、ユーザーScriptにはHookPointとして見せる。
class HookPoint final {
public:
	static constexpr const char* TypeName() {
		return "WireConnectable";
	}

	explicit HookPoint(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit HookPoint(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	bool IsValid() const {
		return GameObject{gameObjectId_}.HasComponent(TypeName());
	}

	bool CanConnect() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->CanConnectWire != nullptr &&
			runtimeApi->CanConnectWire(gameObjectId_);
	}

	bool SetVisualState(HookVisualState visualState) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetHookVisualState != nullptr &&
			runtimeApi->SetHookVisualState(gameObjectId_, static_cast<int32_t>(visualState));
	}

	std::vector<Wire> GetWires() const;

private:
	int32_t gameObjectId_ = -1;
};

class Renderer final {
public:
	explicit Renderer(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool SetColor(const EditorScriptVector3& color) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRendererColor != nullptr &&
			runtimeApi->SetRendererColor(gameObjectId_, &color);
	}

	bool SetEmission(const EditorScriptVector3& color, float strength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SetRendererEmission != nullptr &&
			runtimeApi->SetRendererEmission(gameObjectId_, &color, strength);
	}

	bool GetColor(EditorScriptVector3& color) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetColor != nullptr &&
			runtimeApi->RendererGetColor(gameObjectId_, &color);
	}

	bool GetEmission(EditorScriptVector3& color, float& strength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetEmission != nullptr &&
			runtimeApi->RendererGetEmission(gameObjectId_, &color, &strength);
	}

	// 描画のON/OFF。ModelRenderer / SkinnedMeshRenderer Componentの有効状態を切り替える。
	bool SetEnabled(bool isEnabled) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererSetEnabled != nullptr &&
			runtimeApi->RendererSetEnabled(gameObjectId_, isEnabled);
	}

	bool GetEnabled(bool& isEnabled) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetEnabled != nullptr &&
			runtimeApi->RendererGetEnabled(gameObjectId_, &isEnabled);
	}

	bool SetOpacity(float opacity) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererSetOpacity != nullptr &&
			runtimeApi->RendererSetOpacity(gameObjectId_, opacity);
	}

	bool GetOpacity(float& opacity) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetOpacity != nullptr &&
			runtimeApi->RendererGetOpacity(gameObjectId_, &opacity);
	}

	// propertyNameは "Metallic" "Roughness" "IOR" "Alpha" "EmissionStrength" "ReflectionStrength"
	// "NormalScale" "AmbientOcclusionStrength" "HeightScale" "AlphaCutoff" "ClearCoat"
	// "ClearCoatRoughness" "Intensity"。大文字小文字は区別しない。未知名はfalse。
	bool SetMaterialFloat(const char* propertyName, float value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererSetMaterialFloat != nullptr &&
			runtimeApi->RendererSetMaterialFloat(gameObjectId_, propertyName, value);
	}

	bool GetMaterialFloat(const char* propertyName, float& value) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetMaterialFloat != nullptr &&
			runtimeApi->RendererGetMaterialFloat(gameObjectId_, propertyName, &value);
	}

	// propertyNameは "Color"("BaseColor" / "Albedo") または "EmissionColor"。
	bool SetMaterialColor(const char* propertyName, const EditorScriptVector3& color) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererSetMaterialColor != nullptr &&
			runtimeApi->RendererSetMaterialColor(gameObjectId_, propertyName, &color);
	}

	bool GetMaterialColor(const char* propertyName, EditorScriptVector3& color) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererGetMaterialColor != nullptr &&
			runtimeApi->RendererGetMaterialColor(gameObjectId_, propertyName, &color);
	}

	// slotNameは "BaseColor"("Albedo" / "Texture") "Normal" "Metallic" "Roughness"
	// "AmbientOcclusion"("AO") "Emission" "Height" "Opacity"。空文字を渡すとそのスロットを外す。
	bool SetMaterialTexture(const char* slotName, const char* textureAssetPath) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->RendererSetMaterialTexture != nullptr &&
			runtimeApi->RendererSetMaterialTexture(gameObjectId_, slotName, textureAssetPath);
	}

	bool GetMaterialTexture(const char* slotName, std::string& textureAssetPath) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->RendererGetMaterialTexture == nullptr) {
			return false;
		}

		char buffer[520] = {};

		if (!runtimeApi->RendererGetMaterialTexture(
				gameObjectId_, slotName, buffer, static_cast<int32_t>(sizeof(buffer)))) {
			return false;
		}

		textureAssetPath = buffer;
		return true;
	}

	bool SetColorFromMass(
		const GameObject& rigidBodyGameObject,
		float minimumMass,
		float maximumMass,
		const EditorScriptVector3& lightColor,
		const EditorScriptVector3& heavyColor) const {
		float mass = minimumMass;

		if (!rigidBodyGameObject.GetComponent("RigidBody").GetFloat("mass", mass)) {
			return false;
		}

		const float massRange = maximumMass - minimumMass;
		const float normalizedMass = std::fabs(massRange) > 0.0001f
			? (std::clamp)((mass - minimumMass) / massRange, 0.0f, 1.0f)
			: 0.0f;
		return SetColor(EditorScriptVector3{
			lightColor.x + (heavyColor.x - lightColor.x) * normalizedMass,
			lightColor.y + (heavyColor.y - lightColor.y) * normalizedMass,
			lightColor.z + (heavyColor.z - lightColor.z) * normalizedMass});
	}

private:
	int32_t gameObjectId_ = -1;
};

class WireRenderer final {
public:
	static constexpr const char* TypeName() {
		return "WireRenderer";
	}

	explicit WireRenderer(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	explicit WireRenderer(int32_t gameObjectId)
		: gameObjectId_(gameObjectId) {
	}

	bool IsValid() const {
		return GameObject{gameObjectId_}.HasComponent(TypeName());
	}

private:
	int32_t gameObjectId_ = -1;
};

class Wire final {
public:
	using Handle = EditorScriptWireHandle;
	using Desc = EditorScriptWireDesc;
	using State = EditorScriptWireState;

	explicit Wire(Handle wireHandle = kInvalidEditorScriptWireHandle)
		: wireHandle_(wireHandle) {
	}

	static Wire Create(const Desc& wireDesc) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->CreateWire == nullptr) {
			return Wire{};
		}

		return Wire{runtimeApi->CreateWire(&wireDesc)};
	}

	static Wire Create(
		const GameObject& firstGameObject,
		const GameObject& secondGameObject,
		const EditorScriptVector3& firstLocalAnchor,
		const EditorScriptVector3& secondLocalAnchor,
		float maximumLength,
		const GameObject& ownerGameObject = GameObject{}) {
		Desc wireDesc{};
		wireDesc.firstGameObjectId = firstGameObject.GetInstanceId();
		wireDesc.secondGameObjectId = secondGameObject.GetInstanceId();
		wireDesc.ownerGameObjectId = ownerGameObject.GetInstanceId();
		wireDesc.rendererSettingsGameObjectId = ownerGameObject.GetInstanceId();
		wireDesc.firstLocalAnchor = firstLocalAnchor;
		wireDesc.secondLocalAnchor = secondLocalAnchor;
		wireDesc.maximumLength = maximumLength;
		return Create(wireDesc);
	}

	static int32_t GetCount(const GameObject& gameObject) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObject.HasReference() && runtimeApi != nullptr &&
			runtimeApi->GetWireCountForGameObject != nullptr
			? runtimeApi->GetWireCountForGameObject(gameObject.GetInstanceId())
			: 0;
	}

	static std::vector<Wire> GetAll(const GameObject& gameObject) {
		std::vector<Wire> wires;
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		const int32_t wireCount = GetCount(gameObject);

		if (runtimeApi == nullptr || runtimeApi->GetWireForGameObject == nullptr ||
			wireCount <= 0) {
			return wires;
		}

		wires.reserve(static_cast<size_t>(wireCount));

		for (int32_t wireIndex = 0; wireIndex < wireCount; wireIndex++) {
			State wireState{};

			if (runtimeApi->GetWireForGameObject(
				gameObject.GetInstanceId(),
				wireIndex,
				&wireState)) {
				wires.emplace_back(wireState.handle);
			}
		}

		return wires;
	}

	Handle GetHandle() const {
		return wireHandle_;
	}

	bool IsValid() const {
		State wireState{};
		return GetState(wireState);
	}

	bool Destroy() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return wireHandle_ != kInvalidEditorScriptWireHandle && runtimeApi != nullptr &&
			runtimeApi->DestroyWire != nullptr && runtimeApi->DestroyWire(wireHandle_);
	}

	bool SetLength(float maximumLength) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return wireHandle_ != kInvalidEditorScriptWireHandle && runtimeApi != nullptr &&
			runtimeApi->SetWireLengthByHandle != nullptr &&
			runtimeApi->SetWireLengthByHandle(wireHandle_, maximumLength);
	}

	bool SetShrinkSpeed(float shrinkSpeed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return wireHandle_ != kInvalidEditorScriptWireHandle && runtimeApi != nullptr &&
			runtimeApi->SetWireShrinkSpeed != nullptr &&
			runtimeApi->SetWireShrinkSpeed(wireHandle_, shrinkSpeed);
	}

	bool Repair() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return wireHandle_ != kInvalidEditorScriptWireHandle && runtimeApi != nullptr &&
			runtimeApi->RepairWire != nullptr && runtimeApi->RepairWire(wireHandle_);
	}

	bool GetState(State& wireState) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return wireHandle_ != kInvalidEditorScriptWireHandle && runtimeApi != nullptr &&
			runtimeApi->GetWireStateByHandle != nullptr &&
			runtimeApi->GetWireStateByHandle(wireHandle_, &wireState);
	}

private:
	Handle wireHandle_ = kInvalidEditorScriptWireHandle;
};

inline std::vector<Wire> HookPoint::GetWires() const {
	return Wire::GetAll(GameObject{gameObjectId_});
}

//================================================================
// ユーザー C++ Script の共通基底クラス
//================================================================

struct EditorNativeScriptAutoFieldDefinition {
	std::string name;
	std::string displayName;
	EditorScriptFieldValue defaultValue{};
	bool hasRange = false;
	float minValue = 0.0f;
	float maxValue = 0.0f;
	float step = 1.0f;
};

inline std::vector<EditorNativeScriptAutoFieldDefinition>& GetEditorNativeScriptAutoFieldRegistry() {
	static std::vector<EditorNativeScriptAutoFieldDefinition> fieldDefinitions;
	return fieldDefinitions;
}

class EditorNativeScriptAutoFieldRegistrar final {
public:
	EditorNativeScriptAutoFieldRegistrar(
		const char* name,
		const char* displayName,
		int32_t fieldType,
		bool boolValue,
		int32_t intValue,
		float floatValue,
		const EditorScriptVector2& vector2Value,
		const EditorScriptVector3& vector3Value,
		const char* stringValue,
		bool hasRange,
		float minValue,
		float maxValue,
		float step) {
		EditorNativeScriptAutoFieldDefinition definition{};
		definition.name = name != nullptr ? name : "";
		definition.displayName = displayName != nullptr ? displayName : definition.name;
		definition.defaultValue.type = fieldType;
		definition.defaultValue.boolValue = boolValue;
		definition.defaultValue.intValue = intValue;
		definition.defaultValue.floatValue = floatValue;
		definition.defaultValue.vector2Value = vector2Value;
		definition.defaultValue.vector3Value = vector3Value;
		definition.hasRange = hasRange;
		definition.minValue = minValue;
		definition.maxValue = maxValue;
		definition.step = step;
		const char* sourceText = stringValue != nullptr ? stringValue : "";
		size_t characterIndex = 0U;

		while (sourceText[characterIndex] != '\0' &&
			characterIndex + 1U < sizeof(definition.defaultValue.stringValue)) {
			definition.defaultValue.stringValue[characterIndex] = sourceText[characterIndex];
			characterIndex++;
		}

		definition.defaultValue.stringValue[characterIndex] = '\0';

		if (!definition.name.empty()) {
			GetEditorNativeScriptAutoFieldRegistry().push_back(std::move(definition));
		}
	}
};

#define EDITOR_SCRIPT_JOIN_INNER(firstToken, secondToken) firstToken##secondToken
#define EDITOR_SCRIPT_JOIN(firstToken, secondToken) EDITOR_SCRIPT_JOIN_INNER(firstToken, secondToken)
#define EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, fieldType, boolValue, intValue, floatValue, vector2Value, vector3Value, stringValue, hasRange, minValue, maxValue, step) \
	namespace { const EditorNativeScriptAutoFieldRegistrar EDITOR_SCRIPT_JOIN(gEditorScriptAutoField_, __COUNTER__){ \
		#fieldName, displayName, fieldType, boolValue, intValue, floatValue, vector2Value, vector3Value, stringValue, hasRange, minValue, maxValue, step}; }
#define SCRIPT_FIELD_BOOL(fieldName, displayName, defaultValue) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeBool, defaultValue, 0, 0.0f, EditorScriptVector2{}, EditorScriptVector3{}, "", false, 0.0f, 0.0f, 1.0f)
#define SCRIPT_FIELD_INT(fieldName, displayName, defaultValue, minValue, maxValue, step) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeInt32, false, defaultValue, 0.0f, EditorScriptVector2{}, EditorScriptVector3{}, "", true, static_cast<float>(minValue), static_cast<float>(maxValue), static_cast<float>(step))
#define SCRIPT_FIELD_FLOAT(fieldName, displayName, defaultValue, minValue, maxValue, step) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeFloat, false, 0, defaultValue, EditorScriptVector2{}, EditorScriptVector3{}, "", true, minValue, maxValue, step)
#define SCRIPT_FIELD_VECTOR2(fieldName, displayName, defaultX, defaultY, minValue, maxValue, step) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeVector2, false, 0, 0.0f, (EditorScriptVector2{defaultX, defaultY}), EditorScriptVector3{}, "", true, minValue, maxValue, step)
#define SCRIPT_FIELD_VECTOR3(fieldName, displayName, defaultX, defaultY, defaultZ, minValue, maxValue, step) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeVector3, false, 0, 0.0f, EditorScriptVector2{}, (EditorScriptVector3{defaultX, defaultY, defaultZ}), "", true, minValue, maxValue, step)
#define SCRIPT_FIELD_STRING(fieldName, displayName, defaultValue) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeString, false, 0, 0.0f, EditorScriptVector2{}, EditorScriptVector3{}, defaultValue, false, 0.0f, 0.0f, 1.0f)
#define SCRIPT_FIELD_GAMEOBJECT(fieldName, displayName, defaultGameObjectId) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeGameObject, false, defaultGameObjectId, 0.0f, EditorScriptVector2{}, EditorScriptVector3{}, "", false, 0.0f, 0.0f, 1.0f)
#define SCRIPT_FIELD_SCENE(fieldName, displayName, defaultScenePath) \
	EDITOR_SCRIPT_AUTO_FIELD(fieldName, displayName, EditorScriptFieldTypeSceneAsset, false, 0, 0.0f, EditorScriptVector2{}, EditorScriptVector3{}, defaultScenePath, false, 0.0f, 0.0f, 1.0f)

class EditorNativeScript {
public:
	EditorNativeScript() {
		for (const EditorNativeScriptAutoFieldDefinition& definition :
			GetEditorNativeScriptAutoFieldRegistry()) {
			autoFieldValues_[definition.name] = definition.defaultValue;
			const std::string fieldName = definition.name;
			AddField(
				definition.name.c_str(),
				definition.displayName.c_str(),
				definition.defaultValue,
				definition.hasRange,
				definition.minValue,
				definition.maxValue,
				definition.step,
				[this, fieldName](EditorScriptFieldValue& fieldValue) {
					fieldValue = autoFieldValues_[fieldName];
				},
				[this, fieldName](const EditorScriptFieldValue& fieldValue) {
					autoFieldValues_[fieldName] = fieldValue;
				});
		}
	}

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
	// Wireイベント
	//============================================================

	virtual void OnWireConnected(const EditorScriptWireEvent& wireEvent) {
		(void)wireEvent;
	}

	virtual void OnWireTensionChanged(const EditorScriptWireEvent& wireEvent) {
		(void)wireEvent;
	}

	virtual void OnWireBroken(const EditorScriptWireEvent& wireEvent) {
		(void)wireEvent;
	}

	virtual void OnWireTargetLost(const EditorScriptWireEvent& wireEvent) {
		(void)wireEvent;
	}

	virtual void OnWireDestroyed(const EditorScriptWireEvent& wireEvent) {
		(void)wireEvent;
	}

	void DispatchWireEvent(const EditorScriptWireEvent& wireEvent) {
		switch (wireEvent.type) {
		case EditorScriptWireEventTypeConnected:
			OnWireConnected(wireEvent);
			break;
		case EditorScriptWireEventTypeTensionChanged:
			OnWireTensionChanged(wireEvent);
			break;
		case EditorScriptWireEventTypeBroken:
			OnWireBroken(wireEvent);
			break;
		case EditorScriptWireEventTypeTargetLost:
			OnWireTargetLost(wireEvent);
			break;
		case EditorScriptWireEventTypeDestroyed:
			OnWireDestroyed(wireEvent);
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

	bool FieldBool(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeBool);
		return fieldValue != nullptr ? fieldValue->boolValue : false;
	}

	int32_t FieldInt(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeInt32);
		return fieldValue != nullptr ? fieldValue->intValue : 0;
	}

	float FieldFloat(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeFloat);
		return fieldValue != nullptr ? fieldValue->floatValue : 0.0f;
	}

	EditorScriptVector2 FieldVector2(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeVector2);
		return fieldValue != nullptr ? fieldValue->vector2Value : EditorScriptVector2{};
	}

	EditorScriptVector3 FieldVector3(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeVector3);
		return fieldValue != nullptr ? fieldValue->vector3Value : EditorScriptVector3{};
	}

	std::string FieldString(const char* fieldName) const {
		const auto fieldIterator = autoFieldValues_.find(fieldName != nullptr ? fieldName : "");
		return fieldIterator != autoFieldValues_.end()
			? std::string(fieldIterator->second.stringValue)
			: std::string{};
	}

	GameObject FieldGameObject(const char* fieldName) const {
		const EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeGameObject);
		return GameObject{fieldValue != nullptr ? fieldValue->intValue : -1};
	}

	//============================================================
	// SCRIPT_FIELD_* で宣言した Field の書き換え
	//
	// 値は Script インスタンスごとに保持し、Inspector 表示にもそのまま反映される。
	// .h へメンバーを増やさずゲーム中の状態を持てる。型が違う名前へは書き込まず false を返す。
	//============================================================

	bool SetFieldBool(const char* fieldName, bool value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeBool);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->boolValue = value;
		return true;
	}

	bool SetFieldInt(const char* fieldName, int32_t value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeInt32);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->intValue = value;
		return true;
	}

	bool SetFieldFloat(const char* fieldName, float value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeFloat);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->floatValue = value;
		return true;
	}

	bool SetFieldVector2(const char* fieldName, const EditorScriptVector2& value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeVector2);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->vector2Value = value;
		return true;
	}

	bool SetFieldVector3(const char* fieldName, const EditorScriptVector3& value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeVector3);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->vector3Value = value;
		return true;
	}

	bool SetFieldString(const char* fieldName, const std::string& value) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeString);

		if (fieldValue == nullptr) {
			return false;
		}

		CopyText(value.c_str(), fieldValue->stringValue, sizeof(fieldValue->stringValue));
		return true;
	}

	bool SetFieldGameObject(const char* fieldName, const GameObject& gameObject) {
		EditorScriptFieldValue* fieldValue = FindAutoFieldValue(fieldName, EditorScriptFieldTypeGameObject);

		if (fieldValue == nullptr) {
			return false;
		}

		fieldValue->intValue = gameObject.GetInstanceId();
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

	//============================================================
	// Inspector へ出さない可変状態
	//
	// Script インスタンスごとに 1 つ作られる。lambda へコピーキャプチャして使うので、
	// .h へメンバー変数を増やさずに状態を持てる。中身は std::make_shared と同じである。
	// 例: const auto hitCount = MakeState<int32_t>(0);
	//     BindCollisionEnter([hitCount](const EditorScriptPhysicsEvent&) { (*hitCount)++; });
	//============================================================

	template <typename StateType, typename... ArgumentTypes>
	static std::shared_ptr<StateType> MakeState(ArgumentTypes&&... arguments) {
		return std::make_shared<StateType>(std::forward<ArgumentTypes>(arguments)...);
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
	std::unordered_map<std::string, EditorScriptFieldValue> autoFieldValues_;  // .cpp宣言FieldをScriptインスタンスごとに保持する。
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

	const EditorScriptFieldValue* FindAutoFieldValue(const char* fieldName, int32_t fieldType) const {
		const auto fieldIterator = autoFieldValues_.find(fieldName != nullptr ? fieldName : "");

		if (fieldIterator == autoFieldValues_.end() || fieldIterator->second.type != fieldType) {
			return nullptr;
		}

		return &fieldIterator->second;
	}

	EditorScriptFieldValue* FindAutoFieldValue(const char* fieldName, int32_t fieldType) {
		const auto fieldIterator = autoFieldValues_.find(fieldName != nullptr ? fieldName : "");

		if (fieldIterator == autoFieldValues_.end() || fieldIterator->second.type != fieldType) {
			return nullptr;
		}

		return &fieldIterator->second;
	}
};

//================================================================
// ユーザー C++ Script 用の自己参照型ライフサイクル
//
// DLL ABI と GameObject ID の受け渡しは Generated 側が担当する。
// ユーザーは必要なライフサイクルだけ override すればよい。
//================================================================

class Script : public EditorNativeScript {
public:
	using LifecycleCallback = std::function<void()>;
	using UpdateCallback = std::function<void(float)>;
	using PhysicsEventCallback = std::function<void(const EditorScriptPhysicsEvent&)>;
	using WireEventCallback = std::function<void(const EditorScriptWireEvent&)>;
	using AnimationEventCallback = std::function<void(const EditorScriptAnimationEvent&)>;

	// 任意のライフサイクルだけを .cpp のコンストラクタから登録する。
	// 派生クラスの .h へ override 宣言を追加する必要はない。
	void BindStart(LifecycleCallback callback) { startCallback_ = std::move(callback); }
	void BindUpdate(UpdateCallback callback) { updateCallback_ = std::move(callback); }
	void BindFixedUpdate(UpdateCallback callback) { fixedUpdateCallback_ = std::move(callback); }
	void BindStop(LifecycleCallback callback) { stopCallback_ = std::move(callback); }
	void BindCollisionEnter(PhysicsEventCallback callback) { collisionEnterCallback_ = std::move(callback); }
	void BindCollisionStay(PhysicsEventCallback callback) { collisionStayCallback_ = std::move(callback); }
	void BindCollisionExit(PhysicsEventCallback callback) { collisionExitCallback_ = std::move(callback); }
	void BindTriggerEnter(PhysicsEventCallback callback) { triggerEnterCallback_ = std::move(callback); }
	void BindTriggerStay(PhysicsEventCallback callback) { triggerStayCallback_ = std::move(callback); }
	void BindTriggerExit(PhysicsEventCallback callback) { triggerExitCallback_ = std::move(callback); }
	void BindWireConnected(WireEventCallback callback) { wireConnectedCallback_ = std::move(callback); }
	void BindWireTensionChanged(WireEventCallback callback) { wireTensionChangedCallback_ = std::move(callback); }
	void BindWireBroken(WireEventCallback callback) { wireBrokenCallback_ = std::move(callback); }
	void BindWireTargetLost(WireEventCallback callback) { wireTargetLostCallback_ = std::move(callback); }
	void BindWireDestroyed(WireEventCallback callback) { wireDestroyedCallback_ = std::move(callback); }
	void BindAnimationEvent(AnimationEventCallback callback) { animationEventCallback_ = std::move(callback); }

	virtual void Start() {
		if (startCallback_) startCallback_();
	}

	virtual void Update(float deltaTime) {
		if (updateCallback_) updateCallback_(deltaTime);
	}

	virtual void FixedUpdate(float fixedDeltaTime) {
		if (fixedUpdateCallback_) fixedUpdateCallback_(fixedDeltaTime);
	}

	virtual void Stop() {
		if (stopCallback_) stopCallback_();
	}

	void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (collisionEnterCallback_) collisionEnterCallback_(physicsEvent);
	}
	void OnCollisionStay(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (collisionStayCallback_) collisionStayCallback_(physicsEvent);
	}
	void OnCollisionExit(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (collisionExitCallback_) collisionExitCallback_(physicsEvent);
	}
	void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (triggerEnterCallback_) triggerEnterCallback_(physicsEvent);
	}
	void OnTriggerStay(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (triggerStayCallback_) triggerStayCallback_(physicsEvent);
	}
	void OnTriggerExit(const EditorScriptPhysicsEvent& physicsEvent) override {
		if (triggerExitCallback_) triggerExitCallback_(physicsEvent);
	}
	void OnWireConnected(const EditorScriptWireEvent& wireEvent) override {
		if (wireConnectedCallback_) wireConnectedCallback_(wireEvent);
	}
	void OnWireTensionChanged(const EditorScriptWireEvent& wireEvent) override {
		if (wireTensionChangedCallback_) wireTensionChangedCallback_(wireEvent);
	}
	void OnWireBroken(const EditorScriptWireEvent& wireEvent) override {
		if (wireBrokenCallback_) wireBrokenCallback_(wireEvent);
	}
	void OnWireTargetLost(const EditorScriptWireEvent& wireEvent) override {
		if (wireTargetLostCallback_) wireTargetLostCallback_(wireEvent);
	}
	void OnWireDestroyed(const EditorScriptWireEvent& wireEvent) override {
		if (wireDestroyedCallback_) wireDestroyedCallback_(wireEvent);
	}
	void OnAnimationEvent(const EditorScriptAnimationEvent& animationEvent) override {
		if (animationEventCallback_) animationEventCallback_(animationEvent);
	}

	GameObject GetGameObject() const {
		return GameObject{gameObjectId_};
	}

	template <typename ComponentType>
	ComponentType GetComponent() const {
		return ComponentType{gameObjectId_};
	}

	template <typename ComponentType>
	ComponentType AddComponent() const {
		return GetGameObject().AddComponent<ComponentType>();
	}

	template <typename ComponentType>
	ComponentType GetOrAddComponent() const {
		return GetGameObject().GetOrAddComponent<ComponentType>();
	}

	template <typename ComponentType>
	bool RemoveComponent() const {
		return GetGameObject().RemoveComponent<ComponentType>();
	}

	int32_t GetGameObjectId() const {
		return gameObjectId_;
	}

	void AttachToGameObject(int32_t gameObjectId) {
		gameObjectId_ = gameObjectId;
	}

private:
	void Start(int32_t gameObjectId) final {
		AttachToGameObject(gameObjectId);
		Start();
	}

	void Update(int32_t gameObjectId, float deltaTime) final {
		AttachToGameObject(gameObjectId);
		Update(deltaTime);
	}

	void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) final {
		AttachToGameObject(gameObjectId);
		FixedUpdate(fixedDeltaTime);
	}

	void Stop(int32_t gameObjectId) final {
		AttachToGameObject(gameObjectId);
		Stop();
	}

	int32_t gameObjectId_ = -1;
	LifecycleCallback startCallback_;
	UpdateCallback updateCallback_;
	UpdateCallback fixedUpdateCallback_;
	LifecycleCallback stopCallback_;
	PhysicsEventCallback collisionEnterCallback_;
	PhysicsEventCallback collisionStayCallback_;
	PhysicsEventCallback collisionExitCallback_;
	PhysicsEventCallback triggerEnterCallback_;
	PhysicsEventCallback triggerStayCallback_;
	PhysicsEventCallback triggerExitCallback_;
	WireEventCallback wireConnectedCallback_;
	WireEventCallback wireTensionChangedCallback_;
	WireEventCallback wireBrokenCallback_;
	WireEventCallback wireTargetLostCallback_;
	WireEventCallback wireDestroyedCallback_;
	AnimationEventCallback animationEventCallback_;
};

//================================================================
// 外部認識・オンライン連携の高水準 API
//================================================================
// Speech / Vision / Haptics / Online のいずれも、Backend や外部 SDK を
// ゲーム側へ出さず、この型だけで使えるようにしている。
// 機能が使えない環境では「何もせず false / 既定値」を返し、ゲームロジックは止めない。

// 外部機能の共通状態。Editor 表示と同じ意味で使う。
enum class ExternalFeatureStatus : int32_t {
	Unavailable = 0,
	Ready = 1,
	Running = 2,
	Error = 3,
};

// SpeechRecognizerComponent を持つ GameObject の音声認識を操作する。
// 使い方: Speech speech(gameObject); speech.Start(); if (speech.WasKeywordRecognized("Jump")) { ... }
class Speech final {
public:
	explicit Speech(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool Start() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SpeechStartRecognition != nullptr &&
			runtimeApi->SpeechStartRecognition(gameObjectId_);
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SpeechStopRecognition != nullptr &&
			runtimeApi->SpeechStopRecognition(gameObjectId_);
	}

	bool IsRecognizing() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SpeechIsRecognizing != nullptr &&
			runtimeApi->SpeechIsRecognizing(gameObjectId_);
	}

	// マイクが開いているだけの待機状態と、実際の発話中を区別する。
	bool IsSpeaking() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SpeechIsSpeaking != nullptr &&
			runtimeApi->SpeechIsSpeaking(gameObjectId_);
	}

	// Whisperが録音済み音声を文字へ変換している間だけtrueを返す。
	bool IsProcessing() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->SpeechIsProcessing != nullptr &&
			runtimeApi->SpeechIsProcessing(gameObjectId_);
	}

	// UIでそのまま使える既定の状態文字列。独自表示にしたい場合は上のbool APIを使う。
	std::string GetActivityText() const {
		if (IsSpeaking()) return "話し中";
		if (IsProcessing()) return "推論中";
		if (IsRecognizing()) return "認識待機中";
		return "停止中";
	}

	// 直近の確定文字列。まだ何も認識していなければ空文字列を返す。
	std::string GetLastText() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SpeechGetLastResult == nullptr) {
			return std::string();
		}

		char textBuffer[512] = {};
		float confidence = 0.0f;
		bool isFinal = false;

		if (!runtimeApi->SpeechGetLastResult(gameObjectId_, textBuffer, 512, &confidence, &isFinal)) {
			return std::string();
		}

		return std::string(textBuffer);
	}

	float GetLastConfidence() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->SpeechGetLastResult == nullptr) {
			return 0.0f;
		}

		char textBuffer[512] = {};
		float confidence = 0.0f;
		bool isFinal = false;
		runtimeApi->SpeechGetLastResult(gameObjectId_, textBuffer, 512, &confidence, &isFinal);
		return confidence;
	}

	// 発話・推論中は状態を、認識完了後は直近の認識文字列を返す。
	// Whisperは推論完了前の文字列を持たないため、発話中の内容を推測して返さない。
	std::string GetDisplayText() const {
		if (IsSpeaking() || IsProcessing()) {
			return GetActivityText();
		}

		const std::string lastText = GetLastText();
		return lastText.empty() ? GetActivityText() : lastText;
	}

	// Text Componentを持つUI GameObjectへ、現在の状態または認識文字列を直接表示する。
	bool SetUiText(const GameObject& uiGameObject) const {
		return Ui(uiGameObject).SetText(GetDisplayText());
	}

	// このフレームに登録キーワードを認識したか。Input Action と同じ感覚で使える。
	bool WasKeywordRecognized(const char* keyword) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr &&
			runtimeApi->SpeechWasKeywordRecognized != nullptr &&
			runtimeApi->SpeechWasKeywordRecognized(gameObjectId_, keyword);
	}

private:
	int32_t gameObjectId_ = -1;
};

// 画像認識の 1 件分の検出結果。
struct VisionObject {
	std::string label;
	float confidence = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

// CameraInputComponent / ImageRecognizerComponent を操作する。
// 使い方: Vision vision(gameObject); vision.StartRecognition(); if (vision.HasMotion()) { ... }
class Vision final {
public:
	explicit Vision(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	bool StartCamera() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionStartCamera != nullptr &&
			runtimeApi->VisionStartCamera(gameObjectId_);
	}

	bool StopCamera() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionStopCamera != nullptr &&
			runtimeApi->VisionStopCamera(gameObjectId_);
	}

	bool StartRecognition() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionStartRecognition != nullptr &&
			runtimeApi->VisionStartRecognition(gameObjectId_);
	}

	bool StopRecognition() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionStopRecognition != nullptr &&
			runtimeApi->VisionStopRecognition(gameObjectId_);
	}

	ExternalFeatureStatus GetStatus() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetState == nullptr) {
			return ExternalFeatureStatus::Unavailable;
		}

		return static_cast<ExternalFeatureStatus>(runtimeApi->VisionGetState(gameObjectId_));
	}

	int32_t GetObjectCount() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionGetObjectCount != nullptr
			? runtimeApi->VisionGetObjectCount(gameObjectId_)
			: 0;
	}

	bool TryGetObject(int32_t objectIndex, VisionObject& outObject) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetObject == nullptr) {
			return false;
		}

		char labelBuffer[128] = {};

		if (!runtimeApi->VisionGetObject(
				gameObjectId_,
				objectIndex,
				labelBuffer,
				128,
				&outObject.confidence,
				&outObject.x,
				&outObject.y,
				&outObject.width,
				&outObject.height)) {
			return false;
		}

		outObject.label = labelBuffer;
		return true;
	}

	// 指定ラベルの物体が見えているか。
	bool IsObjectDetected(const char* label) const {
		const int32_t objectCount = GetObjectCount();

		for (int32_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
			VisionObject detectedObject{};

			if (!TryGetObject(objectIndex, detectedObject)) {
				continue;
			}

			if (label == nullptr || detectedObject.label == label) {
				return true;
			}
		}

		return false;
	}

	std::string GetTopClassification() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetTopClassification == nullptr) {
			return std::string();
		}

		char labelBuffer[128] = {};
		float confidence = 0.0f;

		if (!runtimeApi->VisionGetTopClassification(gameObjectId_, labelBuffer, 128, &confidence)) {
			return std::string();
		}

		return std::string(labelBuffer);
	}

	int32_t GetFaceCount() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionGetFaceCount != nullptr
			? runtimeApi->VisionGetFaceCount(gameObjectId_)
			: 0;
	}

	bool TryGetFace(int32_t faceIndex, VisionObject& outFace) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetFace == nullptr) {
			return false;
		}

		outFace.label = "face";
		return runtimeApi->VisionGetFace(
			gameObjectId_,
			faceIndex,
			&outFace.confidence,
			&outFace.x,
			&outFace.y,
			&outFace.width,
			&outFace.height);
	}

	// 顔の向き。未対応 Backend では false を返す(勝手に別の値へ置き換えない)。
	bool TryGetHeadPose(float& outYaw, float& outPitch, float& outRoll) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->VisionGetHeadPose != nullptr &&
			runtimeApi->VisionGetHeadPose(gameObjectId_, &outYaw, &outPitch, &outRoll);
	}

	bool HasMotion() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetMotion == nullptr) {
			return false;
		}

		bool hasMotion = false;
		float motionMagnitude = 0.0f;
		float centerX = 0.0f;
		float centerY = 0.0f;

		return runtimeApi->VisionGetMotion(gameObjectId_, &hasMotion, &motionMagnitude, &centerX, &centerY) &&
			hasMotion;
	}

	float GetMotionMagnitude() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetMotion == nullptr) {
			return 0.0f;
		}

		bool hasMotion = false;
		float motionMagnitude = 0.0f;
		float centerX = 0.0f;
		float centerY = 0.0f;
		runtimeApi->VisionGetMotion(gameObjectId_, &hasMotion, &motionMagnitude, &centerX, &centerY);
		return motionMagnitude;
	}

	// 色追跡。検出できていれば中心座標を 0〜1 で返す。
	bool TryGetTrackedColorCenter(float& outCenterX, float& outCenterY) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->VisionGetColorTracking == nullptr) {
			return false;
		}

		bool isDetected = false;
		float areaRatio = 0.0f;

		if (!runtimeApi->VisionGetColorTracking(
				gameObjectId_, &isDetected, &outCenterX, &outCenterY, &areaRatio)) {
			return false;
		}

		return isDetected;
	}

private:
	int32_t gameObjectId_ = -1;
};

// 再生中の振動 1 本を操作する Handle。
class HapticVoice final {
public:
	explicit HapticVoice(uint32_t hapticHandle = 0u)
		: hapticHandle_(hapticHandle) {
	}

	bool IsValid() const {
		return hapticHandle_ != 0u;
	}

	bool IsPlaying() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticIsPlayingHandle != nullptr &&
			runtimeApi->HapticIsPlayingHandle(hapticHandle_);
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticStopHandle != nullptr &&
			runtimeApi->HapticStopHandle(hapticHandle_);
	}

	// 0.0 〜 1.0。再生中でも変えられる。
	bool SetIntensity(float intensity) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticSetHandleIntensity != nullptr &&
			runtimeApi->HapticSetHandleIntensity(hapticHandle_, intensity);
	}

	bool SetFrequency(float frequency) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticSetHandleFrequency != nullptr &&
			runtimeApi->HapticSetHandleFrequency(hapticHandle_, frequency);
	}

	bool SetPlaybackSpeed(float playbackSpeed) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticSetHandlePlaybackSpeed != nullptr &&
			runtimeApi->HapticSetHandlePlaybackSpeed(hapticHandle_, playbackSpeed);
	}

	bool SetLooping(bool isLooping) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return IsValid() && runtimeApi != nullptr && runtimeApi->HapticSetHandleLooping != nullptr &&
			runtimeApi->HapticSetHandleLooping(hapticHandle_, isLooping);
	}

	uint32_t GetHandle() const {
		return hapticHandle_;
	}

private:
	uint32_t hapticHandle_ = 0u;
};

// Device の接続状態。
enum class HapticDeviceStatus : int32_t {
	Unavailable = 0,
	Disconnected = 1,
	Connected = 2,
	Error = 3,
};

// HapticSourceComponent を持つ GameObject の振動を操作する。
// Device が無い場合は無効 Handle を返すだけで、ゲームロジックは止まらない。
// 使い方: HapticVoice voice = Haptic(gameObject).Play(); voice.SetIntensity(0.5f);
class Haptic final {
public:
	explicit Haptic(const GameObject& gameObject)
		: gameObjectId_(gameObject.GetInstanceId()) {
	}

	HapticVoice Play() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->HapticPlaySource == nullptr) {
			return HapticVoice();
		}

		return HapticVoice(runtimeApi->HapticPlaySource(gameObjectId_));
	}

	// 衝突の強さから振動を作る。HapticSource の Physics Reactive が有効な時だけ鳴る。
	HapticVoice PlayFromImpulse(float impulse) const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (gameObjectId_ < 0 || runtimeApi == nullptr || runtimeApi->HapticPlayFromImpulse == nullptr) {
			return HapticVoice();
		}

		return HapticVoice(runtimeApi->HapticPlayFromImpulse(gameObjectId_, impulse));
	}

	bool Stop() const {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return gameObjectId_ >= 0 && runtimeApi != nullptr && runtimeApi->HapticStopSource != nullptr &&
			runtimeApi->HapticStopSource(gameObjectId_);
	}

	// Component を用意せず .haptic Clip を直接鳴らす。
	static HapticVoice PlayClip(const char* clipAssetPath, int32_t ownerGameObjectId = -1) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->HapticPlayClipAsset == nullptr) {
			return HapticVoice();
		}

		return HapticVoice(runtimeApi->HapticPlayClipAsset(clipAssetPath, ownerGameObjectId));
	}

	static void SetMasterIntensity(float masterIntensity) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->HapticSetMasterIntensity != nullptr) {
			runtimeApi->HapticSetMasterIntensity(masterIntensity);
		}
	}

	static HapticDeviceStatus GetDeviceStatus() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->HapticGetDeviceState == nullptr) {
			return HapticDeviceStatus::Unavailable;
		}

		return static_cast<HapticDeviceStatus>(runtimeApi->HapticGetDeviceState());
	}

private:
	int32_t gameObjectId_ = -1;
};

// Leaderboard の 1 行。
struct OnlineLeaderboardEntry {
	std::string playerId;
	std::string playerName;
	int64_t score = 0;
	int32_t rank = 0;
};

// ランキング種類。
enum class OnlineLeaderboardScope : int32_t {
	Global = 0,
	Daily = 1,
	Weekly = 2,
	Season = 3,
	Custom = 4,
};

// 接続状態。
enum class OnlineStatus : int32_t {
	Offline = 0,
	Connecting = 1,
	Online = 2,
	Error = 3,
};

// Cloudflare などのオンライン機能を使う。HTTP の詳細は Engine 側が持つ。
// 取得系は「要求 → 次以降のフレームで参照」で使う(通信で Main Thread を止めないため)。
// 使い方:
//   Online::SubmitScore("Score", 12500);
//   Online::RequestTopScores("Score", 100);
//   ... 数フレーム後 ...
//   for (int32_t i = 0; i < Online::GetLeaderboardCount(); ++i) { ... }
class Online final {
public:
	// Player ID の決め方はゲーム側が選ぶ。ログイン方式でも端末固有 ID でもよい。
	static void SetPlayerIdentity(const char* playerId, const char* playerName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->OnlineSetPlayerIdentity != nullptr) {
			runtimeApi->OnlineSetPlayerIdentity(playerId, playerName);
		}
	}

	static bool IsEnabled() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineIsEnabled != nullptr &&
			runtimeApi->OnlineIsEnabled();
	}

	static OnlineStatus GetStatus() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->OnlineGetConnectionState == nullptr) {
			return OnlineStatus::Offline;
		}

		return static_cast<OnlineStatus>(runtimeApi->OnlineGetConnectionState());
	}

	// 送れなかった送信は Engine 側の再送 Queue に残る。その件数。
	static int32_t GetPendingRequestCount() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineGetPendingRequestCount != nullptr
			? runtimeApi->OnlineGetPendingRequestCount()
			: 0;
	}

	static bool SubmitScore(
		const char* boardName,
		int64_t score,
		OnlineLeaderboardScope scope = OnlineLeaderboardScope::Global) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineSubmitScore != nullptr &&
			runtimeApi->OnlineSubmitScore(boardName, score, static_cast<int32_t>(scope));
	}

	static bool RequestTopScores(
		const char* boardName,
		int32_t entryCount = 100,
		OnlineLeaderboardScope scope = OnlineLeaderboardScope::Global) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineRequestTopScores != nullptr &&
			runtimeApi->OnlineRequestTopScores(boardName, entryCount, static_cast<int32_t>(scope));
	}

	static int32_t GetLeaderboardCount() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineGetLeaderboardCount != nullptr
			? runtimeApi->OnlineGetLeaderboardCount()
			: 0;
	}

	static bool TryGetLeaderboardEntry(int32_t entryIndex, OnlineLeaderboardEntry& outEntry) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->OnlineGetLeaderboardEntry == nullptr) {
			return false;
		}

		char playerIdBuffer[128] = {};
		char playerNameBuffer[128] = {};

		if (!runtimeApi->OnlineGetLeaderboardEntry(
				entryIndex,
				playerIdBuffer,
				128,
				playerNameBuffer,
				128,
				&outEntry.score,
				&outEntry.rank)) {
			return false;
		}

		outEntry.playerId = playerIdBuffer;
		outEntry.playerName = playerNameBuffer;
		return true;
	}

	static bool SetPlayerValue(const char* key, const char* value) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineSetPlayerValue != nullptr &&
			runtimeApi->OnlineSetPlayerValue(key, value);
	}

	static bool RequestPlayerData() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineRequestPlayerData != nullptr &&
			runtimeApi->OnlineRequestPlayerData();
	}

	static std::string GetPlayerValue(const char* key) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->OnlineGetPlayerValue == nullptr) {
			return std::string();
		}

		char valueBuffer[1024] = {};

		if (!runtimeApi->OnlineGetPlayerValue(key, valueBuffer, 1024)) {
			return std::string();
		}

		return std::string(valueBuffer);
	}

	static bool UploadCloudSave(const char* slotName, const char* saveText) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineUploadCloudSave != nullptr &&
			runtimeApi->OnlineUploadCloudSave(slotName, saveText);
	}

	static bool RequestCloudSave(const char* slotName) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();
		return runtimeApi != nullptr && runtimeApi->OnlineRequestCloudSave != nullptr &&
			runtimeApi->OnlineRequestCloudSave(slotName);
	}

	// RequestCloudSave の結果。まだ届いていなければ空文字列。
	static std::string GetCloudSave() {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi == nullptr || runtimeApi->OnlineGetCloudSave == nullptr) {
			return std::string();
		}

		std::string saveText(65536u, '\0');

		if (!runtimeApi->OnlineGetCloudSave(saveText.data(), static_cast<int32_t>(saveText.size()))) {
			return std::string();
		}

		saveText.resize(std::strlen(saveText.c_str()));
		return saveText;
	}
};
