#include "EditorInspectorPanel.h"

#pragma warning(disable : 5045)
#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorNativeScriptAssetManager.h"
#include "EditorSharedState.h"
#include "Source/Engine/Animation/PropertyAnimationClip.h"
#include "Vector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
using namespace EditorSharedState;

namespace {
	constexpr float kPropertyLabelWidth = 118.0f;  // Unity 風に左側へ置く項目名の幅
	constexpr float kAxisLabelWidth = 16.0f;  // X / Y / Z の軸名だけを表示する幅
	constexpr float kWideButtonWidth = 230.0f;  // Inspector 下部の横長ボタン幅
	constexpr float kRadianToDegree = 57.2957795f;  // 内部のラジアン値を Inspector 表示用の度数へ変換する係数。
	constexpr float kDegreeToRadian = 0.0174532924f;  // Inspector で入力された度数を内部用ラジアンへ戻す係数。
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // テキスト系アセットを UTF-8 BOM 付きで保存する。
	int32_t g_pendingActionSequenceStepParentId = -1;  // Inspector 描画中の GameObject 配列再確保を避ける遅延作成要求。
	int32_t g_pendingWeaponSlotParentId = -1;  // 可変Weapon Slotを安全な次フレームに追加する。
	int32_t g_pendingActionRelayTargetParentId = -1;  // 可変Relay接続先を安全な次フレームに追加する。
	int32_t g_pendingRailSetupOwnerId = -1;  // Rail Path の一括作成を次フレームの安全な位置で処理する。

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
		{"描画・レンダリング", "Ocean", EditorComponentType::Ocean},
		{"物理", "Buoyancy", EditorComponentType::Buoyancy},
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
		{"3D物理", "Auto Convex Collision", EditorComponentType::AutoConvexCollision},
		{"3D物理", "地形の当たり判定", EditorComponentType::TerrainCollider},
		{"3D物理", "車輪の当たり判定", EditorComponentType::WheelCollider},
		{"3D物理", "キャラクターコントローラー", EditorComponentType::CharacterController},
		{"3D物理", "コンスタントフォース", EditorComponentType::ConstantForce},
		{"3D物理", "空気力学", EditorComponentType::Aerodynamics},
		{"3D物理", "風ゾーン", EditorComponentType::WindZone},
		{"3D物理", "重力場", EditorComponentType::GravityField},
		{"3D物理", "回転座標系", EditorComponentType::RotatingFrame},
		{"3D物理", "流体ボリューム", EditorComponentType::FluidVolume},
		{"3D物理", "ばね力", EditorComponentType::SpringForce},
		{"3D物理", "ロープ拘束", EditorComponentType::RopeConstraint},
		{"3D物理", "フックポイント", EditorComponentType::WireConnectable},
		{"描画・レンダリング", "ワイヤーレンダラー", EditorComponentType::WireRenderer},
		{"3D物理", "ねじりばね", EditorComponentType::TorsionSpring},
		{"3D物理", "推進力", EditorComponentType::Thruster},
		{"3D物理", "滑車拘束", EditorComponentType::PulleyConstraint},
		{"3D物理", "物理サーボ", EditorComponentType::PhysicsServo},
		{"3D物理", "渦流場", EditorComponentType::VortexField},
		{"3D物理", "圧力場", EditorComponentType::PressureField},
		{"3D物理", "サスペンション", EditorComponentType::Suspension},
		{"3D物理", "姿勢安定化", EditorComponentType::UprightStabilizer},
		{"3D物理", "電磁気ボディ", EditorComponentType::ElectromagneticBody},
		{"3D物理", "電磁場", EditorComponentType::ElectromagneticField},
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
		{"UI", "Scene ボタン", EditorComponentType::SceneButton},
		{"入力・イベント", "イベントシステム", EditorComponentType::EventSystem},
		{"入力・イベント", "スタンドアロン入力モジュール", EditorComponentType::StandaloneInputModule},
		{"入力・イベント", "Input System UI 入力モジュール", EditorComponentType::InputSystemUIInputModule},
		{"入力・イベント", "プレイヤー入力", EditorComponentType::PlayerInput},
		{"入力・イベント", "プレイヤー入力マネージャー", EditorComponentType::PlayerInputManager},
		{"入力・イベント", "タッチ入力モジュール", EditorComponentType::TouchInputModule},
		{"入力・イベント", "入力", EditorComponentType::Input},
		{"ゲームプレイ", "ローカル移動", EditorComponentType::LocalMove},
		{"ゲームプレイ", "ローリング移動", EditorComponentType::RollingMove},
		{"ゲームプレイ", "レール移動", EditorComponentType::RailMovement},
		{"ゲームプレイ", "レール速度プロファイル", EditorComponentType::RailSpeedProfile},
		{"ゲームプレイ", "レール区間", EditorComponentType::RailZone},
		{"ゲームプレイ", "レール分岐", EditorComponentType::RailBranch},
		{"ゲームプレイ", "体力", EditorComponentType::Health},
		{"ゲームプレイ", "ダメージ受信", EditorComponentType::DamageReceiver},
		{"ゲームプレイ", "レイ射撃", EditorComponentType::HitscanWeapon},
		{"ゲームプレイ", "弾発射", EditorComponentType::ProjectileEmitter},
		{"ゲームプレイ", "武器ロードアウト", EditorComponentType::WeaponLoadout},
		{"ゲームプレイ", "武器スロット", EditorComponentType::WeaponLoadoutSlot},
		{"ゲームプレイ", "ターゲット選択", EditorComponentType::TargetSelector},
		{"ゲームプレイ", "ターゲット追従", EditorComponentType::TargetSteering},
		{"ゲームプレイ", "ターゲットポイント", EditorComponentType::TargetPoint},
		{"ゲームプレイ", "チーム", EditorComponentType::Team},
		{"入力・イベント", "タイマー", EditorComponentType::Timer},
		{"ゲームプレイ", "汎用ステートマシン", EditorComponentType::GenericStateMachine},
		{"ゲームプレイ", "属性・リソース", EditorComponentType::Attribute},
		{"ゲームプレイ", "破壊可能部位", EditorComponentType::DestructiblePart},
		{"ゲームプレイ", "編隊追従", EditorComponentType::FormationFollower},
		{"ゲームプレイ", "ターゲットロック", EditorComponentType::TargetLock},
		{"ゲームプレイ", "複数ターゲットロック", EditorComponentType::MultiTargetLock},
		{"UI", "ワールドターゲットマーカー", EditorComponentType::WorldTargetMarker},
		{"UI", "画面外インジケーター", EditorComponentType::OffScreenIndicator},
		{"ゲームプレイ", "属性セット", EditorComponentType::AttributeSet},
		{"ゲームプレイ", "汎用カウンター", EditorComponentType::GenericCounter},
		{"ゲームプレイ", "汎用条件", EditorComponentType::GenericCondition},
		{"データ", "ゲームプレイデータ", EditorComponentType::GameplayData},
		{"ゲームプレイ", "範囲ダメージ", EditorComponentType::AreaDamage},
		{"ゲームプレイ", "ヒットゾーン", EditorComponentType::HitZone},
		{"ゲームプレイ", "ダメージタグ倍率", EditorComponentType::DamageTagModifier},
		{"ゲームプレイ", "弾起爆装置", EditorComponentType::ProjectileDetonator},
		{"ゲームプレイ", "脅威トラッカー", EditorComponentType::ThreatTracker},
		{"ゲームプレイ", "実行状態リセット", EditorComponentType::RuntimeStateReset},
		{"ゲームプレイ", "クールダウンセット", EditorComponentType::CooldownSet},
		{"ゲームプレイ", "武器発射パターン", EditorComponentType::WeaponFirePattern},
		{"ゲームプレイ", "ターゲット割り当て斉射", EditorComponentType::TargetAssignment},
		{"ゲームプレイ", "武器命中精度", EditorComponentType::WeaponAccuracy},
		{"ゲームプレイ", "武器反動", EditorComponentType::WeaponRecoil},
		{"ゲームプレイ", "命中応答", EditorComponentType::ImpactResponder},
		{"ゲームプレイ", "サーフェスタイプ", EditorComponentType::SurfaceType},
		{"ゲームプレイ", "時間倍率・ヒットストップ", EditorComponentType::TimeScale},
		{"照準", "照準補助", EditorComponentType::AimAssist},
		{"照準", "迎撃予測", EditorComponentType::InterceptPrediction},
		{"UI", "被弾方向表示", EditorComponentType::DamageDirectionIndicator},
		{"ゲームプレイ", "目標トラッカー", EditorComponentType::ObjectiveTracker},
		{"ゲームプレイ", "エンカウンター制御", EditorComponentType::EncounterController},
		{"ゲームプレイ", "生成地点セット", EditorComponentType::SpawnPointSet},
		{"ゲームプレイ", "難易度パラメーターセット", EditorComponentType::DifficultyParameterSet},
		{"カメラ", "カメラフィードバックミキサー", EditorComponentType::CameraFeedbackMixer},
		{"カメラ", "カメラ追従コンポーザー", EditorComponentType::CameraFollowComposer},
		{"カメラ", "速度フィードバック", EditorComponentType::SpeedFeedback},
		{"照準", "弾道予測", EditorComponentType::BallisticPrediction},
		{"UI", "複数被弾履歴", EditorComponentType::DamageEventBuffer},
		{"ゲームプレイ", "ゲーム一時停止", EditorComponentType::GamePause},
		{"海・水面", "水面航跡エミッター", EditorComponentType::SurfaceWakeEmitter},
		{"海・水面", "水面出入り状態", EditorComponentType::WaterSurfaceState},
		{"海・水面", "海面前方プローブ", EditorComponentType::OceanProbeSet},
		{"ゲームプレイ", "攻撃コリジョンフィルター", EditorComponentType::AttackCollisionFilter},
		{"照準", "砲塔照準", EditorComponentType::TurretAim},
		{"ゲームプレイ", "武器グループ", EditorComponentType::WeaponGroup},
		{"ゲームプレイ", "弾体貫通・跳弾", EditorComponentType::ProjectileImpactPhysics},
		{"カメラ", "水平線スタビライザー", EditorComponentType::CameraHorizonStabilizer},
		{"ゲームプレイ", "発射前射線チェック", EditorComponentType::FireLineCheck},
		{"ゲームプレイ", "状態効果セット", EditorComponentType::StatusEffectSet},
		{"描画・レンダリング", "軌道プレビュー", EditorComponentType::TrajectoryRenderer},
		{"ゲームプレイ", "移動補正", EditorComponentType::MovementModifier},
		{"ゲームプレイ", "オブジェクトプール", EditorComponentType::ObjectPool},
		{"ゲームプレイ", "プレハブ生成", EditorComponentType::PrefabSpawner},
		{"ゲームプレイ", "ウェーブ生成", EditorComponentType::WaveSpawner},
		{"ゲームプレイ", "生成オブジェクト設定", EditorComponentType::SpawnedObjectSetup},
		{"ゲームプレイ", "ウェーブ移動プロファイル", EditorComponentType::WaveMotionProfile},
		{"最適化", "距離アクティベーション", EditorComponentType::DistanceActivation},
		{"最適化", "Scene Streaming", EditorComponentType::SceneStreaming},
		{"最適化", "シミュレーション LOD", EditorComponentType::SimulationLOD},
		{"入力・イベント", "レールイベントマーカー", EditorComponentType::RailEventMarker},
		{"入力・イベント", "画面照準", EditorComponentType::ScreenAim},
		{"入力・イベント", "タイムラインイベント", EditorComponentType::TimelineEvent},
		{"入力・イベント", "しきい値状態", EditorComponentType::ThresholdState},
		{"入力・イベント", "アクションシーケンス", EditorComponentType::ActionSequence},
		{"入力・イベント", "シーケンスステップ", EditorComponentType::ActionSequenceStep},
		{"入力・イベント", "プロパティ補間", EditorComponentType::PropertyTween},
		{"入力・イベント", "アクション中継", EditorComponentType::ActionRelay},
		{"入力・イベント", "中継先", EditorComponentType::ActionRelayTarget},
		{"カメラ", "カメラブレンド", EditorComponentType::CameraBlend},
		{"カメラ", "カメラシェイク", EditorComponentType::CameraShake},
		{"UI", "値バインディング", EditorComponentType::UIValueBinding},
		{"ゲームプレイ", "保存対象", EditorComponentType::Saveable},
		{"ゲームプレイ", "チェックポイント", EditorComponentType::Checkpoint},
		{"ゲームプレイ", "自由移動/回転", EditorComponentType::FreeTransform},
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
		{"地形・タイルマップ", "フォリッジ", EditorComponentType::Foliage},
		{"地形・タイルマップ", "地形の当たり判定", EditorComponentType::TerrainCollider},
		{"地形・タイルマップ", "タイルマップ", EditorComponentType::Tilemap},
		{"地形・タイルマップ", "タイルマップレンダラー", EditorComponentType::TilemapRenderer},
		{"地形・タイルマップ", "タイルマップ当たり判定 2D", EditorComponentType::TilemapCollider2D},
		{"地形・タイルマップ", "グリッド", EditorComponentType::Grid},
		{"FeelKit", "FeelKit 触覚ソース", EditorComponentType::HapticSource},
		{"ライト・環境", "ポストプロセス", EditorComponentType::PostProcess},
		{"ライト・環境", "環境", EditorComponentType::Environment},
		{"ライト・環境", "Sun Portal", EditorComponentType::SunPortal},
		{"UI", "テキストエフェクト", EditorComponentType::TextEffect},
		{"エフェクト", "シーン遷移", EditorComponentType::SceneTransition},
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

	bool DrawVector2Row(const char* label, EditorScriptVector2& value, float speed, float minValue, float maxValue) {
		bool isChanged = false;
		ImGui::PushID(label);

		if (BeginPropertyTable("Vector2Row", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			const ImGuiStyle& style = ImGui::GetStyle();
			float inputWidth =
				(ImGui::GetContentRegionAvail().x - kAxisLabelWidth * 2.0f - style.ItemInnerSpacing.x * 3.0f) / 2.0f;
			inputWidth = (std::max)(inputWidth, 48.0f);

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
			ImGui::EndTable();
		}

		ImGui::PopID();
		return isChanged;
	}

	bool DrawVector2Row(const char* label, Vector2& value, float speed, float minValue, float maxValue) {
		bool isChanged = false;
		ImGui::PushID(label);

		if (BeginPropertyTable("MaterialVector2Row", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			const ImGuiStyle& style = ImGui::GetStyle();
			float inputWidth =
				(ImGui::GetContentRegionAvail().x - kAxisLabelWidth * 2.0f - style.ItemInnerSpacing.x * 3.0f) / 2.0f;
			inputWidth = (std::max)(inputWidth, 48.0f);

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

	bool DrawScientificFloatRow(const char* label, float& value, float speed, float minValue, float maxValue) {
		bool isChanged = false;  // 小さい物理定数を指数表記のまま編集できるようにする
		ImGui::PushID(label);

		if (BeginPropertyTable("ScientificFloatRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			isChanged = ImGui::DragFloat("##Value", &value, speed, minValue, maxValue, "%.6e");
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

	// Play中に物理側が書き込むRuntime診断値を、編集不可のまま数値で見せる。
	void DrawReadOnlyFloatRow(const char* label, float value) {
		char valueText[64];
		std::snprintf(valueText, sizeof(valueText), "%.3f", static_cast<double>(value));
		DrawReadOnlyFieldRow(label, valueText);
	}

	void DrawReadOnlyVector3Row(const char* label, const Vector3& value) {
		char valueText[128];
		std::snprintf(
			valueText,
			sizeof(valueText),
			"X %.2f   Y %.2f   Z %.2f   |v| %.2f",
			static_cast<double>(value.x),
			static_cast<double>(value.y),
			static_cast<double>(value.z),
			static_cast<double>(std::sqrt(
				value.x * value.x + value.y * value.y + value.z * value.z)));
		DrawReadOnlyFieldRow(label, valueText);
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
			    2,
			    ImGuiTableFlags_SizingStretchProp |
			    ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("項目名", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("タグ");

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::Combo("##Tag", &selectedTagIndex, tagItems, _countof(tagItems));

			ImGui::TableNextRow();
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
		DrawStringInputRow("描画アセット", component.assetPath);
		DrawStringInputRow("テクスチャ", component.textureAssetPath);
		DrawSubHeader("マテリアル");
		DrawReadOnlyFieldRow("要素 0", materialName);
		DrawColor3Row("色", component.color);
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("透明度", component.alpha, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("粗さ", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("金属度", component.metallic, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("反射", component.reflectionStrength, 0.01f, 0.0f, 4.0f);
		DrawColor3Row("発光色", component.emissionColor);
		DrawFloatRow("発光", component.emissionStrength, 0.01f, 0.0f, 100.0f);
		DrawCheckboxRow("両面", component.doubleSided);
	}

	std::string GetRenderableModelAssetPath(const EditorGameObject& gameObject) {
		const EditorComponent* modelRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
		if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
			return modelRenderer->assetPath;
		}

		const EditorComponent* skinnedMeshRenderer =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SkinnedMeshRenderer);
		if (skinnedMeshRenderer != nullptr && !skinnedMeshRenderer->assetPath.empty()) {
			return skinnedMeshRenderer->assetPath;
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

	const ModelData* GetModelDataForComponent(
		const EditorGameObject& gameObject,
		const EditorComponent& component,
		bool includeAnimation,
		std::string& assetPath) {
		assetPath = GetModelAssetPathForComponent(gameObject, component);
		if (assetPath.empty()) {
			return nullptr;
		}

		return EditorAssetUtility::GetSharedModelAssetData(assetPath, includeAnimation);
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

	bool DrawMaterialTexturePicker(
		const EditorInspectorPanelContext& context,
		const char* label,
		std::string& textureAssetPath) {
		bool isChanged = DrawStringInputRow(label, textureAssetPath);
		const bool isImageSelected =
			!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".png") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpg") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".jpeg") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".dds") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".tga") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".hdr"));

		ImGui::PushID(label);
		if (isImageSelected && ImGui::Button("選択中画像を設定", ImVec2(-1.0f, 0.0f))) {
			textureAssetPath = context.selectedAssetPath;
			isChanged = true;
		}
		if (!textureAssetPath.empty() && ImGui::Button("画像を解除", ImVec2(-1.0f, 0.0f))) {
			textureAssetPath.clear();
			isChanged = true;
		}
		ImGui::PopID();
		return isChanged;
	}

	std::string WideToUtf8Editor(const std::wstring& wideText) {
		if (wideText.empty()) {
			return "";
		}
		const int32_t sizeNeeded = WideCharToMultiByte(
			CP_UTF8, 0, wideText.c_str(), static_cast<int32_t>(wideText.size()), nullptr, 0, nullptr, nullptr);
		std::string utf8Text(static_cast<size_t>(sizeNeeded > 0 ? sizeNeeded : 0), '\0');
		WideCharToMultiByte(
			CP_UTF8, 0, wideText.c_str(), static_cast<int32_t>(wideText.size()), &utf8Text[0], sizeNeeded, nullptr, nullptr);
		return utf8Text;
	}

	std::string MakeProjectRelativeModelPath(const std::string& pickedPathText) {
		const std::filesystem::path pickedPath(pickedPathText);
		std::error_code fileError;
		const std::filesystem::path projectRoot = std::filesystem::current_path(fileError);
		if (!fileError && !projectRoot.empty()) {
			const std::filesystem::path relativePath = std::filesystem::relative(pickedPath, projectRoot, fileError);
			if (!fileError && !relativePath.empty()) {
				const std::string relativeText = relativePath.generic_string();
				if (relativeText.rfind("..", 0) != 0) {
					return relativeText;  // プロジェクト内のモデルは相対パスでシーンに保存して移植性を保つ
				}
			}
		}

		return pickedPath.generic_string();  // プロジェクト外のファイルは絶対パスのまま使う
	}

	bool DrawModelAssetPicker(const EditorInspectorPanelContext& context, std::string& assetPath) {
		bool isChanged = DrawStringInputRow("描画アセット", assetPath);
		const bool isModelSelected =
			!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".fbx") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".obj"));

		ImGui::PushID("ModelAssetPicker");
		if (isModelSelected && ImGui::Button("選択中モデルを設定", ImVec2(-1.0f, 0.0f))) {
			assetPath = context.selectedAssetPath;  // Project パネルで選択中の .fbx / .obj を割り当てる
			isChanged = true;
		}
		if (ImGui::Button("ファイルから選択…", ImVec2(-1.0f, 0.0f))) {
			wchar_t fileBuffer[MAX_PATH] = {};
			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.lpstrFilter = L"モデルファイル (*.fbx;*.obj)\0*.fbx;*.obj\0すべてのファイル (*.*)\0*.*\0";
			ofn.lpstrFile = fileBuffer;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrDefExt = L"fbx";
			ofn.lpstrInitialDir = L"Assets";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

			if (GetOpenFileNameW(&ofn) != 0) {
				assetPath = MakeProjectRelativeModelPath(WideToUtf8Editor(fileBuffer));
				isChanged = true;
			}
		}
		if (!assetPath.empty() && ImGui::Button("モデルを解除", ImVec2(-1.0f, 0.0f))) {
			assetPath.clear();
			isChanged = true;
		}
		ImGui::PopID();
		return isChanged;
	}

	bool DrawScriptActionRow(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		int32_t targetGameObjectId,
		const char* label,
		std::string& actionName) {
		bool isChanged = DrawStringInputRow(label, actionName);
		const int32_t resolvedTargetGameObjectId = targetGameObjectId >= 0
			? targetGameObjectId
			: ownerGameObject.id;
		const std::vector<std::string> actionNames =
			context.runtimeManager.GetScriptManager().GetRegisteredActionNames(resolvedTargetGameObjectId);

		if (actionNames.empty()) {
			return isChanged;
		}

		const std::string candidateLabel = std::string(label) + " 候補";
		ImGui::PushID(candidateLabel.c_str());

		if (BeginPropertyTable("ScriptActionCandidateRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(candidateLabel.c_str());
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);

			if (ImGui::BeginCombo("##Value", actionName.empty() ? "登録Actionを選択" : actionName.c_str())) {
				for (const std::string& candidateActionName : actionNames) {
					const bool isSelected = actionName == candidateActionName;

					if (ImGui::Selectable(candidateActionName.c_str(), isSelected)) {
						actionName = candidateActionName;
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

	void DrawRendererComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& gameObject,
		EditorComponent& component,
		const char* fallbackMaterialName) {
		std::string modelAssetPath;
		const ModelData* loadedModelData = GetModelDataForComponent(gameObject, component, false, modelAssetPath);
		const ModelData emptyModelData{};
		const ModelData& modelData = loadedModelData != nullptr ? *loadedModelData : emptyModelData;
		const bool hasModelData = loadedModelData != nullptr;
		const std::string materialName =
			hasModelData && !modelData.material.name.empty() ? modelData.material.name : fallbackMaterialName;
		const std::string texturePath =
			hasModelData && !modelData.material.textureFilePath.empty() ? modelData.material.textureFilePath : "なし";
		std::string effectiveBaseColorTexturePath = component.textureAssetPath;

		if (effectiveBaseColorTexturePath.empty() && component.useImportedMaterialTextures &&
			hasModelData && !modelData.material.textureFilePath.empty()) {
			effectiveBaseColorTexturePath = modelData.material.textureFilePath;
		}

		DrawModelAssetPicker(context, component.assetPath);
		DrawSubHeader("マテリアル");
		DrawReadOnlyFieldRow("要素 0", materialName.c_str());
		DrawTextRow("FBX内テクスチャ", texturePath.c_str());
		DrawTextRow(
			"現在の描画画像",
			effectiveBaseColorTexturePath.empty() ? "未設定（ベースカラーのみ）" : effectiveBaseColorTexturePath.c_str());
		int32_t materialCount = static_cast<int32_t>(modelData.materials.size());
		DrawIntRow("マテリアル数", materialCount);

		//============================================================
		// 基本サーフェス
		//============================================================

		const char* alphaModeItems[] = {"不透明", "アルファマスク", "半透明"};
		const char* lightingModeItems[] = {"Lightingなし", "Lambert", "Half Lambert", "PBR"};
		component.alphaMode = (std::clamp)(component.alphaMode, 0, 2);
		component.lightingMode = (std::clamp)(component.lightingMode, 0, 3);
		DrawComboRow("描画方式", component.alphaMode, alphaModeItems, static_cast<int32_t>(_countof(alphaModeItems)));
		DrawComboRow(
			"Lighting方式",
			component.lightingMode,
			lightingModeItems,
			static_cast<int32_t>(_countof(lightingModeItems)));
		DrawCheckboxRow("両面描画", component.doubleSided);
		DrawColor3Row("ベースカラー", component.color);
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("メタリック", component.metallic, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("粗さ", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("屈折率", component.ior, 0.01f, 1.0f, 3.0f);
		DrawFloatRow("アルファ", component.alpha, 0.01f, 0.0f, 1.0f);
		if (component.alphaMode == 1) {
			DrawFloatRow("アルファ境界", component.alphaCutoff, 0.01f, 0.0f, 1.0f);
		}
		DrawFloatRow("反射", component.reflectionStrength, 0.01f, 0.0f, 1.0f);
		DrawColor3Row("放射色", component.emissionColor);
		DrawFloatRow("放射", component.emissionStrength, 0.01f, 0.0f, 50.0f);

		bool isTexturePathChanged = false;
		if (ImGui::TreeNodeEx("PBR テクスチャ", ImGuiTreeNodeFlags_DefaultOpen)) {
			isTexturePathChanged |= DrawCheckboxRow("FBX内画像を自動使用", component.useImportedMaterialTextures);
			DrawTextRow(
				"画像の使い分け",
				"ベースカラー画像はモデルへ描画します。UV確認画像はInspectorのプレビューだけに使い、モデルへは描画しません。");
			isTexturePathChanged |= DrawMaterialTexturePicker(
				context,
				"ベースカラー画像（描画用）",
				component.textureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(
				context,
				"UV確認画像（プレビュー専用）",
				component.uvLayoutTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "法線", component.normalTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "メタリック", component.metallicTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "粗さ", component.roughnessTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "アンビエントオクルージョン", component.ambientOcclusionTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "放射", component.emissionTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "高さ", component.heightTextureAssetPath);
			isTexturePathChanged |= DrawMaterialTexturePicker(context, "不透明度", component.opacityTextureAssetPath);
			DrawVector2Row("UV繰り返し", component.uvTiling, 0.01f, -100.0f, 100.0f);
			DrawVector2Row("UVオフセット", component.uvOffset, 0.01f, -100.0f, 100.0f);
			DrawFloatRow("法線強度", component.normalScale, 0.01f, -2.0f, 2.0f);
			DrawFloatRow("AO強度", component.ambientOcclusionStrength, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("高さ強度", component.heightScale, 0.001f, -0.2f, 0.2f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("高度なPBR", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawFloatRow("クリアコート", component.clearCoat, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("コート粗さ", component.clearCoatRoughness, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("透過", component.transmission, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("表面下散乱", component.subsurface, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("材質の厚み", component.materialThickness, 0.001f, 0.001f, 10.0f);
			DrawFloatRow("異方性", component.anisotropy, 0.01f, -1.0f, 1.0f);
			DrawFloatRow("異方性回転", component.anisotropyRotation, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("濡れ", component.materialWetness, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("水際の高さ", component.materialWaterlineHeight, 0.01f, -10000.0f, 10000.0f);
			DrawFloatRow("水際の幅", component.materialWaterlineWidth, 0.01f, 0.001f, 1000.0f);
			DrawFloatRow("鏡面色", component.specularTint, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("シーン", component.sheen, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("シーン色", component.sheenTint, 0.01f, 0.0f, 1.0f);
			ImGui::TreePop();
		}

		if (isTexturePathChanged) {
			context.sceneSynchronizer.Update(context.textureFilePaths, context.selectedPlacedSceneObjectIndex);
		}
		if (!effectiveBaseColorTexturePath.empty()) {
			DrawTexturePreviewByPath(context, "描画用ベースカラー画像プレビュー", effectiveBaseColorTexturePath);
		}

		if (!component.uvLayoutTextureAssetPath.empty()) {
			DrawTexturePreviewByPath(context, "UV確認画像プレビュー（描画には未使用）", component.uvLayoutTextureAssetPath);
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

			if (ImGui::TreeNodeEx("FBX PBR テクスチャ", ImGuiTreeNodeFlags_DefaultOpen)) {
				DrawTextRow("ベースカラー", modelData.material.textureFilePath.empty() ? "なし" : modelData.material.textureFilePath.c_str());
				DrawTextRow("法線", modelData.material.normalTextureFilePath.empty() ? "なし" : modelData.material.normalTextureFilePath.c_str());
				DrawTextRow("メタリック", modelData.material.metallicTextureFilePath.empty() ? "なし" : modelData.material.metallicTextureFilePath.c_str());
				DrawTextRow("粗さ", modelData.material.roughnessTextureFilePath.empty() ? "なし" : modelData.material.roughnessTextureFilePath.c_str());
				DrawTextRow("AO", modelData.material.ambientOcclusionTextureFilePath.empty() ? "なし" : modelData.material.ambientOcclusionTextureFilePath.c_str());
				DrawTextRow("放射", modelData.material.emissionTextureFilePath.empty() ? "なし" : modelData.material.emissionTextureFilePath.c_str());
				DrawTextRow("高さ", modelData.material.heightTextureFilePath.empty() ? "なし" : modelData.material.heightTextureFilePath.c_str());
				DrawTextRow("不透明度", modelData.material.opacityTextureFilePath.empty() ? "なし" : modelData.material.opacityTextureFilePath.c_str());
				ImGui::TreePop();
			}
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
			DrawTextRow("注", "Sun は既定でGameObjectの回転から方向を作ります。位置は使いません。");

			DrawSubHeader("太陽システム");
			DrawCheckboxRow("方位角/高度を使用", component.sunUseAzimuthElevation);
			DrawTextRow("説明", "ONの間、下の方位角・高度からsunDirectionを作り、Transform回転より優先します。");
			DrawFloatRow("太陽方位角", component.sunAzimuthDegrees, 1.0f, -360.0f, 360.0f);
			DrawFloatRow("太陽高度", component.sunElevationDegrees, 0.5f, -10.0f, 90.0f);

			DrawCheckboxRow("色温度を使用", component.sunUseColorTemperature);
			DrawTextRow("説明2", "ONの間、Kelvinから色を作り、上の色フィールドより優先します。");
			DrawCheckboxRow("色温度を高度から自動推定", component.sunAutoTemperatureFromElevation);
			DrawFloatRow("太陽色温度(K)", component.sunTemperatureKelvin, 25.0f, 1000.0f, 12000.0f);
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

		DrawCheckboxRow("Colliderから質量を計算", component.automaticMassFromCollider);

		if (component.automaticMassFromCollider) {
			DrawFloatRow("実質密度 kg/m3", component.bodyDensity, 1.0f, 0.01f, 1000000.0f);
			DrawTextRow("質量", "Play開始時にCollider体積 x 実質密度で計算");
		}
		else {
			DrawFloatRow("質量", component.mass, 0.01f, 0.01f, 1000000.0f);
		}

		DrawFloatRow("線形減衰", component.drag, 0.01f, 0.0f, 20.0f);
		DrawFloatRow("角度減衰", component.angularDrag, 0.01f, 0.0f, 20.0f);
		DrawFloatRow("慣性倍率", component.inertiaMultiplier, 0.01f, 0.01f, 1000.0f);
		DrawVector3Row("重心オフセット", component.centerOfMassOffset, 0.01f, -10000.0f, 10000.0f);
		DrawCheckboxRow("ジャイロ効果", component.applyGyroscopicForce);
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
			const char* valueTypeItems[] = {"Button", "Vector2"};
			component.inputBehavior = (std::clamp)(component.inputBehavior, 0, static_cast<int32_t>(_countof(behaviorItems)) - 1);

			DrawTextRow("説明", "Input Actions の ActionMap / Action を、同じ GameObject の C++ 関数へ接続します。");
			DrawTextRow("Actions", component.assetPath.empty() ? "未設定" : component.assetPath.c_str());
			DrawStringInputRow("Actions パス", component.assetPath);
			DrawStringInputRow("Default Map", component.inputActionMapName);
			DrawComboRow("Behavior", component.inputBehavior, behaviorItems, static_cast<int32_t>(_countof(behaviorItems)));
			DrawSubHeader("Events");

			int32_t removeEventIndex = -1;
			for (size_t eventIndex = 0U; eventIndex < component.inputEventBindings.size(); eventIndex++) {
				EditorInputEventBinding& eventBinding = component.inputEventBindings[eventIndex];
				ImGui::PushID(static_cast<int32_t>(eventIndex));
				ImGui::SeparatorText(("イベント " + std::to_string(eventIndex + 1U)).c_str());
				DrawStringInputRow("Action Map", eventBinding.actionMapName);
				DrawStringInputRow("Action", eventBinding.actionName);
				DrawStringInputRow("C++ 関数", eventBinding.functionName);
				eventBinding.valueType = (std::clamp)(eventBinding.valueType, 0, 1);
				DrawComboRow("値の型", eventBinding.valueType, valueTypeItems, static_cast<int32_t>(_countof(valueTypeItems)));

				if (ImGui::Button("このイベントを削除", ImVec2(-1.0f, 0.0f))) {
					removeEventIndex = static_cast<int32_t>(eventIndex);
				}

				ImGui::PopID();
			}

			if (removeEventIndex >= 0) {
				component.inputEventBindings.erase(component.inputEventBindings.begin() + removeEventIndex);
			}

			if (ImGui::Button("イベントを追加", ImVec2(-1.0f, 0.0f))) {
				component.inputEventBindings.push_back({component.inputActionMapName, "NewAction", "OnNewAction", 0});
			}

			if (!component.assetPath.empty()) {
				if (ImGui::Button("Actions から不足イベントを追加", ImVec2(-1.0f, 0.0f))) {
					const std::vector<InputActionsAssetEntry> actionEntries = LoadInputActionsEntries(component.assetPath);

					for (const InputActionsAssetEntry& actionEntry : actionEntries) {
						const auto eventIt = std::find_if(
							component.inputEventBindings.begin(),
							component.inputEventBindings.end(),
							[&actionEntry](const EditorInputEventBinding& eventBinding) {
								return eventBinding.actionMapName == actionEntry.actionMapName &&
									eventBinding.actionName == actionEntry.actionName;
							});

						if (eventIt == component.inputEventBindings.end()) {
							component.inputEventBindings.push_back({
								actionEntry.actionMapName,
								actionEntry.actionName,
								"On" + actionEntry.actionName,
								actionEntry.isVector2 ? 1 : 0});
						}
					}
				}
			}

			for (const EditorInputEventBinding& eventBinding : component.inputEventBindings) {
				if (eventBinding.actionName == "Move") {
					component.inputMoveEventName = eventBinding.functionName;
				}
				else if (eventBinding.actionName == "Jump") {
					component.inputJumpEventName = eventBinding.functionName;
				}
				else if (eventBinding.actionName == "Fire") {
					component.inputFireEventName = eventBinding.functionName;
				}
			}

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

			DrawTextRow("C++ 側", "BindAction(\"OnMove\", ...) の登録名と、上の C++ 関数名を一致させます。");
		}

	void DrawHapticSourceComponent(EditorComponent& component) {
		DrawTextRow("説明", "FeelKitHaptics の触覚効果を再生するコンポーネントです。");
		DrawStringInputRow("サウンド", component.assetPath);
		DrawCheckboxRow("自動再生", component.audioPlayOnAwake);
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
		static int32_t selectedScriptTemplate = 0;  // 用途別Template。生成後も選択を維持する。
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
					const std::string nextDllPath = dllPathBuffer;

					if (component.assetPath != nextDllPath) {
						component.assetPath = nextDllPath;  // DLL を差し替えた時は、新しい型情報を読み直す。
						component.scriptProperties.clear();
					}
				}
				ImGui::EndTable();
			}

			ImGui::PopID();
		}

		if (!context.selectedAssetPath.empty() && context.selectedAssetPath.find(".dll") != std::string::npos) {
			if (ImGui::Button("選択中 DLL を設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;  // Project で選んだ DLL をそのまま Script に割り当てる。
				component.scriptProperties.clear();
			}
		}

		EditorScriptManager& scriptManager = context.runtimeManager.GetScriptManager();
		if (!component.assetPath.empty()) {
			scriptManager.RefreshExposedFields(component);
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

		DrawSubHeader("公開変数");
		if (component.scriptProperties.empty()) {
			DrawTextRow("状態", "公開変数なし。DLLをビルドすると Expose... で登録した変数が表示されます。");
		}

		for (size_t propertyIndex = 0U; propertyIndex < component.scriptProperties.size(); propertyIndex++) {
			EditorScriptProperty& scriptProperty = component.scriptProperties[propertyIndex];
			const char* propertyLabel = scriptProperty.displayName.empty()
				? scriptProperty.name.c_str()
				: scriptProperty.displayName.c_str();
			const float minValue = scriptProperty.hasRange ? scriptProperty.minValue : 0.0f;
			const float maxValue = scriptProperty.hasRange ? scriptProperty.maxValue : 0.0f;
			const float step = (std::max)(scriptProperty.step, 0.001f);
			ImGui::PushID(static_cast<int32_t>(propertyIndex));

			switch (scriptProperty.type) {
			case EditorScriptFieldTypeBool:
				DrawCheckboxRow(propertyLabel, scriptProperty.boolValue);
				break;
			case EditorScriptFieldTypeInt32:
				DrawIntRow(propertyLabel, scriptProperty.intValue);

				if (scriptProperty.hasRange) {
					scriptProperty.intValue = (std::clamp)(
						scriptProperty.intValue,
						static_cast<int32_t>(scriptProperty.minValue),
						static_cast<int32_t>(scriptProperty.maxValue));
				}
				break;
			case EditorScriptFieldTypeFloat:
				DrawFloatRow(propertyLabel, scriptProperty.floatValue, step, minValue, maxValue);
				break;
			case EditorScriptFieldTypeVector2:
				DrawVector2Row(propertyLabel, scriptProperty.vector2Value, step, minValue, maxValue);
				break;
			case EditorScriptFieldTypeVector3:
				DrawVector3Row(propertyLabel, scriptProperty.vector3Value, step, minValue, maxValue);
				break;
			case EditorScriptFieldTypeString:
				DrawStringInputRow(propertyLabel, scriptProperty.stringValue);
				break;
			case EditorScriptFieldTypeGameObject:
				DrawGameObjectReferenceRow(
					context,
					gameObject,
					propertyLabel,
					scriptProperty.intValue,
					"未設定",
					true);
				break;
			case EditorScriptFieldTypeSceneAsset:
				DrawStringInputRow(propertyLabel, scriptProperty.stringValue);

				if (!context.selectedAssetPath.empty() &&
					EditorAssetUtility::HasExtension(context.selectedAssetPath, ".scene") &&
					ImGui::Button("選択中 Scene を設定", ImVec2(-1.0f, 0.0f))) {
					scriptProperty.stringValue = context.selectedAssetPath;
				}
				break;
			default:
				DrawTextRow(propertyLabel, "未対応の型");
				break;
			}

			ImGui::PopID();
		}

		if (!component.assetPath.empty()) {
			if (ImGui::Button("DLL から公開変数を読み込む", ImVec2(-1.0f, 0.0f))) {
				scriptManager.RefreshExposedFields(component);
			}
		}

		DrawSubHeader("C++ スクリプト生成");
		ImGui::InputText("クラス名", requestedScriptName, sizeof(requestedScriptName));
		const int32_t scriptTemplateCount = EditorNativeScriptAssetManager::GetTemplateCount();
		selectedScriptTemplate = (std::clamp)(
			selectedScriptTemplate,
			0,
			scriptTemplateCount - 1);
		const EditorNativeScriptTemplateInfo& selectedTemplateInfo =
			EditorNativeScriptAssetManager::GetTemplateInfo(selectedScriptTemplate);
		const std::string selectedTemplateLabel =
			std::string(selectedTemplateInfo.category) + " / " + selectedTemplateInfo.displayName;

		if (ImGui::BeginCombo("処理テンプレート", selectedTemplateLabel.c_str())) {
			for (int32_t templateIndex = 0; templateIndex < scriptTemplateCount; templateIndex++) {
				const EditorNativeScriptTemplateInfo& templateInfo =
					EditorNativeScriptAssetManager::GetTemplateInfo(templateIndex);
				const std::string templateLabel =
					std::string(templateInfo.category) + " / " + templateInfo.displayName;
				const bool isSelected = templateIndex == selectedScriptTemplate;

				if (ImGui::Selectable(templateLabel.c_str(), isSelected)) {
					selectedScriptTemplate = templateIndex;
				}

				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		const EditorNativeScriptTemplateInfo& currentTemplateInfo =
			EditorNativeScriptAssetManager::GetTemplateInfo(selectedScriptTemplate);
		DrawTextRow("生成内容", currentTemplateInfo.description);
		DrawTextRow("推奨Component", currentTemplateInfo.recommendedComponents);
		DrawTextRow("方針", "既存Componentを組み合わせる開始コードです。ゲーム固有ルールは生成後のC++へ書きます。");

		if (ImGui::Button("C++ スクリプトを作成", ImVec2(-1.0f, 0.0f))) {
			const EditorNativeScriptAssetResult result =
				EditorNativeScriptAssetManager::CreateNativeScriptAsset(
					requestedScriptName,
					kIsDebugEditorBuild,
					currentTemplateInfo.type);
			generationMessage = result.message;

			if (result.isSucceeded) {
				component.assetPath = result.dllFilePath;  // 生成直後から Script Component は想定 DLL を参照する。
				component.scriptProperties.clear();
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

	void DrawAerodynamicsComponent(EditorComponent& component) {
		DrawTextRow("説明", "相対風速から二次抗力、揚力、横力、Magnus 力、回転抗力を計算します。");
		DrawTextRow("ローカル軸", "+Z=前、+Y=上、+X=右");
		DrawSubHeader("流体と抗力");
		DrawFloatRow("空気密度 kg/m3", component.aerodynamicAirDensity, 0.001f, 0.0f, 1000.0f);
		DrawFloatRow("抗力係数 Cd", component.aerodynamicDragCoefficient, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("代表面積 m2", component.aerodynamicReferenceArea, 0.01f, 0.0001f, 100000.0f);
		DrawVector3Row("基礎風速 m/s", component.aerodynamicAmbientWindVelocity, 0.05f, -10000.0f, 10000.0f);

		DrawSubHeader("揚力と失速");
		DrawFloatRow("基礎揚力係数", component.aerodynamicBaseLiftCoefficient, 0.01f, -10.0f, 10.0f);
		DrawFloatRow("揚力傾斜 /rad", component.aerodynamicLiftSlope, 0.01f, -20.0f, 20.0f);
		DrawFloatRow("翼面積 m2", component.aerodynamicLiftArea, 0.01f, 0.0f, 100000.0f);
		DrawFloatRow("ゼロ揚力迎角 deg", component.aerodynamicZeroLiftAngleDegrees, 0.1f, -89.0f, 89.0f);
		DrawFloatRow("失速迎角 deg", component.aerodynamicStallAngleDegrees, 0.1f, 1.0f, 89.0f);

		DrawSubHeader("横力と回転");
		DrawFloatRow("横力係数", component.aerodynamicSideForceCoefficient, 0.01f, 0.0f, 20.0f);
		DrawFloatRow("側面積 m2", component.aerodynamicSideArea, 0.01f, 0.0f, 100000.0f);
		DrawFloatRow("回転抗力係数", component.aerodynamicAngularDragCoefficient, 0.01f, 0.0f, 20.0f);
		DrawFloatRow("Magnus 係数", component.aerodynamicMagnusCoefficient, 0.01f, -20.0f, 20.0f);
		DrawVector3Row("圧力中心", component.aerodynamicCenterOfPressure, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("合力上限 N", component.aerodynamicMaximumForce, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawWindZoneComponent(EditorComponent& component) {
		static const char* windModes[] = {"方向風", "放射風"};
		DrawTextRow("説明", "Aerodynamics を持つ Rigidbody へ加算される Scene 風速場です。");
		DrawComboRow("種類", component.windZoneMode, windModes, static_cast<int32_t>(_countof(windModes)));

		if (component.windZoneMode == 0) {
			DrawVector3Row("風向", component.windZoneDirection, 0.01f, -1.0f, 1.0f);
		}

		DrawFloatRow("風速 m/s", component.windZoneSpeed, 0.1f, -10000.0f, 10000.0f);
		DrawFloatRow("影響半径 m", component.windZoneRadius, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("乱流速度 m/s", component.windZoneTurbulenceStrength, 0.05f, 0.0f, 10000.0f);
		DrawFloatRow("乱流周波数", component.windZoneTurbulenceFrequency, 0.05f, 0.0f, 1000.0f);
	}

	void DrawGravityFieldComponent(EditorComponent& component) {
		static const char* gravityModes[] = {"Newton 逆二乗", "定加速度"};
		DrawTextRow("説明", "この GameObject の位置へ Dynamic Rigidbody を引く点重力です。");
		DrawComboRow("種類", component.gravityFieldMode, gravityModes, static_cast<int32_t>(_countof(gravityModes)));

		if (component.gravityFieldMode == 0) {
			DrawScientificFloatRow(
				"万有引力定数 G",
				component.gravityFieldGravitationalConstant,
				1.0e-12f,
				0.0f,
				1.0f);
			DrawFloatRow("引力源質量 kg", component.gravityFieldSourceMass, 1000000.0f, 0.0f, 1.0e20f);
		}
		else {
			DrawFloatRow("加速度 m/s2", component.gravityFieldAcceleration, 0.01f, -1000000.0f, 1000000.0f);
		}

		DrawFloatRow("最小計算距離 m", component.gravityFieldMinimumDistance, 0.01f, 0.001f, 1000000.0f);
		DrawFloatRow("影響半径 m", component.gravityFieldInfluenceRadius, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("加速度上限 m/s2", component.gravityFieldMaximumAcceleration, 0.1f, 0.0f, 1000000000.0f);
	}

	void DrawRotatingFrameComponent(EditorComponent& component) {
		DrawTextRow("説明", "回転中心の周囲へ遠心力、Coriolis 力、Euler 力を加えます。");
		DrawTextRow("計算", "World座標の角速度・角加速度を使用");
		DrawVector3Row("角速度 rad/s", component.rotatingFrameAngularVelocity, 0.01f, -10000.0f, 10000.0f);
		DrawVector3Row("角加速度 rad/s2", component.rotatingFrameAngularAcceleration, 0.01f, -10000.0f, 10000.0f);
		DrawVector3Row("中心の速度 m/s", component.rotatingFrameLinearVelocity, 0.05f, -10000.0f, 10000.0f);
		DrawFloatRow("影響半径 m", component.rotatingFrameRadius, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("加速度上限 m/s2", component.rotatingFrameMaximumAcceleration, 0.1f, 0.0f, 1000000000.0f);
	}

	void DrawFluidVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "有限の3D箱領域へArchimedes浮力、Stokes抵抗、二次抗力を作ります。");
		DrawTextRow("領域", "GameObjectの位置・回転・Scaleを反映");
		DrawSubHeader("流体領域");
		DrawVector3Row("サイズ m", component.fluidVolumeSize, 0.1f, 0.01f, 1000000.0f);
		DrawFloatRow("密度 kg/m3", component.fluidDensity, 0.1f, 0.0f, 1000000.0f);
		DrawVector3Row("流速 m/s", component.fluidFlowVelocity, 0.05f, -100000.0f, 100000.0f);

		DrawSubHeader("抵抗と安定化");
		DrawFloatRow("粘性 Pa*s", component.fluidDynamicViscosity, 0.001f, 0.0f, 1000000.0f);
		DrawFloatRow("二次抗力係数", component.fluidDragCoefficient, 0.01f, 0.0f, 100.0f);
		DrawFloatRow("角粘性", component.fluidAngularViscosity, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("合力上限 N", component.fluidMaximumForce, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawSpringForceComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "拘束を作らず、2点間へHookeばね力と速度減衰を加えます。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"接続先",
			component.springForceTargetGameObjectId,
			"World固定点",
			false);
		DrawVector3Row("所有者Anchor", component.springForceLocalAnchor, 0.01f, -100000.0f, 100000.0f);

		if (component.springForceTargetGameObjectId >= 0) {
			DrawVector3Row("接続先Anchor", component.springForceTargetLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		}
		else {
			DrawVector3Row("World固定点", component.springForceWorldAnchor, 0.01f, -1000000.0f, 1000000.0f);
		}

		DrawFloatRow("自然長 m", component.springForceRestLength, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("ばね定数 N/m", component.springForceStiffness, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("減衰 Ns/m", component.springForceDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("Force上限 N", component.springForceMaximumForce, 10.0f, 0.0f, 1000000000.0f);
		DrawCheckboxRow("接続先へ反作用", component.springForceApplyReaction);
	}

	void DrawRopeConstraintComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "最大長を超えた時だけ張力を発生するロープ / ケーブルです。縮んだ時は押し返しません。");
		const char* ropeStateText = !component.isActive ? "解除" : (component.ropeIsBroken ? "破断" : "接続中");
		const std::string ropeLengthText = std::to_string(component.ropeCurrentLength) + " m";
		const std::string ropeTensionText = std::to_string(component.ropeCurrentTension) + " N";
		DrawTextRow("実行状態", ropeStateText);
		DrawTextRow("現在長", ropeLengthText.c_str());
		DrawTextRow("現在張力", ropeTensionText.c_str());
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"接続先",
			component.ropeTargetGameObjectId,
			"World固定点",
			false);
		DrawVector3Row("所有者Anchor", component.ropeLocalAnchor, 0.01f, -100000.0f, 100000.0f);

		if (component.ropeTargetGameObjectId >= 0) {
			DrawVector3Row("接続先Anchor", component.ropeTargetLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		}
		else {
			DrawVector3Row("World固定点", component.ropeWorldAnchor, 0.01f, -1000000.0f, 1000000.0f);
		}

		DrawFloatRow("最大長 m", component.ropeMaximumLength, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("張力係数 N/m", component.ropeStiffness, 1.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("減衰 Ns/m", component.ropeDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("張力上限 N", component.ropeMaximumTension, 10.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("破断張力 N", component.ropeBreakingTension, 10.0f, 0.0f, 1000000000.0f);
		DrawCheckboxRow("接続先へ反作用", component.ropeApplyReaction);
	}

	void DrawWireConnectableComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Hook専用の接続点です。子Hookの場合は力を伝える親Rigidbodyを指定します。");
		DrawCheckboxRow("選択可能", component.wireConnectableAllowSelection);
		DrawIntRow("最大接続本数", component.wireConnectableMaximumConnections);
		DrawFloatRow("破断強度 N", component.wireConnectableStrength, 10.0f, 0.0f, 1000000000.0f);
		DrawIntRow("カテゴリ", component.wireConnectableCategory);
		DrawCheckboxRow("命中点をAnchorに使用", component.wireConnectableUseHitPoint);

		if (!component.wireConnectableUseHitPoint) {
			DrawVector3Row("固定ローカルAnchor", component.wireConnectableLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		}

		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"力を伝えるRigidbody",
			component.wireConnectablePhysicsBodyGameObjectId,
			"自身",
			true);
		DrawVector3Row("通常色", component.wireConnectableNormalColor, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("照準中色", component.wireConnectableTargetedColor, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("選択中色", component.wireConnectableSelectedColor, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("接続中色", component.wireConnectableConnectedColor, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("発光倍率", component.wireConnectableEmissionStrength, 0.05f, 0.0f, 100.0f);
	}

	void DrawWireRendererComponent(EditorComponent& component) {
		DrawTextRow("説明", "Scriptから生成された複数WireをGame Viewへ描画します。");
		DrawCheckboxRow("表示", component.wireRendererVisible);
		DrawFloatRow("線幅 px", component.wireRendererWidth, 0.1f, 0.1f, 128.0f);
		DrawFloatRow("3D半径 m", component.wireRendererWorldRadius, 0.005f, 0.001f, 10.0f);
		DrawIntRow("断面分割数", component.wireRendererRadialSegments);
		DrawFloatRow("発光倍率", component.wireRendererEmissionStrength, 0.05f, 0.0f, 100.0f);
		DrawVector3Row("通常色", component.wireRendererColor, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("高張力色", component.wireRendererTensionColor, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("破断色", component.wireRendererBrokenColor, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("不透明度", component.wireRendererAlpha, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("たるみ量 m", component.wireRendererSlackSag, 0.01f, 0.0f, 1000.0f);
		DrawIntRow("分割数", component.wireRendererSegmentCount);
	}

	void DrawTorsionSpringComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "角度誤差と相対角速度から復元Torqueを計算するねじりばねです。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"基準Object",
			component.torsionTargetGameObjectId,
			"World回転",
			false);
		DrawAngleDegreeRow("目標回転 X", component.torsionRestRotation.x, 0.1f, -180.0f, 180.0f);
		DrawAngleDegreeRow("目標回転 Y", component.torsionRestRotation.y, 0.1f, -180.0f, 180.0f);
		DrawAngleDegreeRow("目標回転 Z", component.torsionRestRotation.z, 0.1f, -180.0f, 180.0f);
		DrawFloatRow("ばね定数 N*m/rad", component.torsionStiffness, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("減衰 N*m*s/rad", component.torsionDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("Torque上限 N*m", component.torsionMaximumTorque, 10.0f, 0.0f, 1000000000.0f);
		DrawCheckboxRow("接続先へ反作用", component.torsionApplyReaction);
	}

	void DrawThrusterComponent(EditorComponent& component) {
		DrawTextRow("説明", "指定作用点へ推進Forceを加えます。重心から外すと旋回Torqueも発生します。");
		DrawVector3Row("推進方向", component.thrusterDirection, 0.01f, -1.0f, 1.0f);
		DrawVector3Row("ローカル作用点", component.thrusterLocalApplicationPoint, 0.01f, -100000.0f, 100000.0f);
		DrawFloatRow("推進力 N", component.thrusterForce, 1.0f, -1000000000.0f, 1000000000.0f);
		DrawFloatRow("スロットル", component.thrusterThrottle, 0.01f, 0.0f, 1.0f);
		DrawCheckboxRow("ローカル方向を使用", component.thrusterUseLocalDirection);
	}

	void DrawPulleyConstraintComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "2本のロープ長を滑車比で結び、全長を超えた時に両方のBodyへ張力を加えます。");
		DrawTextRow("実行状態", component.pulleyIsBroken ? "破断" : "接続中");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"反対側Object",
			component.pulleyTargetGameObjectId,
			"未設定",
			false);
		DrawVector3Row("所有者Anchor", component.pulleyOwnerLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("反対側Anchor", component.pulleyTargetLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("所有者側支持点", component.pulleyOwnerWorldSupport, 0.01f, -1000000.0f, 1000000.0f);
		DrawVector3Row("反対側支持点", component.pulleyTargetWorldSupport, 0.01f, -1000000.0f, 1000000.0f);
		DrawFloatRow("全長 m", component.pulleyTotalLength, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("滑車比", component.pulleyRatio, 0.01f, 0.0001f, 10000.0f);
		DrawFloatRow("張力係数 N/m", component.pulleyStiffness, 1.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("減衰 Ns/m", component.pulleyDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("張力上限 N", component.pulleyMaximumTension, 10.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("破断張力 N", component.pulleyBreakingTension, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawPhysicsServoComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Transformを直接書き換えず、PD制御のForceとTorqueで目標へ追従します。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"追従先",
			component.servoTargetGameObjectId,
			"World目標",
			false);
		DrawVector3Row(
			component.servoTargetGameObjectId >= 0 ? "位置オフセット" : "目標World位置",
			component.servoTargetPosition,
			0.01f,
			-1000000.0f,
			1000000.0f);
		DrawAngleDegreeRow("目標回転 X", component.servoTargetRotation.x, 0.1f, -180.0f, 180.0f);
		DrawAngleDegreeRow("目標回転 Y", component.servoTargetRotation.y, 0.1f, -180.0f, 180.0f);
		DrawAngleDegreeRow("目標回転 Z", component.servoTargetRotation.z, 0.1f, -180.0f, 180.0f);
		DrawSubHeader("位置PD制御");
		DrawFloatRow("位置ばね N/m", component.servoPositionStiffness, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("速度減衰 Ns/m", component.servoPositionDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("Force上限 N", component.servoMaximumForce, 10.0f, 0.0f, 1000000000.0f);
		DrawSubHeader("姿勢PD制御");
		DrawFloatRow("回転ばね N*m/rad", component.servoRotationStiffness, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("角速度減衰", component.servoRotationDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("Torque上限 N*m", component.servoMaximumTorque, 10.0f, 0.0f, 1000000000.0f);
		DrawCheckboxRow("追従先へ反作用", component.servoApplyReaction);
	}

	void DrawVortexFieldComponent(EditorComponent& component) {
		DrawTextRow("説明", "剛体回転流、軸方向流、中心への流入を合成した3D速度場です。");
		DrawVector3Row("ローカル渦軸", component.vortexAxis, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("影響半径 m", component.vortexRadius, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("角速度 rad/s", component.vortexAngularVelocity, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("中心流入速度 m/s", component.vortexRadialInflowVelocity, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("軸方向速度 m/s", component.vortexAxialVelocity, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("速度結合率 1/s", component.vortexVelocityCoupling, 0.01f, 0.0f, 10000.0f);
		DrawFloatRow("加速度上限 m/s2", component.vortexMaximumAcceleration, 0.1f, 0.0f, 1000000000.0f);
	}

	void DrawPressureFieldComponent(EditorComponent& component) {
		DrawTextRow("説明", "圧力とCollider体積から求めた代表面積を使い、F=pAの放射Forceを加えます。");
		DrawFloatRow("圧力 Pa", component.pressureFieldPressure, 1.0f, -1000000000.0f, 1000000000.0f);
		DrawFloatRow("影響半径 m", component.pressureFieldRadius, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("減衰指数", component.pressureFieldFalloffExponent, 0.01f, 0.0f, 32.0f);
		DrawFloatRow("Force上限 N", component.pressureFieldMaximumForce, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawSuspensionComponent(EditorComponent& component) {
		DrawTextRow("説明", "接地Rayとばね・減衰Forceで車輪やホバー脚を支えるサスペンションです。");
		const std::string suspensionLengthText = std::to_string(component.suspensionCurrentLength) + " m";
		DrawTextRow("接地状態", component.suspensionIsGrounded ? "接地" : "非接地");
		DrawTextRow("現在長", suspensionLengthText.c_str());
		DrawVector3Row("ローカル取付点", component.suspensionLocalAnchor, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("ローカル接地方向", component.suspensionLocalDirection, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("自然長 m", component.suspensionRestLength, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("最大伸長 m", component.suspensionMaximumLength, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("車輪半径 m", component.suspensionWheelRadius, 0.01f, 0.0f, 1000000.0f);
		DrawFloatRow("ばね定数 N/m", component.suspensionStiffness, 1.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("減衰 Ns/m", component.suspensionDamping, 1.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("Force上限 N", component.suspensionMaximumForce, 10.0f, 0.0f, 1000000000.0f);
		DrawCheckboxRow("接地法線へForce", component.suspensionUseHitNormal);
		DrawCheckboxRow("接地物へ反作用", component.suspensionApplyReaction);
	}

	void DrawUprightStabilizerComponent(EditorComponent& component) {
		DrawTextRow("説明", "ローカル上方向をWorld上方向へ戻すTorqueを加え、船・車両・飛行体の転倒を抑えます。");
		DrawVector3Row("ローカル上方向", component.uprightLocalUpAxis, 0.01f, -1.0f, 1.0f);
		DrawVector3Row("目標World上方向", component.uprightTargetWorldUp, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("姿勢ばね N*m/rad", component.uprightStiffness, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("角速度減衰", component.uprightDamping, 0.1f, 0.0f, 1000000000.0f);
		DrawFloatRow("Torque上限 N*m", component.uprightMaximumTorque, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawElectromagneticBodyComponent(EditorComponent& component) {
		DrawTextRow("説明", "電場・磁場からCoulomb力、Lorentz力、磁気Torqueを受ける物体です。");
		DrawScientificFloatRow("電荷 C", component.electromagneticCharge, 1.0e-6f, -1.0e12f, 1.0e12f);
		DrawVector3Row("磁気Moment A*m2", component.electromagneticMagneticMoment, 0.01f, -1.0e12f, 1.0e12f);
		DrawFloatRow("Force上限 N", component.electromagneticMaximumForce, 10.0f, 0.0f, 1000000000.0f);
		DrawFloatRow("Torque上限 N*m", component.electromagneticMaximumTorque, 10.0f, 0.0f, 1000000000.0f);
	}

	void DrawElectromagneticFieldComponent(EditorComponent& component) {
		static const char* fieldModes[] = {"一様場", "点電荷"};
		DrawTextRow("説明", "ElectromagneticBodyへ電場と磁場を供給します。");
		DrawComboRow(
			"種類",
			component.electromagneticFieldMode,
			fieldModes,
			static_cast<int32_t>(_countof(fieldModes)));

		if (component.electromagneticFieldMode == 0) {
			DrawVector3Row("電場 E N/C", component.electromagneticElectricField, 0.1f, -1.0e12f, 1.0e12f);
		}
		else {
			DrawScientificFloatRow("源電荷 C", component.electromagneticSourceCharge, 1.0e-6f, -1.0e12f, 1.0e12f);
			DrawScientificFloatRow("Coulomb定数 k", component.electromagneticCoulombConstant, 1000.0f, 0.0f, 1.0e20f);
			DrawFloatRow("最小計算距離 m", component.electromagneticMinimumDistance, 0.01f, 0.001f, 1000000.0f);
		}

		DrawVector3Row("磁束密度 B T", component.electromagneticMagneticField, 0.01f, -1.0e12f, 1.0e12f);
		DrawFloatRow("影響半径 m", component.electromagneticInfluenceRadius, 0.1f, 0.0f, 1000000000.0f);
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

		if (component.type == EditorComponentType::ConfigurableJoint) {
			DrawSubHeader("6 自由度制限");
			DrawAxisFreezeRow(
				"位置を固定",
				component.freezePositionX,
				component.freezePositionY,
				component.freezePositionZ);
			DrawAxisFreezeRow(
				"回転を固定",
				component.freezeRotationX,
				component.freezeRotationY,
				component.freezeRotationZ);
			DrawFloatRow("最小移動", component.jointMinDistance, 0.01f, -100.0f, 100.0f);
			DrawFloatRow("最大移動", component.jointMaxDistance, 0.01f, -100.0f, 100.0f);
			DrawAngleDegreeRow("最小角度", component.jointMinLimit, 0.1f, -180.0f, 180.0f);
			DrawAngleDegreeRow("最大角度", component.jointMaxLimit, 0.1f, -180.0f, 180.0f);
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

		if (component.type == EditorComponentType::Canvas) {
			DrawTextRow("描画先", "Game View");
			DrawIntRow("描画順", component.physicsLayer);
		}

		if (component.type == EditorComponentType::RectTransform) {
			DrawVector2Row("アンカー位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		}

		if (component.type == EditorComponentType::CanvasScaler) {
			DrawVector2Row("基準解像度", component.buttonSize, 1.0f, 1.0f, 16384.0f);
			DrawFloatRow("幅高さの比重", component.sliderValue, 0.01f, 0.0f, 1.0f);
		}

		if (component.type == EditorComponentType::GraphicRaycaster) {
			DrawCheckboxRow("入力を受け取る", component.buttonInteractable);
			DrawIntRow("優先度", component.physicsLayer);
		}

		if (component.type == EditorComponentType::Text ||
			component.type == EditorComponentType::TextMeshProUGUI) {
			DrawStringInputRow("表示文字", component.buttonLabel);
			DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawFloatRow("文字サイズ", component.buttonSize.y, 1.0f, 8.0f, 512.0f);
			const char* fontItems[] = {"既定 (Yu Gothic)", "Meiryo", "MS ゴシック", "MS 明朝", "Yu Gothic Bold"};
			component.textFontIndex = (std::clamp)(
				component.textFontIndex, 0, static_cast<int32_t>(_countof(fontItems)) - 1);
			DrawComboRow("フォント", component.textFontIndex, fontItems, static_cast<int32_t>(_countof(fontItems)));
		}

		if (component.type == EditorComponentType::Image ||
			component.type == EditorComponentType::RawImage) {
			DrawStringInputRow("画像", component.assetPath);

			const bool hasSelectedImage =
				EditorAssetUtility::HasExtension(g_selectedAssetPath, ".png") ||
				EditorAssetUtility::HasExtension(g_selectedAssetPath, ".jpg") ||
				EditorAssetUtility::HasExtension(g_selectedAssetPath, ".jpeg");

			if (hasSelectedImage &&
				ImGui::Button("Projectで選択中の画像を設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = g_selectedAssetPath;
			}

			DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		}

		if (component.type == EditorComponentType::Scrollbar) {
			DrawStringInputRow("表示名", component.buttonLabel);
			DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawFloatRow("値", component.sliderValue, 0.01f, 0.0f, 1.0f);
			DrawStringInputRow("変更関数", component.sliderOnValueChangedFunction);
			DrawCheckboxRow("操作可能", component.buttonInteractable);
		}

		if (component.type == EditorComponentType::Dropdown ||
			component.type == EditorComponentType::TMPDropdown) {
			DrawStringInputRow("表示名", component.buttonLabel);
			DrawStringInputRow("選択肢 (|区切り)", component.assetPath);
			DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawIntRow("選択番号", component.inputBehavior);
			DrawStringInputRow("変更関数", component.sliderOnValueChangedFunction);
			DrawCheckboxRow("操作可能", component.buttonInteractable);
		}

		if (component.type == EditorComponentType::InputField ||
			component.type == EditorComponentType::TMPInputField) {
			DrawStringInputRow("入力文字", component.buttonLabel);
			DrawStringInputRow("プレースホルダー", component.assetPath);
			DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawStringInputRow("変更関数", component.sliderOnValueChangedFunction);
			DrawCheckboxRow("操作可能", component.buttonInteractable);
		}

		if (component.type == EditorComponentType::ScrollRect) {
			DrawVector2Row("表示位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("表示サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawFloatRow("横スクロール", component.uvOffset.x, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("縦スクロール", component.uvOffset.y, 0.01f, 0.0f, 1.0f);
		}

		if (component.type == EditorComponentType::Mask ||
			component.type == EditorComponentType::RectMask2D) {
			DrawVector2Row("切り抜き位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("切り抜きサイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		}

		if (component.type == EditorComponentType::HorizontalLayoutGroup ||
			component.type == EditorComponentType::VerticalLayoutGroup ||
			component.type == EditorComponentType::GridLayoutGroup) {
			DrawVector2Row("開始位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
			DrawVector2Row("セルサイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawFloatRow("間隔", component.sliderValue, 0.5f, 0.0f, 1024.0f);

			if (component.type == EditorComponentType::GridLayoutGroup) {
				DrawIntRow("列数", component.inputBehavior);
				component.inputBehavior = (std::max)(component.inputBehavior, 1);
			}
		}

		if (component.type == EditorComponentType::ContentSizeFitter) {
			DrawCheckboxRow("横を内容へ合わせる", component.freezePositionX);
			DrawCheckboxRow("縦を内容へ合わせる", component.freezePositionY);
		}

		if (component.type == EditorComponentType::AspectRatioFitter) {
			DrawFloatRow("アスペクト比", component.sliderValue, 0.01f, 0.01f, 100.0f);
		}

		if (component.type == EditorComponentType::LayoutElement) {
			DrawVector2Row("優先サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
			DrawIntRow("優先度", component.physicsLayer);
		}

		DrawColor3Row("色", component.color);
		DrawFloatRow("透明度", component.intensity, 0.01f, 0.0f, 1.0f);
	}

	void DrawButtonComponent(EditorComponent& component) {
		DrawTextRow("説明", "Game View 上に表示し、クリック時に C++ Script の関数を呼びます。");
		DrawStringInputRow("表示文字", component.buttonLabel);
		DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
		DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		DrawColor3Row("通常色", component.color);
		DrawColor3Row("ホバー色", component.buttonHoverColor);
		DrawColor3Row("押下色", component.buttonPressedColor);
		DrawCheckboxRow("操作可能", component.buttonInteractable);
		DrawStringInputRow("クリック関数", component.buttonOnClickFunction);
	}

	void DrawToggleComponent(EditorComponent& component) {
		DrawTextRow("説明", "Game View 上に ON / OFF を表示し、変更時に C++ Script の関数を呼びます。");
		DrawStringInputRow("表示文字", component.buttonLabel);
		DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
		DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		DrawCheckboxRow("現在値", component.toggleValue);
		DrawColor3Row("通常色", component.color);
		DrawCheckboxRow("操作可能", component.buttonInteractable);
		DrawStringInputRow("変更関数", component.toggleOnValueChangedFunction);
	}

	void DrawSliderComponent(EditorComponent& component) {
		DrawTextRow("説明", "Game View 上に数値バーを表示し、変更時に C++ Script の関数を呼びます。");
		DrawStringInputRow("表示文字", component.buttonLabel);
		DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
		DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		DrawFloatRow("最小値", component.sliderMinValue, 0.01f, -100000.0f, 100000.0f);
		DrawFloatRow("最大値", component.sliderMaxValue, 0.01f, -100000.0f, 100000.0f);
		DrawFloatRow("現在値", component.sliderValue, 0.01f, component.sliderMinValue, component.sliderMaxValue);
		DrawColor3Row("色", component.color);
		DrawCheckboxRow("操作可能", component.buttonInteractable);
		DrawStringInputRow("変更関数", component.sliderOnValueChangedFunction);
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
		const bool hasBehaviorMode =
			component.type == EditorComponentType::AIBehaviorTask ||
			component.type == EditorComponentType::AIState ||
			component.type == EditorComponentType::AIGoapAction ||
			component.type == EditorComponentType::AIHtnTask ||
			component.type == EditorComponentType::AIHtnMethod;

		if (hasBehaviorMode) {
			const char* behaviorModes[] = {"追跡", "逃走", "巡回", "待機"};
			DrawComboRow("動作", component.inputBehavior, behaviorModes, static_cast<int32_t>(_countof(behaviorModes)));
		}

		if (component.type == EditorComponentType::AIMicroPatherGrid) {
			DrawFloatRow("セルサイズ", component.colliderRadius, 0.01f, 0.1f, 100.0f);
			DrawVector3Row("グリッド数", component.colliderSize, 1.0f, 15.0f, 161.0f);
		}
		else if (component.type == EditorComponentType::AIPathRequest) {
			DrawFloatRow("停止距離", component.colliderRadius, 0.01f, 0.0f, 1000.0f);
		}
		else if (component.type == EditorComponentType::AIRecastNavMeshBuilder) {
			DrawVector3Row("生成範囲", component.colliderSize, 0.1f, 0.1f, 10000.0f);
			DrawFloatRow("最大傾斜", component.navMaxSlope, 1.0f, 0.0f, 89.0f);
			DrawFloatRow("段差", component.navMaxClimb, 0.01f, 0.0f, 100.0f);
		}
		else {
			DrawFloatRow("判定半径", component.colliderRadius, 0.01f, 0.0f, 1000.0f);
			DrawVector3Row("判定サイズ", component.colliderSize, 0.01f, 0.0f, 0.0f);
		}
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

	void DrawFreeTransformComponent(EditorComponent& component) {
		DrawTextRow("説明", "velocity 方向へ力無関係に移動・回転します。軸ごとに有効/無効を指定できます。");
		DrawVector3Row("移動入力", component.velocity, 0.01f, 0.0f, 0.0f);
		DrawFloatRow("移動速度", component.freeMoveSpeed, 0.1f, 0.0f, 100.0f);
		DrawVector3Row("回転入力(deg/s)", component.freeRotationInput, 1.0f, 0.0f, 0.0f);
		DrawFloatRow("回転速度", component.freeRotateSpeed, 1.0f, 0.0f, 360.0f);
		bool moveX = (component.freeMoveAxes & 1) != 0;
		bool moveY = (component.freeMoveAxes & 2) != 0;
		bool moveZ = (component.freeMoveAxes & 4) != 0;
		DrawCheckboxRow("移動 X", moveX);
		DrawCheckboxRow("移動 Y", moveY);
		DrawCheckboxRow("移動 Z", moveZ);
		component.freeMoveAxes = (moveX ? 1 : 0) | (moveY ? 2 : 0) | (moveZ ? 4 : 0);
		bool rotX = (component.freeRotateAxes & 1) != 0;
		bool rotY = (component.freeRotateAxes & 2) != 0;
		bool rotZ = (component.freeRotateAxes & 4) != 0;
		DrawCheckboxRow("回転 X", rotX);
		DrawCheckboxRow("回転 Y", rotY);
		DrawCheckboxRow("回転 Z", rotZ);
		component.freeRotateAxes = (rotX ? 1 : 0) | (rotY ? 2 : 0) | (rotZ ? 4 : 0);
		DrawCheckboxRow("ローカル空間", component.freeUseLocalSpace);
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
		std::string collisionAssetPath;
		const ModelData* loadedModelData = GetModelDataForComponent(gameObject, component, false, collisionAssetPath);
		const ModelData emptyModelData{};
		const ModelData& modelData = loadedModelData != nullptr ? *loadedModelData : emptyModelData;
		const bool hasModelData = loadedModelData != nullptr;
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

	void DrawRailMovementComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Splineに沿う前進と、レール基準の左右・上下移動を組み合わせる汎用RailFollowerです。");
		DrawTextRow("Scene表示", "選択中Pathは橙色、進行方向と左右・上下範囲も補助線で表示します。");

		DrawSubHeader("クイック設定");

		if (ImGui::Button("標準移動", ImVec2(100.0f, 0.0f))) {
			component.railMovementMode = 0;
			component.railSpeed = 8.0f;
			component.railAcceleration = 8.0f;
			component.railDeceleration = 10.0f;
			component.railMovementRange = {5.0f, 3.0f};
			component.railOffsetMoveSpeed = 8.0f;
			component.railOrientToPath = true;
			component.railUseSmoothCurve = true;
			component.railStopAtEnd = true;
		}

		ImGui::SameLine();

		if (ImGui::Button("カメラ経路", ImVec2(110.0f, 0.0f))) {
			component.railMovementMode = 0;
			component.railSpeed = 6.0f;
			component.railAcceleration = 3.0f;
			component.railDeceleration = 3.0f;
			component.railMovementRange = {0.0f, 0.0f};
			component.railStartOffset = {0.0f, 0.0f};
			component.railOrientToPath = true;
			component.railUseSmoothCurve = true;
			component.railStopAtEnd = true;
		}

		ImGui::SameLine();

		if (ImGui::Button("物理乗物", ImVec2(100.0f, 0.0f))) {
			component.railMovementMode = 1;
			component.railSpeed = 10.0f;
			component.railAcceleration = 4.0f;
			component.railDeceleration = 6.0f;
			component.railMovementRange = {3.0f, 1.5f};
			component.railOffsetMoveSpeed = 5.0f;
			component.railPositionInfluence = {1.0f, 1.0f, 1.0f};
			component.railRotationInfluence = {1.0f, 1.0f, 1.0f};
			component.railPositionSpring = 7.0f;
			component.railPositionDamping = 5.0f;
			component.railMaximumAcceleration = 24.0f;
			component.railRotationSpring = 7.0f;
			component.railRotationDamping = 5.0f;
			component.railMaximumAngularAcceleration = 10.0f;
			component.railOrientToPath = true;
			component.railUseSmoothCurve = true;
			component.railStopAtEnd = true;
		}

		ImGui::SameLine();

		if (ImGui::Button("船体推進", ImVec2(100.0f, 0.0f))) {
			component.railMovementMode = 2;
			component.railSpeed = 10.0f;
			component.railAcceleration = 3.0f;
			component.railDeceleration = 5.0f;
			component.railMovementRange = {3.0f, 0.0f};
			component.railOffsetMoveSpeed = 4.0f;
			component.railPositionInfluence = {1.0f, 0.0f, 1.0f};
			component.railRotationInfluence = {0.0f, 1.0f, 0.0f};
			component.railPositionSpring = 4.0f;
			component.railPositionDamping = 2.5f;
			component.railMaximumAcceleration = 12.0f;
			component.railRotationSpring = 5.0f;
			component.railRotationDamping = 3.0f;
			component.railMaximumAngularAcceleration = 5.0f;
			component.railLocalForwardAxis = 0;
			component.railShipHorizontalThrust = true;
			component.railShipLateralAssist = 0.2f;
			component.railOrientToPath = true;
			component.railUseSmoothCurve = true;
			component.railStopAtEnd = true;
		}

		DrawTextRow("用途", "物理乗物=目標Poseへのサーボ、船体推進=船首方向の推力とYaw操舵です。");
		DrawSubHeader("Path作成・編集");

		if (component.railPathGameObjectId < 0 &&
			ImGui::Button("Splineを作成して接続", ImVec2(-1.0f, 0.0f))) {
			g_pendingRailSetupOwnerId = ownerGameObject.id;
		}
		else if (component.railPathGameObjectId >= 0 &&
			ImGui::Button("Spline Editorで編集", ImVec2(-1.0f, 0.0f))) {
			g_isSplineEditorVisible = true;
		}

		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Rail Path",
			component.railPathGameObjectId,
			"未設定",
			false);

		const EditorGameObject* railPathGameObject =
			context.editorScene.FindGameObject(component.railPathGameObjectId);

		if (component.railPathGameObjectId >= 0 && railPathGameObject == nullptr) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
				"参照中のRail PathがSceneに存在しません。");
		}
		else if (railPathGameObject != nullptr && railPathGameObject->children.size() < 2u) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
				"Rail Pathには2点以上の子GameObjectが必要です。");
		}

		DrawSubHeader("移動設定");
		DrawFloatRow("速度", component.railSpeed, 0.1f, -1000.0f, 1000.0f);
		DrawFloatRow("加速度", component.railAcceleration, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("減速度", component.railDeceleration, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("開始位置", component.railStartNormalized, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("向きの先読み", component.railLookAheadDistance, 0.05f, 0.01f, 1000.0f);
		DrawSubHeader("レール内移動");
		DrawVector2Row("左右・上下の範囲", component.railMovementRange, 0.1f, 0.0f, 10000.0f);
		DrawVector2Row("開始オフセット", component.railStartOffset, 0.1f, -10000.0f, 10000.0f);
		component.railStartOffset.x = (std::clamp)(
			component.railStartOffset.x,
			-component.railMovementRange.x,
			component.railMovementRange.x);
		component.railStartOffset.y = (std::clamp)(
			component.railStartOffset.y,
			-component.railMovementRange.y,
			component.railMovementRange.y);
		DrawFloatRow("移動速度", component.railOffsetMoveSpeed, 0.1f, 0.0f, 10000.0f);
		DrawCheckboxRow("PlayerInputを使用", component.railUsePlayerInput);

		if (component.railUsePlayerInput) {
			DrawStringInputRow("Action Map", component.railInputActionMapName);
			DrawStringInputRow("Vector2 Action", component.railInputActionName);
			DrawTextRow("入力", "同じGameObjectのPlayerInputから左右X・上下Yを読みます。");
		}

		const char* movementModeItems[] = {
			"Transform 追従",
			"Dynamic Rigidbody 物理サーボ",
			"Dynamic Rigidbody 船体推進"};
		component.railMovementMode = (std::clamp)(
			component.railMovementMode,
			0,
			static_cast<int32_t>(_countof(movementModeItems)) - 1);
		DrawComboRow(
			"移動方式",
			component.railMovementMode,
			movementModeItems,
			static_cast<int32_t>(_countof(movementModeItems)));

		if (component.railMovementMode == 1 || component.railMovementMode == 2) {
			DrawTextRow(
				"物理追従",
				component.railMovementMode == 1 ?
					"Spline接線と目標位置へPD制御の力・トルクを加えます。" :
					"船首ローカル軸へ推力を加え、Spline方向へYaw操舵します。");
			DrawTextRow("浮力と併用", "位置追従軸Yを0、回転追従軸X/Zを0にすると上下・傾きを浮力へ任せられます。");

			if (ImGui::Button("浮力併用プリセット", ImVec2(-1.0f, 0.0f))) {
				component.railPositionInfluence = {1.0f, 0.0f, 1.0f};
				component.railRotationInfluence = {0.0f, 1.0f, 0.0f};
				component.railPositionSpring = 5.0f;
				component.railPositionDamping = 4.0f;
				component.railMaximumAcceleration = 18.0f;
				component.railRotationSpring = 6.0f;
				component.railRotationDamping = 4.0f;
				component.railMaximumAngularAcceleration = 8.0f;
			}

			DrawVector3Row("位置追従軸", component.railPositionInfluence, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("位置ばね", component.railPositionSpring, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("位置減衰", component.railPositionDamping, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("最大加速度", component.railMaximumAcceleration, 0.1f, 0.0f, 10000.0f);
			DrawVector3Row("回転追従軸", component.railRotationInfluence, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("回転ばね", component.railRotationSpring, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("回転減衰", component.railRotationDamping, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("最大角加速度", component.railMaximumAngularAcceleration, 0.1f, 0.0f, 10000.0f);

			if (component.railMovementMode == 2) {
				const char* forwardAxisItems[] = {"+Z", "-Z", "+X", "-X"};
				component.railLocalForwardAxis = (std::clamp)(component.railLocalForwardAxis, 0, 3);
				DrawComboRow(
					"船首ローカル軸",
					component.railLocalForwardAxis,
					forwardAxisItems,
					static_cast<int32_t>(_countof(forwardAxisItems)));

				DrawSubHeader("Mode 2 オートパイロット");
				DrawTextRow("説明",
					"RailはPD拘束の目標位置ではなく、航路・少し先の目標地点・目標速度だけを与えます。"
					"実際の移動は船首方向のエンジン推力とYaw操舵で発生させます。カーブは横Forceではなく"
					"Yaw操舵で船首が先に向き、その方向への推進力で曲がります。");
				DrawFloatRow("推進速度ゲイン", component.railEngineSpeedGain, 0.1f, 0.0f, 50.0f);
				DrawFloatRow("エンジン加速応答", component.railEngineAccelResponse, 0.1f, 0.0f, 100.0f);
				DrawFloatRow("エンジン減速応答", component.railEngineDecelResponse, 0.1f, 0.0f, 100.0f);
				DrawFloatRow("最大前進加速度", component.railEngineMaxAcceleration, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("操舵基本先読み距離(m)", component.railSteeringBaseLookAheadDistance, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("操舵先読み時間(秒)", component.railSteeringLookAheadTime, 0.05f, 0.0f, 5.0f);
				DrawFloatRow("操舵Yaw強さ", component.railSteeringYawGain, 0.1f, 0.0f, 100.0f);
				DrawFloatRow("操舵Yawダンピング", component.railSteeringYawDamping, 0.1f, 0.0f, 100.0f);
				DrawFloatRow(
					"最大Yaw角加速度", component.railSteeringMaxYawAngularAcceleration, 0.1f, 0.0f, 100.0f);
				DrawFloatRow("横補助Dead Zone(m)", component.railLateralAssistDeadZone, 0.1f, 0.0f, 50.0f);
				DrawFloatRow("横補助開始距離(m)", component.railLateralAssistSoftRadius, 0.1f, 0.0f, 50.0f);
				DrawFloatRow("横補助緊急距離(m)", component.railLateralAssistEmergencyRadius, 0.1f, 0.0f, 100.0f);
				DrawFloatRow("横補助最大倍率", component.railLateralAssistMaxMultiplier, 0.1f, 0.0f, 10.0f);
				DrawTextRow("横補助の意味",
					"Dead Zone以内は操舵のみで戻します(横Forceなし)。開始距離まで弱く、緊急距離まで"
					"最大倍率まで強め、それ以上はクランプします。位置ばね/減衰(上の位置ばね・位置減衰)に"
					"倍率として掛かります。");

				DrawSubHeader("船体横滑り抑制 (Hull Lateral Grip)");
				DrawTextRow("説明",
					"上の横補助(Rail位置基準)とは完全に別物です。Rail位置は一切見ず、船体基準の"
					"横方向速度(shipRightXZ成分)だけを、船体が水を横から受けて減衰する挙動として"
					"再現します。船首が先に曲がり、速度ベクトルが遅れて追従する高速艇らしい旋回を"
					"作るためのMode 2専用ゲームプレイ補助で、Buoyancy等の水力モデルは変更しません。"
					"Center of Massへの通常AddForceのみで、余計なTorqueは発生させません。");
				DrawCheckboxRow("船体横滑り抑制を使用", component.railHullLateralGripEnabled);
				DrawFloatRow("横グリップ強さ", component.railHullLateralGripStrength, 0.1f, 0.0f, 50.0f);
				DrawFloatRow(
					"横グリップ最大加速度", component.railHullLateralGripMaxAcceleration, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("横グリップ開始速度(m/s)", component.railHullLateralGripMinSpeed, 0.1f, 0.0f, 50.0f);
				DrawFloatRow("横グリップ最大速度(m/s)", component.railHullLateralGripFullSpeed, 0.1f, 0.0f, 50.0f);
				DrawFloatRow(
					"横滑りDead Zone速度(m/s)", component.railHullLateralGripDeadZoneSpeed, 0.05f, 0.0f, 10.0f);
				DrawFloatRow(
					"横滑り角補助開始角度(度)", component.railHullLateralGripSlipStartDegrees, 1.0f, 0.0f, 90.0f);
				DrawFloatRow(
					"横滑り角補助最大角度(度)", component.railHullLateralGripSlipFullDegrees, 1.0f, 0.0f, 90.0f);

				DrawSubHeader("Mode 2 最終合成加速度上限");
				DrawTextRow("説明",
					"上のrailMaximumAcceleration(最大加速度、Mode 1由来)ではなく、Mode 2の"
					"Engine+Hull Grip+Rail Assist合成後にはこちらを使います。各成分は既に個別に"
					"Clamp済みのため、通常走行ではこの上限に到達しないくらい大きな値にしてください。");
				DrawFloatRow(
					"Mode2 最大合成加速度", component.railMode2MaxCombinedAcceleration, 1.0f, 0.0f, 500.0f);

				DrawSubHeader("Mode 2 移動方式");
				DrawTextRow("説明",
					"0=Boat Autopilot(上のPure Pursuit・Engine・Hull Grip・Rail Assist等の"
					"物理追従方式、既存)。1=Rail Ride(ディズニーのボートライドのように、"
					"XZ位置・Yaw・進行速度をRailへ完全固定し、Y/Pitch/RollだけBuoyancy等の"
					"物理演出として残すレールシューティング専用方式)。Boat Autopilotのコードは"
					"削除せず両方式を切替可能な形で維持しています。PlayerShipはRail Rideを使用します。");
				{
					static const char* const kMode2MovementStyleItems[] = {"Boat Autopilot", "Rail Ride"};
					DrawComboRow(
						"Mode2移動方式",
						component.railMode2MovementStyle,
						kMode2MovementStyleItems,
						static_cast<int32_t>(_countof(kMode2MovementStyleItems)));
				}
				DrawFloatRow(
					"Rail RideのYawサンプル距離(m)", component.railRideYawSampleDistance, 0.1f, 0.1f, 20.0f);
				DrawTextRow("サンプル距離",
					"Rail RideのYawは、現在Rail Progressの前後をこの距離だけ中央差分サンプルして"
					"接線方向を求めます。Rail終端(非ループ)では片側差分へ自動的にフォールバックします。");

				DrawTextRow("下のRoll/Pitch/Yaw角度制限・回転ばね等について",
					"Mode 2はこれらのSpline接線ベースの回転PD経路を使いません(上のオートパイロットの"
					"操舵Yawのみで制御します)。Pitch/RollはBuoyancy・Safety Envelope・絶対角度制限に"
					"委ねられます。以下はMode 1、または将来Mode 2で使う場合のために残しています。");
				DrawFloatRow("最大ロール角度", component.railMaximumRollAngle, 1.0f, 0.0f, 90.0f);
				DrawTextRow("角度制限", "0で制限なし。波で転覆しない角度を指定します。");
				DrawFloatRow("ロール復元力", component.railRollRestorationStrength, 0.5f, 0.0f, 100.0f);
				DrawTextRow("復元力", "直立姿勢に戻す力の強さ。0で無効。");
				DrawFloatRow("ロールダンピング", component.railRollDamping, 0.5f, 0.0f, 100.0f);
				DrawTextRow("ダンピング", "ロール角速度への減衰。0で無効。");
				DrawFloatRow("最大ピッチ角度", component.railMaximumPitchAngle, 1.0f, 0.0f, 90.0f);
				DrawTextRow("角度制限", "0で制限なし。波で前後に傾きすぎない角度を指定します。");
				DrawFloatRow("ピッチ復元力", component.railPitchRestorationStrength, 0.5f, 0.0f, 100.0f);
				DrawTextRow("復元力", "水平姿勢に戻す力の強さ。0で無効。");
				DrawFloatRow("ピッチダンピング", component.railPitchDamping, 0.5f, 0.0f, 100.0f);
				DrawTextRow("ダンピング", "ピッチ角速度への減衰。0で無効。");
				DrawFloatRow("最大ヨー角度", component.railMaximumYawAngle, 1.0f, 0.0f, 180.0f);
				DrawTextRow("角度制限", "0で制限なし。進行方向から左右に回転できる角度を指定します。");
				DrawFloatRow("ヨー復元力", component.railYawRestorationStrength, 0.5f, 0.0f, 100.0f);
				DrawTextRow("復元力", "進行方向に戻す力の強さ。0で無効。");
				DrawFloatRow("ヨーダンピング", component.railYawDamping, 0.5f, 0.0f, 100.0f);
				DrawTextRow("ダンピング", "ヨー角速度への減衰。0で無効。");

				DrawSubHeader("Yaw Safety Assist");
				DrawTextRow("説明",
					"Rail見出しからのYaw偏差が大きいほど、物理追従の目標前進速度を非線形に落とし、"
					"Yaw復元強度を非線形に強めます。船が横向きのままRailだけ全速で押し続けることを防ぎます。"
					"Rail進行そのもの(進行距離・指令速度)は変更しません。");
				DrawCheckboxRow("Yaw Safety Assistを使用", component.railYawSafetyAssistEnabled);
				DrawFloatRow("Stage1 開始角度(度)", component.railYawSafetyStage1Degrees, 1.0f, 0.0f, 180.0f);
				DrawTextRow("Stage1", "この角度からYaw復元強化を開始します。速度はまだ落ちません。");
				DrawFloatRow("Stage2 開始角度(度)", component.railYawSafetyStage2Degrees, 1.0f, 0.0f, 180.0f);
				DrawTextRow("Stage2", "この角度から目標前進速度を落とし始めます。");
				DrawFloatRow("Stage4 到達角度(度)", component.railYawSafetyStage4Degrees, 1.0f, 0.0f, 180.0f);
				DrawTextRow("Stage4", "この角度で速度スケール最小・復元強化最大に達します(高速直進を禁止)。");
				DrawFloatRow("最大復元倍率", component.railYawSafetyMaxRestorationScale, 0.1f, 1.0f, 10.0f);
				DrawFloatRow("最小速度倍率", component.railYawSafetyMinSpeedScale, 0.01f, 0.0f, 1.0f);
				DrawFloatRow("Forward Position Error 上限(m)", component.railMaxForwardRecoveryError, 0.5f, 0.0f, 500.0f);
				DrawTextRow("上限の意味",
					"Rail進行(s)が物理追従より先へ進んでも、位置補正力の元になるForward誤差の絶対値を"
					"ここで頭打ちにします。横方向誤差には影響しません。");

				DrawSubHeader("Roll/Pitch Safety Envelope");
				DrawTextRow("説明",
					"波による通常の揺れ(free角度以下)にはRailは一切介入せずBuoyancyへ任せます。"
					"emergency角度へ近づくほど、Attitude Recovery Torqueと前進方向の推力/位置補正を"
					"非線形(t^2)に強める/弱めます。railRotationInfluenceのマスクとは独立して働きます。");
				DrawCheckboxRow("Attitude Safety Assistを使用", component.railAttitudeSafetyAssistEnabled);
				DrawFloatRow("Roll Free角度(度)", component.railRollFreeDegrees, 1.0f, 0.0f, 90.0f);
				DrawFloatRow("Roll Emergency角度(度)", component.railRollEmergencyDegrees, 1.0f, 0.0f, 180.0f);
				DrawFloatRow("Pitch Free角度(度)", component.railPitchFreeDegrees, 1.0f, 0.0f, 90.0f);
				DrawFloatRow("Pitch Emergency角度(度)", component.railPitchEmergencyDegrees, 1.0f, 0.0f, 180.0f);
				DrawFloatRow("Attitude復元力", component.railAttitudeSafetyStrength, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("Attitudeダンピング", component.railAttitudeSafetyDamping, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("Attitude Torque上限", component.railAttitudeSafetyMaxTorque, 0.5f, 0.0f, 200.0f);
				DrawFloatRow("危険時Forward最小倍率", component.railAttitudeSafetyMinForwardScale, 0.01f, 0.0f, 1.0f);

				DrawSubHeader("Physical Rail Progress");
				DrawTextRow("説明",
					"Gameplay Rail Progress(敵出現等の進行)は基準速度で進み続けますが、Position PDが"
					"追う目標位置はPhysical Rail Progressという別の距離から作ります。Safetyで物理速度を"
					"落としてもGameplayだけが先へ逃げず、Position Errorが無制限に増大しません。"
					"Safety解除後はここで設定した倍率の範囲でGameplayへ徐々に追いつきます。");
				DrawFloatRow(
					"Catchup倍率", component.railPhysicalCatchupSpeedMultiplier, 0.01f, 1.0f, 3.0f);

				DrawSubHeader("Pitch/Roll 絶対角度制限 (Hard Clamp)");
				DrawTextRow("説明",
					"上のRoll/Pitch Safety Envelope(段階的な復元Torque)とも下の角度ソフト制限とも"
					"完全に独立した第3の機能です。Pitch/RollはBuoyancy・波・着水・Planingで通常通り"
					"物理的に動かしますが、毎Physics Step終了後(Jolt積分・最終姿勢確定後)に限界角度を"
					"超えていないか確認し、超えていればその場でRigidbody回転を限界角度へ直接補正します"
					"(この機能に限りTransform/Rigidbody回転の直接変更を行います)。"
					"Yawは変更しません。abs(Pitch)・abs(Roll)は常に指定角度以内に収まります。");
				DrawCheckboxRow("絶対角度制限(Hard Clamp)を使用", component.railAttitudeAngleLimitEnabled);
				DrawFloatRow(
					"Pitch最大角度(度)", component.railAttitudeAngleLimitMaxPitchDegrees, 1.0f, 0.0f, 90.0f);
				DrawFloatRow(
					"Roll最大角度(度)", component.railAttitudeAngleLimitMaxRollDegrees, 1.0f, 0.0f, 90.0f);

				DrawSubHeader("Pitch/Roll 角度ソフト制限 (Torque)");
				DrawTextRow("説明",
					"上のHard Clampとは別に、Torqueによる押し戻し方式も独立して用意しています。"
					"上で設定した限界角度を超えた分だけTorqueで押し戻します(角度そのものは書き換えません)。"
					"既定OFF。Hard ClampとSoft Limitは同時に有効化できます。");
				DrawCheckboxRow("角度ソフト制限を使用", component.railAttitudeAngleSoftLimitEnabled);
				DrawFloatRow(
					"押し戻し強さ", component.railAttitudeAngleSoftLimitStrength, 0.5f, 0.0f, 200.0f);
				DrawFloatRow(
					"押し戻しダンピング", component.railAttitudeAngleSoftLimitDamping, 0.5f, 0.0f, 200.0f);
				DrawFloatRow(
					"押し戻しTorque上限", component.railAttitudeAngleSoftLimitMaxTorque, 0.5f, 0.0f, 200.0f);
			}
		}

		DrawCheckboxRow("ループ", component.railLoop);
		DrawCheckboxRow("進行方向へ回転", component.railOrientToPath);
		DrawCheckboxRow("滑らかな曲線", component.railUseSmoothCurve);
		DrawCheckboxRow("開始時に停止", component.railStartPaused);
		DrawCheckboxRow("逆方向", component.railReverse);
		DrawCheckboxRow("終端で停止", component.railStopAtEnd);

		if (component.railMovementMode != 0) {
			DrawSubHeader("Runtime 診断");
			DrawTextRow("説明",
				"物理追従が実際に加えた力です。浮力併用(位置追従軸Y=0、回転追従軸X/Z=0)なら "
				"追従ForceのYと追従TorqueのX/Zが0になり、上下と傾きはBuoyancyへ委ねられています。");
			DrawReadOnlyVector3Row("追従Force", component.railDebugFollowForce);
			DrawReadOnlyVector3Row("追従Torque", component.railDebugFollowTorque);
			DrawReadOnlyVector3Row("位置誤差", component.railDebugPositionError);
			DrawReadOnlyFloatRow("Yaw誤差 rad", component.railDebugYawError);
			DrawReadOnlyFloatRow("Rail指令速度 m/s", component.railDebugCurrentSpeed);
			DrawReadOnlyFloatRow("実前進速度 m/s", component.railDebugActualForwardSpeed);
			DrawReadOnlyFloatRow("Gameplay Rail Progress m", component.railDebugGameplayRailProgress);
			DrawReadOnlyFloatRow("Physical Rail Progress m", component.railDebugPhysicalRailProgress);
			DrawReadOnlyFloatRow("Physical Target Speed m/s", component.railDebugPhysicalTargetSpeed);
			DrawReadOnlyFloatRow("Pitch制限中(1=制限)", component.railDebugPitchAngleLimited);
			DrawReadOnlyFloatRow("Roll制限中(1=制限)", component.railDebugRollAngleLimited);
			DrawReadOnlyFloatRow("YawSafety 速度倍率", component.railDebugYawSafetySpeedScale);
			DrawReadOnlyFloatRow("YawSafety 復元倍率", component.railDebugYawSafetyRestorationScale);
			DrawReadOnlyFloatRow("Forward Position Scale(合成)", component.railDebugForwardPositionScale);
			DrawReadOnlyFloatRow("Forward Position Error(m)", component.railDebugForwardPositionError);
			DrawReadOnlyFloatRow("Lateral Position Error(m)", component.railDebugLateralPositionError);
			DrawReadOnlyFloatRow("Forward補正力 N", component.railDebugForwardCorrectionForce);
			DrawReadOnlyFloatRow("Lateral補正力 N", component.railDebugLateralCorrectionForce);
			DrawReadOnlyFloatRow("船体Pitch(度)", component.railDebugBoatPitchDegrees);
			DrawReadOnlyFloatRow("船体Roll(度)", component.railDebugBoatRollDegrees);
			DrawReadOnlyFloatRow("Pitch Safety Factor", component.railDebugPitchSafetyFactor);
			DrawReadOnlyFloatRow("Roll Safety Factor", component.railDebugRollSafetyFactor);
			DrawReadOnlyVector3Row("Attitude Recovery Torque", component.railDebugAttitudeRecoveryTorque);

			if (component.railMovementMode == 2) {
				DrawSubHeader("Mode 2 診断");
				DrawReadOnlyFloatRow("Rail最近傍距離 m", component.railDebugClosestRailDistance);
			DrawReadOnlyFloatRow("Rail最近傍距離変化量 m", component.railDebugClosestRailDistanceDelta);
				DrawReadOnlyFloatRow("操舵先読み距離 m", component.railDebugSteeringLookAheadDistance);
				DrawReadOnlyFloatRow("操舵目標Yaw誤差 度", component.railDebugSteeringYawErrorDegrees);
				DrawReadOnlyFloatRow("エンジン加速度 m/s2", component.railDebugEngineAcceleration);
				DrawReadOnlyFloatRow("横補助加速度 m/s2", component.railDebugLateralAssistAcceleration);
				DrawReadOnlyFloatRow("横補助倍率", component.railDebugLateralAssistScale);
				DrawReadOnlyFloatRow("Yaw角速度 rad/s", component.railDebugYawAngularVelocity);
				DrawReadOnlyVector3Row("船体計算上Forward", component.railDebugShipForward);
				DrawReadOnlyVector3Row("船体計算上Right", component.railDebugShipRight);
				DrawReadOnlyFloatRow(
					"船首-移動方向差 度", component.railDebugForwardVelocitySlipAngleDegrees);
				DrawReadOnlyFloatRow("水平速度 m/s", component.railDebugHorizontalSpeed);
				DrawReadOnlyFloatRow("船体横方向速度 m/s", component.railDebugLateralSpeed);
				DrawReadOnlyFloatRow(
					"船体横グリップ加速度 m/s2", component.railDebugHullLateralGripAcceleration);
				DrawReadOnlyFloatRow("船体横グリップ速度倍率", component.railDebugHullLateralGripSpeedFactor);
				DrawReadOnlyFloatRow("船体横グリップSlip倍率", component.railDebugHullLateralGripSlipFactor);
				DrawReadOnlyFloatRow("船体横グリップ最終倍率", component.railDebugHullLateralGripScale);
				DrawReadOnlyFloatRow("合成前加速度 m/s2", component.railDebugPreClampAcceleration);
				DrawReadOnlyFloatRow("合成後加速度 m/s2", component.railDebugPostClampAcceleration);
				DrawReadOnlyFloatRow("Mode2最終Clamp倍率", component.railDebugMode2ClampScale);

				if (component.railMode2MovementStyle == 1) {
					DrawSubHeader("Rail Ride 診断");
					DrawTextRow("説明",
						"Rail Ride成功条件: Rail位置誤差XZ≒0、Rail-Yaw誤差≒0、速度方向-Rail方向差≒0。"
						"上のBoat Autopilot診断(Rail最近傍距離・操舵Yaw誤差・Hull Grip等)はRail Ride中は"
						"未使用のため0またはNot Activeのままで問題ありません。");
					DrawReadOnlyVector3Row("Rail固定位置", component.railDebugRailRidePosition);
					DrawReadOnlyVector3Row("実PlayerShip位置", component.railDebugRailRideActualPosition);
					DrawReadOnlyFloatRow("Rail位置誤差XZ m", component.railDebugRailRidePositionErrorXZ);
					DrawReadOnlyVector3Row("Rail接線Forward", component.railDebugRailRideForward);
					DrawReadOnlyFloatRow("Rail Target Yaw 度", component.railDebugRailRideTargetYawDegrees);
					DrawReadOnlyFloatRow("PlayerShip最終Yaw 度", component.railDebugRailRideFinalYawDegrees);
					DrawReadOnlyFloatRow("Rail-Yaw誤差 度", component.railDebugRailRideYawErrorDegrees);
					DrawReadOnlyVector3Row("Rail Velocity XZ", component.railDebugRailRideVelocityXZ);
					DrawReadOnlyVector3Row("Rigidbody Velocity XZ", component.railDebugRailRideActualVelocityXZ);
					DrawReadOnlyFloatRow(
						"速度方向-Rail方向差 度", component.railDebugRailRideVelocityDirectionErrorDegrees);
					DrawReadOnlyFloatRow("Physics Y", component.railDebugRailRidePhysicsY);
					DrawReadOnlyFloatRow("最終Y", component.railDebugRailRideFinalY);
					DrawReadOnlyFloatRow("Physics Pitch 度", component.railDebugRailRidePhysicsPitchDegrees);
					DrawReadOnlyFloatRow("最終Pitch 度", component.railDebugRailRideFinalPitchDegrees);
					DrawReadOnlyFloatRow("Physics Roll 度", component.railDebugRailRidePhysicsRollDegrees);
					DrawReadOnlyFloatRow("最終Roll 度", component.railDebugRailRideFinalRollDegrees);
				}
			}

			DrawTextRow("追従軸の実効値",
				"下2行が実際に適用されている追従軸です。浮力併用なら位置(1,0,1)・回転(0,1,0)に "
				"なっているはずで、そうでなければSceneの保存値が想定と違っています。");
			DrawReadOnlyVector3Row("適用中 位置追従軸", component.railDebugAppliedPositionInfluence);
			DrawReadOnlyVector3Row("適用中 回転追従軸", component.railDebugAppliedRotationInfluence);
		}
	}

	void DrawRailSpeedProfileComponent(EditorComponent& component) {
		DrawTextRow("説明", "Rail進行率ごとの速度倍率を補間します。RailMovementの基準速度は変更しません。");
		DrawCheckboxRow("プロファイルを使用", component.railSpeedProfileEnabled);
		int32_t removeIndex = -1;

		for (size_t keyIndex = 0u; keyIndex < component.railSpeedKeys.size(); keyIndex++) {
			EditorRailSpeedKey& speedKey = component.railSpeedKeys[keyIndex];
			ImGui::PushID(static_cast<int32_t>(keyIndex));
			DrawSubHeader(("速度キー " + std::to_string(keyIndex + 1u)).c_str());
			DrawFloatRow("進行率", speedKey.normalizedProgress, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("速度倍率", speedKey.speedMultiplier, 0.01f, 0.0f, 100.0f);

			if (ImGui::Button("このキーを削除")) {
				removeIndex = static_cast<int32_t>(keyIndex);
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.railSpeedKeys.erase(component.railSpeedKeys.begin() + removeIndex);
		}

		if (ImGui::Button("速度キーを追加")) {
			component.railSpeedKeys.push_back({1.0f, 1.0f});
		}

		ImGui::SameLine();

		if (ImGui::Button("進行率順に並べる")) {
			std::sort(
				component.railSpeedKeys.begin(),
				component.railSpeedKeys.end(),
				[](const EditorRailSpeedKey& firstKey, const EditorRailSpeedKey& secondKey) {
					return firstKey.normalizedProgress < secondKey.normalizedProgress;
				});
		}
	}

	void DrawRailZoneComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "進入中だけ速度倍率と左右上下範囲を上書きし、進入・退出ActionへZone IDを渡します。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Action対象",
			component.railZoneActionTargetGameObjectId,
			"このObject",
			true);
		int32_t removeIndex = -1;

		for (size_t zoneIndex = 0u; zoneIndex < component.railZoneEntries.size(); zoneIndex++) {
			EditorRailZoneEntry& zoneEntry = component.railZoneEntries[zoneIndex];
			ImGui::PushID(static_cast<int32_t>(zoneIndex));
			DrawSubHeader(("区間 " + std::to_string(zoneIndex + 1u)).c_str());
			DrawStringInputRow("Zone ID", zoneEntry.zoneId);
			DrawFloatRow("開始進行率", zoneEntry.startNormalized, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("終了進行率", zoneEntry.endNormalized, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("速度倍率", zoneEntry.speedMultiplier, 0.01f, 0.0f, 100.0f);
			DrawCheckboxRow("移動範囲を上書き", zoneEntry.overrideMovementRange);

			if (zoneEntry.overrideMovementRange) {
				DrawVector2Row("左右・上下範囲", zoneEntry.movementRange, 0.1f, 0.0f, 10000.0f);
			}

			DrawStringInputRow("進入Action", zoneEntry.enteredActionName);
			DrawStringInputRow("退出Action", zoneEntry.exitedActionName);

			if (ImGui::Button("この区間を削除")) {
				removeIndex = static_cast<int32_t>(zoneIndex);
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.railZoneEntries.erase(component.railZoneEntries.begin() + removeIndex);
		}

		if (ImGui::Button("レール区間を追加")) {
			EditorRailZoneEntry zoneEntry{};
			zoneEntry.zoneId = "Zone" + std::to_string(component.railZoneEntries.size() + 1u);
			component.railZoneEntries.push_back(zoneEntry);
		}

		DrawTextRow("Runtime区間", std::to_string(component.railZoneActiveIndex).c_str());
	}

	void DrawCameraFollowComposerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "対象の向きと速度を使い、位置・注視点・減衰・水平安定化をGame Cameraへ合成します。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"追従対象",
			component.cameraComposerTargetGameObjectId,
			"Cameraの接続先",
			true);
		DrawVector3Row("追従Offset", component.cameraComposerFollowOffset, 0.1f, -10000.0f, 10000.0f);
		DrawVector3Row("注視Offset", component.cameraComposerLookAtOffset, 0.1f, -10000.0f, 10000.0f);
		DrawFloatRow("位置減衰", component.cameraComposerPositionDamping, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("回転減衰", component.cameraComposerRotationDamping, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("速度先読み秒", component.cameraComposerLookAheadSeconds, 0.01f, 0.0f, 10.0f);
		DrawVector2Row("デッドゾーン", component.cameraComposerDeadZone, 0.01f, 0.0f, 1000.0f);
		DrawFloatRow("1Frame最大追従距離", component.cameraComposerMaximumDistance, 0.1f, 0.0f, 10000.0f);
		DrawCheckboxRow("対象Yawを継承", component.cameraComposerInheritTargetYaw);
		DrawCheckboxRow("Pitch/Rollを安定化", component.cameraComposerStabilizePitchRoll);
	}

	void DrawSpeedFeedbackComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "RigidbodyまたはRailMovementの実速度をFOV、Motion Blur、Camera揺れ強度へ変換します。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "速度Source", component.speedFeedbackSourceGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "対象Camera", component.speedFeedbackCameraGameObjectId, "最高Priority Camera", true);
		DrawFloatRow("最小速度", component.speedFeedbackMinimumSpeed, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("最大速度", component.speedFeedbackMaximumSpeed, 0.1f, 0.01f, 10000.0f);
		component.speedFeedbackMaximumSpeed = (std::max)(
			component.speedFeedbackMaximumSpeed,
			component.speedFeedbackMinimumSpeed + 0.01f);
		DrawFloatRow("最小FOV", component.speedFeedbackMinimumFovDegrees, 0.1f, 1.0f, 179.0f);
		DrawFloatRow("最大FOV", component.speedFeedbackMaximumFovDegrees, 0.1f, 1.0f, 179.0f);
		DrawFloatRow("最小Blur", component.speedFeedbackMinimumMotionBlur, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("最大Blur", component.speedFeedbackMaximumMotionBlur, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Camera強度加算", component.speedFeedbackCameraStrength, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("応答速度", component.speedFeedbackResponseSpeed, 0.1f, 0.0f, 1000.0f);
		DrawTextRow("Runtime速度率", std::to_string(component.speedFeedbackNormalized).c_str());
	}

	void DrawSpawnedObjectSetupComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "同じWaveから生成した全個体へ共通設定を自動適用します。敵ごとの子設定は不要です。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "Rail Path", component.spawnedSetupRailPathGameObjectId, "Template設定を使用", true);
		DrawFloatRow("開始進行率", component.spawnedSetupRailStartNormalized, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("個体ごとの進行率差", component.spawnedSetupRailStartStep, 0.001f, -1.0f, 1.0f);
		DrawFloatRow("Rail速度倍率", component.spawnedSetupRailSpeedMultiplier, 0.01f, 0.0f, 100.0f);
		DrawCheckboxRow("Teamを上書き", component.spawnedSetupOverrideTeam);

		if (component.spawnedSetupOverrideTeam) {
			DrawIntRow("Team ID", component.spawnedSetupTeamId);
		}

		DrawCheckboxRow("生成時にRuntime状態をReset", component.spawnedSetupResetRuntimeState);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.spawnedSetupActionTargetGameObjectId, "このWave", true);
		DrawStringInputRow("適用完了Action", component.spawnedSetupAppliedActionName);
	}

	void DrawWaveMotionProfileComponent(EditorComponent& component) {
		DrawTextRow("説明", "Wave編隊の基準Offsetへ周期運動を加えます。生成個体ごとの設定は不要です。");
		const char* motionModeItems[] = {"なし", "Sine", "8の字", "交互運動"};
		component.waveMotionMode = (std::clamp)(component.waveMotionMode, 0, 3);
		DrawComboRow("移動パターン", component.waveMotionMode, motionModeItems, static_cast<int32_t>(_countof(motionModeItems)));
		DrawVector2Row("左右・上下振幅", component.waveMotionAmplitude, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("周波数", component.waveMotionFrequency, 0.01f, 0.0f, 1000.0f);
		DrawFloatRow("個体ごとの位相差", component.waveMotionPhaseStep, 0.01f, -100.0f, 100.0f);
		DrawFloatRow("Blend In秒", component.waveMotionBlendInSeconds, 0.01f, 0.0f, 1000.0f);
	}

	void DrawDistanceActivationComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Camera等との距離を使いObject実体を停止・復帰します。往復距離を分けて境界振動を防ぎます。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"距離基準",
			component.distanceActivationReferenceGameObjectId,
			"最高Priority Camera",
			true);
		DrawFloatRow("有効化距離", component.distanceActivationEnterDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("無効化距離", component.distanceActivationExitDistance, 1.0f, 0.0f, 1000000.0f);
		component.distanceActivationExitDistance = (std::max)(
			component.distanceActivationExitDistance,
			component.distanceActivationEnterDistance);
		DrawCheckboxRow("子階層も対象", component.distanceActivationAffectHierarchy);
		DrawTextRow("Runtime", component.distanceActivationRuntimeActive ? "実体化" : "休止");
	}

	void DrawSimulationLodComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "距離をNear/Medium/Far/Culledへ分け、Farで重い系統を止め、CulledでObjectを休止します。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"距離基準",
			component.simulationLodReferenceGameObjectId,
			"最高Priority Camera",
			true);
		DrawFloatRow("Medium距離", component.simulationLodMediumDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("Far距離", component.simulationLodFarDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("Culled距離", component.simulationLodCulledDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("Medium Script更新秒", component.simulationLodMediumScriptInterval, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("Far Script更新秒", component.simulationLodFarScriptInterval, 0.01f, 0.0f, 10.0f);
		component.simulationLodFarDistance = (std::max)(
			component.simulationLodFarDistance,
			component.simulationLodMediumDistance);
		component.simulationLodCulledDistance = (std::max)(
			component.simulationLodCulledDistance,
			component.simulationLodFarDistance);
		DrawCheckboxRow("FarでPhysics停止", component.simulationLodDisablePhysicsAtFar);
		DrawCheckboxRow("FarでScript停止", component.simulationLodDisableScriptsAtFar);
		DrawCheckboxRow("FarでAI停止", component.simulationLodDisableAiAtFar);
		DrawCheckboxRow("FarでAnimation停止", component.simulationLodDisableAnimationAtFar);
		DrawCheckboxRow("FarでEffect停止", component.simulationLodDisableEffectsAtFar);
		DrawCheckboxRow("子階層も対象", component.simulationLodAffectHierarchy);
		const char* runtimeLevelNames[] = {"Near", "Medium", "Far", "Culled"};
		const int32_t runtimeLevel = (std::clamp)(component.simulationLodRuntimeLevel, 0, 3);
		DrawTextRow("Runtime LOD", runtimeLevelNames[runtimeLevel]);
	}

	void DrawSceneStreamingComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "基準Objectが範囲へ入るとAdditive Sceneを非同期読込し、離れると破棄します。");
		DrawStringInputRow("Scene Path", component.sceneStreamingScenePath);
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"距離基準",
			component.sceneStreamingReferenceGameObjectId,
			"最高Priority Camera",
			true);
		DrawFloatRow("読込距離", component.sceneStreamingLoadDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("解除距離", component.sceneStreamingUnloadDistance, 1.0f, 0.0f, 1000000.0f);
		component.sceneStreamingUnloadDistance = (std::max)(
			component.sceneStreamingUnloadDistance,
			component.sceneStreamingLoadDistance);
		DrawCheckboxRow("遠距離でSceneを破棄", component.sceneStreamingUnloadWhenFar);
		DrawTextRow(
			"Runtime",
			component.sceneStreamingRuntimePending
				? "処理中"
				: (component.sceneStreamingRuntimeLoaded ? "Loaded" : "Unloaded"));
	}

	void DrawRailEventMarkerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Rail進行率がMarkerを横切ったFrameにMarker IDをString Payloadとして通知します。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Action対象",
			component.railEventMarkerActionTargetGameObjectId,
			"このObject",
			true);
		int32_t removeIndex = -1;

		for (size_t markerIndex = 0u; markerIndex < component.railEventMarkerEntries.size(); markerIndex++) {
			EditorRailEventMarkerEntry& markerEntry = component.railEventMarkerEntries[markerIndex];
			ImGui::PushID(static_cast<int32_t>(markerIndex));
			DrawSubHeader(("Marker " + std::to_string(markerIndex + 1u)).c_str());
			DrawStringInputRow("Marker ID", markerEntry.markerId);
			DrawFloatRow("進行率", markerEntry.normalizedProgress, 0.01f, 0.0f, 1.0f);
			const char* directionItems[] = {"両方向", "順方向のみ", "逆方向のみ"};
			markerEntry.directionMode = (std::clamp)(markerEntry.directionMode, 0, 2);
			DrawComboRow("通過方向", markerEntry.directionMode, directionItems, static_cast<int32_t>(_countof(directionItems)));
			DrawCheckboxRow("Play中1回だけ", markerEntry.triggerOnce);
			DrawStringInputRow("Action", markerEntry.actionName);
			DrawTextRow("Runtime", markerEntry.runtimeTriggered ? "通知済み" : "未通知");

			if (ImGui::Button("このMarkerを削除")) {
				removeIndex = static_cast<int32_t>(markerIndex);
			}

			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.railEventMarkerEntries.erase(
				component.railEventMarkerEntries.begin() + removeIndex);
		}

		if (ImGui::Button("Markerを追加")) {
			EditorRailEventMarkerEntry markerEntry{};
			markerEntry.markerId = "Marker" + std::to_string(component.railEventMarkerEntries.size() + 1u);
			component.railEventMarkerEntries.push_back(markerEntry);
		}

		ImGui::SameLine();

		if (ImGui::Button("進行率順に並べる")) {
			std::sort(
				component.railEventMarkerEntries.begin(),
				component.railEventMarkerEntries.end(),
				[](const EditorRailEventMarkerEntry& firstMarker, const EditorRailEventMarkerEntry& secondMarker) {
					return firstMarker.normalizedProgress < secondMarker.normalizedProgress;
				});
		}
	}

	void DrawHealthComponent(EditorComponent& component) {
		DrawTextRow("説明", "ダメージや耐久値など、ゲーム側が用途を決める汎用の現在値と最大値です。");
		DrawFloatRow("最大体力", component.healthMaximum, 1.0f, 0.0f, 1000000.0f);

		if (component.healthCurrent > component.healthMaximum) {
			component.healthCurrent = component.healthMaximum;
		}

		DrawTextRow("実行中体力", std::to_string(component.healthCurrent).c_str());
	}

	void DrawScreenAimComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "マウスまたはVector2 Actionを0～1の画面照準へ変換し、任意のRectTransformと連動します。");
		const char* inputModes[] = {"Game Viewマウス", "Vector2 Action"};
		component.screenAimInputMode = (std::clamp)(component.screenAimInputMode, 0, 1);
		DrawComboRow("入力方式", component.screenAimInputMode, inputModes, 2);

		if (component.screenAimInputMode == 1) {
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"入力Object",
				component.screenAimInputGameObjectId,
				"このObject",
				true);
			DrawStringInputRow("Action Map", component.screenAimActionMapName);
			DrawStringInputRow("移動Action", component.screenAimActionName);
			DrawFloatRow("移動速度", component.screenAimSpeed, 0.01f, 0.0f, 100.0f);
			DrawCheckboxRow("Y軸反転", component.screenAimInvertY);
		}

		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"照準UI",
			component.screenAimReticleGameObjectId,
			"UIなし",
			false);
		DrawVector2Row("初期画面位置", component.screenAimNormalizedPosition, 0.01f, 0.0f, 1.0f);
		DrawCheckboxRow("画面内に制限", component.screenAimClamp);
	}

	void DrawHitscanWeaponComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "照準Rayで即時命中判定を行います。命中先のDamageReceiverとHealthへ値を渡します。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "画面照準", component.hitscanAimGameObjectId, "画面中央", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "入力Object", component.hitscanInputGameObjectId, "このObject", true);
		DrawStringInputRow("Action Map", component.hitscanActionMapName);
		DrawStringInputRow("発射Action", component.hitscanFireActionName);
		DrawFloatRow("射程", component.hitscanRange, 1.0f, 0.01f, 1000000.0f);
		DrawFloatRow("ダメージ", component.hitscanDamage, 1.0f, 0.0f, 1000000.0f);
		DrawStringInputRow("Damage Tag", component.hitscanDamageTag);
		DrawFloatRow("発射間隔", component.hitscanInterval, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("押下中に連射", component.hitscanAutomatic);
		DrawCheckboxRow("FFT水面へ命中", component.hitscanOceanCollision);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.hitscanActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.hitscanActionTargetGameObjectId, "発射Action通知", component.hitscanFiredActionName);
		DrawScriptActionRow(context, ownerGameObject, component.hitscanActionTargetGameObjectId, "命中Action通知", component.hitscanHitActionName);
		DrawScriptActionRow(context, ownerGameObject, component.hitscanActionTargetGameObjectId, "非命中Action通知", component.hitscanMissActionName);
	}

	void DrawProjectileEmitterComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "ObjectPoolから弾を取得し、毎フレーム連続Castで高速弾のすり抜けを防ぎます。");
		const char* aimModes[] = {"画面照準", "Transform前方", "Target", "弾道予測", "可変速度"};
		component.projectileAimMode = (std::clamp)(component.projectileAimMode, 0, 4);
		DrawComboRow("照準Source", component.projectileAimMode, aimModes, 5);
		DrawGameObjectReferenceRow(context, ownerGameObject, "照準/Selector", component.projectileAimGameObjectId, "画面中央/このObject", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "弾道予測/可変速度Target", component.projectileBallisticPredictionGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "入力Object", component.projectileInputGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "弾ObjectPool", component.projectilePoolGameObjectId, "未設定", false);
		DrawGameObjectReferenceRow(context, ownerGameObject, "発射位置", component.projectileSpawnPointGameObjectId, "このObject", true);
		DrawStringInputRow("Action Map", component.projectileActionMapName);
		DrawStringInputRow("発射Action", component.projectileFireActionName);
		DrawFloatRow("速度", component.projectileSpeed, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("ダメージ", component.projectileDamage, 1.0f, 0.0f, 1000000.0f);
		DrawStringInputRow("Damage Tag", component.projectileDamageTag);
		DrawFloatRow("判定半径", component.projectileRadius, 0.01f, 0.0f, 10000.0f);
		DrawFloatRow("発射位置の安全距離", component.projectileSpawnClearance, 0.05f, 0.0f, 10000.0f);
		DrawFloatRow("寿命", component.projectileLifetime, 0.05f, 0.01f, 3600.0f);
		DrawFloatRow("発射間隔", component.projectileInterval, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("押下中に連射", component.projectileAutomatic);
		DrawCheckboxRow("FFT水面へ命中", component.projectileOceanCollision);
		DrawCheckboxRow("発射元速度を継承", component.projectileInheritSourceVelocity);
		DrawGameObjectReferenceRow(context, ownerGameObject, "速度Source", component.projectileSourceVelocityGameObjectId, "このObject", true);
		DrawCheckboxRow("親Rigidbodyを検索", component.projectileUseParentRigidBody);
		DrawFloatRow("並進速度継承", component.projectileLinearVelocityInheritance, 0.01f, -10.0f, 10.0f);
		DrawFloatRow("角速度継承", component.projectileAngularVelocityInheritance, 0.01f, -10.0f, 10.0f);
		const char* variableSpeedTimeModes[] = {"距離に応じる", "固定時間"};
		component.projectileVariableSpeedTimeMode = (std::clamp)(component.projectileVariableSpeedTimeMode, 0, 1);
		DrawComboRow("可変速度: 時間の決め方", component.projectileVariableSpeedTimeMode, variableSpeedTimeModes, 2);
		DrawFloatRow("可変速度: 最短飛行時間(距離依存時)", component.projectileVariableSpeedMinimumFlightTime, 0.01f, 0.01f, 3600.0f);
		DrawFloatRow("可変速度: 最長飛行時間(距離依存時)", component.projectileVariableSpeedMaximumFlightTime, 0.01f, 0.01f, 3600.0f);
		DrawFloatRow("可変速度: 距離÷この値=飛行時間(距離依存時)", component.projectileVariableSpeedDistanceFactor, 1.0f, 1.0f, 100000.0f);
		DrawFloatRow("可変速度: 固定飛行時間", component.projectileVariableSpeedFixedFlightTime, 0.01f, 0.01f, 3600.0f);
		const char* variableSpeedTrajectoryModes[] = {"物理(初速+重力)", "俯角固定の直線", "位置補間(物理無視)"};
		component.projectileVariableSpeedTrajectoryMode = (std::clamp)(component.projectileVariableSpeedTrajectoryMode, 0, 2);
		DrawComboRow("可変速度: 弾道方式", component.projectileVariableSpeedTrajectoryMode, variableSpeedTrajectoryModes, 3);
		DrawFloatRow("可変速度: 俯角(度)", component.projectileVariableSpeedDepressionAngleDegrees, 0.1f, -89.0f, 89.0f);
		DrawFloatRow("可変速度: 弧の高さ", component.projectileVariableSpeedArcHeight, 0.1f, -10000.0f, 10000.0f);
		DrawCheckboxRow("曳光弾ストレッチ表示", component.projectileTracerStretchEnabled);
		DrawFloatRow("曳光弾: 長さ倍率", component.projectileTracerLengthScale, 0.01f, 0.0f, 100.0f);
		DrawFloatRow("曳光弾: 最小長さ", component.projectileTracerMinimumLength, 0.1f, 0.0f, 10000.0f);
		DrawFloatRow("曳光弾: 太さ", component.projectileTracerThickness, 0.01f, 0.001f, 100.0f);
		DrawCheckboxRow("Hitscanで即ダメージ解決(この弾は演出専用)", component.projectileHitscanResolution);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.projectileActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.projectileActionTargetGameObjectId, "発射Action通知", component.projectileFiredActionName);
		DrawScriptActionRow(context, ownerGameObject, component.projectileActionTargetGameObjectId, "命中Action通知", component.projectileHitActionName);
	}

	void DrawDamageReceiverComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "受け取ったダメージの倍率・無敵時間・死亡時処理だけを担当します。攻撃ルールは持ちません。");
		DrawFloatRow("ダメージ倍率", component.damageMultiplier, 0.01f, 0.0f, 10000.0f);
		DrawFloatRow("無敵時間", component.damageInvulnerabilitySeconds, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("死亡時に無効化", component.damageDeactivateOnDeath);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.damageActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.damageActionTargetGameObjectId, "被弾Action", component.damagedActionName);
		DrawScriptActionRow(context, ownerGameObject, component.damageActionTargetGameObjectId, "死亡Action", component.deathActionName);
	}

	void DrawObjectPoolComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Play開始前にTemplateを複製し、生成と破棄の代わりに貸出・返却します。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "Template", component.objectPoolTemplateGameObjectId, "未設定", false);
		DrawIntRow("遅延生成容量", component.objectPoolInitialSize);
		component.objectPoolInitialSize = (std::max)(component.objectPoolInitialSize, 1);
		DrawCheckboxRow("容量不足時に拡張", component.objectPoolAllowExpand);
		DrawTextRow("実体化", "Play開始時はTemplate 1体だけを保持し、残りは初回貸出時に生成して返却後に再利用します。");
		DrawTextRow("物理Template", "Collider/Rigidbody付きはPhysics開始前の事前生成数だけ使用します。");
	}

	void DrawPrefabSpawnerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "指定ObjectPoolから任意位置へ1体生成します。生成後の行動はScriptまたは別Componentへ委ねます。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "ObjectPool", component.prefabSpawnerPoolGameObjectId, "未設定", false);
		DrawGameObjectReferenceRow(context, ownerGameObject, "生成位置", component.prefabSpawnerPointGameObjectId, "このObject", true);
		const char* spawnModes[] = {"外部命令のみ", "Play開始時", "一定間隔"};
		component.prefabSpawnerMode = (std::clamp)(component.prefabSpawnerMode, 0, 2);
		DrawComboRow("生成方式", component.prefabSpawnerMode, spawnModes, 3);

		if (component.prefabSpawnerMode == 2) {
			DrawFloatRow("生成間隔", component.prefabSpawnerInterval, 0.01f, 0.01f, 3600.0f);
		}

		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.prefabSpawnerActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.prefabSpawnerActionTargetGameObjectId, "生成Action", component.prefabSpawnerSpawnedActionName);
	}

	void DrawCameraBlendComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "現在または指定Cameraから別CameraへGame View姿勢を補間します。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "開始Camera", component.cameraBlendSourceGameObjectId, "現在Camera", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "終了Camera", component.cameraBlendTargetGameObjectId, "未設定", false);
		DrawFloatRow("時間", component.cameraBlendDuration, 0.05f, 0.0f, 3600.0f);
		const char* easingModes[] = {"Linear", "SmoothStep"};
		component.cameraBlendEasing = (std::clamp)(component.cameraBlendEasing, 0, 1);
		DrawComboRow("補間", component.cameraBlendEasing, easingModes, 2);
		DrawCheckboxRow("Play開始時に再生", component.cameraBlendPlayOnStart);
	}

	void DrawCameraShakeComponent(EditorComponent& component) {
		DrawTextRow("説明", "複数の振動を加算できる汎用Camera Shakeです。Script Actionから再生できます。");
		DrawVector3Row("位置振幅", component.cameraShakePositionAmplitude, 0.01f, 0.0f, 10000.0f);
		DrawVector3Row("回転振幅", component.cameraShakeRotationAmplitude, 0.001f, 0.0f, 6.2832f);
		DrawFloatRow("周波数", component.cameraShakeFrequency, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("時間", component.cameraShakeDuration, 0.05f, 0.0f, 3600.0f);
		DrawIntRow("Priority", component.cameraShakePriority);
		DrawCheckboxRow("Play開始時に再生", component.cameraShakePlayOnStart);
	}

	void DrawRailBranchComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "RailFollowerを指定した別Rail Pathへ切り替えます。ゲーム固有条件はScript Action側で組みます。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "RailFollower", component.railBranchFollowerGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "切替先Rail Path", component.railBranchTargetPathGameObjectId, "未設定", false);
		const char* triggerModes[] = {"進行率", "外部命令のみ"};
		component.railBranchTriggerMode = (std::clamp)(component.railBranchTriggerMode, 0, 1);
		DrawComboRow("切替条件", component.railBranchTriggerMode, triggerModes, 2);

		if (component.railBranchTriggerMode == 0) {
			DrawFloatRow("切替進行率", component.railBranchTriggerNormalized, 0.01f, 0.0f, 1.0f);
		}

		DrawCheckboxRow("進行率を維持", component.railBranchPreserveProgress);
		DrawCheckboxRow("一度だけ", component.railBranchTriggerOnce);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.railBranchActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.railBranchActionTargetGameObjectId, "切替Action", component.railBranchActionName);
	}

	void DrawActionSequenceComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "子GameObjectのStepをHierarchy順に実行します。待機、並列、Action、Scene、条件、Signalをゲーム固有コードなしで組み合わせます。");
		DrawCheckboxRow("Play開始時に再生", component.actionSequencePlayOnStart);
		DrawCheckboxRow("ループ", component.actionSequenceLoop);

		if (ImGui::Button("子Stepを追加")) {
			g_pendingActionSequenceStepParentId = ownerGameObject.id;
		}
	}

	void DrawActionSequenceStepComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "親のアクションシーケンスから実行される汎用Stepです。同じ0以上の並列Groupが連続するStepは同時に開始します。");
		const char* stepTypes[] = {"Script Action", "待機", "Active変更", "Scene読込", "条件分岐", "Signal待機"};
		component.actionSequenceStepType = (std::clamp)(component.actionSequenceStepType, 0, 5);
		DrawComboRow("Step種類", component.actionSequenceStepType, stepTypes, 6);
		DrawIntRow("並列Group (-1=順次)", component.actionSequenceParallelGroup);
		component.actionSequenceParallelGroup = (std::max)(component.actionSequenceParallelGroup, -1);

		if (component.actionSequenceStepType == 0) {
			DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.actionSequenceTargetGameObjectId, "親Sequence", true);
			DrawScriptActionRow(context, ownerGameObject, component.actionSequenceTargetGameObjectId, "Action", component.actionSequenceActionName);
		}
		else if (component.actionSequenceStepType == 1) {
			DrawFloatRow("待機秒", component.actionSequenceWaitSeconds, 0.01f, 0.0f, 86400.0f);
		}
		else if (component.actionSequenceStepType == 2) {
			DrawGameObjectReferenceRow(context, ownerGameObject, "対象", component.actionSequenceTargetGameObjectId, "親Sequence", true);
			DrawCheckboxRow("Active", component.actionSequenceActiveValue);
		}
		else if (component.actionSequenceStepType == 3) {
			DrawStringInputRow("Scene Asset", component.actionSequenceScenePath);
			DrawCheckboxRow("Additive読込", component.actionSequenceSceneAdditive);
		}
		else if (component.actionSequenceStepType == 4) {
			DrawGameObjectReferenceRow(context, ownerGameObject, "条件対象", component.actionSequenceTargetGameObjectId, "親Sequence", true);
			const char* conditionModes[] = {"Active", "体力比率", "Rail進行率"};
			const char* compareModes[] = {">=", "<=", ">", "<"};
			component.actionSequenceConditionMode = (std::clamp)(component.actionSequenceConditionMode, 0, 2);
			component.actionSequenceCompareMode = (std::clamp)(component.actionSequenceCompareMode, 0, 3);
			DrawComboRow("条件値", component.actionSequenceConditionMode, conditionModes, 3);
			DrawComboRow("比較", component.actionSequenceCompareMode, compareModes, 4);
			DrawFloatRow("比較値", component.actionSequenceCompareValue, 0.01f, -1000000.0f, 1000000.0f);
			DrawIntRow("true移動先Index", component.actionSequenceTrueStepIndex);
			DrawIntRow("false移動先Index", component.actionSequenceFalseStepIndex);
		}
		else {
			DrawStringInputRow("Signal名", component.actionSequenceActionName);
		}
	}

	void DrawSaveableComponent(EditorComponent& component) {
		DrawTextRow("説明", "登録した状態だけをSave Slotへ保存します。保存対象はゲーム側の用途に合わせて選択できます。");
		DrawStringInputRow("保存Key (空=Object名)", component.saveableKey);
		DrawCheckboxRow("Transform", component.saveableTransform);
		DrawCheckboxRow("Active", component.saveableActive);
		DrawCheckboxRow("Health", component.saveableHealth);
		DrawCheckboxRow("Rigidbody", component.saveableRigidbody);
		DrawCheckboxRow("C++ Script公開値", component.saveableScriptProperties);
	}

	void DrawCheckpointComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "指定Slotを保存または復元する入口です。TriggerやC++ Scriptから任意の条件で呼び出せます。");
		DrawStringInputRow("Slot名", component.checkpointSlotName);
		DrawCheckboxRow("Play開始時に保存", component.checkpointSaveOnStart);
		DrawCheckboxRow("Play開始時に読込", component.checkpointLoadOnStart);
		DrawGameObjectReferenceRow(context, ownerGameObject, "完了Action対象", component.checkpointActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.checkpointActionTargetGameObjectId, "保存完了Action", component.checkpointSavedActionName);
		DrawScriptActionRow(context, ownerGameObject, component.checkpointActionTargetGameObjectId, "読込完了Action", component.checkpointLoadedActionName);
	}

	void DrawWeaponLoadoutComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "子の武器スロットを可変数で管理します。Weaponの発射責務とは分離し、装備・弾薬・Reloadだけを扱います。");
		DrawIntRow("選択Slot", component.weaponLoadoutSelectedSlotIndex);
		component.weaponLoadoutSelectedSlotIndex = (std::max)(component.weaponLoadoutSelectedSlotIndex, 0);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.weaponLoadoutActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.weaponLoadoutActionTargetGameObjectId, "装備変更Action", component.weaponLoadoutChangedActionName);
		DrawScriptActionRow(context, ownerGameObject, component.weaponLoadoutActionTargetGameObjectId, "Reload完了Action", component.weaponLoadoutReloadedActionName);

		if (ImGui::Button("子Weapon Slotを追加")) {
			g_pendingWeaponSlotParentId = ownerGameObject.id;
		}
	}

	void DrawWeaponLoadoutSlotComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "親WeaponLoadoutへ1つのWeaponと弾薬設定を提供します。");
		DrawStringInputRow("Slot名", component.weaponSlotName);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Weapon Object", component.weaponSlotWeaponGameObjectId, "未設定", false);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Visual Object", component.weaponSlotVisualGameObjectId, "なし", true);
		DrawIntRow("現在弾数", component.weaponSlotCurrentAmmo);
		DrawIntRow("予備弾 (-1=無限)", component.weaponSlotReserveAmmo);
		DrawIntRow("最大弾数", component.weaponSlotMaximumAmmo);
		component.weaponSlotCurrentAmmo = (std::max)(component.weaponSlotCurrentAmmo, 0);
		component.weaponSlotReserveAmmo = (std::max)(component.weaponSlotReserveAmmo, -1);
		component.weaponSlotMaximumAmmo = (std::max)(component.weaponSlotMaximumAmmo, 1);
		component.weaponSlotCurrentAmmo = (std::min)(component.weaponSlotCurrentAmmo, component.weaponSlotMaximumAmmo);
		DrawFloatRow("Reload秒", component.weaponSlotReloadSeconds, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("空で自動Reload", component.weaponSlotAutoReload);
	}

	void DrawTargetSelectorComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "距離・角度・遮蔽から汎用Targetを選択します。攻撃やミサイルのルールは持ちません。");
		DrawIntRow("検索Layer (-1=全て)", component.targetSelectorSearchLayer);
		DrawFloatRow("最大距離", component.targetSelectorMaximumDistance, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("最大角度", component.targetSelectorMaximumAngle, 0.1f, 0.0f, 180.0f);
		DrawGameObjectReferenceRow(context, ownerGameObject, "基準Object", component.targetSelectorReferenceGameObjectId, "このObject", true);
		DrawCheckboxRow("遮蔽判定", component.targetSelectorOcclusionCheck);

		if (component.targetSelectorOcclusionCheck) {
			const char* occlusionModes[] = {"なし", "Physics", "Ocean", "Physics + Ocean"};
			component.targetSelectorOcclusionMode = (std::clamp)(component.targetSelectorOcclusionMode, 0, 3);
			DrawComboRow("遮蔽方式", component.targetSelectorOcclusionMode, occlusionModes, 4);

			if ((component.targetSelectorOcclusionMode & 2) != 0) {
				DrawFloatRow("Ocean Clearance", component.targetSelectorOceanClearance, 0.01f, -1000.0f, 1000.0f);
			}
		}

		DrawIntRow("最大候補数", component.targetSelectorMaximumTargets);
		component.targetSelectorMaximumTargets = (std::max)(component.targetSelectorMaximumTargets, 1);
		const char* selectionModes[] = {"最短距離", "照準中心", "低HP", "優先値"};
		component.targetSelectorSelectionMode = (std::clamp)(component.targetSelectorSelectionMode, 0, 3);
		DrawComboRow("選択方式", component.targetSelectorSelectionMode, selectionModes, 4);
		const char* teamFilters[] = {"Target可能な全て", "別Team", "同じTeam", "指定Team"};
		component.targetSelectorTeamFilter = (std::clamp)(component.targetSelectorTeamFilter, 0, 3);
		DrawComboRow("Team Filter", component.targetSelectorTeamFilter, teamFilters, 4);

		if (component.targetSelectorTeamFilter == 3) {
			DrawIntRow("指定Team ID", component.targetSelectorSpecificTeamId);
		}

		DrawCheckboxRow("Neutralを含む", component.targetSelectorIncludeNeutral);
		DrawIntRow("現在Target ID", component.targetSelectorCurrentTargetGameObjectId);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.targetSelectorActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.targetSelectorActionTargetGameObjectId, "取得Action", component.targetSelectorFoundActionName);
		DrawScriptActionRow(context, ownerGameObject, component.targetSelectorActionTargetGameObjectId, "喪失Action", component.targetSelectorLostActionName);
		DrawScriptActionRow(context, ownerGameObject, component.targetSelectorActionTargetGameObjectId, "変更Action", component.targetSelectorChangedActionName);
	}

	void DrawTargetSteeringComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "GameObjectをTargetへ旋回・加速します。Projectile、Drone、敵など用途を限定しません。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "明示Target", component.targetSteeringTargetGameObjectId, "Selectorを使用", true);
		DrawGameObjectReferenceRow(context, ownerGameObject, "TargetSelector", component.targetSteeringSelectorGameObjectId, "このObject", true);
		DrawFloatRow("旋回速度 deg/s", component.targetSteeringTurnSpeed, 1.0f, 0.0f, 100000.0f);
		DrawFloatRow("加速度", component.targetSteeringAcceleration, 0.1f, 0.0f, 100000.0f);
		DrawFloatRow("最大速度", component.targetSteeringMaximumSpeed, 0.1f, 0.0f, 100000.0f);
		DrawFloatRow("開始Delay", component.targetSteeringStartDelay, 0.01f, 0.0f, 3600.0f);
		DrawFloatRow("予測秒", component.targetSteeringPredictionSeconds, 0.01f, 0.0f, 60.0f);
		const char* steeringModes[] = {"Transform", "Rigidbody Force"};
		component.targetSteeringMode = (std::clamp)(component.targetSteeringMode, 0, 1);
		DrawComboRow("移動方式", component.targetSteeringMode, steeringModes, 2);

		DrawTextRow("移動Mode", "Targetからの相対位置で動く基本移動。敵パターンごとにComponentを増やさずここで切り替えます。");
		const char* moveModes[] = {
			"Direct (直進追尾)",
			"ArcApproach (旋回接近)",
			"Parallel (並走)",
			"Chase (後方追跡)",
			"KeepDistance (距離維持)",
			"PlayerRelativeMove (相対移動)",
			"Retreat (離脱)"};
		component.targetSteeringMoveMode = (std::clamp)(component.targetSteeringMoveMode, 0, 6);
		DrawComboRow("移動Mode", component.targetSteeringMoveMode, moveModes, 7);

		if (component.targetSteeringMoveMode == 2 || component.targetSteeringMoveMode == 6) {
			DrawFloatRow("横Offset (右+/左-)", component.targetSteeringSideOffset, 0.1f, -100000.0f, 100000.0f);
			DrawFloatRow("前後Offset", component.targetSteeringForwardOffset, 0.1f, -100000.0f, 100000.0f);
			DrawFloatRow("高さOffset", component.targetSteeringVerticalOffset, 0.1f, -100000.0f, 100000.0f);
		}

		if (component.targetSteeringMoveMode == 3 || component.targetSteeringMoveMode == 4) {
			DrawFloatRow("目標距離", component.targetSteeringTargetDistance, 0.1f, 0.0f, 100000.0f);
		}

		if (component.targetSteeringMoveMode == 4) {
			DrawFloatRow("距離Margin", component.targetSteeringDistanceMargin, 0.1f, 0.0f, 100000.0f);
			DrawFloatRow("横Offset (右+/左-)", component.targetSteeringSideOffset, 0.1f, -100000.0f, 100000.0f);
		}

		if (component.targetSteeringMoveMode == 5) {
			DrawTextRow("相対Offset", "x=右、y=上、z=前。Target基準の開始位置から終了位置へ移動します。横切り・正面通過・離脱に使います。");
			DrawVector3Row("開始Offset", component.targetSteeringStartOffset, 0.1f, -100000.0f, 100000.0f);
			DrawVector3Row("終了Offset", component.targetSteeringEndOffset, 0.1f, -100000.0f, 100000.0f);
		}

		if (component.targetSteeringMoveMode >= 2) {
			DrawFloatRow("相対位置追従速度 (0=最大速度)", component.targetSteeringPositionLerpSpeed, 0.1f, 0.0f, 100000.0f);
		}

		DrawFloatRow("継続秒 (0=無期限)", component.targetSteeringDuration, 0.01f, 0.0f, 3600.0f);
		const char* nextMoveModes[] = {
			"維持",
			"Direct (直進追尾)",
			"ArcApproach (旋回接近)",
			"Parallel (並走)",
			"Chase (後方追跡)",
			"KeepDistance (距離維持)",
			"PlayerRelativeMove (相対移動)",
			"Retreat (離脱)"};
		int32_t nextMoveModeIndex = (std::clamp)(component.targetSteeringNextMoveMode + 1, 0, 7);
		if (DrawComboRow("完了後のMode", nextMoveModeIndex, nextMoveModes, 8)) {
			component.targetSteeringNextMoveMode = nextMoveModeIndex - 1;
		}
		DrawGameObjectReferenceRow(context, ownerGameObject, "完了Action対象", component.targetSteeringActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.targetSteeringActionTargetGameObjectId, "完了Action", component.targetSteeringCompletedActionName);
	}

	void DrawTargetPointComponent(EditorComponent& component) {
		DrawTextRow("説明", "大型Objectの部位、弱点、カメラ注視点としてTargetSelectorへ候補位置を提供します。");
		DrawFloatRow("優先値", component.targetPointPriority, 0.1f, -100000.0f, 100000.0f);
		DrawFloatRow("注視半径", component.targetPointRadius, 0.01f, 0.01f, 100000.0f);
		DrawVector3Row("注視Offset", component.targetPointAimOffset, 0.01f, -100000.0f, 100000.0f);
	}

	void DrawTeamComponent(EditorComponent& component) {
		DrawTextRow("説明", "Target選択で使う汎用所属です。負数はNeutralとして扱います。");
		DrawIntRow("Team ID", component.teamId);
		DrawCheckboxRow("Target可能", component.teamTargetable);
	}

	void DrawTimerComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "一定時間後または一定間隔で任意のC++ Script Actionを通知します。");
		DrawFloatRow("時間", component.timerDuration, 0.01f, 0.001f, 86400.0f);
		DrawCheckboxRow("繰り返す", component.timerRepeat);
		DrawCheckboxRow("Play開始時に再生", component.timerPlayOnStart);
		DrawCheckboxRow("一時停止", component.timerPaused);
		DrawTextRow("残り時間", std::to_string(component.timerRemaining).c_str());
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.timerActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.timerActionTargetGameObjectId, "発火Action", component.timerActionName);
	}

	void DrawGenericStateMachineComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "ゲーム側が定義する任意文字列Stateを保持し、変更時だけActionを通知します。");
		DrawStringInputRow("初期State", component.stateMachineInitialState);
		DrawTextRow("現在State", component.stateMachineCurrentState.c_str());
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.stateMachineActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.stateMachineActionTargetGameObjectId, "変更Action", component.stateMachineChangedActionName);
	}

	void DrawAttributeComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "Boost、Heat、Shield、FuelなどHealth以外の汎用可変値です。");
		DrawStringInputRow("属性名", component.attributeName);
		DrawFloatRow("最小", component.attributeMinimum, 0.1f, -1000000.0f, 1000000.0f);
		DrawFloatRow("最大", component.attributeMaximum, 0.1f, -1000000.0f, 1000000.0f);
		DrawFloatRow("現在", component.attributeCurrent, 0.1f, -1000000.0f, 1000000.0f);
		DrawFloatRow("毎秒回復", component.attributeRegenerationPerSecond, 0.1f, -1000000.0f, 1000000.0f);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.attributeActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.attributeActionTargetGameObjectId, "変更Action", component.attributeChangedActionName);
	}

	void DrawDestructiblePartComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "Healthが0になった時に指定Componentと直下の子Objectを無効化します。");
		DrawGameObjectReferenceRow(context, owner, "Health Source", component.destructibleHealthGameObjectId, "このObject", true);
		DrawStringInputRow("無効化Component (;区切り)", component.destructibleDisableComponentNames);
		DrawCheckboxRow("子Objectを無効化", component.destructibleDisableChildren);
		DrawTextRow("破壊済み", component.destructibleDestroyed ? "true" : "false");
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.destructibleActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.destructibleActionTargetGameObjectId, "破壊Action", component.destructibleDestroyedActionName);
	}

	void DrawFormationFollowerComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "LeaderのローカルOffset位置へ汎用的に追従します。攻撃や敵ルールは持ちません。");
		DrawGameObjectReferenceRow(context, owner, "Leader", component.formationLeaderGameObjectId, "未設定", false);
		DrawVector3Row("ローカルOffset", component.formationLocalOffset, 0.1f, -100000.0f, 100000.0f);
		DrawFloatRow("位置追従速度", component.formationPositionSpeed, 0.1f, 0.0f, 100000.0f);
		DrawFloatRow("回転追従速度 deg/s", component.formationRotationSpeed, 1.0f, 0.0f, 100000.0f);
		DrawCheckboxRow("回転を追従", component.formationFollowRotation);
	}

	void DrawTargetLockComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "TargetSelectorの同一Targetを一定時間維持するとLock完了にします。");
		DrawGameObjectReferenceRow(context, owner, "TargetSelector", component.targetLockSelectorGameObjectId, "このObject", true);
		DrawFloatRow("Lock時間", component.targetLockSeconds, 0.01f, 0.0f, 3600.0f);
		DrawFloatRow("喪失猶予", component.targetLockLostGraceSeconds, 0.01f, 0.0f, 3600.0f);
		DrawTextRow("進行率", std::to_string(component.targetLockProgress).c_str());
		DrawTextRow("Locked", component.targetLockLocked ? "true" : "false");
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.targetLockActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.targetLockActionTargetGameObjectId, "開始Action", component.targetLockStartedActionName);
		DrawScriptActionRow(context, owner, component.targetLockActionTargetGameObjectId, "完了Action", component.targetLockCompletedActionName);
		DrawScriptActionRow(context, owner, component.targetLockActionTargetGameObjectId, "解除Action", component.targetLockLostActionName);
	}

	void DrawMultiTargetLockComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "TargetSelectorの候補を複数Slotへ追加し、TargetごとにLock進行率を保持します。");
		DrawGameObjectReferenceRow(context, owner, "TargetSelector", component.multiTargetLockSelectorGameObjectId, "このObject", true);
		DrawIntRow("最大Lock数", component.multiTargetLockMaximumCount);
		component.multiTargetLockMaximumCount = (std::clamp)(component.multiTargetLockMaximumCount, 1, 64);
		DrawFloatRow("1体のLock時間", component.multiTargetLockSecondsPerTarget, 0.01f, 0.0f, 3600.0f);
		DrawFloatRow("喪失猶予", component.multiTargetLockLostGraceSeconds, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("候補を自動取得", component.multiTargetLockAutoAcquire);
		DrawTextRow("現在Target数", std::to_string(component.multiTargetLockTargetGameObjectIds.size()).c_str());
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.multiTargetLockActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.multiTargetLockActionTargetGameObjectId, "追加Action", component.multiTargetLockAddedActionName);
		DrawScriptActionRow(context, owner, component.multiTargetLockActionTargetGameObjectId, "Lock完了Action", component.multiTargetLockCompletedActionName);
		DrawScriptActionRow(context, owner, component.multiTargetLockActionTargetGameObjectId, "解除Action", component.multiTargetLockLostActionName);
	}

	void DrawTargetMarkerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component,
		bool isOffScreenIndicator) {
		DrawTextRow("説明", isOffScreenIndicator
			? "画面外Targetの方向をCanvas端へ表示します。Imageと同じObjectへ追加してください。"
			: "3D TargetのWorld位置をCanvas座標へ投影します。ImageまたはTextと同じObjectへ追加してください。");
		DrawGameObjectReferenceRow(context, owner, "明示Target", component.targetMarkerTargetGameObjectId, "自動参照", true);
		DrawGameObjectReferenceRow(context, owner, "TargetSelector", component.targetMarkerSelectorGameObjectId, "未使用", true);
		DrawGameObjectReferenceRow(context, owner, "TargetLock", component.targetMarkerLockGameObjectId, "未使用", true);
		if (DrawIntRow("Multi Lock番号", component.targetMarkerMultiLockIndex)) {
			component.targetMarkerMultiLockIndex = (std::clamp)(component.targetMarkerMultiLockIndex, 0, 63);
		}
		DrawVector3Row("World Offset", component.targetMarkerWorldOffset, 0.01f, -100000.0f, 100000.0f);
		DrawVector2Row("Screen Offset", component.targetMarkerScreenOffset, 1.0f, -100000.0f, 100000.0f);
		DrawFloatRow("画面端余白", component.targetMarkerEdgePadding, 1.0f, 0.0f, 1000.0f);
		DrawCheckboxRow("カメラ後方を隠す", component.targetMarkerHideBehindCamera);
		DrawCheckboxRow("Lock完了時だけ表示", component.targetMarkerOnlyWhenLocked);

		if (isOffScreenIndicator) {
			DrawCheckboxRow("Target方向へ回転", component.targetMarkerRotateToDirection);
		}
	}

	void DrawAttributeSetComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "Boost、Heat、Shield等を名前付きResourceとして同一Objectで管理します。");
		int32_t removeIndex = -1;

		for (int32_t entryIndex = 0; entryIndex < static_cast<int32_t>(component.attributeSetEntries.size()); ++entryIndex) {
			EditorNamedAttributeEntry& entry = component.attributeSetEntries[static_cast<size_t>(entryIndex)];
			ImGui::PushID(entryIndex);
			ImGui::SeparatorText(entry.name.empty() ? "属性" : entry.name.c_str());
			DrawStringInputRow("名前", entry.name);
			DrawFloatRow("最小", entry.minimum, 0.1f, -1000000.0f, 1000000.0f);
			DrawFloatRow("最大", entry.maximum, 0.1f, entry.minimum, 1000000.0f);
			entry.current = (std::clamp)(entry.current, entry.minimum, entry.maximum);
			DrawFloatRow("現在", entry.current, 0.1f, entry.minimum, entry.maximum);
			DrawFloatRow("毎秒回復", entry.regenerationPerSecond, 0.1f, -1000000.0f, 1000000.0f);

			if (ImGui::Button("この属性を削除")) removeIndex = entryIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.attributeSetEntries.erase(component.attributeSetEntries.begin() + removeIndex);
		}

		if (ImGui::Button("属性を追加")) {
			component.attributeSetEntries.push_back(EditorNamedAttributeEntry{"Resource", 0.0f, 100.0f, 100.0f, 0.0f});
		}

		DrawGameObjectReferenceRow(context, owner, "Action対象", component.attributeSetActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.attributeSetActionTargetGameObjectId, "変更Action", component.attributeSetChangedActionName);
	}

	void DrawGenericCounterComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "撃破数・残数・部位数・コンボ等の単純な数値と閾値通知を管理します。");
		DrawStringInputRow("名前", component.counterName);
		DrawFloatRow("初期値", component.counterInitialValue, 1.0f, -1000000.0f, 1000000.0f);
		DrawFloatRow("最小", component.counterMinimumValue, 1.0f, -1000000.0f, 1000000.0f);
		DrawFloatRow("最大", component.counterMaximumValue, 1.0f, component.counterMinimumValue, 1000000.0f);
		DrawFloatRow("閾値", component.counterThresholdValue, 1.0f, -1000000.0f, 1000000.0f);
		const char* compareModes[] = {">=", "<=", "==", ">", "<", "!="};
		component.counterCompareMode = (std::clamp)(component.counterCompareMode, 0, 5);
		DrawComboRow("比較", component.counterCompareMode, compareModes, 6);
		DrawCheckboxRow("成立時は1回だけ", component.counterFireOnce);
		DrawTextRow("現在値", std::to_string(component.counterCurrentValue).c_str());
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.counterActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.counterActionTargetGameObjectId, "変更Action", component.counterChangedActionName);
		DrawScriptActionRow(context, owner, component.counterActionTargetGameObjectId, "閾値Action", component.counterThresholdActionName);
	}

	void DrawGenericConditionComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "公開Property・Attribute・Counter・Active・State・Lock状態を比較してActionを通知します。");
		DrawGameObjectReferenceRow(context, owner, "比較元", component.conditionSourceGameObjectId, "このObject", true);
		const char* sourceTypes[] = {"Runtime Float", "Runtime Int", "Runtime Bool", "AttributeSet", "Counter", "Object Active", "Generic State", "Target Locked"};
		component.conditionSourceType = (std::clamp)(component.conditionSourceType, 0, 7);
		DrawComboRow("比較元種類", component.conditionSourceType, sourceTypes, 8);

		if (component.conditionSourceType <= 2) DrawStringInputRow("Component", component.conditionComponentName);
		if (component.conditionSourceType <= 4) DrawStringInputRow("Property / 属性名", component.conditionPropertyName);
		const char* compareModes[] = {">=", "<=", "==", ">", "<", "!="};
		component.conditionCompareMode = (std::clamp)(component.conditionCompareMode, 0, 5);
		DrawComboRow("比較", component.conditionCompareMode, compareModes, 6);

		if (component.conditionSourceType == 6) DrawStringInputRow("比較State", component.conditionCompareString);
		else DrawFloatRow("比較値", component.conditionCompareFloat, 0.1f, -1000000.0f, 1000000.0f);
		DrawCheckboxRow("毎Frame評価", component.conditionEvaluateEveryFrame);
		DrawCheckboxRow("結果変化時だけ通知", component.conditionFireOnChangeOnly);
		DrawTextRow("現在結果", component.conditionLastResult ? "true" : "false");
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.conditionActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.conditionActionTargetGameObjectId, "成立Action", component.conditionTrueActionName);
		DrawScriptActionRow(context, owner, component.conditionActionTargetGameObjectId, "不成立Action", component.conditionFalseActionName);
	}

	void DrawGameplayDataComponent(EditorInspectorPanelContext& context, EditorComponent& component) {
		DrawTextRow("説明", "武器・敵・Upgrade等のゲームプレイ値をKey/Type/Valueで保持する汎用Data Asset参照です。");
		DrawStringInputRow("Data Asset", component.gameplayDataAssetPath);

		if (EditorAssetUtility::HasExtension(context.selectedAssetPath, ".gdata") && ImGui::Button("選択中.gdataを設定")) {
			component.gameplayDataAssetPath = context.selectedAssetPath;
		}

		int32_t removeIndex = -1;

		for (int32_t entryIndex = 0; entryIndex < static_cast<int32_t>(component.gameplayDataEntries.size()); ++entryIndex) {
			EditorGameplayDataEntry& entry = component.gameplayDataEntries[static_cast<size_t>(entryIndex)];
			ImGui::PushID(entryIndex);
			ImGui::SeparatorText(entry.key.empty() ? "Data" : entry.key.c_str());
			DrawStringInputRow("Key", entry.key);
			const char* dataTypes[] = {"String", "Int", "Float", "Bool", "Asset Path"};
			entry.type = (std::clamp)(entry.type, 0, 4);
			DrawComboRow("型", entry.type, dataTypes, 5);
			DrawStringInputRow("値", entry.value);
			if (ImGui::Button("この項目を削除")) removeIndex = entryIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.gameplayDataEntries.erase(component.gameplayDataEntries.begin() + removeIndex);
		if (ImGui::Button("Data項目を追加")) component.gameplayDataEntries.push_back(EditorGameplayDataEntry{"Key", 0, "Value"});
	}

	void DrawAreaDamageComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "球範囲内のHealthへ距離減衰Damageを送り、必要なら同じ減衰でImpulseを与えます。");
		DrawFloatRow("半径", component.areaDamageRadius, 0.1f, 0.01f, 100000.0f);
		DrawFloatRow("基礎Damage", component.areaDamageBaseDamage, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("端の最低倍率", component.areaDamageMinimumMultiplier, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Impulse", component.areaDamageImpulse, 1.0f, -1000000.0f, 1000000.0f);
		const char* falloffModes[] = {"一定", "線形", "SmoothStep"};
		component.areaDamageFalloffMode = (std::clamp)(component.areaDamageFalloffMode, 0, 2);
		DrawComboRow("距離減衰", component.areaDamageFalloffMode, falloffModes, 3);
		DrawIntRow("Layer Mask", component.areaDamageLayerMask);
		DrawStringInputRow("Damage Tag", component.areaDamageTag);
		DrawCheckboxRow("発生元を除外", component.areaDamageIgnoreOwner);
		const char* occlusionModes[] = {"なし", "Physics", "Physics + Ocean"};
		component.areaDamageOcclusionMode = (std::clamp)(component.areaDamageOcclusionMode, 0, 2);
		DrawComboRow("遮蔽判定", component.areaDamageOcclusionMode, occlusionModes, 3);
		DrawIntRow("遮蔽Layer Mask", component.areaDamageOcclusionLayerMask);
		DrawFloatRow("遮蔽時倍率", component.areaDamageBlockedMultiplier, 0.01f, 0.0f, 1.0f);
		DrawIntRow("遮蔽Sample数", component.areaDamageOcclusionSamplePoints);
		component.areaDamageOcclusionSamplePoints = (std::clamp)(component.areaDamageOcclusionSamplePoints, 1, 9);
		const char* teamRules[] = {"すべて", "異なるTeamのみ", "同じTeamのみ"};
		component.areaDamageTeamRule = (std::clamp)(component.areaDamageTeamRule, 0, 2);
		DrawComboRow("Teamルール", component.areaDamageTeamRule, teamRules, 3);
		DrawCheckboxRow("Neutralを無視", component.areaDamageIgnoreNeutral);
		DrawGameObjectReferenceRow(context, owner, "Team Source", component.areaDamageTeamSourceGameObjectId, "Instigator", true);
		DrawCheckboxRow("Play開始時に実行", component.areaDamagePlayOnStart);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.areaDamageActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.areaDamageActionTargetGameObjectId, "適用Action", component.areaDamageAppliedActionName);
	}

	void DrawHitZoneComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "このColliderへの命中を指定Healthへ転送し、艦橋・装甲・エンジン等の部位倍率を適用します。");
		DrawGameObjectReferenceRow(context, owner, "Health対象", component.hitZoneHealthGameObjectId, "このObject", true);
		DrawFloatRow("部位Damage倍率", component.hitZoneDamageMultiplier, 0.05f, 0.0f, 1000.0f);
	}

	void DrawDamageTagModifierComponent(EditorComponent& component) {
		DrawTextRow("説明", "文字列Damage Tagごとの耐性・弱点倍率を管理します。固定Enumではありません。");
		DrawFloatRow("未登録Tag倍率", component.damageTagDefaultMultiplier, 0.05f, 0.0f, 1000.0f);
		int32_t removeIndex = -1;

		for (int32_t entryIndex = 0; entryIndex < static_cast<int32_t>(component.damageTagModifierEntries.size()); ++entryIndex) {
			EditorDamageTagModifierEntry& entry = component.damageTagModifierEntries[static_cast<size_t>(entryIndex)];
			ImGui::PushID(entryIndex);
			ImGui::SeparatorText(entry.tagName.empty() ? "Damage Tag" : entry.tagName.c_str());
			DrawStringInputRow("Tag", entry.tagName);
			DrawFloatRow("倍率", entry.multiplier, 0.05f, 0.0f, 1000.0f);
			if (ImGui::Button("このTagを削除")) removeIndex = entryIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.damageTagModifierEntries.erase(component.damageTagModifierEntries.begin() + removeIndex);
		if (ImGui::Button("Damage Tagを追加")) component.damageTagModifierEntries.push_back(EditorDamageTagModifierEntry{"Explosion", 1.0f});
	}

	void DrawProjectileDetonatorComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "ProjectileEmitterのPool Itemへ接触・近接・寿命・手動起爆を追加します。AreaDamageと組み合わせます。");
		DrawCheckboxRow("接触時起爆", component.projectileDetonateOnContact);
		DrawCheckboxRow("近接時起爆", component.projectileDetonateOnProximity);
		DrawCheckboxRow("寿命切れ時起爆", component.projectileDetonateOnLifetime);
		DrawGameObjectReferenceRow(context, owner, "近接Target", component.projectileDetonatorTargetGameObjectId, "TargetSteering", true);
		DrawFloatRow("近接半径", component.projectileDetonatorProximityRadius, 0.1f, 0.0f, 100000.0f);
		DrawGameObjectReferenceRow(context, owner, "AreaDamage", component.projectileDetonatorAreaDamageGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.projectileDetonatorActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.projectileDetonatorActionTargetGameObjectId, "起爆Action", component.projectileDetonatedActionName);
	}

	void DrawThreatTrackerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "対象へ接近中のProjectileを距離・接近速度・最接近予測時間付きで収集します。");
		DrawGameObjectReferenceRow(context, owner, "監視対象", component.threatTrackerTargetGameObjectId, "このObject", true);
		DrawFloatRow("最大距離", component.threatTrackerMaximumDistance, 1.0f, 0.0f, 1000000.0f);
		DrawFloatRow("最低接近速度", component.threatTrackerMinimumClosingSpeed, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("最大逸れ距離", component.threatTrackerMaximumMissDistance, 0.1f, 0.0f, 1000000.0f);
		DrawIntRow("最大脅威数", component.threatTrackerMaximumCount);
		component.threatTrackerMaximumCount = (std::clamp)(component.threatTrackerMaximumCount, 1, 64);
		DrawTextRow("現在脅威数", std::to_string(component.threatTrackerEntries.size()).c_str());
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.threatTrackerActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.threatTrackerActionTargetGameObjectId, "追加Action", component.threatTrackerAddedActionName);
		DrawScriptActionRow(context, owner, component.threatTrackerActionTargetGameObjectId, "解除Action", component.threatTrackerLostActionName);
	}

	void DrawRuntimeStateResetComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "ObjectPool再貸出時に複数ComponentのRuntime状態を共通契約で初期化します。");
		DrawCheckboxRow("Health", component.runtimeResetHealth);
		DrawCheckboxRow("State Machine", component.runtimeResetStateMachine);
		DrawCheckboxRow("Attribute / Counter", component.runtimeResetAttributes);
		DrawCheckboxRow("Target Lock", component.runtimeResetLocks);
		DrawCheckboxRow("Timer", component.runtimeResetTimers);
		DrawCheckboxRow("破壊可能部位", component.runtimeResetDestructibleParts);
		DrawCheckboxRow("Cooldown", component.runtimeResetCooldowns);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.runtimeResetActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.runtimeResetActionTargetGameObjectId, "Reset Action", component.runtimeResetActionName);
	}

	void DrawCooldownSetComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "Boost・特殊兵器・回避等の独立Cooldownを子GameObjectなしで名前付き管理します。");
		int32_t removeIndex = -1;

		for (int32_t entryIndex = 0; entryIndex < static_cast<int32_t>(component.cooldownSetEntries.size()); ++entryIndex) {
			EditorCooldownEntry& entry = component.cooldownSetEntries[static_cast<size_t>(entryIndex)];
			ImGui::PushID(entryIndex);
			ImGui::SeparatorText(entry.name.empty() ? "Cooldown" : entry.name.c_str());
			DrawStringInputRow("名前", entry.name);
			DrawFloatRow("時間", entry.duration, 0.05f, 0.0f, 36000.0f);
			DrawCheckboxRow("開始時使用可能", entry.startReady);
			DrawTextRow("残り", std::to_string(entry.remaining).c_str());
			if (ImGui::Button("このCooldownを削除")) removeIndex = entryIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.cooldownSetEntries.erase(component.cooldownSetEntries.begin() + removeIndex);
		if (ImGui::Button("Cooldownを追加")) component.cooldownSetEntries.push_back(EditorCooldownEntry{"Ability", 1.0f, 0.0f, true, false});
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.cooldownSetActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.cooldownSetActionTargetGameObjectId, "完了Action", component.cooldownSetCompletedActionName);
	}

	void DrawWeaponFirePatternComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "1回の発射要求から単発・Burst・Salvo・Spread・Sequence・Chargeの実弾列を作ります。");
		const char* modes[] = {"単発", "バースト", "斉射", "扇状拡散", "発射点順番", "チャージ"};
		component.weaponFirePatternMode = (std::clamp)(component.weaponFirePatternMode, 0, 5);
		DrawComboRow("モード", component.weaponFirePatternMode, modes, 6);
		DrawIntRow("発射数", component.weaponFirePatternCount);
		component.weaponFirePatternCount = (std::clamp)(component.weaponFirePatternCount, 1, 128);
		DrawFloatRow("発射間隔", component.weaponFirePatternInterval, 0.01f, 0.0f, 60.0f);
		DrawFloatRow("扇状角度 deg", component.weaponFirePatternSpreadAngle, 0.1f, 0.0f, 360.0f);
		DrawFloatRow("チャージ秒", component.weaponFirePatternChargeSeconds, 0.05f, 0.0f, 60.0f);
		int32_t removeIndex = -1;

		for (int32_t pointIndex = 0; pointIndex < static_cast<int32_t>(component.weaponFirePatternSpawnPointGameObjectIds.size()); ++pointIndex) {
			ImGui::PushID(pointIndex);
			DrawGameObjectReferenceRow(
				context, owner, "発射点", component.weaponFirePatternSpawnPointGameObjectIds[static_cast<size_t>(pointIndex)], "未設定", true);
			if (ImGui::Button("この発射点を削除")) removeIndex = pointIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.weaponFirePatternSpawnPointGameObjectIds.erase(
				component.weaponFirePatternSpawnPointGameObjectIds.begin() + removeIndex);
		}
		if (ImGui::Button("発射点を追加")) component.weaponFirePatternSpawnPointGameObjectIds.push_back(-1);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.weaponFirePatternActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.weaponFirePatternActionTargetGameObjectId, "完了Action", component.weaponFirePatternCompletedActionName);
	}

	void DrawTargetAssignmentComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "MultiTargetLockの各Targetへ別Projectileを割り当て、指定間隔で斉射します。");
		DrawGameObjectReferenceRow(context, owner, "複数Target Lock", component.targetAssignmentMultiTargetLockGameObjectId, "このObject", true);
		DrawIntRow("最大Target数", component.targetAssignmentMaximumTargets);
		component.targetAssignmentMaximumTargets = (std::clamp)(component.targetAssignmentMaximumTargets, 1, 64);
		DrawFloatRow("発射間隔", component.targetAssignmentInterval, 0.01f, 0.0f, 60.0f);
		DrawCheckboxRow("Lock完了Targetのみ", component.targetAssignmentLockedOnly);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.targetAssignmentActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.targetAssignmentActionTargetGameObjectId, "完了Action", component.targetAssignmentCompletedActionName);
	}

	void DrawWeaponAccuracyComponent(EditorComponent& component) {
		DrawTextRow("説明", "連射で増える拡散と時間回復を管理し、HitscanとProjectileへ同じ精度モデルを適用します。");
		DrawFloatRow("基礎Spread deg", component.weaponAccuracyBaseSpread, 0.05f, 0.0f, 180.0f);
		DrawFloatRow("最大Spread deg", component.weaponAccuracyMaximumSpread, 0.05f, 0.0f, 180.0f);
		DrawFloatRow("1発の増加 deg", component.weaponAccuracySpreadPerShot, 0.05f, 0.0f, 180.0f);
		DrawFloatRow("毎秒回復 deg", component.weaponAccuracyRecoveryPerSecond, 0.05f, 0.0f, 1000.0f);
		DrawFloatRow("移動Spread倍率", component.weaponAccuracyMovementSpread, 0.05f, 0.0f, 1000.0f);
		const char* distributions[] = {"一様Cone", "一様Disk", "中心寄り"};
		component.weaponAccuracyDistribution = (std::clamp)(component.weaponAccuracyDistribution, 0, 2);
		DrawComboRow("分布", component.weaponAccuracyDistribution, distributions, 3);
		DrawTextRow("現在Spread", std::to_string(component.weaponAccuracyCurrentSpread).c_str());
	}

	void DrawWeaponRecoilComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "発射時にRigidbody Impulse/Torque、表示反動、Camera Shake、Actionを同時実行します。");
		DrawVector3Row("Body Impulse", component.weaponRecoilBodyImpulse, 0.05f, -100000.0f, 100000.0f);
		DrawVector3Row("Body Torque", component.weaponRecoilBodyTorque, 0.05f, -100000.0f, 100000.0f);
		DrawGameObjectReferenceRow(context, owner, "表示反動対象", component.weaponRecoilVisualGameObjectId, "なし", true);
		DrawVector3Row("表示位置反動", component.weaponRecoilVisualPosition, 0.01f, -1000.0f, 1000.0f);
		DrawVector3Row("表示回転反動 rad", component.weaponRecoilVisualRotation, 0.01f, -100.0f, 100.0f);
		DrawFloatRow("回復速度", component.weaponRecoilRecoveryPerSecond, 0.1f, 0.0f, 1000.0f);
		DrawGameObjectReferenceRow(context, owner, "Camera Shake", component.weaponRecoilCameraShakeGameObjectId, "なし", true);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.weaponRecoilActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.weaponRecoilActionTargetGameObjectId, "反動Action", component.weaponRecoilActionName);
	}

	void DrawImpactResponderComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "Damage Tagと命中Surface TagでEffect・Sound・Decal・Camera Shake・Actionを選びます。空Tagは全一致です。");
		int32_t removeIndex = -1;

		for (int32_t entryIndex = 0; entryIndex < static_cast<int32_t>(component.impactResponseEntries.size()); ++entryIndex) {
			EditorImpactResponseEntry& entry = component.impactResponseEntries[static_cast<size_t>(entryIndex)];
			ImGui::PushID(entryIndex);
			ImGui::SeparatorText(entry.surfaceTag.empty() ? "Impact Response" : entry.surfaceTag.c_str());
			DrawStringInputRow("Damage Tag", entry.damageTag);
			DrawStringInputRow("Surface Tag", entry.surfaceTag);
			DrawStringInputRow("Effect Asset", entry.effectAssetPath);
			DrawGameObjectReferenceRow(context, owner, "Audio Source", entry.audioSourceGameObjectId, "なし", true);
			DrawGameObjectReferenceRow(context, owner, "Decal", entry.decalGameObjectId, "なし", true);
			DrawGameObjectReferenceRow(context, owner, "Camera Shake", entry.cameraShakeGameObjectId, "なし", true);
			DrawGameObjectReferenceRow(context, owner, "Action対象", entry.actionTargetGameObjectId, "このObject", true);
			DrawScriptActionRow(context, owner, entry.actionTargetGameObjectId, "命中Action", entry.actionName);
			if (ImGui::Button("この応答を削除")) removeIndex = entryIndex;
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.impactResponseEntries.erase(component.impactResponseEntries.begin() + removeIndex);
		if (ImGui::Button("命中応答を追加")) component.impactResponseEntries.push_back(EditorImpactResponseEntry{});
	}

	void DrawSurfaceTypeComponent(EditorComponent& component) {
		DrawTextRow("説明", "命中材質を固定Enumではない文字列Tagで公開します。Collider子に無ければ親を検索します。");
		DrawStringInputRow("Surface Tag", component.surfaceTypeTag);
	}

	void DrawTimeScaleComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "Runtime全体の時間倍率を一時変更します。0でHitStop、1で通常速度です。");
		DrawFloatRow("時間倍率", component.timeScaleValue, 0.01f, 0.0f, 8.0f);
		DrawFloatRow("継続秒（実時間）", component.timeScaleDuration, 0.01f, 0.0f, 3600.0f);
		DrawFloatRow("Blend秒（実時間）", component.timeScaleBlendSeconds, 0.01f, 0.0f, 60.0f);
		DrawCheckboxRow("Play開始時に実行", component.timeScalePlayOnStart);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.timeScaleActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.timeScaleActionTargetGameObjectId, "完了Action", component.timeScaleCompletedActionName);
	}

	void DrawAimAssistComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "入力後のScreenAimをTargetSelectorの対象へ範囲内だけ滑らかに補正します。");
		DrawGameObjectReferenceRow(context, owner, "画面照準", component.aimAssistScreenAimGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, owner, "Target Selector", component.aimAssistTargetSelectorGameObjectId, "このObject", true);
		DrawFloatRow("補助半径", component.aimAssistRadius, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("補助強度", component.aimAssistStrength, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("追従速度", component.aimAssistFollowSpeed, 0.1f, 0.0f, 100.0f);
		DrawFloatRow("入力中の抑制", component.aimAssistInputSuppression, 0.01f, 0.0f, 1.0f);
	}

	void DrawInterceptPredictionComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "発射元、Target速度、弾速から非誘導弾の迎撃位置を解析計算します。");
		DrawGameObjectReferenceRow(context, owner, "明示Target", component.interceptTargetGameObjectId, "なし", true);
		DrawGameObjectReferenceRow(context, owner, "Target Selector", component.interceptTargetSelectorGameObjectId, "このObject", true);
		DrawFloatRow("Projectile速度", component.interceptProjectileSpeed, 0.1f, 0.01f, 100000.0f);
		DrawFloatRow("最大予測秒", component.interceptMaximumTime, 0.1f, 0.01f, 3600.0f);
		DrawTextRow("Runtime", component.interceptValid ? "有効" : "解なし");
	}

	void DrawDamageDirectionIndicatorComponent(EditorComponent& component) {
		DrawTextRow("説明", "最後のDamage Source方向を画面端HUD用の正規化方向として保持します。");
		DrawFloatRow("表示秒", component.damageDirectionDuration, 0.05f, 0.0f, 60.0f);
		DrawFloatRow("Fade秒", component.damageDirectionFadeSeconds, 0.05f, 0.0f, 60.0f);
		DrawFloatRow("最低Damage", component.damageDirectionMinimumDamage, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("画面端半径", component.damageDirectionEdgeRadius, 0.01f, 0.0f, 1.0f);
		DrawTextRow("残り秒", std::to_string(component.damageDirectionRemaining).c_str());
	}

	void DrawObjectiveTrackerComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "任意IDのObjective状態と進行値だけを管理します。達成条件の意味は持ちません。");
		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(component.objectiveEntries.size()); ++index) {
			EditorObjectiveEntry& entry = component.objectiveEntries[static_cast<size_t>(index)]; ImGui::PushID(index); ImGui::SeparatorText("Objective");
			DrawStringInputRow("ID", entry.objectiveId); DrawStringInputRow("表示名", entry.displayName);
			const char* states[] = {"無効", "進行中", "完了", "失敗"}; DrawComboRow("状態", entry.state, states, 4);
			DrawFloatRow("現在値", entry.currentValue, 0.1f, -1000000.0f, 1000000.0f);
			DrawFloatRow("目標値", entry.targetValue, 0.1f, -1000000.0f, 1000000.0f);
			if (ImGui::Button("このObjectiveを削除")) removeIndex = index; ImGui::PopID();
		}
		if (removeIndex >= 0) component.objectiveEntries.erase(component.objectiveEntries.begin() + removeIndex);
		if (ImGui::Button("Objectiveを追加")) component.objectiveEntries.push_back(EditorObjectiveEntry{});
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.objectiveActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.objectiveActionTargetGameObjectId, "変更Action", component.objectiveChangedActionName);
	}

	void DrawEncounterControllerComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "複数WaveSpawnerを待機時間と完了方式で順番に開始します。"); DrawCheckboxRow("Play開始時に実行", component.encounterPlayOnStart);
		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(component.encounterWaveEntries.size()); ++index) {
			EditorEncounterWaveEntry& entry = component.encounterWaveEntries[static_cast<size_t>(index)]; ImGui::PushID(index);
			DrawGameObjectReferenceRow(context, owner, "Wave Spawner", entry.waveSpawnerGameObjectId, "未設定", true);
			DrawFloatRow("開始前待機", entry.startDelay, 0.05f, 0.0f, 3600.0f); DrawCheckboxRow("全撃破を待つ", entry.waitsForAllDefeated);
			if (ImGui::Button("このWaveを削除")) removeIndex = index; ImGui::PopID();
		}
		if (removeIndex >= 0) component.encounterWaveEntries.erase(component.encounterWaveEntries.begin() + removeIndex);
		if (ImGui::Button("Waveを追加")) component.encounterWaveEntries.push_back(EditorEncounterWaveEntry{});
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.encounterActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.encounterActionTargetGameObjectId, "完了Action", component.encounterCompletedActionName);
	}

	void DrawSpawnPointSetComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		const char* modes[] = {"順番", "ランダム", "重み付き", "Volume"}; DrawComboRow("選択方法", component.spawnPointSetMode, modes, 4);
		DrawVector3Row("Volume Size", component.spawnPointVolumeSize, 0.1f, 0.0f, 100000.0f); DrawCheckboxRow("直前を避ける", component.spawnPointAvoidImmediateRepeat);
		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(component.spawnPointSetEntries.size()); ++index) {
			EditorSpawnPointEntry& entry = component.spawnPointSetEntries[static_cast<size_t>(index)]; ImGui::PushID(index);
			DrawGameObjectReferenceRow(context, owner, "生成地点", entry.gameObjectId, "未設定", true); DrawFloatRow("重み", entry.weight, 0.1f, 0.0f, 100000.0f);
			if (ImGui::Button("この地点を削除")) removeIndex = index; ImGui::PopID();
		}
		if (removeIndex >= 0) component.spawnPointSetEntries.erase(component.spawnPointSetEntries.begin() + removeIndex);
		if (ImGui::Button("生成地点を追加")) component.spawnPointSetEntries.push_back(EditorSpawnPointEntry{});
	}

	void DrawDifficultyParameterSetComponent(EditorInspectorPanelContext& context, const EditorGameObject& owner, EditorComponent& component) {
		DrawTextRow("説明", "選択難易度に一致するRuntime Property Overrideを一括適用します。");
		DrawIntRow("選択Index", component.difficultySelectedIndex);
		DrawCheckboxRow("Play開始時に適用", component.difficultyApplyOnStart);
		int32_t removeDifficultyIndex = -1;

		for (int32_t index = 0; index < static_cast<int32_t>(component.difficultyNames.size()); index++) {
			ImGui::PushID(index);
			DrawStringInputRow("難易度名", component.difficultyNames[static_cast<size_t>(index)]);

			if (component.difficultyNames.size() > 1u && ImGui::Button("この難易度を削除")) {
				removeDifficultyIndex = index;
			}

			ImGui::PopID();
		}

		if (removeDifficultyIndex >= 0) {
			component.difficultyNames.erase(
				component.difficultyNames.begin() + removeDifficultyIndex);
			component.difficultySelectedIndex = (std::clamp)(
				component.difficultySelectedIndex,
				0,
				static_cast<int32_t>(component.difficultyNames.size()) - 1);
		}

		if (ImGui::Button("難易度を追加")) {
			component.difficultyNames.push_back("New Difficulty");
		}

		int32_t removeOverrideIndex = -1;

		for (int32_t index = 0; index < static_cast<int32_t>(component.difficultyOverrides.size()); ++index) {
			EditorDifficultyOverrideEntry& entry = component.difficultyOverrides[static_cast<size_t>(index)];
			ImGui::PushID(1000 + index);
			ImGui::SeparatorText("Override");
			DrawIntRow("難易度Index", entry.difficultyIndex);
			DrawGameObjectReferenceRow(context, owner, "対象", entry.targetGameObjectId, "このObject", true);
			DrawStringInputRow("Component", entry.componentName);
			DrawStringInputRow("Property", entry.propertyName);
			const char* types[] = {"Float", "Int", "Bool"};
			DrawComboRow("型", entry.valueType, types, 3);

			if (entry.valueType == 0) {
				DrawFloatRow("値", entry.floatValue, 0.1f, -1000000.0f, 1000000.0f);
			}
			else if (entry.valueType == 1) {
				DrawIntRow("値", entry.intValue);
			}
			else {
				DrawCheckboxRow("値", entry.boolValue);
			}

			if (ImGui::Button("このOverrideを削除")) {
				removeOverrideIndex = index;
			}

			ImGui::PopID();
		}

		if (removeOverrideIndex >= 0) {
			component.difficultyOverrides.erase(
				component.difficultyOverrides.begin() + removeOverrideIndex);
		}

		if (ImGui::Button("Overrideを追加")) {
			component.difficultyOverrides.push_back(EditorDifficultyOverrideEntry{});
		}

		DrawGameObjectReferenceRow(
			context,
			owner,
			"Action対象",
			component.difficultyActionTargetGameObjectId,
			"このObject",
			true);
		DrawScriptActionRow(
			context,
			owner,
			component.difficultyActionTargetGameObjectId,
			"適用Action",
			component.difficultyAppliedActionName);
	}

	void DrawCameraFeedbackMixerComponent(EditorComponent& component) {
		DrawVector3Row("最大位置振幅", component.cameraFeedbackMaximumPosition, 0.01f, 0.0f, 1000.0f); DrawVector3Row("最大回転振幅", component.cameraFeedbackMaximumRotation, 0.01f, 0.0f, 100.0f);
		DrawIntRow("最大同時数", component.cameraFeedbackMaximumConcurrent); const char* modes[] = {"加算", "最高Priority"}; DrawComboRow("合成", component.cameraFeedbackMixMode, modes, 2); DrawFloatRow("全体強度", component.cameraFeedbackGlobalStrength, 0.01f, 0.0f, 10.0f);
	}

	void DrawBallisticPredictionComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "重力、線形Drag、Target加速度を含む弾道を数値積分し、発射方向と軌道点を公開します。");
		DrawGameObjectReferenceRow(context, owner, "明示Target", component.ballisticTargetGameObjectId, "なし", true);
		DrawGameObjectReferenceRow(context, owner, "Target Selector", component.ballisticTargetSelectorGameObjectId, "このObject", true);
		DrawFloatRow("初速", component.ballisticInitialSpeed, 0.1f, 0.01f, 100000.0f);
		DrawVector3Row("重力", component.ballisticGravity, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("線形Drag", component.ballisticDrag, 0.001f, 0.0f, 1000.0f);
		DrawVector3Row("Target加速度", component.ballisticTargetAcceleration, 0.01f, -10000.0f, 10000.0f);
		DrawFloatRow("最大飛翔秒", component.ballisticMaximumTime, 0.05f, 0.01f, 3600.0f);
		DrawFloatRow("積分Step", component.ballisticSimulationStep, 0.001f, 0.001f, 0.25f);
		DrawIntRow("最大Point数", component.ballisticMaximumPoints);
		DrawCheckboxRow("発射元速度を継承", component.ballisticInheritSourceVelocity);
		DrawGameObjectReferenceRow(context, owner, "速度Source", component.ballisticSourceVelocityGameObjectId, "このObject", true);
		DrawCheckboxRow("親Rigidbodyを検索", component.ballisticUseParentRigidBody);
		DrawFloatRow("並進速度継承", component.ballisticLinearVelocityInheritance, 0.01f, -10.0f, 10.0f);
		DrawFloatRow("角速度継承", component.ballisticAngularVelocityInheritance, 0.01f, -10.0f, 10.0f);
		DrawTextRow("Runtime", component.ballisticValid ? "解あり" : "解なし");
		DrawVector3Row("発射元World速度", component.ballisticSourceVelocity, 0.0f, -1000000.0f, 1000000.0f);
		DrawVector3Row("初期World速度", component.ballisticLaunchVelocity, 0.0f, -1000000.0f, 1000000.0f);
		DrawTextRow("飛翔秒", std::to_string(component.ballisticFlightTime).c_str());
	}

	void DrawDamageEventBufferComponent(EditorComponent& component) {
		DrawTextRow("説明", "複数Sourceからの被弾を寿命付きで保持し、HUDが同時方向表示できるようにします。");
		DrawIntRow("最大Entry数", component.damageEventMaximumEntries);
		DrawFloatRow("表示寿命", component.damageEventLifetime, 0.05f, 0.01f, 60.0f);
		DrawFloatRow("最低Damage", component.damageEventMinimumDamage, 0.1f, 0.0f, 1000000.0f);
		DrawCheckboxRow("同じSourceを統合", component.damageEventMergeSameSource);
		DrawTextRow("Runtime Entry数", std::to_string(component.damageEventEntries.size()).c_str());
	}

	void DrawGamePauseComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "永続Pauseとしてゲーム時間、物理、Audio、Input Mapをまとめて切り替えます。");
		DrawCheckboxRow("ゲーム時間を停止", component.gamePausePauseGameTime);
		DrawCheckboxRow("物理を停止", component.gamePausePausePhysics);
		DrawCheckboxRow("Audioを停止", component.gamePausePauseAudio);
		DrawStringInputRow("Gameplay Input Map", component.gamePauseGameplayInputMap);
		DrawStringInputRow("UI Input Map", component.gamePauseUiInputMap);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.gamePauseActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.gamePauseActionTargetGameObjectId, "Pause Action", component.gamePausePausedActionName);
		DrawScriptActionRow(context, owner, component.gamePauseActionTargetGameObjectId, "Resume Action", component.gamePauseResumedActionName);
		DrawTextRow("Runtime", component.gamePausePaused ? "一時停止中" : "実行中");
	}

	void DrawSurfaceWakeEmitterComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "船速と同じOcean Sampleから航跡Effectと、描画・浮力で共有する局所波を駆動します。");
		DrawGameObjectReferenceRow(context, owner, "Ocean", component.surfaceWakeOceanGameObjectId, "自動検索", true);
		DrawGameObjectReferenceRow(context, owner, "左航跡Effect", component.surfaceWakeLeftEffectGameObjectId, "未設定", true);
		DrawGameObjectReferenceRow(context, owner, "右航跡Effect", component.surfaceWakeRightEffectGameObjectId, "未設定", true);
		DrawGameObjectReferenceRow(context, owner, "船首Spray Effect", component.surfaceWakeBowEffectGameObjectId, "未設定", true);
		DrawFloatRow("最低速度", component.surfaceWakeMinimumSpeed, 0.05f, 0.0f, 10000.0f);
		DrawFloatRow("最大強度速度", component.surfaceWakeMaximumSpeed, 0.1f, 0.01f, 10000.0f);
		DrawFloatRow("航跡幅", component.surfaceWakeWidth, 0.05f, 0.01f, 1000.0f);
		DrawFloatRow("Foam寿命", component.surfaceWakeLifetime, 0.05f, 0.01f, 120.0f);
		DrawFloatRow("最大発生数/秒", component.surfaceWakeMaximumEmissionRate, 1.0f, 0.0f, 100000.0f);
		DrawCheckboxRow("水面へ局所波を与える", component.surfaceWakeAffectOceanSurface);
		DrawFloatRow("局所波の強度", component.surfaceWakeWaveAmplitudeScale, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("局所波の影響半径", component.surfaceWakeWaveRadiusScale, 0.1f, 0.5f, 20.0f);
		DrawTextRow("Runtime速度", std::to_string(component.surfaceWakeCurrentSpeed).c_str());
		DrawTextRow("Runtime強度", std::to_string(component.surfaceWakeCurrentIntensity).c_str());
	}

	void DrawTrajectoryRendererComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "弾道計算とは分離し、BallisticPredictionのPoint列だけをScene/Game Viewへ描画します。");
		DrawGameObjectReferenceRow(context, owner, "弾道予測", component.trajectoryPredictionGameObjectId, "このObject", true);
		DrawColor3Row("線色", component.trajectoryColor);
		DrawFloatRow("透明度", component.trajectoryAlpha, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("太さ", component.trajectoryThickness, 0.1f, 0.1f, 20.0f);
		DrawIntRow("最大Point数", component.trajectoryMaximumPoints);
		DrawCheckboxRow("Scene View", component.trajectoryShowInSceneView);
		DrawCheckboxRow("Game View", component.trajectoryShowInGameView);
		DrawCheckboxRow("着弾点", component.trajectoryShowImpactPoint);
	}

	void DrawWaterSurfaceStateComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "所有者の判定点がFFT水面を横切った瞬間と現在の水面情報を公開します。魚雷、沈没物、水面跳弾のゲームルールはScript側で組み立てます。");
		DrawGameObjectReferenceRow(context, owner, "Ocean", component.waterSurfaceOceanGameObjectId, "自動検索", true);
		DrawVector3Row("ローカル判定位置", component.waterSurfaceLocalOffset, 0.01f, -100000.0f, 100000.0f);
		DrawFloatRow("Clearance", component.waterSurfaceClearance, 0.01f, -1000.0f, 1000.0f);
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.waterSurfaceActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.waterSurfaceActionTargetGameObjectId, "入水Action", component.waterSurfaceEnteredActionName);
		DrawScriptActionRow(context, owner, component.waterSurfaceActionTargetGameObjectId, "出水Action", component.waterSurfaceExitedActionName);
		const char* stateNames[] = {"水上", "入水", "水中", "出水"};
		const int32_t stateIndex = (std::clamp)(component.waterSurfaceState, 0, 3);
		DrawTextRow("Runtime状態", stateNames[stateIndex]);
		DrawTextRow("水面距離", std::to_string(component.waterSurfaceSignedDistance).c_str());
		DrawTextRow("砕波・泡率", std::to_string(component.waterSurfaceFoam).c_str());
		DrawTextRow("Ocean ID", std::to_string(component.waterSurfaceCurrentOceanGameObjectId).c_str());
	}

	void DrawOceanProbeSetComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "所有者ローカル方向の複数距離を同じFFT水面からSampleします。大波などの意味判定はC++ Script側で行います。");
		DrawGameObjectReferenceRow(context, owner, "Ocean", component.oceanProbeOceanGameObjectId, "自動検索", true);
		DrawVector3Row("ローカル原点", component.oceanProbeLocalOriginOffset, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("ローカル方向", component.oceanProbeLocalDirection, 0.01f, -1.0f, 1.0f);
		int32_t removeProbeIndex = -1;

		for (size_t probeIndex = 0u; probeIndex < component.oceanProbeEntries.size(); probeIndex++) {
			EditorOceanProbeEntry& probeEntry = component.oceanProbeEntries[probeIndex];
			ImGui::PushID(static_cast<int32_t>(probeIndex));
			DrawFloatRow("距離", probeEntry.distance, 0.1f, 0.0f, 1000000.0f);
			DrawTextRow("Runtime", probeEntry.isValid ? "Sample済み" : "範囲外");
			DrawTextRow("相対高さ", std::to_string(probeEntry.relativeHeight).c_str());
			DrawTextRow("砕波・泡率", std::to_string(probeEntry.foam).c_str());

			if (ImGui::Button("このProbeを削除")) {
				removeProbeIndex = static_cast<int32_t>(probeIndex);
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		if (removeProbeIndex >= 0) {
			component.oceanProbeEntries.erase(
				component.oceanProbeEntries.begin() + removeProbeIndex);
		}

		if (ImGui::Button("Probeを追加")) {
			component.oceanProbeEntries.push_back(EditorOceanProbeEntry{});
		}
	}

	void DrawAttackCollisionFilterComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "WeaponのPhysics Castから発射者階層、Team、明示Objectを除外し、Projectileの安全距離を設定します。");
		DrawGameObjectReferenceRow(context, owner, "発射責任者", component.attackFilterInstigatorGameObjectId, "Filter所有者", true);
		DrawCheckboxRow("発射者を無視", component.attackFilterIgnoreInstigator);
		DrawCheckboxRow("発射者の子も無視", component.attackFilterIgnoreInstigatorHierarchy);
		const char* teamRules[] = {"すべて", "異なるTeamのみ", "同じTeamのみ"};
		component.attackFilterTeamRule = (std::clamp)(component.attackFilterTeamRule, 0, 2);
		DrawComboRow("Teamルール", component.attackFilterTeamRule, teamRules, 3);
		DrawCheckboxRow("Neutralを無視", component.attackFilterIgnoreNeutral);
		DrawFloatRow("Arming距離", component.attackFilterArmingDistance, 0.1f, 0.0f, 100000.0f);
		int32_t removeIndex = -1;

		for (size_t entryIndex = 0u; entryIndex < component.attackFilterIgnoredGameObjectIds.size(); entryIndex++) {
			ImGui::PushID(static_cast<int32_t>(entryIndex));
			DrawGameObjectReferenceRow(
				context,
				owner,
				"無視Object",
				component.attackFilterIgnoredGameObjectIds[entryIndex],
				"未設定",
				true);

			if (ImGui::Button("除外設定を削除")) removeIndex = static_cast<int32_t>(entryIndex);
			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.attackFilterIgnoredGameObjectIds.erase(
				component.attackFilterIgnoredGameObjectIds.begin() + removeIndex);
		}

		if (ImGui::Button("無視Objectを追加")) component.attackFilterIgnoredGameObjectIds.push_back(-1);
	}

	void DrawTurretAimComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "Yaw台座とPitch砲身を別々に回し、可動範囲・旋回速度・照準誤差を管理します。");
		DrawGameObjectReferenceRow(context, owner, "明示Target", component.turretTargetGameObjectId, "Selectorを使用", true);
		DrawGameObjectReferenceRow(context, owner, "Target Selector", component.turretTargetSelectorGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, owner, "Yaw Pivot", component.turretYawPivotGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, owner, "Pitch Pivot", component.turretPitchPivotGameObjectId, "Yaw Pivot", true);
		DrawFloatRow("Yaw最小 deg", component.turretYawMinimumDegrees, 0.1f, -360.0f, 360.0f);
		DrawFloatRow("Yaw最大 deg", component.turretYawMaximumDegrees, 0.1f, -360.0f, 360.0f);
		DrawFloatRow("Pitch最小 deg", component.turretPitchMinimumDegrees, 0.1f, -180.0f, 180.0f);
		DrawFloatRow("Pitch最大 deg", component.turretPitchMaximumDegrees, 0.1f, -180.0f, 180.0f);
		DrawFloatRow("Yaw速度 deg/s", component.turretYawSpeedDegrees, 1.0f, 0.0f, 100000.0f);
		DrawFloatRow("Pitch速度 deg/s", component.turretPitchSpeedDegrees, 1.0f, 0.0f, 100000.0f);
		DrawFloatRow("照準許容角 deg", component.turretAimToleranceDegrees, 0.1f, 0.0f, 180.0f);
		DrawFloatRow("Target予測秒", component.turretPredictionSeconds, 0.01f, 0.0f, 60.0f);
		DrawTextRow("到達可能", component.turretCanReachTarget ? "はい" : "いいえ");
		DrawTextRow("照準完了", component.turretIsAimed ? "はい" : "いいえ");
		DrawTextRow("Yaw誤差", std::to_string(component.turretYawErrorDegrees).c_str());
		DrawTextRow("Pitch誤差", std::to_string(component.turretPitchErrorDegrees).c_str());
	}

	void DrawWeaponGroupComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "複数のHitscan/Projectile Weaponを同時、順次、Round Robinで発射します。");
		const char* modes[] = {"同時", "順次", "Round Robin"};
		component.weaponGroupMode = (std::clamp)(component.weaponGroupMode, 0, 2);
		DrawComboRow("発射方式", component.weaponGroupMode, modes, 3);
		DrawFloatRow("順次間隔", component.weaponGroupInterval, 0.01f, 0.0f, 3600.0f);
		DrawCheckboxRow("全武器Ready必須", component.weaponGroupRequireAllReady);
		int32_t removeIndex = -1;

		for (size_t entryIndex = 0u; entryIndex < component.weaponGroupEntries.size(); entryIndex++) {
			EditorWeaponGroupEntry& entry = component.weaponGroupEntries[entryIndex];
			ImGui::PushID(static_cast<int32_t>(entryIndex));
			DrawGameObjectReferenceRow(context, owner, "Weapon", entry.weaponGameObjectId, "未設定", false);
			DrawCheckboxRow("使用", entry.isEnabled);

			if (ImGui::Button("Weaponを削除")) removeIndex = static_cast<int32_t>(entryIndex);
			ImGui::Separator();
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.weaponGroupEntries.erase(component.weaponGroupEntries.begin() + removeIndex);
		if (ImGui::Button("Weaponを追加")) component.weaponGroupEntries.push_back(EditorWeaponGroupEntry{});
		DrawGameObjectReferenceRow(context, owner, "完了Action対象", component.weaponGroupActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, owner, component.weaponGroupActionTargetGameObjectId, "完了Action", component.weaponGroupCompletedActionName);
		DrawTextRow("Runtime", component.weaponGroupIsFiring ? "順次発射中" : "待機");
	}

	void DrawProjectileImpactPhysicsComponent(EditorComponent& component) {
		DrawTextRow("説明", "弾の残存Energy、入射角、Surface補正から貫通または跳弾を決めます。");
		DrawFloatRow("初期貫通Energy", component.projectileImpactPenetrationEnergy, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("基本貫通損失", component.projectileImpactPenetrationLoss, 0.1f, 0.0f, 1000000.0f);
		DrawIntRow("最大貫通回数", component.projectileImpactMaximumPenetrations);
		component.projectileImpactMaximumPenetrations = (std::max)(component.projectileImpactMaximumPenetrations, 0);
		DrawFloatRow("跳弾開始角 deg", component.projectileImpactRicochetAngleDegrees, 0.1f, 0.0f, 90.0f);
		DrawFloatRow("速度保持率", component.projectileImpactEnergyRetention, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Damage保持率", component.projectileImpactDamageRetention, 0.01f, 0.0f, 1.0f);
		DrawIntRow("最大跳弾回数", component.projectileImpactMaximumRicochets);
		component.projectileImpactMaximumRicochets = (std::max)(component.projectileImpactMaximumRicochets, 0);
		int32_t removeIndex = -1;

		for (size_t entryIndex = 0u; entryIndex < component.projectileImpactSurfaceModifiers.size(); entryIndex++) {
			EditorProjectileSurfaceModifierEntry& entry = component.projectileImpactSurfaceModifiers[entryIndex];
			ImGui::PushID(static_cast<int32_t>(entryIndex));
			DrawStringInputRow("Surface Tag", entry.surfaceTag);
			DrawFloatRow("貫通損失倍率", entry.penetrationLossMultiplier, 0.01f, 0.0f, 1000.0f);
			DrawFloatRow("跳弾角Offset", entry.ricochetAngleOffset, 0.1f, -90.0f, 90.0f);
			DrawFloatRow("Energy保持倍率", entry.energyRetentionMultiplier, 0.01f, 0.0f, 1000.0f);

			if (ImGui::Button("Surface補正を削除")) removeIndex = static_cast<int32_t>(entryIndex);
			ImGui::Separator();
			ImGui::PopID();
		}

		if (removeIndex >= 0) component.projectileImpactSurfaceModifiers.erase(component.projectileImpactSurfaceModifiers.begin() + removeIndex);
		if (ImGui::Button("Surface補正を追加")) component.projectileImpactSurfaceModifiers.push_back(EditorProjectileSurfaceModifierEntry{});
	}

	void DrawCameraHorizonStabilizerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "船体等のWorld姿勢を軸別に減衰継承し、Cameraの水平線と画面酔いを制御します。");
		DrawGameObjectReferenceRow(context, owner, "追従Source", component.horizonSourceGameObjectId, "未設定", false);
		DrawVector3Row("ローカル位置Offset", component.horizonLocalPositionOffset, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("回転Offset deg", component.horizonRotationOffsetDegrees, 0.1f, -360.0f, 360.0f);
		DrawCheckboxRow("位置を追従", component.horizonFollowPosition);
		DrawFloatRow("Pitch継承", component.horizonPitchInheritance, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Yaw継承", component.horizonYawInheritance, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Roll継承", component.horizonRollInheritance, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("World Up", component.horizonWorldUp, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("減衰", component.horizonDamping, 0.1f, 0.0f, 1000.0f);
		DrawFloatRow("最大Roll deg", component.horizonMaximumRollDegrees, 0.1f, 0.0f, 180.0f);
	}

	// 常時演出の種類を切り替えた瞬間、その演出に合った値へParamA/B/Cをリセットする。
	// 演出ごとに単位(px/秒/0-1など)が全く違うため、共通の初期値では大抵ちぐはぐになる。
	void ResetContinuousTextEffectParamsForType(EditorComponent& component, int32_t type) {
		switch (type) {
		case 1:  // 点滅
			component.textContinuousParamA = 0.3f;
			component.textContinuousParamB = 0.0f;
			component.textContinuousParamC = 0.0f;
			break;
		case 2:  // レインボー
			component.textContinuousParamA = 0.6f;
			component.textContinuousParamB = 0.15f;
			component.textContinuousParamC = 0.0f;
			break;
		case 3:  // 波
			component.textContinuousParamA = 8.0f;
			component.textContinuousParamB = 0.5f;
			component.textContinuousParamC = 4.0f;
			break;
		case 4:  // 発光
		case 5:  // 輪郭の発光
			component.textContinuousParamA = type == 4 ? 10.0f : 2.0f;
			component.textContinuousParamB = 3.0f;
			component.textContinuousParamC = 0.3f;
			break;
		case 6:  // 色収差
			component.textContinuousParamA = 2.0f;
			component.textContinuousParamB = 0.0f;
			component.textContinuousParamC = 0.0f;
			break;
		case 7:  // 微振動
			component.textContinuousParamA = 2.0f;
			component.textContinuousParamB = 20.0f;
			component.textContinuousParamC = 0.0f;
			break;
		case 8:  // 不規則点滅
			component.textContinuousParamA = 0.4f;
			component.textContinuousParamB = 6.0f;
			component.textContinuousParamC = 0.0f;
			break;
		case 9:  // 脈動
			component.textContinuousParamA = 1.05f;
			component.textContinuousParamB = 3.0f;
			component.textContinuousParamC = 0.0f;
			break;
		case 10:  // 残像
			component.textContinuousParamA = 10.0f;
			component.textContinuousParamB = 2.0f;
			component.textContinuousParamC = 4.0f;
			break;
		case 11:  // 簡易グリッチ
			component.textContinuousParamA = 6.0f;
			component.textContinuousParamB = 1.5f;
			component.textContinuousParamC = 0.8f;
			break;
		default:
			break;
		}
	}

	void DrawTextEffectComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		(void)context;
		(void)owner;
		DrawTextRow(
			"説明",
			"同じObjectのText/TextMeshProUGUIへ演出をかけます。出現演出(1回だけ)と常時演出(ずっと動く)は"
			"独立していて、両方同時に組み合わせられます。");

		DrawTextRow("── 出現演出 ──", "Activeになった瞬間に1回だけ再生されます。");
		const char* appearItems[] = {"なし", "フェードイン", "タイプライター", "スケールポップ"};
		component.textAppearEffectType = (std::clamp)(
			component.textAppearEffectType, 0, static_cast<int32_t>(_countof(appearItems)) - 1);
		DrawComboRow("出現効果", component.textAppearEffectType, appearItems, static_cast<int32_t>(_countof(appearItems)));

		if (component.textAppearEffectType != 0) {
			DrawFloatRow("継続時間(秒)", component.textAppearDuration, 0.01f, 0.01f, 60.0f);
			DrawFloatRow("開始遅延(秒)", component.textAppearDelay, 0.01f, 0.0f, 60.0f);

			if (component.textAppearEffectType == 2) {
				DrawFloatRow("文字/秒", component.textAppearParamA, 0.1f, 0.1f, 200.0f);
			}
			else if (component.textAppearEffectType == 3) {
				DrawFloatRow("オーバーシュート倍率", component.textAppearParamA, 0.01f, 1.0f, 3.0f);
			}
		}

		DrawTextRow("── 常時演出 ──", "表示され続ける間、ずっと動き続けます。出現演出と並行して再生されます。");
		const char* continuousItems[] = {
			"なし", "点滅", "レインボー", "波", "発光", "輪郭の発光", "色収差",
			"微振動", "不規則点滅", "脈動", "残像", "簡易グリッチ"};
		component.textContinuousEffectType = (std::clamp)(
			component.textContinuousEffectType, 0, static_cast<int32_t>(_countof(continuousItems)) - 1);
		const int32_t previousContinuousType = component.textContinuousEffectType;

		if (DrawComboRow(
			"常時効果", component.textContinuousEffectType, continuousItems,
			static_cast<int32_t>(_countof(continuousItems))) &&
			component.textContinuousEffectType != previousContinuousType) {
			ResetContinuousTextEffectParamsForType(component, component.textContinuousEffectType);
		}

		if (component.textContinuousEffectType != 0) {
			DrawFloatRow("開始遅延(秒)", component.textContinuousDelay, 0.01f, 0.0f, 60.0f);
		}

		switch (component.textContinuousEffectType) {
		case 1:
			DrawFloatRow("点滅間隔(秒)", component.textContinuousParamA, 0.01f, 0.02f, 5.0f);
			break;
		case 2:
			DrawTextRow("説明", "文字の色が虹色に変化し続けます。");
			DrawFloatRow("色相回転速度(周/秒)", component.textContinuousParamA, 0.01f, 0.01f, 10.0f);
			DrawFloatRow("文字ごとの色ずれ", component.textContinuousParamB, 0.01f, 0.0f, 1.0f);
			break;
		case 3:
			DrawTextRow("説明", "文字が先頭から順に位相をずらして上下し、波として伝わって見えます。");
			DrawFloatRow("振幅(px)", component.textContinuousParamA, 0.1f, 0.0f, 200.0f);
			DrawFloatRow("文字ごとの位相", component.textContinuousParamB, 0.01f, 0.0f, 3.0f);
			DrawFloatRow("揺れる速さ", component.textContinuousParamC, 0.1f, 0.0f, 20.0f);
			break;
		case 4:
			DrawTextRow("説明", "文字の周囲がずっと光ります。");
			DrawFloatRow("Glow半径(px)", component.textContinuousParamA, 0.1f, 0.0f, 60.0f);
			DrawFloatRow("脈動速度(0で静止)", component.textContinuousParamB, 0.05f, 0.0f, 20.0f);
			DrawFloatRow("脈動の深さ", component.textContinuousParamC, 0.01f, 0.0f, 1.0f);
			break;
		case 5:
			DrawTextRow("説明", "文字本体は普通のまま、輪郭だけ光ります。");
			DrawFloatRow("輪郭太さ(px)", component.textContinuousParamA, 0.1f, 0.5f, 20.0f);
			DrawFloatRow("脈動速度(0で静止)", component.textContinuousParamB, 0.05f, 0.0f, 20.0f);
			DrawFloatRow("脈動の深さ", component.textContinuousParamC, 0.01f, 0.0f, 1.0f);
			break;
		case 6:
			DrawTextRow("説明", "赤と青の文字コピーを左右にずらして表示します。");
			DrawFloatRow("ズレ量(px)", component.textContinuousParamA, 0.1f, 0.0f, 20.0f);
			DrawFloatRow("脈動速度(0で静止)", component.textContinuousParamB, 0.05f, 0.0f, 20.0f);
			DrawFloatRow("脈動の深さ", component.textContinuousParamC, 0.01f, 0.0f, 1.0f);
			break;
		case 7:
			DrawTextRow("説明", "細かく位置を揺らし続けます。WARNING等の危険表示向け。");
			DrawFloatRow("振れ幅(px)", component.textContinuousParamA, 0.1f, 0.0f, 40.0f);
			DrawFloatRow("速さ", component.textContinuousParamB, 0.1f, 0.1f, 60.0f);
			break;
		case 8:
			DrawTextRow("説明", "不規則な蛍光灯のように明るさが揺れ続けます。");
			DrawFloatRow("最低輝度(0-1)", component.textContinuousParamA, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("フリッカー速さ", component.textContinuousParamB, 0.1f, 0.1f, 30.0f);
			break;
		case 9:
			DrawTextRow("説明", "文字サイズがゆっくり拡大縮小を繰り返します。");
			DrawFloatRow("最大拡大率", component.textContinuousParamA, 0.01f, 1.0f, 2.0f);
			DrawFloatRow("速さ", component.textContinuousParamB, 0.05f, 0.05f, 20.0f);
			break;
		case 10:
			DrawTextRow("説明", "文字がわずかに動き、その軌跡に薄いコピーが残り続けます。");
			DrawFloatRow("振れ幅(px)", component.textContinuousParamA, 0.1f, 0.0f, 100.0f);
			DrawFloatRow("速さ", component.textContinuousParamB, 0.05f, 0.05f, 20.0f);
			DrawFloatRow("残像の数", component.textContinuousParamC, 1.0f, 2.0f, 6.0f);
			break;
		case 11:
			DrawTextRow("説明", "定期的に位置ズレ・RGB分離・一部欠けをランダムに繰り返します。");
			DrawFloatRow("ズレ量(px)", component.textContinuousParamA, 0.1f, 0.0f, 40.0f);
			DrawFloatRow("発生頻度(回/秒)", component.textContinuousParamB, 0.05f, 0.05f, 10.0f);
			DrawFloatRow("強さ(0-1)", component.textContinuousParamC, 0.01f, 0.0f, 1.0f);
			break;
		default:
			break;
		}
	}

	void DrawSceneTransitionComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "Scene切り替え時の色フェード・ワイプ・Camera移動を管理します。SceneButton等から遷移先として使えます。");
		const char* typeItems[] = {"なし", "色フェード", "ワイプ", "Camera Dive"};
		component.sceneTransitionType = (std::clamp)(component.sceneTransitionType, 0, static_cast<int32_t>(_countof(typeItems)) - 1);
		DrawComboRow("種類", component.sceneTransitionType, typeItems, static_cast<int32_t>(_countof(typeItems)));
		DrawStringInputRow("遷移先 Scene", component.sceneTransitionTargetScenePath);
		DrawFloatRow("覆うまでの時間(秒)", component.sceneTransitionOutDuration, 0.01f, 0.01f, 30.0f);
		DrawFloatRow("静止時間(秒)", component.sceneTransitionHoldSeconds, 0.01f, 0.0f, 30.0f);
		DrawFloatRow("見せる時間(秒)", component.sceneTransitionInDuration, 0.01f, 0.01f, 30.0f);
		DrawVector3Row("色", component.sceneTransitionColor, 0.01f, 0.0f, 1.0f);

		if (component.sceneTransitionType == 3) {
			DrawGameObjectReferenceRow(context, owner, "Dive基準Object", component.sceneTransitionCameraDiveSourceGameObjectId, "遷移開始時のCamera位置", false);
			DrawVector3Row("Dive開始位置 Offset", component.sceneTransitionCameraDivePositionOffset, 0.1f, -100000.0f, 100000.0f);
			DrawVector3Row("Dive開始角度 deg", component.sceneTransitionCameraDiveRotationDegrees, 0.1f, -360.0f, 360.0f);
		}
	}

	void DrawFireLineCheckComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "発射後の命中除外とは分離し、砲口前方に自艦構造物や遮蔽物がある時だけ発射を止めます。");
		DrawGameObjectReferenceRow(context, owner, "砲口", component.fireLineMuzzleGameObjectId, "このObject", true);
		DrawGameObjectReferenceRow(context, owner, "前方向Source", component.fireLineDirectionGameObjectId, "砲口", true);
		DrawGameObjectReferenceRow(context, owner, "許可Target", component.fireLineAllowedTargetGameObjectId, "なし", true);
		DrawFloatRow("検査距離", component.fireLineDistance, 0.1f, 0.0f, 1000000.0f);
		DrawFloatRow("検査半径", component.fireLineRadius, 0.01f, 0.0f, 100000.0f);
		DrawIntRow("Block Layer Mask", component.fireLineLayerMask);
		int32_t removeIndex = -1;

		for (size_t ignoredIndex = 0u; ignoredIndex < component.fireLineIgnoredGameObjectIds.size(); ignoredIndex++) {
			ImGui::PushID(static_cast<int32_t>(ignoredIndex));
			DrawGameObjectReferenceRow(
				context,
				owner,
				"無視Object",
				component.fireLineIgnoredGameObjectIds[ignoredIndex],
				"未設定",
				false);

			if (ImGui::Button("無視Objectを削除")) {
				removeIndex = static_cast<int32_t>(ignoredIndex);
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.fireLineIgnoredGameObjectIds.erase(
				component.fireLineIgnoredGameObjectIds.begin() + removeIndex);
		}

		if (ImGui::Button("無視Objectを追加")) {
			component.fireLineIgnoredGameObjectIds.push_back(-1);
		}

		DrawTextRow("Runtime", component.fireLineClear ? "射線Clear" : "発射Blocked");
		DrawTextRow("Blocking Object", std::to_string(component.fireLineBlockingGameObjectId).c_str());
		DrawTextRow("Blocking距離", std::to_string(component.fireLineBlockingDistance).c_str());
	}

	void DrawStatusEffectSetComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& owner,
		EditorComponent& component) {
		DrawTextRow("説明", "火災等の意味やDamage式は持たず、ID、Duration、Stack、TickとScript Actionだけを管理します。");
		DrawGameObjectReferenceRow(context, owner, "Action対象", component.statusEffectActionTargetGameObjectId, "このObject", true);
		int32_t removeIndex = -1;

		for (size_t definitionIndex = 0u; definitionIndex < component.statusEffectDefinitions.size(); definitionIndex++) {
			EditorStatusEffectDefinitionEntry& definition = component.statusEffectDefinitions[definitionIndex];
			ImGui::PushID(static_cast<int32_t>(definitionIndex));
			DrawStringInputRow("Effect ID", definition.effectId);
			DrawFloatRow("Duration", definition.duration, 0.05f, 0.001f, 1000000.0f);
			const char* stackModes[] = {"Refresh", "Stack", "Ignore"};
			definition.stackMode = (std::clamp)(definition.stackMode, 0, 2);
			DrawComboRow("Stack Mode", definition.stackMode, stackModes, 3);
			DrawIntRow("最大Stack", definition.maximumStacks);
			definition.maximumStacks = (std::max)(definition.maximumStacks, 1);
			DrawFloatRow("Tick間隔", definition.tickInterval, 0.05f, 0.0f, 1000000.0f);
			DrawScriptActionRow(context, owner, component.statusEffectActionTargetGameObjectId, "開始Action", definition.startedActionName);
			DrawScriptActionRow(context, owner, component.statusEffectActionTargetGameObjectId, "Tick Action", definition.tickActionName);
			DrawScriptActionRow(context, owner, component.statusEffectActionTargetGameObjectId, "終了Action", definition.endedActionName);

			if (ImGui::Button("Effect定義を削除")) {
				removeIndex = static_cast<int32_t>(definitionIndex);
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		if (removeIndex >= 0) {
			component.statusEffectDefinitions.erase(
				component.statusEffectDefinitions.begin() + removeIndex);
		}

		if (ImGui::Button("Effect定義を追加")) {
			EditorStatusEffectDefinitionEntry definition{};
			definition.effectId = "Effect" + std::to_string(component.statusEffectDefinitions.size() + 1u);
			component.statusEffectDefinitions.push_back(definition);
		}

		DrawTextRow("Runtime Entry数", std::to_string(component.statusEffectRuntimeEntries.size()).c_str());
	}

	void DrawMovementModifierComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Rail等が生成した基準Transformへ汎用Offsetを加えます。単独ではゲームルールを実行しません。");
		DrawVector3Row("位置Offset", component.movementModifierLocalPositionOffset, 0.01f, -100000.0f, 100000.0f);
		DrawVector3Row("回転Offset deg", component.movementModifierLocalRotationOffset, 0.1f, -36000.0f, 36000.0f);
		bool allowsX = (component.movementModifierAxisMask & 1) != 0;
		bool allowsY = (component.movementModifierAxisMask & 2) != 0;
		bool allowsZ = (component.movementModifierAxisMask & 4) != 0;
		DrawCheckboxRow("位置X", allowsX);
		DrawCheckboxRow("位置Y", allowsY);
		DrawCheckboxRow("位置Z", allowsZ);
		component.movementModifierAxisMask = (allowsX ? 1 : 0) | (allowsY ? 2 : 0) | (allowsZ ? 4 : 0);
		DrawVector2Row("入力範囲", component.movementModifierInputRange, 0.1f, 0.0f, 100000.0f);
		DrawFloatRow("入力追従速度", component.movementModifierInputSpeed, 0.1f, 0.0f, 100000.0f);
		DrawGameObjectReferenceRow(context, ownerGameObject, "PlayerInput", component.movementModifierInputGameObjectId, "このObject", true);
		DrawStringInputRow("Action Map", component.movementModifierActionMapName);
		DrawStringInputRow("Vector2 Action", component.movementModifierActionName);
	}

	void DrawPropertyTweenComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Runtime Property Registryへ登録されたFloatまたはVector3を時間補間します。");
		DrawGameObjectReferenceRow(context, ownerGameObject, "対象", component.propertyTweenTargetGameObjectId, "このObject", true);
		DrawStringInputRow("Component", component.propertyTweenComponentName);
		DrawStringInputRow("Property", component.propertyTweenPropertyName);
		const char* valueTypes[] = {"Float", "Vector3"};
		component.propertyTweenValueType = (std::clamp)(component.propertyTweenValueType, 0, 1);
		DrawComboRow("値型", component.propertyTweenValueType, valueTypes, 2);
		DrawVector3Row("開始値", component.propertyTweenStartValue, 0.01f, -1000000.0f, 1000000.0f);
		DrawVector3Row("終了値", component.propertyTweenEndValue, 0.01f, -1000000.0f, 1000000.0f);
		DrawFloatRow("時間", component.propertyTweenDuration, 0.01f, 0.001f, 86400.0f);
		const char* curves[] = {"Linear", "SmoothStep", "Ease In", "Ease Out"};
		component.propertyTweenCurve = (std::clamp)(component.propertyTweenCurve, 0, 3);
		DrawComboRow("Curve", component.propertyTweenCurve, curves, 4);
		DrawCheckboxRow("Play開始時に再生", component.propertyTweenPlayOnStart);
		DrawCheckboxRow("ループ", component.propertyTweenLoop);
		DrawGameObjectReferenceRow(context, ownerGameObject, "完了Action対象", component.propertyTweenActionTargetGameObjectId, "このObject", true);
		DrawScriptActionRow(context, ownerGameObject, component.propertyTweenActionTargetGameObjectId, "完了Action", component.propertyTweenCompletedActionName);
	}

	void DrawActionRelayComponent(
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Relay()の1回の呼出を、子の中継先へ分配します。ゲーム固有Actionの意味は持ちません。");
		DrawCheckboxRow("Play開始時にRelay", component.actionRelayOnStart);

		if (ImGui::Button("子Relay Targetを追加")) {
			g_pendingActionRelayTargetParentId = ownerGameObject.id;
		}
	}

	void DrawActionRelayTargetComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "親ActionRelayから呼ばれる1つのScript Action接続です。");
		DrawCheckboxRow("有効", component.actionRelayTargetEnabled);
		DrawGameObjectReferenceRow(context, ownerGameObject, "Action対象", component.actionRelayTargetGameObjectId, "親Relay", true);
		DrawScriptActionRow(context, ownerGameObject, component.actionRelayTargetGameObjectId, "Action", component.actionRelayActionName);
	}

	void DrawWaveSpawnerComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "ObjectPoolから指定数を生成し、編隊配置とWave完了を管理します。敵の行動は別Componentへ委ねます。");
		const char* sourceModes[] = {"ObjectPool 生成", "事前配置した子（互換）"};
		component.waveSpawnSourceMode = (std::clamp)(component.waveSpawnSourceMode, 0, 1);
		DrawComboRow("生成元", component.waveSpawnSourceMode, sourceModes, 2);

		if (component.waveSpawnSourceMode == 0) {
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"ObjectPool",
				component.wavePoolGameObjectId,
				"未設定",
				false);
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"生成基準位置",
				component.waveSpawnPointGameObjectId,
				"このObject",
				true);
			DrawIntRow("生成数(総数)", component.waveSpawnCount);
			component.waveSpawnCount = (std::clamp)(component.waveSpawnCount, 1, 1024);
			DrawIntRow("同時存在目標数(0=一括生成)", component.waveTargetAliveCount);
			component.waveTargetAliveCount = (std::clamp)(component.waveTargetAliveCount, 0, 1024);

			if (component.waveTargetAliveCount > 0) {
				DrawTextRow("ラッシュ",
					"この数を画面内に保つよう、撃破されるたびに総数へ達するまで補充します。"
					"SpawnPointSetを設定すると補充のたびに別方向から出現します。");
				DrawGameObjectReferenceRow(
					context,
					ownerGameObject,
					"補充SpawnPointSet",
					component.waveSpawnPointSetGameObjectId,
					"編隊配置のまま",
					true);
			}

			DrawIntRow("1Frame最大生成数", component.waveSpawnMaximumPerFrame);
			component.waveSpawnMaximumPerFrame = (std::clamp)(component.waveSpawnMaximumPerFrame, 1, 1024);
			const char* formationPatterns[] = {"同一点", "横列", "V字", "円", "グリッド"};
			component.waveFormationPattern = (std::clamp)(component.waveFormationPattern, 0, 4);
			DrawComboRow("編隊", component.waveFormationPattern, formationPatterns, 5);
			DrawFloatRow("編隊間隔", component.waveFormationSpacing, 0.1f, 0.0f, 100000.0f);
			DrawFloatRow("レール開始進行率", component.waveSpawnRailStartNormalized, 0.01f, -1.0f, 1.0f);

			if (component.waveFormationPattern == 4) {
				DrawIntRow("グリッド列数", component.waveFormationColumns);
				component.waveFormationColumns = (std::clamp)(component.waveFormationColumns, 1, 1024);
			}

			DrawTextRow("物理ObjectPool", "Rigidbody/Collider付きでも必要時に遅延生成します。容量は同時出現数以上に設定してください。");
			DrawTextRow("互換動作", "ObjectPoolが未設定または無効なら、旧Scene保護のため直下の子を順番に有効化します。");
		}
		else {
			DrawCheckboxRow("開始時に子を待機", component.waveDeactivateChildrenOnStart);
			DrawTextRow("生成対象", "このGameObject直下の子をHierarchy順で使用する旧Scene互換方式です。");
		}

		const char* triggerModes[] = {"Play 開始", "RailFollower 進行率", "外部開始", "距離"};
		component.waveTriggerMode = (std::clamp)(component.waveTriggerMode, 0, 3);
		DrawComboRow("開始条件", component.waveTriggerMode, triggerModes, 4);

		if (component.waveTriggerMode == 1) {
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"進行率 Source",
				component.waveTriggerSourceGameObjectId,
				"未設定",
				true);
			DrawFloatRow("開始進行率", component.waveTriggerValue, 0.01f, 0.0f, 1.0f);
		}
		else if (component.waveTriggerMode == 3) {
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"距離 Source",
				component.waveTriggerSourceGameObjectId,
				"未設定",
				true);
			DrawFloatRow("開始距離", component.waveTriggerValue, 1.0f, 0.0f, 1000000.0f);
			DrawTextRow("距離の基準点", "生成基準位置。未設定時はWave所有ObjectのWorld位置です。");
		}

		DrawFloatRow("生成間隔", component.waveSpawnInterval, 0.01f, 0.0f, 3600.0f);
		const char* completionModes[] = {"全生成", "全撃破・全返却"};
		component.waveCompletionMode = (std::clamp)(component.waveCompletionMode, 0, 1);
		DrawComboRow("完了条件", component.waveCompletionMode, completionModes, 2);
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Action 対象",
			component.waveActionTargetGameObjectId,
			"このObject",
			true);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.waveActionTargetGameObjectId,
			"開始 Action",
			component.waveStartedActionName);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.waveActionTargetGameObjectId,
			"各生成 Action",
			component.waveSpawnedActionName);
		DrawTextRow("各生成 Payload", "GameObject: 今回生成したObject ID");
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.waveActionTargetGameObjectId,
			"完了条件 Action",
			component.waveCompletedActionName);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.waveActionTargetGameObjectId,
			"全撃破 Action",
			component.waveAllDefeatedActionName);
		DrawTextRow("完了 Payload", "Int: このWaveで生成した総数");
	}

	void DrawTimelineEventComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "条件成立時に対象C++ Scriptへ任意名のActionを通知します。");
		const char* sourceModes[] = {"Play 経過秒", "RailFollower 進行率"};
		component.timelineSourceMode = (std::clamp)(component.timelineSourceMode, 0, 1);
		DrawComboRow("時間 Source", component.timelineSourceMode, sourceModes, 2);

		if (component.timelineSourceMode == 1) {
			DrawGameObjectReferenceRow(
				context,
				ownerGameObject,
				"進行率 Source",
				component.timelineSourceGameObjectId,
				"未設定",
				true);
			DrawFloatRow("発火進行率", component.timelineTriggerValue, 0.01f, 0.0f, 1.0f);
		}
		else {
			DrawFloatRow("発火秒", component.timelineTriggerValue, 0.05f, 0.0f, 86400.0f);
		}

		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Action 対象",
			component.timelineTargetGameObjectId,
			"このObject",
			true);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.timelineTargetGameObjectId,
			"Action 名",
			component.timelineActionName);
		DrawCheckboxRow("一度だけ", component.timelineTriggerOnce);
	}

	void DrawThresholdStateComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "値を3状態へ分け、状態変更時に名前付きScript Actionだけを通知します。");
		const char* sourceModes[] = {"Health 比率", "RailFollower 進行率"};
		component.thresholdSourceMode = (std::clamp)(component.thresholdSourceMode, 0, 1);
		DrawComboRow("値 Source", component.thresholdSourceMode, sourceModes, 2);
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Source Object",
			component.thresholdSourceGameObjectId,
			"このObject",
			true);
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Action 対象",
			component.thresholdTargetGameObjectId,
			"このObject",
			true);
		DrawFloatRow("State 2 境界", component.thresholdSecondValue, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("State 3 境界", component.thresholdThirdValue, 0.01f, 0.0f, 1.0f);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.thresholdTargetGameObjectId,
			"State 1 Action",
			component.thresholdFirstActionName);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.thresholdTargetGameObjectId,
			"State 2 Action",
			component.thresholdSecondActionName);
		DrawScriptActionRow(
			context,
			ownerGameObject,
			component.thresholdTargetGameObjectId,
			"State 3 Action",
			component.thresholdThirdActionName);
	}

	void DrawUiValueBindingComponent(
		EditorInspectorPanelContext& context,
		const EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "汎用値を同じGameObjectのTextとSliderへ反映します。ゲームオブジェクトを自動探索しません。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"Source Object",
			component.uiBindingSourceGameObjectId,
			"このObject",
			true);
		const char* valueTypes[] = {
			"Health 現在値",
			"Health 比率",
			"RailFollower 進行率",
			"Active",
			"Counter 現在値",
			"WeaponLoadout 選択武器名"};
		component.uiBindingValueType = (std::clamp)(component.uiBindingValueType, 0, 5);
		DrawComboRow(
			"値",
			component.uiBindingValueType,
			valueTypes,
			static_cast<int32_t>(_countof(valueTypes)));
		DrawStringInputRow("接頭文字", component.uiBindingPrefix);
		DrawIntRow("小数桁", component.uiBindingPrecision);
		component.uiBindingPrecision = (std::clamp)(component.uiBindingPrecision, 0, 6);
		DrawFloatRow("表示倍率", component.uiBindingScale, 0.1f, -1000000.0f, 1000000.0f);
	}

	void DrawSceneButtonComponent(
		EditorInspectorPanelContext& context,
		EditorComponent& component) {
		DrawTextRow("説明", "Script を書かず、Game View のクリックで指定 Scene を開きます。");
		DrawStringInputRow("表示文字", component.buttonLabel);
		DrawVector2Row("位置", component.buttonPosition, 0.5f, -10000.0f, 10000.0f);
		DrawVector2Row("サイズ", component.buttonSize, 0.5f, 1.0f, 4096.0f);
		DrawColor3Row("通常色", component.color);
		DrawColor3Row("ホバー色", component.buttonHoverColor);
		DrawColor3Row("押下色", component.buttonPressedColor);
		DrawCheckboxRow("操作可能", component.buttonInteractable);
		DrawStringInputRow("遷移先 Scene", component.sceneButtonScenePath);

		if (!context.selectedAssetPath.empty() &&
			EditorAssetUtility::HasExtension(context.selectedAssetPath, ".scene") &&
			ImGui::Button("選択中 Scene を設定", ImVec2(-1.0f, 0.0f))) {
			component.sceneButtonScenePath = context.selectedAssetPath;
		}
	}

	void DrawAutoConvexCollisionComponent(const EditorGameObject& gameObject, EditorComponent& component) {
		std::string collisionAssetPath;
		const ModelData* loadedModelData = GetModelDataForComponent(gameObject, component, false, collisionAssetPath);
		const ModelData emptyModelData{};
		const ModelData& modelData = loadedModelData != nullptr ? *loadedModelData : emptyModelData;
		const bool hasModelData = loadedModelData != nullptr;
		const std::string renderAssetPath = GetRenderableModelAssetPath(gameObject);
		const char* meshSourceLabel =
			component.assetPath.empty() ? "描画メッシュを流用" : "凸包生成メッシュを個別使用";

		DrawTextRow("説明", "FBX / OBJ を非重複区間へ分割し、Play 開始時に複数の凸包を自動生成します。");
		DrawTextRow("メッシュ", collisionAssetPath.empty() ? "未設定" : collisionAssetPath.c_str());
		DrawTextRow("参照元", meshSourceLabel);
		if (!component.assetPath.empty() && !renderAssetPath.empty()) {
			DrawTextRow("描画メッシュ", renderAssetPath.c_str());
		}

		DrawColliderCommonRows(component);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);

		DrawSubHeader("Auto Convex");
		DrawIntRow("最大凸包数", component.autoConvexMaximumHulls);
		component.autoConvexMaximumHulls = (std::clamp)(component.autoConvexMaximumHulls, 1, 16);
		int32_t sourceVertexCount = static_cast<int32_t>(modelData.vertices.size());
		DrawIntRow("入力頂点数", sourceVertexCount);
		DrawTextRow("生成状態", hasModelData ? "Play 開始時に自動生成" : "メッシュ未設定");
		DrawTextRow("判定形状", "Jolt StaticCompoundShape + ConvexHullShape");
	}

	void DrawAnimationComponent(EditorInspectorPanelContext& context, const EditorGameObject& gameObject, EditorComponent& component) {
		std::string animationAssetPath;
		const ModelData* loadedModelData = GetModelDataForComponent(gameObject, component, true, animationAssetPath);
		const ModelData emptyModelData{};
		const ModelData& modelData = loadedModelData != nullptr ? *loadedModelData : emptyModelData;
		const bool hasModelData = loadedModelData != nullptr;
		PropertyAnimationClip propertyAnimationClip{};
		const bool hasPropertyAnimationClip =
			EditorAssetUtility::HasExtension(component.assetPath, ".animclip") &&
			propertyAnimationClip.LoadFromJson(component.assetPath);
		EditorAnimationManager& animationManager = context.runtimeManager.GetAnimationManager();

		DrawTextRow("説明", "FBX Clip または Transform / Light / Material の Property Animation Clip を再生します。");
		DrawTextRow("アセット", animationAssetPath.empty() ? "未設定 (プロシージャル)" : animationAssetPath.c_str());
		DrawStringInputRow("Animation Clip パス", component.assetPath);

		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".animclip") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".fbx")) &&
			ImGui::Button("選択中 Animation Clip を設定", ImVec2(-1.0f, 0.0f))) {
			component.assetPath = context.selectedAssetPath;
			component.animationType = 0;
		}

		DrawFloatRow("速度", component.animationSpeed, 0.1f, 0.0f, 10.0f);
		DrawCheckboxRow("ループ", component.animationLoop);
		DrawCheckboxRow("自動再生", component.animationPlayOnAwake);
		const char* animTypes[] = {"FBX / Property Clip", "Float", "Rotate", "Pulse", "Bob"};
		component.animationType = (std::clamp)(component.animationType, 0, 4);
		DrawComboRow(
			"種類",
			component.animationType,
			animTypes,
			static_cast<int32_t>(_countof(animTypes)));
		if (component.animationType >= 1) {
			DrawFloatRow("振幅", component.animationAmplitude, 0.01f, 0.0f, 10.0f);
		}

		DrawSubHeader("再生状態");
		bool isPlaying = animationManager.IsAnimationPlaying(gameObject.id);
		DrawDisabledCheckboxRow("再生中", isPlaying);
		const float currentTime = animationManager.GetAnimationTime(gameObject.id);
		const std::string currentTimeText = std::to_string(currentTime);
		DrawTextRow("現在時間", currentTimeText.c_str());

		if (context.runtimeManager.IsPlaying()) {
			if (ImGui::Button("先頭から再生", ImVec2(-1.0f, 0.0f))) {
				animationManager.PlayAnimation(gameObject.id);
			}

			if (ImGui::Button("再生を停止", ImVec2(-1.0f, 0.0f))) {
				animationManager.StopAnimation(gameObject.id);
			}
		}

		DrawSubHeader("クリップ");
		if (hasPropertyAnimationClip) {
			DrawTextRow("種類", "Property Animation Clip");
			DrawTextRow("名前", propertyAnimationClip.name.c_str());
			DrawTextRow("長さ (秒)", std::to_string(propertyAnimationClip.durationSeconds).c_str());
			DrawTextRow("サンプルレート", std::to_string(propertyAnimationClip.sampleRate).c_str());
			int32_t trackCount = static_cast<int32_t>(propertyAnimationClip.tracks.size());
			int32_t eventCount = static_cast<int32_t>(propertyAnimationClip.events.size());
			DrawIntRow("プロパティ数", trackCount);
			DrawIntRow("イベント数", eventCount);

			if (ImGui::CollapsingHeader("アニメーション対象", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (size_t trackIndex = 0u; trackIndex < propertyAnimationClip.tracks.size(); trackIndex++) {
					const PropertyAnimationTrack& track = propertyAnimationClip.tracks[trackIndex];
					const std::string rowName = "Track " + std::to_string(trackIndex);
					DrawTextRow(rowName.c_str(), GetAnimationPropertyTargetName(track.target));
				}
			}

			return;
		}

		int32_t clipCount = static_cast<int32_t>(modelData.animationClips.size());
		DrawIntRow("クリップ数", clipCount);
		if (hasModelData && !modelData.animationClips.empty()) {
			component.animationClipIndex = (std::clamp)(
				component.animationClipIndex,
				0,
				clipCount - 1);
			std::vector<const char*> clipNames{};
			clipNames.reserve(modelData.animationClips.size());

			for (const ModelAnimationClipData& animationClip : modelData.animationClips) {
				clipNames.push_back(animationClip.name.c_str());
			}

			DrawComboRow(
				"再生クリップ",
				component.animationClipIndex,
				clipNames.data(),
				static_cast<int32_t>(clipNames.size()));
			const ModelAnimationClipData& selectedClip = modelData.animationClips[
				static_cast<size_t>(component.animationClipIndex)];
			const std::string clipDurationText = std::to_string(selectedClip.durationSeconds);
			int32_t keyframeCount = static_cast<int32_t>(selectedClip.keyframes.size());
			DrawTextRow("クリップ長さ", clipDurationText.c_str());
			DrawIntRow("Transform キー数", keyframeCount);
			DrawTextRow(
				"アニメーション対象",
				selectedClip.animatedNodeName.empty()
					? "Transform カーブなし"
					: selectedClip.animatedNodeName.c_str());
		}
	}

	void DrawAnimatorComponent(
		EditorInspectorPanelContext& context,
		EditorGameObject& gameObject,
		EditorComponent& component) {
		DrawTextRow("説明", "Animation Graph、State Machine、Blend Tree、Action、Root Motion、Animation Event を実行します。");
		DrawStringInputRow("Graph アセット", component.assetPath);

		if (!context.selectedAssetPath.empty() &&
			EditorAssetUtility::HasExtension(context.selectedAssetPath, ".animgraph") &&
			ImGui::Button("選択中 Animation Graph を設定", ImVec2(-1.0f, 0.0f))) {
			component.assetPath = context.selectedAssetPath;
		}

		if (ImGui::CollapsingHeader("再生設定", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawFloatRow("再生速度", component.animationSpeed, 0.05f, 0.0f, 10.0f);
			DrawCheckboxRow("Root Motion を適用", component.animatorApplyRootMotion);
			DrawCheckboxRow("移動速度を自動取得", component.animatorAutoVelocity);
			DrawFloatRow("既定遷移秒", component.animatorTransitionDuration, 0.01f, 0.0f, 5.0f);
		}

		if (ImGui::CollapsingHeader("標準パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (component.animatorAutoVelocity) {
				DrawTextRow("入力元", "RigidBody / CharacterController / ローカル移動 / 転がり移動の速度");
				DrawTextRow("MoveX", std::to_string(component.animatorMoveX).c_str());
				DrawTextRow("MoveY", std::to_string(component.animatorMoveY).c_str());
				DrawTextRow("Speed", std::to_string(component.animatorSpeedParameter).c_str());
			}
			else {
				DrawFloatRow("MoveX 左右", component.animatorMoveX, 0.01f, -1.0f, 1.0f);
				DrawFloatRow("MoveY 前後", component.animatorMoveY, 0.01f, -1.0f, 1.0f);
				DrawFloatRow("Speed", component.animatorSpeedParameter, 0.01f, 0.0f, 100.0f);
			}
		}

		if (ImGui::CollapsingHeader("既定 Directional Blend")) {
			DrawTextRow("注", "Graph 未設定・読み込み失敗時に使用します。FBX 内 Animation Clip の番号を設定します。");
			DrawIntRow("停止 Clip", component.animatorIdleClipIndex);
			DrawIntRow("前 Clip", component.animatorForwardClipIndex);
			DrawIntRow("後 Clip", component.animatorBackwardClipIndex);
			DrawIntRow("左 Clip", component.animatorLeftClipIndex);
			DrawIntRow("右 Clip", component.animatorRightClipIndex);
		}

		if (context.runtimeManager.IsPlaying()) {
			EditorAnimationManager& animationManager = context.runtimeManager.GetAnimationManager();
			DrawTextRow(
				"実行中 State",
				animationManager.GetAnimatorStateName(gameObject.id).c_str());
			DrawTextRow(
				"実時間",
				std::to_string(animationManager.GetAnimationTime(gameObject.id)).c_str());

			std::vector<std::pair<std::string, AnimatorParameterValue>> parameters;
			if (animationManager.GetAnimatorParameters(gameObject.id, parameters) &&
				ImGui::CollapsingHeader("実行中パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (auto& parameterPair : parameters) {
					const std::string& parameterName = parameterPair.first;
					AnimatorParameterValue& parameter = parameterPair.second;
					ImGui::PushID(parameterName.c_str());

					if (parameter.type == AnimatorParameterType::Float) {
						if (DrawFloatRow(parameterName.c_str(), parameter.floatValue, 0.01f, -10000.0f, 10000.0f)) {
							animationManager.SetFloat(gameObject.id, parameterName, parameter.floatValue);
						}
					}
					else if (parameter.type == AnimatorParameterType::Int) {
						if (DrawIntRow(parameterName.c_str(), parameter.intValue)) {
							animationManager.SetInt(gameObject.id, parameterName, parameter.intValue);
						}
					}
					else if (parameter.type == AnimatorParameterType::Bool) {
						if (DrawCheckboxRow(parameterName.c_str(), parameter.boolValue)) {
							animationManager.SetBool(gameObject.id, parameterName, parameter.boolValue);
						}
					}
					else if (parameter.type == AnimatorParameterType::Trigger) {
						if (ImGui::Button((parameterName + " を発火").c_str(), ImVec2(-1.0f, 0.0f))) {
							animationManager.SetTrigger(gameObject.id, parameterName);
						}
					}
					else if (parameter.type == AnimatorParameterType::Vector2) {
						if (DrawVector2Row(parameterName.c_str(), parameter.vector2Value, 0.01f, -10000.0f, 10000.0f)) {
							animationManager.SetVector2(gameObject.id, parameterName, parameter.vector2Value);
						}
					}
					else if (parameter.type == AnimatorParameterType::Vector3 &&
						DrawVector3Row(parameterName.c_str(), parameter.vector3Value, 0.01f, -10000.0f, 10000.0f)) {
						animationManager.SetVector3(gameObject.id, parameterName, parameter.vector3Value);
					}

					ImGui::PopID();
				}
			}
		}
	}

	void DrawAvatarMaskComponent(EditorComponent& component) {
		DrawTextRow("説明", "アニメーションを適用する体の範囲を制御するコンポーネントです。");
		DrawStringInputRow("Mask アセット", component.assetPath);
		DrawTextRow("形式", "1 行に 1 Bone 名。名前末尾の * で前方一致します。");
	}

	void DrawAudioListenerComponent(
		EditorInspectorPanelContext& context,
		EditorComponent& component) {
		DrawTextRow("説明", "Scene 内の音を聞く位置です。");
		DrawTextRow("状態", component.isActive ? "アクティブ (聞こえています)" : "無効");
		DrawTextRow("注", "AudioSource + AudioReverbZone の距離判定に使われます。");
		DrawSubHeader("実行時 Audio Mixer");
		EditorAudioManager& audioManager = context.runtimeManager.GetAudioManager();
		float masterVolume = audioManager.GetMasterVolume();
		float sfxVolume = audioManager.GetBusVolume(EditorAudioBus::Sfx);
		float bgmVolume = audioManager.GetBusVolume(EditorAudioBus::Bgm);
		float ambienceVolume = audioManager.GetBusVolume(EditorAudioBus::Ambience);
		float uiVolume = audioManager.GetBusVolume(EditorAudioBus::Ui);
		DrawFloatRow("Master", masterVolume, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("SFX", sfxVolume, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("BGM", bgmVolume, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("Ambience", ambienceVolume, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("UI", uiVolume, 0.01f, 0.0f, 1.0f);
		audioManager.SetMasterVolume(masterVolume);
		audioManager.SetBusVolume(EditorAudioBus::Sfx, sfxVolume);
		audioManager.SetBusVolume(EditorAudioBus::Bgm, bgmVolume);
		audioManager.SetBusVolume(EditorAudioBus::Ambience, ambienceVolume);
		audioManager.SetBusVolume(EditorAudioBus::Ui, uiVolume);
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
		DrawStringInputRow("共有 Actions", component.assetPath);

		if (DrawIntRow("最大 Player 数", component.particleMaxCount)) {
			component.particleMaxCount = (std::clamp)(component.particleMaxCount, 1, 8);
		}
	}

	void DrawTouchInputModuleComponent(EditorComponent& component) {
		DrawTextRow("説明", "タッチ操作を UI イベントへ渡すモジュールです。");
		DrawTextRow("状態", component.isActive ? "有効" : "無効");
	}

	void DrawParticleSystemComponent(
		EditorInspectorPanelContext& context,
		EditorGameObject& gameObject,
		EditorComponent& component,
		const char* description) {
		DrawTextRow("説明", description);
		DrawStringInputRow("Effect Asset", component.assetPath);

		if (!context.selectedAssetPath.empty() &&
			(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".effect") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".efk") ||
			 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".efkefc")) &&
			ImGui::Button("選択中 Effect Asset を設定", ImVec2(-1.0f, 0.0f))) {
			component.assetPath = context.selectedAssetPath;
		}

		if (EditorAssetUtility::HasExtension(component.assetPath, ".effect")) {
			DrawTextRow("共有設定", "Play 時は .effect 内の値を読み込み、下の個別値より優先します。");
		}
		else if (EditorAssetUtility::HasExtension(component.assetPath, ".efk") ||
			EditorAssetUtility::HasExtension(component.assetPath, ".efkefc")) {
			DrawTextRow("実行方式", "Effekseer 1.70e の公式 DX12 Runtime で再生します。");
		}

		if (ImGui::CollapsingHeader("メイン", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawCheckboxRow("自動再生", component.animationPlayOnAwake);
			DrawCheckboxRow("ループ", component.particleLooping);
			DrawFloatRow("再生時間", component.particleDuration, 0.1f, 0.01f, 3600.0f);
			DrawFloatRow("開始遅延", component.particleStartDelay, 0.1f, 0.0f, 3600.0f);
			DrawCheckboxRow("プリウォーム", component.particlePrewarm);
			DrawFloatRow("寿命", component.particleLifetime, 0.1f, 0.01f, 60.0f);
			DrawFloatRow("寿命のばらつき", component.particleLifetimeRandomness, 0.01f, 0.0f, 1.0f);
			DrawIntRow("最大数", component.particleMaxCount);
		}

		if (ImGui::CollapsingHeader("発生", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawFloatRow("1秒当たり", component.particleRate, 1.0f, 0.0f, 10000.0f);
			DrawIntRow("開始バースト", component.particleBurstCount);
		}

		if (ImGui::CollapsingHeader("形状", ImGuiTreeNodeFlags_DefaultOpen)) {
			const char* shapeItems[] = {"点", "球", "コーン", "ボックス"};
			DrawComboRow("形状", component.particleShape, shapeItems, static_cast<int32_t>(_countof(shapeItems)));
			const char* simulationSpaceItems[] = {"ワールド", "ローカル"};
			DrawComboRow(
				"シミュレーション空間",
				component.particleSimulationSpace,
				simulationSpaceItems,
				static_cast<int32_t>(_countof(simulationSpaceItems)));
			DrawFloatRow("半径", component.particleShapeRadius, 0.01f, 0.0f, 1000.0f);
			DrawFloatRow("コーン角度", component.particleShapeAngle, 1.0f, 0.0f, 89.0f);
			DrawVector3Row("ボックス範囲", component.particleBoxSize, 0.01f, 0.0f, 1000.0f);
			DrawVector3Row("放出方向", component.particleDirection, 0.01f, -1.0f, 1.0f);
		}

		if (ImGui::CollapsingHeader("移動", ImGuiTreeNodeFlags_DefaultOpen)) {
			const char* motionItems[] = {
				"直線",
				"軌道",
				"渦",
				"波",
				"吸引",
				"雲",
				"爆発 / 水しぶき",
				"Projectile Trail",
				"Ocean Spray / Mist"
			};
			DrawComboRow(
				"運動方式",
				component.particleMotionType,
				motionItems,
				static_cast<int32_t>(_countof(motionItems)));
			DrawFloatRow("初速度", component.particleSpeed, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("速度のばらつき", component.particleSpeedRandomness, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("重力", component.particleGravity, 0.1f, -100.0f, 100.0f);
			DrawFloatRow("空気抵抗", component.particleDrag, 0.01f, 0.0f, 100.0f);
			DrawFloatRow("終了速度倍率", component.particleEndSpeedMultiplier, 0.01f, 0.0f, 10.0f);
			DrawFloatRow("回転速度", component.particleRotationSpeed, 1.0f, -3600.0f, 3600.0f);
			DrawFloatRow("乱流の強さ", component.particleNoiseStrength, 0.01f, 0.0f, 1000.0f);
			DrawFloatRow("乱流の周波数", component.particleNoiseFrequency, 0.01f, 0.0f, 100.0f);

			if (component.particleMotionType == 1 ||
				component.particleMotionType == 2 ||
				component.particleMotionType == 4 ||
				component.particleMotionType == 5) {
				DrawVector3Row("運動中心", component.particleMotionCenter, 0.01f, -10000.0f, 10000.0f);
			}

			if (component.particleMotionType == 1 || component.particleMotionType == 2) {
				DrawFloatRow("角速度", component.particleAngularSpeed, 1.0f, -3600.0f, 3600.0f);
				DrawFloatRow("半径方向加速度", component.particleRadialAcceleration, 0.1f, -1000.0f, 1000.0f);
			}

			if (component.particleMotionType == 3 ||
				component.particleMotionType == 5 ||
				component.particleMotionType == 8) {
				DrawFloatRow("波の振幅", component.particleWaveAmplitude, 0.1f, 0.0f, 1000.0f);
				DrawFloatRow("波の周波数", component.particleWaveFrequency, 0.01f, 0.0f, 100.0f);
			}

			if (component.particleMotionType == 4) {
				DrawFloatRow("吸引力", component.particleAttractorStrength, 0.1f, 0.0f, 1000.0f);
			}

			DrawCheckboxRow("衝突", component.particleCollision);

			if (component.particleCollision) {
				const char* collisionModeItems[] = {
					"Depth (画面内エフェクト)",
					"Physics SDF (物理オブジェクト)"};
				DrawComboRow(
					"衝突方式",
					component.collisionDetectionMode,
					collisionModeItems,
					static_cast<int32_t>(_countof(collisionModeItems)));
				DrawFloatRow("反発", component.particleCollisionBounce, 0.01f, 0.0f, 1.0f);
				DrawFloatRow("摩擦", component.particleCollisionFriction, 0.01f, 0.0f, 1.0f);
			}
		}

		if (ImGui::CollapsingHeader("寿命による見た目", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawFloatRow("開始サイズ", component.particleSize, 0.01f, 0.001f, 100.0f);
			DrawFloatRow("終了サイズ", component.particleEndSize, 0.01f, 0.0f, 100.0f);
			DrawFloatRow("サイズのばらつき", component.particleSizeRandomness, 0.01f, 0.0f, 1.0f);
			DrawColor3Row("開始色", component.color);
			DrawColor3Row("終了色", component.particleEndColor);
			DrawFloatRow("開始アルファ", component.particleStartAlpha, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("終了アルファ", component.particleEndAlpha, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("放射強度", component.particleEmissionStrength, 0.05f, 0.0f, 1000.0f);
		}

		if (ImGui::CollapsingHeader("描画モデル")) {
			const char* billboardModeItems[] = {
				"カメラ正対",
				"Y軸固定",
				"速度方向",
				"World XY固定"
			};
			component.particleBillboardMode = (std::clamp)(component.particleBillboardMode, 0, 3);
			DrawComboRow(
				"板の向き",
				component.particleBillboardMode,
				billboardModeItems,
				static_cast<int32_t>(_countof(billboardModeItems)));

			if (component.particleBillboardMode == 2) {
				DrawFloatRow("速度方向の長さ", component.particleBillboardStretch, 0.05f, 0.01f, 100.0f);
			}

			DrawStringInputRow("FBX / OBJ", component.particleRenderAssetPath);

			if (!context.selectedAssetPath.empty() &&
				(EditorAssetUtility::HasExtension(context.selectedAssetPath, ".fbx") ||
				 EditorAssetUtility::HasExtension(context.selectedAssetPath, ".obj")) &&
				ImGui::Button("選択中モデルを Particle に設定", ImVec2(-1.0f, 0.0f))) {
				component.particleRenderAssetPath = context.selectedAssetPath;
			}

			DrawTextRow(
				"動作",
				component.particleRenderAssetPath.empty()
					? "未設定時は選択した向きの板ポリゴンを GPU インスタンシングします。"
					: "指定モデルはBillboard設定を使わず、3D形状としてGPUインスタンシングします。");
		}

		if (context.runtimeManager.IsPlaying()) {
			if (ImGui::Button("エフェクトを再生", ImVec2(-1.0f, 0.0f))) {
				context.runtimeManager.PlayEffect(gameObject.id);
			}

			if (ImGui::Button("新規発生を停止", ImVec2(-1.0f, 0.0f))) {
				context.runtimeManager.StopEffect(gameObject.id);
			}

			DrawTextRow(
				"現在の生存数",
				std::to_string(context.runtimeManager.GetAliveEffectCount(gameObject.id)).c_str());
		}
	}

	void DrawVisualEffectComponent(
		EditorInspectorPanelContext& context,
		EditorGameObject& gameObject,
		EditorComponent& component) {
		DrawParticleSystemComponent(
			context,
			gameObject,
			component,
			"Animation Event や C++ Script から再生できる、再利用可能な Visual Effect Emitter です。");
	}

	void DrawTrailRendererComponent(EditorComponent& component) {
		DrawRendererComponent(component, "Trail Particle");
		DrawSubHeader("軌跡");
		DrawFloatRow("太さ", component.particleSize, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("終端の太さ", component.particleEndSize, 0.01f, 0.0f, 100.0f);
		DrawFloatRow("残る秒数", component.particleLifetime, 0.01f, 0.01f, 60.0f);
		DrawFloatRow("毎秒の分割数", component.particleRate, 1.0f, 1.0f, 1000.0f);
		DrawColor3Row("終端色", component.particleEndColor);
		DrawFloatRow("終端透明度", component.particleEndAlpha, 0.01f, 0.0f, 1.0f);
	}

	void DrawLineRendererComponent(EditorComponent& component) {
		DrawRendererComponent(component, "Line Sprite");
		DrawSubHeader("線");
		DrawVector3Row("表示サイズ", component.colliderSize, 0.01f, 0.01f, 10000.0f);
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

		DrawTextRow("説明", "反射方式を切り替えます。材質の反射・屈折率を基礎反射率に使い、この強さで反射像の寄与を調整します。");
		DrawFloatRow("反射像の強さ", component.intensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("反射の粗さ", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 1000.0f);
	}

	void DrawPostProcessComponent(EditorComponent& component) {
		DrawTextRow("説明", "画面全体のポストプロセス効果を調整します。このコンポーネントを追加した GameObject の設定が有効になります。");
		const char* aaItems[] = {"None", "FXAA", "SMAA", "Temporal"};
		DrawComboRow("アンチエイリアス", component.aaMode, aaItems, static_cast<int32_t>(_countof(aaItems)));
		if (component.aaMode == 2) {
			DrawFloatRow("SMAA しきい値", component.smaaThreshold, 0.001f, 0.001f, 0.5f);
			DrawFloatRow("SMAA 角丸め", component.smaaCornerRounding, 1.0f, 0.0f, 100.0f);
		}
		if (component.aaMode == 3) {
			DrawFloatRow("Temporal シャープ", component.temporalSharpness, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("Temporal 履歴ブレンド", component.temporalBlendRatio, 0.01f, 0.0f, 0.98f);
		}
		DrawCheckboxRow("SSR", component.ssrEnabled);
		ImGui::Separator();
		DrawTextRow("グレア", "明るい部分から、にじみ・ゴースト・光条・放射状の光を生成します。");
		const char* glareItems[] = {
			"無効",
			"ブルーム",
			"ゴースト",
			"光の筋",
			"フォググロー",
			"単純な星型",
			"サンビーム",
			"カーネル"
		};
		if (component.glareModeMask == 0 &&
			component.glareMode > 0 &&
			component.glareMode < static_cast<int32_t>(_countof(glareItems))) {
			component.glareModeMask = 1 << component.glareMode;
		}

		ImGui::PushID("グレア追加");
		if (BeginPropertyTable("GlareAddRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("効果を追加");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);

			if (ImGui::BeginCombo("##Value", "追加するグレア")) {
				for (int32_t glareModeIndex = 1;
					glareModeIndex < static_cast<int32_t>(_countof(glareItems));
					glareModeIndex++) {
					const int32_t glareModeBit = 1 << glareModeIndex;
					const bool isAlreadyAdded = (component.glareModeMask & glareModeBit) != 0;

					if (ImGui::Selectable(
						glareItems[glareModeIndex],
						isAlreadyAdded)) {
						component.glareModeMask |= glareModeBit;
					}
				}

				ImGui::EndCombo();
			}

			ImGui::EndTable();
		}

		ImGui::PopID();

		if (component.glareModeMask == 0) {
			DrawTextRow("追加済みグレア", "なし");
		}
		else {
			DrawTextRow("合成順", "追加済みのグレアを、下に並んでいる上から順番に直列合成します。");
			DrawFloatRow("明部しきい値", component.bloomThreshold, 0.01f, 0.0f, 10.0f);
			DrawFloatRow("しきい値遷移", component.bloomSoftKnee, 0.01f, 0.0f, 1.0f);

			for (int32_t glareModeIndex = 1;
				glareModeIndex < static_cast<int32_t>(_countof(glareItems));
				glareModeIndex++) {
				const int32_t glareModeBit = 1 << glareModeIndex;

				if ((component.glareModeMask & glareModeBit) == 0) {
					continue;
				}

				ImGui::PushID(glareItems[glareModeIndex]);
				const bool isOpen = ImGui::TreeNodeEx(
					glareItems[glareModeIndex],
					ImGuiTreeNodeFlags_DefaultOpen |
					ImGuiTreeNodeFlags_Framed |
					ImGuiTreeNodeFlags_SpanAvailWidth);

				if (isOpen) {
					const size_t glareArrayIndex = static_cast<size_t>(glareModeIndex);
					if (ImGui::Button("このグレアを削除", ImVec2(-1.0f, 0.0f))) {
						component.glareModeMask &= ~glareModeBit;
					}

					DrawFloatRow("強さ", component.glareIntensityByMode[glareArrayIndex], 0.01f, 0.0f, 10.0f);
					DrawColor3Row("色", component.glareColorByMode[glareArrayIndex]);

					if (glareModeIndex == 1) {
						DrawFloatRow("にじみ", component.glareSizeByMode[glareArrayIndex], 0.01f, 0.0f, 1.0f);
					}

					if (glareModeIndex >= 2) {
						const char* sizeLabel = glareModeIndex == 6 ? "長さ" : "広がり";
						DrawFloatRow(sizeLabel, component.glareSizeByMode[glareArrayIndex], 0.01f, 0.1f, 8.0f);
						DrawFloatRow("減衰", component.glareFadeByMode[glareArrayIndex], 0.01f, 0.0f, 1.0f);
					}

					if (glareModeIndex == 2 || glareModeIndex == 3 || glareModeIndex == 5) {
						DrawFloatRow("角度", component.glareAngleByMode[glareArrayIndex], 1.0f, -180.0f, 180.0f);
						DrawFloatRow("色ずれ", component.glareColorModulationByMode[glareArrayIndex], 0.01f, 0.0f, 1.0f);
					}

					if (glareModeIndex == 3 || glareModeIndex == 5) {
						if (DrawIntRow("光条数", component.glareStreakCountByMode[glareArrayIndex])) {
							component.glareStreakCountByMode[glareArrayIndex] =
								(std::clamp)(component.glareStreakCountByMode[glareArrayIndex], 2, 8);
						}
					}

					if (glareModeIndex == 6) {
						DrawFloatRow("光源位置 X", component.glareCenterByMode[glareArrayIndex].x, 0.01f, 0.0f, 1.0f);
						DrawFloatRow("光源位置 Y", component.glareCenterByMode[glareArrayIndex].y, 0.01f, 0.0f, 1.0f);
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}
		}

		component.glareMode = 0;
		for (int32_t glareModeIndex = 1; glareModeIndex < static_cast<int32_t>(_countof(glareItems)); glareModeIndex++) {
			if ((component.glareModeMask & (1 << glareModeIndex)) != 0) {
				component.glareMode = glareModeIndex;
				break;
			}
		}

		ImGui::Separator();
		DrawTextRow("フィルター", "画面へ畳み込みフィルターを適用します。輪郭抽出系は強さで元画像との混合量を調整できます。");
		const char* filterItems[] = {
			"無効",
			"ソフト化",
			"ボックスシャープ",
			"ダイヤモンドシャープ",
			"ラプラス",
			"ソーベル",
			"プリウィット",
			"キルシュ",
			"影"
		};
		if (component.filterModeMask == 0 &&
			component.filterMode > 0 &&
			component.filterMode < static_cast<int32_t>(_countof(filterItems))) {
			component.filterModeMask = 1 << component.filterMode;
		}

		ImGui::PushID("フィルター追加");
		if (BeginPropertyTable("FilterAddRow", 2)) {
			SetupTwoColumnPropertyTable();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("効果を追加");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);

			if (ImGui::BeginCombo("##Value", "追加するフィルター")) {
				for (int32_t filterModeIndex = 1;
					filterModeIndex < static_cast<int32_t>(_countof(filterItems));
					filterModeIndex++) {
					const int32_t filterModeBit = 1 << filterModeIndex;
					const bool isAlreadyAdded = (component.filterModeMask & filterModeBit) != 0;

					if (ImGui::Selectable(
						filterItems[filterModeIndex],
						isAlreadyAdded)) {
						component.filterModeMask |= filterModeBit;
					}
				}

				ImGui::EndCombo();
			}

			ImGui::EndTable();
		}
		ImGui::PopID();

		if (component.filterModeMask == 0) {
			DrawTextRow("追加済みフィルター", "なし");
		}
		else {
			DrawTextRow("合成順", "追加済みのフィルターを、下に並んでいる上から順番に直列合成します。");

			for (int32_t filterModeIndex = 1;
				filterModeIndex < static_cast<int32_t>(_countof(filterItems));
				filterModeIndex++) {
				const int32_t filterModeBit = 1 << filterModeIndex;

				if ((component.filterModeMask & filterModeBit) == 0) {
					continue;
				}

				ImGui::PushID(filterItems[filterModeIndex]);
				const bool isOpen = ImGui::TreeNodeEx(
					filterItems[filterModeIndex],
					ImGuiTreeNodeFlags_DefaultOpen |
					ImGuiTreeNodeFlags_Framed |
					ImGuiTreeNodeFlags_SpanAvailWidth);

				if (isOpen) {
					const size_t filterArrayIndex = static_cast<size_t>(filterModeIndex);
					if (ImGui::Button("このフィルターを削除", ImVec2(-1.0f, 0.0f))) {
						component.filterModeMask &= ~filterModeBit;
					}

					DrawFloatRow("強さ", component.filterStrengthByMode[filterArrayIndex], 0.01f, 0.0f, 2.0f);
					DrawColor3Row("色", component.filterColorByMode[filterArrayIndex]);
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
		}

		component.filterMode = 0;
		for (int32_t filterModeIndex = 1;
			filterModeIndex < static_cast<int32_t>(_countof(filterItems));
			filterModeIndex++) {
			if ((component.filterModeMask & (1 << filterModeIndex)) != 0) {
				component.filterMode = filterModeIndex;
				break;
			}
		}

		ImGui::Separator();
		DrawFloatRow("最終明るさ", component.finalBrightness, 0.01f, 0.0f, 4.0f);
		ImGui::Separator();
		DrawTextRow("トーンマップ", "トーンマッピングと最終合成の設定");
		const char* tmItems[] = {"Reinhard", "Filmic", "Timothy", "Uncharted2", "ACES"};
		DrawComboRow("トーンマップ", component.compositeToneMappingMode, tmItems, static_cast<int32_t>(_countof(tmItems)));
		DrawFloatRow("露出", component.compositeExposure, 0.01f, 0.0f, 8.0f);
		DrawCheckboxRow("自動露出", component.compositeAutoExposureEnabled);

		if (component.compositeAutoExposureEnabled) {
			DrawFloatRow("自動露出 下限", component.compositeMinimumExposure, 0.01f, 0.01f, 8.0f);
			DrawFloatRow("自動露出 上限", component.compositeMaximumExposure, 0.01f, 0.01f, 16.0f);
			DrawFloatRow("露出追従速度", component.compositeExposureAdaptationSpeed, 0.05f, 0.0f, 10.0f);
			DrawFloatRow("基準輝度", component.compositeTargetLuminance, 0.01f, 0.01f, 1.0f);
		}

		DrawFloatRow("ホワイトポイント", component.compositeWhitePoint, 0.1f, 0.1f, 20.0f);
		DrawFloatRow("グレア合成強さ", component.compositeBloomIntensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("彩度", component.compositeSaturation, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("コントラスト", component.compositeContrast, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("色温度", component.compositeTemperature, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("Tint", component.compositeTint, 0.01f, -1.0f, 1.0f);
		DrawVector3Row("Lift", component.compositeLift, 0.005f, -1.0f, 1.0f);
		DrawFloatRow("Gamma", component.compositeGamma, 0.01f, 0.1f, 4.0f);
		DrawVector3Row("Gain", component.compositeGain, 0.01f, 0.0f, 4.0f);
		ImGui::Separator();
		DrawTextRow("ディテール", "近傍輝度から立体感を補い、最終出力の階調縞を軽減します。");
		DrawFloatRow("局所コントラスト", component.compositeLocalContrast, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("出力ディザリング", component.compositeOutputDither, 0.05f, 0.0f, 2.0f);
		DrawCheckboxRow("SSGI", component.compositeSsgiEnabled);
		if (component.compositeSsgiEnabled) {
			DrawFloatRow("SSGI強度", component.compositeSsgiIntensity, 0.01f, 0.0f, 4.0f);
			DrawFloatRow("SSGI半径", component.compositeSsgiRadiusPixels, 1.0f, 1.0f, 128.0f);
		}
		DrawStringInputRow("カラーLUT画像", component.compositeColorLutAssetPath);
		DrawFloatRow("カラーLUT強度", component.compositeColorLutStrength, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("ビネット", component.compositeVignetteStrength, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("ビネット半径", component.compositeVignetteRadius, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("フィルムグレイン", component.compositeFilmGrain, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("色収差", component.compositeChromaticAberration, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("AO強度", component.compositeAmbientOcclusionStrength, 0.01f, 0.0f, 2.0f);
		ImGui::Separator();
		DrawTextRow(
			"Final Composite診断",
			"sceneColorを50%グレーへ強制し、LocalContrast/Bloomのどちらが元画像の"
			"模様(泡・Sun反射など)を再注入しているか切り分けます。0=通常。");
		const char* compositeDebugViewItems[] = {
			"0 通常",
			"1 基準のみ (LocalContrast/Bloomとも無効)",
			"2 元画像再参照のみ (LocalContrastのみ有効)",
			"3 ブルームのみ (Bloomのみ有効)",
			"4 両方 (LocalContrast+Bloom)",
		};
		component.compositeDebugView = (std::clamp)(
			component.compositeDebugView, 0, static_cast<int32_t>(_countof(compositeDebugViewItems)) - 1);
		DrawComboRow(
			"診断表示",
			component.compositeDebugView,
			compositeDebugViewItems,
			static_cast<int32_t>(_countof(compositeDebugViewItems)));
	}

	void DrawEnvironmentComponent(EditorComponent& component) {
		DrawTextRow("説明", "環境光・スカイ・HDRIの設定を行います。空の色・明るさ・反射への影響を調整します。");
		DrawStringInputRow("環境画像", component.assetPath);
		DrawCheckboxRow("環境画像を使用", component.environmentTextureEnabled);
		DrawFloatRow("露出", component.intensity, 0.01f, 0.0f, 8.0f);
		DrawColor3Row("上空の色", component.color);
		DrawColor3Row("地平線の色", component.skyLowerColor);
		DrawFloatRow("地平線のぼかし", component.roughness, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("反射への寄与", component.reflectionStrength, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("環境光", component.metallic, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("放射の強さ", component.emissionStrength, 0.01f, 0.0f, 10.0f);
		DrawFloatRow("環境テクスチャ回転", component.environmentTextureRotation, 0.01f, 0.0f, 6.2832f);
		DrawFloatRow("MIPバイアス", component.environmentTextureMipBias, 0.01f, 0.0f, 4.0f);
		if (ImGui::TreeNodeEx("体積雲", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawCheckboxRow("体積雲を使用", component.volumetricCloudEnabled);
			DrawFloatRow("雲量", component.volumetricCloudCoverage, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("密度", component.volumetricCloudDensity, 0.01f, 0.0f, 4.0f);
			DrawFloatRow("スケール", component.volumetricCloudScale, 0.0001f, 0.0001f, 1.0f);
			DrawFloatRow("移動速度", component.volumetricCloudSpeed, 0.01f, -1000.0f, 1000.0f);
			DrawFloatRow("高度", component.volumetricCloudHeight, 1.0f, -10000.0f, 100000.0f);
			DrawFloatRow("厚さ", component.volumetricCloudThickness, 1.0f, 1.0f, 100000.0f);
			DrawFloatRow("光吸収", component.volumetricCloudLightAbsorption, 0.01f, 0.0f, 8.0f);
			DrawFloatRow("銀縁", component.volumetricCloudSilverLining, 0.01f, 0.0f, 4.0f);
			DrawColor3Row("雲の色", component.volumetricCloudColor);
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("光の筋(ボリュメトリックライト)", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawCheckboxRow("光の筋を使用", component.volumetricLightEnabled);
			DrawFloatRow("強さ", component.volumetricLightIntensity, 0.01f, 0.0f, 4.0f);
			DrawFloatRow("筋の鋭さ", component.volumetricLightAnisotropy, 0.01f, 0.0f, 0.95f);
			DrawFloatRow("到達距離", component.volumetricLightDistance, 0.5f, 1.0f, 500.0f);
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("熱気・遠景揺らぎ", ImGuiTreeNodeFlags_DefaultOpen)) {
			DrawFloatRow("熱気の強さ", component.environmentHeatIntensity, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("地平線中心", component.environmentHeatHorizonCenter, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("地平線範囲", component.environmentHeatHorizonWidth, 0.01f, 0.01f, 1.0f);
			DrawFloatRow("太陽方向の影響", component.environmentHeatSunInfluence, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("歪みスケール", component.environmentHeatDistortionScale, 0.01f, 0.01f, 4.0f);
			ImGui::TreePop();
		}
	}

	void DrawSunPortalComponent(EditorComponent& component) {
		DrawTextRow(
			"説明",
			"窓や開口部に置くと、その面を簡易的なArea Lightとして扱い、"
			"Sunが正面から当たっている間だけ室内側へ光を足します。"
			"GameObjectの向き(+Z)がPortalの外向き(Sun側)になります。");
		DrawFloatRow("強さ", component.intensity, 0.01f, 0.0f, 4.0f);
		DrawColor3Row("色味", component.color);
		DrawFloatRow("半幅", component.colliderSize.x, 0.05f, 0.05f, 50.0f);
		DrawFloatRow("半高", component.colliderSize.y, 0.05f, 0.05f, 50.0f);
		DrawFloatRow("到達距離", component.colliderRadius, 0.1f, 0.1f, 100.0f);
		DrawFloatRow("奥への広がり", component.roughness, 0.01f, 0.0f, 2.0f);
	}

	void DrawCameraFollowRows(
		EditorInspectorPanelContext& context,
		EditorGameObject& gameObject,
		EditorComponent& component) {
		DrawGameObjectReferenceRow(
			context,
			gameObject,
			"追従対象",
			component.connectedGameObjectId,
			"未設定",
			false);

		if (component.connectedGameObjectId < 0) {
			return;
		}

		if (ImGui::Button("プレイヤー追従プリセット", ImVec2(-1.0f, 0.0f))) {
			gameObject.translate = {0.0f, 2.0f, -6.0f};
			gameObject.rotate = {0.0f, 0.0f, 0.0f};
			component.cameraFollowPositionSpace = 1;
			component.cameraFollowRotationMode = 1;
		}

		const char* positionSpaceItems[] = {"World固定", "対象Local"};
		component.cameraFollowPositionSpace = (std::clamp)(component.cameraFollowPositionSpace, 0, 1);
		DrawComboRow(
			"位置オフセット基準",
			component.cameraFollowPositionSpace,
			positionSpaceItems,
			static_cast<int32_t>(_countof(positionSpaceItems)));
		const char* rotationModeItems[] = {"Camera角度を固定", "対象回転を継承", "対象を見る"};
		component.cameraFollowRotationMode = (std::clamp)(component.cameraFollowRotationMode, 0, 2);
		DrawComboRow(
			"回転方式",
			component.cameraFollowRotationMode,
			rotationModeItems,
			static_cast<int32_t>(_countof(rotationModeItems)));
		DrawTextRow("位置Offset", "このCamera GameObjectの位置をOffsetとして使います。");
		DrawTextRow("回転Offset", "回転継承・対象を見るではCamera GameObjectの回転を追加Offsetとして使います。");
	}

	void DrawCameraComponent(EditorInspectorPanelContext& context, EditorGameObject& gameObject, EditorComponent& component) {
		DrawTextRow("説明", "GameView に描く実行カメラです。追従対象IDを設定すると、その GameObject へ簡易追従します。");
		DrawCameraFollowRows(context, gameObject, component);
		DrawIntRow("優先度", component.cameraPriority);
		const char* projectionItems[] = {"Perspective", "Orthographic"};
		DrawComboRow("投影", component.cameraProjectionMode, projectionItems, static_cast<int32_t>(_countof(projectionItems)));
		DrawFloatRow("視野角", component.cameraFieldOfView, 1.0f, 1.0f, 179.0f);
		DrawFloatRow("ニアクリップ", component.cameraNearClip, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("ファークリップ", component.cameraFarClip, 1.0f, 0.1f, 10000.0f);
		DrawFloatRow("露出補正 (EV)", component.cameraExposure, 0.1f, -10.0f, 10.0f);
		ImGui::Separator();
		DrawCheckboxRow("被写界深度", component.cameraDofEnabled);
		if (component.cameraDofEnabled) {
			DrawFloatRow("フォーカス距離", component.cameraDofFocusDistance, 0.1f, 0.1f, 1000.0f);
			DrawFloatRow("絞り", component.cameraDofAperture, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("焦点距離 (mm)", component.cameraDofFocalLength, 1.0f, 1.0f, 300.0f);
		}
		ImGui::Separator();
		DrawCheckboxRow("モーションブラー", component.cameraMotionBlurEnabled);
		if (component.cameraMotionBlurEnabled) {
			DrawFloatRow("ブラー強度", component.cameraMotionBlurIntensity, 0.01f, 0.0f, 1.0f);
		}

		ImGui::SeparatorText("ゲーム中の視点操作");
		DrawCheckboxRow("視点操作を有効化", component.cameraInputEnabled);

		if (component.cameraInputEnabled) {
			DrawTextRow("説明", "標準操作はここで設定し、特殊な動きはInputとCamera TransformのScript APIで上書きできます。");
			const char* cameraInputStyleItems[] = {"FreeLook", "Orbit"};
			DrawComboRow(
				"操作形式",
				component.cameraInputStyle,
				cameraInputStyleItems,
				static_cast<int32_t>(_countof(cameraInputStyleItems)));
			const char* cameraInputActivationItems[] = {"右ボタンを押している間", "常時"};
			DrawComboRow(
				"回転入力",
				component.cameraInputActivation,
				cameraInputActivationItems,
				static_cast<int32_t>(_countof(cameraInputActivationItems)));
			DrawFloatRow("マウス感度", component.cameraInputLookSensitivity, 0.0001f, 0.0f, 0.1f);
			DrawCheckboxRow("Y軸反転", component.cameraInputInvertY);
			DrawFloatRow("Pitch最小角度", component.cameraInputMinimumPitchDegrees, 1.0f, -89.0f, 89.0f);
			DrawFloatRow("Pitch最大角度", component.cameraInputMaximumPitchDegrees, 1.0f, -89.0f, 89.0f);
			component.cameraInputMaximumPitchDegrees = (std::max)(
				component.cameraInputMaximumPitchDegrees,
				component.cameraInputMinimumPitchDegrees);
			DrawCheckboxRow("操作中カーソル固定", component.cameraInputLockCursor);
			DrawCheckboxRow("操作中カーソル非表示", component.cameraInputHideCursor);

			if (component.cameraInputStyle == 0) {
				DrawCheckboxRow("WASD/QE移動", component.cameraInputMovementEnabled);

				if (component.cameraInputMovementEnabled) {
					DrawFloatRow("移動速度", component.cameraInputMoveSpeed, 0.1f, 0.0f, 10000.0f);
					DrawFloatRow("Shift倍率", component.cameraInputFastMultiplier, 0.1f, 1.0f, 100.0f);
				}
			}
			else {
				DrawGameObjectReferenceRow(
					context,
					gameObject,
					"Orbit中心",
					component.cameraInputTargetGameObjectId,
					"Cameraの接続先",
					true);
				DrawVector3Row("中心Offset", component.cameraInputPivotOffset, 0.1f, -10000.0f, 10000.0f);
				DrawFloatRow("距離", component.cameraInputOrbitDistance, 0.1f, 0.01f, 10000.0f);
				DrawFloatRow("最小距離", component.cameraInputMinimumDistance, 0.1f, 0.01f, 10000.0f);
				DrawFloatRow("最大距離", component.cameraInputMaximumDistance, 0.1f, 0.01f, 10000.0f);
				component.cameraInputMaximumDistance = (std::max)(
					component.cameraInputMaximumDistance,
					component.cameraInputMinimumDistance);
				DrawFloatRow("ホイールZoom速度", component.cameraInputZoomSpeed, 0.1f, 0.0f, 1000.0f);
			}
		}
	}

	void DrawLightProbeGroupComponent(EditorComponent& component) {
		DrawTextRow(
			"説明",
			"間接光(GI)用のライトプローブを直方体グリッドへ等間隔に置きます。"
			"GameObjectの位置がグリッドの中心です。各Probeから周囲を撮影して"
			"間接光を焼き、動的な物体もそこから間接光を受け取ります。");
		DrawCheckboxRow("GIを使用", component.environmentTextureEnabled);
		DrawVector3Row("範囲(半径)", component.colliderSize, 0.1f, 0.5f, 500.0f);
		DrawFloatRow("Probe間隔", component.colliderRadius, 0.1f, 0.5f, 50.0f);
		DrawFloatRow("GIの強さ", component.intensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("法線バイアス", component.roughness, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("撮影距離", component.reflectionStrength, 1.0f, 1.0f, 500.0f);
		DrawFloatRow("時間平滑", component.metallic, 0.01f, 0.0f, 0.99f);

		// 設定から実際に生成されるProbe数を出しておく。上限を超えるとGIは無効になる。
		const float spacing = (std::max)(component.colliderRadius, 0.5f);
		int64_t probeCount = 1;

		for (int32_t axisIndex = 0; axisIndex < 3; axisIndex++) {
			const float halfExtent = axisIndex == 0
				? component.colliderSize.x
				: (axisIndex == 1 ? component.colliderSize.y : component.colliderSize.z);
			const int64_t axisCount = static_cast<int64_t>(
				std::floor((std::max)(halfExtent, 0.0f) * 2.0f / spacing)) + 1;
			probeCount *= (std::max)(axisCount, static_cast<int64_t>(1));
		}

		DrawTextRow(
			"Probe数",
			std::format(
				"{} 個 (上限 {} 個)",
				probeCount,
				EditorLightProbeManager::kMaxProbeCount).c_str());

		if (probeCount > static_cast<int64_t>(EditorLightProbeManager::kMaxProbeCount)) {
			DrawTextRow("警告", "上限を超えているためGIは無効です。間隔を広げてください。");
		}
	}

	void DrawLightProbeProxyVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "大きな動的物体向けにライトプローブを補間するコンポーネントです。");
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 1000.0f);
		DrawFloatRow("反射寄与", component.intensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("ぼかし", component.roughness, 0.01f, 0.0f, 1.0f);
	}

	void DrawVolumeComponent(EditorComponent& component) {
		DrawTextRow("説明", "既存 PostProcess パスへ合成する Volume 設定です。");
		DrawFloatRow("重み", component.intensity, 0.01f, 0.0f, 1.0f);
		DrawPostProcessComponent(component);
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
		DrawStringInputRow("テクスチャ", component.assetPath);
		DrawFloatRow("明るさ", component.intensity, 0.01f, 0.0f, 10.0f);
		DrawColor3Row("色", component.color);
		DrawFloatRow("透明度", component.alpha, 0.01f, 0.0f, 1.0f);
	}

	void DrawProjectorComponent(EditorComponent& component) {
		DrawTextRow("説明", "Texture や影を Scene に投影するコンポーネントです。");
		DrawStringInputRow("テクスチャ", component.assetPath);
		DrawFloatRow("視野角", component.intensity, 1.0f, 1.0f, 180.0f);
		DrawVector3Row("投影サイズ", component.colliderSize, 0.01f, 0.01f, 1000.0f);
		DrawFloatRow("透明度", component.alpha, 0.01f, 0.0f, 1.0f);
	}

	void DrawDecalProjectorComponent(EditorComponent& component) {
		DrawTextRow("説明", "URP / HDRP の Decal を投影するコンポーネントです。");
		DrawStringInputRow("テクスチャ", component.assetPath);
		DrawVector3Row("サイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("透明度", component.alpha, 0.01f, 0.0f, 1.0f);
	}

	void DrawTerrainComponent(EditorComponent& component) {
		DrawTextRow("説明", "HeightMap から地形を生成し、距離に応じてメッシュLODを切り替えます。");
		DrawStringInputRow("Height Map", component.assetPath);
		DrawVector3Row("サイズ X / 高さ / Z", component.colliderSize, 1.0f, 0.01f, 10000.0f);

		if (DrawIntRow("最高LOD解像度", component.oceanGridResolution)) {
			component.oceanGridResolution = (std::clamp)(component.oceanGridResolution, 16, 256);
		}

		DrawTextRow("LOD", "近距離 / 中距離 / 遠距離の3段階。Shadowは一段低いLODを使用します。");
	}

	void DrawFoliageComponent(EditorComponent& component) {
		DrawTextRow("説明", "DensityMap とGPU Instancingで草木を配置し、距離で描画密度を落とします。");
		DrawStringInputRow("Density Map", component.assetPath);
		DrawVector3Row("配置範囲", component.colliderSize, 1.0f, 1.0f, 10000.0f);
		DrawFloatRow("密度", component.intensity, 0.01f, 0.0f, 1.0f);

		if (DrawIntRow("最大Instance数", component.particleMaxCount)) {
			component.particleMaxCount = (std::clamp)(component.particleMaxCount, 1, 65535);
		}

		DrawFloatRow("LOD距離", component.colliderRadius, 1.0f, 1.0f, 10000.0f);
		DrawSubHeader("風");
		DrawVector2Row("風向き", component.oceanPrimaryDirection, 0.01f, -1.0f, 1.0f);
		DrawFloatRow("揺れ幅", component.oceanWaveHeight, 0.01f, 0.0f, 5.0f);
		DrawFloatRow("風速", component.oceanWindSpeed, 0.1f, 0.0f, 100.0f);
		DrawFloatRow("空間周波数", component.oceanWaveLength, 0.01f, 0.01f, 100.0f);
		DrawFloatRow("時間倍率", component.oceanTimeScale, 0.01f, 0.0f, 10.0f);
	}

	void DrawTilemapComponent(EditorComponent& component) {
		DrawTextRow("説明", "2D Tile を配置するコンポーネントです。");
		DrawStringInputRow("タイル画像", component.assetPath);
		DrawVector3Row("サイズ", component.colliderSize, 1.0f, 1.0f, 1000.0f);
	}

	void DrawGridComponent(EditorComponent& component) {
		DrawTextRow("説明", "Tilemap の親になる Grid コンポーネントです。");
		DrawVector3Row("セルサイズ", component.colliderSize, 0.01f, 0.01f, 100.0f);
	}

	void DrawOceanComponent(EditorComponent& component) {
		//============================================================
		// FFocean3D を基にした Ocean の調整値
		//============================================================

		DrawTextRow("説明", "外部モデル不要の海面です。Scene View と Game View の両方へ同じ波を描画します。");

		DrawTextRow("メッシュ", "16～2048の2冪。近傍密度を維持し、外周を追加頂点なしで地平線方向へ広げます。");
		const int32_t clampedPreviousResolution =
			(std::clamp)(component.oceanGridResolution, 16, 2048);
		int32_t previousGridResolution = 16;

		while (previousGridResolution <= 1024 &&
			previousGridResolution * 2 <= clampedPreviousResolution) {
			previousGridResolution *= 2;
		}

		component.oceanGridResolution = previousGridResolution;

		if (DrawIntRow("グリッド解像度", component.oceanGridResolution)) {
			if (component.oceanGridResolution == previousGridResolution + 1) {
				component.oceanGridResolution =
					(std::min)(previousGridResolution * 2, 2048);
			}
			else if (component.oceanGridResolution == previousGridResolution - 1) {
				component.oceanGridResolution =
					(std::max)(previousGridResolution / 2, 16);
			}
			else {
				const int32_t clampedResolution =
					(std::clamp)(component.oceanGridResolution, 16, 2048);
				int32_t normalizedResolution = 16;

				while (normalizedResolution <= 1024 &&
					normalizedResolution * 2 <= clampedResolution) {
					normalizedResolution *= 2;
				}

				component.oceanGridResolution = normalizedResolution;
			}
		}
		DrawFloatRow("海面サイズ", component.oceanSize, 1.0f, 1.0f, 20000.0f);

		DrawTextRow("主波", "波高・波長・速度と XZ 方向を調整します。");
		DrawFloatRow("波の高さ", component.oceanWaveHeight, 0.05f, 0.0f, 2000.0f);
		DrawFloatRow("最大波高", component.oceanMaxWaveHeight, 0.05f, 0.0f, 4000.0f);
		DrawFloatRow("波長", component.oceanWaveLength, 0.1f, 0.1f, 10000.0f);
		DrawFloatRow("波の速度", component.oceanWaveSpeed, 0.01f, -10.0f, 10.0f);
		DrawFloatRow("時間倍率", component.oceanTimeScale, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("Choppiness", component.oceanChoppiness, 0.01f, -4.0f, 4.0f);
		DrawVector2Row("主波方向 XZ", component.oceanPrimaryDirection, 0.01f, -100.0f, 100.0f);

		DrawTextRow("副波・細波", "別方向の波を重ね、均一な波並びを崩します。");
		DrawVector2Row("副波方向 XZ", component.oceanSecondaryDirection, 0.01f, -100.0f, 100.0f);
		DrawFloatRow("副波の強さ", component.oceanSecondaryWaveScale, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("細波の波長比", component.oceanRippleScale, 0.005f, 0.02f, 1.0f);
		DrawFloatRow("細波の強さ", component.oceanRippleStrength, 0.005f, 0.0f, 1.0f);

		DrawTextRow("波スペクトル", "16成分を長いうねり・風浪・細波へ分け、有限水深の分散則で動かします。");
		DrawFloatRow("風速", component.oceanWindSpeed, 0.1f, 0.1f, 80.0f);
		DrawFloatRow("水深", component.oceanWaterDepth, 0.5f, 0.1f, 5000.0f);
		DrawFloatRow("方向分散", component.oceanDirectionSpread, 0.01f, 0.0f, 3.14159f);
		DrawFloatRow("うねりの強さ", component.oceanSwellStrength, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("スペクトルシード", component.oceanSpectrumSeed, 1.0f, 0.0f, 65535.0f);
		DrawFloatRow("波頭の尖り", component.oceanCrestSharpness, 0.01f, 0.0f, 1.0f);

		DrawTextRow("海面材質", "微細法線、圧縮泡、吸収、Fresnel 反射と屈折を合成します。");
		DrawFloatRow("泡の強さ", component.oceanFoamStrength, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("泡の閾値", component.oceanFoamThreshold, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("粗さ", component.oceanRoughness, 0.01f, 0.035f, 1.0f);
		DrawFloatRow("反射", component.oceanReflectionStrength, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("屈折", component.transmission, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("微細法線", component.oceanDetailNormalStrength, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("吸収距離", component.oceanAbsorptionDistance, 0.1f, 0.1f, 500.0f);
		DrawFloatRow("屈折の歪み", component.oceanRefractionDistortion, 0.005f, 0.0f, 1.0f);
		DrawTextRow("水深色", "水深が吸収距離以下なら浅瀬色、深くなるほど深海色を強くします。");
		DrawColor3Row("浅瀬色", component.oceanShallowColor);
		DrawColor3Row("深海色", component.oceanDeepColor);

		DrawTextRow("Water Lighting", "SUNの各項目が海面へどれだけ効くかを個別に調整します。0で完全に無効化します。");
		DrawFloatRow("太陽Diffuse影響", component.oceanSunDiffuseInfluence, 0.01f, 0.0f, 3.0f);
		DrawFloatRow("太陽Diffuse下限", component.oceanDiffuseFloor, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("太陽Specular影響", component.oceanSunSpecularInfluence, 0.01f, 0.0f, 3.0f);
		DrawFloatRow("太陽Glitter影響", component.oceanSunGlitterInfluence, 0.01f, 0.0f, 3.0f);
		DrawFloatRow("Sky Reflection影響", component.oceanSkyReflectionInfluence, 0.01f, 0.0f, 3.0f);
		DrawFloatRow("Ambient影響", component.oceanAmbientInfluence, 0.01f, 0.0f, 3.0f);

		DrawTextRow("Wave Shape Lighting", "大波Normalと曲率で、真昼でも波頭・斜面・谷を読みやすくします。");
		DrawFloatRow("大波反射影響", component.oceanMacroReflectionInfluence, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("曲率感度", component.oceanCurvatureInfluence, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("谷の環境遮蔽", component.oceanTroughOcclusionStrength, 0.005f, 0.0f, 0.25f);
		DrawFloatRow("波頭Haze", component.oceanCrestHazeStrength, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("波頭細波増幅", component.oceanCrestDetailBoost, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("斜面屈折影響", component.oceanSlopeRefractionInfluence, 0.01f, 0.0f, 2.0f);
		DrawFloatRow("中波構造", component.oceanMediumWaveStrength, 0.01f, 0.0f, 3.0f);
		DrawFloatRow("波形の色分離", component.oceanWaveColorSeparation, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("形状による粗さ差", component.oceanShapeRoughnessVariation, 0.01f, 0.0f, 0.5f);
		DrawFloatRow("近距離Detail保持", component.oceanDetailFilterSharpness, 0.01f, 0.5f, 2.5f);
		DrawFloatRow("浅角度形状保持", component.oceanGrazingShapeVisibility, 0.01f, 0.0f, 1.0f);

		DrawTextRow(
			"Ocean Debug View",
			"海面の各成分を単体表示します。0=通常描画。シェーダは実行時コンパイルなのでリビルド不要です。");
		const char* oceanDebugViewItems[] = {
			"0 通常",
			"1 太陽Diffuse",
			"2 GGX Specular",
			"3 Sun Glitter",
			"4 Glitter反射整列",
			"5 Glitter最終Mask",
			"6 Medium Normal",
			"7 Fine Normal",
			"8 Normal差(x8)",
			"9 傾き量",
			"10 中波構造",
			"11 Foam",
			"12 Sky反射",
			"13 屈折Scene",
			"14 Caustics",
			"15 水中実体被覆",
			"16 FFT泡チャンネル",
			"17 Fine Delta Slope",
			"18 Fine Lobe Normal",
			"19 Fine Micro Roughness",
			"20 Fine Sun Specular",
			"21 Fine Env Normal Delta",
			"22 Fine Lobe OFF (比較用)",
			"23 Medium Slope (17と比較)",
			"24 Fine Env Final Delta",
			"25 Env Normal 角度差",
			"26 Fine GGX D",
			"27 Fine NdotH",
			"28 Fine Raw Lobe",
			"29 Medium参照Lobe",
			"30 ReflectDir 21",
			"31 ReflectDir 24",
			"32 ReflectDir差分",
			"33 RawEnvSample 21",
			"34 RawEnvSample 24",
			"35 RawEnvSample差分",
			"36 Compress後差分",
			"37 Delta21-24差分",
			"38 RGB Delta 21",
			"39 RGB Delta 24",
			"40 Delta21-24直接",
			"41 定数:黒",
			"42 定数:グレー",
			"43 定数:赤",
			"44 絶対量 x4",
			"45 絶対量 x64",
			"46 絶対量 x256",
		};
		static_assert(
			static_cast<int32_t>(_countof(oceanDebugViewItems)) == kOceanDebugViewCount,
			"Ocean Debug View の項目数が kOceanDebugViewCount と一致していません。");
		component.oceanDebugView = (std::clamp)(
			component.oceanDebugView,
			0,
			kOceanDebugViewCount - 1);
		DrawComboRow(
			"デバッグ表示",
			component.oceanDebugView,
			oceanDebugViewItems,
			static_cast<int32_t>(_countof(oceanDebugViewItems)));

		DrawTextRow(
			"近景Geometry品質",
			"画素変位は光学的な奥行きだけを補い、GPU細分化は実際のFFT水面形状を増やします。");
		DrawFloatRow(
			"近景Pixel変位",
			component.oceanPerPixelDisplacementStrength,
			0.01f,
			0.0f,
			0.5f);
		DrawIntRow("Pixel変位反復数", component.oceanPerPixelDisplacementSteps);
		component.oceanPerPixelDisplacementSteps = (std::clamp)(
			component.oceanPerPixelDisplacementSteps,
			1,
			6);
		DrawFloatRow(
			"Pixel変位距離",
			component.oceanPerPixelDisplacementDistance,
			1.0f,
			5.0f,
			150.0f);
		DrawCheckboxRow("GPU適応細分化", component.oceanGpuTessellationEnabled);
		DrawFloatRow(
			"細分化目標Pixel",
			component.oceanTessellationTargetPixels,
			1.0f,
			4.0f,
			64.0f);
		DrawFloatRow(
			"細分化最大係数",
			component.oceanTessellationMaximumFactor,
			1.0f,
			1.0f,
			8.0f);

		DrawTextRow("Glitter", "太陽方向へ伸びる細かいキラキラ反射(sun glitter)を調整します。");
		DrawFloatRow("グリッター強度", component.oceanGlitterIntensity, 0.01f, 0.0f, 8.0f);
		DrawFloatRow("グリッター鋭さ", component.oceanGlitterSharpness, 0.01f, 0.0f, 1.0f);
		DrawFloatRow("グリッター密度", component.oceanGlitterDensity, 0.01f, 0.0f, 4.0f);
		DrawFloatRow("グリッター開始閾値", component.oceanGlitterThreshold, 0.01f, 0.0f, 0.99f);
		DrawFloatRow("グリッター最大輝度", component.oceanGlitterMaxClamp, 0.1f, 0.1f, 64.0f);
	}

	void DrawBuoyancyComponent(
		EditorInspectorPanelContext& context,
		EditorGameObject& ownerGameObject,
		EditorComponent& component) {
		//============================================================
		// 描画と同じ GPU FFT 波面を使う船体浮力
		//============================================================

		DrawTextRow("説明", "実Colliderの排水体積と表面へ、描画と同じFFT水面の浮力・流体力を加えます。");
		DrawTextRow("形状差", "船・箱を区別せず、体積、表面法線、重心、密度から挙動差を作ります。");
		DrawTextRow("必須", "同じ GameObject に Rigidbody と Box / Convex / Mesh Collider を追加してください。");
		DrawGameObjectReferenceRow(
			context,
			ownerGameObject,
			"対象 Ocean",
			component.buoyancyOceanGameObjectId,
			"自動検出",
			false);

		DrawSubHeader("自動物理設定");
		DrawCheckboxRow("自動物理を使用", component.buoyancyAutomaticPhysicalProperties);

		if (component.buoyancyAutomaticPhysicalProperties) {
			DrawFloatRow("水密度 kg/m3", component.buoyancyWaterDensity, 1.0f, 0.0f, 1000000.0f);
			DrawFloatRow("目標水没率", component.buoyancyTargetSubmersionRatio, 0.01f, 0.01f, 0.99f);
			EditorComponent* rigidBodyComponent = nullptr;

			for (EditorComponent& ownerComponent : ownerGameObject.components) {
				if (ownerComponent.type == EditorComponentType::RigidBody) {
					rigidBodyComponent = &ownerComponent;
					break;
				}
			}

			if (rigidBodyComponent != nullptr) {
				rigidBodyComponent->automaticMassFromCollider = true;
				rigidBodyComponent->bodyDensity =
					component.buoyancyWaterDensity * component.buoyancyTargetSubmersionRatio;
				DrawTextRow("質量計算", "Collider体積 x 水密度 x 目標水没率");
			}
			else {
				DrawTextRow("設定不足", "同じGameObjectへRigidbodyを追加してください。");
			}

			DrawTextRow("自動項目", "質量・慣性・排水浮力/作用点・並進/回転付加慣性・造波抵抗・減衰・面抗力・着水衝撃");
		}
		else {
			DrawSubHeader("手動詳細設定");
			DrawVector3Row("浮力中心", component.buoyancyCenterOffset, 0.01f, -1000.0f, 1000.0f);
			DrawVector3Row("船体サイズ", component.buoyancyHullSize, 0.05f, 0.05f, 10000.0f);
			DrawFloatRow("浮力", component.buoyancyStrength, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("上下減衰", component.buoyancyDamping, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("前後の水抵抗", component.buoyancyWaterDrag, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("横方向の水抵抗", component.buoyancyLateralDrag, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("上下の水抵抗", component.buoyancyVerticalDrag, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("回転抵抗", component.buoyancyAngularDrag, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("着水衝撃", component.buoyancySlammingStrength, 0.05f, 0.0f, 100.0f);
			DrawFloatRow("波の横押し", component.buoyancyNormalInfluence, 0.01f, 0.0f, 1.0f);
		}

		DrawSubHeader("Runtime 診断");
		DrawTextRow("説明",
			"Play中に実際へ加えた各水力の大きさです。圧力の上向き成分と船体重量を比べると、"
			"高速時にどれだけ動的揚力が出ているか(Planingが効いているか)が分かります。");
		DrawReadOnlyFloatRow("船体重量 N", component.buoyancyDebugWeightForce);
		DrawReadOnlyFloatRow("浮力 N", component.buoyancyDebugBuoyancyForce);
		DrawReadOnlyFloatRow("圧力抗力 N", component.buoyancyDebugPressureDragForce);
		DrawReadOnlyFloatRow("圧力の上向き成分 N", component.buoyancyDebugPressureUpwardForce);

		if (component.buoyancyDebugWeightForce > 0.0001f) {
			// 生成レジストリへ誤ったラベルで登録されないよう、比率は局所変数で作る。
			const float upwardForceWeightRatio =
				component.buoyancyDebugPressureUpwardForce / component.buoyancyDebugWeightForce;
			DrawReadOnlyFloatRow("上向き成分 / 重量", upwardForceWeightRatio);
		}

		DrawReadOnlyFloatRow("摩擦抗力 N", component.buoyancyDebugSkinFrictionForce);
		DrawReadOnlyFloatRow("付加質量力 N", component.buoyancyDebugAddedMassForce);
		DrawReadOnlyFloatRow("着水衝撃 N", component.buoyancyDebugSlammingForce);
		DrawReadOnlyFloatRow("造波抵抗 N", component.buoyancyDebugWaveMakingResistance);
		DrawReadOnlyFloatRow("水没率", component.buoyancyDebugSubmergedRatio);
		DrawReadOnlyFloatRow("濡れ面積 m2", component.buoyancyDebugWettedArea);
		DrawReadOnlyFloatRow("Trim角 度", component.buoyancyDebugTrimAngleDegrees);
		DrawReadOnlyFloatRow("前進相対速度 m/s", component.buoyancyDebugForwardSpeed);
		DrawReadOnlyFloatRow("斜航角 度", component.buoyancyDebugSideslipAngleDegrees);
		DrawReadOnlyVector3Row(
			"付加質量Coriolis N·m",
			component.buoyancyDebugAddedMassCoriolisTorque);
	}

	void DrawComponentBody(EditorInspectorPanelContext& context, EditorGameObject& gameObject, EditorComponent& component) {
		// Component 種類ごとに Inspector の中身を分ける
		switch (component.type) {
		case EditorComponentType::MeshFilter:
			DrawModelAssetPicker(context, component.assetPath);
			break;
		case EditorComponentType::ModelRenderer:
		case EditorComponentType::SkinnedMeshRenderer:
			DrawRendererComponent(context, gameObject, component, "Lit");
			break;
		case EditorComponentType::SpriteRenderer:
			DrawRendererComponent(component, "Sprite Lit");
			break;
		case EditorComponentType::LineRenderer:
			DrawLineRendererComponent(component);
			break;
		case EditorComponentType::TrailRenderer:
			DrawTrailRendererComponent(component);
			break;
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
			DrawCameraComponent(context, gameObject, component);
			break;
		case EditorComponentType::FlareLayer:
			DrawFlareLayerComponent(component);
			break;
		case EditorComponentType::CinemachineCamera:
			DrawTextRow("状態", component.isActive ? "有効" : "無効");
			DrawTextRow("説明", "対象のWorldまたはLocal基準で位置と角度を追従するVirtual Cameraです。");
			DrawCameraFollowRows(context, gameObject, component);
			break;
		case EditorComponentType::ReflectionProbe:
			DrawReflectionProbeComponent(component);
			break;
		case EditorComponentType::PostProcess:
			DrawPostProcessComponent(component);
			break;
		case EditorComponentType::Environment:
			DrawEnvironmentComponent(component);
			break;
		case EditorComponentType::SunPortal:
			DrawSunPortalComponent(component);
			break;
		case EditorComponentType::Ocean:
			DrawOceanComponent(component);
			break;
		case EditorComponentType::Buoyancy:
			DrawBuoyancyComponent(context, gameObject, component);
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
			DrawStringInputRow("音声パス", component.assetPath);

			if (!context.selectedAssetPath.empty() &&
				EditorAssetUtility::HasExtension(context.selectedAssetPath, ".wav") &&
				ImGui::Button("選択中 WAV を設定", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = context.selectedAssetPath;
			}

			if (ImGui::Button("内蔵 Cannon", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = "builtin://rail-cannon";
			}

			if (ImGui::Button("内蔵 Water Impact", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = "builtin://water-impact";
			}

			if (ImGui::Button("内蔵 Ocean Ambience", ImVec2(-1.0f, 0.0f))) {
				component.assetPath = "builtin://ocean-ambience";
			}

			DrawFloatRow("音量", component.audioVolume, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("ピッチ", component.audioPitch, 0.01f, 0.0f, 3.0f);
			DrawCheckboxRow("ループ", component.audioLoop);
			DrawCheckboxRow("自動再生", component.audioPlayOnAwake);
			{
				const char* audioBusItems[] = {"SFX", "BGM", "Ambience", "UI"};
				component.audioBus = (std::clamp)(component.audioBus, 0, 3);
				DrawComboRow(
					"ミキサーバス",
					component.audioBus,
					audioBusItems,
					static_cast<int32_t>(_countof(audioBusItems)));
			}
			DrawIntRow("同時発音数", component.audioMaxVoices);
			component.audioMaxVoices = (std::clamp)(component.audioMaxVoices, 1, 32);
			DrawFloatRow("再発音間隔", component.audioRetriggerInterval, 0.01f, 0.0f, 10.0f);
			DrawFloatRow("空間ブレンド", component.audioSpatialBlend, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("最小距離", component.audioMinDistance, 0.1f, 0.0f, 1000.0f);
			DrawFloatRow("最大距離", component.audioMaxDistance, 0.1f, 0.0f, 10000.0f);
			DrawSubHeader("3D 空間音響");
			DrawFloatRow("Doppler", component.audioDopplerLevel, 0.01f, 0.0f, 5.0f);
			DrawFloatRow("広がり角度", component.audioSpread, 1.0f, 0.0f, 360.0f);
			DrawFloatRow("指向性 内角", component.audioConeInnerAngle, 1.0f, 0.0f, 360.0f);
			DrawFloatRow("指向性 外角", component.audioConeOuterAngle, 1.0f, 0.0f, 360.0f);
			component.audioConeOuterAngle = (std::max)(component.audioConeOuterAngle, component.audioConeInnerAngle);
			DrawFloatRow("外側音量", component.audioConeOuterVolume, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("遮蔽", component.audioOcclusionStrength, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("残響Send", component.audioReverbSend, 0.01f, 0.0f, 1.0f);
			DrawFloatRow("初期反射", component.audioReflectionStrength, 0.01f, 0.0f, 1.0f);

			if (context.runtimeManager.IsPlaying()) {
				if (ImGui::Button("テスト再生", ImVec2(-1.0f, 0.0f))) {
					context.runtimeManager.GetAudioManager().Play(gameObject.id);
				}

				if (ImGui::Button("停止", ImVec2(-1.0f, 0.0f))) {
					context.runtimeManager.GetAudioManager().Stop(gameObject.id);
				}
			}
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
		case EditorComponentType::AutoConvexCollision:
			DrawAutoConvexCollisionComponent(gameObject, component);
			break;
		case EditorComponentType::WheelCollider:
			DrawTextRow("説明", "車輪用の 3D 当たり判定です。");
			DrawColliderCommonRows(component);
			DrawVector3Row("中心", component.colliderCenter, 0.01f, 0.0f, 0.0f);
			DrawFloatRow("半径", component.colliderRadius, 0.01f, 0.01f, 100.0f);
			DrawFloatRow("幅", component.colliderSize.x, 0.01f, 0.01f, 100.0f);
			break;
		case EditorComponentType::ConstantForce:
			DrawConstantForceComponent(component);
			break;
		case EditorComponentType::Aerodynamics:
			DrawAerodynamicsComponent(component);
			break;
		case EditorComponentType::WindZone:
			DrawWindZoneComponent(component);
			break;
		case EditorComponentType::GravityField:
			DrawGravityFieldComponent(component);
			break;
		case EditorComponentType::RotatingFrame:
			DrawRotatingFrameComponent(component);
			break;
		case EditorComponentType::FluidVolume:
			DrawFluidVolumeComponent(component);
			break;
		case EditorComponentType::SpringForce:
			DrawSpringForceComponent(context, gameObject, component);
			break;
		case EditorComponentType::RopeConstraint:
			DrawRopeConstraintComponent(context, gameObject, component);
			break;
		case EditorComponentType::WireConnectable:
			DrawWireConnectableComponent(context, gameObject, component);
			break;
		case EditorComponentType::WireRenderer:
			DrawWireRendererComponent(component);
			break;
		case EditorComponentType::TorsionSpring:
			DrawTorsionSpringComponent(context, gameObject, component);
			break;
		case EditorComponentType::WeaponLoadout:
			DrawWeaponLoadoutComponent(context, gameObject, component);
			break;
		case EditorComponentType::WeaponLoadoutSlot:
			DrawWeaponLoadoutSlotComponent(context, gameObject, component);
			break;
		case EditorComponentType::TargetSelector:
			DrawTargetSelectorComponent(context, gameObject, component);
			break;
		case EditorComponentType::TargetSteering:
			DrawTargetSteeringComponent(context, gameObject, component);
			break;
		case EditorComponentType::TargetPoint:
			DrawTargetPointComponent(component);
			break;
		case EditorComponentType::Team:
			DrawTeamComponent(component);
			break;
		case EditorComponentType::Timer:
			DrawTimerComponent(context, gameObject, component);
			break;
		case EditorComponentType::GenericStateMachine:
			DrawGenericStateMachineComponent(context, gameObject, component);
			break;
		case EditorComponentType::Attribute:
			DrawAttributeComponent(context, gameObject, component);
			break;
		case EditorComponentType::DestructiblePart:
			DrawDestructiblePartComponent(context, gameObject, component);
			break;
		case EditorComponentType::FormationFollower:
			DrawFormationFollowerComponent(context, gameObject, component);
			break;
		case EditorComponentType::TargetLock:
			DrawTargetLockComponent(context, gameObject, component);
			break;
		case EditorComponentType::MultiTargetLock:
			DrawMultiTargetLockComponent(context, gameObject, component);
			break;
		case EditorComponentType::WorldTargetMarker:
			DrawTargetMarkerComponent(context, gameObject, component, false);
			break;
		case EditorComponentType::OffScreenIndicator:
			DrawTargetMarkerComponent(context, gameObject, component, true);
			break;
		case EditorComponentType::AttributeSet:
			DrawAttributeSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::GenericCounter:
			DrawGenericCounterComponent(context, gameObject, component);
			break;
		case EditorComponentType::GenericCondition:
			DrawGenericConditionComponent(context, gameObject, component);
			break;
		case EditorComponentType::GameplayData:
			DrawGameplayDataComponent(context, component);
			break;
		case EditorComponentType::AreaDamage:
			DrawAreaDamageComponent(context, gameObject, component);
			break;
		case EditorComponentType::HitZone:
			DrawHitZoneComponent(context, gameObject, component);
			break;
		case EditorComponentType::DamageTagModifier:
			DrawDamageTagModifierComponent(component);
			break;
		case EditorComponentType::ProjectileDetonator:
			DrawProjectileDetonatorComponent(context, gameObject, component);
			break;
		case EditorComponentType::ThreatTracker:
			DrawThreatTrackerComponent(context, gameObject, component);
			break;
		case EditorComponentType::RuntimeStateReset:
			DrawRuntimeStateResetComponent(context, gameObject, component);
			break;
		case EditorComponentType::CooldownSet:
			DrawCooldownSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::WeaponFirePattern:
			DrawWeaponFirePatternComponent(context, gameObject, component);
			break;
		case EditorComponentType::TargetAssignment:
			DrawTargetAssignmentComponent(context, gameObject, component);
			break;
		case EditorComponentType::WeaponAccuracy:
			DrawWeaponAccuracyComponent(component);
			break;
		case EditorComponentType::WeaponRecoil:
			DrawWeaponRecoilComponent(context, gameObject, component);
			break;
		case EditorComponentType::ImpactResponder:
			DrawImpactResponderComponent(context, gameObject, component);
			break;
		case EditorComponentType::SurfaceType:
			DrawSurfaceTypeComponent(component);
			break;
		case EditorComponentType::TimeScale:
			DrawTimeScaleComponent(context, gameObject, component);
			break;
		case EditorComponentType::AimAssist:
			DrawAimAssistComponent(context, gameObject, component);
			break;
		case EditorComponentType::InterceptPrediction:
			DrawInterceptPredictionComponent(context, gameObject, component);
			break;
		case EditorComponentType::DamageDirectionIndicator:
			DrawDamageDirectionIndicatorComponent(component);
			break;
		case EditorComponentType::ObjectiveTracker:
			DrawObjectiveTrackerComponent(context, gameObject, component);
			break;
		case EditorComponentType::EncounterController:
			DrawEncounterControllerComponent(context, gameObject, component);
			break;
		case EditorComponentType::SpawnPointSet:
			DrawSpawnPointSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::DifficultyParameterSet:
			DrawDifficultyParameterSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::CameraFeedbackMixer:
			DrawCameraFeedbackMixerComponent(component);
			break;
		case EditorComponentType::BallisticPrediction:
			DrawBallisticPredictionComponent(context, gameObject, component);
			break;
		case EditorComponentType::DamageEventBuffer:
			DrawDamageEventBufferComponent(component);
			break;
		case EditorComponentType::GamePause:
			DrawGamePauseComponent(context, gameObject, component);
			break;
		case EditorComponentType::SurfaceWakeEmitter:
			DrawSurfaceWakeEmitterComponent(context, gameObject, component);
			break;
		case EditorComponentType::TrajectoryRenderer:
			DrawTrajectoryRendererComponent(context, gameObject, component);
			break;
		case EditorComponentType::WaterSurfaceState:
			DrawWaterSurfaceStateComponent(context, gameObject, component);
			break;
		case EditorComponentType::OceanProbeSet:
			DrawOceanProbeSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::AttackCollisionFilter:
			DrawAttackCollisionFilterComponent(context, gameObject, component);
			break;
		case EditorComponentType::TurretAim:
			DrawTurretAimComponent(context, gameObject, component);
			break;
		case EditorComponentType::WeaponGroup:
			DrawWeaponGroupComponent(context, gameObject, component);
			break;
		case EditorComponentType::ProjectileImpactPhysics:
			DrawProjectileImpactPhysicsComponent(component);
			break;
		case EditorComponentType::CameraHorizonStabilizer:
			DrawCameraHorizonStabilizerComponent(context, gameObject, component);
			break;
		case EditorComponentType::TextEffect:
			DrawTextEffectComponent(context, gameObject, component);
			break;
		case EditorComponentType::SceneTransition:
			DrawSceneTransitionComponent(context, gameObject, component);
			break;
		case EditorComponentType::FireLineCheck:
			DrawFireLineCheckComponent(context, gameObject, component);
			break;
		case EditorComponentType::StatusEffectSet:
			DrawStatusEffectSetComponent(context, gameObject, component);
			break;
		case EditorComponentType::MovementModifier:
			DrawMovementModifierComponent(context, gameObject, component);
			break;
		case EditorComponentType::PropertyTween:
			DrawPropertyTweenComponent(context, gameObject, component);
			break;
		case EditorComponentType::ActionRelay:
			DrawActionRelayComponent(gameObject, component);
			break;
		case EditorComponentType::ActionRelayTarget:
			DrawActionRelayTargetComponent(context, gameObject, component);
			break;
		case EditorComponentType::Thruster:
			DrawThrusterComponent(component);
			break;
		case EditorComponentType::PulleyConstraint:
			DrawPulleyConstraintComponent(context, gameObject, component);
			break;
		case EditorComponentType::PhysicsServo:
			DrawPhysicsServoComponent(context, gameObject, component);
			break;
		case EditorComponentType::VortexField:
			DrawVortexFieldComponent(component);
			break;
		case EditorComponentType::PressureField:
			DrawPressureFieldComponent(component);
			break;
		case EditorComponentType::Suspension:
			DrawSuspensionComponent(component);
			break;
		case EditorComponentType::UprightStabilizer:
			DrawUprightStabilizerComponent(component);
			break;
		case EditorComponentType::ElectromagneticBody:
			DrawElectromagneticBodyComponent(component);
			break;
		case EditorComponentType::ElectromagneticField:
			DrawElectromagneticFieldComponent(component);
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
			DrawAnimatorComponent(context, gameObject, component);
			break;
		case EditorComponentType::AvatarMask:
			DrawAvatarMaskComponent(component);
			break;
		case EditorComponentType::AudioListener:
			DrawAudioListenerComponent(context, component);
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
		case EditorComponentType::RailMovement:
			DrawRailMovementComponent(context, gameObject, component);
			break;
		case EditorComponentType::RailSpeedProfile:
			DrawRailSpeedProfileComponent(component);
			break;
		case EditorComponentType::RailZone:
			DrawRailZoneComponent(context, gameObject, component);
			break;
		case EditorComponentType::RailBranch:
			DrawRailBranchComponent(context, gameObject, component);
			break;
		case EditorComponentType::ActionSequence:
			DrawActionSequenceComponent(context, gameObject, component);
			break;
		case EditorComponentType::ActionSequenceStep:
			DrawActionSequenceStepComponent(context, gameObject, component);
			break;
		case EditorComponentType::Saveable:
			DrawSaveableComponent(component);
			break;
		case EditorComponentType::Checkpoint:
			DrawCheckpointComponent(context, gameObject, component);
			break;
		case EditorComponentType::Health:
			DrawHealthComponent(component);
			break;
		case EditorComponentType::DamageReceiver:
			DrawDamageReceiverComponent(context, gameObject, component);
			break;
		case EditorComponentType::ScreenAim:
			DrawScreenAimComponent(context, gameObject, component);
			break;
		case EditorComponentType::HitscanWeapon:
			DrawHitscanWeaponComponent(context, gameObject, component);
			break;
		case EditorComponentType::ProjectileEmitter:
			DrawProjectileEmitterComponent(context, gameObject, component);
			break;
		case EditorComponentType::ObjectPool:
			DrawObjectPoolComponent(context, gameObject, component);
			break;
		case EditorComponentType::PrefabSpawner:
			DrawPrefabSpawnerComponent(context, gameObject, component);
			break;
		case EditorComponentType::CameraBlend:
			DrawCameraBlendComponent(context, gameObject, component);
			break;
		case EditorComponentType::CameraShake:
			DrawCameraShakeComponent(component);
			break;
		case EditorComponentType::CameraFollowComposer:
			DrawCameraFollowComposerComponent(context, gameObject, component);
			break;
		case EditorComponentType::SpeedFeedback:
			DrawSpeedFeedbackComponent(context, gameObject, component);
			break;
		case EditorComponentType::WaveSpawner:
			DrawWaveSpawnerComponent(context, gameObject, component);
			break;
		case EditorComponentType::SpawnedObjectSetup:
			DrawSpawnedObjectSetupComponent(context, gameObject, component);
			break;
		case EditorComponentType::WaveMotionProfile:
			DrawWaveMotionProfileComponent(component);
			break;
		case EditorComponentType::DistanceActivation:
			DrawDistanceActivationComponent(context, gameObject, component);
			break;
		case EditorComponentType::SimulationLOD:
			DrawSimulationLodComponent(context, gameObject, component);
			break;
		case EditorComponentType::RailEventMarker:
			DrawRailEventMarkerComponent(context, gameObject, component);
			break;
		case EditorComponentType::SceneStreaming:
			DrawSceneStreamingComponent(context, gameObject, component);
			break;
		case EditorComponentType::TimelineEvent:
			DrawTimelineEventComponent(context, gameObject, component);
			break;
		case EditorComponentType::ThresholdState:
			DrawThresholdStateComponent(context, gameObject, component);
			break;
		case EditorComponentType::UIValueBinding:
			DrawUiValueBindingComponent(context, gameObject, component);
			break;
		case EditorComponentType::FreeTransform:
			DrawFreeTransformComponent(component);
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
			DrawParticleSystemComponent(
				context,
				gameObject,
				component,
				"発生率、形状、寿命、色、移動、衝突を組み合わせる Particle Emitter です。");
			break;
		case EditorComponentType::VisualEffect:
			DrawVisualEffectComponent(context, gameObject, component);
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
		case EditorComponentType::Foliage:
			DrawFoliageComponent(component);
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
		case EditorComponentType::Button:
			DrawButtonComponent(component);
			break;
		case EditorComponentType::SceneButton:
			DrawSceneButtonComponent(context, component);
			break;
		case EditorComponentType::Toggle:
			DrawToggleComponent(component);
			break;
		case EditorComponentType::Slider:
			DrawSliderComponent(component);
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
				"ゲームプレイ",
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
				const bool hasComponent = context.editorScene.HasComponent(gameObject.id, entry.type);
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

					const bool hasComponent = context.editorScene.HasComponent(gameObject.id, entry.type);
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

			static char prefabPathBuffer[260] = "Assets/Prefabs/NewPrefab.prefab";
			static std::string lastSelectedPrefabAssetPath;
			if (EditorAssetUtility::HasExtension(context.selectedAssetPath, ".prefab") &&
				context.selectedAssetPath != lastSelectedPrefabAssetPath) {
				strncpy_s(
					prefabPathBuffer,
					sizeof(prefabPathBuffer),
					context.selectedAssetPath.c_str(),
					_TRUNCATE);
				lastSelectedPrefabAssetPath = context.selectedAssetPath;
			}

			const int32_t selectedPrefabRootId = selectedEditorGameObject->id;
			const std::string selectedPrefabSourcePath = selectedEditorGameObject->prefabSourcePath;
			ImGui::InputText("Prefab Asset", prefabPathBuffer, sizeof(prefabPathBuffer));

			if (ImGui::Button("Prefabとして保存")) {
				context.editorScene.SavePrefab(selectedPrefabRootId, prefabPathBuffer);
			}

			ImGui::SameLine();

			if (ImGui::Button("Variantとして保存")) {
				context.editorScene.SavePrefabVariant(
					selectedPrefabRootId,
					selectedPrefabSourcePath,
					prefabPathBuffer);
			}

			if (ImGui::Button("PrefabをSceneへ生成")) {
				context.editorScene.PushUndo();
				int32_t prefabId = context.editorScene.InstantiatePrefab(prefabPathBuffer);

				if (prefabId >= 0) {
					context.selectedEditorGameObjectId = prefabId;
					SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
					context.previousSelectedEditorGameObjectId = -1;
					SyncEditorSelection(context);
				}
			}

			if (!selectedPrefabSourcePath.empty()) {
				ImGui::SameLine();

				if (ImGui::Button("Prefabへ反映")) {
					context.editorScene.ApplyPrefabInstance(selectedPrefabRootId);
				}

				ImGui::SameLine();

				if (ImGui::Button("Prefabへ戻す")) {
					context.editorScene.PushUndo();
					const int32_t revertedRootId =
						context.editorScene.RevertPrefabInstance(selectedPrefabRootId);

					if (revertedRootId >= 0) {
						context.selectedEditorGameObjectId = revertedRootId;
						SetSingleSelectedGameObject(context.selectedEditorGameObjectId);
						context.previousSelectedEditorGameObjectId = -1;
						SyncEditorSelection(context);
					}
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
		DrawCheckboxRow("速度 / 角速度", physicsSettings.drawVelocityDebug);
		DrawCheckboxRow("力 / 場の向き", physicsSettings.drawForceDirectionDebug);
		DrawCheckboxRow("影響範囲 / 流体領域", physicsSettings.drawFieldVolumeDebug);
		DrawCheckboxRow("ばね / Joint 接続", physicsSettings.drawConnectionDebug);
		DrawCheckboxRow("選択中だけ表示", physicsSettings.drawSelectedOnlyDebug);
		DrawFloatRow("ベクトル表示倍率", physicsSettings.debugVectorScale, 0.01f, 0.01f, 10.0f);
		physicsSettings.debugVectorScale = (std::clamp)(physicsSettings.debugVectorScale, 0.01f, 10.0f);

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

const char* GetEditorComponentDisplayName(EditorComponentType type) {
	// Inspector 内部の日本語名テーブルを、Log監視など他 UI からも同じ表記で使えるよう公開する。
	return GetComponentDisplayName(type);
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

	// 前フレームの描画中に要求された追加を、参照を取得する前に安全に反映する。
	if (g_pendingActionSequenceStepParentId >= 0) {
		const int32_t parentGameObjectId = g_pendingActionSequenceStepParentId;
		g_pendingActionSequenceStepParentId = -1;

		if (context.editorScene.FindGameObject(parentGameObjectId) != nullptr) {
			context.editorScene.PushUndo();
			const int32_t stepGameObjectId = context.editorScene.CreateGameObject("Sequence Step");
			context.editorScene.SetParent(stepGameObjectId, parentGameObjectId);
			context.editorScene.AddComponent(stepGameObjectId, EditorComponentType::ActionSequenceStep);
			context.selectedEditorGameObjectId = stepGameObjectId;
			SetSingleSelectedGameObject(stepGameObjectId);
			context.previousSelectedEditorGameObjectId = -1;
			SyncEditorSelection(context);
		}
	}

	if (g_pendingWeaponSlotParentId >= 0) {
		const int32_t parentGameObjectId = g_pendingWeaponSlotParentId;
		g_pendingWeaponSlotParentId = -1;

		if (context.editorScene.FindGameObject(parentGameObjectId) != nullptr) {
			context.editorScene.PushUndo();
			const int32_t slotGameObjectId = context.editorScene.CreateGameObject("Weapon Slot");
			context.editorScene.SetParent(slotGameObjectId, parentGameObjectId);
			context.editorScene.AddComponent(slotGameObjectId, EditorComponentType::WeaponLoadoutSlot);
			context.selectedEditorGameObjectId = slotGameObjectId;
			SetSingleSelectedGameObject(slotGameObjectId);
			context.previousSelectedEditorGameObjectId = -1;
			SyncEditorSelection(context);
		}
	}

	if (g_pendingActionRelayTargetParentId >= 0) {
		const int32_t parentGameObjectId = g_pendingActionRelayTargetParentId;
		g_pendingActionRelayTargetParentId = -1;

		if (context.editorScene.FindGameObject(parentGameObjectId) != nullptr) {
			context.editorScene.PushUndo();
			const int32_t targetGameObjectId = context.editorScene.CreateGameObject("Relay Target");
			context.editorScene.SetParent(targetGameObjectId, parentGameObjectId);
			context.editorScene.AddComponent(targetGameObjectId, EditorComponentType::ActionRelayTarget);
			context.selectedEditorGameObjectId = targetGameObjectId;
			SetSingleSelectedGameObject(targetGameObjectId);
			context.previousSelectedEditorGameObjectId = -1;
			SyncEditorSelection(context);
		}
	}

	// Rail Movement を選んだまま、標準Splineの作成・接続・編集開始までを一度に行う。
	if (g_pendingRailSetupOwnerId >= 0) {
		const int32_t ownerGameObjectId = g_pendingRailSetupOwnerId;
		g_pendingRailSetupOwnerId = -1;
		EditorGameObject* ownerGameObject = context.editorScene.FindGameObject(ownerGameObjectId);

		if (ownerGameObject != nullptr) {
			const std::string railPathName = ownerGameObject->name + " Spline";
			Vector3 ownerWorldScale{};
			Vector3 ownerWorldRotation{};
			Vector3 ownerWorldPosition{};
			context.editorScene.GetWorldTransform(
				ownerGameObjectId,
				ownerWorldScale,
				ownerWorldRotation,
				ownerWorldPosition);
			context.editorScene.PushUndo();
			const int32_t railPathGameObjectId = context.editorScene.CreateGameObject(railPathName);
			EditorGameObject* railPathGameObject = context.editorScene.FindGameObject(railPathGameObjectId);

			if (railPathGameObject != nullptr) {
				railPathGameObject->translate = ownerWorldPosition;
			}

			for (int32_t pointIndex = 0; pointIndex < 4; pointIndex++) {
				char pointName[32]{};
				std::snprintf(pointName, _countof(pointName), "Point %02d", pointIndex);
				const int32_t pointGameObjectId = context.editorScene.CreateGameObject(pointName);
				context.editorScene.SetParent(pointGameObjectId, railPathGameObjectId);
				EditorGameObject* pointGameObject = context.editorScene.FindGameObject(pointGameObjectId);

				if (pointGameObject != nullptr) {
					pointGameObject->translate = {
						0.0f,
						0.0f,
						static_cast<float>(pointIndex) * 12.0f};
				}
			}

			ownerGameObject = context.editorScene.FindGameObject(ownerGameObjectId);
			EditorComponent* railMovementComponent = ownerGameObject != nullptr ?
				EditorComponentUtility::FindComponent(*ownerGameObject, EditorComponentType::RailMovement) : nullptr;

			if (railMovementComponent != nullptr) {
				railMovementComponent->railPathGameObjectId = railPathGameObjectId;
				g_isSplineEditorVisible = true;
				context.previousSelectedEditorGameObjectId = -1;
				SyncEditorSelection(context);
			}
		}
	}

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
