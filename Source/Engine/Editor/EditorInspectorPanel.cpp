#include "EditorInspectorPanel.h"

#pragma warning(disable : 5045)
#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorNativeScriptAssetManager.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace EditorSharedState;

namespace {
	constexpr auto kEditorPrefabPath = "resources/editorPrefab.prefab";  // Prefab 保存 / 生成の固定パス
	constexpr float kPropertyLabelWidth = 118.0f;  // Unity 風に左側へ置く項目名の幅
	constexpr float kAxisLabelWidth = 16.0f;  // X / Y / Z の軸名だけを表示する幅
	constexpr float kWideButtonWidth = 230.0f;  // Inspector 下部の横長ボタン幅
	constexpr float kRadianToDegree = 57.2957795f;  // 内部のラジアン値を Inspector 表示用の度数へ変換する係数。
	constexpr float kDegreeToRadian = 0.0174532924f;  // Inspector で入力された度数を内部用ラジアンへ戻す係数。
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // テキスト系アセットを UTF-8 BOM 付きで保存する。

#ifdef _DEBUG
	constexpr bool kIsDebugEditorBuild = true;  // 実行中エンジンが Debug なら Script も Debug DLL を基準にする。
	constexpr const char* kCurrentScriptConfigName = "Debug";
	constexpr const char* kCurrentBuildScriptName = "build_debug.bat";
	constexpr const char* kCurrentBuildButtonLabel = "Debug DLL をビルド";
	constexpr const char* kCurrentExpectedDllLabel = "想定 Debug DLL";
	constexpr const char* kCurrentHowToText = "1. C++ スクリプトを作成 2. C++ を開く 3. build_debug.bat を実行 4. Play 中は DLL 更新で自動再読み込み";
#else
	constexpr bool kIsDebugEditorBuild = false;  // 実行中エンジンが Release なら Script も Release DLL を基準にする。
	constexpr const char* kCurrentScriptConfigName = "Release";
	constexpr const char* kCurrentBuildScriptName = "build_release.bat";
	constexpr const char* kCurrentBuildButtonLabel = "Release DLL をビルド";
	constexpr const char* kCurrentExpectedDllLabel = "想定 Release DLL";
	constexpr const char* kCurrentHowToText = "1. C++ スクリプトを作成 2. C++ を開く 3. build_release.bat を実行 4. Play 中は DLL 更新で自動再読み込み";
#endif

#pragma warning(push)
#pragma warning(disable : 4820)
	struct DikEntry {
		const char* name;
		int32_t dikCode;
	};
#pragma warning(pop)

	const DikEntry kDikEntries[] = {
		{"Escape", 0x01},
		{"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05},
		{"5", 0x06}, {"6", 0x07}, {"7", 0x08}, {"8", 0x09},
		{"9", 0x0A}, {"0", 0x0B},
		{"Minus", 0x0C}, {"Equals", 0x0D},
		{"Backspace", 0x0E}, {"Tab", 0x0F},
		{"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13},
		{"T", 0x14}, {"Y", 0x15}, {"U", 0x16}, {"I", 0x17},
		{"O", 0x18}, {"P", 0x19},
		{"LBracket", 0x1A}, {"RBracket", 0x1B},
		{"Enter", 0x1C},
		{"LCtrl", 0x1D},
		{"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21},
		{"G", 0x22}, {"H", 0x23}, {"J", 0x24}, {"K", 0x25},
		{"L", 0x26},
		{"Semicolon", 0x27}, {"Apostrophe", 0x28},
		{"Grave", 0x29},
		{"LShift", 0x2A}, {"Backslash", 0x2B},
		{"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E}, {"V", 0x2F},
		{"B", 0x30}, {"N", 0x31}, {"M", 0x32},
		{"Comma", 0x33}, {"Period", 0x34}, {"Slash", 0x35},
		{"RShift", 0x36},
		{"Multiply", 0x37}, {"LAlt", 0x38}, {"Space", 0x39},
		{"CapsLock", 0x3A},
		{"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E},
		{"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},
		{"F9", 0x43}, {"F10", 0x44}, {"F11", 0x45}, {"F12", 0x46},
		{"Num7", 0x47}, {"Num8", 0x48}, {"Num9", 0x49},
		{"Subtract", 0x4A},
		{"Num4", 0x4B}, {"Num5", 0x4C}, {"Num6", 0x4D},
		{"Add", 0x4E},
		{"Num1", 0x4F}, {"Num2", 0x50}, {"Num3", 0x51},
		{"Num0", 0x52}, {"Decimal", 0x53},
		{"F13", 0x57},
		{"F17", 0x7A}, {"F18", 0x7B}, {"F19", 0x7C}, {"F20", 0x7D},
		{"Up", 0xC8}, {"Down", 0xD0}, {"Left", 0xCB}, {"Right", 0xCD},
	};

	int32_t DikCodeToIndex(int32_t dikCode) {
		for (int32_t i = 0; i < static_cast<int32_t>(_countof(kDikEntries)); ++i) {
			if (kDikEntries[i].dikCode == dikCode) return i;
		}
		return -1;
	}

	struct InputActionsAssetEntry {
		std::string actionMapName;  // ActionMap 名。例: Player
		std::string actionName;  // Action 名。例: Submit
		bool isVector2 = false;  // true なら 2DVector、false なら Button
		bool usesMouse = false;  // Button Action の時にマウスを使うなら true
		std::string keyName = "Space";  // Button Key の Path 名
		std::string mouseButtonName = "LeftButton";  // Button Mouse の Path 名
		std::string upKeyName = "W";  // 2DVector Composite の Up
		std::string downKeyName = "S";  // 2DVector Composite の Down
		std::string leftKeyName = "A";  // 2DVector Composite の Left
		std::string rightKeyName = "D";  // 2DVector Composite の Right
	};

	std::vector<std::string> SplitPipeText(const std::string& line) {
		std::vector<std::string> elements;
		std::stringstream stream(line);
		std::string element;
		while (std::getline(stream, element, '|')) {
			elements.push_back(element);
		}

		return elements;
	}

	std::vector<InputActionsAssetEntry> LoadInputActionsEntries(const std::string& filePath) {
		std::vector<InputActionsAssetEntry> entries;
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open()) {
			return entries;
		}

		std::string line;
		bool isFirstLine = true;
		while (std::getline(file, line)) {
			if (isFirstLine &&
				line.size() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xEFu &&
				static_cast<unsigned char>(line[1]) == 0xBBu &&
				static_cast<unsigned char>(line[2]) == 0xBFu) {
				line.erase(0, 3);
			}
			isFirstLine = false;

			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			if (line.empty() || line[0] == '#') {
				continue;
			}

			const std::vector<std::string> elements = SplitPipeText(line);
			if (elements.size() < 6 || elements[0] != "Action") {
				continue;
			}

			InputActionsAssetEntry entry{};
			entry.actionMapName = elements[1];
			entry.actionName = elements[2];
			entry.isVector2 = elements[3] == "Vector2";

			if (entry.isVector2 && elements[4] == "2DVector" && elements.size() >= 9) {
				entry.upKeyName = elements[5];
				entry.downKeyName = elements[6];
				entry.leftKeyName = elements[7];
				entry.rightKeyName = elements[8];
			}
			else if (!entry.isVector2) {
				entry.usesMouse = elements[4] == "Mouse";
				if (entry.usesMouse) {
					entry.mouseButtonName = elements[5];
				}
				else {
					entry.keyName = elements[5];
				}
			}

			entries.push_back(entry);
		}

		return entries;
	}

	std::string BuildInputActionsFileText(const std::vector<InputActionsAssetEntry>& entries) {
		std::ostringstream fileText;
		fileText
			<< "# CG2 PlayerInput Actions\r\n"
			<< "# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n";

		for (const InputActionsAssetEntry& entry : entries) {
			if (entry.isVector2) {
				fileText
					<< "Action|" << entry.actionMapName
					<< "|" << entry.actionName
					<< "|Vector2|2DVector|"
					<< entry.upKeyName
					<< "|" << entry.downKeyName
					<< "|" << entry.leftKeyName
					<< "|" << entry.rightKeyName
					<< "\r\n";
			}
			else {
				fileText
					<< "Action|" << entry.actionMapName
					<< "|" << entry.actionName
					<< "|Button|" << (entry.usesMouse ? "Mouse" : "Key")
					<< "|" << (entry.usesMouse ? entry.mouseButtonName : entry.keyName)
					<< "\r\n";
			}
		}

		return fileText.str();
	}

	bool SaveInputActionsEntries(const std::string& filePath, const std::vector<InputActionsAssetEntry>& entries) {
		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}

		const std::string fileText = BuildInputActionsFileText(entries);
		file.write(reinterpret_cast<const char*>(kUtf8Bom), static_cast<std::streamsize>(sizeof(kUtf8Bom)));
		file.write(fileText.data(), static_cast<std::streamsize>(fileText.size()));
		return file.good();
	}

	const char* const kMouseButtonNames[] = {
		"LeftButton",
		"RightButton",
		"MiddleButton",
	};

	struct ComponentAddEntry {
		const char* categoryName;  // Unity 風 Popup のカテゴリ名
		const char* displayName;  // 追加 Popup に表示する日本語名
		EditorComponentType type;  // 実際に追加する Component 種類
		int32_t reservedPadding = 0;  // /Wall の構造体 padding 警告を明示的な未使用領域で防ぐ
	};

	const ComponentAddEntry kComponentAddEntries[] = {
		{"基本", "トランスフォーム", EditorComponentType::Transform},
		{"基本", "レクトトランスフォーム", EditorComponentType::RectTransform},
		{"基本", "キャンバス", EditorComponentType::Canvas},
		{"基本", "ゲームオブジェクト + スクリプト", EditorComponentType::Script},
		{"基本", "モノビヘイビア", EditorComponentType::MonoBehaviour},
		{"描画・レンダリング", "メッシュフィルター", EditorComponentType::MeshFilter},
		{"描画・レンダリング", "メッシュレンダラー", EditorComponentType::ModelRenderer},
		{"描画・レンダリング", "スキンメッシュレンダラー", EditorComponentType::SkinnedMeshRenderer},
		{"描画・レンダリング", "スプライトレンダラー", EditorComponentType::SpriteRenderer},
		{"描画・レンダリング", "ラインレンダラー", EditorComponentType::LineRenderer},
		{"描画・レンダリング", "トレイルレンダラー", EditorComponentType::TrailRenderer},
		{"描画・レンダリング", "ビルボードレンダラー", EditorComponentType::BillboardRenderer},
		{"描画・レンダリング", "キャンバスレンダラー", EditorComponentType::CanvasRenderer},
		{"描画・レンダリング", "パーティクルシステムレンダラー", EditorComponentType::ParticleSystemRenderer},
		{"カメラ", "カメラ", EditorComponentType::Camera},
		{"カメラ", "オーディオリスナー", EditorComponentType::AudioListener},
		{"カメラ", "フレアレイヤー", EditorComponentType::FlareLayer},
		{"カメラ", "Cinemachine カメラ", EditorComponentType::CinemachineCamera},
		{"ライト・環境", "ライト", EditorComponentType::Light},
		{"ライト・環境", "リフレクションプローブ", EditorComponentType::ReflectionProbe},
		{"ライト・環境", "ライトプローブグループ", EditorComponentType::LightProbeGroup},
		{"ライト・環境", "ライトプローブプロキシボリューム", EditorComponentType::LightProbeProxyVolume},
		{"ライト・環境", "ボリューム", EditorComponentType::Volume},
		{"3D物理", "リジッドボディ", EditorComponentType::RigidBody},
		{"3D物理", "箱の当たり判定", EditorComponentType::BoxCollider},
		{"3D物理", "球の当たり判定", EditorComponentType::SphereCollider},
		{"3D物理", "カプセル当たり判定", EditorComponentType::CapsuleCollider},
		{"3D物理", "メッシュ当たり判定", EditorComponentType::MeshCollider},
		{"3D物理", "地形の当たり判定", EditorComponentType::TerrainCollider},
		{"3D物理", "車輪の当たり判定", EditorComponentType::WheelCollider},
		{"3D物理", "キャラクターコントローラー", EditorComponentType::CharacterController},
		{"3D物理", "コンスタントフォース", EditorComponentType::ConstantForce},
		{"3D物理", "ヒンジジョイント", EditorComponentType::HingeJoint},
		{"3D物理", "固定ジョイント", EditorComponentType::FixedJoint},
		{"3D物理", "スプリングジョイント", EditorComponentType::SpringJoint},
		{"3D物理", "コンフィギュラブルジョイント", EditorComponentType::ConfigurableJoint},
		{"3D物理", "キャラクタージョイント", EditorComponentType::CharacterJoint},
		{"2D物理", "リジッドボディ 2D", EditorComponentType::RigidBody2D},
		{"2D物理", "四角の当たり判定 2D", EditorComponentType::BoxCollider2D},
		{"2D物理", "円の当たり判定 2D", EditorComponentType::CircleCollider2D},
		{"2D物理", "カプセル当たり判定 2D", EditorComponentType::CapsuleCollider2D},
		{"2D物理", "多角形の当たり判定 2D", EditorComponentType::PolygonCollider2D},
		{"2D物理", "線の当たり判定 2D", EditorComponentType::EdgeCollider2D},
		{"2D物理", "複合当たり判定 2D", EditorComponentType::CompositeCollider2D},
		{"2D物理", "タイルマップ当たり判定 2D", EditorComponentType::TilemapCollider2D},
		{"2D物理", "カスタム当たり判定 2D", EditorComponentType::CustomCollider2D},
		{"2D物理", "ディスタンスジョイント 2D", EditorComponentType::DistanceJoint2D},
		{"2D物理", "ヒンジジョイント 2D", EditorComponentType::HingeJoint2D},
		{"2D物理", "スプリングジョイント 2D", EditorComponentType::SpringJoint2D},
		{"2D物理", "固定ジョイント 2D", EditorComponentType::FixedJoint2D},
		{"2D物理", "スライダージョイント 2D", EditorComponentType::SliderJoint2D},
		{"2D物理", "ホイールジョイント 2D", EditorComponentType::WheelJoint2D},
		{"2D物理", "プラットフォームエフェクター 2D", EditorComponentType::PlatformEffector2D},
		{"2D物理", "サーフェスエフェクター 2D", EditorComponentType::SurfaceEffector2D},
		{"2D物理", "エリアエフェクター 2D", EditorComponentType::AreaEffector2D},
		{"2D物理", "ポイントエフェクター 2D", EditorComponentType::PointEffector2D},
		{"2D物理", "浮力エフェクター 2D", EditorComponentType::BuoyancyEffector2D},
		{"アニメーション", "アニメーター", EditorComponentType::Animator},
		{"アニメーション", "アニメーション", EditorComponentType::Animation},
		{"アニメーション", "アバターマスク", EditorComponentType::AvatarMask},
		{"アニメーション", "プレイアブルディレクター", EditorComponentType::PlayableDirector},
		{"アニメーション", "エイム制約", EditorComponentType::AimConstraint},
		{"アニメーション", "ルックアット制約", EditorComponentType::LookAtConstraint},
		{"アニメーション", "親制約", EditorComponentType::ParentConstraint},
		{"アニメーション", "位置制約", EditorComponentType::PositionConstraint},
		{"アニメーション", "回転制約", EditorComponentType::RotationConstraint},
		{"アニメーション", "スケール制約", EditorComponentType::ScaleConstraint},
		{"オーディオ", "オーディオソース", EditorComponentType::AudioSource},
		{"オーディオ", "オーディオリスナー", EditorComponentType::AudioListener},
		{"オーディオ", "オーディオリバーブゾーン", EditorComponentType::AudioReverbZone},
		{"オーディオ", "オーディオローパスフィルター", EditorComponentType::AudioLowPassFilter},
		{"オーディオ", "オーディオハイパスフィルター", EditorComponentType::AudioHighPassFilter},
		{"オーディオ", "オーディオエコーフィルター", EditorComponentType::AudioEchoFilter},
		{"オーディオ", "オーディオディストーションフィルター", EditorComponentType::AudioDistortionFilter},
		{"オーディオ", "オーディオリバーブフィルター", EditorComponentType::AudioReverbFilter},
		{"オーディオ", "オーディオコーラスフィルター", EditorComponentType::AudioChorusFilter},
		{"UI", "キャンバス", EditorComponentType::Canvas},
		{"UI", "キャンバススケーラー", EditorComponentType::CanvasScaler},
		{"UI", "グラフィックレイキャスター", EditorComponentType::GraphicRaycaster},
		{"UI", "イメージ", EditorComponentType::Image},
		{"UI", "Raw イメージ", EditorComponentType::RawImage},
		{"UI", "テキスト", EditorComponentType::Text},
		{"UI", "TextMeshPro UGUI", EditorComponentType::TextMeshProUGUI},
		{"UI", "ボタン", EditorComponentType::Button},
		{"UI", "トグル", EditorComponentType::Toggle},
		{"UI", "スライダー", EditorComponentType::Slider},
		{"UI", "スクロールバー", EditorComponentType::Scrollbar},
		{"UI", "ドロップダウン", EditorComponentType::Dropdown},
		{"UI", "TMP ドロップダウン", EditorComponentType::TMPDropdown},
		{"UI", "入力フィールド", EditorComponentType::InputField},
		{"UI", "TMP 入力フィールド", EditorComponentType::TMPInputField},
		{"UI", "スクロールレクト", EditorComponentType::ScrollRect},
		{"UI", "マスク", EditorComponentType::Mask},
		{"UI", "レクトマスク 2D", EditorComponentType::RectMask2D},
		{"UI", "水平レイアウトグループ", EditorComponentType::HorizontalLayoutGroup},
		{"UI", "垂直レイアウトグループ", EditorComponentType::VerticalLayoutGroup},
		{"UI", "グリッドレイアウトグループ", EditorComponentType::GridLayoutGroup},
		{"UI", "コンテンツサイズフィッター", EditorComponentType::ContentSizeFitter},
		{"UI", "アスペクト比フィッター", EditorComponentType::AspectRatioFitter},
		{"UI", "レイアウトエレメント", EditorComponentType::LayoutElement},
		{"入力・イベント", "イベントシステム", EditorComponentType::EventSystem},
		{"入力・イベント", "スタンドアロン入力モジュール", EditorComponentType::StandaloneInputModule},
		{"入力・イベント", "Input System UI 入力モジュール", EditorComponentType::InputSystemUIInputModule},
		{"入力・イベント", "プレイヤー入力", EditorComponentType::PlayerInput},
		{"入力・イベント", "プレイヤー入力マネージャー", EditorComponentType::PlayerInputManager},
		{"入力・イベント", "タッチ入力モジュール", EditorComponentType::TouchInputModule},
		{"入力・イベント", "入力", EditorComponentType::Input},
		{"ゲームプレイ", "ローカル移動", EditorComponentType::LocalMove},
		{"ゲームプレイ", "ローリング移動", EditorComponentType::RollingMove},
		{"ナビゲーション", "NavMesh エージェント", EditorComponentType::NavigationAgent},
		{"ナビゲーション", "NavMesh 障害物", EditorComponentType::NavMeshObstacle},
		{"ナビゲーション", "NavMesh サーフェス", EditorComponentType::NavMeshSurface},
		{"ナビゲーション", "NavMesh モディファイア", EditorComponentType::NavMeshModifier},
		{"ナビゲーション", "NavMesh モディファイアボリューム", EditorComponentType::NavMeshModifierVolume},
		{"ナビゲーション", "NavMesh リンク", EditorComponentType::NavMeshLink},
		{"AI", "行動ツリー", EditorComponentType::AIBehaviorTree},
		{"AI", "共有データ（Blackboard）", EditorComponentType::AIBehaviorBlackboard},
		{"AI", "条件分岐（Selector）", EditorComponentType::AIBehaviorSelector},
		{"AI", "順番実行（Sequence）", EditorComponentType::AIBehaviorSequence},
		{"AI", "実行処理（Task）", EditorComponentType::AIBehaviorTask},
		{"AI", "条件装飾（Decorator）", EditorComponentType::AIBehaviorDecorator},
		{"AI", "状態制御", EditorComponentType::AIStateMachine},
		{"AI", "状態", EditorComponentType::AIState},
		{"AI", "状態遷移", EditorComponentType::AIStateTransition},
		{"AI", "目標計画", EditorComponentType::AIGoapPlanner},
		{"AI", "目標条件", EditorComponentType::AIGoapGoal},
		{"AI", "計画行動", EditorComponentType::AIGoapAction},
		{"AI", "世界状態", EditorComponentType::AIGoapWorldState},
		{"AI", "タスク計画", EditorComponentType::AIHtnPlanner},
		{"AI", "タスク領域", EditorComponentType::AIHtnDomain},
		{"AI", "タスク", EditorComponentType::AIHtnTask},
		{"AI", "タスク分解", EditorComponentType::AIHtnMethod},
		{"AI", "経路探索エージェント", EditorComponentType::AIPathfindingAgent},
		{"AI", "グリッド経路", EditorComponentType::AIMicroPatherGrid},
		{"AI", "ナビメッシュ生成", EditorComponentType::AIRecastNavMeshBuilder},
		{"AI", "群衆エージェント", EditorComponentType::AIRecastCrowdAgent},
		{"AI", "経路要求", EditorComponentType::AIPathRequest},
		{"AI", "動的障害物", EditorComponentType::AIDynamicObstacle},
		{"AI", "操舵エージェント", EditorComponentType::AISteeringAgent},
		{"AI", "接近操舵", EditorComponentType::AISeekSteering},
		{"AI", "逃走操舵", EditorComponentType::AIFleeSteering},
		{"AI", "到着操舵", EditorComponentType::AIArriveSteering},
		{"AI", "追跡操舵", EditorComponentType::AIPursuitSteering},
		{"AI", "徘徊操舵", EditorComponentType::AIWanderSteering},
		{"AI", "障害物回避操舵", EditorComponentType::AIObstacleAvoidanceSteering},
		{"AI", "群れ操舵", EditorComponentType::AIFlockSteering},
		{"AI", "視界センサー", EditorComponentType::AIVisionSensor},
		{"AI", "画像入力カメラ", EditorComponentType::AIOpenCvCamera},
		{"AI", "画像物体検出", EditorComponentType::AIOpenCvObjectDetector},
		{"AI", "画像色追跡", EditorComponentType::AIOpenCvColorTracker},
		{"AI", "動きセンサー", EditorComponentType::AIMotionSensor},
		{"AI", "Whisper 音声認識", EditorComponentType::AIWhisperSpeechRecognizer},
		{"AI", "音声コマンド", EditorComponentType::AIVoiceCommand},
		{"エフェクト", "パーティクルシステム", EditorComponentType::ParticleSystem},
		{"エフェクト", "ビジュアルエフェクト", EditorComponentType::VisualEffect},
		{"エフェクト", "トレイルレンダラー", EditorComponentType::TrailRenderer},
		{"エフェクト", "ラインレンダラー", EditorComponentType::LineRenderer},
		{"エフェクト", "レンズフレア", EditorComponentType::LensFlare},
		{"エフェクト", "プロジェクター", EditorComponentType::Projector},
		{"エフェクト", "デカールプロジェクター", EditorComponentType::DecalProjector},
		{"地形・タイルマップ", "テレイン", EditorComponentType::Terrain},
		{"地形・タイルマップ", "地形の当たり判定", EditorComponentType::TerrainCollider},
		{"地形・タイルマップ", "タイルマップ", EditorComponentType::Tilemap},
		{"地形・タイルマップ", "タイルマップレンダラー", EditorComponentType::TilemapRenderer},
		{"地形・タイルマップ", "タイルマップ当たり判定 2D", EditorComponentType::TilemapCollider2D},
		{"地形・タイルマップ", "グリッド", EditorComponentType::Grid},
		{"FeelKit", "FeelKit 触覚ソース", EditorComponentType::HapticSource},
	};

	const char* GetComponentDisplayName(EditorComponentType type) {
		// 追加メニューと Inspector 見出しの日本語名を同じ定義から取得する
		for (const ComponentAddEntry& entry : kComponentAddEntries) {
			if (entry.type == type) {
				return entry.displayName;
			}
		}

		return "不明なコンポーネント";
	}

	bool ContainsIgnoreCase(const char* text, const char* filter) {
		// Add Component 検索欄用の大小文字を無視した部分一致
		if (text == nullptr || filter == nullptr || filter[0] == '\0') {
			return true;
		}

		std::string textLower = text;
		std::string filterLower = filter;
		for (char& character : textLower) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		for (char& character : filterLower) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}

		return textLower.find(filterLower) != std::string::npos;
	}

	const char* GetAiSubCategory(EditorComponentType type) {
		if (type == EditorComponentType::AIBehaviorTree ||
			type == EditorComponentType::AIBehaviorBlackboard ||
			type == EditorComponentType::AIBehaviorSelector ||
			type == EditorComponentType::AIBehaviorSequence ||
			type == EditorComponentType::AIBehaviorTask ||
			type == EditorComponentType::AIBehaviorDecorator) {
			return "行動制御";
		}

		if (type == EditorComponentType::AIStateMachine ||
			type == EditorComponentType::AIState ||
			type == EditorComponentType::AIStateTransition) {
			return "状態管理";
		}

		if (type == EditorComponentType::AIGoapPlanner ||
			type == EditorComponentType::AIGoapGoal ||
			type == EditorComponentType::AIGoapAction ||
			type == EditorComponentType::AIGoapWorldState) {
			return "目標計画";
		}

		if (type == EditorComponentType::AIHtnPlanner ||
			type == EditorComponentType::AIHtnDomain ||
			type == EditorComponentType::AIHtnTask ||
			type == EditorComponentType::AIHtnMethod) {
			return "タスク計画";
		}

		if (type == EditorComponentType::AIPathfindingAgent ||
			type == EditorComponentType::AIMicroPatherGrid ||
			type == EditorComponentType::AIRecastNavMeshBuilder ||
			type == EditorComponentType::AIRecastCrowdAgent ||
			type == EditorComponentType::AIPathRequest ||
			type == EditorComponentType::AIDynamicObstacle) {
			return "経路探索";
		}

		if (type == EditorComponentType::AISteeringAgent ||
			type == EditorComponentType::AISeekSteering ||
			type == EditorComponentType::AIFleeSteering ||
			type == EditorComponentType::AIArriveSteering ||
			type == EditorComponentType::AIPursuitSteering ||
			type == EditorComponentType::AIWanderSteering ||
			type == EditorComponentType::AIObstacleAvoidanceSteering ||
			type == EditorComponentType::AIFlockSteering) {
			return "移動操舵";
		}

		if (type == EditorComponentType::AIVisionSensor ||
			type == EditorComponentType::AIOpenCvCamera ||
			type == EditorComponentType::AIOpenCvObjectDetector ||
			type == EditorComponentType::AIOpenCvColorTracker ||
			type == EditorComponentType::AIMotionSensor) {
			return "知覚";
		}

		if (type == EditorComponentType::AIWhisperSpeechRecognizer ||
			type == EditorComponentType::AIVoiceCommand) {
			return "音声";
		}

		return "";
	}

	void SyncEditorSelection(EditorInspectorPanelContext& context) {
		// Hierarchy の GameObject 選択と SceneView の選択 index を同じ対象へ寄せる
		context.selectionManager.SyncLegacySelection(
			context.selectedEditorGameObjectId,
			context.selectedSceneObject,
			context.selectedPlacedSceneObjectIndex);
	}

	void SyncGameObjectTransformToSceneObject(EditorInspectorPanelContext& context, const EditorGameObject& gameObject) {
		// Inspector で編集した Transform を SceneView 描画用 SceneObject へ反映する
		if (context.selectedPlacedSceneObjectIndex < 0 ||
			context.selectedPlacedSceneObjectIndex >= static_cast<int32_t>(context.sceneObjects.size())) {
			return;
		}

		EditorSceneObject& sceneObject =
			context.sceneObjects[static_cast<size_t>(context.selectedPlacedSceneObjectIndex)];

		if (sceneObject.gameObjectId != gameObject.id) {
			return;
		}

		sceneObject.transform.translate = gameObject.translate;
		sceneObject.transform.rotate = gameObject.rotate;
		sceneObject.transform.scale = gameObject.scale;
	}

	std::vector<EditorGameObject*> CollectSelectedGameObjects(EditorInspectorPanelContext& context) {
		std::vector<EditorGameObject*> selectedGameObjects;

		for (int32_t gameObjectId : context.selectedEditorGameObjectIds) {
			EditorGameObject* gameObject = context.editorScene.FindGameObject(gameObjectId);
			if (gameObject != nullptr) {
				selectedGameObjects.push_back(gameObject);
			}
		}

		return selectedGameObjects;
	}

	Transforms BuildMultiSelectionTransform(const std::vector<EditorGameObject*>& selectedGameObjects) {
		Transforms selectionTransform{};
		selectionTransform.scale = {1.0f, 1.0f, 1.0f};

		if (selectedGameObjects.empty()) {
			return selectionTransform;
		}

		float selectedObjectCount = static_cast<float>(selectedGameObjects.size());
		Vector3 totalTranslate{};
		Vector3 totalRotate{};
		Vector3 totalScale{};

		for (const EditorGameObject* gameObject : selectedGameObjects) {
			totalTranslate = Add(totalTranslate, gameObject->translate);
			totalRotate = Add(totalRotate, gameObject->rotate);
			totalScale = Add(totalScale, gameObject->scale);
		}

		float inverseSelectedObjectCount = 1.0f / selectedObjectCount;
		selectionTransform.translate = Multiply(inverseSelectedObjectCount, totalTranslate);
		selectionTransform.rotate = Multiply(inverseSelectedObjectCount, totalRotate);
		selectionTransform.scale = Multiply(inverseSelectedObjectCount, totalScale);
		return selectionTransform;
	}

	void ApplyMultiSelectionTransform(
		EditorInspectorPanelContext& context,
		const std::vector<EditorGameObject*>& selectedGameObjects,
		const Transforms& beforeTransform,
		const Transforms& afterTransform) {
		const Vector3 translationDelta = Subtract(afterTransform.translate, beforeTransform.translate);
		const Vector3 rotationDelta = Subtract(afterTransform.rotate, beforeTransform.rotate);
		Vector3 scaleRatio{1.0f, 1.0f, 1.0f};
		scaleRatio.x = std::fabs(beforeTransform.scale.x) > 0.0001f ? afterTransform.scale.x / beforeTransform.scale.x : 1.0f;
		scaleRatio.y = std::fabs(beforeTransform.scale.y) > 0.0001f ? afterTransform.scale.y / beforeTransform.scale.y : 1.0f;
		scaleRatio.z = std::fabs(beforeTransform.scale.z) > 0.0001f ? afterTransform.scale.z / beforeTransform.scale.z : 1.0f;
		const Matrix4x4 deltaRotationMatrix = MakeAffineMatrix(
			Vector3{1.0f, 1.0f, 1.0f},
			rotationDelta,
			Vector3{0.0f, 0.0f, 0.0f});

		for (EditorGameObject* gameObject : selectedGameObjects) {
			if (gameObject == nullptr) {
				continue;
			}

			Vector3 localOffset = Subtract(gameObject->translate, beforeTransform.translate);
			localOffset.x *= scaleRatio.x;
			localOffset.y *= scaleRatio.y;
			localOffset.z *= scaleRatio.z;
			localOffset = Transform(localOffset, deltaRotationMatrix);

			gameObject->translate = Add(afterTransform.translate, localOffset);
			gameObject->rotate = Add(gameObject->rotate, rotationDelta);
			gameObject->scale = {
				(std::max)(0.01f, gameObject->scale.x * scaleRatio.x),
				(std::max)(0.01f, gameObject->scale.y * scaleRatio.y),
				(std::max)(0.01f, gameObject->scale.z * scaleRatio.z)};
			SyncGameObjectTransformToSceneObject(context, *gameObject);
		}

		context.sceneSynchronizer.Update(context.textureFilePaths, context.selectedPlacedSceneObjectIndex);
	}

	void DrawSectionSpace() {
		// Component と Component の間に薄い区切りを入れて読みやすくする
		ImGui::Spacing();
		ImGui::Separator();
	}

	bool DrawComponentHeader(const char* title, bool* isActive) {
		// Unity の Component ヘッダーに近い、チェックボックス + 折りたたみ見出し
		DrawSectionSpace();
		ImGui::PushID(title);

		if (isActive != nullptr) {
			ImGui::AlignTextToFramePadding();
			ImGui::Checkbox("##ComponentActive", isActive);
			ImGui::SameLine();
		}

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
		bool isOpen = ImGui::CollapsingHeader(
			title,
			ImGuiTreeNodeFlags_DefaultOpen |
			ImGuiTreeNodeFlags_Framed |
			ImGuiTreeNodeFlags_SpanAvailWidth);
		ImGui::PopStyleColor(3);

		ImGui::PopID();

		return isOpen;
	}

	bool BeginPropertyTable(const char* tableId, int32_t columnCount) {
		// Label + Value を横並びにするための共通 Table
		return ImGui::BeginTable(
			tableId,
			columnCount,
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_NoSavedSettings);
	}

	void SetupTwoColumnPropertyTable() {
		// 左に日本語ラベル、右に入力欄を置く
		ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, kPropertyLabelWidth);
		ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthStretch);
	}

	bool DrawVector3Row(const char* label, Vector3& value, float speed, float minValue, float maxValue) {
		bool isChanged = false;  // X / Y / Z のどれかが編集されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("Vector3Row", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);

			ImGui::TableNextColumn();
			const ImGuiStyle& style = ImGui::GetStyle();  // 値列の中で X/Y/Z を同じ幅に分けるため、現在の余白を使う
			float valueAreaWidth = ImGui::GetContentRegionAvail().x;
			float inputWidth = (valueAreaWidth - kAxisLabelWidth * 3.0f - style.ItemInnerSpacing.x * 5.0f) / 3.0f;
			inputWidth = (std::max)(inputWidth, 48.0f);  // Dock 幅が狭い時でも X/Y が潰れて触れない状態を避ける

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("X");
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::SetNextItemWidth(inputWidth);
			isChanged |= ImGui::DragFloat("##X", &value.x, speed, minValue, maxValue, "%.3f");

			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Y");
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::SetNextItemWidth(inputWidth);
			isChanged |= ImGui::DragFloat("##Y", &value.y, speed, minValue, maxValue, "%.3f");

			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Z");
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::SetNextItemWidth(inputWidth);
			isChanged |= ImGui::DragFloat("##Z", &value.z, speed, minValue, maxValue, "%.3f");

			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

	bool DrawFloatRow(const char* label, float& value, float speed, float minValue, float maxValue) {
		bool isChanged = false;  // float 入力が変更されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("FloatRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			isChanged = ImGui::DragFloat("##Value", &value, speed, minValue, maxValue, "%.3f");
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

	bool DrawIntRow(const char* label, int32_t& value) {
		bool isChanged = false;  // int 入力が変更されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("IntRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			isChanged = ImGui::InputInt("##Value", &value);
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

	bool DrawCheckboxRow(const char* label, bool& value) {
		bool isChanged = false;  // bool チェックが変更されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("CheckboxRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			isChanged = ImGui::Checkbox("##Value", &value);
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

	bool DrawStringInputRow(const char* label, std::string& value) {
		bool isChanged = false;  // std::string 入力が変更されたか
		char buffer[260]{};
		strncpy_s(buffer, sizeof(buffer), value.c_str(), _TRUNCATE);
		ImGui::PushID(label);

		if (BeginPropertyTable("StringInputRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			isChanged = ImGui::InputText("##Value", buffer, sizeof(buffer));
			ImGui::EndTable();
		}

		if (isChanged) {
			value = buffer;
		}

		ImGui::PopID();
		return isChanged;
	}

	bool DrawAxisFreezeRow(const char* label, bool& freezeX, bool& freezeY, bool& freezeZ) {
		bool isChanged = false;  // X / Y / Z の固定チェックが変更されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("AxisFreezeRow", 7)) {
			ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, kPropertyLabelWidth);
			ImGui::TableSetupColumn("X名", ImGuiTableColumnFlags_WidthFixed, kAxisLabelWidth);
			ImGui::TableSetupColumn("X値", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Y名", ImGuiTableColumnFlags_WidthFixed, kAxisLabelWidth);
			ImGui::TableSetupColumn("Y値", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Z名", ImGuiTableColumnFlags_WidthFixed, kAxisLabelWidth);
			ImGui::TableSetupColumn("Z値", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("X");
			ImGui::TableNextColumn();
			isChanged |= ImGui::Checkbox("##X", &freezeX);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Y");
			ImGui::TableNextColumn();
			isChanged |= ImGui::Checkbox("##Y", &freezeY);

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Z");
			ImGui::TableNextColumn();
			isChanged |= ImGui::Checkbox("##Z", &freezeZ);

			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

	void DrawDisabledCheckboxRow(const char* label, bool value) {
		bool disabledValue = value;  // まだ内部データを持たない表示専用チェック
		ImGui::BeginDisabled();
		DrawCheckboxRow(label, disabledValue);
		ImGui::EndDisabled();
	}

	bool DrawColor3Row(const char* label, Vector3& color) {
		bool isChanged = false;  // RGB カラーが変更されたか
		ImGui::PushID(label);

		if (BeginPropertyTable("Color3Row", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			isChanged = ImGui::ColorEdit3("##Value", &color.x);
			ImGui::EndTable();
		}

		ImGui::PopID();

		return isChanged;
	}

		void DrawTextRow(const char* label, const char* text) {
		// Asset パスや未実装項目を Unity のフィールド風に横並び表示する
		ImGui::PushID(label);

		if (BeginPropertyTable("TextRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::TextWrapped("%s", text);
			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	void DrawDisabledComboRow(const char* label, const char* currentText) {
		// バックエンド未接続の項目は Disabled にして見た目だけを揃える
		const char* comboItems[] = {currentText};
		int32_t selectedIndex = 0;
		ImGui::BeginDisabled();
		ImGui::PushID(label);

		if (BeginPropertyTable("DisabledComboRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::Combo("##Value", &selectedIndex, comboItems, _countof(comboItems));
			ImGui::EndTable();
		}

		ImGui::PopID();
		ImGui::EndDisabled();
	}

	bool DrawComboRow(const char* label, int32_t& selectedIndex, const char* const* comboItems, int32_t comboItemCount) {
			bool isChanged = false;  // Combo の選択が変更されたか
			ImGui::PushID(label);

			if (BeginPropertyTable("ComboRow", 2)) {
				SetupTwoColumnPropertyTable();
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.0f);
				isChanged = ImGui::Combo("##Value", &selectedIndex, comboItems, comboItemCount);
				ImGui::EndTable();
			}

			ImGui::PopID();
		return isChanged;
	}

	bool DrawStringComboRow(const char* label, std::string& value, const char* const* comboItems, int32_t comboItemCount) {
		int32_t selectedIndex = 0;
		for (int32_t itemIndex = 0; itemIndex < comboItemCount; ++itemIndex) {
			if (value == comboItems[itemIndex]) {
				selectedIndex = itemIndex;
				break;
			}
		}

		const bool isChanged = DrawComboRow(label, selectedIndex, comboItems, comboItemCount);
		value = comboItems[selectedIndex];
		return isChanged;
	}

	bool DrawRotationDegreeRow(const char* label, Vector3& radianValue, float speedDegree) {
		Vector3 degreeValue{
			radianValue.x * kRadianToDegree,
			radianValue.y * kRadianToDegree,
			radianValue.z * kRadianToDegree
		};  // UI には Unity と同じように度数で出す。

		const bool isChanged = DrawVector3Row(label, degreeValue, speedDegree, 0.0f, 0.0f);
		if (isChanged) {
			radianValue.x = degreeValue.x * kDegreeToRadian;
			radianValue.y = degreeValue.y * kDegreeToRadian;
			radianValue.z = degreeValue.z * kDegreeToRadian;
		}

		return isChanged;
	}

	bool DrawAngleDegreeRow(const char* label, float& radianValue, float speedDegree, float minDegree, float maxDegree) {
		float degreeValue = radianValue * kRadianToDegree;  // UI では角度を度数で編集する。
		const bool isChanged = DrawFloatRow(label, degreeValue, speedDegree, minDegree, maxDegree);

		if (isChanged) {
			radianValue = degreeValue * kDegreeToRadian;
		}

		return isChanged;
	}

	std::string MakeGameObjectReferenceLabel(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		int32_t referencedGameObjectId,
		const char* noneLabel) {
		if (referencedGameObjectId < 0) {
			return noneLabel;
		}

		const EditorGameObject* referencedGameObject = context.editorScene.FindGameObject(referencedGameObjectId);
		if (referencedGameObject == nullptr) {
			return "見つからない参照 (ID:" + std::to_string(referencedGameObjectId) + ")";
		}

		std::string displayName = referencedGameObject->name.empty() ? "GameObject" : referencedGameObject->name;
		if (referencedGameObject->id == ownerGameObject.id) {
			displayName += " (自分自身 / ID:" + std::to_string(referencedGameObject->id) + ")";
		}
		else {
			displayName += " (ID:" + std::to_string(referencedGameObject->id) + ")";
		}

		if (!referencedGameObject->isActive) {
			displayName += " / 無効";
		}

		return displayName;
	}

	bool DrawGameObjectReferenceRow(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		const char* label,
		int32_t& referencedGameObjectId,
		const char* noneLabel,
		bool allowSelfReference) {
		bool isChanged = false;  // 参照先の GameObject が変更されたか。
		const std::string previewLabel = MakeGameObjectReferenceLabel(context, ownerGameObject, referencedGameObjectId, noneLabel);
		ImGui::PushID(label);

		if (BeginPropertyTable("GameObjectReferenceRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);

			if (ImGui::BeginCombo("##Value", previewLabel.c_str())) {
				const bool isNoneSelected = referencedGameObjectId < 0;
				if (ImGui::Selectable(noneLabel, isNoneSelected)) {
					referencedGameObjectId = -1;
					isChanged = true;
				}

				if (isNoneSelected) {
					ImGui::SetItemDefaultFocus();
				}

				for (const EditorGameObject& candidateGameObject : context.editorScene.GetGameObjects()) {
					if (!allowSelfReference && candidateGameObject.id == ownerGameObject.id) {
						continue;
					}

					std::string candidateLabel = candidateGameObject.name.empty() ? "GameObject" : candidateGameObject.name;
					candidateLabel += " (ID:" + std::to_string(candidateGameObject.id) + ")";
					if (!candidateGameObject.isActive) {
						candidateLabel += " / 無効";
					}

					const bool isSelected = referencedGameObjectId == candidateGameObject.id;
					if (ImGui::Selectable(candidateLabel.c_str(), isSelected)) {
						referencedGameObjectId = candidateGameObject.id;
						isChanged = true;
					}

					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}

			ImGui::EndTable();
		}

		ImGui::PopID();
		return isChanged;
	}


		void DrawReadOnlyFieldRow(const char* label, const char* text) {
		// Material スロットのような読み取り専用フィールド
		ImGui::PushID(label);

		if (BeginPropertyTable("ReadOnlyFieldRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::BeginDisabled();
			ImGui::Button(text, ImVec2(-1.0f, 0.0f));
			ImGui::EndDisabled();
			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	void DrawSubHeader(const char* title) {
		// Component 内の小見出し。Unity の Materials / Lighting 相当
		ImGui::Spacing();
		ImGui::TextDisabled("%s", title);
	}

	void DrawCenteredButtonAndOpenPopup(const char* label, const char* popupId) {
		// Inspector 下部の「コンポーネントを追加」を中央へ配置する
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float offsetX = (std::max)(0.0f, (availableWidth - kWideButtonWidth) * 0.5f);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

		if (ImGui::Button(label, ImVec2(kWideButtonWidth, 0.0f))) {
			ImGui::OpenPopup(popupId);
		}
	}

	void DrawGameObjectHeader(EditorInspectorPanelContext& context, EditorGameObject& gameObject) {
		static int32_t selectedTagIndex = 0;  // タグ機能の保存先がまだないため UI 表示用に保持する
		static int32_t selectedLayerIndex = 0;  // レイヤー機能の保存先がまだないため UI 表示用に保持する
		static bool isStatic = false;  // 静的フラグの保存先がまだないため UI 表示用に保持する
		const char* tagItems[] = {"Untagged"};
		const char* layerItems[] = {"Default"};

		if (ImGui::BeginTable(
			    "GameObjectHeader",
			    4,
			    ImGuiTableFlags_SizingStretchProp |
			    ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("アイコン", ImGuiTableColumnFlags_WidthFixed, 24.0f);
			ImGui::TableSetupColumn("有効", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("静的", ImGuiTableColumnFlags_WidthFixed, 78.0f);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("[ ]");

			ImGui::TableNextColumn();
			ImGui::Checkbox("##GameObjectActive", &gameObject.isActive);

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText(
				"##GameObjectName",
				context.selectedGameObjectName,
				context.selectedGameObjectNameSize);

			if (ImGui::IsItemDeactivatedAfterEdit()) {
				context.editorScene.PushUndo();
				context.editorScene.RenameGameObject(gameObject.id, context.selectedGameObjectName);
			}

			ImGui::TableNextColumn();
			ImGui::Checkbox("静的", &isStatic);
			ImGui::EndTable();
		}

		if (ImGui::BeginTable(
			    "GameObjectTagLayer",
			    4,
			    ImGuiTableFlags_SizingStretchProp |
			    ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("タグ名", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("タグ", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("レイヤー名", ImGuiTableColumnFlags_WidthFixed, 58.0f);
			ImGui::TableSetupColumn("レイヤー", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("タグ");

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::Combo("##Tag", &selectedTagIndex, tagItems, _countof(tagItems));

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("レイヤー");

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::Combo("##Layer", &selectedLayerIndex, layerItems, _countof(layerItems));
			ImGui::EndTable();
		}
	}

	bool DrawTransformComponent(EditorInspectorPanelContext& context, EditorGameObject& gameObject) {
		bool isTransformChanged = false;  // Transform の 3 項目の変更をまとめて返す

		if (DrawComponentHeader("トランスフォーム", nullptr)) {
			isTransformChanged |= DrawVector3Row("位置", gameObject.translate, 0.01f, 0.0f, 0.0f);
			isTransformChanged |= DrawRotationDegreeRow("回転", gameObject.rotate, 0.1f);
			isTransformChanged |= DrawVector3Row("スケール", gameObject.scale, 0.01f, 0.01f, 100.0f);
		}

		if (isTransformChanged) {
			SyncGameObjectTransformToSceneObject(context, gameObject);
		}

		return isTransformChanged;
	}

	bool DrawMultiSelectionTransform(EditorInspectorPanelContext& context) {
		std::vector<EditorGameObject*> selectedGameObjects = CollectSelectedGameObjects(context);
		if (selectedGameObjects.size() < 2) {
			return false;
		}

		Transforms beforeTransform = BuildMultiSelectionTransform(selectedGameObjects);
		Transforms editedTransform = beforeTransform;
		bool isTransformChanged = false;

		if (DrawComponentHeader("トランスフォーム (複数)", nullptr)) {
			isTransformChanged |= DrawVector3Row("位置", editedTransform.translate, 0.01f, 0.0f, 0.0f);
			isTransformChanged |= DrawRotationDegreeRow("回転", editedTransform.rotate, 0.1f);
			isTransformChanged |= DrawVector3Row("スケール", editedTransform.scale, 0.01f, 0.01f, 100.0f);
		}

		if (isTransformChanged) {
			ApplyMultiSelectionTransform(context, selectedGameObjects, beforeTransform, editedTransform);
		}

		return isTransformChanged;
	}

	void DrawMultiSelectionInspector(EditorInspectorPanelContext& context) {
		const int32_t selectedCount = static_cast<int32_t>(context.selectedEditorGameObjectIds.size());
		ImGui::Text("選択: %d 個の GameObject", selectedCount);

		if (ImGui::CollapsingHeader("オブジェクト操作")) {
			if (ImGui::Button("複数選択を解除")) {
				ClearSelectedGameObjects();
			}

			ImGui::SameLine();

			if (ImGui::Button("複数削除")) {
				ImGui::OpenPopup("MultiGameObject削除確認");
			}
		}

		if (ImGui::BeginPopupModal("MultiGameObject削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("選択中の GameObject をまとめて削除しますか？");

			if (ImGui::Button("削除する")) {
				const std::vector<int32_t> deleteTargetIds = context.selectedEditorGameObjectIds;
				context.editorScene.PushUndo();

				for (int32_t gameObjectId : deleteTargetIds) {
					context.editorScene.DeleteGameObject(gameObjectId);
				}

				if (context.editorScene.GetGameObjects().empty()) {
					ClearSelectedGameObjects();
				}
				else {
					SetSingleSelectedGameObject(context.editorScene.GetGameObjects()[0].id);
					SyncEditorSelection(context);
				}

				context.sceneSynchronizer.Update(context.textureFilePaths, context.selectedPlacedSceneObjectIndex);
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("キャンセル")) {
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		DrawMultiSelectionTransform(context);
		DrawTextRow("注", "複数選択時は Transform の一括編集に対応します。");
	}

	void DrawRendererComponent(EditorComponent& component, const char* materialName) {
		const char* assetText = component.assetPath.empty() ? "未設定" : component.assetPath.c_str();
		DrawTextRow("アセット", assetText);
		DrawSubHeader("マテリアル");
		DrawReadOnlyFieldRow("要素 0", materialName);
		DrawColor3Row("色", component.color);
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 10.0f);

		DrawSubHeader("ライティング");
		DrawDisabledComboRow("投影", "オン");
		DrawDisabledCheckboxRow("静的シャドウキャスター", false);
		DrawDisabledCheckboxRow("グローバルイルミネーションに影響", false);
		DrawDisabledComboRow("グローバルイルミネーションを受ける", "ライトプローブ");

		DrawSubHeader("プローブ");
		DrawDisabledComboRow("ライトプローブ", "プローブをブレンド");
		DrawDisabledComboRow("アンカーオーバーライド", "なし (トランスフォーム)");

		DrawSubHeader("Ray Tracing");
		DrawDisabledComboRow("レイトレーシングモード", "Dynamic Transform");
		DrawDisabledCheckboxRow("プロシージャルジオメトリ", false);
		DrawDisabledCheckboxRow("加速構造構築フラグ", false);

		DrawSubHeader("追加設定");
		DrawDisabledComboRow("モーションベクトル", "オブジェクトモーションごと");
		DrawDisabledCheckboxRow("動的オクルージョン", true);
		DrawDisabledComboRow("レンダリングレイヤーマスク", "Default");
	}

	std::string GetRenderableModelAssetPath(const EditorGameObject& gameObject) {
		const EditorComponent* modelRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* meshFilter =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
		if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
			return meshFilter->assetPath;
		}

		return "";
	}

	std::string GetModelAssetPathForComponent(const EditorGameObject& gameObject, const EditorComponent& component) {
		if (!component.assetPath.empty()) {
			return component.assetPath;
		}

		return GetRenderableModelAssetPath(gameObject);
	}

	bool TryLoadModelDataForComponent(
		const EditorGameObject& gameObject,
		const EditorComponent& component,
		ModelData& modelData,
		std::string& assetPath) {
		assetPath = GetModelAssetPathForComponent(gameObject, component);
		if (assetPath.empty()) {
			return false;
		}

		return EditorAssetUtility::LoadModelAsset(assetPath, modelData);
	}

	void DrawTexturePreviewByPath(
		const EditorInspectorPanelContext& context,
		const char* label,
		const std::string& texturePath) {
		if (texturePath.empty() || context.textureSrvHandlesGPU == nullptr) {
			return;
		}

		const int32_t textureIndex = EditorAssetUtility::GetTextureIndex(context.textureFilePaths, texturePath);
		if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= context.textureCount) {
			return;
		}

		DrawSubHeader(label);
		ImGui::Image(
			ImTextureRef(context.textureSrvHandlesGPU[static_cast<size_t>(textureIndex)].ptr),
			ImVec2(160.0f, 160.0f));
	}

	void DrawRendererComponent(
		const EditorInspectorPanelContext& context,
		const EditorGameObject& gameObject,
		EditorComponent& component,
		const char* fallbackMaterialName) {
		ModelData modelData{};
		std::string modelAssetPath;
		const bool hasModelData = TryLoadModelDataForComponent(gameObject, component, modelData, modelAssetPath);
		const std::string materialName =
			hasModelData && !modelData.material.name.empty() ? modelData.material.name : fallbackMaterialName;
		const std::string texturePath =
			hasModelData && !modelData.material.textureFilePath.empty() ? modelData.material.textureFilePath : "なし";
		const std::string uvLayoutTexturePath =
			hasModelData && !modelData.material.uvLayoutTextureFilePath.empty() ? modelData.material.uvLayoutTextureFilePath : "なし";

		DrawTextRow("アセット", modelAssetPath.empty() ? "未設定" : modelAssetPath.c_str());
		DrawSubHeader("マテリアル");
		DrawReadOnlyFieldRow("要素 0", materialName.c_str());
		DrawTextRow("テクスチャ", texturePath.c_str());
		DrawTextRow("UV確認画像", uvLayoutTexturePath.c_str());
		DrawTextRow("設定画像", component.textureAssetPath.empty() ? "未設定" : component.textureAssetPath.c_str());
		int32_t materialCount = static_cast<int32_t>(modelData.materials.size());
		DrawIntRow("マテリアル数", materialCount);
		DrawColor3Row("色", component.color);
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("メタリック", component.metallic, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("粗さ", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("屈折率", component.ior, 0.01f, 1.0f, 3.0f);
		DrawFloatRow("アルファ", component.alpha, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("反射", component.reflectionStrength, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("放射", component.emissionStrength, 0.01f, 0.0f, 10.0f);
		DrawStringInputRow("画像パス", component.textureAssetPath);
		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".png") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpg") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpeg"))) {
			if (ImGui::Button("選択中画像を設定", ImVec2(-1.0f, 0.0f))) {
				component.textureAssetPath = context.selectedAssetPath;
			}
		}
		if (!component.textureAssetPath.empty()) {
			DrawTexturePreviewByPath(context, "設定画像プレビュー", component.textureAssetPath);
		}

		if (hasModelData) {
			const std::string baseColorText =
				std::to_string(modelData.material.baseColor.x) + ", " +
				std::to_string(modelData.material.baseColor.y) + ", " +
				std::to_string(modelData.material.baseColor.z);
			DrawTextRow("元ベースカラー", baseColorText.c_str());
			const std::string alphaText = std::to_string(modelData.material.alpha);
			const std::string metallicText = std::to_string(modelData.material.metallic);
			const std::string roughnessText = std::to_string(modelData.material.roughness);
			const std::string iorText = std::to_string(modelData.material.ior);
			const std::string reflectionText = std::to_string(modelData.material.reflectance);
			DrawTextRow("元アルファ", alphaText.c_str());
			DrawTextRow("元メタリック", metallicText.c_str());
			DrawTextRow("元粗さ", roughnessText.c_str());
			DrawTextRow("元屈折率", iorText.c_str());
			DrawTextRow("元反射", reflectionText.c_str());
		}

		if (hasModelData && !modelData.material.textureFilePath.empty()) {
			DrawTexturePreviewByPath(context, "テクスチャプレビュー", modelData.material.textureFilePath);
		}

		if (hasModelData && !modelData.material.uvLayoutTextureFilePath.empty()) {
			DrawTexturePreviewByPath(context, "UV確認画像プレビュー", modelData.material.uvLayoutTextureFilePath);
		}

		DrawSubHeader("モデル情報");
		int32_t vertexCount = static_cast<int32_t>(modelData.vertices.size());
		int32_t animationClipCount = static_cast<int32_t>(modelData.animationClips.size());
		DrawIntRow("頂点数", vertexCount);
		DrawIntRow("アニメーションクリップ数", animationClipCount);

		DrawSubHeader("ライティング");
		DrawDisabledComboRow("投影", "オン");
		DrawDisabledCheckboxRow("静的シャドウキャスター", false);
		DrawDisabledCheckboxRow("グローバルイルミネーションに影響", false);
		DrawDisabledComboRow("グローバルイルミネーションを受ける", "ライトプローブ");

		DrawSubHeader("プローブ");
		DrawDisabledComboRow("ライトプローブ", "プローブをブレンド");
		DrawDisabledComboRow("アンカーオーバーライド", "なし (トランスフォーム)");

		DrawSubHeader("Ray Tracing");
		DrawDisabledComboRow("レイトレーシングモード", "Dynamic Transform");
		DrawDisabledCheckboxRow("プロシージャルジオメトリ", false);
		DrawDisabledCheckboxRow("加速構造構築フラグ", false);

		DrawSubHeader("追加設定");
		DrawDisabledComboRow("モーションベクトル", "オブジェクトモーションごと");
		DrawDisabledCheckboxRow("動的オクルージョン", true);
		DrawDisabledComboRow("レンダリングレイヤーマスク", "Default");
	}

	void DrawLightComponent(EditorComponent& component) {
		const char* lightTypeItems[] = {"Point", "Sun", "Spot", "Area"};
		int32_t lightTypeIndex = 0;
		if (component.assetPath == "Sun") {
			lightTypeIndex = 1;
		}
		else if (component.assetPath == "Spot") {
			lightTypeIndex = 2;
		}
		else if (component.assetPath == "Area") {
			lightTypeIndex = 3;
		}

		if (DrawComboRow("種類", lightTypeIndex, lightTypeItems, static_cast<int32_t>(_countof(lightTypeItems)))) {
			component.assetPath = lightTypeItems[lightTypeIndex];
		}

		DrawColor3Row("色", component.color);
		DrawFloatRow("強さ", component.intensity, 1.0f, 0.0f, 100000.0f);

		if (component.assetPath == "Point") {
			DrawFloatRow("半径", component.colliderRadius, 0.1f, 0.01f, 1000.0f);
		}
		else if (component.assetPath == "Sun") {
			DrawTextRow("注", "Sun は GameObject の回転から方向を作ります。位置は使いません。");
		}
		else if (component.assetPath == "Spot") {
			DrawFloatRow("距離", component.colliderRadius, 0.1f, 0.01f, 1000.0f);
			DrawFloatRow("内側角度", component.colliderSize.x, 0.1f, 1.0f, 89.0f);
			DrawFloatRow("外側角度", component.colliderSize.y, 0.1f, 1.0f, 89.0f);
		}
		else if (component.assetPath == "Area") {
			DrawFloatRow("距離", component.colliderRadius, 0.1f, 0.01f, 1000.0f);
			DrawFloatRow("広がり", component.colliderSize.z, 0.1f, 0.01f, 100.0f);
		}
	}

	void DrawPhysicsLayerRows(EditorComponent& component) {
		const char* layerItems[] = {
			"Default",
			"Player",
			"Enemy",
			"Ground",
			"Projectile",
			"Trigger",
			"UI",
			"Ignore Raycast"};
		component.physicsLayer = (std::clamp)(component.physicsLayer, 0, static_cast<int32_t>(_countof(layerItems)) - 1);
		DrawComboRow("衝突レイヤー", component.physicsLayer, layerItems, static_cast<int32_t>(_countof(layerItems)));
	}

	void DrawPhysicsMaterialRows(EditorComponent& component) {
		const char* combineItems[] = {"平均", "最小", "最大", "乗算"};
		component.frictionCombineMode = (std::clamp)(component.frictionCombineMode, 0, static_cast<int32_t>(_countof(combineItems)) - 1);
		component.bouncinessCombineMode = (std::clamp)(component.bouncinessCombineMode, 0, static_cast<int32_t>(_countof(combineItems)) - 1);

		DrawSubHeader("物理マテリアル");
		DrawFloatRow("動摩擦", component.dynamicFriction, 0.01f, 0.0f, 5.0f);
		DrawFloatRow("静止摩擦", component.staticFriction, 0.01f, 0.0f, 5.0f);
		DrawFloatRow("弾力性", component.bounciness, 0.01f, 0.0f, 1.0f);
		DrawComboRow("摩擦の合成", component.frictionCombineMode, combineItems, static_cast<int32_t>(_countof(combineItems)));
		DrawComboRow("弾力性の合成", component.bouncinessCombineMode, combineItems, static_cast<int32_t>(_countof(combineItems)));
	}

	void DrawColliderCommonRows(EditorComponent& component) {
		DrawReadOnlyFieldRow("当たり判定の編集", "編集");
		DrawCheckboxRow("トリガーにする", component.isTrigger);
		DrawCheckboxRow("接触イベント", component.generateContactEvents);
		DrawPhysicsLayerRows(component);
		DrawPhysicsMaterialRows(component);
	}

	void DrawRigidBodyComponent(EditorComponent& component) {
		const char* interpolationItems[] = {"補間なし", "補間", "外挿"};
		const char* collisionDetectionItems[] = {"離散", "連続"};
		component.interpolationMode = (std::clamp)(component.interpolationMode, 0, static_cast<int32_t>(_countof(interpolationItems)) - 1);
		component.collisionDetectionMode =
			(std::clamp)(component.collisionDetectionMode, 0, static_cast<int32_t>(_countof(collisionDetectionItems)) - 1);

		DrawFloatRow("質量", component.mass, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("線形減衰", component.drag, 0.01f, 0.0f, 20.0f);
		DrawFloatRow("角度減衰", component.angularDrag, 0.01f, 0.0f, 20.0f);
		DrawCheckboxRow("重力を使用", component.useGravity);
		DrawCheckboxRow("キネマティックにする", component.isKinematic);
		DrawComboRow("補間", component.interpolationMode, interpolationItems, static_cast<int32_t>(_countof(interpolationItems)));
		DrawComboRow(
			"衝突判定",
			component.collisionDetectionMode,
			collisionDetectionItems,
			static_cast<int32_t>(_countof(collisionDetectionItems)));
		DrawVector3Row("速度", component.velocity, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("角速度", component.angularVelocity, 0.01f, 0.0f, 0.0f);
		DrawSubHeader("Constraints");
		DrawAxisFreezeRow("位置を固定", component.freezePositionX, component.freezePositionY, component.freezePositionZ);
		DrawAxisFreezeRow("回転を固定", component.freezeRotationX, component.freezeRotationY, component.freezeRotationZ);
	}

	void DrawBoxColliderComponent(EditorComponent& component) {
		DrawColliderCommonRows(component);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
	}

	void DrawSphereColliderComponent(EditorComponent& component) {
		DrawColliderCommonRows(component);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.01f, 100.0f);
	}

	void DrawCapsuleColliderComponent(EditorComponent& component) {
		DrawColliderCommonRows(component);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("高さ", component.colliderSize.y, 0.01f, 0.01f, 100.0f);
	}

	void DrawDikComboRow(const char* label, int32_t& key) {
		int32_t index = DikCodeToIndex(key);
		if (index < 0) { index = 0; }
		if (DrawComboRow(label, index, []() {
			static const char* names[_countof(kDikEntries)] = {};
			if (!names[0]) {
				for (size_t i = 0; i < _countof(kDikEntries); ++i) {
					names[i] = kDikEntries[i].name;
				}
			}
			return names;
		}(), static_cast<int32_t>(_countof(kDikEntries)))) {
			key = kDikEntries[index].dikCode;
		}
	}

		void DrawInputComponent(EditorComponent& component) {
			DrawFloatRow("移動速度", component.inputMoveSpeed, 0.01f, 0.0f, 20.0f);
			DrawDikComboRow("前進", component.inputForwardKey);
			DrawDikComboRow("後退", component.inputBackKey);
			DrawDikComboRow("左移動", component.inputLeftKey);
			DrawDikComboRow("右移動", component.inputRightKey);
			DrawDikComboRow("ジャンプ", component.inputJumpKey);
			DrawFloatRow("マウス感度", component.inputMouseSensitivity, 0.01f, 0.0f, 10.0f);
			DrawCheckboxRow("Y軸反転", component.inputInvertY);
		}

		std::string MakeDefaultPlayerInputActionsText() {
			return
				"# CG2 PlayerInput Actions\r\n"
				"# Action|ActionMap|ActionName|ValueType|BindingType|...\r\n"
				"Action|Player|Move|Vector2|2DVector|W|S|A|D\r\n"
				"Action|Player|Jump|Button|Key|Space\r\n"
				"Action|Player|Fire|Button|Mouse|LeftButton\r\n";
		}

		std::string GetProjectAssetCreateDirectory(const std::string& selectedAssetPath) {
			// Project で Assets 配下のファイルを選択している時は、その親フォルダへ作る。
			if (!selectedAssetPath.empty() &&
				selectedAssetPath.rfind("Assets/", 0) == 0) {
				std::filesystem::path selectedPath(selectedAssetPath);
				if (std::filesystem::is_directory(selectedPath)) {
					return selectedPath.generic_string();
				}

				const std::filesystem::path parentPath = selectedPath.parent_path();
				if (!parentPath.empty()) {
					return parentPath.generic_string();
				}
			}

			return "Assets";
		}

		std::string MakeUniqueInputActionsAssetPath(const std::string& directoryPath) {
			const std::filesystem::path baseDirectoryPath(directoryPath);
			const std::string baseName = "InGameInputAction";
			for (int32_t fileIndex = 0; fileIndex < 1000; ++fileIndex) {
				std::string candidateName = baseName;
				if (fileIndex > 0) {
					candidateName += std::to_string(fileIndex);
				}

				const std::filesystem::path candidatePath =
					baseDirectoryPath / (candidateName + ".inputactions");
				if (!std::filesystem::exists(candidatePath)) {
					return candidatePath.generic_string();
				}
			}

			return (baseDirectoryPath / "InGameInputAction.inputactions").generic_string();
		}

		void CreatePlayerInputActionsAsset(EditorInspectorPanelContext& context, EditorComponent& component) {
			const std::string createDirectoryPath = GetProjectAssetCreateDirectory(context.selectedAssetPath);
			std::filesystem::create_directories(createDirectoryPath);
			const std::string filePath = MakeUniqueInputActionsAssetPath(createDirectoryPath);
			std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
			if (!file.is_open()) {
				return;
			}

			const std::string fileText = MakeDefaultPlayerInputActionsText();
			file.write(reinterpret_cast<const char*>(kUtf8Bom), static_cast<std::streamsize>(sizeof(kUtf8Bom)));
			file.write(fileText.data(), static_cast<std::streamsize>(fileText.size()));
			component.assetPath = filePath;  // 作成した Actions アセットをそのまま PlayerInput に割り当てる
		}

		void DrawPlayerInputComponent(EditorInspectorPanelContext& context, EditorComponent& component) {
			const char* behaviorItems[] = {"Invoke C++ Events"};
			component.inputBehavior = (std::clamp)(component.inputBehavior, 0, static_cast<int32_t>(_countof(behaviorItems)) - 1);

			DrawTextRow("説明", "新 Input System 風の Player Input です。Actions は Project の Assets 配下に作成し、そこから割り当てます。");
			DrawTextRow("手順1", "下の Project タブで .inputactions を作成し、Inspector の Input Actions で ActionMap / Action / Path を設定します。");
			DrawTextRow("手順2", "この GameObject に PlayerInput を付け、Actions にその .inputactions を割り当てます。");
			DrawTextRow("手順3", "Play 中は Move で移動、Jump で上方向速度、Fire でイベント取得が動きます。");
			DrawTextRow("Actions", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
			DrawStringInputRow("Actions パス", component.assetPath);
			DrawStringInputRow("Default Map", component.inputActionMapName);
			DrawComboRow("Behavior", component.inputBehavior, behaviorItems, static_cast<int32_t>(_countof(behaviorItems)));
			DrawSubHeader("Events");
			DrawStringInputRow("Move", component.inputMoveEventName);
			DrawStringInputRow("Jump", component.inputJumpEventName);
			DrawStringInputRow("Fire", component.inputFireEventName);

			if (ImGui::Button("Create Actions...", ImVec2(-1.0f, 0.0f))) {
				CreatePlayerInputActionsAsset(context, component);
			}

			if (!component.assetPath.empty()) {
				if (ImGui::Button("Actions を開く", ImVec2(-1.0f, 0.0f))) {
					const std::string command = "start \"\" \"" + component.assetPath + "\"";
					std::system(command.c_str());
				}
			}

			if (!context.selectedAssetPath.empty() && context.selectedAssetPath.find(".inputactions") != std::string::npos) {
				if (ImGui::Button("選択中アセットを Actions に設定", ImVec2(-1.0f, 0.0f))) {
					component.assetPath = context.selectedAssetPath;  // Project で選択中の .inputactions を PlayerInput に割り当てる
				}
			}

			DrawTextRow("C++ 取得例1", "runtimeApi->GetActionVector2(gameObjectId, \"Player\", \"Move\")");
			DrawTextRow("C++ 取得例2", "runtimeApi->WasActionJustPressed(gameObjectId, \"Player\", \"Jump\")");
			DrawTextRow("追加 Action", "Project の Input Actions アセットを編集して ActionMap / Action / Path を増やし、C++ 側でその名前を使います。");
		}

	void DrawHapticSourceComponent(EditorComponent& component) {
		DrawTextRow("説明", "FeelKitHaptics の触覚効果を再生するコンポーネントです。");
		DrawTextRow("サウンド", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawFloatRow("強さ", component.hapticStrength, 0.01f, 0.0f, 1.0f);
		DrawIntRow("持続時間(ms)", component.hapticDurationMs);
		DrawCheckboxRow("ループ", component.hapticLoop);
	}

	void DrawNativeScriptComponent(
		EditorInspectorPanelContext& context,
		EditorGameObject& gameObject,
		EditorComponent& component,
		const char* description) {
		static char requestedScriptName[128] = "NewNativeScript";  // GUI から生成する C++ クラス名の入力欄。
		static std::string generationMessage;  // 直前の生成結果をそのまま Inspector に表示する。
		auto openWithShell = [](const std::filesystem::path& filePath) {
			const std::filesystem::path absolutePath = std::filesystem::absolute(filePath);
			std::string commandPath = absolutePath.string();
			std::replace(commandPath.begin(), commandPath.end(), '/', '\\');

			if (absolutePath.extension() == ".bat") {
				const std::string command = "cmd /c \"\"" + commandPath + "\"\"";
				std::system(command.c_str());
				return;
			}

			const std::string command = "start \"\" \"" + commandPath + "\"";
			std::system(command.c_str());
		};

		DrawTextRow("説明", description);
		DrawTextRow("DLL", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());

		{
			char dllPathBuffer[260]{};
			strncpy_s(dllPathBuffer, sizeof(dllPathBuffer), component.assetPath.c_str(), _TRUNCATE);
			ImGui::PushID("NativeScriptDllPath");

			if (BeginPropertyTable("NativeScriptDllPathTable", 2)) {
				SetupTwoColumnPropertyTable();
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("DLL パス");
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##DllPath", dllPathBuffer, sizeof(dllPathBuffer))) {
					component.assetPath = dllPathBuffer;  // DLL を手入力で差し替えたい時のため、直接パス編集も許可する。
				}
				ImGui::EndTable();
			}

			ImGui::PopID();
		}

		if (!context.selectedAssetPath.empty() && context.selectedAssetPath.find(".dll") != std::string::npos) {
			if (ImGui::Button("選択中 DLL を設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;  // Project で選んだ DLL をそのまま Script に割り当てる。
			}
		}

		{
			const EditorScriptManager::ScriptDebugInfo debugInfo =
				context.runtimeManager.GetScriptManager().GetDebugInfo(gameObject.id);

			DrawSubHeader("デバッグ");
			DrawTextRow("DLL 状態", debugInfo.isLoaded ? "読込成功" : "未読込 / 失敗");
			DrawTextRow("DLL 実在", debugInfo.sourceDllExists ? "あり" : "なし");
			DrawTextRow("実行中 DLL", debugInfo.loadedDllPath.empty() ? "未作成" : debugInfo.loadedDllPath.c_str());
			DrawTextRow("状態メッセージ", debugInfo.lastStatusMessage.empty() ? "なし" : debugInfo.lastStatusMessage.c_str());
		}

		DrawSubHeader("C++ スクリプト生成");
		ImGui::InputText("クラス名", requestedScriptName, sizeof(requestedScriptName));

		if (ImGui::Button("C++ スクリプトを作成", ImVec2(-1.0f, 0.0f))) {
			const EditorNativeScriptAssetResult result =
				EditorNativeScriptAssetManager::CreateNativeScriptAsset(requestedScriptName, kIsDebugEditorBuild);
			generationMessage = result.message;

			if (result.isSucceeded) {
				component.assetPath = result.dllFilePath;  // 生成直後から Script Component は想定 DLL を参照する。
				context.selectedAssetPath = result.sourceFilePath;  // Project ではまず .cpp を選択状態にして編集へ入りやすくする。
				std::string nextDefaultName = gameObject.name + "Script";
				for (char& letter : nextDefaultName) {
					const bool isAllowedLetter =
						(std::isalnum(static_cast<unsigned char>(letter)) != 0) || letter == '_';
					if (!isAllowedLetter) {
						letter = '_';
					}
				}

				strncpy_s(requestedScriptName, sizeof(requestedScriptName), nextDefaultName.c_str(), _TRUNCATE);
			}
		}

		if (!generationMessage.empty()) {
			ImGui::TextWrapped("%s", generationMessage.c_str());
		}

		if (!component.assetPath.empty()) {
			DrawTextRow(kCurrentExpectedDllLabel, component.assetPath.c_str());
		}

		if (!component.assetPath.empty()) {
			const std::filesystem::path dllPath = std::filesystem::path(component.assetPath);
			const std::filesystem::path scriptSourcePath = dllPath.parent_path().parent_path().parent_path() / (dllPath.stem().generic_string() + ".cpp");
			const std::filesystem::path buildScriptPath = dllPath.parent_path().parent_path().parent_path() / kCurrentBuildScriptName;
			const std::filesystem::path expectedDllPath =
				dllPath.parent_path().parent_path().parent_path() / "x64" / kCurrentScriptConfigName / dllPath.filename();

			if (std::filesystem::exists(scriptSourcePath)) {
				if (ImGui::Button("C++ を開く", ImVec2(-1.0f, 0.0f))) {
					openWithShell(scriptSourcePath);
				}
			}

			if (std::filesystem::exists(buildScriptPath)) {
				if (ImGui::Button(kCurrentBuildButtonLabel, ImVec2(-1.0f, 0.0f))) {
					openWithShell(buildScriptPath);
				}
				DrawTextRow("ビルド", buildScriptPath.generic_string().c_str());
			}

			DrawTextRow("実行中構成 DLL", expectedDllPath.generic_string().c_str());
		}

		DrawTextRow("使い方", kCurrentHowToText);
	}

	[[maybe_unused]] void DrawGenericComponent(EditorComponent& component, const char* description) {
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
		DrawTextRow("説明", description);
		DrawTextRow("アセット", component.assetPath.empty() ? "未使用" : component.assetPath.c_str());
	}

	void DrawConstantForceComponent(EditorComponent& component) {
		DrawTextRow("説明", "毎フレーム同じ力を加える 3D 物理コンポーネントです。");
		DrawVector3Row("力", component.velocity, 0.01f, 0.0f, 0.0f);
	}

	void DrawJointComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component,
		const char* description) {
		DrawTextRow("説明", description);
		DrawGameObjectReferenceRow(context, ownerGameObject, "接続先", component.connectedGameObjectId, "未設定", false);
		DrawVector3Row("アンカー", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("回転軸", component.jointAxis, 0.01f, 0.0f, 0.0f);

		// Hinge は回転角度の制限を Jolt の HingeConstraint へ渡す。
		if (component.type == EditorComponentType::HingeJoint ||
		    component.type == EditorComponentType::CharacterJoint) {
			DrawAngleDegreeRow("最小角度", component.jointMinLimit, 0.1f, -180.0f, 0.0f);
			DrawAngleDegreeRow("最大角度", component.jointMaxLimit, 0.1f, 0.0f, 180.0f);
		}

		// Spring は 2 点間の距離制限を Jolt の DistanceConstraint へ渡す。
		if (component.type == EditorComponentType::SpringJoint ||
		    component.type == EditorComponentType::DistanceJoint2D) {
			DrawFloatRow("最小距離", component.jointMinDistance, 0.01f, 0.0f, 100.0f);
			DrawFloatRow("最大距離", component.jointMaxDistance, 0.01f, 0.0f, 100.0f);
		}

		DrawFloatRow("ばね周波数", component.jointSpringFrequency, 0.01f, 0.0f, 60.0f);
		DrawFloatRow("ばね減衰", component.jointSpringDamping, 0.01f, 0.0f, 10.0f);
	}

	void DrawAudioFilterComponent(EditorComponent& component, const char* description) {
		DrawTextRow("説明", description);
		DrawFloatRow("効果量", component.intensity, 0.01f, 0.0f, 1.0f);
	}

	void DrawUIComponent(EditorComponent& component, const char* description) {
		DrawTextRow("説明", description);
		DrawColor3Row("色", component.color);
		DrawFloatRow("透明度", component.intensity, 0.01f, 0.0f, 1.0f);
	}

	void DrawNavMeshAgentComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "NavMesh 上を移動する AI Agent 設定です。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "目的地", component.connectedGameObjectId, "未設定", false);
		DrawFloatRow("半径", component.navAgentRadius, 0.01f, 0.1f, 10.0f);
		DrawFloatRow("高さ", component.navAgentHeight, 0.01f, 0.1f, 10.0f);
		DrawFloatRow("最大速度", component.navMaxSpeed, 0.1f, 0.1f, 50.0f);
		DrawFloatRow("最大加速度", component.navMaxAcceleration, 0.1f, 0.1f, 100.0f);
		DrawFloatRow("停止距離", component.navStoppingDistance, 0.1f, 0.0f, 10.0f);
		DrawCheckboxRow("自動再経路", component.navAutoRepath);
	}

	void DrawNavMeshObstacleComponent(EditorComponent& component) {
		DrawTextRow("説明", "NavMesh Agent の経路上に置く障害物コンポーネントです。");
		DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.1f, 10.0f);
		DrawFloatRow("高さ", component.colliderSize.y, 0.01f, 0.1f, 10.0f);
		DrawCheckboxRow("移動中も NavMesh を更新", component.navCarve);
	}

	void DrawNavMeshSurfaceComponent(EditorComponent& component) {
		DrawTextRow("説明", "NavMesh を生成する面を指定するコンポーネントです。");
		DrawFloatRow("Agent 半径", component.navAgentRadius, 0.01f, 0.1f, 10.0f);
		DrawFloatRow("Agent 高さ", component.navAgentHeight, 0.01f, 0.1f, 10.0f);
		DrawFloatRow("最大傾斜角度", component.navMaxSlope, 1.0f, 0.0f, 90.0f);
		DrawFloatRow("最大段差", component.navMaxClimb, 0.01f, 0.0f, 10.0f);
		DrawIntRow("レイヤーマスク", component.physicsLayer);
	}

	void DrawNavMeshModifierComponent(EditorComponent& component) {
		DrawTextRow("説明", "NavMesh 生成ルールを GameObject 単位で変更するコンポーネントです。");
		DrawCheckboxRow("Area を上書き", component.navAreaOverride);
		DrawIntRow("Area", component.navArea);
		DrawCheckboxRow("ビルドから除外", component.navIgnoreFromBuild);
	}

	void DrawNavMeshModifierVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "指定範囲だけ NavMesh 生成ルールを変更するコンポーネントです。");
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
		DrawIntRow("Area", component.navArea);
	}

	void DrawNavMeshLinkComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "離れた NavMesh 同士を接続するコンポーネントです。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "接続先", component.connectedGameObjectId, "未設定", false);
		DrawCheckboxRow("双方向", component.navBidirectional);
		DrawFloatRow("コスト倍率", component.navCostModifier, 0.01f, 0.0f, 100.0f);
		DrawFloatRow("幅", component.colliderRadius, 0.01f, 0.1f, 10.0f);
	}

	void DrawAiAgentComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component,
		const char* description,
		const char* libraryName) {
		const char* behaviorModes[] = {"追跡", "逃走", "巡回", "待機"};

		DrawTextRow("説明", description);
		DrawTextRow("意味", "この GameObject を AI として動かす実行用コンポーネントです。");
		DrawTextRow("使い方", "対象を設定し、動作と速度を決めて Play すると移動します。Rigidbody があれば速度を物理へ渡します。");
		DrawTextRow("外部ライブラリ", libraryName);
		DrawGameObjectReferenceRow(context, ownerGameObject, "対象", component.connectedGameObjectId, "未設定", false);
		DrawComboRow("動作", component.inputBehavior, behaviorModes, static_cast<int32_t>(_countof(behaviorModes)));
		DrawFloatRow("最大速度", component.navMaxSpeed, 0.1f, 0.0f, 100.0f);
		DrawFloatRow("最大加速度", component.navMaxAcceleration, 0.1f, 0.0f, 200.0f);
		DrawFloatRow("停止距離", component.navStoppingDistance, 0.01f, 0.0f, 100.0f);
		DrawFloatRow("回避半径", component.navAgentRadius, 0.01f, 0.0f, 20.0f);
		DrawTextRow("AI アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawStringInputRow("AI アセットパス", component.assetPath);
		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".py") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".json") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".onnx") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".xml"))) {
			if (ImGui::Button("選択中 AI アセットを設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;  // Project で選んだ AI 用アセットを、この AI Component に割り当てる。
			}
		}
	}

	void DrawAiDataComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component,
		const char* description,
		const char* libraryName) {
		DrawTextRow("説明", description);
		DrawTextRow("意味", "AI の判断材料や設定を持つ部品です。単体では基本的に移動しません。");
		DrawTextRow("使い方", "Planner / Agent / Sensor などの実行用 AI から接続先として参照して使います。");
		DrawTextRow("外部ライブラリ", libraryName);
		DrawGameObjectReferenceRow(context, ownerGameObject, "接続先", component.connectedGameObjectId, "未設定", false);
		DrawTextRow("AI アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawStringInputRow("AI アセットパス", component.assetPath);
		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".py") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".json") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".onnx") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".xml"))) {
			if (ImGui::Button("選択中 AI アセットを設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;  // Project で選んだ AI 用アセットを、この AI Data に割り当てる。
			}
		}
		DrawFloatRow("判定半径", component.colliderRadius, 0.01f, 0.0f, 1000.0f);
		DrawVector3Row("判定サイズ", component.colliderSize, 0.01f, 0.0f, 0.0f);
	}

	void DrawAiVisionSensorComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "対象が視界範囲に入っているかを見る AI センサーです。");
		DrawTextRow("意味", "敵の発見、索敵範囲、クリック/画像検出などの知覚判定に使います。");
		DrawTextRow("使い方", "対象を設定し、視界距離と視野角を調整して Play します。範囲内に入ると内部状態と Console ログが更新されます。");
		DrawTextRow("外部ライブラリ", "OpenCV / MediaPipe 連携用の入口");
		DrawGameObjectReferenceRow(context, ownerGameObject, "対象", component.connectedGameObjectId, "未設定", false);
		DrawTextRow("AI アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawStringInputRow("AI アセットパス", component.assetPath);
		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".py") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".json") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".onnx") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".xml"))) {
			if (ImGui::Button("選択中 AI アセットを設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;  // Project で選んだ AI 用アセットを、Sensor に割り当てる。
			}
		}
		DrawFloatRow("視界距離", component.colliderRadius, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("視野角", component.colliderSize.x, 1.0f, 0.0f, 360.0f);
	}

	void DrawLocalMoveComponent(EditorComponent& component) {
		DrawTextRow("説明", "自身のローカル軸方向へ毎フレーム移動し続けるコンポーネントです。");
		DrawVector3Row("ローカル方向", component.velocity, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("速度", component.inputMoveSpeed, 0.01f, 0.0f, 100.0f);
		DrawTextRow("例", "X=1 でローカル右、Z=1 でローカル前、Y=1 でローカル上へ移動します。");
	}

	void DrawRollingMoveComponent(EditorComponent& component) {
		DrawTextRow("説明", "Rigidbody へ回転トルクを加え、摩擦で前進させる球やタイヤ向けの物理移動コンポーネントです。");
		DrawVector3Row("進行方向", component.velocity, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("トルク", component.rollingTorque, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("馬力", component.rollingHorsepower, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.01f, 100.0f);
		DrawTextRow("条件", "Dynamic の Rigidbody と十分な摩擦が必要です。SphereCollider の半径を見た目とそろえると自然に転がります。");
	}

	void DrawAimConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "指定対象へ向きを合わせる Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		const char* axisNames[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
		int32_t clampedAxis = (std::max)(0, (std::min)(component.constraintAimAxis, 5));
		DrawIntRow("ターゲット方向軸", component.constraintAimAxis);
		DrawTextRow("軸の意味", axisNames[clampedAxis]);
	}

	void DrawLookAtConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "指定対象を見るように回転する Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		const char* axisNames[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
		int32_t clampedUp = (std::max)(0, (std::min)(component.constraintUpAxis, 5));
		DrawIntRow("上方向軸", component.constraintUpAxis);
		DrawTextRow("軸の意味", axisNames[clampedUp]);
		DrawFloatRow("ロール角", component.constraintRoll, 0.1f, -180.0f, 180.0f);
	}

	void DrawParentConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "Transform 全体を別オブジェクトへ追従させる Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("位置オフセット", component.constraintPositionOffset, 0.01f, 0.0f, 0.0f);
		DrawRotationDegreeRow("回転オフセット", component.constraintRotationOffset, 0.1f);
	}

	void DrawPositionConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "位置だけを別オブジェクトへ追従させる Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("オフセット", component.constraintPositionOffset, 0.01f, 0.0f, 0.0f);
	}

	void DrawRotationConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "回転だけを別オブジェクトへ追従させる Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		DrawRotationDegreeRow("回転オフセット", component.constraintRotationOffset, 0.1f);
	}

	void DrawScaleConstraintComponent(EditorComponent& component) {
		DrawTextRow("説明", "拡縮だけを別オブジェクトへ追従させる Constraint です。");
		DrawIntRow("ターゲット ID", component.connectedGameObjectId);
		DrawFloatRow("重み", component.constraintWeight, 0.01f, 0.0f, 1.0f);
		DrawCheckboxRow("X 軸フリーズ", component.constraintFreezeAxisX);
		DrawCheckboxRow("Y 軸フリーズ", component.constraintFreezeAxisY);
		DrawCheckboxRow("Z 軸フリーズ", component.constraintFreezeAxisZ);
	}

	void DrawMeshColliderComponent(const EditorGameObject& gameObject, EditorComponent& component) {
		ModelData modelData{};
		std::string collisionAssetPath;
		const bool hasModelData = TryLoadModelDataForComponent(gameObject, component, modelData, collisionAssetPath);
		const std::string renderAssetPath = GetRenderableModelAssetPath(gameObject);
		const char* meshSourceLabel =
			component.assetPath.empty() ? "描画メッシュを流用" : "当たり判定メッシュを個別使用";

		DrawTextRow("メッシュ", collisionAssetPath.empty() ? "未設定" : collisionAssetPath.c_str());
		DrawTextRow("参照元", meshSourceLabel);
		if (!component.assetPath.empty() && !renderAssetPath.empty()) {
			DrawTextRow("描画メッシュ", renderAssetPath.c_str());
		}

		DrawColliderCommonRows(component);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);

		DrawSubHeader("BVH / メッシュ");
		int32_t vertexCount = static_cast<int32_t>(modelData.vertices.size());
		int32_t triangleCount = static_cast<int32_t>(modelData.vertices.size() / 3u);
		DrawIntRow("頂点数", vertexCount);
		DrawIntRow("三角形数", triangleCount);
		DrawTextRow("BVH", hasModelData ? "生成対象" : "未生成");
	}

	void DrawAnimationComponent(EditorInspectorPanelContext& context, const EditorGameObject& gameObject, EditorComponent& component) {
		ModelData modelData{};
		std::string animationAssetPath;
		const bool hasModelData = TryLoadModelDataForComponent(gameObject, component, modelData, animationAssetPath);
		const EditorAnimationManager& animationManager = context.runtimeManager.GetAnimationManager();

		DrawTextRow("説明", "Animation Clip の再生設定です。");
		DrawTextRow("アセット", animationAssetPath.empty() ? "未設定 (プロシージャル)" : animationAssetPath.c_str());
		DrawFloatRow("速度", component.animationSpeed, 0.1f, 0.0f, 10.0f);
		DrawCheckboxRow("ループ", component.animationLoop);
		DrawCheckboxRow("自動再生", component.animationPlayOnAwake);
		const char* animTypes[] = {"FBX Clip", "Float", "Rotate", "Pulse", "Bob"};
		int32_t clamped = (std::max)(0, (std::min)(component.animationType, 4));
		DrawIntRow("種類", component.animationType);
		DrawTextRow("種類の意味", animTypes[clamped]);
		if (component.animationType >= 1) {
			DrawFloatRow("振幅", component.animationAmplitude, 0.01f, 0.0f, 10.0f);
		}

		DrawSubHeader("再生状態");
		bool isPlaying = animationManager.IsAnimationPlaying(gameObject.id);
		DrawDisabledCheckboxRow("再生中", isPlaying);
		const float currentTime = animationManager.GetAnimationTime(gameObject.id);
		const std::string currentTimeText = std::to_string(currentTime);
		DrawTextRow("現在時間", currentTimeText.c_str());

		DrawSubHeader("クリップ");
		int32_t clipCount = static_cast<int32_t>(modelData.animationClips.size());
		DrawIntRow("クリップ数", clipCount);
		if (hasModelData && !modelData.animationClips.empty()) {
			DrawTextRow("先頭クリップ", modelData.animationClips.front().name.c_str());
			const std::string clipDurationText = std::to_string(modelData.animationClips.front().durationSeconds);
			DrawTextRow("先頭クリップ長さ", clipDurationText.c_str());
		}
	}

	void DrawAnimatorComponent(EditorComponent& component) {
		DrawTextRow("説明", "Animator Controller の状態管理です。");
		DrawFloatRow("速度", component.animationSpeed, 0.1f, 0.0f, 10.0f);
		DrawIntRow("状態", component.animatorState);
		DrawTextRow("注", "状態 = 0:Float / 1:Rotate / 2:Pulse / 3:Bob");
	}

	void DrawAvatarMaskComponent(EditorComponent& component) {
		DrawTextRow("説明", "アニメーションを適用する体の範囲を制御するコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
	}

	void DrawAudioListenerComponent(EditorComponent& component) {
		DrawTextRow("説明", "Scene 内の音を聞く位置です。");
		DrawTextRow("状態", component.isActive ? "アクティブ (聞こえています)" : "無効");
		DrawTextRow("注", "AudioSource + AudioReverbZone の距離判定に使われます。");
	}

	void DrawPlayableDirectorComponent(EditorComponent& component) {
		DrawTextRow("説明", "Timeline / Playable を再生するコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawCheckboxRow("自動再生", component.animationPlayOnAwake);
		DrawFloatRow("速度", component.animationSpeed, 0.1f, 0.0f, 10.0f);
		DrawCheckboxRow("ループ", component.animationLoop);
	}

	void DrawEventSystemComponent(EditorComponent& component) {
		DrawTextRow("説明", "UI や入力イベントを扱うコンポーネントです。");
		DrawIntRow("優先度", component.physicsLayer);
	}

	void DrawStandaloneInputModuleComponent(EditorComponent& component) {
		DrawTextRow("説明", "旧 Input 用の UI 入力モジュールです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawInputSystemUIInputModuleComponent(EditorComponent& component) {
		DrawTextRow("説明", "新 Input System 用の UI 入力モジュールです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawPlayerInputManagerComponent(EditorComponent& component) {
		DrawTextRow("説明", "複数プレイヤーの参加と入力を管理するコンポーネントです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawTouchInputModuleComponent(EditorComponent& component) {
		DrawTextRow("説明", "タッチ操作を UI イベントへ渡すモジュールです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawParticleSystemComponent(EditorComponent& component) {
		DrawTextRow("説明", "パーティクルを発生させるエフェクトコンポーネントです。");
		DrawFloatRow("発生レート", component.particleRate, 1.0f, 0.0f, 1000.0f);
		DrawFloatRow("寿命", component.particleLifetime, 0.1f, 0.1f, 60.0f);
		DrawFloatRow("速度", component.particleSpeed, 0.1f, 0.0f, 100.0f);
		DrawFloatRow("サイズ", component.particleSize, 0.01f, 0.01f, 10.0f);
		DrawColor3Row("色", component.color);
		DrawCheckboxRow("自動再生", component.animationPlayOnAwake);
	}

	void DrawVisualEffectComponent(EditorComponent& component) {
		DrawTextRow("説明", "VFX Graph 相当のエフェクトコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
	}

	void DrawFlareLayerComponent(EditorComponent& component) {
		DrawTextRow("説明", "カメラにレンズフレアを重ねるための設定です。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawReflectionProbeComponent(EditorComponent& component) {
		const char* reflectionModeItems[] = {
			"スクリーンスペース反射",
			"キューブマップ反射",
			"平面反射",
		};
		int32_t reflectionModeIndex = 0;
		if (component.assetPath == "Cubemap") {
			reflectionModeIndex = 1;
		}
		else if (component.assetPath == "Planar") {
			reflectionModeIndex = 2;
		}

		if (DrawComboRow("種類", reflectionModeIndex, reflectionModeItems, static_cast<int32_t>(_countof(reflectionModeItems)))) {
			if (reflectionModeIndex == 1) {
				component.assetPath = "Cubemap";
			}
			else if (reflectionModeIndex == 2) {
				component.assetPath = "Planar";
			}
			else {
				component.assetPath = "ScreenSpace";
			}
		}

		DrawTextRow("説明", "反射方式を切り替えるコンポーネントです。スクリーンスペース反射は床や濡れ面、キューブマップ反射は金属や球体、平面反射は鏡や磨かれた床向けです。");
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("反射の粗さ", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 1000.0f);
	}

	void DrawLightProbeGroupComponent(EditorComponent& component) {
		DrawTextRow("説明", "ライトプローブの配置をまとめるコンポーネントです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawLightProbeProxyVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "大きな動的物体向けにライトプローブを補間するコンポーネントです。");
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 1000.0f);
	}

	void DrawVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "URP / HDRP のポストプロセスや環境設定を持つコンポーネントです。");
		DrawFloatRow("重み", component.intensity, 0.01f, 0.0f, 1.0f);
	}

	void DrawPlatformEffector2DComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D の片方向床を作る Effector です。");
		DrawCheckboxRow("片面", component.isTrigger);
	}

	void DrawSurfaceEffector2DComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D 当たり判定の表面に沿って移動力を与えるエフェクターです。");
		DrawFloatRow("力", component.intensity, 0.1f, 0.0f, 100.0f);
	}

	void DrawAreaEffector2DComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D の範囲内に力を加える Effector です。");
		DrawFloatRow("力", component.intensity, 0.1f, 0.0f, 100.0f);
		DrawVector3Row("方向", component.velocity, 0.01f, 0.0f, 0.0f);
	}

	void DrawPointEffector2DComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D の点へ向かう力、または離れる力を加える Effector です。");
		DrawFloatRow("力", component.intensity, 0.1f, 0.0f, 100.0f);
	}

	void DrawBuoyancyEffector2DComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D の浮力を加える Effector です。");
		DrawFloatRow("浮力", component.intensity, 0.01f, 0.0f, 10.0f);
	}

	void DrawLensFlareComponent(EditorComponent& component) {
		DrawTextRow("説明", "光源やカメラにフレア表現を足すコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawFloatRow("明るさ", component.intensity, 0.01f, 0.0f, 10.0f);
		DrawColor3Row("色", component.color);
	}

	void DrawProjectorComponent(EditorComponent& component) {
		DrawTextRow("説明", "Texture や影を Scene に投影するコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawFloatRow("視野角", component.intensity, 1.0f, 1.0f, 180.0f);
	}

	void DrawDecalProjectorComponent(EditorComponent& component) {
		DrawTextRow("説明", "URP / HDRP の Decal を投影するコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
	}

	void DrawTerrainComponent(EditorComponent& component) {
		DrawTextRow("説明", "地形を扱うコンポーネントです。");
		DrawVector3Row("サイズ", component.colliderSize, 1.0f, 1.0f, 10000.0f);
	}

	void DrawTilemapComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D Tile を配置するコンポーネントです。");
		DrawTextRow("アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
		DrawVector3Row("サイズ", component.colliderSize, 1.0f, 1.0f, 1000.0f);
	}

	void DrawGridComponent(EditorComponent& component) {
		DrawTextRow("説明", "Tilemap の親になる Grid コンポーネントです。");
		DrawVector3Row("セルサイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
	}

	void DrawComponentBody(EditorInspectorPanelContext& context, EditorGameObject& gameObject, EditorComponent& component) {
		// Component 種類ごとに Inspector の中身を分ける
		switch (component.type) {
		case EditorComponentType::MeshFilter:
			DrawTextRow("メッシュ", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
			break;
		case EditorComponentType::ModelRenderer:
		case EditorComponentType::SkinnedMeshRenderer:
			DrawRendererComponent(context, gameObject, component, "Lit");
			break;
		case EditorComponentType::SpriteRenderer:
			DrawRendererComponent(component, "Sprite Lit");
			break;
		case EditorComponentType::LineRenderer:
		case EditorComponentType::TrailRenderer:
		case EditorComponentType::BillboardRenderer:
		case EditorComponentType::ParticleSystemRenderer:
		case EditorComponentType::TilemapRenderer:
			DrawRendererComponent(component, "Effect Lit");
			break;
		case EditorComponentType::CanvasRenderer:
			DrawUIComponent(component, "Canvas 上の UI 要素を描画する Renderer です。");
			break;
		case EditorComponentType::Light:
			DrawLightComponent(component);
			break;
		case EditorComponentType::Camera:
			DrawTextRow("説明", "GameView に描く実行カメラです。追従対象IDを設定すると、その GameObject へ簡易追従します。");
			DrawTextRow("投影", "Perspective");
			DrawDisabledComboRow("描画順", "Main Camera");
			DrawGameObjectReferenceRow(context, gameObject, "追従対象", component.connectedGameObjectId, "未設定", false);
			DrawTextRow("追従オフセット", "この GameObject の位置をオフセットとして使います。0,0,0 なら 0,2,-6 を使います。");
			break;
		case EditorComponentType::FlareLayer:
			DrawFlareLayerComponent(component);
			break;
		case EditorComponentType::CinemachineCamera:
			DrawTextRow("状態", component.isActive ? "有効" : "無効");
			DrawTextRow("説明", "簡易追従カメラです。追従対象IDを設定すると、その GameObject へオフセット追従します。");
			DrawGameObjectReferenceRow(context, gameObject, "追従対象", component.connectedGameObjectId, "未設定", false);
			DrawTextRow("追従オフセット", "この GameObject の位置をオフセットとして使います。0,0,0 なら 0,2,-6 を使います。");
			break;
		case EditorComponentType::ReflectionProbe:
			DrawReflectionProbeComponent(component);
			break;
		case EditorComponentType::LightProbeGroup:
			DrawLightProbeGroupComponent(component);
			break;
		case EditorComponentType::LightProbeProxyVolume:
			DrawLightProbeProxyVolumeComponent(component);
			break;
		case EditorComponentType::Volume:
			DrawVolumeComponent(component);
			break;
		case EditorComponentType::AudioSource:
			DrawTextRow("音声アセット", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
			DrawFloatRow("音量", component.audioVolume, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("ピッチ", component.audioPitch, 0.01f, 0.0f, 3.0f);
			DrawCheckboxRow("ループ", component.audioLoop);
			DrawCheckboxRow("自動再生", component.audioPlayOnAwake);
			break;
		case EditorComponentType::AudioReverbZone:
			DrawAudioFilterComponent(component, "範囲内の音へリバーブを加えるコンポーネントです。");
			DrawFloatRow("最小距離", component.colliderRadius, 0.01f, 0.0f, 100.0f);
			DrawFloatRow("最大距離", component.colliderSize.x, 0.01f, 0.0f, 500.0f);
			break;
		case EditorComponentType::AudioLowPassFilter:
			DrawAudioFilterComponent(component, "高音を抑えて低音を通すオーディオフィルターです。");
			break;
		case EditorComponentType::AudioHighPassFilter:
			DrawAudioFilterComponent(component, "低音を抑えて高音を通すオーディオフィルターです。");
			break;
		case EditorComponentType::AudioEchoFilter:
			DrawAudioFilterComponent(component, "音にエコーを加えるオーディオフィルターです。");
			break;
		case EditorComponentType::AudioDistortionFilter:
			DrawAudioFilterComponent(component, "音に歪みを加えるオーディオフィルターです。");
			break;
		case EditorComponentType::AudioReverbFilter:
			DrawAudioFilterComponent(component, "音に残響を加えるオーディオフィルターです。");
			break;
		case EditorComponentType::AudioChorusFilter:
			DrawAudioFilterComponent(component, "音にコーラス効果を加えるオーディオフィルターです。");
			break;
		case EditorComponentType::RigidBody:
		case EditorComponentType::RigidBody2D:
			DrawRigidBodyComponent(component);
			break;
		case EditorComponentType::BoxCollider:
		case EditorComponentType::BoxCollider2D:
			DrawBoxColliderComponent(component);
			break;
		case EditorComponentType::SphereCollider:
		case EditorComponentType::CircleCollider2D:
			DrawSphereColliderComponent(component);
			break;
		case EditorComponentType::CapsuleCollider:
		case EditorComponentType::CharacterController:
		case EditorComponentType::CapsuleCollider2D:
			DrawCapsuleColliderComponent(component);
			break;
		case EditorComponentType::MeshCollider:
		case EditorComponentType::TerrainCollider:
		case EditorComponentType::PolygonCollider2D:
		case EditorComponentType::EdgeCollider2D:
		case EditorComponentType::CompositeCollider2D:
		case EditorComponentType::TilemapCollider2D:
		case EditorComponentType::CustomCollider2D:
			DrawMeshColliderComponent(gameObject, component);
			break;
		case EditorComponentType::WheelCollider:
			DrawTextRow("説明", "車輪用の 3D 当たり判定です。");
			DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.01f, 100.0f);
			DrawFloatRow("反発", component.bounciness, 0.01f, 0.0f, 1.0f);
			break;
		case EditorComponentType::ConstantForce:
			DrawConstantForceComponent(component);
			break;
		case EditorComponentType::HingeJoint:
			DrawJointComponent(context, gameObject, component, "ちょうつがいのように回転軸を固定する Joint です。");
			break;
		case EditorComponentType::FixedJoint:
			DrawJointComponent(context, gameObject, component, "2 つの Rigidbody を固定接続する Joint です。");
			break;
		case EditorComponentType::SpringJoint:
			DrawJointComponent(context, gameObject, component, "バネのように距離を保つ Joint です。");
			break;
		case EditorComponentType::ConfigurableJoint:
			DrawJointComponent(context, gameObject, component, "移動軸や回転軸を細かく制限する Joint です。");
			break;
		case EditorComponentType::CharacterJoint:
			DrawJointComponent(context, gameObject, component, "ラグドール向けの回転制限 Joint です。");
			break;
		case EditorComponentType::DistanceJoint2D:
			DrawJointComponent(context, gameObject, component, "2D の距離制約 Joint です。");
			break;
		case EditorComponentType::HingeJoint2D:
			DrawJointComponent(context, gameObject, component, "2D の回転接続 Joint です。");
			break;
		case EditorComponentType::SpringJoint2D:
			DrawJointComponent(context, gameObject, component, "2D のバネ接続 Joint です。");
			break;
		case EditorComponentType::FixedJoint2D:
			DrawJointComponent(context, gameObject, component, "2D の固定接続 Joint です。");
			break;
		case EditorComponentType::SliderJoint2D:
			DrawJointComponent(context, gameObject, component, "2D のスライド制約 Joint です。");
			break;
		case EditorComponentType::WheelJoint2D:
			DrawJointComponent(context, gameObject, component, "2D の車輪用 Joint です。");
			break;
		case EditorComponentType::PlatformEffector2D:
			DrawPlatformEffector2DComponent(component);
			break;
		case EditorComponentType::SurfaceEffector2D:
			DrawSurfaceEffector2DComponent(component);
			break;
		case EditorComponentType::AreaEffector2D:
			DrawAreaEffector2DComponent(component);
			break;
		case EditorComponentType::PointEffector2D:
			DrawPointEffector2DComponent(component);
			break;
		case EditorComponentType::BuoyancyEffector2D:
			DrawBuoyancyEffector2DComponent(component);
			break;
		case EditorComponentType::Input:
			DrawInputComponent(component);
			break;
		case EditorComponentType::Animation:
			DrawAnimationComponent(context, gameObject, component);
			break;
		case EditorComponentType::Animator:
			DrawAnimatorComponent(component);
			break;
		case EditorComponentType::AvatarMask:
			DrawAvatarMaskComponent(component);
			break;
		case EditorComponentType::AudioListener:
			DrawAudioListenerComponent(component);
			break;
		case EditorComponentType::AimConstraint:
			DrawAimConstraintComponent(component);
			break;
		case EditorComponentType::LookAtConstraint:
			DrawLookAtConstraintComponent(component);
			break;
		case EditorComponentType::ParentConstraint:
			DrawParentConstraintComponent(component);
			break;
		case EditorComponentType::PositionConstraint:
			DrawPositionConstraintComponent(component);
			break;
		case EditorComponentType::RotationConstraint:
			DrawRotationConstraintComponent(component);
			break;
		case EditorComponentType::ScaleConstraint:
			DrawScaleConstraintComponent(component);
			break;
		case EditorComponentType::EventSystem:
			DrawEventSystemComponent(component);
			break;
		case EditorComponentType::StandaloneInputModule:
			DrawStandaloneInputModuleComponent(component);
			break;
		case EditorComponentType::InputSystemUIInputModule:
			DrawInputSystemUIInputModuleComponent(component);
			break;
			case EditorComponentType::PlayerInput:
				DrawPlayerInputComponent(context, component);
				break;
		case EditorComponentType::PlayerInputManager:
			DrawPlayerInputManagerComponent(component);
			break;
		case EditorComponentType::TouchInputModule:
			DrawTouchInputModuleComponent(component);
			break;
		case EditorComponentType::NavigationAgent:
			DrawNavMeshAgentComponent(context, gameObject, component);
			break;
		case EditorComponentType::NavMeshObstacle:
			DrawNavMeshObstacleComponent(component);
			break;
		case EditorComponentType::NavMeshSurface:
			DrawNavMeshSurfaceComponent(component);
			break;
		case EditorComponentType::NavMeshModifier:
			DrawNavMeshModifierComponent(component);
			break;
		case EditorComponentType::NavMeshModifierVolume:
			DrawNavMeshModifierVolumeComponent(component);
			break;
		case EditorComponentType::NavMeshLink:
			DrawNavMeshLinkComponent(context, gameObject, component);
			break;
		case EditorComponentType::AIBehaviorTree:
			DrawAiAgentComponent(context, gameObject, component, "条件を上から評価して、移動・攻撃・待機などの行動を選ぶ AI です。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIBehaviorBlackboard:
			DrawAiDataComponent(context, gameObject, component, "AI が見る変数や状態をまとめる共有データです。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIBehaviorSelector:
			DrawAiDataComponent(context, gameObject, component, "複数の行動候補から、成功したものを 1 つ選ぶ分岐です。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIBehaviorSequence:
			DrawAiDataComponent(context, gameObject, component, "複数の処理を上から順番に実行する流れです。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIBehaviorTask:
			DrawAiDataComponent(context, gameObject, component, "移動、攻撃、待機など実際の処理を呼ぶ末端行動です。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIBehaviorDecorator:
			DrawAiDataComponent(context, gameObject, component, "距離や HP などの条件で、行動を実行するか止める制御です。", "BehaviorTree.CPP 4.9.0");
			break;
		case EditorComponentType::AIStateMachine:
			DrawAiAgentComponent(context, gameObject, component, "待機中、追跡中、攻撃中のような状態を切り替える AI です。", "HFSM2 2.12.1");
			break;
		case EditorComponentType::AIState:
			DrawAiDataComponent(context, gameObject, component, "HFSM2 の 1 つの状態を表す部品です。", "HFSM2 2.12.1");
			break;
		case EditorComponentType::AIStateTransition:
			DrawAiDataComponent(context, gameObject, component, "状態を切り替える条件と接続先を持つ部品です。", "HFSM2 2.12.1");
			break;
		case EditorComponentType::AIGoapPlanner:
			DrawAiAgentComponent(context, gameObject, component, "目標を達成するために必要な行動順を自動で選ぶ AI です。", "cppGOAP");
			break;
		case EditorComponentType::AIGoapGoal:
			DrawAiDataComponent(context, gameObject, component, "GOAP が達成したい目標条件です。", "cppGOAP");
			break;
		case EditorComponentType::AIGoapAction:
			DrawAiDataComponent(context, gameObject, component, "GOAP が選択する行動と前提条件です。", "cppGOAP");
			break;
		case EditorComponentType::AIGoapWorldState:
			DrawAiDataComponent(context, gameObject, component, "GOAP の世界状態を表すデータです。", "cppGOAP");
			break;
		case EditorComponentType::AIHtnPlanner:
			DrawAiAgentComponent(context, gameObject, component, "大きな目的を小さなタスクへ分解して行動する AI です。", "Fluid HTN 0.4.1");
			break;
		case EditorComponentType::AIHtnDomain:
			DrawAiDataComponent(context, gameObject, component, "HTN のタスク体系全体を表す Domain です。", "Fluid HTN 0.4.1");
			break;
		case EditorComponentType::AIHtnTask:
			DrawAiDataComponent(context, gameObject, component, "HTN の実行単位になる Task です。", "Fluid HTN 0.4.1");
			break;
		case EditorComponentType::AIHtnMethod:
			DrawAiDataComponent(context, gameObject, component, "HTN の Task を分解する Method です。", "Fluid HTN 0.4.1");
			break;
		case EditorComponentType::AIPathfindingAgent:
			DrawAiAgentComponent(context, gameObject, component, "障害物を避けながら目的地へ向かう移動 AI です。", "MicroPather / RecastNavigation");
			break;
		case EditorComponentType::AIMicroPatherGrid:
			DrawAiDataComponent(context, gameObject, component, "MicroPather で使う Grid 探索設定です。", "MicroPather");
			break;
		case EditorComponentType::AIRecastNavMeshBuilder:
			DrawAiDataComponent(context, gameObject, component, "RecastNavigation で NavMesh を生成する設定です。", "RecastNavigation 1.6.0");
			break;
		case EditorComponentType::AIRecastCrowdAgent:
			DrawAiAgentComponent(context, gameObject, component, "Recast DetourCrowd を想定した群衆 Agent です。", "RecastNavigation 1.6.0");
			break;
		case EditorComponentType::AIPathRequest:
			DrawAiDataComponent(context, gameObject, component, "目的地までの経路要求を表す部品です。", "MicroPather / RecastNavigation");
			break;
		case EditorComponentType::AIDynamicObstacle:
			DrawAiDataComponent(context, gameObject, component, "AI 経路探索へ渡す動的障害物です。", "RecastNavigation 1.6.0");
			break;
		case EditorComponentType::AISteeringAgent:
			DrawAiAgentComponent(context, gameObject, component, "向きや速度をなめらかに変えて動かす操舵 AI です。", "OpenSteer");
			break;
		case EditorComponentType::AISeekSteering:
			DrawAiAgentComponent(context, gameObject, component, "対象へ向かう Seek 操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIFleeSteering:
			DrawAiAgentComponent(context, gameObject, component, "対象から離れる Flee 操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIArriveSteering:
			DrawAiAgentComponent(context, gameObject, component, "目的地の近くで減速する Arrive 操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIPursuitSteering:
			DrawAiAgentComponent(context, gameObject, component, "移動する対象を先読みして追う Pursuit 操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIWanderSteering:
			DrawAiAgentComponent(context, gameObject, component, "ランダムに歩き回る Wander 操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIObstacleAvoidanceSteering:
			DrawAiAgentComponent(context, gameObject, component, "前方の Collider を避ける障害物回避操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIFlockSteering:
			DrawAiAgentComponent(context, gameObject, component, "分離・整列・結合をまとめた群れ操舵です。", "OpenSteer");
			break;
		case EditorComponentType::AIVisionSensor:
			DrawAiVisionSensorComponent(context, gameObject, component);
			break;
		case EditorComponentType::AIOpenCvCamera:
			DrawAiDataComponent(context, gameObject, component, "OpenCV へ渡すカメラ入力の入口です。", "OpenCV");
			break;
		case EditorComponentType::AIOpenCvObjectDetector:
			DrawAiDataComponent(context, gameObject, component, "OpenCV で画像内の対象を検出する入口です。", "OpenCV");
			break;
		case EditorComponentType::AIOpenCvColorTracker:
			DrawAiDataComponent(context, gameObject, component, "OpenCV で指定色を追跡する入口です。", "OpenCV");
			break;
		case EditorComponentType::AIMotionSensor:
			DrawAiVisionSensorComponent(context, gameObject, component);
			break;
		case EditorComponentType::AIWhisperSpeechRecognizer:
			DrawAiDataComponent(context, gameObject, component, "Whisper で音声を文字列へ変換する入口です。", "Whisper");
			break;
		case EditorComponentType::AIVoiceCommand:
			DrawAiDataComponent(context, gameObject, component, "音声認識結果からゲーム内コマンドを発火する入口です。", "Whisper");
			break;
		case EditorComponentType::LocalMove:
			DrawLocalMoveComponent(component);
			break;
		case EditorComponentType::RollingMove:
			DrawRollingMoveComponent(component);
			break;
		case EditorComponentType::PlayableDirector:
			DrawPlayableDirectorComponent(component);
			break;
		case EditorComponentType::Script:
			DrawNativeScriptComponent(context, gameObject, component, "C++ DLL を Play 中に読み込み、更新で差し替えられるユーザースクリプトです。");
			break;
		case EditorComponentType::MonoBehaviour:
			DrawNativeScriptComponent(context, gameObject, component, "MonoBehaviour 風に使う C++ DLL コンポーネントです。");
			break;
		case EditorComponentType::HapticSource:
			DrawHapticSourceComponent(component);
			break;
		case EditorComponentType::ParticleSystem:
			DrawParticleSystemComponent(component);
			break;
		case EditorComponentType::VisualEffect:
			DrawVisualEffectComponent(component);
			break;
		case EditorComponentType::LensFlare:
			DrawLensFlareComponent(component);
			break;
		case EditorComponentType::Projector:
			DrawProjectorComponent(component);
			break;
		case EditorComponentType::DecalProjector:
			DrawDecalProjectorComponent(component);
			break;
		case EditorComponentType::Terrain:
			DrawTerrainComponent(component);
			break;
		case EditorComponentType::Tilemap:
			DrawTilemapComponent(component);
			break;
		case EditorComponentType::Grid:
			DrawGridComponent(component);
			break;
		case EditorComponentType::Canvas:
		case EditorComponentType::Image:
		case EditorComponentType::Text:
		case EditorComponentType::RectTransform:
		case EditorComponentType::CanvasScaler:
		case EditorComponentType::GraphicRaycaster:
		case EditorComponentType::RawImage:
		case EditorComponentType::TextMeshProUGUI:
		case EditorComponentType::Button:
		case EditorComponentType::Toggle:
		case EditorComponentType::Slider:
		case EditorComponentType::Scrollbar:
		case EditorComponentType::Dropdown:
		case EditorComponentType::TMPDropdown:
		case EditorComponentType::InputField:
		case EditorComponentType::TMPInputField:
		case EditorComponentType::ScrollRect:
		case EditorComponentType::Mask:
		case EditorComponentType::RectMask2D:
		case EditorComponentType::HorizontalLayoutGroup:
		case EditorComponentType::VerticalLayoutGroup:
		case EditorComponentType::GridLayoutGroup:
		case EditorComponentType::ContentSizeFitter:
		case EditorComponentType::AspectRatioFitter:
		case EditorComponentType::LayoutElement:
			DrawUIComponent(component, "UI 表示用コンポーネントです。");
			break;
		case EditorComponentType::Transform:
		case EditorComponentType::Count:
		default:
			break;
		}
	}

	void DrawAddComponentPopup(EditorInspectorPanelContext& context, EditorGameObject& gameObject) {
		DrawSectionSpace();
		DrawCenteredButtonAndOpenPopup("コンポーネントを追加", "AddComponentPopup");

		if (ImGui::BeginPopup("AddComponentPopup")) {
			static char componentSearchText[96] = {};  // Add Component Popup 内の検索文字列
			const char* categoryNames[] = {
				"基本",
				"描画・レンダリング",
				"カメラ",
				"ライト・環境",
				"3D物理",
				"2D物理",
				"アニメーション",
				"オーディオ",
				"UI",
				"入力・イベント",
				"ナビゲーション",
				"AI",
				"エフェクト",
				"地形・タイルマップ",
				"FeelKit",
			};

			auto addComponent = [&](const ComponentAddEntry& entry) {
				context.editorScene.PushUndo();
				context.selectedAddComponentIndex = static_cast<int32_t>(entry.type);
				context.editorScene.AddComponent(gameObject.id, entry.type);
				SyncEditorSelection(context);
			};

			auto drawEntry = [&](const ComponentAddEntry& entry) {
				bool hasComponent = context.editorScene.HasComponent(gameObject.id, entry.type);
				if (ImGui::MenuItem(entry.displayName, nullptr, false, !hasComponent)) {
					addComponent(entry);
				}
			};

			auto drawAiEntries = [&]() {
				const char* subCategoryNames[] = {
					"行動制御",
					"状態管理",
					"目標計画",
					"タスク計画",
					"経路探索",
					"移動操舵",
					"知覚",
					"音声",
				};

				for (const char* subCategoryName : subCategoryNames) {
					if (!ImGui::BeginMenu(subCategoryName)) {
						continue;
					}

					bool hasAnyEntry = false;  // AI の中を用途別に分けるため、該当 Component だけを表示する。
					for (const ComponentAddEntry& entry : kComponentAddEntries) {
						if (std::strcmp(entry.categoryName, "AI") != 0 ||
							std::strcmp(GetAiSubCategory(entry.type), subCategoryName) != 0) {
							continue;
						}

						hasAnyEntry = true;
						drawEntry(entry);
					}

					if (!hasAnyEntry) {
						ImGui::MenuItem("未登録", nullptr, false, false);
					}

					ImGui::EndMenu();
				}
			};

			ImGui::TextUnformatted("コンポーネント");
			ImGui::SetNextItemWidth(280.0f);
			ImGui::InputTextWithHint("##ComponentSearch", "検索", componentSearchText, _countof(componentSearchText));
			ImGui::Separator();

			if (EditorAssetUtility::HasFilterText(componentSearchText)) {
				ImGui::BeginChild("ComponentSearchResults", ImVec2(300.0f, 280.0f), ImGuiChildFlags_Borders);

				for (const ComponentAddEntry& entry : kComponentAddEntries) {
					if (!ContainsIgnoreCase(entry.displayName, componentSearchText) &&
						!ContainsIgnoreCase(entry.categoryName, componentSearchText)) {
						continue;
					}

					bool hasComponent = context.editorScene.HasComponent(gameObject.id, entry.type);
					if (hasComponent) {
						ImGui::BeginDisabled();
					}

					if (ImGui::Selectable(entry.displayName, false)) {
						addComponent(entry);
					}

					if (hasComponent) {
						ImGui::EndDisabled();
					}
				}

				ImGui::EndChild();
			}
			else {
				ImGui::BeginChild("ComponentCategories", ImVec2(300.0f, 320.0f), ImGuiChildFlags_Borders);

				for (const char* categoryName : categoryNames) {
					if (ImGui::BeginMenu(categoryName)) {
						bool hasAnyEntry = false;  // 空カテゴリでも Unity の項目数に近い見た目を残すための判定

						if (std::strcmp(categoryName, "AI") == 0) {
							drawAiEntries();
							hasAnyEntry = true;
						}

						for (const ComponentAddEntry& entry : kComponentAddEntries) {
							if (std::strcmp(entry.categoryName, categoryName) != 0) {
								continue;
							}
							if (std::strcmp(categoryName, "AI") == 0) {
								continue;
							}

							hasAnyEntry = true;
							drawEntry(entry);
						}

						if (!hasAnyEntry) {
							ImGui::MenuItem("未登録", nullptr, false, false);
						}

						ImGui::EndMenu();
					}
				}

				ImGui::EndChild();
			}

			ImGui::EndPopup();
		}
	}

	void DrawObjectOperationPanel(EditorInspectorPanelContext& context, EditorGameObject*& selectedEditorGameObject) {
		if (selectedEditorGameObject == nullptr) {
			return;
		}

		if (ImGui::CollapsingHeader("オブジェクト操作")) {
			if (ImGui::Button("複製")) {
				context.editorScene.PushUndo();
				context.selectedEditorGameObjectId =
					context.editorScene.DuplicateGameObject(selectedEditorGameObject->id);
				SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
				context.previousSelectedEditorGameObjectId = -1;
				SyncEditorSelection(context);
			}

			ImGui::SameLine();

			if (ImGui::Button("削除")) {
				ImGui::OpenPopup("GameObject削除確認");
			}

			ImGui::SameLine();

			if (ImGui::Button("Undo")) {
				context.editorScene.Undo();
				SyncEditorSelection(context);
			}

			ImGui::SameLine();

			if (ImGui::Button("Redo")) {
				context.editorScene.Redo();
				SyncEditorSelection(context);
			}

			if (ImGui::Button("Scene 保存")) {
				if (!g_currentScenePath.empty()) {
					context.editorScene.SaveScene(g_currentScenePath);
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Scene 読込")) {
				if (!g_currentScenePath.empty() &&
					context.editorScene.LoadScene(g_currentScenePath) &&
					!context.editorScene.GetGameObjects().empty()) {
					context.selectedEditorGameObjectId = context.editorScene.GetGameObjects()[0].id;
					SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
					context.previousSelectedEditorGameObjectId = -1;
					SyncEditorSelection(context);
				}
			}

			if (ImGui::Button("Prefab 保存")) {
				context.editorScene.SavePrefab(selectedEditorGameObject->id, kEditorPrefabPath);
			}

			ImGui::SameLine();

			if (ImGui::Button("Prefab 生成")) {
				context.editorScene.PushUndo();
				int32_t prefabId = context.editorScene.InstantiatePrefab(kEditorPrefabPath);

				if (prefabId >= 0) {
					context.selectedEditorGameObjectId = prefabId;
					SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
					context.previousSelectedEditorGameObjectId = -1;
					SyncEditorSelection(context);
				}
			}
		}

		if (ImGui::BeginPopupModal("GameObject削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("選択中のGameObjectを削除しますか？");

			if (ImGui::Button("削除する")) {
				context.editorScene.PushUndo();
				context.editorScene.DeleteGameObject(context.selectedEditorGameObjectId);
				context.selectedEditorGameObjectId = context.editorScene.GetGameObjects().empty()
					                                     ? -1
					                                     : context.editorScene.GetGameObjects()[0].id;
				SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
				context.previousSelectedEditorGameObjectId = -1;
				context.sceneSynchronizer.Update(
					context.textureFilePaths,
					context.selectedPlacedSceneObjectIndex);
				SyncEditorSelection(context);
				selectedEditorGameObject =
					context.editorScene.FindGameObject(context.selectedEditorGameObjectId);
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("キャンセル")) {
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void DrawEnvironmentPanel(EditorInspectorPanelContext& context) {
		if (ImGui::CollapsingHeader("環境 / 背景")) {
			ImGui::ColorEdit4("背景色", context.sceneClearColor);

			if (context.directionalLightData != nullptr) {
				ImGui::Separator();
				ImGui::TextUnformatted("天球 / ビューポートシェーディング");
				ImGui::Text("環境画像: %s", g_environmentTextureAssetPath.empty() ? "未設定" : g_environmentTextureAssetPath.c_str());
				bool usesEnvironmentTexture = context.directionalLightData->environmentTextureEnabled >= 0.5f;
				if (ImGui::Checkbox("環境画像を使う", &usesEnvironmentTexture)) {
					context.directionalLightData->environmentTextureEnabled = usesEnvironmentTexture ? 1.0f : 0.0f;
				}
				ImGui::DragFloat("環境画像の強さ", &context.directionalLightData->environmentTextureIntensity, 0.01f, 0.0f, 16.0f);
				ImGui::DragFloat("環境画像の回転", &context.directionalLightData->environmentTextureRotation, 0.01f, -6.28318f, 6.28318f);
				ImGui::DragFloat("環境画像の粗さ補正", &context.directionalLightData->environmentTextureMipBias, 0.01f, 0.0f, 16.0f);
				if (!context.selectedAssetPath.empty() &&
					(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".hdr") ||
					 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".dds") ||
					 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".png") ||
					 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpg") ||
					 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpeg"))) {
					if (ImGui::Button("選択中アセットを環境画像に設定", ImVec2(-1.0f, 0.0f))) {
						g_environmentTextureAssetPath = context.selectedAssetPath;
						g_isEnvironmentTextureDirty = true;
						context.directionalLightData->environmentTextureEnabled = 1.0f;
					}
				}
				if (ImGui::Button("環境画像を解除", ImVec2(-1.0f, 0.0f))) {
					g_environmentTextureAssetPath.clear();
					g_loadedEnvironmentTextureAssetPath.clear();
					g_isEnvironmentTextureDirty = true;
					context.directionalLightData->environmentTextureEnabled = 0.0f;
				}
				ImGui::ColorEdit3("天球上色", &context.directionalLightData->skyUpperColor.x);
				ImGui::ColorEdit3("天球下色", &context.directionalLightData->skyLowerColor.x);
				ImGui::DragFloat("天球明るさ", &context.directionalLightData->skyIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("天球放射", &context.directionalLightData->skyEmission, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("環境光", &context.directionalLightData->ambientIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("反射寄与", &context.directionalLightData->reflectionIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("空の切替", &context.directionalLightData->horizonSharpness, 0.01f, 0.01f, 8.0f);
			}

			ImGui::Checkbox("ギズモ表示", &context.isSceneGizmoVisible);
			ImGui::Checkbox("ライトアイコン", &context.isLightGizmoVisible);
			ImGui::Checkbox("カメラアイコン", &context.isCameraGizmoVisible);
		}
	}

	void DrawKeyNameComboRow(const char* label, std::string& keyName) {
		static const char* names[_countof(kDikEntries)] = {};
		if (names[0] == nullptr) {
			for (size_t keyIndex = 0; keyIndex < _countof(kDikEntries); ++keyIndex) {
				names[keyIndex] = kDikEntries[keyIndex].name;
			}
		}

		int32_t selectedIndex = 0;
		for (int32_t keyIndex = 0; keyIndex < static_cast<int32_t>(_countof(kDikEntries)); ++keyIndex) {
			if (keyName == kDikEntries[keyIndex].name) {
				selectedIndex = keyIndex;
				break;
			}
		}

		if (DrawComboRow(label, selectedIndex, names, static_cast<int32_t>(_countof(kDikEntries)))) {
			keyName = kDikEntries[selectedIndex].name;
		}
	}

	void DrawPhysicsSettingsPanel(EditorInspectorPanelContext& context) {
		if (!ImGui::CollapsingHeader("物理設定")) {
			return;
		}

		EditorPhysicsSettings& physicsSettings = context.editorScene.GetPhysicsSettings();
		DrawVector3Row("重力", physicsSettings.gravity, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("固定更新時間", physicsSettings.fixedTimeStep, 0.001f, 0.001f, 0.1f);
		DrawIntRow("衝突ステップ", physicsSettings.collisionStepCount);
		physicsSettings.fixedTimeStep = (std::clamp)(physicsSettings.fixedTimeStep, 0.001f, 0.1f);
		physicsSettings.collisionStepCount = (std::clamp)(physicsSettings.collisionStepCount, 1, 8);

		DrawSubHeader("デバッグ表示");
		DrawCheckboxRow("当たり判定の形", physicsSettings.drawColliderDebug);
		DrawCheckboxRow("接触点 / 法線", physicsSettings.drawContactDebug);
		DrawCheckboxRow("Ray / ShapeCast", physicsSettings.drawCastDebug);

		const char* layerItems[] = {
			"Default",
			"Player",
			"Enemy",
			"Ground",
			"Projectile",
			"Trigger",
			"UI",
			"Ignore Raycast"};
		DrawSubHeader("Layer Collision Matrix");
		if (ImGui::BeginTable(
			    "LayerCollisionMatrix",
			    kEditorPhysicsLayerCount + 1,
			    ImGuiTableFlags_Borders |
			    ImGuiTableFlags_SizingStretchProp |
			    ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 92.0f);
			for (int32_t layerIndex = 0; layerIndex < kEditorPhysicsLayerCount; ++layerIndex) {
				ImGui::TableSetupColumn(layerItems[layerIndex], ImGuiTableColumnFlags_WidthFixed, 28.0f);
			}

			ImGui::TableHeadersRow();
			for (int32_t firstLayer = 0; firstLayer < kEditorPhysicsLayerCount; ++firstLayer) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(layerItems[firstLayer]);

				for (int32_t secondLayer = 0; secondLayer < kEditorPhysicsLayerCount; ++secondLayer) {
					ImGui::TableNextColumn();
					ImGui::PushID((firstLayer * kEditorPhysicsLayerCount) + secondLayer);
					bool doesCollide = physicsSettings.layerCollisionMatrix[firstLayer][secondLayer];
					if (ImGui::Checkbox("##LayerPair", &doesCollide)) {
						physicsSettings.layerCollisionMatrix[firstLayer][secondLayer] = doesCollide;
						physicsSettings.layerCollisionMatrix[secondLayer][firstLayer] = doesCollide;
					}
					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}
	}

	void DrawMaterialPanel(EditorInspectorPanelContext& context) {
		if (context.sphereMaterialData == nullptr) {
			return;
		}

		if (ImGui::CollapsingHeader("モデル / マテリアル")) {
			bool isLighting = context.sphereMaterialData->enableLighting != FALSE;
			ImGui::Checkbox("ライティング", &isLighting);
			ImGui::ColorEdit4("マテリアル色", &context.sphereMaterialData->color.x);
			context.sphereMaterialData->enableLighting = isLighting ? TRUE : FALSE;
		}
	}

	void DrawToolPanel(EditorInspectorPanelContext& context) {
		if (ImGui::CollapsingHeader("操作ツール")) {
			ImGui::Checkbox("ローカル座標", &context.isGizmoLocalMode);
			ImGui::Checkbox("スナップ", &context.isGizmoSnapEnabled);
			ImGui::DragFloat3("スナップ値", context.gizmoSnapValues, 0.01f, 0.01f, 10.0f);
			ImGui::RadioButton("移動", &context.activeEditorTool, 1);
			ImGui::SameLine();
			ImGui::RadioButton("回転", &context.activeEditorTool, 2);
			ImGui::SameLine();
			ImGui::RadioButton("拡縮", &context.activeEditorTool, 3);
			ImGui::SameLine();
			ImGui::RadioButton("統合", &context.activeEditorTool, 4);
			ImGui::Checkbox("Scene操作ヘルプ", &context.isSceneAssistVisible);
			ImGui::TextDisabled("右上のViewCubeで視点方向を変更できます。");
		}
	}

	void DrawCameraControlPanel(EditorInspectorPanelContext& context) {
		if (ImGui::CollapsingHeader("シーンカメラ操作")) {
			ImGui::DragFloat("移動速度", &context.editorCameraMoveSpeed, 0.01f, 0.01f, 2.0f);
			ImGui::DragFloat("回転感度", &context.editorCameraRotateSpeed, 0.001f, 0.001f, 0.05f);
			ImGui::DragFloat("ホイール速度", &context.editorCameraWheelMoveSpeed, 0.01f, 0.05f, 5.0f);
			ImGui::DragFloat("中ボタン移動", &context.editorCameraPanSpeed, 0.001f, 0.001f, 0.1f);
			ImGui::DragFloat("Shift倍率", &context.editorCameraFastRate, 0.1f, 1.0f, 10.0f);
			ImGui::TextDisabled("右ドラッグ: 回転 / 中ドラッグ: 平行移動 / ホイール: 前後");
			ImGui::TextDisabled("WASD: 視点基準移動 / Q,E: 上下 / Shift: 高速");
		}
	}

	void DrawLegacyPreviewInspector(EditorInspectorPanelContext& context, Transforms& modelTransform, Transforms& spriteTransform) {
		ImGui::Separator();

		if (context.selectedSceneObject == 0) {
			if (DrawComponentHeader("モデル プレビュー", nullptr)) {
				DrawVector3Row("位置", modelTransform.translate, 0.01f, 0.0f, 0.0f);
				DrawRotationDegreeRow("回転", modelTransform.rotate, 0.1f);
				DrawVector3Row("スケール", modelTransform.scale, 0.01f, 0.01f, 10.0f);
			}

			if (ImGui::CollapsingHeader("UV Transform")) {
				ImGui::DragFloat2("UVスケール", &context.uvTransform.scale.x, 0.01f, 0.1f, 4.0f);
				float uvRotationDegree = context.uvTransform.rotate.z * kRadianToDegree;
				if (ImGui::DragFloat("UV回転", &uvRotationDegree, 0.1f, -180.0f, 180.0f, "%.1f")) {
					context.uvTransform.rotate.z = uvRotationDegree * kDegreeToRadian;
				}
				ImGui::DragFloat2("UV移動", &context.uvTransform.translate.x, 0.01f, -2.0f, 2.0f);
			}
		}
		else if (context.selectedSceneObject == 1) {
			if (DrawComponentHeader("スプライト プレビュー", nullptr)) {
				DrawVector3Row("位置", spriteTransform.translate, 1.0f, 0.0f, 0.0f);
				DrawRotationDegreeRow("回転", spriteTransform.rotate, 0.1f);
				DrawVector3Row("スケール", spriteTransform.scale, 1.0f, 1.0f, 1024.0f);
			}

			if (context.spriteMaterialData != nullptr &&
				ImGui::CollapsingHeader("スプライト マテリアル", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("色", &context.spriteMaterialData->color.x);
			}
		}
		else if (context.selectedSceneObject == 2) {
			if (context.directionalLightData != nullptr &&
				ImGui::CollapsingHeader("平行光源", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("色", &context.directionalLightData->color.x);
				ImGui::DragFloat3("方向", &context.directionalLightData->direction.x, 0.01f, -1.0f, 1.0f);
				ImGui::DragFloat("強さ", &context.directionalLightData->intensity, 0.01f, 0.0f, 4.0f);
				ImGui::Separator();
				ImGui::TextUnformatted("天球");
				ImGui::ColorEdit3("上空色", &context.directionalLightData->skyUpperColor.x);
				ImGui::ColorEdit3("下側色", &context.directionalLightData->skyLowerColor.x);
				ImGui::DragFloat("明るさ", &context.directionalLightData->skyIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("放射", &context.directionalLightData->skyEmission, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("環境光", &context.directionalLightData->ambientIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("反射", &context.directionalLightData->reflectionIntensity, 0.01f, 0.0f, 8.0f);
				ImGui::DragFloat("切替の鋭さ", &context.directionalLightData->horizonSharpness, 0.01f, 0.01f, 8.0f);
				ImGui::DragFloat3("アイコン位置", &context.directionalLightIconPosition.x, 0.01f);
			}
		}
		else {
			if (ImGui::CollapsingHeader("デバッグ カメラ", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat3("位置", &context.cameraTransform.translate.x, 0.1f);
				DrawRotationDegreeRow("回転", context.cameraTransform.rotate, 0.1f);

				if (ImGui::Button("カメラを初期化")) {
					context.cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
					context.cameraTransform.translate = {0.0f, 0.0f, -5.0f};
				}
			}
		}
	}

	void DrawInputActionsAssetEditor(EditorInspectorPanelContext& context) {
		if (context.selectedAssetPath.empty() ||
			!EditorAssetUtility::HasExtension(context.selectedAssetPath, ".inputactions")) {
			return;
		}

		static std::string loadedAssetPath;  // 今開いている Input Actions アセットのパス
		static std::vector<InputActionsAssetEntry> entries;  // GUI 編集中の Action 一覧
		static std::string statusMessage;  // 保存結果などを Inspector に表示する文言

		if (loadedAssetPath != context.selectedAssetPath) {
			loadedAssetPath = context.selectedAssetPath;
			entries = LoadInputActionsEntries(context.selectedAssetPath);
			statusMessage.clear();
		}

		if (!ImGui::CollapsingHeader("Input Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
			return;
		}

		DrawTextRow("アセット", context.selectedAssetPath.c_str());
		DrawTextRow("使い方", "ActionMap 名、Action 名、Path をここで編集して保存し、PlayerInput の Actions に割り当てます。");

		for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
			InputActionsAssetEntry& entry = entries[entryIndex];
			std::string headerText = entry.actionMapName + "/" + entry.actionName;
			if (headerText == "/") {
				headerText = "新しい Action";
			}

			ImGui::PushID(static_cast<int32_t>(entryIndex));
			const bool isOpen = DrawComponentHeader(headerText.c_str(), nullptr);
			if (ImGui::Button("この Action を削除")) {
				entries.erase(entries.begin() + static_cast<int64_t>(entryIndex));
				ImGui::PopID();
				break;
			}

			if (isOpen) {
				DrawStringInputRow("ActionMap", entry.actionMapName);
				DrawStringInputRow("Action", entry.actionName);

				const char* valueTypeItems[] = {"Button", "Vector2"};
				int32_t valueTypeIndex = entry.isVector2 ? 1 : 0;
				if (DrawComboRow("Value Type", valueTypeIndex, valueTypeItems, static_cast<int32_t>(_countof(valueTypeItems)))) {
					entry.isVector2 = valueTypeIndex == 1;
				}

				if (entry.isVector2) {
					DrawTextRow("Binding", "2DVector");
					DrawKeyNameComboRow("Up", entry.upKeyName);
					DrawKeyNameComboRow("Down", entry.downKeyName);
					DrawKeyNameComboRow("Left", entry.leftKeyName);
					DrawKeyNameComboRow("Right", entry.rightKeyName);
				}
				else {
					const char* bindingTypeItems[] = {"Key", "Mouse"};
					int32_t bindingTypeIndex = entry.usesMouse ? 1 : 0;
					if (DrawComboRow("Binding Type", bindingTypeIndex, bindingTypeItems, static_cast<int32_t>(_countof(bindingTypeItems)))) {
						entry.usesMouse = bindingTypeIndex == 1;
					}

					if (entry.usesMouse) {
						DrawStringComboRow("Path", entry.mouseButtonName, kMouseButtonNames, static_cast<int32_t>(_countof(kMouseButtonNames)));
					}
					else {
						DrawKeyNameComboRow("Path", entry.keyName);
					}
				}
			}

			ImGui::PopID();
		}

		if (ImGui::Button("Button Action 追加", ImVec2(-1.0f, 0.0f))) {
			InputActionsAssetEntry entry{};
			entry.actionMapName = "Player";
			entry.actionName = "Submit";
			entries.push_back(entry);
		}

		if (ImGui::Button("2D Vector Action 追加", ImVec2(-1.0f, 0.0f))) {
			InputActionsAssetEntry entry{};
			entry.actionMapName = "Player";
			entry.actionName = "Move";
			entry.isVector2 = true;
			entries.push_back(entry);
		}

		if (ImGui::Button("保存", ImVec2(-1.0f, 0.0f))) {
			if (SaveInputActionsEntries(context.selectedAssetPath, entries)) {
				statusMessage = "Input Actions を保存しました。";
			}
			else {
				statusMessage = "Input Actions の保存に失敗しました。";
			}
		}

		if (!statusMessage.empty()) {
			ImGui::TextWrapped("%s", statusMessage.c_str());
		}
	}

	void DrawSelectedAssetPreview(EditorInspectorPanelContext& context) {
		if (!ImGui::CollapsingHeader("選択アセット")) {
			return;
		}

		if (context.selectedAssetPath.empty()) {
			ImGui::TextDisabled("未選択");
			return;
		}

		ImGui::TextWrapped("%s", context.selectedAssetPath.c_str());
		if (EditorAssetUtility::HasExtension(context.selectedAssetPath, ".inputactions")) {
			DrawInputActionsAssetEditor(context);
			return;
		}

		int32_t textureIndex =
			EditorAssetUtility::GetTextureIndex(context.textureFilePaths, context.selectedAssetPath);

		if (context.textureSrvHandlesGPU != nullptr &&
			textureIndex >= 0 &&
			static_cast<size_t>(textureIndex) < context.textureCount) {
			ImGui::Image(
				ImTextureRef(context.textureSrvHandlesGPU[static_cast<size_t>(textureIndex)].ptr),
				ImVec2(160.0f, 160.0f));
		}
	}
}

void EditorInspectorPanel::Initialize() {
}

void EditorInspectorPanel::Update() {
}

void EditorInspectorPanel::Draw(EditorInspectorPanelContext& context) {
	// Inspector の初期位置。Docking 後はユーザーが自由に移動できる
	ImGui::SetNextWindowPos(
		ImVec2(context.editorSceneX + context.editorSceneWidth, context.editorMenuHeight),
		ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(
		ImVec2(context.editorRightWidth, context.editorWindowHeight - context.editorMenuHeight),
		ImGuiCond_FirstUseEver);
	ImGui::Begin("インスペクター###Inspector", nullptr, context.dockableWindowFlags);

	const char* selectedObjectLabel = GetSelectedObjectLabel(context);  // 旧プレビュー選択名
	Transforms* inspectedModelTransform = &context.modelTransform;  // 旧モデル Transform の編集先
	Transforms* inspectedSpriteTransform = &context.spriteTransform;  // 旧スプライト Transform の編集先

	if (context.selectedPlacedSceneObjectIndex >= 0 &&
		context.selectedPlacedSceneObjectIndex < static_cast<int32_t>(context.sceneObjects.size())) {
		EditorSceneObject& selectedPlacedSceneObject =
			context.sceneObjects[static_cast<size_t>(context.selectedPlacedSceneObjectIndex)];
		selectedObjectLabel = selectedPlacedSceneObject.name.c_str();

		if (selectedPlacedSceneObject.type == EditorSceneObjectType::Model) {
			inspectedModelTransform = &selectedPlacedSceneObject.transform;
		}
		else {
			inspectedSpriteTransform = &selectedPlacedSceneObject.transform;
		}
	}

	EditorGameObject* selectedEditorGameObject =
		context.editorScene.FindGameObject(context.selectedEditorGameObjectId);
	const bool isMultiSelectionActive = context.selectedEditorGameObjectIds.size() >= 2;

	if (selectedEditorGameObject != nullptr) {
		selectedObjectLabel = selectedEditorGameObject->name.c_str();

		if (context.previousSelectedEditorGameObjectId != context.selectedEditorGameObjectId) {
			strncpy_s(
				context.selectedGameObjectName,
				context.selectedGameObjectNameSize,
				selectedEditorGameObject->name.c_str(),
				_TRUNCATE);
			context.selectedGameObjectName[context.selectedGameObjectNameSize - 1] = '\0';
			context.previousSelectedEditorGameObjectId = context.selectedEditorGameObjectId;
		}
	}
	else if (!context.isLegacyPreviewVisible && context.selectedPlacedSceneObjectIndex < 0) {
		selectedObjectLabel = "なし";
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

	if (isMultiSelectionActive) {
		DrawMultiSelectionInspector(context);
	}
	else if (selectedEditorGameObject != nullptr) {
		DrawGameObjectHeader(context, *selectedEditorGameObject);
		DrawObjectOperationPanel(context, selectedEditorGameObject);

		if (selectedEditorGameObject == nullptr) {
			ImGui::PopStyleVar(2);
			ImGui::End();
			return;
		}

		DrawTransformComponent(context, *selectedEditorGameObject);

		int32_t removeComponentIndex = -1;  // ループ中に erase しないため、削除対象 index だけ保持する

		for (int32_t componentIndex = 0;
		     componentIndex < static_cast<int32_t>(selectedEditorGameObject->components.size());
		     componentIndex++) {
			EditorComponent& component =
				selectedEditorGameObject->components[static_cast<size_t>(componentIndex)];

			if (component.type == EditorComponentType::Transform) {
				continue;
			}

			ImGui::PushID(componentIndex);

			if (DrawComponentHeader(GetComponentDisplayName(component.type), &component.isActive)) {
				DrawComponentBody(context, *selectedEditorGameObject, component);

				if (ImGui::Button("コンポーネント削除")) {
					removeComponentIndex = componentIndex;
				}
			}

			ImGui::PopID();
		}


		if (removeComponentIndex >= 0) {
			context.editorScene.PushUndo();
			EditorComponentType removeType =
				selectedEditorGameObject->components[static_cast<size_t>(removeComponentIndex)].type;
			context.editorScene.RemoveComponent(selectedEditorGameObject->id, removeType);
			SyncSelection(context);
		}

		DrawAddComponentPopup(context, *selectedEditorGameObject);
	}
	else {
		ImGui::Text("選択: %s", selectedObjectLabel);

		if (context.isLegacyPreviewVisible) {
			DrawLegacyPreviewInspector(context, *inspectedModelTransform, *inspectedSpriteTransform);
		}

		DrawEnvironmentPanel(context);
		DrawPhysicsSettingsPanel(context);
		DrawMaterialPanel(context);
		DrawToolPanel(context);
		DrawCameraControlPanel(context);
		DrawSelectedAssetPreview(context);
	}

	ImGui::PopStyleVar(2);
	ImGui::End();
}

const char* EditorInspectorPanel::GetSelectedObjectLabel(const EditorInspectorPanelContext& context) const {
	// objectNames の範囲外を参照しないための防御
	if (context.objectNames == nullptr ||
		context.objectNameCount == 0 ||
		context.selectedSceneObject < 0 ||
		static_cast<size_t>(context.selectedSceneObject) >= context.objectNameCount) {
		return "なし";
	}

	return context.objectNames[context.selectedSceneObject];  // selectedSceneObject に対応した表示名
}

void EditorInspectorPanel::SyncSelection(EditorInspectorPanelContext& context) const {
	// GameObject 選択を SceneView の SceneObject 選択に合わせる
	context.selectionManager.SyncLegacySelection(
		context.selectedEditorGameObjectId,
		context.selectedSceneObject,
		context.selectedPlacedSceneObjectIndex);
}
