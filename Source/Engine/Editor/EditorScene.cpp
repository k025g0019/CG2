#include "EditorScene.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "Vector&Matrix.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#pragma warning(push)
#pragma warning(disable : 5045)

namespace {
	constexpr unsigned char kSceneUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // Scene は UTF-8 BOM 付きで保存する。

	constexpr int32_t kInvalidGameObjectId = -1;  // 親なし / 無効 ID を表す値
	constexpr int32_t kGlareModeCount = 8;  // Glare の mode 0-7 を固定配列で保存する
	constexpr int32_t kFilterModeCount = 9;  // Filter の mode 0-8 を固定配列で保存する
	constexpr float kTransformEpsilon = 0.00001f;

	float GetVectorLength(float x, float y, float z) {
		return std::sqrt(x * x + y * y + z * z);
	}

	bool DecomposeTransformMatrix(
		const Matrix4x4& transformMatrix,
		Vector3& scale,
		Vector3& rotation,
		Vector3& position) {
		position = {
			transformMatrix.matrix[3][0],
			transformMatrix.matrix[3][1],
			transformMatrix.matrix[3][2]};
		scale = {
			GetVectorLength(
				transformMatrix.matrix[0][0],
				transformMatrix.matrix[0][1],
				transformMatrix.matrix[0][2]),
			GetVectorLength(
				transformMatrix.matrix[1][0],
				transformMatrix.matrix[1][1],
				transformMatrix.matrix[1][2]),
			GetVectorLength(
				transformMatrix.matrix[2][0],
				transformMatrix.matrix[2][1],
				transformMatrix.matrix[2][2])};

		if (scale.x <= kTransformEpsilon ||
			scale.y <= kTransformEpsilon ||
			scale.z <= kTransformEpsilon) {
			return false;
		}

		Matrix4x4 rotationMatrix = transformMatrix;
		for (int32_t columnIndex = 0; columnIndex < 3; columnIndex++) {
			rotationMatrix.matrix[0][columnIndex] /= scale.x;
			rotationMatrix.matrix[1][columnIndex] /= scale.y;
			rotationMatrix.matrix[2][columnIndex] /= scale.z;
		}

		const float sineY = (std::clamp)(-rotationMatrix.matrix[0][2], -1.0f, 1.0f);
		rotation.y = std::asin(sineY);
		const float cosineY = std::cos(rotation.y);

		if (std::fabs(cosineY) > kTransformEpsilon) {
			rotation.x = std::atan2(
				rotationMatrix.matrix[1][2],
				rotationMatrix.matrix[2][2]);
			rotation.z = std::atan2(
				rotationMatrix.matrix[0][1],
				rotationMatrix.matrix[0][0]);
		}
		else {
			// Gimbal lock時はZを0に固定し、残る回転をXへまとめる。
			rotation.x = std::atan2(
				-rotationMatrix.matrix[2][1],
				rotationMatrix.matrix[1][1]);
			rotation.z = 0.0f;
		}

		return true;
	}
	constexpr const char* kEditorComponentTypeNames[] = {
		"Transform",
		"ModelRenderer",
		"SpriteRenderer",
		"Light",
		"Camera",
		"AudioSource",
		"RigidBody",
		"BoxCollider",
		"SphereCollider",
		"Input",
		"Animation",
		"Animator",
		"AudioListener",
		"ParentConstraint",
		"PositionConstraint",
		"RotationConstraint",
		"ScaleConstraint",
		"EventSystem",
		"MeshFilter",
		"CapsuleCollider",
		"MeshCollider",
		"CharacterController",
		"NavMeshAgent",
		"PlayableDirector",
		"Script",
		"HapticSource",
		"Canvas",
		"Image",
		"Text",
		"RectTransform",
		"MonoBehaviour",
		"SkinnedMeshRenderer",
		"LineRenderer",
		"TrailRenderer",
		"BillboardRenderer",
		"CanvasRenderer",
		"ParticleSystemRenderer",
		"FlareLayer",
		"CinemachineCamera",
		"ReflectionProbe",
		"LightProbeGroup",
		"LightProbeProxyVolume",
		"Volume",
		"TerrainCollider",
		"WheelCollider",
		"ConstantForce",
		"HingeJoint",
		"FixedJoint",
		"SpringJoint",
		"ConfigurableJoint",
		"CharacterJoint",
		"RigidBody2D",
		"BoxCollider2D",
		"CircleCollider2D",
		"CapsuleCollider2D",
		"PolygonCollider2D",
		"EdgeCollider2D",
		"CompositeCollider2D",
		"TilemapCollider2D",
		"CustomCollider2D",
		"DistanceJoint2D",
		"HingeJoint2D",
		"SpringJoint2D",
		"FixedJoint2D",
		"SliderJoint2D",
		"WheelJoint2D",
		"PlatformEffector2D",
		"SurfaceEffector2D",
		"AreaEffector2D",
		"PointEffector2D",
		"BuoyancyEffector2D",
		"AvatarMask",
		"AimConstraint",
		"LookAtConstraint",
		"AudioReverbZone",
		"AudioLowPassFilter",
		"AudioHighPassFilter",
		"AudioEchoFilter",
		"AudioDistortionFilter",
		"AudioReverbFilter",
		"AudioChorusFilter",
		"CanvasScaler",
		"GraphicRaycaster",
		"RawImage",
		"TextMeshProUGUI",
		"Button",
		"Toggle",
		"Slider",
		"Scrollbar",
		"Dropdown",
		"TMPDropdown",
		"InputField",
		"TMPInputField",
		"ScrollRect",
		"Mask",
		"RectMask2D",
		"HorizontalLayoutGroup",
		"VerticalLayoutGroup",
		"GridLayoutGroup",
		"ContentSizeFitter",
		"AspectRatioFitter",
		"LayoutElement",
		"StandaloneInputModule",
		"InputSystemUIInputModule",
		"PlayerInput",
		"PlayerInputManager",
		"TouchInputModule",
		"NavMeshObstacle",
		"NavMeshSurface",
		"NavMeshModifier",
		"NavMeshModifierVolume",
		"NavMeshLink",
		"AIBehaviorTree",
		"AIBehaviorBlackboard",
		"AIBehaviorSelector",
		"AIBehaviorSequence",
		"AIBehaviorTask",
		"AIBehaviorDecorator",
		"AIStateMachine",
		"AIState",
		"AIStateTransition",
		"AIGoapPlanner",
		"AIGoapGoal",
		"AIGoapAction",
		"AIGoapWorldState",
		"AIHtnPlanner",
		"AIHtnDomain",
		"AIHtnTask",
		"AIHtnMethod",
		"AIPathfindingAgent",
		"AIMicroPatherGrid",
		"AIRecastNavMeshBuilder",
		"AIRecastCrowdAgent",
		"AIPathRequest",
		"AIDynamicObstacle",
		"AISteeringAgent",
		"AISeekSteering",
		"AIFleeSteering",
		"AIArriveSteering",
		"AIPursuitSteering",
		"AIWanderSteering",
		"AIObstacleAvoidanceSteering",
		"AIFlockSteering",
		"AIVisionSensor",
		"AIOpenCvCamera",
		"AIOpenCvObjectDetector",
		"AIOpenCvColorTracker",
		"AIMotionSensor",
		"AIWhisperSpeechRecognizer",
		"AIVoiceCommand",
		"ParticleSystem",
		"VisualEffect",
		"LensFlare",
		"Projector",
		"DecalProjector",
		"Terrain",
		"Tilemap",
		"TilemapRenderer",
		"Grid",
		"LocalMove",
		"RollingMove",
		"PostProcess",
		"Environment",
		"FreeTransform",
		"AutoConvexCollision",
		"Ocean",
		"Buoyancy",
		"RailMovement",
		"Health",
		"LegacyRailShooterEnemy",
		"LegacyRailShooterShip",
		"LegacyRailShooterEnemyMotion",
		"LegacyRailShooterStage",
		"SceneButton",
		"Foliage",
		"WaveSpawner",
		"TimelineEvent",
		"ThresholdState",
		"UIValueBinding",
		"Aerodynamics",
		"WindZone",
		"GravityField",
		"RotatingFrame",
		"FluidVolume",
		"SpringForce",
		"ElectromagneticBody",
		"ElectromagneticField",
		"ScreenAim",
		"HitscanWeapon",
		"ProjectileEmitter",
		"DamageReceiver",
		"ObjectPool",
		"PrefabSpawner",
		"CameraBlend",
		"CameraShake",
		"RailBranch",
		"ActionSequence",
		"ActionSequenceStep",
		"Saveable",
		"Checkpoint",
		"RopeConstraint",
		"TorsionSpring",
		"WeaponLoadout",
		"WeaponLoadoutSlot",
		"TargetSelector",
		"TargetSteering",
		"MovementModifier",
		"PropertyTween",
		"ActionRelay",
		"ActionRelayTarget",
		"Thruster",
		"PulleyConstraint",
		"PhysicsServo",
		"VortexField",
		"PressureField",
		"Suspension",
		"UprightStabilizer",
		"TargetPoint",
		"Team",
		"Timer",
		"GenericStateMachine",
		"Attribute",
		"DestructiblePart",
		"FormationFollower",
		"TargetLock",
		"MultiTargetLock",
		"WorldTargetMarker",
		"OffScreenIndicator",
		"AttributeSet",
		"GenericCounter",
		"GenericCondition",
		"GameplayData",
		"AreaDamage",
		"HitZone",
		"DamageTagModifier",
		"ProjectileDetonator",
		"ThreatTracker",
		"RuntimeStateReset",
		"CooldownSet",
		"WeaponFirePattern",
		"TargetAssignment",
		"WeaponAccuracy",
		"WeaponRecoil",
		"ImpactResponder",
		"SurfaceType",
		"TimeScale",
		"AimAssist",
		"InterceptPrediction",
		"DamageDirectionIndicator",
		"ObjectiveTracker",
		"EncounterController",
		"SpawnPointSet",
		"DifficultyParameterSet",
		"CameraFeedbackMixer",
		"BallisticPrediction",
		"DamageEventBuffer",
		"GamePause",
		"SurfaceWakeEmitter",
		"TrajectoryRenderer",
		"WaterSurfaceState",
		"OceanProbeSet",
		"AttackCollisionFilter",
		"TurretAim",
		"WeaponGroup",
		"ProjectileImpactPhysics",
		"CameraHorizonStabilizer",
		"FireLineCheck",
		"StatusEffectSet",
		"RailSpeedProfile",
		"RailZone",
		"CameraFollowComposer",
		"SpeedFeedback",
		"SpawnedObjectSetup",
		"WaveMotionProfile",
		"DistanceActivation",
		"SimulationLOD",
		"RailEventMarker",
		"SceneStreaming",
	};
	constexpr int32_t kEditorComponentTypeCount =
		static_cast<int32_t>(sizeof(kEditorComponentTypeNames) / sizeof(kEditorComponentTypeNames[0]));
	static_assert(
		kEditorComponentTypeCount == static_cast<int32_t>(EditorComponentType::Count),
		"EditorComponentType と kEditorComponentTypeNames の数が一致していません。");

	std::vector<std::string> SplitLine(const std::string& line, char delimiter) {
		std::vector<std::string> elements;  // SaveScene の 1 行を delimiter 区切りで分割する
		std::stringstream stream(line);
		std::string element;

		while (std::getline(stream, element, delimiter)) {
			elements.push_back(element);
		}

		return elements;
	}

	void ApplyPostProcessEffectDefaults(EditorComponent& component) {
		//============================================================
		// Glare / Filter の種類別初期値
		//============================================================
		// 旧Sceneの共有値は残しつつ、Inspector と描画では種類ごとの値を使う。

		component.glareIntensityByMode.fill(component.glareIntensity);
		component.glareSizeByMode.fill(component.glareSize);
		component.glareAngleByMode.fill(component.glareAngle);
		component.glareStreakCountByMode.fill(component.glareStreakCount);
		component.glareFadeByMode.fill(component.glareFade);
		component.glareColorModulationByMode.fill(component.glareColorModulation);
		component.glareCenterByMode.fill(component.glareCenter);
		component.glareColorByMode.fill({1.0f, 1.0f, 1.0f});

		component.glareIntensityByMode[1] = 1.0f;  // Bloom: 白いにじみ
		component.glareSizeByMode[1] = 1.0f;
		component.glareFadeByMode[1] = 0.85f;

		component.glareIntensityByMode[2] = 0.55f;  // Ghost: レンズ内反射
		component.glareSizeByMode[2] = 1.35f;
		component.glareFadeByMode[2] = 0.72f;
		component.glareColorModulationByMode[2] = 0.35f;

		component.glareIntensityByMode[3] = 0.75f;  // 光の筋: 長い線状
		component.glareSizeByMode[3] = 3.0f;
		component.glareFadeByMode[3] = 0.82f;
		component.glareStreakCountByMode[3] = 2;

		component.glareIntensityByMode[4] = 0.45f;  // フォググロー: 広い霧状
		component.glareSizeByMode[4] = 2.2f;
		component.glareFadeByMode[4] = 0.65f;
		component.glareColorByMode[4] = {0.65f, 0.78f, 1.0f};

		component.glareIntensityByMode[5] = 0.65f;  // 単純な星型: 短い放射状
		component.glareSizeByMode[5] = 1.55f;
		component.glareFadeByMode[5] = 0.70f;
		component.glareStreakCountByMode[5] = 6;

		component.glareIntensityByMode[6] = 0.35f;  // サンビーム: 暴れない初期値
		component.glareSizeByMode[6] = 0.75f;
		component.glareFadeByMode[6] = 0.88f;
		component.glareCenterByMode[6] = {0.5f, 0.5f, 0.0f};
		component.glareColorByMode[6] = {0.55f, 0.75f, 1.0f};

		component.glareIntensityByMode[7] = 0.45f;  // カーネル: 局所強調
		component.glareSizeByMode[7] = 1.0f;
		component.glareFadeByMode[7] = 0.85f;

		component.filterStrengthByMode.fill(component.filterStrength);
		component.filterColorByMode.fill({1.0f, 1.0f, 1.0f});
		component.filterStrengthByMode[1] = 0.45f;
		component.filterStrengthByMode[2] = 0.45f;
		component.filterStrengthByMode[3] = 0.45f;
		component.filterStrengthByMode[4] = 0.55f;
		component.filterStrengthByMode[5] = 0.80f;
		component.filterStrengthByMode[6] = 0.80f;
		component.filterStrengthByMode[7] = 0.80f;
		component.filterStrengthByMode[8] = 0.50f;
		component.filterColorByMode[8] = {0.65f, 0.75f, 1.0f};
	}

	void ReplaceSceneTokenText(std::string& text, const std::string& sourceText, const std::string& replacementText) {
		size_t searchPosition = 0U;
		while ((searchPosition = text.find(sourceText, searchPosition)) != std::string::npos) {
			text.replace(searchPosition, sourceText.size(), replacementText);
			searchPosition += replacementText.size();
		}
	}

	std::string EncodeSceneToken(const std::string& text) {
		std::string encodedText = text;  // 可変長 Script 文字列が Scene の区切り記号を壊さないよう符号化する。
		ReplaceSceneTokenText(encodedText, "%", "%25");
		ReplaceSceneTokenText(encodedText, "|", "%7C");
		ReplaceSceneTokenText(encodedText, "\r", "%0D");
		ReplaceSceneTokenText(encodedText, "\n", "%0A");
		return encodedText;
	}

	std::string DecodeSceneToken(const std::string& text) {
		std::string decodedText = text;
		ReplaceSceneTokenText(decodedText, "%0A", "\n");
		ReplaceSceneTokenText(decodedText, "%0D", "\r");
		ReplaceSceneTokenText(decodedText, "%7C", "|");
		ReplaceSceneTokenText(decodedText, "%25", "%");
		return decodedText;
	}

	void AddDefaultInputEventBindings(EditorComponent& component) {
		if (component.type != EditorComponentType::PlayerInput || !component.inputEventBindings.empty()) {
			return;
		}

		component.inputEventBindings.push_back({"Player", "Move", "OnMove", 1});
		component.inputEventBindings.push_back({"Player", "Jump", "OnJump", 0});
		component.inputEventBindings.push_back({"Player", "Fire", "OnFire", 0});
	}

	void RemapGameObjectReference(
		int32_t& gameObjectId,
		const std::unordered_map<int32_t, int32_t>& remappedIds) {
		if (gameObjectId < 0) {
			return;
		}

		const auto remappedIterator = remappedIds.find(gameObjectId);
		gameObjectId = remappedIterator == remappedIds.end()
			? kInvalidGameObjectId
			: remappedIterator->second;
	}

	void RemapComponentGameObjectReferences(
		EditorComponent& component,
		const std::unordered_map<int32_t, int32_t>& remappedIds) {
		// PrefabとAdditive Sceneの複製先が、元SceneのIDを誤参照しないよう全参照を一括変換する。
		RemapGameObjectReference(component.connectedGameObjectId, remappedIds);
		RemapGameObjectReference(component.buoyancyOceanGameObjectId, remappedIds);
		RemapGameObjectReference(component.railPathGameObjectId, remappedIds);
		RemapGameObjectReference(component.springForceTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.ropeTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.torsionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.pulleyTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.servoTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.waveTriggerSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.waveActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.enemySpawnFollowerGameObjectId, remappedIds);
		RemapGameObjectReference(component.enemyAttackTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.enemyProjectileTemplateGameObjectId, remappedIds);
		RemapGameObjectReference(component.railShipSpeedSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.railShipSailGameObjectId, remappedIds);
		RemapGameObjectReference(component.railShipWakeEffectGameObjectId, remappedIds);
		RemapGameObjectReference(component.railShipWindEffectGameObjectId, remappedIds);
		RemapGameObjectReference(component.enemyMotionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.stageFollowerGameObjectId, remappedIds);
		RemapGameObjectReference(component.stageStartMarkerGameObjectId, remappedIds);
		RemapGameObjectReference(component.stageGoalMarkerGameObjectId, remappedIds);
		RemapGameObjectReference(component.stageStartEffectGameObjectId, remappedIds);
		RemapGameObjectReference(component.stageGoalEffectGameObjectId, remappedIds);
		RemapGameObjectReference(component.timelineSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.timelineTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.railEventFollowerGameObjectId, remappedIds);
		RemapGameObjectReference(component.railEventTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.thresholdSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.thresholdTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.uiBindingSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.railHudSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.screenAimInputGameObjectId, remappedIds);
		RemapGameObjectReference(component.screenAimReticleGameObjectId, remappedIds);
		RemapGameObjectReference(component.hitscanAimGameObjectId, remappedIds);
		RemapGameObjectReference(component.hitscanInputGameObjectId, remappedIds);
		RemapGameObjectReference(component.hitscanActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.projectileAimGameObjectId, remappedIds);
		RemapGameObjectReference(component.projectileInputGameObjectId, remappedIds);
		RemapGameObjectReference(component.projectilePoolGameObjectId, remappedIds);
		RemapGameObjectReference(component.projectileSpawnPointGameObjectId, remappedIds);
		RemapGameObjectReference(component.projectileActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.damageActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.objectPoolTemplateGameObjectId, remappedIds);
		RemapGameObjectReference(component.prefabSpawnerPoolGameObjectId, remappedIds);
		RemapGameObjectReference(component.prefabSpawnerPointGameObjectId, remappedIds);
		RemapGameObjectReference(component.prefabSpawnerActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.cameraBlendSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.cameraBlendTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.railBranchFollowerGameObjectId, remappedIds);
		RemapGameObjectReference(component.railBranchTargetPathGameObjectId, remappedIds);
		RemapGameObjectReference(component.railBranchActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.actionSequenceTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.checkpointActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.railZoneActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.cameraComposerTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.speedFeedbackSourceGameObjectId, remappedIds);
		RemapGameObjectReference(component.speedFeedbackCameraGameObjectId, remappedIds);
		RemapGameObjectReference(component.spawnedSetupRailPathGameObjectId, remappedIds);
		RemapGameObjectReference(component.spawnedSetupActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.distanceActivationReferenceGameObjectId, remappedIds);
		RemapGameObjectReference(component.simulationLodReferenceGameObjectId, remappedIds);
		RemapGameObjectReference(component.railEventMarkerActionTargetGameObjectId, remappedIds);
		RemapGameObjectReference(component.sceneStreamingReferenceGameObjectId, remappedIds);
	}

	int32_t ToInt(const std::string& text) {
		// Scene ファイルの文字列を int32_t に変換する
		return static_cast<int32_t>(std::stoi(text));
	}

	float ToFloat(const std::string& text) {
		// Scene ファイルの文字列を float に変換する
		return std::stof(text);
	}

	void ResetPhysicsSettings(EditorPhysicsSettings& physicsSettings) {
		physicsSettings.gravity = {0.0f, -9.8f, 0.0f};
		physicsSettings.fixedTimeStep = 1.0f / 60.0f;
		physicsSettings.collisionStepCount = 1;
		physicsSettings.drawColliderDebug = true;
		physicsSettings.drawContactDebug = true;
		physicsSettings.drawCastDebug = true;
		physicsSettings.drawVelocityDebug = true;
		physicsSettings.drawForceDirectionDebug = true;
		physicsSettings.drawFieldVolumeDebug = true;
		physicsSettings.drawConnectionDebug = true;
		physicsSettings.drawSelectedOnlyDebug = false;
		physicsSettings.debugVectorScale = 0.25f;

		for (int32_t firstLayer = 0; firstLayer < kEditorPhysicsLayerCount; ++firstLayer) {
			for (int32_t secondLayer = 0; secondLayer < kEditorPhysicsLayerCount; ++secondLayer) {
				bool isUiLayer = firstLayer == 6 || secondLayer == 6;
				bool isIgnoreRaycastLayer = firstLayer == 7 || secondLayer == 7;
				physicsSettings.layerCollisionMatrix[firstLayer][secondLayer] =
					!isUiLayer && !isIgnoreRaycastLayer;
			}
		}
	}
}

//============================================================
// Component
//============================================================

std::string ToString(EditorComponentType type) {
	// 保存用の英語名へ変換する。Inspector の日本語表示は EditorInspectorPanel 側で行う
	int32_t componentIndex = static_cast<int32_t>(type);
	if (componentIndex < 0 || componentIndex >= kEditorComponentTypeCount) {
		return "Unknown";
	}

	return kEditorComponentTypeNames[static_cast<size_t>(componentIndex)];
}

EditorComponentType ComponentTypeFromIndex(int32_t componentIndex) {
	// UI の選択番号は enum 値と同じ値にする。範囲外は安全側で Transform に戻す
	if (componentIndex < 0 || componentIndex >= kEditorComponentTypeCount) {
		return EditorComponentType::Transform;
	}

	return static_cast<EditorComponentType>(componentIndex);
}

//============================================================
// Scene
//============================================================

EditorScene::EditorScene() : nextGameObjectId_(1) {
	ResetPhysicsSettings(physicsSettings_);
}

void EditorScene::InitializeDefaultScene() {
	gameObjects_.clear();
	undoStack_.clear();
	redoStack_.clear();
	nextGameObjectId_ = 1;
	ResetPhysicsSettings(physicsSettings_);

	//============================================================
	// 起動時に必要な撮影・照明環境
	//============================================================

	const int32_t environmentGameObjectId = CreateGameObject("Environment Light");
	AddComponent(environmentGameObjectId, EditorComponentType::Environment);

	const int32_t cameraGameObjectId = CreateGameObject("Main Camera");
	AddComponent(cameraGameObjectId, EditorComponentType::Camera);
	EditorGameObject* cameraGameObject = FindGameObject(cameraGameObjectId);

	if (cameraGameObject != nullptr) {
		cameraGameObject->translate = {0.0f, 2.0f, -6.0f};
		cameraGameObject->rotate = {0.25f, 0.0f, 0.0f};
	}

	const int32_t pointLightGameObjectId = CreateGameObject("Point Light");
	AddComponent(pointLightGameObjectId, EditorComponentType::Light);
	EditorGameObject* pointLightGameObject = FindGameObject(pointLightGameObjectId);

	if (pointLightGameObject != nullptr) {
		pointLightGameObject->translate = {2.0f, 3.0f, -2.0f};
	}

	//============================================================
	// 課題確認用 Sprite
	//============================================================

	const int32_t spriteGameObjectId = CreateGameObject("Sprite");
	AddComponent(spriteGameObjectId, EditorComponentType::SpriteRenderer);
	EditorGameObject* spriteGameObject = FindGameObject(spriteGameObjectId);

	if (spriteGameObject != nullptr) {
		// 中心原点の単位四角形を、資料の左上 (0, 0) から 640x360 の表示へ合わせる。
		spriteGameObject->translate = {320.0f, 180.0f, 0.0f};
		spriteGameObject->scale = {640.0f, 360.0f, 1.0f};

		for (EditorComponent& component : spriteGameObject->components) {
			if (component.type == EditorComponentType::SpriteRenderer) {
				component.assetPath = "resources/editorDefault/uvChecker.png";
			}
		}
	}

	// 初期配置そのものは利用者の操作ではないため Undo 履歴へ残さない。
	undoStack_.clear();
	redoStack_.clear();
}

int32_t EditorScene::CreateGameObject(const std::string& name) {
	// 新規 GameObject の基本値
	EditorGameObject gameObject{};
	gameObject.id = nextGameObjectId_;
	gameObject.parentId = kInvalidGameObjectId;
	gameObject.isActive = true;
	gameObject.name = name;
	gameObject.translate = {0.0f, 0.0f, 0.0f};
	gameObject.rotate = {0.0f, 0.0f, 0.0f};
	gameObject.scale = {1.0f, 1.0f, 1.0f};
	gameObject.components.push_back(CreateComponent(EditorComponentType::Transform));  // GameObject は必ず Transform Component を持つ
	nextGameObjectId_++;  // 次回生成用に ID を進める
	gameObjects_.push_back(gameObject);
	return gameObject.id;
}

int32_t EditorScene::DuplicateGameObject(int32_t gameObjectId) {
	const EditorGameObject* sourceGameObject = FindGameObject(gameObjectId);  // コピー元がなければ無効 ID を返す
	if (sourceGameObject == nullptr) {
		return kInvalidGameObjectId;
	}

	// Scene配列を変更する前にSubtree全体を値で退避し、再確保後の無効ポインタ参照を防ぐ。
	std::vector<EditorGameObject> sourceSubtree;
	std::vector<int32_t> pendingGameObjectIds{gameObjectId};

	while (!pendingGameObjectIds.empty()) {
		const int32_t currentGameObjectId = pendingGameObjectIds.back();
		pendingGameObjectIds.pop_back();
		const EditorGameObject* currentGameObject = FindGameObject(currentGameObjectId);

		if (currentGameObject == nullptr) {
			continue;
		}

		sourceSubtree.push_back(*currentGameObject);

		for (auto childIterator = currentGameObject->children.rbegin();
			childIterator != currentGameObject->children.rend();
			++childIterator) {
			pendingGameObjectIds.push_back(*childIterator);
		}
	}

	if (sourceSubtree.empty()) {
		return kInvalidGameObjectId;
	}

	std::unordered_map<int32_t, int32_t> duplicatedIds;

	for (const EditorGameObject& sourceObject : sourceSubtree) {
		duplicatedIds[sourceObject.id] = nextGameObjectId_;
		nextGameObjectId_++;
	}

	const int32_t duplicatedRootId = duplicatedIds[gameObjectId];
	gameObjects_.reserve(gameObjects_.size() + sourceSubtree.size());

	for (const EditorGameObject& sourceObject : sourceSubtree) {
		EditorGameObject duplicatedGameObject = sourceObject;
		duplicatedGameObject.id = duplicatedIds[sourceObject.id];
		duplicatedGameObject.parentId = sourceObject.id == gameObjectId
			? kInvalidGameObjectId
			: duplicatedIds[sourceObject.parentId];
		duplicatedGameObject.children.clear();
		duplicatedGameObject.name += "_Copy";

		if (sourceObject.id == gameObjectId) {
			duplicatedGameObject.translate.x += 0.2f;
		}

		gameObjects_.push_back(std::move(duplicatedGameObject));
	}

	RebuildChildren();

	return duplicatedRootId;
}

bool EditorScene::DeleteGameObject(int32_t gameObjectId) {
	// 存在しない ID は削除失敗
	if (FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	DeleteGameObjectRecursive(gameObjectId);  // 子も含めて削除してから children 配列を再構築する
	RebuildChildren();
	return true;
}

bool EditorScene::RenameGameObject(int32_t gameObjectId, const std::string& name) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);  // 空名は Hierarchy 表示が壊れるため拒否する
	if (gameObject == nullptr || name.empty()) {
		return false;
	}

	gameObject->name = name;
	return true;
}

bool EditorScene::SetParent(int32_t childId, int32_t parentId, bool preserveWorldTransform) {
	// 自分自身を親にすると循環するため拒否する
	if (childId == parentId) {
		return false;
	}

	EditorGameObject* child = FindGameObject(childId);
	if (child == nullptr) {
		return false;
	}

	// parentId が無効 ID 以外なら、実在する親だけ許可する
	if (parentId != kInvalidGameObjectId && FindGameObject(parentId) == nullptr) {
		return false;
	}

	// 子孫を親へ指定すると循環階層になるため、親候補からルートまでを検査する。
	int32_t ancestorId = parentId;
	std::unordered_set<int32_t> visitedIds;
	while (ancestorId != kInvalidGameObjectId) {
		if (ancestorId == childId || !visitedIds.insert(ancestorId).second) {
			return false;
		}

		const EditorGameObject* ancestor = FindGameObject(ancestorId);
		if (ancestor == nullptr) {
			break;
		}

		ancestorId = ancestor->parentId;
	}

	const Matrix4x4 previousWorldMatrix = preserveWorldTransform ?
		GetWorldMatrix(childId) : MakeIdentity4x4();
	const int32_t previousParentId = child->parentId;
	const Vector3 previousLocalScale = child->scale;
	const Vector3 previousLocalRotation = child->rotate;
	const Vector3 previousLocalPosition = child->translate;

	RemoveFromParent(childId);  // 既存親から外して、新しい親 ID を設定する
	child->parentId = parentId;
	RebuildChildren();

	if (preserveWorldTransform && !SetWorldMatrix(childId, previousWorldMatrix)) {
		// 逆行列またはSRT分解が成立しない親へは変更せず、元の階層とローカル値へ戻す。
		child->parentId = previousParentId;
		child->scale = previousLocalScale;
		child->rotate = previousLocalRotation;
		child->translate = previousLocalPosition;
		RebuildChildren();
		return false;
	}

	return true;
}

Matrix4x4 EditorScene::GetWorldMatrix(int32_t gameObjectId) const {
	const EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return MakeIdentity4x4();
	}

	Matrix4x4 worldMatrix = MakeAffineMatrix(
		gameObject->scale,
		gameObject->rotate,
		gameObject->translate);
	int32_t parentId = gameObject->parentId;
	int32_t traversedParentCount = 0;
	const int32_t maximumParentCount = static_cast<int32_t>(gameObjects_.size());

	while (parentId != kInvalidGameObjectId &&
		traversedParentCount < maximumParentCount) {
		const EditorGameObject* parent = FindGameObject(parentId);
		if (parent == nullptr) {
			break;
		}

		const Matrix4x4 parentLocalMatrix = MakeAffineMatrix(
			parent->scale,
			parent->rotate,
			parent->translate);
		worldMatrix = Multiply(worldMatrix, parentLocalMatrix);
		parentId = parent->parentId;
		traversedParentCount++;
	}

	return worldMatrix;
}

bool EditorScene::GetWorldTransform(
	int32_t gameObjectId,
	Vector3& worldScale,
	Vector3& worldRotation,
	Vector3& worldPosition) const {
	Matrix4x4 worldMatrix{};
	return GetWorldTransformAndMatrix(
		gameObjectId,
		worldScale,
		worldRotation,
		worldPosition,
		worldMatrix);
}

bool EditorScene::GetWorldTransformAndMatrix(
	int32_t gameObjectId,
	Vector3& worldScale,
	Vector3& worldRotation,
	Vector3& worldPosition,
	Matrix4x4& worldMatrix) const {
	if (FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	worldMatrix = GetWorldMatrix(gameObjectId);
	return DecomposeTransformMatrix(
		worldMatrix,
		worldScale,
		worldRotation,
		worldPosition);
}

bool EditorScene::SetWorldTransform(
	int32_t gameObjectId,
	const Vector3& worldScale,
	const Vector3& worldRotation,
	const Vector3& worldPosition) {
	return SetWorldMatrix(
		gameObjectId,
		MakeAffineMatrix(worldScale, worldRotation, worldPosition));
}

bool EditorScene::SetWorldMatrix(int32_t gameObjectId, const Matrix4x4& worldMatrix) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	Matrix4x4 localMatrix = worldMatrix;
	if (gameObject->parentId != kInvalidGameObjectId) {
		const Matrix4x4 parentWorldMatrix = GetWorldMatrix(gameObject->parentId);
		localMatrix = Multiply(worldMatrix, Inverse(parentWorldMatrix));
	}

	Vector3 localScale{};
	Vector3 localRotation{};
	Vector3 localPosition{};
	if (!DecomposeTransformMatrix(localMatrix, localScale, localRotation, localPosition)) {
		return false;
	}

	gameObject->scale = localScale;
	gameObject->rotate = localRotation;
	gameObject->translate = localPosition;
	return true;
}

bool EditorScene::AddComponent(int32_t gameObjectId, EditorComponentType type) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);  // 同じ種類の Component は重複追加しない
	if (gameObject == nullptr || HasComponent(gameObjectId, type)) {
		return false;
	}

	gameObject->components.push_back(CreateComponent(type));

	// メッシュ系の当たり判定は、後から Component を付けた場合もモデル配置時と同じく
	// 描画メッシュ（ModelRenderer / SkinnedMeshRenderer / MeshFilter）から自動的に流用する。
	if (type == EditorComponentType::MeshCollider ||
		type == EditorComponentType::AutoConvexCollision) {
		EditorComponent& addedCollider = gameObject->components.back();
		if (addedCollider.assetPath.empty()) {
			std::string renderModelPath;
			const EditorComponent* modelRenderer =
				EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::ModelRenderer);
			if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
				renderModelPath = modelRenderer->assetPath;
			}
			else {
				const EditorComponent* skinnedMeshRenderer =
					EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::SkinnedMeshRenderer);
				if (skinnedMeshRenderer != nullptr && !skinnedMeshRenderer->assetPath.empty()) {
					renderModelPath = skinnedMeshRenderer->assetPath;
				}
				else {
					const EditorComponent* meshFilter =
						EditorComponentUtility::FindComponent(*gameObject, EditorComponentType::MeshFilter);
					if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
						renderModelPath = meshFilter->assetPath;
					}
				}
			}

			if (!renderModelPath.empty()) {
				addedCollider.assetPath = renderModelPath;
				Vector3 modelColliderCenter{};
				Vector3 modelColliderSize{};
				if (EditorAssetUtility::GetModelColliderBounds(renderModelPath, modelColliderCenter, modelColliderSize)) {
					addedCollider.colliderCenter = modelColliderCenter;
					addedCollider.colliderSize = modelColliderSize;
				}
			}
		}
	}

	return true;
}

bool EditorScene::RemoveComponent(int32_t gameObjectId, EditorComponentType type) {
	// Transform は GameObject の基本情報なので削除禁止
	if (type == EditorComponentType::Transform) {
		return false;
	}

	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	// 指定 type に一致する Component を末尾へ寄せる
	auto removeIterator = std::remove_if(
		gameObject->components.begin(),
		gameObject->components.end(),
		[type](const EditorComponent& component) { return component.type == type; });
	bool isRemoved = removeIterator != gameObject->components.end();
	gameObject->components.erase(removeIterator, gameObject->components.end());  // remove_if で寄せた不要要素を実際に削除する

	return isRemoved;
}

bool EditorScene::HasComponent(int32_t gameObjectId, EditorComponentType type) const {
	const EditorGameObject* gameObject = FindGameObject(gameObjectId);  // 指定 ID がなければ Component も存在しない
	if (gameObject == nullptr) {
		return false;
	}

	for (const EditorComponent& component : gameObject->components) {
		// 同じ ComponentType が 1 つでもあれば true
		if (component.type == type) {
			return true;
		}
	}

	return false;
}

//============================================================
// Save / Load
//============================================================

bool EditorScene::SaveScene(const std::string& filePath) const {
	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);  // Scene を UTF-8 BOM 付きの | 区切りテキストとして保存する
	if (!file.is_open()) {
		return false;
	}

	file.write(
		reinterpret_cast<const char*>(kSceneUtf8Bom),
		static_cast<std::streamsize>(sizeof(kSceneUtf8Bom)));

	file << "PhysicsSettings|"
	     << physicsSettings_.gravity.x << "|"
	     << physicsSettings_.gravity.y << "|"
	     << physicsSettings_.gravity.z << "|"
	     << physicsSettings_.fixedTimeStep << "|"
	     << physicsSettings_.collisionStepCount << "|"
	     << physicsSettings_.drawColliderDebug << "|"
	     << physicsSettings_.drawContactDebug << "|"
	     << physicsSettings_.drawCastDebug;
	for (int32_t firstLayer = 0; firstLayer < kEditorPhysicsLayerCount; ++firstLayer) {
		for (int32_t secondLayer = 0; secondLayer < kEditorPhysicsLayerCount; ++secondLayer) {
			file << "|" << physicsSettings_.layerCollisionMatrix[firstLayer][secondLayer];
		}
	}
	file << "|" << physicsSettings_.drawVelocityDebug
	     << "|" << physicsSettings_.drawForceDirectionDebug
	     << "|" << physicsSettings_.drawFieldVolumeDebug
	     << "|" << physicsSettings_.drawConnectionDebug
	     << "|" << physicsSettings_.drawSelectedOnlyDebug
	     << "|" << physicsSettings_.debugVectorScale;
	file << "\n";

	for (const EditorGameObject& gameObject : gameObjects_) {
		// GameObject 行には ID / 親 / 名前 / Transform を保存する
		file << "GameObject|"
		     << gameObject.id << "|"
		     << gameObject.parentId << "|"
		     << EncodeSceneToken(gameObject.name) << "|"
		     << gameObject.translate.x << "|"
		     << gameObject.translate.y << "|"
		     << gameObject.translate.z << "|"
		     << gameObject.rotate.x << "|"
		     << gameObject.rotate.y << "|"
		     << gameObject.rotate.z << "|"
		     << gameObject.scale.x << "|"
		     << gameObject.scale.y << "|"
		     << gameObject.scale.z << "|"
		     << gameObject.isActive << "\n";

		if (!gameObject.prefabSourcePath.empty() || !gameObject.prefabVariantBasePath.empty()) {
			file << "PrefabLink|"
			     << gameObject.id << "|"
			     << EncodeSceneToken(gameObject.prefabSourcePath) << "|"
			     << gameObject.prefabSourceObjectId << "|"
			     << EncodeSceneToken(gameObject.prefabVariantBasePath) << "\n";
		}

		for (const EditorComponent& component : gameObject.components) {
			// Component 行には Component 種類と各 Component 共通の設定値を保存する
			file << "Component|"
			     << gameObject.id << "|"
			     << static_cast<int32_t>(component.type) << "|"
			     << component.isActive << "|"
			     << component.assetPath << "|"
			     << component.color.x << "|"
			     << component.color.y << "|"
			     << component.color.z << "|"
			     << component.intensity << "|"
			     << component.mass << "|"
			     << component.drag << "|"
			     << component.useGravity << "|"
			     << component.isKinematic << "|"
			     << component.isTrigger << "|"
			     << component.bounciness << "|"
			     << component.velocity.x << "|"
			     << component.velocity.y << "|"
			     << component.velocity.z << "|"
			     << component.colliderCenter.x << "|"
			     << component.colliderCenter.y << "|"
			     << component.colliderCenter.z << "|"
			     << component.colliderSize.x << "|"
			     << component.colliderSize.y << "|"
			     << component.colliderSize.z << "|"
			     << component.colliderRadius << "|"
			     << component.inputMoveSpeed << "|"
			     << component.inputForwardKey << "|"
			     << component.inputBackKey << "|"
			     << component.inputLeftKey << "|"
			     << component.inputRightKey << "|"
			     << component.inputJumpKey << "|"
			     << component.inputMouseSensitivity << "|"
			     << component.inputInvertY << "|"
			     << component.hapticStrength << "|"
			     << component.hapticDurationMs << "|"
			     << component.hapticLoop << "|"
			     << component.audioVolume << "|"
			     << component.audioPitch << "|"
			     << component.audioLoop << "|"
			     << component.audioPlayOnAwake << "|"
			     << component.angularVelocity.x << "|"
			     << component.angularVelocity.y << "|"
			     << component.angularVelocity.z << "|"
			     << component.angularDrag << "|"
			     << component.freezePositionX << "|"
			     << component.freezePositionY << "|"
			     << component.freezePositionZ << "|"
			     << component.freezeRotationX << "|"
			     << component.freezeRotationY << "|"
			     << component.freezeRotationZ << "|"
			     << component.interpolationMode << "|"
			     << component.collisionDetectionMode << "|"
			     << component.dynamicFriction << "|"
			     << component.staticFriction << "|"
			     << component.frictionCombineMode << "|"
			     << component.bouncinessCombineMode << "|"
			     << component.physicsLayer << "|"
			     << component.generateContactEvents << "|"
				 << component.connectedGameObjectId << "|"
				 << component.jointAxis.x << "|"
				 << component.jointAxis.y << "|"
				 << component.jointAxis.z << "|"
				 << component.jointMinLimit << "|"
				 << component.jointMaxLimit << "|"
				 << component.jointMinDistance << "|"
				 << component.jointMaxDistance << "|"
				 << component.jointSpringFrequency << "|"
				 << component.jointSpringDamping << "|"
				 << component.inputActionMapName << "|"
				 << component.inputBehavior << "|"
				 << component.inputMoveEventName << "|"
				 << component.inputJumpEventName << "|"
				 << component.inputFireEventName << "|"
				 << component.navAgentRadius << "|"
				 << component.navAgentHeight << "|"
				 << component.navMaxSpeed << "|"
				 << component.navMaxAcceleration << "|"
				 << component.navStoppingDistance << "|"
				 << component.navAutoRepath << "|"
				 << component.navCarve << "|"
				 << component.navMaxSlope << "|"
				 << component.navMaxClimb << "|"
				 << component.navAreaOverride << "|"
				 << component.navArea << "|"
				 << component.navIgnoreFromBuild << "|"
				 << component.navBidirectional << "|"
				 << component.navCostModifier << "|"
				 << component.rollingTorque << "|"
				 << component.rollingHorsepower << "|"
				 << component.constraintWeight << "|"
				 << component.constraintPositionOffset.x << "|"
				 << component.constraintPositionOffset.y << "|"
				 << component.constraintPositionOffset.z << "|"
				 << component.constraintRotationOffset.x << "|"
				 << component.constraintRotationOffset.y << "|"
				 << component.constraintRotationOffset.z << "|"
				 << component.constraintAimAxis << "|"
				 << component.constraintUpAxis << "|"
				 << component.constraintRoll << "|"
				 << component.constraintFreezeAxisX << "|"
				 << component.constraintFreezeAxisY << "|"
				 << component.constraintFreezeAxisZ << "|"
				 << component.animationSpeed << "|"
				 << component.animationLoop << "|"
				 << component.animationPlayOnAwake << "|"
				 << component.animationType << "|"
				 << component.animationAmplitude << "|"
				 << component.animatorState << "|"
				 << component.particleRate << "|"
				 << component.particleLifetime << "|"
				 << component.particleSpeed << "|"
			     << component.particleSize << "|"
			     << component.metallic << "|"
			     << component.roughness << "|"
			     << component.ior << "|"
			     << component.alpha << "|"
			     << component.reflectionStrength << "|"
			     << component.textureAssetPath << "|"
			     << component.emissionStrength << "|"
			     << component.bloomIntensity << "|"
			     << component.finalBrightness << "|"
			     << component.smaaEnabled << "|"
			     << component.taaEnabled << "|"
			     << component.ssrEnabled << "|"
			     << component.skyLowerColor.x << "|"
			     << component.skyLowerColor.y << "|"
			     << component.skyLowerColor.z << "|"
			     << component.environmentTextureRotation << "|"
			     << component.environmentTextureMipBias << "|"
			     << component.environmentTextureEnabled << "|"
			     << component.cameraFieldOfView << "|"
			     << component.cameraNearClip << "|"
			     << component.cameraFarClip << "|"
			     << component.cameraProjectionMode << "|"
			     << component.cameraDofEnabled << "|"
			     << component.cameraDofFocusDistance << "|"
			     << component.cameraDofAperture << "|"
			     << component.cameraDofFocalLength << "|"
			     << component.cameraMotionBlurEnabled << "|"
			     << component.cameraMotionBlurIntensity << "|"
			     << component.cameraExposure << "|"
			     << component.bloomThreshold << "|"
			     << component.bloomSoftKnee << "|"
			     << component.bloomScatter << "|"
			     << component.aaMode << "|"
			     << component.compositeExposure << "|"
			     << component.compositeWhitePoint << "|"
			     << component.compositeToneMappingMode << "|"
			     << component.compositeBloomIntensity << "|"
			     << component.compositeSaturation << "|"
			     << component.compositeContrast << "|"
			     << component.compositeVignetteStrength << "|"
			     << component.compositeVignetteRadius << "|"
			     << component.compositeFilmGrain << "|"
			     << component.compositeChromaticAberration << "|"
			     << component.compositeAmbientOcclusionStrength << "|"
			     << EncodeSceneToken(component.buttonLabel) << "|"
			     << component.buttonPosition.x << "|"
			     << component.buttonPosition.y << "|"
			     << component.buttonSize.x << "|"
			     << component.buttonSize.y << "|"
			     << component.buttonInteractable << "|"
			     << EncodeSceneToken(component.buttonOnClickFunction) << "|"
			     << component.buttonHoverColor.x << "|"
			     << component.buttonHoverColor.y << "|"
			     << component.buttonHoverColor.z << "|"
			     << component.buttonPressedColor.x << "|"
			     << component.buttonPressedColor.y << "|"
			     << component.buttonPressedColor.z << "|"
			     << component.toggleValue << "|"
			     << EncodeSceneToken(component.toggleOnValueChangedFunction) << "|"
			     << component.sliderValue << "|"
			     << component.sliderMinValue << "|"
			     << component.sliderMaxValue << "|"
			     << EncodeSceneToken(component.sliderOnValueChangedFunction) << "|"
			     << component.audioSpatialBlend << "|"
			     << component.audioMinDistance << "|"
			     << component.audioMaxDistance << "|"
			     << component.smaaThreshold << "|"
				     << component.smaaCornerRounding << "|"
				     << component.temporalSharpness << "|"
				     << component.temporalBlendRatio << "|"
				     << component.glareMode << "|"
				     << component.glareIntensity << "|"
				     << component.glareSize << "|"
				     << component.glareAngle << "|"
				     << component.glareStreakCount << "|"
				     << component.glareFade << "|"
				     << component.glareColorModulation << "|"
				     << component.glareCenter.x << "|"
				     << component.glareCenter.y << "|"
				     << component.glareCenter.z << "|"
				     << component.filterMode << "|"
				     << component.filterStrength << "|"
				     << EncodeSceneToken(component.normalTextureAssetPath) << "|"
				     << EncodeSceneToken(component.metallicTextureAssetPath) << "|"
				     << EncodeSceneToken(component.roughnessTextureAssetPath) << "|"
				     << EncodeSceneToken(component.ambientOcclusionTextureAssetPath) << "|"
				     << EncodeSceneToken(component.emissionTextureAssetPath) << "|"
				     << EncodeSceneToken(component.heightTextureAssetPath) << "|"
				     << EncodeSceneToken(component.opacityTextureAssetPath) << "|"
				     << component.emissionColor.x << "|"
				     << component.emissionColor.y << "|"
				     << component.emissionColor.z << "|"
				     << component.normalScale << "|"
				     << component.ambientOcclusionStrength << "|"
				     << component.heightScale << "|"
				     << component.alphaCutoff << "|"
				     << component.clearCoat << "|"
				     << component.clearCoatRoughness << "|"
				     << component.transmission << "|"
				     << component.subsurface << "|"
				     << component.anisotropy << "|"
				     << component.anisotropyRotation << "|"
				     << component.specularTint << "|"
				     << component.sheen << "|"
				     << component.sheenTint << "|"
				     << component.alphaMode << "|"
				     << component.doubleSided << "|"
				     << component.uvTiling.x << "|"
				     << component.uvTiling.y << "|"
				     << component.uvOffset.x << "|"
				     << component.uvOffset.y << "|"
				     << component.animationClipIndex << "|"
				     << component.useImportedMaterialTextures << "|"
				     << EncodeSceneToken(component.uvLayoutTextureAssetPath) << "|"
				     << component.glareModeMask << "|"
				     << component.filterModeMask;

			for (int32_t glareModeIndex = 1; glareModeIndex < kGlareModeCount; glareModeIndex++) {
				file << "|"
				     << component.glareIntensityByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareSizeByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareAngleByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareStreakCountByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareFadeByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareColorModulationByMode[static_cast<size_t>(glareModeIndex)] << "|"
				     << component.glareCenterByMode[static_cast<size_t>(glareModeIndex)].x << "|"
				     << component.glareCenterByMode[static_cast<size_t>(glareModeIndex)].y << "|"
				     << component.glareCenterByMode[static_cast<size_t>(glareModeIndex)].z << "|"
				     << component.glareColorByMode[static_cast<size_t>(glareModeIndex)].x << "|"
				     << component.glareColorByMode[static_cast<size_t>(glareModeIndex)].y << "|"
				     << component.glareColorByMode[static_cast<size_t>(glareModeIndex)].z;
			}

			for (int32_t filterModeIndex = 1; filterModeIndex < kFilterModeCount; filterModeIndex++) {
				file << "|"
				     << component.filterStrengthByMode[static_cast<size_t>(filterModeIndex)] << "|"
				     << component.filterColorByMode[static_cast<size_t>(filterModeIndex)].x << "|"
				     << component.filterColorByMode[static_cast<size_t>(filterModeIndex)].y << "|"
				     << component.filterColorByMode[static_cast<size_t>(filterModeIndex)].z;
			}

			// Animation / Effect の拡張値は既存 Scene と互換を保つため、必ず共通行の末尾へ追加する。
			file << "|" << component.animatorApplyRootMotion
			     << "|" << component.animatorAutoVelocity
			     << "|" << component.animatorTransitionDuration
			     << "|" << component.animatorMoveX
			     << "|" << component.animatorMoveY
			     << "|" << component.animatorSpeedParameter
			     << "|" << component.animatorIdleClipIndex
			     << "|" << component.animatorForwardClipIndex
			     << "|" << component.animatorBackwardClipIndex
			     << "|" << component.animatorLeftClipIndex
			     << "|" << component.animatorRightClipIndex
			     << "|" << component.particleMaxCount
			     << "|" << component.particleBurstCount
			     << "|" << component.particleShape
			     << "|" << component.particleSimulationSpace
			     << "|" << component.particleDuration
			     << "|" << component.particleStartDelay
			     << "|" << component.particleGravity
			     << "|" << component.particleDrag
			     << "|" << component.particleEndSize
			     << "|" << component.particleShapeRadius
			     << "|" << component.particleShapeAngle
			     << "|" << component.particleSpeedRandomness
			     << "|" << component.particleLifetimeRandomness
			     << "|" << component.particleSizeRandomness
			     << "|" << component.particleRotationSpeed
			     << "|" << component.particleLooping
			     << "|" << component.particleCollision
			     << "|" << component.particleEndColor.x
			     << "|" << component.particleEndColor.y
			     << "|" << component.particleEndColor.z
			     << "|" << component.particleDirection.x
			     << "|" << component.particleDirection.y
			     << "|" << component.particleDirection.z
			     << "|" << component.particleBoxSize.x
			     << "|" << component.particleBoxSize.y
			     << "|" << component.particleBoxSize.z
			     << "|" << component.particleStartAlpha
			     << "|" << component.particleEndAlpha
			     << "|" << component.particleEmissionStrength
			     << "|" << component.particleEndSpeedMultiplier
			     << "|" << component.particleNoiseStrength
			     << "|" << component.particleNoiseFrequency
			     << "|" << component.particleCollisionBounce
			     << "|" << component.particleCollisionFriction
			     << "|" << component.particlePrewarm
			     << "|" << component.freeMoveSpeed
			     << "|" << component.freeRotateSpeed
			     << "|" << component.freeMoveAxes
			     << "|" << component.freeRotateAxes
			     << "|" << component.freeUseLocalSpace
			     << "|" << component.freeRotationInput.x
			     << "|" << component.freeRotationInput.y
			     << "|" << component.freeRotationInput.z
			     << "|" << component.particleMotionType
			     << "|" << component.particleMotionCenter.x
			     << "|" << component.particleMotionCenter.y
			     << "|" << component.particleMotionCenter.z
			     << "|" << component.particleAngularSpeed
			     << "|" << component.particleRadialAcceleration
			     << "|" << component.particleWaveAmplitude
			     << "|" << component.particleWaveFrequency
			     << "|" << component.particleAttractorStrength
			     << "|" << EncodeSceneToken(component.particleRenderAssetPath)
			     << "|" << component.lightingMode
			     << "|" << component.oceanGridResolution
			     << "|" << component.oceanSize
			     << "|" << component.oceanWaveHeight
			     << "|" << component.oceanMaxWaveHeight
			     << "|" << component.oceanWaveLength
			     << "|" << component.oceanWaveSpeed
			     << "|" << component.oceanTimeScale
			     << "|" << component.oceanChoppiness
			     << "|" << component.oceanPrimaryDirection.x
			     << "|" << component.oceanPrimaryDirection.y
			     << "|" << component.oceanSecondaryDirection.x
			     << "|" << component.oceanSecondaryDirection.y
			     << "|" << component.oceanSecondaryWaveScale
			     << "|" << component.oceanRippleScale
			     << "|" << component.oceanRippleStrength
			     << "|" << component.oceanFoamStrength
			     << "|" << component.oceanRoughness
			     << "|" << component.oceanReflectionStrength
			     << "|" << component.oceanShallowColor.x
			     << "|" << component.oceanShallowColor.y
			     << "|" << component.oceanShallowColor.z
			     << "|" << component.oceanDeepColor.x
			     << "|" << component.oceanDeepColor.y
			     << "|" << component.oceanDeepColor.z
			     << "|" << component.buoyancyOceanGameObjectId
			     << "|" << component.buoyancyCenterOffset.x
			     << "|" << component.buoyancyCenterOffset.y
			     << "|" << component.buoyancyCenterOffset.z
			     << "|" << component.buoyancyHullSize.x
			     << "|" << component.buoyancyHullSize.y
			     << "|" << component.buoyancyHullSize.z
			     << "|" << component.buoyancyStrength
			     << "|" << component.buoyancyMaxSubmersion
			     << "|" << component.buoyancyDamping
			     << "|" << component.buoyancyWaterDrag
			     << "|" << component.buoyancyAngularDrag
			     << "|" << component.buoyancyNormalInfluence
			     << "|" << (component.buoyancyUseCenterPoint ? 1 : 0)
			     << "|" << component.oceanWindSpeed
			     << "|" << component.oceanWaterDepth
			     << "|" << component.oceanDirectionSpread
			     << "|" << component.oceanSwellStrength
			     << "|" << component.oceanSpectrumSeed
			     << "|" << component.oceanCrestSharpness
			     << "|" << component.oceanFoamThreshold
			     << "|" << component.oceanDetailNormalStrength
			     << "|" << component.oceanAbsorptionDistance
			     << "|" << component.oceanRefractionDistortion
			     << "|" << component.railPathGameObjectId
			     << "|" << component.railSpeed
			     << "|" << component.railStartNormalized
			     << "|" << component.railLookAheadDistance
			     << "|" << (component.railLoop ? 1 : 0)
			     << "|" << (component.railOrientToPath ? 1 : 0)
			     << "|" << (component.railUseSmoothCurve ? 1 : 0)
			     << "|" << component.healthMaximum
			     << "|" << component.healthCurrent
			     << "|" << component.enemySpawnFollowerGameObjectId
			     << "|" << component.enemySpawnNormalized
			     << "|" << component.enemyAttackTargetGameObjectId
			     << "|" << component.enemyAttackInterval
			     << "|" << component.enemyAttackRange
			     << "|" << component.enemyAttackDamage
			     << "|" << component.enemyProjectileTemplateGameObjectId
			     << "|" << component.enemyProjectilePoolSize
			     << "|" << component.enemyProjectileSpeed
			     << "|" << component.enemyProjectileHitRadius
			     << "|" << component.enemyProjectileLifetime
			     << "|" << component.railShipSpeedSourceGameObjectId
			     << "|" << component.railShipSailGameObjectId
			     << "|" << component.railShipWakeEffectGameObjectId
			     << "|" << component.railShipWindEffectGameObjectId
			     << "|" << component.railShipEffectStartSpeed
			     << "|" << component.railShipEffectFullSpeed
			     << "|" << component.railShipSailMinimumSpeed
			     << "|" << component.railShipSailMaximumSpeed
			     << "|" << component.enemyMotionPattern
			     << "|" << component.enemyMotionAmplitude.x
			     << "|" << component.enemyMotionAmplitude.y
			     << "|" << component.enemyMotionAmplitude.z
			     << "|" << component.enemyMotionFrequency
			     << "|" << component.enemyMotionPhase
			     << "|" << component.enemyMotionTargetGameObjectId
			     << "|" << component.enemyMotionSpeed
			     << "|" << (component.enemyMotionLookAtTarget ? 1 : 0)
			     << "|" << component.stageFollowerGameObjectId
			     << "|" << component.stageStartMarkerGameObjectId
			     << "|" << component.stageGoalMarkerGameObjectId
			     << "|" << component.stageStartEffectGameObjectId
			     << "|" << component.stageGoalEffectGameObjectId
			     << "|" << component.stageStartDelay
			     << "|" << component.stageGoalRadius
			     << "|" << component.stageGoalDelay
			     << "|" << EncodeSceneToken(component.stageNextScenePath)
			     << "|" << EncodeSceneToken(component.stageSelectScenePath)
			     << "|" << EncodeSceneToken(component.sceneButtonScenePath)
			     << "|" << component.audioBus
			     << "|" << component.audioMaxVoices
			     << "|" << component.audioRetriggerInterval
			     << "|" << (component.compositeAutoExposureEnabled ? 1 : 0)
			     << "|" << component.compositeMinimumExposure
			     << "|" << component.compositeMaximumExposure
			     << "|" << component.compositeExposureAdaptationSpeed
			     << "|" << component.compositeTargetLuminance
			     << "|" << component.compositeTemperature
			     << "|" << component.compositeTint
			     << "|" << component.compositeLift.x
			     << "|" << component.compositeLift.y
			     << "|" << component.compositeLift.z
			     << "|" << component.compositeGamma
			     << "|" << component.compositeGain.x
			     << "|" << component.compositeGain.y
			     << "|" << component.compositeGain.z
			     << "|" << component.audioDopplerLevel
			     << "|" << component.audioSpread
			     << "|" << component.audioConeInnerAngle
			     << "|" << component.audioConeOuterAngle
			     << "|" << component.audioConeOuterVolume
			     << "|" << component.audioOcclusionStrength
			     << "|" << component.audioReverbSend
			     << "|" << component.audioReflectionStrength
			     << "|" << component.enemyWaveIndex
			     << "|" << component.enemyFormationPattern
			     << "|" << component.enemyFormationSlot
			     << "|" << component.enemyFormationSpacing
			     << "|" << component.railAimMouseSensitivity
			     << "|" << component.railAimGamepadSensitivity
			     << "|" << component.railAimAssistRadius
			     << "|" << (component.railAimInvertY ? 1 : 0)
			     << "|" << component.railEventFollowerGameObjectId
			     << "|" << component.railEventNormalized
			     << "|" << component.railEventType
			     << "|" << component.railEventTargetGameObjectId
			     << "|" << component.railEventDuration
			     << "|" << EncodeSceneToken(component.railEventText)
			     << "|" << (component.railEventPauseRail ? 1 : 0)
			     << "|" << component.bossPhaseTwoHealthRatio
			     << "|" << component.bossPhaseThreeHealthRatio
			     << "|" << component.bossPhaseOneMotionPattern
			     << "|" << component.bossPhaseTwoMotionPattern
			     << "|" << component.bossPhaseThreeMotionPattern
			     << "|" << component.bossPhaseOneAttackInterval
			     << "|" << component.bossPhaseTwoAttackInterval
			     << "|" << component.bossPhaseThreeAttackInterval
			     << "|" << component.railHudBindingType
			     << "|" << component.railHudSourceGameObjectId
			     << "|" << component.railAcceleration
			     << "|" << component.railDeceleration
			     << "|" << (component.railStartPaused ? 1 : 0)
			     << "|" << (component.railReverse ? 1 : 0)
			     << "|" << (component.railStopAtEnd ? 1 : 0)
			     << "|" << component.waveTriggerMode
			     << "|" << component.waveTriggerSourceGameObjectId
			     << "|" << component.waveTriggerValue
			     << "|" << component.waveSpawnInterval
			     << "|" << (component.waveDeactivateChildrenOnStart ? 1 : 0)
			     << "|" << component.timelineSourceMode
			     << "|" << component.timelineSourceGameObjectId
			     << "|" << component.timelineTriggerValue
			     << "|" << component.timelineTargetGameObjectId
			     << "|" << EncodeSceneToken(component.timelineActionName)
			     << "|" << (component.timelineTriggerOnce ? 1 : 0)
			     << "|" << component.thresholdSourceMode
			     << "|" << component.thresholdSourceGameObjectId
			     << "|" << component.thresholdSecondValue
			     << "|" << component.thresholdThirdValue
			     << "|" << EncodeSceneToken(component.thresholdFirstActionName)
			     << "|" << EncodeSceneToken(component.thresholdSecondActionName)
			     << "|" << EncodeSceneToken(component.thresholdThirdActionName)
			     << "|" << component.uiBindingSourceGameObjectId
			     << "|" << component.uiBindingValueType
			     << "|" << EncodeSceneToken(component.uiBindingPrefix)
			     << "|" << component.uiBindingPrecision
			     << "|" << component.uiBindingScale
			     << "|" << component.waveActionTargetGameObjectId
			     << "|" << EncodeSceneToken(component.waveStartedActionName)
			     << "|" << EncodeSceneToken(component.waveSpawnedActionName)
			     << "|" << EncodeSceneToken(component.waveCompletedActionName)
			     << "|" << component.thresholdTargetGameObjectId
			     << "|" << component.buoyancyLateralDrag
			     << "|" << component.buoyancyVerticalDrag
			     << "|" << component.buoyancySlammingStrength
			     << "|" << component.railMovementMode
			     << "|" << component.railPositionInfluence.x
			     << "|" << component.railPositionInfluence.y
			     << "|" << component.railPositionInfluence.z
			     << "|" << component.railRotationInfluence.x
			     << "|" << component.railRotationInfluence.y
			     << "|" << component.railRotationInfluence.z
			     << "|" << component.railPositionSpring
			     << "|" << component.railPositionDamping
			     << "|" << component.railMaximumAcceleration
			     << "|" << component.railRotationSpring
			     << "|" << component.railRotationDamping
			     << "|" << component.railMaximumAngularAcceleration
			     << "|" << component.aerodynamicAirDensity
			     << "|" << component.aerodynamicDragCoefficient
			     << "|" << component.aerodynamicReferenceArea
			     << "|" << component.aerodynamicBaseLiftCoefficient
			     << "|" << component.aerodynamicLiftSlope
			     << "|" << component.aerodynamicLiftArea
			     << "|" << component.aerodynamicSideForceCoefficient
			     << "|" << component.aerodynamicSideArea
			     << "|" << component.aerodynamicZeroLiftAngleDegrees
			     << "|" << component.aerodynamicStallAngleDegrees
			     << "|" << component.aerodynamicAngularDragCoefficient
			     << "|" << component.aerodynamicMagnusCoefficient
			     << "|" << component.aerodynamicCenterOfPressure.x
			     << "|" << component.aerodynamicCenterOfPressure.y
			     << "|" << component.aerodynamicCenterOfPressure.z
			     << "|" << component.aerodynamicAmbientWindVelocity.x
			     << "|" << component.aerodynamicAmbientWindVelocity.y
			     << "|" << component.aerodynamicAmbientWindVelocity.z
			     << "|" << component.aerodynamicMaximumForce
			     << "|" << component.windZoneMode
			     << "|" << component.windZoneDirection.x
			     << "|" << component.windZoneDirection.y
			     << "|" << component.windZoneDirection.z
			     << "|" << component.windZoneSpeed
			     << "|" << component.windZoneRadius
			     << "|" << component.windZoneTurbulenceStrength
			     << "|" << component.windZoneTurbulenceFrequency
			     << "|" << component.gravityFieldMode
			     << "|" << component.gravityFieldGravitationalConstant
			     << "|" << component.gravityFieldSourceMass
			     << "|" << component.gravityFieldAcceleration
			     << "|" << component.gravityFieldMinimumDistance
			     << "|" << component.gravityFieldInfluenceRadius
			     << "|" << component.gravityFieldMaximumAcceleration
			     << "|" << component.rotatingFrameAngularVelocity.x
			     << "|" << component.rotatingFrameAngularVelocity.y
			     << "|" << component.rotatingFrameAngularVelocity.z
			     << "|" << component.rotatingFrameAngularAcceleration.x
			     << "|" << component.rotatingFrameAngularAcceleration.y
			     << "|" << component.rotatingFrameAngularAcceleration.z
			     << "|" << component.rotatingFrameLinearVelocity.x
			     << "|" << component.rotatingFrameLinearVelocity.y
			     << "|" << component.rotatingFrameLinearVelocity.z
			     << "|" << component.rotatingFrameRadius
			     << "|" << component.rotatingFrameMaximumAcceleration
			     << "|" << component.fluidVolumeSize.x
			     << "|" << component.fluidVolumeSize.y
			     << "|" << component.fluidVolumeSize.z
			     << "|" << component.fluidDensity
			     << "|" << component.fluidDynamicViscosity
			     << "|" << component.fluidDragCoefficient
			     << "|" << component.fluidFlowVelocity.x
			     << "|" << component.fluidFlowVelocity.y
			     << "|" << component.fluidFlowVelocity.z
			     << "|" << component.fluidAngularViscosity
			     << "|" << component.fluidMaximumForce
			     << "|" << component.springForceTargetGameObjectId
			     << "|" << component.springForceLocalAnchor.x
			     << "|" << component.springForceLocalAnchor.y
			     << "|" << component.springForceLocalAnchor.z
			     << "|" << component.springForceTargetLocalAnchor.x
			     << "|" << component.springForceTargetLocalAnchor.y
			     << "|" << component.springForceTargetLocalAnchor.z
			     << "|" << component.springForceWorldAnchor.x
			     << "|" << component.springForceWorldAnchor.y
			     << "|" << component.springForceWorldAnchor.z
			     << "|" << component.springForceRestLength
			     << "|" << component.springForceStiffness
			     << "|" << component.springForceDamping
			     << "|" << component.springForceMaximumForce
			     << "|" << (component.springForceApplyReaction ? 1 : 0)
			     << "|" << component.electromagneticCharge
			     << "|" << component.electromagneticMagneticMoment.x
			     << "|" << component.electromagneticMagneticMoment.y
			     << "|" << component.electromagneticMagneticMoment.z
			     << "|" << component.electromagneticMaximumForce
			     << "|" << component.electromagneticMaximumTorque
			     << "|" << component.electromagneticFieldMode
			     << "|" << component.electromagneticElectricField.x
			     << "|" << component.electromagneticElectricField.y
			     << "|" << component.electromagneticElectricField.z
			     << "|" << component.electromagneticMagneticField.x
			     << "|" << component.electromagneticMagneticField.y
			     << "|" << component.electromagneticMagneticField.z
			     << "|" << component.electromagneticSourceCharge
			     << "|" << component.electromagneticCoulombConstant
			     << "|" << component.electromagneticMinimumDistance
			     << "|" << component.electromagneticInfluenceRadius;

			file << "\n";

			// 追加物理は独立行へ保存し、既存Component行の列位置と旧Scene互換性を維持する。
			const bool savesPhysicsExtension =
				component.type == EditorComponentType::RigidBody ||
				component.type == EditorComponentType::AutoConvexCollision ||
				component.type == EditorComponentType::RopeConstraint ||
				component.type == EditorComponentType::TorsionSpring ||
				component.type == EditorComponentType::Thruster;

			if (savesPhysicsExtension) {
				file << "PhysicsExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.inertiaMultiplier
				     << "|" << (component.applyGyroscopicForce ? 1 : 0)
				     << "|" << component.ropeTargetGameObjectId
				     << "|" << component.ropeLocalAnchor.x
				     << "|" << component.ropeLocalAnchor.y
				     << "|" << component.ropeLocalAnchor.z
				     << "|" << component.ropeTargetLocalAnchor.x
				     << "|" << component.ropeTargetLocalAnchor.y
				     << "|" << component.ropeTargetLocalAnchor.z
				     << "|" << component.ropeWorldAnchor.x
				     << "|" << component.ropeWorldAnchor.y
				     << "|" << component.ropeWorldAnchor.z
				     << "|" << component.ropeMaximumLength
				     << "|" << component.ropeStiffness
				     << "|" << component.ropeDamping
				     << "|" << component.ropeMaximumTension
				     << "|" << component.ropeBreakingTension
				     << "|" << (component.ropeApplyReaction ? 1 : 0)
				     << "|" << component.torsionTargetGameObjectId
				     << "|" << component.torsionRestRotation.x
				     << "|" << component.torsionRestRotation.y
				     << "|" << component.torsionRestRotation.z
				     << "|" << component.torsionStiffness
				     << "|" << component.torsionDamping
				     << "|" << component.torsionMaximumTorque
				     << "|" << (component.torsionApplyReaction ? 1 : 0)
				     << "|" << component.thrusterDirection.x
				     << "|" << component.thrusterDirection.y
				     << "|" << component.thrusterDirection.z
				     << "|" << component.thrusterLocalApplicationPoint.x
				     << "|" << component.thrusterLocalApplicationPoint.y
				     << "|" << component.thrusterLocalApplicationPoint.z
				     << "|" << component.thrusterForce
				     << "|" << component.thrusterThrottle
				     << "|" << (component.thrusterUseLocalDirection ? 1 : 0)
				     << "|" << component.centerOfMassOffset.x
				     << "|" << component.centerOfMassOffset.y
				     << "|" << component.centerOfMassOffset.z
				     << "|" << component.autoConvexMaximumHulls
				     << "|" << (component.automaticMassFromCollider ? 1 : 0)
				     << "|" << component.bodyDensity
				     << "\n";
			}

			if (component.type == EditorComponentType::Buoyancy) {
				file << "BuoyancyPhysicalExtension"
				     << "|" << gameObject.id
				     << "|" << (component.buoyancyAutomaticPhysicalProperties ? 1 : 0)
				     << "|" << component.buoyancyWaterDensity
				     << "|" << component.buoyancyTargetSubmersionRatio
				     << "\n";
			}

			const bool savesPhysicsExtension2 =
				component.type == EditorComponentType::PulleyConstraint ||
				component.type == EditorComponentType::PhysicsServo ||
				component.type == EditorComponentType::VortexField ||
				component.type == EditorComponentType::PressureField;

			if (savesPhysicsExtension2) {
				file << "PhysicsExtension2"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.pulleyTargetGameObjectId
				     << "|" << component.pulleyOwnerLocalAnchor.x
				     << "|" << component.pulleyOwnerLocalAnchor.y
				     << "|" << component.pulleyOwnerLocalAnchor.z
				     << "|" << component.pulleyTargetLocalAnchor.x
				     << "|" << component.pulleyTargetLocalAnchor.y
				     << "|" << component.pulleyTargetLocalAnchor.z
				     << "|" << component.pulleyOwnerWorldSupport.x
				     << "|" << component.pulleyOwnerWorldSupport.y
				     << "|" << component.pulleyOwnerWorldSupport.z
				     << "|" << component.pulleyTargetWorldSupport.x
				     << "|" << component.pulleyTargetWorldSupport.y
				     << "|" << component.pulleyTargetWorldSupport.z
				     << "|" << component.pulleyTotalLength
				     << "|" << component.pulleyRatio
				     << "|" << component.pulleyStiffness
				     << "|" << component.pulleyDamping
				     << "|" << component.pulleyMaximumTension
				     << "|" << component.pulleyBreakingTension
				     << "|" << component.servoTargetGameObjectId
				     << "|" << component.servoTargetPosition.x
				     << "|" << component.servoTargetPosition.y
				     << "|" << component.servoTargetPosition.z
				     << "|" << component.servoTargetRotation.x
				     << "|" << component.servoTargetRotation.y
				     << "|" << component.servoTargetRotation.z
				     << "|" << component.servoPositionStiffness
				     << "|" << component.servoPositionDamping
				     << "|" << component.servoMaximumForce
				     << "|" << component.servoRotationStiffness
				     << "|" << component.servoRotationDamping
				     << "|" << component.servoMaximumTorque
				     << "|" << (component.servoApplyReaction ? 1 : 0)
				     << "|" << component.vortexAxis.x
				     << "|" << component.vortexAxis.y
				     << "|" << component.vortexAxis.z
				     << "|" << component.vortexRadius
				     << "|" << component.vortexAngularVelocity
				     << "|" << component.vortexRadialInflowVelocity
				     << "|" << component.vortexAxialVelocity
				     << "|" << component.vortexVelocityCoupling
				     << "|" << component.vortexMaximumAcceleration
				     << "|" << component.pressureFieldPressure
				     << "|" << component.pressureFieldRadius
				     << "|" << component.pressureFieldFalloffExponent
				     << "|" << component.pressureFieldMaximumForce
				     << "\n";
			}

			const bool savesPhysicsExtension3 =
				component.type == EditorComponentType::Suspension ||
				component.type == EditorComponentType::UprightStabilizer;

			if (savesPhysicsExtension3) {
				file << "PhysicsExtension3"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.suspensionLocalAnchor.x
				     << "|" << component.suspensionLocalAnchor.y
				     << "|" << component.suspensionLocalAnchor.z
				     << "|" << component.suspensionLocalDirection.x
				     << "|" << component.suspensionLocalDirection.y
				     << "|" << component.suspensionLocalDirection.z
				     << "|" << component.suspensionRestLength
				     << "|" << component.suspensionMaximumLength
				     << "|" << component.suspensionWheelRadius
				     << "|" << component.suspensionStiffness
				     << "|" << component.suspensionDamping
				     << "|" << component.suspensionMaximumForce
				     << "|" << (component.suspensionUseHitNormal ? 1 : 0)
				     << "|" << (component.suspensionApplyReaction ? 1 : 0)
				     << "|" << component.uprightLocalUpAxis.x
				     << "|" << component.uprightLocalUpAxis.y
				     << "|" << component.uprightLocalUpAxis.z
				     << "|" << component.uprightTargetWorldUp.x
				     << "|" << component.uprightTargetWorldUp.y
				     << "|" << component.uprightTargetWorldUp.z
				     << "|" << component.uprightStiffness
				     << "|" << component.uprightDamping
				     << "|" << component.uprightMaximumTorque
				     << "\n";
			}

			// RailFollower の横・縦移動設定は独立行にして旧 Scene の Component 列を維持する。
			if (component.type == EditorComponentType::RailMovement) {
				file << "RailMovementExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.railMovementRange.x
				     << "|" << component.railMovementRange.y
				     << "|" << component.railStartOffset.x
				     << "|" << component.railStartOffset.y
				     << "|" << component.railOffsetMoveSpeed
				     << "|" << (component.railUsePlayerInput ? 1 : 0)
				     << "|" << EncodeSceneToken(component.railInputActionMapName)
				     << "|" << EncodeSceneToken(component.railInputActionName)
				     << "|" << component.railLocalForwardAxis
				     << "|" << (component.railShipHorizontalThrust ? 1 : 0)
				     << "|" << component.railShipLateralAssist
				     << "|" << component.railMaximumRollAngle
				     << "|" << component.railRollRestorationStrength
				     << "|" << component.railRollDamping
				     << "|" << component.railMaximumPitchAngle
				     << "|" << component.railPitchRestorationStrength
				     << "|" << component.railPitchDamping
				     << "|" << component.railMaximumYawAngle
				     << "|" << component.railYawRestorationStrength
				     << "|" << component.railYawDamping
				     << "|" << (component.railYawSafetyAssistEnabled ? 1 : 0)
				     << "|" << component.railYawSafetyStage1Degrees
				     << "|" << component.railYawSafetyStage2Degrees
				     << "|" << component.railYawSafetyStage4Degrees
				     << "|" << component.railYawSafetyMaxRestorationScale
				     << "|" << component.railYawSafetyMinSpeedScale
				     << "|" << component.railMaxForwardRecoveryError
				     << "|" << (component.railAttitudeSafetyAssistEnabled ? 1 : 0)
				     << "|" << component.railRollFreeDegrees
				     << "|" << component.railRollEmergencyDegrees
				     << "|" << component.railPitchFreeDegrees
				     << "|" << component.railPitchEmergencyDegrees
				     << "|" << component.railAttitudeSafetyStrength
				     << "|" << component.railAttitudeSafetyDamping
				     << "|" << component.railAttitudeSafetyMaxTorque
				     << "|" << component.railAttitudeSafetyMinForwardScale
				     << "|" << component.railPhysicalCatchupSpeedMultiplier
				     << "|" << (component.railAttitudeAngleLimitEnabled ? 1 : 0)
				     << "|" << component.railAttitudeAngleLimitMaxPitchDegrees
				     << "|" << component.railAttitudeAngleLimitMaxRollDegrees
				     << "|" << component.railAttitudeAngleSoftLimitStrength
				     << "|" << component.railAttitudeAngleSoftLimitDamping
				     << "|" << component.railAttitudeAngleSoftLimitMaxTorque
				     << "|" << (component.railAttitudeAngleSoftLimitEnabled ? 1 : 0)
				     << "|" << component.railEngineSpeedGain
				     << "|" << component.railEngineAccelResponse
				     << "|" << component.railEngineDecelResponse
				     << "|" << component.railEngineMaxAcceleration
				     << "|" << component.railSteeringBaseLookAheadDistance
				     << "|" << component.railSteeringLookAheadTime
				     << "|" << component.railSteeringYawGain
				     << "|" << component.railSteeringYawDamping
				     << "|" << component.railSteeringMaxYawAngularAcceleration
				     << "|" << component.railLateralAssistDeadZone
				     << "|" << component.railLateralAssistSoftRadius
				     << "|" << component.railLateralAssistEmergencyRadius
				     << "|" << component.railLateralAssistMaxMultiplier
				     << "|" << (component.railHullLateralGripEnabled ? 1 : 0)
				     << "|" << component.railHullLateralGripStrength
				     << "|" << component.railHullLateralGripMaxAcceleration
				     << "|" << component.railHullLateralGripMinSpeed
				     << "|" << component.railHullLateralGripFullSpeed
				     << "|" << component.railHullLateralGripDeadZoneSpeed
				     << "|" << component.railHullLateralGripSlipStartDegrees
				     << "|" << component.railHullLateralGripSlipFullDegrees
				     << "|" << component.railMode2MaxCombinedAcceleration
				     << "|" << component.railMode2MovementStyle
				     << "|" << component.railRideYawSampleDistance
				     << "\n";
			}

			if (component.type == EditorComponentType::Camera ||
				component.type == EditorComponentType::CinemachineCamera) {
				file << "CameraFollowExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.cameraFollowPositionSpace
				     << "|" << component.cameraFollowRotationMode
				     << "\n";
			}

			// 最終合成の追加機能は独立行へ保存し、既存Sceneの巨大なComponent列をずらさない。
			if (component.type == EditorComponentType::PostProcess ||
				component.type == EditorComponentType::Volume) {
				file << "PostProcessVisualExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.compositeLocalContrast
				     << "|" << component.compositeOutputDither
				     << "|" << (component.compositeSsgiEnabled ? 1 : 0)
				     << "|" << component.compositeSsgiIntensity
				     << "|" << component.compositeSsgiRadiusPixels
				     << "|" << EncodeSceneToken(component.compositeColorLutAssetPath)
				     << "|" << component.compositeColorLutStrength
				     << "\n";
			}

			// 高度材質は独立行へ保存し、既存Renderer列とMaterial定数バッファの互換性を維持する。
			if (component.type == EditorComponentType::ModelRenderer ||
				component.type == EditorComponentType::SkinnedMeshRenderer) {
				file << "MaterialVisualExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.materialThickness
				     << "|" << component.materialWetness
				     << "|" << component.materialWaterlineHeight
				     << "|" << component.materialWaterlineWidth
				     << "\n";
			}

			// 体積雲設定も独立行にし、旧Environment列をそのまま読めるようにする。
			if (component.type == EditorComponentType::Environment) {
				file << "EnvironmentCloudExtension"
				     << "|" << gameObject.id
				     << "|" << (component.volumetricCloudEnabled ? 1 : 0)
				     << "|" << component.volumetricCloudCoverage
				     << "|" << component.volumetricCloudDensity
				     << "|" << component.volumetricCloudScale
				     << "|" << component.volumetricCloudSpeed
				     << "|" << component.volumetricCloudHeight
				     << "|" << component.volumetricCloudThickness
				     << "|" << component.volumetricCloudLightAbsorption
				     << "|" << component.volumetricCloudSilverLining
				     << "|" << component.volumetricCloudColor.x
				     << "|" << component.volumetricCloudColor.y
				     << "|" << component.volumetricCloudColor.z
				     << "\n";
				file << "EnvironmentHeatExtension"
				     << "|" << gameObject.id
				     << "|" << component.environmentHeatIntensity
				     << "|" << component.environmentHeatHorizonCenter
				     << "|" << component.environmentHeatHorizonWidth
				     << "|" << component.environmentHeatSunInfluence
				     << "|" << component.environmentHeatDistortionScale
				     << "\n";
			}

			// Sun の方位角/高度/色温度は独立行にし、旧 Scene の巨大な Component 列を維持する。
			if (component.type == EditorComponentType::Light) {
				file << "SunSystemExtension"
				     << "|" << gameObject.id
				     << "|" << component.sunAzimuthDegrees
				     << "|" << component.sunElevationDegrees
				     << "|" << (component.sunUseAzimuthElevation ? 1 : 0)
				     << "|" << component.sunTemperatureKelvin
				     << "|" << (component.sunUseColorTemperature ? 1 : 0)
				     << "|" << (component.sunAutoTemperatureFromElevation ? 1 : 0)
				     << "\n";
			}

			// Ocean の SUN Lighting / Glitter 調整項目は独立行にし、旧 Scene の巨大な Component 列を維持する。
			if (component.type == EditorComponentType::Ocean) {
				file << "OceanSunLightingExtension"
				     << "|" << gameObject.id
				     << "|" << component.oceanSunDiffuseInfluence
				     << "|" << component.oceanSunSpecularInfluence
				     << "|" << component.oceanSunGlitterInfluence
				     << "|" << component.oceanSkyReflectionInfluence
				     << "|" << component.oceanAmbientInfluence
				     << "|" << component.oceanDiffuseFloor
				     << "|" << component.oceanGlitterIntensity
				     << "|" << component.oceanGlitterSharpness
				     << "|" << component.oceanGlitterDensity
				     << "|" << component.oceanGlitterThreshold
				     << "|" << component.oceanGlitterMaxClamp
				     << "\n";
				file << "OceanShapeLightingExtension"
				     << "|" << gameObject.id
				     << "|" << component.oceanMacroReflectionInfluence
				     << "|" << component.oceanCurvatureInfluence
				     << "|" << component.oceanTroughOcclusionStrength
				     << "|" << component.oceanCrestHazeStrength
				     << "|" << component.oceanCrestDetailBoost
				     << "|" << component.oceanSlopeRefractionInfluence
				     << "|" << component.oceanMediumWaveStrength
				     << "|" << component.oceanWaveColorSeparation
				     << "|" << component.oceanShapeRoughnessVariation
				     << "|" << component.oceanDetailFilterSharpness
				     << "|" << component.oceanGrazingShapeVisibility
				     << "\n";
			}

			// Wave の生成元と編隊設定は独立行にし、旧 Scene の巨大な Component 列を維持する。
			if (component.type == EditorComponentType::WaveSpawner) {
				file << "WaveSpawnerExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.waveSpawnSourceMode
				     << "|" << component.wavePoolGameObjectId
				     << "|" << component.waveSpawnPointGameObjectId
				     << "|" << component.waveSpawnCount
				     << "|" << component.waveFormationPattern
				     << "|" << component.waveFormationSpacing
				     << "|" << component.waveFormationColumns
<< "|" << component.waveCompletionMode
				     << "|" << EncodeSceneToken(component.waveAllDefeatedActionName)
				     << "|" << component.waveSpawnMaximumPerFrame
				     << "|" << component.waveSpawnRailStartNormalized
				     << "\n";
			}

			// 可変長Gameplay設定は専用行へ保存し、既存Component列の互換性を維持する。
			if (component.type == EditorComponentType::MultiTargetLock) {
				file << "MultiTargetLockExtension"
				     << "|" << gameObject.id
				     << "|" << component.multiTargetLockSelectorGameObjectId
				     << "|" << component.multiTargetLockMaximumCount
				     << "|" << component.multiTargetLockSecondsPerTarget
				     << "|" << component.multiTargetLockLostGraceSeconds
				     << "|" << (component.multiTargetLockAutoAcquire ? 1 : 0)
				     << "|" << component.multiTargetLockActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.multiTargetLockAddedActionName)
				     << "|" << EncodeSceneToken(component.multiTargetLockCompletedActionName)
				     << "|" << EncodeSceneToken(component.multiTargetLockLostActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::WorldTargetMarker ||
				component.type == EditorComponentType::OffScreenIndicator) {
				file << "TargetMarkerExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.targetMarkerTargetGameObjectId
				     << "|" << component.targetMarkerSelectorGameObjectId
				     << "|" << component.targetMarkerLockGameObjectId
				     << "|" << component.targetMarkerMultiLockIndex
				     << "|" << component.targetMarkerWorldOffset.x
				     << "|" << component.targetMarkerWorldOffset.y
				     << "|" << component.targetMarkerWorldOffset.z
				     << "|" << component.targetMarkerScreenOffset.x
				     << "|" << component.targetMarkerScreenOffset.y
				     << "|" << component.targetMarkerEdgePadding
				     << "|" << (component.targetMarkerHideBehindCamera ? 1 : 0)
				     << "|" << (component.targetMarkerOnlyWhenLocked ? 1 : 0)
				     << "|" << (component.targetMarkerRotateToDirection ? 1 : 0)
				     << "\n";
			}

			if (component.type == EditorComponentType::AttributeSet) {
				file << "AttributeSetExtension"
				     << "|" << gameObject.id
				     << "|" << component.attributeSetActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.attributeSetChangedActionName)
				     << "|" << component.attributeSetEntries.size();

				for (const EditorNamedAttributeEntry& entry : component.attributeSetEntries) {
					file << "|" << EncodeSceneToken(entry.name)
					     << "|" << entry.minimum
					     << "|" << entry.maximum
					     << "|" << entry.current
					     << "|" << entry.regenerationPerSecond;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::GenericCounter) {
				file << "GenericCounterExtension"
				     << "|" << gameObject.id
				     << "|" << EncodeSceneToken(component.counterName)
				     << "|" << component.counterInitialValue
				     << "|" << component.counterMinimumValue
				     << "|" << component.counterMaximumValue
				     << "|" << component.counterThresholdValue
				     << "|" << component.counterCompareMode
				     << "|" << (component.counterFireOnce ? 1 : 0)
				     << "|" << component.counterActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.counterChangedActionName)
				     << "|" << EncodeSceneToken(component.counterThresholdActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::GenericCondition) {
				file << "GenericConditionExtension"
				     << "|" << gameObject.id
				     << "|" << component.conditionSourceGameObjectId
				     << "|" << component.conditionSourceType
				     << "|" << EncodeSceneToken(component.conditionComponentName)
				     << "|" << EncodeSceneToken(component.conditionPropertyName)
				     << "|" << component.conditionCompareMode
				     << "|" << component.conditionCompareFloat
				     << "|" << EncodeSceneToken(component.conditionCompareString)
				     << "|" << (component.conditionEvaluateEveryFrame ? 1 : 0)
				     << "|" << (component.conditionFireOnChangeOnly ? 1 : 0)
				     << "|" << component.conditionActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.conditionTrueActionName)
				     << "|" << EncodeSceneToken(component.conditionFalseActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::GameplayData) {
				file << "GameplayDataExtension"
				     << "|" << gameObject.id
				     << "|" << EncodeSceneToken(component.gameplayDataAssetPath)
				     << "|" << component.gameplayDataEntries.size();

				for (const EditorGameplayDataEntry& entry : component.gameplayDataEntries) {
					file << "|" << EncodeSceneToken(entry.key)
					     << "|" << entry.type
					     << "|" << EncodeSceneToken(entry.value);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::AreaDamage) {
				file << "AreaDamageExtension"
				     << "|" << gameObject.id
				     << "|" << component.areaDamageRadius
				     << "|" << component.areaDamageBaseDamage
				     << "|" << component.areaDamageMinimumMultiplier
				     << "|" << component.areaDamageImpulse
				     << "|" << component.areaDamageFalloffMode
				     << "|" << component.areaDamageLayerMask
				     << "|" << EncodeSceneToken(component.areaDamageTag)
				     << "|" << (component.areaDamageIgnoreOwner ? 1 : 0)
				     << "|" << (component.areaDamagePlayOnStart ? 1 : 0)
				     << "|" << component.areaDamageActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.areaDamageAppliedActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::HitscanWeapon ||
				component.type == EditorComponentType::ProjectileEmitter) {
				file << "WeaponDamageTagExtension"
				     << "|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << EncodeSceneToken(
						 component.type == EditorComponentType::HitscanWeapon
							 ? component.hitscanDamageTag
							 : component.projectileDamageTag)
				     << "\n";
			}

			if (component.type == EditorComponentType::HitZone) {
				file << "HitZoneExtension"
				     << "|" << gameObject.id
				     << "|" << component.hitZoneHealthGameObjectId
				     << "|" << component.hitZoneDamageMultiplier
				     << "\n";
			}

			if (component.type == EditorComponentType::DamageTagModifier) {
				file << "DamageTagModifierExtension"
				     << "|" << gameObject.id
				     << "|" << component.damageTagDefaultMultiplier
				     << "|" << component.damageTagModifierEntries.size();

				for (const EditorDamageTagModifierEntry& entry : component.damageTagModifierEntries) {
					file << "|" << EncodeSceneToken(entry.tagName)
					     << "|" << entry.multiplier;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::ProjectileDetonator) {
				file << "ProjectileDetonatorExtension"
				     << "|" << gameObject.id
				     << "|" << (component.projectileDetonateOnContact ? 1 : 0)
				     << "|" << (component.projectileDetonateOnProximity ? 1 : 0)
				     << "|" << (component.projectileDetonateOnLifetime ? 1 : 0)
				     << "|" << component.projectileDetonatorTargetGameObjectId
				     << "|" << component.projectileDetonatorProximityRadius
				     << "|" << component.projectileDetonatorAreaDamageGameObjectId
				     << "|" << component.projectileDetonatorActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.projectileDetonatedActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::ThreatTracker) {
				file << "ThreatTrackerExtension"
				     << "|" << gameObject.id
				     << "|" << component.threatTrackerTargetGameObjectId
				     << "|" << component.threatTrackerMaximumDistance
				     << "|" << component.threatTrackerMinimumClosingSpeed
				     << "|" << component.threatTrackerMaximumMissDistance
				     << "|" << component.threatTrackerMaximumCount
				     << "|" << component.threatTrackerActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.threatTrackerAddedActionName)
				     << "|" << EncodeSceneToken(component.threatTrackerLostActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::RuntimeStateReset) {
				file << "RuntimeStateResetExtension"
				     << "|" << gameObject.id
				     << "|" << (component.runtimeResetHealth ? 1 : 0)
				     << "|" << (component.runtimeResetStateMachine ? 1 : 0)
				     << "|" << (component.runtimeResetAttributes ? 1 : 0)
				     << "|" << (component.runtimeResetLocks ? 1 : 0)
				     << "|" << (component.runtimeResetTimers ? 1 : 0)
				     << "|" << (component.runtimeResetDestructibleParts ? 1 : 0)
				     << "|" << (component.runtimeResetCooldowns ? 1 : 0)
				     << "|" << component.runtimeResetActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.runtimeResetActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::CooldownSet) {
				file << "CooldownSetExtension"
				     << "|" << gameObject.id
				     << "|" << component.cooldownSetActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.cooldownSetCompletedActionName)
				     << "|" << component.cooldownSetEntries.size();

				for (const EditorCooldownEntry& entry : component.cooldownSetEntries) {
					file << "|" << EncodeSceneToken(entry.name)
					     << "|" << entry.duration
					     << "|" << (entry.startReady ? 1 : 0);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::WeaponFirePattern) {
				file << "WeaponFirePatternExtension"
				     << "|" << gameObject.id
				     << "|" << component.weaponFirePatternMode
				     << "|" << component.weaponFirePatternCount
				     << "|" << component.weaponFirePatternInterval
				     << "|" << component.weaponFirePatternSpreadAngle
				     << "|" << component.weaponFirePatternChargeSeconds
				     << "|" << component.weaponFirePatternActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.weaponFirePatternCompletedActionName)
				     << "|" << component.weaponFirePatternSpawnPointGameObjectIds.size();

				for (const int32_t spawnPointGameObjectId : component.weaponFirePatternSpawnPointGameObjectIds) {
					file << "|" << spawnPointGameObjectId;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::TargetAssignment) {
				file << "TargetAssignmentExtension"
				     << "|" << gameObject.id
				     << "|" << component.targetAssignmentMultiTargetLockGameObjectId
				     << "|" << component.targetAssignmentMaximumTargets
				     << "|" << component.targetAssignmentInterval
				     << "|" << (component.targetAssignmentLockedOnly ? 1 : 0)
				     << "|" << component.targetAssignmentActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.targetAssignmentCompletedActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::WeaponAccuracy) {
				file << "WeaponAccuracyExtension"
				     << "|" << gameObject.id
				     << "|" << component.weaponAccuracyBaseSpread
				     << "|" << component.weaponAccuracyMaximumSpread
				     << "|" << component.weaponAccuracySpreadPerShot
				     << "|" << component.weaponAccuracyRecoveryPerSecond
				     << "|" << component.weaponAccuracyMovementSpread
				     << "|" << component.weaponAccuracyDistribution
				     << "\n";
			}

			if (component.type == EditorComponentType::WeaponRecoil) {
				file << "WeaponRecoilExtension"
				     << "|" << gameObject.id
				     << "|" << component.weaponRecoilBodyImpulse.x
				     << "|" << component.weaponRecoilBodyImpulse.y
				     << "|" << component.weaponRecoilBodyImpulse.z
				     << "|" << component.weaponRecoilBodyTorque.x
				     << "|" << component.weaponRecoilBodyTorque.y
				     << "|" << component.weaponRecoilBodyTorque.z
				     << "|" << component.weaponRecoilVisualGameObjectId
				     << "|" << component.weaponRecoilVisualPosition.x
				     << "|" << component.weaponRecoilVisualPosition.y
				     << "|" << component.weaponRecoilVisualPosition.z
				     << "|" << component.weaponRecoilVisualRotation.x
				     << "|" << component.weaponRecoilVisualRotation.y
				     << "|" << component.weaponRecoilVisualRotation.z
				     << "|" << component.weaponRecoilRecoveryPerSecond
				     << "|" << component.weaponRecoilCameraShakeGameObjectId
				     << "|" << component.weaponRecoilActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.weaponRecoilActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::ImpactResponder) {
				file << "ImpactResponderExtension"
				     << "|" << gameObject.id
				     << "|" << component.impactResponseEntries.size();

				for (const EditorImpactResponseEntry& entry : component.impactResponseEntries) {
					file << "|" << EncodeSceneToken(entry.damageTag)
					     << "|" << EncodeSceneToken(entry.surfaceTag)
					     << "|" << EncodeSceneToken(entry.effectAssetPath)
					     << "|" << entry.audioSourceGameObjectId
					     << "|" << entry.decalGameObjectId
					     << "|" << entry.cameraShakeGameObjectId
					     << "|" << entry.actionTargetGameObjectId
					     << "|" << EncodeSceneToken(entry.actionName);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::SurfaceType) {
				file << "SurfaceTypeExtension"
				     << "|" << gameObject.id
				     << "|" << EncodeSceneToken(component.surfaceTypeTag)
				     << "\n";
			}

			if (component.type == EditorComponentType::TimeScale) {
				file << "TimeScaleExtension"
				     << "|" << gameObject.id
				     << "|" << component.timeScaleValue
				     << "|" << component.timeScaleDuration
				     << "|" << component.timeScaleBlendSeconds
				     << "|" << (component.timeScalePlayOnStart ? 1 : 0)
				     << "|" << component.timeScaleActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.timeScaleCompletedActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::AimAssist) {
				file << "AimAssistExtension|" << gameObject.id
				     << "|" << component.aimAssistScreenAimGameObjectId
				     << "|" << component.aimAssistTargetSelectorGameObjectId
				     << "|" << component.aimAssistRadius
				     << "|" << component.aimAssistStrength
				     << "|" << component.aimAssistFollowSpeed
				     << "|" << component.aimAssistInputSuppression << "\n";
			}

			if (component.type == EditorComponentType::InterceptPrediction) {
				file << "InterceptPredictionExtension|" << gameObject.id
				     << "|" << component.interceptTargetGameObjectId
				     << "|" << component.interceptTargetSelectorGameObjectId
				     << "|" << component.interceptProjectileSpeed
				     << "|" << component.interceptMaximumTime << "\n";
			}

			if (component.type == EditorComponentType::DamageDirectionIndicator) {
				file << "DamageDirectionIndicatorExtension|" << gameObject.id
				     << "|" << component.damageDirectionDuration
				     << "|" << component.damageDirectionFadeSeconds
				     << "|" << component.damageDirectionMinimumDamage
				     << "|" << component.damageDirectionEdgeRadius << "\n";
			}

			if (component.type == EditorComponentType::ObjectiveTracker) {
				file << "ObjectiveTrackerExtension|" << gameObject.id
				     << "|" << component.objectiveActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.objectiveChangedActionName)
				     << "|" << component.objectiveEntries.size();
				for (const EditorObjectiveEntry& entry : component.objectiveEntries) {
					file << "|" << EncodeSceneToken(entry.objectiveId)
					     << "|" << EncodeSceneToken(entry.displayName)
					     << "|" << entry.state << "|" << entry.currentValue << "|" << entry.targetValue;
				}
				file << "\n";
			}

			if (component.type == EditorComponentType::EncounterController) {
				file << "EncounterControllerExtension|" << gameObject.id
				     << "|" << (component.encounterPlayOnStart ? 1 : 0)
				     << "|" << component.encounterActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.encounterCompletedActionName)
				     << "|" << component.encounterWaveEntries.size();
				for (const EditorEncounterWaveEntry& entry : component.encounterWaveEntries) {
					file << "|" << entry.waveSpawnerGameObjectId << "|" << entry.startDelay
					     << "|" << (entry.waitsForAllDefeated ? 1 : 0);
				}
				file << "\n";
			}

			if (component.type == EditorComponentType::SpawnPointSet) {
				file << "SpawnPointSetExtension|" << gameObject.id
				     << "|" << component.spawnPointSetMode
				     << "|" << component.spawnPointVolumeSize.x << "|" << component.spawnPointVolumeSize.y
				     << "|" << component.spawnPointVolumeSize.z
				     << "|" << (component.spawnPointAvoidImmediateRepeat ? 1 : 0)
				     << "|" << component.spawnPointSetEntries.size();
				for (const EditorSpawnPointEntry& entry : component.spawnPointSetEntries) {
					file << "|" << entry.gameObjectId << "|" << entry.weight;
				}
				file << "\n";
			}

			if (component.type == EditorComponentType::DifficultyParameterSet) {
				file << "DifficultyParameterSetExtension|" << gameObject.id
				     << "|" << component.difficultySelectedIndex
				     << "|" << (component.difficultyApplyOnStart ? 1 : 0)
				     << "|" << component.difficultyActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.difficultyAppliedActionName)
				     << "|" << component.difficultyNames.size();
				for (const std::string& name : component.difficultyNames) file << "|" << EncodeSceneToken(name);
				file << "|" << component.difficultyOverrides.size();
				for (const EditorDifficultyOverrideEntry& entry : component.difficultyOverrides) {
					file << "|" << entry.difficultyIndex << "|" << entry.targetGameObjectId
					     << "|" << EncodeSceneToken(entry.componentName) << "|" << EncodeSceneToken(entry.propertyName)
					     << "|" << entry.valueType << "|" << entry.floatValue << "|" << entry.intValue
					     << "|" << (entry.boolValue ? 1 : 0);
				}
				file << "\n";
			}

			if (component.type == EditorComponentType::CameraFeedbackMixer) {
				file << "CameraFeedbackMixerExtension|" << gameObject.id
				     << "|" << component.cameraFeedbackMaximumPosition.x << "|" << component.cameraFeedbackMaximumPosition.y
				     << "|" << component.cameraFeedbackMaximumPosition.z
				     << "|" << component.cameraFeedbackMaximumRotation.x << "|" << component.cameraFeedbackMaximumRotation.y
				     << "|" << component.cameraFeedbackMaximumRotation.z
				     << "|" << component.cameraFeedbackMaximumConcurrent
				     << "|" << component.cameraFeedbackMixMode
				     << "|" << component.cameraFeedbackGlobalStrength << "\n";
			}

			if (component.type == EditorComponentType::BallisticPrediction) {
				file << "BallisticPredictionExtension|" << gameObject.id
				     << "|" << component.ballisticTargetGameObjectId
				     << "|" << component.ballisticTargetSelectorGameObjectId
				     << "|" << component.ballisticInitialSpeed
				     << "|" << component.ballisticGravity.x
				     << "|" << component.ballisticGravity.y
				     << "|" << component.ballisticGravity.z
				     << "|" << component.ballisticDrag
				     << "|" << component.ballisticTargetAcceleration.x
				     << "|" << component.ballisticTargetAcceleration.y
				     << "|" << component.ballisticTargetAcceleration.z
				     << "|" << component.ballisticMaximumTime
				     << "|" << component.ballisticSimulationStep
				     << "|" << component.ballisticMaximumPoints << "\n";
				file << "BallisticSourceVelocityExtension|" << gameObject.id
				     << "|" << component.ballisticInheritSourceVelocity
				     << "|" << component.ballisticSourceVelocityGameObjectId
				     << "|" << component.ballisticUseParentRigidBody
				     << "|" << component.ballisticLinearVelocityInheritance
				     << "|" << component.ballisticAngularVelocityInheritance << "\n";
			}

			if (component.type == EditorComponentType::DamageEventBuffer) {
				file << "DamageEventBufferExtension|" << gameObject.id
				     << "|" << component.damageEventMaximumEntries
				     << "|" << component.damageEventLifetime
				     << "|" << component.damageEventMinimumDamage
				     << "|" << (component.damageEventMergeSameSource ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::GamePause) {
				file << "GamePauseExtension|" << gameObject.id
				     << "|" << (component.gamePausePauseGameTime ? 1 : 0)
				     << "|" << (component.gamePausePausePhysics ? 1 : 0)
				     << "|" << (component.gamePausePauseAudio ? 1 : 0)
				     << "|" << EncodeSceneToken(component.gamePauseGameplayInputMap)
				     << "|" << EncodeSceneToken(component.gamePauseUiInputMap)
				     << "|" << component.gamePauseActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.gamePausePausedActionName)
				     << "|" << EncodeSceneToken(component.gamePauseResumedActionName) << "\n";
			}

			if (component.type == EditorComponentType::SurfaceWakeEmitter) {
				file << "SurfaceWakeEmitterExtension|" << gameObject.id
				     << "|" << component.surfaceWakeOceanGameObjectId
				     << "|" << component.surfaceWakeLeftEffectGameObjectId
				     << "|" << component.surfaceWakeRightEffectGameObjectId
				     << "|" << component.surfaceWakeBowEffectGameObjectId
				     << "|" << component.surfaceWakeMinimumSpeed
				     << "|" << component.surfaceWakeMaximumSpeed
				     << "|" << component.surfaceWakeWidth
				     << "|" << component.surfaceWakeLifetime
				     << "|" << component.surfaceWakeMaximumEmissionRate << "\n";
			}

			if (component.type == EditorComponentType::TrajectoryRenderer) {
				file << "TrajectoryRendererExtension|" << gameObject.id
				     << "|" << component.trajectoryPredictionGameObjectId
				     << "|" << component.trajectoryColor.x
				     << "|" << component.trajectoryColor.y
				     << "|" << component.trajectoryColor.z
				     << "|" << component.trajectoryAlpha
				     << "|" << component.trajectoryThickness
				     << "|" << component.trajectoryMaximumPoints
				     << "|" << (component.trajectoryShowInSceneView ? 1 : 0)
				     << "|" << (component.trajectoryShowInGameView ? 1 : 0)
				     << "|" << (component.trajectoryShowImpactPoint ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::TargetSelector) {
				file << "TargetSelectorOceanExtension|" << gameObject.id
				     << "|" << component.targetSelectorOcclusionMode
				     << "|" << component.targetSelectorOceanClearance << "\n";
			}

			if (component.type == EditorComponentType::HitscanWeapon ||
				component.type == EditorComponentType::ProjectileEmitter) {
				file << "WeaponOceanCollisionExtension|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << (component.type == EditorComponentType::HitscanWeapon
						 ? component.hitscanOceanCollision
						 : component.projectileOceanCollision)
				     << "\n";

				if (component.type == EditorComponentType::ProjectileEmitter) {
					file << "ProjectileVelocityAimExtension|" << gameObject.id
					     << "|" << component.projectileAimMode
					     << "|" << component.projectileBallisticPredictionGameObjectId
					     << "|" << component.projectileInheritSourceVelocity
					     << "|" << component.projectileSourceVelocityGameObjectId
					     << "|" << component.projectileUseParentRigidBody
					     << "|" << component.projectileLinearVelocityInheritance
					     << "|" << component.projectileAngularVelocityInheritance
					     << "|" << component.projectileVariableSpeedMinimumFlightTime
					     << "|" << component.projectileVariableSpeedMaximumFlightTime << "\n";
					file << "ProjectileVariableSpeedExtension|" << gameObject.id
					     << "|" << component.projectileVariableSpeedTimeMode
					     << "|" << component.projectileVariableSpeedFixedFlightTime
					     << "|" << component.projectileVariableSpeedTrajectoryMode
					     << "|" << component.projectileVariableSpeedDepressionAngleDegrees
					     << "|" << component.projectileVariableSpeedArcHeight
					     << "|" << component.projectileVariableSpeedDistanceFactor << "\n";
					file << "ProjectileTracerExtension|" << gameObject.id
					     << "|" << component.projectileTracerStretchEnabled
					     << "|" << component.projectileTracerLengthScale
					     << "|" << component.projectileTracerMinimumLength
					     << "|" << component.projectileTracerThickness
					     << "|" << component.projectileHitscanResolution << "\n";
					file << "ProjectileSpawnClearanceExtension|" << gameObject.id
					     << "|" << component.projectileSpawnClearance << "\n";
				}
			}

			if (component.type == EditorComponentType::WaterSurfaceState) {
				file << "WaterSurfaceStateExtension|" << gameObject.id
				     << "|" << component.waterSurfaceOceanGameObjectId
				     << "|" << component.waterSurfaceLocalOffset.x
				     << "|" << component.waterSurfaceLocalOffset.y
				     << "|" << component.waterSurfaceLocalOffset.z
				     << "|" << component.waterSurfaceClearance
				     << "|" << component.waterSurfaceActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.waterSurfaceEnteredActionName)
				     << "|" << EncodeSceneToken(component.waterSurfaceExitedActionName)
				     << "\n";
			}

			if (component.type == EditorComponentType::OceanProbeSet) {
				file << "OceanProbeSetExtension|" << gameObject.id
				     << "|" << component.oceanProbeOceanGameObjectId
				     << "|" << component.oceanProbeLocalOriginOffset.x
				     << "|" << component.oceanProbeLocalOriginOffset.y
				     << "|" << component.oceanProbeLocalOriginOffset.z
				     << "|" << component.oceanProbeLocalDirection.x
				     << "|" << component.oceanProbeLocalDirection.y
				     << "|" << component.oceanProbeLocalDirection.z
				     << "|" << component.oceanProbeEntries.size();

				for (const EditorOceanProbeEntry& probeEntry : component.oceanProbeEntries) {
					file << "|" << probeEntry.distance;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::AttackCollisionFilter) {
				file << "AttackCollisionFilterExtension|" << gameObject.id
				     << "|" << component.attackFilterInstigatorGameObjectId
				     << "|" << component.attackFilterIgnoreInstigator
				     << "|" << component.attackFilterIgnoreInstigatorHierarchy
				     << "|" << component.attackFilterTeamRule
				     << "|" << component.attackFilterIgnoreNeutral
				     << "|" << component.attackFilterArmingDistance
				     << "|" << component.attackFilterIgnoredGameObjectIds.size();

				for (const int32_t ignoredGameObjectId : component.attackFilterIgnoredGameObjectIds) {
					file << "|" << ignoredGameObjectId;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::TurretAim) {
				file << "TurretAimExtension|" << gameObject.id
				     << "|" << component.turretTargetGameObjectId
				     << "|" << component.turretTargetSelectorGameObjectId
				     << "|" << component.turretYawPivotGameObjectId
				     << "|" << component.turretPitchPivotGameObjectId
				     << "|" << component.turretYawMinimumDegrees
				     << "|" << component.turretYawMaximumDegrees
				     << "|" << component.turretPitchMinimumDegrees
				     << "|" << component.turretPitchMaximumDegrees
				     << "|" << component.turretYawSpeedDegrees
				     << "|" << component.turretPitchSpeedDegrees
				     << "|" << component.turretAimToleranceDegrees
				     << "|" << component.turretPredictionSeconds
				     << "\n";
			}

			if (component.type == EditorComponentType::WeaponGroup) {
				file << "WeaponGroupExtension|" << gameObject.id
				     << "|" << component.weaponGroupMode
				     << "|" << component.weaponGroupInterval
				     << "|" << component.weaponGroupRequireAllReady
				     << "|" << component.weaponGroupActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.weaponGroupCompletedActionName)
				     << "|" << component.weaponGroupEntries.size();

				for (const EditorWeaponGroupEntry& entry : component.weaponGroupEntries) {
					file << "|" << entry.weaponGameObjectId << "|" << entry.isEnabled;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::ProjectileImpactPhysics) {
				file << "ProjectileImpactPhysicsExtension|" << gameObject.id
				     << "|" << component.projectileImpactPenetrationEnergy
				     << "|" << component.projectileImpactPenetrationLoss
				     << "|" << component.projectileImpactMaximumPenetrations
				     << "|" << component.projectileImpactRicochetAngleDegrees
				     << "|" << component.projectileImpactEnergyRetention
				     << "|" << component.projectileImpactDamageRetention
				     << "|" << component.projectileImpactMaximumRicochets
				     << "|" << component.projectileImpactSurfaceModifiers.size();

				for (const EditorProjectileSurfaceModifierEntry& entry : component.projectileImpactSurfaceModifiers) {
					file << "|" << EncodeSceneToken(entry.surfaceTag)
					     << "|" << entry.penetrationLossMultiplier
					     << "|" << entry.ricochetAngleOffset
					     << "|" << entry.energyRetentionMultiplier;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::CameraHorizonStabilizer) {
				file << "CameraHorizonStabilizerExtension|" << gameObject.id
				     << "|" << component.horizonSourceGameObjectId
				     << "|" << component.horizonLocalPositionOffset.x
				     << "|" << component.horizonLocalPositionOffset.y
				     << "|" << component.horizonLocalPositionOffset.z
				     << "|" << component.horizonRotationOffsetDegrees.x
				     << "|" << component.horizonRotationOffsetDegrees.y
				     << "|" << component.horizonRotationOffsetDegrees.z
				     << "|" << component.horizonFollowPosition
				     << "|" << component.horizonPitchInheritance
				     << "|" << component.horizonYawInheritance
				     << "|" << component.horizonRollInheritance
				     << "|" << component.horizonWorldUp.x
				     << "|" << component.horizonWorldUp.y
				     << "|" << component.horizonWorldUp.z
				     << "|" << component.horizonDamping
				     << "|" << component.horizonMaximumRollDegrees
				     << "\n";
			}

			if (component.type == EditorComponentType::AreaDamage) {
				file << "AreaDamageFilterExtension|" << gameObject.id
				     << "|" << component.areaDamageOcclusionMode
				     << "|" << component.areaDamageOcclusionLayerMask
				     << "|" << component.areaDamageBlockedMultiplier
				     << "|" << component.areaDamageOcclusionSamplePoints
				     << "|" << component.areaDamageTeamRule
				     << "|" << component.areaDamageIgnoreNeutral
				     << "|" << component.areaDamageTeamSourceGameObjectId << "\n";
			}

			if (component.type == EditorComponentType::FireLineCheck) {
				file << "FireLineCheckExtension|" << gameObject.id
				     << "|" << component.fireLineMuzzleGameObjectId
				     << "|" << component.fireLineDirectionGameObjectId
				     << "|" << component.fireLineAllowedTargetGameObjectId
				     << "|" << component.fireLineDistance
				     << "|" << component.fireLineRadius
				     << "|" << component.fireLineLayerMask
				     << "|" << component.fireLineIgnoredGameObjectIds.size();

				for (const int32_t ignoredGameObjectId : component.fireLineIgnoredGameObjectIds) {
					file << "|" << ignoredGameObjectId;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::StatusEffectSet) {
				file << "StatusEffectSetExtension|" << gameObject.id
				     << "|" << component.statusEffectActionTargetGameObjectId
				     << "|" << component.statusEffectDefinitions.size();

				for (const EditorStatusEffectDefinitionEntry& definition : component.statusEffectDefinitions) {
					file << "|" << EncodeSceneToken(definition.effectId)
					     << "|" << definition.duration
					     << "|" << definition.stackMode
					     << "|" << definition.maximumStacks
					     << "|" << definition.tickInterval
					     << "|" << EncodeSceneToken(definition.startedActionName)
					     << "|" << EncodeSceneToken(definition.tickActionName)
					     << "|" << EncodeSceneToken(definition.endedActionName);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::RailSpeedProfile) {
				file << "RailSpeedProfileExtension|" << gameObject.id
				     << "|" << (component.railSpeedProfileEnabled ? 1 : 0)
				     << "|" << component.railSpeedKeys.size();

				for (const EditorRailSpeedKey& speedKey : component.railSpeedKeys) {
					file << "|" << speedKey.normalizedProgress
					     << "|" << speedKey.speedMultiplier;
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::RailZone) {
				file << "RailZoneExtension|" << gameObject.id
				     << "|" << component.railZoneActionTargetGameObjectId
				     << "|" << component.railZoneEntries.size();

				for (const EditorRailZoneEntry& zoneEntry : component.railZoneEntries) {
					file << "|" << EncodeSceneToken(zoneEntry.zoneId)
					     << "|" << zoneEntry.startNormalized
					     << "|" << zoneEntry.endNormalized
					     << "|" << zoneEntry.speedMultiplier
					     << "|" << zoneEntry.movementRange.x
					     << "|" << zoneEntry.movementRange.y
					     << "|" << (zoneEntry.overrideMovementRange ? 1 : 0)
					     << "|" << EncodeSceneToken(zoneEntry.enteredActionName)
					     << "|" << EncodeSceneToken(zoneEntry.exitedActionName);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::CameraFollowComposer) {
				file << "CameraFollowComposerExtension|" << gameObject.id
				     << "|" << component.cameraComposerTargetGameObjectId
				     << "|" << component.cameraComposerFollowOffset.x
				     << "|" << component.cameraComposerFollowOffset.y
				     << "|" << component.cameraComposerFollowOffset.z
				     << "|" << component.cameraComposerLookAtOffset.x
				     << "|" << component.cameraComposerLookAtOffset.y
				     << "|" << component.cameraComposerLookAtOffset.z
				     << "|" << component.cameraComposerPositionDamping
				     << "|" << component.cameraComposerRotationDamping
				     << "|" << component.cameraComposerLookAheadSeconds
				     << "|" << component.cameraComposerDeadZone.x
				     << "|" << component.cameraComposerDeadZone.y
				     << "|" << component.cameraComposerMaximumDistance
				     << "|" << (component.cameraComposerInheritTargetYaw ? 1 : 0)
				     << "|" << (component.cameraComposerStabilizePitchRoll ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::SpeedFeedback) {
				file << "SpeedFeedbackExtension|" << gameObject.id
				     << "|" << component.speedFeedbackSourceGameObjectId
				     << "|" << component.speedFeedbackCameraGameObjectId
				     << "|" << component.speedFeedbackMinimumSpeed
				     << "|" << component.speedFeedbackMaximumSpeed
				     << "|" << component.speedFeedbackMinimumFovDegrees
				     << "|" << component.speedFeedbackMaximumFovDegrees
				     << "|" << component.speedFeedbackMinimumMotionBlur
				     << "|" << component.speedFeedbackMaximumMotionBlur
				     << "|" << component.speedFeedbackCameraStrength
				     << "|" << component.speedFeedbackResponseSpeed << "\n";
			}

			if (component.type == EditorComponentType::SpawnedObjectSetup) {
				file << "SpawnedObjectSetupExtension|" << gameObject.id
				     << "|" << component.spawnedSetupRailPathGameObjectId
				     << "|" << component.spawnedSetupRailStartNormalized
				     << "|" << component.spawnedSetupRailStartStep
				     << "|" << component.spawnedSetupRailSpeedMultiplier
				     << "|" << (component.spawnedSetupOverrideTeam ? 1 : 0)
				     << "|" << component.spawnedSetupTeamId
				     << "|" << (component.spawnedSetupResetRuntimeState ? 1 : 0)
				     << "|" << component.spawnedSetupActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.spawnedSetupAppliedActionName) << "\n";
			}

			if (component.type == EditorComponentType::WaveMotionProfile) {
				file << "WaveMotionProfileExtension|" << gameObject.id
				     << "|" << component.waveMotionMode
				     << "|" << component.waveMotionAmplitude.x
				     << "|" << component.waveMotionAmplitude.y
				     << "|" << component.waveMotionFrequency
				     << "|" << component.waveMotionPhaseStep
				     << "|" << component.waveMotionBlendInSeconds << "\n";
			}

			if (component.type == EditorComponentType::WaveSpawner) {
				file << "WaveSustainExtension|" << gameObject.id
				     << "|" << component.waveTargetAliveCount
				     << "|" << component.waveSpawnPointSetGameObjectId << "\n";
			}

			if (component.type == EditorComponentType::TargetSteering) {
				file << "TargetSteeringMoveExtension|" << gameObject.id
				     << "|" << component.targetSteeringMoveMode
				     << "|" << component.targetSteeringSideOffset
				     << "|" << component.targetSteeringForwardOffset
				     << "|" << component.targetSteeringVerticalOffset
				     << "|" << component.targetSteeringTargetDistance
				     << "|" << component.targetSteeringDistanceMargin
				     << "|" << component.targetSteeringDuration
				     << "|" << component.targetSteeringNextMoveMode
				     << "|" << component.targetSteeringStartOffset.x
				     << "|" << component.targetSteeringStartOffset.y
				     << "|" << component.targetSteeringStartOffset.z
				     << "|" << component.targetSteeringEndOffset.x
				     << "|" << component.targetSteeringEndOffset.y
				     << "|" << component.targetSteeringEndOffset.z
				     << "|" << component.targetSteeringPositionLerpSpeed
				     << "|" << component.targetSteeringActionTargetGameObjectId
				     << "|" << EncodeSceneToken(component.targetSteeringCompletedActionName) << "\n";
			}

			if (component.type == EditorComponentType::DistanceActivation) {
				file << "DistanceActivationExtension|" << gameObject.id
				     << "|" << component.distanceActivationReferenceGameObjectId
				     << "|" << component.distanceActivationEnterDistance
				     << "|" << component.distanceActivationExitDistance
				     << "|" << (component.distanceActivationAffectHierarchy ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::SimulationLOD) {
				file << "SimulationLodExtension|" << gameObject.id
				     << "|" << component.simulationLodReferenceGameObjectId
				     << "|" << component.simulationLodMediumDistance
				     << "|" << component.simulationLodFarDistance
				     << "|" << component.simulationLodCulledDistance
				     << "|" << component.simulationLodMediumScriptInterval
				     << "|" << component.simulationLodFarScriptInterval
				     << "|" << (component.simulationLodDisablePhysicsAtFar ? 1 : 0)
				     << "|" << (component.simulationLodDisableScriptsAtFar ? 1 : 0)
				     << "|" << (component.simulationLodDisableAiAtFar ? 1 : 0)
				     << "|" << (component.simulationLodDisableAnimationAtFar ? 1 : 0)
				     << "|" << (component.simulationLodDisableEffectsAtFar ? 1 : 0)
				     << "|" << (component.simulationLodAffectHierarchy ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::RailEventMarker) {
				file << "RailEventMarkerExtension|" << gameObject.id
				     << "|" << component.railEventMarkerActionTargetGameObjectId
				     << "|" << component.railEventMarkerEntries.size();

				for (const EditorRailEventMarkerEntry& markerEntry : component.railEventMarkerEntries) {
					file << "|" << EncodeSceneToken(markerEntry.markerId)
					     << "|" << markerEntry.normalizedProgress
					     << "|" << markerEntry.directionMode
					     << "|" << (markerEntry.triggerOnce ? 1 : 0)
					     << "|" << EncodeSceneToken(markerEntry.actionName);
				}

				file << "\n";
			}

			if (component.type == EditorComponentType::SceneStreaming) {
				file << "SceneStreamingExtension|" << gameObject.id
				     << "|" << EncodeSceneToken(component.sceneStreamingScenePath)
				     << "|" << component.sceneStreamingReferenceGameObjectId
				     << "|" << component.sceneStreamingLoadDistance
				     << "|" << component.sceneStreamingUnloadDistance
				     << "|" << (component.sceneStreamingUnloadWhenFar ? 1 : 0) << "\n";
			}

			if (component.type == EditorComponentType::ParticleSystem ||
				component.type == EditorComponentType::VisualEffect) {
				file << "ParticleBillboardExtension|" << gameObject.id
				     << "|" << static_cast<int32_t>(component.type)
				     << "|" << component.particleBillboardMode
				     << "|" << component.particleBillboardStretch << "\n";
			}

			// 汎用ゲームプレイ基盤は独立行へ保存し、巨大な既存Component行の列位置を変更しない。
			const bool savesGameplayFoundation =
				component.type == EditorComponentType::Camera ||
				component.type == EditorComponentType::CinemachineCamera ||
				component.type == EditorComponentType::ScreenAim ||
				component.type == EditorComponentType::HitscanWeapon ||
				component.type == EditorComponentType::ProjectileEmitter ||
				component.type == EditorComponentType::DamageReceiver ||
				component.type == EditorComponentType::ObjectPool ||
				component.type == EditorComponentType::PrefabSpawner ||
				component.type == EditorComponentType::CameraBlend ||
				component.type == EditorComponentType::CameraShake ||
				component.type == EditorComponentType::RailBranch;

			if (savesGameplayFoundation) {
				file << "GameplayFoundation|"
				     << gameObject.id << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << component.cameraPriority << "|"
				     << component.screenAimInputGameObjectId << "|"
				     << component.screenAimReticleGameObjectId << "|"
				     << component.screenAimInputMode << "|"
				     << EncodeSceneToken(component.screenAimActionMapName) << "|"
				     << EncodeSceneToken(component.screenAimActionName) << "|"
				     << component.screenAimNormalizedPosition.x << "|"
				     << component.screenAimNormalizedPosition.y << "|"
				     << component.screenAimSpeed << "|"
				     << (component.screenAimInvertY ? 1 : 0) << "|"
				     << (component.screenAimClamp ? 1 : 0) << "|"
				     << component.hitscanAimGameObjectId << "|"
				     << component.hitscanInputGameObjectId << "|"
				     << EncodeSceneToken(component.hitscanActionMapName) << "|"
				     << EncodeSceneToken(component.hitscanFireActionName) << "|"
				     << component.hitscanRange << "|"
				     << component.hitscanDamage << "|"
				     << component.hitscanInterval << "|"
				     << (component.hitscanAutomatic ? 1 : 0) << "|"
				     << component.hitscanActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.hitscanFiredActionName) << "|"
				     << EncodeSceneToken(component.hitscanHitActionName) << "|"
				     << EncodeSceneToken(component.hitscanMissActionName) << "|"
				     << component.projectileAimGameObjectId << "|"
				     << component.projectileInputGameObjectId << "|"
				     << component.projectilePoolGameObjectId << "|"
				     << component.projectileSpawnPointGameObjectId << "|"
				     << EncodeSceneToken(component.projectileActionMapName) << "|"
				     << EncodeSceneToken(component.projectileFireActionName) << "|"
				     << component.projectileSpeed << "|"
				     << component.projectileDamage << "|"
				     << component.projectileRadius << "|"
				     << component.projectileLifetime << "|"
				     << component.projectileInterval << "|"
				     << (component.projectileAutomatic ? 1 : 0) << "|"
				     << component.projectileActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.projectileFiredActionName) << "|"
				     << EncodeSceneToken(component.projectileHitActionName) << "|"
				     << component.damageMultiplier << "|"
				     << component.damageInvulnerabilitySeconds << "|"
				     << (component.damageDeactivateOnDeath ? 1 : 0) << "|"
				     << component.damageActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.damagedActionName) << "|"
				     << EncodeSceneToken(component.deathActionName) << "|"
				     << component.objectPoolTemplateGameObjectId << "|"
				     << component.objectPoolInitialSize << "|"
				     << (component.objectPoolAllowExpand ? 1 : 0) << "|"
				     << component.prefabSpawnerPoolGameObjectId << "|"
				     << component.prefabSpawnerPointGameObjectId << "|"
				     << component.prefabSpawnerMode << "|"
				     << component.prefabSpawnerInterval << "|"
				     << component.prefabSpawnerActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.prefabSpawnerSpawnedActionName) << "|"
				     << component.cameraBlendSourceGameObjectId << "|"
				     << component.cameraBlendTargetGameObjectId << "|"
				     << component.cameraBlendDuration << "|"
				     << component.cameraBlendEasing << "|"
				     << (component.cameraBlendPlayOnStart ? 1 : 0) << "|"
				     << component.cameraShakePositionAmplitude.x << "|"
				     << component.cameraShakePositionAmplitude.y << "|"
				     << component.cameraShakePositionAmplitude.z << "|"
				     << component.cameraShakeRotationAmplitude.x << "|"
				     << component.cameraShakeRotationAmplitude.y << "|"
				     << component.cameraShakeRotationAmplitude.z << "|"
				     << component.cameraShakeFrequency << "|"
				     << component.cameraShakeDuration << "|"
				     << (component.cameraShakePlayOnStart ? 1 : 0) << "|"
				     << component.railBranchFollowerGameObjectId << "|"
				     << component.railBranchTargetPathGameObjectId << "|"
				     << component.railBranchTriggerMode << "|"
				     << component.railBranchTriggerNormalized << "|"
				     << (component.railBranchPreserveProgress ? 1 : 0) << "|"
				     << (component.railBranchTriggerOnce ? 1 : 0) << "|"
				     << component.railBranchActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.railBranchActionName) << "|"
				     << component.cameraShakePriority
				     << "\n";
			}

			const bool savesWorkflowFoundation =
				component.type == EditorComponentType::ActionSequence ||
				component.type == EditorComponentType::ActionSequenceStep ||
				component.type == EditorComponentType::Saveable ||
				component.type == EditorComponentType::Checkpoint;

			if (savesWorkflowFoundation) {
				file << "WorkflowFoundation|"
				     << gameObject.id << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << (component.actionSequencePlayOnStart ? 1 : 0) << "|"
				     << (component.actionSequenceLoop ? 1 : 0) << "|"
				     << component.actionSequenceStepType << "|"
				     << component.actionSequenceParallelGroup << "|"
				     << component.actionSequenceTargetGameObjectId << "|"
				     << EncodeSceneToken(component.actionSequenceActionName) << "|"
				     << component.actionSequenceWaitSeconds << "|"
				     << (component.actionSequenceActiveValue ? 1 : 0) << "|"
				     << EncodeSceneToken(component.actionSequenceScenePath) << "|"
				     << (component.actionSequenceSceneAdditive ? 1 : 0) << "|"
				     << component.actionSequenceConditionMode << "|"
				     << component.actionSequenceCompareMode << "|"
				     << component.actionSequenceCompareValue << "|"
				     << component.actionSequenceTrueStepIndex << "|"
				     << component.actionSequenceFalseStepIndex << "|"
				     << EncodeSceneToken(component.saveableKey) << "|"
				     << (component.saveableTransform ? 1 : 0) << "|"
				     << (component.saveableActive ? 1 : 0) << "|"
				     << (component.saveableHealth ? 1 : 0) << "|"
				     << (component.saveableRigidbody ? 1 : 0) << "|"
				     << (component.saveableScriptProperties ? 1 : 0) << "|"
				     << EncodeSceneToken(component.checkpointSlotName) << "|"
				     << (component.checkpointSaveOnStart ? 1 : 0) << "|"
				     << (component.checkpointLoadOnStart ? 1 : 0) << "|"
				     << component.checkpointActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.checkpointSavedActionName) << "|"
				     << EncodeSceneToken(component.checkpointLoadedActionName) << "\n";
			}

			// 再利用Gameplay Componentは専用行へ保存し、既存Sceneの固定列を変更しない。
			const bool savesReusableGameplay =
				component.type == EditorComponentType::WeaponLoadout ||
				component.type == EditorComponentType::WeaponLoadoutSlot ||
				component.type == EditorComponentType::TargetSelector ||
				component.type == EditorComponentType::TargetSteering ||
				component.type == EditorComponentType::MovementModifier ||
				component.type == EditorComponentType::PropertyTween ||
				component.type == EditorComponentType::ActionRelay ||
				component.type == EditorComponentType::ActionRelayTarget ||
				component.type == EditorComponentType::TargetPoint ||
				component.type == EditorComponentType::Team ||
				component.type == EditorComponentType::Timer ||
				component.type == EditorComponentType::GenericStateMachine ||
				component.type == EditorComponentType::Attribute ||
				component.type == EditorComponentType::DestructiblePart ||
				component.type == EditorComponentType::FormationFollower ||
				component.type == EditorComponentType::TargetLock;

			if (savesReusableGameplay) {
				file << "ReusableGameplay|"
				     << gameObject.id << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << component.weaponLoadoutSelectedSlotIndex << "|"
				     << component.weaponLoadoutActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.weaponLoadoutChangedActionName) << "|"
				     << EncodeSceneToken(component.weaponLoadoutReloadedActionName) << "|"
				     << EncodeSceneToken(component.weaponSlotName) << "|"
				     << component.weaponSlotWeaponGameObjectId << "|"
				     << component.weaponSlotVisualGameObjectId << "|"
				     << component.weaponSlotCurrentAmmo << "|"
				     << component.weaponSlotReserveAmmo << "|"
				     << component.weaponSlotMaximumAmmo << "|"
				     << component.weaponSlotReloadSeconds << "|"
				     << (component.weaponSlotAutoReload ? 1 : 0) << "|"
				     << component.targetSelectorSearchLayer << "|"
				     << component.targetSelectorMaximumDistance << "|"
				     << component.targetSelectorMaximumAngle << "|"
				     << component.targetSelectorReferenceGameObjectId << "|"
				     << (component.targetSelectorOcclusionCheck ? 1 : 0) << "|"
				     << component.targetSelectorMaximumTargets << "|"
				     << component.targetSelectorSelectionMode << "|"
				     << component.targetSelectorActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.targetSelectorFoundActionName) << "|"
				     << EncodeSceneToken(component.targetSelectorLostActionName) << "|"
				     << EncodeSceneToken(component.targetSelectorChangedActionName) << "|"
				     << component.targetSteeringTargetGameObjectId << "|"
				     << component.targetSteeringSelectorGameObjectId << "|"
				     << component.targetSteeringTurnSpeed << "|"
				     << component.targetSteeringAcceleration << "|"
				     << component.targetSteeringMaximumSpeed << "|"
				     << component.targetSteeringStartDelay << "|"
				     << component.targetSteeringPredictionSeconds << "|"
				     << component.targetSteeringMode << "|"
				     << component.movementModifierLocalPositionOffset.x << "|"
				     << component.movementModifierLocalPositionOffset.y << "|"
				     << component.movementModifierLocalPositionOffset.z << "|"
				     << component.movementModifierLocalRotationOffset.x << "|"
				     << component.movementModifierLocalRotationOffset.y << "|"
				     << component.movementModifierLocalRotationOffset.z << "|"
				     << component.movementModifierAxisMask << "|"
				     << component.movementModifierInputRange.x << "|"
				     << component.movementModifierInputRange.y << "|"
				     << component.movementModifierInputSpeed << "|"
				     << component.movementModifierInputGameObjectId << "|"
				     << EncodeSceneToken(component.movementModifierActionMapName) << "|"
				     << EncodeSceneToken(component.movementModifierActionName) << "|"
				     << component.propertyTweenTargetGameObjectId << "|"
				     << EncodeSceneToken(component.propertyTweenComponentName) << "|"
				     << EncodeSceneToken(component.propertyTweenPropertyName) << "|"
				     << component.propertyTweenStartValue.x << "|"
				     << component.propertyTweenStartValue.y << "|"
				     << component.propertyTweenStartValue.z << "|"
				     << component.propertyTweenEndValue.x << "|"
				     << component.propertyTweenEndValue.y << "|"
				     << component.propertyTweenEndValue.z << "|"
				     << component.propertyTweenValueType << "|"
				     << component.propertyTweenDuration << "|"
				     << component.propertyTweenCurve << "|"
				     << (component.propertyTweenPlayOnStart ? 1 : 0) << "|"
				     << (component.propertyTweenLoop ? 1 : 0) << "|"
				     << component.propertyTweenActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.propertyTweenCompletedActionName) << "|"
				     << (component.actionRelayOnStart ? 1 : 0) << "|"
				     << component.actionRelayTargetGameObjectId << "|"
				     << EncodeSceneToken(component.actionRelayActionName) << "|"
				     << (component.actionRelayTargetEnabled ? 1 : 0) << "|"
				     << component.targetPointPriority << "|"
				     << component.targetPointRadius << "|"
				     << component.targetPointAimOffset.x << "|"
				     << component.targetPointAimOffset.y << "|"
				     << component.targetPointAimOffset.z << "|"
				     << component.teamId << "|"
				     << (component.teamTargetable ? 1 : 0) << "|"
				     << component.targetSelectorTeamFilter << "|"
				     << component.targetSelectorSpecificTeamId << "|"
				     << (component.targetSelectorIncludeNeutral ? 1 : 0) << "|"
				     << component.timerDuration << "|"
				     << (component.timerRepeat ? 1 : 0) << "|"
				     << (component.timerPlayOnStart ? 1 : 0) << "|"
				     << component.timerActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.timerActionName) << "|"
				     << EncodeSceneToken(component.stateMachineInitialState) << "|"
				     << component.stateMachineActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.stateMachineChangedActionName) << "|"
				     << EncodeSceneToken(component.attributeName) << "|"
				     << component.attributeMinimum << "|" << component.attributeMaximum << "|"
				     << component.attributeCurrent << "|" << component.attributeRegenerationPerSecond << "|"
				     << component.attributeActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.attributeChangedActionName) << "|"
				     << component.destructibleHealthGameObjectId << "|"
				     << EncodeSceneToken(component.destructibleDisableComponentNames) << "|"
				     << (component.destructibleDisableChildren ? 1 : 0) << "|"
				     << component.destructibleActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.destructibleDestroyedActionName) << "|"
				     << component.formationLeaderGameObjectId << "|"
				     << component.formationLocalOffset.x << "|" << component.formationLocalOffset.y << "|" << component.formationLocalOffset.z << "|"
				     << component.formationPositionSpeed << "|" << component.formationRotationSpeed << "|"
				     << (component.formationFollowRotation ? 1 : 0) << "|"
				     << component.targetLockSelectorGameObjectId << "|" << component.targetLockSeconds << "|"
				     << component.targetLockLostGraceSeconds << "|" << component.targetLockActionTargetGameObjectId << "|"
				     << EncodeSceneToken(component.targetLockStartedActionName) << "|"
				     << EncodeSceneToken(component.targetLockCompletedActionName) << "|"
				     << EncodeSceneToken(component.targetLockLostActionName)
				     << "\n";
			}

			// C++ Script の公開変数は個数が変わるため、Component 共通行とは別の行で保存する。
			for (const EditorScriptProperty& scriptProperty : component.scriptProperties) {
				file << "ScriptProperty|"
				     << gameObject.id << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << EncodeSceneToken(scriptProperty.name) << "|"
				     << EncodeSceneToken(scriptProperty.displayName) << "|"
				     << scriptProperty.type << "|"
				     << scriptProperty.boolValue << "|"
				     << scriptProperty.intValue << "|"
				     << scriptProperty.floatValue << "|"
				     << scriptProperty.vector2Value.x << "|"
				     << scriptProperty.vector2Value.y << "|"
				     << scriptProperty.vector3Value.x << "|"
				     << scriptProperty.vector3Value.y << "|"
				     << scriptProperty.vector3Value.z << "|"
				     << EncodeSceneToken(scriptProperty.stringValue) << "|"
				     << scriptProperty.minValue << "|"
				     << scriptProperty.maxValue << "|"
				     << scriptProperty.step << "|"
				     << scriptProperty.hasRange << "\n";
			}

			// PlayerInput は Action 数を固定せず、Action と C++ 関数の接続を 1 行ずつ保存する。
			for (const EditorInputEventBinding& inputEventBinding : component.inputEventBindings) {
				file << "InputEventBinding|"
				     << gameObject.id << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << EncodeSceneToken(inputEventBinding.actionMapName) << "|"
				     << EncodeSceneToken(inputEventBinding.actionName) << "|"
				     << EncodeSceneToken(inputEventBinding.functionName) << "|"
				     << inputEventBinding.valueType << "\n";
			}
		}
	}

	return true;
}

bool EditorScene::LoadScene(const std::string& filePath) {
	std::ifstream file(filePath, std::ios::binary);  // BOM の有無に関係なく旧形式と新形式を読み込む
	if (!file.is_open()) {
		return false;
	}

	std::vector<EditorGameObject> loadedGameObjects;  // 読み込み途中の Scene。成功したら gameObjects_ へ置き換える
	EditorPhysicsSettings loadedPhysicsSettings = physicsSettings_;  // 古い Scene に設定行がない場合は現在の既定値を使う
	bool hasSceneData = false;  // 空 Scene 保存も許可するため、GameObject が 0 件でも有効な Scene 行を読んだかを記録する
	std::string line;
	bool isFirstLine = true;

	while (std::getline(file, line)) {
		if (isFirstLine && line.size() >= sizeof(kSceneUtf8Bom) &&
			static_cast<unsigned char>(line[0]) == kSceneUtf8Bom[0] &&
			static_cast<unsigned char>(line[1]) == kSceneUtf8Bom[1] &&
			static_cast<unsigned char>(line[2]) == kSceneUtf8Bom[2]) {
			line.erase(0, sizeof(kSceneUtf8Bom));
		}

		isFirstLine = false;
		std::vector<std::string> elements = SplitLine(line, '|');  // 1 行を | で分割して、先頭要素で行の種類を判定する
		if (elements.empty()) {
			continue;
		}

		if (elements[0] == "PhysicsSettings" && elements.size() >= 9) {
			hasSceneData = true;  // 物理設定だけの空 Scene でも、正しい Scene ファイルとして扱う
			loadedPhysicsSettings.gravity = {ToFloat(elements[1]), ToFloat(elements[2]), ToFloat(elements[3])};
			loadedPhysicsSettings.fixedTimeStep = ToFloat(elements[4]);
			loadedPhysicsSettings.collisionStepCount = ToInt(elements[5]);
			loadedPhysicsSettings.drawColliderDebug = ToInt(elements[6]) != 0;
			loadedPhysicsSettings.drawContactDebug = ToInt(elements[7]) != 0;
			loadedPhysicsSettings.drawCastDebug = ToInt(elements[8]) != 0;
			if (elements.size() >= 9 + (kEditorPhysicsLayerCount * kEditorPhysicsLayerCount)) {
				int32_t elementIndex = 9;
				for (int32_t firstLayer = 0; firstLayer < kEditorPhysicsLayerCount; ++firstLayer) {
					for (int32_t secondLayer = 0; secondLayer < kEditorPhysicsLayerCount; ++secondLayer) {
						loadedPhysicsSettings.layerCollisionMatrix[firstLayer][secondLayer] =
							ToInt(elements[static_cast<size_t>(elementIndex)]) != 0;
						elementIndex++;
					}
				}
			}

			constexpr size_t kPhysicsDebugSettingsIndex =
				9 + static_cast<size_t>(kEditorPhysicsLayerCount * kEditorPhysicsLayerCount);
			if (elements.size() >= kPhysicsDebugSettingsIndex + 6) {
				loadedPhysicsSettings.drawVelocityDebug = ToInt(elements[kPhysicsDebugSettingsIndex]) != 0;
				loadedPhysicsSettings.drawForceDirectionDebug = ToInt(elements[kPhysicsDebugSettingsIndex + 1]) != 0;
				loadedPhysicsSettings.drawFieldVolumeDebug = ToInt(elements[kPhysicsDebugSettingsIndex + 2]) != 0;
				loadedPhysicsSettings.drawConnectionDebug = ToInt(elements[kPhysicsDebugSettingsIndex + 3]) != 0;
				loadedPhysicsSettings.drawSelectedOnlyDebug = ToInt(elements[kPhysicsDebugSettingsIndex + 4]) != 0;
				loadedPhysicsSettings.debugVectorScale = ToFloat(elements[kPhysicsDebugSettingsIndex + 5]);
			}
		}
		else if (elements[0] == "GameObject" && elements.size() >= 13) {
			hasSceneData = true;  // GameObject 行が 1 つでもあれば有効な Scene
			// GameObject 行から ID / 親 / 名前 / Transform を復元する
			EditorGameObject gameObject{};
			gameObject.id = ToInt(elements[1]);
			gameObject.parentId = ToInt(elements[2]);
			gameObject.isActive = elements.size() >= 14 ? ToInt(elements[13]) != 0 : true;
			gameObject.name = DecodeSceneToken(elements[3]);
			gameObject.translate = {ToFloat(elements[4]), ToFloat(elements[5]), ToFloat(elements[6])};
			gameObject.rotate = {ToFloat(elements[7]), ToFloat(elements[8]), ToFloat(elements[9])};
			gameObject.scale = {ToFloat(elements[10]), ToFloat(elements[11]), ToFloat(elements[12])};
			loadedGameObjects.push_back(gameObject);
		}
		else if (elements[0] == "PrefabLink" && elements.size() >= 4) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				gameObject.prefabSourcePath = DecodeSceneToken(elements[2]);
				gameObject.prefabSourceObjectId = ToInt(elements[3]);

				if (elements.size() >= 5) {
					gameObject.prefabVariantBasePath = DecodeSceneToken(elements[4]);
				}

				break;
			}
		}
		else if (elements[0] == "Component" && elements.size() >= 9) {
			hasSceneData = true;  // Component 行だけ先に読んでも Scene テキストとしては有効
			int32_t ownerId = ToInt(elements[1]);  // Component 行は ownerId に一致する GameObject へ追加する
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				EditorComponent component = CreateComponent(ComponentTypeFromIndex(ToInt(elements[2])));  // 保存されていた ComponentType で初期値を作り、保存値で上書きする
				component.scriptProperties.clear();  // 後続の ScriptProperty 行から保存値を復元する。
				component.inputEventBindings.clear();  // 後続の InputEventBinding 行を重複させない。
				component.isActive = ToInt(elements[3]) != 0;
				component.assetPath = elements[4];
				component.color = {ToFloat(elements[5]), ToFloat(elements[6]), ToFloat(elements[7])};
				component.intensity = ToFloat(elements[8]);
				if (elements.size() >= 36) {
					component.mass = ToFloat(elements[9]);  // 物理と Input が追加された新形式 Scene の追加データ
					component.drag = ToFloat(elements[10]);
					component.useGravity = ToInt(elements[11]) != 0;
					component.isKinematic = ToInt(elements[12]) != 0;
					component.isTrigger = ToInt(elements[13]) != 0;
					component.bounciness = ToFloat(elements[14]);
					component.velocity = {ToFloat(elements[15]), ToFloat(elements[16]), ToFloat(elements[17])};
					component.colliderCenter = {ToFloat(elements[18]), ToFloat(elements[19]), ToFloat(elements[20])};
					component.colliderSize = {ToFloat(elements[21]), ToFloat(elements[22]), ToFloat(elements[23])};
					component.colliderRadius = ToFloat(elements[24]);
					component.inputMoveSpeed = ToFloat(elements[25]);
					component.inputForwardKey = ToInt(elements[26]);
					component.inputBackKey = ToInt(elements[27]);
					component.inputLeftKey = ToInt(elements[28]);
					component.inputRightKey = ToInt(elements[29]);
					component.inputJumpKey = ToInt(elements[30]);
					component.inputMouseSensitivity = ToFloat(elements[31]);
					component.inputInvertY = ToInt(elements[32]) != 0;
					component.hapticStrength = ToFloat(elements[33]);
					component.hapticDurationMs = ToInt(elements[34]);
					component.hapticLoop = ToInt(elements[35]) != 0;
				}
				if (elements.size() >= 40) {
					component.audioVolume = ToFloat(elements[36]);
					component.audioPitch = ToFloat(elements[37]);
					component.audioLoop = ToInt(elements[38]) != 0;
					component.audioPlayOnAwake = ToInt(elements[39]) != 0;
				}
				if (elements.size() >= 58) {
					component.angularVelocity = {ToFloat(elements[40]), ToFloat(elements[41]), ToFloat(elements[42])};
					component.angularDrag = ToFloat(elements[43]);
					component.freezePositionX = ToInt(elements[44]) != 0;
					component.freezePositionY = ToInt(elements[45]) != 0;
					component.freezePositionZ = ToInt(elements[46]) != 0;
					component.freezeRotationX = ToInt(elements[47]) != 0;
					component.freezeRotationY = ToInt(elements[48]) != 0;
					component.freezeRotationZ = ToInt(elements[49]) != 0;
					component.interpolationMode = ToInt(elements[50]);
					component.collisionDetectionMode = ToInt(elements[51]);
					component.dynamicFriction = ToFloat(elements[52]);
					component.staticFriction = ToFloat(elements[53]);
					component.frictionCombineMode = ToInt(elements[54]);
					component.bouncinessCombineMode = ToInt(elements[55]);
					component.physicsLayer = ToInt(elements[56]);
					component.generateContactEvents = ToInt(elements[57]) != 0;
				}
				if (elements.size() >= 68) {
					component.connectedGameObjectId = ToInt(elements[58]);  // Joint の接続先 ID。Scene 内 GameObject と対応させる
					component.jointAxis = {ToFloat(elements[59]), ToFloat(elements[60]), ToFloat(elements[61])};
					component.jointMinLimit = ToFloat(elements[62]);
					component.jointMaxLimit = ToFloat(elements[63]);
					component.jointMinDistance = ToFloat(elements[64]);
					component.jointMaxDistance = ToFloat(elements[65]);
					component.jointSpringFrequency = ToFloat(elements[66]);
					component.jointSpringDamping = ToFloat(elements[67]);
				}
				if (elements.size() >= 73) {
					component.inputActionMapName = elements[68];
					component.inputBehavior = ToInt(elements[69]);
					component.inputMoveEventName = elements[70];
					component.inputJumpEventName = elements[71];
					component.inputFireEventName = elements[72];
				}
				if (elements.size() >= 87) {
					component.navAgentRadius = ToFloat(elements[73]);
					component.navAgentHeight = ToFloat(elements[74]);
					component.navMaxSpeed = ToFloat(elements[75]);
					component.navMaxAcceleration = ToFloat(elements[76]);
					component.navStoppingDistance = ToFloat(elements[77]);
					component.navAutoRepath = ToInt(elements[78]) != 0;
					component.navCarve = ToInt(elements[79]) != 0;
					component.navMaxSlope = ToFloat(elements[80]);
					component.navMaxClimb = ToFloat(elements[81]);
					component.navAreaOverride = ToInt(elements[82]) != 0;
					component.navArea = ToInt(elements[83]);
					component.navIgnoreFromBuild = ToInt(elements[84]) != 0;
					component.navBidirectional = ToInt(elements[85]) != 0;
					component.navCostModifier = ToFloat(elements[86]);
				}
				if (elements.size() >= 89) {
					component.rollingTorque = ToFloat(elements[87]);
					component.rollingHorsepower = ToFloat(elements[88]);
				}
				if (elements.size() >= 102) {
					component.constraintWeight = ToFloat(elements[89]);
					component.constraintPositionOffset = {ToFloat(elements[90]), ToFloat(elements[91]), ToFloat(elements[92])};
					component.constraintRotationOffset = {ToFloat(elements[93]), ToFloat(elements[94]), ToFloat(elements[95])};
					component.constraintAimAxis = ToInt(elements[96]);
					component.constraintUpAxis = ToInt(elements[97]);
					component.constraintRoll = ToFloat(elements[98]);
					component.constraintFreezeAxisX = ToInt(elements[99]) != 0;
					component.constraintFreezeAxisY = ToInt(elements[100]) != 0;
					component.constraintFreezeAxisZ = ToInt(elements[101]) != 0;
				}
				if (elements.size() >= 107) {
					component.animationSpeed = ToFloat(elements[102]);
					component.animationLoop = ToInt(elements[103]) != 0;
					component.animationPlayOnAwake = ToInt(elements[104]) != 0;
					component.animationType = ToInt(elements[105]);
					component.animationAmplitude = ToFloat(elements[106]);
				}
				if (elements.size() >= 112) {
					component.animatorState = ToInt(elements[107]);
					component.particleRate = ToFloat(elements[108]);
					component.particleLifetime = ToFloat(elements[109]);
					component.particleSpeed = ToFloat(elements[110]);
					component.particleSize = ToFloat(elements[111]);
				}
				if (elements.size() >= 117) {
					component.metallic = ToFloat(elements[112]);
					component.roughness = ToFloat(elements[113]);
					component.ior = ToFloat(elements[114]);
					component.alpha = ToFloat(elements[115]);
					component.reflectionStrength = ToFloat(elements[116]);
				}
				if (elements.size() >= 119) {
					component.textureAssetPath = elements[117];
					component.emissionStrength = ToFloat(elements[118]);
				}
				if (elements.size() >= 124) {
					component.bloomIntensity = ToFloat(elements[119]);
					component.finalBrightness = ToFloat(elements[120]);
					component.smaaEnabled = ToInt(elements[121]) != 0;
					component.taaEnabled = ToInt(elements[122]) != 0;
					component.ssrEnabled = ToInt(elements[123]) != 0;
				}
				if (elements.size() >= 130) {
					component.skyLowerColor = {ToFloat(elements[124]), ToFloat(elements[125]), ToFloat(elements[126])};
					component.environmentTextureRotation = ToFloat(elements[127]);
					component.environmentTextureMipBias = ToFloat(elements[128]);
					component.environmentTextureEnabled = ToInt(elements[129]) != 0;
				}
				if (elements.size() >= 141) {
					component.cameraFieldOfView = ToFloat(elements[130]);
					component.cameraNearClip = ToFloat(elements[131]);
					component.cameraFarClip = ToFloat(elements[132]);
					component.cameraProjectionMode = ToInt(elements[133]);
					component.cameraDofEnabled = ToInt(elements[134]) != 0;
					component.cameraDofFocusDistance = ToFloat(elements[135]);
					component.cameraDofAperture = ToFloat(elements[136]);
					component.cameraDofFocalLength = ToFloat(elements[137]);
					component.cameraMotionBlurEnabled = ToInt(elements[138]) != 0;
					component.cameraMotionBlurIntensity = ToFloat(elements[139]);
					component.cameraExposure = ToFloat(elements[140]);
				}
				if (elements.size() >= 144) {
					component.bloomThreshold = ToFloat(elements[141]);
					component.bloomSoftKnee = ToFloat(elements[142]);
					component.bloomScatter = ToFloat(elements[143]);
				}
				if (elements.size() >= 145) {
					component.aaMode = ToInt(elements[144]);
				} else {
					// 旧形式: smaaEnabled/taaEnabled から aaMode を推定
					if (component.taaEnabled) {
						component.aaMode = 3;
					} else if (component.smaaEnabled) {
						component.aaMode = 2;
					} else {
						component.aaMode = 0;
					}
				}
				if (elements.size() >= 156) {
					component.compositeExposure = ToFloat(elements[145]);
					component.compositeWhitePoint = ToFloat(elements[146]);
					component.compositeToneMappingMode = ToInt(elements[147]);
					component.compositeBloomIntensity = ToFloat(elements[148]);
					component.compositeSaturation = ToFloat(elements[149]);
					component.compositeContrast = ToFloat(elements[150]);
					component.compositeVignetteStrength = ToFloat(elements[151]);
					component.compositeVignetteRadius = ToFloat(elements[152]);
					component.compositeFilmGrain = ToFloat(elements[153]);
					component.compositeChromaticAberration = ToFloat(elements[154]);
					component.compositeAmbientOcclusionStrength = ToFloat(elements[155]);
				}
				if (elements.size() >= 169) {
					component.buttonLabel = DecodeSceneToken(elements[156]);
					component.buttonPosition = {ToFloat(elements[157]), ToFloat(elements[158])};
					component.buttonSize = {ToFloat(elements[159]), ToFloat(elements[160])};
					component.buttonInteractable = ToInt(elements[161]) != 0;
					component.buttonOnClickFunction = DecodeSceneToken(elements[162]);
					component.buttonHoverColor = {ToFloat(elements[163]), ToFloat(elements[164]), ToFloat(elements[165])};
					component.buttonPressedColor = {ToFloat(elements[166]), ToFloat(elements[167]), ToFloat(elements[168])};
				}
			if (elements.size() >= 175) {
				component.toggleValue = ToInt(elements[169]) != 0;
				component.toggleOnValueChangedFunction = DecodeSceneToken(elements[170]);
				component.sliderValue = ToFloat(elements[171]);
				component.sliderMinValue = ToFloat(elements[172]);
				component.sliderMaxValue = ToFloat(elements[173]);
				component.sliderOnValueChangedFunction = DecodeSceneToken(elements[174]);
			}
			if (elements.size() >= 178) {
				component.audioSpatialBlend = ToFloat(elements[175]);
				component.audioMinDistance = ToFloat(elements[176]);
				component.audioMaxDistance = ToFloat(elements[177]);
			}
				if (elements.size() >= 182) {
					component.smaaThreshold = ToFloat(elements[178]);
					component.smaaCornerRounding = ToFloat(elements[179]);
					component.temporalSharpness = ToFloat(elements[180]);
					component.temporalBlendRatio = ToFloat(elements[181]);
				}
				if (elements.size() >= 194) {
					component.glareMode = ToInt(elements[182]);
					component.glareIntensity = ToFloat(elements[183]);
					component.glareSize = ToFloat(elements[184]);
					component.glareAngle = ToFloat(elements[185]);
					component.glareStreakCount = ToInt(elements[186]);
					component.glareFade = ToFloat(elements[187]);
					component.glareColorModulation = ToFloat(elements[188]);
					component.glareCenter = {
						ToFloat(elements[189]),
						ToFloat(elements[190]),
						ToFloat(elements[191])
					};
					component.filterMode = ToInt(elements[192]);
					component.filterStrength = ToFloat(elements[193]);
				}
				if (elements.size() >= 223) {
					component.normalTextureAssetPath = DecodeSceneToken(elements[194]);
					component.metallicTextureAssetPath = DecodeSceneToken(elements[195]);
					component.roughnessTextureAssetPath = DecodeSceneToken(elements[196]);
					component.ambientOcclusionTextureAssetPath = DecodeSceneToken(elements[197]);
					component.emissionTextureAssetPath = DecodeSceneToken(elements[198]);
					component.heightTextureAssetPath = DecodeSceneToken(elements[199]);
					component.opacityTextureAssetPath = DecodeSceneToken(elements[200]);
					component.emissionColor = {
						ToFloat(elements[201]),
						ToFloat(elements[202]),
						ToFloat(elements[203])
					};
					component.normalScale = ToFloat(elements[204]);
					component.ambientOcclusionStrength = ToFloat(elements[205]);
					component.heightScale = ToFloat(elements[206]);
					component.alphaCutoff = ToFloat(elements[207]);
					component.clearCoat = ToFloat(elements[208]);
					component.clearCoatRoughness = ToFloat(elements[209]);
					component.transmission = ToFloat(elements[210]);
					component.subsurface = ToFloat(elements[211]);
					component.anisotropy = ToFloat(elements[212]);
					component.anisotropyRotation = ToFloat(elements[213]);
					component.specularTint = ToFloat(elements[214]);
					component.sheen = ToFloat(elements[215]);
					component.sheenTint = ToFloat(elements[216]);
					component.alphaMode = ToInt(elements[217]);
					component.doubleSided = ToInt(elements[218]) != 0;
					component.uvTiling = {ToFloat(elements[219]), ToFloat(elements[220])};
					component.uvOffset = {ToFloat(elements[221]), ToFloat(elements[222])};
				}
				if (elements.size() >= 224) {
					component.animationClipIndex = ToInt(elements[223]);
				}
				if (elements.size() >= 227) {
					component.useImportedMaterialTextures = ToInt(elements[224]) != 0;
					component.uvLayoutTextureAssetPath = DecodeSceneToken(elements[225]);
					component.glareModeMask = ToInt(elements[226]);
				}
				else {
					// 旧Sceneの単一 Glare を同じ見た目のビットへ変換する。
					component.glareModeMask = component.glareMode > 0
						? 1 << component.glareMode
						: 0;
				}
				if (elements.size() >= 228) {
					component.filterModeMask = ToInt(elements[227]);
				}
				else {
					// 旧Sceneの単一 Filter を同じ見た目のビットへ変換する。
					component.filterModeMask = component.filterMode > 0
						? 1 << component.filterMode
						: 0;
				}

				ApplyPostProcessEffectDefaults(component);

				size_t postProcessEffectCursor = 228u;
				for (int32_t glareModeIndex = 1; glareModeIndex < kGlareModeCount; glareModeIndex++) {
					if (elements.size() < postProcessEffectCursor + 12u) {
						break;
					}

					const size_t glareModeArrayIndex = static_cast<size_t>(glareModeIndex);
					component.glareIntensityByMode[glareModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 0u]);
					component.glareSizeByMode[glareModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 1u]);
					component.glareAngleByMode[glareModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 2u]);
					component.glareStreakCountByMode[glareModeArrayIndex] = ToInt(elements[postProcessEffectCursor + 3u]);
					component.glareFadeByMode[glareModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 4u]);
					component.glareColorModulationByMode[glareModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 5u]);
					component.glareCenterByMode[glareModeArrayIndex] = {
						ToFloat(elements[postProcessEffectCursor + 6u]),
						ToFloat(elements[postProcessEffectCursor + 7u]),
						ToFloat(elements[postProcessEffectCursor + 8u])
					};
					component.glareColorByMode[glareModeArrayIndex] = {
						ToFloat(elements[postProcessEffectCursor + 9u]),
						ToFloat(elements[postProcessEffectCursor + 10u]),
						ToFloat(elements[postProcessEffectCursor + 11u])
					};
					postProcessEffectCursor += 12u;
				}

				for (int32_t filterModeIndex = 1; filterModeIndex < kFilterModeCount; filterModeIndex++) {
					if (elements.size() < postProcessEffectCursor + 4u) {
						break;
					}

					const size_t filterModeArrayIndex = static_cast<size_t>(filterModeIndex);
					component.filterStrengthByMode[filterModeArrayIndex] = ToFloat(elements[postProcessEffectCursor + 0u]);
					component.filterColorByMode[filterModeArrayIndex] = {
						ToFloat(elements[postProcessEffectCursor + 1u]),
						ToFloat(elements[postProcessEffectCursor + 2u]),
						ToFloat(elements[postProcessEffectCursor + 3u])
					};
					postProcessEffectCursor += 4u;
				}

				// 旧 Scene はここで要素が終わる。新しい拡張値が全てある時だけまとめて復元する。
				if (elements.size() >= postProcessEffectCursor + 37u) {
					component.animatorApplyRootMotion = ToInt(elements[postProcessEffectCursor + 0u]) != 0;
					component.animatorAutoVelocity = ToInt(elements[postProcessEffectCursor + 1u]) != 0;
					component.animatorTransitionDuration = ToFloat(elements[postProcessEffectCursor + 2u]);
					component.animatorMoveX = ToFloat(elements[postProcessEffectCursor + 3u]);
					component.animatorMoveY = ToFloat(elements[postProcessEffectCursor + 4u]);
					component.animatorSpeedParameter = ToFloat(elements[postProcessEffectCursor + 5u]);
					component.animatorIdleClipIndex = ToInt(elements[postProcessEffectCursor + 6u]);
					component.animatorForwardClipIndex = ToInt(elements[postProcessEffectCursor + 7u]);
					component.animatorBackwardClipIndex = ToInt(elements[postProcessEffectCursor + 8u]);
					component.animatorLeftClipIndex = ToInt(elements[postProcessEffectCursor + 9u]);
					component.animatorRightClipIndex = ToInt(elements[postProcessEffectCursor + 10u]);
					component.particleMaxCount = ToInt(elements[postProcessEffectCursor + 11u]);
					component.particleBurstCount = ToInt(elements[postProcessEffectCursor + 12u]);
					component.particleShape = ToInt(elements[postProcessEffectCursor + 13u]);
					component.particleSimulationSpace = ToInt(elements[postProcessEffectCursor + 14u]);
					component.particleDuration = ToFloat(elements[postProcessEffectCursor + 15u]);
					component.particleStartDelay = ToFloat(elements[postProcessEffectCursor + 16u]);
					component.particleGravity = ToFloat(elements[postProcessEffectCursor + 17u]);
					component.particleDrag = ToFloat(elements[postProcessEffectCursor + 18u]);
					component.particleEndSize = ToFloat(elements[postProcessEffectCursor + 19u]);
					component.particleShapeRadius = ToFloat(elements[postProcessEffectCursor + 20u]);
					component.particleShapeAngle = ToFloat(elements[postProcessEffectCursor + 21u]);
					component.particleSpeedRandomness = ToFloat(elements[postProcessEffectCursor + 22u]);
					component.particleLifetimeRandomness = ToFloat(elements[postProcessEffectCursor + 23u]);
					component.particleSizeRandomness = ToFloat(elements[postProcessEffectCursor + 24u]);
					component.particleRotationSpeed = ToFloat(elements[postProcessEffectCursor + 25u]);
					component.particleLooping = ToInt(elements[postProcessEffectCursor + 26u]) != 0;
					component.particleCollision = ToInt(elements[postProcessEffectCursor + 27u]) != 0;
					component.particleEndColor = {
						ToFloat(elements[postProcessEffectCursor + 28u]),
						ToFloat(elements[postProcessEffectCursor + 29u]),
						ToFloat(elements[postProcessEffectCursor + 30u])};
					component.particleDirection = {
						ToFloat(elements[postProcessEffectCursor + 31u]),
						ToFloat(elements[postProcessEffectCursor + 32u]),
						ToFloat(elements[postProcessEffectCursor + 33u])};
					component.particleBoxSize = {
						ToFloat(elements[postProcessEffectCursor + 34u]),
						ToFloat(elements[postProcessEffectCursor + 35u]),
						ToFloat(elements[postProcessEffectCursor + 36u])};
				}

				if (elements.size() >= postProcessEffectCursor + 46u) {
					component.particleStartAlpha = ToFloat(elements[postProcessEffectCursor + 37u]);
					component.particleEndAlpha = ToFloat(elements[postProcessEffectCursor + 38u]);
					component.particleEmissionStrength = ToFloat(elements[postProcessEffectCursor + 39u]);
					component.particleEndSpeedMultiplier = ToFloat(elements[postProcessEffectCursor + 40u]);
					component.particleNoiseStrength = ToFloat(elements[postProcessEffectCursor + 41u]);
					component.particleNoiseFrequency = ToFloat(elements[postProcessEffectCursor + 42u]);
					component.particleCollisionBounce = ToFloat(elements[postProcessEffectCursor + 43u]);
					component.particleCollisionFriction = ToFloat(elements[postProcessEffectCursor + 44u]);
				component.particlePrewarm = ToInt(elements[postProcessEffectCursor + 45u]) != 0;
				}

				if (elements.size() >= postProcessEffectCursor + 54u) {
					component.freeMoveSpeed = ToFloat(elements[postProcessEffectCursor + 46u]);
					component.freeRotateSpeed = ToFloat(elements[postProcessEffectCursor + 47u]);
					component.freeMoveAxes = ToInt(elements[postProcessEffectCursor + 48u]);
					component.freeRotateAxes = ToInt(elements[postProcessEffectCursor + 49u]);
					component.freeUseLocalSpace = ToInt(elements[postProcessEffectCursor + 50u]) != 0;
					component.freeRotationInput = {
						ToFloat(elements[postProcessEffectCursor + 51u]),
						ToFloat(elements[postProcessEffectCursor + 52u]),
						ToFloat(elements[postProcessEffectCursor + 53u])
					};
				}

				if (elements.size() >= postProcessEffectCursor + 64u) {
					component.particleMotionType = ToInt(elements[postProcessEffectCursor + 54u]);
					component.particleMotionCenter = {
						ToFloat(elements[postProcessEffectCursor + 55u]),
						ToFloat(elements[postProcessEffectCursor + 56u]),
						ToFloat(elements[postProcessEffectCursor + 57u])
					};
					component.particleAngularSpeed = ToFloat(elements[postProcessEffectCursor + 58u]);
					component.particleRadialAcceleration = ToFloat(elements[postProcessEffectCursor + 59u]);
					component.particleWaveAmplitude = ToFloat(elements[postProcessEffectCursor + 60u]);
					component.particleWaveFrequency = ToFloat(elements[postProcessEffectCursor + 61u]);
					component.particleAttractorStrength = ToFloat(elements[postProcessEffectCursor + 62u]);
					component.particleRenderAssetPath = DecodeSceneToken(elements[postProcessEffectCursor + 63u]);
				}

				if (elements.size() >= postProcessEffectCursor + 65u) {
					component.lightingMode =
						(std::clamp)(ToInt(elements[postProcessEffectCursor + 64u]), 0, 3);
				}

				// Ocean 値は既存 Scene の全フィールドより後ろへ追加し、旧形式をそのまま読み込めるようにする。
				const size_t oceanCursor = postProcessEffectCursor + 65u;
				if (elements.size() >= oceanCursor + 24u) {
					component.oceanGridResolution = ToInt(elements[oceanCursor + 0u]);
					component.oceanSize = ToFloat(elements[oceanCursor + 1u]);
					component.oceanWaveHeight = ToFloat(elements[oceanCursor + 2u]);
					component.oceanMaxWaveHeight = ToFloat(elements[oceanCursor + 3u]);
					component.oceanWaveLength = ToFloat(elements[oceanCursor + 4u]);
					component.oceanWaveSpeed = ToFloat(elements[oceanCursor + 5u]);
					component.oceanTimeScale = ToFloat(elements[oceanCursor + 6u]);
					component.oceanChoppiness = ToFloat(elements[oceanCursor + 7u]);
					component.oceanPrimaryDirection = {
						ToFloat(elements[oceanCursor + 8u]),
						ToFloat(elements[oceanCursor + 9u])};
					component.oceanSecondaryDirection = {
						ToFloat(elements[oceanCursor + 10u]),
						ToFloat(elements[oceanCursor + 11u])};
					component.oceanSecondaryWaveScale = ToFloat(elements[oceanCursor + 12u]);
					component.oceanRippleScale = ToFloat(elements[oceanCursor + 13u]);
					component.oceanRippleStrength = ToFloat(elements[oceanCursor + 14u]);
					component.oceanFoamStrength = ToFloat(elements[oceanCursor + 15u]);
					component.oceanRoughness = ToFloat(elements[oceanCursor + 16u]);
					component.oceanReflectionStrength = ToFloat(elements[oceanCursor + 17u]);
					component.oceanShallowColor = {
						ToFloat(elements[oceanCursor + 18u]),
						ToFloat(elements[oceanCursor + 19u]),
						ToFloat(elements[oceanCursor + 20u])};
					component.oceanDeepColor = {
						ToFloat(elements[oceanCursor + 21u]),
						ToFloat(elements[oceanCursor + 22u]),
						ToFloat(elements[oceanCursor + 23u])};
				}

				// Buoyancy 値も末尾へ追加し、Ocean までの Scene を変更なしで読み込めるようにする。
				const size_t buoyancyCursor = oceanCursor + 24u;
				if (elements.size() >= buoyancyCursor + 14u) {
					component.buoyancyOceanGameObjectId = ToInt(elements[buoyancyCursor + 0u]);
					component.buoyancyCenterOffset = {
						ToFloat(elements[buoyancyCursor + 1u]),
						ToFloat(elements[buoyancyCursor + 2u]),
						ToFloat(elements[buoyancyCursor + 3u])};
					component.buoyancyHullSize = {
						ToFloat(elements[buoyancyCursor + 4u]),
						ToFloat(elements[buoyancyCursor + 5u]),
						ToFloat(elements[buoyancyCursor + 6u])};
					component.buoyancyStrength = ToFloat(elements[buoyancyCursor + 7u]);
					component.buoyancyMaxSubmersion = ToFloat(elements[buoyancyCursor + 8u]);
					component.buoyancyDamping = ToFloat(elements[buoyancyCursor + 9u]);
					component.buoyancyWaterDrag = ToFloat(elements[buoyancyCursor + 10u]);
					component.buoyancyAngularDrag = ToFloat(elements[buoyancyCursor + 11u]);
					component.buoyancyNormalInfluence = ToFloat(elements[buoyancyCursor + 12u]);
					component.buoyancyUseCenterPoint = ToInt(elements[buoyancyCursor + 13u]) != 0;
				}

				// 高品質 Ocean 値は Buoyancy より後ろへ追加し、従来の列位置を維持する。
				const size_t advancedOceanCursor = buoyancyCursor + 14u;
				if (elements.size() >= advancedOceanCursor + 10u) {
					component.oceanWindSpeed = ToFloat(elements[advancedOceanCursor + 0u]);
					component.oceanWaterDepth = ToFloat(elements[advancedOceanCursor + 1u]);
					component.oceanDirectionSpread = ToFloat(elements[advancedOceanCursor + 2u]);
					component.oceanSwellStrength = ToFloat(elements[advancedOceanCursor + 3u]);
					component.oceanSpectrumSeed = ToFloat(elements[advancedOceanCursor + 4u]);
					component.oceanCrestSharpness = ToFloat(elements[advancedOceanCursor + 5u]);
					component.oceanFoamThreshold = ToFloat(elements[advancedOceanCursor + 6u]);
					component.oceanDetailNormalStrength = ToFloat(elements[advancedOceanCursor + 7u]);
					component.oceanAbsorptionDistance = ToFloat(elements[advancedOceanCursor + 8u]);
					component.oceanRefractionDistortion = ToFloat(elements[advancedOceanCursor + 9u]);
				}

				// Rail Movement は全 Ocean 設定より後ろへ追加し、従来 Scene の列位置を維持する。
				const size_t railMovementCursor = advancedOceanCursor + 10u;
				if (elements.size() >= railMovementCursor + 7u) {
					component.railPathGameObjectId = ToInt(elements[railMovementCursor + 0u]);
					component.railSpeed = ToFloat(elements[railMovementCursor + 1u]);
					component.railStartNormalized = ToFloat(elements[railMovementCursor + 2u]);
					component.railLookAheadDistance = ToFloat(elements[railMovementCursor + 3u]);
					component.railLoop = ToInt(elements[railMovementCursor + 4u]) != 0;
					component.railOrientToPath = ToInt(elements[railMovementCursor + 5u]) != 0;
					component.railUseSmoothCurve = ToInt(elements[railMovementCursor + 6u]) != 0;
				}

				// 旧 RailShooter 列は Scene の数値レイアウト互換だけに読み込む。Runtime は参照しない。
				const size_t legacyRailShooterCursor = railMovementCursor + 7u;
				if (elements.size() >= legacyRailShooterCursor + 8u) {
					component.healthMaximum = ToFloat(elements[legacyRailShooterCursor + 0u]);
					component.healthCurrent = ToFloat(elements[legacyRailShooterCursor + 1u]);
					component.enemySpawnFollowerGameObjectId = ToInt(elements[legacyRailShooterCursor + 2u]);
					component.enemySpawnNormalized = ToFloat(elements[legacyRailShooterCursor + 3u]);
					component.enemyAttackTargetGameObjectId = ToInt(elements[legacyRailShooterCursor + 4u]);
					component.enemyAttackInterval = ToFloat(elements[legacyRailShooterCursor + 5u]);
					component.enemyAttackRange = ToFloat(elements[legacyRailShooterCursor + 6u]);
					component.enemyAttackDamage = ToFloat(elements[legacyRailShooterCursor + 7u]);
				}

				if (elements.size() >= legacyRailShooterCursor + 13u) {
					component.enemyProjectileTemplateGameObjectId = ToInt(elements[legacyRailShooterCursor + 8u]);
					component.enemyProjectilePoolSize = ToInt(elements[legacyRailShooterCursor + 9u]);
					component.enemyProjectileSpeed = ToFloat(elements[legacyRailShooterCursor + 10u]);
					component.enemyProjectileHitRadius = ToFloat(elements[legacyRailShooterCursor + 11u]);
					component.enemyProjectileLifetime = ToFloat(elements[legacyRailShooterCursor + 12u]);
				}

				// 旧制作支援列も保存互換のためだけに読み込む。
				const size_t railAuthoringCursor = legacyRailShooterCursor + 13u;
				if (elements.size() >= railAuthoringCursor + 28u) {
					component.railShipSpeedSourceGameObjectId = ToInt(elements[railAuthoringCursor + 0u]);
					component.railShipSailGameObjectId = ToInt(elements[railAuthoringCursor + 1u]);
					component.railShipWakeEffectGameObjectId = ToInt(elements[railAuthoringCursor + 2u]);
					component.railShipWindEffectGameObjectId = ToInt(elements[railAuthoringCursor + 3u]);
					component.railShipEffectStartSpeed = ToFloat(elements[railAuthoringCursor + 4u]);
					component.railShipEffectFullSpeed = ToFloat(elements[railAuthoringCursor + 5u]);
					component.railShipSailMinimumSpeed = ToFloat(elements[railAuthoringCursor + 6u]);
					component.railShipSailMaximumSpeed = ToFloat(elements[railAuthoringCursor + 7u]);
					component.enemyMotionPattern = ToInt(elements[railAuthoringCursor + 8u]);
					component.enemyMotionAmplitude.x = ToFloat(elements[railAuthoringCursor + 9u]);
					component.enemyMotionAmplitude.y = ToFloat(elements[railAuthoringCursor + 10u]);
					component.enemyMotionAmplitude.z = ToFloat(elements[railAuthoringCursor + 11u]);
					component.enemyMotionFrequency = ToFloat(elements[railAuthoringCursor + 12u]);
					component.enemyMotionPhase = ToFloat(elements[railAuthoringCursor + 13u]);
					component.enemyMotionTargetGameObjectId = ToInt(elements[railAuthoringCursor + 14u]);
					component.enemyMotionSpeed = ToFloat(elements[railAuthoringCursor + 15u]);
					component.enemyMotionLookAtTarget = ToInt(elements[railAuthoringCursor + 16u]) != 0;
					component.stageFollowerGameObjectId = ToInt(elements[railAuthoringCursor + 17u]);
					component.stageStartMarkerGameObjectId = ToInt(elements[railAuthoringCursor + 18u]);
					component.stageGoalMarkerGameObjectId = ToInt(elements[railAuthoringCursor + 19u]);
					component.stageStartEffectGameObjectId = ToInt(elements[railAuthoringCursor + 20u]);
					component.stageGoalEffectGameObjectId = ToInt(elements[railAuthoringCursor + 21u]);
					component.stageStartDelay = ToFloat(elements[railAuthoringCursor + 22u]);
					component.stageGoalRadius = ToFloat(elements[railAuthoringCursor + 23u]);
					component.stageGoalDelay = ToFloat(elements[railAuthoringCursor + 24u]);
					component.stageNextScenePath = DecodeSceneToken(elements[railAuthoringCursor + 25u]);
					component.stageSelectScenePath = DecodeSceneToken(elements[railAuthoringCursor + 26u]);
					component.sceneButtonScenePath = DecodeSceneToken(elements[railAuthoringCursor + 27u]);
				}

				// AudioSource の高度設定は全既存列より後ろへ追加し、旧 Scene の読み込みを維持する。
				const size_t advancedAudioCursor = railAuthoringCursor + 28u;
				if (elements.size() >= advancedAudioCursor + 3u) {
					component.audioBus = ToInt(elements[advancedAudioCursor + 0u]);
					component.audioMaxVoices = ToInt(elements[advancedAudioCursor + 1u]);
					component.audioRetriggerInterval = ToFloat(elements[advancedAudioCursor + 2u]);
				}

				const size_t postProcessColorCursor = advancedAudioCursor + 3u;
				if (elements.size() >= postProcessColorCursor + 14u) {
					component.compositeAutoExposureEnabled =
						ToInt(elements[postProcessColorCursor + 0u]) != 0;
					component.compositeMinimumExposure = ToFloat(elements[postProcessColorCursor + 1u]);
					component.compositeMaximumExposure = ToFloat(elements[postProcessColorCursor + 2u]);
					component.compositeExposureAdaptationSpeed = ToFloat(elements[postProcessColorCursor + 3u]);
					component.compositeTargetLuminance = ToFloat(elements[postProcessColorCursor + 4u]);
					component.compositeTemperature = ToFloat(elements[postProcessColorCursor + 5u]);
					component.compositeTint = ToFloat(elements[postProcessColorCursor + 6u]);
					component.compositeLift = {
						ToFloat(elements[postProcessColorCursor + 7u]),
						ToFloat(elements[postProcessColorCursor + 8u]),
						ToFloat(elements[postProcessColorCursor + 9u])};
					component.compositeGamma = ToFloat(elements[postProcessColorCursor + 10u]);
					component.compositeGain = {
						ToFloat(elements[postProcessColorCursor + 11u]),
						ToFloat(elements[postProcessColorCursor + 12u]),
						ToFloat(elements[postProcessColorCursor + 13u])};
				}

				// レール制作と空間音声の追加値は既存の全列より後ろへ置く。
				const size_t railProductionCursor = postProcessColorCursor + 14u;
				if (elements.size() >= railProductionCursor + 33u) {
					component.audioDopplerLevel = ToFloat(elements[railProductionCursor + 0u]);
					component.audioSpread = ToFloat(elements[railProductionCursor + 1u]);
					component.audioConeInnerAngle = ToFloat(elements[railProductionCursor + 2u]);
					component.audioConeOuterAngle = ToFloat(elements[railProductionCursor + 3u]);
					component.audioConeOuterVolume = ToFloat(elements[railProductionCursor + 4u]);
					component.audioOcclusionStrength = ToFloat(elements[railProductionCursor + 5u]);
					component.audioReverbSend = ToFloat(elements[railProductionCursor + 6u]);
					component.audioReflectionStrength = ToFloat(elements[railProductionCursor + 7u]);
					component.enemyWaveIndex = ToInt(elements[railProductionCursor + 8u]);
					component.enemyFormationPattern = ToInt(elements[railProductionCursor + 9u]);
					component.enemyFormationSlot = ToInt(elements[railProductionCursor + 10u]);
					component.enemyFormationSpacing = ToFloat(elements[railProductionCursor + 11u]);
					component.railAimMouseSensitivity = ToFloat(elements[railProductionCursor + 12u]);
					component.railAimGamepadSensitivity = ToFloat(elements[railProductionCursor + 13u]);
					component.railAimAssistRadius = ToFloat(elements[railProductionCursor + 14u]);
					component.railAimInvertY = ToInt(elements[railProductionCursor + 15u]) != 0;
					component.railEventFollowerGameObjectId = ToInt(elements[railProductionCursor + 16u]);
					component.railEventNormalized = ToFloat(elements[railProductionCursor + 17u]);
					component.railEventType = ToInt(elements[railProductionCursor + 18u]);
					component.railEventTargetGameObjectId = ToInt(elements[railProductionCursor + 19u]);
					component.railEventDuration = ToFloat(elements[railProductionCursor + 20u]);
					component.railEventText = DecodeSceneToken(elements[railProductionCursor + 21u]);
					component.railEventPauseRail = ToInt(elements[railProductionCursor + 22u]) != 0;
					component.bossPhaseTwoHealthRatio = ToFloat(elements[railProductionCursor + 23u]);
					component.bossPhaseThreeHealthRatio = ToFloat(elements[railProductionCursor + 24u]);
					component.bossPhaseOneMotionPattern = ToInt(elements[railProductionCursor + 25u]);
					component.bossPhaseTwoMotionPattern = ToInt(elements[railProductionCursor + 26u]);
					component.bossPhaseThreeMotionPattern = ToInt(elements[railProductionCursor + 27u]);
					component.bossPhaseOneAttackInterval = ToFloat(elements[railProductionCursor + 28u]);
					component.bossPhaseTwoAttackInterval = ToFloat(elements[railProductionCursor + 29u]);
					component.bossPhaseThreeAttackInterval = ToFloat(elements[railProductionCursor + 30u]);
					component.railHudBindingType = ToInt(elements[railProductionCursor + 31u]);
					component.railHudSourceGameObjectId = ToInt(elements[railProductionCursor + 32u]);
				}

				// RailFollower の実行制御値は既存の制作支援列より後ろへ追加する。
				const size_t railFollowerCursor = railProductionCursor + 33u;
				if (elements.size() >= railFollowerCursor + 5u) {
					component.railAcceleration = ToFloat(elements[railFollowerCursor + 0u]);
					component.railDeceleration = ToFloat(elements[railFollowerCursor + 1u]);
					component.railStartPaused = ToInt(elements[railFollowerCursor + 2u]) != 0;
					component.railReverse = ToInt(elements[railFollowerCursor + 3u]) != 0;
					component.railStopAtEnd = ToInt(elements[railFollowerCursor + 4u]) != 0;
				}

				// 汎用 Wave / Event / State / UI Binding は旧 RailShooter 列を再利用せず末尾へ追加する。
				const size_t genericGameplayCursor = railFollowerCursor + 5u;
				if (elements.size() >= genericGameplayCursor + 23u) {
					component.waveTriggerMode = ToInt(elements[genericGameplayCursor + 0u]);
					component.waveTriggerSourceGameObjectId = ToInt(elements[genericGameplayCursor + 1u]);
					component.waveTriggerValue = ToFloat(elements[genericGameplayCursor + 2u]);
					component.waveSpawnInterval = ToFloat(elements[genericGameplayCursor + 3u]);
					component.waveDeactivateChildrenOnStart = ToInt(elements[genericGameplayCursor + 4u]) != 0;
					component.timelineSourceMode = ToInt(elements[genericGameplayCursor + 5u]);
					component.timelineSourceGameObjectId = ToInt(elements[genericGameplayCursor + 6u]);
					component.timelineTriggerValue = ToFloat(elements[genericGameplayCursor + 7u]);
					component.timelineTargetGameObjectId = ToInt(elements[genericGameplayCursor + 8u]);
					component.timelineActionName = DecodeSceneToken(elements[genericGameplayCursor + 9u]);
					component.timelineTriggerOnce = ToInt(elements[genericGameplayCursor + 10u]) != 0;
					component.thresholdSourceMode = ToInt(elements[genericGameplayCursor + 11u]);
					component.thresholdSourceGameObjectId = ToInt(elements[genericGameplayCursor + 12u]);
					component.thresholdSecondValue = ToFloat(elements[genericGameplayCursor + 13u]);
					component.thresholdThirdValue = ToFloat(elements[genericGameplayCursor + 14u]);
					component.thresholdFirstActionName = DecodeSceneToken(elements[genericGameplayCursor + 15u]);
					component.thresholdSecondActionName = DecodeSceneToken(elements[genericGameplayCursor + 16u]);
					component.thresholdThirdActionName = DecodeSceneToken(elements[genericGameplayCursor + 17u]);
					component.uiBindingSourceGameObjectId = ToInt(elements[genericGameplayCursor + 18u]);
					component.uiBindingValueType = ToInt(elements[genericGameplayCursor + 19u]);
					component.uiBindingPrefix = DecodeSceneToken(elements[genericGameplayCursor + 20u]);
					component.uiBindingPrecision = ToInt(elements[genericGameplayCursor + 21u]);
					component.uiBindingScale = ToFloat(elements[genericGameplayCursor + 22u]);
				}

				if (elements.size() >= genericGameplayCursor + 28u) {
					component.waveActionTargetGameObjectId = ToInt(elements[genericGameplayCursor + 23u]);
					component.waveStartedActionName = DecodeSceneToken(elements[genericGameplayCursor + 24u]);
					component.waveSpawnedActionName = DecodeSceneToken(elements[genericGameplayCursor + 25u]);
					component.waveCompletedActionName = DecodeSceneToken(elements[genericGameplayCursor + 26u]);
					component.thresholdTargetGameObjectId = ToInt(elements[genericGameplayCursor + 27u]);
				}

				// 物理浮力と RailFollower の追加値は全て末尾へ置き、従来 Scene の列位置を維持する。
				const size_t physicalMovementCursor = genericGameplayCursor + 28u;
				if (elements.size() >= physicalMovementCursor + 16u) {
					component.buoyancyLateralDrag = ToFloat(elements[physicalMovementCursor + 0u]);
					component.buoyancyVerticalDrag = ToFloat(elements[physicalMovementCursor + 1u]);
					component.buoyancySlammingStrength = ToFloat(elements[physicalMovementCursor + 2u]);
					component.railMovementMode = ToInt(elements[physicalMovementCursor + 3u]);
					component.railPositionInfluence = {
						ToFloat(elements[physicalMovementCursor + 4u]),
						ToFloat(elements[physicalMovementCursor + 5u]),
						ToFloat(elements[physicalMovementCursor + 6u])};
					component.railRotationInfluence = {
						ToFloat(elements[physicalMovementCursor + 7u]),
						ToFloat(elements[physicalMovementCursor + 8u]),
						ToFloat(elements[physicalMovementCursor + 9u])};
					component.railPositionSpring = ToFloat(elements[physicalMovementCursor + 10u]);
					component.railPositionDamping = ToFloat(elements[physicalMovementCursor + 11u]);
					component.railMaximumAcceleration = ToFloat(elements[physicalMovementCursor + 12u]);
					component.railRotationSpring = ToFloat(elements[physicalMovementCursor + 13u]);
					component.railRotationDamping = ToFloat(elements[physicalMovementCursor + 14u]);
					component.railMaximumAngularAcceleration = ToFloat(elements[physicalMovementCursor + 15u]);
				}

				// 力学 Component の値も既存 Scene の末尾へ追加し、過去データは CreateComponent の既定値を使う。
				const size_t physicalTheoryCursor = physicalMovementCursor + 16u;
				if (elements.size() >= physicalTheoryCursor + 34u) {
					component.aerodynamicAirDensity = ToFloat(elements[physicalTheoryCursor + 0u]);
					component.aerodynamicDragCoefficient = ToFloat(elements[physicalTheoryCursor + 1u]);
					component.aerodynamicReferenceArea = ToFloat(elements[physicalTheoryCursor + 2u]);
					component.aerodynamicBaseLiftCoefficient = ToFloat(elements[physicalTheoryCursor + 3u]);
					component.aerodynamicLiftSlope = ToFloat(elements[physicalTheoryCursor + 4u]);
					component.aerodynamicLiftArea = ToFloat(elements[physicalTheoryCursor + 5u]);
					component.aerodynamicSideForceCoefficient = ToFloat(elements[physicalTheoryCursor + 6u]);
					component.aerodynamicSideArea = ToFloat(elements[physicalTheoryCursor + 7u]);
					component.aerodynamicZeroLiftAngleDegrees = ToFloat(elements[physicalTheoryCursor + 8u]);
					component.aerodynamicStallAngleDegrees = ToFloat(elements[physicalTheoryCursor + 9u]);
					component.aerodynamicAngularDragCoefficient = ToFloat(elements[physicalTheoryCursor + 10u]);
					component.aerodynamicMagnusCoefficient = ToFloat(elements[physicalTheoryCursor + 11u]);
					component.aerodynamicCenterOfPressure = {
						ToFloat(elements[physicalTheoryCursor + 12u]),
						ToFloat(elements[physicalTheoryCursor + 13u]),
						ToFloat(elements[physicalTheoryCursor + 14u])};
					component.aerodynamicAmbientWindVelocity = {
						ToFloat(elements[physicalTheoryCursor + 15u]),
						ToFloat(elements[physicalTheoryCursor + 16u]),
						ToFloat(elements[physicalTheoryCursor + 17u])};
					component.aerodynamicMaximumForce = ToFloat(elements[physicalTheoryCursor + 18u]);
					component.windZoneMode = ToInt(elements[physicalTheoryCursor + 19u]);
					component.windZoneDirection = {
						ToFloat(elements[physicalTheoryCursor + 20u]),
						ToFloat(elements[physicalTheoryCursor + 21u]),
						ToFloat(elements[physicalTheoryCursor + 22u])};
					component.windZoneSpeed = ToFloat(elements[physicalTheoryCursor + 23u]);
					component.windZoneRadius = ToFloat(elements[physicalTheoryCursor + 24u]);
					component.windZoneTurbulenceStrength = ToFloat(elements[physicalTheoryCursor + 25u]);
					component.windZoneTurbulenceFrequency = ToFloat(elements[physicalTheoryCursor + 26u]);
					component.gravityFieldMode = ToInt(elements[physicalTheoryCursor + 27u]);
					component.gravityFieldGravitationalConstant = ToFloat(elements[physicalTheoryCursor + 28u]);
					component.gravityFieldSourceMass = ToFloat(elements[physicalTheoryCursor + 29u]);
					component.gravityFieldAcceleration = ToFloat(elements[physicalTheoryCursor + 30u]);
					component.gravityFieldMinimumDistance = ToFloat(elements[physicalTheoryCursor + 31u]);
					component.gravityFieldInfluenceRadius = ToFloat(elements[physicalTheoryCursor + 32u]);
					component.gravityFieldMaximumAcceleration = ToFloat(elements[physicalTheoryCursor + 33u]);
				}

				const size_t rotatingFrameCursor = physicalTheoryCursor + 34u;
				if (elements.size() >= rotatingFrameCursor + 11u) {
					component.rotatingFrameAngularVelocity = {
						ToFloat(elements[rotatingFrameCursor + 0u]),
						ToFloat(elements[rotatingFrameCursor + 1u]),
						ToFloat(elements[rotatingFrameCursor + 2u])};
					component.rotatingFrameAngularAcceleration = {
						ToFloat(elements[rotatingFrameCursor + 3u]),
						ToFloat(elements[rotatingFrameCursor + 4u]),
						ToFloat(elements[rotatingFrameCursor + 5u])};
					component.rotatingFrameLinearVelocity = {
						ToFloat(elements[rotatingFrameCursor + 6u]),
						ToFloat(elements[rotatingFrameCursor + 7u]),
						ToFloat(elements[rotatingFrameCursor + 8u])};
					component.rotatingFrameRadius = ToFloat(elements[rotatingFrameCursor + 9u]);
					component.rotatingFrameMaximumAcceleration = ToFloat(elements[rotatingFrameCursor + 10u]);
				}

				// 流体、遠隔ばね、電磁気もさらに末尾へ追加し、旧Sceneの列を一切移動しない。
				const size_t advancedPhysicsCursor = rotatingFrameCursor + 11u;
				if (elements.size() >= advancedPhysicsCursor + 43u) {
					component.fluidVolumeSize = {
						ToFloat(elements[advancedPhysicsCursor + 0u]),
						ToFloat(elements[advancedPhysicsCursor + 1u]),
						ToFloat(elements[advancedPhysicsCursor + 2u])};
					component.fluidDensity = ToFloat(elements[advancedPhysicsCursor + 3u]);
					component.fluidDynamicViscosity = ToFloat(elements[advancedPhysicsCursor + 4u]);
					component.fluidDragCoefficient = ToFloat(elements[advancedPhysicsCursor + 5u]);
					component.fluidFlowVelocity = {
						ToFloat(elements[advancedPhysicsCursor + 6u]),
						ToFloat(elements[advancedPhysicsCursor + 7u]),
						ToFloat(elements[advancedPhysicsCursor + 8u])};
					component.fluidAngularViscosity = ToFloat(elements[advancedPhysicsCursor + 9u]);
					component.fluidMaximumForce = ToFloat(elements[advancedPhysicsCursor + 10u]);
					component.springForceTargetGameObjectId = ToInt(elements[advancedPhysicsCursor + 11u]);
					component.springForceLocalAnchor = {
						ToFloat(elements[advancedPhysicsCursor + 12u]),
						ToFloat(elements[advancedPhysicsCursor + 13u]),
						ToFloat(elements[advancedPhysicsCursor + 14u])};
					component.springForceTargetLocalAnchor = {
						ToFloat(elements[advancedPhysicsCursor + 15u]),
						ToFloat(elements[advancedPhysicsCursor + 16u]),
						ToFloat(elements[advancedPhysicsCursor + 17u])};
					component.springForceWorldAnchor = {
						ToFloat(elements[advancedPhysicsCursor + 18u]),
						ToFloat(elements[advancedPhysicsCursor + 19u]),
						ToFloat(elements[advancedPhysicsCursor + 20u])};
					component.springForceRestLength = ToFloat(elements[advancedPhysicsCursor + 21u]);
					component.springForceStiffness = ToFloat(elements[advancedPhysicsCursor + 22u]);
					component.springForceDamping = ToFloat(elements[advancedPhysicsCursor + 23u]);
					component.springForceMaximumForce = ToFloat(elements[advancedPhysicsCursor + 24u]);
					component.springForceApplyReaction = ToInt(elements[advancedPhysicsCursor + 25u]) != 0;
					component.electromagneticCharge = ToFloat(elements[advancedPhysicsCursor + 26u]);
					component.electromagneticMagneticMoment = {
						ToFloat(elements[advancedPhysicsCursor + 27u]),
						ToFloat(elements[advancedPhysicsCursor + 28u]),
						ToFloat(elements[advancedPhysicsCursor + 29u])};
					component.electromagneticMaximumForce = ToFloat(elements[advancedPhysicsCursor + 30u]);
					component.electromagneticMaximumTorque = ToFloat(elements[advancedPhysicsCursor + 31u]);
					component.electromagneticFieldMode = ToInt(elements[advancedPhysicsCursor + 32u]);
					component.electromagneticElectricField = {
						ToFloat(elements[advancedPhysicsCursor + 33u]),
						ToFloat(elements[advancedPhysicsCursor + 34u]),
						ToFloat(elements[advancedPhysicsCursor + 35u])};
					component.electromagneticMagneticField = {
						ToFloat(elements[advancedPhysicsCursor + 36u]),
						ToFloat(elements[advancedPhysicsCursor + 37u]),
						ToFloat(elements[advancedPhysicsCursor + 38u])};
					component.electromagneticSourceCharge = ToFloat(elements[advancedPhysicsCursor + 39u]);
					component.electromagneticCoulombConstant = ToFloat(elements[advancedPhysicsCursor + 40u]);
					component.electromagneticMinimumDistance = ToFloat(elements[advancedPhysicsCursor + 41u]);
					component.electromagneticInfluenceRadius = ToFloat(elements[advancedPhysicsCursor + 42u]);
				}

				gameObject.components.push_back(component);
			break;
			}
		}
		else if (elements[0] == "PhysicsExtension" && elements.size() >= 38u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.inertiaMultiplier = ToFloat(elements[3]);
					component.applyGyroscopicForce = ToInt(elements[4]) != 0;
					component.ropeTargetGameObjectId = ToInt(elements[5]);
					component.ropeLocalAnchor = {
						ToFloat(elements[6]), ToFloat(elements[7]), ToFloat(elements[8])};
					component.ropeTargetLocalAnchor = {
						ToFloat(elements[9]), ToFloat(elements[10]), ToFloat(elements[11])};
					component.ropeWorldAnchor = {
						ToFloat(elements[12]), ToFloat(elements[13]), ToFloat(elements[14])};
					component.ropeMaximumLength = ToFloat(elements[15]);
					component.ropeStiffness = ToFloat(elements[16]);
					component.ropeDamping = ToFloat(elements[17]);
					component.ropeMaximumTension = ToFloat(elements[18]);
					component.ropeBreakingTension = ToFloat(elements[19]);
					component.ropeApplyReaction = ToInt(elements[20]) != 0;
					component.torsionTargetGameObjectId = ToInt(elements[21]);
					component.torsionRestRotation = {
						ToFloat(elements[22]), ToFloat(elements[23]), ToFloat(elements[24])};
					component.torsionStiffness = ToFloat(elements[25]);
					component.torsionDamping = ToFloat(elements[26]);
					component.torsionMaximumTorque = ToFloat(elements[27]);
					component.torsionApplyReaction = ToInt(elements[28]) != 0;
					component.thrusterDirection = {
						ToFloat(elements[29]), ToFloat(elements[30]), ToFloat(elements[31])};
					component.thrusterLocalApplicationPoint = {
						ToFloat(elements[32]), ToFloat(elements[33]), ToFloat(elements[34])};
					component.thrusterForce = ToFloat(elements[35]);
					component.thrusterThrottle = ToFloat(elements[36]);
					component.thrusterUseLocalDirection = ToInt(elements[37]) != 0;

					if (elements.size() >= 42u) {
						component.centerOfMassOffset = {
							ToFloat(elements[38]), ToFloat(elements[39]), ToFloat(elements[40])};
						component.autoConvexMaximumHulls = (std::clamp)(ToInt(elements[41]), 1, 16);
					}

					if (elements.size() >= 44u) {
						component.automaticMassFromCollider = ToInt(elements[42]) != 0;
						component.bodyDensity = (std::max)(ToFloat(elements[43]), 0.01f);
					}

					component.ropeIsBroken = false;
					break;
				}
				break;
			}
		}
		else if (elements[0] == "BuoyancyPhysicalExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Buoyancy) {
						continue;
					}

					component.buoyancyAutomaticPhysicalProperties = ToInt(elements[2]) != 0;
					component.buoyancyWaterDensity = (std::max)(ToFloat(elements[3]), 0.0f);
					component.buoyancyTargetSubmersionRatio = (std::clamp)(
						ToFloat(elements[4]),
						0.01f,
						0.99f);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "PhysicsExtension2" && elements.size() >= 49u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.pulleyTargetGameObjectId = ToInt(elements[3]);
					component.pulleyOwnerLocalAnchor = {
						ToFloat(elements[4]), ToFloat(elements[5]), ToFloat(elements[6])};
					component.pulleyTargetLocalAnchor = {
						ToFloat(elements[7]), ToFloat(elements[8]), ToFloat(elements[9])};
					component.pulleyOwnerWorldSupport = {
						ToFloat(elements[10]), ToFloat(elements[11]), ToFloat(elements[12])};
					component.pulleyTargetWorldSupport = {
						ToFloat(elements[13]), ToFloat(elements[14]), ToFloat(elements[15])};
					component.pulleyTotalLength = ToFloat(elements[16]);
					component.pulleyRatio = ToFloat(elements[17]);
					component.pulleyStiffness = ToFloat(elements[18]);
					component.pulleyDamping = ToFloat(elements[19]);
					component.pulleyMaximumTension = ToFloat(elements[20]);
					component.pulleyBreakingTension = ToFloat(elements[21]);
					component.pulleyIsBroken = false;
					component.servoTargetGameObjectId = ToInt(elements[22]);
					component.servoTargetPosition = {
						ToFloat(elements[23]), ToFloat(elements[24]), ToFloat(elements[25])};
					component.servoTargetRotation = {
						ToFloat(elements[26]), ToFloat(elements[27]), ToFloat(elements[28])};
					component.servoPositionStiffness = ToFloat(elements[29]);
					component.servoPositionDamping = ToFloat(elements[30]);
					component.servoMaximumForce = ToFloat(elements[31]);
					component.servoRotationStiffness = ToFloat(elements[32]);
					component.servoRotationDamping = ToFloat(elements[33]);
					component.servoMaximumTorque = ToFloat(elements[34]);
					component.servoApplyReaction = ToInt(elements[35]) != 0;
					component.vortexAxis = {
						ToFloat(elements[36]), ToFloat(elements[37]), ToFloat(elements[38])};
					component.vortexRadius = ToFloat(elements[39]);
					component.vortexAngularVelocity = ToFloat(elements[40]);
					component.vortexRadialInflowVelocity = ToFloat(elements[41]);
					component.vortexAxialVelocity = ToFloat(elements[42]);
					component.vortexVelocityCoupling = ToFloat(elements[43]);
					component.vortexMaximumAcceleration = ToFloat(elements[44]);
					component.pressureFieldPressure = ToFloat(elements[45]);
					component.pressureFieldRadius = ToFloat(elements[46]);
					component.pressureFieldFalloffExponent = ToFloat(elements[47]);
					component.pressureFieldMaximumForce = ToFloat(elements[48]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "PhysicsExtension3" && elements.size() >= 26u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.suspensionLocalAnchor = {
						ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.suspensionLocalDirection = {
						ToFloat(elements[6]), ToFloat(elements[7]), ToFloat(elements[8])};
					component.suspensionRestLength = ToFloat(elements[9]);
					component.suspensionMaximumLength = ToFloat(elements[10]);
					component.suspensionWheelRadius = ToFloat(elements[11]);
					component.suspensionStiffness = ToFloat(elements[12]);
					component.suspensionDamping = ToFloat(elements[13]);
					component.suspensionMaximumForce = ToFloat(elements[14]);
					component.suspensionUseHitNormal = ToInt(elements[15]) != 0;
					component.suspensionApplyReaction = ToInt(elements[16]) != 0;
					component.suspensionIsGrounded = false;
					component.suspensionCurrentLength = component.suspensionMaximumLength;
					component.uprightLocalUpAxis = {
						ToFloat(elements[17]), ToFloat(elements[18]), ToFloat(elements[19])};
					component.uprightTargetWorldUp = {
						ToFloat(elements[20]), ToFloat(elements[21]), ToFloat(elements[22])};
					component.uprightStiffness = ToFloat(elements[23]);
					component.uprightDamping = ToFloat(elements[24]);
					component.uprightMaximumTorque = ToFloat(elements[25]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "RailMovementExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType || component.type != EditorComponentType::RailMovement) {
						continue;
					}

					component.railMovementRange = {
						ToFloat(elements[3]), ToFloat(elements[4])};
					component.railStartOffset = {
						ToFloat(elements[5]), ToFloat(elements[6])};
					component.railOffsetMoveSpeed = ToFloat(elements[7]);
					component.railUsePlayerInput = ToInt(elements[8]) != 0;
					component.railInputActionMapName = DecodeSceneToken(elements[9]);
					component.railInputActionName = DecodeSceneToken(elements[10]);

					if (elements.size() >= 14u) {
						component.railLocalForwardAxis = (std::clamp)(ToInt(elements[11]), 0, 3);
						component.railShipHorizontalThrust = ToInt(elements[12]) != 0;
						component.railShipLateralAssist = (std::clamp)(ToFloat(elements[13]), 0.0f, 1.0f);
					}

					if (elements.size() >= 17u) {
						component.railMaximumRollAngle = ToFloat(elements[14]);
						component.railRollRestorationStrength = ToFloat(elements[15]);
						component.railRollDamping = ToFloat(elements[16]);
					}

					if (elements.size() >= 23u) {
						component.railMaximumPitchAngle = ToFloat(elements[17]);
						component.railPitchRestorationStrength = ToFloat(elements[18]);
						component.railPitchDamping = ToFloat(elements[19]);
						component.railMaximumYawAngle = ToFloat(elements[20]);
						component.railYawRestorationStrength = ToFloat(elements[21]);
						component.railYawDamping = ToFloat(elements[22]);
					}

					if (elements.size() >= 29u) {
						component.railYawSafetyAssistEnabled = ToInt(elements[23]) != 0;
						component.railYawSafetyStage1Degrees = ToFloat(elements[24]);
						component.railYawSafetyStage2Degrees = ToFloat(elements[25]);
						component.railYawSafetyStage4Degrees = ToFloat(elements[26]);
						component.railYawSafetyMaxRestorationScale = ToFloat(elements[27]);
						component.railYawSafetyMinSpeedScale = ToFloat(elements[28]);
					}

					if (elements.size() >= 39u) {
						component.railMaxForwardRecoveryError = ToFloat(elements[29]);
						component.railAttitudeSafetyAssistEnabled = ToInt(elements[30]) != 0;
						component.railRollFreeDegrees = ToFloat(elements[31]);
						component.railRollEmergencyDegrees = ToFloat(elements[32]);
						component.railPitchFreeDegrees = ToFloat(elements[33]);
						component.railPitchEmergencyDegrees = ToFloat(elements[34]);
						component.railAttitudeSafetyStrength = ToFloat(elements[35]);
						component.railAttitudeSafetyDamping = ToFloat(elements[36]);
						component.railAttitudeSafetyMaxTorque = ToFloat(elements[37]);
						component.railAttitudeSafetyMinForwardScale = ToFloat(elements[38]);
					}

					if (elements.size() >= 40u) {
						component.railPhysicalCatchupSpeedMultiplier = ToFloat(elements[39]);
					}

					if (elements.size() >= 43u) {
						component.railAttitudeAngleLimitEnabled = ToInt(elements[40]) != 0;
						component.railAttitudeAngleLimitMaxPitchDegrees = ToFloat(elements[41]);
						component.railAttitudeAngleLimitMaxRollDegrees = ToFloat(elements[42]);
					}

					if (elements.size() >= 46u) {
						component.railAttitudeAngleSoftLimitStrength = ToFloat(elements[43]);
						component.railAttitudeAngleSoftLimitDamping = ToFloat(elements[44]);
						component.railAttitudeAngleSoftLimitMaxTorque = ToFloat(elements[45]);
					}

					if (elements.size() >= 47u) {
						component.railAttitudeAngleSoftLimitEnabled = ToInt(elements[46]) != 0;
					}

					if (elements.size() >= 60u) {
						component.railEngineSpeedGain = ToFloat(elements[47]);
						component.railEngineAccelResponse = ToFloat(elements[48]);
						component.railEngineDecelResponse = ToFloat(elements[49]);
						component.railEngineMaxAcceleration = ToFloat(elements[50]);
						component.railSteeringBaseLookAheadDistance = ToFloat(elements[51]);
						component.railSteeringLookAheadTime = ToFloat(elements[52]);
						component.railSteeringYawGain = ToFloat(elements[53]);
						component.railSteeringYawDamping = ToFloat(elements[54]);
						component.railSteeringMaxYawAngularAcceleration = ToFloat(elements[55]);
						component.railLateralAssistDeadZone = ToFloat(elements[56]);
						component.railLateralAssistSoftRadius = ToFloat(elements[57]);
						component.railLateralAssistEmergencyRadius = ToFloat(elements[58]);
						component.railLateralAssistMaxMultiplier = ToFloat(elements[59]);
					}

					if (elements.size() >= 68u) {
						component.railHullLateralGripEnabled = ToInt(elements[60]) != 0;
						component.railHullLateralGripStrength = ToFloat(elements[61]);
						component.railHullLateralGripMaxAcceleration = ToFloat(elements[62]);
						component.railHullLateralGripMinSpeed = ToFloat(elements[63]);
						component.railHullLateralGripFullSpeed = ToFloat(elements[64]);
						component.railHullLateralGripDeadZoneSpeed = ToFloat(elements[65]);
						component.railHullLateralGripSlipStartDegrees = ToFloat(elements[66]);
						component.railHullLateralGripSlipFullDegrees = ToFloat(elements[67]);
					}

					if (elements.size() >= 69u) {
						component.railMode2MaxCombinedAcceleration = ToFloat(elements[68]);
					}

					if (elements.size() >= 71u) {
						component.railMode2MovementStyle = ToInt(elements[69]);
						component.railRideYawSampleDistance = ToFloat(elements[70]);
					}

					break;
				}
				break;
			}
		}
		else if (elements[0] == "CameraFollowExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType ||
						(component.type != EditorComponentType::Camera &&
						 component.type != EditorComponentType::CinemachineCamera)) {
						continue;
					}

					component.cameraFollowPositionSpace = (std::clamp)(ToInt(elements[3]), 0, 1);
					component.cameraFollowRotationMode = (std::clamp)(ToInt(elements[4]), 0, 2);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "PostProcessVisualExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType ||
						(component.type != EditorComponentType::PostProcess &&
						 component.type != EditorComponentType::Volume)) {
						continue;
					}

					component.compositeLocalContrast = (std::clamp)(ToFloat(elements[3]), 0.0f, 1.0f);
					component.compositeOutputDither = (std::clamp)(ToFloat(elements[4]), 0.0f, 2.0f);

					if (elements.size() >= 8u) {
						component.compositeSsgiEnabled = ToInt(elements[5]) != 0;
						component.compositeSsgiIntensity = (std::clamp)(ToFloat(elements[6]), 0.0f, 4.0f);
						component.compositeSsgiRadiusPixels = (std::clamp)(ToFloat(elements[7]), 1.0f, 128.0f);
					}

					if (elements.size() >= 10u) {
						component.compositeColorLutAssetPath = DecodeSceneToken(elements[8]);
						component.compositeColorLutStrength = (std::clamp)(ToFloat(elements[9]), 0.0f, 1.0f);
					}
					break;
				}

				break;
			}
		}
		else if (elements[0] == "MaterialVisualExtension" && elements.size() >= 7u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.materialThickness = (std::max)(ToFloat(elements[3]), 0.001f);
					component.materialWetness = (std::clamp)(ToFloat(elements[4]), 0.0f, 1.0f);
					component.materialWaterlineHeight = ToFloat(elements[5]);
					component.materialWaterlineWidth = (std::max)(ToFloat(elements[6]), 0.001f);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "EnvironmentCloudExtension" && elements.size() >= 14u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Environment) {
						continue;
					}

					component.volumetricCloudEnabled = ToInt(elements[2]) != 0;
					component.volumetricCloudCoverage = (std::clamp)(ToFloat(elements[3]), 0.0f, 1.0f);
					component.volumetricCloudDensity = (std::max)(ToFloat(elements[4]), 0.0f);
					component.volumetricCloudScale = (std::max)(ToFloat(elements[5]), 0.0001f);
					component.volumetricCloudSpeed = ToFloat(elements[6]);
					component.volumetricCloudHeight = ToFloat(elements[7]);
					component.volumetricCloudThickness = (std::max)(ToFloat(elements[8]), 1.0f);
					component.volumetricCloudLightAbsorption = (std::max)(ToFloat(elements[9]), 0.0f);
					component.volumetricCloudSilverLining = (std::max)(ToFloat(elements[10]), 0.0f);
					component.volumetricCloudColor = {
						ToFloat(elements[11]),
						ToFloat(elements[12]),
						ToFloat(elements[13])};
					break;
				}

				break;
			}
		}
		else if (elements[0] == "EnvironmentHeatExtension" && elements.size() >= 7u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Environment) {
						continue;
					}

					component.environmentHeatIntensity = (std::clamp)(ToFloat(elements[2]), 0.0f, 1.0f);
					component.environmentHeatHorizonCenter = (std::clamp)(ToFloat(elements[3]), 0.0f, 1.0f);
					component.environmentHeatHorizonWidth = (std::clamp)(ToFloat(elements[4]), 0.01f, 1.0f);
					component.environmentHeatSunInfluence = (std::clamp)(ToFloat(elements[5]), 0.0f, 1.0f);
					component.environmentHeatDistortionScale = (std::max)(ToFloat(elements[6]), 0.01f);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "SunSystemExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Light) {
						continue;
					}

					component.sunAzimuthDegrees = ToFloat(elements[2]);
					component.sunElevationDegrees = ToFloat(elements[3]);
					component.sunUseAzimuthElevation = ToInt(elements[4]) != 0;
					component.sunTemperatureKelvin = (std::max)(ToFloat(elements[5]), 1000.0f);
					component.sunUseColorTemperature = ToInt(elements[6]) != 0;
					component.sunAutoTemperatureFromElevation = ToInt(elements[7]) != 0;
					break;
				}

				break;
			}
		}
		else if (elements[0] == "OceanSunLightingExtension" && elements.size() >= 13u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Ocean) {
						continue;
					}

					component.oceanSunDiffuseInfluence = (std::max)(ToFloat(elements[2]), 0.0f);
					component.oceanSunSpecularInfluence = (std::max)(ToFloat(elements[3]), 0.0f);
					component.oceanSunGlitterInfluence = (std::max)(ToFloat(elements[4]), 0.0f);
					component.oceanSkyReflectionInfluence = (std::max)(ToFloat(elements[5]), 0.0f);
					component.oceanAmbientInfluence = (std::max)(ToFloat(elements[6]), 0.0f);
					component.oceanDiffuseFloor = (std::clamp)(ToFloat(elements[7]), 0.0f, 1.0f);
					component.oceanGlitterIntensity = (std::max)(ToFloat(elements[8]), 0.0f);
					component.oceanGlitterSharpness = (std::clamp)(ToFloat(elements[9]), 0.0f, 1.0f);
					component.oceanGlitterDensity = (std::max)(ToFloat(elements[10]), 0.01f);
					component.oceanGlitterThreshold = (std::clamp)(ToFloat(elements[11]), 0.0f, 1.0f);
					component.oceanGlitterMaxClamp = (std::max)(ToFloat(elements[12]), 0.1f);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "OceanShapeLightingExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::Ocean) {
						continue;
					}

					component.oceanMacroReflectionInfluence = (std::clamp)(ToFloat(elements[2]), 0.0f, 2.0f);
					component.oceanCurvatureInfluence = (std::clamp)(ToFloat(elements[3]), 0.0f, 4.0f);
					component.oceanTroughOcclusionStrength = (std::clamp)(ToFloat(elements[4]), 0.0f, 0.25f);
					component.oceanCrestHazeStrength = (std::clamp)(ToFloat(elements[5]), 0.0f, 1.0f);
					component.oceanCrestDetailBoost = (std::clamp)(ToFloat(elements[6]), 0.0f, 1.0f);
					component.oceanSlopeRefractionInfluence = (std::clamp)(ToFloat(elements[7]), 0.0f, 2.0f);

					if (elements.size() >= 11u) {
						component.oceanMediumWaveStrength = (std::clamp)(ToFloat(elements[8]), 0.0f, 3.0f);
						component.oceanWaveColorSeparation = (std::clamp)(ToFloat(elements[9]), 0.0f, 1.0f);
						component.oceanShapeRoughnessVariation = (std::clamp)(ToFloat(elements[10]), 0.0f, 0.5f);
					}

					if (elements.size() >= 13u) {
						component.oceanDetailFilterSharpness = (std::clamp)(ToFloat(elements[11]), 0.5f, 2.5f);
						component.oceanGrazingShapeVisibility = (std::clamp)(ToFloat(elements[12]), 0.0f, 1.0f);
					}
					break;
				}

				break;
			}
		}
		else if (elements[0] == "WaveSpawnerExtension" && elements.size() >= 12u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType || component.type != EditorComponentType::WaveSpawner) {
						continue;
					}

					component.waveSpawnSourceMode = ToInt(elements[3]);
					component.wavePoolGameObjectId = ToInt(elements[4]);
					component.waveSpawnPointGameObjectId = ToInt(elements[5]);
					component.waveSpawnCount = ToInt(elements[6]);
					component.waveFormationPattern = ToInt(elements[7]);
					component.waveFormationSpacing = ToFloat(elements[8]);
					component.waveFormationColumns = ToInt(elements[9]);
					component.waveCompletionMode = ToInt(elements[10]);
					component.waveAllDefeatedActionName = DecodeSceneToken(elements[11]);

if (elements.size() >= 13u) {
					component.waveSpawnMaximumPerFrame = (std::clamp)(ToInt(elements[12]), 1, 1024);
				}

				if (elements.size() >= 14u) {
					component.waveSpawnRailStartNormalized = ToFloat(elements[13]);
				}

					break;
				}
				break;
			}
		}
		else if (elements[0] == "MultiTargetLockExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::MultiTargetLock) {
						continue;
					}

					component.multiTargetLockSelectorGameObjectId = ToInt(elements[2]);
					component.multiTargetLockMaximumCount = ToInt(elements[3]);
					component.multiTargetLockSecondsPerTarget = ToFloat(elements[4]);
					component.multiTargetLockLostGraceSeconds = ToFloat(elements[5]);
					component.multiTargetLockAutoAcquire = ToInt(elements[6]) != 0;
					component.multiTargetLockActionTargetGameObjectId = ToInt(elements[7]);
					component.multiTargetLockAddedActionName = DecodeSceneToken(elements[8]);
					component.multiTargetLockCompletedActionName = DecodeSceneToken(elements[9]);
					component.multiTargetLockLostActionName = DecodeSceneToken(elements[10]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "TargetMarkerExtension" && elements.size() >= 15u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));
			const bool hasMultiLockIndex = elements.size() >= 16u;
			const size_t valueOffset = hasMultiLockIndex ? 1u : 0u;

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) continue;
					component.targetMarkerTargetGameObjectId = ToInt(elements[3]);
					component.targetMarkerSelectorGameObjectId = ToInt(elements[4]);
					component.targetMarkerLockGameObjectId = ToInt(elements[5]);
					component.targetMarkerMultiLockIndex = hasMultiLockIndex ? (std::max)(ToInt(elements[6]), 0) : 0;
					component.targetMarkerWorldOffset = {
						ToFloat(elements[6u + valueOffset]),
						ToFloat(elements[7u + valueOffset]),
						ToFloat(elements[8u + valueOffset])};
					component.targetMarkerScreenOffset = {
						ToFloat(elements[9u + valueOffset]),
						ToFloat(elements[10u + valueOffset])};
					component.targetMarkerEdgePadding = ToFloat(elements[11u + valueOffset]);
					component.targetMarkerHideBehindCamera = ToInt(elements[12u + valueOffset]) != 0;
					component.targetMarkerOnlyWhenLocked = ToInt(elements[13u + valueOffset]) != 0;
					component.targetMarkerRotateToDirection = ToInt(elements[14u + valueOffset]) != 0;
					break;
				}
				break;
			}
		}
		else if (elements[0] == "AttributeSetExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t entryCount = (std::max)(ToInt(elements[4]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::AttributeSet) continue;
					component.attributeSetActionTargetGameObjectId = ToInt(elements[2]);
					component.attributeSetChangedActionName = DecodeSceneToken(elements[3]);
					component.attributeSetEntries.clear();

					for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
						const size_t baseIndex = 5u + static_cast<size_t>(entryIndex) * 5u;
						if (baseIndex + 4u >= elements.size()) break;
						component.attributeSetEntries.push_back(EditorNamedAttributeEntry{
							DecodeSceneToken(elements[baseIndex]), ToFloat(elements[baseIndex + 1u]),
							ToFloat(elements[baseIndex + 2u]), ToFloat(elements[baseIndex + 3u]),
							ToFloat(elements[baseIndex + 4u])});
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "GenericCounterExtension" && elements.size() >= 12u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::GenericCounter) continue;
					component.counterName = DecodeSceneToken(elements[2]);
					component.counterInitialValue = ToFloat(elements[3]);
					component.counterMinimumValue = ToFloat(elements[4]);
					component.counterMaximumValue = ToFloat(elements[5]);
					component.counterThresholdValue = ToFloat(elements[6]);
					component.counterCompareMode = ToInt(elements[7]);
					component.counterFireOnce = ToInt(elements[8]) != 0;
					component.counterActionTargetGameObjectId = ToInt(elements[9]);
					component.counterChangedActionName = DecodeSceneToken(elements[10]);
					component.counterThresholdActionName = DecodeSceneToken(elements[11]);
					component.counterCurrentValue = component.counterInitialValue;
					break;
				}
				break;
			}
		}
		else if (elements[0] == "GenericConditionExtension" && elements.size() >= 14u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::GenericCondition) continue;
					component.conditionSourceGameObjectId = ToInt(elements[2]);
					component.conditionSourceType = ToInt(elements[3]);
					component.conditionComponentName = DecodeSceneToken(elements[4]);
					component.conditionPropertyName = DecodeSceneToken(elements[5]);
					component.conditionCompareMode = ToInt(elements[6]);
					component.conditionCompareFloat = ToFloat(elements[7]);
					component.conditionCompareString = DecodeSceneToken(elements[8]);
					component.conditionEvaluateEveryFrame = ToInt(elements[9]) != 0;
					component.conditionFireOnChangeOnly = ToInt(elements[10]) != 0;
					component.conditionActionTargetGameObjectId = ToInt(elements[11]);
					component.conditionTrueActionName = DecodeSceneToken(elements[12]);
					component.conditionFalseActionName = DecodeSceneToken(elements[13]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "GameplayDataExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::GameplayData) continue;
					component.gameplayDataAssetPath = DecodeSceneToken(elements[2]);
					component.gameplayDataEntries.clear();

					for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
						const size_t baseIndex = 4u + static_cast<size_t>(entryIndex) * 3u;
						if (baseIndex + 2u >= elements.size()) break;
						component.gameplayDataEntries.push_back(EditorGameplayDataEntry{
							DecodeSceneToken(elements[baseIndex]), ToInt(elements[baseIndex + 1u]),
							DecodeSceneToken(elements[baseIndex + 2u])});
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "AreaDamageExtension" && elements.size() >= 13u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::AreaDamage) continue;
					component.areaDamageRadius = ToFloat(elements[2]);
					component.areaDamageBaseDamage = ToFloat(elements[3]);
					component.areaDamageMinimumMultiplier = ToFloat(elements[4]);
					component.areaDamageImpulse = ToFloat(elements[5]);
					component.areaDamageFalloffMode = ToInt(elements[6]);
					component.areaDamageLayerMask = ToInt(elements[7]);
					component.areaDamageTag = DecodeSceneToken(elements[8]);
					component.areaDamageIgnoreOwner = ToInt(elements[9]) != 0;
					component.areaDamagePlayOnStart = ToInt(elements[10]) != 0;
					component.areaDamageActionTargetGameObjectId = ToInt(elements[11]);
					component.areaDamageAppliedActionName = DecodeSceneToken(elements[12]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "WeaponDamageTagExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) continue;

					if (component.type == EditorComponentType::HitscanWeapon) {
						component.hitscanDamageTag = DecodeSceneToken(elements[3]);
					}
					else if (component.type == EditorComponentType::ProjectileEmitter) {
						component.projectileDamageTag = DecodeSceneToken(elements[3]);
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "HitZoneExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::HitZone) continue;
					component.hitZoneHealthGameObjectId = ToInt(elements[2]);
					component.hitZoneDamageMultiplier = ToFloat(elements[3]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "DamageTagModifierExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::DamageTagModifier) continue;
					component.damageTagDefaultMultiplier = ToFloat(elements[2]);
					component.damageTagModifierEntries.clear();

					for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
						const size_t baseIndex = 4u + static_cast<size_t>(entryIndex) * 2u;
						if (baseIndex + 1u >= elements.size()) break;
						component.damageTagModifierEntries.push_back(EditorDamageTagModifierEntry{
							DecodeSceneToken(elements[baseIndex]), ToFloat(elements[baseIndex + 1u])});
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "ProjectileDetonatorExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::ProjectileDetonator) continue;
					component.projectileDetonateOnContact = ToInt(elements[2]) != 0;
					component.projectileDetonateOnProximity = ToInt(elements[3]) != 0;
					component.projectileDetonateOnLifetime = ToInt(elements[4]) != 0;
					component.projectileDetonatorTargetGameObjectId = ToInt(elements[5]);
					component.projectileDetonatorProximityRadius = ToFloat(elements[6]);
					component.projectileDetonatorAreaDamageGameObjectId = ToInt(elements[7]);
					component.projectileDetonatorActionTargetGameObjectId = ToInt(elements[8]);
					component.projectileDetonatedActionName = DecodeSceneToken(elements[9]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "ThreatTrackerExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::ThreatTracker) continue;
					component.threatTrackerTargetGameObjectId = ToInt(elements[2]);
					component.threatTrackerMaximumDistance = ToFloat(elements[3]);
					component.threatTrackerMinimumClosingSpeed = ToFloat(elements[4]);
					component.threatTrackerMaximumMissDistance = ToFloat(elements[5]);
					component.threatTrackerMaximumCount = ToInt(elements[6]);
					component.threatTrackerActionTargetGameObjectId = ToInt(elements[7]);
					component.threatTrackerAddedActionName = DecodeSceneToken(elements[8]);
					component.threatTrackerLostActionName = DecodeSceneToken(elements[9]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "RuntimeStateResetExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::RuntimeStateReset) continue;
					component.runtimeResetHealth = ToInt(elements[2]) != 0;
					component.runtimeResetStateMachine = ToInt(elements[3]) != 0;
					component.runtimeResetAttributes = ToInt(elements[4]) != 0;
					component.runtimeResetLocks = ToInt(elements[5]) != 0;
					component.runtimeResetTimers = ToInt(elements[6]) != 0;
					component.runtimeResetDestructibleParts = ToInt(elements[7]) != 0;
					component.runtimeResetCooldowns = ToInt(elements[8]) != 0;
					component.runtimeResetActionTargetGameObjectId = ToInt(elements[9]);
					component.runtimeResetActionName = DecodeSceneToken(elements[10]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "CooldownSetExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t entryCount = (std::max)(ToInt(elements[4]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::CooldownSet) continue;
					component.cooldownSetActionTargetGameObjectId = ToInt(elements[2]);
					component.cooldownSetCompletedActionName = DecodeSceneToken(elements[3]);
					component.cooldownSetEntries.clear();

					for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
						const size_t baseIndex = 5u + static_cast<size_t>(entryIndex) * 3u;
						if (baseIndex + 2u >= elements.size()) break;
						EditorCooldownEntry entry{};
						entry.name = DecodeSceneToken(elements[baseIndex]);
						entry.duration = ToFloat(elements[baseIndex + 1u]);
						entry.startReady = ToInt(elements[baseIndex + 2u]) != 0;
						component.cooldownSetEntries.push_back(entry);
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "WeaponFirePatternExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t spawnPointCount = (std::max)(ToInt(elements[9]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;

				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::WeaponFirePattern) continue;
					component.weaponFirePatternMode = ToInt(elements[2]);
					component.weaponFirePatternCount = ToInt(elements[3]);
					component.weaponFirePatternInterval = ToFloat(elements[4]);
					component.weaponFirePatternSpreadAngle = ToFloat(elements[5]);
					component.weaponFirePatternChargeSeconds = ToFloat(elements[6]);
					component.weaponFirePatternActionTargetGameObjectId = ToInt(elements[7]);
					component.weaponFirePatternCompletedActionName = DecodeSceneToken(elements[8]);
					component.weaponFirePatternSpawnPointGameObjectIds.clear();

					for (int32_t spawnPointIndex = 0; spawnPointIndex < spawnPointCount; ++spawnPointIndex) {
						const size_t valueIndex = 10u + static_cast<size_t>(spawnPointIndex);
						if (valueIndex >= elements.size()) break;
						component.weaponFirePatternSpawnPointGameObjectIds.push_back(ToInt(elements[valueIndex]));
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "TargetAssignmentExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::TargetAssignment) continue;
					component.targetAssignmentMultiTargetLockGameObjectId = ToInt(elements[2]);
					component.targetAssignmentMaximumTargets = ToInt(elements[3]);
					component.targetAssignmentInterval = ToFloat(elements[4]);
					component.targetAssignmentLockedOnly = ToInt(elements[5]) != 0;
					component.targetAssignmentActionTargetGameObjectId = ToInt(elements[6]);
					component.targetAssignmentCompletedActionName = DecodeSceneToken(elements[7]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "WeaponAccuracyExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::WeaponAccuracy) continue;
					component.weaponAccuracyBaseSpread = ToFloat(elements[2]);
					component.weaponAccuracyMaximumSpread = ToFloat(elements[3]);
					component.weaponAccuracySpreadPerShot = ToFloat(elements[4]);
					component.weaponAccuracyRecoveryPerSecond = ToFloat(elements[5]);
					component.weaponAccuracyMovementSpread = ToFloat(elements[6]);
					component.weaponAccuracyDistribution = ToInt(elements[7]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "WeaponRecoilExtension" && elements.size() >= 19u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::WeaponRecoil) continue;
					component.weaponRecoilBodyImpulse = {ToFloat(elements[2]), ToFloat(elements[3]), ToFloat(elements[4])};
					component.weaponRecoilBodyTorque = {ToFloat(elements[5]), ToFloat(elements[6]), ToFloat(elements[7])};
					component.weaponRecoilVisualGameObjectId = ToInt(elements[8]);
					component.weaponRecoilVisualPosition = {ToFloat(elements[9]), ToFloat(elements[10]), ToFloat(elements[11])};
					component.weaponRecoilVisualRotation = {ToFloat(elements[12]), ToFloat(elements[13]), ToFloat(elements[14])};
					component.weaponRecoilRecoveryPerSecond = ToFloat(elements[15]);
					component.weaponRecoilCameraShakeGameObjectId = ToInt(elements[16]);
					component.weaponRecoilActionTargetGameObjectId = ToInt(elements[17]);
					component.weaponRecoilActionName = DecodeSceneToken(elements[18]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "ImpactResponderExtension" && elements.size() >= 3u) {
			const int32_t ownerId = ToInt(elements[1]);
			const int32_t entryCount = (std::max)(ToInt(elements[2]), 0);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::ImpactResponder) continue;
					component.impactResponseEntries.clear();

					for (int32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
						const size_t baseIndex = 3u + static_cast<size_t>(entryIndex) * 8u;
						if (baseIndex + 7u >= elements.size()) break;
						component.impactResponseEntries.push_back(EditorImpactResponseEntry{
							DecodeSceneToken(elements[baseIndex]), DecodeSceneToken(elements[baseIndex + 1u]),
							DecodeSceneToken(elements[baseIndex + 2u]), ToInt(elements[baseIndex + 3u]),
							ToInt(elements[baseIndex + 4u]), ToInt(elements[baseIndex + 5u]),
							ToInt(elements[baseIndex + 6u]), DecodeSceneToken(elements[baseIndex + 7u])});
					}
					break;
				}
				break;
			}
		}
		else if (elements[0] == "SurfaceTypeExtension" && elements.size() >= 3u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type == EditorComponentType::SurfaceType) {
						component.surfaceTypeTag = DecodeSceneToken(elements[2]);
						break;
					}
				}
				break;
			}
		}
		else if (elements[0] == "TimeScaleExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type != EditorComponentType::TimeScale) continue;
					component.timeScaleValue = ToFloat(elements[2]);
					component.timeScaleDuration = ToFloat(elements[3]);
					component.timeScaleBlendSeconds = ToFloat(elements[4]);
					component.timeScalePlayOnStart = ToInt(elements[5]) != 0;
					component.timeScaleActionTargetGameObjectId = ToInt(elements[6]);
					component.timeScaleCompletedActionName = DecodeSceneToken(elements[7]);
					break;
				}
				break;
			}
		}
		else if (elements[0] == "AimAssistExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::AimAssist) {
					component.aimAssistScreenAimGameObjectId = ToInt(elements[2]);
					component.aimAssistTargetSelectorGameObjectId = ToInt(elements[3]);
					component.aimAssistRadius = ToFloat(elements[4]);
					component.aimAssistStrength = ToFloat(elements[5]);
					component.aimAssistFollowSpeed = ToFloat(elements[6]);
					component.aimAssistInputSuppression = ToFloat(elements[7]);
				}
			}
		}
		else if (elements[0] == "InterceptPredictionExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::InterceptPrediction) {
					component.interceptTargetGameObjectId = ToInt(elements[2]);
					component.interceptTargetSelectorGameObjectId = ToInt(elements[3]);
					component.interceptProjectileSpeed = ToFloat(elements[4]);
					component.interceptMaximumTime = ToFloat(elements[5]);
				}
			}
		}
		else if (elements[0] == "DamageDirectionIndicatorExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::DamageDirectionIndicator) {
					component.damageDirectionDuration = ToFloat(elements[2]);
					component.damageDirectionFadeSeconds = ToFloat(elements[3]);
					component.damageDirectionMinimumDamage = ToFloat(elements[4]);
					component.damageDirectionEdgeRadius = ToFloat(elements[5]);
				}
			}
		}
		else if (elements[0] == "ObjectiveTrackerExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ObjectiveTracker) {
					component.objectiveActionTargetGameObjectId = ToInt(elements[2]);
					component.objectiveChangedActionName = DecodeSceneToken(elements[3]);
					component.objectiveEntries.clear();
					const int32_t count = (std::max)(ToInt(elements[4]), 0);
					for (int32_t index = 0; index < count; ++index) {
						const size_t base = 5u + static_cast<size_t>(index) * 5u;
						if (base + 4u >= elements.size()) break;
						component.objectiveEntries.push_back({DecodeSceneToken(elements[base]), DecodeSceneToken(elements[base + 1u]), ToInt(elements[base + 2u]), ToFloat(elements[base + 3u]), ToFloat(elements[base + 4u])});
					}
				}
			}
		}
		else if (elements[0] == "EncounterControllerExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::EncounterController) {
					component.encounterPlayOnStart = ToInt(elements[2]) != 0;
					component.encounterActionTargetGameObjectId = ToInt(elements[3]);
					component.encounterCompletedActionName = DecodeSceneToken(elements[4]);
					component.encounterWaveEntries.clear();
					const int32_t count = (std::max)(ToInt(elements[5]), 0);
					for (int32_t index = 0; index < count; ++index) {
						const size_t base = 6u + static_cast<size_t>(index) * 3u;
						if (base + 2u >= elements.size()) break;
						component.encounterWaveEntries.push_back({ToInt(elements[base]), ToFloat(elements[base + 1u]), ToInt(elements[base + 2u]) != 0});
					}
				}
			}
		}
		else if (elements[0] == "SpawnPointSetExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SpawnPointSet) {
					component.spawnPointSetMode = ToInt(elements[2]);
					component.spawnPointVolumeSize = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.spawnPointAvoidImmediateRepeat = ToInt(elements[6]) != 0;
					component.spawnPointSetEntries.clear();
					const int32_t count = (std::max)(ToInt(elements[7]), 0);
					for (int32_t index = 0; index < count; ++index) {
						const size_t base = 8u + static_cast<size_t>(index) * 2u;
						if (base + 1u >= elements.size()) break;
						component.spawnPointSetEntries.push_back({ToInt(elements[base]), ToFloat(elements[base + 1u])});
					}
				}
			}
		}
		else if (elements[0] == "DifficultyParameterSetExtension" && elements.size() >= 7u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::DifficultyParameterSet) {
					component.difficultySelectedIndex = ToInt(elements[2]); component.difficultyApplyOnStart = ToInt(elements[3]) != 0;
					component.difficultyActionTargetGameObjectId = ToInt(elements[4]); component.difficultyAppliedActionName = DecodeSceneToken(elements[5]);
					size_t cursor = 6u; const int32_t nameCount = (std::max)(ToInt(elements[cursor++]), 0); component.difficultyNames.clear();
					for (int32_t index = 0; index < nameCount && cursor < elements.size(); ++index) component.difficultyNames.push_back(DecodeSceneToken(elements[cursor++]));
					component.difficultyOverrides.clear(); if (cursor >= elements.size()) break; const int32_t overrideCount = (std::max)(ToInt(elements[cursor++]), 0);
					for (int32_t index = 0; index < overrideCount && cursor + 7u < elements.size(); ++index) {
						EditorDifficultyOverrideEntry entry{}; entry.difficultyIndex = ToInt(elements[cursor++]); entry.targetGameObjectId = ToInt(elements[cursor++]);
						entry.componentName = DecodeSceneToken(elements[cursor++]); entry.propertyName = DecodeSceneToken(elements[cursor++]); entry.valueType = ToInt(elements[cursor++]);
						entry.floatValue = ToFloat(elements[cursor++]); entry.intValue = ToInt(elements[cursor++]); entry.boolValue = ToInt(elements[cursor++]) != 0; component.difficultyOverrides.push_back(entry);
					}
				}
			}
		}
		else if (elements[0] == "CameraFeedbackMixerExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::CameraFeedbackMixer) {
					component.cameraFeedbackMaximumPosition = {ToFloat(elements[2]), ToFloat(elements[3]), ToFloat(elements[4])};
					component.cameraFeedbackMaximumRotation = {ToFloat(elements[5]), ToFloat(elements[6]), ToFloat(elements[7])};
					component.cameraFeedbackMaximumConcurrent = ToInt(elements[8]); component.cameraFeedbackMixMode = ToInt(elements[9]); component.cameraFeedbackGlobalStrength = ToFloat(elements[10]);
				}
			}
		}
		else if (elements[0] == "BallisticPredictionExtension" && elements.size() >= 15u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::BallisticPrediction) {
					component.ballisticTargetGameObjectId = ToInt(elements[2]);
					component.ballisticTargetSelectorGameObjectId = ToInt(elements[3]);
					component.ballisticInitialSpeed = ToFloat(elements[4]);
					component.ballisticGravity = {ToFloat(elements[5]), ToFloat(elements[6]), ToFloat(elements[7])};
					component.ballisticDrag = ToFloat(elements[8]);
					component.ballisticTargetAcceleration = {ToFloat(elements[9]), ToFloat(elements[10]), ToFloat(elements[11])};
					component.ballisticMaximumTime = ToFloat(elements[12]);
					component.ballisticSimulationStep = ToFloat(elements[13]);
					component.ballisticMaximumPoints = ToInt(elements[14]);
				}
			}
		}
		else if (elements[0] == "BallisticSourceVelocityExtension" && elements.size() >= 7u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::BallisticPrediction) {
					component.ballisticInheritSourceVelocity = ToInt(elements[2]) != 0;
					component.ballisticSourceVelocityGameObjectId = ToInt(elements[3]);
					component.ballisticUseParentRigidBody = ToInt(elements[4]) != 0;
					component.ballisticLinearVelocityInheritance = ToFloat(elements[5]);
					component.ballisticAngularVelocityInheritance = ToFloat(elements[6]);
				}
			}
		}
		else if (elements[0] == "DamageEventBufferExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::DamageEventBuffer) {
					component.damageEventMaximumEntries = ToInt(elements[2]);
					component.damageEventLifetime = ToFloat(elements[3]);
					component.damageEventMinimumDamage = ToFloat(elements[4]);
					component.damageEventMergeSameSource = ToInt(elements[5]) != 0;
				}
			}
		}
		else if (elements[0] == "GamePauseExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::GamePause) {
					component.gamePausePauseGameTime = ToInt(elements[2]) != 0;
					component.gamePausePausePhysics = ToInt(elements[3]) != 0;
					component.gamePausePauseAudio = ToInt(elements[4]) != 0;
					component.gamePauseGameplayInputMap = DecodeSceneToken(elements[5]);
					component.gamePauseUiInputMap = DecodeSceneToken(elements[6]);
					component.gamePauseActionTargetGameObjectId = ToInt(elements[7]);
					component.gamePausePausedActionName = DecodeSceneToken(elements[8]);
					component.gamePauseResumedActionName = DecodeSceneToken(elements[9]);
				}
			}
		}
		else if (elements[0] == "SurfaceWakeEmitterExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SurfaceWakeEmitter) {
					component.surfaceWakeOceanGameObjectId = ToInt(elements[2]);
					component.surfaceWakeLeftEffectGameObjectId = ToInt(elements[3]);
					component.surfaceWakeRightEffectGameObjectId = ToInt(elements[4]);
					component.surfaceWakeBowEffectGameObjectId = ToInt(elements[5]);
					component.surfaceWakeMinimumSpeed = ToFloat(elements[6]);
					component.surfaceWakeMaximumSpeed = ToFloat(elements[7]);
					component.surfaceWakeWidth = ToFloat(elements[8]);
					component.surfaceWakeLifetime = ToFloat(elements[9]);
					component.surfaceWakeMaximumEmissionRate = ToFloat(elements[10]);
				}
			}
		}
		else if (elements[0] == "TrajectoryRendererExtension" && elements.size() >= 12u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::TrajectoryRenderer) {
					component.trajectoryPredictionGameObjectId = ToInt(elements[2]);
					component.trajectoryColor = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.trajectoryAlpha = ToFloat(elements[6]);
					component.trajectoryThickness = ToFloat(elements[7]);
					component.trajectoryMaximumPoints = ToInt(elements[8]);
					component.trajectoryShowInSceneView = ToInt(elements[9]) != 0;
					component.trajectoryShowInGameView = ToInt(elements[10]) != 0;
					component.trajectoryShowImpactPoint = ToInt(elements[11]) != 0;
				}
			}
		}
		else if (elements[0] == "TargetSelectorOceanExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::TargetSelector) {
					component.targetSelectorOcclusionMode = ToInt(elements[2]);
					component.targetSelectorOceanClearance = ToFloat(elements[3]);
				}
			}
		}
		else if (elements[0] == "WeaponOceanCollisionExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == componentType) {
					if (component.type == EditorComponentType::HitscanWeapon) {
						component.hitscanOceanCollision = ToInt(elements[3]) != 0;
					}
					else if (component.type == EditorComponentType::ProjectileEmitter) {
						component.projectileOceanCollision = ToInt(elements[3]) != 0;
					}
				}
			}
		}
		else if (elements[0] == "ProjectileVelocityAimExtension" && elements.size() >= 9u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ProjectileEmitter) {
					component.projectileAimMode = ToInt(elements[2]);
					component.projectileBallisticPredictionGameObjectId = ToInt(elements[3]);
					component.projectileInheritSourceVelocity = ToInt(elements[4]) != 0;
					component.projectileSourceVelocityGameObjectId = ToInt(elements[5]);
					component.projectileUseParentRigidBody = ToInt(elements[6]) != 0;
					component.projectileLinearVelocityInheritance = ToFloat(elements[7]);
					component.projectileAngularVelocityInheritance = ToFloat(elements[8]);
					if (elements.size() >= 11u) {
						component.projectileVariableSpeedMinimumFlightTime = ToFloat(elements[9]);
						component.projectileVariableSpeedMaximumFlightTime = ToFloat(elements[10]);
					}
				}
			}
		}
		else if (elements[0] == "ProjectileVariableSpeedExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ProjectileEmitter) {
					component.projectileVariableSpeedTimeMode = ToInt(elements[2]);
					component.projectileVariableSpeedFixedFlightTime = ToFloat(elements[3]);
					component.projectileVariableSpeedTrajectoryMode = ToInt(elements[4]);
					component.projectileVariableSpeedDepressionAngleDegrees = ToFloat(elements[5]);
					if (elements.size() >= 7u) {
						component.projectileVariableSpeedArcHeight = ToFloat(elements[6]);
					}
					if (elements.size() >= 8u) {
						component.projectileVariableSpeedDistanceFactor = ToFloat(elements[7]);
					}
				}
			}
		}
		else if (elements[0] == "ProjectileTracerExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ProjectileEmitter) {
					component.projectileTracerStretchEnabled = ToInt(elements[2]) != 0;
					component.projectileTracerLengthScale = ToFloat(elements[3]);
					component.projectileTracerMinimumLength = ToFloat(elements[4]);
					if (elements.size() >= 6u) {
						component.projectileTracerThickness = ToFloat(elements[5]);
					}
					if (elements.size() >= 7u) {
						component.projectileHitscanResolution = ToInt(elements[6]) != 0;
					}
				}
			}
		}
		else if (elements[0] == "ProjectileSpawnClearanceExtension" && elements.size() >= 3u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ProjectileEmitter) {
					component.projectileSpawnClearance = ToFloat(elements[2]);
				}
			}
		}
		else if (elements[0] == "WaterSurfaceStateExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::WaterSurfaceState) {
					component.waterSurfaceOceanGameObjectId = ToInt(elements[2]);
					component.waterSurfaceLocalOffset = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.waterSurfaceClearance = ToFloat(elements[6]);
					component.waterSurfaceActionTargetGameObjectId = ToInt(elements[7]);
					component.waterSurfaceEnteredActionName = DecodeSceneToken(elements[8]);
					component.waterSurfaceExitedActionName = DecodeSceneToken(elements[9]);
				}
			}
		}
		else if (elements[0] == "OceanProbeSetExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::OceanProbeSet) {
					component.oceanProbeOceanGameObjectId = ToInt(elements[2]);
					component.oceanProbeLocalOriginOffset = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.oceanProbeLocalDirection = {ToFloat(elements[6]), ToFloat(elements[7]), ToFloat(elements[8])};
					component.oceanProbeEntries.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[9]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount && 10u + static_cast<size_t>(entryIndex) < elements.size(); entryIndex++) {
						EditorOceanProbeEntry probeEntry{};
						probeEntry.distance = ToFloat(elements[10u + static_cast<size_t>(entryIndex)]);
						component.oceanProbeEntries.push_back(probeEntry);
					}
				}
			}
		}
		else if (elements[0] == "AttackCollisionFilterExtension" && elements.size() >= 9u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::AttackCollisionFilter) {
					component.attackFilterInstigatorGameObjectId = ToInt(elements[2]);
					component.attackFilterIgnoreInstigator = ToInt(elements[3]) != 0;
					component.attackFilterIgnoreInstigatorHierarchy = ToInt(elements[4]) != 0;
					component.attackFilterTeamRule = ToInt(elements[5]);
					component.attackFilterIgnoreNeutral = ToInt(elements[6]) != 0;
					component.attackFilterArmingDistance = ToFloat(elements[7]);
					component.attackFilterIgnoredGameObjectIds.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[8]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount && 9u + static_cast<size_t>(entryIndex) < elements.size(); entryIndex++) {
						component.attackFilterIgnoredGameObjectIds.push_back(
							ToInt(elements[9u + static_cast<size_t>(entryIndex)]));
					}
				}
			}
		}
		else if (elements[0] == "TurretAimExtension" && elements.size() >= 14u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::TurretAim) {
					component.turretTargetGameObjectId = ToInt(elements[2]);
					component.turretTargetSelectorGameObjectId = ToInt(elements[3]);
					component.turretYawPivotGameObjectId = ToInt(elements[4]);
					component.turretPitchPivotGameObjectId = ToInt(elements[5]);
					component.turretYawMinimumDegrees = ToFloat(elements[6]);
					component.turretYawMaximumDegrees = ToFloat(elements[7]);
					component.turretPitchMinimumDegrees = ToFloat(elements[8]);
					component.turretPitchMaximumDegrees = ToFloat(elements[9]);
					component.turretYawSpeedDegrees = ToFloat(elements[10]);
					component.turretPitchSpeedDegrees = ToFloat(elements[11]);
					component.turretAimToleranceDegrees = ToFloat(elements[12]);
					component.turretPredictionSeconds = ToFloat(elements[13]);
				}
			}
		}
		else if (elements[0] == "WeaponGroupExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::WeaponGroup) {
					component.weaponGroupMode = ToInt(elements[2]);
					component.weaponGroupInterval = ToFloat(elements[3]);
					component.weaponGroupRequireAllReady = ToInt(elements[4]) != 0;
					component.weaponGroupActionTargetGameObjectId = ToInt(elements[5]);
					component.weaponGroupCompletedActionName = DecodeSceneToken(elements[6]);
					component.weaponGroupEntries.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[7]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 8u + static_cast<size_t>(entryIndex) * 2u;

						if (elementIndex + 1u >= elements.size()) {
							break;
						}

						component.weaponGroupEntries.push_back({
							ToInt(elements[elementIndex]),
							ToInt(elements[elementIndex + 1u]) != 0});
					}
				}
			}
		}
		else if (elements[0] == "ProjectileImpactPhysicsExtension" && elements.size() >= 10u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::ProjectileImpactPhysics) {
					component.projectileImpactPenetrationEnergy = ToFloat(elements[2]);
					component.projectileImpactPenetrationLoss = ToFloat(elements[3]);
					component.projectileImpactMaximumPenetrations = ToInt(elements[4]);
					component.projectileImpactRicochetAngleDegrees = ToFloat(elements[5]);
					component.projectileImpactEnergyRetention = ToFloat(elements[6]);
					component.projectileImpactDamageRetention = ToFloat(elements[7]);
					component.projectileImpactMaximumRicochets = ToInt(elements[8]);
					component.projectileImpactSurfaceModifiers.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[9]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 10u + static_cast<size_t>(entryIndex) * 4u;

						if (elementIndex + 3u >= elements.size()) {
							break;
						}

						component.projectileImpactSurfaceModifiers.push_back({
							DecodeSceneToken(elements[elementIndex]),
							ToFloat(elements[elementIndex + 1u]),
							ToFloat(elements[elementIndex + 2u]),
							ToFloat(elements[elementIndex + 3u])});
					}
				}
			}
		}
		else if (elements[0] == "CameraHorizonStabilizerExtension" && elements.size() >= 18u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::CameraHorizonStabilizer) {
					component.horizonSourceGameObjectId = ToInt(elements[2]);
					component.horizonLocalPositionOffset = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.horizonRotationOffsetDegrees = {ToFloat(elements[6]), ToFloat(elements[7]), ToFloat(elements[8])};
					component.horizonFollowPosition = ToInt(elements[9]) != 0;
					component.horizonPitchInheritance = ToFloat(elements[10]);
					component.horizonYawInheritance = ToFloat(elements[11]);
					component.horizonRollInheritance = ToFloat(elements[12]);
					component.horizonWorldUp = {ToFloat(elements[13]), ToFloat(elements[14]), ToFloat(elements[15])};
					component.horizonDamping = ToFloat(elements[16]);
					component.horizonMaximumRollDegrees = ToFloat(elements[17]);
				}
			}
		}
		else if (elements[0] == "AreaDamageFilterExtension" && elements.size() >= 9u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::AreaDamage) {
					component.areaDamageOcclusionMode = ToInt(elements[2]);
					component.areaDamageOcclusionLayerMask = ToInt(elements[3]);
					component.areaDamageBlockedMultiplier = ToFloat(elements[4]);
					component.areaDamageOcclusionSamplePoints = ToInt(elements[5]);
					component.areaDamageTeamRule = ToInt(elements[6]);
					component.areaDamageIgnoreNeutral = ToInt(elements[7]) != 0;
					component.areaDamageTeamSourceGameObjectId = ToInt(elements[8]);
				}
			}
		}
		else if (elements[0] == "FireLineCheckExtension" && elements.size() >= 9u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::FireLineCheck) {
					component.fireLineMuzzleGameObjectId = ToInt(elements[2]);
					component.fireLineDirectionGameObjectId = ToInt(elements[3]);
					component.fireLineAllowedTargetGameObjectId = ToInt(elements[4]);
					component.fireLineDistance = ToFloat(elements[5]);
					component.fireLineRadius = ToFloat(elements[6]);
					component.fireLineLayerMask = ToInt(elements[7]);
					component.fireLineIgnoredGameObjectIds.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[8]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount &&
						9u + static_cast<size_t>(entryIndex) < elements.size(); entryIndex++) {
						component.fireLineIgnoredGameObjectIds.push_back(
							ToInt(elements[9u + static_cast<size_t>(entryIndex)]));
					}
				}
			}
		}
		else if (elements[0] == "StatusEffectSetExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::StatusEffectSet) {
					component.statusEffectActionTargetGameObjectId = ToInt(elements[2]);
					component.statusEffectDefinitions.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 4u + static_cast<size_t>(entryIndex) * 8u;

						if (elementIndex + 7u >= elements.size()) {
							break;
						}

						component.statusEffectDefinitions.push_back({
							DecodeSceneToken(elements[elementIndex]),
							ToFloat(elements[elementIndex + 1u]),
							ToInt(elements[elementIndex + 2u]),
							ToInt(elements[elementIndex + 3u]),
							ToFloat(elements[elementIndex + 4u]),
							DecodeSceneToken(elements[elementIndex + 5u]),
							DecodeSceneToken(elements[elementIndex + 6u]),
							DecodeSceneToken(elements[elementIndex + 7u])});
					}
				}
			}
		}
		else if (elements[0] == "RailSpeedProfileExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::RailSpeedProfile) {
					component.railSpeedProfileEnabled = ToInt(elements[2]) != 0;
					component.railSpeedKeys.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 4u + static_cast<size_t>(entryIndex) * 2u;

						if (elementIndex + 1u >= elements.size()) {
							break;
						}

						component.railSpeedKeys.push_back({
							ToFloat(elements[elementIndex]),
							ToFloat(elements[elementIndex + 1u])});
					}
				}
			}
		}
		else if (elements[0] == "RailZoneExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::RailZone) {
					component.railZoneActionTargetGameObjectId = ToInt(elements[2]);
					component.railZoneEntries.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 4u + static_cast<size_t>(entryIndex) * 9u;

						if (elementIndex + 8u >= elements.size()) {
							break;
						}

						component.railZoneEntries.push_back({
							DecodeSceneToken(elements[elementIndex]),
							ToFloat(elements[elementIndex + 1u]),
							ToFloat(elements[elementIndex + 2u]),
							ToFloat(elements[elementIndex + 3u]),
							{ToFloat(elements[elementIndex + 4u]), ToFloat(elements[elementIndex + 5u])},
							ToInt(elements[elementIndex + 6u]) != 0,
							DecodeSceneToken(elements[elementIndex + 7u]),
							DecodeSceneToken(elements[elementIndex + 8u])});
					}
				}
			}
		}
		else if (elements[0] == "CameraFollowComposerExtension" && elements.size() >= 17u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::CameraFollowComposer) {
					component.cameraComposerTargetGameObjectId = ToInt(elements[2]);
					component.cameraComposerFollowOffset = {ToFloat(elements[3]), ToFloat(elements[4]), ToFloat(elements[5])};
					component.cameraComposerLookAtOffset = {ToFloat(elements[6]), ToFloat(elements[7]), ToFloat(elements[8])};
					component.cameraComposerPositionDamping = ToFloat(elements[9]);
					component.cameraComposerRotationDamping = ToFloat(elements[10]);
					component.cameraComposerLookAheadSeconds = ToFloat(elements[11]);
					component.cameraComposerDeadZone = {ToFloat(elements[12]), ToFloat(elements[13])};
					component.cameraComposerMaximumDistance = ToFloat(elements[14]);
					component.cameraComposerInheritTargetYaw = ToInt(elements[15]) != 0;
					component.cameraComposerStabilizePitchRoll = ToInt(elements[16]) != 0;
				}
			}
		}
		else if (elements[0] == "SpeedFeedbackExtension" && elements.size() >= 12u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SpeedFeedback) {
					component.speedFeedbackSourceGameObjectId = ToInt(elements[2]);
					component.speedFeedbackCameraGameObjectId = ToInt(elements[3]);
					component.speedFeedbackMinimumSpeed = ToFloat(elements[4]);
					component.speedFeedbackMaximumSpeed = ToFloat(elements[5]);
					component.speedFeedbackMinimumFovDegrees = ToFloat(elements[6]);
					component.speedFeedbackMaximumFovDegrees = ToFloat(elements[7]);
					component.speedFeedbackMinimumMotionBlur = ToFloat(elements[8]);
					component.speedFeedbackMaximumMotionBlur = ToFloat(elements[9]);
					component.speedFeedbackCameraStrength = ToFloat(elements[10]);
					component.speedFeedbackResponseSpeed = ToFloat(elements[11]);
				}
			}
		}
		else if (elements[0] == "SpawnedObjectSetupExtension" && elements.size() >= 11u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SpawnedObjectSetup) {
					component.spawnedSetupRailPathGameObjectId = ToInt(elements[2]);
					component.spawnedSetupRailStartNormalized = ToFloat(elements[3]);
					component.spawnedSetupRailStartStep = ToFloat(elements[4]);
					component.spawnedSetupRailSpeedMultiplier = ToFloat(elements[5]);
					component.spawnedSetupOverrideTeam = ToInt(elements[6]) != 0;
					component.spawnedSetupTeamId = ToInt(elements[7]);
					component.spawnedSetupResetRuntimeState = ToInt(elements[8]) != 0;
					component.spawnedSetupActionTargetGameObjectId = ToInt(elements[9]);
					component.spawnedSetupAppliedActionName = DecodeSceneToken(elements[10]);
				}
			}
		}
		else if (elements[0] == "WaveMotionProfileExtension" && elements.size() >= 8u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::WaveMotionProfile) {
					component.waveMotionMode = ToInt(elements[2]);
					component.waveMotionAmplitude = {ToFloat(elements[3]), ToFloat(elements[4])};
					component.waveMotionFrequency = ToFloat(elements[5]);
					component.waveMotionPhaseStep = ToFloat(elements[6]);
					component.waveMotionBlendInSeconds = ToFloat(elements[7]);
				}
			}
		}
		else if (elements[0] == "WaveSustainExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::WaveSpawner) {
					component.waveTargetAliveCount = ToInt(elements[2]);
					component.waveSpawnPointSetGameObjectId = ToInt(elements[3]);
				}
			}
		}
		else if (elements[0] == "TargetSteeringMoveExtension" && elements.size() >= 19u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::TargetSteering) {
					component.targetSteeringMoveMode = ToInt(elements[2]);
					component.targetSteeringSideOffset = ToFloat(elements[3]);
					component.targetSteeringForwardOffset = ToFloat(elements[4]);
					component.targetSteeringVerticalOffset = ToFloat(elements[5]);
					component.targetSteeringTargetDistance = ToFloat(elements[6]);
					component.targetSteeringDistanceMargin = ToFloat(elements[7]);
					component.targetSteeringDuration = ToFloat(elements[8]);
					component.targetSteeringNextMoveMode = ToInt(elements[9]);
					component.targetSteeringStartOffset = {
						ToFloat(elements[10]), ToFloat(elements[11]), ToFloat(elements[12])};
					component.targetSteeringEndOffset = {
						ToFloat(elements[13]), ToFloat(elements[14]), ToFloat(elements[15])};
					component.targetSteeringPositionLerpSpeed = ToFloat(elements[16]);
					component.targetSteeringActionTargetGameObjectId = ToInt(elements[17]);
					component.targetSteeringCompletedActionName = DecodeSceneToken(elements[18]);
				}
			}
		}
		else if (elements[0] == "DistanceActivationExtension" && elements.size() >= 6u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::DistanceActivation) {
					component.distanceActivationReferenceGameObjectId = ToInt(elements[2]);
					component.distanceActivationEnterDistance = ToFloat(elements[3]);
					component.distanceActivationExitDistance = ToFloat(elements[4]);
					component.distanceActivationAffectHierarchy = ToInt(elements[5]) != 0;
				}
			}
		}
		else if (elements[0] == "SimulationLodExtension" && elements.size() >= 12u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SimulationLOD) {
					component.simulationLodReferenceGameObjectId = ToInt(elements[2]);
					component.simulationLodMediumDistance = ToFloat(elements[3]);
					component.simulationLodFarDistance = ToFloat(elements[4]);
					component.simulationLodCulledDistance = ToFloat(elements[5]);
					const bool hasScriptIntervals = elements.size() >= 14u;
					const size_t optionOffset = hasScriptIntervals ? 2u : 0u;

					if (hasScriptIntervals) {
						component.simulationLodMediumScriptInterval = (std::max)(ToFloat(elements[6]), 0.0f);
						component.simulationLodFarScriptInterval = (std::max)(ToFloat(elements[7]), 0.0f);
					}

					component.simulationLodDisablePhysicsAtFar = ToInt(elements[6u + optionOffset]) != 0;
					component.simulationLodDisableScriptsAtFar = ToInt(elements[7u + optionOffset]) != 0;
					component.simulationLodDisableAiAtFar = ToInt(elements[8u + optionOffset]) != 0;
					component.simulationLodDisableAnimationAtFar = ToInt(elements[9u + optionOffset]) != 0;
					component.simulationLodDisableEffectsAtFar = ToInt(elements[10u + optionOffset]) != 0;
					component.simulationLodAffectHierarchy = ToInt(elements[11u + optionOffset]) != 0;
				}
			}
		}
		else if (elements[0] == "RailEventMarkerExtension" && elements.size() >= 4u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::RailEventMarker) {
					component.railEventMarkerActionTargetGameObjectId = ToInt(elements[2]);
					component.railEventMarkerEntries.clear();
					const int32_t entryCount = (std::max)(ToInt(elements[3]), 0);

					for (int32_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
						const size_t elementIndex = 4u + static_cast<size_t>(entryIndex) * 5u;

						if (elementIndex + 4u >= elements.size()) {
							break;
						}

						component.railEventMarkerEntries.push_back({
							DecodeSceneToken(elements[elementIndex]),
							ToFloat(elements[elementIndex + 1u]),
							ToInt(elements[elementIndex + 2u]),
							ToInt(elements[elementIndex + 3u]) != 0,
							DecodeSceneToken(elements[elementIndex + 4u]),
							false});
					}
				}
			}
		}
		else if (elements[0] == "SceneStreamingExtension" && elements.size() >= 7u) {
			const int32_t ownerId = ToInt(elements[1]);

			for (EditorGameObject& object : loadedGameObjects) if (object.id == ownerId) {
				for (EditorComponent& component : object.components) if (component.type == EditorComponentType::SceneStreaming) {
					component.sceneStreamingScenePath = DecodeSceneToken(elements[2]);
					component.sceneStreamingReferenceGameObjectId = ToInt(elements[3]);
					component.sceneStreamingLoadDistance = (std::max)(ToFloat(elements[4]), 0.0f);
					component.sceneStreamingUnloadDistance = (std::max)(
						ToFloat(elements[5]),
						component.sceneStreamingLoadDistance);
					component.sceneStreamingUnloadWhenFar = ToInt(elements[6]) != 0;
				}
			}
		}
		else if (elements[0] == "ParticleBillboardExtension" && elements.size() >= 5u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& object : loadedGameObjects) {
				if (object.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : object.components) {
					if (component.type != componentType ||
						(component.type != EditorComponentType::ParticleSystem &&
						 component.type != EditorComponentType::VisualEffect)) {
						continue;
					}

					component.particleBillboardMode = (std::clamp)(ToInt(elements[3]), 0, 3);
					component.particleBillboardStretch = (std::max)(ToFloat(elements[4]), 0.01f);
				}
			}
		}
		else if (elements[0] == "GameplayFoundation" && elements.size() >= 78u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.cameraPriority = ToInt(elements[3]);
					component.screenAimInputGameObjectId = ToInt(elements[4]);
					component.screenAimReticleGameObjectId = ToInt(elements[5]);
					component.screenAimInputMode = ToInt(elements[6]);
					component.screenAimActionMapName = DecodeSceneToken(elements[7]);
					component.screenAimActionName = DecodeSceneToken(elements[8]);
					component.screenAimNormalizedPosition = {ToFloat(elements[9]), ToFloat(elements[10])};
					component.screenAimSpeed = ToFloat(elements[11]);
					component.screenAimInvertY = ToInt(elements[12]) != 0;
					component.screenAimClamp = ToInt(elements[13]) != 0;
					component.hitscanAimGameObjectId = ToInt(elements[14]);
					component.hitscanInputGameObjectId = ToInt(elements[15]);
					component.hitscanActionMapName = DecodeSceneToken(elements[16]);
					component.hitscanFireActionName = DecodeSceneToken(elements[17]);
					component.hitscanRange = ToFloat(elements[18]);
					component.hitscanDamage = ToFloat(elements[19]);
					component.hitscanInterval = ToFloat(elements[20]);
					component.hitscanAutomatic = ToInt(elements[21]) != 0;
					component.hitscanActionTargetGameObjectId = ToInt(elements[22]);
					component.hitscanFiredActionName = DecodeSceneToken(elements[23]);
					component.hitscanHitActionName = DecodeSceneToken(elements[24]);
					component.hitscanMissActionName = DecodeSceneToken(elements[25]);
					component.projectileAimGameObjectId = ToInt(elements[26]);
					component.projectileInputGameObjectId = ToInt(elements[27]);
					component.projectilePoolGameObjectId = ToInt(elements[28]);
					component.projectileSpawnPointGameObjectId = ToInt(elements[29]);
					component.projectileActionMapName = DecodeSceneToken(elements[30]);
					component.projectileFireActionName = DecodeSceneToken(elements[31]);
					component.projectileSpeed = ToFloat(elements[32]);
					component.projectileDamage = ToFloat(elements[33]);
					component.projectileRadius = ToFloat(elements[34]);
					component.projectileLifetime = ToFloat(elements[35]);
					component.projectileInterval = ToFloat(elements[36]);
					component.projectileAutomatic = ToInt(elements[37]) != 0;
					component.projectileActionTargetGameObjectId = ToInt(elements[38]);
					component.projectileFiredActionName = DecodeSceneToken(elements[39]);
					component.projectileHitActionName = DecodeSceneToken(elements[40]);
					component.damageMultiplier = ToFloat(elements[41]);
					component.damageInvulnerabilitySeconds = ToFloat(elements[42]);
					component.damageDeactivateOnDeath = ToInt(elements[43]) != 0;
					component.damageActionTargetGameObjectId = ToInt(elements[44]);
					component.damagedActionName = DecodeSceneToken(elements[45]);
					component.deathActionName = DecodeSceneToken(elements[46]);
					component.objectPoolTemplateGameObjectId = ToInt(elements[47]);
					component.objectPoolInitialSize = ToInt(elements[48]);
					component.objectPoolAllowExpand = ToInt(elements[49]) != 0;
					component.prefabSpawnerPoolGameObjectId = ToInt(elements[50]);
					component.prefabSpawnerPointGameObjectId = ToInt(elements[51]);
					component.prefabSpawnerMode = ToInt(elements[52]);
					component.prefabSpawnerInterval = ToFloat(elements[53]);
					component.prefabSpawnerActionTargetGameObjectId = ToInt(elements[54]);
					component.prefabSpawnerSpawnedActionName = DecodeSceneToken(elements[55]);
					component.cameraBlendSourceGameObjectId = ToInt(elements[56]);
					component.cameraBlendTargetGameObjectId = ToInt(elements[57]);
					component.cameraBlendDuration = ToFloat(elements[58]);
					component.cameraBlendEasing = ToInt(elements[59]);
					component.cameraBlendPlayOnStart = ToInt(elements[60]) != 0;
					component.cameraShakePositionAmplitude = {
						ToFloat(elements[61]), ToFloat(elements[62]), ToFloat(elements[63])};
					component.cameraShakeRotationAmplitude = {
						ToFloat(elements[64]), ToFloat(elements[65]), ToFloat(elements[66])};
					component.cameraShakeFrequency = ToFloat(elements[67]);
					component.cameraShakeDuration = ToFloat(elements[68]);
					component.cameraShakePlayOnStart = ToInt(elements[69]) != 0;
					component.railBranchFollowerGameObjectId = ToInt(elements[70]);
					component.railBranchTargetPathGameObjectId = ToInt(elements[71]);
					component.railBranchTriggerMode = ToInt(elements[72]);
					component.railBranchTriggerNormalized = ToFloat(elements[73]);
					component.railBranchPreserveProgress = ToInt(elements[74]) != 0;
					component.railBranchTriggerOnce = ToInt(elements[75]) != 0;
					component.railBranchActionTargetGameObjectId = ToInt(elements[76]);
					component.railBranchActionName = DecodeSceneToken(elements[77]);

					if (elements.size() >= 79u) {
						component.cameraShakePriority = ToInt(elements[78]);
					}

					break;
				}

				break;
			}
		}
		else if (elements[0] == "WorkflowFoundation" && elements.size() >= 30u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.actionSequencePlayOnStart = ToInt(elements[3]) != 0;
					component.actionSequenceLoop = ToInt(elements[4]) != 0;
					component.actionSequenceStepType = ToInt(elements[5]);
					component.actionSequenceParallelGroup = ToInt(elements[6]);
					component.actionSequenceTargetGameObjectId = ToInt(elements[7]);
					component.actionSequenceActionName = DecodeSceneToken(elements[8]);
					component.actionSequenceWaitSeconds = ToFloat(elements[9]);
					component.actionSequenceActiveValue = ToInt(elements[10]) != 0;
					component.actionSequenceScenePath = DecodeSceneToken(elements[11]);
					component.actionSequenceSceneAdditive = ToInt(elements[12]) != 0;
					component.actionSequenceConditionMode = ToInt(elements[13]);
					component.actionSequenceCompareMode = ToInt(elements[14]);
					component.actionSequenceCompareValue = ToFloat(elements[15]);
					component.actionSequenceTrueStepIndex = ToInt(elements[16]);
					component.actionSequenceFalseStepIndex = ToInt(elements[17]);
					component.saveableKey = DecodeSceneToken(elements[18]);
					component.saveableTransform = ToInt(elements[19]) != 0;
					component.saveableActive = ToInt(elements[20]) != 0;
					component.saveableHealth = ToInt(elements[21]) != 0;
					component.saveableRigidbody = ToInt(elements[22]) != 0;
					component.saveableScriptProperties = ToInt(elements[23]) != 0;
					component.checkpointSlotName = DecodeSceneToken(elements[24]);
					component.checkpointSaveOnStart = ToInt(elements[25]) != 0;
					component.checkpointLoadOnStart = ToInt(elements[26]) != 0;
					component.checkpointActionTargetGameObjectId = ToInt(elements[27]);
					component.checkpointSavedActionName = DecodeSceneToken(elements[28]);
					component.checkpointLoadedActionName = DecodeSceneToken(elements[29]);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "ReusableGameplay" && elements.size() >= 67u) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					component.weaponLoadoutSelectedSlotIndex = ToInt(elements[3]);
					component.weaponLoadoutActionTargetGameObjectId = ToInt(elements[4]);
					component.weaponLoadoutChangedActionName = DecodeSceneToken(elements[5]);
					component.weaponLoadoutReloadedActionName = DecodeSceneToken(elements[6]);
					component.weaponSlotName = DecodeSceneToken(elements[7]);
					component.weaponSlotWeaponGameObjectId = ToInt(elements[8]);
					component.weaponSlotVisualGameObjectId = ToInt(elements[9]);
					component.weaponSlotCurrentAmmo = ToInt(elements[10]);
					component.weaponSlotReserveAmmo = ToInt(elements[11]);
					component.weaponSlotMaximumAmmo = ToInt(elements[12]);
					component.weaponSlotReloadSeconds = ToFloat(elements[13]);
					component.weaponSlotAutoReload = ToInt(elements[14]) != 0;
					component.targetSelectorSearchLayer = ToInt(elements[15]);
					component.targetSelectorMaximumDistance = ToFloat(elements[16]);
					component.targetSelectorMaximumAngle = ToFloat(elements[17]);
					component.targetSelectorReferenceGameObjectId = ToInt(elements[18]);
					component.targetSelectorOcclusionCheck = ToInt(elements[19]) != 0;
					component.targetSelectorMaximumTargets = ToInt(elements[20]);
					component.targetSelectorSelectionMode = ToInt(elements[21]);
					component.targetSelectorActionTargetGameObjectId = ToInt(elements[22]);
					component.targetSelectorFoundActionName = DecodeSceneToken(elements[23]);
					component.targetSelectorLostActionName = DecodeSceneToken(elements[24]);
					component.targetSelectorChangedActionName = DecodeSceneToken(elements[25]);
					component.targetSteeringTargetGameObjectId = ToInt(elements[26]);
					component.targetSteeringSelectorGameObjectId = ToInt(elements[27]);
					component.targetSteeringTurnSpeed = ToFloat(elements[28]);
					component.targetSteeringAcceleration = ToFloat(elements[29]);
					component.targetSteeringMaximumSpeed = ToFloat(elements[30]);
					component.targetSteeringStartDelay = ToFloat(elements[31]);
					component.targetSteeringPredictionSeconds = ToFloat(elements[32]);
					component.targetSteeringMode = ToInt(elements[33]);
					component.movementModifierLocalPositionOffset = {
						ToFloat(elements[34]), ToFloat(elements[35]), ToFloat(elements[36])};
					component.movementModifierLocalRotationOffset = {
						ToFloat(elements[37]), ToFloat(elements[38]), ToFloat(elements[39])};
					component.movementModifierAxisMask = ToInt(elements[40]);
					component.movementModifierInputRange = {
						ToFloat(elements[41]), ToFloat(elements[42])};
					component.movementModifierInputSpeed = ToFloat(elements[43]);
					component.movementModifierInputGameObjectId = ToInt(elements[44]);
					component.movementModifierActionMapName = DecodeSceneToken(elements[45]);
					component.movementModifierActionName = DecodeSceneToken(elements[46]);
					component.propertyTweenTargetGameObjectId = ToInt(elements[47]);
					component.propertyTweenComponentName = DecodeSceneToken(elements[48]);
					component.propertyTweenPropertyName = DecodeSceneToken(elements[49]);
					component.propertyTweenStartValue = {
						ToFloat(elements[50]), ToFloat(elements[51]), ToFloat(elements[52])};
					component.propertyTweenEndValue = {
						ToFloat(elements[53]), ToFloat(elements[54]), ToFloat(elements[55])};
					component.propertyTweenValueType = ToInt(elements[56]);
					component.propertyTweenDuration = ToFloat(elements[57]);
					component.propertyTweenCurve = ToInt(elements[58]);
					component.propertyTweenPlayOnStart = ToInt(elements[59]) != 0;
					component.propertyTweenLoop = ToInt(elements[60]) != 0;
					component.propertyTweenActionTargetGameObjectId = ToInt(elements[61]);
					component.propertyTweenCompletedActionName = DecodeSceneToken(elements[62]);
					component.actionRelayOnStart = ToInt(elements[63]) != 0;
					component.actionRelayTargetGameObjectId = ToInt(elements[64]);
					component.actionRelayActionName = DecodeSceneToken(elements[65]);
					component.actionRelayTargetEnabled = ToInt(elements[66]) != 0;

					if (elements.size() >= 77u) {
						component.targetPointPriority = ToFloat(elements[67]);
						component.targetPointRadius = ToFloat(elements[68]);
						component.targetPointAimOffset = {
							ToFloat(elements[69]), ToFloat(elements[70]), ToFloat(elements[71])};
						component.teamId = ToInt(elements[72]);
						component.teamTargetable = ToInt(elements[73]) != 0;
						component.targetSelectorTeamFilter = ToInt(elements[74]);
						component.targetSelectorSpecificTeamId = ToInt(elements[75]);
						component.targetSelectorIncludeNeutral = ToInt(elements[76]) != 0;
					}

					if (elements.size() >= 111u) {
						component.timerDuration = ToFloat(elements[77]);
						component.timerRepeat = ToInt(elements[78]) != 0;
						component.timerPlayOnStart = ToInt(elements[79]) != 0;
						component.timerActionTargetGameObjectId = ToInt(elements[80]);
						component.timerActionName = DecodeSceneToken(elements[81]);
						component.stateMachineInitialState = DecodeSceneToken(elements[82]);
						component.stateMachineActionTargetGameObjectId = ToInt(elements[83]);
						component.stateMachineChangedActionName = DecodeSceneToken(elements[84]);
						component.attributeName = DecodeSceneToken(elements[85]);
						component.attributeMinimum = ToFloat(elements[86]);
						component.attributeMaximum = ToFloat(elements[87]);
						component.attributeCurrent = ToFloat(elements[88]);
						component.attributeRegenerationPerSecond = ToFloat(elements[89]);
						component.attributeActionTargetGameObjectId = ToInt(elements[90]);
						component.attributeChangedActionName = DecodeSceneToken(elements[91]);
						component.destructibleHealthGameObjectId = ToInt(elements[92]);
						component.destructibleDisableComponentNames = DecodeSceneToken(elements[93]);
						component.destructibleDisableChildren = ToInt(elements[94]) != 0;
						component.destructibleActionTargetGameObjectId = ToInt(elements[95]);
						component.destructibleDestroyedActionName = DecodeSceneToken(elements[96]);
						component.formationLeaderGameObjectId = ToInt(elements[97]);
						component.formationLocalOffset = {ToFloat(elements[98]), ToFloat(elements[99]), ToFloat(elements[100])};
						component.formationPositionSpeed = ToFloat(elements[101]);
						component.formationRotationSpeed = ToFloat(elements[102]);
						component.formationFollowRotation = ToInt(elements[103]) != 0;
						component.targetLockSelectorGameObjectId = ToInt(elements[104]);
						component.targetLockSeconds = ToFloat(elements[105]);
						component.targetLockLostGraceSeconds = ToFloat(elements[106]);
						component.targetLockActionTargetGameObjectId = ToInt(elements[107]);
						component.targetLockStartedActionName = DecodeSceneToken(elements[108]);
						component.targetLockCompletedActionName = DecodeSceneToken(elements[109]);
						component.targetLockLostActionName = DecodeSceneToken(elements[110]);
					}

					component.targetSelectorCurrentTargetGameObjectId = -1;
					break;
				}

				break;
			}
		}
		else if (elements[0] == "ScriptProperty" && elements.size() >= 19) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					EditorScriptProperty scriptProperty{};
					scriptProperty.name = DecodeSceneToken(elements[3]);
					scriptProperty.displayName = DecodeSceneToken(elements[4]);
					scriptProperty.type = ToInt(elements[5]);
					scriptProperty.boolValue = ToInt(elements[6]) != 0;
					scriptProperty.intValue = ToInt(elements[7]);
					scriptProperty.floatValue = ToFloat(elements[8]);
					scriptProperty.vector2Value = {ToFloat(elements[9]), ToFloat(elements[10])};
					scriptProperty.vector3Value = {ToFloat(elements[11]), ToFloat(elements[12]), ToFloat(elements[13])};
					scriptProperty.stringValue = DecodeSceneToken(elements[14]);
					scriptProperty.minValue = ToFloat(elements[15]);
					scriptProperty.maxValue = ToFloat(elements[16]);
					scriptProperty.step = ToFloat(elements[17]);
					scriptProperty.hasRange = ToInt(elements[18]) != 0;
					component.scriptProperties.push_back(scriptProperty);
					break;
				}

				break;
			}
		}
		else if (elements[0] == "ComponentAddOverride" && elements.size() >= 3) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				if (EditorComponentUtility::FindComponent(gameObject, componentType) == nullptr) {
					gameObject.components.push_back(CreateComponent(componentType));
				}
				break;
			}
		}
		else if (elements[0] == "TargetSteeringBaseOverride" && elements.size() >= 10) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				EditorComponent* steering = EditorComponentUtility::FindComponent(
					gameObject,
					EditorComponentType::TargetSteering);
				if (steering != nullptr) {
					steering->targetSteeringTargetGameObjectId = ToInt(elements[2]);
					steering->targetSteeringSelectorGameObjectId = ToInt(elements[3]);
					steering->targetSteeringTurnSpeed = ToFloat(elements[4]);
					steering->targetSteeringAcceleration = ToFloat(elements[5]);
					steering->targetSteeringMaximumSpeed = ToFloat(elements[6]);
					steering->targetSteeringStartDelay = ToFloat(elements[7]);
					steering->targetSteeringPredictionSeconds = ToFloat(elements[8]);
					steering->targetSteeringMode = ToInt(elements[9]);
				}
				break;
			}
		}
		else if (elements[0] == "ComponentActiveOverride" && elements.size() >= 4) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type == componentType) {
						component.isActive = ToInt(elements[3]) != 0;
						break;
					}
				}
				break;
			}
		}
		else if (elements[0] == "ComponentAssetOverride" && elements.size() >= 4) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type == componentType) {
						component.assetPath = DecodeSceneToken(elements[3]);
						break;
					}
				}
				break;
			}
		}
		else if (elements[0] == "ProjectileSpawnPointOverride" && elements.size() >= 3) {
			const int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) continue;
				for (EditorComponent& component : gameObject.components) {
					if (component.type == EditorComponentType::ProjectileEmitter) {
						component.projectileSpawnPointGameObjectId = ToInt(elements[2]);
						break;
					}
				}
				break;
			}
		}
		else if (elements[0] == "GameObjectScaleOverride" && elements.size() >= 5) {
			const int32_t objectId = ToInt(elements[1]);
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id == objectId) {
					gameObject.scale = {ToFloat(elements[2]), ToFloat(elements[3]), ToFloat(elements[4])};
					break;
				}
			}
		}
		else if (elements[0] == "GameObjectPositionOverride" && elements.size() >= 5) {
			const int32_t objectId = ToInt(elements[1]);
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id == objectId) {
					gameObject.translate = {ToFloat(elements[2]), ToFloat(elements[3]), ToFloat(elements[4])};
					break;
				}
			}
		}
		else if (elements[0] == "InputEventBinding" && elements.size() >= 7) {
			const int32_t ownerId = ToInt(elements[1]);
			const EditorComponentType componentType = ComponentTypeFromIndex(ToInt(elements[2]));

			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				for (EditorComponent& component : gameObject.components) {
					if (component.type != componentType) {
						continue;
					}

					EditorInputEventBinding inputEventBinding{};
					inputEventBinding.actionMapName = DecodeSceneToken(elements[3]);
					inputEventBinding.actionName = DecodeSceneToken(elements[4]);
					inputEventBinding.functionName = DecodeSceneToken(elements[5]);
					inputEventBinding.valueType = ToInt(elements[6]);
					component.inputEventBindings.push_back(inputEventBinding);
					break;
				}

				break;
			}
		}
	}

	if (!hasSceneData) {
		return false;
	}

	// InputEventBinding 行を持たない旧 Scene には、PlayerInput の標準イベントを補完する。
	for (EditorGameObject& gameObject : loadedGameObjects) {
		for (EditorComponent& component : gameObject.components) {
			AddDefaultInputEventBindings(component);
		}
	}

	gameObjects_ = loadedGameObjects;  // 読み込み成功後だけ現在 Scene を差し替える
	physicsSettings_ = loadedPhysicsSettings;
	physicsSettings_.fixedTimeStep = (std::clamp)(physicsSettings_.fixedTimeStep, 0.001f, 0.1f);
	physicsSettings_.collisionStepCount = (std::clamp)(physicsSettings_.collisionStepCount, 1, 8);
	physicsSettings_.debugVectorScale = (std::clamp)(physicsSettings_.debugVectorScale, 0.01f, 10.0f);
	RebuildChildren();  // parentId から children 配列を作り直す
	RefreshNextGameObjectId();  // 次に生成する ID が既存 ID と衝突しないよう更新する
	undoStack_.clear();
	redoStack_.clear();

	return true;
}

bool EditorScene::SavePrefab(int32_t gameObjectId, const std::string& filePath) const {
	return SavePrefabVariant(gameObjectId, "", filePath);
}

bool EditorScene::SavePrefabVariant(
	int32_t gameObjectId,
	const std::string& basePrefabPath,
	const std::string& filePath) const {
	const EditorGameObject* rootGameObject = FindGameObject(gameObjectId);
	if (rootGameObject == nullptr || filePath.empty()) {
		return false;
	}

	std::unordered_set<int32_t> subtreeIds;
	std::vector<int32_t> pendingIds{gameObjectId};

	while (!pendingIds.empty()) {
		const int32_t currentId = pendingIds.back();
		pendingIds.pop_back();

		if (!subtreeIds.insert(currentId).second) {
			continue;
		}

		const EditorGameObject* currentGameObject = FindGameObject(currentId);
		if (currentGameObject == nullptr) {
			continue;
		}

		pendingIds.insert(
			pendingIds.end(),
			currentGameObject->children.begin(),
			currentGameObject->children.end());
	}

	EditorScene prefabScene;
	prefabScene.gameObjects_.clear();
	prefabScene.physicsSettings_ = physicsSettings_;

	for (const EditorGameObject& sourceGameObject : gameObjects_) {
		if (subtreeIds.find(sourceGameObject.id) == subtreeIds.end()) {
			continue;
		}

		EditorGameObject prefabGameObject = sourceGameObject;
		prefabGameObject.prefabSourceObjectId = sourceGameObject.id;
		prefabGameObject.prefabSourcePath.clear();
		prefabGameObject.prefabVariantBasePath = sourceGameObject.id == gameObjectId
			? basePrefabPath
			: "";

		if (sourceGameObject.id == gameObjectId) {
			prefabGameObject.parentId = kInvalidGameObjectId;
		}

		prefabScene.gameObjects_.push_back(prefabGameObject);
	}

	const std::filesystem::path prefabPath(filePath);
	const std::filesystem::path parentPath = prefabPath.parent_path();
	std::error_code directoryError;

	if (!parentPath.empty()) {
		std::filesystem::create_directories(parentPath, directoryError);
	}

	if (directoryError) {
		return false;
	}

	prefabScene.RebuildChildren();
	prefabScene.RefreshNextGameObjectId();
	return prefabScene.SaveScene(filePath);
}

int32_t EditorScene::InstantiatePrefab(const std::string& filePath) {
	EditorScene prefabScene;
	if (!prefabScene.LoadScene(filePath) || prefabScene.gameObjects_.empty()) {
		return kInvalidGameObjectId;
	}

	std::vector<int32_t> addedGameObjectIds;
	if (!MergeScene(prefabScene, addedGameObjectIds) || addedGameObjectIds.empty()) {
		return kInvalidGameObjectId;
	}

	int32_t instantiatedRootId = addedGameObjectIds.front();
	for (size_t gameObjectIndex = 0U; gameObjectIndex < addedGameObjectIds.size(); ++gameObjectIndex) {
		EditorGameObject* instantiatedGameObject = FindGameObject(addedGameObjectIds[gameObjectIndex]);
		const EditorGameObject& prefabGameObject = prefabScene.gameObjects_[gameObjectIndex];

		if (instantiatedGameObject == nullptr) {
			continue;
		}

		instantiatedGameObject->prefabSourcePath = filePath;
		instantiatedGameObject->prefabSourceObjectId = prefabGameObject.id;

		if (prefabGameObject.parentId < 0) {
			instantiatedRootId = instantiatedGameObject->id;
		}
	}

	return instantiatedRootId;
}

bool EditorScene::ApplyPrefabInstance(int32_t gameObjectId) {
	const EditorGameObject* selectedGameObject = FindGameObject(gameObjectId);
	if (selectedGameObject == nullptr || selectedGameObject->prefabSourcePath.empty()) {
		return false;
	}

	const std::string sourcePath = selectedGameObject->prefabSourcePath;
	int32_t rootId = gameObjectId;
	const EditorGameObject* rootGameObject = selectedGameObject;

	while (rootGameObject->parentId >= 0) {
		const EditorGameObject* parentGameObject = FindGameObject(rootGameObject->parentId);
		if (parentGameObject == nullptr || parentGameObject->prefabSourcePath != sourcePath) {
			break;
		}

		rootId = parentGameObject->id;
		rootGameObject = parentGameObject;
	}

	return SavePrefabVariant(rootId, rootGameObject->prefabVariantBasePath, sourcePath);
}

int32_t EditorScene::RevertPrefabInstance(int32_t gameObjectId) {
	const EditorGameObject* selectedGameObject = FindGameObject(gameObjectId);
	if (selectedGameObject == nullptr || selectedGameObject->prefabSourcePath.empty()) {
		return kInvalidGameObjectId;
	}

	const std::string sourcePath = selectedGameObject->prefabSourcePath;
	EditorScene sourcePrefabScene;
	if (!sourcePrefabScene.LoadScene(sourcePath) || sourcePrefabScene.gameObjects_.empty()) {
		return kInvalidGameObjectId;
	}

	int32_t rootId = gameObjectId;
	const EditorGameObject* rootGameObject = selectedGameObject;

	while (rootGameObject->parentId >= 0) {
		const EditorGameObject* parentGameObject = FindGameObject(rootGameObject->parentId);
		if (parentGameObject == nullptr || parentGameObject->prefabSourcePath != sourcePath) {
			break;
		}

		rootId = parentGameObject->id;
		rootGameObject = parentGameObject;
	}

	const int32_t parentId = rootGameObject->parentId;
	const Vector3 scenePosition = rootGameObject->translate;
	const Vector3 sceneRotation = rootGameObject->rotate;
	const Vector3 sceneScale = rootGameObject->scale;
	const EditorScene sceneBackup = *this;

	if (!DeleteGameObject(rootId)) {
		return kInvalidGameObjectId;
	}

	const int32_t revertedRootId = InstantiatePrefab(sourcePath);
	EditorGameObject* revertedRoot = FindGameObject(revertedRootId);

	if (revertedRoot == nullptr) {
		*this = sceneBackup;
		return kInvalidGameObjectId;
	}

	revertedRoot->translate = scenePosition;
	revertedRoot->rotate = sceneRotation;
	revertedRoot->scale = sceneScale;
	SetParent(revertedRootId, parentId);
	return revertedRootId;
}

bool EditorScene::MergeScene(
	const EditorScene& sourceScene,
	std::vector<int32_t>& addedGameObjectIds) {
	addedGameObjectIds.clear();

	if (sourceScene.gameObjects_.empty()) {
		return true;
	}

	std::unordered_map<int32_t, int32_t> remappedIds;
	for (const EditorGameObject& sourceGameObject : sourceScene.gameObjects_) {
		remappedIds[sourceGameObject.id] = nextGameObjectId_;
		addedGameObjectIds.push_back(nextGameObjectId_);
		nextGameObjectId_++;
	}

	for (const EditorGameObject& sourceGameObject : sourceScene.gameObjects_) {
		EditorGameObject addedGameObject = sourceGameObject;
		addedGameObject.id = remappedIds[sourceGameObject.id];
		RemapGameObjectReference(addedGameObject.parentId, remappedIds);
		addedGameObject.children.clear();

		for (EditorComponent& component : addedGameObject.components) {
			RemapComponentGameObjectReferences(component, remappedIds);
		}

		gameObjects_.push_back(addedGameObject);
	}

	RebuildChildren();
	return true;
}

//============================================================
// Undo / Redo
//============================================================

void EditorScene::PushUndo() {
	undoStack_.push_back(gameObjects_);  // 現在の GameObject 配列を丸ごと保存する
	redoStack_.clear();  // 新しい編集が入ったら Redo 履歴は無効になる
}

bool EditorScene::Undo() {
	// 戻せる履歴がなければ失敗
	if (undoStack_.empty()) {
		return false;
	}

	redoStack_.push_back(gameObjects_);  // 現在状態を Redo に退避してから、Undo の最後の状態へ戻す
	gameObjects_ = undoStack_.back();
	undoStack_.pop_back();
	RebuildChildren();  // 復元後に親子配列と ID 採番を整える
	RefreshNextGameObjectId();
	return true;
}

bool EditorScene::Redo() {
	// やり直せる履歴がなければ失敗
	if (redoStack_.empty()) {
		return false;
	}

	undoStack_.push_back(gameObjects_);  // 現在状態を Undo に戻せるよう退避してから、Redo の最後の状態へ進める
	gameObjects_ = redoStack_.back();
	redoStack_.pop_back();
	RebuildChildren();
	RefreshNextGameObjectId();
	return true;
}

//============================================================
// Find
//============================================================

EditorGameObject* EditorScene::FindGameObject(int32_t gameObjectId) {
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);  // ID から配列 index を取得して、編集可能ポインタを返す
	if (gameObjectIndex < 0) {
		return nullptr;
	}

	return &gameObjects_[static_cast<size_t>(gameObjectIndex)];
}

const EditorGameObject* EditorScene::FindGameObject(int32_t gameObjectId) const {
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);  // ID から配列 index を取得して、読み取り専用ポインタを返す
	if (gameObjectIndex < 0) {
		return nullptr;
	}

	return &gameObjects_[static_cast<size_t>(gameObjectIndex)];
}

EditorPhysicsSettings& EditorScene::GetPhysicsSettings() {
	return physicsSettings_;
}

const EditorPhysicsSettings& EditorScene::GetPhysicsSettings() const {
	return physicsSettings_;
}

std::vector<EditorGameObject>& EditorScene::GetGameObjects() {
	return gameObjects_;
}

const std::vector<EditorGameObject>& EditorScene::GetGameObjects() const {
	return gameObjects_;
}

//============================================================
// Private
//============================================================

EditorComponent EditorScene::CreateComponent(EditorComponentType type) const {
	// 全 Component が持つ共通初期値
	EditorComponent component{};
	component.type = type;
	component.isActive = true;
	component.assetPath = "";
	component.textureAssetPath = "";
	component.uvLayoutTextureAssetPath = "";
	component.useImportedMaterialTextures = false;
	component.color = {1.0f, 1.0f, 1.0f};
	component.intensity = 1.0f;
	component.metallic = 0.0f;
	component.roughness = 0.5f;
	component.ior = 1.0f;
	component.alpha = 1.0f;
	component.lightingMode = 3;
	component.reflectionStrength = 0.0f;
	component.emissionStrength = 0.0f;
	component.emissionColor = {1.0f, 1.0f, 1.0f};
	component.normalScale = 1.0f;
	component.ambientOcclusionStrength = 1.0f;
	component.heightScale = 0.02f;
	component.alphaCutoff = 0.5f;
	component.clearCoat = 0.0f;
	component.clearCoatRoughness = 0.1f;
	component.transmission = 0.0f;
	component.subsurface = 0.0f;
	component.anisotropy = 0.0f;
	component.anisotropyRotation = 0.0f;
	component.materialThickness = 0.1f;
	component.materialWetness = 0.0f;
	component.materialWaterlineHeight = 0.0f;
	component.materialWaterlineWidth = 0.25f;
	component.specularTint = 0.0f;
	component.sheen = 0.0f;
	component.sheenTint = 0.5f;
	component.alphaMode = 0;
	component.doubleSided = false;
	component.uvTiling = {1.0f, 1.0f};
	component.uvOffset = {0.0f, 0.0f};
	component.mass = 1.0f;
	component.automaticMassFromCollider = false;
	component.bodyDensity = 500.0f;
	component.drag = 0.0f;
	component.useGravity = true;
	component.isKinematic = false;
	component.isTrigger = false;
	component.bounciness = 0.0f;
	component.velocity = {0.0f, 0.0f, 0.0f};
	component.angularVelocity = {0.0f, 0.0f, 0.0f};
	component.angularDrag = 0.05f;
	component.inertiaMultiplier = 1.0f;
	component.centerOfMassOffset = {0.0f, 0.0f, 0.0f};
	component.applyGyroscopicForce = false;
	component.freezePositionX = false;
	component.freezePositionY = false;
	component.freezePositionZ = false;
	component.freezeRotationX = false;
	component.freezeRotationY = false;
	component.freezeRotationZ = false;
	component.interpolationMode = 0;
	component.collisionDetectionMode = 0;
	component.dynamicFriction = 0.6f;
	component.staticFriction = 0.6f;
	component.frictionCombineMode = 0;
	component.bouncinessCombineMode = 0;
	component.physicsLayer = 0;
	component.generateContactEvents = true;
	component.connectedGameObjectId = kInvalidGameObjectId;
	component.jointAxis = {0.0f, 1.0f, 0.0f};
	component.jointMinLimit = -3.1415926f;
	component.jointMaxLimit = 3.1415926f;
	component.jointMinDistance = 0.0f;
	component.jointMaxDistance = 1.0f;
	component.jointSpringFrequency = 0.0f;
	component.jointSpringDamping = 0.5f;
	component.colliderCenter = {0.0f, 0.0f, 0.0f};
	component.colliderSize = {1.0f, 1.0f, 1.0f};
	component.colliderRadius = 0.5f;
	component.autoConvexMaximumHulls = 8;
	component.inputMoveSpeed = 3.0f;
	component.inputForwardKey = 0x11;
	component.inputBackKey = 0x1F;
	component.inputLeftKey = 0x1E;
	component.inputRightKey = 0x20;
		component.inputJumpKey = 0x39;
		component.inputMouseSensitivity = 1.0f;
		component.inputInvertY = false;
		component.inputActionMapName = "Player";
		component.inputBehavior = 0;
		component.inputMoveEventName = "OnMove";
		component.inputJumpEventName = "OnJump";
		component.inputFireEventName = "OnFire";
		component.hapticStrength = 1.0f;
		component.hapticDurationMs = 120;
		component.hapticLoop = false;
		component.audioVolume = 1.0f;
		component.audioPitch = 1.0f;
		component.audioLoop = false;
		component.audioPlayOnAwake = true;
		component.audioSpatialBlend = 1.0f;
		component.audioMinDistance = 1.0f;
		component.audioMaxDistance = 50.0f;
		component.audioBus = 0;
		component.audioMaxVoices = 4;
		component.audioRetriggerInterval = 0.03f;
		component.audioDopplerLevel = 1.0f;
		component.audioSpread = 0.0f;
		component.audioConeInnerAngle = 360.0f;
		component.audioConeOuterAngle = 360.0f;
		component.audioConeOuterVolume = 0.2f;
		component.audioOcclusionStrength = 0.65f;
		component.audioReverbSend = 0.0f;
		component.audioReflectionStrength = 0.0f;
		component.navAgentRadius = 0.5f;
		component.navAgentHeight = 2.0f;
		component.navMaxSpeed = 3.5f;
		component.navMaxAcceleration = 8.0f;
		component.navStoppingDistance = 0.5f;
		component.navAutoRepath = true;
		component.navCarve = true;
		component.navMaxSlope = 45.0f;
		component.navMaxClimb = 0.5f;
		component.navAreaOverride = false;
		component.navArea = 0;
		component.navIgnoreFromBuild = false;
		component.navBidirectional = true;
		component.navCostModifier = 1.0f;
		component.rollingTorque = 50.0f;
		component.rollingHorsepower = 5.0f;
		component.constraintWeight = 1.0f;
		component.constraintPositionOffset = {0.0f, 0.0f, 0.0f};
		component.constraintRotationOffset = {0.0f, 0.0f, 0.0f};
		component.constraintAimAxis = 2;
		component.constraintUpAxis = 1;
		component.constraintRoll = 0.0f;
		component.constraintFreezeAxisX = false;
		component.constraintFreezeAxisY = false;
		component.constraintFreezeAxisZ = false;
		component.animationSpeed = 1.0f;
		component.animationLoop = true;
		component.animationPlayOnAwake = true;
		component.animationType = 0;
		component.animationAmplitude = 1.0f;
		component.animationClipIndex = 0;
		component.animatorState = 0;
		component.animatorApplyRootMotion = false;
		component.animatorAutoVelocity = true;
		component.animatorTransitionDuration = 0.15f;
		component.animatorMoveX = 0.0f;
		component.animatorMoveY = 0.0f;
		component.animatorSpeedParameter = 0.0f;
		component.animatorIdleClipIndex = 0;
		component.animatorForwardClipIndex = 0;
		component.animatorBackwardClipIndex = 0;
		component.animatorLeftClipIndex = 0;
		component.animatorRightClipIndex = 0;
		component.particleRate = 10.0f;
		component.particleLifetime = 2.0f;
		component.particleSpeed = 5.0f;
		component.particleSize = 0.5f;
		component.particleMaxCount = 256;
		component.particleBurstCount = 0;
		component.particleShape = 0;
		component.particleSimulationSpace = 0;
		component.particleDuration = 5.0f;
		component.particleStartDelay = 0.0f;
		component.particleGravity = 0.0f;
		component.particleDrag = 0.0f;
		component.particleEndSize = 0.0f;
		component.particleShapeRadius = 1.0f;
		component.particleShapeAngle = 25.0f;
		component.particleSpeedRandomness = 0.2f;
		component.particleLifetimeRandomness = 0.1f;
		component.particleSizeRandomness = 0.1f;
		component.particleRotationSpeed = 0.0f;
		component.particleLooping = true;
		component.particleCollision = false;
		component.particleEndColor = {1.0f, 1.0f, 1.0f};
		component.particleDirection = {0.0f, 1.0f, 0.0f};
		component.particleBoxSize = {1.0f, 1.0f, 1.0f};
		component.particleStartAlpha = 1.0f;
		component.particleEndAlpha = 0.0f;
		component.particleEmissionStrength = 1.0f;
		component.particleEndSpeedMultiplier = 1.0f;
		component.particleNoiseStrength = 0.0f;
		component.particleNoiseFrequency = 1.0f;
		component.particleCollisionBounce = 0.35f;
		component.particleCollisionFriction = 0.2f;
		component.particlePrewarm = false;
		component.particleMotionType = 0;
		component.particleMotionCenter = {0.0f, 0.0f, 0.0f};
		component.particleAngularSpeed = 45.0f;
		component.particleRadialAcceleration = 0.0f;
		component.particleWaveAmplitude = 0.0f;
		component.particleWaveFrequency = 1.0f;
		component.particleAttractorStrength = 0.0f;
		component.particleRenderAssetPath.clear();
		component.particleBillboardMode = 0;
		component.particleBillboardStretch = 1.0f;
		component.freeMoveSpeed = 5.0f;
		component.freeRotateSpeed = 90.0f;
		component.freeMoveAxes = 7;  // X|Y|Z all
		component.freeRotateAxes = 7;  // X|Y|Z all
		component.freeUseLocalSpace = true;
		component.freeRotationInput = {0.0f, 0.0f, 0.0f};
		component.oceanGridResolution = 2048;
		component.oceanSize = 240.0f;
		component.oceanWaveHeight = 1.8f;
		component.oceanMaxWaveHeight = 4.5f;
		component.oceanWaveLength = 28.0f;
		component.oceanWaveSpeed = 1.0f;
		component.oceanTimeScale = 1.0f;
		component.oceanChoppiness = 0.65f;
		component.oceanPrimaryDirection = {1.0f, 0.28f};
		component.oceanSecondaryDirection = {-0.45f, 1.0f};
		component.oceanSecondaryWaveScale = 0.45f;
		component.oceanRippleScale = 0.22f;
		component.oceanRippleStrength = 0.12f;
		component.oceanWindSpeed = 14.0f;
		component.oceanWaterDepth = 80.0f;
		component.oceanDirectionSpread = 0.35f;
		component.oceanSwellStrength = 0.65f;
		component.oceanSpectrumSeed = 7.0f;
		component.oceanCrestSharpness = 0.65f;
		component.oceanFoamStrength = 1.0f;
		component.oceanFoamThreshold = 0.58f;
		component.oceanRoughness = 0.12f;
		component.oceanReflectionStrength = 0.85f;
		component.oceanDetailNormalStrength = 0.45f;
		component.oceanAbsorptionDistance = 18.0f;
		component.oceanRefractionDistortion = 0.08f;
		component.oceanShallowColor = {0.04f, 0.34f, 0.46f};
		component.oceanDeepColor = {0.005f, 0.045f, 0.11f};
		component.oceanSunDiffuseInfluence = 1.0f;
		component.oceanSunSpecularInfluence = 1.0f;
		component.oceanSunGlitterInfluence = 1.0f;
		component.oceanSkyReflectionInfluence = 1.0f;
		component.oceanAmbientInfluence = 1.0f;
		component.oceanDiffuseFloor = 0.22f;
		component.oceanGlitterIntensity = 1.0f;
		component.oceanGlitterSharpness = 0.5f;
		component.oceanGlitterDensity = 1.0f;
		component.oceanGlitterThreshold = 0.0f;
		component.oceanGlitterMaxClamp = 7.5f;
		component.oceanMacroReflectionInfluence = 1.0f;
		component.oceanCurvatureInfluence = 1.0f;
		component.oceanTroughOcclusionStrength = 0.08f;
		component.oceanCrestHazeStrength = 0.16f;
		component.oceanCrestDetailBoost = 0.18f;
		component.oceanSlopeRefractionInfluence = 0.35f;
		component.oceanMediumWaveStrength = 1.35f;
		component.oceanWaveColorSeparation = 0.22f;
		component.oceanShapeRoughnessVariation = 0.18f;
		component.oceanDetailFilterSharpness = 1.55f;
		component.oceanGrazingShapeVisibility = 0.35f;
		component.buoyancyOceanGameObjectId = -1;
		component.buoyancyCenterOffset = {0.0f, 0.0f, 0.0f};
		component.buoyancyHullSize = {3.0f, 1.2f, 6.0f};
		component.buoyancyStrength = 18.0f;
		component.buoyancyMaxSubmersion = 2.0f;
		component.buoyancyDamping = 5.0f;
		component.buoyancyWaterDrag = 1.4f;
		component.buoyancyAngularDrag = 1.8f;
		component.buoyancyNormalInfluence = 0.2f;
		component.buoyancyLateralDrag = 4.0f;
		component.buoyancyVerticalDrag = 2.5f;
		component.buoyancySlammingStrength = 2.0f;
		component.buoyancyUseCenterPoint = true;
		component.buoyancyAutomaticPhysicalProperties = false;
		component.buoyancyWaterDensity = 1025.0f;
		component.buoyancyTargetSubmersionRatio = 0.55f;
		component.railPathGameObjectId = -1;
		component.railSpeed = 8.0f;
		component.railStartNormalized = 0.0f;
		component.railLookAheadDistance = 1.0f;
		component.railAcceleration = 0.0f;
		component.railDeceleration = 0.0f;
		component.railLoop = false;
		component.railOrientToPath = true;
		component.railUseSmoothCurve = true;
		component.railStartPaused = false;
		component.railReverse = false;
		component.railStopAtEnd = true;
		component.railMovementMode = 0;
		component.railPositionInfluence = {1.0f, 1.0f, 1.0f};
		component.railRotationInfluence = {1.0f, 1.0f, 1.0f};
		component.railPositionSpring = 8.0f;
		component.railPositionDamping = 5.0f;
		component.railMaximumAcceleration = 30.0f;
		component.railRotationSpring = 8.0f;
		component.railRotationDamping = 4.0f;
		component.railMaximumAngularAcceleration = 12.0f;
		component.railLocalForwardAxis = 0;
		component.railShipHorizontalThrust = true;
		component.railShipLateralAssist = 0.2f;
		component.railMaximumRollAngle = 15.0f;
		component.railRollRestorationStrength = 10.0f;
		component.railRollDamping = 5.0f;
		component.railMaximumPitchAngle = 10.0f;
		component.railPitchRestorationStrength = 10.0f;
		component.railPitchDamping = 5.0f;
		component.railMaximumYawAngle = 0.0f;
		component.railYawRestorationStrength = 10.0f;
		component.railYawDamping = 5.0f;
		component.railYawSafetyAssistEnabled = true;
		component.railYawSafetyStage1Degrees = 10.0f;
		component.railYawSafetyStage2Degrees = 20.0f;
		component.railYawSafetyStage4Degrees = 45.0f;
		component.railYawSafetyMaxRestorationScale = 3.0f;
		component.railYawSafetyMinSpeedScale = 0.15f;
		component.railMaxForwardRecoveryError = 20.0f;
		// 既存の段階的Attitude Safety(復元Torque)は既定OFF、単純な絶対角度制限を既定ONにする。
		// 今回のPlayerShipテスト設定。既存機能は削除せずInspectorから独立にON/OFFできる。
		component.railAttitudeSafetyAssistEnabled = false;
		component.railRollFreeDegrees = 15.0f;
		component.railRollEmergencyDegrees = 45.0f;
		component.railPitchFreeDegrees = 12.0f;
		component.railPitchEmergencyDegrees = 40.0f;
		component.railAttitudeSafetyStrength = 10.0f;
		component.railAttitudeSafetyDamping = 5.0f;
		component.railAttitudeSafetyMaxTorque = 8.0f;
		component.railAttitudeSafetyMinForwardScale = 0.1f;
		component.railPhysicalCatchupSpeedMultiplier = 1.15f;
		component.railAttitudeAngleLimitEnabled = true;
		component.railAttitudeAngleLimitMaxPitchDegrees = 15.0f;
		component.railAttitudeAngleLimitMaxRollDegrees = 15.0f;
		component.railAttitudeAngleSoftLimitEnabled = false;
		component.railAttitudeAngleSoftLimitStrength = 15.0f;
		component.railAttitudeAngleSoftLimitDamping = 6.0f;
		component.railAttitudeAngleSoftLimitMaxTorque = 20.0f;
		component.railEngineSpeedGain = 3.0f;
		component.railEngineAccelResponse = 8.0f;
		component.railEngineDecelResponse = 4.0f;
		component.railEngineMaxAcceleration = 14.0f;
		component.railSteeringBaseLookAheadDistance = 8.0f;
		component.railSteeringLookAheadTime = 0.4f;
		component.railSteeringYawGain = 6.0f;
		component.railSteeringYawDamping = 4.0f;
		component.railSteeringMaxYawAngularAcceleration = 8.0f;
		component.railLateralAssistDeadZone = 1.0f;
		component.railLateralAssistSoftRadius = 3.0f;
		component.railLateralAssistEmergencyRadius = 6.0f;
		component.railLateralAssistMaxMultiplier = 1.0f;
		component.railHullLateralGripEnabled = true;
		component.railHullLateralGripStrength = 2.0f;
		component.railHullLateralGripMaxAcceleration = 20.0f;
		component.railHullLateralGripMinSpeed = 3.0f;
		component.railHullLateralGripFullSpeed = 15.0f;
		component.railHullLateralGripDeadZoneSpeed = 0.5f;
		component.railHullLateralGripSlipStartDegrees = 5.0f;
		component.railHullLateralGripSlipFullDegrees = 30.0f;
		component.railMode2MaxCombinedAcceleration = 60.0f;
		component.railMode2MovementStyle = 0;
		component.railRideYawSampleDistance = 2.0f;
		component.railMovementRange = {5.0f, 3.0f};
		component.railStartOffset = {0.0f, 0.0f};
		component.railOffsetMoveSpeed = 8.0f;
		component.railUsePlayerInput = false;
		component.railInputActionMapName = "Player";
		component.railInputActionName = "Move";
		// Runtime診断値。Playで毎FixedUpdateに上書きされるため保存対象にしない。
		component.buoyancyDebugBuoyancyForce = 0.0f;
		component.buoyancyDebugPressureDragForce = 0.0f;
		component.buoyancyDebugPressureUpwardForce = 0.0f;
		component.buoyancyDebugSkinFrictionForce = 0.0f;
		component.buoyancyDebugAddedMassForce = 0.0f;
		component.buoyancyDebugSlammingForce = 0.0f;
		component.buoyancyDebugWaveMakingResistance = 0.0f;
		component.buoyancyDebugSubmergedRatio = 0.0f;
		component.buoyancyDebugWettedArea = 0.0f;
		component.buoyancyDebugTrimAngleDegrees = 0.0f;
		component.buoyancyDebugForwardSpeed = 0.0f;
		component.buoyancyDebugWeightForce = 0.0f;
		component.buoyancyDebugAddedMassCoriolisTorque = {0.0f, 0.0f, 0.0f};
		component.buoyancyDebugSideslipAngleDegrees = 0.0f;
		component.railDebugFollowForce = {0.0f, 0.0f, 0.0f};
		component.railDebugFollowTorque = {0.0f, 0.0f, 0.0f};
		component.railDebugPositionError = {0.0f, 0.0f, 0.0f};
		component.railDebugYawError = 0.0f;
		component.railDebugAppliedPositionInfluence = {0.0f, 0.0f, 0.0f};
		component.railDebugAppliedRotationInfluence = {0.0f, 0.0f, 0.0f};
		component.railDebugCurrentSpeed = 0.0f;
		component.railDebugActualForwardSpeed = 0.0f;
		component.railDebugYawSafetySpeedScale = 1.0f;
		component.railDebugYawSafetyRestorationScale = 1.0f;
		component.railDebugForwardPositionScale = 1.0f;
		component.railDebugForwardPositionError = 0.0f;
		component.railDebugLateralPositionError = 0.0f;
		component.railDebugForwardCorrectionForce = 0.0f;
		component.railDebugLateralCorrectionForce = 0.0f;
		component.railDebugBoatPitchDegrees = 0.0f;
		component.railDebugBoatRollDegrees = 0.0f;
		component.railDebugPitchSafetyFactor = 0.0f;
		component.railDebugRollSafetyFactor = 0.0f;
		component.railDebugAttitudeRecoveryTorque = {0.0f, 0.0f, 0.0f};
		component.railDebugGameplayRailProgress = 0.0f;
		component.railDebugPhysicalRailProgress = 0.0f;
		component.railDebugPhysicalTargetSpeed = 0.0f;
		component.railDebugPitchAngleLimited = 0.0f;
		component.railDebugRollAngleLimited = 0.0f;
		component.railDebugClosestRailDistance = 0.0f;
		component.railDebugClosestRailDistanceDelta = 0.0f;
		component.railDebugSteeringLookAheadDistance = 0.0f;
		component.railDebugSteeringYawErrorDegrees = 0.0f;
		component.railDebugEngineAcceleration = 0.0f;
		component.railDebugLateralAssistAcceleration = 0.0f;
		component.railDebugLateralAssistScale = 0.0f;
		component.railDebugYawAngularVelocity = 0.0f;
		component.railDebugShipForward = {0.0f, 0.0f, 0.0f};
		component.railDebugShipRight = {0.0f, 0.0f, 0.0f};
		component.railDebugForwardVelocitySlipAngleDegrees = 0.0f;
		component.railDebugLateralSpeed = 0.0f;
		component.railDebugHullLateralGripAcceleration = 0.0f;
		component.railDebugHullLateralGripScale = 0.0f;
		component.railDebugHullLateralGripSpeedFactor = 0.0f;
		component.railDebugHullLateralGripSlipFactor = 0.0f;
		component.railDebugHorizontalSpeed = 0.0f;
		component.railDebugPreClampAcceleration = 0.0f;
		component.railDebugPostClampAcceleration = 0.0f;
		component.railDebugMode2ClampScale = 1.0f;
		component.railDebugRailRidePosition = {0.0f, 0.0f, 0.0f};
		component.railDebugRailRideActualPosition = {0.0f, 0.0f, 0.0f};
		component.railDebugRailRidePositionErrorXZ = 0.0f;
		component.railDebugRailRideForward = {0.0f, 0.0f, 0.0f};
		component.railDebugRailRideTargetYawDegrees = 0.0f;
		component.railDebugRailRideFinalYawDegrees = 0.0f;
		component.railDebugRailRideYawErrorDegrees = 0.0f;
		component.railDebugRailRideVelocityXZ = {0.0f, 0.0f, 0.0f};
		component.railDebugRailRideActualVelocityXZ = {0.0f, 0.0f, 0.0f};
		component.railDebugRailRideVelocityDirectionErrorDegrees = 0.0f;
		component.railDebugRailRidePhysicsY = 0.0f;
		component.railDebugRailRideFinalY = 0.0f;
		component.railDebugRailRidePhysicsPitchDegrees = 0.0f;
		component.railDebugRailRideFinalPitchDegrees = 0.0f;
		component.railDebugRailRidePhysicsRollDegrees = 0.0f;
		component.railDebugRailRideFinalRollDegrees = 0.0f;
		component.aerodynamicAirDensity = 1.225f;
		component.aerodynamicDragCoefficient = 0.47f;
		component.aerodynamicReferenceArea = 1.0f;
		component.aerodynamicBaseLiftCoefficient = 0.0f;
		component.aerodynamicLiftSlope = 0.0f;
		component.aerodynamicLiftArea = 1.0f;
		component.aerodynamicSideForceCoefficient = 0.0f;
		component.aerodynamicSideArea = 1.0f;
		component.aerodynamicZeroLiftAngleDegrees = 0.0f;
		component.aerodynamicStallAngleDegrees = 20.0f;
		component.aerodynamicAngularDragCoefficient = 0.05f;
		component.aerodynamicMagnusCoefficient = 0.0f;
		component.aerodynamicCenterOfPressure = {0.0f, 0.0f, 0.0f};
		component.aerodynamicAmbientWindVelocity = {0.0f, 0.0f, 0.0f};
		component.aerodynamicMaximumForce = 100000.0f;
		component.windZoneMode = 0;
		component.windZoneDirection = {1.0f, 0.0f, 0.0f};
		component.windZoneSpeed = 10.0f;
		component.windZoneRadius = 0.0f;
		component.windZoneTurbulenceStrength = 0.0f;
		component.windZoneTurbulenceFrequency = 1.0f;
		component.gravityFieldMode = 0;
		component.gravityFieldGravitationalConstant = 6.67430e-11f;
		component.gravityFieldSourceMass = 1.0e11f;
		component.gravityFieldAcceleration = 9.80665f;
		component.gravityFieldMinimumDistance = 1.0f;
		component.gravityFieldInfluenceRadius = 0.0f;
		component.gravityFieldMaximumAcceleration = 100.0f;
		component.rotatingFrameAngularVelocity = {0.0f, 1.0f, 0.0f};
		component.rotatingFrameAngularAcceleration = {0.0f, 0.0f, 0.0f};
		component.rotatingFrameLinearVelocity = {0.0f, 0.0f, 0.0f};
		component.rotatingFrameRadius = 0.0f;
		component.rotatingFrameMaximumAcceleration = 100.0f;
		component.fluidVolumeSize = {10.0f, 5.0f, 10.0f};
		component.fluidDensity = 1000.0f;
		component.fluidDynamicViscosity = 0.001f;
		component.fluidDragCoefficient = 1.0f;
		component.fluidFlowVelocity = {0.0f, 0.0f, 0.0f};
		component.fluidAngularViscosity = 1.0f;
		component.fluidMaximumForce = 1000000.0f;
		component.springForceTargetGameObjectId = -1;
		component.springForceLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.springForceTargetLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.springForceWorldAnchor = {0.0f, 0.0f, 0.0f};
		component.springForceRestLength = 1.0f;
		component.springForceStiffness = 50.0f;
		component.springForceDamping = 5.0f;
		component.springForceMaximumForce = 100000.0f;
		component.springForceApplyReaction = true;
		component.ropeTargetGameObjectId = -1;
		component.ropeLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.ropeTargetLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.ropeWorldAnchor = {0.0f, 0.0f, 0.0f};
		component.ropeMaximumLength = 5.0f;
		component.ropeStiffness = 2000.0f;
		component.ropeDamping = 80.0f;
		component.ropeMaximumTension = 100000.0f;
		component.ropeBreakingTension = 0.0f;
		component.ropeApplyReaction = true;
		component.ropeIsBroken = false;
		component.ropeCurrentLength = 0.0f;
		component.ropeCurrentTension = 0.0f;
		component.torsionTargetGameObjectId = -1;
		component.torsionRestRotation = {0.0f, 0.0f, 0.0f};
		component.torsionStiffness = 50.0f;
		component.torsionDamping = 8.0f;
		component.torsionMaximumTorque = 100000.0f;
		component.torsionApplyReaction = true;
		component.thrusterDirection = {0.0f, 0.0f, 1.0f};
		component.thrusterLocalApplicationPoint = {0.0f, 0.0f, 0.0f};
		component.thrusterForce = 100.0f;
		component.thrusterThrottle = 1.0f;
		component.thrusterUseLocalDirection = true;
		component.pulleyTargetGameObjectId = -1;
		component.pulleyOwnerLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.pulleyTargetLocalAnchor = {0.0f, 0.0f, 0.0f};
		component.pulleyOwnerWorldSupport = {-1.0f, 3.0f, 0.0f};
		component.pulleyTargetWorldSupport = {1.0f, 3.0f, 0.0f};
		component.pulleyTotalLength = 6.0f;
		component.pulleyRatio = 1.0f;
		component.pulleyStiffness = 3000.0f;
		component.pulleyDamping = 100.0f;
		component.pulleyMaximumTension = 100000.0f;
		component.pulleyBreakingTension = 0.0f;
		component.pulleyIsBroken = false;
		component.servoTargetGameObjectId = -1;
		component.servoTargetPosition = {0.0f, 0.0f, 0.0f};
		component.servoTargetRotation = {0.0f, 0.0f, 0.0f};
		component.servoPositionStiffness = 100.0f;
		component.servoPositionDamping = 20.0f;
		component.servoMaximumForce = 100000.0f;
		component.servoRotationStiffness = 50.0f;
		component.servoRotationDamping = 8.0f;
		component.servoMaximumTorque = 100000.0f;
		component.servoApplyReaction = false;
		component.vortexAxis = {0.0f, 1.0f, 0.0f};
		component.vortexRadius = 10.0f;
		component.vortexAngularVelocity = 1.0f;
		component.vortexRadialInflowVelocity = 0.0f;
		component.vortexAxialVelocity = 0.0f;
		component.vortexVelocityCoupling = 2.0f;
		component.vortexMaximumAcceleration = 100.0f;
		component.pressureFieldPressure = 1000.0f;
		component.pressureFieldRadius = 10.0f;
		component.pressureFieldFalloffExponent = 2.0f;
		component.pressureFieldMaximumForce = 1000000.0f;
		component.suspensionLocalAnchor = {0.0f, -0.5f, 0.0f};
		component.suspensionLocalDirection = {0.0f, -1.0f, 0.0f};
		component.suspensionRestLength = 0.8f;
		component.suspensionMaximumLength = 1.2f;
		component.suspensionWheelRadius = 0.3f;
		component.suspensionStiffness = 20000.0f;
		component.suspensionDamping = 2500.0f;
		component.suspensionMaximumForce = 100000.0f;
		component.suspensionUseHitNormal = false;
		component.suspensionApplyReaction = true;
		component.suspensionIsGrounded = false;
		component.suspensionCurrentLength = 1.2f;
		component.uprightLocalUpAxis = {0.0f, 1.0f, 0.0f};
		component.uprightTargetWorldUp = {0.0f, 1.0f, 0.0f};
		component.uprightStiffness = 100.0f;
		component.uprightDamping = 15.0f;
		component.uprightMaximumTorque = 100000.0f;
		component.electromagneticCharge = 0.0f;
		component.electromagneticMagneticMoment = {0.0f, 0.0f, 0.0f};
		component.electromagneticMaximumForce = 100000.0f;
		component.electromagneticMaximumTorque = 100000.0f;
		component.electromagneticFieldMode = 0;
		component.electromagneticElectricField = {0.0f, 0.0f, 0.0f};
		component.electromagneticMagneticField = {0.0f, 0.0f, 0.0f};
		component.electromagneticSourceCharge = 0.0f;
		component.electromagneticCoulombConstant = 8.98755179e9f;
		component.electromagneticMinimumDistance = 0.1f;
		component.electromagneticInfluenceRadius = 0.0f;
		component.healthMaximum = 100.0f;
		component.healthCurrent = 100.0f;
		component.enemySpawnFollowerGameObjectId = -1;
		component.enemySpawnNormalized = 0.0f;
		component.enemyAttackTargetGameObjectId = -1;
		component.enemyAttackInterval = 2.0f;
		component.enemyAttackRange = 40.0f;
		component.enemyAttackDamage = 10.0f;
		component.enemyProjectileTemplateGameObjectId = -1;
		component.enemyProjectilePoolSize = 8;
		component.enemyProjectileSpeed = 20.0f;
		component.enemyProjectileHitRadius = 0.5f;
		component.enemyProjectileLifetime = 5.0f;
		component.enemyWaveIndex = 0;
		component.enemyFormationPattern = 0;
		component.enemyFormationSlot = 0;
		component.enemyFormationSpacing = 3.0f;
		component.railShipSpeedSourceGameObjectId = -1;
		component.railShipSailGameObjectId = -1;
		component.railShipWakeEffectGameObjectId = -1;
		component.railShipWindEffectGameObjectId = -1;
		component.railShipEffectStartSpeed = 0.5f;
		component.railShipEffectFullSpeed = 12.0f;
		component.railShipSailMinimumSpeed = 0.35f;
		component.railShipSailMaximumSpeed = 1.5f;
		component.railAimMouseSensitivity = 0.0025f;
		component.railAimGamepadSensitivity = 1.5f;
		component.railAimAssistRadius = 0.18f;
		component.railAimInvertY = false;
		component.enemyMotionPattern = 0;
		component.enemyMotionAmplitude = {2.0f, 1.0f, 0.0f};
		component.enemyMotionFrequency = 0.5f;
		component.enemyMotionPhase = 0.0f;
		component.enemyMotionTargetGameObjectId = -1;
		component.enemyMotionSpeed = 5.0f;
		component.enemyMotionLookAtTarget = true;
		component.stageFollowerGameObjectId = -1;
		component.stageStartMarkerGameObjectId = -1;
		component.stageGoalMarkerGameObjectId = -1;
		component.stageStartEffectGameObjectId = -1;
		component.stageGoalEffectGameObjectId = -1;
		component.stageStartDelay = 1.0f;
		component.stageGoalRadius = 2.0f;
		component.stageGoalDelay = 2.0f;
		component.stageNextScenePath.clear();
		component.stageSelectScenePath.clear();
		component.sceneButtonScenePath.clear();
		component.railEventFollowerGameObjectId = -1;
		component.railEventNormalized = 0.0f;
		component.railEventType = 0;
		component.railEventTargetGameObjectId = -1;
		component.railEventDuration = 2.0f;
		component.railEventText = "Encounter Event";
		component.railEventPauseRail = false;
		component.bossPhaseTwoHealthRatio = 0.66f;
		component.bossPhaseThreeHealthRatio = 0.33f;
		component.bossPhaseOneMotionPattern = 0;
		component.bossPhaseTwoMotionPattern = 1;
		component.bossPhaseThreeMotionPattern = 4;
		component.bossPhaseOneAttackInterval = 2.0f;
		component.bossPhaseTwoAttackInterval = 1.25f;
		component.bossPhaseThreeAttackInterval = 0.65f;
		component.railHudBindingType = 0;
		component.railHudSourceGameObjectId = -1;
		component.waveTriggerMode = 0;
		component.waveTriggerSourceGameObjectId = -1;
		component.waveTriggerValue = 0.0f;
		component.waveSpawnInterval = 0.0f;
		component.waveSpawnMaximumPerFrame = 8;
		component.waveDeactivateChildrenOnStart = true;
		component.waveSpawnSourceMode = 0;
		component.wavePoolGameObjectId = -1;
		component.waveSpawnPointGameObjectId = -1;
		component.waveSpawnCount = 5;
		component.waveFormationPattern = 1;
		component.waveFormationSpacing = 4.0f;
		component.waveFormationColumns = 4;
		component.waveCompletionMode = 1;
		component.waveActionTargetGameObjectId = -1;
		component.waveStartedActionName = "OnWaveStarted";
		component.waveSpawnedActionName = "OnWaveSpawned";
		component.waveCompletedActionName = "OnWaveCompleted";
		component.waveAllDefeatedActionName = "OnWaveAllDefeated";
		component.waveSpawnRailStartNormalized = -1.0f;
		// 既定は0=従来どおり一括生成。既存Sceneの挙動を変えない。
		component.waveTargetAliveCount = 0;
		component.waveSpawnPointSetGameObjectId = -1;
		component.timelineSourceMode = 0;
		component.timelineSourceGameObjectId = -1;
		component.timelineTriggerValue = 0.0f;
		component.timelineTargetGameObjectId = -1;
		component.timelineActionName = "OnTimelineEvent";
		component.timelineTriggerOnce = true;
		component.thresholdSourceMode = 0;
		component.thresholdSourceGameObjectId = -1;
		component.thresholdTargetGameObjectId = -1;
		component.thresholdSecondValue = 0.66f;
		component.thresholdThirdValue = 0.33f;
		component.thresholdFirstActionName = "OnState1";
		component.thresholdSecondActionName = "OnState2";
		component.thresholdThirdActionName = "OnState3";
		component.uiBindingSourceGameObjectId = -1;
		component.uiBindingValueType = 1;
		component.uiBindingPrefix.clear();
		component.uiBindingPrecision = 0;
		component.uiBindingScale = 100.0f;
		component.buttonLabel = "Button";
		component.buttonPosition = {20.0f, 20.0f};
		component.buttonSize = {160.0f, 48.0f};
		component.buttonInteractable = true;
		component.buttonHoverColor = {0.25f, 0.45f, 0.80f};
		component.buttonPressedColor = {0.15f, 0.30f, 0.60f};
		component.buttonOnClickFunction = "OnClick";
		component.toggleValue = false;
		component.toggleOnValueChangedFunction = "OnValueChanged";
		component.sliderValue = 0.5f;
		component.sliderMinValue = 0.0f;
		component.sliderMaxValue = 1.0f;
		component.sliderOnValueChangedFunction = "OnValueChanged";
		component.scriptProperties.clear();
		component.inputEventBindings.clear();
		AddDefaultInputEventBindings(component);

	if (type == EditorComponentType::PostProcess) {
		component.bloomIntensity = 1.0f;
		component.bloomThreshold = 1.0f;
		component.bloomSoftKnee = 0.5f;
		component.bloomScatter = 0.72f;
		component.aaMode = 2;
		component.smaaThreshold = 0.10f;
		component.smaaCornerRounding = 25.0f;
		component.temporalSharpness = 0.08f;
		component.temporalBlendRatio = 0.90f;
		component.glareMode = 1;
		component.glareModeMask = 1 << 1;
		component.glareIntensity = 0.35f;
		component.glareSize = 1.0f;
		component.glareAngle = 0.0f;
		component.glareStreakCount = 4;
		component.glareFade = 0.85f;
		component.glareColorModulation = 0.15f;
		component.glareCenter = {0.5f, 0.5f, 0.0f};
		component.filterMode = 0;
		component.filterModeMask = 0;
		component.filterStrength = 1.0f;
		ApplyPostProcessEffectDefaults(component);
		component.finalBrightness = 1.0f;
		component.smaaEnabled = true;
		component.taaEnabled = false;
		component.ssrEnabled = false;
		component.compositeExposure = 1.0f;
		component.compositeWhitePoint = 3.0f;
		component.compositeToneMappingMode = 4;
		component.compositeBloomIntensity = 0.22f;
		component.compositeSaturation = 1.02f;
		component.compositeContrast = 1.04f;
		component.compositeVignetteStrength = 0.0f;
		component.compositeVignetteRadius = 0.97f;
		component.compositeFilmGrain = 0.015f;
		component.compositeChromaticAberration = 0.01f;
		component.compositeAmbientOcclusionStrength = 0.55f;
		component.compositeAutoExposureEnabled = true;
		component.compositeMinimumExposure = 0.80f;
		component.compositeMaximumExposure = 1.25f;
		component.compositeExposureAdaptationSpeed = 2.0f;
		component.compositeTargetLuminance = 0.18f;
		component.compositeTemperature = 0.0f;
		component.compositeTint = 0.0f;
		component.compositeLift = {0.0f, 0.0f, 0.0f};
		component.compositeGamma = 1.0f;
		component.compositeGain = {1.0f, 1.0f, 1.0f};
		component.compositeLocalContrast = 0.14f;
		component.compositeOutputDither = 0.65f;
		component.compositeSsgiEnabled = false;
		component.compositeSsgiIntensity = 0.30f;
		component.compositeSsgiRadiusPixels = 14.0f;
		component.compositeColorLutAssetPath.clear();
		component.compositeColorLutStrength = 1.0f;
	}

	if (type == EditorComponentType::Environment) {
		component.color = {0.4f, 0.6f, 1.0f};
		component.intensity = 1.0f;
		component.emissionStrength = 0.0f;
		component.roughness = 0.05f;
		component.reflectionStrength = 1.0f;
		component.metallic = 0.3f;
		component.skyLowerColor = {0.4f, 0.4f, 0.4f};
		component.environmentTextureEnabled = false;
		component.environmentTextureRotation = 0.0f;
		component.environmentTextureMipBias = 0.0f;
		component.volumetricCloudEnabled = false;
		component.volumetricCloudCoverage = 0.52f;
		component.volumetricCloudDensity = 1.15f;
		component.volumetricCloudScale = 0.0018f;
		component.volumetricCloudSpeed = 8.0f;
		component.volumetricCloudHeight = 900.0f;
		component.volumetricCloudThickness = 650.0f;
		component.volumetricCloudLightAbsorption = 1.25f;
		component.volumetricCloudSilverLining = 0.75f;
		component.volumetricCloudColor = {0.92f, 0.96f, 1.0f};
		component.environmentHeatIntensity = 0.0f;
		component.environmentHeatHorizonCenter = 0.46f;
		component.environmentHeatHorizonWidth = 0.16f;
		component.environmentHeatSunInfluence = 0.55f;
		component.environmentHeatDistortionScale = 0.65f;
	}

	if (type == EditorComponentType::Camera) {
		component.cameraFieldOfView = 60.0f;
		component.cameraNearClip = 0.3f;
		component.cameraFarClip = 1000.0f;
		component.cameraProjectionMode = 0;
		component.cameraDofEnabled = false;
		component.cameraDofFocusDistance = 10.0f;
		component.cameraDofAperture = 0.1f;
		component.cameraDofFocalLength = 50.0f;
		component.cameraMotionBlurEnabled = false;
		component.cameraMotionBlurIntensity = 0.5f;
		component.cameraExposure = 0.0f;
	}

	if (type == EditorComponentType::Ocean) {
		component.color = component.oceanShallowColor;
		component.roughness = component.oceanRoughness;
		component.ior = 1.333f;
		component.reflectionStrength = component.oceanReflectionStrength;
		component.clearCoat = 1.0f;
		component.clearCoatRoughness = 0.05f;
		component.transmission = 0.18f;
		component.lightingMode = 3;
	}

	if (type == EditorComponentType::Terrain) {
		component.colliderSize = {100.0f, 20.0f, 100.0f};
		component.oceanGridResolution = 128;
	}

	if (type == EditorComponentType::Foliage) {
		component.colliderSize = {60.0f, 1.0f, 60.0f};
		component.intensity = 1.0f;
		component.particleMaxCount = 4096;
		component.colliderRadius = 120.0f;
		component.oceanWaveHeight = 0.18f;
		component.oceanWindSpeed = 14.0f;
		component.oceanWaveLength = 4.0f;
	}

	if (type == EditorComponentType::ReflectionProbe) {
		component.assetPath = "ScreenSpace";  // 反射コンポーネントは既定で既存 SSR を使う。
		component.intensity = 1.0f;
		component.roughness = 0.0f;
		component.colliderSize = {5.0f, 5.0f, 5.0f};
	}

	if (type == EditorComponentType::BoxCollider ||
	    type == EditorComponentType::BoxCollider2D) {
		// BoxCollider 系の初期サイズ
		component.colliderSize = {1.0f, 1.0f, 1.0f};
	}
	else if (type == EditorComponentType::SphereCollider ||
	         type == EditorComponentType::CircleCollider2D) {
		component.colliderRadius = 0.5f;  // 球 / 円 Collider の初期半径
	}
	else if (type == EditorComponentType::CapsuleCollider ||
	         type == EditorComponentType::CapsuleCollider2D ||
	         type == EditorComponentType::CharacterController) {
		component.colliderRadius = 0.5f;  // Capsule / CharacterController は半径と高さを既存フィールドで表す
		component.colliderSize = {1.0f, 2.0f, 1.0f};
	}
	else if (type == EditorComponentType::WheelCollider ||
	         type == EditorComponentType::WheelJoint2D) {
		component.colliderRadius = 0.5f;  // 車輪系は半径を主な編集値にする
		component.bounciness = 0.2f;
	}
	else if (type == EditorComponentType::LineRenderer ||
	         type == EditorComponentType::TrailRenderer ||
	         type == EditorComponentType::ParticleSystem ||
	         type == EditorComponentType::VisualEffect) {
		component.intensity = 1.0f;  // Effect 系の見た目の強さ
	}
	else if (type == EditorComponentType::Light) {
		component.assetPath = "Point";  // Light は Blender 風に Point を既定値にする。
		component.intensity = 12.0f;  // 生成直後に白飛びしない、Scene 確認用の明るさ。
		component.colliderRadius = 12.0f;  // Point / Spot / Area の届く距離。
		component.colliderSize.x = 20.0f;  // Spot の内側角度。
		component.colliderSize.y = 35.0f;  // Spot の外側角度。
		component.colliderSize.z = 2.0f;  // Area の半径相当。
		component.sunAzimuthDegrees = 45.0f;
		component.sunElevationDegrees = 55.0f;
		component.sunUseAzimuthElevation = false;  // 既定はTransform回転を使い、既存Sceneの見た目を変えない。
		component.sunTemperatureKelvin = 5500.0f;  // 昼光相当。
		component.sunUseColorTemperature = false;  // 既定はcolorフィールドをそのまま使う。
		component.sunAutoTemperatureFromElevation = false;
	}
	else if (type == EditorComponentType::NavigationAgent) {
		component.navAgentRadius = 0.5f;
		component.navAgentHeight = 2.0f;
		component.navMaxSpeed = 3.5f;
		component.navMaxAcceleration = 8.0f;
		component.navStoppingDistance = 0.5f;
		component.navAutoRepath = true;
	}
	else if (type == EditorComponentType::NavMeshObstacle) {
		component.navCarve = true;
		component.colliderRadius = 0.5f;
		component.colliderSize = {1.0f, 2.0f, 1.0f};
	}
	else if (type == EditorComponentType::NavMeshSurface) {
		component.navAgentRadius = 0.5f;
		component.navAgentHeight = 2.0f;
		component.navMaxSlope = 45.0f;
		component.navMaxClimb = 0.5f;
		component.colliderSize = {20.0f, 0.2f, 20.0f};
	}
	else if (type == EditorComponentType::NavMeshModifier) {
		component.navAreaOverride = false;
		component.navArea = 0;
		component.navIgnoreFromBuild = false;
	}
	else if (type == EditorComponentType::NavMeshModifierVolume) {
		component.navArea = 0;
		component.colliderSize = {4.0f, 2.0f, 4.0f};
	}
	else if (type == EditorComponentType::NavMeshLink) {
		component.navBidirectional = true;
		component.navCostModifier = 1.0f;
		component.colliderRadius = 0.5f;
	}
	else if (type == EditorComponentType::AIBehaviorTree ||
	         type == EditorComponentType::AIStateMachine ||
	         type == EditorComponentType::AIGoapPlanner ||
	         type == EditorComponentType::AIHtnPlanner ||
	         type == EditorComponentType::AIPathfindingAgent ||
	         type == EditorComponentType::AIRecastCrowdAgent ||
	         type == EditorComponentType::AISteeringAgent ||
	         type == EditorComponentType::AISeekSteering ||
	         type == EditorComponentType::AIFleeSteering ||
	         type == EditorComponentType::AIArriveSteering ||
	         type == EditorComponentType::AIPursuitSteering ||
	         type == EditorComponentType::AIWanderSteering ||
	         type == EditorComponentType::AIObstacleAvoidanceSteering ||
	         type == EditorComponentType::AIFlockSteering) {
		component.navMaxSpeed = 3.0f;
		component.navMaxAcceleration = 8.0f;
		component.navStoppingDistance = 0.5f;
		component.navAgentRadius = 0.5f;
		component.inputBehavior = 0;
		component.colliderRadius = 5.0f;
		if (type == EditorComponentType::AIFleeSteering) {
			component.inputBehavior = 1;
		}
		else if (type == EditorComponentType::AIWanderSteering) {
			component.inputBehavior = 2;
		}
	}
	else if (type == EditorComponentType::AIVisionSensor ||
	         type == EditorComponentType::AIOpenCvCamera ||
	         type == EditorComponentType::AIOpenCvObjectDetector ||
	         type == EditorComponentType::AIOpenCvColorTracker ||
	         type == EditorComponentType::AIMotionSensor ||
	         type == EditorComponentType::AIWhisperSpeechRecognizer ||
	         type == EditorComponentType::AIVoiceCommand) {
		component.colliderRadius = 8.0f;
		component.colliderSize = {90.0f, 0.0f, 0.0f};
	}
	else if (type == EditorComponentType::AIBehaviorBlackboard ||
	         type == EditorComponentType::AIBehaviorSelector ||
	         type == EditorComponentType::AIBehaviorSequence ||
	         type == EditorComponentType::AIBehaviorTask ||
	         type == EditorComponentType::AIBehaviorDecorator ||
	         type == EditorComponentType::AIState ||
	         type == EditorComponentType::AIStateTransition ||
	         type == EditorComponentType::AIGoapGoal ||
	         type == EditorComponentType::AIGoapAction ||
	         type == EditorComponentType::AIGoapWorldState ||
	         type == EditorComponentType::AIHtnDomain ||
	         type == EditorComponentType::AIHtnTask ||
	         type == EditorComponentType::AIHtnMethod ||
	         type == EditorComponentType::AIMicroPatherGrid ||
	         type == EditorComponentType::AIRecastNavMeshBuilder ||
	         type == EditorComponentType::AIPathRequest ||
	         type == EditorComponentType::AIDynamicObstacle) {
		component.navAgentRadius = 0.5f;
		component.colliderRadius = 1.0f;
		component.colliderSize = {1.0f, 1.0f, 1.0f};
	}
	else if (type == EditorComponentType::LocalMove) {
		component.velocity = {1.0f, 0.0f, 0.0f};
		component.inputMoveSpeed = 1.0f;
	}
	else if (type == EditorComponentType::RollingMove) {
		component.velocity = {0.0f, 0.0f, 1.0f};
		component.rollingTorque = 50.0f;
		component.rollingHorsepower = 5.0f;
		component.colliderRadius = 0.5f;
	}
	else if (type == EditorComponentType::ParentConstraint ||
	         type == EditorComponentType::PositionConstraint ||
	         type == EditorComponentType::RotationConstraint) {
		component.constraintWeight = 1.0f;
		component.constraintPositionOffset = {0.0f, 0.0f, 0.0f};
		component.constraintRotationOffset = {0.0f, 0.0f, 0.0f};
	}
	else if (type == EditorComponentType::ScaleConstraint) {
		component.constraintWeight = 1.0f;
		component.constraintFreezeAxisX = false;
		component.constraintFreezeAxisY = false;
		component.constraintFreezeAxisZ = false;
	}
	else if (type == EditorComponentType::AimConstraint) {
		component.constraintWeight = 1.0f;
		component.constraintAimAxis = 2;
	}
	else if (type == EditorComponentType::LookAtConstraint) {
		component.constraintWeight = 1.0f;
		component.constraintUpAxis = 1;
		component.constraintRoll = 0.0f;
	}
	else if (type == EditorComponentType::Animation) {
		component.animationSpeed = 1.0f;
		component.animationLoop = true;
		component.animationPlayOnAwake = true;
		component.animationType = 0;
		component.animationAmplitude = 0.5f;
		component.animationClipIndex = 0;
	}
	else if (type == EditorComponentType::Animator) {
		component.animationSpeed = 1.0f;
		component.animatorState = 0;
		component.animatorAutoVelocity = true;
		component.animatorTransitionDuration = 0.15f;
	}
	else if (type == EditorComponentType::ParticleSystem) {
		component.particleRate = 10.0f;
		component.particleLifetime = 2.0f;
		component.particleSpeed = 5.0f;
		component.particleSize = 0.5f;
		component.particleMaxCount = 256;
		component.particleDuration = 5.0f;
		component.particleLooping = true;
		component.particleDirection = {0.0f, 1.0f, 0.0f};
		component.particleBoxSize = {1.0f, 1.0f, 1.0f};
		component.particleShapeRadius = 1.0f;
		component.particleShapeAngle = 25.0f;
		component.particleEndColor = component.color;
	}
	else if (type == EditorComponentType::VisualEffect) {
		component.particleRate = 24.0f;
		component.particleLifetime = 1.5f;
		component.particleSpeed = 3.0f;
		component.particleSize = 0.25f;
		component.particleEndSize = 0.0f;
		component.particleMaxCount = 512;
		component.particleBurstCount = 16;
		component.particleDuration = 2.0f;
		component.particleLooping = true;
		component.particleShape = 1;
		component.particleShapeRadius = 0.5f;
		component.particleEndColor = component.color;
	}

	//============================================================
	// 汎用ゲームプレイ基盤の既定値
	//============================================================

	component.cameraPriority = 0;
	component.cameraFollowPositionSpace = 0;
	component.cameraFollowRotationMode = 0;
	component.screenAimInputGameObjectId = -1;
	component.screenAimReticleGameObjectId = -1;
	component.screenAimInputMode = 0;
	component.screenAimActionMapName = "Player";
	component.screenAimActionName = "Aim";
	component.screenAimNormalizedPosition = {0.5f, 0.5f};
	component.screenAimSpeed = 0.75f;
	component.screenAimInvertY = false;
	component.screenAimClamp = true;
	component.hitscanAimGameObjectId = -1;
	component.hitscanInputGameObjectId = -1;
	component.hitscanActionMapName = "Player";
	component.hitscanFireActionName = "Fire";
	component.hitscanRange = 1000.0f;
	component.hitscanDamage = 10.0f;
	component.hitscanDamageTag = "Bullet";
	component.hitscanInterval = 0.15f;
	component.hitscanAutomatic = false;
	component.hitscanOceanCollision = true;
	component.hitscanActionTargetGameObjectId = -1;
	component.hitscanFiredActionName = "OnWeaponFired";
	component.hitscanHitActionName = "OnWeaponHit";
	component.hitscanMissActionName = "OnWeaponMiss";
	component.projectileAimGameObjectId = -1;
	component.projectileInputGameObjectId = -1;
	component.projectilePoolGameObjectId = -1;
	component.projectileSpawnPointGameObjectId = -1;
	component.projectileActionMapName = "Player";
	component.projectileFireActionName = "Fire";
	component.projectileSpeed = 40.0f;
	component.projectileDamage = 10.0f;
	component.projectileDamageTag = "Projectile";
	component.projectileRadius = 0.15f;
	component.projectileSpawnClearance = 0.05f;
	component.projectileLifetime = 5.0f;
	component.projectileInterval = 0.25f;
	component.projectileAutomatic = false;
	component.projectileOceanCollision = true;
	component.projectileAimMode = 0;
	component.projectileBallisticPredictionGameObjectId = -1;
	component.projectileInheritSourceVelocity = true;
	component.projectileSourceVelocityGameObjectId = -1;
	component.projectileUseParentRigidBody = true;
	component.projectileLinearVelocityInheritance = 1.0f;
	component.projectileAngularVelocityInheritance = 1.0f;
	component.projectileVariableSpeedMinimumFlightTime = 0.2f;
	component.projectileVariableSpeedMaximumFlightTime = 1.2f;
	component.projectileVariableSpeedDistanceFactor = 300.0f;
	component.projectileVariableSpeedTimeMode = 0;
	component.projectileVariableSpeedFixedFlightTime = 1.5f;
	component.projectileVariableSpeedTrajectoryMode = 0;
	component.projectileVariableSpeedDepressionAngleDegrees = 15.0f;
	component.projectileVariableSpeedArcHeight = 5.0f;
	component.projectileTracerStretchEnabled = false;
	component.projectileTracerLengthScale = 1.0f;
	component.projectileTracerMinimumLength = 2.0f;
	component.projectileTracerThickness = 0.15f;
	component.projectileHitscanResolution = false;
	component.projectileActionTargetGameObjectId = -1;
	component.projectileFiredActionName = "OnProjectileFired";
	component.projectileHitActionName = "OnProjectileHit";
	component.damageMultiplier = 1.0f;
	component.damageInvulnerabilitySeconds = 0.0f;
	component.damageDeactivateOnDeath = true;
	component.damageActionTargetGameObjectId = -1;
	component.damagedActionName = "OnDamaged";
	component.deathActionName = "OnDeath";
	component.objectPoolTemplateGameObjectId = -1;
	component.objectPoolInitialSize = 16;
	component.objectPoolAllowExpand = false;
	component.prefabSpawnerPoolGameObjectId = -1;
	component.prefabSpawnerPointGameObjectId = -1;
	component.prefabSpawnerMode = 0;
	component.prefabSpawnerInterval = 1.0f;
	component.prefabSpawnerActionTargetGameObjectId = -1;
	component.prefabSpawnerSpawnedActionName = "OnSpawned";
	component.cameraBlendSourceGameObjectId = -1;
	component.cameraBlendTargetGameObjectId = -1;
	component.cameraBlendDuration = 1.0f;
	component.cameraBlendEasing = 1;
	component.cameraBlendPlayOnStart = false;
	component.cameraShakePositionAmplitude = {0.1f, 0.1f, 0.1f};
	component.cameraShakeRotationAmplitude = {0.01f, 0.01f, 0.01f};
	component.cameraShakeFrequency = 12.0f;
	component.cameraShakeDuration = 0.35f;
	component.cameraShakePlayOnStart = false;
	component.cameraShakePriority = 0;
	component.railBranchFollowerGameObjectId = -1;
	component.railBranchTargetPathGameObjectId = -1;
	component.railBranchTriggerMode = 0;
	component.railBranchTriggerNormalized = 0.5f;
	component.railBranchPreserveProgress = false;
	component.railBranchTriggerOnce = true;
	component.railBranchActionTargetGameObjectId = -1;
	component.railBranchActionName = "OnRailBranched";
	component.actionSequencePlayOnStart = false;
	component.actionSequenceLoop = false;
	component.actionSequenceStepType = 0;
	component.actionSequenceParallelGroup = -1;
	component.actionSequenceTargetGameObjectId = -1;
	component.actionSequenceActionName = "OnSequenceAction";
	component.actionSequenceWaitSeconds = 1.0f;
	component.actionSequenceActiveValue = true;
	component.actionSequenceScenePath = "";
	component.actionSequenceSceneAdditive = false;
	component.actionSequenceConditionMode = 0;
	component.actionSequenceCompareMode = 0;
	component.actionSequenceCompareValue = 1.0f;
	component.actionSequenceTrueStepIndex = -1;
	component.actionSequenceFalseStepIndex = -1;
	component.saveableKey = "";
	component.saveableTransform = true;
	component.saveableActive = true;
	component.saveableHealth = true;
	component.saveableRigidbody = true;
	component.saveableScriptProperties = true;
	component.checkpointSlotName = "autosave";
	component.checkpointSaveOnStart = false;
	component.checkpointLoadOnStart = false;
	component.checkpointActionTargetGameObjectId = -1;
	component.checkpointSavedActionName = "OnCheckpointSaved";
	component.checkpointLoadedActionName = "OnCheckpointLoaded";
	component.weaponLoadoutSelectedSlotIndex = 0;
	component.weaponLoadoutActionTargetGameObjectId = -1;
	component.weaponLoadoutChangedActionName = "OnWeaponChanged";
	component.weaponLoadoutReloadedActionName = "OnWeaponReloaded";
	component.weaponSlotName = "Weapon";
	component.weaponSlotWeaponGameObjectId = -1;
	component.weaponSlotVisualGameObjectId = -1;
	component.weaponSlotCurrentAmmo = 30;
	component.weaponSlotReserveAmmo = 90;
	component.weaponSlotMaximumAmmo = 30;
	component.weaponSlotReloadSeconds = 1.5f;
	component.weaponSlotAutoReload = true;
	component.targetSelectorSearchLayer = -1;
	component.targetSelectorMaximumDistance = 100.0f;
	component.targetSelectorMaximumAngle = 45.0f;
	component.targetSelectorReferenceGameObjectId = -1;
	component.targetSelectorOcclusionCheck = true;
	component.targetSelectorOcclusionMode = 3;
	component.targetSelectorOceanClearance = 0.0f;
	component.targetSelectorMaximumTargets = 16;
	component.targetSelectorSelectionMode = 1;
	component.targetSelectorCurrentTargetGameObjectId = -1;
	component.targetSelectorActionTargetGameObjectId = -1;
	component.targetSelectorFoundActionName = "OnTargetFound";
	component.targetSelectorLostActionName = "OnTargetLost";
	component.targetSelectorChangedActionName = "OnTargetChanged";
	component.targetSteeringTargetGameObjectId = -1;
	component.targetSteeringSelectorGameObjectId = -1;
	component.targetSteeringTurnSpeed = 180.0f;
	component.targetSteeringAcceleration = 20.0f;
	component.targetSteeringMaximumSpeed = 40.0f;
	component.targetSteeringStartDelay = 0.0f;
	component.targetSteeringPredictionSeconds = 0.0f;
	component.targetSteeringMode = 0;
	// 既定は従来どおりのDirect(Targetへ真っすぐ旋回・前進)。
	// 既存SceneのTargetSteeringは拡張行が無ければこの値のまま読み込まれるため挙動は変わらない。
	component.targetSteeringMoveMode = 0;
	component.targetSteeringSideOffset = 25.0f;
	component.targetSteeringForwardOffset = 0.0f;
	component.targetSteeringVerticalOffset = 0.0f;
	component.targetSteeringTargetDistance = 120.0f;
	component.targetSteeringDistanceMargin = 15.0f;
	component.targetSteeringDuration = 0.0f;
	component.targetSteeringNextMoveMode = -1;
	component.targetSteeringStartOffset = {80.0f, 0.0f, 50.0f};
	component.targetSteeringEndOffset = {-80.0f, 0.0f, 20.0f};
	component.targetSteeringPositionLerpSpeed = 0.0f;
	component.targetSteeringActionTargetGameObjectId = -1;
	component.targetSteeringCompletedActionName = "";
	component.movementModifierLocalPositionOffset = {0.0f, 0.0f, 0.0f};
	component.movementModifierLocalRotationOffset = {0.0f, 0.0f, 0.0f};
	component.movementModifierAxisMask = 7;
	component.movementModifierInputRange = {0.0f, 0.0f};
	component.movementModifierInputSpeed = 8.0f;
	component.movementModifierInputGameObjectId = -1;
	component.movementModifierActionMapName = "Player";
	component.movementModifierActionName = "Move";
	component.propertyTweenTargetGameObjectId = -1;
	component.propertyTweenComponentName = "Ocean";
	component.propertyTweenPropertyName = "WaveHeight";
	component.propertyTweenStartValue = {1.0f, 0.0f, 0.0f};
	component.propertyTweenEndValue = {4.0f, 0.0f, 0.0f};
	component.propertyTweenValueType = 0;
	component.propertyTweenDuration = 1.0f;
	component.propertyTweenCurve = 1;
	component.propertyTweenPlayOnStart = false;
	component.propertyTweenLoop = false;
	component.propertyTweenActionTargetGameObjectId = -1;
	component.propertyTweenCompletedActionName = "OnTweenCompleted";
	component.actionRelayOnStart = false;
	component.actionRelayTargetGameObjectId = -1;
	component.actionRelayActionName = "OnAction";
	component.actionRelayTargetEnabled = true;
	component.targetPointPriority = 0.0f;
	component.targetPointRadius = 0.25f;
	component.targetPointAimOffset = {0.0f, 0.0f, 0.0f};
	component.teamId = -1;
	component.teamTargetable = true;
	component.targetSelectorTeamFilter = 0;
	component.targetSelectorSpecificTeamId = 0;
	component.targetSelectorIncludeNeutral = true;
	component.timerDuration = 1.0f;
	component.timerRepeat = false;
	component.timerPlayOnStart = true;
	component.timerActionTargetGameObjectId = -1;
	component.timerActionName = "OnTimer";
	component.timerRemaining = 1.0f;
	component.timerPaused = false;
	component.stateMachineInitialState = "Initial";
	component.stateMachineCurrentState = "Initial";
	component.stateMachineActionTargetGameObjectId = -1;
	component.stateMachineChangedActionName = "OnStateChanged";
	component.attributeName = "Resource";
	component.attributeMinimum = 0.0f;
	component.attributeMaximum = 100.0f;
	component.attributeCurrent = 100.0f;
	component.attributeRegenerationPerSecond = 0.0f;
	component.attributeActionTargetGameObjectId = -1;
	component.attributeChangedActionName = "OnAttributeChanged";
	component.destructibleHealthGameObjectId = -1;
	component.destructibleDisableComponentNames.clear();
	component.destructibleDisableChildren = true;
	component.destructibleActionTargetGameObjectId = -1;
	component.destructibleDestroyedActionName = "OnPartDestroyed";
	component.destructibleDestroyed = false;
	component.formationLeaderGameObjectId = -1;
	component.formationLocalOffset = {};
	component.formationPositionSpeed = 8.0f;
	component.formationRotationSpeed = 180.0f;
	component.formationFollowRotation = true;
	component.targetLockSelectorGameObjectId = -1;
	component.targetLockSeconds = 0.75f;
	component.targetLockLostGraceSeconds = 0.25f;
	component.targetLockProgress = 0.0f;
	component.targetLockLocked = false;
	component.targetLockCurrentGameObjectId = -1;
	component.targetLockActionTargetGameObjectId = -1;
	component.targetLockStartedActionName = "OnLockStarted";
	component.targetLockCompletedActionName = "OnLockCompleted";
	component.targetLockLostActionName = "OnLockLost";
	component.multiTargetLockSelectorGameObjectId = -1;
	component.multiTargetLockMaximumCount = 8;
	component.multiTargetLockSecondsPerTarget = 0.35f;
	component.multiTargetLockLostGraceSeconds = 0.25f;
	component.multiTargetLockAutoAcquire = true;
	component.multiTargetLockActionTargetGameObjectId = -1;
	component.multiTargetLockAddedActionName = "OnMultiTargetAdded";
	component.multiTargetLockCompletedActionName = "OnMultiTargetLocked";
	component.multiTargetLockLostActionName = "OnMultiTargetLost";
	component.targetMarkerTargetGameObjectId = -1;
	component.targetMarkerSelectorGameObjectId = -1;
	component.targetMarkerLockGameObjectId = -1;
	component.targetMarkerMultiLockIndex = 0;
	component.targetMarkerWorldOffset = {0.0f, 0.0f, 0.0f};
	component.targetMarkerScreenOffset = {0.0f, 0.0f};
	component.targetMarkerEdgePadding = 32.0f;
	component.targetMarkerHideBehindCamera = true;
	component.targetMarkerOnlyWhenLocked = false;
	component.targetMarkerRotateToDirection = true;
	component.attributeSetActionTargetGameObjectId = -1;
	component.attributeSetChangedActionName = "OnAttributeSetChanged";
	component.counterName = "Counter";
	component.counterInitialValue = 0.0f;
	component.counterCurrentValue = 0.0f;
	component.counterMinimumValue = 0.0f;
	component.counterMaximumValue = 999999.0f;
	component.counterThresholdValue = 1.0f;
	component.counterCompareMode = 0;
	component.counterFireOnce = true;
	component.counterWasSatisfied = false;
	component.counterActionTargetGameObjectId = -1;
	component.counterChangedActionName = "OnCounterChanged";
	component.counterThresholdActionName = "OnCounterThreshold";
	component.conditionSourceGameObjectId = -1;
	component.conditionSourceType = 0;
	component.conditionCompareMode = 0;
	component.conditionCompareFloat = 0.0f;
	component.conditionEvaluateEveryFrame = false;
	component.conditionFireOnChangeOnly = true;
	component.conditionLastResult = false;
	component.conditionActionTargetGameObjectId = -1;
	component.conditionTrueActionName = "OnConditionTrue";
	component.conditionFalseActionName = "OnConditionFalse";
	component.areaDamageRadius = 5.0f;
	component.areaDamageBaseDamage = 50.0f;
	component.areaDamageMinimumMultiplier = 0.0f;
	component.areaDamageImpulse = 0.0f;
	component.areaDamageFalloffMode = 1;
	component.areaDamageLayerMask = -1;
	component.areaDamageTag = "Explosion";
	component.areaDamageIgnoreOwner = true;
	component.areaDamagePlayOnStart = false;
	component.areaDamageActionTargetGameObjectId = -1;
	component.areaDamageAppliedActionName = "OnAreaDamageApplied";
	component.areaDamageOcclusionMode = 0;
	component.areaDamageOcclusionLayerMask = -1;
	component.areaDamageBlockedMultiplier = 0.0f;
	component.areaDamageOcclusionSamplePoints = 1;
	component.areaDamageTeamRule = 0;
	component.areaDamageIgnoreNeutral = false;
	component.areaDamageTeamSourceGameObjectId = -1;
	component.hitZoneHealthGameObjectId = -1;
	component.hitZoneDamageMultiplier = 1.0f;
	component.damageTagDefaultMultiplier = 1.0f;
	component.projectileDetonateOnContact = true;
	component.projectileDetonateOnProximity = false;
	component.projectileDetonateOnLifetime = false;
	component.projectileDetonatorTargetGameObjectId = -1;
	component.projectileDetonatorProximityRadius = 1.0f;
	component.projectileDetonatorAreaDamageGameObjectId = -1;
	component.projectileDetonatorActionTargetGameObjectId = -1;
	component.projectileDetonatedActionName = "OnProjectileDetonated";
	component.threatTrackerTargetGameObjectId = -1;
	component.threatTrackerMaximumDistance = 200.0f;
	component.threatTrackerMinimumClosingSpeed = 1.0f;
	component.threatTrackerMaximumMissDistance = 10.0f;
	component.threatTrackerMaximumCount = 8;
	component.threatTrackerActionTargetGameObjectId = -1;
	component.threatTrackerAddedActionName = "OnThreatAdded";
	component.threatTrackerLostActionName = "OnThreatLost";
	component.runtimeResetHealth = true;
	component.runtimeResetStateMachine = true;
	component.runtimeResetAttributes = true;
	component.runtimeResetLocks = true;
	component.runtimeResetTimers = true;
	component.runtimeResetDestructibleParts = true;
	component.runtimeResetCooldowns = true;
	component.runtimeResetActionTargetGameObjectId = -1;
	component.runtimeResetActionName = "OnRuntimeStateReset";
	component.cooldownSetActionTargetGameObjectId = -1;
	component.cooldownSetCompletedActionName = "OnCooldownCompleted";
	component.weaponFirePatternMode = 0;
	component.weaponFirePatternCount = 3;
	component.weaponFirePatternInterval = 0.1f;
	component.weaponFirePatternSpreadAngle = 8.0f;
	component.weaponFirePatternChargeSeconds = 0.75f;
	component.weaponFirePatternActionTargetGameObjectId = -1;
	component.weaponFirePatternCompletedActionName = "OnFirePatternCompleted";
	component.targetAssignmentMultiTargetLockGameObjectId = -1;
	component.targetAssignmentMaximumTargets = 8;
	component.targetAssignmentInterval = 0.1f;
	component.targetAssignmentLockedOnly = true;
	component.targetAssignmentActionTargetGameObjectId = -1;
	component.targetAssignmentCompletedActionName = "OnTargetSalvoCompleted";
	component.weaponAccuracyBaseSpread = 0.0f;
	component.weaponAccuracyMaximumSpread = 12.0f;
	component.weaponAccuracySpreadPerShot = 0.5f;
	component.weaponAccuracyRecoveryPerSecond = 3.0f;
	component.weaponAccuracyMovementSpread = 0.0f;
	component.weaponAccuracyDistribution = 2;
	component.weaponAccuracyCurrentSpread = 0.0f;
	component.weaponRecoilBodyImpulse = {0.0f, 0.0f, -1.0f};
	component.weaponRecoilBodyTorque = {0.0f, 0.0f, 0.0f};
	component.weaponRecoilVisualGameObjectId = -1;
	component.weaponRecoilVisualPosition = {0.0f, 0.0f, -0.1f};
	component.weaponRecoilVisualRotation = {0.0f, 0.0f, 0.0f};
	component.weaponRecoilRecoveryPerSecond = 8.0f;
	component.weaponRecoilCameraShakeGameObjectId = -1;
	component.weaponRecoilActionTargetGameObjectId = -1;
	component.weaponRecoilActionName = "OnWeaponRecoil";
	component.surfaceTypeTag = "Default";
	component.timeScaleValue = 0.0f;
	component.timeScaleDuration = 0.1f;
	component.timeScaleBlendSeconds = 0.0f;
	component.timeScalePlayOnStart = false;
	component.timeScaleActionTargetGameObjectId = -1;
	component.timeScaleCompletedActionName = "OnTimeScaleCompleted";
	component.aimAssistScreenAimGameObjectId = -1;
	component.aimAssistTargetSelectorGameObjectId = -1;
	component.aimAssistRadius = 0.12f;
	component.aimAssistStrength = 0.35f;
	component.aimAssistFollowSpeed = 8.0f;
	component.aimAssistInputSuppression = 0.5f;
	component.interceptTargetGameObjectId = -1;
	component.interceptTargetSelectorGameObjectId = -1;
	component.interceptProjectileSpeed = 100.0f;
	component.interceptMaximumTime = 10.0f;
	component.interceptPredictedPosition = {0.0f, 0.0f, 0.0f};
	component.interceptTime = 0.0f;
	component.interceptValid = false;
	component.damageDirectionDuration = 1.5f;
	component.damageDirectionFadeSeconds = 0.4f;
	component.damageDirectionMinimumDamage = 1.0f;
	component.damageDirectionEdgeRadius = 0.45f;
	component.damageDirectionSourceGameObjectId = -1;
	component.damageDirectionNormalized = {0.0f, -1.0f};
	component.damageDirectionRemaining = 0.0f;
	component.objectiveActionTargetGameObjectId = -1;
	component.objectiveChangedActionName = "OnObjectiveChanged";
	component.encounterPlayOnStart = false;
	component.encounterActionTargetGameObjectId = -1;
	component.encounterCompletedActionName = "OnEncounterCompleted";
	component.spawnPointSetMode = 0;
	component.spawnPointVolumeSize = {10.0f, 0.0f, 10.0f};
	component.spawnPointAvoidImmediateRepeat = true;
	component.difficultyNames = {"Easy", "Normal", "Hard"};
	component.difficultySelectedIndex = 1;
	component.difficultyApplyOnStart = false;
	component.difficultyActionTargetGameObjectId = -1;
	component.difficultyAppliedActionName = "OnDifficultyApplied";
	component.cameraFeedbackMaximumPosition = {1.0f, 1.0f, 1.0f};
	component.cameraFeedbackMaximumRotation = {0.2f, 0.2f, 0.2f};
	component.cameraFeedbackMaximumConcurrent = 8;
	component.cameraFeedbackMixMode = 0;
	component.cameraFeedbackGlobalStrength = 1.0f;
	component.ballisticTargetGameObjectId = -1;
	component.ballisticTargetSelectorGameObjectId = -1;
	component.ballisticInitialSpeed = 80.0f;
	component.ballisticGravity = {0.0f, -9.81f, 0.0f};
	component.ballisticDrag = 0.0f;
	component.ballisticTargetAcceleration = {0.0f, 0.0f, 0.0f};
	component.ballisticMaximumTime = 12.0f;
	component.ballisticSimulationStep = 1.0f / 60.0f;
	component.ballisticMaximumPoints = 128;
	component.ballisticValid = false;
	component.ballisticLaunchDirection = {0.0f, 0.0f, 1.0f};
	component.ballisticLaunchVelocity = {0.0f, 0.0f, 0.0f};
	component.ballisticSourceVelocity = {0.0f, 0.0f, 0.0f};
	component.ballisticImpactPosition = {0.0f, 0.0f, 0.0f};
	component.ballisticFlightTime = 0.0f;
	component.ballisticInheritSourceVelocity = true;
	component.ballisticSourceVelocityGameObjectId = -1;
	component.ballisticUseParentRigidBody = true;
	component.ballisticLinearVelocityInheritance = 1.0f;
	component.ballisticAngularVelocityInheritance = 1.0f;
	component.damageEventMaximumEntries = 8;
	component.damageEventLifetime = 1.5f;
	component.damageEventMinimumDamage = 1.0f;
	component.damageEventMergeSameSource = true;
	component.gamePausePaused = false;
	component.gamePausePauseGameTime = true;
	component.gamePausePausePhysics = true;
	component.gamePausePauseAudio = true;
	component.gamePauseGameplayInputMap = "Gameplay";
	component.gamePauseUiInputMap = "UI";
	component.gamePauseActionTargetGameObjectId = -1;
	component.gamePausePausedActionName = "OnGamePaused";
	component.gamePauseResumedActionName = "OnGameResumed";
	component.surfaceWakeOceanGameObjectId = -1;
	component.surfaceWakeLeftEffectGameObjectId = -1;
	component.surfaceWakeRightEffectGameObjectId = -1;
	component.surfaceWakeBowEffectGameObjectId = -1;
	component.surfaceWakeMinimumSpeed = 0.5f;
	component.surfaceWakeMaximumSpeed = 20.0f;
	component.surfaceWakeWidth = 1.5f;
	component.surfaceWakeLifetime = 4.0f;
	component.surfaceWakeMaximumEmissionRate = 80.0f;
	component.surfaceWakeCurrentSpeed = 0.0f;
	component.surfaceWakeCurrentIntensity = 0.0f;
	component.trajectoryPredictionGameObjectId = -1;
	component.trajectoryColor = {1.0f, 0.7f, 0.15f};
	component.trajectoryAlpha = 0.9f;
	component.trajectoryThickness = 2.0f;
	component.trajectoryMaximumPoints = 128;
	component.trajectoryShowInSceneView = true;
	component.trajectoryShowInGameView = true;
	component.trajectoryShowImpactPoint = true;
	component.waterSurfaceOceanGameObjectId = -1;
	component.waterSurfaceLocalOffset = {0.0f, 0.0f, 0.0f};
	component.waterSurfaceClearance = 0.0f;
	component.waterSurfaceState = 0;
	component.waterSurfaceSignedDistance = 0.0f;
	component.waterSurfaceCurrentOceanGameObjectId = -1;
	component.waterSurfacePosition = {0.0f, 0.0f, 0.0f};
	component.waterSurfaceNormal = {0.0f, 1.0f, 0.0f};
	component.waterSurfaceVelocity = {0.0f, 0.0f, 0.0f};
	component.waterSurfaceFoam = 0.0f;
	component.waterSurfaceActionTargetGameObjectId = -1;
	component.waterSurfaceEnteredActionName = "OnWaterEntered";
	component.waterSurfaceExitedActionName = "OnWaterExited";
	component.oceanProbeOceanGameObjectId = -1;
	component.oceanProbeLocalOriginOffset = {0.0f, 0.0f, 0.0f};
	component.oceanProbeLocalDirection = {0.0f, 0.0f, 1.0f};
	component.oceanProbeEntries = {{10.0f}, {25.0f}, {50.0f}};
	component.attackFilterInstigatorGameObjectId = -1;
	component.attackFilterIgnoreInstigator = true;
	component.attackFilterIgnoreInstigatorHierarchy = true;
	component.attackFilterTeamRule = 1;
	component.attackFilterIgnoreNeutral = false;
	component.attackFilterArmingDistance = 1.0f;
	component.attackFilterIgnoredGameObjectIds.clear();
	component.turretTargetGameObjectId = -1;
	component.turretTargetSelectorGameObjectId = -1;
	component.turretYawPivotGameObjectId = -1;
	component.turretPitchPivotGameObjectId = -1;
	component.turretYawMinimumDegrees = -180.0f;
	component.turretYawMaximumDegrees = 180.0f;
	component.turretPitchMinimumDegrees = -10.0f;
	component.turretPitchMaximumDegrees = 75.0f;
	component.turretYawSpeedDegrees = 90.0f;
	component.turretPitchSpeedDegrees = 60.0f;
	component.turretAimToleranceDegrees = 2.0f;
	component.turretPredictionSeconds = 0.0f;
	component.turretCurrentTargetGameObjectId = -1;
	component.turretCanReachTarget = false;
	component.turretIsAimed = false;
	component.turretYawErrorDegrees = 0.0f;
	component.turretPitchErrorDegrees = 0.0f;
	component.weaponGroupEntries.clear();
	component.weaponGroupMode = 0;
	component.weaponGroupInterval = 0.1f;
	component.weaponGroupRequireAllReady = true;
	component.weaponGroupActionTargetGameObjectId = -1;
	component.weaponGroupCompletedActionName = "OnWeaponGroupCompleted";
	component.weaponGroupRoundRobinIndex = 0;
	component.weaponGroupIsFiring = false;
	component.projectileImpactPenetrationEnergy = 0.0f;
	component.projectileImpactPenetrationLoss = 1.0f;
	component.projectileImpactMaximumPenetrations = 0;
	component.projectileImpactRicochetAngleDegrees = 75.0f;
	component.projectileImpactEnergyRetention = 0.65f;
	component.projectileImpactDamageRetention = 0.75f;
	component.projectileImpactMaximumRicochets = 0;
	component.projectileImpactSurfaceModifiers.clear();
	component.horizonSourceGameObjectId = -1;
	component.horizonLocalPositionOffset = {0.0f, 2.0f, -6.0f};
	component.horizonRotationOffsetDegrees = {0.0f, 0.0f, 0.0f};
	component.horizonFollowPosition = true;
	component.horizonPitchInheritance = 0.35f;
	component.horizonYawInheritance = 1.0f;
	component.horizonRollInheritance = 0.2f;
	component.horizonWorldUp = {0.0f, 1.0f, 0.0f};
	component.horizonDamping = 8.0f;
	component.horizonMaximumRollDegrees = 8.0f;
	component.fireLineMuzzleGameObjectId = -1;
	component.fireLineDirectionGameObjectId = -1;
	component.fireLineAllowedTargetGameObjectId = -1;
	component.fireLineDistance = 10.0f;
	component.fireLineRadius = 0.05f;
	component.fireLineLayerMask = -1;
	component.fireLineIgnoredGameObjectIds.clear();
	component.fireLineClear = true;
	component.fireLineBlockingGameObjectId = -1;
	component.fireLineBlockingDistance = 0.0f;
	component.statusEffectDefinitions.clear();
	component.statusEffectActionTargetGameObjectId = -1;
	component.statusEffectRuntimeEntries.clear();
	component.railSpeedKeys = {{0.0f, 1.0f}, {1.0f, 1.0f}};
	component.railSpeedProfileEnabled = true;
	component.railZoneEntries.clear();
	component.railZoneActionTargetGameObjectId = -1;
	component.railZoneActiveIndex = -1;
	component.cameraComposerTargetGameObjectId = -1;
	component.cameraComposerFollowOffset = {0.0f, 3.0f, -8.0f};
	component.cameraComposerLookAtOffset = {0.0f, 1.0f, 6.0f};
	component.cameraComposerPositionDamping = 6.0f;
	component.cameraComposerRotationDamping = 8.0f;
	component.cameraComposerLookAheadSeconds = 0.25f;
	component.cameraComposerDeadZone = {0.0f, 0.0f};
	component.cameraComposerMaximumDistance = 30.0f;
	component.cameraComposerInheritTargetYaw = true;
	component.cameraComposerStabilizePitchRoll = true;
	component.cameraComposerRuntimePosition = {0.0f, 0.0f, 0.0f};
	component.cameraComposerRuntimeRotation = {0.0f, 0.0f, 0.0f};
	component.cameraComposerRuntimeInitialized = false;
	component.speedFeedbackSourceGameObjectId = -1;
	component.speedFeedbackCameraGameObjectId = -1;
	component.speedFeedbackMinimumSpeed = 0.0f;
	component.speedFeedbackMaximumSpeed = 40.0f;
	component.speedFeedbackMinimumFovDegrees = 60.0f;
	component.speedFeedbackMaximumFovDegrees = 78.0f;
	component.speedFeedbackMinimumMotionBlur = 0.0f;
	component.speedFeedbackMaximumMotionBlur = 0.2f;
	component.speedFeedbackCameraStrength = 0.35f;
	component.speedFeedbackResponseSpeed = 5.0f;
	component.speedFeedbackNormalized = 0.0f;
	component.spawnedSetupRailPathGameObjectId = -1;
	component.spawnedSetupRailStartNormalized = 0.0f;
	component.spawnedSetupRailStartStep = 0.0f;
	component.spawnedSetupRailSpeedMultiplier = 1.0f;
	component.spawnedSetupOverrideTeam = false;
	component.spawnedSetupTeamId = 1;
	component.spawnedSetupResetRuntimeState = true;
	component.spawnedSetupActionTargetGameObjectId = -1;
	component.spawnedSetupAppliedActionName = "OnSpawnedObjectSetup";
	component.waveMotionMode = 0;
	component.waveMotionAmplitude = {6.0f, 2.0f};
	component.waveMotionFrequency = 0.35f;
	component.waveMotionPhaseStep = 0.7f;
	component.waveMotionBlendInSeconds = 0.5f;
	component.distanceActivationReferenceGameObjectId = -1;
	component.distanceActivationEnterDistance = 250.0f;
	component.distanceActivationExitDistance = 300.0f;
	component.distanceActivationAffectHierarchy = true;
	component.distanceActivationRuntimeActive = true;
	component.distanceActivationRuntimeInitialized = false;
	component.simulationLodReferenceGameObjectId = -1;
	component.simulationLodMediumDistance = 100.0f;
	component.simulationLodFarDistance = 250.0f;
	component.simulationLodCulledDistance = 500.0f;
	component.simulationLodMediumScriptInterval = 1.0f / 30.0f;
	component.simulationLodFarScriptInterval = 0.2f;
	component.simulationLodDisablePhysicsAtFar = true;
	component.simulationLodDisableScriptsAtFar = false;
	component.simulationLodDisableAiAtFar = true;
	component.simulationLodDisableAnimationAtFar = true;
	component.simulationLodDisableEffectsAtFar = true;
	component.simulationLodAffectHierarchy = true;
	component.simulationLodRuntimeLevel = 0;
	component.railEventMarkerEntries.clear();
	component.railEventMarkerActionTargetGameObjectId = -1;
	component.railEventMarkerPreviousProgress = 0.0f;
	component.railEventMarkerRuntimeInitialized = false;
	component.sceneStreamingScenePath.clear();
	component.sceneStreamingReferenceGameObjectId = -1;
	component.sceneStreamingLoadDistance = 500.0f;
	component.sceneStreamingUnloadDistance = 650.0f;
	component.sceneStreamingUnloadWhenFar = true;
	component.sceneStreamingRuntimeLoaded = false;
	component.sceneStreamingRuntimePending = false;

	return component;
}

int32_t EditorScene::FindGameObjectIndex(int32_t gameObjectId) const {
	if (gameObjectIndexById_.size() != gameObjects_.size()) {
		RebuildGameObjectIndex();
	}

	const auto gameObjectIndexIterator = gameObjectIndexById_.find(gameObjectId);

	if (gameObjectIndexIterator != gameObjectIndexById_.end()) {
		const int32_t gameObjectIndex = gameObjectIndexIterator->second;

		if (gameObjectIndex >= 0 &&
			gameObjectIndex < static_cast<int32_t>(gameObjects_.size()) &&
			gameObjects_[static_cast<size_t>(gameObjectIndex)].id == gameObjectId) {
			return gameObjectIndex;
		}
	}

	// 無効IDや未接続Component参照は通常経路なので、Missごとに全索引を再構築しない。
	return -1;
}

void EditorScene::RebuildGameObjectIndex() const {
	gameObjectIndexById_.clear();
	gameObjectIndexById_.reserve(gameObjects_.size());

	for (int32_t gameObjectIndex = 0;
		gameObjectIndex < static_cast<int32_t>(gameObjects_.size());
		gameObjectIndex++) {
		gameObjectIndexById_[gameObjects_[static_cast<size_t>(gameObjectIndex)].id] = gameObjectIndex;
	}
}

void EditorScene::RemoveFromParent(int32_t childId) {
	for (EditorGameObject& gameObject : gameObjects_) {
		auto removeIterator = std::remove(gameObject.children.begin(), gameObject.children.end(), childId);  // 全親候補の children から childId を取り除く
		gameObject.children.erase(removeIterator, gameObject.children.end());
	}
}

void EditorScene::RebuildChildren() {
	// Load / Undoで件数が同じままID構成が変わる場合も、親参照を解決する前に索引を更新する。
	RebuildGameObjectIndex();

	// parentId を正として children 配列を作り直すため、一度全て空にする
	for (EditorGameObject& gameObject : gameObjects_) {
		gameObject.children.clear();
	}

	for (EditorGameObject& gameObject : gameObjects_) {
		EditorGameObject* parent = FindGameObject(gameObject.parentId);  // parentId が実在する場合だけ、その親の children に自分の ID を追加する
		if (parent != nullptr) {
			parent->children.push_back(gameObject.id);
		}
	}
}

void EditorScene::DeleteGameObjectRecursive(int32_t gameObjectId) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);  // 削除対象がなければ何もしない
	if (gameObject == nullptr) {
		return;
	}

	std::vector<int32_t> childIds = gameObject->children;  // 再帰中に children が変わるため、削除前に子 ID をコピーしておく
	for (int32_t childId : childIds) {
		DeleteGameObjectRecursive(childId);
	}

	RemoveFromParent(gameObjectId);  // 親の children から自分を外す
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);
	if (gameObjectIndex >= 0) {
		gameObjects_.erase(gameObjects_.begin() + static_cast<std::ptrdiff_t>(gameObjectIndex));  // 配列から GameObject 本体を削除する
	}
}

void EditorScene::RefreshNextGameObjectId() {
	nextGameObjectId_ = 1;  // 既存 ID の最大値 + 1 を次回生成 ID にする
	for (const EditorGameObject& gameObject : gameObjects_) {
		nextGameObjectId_ = (std::max)(nextGameObjectId_, gameObject.id + 1);
	}
}

#pragma warning(pop)
