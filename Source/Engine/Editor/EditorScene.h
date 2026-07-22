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
	int32_t particleMotionType;  // 0=直線、1=軌道、2=渦、3=波、4=吸引、5=雲、6=爆発
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

	void InitializeDefaultScene();  // 空 Scene と ID 採番を初期状態へ戻す
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
