#pragma once

#include <cstdint>

//================================================================
// DLL Script と Editor 本体が共有する C 互換 API
//================================================================

constexpr uint32_t kEditorScriptApiVersion = 5U;  // DLL 側との互換性確認に使う固定バージョン。

enum EditorScriptPhysicsEventType : int32_t {
	EditorScriptPhysicsEventTypeCollisionEnter = 0,
	EditorScriptPhysicsEventTypeCollisionStay = 1,
	EditorScriptPhysicsEventTypeCollisionExit = 2,
	EditorScriptPhysicsEventTypeTriggerEnter = 3,
	EditorScriptPhysicsEventTypeTriggerStay = 4,
	EditorScriptPhysicsEventTypeTriggerExit = 5,
};

enum EditorScriptKeyCode : int32_t {
	EditorScriptKeyCodeW = 17,
	EditorScriptKeyCodeA = 30,
	EditorScriptKeyCodeS = 31,
	EditorScriptKeyCodeD = 32,
	EditorScriptKeyCodeQ = 16,
	EditorScriptKeyCodeE = 18,
	EditorScriptKeyCodeR = 19,
	EditorScriptKeyCodeF = 33,
	EditorScriptKeyCodeSpace = 57,
	EditorScriptKeyCodeLeftShift = 42,
	EditorScriptKeyCodeLeftCtrl = 29,
	EditorScriptKeyCodeUp = 200,
	EditorScriptKeyCodeDown = 208,
	EditorScriptKeyCodeLeft = 203,
	EditorScriptKeyCodeRight = 205,
};

enum EditorScriptAiSensorKind : int32_t {
	EditorScriptAiSensorKindVision = 0,
	EditorScriptAiSensorKindObjectDetection = 1,
	EditorScriptAiSensorKindColorTracking = 2,
	EditorScriptAiSensorKindMotionDetection = 3,
	EditorScriptAiSensorKindWhisperSpeech = 4,
	EditorScriptAiSensorKindVoiceCommand = 5,
};

namespace Key {
	constexpr int32_t W = EditorScriptKeyCodeW;  // W
	constexpr int32_t A = EditorScriptKeyCodeA;  // A
	constexpr int32_t S = EditorScriptKeyCodeS;  // S
	constexpr int32_t D = EditorScriptKeyCodeD;  // D
	constexpr int32_t Q = EditorScriptKeyCodeQ;  // Q
	constexpr int32_t E = EditorScriptKeyCodeE;  // E
	constexpr int32_t R = EditorScriptKeyCodeR;  // R
	constexpr int32_t F = EditorScriptKeyCodeF;  // F
	constexpr int32_t Space = EditorScriptKeyCodeSpace;  // Space
	constexpr int32_t LeftShift = EditorScriptKeyCodeLeftShift;  // Left Shift
	constexpr int32_t LeftCtrl = EditorScriptKeyCodeLeftCtrl;  // Left Ctrl
	constexpr int32_t Up = EditorScriptKeyCodeUp;  // Up
	constexpr int32_t Down = EditorScriptKeyCodeDown;  // Down
	constexpr int32_t Left = EditorScriptKeyCodeLeft;  // Left
	constexpr int32_t Right = EditorScriptKeyCodeRight;  // Right
}

struct EditorScriptVector2 {
	float x;
	float y;
};

struct EditorScriptVector3 {
	float x;
	float y;
	float z;
};

struct EditorScriptTransform {
	EditorScriptVector3 position;
	EditorScriptVector3 rotation;
	EditorScriptVector3 scale;
};

//================================================================
// Inspector 公開変数と Input Action 通知
//================================================================

enum EditorScriptFieldType : int32_t {
	EditorScriptFieldTypeBool = 0,
	EditorScriptFieldTypeInt32 = 1,
	EditorScriptFieldTypeFloat = 2,
	EditorScriptFieldTypeVector2 = 3,
	EditorScriptFieldTypeVector3 = 4,
	EditorScriptFieldTypeString = 5,
};

enum EditorScriptInputPhase : int32_t {
	EditorScriptInputPhaseStarted = 0,
	EditorScriptInputPhasePerformed = 1,
	EditorScriptInputPhaseCanceled = 2,
};

enum EditorScriptInputValueType : int32_t {
	EditorScriptInputValueTypeButton = 0,
	EditorScriptInputValueTypeVector2 = 1,
};

struct EditorScriptFieldValue {
	int32_t type;
	bool boolValue;
	uint8_t reservedPadding[3];
	int32_t intValue;
	float floatValue;
	EditorScriptVector2 vector2Value;
	EditorScriptVector3 vector3Value;
	char stringValue[256];
};

struct EditorScriptFieldDescriptor {
	char name[64];
	char displayName[64];
	EditorScriptFieldValue defaultValue;
	float minValue;
	float maxValue;
	float step;
	bool hasRange;
	uint8_t reservedPadding[3];
};

struct EditorScriptInputActionContext {
	int32_t gameObjectId;
	int32_t phase;
	int32_t valueType;
	float buttonValue;
	EditorScriptVector2 vector2Value;
	char actionMapName[64];
	char actionName[64];
	char bindingPath[128];
};

struct EditorScriptPhysicsEvent {
	int32_t type;
	int32_t selfGameObjectId;
	int32_t otherGameObjectId;
	EditorScriptVector3 point;
	EditorScriptVector3 normal;
	EditorScriptVector3 relativeVelocity;
	float separation;
	bool isTrigger;
	uint8_t reservedPadding[3];
};

struct EditorScriptAiSensorState {
	bool hasComponent;
	bool isActive;
	bool isDetected;
	bool hasDetails;
	int32_t connectedGameObjectId;
	int32_t detectedGameObjectId;
	int32_t commandId;
	float range;
	float angleDegrees;
	float confidence;
	float distance;
	EditorScriptVector3 direction;
	EditorScriptVector2 screenPosition;
	EditorScriptVector2 boundsPosition;
	EditorScriptVector2 boundsSize;
	EditorScriptVector2 motion;
	float motionMagnitude;
	char label[64];
	char text[256];
	char command[64];
};

#pragma warning(push)
#pragma warning(disable : 4820)
struct EditorScriptMaterialState {
	bool hasComponent;
	bool hasTexture;
	bool hasUvLayoutTexture;
	bool useLighting;
	bool reservedPadding[3];
	float intensity;
	float metallic;
	float roughness;
	float ior;
	float alpha;
	float reflectionStrength;
	EditorScriptVector3 color;
	char rendererAssetPath[260];
	char materialName[64];
	char texturePath[260];
	char uvLayoutTexturePath[260];
};
#pragma warning(pop)

struct EditorScriptAnimationState {
	bool hasComponent;
	bool isPlaying;
	bool isLoop;
	bool playOnAwake;
	int32_t animationType;
	int32_t clipCount;
	float animationSpeed;
	float animationAmplitude;
	float currentTime;
	float currentClipDuration;
	char assetPath[260];
	char currentClipName[64];
};

struct EditorScriptRuntimeApi {
	uint32_t apiVersion;
	uint32_t reservedPadding;
	void (*Log)(const char* message);
	bool (*IsKeyDown)(int32_t keyCode);
	bool (*IsKeyPressed)(int32_t keyCode);
	EditorScriptVector2 (*GetActionVector2)(int32_t gameObjectId, const char* actionMapName, const char* actionName);
	bool (*IsActionPressed)(int32_t gameObjectId, const char* actionMapName, const char* actionName);
	bool (*WasActionJustPressed)(int32_t gameObjectId, const char* actionMapName, const char* actionName);
	EditorScriptVector2 (*GetMousePosition)();
	EditorScriptTransform (*GetTransform)(int32_t gameObjectId);
	void (*SetTransform)(int32_t gameObjectId, const EditorScriptTransform* transform);
	EditorScriptVector3 (*GetVelocity)(int32_t gameObjectId);
	void (*SetVelocity)(int32_t gameObjectId, const EditorScriptVector3* velocity);
	EditorScriptVector3 (*GetAngularVelocity)(int32_t gameObjectId);
	void (*SetAngularVelocity)(int32_t gameObjectId, const EditorScriptVector3* angularVelocity);
	bool (*AddForce)(int32_t gameObjectId, const EditorScriptVector3* force);
	bool (*AddImpulse)(int32_t gameObjectId, const EditorScriptVector3* impulse);
	bool (*AddTorque)(int32_t gameObjectId, const EditorScriptVector3* torque);
	EditorScriptAiSensorState (*GetAiSensorState)(int32_t gameObjectId, int32_t sensorKind);
	EditorScriptMaterialState (*GetMaterialState)(int32_t gameObjectId);
	EditorScriptAnimationState (*GetAnimationState)(int32_t gameObjectId);
};

extern "C" {
	typedef bool(__cdecl* EditorScriptLoadFn)(uint32_t apiVersion, const EditorScriptRuntimeApi* runtimeApi);
	typedef void(__cdecl* EditorScriptUnloadFn)();
	typedef void(__cdecl* EditorScriptStartFn)(int32_t gameObjectId);
	typedef void(__cdecl* EditorScriptUpdateFn)(int32_t gameObjectId, float deltaTime);
	typedef void(__cdecl* EditorScriptFixedUpdateFn)(int32_t gameObjectId, float fixedDeltaTime);
	typedef void(__cdecl* EditorScriptPhysicsEventFn)(int32_t gameObjectId, const EditorScriptPhysicsEvent* physicsEvent);
	typedef void(__cdecl* EditorScriptStopFn)(int32_t gameObjectId);
	typedef int32_t(__cdecl* EditorScriptGetFieldCountFn)();
	typedef bool(__cdecl* EditorScriptGetFieldDescriptorFn)(int32_t fieldIndex, EditorScriptFieldDescriptor* fieldDescriptor);
	typedef bool(__cdecl* EditorScriptGetFieldValueFn)(int32_t gameObjectId, const char* fieldName, EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptSetFieldValueFn)(int32_t gameObjectId, const char* fieldName, const EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptInvokeActionFn)(int32_t gameObjectId, const char* functionName, const EditorScriptInputActionContext* inputContext);
}
