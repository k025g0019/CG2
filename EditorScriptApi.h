#pragma once

#include <cstdint>

//================================================================
// DLL Script と Editor 本体が共有する C 互換 API
//================================================================

constexpr uint32_t kEditorScriptApiVersion = 1U;  // DLL 側との互換性確認に使う固定バージョン。

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
	bool (*AddForce)(int32_t gameObjectId, const EditorScriptVector3* force);
	bool (*AddImpulse)(int32_t gameObjectId, const EditorScriptVector3* impulse);
};

extern "C" {
	typedef bool(__cdecl* EditorScriptLoadFn)(uint32_t apiVersion, const EditorScriptRuntimeApi* runtimeApi);
	typedef void(__cdecl* EditorScriptUnloadFn)();
	typedef void(__cdecl* EditorScriptStartFn)(int32_t gameObjectId);
	typedef void(__cdecl* EditorScriptUpdateFn)(int32_t gameObjectId, float deltaTime);
	typedef void(__cdecl* EditorScriptFixedUpdateFn)(int32_t gameObjectId, float fixedDeltaTime);
	typedef void(__cdecl* EditorScriptPhysicsEventFn)(int32_t gameObjectId, const EditorScriptPhysicsEvent* physicsEvent);
	typedef void(__cdecl* EditorScriptStopFn)(int32_t gameObjectId);
}
