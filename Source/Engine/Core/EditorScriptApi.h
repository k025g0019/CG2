#pragma once

#include <cstdint>

//================================================================
// DLL Script と Editor 本体が共有する C 互換 API
//================================================================

constexpr uint32_t kEditorScriptApiVersion = 8U;  // 末尾拡張で旧DLL互換を維持する。

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
	EditorScriptFieldTypeGameObject = 6,
	EditorScriptFieldTypeSceneAsset = 7,
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

enum EditorScriptActionPayloadType : int32_t {
	EditorScriptActionPayloadTypeNone = 0,
	EditorScriptActionPayloadTypeGameObject = 1,
	EditorScriptActionPayloadTypeInt = 2,
	EditorScriptActionPayloadTypeFloat = 3,
	EditorScriptActionPayloadTypeBool = 4,
	EditorScriptActionPayloadTypeVector3 = 5,
	EditorScriptActionPayloadTypeString = 6,
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
	int32_t payloadType;  // EditorScriptActionPayloadType
	int32_t payloadGameObjectId;
	int32_t payloadInt;
	float payloadFloat;
	bool payloadBool;
	uint8_t payloadPadding[3];
	EditorScriptVector3 payloadVector3;
	char payloadString[256];
};

struct EditorScriptActionPayload {
	int32_t type;
	int32_t gameObjectId;
	int32_t intValue;
	float floatValue;
	bool boolValue;
	uint8_t reservedPadding[3];
	EditorScriptVector3 vector3Value;
	char stringValue[256];
};

struct EditorScriptPhysicsEvent {
	int32_t type;
	int32_t selfGameObjectId;
	int32_t otherGameObjectId;
	EditorScriptVector3 point;
	EditorScriptVector3 normal;
	EditorScriptVector3 relativeVelocity;
	float separation;
	float contactImpulse;  // 接触法線方向の推定Impulse。衝突Damageの強度判定に使う。
	float selfMass;  // self側Bodyの実質量。Static / Kinematicは0。
	float otherMass;  // other側Bodyの実質量。Static / Kinematicは0。
	bool isTrigger;
	uint8_t reservedPadding[3];
};

struct EditorScriptRay {
	EditorScriptVector3 origin;
	EditorScriptVector3 direction;
};

struct EditorScriptPhysicsHit {
	int32_t gameObjectId;
	EditorScriptVector3 point;
	EditorScriptVector3 normal;
	float distance;
	bool isTrigger;
	uint8_t reservedPadding[3];
};

struct EditorScriptOceanSurfaceHit {
	int32_t oceanGameObjectId;
	EditorScriptVector3 point;
	EditorScriptVector3 normal;
	EditorScriptVector3 velocity;
	float signedDistance;
};

struct EditorScriptOceanSegmentHit {
	int32_t oceanGameObjectId;
	EditorScriptVector3 point;
	EditorScriptVector3 normal;
	EditorScriptVector3 surfaceVelocity;
	float distance;
	float normalizedDistance;
};

struct EditorScriptOceanOcclusion {
	bool blocked;
	uint8_t reservedPadding[3];
	float minimumClearance;
	float maximumSurfaceHeight;
	EditorScriptOceanSegmentHit intersection;
};

struct EditorScriptWaterSurfaceState {
	int32_t state;  // 0=Above、1=Entering、2=Underwater、3=Leaving
	float signedDistance;
	int32_t oceanGameObjectId;
	EditorScriptVector3 surfacePosition;
	EditorScriptVector3 surfaceNormal;
	EditorScriptVector3 surfaceVelocity;
};

struct EditorScriptOceanProbeSample {
	bool valid;
	uint8_t reservedPadding[3];
	float distance;
	EditorScriptVector3 position;
	EditorScriptVector3 normal;
	EditorScriptVector3 velocity;
	float relativeHeight;
};

//============================================================
// Damage Context
//============================================================

struct EditorScriptDamageContext {
	int32_t targetGameObjectId = -1;  // DamageReceiver / Healthを持つ被弾対象
	int32_t sourceGameObjectId = -1;  // ProjectileやWeaponなど、直接Damageを発生させたObject
	int32_t instigatorGameObjectId = -1;  // 発射者や攻撃者など、Damageの責任主体
	EditorScriptVector3 hitPosition{};  // World空間の命中位置
	EditorScriptVector3 hitNormal{0.0f, 1.0f, 0.0f};  // World空間の命中面法線
	EditorScriptVector3 impulse{};  // 対象Rigidbodyへ加える瞬間力
	float baseDamage = 0.0f;  // DamageReceiver倍率を適用する前の値
	float appliedDamage = 0.0f;  // Runtimeが実際にHealthから減らした値
	int32_t userTag = 0;  // ゲーム側が任意用途へ使う識別値。固定Enumの意味は持たない
};

struct EditorScriptThreatInfo {
	int32_t projectileGameObjectId = -1;  // 接近中のProjectile実体
	int32_t sourceGameObjectId = -1;  // Projectileを発射したGameObject
	float distance = 0.0f;  // Tracker対象までのWorld距離
	float closingSpeed = 0.0f;  // 対象へ近づく相対速度
	float estimatedArrivalSeconds = 0.0f;  // 最接近までの予測秒数
};

struct EditorScriptBallisticPrediction {
	bool valid;
	uint8_t reservedPadding[3];
	EditorScriptVector3 launchDirection;
	EditorScriptVector3 impactPosition;
	float flightTime;
	int32_t trajectoryPointCount;
	EditorScriptVector3 launchVelocity;  // 発射元速度を含む初期World速度
	EditorScriptVector3 sourceVelocity;  // 発射母体から継承した作用点速度
};

struct EditorScriptFireLineState {
	bool isClear;
	uint8_t reservedPadding[3];
	int32_t blockingGameObjectId;
	float blockingDistance;
};

struct EditorScriptStatusEffectEntry {
	char effectId[64];
	int32_t sourceGameObjectId;
	float remainingSeconds;
	float tickRemainingSeconds;
	int32_t stackCount;
};

struct EditorScriptDamageEvent {
	int32_t sourceGameObjectId;
	EditorScriptVector3 worldDirection;
	float damage;
	int32_t damageTagId;
	float remainingSeconds;
};

#pragma warning(push)
#pragma warning(disable : 4820)
struct EditorScriptRailState {
	bool hasComponent;
	bool isReady;
	bool isPaused;
	bool isReversed;
	bool endReached;
	uint8_t reservedPadding[3];
	float normalizedProgress;
	float traveledDistance;
	float totalDistance;
	float currentSpeed;
	float targetSpeed;
	EditorScriptVector2 offset;
};
#pragma warning(pop)

struct EditorScriptRailFrame {
	EditorScriptVector3 position;
	EditorScriptVector3 forward;
	EditorScriptVector3 right;
	EditorScriptVector3 up;
};

//================================================================
// Animation Graph から C++ Script へ渡す Event
//================================================================

struct EditorScriptAnimationEvent {
	const char* name;  // .animgraph の Event に設定した任意のイベント名。
	const char* effectAssetPath;  // Event と同時に再生する .effect。未設定なら空文字列。
	float time;  // Clip 先頭からイベント位置までの秒数。
	EditorScriptVector3 localOffset;  // GameObject 基準で Effect を発生させるローカル位置。
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

#pragma warning(push)
#pragma warning(disable : 4820)
struct EditorScriptRopeState {
	bool hasComponent;
	bool isActive;
	bool isBroken;
	bool reservedPadding;
	int32_t targetGameObjectId;
	float maximumLength;
	float currentLength;
	float currentTension;
};
#pragma warning(pop)

//============================================================
// 複数接続対応 Wire Runtime
//============================================================

using EditorScriptWireHandle = uint64_t;
constexpr EditorScriptWireHandle kInvalidEditorScriptWireHandle = 0ULL;

enum EditorScriptWireEventType : int32_t {
	EditorScriptWireEventTypeConnected = 0,
	EditorScriptWireEventTypeTensionChanged = 1,
	EditorScriptWireEventTypeBroken = 2,
	EditorScriptWireEventTypeTargetLost = 3,
	EditorScriptWireEventTypeDestroyed = 4,
};

struct EditorScriptWireDesc {
	int32_t firstGameObjectId = -1;
	int32_t secondGameObjectId = -1;
	int32_t ownerGameObjectId = -1;  // Wireを作ったPlayer等。通知と一括破棄に使う。
	int32_t rendererSettingsGameObjectId = -1;  // WireRenderer設定を読むObject。-1なら既定値。
	EditorScriptVector3 firstLocalAnchor = {0.0f, 0.0f, 0.0f};
	EditorScriptVector3 secondLocalAnchor = {0.0f, 0.0f, 0.0f};
	float maximumLength = 1.0f;
	float minimumLength = 0.1f;
	float stiffness = 1200.0f;
	float damping = 80.0f;
	float maximumTension = 0.0f;
	float breakingTension = 0.0f;
	float shrinkSpeed = 0.0f;
	bool applyReaction = true;
	bool requireConnectable = true;
	uint8_t reservedPadding[2] = {0U, 0U};
};

struct EditorScriptWireState {
	EditorScriptWireHandle handle = kInvalidEditorScriptWireHandle;
	int32_t firstGameObjectId = -1;
	int32_t secondGameObjectId = -1;
	int32_t ownerGameObjectId = -1;
	bool isActive = false;
	bool isBroken = false;
	uint8_t reservedPadding[2] = {0U, 0U};
	EditorScriptVector3 firstWorldAnchor{};
	EditorScriptVector3 secondWorldAnchor{};
	float maximumLength = 0.0f;
	float minimumLength = 0.0f;
	float currentLength = 0.0f;
	float currentTension = 0.0f;
};

struct EditorScriptWireEvent {
	int32_t type = EditorScriptWireEventTypeConnected;
	EditorScriptWireHandle handle = kInvalidEditorScriptWireHandle;
	int32_t firstGameObjectId = -1;
	int32_t secondGameObjectId = -1;
	int32_t ownerGameObjectId = -1;
	float tension = 0.0f;
};

using EditorScriptJointHandle = uint64_t;
constexpr EditorScriptJointHandle kInvalidEditorScriptJointHandle = 0ULL;

enum class EditorScriptJointType : int32_t {
	Fixed = 0,
	Hinge = 1,
	Spring = 2,
	Configurable = 3,
	Character = 4
};

//============================================================
// Runtime SpringJoint 設定
//============================================================

struct EditorScriptSpringJointDesc {
	EditorScriptVector3 ownerAnchor = {0.0f, 0.0f, 0.0f};  // owner のローカルアンカー
	EditorScriptVector3 connectedAnchor = {0.0f, 0.0f, 0.0f};  // connected のローカルアンカー
	float minDistance = 0.0f;  // これより縮まない距離
	float maxDistance = 1.0f;  // これより伸びない距離
	float frequency = 5.0f;  // ばね周波数
	float damping = 0.7f;  // ばね減衰
};

struct EditorScriptJointDesc {
	EditorScriptVector3 ownerAnchor = {0.0f, 0.0f, 0.0f};  // ownerのローカルアンカー
	EditorScriptVector3 connectedAnchor = {0.0f, 0.0f, 0.0f};  // connectedのローカルアンカー
	EditorScriptVector3 axis = {1.0f, 0.0f, 0.0f};  // Hinge / Character / Configurableの基準軸
	float minDistance = 0.0f;
	float maxDistance = 1.0f;
	float minAngle = -3.1415926f;
	float maxAngle = 3.1415926f;
	float frequency = 5.0f;
	float damping = 0.7f;
	bool freezePositionX = false;
	bool freezePositionY = false;
	bool freezePositionZ = false;
	bool freezeRotationX = false;
	bool freezeRotationY = false;
	bool freezeRotationZ = false;
	uint8_t reservedPadding[2] = {0U, 0U};
};

#pragma warning(push)
#pragma warning(disable : 4820)
struct EditorScriptTurretAimState {
	int32_t targetGameObjectId;
	bool canReachTarget;
	bool isAimed;
	uint8_t reservedPadding[2];
	float yawErrorDegrees;
	float pitchErrorDegrees;
};
#pragma warning(pop)

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
	bool (*SetAnimatorFloat)(int32_t gameObjectId, const char* parameterName, float value);
	bool (*SetAnimatorInt)(int32_t gameObjectId, const char* parameterName, int32_t value);
	bool (*SetAnimatorBool)(int32_t gameObjectId, const char* parameterName, bool value);
	bool (*SetAnimatorTrigger)(int32_t gameObjectId, const char* parameterName);
	bool (*SetAnimatorVector2)(int32_t gameObjectId, const char* parameterName, const EditorScriptVector2* value);
	bool (*SetAnimatorVector3)(int32_t gameObjectId, const char* parameterName, const EditorScriptVector3* value);
	bool (*PlayAnimationAction)(
		int32_t gameObjectId,
		int32_t clipIndex,
		float blendIn,
		float blendOut,
		float playbackSpeed,
		int32_t priority,
		bool loop);
	bool (*PlayEffect)(int32_t gameObjectId);
	bool (*PlayEffectAt)(int32_t gameObjectId, const char* effectAssetPath, const EditorScriptVector3* localOffset);
	void (*StopEffect)(int32_t gameObjectId);
	// AudioSource を Script から任意のタイミングで鳴らす。Effect 系と同じ設計。
	bool (*PlayAudio)(int32_t gameObjectId);
	void (*StopAudio)(int32_t gameObjectId);
	// Bus は 0=SFX / 1=BGM / 2=Ambience / 3=UI。BGM ダッキング等に使う。
	void (*SetAudioBusVolume)(int32_t audioBus, float volume);
	float (*GetAudioBusVolume)(int32_t audioBus);
	void (*SetAudioMasterVolume)(float volume);
	float (*GetAudioMasterVolume)();
	int32_t (*GetAliveParticleCount)(int32_t gameObjectId);
	bool (*GetAnimatorFloat)(int32_t gameObjectId, const char* parameterName, float* value);
	bool (*GetAnimatorInt)(int32_t gameObjectId, const char* parameterName, int32_t* value);
	bool (*GetAnimatorBool)(int32_t gameObjectId, const char* parameterName, bool* value);
	bool (*GetAnimatorVector2)(int32_t gameObjectId, const char* parameterName, EditorScriptVector2* value);
	bool (*GetAnimatorVector3)(int32_t gameObjectId, const char* parameterName, EditorScriptVector3* value);
	bool (*ResetAnimatorTrigger)(int32_t gameObjectId, const char* parameterName);
	bool (*PlayAnimation)(int32_t gameObjectId);
	bool (*StopAnimation)(int32_t gameObjectId);
	bool (*IsAnimationPlaying)(int32_t gameObjectId);
	float (*GetAnimationTime)(int32_t gameObjectId);
	bool (*SetAnimationTime)(int32_t gameObjectId, float playbackTime);
	bool (*SetAnimationSpeed)(int32_t gameObjectId, float playbackSpeed);
	bool (*GetAnimatorStateName)(int32_t gameObjectId, char* stateName, int32_t stateNameCapacity);
	bool (*IsEffectPlaying)(int32_t gameObjectId);
	int32_t (*FindGameObjectByName)(const char* gameObjectName);
	bool (*SetGameObjectActive)(int32_t gameObjectId, bool isActive);
	bool (*IsGameObjectActive)(int32_t gameObjectId);
	bool (*LoadScene)(const char* scenePath);
	bool (*LoadSceneByBuildIndex)(int32_t sceneIndex);
	bool (*SetRailPaused)(int32_t gameObjectId, bool isPaused);
	bool (*IsRailPaused)(int32_t gameObjectId);
	bool (*SetRailSpeed)(int32_t gameObjectId, float speed);
	bool (*SetRailReverse)(int32_t gameObjectId, bool isReversed);
	bool (*SetRailNormalizedProgress)(int32_t gameObjectId, float normalizedProgress);
	bool (*SetRailPath)(int32_t gameObjectId, int32_t railPathGameObjectId, bool preservesProgress);
	bool (*GetRailNormalizedProgress)(int32_t gameObjectId, float* normalizedProgress);
	bool (*GetRailLength)(int32_t gameObjectId, float* railLength);
	bool (*GetRailPosition)(int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* position);
	bool (*GetRailDirection)(int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* direction);
	bool (*ConsumeRailEndReached)(int32_t gameObjectId);
	bool (*ViewportPointToRay)(const EditorScriptVector2* normalizedPosition, EditorScriptRay* ray);
	bool (*GetAimRay)(int32_t screenAimGameObjectId, EditorScriptRay* ray);
	bool (*PhysicsRaycast)(const EditorScriptRay* ray, float distance, EditorScriptPhysicsHit* hit);
	bool (*PhysicsSphereCast)(const EditorScriptRay* ray, float radius, float distance, EditorScriptPhysicsHit* hit);
	bool (*PhysicsCapsuleCast)(const EditorScriptRay* ray, float radius, float height, float distance, EditorScriptPhysicsHit* hit);
	bool (*ApplyDamage)(int32_t targetGameObjectId, float damage, int32_t sourceGameObjectId);
	bool (*GetHealth)(int32_t gameObjectId, float* currentHealth, float* maximumHealth);
	bool (*SetHealth)(int32_t gameObjectId, float currentHealth);
	int32_t (*SpawnFromPool)(int32_t poolGameObjectId, const EditorScriptVector3* position, const EditorScriptVector3* rotation);
	int32_t (*SpawnFromSpawner)(int32_t spawnerGameObjectId);
	bool (*ReleaseToPool)(int32_t gameObjectId);
	bool (*FireHitscan)(int32_t weaponGameObjectId);
	bool (*FireProjectile)(int32_t emitterGameObjectId);
	bool (*PlayCameraBlend)(int32_t componentOwnerGameObjectId);
	bool (*PlayCameraShake)(int32_t componentOwnerGameObjectId);
	bool (*TriggerRailBranch)(int32_t componentOwnerGameObjectId);
	bool (*LoadSceneAsync)(const char* scenePath, bool isAdditive);
	bool (*UnloadScene)(const char* scenePath);
	float (*GetSceneLoadProgress)();
	bool (*IsSceneLoading)();
	bool (*IsSceneLoaded)(const char* scenePath);
	void (*SetSceneFloat)(const char* key, float value);
	bool (*GetSceneFloat)(const char* key, float* value);
	void (*SetSceneString)(const char* key, const char* value);
	bool (*GetSceneString)(const char* key, char* value, int32_t valueCapacity);
	bool (*PlayActionSequence)(int32_t sequenceGameObjectId);
	bool (*PauseActionSequence)(int32_t sequenceGameObjectId, bool isPaused);
	bool (*StopActionSequence)(int32_t sequenceGameObjectId);
	bool (*SignalActionSequence)(int32_t sequenceGameObjectId, const char* signalName);
	bool (*IsActionSequencePlaying)(int32_t sequenceGameObjectId);
	bool (*SaveSlot)(const char* slotName);
	bool (*LoadSlot)(const char* slotName);
	bool (*DeleteSlot)(const char* slotName);
	bool (*HasSlot)(const char* slotName);
	bool (*ActivateCheckpoint)(int32_t checkpointGameObjectId, bool shouldLoad);
	void (*SetSaveFloat)(const char* key, float value);
	bool (*GetSaveFloat)(const char* key, float* value);
	void (*SetSaveString)(const char* key, const char* value);
	bool (*GetSaveString)(const char* key, char* value, int32_t valueCapacity);
	bool (*SampleOceanSurface)(
		int32_t queryGameObjectId,
		const EditorScriptVector3* worldPosition,
		EditorScriptOceanSurfaceHit* hit);
	bool (*SetComponentActive)(int32_t gameObjectId, const char* componentTypeName, bool isActive);
	bool (*IsComponentActive)(int32_t gameObjectId, const char* componentTypeName);
	bool (*AddForceAtPosition)(int32_t gameObjectId, const EditorScriptVector3* force, const EditorScriptVector3* worldPosition);
	int32_t (*AddExplosionImpulse)(const EditorScriptVector3* center, float radius, float impulseStrength, float upwardModifier);
	bool (*AttachRope)(int32_t ownerGameObjectId, int32_t targetGameObjectId, const EditorScriptVector3* ownerLocalAnchor, const EditorScriptVector3* targetAnchor, float maximumLength);
	bool (*DetachRope)(int32_t ownerGameObjectId);
	bool (*SetRopeLength)(int32_t ownerGameObjectId, float maximumLength);
	bool (*RepairRope)(int32_t ownerGameObjectId);
	EditorScriptRopeState (*GetRopeState)(int32_t ownerGameObjectId);
	// ABI 互換のため、新しい RailFollower API は RuntimeApi の末尾へ追加する。
	bool (*SetRailMoveInput)(int32_t gameObjectId, const EditorScriptVector2* moveInput);
	bool (*SetRailOffset)(int32_t gameObjectId, const EditorScriptVector2* offset);
	bool (*GetRailOffset)(int32_t gameObjectId, EditorScriptVector2* offset);
	// ABI互換のため、再利用Gameplay APIも必ず末尾へ追加する。
	bool (*LoadoutSelectSlot)(int32_t gameObjectId, int32_t slotIndex);
	bool (*LoadoutSelectNext)(int32_t gameObjectId);
	bool (*LoadoutSelectPrevious)(int32_t gameObjectId);
	bool (*LoadoutFire)(int32_t gameObjectId);
	bool (*LoadoutReload)(int32_t gameObjectId);
	bool (*LoadoutGetAmmo)(int32_t gameObjectId, int32_t* currentAmmo, int32_t* reserveAmmo);
	bool (*GetCurrentTarget)(int32_t gameObjectId, int32_t* targetGameObjectId);
	bool (*SetExplicitTarget)(int32_t gameObjectId, int32_t targetGameObjectId);
	bool (*SetRuntimeFloat)(int32_t gameObjectId, const char* componentName, const char* propertyName, float value);
	bool (*GetRuntimeFloat)(int32_t gameObjectId, const char* componentName, const char* propertyName, float* value);
	bool (*SetRuntimeInt)(int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t value);
	bool (*GetRuntimeInt)(int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t* value);
	bool (*SetRuntimeBool)(int32_t gameObjectId, const char* componentName, const char* propertyName, bool value);
	bool (*GetRuntimeBool)(int32_t gameObjectId, const char* componentName, const char* propertyName, bool* value);
	bool (*SetRuntimeVector3)(int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector3* value);
	bool (*GetRuntimeVector3)(int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector3* value);
	bool (*PlayPropertyTween)(int32_t gameObjectId);
	bool (*StopPropertyTween)(int32_t gameObjectId);
	bool (*IsPropertyTweenPlaying)(int32_t gameObjectId);
	bool (*RelayAction)(int32_t gameObjectId);
	// ABI互換のため、Component連携とRail照会APIは必ず構造体末尾へ追加する。
	bool (*HasComponent)(int32_t gameObjectId, const char* componentTypeName);
	bool (*InvokeScriptAction)(int32_t gameObjectId, const char* functionName);
	bool (*SetRuntimeVector2)(int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector2* value);
	bool (*GetRuntimeVector2)(int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector2* value);
	bool (*GetRailState)(int32_t gameObjectId, EditorScriptRailState* state);
	bool (*SetRailDistance)(int32_t gameObjectId, float distance);
	bool (*GetRailClosestProgress)(int32_t gameObjectId, const EditorScriptVector3* worldPosition, float* normalizedProgress);
	bool (*GetRailFrame)(int32_t gameObjectId, float normalizedProgress, EditorScriptRailFrame* frame);
	// ABI互換のため、情報付きDamage APIはRuntimeApiの末尾へ追加する。
	bool (*ApplyDamageContext)(EditorScriptDamageContext* damageContext);
	bool (*GetLastDamageContext)(int32_t targetGameObjectId, EditorScriptDamageContext* damageContext);
	bool (*InvokeScriptActionPayload)(int32_t gameObjectId, const char* functionName, const EditorScriptActionPayload* payload);
	bool (*StartTimer)(int32_t gameObjectId);
	bool (*PauseTimer)(int32_t gameObjectId, bool isPaused);
	bool (*GetTimerRemaining)(int32_t gameObjectId, float* remainingSeconds);
	bool (*ChangeGenericState)(int32_t gameObjectId, const char* stateName);
	bool (*GetGenericState)(int32_t gameObjectId, char* stateName, int32_t stateNameCapacity);
	bool (*SetAttributeValue)(int32_t gameObjectId, float value);
	bool (*GetAttributeValue)(int32_t gameObjectId, float* current, float* maximum);
	bool (*GetTargetLockState)(int32_t gameObjectId, float* progress, bool* isLocked, int32_t* targetGameObjectId);
	// ABI互換のため、複数Target・名前付き値・条件・Data APIは末尾へ追加する。
	bool (*SetNamedAttributeValue)(int32_t gameObjectId, const char* attributeName, float value);
	bool (*GetNamedAttributeValue)(int32_t gameObjectId, const char* attributeName, float* current, float* maximum);
	bool (*SetCounterValue)(int32_t gameObjectId, float value);
	bool (*AddCounterValue)(int32_t gameObjectId, float deltaValue);
	bool (*GetCounterValue)(int32_t gameObjectId, float* value);
	bool (*EvaluateGenericCondition)(int32_t gameObjectId, bool* result);
	bool (*GetMultiTargetLockCount)(int32_t gameObjectId, int32_t* targetCount);
	bool (*GetMultiTargetLockTarget)(int32_t gameObjectId, int32_t targetIndex, int32_t* targetGameObjectId, float* progress, bool* isLocked);
	bool (*GetGameplayDataValue)(int32_t gameObjectId, const char* key, int32_t* valueType, char* value, int32_t valueCapacity);
	// ABI互換のため、Damage・Threat・Cooldown・Reset APIは必ず構造体末尾へ追加する。
	int32_t (*HashDamageTag)(const char* damageTag);
	int32_t (*ApplyAreaDamage)(int32_t areaDamageGameObjectId, int32_t instigatorGameObjectId);
	bool (*DetonateProjectile)(int32_t projectileGameObjectId);
	bool (*GetThreatTrackerCount)(int32_t gameObjectId, int32_t* threatCount);
	bool (*GetThreatTrackerEntry)(int32_t gameObjectId, int32_t threatIndex, EditorScriptThreatInfo* threatInfo);
	bool (*StartNamedCooldown)(int32_t gameObjectId, const char* cooldownName, float durationOverride);
	bool (*ResetNamedCooldown)(int32_t gameObjectId, const char* cooldownName);
	bool (*GetNamedCooldown)(int32_t gameObjectId, const char* cooldownName, float* remainingSeconds, bool* isReady);
	bool (*ResetRuntimeState)(int32_t gameObjectId);
	// ABI互換のため、Weapon合成状態とTimeScale APIは必ず構造体末尾へ追加する。
	bool (*GetWeaponAccuracySpread)(int32_t gameObjectId, float* spreadDegrees);
	bool (*PlayTimeScale)(int32_t gameObjectId, float scaleOverride, float durationOverride);
	float (*GetTimeScale)();
	// ABI互換のため、照準・Mission・Encounter APIは必ず構造体末尾へ追加する。
	bool (*GetInterceptPrediction)(int32_t gameObjectId, EditorScriptVector3* position, float* timeSeconds);
	bool (*SetObjective)(int32_t gameObjectId, const char* objectiveId, int32_t state, float currentValue);
	bool (*GetObjective)(int32_t gameObjectId, const char* objectiveId, int32_t* state, float* currentValue, float* targetValue);
	bool (*StartEncounter)(int32_t gameObjectId);
	bool (*ResolveSpawnPoint)(int32_t gameObjectId, EditorScriptVector3* position, EditorScriptVector3* rotation);
	bool (*ApplyDifficulty)(int32_t gameObjectId, int32_t difficultyIndex);
	bool (*GetDamageDirection)(int32_t gameObjectId, EditorScriptVector2* direction, float* alpha, int32_t* sourceGameObjectId);
	// ABI互換のため、弾道・複数被弾・永続Pause・航跡APIは必ず構造体末尾へ追加する。
	bool (*GetBallisticPrediction)(int32_t gameObjectId, EditorScriptBallisticPrediction* prediction);
	bool (*GetBallisticTrajectoryPoint)(int32_t gameObjectId, int32_t pointIndex, EditorScriptVector3* point);
	bool (*GetDamageEventBufferCount)(int32_t gameObjectId, int32_t* eventCount);
	bool (*GetDamageEventBufferEntry)(int32_t gameObjectId, int32_t eventIndex, EditorScriptDamageEvent* damageEvent);
	bool (*SetGamePaused)(int32_t gameObjectId, bool isPaused);
	bool (*IsGamePaused)();
	bool (*GetSurfaceWakeState)(int32_t gameObjectId, float* speed, float* intensity);
	// ABI互換のため、FFT水面Gameplay Query APIは必ず構造体末尾へ追加する。
	bool (*OceanSegmentCast)(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanSegmentHit* hit);
	bool (*OceanRaycast)(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptRay* ray, float maximumDistance, float clearance, EditorScriptOceanSegmentHit* hit);
	bool (*QueryOceanOcclusion)(int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanOcclusion* occlusion);
	bool (*GetWaterSurfaceState)(int32_t gameObjectId, EditorScriptWaterSurfaceState* state);
	bool (*GetOceanProbeSample)(int32_t gameObjectId, int32_t probeIndex, EditorScriptOceanProbeSample* sample);
	// ABI互換のため、武器運用・砲塔状態APIは必ず構造体末尾へ追加する。
	bool (*LoadoutGetAmmoAtSlot)(int32_t gameObjectId, int32_t slotIndex, int32_t* currentAmmo, int32_t* reserveAmmo, int32_t* maximumAmmo);
	bool (*LoadoutAddMagazineAmmo)(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	bool (*LoadoutAddReserveAmmo)(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	bool (*LoadoutSetMagazineAmmo)(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	bool (*LoadoutSetReserveAmmo)(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	bool (*LoadoutSetMaximumAmmo)(int32_t gameObjectId, int32_t slotIndex, int32_t amount);
	bool (*LoadoutRefillMagazine)(int32_t gameObjectId, int32_t slotIndex);
	bool (*FireWeaponGroup)(int32_t gameObjectId);
	bool (*IsWeaponGroupFiring)(int32_t gameObjectId);
	bool (*GetTurretAimState)(int32_t gameObjectId, EditorScriptTurretAimState* state);
	// ABI互換のため、発射前検査と時間制Effect APIは必ず構造体末尾へ追加する。
	bool (*GetFireLineState)(int32_t gameObjectId, EditorScriptFireLineState* state);
	bool (*ApplyStatusEffect)(int32_t gameObjectId, const char* effectId, int32_t sourceGameObjectId);
	bool (*RemoveStatusEffect)(int32_t gameObjectId, const char* effectId);
	bool (*ClearStatusEffects)(int32_t gameObjectId);
	bool (*HasStatusEffect)(int32_t gameObjectId, const char* effectId);
	bool (*GetStatusEffectCount)(int32_t gameObjectId, int32_t* effectCount);
	bool (*GetStatusEffectEntry)(int32_t gameObjectId, int32_t effectIndex, EditorScriptStatusEffectEntry* effectEntry);
	// ABI互換のため、Ocean詳細Sample APIは既存構造体を変えず末尾へ追加する。
	bool (*SampleOceanSurfaceDetailed)(int32_t queryGameObjectId, const EditorScriptVector3* worldPosition, EditorScriptOceanSurfaceHit* hit, float* foam);
	bool (*GetWaterSurfaceFoam)(int32_t gameObjectId, float* foam);
	bool (*GetOceanProbeFoam)(int32_t gameObjectId, int32_t probeIndex, float* foam);
	// ABI互換のため、Rail制作支援APIは既存構造体を変えず末尾へ追加する。
	bool (*SetRailSpeedProfileEnabled)(int32_t gameObjectId, bool isEnabled);
	bool (*GetRailSpeedMultiplier)(int32_t gameObjectId, float* speedMultiplier);
	bool (*GetRailActiveZone)(int32_t gameObjectId, char* zoneId, int32_t zoneIdCapacity);
	// ABI互換のため、Rail MarkerとSimulation LOD照会APIは末尾へ追加する。
	bool (*RearmRailEventMarkers)(int32_t gameObjectId, const char* markerId);
	bool (*GetSimulationLodLevel)(int32_t gameObjectId, int32_t* lodLevel);
	// ABI互換のため、Wave外部制御APIは既存構造体の末尾へ追加する。
	bool (*StartWaveSpawner)(int32_t gameObjectId);
	bool (*IsWaveSpawnerComplete)(int32_t gameObjectId, bool waitsForAllDefeated);
	// ABI互換のため、階層除外Raycast APIは既存構造体を変えず末尾へ追加する。
	bool (*PhysicsRaycastIgnoringHierarchy)(
		const EditorScriptRay* ray,
		float distance,
		int32_t ignoreHierarchyRootGameObjectId,
		EditorScriptPhysicsHit* hit);
	// ABI互換のため、位置指定Effekseer再生APIは既存構造体を変えず末尾へ追加する。
	// GameObjectを介さず任意のWorld座標へ.efk/.efkefcを再生する(EffectManager::PlayEffekseer)。
	int32_t (*PlayEffekseerAtPosition)(const char* effectAssetPath, const EditorScriptVector3* position, const EditorScriptVector3* rotationEuler);
	bool (*SetEffekseerEffectPosition)(int32_t effekseerPlaybackHandle, const EditorScriptVector3* position);
	void (*StopEffekseerEffectAtPosition)(int32_t effekseerPlaybackHandle);
	// ABI互換のため、EffectDefinition(.effectdef)のWorld座標再生APIは末尾へ追加する。
	bool (*PlayVfxAtPosition)(const char* effectId, const EditorScriptVector3* position);
	// ABI互換のため、Runtime Joint APIは既存構造体を変えず末尾へ追加する。
	EditorScriptJointHandle (*CreateSpringJoint)(
		int32_t ownerGameObjectId,
		int32_t connectedGameObjectId,
		const EditorScriptSpringJointDesc* springJointDesc);
	bool (*DestroyJoint)(EditorScriptJointHandle jointHandle);
	bool (*SetSpringJointSettings)(
		EditorScriptJointHandle jointHandle,
		const EditorScriptSpringJointDesc* springJointDesc);
	bool (*IsJointValid)(EditorScriptJointHandle jointHandle);
	// Spring以外も同じHandle管理へ載せる汎用Runtime Constraint API。
	EditorScriptJointHandle (*CreateJoint)(
		EditorScriptJointType jointType,
		int32_t ownerGameObjectId,
		int32_t connectedGameObjectId,
		const EditorScriptJointDesc* jointDesc);
	bool (*SetJointSettings)(
		EditorScriptJointHandle jointHandle,
		const EditorScriptJointDesc* jointDesc);
	// ABI互換のため、実行時Camera制作用のマウス・カーソルAPIは末尾へ追加する。
	EditorScriptVector2 (*GetMouseDelta)();
	bool (*IsMouseButtonDown)(int32_t mouseButton);
	bool (*WasMouseButtonPressed)(int32_t mouseButton);
	bool (*WasMouseButtonReleased)(int32_t mouseButton);
	void (*SetCursorLocked)(bool isLocked);
	bool (*IsCursorLocked)();
	void (*SetCursorVisible)(bool isVisible);
	bool (*IsCursorVisible)();
	// Wireゲーム向けの独立Runtime接続。GameObject Componentの個数制限を受けない。
	EditorScriptWireHandle (*CreateWire)(const EditorScriptWireDesc* wireDesc);
	bool (*DestroyWire)(EditorScriptWireHandle wireHandle);
	bool (*SetWireLengthByHandle)(EditorScriptWireHandle wireHandle, float maximumLength);
	bool (*SetWireShrinkSpeed)(EditorScriptWireHandle wireHandle, float shrinkSpeed);
	bool (*RepairWire)(EditorScriptWireHandle wireHandle);
	bool (*GetWireStateByHandle)(EditorScriptWireHandle wireHandle, EditorScriptWireState* wireState);
	int32_t (*GetWireCountForGameObject)(int32_t gameObjectId);
	bool (*GetWireForGameObject)(int32_t gameObjectId, int32_t wireIndex, EditorScriptWireState* wireState);
	bool (*CanConnectWire)(int32_t gameObjectId);
	// 実行時Component構成とGameObject生成をScriptから変更する。
	bool (*AddComponent)(int32_t gameObjectId, const char* componentTypeName);
	bool (*RemoveComponent)(int32_t gameObjectId, const char* componentTypeName);
	int32_t (*FindGameObjectsWithComponent)(const char* componentTypeName, int32_t* gameObjectIds, int32_t capacity);
	int32_t (*InstantiateGameObject)(int32_t sourceGameObjectId, const EditorScriptVector3* position, const EditorScriptVector3* rotation);
	bool (*DestroyGameObject)(int32_t gameObjectId);
	// 親子、回転、Scaleを含む正しい座標変換。
	bool (*WorldToLocalPoint)(int32_t gameObjectId, const EditorScriptVector3* worldPoint, EditorScriptVector3* localPoint);
	bool (*LocalToWorldPoint)(int32_t gameObjectId, const EditorScriptVector3* localPoint, EditorScriptVector3* worldPoint);
	bool (*WorldToLocalDirection)(int32_t gameObjectId, const EditorScriptVector3* worldDirection, EditorScriptVector3* localDirection);
	bool (*LocalToWorldDirection)(int32_t gameObjectId, const EditorScriptVector3* localDirection, EditorScriptVector3* worldDirection);
	// Layer、Trigger、必須Componentを同時に指定する選択用Raycast。
	bool (*PhysicsRaycastFiltered)(
		const EditorScriptRay* ray,
		float distance,
		uint32_t physicsLayerMask,
		bool includeTriggers,
		const char* requiredComponentTypeName,
		EditorScriptPhysicsHit* hit);
	// Hook表示、質量色表示、実行時Hierarchy構築をユーザーScriptへ公開する。
	bool (*SetRendererColor)(int32_t gameObjectId, const EditorScriptVector3* color);
	bool (*SetRendererEmission)(int32_t gameObjectId, const EditorScriptVector3* color, float strength);
	bool (*SetHookVisualState)(int32_t hookGameObjectId, int32_t visualState);
	int32_t (*CreateGameObject)(const char* name);
	int32_t (*GetParentGameObject)(int32_t gameObjectId);
	bool (*SetParentGameObject)(int32_t childGameObjectId, int32_t parentGameObjectId, bool preserveWorldTransform);
	int32_t (*GetChildGameObjectCount)(int32_t gameObjectId);
	int32_t (*GetChildGameObject)(int32_t gameObjectId, int32_t childIndex);
	bool (*ReloadPrimaryScene)();
};

extern "C" {
	typedef bool(__cdecl* EditorScriptLoadFn)(uint32_t apiVersion, const EditorScriptRuntimeApi* runtimeApi);
	typedef void(__cdecl* EditorScriptUnloadFn)();
	typedef void(__cdecl* EditorScriptStartFn)(int32_t gameObjectId);
	typedef void(__cdecl* EditorScriptUpdateFn)(int32_t gameObjectId, float deltaTime);
	typedef void(__cdecl* EditorScriptFixedUpdateFn)(int32_t gameObjectId, float fixedDeltaTime);
	typedef void(__cdecl* EditorScriptPhysicsEventFn)(int32_t gameObjectId, const EditorScriptPhysicsEvent* physicsEvent);
	typedef void(__cdecl* EditorScriptWireEventFn)(int32_t gameObjectId, const EditorScriptWireEvent* wireEvent);
	typedef void(__cdecl* EditorScriptAnimationEventFn)(int32_t gameObjectId, const EditorScriptAnimationEvent* animationEvent);
	typedef void(__cdecl* EditorScriptStopFn)(int32_t gameObjectId);
	typedef int32_t(__cdecl* EditorScriptGetFieldCountFn)();
	typedef bool(__cdecl* EditorScriptGetFieldDescriptorFn)(int32_t fieldIndex, EditorScriptFieldDescriptor* fieldDescriptor);
	typedef bool(__cdecl* EditorScriptGetFieldValueFn)(int32_t gameObjectId, const char* fieldName, EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptSetFieldValueFn)(int32_t gameObjectId, const char* fieldName, const EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptInvokeActionFn)(int32_t gameObjectId, const char* functionName, const EditorScriptInputActionContext* inputContext);
	typedef int32_t(__cdecl* EditorScriptGetActionCountFn)();
	typedef bool(__cdecl* EditorScriptGetActionNameFn)(int32_t actionIndex, char* actionName, int32_t actionNameCapacity);

	// Component ごとの状態をエンジン側で所有する新しい任意 ABI。
	// Create / Destroy が両方ある DLL だけを Instance API として扱い、旧 DLL は従来の gameObjectId API で実行する。
	typedef void* (__cdecl* EditorScriptCreateInstanceFn)(int32_t gameObjectId);
	typedef void(__cdecl* EditorScriptDestroyInstanceFn)(void* instance);
	typedef void(__cdecl* EditorScriptStartInstanceFn)(void* instance);
	typedef void(__cdecl* EditorScriptUpdateInstanceFn)(void* instance, float deltaTime);
	typedef void(__cdecl* EditorScriptFixedUpdateInstanceFn)(void* instance, float fixedDeltaTime);
	typedef void(__cdecl* EditorScriptPhysicsEventInstanceFn)(void* instance, const EditorScriptPhysicsEvent* physicsEvent);
	typedef void(__cdecl* EditorScriptWireEventInstanceFn)(void* instance, const EditorScriptWireEvent* wireEvent);
	typedef void(__cdecl* EditorScriptAnimationEventInstanceFn)(void* instance, const EditorScriptAnimationEvent* animationEvent);
	typedef void(__cdecl* EditorScriptStopInstanceFn)(void* instance);
	typedef bool(__cdecl* EditorScriptGetFieldValueInstanceFn)(void* instance, const char* fieldName, EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptSetFieldValueInstanceFn)(void* instance, const char* fieldName, const EditorScriptFieldValue* fieldValue);
	typedef bool(__cdecl* EditorScriptInvokeActionInstanceFn)(void* instance, const char* functionName, const EditorScriptInputActionContext* inputContext);
}
