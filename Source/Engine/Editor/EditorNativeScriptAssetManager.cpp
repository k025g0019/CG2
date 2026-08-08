#include "EditorNativeScriptAssetManager.h"

#include "Source/Engine/Core/EditorNativeScript.h"  // 生成対象の公開 C++ API もエンジンビルド時に検証する。

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
	constexpr std::array<unsigned char, 3> kUtf8Bom{{0xEFu, 0xBBu, 0xBFu}};  // 保存時に先頭へ付ける UTF-8 BOM。
	constexpr std::array<
		EditorNativeScriptTemplateInfo,
		static_cast<size_t>(EditorNativeScriptTemplate::Count)> kTemplateInfos{{
		{EditorNativeScriptTemplate::Empty, "基本", "空のスクリプト", "最小構成から独自処理を書きます。", "Script / MonoBehaviour"},
		{EditorNativeScriptTemplate::PlayerController, "移動・入力", "プレイヤー移動", "Vector2入力でTransformを移動します。", "PlayerInput / Input / FreeTransform"},
		{EditorNativeScriptTemplate::RailPlayer, "移動・入力", "レール移動操作", "入力をRailMovementの左右・上下Offsetへ渡します。", "RailMovement / PlayerInput / MovementModifier"},
		{EditorNativeScriptTemplate::EnemyController, "戦闘・AI", "敵の基本制御", "TargetSelectorの結果を使う敵処理の開始コードです。", "TargetSelector / Health / HitscanWeapon / ProjectileEmitter"},
		{EditorNativeScriptTemplate::TurretController, "戦闘・AI", "砲塔制御", "選択TargetへYaw/Pitchを向けて射撃する開始コードです。", "TargetSelector / HitscanWeapon / ProjectileEmitter"},
		{EditorNativeScriptTemplate::HomingController, "戦闘・AI", "追尾制御", "TargetSteeringへ追尾開始・終了条件を追加します。", "TargetSelector / TargetSteering / Rigidbody"},
		{EditorNativeScriptTemplate::BossController, "戦闘・AI", "体力フェーズ制御", "Health比率からフェーズを切り替える開始コードです。", "Health / ThresholdState / ActionSequence"},
		{EditorNativeScriptTemplate::StageController, "Scene・進行", "Scene進行", "入力やゲーム条件からSceneを切り替えます。", "TimelineEvent / ActionSequence / Scene Asset"},
		{EditorNativeScriptTemplate::LoadoutController, "戦闘・入力", "武器切替", "WeaponLoadoutの切替・射撃・リロードを入力へ接続します。", "WeaponLoadout / WeaponLoadoutSlot / PlayerInput"},
		{EditorNativeScriptTemplate::PhysicsController, "物理", "Rigidbody移動", "FixedUpdateで入力方向へ力を加えます。", "RigidBody / Collider / ConstantForce"},
		{EditorNativeScriptTemplate::HealthDamageController, "戦闘", "体力・破壊", "Healthを監視し0以下の終了処理を書く開始コードです。", "Health / DamageReceiver / Collider"},
		{EditorNativeScriptTemplate::SpawnPoolController, "生成", "生成・Pool", "PrefabSpawnerまたはObjectPoolからObjectを生成します。", "PrefabSpawner / ObjectPool / WaveSpawner"},
		{EditorNativeScriptTemplate::CameraEffectsController, "カメラ", "カメラ演出", "Camera BlendとShakeを入力・イベントから再生します。", "Camera / CameraBlend / CameraShake"},
		{EditorNativeScriptTemplate::AnimationEffectController, "Animation・VFX", "Animation・Effect", "AnimationとParticle/VFXを同時に起動する開始コードです。", "Animator / Animation / ParticleSystem / VisualEffect"},
		{EditorNativeScriptTemplate::AudioController, "Audio", "音量・音響制御", "AudioSourceの公開Propertyをゲーム中に変更します。", "AudioSource / AudioReverbZone / Audio Filter"},
		{EditorNativeScriptTemplate::UiController, "UI", "UIイベント", "Button等からBindActionを呼ぶUI処理の開始コードです。", "Canvas / Button / Text / Image / UIValueBinding"},
		{EditorNativeScriptTemplate::ActionEventController, "イベント", "Action・Sequence", "ActionRelayとActionSequenceをゲーム条件へ接続します。", "ActionRelay / ActionSequence / TimelineEvent / PropertyTween"},
		{EditorNativeScriptTemplate::SaveCheckpointController, "保存", "Save・Checkpoint", "Save SlotとCheckpointを入力・イベントへ接続します。", "Saveable / Checkpoint"},
		{EditorNativeScriptTemplate::OceanBuoyancyController, "海・物理", "海面問い合わせ", "描画と浮力が共有するOcean表面情報を取得します。", "Ocean / Buoyancy / RigidBody"},
		{EditorNativeScriptTemplate::NavigationAiController, "Navigation・AI", "Target・経路AI", "Target取得後のNavigation/Steering条件を書く開始コードです。", "NavigationAgent / AIPathRequest / TargetSelector / AISteeringAgent"},
		{EditorNativeScriptTemplate::RuntimePropertyController, "Component連携", "Component Property操作", "Component存在確認と公開Property変更を行います。", "任意Component / PropertyTween / ActionRelay"},
		{EditorNativeScriptTemplate::ScoreController, "ゲーム進行", "スコア制御", "型付きAction PayloadをGenericCounterへ加算する開始コードです。", "GenericCounter / ActionRelay / UIValueBinding"},
		{EditorNativeScriptTemplate::ComboController, "ゲーム進行", "コンボ制御", "命中ActionでComboを加算しTimer満了Actionでリセットします。", "GenericCounter / Timer / ActionRelay"},
		{EditorNativeScriptTemplate::StageResultController, "Scene・進行", "ステージ結果", "ScoreからRankを決定しScene間データへ保存します。", "GenericCounter / GameplayData / SceneButton"},
		{EditorNativeScriptTemplate::RailEventController, "移動・イベント", "レールイベント受信", "Rail進行率MarkerのIDを型付きAction Payloadとして受け取ります。", "RailMovement / RailEventMarker / ActionRelay"},
		{EditorNativeScriptTemplate::SimulationLodController, "最適化", "シミュレーションLOD参照", "距離別のRuntime LOD段階をゲーム固有処理から参照します。", "SimulationLOD / Script"},
	}};

	void ReplaceAll(std::string& text, const std::string& oldText, const std::string& newText) {
		size_t replacePosition = 0U;

		while ((replacePosition = text.find(oldText, replacePosition)) != std::string::npos) {
			text.replace(replacePosition, oldText.size(), newText);
			replacePosition += newText.size();
		}
	}

	std::string MakeTemplateUpdateBody(EditorNativeScriptTemplate scriptTemplate) {
		switch (scriptTemplate) {
		case EditorNativeScriptTemplate::RailPlayer:
			return "\t(void)deltaTime;\n\tRailFollower{GameObject{gameObjectId}}.SetMoveInput(moveInput_);\n";
		case EditorNativeScriptTemplate::EnemyController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tconst GameObject target = Targeting{owner}.GetCurrentTarget();\n\n\tif (!target.HasReference()) {\n\t\treturn;\n\t}\n\n\t// 距離、視界、攻撃間隔などゲーム固有条件をここへ追加する。\n";
		case EditorNativeScriptTemplate::TurretController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tconst GameObject target = Targeting{owner}.GetCurrentTarget();\n\n\tif (!target.HasReference()) {\n\t\treturn;\n\t}\n\n\t// Yaw台座とPitch砲身を公開GameObjectにしてTarget方向へ回転させる。\n";
		case EditorNativeScriptTemplate::HomingController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tconst GameObject target = Targeting{owner}.GetCurrentTarget();\n\n\tif (!target.HasReference()) {\n\t\treturn;\n\t}\n\n\t// 旋回・加速はTargetSteeringが行い、ここでは開始・爆発条件を書く。\n";
		case EditorNativeScriptTemplate::BossController:
			return "\t(void)deltaTime;\n\tfloat currentHealth = 0.0f;\n\tfloat maximumHealth = 0.0f;\n\n\tif (!Health{GameObject{gameObjectId}}.Get(currentHealth, maximumHealth) || maximumHealth <= 0.0f) {\n\t\treturn;\n\t}\n\n\tconst float healthRatio = currentHealth / maximumHealth;\n\t(void)healthRatio;  // Phase境界とActionSequence切替をゲーム側で実装する。\n";
		case EditorNativeScriptTemplate::StageController:
			return "\t(void)gameObjectId;\n\t(void)deltaTime;\n\n\tif (!nextScenePath_.empty() && Input::GetKeyDown(KeyCode::Space)) {\n\t\tSceneManager::LoadScene(nextScenePath_);\n\t}\n";
		case EditorNativeScriptTemplate::LoadoutController:
			return "\t(void)deltaTime;\n\tWeaponLoadout loadout{GameObject{gameObjectId}};\n\n\tif (Input::GetKeyDown(KeyCode::Q)) {\n\t\tloadout.Previous();\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::E)) {\n\t\tloadout.Next();\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::R)) {\n\t\tloadout.Reload();\n\t}\n";
		case EditorNativeScriptTemplate::PhysicsController:
			return "\t(void)gameObjectId;\n\t(void)deltaTime;  // 力の適用はFixedUpdateへ分離する。\n";
		case EditorNativeScriptTemplate::HealthDamageController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tfloat currentHealth = 0.0f;\n\tfloat maximumHealth = 0.0f;\n\n\tif (!Health{owner}.Get(currentHealth, maximumHealth)) {\n\t\treturn;\n\t}\n\n\tif (currentHealth <= 0.0f) {\n\t\towner.SetActive(false);  // 破壊演出やPool返却へ差し替える。\n\t}\n";
		case EditorNativeScriptTemplate::SpawnPoolController:
			return "\t(void)deltaTime;\n\n\tif (Input::GetKeyDown(KeyCode::Space)) {\n\t\tSpawner{GameObject{gameObjectId}}.Spawn();\n\t}\n";
		case EditorNativeScriptTemplate::CameraEffectsController:
			return "\t(void)deltaTime;\n\tCameraEffects cameraEffects{GameObject{gameObjectId}};\n\n\tif (Input::GetKeyDown(KeyCode::C)) {\n\t\tcameraEffects.PlayShake();\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::V)) {\n\t\tcameraEffects.PlayBlend();\n\t}\n";
		case EditorNativeScriptTemplate::AnimationEffectController:
			return "\t(void)deltaTime;\n\n\tif (runtimeApi != nullptr && Input::GetKeyDown(KeyCode::Space)) {\n\t\tif (runtimeApi->PlayAnimation != nullptr) {\n\t\t\truntimeApi->PlayAnimation(gameObjectId);\n\t\t}\n\n\t\tif (runtimeApi->PlayEffect != nullptr) {\n\t\t\truntimeApi->PlayEffect(gameObjectId);\n\t\t}\n\t}\n";
		case EditorNativeScriptTemplate::AudioController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tfloat volume = 1.0f;\n\n\tif (!RuntimeProperty::GetFloat(owner, \"AudioSource\", \"Volume\", volume)) {\n\t\treturn;\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::Q)) {\n\t\tvolume = volume > 0.1f ? volume - 0.1f : 0.0f;\n\t\tRuntimeProperty::SetFloat(owner, \"AudioSource\", \"Volume\", volume);\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::E)) {\n\t\tvolume = volume < 0.9f ? volume + 0.1f : 1.0f;\n\t\tRuntimeProperty::SetFloat(owner, \"AudioSource\", \"Volume\", volume);\n\t}\n";
		case EditorNativeScriptTemplate::UiController:
			return "\t(void)gameObjectId;\n\t(void)deltaTime;  // Button/Toggle/SliderからOnClick/OnValueChangedを呼ぶ。\n";
		case EditorNativeScriptTemplate::ActionEventController:
			return "\t(void)deltaTime;\n\n\tif (Input::GetKeyDown(KeyCode::Space)) {\n\t\tconst GameObject owner{gameObjectId};\n\t\tActionRelay{owner}.Relay();\n\t\tActionSequence{owner}.Play();\n\t}\n";
		case EditorNativeScriptTemplate::SaveCheckpointController:
			return "\t(void)deltaTime;\n\n\tif (Input::GetKeyDown(KeyCode::F)) {\n\t\tSaveSystem::Save(\"Save01\");\n\t\tCheckpoint{GameObject{gameObjectId}}.Save();\n\t}\n\n\tif (Input::GetKeyDown(KeyCode::L)) {\n\t\tSaveSystem::Load(\"Save01\");\n\t}\n";
		case EditorNativeScriptTemplate::OceanBuoyancyController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tconst EditorScriptTransform transform = owner.GetTransform();\n\tEditorScriptOceanSurfaceHit oceanHit{};\n\n\tif (!Physics::SampleOceanSurface(owner, transform.position, oceanHit)) {\n\t\treturn;\n\t}\n\n\tconst float distanceToSurface = oceanHit.signedDistance;\n\t(void)distanceToSurface;  // 着水、航跡、AI判断などゲーム固有条件へ使う。\n";
		case EditorNativeScriptTemplate::NavigationAiController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\tconst GameObject target = Targeting{owner}.GetCurrentTarget();\n\n\tif (!target.HasReference()) {\n\t\treturn;\n\t}\n\n\t// NavigationAgent、AIPathRequest、Steeringの目的地をTargetへ更新する。\n";
		case EditorNativeScriptTemplate::RuntimePropertyController:
			return "\t(void)deltaTime;\n\tconst GameObject owner{gameObjectId};\n\n\tif (Input::GetKeyDown(KeyCode::Space) && owner.HasComponent(\"ParticleSystem\")) {\n\t\tconst bool isActive = owner.IsComponentActive(\"ParticleSystem\");\n\t\towner.SetComponentActive(\"ParticleSystem\", !isActive);\n\t}\n";
		case EditorNativeScriptTemplate::ScoreController:
		case EditorNativeScriptTemplate::ComboController:
		case EditorNativeScriptTemplate::StageResultController:
		case EditorNativeScriptTemplate::RailEventController:
			return "\t(void)gameObjectId;\n\t(void)deltaTime;  // 加算・確定はActionから受け取り、毎フレーム処理を増やさない。\n";
		case EditorNativeScriptTemplate::SimulationLodController:
			return "\t(void)deltaTime;\n\tint32_t lodLevel = 0;\n\n\tif (!SimulationLod{GameObject{gameObjectId}}.GetLevel(lodLevel)) {\n\t\treturn;\n\t}\n\n\t// 0=Near、1=Medium、2=Far、3=Culled。固有処理の頻度や品質選択に使う。\n\t(void)lodLevel;\n";
		case EditorNativeScriptTemplate::PlayerController:
			return "\tconst GameObject gameObject{gameObjectId};\n\tEditorScriptTransform transform = gameObject.GetTransform();\n\ttransform.position.x += moveInput_.x * moveSpeed_ * deltaTime;\n\ttransform.position.z += moveInput_.y * moveSpeed_ * deltaTime;\n\tgameObject.SetTransform(transform);\n";
		case EditorNativeScriptTemplate::Count:
		case EditorNativeScriptTemplate::Empty:
		default:
			return "\t(void)gameObjectId;\n\t(void)deltaTime;\n";
		}
	}

	std::string MakeTemplateFixedUpdateBody(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::PhysicsController) {
			return "\t(void)fixedDeltaTime;\n\tconst EditorScriptVector3 movementForce{\n\t\tmoveInput_.x * moveSpeed_,\n\t\t0.0f,\n\t\tmoveInput_.y * moveSpeed_};\n\tRigidbody{gameObjectId}.AddForce(movementForce);\n";
		}

		return "\t(void)gameObjectId;\n\t(void)fixedDeltaTime;  // AddForce など周期を固定した物理処理を書く。\n";
	}

	std::string MakeTemplateFireBody(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::RailPlayer ||
			scriptTemplate == EditorNativeScriptTemplate::EnemyController ||
			scriptTemplate == EditorNativeScriptTemplate::TurretController ||
			scriptTemplate == EditorNativeScriptTemplate::LoadoutController) {
			return "\tif (inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\tWeaponLoadout{GameObject{inputContext.gameObjectId}}.Fire();\n\t}\n";
		}

		return "\tif (runtimeApi != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\truntimeApi->Log(\"OnFire\");\n\t}\n";
	}

	std::string MakeTemplateClickBody(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::UiController) {
			return "\tif (inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\tActionRelay{GameObject{inputContext.gameObjectId}}.Relay();\n\t}\n";
		}

		if (scriptTemplate == EditorNativeScriptTemplate::ScoreController) {
			return "\tif (inputContext.phase != EditorScriptInputPhasePerformed) {\n\t\treturn;\n\t}\n\n\tfloat scoreDelta = 1.0f;\n\n\tif (inputContext.payloadType == EditorScriptActionPayloadTypeFloat) {\n\t\tscoreDelta = inputContext.payloadFloat;\n\t}\n\telse if (inputContext.payloadType == EditorScriptActionPayloadTypeInt) {\n\t\tscoreDelta = static_cast<float>(inputContext.payloadInt);\n\t}\n\n\tGenericCounter{GameObject{inputContext.gameObjectId}}.Add(scoreDelta);\n";
		}

		if (scriptTemplate == EditorNativeScriptTemplate::ComboController) {
			return "\tif (inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\tconst GameObject owner{inputContext.gameObjectId};\n\t\tGenericCounter{owner}.Add(1.0f);\n\t\tTimer{owner}.Start();\n\t}\n";
		}

		if (scriptTemplate == EditorNativeScriptTemplate::StageResultController) {
			return "\tif (inputContext.phase != EditorScriptInputPhasePerformed) {\n\t\treturn;\n\t}\n\n\tfloat score = 0.0f;\n\n\tif (!GenericCounter{GameObject{inputContext.gameObjectId}}.Get(score)) {\n\t\treturn;\n\t}\n\n\tconst std::string rank = score >= 100000.0f ? \"S\" :\n\t\tscore >= 70000.0f ? \"A\" :\n\t\tscore >= 40000.0f ? \"B\" : \"C\";\n\tSceneManager::SetFloat(\"StageScore\", score);\n\tSceneManager::SetString(\"StageRank\", rank);\n";
		}

		return "\tif (runtimeApi != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\truntimeApi->Log(\"OnClick\");\n\t}\n";
	}

	std::string MakeTemplateValueChangedBody(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::ComboController) {
			return "\tif (inputContext.phase == EditorScriptInputPhasePerformed) {\n\t\tGenericCounter{GameObject{inputContext.gameObjectId}}.Set(0.0f);\n\t}\n";
		}

		return "\tif (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {\n\t\treturn;\n\t}\n\n\tif (inputContext.valueType == EditorScriptInputValueTypeButton) {\n\t\truntimeApi->Log(inputContext.buttonValue > 0.5f ? \"OnValueChanged: ON\" : \"OnValueChanged: OFF\");\n\t\treturn;\n\t}\n\n\tconst std::string message = \"OnValueChanged: \" + std::to_string(inputContext.vector2Value.x);\n\truntimeApi->Log(message.c_str());\n";
	}

	std::string MakeTemplateMethodDeclarations(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::RailEventController) {
			return "\tvoid OnRailMarker(const EditorScriptInputActionContext& inputContext);\n";
		}

		return {};
	}

	std::string MakeTemplateActionBindings(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::RailEventController) {
			return "\tBindAction(\"OnRailMarker\", [this](const EditorScriptInputActionContext& inputContext) { OnRailMarker(inputContext); });\n";
		}

		return {};
	}

	std::string MakeTemplateMethodDefinitions(
		const std::string& scriptName,
		EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate != EditorNativeScriptTemplate::RailEventController) {
			return {};
		}

		std::string methodText = R"SCRIPT(
void __SCRIPT_NAME__::OnRailMarker(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	const std::string markerId = inputContext.payloadType == EditorScriptActionPayloadTypeString &&
		inputContext.payloadString != nullptr
		? inputContext.payloadString
		: "";
	const std::string message = "Rail Marker: " + markerId;
	runtimeApi->Log(message.c_str());

	// markerIdごとのゲーム固有処理はここへ追加する。
}
)SCRIPT";
		ReplaceAll(methodText, "__SCRIPT_NAME__", scriptName);
		return methodText;
	}
}

int32_t EditorNativeScriptAssetManager::GetTemplateCount() {
	return static_cast<int32_t>(kTemplateInfos.size());
}

const EditorNativeScriptTemplateInfo& EditorNativeScriptAssetManager::GetTemplateInfo(
	int32_t templateIndex) {
	const int32_t safeTemplateIndex = (std::clamp)(
		templateIndex,
		0,
		static_cast<int32_t>(kTemplateInfos.size()) - 1);
	return kTemplateInfos[static_cast<size_t>(safeTemplateIndex)];
}

EditorNativeScriptAssetResult EditorNativeScriptAssetManager::CreateNativeScriptAsset(
	const std::string& requestedScriptName,
	bool isDebugBuild,
	EditorNativeScriptTemplate scriptTemplate) {
	EditorNativeScriptAssetResult result{};
	result.sanitizedScriptName = SanitizeScriptName(requestedScriptName);

	if (result.sanitizedScriptName.empty()) {
		result.message = "C++ スクリプト名が空です。半角英数字で入力してください。";
		return result;
	}

	const std::filesystem::path scriptDirectoryPath =
		std::filesystem::path("resources") / "scripts" / result.sanitizedScriptName;

	std::error_code fileError;
	std::filesystem::create_directories(scriptDirectoryPath, fileError);
	if (fileError) {
		result.message = "script フォルダを作成できませんでした。";
		return result;
	}

	result.scriptDirectoryPath = scriptDirectoryPath.generic_string();
	result.headerFilePath = (scriptDirectoryPath / (result.sanitizedScriptName + ".h")).generic_string();
	result.sourceFilePath = (scriptDirectoryPath / (result.sanitizedScriptName + ".cpp")).generic_string();
	result.buildDebugFilePath = (scriptDirectoryPath / "build_debug.bat").generic_string();
	result.buildReleaseFilePath = (scriptDirectoryPath / "build_release.bat").generic_string();
	result.dllFilePath =
		(scriptDirectoryPath /
		 "x64" /
		 (isDebugBuild ? "Debug" : "Release") /
		 (result.sanitizedScriptName + ".dll"))
			.generic_string();

	const bool isHeaderWritten = WriteUtf8BomFile(
		result.headerFilePath,
		MakeHeaderText(result.sanitizedScriptName, scriptTemplate));
	const bool isSourceWritten = WriteUtf8BomFile(
		result.sourceFilePath,
		MakeSourceText(result.sanitizedScriptName, scriptTemplate));
	const bool isDebugBuildFileWritten =
		WriteUtf8BomFile(result.buildDebugFilePath, MakeBuildScriptText(result.sanitizedScriptName, true));
	const bool isReleaseBuildFileWritten =
		WriteUtf8BomFile(result.buildReleaseFilePath, MakeBuildScriptText(result.sanitizedScriptName, false));

	if (!isHeaderWritten || !isSourceWritten || !isDebugBuildFileWritten || !isReleaseBuildFileWritten) {
		result.message = "C++ スクリプト雛形の保存に失敗しました。";
		return result;
	}

	result.isSucceeded = true;
	result.message = isDebugBuild
		? "C++ スクリプトを生成しました。build_debug.bat を実行すると DLL が作られます。"
		: "C++ スクリプトを生成しました。build_release.bat を実行すると DLL が作られます。";
	return result;
}

std::string EditorNativeScriptAssetManager::SanitizeScriptName(const std::string& requestedScriptName) {
	std::string sanitizedScriptName;
	sanitizedScriptName.reserve(requestedScriptName.size());

	for (const char letterValue : requestedScriptName) {
		const unsigned char letter = static_cast<unsigned char>(letterValue);
		const bool isNumber = letter >= '0' && letter <= '9';
		const bool isUpperAlphabet = letter >= 'A' && letter <= 'Z';
		const bool isLowerAlphabet = letter >= 'a' && letter <= 'z';
		const bool isUnderscore = letter == '_';

		if (isNumber || isUpperAlphabet || isLowerAlphabet || isUnderscore) {
			sanitizedScriptName.push_back(static_cast<char>(letter));
		}
	}

	if (!sanitizedScriptName.empty()) {
		const bool startsWithNumber = sanitizedScriptName.front() >= '0' && sanitizedScriptName.front() <= '9';

		if (startsWithNumber) {
			sanitizedScriptName.insert(sanitizedScriptName.begin(), '_');
		}
	}

	return sanitizedScriptName;
}

std::string EditorNativeScriptAssetManager::MakeHeaderText(
	const std::string& scriptName,
	EditorNativeScriptTemplate scriptTemplate) {
	std::string headerText = R"SCRIPT(#pragma once

#include "EditorNativeScript.h"

#include <string>

//================================================================
// __SCRIPT_NAME__ - GameObject へ追加する C++ Component
//================================================================

class __SCRIPT_NAME__ final : public EditorNativeScript {
public:
	__SCRIPT_NAME__();  // 公開変数と Input Action 関数を登録する。

	void Start(int32_t gameObjectId) override;
	void Update(int32_t gameObjectId, float deltaTime) override;
	void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) override;
	void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void Stop(int32_t gameObjectId) override;

private:
	float moveSpeed_ = 3.0f;  // Inspector から編集する移動速度。
	float jumpImpulse_ = 5.0f;  // Inspector から編集するジャンプの瞬間力。
	std::string startMessage_ = "__SCRIPT_NAME__::Start";  // Inspector から編集する開始ログ。
	std::string nextScenePath_;  // Space を押した時に開く .scene。空なら遷移しない。
	EditorScriptVector2 moveInput_{};  // OnMove が受けた入力を Update まで保持する。

	void OnMove(const EditorScriptInputActionContext& inputContext);
	void OnJump(const EditorScriptInputActionContext& inputContext);
	void OnFire(const EditorScriptInputActionContext& inputContext);
	void OnClick(const EditorScriptInputActionContext& inputContext);
	void OnValueChanged(const EditorScriptInputActionContext& inputContext);
__TEMPLATE_METHOD_DECLARATIONS__
};
)SCRIPT";
	ReplaceAll(headerText, "__SCRIPT_NAME__", scriptName);
	ReplaceAll(headerText, "__TEMPLATE_METHOD_DECLARATIONS__", MakeTemplateMethodDeclarations(scriptTemplate));
	return headerText;
}

std::string EditorNativeScriptAssetManager::MakeSourceText(
	const std::string& scriptName,
	EditorNativeScriptTemplate scriptTemplate) {
	std::string sourceText = R"SCRIPT(#include "__SCRIPT_NAME__.h"

#include <new>
#include <string>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す実行 API。

	struct ScriptInstance {
		explicit ScriptInstance(int32_t ownerGameObjectId)
			: gameObjectId(ownerGameObjectId) {
		}

		int32_t gameObjectId = -1;  // この Component を所有する GameObject。
		__SCRIPT_NAME__ script;  // Component ごとに独立したユーザー状態。
	};

	__SCRIPT_NAME__& GetMetadataState() {
		static __SCRIPT_NAME__ metadataState;  // Play 前の Inspector が型情報だけを取得する。
		return metadataState;
	}

	ScriptInstance* GetScriptInstance(void* instance) {
		return static_cast<ScriptInstance*>(instance);
	}
}

//================================================================
// ユーザーが編集する C++ Component 本体
//================================================================

__SCRIPT_NAME__::__SCRIPT_NAME__() {
	ExposeFloat("moveSpeed", "移動速度", moveSpeed_, 0.0f, 100.0f, 0.1f);
	ExposeFloat("jumpImpulse", "ジャンプ力", jumpImpulse_, 0.0f, 100.0f, 0.1f);
	ExposeString("startMessage", "開始メッセージ", startMessage_);
	ExposeScene("nextScenePath", "Space 遷移先 Scene", nextScenePath_);

	BindAction("OnMove", [this](const EditorScriptInputActionContext& inputContext) { OnMove(inputContext); });
	BindAction("OnJump", [this](const EditorScriptInputActionContext& inputContext) { OnJump(inputContext); });
	BindAction("OnFire", [this](const EditorScriptInputActionContext& inputContext) { OnFire(inputContext); });
	BindAction("OnClick", [this](const EditorScriptInputActionContext& inputContext) { OnClick(inputContext); });
	BindAction("OnValueChanged", [this](const EditorScriptInputActionContext& inputContext) { OnValueChanged(inputContext); });
__TEMPLATE_ACTION_BINDINGS__
}

void __SCRIPT_NAME__::Start(int32_t gameObjectId) {
	(void)gameObjectId;

	if (runtimeApi != nullptr) {
		runtimeApi->Log(startMessage_.c_str());
	}
}

void __SCRIPT_NAME__::Update(int32_t gameObjectId, float deltaTime) {
__TEMPLATE_UPDATE_BODY__
}

void __SCRIPT_NAME__::FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
__TEMPLATE_FIXED_UPDATE_BODY__
}

void __SCRIPT_NAME__::OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnCollisionEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void __SCRIPT_NAME__::OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnTriggerEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void __SCRIPT_NAME__::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	moveInput_ = {};
}

void __SCRIPT_NAME__::OnMove(const EditorScriptInputActionContext& inputContext) {
	moveInput_ = inputContext.phase == EditorScriptInputPhaseCanceled
		? EditorScriptVector2{}
		: inputContext.vector2Value;
}

void __SCRIPT_NAME__::OnJump(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	const EditorScriptVector3 jumpImpulse{0.0f, jumpImpulse_, 0.0f};
	Rigidbody{inputContext.gameObjectId}.AddImpulse(jumpImpulse);
}

void __SCRIPT_NAME__::OnFire(const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_FIRE_BODY__
}

void __SCRIPT_NAME__::OnClick(const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_CLICK_BODY__
}

void __SCRIPT_NAME__::OnValueChanged(const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_VALUE_CHANGED_BODY__
}
__TEMPLATE_METHOD_DEFINITIONS__

//================================================================
// Editor と C++ Component を接続する DLL ABI
//================================================================

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;
	EditorNativeScriptRuntime::SetRuntimeApi(api);
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void* EditorScript_CreateInstance(int32_t gameObjectId) {
	return new (std::nothrow) ScriptInstance(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_DestroyInstance(void* instance) {
	delete GetScriptInstance(instance);
}

extern "C" __declspec(dllexport) void EditorScript_StartInstance(void* instance) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		scriptInstance->script.Start(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) void EditorScript_UpdateInstance(void* instance, float deltaTime) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		scriptInstance->script.Update(scriptInstance->gameObjectId, deltaTime);
	}
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdateInstance(void* instance, float fixedDeltaTime) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		scriptInstance->script.FixedUpdate(scriptInstance->gameObjectId, fixedDeltaTime);
	}
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEventInstance(
	void* instance,
	const EditorScriptPhysicsEvent* physicsEvent) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance == nullptr || physicsEvent == nullptr) {
		return;
	}

	scriptInstance->script.DispatchPhysicsEvent(*physicsEvent);
}

extern "C" __declspec(dllexport) void EditorScript_OnAnimationEventInstance(
	void* instance,
	const EditorScriptAnimationEvent* animationEvent) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance == nullptr || animationEvent == nullptr) {
		return;
	}

	scriptInstance->script.OnAnimationEvent(*animationEvent);
}


extern "C" __declspec(dllexport) void EditorScript_StopInstance(void* instance) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		scriptInstance->script.Stop(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {
	return fieldDescriptor != nullptr && GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldValueInstance(
	void* instance,
	const char* fieldName,
	EditorScriptFieldValue* fieldValue) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && fieldValue != nullptr &&
		scriptInstance->script.GetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_SetFieldValueInstance(
	void* instance,
	const char* fieldName,
	const EditorScriptFieldValue* fieldValue) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && fieldValue != nullptr &&
		scriptInstance->script.SetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_InvokeActionInstance(
	void* instance,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);
	return scriptInstance != nullptr && inputContext != nullptr &&
		scriptInstance->script.InvokeAction(functionName, *inputContext);
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetActionCount() {
	return GetMetadataState().GetActionCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetActionName(
	int32_t actionIndex,
	char* actionName,
	int32_t actionNameCapacity) {
	return GetMetadataState().GetActionName(actionIndex, actionName, actionNameCapacity);
}
)SCRIPT";
	ReplaceAll(sourceText, "__SCRIPT_NAME__", scriptName);
	ReplaceAll(sourceText, "__TEMPLATE_UPDATE_BODY__", MakeTemplateUpdateBody(scriptTemplate));
	ReplaceAll(sourceText, "__TEMPLATE_FIXED_UPDATE_BODY__", MakeTemplateFixedUpdateBody(scriptTemplate));
	ReplaceAll(sourceText, "__TEMPLATE_FIRE_BODY__", MakeTemplateFireBody(scriptTemplate));
	ReplaceAll(sourceText, "__TEMPLATE_CLICK_BODY__", MakeTemplateClickBody(scriptTemplate));
	ReplaceAll(sourceText, "__TEMPLATE_VALUE_CHANGED_BODY__", MakeTemplateValueChangedBody(scriptTemplate));
	ReplaceAll(sourceText, "__TEMPLATE_ACTION_BINDINGS__", MakeTemplateActionBindings(scriptTemplate));
	ReplaceAll(
		sourceText,
		"__TEMPLATE_METHOD_DEFINITIONS__",
		MakeTemplateMethodDefinitions(scriptName, scriptTemplate));
	return sourceText;
}

std::string EditorNativeScriptAssetManager::MakeBuildScriptText(const std::string& scriptName, bool isDebug) {
	std::ostringstream buildScriptText;
	const char* configurationDirectory = isDebug ? "Debug" : "Release";
	const char* runtimeOption = isDebug ? "/MDd" : "/MD";
	const char* optimizationOption = isDebug ? "/Od /Zi" : "/O2";

	buildScriptText
		<< "@echo off\r\n"
		<< "setlocal\r\n"
		<< "\r\n"
		<< "pushd \"%~dp0\"\r\n"
		<< "set \"SCRIPT_DIR=%CD%\"\r\n"
		<< "set \"PROJECT_ROOT=%SCRIPT_DIR%\\..\\..\\..\"\r\n"
		<< "set \"VSWHERE=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\"\r\n"
		<< "for /f \"usebackq delims=\" %%i in (`\"%VSWHERE%\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i\r\n"
		<< "if \"%VSINSTALL%\"==\"\" exit /b 1\r\n"
		<< "call \"%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat\" -arch=x64\r\n"
		<< "if errorlevel 1 exit /b 1\r\n"
		<< "\r\n"
		<< "if not exist \"%SCRIPT_DIR%\\x64\\" << configurationDirectory
		<< "\" mkdir \"%SCRIPT_DIR%\\x64\\" << configurationDirectory << "\"\r\n"
		<< "\r\n"
		<< "cl /nologo /utf-8 /std:c++20 /EHsc " << runtimeOption << " " << optimizationOption
		<< " /LD /I \"%PROJECT_ROOT%\\Source\\Engine\\Core\" /I \"%PROJECT_ROOT%\" \"%SCRIPT_DIR%\\"
		<< scriptName << ".cpp\" /Fe:\"%SCRIPT_DIR%\\x64\\" << configurationDirectory << "\\"
		<< scriptName << ".dll\"\r\n"
		<< "set \"BUILD_RESULT=%ERRORLEVEL%\"\r\n"
		<< "popd\r\n"
		<< "exit /b %BUILD_RESULT%\r\n";

	return buildScriptText.str();
}

bool EditorNativeScriptAssetManager::WriteUtf8BomFile(const std::string& filePath, const std::string& text) {
	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	file.write(reinterpret_cast<const char*>(kUtf8Bom.data()), static_cast<std::streamsize>(kUtf8Bom.size()));
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return file.good();
}
