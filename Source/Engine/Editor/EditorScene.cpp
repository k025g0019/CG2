#include "EditorScene.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>

#pragma warning(push)
#pragma warning(disable : 5045)

namespace {
	constexpr int32_t kInvalidGameObjectId = -1;  // 親なし / 無効 ID を表す値
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
	gameObjects_.clear();  // 起動時は GameObject を置かない空 Scene にする
	undoStack_.clear();
	redoStack_.clear();
	nextGameObjectId_ = 1;
	ResetPhysicsSettings(physicsSettings_);
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

	EditorGameObject duplicatedGameObject = *sourceGameObject;  // コピー先は新しい ID と名前を持ち、親子関係は一旦切る
	duplicatedGameObject.id = nextGameObjectId_;
	duplicatedGameObject.parentId = kInvalidGameObjectId;
	duplicatedGameObject.children.clear();
	duplicatedGameObject.name += "_Copy";
	duplicatedGameObject.translate.x += 0.2f;

	nextGameObjectId_++;  // 複製後に children 配列を作り直す
	gameObjects_.push_back(duplicatedGameObject);
	RebuildChildren();

	return duplicatedGameObject.id;
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

bool EditorScene::SetParent(int32_t childId, int32_t parentId) {
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

	RemoveFromParent(childId);  // 既存親から外して、新しい親 ID を設定する
	child->parentId = parentId;
	RebuildChildren();

	return true;
}

bool EditorScene::AddComponent(int32_t gameObjectId, EditorComponentType type) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);  // 同じ種類の Component は重複追加しない
	if (gameObject == nullptr || HasComponent(gameObjectId, type)) {
		return false;
	}

	gameObject->components.push_back(CreateComponent(type));
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
	std::ofstream file(filePath);  // Scene を独自の | 区切りテキストとして保存する
	if (!file.is_open()) {
		return false;
	}

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
	file << "\n";

	for (const EditorGameObject& gameObject : gameObjects_) {
		// GameObject 行には ID / 親 / 名前 / Transform を保存する
		file << "GameObject|"
		     << gameObject.id << "|"
		     << gameObject.parentId << "|"
		     << gameObject.name << "|"
		     << gameObject.translate.x << "|"
		     << gameObject.translate.y << "|"
		     << gameObject.translate.z << "|"
		     << gameObject.rotate.x << "|"
		     << gameObject.rotate.y << "|"
		     << gameObject.rotate.z << "|"
		     << gameObject.scale.x << "|"
		     << gameObject.scale.y << "|"
		     << gameObject.scale.z << "\n";

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
			     << EncodeSceneToken(component.sliderOnValueChangedFunction) << "\n";

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
	std::ifstream file(filePath);  // SaveScene と同じ | 区切りテキストを読み込む
	if (!file.is_open()) {
		return false;
	}

	std::vector<EditorGameObject> loadedGameObjects;  // 読み込み途中の Scene。成功したら gameObjects_ へ置き換える
	EditorPhysicsSettings loadedPhysicsSettings = physicsSettings_;  // 古い Scene に設定行がない場合は現在の既定値を使う
	bool hasSceneData = false;  // 空 Scene 保存も許可するため、GameObject が 0 件でも有効な Scene 行を読んだかを記録する
	std::string line;

	while (std::getline(file, line)) {
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
		}
		else if (elements[0] == "GameObject" && elements.size() >= 13) {
			hasSceneData = true;  // GameObject 行が 1 つでもあれば有効な Scene
			// GameObject 行から ID / 親 / 名前 / Transform を復元する
			EditorGameObject gameObject{};
			gameObject.id = ToInt(elements[1]);
			gameObject.parentId = ToInt(elements[2]);
			gameObject.isActive = true;
			gameObject.name = elements[3];
			gameObject.translate = {ToFloat(elements[4]), ToFloat(elements[5]), ToFloat(elements[6])};
			gameObject.rotate = {ToFloat(elements[7]), ToFloat(elements[8]), ToFloat(elements[9])};
			gameObject.scale = {ToFloat(elements[10]), ToFloat(elements[11]), ToFloat(elements[12])};
			loadedGameObjects.push_back(gameObject);
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
				gameObject.components.push_back(component);
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
	RebuildChildren();  // parentId から children 配列を作り直す
	RefreshNextGameObjectId();  // 次に生成する ID が既存 ID と衝突しないよう更新する
	undoStack_.clear();
	redoStack_.clear();

	return true;
}

bool EditorScene::SavePrefab(int32_t gameObjectId, const std::string& filePath) const {
	EditorScene prefabScene;  // Prefab は GameObject 1 個だけを持つ一時 Scene として保存する
	const EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	prefabScene.gameObjects_.push_back(*gameObject);  // Prefab 化すると親子関係は切る
	prefabScene.gameObjects_[0].parentId = kInvalidGameObjectId;
	prefabScene.gameObjects_[0].children.clear();
	return prefabScene.SaveScene(filePath);
}

int32_t EditorScene::InstantiatePrefab(const std::string& filePath) {
	EditorScene prefabScene;  // Prefab ファイルを一時 Scene として読み込む
	if (!prefabScene.LoadScene(filePath) || prefabScene.gameObjects_.empty()) {
		return kInvalidGameObjectId;
	}

	EditorGameObject gameObject = prefabScene.gameObjects_[0];  // 読み込んだ先頭 GameObject に新しい ID を付けて Scene に追加する
	gameObject.id = nextGameObjectId_;
	gameObject.parentId = kInvalidGameObjectId;
	gameObject.children.clear();
	gameObject.name += "_Prefab";
	gameObject.translate.x += 0.3f;  // 生成位置が完全に重ならないよう X 方向へ少しずらす

	nextGameObjectId_++;
	gameObjects_.push_back(gameObject);
	return gameObject.id;
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
	component.color = {1.0f, 1.0f, 1.0f};
	component.intensity = 1.0f;
	component.metallic = 0.0f;
	component.roughness = 0.5f;
	component.ior = 1.0f;
	component.alpha = 1.0f;
	component.reflectionStrength = 0.0f;
	component.emissionStrength = 0.0f;
	component.mass = 1.0f;
	component.drag = 0.0f;
	component.useGravity = true;
	component.isKinematic = false;
	component.isTrigger = false;
	component.bounciness = 0.0f;
	component.velocity = {0.0f, 0.0f, 0.0f};
	component.angularVelocity = {0.0f, 0.0f, 0.0f};
	component.angularDrag = 0.05f;
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
		component.animatorState = 0;
		component.particleRate = 10.0f;
		component.particleLifetime = 2.0f;
		component.particleSpeed = 5.0f;
		component.particleSize = 0.5f;
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
		component.aaMode = 1;
		component.finalBrightness = 1.0f;
		component.smaaEnabled = true;
		component.taaEnabled = true;
		component.ssrEnabled = false;
		component.compositeExposure = 1.0f;
		component.compositeWhitePoint = 3.0f;
		component.compositeToneMappingMode = 3;
		component.compositeBloomIntensity = 0.70f;
		component.compositeSaturation = 1.08f;
		component.compositeContrast = 1.05f;
		component.compositeVignetteStrength = 0.18f;
		component.compositeVignetteRadius = 0.92f;
		component.compositeFilmGrain = 0.25f;
		component.compositeChromaticAberration = 0.15f;
		component.compositeAmbientOcclusionStrength = 0.65f;
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
		component.animationType = 1;
		component.animationAmplitude = 0.5f;
	}
	else if (type == EditorComponentType::Animator) {
		component.animationSpeed = 1.0f;
		component.animatorState = 0;
	}
	else if (type == EditorComponentType::ParticleSystem) {
		component.particleRate = 10.0f;
		component.particleLifetime = 2.0f;
		component.particleSpeed = 5.0f;
		component.particleSize = 0.5f;
	}

	return component;
}

int32_t EditorScene::FindGameObjectIndex(int32_t gameObjectId) const {
	for (int32_t gameObjectIndex = 0; gameObjectIndex < static_cast<int32_t>(gameObjects_.size()); ++gameObjectIndex) {
		// ID が一致した配列番号を返す
		if (gameObjects_[static_cast<size_t>(gameObjectIndex)].id == gameObjectId) {
			return gameObjectIndex;
		}
	}

	return -1;
}

void EditorScene::RemoveFromParent(int32_t childId) {
	for (EditorGameObject& gameObject : gameObjects_) {
		auto removeIterator = std::remove(gameObject.children.begin(), gameObject.children.end(), childId);  // 全親候補の children から childId を取り除く
		gameObject.children.erase(removeIterator, gameObject.children.end());
	}
}

void EditorScene::RebuildChildren() {
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
