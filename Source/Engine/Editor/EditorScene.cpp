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
			     << component.emissionStrength << "\n";
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
					if (elements.size() >= 64) {
						component.connectedGameObjectId = ToInt(elements[54]);  // Joint の接続先 ID。Scene 内 GameObject と対応させる
						component.jointAxis = {ToFloat(elements[55]), ToFloat(elements[56]), ToFloat(elements[57])};
						component.jointMinLimit = ToFloat(elements[58]);
						component.jointMaxLimit = ToFloat(elements[59]);
						component.jointMinDistance = ToFloat(elements[60]);
						component.jointMaxDistance = ToFloat(elements[61]);
						component.jointSpringFrequency = ToFloat(elements[62]);
						component.jointSpringDamping = ToFloat(elements[63]);
					}
					if (elements.size() >= 69) {
						component.inputActionMapName = elements[64];
						component.inputBehavior = ToInt(elements[65]);
						component.inputMoveEventName = elements[66];
						component.inputJumpEventName = elements[67];
						component.inputFireEventName = elements[68];
					}
					if (elements.size() >= 83) {
						component.navAgentRadius = ToFloat(elements[69]);
						component.navAgentHeight = ToFloat(elements[70]);
						component.navMaxSpeed = ToFloat(elements[71]);
						component.navMaxAcceleration = ToFloat(elements[72]);
						component.navStoppingDistance = ToFloat(elements[73]);
						component.navAutoRepath = ToInt(elements[74]) != 0;
						component.navCarve = ToInt(elements[75]) != 0;
						component.navMaxSlope = ToFloat(elements[76]);
						component.navMaxClimb = ToFloat(elements[77]);
						component.navAreaOverride = ToInt(elements[78]) != 0;
						component.navArea = ToInt(elements[79]);
						component.navIgnoreFromBuild = ToInt(elements[80]) != 0;
						component.navBidirectional = ToInt(elements[81]) != 0;
						component.navCostModifier = ToFloat(elements[82]);
					}
					if (elements.size() >= 85) {
						component.rollingTorque = ToFloat(elements[83]);
						component.rollingHorsepower = ToFloat(elements[84]);
					}
					if (elements.size() >= 96) {
						component.constraintWeight = ToFloat(elements[85]);
						component.constraintPositionOffset = {ToFloat(elements[86]), ToFloat(elements[87]), ToFloat(elements[88])};
						component.constraintRotationOffset = {ToFloat(elements[89]), ToFloat(elements[90]), ToFloat(elements[91])};
						component.constraintAimAxis = ToInt(elements[92]);
						component.constraintUpAxis = ToInt(elements[93]);
						component.constraintRoll = ToFloat(elements[94]);
						component.constraintFreezeAxisX = ToInt(elements[95]) != 0;
						component.constraintFreezeAxisY = ToInt(elements[96]) != 0;
						component.constraintFreezeAxisZ = ToInt(elements[97]) != 0;
					}
					if (elements.size() >= 103) {
						component.animationSpeed = ToFloat(elements[98]);
						component.animationLoop = ToInt(elements[99]) != 0;
						component.animationPlayOnAwake = ToInt(elements[100]) != 0;
						component.animationType = ToInt(elements[101]);
						component.animationAmplitude = ToFloat(elements[102]);
					}
					if (elements.size() >= 108) {
						component.animatorState = ToInt(elements[103]);
						component.particleRate = ToFloat(elements[104]);
						component.particleLifetime = ToFloat(elements[105]);
						component.particleSpeed = ToFloat(elements[106]);
						component.particleSize = ToFloat(elements[107]);
					}
					if (elements.size() >= 113) {
						component.metallic = ToFloat(elements[108]);
						component.roughness = ToFloat(elements[109]);
						component.ior = ToFloat(elements[110]);
						component.alpha = ToFloat(elements[111]);
						component.reflectionStrength = ToFloat(elements[112]);
					}
					if (elements.size() >= 115) {
						component.textureAssetPath = elements[113];
						component.emissionStrength = ToFloat(elements[114]);
					}
					gameObject.components.push_back(component);
					break;
				}
		}
	}

	if (!hasSceneData) {
		return false;
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
