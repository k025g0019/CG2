#pragma once

#include "EditorScriptApi.h"
#include "Matrix.h"
#include "Vector.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// エディタ用 Component / GameObject / Scene
//============================================================

enum class EditorComponentType {
	// 位置 / 回転 / 拡縮
	Transform,
	// 3D モデル表示
	ModelRenderer,
	// 2D スプライト表示
	SpriteRenderer,
	// ライト設定
	Light,
	// カメラ設定
	Camera,
	// 音再生設定
	AudioSource,
	// 質量や速度などの物理設定
	RigidBody,
	// Box 形状の当たり判定
	BoxCollider,
	// Sphere 形状の当たり判定
	SphereCollider,
	// 入力操作設定
	Input,
	// Animation Clip の再生設定
	Animation,
	// Animator Controller の状態管理
	Animator,
	// AudioListener の位置設定
	AudioListener,
	// 親 Constraint
	ParentConstraint,
	// 位置 Constraint
	PositionConstraint,
	// 回転 Constraint
	RotationConstraint,
	// 拡縮 Constraint
	ScaleConstraint,
	// EventSystem の入力イベント設定
	EventSystem,
	// Mesh Asset 参照
	MeshFilter,
	// Capsule 形状の当たり判定
	CapsuleCollider,
	// Mesh 形状の当たり判定
	MeshCollider,
	// Character Controller 設定
	CharacterController,
	// Navigation Agent 設定
	NavigationAgent,
	// Playables 設定
	PlayableDirector,
	// 汎用 Script Component
	Script,
	// FeelKitHaptics の振動 Component
	HapticSource,
	// UI Canvas
	Canvas,
	// UI Image
	Image,
	// UI Text
	Text,
	// UI 用 RectTransform
	RectTransform,
	// 自作 Component の基本クラス
	MonoBehaviour,
	// ボーン付き 3D モデル描画
	SkinnedMeshRenderer,
	// 線描画
	LineRenderer,
	// 軌跡描画
	TrailRenderer,
	// カメラ方向を向く描画
	BillboardRenderer,
	// UI 描画用 Renderer
	CanvasRenderer,
	// ParticleSystem の描画設定
	ParticleSystemRenderer,
	// レンズフレアを Camera へ重ねる設定
	FlareLayer,
	// Cinemachine 用 Camera 制御
	CinemachineCamera,
	// 反射情報の取得範囲
	ReflectionProbe,
	// ライトプローブ配置
	LightProbeGroup,
	// 大きな動的物体向けライト補間
	LightProbeProxyVolume,
	// URP / HDRP の Volume 設定
	Volume,
	// Terrain 用 Collider
	TerrainCollider,
	// 車輪用 Collider
	WheelCollider,
	// 常時力を加える 3D 物理設定
	ConstantForce,
	// ちょうつがい Joint
	HingeJoint,
	// 固定 Joint
	FixedJoint,
	// バネ Joint
	SpringJoint,
	// 細かい制約付き Joint
	ConfigurableJoint,
	// ラグドール向け Joint
	CharacterJoint,
	// 2D 物理挙動
	RigidBody2D,
	// 2D 四角 Collider
	BoxCollider2D,
	// 2D 円 Collider
	CircleCollider2D,
	// 2D Capsule Collider
	CapsuleCollider2D,
	// 2D 多角形 Collider
	PolygonCollider2D,
	// 2D 線 Collider
	EdgeCollider2D,
	// 2D Collider 統合
	CompositeCollider2D,
	// Tilemap 用 2D Collider
	TilemapCollider2D,
	// 独自形状の 2D Collider
	CustomCollider2D,
	// 2D 距離 Joint
	DistanceJoint2D,
	// 2D 回転 Joint
	HingeJoint2D,
	// 2D バネ Joint
	SpringJoint2D,
	// 2D 固定 Joint
	FixedJoint2D,
	// 2D スライド Joint
	SliderJoint2D,
	// 2D 車輪 Joint
	WheelJoint2D,
	// 2D 片方向床
	PlatformEffector2D,
	// 2D 表面移動
	SurfaceEffector2D,
	// 2D 範囲内の力
	AreaEffector2D,
	// 2D 点に向かう力
	PointEffector2D,
	// 2D 浮力
	BuoyancyEffector2D,
	// Animation の適用範囲
	AvatarMask,
	// 向き制約
	AimConstraint,
	// 対象を見る制約
	LookAtConstraint,
	// 音の残響範囲
	AudioReverbZone,
	// Audio 低音通過 Filter
	AudioLowPassFilter,
	// Audio 高音通過 Filter
	AudioHighPassFilter,
	// Audio Echo Filter
	AudioEchoFilter,
	// Audio 歪み Filter
	AudioDistortionFilter,
	// Audio 残響 Filter
	AudioReverbFilter,
	// Audio Chorus Filter
	AudioChorusFilter,
	// Canvas の解像度追従
	CanvasScaler,
	// UI クリック判定
	GraphicRaycaster,
	// Texture 表示
	RawImage,
	// TextMeshPro の UI Text
	TextMeshProUGUI,
	// UI Button
	Button,
	// UI Toggle
	Toggle,
	// UI Slider
	Slider,
	// UI Scrollbar
	Scrollbar,
	// UI Dropdown
	Dropdown,
	// TextMeshPro 版 Dropdown
	TMPDropdown,
	// UI InputField
	InputField,
	// TextMeshPro 版 InputField
	TMPInputField,
	// UI ScrollRect
	ScrollRect,
	// UI Mask
	Mask,
	// UI RectMask2D
	RectMask2D,
	// 横並び Layout
	HorizontalLayoutGroup,
	// 縦並び Layout
	VerticalLayoutGroup,
	// Grid Layout
	GridLayoutGroup,
	// 内容に合わせる Layout
	ContentSizeFitter,
	// Aspect 比を固定する Layout
	AspectRatioFitter,
	// Layout 個別指定
	LayoutElement,
	// 旧 Input 用 UI Module
	StandaloneInputModule,
	// 新 Input System 用 UI Module
	InputSystemUIInputModule,
	// 新 Input System の Player 入力
	PlayerInput,
	// 複数 Player 入力管理
	PlayerInputManager,
	// Touch 入力 Module
	TouchInputModule,
	// NavMesh 障害物
	NavMeshObstacle,
	// NavMesh 生成面
	NavMeshSurface,
	// NavMesh 生成ルール変更
	NavMeshModifier,
	// 範囲指定の NavMesh 変更
	NavMeshModifierVolume,
	// 離れた NavMesh 接続
	NavMeshLink,
	// BehaviorTree.CPP 用の行動ツリー AI
	AIBehaviorTree,
	// BehaviorTree.CPP 用の共有データ
	AIBehaviorBlackboard,
	// BehaviorTree.CPP 用の Selector ノード
	AIBehaviorSelector,
	// BehaviorTree.CPP 用の Sequence ノード
	AIBehaviorSequence,
	// BehaviorTree.CPP 用の実行 Task
	AIBehaviorTask,
	// BehaviorTree.CPP 用の Decorator
	AIBehaviorDecorator,
	// HFSM2 用の階層ステート AI
	AIStateMachine,
	// HFSM2 用の State
	AIState,
	// HFSM2 用の Transition
	AIStateTransition,
	// cppGOAP 用の目標計画 AI
	AIGoapPlanner,
	// cppGOAP 用の Goal
	AIGoapGoal,
	// cppGOAP 用の Action
	AIGoapAction,
	// cppGOAP 用の WorldState
	AIGoapWorldState,
	// Fluid HTN 用の階層タスク AI
	AIHtnPlanner,
	// Fluid HTN 用の Domain
	AIHtnDomain,
	// Fluid HTN 用の Task
	AIHtnTask,
	// Fluid HTN 用の Method
	AIHtnMethod,
	// MicroPather / Recast 用の経路探索 AI
	AIPathfindingAgent,
	// MicroPather 用の Grid 探索
	AIMicroPatherGrid,
	// RecastNavigation 用の NavMesh 生成
	AIRecastNavMeshBuilder,
	// RecastNavigation 用の Crowd Agent
	AIRecastCrowdAgent,
	// 経路要求
	AIPathRequest,
	// 動的障害物
	AIDynamicObstacle,
	// OpenSteer 用のステアリング AI
	AISteeringAgent,
	// OpenSteer 用の Seek 操舵
	AISeekSteering,
	// OpenSteer 用の Flee 操舵
	AIFleeSteering,
	// OpenSteer 用の Arrive 操舵
	AIArriveSteering,
	// OpenSteer 用の Pursuit 操舵
	AIPursuitSteering,
	// OpenSteer 用の Wander 操舵
	AIWanderSteering,
	// OpenSteer 用の障害物回避操舵
	AIObstacleAvoidanceSteering,
	// OpenSteer 用の群れ操舵
	AIFlockSteering,
	// 視界判定用 Sensor
	AIVisionSensor,
	// OpenCV 用のカメラ入力
	AIOpenCvCamera,
	// OpenCV 用の画像検出
	AIOpenCvObjectDetector,
	// OpenCV 用の色追跡
	AIOpenCvColorTracker,
	// OpenCV 用の動き検出
	AIMotionSensor,
	// Whisper 用の音声認識
	AIWhisperSpeechRecognizer,
	// Whisper 用の音声コマンド
	AIVoiceCommand,
	// Particle 表現
	ParticleSystem,
	// VFX Graph 表現
	VisualEffect,
	// レンズフレア表現
	LensFlare,
	// 投影表現
	Projector,
	// URP / HDRP の Decal
	DecalProjector,
	// 地形
	Terrain,
	// 2D Tilemap
	Tilemap,
	// Tilemap 描画
	TilemapRenderer,
	// Tilemap 親 Grid
	Grid,
	// ローカル軸の自動移動
	LocalMove,
	// 転がりながら進む移動
	RollingMove,
	// ポストプロセス設定（Bloom / SMAA / TAA / SSR / 最終合成）
	PostProcess,
	// 環境光 / HDRI 設定
	Environment,
	// 自由移動/回転（力無関係、軸指定可）
	FreeTransform,
	// FBX / OBJ の頂点位置から自動生成する凸包 Collider
	AutoConvexCollision,
	// FFocean3D を基にした編集可能な海面描画
	Ocean,
	// Ocean の波面を使って Dynamic Rigidbody へ複数点浮力を加える
	Buoyancy,
	// 子 GameObject を制御点にしたレール上の自動移動
	RailMovement,
	// ゲーム中にダメージを受ける体力
	Health,
	// 旧 RailShooterEnemy の Scene 読み込み互換スロット。Engine Runtime は実行しない
	LegacyRailShooterEnemy,
	// 旧 RailShooterShip の Scene 読み込み互換スロット。Engine Runtime は実行しない
	LegacyRailShooterShip,
	// 旧 RailShooterEnemyMotion の Scene 読み込み互換スロット。Engine Runtime は実行しない
	LegacyRailShooterEnemyMotion,
	// 旧 RailShooterStage の Scene 読み込み互換スロット。Engine Runtime は実行しない
	LegacyRailShooterStage,
	// Script を書かずに Scene を切り替える GameView Button
	SceneButton,
	// 草木メッシュへ風による頂点変形と透過光を追加する
	Foliage,
	// ObjectPoolから指定数を編隊生成し、旧Sceneでは子GameObject方式へフォールバックする
	WaveSpawner,
	// 時間または RailFollower 進行率から任意の C++ Script Action を通知する
	TimelineEvent,
	// Health または RailFollower の値を閾値で状態へ変換し、任意 Action を通知する
	ThresholdState,
	// Health / RailFollower / Active 値を同じ GameObject の Text / Slider へ反映する
	UIValueBinding,
	// 相対風速から抗力・揚力・横力・Magnus 力と圧力中心トルクを計算する
	Aerodynamics,
	// Scene 内へ方向風または放射風を作る
	WindZone,
	// Dynamic Rigidbody を点へ引く逆二乗または定加速度の重力場
	GravityField,
	// 回転座標系内の遠心力、Coriolis 力、Euler 力を加える
	RotatingFrame,
	// 有限3D領域へArchimedes浮力、粘性抵抗、流体抗力を作る
	FluidVolume,
	// World点または別GameObjectとの間へHookeばね力を加える
	SpringForce,
	// 電荷と磁気双極子モーメントをDynamic Rigidbodyへ持たせる
	ElectromagneticBody,
	// 一様電磁場または点電荷源をScene内へ作る
	ElectromagneticField,
	// 画面上の照準位置をマウスまたはInput Actionから更新する
	ScreenAim,
	// 画面照準または中央方向へ即時Ray射撃を行う
	HitscanWeapon,
	// ObjectPoolから弾を取得して移動・命中判定を行う
	ProjectileEmitter,
	// Healthへ入るダメージ倍率、無敵時間、通知を管理する
	DamageReceiver,
	// Template GameObjectをPlay開始前に複製して再利用する
	ObjectPool,
	// ObjectPoolから指定位置へGameObjectを生成する
	PrefabSpawner,
	// 2つのCamera Transformを時間補間する
	CameraBlend,
	// Game View Cameraへ位置・回転振動を加える
	CameraShake,
	// RailFollowerを進行率または外部命令で別Railへ切り替える
	RailBranch,
	// 子のActionSequenceStepを順次・並列実行する汎用シーケンス
	ActionSequence,
	// Action呼出、待機、Active変更、Scene遷移、条件分岐を表すシーケンス要素
	ActionSequenceStep,
	// Save Slotへ保存するGameObject状態を登録する
	Saveable,
	// Save Slotの作成・復元を外部命令またはPlay開始時に行う
	Checkpoint,
	// 2点間が最大長を超えた時だけ張力を発生させるロープ / ケーブル
	RopeConstraint,
	// 目標相対角へHooke則の復元Torqueを加える回転ばね
	TorsionSpring,
	// 子のWeaponLoadoutSlotを可変数保持して装備・弾薬・リロードを管理する
	WeaponLoadout,
	// 親WeaponLoadoutへ1つの武器Slot設定を提供する
	WeaponLoadoutSlot,
	// 距離・角度・遮蔽・優先方式から汎用Targetを選択する
	TargetSelector,
	// 選択Targetまたは明示TargetへGameObjectを旋回・加速させる
	TargetSteering,
	// Rail等の基準移動へローカル位置・回転・入力Offsetを追加する
	MovementModifier,
	// 公開Runtime Propertyを時間補間する
	PropertyTween,
	// 子ActionRelayTargetへ同じActionを分配する
	ActionRelay,
	// 親ActionRelayから通知されるActionの接続先を表す
	ActionRelayTarget,
	// ローカル作用点から指定方向へ推進Forceを加える
	Thruster,
	// 2つのBodyと固定支持点をロープ長比で結ぶ滑車
	PulleyConstraint,
	// 目標位置と姿勢へForce / Torqueで追従する物理サーボ
	PhysicsServo,
	// 回転流体の速度場へDynamic Rigidbodyを追従させる
	VortexField,
	// 圧力と投影面積から放射Forceを加える
	PressureField,
	// Raycast接地点との距離からばね・減衰Forceを加える
	Suspension,
	// ローカル上方向をWorld目標方向へTorqueで安定させる
	UprightStabilizer,
	// 大型Objectの部位、弱点、注視点としてTargetSelectorへ候補位置を提供する
	TargetPoint,
	// Target選択やDamage判定で使う汎用Team所属とTarget可否を表す
	Team,
	// 軽量な遅延・繰り返しActionを管理する
	Timer,
	// 任意文字列Stateを保持して変更Actionを通知する
	GenericStateMachine,
	// Health以外の汎用可変Resourceを保持する
	Attribute,
	// 部位破壊時に子Objectや指定Componentを無効化する
	DestructiblePart,
	// LeaderのローカルOffsetへ追従する
	FormationFollower,
	// TargetSelectorのTargetを時間付きでLockする
	TargetLock,
	// TargetSelectorの候補を複数保持し、順次Lockする
	MultiTargetLock,
	// 3D TargetのWorld座標をCanvas上へ追従表示する
	WorldTargetMarker,
	// 画面外Targetの方向をCanvas端へ表示する
	OffScreenIndicator,
	// 同一GameObject上で名前付きResourceを可変数保持する
	AttributeSet,
	// 汎用数値を加減算し、閾値到達を通知する
	GenericCounter,
	// Runtime PropertyやComponent状態を比較してActionを通知する
	GenericCondition,
	// Project内のGameplay Data Assetを参照する
	GameplayData,
	// 球範囲内へ距離減衰付きDamageとImpulseを与える
	AreaDamage,
	// Collider部位からHealth所有ObjectへDamageを転送して倍率を掛ける
	HitZone,
	// Damage Tagごとの耐性・弱点倍率を可変数保持する
	DamageTagModifier,
	// Projectileへ接触・近接・寿命・手動起爆条件を追加する
	ProjectileDetonator,
	// 接近中Projectileを収集して警告情報を公開する
	ThreatTracker,
	// ObjectPool再利用時にRuntime状態を共通初期化する
	RuntimeStateReset,
	// 名前付きCooldownを同一GameObject上で可変数管理する
	CooldownSet,
	// 単発・Burst・Salvo・Spread・Sequence・Chargeの発射列を作る
	WeaponFirePattern,
	// MultiTargetLockの各TargetへProjectileを割り当てて斉射する
	TargetAssignment,
	// 射撃ごとの拡散増加と時間回復を管理する
	WeaponAccuracy,
	// 発射側Rigidbody・表示Object・Cameraへ反動を返す
	WeaponRecoil,
	// 命中TagとSurface Tagに応じてEffect・Sound・Decal・Actionを実行する
	ImpactResponder,
	// Colliderが属するGameplay Surfaceを文字列Tagで表す
	SurfaceType,
	// Runtime全体の時間倍率とHitStop継続時間を管理する
	TimeScale,
	AimAssist,
	InterceptPrediction,
	DamageDirectionIndicator,
	ObjectiveTracker,
	EncounterController,
	SpawnPointSet,
	DifficultyParameterSet,
	CameraFeedbackMixer,
	// 重力・線形Drag・Target加速度を含むProjectile軌道を数値積分する
	BallisticPrediction,
	// 同時被弾を寿命付きEntryとして複数保持する
	DamageEventBuffer,
	// ゲーム時間・物理・Audio・Input Mapをまとめて停止・再開する
	GamePause,
	// Ocean Sampleと船速から船尾Foam・船首Sprayを駆動する
	SurfaceWakeEmitter,
	// BallisticPredictionの計算点をScene/Game Viewへ線描画する
	TrajectoryRenderer,
	// GameObjectがFFT水面を横切った状態と水面情報を公開する
	WaterSurfaceState,
	// 所有者の前方距離ごとにFFT水面をまとめてSampleする
	OceanProbeSet,
	// 発射者階層、Team、明示Object、Arming距離で攻撃Castの命中対象を制限する
	AttackCollisionFilter,
	// Yaw/Pitch PivotをTargetへ可動範囲と速度付きで向ける
	TurretAim,
	// 複数Weaponを同時、順次、Round Robinで発射する
	WeaponGroup,
	// Projectileの残存Energy、貫通、跳弾を命中Surfaceごとに処理する
	ProjectileImpactPhysics,
	// 移動元の揺れを軸別に減衰してCameraへ継承する
	CameraHorizonStabilizer,
	// 砲口から前方の自艦構造物や遮蔽物を発射前に検査する
	FireLineCheck,
	// 時間制の汎用状態効果をID、Stack、Tick Actionで管理する
	StatusEffectSet,
	// レール進行率ごとの目標速度倍率を補間する
	RailSpeedProfile,
	// レール上の区間進入・退出と区間設定を管理する
	RailZone,
	// 追従CameraへDamping、Dead Zone、Look Aheadを追加する
	CameraFollowComposer,
	// 移動速度からFOV、Motion Blur、Camera Feedbackを駆動する
	SpeedFeedback,
	// Wave生成Objectへ共通初期設定を適用する
	SpawnedObjectSetup,
	// Wave内の生成Objectへ時間変化するレールOffsetを適用する
	WaveMotionProfile,
	// Camera等との距離を使いGameObject階層の実体化を切り替える
	DistanceActivation,
	// 距離段階ごとに重いComponent系統を停止する
	SimulationLOD,
	// Rail進行率を横切った時に名前付きActionを通知する
	RailEventMarker,
	// 距離に応じてAdditive Sceneを非同期読込・破棄する
	SceneStreaming,
	// 付いているGameObjectのProjectile発射・飛翔を詳細Markdown Logへ記録する(付け外しでOn/Off)
	ProjectileDebugLogger,
	// Component 種類数。範囲チェックに使う
	Count,
};

//============================================================
// C++ Script / PlayerInput の可変長データ
//============================================================

struct EditorScriptProperty {
	std::string name;  // C++ 側で ExposeFloat などへ渡した変数名。
	std::string displayName;  // Inspector に表示する日本語名。
	int32_t type = 0;  // EditorScriptFieldType と同じ値を保存する。
	bool boolValue = false;  // bool 公開変数の保存値。
	int32_t intValue = 0;  // int32_t 公開変数の保存値。
	float floatValue = 0.0f;  // float 公開変数の保存値。
	EditorScriptVector2 vector2Value{0.0f, 0.0f};  // Vector2 公開変数の保存値。DLL 境界と同じ型を使う。
	Vector3 vector3Value{0.0f, 0.0f, 0.0f};  // Vector3 公開変数の保存値。
	std::string stringValue;  // std::string 公開変数の保存値。
	float minValue = 0.0f;  // Inspector の入力下限。
	float maxValue = 0.0f;  // Inspector の入力上限。
	float step = 0.1f;  // Inspector のドラッグ変化量。
	bool hasRange = false;  // minValue / maxValue を使うなら true。
};

struct EditorNamedAttributeEntry {
	std::string name;
	float minimum = 0.0f;
	float maximum = 100.0f;
	float current = 100.0f;
	float regenerationPerSecond = 0.0f;
};

struct EditorGameplayDataEntry {
	std::string key;
	int32_t type = 0;  // 0=String、1=Int、2=Float、3=Bool、4=Asset Path
	std::string value;
};

struct EditorDamageTagModifierEntry {
	std::string tagName;
	float multiplier = 1.0f;
};

struct EditorCooldownEntry {
	std::string name;
	float duration = 1.0f;
	float remaining = 0.0f;
	bool startReady = true;
	bool wasRunning = false;
};

struct EditorThreatRuntimeEntry {
	int32_t projectileGameObjectId = -1;
	int32_t sourceGameObjectId = -1;
	float distance = 0.0f;
	float closingSpeed = 0.0f;
	float estimatedArrivalSeconds = 0.0f;
};

struct EditorImpactResponseEntry {
	std::string damageTag;
	std::string surfaceTag;
	std::string effectAssetPath;
	int32_t audioSourceGameObjectId = -1;
	int32_t decalGameObjectId = -1;
	int32_t cameraShakeGameObjectId = -1;
	int32_t actionTargetGameObjectId = -1;
	std::string actionName;
};

struct EditorObjectiveEntry {
	std::string objectiveId;
	std::string displayName;
	int32_t state = 0;  // 0=Inactive、1=Active、2=Completed、3=Failed
	float currentValue = 0.0f;
	float targetValue = 1.0f;
};

struct EditorEncounterWaveEntry {
	int32_t waveSpawnerGameObjectId = -1;
	float startDelay = 0.0f;
	bool waitsForAllDefeated = true;
};

struct EditorSpawnPointEntry {
	int32_t gameObjectId = -1;
	float weight = 1.0f;
};

struct EditorDifficultyOverrideEntry {
	int32_t difficultyIndex = 0;
	int32_t targetGameObjectId = -1;
	std::string componentName;
	std::string propertyName;
	int32_t valueType = 0;  // 0=Float、1=Int、2=Bool
	float floatValue = 0.0f;
	int32_t intValue = 0;
	bool boolValue = false;
};

struct EditorDamageEventRuntimeEntry {
	int32_t sourceGameObjectId = -1;
	Vector3 worldDirection{0.0f, 0.0f, 1.0f};
	float damage = 0.0f;
	int32_t damageTagId = 0;
	float remainingSeconds = 0.0f;
};

struct EditorOceanProbeEntry {
	float distance = 10.0f;  // 所有者からローカル方向へ測る距離
	bool isValid = false;  // Ocean範囲内をSampleできた場合だけtrue
	Vector3 position{0.0f, 0.0f, 0.0f};  // World空間の水面位置
	Vector3 normal{0.0f, 1.0f, 0.0f};  // World空間の水面法線
	Vector3 velocity{0.0f, 0.0f, 0.0f};  // World空間の水面速度
	float relativeHeight = 0.0f;  // 所有者基準点から水面までのWorld Y差
	float foam = 0.0f;  // 0..1の砕波・圧縮泡率
};

struct EditorWeaponGroupEntry {
	int32_t weaponGameObjectId = -1;  // HitscanWeaponまたはProjectileEmitter所有Object
	bool isEnabled = true;  // falseならGroup発射対象から一時除外する
};

struct EditorProjectileSurfaceModifierEntry {
	std::string surfaceTag;  // SurfaceTypeの文字列Tag。空ならDefaultとして扱う
	float penetrationLossMultiplier = 1.0f;  // Surface命中時のEnergy損失倍率
	float ricochetAngleOffset = 0.0f;  // 跳弾開始角度へ加えるDegree
	float energyRetentionMultiplier = 1.0f;  // 貫通・跳弾後の保持率倍率
};

struct EditorStatusEffectDefinitionEntry {
	std::string effectId;  // ScriptとAction Payloadで使う任意ID
	float duration = 5.0f;  // 1 Stackの有効秒数
	int32_t stackMode = 0;  // 0=Refresh、1=Stack、2=Ignore
	int32_t maximumStacks = 1;
	float tickInterval = 1.0f;  // 0以下ならTick Actionを送らない
	std::string startedActionName = "OnStatusEffectStarted";
	std::string tickActionName = "OnStatusEffectTick";
	std::string endedActionName = "OnStatusEffectEnded";
};

struct EditorStatusEffectRuntimeEntry {
	std::string effectId;
	int32_t sourceGameObjectId = -1;
	float remainingSeconds = 0.0f;
	float tickRemainingSeconds = 0.0f;
	int32_t stackCount = 1;
};

struct EditorRailSpeedKey {
	float normalizedProgress = 0.0f;  // レール始点0、終端1の位置
	float speedMultiplier = 1.0f;  // RailMovementの基礎速度へ掛ける倍率
};

struct EditorRailZoneEntry {
	std::string zoneId;  // ScriptとEditor表示で使う任意ID
	float startNormalized = 0.0f;
	float endNormalized = 0.25f;
	float speedMultiplier = 1.0f;
	EditorScriptVector2 movementRange{5.0f, 3.0f};
	bool overrideMovementRange = false;
	std::string enteredActionName = "OnRailZoneEntered";
	std::string exitedActionName = "OnRailZoneExited";
};

struct EditorRailEventMarkerEntry {
	std::string markerId;
	float normalizedProgress = 0.5f;
	int32_t directionMode = 0;  // 0=Both、1=Forward、2=Reverse
	bool triggerOnce = true;
	std::string actionName = "OnRailMarker";
	bool runtimeTriggered = false;
};

struct EditorInputEventBinding {
	std::string actionMapName;  // Input Actions 内の ActionMap 名。
	std::string actionName;  // Input Actions 内の Action 名。
	std::string functionName;  // C++ Script で BindAction した関数名。
	int32_t valueType = 0;  // 0=Button、1=Vector2。
};

struct EditorComponent {
	EditorComponentType type;  // Component の種類
	bool isActive;  // Inspector の有効チェック
	std::string assetPath;  // Model / Sprite / Audio などの Asset パス
	std::string textureAssetPath;  // Renderer が明示的に使う画像パス
	std::string normalTextureAssetPath;  // Renderer が使う Normal Map の画像パス
	std::string metallicTextureAssetPath;  // Renderer が使う Metallic Map の画像パス
	std::string roughnessTextureAssetPath;  // Renderer が使う Roughness Map の画像パス
	std::string ambientOcclusionTextureAssetPath;  // Renderer が使う AO Map の画像パス
	std::string emissionTextureAssetPath;  // Renderer が使う Emission Map の画像パス
	std::string heightTextureAssetPath;  // Renderer が使う Height Map の画像パス
	std::string opacityTextureAssetPath;  // Renderer が使う Opacity Map の画像パス
	std::string uvLayoutTextureAssetPath;  // UV 配置を確認するだけの画像パス。描画用 Base Color とは分離する
	bool useImportedMaterialTextures;  // true なら手動画像が空のスロットへ FBX 内画像を自動適用する
	Vector3 color;  // Renderer / Light で使う色
	float intensity;  // Light や Material の強さ
	float metallic;  // Renderer の金属感。0 は非金属、1 は金属
	float roughness;  // Renderer の粗さ。0 は鏡面、1 は粗い
	float ior;  // Renderer の屈折率。ガラスや水の見た目調整に使う
	float alpha;  // Renderer の透明度。1 は不透明
	int32_t lightingMode;  // 0=Lightingなし、1=Lambert、2=Half Lambert、3=PBR
	float reflectionStrength;  // Renderer の反射強度
	float emissionStrength;  // Renderer の放射強度。0 より大きいと自発光する
	Vector3 emissionColor;  // Renderer の放射色。Emission Map にも掛ける
	float normalScale;  // Normal Map の凹凸強度
	float ambientOcclusionStrength;  // AO Map が間接光へ与える強度
	float heightScale;  // Height Map から作る視差量
	float alphaCutoff;  // Mask 描画で破棄する Alpha 境界
	float clearCoat;  // クリアコート層の強度
	float clearCoatRoughness;  // クリアコート層の粗さ
	float transmission;  // 材質を透過する環境光の割合
	float subsurface;  // 表面下へ回り込む拡散光の割合
	float anisotropy;  // 接線方向へ伸びる反射の強度
	float anisotropyRotation;  // 異方性方向の回転量
	float materialThickness;  // 透過・表面下散乱で使う材質内部の光路長
	float materialWetness;  // 表面を暗くし、粗さを下げる濡れ量
	float materialWaterlineHeight;  // World Yで指定する水際の中心高さ
	float materialWaterlineWidth;  // 水際の濡れ遷移幅
	float specularTint;  // 鏡面色へベースカラーを混ぜる割合
	float sheen;  // 布の縁に出る反射の強度
	float sheenTint;  // Sheen 色へベースカラーを混ぜる割合
	int32_t alphaMode;  // 0=不透明、1=マスク、2=半透明
	bool doubleSided;  // true なら両面描画する
	EditorScriptVector2 uvTiling;  // Material Texture の UV 繰り返し数
	EditorScriptVector2 uvOffset;  // Material Texture の UV 開始位置
	float mass;  // RigidBody の質量
	bool automaticMassFromCollider;  // trueならCollider体積と実質密度から質量を決める
	float bodyDensity;  // 自動質量へ使う物体全体の実質密度 kg/m3。中空部分も含む
	float drag;  // RigidBody の速度減衰
	bool useGravity;  // RigidBody に重力を使うか
	bool isKinematic;  // true なら物理で動かさない
	bool isTrigger;  // true なら接触だけ検出して押し戻ししない
	float bounciness;  // 床衝突時の跳ね返り係数
	Vector3 velocity;  // RigidBody の現在速度
	Vector3 angularVelocity;  // RigidBody の現在角速度。回転方向と速さをラジアン毎秒で持つ
	float angularDrag;  // RigidBody の角速度減衰。回転をどれだけ止めやすくするか
	float inertiaMultiplier;  // Colliderから求めた慣性Tensorへ掛ける倍率。大きいほど回転しにくい
	Vector3 centerOfMassOffset;  // Colliderが求めた重心へ加えるローカルOffset。質量分布やBallastを表す
	bool applyGyroscopicForce;  // Joltのジャイロ効果を有効にし、非対称回転体の歳差運動を再現する
	bool freezePositionX;  // true なら物理更新で X 方向へ移動させない
	bool freezePositionY;  // true なら物理更新で Y 方向へ移動させない
	bool freezePositionZ;  // true なら物理更新で Z 方向へ移動させない
	bool freezeRotationX;  // true なら物理更新で X 軸回転させない
	bool freezeRotationY;  // true なら物理更新で Y 軸回転させない
	bool freezeRotationZ;  // true なら物理更新で Z 軸回転させない
	int32_t interpolationMode;  // 0: 補間なし / 1: 補間 / 2: 外挿
	int32_t collisionDetectionMode;  // 0: 離散 / 1: 連続。高速移動時のすり抜け対策に使う
	float dynamicFriction;  // 動いている接触面の摩擦。氷や床の滑りやすさを決める
	float staticFriction;  // 止まっている接触面の摩擦。坂や半分乗った時の粘りに効く
	int32_t frictionCombineMode;  // 0: 平均 / 1: 最小 / 2: 最大 / 3: 乗算
	int32_t bouncinessCombineMode;  // 0: 平均 / 1: 最小 / 2: 最大 / 3: 乗算
	int32_t physicsLayer;  // 物理衝突レイヤー。Layer Collision Matrix の行列参照に使う
	bool generateContactEvents;  // true なら Collision / Trigger の Enter Stay Exit を記録する
	int32_t connectedGameObjectId;  // Joint が接続する相手 GameObject ID。未設定は -1
	Vector3 jointAxis;  // Hinge / Character Joint が回転を許可するワールド軸
	float jointMinLimit;  // Hinge Joint の最小角度。ラジアンで保存する
	float jointMaxLimit;  // Hinge Joint の最大角度。ラジアンで保存する
	float jointMinDistance;  // Spring / Distance Joint が許す最短距離
	float jointMaxDistance;  // Spring / Distance Joint が許す最長距離
	float jointSpringFrequency;  // Joint 制限を柔らかく戻すばね周波数
	float jointSpringDamping;  // Joint 制限の減衰率。大きいほど揺れを抑える
	Vector3 colliderCenter;  // Collider の中心オフセット
	Vector3 colliderSize;  // BoxCollider の大きさ
	float colliderRadius;  // SphereCollider の半径
	int32_t autoConvexMaximumHulls;  // Auto Convexがモデルを非重複凸包へ分割する最大数
	float inputMoveSpeed;  // Input による平面移動速度
	int32_t inputForwardKey;  // 前進キーの DirectInput 番号
	int32_t inputBackKey;  // 後退キーの DirectInput 番号
	int32_t inputLeftKey;  // 左移動キーの DirectInput 番号
	int32_t inputRightKey;  // 右移動キーの DirectInput 番号
		int32_t inputJumpKey;  // ジャンプキーの DirectInput 番号
		float inputMouseSensitivity;  // マウス感度
		bool inputInvertY;  // Y 軸反転
		std::string inputActionMapName;  // PlayerInput が使う既定 Action Map 名
		int32_t inputBehavior;  // PlayerInput の通知方式。0 は Invoke C++ Events 相当
		std::string inputMoveEventName;  // Move Action が呼ぶ C++ 関数名
		std::string inputJumpEventName;  // Jump Action が呼ぶ C++ 関数名
		std::string inputFireEventName;  // Fire Action が呼ぶ C++ 関数名
		float hapticStrength;  // FeelKitHaptics の振動強度
		int32_t hapticDurationMs;  // 振動持続時間 (ms)
		bool hapticLoop;  // ループ再生
		float audioVolume;  // AudioSource の音量 (0〜1)
		float audioPitch;  // AudioSource のピッチ (1=通常)
		bool audioLoop;  // AudioSource のループ再生
		bool audioPlayOnAwake;  // Awake 時に自動再生
		float audioSpatialBlend;  // 3D空間ブレンド (0=2D, 1=3D)
		float audioMinDistance;  // 3D最小距離
		float audioMaxDistance;  // 3D最大距離
		int32_t audioBus;  // 0=SFX、1=BGM、2=Ambience、3=UI
		int32_t audioMaxVoices;  // 同じ AudioSource から同時に鳴らせる最大数
		float audioRetriggerInterval;  // 同じ AudioSource を再発音できるまでの秒数
		float audioDopplerLevel;  // 相対速度をPitchへ反映する強さ
		float audioSpread;  // 3D音源の左右への広がり角度
		float audioConeInnerAngle;  // 最大音量になる指向性コーン内角度
		float audioConeOuterAngle;  // 外側音量へ到達する指向性コーン外角度
		float audioConeOuterVolume;  // 指向性コーン外側の音量倍率
		float audioOcclusionStrength;  // Colliderで遮られた時の減衰強度
		float audioReverbSend;  // 残響Submixへ送る量
		float audioReflectionStrength;  // 初期反射音の量
		float navAgentRadius;  // NavigationAgent / NavMeshSurface の Agent 半径
		float navAgentHeight;  // NavigationAgent / NavMeshSurface の Agent 高さ
		float navMaxSpeed;  // NavigationAgent の最大速度
		float navMaxAcceleration;  // NavigationAgent の最大加速度
		float navStoppingDistance;  // NavigationAgent の停止距離
		bool navAutoRepath;  // NavigationAgent の自動再経路探索
		bool navCarve;  // NavMeshObstacle が移動中も NavMesh を更新するか
		float navMaxSlope;  // NavMeshSurface の最大登坂角度 (度)
	float navMaxClimb;  // NavMeshSurface の最大段差高さ
	bool navAreaOverride;  // NavMeshModifier の Area 上書きフラグ
	int32_t navArea;  // NavMeshModifier / ModifierVolume の Area 番号
	bool navIgnoreFromBuild;  // NavMeshModifier のビルド除外フラグ
	bool navBidirectional;  // NavMeshLink の双方向通行
	float navCostModifier;  // NavMeshLink のコスト倍率
	float rollingTorque;  // RollingMove が回転へ与える駆動トルク
	float rollingHorsepower;  // RollingMove の出力上限。回し続けられる角速度の上限計算に使う
	float constraintWeight;  // Constraint 全般の追従重み (0=切, 1=完全追従)
	Vector3 constraintPositionOffset;  // Position / Parent Constraint の位置オフセット
	Vector3 constraintRotationOffset;  // Rotation / Parent Constraint の回転オフセット
	int32_t constraintAimAxis;  // AimConstraint のターゲット方向軸 (0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z)
	int32_t constraintUpAxis;  // LookAtConstraint の上方向軸
	float constraintRoll;  // LookAtConstraint のロール角
	bool constraintFreezeAxisX;  // ScaleConstraint の X 軸フリーズ
	bool constraintFreezeAxisY;  // ScaleConstraint の Y 軸フリーズ
	bool constraintFreezeAxisZ;  // ScaleConstraint の Z 軸フリーズ
	float animationSpeed;  // Animation の再生速度倍率
	bool animationLoop;  // Animation のループ再生
	bool animationPlayOnAwake;  // Animation の自動再生
	int32_t animationType;  // 0=FBX Clip, 1=Float, 2=Rotate, 3=Pulse, 4=Bob
	float animationAmplitude;  // プロシージャルアニメーションの振幅
	int32_t animationClipIndex;  // Animation が再生する FBX Clip の配列番号
	int32_t animatorState;  // Animator の現在状態インデックス
	bool animatorApplyRootMotion;  // true なら Clip の移動差分を GameObject の Transform へ適用する
	bool animatorAutoVelocity;  // true なら Rigidbody 速度から MoveX / MoveY / Speed を自動更新する
	float animatorTransitionDuration;  // 既定状態遷移で前後の Pose を混ぜる秒数
	float animatorMoveX;  // Animator のローカル左右入力。-1 が左、1 が右
	float animatorMoveY;  // Animator のローカル前後入力。-1 が後、1 が前
	float animatorSpeedParameter;  // Animator の移動速度 Parameter
	int32_t animatorIdleClipIndex;  // 方向入力がない時に使う Clip
	int32_t animatorForwardClipIndex;  // 前方向へ移動する時に使う Clip
	int32_t animatorBackwardClipIndex;  // 後方向へ移動する時に使う Clip
	int32_t animatorLeftClipIndex;  // 左方向へ移動する時に使う Clip
	int32_t animatorRightClipIndex;  // 右方向へ移動する時に使う Clip
	float particleRate;  // ParticleSystem の発生レート (個/秒)
	float particleLifetime;  // ParticleSystem のパーティクル寿命 (秒)
	float particleSpeed;  // ParticleSystem の初期速度
	float particleSize;  // ParticleSystem のパーティクルサイズ
	int32_t particleMaxCount;  // 同じ Emitter が同時に保持できる最大 Particle 数
	int32_t particleBurstCount;  // 再生開始時に一度だけ発生させる Particle 数
	int32_t particleShape;  // 0=Point、1=Sphere、2=Cone、3=Box
	int32_t particleSimulationSpace;  // 0=World、1=Local
	float particleDuration;  // 1 周分の発生時間。Loop 無効時はこの時間で発生を停止する
	float particleStartDelay;  // Play 開始から発生開始までの待機秒数
	float particleGravity;  // Particle へ毎秒加える下方向加速度
	float particleDrag;  // Particle 速度を毎秒減衰させる係数
	float particleEndSize;  // 寿命終了時の Particle サイズ
	float particleShapeRadius;  // Sphere / Cone 発生位置の半径
	float particleShapeAngle;  // Cone の開き角度（度）
	float particleSpeedRandomness;  // 初速度へ加える 0 から 1 のランダム幅
	float particleLifetimeRandomness;  // 寿命へ加える 0 から 1 のランダム幅
	float particleSizeRandomness;  // 初期サイズへ加える 0 から 1 のランダム幅
	float particleRotationSpeed;  // Particle の Y 軸回転速度（度/秒）
	bool particleLooping;  // true なら Duration 終了後に Emitter を再開する
	bool particleCollision;  // true なら簡易 Ground 衝突で Particle を跳ね返す
	Vector3 particleEndColor;  // 寿命終了時の Particle 色
	Vector3 particleDirection;  // Emitter の基準放出方向
	Vector3 particleBoxSize;  // Box Shape の発生範囲
	float particleStartAlpha;  // 発生時の不透明度
	float particleEndAlpha;  // 寿命終了時の不透明度
	float particleEmissionStrength;  // Particle Material の放射強度
	float particleEndSpeedMultiplier;  // 寿命終了時に初速度へ掛ける倍率
	float particleNoiseStrength;  // 乱流が速度へ加える加速度
	float particleNoiseFrequency;  // 乱流方向が変化する周波数
	float particleCollisionBounce;  // Ground 衝突時に残す Y 速度の割合
	float particleCollisionFriction;  // Ground 衝突時に減らす水平速度の割合
	bool particlePrewarm;  // Loop Effect を開始時から進行済みの見た目にする
	int32_t particleMotionType;  // 0=直線、1=軌道、2=渦、3=波/航跡、4=吸引、5=雲、6=爆発/水しぶき、7=弾道、8=水煙。
	Vector3 particleMotionCenter;  // 軌道・渦・吸引運動の中心を Emitter からの相対位置で指定する
	float particleAngularSpeed;  // 軌道・渦運動の角速度（度/秒）
	float particleRadialAcceleration;  // 中心から外向きへ加える加速度。負なら中心へ寄る
	float particleWaveAmplitude;  // 波運動で上下へ揺らす加速度の大きさ
	float particleWaveFrequency;  // 波運動と雲のうねりが 1 秒間に変化する回数
	float particleAttractorStrength;  // 吸引運動で中心へ加える加速度
	std::string particleRenderAssetPath;  // Particle 1 個の描画形状に使う FBX / OBJ。空なら板ポリゴン
	int32_t particleBillboardMode;  // 0=Camera Facing、1=Y軸固定、2=Velocity Facing、3=World XY固定
	float particleBillboardStretch;  // Velocity Facing時に速度方向へ掛ける板の長さ倍率
	// PostProcess 設定
	float bloomIntensity;  // Bloom の強さ。0 で Bloom OFF
	float bloomThreshold;  // Bloom 輝度しきい値
	float bloomSoftKnee;  // Bloom しきい値遷移の softness
	float bloomScatter;  // Bloom のにじみ広がり (0=狭, 1=広)
	int32_t aaMode;  // 0=None, 1=FXAA, 2=SMAA, 3=Temporal
	float smaaThreshold;  // SMAA が輪郭として扱う輝度差
	float smaaCornerRounding;  // SMAA の角部分の丸め量
	float temporalSharpness;  // Temporal 後に戻す輪郭の強さ
	float temporalBlendRatio;  // Temporal の履歴色を混ぜる割合
	int32_t glareMode;  // 0=無効、1=Bloom、2=Ghosts、3=Streaks、4=Fog Glow、5=Simple Star、6=Sun Beams、7=Kernel
	int32_t glareModeMask;  // bit 1-7 に有効な Glare 方式を保持し、複数方式を直列合成する
	float glareIntensity;  // Glare を元画像へ加算する強さ
	float glareSize;  // Glare の広がり。サンプル間隔と光条の長さに使う
	float glareAngle;  // Streaks / Simple Star の基準角度（度）
	int32_t glareStreakCount;  // 光の筋を放射する方向数
	float glareFade;  // 光条が中心から離れるほど減衰する割合
	float glareColorModulation;  // Ghosts / Streaks の色ずれ量
	Vector3 glareCenter;  // Sun Beams の光源位置。x/y は画面 UV、z は予約値
	std::array<float, 8> glareIntensityByMode;  // Glare 種類ごとの強さ。index は glareMode と同じ
	std::array<float, 8> glareSizeByMode;  // Glare 種類ごとの広がり / 長さ
	std::array<float, 8> glareAngleByMode;  // Glare 種類ごとの角度（度）
	std::array<int32_t, 8> glareStreakCountByMode;  // Glare 種類ごとの光条数
	std::array<float, 8> glareFadeByMode;  // Glare 種類ごとの減衰
	std::array<float, 8> glareColorModulationByMode;  // Glare 種類ごとの色ずれ
	std::array<Vector3, 8> glareCenterByMode;  // Glare 種類ごとの画面上の中心位置 / 予約値
	std::array<Vector3, 8> glareColorByMode;  // Glare 種類ごとの色
	int32_t filterMode;  // 0=無効、1=Soften、2=Box Sharpen、3=Diamond Sharpen、4=Laplace、5=Sobel、6=Prewitt、7=Kirsch、8=Shadow
	int32_t filterModeMask;  // bit 1-8 に有効な Filter を保持し、追加順相当の固定順で直列合成する
	float filterStrength;  // Filter 結果を元画像へ混ぜる強さ
	std::array<float, 9> filterStrengthByMode;  // Filter 種類ごとの強さ。index は filterMode と同じ
	std::array<Vector3, 9> filterColorByMode;  // Filter 種類ごとの色。輪郭抽出の色付けにも使う
	float finalBrightness;  // 最終合成の明るさ
	bool smaaEnabled;  // SMAA 有効（旧aaMode）
	bool taaEnabled;  // TAA 有効（旧aaMode）
	bool ssrEnabled;  // SSR 有効
	// Final Composite 設定（FinalComposite.PS.hlsl の定数バッファに対応）
	float compositeExposure;  // 露出
	float compositeWhitePoint;  // ホワイトポイント
	int32_t compositeToneMappingMode;  // 0=Reinhard, 1=Filmic, 2=Timothy, 3=Uncharted2, 4=ACES
	float compositeBloomIntensity;  // Bloom 合成強さ
	float compositeSaturation;  // 彩度
	float compositeContrast;  // コントラスト
	float compositeVignetteStrength;  // ビネット
	float compositeVignetteRadius;  // ビネット半径
	float compositeFilmGrain;  // フィルムグレイン
	float compositeChromaticAberration;  // 色収差
	float compositeAmbientOcclusionStrength;  // AO強度
	bool compositeAutoExposureEnabled;  // 画面平均輝度から露出を自動調整する
	float compositeMinimumExposure;  // 自動露出の下限
	float compositeMaximumExposure;  // 自動露出の上限
	float compositeExposureAdaptationSpeed;  // 明暗へ追従する速度
	float compositeTargetLuminance;  // 自動露出が合わせる中間輝度
	float compositeTemperature;  // 赤青方向のホワイトバランス
	float compositeTint;  // 緑紫方向のホワイトバランス
	Vector3 compositeLift;  // 暗部へ加える色
	float compositeGamma;  // 中間調ガンマ
	Vector3 compositeGain;  // 明部へ掛ける色
	float compositeLocalContrast;  // 近傍輝度との差から中間調の細部を強調する
	float compositeOutputDither;  // 最終出力の階調縞を抑えるディザリング強度
	bool compositeSsgiEnabled;  // GBufferとDepthから画面空間の間接光を加える
	float compositeSsgiIntensity;  // SSGIの色回り込み強度
	float compositeSsgiRadiusPixels;  // SSGIが近傍を探索する画面半径
	std::string compositeColorLutAssetPath;  // 32x32x32等を横へ並べた2D strip LUT画像
	float compositeColorLutStrength;  // Identity色とLUT結果の補間量
	// Environment 設定
	Vector3 skyLowerColor;  // 地平線 / 下側の空色
	float environmentTextureRotation;  // 環境テクスチャの水平回転（ラジアン）
	float environmentTextureMipBias;  // 反射時のMIPバイアス
	bool environmentTextureEnabled;  // 環境テクスチャを使うか
	bool volumetricCloudEnabled;  // プロシージャル体積雲を空へ合成するか
	float volumetricCloudCoverage;  // 雲が空を覆う割合
	float volumetricCloudDensity;  // 雲層の光学密度
	float volumetricCloudScale;  // 雲ノイズのWorld空間スケール
	float volumetricCloudSpeed;  // 風による雲移動速度
	float volumetricCloudHeight;  // 雲層下端のWorld Y
	float volumetricCloudThickness;  // 雲層の厚さ
	float volumetricCloudLightAbsorption;  // 雲内部でSun光を吸収する量
	float volumetricCloudSilverLining;  // Sun方向の縁光強度
	Vector3 volumetricCloudColor;  // 雲の散乱色
	// Camera 設定
	float cameraFieldOfView;  // 視野角（度）
	float cameraNearClip;  // ニアクリップ距離
	float cameraFarClip;  // ファークリップ距離
	int32_t cameraProjectionMode;  // 0=Perspective, 1=Orthographic
	bool cameraDofEnabled;  // 被写界深度有効
	float cameraDofFocusDistance;  // フォーカス距離
	float cameraDofAperture;  // 絞り（ボケ量）
	float cameraDofFocalLength;  // 焦点距離
	bool cameraMotionBlurEnabled;  // モーションブラー有効
	float cameraMotionBlurIntensity;  // ブラー強度
	float cameraExposure;  // 露出補正 (EV)
	std::string buttonLabel;  // Game View に表示する Button の文字。
	EditorScriptVector2 buttonPosition;  // Game View 左上からの Button 表示位置。
	EditorScriptVector2 buttonSize;  // Game View 上の Button サイズ。
	bool buttonInteractable;  // false なら見えるがクリックできない。
	Vector3 buttonHoverColor;  // マウスが乗った時の Button 色。
	Vector3 buttonPressedColor;  // 押している間の Button 色。
	std::string buttonOnClickFunction;  // クリック時に C++ Script へ通知する関数名。
	bool toggleValue;  // Toggle の現在値。
	std::string toggleOnValueChangedFunction;  // Toggle 変更時に C++ Script へ通知する関数名。
	float sliderValue;  // Slider の現在値。
	float sliderMinValue;  // Slider の最小値。
	float sliderMaxValue;  // Slider の最大値。
	std::string sliderOnValueChangedFunction;  // Slider 変更時に C++ Script へ通知する関数名。
	std::vector<EditorScriptProperty> scriptProperties;  // DLL が公開した変数と GameObject ごとの保存値。
	std::vector<EditorInputEventBinding> inputEventBindings;  // Input Action と C++ 関数名の接続一覧。
	// FreeTransform 設定
	float freeMoveSpeed;
	float freeRotateSpeed;
	int32_t freeMoveAxes;  // bit 0=X, 1=Y, 2=Z
	int32_t freeRotateAxes;  // bit 0=X, 1=Y, 2=Z
	bool freeUseLocalSpace;
	Vector3 freeRotationInput;  // deg/sec per axis
	// Ocean 設定
	int32_t oceanGridResolution;  // 海面グリッドの一辺に使う分割数
	float oceanSize;  // 海面メッシュ一辺のワールド寸法
	float oceanWaveHeight;  // 主波の高さ
	float oceanMaxWaveHeight;  // 複数波を合成した後の高さ上限
	float oceanWaveLength;  // 主波の波長
	float oceanWaveSpeed;  // 波位相の進行速度
	float oceanTimeScale;  // Ocean 全体の時間倍率。0 なら停止
	float oceanChoppiness;  // 波頂点の水平押し出し量
	EditorScriptVector2 oceanPrimaryDirection;  // 主波の XZ 方向
	EditorScriptVector2 oceanSecondaryDirection;  // 副波の XZ 方向
	float oceanSecondaryWaveScale;  // 副波レイヤの強さ
	float oceanRippleScale;  // 主波長に対する細波波長の比率
	float oceanRippleStrength;  // 細波の高さ比率
	float oceanWindSpeed;  // スペクトルへ与える風速
	float oceanWaterDepth;  // 有限水深の分散計算に使う水深
	float oceanDirectionSpread;  // 風向きから波方向を散らす角度幅
	float oceanSwellStrength;  // 長いうねり帯域の強さ
	float oceanSpectrumSeed;  // 波成分を固定生成するシード
	float oceanCrestSharpness;  // 二次高調波で波頭を尖らせる強さ
	float oceanFoamStrength;  // 急斜面へ出す泡の強さ
	float oceanFoamThreshold;  // 圧縮泡が出始める閾値
	float oceanRoughness;  // 海面反射の粗さ
	float oceanReflectionStrength;  // 海面の環境反射強度
	float oceanDetailNormalStrength;  // ピクセル単位の微細波法線強度
	float oceanAbsorptionDistance;  // 水色が深海色へ吸収される距離
	float oceanRefractionDistortion;  // 微細波による屈折方向の歪み
	Vector3 oceanShallowColor;  // 光が届く浅い海面の色
	Vector3 oceanDeepColor;  // 深い海面の色
	// Buoyancy 設定
	int32_t buoyancyOceanGameObjectId;  // 対象 Ocean。-1 は現在位置を覆う Ocean を自動検出
	Vector3 buoyancyCenterOffset;  // 船体中心から浮力領域中心までのローカル差分
	Vector3 buoyancyHullSize;  // 浮力点を置く船体幅、高さ、長さ
	float buoyancyStrength;  // 1m 沈んだ時に発生する上向き加速度
	float buoyancyMaxSubmersion;  // 旧 Scene の読み書き互換用。体積浮力では使用しない
	float buoyancyDamping;  // 水面に対する上下速度の減衰
	float buoyancyWaterDrag;  // 浸水率に応じた船体全体の速度抵抗
	float buoyancyAngularDrag;  // 浸水率に応じた角速度抵抗
	float buoyancyNormalInfluence;  // 浮力方向へ波面法線を混ぜる割合
	float buoyancyLateralDrag;  // 船体横方向の水抵抗。横滑りを前後移動より強く止める
	float buoyancyVerticalDrag;  // 船体上下方向の水抵抗。着水時の沈み込みを抑える
	float buoyancySlammingStrength;  // 水面へ入る速度に応じて加える着水衝撃の強さ
	bool buoyancyUseCenterPoint;  // 旧 Scene の読み書き互換用。自動セル配置では使用しない
	bool buoyancyAutomaticPhysicalProperties;  // trueなら水密度と実Shapeから浮力・抗力・減衰を決める
	float buoyancyWaterDensity;  // 自動物理で使う流体密度 kg/m3。海水は約1025
	float buoyancyTargetSubmersionRatio;  // 自動質量へ使う平衡時の目標水没体積率
	// Rail Movement 設定
	int32_t railPathGameObjectId;  // 直下の子を制御点として使う親 GameObject ID
	float railSpeed;  // レール上を1秒間に進む距離
	float railStartNormalized;  // レール全長に対する開始位置。0～1
	float railLookAheadDistance;  // 進行方向を決めるために先読みする距離
	float railAcceleration;  // 目標速度へ近づく毎秒の加速度。0 以下なら即時変更
	float railDeceleration;  // 停止または減速時の毎秒の減速度。0 以下なら即時変更
	bool railLoop;  // 終端から始点へつなげるなら true
	bool railOrientToPath;  // 進行方向へ自動回転するなら true
	bool railUseSmoothCurve;  // Catmull-Rom 曲線で制御点間を補間するなら true
	bool railStartPaused;  // Play 開始時に外部から再開されるまで停止するなら true
	bool railReverse;  // Play 開始時にレールの逆方向へ進むなら true
	bool railStopAtEnd;  // 非ループ終端へ到達した時に停止するなら true
	int32_t railMovementMode;  // 0=Transform 追従、1=Dynamic Rigidbody へ力を加える物理追従
	Vector3 railPositionInfluence;  // 物理追従で位置補正するワールド軸。0～1
	Vector3 railRotationInfluence;  // 物理追従で回転補正する軸。0～1
	float railPositionSpring;  // レール位置誤差へ掛ける PD 制御のばね係数
	float railPositionDamping;  // レール目標速度との差へ掛ける PD 制御の減衰係数
	float railMaximumAcceleration;  // 物理追従が要求できる最大加速度
	float railRotationSpring;  // レール向き誤差へ掛ける角度ばね係数
	float railRotationDamping;  // 角速度へ掛ける角度減衰係数
	float railMaximumAngularAcceleration;  // 物理追従が要求できる最大角加速度
	int32_t railLocalForwardAxis;  // 船体推進で前方として使うローカル軸。0=+Z、1=-Z、2=+X、3=-X
	bool railShipHorizontalThrust;  // trueならPitch/Rollで推進力を上下へ傾けず水平方向へ加える
	float railShipLateralAssist;  // 船体推進でレール横ずれを直接戻す補助率。0なら操舵だけ
	float railMaximumRollAngle;  // レール移動時の最大ロール角度（度）。0なら制限なし
	float railRollRestorationStrength;  // ロールを直立に戻す復元力の強さ
	float railRollDamping;  // ロール角速度へのダンピング
	float railMaximumPitchAngle;  // レール移動時の最大ピッチ角度（度）。0なら制限なし
	float railPitchRestorationStrength;  // ピッチを水平に戻す復元力の強さ
	float railPitchDamping;  // ピッチ角速度へのダンピング
	float railMaximumYawAngle;  // レール移動時の最大ヨー角度（度）。0なら制限なし
	float railYawRestorationStrength;  // ヨーを進行方向に戻す復元力の強さ
	float railYawDamping;  // ヨー角速度へのダンピング
	EditorScriptVector2 railMovementRange;  // レール中心から左右・上下へ移動できる最大距離
	EditorScriptVector2 railStartOffset;  // Play 開始時のレール右・上方向オフセット
	float railOffsetMoveSpeed;  // Vector2 入力をレール内移動距離へ変換する毎秒速度
	bool railUsePlayerInput;  // 同じ GameObject の PlayerInput Vector2 Action を自動で使うなら true
	std::string railInputActionMapName;  // レール内移動へ使う Action Map 名
	std::string railInputActionName;  // レール内移動へ使う Vector2 Action 名
	// Aerodynamics 設定
	float aerodynamicAirDensity;  // 流体密度 kg/m^3。標準大気は約 1.225
	float aerodynamicDragCoefficient;  // 二次抗力 F=1/2*rho*Cd*A*v^2 の Cd
	float aerodynamicReferenceArea;  // 抗力計算へ使う代表面積 m^2
	float aerodynamicBaseLiftCoefficient;  // 迎角 0 の揚力係数
	float aerodynamicLiftSlope;  // 迎角 1 rad あたりの揚力係数増加量
	float aerodynamicLiftArea;  // 揚力計算へ使う翼面積 m^2
	float aerodynamicSideForceCoefficient;  // 横滑り角へ掛ける横力係数
	float aerodynamicSideArea;  // 横力計算へ使う側面積 m^2
	float aerodynamicZeroLiftAngleDegrees;  // 揚力が 0 になる迎角 degree
	float aerodynamicStallAngleDegrees;  // 揚力低下を開始する絶対迎角 degree
	float aerodynamicAngularDragCoefficient;  // 角速度の二乗へ比例する回転抗力係数
	float aerodynamicMagnusCoefficient;  // 回転体へ加える Magnus 効果の係数
	Vector3 aerodynamicCenterOfPressure;  // 力を加えるローカル圧力中心。重心との差がトルクになる
	Vector3 aerodynamicAmbientWindVelocity;  // WindZone へ加算するワールド空間の基礎風速 m/s
	float aerodynamicMaximumForce;  // 数値暴走を防ぐ合力上限 N。0 以下なら制限しない
	// WindZone 設定
	int32_t windZoneMode;  // 0=方向風、1=中心から外向きの放射風
	Vector3 windZoneDirection;  // 方向風のワールド方向
	float windZoneSpeed;  // 基準風速 m/s
	float windZoneRadius;  // 影響半径 m。0 以下の方向風は Scene 全域
	float windZoneTurbulenceStrength;  // 基準風速へ重ねる乱流速度 m/s
	float windZoneTurbulenceFrequency;  // 乱流位相の毎秒進行量
	// GravityField 設定
	int32_t gravityFieldMode;  // 0=Newton の逆二乗、1=距離によらない定加速度
	float gravityFieldGravitationalConstant;  // 万有引力定数 G。Scene スケールに合わせて変更可能
	float gravityFieldSourceMass;  // 引力源の質量 kg
	float gravityFieldAcceleration;  // 定加速度モードの加速度 m/s^2
	float gravityFieldMinimumDistance;  // 特異点を避ける最小計算距離 m
	float gravityFieldInfluenceRadius;  // 影響半径 m。0 以下なら無限
	float gravityFieldMaximumAcceleration;  // 安全のための加速度上限 m/s^2。0 以下なら制限しない
	// RotatingFrame 設定
	Vector3 rotatingFrameAngularVelocity;  // 回転座標系のワールド角速度 rad/s
	Vector3 rotatingFrameAngularAcceleration;  // 角速度変化による Euler 力用のワールド角加速度 rad/s^2
	Vector3 rotatingFrameLinearVelocity;  // 回転中心自体のワールド移動速度 m/s
	float rotatingFrameRadius;  // 疑似力を加える中心からの半径 m。0 以下なら無限
	float rotatingFrameMaximumAcceleration;  // 疑似加速度の安全上限 m/s^2。0 以下なら制限しない
	// FluidVolume 設定
	Vector3 fluidVolumeSize;  // 流体が満たすローカル箱領域の幅、高さ、奥行き m
	float fluidDensity;  // Archimedes浮力と二次抗力へ使う密度 kg/m^3
	float fluidDynamicViscosity;  // Stokes抵抗へ使う動粘度ではなく粘性係数 Pa*s
	float fluidDragCoefficient;  // 二次流体抗力へ使う代表抗力係数
	Vector3 fluidFlowVelocity;  // 流体自体のWorld流速 m/s
	float fluidAngularViscosity;  // 浸水率に応じて角速度へ掛ける粘性Torque係数
	float fluidMaximumForce;  // 浮力と抵抗を合成したForce上限 N。0以下なら制限しない
	// SpringForce 設定
	int32_t springForceTargetGameObjectId;  // 接続先GameObject。-1ならWorld固定点を使う
	Vector3 springForceLocalAnchor;  // 所有者側のローカル取付位置
	Vector3 springForceTargetLocalAnchor;  // 接続先GameObject側のローカル取付位置
	Vector3 springForceWorldAnchor;  // 接続先未設定時のWorld固定点
	float springForceRestLength;  // Hooke則で力が0になる自然長 m
	float springForceStiffness;  // Hooke則 F=-kx のばね定数 N/m
	float springForceDamping;  // ばね軸方向の相対速度へ掛ける減衰 Ns/m
	float springForceMaximumForce;  // 数値暴走を防ぐ絶対Force上限 N。0以下なら制限しない
	bool springForceApplyReaction;  // 接続先がDynamic Rigidbodyなら反作用を加える
	// RopeConstraint 設定
	int32_t ropeTargetGameObjectId;  // 接続先GameObject。-1ならWorld固定点を使う
	Vector3 ropeLocalAnchor;  // 所有者側のローカル取付位置
	Vector3 ropeTargetLocalAnchor;  // 接続先側のローカル取付位置
	Vector3 ropeWorldAnchor;  // 接続先未設定時のWorld固定点
	float ropeMaximumLength;  // 張力が発生し始める最大長 m
	float ropeStiffness;  // 伸び量へ掛ける張力係数 N/m
	float ropeDamping;  // 2点が離れる速度へ掛ける減衰 Ns/m
	float ropeMaximumTension;  // 数値安定化用の張力上限 N。0以下なら制限しない
	float ropeBreakingTension;  // この張力を超えたら破断する。0以下なら破断しない
	bool ropeApplyReaction;  // 接続先がDynamic Rigidbodyなら反作用を加える
	bool ropeIsBroken;  // Play中の破断状態。Play開始時にfalseへ戻す
	float ropeCurrentLength;  // Play中に計測したAnchor間距離 m
	float ropeCurrentTension;  // Play中に計算した張力 N。たるみ・切断時は0
	// TorsionSpring 設定
	int32_t torsionTargetGameObjectId;  // 基準回転を持つ接続先。-1ならWorld回転を使う
	Vector3 torsionRestRotation;  // 接続先から見た目標相対回転。World接続時は目標World回転 rad
	float torsionStiffness;  // 角度誤差へ掛ける回転ばね定数 N*m/rad
	float torsionDamping;  // 相対角速度へ掛ける回転減衰 N*m*s/rad
	float torsionMaximumTorque;  // 数値安定化用Torque上限 N*m。0以下なら制限しない
	bool torsionApplyReaction;  // 接続先がDynamic Rigidbodyなら反作用Torqueを加える
	// Thruster 設定
	Vector3 thrusterDirection;  // 推進方向。ローカル方向またはWorld方向
	Vector3 thrusterLocalApplicationPoint;  // Forceを加えるローカル作用点。重心との差でTorqueが生じる
	float thrusterForce;  // スロットル1の推進力 N。負値なら逆噴射
	float thrusterThrottle;  // 推進力へ掛ける0～1の入力値
	bool thrusterUseLocalDirection;  // trueならGameObjectの回転を推進方向へ反映する
	// PulleyConstraint 設定
	int32_t pulleyTargetGameObjectId;  // 滑車の反対側へ接続するGameObject
	Vector3 pulleyOwnerLocalAnchor;  // 所有者側Bodyのローカル取付位置
	Vector3 pulleyTargetLocalAnchor;  // 接続先Bodyのローカル取付位置
	Vector3 pulleyOwnerWorldSupport;  // 所有者側ロープを通すWorld支持点
	Vector3 pulleyTargetWorldSupport;  // 接続先側ロープを通すWorld支持点
	float pulleyTotalLength;  // lenA + ratio * lenB が超えられない全長 m
	float pulleyRatio;  // 接続先側の移動量へ掛ける滑車比
	float pulleyStiffness;  // 全長超過へ掛ける張力係数 N/m
	float pulleyDamping;  // 全長の増加速度へ掛ける減衰 Ns/m
	float pulleyMaximumTension;  // 数値安定化用の張力上限 N。0以下なら制限しない
	float pulleyBreakingTension;  // この張力を超えたら破断する。0以下なら破断しない
	bool pulleyIsBroken;  // Play中の破断状態。Play開始時にfalseへ戻す
	// PhysicsServo 設定
	int32_t servoTargetGameObjectId;  // 追従先GameObject。-1ならWorld目標を使う
	Vector3 servoTargetPosition;  // 接続先使用時はWorld位置オフセット、未設定時はWorld目標位置
	Vector3 servoTargetRotation;  // 接続先使用時は相対回転、未設定時はWorld目標回転 rad
	float servoPositionStiffness;  // 位置誤差へ掛ける比例Gain N/m
	float servoPositionDamping;  // 相対速度へ掛ける微分Gain Ns/m
	float servoMaximumForce;  // 位置制御Force上限 N。0以下なら制限しない
	float servoRotationStiffness;  // 角度誤差へ掛ける比例Gain N*m/rad
	float servoRotationDamping;  // 相対角速度へ掛ける微分Gain N*m*s/rad
	float servoMaximumTorque;  // 姿勢制御Torque上限 N*m。0以下なら制限しない
	bool servoApplyReaction;  // 追従先がDynamic Rigidbodyなら反作用を加える
	// VortexField 設定
	Vector3 vortexAxis;  // 回転流のローカル軸
	float vortexRadius;  // 影響半径 m。0以下なら無限
	float vortexAngularVelocity;  // 剛体回転流の角速度 rad/s
	float vortexRadialInflowVelocity;  // 中心軸へ吸い込む目標速度 m/s
	float vortexAxialVelocity;  // 軸方向へ流す目標速度 m/s
	float vortexVelocityCoupling;  // 物体速度を流速へ近づける結合率 1/s
	float vortexMaximumAcceleration;  // 数値安定化用加速度上限 m/s2
	// PressureField 設定
	float pressureFieldPressure;  // 放射方向の圧力 Pa。負値なら中心へ吸引する
	float pressureFieldRadius;  // 影響半径 m。0以下なら無限
	float pressureFieldFalloffExponent;  // 距離減衰 (1-d/r)^n の指数
	float pressureFieldMaximumForce;  // 数値安定化用Force上限 N。0以下なら制限しない
	// Suspension 設定
	Vector3 suspensionLocalAnchor;  // RigidbodyへForceを加えるローカル取付位置
	Vector3 suspensionLocalDirection;  // 接地Rayを飛ばすローカル方向
	float suspensionRestLength;  // 荷重がない時のばね長 m
	float suspensionMaximumLength;  // 接地を探す最大ストローク長 m
	float suspensionWheelRadius;  // 接触面からばね先端まで確保する半径 m
	float suspensionStiffness;  // 圧縮量へ掛けるばね定数 N/m
	float suspensionDamping;  // 圧縮速度へ掛ける減衰 Ns/m
	float suspensionMaximumForce;  // 数値安定化用Force上限 N。0以下なら制限しない
	bool suspensionUseHitNormal;  // trueなら接触法線、falseならサスペンション軸へForceを加える
	bool suspensionApplyReaction;  // 接地先がDynamic Rigidbodyなら反作用Forceを加える
	bool suspensionIsGrounded;  // Play中に接地Rayが有効な面へ当たっているか
	float suspensionCurrentLength;  // Play中の接地点までの実効ばね長 m
	// UprightStabilizer 設定
	Vector3 uprightLocalUpAxis;  // 安定させるGameObjectローカル方向
	Vector3 uprightTargetWorldUp;  // 合わせるWorld方向。通常は上方向
	float uprightStiffness;  // 角度誤差へ掛ける比例Gain N*m/rad
	float uprightDamping;  // 角速度へ掛ける微分Gain N*m*s/rad
	float uprightMaximumTorque;  // 数値安定化用Torque上限 N*m。0以下なら制限しない
	// ElectromagneticBody 設定
	float electromagneticCharge;  // Coulomb力とLorentz力へ使う電荷 C
	Vector3 electromagneticMagneticMoment;  // 物体ローカル空間の磁気双極子モーメント A*m^2
	float electromagneticMaximumForce;  // 電磁Force上限 N。0以下なら制限しない
	float electromagneticMaximumTorque;  // 磁気Torque上限 N*m。0以下なら制限しない
	// ElectromagneticField 設定
	int32_t electromagneticFieldMode;  // 0=一様場、1=点電荷による逆二乗電場
	Vector3 electromagneticElectricField;  // 一様電場 E N/C
	Vector3 electromagneticMagneticField;  // 一様磁束密度 B T
	float electromagneticSourceCharge;  // 点電荷モードの源電荷 C
	float electromagneticCoulombConstant;  // Coulomb定数 k。Scene縮尺に合わせて変更可能
	float electromagneticMinimumDistance;  // 点電荷中心の特異点を避ける距離 m
	float electromagneticInfluenceRadius;  // 電磁場の影響半径 m。0以下なら無限
	// Health 設定
	float healthMaximum;  // Play 開始時に設定する最大体力
	float healthCurrent;  // Play 中の現在体力
	// WaveSpawner 設定
	int32_t waveTriggerMode;  // 0=Play開始、1=RailFollower進行率、2=外部開始、3=距離
	int32_t waveTriggerSourceGameObjectId;  // Rail進行率または距離を読む GameObject。未設定は -1
	float waveTriggerValue;  // Mode 1は開始進行率、Mode 3は開始距離m
	float waveSpawnInterval;  // Wave 内の各Objectを生成する間隔秒
	int32_t waveSpawnMaximumPerFrame;  // 生成間隔0や低FPS時に1Frameへ集中させない最大生成数
	bool waveDeactivateChildrenOnStart;  // 互換子方式で Play 開始時に子を待機させる
	int32_t waveSpawnSourceMode;  // 0=ObjectPool生成、1=事前配置した子を有効化する互換方式
	int32_t wavePoolGameObjectId;  // 生成元 ObjectPool。未設定時は互換子方式へフォールバック
	int32_t waveSpawnPointGameObjectId;  // Waveの基準姿勢。未設定時は所有GameObject
	int32_t waveSpawnCount;  // ObjectPoolから生成する総数
	int32_t waveFormationPattern;  // 0=同一点、1=横列、2=V字、3=円、4=グリッド
	float waveFormationSpacing;  // 編隊内Object間のローカル距離
	int32_t waveFormationColumns;  // グリッド編隊の列数
	int32_t waveCompletionMode;  // 0=全生成で完了、1=全撃破または全返却で完了
	int32_t waveActionTargetGameObjectId;  // Wave通知を受け取るScript所有GameObject。未設定なら所有者
std::string waveStartedActionName;  // 条件成立時に通知する任意Script Action
	std::string waveSpawnedActionName;  // 1体生成時にGameObject Payload付きで通知する任意Script Action
	std::string waveCompletedActionName;  // 全生成時に生成数Payload付きで通知する任意Script Action
	std::string waveAllDefeatedActionName;  // 全撃破または全返却時に通知する任意Script Action
	float waveSpawnRailStartNormalized;  // 生成物が RailMovement を持つ時のレール開始進行率。-1 なら生成物自身の設定
	// 旧 RailShooterEnemy 保存互換値。新規機能から参照しない
	int32_t enemySpawnFollowerGameObjectId;  // 出現判定に使う Rail Movement 所有者
	float enemySpawnNormalized;  // 所有者のレール進行率がこの値へ達したら出現する
	int32_t enemyAttackTargetGameObjectId;  // 攻撃対象
	float enemyAttackInterval;  // 攻撃間隔の秒数
	float enemyAttackRange;  // 攻撃可能距離
	float enemyAttackDamage;  // 1 回の攻撃で減らす Health
	int32_t enemyProjectileTemplateGameObjectId;  // 実行時に複製する敵弾の見た目 Object
	int32_t enemyProjectilePoolSize;  // 敵ごとに事前生成する弾数
	float enemyProjectileSpeed;  // 敵弾が 1 秒間に進む距離
	float enemyProjectileHitRadius;  // 対象中心へ命中したとみなす半径
	float enemyProjectileLifetime;  // 命中しなかった敵弾を戻すまでの秒数
	int32_t enemyWaveIndex;  // 専用Timeline上でまとめるWave番号
	int32_t enemyFormationPattern;  // 0=横列、1=V字、2=円、3=グリッド
	int32_t enemyFormationSlot;  // Wave内での配置順
	float enemyFormationSpacing;  // 編隊を一括配置する時の間隔
	// 旧 RailShooterShip 保存互換値。新規機能から参照しない
	int32_t railShipSpeedSourceGameObjectId;  // 速度を読む Rail Movement 所有者。未設定なら船自身
	int32_t railShipSailGameObjectId;  // 帆 Animation を持つ Object
	int32_t railShipWakeEffectGameObjectId;  // 航跡 Effect を持つ Object
	int32_t railShipWindEffectGameObjectId;  // 風切り Effect を持つ Object
	float railShipEffectStartSpeed;  // Effect の再生を始める船速
	float railShipEffectFullSpeed;  // 演出強度を最大とみなす船速
	float railShipSailMinimumSpeed;  // 停止付近の帆 Animation 再生倍率
	float railShipSailMaximumSpeed;  // 最大船速時の帆 Animation 再生倍率
	float railAimMouseSensitivity;  // マウス差分から照準位置へ加える感度
	float railAimGamepadSensitivity;  // 右スティックで照準を動かす毎秒速度
	float railAimAssistRadius;  // 画面上でロック対象を選ぶ照準半径
	bool railAimInvertY;  // trueなら照準の上下を反転する
	// 旧 RailShooterEnemyMotion 保存互換値。新規機能から参照しない
	int32_t enemyMotionPattern;  // 0=上下揺動、1=旋回、2=8の字、3=追跡、4=突進離脱
	Vector3 enemyMotionAmplitude;  // パターンの X/Y/Z 振幅
	float enemyMotionFrequency;  // 1 秒当たりの周期
	float enemyMotionPhase;  // 個体ごとの開始位相
	int32_t enemyMotionTargetGameObjectId;  // 追跡・向き制御の対象
	float enemyMotionSpeed;  // 追跡・突進の移動速度
	bool enemyMotionLookAtTarget;  // 対象方向へ回転するなら true
	// 旧 RailShooterStage 保存互換値。新規機能から参照しない
	int32_t stageFollowerGameObjectId;  // レールを進む Player / Camera Rig
	int32_t stageStartMarkerGameObjectId;  // Play 開始時に配置する Start Marker
	int32_t stageGoalMarkerGameObjectId;  // 到達判定に使う Goal Marker
	int32_t stageStartEffectGameObjectId;  // 開始時に再生する Effect Object
	int32_t stageGoalEffectGameObjectId;  // ゴール時に再生する Effect Object
	float stageStartDelay;  // レール移動開始までの秒数
	float stageGoalRadius;  // Goal Marker への到達半径
	float stageGoalDelay;  // ゴール演出から Scene 遷移までの秒数
	std::string stageNextScenePath;  // ゴール後に開く次 Scene
	std::string stageSelectScenePath;  // Next 未設定時に戻る Stage Select Scene
	// Scene Button 設定
	std::string sceneButtonScenePath;  // クリック時に開く Scene
	// TimelineEvent 設定
	int32_t timelineSourceMode;  // 0=Play開始からの秒数、1=RailFollower進行率
	int32_t timelineSourceGameObjectId;  // RailFollower進行率を読む GameObject。未設定は -1
	float timelineTriggerValue;  // 秒数または 0～1 の進行率
	int32_t timelineTargetGameObjectId;  // Actionを受け取る C++ Script 所有 GameObject
	std::string timelineActionName;  // EditorNativeScript::BindAction で登録した任意名
	bool timelineTriggerOnce;  // trueなら Play 中に一度だけ通知する
	// 旧 RailShooterEvent 保存互換値。新規機能から参照しない
	int32_t railEventFollowerGameObjectId;  // 発火進行率を読む Rail Movement 所有者
	float railEventNormalized;  // Timeline上の発火進行率
	int32_t railEventType;  // 0=演出、1=BGM、2=会話、3=ボスPhase、4=汎用Trigger
	int32_t railEventTargetGameObjectId;  // Audio / Effect / Text / Boss の対象
	float railEventDuration;  // 会話表示やレール停止を維持する秒数
	std::string railEventText;  // 会話またはTimeline上の表示名
	bool railEventPauseRail;  // 発火中にFollowerのRail Movementを停止するならtrue
	// ThresholdState 設定
	int32_t thresholdSourceMode;  // 0=Health比率、1=RailFollower進行率
	int32_t thresholdSourceGameObjectId;  // 値を読む GameObject。未設定なら所有者
	int32_t thresholdTargetGameObjectId;  // State Actionを受け取るScript所有GameObject。未設定なら所有者
	float thresholdSecondValue;  // State 1から2へ切り替える境界
	float thresholdThirdValue;  // State 2から3へ切り替える境界
	std::string thresholdFirstActionName;  // State 1へ入った時の任意 Script Action
	std::string thresholdSecondActionName;  // State 2へ入った時の任意 Script Action
	std::string thresholdThirdActionName;  // State 3へ入った時の任意 Script Action
	// 旧 RailShooterBoss 保存互換値。新規機能から参照しない
	float bossPhaseTwoHealthRatio;  // Phase 2へ移る残りHP比率
	float bossPhaseThreeHealthRatio;  // Phase 3へ移る残りHP比率
	int32_t bossPhaseOneMotionPattern;  // Phase 1のEnemyMotionパターン
	int32_t bossPhaseTwoMotionPattern;  // Phase 2のEnemyMotionパターン
	int32_t bossPhaseThreeMotionPattern;  // Phase 3のEnemyMotionパターン
	float bossPhaseOneAttackInterval;  // Phase 1の攻撃間隔
	float bossPhaseTwoAttackInterval;  // Phase 2の攻撃間隔
	float bossPhaseThreeAttackInterval;  // Phase 3の攻撃間隔
	// UIValueBinding 設定
	int32_t uiBindingSourceGameObjectId;  // 値を読む GameObject。未設定なら所有者
	int32_t uiBindingValueType;  // 0=Health現在値、1=Health比率、2=Rail進行率、3=Active、4=GenericCounter
	std::string uiBindingPrefix;  // Textへ数値より前に付ける文字列
	int32_t uiBindingPrecision;  // Textへ表示する小数桁数
	float uiBindingScale;  // 読み取った値へ掛ける表示倍率
	// 旧 RailShooterHud 保存互換値。新規機能から参照しない
	int32_t railHudBindingType;  // 0=HP、1=進行率、2=敵数、3=リロード、4=照準、5=会話
	int32_t railHudSourceGameObjectId;  // HP / Rail / Weaponの参照元。-1は自動検出
	// Camera 共通設定
	int32_t cameraPriority;  // Game Viewで複数Cameraが有効な場合に大きい値を優先する
	int32_t cameraFollowPositionSpace;  // 0=World固定Offset、1=追従対象のLocal Offset
	int32_t cameraFollowRotationMode;  // 0=Camera角度固定、1=対象回転を継承、2=対象を見る
	// ScreenAim 設定
	int32_t screenAimInputGameObjectId;  // PlayerInputを読むGameObject。-1なら所有者
	int32_t screenAimReticleGameObjectId;  // RectTransformを動かす照準UI。-1ならUI連動なし
	int32_t screenAimInputMode;  // 0=Game View内のマウス絶対位置、1=Input ActionのVector2差分
	std::string screenAimActionMapName;  // Gamepad等の照準移動に使うAction Map名
	std::string screenAimActionName;  // Gamepad等の照準移動に使うVector2 Action名
	EditorScriptVector2 screenAimNormalizedPosition;  // 左上0,0から右下1,1の現在照準位置
	float screenAimSpeed;  // Input Action差分を正規化座標へ加える毎秒速度
	bool screenAimInvertY;  // trueならAction入力の上下を反転する
	bool screenAimClamp;  // trueなら照準位置を画面内へ制限する
	// HitscanWeapon 設定
	int32_t hitscanAimGameObjectId;  // ScreenAim所有GameObject。-1なら画面中央
	int32_t hitscanInputGameObjectId;  // Fire Actionを読むGameObject。-1なら所有者
	std::string hitscanActionMapName;  // Fire ActionのMap名
	std::string hitscanFireActionName;  // 発射に使うButton Action名
	float hitscanRange;  // Rayの最大距離
	float hitscanDamage;  // DamageReceiver / Healthへ送る基礎ダメージ
	std::string hitscanDamageTag;  // DamageTagModifierへ渡す文字列Tag
	float hitscanInterval;  // 次に発射できるまでの秒数
	bool hitscanAutomatic;  // trueなら押下中に間隔発射、falseなら押した瞬間だけ発射
	int32_t hitscanActionTargetGameObjectId;  // 発射・命中通知を受けるScript所有者。-1なら所有者
	std::string hitscanFiredActionName;  // 発射時に通知する任意Script Action
	std::string hitscanHitActionName;  // 命中時に通知する任意Script Action
	std::string hitscanMissActionName;  // 非命中時に通知する任意Script Action
	bool hitscanOceanCollision;  // Physics RayとFFT水面の近い命中を採用する
	// ProjectileEmitter 設定
	int32_t projectileAimGameObjectId;  // ScreenAim所有GameObject。-1なら画面中央
	int32_t projectileInputGameObjectId;  // Fire Actionを読むGameObject。-1なら所有者
	int32_t projectilePoolGameObjectId;  // 弾を取得するObjectPool所有GameObject
	int32_t projectileSpawnPointGameObjectId;  // 発射位置。-1なら所有者位置
	std::string projectileActionMapName;  // Fire ActionのMap名
	std::string projectileFireActionName;  // 発射に使うButton Action名
	float projectileSpeed;  // 弾の毎秒移動距離
	float projectileDamage;  // 命中時の基礎ダメージ
	std::string projectileDamageTag;  // DamageTagModifierへ渡す文字列Tag
	float projectileRadius;  // 連続SphereCastへ使う弾半径
	float projectileSpawnClearance;  // 発射位置から安全距離として足すオフセット(既定0.05、発射元の当たり判定を確実に抜けたい武器はここを大きくする)
	float projectileLifetime;  // 自動的にPoolへ戻すまでの秒数
	float projectileInterval;  // 次に発射できるまでの秒数
	bool projectileAutomatic;  // trueなら押下中に間隔発射、falseなら押した瞬間だけ発射
	int32_t projectileActionTargetGameObjectId;  // 発射・命中通知を受けるScript所有者。-1なら所有者
	std::string projectileFiredActionName;  // 発射時に通知する任意Script Action
	std::string projectileHitActionName;  // 命中時に通知する任意Script Action
	bool projectileOceanCollision;  // フレーム移動区間とFFT水面の交差を判定する
	int32_t projectileAimMode;  // 0=ScreenAim、1=Transform Forward、2=Target、3=BallisticPrediction、4=VariableSpeed(可変速度で直接着弾点を狙う)
	int32_t projectileBallisticPredictionGameObjectId;  // -1ならEmitter所有者
	bool projectileInheritSourceVelocity;
	int32_t projectileSourceVelocityGameObjectId;  // -1ならEmitter所有者
	bool projectileUseParentRigidBody;
	float projectileLinearVelocityInheritance;
	float projectileAngularVelocityInheritance;
	float projectileVariableSpeedMinimumFlightTime;  // AimMode=4(可変速度)TimeMode=0(距離依存)の最短飛行時間 秒
	float projectileVariableSpeedMaximumFlightTime;  // AimMode=4(可変速度)TimeMode=0(距離依存)の最長飛行時間 秒
	float projectileVariableSpeedDistanceFactor;  // TimeMode=0で flightTime=distance/この値 を計算する基準速度。武器の実速度(projectileSpeed)とは別物で、弾道の見え方だけを調整する
	int32_t projectileVariableSpeedTimeMode;  // 0=距離に応じて時間を決める、1=固定時間
	float projectileVariableSpeedFixedFlightTime;  // TimeMode=1で使う固定飛行時間 秒
	int32_t projectileVariableSpeedTrajectoryMode;  // 0=物理(初速+重力)、1=俯角固定の直線、2=物理無視の位置補間
	float projectileVariableSpeedDepressionAngleDegrees;  // TrajectoryMode=1で水平から下へ足す角度
	float projectileVariableSpeedArcHeight;  // TrajectoryMode=2で軌道頂点へ足す高さ
	bool projectileTracerStretchEnabled;  // trueならこの弾を曳光弾風に引き伸ばして表示する
	float projectileTracerLengthScale;  // 1フレームの移動距離に掛ける倍率(見た目の長さ調整)
	float projectileTracerMinimumLength;  // 最低限のScale Z(発射直後や低速時に点にならない下限)
	float projectileTracerThickness;  // Scale X/Yに使う太さ。小さいほど弾ではなく細い線に見える
	bool projectileHitscanResolution;  // trueなら発射時にHitscanWeaponで即ダメージ解決し、この弾は演出専用(ダメージ0)にする
	// DamageReceiver 設定
	float damageMultiplier;  // 受け取った基礎ダメージへ掛ける倍率
	float damageInvulnerabilitySeconds;  // 1回受けた後に次を無視する秒数
	bool damageDeactivateOnDeath;  // Healthが0以下になった時にGameObjectを無効化する
	int32_t damageActionTargetGameObjectId;  // ダメージ通知を受けるScript所有者。-1なら所有者
	std::string damagedActionName;  // ダメージ成立時に通知する任意Script Action
	std::string deathActionName;  // Healthが初めて0以下になった時に通知する任意Script Action
	// ObjectPool 設定
	int32_t objectPoolTemplateGameObjectId;  // 遅延生成元として使うTemplate GameObject
	int32_t objectPoolInitialSize;  // Templateを含め、初回利用時に必要な分だけ実体化する初期容量
	bool objectPoolAllowExpand;  // 空きがない場合に物理Templateを含めて容量を超える追加複製を許すか
	// PrefabSpawner 設定
	int32_t prefabSpawnerPoolGameObjectId;  // 生成元ObjectPool所有GameObject
	int32_t prefabSpawnerPointGameObjectId;  // 生成位置。-1なら所有者位置
	int32_t prefabSpawnerMode;  // 0=外部命令のみ、1=Play開始時、2=一定間隔
	float prefabSpawnerInterval;  // 一定間隔Modeの秒数
	int32_t prefabSpawnerActionTargetGameObjectId;  // 生成通知を受けるScript所有者。-1なら所有者
	std::string prefabSpawnerSpawnedActionName;  // 生成時に通知する任意Script Action
	// CameraBlend 設定
	int32_t cameraBlendSourceGameObjectId;  // 補間開始Camera。-1なら現在のGame View Camera
	int32_t cameraBlendTargetGameObjectId;  // 補間終了Camera
	float cameraBlendDuration;  // 補間時間秒
	int32_t cameraBlendEasing;  // 0=Linear、1=SmoothStep
	bool cameraBlendPlayOnStart;  // Play開始時に自動再生する
	// CameraShake 設定
	Vector3 cameraShakePositionAmplitude;  // World位置へ加える最大振幅
	Vector3 cameraShakeRotationAmplitude;  // Rotationへ加える最大振幅rad
	float cameraShakeFrequency;  // 振動位相のHz
	float cameraShakeDuration;  // 1回の継続秒数
	bool cameraShakePlayOnStart;  // Play開始時に自動再生する
	int32_t cameraShakePriority;  // Feedback Mixerの最高Priority合成で比較する値
	// RailBranch 設定
	int32_t railBranchFollowerGameObjectId;  // 切替対象RailFollower
	int32_t railBranchTargetPathGameObjectId;  // 切替先Rail Path
	int32_t railBranchTriggerMode;  // 0=進行率、1=外部命令のみ
	float railBranchTriggerNormalized;  // 自動切替を実行する進行率
	bool railBranchPreserveProgress;  // 切替前の正規化進行率を維持する
	bool railBranchTriggerOnce;  // Play中に1回だけ切り替える
	int32_t railBranchActionTargetGameObjectId;  // 切替通知を受けるScript所有者。-1なら所有者
	std::string railBranchActionName;  // 切替時に通知する任意Script Action
	// ActionSequence 設定
	bool actionSequencePlayOnStart;  // Play開始時に自動再生する
	bool actionSequenceLoop;  // 終端へ到達した時に先頭から再生する
	// ActionSequenceStep 設定
	int32_t actionSequenceStepType;  // 0=Action、1=待機、2=Active変更、3=Scene読込、4=条件分岐、5=Signal待機
	int32_t actionSequenceParallelGroup;  // 同じ0以上の番号が連続するStepは並列実行。負数は単独実行
	int32_t actionSequenceTargetGameObjectId;  // Action、Active変更、条件判定の対象。-1なら親Sequence所有者
	std::string actionSequenceActionName;  // Scriptへ通知する任意Action名またはSignal名
	float actionSequenceWaitSeconds;  // 待機時間秒
	bool actionSequenceActiveValue;  // Active変更で設定する値
	std::string actionSequenceScenePath;  // Scene読込Stepの対象
	bool actionSequenceSceneAdditive;  // trueなら現在Sceneへ追加読み込みする
	int32_t actionSequenceConditionMode;  // 0=Active、1=Health比率、2=Rail進行率
	int32_t actionSequenceCompareMode;  // 0=>=、1=<=、2=>、3=<
	float actionSequenceCompareValue;  // 条件判定の比較値
	int32_t actionSequenceTrueStepIndex;  // 条件成立時の子Step番号。負数なら次へ進む
	int32_t actionSequenceFalseStepIndex;  // 条件不成立時の子Step番号。負数なら次へ進む
	// Saveable 設定
	std::string saveableKey;  // Save Slot内で状態を識別する安定キー。空ならGameObject名
	bool saveableTransform;  // Transformを保存する
	bool saveableActive;  // GameObject Activeを保存する
	bool saveableHealth;  // Health現在値を保存する
	bool saveableRigidbody;  // Rigidbody速度・角速度を保存する
	bool saveableScriptProperties;  // C++ Script公開値を保存する
	// Checkpoint 設定
	std::string checkpointSlotName;  // 保存・復元に使うSlot名
	bool checkpointSaveOnStart;  // Play開始時にSlotを作成する
	bool checkpointLoadOnStart;  // Play開始時に既存Slotを復元する
	int32_t checkpointActionTargetGameObjectId;  // 保存・復元通知を受けるScript所有者。-1なら所有者
	std::string checkpointSavedActionName;  // 保存成功時の任意Script Action
	std::string checkpointLoadedActionName;  // 復元成功時の任意Script Action
	// WeaponLoadout / WeaponLoadoutSlot 設定
	int32_t weaponLoadoutSelectedSlotIndex;  // 現在装備中の子Slot番号
	int32_t weaponLoadoutActionTargetGameObjectId;  // 装備・Reload通知先。-1なら所有者
	std::string weaponLoadoutChangedActionName;  // 装備変更時の任意Script Action
	std::string weaponLoadoutReloadedActionName;  // Reload完了時の任意Script Action
	std::string weaponSlotName;  // InspectorとHUDへ表示するSlot名
	int32_t weaponSlotWeaponGameObjectId;  // HitscanWeaponまたはProjectileEmitter所有者
	int32_t weaponSlotVisualGameObjectId;  // 選択中だけ有効にする任意Visual
	int32_t weaponSlotCurrentAmmo;  // Magazine内の現在弾数
	int32_t weaponSlotReserveAmmo;  // 予備弾数。負数は無限
	int32_t weaponSlotMaximumAmmo;  // Magazine最大弾数
	float weaponSlotReloadSeconds;  // Reload完了までの秒数
	bool weaponSlotAutoReload;  // 空になった時に自動Reloadする
	// TargetSelector / TargetSteering 設定
	int32_t targetSelectorSearchLayer;  // -1=全Layer、0以上=Rigidbody physicsLayer
	float targetSelectorMaximumDistance;  // 検索距離
	float targetSelectorMaximumAngle;  // 前方からの最大角度Degree
	int32_t targetSelectorReferenceGameObjectId;  // 検索原点と前方。-1なら所有者
	bool targetSelectorOcclusionCheck;  // Physics Raycastで遮蔽を除外する
	int32_t targetSelectorOcclusionMode;  // 0=None、1=Physics、2=Ocean、3=Physics+Ocean
	float targetSelectorOceanClearance;  // 水面からこの距離以内の視線も遮蔽扱いにする
	int32_t targetSelectorMaximumTargets;  // 候補として評価する最大数
	int32_t targetSelectorSelectionMode;  // 0=最短距離、1=前方中心、2=低HP、3=優先値
	int32_t targetSelectorCurrentTargetGameObjectId;  // Runtimeが選択した現在Target
	int32_t targetSelectorActionTargetGameObjectId;  // Target変更通知先。-1なら所有者
	std::string targetSelectorFoundActionName;  // Target取得時のAction
	std::string targetSelectorLostActionName;  // Target喪失時のAction
	std::string targetSelectorChangedActionName;  // Target変更時のAction
	int32_t targetSteeringTargetGameObjectId;  // 明示Target。-1ならSelectorを参照する
	int32_t targetSteeringSelectorGameObjectId;  // TargetSelector所有者。-1なら所有者
	float targetSteeringTurnSpeed;  // 最大旋回速度Degree/秒
	float targetSteeringAcceleration;  // 前進加速度
	float targetSteeringMaximumSpeed;  // 最大速度
	float targetSteeringStartDelay;  // Play開始後の待機秒数
	float targetSteeringPredictionSeconds;  // Target速度の先読み秒数
	int32_t targetSteeringMode;  // 0=Transform、1=Rigidbody Force
	// MovementModifier 設定
	Vector3 movementModifierLocalPositionOffset;  // 基準姿勢へ加えるローカル位置
	Vector3 movementModifierLocalRotationOffset;  // 基準姿勢へ加えるEuler回転Degree
	int32_t movementModifierAxisMask;  // 位置X/Y/Zをbit 0/1/2で許可する
	EditorScriptVector2 movementModifierInputRange;  // Inputによる横・縦最大Offset
	float movementModifierInputSpeed;  // Offset追従速度/秒
	int32_t movementModifierInputGameObjectId;  // PlayerInput所有者。-1なら所有者
	std::string movementModifierActionMapName;  // Vector2 Action Map
	std::string movementModifierActionName;  // Vector2 Action
	// PropertyTween 設定
	int32_t propertyTweenTargetGameObjectId;  // Property所有者。-1なら所有者
	std::string propertyTweenComponentName;  // EditorComponentType名
	std::string propertyTweenPropertyName;  // Runtime Property登録名
	Vector3 propertyTweenStartValue;  // Floatはxだけを使う
	Vector3 propertyTweenEndValue;  // Floatはxだけを使う
	int32_t propertyTweenValueType;  // 0=Float、1=Vector3
	float propertyTweenDuration;  // 補間秒数
	int32_t propertyTweenCurve;  // 0=Linear、1=SmoothStep、2=EaseIn、3=EaseOut
	bool propertyTweenPlayOnStart;  // Play開始時に自動再生する
	bool propertyTweenLoop;  // 終端から再生を繰り返す
	int32_t propertyTweenActionTargetGameObjectId;  // 完了通知先。-1なら所有者
	std::string propertyTweenCompletedActionName;  // 完了時の任意Action
	// ActionRelay / ActionRelayTarget 設定
	bool actionRelayOnStart;  // Play開始時に一度Relayする
	int32_t actionRelayTargetGameObjectId;  // Relay先Script所有者。-1なら親Relay所有者
	std::string actionRelayActionName;  // Relay先へ通知する任意Action
	bool actionRelayTargetEnabled;  // falseなら対象から一時除外する
	// TargetPoint / Team / TargetSelector Team Filter 設定
	float targetPointPriority;  // Priority選択時に大きい値を優先する
	float targetPointRadius;  // Scene表示と照準補助に使う注視半径
	Vector3 targetPointAimOffset;  // TargetPoint Transformからのローカル注視位置Offset
	int32_t teamId;  // 負数はNeutral、0以上はゲーム側が定義するTeam
	bool teamTargetable;  // falseならTargetSelectorの自動候補から除外する
	int32_t targetSelectorTeamFilter;  // 0=Any、1=Different、2=Same、3=Specific
	int32_t targetSelectorSpecificTeamId;  // Specific Filterで許可するTeam ID
	bool targetSelectorIncludeNeutral;  // Team IDが負数またはTeam未設定の候補を含める
	// Timer 設定
	float timerDuration;  // 発火までの秒数
	bool timerRepeat;  // 発火後に同じDurationで繰り返す
	bool timerPlayOnStart;  // Play開始時から進める
	int32_t timerActionTargetGameObjectId;  // Action通知先。-1なら所有者
	std::string timerActionName;  // 発火時Action
	float timerRemaining;  // Play中の残り秒数
	bool timerPaused;  // Play中の一時停止状態
	// GenericStateMachine 設定
	std::string stateMachineInitialState;  // Play開始時State
	std::string stateMachineCurrentState;  // Play中の現在State
	int32_t stateMachineActionTargetGameObjectId;  // State変更通知先
	std::string stateMachineChangedActionName;  // State変更Action
	// Attribute 設定
	std::string attributeName;  // ゲーム側が定義するResource名
	float attributeMinimum;  // 最小値
	float attributeMaximum;  // 最大値
	float attributeCurrent;  // 現在値
	float attributeRegenerationPerSecond;  // 1秒当たりの増減値
	int32_t attributeActionTargetGameObjectId;  // 値変更通知先
	std::string attributeChangedActionName;  // 値変更Action
	// DestructiblePart 設定
	int32_t destructibleHealthGameObjectId;  // Health Source。-1なら所有者
	std::string destructibleDisableComponentNames;  // 無効化する内部Component名。セミコロン区切り
	bool destructibleDisableChildren;  // 破壊時に直下の子を無効化する
	int32_t destructibleActionTargetGameObjectId;  // 破壊通知先
	std::string destructibleDestroyedActionName;  // 破壊Action
	bool destructibleDestroyed;  // Play中の破壊済み状態
	// FormationFollower 設定
	int32_t formationLeaderGameObjectId;  // Leader
	Vector3 formationLocalOffset;  // Leader基準の位置Offset
	float formationPositionSpeed;  // 位置追従速度。0以下は即時
	float formationRotationSpeed;  // 回転追従速度 degree/s。0以下は即時
	bool formationFollowRotation;  // Leader回転へ追従する
	// TargetLock 設定
	int32_t targetLockSelectorGameObjectId;  // TargetSelector所有者。-1なら所有者
	float targetLockSeconds;  // Lock完了までの秒数
	float targetLockLostGraceSeconds;  // Target喪失を許容する秒数
	float targetLockProgress;  // Play中の0～1進行率
	bool targetLockLocked;  // Play中のLock完了状態
	int32_t targetLockCurrentGameObjectId;  // 現在追跡中Target
	int32_t targetLockActionTargetGameObjectId;  // Action通知先
	std::string targetLockStartedActionName;  // Lock開始Action
	std::string targetLockCompletedActionName;  // Lock完了Action
	std::string targetLockLostActionName;  // Lock解除Action
	// MultiTargetLock 設定
	int32_t multiTargetLockSelectorGameObjectId;
	int32_t multiTargetLockMaximumCount;
	float multiTargetLockSecondsPerTarget;
	float multiTargetLockLostGraceSeconds;
	bool multiTargetLockAutoAcquire;
	int32_t multiTargetLockActionTargetGameObjectId;
	std::string multiTargetLockAddedActionName;
	std::string multiTargetLockCompletedActionName;
	std::string multiTargetLockLostActionName;
	std::vector<int32_t> multiTargetLockTargetGameObjectIds;
	std::vector<float> multiTargetLockProgressValues;
	std::vector<bool> multiTargetLockCompletedValues;
	// WorldTargetMarker / OffScreenIndicator 設定
	int32_t targetMarkerTargetGameObjectId;
	int32_t targetMarkerSelectorGameObjectId;
	int32_t targetMarkerLockGameObjectId;
	int32_t targetMarkerMultiLockIndex;
	Vector3 targetMarkerWorldOffset;
	EditorScriptVector2 targetMarkerScreenOffset;
	float targetMarkerEdgePadding;
	bool targetMarkerHideBehindCamera;
	bool targetMarkerOnlyWhenLocked;
	bool targetMarkerRotateToDirection;
	// AttributeSet 設定
	std::vector<EditorNamedAttributeEntry> attributeSetEntries;
	int32_t attributeSetActionTargetGameObjectId;
	std::string attributeSetChangedActionName;
	// GenericCounter 設定
	std::string counterName;
	float counterInitialValue;
	float counterCurrentValue;
	float counterMinimumValue;
	float counterMaximumValue;
	float counterThresholdValue;
	int32_t counterCompareMode;
	bool counterFireOnce;
	bool counterWasSatisfied;
	int32_t counterActionTargetGameObjectId;
	std::string counterChangedActionName;
	std::string counterThresholdActionName;
	// GenericCondition 設定
	int32_t conditionSourceGameObjectId;
	int32_t conditionSourceType;
	std::string conditionComponentName;
	std::string conditionPropertyName;
	int32_t conditionCompareMode;
	float conditionCompareFloat;
	std::string conditionCompareString;
	bool conditionEvaluateEveryFrame;
	bool conditionFireOnChangeOnly;
	bool conditionLastResult;
	int32_t conditionActionTargetGameObjectId;
	std::string conditionTrueActionName;
	std::string conditionFalseActionName;
	// GameplayData 設定
	std::string gameplayDataAssetPath;
	std::vector<EditorGameplayDataEntry> gameplayDataEntries;
	// AreaDamage 設定
	float areaDamageRadius;
	float areaDamageBaseDamage;
	float areaDamageMinimumMultiplier;
	float areaDamageImpulse;
	int32_t areaDamageFalloffMode;  // 0=一定、1=Linear、2=SmoothStep
	int32_t areaDamageLayerMask;  // Physics Layer bit。-1なら全Layer
	std::string areaDamageTag;
	bool areaDamageIgnoreOwner;
	bool areaDamagePlayOnStart;
	int32_t areaDamageActionTargetGameObjectId;
	std::string areaDamageAppliedActionName;
	int32_t areaDamageOcclusionMode;  // 0=None、1=Physics、2=Physics + Ocean
	int32_t areaDamageOcclusionLayerMask;
	float areaDamageBlockedMultiplier;
	int32_t areaDamageOcclusionSamplePoints;
	int32_t areaDamageTeamRule;  // 0=Any、1=Different Team、2=Same Team
	bool areaDamageIgnoreNeutral;
	int32_t areaDamageTeamSourceGameObjectId;  // -1ならInstigator
	// HitZone 設定
	int32_t hitZoneHealthGameObjectId;
	float hitZoneDamageMultiplier;
	// DamageTagModifier 設定
	float damageTagDefaultMultiplier;
	std::vector<EditorDamageTagModifierEntry> damageTagModifierEntries;
	// ProjectileDetonator 設定
	bool projectileDetonateOnContact;
	bool projectileDetonateOnProximity;
	bool projectileDetonateOnLifetime;
	int32_t projectileDetonatorTargetGameObjectId;
	float projectileDetonatorProximityRadius;
	int32_t projectileDetonatorAreaDamageGameObjectId;
	int32_t projectileDetonatorActionTargetGameObjectId;
	std::string projectileDetonatedActionName;
	// ThreatTracker 設定
	int32_t threatTrackerTargetGameObjectId;
	float threatTrackerMaximumDistance;
	float threatTrackerMinimumClosingSpeed;
	float threatTrackerMaximumMissDistance;
	int32_t threatTrackerMaximumCount;
	int32_t threatTrackerActionTargetGameObjectId;
	std::string threatTrackerAddedActionName;
	std::string threatTrackerLostActionName;
	std::vector<EditorThreatRuntimeEntry> threatTrackerEntries;
	// RuntimeStateReset 設定
	bool runtimeResetHealth;
	bool runtimeResetStateMachine;
	bool runtimeResetAttributes;
	bool runtimeResetLocks;
	bool runtimeResetTimers;
	bool runtimeResetDestructibleParts;
	bool runtimeResetCooldowns;
	int32_t runtimeResetActionTargetGameObjectId;
	std::string runtimeResetActionName;
	// CooldownSet 設定
	std::vector<EditorCooldownEntry> cooldownSetEntries;
	int32_t cooldownSetActionTargetGameObjectId;
	std::string cooldownSetCompletedActionName;
	// WeaponFirePattern 設定
	int32_t weaponFirePatternMode;  // 0=Single、1=Burst、2=Salvo、3=Spread、4=Sequence、5=Charge
	int32_t weaponFirePatternCount;
	float weaponFirePatternInterval;
	float weaponFirePatternSpreadAngle;
	float weaponFirePatternChargeSeconds;
	std::vector<int32_t> weaponFirePatternSpawnPointGameObjectIds;
	int32_t weaponFirePatternActionTargetGameObjectId;
	std::string weaponFirePatternCompletedActionName;
	// TargetAssignment 設定
	int32_t targetAssignmentMultiTargetLockGameObjectId;
	int32_t targetAssignmentMaximumTargets;
	float targetAssignmentInterval;
	bool targetAssignmentLockedOnly;
	int32_t targetAssignmentActionTargetGameObjectId;
	std::string targetAssignmentCompletedActionName;
	// WeaponAccuracy 設定
	float weaponAccuracyBaseSpread;
	float weaponAccuracyMaximumSpread;
	float weaponAccuracySpreadPerShot;
	float weaponAccuracyRecoveryPerSecond;
	float weaponAccuracyMovementSpread;
	int32_t weaponAccuracyDistribution;  // 0=Uniform Cone、1=Uniform Disk、2=Center Weighted
	float weaponAccuracyCurrentSpread;
	// WeaponRecoil 設定
	Vector3 weaponRecoilBodyImpulse;
	Vector3 weaponRecoilBodyTorque;
	int32_t weaponRecoilVisualGameObjectId;
	Vector3 weaponRecoilVisualPosition;
	Vector3 weaponRecoilVisualRotation;
	float weaponRecoilRecoveryPerSecond;
	int32_t weaponRecoilCameraShakeGameObjectId;
	int32_t weaponRecoilActionTargetGameObjectId;
	std::string weaponRecoilActionName;
	// ImpactResponder / SurfaceType 設定
	std::vector<EditorImpactResponseEntry> impactResponseEntries;
	std::string surfaceTypeTag;
	// TimeScale 設定
	float timeScaleValue;
	float timeScaleDuration;
	float timeScaleBlendSeconds;
	bool timeScalePlayOnStart;
	int32_t timeScaleActionTargetGameObjectId;
	std::string timeScaleCompletedActionName;
	// AimAssist / InterceptPrediction 設定
	int32_t aimAssistScreenAimGameObjectId;
	int32_t aimAssistTargetSelectorGameObjectId;
	float aimAssistRadius;
	float aimAssistStrength;
	float aimAssistFollowSpeed;
	float aimAssistInputSuppression;
	int32_t interceptTargetGameObjectId;
	int32_t interceptTargetSelectorGameObjectId;
	float interceptProjectileSpeed;
	float interceptMaximumTime;
	Vector3 interceptPredictedPosition;
	float interceptTime;
	bool interceptValid;
	// DamageDirectionIndicator 設定とRuntime値
	float damageDirectionDuration;
	float damageDirectionFadeSeconds;
	float damageDirectionMinimumDamage;
	float damageDirectionEdgeRadius;
	int32_t damageDirectionSourceGameObjectId;
	EditorScriptVector2 damageDirectionNormalized;
	float damageDirectionRemaining;
	// ObjectiveTracker 設定
	std::vector<EditorObjectiveEntry> objectiveEntries;
	int32_t objectiveActionTargetGameObjectId;
	std::string objectiveChangedActionName;
	// EncounterController 設定
	std::vector<EditorEncounterWaveEntry> encounterWaveEntries;
	bool encounterPlayOnStart;
	int32_t encounterActionTargetGameObjectId;
	std::string encounterCompletedActionName;
	// SpawnPointSet 設定
	int32_t spawnPointSetMode;  // 0=Sequence、1=Random、2=Weighted、3=Volume
	std::vector<EditorSpawnPointEntry> spawnPointSetEntries;
	Vector3 spawnPointVolumeSize;
	bool spawnPointAvoidImmediateRepeat;
	// DifficultyParameterSet 設定
	std::vector<std::string> difficultyNames;
	std::vector<EditorDifficultyOverrideEntry> difficultyOverrides;
	int32_t difficultySelectedIndex;
	bool difficultyApplyOnStart;
	int32_t difficultyActionTargetGameObjectId;
	std::string difficultyAppliedActionName;
	// CameraFeedbackMixer 設定
	Vector3 cameraFeedbackMaximumPosition;
	Vector3 cameraFeedbackMaximumRotation;
	int32_t cameraFeedbackMaximumConcurrent;
	int32_t cameraFeedbackMixMode;  // 0=Add、1=Highest Priority
	float cameraFeedbackGlobalStrength;
	// BallisticPrediction 設定とRuntime出力
	int32_t ballisticTargetGameObjectId;
	int32_t ballisticTargetSelectorGameObjectId;
	float ballisticInitialSpeed;
	Vector3 ballisticGravity;
	float ballisticDrag;
	Vector3 ballisticTargetAcceleration;
	float ballisticMaximumTime;
	float ballisticSimulationStep;
	int32_t ballisticMaximumPoints;
	bool ballisticValid;
	Vector3 ballisticLaunchDirection;
	Vector3 ballisticLaunchVelocity;
	Vector3 ballisticSourceVelocity;
	Vector3 ballisticImpactPosition;
	float ballisticFlightTime;
	std::vector<Vector3> ballisticTrajectoryPoints;
	bool ballisticInheritSourceVelocity;
	int32_t ballisticSourceVelocityGameObjectId;  // -1なら所有者
	bool ballisticUseParentRigidBody;
	float ballisticLinearVelocityInheritance;
	float ballisticAngularVelocityInheritance;
	// DamageEventBuffer 設定とRuntime Entry
	int32_t damageEventMaximumEntries;
	float damageEventLifetime;
	float damageEventMinimumDamage;
	bool damageEventMergeSameSource;
	std::vector<EditorDamageEventRuntimeEntry> damageEventEntries;
	// GamePause 設定とRuntime状態
	bool gamePausePaused;
	bool gamePausePauseGameTime;
	bool gamePausePausePhysics;
	bool gamePausePauseAudio;
	std::string gamePauseGameplayInputMap;
	std::string gamePauseUiInputMap;
	int32_t gamePauseActionTargetGameObjectId;
	std::string gamePausePausedActionName;
	std::string gamePauseResumedActionName;
	// SurfaceWakeEmitter 設定とRuntime値
	int32_t surfaceWakeOceanGameObjectId;
	int32_t surfaceWakeLeftEffectGameObjectId;
	int32_t surfaceWakeRightEffectGameObjectId;
	int32_t surfaceWakeBowEffectGameObjectId;
	float surfaceWakeMinimumSpeed;
	float surfaceWakeMaximumSpeed;
	float surfaceWakeWidth;
	float surfaceWakeLifetime;
	float surfaceWakeMaximumEmissionRate;
	float surfaceWakeCurrentSpeed;
	float surfaceWakeCurrentIntensity;
	// TrajectoryRenderer 設定
	int32_t trajectoryPredictionGameObjectId;
	Vector3 trajectoryColor;
	float trajectoryAlpha;
	float trajectoryThickness;
	int32_t trajectoryMaximumPoints;
	bool trajectoryShowInSceneView;
	bool trajectoryShowInGameView;
	bool trajectoryShowImpactPoint;
	// WaterSurfaceState 設定とRuntime出力
	int32_t waterSurfaceOceanGameObjectId;  // -1なら位置を覆うOceanを自動検索する
	Vector3 waterSurfaceLocalOffset;  // 判定に使う所有者ローカル位置
	float waterSurfaceClearance;  // 水面からの状態判定Offset
	int32_t waterSurfaceState;  // 0=Above、1=Entering、2=Underwater、3=Leaving
	float waterSurfaceSignedDistance;  // 判定点から水面までの符号付き距離
	int32_t waterSurfaceCurrentOceanGameObjectId;
	Vector3 waterSurfacePosition;
	Vector3 waterSurfaceNormal;
	Vector3 waterSurfaceVelocity;
	float waterSurfaceFoam;
	int32_t waterSurfaceActionTargetGameObjectId;
	std::string waterSurfaceEnteredActionName;
	std::string waterSurfaceExitedActionName;
	// OceanProbeSet 設定とRuntime出力
	int32_t oceanProbeOceanGameObjectId;  // -1なら各Probe位置を覆うOceanを自動検索する
	Vector3 oceanProbeLocalOriginOffset;  // Probe列の基準点
	Vector3 oceanProbeLocalDirection;  // 所有者ローカル空間の走査方向
	std::vector<EditorOceanProbeEntry> oceanProbeEntries;
	// AttackCollisionFilter 設定
	int32_t attackFilterInstigatorGameObjectId;  // -1ならWeapon/Emitter所有者
	bool attackFilterIgnoreInstigator;
	bool attackFilterIgnoreInstigatorHierarchy;
	int32_t attackFilterTeamRule;  // 0=Any、1=Different Team、2=Same Team
	bool attackFilterIgnoreNeutral;
	float attackFilterArmingDistance;
	std::vector<int32_t> attackFilterIgnoredGameObjectIds;
	// TurretAim 設定とRuntime出力
	int32_t turretTargetGameObjectId;  // 明示Target。-1ならTargetSelectorを使う
	int32_t turretTargetSelectorGameObjectId;  // -1なら所有者
	int32_t turretYawPivotGameObjectId;
	int32_t turretPitchPivotGameObjectId;
	float turretYawMinimumDegrees;
	float turretYawMaximumDegrees;
	float turretPitchMinimumDegrees;
	float turretPitchMaximumDegrees;
	float turretYawSpeedDegrees;
	float turretPitchSpeedDegrees;
	float turretAimToleranceDegrees;
	float turretPredictionSeconds;
	int32_t turretCurrentTargetGameObjectId;
	bool turretCanReachTarget;
	bool turretIsAimed;
	float turretYawErrorDegrees;
	float turretPitchErrorDegrees;
	// WeaponGroup 設定とRuntime出力
	std::vector<EditorWeaponGroupEntry> weaponGroupEntries;
	int32_t weaponGroupMode;  // 0=Simultaneous、1=Sequential、2=RoundRobin
	float weaponGroupInterval;
	bool weaponGroupRequireAllReady;
	int32_t weaponGroupActionTargetGameObjectId;
	std::string weaponGroupCompletedActionName;
	int32_t weaponGroupRoundRobinIndex;
	bool weaponGroupIsFiring;
	// ProjectileImpactPhysics 設定
	float projectileImpactPenetrationEnergy;
	float projectileImpactPenetrationLoss;
	int32_t projectileImpactMaximumPenetrations;
	float projectileImpactRicochetAngleDegrees;
	float projectileImpactEnergyRetention;
	float projectileImpactDamageRetention;
	int32_t projectileImpactMaximumRicochets;
	std::vector<EditorProjectileSurfaceModifierEntry> projectileImpactSurfaceModifiers;
	// CameraHorizonStabilizer 設定
	int32_t horizonSourceGameObjectId;
	Vector3 horizonLocalPositionOffset;
	Vector3 horizonRotationOffsetDegrees;
	bool horizonFollowPosition;
	float horizonPitchInheritance;
	float horizonYawInheritance;
	float horizonRollInheritance;
	Vector3 horizonWorldUp;
	float horizonDamping;
	float horizonMaximumRollDegrees;
	// FireLineCheck 設定とRuntime出力
	int32_t fireLineMuzzleGameObjectId;  // -1なら所有者
	int32_t fireLineDirectionGameObjectId;  // -1ならMuzzle
	int32_t fireLineAllowedTargetGameObjectId;
	float fireLineDistance;
	float fireLineRadius;
	int32_t fireLineLayerMask;
	std::vector<int32_t> fireLineIgnoredGameObjectIds;
	bool fireLineClear;
	int32_t fireLineBlockingGameObjectId;
	float fireLineBlockingDistance;
	// StatusEffectSet 設定とRuntime Entry
	std::vector<EditorStatusEffectDefinitionEntry> statusEffectDefinitions;
	int32_t statusEffectActionTargetGameObjectId;
	std::vector<EditorStatusEffectRuntimeEntry> statusEffectRuntimeEntries;
	// RailSpeedProfile 設定
	std::vector<EditorRailSpeedKey> railSpeedKeys;
	bool railSpeedProfileEnabled;
	// RailZone 設定とRuntime値
	std::vector<EditorRailZoneEntry> railZoneEntries;
	int32_t railZoneActionTargetGameObjectId;
	int32_t railZoneActiveIndex;
	// CameraFollowComposer 設定とRuntime値
	int32_t cameraComposerTargetGameObjectId;  // -1ならCameraの接続先
	Vector3 cameraComposerFollowOffset;
	Vector3 cameraComposerLookAtOffset;
	float cameraComposerPositionDamping;
	float cameraComposerRotationDamping;
	float cameraComposerLookAheadSeconds;
	EditorScriptVector2 cameraComposerDeadZone;
	float cameraComposerMaximumDistance;
	bool cameraComposerInheritTargetYaw;
	bool cameraComposerStabilizePitchRoll;
	Vector3 cameraComposerRuntimePosition;
	Vector3 cameraComposerRuntimeRotation;
	bool cameraComposerRuntimeInitialized;
	// SpeedFeedback 設定とRuntime値
	int32_t speedFeedbackSourceGameObjectId;  // -1なら所有者
	int32_t speedFeedbackCameraGameObjectId;  // -1なら最高Priority Camera
	float speedFeedbackMinimumSpeed;
	float speedFeedbackMaximumSpeed;
	float speedFeedbackMinimumFovDegrees;
	float speedFeedbackMaximumFovDegrees;
	float speedFeedbackMinimumMotionBlur;
	float speedFeedbackMaximumMotionBlur;
	float speedFeedbackCameraStrength;
	float speedFeedbackResponseSpeed;
	float speedFeedbackNormalized;
	// SpawnedObjectSetup 設定
	int32_t spawnedSetupRailPathGameObjectId;
	float spawnedSetupRailStartNormalized;
	float spawnedSetupRailStartStep;
	float spawnedSetupRailSpeedMultiplier;
	bool spawnedSetupOverrideTeam;
	int32_t spawnedSetupTeamId;
	bool spawnedSetupResetRuntimeState;
	int32_t spawnedSetupActionTargetGameObjectId;
	std::string spawnedSetupAppliedActionName;
	// WaveMotionProfile 設定
	int32_t waveMotionMode;  // 0=None、1=Sine、2=FigureEight、3=Alternating
	EditorScriptVector2 waveMotionAmplitude;
	float waveMotionFrequency;
	float waveMotionPhaseStep;
	float waveMotionBlendInSeconds;
	// DistanceActivation 設定とRuntime値
	int32_t distanceActivationReferenceGameObjectId;
	float distanceActivationEnterDistance;
	float distanceActivationExitDistance;
	bool distanceActivationAffectHierarchy;
	bool distanceActivationRuntimeActive;
	bool distanceActivationRuntimeInitialized;
	// SimulationLOD 設定とRuntime値
	int32_t simulationLodReferenceGameObjectId;
	float simulationLodMediumDistance;
	float simulationLodFarDistance;
	float simulationLodCulledDistance;
	float simulationLodMediumScriptInterval;
	float simulationLodFarScriptInterval;
	bool simulationLodDisablePhysicsAtFar;
	bool simulationLodDisableScriptsAtFar;
	bool simulationLodDisableAiAtFar;
	bool simulationLodDisableAnimationAtFar;
	bool simulationLodDisableEffectsAtFar;
	bool simulationLodAffectHierarchy;
	int32_t simulationLodRuntimeLevel;
	// RailEventMarker 設定とRuntime値
	std::vector<EditorRailEventMarkerEntry> railEventMarkerEntries;
	int32_t railEventMarkerActionTargetGameObjectId;
	float railEventMarkerPreviousProgress;
	bool railEventMarkerRuntimeInitialized;
	// SceneStreaming 設定とRuntime値
	std::string sceneStreamingScenePath;
	int32_t sceneStreamingReferenceGameObjectId;
	float sceneStreamingLoadDistance;
	float sceneStreamingUnloadDistance;
	bool sceneStreamingUnloadWhenFar;
	bool sceneStreamingRuntimeLoaded;
	bool sceneStreamingRuntimePending;
};

struct EditorGameObject {
	int32_t id;  // Scene 内で一意な ID
	int32_t parentId;  // 親 GameObject の ID。親なしは -1
	bool isActive;  // false なら更新や物理の対象外
	std::string name;  // Hierarchy に表示する名前
	Vector3 translate;  // 親がある場合は親空間、ルートではワールド空間の位置
	Vector3 rotate;  // 親がある場合は親空間の回転値。各軸ラジアン
	Vector3 scale;  // 親がある場合は親空間の拡縮値
	std::vector<int32_t> children;  // 子 GameObject の ID 配列
	std::vector<EditorComponent> components;  // 付与されている Component 配列
	std::string prefabSourcePath;  // Prefab Instanceの生成元Asset。通常Objectは空
	int32_t prefabSourceObjectId = -1;  // Prefab Asset内で対応する元Object ID
	std::string prefabVariantBasePath;  // Variant Assetが継承する基底Prefab。通常Prefabは空
};

constexpr int32_t kEditorPhysicsLayerCount = 8;  // Default / Player / Enemy / Ground / Projectile / Trigger / UI / Ignore Raycast

struct EditorPhysicsSettings {
	Vector3 gravity;  // Scene 全体の重力。Y を負にすると下方向へ落ちる
	float fixedTimeStep;  // 物理の固定更新間隔。Unity の Fixed Timestep 相当
	int32_t collisionStepCount;  // Jolt Update 内で衝突解決を何分割するか
	bool drawColliderDebug;  // SceneView に Collider の補助表示を出すか
	bool drawContactDebug;  // 接触点や法線の補助表示を出すか
	bool drawCastDebug;  // Raycast / ShapeCast の補助表示を出すか
	bool drawVelocityDebug;  // Rigidbody の速度と角速度を矢印表示するか
	bool drawForceDirectionDebug;  // 重力、風、力、場の向きを矢印表示するか
	bool drawFieldVolumeDebug;  // 風、重力、流体、電磁場などの影響範囲を表示するか
	bool drawConnectionDebug;  // SpringForce と Joint の接続先を線で表示するか
	bool drawSelectedOnlyDebug;  // 選択中 GameObject の物理情報だけを表示するか
	float debugVectorScale;  // 速度や力の矢印へ掛ける SceneView 表示倍率
	bool layerCollisionMatrix[kEditorPhysicsLayerCount][kEditorPhysicsLayerCount];  // レイヤー同士が当たるかを示す行列
};

struct EditorPrefab {
	EditorGameObject gameObject;  // Prefab として保存する GameObject
	std::string sourcePath;  // Prefab ファイルの元パス
};

class EditorScene {
public:
	EditorScene();

	void InitializeDefaultScene();  // Environment / Camera / Point Light を持つ初期 Scene を作る
	int32_t CreateGameObject(const std::string& name);  // Transform だけを持つ GameObject を作成する
	int32_t DuplicateGameObject(int32_t gameObjectId);  // 既存 GameObject をコピーして新しい ID を付ける
	bool DeleteGameObject(int32_t gameObjectId);  // 指定 ID の GameObject と子を削除する
	bool RenameGameObject(int32_t gameObjectId, const std::string& name);  // 指定 ID の GameObject 名を変更する
	bool SetParent(int32_t childId, int32_t parentId, bool preserveWorldTransform = false);  // childId の親を設定する。trueなら見た目のワールド姿勢を維持する
	bool AddComponent(int32_t gameObjectId, EditorComponentType type);  // 指定 Component を GameObject に追加する
	bool RemoveComponent(int32_t gameObjectId, EditorComponentType type);  // 指定 Component を GameObject から削除する
	bool HasComponent(int32_t gameObjectId, EditorComponentType type) const;  // 指定 Component を GameObject が持っているか調べる
	bool SaveScene(const std::string& filePath) const;  // Scene をテキスト形式で保存する
	bool LoadScene(const std::string& filePath);  // テキスト形式の Scene を読み込む
	bool SavePrefab(int32_t gameObjectId, const std::string& filePath) const;  // GameObject 1 つを Prefab として保存する
	bool SavePrefabVariant(int32_t gameObjectId, const std::string& basePrefabPath, const std::string& filePath) const;  // 現在値を基底Prefab参照付きVariantとして保存する
	int32_t InstantiatePrefab(const std::string& filePath);  // Prefab階層へ新しいIDを割り当ててSceneへ追加する
	bool ApplyPrefabInstance(int32_t gameObjectId);  // Instanceの現在階層を生成元Prefabへ反映する
	int32_t RevertPrefabInstance(int32_t gameObjectId);  // Instanceを生成元Prefabの内容へ戻して新しいRoot IDを返す
	bool MergeScene(const EditorScene& sourceScene, std::vector<int32_t>& addedGameObjectIds);  // Additive Scene用にIDと内部参照を再割当して追加する
	void PushUndo();  // 現在の Scene 状態を Undo スタックへ積む
	bool Undo();  // 1 つ前の Scene 状態へ戻す
	bool Redo();  // Undo した Scene 状態をやり直す

	EditorGameObject* FindGameObject(int32_t gameObjectId);
	const EditorGameObject* FindGameObject(int32_t gameObjectId) const;
	Matrix4x4 GetWorldMatrix(int32_t gameObjectId) const;  // 親のSRTをルートまで合成したワールド行列を返す
	bool GetWorldTransform(
		int32_t gameObjectId,
		Vector3& worldScale,
		Vector3& worldRotation,
		Vector3& worldPosition) const;  // ワールド行列をInspectorと物理で扱えるSRTへ分解する
	bool GetWorldTransformAndMatrix(
		int32_t gameObjectId,
		Vector3& worldScale,
		Vector3& worldRotation,
		Vector3& worldPosition,
		Matrix4x4& worldMatrix) const;  // 階層行列を1回だけ計算し、SRTと行列を同時に返す
	bool SetWorldTransform(
		int32_t gameObjectId,
		const Vector3& worldScale,
		const Vector3& worldRotation,
		const Vector3& worldPosition);  // ワールドSRTを現在の親空間へ戻して保存する
	bool SetWorldMatrix(int32_t gameObjectId, const Matrix4x4& worldMatrix);  // ギズモや物理のワールド行列を現在の親空間へ戻して保存する
	EditorPhysicsSettings& GetPhysicsSettings();  // Scene 全体の物理設定を編集用に返す
	const EditorPhysicsSettings& GetPhysicsSettings() const;  // Scene 全体の物理設定を読み取り専用で返す
	std::vector<EditorGameObject>& GetGameObjects();
	const std::vector<EditorGameObject>& GetGameObjects() const;

private:
	int32_t nextGameObjectId_;
	EditorPhysicsSettings physicsSettings_;
	std::vector<EditorGameObject> gameObjects_;
	mutable std::unordered_map<int32_t, int32_t> gameObjectIndexById_;  // ID検索を全件線形走査せず O(1) で行う索引。
	std::vector<std::vector<EditorGameObject>> undoStack_;
	std::vector<std::vector<EditorGameObject>> redoStack_;

	EditorComponent CreateComponent(EditorComponentType type) const;
	int32_t FindGameObjectIndex(int32_t gameObjectId) const;
	void RebuildGameObjectIndex() const;  // Sceneの件数やIDが変わった時だけ索引を再構築する。
	void RemoveFromParent(int32_t childId);
	void RebuildChildren();
	void DeleteGameObjectRecursive(int32_t gameObjectId);
	void RefreshNextGameObjectId();
};

std::string ToString(EditorComponentType type);
EditorComponentType ComponentTypeFromIndex(int32_t componentIndex);

#pragma warning(pop)
