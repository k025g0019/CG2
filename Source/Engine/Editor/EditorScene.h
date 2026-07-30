#pragma once

#include "EditorScriptApi.h"
#include "Vector.h"

#include <array>
#include <cstdint>
#include <string>
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
	// 子 GameObject 群を開始条件と間隔に従って順次有効化する
	WaveSpawner,
	// 時間または RailFollower 進行率から任意の C++ Script Action を通知する
	TimelineEvent,
	// Health または RailFollower の値を閾値で状態へ変換し、任意 Action を通知する
	ThresholdState,
	// Health / RailFollower / Active 値を同じ GameObject の Text / Slider へ反映する
	UIValueBinding,
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
	float specularTint;  // 鏡面色へベースカラーを混ぜる割合
	float sheen;  // 布の縁に出る反射の強度
	float sheenTint;  // Sheen 色へベースカラーを混ぜる割合
	int32_t alphaMode;  // 0=不透明、1=マスク、2=半透明
	bool doubleSided;  // true なら両面描画する
	EditorScriptVector2 uvTiling;  // Material Texture の UV 繰り返し数
	EditorScriptVector2 uvOffset;  // Material Texture の UV 開始位置
	float mass;  // RigidBody の質量
	float drag;  // RigidBody の速度減衰
	bool useGravity;  // RigidBody に重力を使うか
	bool isKinematic;  // true なら物理で動かさない
	bool isTrigger;  // true なら接触だけ検出して押し戻ししない
	float bounciness;  // 床衝突時の跳ね返り係数
	Vector3 velocity;  // RigidBody の現在速度
	Vector3 angularVelocity;  // RigidBody の現在角速度。回転方向と速さをラジアン毎秒で持つ
	float angularDrag;  // RigidBody の角速度減衰。回転をどれだけ止めやすくするか
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
	int32_t particleMotionType;  // 0=直線、1=軌道、2=渦、3=波/航跡、4=吸引、5=雲、6=爆発/水しぶき、7=弾道。
	Vector3 particleMotionCenter;  // 軌道・渦・吸引運動の中心を Emitter からの相対位置で指定する
	float particleAngularSpeed;  // 軌道・渦運動の角速度（度/秒）
	float particleRadialAcceleration;  // 中心から外向きへ加える加速度。負なら中心へ寄る
	float particleWaveAmplitude;  // 波運動で上下へ揺らす加速度の大きさ
	float particleWaveFrequency;  // 波運動と雲のうねりが 1 秒間に変化する回数
	float particleAttractorStrength;  // 吸引運動で中心へ加える加速度
	std::string particleRenderAssetPath;  // Particle 1 個の描画形状に使う FBX / OBJ。空なら板ポリゴン
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
	// Environment 設定
	Vector3 skyLowerColor;  // 地平線 / 下側の空色
	float environmentTextureRotation;  // 環境テクスチャの水平回転（ラジアン）
	float environmentTextureMipBias;  // 反射時のMIPバイアス
	bool environmentTextureEnabled;  // 環境テクスチャを使うか
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
	bool buoyancyUseCenterPoint;  // 旧 Scene の読み書き互換用。自動セル配置では使用しない
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
	// Health 設定
	float healthMaximum;  // Play 開始時に設定する最大体力
	float healthCurrent;  // Play 中の現在体力
	// WaveSpawner 設定
	int32_t waveTriggerMode;  // 0=Play開始、1=RailFollower進行率
	int32_t waveTriggerSourceGameObjectId;  // RailFollower進行率を読む GameObject。未設定は -1
	float waveTriggerValue;  // RailFollower全長に対する開始進行率
	float waveSpawnInterval;  // 子 GameObject を順次有効化する間隔秒
	bool waveDeactivateChildrenOnStart;  // trueなら Play 開始時に子を非表示へ移す
	int32_t waveActionTargetGameObjectId;  // Wave通知を受け取るScript所有GameObject。未設定なら所有者
	std::string waveStartedActionName;  // 条件成立時に通知する任意Script Action
	std::string waveSpawnedActionName;  // 子を1つ有効化した時に通知する任意Script Action
	std::string waveCompletedActionName;  // 全ての子を有効化した時に通知する任意Script Action
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
	int32_t uiBindingValueType;  // 0=Health現在値、1=Health比率、2=Rail進行率、3=Active
	std::string uiBindingPrefix;  // Textへ数値より前に付ける文字列
	int32_t uiBindingPrecision;  // Textへ表示する小数桁数
	float uiBindingScale;  // 読み取った値へ掛ける表示倍率
	// 旧 RailShooterHud 保存互換値。新規機能から参照しない
	int32_t railHudBindingType;  // 0=HP、1=進行率、2=敵数、3=リロード、4=照準、5=会話
	int32_t railHudSourceGameObjectId;  // HP / Rail / Weaponの参照元。-1は自動検出
	};

struct EditorGameObject {
	int32_t id;  // Scene 内で一意な ID
	int32_t parentId;  // 親 GameObject の ID。親なしは -1
	bool isActive;  // false なら更新や物理の対象外
	std::string name;  // Hierarchy に表示する名前
	Vector3 translate;  // ワールド座標
	Vector3 rotate;  // 回転値。各軸ラジアン
	Vector3 scale;  // 拡縮値
	std::vector<int32_t> children;  // 子 GameObject の ID 配列
	std::vector<EditorComponent> components;  // 付与されている Component 配列
};

constexpr int32_t kEditorPhysicsLayerCount = 8;  // Default / Player / Enemy / Ground / Projectile / Trigger / UI / Ignore Raycast

struct EditorPhysicsSettings {
	Vector3 gravity;  // Scene 全体の重力。Y を負にすると下方向へ落ちる
	float fixedTimeStep;  // 物理の固定更新間隔。Unity の Fixed Timestep 相当
	int32_t collisionStepCount;  // Jolt Update 内で衝突解決を何分割するか
	bool drawColliderDebug;  // SceneView に Collider の補助表示を出すか
	bool drawContactDebug;  // 接触点や法線の補助表示を出すか
	bool drawCastDebug;  // Raycast / ShapeCast の補助表示を出すか
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
	bool SetParent(int32_t childId, int32_t parentId);  // childId の親を parentId に設定する
	bool AddComponent(int32_t gameObjectId, EditorComponentType type);  // 指定 Component を GameObject に追加する
	bool RemoveComponent(int32_t gameObjectId, EditorComponentType type);  // 指定 Component を GameObject から削除する
	bool HasComponent(int32_t gameObjectId, EditorComponentType type) const;  // 指定 Component を GameObject が持っているか調べる
	bool SaveScene(const std::string& filePath) const;  // Scene をテキスト形式で保存する
	bool LoadScene(const std::string& filePath);  // テキスト形式の Scene を読み込む
	bool SavePrefab(int32_t gameObjectId, const std::string& filePath) const;  // GameObject 1 つを Prefab として保存する
	int32_t InstantiatePrefab(const std::string& filePath);  // Prefab ファイルから GameObject を作る
	void PushUndo();  // 現在の Scene 状態を Undo スタックへ積む
	bool Undo();  // 1 つ前の Scene 状態へ戻す
	bool Redo();  // Undo した Scene 状態をやり直す

	EditorGameObject* FindGameObject(int32_t gameObjectId);
	const EditorGameObject* FindGameObject(int32_t gameObjectId) const;
	EditorPhysicsSettings& GetPhysicsSettings();  // Scene 全体の物理設定を編集用に返す
	const EditorPhysicsSettings& GetPhysicsSettings() const;  // Scene 全体の物理設定を読み取り専用で返す
	std::vector<EditorGameObject>& GetGameObjects();
	const std::vector<EditorGameObject>& GetGameObjects() const;

private:
	int32_t nextGameObjectId_;
	EditorPhysicsSettings physicsSettings_;
	std::vector<EditorGameObject> gameObjects_;
	std::vector<std::vector<EditorGameObject>> undoStack_;
	std::vector<std::vector<EditorGameObject>> redoStack_;

	EditorComponent CreateComponent(EditorComponentType type) const;
	int32_t FindGameObjectIndex(int32_t gameObjectId) const;
	void RemoveFromParent(int32_t childId);
	void RebuildChildren();
	void DeleteGameObjectRecursive(int32_t gameObjectId);
	void RefreshNextGameObjectId();
};

std::string ToString(EditorComponentType type);
EditorComponentType ComponentTypeFromIndex(int32_t componentIndex);

#pragma warning(pop)
