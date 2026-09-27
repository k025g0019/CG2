#include "EditorNativeScriptAssetManager.h"

#include "Source/Engine/Core/EditorNativeScript.h"  // 生成対象の公開 C++ API もエンジンビルド時に検証する。
#include "Source/Engine/Core/EngineVersion.h"  // Engine インストール先の候補を Version 付きで bat へ書き出す。

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

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

	// Script API ヘッダーを配っている Engine インストール先を返す。
	// Engine 配布物は CG2.exe と同じ場所へ ScriptApi フォルダーを持つ。
	std::string GetEngineDirectoryPath() {
		std::wstring executablePath(32768U, L'\0');
		const DWORD pathLength = GetModuleFileNameW(
			nullptr,
			executablePath.data(),
			static_cast<DWORD>(executablePath.size()));

		if (pathLength == 0U || pathLength >= executablePath.size()) {
			return {};
		}

		executablePath.resize(pathLength);
		return std::filesystem::path(executablePath).parent_path().string();
	}

	// 用途別の本体は「関数の中身」として書いてあるので、lambda へ入れる分だけ字下げを足す。
	std::string IndentBlock(const std::string& blockText) {
		std::string indentedText;
		indentedText.reserve(blockText.size() + blockText.size() / 8U);
		bool isLineStart = true;

		for (const char letter : blockText) {
			if (isLineStart && letter != '\n') {
				indentedText.push_back('\t');
			}

			indentedText.push_back(letter);
			isLineStart = letter == '\n';
		}

		// 末尾の改行は挿入先が持つため、空行が増えないよう取る。
		if (!indentedText.empty() && indentedText.back() == '\n') {
			indentedText.pop_back();
		}

		return indentedText;
	}

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

	std::string MakeTemplateActionBindings(EditorNativeScriptTemplate scriptTemplate) {
		if (scriptTemplate == EditorNativeScriptTemplate::RailEventController) {
			return R"SCRIPT(	BindAction("OnRailMarker", [this](const EditorScriptInputActionContext& inputContext) {
		if (EditorNativeScriptRuntime::GetRuntimeApi() == nullptr ||
			inputContext.phase != EditorScriptInputPhasePerformed) {
			return;
		}

		const std::string markerId = inputContext.payloadType == EditorScriptActionPayloadTypeString &&
			inputContext.payloadString != nullptr
			? inputContext.payloadString
			: "";
		const std::string message = "Rail Marker: " + markerId;
		EditorNativeScriptRuntime::GetRuntimeApi()->Log(message.c_str());

		// markerIdごとのゲーム固有処理はここへ追加する。
	});
)SCRIPT";
		}

		return {};
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
	result.generatedSourceFilePath =
		(scriptDirectoryPath / (result.sanitizedScriptName + ".Generated.cpp")).generic_string();
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
		MakeHeaderText(result.sanitizedScriptName));
	const bool isSourceWritten = WriteUtf8BomFile(
		result.sourceFilePath,
		MakeSourceText(result.sanitizedScriptName, scriptTemplate));
	const bool isGeneratedSourceWritten = WriteUtf8BomFile(
		result.generatedSourceFilePath,
		MakeGeneratedSourceText(result.sanitizedScriptName));
	const bool isDebugBuildFileWritten =
		WriteUtf8File(result.buildDebugFilePath, MakeBuildScriptText(result.sanitizedScriptName, true));
	const bool isReleaseBuildFileWritten =
		WriteUtf8File(result.buildReleaseFilePath, MakeBuildScriptText(result.sanitizedScriptName, false));

	if (!isHeaderWritten || !isSourceWritten || !isGeneratedSourceWritten ||
		!isDebugBuildFileWritten || !isReleaseBuildFileWritten) {
		result.message = "C++ スクリプト雛形の保存に失敗しました。";
		return result;
	}

	result.isSucceeded = true;
	result.message = isDebugBuild
		? "C++ スクリプトを生成しました。build_debug.bat を実行すると DLL が作られます。"
		: "C++ スクリプトを生成しました。build_release.bat を実行すると DLL が作られます。";
	return result;
}

EditorNativeScriptAssetResult EditorNativeScriptAssetManager::RefreshNativeScriptSupportFiles(
	const std::string& dllFilePath,
	bool isDebugBuild) {
	EditorNativeScriptAssetResult result{};
	const std::filesystem::path dllPath = std::filesystem::path(dllFilePath);
	result.sanitizedScriptName = SanitizeScriptName(dllPath.stem().string());

	if (result.sanitizedScriptName.empty() || dllPath.parent_path().empty()) {
		result.message = "C++ ScriptのDLL PathからScript名を取得できませんでした。";
		return result;
	}

	// resources/scripts/<ScriptName>/x64/<Configuration>/<ScriptName>.dll の3階層上。
	std::filesystem::path scriptDirectoryPath = dllPath;
	for (int32_t parentIndex = 0; parentIndex < 3; ++parentIndex) {
		if (scriptDirectoryPath.empty() || !scriptDirectoryPath.has_parent_path()) {
			result.message = "C++ ScriptのDLL Pathが標準Folder構成ではありません。";
			return result;
		}
		scriptDirectoryPath = scriptDirectoryPath.parent_path();
	}

	result.scriptDirectoryPath = scriptDirectoryPath.generic_string();
	result.headerFilePath =
		(scriptDirectoryPath / (result.sanitizedScriptName + ".h")).generic_string();
	result.sourceFilePath =
		(scriptDirectoryPath / (result.sanitizedScriptName + ".cpp")).generic_string();
	result.generatedSourceFilePath =
		(scriptDirectoryPath / (result.sanitizedScriptName + ".Generated.cpp")).generic_string();
	result.buildDebugFilePath = (scriptDirectoryPath / "build_debug.bat").generic_string();
	result.buildReleaseFilePath = (scriptDirectoryPath / "build_release.bat").generic_string();
	result.dllFilePath = dllFilePath;

	if (!std::filesystem::exists(result.sourceFilePath)) {
		result.message = "ユーザーScript本体が見つからないため、補助ファイルを更新できませんでした。";
		return result;
	}

	// .hと.cppはユーザー資産なので既存内容を絶対に上書きしない。
	// .Generated.cppとbuild batはEngine管理ファイルのため、現行APIに合わせて再生成する。
	const bool isGeneratedSourceWritten = WriteUtf8BomFile(
		result.generatedSourceFilePath,
		MakeGeneratedSourceText(result.sanitizedScriptName));
	const bool isDebugBuildFileWritten = WriteUtf8File(
		result.buildDebugFilePath,
		MakeBuildScriptText(result.sanitizedScriptName, true));
	const bool isReleaseBuildFileWritten = WriteUtf8File(
		result.buildReleaseFilePath,
		MakeBuildScriptText(result.sanitizedScriptName, false));

	if (!isGeneratedSourceWritten || !isDebugBuildFileWritten || !isReleaseBuildFileWritten) {
		result.message = "C++ Scriptの補助ファイル更新に失敗しました。ユーザーの.cppは変更していません。";
		return result;
	}

	result.isSucceeded = true;
	result.message = isDebugBuild
		? "Script補助ファイルを現行Engine用へ更新しました。Debug DLLをBuildします。"
		: "Script補助ファイルを現行Engine用へ更新しました。Release DLLをBuildします。";
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

std::string EditorNativeScriptAssetManager::MakeHeaderText(const std::string& scriptName) {
	std::string headerText = R"SCRIPT(#pragma once

#include "EditorNativeScript.h"

//================================================================
// __SCRIPT_NAME__ - GameObject へ追加する C++ Script
//
// このファイルは Engine が作る定型で、編集する必要はない。
// 公開変数は SCRIPT_FIELD_*、ライフサイクルは Bind*、Inspector へ出さない状態は
// MakeState を使い、すべて __SCRIPT_NAME__.cpp へ書く。
//================================================================

class __SCRIPT_NAME__ final : public Script {
public:
	__SCRIPT_NAME__();  // 必要な処理だけ .cpp 側で Bind する。
};
)SCRIPT";
	ReplaceAll(headerText, "__SCRIPT_NAME__", scriptName);
	return headerText;
}

std::string EditorNativeScriptAssetManager::MakeSourceText(
	const std::string& scriptName,
	EditorNativeScriptTemplate scriptTemplate) {
	if (scriptTemplate == EditorNativeScriptTemplate::Empty) {
		std::string emptySourceText = R"SCRIPT(#include "__SCRIPT_NAME__.h"

//================================================================
// ユーザーが編集する C++ Script 本体
//
// 追記はこの .cpp だけで完結する。.h へ宣言を増やす必要はない。
//================================================================

// 公開変数はここで宣言する。Inspector へ並び、FieldFloat で読み、SetFieldFloat で書く。
// 例:
// SCRIPT_FIELD_FLOAT(moveSpeed, "移動速度", 3.0f, 0.0f, 100.0f, 0.1f)

__SCRIPT_NAME__::__SCRIPT_NAME__() {
	// Inspector へ出さない状態は MakeState で持つ。Script インスタンスごとに 1 つ作られる。
	// 例:
	// const auto elapsedTime = MakeState<float>(0.0f);

	// 必要な処理だけ登録する。使わないライフサイクルの空実装は不要。
	// 例:
	// BindUpdate([this](float deltaTime) {
	// 	(void)deltaTime;
	// });
	//
	// BindCollisionEnter([this](const EditorScriptPhysicsEvent& physicsEvent) {
	// 	(void)physicsEvent;
	// });
	//
	// 任意Actionも同じ場所へ登録できる。
	// 例:
	// BindAction("OnFire", [this](const EditorScriptInputActionContext& inputContext) {
	// 	if (inputContext.phase != EditorScriptInputPhasePerformed) {
	// 		return;
	// 	}
	//
	// 	// 発射処理などを書く。
	// });
}
)SCRIPT";
		ReplaceAll(emptySourceText, "__SCRIPT_NAME__", scriptName);
		return emptySourceText;
	}

	std::string sourceText = R"SCRIPT(#include "__SCRIPT_NAME__.h"

#include <string>

//================================================================
// ユーザーが編集する C++ Component 本体
//
// 公開変数は SCRIPT_FIELD_*、ライフサイクルは Bind*、Inspector へ出さない状態は
// MakeState で持つ。追記も削除もこの .cpp だけで済み、.h は触らない。
//================================================================

SCRIPT_FIELD_FLOAT(moveSpeed, "移動速度", 3.0f, 0.0f, 100.0f, 0.1f)
SCRIPT_FIELD_FLOAT(jumpImpulse, "ジャンプ力", 5.0f, 0.0f, 100.0f, 0.1f)
SCRIPT_FIELD_STRING(startMessage, "開始メッセージ", "__SCRIPT_NAME__::Start")
SCRIPT_FIELD_SCENE(nextScenePath, "Space 遷移先 Scene", "")

__SCRIPT_NAME__::__SCRIPT_NAME__() {
	// OnMove が受けた入力を Update まで保持する。
	const std::shared_ptr<EditorScriptVector2> moveInput = MakeState<EditorScriptVector2>();

	BindStart([this]() {
		if (runtimeApi != nullptr) {
			runtimeApi->Log(startMessage_.c_str());
		}
	});

	BindUpdate([this, moveInput](float deltaTime) {
		const int32_t gameObjectId = GetGameObjectId();
__TEMPLATE_UPDATE_BODY__
	});

	BindFixedUpdate([this, moveInput](float fixedDeltaTime) {
		const int32_t gameObjectId = GetGameObjectId();
__TEMPLATE_FIXED_UPDATE_BODY__
	});

	BindCollisionEnter([this](const EditorScriptPhysicsEvent& physicsEvent) {
		if (runtimeApi != nullptr) {
			const std::string message = "OnCollisionEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
			runtimeApi->Log(message.c_str());
		}
	});

	BindTriggerEnter([this](const EditorScriptPhysicsEvent& physicsEvent) {
		if (runtimeApi != nullptr) {
			const std::string message = "OnTriggerEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
			runtimeApi->Log(message.c_str());
		}
	});

	BindStop([moveInput]() {
		*moveInput = {};
	});

	BindAction("OnMove", [moveInput](const EditorScriptInputActionContext& inputContext) {
		*moveInput = inputContext.phase == EditorScriptInputPhaseCanceled
			? EditorScriptVector2{}
			: inputContext.vector2Value;
	});

	BindAction("OnJump", [this](const EditorScriptInputActionContext& inputContext) {
		if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
			return;
		}

		const EditorScriptVector3 jumpVelocity{0.0f, jumpImpulse_, 0.0f};
		Rigidbody{inputContext.gameObjectId}.AddImpulse(jumpVelocity);
	});

	BindAction("OnFire", [this](const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_FIRE_BODY__
	});

	BindAction("OnClick", [this](const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_CLICK_BODY__
	});

	BindAction("OnValueChanged", [this](const EditorScriptInputActionContext& inputContext) {
__TEMPLATE_VALUE_CHANGED_BODY__
	});
__TEMPLATE_ACTION_BINDINGS__
}
)SCRIPT";
	ReplaceAll(sourceText, "__SCRIPT_NAME__", scriptName);
	ReplaceAll(sourceText, "__TEMPLATE_UPDATE_BODY__", IndentBlock(MakeTemplateUpdateBody(scriptTemplate)));
	ReplaceAll(
		sourceText,
		"__TEMPLATE_FIXED_UPDATE_BODY__",
		IndentBlock(MakeTemplateFixedUpdateBody(scriptTemplate)));
	ReplaceAll(sourceText, "__TEMPLATE_FIRE_BODY__", IndentBlock(MakeTemplateFireBody(scriptTemplate)));
	ReplaceAll(sourceText, "__TEMPLATE_CLICK_BODY__", IndentBlock(MakeTemplateClickBody(scriptTemplate)));
	ReplaceAll(
		sourceText,
		"__TEMPLATE_VALUE_CHANGED_BODY__",
		IndentBlock(MakeTemplateValueChangedBody(scriptTemplate)));
	ReplaceAll(sourceText, "__TEMPLATE_ACTION_BINDINGS__", MakeTemplateActionBindings(scriptTemplate));
	// 用途別の本体は従来のメンバー名で書いてあるので、Field と MakeState の参照へ置き換える。
	ReplaceAll(sourceText, "moveInput_", "(*moveInput)");
	ReplaceAll(sourceText, "moveSpeed_", "FieldFloat(\"moveSpeed\")");
	ReplaceAll(sourceText, "jumpImpulse_", "FieldFloat(\"jumpImpulse\")");
	ReplaceAll(sourceText, "startMessage_", "FieldString(\"startMessage\")");
	ReplaceAll(sourceText, "nextScenePath_", "FieldString(\"nextScenePath\")");
	ReplaceAll(sourceText, "runtimeApi", "EditorNativeScriptRuntime::GetRuntimeApi()");
	return sourceText;
}

std::string EditorNativeScriptAssetManager::MakeGeneratedSourceText(const std::string& scriptName) {
	std::string generatedSourceText = R"SCRIPT(// このファイルは Engine が自動生成します。ユーザーコードは __SCRIPT_NAME__.cpp へ記述してください。
#include "__SCRIPT_NAME__.h"

#include <new>
#include <type_traits>

namespace {
	template <typename ScriptType>
	void AttachScriptToGameObject(ScriptType& script, int32_t gameObjectId) {
		if constexpr (std::is_base_of_v<Script, ScriptType>) {
			script.AttachToGameObject(gameObjectId);
		}
	}

	struct ScriptInstance {
		explicit ScriptInstance(int32_t ownerGameObjectId)
			: gameObjectId(ownerGameObjectId) {

			AttachScriptToGameObject(script, ownerGameObjectId);
		}

		int32_t gameObjectId = -1;
		__SCRIPT_NAME__ script;
	};

	__SCRIPT_NAME__& GetMetadataState() {
		static __SCRIPT_NAME__ metadataState;
		return metadataState;
	}

	ScriptInstance* GetScriptInstance(void* instance) {
		return static_cast<ScriptInstance*>(instance);
	}
}

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* runtimeApi) {

	// Runtime APIは末尾追加のため、新しいEngineから古いScript API範囲を使うことは安全。
	if (apiVersion < kEditorScriptApiVersion || runtimeApi == nullptr) {
		return false;
	}

	EditorNativeScriptRuntime::SetRuntimeApi(runtimeApi);
	return true;
}

extern "C" __declspec(dllexport) uint32_t EditorScript_GetRequiredApiVersion() {
	return kEditorScriptApiVersion;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);
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
		static_cast<EditorNativeScript&>(scriptInstance->script).Start(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) void EditorScript_UpdateInstance(void* instance, float deltaTime) {
	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).Update(
			scriptInstance->gameObjectId,
			deltaTime);
	}
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdateInstance(
	void* instance,
	float fixedDeltaTime) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance != nullptr) {
		static_cast<EditorNativeScript&>(scriptInstance->script).FixedUpdate(
			scriptInstance->gameObjectId,
			fixedDeltaTime);
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

extern "C" __declspec(dllexport) void EditorScript_OnWireEventInstance(
	void* instance,
	const EditorScriptWireEvent* wireEvent) {

	ScriptInstance* scriptInstance = GetScriptInstance(instance);

	if (scriptInstance == nullptr || wireEvent == nullptr) {
		return;
	}

	scriptInstance->script.DispatchWireEvent(*wireEvent);
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
		static_cast<EditorNativeScript&>(scriptInstance->script).Stop(scriptInstance->gameObjectId);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {

	return fieldDescriptor != nullptr &&
		GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
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
	ReplaceAll(generatedSourceText, "__SCRIPT_NAME__", scriptName);
	return generatedSourceText;
}

std::string EditorNativeScriptAssetManager::MakeBuildScriptText(const std::string& scriptName, bool isDebug) {
	std::ostringstream buildScriptText;
	const char* configurationDirectory = isDebug ? "Debug" : "Release";
	const char* runtimeOption = isDebug ? "/MDd" : "/MD";
	const char* optimizationOption = isDebug ? "/Od /Zi" : "/O2";
	const std::string engineDirectoryPath = GetEngineDirectoryPath();
	const std::string engineVersionText = GetManoEngineDisplayVersion();

	buildScriptText
		<< "@echo off\r\n"
		<< "setlocal\r\n"
		<< "\r\n"
		<< "pushd \"%~dp0\"\r\n"
		<< "set \"SCRIPT_DIR=%CD%\"\r\n"
		<< "set \"PROJECT_ROOT=%SCRIPT_DIR%\\..\\..\\..\"\r\n"
		<< "set \"ENGINE_DIR=" << engineDirectoryPath << "\"\r\n"
		<< "set \"ENGINE_VERSION=" << engineVersionText << "\"\r\n"
		<< "\r\n"
		// Script API ヘッダーの置き場所は Engine の入れ方で変わるので、候補を順に探す。
		// Engine 配布物は CG2.exe と同じ場所の ScriptApi フォルダーへ入れている。
		<< "set \"SCRIPT_API_DIR=\"\r\n"
		<< "for %%D in (\r\n"
		<< "  \"%MANOENGINE_SCRIPT_API%\"\r\n"
		<< "  \"%PROJECT_ROOT%\\Source\\Engine\\Core\"\r\n"
		<< "  \"%ENGINE_DIR%\\ScriptApi\"\r\n"
		<< "  \"%PROJECT_ROOT%\\PortableEngine\\%ENGINE_VERSION%\\ScriptApi\"\r\n"
		<< "  \"%PROJECT_ROOT%\\PortableEngine\\ScriptApi\"\r\n"
		<< "  \"%LOCALAPPDATA%\\ManoEngine\\Engines\\%ENGINE_VERSION%\\ScriptApi\"\r\n"
		<< "  \"%MANOENGINE_INSTALL_ROOT%\\Engines\\%ENGINE_VERSION%\\ScriptApi\"\r\n"
		<< "  \"C:\\ManoHub\\Engines\\%ENGINE_VERSION%\\ScriptApi\"\r\n"
		<< ") do if not defined SCRIPT_API_DIR if exist \"%%~D\\EditorNativeScript.h\" set \"SCRIPT_API_DIR=%%~D\"\r\n"
		<< "\r\n"
		<< "if not defined SCRIPT_API_DIR (\r\n"
		<< "  echo [error] EditorNativeScript.h was not found.\r\n"
		<< "  echo         Set MANOENGINE_SCRIPT_API to the ScriptApi folder of your ManoEngine install.\r\n"
		<< "  popd\r\n"
		<< "  exit /b 1\r\n"
		<< ")\r\n"
		<< "\r\n"
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
		<< " /LD /I \"%SCRIPT_API_DIR%\" /I \"%PROJECT_ROOT%\" \"%SCRIPT_DIR%\\"
		<< scriptName << ".cpp\" \"%SCRIPT_DIR%\\" << scriptName
		<< ".Generated.cpp\" /Fe:\"%SCRIPT_DIR%\\x64\\" << configurationDirectory << "\\"
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

// cmd.exe は先頭の UTF-8 BOM を命令として読んでしまうため、bat だけ BOM なしで保存する。
bool EditorNativeScriptAssetManager::WriteUtf8File(const std::string& filePath, const std::string& text) {
	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return file.good();
}
