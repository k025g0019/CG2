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
- Advanced Material: Clear Coat、Clear Coat Roughness、Transmission、Subsurface、Anisotropy、Anisotropy Rotation、Specular Tint、Sheen、Sheen Tintも個別に説明する。
- Alpha Mode: Opaque、Mask、Transparentを分ける。Transparentは通常半透明OIT、Transmissionが高い材質は屈折専用Pathとの関係も確認する。

### SkinnedMeshRenderer

- 目的: Bone と Skin Weight を持つ Mesh を描画する。
- 使う場面: キャラクター、腕、服などの変形モデル。
- 必要条件: Skinned Mesh、Bone、Animation / Animator。
- 主な設定: Mesh、Material、Root Bone、Bounds。
- 現在のデータ: FBX Clusterから頂点ごとに最大4本のBone Index / WeightをImportし、Weightを正規化する。
- 現在の描画: 現在Frameと前FrameのBone Matrix BufferをSkinned描画、GBuffer、Shadow、Motion Vectorへ渡す。
- 注意: GPU Skinningの実装と、Animation WindowでBone Poseを直接編集する機能は別である。後者、Humanoid Retarget、Avatar、IKは未実装として分ける。

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

### Ocean

- 目的: 外部モデルを用意せず、GPU FFT を使う広域海面を描画する。
- 追加場所: `コンポーネントを追加 > 描画・レンダリング > Ocean`。または `ゲームオブジェクト > 3D Object > Ocean`。
- 必要条件: Game View を確認する場合は有効な Camera が必要。反射と陰影を確認する場合は Environment と Light も用意する。
- 最小手順:
  1. `ゲームオブジェクト > 3D Object > Ocean` を選ぶ。
  2. Ocean の Transform の Y を基準水位に合わせる。
  3. `グリッド解像度`を 16～2048 の 2 の累乗から選ぶ。
  4. `海面サイズ`、`波の高さ`、`最大波高`、`波長`を調整する。
  5. `主波方向 XZ`と`副波方向 XZ`を異なる方向にする。
  6. `風速`、`水深`、`方向分散`、`うねりの強さ`でスペクトルを調整する。
  7. `泡の強さ`、`泡の閾値`、`粗さ`、`反射`、`屈折`を調整する。
  8. `浅瀬色`と`深海色`を設定し、Scene View と Game View の両方で確認する。
- 主な初期値: 解像度 2048、海面サイズ 240、波高 1.8、最大波高 4.5、波長 28、風速 14、水深 80、粗さ 0.12、反射 0.85。
- 解像度: Inspector の `+` / `-` は 16、32、64、128、256、512、1024、2048 の順に切り替える。中間値を入力した場合は 2 の累乗へ正規化される。
- Play 時: 描画用 FFT 変位を更新する。Buoyancy も同じ Ocean のサンプリング結果を参照する。
- 描画: Fresnel 反射、屈折、吸収、浅瀬色、深海色、微細法線、波頭の泡を同じ水面データから合成する。
- 性能: 解像度を上げるほど FFT と頂点処理の負荷が増える。まず 256 または 512 で調整し、最終確認時だけ 1024 / 2048 を試す。
- 制限: Ocean は通常の MeshRenderer ではない。通常半透明 OIT、平面反射面、Terrain と同じ設定として扱わない。
- 確認手順: Light を斜めから当て、近景の波頭、遠景の連続性、浅瀬色、反射、カメラが水面下へ移動した時の Underwater / Caustics を別々に確認する。

### Foliage

- 目的: Density Map と GPU Instancing で草木を大量配置し、距離に応じて描画密度を下げる。
- 追加場所: `コンポーネントを追加 > 地形・タイルマップ > フォリッジ`。
- 必要条件: 描画する FBX / OBJ を持つ MeshFilter / ModelRenderer。配置密度を制御する画像を使う場合は Density Map。
- 最小手順:
  1. 草や木のモデルを持つ GameObject を作る。
  2. Foliage を追加する。
  3. `Density Map`へ白黒画像を設定する。
  4. `配置範囲`、`密度`、`最大Instance数`、`LOD距離`を設定する。
  5. `風向き`、`揺れ幅`、`風速`、`空間周波数`、`時間倍率`を調整する。
- 主な初期値: 配置範囲 60 x 1 x 60、密度 1、最大 4096 Instance、LOD距離 120、揺れ幅 0.18、風速 14。
- Play / 描画時: Instance をまとめて描画し、距離で密度を落とし、頂点を風で変形する。
- 制限: Terrain の Brush で草を塗る機能とは別。Density Map と配置範囲を使用する。
- 確認手順: 近距離と遠距離で密度、影、風の揺れ、透過境界を確認し、最大Instance数を増やした時は FPS と VRAM も記録する。

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
- 現在の方式: `スクリーンスペース反射`、`キューブマップ反射`、`平面反射`を選択する。
- 現在の設定: `反射像の強さ` 0～4、`反射の粗さ` 0～1、`中心`、`サイズ`。
- 使い分け:
  - Screen Space は画面内の情報だけを反射するため、画面外や背面は欠ける。
  - Cubemap は環境反射向けで、鏡のような現在 Scene の厳密な像ではない。
  - Planar は平面向けで、反射面となる GameObject の位置・回転・Mesh Bounds を基準にする。
- 平面反射の確認手順: 平らな Mesh に ModelRenderer と ReflectionProbe を置き、種類を`平面反射`にする。Transform を回転した場合も反射面が Mesh と一致するか、Scene View と Game View を個別に確認する。

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
- 追加済み設定: Auto Exposure、Minimum / Maximum Exposure、Exposure Adaptation Speed、Target Luminance、Temperature、Tint、Lift、Gamma、Gain。
- AA の使い分け: None、FXAA、SMAA、Temporal は排他選択。SMAA はしきい値と角丸め、Temporal はシャープネスと履歴ブレンドを調整する。
- 最小手順:
  1. 空の GameObject に PostProcess を追加する。
  2. AA を 1 種類選ぶ。
  3. Bloom / Glare を必要な方式だけ有効にする。
  4. 明るさを固定する場合は自動露出を OFF にして`露出`を調整する。
  5. 明暗差へ追従させる場合は自動露出を ON にし、下限、上限、追従速度、基準輝度を設定する。
  6. Tone Mapping、White Point、Saturation、Contrast を調整する。
  7. Temperature / Tint と Lift / Gamma / Gain は最後に微調整する。
- 注意: Bloom の強さと最終合成側の Bloom 量を同時に上げると二重に強くなる。SSR は ReflectionProbe の方式や材質 Reflection と役割が異なる。

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

### AutoConvexCollision

- 目的: FBX / OBJ の位置頂点だけを使い、Play 開始時に Jolt の Convex Hull Collider を自動生成する。
- 追加場所: `コンポーネントを追加 > 3D物理 > Auto Convex Collision`。
- 必要条件: 同じ GameObject に MeshFilter または ModelRenderer。Dynamic 物体として動かす場合は Rigidbody。
- 最小手順:
  1. FBX / OBJ を Scene へ配置する。
  2. Auto Convex Collision を追加する。
  3. 描画メッシュをそのまま使う場合は個別の Asset Path を空にする。
  4. 別メッシュを判定に使う場合は Collider 側へ FBX / OBJ を設定する。
  5. `中心`と`サイズ`を見た目に合わせる。
  6. Play し、`入力頂点数`が 0 でないことと衝突を確認する。
- Play 時: 頂点位置から `Jolt ConvexHullShape`を生成する。FBX のMaterial、Texture、AnimationなどはCollider生成に使わない。
- MeshColliderとの違い: MeshCollider は三角形形状を保つ。Auto Convex は凹みや穴を凸包で埋めるが、Dynamic Rigidbody 向けに軽い判定を作りやすい。
- 制限: 複数の凸パーツへ自動分解する機能ではなく、1つの凸包として扱う。凹形状を正確に判定する場合はMeshColliderを残して使い分ける。

### Buoyancy

- 目的: Ocean の波面に対して船体全体へ浮力セルを自動配置し、水没量に応じて Rigidbody へ浮力と抵抗を加える。
- 追加場所: `コンポーネントを追加 > 物理 > Buoyancy`。
- 必要条件: 同じ GameObject の Rigidbody と BoxCollider、AutoConvexCollision、または MeshCollider。Scene 内に有効な Ocean。
- 最小手順:
  1. 船体モデルへ Rigidbody と Collider を追加する。
  2. Buoyancy を追加する。
  3. `対象 Ocean`を設定する。未設定なら有効な Ocean の自動検出を使う。
  4. `船体サイズ`を船全体に合わせ、`浮力中心`を重心に合わせる。
  5. Play し、`浮力`で沈み込み量を調整する。
  6. 上下の跳ねを`上下減衰`、横滑りを`水の抵抗`、不自然な回転を`回転抵抗`で抑える。
- 主な初期値: 船体サイズ 3 x 1.2 x 6、浮力 18、上下減衰 5、水の抵抗 1.4、回転抵抗 1.8。
- Play 時: 船体サイズに応じて 8～512 点へ分割する。固定5点や船底1点だけの判定ではない。
- 注意: Transform を毎フレーム直接設定する移動Componentと併用すると物理姿勢を上書きする。レール移動と併用する場合は、どちらが位置を決めるかをゲーム側で分離する。
- 確認手順: 平水面で静止、波ありで上下、片側だけ波に乗った時の傾斜、横速度を与えた時の抵抗を順番に確認する。

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
- 追加設定: Bus、同時発音数、再発音間隔、Doppler、Spread、指向性Inner / Outer Angle、Outer Volume、Occlusion、Reverb Send、初期反射。
- 初期値: Volume 1、Pitch 1、自動再生ON、Spatial Blend 1、Min 1、Max 50、Bus SFX、最大4 Voice、再発音間隔0.03秒、Doppler 1、Cone 360度、Occlusion 0.65。
- 最小手順:
  1. Main CameraへAudioListenerを追加する。
  2. 音源GameObjectへAudioSourceを追加してWAVを指定する。
  3. 2D音ならSpatial Blend 0、3D音なら1にする。
  4. Min / Max Distanceを設定する。
  5. 指向性が必要ならCone Inner / Outerを360未満へ下げ、Sourceの前方向を確認する。
  6. 壁越しの減衰にはOcclusionを設定し、Colliderを置く。
  7. Reverb Zoneを使う場合はReverb Sendと初期反射を設定する。
  8. Play中の再生 / 停止Button、移動、壁、Zoneで聴き比べる。
- Play 時: Voice上限、Retrigger抑止、距離減衰、Pan、Cone、Doppler、Collider Raycast遮蔽、Reverb Submix Sendを更新する。
- 注意: Inspector上限はPitch 3でも、Voiceへ設定するFrequency Ratioは実装上0.01～2へClampされるため、UIと実音の差を制限として記載する。

### Audio Filter / Reverb Zone

- 対象: AudioLowPassFilter、AudioHighPassFilter、AudioEchoFilter、AudioDistortionFilter、AudioReverbFilter、AudioChorusFilter、AudioReverbZone。
- 目的: 音質や空間効果を変える。
- 使う場面: 水中、洞窟、無線、残響、特殊演出。
- 現在の実接続候補: Low Pass、High Pass、Reverb Filter、Reverb Zone。
- Reverb: Dry音はMasterへ残し、Reverbと初期反射をSubmixへ並列送信する。
- Zone: Listenerが複数Zone内にある場合、最も強いReverb量を使用する。
- 注意: Echo、Distortion、Chorusを含め、Inspector表示だけかXAudio2 Effect Chainへ反映されるかComponentごとに確認する。未接続なら設定のみと書く。

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

### SceneButton

- 目的: C++ Script を書かず、Game View 上のボタンから指定 `.scene`へ遷移する。
- 追加場所: `コンポーネントを追加 > UI > Scene ボタン`。
- 最小手順:
  1. SceneButton を持つ GameObject を作る。
  2. `表示文字`、`位置`、`サイズ`、通常・Hover・押下色を設定する。
  3. Project で遷移先 `.scene`を選択する。
  4. Inspector の`選択中 Scene を設定`を押す。
  5. `ファイル > ゲームをビルド...`で遷移元と遷移先をビルド対象へ追加する。
  6. Play し、Game View でボタンをクリックする。
- 注意: `操作可能`がOFF、Scene Pathが空、ビルド対象外、Game Viewに入力Focusがない場合は遷移しない。

### UIValueBinding

- 目的: Health、RailFollower、Activeの値を、同じGameObjectのText / TextMeshProUGUIとSliderへ反映する。
- 追加場所: `コンポーネントを追加 > UI > 値バインディング`。
- 最小手順:
  1. UI表示用GameObjectにTextまたはTextMeshProUGUIを追加する。
  2. 必要なら同じGameObjectへSliderも追加する。
  3. UIValueBindingを追加する。
  4. `Source Object`へHealthまたはRailMovementを持つGameObjectを設定する。
  5. `値`からHealth現在値、Health比率、RailFollower進行率、Activeを選ぶ。
  6. Text用の`接頭文字`、`小数桁`、`表示倍率`を設定する。
- 初期値: Health比率、小数0桁、表示倍率100。したがって既定では`0～100`の百分率表示になる。
- Play 時: Textは`元値 x 表示倍率`を表示する。Sliderは表示倍率を使わず、元値をSliderのMin / MaxへClampして入れる。
- 制限: 別GameObjectのTextやSliderを自動探索しない。表示先ComponentはUIValueBindingと同じGameObjectに置く。

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

### TimelineEvent

- 目的: Play経過秒またはRailFollower進行率が境界を越えた時、対象C++ Scriptへ名前付きActionを送る。
- 追加場所: `コンポーネントを追加 > 入力・イベント > Timeline Event`。`ウィンドウ > Event Timeline`から作成・配置編集もできる。
- 設定: `時間 Source`、`発火秒`または`発火進行率`、`進行率 Source`、`Action 対象`、`Action 名`、`一度だけ`。
- Play 時: 条件が false から true へ変わった瞬間に1回通知する。Actionの`buttonValue`には発火秒または発火進行率の設定値が入る。
- 注意: 経過秒は一度境界を越えると通常はfalseへ戻らない。Railを巻き戻して境界より下へ戻した場合、`一度だけ`がOFFなら再度通過時に通知できる。

### ThresholdState

- 目的: Health比率またはRailFollower進行率を3区間へ分け、状態が変わった時だけ対象ScriptへActionを送る。
- 追加場所: `コンポーネントを追加 > 入力・イベント > Threshold State`。`ウィンドウ > State Graph`から追加・編集もできる。
- 設定: `値 Source`、`Source Object`、`Action 対象`、State 2 / 3境界、State 1 / 2 / 3 Action。
- Play 時: Healthは値の低下方向、Railは値の上昇方向でState 1から3へ判定する。境界値の大小が逆でも内部で並べ替える。
- Action値: State 1は1.0、State 2は2.0、State 3は3.0を`buttonValue`へ入れる。
- 注意: Play開始後の最初の評価でも、初期Stateが確定した時に対応Actionが通知される。

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

### FreeTransform

- 目的: Rigidbodyの力を使わず、指定軸だけを毎フレーム移動・回転する。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > 自由移動/回転`。
- 設定: 移動入力、移動速度、回転入力(deg/s)、回転速度、移動X/Y/Z、回転X/Y/Z、ローカル空間。
- 初期値: 移動速度5、回転速度90、全軸ON、ローカル空間ON。
- 使う場面: 移動床、回転展示台、背景Object、物理を必要としない単純な自動運動。
- 注意: Rigidbody、Animation、Constraint、RailMovementが同じTransformを書き換える構成は避ける。

### RailMovement

- 目的: Rail Pathの子GameObjectを制御点として、任意GameObjectを距離基準で移動する汎用RailFollower。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > レール移動`。編集は`ウィンドウ > Spline Editor`。
- 作成手順:
  1. 移動させるGameObjectへRailMovementを追加する。
  2. `ウィンドウ > Spline Editor`を開く。
  3. 対象を選択した状態で`新規Spline`を押す。
  4. 作成された`Spline Path`の子`Point 00`以降をScene Gizmo、位置入力、上面XZ / 側面ZY Canvasで動かす。
  5. 必要なら`制御点を追加`を押す。最低2点必要。
  6. Inspectorで速度、加速度、減速度、開始位置、向きの先読みを設定する。
  7. Loop、進行方向へ回転、滑らかな曲線、開始停止、逆方向、終端停止を設定する。
  8. Play中はSpline Editorの進行率Slider、停止/再開、順方向/逆方向でPreviewする。
- 初期値: 速度8、開始位置0、先読み1、Loop OFF、進行方向へ回転ON、滑らかな曲線ON、終端停止ON。
- C++ Script: `RailFollower`で停止、再開、速度、逆方向、進行率Jump、Rail切替、位置・方向・長さ・終端通知を操作する。
- 制限: RailMovementは移動だけを担当する。攻撃、敵判定、Wave、Camera、ゴールなどのゲームルールは持たない。

### Health

- 目的: 体力、耐久値、シールドなどに使う汎用の現在値と最大値を保持する。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > 体力`。
- 初期値: 最大100、Play開始時の現在値100。
- Inspector: 最大値を編集し、現在値は実行中表示として確認する。
- 連携: ThresholdStateのHealth比率Source、UIValueBindingの現在値 / 比率Sourceとして使用する。
- 制限: ダメージ種別、無敵時間、死亡、ドロップなどのゲーム固有ルールはHealth自身に含めない。

### WaveSpawner

- 目的: 直下の子GameObjectをHierarchy順に、開始条件と間隔に従って順次有効化する。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > Wave Spawner`。`ウィンドウ > Event Timeline`には選択Objectを複製してWaveを作る補助機能がある。
- 作成手順:
  1. 空の親GameObjectへWaveSpawnerを追加する。
  2. 生成したいPrefab InstanceまたはGameObjectを直下の子にする。
  3. Hierarchy上で子を希望する順番に並べる。
  4. 開始条件をPlay開始またはRailFollower進行率から選ぶ。
  5. Rail開始なら進行率Sourceと開始進行率を設定する。
  6. 生成間隔と`開始時に子を待機`を設定する。
  7. 必要な場合だけAction対象と開始・各生成・完了Actionを設定する。
- 初期値: Play開始、生成間隔0、開始時に子を待機ON、Action名は`OnWaveStarted`、`OnWaveSpawned`、`OnWaveCompleted`。
- Play 時: 間隔0なら条件成立Frameで全子を有効化する。各生成Actionの`buttonValue`は生成したGameObject ID、完了Actionは子数。
- 制限: 子の移動、攻撃、HP、敵全滅判定は扱わない。各子のComponentまたはゲーム側Scriptで構成する。

### 旧RailShooter互換型

- `LegacyRailShooterEnemy`、`LegacyRailShooterShip`、`LegacyRailShooterEnemyMotion`、`LegacyRailShooterStage`は旧Sceneを読み込むための互換スロットである。
- Add Componentの通常機能として使用手順を作らない。
- Engine Runtimeはこれらのゲームルールを実行しない。新規SceneではRailMovement、WaveSpawner、TimelineEvent、ThresholdState、Health、UIValueBinding、C++ Scriptを必要な組み合わせで使う。

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
- 現在の運動方式: 直線、軌道、渦、波、吸引、雲、爆発 / 水しぶき、Projectile Trail。
- 現在の衝突方式: Depthは画面内エフェクト向け。Physics SDFは物理Objectとの判定向け。Depthは画面外、裏側、薄い形状を正確には判定できない。
- 描画形状: FBX / OBJをParticle 1個の形として指定できる。未設定時はCamera向きの板ポリゴンをGPU Instancingする。
- Play操作: Inspectorから`エフェクトを再生`、`新規発生を停止`、現在の生存数確認ができる。
- Effect Asset: `.effect`は共有設定を読み込み、`.efk` / `.efkefc`はEffekseer 1.70e DX12 Runtimeで再生する。

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

### Terrain

- 目的: Height Mapから地形を生成し、近距離・中距離・遠距離の3段階LODで描画する。
- 追加場所: `コンポーネントを追加 > 地形・タイルマップ > テレイン`。
- 手順: Height Mapを設定し、`サイズ X / 高さ / Z`と`最高LOD解像度`16～256を設定する。
- 描画: Shadowは通常描画より一段低いLODを使う。
- 制限: Brush編集やTerrain Layer塗装が実装されていると推測しない。現在のInspectorにあるHeight Map、サイズ、LODを基準に説明する。

## FeelKit

### HapticSource

- 目的: FeelKitHaptics の振動を再生する。
- 使う場面: 衝突、攻撃、UI 決定、ダメージ。
- 必要条件: FeelKitHaptics Library、対応 Device、Effect Asset。
- 主な設定: Strength、Duration、Loop、Effect Path。
- 注意: 接続 Device がない時の挙動を説明する。
