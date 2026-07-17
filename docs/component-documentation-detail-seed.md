# CG2 コンポーネント詳細下書き

このファイルは、ChatGPT Work が使用者向けサイトを作るための Component 詳細素材である。
`docs/user-documentation-research-spec.md` は調査仕様、このファイルはページ本文の下書きとして使う。

## 共通で必ず書くこと

各 Component ページには、次を必ず入れる。

| 項目 | 書く内容 |
| --- | --- |
| 目的 | その Component が何をするか。 |
| 使う場面 | 使用者がどんな時に追加するか。 |
| 追加場所 | Add Component のカテゴリと表示名。 |
| 必要条件 | 一緒に必要な Component、Asset、Project Settings。 |
| Inspector 項目 | 表示名、内部フィールド、初期値、単位、範囲。 |
| Play 時の動作 | 実行中に何が変わるか。 |
| C++ Script | 関連する Runtime API。 |
| 保存 | Scene / Prefab に保存される値。 |
| 制限 | 未実装、設定のみ、既知の問題。 |
| 確認手順 | 最小 Scene でどう動作確認するか。 |

## 基本

### Transform

- 目的: GameObject の位置、回転、スケールを持つ。
- 使う場面: すべての 3D / 2D Object の配置。
- 主な設定: 位置、回転、スケール。
- Play 時: Script、物理、Navigation、Animation から変更される。
- C++ Script: `GetTransform`、`SetTransform`。
- 注意: Rigidbody と併用する場合、Transform 直接変更が物理結果を上書きする可能性を説明する。

### RectTransform

- 目的: UI の位置、サイズ、Anchor、Pivot を扱う。
- 使う場面: Canvas 配下の UI 配置。
- 必要条件: Canvas。
- 主な設定: Anchor、Pivot、Width、Height、Position。
- Play 時: Layout Group や Canvas Scaler により再配置される。
- 注意: Transform との違いを図で説明する。

### Canvas

- 目的: UI を描画する土台。
- 使う場面: Button、Text、Image、Slider などを表示する時。
- 必要条件: EventSystem、CanvasRenderer、UI Graphic。
- 主な設定: Screen Space / World Space、Scale、Sort Order。
- Play 時: Game View に UI を描画する。
- 注意: Scene View と Game View の違いを説明する。

### Script / ゲームオブジェクト + スクリプト

- 目的: C++ DLL Script を GameObject に追加する。
- 使う場面: 使用者が C++ で独自処理を書く時。
- 必要条件: C++ ファイル、Build、DLL Path、API Version 一致。
- 主な設定: Class 名、DLL Path、Build ボタン、Script 有効状態。
- Play 時: `Load`、`Start`、`Update`、`FixedUpdate`、`OnPhysicsEvent`、`Stop` が呼ばれる。
- C++ Script: `EditorScriptRuntimeApi` 全体。
- 注意: Debug / Release、x64、古い DLL のロック、依存 DLL をトラブルシューティングに入れる。

### MonoBehaviour

- 目的: Unity 風の自作 Component 表現。
- 使う場面: C++ Script を Component として扱う時。
- 必要条件: Script Runtime との接続。
- 主な設定: Script 参照、公開変数、Enabled。
- 注意: `Script` と `MonoBehaviour` の違いを必ず説明する。

## 描画・レンダリング

### MeshFilter

- 目的: 描画に使う Mesh Asset を参照する。
- 使う場面: FBX / OBJ / Primitive Mesh を表示する時。
- 必要条件: MeshRenderer。
- 主な設定: Mesh Path、Asset Path、Import Scale。
- 注意: MeshFilter だけでは表示されない。

### MeshRenderer / ModelRenderer

- 目的: MeshFilter の Mesh を描画する。
- 使う場面: 3D モデルを表示する時。
- 必要条件: MeshFilter、Material。
- 主な設定: Base Color、Texture、Normal、Metallic、Roughness、AO、Emission、Alpha、Reflection、Double Sided、UV Tiling、UV Offset。
- Play 時: Light、Shadow、Reflection、PostProcess の影響を受ける。
- C++ Script: `GetMaterialState`。
- 注意: UV確認画像は描画用 Base Color Texture ではない。Texture Slot と UV確認画像を分けて説明する。

### SkinnedMeshRenderer

- 目的: Bone と Skin Weight を持つ Mesh を描画する。
- 使う場面: キャラクター、腕、服などの変形モデル。
- 必要条件: Skinned Mesh、Bone、Animation / Animator。
- 主な設定: Mesh、Material、Root Bone、Bounds。
- 注意: GPU Skinning、Bone Import、FBX Animation 対応範囲を確認する。

### SpriteRenderer

- 目的: 2D 画像を Scene に表示する。
- 使う場面: 2D Game、Billboard 的な画像、UI 以外の Sprite。
- 必要条件: Texture Asset。
- 主な設定: Sprite Path、Color、Sorting、Flip。
- 注意: UI Image との違いを説明する。

### LineRenderer

- 目的: 線分や折れ線を描画する。
- 使う場面: Debug Line、Laser、Path 表示。
- 主な設定: Width、Color、Point Count、World Space。
- 注意: Effect カテゴリにも出るため、詳細ページは 1 つに統合する。

### TrailRenderer

- 目的: 移動した軌跡を描画する。
- 使う場面: 弾、剣、車、Particle の軌跡。
- 主な設定: Lifetime、Width、Color、Min Vertex Distance。
- 注意: LineRenderer との違いを説明する。

### BillboardRenderer

- 目的: Camera 方向を向く板を描画する。
- 使う場面: 木、草、遠景、アイコン。
- 主な設定: Texture、Size、Color。
- 注意: SpriteRenderer との違いを説明する。

### CanvasRenderer

- 目的: Canvas 上の UI Graphic を描画する。
- 使う場面: Image、Text、Button の内部描画。
- 必要条件: Canvas、UI Graphic。
- 注意: 使用者向けには UI の描画部品として説明する。

### ParticleSystemRenderer

- 目的: ParticleSystem の粒を描画する。
- 使う場面: 炎、煙、爆発、魔法。
- 必要条件: ParticleSystem。
- 主な設定: Material、Render Mode、Sort、Trail。
- 注意: ParticleSystem 本体と Renderer を分けて説明する。

## カメラ

### Camera

- 目的: Game View に映す視点を定義する。
- 使う場面: Main Camera、Scene Camera、RenderTexture、Mirror Camera。
- 主な設定: Projection、FOV、Near、Far、Clear Color、Exposure、Target Texture、Culling Mask。
- 注意: Scene View Camera と Game View Camera の違いを説明する。

### AudioListener

- 目的: 音を聞く位置を決める。
- 使う場面: Main Camera に付ける。
- 必要条件: AudioSource。
- 主な設定: Enabled。
- 注意: 複数 Listener の扱いを確認する。

### FlareLayer

- 目的: Lens Flare を Camera に重ねる。
- 使う場面: 太陽、ライトの眩しさ表現。
- 必要条件: Camera、LensFlare。
- 注意: PostProcess Bloom / Glare との違いを説明する。

### CinemachineCamera

- 目的: 追従、注視、揺れなどの Camera 制御を行う。
- 使う場面: Player 追従、演出 Camera。
- 主な設定: Follow、LookAt、Offset、Damping。
- 注意: 実装が表示だけなら明記する。

## ライト・環境

### Light

- 目的: Scene 内の物体を照らす。
- 使う場面: Sun、Point、Spot、Area Light。
- 主な設定: Type、Color、Intensity、Range、Spot Angle、Area Radius、Shadow。
- Play 時: PBR Shader と Shadow に影響する。
- 注意: 複数 Light、Gizmo、影対応、環境光なし時の挙動を必ず確認する。

### ReflectionProbe

- 目的: 周囲の反射情報を提供する。
- 使う場面: 金属、鏡面、屋内外の反射。
- 主な設定: Mode、Size、Center、Intensity、Resolution、Box Projection。
- 注意: Bake / Realtime / Probe Blend / Cubemap Capture の対応状況を明記する。

### LightProbeGroup

- 目的: 間接光のサンプル点を配置する。
- 使う場面: 動的 Object の間接光補間。
- 主な設定: Probe Position、Group。
- 注意: 実処理未接続なら設定のみと書く。

### LightProbeProxyVolume

- 目的: 大きな動的 Object の Light Probe 補間を改善する。
- 使う場面: 大型キャラクター、車、建物。
- 主な設定: Bounds、Resolution。
- 注意: Unity 風の表示だけなら制限に書く。

### Volume

- 目的: PostProcess や環境設定を領域単位で適用する。
- 使う場面: エリアごとに色、Fog、Bloom、Exposure を変える。
- 主な設定: Priority、Weight、Profile、Blend Distance。
- 注意: Volume Blend が未実装なら明記する。

### PostProcess

- 目的: 描画後の画面効果を設定する。
- 使う場面: Bloom、AA、ToneMapping、SSR、Glare、Filter。
- 主な設定: Bloom、AA Mode、SMAA、Temporal、Exposure、Vignette、Grain、Chromatic Aberration。
- 注意: 複数効果は追加式の折りたたみ項目として説明する。

### Environment

- 目的: 空、環境光、HDRI、IBL、背景色を設定する。
- 使う場面: Scene 全体の明るさ、Skybox、反射、Ambient。
- 主な設定: Sky Color、Ambient Intensity、HDRI Path、IBL Intensity、Rotation。
- 注意: Light なしでも光る場合、Environment か Shader 固定光か確認する。

## 3D物理

### Rigidbody

- 目的: GameObject を物理演算で動かす。
- 使う場面: 落下、衝突、押し出し、Force、Torque。
- 必要条件: Collider、Jolt Physics。
- 主な設定: Mass、Use Gravity、Velocity、Angular Velocity、Drag、Angular Drag、Freeze、Interpolation、Collision Detection。
- C++ Script: `GetVelocity`、`SetVelocity`、`AddForce`、`AddImpulse`、`AddTorque`。
- 注意: Transform 直接変更との競合を必ず説明する。

### BoxCollider

- 目的: 箱形状の当たり判定を作る。
- 使う場面: 床、壁、箱、単純な障害物。
- 主な設定: Center、Size、Is Trigger、Physics Material、Layer。
- 注意: 見た目 Mesh と Collider Size が一致するか確認する。

### SphereCollider

- 目的: 球形状の当たり判定を作る。
- 使う場面: ボール、弾、範囲判定。
- 主な設定: Center、Radius、Is Trigger、Physics Material。
- 注意: 見た目が球でも MeshCollider ではない場合、穴や凹凸は反映されない。

### CapsuleCollider

- 目的: カプセル形状の当たり判定を作る。
- 使う場面: Player、Enemy、人型キャラクター。
- 主な設定: Center、Radius、Height、Direction、Is Trigger。
- 注意: CharacterController との違いを説明する。

### MeshCollider

- 目的: Mesh 形状に沿った当たり判定を作る。
- 使う場面: 地形、複雑な静的モデル、穴のある形状。
- 必要条件: MeshFilter、Collision Mesh。
- 主な設定: Mesh、Convex、Center、Scale、Layer。
- 注意: 動的 MeshCollider、BVH、Convex Hull、軽量化の対応状態を明記する。

### TerrainCollider

- 目的: Terrain の当たり判定を作る。
- 使う場面: 地面、山、広いフィールド。
- 必要条件: Terrain。
- 注意: 3D物理と地形カテゴリの両方に出る。

### WheelCollider

- 目的: 車輪向けの物理判定を作る。
- 使う場面: 車、バイク、タイヤ。
- 必要条件: Rigidbody。
- 主な設定: Radius、Suspension、Friction、Motor Torque、Brake。
- 注意: 実装が簡易なら WheelJoint との違いも含める。

### CharacterController

- 目的: 物理だけに任せずキャラクター移動を制御する。
- 使う場面: Player、Enemy、人型移動。
- 主な設定: Radius、Height、Slope Limit、Step Offset、Skin Width。
- 注意: Rigidbody との違いを説明する。

### ConstantForce

- 目的: 常に一定方向へ力を加える。
- 使う場面: 風、重力以外の加速、磁力。
- 必要条件: Rigidbody。
- 注意: 表示のみか実処理ありか確認する。

### Joint 系

- 対象: HingeJoint、FixedJoint、SpringJoint、ConfigurableJoint、CharacterJoint。
- 目的: Rigidbody 同士を制約でつなぐ。
- 使う場面: 扉、鎖、ばね、固定接続、ラグドール。
- 主な設定: Connected Body、Axis、Limit、Spring、Damping。
- 注意: Joint 種類ごとの対応範囲を個別に分けて書く。

## 2D物理

### 2D Collider / Rigidbody

- 対象: RigidBody2D、BoxCollider2D、CircleCollider2D、CapsuleCollider2D、PolygonCollider2D、EdgeCollider2D、CompositeCollider2D、TilemapCollider2D、CustomCollider2D。
- 目的: 2D ゲーム用の物理と当たり判定。
- 使う場面: 横スクロール、トップビュー2D、Tilemap。
- 注意: 3D Jolt Physics と同じ実装か、表示だけかを必ず確認する。

### 2D Joint / Effector

- 対象: DistanceJoint2D、HingeJoint2D、SpringJoint2D、FixedJoint2D、SliderJoint2D、WheelJoint2D、PlatformEffector2D、SurfaceEffector2D、AreaEffector2D、PointEffector2D、BuoyancyEffector2D。
- 目的: 2D 物理の制約や特殊効果。
- 使う場面: 片方向床、水、風、吸引、2D ギミック。
- 注意: 未実装なら「追加と保存のみ」と明記する。

## アニメーション

### Animator

- 目的: Animation Clip の状態遷移を制御する。
- 使う場面: Idle、Walk、Run、Attack の切り替え。
- 主な設定: Controller、Parameters、State、Transition。
- C++ Script: `GetAnimationState`。
- 注意: Controller / BlendTree / RootMotion の対応範囲を確認する。

### Animation

- 目的: Clip を直接再生する。
- 使う場面: 単純な回転、往復、開閉、キャラの単発動作。
- 主な設定: Clip、Speed、Loop、Play On Awake、Amplitude。
- C++ Script: `GetAnimationState`。
- 注意: FBX Animation Import の対応範囲を明記する。

### AvatarMask / PlayableDirector

- 目的: AvatarMask は体の一部だけ Animation を適用し、PlayableDirector は Timeline 的な再生を行う。
- 使う場面: 上半身だけ攻撃、Timeline 演出。
- 注意: 設定のみなら明記する。

### Constraint 系

- 対象: AimConstraint、LookAtConstraint、ParentConstraint、PositionConstraint、RotationConstraint、ScaleConstraint。
- 目的: Transform を他の Object に追従、注視、制約する。
- 主な設定: Target、Weight、Offset、Axis、Freeze。
- 注意: Transform 直接変更との優先順位を確認する。

## オーディオ

### AudioSource

- 目的: 音声 Asset を再生する。
- 使う場面: BGM、SE、環境音。
- 必要条件: WAV などの Audio Asset、AudioListener。
- 主な設定: Clip、Volume、Pitch、Loop、Play On Awake、Spatial Blend、Min / Max Distance。
- 注意: 対応形式、同時再生、3D減衰を確認する。

### Audio Filter / Reverb Zone

- 対象: AudioLowPassFilter、AudioHighPassFilter、AudioEchoFilter、AudioDistortionFilter、AudioReverbFilter、AudioChorusFilter、AudioReverbZone。
- 目的: 音質や空間効果を変える。
- 使う場面: 水中、洞窟、無線、残響、特殊演出。
- 注意: Inspector 表示だけか、本当に XAudio2 へ反映されるか確認する。

## UI

### UI Graphic

- 対象: Image、RawImage、Text、TextMeshProUGUI。
- 目的: UI と文字を表示する。
- 必要条件: Canvas、CanvasRenderer。
- 主な設定: Texture、Text、Font、Color、Raycast Target。
- 注意: Text と TextMeshPro の違いを説明する。

### UI Interaction

- 対象: Button、Toggle、Slider、Scrollbar、Dropdown、TMPDropdown、InputField、TMPInputField、ScrollRect。
- 目的: 使用者入力を受ける UI。
- 必要条件: Canvas、EventSystem、Input Module。
- 主な設定: OnClick、Value、Min、Max、Options、Text、Navigation。
- C++ Script: `EditorScript_InvokeAction` または UI Event 関数名。
- 注意: 実際に C++ 関数が呼ばれるか必ず確認する。

### UI Layout

- 対象: Mask、RectMask2D、HorizontalLayoutGroup、VerticalLayoutGroup、GridLayoutGroup、ContentSizeFitter、AspectRatioFitter、LayoutElement。
- 目的: UI の配置と表示範囲を制御する。
- 必要条件: Canvas、RectTransform。
- 主な設定: Padding、Spacing、Constraint、Preferred Size、Mask。
- 注意: 編集時と Play 時で再計算タイミングが違うか確認する。

## 入力・イベント

### EventSystem / Input Module

- 対象: EventSystem、StandaloneInputModule、InputSystemUIInputModule、TouchInputModule。
- 目的: UI と入力イベントを接続する。
- 必要条件: Canvas、UI Component、Input。
- 注意: 旧入力と新 Input Action の違いを説明する。

### PlayerInput / PlayerInputManager / Input

- 目的: Action Map、Binding、Player ごとの入力を扱う。
- 必要条件: Input Action Asset、Project Settings、C++ Script。
- 主な設定: Actions、Default Map、Behavior、Move / Jump / Fire Event。
- C++ Script: `GetActionVector2`、`IsActionPressed`、`WasActionJustPressed`、`EditorScript_InvokeAction`。
- 注意: キー直書き版と Action 版を分けて説明する。

## ゲームプレイ

### LocalMove

- 目的: 指定方向へ単純移動させる。
- 使う場面: テスト移動、移動床、単純な敵。
- 注意: Rigidbody がある場合の競合を説明する。

### RollingMove

- 目的: 球や車輪を Torque / 馬力で動かす。
- 使う場面: ボール、タイヤ、物理移動。
- 必要条件: Rigidbody、SphereCollider または WheelCollider。
- C++ Script: `AddTorque`。
- 注意: 回転固定、摩擦不足、接地判定を説明する。

## ナビゲーション

### Navigation Components

- 対象: NavigationAgent、NavMeshObstacle、NavMeshSurface、NavMeshModifier、NavMeshModifierVolume、NavMeshLink。
- 目的: 経路探索、障害物、NavMesh 生成、Area 設定、リンク移動。
- 必要条件: Recast Navigation、NavMeshSurface、Agent。
- 主な設定: Radius、Height、Speed、Acceleration、Stopping Distance、Area、Carve、Bidirectional。
- 注意: 目的地 ID、座標指定、NavMesh Build 手順を分けて説明する。

## AI

### Behavior Tree

- 対象: AIBehaviorTree、AIBehaviorBlackboard、AIBehaviorSelector、AIBehaviorSequence、AIBehaviorTask、AIBehaviorDecorator。
- 目的: AI の行動を木構造で制御する。
- 必要条件: BehaviorTree.CPP、Blackboard、Task 定義。
- 注意: ノード編集 UI と実行 Runtime の対応範囲を確認する。

### State Machine / GOAP / HTN

- 対象: AIStateMachine、AIState、AIStateTransition、AIGoapPlanner、AIGoapGoal、AIGoapAction、AIGoapWorldState、AIHtnPlanner、AIHtnDomain、AIHtnTask、AIHtnMethod。
- 目的: 状態遷移、目標計画、タスク分解で AI を制御する。
- 注意: 実装状態を Component ごとに分けて書く。

### Pathfinding / Steering / Sensors

- 対象: AIPathfindingAgent、AIMicroPatherGrid、AIRecastNavMeshBuilder、AIRecastCrowdAgent、AIPathRequest、AIDynamicObstacle、AISteeringAgent、AISeekSteering、AIFleeSteering、AIArriveSteering、AIPursuitSteering、AIWanderSteering、AIObstacleAvoidanceSteering、AIFlockSteering、AIVisionSensor、AIOpenCvCamera、AIOpenCvObjectDetector、AIOpenCvColorTracker、AIMotionSensor、AIWhisperSpeechRecognizer、AIVoiceCommand。
- 目的: 経路探索、操舵、知覚、画像認識、音声認識。
- 必要条件: Navigation、OpenCV / Python / ONNX / Whisper など該当外部ライブラリ。
- C++ Script: `GetAiSensorState`。
- 注意: Sensor ごとに戻り値の意味を分ける。同じ構造体でも同じ意味として扱わない。

## エフェクト

### ParticleSystem / VisualEffect

- 目的: 粒子や GPU Effect を表示する。
- 使う場面: 爆発、煙、魔法、水しぶき、光。
- 必要条件: ParticleSystemRenderer、Material。
- 主な設定: Emission、Shape、Lifetime、Speed、Size、Color、Noise、Collision、Renderer。
- 注意: VisualEffect が設定のみなら明記する。

### LensFlare / Projector / DecalProjector

- 目的: 光のにじみ、投影、表面デカールを描画する。
- 使う場面: 太陽、ライト、魔法陣、汚れ、弾痕。
- 注意: PostProcess Glare / Bloom との違いを説明する。

## 地形・タイルマップ

### Terrain / Tilemap / Grid

- 対象: Terrain、TerrainCollider、Tilemap、TilemapRenderer、TilemapCollider2D、Grid。
- 目的: 地形、タイル、グリッドベースのマップを作る。
- 使う場面: フィールド、2D Map、Tile Stage。
- 注意: Brush 編集、HeightMap、Tile Palette の対応範囲を明記する。

## FeelKit

### HapticSource

- 目的: FeelKitHaptics の振動を再生する。
- 使う場面: 衝突、攻撃、UI 決定、ダメージ。
- 必要条件: FeelKitHaptics Library、対応 Device、Effect Asset。
- 主な設定: Strength、Duration、Loop、Effect Path。
- 注意: 接続 Device がない時の挙動を説明する。
