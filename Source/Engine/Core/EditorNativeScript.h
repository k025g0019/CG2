#pragma once

#include "EditorScriptApi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
	Enter = 0x1C,
	LeftControl = 0x1D,
	A = 0x1E,
	D = 0x20,
	W = 0x11,
	S = 0x1F,
	LeftShift = 0x2A,
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
