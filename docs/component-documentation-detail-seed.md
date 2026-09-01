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
- 必要条件: ユーザー `.h` / `.cpp`、自動生成 `.Generated.cpp`、Build、DLL Path、API Version 一致。
- 主な設定: Class 名、DLL Path、Build ボタン、Script 有効状態。
- Play 時: 使用者が実装した`Start()`、`Update(float)`、`FixedUpdate(float)`、Collision / Trigger、`Stop()`が呼ばれる。未実装処理はno-opになる。
- C++ Script: `Script`、`GetGameObject()`、`GetComponent<T>()`と高水準Wrapperを使用する。
- Script作成時Template: ProjectでC++ Script Assetを作成すると、ユーザー用`.h` / `.cpp`とEngine管理の`.Generated.cpp`を生成する。使用者はGenerated側のDLL ABIを編集しない。
- Template選択UI: Category、日本語表示名、説明、推奨Componentを表示する。推奨Componentは自動追加ではなく、使用者がScene構成として確認するための情報である。
- 代表Template: Empty、PlayerController、RailPlayer、EnemyController、TurretController、HomingController、BossController、StageController、LoadoutController、PhysicsController、HealthDamageController、SpawnPoolController、CameraEffectsController、AnimationEffectController、AudioController、UiController、ActionEventController、SaveCheckpointController、OceanBuoyancyController、NavigationAiController、RuntimePropertyController。
- Action候補: Constructorの`BindAction`だけをユーザーが記述し、Generated側が登録済みAction名をInspector候補として公開する。
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
- 描画: Fresnel反射、屈折、RGB吸収、浅瀬色、深海色、微細法線、波頭の泡を同じ水面データから合成する。画面微分がFFTセル幅を越える距離では細波法線を段階的に除去し、法線補間分散を鏡面Roughnessへ加えて遠景の水玉状HighlightとMesh三角形境界を抑える。
- 自動平面反射: 明示的なPlanar Reflection Probeがない場合だけ、各Oceanを候補にしてScene View / Game ViewのCameraへ最も近い水域のTransform Y面から反射Captureを生成する。EnvironmentをFallback、Planar Captureを画面外補完、SSRを画面内の最終補正として合成する。Ocean自身はCaptureしない。反射強度0のOceanはCapture候補から除外し、明示Planar Probeがある場合は鏡面合成を優先する。
- Underwater: Cameraと物体のFFT水面に対する位置、視線の水中通過距離、波面Slope / Curvatureを使い、境界、RGB吸収、散乱、屈折、Causticsを計算する。屈折UVは各Viewport内へClampされ、Scene ViewとGame ViewはそれぞれCamera位置に最も適した同一FFT設定のOceanを選ぶ。
- 性能: 解像度を上げるほど FFT と頂点処理の負荷が増える。まず 256 または 512 で調整し、最終確認時だけ 1024 / 2048 を試す。
- 性能上の注意: 自動平面反射はSceneを追加描画する。FFT解像度だけでなく反射Captureあり/なしのGPU時間を比較し、不要なOceanは反射強度を0にする。物理・Gameplay QueryのCPU有限水深スペクトルはOcean設定変更時だけ16波を再生成し、同じ設定の多数Sampleではキャッシュを共有する。
- 制限: Ocean は通常の MeshRenderer ではない。通常半透明 OIT、鏡用Planar Reflection Probe、Terrain と同じ設定として扱わない。
- 確認手順: Light を斜めから当て、近景の波頭、遠景の連続性、浅瀬色、反射、カメラが水面下へ移動した時の Underwater / Caustics を別々に確認する。
- **Ocean Debug View（デバッグ表示、`oceanDebugView`）:** 海面シェーダの各成分を単体表示する診断コンボ。値は0～46の47段階（`kOceanDebugViewCount`個）で、コンポーネント経由でMaterial Dataの`oceanDebugView`へそのまま渡り、シェーダは実行時コンパイルのためリビルド不要で即座に切り替わる。用途別に大まかに以下のグループへ分かれる。

  | 範囲 | 内容 |
  | --- | --- |
  | 0 | 通常描画 |
  | 1～5 | 太陽反射系（太陽Diffuse、GGX Specular、Sun Glitter、Glitter反射整列、Glitter最終Mask） |
  | 6～11 | 法線・波構造・泡（Medium/Fine Normal、Normal差x8、傾き量、中波構造、Foam） |
  | 12～16 | 反射・屈折・水中（Sky反射、屈折Scene、Caustics、水中実体被覆、FFT泡チャンネル） |
  | 17～40 | Fine Micro Specular診断パイプライン（Fine Delta Slope、Lobe Normal、Micro Roughness、Sun Specular、Env Normal Delta、各比較用OFF版・Medium参照・ReflectDir・RawEnvSample・Compress後差分など、新旧アルゴリズムの中間値を1つずつ切り分けて確認するための連番） |
  | 41～43 | 定数出力（黒・グレー・赤の固定色。パイプライン自体が正しく動作しているかの基準確認用） |
  | 44～46 | 絶対量表示（x4／x64／x256倍率。微小な差分をゲインで持ち上げて視認する） |

  Final Compositeの`診断表示`（`compositeDebugView`、PostProcess節参照）とは名前が似ているが完全に別系統であり、相互に連動・自動切替はしない。Ocean Debug Viewは海面シェーダ内部の中間値を直接可視化する開発者向け診断であり、通常のゲーム進行やLocalContrast/Bloomの調整には使わない。確認後は必ず0（通常）へ戻す。
  根拠: `EditorInspectorPanel.cpp`（`oceanDebugViewItems`、`kOceanDebugViewCount`の`static_assert`）、`EditorSceneSynchronizer.cpp`（`sceneObject.ocean.debugView`→`materialData->oceanDebugView`への転送）、`EditorScene.h`/`EditorScene.cpp`（`oceanDebugView`のScene保存・Clamp）。

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

#### ゲーム中の視点操作（FreeLook / Orbit）

既存のCamera ComponentのInspectorにある「ゲーム中の視点操作」で設定する。追加Componentは不要。初期状態はOFFで、Scene Viewの編集用カメラ操作とは別の機能である。

1. Game Viewに使うCameraを有効にする。複数ある場合は優先度を確認する。「MainCamera」という名前だけで操作対象が決まるわけではない。
2. 「視点操作を有効化」をONにして、「操作形式」をFreeLookまたはOrbitにする。
3. 「回転入力」を「右ボタンを押している間」または「常時」にする。Game View内で操作を開始する。

| 操作形式 | 操作・設定 | 実際の動作 |
| --- | --- | --- |
| 共通 | マウス移動 | Yaw/Pitch回転。感度、Y軸反転、Pitch制限を設定できる。 |
| 共通 | カーソル固定／非表示 | 回転操作中だけ適用。操作終了、標準操作OFF、Play停止時に解放する。 |
| FreeLook | WASD/QE移動 | W/Sは視線方向の前後、A/Dは左右、Eは上、Qは下。Game View内では右ボタンを押していなくても移動できる。 |
| FreeLook | 移動速度／Shift倍率 | 速度はワールド単位/秒。左右どちらのShiftでも加速する。斜め移動は正規化する。 |
| Orbit | Orbit中心／中心Offset | 指定Objectのワールド位置＋Offsetを中心に回る。Offsetは対象のローカル座標ではない。 |
| Orbit | 距離／最小距離／最大距離 | 「距離」は操作初期化時の距離。ホイール操作後の距離は別の実行時状態に保持する。 |
| Orbit | ホイールZoom速度 | ホイール1段あたりの距離変化。中心への追従とZoomは右ボタンを押していなくても働く。WASD移動は行わない。 |

Orbit中心が未指定（ID=-1）の場合はCameraの接続先を使用する。有効な接続先もない場合は、中心Offsetそのものをワールド座標の中心として使う。Playerを中心にするならOrbit中心へPlayerを指定し、中心Offsetで頭の高さなどを調整する。

現在の実装では、操作開始時にCameraの接続先への追従を反映した位置・回転を初期姿勢として解決する。FreeLookはその姿勢から移動し、Orbitは中心・距離設定から位置を計算する。標準操作中の姿勢は描画用の実行時状態であり、Camera GameObjectのTransformへ書き戻さない。

Camera Blend中は標準操作を適用せず、標準操作が有効なときはFollowComposerより優先する。独自Scriptで姿勢を制御する場合は`cameraInputEnabled`をOFFにし、競合する追従やBlendも整理する。Camera Component自体をOFFにする必要はない。[Scriptによる標準設定・独自操作](cpp-script-documentation-detail-seed.md#cameraの標準操作と独自script操作)を参照。

18項目の設定はSceneの`CameraInputExtension`へ保存・読込され、Orbit中心のObject参照もID再割り当ての対象となる。操作中の一時的な姿勢・Zoom距離は設定値として保存しない。画面端での自動スクロールや壁との衝突回避はこの標準操作には含まれない。

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
- **診断表示（compositeDebugView、Final Composite診断）:** LocalContrastとBloomのどちらが元画像の模様（泡、Sun反射など）を最終合成へ再注入しているかを切り分けるための専用デバッグモード。sceneColorを50%グレーへ強制した上で、選択した効果だけを重ねて表示する。値は0～4の5段階で、Inspectorのコンボ「診断表示」から選ぶ。

  | 値 | 表示内容 |
  | --- | --- |
  | 0 | 通常描画（デバッグ無効） |
  | 1 | 基準のみ（LocalContrast/Bloomとも無効化した50%グレー） |
  | 2 | 元画像再参照のみ（LocalContrastだけ有効） |
  | 3 | ブルームのみ（Bloomだけ有効） |
  | 4 | 両方（LocalContrast+Bloom同時） |

  1と2/3を見比べることで、意図しない模様の再現が「LocalContrastの元画像再参照」由来か「Bloom」由来かを画面上で直接切り分けられる。Ocean側の`Ocean Debug View`（Oceanコンポーネント参照）とは完全に独立した別系統の診断機能であり、互いに連動・自動切替はしない。値はScene保存対象（保存フォーマットのelements[10]相当）。デバッグ用途のためPlay終了後や確認後は必ず0へ戻す。
  根拠: `EditorInspectorPanel.cpp`（`compositeDebugViewItems`、`DrawFinalCompositeComponent`相当の描画箇所）、`EditorRenderManager.cpp`（`compositeDebugView`をシェーダパラメータへ渡す箇所）、`EditorScene.h`（フィールド定義コメント）。

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
- 主な設定: Mass、Colliderから質量を計算、実質密度、Use Gravity、Velocity、Angular Velocity、Drag、Angular Drag、慣性倍率、重心オフセット、Freeze、Interpolation、Collision Detection。
- 自動質量: `Colliderから質量を計算`をONにすると、Play開始時にJolt Shapeの体積へ`実質密度 kg/m3`を掛けて質量を決める。箱、球、Capsule、Auto Convex、Dynamic MeshのConvex Hullで同じ式を使い、Shapeから求めた慣性Tensorも計算後の質量へ合わせる。中空物体は材質そのものの密度ではなく、空洞、積荷、機関、Ballastを含む物体全体の質量を外形体積で割った実質密度を入力する。
- 重心オフセット: Colliderから一様密度で求めた重心へ加えるローカル位置。Joltの`OffsetCenterOfMassShape`へ接続され、衝突形状を動かさず、慣性とForce Momentの基準を変更する。負のYは船底Ballast、下側へ質量が集中する物体、背の高い車両などへ使う。GameObject Scaleを反映した後の物理Shapeへ適用される。
- C++ Script: `GetVelocity`、`SetVelocity`、`AddForce`、`AddImpulse`、`AddTorque`。
- 注意: Transform 直接変更との競合を必ず説明する。

### BoxCollider

- 目的: 箱形状の当たり判定を作る。
- 使う場面: 床、壁、箱、単純な障害物。
- 主な設定: Center、Size、Is Trigger、Physics Material、Layer。
- 注意: 見た目 Mesh と Collider Size が一致するか確認する。
- **重要な注意（MeshCollider/AutoConvexCollisionとの非対称性）:** `RefreshRuntimeMeshColliderBounds()`（`EditorJoltPhysicsManager.cpp`）はモデル差し替えやScale変更に合わせてColliderの`Center`/`Size`を自動で再計算するが、これは`MeshCollider`、`AutoConvexCollision`、`TerrainCollider`のみが対象であり、**`BoxCollider`は対象外**である。つまりBoxColliderの`Center`/`Size`はInspectorで手入力した数値のまま固定され、後からモデル（FBX/OBJ）やGameObjectのScaleを変えても自動追従しない。ブリッジ追加などでモデル形状を変更した後にBoxColliderを使い続けると、見た目と当たり判定がずれて「近くにいるのに当たらない／当たらないはずの場所で当たる」原因になる。モデル形状を変えたら、AutoConvexCollisionを一時的に追加して自動検出された`Center`/`Size`の値を確認し、その数値をBoxColliderへ手動で転記するのが確実。
  根拠: `EditorJoltPhysicsManager.cpp` `RefreshRuntimeMeshColliderBounds()`（BoxColliderの分岐が存在しないことを確認）。

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
- **動的MeshCollider（Convex+Rigidbody）の既知の潜在リスク:** `Convex`かつRigidbody付きでDynamicとして使う場合、内部的には`CreateMeshBody`→`CreateDynamicMeshBody`が呼ばれ、AutoConvexCollisionが使う共通ヘルパー`CreateConvexHullShape()`（凸包半径を`min(0.05, 最大範囲*0.1)`で確保する修正済み版）を経由せず、`JPH::ConvexHullShapeSettings(hullPoints, 0.0f)`を直接、凸包半径0のまま呼んでいる。半径0はGJK/EPAの投機的接触マージンが無くなることを意味し、AutoConvexCollisionで実際に「Boxでは当たっていたのにAuto Convexに変えた途端に一切当たらなくなる」不具合を起こした原因と同じ条件である。したがって動的MeshColliderをConvexで使う構成でも、同種の当たり判定漏れが理論上起こり得る（本セッションでは未再現・未修正）。命中しない場合はAutoConvexCollisionへの置き換えを試すか、`CreateDynamicMeshBody`にも同じ非ゼロ半径を適用する修正を検討する。
  根拠: `EditorJoltPhysicsManager.cpp` `CreateMeshBody()`→`CreateDynamicMeshBody()`（`JPH::ConvexHullShapeSettings(hullPoints, 0.0f)`固定）、対比: `CreateConvexHullShape()`（`convexRadius`修正済み、`AutoConvexCollision`の実体生成経路）。

### AutoConvexCollision

- 目的: FBX / OBJ の位置頂点だけを使い、Play開始時にJoltの複数Convex Hull Colliderを自動生成する。
- 追加場所: `コンポーネントを追加 > 3D物理 > Auto Convex Collision`。
- 必要条件: 同じ GameObject に MeshFilter または ModelRenderer。Dynamic 物体として動かす場合は Rigidbody。
- 最小手順:
  1. FBX / OBJ を Scene へ配置する。
  2. Auto Convex Collision を追加する。
  3. 描画メッシュをそのまま使う場合は個別の Asset Path を空にする。
  4. 別メッシュを判定に使う場合は Collider 側へ FBX / OBJ を設定する。
  5. `中心`と`サイズ`を見た目に合わせる。
  6. `最大凸包数`を1～16で設定する。箱など低Polygonの単純形状は自動的に1個へ制限される。
  7. Playし、Consoleの`凸包=N`と衝突を確認する。
- Play時: 三角形をモデル境界の最長軸に沿う非重複区間へClipし、各区間の表面点から`Jolt ConvexHullShape`を生成して`StaticCompoundShape`へまとめる。区間は重ならないため、複数Hullの体積を重複加算しない。FBXのMaterial、Texture、AnimationなどはCollider生成に使わない。
- 失敗時: 一部の区間HullまたはCompound生成に失敗した場合はモデル全体の単一ConvexHullへ戻し、それも失敗した場合だけBox近似へ戻す。欠けた区間だけを使って物理形状を作らない。
- MeshColliderとの違い: MeshColliderは静的Bodyで三角形形状を保つ。Auto ConvexはDynamic Rigidbodyで使える複数凸Shapeへ近似し、長い船体、車体、岩などの全体を1個の大きな凸包で埋める量を減らす。
- 制限: 最長軸の区間分割であり、VHACDのような任意方向の凹部解析ではない。同じ断面内の穴や深い凹みは各区間の凸包で埋まるため、必要なら判定専用の簡略MeshをCollider Assetへ設定する。
- **既知の不具合と修正: 凸包半径0による命中漏れ。** `CreateConvexHullShape()`（区間Hull・単一Hullフォールバック共通の生成ヘルパー）は元々`JPH::ConvexHullShapeSettings(hullPoints, 0.0f)`と凸包半径0で生成していた。BoxColliderでは命中していた対象をAutoConvexCollisionへ切り替えた途端に一切命中しなくなる不具合の実際の原因の一つがこれで、半径0はJoltのGJK/EPA投機的接触マージンを消し去り、`ShapeCast`の開始点が既にShape内部にある高速Projectile等の判定を取りこぼしやすくする。修正として`convexRadius = min(0.05, 最長範囲*0.1)`（Jolt既定の`cDefaultConvexRadius`相当、極端に小さいShapeでは半径が本体より大きくならないよう上限を掛ける）を全Hull生成へ適用済み。この修正は`AutoConvexCollision`が使う経路にのみ入っており、MeshColliderのDynamic Convex経路（`CreateDynamicMeshBody`、「MeshCollider」節参照）には未適用。
  根拠: `EditorJoltPhysicsManager.cpp` `CreateConvexHullShape()`（`convexRadius`計算とコメント）。

### Buoyancy

- 目的: 描画と共通のFFT波面で実Physics Shapeを切り、水没体積と浮心に基づいてRigidbodyへ浮力、減衰、流体抵抗、着水衝撃を加える。
- 追加場所: `コンポーネントを追加 > 物理 > Buoyancy`。
- 必要条件: 同じGameObjectのDynamicかつ非KinematicなRigidbodyと3D Collider。Scene内に有効なOcean。
- 対応Shape: Box、Sphere、Capsule、Auto Convexを直接計算する。Dynamic MeshColliderはJolt Body生成時のConvexHullを計算対象にし、三角形MeshCollider自体は削除しない。Joltが体積を返せない特殊Shapeだけ従来の分布グリッドへフォールバックする。
- 最小手順:
  1. 船体モデルへ Rigidbody と Collider を追加する。
  2. Buoyancy を追加する。
  3. `対象 Ocean`を設定する。未設定なら有効な Ocean の自動検出を使う。
  4. Colliderを実際に排水する船体形状へ合わせる。上部構造まで含む大きなBoxより、Auto Convexまたは船体用Colliderを優先する。
  5. `自動物理を使用`をONにし、海水なら`水密度=1025`、淡水なら約1000を設定する。
  6. `目標水没率`へ平水面で沈めたい排水体積率を設定する。0.55ならRigidbodyの実質密度を`水密度 x 0.55`へ同期し、Collider体積から質量と慣性を自動計算する。
  7. Playし、喫水、Pitch / Roll、船首先行と横腹先行、着水を確認する。自動物理では船体サイズ、浮力中心、浮力、方向別抗力、減衰、着水係数を手動調整しない。
- 自動物理: Collider中心と寸法をFFT Probe範囲へ使い、Joltへ反映済みの実質量、Shape排水体積、水密度から`Fb = waterDensity x displacedVolume x |gravity|`を計算する。局所FFT水深から浮力作用点、投影面積と排水流体質量から並進・回転付加慣性、船長と速度からFroude造波抵抗を求める。上下減衰は水線面積から臨界減衰比0.7、Pitch / Rollは水線二次Momentとメタセンタ高さから放射減衰を求める。船用・箱用のPreset分岐はない。
- 手動詳細設定: `自動物理を使用`をOFFにすると旧Scene互換の船体サイズ、浮力、上下減衰、方向別抵抗、回転抵抗、着水衝撃、波法線影響を編集できる。既存SceneはExtensionがなければ手動方式のまま読み込む。
- 主な初期値: 船体サイズ 3 x 1.2 x 6、浮力 18、上下減衰 5、前後抵抗 1.4、横抵抗 4、上下抵抗 2.5、回転抵抗 1.8、着水衝撃 2、波の横押し 0.2。
- Play時の水面: 船体幅・船長を覆う固定5x5の25点で同じFFTをSampleする。25点の高さへ最小二乗Planeを当てて実Shape体積と水線面積を求め、同じ25点を双線形補間して各水力面の3頂点と面中心における局所水位、法線、表面速度を求める。水力面が512面でもFFT評価は25点のままである。
- Play時の体積: Joltの`GetSubmergedVolume`で総体積、水没体積、水没部分の重心、Body重心、実Shape境界寸法を取得する。水面をわずかに上げた2回目の体積差`dV/dh`から水線面積も求める。
- Play時の静水圧と上下減衰: 自動物理は設定した水密度を直接使う。手動方式だけ`浮力`を完全水没時の基準加速度として実効流体密度へ変換する。静水圧合力の大きさは表面Meshへ置き換えず、常にJoltの`displacedVolume`から`density x displacedVolume x |gravity|`を求める。作用点だけは各水没面の`局所水深 x 面積 x 上向き投影率`で積分してJolt浮心から最大Shape半径35%まで寄せ、0.08秒時定数で平滑化する。面が開いている、法線が反転している、積分不能の場合はJolt浮心へ戻る。上下減衰は`density x gravity x waterplaneArea`の上下剛性から求め、自動物理は臨界減衰比0.7、手動方式は`上下減衰 / 10`を使う。
- Play時の水力面: Auto ConvexとDynamic Meshは判定用Compound内部面ではなく、Collider Assetの元三角形をRoot COM空間へ一度だけ保存する。Primitiveとその他ShapeはJolt BodyをLeaf Shapeまで展開する。512面を越える場合は総表面積を保つ面積分位Samplingへ縮約する。毎固定更新では各頂点を5x5補間水面でClipし、水中部分の面積、外向き法線、面積重心を圧力抗力と表面摩擦だけに使う。静水圧合力はこのMesh積分へ置き換えない。
- Play時の付加質量: 浮心における水との相対加速度へ、`水密度 x 排水体積`と各軸の投影面積比を掛ける。相対加速度は0.08秒時定数で平滑化する。回転も前回角速度との差を0.1秒時定数で平滑化し、回転軸が水を押す投影面積比、排水流体質量、形状二次Momentから付加慣性Torqueを求める。初回接水では履歴だけを作り、並進は6G、回転は`mass x shapeRadius x gravity x 4`を上限にする。
- Play時の回転放射減衰: 水線面積からRoll/Pitch水線二次Moment、排水体積からメタセンタ高さ、形状寸法と質量から近似慣性を求める。減衰比0.35の臨界減衰Torqueを現在角速度と逆向きへ加える。復元角度を強制するTorqueではなく、1固定更新で角速度を反転しない上限を持つ。
- Play時の造波抵抗: 船首軸方向の相対速度、Gravity、Shape船長から`Fn = speed / sqrt(gravity x length)`を求める。排水体積の`2/3`乗を基準面積とし、幅/長さ比、Fn 0.38付近の抵抗Hump、高速遷移を抵抗係数へ反映する。1固定更新で前後速度を反転させず2Gを越えない範囲で、進行方向と逆向きへ加える。
- Play時の圧力抵抗: 各水没面中心の速度を`linearVelocity + angularVelocity x (面中心 - 重心)`で求め、FFT水面速度を引く。相対流が面の外側から内側へ入る成分に`0.5 x density x Cd x submergedArea x normalSpeed^2`を適用する。鋭い船首は斜面へ分散する小さい正面速度と面積、横腹は進行方向を向く大面積になるため、同じ速度でも抵抗が異なる。船尾形状が船首と違えば後進時の抵抗も変わる。
- Play時の表面摩擦: 面に沿う相対速度へ、船長と20度付近の水の動粘性係数からReynolds数を求める。層流域は`1.328 / sqrt(Re)`、乱流域はITTC-1957の`0.075 / (log10(Re) - 2)^2`を使い、`前後の水抵抗`を粗さ倍率として適用する。
- Play時のMoment: 圧力抵抗と表面摩擦を各面の面積重心へ作用させ、`(面中心 - 重心) x 面の力`を合計する。斜航、横滑り、片側だけの水没、Yaw / Pitch / Rollでは左右・前後の作用点差がTorqueになる。Shape面を取得できない場合だけ、実Shape境界寸法の軸別投影面積と回転抵抗へフォールバックする。
- Play時の着水: 前回固定更新の排水体積をObjectごとに保持し、排水体積増加率と相対入水速度からSlammingを加える。静止中や完全水没後に毎Frame同じ着水衝撃を繰り返さない。
- 性能: FixedUpdate開始時にSceneの各GameObjectのComponent配列を1回だけ走査し、Rigidbody、Buoyancy、外力、拘束、場の参照を同じ固定更新内で共有する。風・重力場・流体・渦・圧力・電磁場の一時配列はCapacityを再利用する。実Shape体積、固定25点FFT Sample、最大512水力面を維持し、FFT Sample回数は水力面数に比例させない。
- 静止面の早期終了: 水没面の法線方向速度と接線方向速度が両方ほぼ0なら、その面の圧力係数、Reynolds数、ITTC摩擦計算を省く。結果が0になる計算だけを省略し、浮力、浮心、上下減衰、Slamming、面数判定は維持する。
- 復元: 通常経路も特殊Shapeの分布グリッドも、World上方向へ戻す物体種別非依存の人工Torqueを加えない。Jolt浮心または分布浮力点とRigidbody重心の位置差だけから自然な復元Momentを発生させる。回転放射減衰は現在角速度を止めるだけで、傾斜角を0へ戻さない。特殊Shapeの喫水制限は計算範囲の安全策として残るが、姿勢を上向きへ固定しない。
- 注意: Transform を毎フレーム直接設定する移動Componentと併用すると物理姿勢を上書きする。RailMovementでは`Dynamic Rigidbody 物理追従`と`浮力併用プリセット`を使う。
- 確認手順: 平水面で平衡喫水、片側浸水で浮力作用点移動、船首だけが短い波へ乗るPitch、回転開始時の付加慣性、速度域で変わる造波抵抗、鋭い船首を前へ向けた前進、横腹を前へ向けた同速度の移動、斜航時のYaw Moment、空中から一度だけ発生する着水衝撃、Pool再利用後の初期化を順番に確認する。船首と横腹の差を出すにはBoxではなく、船底を表すAuto Convexまたは船体用Colliderを使う。

### Aerodynamics

- 目的: Dynamic Rigidbody の速度と風速の差から、空気中の二次抗力、揚力、横力、Magnus 力、回転抗力を求める。
- 追加場所: `コンポーネントを追加 > 3D物理 > 空気力学`。
- 必要条件: 同じ GameObject に有効な Dynamic Rigidbody と Collider。風を Scene 側で共有する場合は WindZone。
- ローカル軸: `+Z`を前、`+Y`を上、`+X`を右として迎角と横滑りを計算する。
- 抗力: `Fd = 1/2 * rho * Cd * A * |vRelative|^2`。`vRelative`はRigidbody速度から基礎風速と全WindZone風速を引いた値で、力は相対速度と逆向きに加える。
- 揚力: `Cl = Cl0 + liftSlope * (angleOfAttack - zeroLiftAngle)`、`Fl = 1/2 * rho * Cl * liftArea * |vRelative|^2`。絶対迎角が失速迎角を越えると、90度へ近づくほど揚力係数を0へ減らす。
- 横力: 相対速度のBody Right成分を横滑り量として、設定した横力係数と側面積から横滑りと逆向きの力を加える。
- Magnus効果: `Fm = Cmag * rho * A * L * (angularVelocity x vRelative)`。回転する球、弾、ローターなどで使用する。
- 圧力中心: 合力をGameObject中心ではなく`圧力中心`へ`AddForceAtPosition`し、Joltが重心との差からPitch / Yaw / Roll Torqueを発生させる。
- 回転抗力: `M = -1/2 * rho * Cw * A * L^3 * |omega| * omega`。RigidBodyの線形なAngular Dragとは別の二次減衰である。
- 初期値: 空気密度1.225 kg/m3、Cd 0.47、代表面積1 m2、揚力0、横力0、回転抗力0.05、Magnus 0、合力上限100000 N。
- 最小手順:
  1. 動かすObjectへRigidbody、Collider、Aerodynamicsを追加する。
  2. 抗力だけならCdと代表面積を設定し、初速を与える。
  3. 翼ならLift Slopeと翼面積、ゼロ揚力迎角、失速迎角を設定する。
  4. 矢や飛行体は圧力中心を重心より後ろへ置き、姿勢が相対風へ戻るか確認する。
  5. 回転球はMagnus係数とAngular Velocityを設定し、軌道が曲がるか確認する。
- 制限: CFDや翼型別の圧力分布を解く機能ではない。代表係数を使う剛体向けモデルで、係数はモデル寸法とScene単位へ合わせる。

### WindZone

- 目的: 複数のAerodynamicsへ共通の風速場を供給する。
- 追加場所: `コンポーネントを追加 > 3D物理 > 風ゾーン`。
- 種類: `方向風`は指定方向へ一定風を流す。影響半径0ならScene全域、0より大きい場合はWindZone位置から半径端へ二次減衰する。`放射風`はWindZone中心から外向きへ流す。
- 乱流: 固定更新時間とWorld位置から連続した3軸Gustを作る。Frame乱数は使わないため、描画FPSが変わっても風速が不連続に跳ばない。
- 初期値: 方向風、方向+X、風速10 m/s、半径0、乱流0、周波数1。
- 必要条件: 力を受けるObject側のAerodynamics。WindZone単体ではRigidbodyへ直接Forceを加えない。
- 性能: 固定更新の共通Component索引から有効なWindZoneだけを参照し、AerodynamicsごとにScene全体を再走査しない。

### GravityField

- 目的: Global Gravityとは別に、GameObject位置を中心とする点重力をDynamic Rigidbodyへ加える。
- 追加場所: `コンポーネントを追加 > 3D物理 > 重力場`。
- 逆二乗: `a = G * sourceMass / max(distance, minimumDistance)^2`、`F = rigidBodyMass * a`。標準の万有引力定数は`6.67430e-11`。
- 定加速度: 距離に関係なく設定加速度で中心へ引く。Scene縮尺に合わせた小惑星、重力井戸、球形ステージに使う。
- 安全設定: `最小計算距離`で中心の特異点を避け、`影響半径`で計算対象を制限し、`加速度上限`で極端なForceを抑える。0の影響半径と加速度上限は無制限。
- 初期値: Newton逆二乗、G 6.67430e-11、引力源質量1e11 kg、最小距離1 m、影響半径0、加速度上限100 m/s2。
- 注意: Global GravityとGravityFieldは加算される。点重力だけを使う物体はRigidbodyの`重力を使用`を無効にする。
- 確認手順: Global Gravityを切った球へ接線方向の初速を与え、重力源へ落下すること、速度次第で周回すること、半径外では力が加わらないことを分けて確認する。

### RotatingFrame

- 目的: GameObject位置を原点とする回転座標系を定義し、範囲内のDynamic Rigidbodyへ遠心力、Coriolis力、Euler力を加える。
- 追加場所: `コンポーネントを追加 > 3D物理 > 回転座標系`。
- 遠心加速度: `aCentrifugal = -omega x (omega x r)`。回転軸から外向きへ働き、回転ステーションの人工重力などに使う。
- Coriolis加速度: `aCoriolis = -2 * omega x vRotating`。回転座標系内を移動する物体の軌道を横へ曲げる。
- Euler加速度: `aEuler = -angularAcceleration x r`。回転速度が増減する時だけ発生する。
- 相対速度: `vRotating = rigidBodyVelocity - (frameLinearVelocity + omega x r)`として、回転中心の並進と回転を差し引く。
- 初期値: 角速度(0,1,0) rad/s、角加速度0、中心速度0、影響半径0、加速度上限100 m/s2。
- 最小手順:
  1. 回転中心のGameObjectへRotatingFrameを追加する。
  2. World角速度と影響半径を設定する。
  3. 範囲内へDynamic Rigidbodyを置き、中心から外向きへ加速することを確認する。
  4. Rigidbodyへ接線または半径方向の初速を与え、Coriolis偏向を確認する。
  5. 回転の立ち上がりを表す場合だけ角加速度を設定する。
- 注意: GameObject Transformを自動回転させるComponentではない。見た目の回転はAnimation、FreeTransform、C++ Scriptなどで別に設定する。

### FluidVolume

- 目的: Oceanに依存しない有限の3D流体領域を作り、領域内のDynamic Rigidbodyへ浮力、線形粘性抵抗、二次抗力、回転粘性を加える。
- 追加場所: `コンポーネントを追加 > 3D物理 > 流体ボリューム`。
- 必要条件: 力を受けるGameObjectにDynamic Rigidbodyと3D Collider。FluidVolume側にColliderは不要で、`サイズ`、Transform回転、Transform Scaleが領域を決める。
- 部分浸水: Colliderから得たBody寸法をOBBとして各FluidVolume軸へ投影し、箱領域との重なり体積を求める。Body全体が入る前から重なり量に応じて浮力が連続的に増える。
- Archimedes浮力: `Fb = density * displacedVolume * |gravity|`。方向はScene Gravityと逆向きで、Global Gravityが0なら浮力も0になる。
- 圧力中心: 浮力と流体抵抗は重なり体積の中心へ`AddForceAtPosition`する。物体が片側だけ浸水した場合は重心との差から自然な復元Torqueが発生する。
- Stokes抵抗: 重なり体積から等価球半径`r=(3V/4pi)^(1/3)`を求め、`Fd=-6*pi*mu*r*vRelative`を加える。低Reynolds数域の粘性抵抗を担当する。
- 二次抗力: `Fd=-1/2*rho*Cd*A*|vRelative|*vRelative`。`A=displacedVolume^(2/3)`を代表投影面積として、高速域の流体抵抗を担当する。
- 回転粘性: 等価球の式`T=-8*pi*mu*r^3*omega`へ浸水率と`角粘性`を掛ける。
- 流速: `vRelative=rigidBodyVelocity-fluidFlowVelocity`。川、ベルト状の流れ、局所的な海流を作れる。
- 初期値: サイズ10 x 5 x 10 m、水密度1000 kg/m3、粘性0.001 Pa*s、Cd 1、流速0、角粘性1、合力上限1000000 N。
- 最小手順:
  1. 空のGameObjectへFluidVolumeを追加し、位置、回転、サイズを流体領域へ合わせる。
  2. 落とす物体へRigidbodyとColliderを追加する。
  3. 水なら密度1000前後、空気や粘性液体なら密度と粘性を変更する。
  4. Playし、境界へ入る途中から浮力が増えること、流速に追従すること、片側浸水で傾くことを確認する。
- 使い分け: 波面と同期する広域海面はOcean + Buoyancy、箱形の水槽・川・粘性領域はFluidVolumeを使う。同じ場所で両方を有効にすると力は加算される。
- 制限: Colliderの厳密な水没ポリゴン積分ではなくOBB投影体積近似である。高速で薄い境界を越える場合はFixed Time StepとCollider寸法も確認する。

### SpringForce

- 目的: 物理拘束を生成せず、所有者AnchorとWorld固定点または別GameObject Anchorの間へHookeばね力を加える。
- 追加場所: `コンポーネントを追加 > 3D物理 > ばね力`。
- 必要条件: 所有者にDynamic RigidbodyとCollider。接続先はTransformだけでも固定Anchorとして使え、Dynamic Rigidbodyなら反作用を受けられる。
- Hooke則: `Fspring = stiffness * (length-restLength)`。自然長より長い場合は引き、短い場合は押す。
- 減衰: `Fdamping=-damping*dot(vOwnerPoint-vTargetPoint, direction)`。線速度だけでなく`angularVelocity x anchorOffset`を含む取付点速度を使う。
- Torque: 力を所有者と接続先のAnchorへ`AddForceAtPosition`するため、Anchorが重心から離れていれば回転も発生する。
- 反作用: `接続先へ反作用`が有効で、接続先もDynamic Rigidbodyなら同じ力を逆向きに加える。World固定点とKinematic Bodyには反作用を加えない。
- 初期値: World固定点、自然長1 m、ばね定数50 N/m、減衰5 Ns/m、Force上限100000 N、反作用あり。
- 最小手順:
  1. 動かす物体へRigidbody、Collider、SpringForceを追加する。
  2. World固定点を設定するか、接続先GameObjectをHierarchyから指定する。
  3. 両側のローカルAnchorと自然長を設定する。
  4. ばね定数を上げ、振動が残る場合は減衰を上げる。
- 使い分け: SpringJointは距離制約として分離を防ぐ。SpringForceは切断可能なロープ、吸着、サスペンション補助など、力だけを加えて他の運動を拘束しない用途に使う。
- 注意: 非常に大きなばね定数はFixed Time Stepに対して不安定になる。Force上限、減衰、物体Massを同時に調整する。

### RopeConstraint

- 目的: 最大長を超えた時だけ張力を伝えるロープ、ワイヤー、グラップリングフック、係留索を作る。縮んだ時は押し返さない。
- 追加場所: `コンポーネントを追加 > 3D物理 > ロープ拘束`。
- 必要条件: 所有者にDynamic RigidbodyとCollider。接続先はWorld固定点、Transform、Dynamic Rigidbodyのいずれでもよい。
- 物理: `tension=max(stiffness*(length-maximumLength)+damping*separationSpeed,0)`。所有者と接続先の取付点速度には角速度を含める。
- 破断: `破断張力`を超えると実行状態が破断になり、以後の張力を止める。0なら破断しない。
- 実行状態: Inspectorには現在長、現在張力、接続・解除・破断を表示する。Scene Viewの接続表示では通常を水色、破断を赤で描く。
- C++ Script: `RopeConstraint::Attach`、`AttachToWorld`、`Detach`、`SetLength`、`Repair`、`GetState`を使う。入力キーはComponentへ固定せず、InputまたはInput Actionからこれらを呼ぶ。
- ゲーム用途: Eで掴む・離す、ウインチで巻き取る、破断後に修復する、AIがフックを射出する、Timeline Eventから係留を解除する処理を同じAPIで作れる。
- 注意: AttachはSceneに存在するRopeConstraintの設定を切り替える。Play中にComponentそのものを新規生成するAPIではない。

### HookPoint（互換名: WireConnectable）

- 目的: Hook同士だけをWire接続できるよう、選択点と物理Bodyを分離する。
- 追加場所: `コンポーネントを追加 > 3D物理 > フックポイント`。
- 必要条件: Hookの見た目を持つRendererと選択用Collider。子Hookでは`力を伝えるRigidbody`へ親物体を指定する。
- 設定: 選択可能、最大接続本数、破断強度、カテゴリ、固定ローカルAnchor、親Rigidbody、通常・照準中・選択中・接続中の色、発光倍率。
- 選択: `Physics::FindBestHook`へ選択距離と選択角度を渡す。壁に隠れたHookは候補にしない。
- 状態表示: `HookPoint::SetVisualState`へ`Normal`、`Targeted`、`Selected`、`Connected`を渡す。
- 注意: 敵やBOX本体へ自動追加しない。接続させたい場所に専用Hook GameObjectを配置する。
- Script: `GetOrAddComponent<HookPoint>()`で必要な接続点へ実行時追加できる。`GetWires()`はこのHookを端点とするActive・未破断Wireのみを返し、`CanConnect()`で接続枠を確認できる。
- 選択制限: 現行`FindBestHook`は照準候補検索であり、選択可能フラグや本数制限を自身では除外しない。ScriptでCanConnectを確認する。requireConnectable=trueのWire生成でも接続可否を検証する。
- 色の反映先: Hook自身のModelRenderer、なければSkinnedMeshRenderer。子・親Rendererの自動探索はしない。4状態の切替もScriptが担当する。

| Inspector設定 | Script用Fieldキー | 意味 |
| --- | --- | --- |
| 選択可能 | `wireConnectableAllowSelection` | Bool。falseならCanConnectとHook必須のWire生成を拒否。照準表示から自動除外されるとは限らない。 |
| 最大接続本数 | `wireConnectableMaximumConnections` | Int。0以下は無制限。Active・未破断Wireを数える。 |
| 破断強度 N | `wireConnectableStrength` | Float。正値ならWire生成時に破断閾値へ反映。Wireと両端Hookの正の閾値のうち最小を採用。 |
| カテゴリ | `wireConnectableCategory` | Int。ゲーム側分類用で、自動的な接続ルールは付かない。 |
| 命中点をAnchorに使用 | `wireConnectableUseHitPoint` | Bool。サンプルScriptがAnchor決定に使用。固定フック仕様ではfalseにする。Wire::Createは渡されたAnchorを使う。 |
| 固定ローカルAnchor | `wireConnectableLocalAnchor` | Vector3。Hookのローカル接続点。 |
| 力を伝えるRigidbody | `wireConnectablePhysicsBodyGameObjectId` | GameObject参照。-1は自身。子Hookでは親などの物理Bodyを明示指定する。 |
| 通常色／照準中色／選択中色／接続中色 | `wireConnectableNormalColor` / `wireConnectableTargetedColor` / `wireConnectableSelectedColor` / `wireConnectableConnectedColor` | Vector3のRGB。SetVisualStateから適用する。 |
| 発光倍率 | `wireConnectableEmissionStrength` | Float。SetVisualState時のRenderer発光強度。 |

これらの設定はScene保存・読込の対象。Scriptからは内部名`"WireConnectable"`と上記Fieldキーを使い、Bool/Int/Float/Vector3/GameObjectに対応したComponentアクセサで操作する。物理Body参照は階層複製時の参照ID再割り当て対象でもある。

### WireRenderer

- 目的: 実行時に生成した複数WireをGame Viewへ表示する。
- 追加場所: `コンポーネントを追加 > 描画・レンダリング > ワイヤーレンダラー`。
- 設定: 投影幅計算用の3D半径、最小画面線幅、通常色、高張力色、破断色、発光、不透明度、たるみ係数、長手方向の分割数、表示。断面分割数も保存されるが描画では未使用。
- 状態表現: 張力比に応じて通常色から高張力色へ補間し、破断時は破断色を使う。ワールド半径を投影して距離に応じた太さにし、暗い外周とハイライトで丸みを表す。
- 遮蔽: Wire各区間の中点へCameraからPhysics Raycastし、手前にColliderがあれば区間全体を非表示にする。ピクセル単位の深度判定ではなく近似。
- 自動追加: Player Scriptから`GetOrAddComponent<WireRenderer>()`を呼べる。未追加時もEngine既定の水色Wireを描画する。

| Inspector設定 | Script用Fieldキー | 現行の効果 |
| --- | --- | --- |
| 表示 | `wireRendererVisible` | Bool。有効なWireRendererのfalseで非表示。Componentの無効化・削除は既定描画へ戻るため、非表示指定にはこのFieldを使う。 |
| 線幅 px | `wireRendererWidth` | Float。投影直径の下限となる画面幅。遠方でもこれ以下には細くならない。 |
| 3D半径 m | `wireRendererWorldRadius` | Float。距離に応じた投影直径を求める。実際の円筒メッシュ半径ではない。 |
| 断面分割数 | `wireRendererRadialSegments` | Int。保存・Inspector表示のみ。現在変更しても描画は変わらない。 |
| 発光倍率 | `wireRendererEmissionStrength` | Float。太い半透明線を重ねる疑似Glow。3D Materialの発光やBloom連携ではない。 |
| 通常色／高張力色／破断色 | `wireRendererColor` / `wireRendererTensionColor` / `wireRendererBrokenColor` | Vector3のRGB。張力比で補間し、破断中は破断色。 |
| 不透明度 | `wireRendererAlpha` | Float。0～1にClamp。 |
| たるみ量 m | `wireRendererSlackSag` | Float。現行計算では余長に掛ける係数。中央の下げ幅は`余長 × 係数`で、表示名のような固定m値ではない。 |
| 分割数 | `wireRendererSegmentCount` | Int。長手方向の線分数。描画時2～64にClamp。遮蔽Raycast数にも影響する。 |

設定元はWire生成時の`rendererSettingsGameObjectId`、負値なら`ownerGameObjectId`。そこにActiveなWireRendererがなければ既定設定で描く。設定はSceneへ保存されるが、Runtime Wire HandleそのものをSceneへ保存する機能ではない。

#### 現行描画の境界

`EditorGameViewManager.cpp::DrawGameRuntimeWires`はPlay中のGame ViewへWorld座標を投影し、`ImDrawList::AddLine`で外周・本体・ハイライト・Glowを重ねる。たるみは余長から作る下向きの放物線で、縄の各節点を物理シミュレーションしているわけではない。

- 立体チューブ／リボンメッシュ、GPU深度バッファによる遮蔽、縄Material・Textureは未実装。
- Colliderのない描画物は遮蔽に使えない。区間中点方式なので、細い障害物や部分遮蔽では欠け・飛び出しがあり得る。
- 縄自体が障害物に当たって曲がる／巻き付く物理はない。描画上の遮蔽は物理的な接触を作らない。
- 3Dライト、影、反射への対応をこの線描画に期待しない。丸みと発光は画面上の表現。
- 今回の記述はコード照合によるもの。Play時の見た目・遮蔽精度・性能は未検証。

### Suspension

- 目的: Raycast接地とばね・減衰Forceで車輪、着陸脚、ホバー脚を支える。
- 追加場所: `コンポーネントを追加 > 3D物理 > サスペンション`。
- 必要条件: 同じGameObjectにDynamic RigidbodyとCollider。接地先にもColliderが必要。
- 設定: ローカル取付点、ローカル接地方向、自然長、最大伸長、車輪半径、ばね定数、減衰、Force上限を指定する。
- 物理: 所有者自身の全Colliderを除外してRaycastし、`compression=restLength-currentLength`と取付点の相対速度からForceを計算する。
- 接地法線: OFFはサスペンション軸の逆方向へ押す。ONは斜面法線へ押し、斜面上で横滑りしにくい支持方向を作る。
- 反作用: 接地先がDynamic Rigidbodyなら同じForceを逆向きに加えられる。静的地面とKinematic Bodyには反作用を加えない。
- デバッグ: Inspectorに接地状態と現在長を表示する。Scene Viewの接続表示では非接地を灰、接地を緑で表示する。

### UprightStabilizer

- 目的: 車両、船、ホバー機、キャラクターのローカル上方向を目標World上方向へ戻し、自然な転倒復元を作る。
- 追加場所: `コンポーネントを追加 > 3D物理 > 姿勢安定化`。
- 必要条件: 同じGameObjectにDynamic RigidbodyとCollider。
- 物理: 現在Upと目標Upの外積・角度から復元Torqueを作り、目標Up周りのYawを除いた角速度へ減衰Torqueを加える。
- 設定: ローカル上方向、目標World上方向、姿勢ばね、角速度減衰、Torque上限を指定する。
- ゲーム用途: 船の横揺れ復元、車体の横転抑制、ホバー機の自動水平、重力方向が変わるSceneの姿勢制御。
- デバッグ: Scene Viewの力方向表示で現在Upを橙、目標Upを緑の矢印として比較できる。
- 注意: 回転を直接上書きしないため衝突や波のTorqueは残る。強すぎるばねは物理的な揺れを消すため、MassとFixed Time Stepに合わせて調整する。

### ElectromagneticBody

- 目的: Rigidbodyへ電荷と磁気双極子モーメントを与え、Scene内のElectromagneticFieldから力とTorqueを受けられるようにする。
- 追加場所: `コンポーネントを追加 > 3D物理 > 電磁気ボディ`。
- 必要条件: 同じGameObjectにDynamic RigidbodyとCollider。Scene側に1つ以上のElectromagneticField。
- 電荷: 正負を含むCoulomb単位で指定する。0なら電気力とLorentz力を受けない。
- 磁気Moment: Local座標で指定し、GameObject回転を反映してWorldへ変換する。0なら磁気Torqueを受けない。
- 安全設定: Force上限とTorque上限は、それぞれ合成後のベクトル長を制限する。0以下なら制限しない。
- 初期値: 電荷0 C、磁気Moment 0、Force/Torque上限100000。

### ElectromagneticField

- 目的: ElectromagneticBodyへ一様電場・磁場、または点電荷による逆二乗電場を供給する。
- 追加場所: `コンポーネントを追加 > 3D物理 > 電磁場`。
- 一様場: `electricField`をN/C、`magneticField`をTeslaで指定する。影響半径0ならScene全域、正値なら中心から半径端へ二次減衰する。
- 点電荷: `E=k*sourceCharge/r^2`。正電荷なら外向き、負電荷なら内向き。最小計算距離で中心特異点を避け、影響半径端では連続的に0へ減衰する。
- Coulomb力: `Fe=qE`。複数Fieldの電場を合成してからBody電荷を掛ける。
- Lorentz力: `Fm=q(v x B)`。速度と磁場の両方へ垂直に働くため仕事をせず、軌道を曲げる。
- 磁気Torque: `T=m x B`。磁気Momentを磁場方向へそろえる回転を起こす。
- 初期値: 一様場、E=0、B=0、源電荷0、Coulomb定数8.98755179e9、最小距離0.1 m、影響半径0。
- 最小手順:
  1. 場を置くGameObjectへElectromagneticFieldを追加する。
  2. 一様電場・磁場、または点電荷と最小距離を設定する。
  3. 対象へRigidbody、Collider、ElectromagneticBodyを追加し、電荷または磁気Momentを設定する。
  4. 電場だけ、磁場と初速、磁気Momentと磁場を分けて確認する。
- 制限: 電磁波、誘導電流、Maxwell方程式の時間発展、磁場勾配による双極子並進力は解かない。剛体ゲーム物理向けの準静的一様場・点電荷モデルである。

### 3D物理デバッグ表示

- 到達方法: Scene View左側の`物理`ボタン。GameObjectを選択していない場合はInspectorの`物理設定 > デバッグ表示`からも同じ値を編集できる。
- Collider: Box、Sphere、Capsule、Mesh、Auto Convex、Terrain、Wheel、Character Controllerを表示する。同じGameObjectに複数Colliderがある場合も全て表示する。通常は青、Triggerは緑、選択中は黄。
- 速度: Rigidbodyの`velocity`を水色、`angularVelocity`を紫の矢印で表示する。`ベクトル倍率`は方向を変えず表示長だけを調整する。
- 力と場: Scene重力、ConstantForce、Aerodynamicsの基礎風、WindZone、GravityField、RotatingFrame、FluidVolumeの流れ、ElectromagneticField、Joint軸を色分けした矢印で表示する。
- 影響範囲: WindZone、GravityField、RotatingFrame、ElectromagneticFieldは正値の影響半径を3軸円で表示する。FluidVolumeとBuoyancyは回転・Scale込みの箱、Aerodynamicsは圧力中心を表示する。
- 接続: SpringForceは両Anchorを点と線で表示し、Jointは所有者と接続先GameObjectを線で結ぶ。
- 接触: Play中にJoltの直近固定更新から接触点と法線を表示する。高FPSで固定更新がない描画フレームは最後の固定更新結果を保持し、点滅を避ける。
- Cast: Play中に実行されたRaycast、SphereCast、CapsuleCastを1フレーム最大256件まで記録する。Rayは黄、Sphereは水色、Capsuleは紫、命中点と法線は赤。
- 絞り込み: `選択中だけ表示`を有効にするとGameObject単位のCollider、速度、力、場、接続を選択対象へ絞る。接触は接触ペアのどちらかが選択中の場合だけ表示する。
- 保存: 表示種類と倍率はSceneのPhysicsSettingsへ保存する。旧Sceneには追加列がないため、読み込み時は既定値を使う。

### TerrainCollider

- 目的: Terrain の当たり判定を作る。
- 使う場面: 地面、山、広いフィールド。
- 必要条件: Terrain。
- 注意: 3D物理と地形カテゴリの両方に出る。

### WheelCollider

- 目的: 車輪向けの物理判定を作る。
- 使う場面: 車、バイク、タイヤ。
- 必要条件: Rigidbody。
- Play時の動作: Joltへ横向き円柱Shapeとして登録し、Rigidbodyの重力、摩擦、反発、角速度を適用する。
- 主な設定: 中心、半径、幅、Trigger、摩擦、反発、Physics Layer、Contact Event。
- 注意: 車両のサスペンションや駆動制御は別ComponentまたはC++ Scriptで組み立てる。

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
- Play時の動作: Fixed、Hinge、Spring、Characterは対応するJolt Constraintへ接続する。ConfigurableJointは位置XYZと回転XYZをJolt SixDOF Constraintで固定または制限する。
- ConfigurableJoint: Freeze Position/Rotation、最小/最大移動、最小/最大角度、ばね周波数、ばね減衰を設定する。

## 2D物理

### 2D Collider / Rigidbody

- 対象: RigidBody2D、BoxCollider2D、CircleCollider2D、CapsuleCollider2D、PolygonCollider2D、EdgeCollider2D、CompositeCollider2D、TilemapCollider2D、CustomCollider2D。
- 目的: 2D ゲーム用の物理と当たり判定。
- 使う場面: 横スクロール、トップビュー2D、Tilemap。
- 注意: 2D物理は現行エンジンの実装対象外である。既存Scene互換のComponent型と保存値は残すが、Play時の2D物理動作は保証しない。

### 2D Joint / Effector

- 対象: DistanceJoint2D、HingeJoint2D、SpringJoint2D、FixedJoint2D、SliderJoint2D、WheelJoint2D、PlatformEffector2D、SurfaceEffector2D、AreaEffector2D、PointEffector2D、BuoyancyEffector2D。
- 目的: 2D 物理の制約や特殊効果。
- 使う場面: 片方向床、水、風、吸引、2D ギミック。
- 注意: 2D物理は現行エンジンの実装対象外であり、Component型は既存Scene互換用である。

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
- AvatarMask: Maskアセットへ1行1Bone名を記述する。名前末尾の`*`は前方一致で、列挙されていないBoneはFBX初期姿勢へ戻す。
- PlayableDirector: Animationと同じClip再生経路を使用し、再生時間と速度を制御する。
- 注意: Humanoid Retarget、IK、Avatar編集画面は別機能であり、AvatarMaskのBone選択とは区別する。

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
- C++ Script: `BindAction`で受信し、送信は`GameObject::InvokeAction`を使う。
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
- C++ Script: `BindAction`、`EditorScriptInputActionContext`、`GameObject::InvokeAction`。
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
  2. Inspectorの`Splineを作成して接続`を押す。対象位置から前方へ4制御点のSplineが作成され、自動接続された状態でSpline Editorが開く。
  3. 作成されたRail Pathの子`Point 00`以降をScene Gizmo、位置入力、上面XZ / 側面ZY Canvasで動かす。
  4. 必要なら`制御点を追加`を押す。最低2点必要。
  5. Inspectorで速度、加速度、減速度、開始位置、向きの先読みを設定する。
  6. `レール内移動`で左右範囲X、上下範囲Y、開始オフセット、移動速度を設定する。範囲はRail中心から片側までの距離である。
  7. PlayerInputを使う場合は`PlayerInputから移動入力`をONにし、同じGameObjectのPlayerInputでAction MapとVector2 Actionを用意する。
  8. 物理挙動が必要なら`移動方式`を`Dynamic Rigidbody 物理追従`へ変え、同じGameObjectへDynamic RigidbodyとColliderを追加する。
  9. Ocean浮力と併用する場合は`浮力併用プリセット`を押す。X/Z位置とY回転だけをRailが制御し、Y位置とX/Z回転はBuoyancyへ任せる。
  10. Loop、進行方向へ回転、滑らかな曲線、開始停止、逆方向、終端停止を設定する。
  11. Play中はSpline Editorの進行率Slider、停止/再開、順方向/逆方向でPreviewする。
- 初期値: 速度8、開始位置0、先読み1、左右範囲5、上下範囲3、レール内移動速度8、Loop OFF、進行方向へ回転ON、滑らかな曲線ON、終端停止ON。
- Scene View表示: Rail Pathを線で表示する。選択中Pathは橙、非選択Pathは水色、制御点はMarkerと番号、進行方向は矢印で表示する。レール内左右範囲は水色線、上下範囲は緑線で表示する。
- Spline Editor表示: Runtimeと同じCatmull-Rom曲線を2D CanvasへPreviewする。上面XZと側面ZYを切り替えて、横移動と高さを別々に編集できる。
- 制御点追加: `選択点の次へ追加`で選択点の直後へ挿入する。次Pointがあれば中間へ、最後なら進行方向へ外挿した位置へ置く。未選択時は末尾へ追加する。
- 制御点同期: Canvas Drag終了時にSceneへ同期し、Inspector、Scene View、Rail Sampleを更新する。Hierarchy順が移動順である。
- Inspector警告: Rail Path未設定、制御点2点未満、Dynamic Rigidbody追従なのにRigidbody/Collider不足の場合は設定不足として説明する。
- Inspectorプリセット: `標準移動`、`カメラ経路`、`物理乗物`、`浮力併用`を説明する。プリセットは速度、向き、追従方式、追従軸、ばね/減衰などをまとめて設定する補助であり、ゲームルールは含めない。
- 物理追従: Joltの固定更新直前に位置と速度のPD制御力、向きと角速度のPD制御トルクを加える。位置/回転の追従軸、ばね、減衰、最大加速度をInspectorで調整できる。
- C++ Script: `RailFollower`で停止、再開、速度、逆方向、進行率Jump、Rail切替、レール内移動入力・オフセット、位置・方向・長さ・終端通知を操作する。
- 制限: RailMovementは移動だけを担当する。攻撃、敵判定、Wave、Camera、ゴールなどのゲームルールは持たない。

### Health

- 目的: 体力、耐久値、シールドなどに使う汎用の現在値と最大値を保持する。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > 体力`。
- 初期値: 最大100、Play開始時の現在値100。
- Inspector: 最大値を編集し、現在値は実行中表示として確認する。
- 連携: ThresholdStateのHealth比率Source、UIValueBindingの現在値 / 比率Sourceとして使用する。
- 制限: ダメージ種別、無敵時間、死亡、ドロップなどのゲーム固有ルールはHealth自身に含めない。

### WaveSpawner

- 目的: 1つの敵Templateを参照するObjectPoolから指定数を生成し、開始条件、生成間隔、編隊配置、全生成・全撃破通知をWave単位で管理する。攻撃や移動のルールはTemplate側Componentへ分離する。
- 追加場所: `コンポーネントを追加 > ゲームプレイ > ウェーブ生成`。Gameplay ToolsのWave作成は、選択Objectから`Wave Template`、`Wave Pool`、`Wave`を自動作成する。
- 作成手順:
  1. 敵1体のGameObjectへModel、Collider、Health、移動・攻撃ComponentまたはC++ Scriptを設定する。
  2. ObjectPoolを持つGameObjectを作り、敵をTemplateへ指定し、遅延生成容量を同時出現数以上にする。未使用枠はGameObject化されず、物理Templateも初回貸出時に登録される。
  3. 空のGameObjectへWaveSpawnerを追加し、生成元を`ObjectPool 生成`、ObjectPool、生成基準位置、生成数を設定する。
  4. 編隊を同一点、横列、V字、円、グリッドから選び、編隊間隔を設定する。グリッドだけ列数も設定する。
  5. 開始条件をPlay開始またはRailFollower進行率から選ぶ。Rail開始では進行率Sourceと0～1の開始進行率を設定する。
  6. 生成間隔と完了条件を設定する。`全生成`は最後の貸出直後、`全撃破・全返却`はHealthが0、非Active、またはPool返却になった後に完了する。
  7. 必要な場合だけAction対象、開始、各生成、完了条件、全撃破Actionを設定する。
- 初期値: ObjectPool生成、生成数5、横列、間隔4、Play開始、生成間隔0、完了条件は全撃破・全返却。Action名は`OnWaveStarted`、`OnWaveSpawned`、`OnWaveCompleted`、`OnWaveAllDefeated`。
- Play 時: 間隔0なら空きPool Itemを同じFrameで必要数まで貸し出す。Pool不足時は無限Loopせず、次Frame以降に空きができるまで再試行する。
- Action Payload: 開始は`None`、各生成は`GameObject`、完了条件と全撃破は`Int`の生成総数。GameObject IDをfloatへ変換しない。
- RailMovement連携: Pool ItemにRailMovementがある場合、再貸出時に開始進行率、開始Offset、Reverse、Pauseを初期化する。編隊のX/YはRailの横・縦Offsetへ加算する。
- Pool再利用: WaveはGameObject IDだけでなく貸出世代を追跡する。同じItemが別Waveへ再貸出されても、元Waveの全撃破判定へ混入しない。
- 旧Scene互換: 生成元が`事前配置した子`、またはObjectPoolが未設定・無効なら直下の子をHierarchy順に有効化する。新規Waveでは互換方式を標準手順にしない。

### 旧RailShooter互換型

- `LegacyRailShooterEnemy`、`LegacyRailShooterShip`、`LegacyRailShooterEnemyMotion`、`LegacyRailShooterStage`は旧Sceneを読み込むための互換スロットである。
- Add Componentの通常機能として使用手順を作らない。
- Engine Runtimeはこれらのゲームルールを実行しない。新規SceneではRailMovement、WaveSpawner、TimelineEvent、ThresholdState、Health、UIValueBinding、C++ Scriptを必要な組み合わせで使う。

### 画面照準

- 追加場所: `コンポーネントを追加 > 入力・イベント > 画面照準`。
- 目的: Game View内のマウス位置、またはPlayerInputのVector2 Actionを0～1の画面座標として保持する。
- UI連動: `照準UI`へRectTransformを持つGameObjectを設定すると、1280x720基準の位置へ自動反映する。
- Ray取得: レイ射撃、弾発射、またはC++ Scriptの`Physics::GetAimRay`が同じ照準値を使う。
- 制限: 命中補正、ロックオン、敵選択は扱わない。必要ならC++ ScriptでRayを補正する。

### レイ射撃 / 弾発射

- レイ射撃: 射程、ダメージ、発射間隔、単発・連射、発射・命中・非命中Actionを設定する。
- 弾発射: ObjectPool、発射位置、速度、半径、寿命、ダメージ、発射間隔を設定する。
- すり抜け対策: 弾発射は前Frame位置から移動距離分のRaycastまたはSphereCastを毎Frame行う。
- 入力を使わない場合: Action名を空にし、C++ Scriptの`Weapon::FireHitscan`または`Weapon::FireProjectile`から発射する。
- 責務外: 残弾、リロード、誘導、属性、スコアは固定実装しない。公開ActionとC++ Scriptを組み合わせる。

### ダメージ受信

- 必要Component: 同じGameObjectのHealth。
- 目的: 基礎ダメージへ倍率を掛け、無敵時間を適用し、被弾・死亡Actionを通知する。
- 死亡時に無効化: 通常ObjectはGameObjectと物理Bodyを無効化する。ObjectPool ItemはPoolへ返す。
- Pool再利用: 貸出時にHealth、無敵時間、死亡状態を初期化する。

### オブジェクトプール / プレハブ生成

- ObjectPool: Template GameObjectと遅延生成容量を設定する。Play開始時はTemplate一体だけを待機させ、残りは初回貸出時にGameObject、Collider、Rigidbody、C++ Scriptを登録する。
- PrefabSpawner: ObjectPool、生成位置、外部命令・Play開始・一定間隔のいずれかを設定する。
- C++ Script: `ObjectPool::Spawn`、`ObjectPool::Release`、`Spawner::Spawn`で直接操作できる。
- 物理Template: Play中の遅延生成と容量超過拡張に対応する。通常は同時出現数を遅延生成容量へ設定し、無制限な拡張による高水位メモリ増加を避ける。
- Prefab Assetとの関係: Project上の`.prefab`保存・生成機能とは別で、Runtime PoolはScene内Templateを使う。
- 初回貸出ヒッチ対策（Prewarm）: `CreatePoolItem()`/`PrewarmPool()`/`PrewarmAllPools()`によって、Play開始直後に遅延生成容量ぶんのItemをまとめて複製・登録できる。これを使わず初回`Spawn()`任せにすると、初撃破・初出現の瞬間に`DuplicateGameObject`とPhysics/Script登録が同一Frameへ集中し、体感できるヒッチ（コマ落ち）が発生する。敵Waveなど大量出現する構成ではPlay開始時に必ずPrewarmしておく。
- **既知の不具合と修正（重要）: Template非ActiveのままComplicateするとPool個体にBodyが生成されない。** `CreatePoolItem()`は`DuplicateGameObject`でTemplateを複製するが、複製処理は複製元Templateの**現在の`isActive`状態をそのままコピーする**。Templateを地下や画面外など待機用の位置に置く構成では、SimulationLOD等の距離ベース処理がTemplateを非Active（またはPhysics停止）扱いにすることがあり、その瞬間に複製が走ると複製されたPool個体（`_Pool_N`という名前が付く）も`isActive=false`で生成される。`EditorJoltPhysicsManager::RegisterRuntimeGameObject()`は`gameObject.isActive==false`のとき登録処理そのものを早期returnしてスキップするため、そのPool個体には**Physics Bodyが最初から一切作られない**。後から`SetItemActive(true)`で見た目上Activeへ戻して貸し出しても、`SetItemActive`が呼ぶ`SetGameObjectSimulationActive`は「既存Bodyの追加/除去」しかできず、存在しないBodyを新規作成する処理ではないため、そのPool個体は永久にRay/ShapeCastへ反応しない（弾がすり抜ける）。この状態は`SetActive`をいくら繰り返しても直らない——一度Body登録に失敗した個体は、Pool内に留まる限り物理的に「透明」のままになる。
  - 症状: Templateそのもの（Pool内の0番目、`_Pool_`接尾辞のない個体）にだけ攻撃が命中し、複製された他のPool個体には何度撃っても命中しない。
  - 確認手順: `EditorJoltPhysicsManager::GetBodyDiagnostics()`または診断用RuntimeLogの`PhysicsBodyFailureCount`/`LastPhysicsBodyFailure`システム項目で「非Activeで登録スキップ」ログが出ていないか確認する。
  - 修正: `CreatePoolItem()`内で複製直後に`duplicatedGameObject->isActive = true;`を強制し、Physics/Script登録より前に必ずActive状態を確定させる。直後に呼ばれる`SetItemActive(false)`（待機状態へ戻す処理）がこのActive状態を「本来の初期状態」として記録するため、後続の`SetItemActive(true)`（貸出時）でも正しくActiveへ戻る。
  - 根拠: `EditorObjectPoolManager.cpp` `CreatePoolItem()`、`EditorJoltPhysicsManager.cpp` `RegisterRuntimeGameObject()` / `AddGameObjectBody()`。関連: 「SimulationLOD」節の「ObjectPoolのTemplateと組み合わせる際の注意」。
- 診断のコツ: この種の「一部の個体だけ当たらない」不具合は、Console表示だけでは大量のログに埋もれて発見できない。ログ監視Windowで`Physics`カテゴリの`PhysicsBodyFailureCount`/`LastPhysicsBodyFailure`、および武器側の`LastProjectileNearestCandidateName`等をWatchへ登録し、命中しない個体名（`_Pool_N`）とBody有無を突き合わせて特定する。

### カメラブレンド / カメラシェイク

- Camera優先度: 有効なCameraまたはCinemachine Cameraのうち、Inspectorの優先度が最大のものをGame Viewへ使う。
- CameraBlend: 開始Camera、省略時の現在Camera、終了Camera、時間、Linear / SmoothStepを設定する。
- CameraShake: 位置振幅、回転振幅、周波数、時間を設定する。複数再生は加算する。
- C++ Script: `CameraEffects::PlayBlend`と`CameraEffects::PlayShake`から任意タイミングで再生する。

### レール分岐

- 目的: RailFollowerを別Rail Pathへ切り替える。敵、攻撃、ボス、ステージ条件は扱わない。
- 自動: 進行率が設定値を下から上へ横切った時に切り替える。
- 手動: 切替条件を`外部命令のみ`にして、C++ Scriptの`RailBranch::Trigger`から実行する。
- 設定: RailFollower、切替先Rail Path、進行率維持、一度だけ、切替後Action。

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
- 描画形状: FBX / OBJをParticle 1個の形として指定できる。未設定時は選択したBillboard方式の板ポリゴンをGPU Instancingする。
- Billboard方式: Camera Facing、Y軸固定、Velocity Facing、World XY固定。既定値はCamera Facing。
- View分離: Scene ViewとGame Viewは各描画PassのView MatrixからRight / Upを作る。同じFrameでも別Cameraの基底を流用しない。
- Play操作: Inspectorから`エフェクトを再生`、`新規発生を停止`、現在の生存数確認ができる。
- Effect Asset: `.effect`は共有設定を読み込み、`.efk` / `.efkefc`はEffekseer 1.70e DX12 Runtimeで再生する。

| 設定 | 内部Property | 型 / 初期値 | 動作 |
| --- | --- | --- | --- |
| 板の向き | `particleBillboardMode` / `BillboardMode` | int / 0 | 0=Camera Facing、1=Y軸固定、2=Velocity Facing、3=World XY固定。 |
| 速度方向の長さ | `particleBillboardStretch` / `BillboardStretch` | float / 1.0 | Velocity Facingだけで速度方向へ掛ける長さ倍率。0.01以上へ制限する。 |

Camera Facingは爆発、煙、円形の水しぶき等で常にCameraへ正対する。Y軸固定はWorld Upを維持し、縦煙、立ち上る炎、地上Markerの横倒れを防ぐ。Velocity Facingは速度をCamera Planeへ射影して板のUp軸へ使い、曳光弾、飛沫、Trail粒子を進行方向へ伸ばす。速度がほぼ0ならCamera FacingへFallbackする。World XY固定は旧描画互換または特定World Planeへ固定した表現に使う。

Particle自身のRotationは選択したBillboard Plane内の回転として適用する。FBX / OBJ指定時は`ParticleModel.VS.hlsl`の3D Transformを使うため、Billboard ModeとStretchを無視する。描画処理は向きの決定だけを行い、Spawn、Velocity、Lifetime等の更新処理を持たない。

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

## 汎用ワークフローComponent

### アクションシーケンス

- 表示名: `アクションシーケンス`
- 役割: 子GameObjectの`シーケンスステップ`をHierarchy順に実行する。
- 主な設定: Play開始時に再生、Loop。
- Runtime操作: Play、Pause、Stop、Signal、IsPlaying。
- 保存: Component設定と子StepはScene/Prefabへ保存する。
- 制限: ゲーム固有のBoss、敵Wave、会話を固定ノードとして持たない。Script ActionとSignalへ委譲する。

### シーケンスステップ

| Step | 設定 | 完了条件 |
| --- | --- | --- |
| Script Action | 対象GameObject、公開Action名 | ActionをQueueしたFrameで完了。 |
| 待機 | 秒数 | 経過時間が設定値以上。 |
| Active変更 | 対象、Active値 | GameObjectと物理Bodyを同じ状態へ変更して完了。 |
| Scene読込 | Scene Path、Additive | 非同期読込状態が終了して完了。 |
| 条件 | Active / 体力比率 / Rail進行率、比較、値 | 比較結果がtrueになるまで待機。 |
| Signal待機 | Signal名 | 同じSequenceへSignalが届くまで待機。 |

`並列Group=-1`は順次である。同じ0以上のGroupが連続している範囲だけを並列開始する。途中へ別Groupまたは-1を置くと前Groupの全Step完了後に次へ進む。

### 保存対象

- 表示名: `保存対象`
- 必須設定: Scene内で一意な保存Key。
- 保存項目: Transform、Active、Health、Rigidbody速度、Script公開変数。
- 復元: 現在Sceneで同じKeyを持つGameObjectへ適用する。
- 安全条件: 重複Keyを検出した場合はSlotを保存・復元しない。未知Version、破損数値、存在しないSlotはfalseを返す。
- Runtime値: `SaveSystem`へ登録したfloat/stringも同じSlotへ保存する。

### チェックポイント

- 表示名: `チェックポイント`
- 設定: Slot名、Play開始時Save、Play開始時Load、Save成功Action、Load成功Action。
- 手動操作: C++の`Checkpoint::Save`または`Checkpoint::Load`。
- 実行順: Slot処理成功後に公開ActionをQueueする。
- Additive: Runtime再構築で開始処理を二重実行しない。
- 注意: 保存対象を自動推測しない。保存対象Componentと一意Keyを明示する。

## Component詳細ページの完成条件

Componentページは「何となく使える説明」では足りない。
使用者がScene上で置き、Inspectorで設定し、Playして確認し、C++ Scriptから操作できるところまで書く。

### 全Component共通テンプレート

各Componentの詳細ページは、必ず次の形にする。

| 項目 | 書く内容 |
| --- | --- |
| 表示名 | Add ComponentやInspectorに出る日本語名。 |
| 内部型名 | Scene保存、C++ Script、RuntimePropertyで使う英語名。 |
| カテゴリ | Add Component Popup上の分類。複数カテゴリに出る場合は全部書く。 |
| 目的 | 何をするComponentか。ゲーム固有の意味を含めない。 |
| 使う場面 | 具体的な使用例。 |
| 追加手順 | メニュー、Hierarchy、Project Drag、Inspector Buttonの手順。 |
| 必要Component | 同じGameObjectに必要なもの、参照先に必要なものを分ける。 |
| 必要Asset | Texture、Model、Audio、Scene、InputActions、Prefabなど。 |
| Inspector項目 | 表示名、内部Field、型、単位、初期値、範囲、保存有無、Play中変更可否。 |
| Scene View表示 | Gizmo、線、範囲、警告、Debug表示の見え方。 |
| Game View表示 | 実際の画面に出るもの、出ないもの。 |
| Play時の動作 | Start、Update、FixedUpdate、Renderのどこで反映されるか。 |
| C++ Script連携 | Wrapper、Runtime API、Action名、RuntimeProperty名。 |
| 他Componentとの関係 | 併用、競合、優先順位。 |
| Prefab/Scene保存 | 保存される値、参照の扱い、Prefab外参照の制限。 |
| Build/Standalone | Buildに含めるAsset、実行時Path、Editor専用機能かどうか。 |
| トラブルシュート | 表示されない、動かない、重い、Actionが来ない時の確認順。 |
| 制限 | 未実装、近似、例外、保証しない動作。 |

### Inspector項目の書き方

Inspector項目は箇条書きだけで終わらせず、次の表を使う。

| 表示名 | 内部Field | 型 | 初期値 | 単位 | 保存 | Runtime | 説明 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 速度 | `railSpeed` | float | 8.0 | m/s相当 | する | 変更可 | RailMovementの目標速度。 |

Runtime列には、次のどれかを書く。

- 編集のみ: Play中に変えてもRuntimeへ反映されない。
- Play中反映: Inspector変更が次Frame以降に反映される。
- FixedUpdate反映: 物理固定更新で反映される。
- RuntimeProperty可: C++ Scriptから名前で変更できる。
- Action経由: Actionを受けた時だけ変わる。

### 参照Fieldの書き方

GameObject参照、Asset参照、Scene参照は、失敗時に何が起きるかを書く。

- 空の場合に自動検出するか。
- 自動検出の対象範囲はScene全体か、同じGameObjectか、子だけか。
- 参照先が削除された時に警告するか。
- Prefab化した時にPrefab外参照を保持できるか。
- Standalone BuildでAssetがCopyされない場合どうなるか。

### Action対応Componentの書き方

Actionを持つComponentは、Action名と値の表を必ず入れる。

| タイミング | 既定Action名 | `buttonValue` | 呼ばれる条件 | 失敗時 |
| --- | --- | --- | --- | --- |
| Wave開始 | `OnWaveStarted` | 1.0 | 開始条件を満たしたFrame | 対象なしなら何もしない。 |

Action対象はScript / MonoBehaviourが付いたGameObjectである。
Action名候補は新しいC++ Script Templateが公開する。
旧DLLやExportなしDLLは直接入力になる。

### Debug表示の書き方

Scene Viewに出る補助表示は、Game Viewの描画と分けて説明する。

- Colliderは形、Trigger色、選択色を書く。
- Railは線、制御点、方向、Offset範囲を書く。
- Physicsは速度、力、接触、Castを分けて書く。
- AudioはListener、Source範囲、Cone、Occlusion Rayを分けて書く。
- CameraはFrustum、Target、Blend状態を書く。
- NavigationはPath、Corner、Agent半径、Obstacle範囲を書く。
- UIはCanvas範囲、RectTransform、Raycast Targetを分けて書く。

Debug表示は編集補助であり、Game ViewやStandaloneに出るかどうかを必ず区別する。

### 競合を書くべきComponent

次のComponentは同じTransformや同じRuntime状態を触りやすいため、競合を必ず書く。

| Component | 競合しやすい相手 | 説明すること |
| --- | --- | --- |
| FreeTransform | Rigidbody、RailMovement、Animation | Transformを直接書くため物理やRailと競合する。 |
| RailMovement | Rigidbody、Buoyancy、Animation、ParentConstraint | 追従軸を分ける必要がある。 |
| Buoyancy | RailMovement、FluidVolume、Rigidbody重力 | Y位置とPitch/Rollを誰が決めるか。 |
| CameraBlend | Camera優先度、CinemachineCamera | Game Viewで採用されるCamera。 |
| AudioSource | AudioListener、ReverbZone、AudioFilter | 距離、方向、遮蔽、Busの合成順。 |
| Animation | Animator、Constraint、Script | 同じTransformやMaterialを誰が最後に書くか。 |
| UIValueBinding | ScriptでのText更新 | 同じTextへ二重に書かない。 |
| ObjectPool | PrefabSpawner、DamageReceiver | 無効化とPool返却のタイミング。 |

### レールシューティングを作る時のComponent確認表

レールシューティング専用Componentを増やすのではなく、次のComponent群を組み合わせる。

| 目的 | Component | C++ Script Template | 確認すること |
| --- | --- | --- | --- |
| 自動進行 | RailMovement | RailPlayer | Scene ViewにPath線、方向、Offset範囲が見えるか。 |
| 左右上下移動 | RailMovement + PlayerInput | RailPlayer | Script入力と自動入力を二重にしていないか。 |
| 照準 | ScreenAim + UI | PlayerController / RailPlayer | Game View座標とReticle UIが一致するか。 |
| 射撃 | HitscanWeapon / ProjectileEmitter | TurretController / EnemyController | Raycast、Pool、発射間隔、Action。 |
| 敵生成 | WaveSpawner / PrefabSpawner / ObjectPool | SpawnPoolController | 生成順、Pool数、Action値。 |
| 敵耐久 | Health + DamageReceiver | HealthDamageController | 死亡Action、Pool返却、無敵時間。 |
| 分岐 | RailBranch / TimelineEvent / ThresholdState | StageController | 条件はScript側か、Component設定か。 |
| 演出 | ActionSequence / CameraBlend / CameraShake | ActionEventController | Signal、並列Group、Camera優先度。 |
| 音 | AudioSource / ReverbZone / AudioFilter | AudioController | 3D距離、方向、遮蔽、残響。 |
| HUD | Canvas / Text / Slider / UIValueBinding | UiController | 値Source、桁数、解像度。 |
| 保存 | Saveable / Checkpoint | SaveCheckpointController | 一意Key、保存対象、Slot。 |

### 未実装を未実装と書くルール

Component型やInspector欄が存在していても、Runtimeに接続されていない場合は「設定のみ」と書く。
名前だけで実装済みと判断しない。

確認順は次の通り。

1. Add Componentに出るか。
2. Scene保存されるか。
3. Inspectorで値を変えられるか。
4. Play時にRuntimeへ渡っているか。
5. 描画、物理、Audio、Inputなど実処理へ接続されているか。
6. C++ Script APIまたはActionから操作できるか。
7. Standalone Buildで同じように動くか。

この7点のどれかが未確認なら、使用者向けドキュメントでは断定しない。

## 再利用Gameplay Component実装詳細

### 武器ロードアウト / 武器スロット

`武器ロードアウト`は子GameObjectに付けた複数の`武器スロット`をHierarchy順で収集し、選択、射撃、弾薬、Reload、Visual切替を管理する。発射判定そのものは`レイ射撃`または`弾発射`へ委譲するため、LoadoutへDamage、射程、誘導方式を重複設定しない。

| 項目 | 内容 |
| --- | --- |
| 選択Slot | 現在装備する子Slot番号。範囲外はRuntime開始時に補正する。 |
| 変更Action / Reload Action | 選択変更、Reload完了を任意Scriptへ通知する。 |
| Weapon Object | `レイ射撃`または`弾発射`を持つGameObject参照。 |
| Visual Object | 選択中だけActiveにする任意の表示Object。 |
| 現在弾数 / 最大弾数 | Magazine内の弾数。発射成功時だけ減る。 |
| 予備弾 | `-1`は無限。0以上ではReload時に消費する。 |
| Reload秒 / 自動Reload | Reload時間と、空Magazine時の自動開始を設定する。 |

親へ`武器ロードアウト`を追加し、Inspectorの「子武器Slotを追加」でSlotを増やす。入力は`LoadoutController` TemplateまたはC++ Scriptの`WeaponLoadout` APIから接続する。

### ターゲット選択

`ターゲット選択`は距離、前方角、Physics Layer、遮蔽、Team、選択方式から現在Targetを1つ決める。候補はCollider、Health、または`ターゲットポイント`を持つActive GameObjectである。

| 項目 | 内容 |
| --- | --- |
| 検索Layer | `-1`は全Layer。0以上は対象ColliderのPhysics Layerと一致させる。 |
| 最大距離 / 最大角度 | 基準ObjectのWorld位置と前方から候補を絞る。 |
| 基準Object | 未設定時はComponent所有者。Cameraや砲塔Pivotも指定できる。 |
| 遮蔽判定 | Raycastで候補またはその親以外が先に当たれば除外する。 |
| 選択方式 | 最短距離、照準中心、低HP、優先値。 |
| Team Filter | Target可能な全て、別Team、同じTeam、指定Team。 |
| Neutralを含む | Team未設定または負のTeam IDを候補へ含める。 |
| 現在Target ID | Play中にManagerが更新する読み取り用値。 |

旧Scene互換の初期値は「Target可能な全て」「Neutralを含む」である。敵味方判定を使うSceneでは、選択者と候補Rootへ`チーム`を追加してからFilterを「別Team」にする。

### ターゲットポイント / チーム / ターゲット追従

`ターゲットポイント`は大型Objectの部位、弱点、会話注視点、Camera LookAt位置を表す。子GameObjectとして配置し、Scene Viewに橙色の球と名前を表示する。優先値、注視半径、ローカル注視Offsetを設定できる。親にTeamがあれば継承する。

`チーム`はゲームルールを固定しない整数所属である。`Team ID < 0`はNeutral、`Target可能=false`は自動Target選択からの除外を意味する。Player/Enemyという固定Enumは持たず、ゲーム側がIDの意味を決める。

`ターゲット追従`は明示TargetまたはTargetSelectorの現在Targetへ向きを変え、Transform移動またはRigidbody Forceで前進する。TargetPointの場合はAim Offsetを含むWorld位置へ向かう。Damage、爆発、寿命はこのComponentの責務ではない。

### Runtime Property / プロパティ補間

Runtime Propertyは登録済みComponent Propertyを型付きで読み書きする共通経路である。`プロパティ補間`はそのFloatまたはVector3を時間補間する。Target関連の登録Propertyは次のとおり。

| Component | 型 | Property |
| --- | --- | --- |
| TargetPoint | Float | `Priority`, `Radius` |
| TargetPoint | Vector3 | `AimOffset` |
| Team | Int / Bool | `TeamId`, `Targetable` |
| TargetSelector | Int / Bool | `TeamFilter`, `SpecificTeamId`, `IncludeNeutral` |
| TargetSteering | Float | `MaximumSpeed`, `TurnSpeed` |

### Damage Context

`DamageReceiver`は最後に適用された`DamageContext`を対象ごとにRuntime保持する。ContextにはTarget、直接Source、Instigator、命中位置、法線、Impulse、基礎Damage、適用Damage、ゲーム側UserTagが含まれる。HitscanとProjectileはPhysics Hitから命中位置・法線を設定する。被弾Actionを受けたScriptは`Health::GetLastDamageContext`で詳細を取得し、被弾方向、Hit Marker、Score、部位破壊、Knockbackへ利用する。

## 未記載Componentの個別仕様

### NavMeshAgent

- 位置付け: 旧名称互換用。新規Sceneでは`NavigationAgent`を使う。
- Runtime: `NavMeshAgent`型そのものを移動Managerの主入口にしない。
- 移行: Speed、Acceleration、Radius、Height、Stopping Distance、Target参照を`NavigationAgent`へ写す。
- 確認: Add Componentに通常候補として出す場合は互換型であることをInspectorへ警告する。

### CanvasScaler

- 目的: 参照解像度に対するUI全体の拡縮基準を決める。
- 主設定: Scale Mode、Reference Resolution、Screen Match Mode、Match Width Or Height、Reference Pixels Per Unit。
- 必要: Canvas。複数Scalerを同一Canvas階層へ置かない。
- 確認: 16:9、4:3、Ultrawide、Window resizeでAnchorと文字サイズを確認する。

### GraphicRaycaster

- 目的: Pointer位置とUI GraphicのRaycast Targetを照合し、Button等へ入力を渡す。
- 必要: Canvas、EventSystem、Input Module。
- 主設定: Ignore Reversed Graphics、Blocking Objects、Blocking Mask。
- 失敗確認: Canvasの表示順、Raycast Target、透明Imageによる遮蔽、EventSystem重複、Game View Focus。

### ActionSequenceStep

- 所有: `ActionSequence`直下の子GameObjectとしてHierarchy順に評価される。
- Step種別: Script Action、Wait、Active変更、Scene読込、条件分岐、Signal待機。
- 並列: 同じ0以上のParallel Groupが連続するStepだけを同時開始する。負数は単独実行。
- 分岐: Active、Health比率、Rail進行率を比較し、True/Falseの子番号へ移る。
- 注意: Step自身へゲーム固有のBoss/Wave意味を追加せず、Action名でScriptへ委譲する。

### TorsionSpring

- 必要: 所有者へDynamic Rigidbody。Targetを使う場合はTarget側にもDynamic Rigidbodyを推奨する。
- 設定: Target、目標相対回転、回転ばね定数、回転減衰、最大Torque、反作用。
- Runtime: 角度誤差へHooke則Torque、相対角速度へ減衰Torqueを加える。
- 用途: 扉の復元、砲塔の柔らかい追従、車体のAnti-roll、揺れる装備。
- Debug: 接続線、目標軸、Torqueベクトルを物理Debugで確認する。

### WeaponLoadoutSlot

- 所有: `WeaponLoadout`直下の子として使う。Hierarchy順がSlot番号になる。
- 設定: 表示名、Weapon Object、Visual Object、現在弾、予備弾、最大弾、Reload秒、自動Reload。
- Runtime: 選択中VisualだけActiveにし、Weapon発射成功時だけMagazineを減らす。
- 失敗確認: 親Loadout、Weapon参照、Weapon Component、弾数、Reload中、Hierarchy順。

### MovementModifier

- 目的: Rail等が作った基準Transformへ、ゲームルールを持たない位置・回転Offsetを追加する。
- 設定: Local Position/Rotation Offset、許可軸Mask、入力範囲、入力追従速度、PlayerInput、Action Map、Vector2 Action。
- 用途: レール内移動、Formation Offset、Camera Offset、乗り物座席。
- 注意: RailMovement自身のOffset入力と同時使用すると二重移動になる。

### PropertyTween

- 対象: Runtime Property Registryへ登録済みのFloatまたはVector3。
- 設定: Target、Component名、Property名、開始値、終了値、Duration、Curve、Loop、Play On Start、完了Action。
- Runtime: Linear、SmoothStep、Ease In、Ease Outで補間し、完了時だけActionを送る。
- 失敗確認: Component/Propertyの大文字小文字、値型、対象参照、Registry登録、Duration。

### ActionRelay / ActionRelayTarget

- `ActionRelay`: `Relay()`またはPlay開始を入口に、直下のTargetへ通知を分配する。
- `ActionRelayTarget`: Action対象、Action名、有効フラグを1件分保持する。
- 順序: Hierarchy順。無効Targetや空Action名は飛ばす。
- 用途: 1回のStage開始からBGM、Camera、Effect、Ocean Tweenを同時開始する。
- 注意: 条件分岐や時間待機が必要ならActionSequenceを使う。

### Thruster

- 必要: Dynamic Rigidbody。
- 設定: 推進方向、ローカル作用点、最大推進力、Throttle、Local/World方向。
- Runtime: `force * throttle`を作用点へ加えるため、重心からずれればTorqueも発生する。
- 用途: 船、飛行機、宇宙船、Hover vehicle、逆噴射。
- 失敗確認: Rigidbody Motion Type、質量、方向正規化、作用点、Throttle、最大Force。

### PulleyConstraint

- 必要: 所有者とTargetのDynamic Rigidbody。
- 設定: 両側Anchor、両支持点、全長、比率、剛性、減衰、最大張力、破断張力。
- Runtime: `lengthA + ratio * lengthB`の超過へ張力を加え、両Bodyへ反作用する。
- 用途: 昇降機、釣り合い重り、滑車扉、クレーン。
- Debug: 4点を結ぶ線、現在長、張力、破断状態を表示する。

### PhysicsServo

- 必要: 所有者へDynamic Rigidbody。
- 設定: TargetまたはWorld目標、位置/回転目標、位置PD、回転PD、Force/Torque上限、反作用。
- Runtime: Transformを直接上書きせず、位置誤差と速度差からForce、角度誤差と角速度差からTorqueを計算する。
- 用途: 物理追従台、船体姿勢補助、ロボット関節、物理Camera rig。
- 注意: Gain過大、質量差、低いFixed timestepで振動する。

### VortexField

- 設定: 回転軸、半径、角速度、中心流入速度、軸流速度、速度結合率、最大加速度。
- Runtime: 影響範囲内のDynamic Rigidbodyを目標流速へ近づける。
- 用途: 渦潮、竜巻、吸気流、回転する宇宙ステーション内部。
- Debug: 影響球、軸、接線方向、対象への加速度を表示する。

### PressureField

- 設定: 圧力Pa、半径、距離減衰指数、最大Force。
- Runtime: Collider投影面積と中心方向からForceを求める。負圧は吸引になる。
- 用途: 爆風、衝撃波、吸引Field、水圧差。
- 注意: Damageは与えない。Healthへ影響させる場合はTrigger/ScriptからDamageContextを送る。

## 既存Componentの個別差分と実行確認

この章は、以前カテゴリ単位でまとめていたComponentを、利用者がAdd Componentで選ぶ単位まで分解した索引である。状態欄の「Runtime接続」はManagerまたは描画・物理経路が存在することを示し、Standaloneでの動作保証までは意味しない。「互換データ」はScene読込互換のため型と設定を保持するが、現行の主実装として選ばない。

### UI描画Component

| Component | 役割 | 主設定 | 必須・組み合わせ | 実行確認 |
| --- | --- | --- | --- | --- |
| Image | Texture/SpriteをRectへ描画する。 | Source、Color、Preserve Aspect、Raycast Target。 | RectTransform、Canvas。 | Alpha、Canvas順、Texture SRV、Mask範囲を見る。 |
| RawImage | UV範囲を指定してTextureをそのまま描画する。 | Texture、UV Rect、Color。 | RectTransform、Canvas。 | Sprite Atlasを前提にせず、UVとSamplerを確認する。 |
| Text | 旧Text描画。既存Scene互換向け。 | Text、Font、Size、Alignment、Color。 | RectTransform、Canvas。 | 新規UIはTextMeshProUGUIを優先する。 |
| TextMeshProUGUI | SDF Fontで拡縮に強い文字を描画する。 | Font Asset、Size、Spacing、Outline、Wrapping。 | RectTransform、Canvas、Font Asset。 | Font Atlas欠落、Scale、AA、Outline幅を確認する。 |
| CanvasRenderer | UI頂点、色、Alpha、MaterialをCanvasへ渡す低水準描画部。 | Material、Cull、Inherited Alpha。 | Canvas配下のGraphic。 | 単独追加で絵は出ない。Graphic側を確認する。 |
| Canvas | Screen SpaceまたはWorld Space UIの描画Root。 | Render Mode、Sort Order、Target Camera、Reference Plane。 | UI子Object。 | Game View解像度、Camera参照、複数Canvas順を確認する。 |
| CanvasScaler | 解像度差に対するCanvas拡縮を決める。 | Reference Resolution、Match、Scale Mode。 | Canvas。 | 16:9以外とWindow resizeを必ず確認する。 |
| GraphicRaycaster | PointerとUI Graphicを照合する。 | Blocking、Layer Mask、Reversed Graphic。 | Canvas、EventSystem、Input Module。 | 透明ImageのRaycast Targetによる遮蔽を見る。 |
| Mask | 親Graphicの形状をStencilとして子UIを切り抜く。 | Show Mask Graphic。 | Image等のGraphic、Canvas。 | Stencil階層とMaterial対応を確認する。 |
| RectMask2D | 軸平行Rectで子UIを切り抜く。 | Padding、Softness。 | RectTransform、Canvas。 | 回転Maskには使わず、ScrollRectで優先する。 |

### UI入力Component

| Component | 入力と出力 | 主設定 | 失敗時の確認順 |
| --- | --- | --- | --- |
| Button | ClickでActionを1回通知する。 | Interactable、Normal/Hover/Pressed色、Action対象・名前。 | EventSystem、Raycaster、Raycast Target、重なり、Action名。 |
| Toggle | bool状態を切り替え、変更Actionへ0/1値を渡す。 | Is On、Group、Graphic、Action。 | Group参照、同FrameのScript上書き、初期通知。 |
| Slider | 範囲内floatをDragまたはKeyで変更する。 | Min、Max、Whole Numbers、Value、Direction。 | Handle Rect、Fill Rect、Range逆転、Binding二重書込み。 |
| Scrollbar | ScrollRect等の正規化位置を操作する。 | Value、Size、Steps、Direction。 | ScrollRect参照、Handle、Content寸法。 |
| Dropdown | 旧文字系の選択肢UI。 | Options、Value、Template、Caption。 | Template階層、表示Text、選択Index範囲。 |
| TMPDropdown | TextMeshProを使う選択肢UI。 | Options、Value、TMP Caption/Item、Template。 | TMP Font AssetとTemplateのActive状態を確認する。 |
| InputField | 旧Textへの文字入力。 | Text、Placeholder、Content Type、Character Limit。 | Focus、Input Module、IME、旧Fontを確認する。 |
| TMPInputField | TextMeshProへの文字入力。 | TMP Text、Placeholder、Line Type、Validation。 | Font Atlas、IME、Caret、Selection色を確認する。 |
| ScrollRect | ContentをDrag/Mouse wheelで移動する。 | Content、Viewport、Horizontal/Vertical、Elasticity、Inertia。 | ContentとViewport参照、Mask、Layout更新順を見る。 |
| SceneButton | Button操作から登録Sceneを同期/非同期で開く。 | Scene Path/Build Index、Additive、Loading表示。 | Build Settings登録、Path、Button Action、Scene copy。 |
| UIValueBinding | Runtime値をText、Slider等へ表示する。 | Source、Property、Format、更新間隔、Target UI。 | Source有無、型、Format、Scriptとの二重更新。 |

### UI Layout Component

| Component | 計算内容 | 主設定 | 注意 |
| --- | --- | --- | --- |
| RectTransform | Anchor、Pivot、Size、OffsetからCanvas内矩形を作る。 | Anchor Min/Max、Pivot、Anchored Position、Size Delta。 | 親Rect変更時の伸縮をScene/Game両方で確認する。 |
| HorizontalLayoutGroup | 子を横方向へ整列する。 | Padding、Spacing、Alignment、Child Control/Force Expand。 | 子のLayoutElementと優先度が競合する。 |
| VerticalLayoutGroup | 子を縦方向へ整列する。 | Padding、Spacing、Alignment、Child Control/Force Expand。 | ScrollRect ContentではContentSizeFitterとの更新順を見る。 |
| GridLayoutGroup | 固定Cellを格子状に配置する。 | Cell Size、Spacing、Start Corner/Axis、Constraint。 | 可変サイズ子には不向き。 |
| ContentSizeFitter | Preferred/Minimum Sizeから自身のRectを決める。 | Horizontal Fit、Vertical Fit。 | 親Layoutと同じ軸を双方が制御しない。 |
| AspectRatioFitter | 指定AspectへRectを補正する。 | Aspect Mode、Aspect Ratio。 | LayoutGroup配下では制御軸の競合を確認する。 |
| LayoutElement | 子がLayoutへ申告するMin/Preferred/Flexible寸法。 | Ignore Layout、各Width/Height、Priority。 | 値が未設定の軸はGraphicのPreferred値を使う。 |

### EventSystemとInput Module

| Component | 対象Device | 役割 | 同時使用 |
| --- | --- | --- | --- |
| EventSystem | 全UI | Focus、Selected Object、Pointer eventの中央管理。 | Scene内に有効なものを原則1つにする。 |
| StandaloneInputModule | 旧Keyboard/Mouse | Move、Submit、Cancel、PointerをEventSystemへ送る。 | InputSystemUIInputModuleと二重有効にしない。 |
| InputSystemUIInputModule | Action Map | Point、Click、Scroll、Navigate等のActionをUIへ送る。 | PlayerInputと同じAction Assetを共有できる。 |
| TouchInputModule | Touch | Touch IDごとのPointer eventを送る。 | Mouse emulationとの二重Clickを確認する。 |
| Input | 旧低水準入力設定。 | Key/Button/Axis。 | 新規Gameplay入力はPlayerInputを優先する。 |
| PlayerInput | GameObject単位 | Action Mapを読み、Script/Componentへ値とActionを渡す。 | Map名とAction名、大文字小文字、Deviceを確認する。 |
| PlayerInputManager | 複数Player | Join、Leave、Player Index、Device割当を管理する。 | 1人用Sceneでは不要。 |

### Navigation Component

| Component | 役割 | 主設定 | Runtime状態・確認 |
| --- | --- | --- | --- |
| NavigationAgent | NavMesh上で目標へ移動する現行Agent。 | Speed、Acceleration、Angular Speed、Radius、Height、Stopping Distance、Target。 | EditorNavigationManager経路。Path線、到達可否、TargetのNavMesh上位置を確認する。 |
| NavMeshAgent | 旧名称互換。 | 旧Agent設定。 | 新規追加せずNavigationAgentへ移行する。 |
| NavMeshSurface | NavMesh生成範囲と収集条件を定義する。 | Agent Type、Collect Objects、Layer、Bounds、Voxel。 | Bake dataの保存先とScene変更後の再Bakeを確認する。 |
| NavMeshObstacle | 移動不能領域またはCarving対象を作る。 | Shape、Size、Carve、Move Threshold。 | Carving頻度とDynamic Object数に注意する。 |
| NavMeshModifier | 対象階層のArea/生成可否を上書きする。 | Override Area、Area、Affected Agents。 | Surfaceの収集対象に入っているか確認する。 |
| NavMeshModifierVolume | Box範囲のAreaを上書きする。 | Center、Size、Area、Affected Agents。 | Scene View BoundsとBake結果を比較する。 |
| NavMeshLink | 分離したNavMesh間を接続する。 | Start/End、Width、Bidirectional、Cost、Area。 | Agent半径、段差、向き、Link有効状態を確認する。 |

### AI意思決定Component

| Component | 所有する責務 | 子・関連Component | 使用時の確認 |
| --- | --- | --- | --- |
| AIBehaviorTree | Tree Asset/Rootを評価する。 | AIBehaviorBlackboard、Selector/Sequence/Task/Decorator。 | Root、実行中Node、Abort条件、Tick間隔。 |
| AIBehaviorBlackboard | Key-Value状態を共有する。 | Behavior Tree、Sensor、Script。 | Key名、型、初期値、書込元。 |
| AIBehaviorSelector | 成功する最初の子を選ぶ。 | 子Node。 | 子順序とFailure返却。 |
| AIBehaviorSequence | 子を順番に全て成功させる。 | 子Node。 | Running再開位置と途中Failure。 |
| AIBehaviorTask | 実処理をActionまたはTask種別へ委譲する。 | Script Action。 | Action対象、完了/失敗通知。 |
| AIBehaviorDecorator | 子の実行条件や結果反転を行う。 | Blackboard条件、子Node。 | 条件比較型とAbort範囲。 |
| AIStateMachine | 現在StateとTransitionを評価する。 | AIState、AIStateTransition。 | 初期State、同時成立時の優先順位。 |
| AIState | Enter/Update/Exit Actionを持つ。 | Script Action。 | Actionの実行順と再入。 |
| AIStateTransition | 条件成立時にStateを切り替える。 | Blackboard/Sensor/Timer条件。 | From/To参照、条件、Exit Time。 |
| AIGoapPlanner | World StateからGoalまでのAction列を探索する。 | AIGoapGoal、AIGoapAction、AIGoapWorldState。 | 前提/効果Key、Cost、再計画条件。 |
| AIGoapGoal | 望むWorld Stateと優先度を表す。 | Planner。 | 達成済み条件とPriority更新。 |
| AIGoapAction | 前提、効果、Cost、実行Actionを表す。 | Planner、Script。 | 実行失敗時の再計画。 |
| AIGoapWorldState | Planning用の現在条件を保持する。 | Sensor、Script。 | Key型と更新間隔。 |
| AIHtnPlanner | TaskをMethodで分解して実行列を作る。 | AIHtnDomain、AIHtnTask、AIHtnMethod。 | Domain Root、分解失敗、再計画。 |
| AIHtnDomain | Task/Methodの定義集合。 | Planner。 | 参照切れと循環分解。 |
| AIHtnTask | PrimitiveまたはCompound Task。 | Method、Script Action。 | 完了/失敗とWorld State反映。 |
| AIHtnMethod | Compound Taskの分解条件と子列。 | Task、World State。 | 条件順と代替Method。 |

### AI経路・Steering・Sensor Component

| Component | 個別役割 | 主確認 |
| --- | --- | --- |
| AIPathfindingAgent | Grid/NavMesh経路要求とWaypoint追従をまとめる。 | Path provider、再探索距離、到達半径。 |
| AIMicroPatherGrid | MicroPather用GridとCostを提供する。 | Cell寸法、障害物、斜め移動、再構築。 |
| AIRecastNavMeshBuilder | Recast用NavMesh dataを構築する。 | Bounds、Voxel、Slope、Agent寸法、Bake結果。 |
| AIRecastCrowdAgent | Recast Crowdで局所回避しながら移動する。 | Crowd登録、半径、最大速度、Target。 |
| AIPathRequest | 経路要求のStart/Goal/結果を保持する。 | Provider、Pending/Success/Failed状態。 |
| AIDynamicObstacle | AI経路へ動的障害物を登録する。 | Shape、更新閾値、登録解除。 |
| AISteeringAgent | Steering力の合成、速度上限、加速度上限を管理する。 | Rigidbody/Transform出力、Weight合計。 |
| AISeekSteering | Targetへ向かう。 | Target、Weight、到達時の停止方法。 |
| AIFleeSteering | Targetから離れる。 | Panic距離、Target、Weight。 |
| AIArriveSteering | 減速してTargetへ到達する。 | Slow Radius、Stop Radius、Time To Target。 |
| AIPursuitSteering | Target速度から未来位置を予測する。 | Prediction上限、Target velocity取得。 |
| AIWanderSteering | 連続性のあるランダム方向を作る。 | Circle距離/半径、Jitter、Seed。 |
| AIObstacleAvoidanceSteering | 前方Probeから回避力を作る。 | Probe長、Layer、Whisker、Weight。 |
| AIFlockSteering | Separation、Alignment、Cohesionを合成する。 | Neighbor半径、各Weight、Group条件。 |
| AIVisionSensor | 距離、FOV、遮蔽で可視対象を検出する。 | Layer、距離、角度、Occlusion、検出間隔。 |
| AIOpenCvCamera | OpenCV処理へCamera frameを渡す。 | Capture source、解像度、更新頻度。 |
| AIOpenCvObjectDetector | Frameから学習済みClassを検出する。 | Model、Threshold、NMS、Class filter。 |
| AIOpenCvColorTracker | 色範囲から領域を追跡する。 | HSV範囲、最小面積、平滑化。 |
| AIMotionSensor | Frame差分またはTransform変化を検出する。 | Threshold、Mask、更新頻度。 |
| AIWhisperSpeechRecognizer | 音声を文字列へ変換する。 | Model、Language、Device、区切り。 |
| AIVoiceCommand | 認識文字列をCommand/Actionへ対応付ける。 | Phrase、Tolerance、Cooldown、Action。 |

AI系はComponent名だけで完成扱いにしない。EditorAIManagerへ登録されても、外部Model、Bake data、Capture device、Audio input、Runtime DLLが必要な型は、個別ページに依存物と失敗時ログを記載する。

### Audio空間処理とFilter

| Component | 処理 | 主設定 | 順序・注意 |
| --- | --- | --- | --- |
| AudioListener | 最終的な聴取位置と向きを提供する。 | Active、Volume。 | 有効Listenerを原則1つにし、Game Camera追従を確認する。 |
| AudioSource | Clip/BGM/SEを再生し、距離、Cone、Doppler、Busへ送る。 | Clip、Loop、Volume、Pitch、Spatial Blend、Min/Max Distance。 | Source、Occlusion、Filter、Bus、Listenerの順に調べる。 |
| AudioReverbZone | World範囲内のSource/Listenerへ残響環境を適用する。 | Min/Max Distance、Preset、Decay、Reflections。 | Zone重複時のBlendとListener位置を見る。 |
| AudioLowPassFilter | Cutoffより高い周波数を減衰する。 | Cutoff、Resonance。 | 水中、壁越し表現。Cutoff単位とBypassを確認する。 |
| AudioHighPassFilter | Cutoffより低い周波数を減衰する。 | Cutoff、Resonance。 | 無線、薄い音表現。LowPassとの併用順を見る。 |
| AudioEchoFilter | 遅延音をFeedback付きで加える。 | Delay、Decay、Wet/Dry Mix。 | Feedback過大とVoice tail停止を確認する。 |
| AudioDistortionFilter | 波形を非線形変形する。 | Distortion Level。 | Clip防止とMaster Volumeを確認する。 |
| AudioReverbFilter | Source単位で残響を加える。 | Preset、Room、Decay、Diffusion、Density。 | ReverbZoneとの二重適用に注意する。 |
| AudioChorusFilter | 遅延変調した複数Voiceを混ぜる。 | Rate、Depth、Delay、Mix。 | 3D定位をぼかし過ぎない。 |

### RendererとEffect Component

| Component | 担当 | 必要Asset/Component | Runtime確認 |
| --- | --- | --- | --- |
| MeshFilter | 静的Mesh参照だけを保持する。 | Mesh Asset、MeshRenderer。 | Mesh未設定、SubMesh数、Bounds。 |
| ModelRenderer | Model/MeshをMaterial付きで描画する。 | MeshまたはModel Asset、Material。 | Normal/Tangent、Light、Reflection、Cull。 |
| SkinnedMeshRenderer | Bone Skinning済みMeshを描画する。 | Skin、Skeleton、Material、Animator。 | Bone index/weight、現在/前Frame行列、Bounds。 |
| SpriteRenderer | QuadへTexture/Spriteを描画する。 | Texture、Material。 | Alpha、UV、Camera、Sort、SRV。 |
| LineRenderer | 点列を幅付きLineとして描く。 | Point列、Material。 | World/Local、幅、点数2以上。 |
| TrailRenderer | 時系列位置からTrailを生成する。 | Material、Lifetime、Width curve。 | 更新と描画を分離し、Teleport時にClearする。 |
| BillboardRenderer | Camera向きQuadを描く。 | Texture/Material、Size。 | 軸固定方式とCamera参照。 |
| ParticleSystemRenderer | Particle dataを描画する。 | ParticleSystem、Material。 | SimulationはParticleSystem側、描画だけを担当する。 |
| ParticleSystem | Spawn、Lifetime、Velocity等を更新する。 | Renderer、Material。 | Max Particle、Pool、Simulation space。 |
| VisualEffect | Effect Asset/Graph相当の処理を再生する。 | Effect Asset。 | Asset path、Play/Stop、Parameter接続。 |
| LensFlare | Light方向のScreen effectを描く。 | Flare Asset、Light/位置。 | OcclusionとExposure、Camera角度。 |
| FlareLayer | Camera側でLensFlareを受け取る。 | Camera。 | 対象Cameraと有効状態。 |
| Projector | Worldへ投影Materialを描く。 | Projector Material。 | Frustum、Layer、Depth bias。 |
| DecalProjector | SurfaceへDecalを投影する。 | Decal Material。 | Normal、Depth、受信Layer、寿命。 |

### 3D JointとConstraintの差分

| Component | 拘束する量 | 主設定 | 代表用途 |
| --- | --- | --- | --- |
| HingeJoint | 1回転軸以外を拘束する。 | Anchor、Axis、Limit、Motor、Break。 | 扉、車輪、舵。 |
| FixedJoint | 相対位置と回転を固定する。 | Connected Body、Break Force/Torque。 | 一時接着、破壊可能接続。 |
| SpringJoint | 2点距離へばね・減衰をかける。 | Min/Max Distance、Spring、Damper。 | ゴム、簡易Suspension。 |
| ConfigurableJoint | 各平行移動・回転軸を個別制限する。 | Motion、Limit、Drive、Projection。 | 複雑な機械関節。 |
| CharacterJoint | 人体向けCone/Twist制限を行う。 | Swing/Twist Limit、Connected Body。 | Ragdoll。 |
| ParentConstraint | 複数Sourceの位置・回転をWeight合成する。 | Sources、Weights、Offsets。 | 装備持替、Camera Rig。 |
| PositionConstraint | World位置だけをSourceへ追従する。 | Sources、Axes、Offset。 | Marker、追従点。 |
| RotationConstraint | 回転だけをSourceへ追従する。 | Sources、Axes、Offset。 | 砲塔、UI Marker向き。 |
| ScaleConstraint | ScaleだけをSourceへ追従する。 | Sources、Axes、Offset。 | 演出階層。 |
| AimConstraint | 指定軸をTarget方向へ向ける。 | Aim Axis、Up Axis、World Up、Weight。 | 砲身、視線。 |
| LookAtConstraint | Object前方をTargetへ向ける。 | Target、Up、Clamp、Weight。 | Camera、Character頭部。 |

ConstraintはAnimator、親子Transform、Physics Servoと同じTransformを更新し得る。更新順を個別ページへ書き、Dynamic RigidbodyをTransformで直接上書きしない。

### 2D物理Componentの現行扱い

CG2では2D物理Runtimeを実装対象にしない。次の型はScene互換とInspector dataのため残っているが、新規ゲームで「物理が動くComponent」として案内しない。

| 分類 | Component | 現行扱い |
| --- | --- | --- |
| Body | RigidBody2D | 互換データ。3D Rigidbodyの代替にはならない。 |
| Collider | BoxCollider2D、CircleCollider2D、CapsuleCollider2D、PolygonCollider2D、EdgeCollider2D、CompositeCollider2D、TilemapCollider2D、CustomCollider2D | 互換データ。3D Physics Queryへ登録されない。 |
| Joint | DistanceJoint2D、HingeJoint2D、SpringJoint2D、FixedJoint2D、SliderJoint2D、WheelJoint2D | 互換データ。拘束Solverへ接続しない。 |
| Effector | PlatformEffector2D、SurfaceEffector2D、AreaEffector2D、PointEffector2D、BuoyancyEffector2D | 互換データ。Forceは発生しない。 |

2D表示が必要な場合はSpriteRenderer、Canvas、UI、3D Colliderを組み合わせる。2D物理が必要なProjectでは別Pluginとして導入し、現行3D Solverへ中途半端に混在させない。

### Animation補助Component

| Component | 役割 | 設定・Data | Runtime確認 |
| --- | --- | --- | --- |
| Animator | ParameterとState/Transitionから再生ClipとBlendを決める。 | Controller、初期State、Float/Int/Bool/Trigger/Vector Parameter。 | State名、遷移条件、Blend時間、現在/前Frame Bone行列を確認する。 |
| Animation | 1つのClipを直接再生する。 | Clip、Speed、Loop、Play On Awake、Time。 | Clip Import、Duration、停止/再開、負のSpeed対応を確認する。 |
| AvatarMask | Animationを適用するBone集合を制限する。 | 1行1Bone名、末尾`*`の前方一致。 | FBX Bone名、親だけ選択した時の子、除外Boneの初期姿勢を確認する。 |
| PlayableDirector | 時刻を持つAnimation/演出再生を制御する。 | Playable/Clip、Time、Speed、Loop、Play On Awake。 | 現行はAnimation再生経路との共有範囲を明記し、未対応Trackを列挙する。 |

`SkinnedMeshRenderer`は描画、`Animator`は状態決定、`Animation`はClip再生、`AvatarMask`はBone適用範囲、`PlayableDirector`は時間制御であり、1つのComponentへ責務を集めない。

### Terrain・Tilemap Component

| Component | 役割 | 主設定 | 現行確認 |
| --- | --- | --- | --- |
| Terrain | Height Mapから3D地形Meshと距離LODを生成する。 | Height Map、Size X/Y/Z、最高LOD解像度、Material。 | Height Map import、LOD境界、Shadow用LOD、Boundsを確認する。 |
| TerrainCollider | TerrainまたはHeight dataを3D Physics形状へ渡す。 | Terrain参照、Friction、Restitution、Layer。 | 描画HeightとCollider Height、Transform Scale、再生成Timingを比較する。 |
| Grid | Cell座標とWorld座標の変換基準を提供する。 | Cell Size、Gap、Layout、Swizzle。 | 子Tilemapとの原点、Scale、座標変換を確認する。 |
| Tilemap | CellごとのTile ID/Transform/Color dataを保持する。 | Grid参照、Size、Origin、Tile data。 | 現行Editorで作成・保存・編集できる範囲を明記する。 |
| TilemapRenderer | Tilemap dataをBatch描画する。 | Material、Sort、Chunk/Culling Bounds。 | Tilemap参照、Texture Atlas、Sort、Draw callを確認する。 |
| TilemapCollider2D | 旧2D Tilemap Collider data。 | Tilemap参照、Composite設定。 | 2D Physics Runtime対象外。3D衝突にはTerrainColliderまたは3D Colliderを使う。 |

Brush、Palette、Terrain Layer Paint、Runtime地形変形は、対応UIと保存・Runtime経路を確認できたものだけ実装済みと書く。

### 旧RailShooter互換Component

| Component | 旧責務 | 現行での置換 | 現行扱い |
| --- | --- | --- | --- |
| LegacyRailShooterEnemy | 敵HP、移動、攻撃等を一体化していた旧data。 | Health、RailMovement/Steering、Weapon、C++ Script。 | 互換読込専用。新規追加しない。 |
| LegacyRailShooterShip | 船移動、照準、射撃、HUD等を一体化していた旧data。 | RailMovement、Buoyancy、ScreenAim、WeaponLoadout、UI、Script。 | 互換読込専用。 |
| LegacyRailShooterEnemyMotion | 敵固有の軌道data。 | RailMovement、MovementModifier、TargetSteering、Script。 | 互換読込専用。 |
| LegacyRailShooterStage | Wave、Boss、進行、演出を一体化していた旧data。 | WaveSpawner、ActionSequence、TimelineEvent、Scene、Script。 | 互換読込専用。 |

旧Componentを読み込んだ時に自動で新構成へ変換するか、値を保持するだけか、警告を出すかをMigration手順へ明記する。互換型へ新しいゲーム機能を追加しない。

## 全Component個別説明の詳細規約

以降の表は概要ではなく、Inspectorで設定する時の実用リファレンスである。各行を独立したComponent説明として読み、同じ共通描画関数を使う型でも用途と必要構成を混同しない。

### 基本Componentの個別設定

| Component | 目的と責務 | Inspector設定 | Runtime・保存 | 連携と確認 |
| --- | --- | --- | --- | --- |
| Transform | GameObjectの親に対するLocal位置・回転・Scaleを保持し、World行列を作る。描画・物理・Audio・子階層の基準であり、移動ルールは持たない。 | Position m、Rotation degree、Scale。Scene GizmoではLocal/World軸とSnapを選ぶ。 | Scene/Prefabへ保存。親World行列との積で子Worldを更新する。Dynamic RigidbodyではPhysics結果がWorld姿勢を更新する。 | 親移動で子が動かない場合は親ID、循環参照、World/Local変換、Rigidbody同期順を確認する。 |
| RectTransform | Canvas内の矩形、Anchor、Pivot、Size、Offsetを保持する。通常Transformの3D寸法とは別にUI Layoutが使う。 | Anchor Min/Max、Pivot、Anchored Position、Size Delta。 | Scene保存。Canvas解像度または親Rect変更時にLayoutを再計算する。 | 伸縮しない場合はAnchorが同一点か範囲か、CanvasScaler、Layout Groupによる上書きを確認する。 |
| Canvas | UI描画のRoot、描画空間、Sort、対象Cameraを決める。個別Buttonのゲーム処理は持たない。 | Screen/Camera/World方式、Sort Order、Target Camera。 | Game View解像度ごとにRectを更新する。子Graphicを順序付けして描画する。 | CanvasScaler、GraphicRaycaster、EventSystemを用途に応じて追加する。複数CanvasではSortを確認する。 |
| Script | Native C++ DLL、Instance生成、公開Field、ActionをGameObjectへ接続する。 | DLL Path、Class/Factory、公開Field、Action接続。 | Play開始時にInstance生成、Stop/Scene unloadで破棄。SceneにはPathと公開値を保存する。 | ABI Version、Export、Debug/Release DLL、作業Directory、同じDLLを複数Objectで使う場合のInstance分離を確認する。 |
| MonoBehaviour | 旧または互換Script Componentの設定を保持する。 | Script参照、公開値。 | 現行Native C++ Script経路と同じ実体を持つかを個別確認する。 | 新規Scriptでは`Script`を主入口にし、互換型を完成機能として案内しない。 |

### 描画Componentの個別設定

全Renderer共通Material設定はBase Color/Texture、Normal、Metallic、Roughness、AO、Emission、Height、Opacity、Lighting Mode、Reflection、IOR、Alpha Mode、Alpha Cutoff、Double Sided、UV Tiling/Offset、Clear Coat、Transmission、Subsurface、Anisotropy、Sheenである。ただし、次の通りMesh供給元と更新方法が異なる。

| Component | Geometryの供給 | 有効な主要設定 | Runtime動作 | 失敗時の確認 |
| --- | --- | --- | --- | --- |
| MeshFilter | Mesh Assetだけを保持する。自身は描画しない。 | FBX/OBJ/基本形Mesh、SubMesh。 | ModelRendererが同じObjectのMeshFilterを参照する。 | Asset Path、Import完了、SubMesh数、Bounds。 |
| ModelRenderer | 静的Mesh/ModelをPBRまたは選択Lightingで描画する。 | 全Material設定、Imported Material Texture、Cast/Receive Shadow。 | World行列とMaterialを描画Queueへ登録する。 | MeshFilter/Model Asset、Normal/Tangent、Material SRV、Camera Layer、Light。 |
| SkinnedMeshRenderer | Bone Weight/Indexで頂点を変形して描画する。 | Skin Asset、Material、Bounds。 | Animator/Animationの現在・前Frame Bone行列を使い、Motion Vectorへスキニング後位置を出す。 | Skeleton一致、Weight合計、Bone Index範囲、Bounds、前Frame行列。 |
| SpriteRenderer | QuadへPNG等を貼り、Scene内Spriteとして描画する。 | Texture、Color、Alpha、UV、Lighting Mode、Sort。 | Cameraへ向ける処理は持たず、Transform姿勢のQuadを描画する。 | Texture SRV、Alpha Mode、Scale、Camera Clip、背面Cull。 |
| LineRenderer | 点列を連結した幅付きGeometryとして描く。 | Point、Width、Color/Material、World/Local。 | 点列更新後にVertex dataを再構築する。 | 2点以上、幅>0、座標空間、透明Sort。 |
| TrailRenderer | 過去位置を時間順に保持して帯を描く。 | Lifetime、Width curve、Color、Material、Minimum Distance。 | Updateで履歴を追加しDrawで既存履歴を描画する。Teleport/Pool返却時は履歴をClearする。 | Lifetime、移動距離、Simulation Space、再利用時の残像。 |
| BillboardRenderer | QuadをCamera方向または軸固定で回転して描く。 | Texture、Size、Axis Lock、Material。 | ViewごとにCamera方向を使うためScene/Game Cameraを混同しない。 | 対象Camera、軸固定、Alpha、遠近Scale。 |
| CanvasRenderer | CanvasのGraphic頂点をUI Passへ送る。 | Material、Color、Inherited Alpha、Cull。 | 単独では描画せずImage/Text等からGeometryを受ける。 | 親Canvas、Graphic、Mask、Sort。 |
| ParticleSystemRenderer | Particle instanceをBillboard/Meshとして描く。 | Material、Render Asset、Facing、Sort。 | SimulationはParticleSystem、描画だけを担当する。 | ParticleSystem Active、Alive数、Material、Mesh Asset。 |
| TilemapRenderer | TilemapのCell dataをAtlas単位でBatch描画する。 | Atlas、Material、Sort、Chunk Bounds。 | Grid/Tilemap更新後に可視Chunkを描画する。 | Grid原点、Tile ID、Atlas UV、2D Sort。 |

### Camera・Light・環境Componentの個別設定

| Component | 設定項目と単位 | Runtime動作 | 組み合わせ・制限 |
| --- | --- | --- | --- |
| Camera | FOV degree、Near/Far m、Perspective/Orthographic、Exposure EV、DOF、Motion Blur、Priority。 | Game Viewは有効CameraのPriorityから採用し、View/Projection、前Frame行列、VelocityをViewごとに保持する。 | Scene View Cameraと履歴を共有しない。DOF/Motion Blurは設定だけでなくPass接続を確認する。 |
| CinemachineCamera | Priority、Follow、LookAt、Position/Rotation damping、FOV。 | 実Cameraへ目標姿勢を出すVirtual Cameraとして扱う。 | Game ruleやRail移動は持たない。RailMovement/Constraintと組み合わせる。 |
| Light | Type、Color、Intensity、Range、Spot Angle、Shadow、Direction。 | Directionalは位置ではなくTransform前方から平行光を作り、Point/SpotはWorld位置を使う。 | Right/Sun等の名前では方向は決まらない。Scene Gizmoの前方とShadow Cameraを確認する。 |
| ReflectionProbe | Cubemap/Planar方式、Center、Size、Strength、Roughness。 | Probe範囲とMaterial roughnessから反射を選ぶ。Planarは反射面TransformからMirror Cameraを作る。 | SSR、Environment、Planarを加算し過ぎない。反射面位置・法線・Clip planeを確認する。 |
| LightProbeGroup | Local Probe Position配列を保持する。 | 動的Object位置で近傍Probeを補間する経路へ接続された場合だけ間接光へ反映する。 | Bake/補間Runtime未接続なら設定のみと明記する。 |
| LightProbeProxyVolume | BoundsとResolutionで大型Object用の補間領域を定義する。 | Volume内SampleをSkinned/大型Rendererへ渡す実装の有無を確認する。 | LightProbeGroupが必要。Unity名だけで同等実装としない。 |
| Environment | Sky上/下色、HDRI Texture、Rotation rad、Mip Bias、Exposure、Ambient、Reflection contribution。 | Sky描画、間接光、Reflection fallbackへ同じ環境設定を供給する。 | Environmentを上げても直接光の陰影は増えない。Lightとの役割を分ける。 |
| Volume | Priority、Weight、Blend Distance、Profile/範囲。 | Camera位置に応じてPostProcess値を補間する実装が接続されている場合だけ有効。 | Global/Local、重複Priority、未接続Parameterを明記する。 |
| PostProcess | Bloom、Glare、AA、SSR、Exposure、Tone Map、Saturation、Contrast、Vignette、Grain、CA、AO。 | Scene/Game View別履歴で各Passを実行しFinal Compositeへ値を渡す。 | Inspector値がHardcodeで上書きされていないか、AA Modeが排他か、History reset条件を確認する。 |
| FlareLayer | CameraがLensFlareを収集するかを決める。 | CameraごとのFlare passを有効化する。 | LensFlare側だけでは表示されない。ExposureとOcclusionも確認する。 |

### 3D Physics Componentの個別設定

| Component | 必須構成 | 設定の意味 | FixedUpdate挙動・Debug |
| --- | --- | --- | --- |
| RigidBody | 3D Colliderを推奨。 | Mass kg、Linear/Angular Drag、Gravity、Kinematic、Velocity m/s、Angular Velocity rad/s、Inertia倍率、Gyroscopic、Freeze、Interpolation、CCD。 | Play開始時にBodyを登録。DynamicだけForceで動く。速度・角速度・重心・SleepをDebug表示する。 |
| BoxCollider | Transform。 | Center m、Size m、Trigger、Friction、Restitution、Layer、Contact Event。 | World Scaleを反映したBox Shapeを登録する。負/0 Sizeを避ける。 |
| SphereCollider | Transform。 | Center、Radius、Trigger、Physics Material、Layer。 | 非一様Scale時の半径規則を明記する。Scene球とPhysics Shapeを比較する。 |
| CapsuleCollider | Transform。 | Center、Radius、Height、Direction、Trigger、Material。 | Heightは直径以上。軸とScaleをScene Gizmoで確認する。 |
| MeshCollider | Model/Mesh。DynamicではConvexを推奨。 | Mesh Source、Convex、Trigger、Material、Layer。 | Triangle Meshは静的地形向け。Dynamic concaveの制限とCook失敗Logを書く。 |
| AutoConvexCollision | Model/Mesh。 | Source、Hull数/頂点数、Concavity、生成操作。 | 生成済みConvex Hullを別Collision dataとして使い、MeshColliderを削除しない。生成時間とCacheを確認する。 |
| TerrainCollider | Terrain/Height Map。 | Terrain参照、Material、Layer。 | 描画Heightと同じScale/OffsetでPhysics Height Fieldを作る。再生成条件を確認する。 |
| WheelCollider | RigidBody。 | Radius、Suspension、Spring/Damper、Motor/Brake/Steer、Friction。 | Contact、Compression、Slip、ForceをDebug表示する。単なるSphere Colliderではない。 |
| CharacterController | Capsule形状。 | Radius、Height、Step Offset、Slope Limit、Skin Width。 | Rigidbody ForceではなくSweep/Slideで移動する実装範囲を明記する。 |
| ConstantForce | Dynamic Rigidbody。 | Force N、Relative Force、Torque N m、Relative Torque。 | 毎FixedUpdateで加える。Impulseではない。Enable切替時の二重適用を確認する。 |
| Aerodynamics | Dynamic Rigidbody。 | Air Density、Cd、Area、Lift、Stall、Side Force、Magnus、Center of Pressure、Wind、Force上限。 | 相対風速からDrag/Lift/Side Forceを作用点へ加える。迎角、横滑り、力・Torqueを表示する。 |
| WindZone | Aerodynamics等の受け手。 | Direction/Radial、Speed m/s、Radius、Turbulence、Frequency。 | World風速を提供するだけでBodyへ直接Forceを固定しない。範囲と風Vectorを表示する。 |
| GravityField | Dynamic Rigidbody。 | Inverse-square/Constant、Source Mass、G、Acceleration、Minimum Distance、Radius、上限。 | 既定重力と加算される。中心特異点と単位Scaleを確認する。 |
| RotatingFrame | Dynamic Rigidbody。 | Angular/Linear Velocity、Angular Acceleration、Radius、上限。 | Coriolis、遠心、Euler疑似力を加える。World/Local座標系を明記する。 |
| FluidVolume | Dynamic RigidbodyとCollider。 | Volume Size、Density、Viscosity、Drag、Flow、Angular Viscosity、Force上限。 | 浸水率からArchimedes浮力と抵抗を計算する。Volume境界とForceを表示する。 |
| SpringForce | Owner Rigidbody、任意Target Rigidbody。 | Anchor、World/Target Anchor、Rest Length、Stiffness、Damping、上限、Reaction。 | Hooke則と軸方向相対速度からForceを計算する。長さとForceを表示する。 |
| RopeConstraint | Owner Rigidbody。 | Target/World Anchor、Maximum Length、Stiffness、Damping、Tension上限、Break、Reaction。 | たるみ時Force 0、伸長時だけ張力。Current Length/Tension/Brokenを表示する。 |
| TorsionSpring | Dynamic Rigidbody。 | Target、Rest Rotation rad、Stiffness、Damping、Torque上限、Reaction。 | 角度誤差と相対角速度からTorqueを加える。目標軸とTorqueを表示する。 |
| Thruster | Dynamic Rigidbody。 | Direction、Local Application Point、Force N、Throttle 0-1、Local Direction。 | 作用点へForceを加え、重心差でTorqueを生む。方向とForceを表示する。 |
| PulleyConstraint | 2つのDynamic Rigidbody。 | 両Anchor/Support、Total Length、Ratio、Stiffness、Damping、Break。 | `lengthA + ratio * lengthB`超過へ張力。両側長とTensionを表示する。 |
| PhysicsServo | Dynamic Rigidbody。 | Target/World Pose、Position/Rotation PD、Force/Torque上限、Reaction。 | Transformを直接上書きせずForce/Torqueで追従する。誤差Vectorを表示する。 |
| VortexField | Dynamic Rigidbody。 | Axis、Radius、Angular/Radial/Axial velocity、Coupling、加速度上限。 | 目標流速との差を加速度へ変換する。影響範囲と接線Vectorを表示する。 |
| PressureField | Dynamic RigidbodyとCollider面積。 | Pressure Pa、Radius、Falloff、Force上限。 | 投影面積と距離減衰から放射/吸引Forceを計算する。範囲とForceを表示する。 |
| Suspension | Dynamic Rigidbody。 | Anchor、Direction、Rest/Max Length、Wheel Radius、Spring、Damping、上限、Reaction。 | Ray/Sphere contactから圧縮Forceを作用点へ加える。Grounded、Length、Normalを表示する。 |
| UprightStabilizer | Dynamic Rigidbody。 | Local Up、Target World Up、Stiffness、Damping、Torque上限。 | 傾き誤差へPD Torqueを加える。完全固定ではなく外力で傾く。 |
| ElectromagneticBody | Dynamic Rigidbody。 | Charge C、Magnetic Moment、Force/Torque上限。 | FieldからCoulomb/Lorentz/磁気Torqueを受ける。電荷とForceを表示する。 |
| ElectromagneticField | ElectromagneticBody。 | Uniform/Point Charge、Electric Field、Magnetic Field、Source Charge、Coulomb定数、Radius。 | 範囲内BodyへField値を供給する。Scene Scaleに応じた定数を明記する。 |

Jointの個別設定は、全てConnected GameObject、Anchor、Break Force/Torqueを共通確認する。HingeJointはAxis/角度Limit/Motor、FixedJointは全自由度固定、SpringJointはMin/Max DistanceとSpring/Damping、ConfigurableJointは6自由度ごとのMotion/Limit/Drive、CharacterJointはSwing/Twist制限を使う。

### Gameplay Componentの個別設定

| Component | Inspectorで必ず説明する値 | Runtime責務 | C++/Actionと失敗確認 |
| --- | --- | --- | --- |
| LocalMove | Local/World方向、速度、対象軸。 | Transformを単純移動する。衝突解決はしない。 | Rigidbody/Animation/Railとの二重更新を避ける。 |
| RollingMove | Torque、Horsepower、入力方向。 | Dynamic RigidbodyへTorqueを加える。 | 接地、摩擦、Freeze Rotation、質量を確認する。 |
| FreeTransform | 移動/回転入力、速度、軸Enable、Local。 | 毎UpdateでTransformを直接変更する。 | 物理Objectへ付けない。 |
| RailMovement | Path、Speed、Acceleration/Deceleration、Start、Look Ahead、Range、Offset、Input、Mode、PD、Loop/Reverse/Stop。 | 距離基準でSpline Frameを求め、TransformまたはRigidbodyへ追従させる。 | `RailFollower` API。Path 2点以上、Input二重、Buoyancy軸分担を確認する。 |
| RailBranch | Follower、Target Path、Progress/External、Preserve、Once、Action。 | 条件成立時にPath参照を切り替える。 | `RailBranch::Trigger`。ゲーム条件はScript側。 |
| Health | Maximum、Play中Current。 | 汎用現在値を保持する。死亡ルールは持たない。 | `Health::Get/Set/Damage`。DamageReceiver有無を確認する。 |
| DamageReceiver | Multiplier、Invulnerability、Deactivate on Death、Damaged/Death Action。 | DamageContextを適用し、無敵時間と通知を管理する。 | 最後のContext取得。Pool返却時のHealth resetを確認する。 |
| ScreenAim | Mouse/Vector2、Input Object、Action Map/Action、Speed、Invert Y、Reticle、Initial、Clamp。 | 0-1画面座標とAim Rayを提供する。 | `Physics::GetAimRay`。Game View FocusとCanvas座標を確認する。 |
| HitscanWeapon | Aim/Input、Action、Range、Damage、Interval、Automatic、Fired/Hit/Miss Action。 | 発射時にRaycastしてDamageContextを送る。 | `Weapon::FireHitscan`。trueは発射成立であり命中保証ではない。 |
| ProjectileEmitter | Aim/Input、Pool、Spawn Point、Speed、Damage、Radius、Lifetime、Interval、Automatic、Action。 | Pool弾を生成し、前Frameから連続Castして命中を処理する。 | `Weapon::FireProjectile`。Pool不足、Spawn参照、CCDを確認する。 |
| WeaponLoadout | Selected Slot、Changed/Reloaded Action。 | 子WeaponLoadoutSlotをHierarchy順に収集して装備を管理する。 | `WeaponLoadout` API。Slot順とVisual Activeを確認する。 |
| WeaponLoadoutSlot | Name、Weapon/Visual Object、Current/Reserve/Maximum Ammo、Reload、Auto Reload。 | 選択中Weaponと弾薬状態を提供する。 | Weapon Component、Reserve=-1、Reload中を確認する。 |
| TargetSelector | Layer、Distance、Angle、Reference、Occlusion、Max Targets、Mode、Team Filter、Action。 | Collider/Health/TargetPoint候補から現在Targetを決める。 | `Targeting` API。Team継承、Neutral、遮蔽Rayを確認する。 |
| TargetSteering | Target/Selector、Turn Speed、Acceleration、Max Speed、Delay、Prediction、Transform/Rigidbody。 | Target方向へ旋回・前進する。Damageは扱わない。 | 明示Target優先、Selector喪失、Rigidbody有無を確認する。 |
| TargetPoint | Priority、Radius、Aim Offset。 | 親Object上の独立したTarget位置を提供する。 | Scene球、親Team/Health/Collider継承を確認する。 |
| Team | Team ID、Targetable。 | TargetSelector用の所属だけを提供する。 | 負数Neutral。Player/Enemy固定Enumではない。 |
| MovementModifier | Position/Rotation Offset、Axis Mask、Input Range/Speed、PlayerInput、Action。 | 基準Transformへ汎用Offsetを追加する。 | Rail側Offsetとの二重適用を避ける。 |
| ObjectPool | Template、Lazy Capacity、Allow Expand。 | 必要になったInstanceだけを遅延生成し、返却後に再利用する。 | 最大同時利用数、実体化済み高水位、物理・Script動的登録を確認する。 |
| PrefabSpawner | Pool、Spawn Point、External/Start/Interval、Spawned Action。 | Poolから1体生成する。 | `Spawner::Spawn`。生成後行動は別Component/Script。 |
| WaveSpawner | Pool、Spawn Point、Count、Formation、Spacing、Start/Rail条件、Interval、Completion、Actions。 | Wave設定はデータとして保持し、出現時だけPoolから実体を編隊生成して全生成と全撃破・全返却を追跡する。 | Pool遅延生成容量、Health/返却条件、貸出世代、Rail再初期化を確認する。 |
| TimelineEvent | Time/Rail Source、Value、Target、Action、Once。 | 閾値を横切った時だけActionを送る。 | Sourceの進行方向とPlay再開時の再発火を確認する。 |
| ThresholdState | Health/Rail Source、2境界、3 Action。 | 値を3状態へ分類し変更時だけ通知する。 | 境界順を補正し、初回評価通知を確認する。 |
| ActionSequence | Play On Start、Loop、子Step。 | Stepを順次/並列実行する。 | `ActionSequence` API。子Hierarchy順、Signal名、Scene loadを確認する。 |
| ActionSequenceStep | Type、Parallel Group、対象、Action/Wait/Active/Scene/Condition/Signal。 | 親Sequenceからのみ評価される。 | 親なし、分岐Index、連続Parallel Groupを確認する。 |
| PropertyTween | Target、Component、Property、Float/Vector3、Start/End、Duration、Curve、Loop、Action。 | Runtime Propertyを時間補間する。 | 登録名・型一致、Duration、Stop時状態を確認する。 |
| ActionRelay | Relay On Start、子Target。 | 1回のRelayを複数Actionへ分配する。 | `ActionRelay::Relay`。条件/待機はSequenceを使う。 |
| ActionRelayTarget | Enabled、Target、Action。 | 親Relayの通知1件を保持する。 | 親なし、空Action、非Active Scriptを確認する。 |
| Saveable | Key、Transform、Active、Health、Rigidbody、Script公開値。 | 選択した状態だけSlotへSerializeする。 | Key重複、Scene Object欠落、Versionを確認する。 |
| Checkpoint | Slot、Save/Load On Start、完了Action。 | SaveSystem呼出のScene入口を提供する。 | `Checkpoint::Save/Load`。Slot名とAction結果を確認する。 |
| CameraBlend | Source/Target Camera、Duration、Linear/SmoothStep、Play On Start。 | Game View Camera姿勢とFOVを補間する。 | Target不在、Priority競合、0秒を確認する。 |
| CameraShake | Position/Rotation Amplitude、Frequency、Duration、Play On Start。 | 複数ShakeをCamera基準姿勢へ加算する。 | `CameraEffects::PlayShake`。Transformへ永久蓄積しない。 |

### 保存とRuntime状態の個別確認

全Componentで次を区別する。

1. Sceneへ保存される編集値。
2. Play開始時に初期化されるRuntime値。
3. Stop時に破棄される一時状態。
4. Saveable/SaveSystemで明示的に永続化するゲーム状態。
5. Scene切替時だけ渡すScene Data。

`現在Target ID`、`現在体力`、`Rope Current Tension`、`Suspension Grounded`、Particle Alive数などのRuntime表示値をScene初期値として保存しない。逆に、Path参照、Action名、Material、物理係数、Slot名などの編集設定はScene/Prefabへ確実に保存する。

## 水上3Dレールシューティング向け汎用基盤Component

ここで説明する6 Componentは、敵、武器、船、ボスへ専用ルールを埋め込むものではない。時間、状態、可変値、部位破壊の接着、編隊追従、ロック進行という再利用可能な責務だけを持つ。ゲーム固有の「どのStateで何を撃つか」「破壊時に何点加算するか」はC++ ScriptのAction側へ置く。

### Timer（表示名: タイマー）

**目的:** 1回または一定間隔でC++ Script Actionを通知する。敵射撃間隔、再装填完了、無敵解除、演出遅延などに使う。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| 時間 | float、1.0秒、0.001-86400 | 発火までの時間。0以下はRuntimeで0.001秒へ補正する。 |
| 繰り返す | bool、false | trueなら発火後に同じ時間へ戻して継続する。 |
| Play開始時に再生 | bool、true | Play開始時に自動開始する。falseならC++の`Timer::Start()`が必要。 |
| 一時停止 | bool、Runtime表示兼操作 | trueの間は残り時間を減らさない。Play開始時は自動再生設定から再初期化される。 |
| 残り時間 | Runtime表示 | 秒。Scene初期値ではなく、Play開始時に`時間`から作り直す。 |
| Action対象 | GameObject、未設定時Owner | Actionを受けるC++ Script所有Object。 |
| 発火Action | string、`OnTimer` | 対象Scriptで`BindAction`した関数名。 |

Runtimeは可変`Update`で秒を減算する。厳密な物理周期やFrame固定処理には使わず、物理へForceを加える処理は受信ActionからFixedUpdate用状態へ渡す。1 Objectに複数の独立Timerが必要ならTimer所有用の子GameObjectを分けるかActionSequenceを使う。

失敗時は、Component Active、Play開始設定、一時停止、Action対象、Action名の大文字小文字、対象Scriptの`BindAction`を順に確認する。`Timer::Start()`は残り時間を先頭へ戻すため、単なるResumeには`Timer::Resume()`を使う。

### GenericStateMachine（表示名: 汎用ステートマシン）

**目的:** ゲーム側が定義する任意の文字列Stateを1つ保持し、変更時だけActionを通知する。`Approach`、`Attack`、`Retreat`等の意味と遷移条件はScript側が決める。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| 初期State | string、`Initial` | Play開始時の現在State。 |
| 現在State | Runtime表示 | `ChangeState`成功後の文字列。 |
| Action対象 | GameObject、未設定時Owner | 変更通知先。 |
| 変更Action | string、`OnStateChanged` | Stateが異なる文字列へ変わった時だけ通知する。PayloadはString。 |

同じState名への変更、空文字、Component不在はfalseで、Actionも発生しない。自動遷移、遷移表、Exit Time、StateごとのUpdate、視覚グラフは持たない。必要ならC++ Script、AI State、Timeline、条件Componentを組み合わせる。State文字列は大文字小文字を区別する。

### Attribute（表示名: 属性・リソース）

**目的:** Health以外の連続値を共通管理する。Boost、Heat、Shield、Fuel、Special Gauge等に使う。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| 属性名 | string、`Resource` | 利用者向け識別名。現在の専用APIはOwnerのAttributeを対象にする。 |
| 最小 | float、0.0 | 下限。 |
| 最大 | float、100.0 | 上限。最小未満ならPlay開始時に最小へ補正する。 |
| 現在 | float、100.0 | Play中の現在値。毎Frame範囲内へClampする。 |
| 毎秒回復 | float、0.0/秒 | `current += regeneration * deltaTime`。負数なら毎秒消費になる。 |
| Action対象 | GameObject、未設定時Owner | 値変更通知先。 |
| 変更Action | string、`OnAttributeChanged` | 値が変わったFrameに通知する。Payloadは変更後のFloat。 |

`Attribute::Set`は範囲へClampするが、DamageReceiverや無敵時間は通らない。Healthの代替ではない。複数Attributeが必要なら属性ごとに子GameObjectを用意するか、ゲーム側Data Asset/Scriptで名前付き集合を管理する。毎秒回復とScript更新を同時に使う場合は二重加算に注意する。

### DestructiblePart（表示名: 破壊可能部位）

**目的:** Healthが0以下になった部位と、その部位に紐づく見た目・武器・子Objectを汎用的に無効化してActionを送る。スコア、爆発、Boss Phase変更はAction受信Scriptへ置く。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| Health Source | GameObject、未設定時Owner | 監視するHealth所有Object。 |
| 無効化Component (;区切り) | string、空 | Owner上の内部Component名。例: `ProjectileEmitter;TargetSelector`。空白を入れず完全一致させる。 |
| 子Objectを無効化 | bool、true | 直下の子をActive falseにし、描画と物理を同時に止める。孫は親Inactiveの扱いに従う。 |
| 破壊済み | Runtime表示 | Play開始時false。最初の破壊処理後trueとなり二重発火を防ぐ。 |
| Action対象 | GameObject、未設定時Owner | 破壊通知先。 |
| 破壊Action | string、`OnPartDestroyed` | Payloadに破壊された部位GameObjectを入れる。 |

必要構成は有効なHealth SourceとHealth Componentである。無効化名は日本語表示名ではなく`ProjectileEmitter`等の内部名を使う。Owner自体は自動無効化しないため、残骸表示、Collider維持、爆発後のPool返却をAction側で選べる。Healthを後から回復しても自動修復しない。

### FormationFollower（表示名: 編隊追従）

**目的:** Leaderの姿勢で回転したLocal Offset位置へObjectを追従させる。横一列、V字、護衛、追従カメラ等に使えるが、敵、攻撃、Waveの概念は持たない。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| Leader | GameObject、未設定 | 追従基準。必須。 |
| ローカルOffset | Vector3、(0,0,0) m | Leader回転を反映してWorld Offsetへ変換する。 |
| 位置追従速度 | float、8.0/秒 | 位置補間率。0以下は即時追従。 |
| 回転追従速度 deg/s | float、180.0 | Euler回転補間率。0以下は即時追従。 |
| 回転を追従 | bool、true | falseなら位置だけ追従する。 |

RuntimeはTransformを可変`Update`で書き換える。Dynamic Rigidbody、RailMovement、Animation等が同じTransformを書くObjectへ直接追加しない。物理編隊が必要ならFormationFollowerで目標Poseを得る設計へ拡張し、PhysicsServoやForce制御へ渡す。Leader未設定または削除済みなら何も更新しない。

### TargetLock（表示名: ターゲットロック）

**目的:** TargetSelectorが同一Targetを一定時間選び続けた時にLock完了とする。候補探索、距離、角度、遮蔽、Team判定はTargetSelectorへ任せる。

| Inspector項目 | 型・初期値 | Runtimeでの意味 |
| --- | --- | --- |
| TargetSelector | GameObject、未設定時Owner | Current Targetを読むTargetSelector所有Object。 |
| Lock時間 | float、0.75秒 | 同一Targetを維持する必要時間。 |
| 喪失猶予 | float、0.25秒 | 候補が一時的に消えても進行を保持する時間。 |
| 進行率 | Runtime表示、0-1 | `経過秒 / Lock時間`。 |
| Locked | Runtime表示、bool | 完了後true。 |
| Action対象 | GameObject、未設定時Owner | 開始・完了・解除通知先。 |
| 開始Action | string、`OnLockStarted` | 新Targetへ切り替わった時。GameObject Payload。 |
| 完了Action | string、`OnLockCompleted` | 進行率が1へ到達した時に1回。GameObject Payload。 |
| 解除Action | string、`OnLockLost` | 喪失猶予を超えた時。失ったGameObject Payload。 |

Targetが別Objectへ直接切り替わった場合は旧Targetの解除Action、新Targetの開始Actionの順に送り、進行率を0へ戻す。Lock時間0は新Target取得Frameで即時完了する。Targetが一時的に消え、猶予内に同じTargetへ戻れば進行を継続する。ミサイル発射可否は`TargetLock::GetState`の`isLocked`とTarget参照を確認してゲームScript側で決める。

### 型付きAction PayloadのComponent側契約

| 発生元 | Payload Type | 値 |
| --- | --- | --- |
| GenericStateMachine 変更Action | String | 変更後State名。 |
| Attribute 変更Action | Float | 変更後Current。 |
| DestructiblePart 破壊Action | GameObject | 破壊された部位Object。 |
| TargetLock 開始/完了/解除Action | GameObject | 対象または失ったTarget。 |
| Timer 発火Action | None | 時刻情報は`Timer::GetRemaining`またはComponent設定から読む。 |

受信Scriptは`EditorScriptInputActionContext::payloadType`を先に確認し、一致するMemberだけを読む。Input Action由来の`buttonValue`へGameObject IDや汎用値を詰めない。

## 複数Target・HUD・汎用ゲームデータComponent

### MultiTargetLock（表示名: 複数ターゲットロック）

TargetSelectorと同じ距離、角度、Layer、Team、遮蔽、選択Priorityを使って候補一覧を作り、最大64件まで個別の進行率を保持する。HierarchyへTarget Slotを手動作成しない。

| Inspector項目 | 初期値 | Runtimeでの意味 |
| --- | --- | --- |
| TargetSelector | Owner | 候補条件の参照元。 |
| 最大Lock数 | 8 | 保持するTarget上限。 |
| 1体のLock時間 | 0.35秒 | 各Targetが完了するまでの時間。 |
| 喪失猶予 | 0.25秒 | 遮蔽や範囲外を許容する時間。 |
| 候補を自動取得 | true | 空きSlotへ優先順で追加する。 |
| 追加/Lock完了/解除Action | 各既定名 | GameObject Payloadで個別Targetを通知する。 |

Poolで同じGameObject IDが再利用される場合は解除Action後に再取得される。発射後に何を解除するか、同じ敵を再Lock可能にするかはゲームScript側で決める。

### WorldTargetMarker / OffScreenIndicator

Image、RawImage、Text、TextMeshProUGUIと同じGameObjectへ追加する。明示Target、TargetSelector、TargetLock/MultiTargetLockの順で参照を解決し、Game CameraのView ProjectionでCanvas座標へ変換する。

| Inspector項目 | 意味 |
| --- | --- |
| 明示Target | 固定Target。設定時は他参照より優先。 |
| TargetSelector | Current Targetを追従する。 |
| TargetLock | 単一Lockまたは複数LockのTargetを追従する。 |
| Multi Lock番号 | MultiTargetLockを参照したときに追従する0始まりのTarget番号。複数のUIへ0、1、2を設定すると各Lockを別々に表示できる。 |
| World Offset | 弱点や頭上表示用のWorld補正。 |
| Screen Offset | 投影後のCanvas補正。 |
| 画面端余白 | IndicatorのClamp余白。 |
| カメラ後方を隠す | 後方Targetを非表示にする。 |
| Lock完了時だけ表示 | 未完了Lockを隠す。 |
| Target方向へ回転 | OffScreen Imageを方向へ回転する。 |

WorldTargetMarkerは画面外で非表示、OffScreenIndicatorは画面内で非表示になる。Canvas/Imageを自動生成するComponentではないため、見た目は通常のUI部品で自由に設計できる。

### AttributeSet（表示名: 属性セット）

同一GameObject上へ`Boost`、`Heat`、`Shield`、`SpecialGauge`等を可変数で保持する。各EntryはName、Minimum、Maximum、Current、Regeneration/秒を持つ。子GameObjectへResourceを分散しない。NameはOwner内で一意にする。変更ActionのPayloadは変更されたNameのStringで、値は`AttributeSet::Get`から取得する。

### GenericCounter（表示名: 汎用カウンター）

撃破数、残敵数、部位破壊数、コンボ等をfloatで保持する。初期値、Clamp範囲、閾値、比較演算`>= <= == > < !=`、一回発火を設定できる。変更Actionと閾値Actionは変更後値のFloat Payloadを持つ。Score計算規則や敵全滅の意味は持たない。

### GenericCondition（表示名: 汎用条件）

Runtime Float/Int/Bool、AttributeSet、Counter、Object Active、Generic State、Target Lockedを比較する。毎Frame評価またはC++からの明示評価を選べる。成立/不成立Actionは結果変化時だけ、または評価ごとに通知できる。複数条件のAND/ORやゲーム固有分岐はActionSequenceまたはScriptで構成する。

### GameplayData（表示名: ゲームプレイデータ）

`.gdata` AssetまたはInline EntryからKey/Type/Valueを読み込む。TypeはString、Int、Float、Bool、Asset Path。Play開始時にAssetを読み込み、正常なEntryが1件以上あればInline値を置き換える。武器、敵、Upgradeという固定Schemaはエンジンへ埋め込まず、ゲーム側がKeyを定義する。

## Damage・Projectile・Runtime再利用Component

### AreaDamage（表示名: 範囲ダメージ）

**目的:** OwnerのWorld位置を中心とする球内を1回Overlapし、Health単位で重複を除去して距離減衰DamageとImpulseを適用する。爆発の見た目、音、スコア、敵種別は持たない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 半径 | float、5.0 m | 0以下では適用対象なし。Overlap Sphereの半径。 |
| 基礎Damage | float、50.0 | 中心でのDamage。HitZone、DamageReceiver、DamageTag倍率はこの後に適用する。 |
| 端の最低倍率 | float、0.0 | 半径端でも残す倍率。0-1へ制限する。 |
| Impulse | float、0.0 | 中心から外向きへ与える瞬間力。Damageとは独立して0にできる。 |
| 距離減衰 | 一定 / 線形 / SmoothStep | 一定は全域1、線形は距離比、SmoothStepは急変を抑えた減衰。 |
| Layer Mask | int、全Layer | Physics Layerのbit mask。対象候補を早期除外する。 |
| Damage Tag | string、`Explosion` | 固定Enumではない。FNV-1aで安定IDへ変換しDamageContext.userTagへ入る。 |
| 発生元を除外 | bool、true | Instigatorと同じGameObjectを除外する。 |
| Play開始時に実行 | bool、false | Startで1回だけ適用する。通常の弾はProjectileDetonatorから呼ぶ。 |
| Action対象 / 適用Action | Owner / `OnAreaDamageApplied` | 1体以上へ適用できた時、適用数をInt Payloadで通知する。 |

Colliderが複数ある同一Health対象へ二重Damageを与えない。HitZoneがHealthを別Objectへ転送する場合も転送先IDで重複除去する。`AreaDamage::Apply`は適用対象数を返し、0はComponent不在、範囲内Healthなし、全対象除外を含む。

### HitZone（表示名: ヒットゾーン）

**目的:** 命中ColliderのOwnerから実際のHealth所有ObjectへDamageを転送し、艦橋、装甲、エンジン等の部位倍率を適用する。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| Health対象 | GameObject、Owner | 未設定ならOwner。設定時はDamageContext.targetGameObjectIdをこのIDへ置き換える。 |
| 部位Damage倍率 | float、1.0 | 0ならDamage無効、1なら等倍。DamageReceiver倍率とDamageTag倍率へ乗算する。 |

HitZoneはCollider、Health、破壊演出を自動生成しない。部位ColliderのGameObjectへ追加し、Health対象を本体または部位Healthへ接続する。部位ごとに独立破壊する場合はHealth対象側へHealthとDestructiblePartを置く。

### DamageTagModifier（表示名: ダメージタグ倍率）

**目的:** `Bullet`、`Explosion`、`Missile`等の任意文字列Tagごとに耐性・弱点倍率を設定する。Tag集合をEngineのEnumへ固定しない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 未登録Tag倍率 | float、1.0 | 一致EntryがないDamageに使う。 |
| Tag | UTF-8 string | Owner内で意味を一意にする。空Entryは通常使わない。 |
| 倍率 | float、1.0 | 0で無効、0.5で半減、2で2倍。 |
| Damage Tagを追加 / 削除 | 可変配列 | 子GameObjectを作らずComponent内部へ保存する。 |

比較には`DamageTag::Id()`と同じ安定ハッシュを使う。HitscanWeapon、ProjectileEmitter、AreaDamageのDamage Tag入力と綴り・大文字小文字を一致させる。最終Damageは`baseDamage * HitZone倍率 * DamageReceiver倍率 * DamageTag倍率`である。

### ProjectileDetonator（表示名: 弾起爆装置）

**目的:** ProjectileEmitterが生成したPool Itemへ接触、近接、寿命切れ、C++手動の起爆条件を追加し、AreaDamageへ接続する。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 接触時起爆 | bool、true | Continuous SphereCastの最初の命中位置で起爆する。 |
| 近接時起爆 | bool、false | Targetまでの距離が近接半径以下で起爆する。 |
| 寿命切れ時起爆 | bool、false | Projectile Lifetime終了位置で起爆する。 |
| 近接Target | GameObject、未設定 | 明示Targetを優先し、未設定時はTargetSteering / TargetSelectorのTargetを使う。 |
| 近接半径 | float、2.0 m | 0なら位置一致時だけ。 |
| AreaDamage | GameObject、Owner | 起爆時に呼ぶAreaDamage所有Object。Ownerへ同居できる。 |
| Action対象 / 起爆Action | Owner / `OnProjectileDetonated` | Hit GameObjectをGameObject Payloadで通知する。命中対象なしは空参照。 |

直接弾DamageとAreaDamageは独立である。爆発だけにしたい弾はProjectileEmitterの直接Damageを0へ設定する。手動起爆はActive Projectileだけ成功し、起爆後はPoolへ返却してActive一覧から除去する。

### ThreatTracker（表示名: 脅威トラッカー）

**目的:** 監視対象へ接近するActive Projectileを、距離、接近速度、最接近予測時間で抽出する。HUD描画や回避判断そのものは持たない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 監視対象 | GameObject、Owner | 自機、護衛対象等。 |
| 最大距離 | float、500.0 m | 遠方Projectileを除外する。 |
| 最低接近速度 | float、1.0 m/s | 離れている弾、ほぼ静止した弾を除外する。 |
| 最大逸れ距離 | float、10.0 m | 現速度を維持した時のClosest Approachがこれを超える弾を除外する。 |
| 最大脅威数 | int、8、1-64 | 到達予測時間の短い順に保持する。 |
| 現在脅威数 | Runtime表示 | Play中の抽出件数。 |
| 追加 / 解除Action | `OnThreatAdded` / `OnThreatLost` | ProjectileをGameObject Payloadで通知する。 |

Runtime一覧はComponent内部の可変配列であり、脅威ごとの子GameObjectは作らない。`ThreatTracker::GetEntries()`はそのFrameのsnapshotを返すため、次FrameやPool返却後にProjectile参照が有効とは限らない。

### RuntimeStateReset（表示名: 実行状態リセット）

**目的:** Pool Item再貸出時とC++明示Reset時に、複数Runtime Componentを同じ初期状態へ戻す契約を1箇所へ集約する。

| Inspector項目 | 初期値 | Reset対象 |
| --- | --- | --- |
| Health | true | Health値、死亡状態、Damage履歴。 |
| State Machine | true | GenericStateMachineをInitial Stateへ戻す。 |
| Attribute / Counter | true | Attribute、AttributeSet、GenericCounterをPlay開始値へ戻す。 |
| Target Lock | true | TargetLock、MultiTargetLockのTarget、進行、猶予を消去する。 |
| Timer | true | DurationとPlay On Startに従って戻す。 |
| 破壊可能部位 | true | 破壊済みをfalseへ戻し、無効化した子とComponentを再有効化する。 |
| Cooldown | true | 各Entryの開始時使用可能設定へ戻す。 |
| Action対象 / Reset Action | Owner / `OnRuntimeStateReset` | Reset完了後にOwnerをGameObject Payloadで通知する。 |

Componentがない項目は無視する。RuntimeStateReset自体がないPool Itemは互換動作として全対応項目をResetする。Scene編集値は変更せず、Play中状態だけを初期化する。

### CooldownSet（表示名: クールダウンセット）

**目的:** Boost、特殊兵器、回避等の独立Cooldownを同一GameObjectへ可変数保持する。Timerごとの子GameObjectを要求しない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 名前 | UTF-8 string | Owner内の検索Key。重複させない。 |
| 時間 | float、1.0秒 | `Start(name)`で使用する既定時間。 |
| 開始時使用可能 | bool、true | trueは残り0、falseは既定時間から開始。 |
| 残り | Runtime表示 | Updateで0まで減算する。 |
| 完了Action | `OnCooldownCompleted` | 完了した名前をString Payloadで通知する。 |

`Start(name, -1.0f)`は既定時間、0以上のoverrideはその秒数を使う。`Reset(name)`は残り0へして使用可能にする。再実行は残り時間を先頭から上書きする。Entry追加・削除は直接配列へ保存され、Hierarchyは増えない。

### 新規7 Componentの保存・受入条件

7 Componentは既存の巨大なComponent行へ列追加せず、それぞれ専用Extension行へ保存する。Scene保存、別Sceneを開く、元Sceneを再度開く、Play開始・停止の順で可変Tag/Cooldown EntryとGameObject参照が復元されることを確認する。この時点の基準はComponent 239件である。

## 武器発射・命中応答・時間制御Component

### WeaponFirePattern（表示名: 武器発射パターン）

**目的:** HitscanWeaponまたはProjectileEmitterの1回の発射要求を、複数の実Shotへ展開する。Damage、弾Prefab、Aim、弾薬は既存Weapon側の責務であり、Patternへ複製しない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| Mode | Single | Single、Burst、Salvo、Spread、Sequence、Charge。 |
| Count | int、3 | Burst/Salvo/Spread/SequenceのShot数。1-64へ制限する。 |
| Interval | float、0.1秒 | Burst/SequenceのShot間隔。Salvo/Spreadは同Frame。 |
| Spread Angle | float、8度 | Spread全体の左右角度。各Shotを均等配置する。 |
| Charge Seconds | float、0.75秒 | Charge要求から実Shotまでの待機時間。入力長判定ではない。 |
| Spawn Points | GameObject可変配列 | Sequenceが順番に使う。配列末尾では先頭へ循環する。 |
| Action対象 / 完了Action | Owner / `OnFirePatternCompleted` | 最後の予定Shotを処理した時にButton値1で通知する。 |

`Weapon::FireHitscan`と`Weapon::FireProjectile`はPatternの有無を自動判定する。Patternなしでは従来どおり1発となる。Pattern最終Shotまでの時間へ基礎Weaponの発射間隔を加えた値をCooldownとして使い、同じSequenceの途中と直後へ別要求を重ねない。Chargeは「要求後に遅延して発射」であり、長押し中断、Charge Gauge、段階DamageはゲームScriptまたはAttribute/PropertyTweenで構成する。

### TargetAssignment（表示名: ターゲット割り当て斉射）

**目的:** MultiTargetLockの可変Target一覧を読み、Projectileを1Targetにつき1発生成し、TargetSteeringとProjectileDetonatorへ同じTarget IDを割り当てる。Target Slot用の子GameObjectは作らない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| MultiTargetLock | GameObject、Owner | 未設定時はProjectileEmitter Ownerから検索する。 |
| 最大Target数 | int、8 | 先頭から最大64件。 |
| 発射間隔 | float、0.1秒 | 0なら同Frame一斉、正なら順次発射。 |
| Lock完了だけ | bool、true | falseならLock進行中Targetも割り当てる。 |
| Action対象 / 完了Action | Owner / `OnTargetAssignmentCompleted` | 最後の割当Shot処理時に通知する。 |

Lock対象が0件またはMultiTargetLock不在の場合はWeaponFirePatternへFallbackする。各弾のPool生成に失敗しても列の残りは継続する。発射後にLockを消すか、再Lockを許すかはゲームルールなので自動変更しない。

### WeaponAccuracy（表示名: 武器命中精度）

**目的:** Weapon方向へ、移動量と連射蓄積を含むCone Spreadを加える。HitscanとProjectileで同じ計算を使う。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| Base Spread | float、0度 | 静止・初弾でも常に加える角度。 |
| Maximum Spread | float、12度 | Baseと蓄積Spreadの上限。 |
| Spread Per Shot | float、0.5度 | 実Shotごとの蓄積量。発射要求単位ではない。 |
| Recovery / Second | float、3度/秒 | Game Timeで蓄積Spreadを0へ戻す。 |
| Movement Spread | float、0 | Rigidbody速度1 m/s当たりの追加角度。 |
| Distribution | Uniform Cone / Disk / Center Weighted | 半径分布。Center Weightedは中心へ寄せる。 |
| Current Spread | Runtime表示 | Pool Reset、Play開始で0。C++から取得可能。 |

乱数はPlay開始ごとに同じSeedへ戻すため、同じ入力列を再現しやすい。Spread PatternのYawを先に加え、その周囲へAccuracy Coneを加える。弾ごとに独自の`Random` Scriptを要求しない。

### WeaponRecoil（表示名: 武器反動）

**目的:** 1実ShotからPhysics Impulse/Torque、砲身等の表示Offset、CameraShake、Actionを同時に発生させる。

| Inspector項目 | 初期値 | Runtime契約 |
| --- | --- | --- |
| Body Impulse | (0,0,-1) | Weapon OwnerのRigidbodyへ瞬間力。 |
| Body Torque | (0,0,0) | Weapon Ownerへ瞬間Torque。 |
| Visual Target | 未設定 | 砲身、Slide等。Ownerと分離できる。 |
| Position / Rotation Offset | (0,0,-0.1) / (0,0,0) | Shotごとに加算し、残差を管理する。 |
| Recovery / Second | 8 | 現在差分に対する毎秒復帰率。Stop/Pool Resetで残差を除去する。 |
| Camera Shake | 未設定 | 参照先CameraShakeを再生する。 |
| Action対象 / Action | Owner / `OnWeaponRecoil` | Shotごとに通知する。 |

CameraShakeの振幅・時間はCameraShake Component側で設定する。RecoilはCamera、砲身、Rigidbodyを自動生成しない。Visual TargetとPhysics Bodyを同一Objectへ設定して二重移動させない。

### SurfaceType（表示名: サーフェスタイプ）

Colliderが属するGameplay SurfaceをUTF-8文字列Tagで表す。初期値は`Default`。`Metal`、`Water`、`Wood`等を固定Enumへせず、ゲーム側で追加できる。命中ColliderにSurfaceTypeがない場合は親をRootまで検索し、それでもなければ`Default`を返す。

### ImpactResponder（表示名: 命中応答）

**目的:** Weapon命中時にDamage TagとSurface Tagを照合し、Effect、Audio、Decal、CameraShake、Actionを選ぶ。最初に一致したEntryだけを実行するため、具体的条件を上、空欄Wildcardを下へ置く。

| Entry項目 | 契約 |
| --- | --- |
| Damage Tag / Surface Tag | 空欄はWildcard。Damage TagはDamageContextと同じ安定Hash、Surfaceは文字列一致。 |
| Effect Asset | 命中Object基準の命中位置Offsetへ1回再生する。 |
| Audio Source | 指定AudioSourceを命中位置へ移して再生する。3D音響設定はAudioSource側。 |
| Decal Object | 指定Objectを命中位置で有効化する。寿命、法線方向の高度な投影はDecal側またはScript。 |
| Camera Shake | 指定CameraShakeを再生する。 |
| Action対象 / Action | Hit ObjectをGameObject Payloadで通知する。 |

海面へSurfaceType=`Water`、船体Rootへ`Metal`を設定すれば、同じProjectileEmitterで水柱と火花を分けられる。ImpactResponderはDamage適用後に動くが、演出失敗でDamageを取り消さない。

### TimeScale（表示名: 時間倍率・ヒットストップ）

**目的:** Globalなゲーム更新時間を一時的に0-4倍へ変更する。入力取得とTimeScale自身の復帰は非スケール時間、Script、移動、物理、武器、Effect、Audio、Camera演出はスケール時間を受ける。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| Scale | float、0 | 0でHitStop、0.2でSlow Motion、1で通常。 |
| Duration | float、0.1秒 | 非スケール時間で測るためScale 0でも必ず復帰する。 |
| Blend Seconds | float、0 | 開始ScaleからSmoothStep補間。Duration以下へ制限する。 |
| Play On Start | bool、false | Play開始時の最初の有効TimeScaleだけを開始する。 |
| Action対象 / 完了Action | Owner / `OnTimeScaleCompleted` | 1倍へ復帰した後にFloat Payload 1を通知する。 |

同時に複数再生した場合は最後の要求が前の状態を置き換える。Pause Menuの永続停止とは分離し、短い演出用に使う。C++では`TimeScale{owner}.HitStop(0.08f)`または`Play(scale,duration)`を呼ぶ。

### 新規7 Componentの保存・受入条件

各Componentは専用Extension行で保存する。Component配列内に可変Entryを持つWeaponFirePatternとImpactResponderもHierarchyへ展開せず復元する。この章の追加開始時点はComponent 246件、Runtime API Entry 172件であり、この章の完了時点ではComponent 268件、Runtime API Entry 211件だった。

## 照準・Mission・Encounter・Difficulty Component

この章の8 Componentは水上レールシューティングだけへ固定しない。照準入力、迎撃計算、Mission状態、複数Wave進行、生成地点選択、Property一括適用、Camera揺れ合成という汎用責務だけを持つ。

### AimAssist（表示名: 照準補助）

**目的:** `ScreenAim`へ入ったプレイヤー入力を残したまま、`TargetSelector`の現在Targetが補助半径内にある時だけ照準を弱く引き寄せる。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 画面照準 | GameObject、Owner | 有効なScreenAimを参照する。 |
| Target Selector | GameObject、Owner | 現在TargetとTargetPoint位置を取得する。 |
| 補助半径 | float、0.12 | Game View正規化座標。0.12は画面短辺の約12%。 |
| 補助強度 | float、0.35 | Target方向へ寄せる基本倍率。0で無効、1で最大。 |
| 追従速度 | float、8 | 1秒当たりの補間速度。Delta Timeを掛ける。 |
| 入力中の抑制 | float、0.5 | Mouse移動量またはStick入力が大きいほど補助を弱める。 |

処理順は`ScreenAim生入力 -> TargetSelector更新 -> AimAssist補正 -> Reticle UI更新`である。Game View CameraのViewProjectionでTargetを投影し、背面Target、範囲外Target、無効Targetでは補正しない。AimAssistはTargetを選ばず、射撃も行わない。

### InterceptPrediction（表示名: 迎撃予測）

**目的:** 発射元World位置、Target位置・速度、Projectile速度から、一定速度の非誘導弾が到達できる未来位置と到達秒を解析計算する。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 明示Target | GameObject、なし | 設定時はSelectorより優先する。 |
| Target Selector | GameObject、Owner | 明示Target未設定時のTarget供給元。 |
| Projectile速度 | float、100 | 0以下では解なし。 |
| 最大予測秒 | float、10 | 遠すぎる解を棄却する上限。 |
| Runtime | 読取専用 | 有効/解なし、予測位置、到達秒を保持する。 |

Target速度は継承RigidbodyのVelocityを使う。重力落下、加速度、Drag、Target加速度は含めないため、弾道砲はC++ Script側で追加補正する。`Targeting::GetInterceptPrediction`はそのFrameの解だけを返し、解なしならfalseである。

### DamageDirectionIndicator（表示名: 被弾方向表示）

**目的:** 最後に受けた`DamageContext`のSource方向を、Game Camera基準の正規化2D方向とFade値へ変換する。HUD Imageそのものは生成しない。

| Inspector項目 | 型・初期値 | Runtime契約 |
| --- | --- | --- |
| 表示秒 | float、1.5 | 新しいDamageを受けると先頭へ戻る。 |
| Fade秒 | float、0.4 | 残り時間がこの値以下になった区間でAlphaを0へ近づける。 |
| 最低Damage | float、1 | これ未満のDamageContextは表示を更新しない。 |
| 画面端半径 | float、0.45 | HUD側が画面中心から配置する正規化半径。 |
| 残り秒 | 読取専用 | 0で非表示。 |

有効CameraのうちPriority最大のYawを使い、右方向を`(+1,0)`、画面上方向を`(0,-1)`として返す。Source GameObjectが既に破棄済み、Damageが閾値未満、DamageManager未接続なら更新しない。HUDは`DamageDirectionIndicator::Get`のDirectionへ画面端半径を掛けて配置する。

### ObjectiveTracker（表示名: 目標トラッカー）

**目的:** 任意IDのObjectiveについて`Inactive / Active / Completed / Failed`、現在値、目標値を可変配列で保持する。撃破、護衛、収集などの意味は持たない。

| 項目 | 契約 |
| --- | --- |
| ID | C++とActionで参照する一意文字列。大文字小文字を区別する。 |
| 表示名 | HUDやLocalization側で利用する表示用文字列。 |
| 状態 | 0=無効、1=進行中、2=完了、3=失敗。 |
| 現在値 / 目標値 | Activeかつ目標値が正で、現在値が目標値以上になるとCompletedへ遷移する。 |
| 変更Action | String PayloadへObjective IDを入れて通知する。 |

EntryはComponent内部へ保存し、Objectiveごとの子GameObjectを要求しない。失敗条件、複合条件、報酬、表示順、LocalizationはゲームScriptまたはUI側の責務である。

### EncounterController（表示名: エンカウンター制御）

**目的:** 複数`WaveSpawner`を、開始前Delayと完了待機方式に従って順番に開始する。

| Inspector項目 | 契約 |
| --- | --- |
| Play開始時に実行 | trueならPlay開始時にEntry 0から開始する。falseならC++の`EncounterController::Start`で開始する。 |
| Wave Spawner | 開始するWaveSpawner GameObject。Wave側の開始条件は`外部開始`を推奨する。 |
| 開始前待機 | 前Wave完了後から次Wave開始までの秒数。 |
| 全撃破を待つ | trueは全生成Objectの撃破/Pool返却、falseは全生成完了で次へ進む。 |
| 完了Action | 全Entry終了時に1回通知する。 |

Encounterは敵AI、攻撃、勝敗、Scoreを知らない。不正なWave参照はCrashせず次Entryへ進む。再度Startした場合はEncounter進行を先頭へ戻し、各Waveの生成番号と完了Flagも外部開始時にResetする。

### SpawnPointSet（表示名: 生成地点セット）

**目的:** Spawn位置をHierarchy子の個数へ依存させず、Component内部の可変参照配列またはVolumeから選ぶ。

| Mode | 動作 |
| --- | --- |
| 順番 | 登録順で循環する。 |
| ランダム | 全有効地点を等確率で選ぶ。 |
| 重み付き | Weightの正値合計を使って選ぶ。全Weight 0なら等確率へFallbackする。 |
| Volume | Owner World位置を中心にVolume Size内のランダム位置を返す。 |

`直前を避ける`は候補2件以上のランダム/重み付きで同じ地点の連続選択を防ぐ。WaveSpawnerと同じGameObjectに置くと各生成の基準位置へ自動適用され、C++では`SpawnPointSet::Resolve`を直接使える。無効参照は候補から除外し、全候補無効ならfalseである。

### DifficultyParameterSet（表示名: 難易度パラメーターセット）

**目的:** Easy/Normal/Hard等の名前付きIndexに対して、複数GameObjectの登録済みRuntime Propertyを一括適用する。

| Inspector項目 | 契約 |
| --- | --- |
| 難易度名 | 可変配列。最低1件を維持する。 |
| 選択Index | Play開始適用またはC++ Applyで使うIndex。 |
| Play開始時に適用 | trueならRuntime Manager開始時に1回適用する。 |
| Override | 難易度Index、対象、Component名、Property名、Float/Int/Bool値。 |
| 適用Action | String Payloadへ適用した難易度名を入れる。 |

Property名はRuntime Property Registryと完全一致させる。未登録Propertyだけを無視し、他の有効Overrideは継続する。元値のsnapshotや難易度解除は行わず、別IndexのApplyで上書きする。

### CameraFeedbackMixer（表示名: カメラフィードバックミキサー）

**目的:** 武器反動、爆発、被弾、着水など複数Camera Shakeの同時再生を1箇所で制限・合成する。

| Inspector項目 | 契約 |
| --- | --- |
| 最大位置振幅 / 最大回転振幅 | 合成後Offsetを軸別にClampする。 |
| 最大同時数 | 上限到達後の新規Shake要求をfalseにする。最低1。 |
| 合成 | 加算、または最高Priorityの1 Shakeだけを使用する。 |
| 全体強度 | Clamp前の合成結果へ掛けるGlobal倍率。 |

Scene内の最初の有効MixerをGlobal設定として使う。Camera Shakeには個別`Priority`があり、最高Priority Modeで比較する。同Priorityが重なった場合は後から評価したShakeが優先される。MixerはShake波形を生成せず、既存Camera Shakeの結果だけを調停する。

### 保存・受入条件

8 Componentはそれぞれ専用Extension行でSceneへ保存する。Objective、Encounter、SpawnPoint、Difficultyの可変EntryはHierarchyへ展開せずComponent内部で復元する。Camera Shake Priorityは旧`GameplayFoundation`行末へ任意列として追加し、旧Sceneでは0へFallbackする。

| 試験 | 合格条件 |
| --- | --- |
| Aim入力競合 | Stick/Mouse操作中は補助が弱まり、入力停止後だけ設定強度へ戻る。 |
| 迎撃不能 | 弾速不足、Targetなし、最大秒超過でfalseかつNaNを残さない。 |
| 被弾方向 | Cameraを90度回してもHUD左右がCamera基準で一致する。 |
| Objective | Active値が閾値を跨いだFrameだけCompletedへ変わり、String Payloadが届く。 |
| Encounter | 外部開始WaveがDelay、全生成/全撃破設定どおり順次進む。 |
| Spawn選択 | 順番、乱数、Weight、Volumeと直前回避を再生中に確認できる。 |
| Difficulty | 複数対象へ型一致Propertyを適用し、不正Propertyで他Entryを中断しない。 |
| Camera Mixer | 同時数、軸Clamp、加算、Priority Mode、Global強度が個別に効く。 |
| Scene再読込 | 全参照、可変Entry、文字列、Flag、Camera Shake Priorityが一致する。 |

この章追加後の機械照合基準はComponent 274件、Runtime API Entry 214件である。

## レールシューティング制作支援Component

### RailSpeedProfile（表示名: レール速度プロファイル）

**目的:** `RailMovement`の基準速度へ、レール進行率に応じた倍率を連続的に掛ける。直線、カーブ、演出区間ごとに`RailMovement::速度`を手動更新しない。

| Inspector項目 | 型 / 既定値 | Play時の動作 |
| --- | --- | --- |
| プロファイルを使用 | bool / true | falseなら倍率1.0として扱う。Component自体のActiveとは別にScriptから切替可能。 |
| 速度キー[] | 可変配列 / 0:1.0、1:1.0 | 進行率と速度倍率の組。隣接キーを線形補間する。 |
| 進行率 | float / 0～1 | Rail全長に対する正規化位置。範囲外は0～1へ制限する。 |
| 速度倍率 | float / 1.0 | 負値は0として扱う。0ならその地点で目標速度0になる。 |

Inspectorの`進行率順に並べる`でキーを整列できる。Runtimeも探索時に進行率順として評価するため、編集時に順序を揃える。キーが0件、Component無効、`プロファイルを使用=false`の場合は倍率1.0である。`RailZone`が同時に有効なら、最終倍率は`SpeedProfile倍率 x Zone倍率`になる。

### RailZone（表示名: レール区間）

**目的:** 正規化したレール区間へ名前を付け、区間中だけ速度倍率と左右・上下可動範囲を変更し、進入・退出をScript Actionへ通知する。

| Inspector項目 | 型 / 既定値 | Play時の動作 |
| --- | --- | --- |
| Action対象 | GameObject / Owner | 進入・退出Actionを受けるC++ Script Object。 |
| Zone ID | string / ZoneN | ActionのString Payloadと`RailFollower::GetActiveZone()`で返す識別子。 |
| 開始 / 終了進行率 | float / 0～0.25 | 大小を逆に入力しても小さい方から大きい方までを区間にする。 |
| 速度倍率 | float / 1.0 | SpeedProfile評価後へ乗算する。 |
| 移動範囲を上書き | bool / false | trueの区間だけRailMovementの左右・上下Clamp範囲を一時変更する。 |
| 左右・上下範囲 | Vector2 / 5,3 | `MovementModifier`ではなくRail入力Offsetの範囲を制限する。 |
| 進入 / 退出Action | string | Zone切替時に一度だけ通知する。 |

複数Zoneが重なる場合は配列で先に見つかった1件を採用する。区間外へ出たFrameで退出Action、次区間へ入った同じFrameで進入Actionを順にQueueする。敵全滅、Boss開始、BGM変更などの意味は持たず、受信ScriptまたはAction接続が決める。

### CameraFollowComposer（表示名: カメラ追従コンポーザー）

**目的:** Cameraの角度固定追従を置き換え、対象のYaw、速度先読み、注視点、独立減衰、Pitch/Roll安定化を1つのCamera追従姿勢へ合成する。

| Inspector項目 | 型 / 既定値 | Play時の動作 |
| --- | --- | --- |
| 追従対象 | GameObject / Camera接続先 | 明示参照がなければCamera Componentの接続先を使用する。 |
| 追従Offset | Vector3 / 0,3,-8 | 対象Yawを継承する場合は対象の左右・前後へ回転する。 |
| 注視Offset | Vector3 / 0,1,6 | Cameraが見る対象基準位置。 |
| 位置 / 回転減衰 | float / 6,8 | `1-exp(-damping*deltaTime)`でFPS非依存に追従する。 |
| 速度先読み秒 | float / 0.25 | RigidbodyまたはRailMovementのWorld速度を注視点へ加える。 |
| デッドゾーン | Vector2 / 0.15,0.1 | 目標位置変化が範囲内ならCamera位置を維持する。 |
| 1Frame最大追従距離 | float / 20 | TelePort等で1Frameに追従する最大World距離。0なら制限しない。 |
| 対象Yawを継承 | bool / true | falseならOffsetをWorld固定として扱う。 |
| Pitch/Rollを安定化 | bool / true | Camera Rollを0へ保ち、船体Rollを画面へ直接コピーしない。 |

最高PriorityのCameraに付ける。CameraBlend再生中はBlendを優先し、終了後にComposerが追従を再開する。CameraShakeは最終Offsetとして別途加算される。CameraのTransform自体を毎Frame保存値へ書き戻さず、Play中のGame View Camera Overrideだけを更新する。

### SpeedFeedback（表示名: 速度フィードバック）

**目的:** 実速度を0～1へ正規化し、FOV、Motion Blur、Camera Feedback強度へ変換して疾走感を作る。

速度SourceはDynamic Rigidbodyを優先し、なければRailMovementのRuntime velocityを使う。最小速度以下は0、最大速度以上は1で、間は線形補間後に応答速度で平滑化する。対象Camera未設定時は最高Priority Cameraを使う。FOVとMotion Blurは最小値から最大値へ補間し、Camera Shake強度は`1 + normalized x Camera強度加算`を既存Mixer強度へ乗算する。

### SpawnedObjectSetup（表示名: 生成オブジェクト設定）

**目的:** `WaveSpawner`所有Objectへ1つ配置し、Poolまたは互換子方式で生成した全個体へ共通のRail、開始位置、速度、Team、Reset設定を適用する。

適用順は`Pool貸出/子Active化 -> Runtime Reset -> Rail/Team設定 -> Rail進行と編隊Offset初期化 -> 適用完了Action`である。開始進行率は`base + step x spawnIndex`を0～1へClampする。Rail速度倍率はTemplateの初回基準速度へ掛け、Pool再利用のたびに倍率を累積しない。Team Componentがない個体へTeam Componentを暗黙追加しない。

### WaveMotionProfile（表示名: ウェーブ移動プロファイル）

**目的:** `WaveSpawner`が生成した全個体へ、編隊の基準Offsetを保ったまま周期運動を加える。

| Mode | 動作 |
| --- | --- |
| なし | WaveSpawnerの編隊Offsetだけを使う。 |
| Sine | 左右をsin、上下をcosで動かす。 |
| 8の字 | 左右をsin、上下を2倍位相のsinで動かす。 |
| 交互運動 | spawnIndexの偶奇で左右・上下位相を反転する。 |

振幅は左右・上下、周波数はHz、位相差は個体Indexごとのradian、Blend Inは生成直後の振幅立ち上げ秒である。攻撃、Target選択、Damage、敵AIは持たない。`RailMovement::SetOffset`を使うため、Railの左右上下範囲によるClampは維持される。

### 保存と受入確認

6 Componentは独立Extension行へ保存し、既存Component行の列順を変えない。Runtime位置、Runtime回転、現在Zone Index、速度正規化値、生成後経過時間はSceneへ保存しない。

1. Rail速度キー0%、50%、100%で目標速度が連続補間される。
2. Zone進入・退出Actionが1回ずつ発生し、String PayloadがZone IDと一致する。
3. Cameraが旋回する船の後方を保ち、船体Roll時も水平線安定化が働く。
4. Rigidbody速度とRail速度の両方で同じSpeedFeedback範囲を評価できる。
5. Pool再利用100回後もRail速度倍率が累積しない。
6. Wave Spawn Countを増やしてもHierarchyへ敵の手動子設定を追加せず、全個体へ設定と運動が適用される。

## Camera追従・RailMovement船体推進の詳細

### Camera / CinemachineCamera追従

CameraとCinemachineCameraは同じ追従設定を持つ。`追従対象`は`connectedGameObjectId`へ保存し、Game Viewが採用したCamera Transformを作る時だけ追従Poseへ解決する。Scene View Cameraや追従対象のTransformは変更しない。

| Inspector項目 | 内部値 | 既定値 | Runtime動作 |
| --- | --- | --- | --- |
| 追従対象 | `connectedGameObjectId` | -1 | TargetのWorld Transformを参照する。Owner自身、無効参照、非Activeは追従しない。 |
| 位置オフセット基準 | `cameraFollowPositionSpace` | 0 | 0=World固定、1=Target Local。Camera GameObjectのTranslateをOffsetに使う。 |
| 回転方式 | `cameraFollowRotationMode` | 0 | 0=Camera角度固定、1=Target回転継承、2=Targetを見る。 |
| Camera Transform位置 | `translate` | Component外 | 追従時はOffset。零Vectorなら`(0,2,-6)`へFallbackする。 |
| Camera Transform回転 | `rotate` | Component外 | Mode 0ではWorld角度、Mode 1/2では追加回転Offset。 |

Target Local位置はTargetのWorld Euler回転から作った回転行列でOffsetを変換する。回転継承は`Target World Rotation + Camera Local Rotation`、Look AtはCameraからTargetへの正規化方向からPitch/Yawを求めた後にCamera回転Offsetを加える。Look距離が極小なら現在Camera回転を保持する。

`プレイヤー追従プリセット`は位置`(0,2,-6)`、回転0、対象Local、対象回転継承を設定する。既存Sceneは`CameraFollowExtension`がなければ0/0で初期化され、従来のWorld Offset・固定角度を維持する。

### RailMovementの3方式

| `railMovementMode` | 表示名 | 更新場所 | 制御 |
| --- | --- | --- | --- |
| 0 | Transform追従 | Update | Spline FrameとOffsetからWorld Transformを直接設定する。 |
| 1 | Dynamic Rigidbody 物理サーボ | FixedUpdate | Spline接線速度と先読み目標位置へPD Force/Torqueを加える。 |
| 2 | Dynamic Rigidbody 船体推進 | FixedUpdate | 船首World方向へEngine Forceを加え、Spline接線へYaw Torqueで操舵する。 |

Mode 1と2はDynamicかつ非KinematicなRigidbodyを必要とする。先読みRail位置へRail OffsetとMovementModifierを適用し、その結果を共通Target Positionにする。これにより左右移動入力が表示だけで終わらず、物理追従Forceへ反映される。

船体推進の追加設定:

| Inspector項目 | 内部値 | 既定値 | 動作 |
| --- | --- | --- | --- |
| 船首ローカル軸 | `railLocalForwardAxis` | 0 | 0=+Z、1=-Z、2=+X、3=-X。Modelの船首方向と一致させる。 |
| 推力を水平にする | `railShipHorizontalThrust` | true | 船首World方向のYを0にして再正規化する。 |
| 横ずれ補助率 | `railShipLateralAssist` | 0.2 | 0～1。Rail横方向の位置・速度PD補正へ掛ける。 |

Mode 2の前進加速度は、船首方向の現在速度とRail Runtime目標速度の差から求める。Spline接線をForce方向へ直接使わないため、船が曲線へ入るとYaw操舵後に推力方向が変わる。横ずれ補助率0では自然な旋回遅れを優先し、1ではRail拘束を優先する。最大加速度と最大角加速度は既存Clampを共通利用する。

Buoyancy併用では位置追従軸`(1,0,1)`、回転追従軸`(0,1,0)`を推奨する。RailはXZとYaw、BuoyancyはYとPitch/Rollを担当する。同じ軸を両方が強く制御する設定は振動の原因になる。

### 保存と検証

Rail追加値は既存`RailMovementExtension`の任意末尾列へ保存する。Camera追加値は`CameraFollowExtension|OwnerId|ComponentType|PositionSpace|RotationMode`へ保存する。旧Sceneの短いRail行とCamera Extensionなしを受理し、既定値へFallbackする。

検証では、+Z/-Z/+X/-X Modelの推力方向、Forward/Reverse、Pause、Loop、Offset入力、MovementModifier、親Transform、Buoyancy、Scene再読込を個別に確認する。Cameraは親付きTarget、Target非Active、回転継承、Look At、複数Camera Priority、CameraHorizonStabilizerとの責務競合を確認する。

この変更の記録時点では既存Componentの設定追加だけであり、Component 268件、Runtime API Entry 211件を維持していた。

## ParticleSystem / VisualEffect Billboard描画仕様

### 責務と描画経路

ParticleSystem / VisualEffectはSpawn時にBillboard ModeとStretchをGPU Particle Dataへ複製する。`EditorGpuParticleManager::Draw`は描画対象ViewのView Matrixを受け取り、逆行列からCamera Right / UpをRoot Constantsへ設定する。`Particle.VS.hlsl`はその基底、Particle Velocity、Particle Rotationから板のWorld Positionを構築する。

描画処理はParticleのPosition、Velocity、Lifetimeを更新しない。更新はEffect ManagerとCompute Shader、描画はGPU Particle ManagerとVertex Shaderへ分離する。Scene ViewのCamera MatrixをGame Viewへ流用せず、描画PassごとのView Matrixを使う。

| Mode | GPU値 | 基底 | Degenerate時 |
| --- | ---: | --- | --- |
| Camera Facing | 0 | Camera Right / Camera Up。 | 常に有効な既定基底を使う。 |
| Y軸固定 | 1 | Camera RightをXZへ射影、UpはWorld Y。 | 水平方向が退化した時はCamera Facingへ戻す。 |
| Velocity Facing | 2 | VelocityをCamera Planeへ射影してUp、RightはCamera Forwardとの外積。 | 射影速度がほぼ0ならCamera Facingへ戻す。 |
| World XY固定 | 3 | World X / World Y。 | 旧固定Plane描画としてそのまま使う。 |

Velocity FacingのStretchは板の速度方向軸だけへ掛ける。負値や0は受け付けず、Inspector、Effect Asset読込、Runtime Property、Spawn変換の各境界で0.01以上へ制限する。Particle Rotationは決定済みの2軸をPlane内で回転し、Camera Facingでも粒ごとの回転差を保持する。

### 保存、Asset、Runtime Property

Sceneは`ParticleBillboardExtension|Owner ID|Component Type|Mode|Stretch`へ保存する。Owner IDだけでなくParticleSystem / VisualEffectのComponent Typeも照合し、同一GameObjectに両Componentがある場合の誤適用を防ぐ。古いSceneにExtensionがなければMode 0、Stretch 1.0で初期化する。

`.effect` JSONは`billboardMode`と`billboardStretch`を任意項目として読む。省略時は既定値を維持する。Runtime Propertyは両Componentの`BillboardMode` intと`BillboardStretch` floatを公開する。専用Runtime API Entryは増やさない。

FBX / OBJ Render Assetが空でない場合は`ParticleModel.VS.hlsl`を使い、ModelのWorld Transformを保持する。Root Signature互換のためCamera基底Constantsは受け取るが、Model Vertex計算では使用しない。

### Debugと受入条件

| 確認 | 合格条件 |
| --- | --- |
| Root Constants | Billboard / Model両PSOが同じ28個の32-bit Constant layoutを使う。 |
| GPU Layout | C++の`GpuParticleData`とHLSLの`ParticleData`でorientation位置とfloat4 Sizeが一致する。 |
| Multi View | Scene / Game各Drawが対象View Matrixを渡し、別Cameraへ正対する。 |
| Mode境界 | Modeは0～3、Stretchは0.01以上へ制限される。 |
| Shader Compile | Billboard VS、Model VS、Clear / Spawn / Update CSがDXCでCompile成功する。 |
| Save / Load | ParticleSystemとVisualEffectを同一Ownerへ置いても各値が混線しない。 |

この機能の追加時点では既存2 Componentの拡張であり、その時点の機械照合基準はComponent 268件、Runtime API Entry 211件だった。

## 攻撃判定・砲塔・艦砲運用・Camera安定化Component詳細

### AttackCollisionFilter（表示名: 攻撃コリジョンフィルター）

**目的:** HitscanとProjectileが発射者自身、発射者の子Collider、味方、明示除外Objectへ誤命中することを防ぐ。Target選択用Team Filterとは分離し、実際の攻撃Castへ同じ規則を適用する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Instigator | -1 | -1ならFilter所有ObjectまたはWeapon所有Objectを発射者として使う。DamageContextのInstigatorにも渡す。 |
| Instigatorを無視 | true | 発射者自身のPhysics BodyをRay/Sphere Cast候補から除く。 |
| Instigator階層を無視 | true | 発射者の親子階層に属するBodyを除く。砲口が艦艇の子である構成を想定する。 |
| Team Rule | Different Team | `Any / Different Team / Same Team`。Teamを持たない地形や構造物は通常の衝突候補として残す。 |
| Neutralを無視 | false | Team ID=-1として設定されたObjectを除く。Team Component自体がないObjectはNeutral扱いにしない。 |
| Arming Distance | 1.0 m | Projectileが発射位置からこの距離を進むまでは攻撃Castを行わない。負値は0として扱う。 |
| Ignore Objects[] | 空 | 明示GameObjectと、そのBodyを除外する可変配列。Hierarchyへ補助Objectを作らない。 |

FilterはWeaponまたはその親階層から検索する。Projectile生成時に必要な除外ID、Team規則、Arming Distanceを飛翔中データへ複製するため、発射後にFilterを変更しても既に飛んでいる弾の規則は変わらない。Physics HitとOcean Hitを比較する前にPhysics Castへ適用し、Ocean判定自体は除外しない。

**危険な落とし穴（Instigator=-1の階層解決）:** `BuildAttackIgnoredGameObjects()`はInstigatorが-1のとき、「このFilter Componentを持つGameObject自身」（＝Weaponが子GameObjectとして持っている場合はそのWeapon）を発射者として解決する。親であるPlayerShip側までは自動的に遡らない。さらに「Instigator階層を無視」はこの解決済みInstigatorから**下方向**（`IsInHierarchy`で子孫を辿る方向）だけを除外し、Instigatorの**祖先**は一切除外しない。したがって、武器がPlayerの子GameObjectとして配置され、かつInstigatorが-1のままだと、除外対象は「Weapon自身とその子」だけになり、Weaponの親であるPlayerShip自身は除外対象に含まれず、自弾が発射元のPlayerへ命中し得る。回避策は、Weapon側のInstigatorへ明示的にPlayerShipなど「本来無視したい階層のルートGameObject」を設定すること。子搭載Weapon構成では既定値(-1)に任せず必ず明示設定を確認する。
根拠: `EditorWeaponManager.cpp` `BuildAttackIgnoredGameObjects()`。

### TurretAim（表示名: 砲塔照準）

**目的:** Yaw台座とPitch砲身を分離してTargetへ向け、可動範囲、旋回速度、到達可能判定、照準完了判定を共通化する。Weapon発射、弾薬消費、Target選択規則は持たない。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| 明示Target | -1 | 0以上ならTargetSelectorより優先する。 |
| Target Selector | -1 | 明示Target未設定時に現在Targetを読む。-1ならOwner上のTargetSelector。 |
| Yaw Pivot | -1 | -1ならOwner。ローカルY回転だけを更新する。 |
| Pitch Pivot | -1 | -1ならYaw Pivot。ローカルX回転だけを更新する。 |
| Yaw範囲 | -180～180 deg | 最小・最大を正規化してTarget角をClampする。 |
| Pitch範囲 | -10～75 deg | 砲身の仰俯角。Targetが範囲外なら端まで向けるがCanReachTarget=false。 |
| Yaw/Pitch速度 | 90/60 deg/s | Frame Deltaで角速度制限し、瞬間回転させない。 |
| 照準許容角 | 2 deg | Yaw/Pitch両誤差が範囲内でIsAimed=true。 |
| Target予測秒 | 0 | Targetまたは親階層のRigidBody速度を一定速度として未来位置へ加える。弾道解はBallisticPredictionを使う。 |

Runtime出力はCurrent Target、Can Reach Target、Is Aimed、Yaw Error、Pitch Errorである。C++ Scriptでは`TurretAim::GetState`で一括取得できる。砲塔の`IsAimed`を確認してからWeaponGroupを発射することで、回転処理と攻撃規則を分離できる。

### WeaponGroup（表示名: 武器グループ）

**目的:** 複数のHitscanWeaponまたはProjectileEmitterを一つの砲撃単位として発射する。主砲塔A/B/C、左舷副砲列、複数ランチャーをHierarchyへ専用Managerとして埋め込まず、参照配列で構成する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Weapons[] | 空 | Weapon GameObject IDとEntry有効Flagの可変配列。無効・欠落参照は飛ばす。 |
| Mode | Simultaneous | `Simultaneous / Sequential / RoundRobin`。 |
| Interval | 0.1 s | Sequentialで次Weaponへ進む間隔。0なら同Frameで処理できる。 |
| Require All Ready | true | 発射前に全有効WeaponのCooldown等を確認し、一つでも未準備ならGroup全体を開始しない。 |
| Completion Action | 空 | Group処理完了時に通知する。Sequentialは最後の予約Shot処理後に送る。 |

Simultaneousは全有効Entryへ同Frameに要求し、SequentialはMain thread上の予約Queueへ積み、RoundRobinは前回Indexの次から発射可能な1 Weaponを選ぶ。WeaponFirePattern、WeaponAccuracy、ProjectileEmitter、WeaponRecoil等の既存合成順は各Weapon側で維持する。GroupはDamage、Ammo、Target割当を所有しない。

### ProjectileImpactPhysics（表示名: 弾体貫通・跳弾）

**目的:** ProjectileのPhysics Hit後に即消滅するか、残存Energyで貫通・跳弾して飛翔を継続するかを決める。HitZoneのDamage倍率やDamageTag耐性とは別に、弾体の進行状態を管理する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| 有効 | false | falseなら従来どおり最初のPhysics Hitで停止する。 |
| 初期Energy | 1.0 | 発射時の残存Energy。0以下では貫通・跳弾しない。 |
| 貫通損失 | 0.5 | Surface補正後の固定損失をEnergyから引く。 |
| 最大貫通数 | 0 | 0なら貫通しない。貫通したObjectは以後のCast除外へ追加する。 |
| 跳弾角 | 75 deg | 表面法線に対して浅い入射で跳弾候補にする。 |
| 最大跳弾数 | 0 | 0なら跳弾しない。 |
| 貫通/跳弾速度保持率 | 0.65/0.75 | 継続後の速度へ掛ける0～1倍率。Damageも残存Energy比で減衰する。 |
| Surface Modifiers[] | 空 | Surface Tagごとの貫通損失倍率、跳弾角Offset、Energy保持倍率。 |

命中時はImpact、Damage、命中Actionを通常どおり発生させ、その後に跳弾、貫通、停止の順で継続可否を決める。これは装甲の実厚を出口Castで測る物理シミュレーションではなく、Surfaceごとの固定Energy損失を使うゲーム向けモデルである。Ocean Hitは水面Impactとして停止し、このComponentによる水面跳弾は行わない。

### CameraHorizonStabilizer（表示名: 水平線スタビライザー）

**目的:** Buoyancy等で大きくPitch/Rollする船体をCameraが完全継承することを避け、波の揺れを残しながら画面酔いを抑える。Camera Shakeは別経路で最終加算する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Follow Source | -1 | 追従する船体等。未設定または自分自身なら更新しない。 |
| Local Position Offset | (0,2,-6) | SourceのWorld行列で変換したCamera目標位置。 |
| Position Follow | true | falseなら現在World位置を保持し、回転だけ補正する。 |
| Pitch/Yaw/Roll Inheritance | 0.35/1.0/0.2 | Source姿勢を軸別に0～1で継承する。 |
| World Up | (0,1,0) | 水平線を決めるWorld方向。ゼロVectorはY UpへFallbackする。 |
| Rotation Offset | (0,0,0) deg | 継承後に加えるCamera向き。 |
| Damping | 8 | 指数補間の追従速度。0以下は即時反映。 |
| Maximum Roll | 8 deg | 最終Camera Rollの絶対上限。 |

SourceのForwardへWorld Upを射影してRollを求め、軸継承率と最大Rollを適用する。World TransformとしてCameraへ戻すため、Cameraに親がある場合も親空間へ正しく逆変換される。描画処理内では更新せず、Constraint更新段階だけで姿勢を変更する。

### 保存・実行順・受入条件

5 Componentは`AttackCollisionFilterExtension`、`TurretAimExtension`、`WeaponGroupExtension`、`ProjectileImpactPhysicsExtension`、`CameraHorizonStabilizerExtension`で保存する。Current Target、照準誤差、Group Queue、RoundRobin Index、Projectile残存Energyと命中回数はRuntime値でありSceneへ保存しない。

| 試験 | 合格条件 |
| --- | --- |
| 自艦誤爆 | 艦艇Root、砲塔子、砲口孫にColliderがあっても発射直後に自艦へ命中しない。 |
| Team | Different/Same/Any、Neutral除外、Teamなし地形が個別に期待どおり判定される。 |
| Arming | 砲口直後はCastせず、指定距離を越えた区間から連続Castを再開する。 |
| Turret | Yaw/Pitch別Pivot、可動端、速度、予測、到達不能、許容角が独立して機能する。 |
| Group | 3 Mode、無効Entry、Require All Ready、Completion Action、連続要求が破綻しない。 |
| Impact | 最大回数、Surface補正、速度/Damage減衰、跳弾方向、最終Detonatorが一致する。 |
| Horizon | 船体Rollを0/一部/全部継承し、Position FollowとMaximum Rollが独立する。 |
| Save/Load | 全GameObject参照、可変配列、文字列、数値、FlagがScene再読込後に一致する。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件だった。

## 距離最適化・レールイベントComponent詳細

この章の3 Componentは敵AIやステージ規則を持たない。`DistanceActivation`と`SimulationLOD`はScene内に既に存在するObjectの実行負荷を距離で制御し、`RailEventMarker`はRail進行率の通過を名前付きActionへ変換する。制作導線は`Project -> Sceneを開く -> Hierarchyで対象を選ぶ -> InspectorからComponentを追加 -> Play`である。

### DistanceActivation（表示名: 距離アクティベーション）

**目的:** Cameraまたは明示Objectとの距離に応じて、Ownerと必要なら子階層を実体化・休止する。遠距離の敵、港湾小物、破片、演出RootをHierarchyへ常時Activeで残したまま全更新する問題を避ける。

| Inspector項目 | 型 / 既定値 | 動作 |
| --- | --- | --- |
| 距離基準 | GameObject / 未設定 | 明示ObjectのWorld位置を使う。未設定または参照切れなら、有効なCamera / Cinemachine CameraのうちPriority最大のObjectを使う。 |
| 有効化距離 | float / 250.0 | 休止中にこの距離以下へ入ると元のActive状態へ復帰する。 |
| 無効化距離 | float / 300.0 | 実体化中にこの距離を超えると休止する。Inspectorは有効化距離以上へClampする。 |
| 子階層も対象 | bool / true | trueならOwnerのchildrenを再帰処理する。falseならOwnerだけを切り替える。 |
| Runtime | 読み取り表示 | `実体化`または`休止`。Scene保存しない。 |

有効化距離と無効化距離を分けるHysteresisにより、境界付近で毎Frame Active/Inactiveが往復することを防ぐ。復帰時はPlay開始時にInactiveだったObjectを勝手にActiveへしない。Physics Simulationも同じ実体状態へ接続し、Objectを非表示にしただけでRigidBody計算だけが残る状態を作らない。

距離基準Cameraが存在しない場合は開始時状態を維持する。Cameraを生成するComponentではない。Prefab生成そのものや遠距離状態のデータだけを保持する仮想化は担当せず、既にSceneまたはPoolへ存在するObjectを休止・復帰するComponentである。

### SimulationLOD（表示名: シミュレーション LOD）

**目的:** 参照点との距離を`Near / Medium / Far / Culled`へ分類し、Farでは選択した重いComponent系統だけを停止し、CulledではObject実体を停止する。描画MeshのポリゴンLODはRenderer側のLOD機能であり、このComponentはCPU処理、Physics、Animation、Effectの実行LODを担当する。

| Inspector項目 | 型 / 既定値 | 動作 |
| --- | --- | --- |
| 距離基準 | GameObject / 未設定 | DistanceActivationと同じ解決規則を使う。 |
| Medium距離 | float / 100.0 | この距離以上でRuntime Level=1。現行では分類値だけを変え、Component停止は行わない。Scriptが更新頻度や品質選択へ利用できる。 |
| Far距離 | float / 250.0 | この距離以上でRuntime Level=2。下記のFar停止設定を適用する。 |
| Culled距離 | float / 500.0 | この距離以上でRuntime Level=3となり、Ownerまたは子階層を休止する。 |
| FarでPhysics停止 | bool / true | Physics Managerの対象Body Simulationを停止する。Collider設定を削除しない。 |
| FarでScript停止 | bool / false | Script / MonoBehaviourのActiveをPlay開始時状態から一時的にfalseへする。 |
| FarでAI停止 | bool / true | AI系ComponentのActiveを一時停止する。 |
| FarでAnimation停止 | bool / true | Animator / Animation / PlayableDirectorを一時停止する。 |
| FarでEffect停止 | bool / true | ParticleSystem / VisualEffect / LensFlare / TrailRendererを一時停止する。 |
| 子階層も対象 | bool / true | 同じLOD段階をchildrenへ再帰適用する。 |
| Runtime LOD | 読み取り表示 | 0=Near、1=Medium、2=Far、3=Culled。Scene保存しない。 |

NearまたはMediumへ戻ると、Play開始時に保存したGameObject ActiveとComponent Activeへ復元する。Play停止でも同じ保存値へ戻すため、最適化による一時停止をScene編集値として残さない。Far停止はComponentそのものを削除せず、設定値、可変Entry、Script公開値を保持する。

同じ親子範囲へ複数のDistanceActivation / SimulationLOD Controllerを重ねると、親と子が別基準でActiveを要求できる。基本は編隊Root、建物Root、Effect Rootなど制御単位ごとに1つ置き、子に別Controllerを置く場合は`子階層も対象=false`で責務範囲を分ける。

**ObjectPoolのTemplateと組み合わせる際の注意:** ObjectPoolのTemplate GameObjectを画面外・地下などの待機位置に置く構成では、参照点からの距離が`Far`/`Culled`を超えてSimulationLODが自動的にPhysicsやGameObject自体を停止させることがある。`FarでPhysics停止`はBodyのSimulation ON/OFFだけを切り替えるため単体では実害がないが、`ObjectPool::CreatePoolItem()`がTemplateを複製するタイミングでTemplate自身の`isActive`がfalseになっていると、複製されたPool個体も`isActive=false`のまま生成される。`EditorJoltPhysicsManager::RegisterRuntimeGameObject()`は`isActive=false`のGameObjectに対してPhysics Bodyを一切作らずスキップするため、後から`SetItemActive(true)`で見た目上Activeへ戻しても、そもそもBodyが存在しないため`SetGameObjectSimulationActive`は何もできず、Ray/ShapeCastへ永久に反応しない個体が生まれる（詳細は「オブジェクトプール / プレハブ生成」節、根拠: `EditorObjectPoolManager.cpp` `CreatePoolItem()`、`EditorJoltPhysicsManager.cpp` `RegisterRuntimeGameObject()`）。回避策として、Template配置位置はSimulationLODのFar/Culled距離より内側にするか、Templateに専用のSimulationLODを付けず常時Active維持する。現行実装では`CreatePoolItem()`が複製直後に`duplicatedGameObject->isActive = true`へ強制することでこの問題自体を回避しているため、通常利用では意識しなくてよい。

### RailEventMarker（表示名: レールイベントマーカー）

**目的:** RailMovementの0～1進行率がMarkerを横切ったFrameにActionを1回Queueし、Marker IDをString Payloadとして渡す。敵生成、BGM、会話、Boss開始などの意味は持たず、受信C++ ScriptまたはAction接続がゲーム規則を実行する。

| Inspector項目 | 型 / 既定値 | 動作 |
| --- | --- | --- |
| Action対象 | GameObject / このObject | Marker Actionを受けるScript Object。未設定ならRailMovement Owner。 |
| Marker ID | string | Payloadへ格納する識別子。例: `WaveA`、`CameraTurn01`。大文字小文字を区別する。 |
| 進行率 | float / 0.5 | 0～1。1FrameでMarkerを飛び越えても前回値と現在値の区間で判定する。 |
| 通過方向 | 両方向 / 順方向 / 逆方向 | RailのReverse状態と一致する方向だけ通知する。 |
| Play中1回だけ | bool / true | trueなら通知後にRuntime通知済みとなる。falseならLoopや往復で再通過するたび通知する。 |
| Action | string / `OnRailMarker` | 対象Scriptの`BindAction`名。空なら通知しない。 |
| Runtime | 読み取り表示 | 未通知 / 通知済み。Scene保存しない。 |

MarkerはComponent内部の可変配列であり、Markerごとの子GameObjectは作らない。`Markerを追加`、`このMarkerを削除`、`進行率順に並べる`で編集する。Loop Railでは1.0から0.0、逆方向では0.0から1.0のWrap区間も通過判定する。Play開始時の現在進行率は基準値として初期化し、その地点のMarkerを開始直後に誤発火させない。

`Play中1回だけ`のMarkerをゲーム規則から再利用する場合は、C++の`RailFollower::RearmEventMarkers("MarkerId")`を呼ぶ。空文字なら全Markerを再Armする。同じMarker IDが複数ある場合は一致する全Entryが対象になる。

### 保存・Lifecycle・受入条件

3 Componentは`DistanceActivationExtension`、`SimulationLodExtension`、`RailEventMarkerExtension`へ保存する。距離のRuntime Active、LOD Runtime Level、前回進行率、通知済みFlagはPlay Runtime値なので保存しない。GameObject参照はDuplicate、Scene保存・再読込時にRemapする。

| 試験 | 合格条件 |
| --- | --- |
| 距離境界 | 250/300設定なら301で休止し、299へ戻しただけでは復帰せず、250以下で復帰する。 |
| Active復元 | Play前Inactiveの子やComponentがNear復帰またはStopで勝手にActiveにならない。 |
| Simulation Far | 選択した系統だけ停止し、Renderer設定やHealth値を破壊しない。 |
| Simulation Medium | Runtime Levelだけ1になり、現行実装でComponent Activeを変更しない。 |
| Marker飛び越し | 高速移動で前回0.2、今回0.4になった時、0.3 Markerが1回通知される。 |
| Loop / Reverse | Wrap境界と逆方向で、通過方向設定どおりにだけ通知する。 |
| Action Payload | 受信Scriptが`EditorScriptActionPayloadTypeString`とMarker IDを取得できる。 |
| Stop | 最適化で変更したObject、Component、Physics状態とMarker Runtime Flagが編集状態へ戻る。 |

この章の追加時点の機械照合基準はComponent 277件、Runtime API Entry 216件、C++ Script Template 26件だった。

## 弾道・被弾履歴・Pause・航跡・軌道表示Component

### BallisticPrediction（表示名: 弾道予測）

**目的:** 一定速迎撃とは分離し、重力、線形Drag、Target速度、任意Target加速度を含む発射方向と軌道点列を求める。

| Inspector項目 | 型・既定値 | Runtime契約 |
| --- | --- | --- |
| Target | GameObject / -1 | 0以上ならSelectorより優先する。 |
| Target Selector | GameObject / -1 | Target未指定時に現在Targetを読む。-1ならOwnerのTargetSelector。 |
| 初速 | float / 80 | 0以下は解なし。Projectile実速度と一致させる。 |
| 重力 | Vector3 / (0,-9.81,0) | World加速度。Physics Sceneの重力を暗黙参照しない。 |
| 抗力 | float / 0 | `dv/dt = gravity - drag * velocity`の線形Drag。負値は0。 |
| Target加速度 | Vector3 / 0 | Targetの将来位置だけへ使う一定加速度仮定。 |
| 最大予測秒 | float / 12 | この時間まで初速一致解を探索する。 |
| 計算刻み | float / 1/60 | 0.001～0.25秒へClampする。 |
| 最大点数 | int / 128 | 2～2048へClampし、表示/Script用点列を制限する。 |

出力は`Valid`、正規化済み`LaunchDirection`、`ImpactPosition`、`FlightTime`、`TrajectoryPoints`である。毎Frame Target状態から再計算し、失敗時はValid=falseかつ点列をClearする。Damage、Projectile生成、発射Queue、砲塔回転は責務外である。

### DamageEventBuffer（表示名: 複数被弾履歴）

**目的:** 最後の1Damageだけを扱うDamageDirectionIndicatorの上位データ源として、同時被弾を有限件数・有限寿命で保持する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| 最大件数 | 8 | 1～64。超過時は最古Entryを削除する。 |
| 表示秒 | 1.5 | Entryの寿命。最低0.01秒。 |
| 最小Damage | 1 | `appliedDamage`が未満なら追加しない。 |
| 同じ攻撃元を統合 | true | Source IDが同じEntryへDamageを加算し、方向・Tag・寿命を更新する。 |

DamageManagerが倍率、HitZone、耐性を適用した直後に記録する。方向はTargetからSourceへのWorld正規化方向で、Source不明時は命中法線の逆を使う。Start、Stop、Pool ResetでEntryをClearする。HUD Imageの生成と配置は責務外である。

### GamePause（表示名: ゲーム一時停止）

**目的:** 時間制限付きTimeScaleとは分け、明示Resumeまで続くゲームPauseを入力、物理、音声へ伝播する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| ゲーム時間を停止 | true | RuntimeのゲームDeltaを0にする。非スケール時間は維持する。 |
| Physicsを停止 | true | Physics固定StepとScript FixedUpdateを実行しない。 |
| Audioを停止 | true | XAudio2 Voiceを破棄せずStopし、ResumeでStartする。 |
| Gameplay Map | Gameplay | Pause時に抑止するPlayerInput ActionMap名。 |
| UI Map | UI | Pause中も更新するPlayerInput ActionMap名。 |
| Pause/Resume Action | 任意文字列 | 状態変更時だけBool Payloadを通知する。 |

Scene内で同時に1 OwnerだけをGlobal Pause所有者として扱う。別OwnerをPauseすると旧Ownerを解除する。Script UpdateとUI入力はResume要求を処理するため継続する。Pause Menuの見た目や選択状態はUI側の責務である。

### SurfaceWakeEmitter（表示名: 水面航跡エミッター）

**目的:** 船速と共通Ocean Sampleから、左右航跡と船首飛沫の既存Particle/VisualEffectを制御する。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Ocean | -1 | 指定Oceanを優先し、-1は位置を覆うOceanを検索する。 |
| 左/右/船首Effect | -1 | ParticleSystemまたはVisualEffect所有GameObject。 |
| 開始速度 / 最大速度 | 0.5 / 20 | `(speed-min)/(max-min)`を0～1へClampする。 |
| 幅 | 1.5 | Particle Sizeへ反映する。 |
| 寿命 | 4 | Particle Lifetimeへ反映する。 |
| 最大発生数 | 80 | Rateへ強度を掛ける。船首だけ1.25倍。 |

RootのWorld水平位置差分をDeltaで割った速度からOcean Surface Velocityを引き、水面に対する相対水平速度を求める。上下動だけでは航跡を増やさず、FFTの砕波・圧縮泡率は相対速度から求めた強度を0.85～1.15倍する。Effect点のWorld X/Zは子階層やゲーム側配置に任せ、YだけをOcean波面へ合わせる。強度0との境界でのみPlayEffect/StopEffectを呼び、無駄な再起動を行わない。局所FFT、流体Wake、物理的造波抵抗は責務外である。

### TrajectoryRenderer（表示名: 軌道プレビュー）

**目的:** BallisticPredictionの計算結果を変更せず、Scene ViewとGame Viewへ線と着弾円を重ねる。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| 予測元 | -1 | -1ならOwner、0以上なら参照先のBallisticPrediction。 |
| 色 / Alpha | (1,0.55,0.1) / 0.9 | ImGui Overlay色。 |
| 太さ | 2 | 最低0.5 pixel。 |
| 最大表示点数 | 128 | 2～2048。予測点列自体は変更しない。 |
| Scene/Game表示 | true/true | Viewごとに独立。 |
| 着弾点表示 | true | ImpactPositionへ円を描く。 |

Scene ViewはScene Camera、Game ViewとStandaloneはGame CameraのViewProjectionを使う。Camera背面および極端なNDCの線分は描画しない。LineRenderer GameObject、Material、Meshを要求しない。

### 保存・初期化・受入条件

5 Componentは`BallisticPredictionExtension`、`DamageEventBufferExtension`、`GamePauseExtension`、`SurfaceWakeEmitterExtension`、`TrajectoryRendererExtension`で保存する。軌道点、被弾Entry、Pause状態、現在速度・強度はRuntime値でありPlay開始時に初期化する。

| 試験 | 合格条件 |
| --- | --- |
| Ballistic | 重力0/あり、Drag0/あり、静止/移動Target、解なしでNaNを残さない。 |
| Damage Buffer | Source統合、別Source併存、寿命削除、最大件数、Pool Resetが独立する。 |
| Pause | 時間、Physics、Audioを個別Flagどおり止め、UI MapだけでResumeできる。 |
| Wake | Speed閾値、Ocean Y追従、Effect欠落、Owner非ActiveをCrashなしで処理する。 |
| Trajectory | Scene/Game Flag、色、Alpha、太さ、点数、着弾円が独立して効く。 |
| Save/Load | 全編集値と参照が一致し、Runtime配列と状態を保存値として再利用しない。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件だった。

## FFT海面ゲーム判定Component詳細

### WaterSurfaceState（表示名: 水面出入り状態）

**目的:** Owner上の一点とFFT水面の上下関係を継続監視し、水面進入・水中・水面離脱を汎用状態として公開する。魚雷、着水弾、沈没物、潜水物、船体Effect切替に利用し、Damageや弾種等のゲーム固有責務は持たない。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Ocean | -1 | -1なら判定点を覆う有効Oceanを自動検索する。 |
| ローカル判定位置 | (0,0,0) | OwnerのWorld SRTで変換した位置をSampleする。 |
| 出入り余白 | 0.05 | 水面付近の状態振動を抑える非負距離。 |
| Action通知先 | -1 | -1ならOwnerのC++ Script。 |
| 進入Action | OnWaterEntered | AboveからUnderwater側へ越えた時に一度通知する。 |
| 離脱Action | OnWaterExited | UnderwaterからAbove側へ越えた時に一度通知する。 |

Runtime出力はState、Signed Distance、Ocean GameObject ID、Surface Position、Normal、Velocity、Foamである。Foamは0～1の砕波・圧縮泡率で、Inspectorの`砕波・泡率`とC++の`GetFoam`から取得する。State値は`0=AboveWater`、`1=EnteringWater`、`2=Underwater`、`3=LeavingWater`。EnteringとLeavingは遷移Frameだけ保持し、次の更新で安定状態へ移る。

Play開始時に初回Sampleを行い、既に水中ならUnderwaterから開始する。この初期判定では進入Actionを送らない。Sample失敗時はOcean IDを-1にし、前回の安定状態を不用意に反転しない。進入・離脱Actionの型付きPayloadはGameObjectで、値は検出したOcean IDである。

### OceanProbeSet（表示名: 海面前方プローブ）

**目的:** 一つのOwnerから同方向に並ぶ複数距離のFFT水面情報をまとめて更新する。波予測、船体姿勢補助、AI評価、Camera演出へ幾何データを提供し、`大波`等の意味は決定しない。

| Inspector項目 | 既定値 | Runtime契約 |
| --- | --- | --- |
| Ocean | -1 | -1なら各Probe地点ごとにOceanを検索する。 |
| ローカル原点 | (0,0,0) | Owner位置へ加えるローカルOffset。 |
| ローカル方向 | (0,0,1) | Owner回転を反映して正規化する。ゼロに近い場合は前方を使う。 |
| Probe距離[] | 10, 25, 50 | 可変Entry。0以上のWorld距離として扱う。 |

各Runtime EntryはDistance、Valid、Surface Position、Normal、Velocity、Relative Height、Foamを持つ。Relative Heightは`SurfacePosition.y - ProbeOrigin.y`、Foamは0～1の砕波率であり、船底高さやCamera高さを含めたゲーム判定はC++ Script側で行う。Entry追加・削除はComponent内の配列操作であり、距離ごとの子GameObjectをHierarchyへ作らない。

### 既存Componentとの接続

| Component | 追加項目 | 動作 |
| --- | --- | --- |
| TargetSelector | 遮蔽方式 / Ocean Clearance | None、Physics、Ocean、Bothを選び、波面が候補との線分を遮る場合は候補から除外する。 |
| HitscanWeapon | FFT水面へ命中 | Physics RayとOcean Rayの近いHitだけを採用する。 |
| ProjectileEmitter | FFT水面へ命中 | 前Frame位置から現在位置までの区間でPhysics/Oceanの近いHitだけを採用する。 |
| ImpactResponder | Surface Filter=`Water` | Ocean Hitを水柱、着水音、泡等へ接続する。 |
| TargetLock | 変更なし | Ocean遮蔽でSelectorがTargetを失った後は既存Lost Graceを使う。 |

Ocean HitはHealthへDamageを送らない。ProjectileDetonatorの接触起爆、ImpactResponder、Weaponの命中Actionは実行できる。OceanへColliderを追加して二重判定させない。

### 保存・初期化・受入条件

WaterSurfaceStateは`WaterSurfaceStateExtension`、OceanProbeSetは`OceanProbeSetExtension`で保存する。TargetSelector、HitscanWeapon、ProjectileEmitterのOcean設定も各専用Extensionへ追加する。State、現在Ocean、Surface Sample、Probe Runtime配列の結果はPlay開始時に再計算する。

| 試験 | 合格条件 |
| --- | --- |
| State遷移 | 4状態の順序とAction回数が正しく、余白内で毎Frame反転しない。 |
| Probe配列 | Entry追加・削除・保存復元後も距離順とRuntime Indexが一致する。 |
| Target | 4遮蔽ModeとClearanceが個別に機能する。 |
| Weapon | Physics/Ocean同時候補で距離が近いHitだけが通知される。 |
| 欠落参照 | Ocean、Action先、Effectが未設定でもCrashせずfalse/Invalidを返す。 |
| 共通波面 | 描画・浮力・Segment Cast・State・Probeが同じOcean Sample経路を使う。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件だった。

## 艦艇射撃の速度・遮蔽・安全検査・状態効果Component詳細

### ProjectileEmitter追加契約（表示名: Projectile Emitter）

**目的:** 移動・回転する発射母体の作用点速度をProjectile初速へ加え、照準SourceとしてBallisticPredictionを直接選べるようにする。

| Inspector項目 | 型 / 既定値 | Runtime契約 |
| --- | --- | --- |
| 照準Source | enum / 画面照準 | `画面照準`、`Transform前方`、`Target`、`弾道予測`。 |
| 照準/Selector | GameObject / Owner | ScreenAim、Transform、TargetSelectorの参照元。Modeに応じて意味が変わる。 |
| 弾道予測 | GameObject / Owner | 弾道予測Modeで参照するBallisticPrediction所有Object。 |
| 発射元速度を継承 | bool / true | falseなら従来どおり砲口方向×速度だけを初期World速度にする。 |
| 速度Source | GameObject / Owner | Rigidbody速度を読む基準。Rigidbodyがなければ親検索へ進む。 |
| 親Rigidbodyを検索 | bool / true | Sourceから親方向へ最初のRigidbodyを探す。 |
| 並進速度継承 | float / 1.0 | Rigidbody linearVelocityへ掛ける倍率。 |
| 角速度継承 | float / 1.0 | `cross(angularVelocity, muzzlePosition-bodyOrigin)`へ掛ける倍率。 |

初期World速度は`launchDirection * muzzleSpeed + sourceVelocity`である。Active Projectileは方向とScalar速度ではなくWorld速度Vectorを移動の正本とし、`acceleration = gravity - drag * velocity`で更新する。既存の貫通、跳弾、ThreatTrackerへ渡す方向と速度はこのWorld速度から同期する。

弾道予測ModeではBallisticPredictionの`Valid`、`LaunchDirection`、`Initial Speed`、`Gravity`、`Drag`、`Source Velocity`を同時に採用する。予測解が無効、参照Componentがない、ObjectPoolから弾を取得できない、FireLineCheckがBlockedの場合は発射成立にしない。

### BallisticPrediction追加契約（表示名: 弾道予測）

| Inspector項目 | 型 / 既定値 | Runtime契約 |
| --- | --- | --- |
| 発射元速度を継承 | bool / true | Solverの必要World速度から母体速度を引いて砲口初速一致を調べる。 |
| 速度Source | GameObject / Owner | 発射作用点のRigidbody Source。 |
| 親Rigidbodyを検索 | bool / true | 子砲塔から船体RootのRigidbodyを解決する。 |
| 並進速度継承 | float / 1.0 | SourceのlinearVelocity倍率。 |
| 角速度継承 | float / 1.0 | 作用点の接線速度倍率。 |
| 発射元World速度 | Runtime Vector3 | 解計算に使用したsourceVelocity。 |
| 初期World速度 | Runtime Vector3 | `LaunchDirection*InitialSpeed + SourceVelocity`。 |

`LaunchDirection`は砲口相対の射出方向、`LaunchVelocity`はWorldで実際に積分する初期速度である。ProjectileEmitterと設定値を一致させる。Damage、砲塔回転、発射Timingは責務外だが、ProjectileEmitterの照準Sourceへ直接指定できる。

### AreaDamage追加契約（表示名: 範囲ダメージ）

| Inspector項目 | 型 / 既定値 | Runtime契約 |
| --- | --- | --- |
| 遮蔽判定 | enum / なし | `なし`、`Physics`、`Physics + Ocean`。 |
| 遮蔽Layer Mask | int32 / -1 | 爆心地からSample点へのPhysics Cast対象。Damage対象Layer Maskとは独立。 |
| 遮蔽時倍率 | float / 0 | 0～1。遮蔽Sampleへ適用するDamage/Impulse倍率。 |
| 遮蔽Sample数 | int / 1 | 1～9。Targetの中心と上下へ分散する。 |
| Teamルール | enum / すべて | すべて、異なるTeamのみ、同じTeamのみ。 |
| Neutralを無視 | bool / false | Team IDが負の対象を除外する。 |
| Team Source | GameObject / Instigator | 比較元Team。未設定時はInstigatorを使う。 |

各TargetでDistance Falloffを求めた後、Team Filter、Physics/Ocean遮蔽を評価する。遮蔽率はSampleごとの可視/遮蔽を平均し、`visibleRatio + blockedRatio * blockedMultiplier`をDamageとImpulseの双方へ掛ける。Target自身、AreaDamage Owner、InstigatorのHierarchyは遮蔽Rayの自己遮蔽から除外する。

Team Componentを持たないObjectはNeutralとは扱わず、Team Ruleだけを理由に除外しない。`発生元を除外`はInstigator自身へのDamage除外であり、Team Ruleとは別に適用する。爆発発生、起爆条件、Damage式の意味はProjectileDetonatorまたはゲームScriptの責務である。

### FireLineCheck（表示名: 発射前射線チェック / 内部名: `FireLineCheck`）

**目的:** Weapon発射後の命中Filterとは分離し、砲口直前の自艦構造物や障害物を検出して発射要求を拒否する。

| Inspector項目 | 型 / 既定値 | Runtime契約 |
| --- | --- | --- |
| 砲口 | GameObject / Owner | Cast開始位置。 |
| 前方向Source | GameObject / 砲口 | World前方向を取得するTransform。 |
| 許可Target | GameObject / なし | このObjectまたはHierarchyへ最初に当たった場合はClear扱い。 |
| 検査距離 | float / 10.0 | 0以上。砲口から安全確認するWorld距離。 |
| 検査半径 | float / 0.05 | 0ならRay、0より大きければSphere Cast。 |
| Block Layer Mask | int32 / -1 | Blockerとして扱うPhysics Layer。 |
| 無視Object[] | GameObject配列 / 空 | 明示したObject Hierarchyだけを無視する。 |
| Runtime | bool | `射線Clear`または`発射Blocked`。 |
| Blocking Object | Runtime int32 / -1 | 最初のBlocker GameObject ID。 |
| Blocking距離 | Runtime float / 0 | 砲口からBlockerまでの距離。 |

Weapon ManagerはOwnerまたは親HierarchyからFireLineCheckを探し、Hitscan/Projectileの実発射直前に評価する。Blockedの場合、Cooldown開始、弾薬消費、Pool貸出、発射Actionを行わない。AttackCollisionFilterのIgnore Instigator HierarchyはFireLineCheckへ流用しない。

### StatusEffectSet（表示名: 状態効果セット / 内部名: `StatusEffectSet`）

**目的:** 任意文字列IDの時間制Effectを複数保持し、開始、一定間隔、終了をC++ Script Actionへ通知する。Effectの意味と式は持たない。

| Inspector項目 | 型 / 既定値 | Runtime契約 |
| --- | --- | --- |
| Action対象 | GameObject / Owner | 開始/Tick/終了Actionを受けるScript Object。 |
| Effect ID | string / EffectN | Definition内で検索する識別子。空文字は適用失敗。 |
| Duration | float / 1.0 | 0.001秒以上。適用時の残り時間。 |
| Stack Mode | enum / Refresh | Refresh、Stack、Ignore。 |
| 最大Stack | int / 1 | Stack Modeで増やせる上限。最低1。 |
| Tick間隔 | float / 0 | 0以下なら周期Tickを行わない。 |
| 開始Action | string / OnStatusEffectStarted | 新規Runtime Entry生成時に一度。 |
| Tick Action | string / OnStatusEffectTick | Tick間隔ごと。String PayloadはEffect ID。 |
| 終了Action | string / OnStatusEffectEnded | RemoveまたはDuration満了時に一度。 |
| Runtime Entry数 | Runtime int | 現在ActiveなEffect ID数。 |

Refreshは残り時間と次Tick待ちをDefinition値へ戻す。Stackは最大までStack Countを増やし残り時間を戻す。Ignoreは同じIDがActiveなら状態を変更せず成功扱いにしない。Runtime EntryはEffect ID、Source GameObject ID、残り秒、次Tick残り秒、Stack数を保持する。

Definition削除、Play Stop、RuntimeStateResetではRuntime Entryを残さない。Pool再利用時に以前の火災等を引き継がない。Start/Tick/End ActionはEffect IDをString Payloadとして送るが、Damage、Attribute変更、Component Active、Particle、Audioは受信ScriptまたはAction接続が実行する。

### 保存・初期化・受入条件

Projectile/Area/Ballistic追加値はそれぞれ`ProjectileVelocityAimExtension`、`AreaDamageFilterExtension`、`BallisticSourceVelocityExtension`へ保存する。新規Componentは`FireLineCheckExtension`と`StatusEffectSetExtension`へ保存する。Blocking情報、発射元/初期World速度、Status Runtime EntryはRuntime値でScene保存しない。

| 試験 | 合格条件 |
| --- | --- |
| Source Velocity | 並進、角速度、親Rigidbody、倍率0/1、継承無効が独立する。 |
| Ballistic接続 | Valid時だけ発射し、予測と実弾が同じGravity/Drag/初期World速度を使う。 |
| Area Team | Any/Different/Same、Neutral、TeamなしObject、Instigator除外が独立する。 |
| Area遮蔽 | Physics/Ocean、Layer、1～9 Sample、Blocked MultiplierがDamage/Impulseへ一致する。 |
| FireLine | Ray/Sphere、Allowed Target、Ignore配列、親検索、Layerが正しくBlockする。 |
| Status | Refresh/Stack/Ignore、最大Stack、Tickなし/あり、Remove/Clear/Resetが正しい。 |
| Save/Load | 全編集値と可変配列が一致し、Runtime値は初期化される。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件だった。

## 現行追加基準

後続実装として`DistanceActivation`、`SimulationLOD`、`RailEventMarker`を追加済みである。この追記時点の機械照合基準はComponent 277件、Runtime API Entry 216件、C++ Script Template 26件だった。設定、保存、Runtime、受入条件は「距離最適化・レールイベントComponent詳細」を参照する。

## SimulationLOD更新間引き・Wave遅延実体化の追加仕様

### SimulationLODのScript更新間隔

SimulationLODへ`Medium Script更新秒`と`Far Script更新秒`を追加する。既定値はMedium `1 / 30秒`、Far `0.2秒`である。0秒は間引きなしを意味する。

| Runtime LOD | Script Component Active | Update呼出し | deltaTime |
| --- | --- | --- | --- |
| Near | Play開始時状態 | 毎Frame | そのFrameのdeltaTime。 |
| Medium | Play開始時状態 | Medium更新秒ごと | 省略したFrame分を累積して渡す。 |
| Far、Script停止=false | Activeを維持 | Far更新秒ごと | 省略したFrame分を累積して渡す。 |
| Far、Script停止=true | false | 呼ばない | 復帰時に休止中の巨大なdeltaTimeを渡さない。 |
| Culled | Object Inactive | 呼ばない | 復帰後から再計測する。 |

入力Action、UI Action、Rail Marker ActionはScriptの登録関数へイベントとして通知するため、Update間隔とは分離する。`FixedUpdate`も物理周期の意味を壊さないため、この秒間隔では間引かない。重い連続探索や状態判断はUpdateへ置き、物理ForceはFixedUpdate、即時入力はBindActionへ分ける。

### WaveSpawnerの距離開始

WaveSpawnerの開始条件へ`距離`を追加する。

| Inspector項目 | 動作 |
| --- | --- |
| 距離 Source | Player、Camera、Rail Follower等の接近Object。未設定・参照切れ・Inactiveなら開始しない。 |
| 開始距離 | Sourceと生成基準位置のWorld 3D距離。0以上。 |
| 距離の基準点 | `生成基準位置`。未設定ならWaveSpawner所有Object。 |

距離は平方根を求めず二乗距離で比較する。条件成立前はPool個体を貸し出さず、Wave Runtimeの小さい状態だけを保持する。これにより、Scene開始時に将来の全Waveを実体化しない。距離条件は一度成立した後にWaveを途中停止する機能ではなく、最初の実体化開始条件である。

### 1Frame最大生成数

ObjectPool生成へ`1Frame最大生成数`を追加する。既定値8、範囲1～1024。生成間隔0でも生成数100なら、既定設定では最短13Frameへ分散する。低FPSで生成Timerが複数回分遅れても、同じ上限を適用する。

上限はWaveの総生成数を減らさず、生成完了を後続Frameへ送る。各生成Actionは実際に貸し出したFrameでGameObject Payload付き通知を行う。Pool空き不足では従来どおり次Frame以降へ待機し、架空の生成済み数を増やさない。

### 外部開始と重複開始

開始条件`外部開始`はC++の`WaveSpawner::Start()`で開始する。未開始または前回個体が全撃破・全返却済みならtrue。生成中または追跡個体が残っているWaveへ再度Startした場合はfalseを返し、既存のSpawn Recordを消さない。

`WaveSpawnerExtension`末尾へ1Frame最大生成数、`SimulationLodExtension`では距離3値の直後へScript更新秒2値を保存する。旧Wave行は既定8、旧SimulationLOD行は既定間隔へFallbackする。この追記時点の機械照合基準はComponent 277件、Runtime API Entry 218件、C++ Script Template 26件だった。

## 全Component 値リファレンス（現行コード照合版）

### この章の位置づけ

この章より前の各章は「そのComponentで何ができるか」「どう組むか」を用途別に説明している。この章はそれとは役割が違い、**現在のコードに実在するComponent全件と、その全Inspector値を機械的に洗い出した一次資料**である。

上の章に説明があるComponentでも、Inspector項目を1行も落とさずに列挙しているのはこの章だけである。用途を知りたいときは上の章、値の意味・範囲・既定値を確かめたいときはこの章を見る。

抽出元は次の3ファイルであり、README や過去の記述ではなく現在のソースを正とする。

| 情報 | 抽出元 |
| --- | --- |
| Component種類と内部名 | `Source/Engine/Editor/EditorScene.h` の `enum class EditorComponentType` |
| 表示名とカテゴリ | `Source/Engine/Editor/EditorInspectorPanel.cpp` の `kComponentAddEntries` |
| Inspector行・型・範囲 | `Source/Engine/Editor/EditorInspectorPanel.cpp` の各 `Draw???Component` |
| 既定値 | `Source/Engine/Editor/EditorScene.cpp` の `EditorScene::CreateComponent` |

### Component総数と内訳（現行）

| 区分 | 件数 | 内容 |
| --- | --- | --- |
| `EditorComponentType` の総数 | **280** | `Count` を除いた列挙子の数。 |
| 「コンポーネントを追加」から選べる | **276** | `kComponentAddEntries` の重複を除いた種類数。 |
| 追加メニューに出ない互換スロット | **4** | `LegacyRailShooterEnemy` / `LegacyRailShooterShip` / `LegacyRailShooterEnemyMotion` / `LegacyRailShooterStage`。旧Sceneの読み込み専用で、Engine Runtimeは実行しない。 |
| 追加メニューの行数 | 282 | 同じ型を2カテゴリへ出している行が6件あるため、種類数より6多い。 |

追加メニューに2回出る6件は `AudioListener`、`Canvas`、`LineRenderer`、`TerrainCollider`、`TilemapCollider2D`、`TrailRenderer` である。どちらのカテゴリから追加しても同じ `EditorComponentType` が付き、Inspectorも保存形式も同一になる。「2つある」ように見えても別Componentではない。

以前この文書が基準にしていた「Component 277件」は `SceneStreaming`、`TextEffect`、`SceneTransition` の追加前の値である。**現行の機械照合基準は Component 280件（うち追加可能276件）、Runtime API Entry 229件、C++ Script Template 26件**である。

### 表の読み方

各Componentの表は次の6列を持つ。

| 列 | 意味 | 注意 |
| --- | --- | --- |
| Inspector表示 | Inspectorの左側へ実際に出る日本語ラベル。 | ここに無いラベルは、そのComponentのInspectorには存在しない。 |
| 内部Field | `EditorComponent` のメンバー名。 | Scene保存、Runtime Property、Log Monitorはこの名前を使う。 |
| 型 | 編集ウィジェットの型。 | `int32(選択)` はCombo。`GameObject参照 (int32 ID)` は Hierarchy から割り当てる欄で、値としてはGameObject IDを持つ。 |
| ドラッグ量 | ドラッグ1目盛りで変化する量。 | 精度の目安であり、直接入力すればこの刻みに縛られない。 |
| 範囲・選択肢 | Clamp範囲、またはCombo選択肢を `0=...` 形式で全部並べたもの。 | 「制限なし」は下限と上限に同じ値を渡していてClampが効かない行である。極端な値を入れられるので、使用者側で常識的な値を守る。 |
| 既定値 | `CreateComponent` が設定する初期値。 | `—` は `CreateComponent` で明示初期化していない行で、`EditorComponent` の既定初期化子か、Runtime表示専用の値である。 |

読むときに必ず区別すること。

1. **編集値とRuntime表示値は違う。** 「実行中体力」「Runtime」「Blocking距離」のような行は読み取り専用の状態表示で、Sceneには保存されない。表に出ていない `DrawTextRow` の行がそれにあたる。
2. **同じ描画関数を共有するComponentがある。** 見出しの「同じ描画関数を共有」に他の型名が並ぶ場合、表の内容はその全型で共通である。ただし共通で出ていても、そのComponentのRuntimeが読まない値は意味を持たない。たとえば `BoxCollider` と `BoxCollider2D` は同じ表を出すが、2D側はScene保存までで実処理へ接続していない。
3. **表に出る＝Play中に効く、ではない。** 実装状態の判定はこの章の表ではなく、上の各章の実装状態表と「未実装を未実装と書くルール」に従う。この章はあくまで「Inspectorで編集でき、Sceneへ保存される値の全件」である。
4. **角度の単位はラベルで見分ける。** ラベルに `deg` または「度」が付く行は度、付かない回転系のFieldは内部でラジアン保存の場合がある。Jointの `jointMinLimit` / `jointMaxLimit` はラジアン保存である。
5. **可変長配列を持つComponentは、要素側の項目が別枠になる。** `[要素].xxx` と書かれた行は、Inspectorの「◯◯を追加」で増やした要素1件ごとの設定である。既定値は要素を新規追加したときの値であり、Component自体の既定ではない。

### C++ Scriptから既存Componentの値を操作する

`GameObject::GetComponent("内部型名")` で汎用 `Component` Wrapperを取得できる。ComponentをScriptから追加する機能ではなく、SceneまたはPrefabへ既に付いているComponentの有無、有効状態、公開Fieldを操作する入口である。

```cpp
const GameObject owner{gameObjectId};
const Component springForce = owner.GetComponent("SpringForce");

if (springForce.IsValid()) {
	springForce.SetFloat("springForceStiffness", 40.0f);
	springForce.SetBool("springForceApplyReaction", true);
}
```

現行の自動生成台帳では、280種類中257種類の1,474 Fieldを扱える。内訳はFloat 791、Bool 235、GameObject参照182、Vector3 180、Int 86である。Field名はこの文書の「内部Field」と同じC++メンバー名を使う。ただし、可変長配列、文字列、Asset参照、未登録Vector2、複合構造体の要素は汎用経路の対象外である。

```cpp
void PlayerScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	const GameObject player{physicsEvent.selfGameObjectId};
	const GameObject collidedObject{physicsEvent.otherGameObjectId};
	const Component springForce = player.GetComponent("SpringForce");

	if (!springForce.IsValid()) {
		return;
	}

	springForce.SetGameObject(
		"springForceTargetGameObjectId",
		collidedObject);
}
```

Set系がtrueを返すのはFieldへ書き込めたことまでである。毎更新で値を読むComponentは次の更新から反映されるが、Jolt Joint、Collider Shape、GPU/Asset資源のようにPlay開始時にRuntime資源を構築するSubsystemは、別途再登録・再構築が必要になる場合がある。操作関数、対応外型、全Field台帳、失敗条件は `cpp-script-documentation-detail-seed.md` の「Component 汎用Propertyアクセス完全リファレンス」を参照する。

### 追加された3 Componentの詳細

`SceneStreaming`、`TextEffect`、`SceneTransition` は前回の基準（277件）より後に追加されたため、上の章に用途説明が無い。ここで個別に補う。

#### SceneStreaming（表示名: Scene Streaming ／ カテゴリ: 最適化）

**目的。** 基準Objectがこのコンポーネントを持つGameObjectへ近づいたらAdditive Sceneを非同期で読み込み、離れたら破棄する。広いステージを1つの `.scene` へ全部置かず、区画ごとに分割して常時のObject数を減らすために使う。

**追加手順。** 区画の代表位置になるGameObject（区画の中心へ置いた空Object等）をHierarchyに作り、そこへ「最適化 > Scene Streaming」を追加する。`Scene Path` へ読み込ませたい `.scene` の相対パスを入力し、`距離基準` へプレイヤー機やCameraを割り当てる。

**Inspector項目。**

| 項目 | 内部Field | 意味と使い方 |
| --- | --- | --- |
| Scene Path | `sceneStreamingScenePath` | Additiveで読み込む `.scene` のパス。空文字列の間このComponentは何もしない。 |
| 距離基準 | `sceneStreamingReferenceGameObjectId` | 距離を測る相手。未設定（-1）なら、Scene内でPriorityが最も高い有効Cameraの位置を使う。 |
| 読込距離 | `sceneStreamingLoadDistance` | 既定500。基準Objectとの距離がこれ以下になったフレームで非同期読込を要求する。 |
| 解除距離 | `sceneStreamingUnloadDistance` | 既定650。この距離を超えたら破棄する。**Inspectorが毎フレーム「読込距離」以上へ引き上げるため、読込距離より小さい値は保持できない。** 読込と解除が同じ距離だと境界で読込と破棄を往復するので、必ず解除距離を広めに取る。 |
| 遠距離でSceneを破棄 | `sceneStreamingUnloadWhenFar` | 既定true。falseにすると一度読んだSceneを離れても破棄しない。行って戻る導線で再読込のヒッチを避けたいときに使う。 |
| Runtime | `sceneStreamingRuntimeLoaded` / `sceneStreamingRuntimePending` | 読み取り専用表示。`処理中` / `Loaded` / `Unloaded` を出す。Scene保存しない。 |

**距離の測り方。** 基準Objectと、**このComponentを持つGameObjectのWorld位置**との3D距離である。Component側のY座標も距離へ入るので、区画代表Objectを地形の高い位置へ置くと、水平距離の感覚より早く境界に達する。

**Runtimeの動作。**

1. `EditorRuntimeManager::UpdateAutomaticSceneStreaming()` が毎フレーム、Scene内の全SceneStreamingを先頭から走査する。
2. `isActive` がfalse、または `Scene Path` が空の行は飛ばす。
3. 未読込かつ読込距離以内なら `RequestSceneLoadAsync(path, true)`（Additive）を出し、**そのフレームはそこで走査を打ち切る**。1フレームに開始する読込は最大1件で、同時に複数区画へ入っても順番に処理される。
4. 読込済みかつ解除距離超過かつ「遠距離でSceneを破棄」がtrueなら破棄する。ただし**そのSceneが現在の主Sceneと同じパスなら破棄しない**。
5. 別のScene読込が進行中（`isSceneLoading_`）の間は、この処理自体を実行しない。

**保存。** Sceneへは `SceneStreamingExtension` 行として、Scene Path・距離基準ID・読込距離・解除距離・遠距離破棄フラグの5値を保存する。Runtimeの `Loaded` / `処理中` は保存しない。読み込み時は解除距離を読込距離以上へ補正する。

**C++連携。** 専用Wrapperは無い。読込状態を知りたい場合は `SceneManager::IsLoaded(scenePath)`、`SceneManager::IsLoading()`、`SceneManager::GetLoadProgress()` を使う。Scriptから明示的に読ませたい区画は、SceneStreamingではなく `SceneManager::LoadAsync(path, true)` を直接呼ぶ。

**失敗時の確認順。** ① `Scene Path` の綴りと相対パス、② ComponentとGameObjectの両方が有効か、③ `距離基準` が未設定のときScene内にPriority付きの有効Cameraがあるか、④ 別Sceneの読込が進行中でないか、⑤ 読込距離が実際の距離に対して小さすぎないか。

#### TextEffect（表示名: テキストエフェクト ／ カテゴリ: UI）

**目的。** 同じGameObject上の `Text` または `TextMeshProUGUI` の描画へ演出をかける。文字列そのものは変えず、Alpha・位置・色・表示文字数・スケールだけを毎フレーム加工する。

**責務外。** 文字列の差し替え、レイアウト、フォント選択はこのComponentの担当ではない。文字列はTextまたはUIValueBinding、フォントはText側の `textFontIndex` が持つ。

**追加手順。** Canvas配下のTextまたはTextMeshProUGUIを持つGameObjectを選び、「UI > テキストエフェクト」を追加する。Textが同じObjectに無い場合、このComponentは何も描画しない。

**2系統は独立している。** 「出現演出」はActiveになった瞬間から1回だけ、「常時演出」は表示され続ける間ずっと動く。両方同時に設定でき、同じフレームで重ねて適用される。

**出現演出。**

| 出現効果 | 値 | 効果 | 追加パラメーター |
| --- | --- | --- | --- |
| なし | 0 | 何もしない。 | — |
| フェードイン | 1 | Alphaを0から1へ上げる。 | なし |
| タイプライター | 2 | 表示文字数を先頭から増やす。 | `文字/秒`（`textAppearParamA`、既定12、範囲0.1〜200） |
| スケールポップ | 3 | 拡大しながら出す。 | `オーバーシュート倍率`（`textAppearParamA`、範囲1.0〜3.0） |

`継続時間(秒)`（`textAppearDuration`、既定0.6）と `開始遅延(秒)`（`textAppearDelay`、既定0）は3種共通である。遅延中は演出開始前の状態で待つ。

`textAppearParamA` は「文字/秒」と「オーバーシュート倍率」で共用されるため、**タイプライターからスケールポップへ切り替えると、既定12が範囲1.0〜3.0へ丸められる**。切り替え後は必ず値を見直す。

**常時演出。** `textContinuousEffectType` は0〜11で、種類ごとに `textContinuousParamA/B/C` の意味が変わる。Comboで種類を変更すると、その種類向けの推奨値が自動で入る（`ResetContinuousTextEffectParamsForType`）。

| 値 | 種類 | ParamA | ParamB | ParamC | 切替時の自動値 |
| --- | --- | --- | --- | --- | --- |
| 0 | なし | — | — | — | — |
| 1 | 点滅 | 点滅間隔(秒) 0.02〜5 | — | — | 0.3 / 0 / 0 |
| 2 | レインボー | 色相回転速度(周/秒) 0.01〜10 | 文字ごとの色ずれ 0〜1 | — | 0.6 / 0.15 / 0 |
| 3 | 波 | 振幅(px) 0〜200 | 文字ごとの位相 0〜3 | 揺れる速さ 0〜20 | 8 / 0.5 / 4 |
| 4 | 発光 | Glow半径(px) 0〜60 | 脈動速度 0〜20（0で静止） | 脈動の深さ 0〜1 | 10 / 3 / 0.3 |
| 5 | 輪郭の発光 | 輪郭太さ(px) 0.5〜20 | 脈動速度 0〜20 | 脈動の深さ 0〜1 | 2 / 3 / 0.3 |
| 6 | 色収差 | ズレ量(px) 0〜20 | 脈動速度 0〜20 | 脈動の深さ 0〜1 | 2 / 0 / 0 |
| 7 | 微振動 | 振れ幅(px) 0〜40 | 速さ 0.1〜60 | — | 2 / 20 / 0 |
| 8 | 不規則点滅 | 最低輝度(0-1) | フリッカー速さ 0.1〜30 | — | 0.4 / 6 / 0 |
| 9 | 脈動 | 最大拡大率 1〜2 | 速さ 0.05〜20 | — | 1.05 / 3 / 0 |
| 10 | 残像 | 振れ幅(px) 0〜100 | 速さ 0.05〜20 | 残像の数 2〜6 | 10 / 2 / 4 |
| 11 | 簡易グリッチ | ズレ量(px) 0〜40 | 発生頻度(回/秒) 0.05〜10 | 強さ(0-1) | 6 / 1.5 / 0.8 |

`開始遅延(秒)`（`textContinuousDelay`、既定0）は種類を選んだときだけ表示され、この秒数を過ぎてから常時演出の時間が進む。

**Runtimeの動作。** GameViewのText描画中に処理する。Componentが有効で、かつ出現・常時のどちらかが「なし」以外のときだけ `textEffectRuntimeElapsed` を `DeltaTime` で進める。両方「なし」に戻すかComponentを無効にすると経過時間は0へ戻り、次に有効化したとき出現演出が最初から再生される。**「非表示から表示」ではなく「Component無効から有効」または「効果なしから効果あり」が再生の起点である。**

**保存。** `TextEffectExtension` 行へ出現4値と常時5値を保存する。`textEffectRuntimeElapsed` と `textEffectRuntimeWasActive` はRuntime専用で保存しない。フォント番号は同じObjectのText側が `TextFontExtension` として別に保存する。

**C++連携。** 専用Wrapperは無い。演出の切り替えは `RuntimeProperty::SetInt` / `SetFloat` に `"TextEffect"` と内部Field名を渡すか、Componentごと `GameObject::SetComponentActive("TextEffect", bool)` で入切する。出現演出をやり直したいときは、いったんfalseにして次フレームtrueへ戻す。

#### SceneTransition（表示名: シーン遷移 ／ カテゴリ: エフェクト）

**目的。** Scene切り替えの前後へ、画面を覆う演出を挟む。覆い切ったタイミングで実際のScene差し替えを行うため、読み込みのちらつきを隠せる。

**追加手順。** 遷移を管理するGameObject（Scene常駐のManager Object等）へ「エフェクト > シーン遷移」を追加し、`種類` と `遷移先 Scene` を設定する。`SceneButton` などから同じパスへの遷移が要求されると、この設定が自動で使われる。

**Inspector項目。**

| 項目 | 内部Field | 意味と使い方 |
| --- | --- | --- |
| 種類 | `sceneTransitionType` | 0=なし / 1=色フェード / 2=ワイプ / 3=Camera Dive。**0のままだと遷移演出は一切起動しない。** |
| 遷移先 Scene | `sceneTransitionTargetScenePath` | この演出が担当する遷移先。空文字列だと起動しない。 |
| 覆うまでの時間(秒) | `sceneTransitionOutDuration` | 既定0.8。Overlay Alphaを0から1へ上げる時間。Runtimeは最低0.001秒へ補正する。 |
| 静止時間(秒) | `sceneTransitionHoldSeconds` | 既定0.2。覆い切った状態で止める時間。**実際のScene差し替えはこの区間の直前、覆い終わった瞬間に行う。** |
| 見せる時間(秒) | `sceneTransitionInDuration` | 既定0.8。Overlay Alphaを1から0へ下げる時間。 |
| 色 | `sceneTransitionColor` | 既定 `{1,1,1}`（白）。0〜1のRGB。黒フェードにするなら `{0,0,0}`。 |
| Dive基準Object | `sceneTransitionCameraDiveSourceGameObjectId` | 種類がCamera Diveのときだけ表示。未設定なら遷移開始時の最優先Cameraの位置を着地点にする。 |
| Dive開始位置 Offset | `sceneTransitionCameraDivePositionOffset` | 既定 `{0, 60, 0}`。着地点からの相対Offsetで、ここからCameraが降りてくる。 |
| Dive開始角度 deg | `sceneTransitionCameraDiveRotationDegrees` | 既定 `{90, 0, 0}`（真下向き）。**絶対Euler角の度数**であり、着地姿勢への加算ではない。 |

**Runtimeの動作（3フェーズ）。**

| Phase | 内容 | 終了条件 |
| --- | --- | --- |
| 0 CoveringOut | Overlay Alphaを0から1へ。 | `覆うまでの時間` 経過。ここで `LoadSceneForPlay` を実行しSceneを差し替える。 |
| 1 Holding | Alpha 1のまま静止。Camera Diveなら開始姿勢を上書き設定する。 | `静止時間` 経過。 |
| 2 RevealingIn | Alphaを1から0へ。Camera Diveなら開始姿勢から着地姿勢へ補間する。 | `見せる時間` 経過で演出終了。 |

**多重起動しない。** 演出が動いている間に別のSceneTransitionを起動しても `false` を返して無視する。先に始まった演出が優先される。Play中でないとき（`isPlaying_` がfalse）も起動しない。

**遷移先の自動一致。** `SceneButton` などがScene読込を要求すると、`TryStartSceneTransitionForRequest` がScene内の有効なSceneTransitionを走査し、`遷移先 Scene` を正規化パスで比較して一致した最初の1件を起動する。**Scene遷移ごとに演出を変えたい場合は、遷移先パスの異なるSceneTransitionを複数置く。**

**Camera Diveの注意。** 着地姿勢の取得に失敗した場合（Cameraが1つも見つからない場合）、Diveは自動で無効化され、Overlayだけの演出になる。`Dive基準Object` を指定したときは、その位置だけを着地点として使い、回転は最優先Cameraのものを使う。

**保存。** `SceneTransitionExtension` 行へ、種類・遷移先パス・3つの秒数・色3値・Dive基準ID・Dive Offset 3値・Dive角度3値の15値を保存する。`sceneTransitionRuntimeState` と `sceneTransitionRuntimeElapsed` はRuntime専用で保存しない。

**C++連携。** 専用Wrapperは無い。Scriptから起こす場合は `SceneManager::Load(scenePath)` を呼べば、同じパスを持つSceneTransitionが自動で拾う。`SceneManager::LoadAsync` は非同期読込の経路であり、この演出とは別系統である。

### 全280 Component Inspector値表

以下、`EditorComponentType` の宣言順に全280件を並べる。この順番はScene保存の型番号の並びと同じである。

#### Transform

表示名「トランスフォーム」／カテゴリ「基本」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### ModelRenderer

表示名「メッシュレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: SkinnedMeshRenderer, SpriteRenderer, BillboardRenderer, ParticleSystemRenderer, TilemapRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### SpriteRenderer

表示名「スプライトレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: ModelRenderer, SkinnedMeshRenderer, BillboardRenderer, ParticleSystemRenderer, TilemapRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### Light

表示名「ライト」／カテゴリ「ライト・環境」／Inspector描画 `DrawLightComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | lightTypeIndex | int32(選択) | — | 0=Point / 1=Sun / 2=Spot / 3=Area | — |
| 強さ | intensity | float | 1.0 | 0.0 〜 100000.0 | `1.0` |
| 半径 | colliderRadius | float | 0.1 | 0.01 〜 1000.0 | `0.5` |
| 方位角/高度を使用 | sunUseAzimuthElevation | bool | — | true / false | `false` |
| 太陽方位角 | sunAzimuthDegrees | float | 1.0 | -360.0 〜 360.0 | `45.0` |
| 太陽高度 | sunElevationDegrees | float | 0.5 | -10.0 〜 90.0 | `55.0` |
| 色温度を使用 | sunUseColorTemperature | bool | — | true / false | `false` |
| 色温度を高度から自動推定 | sunAutoTemperatureFromElevation | bool | — | true / false | `false` |
| 太陽色温度(K) | sunTemperatureKelvin | float | 25.0 | 1000.0 〜 12000.0 | `5500.0` |
| 距離 | colliderRadius | float | 0.1 | 0.01 〜 1000.0 | `0.5` |
| 内側角度 | colliderSize.x | float | 0.1 | 1.0 〜 89.0 | `1.0` |
| 外側角度 | colliderSize.y | float | 0.1 | 1.0 〜 89.0 | `1.0` |
| 広がり | colliderSize.z | float | 0.1 | 0.01 〜 100.0 | `1.0` |

#### Camera

表示名「カメラ」／カテゴリ「カメラ」／Inspector描画 `DrawCameraComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 優先度 | cameraPriority | int32 | 1 | — | `0` |
| 投影 | cameraProjectionMode | int32(選択) | — | 0=Perspective / 1=Orthographic | `0` |
| 視野角 | cameraFieldOfView | float | 1.0 | 1.0 〜 179.0 | `60.0` |
| ニアクリップ | cameraNearClip | float | 0.01 | 0.01 〜 100.0 | `0.3` |
| ファークリップ | cameraFarClip | float | 1.0 | 0.1 〜 10000.0 | `1000.0` |
| 露出補正 (EV) | cameraExposure | float | 0.1 | -10.0 〜 10.0 | `0.0` |
| 被写界深度 | cameraDofEnabled | bool | — | true / false | `false` |
| フォーカス距離 | cameraDofFocusDistance | float | 0.1 | 0.1 〜 1000.0 | `10.0` |
| 絞り | cameraDofAperture | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 焦点距離 (mm) | cameraDofFocalLength | float | 1.0 | 1.0 〜 300.0 | `50.0` |
| モーションブラー | cameraMotionBlurEnabled | bool | — | true / false | `false` |
| ブラー強度 | cameraMotionBlurIntensity | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 視点操作を有効化 | cameraInputEnabled | bool | — | true / false | `false` |
| 操作形式 | cameraInputStyle | int32(選択) | — | 0=FreeLook / 1=Orbit | `0` |
| 回転入力 | cameraInputActivation | int32(選択) | — | 0=右ボタン保持 / 1=常時 | `0` |
| マウス感度 | cameraInputLookSensitivity | float | 0.0001 | 0.0 〜 0.1（相対入力1単位あたりのrad） | `0.003` |
| Y軸反転 | cameraInputInvertY | bool | — | true / false | `false` |
| Pitch最小角度 | cameraInputMinimumPitchDegrees | float | 1.0 | -89.0 〜 89.0（度） | `-85.0` |
| Pitch最大角度 | cameraInputMaximumPitchDegrees | float | 1.0 | -89.0 〜 89.0（度）、最小角度以上 | `85.0` |
| 操作中カーソル固定 | cameraInputLockCursor | bool | — | true / false | `true` |
| 操作中カーソル非表示 | cameraInputHideCursor | bool | — | true / false | `true` |
| WASD/QE移動 | cameraInputMovementEnabled | bool | — | true / false（FreeLookのみ） | `true` |
| 移動速度 | cameraInputMoveSpeed | float | 0.1 | 0.0 〜 10000.0（ワールド単位/秒） | `5.0` |
| Shift倍率 | cameraInputFastMultiplier | float | 0.1 | 1.0 〜 100.0 | `3.0` |
| Orbit中心 | cameraInputTargetGameObjectId | GameObject参照 | — | -1=Cameraの接続先 | `-1` |
| 中心Offset | cameraInputPivotOffset | Vector3 | 0.1 | 各軸 -10000.0 〜 10000.0（ワールド座標系） | `(0.0, 1.0, 0.0)` |
| 距離 | cameraInputOrbitDistance | float | 0.1 | 0.01 〜 10000.0（初期距離） | `6.0` |
| 最小距離 | cameraInputMinimumDistance | float | 0.1 | 0.01 〜 10000.0 | `1.0` |
| 最大距離 | cameraInputMaximumDistance | float | 0.1 | 0.01 〜 10000.0、最小距離以上 | `30.0` |
| ホイールZoom速度 | cameraInputZoomSpeed | float | 0.1 | 0.0 〜 1000.0（ワールド単位/段） | `1.0` |

視点操作OFF時は詳細設定を非表示にする。移動速度・Shift倍率はFreeLookの移動ON時、Orbit中心からZoom速度まではOrbit時に表示する。上表の範囲はInspectorの制限であり、Script Setter全般が同じ範囲を自動検証するという意味ではない。

#### AudioSource

表示名「オーディオソース」／カテゴリ「オーディオ」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### RigidBody

表示名「リジッドボディ」／カテゴリ「3D物理」／Inspector描画 `DrawRigidBodyComponent`／同じ描画関数を共有: RigidBody2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Colliderから質量を計算 | automaticMassFromCollider | bool | — | true / false | `false` |
| 実質密度 kg/m3 | bodyDensity | float | 1.0 | 0.01 〜 1000000.0 | `500.0` |
| 質量 | mass | float | 0.01 | 0.01 〜 1000000.0 | `1.0` |
| 線形減衰 | drag | float | 0.01 | 0.0 〜 20.0 | `0.0` |
| 角度減衰 | angularDrag | float | 0.01 | 0.0 〜 20.0 | `0.05` |
| 慣性倍率 | inertiaMultiplier | float | 0.01 | 0.01 〜 1000.0 | `1.0` |
| 重心オフセット | centerOfMassOffset | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| ジャイロ効果 | applyGyroscopicForce | bool | — | true / false | `false` |
| 重力を使用 | useGravity | bool | — | true / false | `true` |
| キネマティックにする | isKinematic | bool | — | true / false | `false` |
| 補間 | interpolationMode | int32(選択) | — | 0=補間なし / 1=補間 / 2=外挿 | `0` |
| 衝突判定 | collisionDetectionMode | int32(選択) | — | 0=離散 / 1=連続 | `0` |
| 速度 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 角速度 | angularVelocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### BoxCollider

表示名「箱の当たり判定」／カテゴリ「3D物理」／Inspector描画 `DrawBoxColliderComponent`／同じ描画関数を共有: BoxCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |

#### SphereCollider

表示名「球の当たり判定」／カテゴリ「3D物理」／Inspector描画 `DrawSphereColliderComponent`／同じ描画関数を共有: CircleCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |

#### Input

表示名「入力」／カテゴリ「入力・イベント」／Inspector描画 `DrawInputComponent`

Inspector編集行なし。

#### Animation

表示名「アニメーション」／カテゴリ「アニメーション」／Inspector描画 `DrawAnimationComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Animation Clip パス | assetPath | string | — | 文字列 | `""` |
| 速度 | animationSpeed | float | 0.1 | 0.0 〜 10.0 | `1.0` |
| ループ | animationLoop | bool | — | true / false | `true` |
| 自動再生 | animationPlayOnAwake | bool | — | true / false | `true` |
| 種類 | animationType | int32(選択) | — | 0=FBX / Property Clip / 1=Float / 2=Rotate / 3=Pulse / 4=Bob | `0` |
| 振幅 | animationAmplitude | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| プロパティ数 | trackCount | int32 | 1 | — | — |
| イベント数 | eventCount | int32 | 1 | — | — |
| クリップ数 | clipCount | int32 | 1 | — | — |
| 再生クリップ | animationClipIndex | int32(選択) | — | clipNames.data() | `0` |
| Transform キー数 | keyframeCount | int32 | 1 | — | — |

#### Animator

表示名「アニメーター」／カテゴリ「アニメーション」／Inspector描画 `DrawAnimatorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Graph アセット | assetPath | string | — | 文字列 | `""` |
| 再生速度 | animationSpeed | float | 0.05 | 0.0 〜 10.0 | `1.0` |
| Root Motion を適用 | animatorApplyRootMotion | bool | — | true / false | `false` |
| 移動速度を自動取得 | animatorAutoVelocity | bool | — | true / false | `true` |
| 既定遷移秒 | animatorTransitionDuration | float | 0.01 | 0.0 〜 5.0 | `0.15` |
| MoveX 左右 | animatorMoveX | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| MoveY 前後 | animatorMoveY | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| Speed | animatorSpeedParameter | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 停止 Clip | animatorIdleClipIndex | int32 | 1 | — | `0` |
| 前 Clip | animatorForwardClipIndex | int32 | 1 | — | `0` |
| 後 Clip | animatorBackwardClipIndex | int32 | 1 | — | `0` |
| 左 Clip | animatorLeftClipIndex | int32 | 1 | — | `0` |
| 右 Clip | animatorRightClipIndex | int32 | 1 | — | `0` |
| parameterName.c_str() | parameter.floatValue | float | 0.01 | -10000.0 〜 10000.0 | — |
| parameterName.c_str() | parameter.intValue | int32 | 1 | — | — |
| parameterName.c_str() | parameter.boolValue | bool | — | true / false | — |
| parameterName.c_str() | parameter.vector2Value | Vector2 | 0.01 | -10000.0 〜 10000.0 | — |
| parameterName.c_str() | parameter.vector3Value | Vector3 | 0.01 | -10000.0 〜 10000.0 | — |

#### AudioListener

表示名「オーディオリスナー」／カテゴリ「カメラ」／Inspector描画 `DrawAudioListenerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Master | masterVolume | float | 0.01 | 0.0 〜 1.0 | — |
| SFX | sfxVolume | float | 0.01 | 0.0 〜 1.0 | — |
| BGM | bgmVolume | float | 0.01 | 0.0 〜 1.0 | — |
| Ambience | ambienceVolume | float | 0.01 | 0.0 〜 1.0 | — |
| UI | uiVolume | float | 0.01 | 0.0 〜 1.0 | — |

#### ParentConstraint

表示名「親制約」／カテゴリ「アニメーション」／Inspector描画 `DrawParentConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 位置オフセット | constraintPositionOffset | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### PositionConstraint

表示名「位置制約」／カテゴリ「アニメーション」／Inspector描画 `DrawPositionConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| オフセット | constraintPositionOffset | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### RotationConstraint

表示名「回転制約」／カテゴリ「アニメーション」／Inspector描画 `DrawRotationConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### ScaleConstraint

表示名「スケール制約」／カテゴリ「アニメーション」／Inspector描画 `DrawScaleConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| X 軸フリーズ | constraintFreezeAxisX | bool | — | true / false | `false` |
| Y 軸フリーズ | constraintFreezeAxisY | bool | — | true / false | `false` |
| Z 軸フリーズ | constraintFreezeAxisZ | bool | — | true / false | `false` |

#### EventSystem

表示名「イベントシステム」／カテゴリ「入力・イベント」／Inspector描画 `DrawEventSystemComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |

#### MeshFilter

表示名「メッシュフィルター」／カテゴリ「描画・レンダリング」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### CapsuleCollider

表示名「カプセル当たり判定」／カテゴリ「3D物理」／Inspector描画 `DrawCapsuleColliderComponent`／同じ描画関数を共有: CharacterController, CapsuleCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 高さ | colliderSize.y | float | 0.01 | 0.01 〜 100.0 | `1.0` |

#### MeshCollider

表示名「メッシュ当たり判定」／カテゴリ「3D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: TerrainCollider, PolygonCollider2D, EdgeCollider2D, CompositeCollider2D, TilemapCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### CharacterController

表示名「キャラクターコントローラー」／カテゴリ「3D物理」／Inspector描画 `DrawCapsuleColliderComponent`／同じ描画関数を共有: CapsuleCollider, CapsuleCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 高さ | colliderSize.y | float | 0.01 | 0.01 〜 100.0 | `1.0` |

#### NavigationAgent

表示名「NavMesh エージェント」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshAgentComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 目的地 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 半径 | navAgentRadius | float | 0.01 | 0.1 〜 10.0 | `0.5` |
| 高さ | navAgentHeight | float | 0.01 | 0.1 〜 10.0 | `2.0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.1 〜 50.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.1 〜 100.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.1 | 0.0 〜 10.0 | `0.5` |
| 自動再経路 | navAutoRepath | bool | — | true / false | `true` |

#### PlayableDirector

表示名「プレイアブルディレクター」／カテゴリ「アニメーション」／Inspector描画 `DrawPlayableDirectorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 自動再生 | animationPlayOnAwake | bool | — | true / false | `true` |
| 速度 | animationSpeed | float | 0.1 | 0.0 〜 10.0 | `1.0` |
| ループ | animationLoop | bool | — | true / false | `true` |

#### Script

表示名「ゲームオブジェクト + スクリプト」／カテゴリ「基本」／Inspector描画 `DrawNativeScriptComponent`／同じ描画関数を共有: MonoBehaviour

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| propertyLabel | scriptProperty.boolValue | bool | — | true / false | — |
| propertyLabel | scriptProperty.intValue | int32 | 1 | — | — |
| propertyLabel | scriptProperty.floatValue | float | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.vector2Value | Vector2 | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.vector3Value | Vector3 | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.stringValue | string | — | 文字列 | — |

#### HapticSource

表示名「FeelKit 触覚ソース」／カテゴリ「FeelKit」／Inspector描画 `DrawHapticSourceComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| サウンド | assetPath | string | — | 文字列 | `""` |
| 自動再生 | audioPlayOnAwake | bool | — | true / false | `true` |
| 強さ | hapticStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 持続時間(ms) | hapticDurationMs | int32 | 1 | — | `120` |
| ループ | hapticLoop | bool | — | true / false | `false` |

#### Canvas

表示名「キャンバス」／カテゴリ「基本」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Image

表示名「イメージ」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Text

表示名「テキスト」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### RectTransform

表示名「レクトトランスフォーム」／カテゴリ「基本」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### MonoBehaviour

表示名「モノビヘイビア」／カテゴリ「基本」／Inspector描画 `DrawNativeScriptComponent`／同じ描画関数を共有: Script

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| propertyLabel | scriptProperty.boolValue | bool | — | true / false | — |
| propertyLabel | scriptProperty.intValue | int32 | 1 | — | — |
| propertyLabel | scriptProperty.floatValue | float | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.vector2Value | Vector2 | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.vector3Value | Vector3 | step | minValue 〜 maxValue | — |
| propertyLabel | scriptProperty.stringValue | string | — | 文字列 | — |

#### SkinnedMeshRenderer

表示名「スキンメッシュレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: ModelRenderer, SpriteRenderer, BillboardRenderer, ParticleSystemRenderer, TilemapRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### LineRenderer

表示名「ラインレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawLineRendererComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 10000.0 | `{1.0, 1.0, 1.0}` |

#### TrailRenderer

表示名「トレイルレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawTrailRendererComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 太さ | particleSize | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 終端の太さ | particleEndSize | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 残る秒数 | particleLifetime | float | 0.01 | 0.01 〜 60.0 | `2.0` |
| 毎秒の分割数 | particleRate | float | 1.0 | 1.0 〜 1000.0 | `10.0` |
| 終端透明度 | particleEndAlpha | float | 0.01 | 0.0 〜 1.0 | `0.0` |

#### BillboardRenderer

表示名「ビルボードレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: ModelRenderer, SkinnedMeshRenderer, SpriteRenderer, ParticleSystemRenderer, TilemapRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### CanvasRenderer

表示名「キャンバスレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### ParticleSystemRenderer

表示名「パーティクルシステムレンダラー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: ModelRenderer, SkinnedMeshRenderer, SpriteRenderer, BillboardRenderer, TilemapRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### FlareLayer

表示名「フレアレイヤー」／カテゴリ「カメラ」／Inspector描画 `DrawFlareLayerComponent`

Inspector編集行なし。

#### CinemachineCamera

表示名「Cinemachine カメラ」／カテゴリ「カメラ」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### ReflectionProbe

表示名「リフレクションプローブ」／カテゴリ「ライト・環境」／Inspector描画 `DrawReflectionProbeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | reflectionModeIndex | int32(選択) | — | 0=スクリーンスペース反射 / 1=キューブマップ反射 / 2=平面反射 | — |
| 反射像の強さ | intensity | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| 反射の粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 1000.0 | `{1.0, 1.0, 1.0}` |

#### LightProbeGroup

表示名「ライトプローブグループ」／カテゴリ「ライト・環境」／Inspector描画 `DrawLightProbeGroupComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 反射寄与 | intensity | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| ぼかし | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |

#### LightProbeProxyVolume

表示名「ライトプローブプロキシボリューム」／カテゴリ「ライト・環境」／Inspector描画 `DrawLightProbeProxyVolumeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 1000.0 | `{1.0, 1.0, 1.0}` |
| 反射寄与 | intensity | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| ぼかし | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |

#### Volume

表示名「ボリューム」／カテゴリ「ライト・環境」／Inspector描画 `DrawVolumeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 重み | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### TerrainCollider

表示名「地形の当たり判定」／カテゴリ「3D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, PolygonCollider2D, EdgeCollider2D, CompositeCollider2D, TilemapCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### WheelCollider

表示名「車輪の当たり判定」／カテゴリ「3D物理」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### ConstantForce

表示名「コンスタントフォース」／カテゴリ「3D物理」／Inspector描画 `DrawConstantForceComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 力 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### HingeJoint

表示名「ヒンジジョイント」／カテゴリ「3D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### FixedJoint

表示名「固定ジョイント」／カテゴリ「3D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### SpringJoint

表示名「スプリングジョイント」／カテゴリ「3D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### ConfigurableJoint

表示名「コンフィギュラブルジョイント」／カテゴリ「3D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### CharacterJoint

表示名「キャラクタージョイント」／カテゴリ「3D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### RigidBody2D

表示名「リジッドボディ 2D」／カテゴリ「2D物理」／Inspector描画 `DrawRigidBodyComponent`／同じ描画関数を共有: RigidBody

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Colliderから質量を計算 | automaticMassFromCollider | bool | — | true / false | `false` |
| 実質密度 kg/m3 | bodyDensity | float | 1.0 | 0.01 〜 1000000.0 | `500.0` |
| 質量 | mass | float | 0.01 | 0.01 〜 1000000.0 | `1.0` |
| 線形減衰 | drag | float | 0.01 | 0.0 〜 20.0 | `0.0` |
| 角度減衰 | angularDrag | float | 0.01 | 0.0 〜 20.0 | `0.05` |
| 慣性倍率 | inertiaMultiplier | float | 0.01 | 0.01 〜 1000.0 | `1.0` |
| 重心オフセット | centerOfMassOffset | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| ジャイロ効果 | applyGyroscopicForce | bool | — | true / false | `false` |
| 重力を使用 | useGravity | bool | — | true / false | `true` |
| キネマティックにする | isKinematic | bool | — | true / false | `false` |
| 補間 | interpolationMode | int32(選択) | — | 0=補間なし / 1=補間 / 2=外挿 | `0` |
| 衝突判定 | collisionDetectionMode | int32(選択) | — | 0=離散 / 1=連続 | `0` |
| 速度 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 角速度 | angularVelocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### BoxCollider2D

表示名「四角の当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawBoxColliderComponent`／同じ描画関数を共有: BoxCollider

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |

#### CircleCollider2D

表示名「円の当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawSphereColliderComponent`／同じ描画関数を共有: SphereCollider

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |

#### CapsuleCollider2D

表示名「カプセル当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawCapsuleColliderComponent`／同じ描画関数を共有: CapsuleCollider, CharacterController

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 高さ | colliderSize.y | float | 0.01 | 0.01 〜 100.0 | `1.0` |

#### PolygonCollider2D

表示名「多角形の当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, TerrainCollider, EdgeCollider2D, CompositeCollider2D, TilemapCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### EdgeCollider2D

表示名「線の当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, TerrainCollider, PolygonCollider2D, CompositeCollider2D, TilemapCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### CompositeCollider2D

表示名「複合当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, TerrainCollider, PolygonCollider2D, EdgeCollider2D, TilemapCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### TilemapCollider2D

表示名「タイルマップ当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, TerrainCollider, PolygonCollider2D, EdgeCollider2D, CompositeCollider2D, CustomCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### CustomCollider2D

表示名「カスタム当たり判定 2D」／カテゴリ「2D物理」／Inspector描画 `DrawMeshColliderComponent`／同じ描画関数を共有: MeshCollider, TerrainCollider, PolygonCollider2D, EdgeCollider2D, CompositeCollider2D, TilemapCollider2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| 三角形数 | triangleCount | int32 | 1 | — | — |

#### DistanceJoint2D

表示名「ディスタンスジョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### HingeJoint2D

表示名「ヒンジジョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### SpringJoint2D

表示名「スプリングジョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, FixedJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### FixedJoint2D

表示名「固定ジョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, SliderJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### SliderJoint2D

表示名「スライダージョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, WheelJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### WheelJoint2D

表示名「ホイールジョイント 2D」／カテゴリ「2D物理」／Inspector描画 `DrawJointComponent`／同じ描画関数を共有: HingeJoint, FixedJoint, SpringJoint, ConfigurableJoint, CharacterJoint, DistanceJoint2D, HingeJoint2D, SpringJoint2D, FixedJoint2D, SliderJoint2D

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| アンカー | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転軸 | jointAxis | Vector3 | 0.01 | 制限なし | `{0.0, 1.0, 0.0}` |
| 最小距離 | jointMinDistance | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 最大距離 | jointMaxDistance | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 最小移動 | jointMinDistance | float | 0.01 | -100.0 〜 100.0 | `0.0` |
| 最大移動 | jointMaxDistance | float | 0.01 | -100.0 〜 100.0 | `1.0` |
| ばね周波数 | jointSpringFrequency | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| ばね減衰 | jointSpringDamping | float | 0.01 | 0.0 〜 10.0 | `0.5` |

#### PlatformEffector2D

表示名「プラットフォームエフェクター 2D」／カテゴリ「2D物理」／Inspector描画 `DrawPlatformEffector2DComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 片面 | isTrigger | bool | — | true / false | `false` |

#### SurfaceEffector2D

表示名「サーフェスエフェクター 2D」／カテゴリ「2D物理」／Inspector描画 `DrawSurfaceEffector2DComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 力 | intensity | float | 0.1 | 0.0 〜 100.0 | `1.0` |

#### AreaEffector2D

表示名「エリアエフェクター 2D」／カテゴリ「2D物理」／Inspector描画 `DrawAreaEffector2DComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 力 | intensity | float | 0.1 | 0.0 〜 100.0 | `1.0` |
| 方向 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |

#### PointEffector2D

表示名「ポイントエフェクター 2D」／カテゴリ「2D物理」／Inspector描画 `DrawPointEffector2DComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 力 | intensity | float | 0.1 | 0.0 〜 100.0 | `1.0` |

#### BuoyancyEffector2D

表示名「浮力エフェクター 2D」／カテゴリ「2D物理」／Inspector描画 `DrawBuoyancyEffector2DComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 浮力 | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |

#### AvatarMask

表示名「アバターマスク」／カテゴリ「アニメーション」／Inspector描画 `DrawAvatarMaskComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Mask アセット | assetPath | string | — | 文字列 | `""` |

#### AimConstraint

表示名「エイム制約」／カテゴリ「アニメーション」／Inspector描画 `DrawAimConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| ターゲット方向軸 | constraintAimAxis | int32 | 1 | — | `2` |

#### LookAtConstraint

表示名「ルックアット制約」／カテゴリ「アニメーション」／Inspector描画 `DrawLookAtConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ターゲット ID | connectedGameObjectId | int32 | 1 | — | `-1` |
| 重み | constraintWeight | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 上方向軸 | constraintUpAxis | int32 | 1 | — | `1` |
| ロール角 | constraintRoll | float | 0.1 | -180.0 〜 180.0 | `0.0` |

#### AudioReverbZone

表示名「オーディオリバーブゾーン」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioLowPassFilter, AudioHighPassFilter, AudioEchoFilter, AudioDistortionFilter, AudioReverbFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioLowPassFilter

表示名「オーディオローパスフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioHighPassFilter, AudioEchoFilter, AudioDistortionFilter, AudioReverbFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioHighPassFilter

表示名「オーディオハイパスフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioLowPassFilter, AudioEchoFilter, AudioDistortionFilter, AudioReverbFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioEchoFilter

表示名「オーディオエコーフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioLowPassFilter, AudioHighPassFilter, AudioDistortionFilter, AudioReverbFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioDistortionFilter

表示名「オーディオディストーションフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioLowPassFilter, AudioHighPassFilter, AudioEchoFilter, AudioReverbFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioReverbFilter

表示名「オーディオリバーブフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioLowPassFilter, AudioHighPassFilter, AudioEchoFilter, AudioDistortionFilter, AudioChorusFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AudioChorusFilter

表示名「オーディオコーラスフィルター」／カテゴリ「オーディオ」／Inspector描画 `DrawAudioFilterComponent`／同じ描画関数を共有: AudioReverbZone, AudioLowPassFilter, AudioHighPassFilter, AudioEchoFilter, AudioDistortionFilter, AudioReverbFilter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 効果量 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### CanvasScaler

表示名「キャンバススケーラー」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### GraphicRaycaster

表示名「グラフィックレイキャスター」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### RawImage

表示名「Raw イメージ」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### TextMeshProUGUI

表示名「TextMeshPro UGUI」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Button

表示名「ボタン」／カテゴリ「UI」／Inspector描画 `DrawButtonComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| クリック関数 | buttonOnClickFunction | string | — | 文字列 | `"OnClick"` |

#### Toggle

表示名「トグル」／カテゴリ「UI」／Inspector描画 `DrawToggleComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 現在値 | toggleValue | bool | — | true / false | `false` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 変更関数 | toggleOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |

#### Slider

表示名「スライダー」／カテゴリ「UI」／Inspector描画 `DrawSliderComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 最小値 | sliderMinValue | float | 0.01 | -100000.0 〜 100000.0 | `0.0` |
| 最大値 | sliderMaxValue | float | 0.01 | -100000.0 〜 100000.0 | `1.0` |
| 現在値 | sliderValue | float | 0.01 | sliderMinValue 〜 sliderMaxValue | `0.5` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |

#### Scrollbar

表示名「スクロールバー」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Dropdown

表示名「ドロップダウン」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### TMPDropdown

表示名「TMP ドロップダウン」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### InputField

表示名「入力フィールド」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### TMPInputField

表示名「TMP 入力フィールド」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### ScrollRect

表示名「スクロールレクト」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Mask

表示名「マスク」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### RectMask2D

表示名「レクトマスク 2D」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### HorizontalLayoutGroup

表示名「水平レイアウトグループ」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### VerticalLayoutGroup

表示名「垂直レイアウトグループ」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### GridLayoutGroup

表示名「グリッドレイアウトグループ」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, ContentSizeFitter, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### ContentSizeFitter

表示名「コンテンツサイズフィッター」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, AspectRatioFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### AspectRatioFitter

表示名「アスペクト比フィッター」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, LayoutElement

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### LayoutElement

表示名「レイアウトエレメント」／カテゴリ「UI」／Inspector描画 `DrawUIComponent`／同じ描画関数を共有: CanvasRenderer, Canvas, Image, Text, RectTransform, CanvasScaler, GraphicRaycaster, RawImage, TextMeshProUGUI, Scrollbar, Dropdown, TMPDropdown, InputField, TMPInputField, ScrollRect, Mask, RectMask2D, HorizontalLayoutGroup, VerticalLayoutGroup, GridLayoutGroup, ContentSizeFitter, AspectRatioFitter

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 描画順 | physicsLayer | int32 | 1 | — | `0` |
| アンカー位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 基準解像度 | buttonSize | Vector2 | 1.0 | 1.0 〜 16384.0 | `{160.0, 48.0}` |
| 幅高さの比重 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 入力を受け取る | buttonInteractable | bool | — | true / false | `true` |
| 優先度 | physicsLayer | int32 | 1 | — | `0` |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 文字サイズ | buttonSize.y | float | 1.0 | 8.0 〜 512.0 | `48.0` |
| フォント | textFontIndex | int32(選択) | — | 0=既定 (Yu Gothic) / 1=Meiryo / 2=MS ゴシック / 3=MS 明朝 / 4=Yu Gothic Bold | `0` |
| 画像 | assetPath | string | — | 文字列 | `""` |
| 表示名 | buttonLabel | string | — | 文字列 | `"Button"` |
| 値 | sliderValue | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 変更関数 | sliderOnValueChangedFunction | string | — | 文字列 | `"OnValueChanged"` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 選択肢 (\|区切り) | assetPath | string | — | 文字列 | `""` |
| 選択番号 | inputBehavior | int32 | 1 | — | `0` |
| 入力文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| プレースホルダー | assetPath | string | — | 文字列 | `""` |
| 表示位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 表示サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 横スクロール | uvOffset.x | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 縦スクロール | uvOffset.y | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 切り抜き位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| 切り抜きサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 開始位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| セルサイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 間隔 | sliderValue | float | 0.5 | 0.0 〜 1024.0 | `0.5` |
| 列数 | inputBehavior | int32 | 1 | — | `0` |
| 横を内容へ合わせる | freezePositionX | bool | — | true / false | `false` |
| 縦を内容へ合わせる | freezePositionY | bool | — | true / false | `false` |
| アスペクト比 | sliderValue | float | 0.01 | 0.01 〜 100.0 | `0.5` |
| 優先サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 透明度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### StandaloneInputModule

表示名「スタンドアロン入力モジュール」／カテゴリ「入力・イベント」／Inspector描画 `DrawStandaloneInputModuleComponent`

Inspector編集行なし。

#### InputSystemUIInputModule

表示名「Input System UI 入力モジュール」／カテゴリ「入力・イベント」／Inspector描画 `DrawInputSystemUIInputModuleComponent`

Inspector編集行なし。

#### PlayerInput

表示名「プレイヤー入力」／カテゴリ「入力・イベント」／Inspector描画 `DrawPlayerInputComponent`

Inspector編集行なし。

#### PlayerInputManager

表示名「プレイヤー入力マネージャー」／カテゴリ「入力・イベント」／Inspector描画 `DrawPlayerInputManagerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 共有 Actions | assetPath | string | — | 文字列 | `""` |
| 最大 Player 数 | particleMaxCount | int32 | 1 | — | `256` |

#### TouchInputModule

表示名「タッチ入力モジュール」／カテゴリ「入力・イベント」／Inspector描画 `DrawTouchInputModuleComponent`

Inspector編集行なし。

#### NavMeshObstacle

表示名「NavMesh 障害物」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshObstacleComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 半径 | colliderRadius | float | 0.01 | 0.1 〜 10.0 | `0.5` |
| 高さ | colliderSize.y | float | 0.01 | 0.1 〜 10.0 | `1.0` |
| 移動中も NavMesh を更新 | navCarve | bool | — | true / false | `true` |

#### NavMeshSurface

表示名「NavMesh サーフェス」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshSurfaceComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Agent 半径 | navAgentRadius | float | 0.01 | 0.1 〜 10.0 | `0.5` |
| Agent 高さ | navAgentHeight | float | 0.01 | 0.1 〜 10.0 | `2.0` |
| 最大傾斜角度 | navMaxSlope | float | 1.0 | 0.0 〜 90.0 | `45.0` |
| 最大段差 | navMaxClimb | float | 0.01 | 0.0 〜 10.0 | `0.5` |
| レイヤーマスク | physicsLayer | int32 | 1 | — | `0` |

#### NavMeshModifier

表示名「NavMesh モディファイア」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshModifierComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Area を上書き | navAreaOverride | bool | — | true / false | `false` |
| Area | navArea | int32 | 1 | — | `0` |
| ビルドから除外 | navIgnoreFromBuild | bool | — | true / false | `false` |

#### NavMeshModifierVolume

表示名「NavMesh モディファイアボリューム」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshModifierVolumeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| Area | navArea | int32 | 1 | — | `0` |

#### NavMeshLink

表示名「NavMesh リンク」／カテゴリ「ナビゲーション」／Inspector描画 `DrawNavMeshLinkComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 双方向 | navBidirectional | bool | — | true / false | `true` |
| コスト倍率 | navCostModifier | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 幅 | colliderRadius | float | 0.01 | 0.1 〜 10.0 | `0.5` |

#### AIBehaviorTree

表示名「行動ツリー」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIBehaviorBlackboard

表示名「共有データ（Blackboard）」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIBehaviorSelector

表示名「条件分岐（Selector）」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIBehaviorSequence

表示名「順番実行（Sequence）」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIBehaviorTask

表示名「実行処理（Task）」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIBehaviorDecorator

表示名「条件装飾（Decorator）」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIStateMachine

表示名「状態制御」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIState

表示名「状態」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIStateTransition

表示名「状態遷移」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIGoapPlanner

表示名「目標計画」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIGoapGoal

表示名「目標条件」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIGoapAction

表示名「計画行動」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIGoapWorldState

表示名「世界状態」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIHtnPlanner

表示名「タスク計画」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIHtnDomain

表示名「タスク領域」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIHtnTask

表示名「タスク」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIHtnMethod

表示名「タスク分解」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIPathfindingAgent

表示名「経路探索エージェント」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIMicroPatherGrid

表示名「グリッド経路」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIRecastNavMeshBuilder

表示名「ナビメッシュ生成」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIRecastCrowdAgent

表示名「群衆エージェント」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIPathRequest

表示名「経路要求」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIDynamicObstacle

表示名「動的障害物」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AISteeringAgent

表示名「操舵エージェント」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AISeekSteering

表示名「接近操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIFleeSteering

表示名「逃走操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIArriveSteering

表示名「到着操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIPursuitSteering

表示名「追跡操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIWanderSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIWanderSteering

表示名「徘徊操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIObstacleAvoidanceSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIObstacleAvoidanceSteering

表示名「障害物回避操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIFlockSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIFlockSteering

表示名「群れ操舵」／カテゴリ「AI」／Inspector描画 `DrawAiAgentComponent`／同じ描画関数を共有: AIBehaviorTree, AIStateMachine, AIGoapPlanner, AIHtnPlanner, AIPathfindingAgent, AIRecastCrowdAgent, AISteeringAgent, AISeekSteering, AIFleeSteering, AIArriveSteering, AIPursuitSteering, AIWanderSteering, AIObstacleAvoidanceSteering

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| 最大速度 | navMaxSpeed | float | 0.1 | 0.0 〜 100.0 | `3.5` |
| 最大加速度 | navMaxAcceleration | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 停止距離 | navStoppingDistance | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 回避半径 | navAgentRadius | float | 0.01 | 0.0 〜 20.0 | `0.5` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |

#### AIVisionSensor

表示名「視界センサー」／カテゴリ「AI」／Inspector描画 `DrawAiVisionSensorComponent`／同じ描画関数を共有: AIMotionSensor

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 視界距離 | colliderRadius | float | 0.1 | 0.0 〜 1000.0 | `0.5` |
| 視野角 | colliderSize.x | float | 1.0 | 0.0 〜 360.0 | `1.0` |

#### AIOpenCvCamera

表示名「画像入力カメラ」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIOpenCvObjectDetector

表示名「画像物体検出」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvColorTracker, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIOpenCvColorTracker

表示名「画像色追跡」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIWhisperSpeechRecognizer, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIMotionSensor

表示名「動きセンサー」／カテゴリ「AI」／Inspector描画 `DrawAiVisionSensorComponent`／同じ描画関数を共有: AIVisionSensor

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 視界距離 | colliderRadius | float | 0.1 | 0.0 〜 1000.0 | `0.5` |
| 視野角 | colliderSize.x | float | 1.0 | 0.0 〜 360.0 | `1.0` |

#### AIWhisperSpeechRecognizer

表示名「Whisper 音声認識」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIVoiceCommand

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### AIVoiceCommand

表示名「音声コマンド」／カテゴリ「AI」／Inspector描画 `DrawAiDataComponent`／同じ描画関数を共有: AIBehaviorBlackboard, AIBehaviorSelector, AIBehaviorSequence, AIBehaviorTask, AIBehaviorDecorator, AIState, AIStateTransition, AIGoapGoal, AIGoapAction, AIGoapWorldState, AIHtnDomain, AIHtnTask, AIHtnMethod, AIMicroPatherGrid, AIRecastNavMeshBuilder, AIPathRequest, AIDynamicObstacle, AIOpenCvCamera, AIOpenCvObjectDetector, AIOpenCvColorTracker, AIWhisperSpeechRecognizer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | connectedGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| AI アセットパス | assetPath | string | — | 文字列 | `""` |
| 動作 | inputBehavior | int32(選択) | — | 0=追跡 / 1=逃走 / 2=巡回 / 3=待機 | `0` |
| セルサイズ | colliderRadius | float | 0.01 | 0.1 〜 100.0 | `0.5` |
| グリッド数 | colliderSize | Vector3 | 1.0 | 15.0 〜 161.0 | `{1.0, 1.0, 1.0}` |
| 停止距離 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 生成範囲 | colliderSize | Vector3 | 0.1 | 0.1 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最大傾斜 | navMaxSlope | float | 1.0 | 0.0 〜 89.0 | `45.0` |
| 段差 | navMaxClimb | float | 0.01 | 0.0 〜 100.0 | `0.5` |
| 判定半径 | colliderRadius | float | 0.01 | 0.0 〜 1000.0 | `0.5` |
| 判定サイズ | colliderSize | Vector3 | 0.01 | 制限なし | `{1.0, 1.0, 1.0}` |

#### ParticleSystem

表示名「パーティクルシステム」／カテゴリ「エフェクト」／Inspector描画 `DrawParticleSystemComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Effect Asset | assetPath | string | — | 文字列 | `""` |
| 自動再生 | animationPlayOnAwake | bool | — | true / false | `true` |
| ループ | particleLooping | bool | — | true / false | `true` |
| 再生時間 | particleDuration | float | 0.1 | 0.01 〜 3600.0 | `5.0` |
| 開始遅延 | particleStartDelay | float | 0.1 | 0.0 〜 3600.0 | `0.0` |
| プリウォーム | particlePrewarm | bool | — | true / false | `false` |
| 寿命 | particleLifetime | float | 0.1 | 0.01 〜 60.0 | `2.0` |
| 寿命のばらつき | particleLifetimeRandomness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 最大数 | particleMaxCount | int32 | 1 | — | `256` |
| 1秒当たり | particleRate | float | 1.0 | 0.0 〜 10000.0 | `10.0` |
| 開始バースト | particleBurstCount | int32 | 1 | — | `0` |
| 形状 | particleShape | int32(選択) | — | 0=点 / 1=球 / 2=コーン / 3=ボックス | `0` |
| シミュレーション空間 | particleSimulationSpace | int32(選択) | — | 0=ワールド / 1=ローカル | `0` |
| 半径 | particleShapeRadius | float | 0.01 | 0.0 〜 1000.0 | `1.0` |
| コーン角度 | particleShapeAngle | float | 1.0 | 0.0 〜 89.0 | `25.0` |
| ボックス範囲 | particleBoxSize | Vector3 | 0.01 | 0.0 〜 1000.0 | `{1.0, 1.0, 1.0}` |
| 放出方向 | particleDirection | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 1.0, 0.0}` |
| 運動方式 | particleMotionType | int32(選択) | — | 0=直線 / 1=軌道 / 2=渦 / 3=波 / 4=吸引 / 5=雲 / 6=爆発 / 水しぶき / 7=Projectile Trail / 8=Ocean Spray / Mist | `0` |
| 初速度 | particleSpeed | float | 0.1 | 0.0 〜 1000.0 | `5.0` |
| 速度のばらつき | particleSpeedRandomness | float | 0.01 | 0.0 〜 1.0 | `0.2` |
| 重力 | particleGravity | float | 0.1 | -100.0 〜 100.0 | `0.0` |
| 空気抵抗 | particleDrag | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| 終了速度倍率 | particleEndSpeedMultiplier | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| 回転速度 | particleRotationSpeed | float | 1.0 | -3600.0 〜 3600.0 | `0.0` |
| 乱流の強さ | particleNoiseStrength | float | 0.01 | 0.0 〜 1000.0 | `0.0` |
| 乱流の周波数 | particleNoiseFrequency | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 運動中心 | particleMotionCenter | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 角速度 | particleAngularSpeed | float | 1.0 | -3600.0 〜 3600.0 | `45.0` |
| 半径方向加速度 | particleRadialAcceleration | float | 0.1 | -1000.0 〜 1000.0 | `0.0` |
| 波の振幅 | particleWaveAmplitude | float | 0.1 | 0.0 〜 1000.0 | `0.0` |
| 波の周波数 | particleWaveFrequency | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 吸引力 | particleAttractorStrength | float | 0.1 | 0.0 〜 1000.0 | `0.0` |
| 衝突 | particleCollision | bool | — | true / false | `false` |
| 衝突方式 | collisionDetectionMode | int32(選択) | — | 0=Depth (画面内エフェクト) / 1=Physics SDF (物理オブジェクト) | `0` |
| 反発 | particleCollisionBounce | float | 0.01 | 0.0 〜 1.0 | `0.35` |
| 摩擦 | particleCollisionFriction | float | 0.01 | 0.0 〜 1.0 | `0.2` |
| 開始サイズ | particleSize | float | 0.01 | 0.001 〜 100.0 | `0.5` |
| 終了サイズ | particleEndSize | float | 0.01 | 0.0 〜 100.0 | `0.0` |
| サイズのばらつき | particleSizeRandomness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 開始アルファ | particleStartAlpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 終了アルファ | particleEndAlpha | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射強度 | particleEmissionStrength | float | 0.05 | 0.0 〜 1000.0 | `1.0` |
| 板の向き | particleBillboardMode | int32(選択) | — | 0=カメラ正対 / 1=Y軸固定 / 2=速度方向 / 3=World XY固定 | `0` |
| 速度方向の長さ | particleBillboardStretch | float | 0.05 | 0.01 〜 100.0 | `1.0` |
| FBX / OBJ | particleRenderAssetPath | string | — | 文字列 | — |

#### VisualEffect

表示名「ビジュアルエフェクト」／カテゴリ「エフェクト」／Inspector描画 `DrawVisualEffectComponent`

Inspector編集行なし。

#### LensFlare

表示名「レンズフレア」／カテゴリ「エフェクト」／Inspector描画 `DrawLensFlareComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| テクスチャ | assetPath | string | — | 文字列 | `""` |
| 明るさ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| 透明度 | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Projector

表示名「プロジェクター」／カテゴリ「エフェクト」／Inspector描画 `DrawProjectorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| テクスチャ | assetPath | string | — | 文字列 | `""` |
| 視野角 | intensity | float | 1.0 | 1.0 〜 180.0 | `1.0` |
| 投影サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 1000.0 | `{1.0, 1.0, 1.0}` |
| 透明度 | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### DecalProjector

表示名「デカールプロジェクター」／カテゴリ「エフェクト」／Inspector描画 `DrawDecalProjectorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| テクスチャ | assetPath | string | — | 文字列 | `""` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 透明度 | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |

#### Terrain

表示名「テレイン」／カテゴリ「地形・タイルマップ」／Inspector描画 `DrawTerrainComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Height Map | assetPath | string | — | 文字列 | `""` |
| サイズ X / 高さ / Z | colliderSize | Vector3 | 1.0 | 0.01 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 最高LOD解像度 | oceanGridResolution | int32 | 1 | — | `2048` |

#### Tilemap

表示名「タイルマップ」／カテゴリ「地形・タイルマップ」／Inspector描画 `DrawTilemapComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| タイル画像 | assetPath | string | — | 文字列 | `""` |
| サイズ | colliderSize | Vector3 | 1.0 | 1.0 〜 1000.0 | `{1.0, 1.0, 1.0}` |

#### TilemapRenderer

表示名「タイルマップレンダラー」／カテゴリ「地形・タイルマップ」／Inspector描画 `DrawRendererComponent`／同じ描画関数を共有: ModelRenderer, SkinnedMeshRenderer, SpriteRenderer, BillboardRenderer, ParticleSystemRenderer

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| マテリアル数 | materialCount | int32 | 1 | — | — |
| 描画方式 | alphaMode | int32(選択) | — | 0=不透明 / 1=アルファマスク / 2=半透明 | `0` |
| Lighting方式 | lightingMode | int32(選択) | — | 0=Lightingなし / 1=Lambert / 2=Half Lambert / 3=PBR | `3` |
| 両面描画 | doubleSided | bool | — | true / false | `false` |
| 強さ | intensity | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| メタリック | metallic | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 粗さ | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 屈折率 | ior | float | 0.01 | 1.0 〜 3.0 | `1.0` |
| アルファ | alpha | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| アルファ境界 | alphaCutoff | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射 | reflectionStrength | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 放射 | emissionStrength | float | 0.01 | 0.0 〜 50.0 | `0.0` |
| FBX内画像を自動使用 | useImportedMaterialTextures | bool | — | true / false | `false` |
| UV繰り返し | uvTiling | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 1.0}` |
| UVオフセット | uvOffset | Vector2 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0}` |
| 法線強度 | normalScale | float | 0.01 | -2.0 〜 2.0 | `1.0` |
| AO強度 | ambientOcclusionStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 高さ強度 | heightScale | float | 0.001 | -0.2 〜 0.2 | `0.02` |
| クリアコート | clearCoat | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| コート粗さ | clearCoatRoughness | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| 透過 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 表面下散乱 | subsurface | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 材質の厚み | materialThickness | float | 0.001 | 0.001 〜 10.0 | `0.1` |
| 異方性 | anisotropy | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| 異方性回転 | anisotropyRotation | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 濡れ | materialWetness | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 水際の高さ | materialWaterlineHeight | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 水際の幅 | materialWaterlineWidth | float | 0.01 | 0.001 〜 1000.0 | `0.25` |
| 鏡面色 | specularTint | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン | sheen | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| シーン色 | sheenTint | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 頂点数 | vertexCount | int32 | 1 | — | — |
| アニメーションクリップ数 | animationClipCount | int32 | 1 | — | — |

#### Grid

表示名「グリッド」／カテゴリ「地形・タイルマップ」／Inspector描画 `DrawGridComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| セルサイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |

#### LocalMove

表示名「ローカル移動」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawLocalMoveComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ローカル方向 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 速度 | inputMoveSpeed | float | 0.01 | 0.0 〜 100.0 | `3.0` |

#### RollingMove

表示名「ローリング移動」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRollingMoveComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 進行方向 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| トルク | rollingTorque | float | 0.1 | 0.0 〜 10000.0 | `50.0` |
| 馬力 | rollingHorsepower | float | 0.1 | 0.0 〜 1000.0 | `5.0` |
| 半径 | colliderRadius | float | 0.01 | 0.01 〜 100.0 | `0.5` |

#### PostProcess

表示名「ポストプロセス」／カテゴリ「ライト・環境」／Inspector描画 `DrawPostProcessComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| アンチエイリアス | aaMode | int32(選択) | — | 0=None / 1=FXAA / 2=SMAA / 3=Temporal | `2` |
| SMAA しきい値 | smaaThreshold | float | 0.001 | 0.001 〜 0.5 | `0.10` |
| SMAA 角丸め | smaaCornerRounding | float | 1.0 | 0.0 〜 100.0 | `25.0` |
| Temporal シャープ | temporalSharpness | float | 0.01 | 0.0 〜 1.0 | `0.08` |
| Temporal 履歴ブレンド | temporalBlendRatio | float | 0.01 | 0.0 〜 0.98 | `0.90` |
| SSR | ssrEnabled | bool | — | true / false | `false` |
| 明部しきい値 | bloomThreshold | float | 0.01 | 0.0 〜 10.0 | `1.0` |
| しきい値遷移 | bloomSoftKnee | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 強さ | glareIntensityByMode | float | 0.01 | 0.0 〜 10.0 | — |
| にじみ | glareSizeByMode | float | 0.01 | 0.0 〜 1.0 | — |
| sizeLabel | glareSizeByMode | float | 0.01 | 0.1 〜 8.0 | — |
| 減衰 | glareFadeByMode | float | 0.01 | 0.0 〜 1.0 | — |
| 角度 | glareAngleByMode | float | 1.0 | -180.0 〜 180.0 | — |
| 色ずれ | glareColorModulationByMode | float | 0.01 | 0.0 〜 1.0 | — |
| 光条数 | glareStreakCountByMode | int32 | 1 | — | — |
| 光源位置 X | glareCenterByMode | float | 0.01 | 0.0 〜 1.0 | — |
| 光源位置 Y | glareCenterByMode | float | 0.01 | 0.0 〜 1.0 | — |
| 強さ | filterStrengthByMode | float | 0.01 | 0.0 〜 2.0 | — |
| 最終明るさ | finalBrightness | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| トーンマップ | compositeToneMappingMode | int32(選択) | — | 0=Reinhard / 1=Filmic / 2=Timothy / 3=Uncharted2 / 4=ACES | `4` |
| 露出 | compositeExposure | float | 0.01 | 0.0 〜 8.0 | `1.0` |
| 自動露出 | compositeAutoExposureEnabled | bool | — | true / false | `true` |
| 自動露出 下限 | compositeMinimumExposure | float | 0.01 | 0.01 〜 8.0 | `0.80` |
| 自動露出 上限 | compositeMaximumExposure | float | 0.01 | 0.01 〜 16.0 | `1.25` |
| 露出追従速度 | compositeExposureAdaptationSpeed | float | 0.05 | 0.0 〜 10.0 | `2.0` |
| 基準輝度 | compositeTargetLuminance | float | 0.01 | 0.01 〜 1.0 | `0.18` |
| ホワイトポイント | compositeWhitePoint | float | 0.1 | 0.1 〜 20.0 | `3.0` |
| 彩度 | compositeSaturation | float | 0.01 | 0.0 〜 4.0 | `1.02` |
| コントラスト | compositeContrast | float | 0.01 | 0.0 〜 4.0 | `1.04` |
| 色温度 | compositeTemperature | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| Tint | compositeTint | float | 0.01 | -1.0 〜 1.0 | `0.0` |
| Lift | compositeLift | Vector3 | 0.005 | -1.0 〜 1.0 | `{0.0, 0.0, 0.0}` |
| Gamma | compositeGamma | float | 0.01 | 0.1 〜 4.0 | `1.0` |
| Gain | compositeGain | Vector3 | 0.01 | 0.0 〜 4.0 | `{1.0, 1.0, 1.0}` |
| 局所コントラスト | compositeLocalContrast | float | 0.01 | 0.0 〜 1.0 | `0.14` |
| 出力ディザリング | compositeOutputDither | float | 0.05 | 0.0 〜 2.0 | `0.65` |
| SSGI | compositeSsgiEnabled | bool | — | true / false | `false` |
| SSGI強度 | compositeSsgiIntensity | float | 0.01 | 0.0 〜 4.0 | `0.30` |
| SSGI半径 | compositeSsgiRadiusPixels | float | 1.0 | 1.0 〜 128.0 | `14.0` |
| カラーLUT画像 | compositeColorLutAssetPath | string | — | 文字列 | — |
| カラーLUT強度 | compositeColorLutStrength | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| ビネット | compositeVignetteStrength | float | 0.01 | 0.0 〜 2.0 | `0.0` |
| ビネット半径 | compositeVignetteRadius | float | 0.01 | 0.0 〜 1.0 | `0.97` |
| フィルムグレイン | compositeFilmGrain | float | 0.01 | 0.0 〜 2.0 | `0.015` |
| 色収差 | compositeChromaticAberration | float | 0.01 | 0.0 〜 2.0 | `0.01` |
| AO強度 | compositeAmbientOcclusionStrength | float | 0.01 | 0.0 〜 2.0 | `0.55` |
| 診断表示 | compositeDebugView | int32(選択) | — | 0=0 通常 / 1=1 基準のみ (LocalContrast/Bloomとも無効) / 2=2 元画像再参照のみ (LocalContrastのみ有効) / 3=3 ブルームのみ (Bloomのみ有効) / 4=4 両方 (LocalContrast+Bloom) | `0` |

#### Environment

表示名「環境」／カテゴリ「ライト・環境」／Inspector描画 `DrawEnvironmentComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 環境画像 | assetPath | string | — | 文字列 | `""` |
| 環境画像を使用 | environmentTextureEnabled | bool | — | true / false | `false` |
| 露出 | intensity | float | 0.01 | 0.0 〜 8.0 | `1.0` |
| 地平線のぼかし | roughness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 反射への寄与 | reflectionStrength | float | 0.01 | 0.0 〜 4.0 | `0.0` |
| 環境光 | metallic | float | 0.01 | 0.0 〜 4.0 | `0.0` |
| 放射の強さ | emissionStrength | float | 0.01 | 0.0 〜 10.0 | `0.0` |
| 環境テクスチャ回転 | environmentTextureRotation | float | 0.01 | 0.0 〜 6.2832 | `0.0` |
| MIPバイアス | environmentTextureMipBias | float | 0.01 | 0.0 〜 4.0 | `0.0` |
| 体積雲を使用 | volumetricCloudEnabled | bool | — | true / false | `false` |
| 雲量 | volumetricCloudCoverage | float | 0.01 | 0.0 〜 1.0 | `0.52` |
| 密度 | volumetricCloudDensity | float | 0.01 | 0.0 〜 4.0 | `1.15` |
| スケール | volumetricCloudScale | float | 0.0001 | 0.0001 〜 1.0 | `0.0018` |
| 移動速度 | volumetricCloudSpeed | float | 0.01 | -1000.0 〜 1000.0 | `8.0` |
| 高度 | volumetricCloudHeight | float | 1.0 | -10000.0 〜 100000.0 | `900.0` |
| 厚さ | volumetricCloudThickness | float | 1.0 | 1.0 〜 100000.0 | `650.0` |
| 光吸収 | volumetricCloudLightAbsorption | float | 0.01 | 0.0 〜 8.0 | `1.25` |
| 銀縁 | volumetricCloudSilverLining | float | 0.01 | 0.0 〜 4.0 | `0.75` |
| 熱気の強さ | environmentHeatIntensity | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 地平線中心 | environmentHeatHorizonCenter | float | 0.01 | 0.0 〜 1.0 | `0.46` |
| 地平線範囲 | environmentHeatHorizonWidth | float | 0.01 | 0.01 〜 1.0 | `0.16` |
| 太陽方向の影響 | environmentHeatSunInfluence | float | 0.01 | 0.0 〜 1.0 | `0.55` |
| 歪みスケール | environmentHeatDistortionScale | float | 0.01 | 0.01 〜 4.0 | `0.65` |

#### FreeTransform

表示名「自由移動/回転」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawFreeTransformComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 移動入力 | velocity | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| 移動速度 | freeMoveSpeed | float | 0.1 | 0.0 〜 100.0 | `5.0` |
| 回転入力(deg/s) | freeRotationInput | Vector3 | 1.0 | 制限なし | `{0.0, 0.0, 0.0}` |
| 回転速度 | freeRotateSpeed | float | 1.0 | 0.0 〜 360.0 | `90.0` |
| 移動 X | moveX | bool | — | true / false | — |
| 移動 Y | moveY | bool | — | true / false | — |
| 移動 Z | moveZ | bool | — | true / false | — |
| 回転 X | rotX | bool | — | true / false | — |
| 回転 Y | rotY | bool | — | true / false | — |
| 回転 Z | rotZ | bool | — | true / false | — |
| ローカル空間 | freeUseLocalSpace | bool | — | true / false | `true` |

#### AutoConvexCollision

表示名「Auto Convex Collision」／カテゴリ「3D物理」／Inspector描画 `DrawAutoConvexCollisionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 中心 | colliderCenter | Vector3 | 0.01 | 制限なし | `{0.0, 0.0, 0.0}` |
| サイズ | colliderSize | Vector3 | 0.01 | 0.01 〜 100.0 | `{1.0, 1.0, 1.0}` |
| 最大凸包数 | autoConvexMaximumHulls | int32 | 1 | — | `8` |
| 入力頂点数 | sourceVertexCount | int32 | 1 | — | — |

#### Ocean

表示名「Ocean」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawOceanComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| グリッド解像度 | oceanGridResolution | int32 | 1 | — | `2048` |
| 海面サイズ | oceanSize | float | 1.0 | 1.0 〜 20000.0 | `240.0` |
| 波の高さ | oceanWaveHeight | float | 0.05 | 0.0 〜 2000.0 | `1.8` |
| 最大波高 | oceanMaxWaveHeight | float | 0.05 | 0.0 〜 4000.0 | `4.5` |
| 波長 | oceanWaveLength | float | 0.1 | 0.1 〜 10000.0 | `28.0` |
| 波の速度 | oceanWaveSpeed | float | 0.01 | -10.0 〜 10.0 | `1.0` |
| 時間倍率 | oceanTimeScale | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| Choppiness | oceanChoppiness | float | 0.01 | -4.0 〜 4.0 | `0.65` |
| 主波方向 XZ | oceanPrimaryDirection | Vector2 | 0.01 | -100.0 〜 100.0 | `{1.0, 0.28}` |
| 副波方向 XZ | oceanSecondaryDirection | Vector2 | 0.01 | -100.0 〜 100.0 | `{-0.45, 1.0}` |
| 副波の強さ | oceanSecondaryWaveScale | float | 0.01 | 0.0 〜 2.0 | `0.45` |
| 細波の波長比 | oceanRippleScale | float | 0.005 | 0.02 〜 1.0 | `0.22` |
| 細波の強さ | oceanRippleStrength | float | 0.005 | 0.0 〜 1.0 | `0.12` |
| 風速 | oceanWindSpeed | float | 0.1 | 0.1 〜 80.0 | `14.0` |
| 水深 | oceanWaterDepth | float | 0.5 | 0.1 〜 5000.0 | `80.0` |
| 方向分散 | oceanDirectionSpread | float | 0.01 | 0.0 〜 3.14159 | `0.35` |
| うねりの強さ | oceanSwellStrength | float | 0.01 | 0.0 〜 2.0 | `0.65` |
| スペクトルシード | oceanSpectrumSeed | float | 1.0 | 0.0 〜 65535.0 | `7.0` |
| 波頭の尖り | oceanCrestSharpness | float | 0.01 | 0.0 〜 1.0 | `0.65` |
| 泡の強さ | oceanFoamStrength | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| 泡の閾値 | oceanFoamThreshold | float | 0.01 | 0.0 〜 1.0 | `0.58` |
| 粗さ | oceanRoughness | float | 0.01 | 0.035 〜 1.0 | `0.12` |
| 反射 | oceanReflectionStrength | float | 0.01 | 0.0 〜 2.0 | `0.85` |
| 屈折 | transmission | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 微細法線 | oceanDetailNormalStrength | float | 0.01 | 0.0 〜 2.0 | `0.45` |
| 吸収距離 | oceanAbsorptionDistance | float | 0.1 | 0.1 〜 500.0 | `18.0` |
| 屈折の歪み | oceanRefractionDistortion | float | 0.005 | 0.0 〜 1.0 | `0.08` |
| 太陽Diffuse影響 | oceanSunDiffuseInfluence | float | 0.01 | 0.0 〜 3.0 | `1.0` |
| 太陽Diffuse下限 | oceanDiffuseFloor | float | 0.01 | 0.0 〜 1.0 | `0.22` |
| 太陽Specular影響 | oceanSunSpecularInfluence | float | 0.01 | 0.0 〜 3.0 | `1.0` |
| 太陽Glitter影響 | oceanSunGlitterInfluence | float | 0.01 | 0.0 〜 3.0 | `1.0` |
| Sky Reflection影響 | oceanSkyReflectionInfluence | float | 0.01 | 0.0 〜 3.0 | `1.0` |
| Ambient影響 | oceanAmbientInfluence | float | 0.01 | 0.0 〜 3.0 | `1.0` |
| 大波反射影響 | oceanMacroReflectionInfluence | float | 0.01 | 0.0 〜 2.0 | `1.0` |
| 曲率感度 | oceanCurvatureInfluence | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| 谷の環境遮蔽 | oceanTroughOcclusionStrength | float | 0.005 | 0.0 〜 0.25 | `0.08` |
| 波頭Haze | oceanCrestHazeStrength | float | 0.01 | 0.0 〜 1.0 | `0.16` |
| 波頭細波増幅 | oceanCrestDetailBoost | float | 0.01 | 0.0 〜 1.0 | `0.18` |
| 斜面屈折影響 | oceanSlopeRefractionInfluence | float | 0.01 | 0.0 〜 2.0 | `0.35` |
| 中波構造 | oceanMediumWaveStrength | float | 0.01 | 0.0 〜 3.0 | `1.35` |
| 波形の色分離 | oceanWaveColorSeparation | float | 0.01 | 0.0 〜 1.0 | `0.22` |
| 形状による粗さ差 | oceanShapeRoughnessVariation | float | 0.01 | 0.0 〜 0.5 | `0.18` |
| 近距離Detail保持 | oceanDetailFilterSharpness | float | 0.01 | 0.5 〜 2.5 | `1.55` |
| 浅角度形状保持 | oceanGrazingShapeVisibility | float | 0.01 | 0.0 〜 1.0 | `0.35` |
| デバッグ表示 | oceanDebugView | int32(選択) | — | 0=0 通常 / 1=1 太陽Diffuse / 2=2 GGX Specular / 3=3 Sun Glitter / 4=4 Glitter反射整列 / 5=5 Glitter最終Mask / 6=6 Medium Normal / 7=7 Fine Normal / 8=8 Normal差(x8) / 9=9 傾き量 / 10=10 中波構造 / 11=11 Foam / 12=12 Sky反射 / 13=13 屈折Scene / 14=14 Caustics / 15=15 水中実体被覆 / 16=16 FFT泡チャンネル / 17=17 Fine Delta Slope / 18=18 Fine Lobe Normal / 19=19 Fine Micro Roughness / 20=20 Fine Sun Specular / 21=21 Fine Env Normal Delta / 22=22 Fine Lobe OFF (比較用) / 23=23 Medium Slope (17と比較) / 24=24 Fine Env Final Delta / 25=25 Env Normal 角度差 / 26=26 Fine GGX D / 27=27 Fine NdotH / 28=28 Fine Raw Lobe / 29=29 Medium参照Lobe / 30=30 ReflectDir 21 / 31=31 ReflectDir 24 / 32=32 ReflectDir差分 / 33=33 RawEnvSample 21 / 34=34 RawEnvSample 24 / 35=35 RawEnvSample差分 / 36=36 Compress後差分 / 37=37 Delta21-24差分 / 38=38 RGB Delta 21 / 39=39 RGB Delta 24 / 40=40 Delta21-24直接 / 41=41 定数:黒 / 42=42 定数:グレー / 43=43 定数:赤 / 44=44 絶対量 x4 / 45=45 絶対量 x64 / 46=46 絶対量 x256 | `0` |
| 近景Pixel変位 | oceanPerPixelDisplacementStrength | float | 0.01 | 0.0 〜 0.5 | `0.0` |
| Pixel変位反復数 | oceanPerPixelDisplacementSteps | int32 | 1 | — | `4` |
| Pixel変位距離 | oceanPerPixelDisplacementDistance | float | 1.0 | 5.0 〜 150.0 | `45.0` |
| GPU適応細分化 | oceanGpuTessellationEnabled | bool | — | true / false | `false` |
| 細分化目標Pixel | oceanTessellationTargetPixels | float | 1.0 | 4.0 〜 64.0 | `12.0` |
| 細分化最大係数 | oceanTessellationMaximumFactor | float | 1.0 | 1.0 〜 8.0 | `4.0` |
| グリッター強度 | oceanGlitterIntensity | float | 0.01 | 0.0 〜 8.0 | `1.0` |
| グリッター鋭さ | oceanGlitterSharpness | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| グリッター密度 | oceanGlitterDensity | float | 0.01 | 0.0 〜 4.0 | `1.0` |
| グリッター開始閾値 | oceanGlitterThreshold | float | 0.01 | 0.0 〜 0.99 | `0.0` |
| グリッター最大輝度 | oceanGlitterMaxClamp | float | 0.1 | 0.1 〜 64.0 | `7.5` |

#### Buoyancy

表示名「Buoyancy」／カテゴリ「物理」／Inspector描画 `DrawBuoyancyComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 Ocean | buoyancyOceanGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動検出 | `-1` |
| 自動物理を使用 | buoyancyAutomaticPhysicalProperties | bool | — | true / false | `false` |
| 水密度 kg/m3 | buoyancyWaterDensity | float | 1.0 | 0.0 〜 1000000.0 | `1025.0` |
| 目標水没率 | buoyancyTargetSubmersionRatio | float | 0.01 | 0.01 〜 0.99 | `0.55` |
| 浮力中心 | buoyancyCenterOffset | Vector3 | 0.01 | -1000.0 〜 1000.0 | `{0.0, 0.0, 0.0}` |
| 船体サイズ | buoyancyHullSize | Vector3 | 0.05 | 0.05 〜 10000.0 | `{3.0, 1.2, 6.0}` |
| 浮力 | buoyancyStrength | float | 0.1 | 0.0 〜 1000.0 | `18.0` |
| 上下減衰 | buoyancyDamping | float | 0.05 | 0.0 〜 100.0 | `5.0` |
| 前後の水抵抗 | buoyancyWaterDrag | float | 0.05 | 0.0 〜 100.0 | `1.4` |
| 横方向の水抵抗 | buoyancyLateralDrag | float | 0.05 | 0.0 〜 100.0 | `4.0` |
| 上下の水抵抗 | buoyancyVerticalDrag | float | 0.05 | 0.0 〜 100.0 | `2.5` |
| 回転抵抗 | buoyancyAngularDrag | float | 0.05 | 0.0 〜 100.0 | `1.8` |
| 着水衝撃 | buoyancySlammingStrength | float | 0.05 | 0.0 〜 100.0 | `2.0` |
| 波の横押し | buoyancyNormalInfluence | float | 0.01 | 0.0 〜 1.0 | `0.2` |

#### RailMovement

表示名「レール移動」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRailMovementComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Rail Path | railPathGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 速度 | railSpeed | float | 0.1 | -1000.0 〜 1000.0 | `8.0` |
| 加速度 | railAcceleration | float | 0.1 | 0.0 〜 10000.0 | `0.0` |
| 減速度 | railDeceleration | float | 0.1 | 0.0 〜 10000.0 | `0.0` |
| 開始位置 | railStartNormalized | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 向きの先読み | railLookAheadDistance | float | 0.05 | 0.01 〜 1000.0 | `1.0` |
| 左右・上下の範囲 | railMovementRange | Vector2 | 0.1 | 0.0 〜 10000.0 | `{5.0, 3.0}` |
| 開始オフセット | railStartOffset | Vector2 | 0.1 | -10000.0 〜 10000.0 | `{0.0, 0.0}` |
| 移動速度 | railOffsetMoveSpeed | float | 0.1 | 0.0 〜 10000.0 | `8.0` |
| PlayerInputを使用 | railUsePlayerInput | bool | — | true / false | `false` |
| Action Map | railInputActionMapName | string | — | 文字列 | `"Player"` |
| Vector2 Action | railInputActionName | string | — | 文字列 | `"Move"` |
| 移動方式 | railMovementMode | int32(選択) | — | 0=Transform 追従 / 1=Dynamic Rigidbody 物理サーボ / 2=Dynamic Rigidbody 船体推進 | `0` |
| 位置追従軸 | railPositionInfluence | Vector3 | 0.01 | 0.0 〜 1.0 | `{1.0, 1.0, 1.0}` |
| 位置ばね | railPositionSpring | float | 0.1 | 0.0 〜 1000.0 | `8.0` |
| 位置減衰 | railPositionDamping | float | 0.1 | 0.0 〜 1000.0 | `5.0` |
| 最大加速度 | railMaximumAcceleration | float | 0.1 | 0.0 〜 10000.0 | `30.0` |
| 回転追従軸 | railRotationInfluence | Vector3 | 0.01 | 0.0 〜 1.0 | `{1.0, 1.0, 1.0}` |
| 回転ばね | railRotationSpring | float | 0.1 | 0.0 〜 1000.0 | `8.0` |
| 回転減衰 | railRotationDamping | float | 0.1 | 0.0 〜 1000.0 | `4.0` |
| 最大角加速度 | railMaximumAngularAcceleration | float | 0.1 | 0.0 〜 10000.0 | `12.0` |
| 船首ローカル軸 | railLocalForwardAxis | int32(選択) | — | 0=+Z / 1=-Z / 2=+X / 3=-X | `0` |
| 推進速度ゲイン | railEngineSpeedGain | float | 0.1 | 0.0 〜 50.0 | `3.0` |
| エンジン加速応答 | railEngineAccelResponse | float | 0.1 | 0.0 〜 100.0 | `8.0` |
| エンジン減速応答 | railEngineDecelResponse | float | 0.1 | 0.0 〜 100.0 | `4.0` |
| 最大前進加速度 | railEngineMaxAcceleration | float | 0.5 | 0.0 〜 200.0 | `14.0` |
| 操舵基本先読み距離(m) | railSteeringBaseLookAheadDistance | float | 0.5 | 0.0 〜 200.0 | `8.0` |
| 操舵先読み時間(秒) | railSteeringLookAheadTime | float | 0.05 | 0.0 〜 5.0 | `0.4` |
| 操舵Yaw強さ | railSteeringYawGain | float | 0.1 | 0.0 〜 100.0 | `6.0` |
| 操舵Yawダンピング | railSteeringYawDamping | float | 0.1 | 0.0 〜 100.0 | `4.0` |
| 最大Yaw角加速度 | railSteeringMaxYawAngularAcceleration | float | 0.1 | 0.0 〜 100.0 | `8.0` |
| 横補助Dead Zone(m) | railLateralAssistDeadZone | float | 0.1 | 0.0 〜 50.0 | `1.0` |
| 横補助開始距離(m) | railLateralAssistSoftRadius | float | 0.1 | 0.0 〜 50.0 | `3.0` |
| 横補助緊急距離(m) | railLateralAssistEmergencyRadius | float | 0.1 | 0.0 〜 100.0 | `6.0` |
| 横補助最大倍率 | railLateralAssistMaxMultiplier | float | 0.1 | 0.0 〜 10.0 | `1.0` |
| 船体横滑り抑制を使用 | railHullLateralGripEnabled | bool | — | true / false | `true` |
| 横グリップ強さ | railHullLateralGripStrength | float | 0.1 | 0.0 〜 50.0 | `2.0` |
| 横グリップ最大加速度 | railHullLateralGripMaxAcceleration | float | 0.5 | 0.0 〜 200.0 | `20.0` |
| 横グリップ開始速度(m/s) | railHullLateralGripMinSpeed | float | 0.1 | 0.0 〜 50.0 | `3.0` |
| 横グリップ最大速度(m/s) | railHullLateralGripFullSpeed | float | 0.1 | 0.0 〜 50.0 | `15.0` |
| 横滑りDead Zone速度(m/s) | railHullLateralGripDeadZoneSpeed | float | 0.05 | 0.0 〜 10.0 | `0.5` |
| 横滑り角補助開始角度(度) | railHullLateralGripSlipStartDegrees | float | 1.0 | 0.0 〜 90.0 | `5.0` |
| 横滑り角補助最大角度(度) | railHullLateralGripSlipFullDegrees | float | 1.0 | 0.0 〜 90.0 | `30.0` |
| Mode2 最大合成加速度 | railMode2MaxCombinedAcceleration | float | 1.0 | 0.0 〜 500.0 | `60.0` |
| Mode2移動方式 | railMode2MovementStyle | int32(選択) | — | 0=Boat Autopilot / 1=Rail Ride | `0` |
| Rail RideのYawサンプル距離(m) | railRideYawSampleDistance | float | 0.1 | 0.1 〜 20.0 | `2.0` |
| 最大ロール角度 | railMaximumRollAngle | float | 1.0 | 0.0 〜 90.0 | `15.0` |
| ロール復元力 | railRollRestorationStrength | float | 0.5 | 0.0 〜 100.0 | `10.0` |
| ロールダンピング | railRollDamping | float | 0.5 | 0.0 〜 100.0 | `5.0` |
| 最大ピッチ角度 | railMaximumPitchAngle | float | 1.0 | 0.0 〜 90.0 | `10.0` |
| ピッチ復元力 | railPitchRestorationStrength | float | 0.5 | 0.0 〜 100.0 | `10.0` |
| ピッチダンピング | railPitchDamping | float | 0.5 | 0.0 〜 100.0 | `5.0` |
| 最大ヨー角度 | railMaximumYawAngle | float | 1.0 | 0.0 〜 180.0 | `0.0` |
| ヨー復元力 | railYawRestorationStrength | float | 0.5 | 0.0 〜 100.0 | `10.0` |
| ヨーダンピング | railYawDamping | float | 0.5 | 0.0 〜 100.0 | `5.0` |
| Yaw Safety Assistを使用 | railYawSafetyAssistEnabled | bool | — | true / false | `true` |
| Stage1 開始角度(度) | railYawSafetyStage1Degrees | float | 1.0 | 0.0 〜 180.0 | `10.0` |
| Stage2 開始角度(度) | railYawSafetyStage2Degrees | float | 1.0 | 0.0 〜 180.0 | `20.0` |
| Stage4 到達角度(度) | railYawSafetyStage4Degrees | float | 1.0 | 0.0 〜 180.0 | `45.0` |
| 最大復元倍率 | railYawSafetyMaxRestorationScale | float | 0.1 | 1.0 〜 10.0 | `3.0` |
| 最小速度倍率 | railYawSafetyMinSpeedScale | float | 0.01 | 0.0 〜 1.0 | `0.15` |
| Forward Position Error 上限(m) | railMaxForwardRecoveryError | float | 0.5 | 0.0 〜 500.0 | `20.0` |
| Attitude Safety Assistを使用 | railAttitudeSafetyAssistEnabled | bool | — | true / false | `false` |
| Roll Free角度(度) | railRollFreeDegrees | float | 1.0 | 0.0 〜 90.0 | `15.0` |
| Roll Emergency角度(度) | railRollEmergencyDegrees | float | 1.0 | 0.0 〜 180.0 | `45.0` |
| Pitch Free角度(度) | railPitchFreeDegrees | float | 1.0 | 0.0 〜 90.0 | `12.0` |
| Pitch Emergency角度(度) | railPitchEmergencyDegrees | float | 1.0 | 0.0 〜 180.0 | `40.0` |
| Attitude復元力 | railAttitudeSafetyStrength | float | 0.5 | 0.0 〜 200.0 | `10.0` |
| Attitudeダンピング | railAttitudeSafetyDamping | float | 0.5 | 0.0 〜 200.0 | `5.0` |
| Attitude Torque上限 | railAttitudeSafetyMaxTorque | float | 0.5 | 0.0 〜 200.0 | `8.0` |
| 危険時Forward最小倍率 | railAttitudeSafetyMinForwardScale | float | 0.01 | 0.0 〜 1.0 | `0.1` |
| Catchup倍率 | railPhysicalCatchupSpeedMultiplier | float | 0.01 | 1.0 〜 3.0 | `1.15` |
| 絶対角度制限(Hard Clamp)を使用 | railAttitudeAngleLimitEnabled | bool | — | true / false | `true` |
| Pitch最大角度(度) | railAttitudeAngleLimitMaxPitchDegrees | float | 1.0 | 0.0 〜 90.0 | `15.0` |
| Roll最大角度(度) | railAttitudeAngleLimitMaxRollDegrees | float | 1.0 | 0.0 〜 90.0 | `15.0` |
| 角度ソフト制限を使用 | railAttitudeAngleSoftLimitEnabled | bool | — | true / false | `false` |
| 押し戻し強さ | railAttitudeAngleSoftLimitStrength | float | 0.5 | 0.0 〜 200.0 | `15.0` |
| 押し戻しダンピング | railAttitudeAngleSoftLimitDamping | float | 0.5 | 0.0 〜 200.0 | `6.0` |
| 押し戻しTorque上限 | railAttitudeAngleSoftLimitMaxTorque | float | 0.5 | 0.0 〜 200.0 | `20.0` |
| ループ | railLoop | bool | — | true / false | `false` |
| 進行方向へ回転 | railOrientToPath | bool | — | true / false | `true` |
| 滑らかな曲線 | railUseSmoothCurve | bool | — | true / false | `true` |
| 開始時に停止 | railStartPaused | bool | — | true / false | `false` |
| 逆方向 | railReverse | bool | — | true / false | `false` |
| 終端で停止 | railStopAtEnd | bool | — | true / false | `true` |

#### Health

表示名「体力」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawHealthComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 最大体力 | healthMaximum | float | 1.0 | 0.0 〜 1000000.0 | `100.0` |

#### LegacyRailShooterEnemy

表示名「—」／カテゴリ「追加不可」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### LegacyRailShooterShip

表示名「—」／カテゴリ「追加不可」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### LegacyRailShooterEnemyMotion

表示名「—」／カテゴリ「追加不可」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### LegacyRailShooterStage

表示名「—」／カテゴリ「追加不可」／Inspector描画 `（専用描画なし）`

Inspector編集行なし。

#### SceneButton

表示名「Scene ボタン」／カテゴリ「UI」／Inspector描画 `DrawSceneButtonComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示文字 | buttonLabel | string | — | 文字列 | `"Button"` |
| 位置 | buttonPosition | Vector2 | 0.5 | -10000.0 〜 10000.0 | `{20.0, 20.0}` |
| サイズ | buttonSize | Vector2 | 0.5 | 1.0 〜 4096.0 | `{160.0, 48.0}` |
| 操作可能 | buttonInteractable | bool | — | true / false | `true` |
| 遷移先 Scene | sceneButtonScenePath | string | — | 文字列 | — |

#### Foliage

表示名「フォリッジ」／カテゴリ「地形・タイルマップ」／Inspector描画 `DrawFoliageComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Density Map | assetPath | string | — | 文字列 | `""` |
| 配置範囲 | colliderSize | Vector3 | 1.0 | 1.0 〜 10000.0 | `{1.0, 1.0, 1.0}` |
| 密度 | intensity | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| 最大Instance数 | particleMaxCount | int32 | 1 | — | `256` |
| LOD距離 | colliderRadius | float | 1.0 | 1.0 〜 10000.0 | `0.5` |
| 風向き | oceanPrimaryDirection | Vector2 | 0.01 | -1.0 〜 1.0 | `{1.0, 0.28}` |
| 揺れ幅 | oceanWaveHeight | float | 0.01 | 0.0 〜 5.0 | `1.8` |
| 風速 | oceanWindSpeed | float | 0.1 | 0.0 〜 100.0 | `14.0` |
| 空間周波数 | oceanWaveLength | float | 0.01 | 0.01 〜 100.0 | `28.0` |
| 時間倍率 | oceanTimeScale | float | 0.01 | 0.0 〜 10.0 | `1.0` |

#### WaveSpawner

表示名「ウェーブ生成」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWaveSpawnerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 生成元 | waveSpawnSourceMode | int32(選択) | — | 0=ObjectPool 生成 / 1=事前配置した子（互換） | `0` |
| ObjectPool | wavePoolGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 生成基準位置 | waveSpawnPointGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 生成数(総数) | waveSpawnCount | int32 | 1 | — | `5` |
| 同時存在目標数(0=一括生成) | waveTargetAliveCount | int32 | 1 | — | `0` |
| 補充SpawnPointSet | waveSpawnPointSetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 編隊配置のまま | `-1` |
| 1Frame最大生成数 | waveSpawnMaximumPerFrame | int32 | 1 | — | `8` |
| 編隊 | waveFormationPattern | int32(選択) | — | 0=同一点 / 1=横列 / 2=V字 / 3=円 / 4=グリッド | `1` |
| 編隊間隔 | waveFormationSpacing | float | 0.1 | 0.0 〜 100000.0 | `4.0` |
| レール開始進行率 | waveSpawnRailStartNormalized | float | 0.01 | -1.0 〜 1.0 | `-1.0` |
| グリッド列数 | waveFormationColumns | int32 | 1 | — | `4` |
| 開始時に子を待機 | waveDeactivateChildrenOnStart | bool | — | true / false | `true` |
| 開始条件 | waveTriggerMode | int32(選択) | — | 0=進行率 / 1=外部命令のみ | `0` |
| 進行率 Source | waveTriggerSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 開始進行率 | waveTriggerValue | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 距離 Source | waveTriggerSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 開始距離 | waveTriggerValue | float | 1.0 | 0.0 〜 1000000.0 | `0.0` |
| 生成間隔 | waveSpawnInterval | float | 0.01 | 0.0 〜 3600.0 | `0.0` |
| 完了条件 | waveCompletionMode | int32(選択) | — | 0=全生成 / 1=全撃破・全返却 | `1` |
| Action 対象 | waveActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 開始 Action | waveStartedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaveStarted"` |
| 各生成 Action | waveSpawnedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaveSpawned"` |
| 完了条件 Action | waveCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaveCompleted"` |
| 全撃破 Action | waveAllDefeatedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaveAllDefeated"` |

#### TimelineEvent

表示名「タイムラインイベント」／カテゴリ「入力・イベント」／Inspector描画 `DrawTimelineEventComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 時間 Source | timelineSourceMode | int32(選択) | — | 0=ObjectPool 生成 / 1=事前配置した子（互換） | `0` |
| 進行率 Source | timelineSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 発火進行率 | timelineTriggerValue | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 発火秒 | timelineTriggerValue | float | 0.05 | 0.0 〜 86400.0 | `0.0` |
| Action 対象 | timelineTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action 名 | timelineActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTimelineEvent"` |
| 一度だけ | timelineTriggerOnce | bool | — | true / false | `true` |

#### ThresholdState

表示名「しきい値状態」／カテゴリ「入力・イベント」／Inspector描画 `DrawThresholdStateComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 値 Source | thresholdSourceMode | int32(選択) | — | 0=ObjectPool 生成 / 1=事前配置した子（互換） | `0` |
| Source Object | thresholdSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action 対象 | thresholdTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| State 2 境界 | thresholdSecondValue | float | 0.01 | 0.0 〜 1.0 | `0.66` |
| State 3 境界 | thresholdThirdValue | float | 0.01 | 0.0 〜 1.0 | `0.33` |
| State 1 Action | thresholdFirstActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnState1"` |
| State 2 Action | thresholdSecondActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnState2"` |
| State 3 Action | thresholdThirdActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnState3"` |

#### UIValueBinding

表示名「値バインディング」／カテゴリ「UI」／Inspector描画 `DrawUiValueBindingComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Source Object | uiBindingSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 値 | uiBindingValueType | int32(選択) | — | 0=Float / 1=Vector3 | `1` |
| 接頭文字 | uiBindingPrefix | string | — | 文字列 | — |
| 小数桁 | uiBindingPrecision | int32 | 1 | — | `0` |
| 表示倍率 | uiBindingScale | float | 0.1 | -1000000.0 〜 1000000.0 | `100.0` |

#### Aerodynamics

表示名「空気力学」／カテゴリ「3D物理」／Inspector描画 `DrawAerodynamicsComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 空気密度 kg/m3 | aerodynamicAirDensity | float | 0.001 | 0.0 〜 1000.0 | `1.225` |
| 抗力係数 Cd | aerodynamicDragCoefficient | float | 0.01 | 0.0 〜 10.0 | `0.47` |
| 代表面積 m2 | aerodynamicReferenceArea | float | 0.01 | 0.0001 〜 100000.0 | `1.0` |
| 基礎風速 m/s | aerodynamicAmbientWindVelocity | Vector3 | 0.05 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 基礎揚力係数 | aerodynamicBaseLiftCoefficient | float | 0.01 | -10.0 〜 10.0 | `0.0` |
| 揚力傾斜 /rad | aerodynamicLiftSlope | float | 0.01 | -20.0 〜 20.0 | `0.0` |
| 翼面積 m2 | aerodynamicLiftArea | float | 0.01 | 0.0 〜 100000.0 | `1.0` |
| ゼロ揚力迎角 deg | aerodynamicZeroLiftAngleDegrees | float | 0.1 | -89.0 〜 89.0 | `0.0` |
| 失速迎角 deg | aerodynamicStallAngleDegrees | float | 0.1 | 1.0 〜 89.0 | `20.0` |
| 横力係数 | aerodynamicSideForceCoefficient | float | 0.01 | 0.0 〜 20.0 | `0.0` |
| 側面積 m2 | aerodynamicSideArea | float | 0.01 | 0.0 〜 100000.0 | `1.0` |
| 回転抗力係数 | aerodynamicAngularDragCoefficient | float | 0.01 | 0.0 〜 20.0 | `0.05` |
| Magnus 係数 | aerodynamicMagnusCoefficient | float | 0.01 | -20.0 〜 20.0 | `0.0` |
| 圧力中心 | aerodynamicCenterOfPressure | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 合力上限 N | aerodynamicMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |

#### WindZone

表示名「風ゾーン」／カテゴリ「3D物理」／Inspector描画 `DrawWindZoneComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | windZoneMode | int32(選択) | — | 0=方向風 / 1=放射風 | `0` |
| 風向 | windZoneDirection | Vector3 | 0.01 | -1.0 〜 1.0 | `{1.0, 0.0, 0.0}` |
| 風速 m/s | windZoneSpeed | float | 0.1 | -10000.0 〜 10000.0 | `10.0` |
| 影響半径 m | windZoneRadius | float | 0.1 | 0.0 〜 1000000.0 | `0.0` |
| 乱流速度 m/s | windZoneTurbulenceStrength | float | 0.05 | 0.0 〜 10000.0 | `0.0` |
| 乱流周波数 | windZoneTurbulenceFrequency | float | 0.05 | 0.0 〜 1000.0 | `1.0` |

#### GravityField

表示名「重力場」／カテゴリ「3D物理」／Inspector描画 `DrawGravityFieldComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | gravityFieldMode | int32(選択) | — | 0=Newton 逆二乗 / 1=定加速度 | `0` |
| 引力源質量 kg | gravityFieldSourceMass | float | 1000000.0 | 0.0 〜 1.0e20 | `1.0e11` |
| 加速度 m/s2 | gravityFieldAcceleration | float | 0.01 | -1000000.0 〜 1000000.0 | `9.80665` |
| 最小計算距離 m | gravityFieldMinimumDistance | float | 0.01 | 0.001 〜 1000000.0 | `1.0` |
| 影響半径 m | gravityFieldInfluenceRadius | float | 0.1 | 0.0 〜 1000000000.0 | `0.0` |
| 加速度上限 m/s2 | gravityFieldMaximumAcceleration | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |

#### RotatingFrame

表示名「回転座標系」／カテゴリ「3D物理」／Inspector描画 `DrawRotatingFrameComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 角速度 rad/s | rotatingFrameAngularVelocity | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 1.0, 0.0}` |
| 角加速度 rad/s2 | rotatingFrameAngularAcceleration | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 中心の速度 m/s | rotatingFrameLinearVelocity | Vector3 | 0.05 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 影響半径 m | rotatingFrameRadius | float | 0.1 | 0.0 〜 1000000000.0 | `0.0` |
| 加速度上限 m/s2 | rotatingFrameMaximumAcceleration | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |

#### FluidVolume

表示名「流体ボリューム」／カテゴリ「3D物理」／Inspector描画 `DrawFluidVolumeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| サイズ m | fluidVolumeSize | Vector3 | 0.1 | 0.01 〜 1000000.0 | `{10.0, 5.0, 10.0}` |
| 密度 kg/m3 | fluidDensity | float | 0.1 | 0.0 〜 1000000.0 | `1000.0` |
| 流速 m/s | fluidFlowVelocity | Vector3 | 0.05 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 粘性 Pa*s | fluidDynamicViscosity | float | 0.001 | 0.0 〜 1000000.0 | `0.001` |
| 二次抗力係数 | fluidDragCoefficient | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 角粘性 | fluidAngularViscosity | float | 0.01 | 0.0 〜 1000000.0 | `1.0` |
| 合力上限 N | fluidMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `1000000.0` |

#### SpringForce

表示名「ばね力」／カテゴリ「3D物理」／Inspector描画 `DrawSpringForceComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | springForceTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は World固定点 | `-1` |
| 所有者Anchor | springForceLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 接続先Anchor | springForceTargetLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| World固定点 | springForceWorldAnchor | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{0.0, 0.0, 0.0}` |
| 自然長 m | springForceRestLength | float | 0.01 | 0.0 〜 1000000.0 | `1.0` |
| ばね定数 N/m | springForceStiffness | float | 0.1 | 0.0 〜 1000000000.0 | `50.0` |
| 減衰 Ns/m | springForceDamping | float | 0.1 | 0.0 〜 1000000000.0 | `5.0` |
| Force上限 N | springForceMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 接続先へ反作用 | springForceApplyReaction | bool | — | true / false | `true` |

#### ElectromagneticBody

表示名「電磁気ボディ」／カテゴリ「3D物理」／Inspector描画 `DrawElectromagneticBodyComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 磁気Moment A*m2 | electromagneticMagneticMoment | Vector3 | 0.01 | -1.0e12 〜 1.0e12 | `{0.0, 0.0, 0.0}` |
| Force上限 N | electromagneticMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| Torque上限 N*m | electromagneticMaximumTorque | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |

#### ElectromagneticField

表示名「電磁場」／カテゴリ「3D物理」／Inspector描画 `DrawElectromagneticFieldComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | electromagneticFieldMode | int32(選択) | — | 0=一様場 / 1=点電荷 | `0` |
| 電場 E N/C | electromagneticElectricField | Vector3 | 0.1 | -1.0e12 〜 1.0e12 | `{0.0, 0.0, 0.0}` |
| 最小計算距離 m | electromagneticMinimumDistance | float | 0.01 | 0.001 〜 1000000.0 | `0.1` |
| 磁束密度 B T | electromagneticMagneticField | Vector3 | 0.01 | -1.0e12 〜 1.0e12 | `{0.0, 0.0, 0.0}` |
| 影響半径 m | electromagneticInfluenceRadius | float | 0.1 | 0.0 〜 1000000000.0 | `0.0` |

#### ScreenAim

表示名「画面照準」／カテゴリ「入力・イベント」／Inspector描画 `DrawScreenAimComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 入力方式 | screenAimInputMode | int32(選択) | — | 0=Game Viewマウス / 1=Vector2 Action | `0` |
| 入力Object | screenAimInputGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action Map | screenAimActionMapName | string | — | 文字列 | `"Player"` |
| 移動Action | screenAimActionName | string | — | 文字列 | `"Aim"` |
| 移動速度 | screenAimSpeed | float | 0.01 | 0.0 〜 100.0 | `0.75` |
| Y軸反転 | screenAimInvertY | bool | — | true / false | `false` |
| 照準UI | screenAimReticleGameObjectId | GameObject参照 (int32 ID) | — | 未設定は UIなし | `-1` |
| 初期画面位置 | screenAimNormalizedPosition | Vector2 | 0.01 | 0.0 〜 1.0 | `{0.5, 0.5}` |
| 画面内に制限 | screenAimClamp | bool | — | true / false | `true` |

#### HitscanWeapon

表示名「レイ射撃」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawHitscanWeaponComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 画面照準 | hitscanAimGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 画面中央 | `-1` |
| 入力Object | hitscanInputGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action Map | hitscanActionMapName | string | — | 文字列 | `"Player"` |
| 発射Action | hitscanFireActionName | string | — | 文字列 | `"Fire"` |
| 射程 | hitscanRange | float | 1.0 | 0.01 〜 1000000.0 | `1000.0` |
| ダメージ | hitscanDamage | float | 1.0 | 0.0 〜 1000000.0 | `10.0` |
| Damage Tag | hitscanDamageTag | string | — | 文字列 | `"Bullet"` |
| 発射間隔 | hitscanInterval | float | 0.01 | 0.0 〜 3600.0 | `0.15` |
| 押下中に連射 | hitscanAutomatic | bool | — | true / false | `false` |
| FFT水面へ命中 | hitscanOceanCollision | bool | — | true / false | `true` |
| Action対象 | hitscanActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 発射Action通知 | hitscanFiredActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponFired"` |
| 命中Action通知 | hitscanHitActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponHit"` |
| 非命中Action通知 | hitscanMissActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponMiss"` |

#### ProjectileEmitter

表示名「弾発射」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawProjectileEmitterComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 照準Source | projectileAimMode | int32(選択) | — | 0=画面照準 / 1=Transform前方 / 2=Target / 3=弾道予測 / 4=可変速度 | `0` |
| 照準/Selector | projectileAimGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 画面中央/このObject | `-1` |
| 弾道予測/可変速度Target | projectileBallisticPredictionGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 入力Object | projectileInputGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 弾ObjectPool | projectilePoolGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 発射位置 | projectileSpawnPointGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action Map | projectileActionMapName | string | — | 文字列 | `"Player"` |
| 発射Action | projectileFireActionName | string | — | 文字列 | `"Fire"` |
| 速度 | projectileSpeed | float | 1.0 | 0.0 〜 1000000.0 | `40.0` |
| ダメージ | projectileDamage | float | 1.0 | 0.0 〜 1000000.0 | `10.0` |
| Damage Tag | projectileDamageTag | string | — | 文字列 | `"Projectile"` |
| 判定半径 | projectileRadius | float | 0.01 | 0.0 〜 10000.0 | `0.15` |
| 発射位置の安全距離 | projectileSpawnClearance | float | 0.05 | 0.0 〜 10000.0 | `0.05` |
| 寿命 | projectileLifetime | float | 0.05 | 0.01 〜 3600.0 | `5.0` |
| 発射間隔 | projectileInterval | float | 0.01 | 0.0 〜 3600.0 | `0.25` |
| 押下中に連射 | projectileAutomatic | bool | — | true / false | `false` |
| FFT水面へ命中 | projectileOceanCollision | bool | — | true / false | `true` |
| 発射元速度を継承 | projectileInheritSourceVelocity | bool | — | true / false | `true` |
| 速度Source | projectileSourceVelocityGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 親Rigidbodyを検索 | projectileUseParentRigidBody | bool | — | true / false | `true` |
| 並進速度継承 | projectileLinearVelocityInheritance | float | 0.01 | -10.0 〜 10.0 | `1.0` |
| 角速度継承 | projectileAngularVelocityInheritance | float | 0.01 | -10.0 〜 10.0 | `1.0` |
| 可変速度: 時間の決め方 | projectileVariableSpeedTimeMode | int32(選択) | — | 0=距離に応じる / 1=固定時間 | `0` |
| 可変速度: 最短飛行時間(距離依存時) | projectileVariableSpeedMinimumFlightTime | float | 0.01 | 0.01 〜 3600.0 | `0.2` |
| 可変速度: 最長飛行時間(距離依存時) | projectileVariableSpeedMaximumFlightTime | float | 0.01 | 0.01 〜 3600.0 | `1.2` |
| 可変速度: 距離÷この値=飛行時間(距離依存時) | projectileVariableSpeedDistanceFactor | float | 1.0 | 1.0 〜 100000.0 | `300.0` |
| 可変速度: 固定飛行時間 | projectileVariableSpeedFixedFlightTime | float | 0.01 | 0.01 〜 3600.0 | `1.5` |
| 可変速度: 弾道方式 | projectileVariableSpeedTrajectoryMode | int32(選択) | — | 0=物理(初速+重力) / 1=俯角固定の直線 / 2=位置補間(物理無視) | `0` |
| 可変速度: 俯角(度) | projectileVariableSpeedDepressionAngleDegrees | float | 0.1 | -89.0 〜 89.0 | `15.0` |
| 可変速度: 弧の高さ | projectileVariableSpeedArcHeight | float | 0.1 | -10000.0 〜 10000.0 | `5.0` |
| 曳光弾ストレッチ表示 | projectileTracerStretchEnabled | bool | — | true / false | `false` |
| 曳光弾: 長さ倍率 | projectileTracerLengthScale | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 曳光弾: 最小長さ | projectileTracerMinimumLength | float | 0.1 | 0.0 〜 10000.0 | `2.0` |
| 曳光弾: 太さ | projectileTracerThickness | float | 0.01 | 0.001 〜 100.0 | `0.15` |
| Hitscanで即ダメージ解決(この弾は演出専用) | projectileHitscanResolution | bool | — | true / false | `false` |
| Action対象 | projectileActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 発射Action通知 | projectileFiredActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnProjectileFired"` |
| 命中Action通知 | projectileHitActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnProjectileHit"` |

#### DamageReceiver

表示名「ダメージ受信」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawDamageReceiverComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ダメージ倍率 | damageMultiplier | float | 0.01 | 0.0 〜 10000.0 | `1.0` |
| 無敵時間 | damageInvulnerabilitySeconds | float | 0.01 | 0.0 〜 3600.0 | `0.0` |
| 死亡時に無効化 | damageDeactivateOnDeath | bool | — | true / false | `true` |
| Action対象 | damageActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 被弾Action | damagedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnDamaged"` |
| 死亡Action | deathActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnDeath"` |

#### ObjectPool

表示名「オブジェクトプール」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawObjectPoolComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Template | objectPoolTemplateGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 遅延生成容量 | objectPoolInitialSize | int32 | 1 | — | `16` |
| 容量不足時に拡張 | objectPoolAllowExpand | bool | — | true / false | `false` |

#### PrefabSpawner

表示名「プレハブ生成」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawPrefabSpawnerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ObjectPool | prefabSpawnerPoolGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 生成位置 | prefabSpawnerPointGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 生成方式 | prefabSpawnerMode | int32(選択) | — | 0=外部命令のみ / 1=Play開始時 / 2=一定間隔 | `0` |
| 生成間隔 | prefabSpawnerInterval | float | 0.01 | 0.01 〜 3600.0 | `1.0` |
| Action対象 | prefabSpawnerActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 生成Action | prefabSpawnerSpawnedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnSpawned"` |

#### CameraBlend

表示名「カメラブレンド」／カテゴリ「カメラ」／Inspector描画 `DrawCameraBlendComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 開始Camera | cameraBlendSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 現在Camera | `-1` |
| 終了Camera | cameraBlendTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 時間 | cameraBlendDuration | float | 0.05 | 0.0 〜 3600.0 | `1.0` |
| 補間 | cameraBlendEasing | int32(選択) | — | 0=Linear / 1=SmoothStep | `1` |
| Play開始時に再生 | cameraBlendPlayOnStart | bool | — | true / false | `false` |

#### CameraShake

表示名「カメラシェイク」／カテゴリ「カメラ」／Inspector描画 `DrawCameraShakeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 位置振幅 | cameraShakePositionAmplitude | Vector3 | 0.01 | 0.0 〜 10000.0 | `{0.1, 0.1, 0.1}` |
| 回転振幅 | cameraShakeRotationAmplitude | Vector3 | 0.001 | 0.0 〜 6.2832 | `{0.01, 0.01, 0.01}` |
| 周波数 | cameraShakeFrequency | float | 0.1 | 0.0 〜 1000.0 | `12.0` |
| 時間 | cameraShakeDuration | float | 0.05 | 0.0 〜 3600.0 | `0.35` |
| Priority | cameraShakePriority | int32 | 1 | — | `0` |
| Play開始時に再生 | cameraShakePlayOnStart | bool | — | true / false | `false` |

#### RailBranch

表示名「レール分岐」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRailBranchComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| RailFollower | railBranchFollowerGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 切替先Rail Path | railBranchTargetPathGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 切替条件 | railBranchTriggerMode | int32(選択) | — | 0=進行率 / 1=外部命令のみ | `0` |
| 切替進行率 | railBranchTriggerNormalized | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 進行率を維持 | railBranchPreserveProgress | bool | — | true / false | `false` |
| 一度だけ | railBranchTriggerOnce | bool | — | true / false | `true` |
| Action対象 | railBranchActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 切替Action | railBranchActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnRailBranched"` |

#### ActionSequence

表示名「アクションシーケンス」／カテゴリ「入力・イベント」／Inspector描画 `DrawActionSequenceComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Play開始時に再生 | actionSequencePlayOnStart | bool | — | true / false | `false` |
| ループ | actionSequenceLoop | bool | — | true / false | `false` |

#### ActionSequenceStep

表示名「シーケンスステップ」／カテゴリ「入力・イベント」／Inspector描画 `DrawActionSequenceStepComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Step種類 | actionSequenceStepType | int32(選択) | — | 0=Script Action / 1=待機 / 2=Active変更 / 3=Scene読込 / 4=条件分岐 / 5=Signal待機 | `0` |
| 並列Group (-1=順次) | actionSequenceParallelGroup | int32 | 1 | — | `-1` |
| Action対象 | actionSequenceTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 親Sequence | `-1` |
| Action | actionSequenceActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnSequenceAction"` |
| 待機秒 | actionSequenceWaitSeconds | float | 0.01 | 0.0 〜 86400.0 | `1.0` |
| 対象 | actionSequenceTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 親Sequence | `-1` |
| Active | actionSequenceActiveValue | bool | — | true / false | `true` |
| Scene Asset | actionSequenceScenePath | string | — | 文字列 | `""` |
| Additive読込 | actionSequenceSceneAdditive | bool | — | true / false | `false` |
| 条件対象 | actionSequenceTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 親Sequence | `-1` |
| 条件値 | actionSequenceConditionMode | int32(選択) | — | 0=Active / 1=体力比率 / 2=Rail進行率 | `0` |
| 比較 | actionSequenceCompareMode | int32(選択) | — | 0=>= / 1=<= / 2=> / 3=< | `0` |
| 比較値 | actionSequenceCompareValue | float | 0.01 | -1000000.0 〜 1000000.0 | `1.0` |
| true移動先Index | actionSequenceTrueStepIndex | int32 | 1 | — | `-1` |
| false移動先Index | actionSequenceFalseStepIndex | int32 | 1 | — | `-1` |
| Signal名 | actionSequenceActionName | string | — | 文字列 | `"OnSequenceAction"` |

#### Saveable

表示名「保存対象」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawSaveableComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 保存Key (空=Object名) | saveableKey | string | — | 文字列 | `""` |
| Transform | saveableTransform | bool | — | true / false | `true` |
| Active | saveableActive | bool | — | true / false | `true` |
| Health | saveableHealth | bool | — | true / false | `true` |
| Rigidbody | saveableRigidbody | bool | — | true / false | `true` |
| C++ Script公開値 | saveableScriptProperties | bool | — | true / false | `true` |

#### Checkpoint

表示名「チェックポイント」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawCheckpointComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Slot名 | checkpointSlotName | string | — | 文字列 | `"autosave"` |
| Play開始時に保存 | checkpointSaveOnStart | bool | — | true / false | `false` |
| Play開始時に読込 | checkpointLoadOnStart | bool | — | true / false | `false` |
| 完了Action対象 | checkpointActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 保存完了Action | checkpointSavedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnCheckpointSaved"` |
| 読込完了Action | checkpointLoadedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnCheckpointLoaded"` |

#### RopeConstraint

表示名「ロープ拘束」／カテゴリ「3D物理」／Inspector描画 `DrawRopeConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接続先 | ropeTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は World固定点 | `-1` |
| 所有者Anchor | ropeLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 接続先Anchor | ropeTargetLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| World固定点 | ropeWorldAnchor | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{0.0, 0.0, 0.0}` |
| 最大長 m | ropeMaximumLength | float | 0.01 | 0.0 〜 1000000.0 | `5.0` |
| 張力係数 N/m | ropeStiffness | float | 1.0 | 0.0 〜 1000000000.0 | `2000.0` |
| 減衰 Ns/m | ropeDamping | float | 0.1 | 0.0 〜 1000000000.0 | `80.0` |
| 張力上限 N | ropeMaximumTension | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 破断張力 N | ropeBreakingTension | float | 10.0 | 0.0 〜 1000000000.0 | `0.0` |
| 接続先へ反作用 | ropeApplyReaction | bool | — | true / false | `true` |

#### TorsionSpring

表示名「ねじりばね」／カテゴリ「3D物理」／Inspector描画 `DrawTorsionSpringComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 基準Object | torsionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は World回転 | `-1` |
| ばね定数 N*m/rad | torsionStiffness | float | 0.1 | 0.0 〜 1000000000.0 | `50.0` |
| 減衰 N*m*s/rad | torsionDamping | float | 0.1 | 0.0 〜 1000000000.0 | `8.0` |
| Torque上限 N*m | torsionMaximumTorque | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 接続先へ反作用 | torsionApplyReaction | bool | — | true / false | `true` |

#### WeaponLoadout

表示名「武器ロードアウト」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponLoadoutComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 選択Slot | weaponLoadoutSelectedSlotIndex | int32 | 1 | — | `0` |
| Action対象 | weaponLoadoutActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 装備変更Action | weaponLoadoutChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponChanged"` |
| Reload完了Action | weaponLoadoutReloadedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponReloaded"` |

#### WeaponLoadoutSlot

表示名「武器スロット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponLoadoutSlotComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Slot名 | weaponSlotName | string | — | 文字列 | `"Weapon"` |
| Weapon Object | weaponSlotWeaponGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| Visual Object | weaponSlotVisualGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| 現在弾数 | weaponSlotCurrentAmmo | int32 | 1 | — | `30` |
| 予備弾 (-1=無限) | weaponSlotReserveAmmo | int32 | 1 | — | `90` |
| 最大弾数 | weaponSlotMaximumAmmo | int32 | 1 | — | `30` |
| Reload秒 | weaponSlotReloadSeconds | float | 0.01 | 0.0 〜 3600.0 | `1.5` |
| 空で自動Reload | weaponSlotAutoReload | bool | — | true / false | `true` |

#### TargetSelector

表示名「ターゲット選択」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTargetSelectorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 検索Layer (-1=全て) | targetSelectorSearchLayer | int32 | 1 | — | `-1` |
| 最大距離 | targetSelectorMaximumDistance | float | 0.1 | 0.0 〜 1000000.0 | `100.0` |
| 最大角度 | targetSelectorMaximumAngle | float | 0.1 | 0.0 〜 180.0 | `45.0` |
| 基準Object | targetSelectorReferenceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 遮蔽判定 | targetSelectorOcclusionCheck | bool | — | true / false | `true` |
| 遮蔽方式 | targetSelectorOcclusionMode | int32(選択) | — | 0=なし / 1=Physics / 2=Ocean / 3=Physics + Ocean | `3` |
| Ocean Clearance | targetSelectorOceanClearance | float | 0.01 | -1000.0 〜 1000.0 | `0.0` |
| 最大候補数 | targetSelectorMaximumTargets | int32 | 1 | — | `16` |
| 選択方式 | targetSelectorSelectionMode | int32(選択) | — | 0=最短距離 / 1=照準中心 / 2=低HP / 3=優先値 | `1` |
| Team Filter | targetSelectorTeamFilter | int32(選択) | — | 0=Target可能な全て / 1=別Team / 2=同じTeam / 3=指定Team | `0` |
| 指定Team ID | targetSelectorSpecificTeamId | int32 | 1 | — | `0` |
| Neutralを含む | targetSelectorIncludeNeutral | bool | — | true / false | `true` |
| 現在Target ID | targetSelectorCurrentTargetGameObjectId | int32 | 1 | — | `-1` |
| Action対象 | targetSelectorActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 取得Action | targetSelectorFoundActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTargetFound"` |
| 喪失Action | targetSelectorLostActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTargetLost"` |
| 変更Action | targetSelectorChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTargetChanged"` |

#### TargetSteering

表示名「ターゲット追従」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTargetSteeringComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | targetSteeringTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Selectorを使用 | `-1` |
| TargetSelector | targetSteeringSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 旋回速度 deg/s | targetSteeringTurnSpeed | float | 1.0 | 0.0 〜 100000.0 | `180.0` |
| 加速度 | targetSteeringAcceleration | float | 0.1 | 0.0 〜 100000.0 | `20.0` |
| 最大速度 | targetSteeringMaximumSpeed | float | 0.1 | 0.0 〜 100000.0 | `40.0` |
| 開始Delay | targetSteeringStartDelay | float | 0.01 | 0.0 〜 3600.0 | `0.0` |
| 予測秒 | targetSteeringPredictionSeconds | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| 移動方式 | targetSteeringMode | int32(選択) | — | 0=Transform / 1=Rigidbody Force | `0` |
| 移動Mode | targetSteeringMoveMode | int32(選択) | — | 0=Direct (直進追尾) / 1=ArcApproach (旋回接近) / 2=Parallel (並走) / 3=Chase (後方追跡) / 4=KeepDistance (距離維持) / 5=PlayerRelativeMove (相対移動) / 6=Retreat (離脱) | `0` |
| 横Offset (右+/左-) | targetSteeringSideOffset | float | 0.1 | -100000.0 〜 100000.0 | `25.0` |
| 前後Offset | targetSteeringForwardOffset | float | 0.1 | -100000.0 〜 100000.0 | `0.0` |
| 高さOffset | targetSteeringVerticalOffset | float | 0.1 | -100000.0 〜 100000.0 | `0.0` |
| 目標距離 | targetSteeringTargetDistance | float | 0.1 | 0.0 〜 100000.0 | `120.0` |
| 距離Margin | targetSteeringDistanceMargin | float | 0.1 | 0.0 〜 100000.0 | `15.0` |
| 開始Offset | targetSteeringStartOffset | Vector3 | 0.1 | -100000.0 〜 100000.0 | `{80.0, 0.0, 50.0}` |
| 終了Offset | targetSteeringEndOffset | Vector3 | 0.1 | -100000.0 〜 100000.0 | `{-80.0, 0.0, 20.0}` |
| 相対位置追従速度 (0=最大速度) | targetSteeringPositionLerpSpeed | float | 0.1 | 0.0 〜 100000.0 | `0.0` |
| 継続秒 (0=無期限) | targetSteeringDuration | float | 0.01 | 0.0 〜 3600.0 | `0.0` |
| 完了後のMode | nextMoveModeIndex | int32(選択) | — | 0=維持 / 1=Direct (直進追尾) / 2=ArcApproach (旋回接近) / 3=Parallel (並走) / 4=Chase (後方追跡) / 5=KeepDistance (距離維持) / 6=PlayerRelativeMove (相対移動) / 7=Retreat (離脱) | — |
| 完了Action対象 | targetSteeringActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | targetSteeringCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `""` |

#### MovementModifier

表示名「移動補正」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawMovementModifierComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 位置Offset | movementModifierLocalPositionOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 回転Offset deg | movementModifierLocalRotationOffset | Vector3 | 0.1 | -36000.0 〜 36000.0 | `{0.0, 0.0, 0.0}` |
| 位置X | allowsX | bool | — | true / false | — |
| 位置Y | allowsY | bool | — | true / false | — |
| 位置Z | allowsZ | bool | — | true / false | — |
| 入力範囲 | movementModifierInputRange | Vector2 | 0.1 | 0.0 〜 100000.0 | `{0.0, 0.0}` |
| 入力追従速度 | movementModifierInputSpeed | float | 0.1 | 0.0 〜 100000.0 | `8.0` |
| PlayerInput | movementModifierInputGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action Map | movementModifierActionMapName | string | — | 文字列 | `"Player"` |
| Vector2 Action | movementModifierActionName | string | — | 文字列 | `"Move"` |

#### PropertyTween

表示名「プロパティ補間」／カテゴリ「入力・イベント」／Inspector描画 `DrawPropertyTweenComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 対象 | propertyTweenTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Component | propertyTweenComponentName | string | — | 文字列 | `"Ocean"` |
| Property | propertyTweenPropertyName | string | — | 文字列 | `"WaveHeight"` |
| 値型 | propertyTweenValueType | int32(選択) | — | 0=Float / 1=Vector3 | `0` |
| 開始値 | propertyTweenStartValue | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{1.0, 0.0, 0.0}` |
| 終了値 | propertyTweenEndValue | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{4.0, 0.0, 0.0}` |
| 時間 | propertyTweenDuration | float | 0.01 | 0.001 〜 86400.0 | `1.0` |
| Curve | propertyTweenCurve | int32(選択) | — | 0=Linear / 1=SmoothStep / 2=Ease In / 3=Ease Out | `1` |
| Play開始時に再生 | propertyTweenPlayOnStart | bool | — | true / false | `false` |
| ループ | propertyTweenLoop | bool | — | true / false | `false` |
| 完了Action対象 | propertyTweenActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | propertyTweenCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTweenCompleted"` |

#### ActionRelay

表示名「アクション中継」／カテゴリ「入力・イベント」／Inspector描画 `DrawActionRelayComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Play開始時にRelay | actionRelayOnStart | bool | — | true / false | `false` |

#### ActionRelayTarget

表示名「中継先」／カテゴリ「入力・イベント」／Inspector描画 `DrawActionRelayTargetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 有効 | actionRelayTargetEnabled | bool | — | true / false | `true` |
| Action対象 | actionRelayTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 親Relay | `-1` |
| Action | actionRelayActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnAction"` |

#### Thruster

表示名「推進力」／カテゴリ「3D物理」／Inspector描画 `DrawThrusterComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 推進方向 | thrusterDirection | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 0.0, 1.0}` |
| ローカル作用点 | thrusterLocalApplicationPoint | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 推進力 N | thrusterForce | float | 1.0 | -1000000000.0 〜 1000000000.0 | `100.0` |
| スロットル | thrusterThrottle | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| ローカル方向を使用 | thrusterUseLocalDirection | bool | — | true / false | `true` |

#### PulleyConstraint

表示名「滑車拘束」／カテゴリ「3D物理」／Inspector描画 `DrawPulleyConstraintComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 反対側Object | pulleyTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 所有者Anchor | pulleyOwnerLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 反対側Anchor | pulleyTargetLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 所有者側支持点 | pulleyOwnerWorldSupport | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{-1.0, 3.0, 0.0}` |
| 反対側支持点 | pulleyTargetWorldSupport | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{1.0, 3.0, 0.0}` |
| 全長 m | pulleyTotalLength | float | 0.01 | 0.0 〜 1000000.0 | `6.0` |
| 滑車比 | pulleyRatio | float | 0.01 | 0.0001 〜 10000.0 | `1.0` |
| 張力係数 N/m | pulleyStiffness | float | 1.0 | 0.0 〜 1000000000.0 | `3000.0` |
| 減衰 Ns/m | pulleyDamping | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |
| 張力上限 N | pulleyMaximumTension | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 破断張力 N | pulleyBreakingTension | float | 10.0 | 0.0 〜 1000000000.0 | `0.0` |

#### PhysicsServo

表示名「物理サーボ」／カテゴリ「3D物理」／Inspector描画 `DrawPhysicsServoComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 追従先 | servoTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は World目標 | `-1` |
| component.servoTargetGameObjectId >= 0 ? "位置オフセット" : "目標World位置" | servoTargetPosition | Vector3 | 0.01 | -1000000.0 〜 1000000.0 | `{0.0, 0.0, 0.0}` |
| 位置ばね N/m | servoPositionStiffness | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |
| 速度減衰 Ns/m | servoPositionDamping | float | 0.1 | 0.0 〜 1000000000.0 | `20.0` |
| Force上限 N | servoMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 回転ばね N*m/rad | servoRotationStiffness | float | 0.1 | 0.0 〜 1000000000.0 | `50.0` |
| 角速度減衰 | servoRotationDamping | float | 0.1 | 0.0 〜 1000000000.0 | `8.0` |
| Torque上限 N*m | servoMaximumTorque | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 追従先へ反作用 | servoApplyReaction | bool | — | true / false | `false` |

#### VortexField

表示名「渦流場」／カテゴリ「3D物理」／Inspector描画 `DrawVortexFieldComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ローカル渦軸 | vortexAxis | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 1.0, 0.0}` |
| 影響半径 m | vortexRadius | float | 0.1 | 0.0 〜 1000000.0 | `10.0` |
| 角速度 rad/s | vortexAngularVelocity | float | 0.01 | -10000.0 〜 10000.0 | `1.0` |
| 中心流入速度 m/s | vortexRadialInflowVelocity | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 軸方向速度 m/s | vortexAxialVelocity | float | 0.01 | -10000.0 〜 10000.0 | `0.0` |
| 速度結合率 1/s | vortexVelocityCoupling | float | 0.01 | 0.0 〜 10000.0 | `2.0` |
| 加速度上限 m/s2 | vortexMaximumAcceleration | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |

#### PressureField

表示名「圧力場」／カテゴリ「3D物理」／Inspector描画 `DrawPressureFieldComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 圧力 Pa | pressureFieldPressure | float | 1.0 | -1000000000.0 〜 1000000000.0 | `1000.0` |
| 影響半径 m | pressureFieldRadius | float | 0.1 | 0.0 〜 1000000.0 | `10.0` |
| 減衰指数 | pressureFieldFalloffExponent | float | 0.01 | 0.0 〜 32.0 | `2.0` |
| Force上限 N | pressureFieldMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `1000000.0` |

#### Suspension

表示名「サスペンション」／カテゴリ「3D物理」／Inspector描画 `DrawSuspensionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ローカル取付点 | suspensionLocalAnchor | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, -0.5, 0.0}` |
| ローカル接地方向 | suspensionLocalDirection | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, -1.0, 0.0}` |
| 自然長 m | suspensionRestLength | float | 0.01 | 0.0 〜 1000000.0 | `0.8` |
| 最大伸長 m | suspensionMaximumLength | float | 0.01 | 0.0 〜 1000000.0 | `1.2` |
| 車輪半径 m | suspensionWheelRadius | float | 0.01 | 0.0 〜 1000000.0 | `0.3` |
| ばね定数 N/m | suspensionStiffness | float | 1.0 | 0.0 〜 1000000000.0 | `20000.0` |
| 減衰 Ns/m | suspensionDamping | float | 1.0 | 0.0 〜 1000000000.0 | `2500.0` |
| Force上限 N | suspensionMaximumForce | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |
| 接地法線へForce | suspensionUseHitNormal | bool | — | true / false | `false` |
| 接地物へ反作用 | suspensionApplyReaction | bool | — | true / false | `true` |

#### UprightStabilizer

表示名「姿勢安定化」／カテゴリ「3D物理」／Inspector描画 `DrawUprightStabilizerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ローカル上方向 | uprightLocalUpAxis | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 1.0, 0.0}` |
| 目標World上方向 | uprightTargetWorldUp | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 1.0, 0.0}` |
| 姿勢ばね N*m/rad | uprightStiffness | float | 0.1 | 0.0 〜 1000000000.0 | `100.0` |
| 角速度減衰 | uprightDamping | float | 0.1 | 0.0 〜 1000000000.0 | `15.0` |
| Torque上限 N*m | uprightMaximumTorque | float | 10.0 | 0.0 〜 1000000000.0 | `100000.0` |

#### TargetPoint

表示名「ターゲットポイント」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTargetPointComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 優先値 | targetPointPriority | float | 0.1 | -100000.0 〜 100000.0 | `0.0` |
| 注視半径 | targetPointRadius | float | 0.01 | 0.01 〜 100000.0 | `0.25` |
| 注視Offset | targetPointAimOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |

#### Team

表示名「チーム」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTeamComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Team ID | teamId | int32 | 1 | — | `-1` |
| Target可能 | teamTargetable | bool | — | true / false | `true` |

#### Timer

表示名「タイマー」／カテゴリ「入力・イベント」／Inspector描画 `DrawTimerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 時間 | timerDuration | float | 0.01 | 0.001 〜 86400.0 | `1.0` |
| 繰り返す | timerRepeat | bool | — | true / false | `false` |
| Play開始時に再生 | timerPlayOnStart | bool | — | true / false | `true` |
| 一時停止 | timerPaused | bool | — | true / false | `false` |
| Action対象 | timerActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 発火Action | timerActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTimer"` |

#### GenericStateMachine

表示名「汎用ステートマシン」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawGenericStateMachineComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 初期State | stateMachineInitialState | string | — | 文字列 | `"Initial"` |
| Action対象 | stateMachineActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 変更Action | stateMachineChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnStateChanged"` |

#### Attribute

表示名「属性・リソース」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawAttributeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 属性名 | attributeName | string | — | 文字列 | `"Resource"` |
| 最小 | attributeMinimum | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| 最大 | attributeMaximum | float | 0.1 | -1000000.0 〜 1000000.0 | `100.0` |
| 現在 | attributeCurrent | float | 0.1 | -1000000.0 〜 1000000.0 | `100.0` |
| 毎秒回復 | attributeRegenerationPerSecond | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| Action対象 | attributeActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 変更Action | attributeChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnAttributeChanged"` |

#### DestructiblePart

表示名「破壊可能部位」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawDestructiblePartComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Health Source | destructibleHealthGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 無効化Component (;区切り) | destructibleDisableComponentNames | string | — | 文字列 | — |
| 子Objectを無効化 | destructibleDisableChildren | bool | — | true / false | `true` |
| Action対象 | destructibleActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 破壊Action | destructibleDestroyedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnPartDestroyed"` |

#### FormationFollower

表示名「編隊追従」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawFormationFollowerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Leader | formationLeaderGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| ローカルOffset | formationLocalOffset | Vector3 | 0.1 | -100000.0 〜 100000.0 | `{}` |
| 位置追従速度 | formationPositionSpeed | float | 0.1 | 0.0 〜 100000.0 | `8.0` |
| 回転追従速度 deg/s | formationRotationSpeed | float | 1.0 | 0.0 〜 100000.0 | `180.0` |
| 回転を追従 | formationFollowRotation | bool | — | true / false | `true` |

#### TargetLock

表示名「ターゲットロック」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTargetLockComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| TargetSelector | targetLockSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Lock時間 | targetLockSeconds | float | 0.01 | 0.0 〜 3600.0 | `0.75` |
| 喪失猶予 | targetLockLostGraceSeconds | float | 0.01 | 0.0 〜 3600.0 | `0.25` |
| Action対象 | targetLockActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 開始Action | targetLockStartedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnLockStarted"` |
| 完了Action | targetLockCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnLockCompleted"` |
| 解除Action | targetLockLostActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnLockLost"` |

#### MultiTargetLock

表示名「複数ターゲットロック」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawMultiTargetLockComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| TargetSelector | multiTargetLockSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 最大Lock数 | multiTargetLockMaximumCount | int32 | 1 | — | `8` |
| 1体のLock時間 | multiTargetLockSecondsPerTarget | float | 0.01 | 0.0 〜 3600.0 | `0.35` |
| 喪失猶予 | multiTargetLockLostGraceSeconds | float | 0.01 | 0.0 〜 3600.0 | `0.25` |
| 候補を自動取得 | multiTargetLockAutoAcquire | bool | — | true / false | `true` |
| Action対象 | multiTargetLockActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 追加Action | multiTargetLockAddedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnMultiTargetAdded"` |
| Lock完了Action | multiTargetLockCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnMultiTargetLocked"` |
| 解除Action | multiTargetLockLostActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnMultiTargetLost"` |

#### WorldTargetMarker

表示名「ワールドターゲットマーカー」／カテゴリ「UI」／Inspector描画 `DrawTargetMarkerComponent`／同じ描画関数を共有: OffScreenIndicator

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | targetMarkerTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動参照 | `-1` |
| TargetSelector | targetMarkerSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未使用 | `-1` |
| TargetLock | targetMarkerLockGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未使用 | `-1` |
| Multi Lock番号 | targetMarkerMultiLockIndex | int32 | 1 | — | `0` |
| World Offset | targetMarkerWorldOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| Screen Offset | targetMarkerScreenOffset | Vector2 | 1.0 | -100000.0 〜 100000.0 | `{0.0, 0.0}` |
| 画面端余白 | targetMarkerEdgePadding | float | 1.0 | 0.0 〜 1000.0 | `32.0` |
| カメラ後方を隠す | targetMarkerHideBehindCamera | bool | — | true / false | `true` |
| Lock完了時だけ表示 | targetMarkerOnlyWhenLocked | bool | — | true / false | `false` |
| Target方向へ回転 | targetMarkerRotateToDirection | bool | — | true / false | `true` |

#### OffScreenIndicator

表示名「画面外インジケーター」／カテゴリ「UI」／Inspector描画 `DrawTargetMarkerComponent`／同じ描画関数を共有: WorldTargetMarker

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | targetMarkerTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動参照 | `-1` |
| TargetSelector | targetMarkerSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未使用 | `-1` |
| TargetLock | targetMarkerLockGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未使用 | `-1` |
| Multi Lock番号 | targetMarkerMultiLockIndex | int32 | 1 | — | `0` |
| World Offset | targetMarkerWorldOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| Screen Offset | targetMarkerScreenOffset | Vector2 | 1.0 | -100000.0 〜 100000.0 | `{0.0, 0.0}` |
| 画面端余白 | targetMarkerEdgePadding | float | 1.0 | 0.0 〜 1000.0 | `32.0` |
| カメラ後方を隠す | targetMarkerHideBehindCamera | bool | — | true / false | `true` |
| Lock完了時だけ表示 | targetMarkerOnlyWhenLocked | bool | — | true / false | `false` |
| Target方向へ回転 | targetMarkerRotateToDirection | bool | — | true / false | `true` |

#### AttributeSet

表示名「属性セット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawAttributeSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 名前 | [要素].name | string | — | 文字列 | — |
| 最小 | [要素].minimum | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| 最大 | [要素].maximum | float | 0.1 | entry.minimum 〜 1000000.0 | `100.0` |
| 現在 | [要素].current | float | 0.1 | entry.minimum 〜 entry.maximum | `100.0` |
| 毎秒回復 | [要素].regenerationPerSecond | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| Action対象 | attributeSetActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 変更Action | attributeSetChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnAttributeSetChanged"` |

#### GenericCounter

表示名「汎用カウンター」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawGenericCounterComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 名前 | counterName | string | — | 文字列 | `"Counter"` |
| 初期値 | counterInitialValue | float | 1.0 | -1000000.0 〜 1000000.0 | `0.0` |
| 最小 | counterMinimumValue | float | 1.0 | -1000000.0 〜 1000000.0 | `0.0` |
| 最大 | counterMaximumValue | float | 1.0 | counterMinimumValue 〜 1000000.0 | `999999.0` |
| 閾値 | counterThresholdValue | float | 1.0 | -1000000.0 〜 1000000.0 | `1.0` |
| 比較 | counterCompareMode | int32(選択) | — | 0=>= / 1=<= / 2=> / 3=< | `0` |
| 成立時は1回だけ | counterFireOnce | bool | — | true / false | `true` |
| Action対象 | counterActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 変更Action | counterChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnCounterChanged"` |
| 閾値Action | counterThresholdActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnCounterThreshold"` |

#### GenericCondition

表示名「汎用条件」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawGenericConditionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 比較元 | conditionSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 比較元種類 | conditionSourceType | int32(選択) | — | 0=Runtime Float / 1=Runtime Int / 2=Runtime Bool / 3=AttributeSet / 4=Counter / 5=Object Active / 6=Generic State / 7=Target Locked | `0` |
| Component | conditionComponentName | string | — | 文字列 | — |
| Property / 属性名 | conditionPropertyName | string | — | 文字列 | — |
| 比較 | conditionCompareMode | int32(選択) | — | 0=>= / 1=<= / 2=> / 3=< | `0` |
| 比較State | conditionCompareString | string | — | 文字列 | — |
| 比較値 | conditionCompareFloat | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| 毎Frame評価 | conditionEvaluateEveryFrame | bool | — | true / false | `false` |
| 結果変化時だけ通知 | conditionFireOnChangeOnly | bool | — | true / false | `true` |
| Action対象 | conditionActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 成立Action | conditionTrueActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnConditionTrue"` |
| 不成立Action | conditionFalseActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnConditionFalse"` |

#### GameplayData

表示名「ゲームプレイデータ」／カテゴリ「データ」／Inspector描画 `DrawGameplayDataComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Data Asset | gameplayDataAssetPath | string | — | 文字列 | — |
| Key | [要素].key | string | — | 文字列 | — |
| 型 | [要素].type | int32(選択) | — | 0=String / 1=Int / 2=Float / 3=Bool / 4=Asset Path | `0` |
| 値 | [要素].value | string | — | 文字列 | — |

#### AreaDamage

表示名「範囲ダメージ」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawAreaDamageComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 半径 | areaDamageRadius | float | 0.1 | 0.01 〜 100000.0 | `5.0` |
| 基礎Damage | areaDamageBaseDamage | float | 1.0 | 0.0 〜 1000000.0 | `50.0` |
| 端の最低倍率 | areaDamageMinimumMultiplier | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| Impulse | areaDamageImpulse | float | 1.0 | -1000000.0 〜 1000000.0 | `0.0` |
| 距離減衰 | areaDamageFalloffMode | int32(選択) | — | 0=一定 / 1=線形 / 2=SmoothStep | `1` |
| Layer Mask | areaDamageLayerMask | int32 | 1 | — | `-1` |
| Damage Tag | areaDamageTag | string | — | 文字列 | `"Explosion"` |
| 発生元を除外 | areaDamageIgnoreOwner | bool | — | true / false | `true` |
| 遮蔽判定 | areaDamageOcclusionMode | int32(選択) | — | 0=なし / 1=Physics / 2=Ocean / 3=Physics + Ocean | `0` |
| 遮蔽Layer Mask | areaDamageOcclusionLayerMask | int32 | 1 | — | `-1` |
| 遮蔽時倍率 | areaDamageBlockedMultiplier | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 遮蔽Sample数 | areaDamageOcclusionSamplePoints | int32 | 1 | — | `1` |
| Teamルール | areaDamageTeamRule | int32(選択) | — | 0=すべて / 1=異なるTeamのみ / 2=同じTeamのみ | `0` |
| Neutralを無視 | areaDamageIgnoreNeutral | bool | — | true / false | `false` |
| Team Source | areaDamageTeamSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Instigator | `-1` |
| Play開始時に実行 | areaDamagePlayOnStart | bool | — | true / false | `false` |
| Action対象 | areaDamageActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 適用Action | areaDamageAppliedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnAreaDamageApplied"` |

#### HitZone

表示名「ヒットゾーン」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawHitZoneComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Health対象 | hitZoneHealthGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 部位Damage倍率 | hitZoneDamageMultiplier | float | 0.05 | 0.0 〜 1000.0 | `1.0` |

#### DamageTagModifier

表示名「ダメージタグ倍率」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawDamageTagModifierComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 未登録Tag倍率 | damageTagDefaultMultiplier | float | 0.05 | 0.0 〜 1000.0 | `1.0` |
| Tag | [要素].tagName | string | — | 文字列 | — |
| 倍率 | [要素].multiplier | float | 0.05 | 0.0 〜 1000.0 | `1.0` |

#### ProjectileDetonator

表示名「弾起爆装置」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawProjectileDetonatorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 接触時起爆 | projectileDetonateOnContact | bool | — | true / false | `true` |
| 近接時起爆 | projectileDetonateOnProximity | bool | — | true / false | `false` |
| 寿命切れ時起爆 | projectileDetonateOnLifetime | bool | — | true / false | `false` |
| 近接Target | projectileDetonatorTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は TargetSteering | `-1` |
| 近接半径 | projectileDetonatorProximityRadius | float | 0.1 | 0.0 〜 100000.0 | `1.0` |
| AreaDamage | projectileDetonatorAreaDamageGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Action対象 | projectileDetonatorActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 起爆Action | projectileDetonatedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnProjectileDetonated"` |

#### ThreatTracker

表示名「脅威トラッカー」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawThreatTrackerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 監視対象 | threatTrackerTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 最大距離 | threatTrackerMaximumDistance | float | 1.0 | 0.0 〜 1000000.0 | `200.0` |
| 最低接近速度 | threatTrackerMinimumClosingSpeed | float | 0.1 | 0.0 〜 1000000.0 | `1.0` |
| 最大逸れ距離 | threatTrackerMaximumMissDistance | float | 0.1 | 0.0 〜 1000000.0 | `10.0` |
| 最大脅威数 | threatTrackerMaximumCount | int32 | 1 | — | `8` |
| Action対象 | threatTrackerActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 追加Action | threatTrackerAddedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnThreatAdded"` |
| 解除Action | threatTrackerLostActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnThreatLost"` |

#### RuntimeStateReset

表示名「実行状態リセット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRuntimeStateResetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Health | runtimeResetHealth | bool | — | true / false | `true` |
| State Machine | runtimeResetStateMachine | bool | — | true / false | `true` |
| Attribute / Counter | runtimeResetAttributes | bool | — | true / false | `true` |
| Target Lock | runtimeResetLocks | bool | — | true / false | `true` |
| Timer | runtimeResetTimers | bool | — | true / false | `true` |
| 破壊可能部位 | runtimeResetDestructibleParts | bool | — | true / false | `true` |
| Cooldown | runtimeResetCooldowns | bool | — | true / false | `true` |
| Action対象 | runtimeResetActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Reset Action | runtimeResetActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnRuntimeStateReset"` |

#### CooldownSet

表示名「クールダウンセット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawCooldownSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 名前 | [要素].name | string | — | 文字列 | — |
| 時間 | [要素].duration | float | 0.05 | 0.0 〜 36000.0 | `1.0` |
| 開始時使用可能 | [要素].startReady | bool | — | true / false | `true` |
| Action対象 | cooldownSetActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | cooldownSetCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnCooldownCompleted"` |

#### WeaponFirePattern

表示名「武器発射パターン」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponFirePatternComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| モード | weaponFirePatternMode | int32(選択) | — | 0=単発 / 1=バースト / 2=斉射 / 3=扇状拡散 / 4=発射点順番 / 5=チャージ | `0` |
| 発射数 | weaponFirePatternCount | int32 | 1 | — | `3` |
| 発射間隔 | weaponFirePatternInterval | float | 0.01 | 0.0 〜 60.0 | `0.1` |
| 扇状角度 deg | weaponFirePatternSpreadAngle | float | 0.1 | 0.0 〜 360.0 | `8.0` |
| チャージ秒 | weaponFirePatternChargeSeconds | float | 0.05 | 0.0 〜 60.0 | `0.75` |
| 発射点 | weaponFirePatternSpawnPointGameObjectIds | GameObject参照 (int32 ID) | — | 未設定は 未設定 | — |
| Action対象 | weaponFirePatternActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | weaponFirePatternCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnFirePatternCompleted"` |

#### TargetAssignment

表示名「ターゲット割り当て斉射」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTargetAssignmentComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 複数Target Lock | targetAssignmentMultiTargetLockGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 最大Target数 | targetAssignmentMaximumTargets | int32 | 1 | — | `8` |
| 発射間隔 | targetAssignmentInterval | float | 0.01 | 0.0 〜 60.0 | `0.1` |
| Lock完了Targetのみ | targetAssignmentLockedOnly | bool | — | true / false | `true` |
| Action対象 | targetAssignmentActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | targetAssignmentCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTargetSalvoCompleted"` |

#### WeaponAccuracy

表示名「武器命中精度」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponAccuracyComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 基礎Spread deg | weaponAccuracyBaseSpread | float | 0.05 | 0.0 〜 180.0 | `0.0` |
| 最大Spread deg | weaponAccuracyMaximumSpread | float | 0.05 | 0.0 〜 180.0 | `12.0` |
| 1発の増加 deg | weaponAccuracySpreadPerShot | float | 0.05 | 0.0 〜 180.0 | `0.5` |
| 毎秒回復 deg | weaponAccuracyRecoveryPerSecond | float | 0.05 | 0.0 〜 1000.0 | `3.0` |
| 移動Spread倍率 | weaponAccuracyMovementSpread | float | 0.05 | 0.0 〜 1000.0 | `0.0` |
| 分布 | weaponAccuracyDistribution | int32(選択) | — | 0=一様Cone / 1=一様Disk / 2=中心寄り | `2` |

#### WeaponRecoil

表示名「武器反動」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponRecoilComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Body Impulse | weaponRecoilBodyImpulse | Vector3 | 0.05 | -100000.0 〜 100000.0 | `{0.0, 0.0, -1.0}` |
| Body Torque | weaponRecoilBodyTorque | Vector3 | 0.05 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| 表示反動対象 | weaponRecoilVisualGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| 表示位置反動 | weaponRecoilVisualPosition | Vector3 | 0.01 | -1000.0 〜 1000.0 | `{0.0, 0.0, -0.1}` |
| 表示回転反動 rad | weaponRecoilVisualRotation | Vector3 | 0.01 | -100.0 〜 100.0 | `{0.0, 0.0, 0.0}` |
| 回復速度 | weaponRecoilRecoveryPerSecond | float | 0.1 | 0.0 〜 1000.0 | `8.0` |
| Camera Shake | weaponRecoilCameraShakeGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Action対象 | weaponRecoilActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 反動Action | weaponRecoilActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponRecoil"` |

#### ImpactResponder

表示名「命中応答」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawImpactResponderComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Damage Tag | [要素].damageTag | string | — | 文字列 | — |
| Surface Tag | [要素].surfaceTag | string | — | 文字列 | — |
| Effect Asset | [要素].effectAssetPath | string | — | 文字列 | — |
| Audio Source | [要素].audioSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Decal | [要素].decalGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Camera Shake | [要素].cameraShakeGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Action対象 | [要素].actionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 命中Action | [要素].actionName | string(Action名) | — | 対象ScriptのAction候補 | — |

#### SurfaceType

表示名「サーフェスタイプ」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawSurfaceTypeComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Surface Tag | surfaceTypeTag | string | — | 文字列 | `"Default"` |

#### TimeScale

表示名「時間倍率・ヒットストップ」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawTimeScaleComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 時間倍率 | timeScaleValue | float | 0.01 | 0.0 〜 8.0 | `0.0` |
| 継続秒（実時間） | timeScaleDuration | float | 0.01 | 0.0 〜 3600.0 | `0.1` |
| Blend秒（実時間） | timeScaleBlendSeconds | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| Play開始時に実行 | timeScalePlayOnStart | bool | — | true / false | `false` |
| Action対象 | timeScaleActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | timeScaleCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnTimeScaleCompleted"` |

#### AimAssist

表示名「照準補助」／カテゴリ「照準」／Inspector描画 `DrawAimAssistComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 画面照準 | aimAssistScreenAimGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Target Selector | aimAssistTargetSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 補助半径 | aimAssistRadius | float | 0.01 | 0.0 〜 1.0 | `0.12` |
| 補助強度 | aimAssistStrength | float | 0.01 | 0.0 〜 1.0 | `0.35` |
| 追従速度 | aimAssistFollowSpeed | float | 0.1 | 0.0 〜 100.0 | `8.0` |
| 入力中の抑制 | aimAssistInputSuppression | float | 0.01 | 0.0 〜 1.0 | `0.5` |

#### InterceptPrediction

表示名「迎撃予測」／カテゴリ「照準」／Inspector描画 `DrawInterceptPredictionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | interceptTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Target Selector | interceptTargetSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Projectile速度 | interceptProjectileSpeed | float | 0.1 | 0.01 〜 100000.0 | `100.0` |
| 最大予測秒 | interceptMaximumTime | float | 0.1 | 0.01 〜 3600.0 | `10.0` |

#### DamageDirectionIndicator

表示名「被弾方向表示」／カテゴリ「UI」／Inspector描画 `DrawDamageDirectionIndicatorComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 表示秒 | damageDirectionDuration | float | 0.05 | 0.0 〜 60.0 | `1.5` |
| Fade秒 | damageDirectionFadeSeconds | float | 0.05 | 0.0 〜 60.0 | `0.4` |
| 最低Damage | damageDirectionMinimumDamage | float | 0.1 | 0.0 〜 1000000.0 | `1.0` |
| 画面端半径 | damageDirectionEdgeRadius | float | 0.01 | 0.0 〜 1.0 | `0.45` |

#### ObjectiveTracker

表示名「目標トラッカー」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawObjectiveTrackerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ID | [要素].objectiveId | string | — | 文字列 | — |
| 表示名 | [要素].displayName | string | — | 文字列 | — |
| 状態 | [要素].state | int32(選択) | — | 0=無効 / 1=進行中 / 2=完了 / 3=失敗 | `0` |
| 現在値 | [要素].currentValue | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| 目標値 | [要素].targetValue | float | 0.1 | -1000000.0 〜 1000000.0 | `1.0` |
| Action対象 | objectiveActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 変更Action | objectiveChangedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnObjectiveChanged"` |

#### EncounterController

表示名「エンカウンター制御」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawEncounterControllerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Play開始時に実行 | encounterPlayOnStart | bool | — | true / false | `false` |
| Wave Spawner | [要素].waveSpawnerGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 開始前待機 | [要素].startDelay | float | 0.05 | 0.0 〜 3600.0 | `0.0` |
| 全撃破を待つ | [要素].waitsForAllDefeated | bool | — | true / false | `true` |
| Action対象 | encounterActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | encounterCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnEncounterCompleted"` |

#### SpawnPointSet

表示名「生成地点セット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawSpawnPointSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 選択方法 | spawnPointSetMode | int32(選択) | — | 0=単発 / 1=バースト / 2=斉射 / 3=扇状拡散 / 4=発射点順番 / 5=チャージ | `0` |
| Volume Size | spawnPointVolumeSize | Vector3 | 0.1 | 0.0 〜 100000.0 | `{10.0, 0.0, 10.0}` |
| 直前を避ける | spawnPointAvoidImmediateRepeat | bool | — | true / false | `true` |
| 生成地点 | [要素].gameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 重み | [要素].weight | float | 0.1 | 0.0 〜 100000.0 | `1.0` |

#### DifficultyParameterSet

表示名「難易度パラメーターセット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawDifficultyParameterSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 選択Index | difficultySelectedIndex | int32 | 1 | — | `1` |
| Play開始時に適用 | difficultyApplyOnStart | bool | — | true / false | `false` |
| 難易度名 | difficultyNames | string | — | 文字列 | `{"Easy", "Normal", "Hard"}` |
| 難易度Index | [要素].difficultyIndex | int32 | 1 | — | `0` |
| 対象 | [要素].targetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Component | [要素].componentName | string | — | 文字列 | — |
| Property | [要素].propertyName | string | — | 文字列 | — |
| 型 | [要素].valueType | int32(選択) | — | 0=Float / 1=Int / 2=Bool | `0` |
| 値 | [要素].floatValue | float | 0.1 | -1000000.0 〜 1000000.0 | `0.0` |
| 値 | [要素].intValue | int32 | 1 | — | `0` |
| 値 | [要素].boolValue | bool | — | true / false | `false` |
| Action対象 | difficultyActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 適用Action | difficultyAppliedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnDifficultyApplied"` |

#### CameraFeedbackMixer

表示名「カメラフィードバックミキサー」／カテゴリ「カメラ」／Inspector描画 `DrawCameraFeedbackMixerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 最大位置振幅 | cameraFeedbackMaximumPosition | Vector3 | 0.01 | 0.0 〜 1000.0 | `{1.0, 1.0, 1.0}` |
| 最大回転振幅 | cameraFeedbackMaximumRotation | Vector3 | 0.01 | 0.0 〜 100.0 | `{0.2, 0.2, 0.2}` |
| 最大同時数 | cameraFeedbackMaximumConcurrent | int32 | 1 | — | `8` |
| 合成 | cameraFeedbackMixMode | int32(選択) | — | 0=単発 / 1=バースト / 2=斉射 / 3=扇状拡散 / 4=発射点順番 / 5=チャージ | `0` |
| 全体強度 | cameraFeedbackGlobalStrength | float | 0.01 | 0.0 〜 10.0 | `1.0` |

#### BallisticPrediction

表示名「弾道予測」／カテゴリ「照準」／Inspector描画 `DrawBallisticPredictionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | ballisticTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| Target Selector | ballisticTargetSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 初速 | ballisticInitialSpeed | float | 0.1 | 0.01 〜 100000.0 | `80.0` |
| 重力 | ballisticGravity | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, -9.81, 0.0}` |
| 線形Drag | ballisticDrag | float | 0.001 | 0.0 〜 1000.0 | `0.0` |
| Target加速度 | ballisticTargetAcceleration | Vector3 | 0.01 | -10000.0 〜 10000.0 | `{0.0, 0.0, 0.0}` |
| 最大飛翔秒 | ballisticMaximumTime | float | 0.05 | 0.01 〜 3600.0 | `12.0` |
| 積分Step | ballisticSimulationStep | float | 0.001 | 0.001 〜 0.25 | `1.0 / 60.0` |
| 最大Point数 | ballisticMaximumPoints | int32 | 1 | — | `128` |
| 発射元速度を継承 | ballisticInheritSourceVelocity | bool | — | true / false | `true` |
| 速度Source | ballisticSourceVelocityGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 親Rigidbodyを検索 | ballisticUseParentRigidBody | bool | — | true / false | `true` |
| 並進速度継承 | ballisticLinearVelocityInheritance | float | 0.01 | -10.0 〜 10.0 | `1.0` |
| 角速度継承 | ballisticAngularVelocityInheritance | float | 0.01 | -10.0 〜 10.0 | `1.0` |
| 発射元World速度 | ballisticSourceVelocity | Vector3 | 0.0 | -1000000.0 〜 1000000.0 | `{0.0, 0.0, 0.0}` |
| 初期World速度 | ballisticLaunchVelocity | Vector3 | 0.0 | -1000000.0 〜 1000000.0 | `{0.0, 0.0, 0.0}` |

#### DamageEventBuffer

表示名「複数被弾履歴」／カテゴリ「UI」／Inspector描画 `DrawDamageEventBufferComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 最大Entry数 | damageEventMaximumEntries | int32 | 1 | — | `8` |
| 表示寿命 | damageEventLifetime | float | 0.05 | 0.01 〜 60.0 | `1.5` |
| 最低Damage | damageEventMinimumDamage | float | 0.1 | 0.0 〜 1000000.0 | `1.0` |
| 同じSourceを統合 | damageEventMergeSameSource | bool | — | true / false | `true` |

#### GamePause

表示名「ゲーム一時停止」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawGamePauseComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| ゲーム時間を停止 | gamePausePauseGameTime | bool | — | true / false | `true` |
| 物理を停止 | gamePausePausePhysics | bool | — | true / false | `true` |
| Audioを停止 | gamePausePauseAudio | bool | — | true / false | `true` |
| Gameplay Input Map | gamePauseGameplayInputMap | string | — | 文字列 | `"Gameplay"` |
| UI Input Map | gamePauseUiInputMap | string | — | 文字列 | `"UI"` |
| Action対象 | gamePauseActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Pause Action | gamePausePausedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnGamePaused"` |
| Resume Action | gamePauseResumedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnGameResumed"` |

#### SurfaceWakeEmitter

表示名「水面航跡エミッター」／カテゴリ「海・水面」／Inspector描画 `DrawSurfaceWakeEmitterComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Ocean | surfaceWakeOceanGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動検索 | `-1` |
| 左航跡Effect | surfaceWakeLeftEffectGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 右航跡Effect | surfaceWakeRightEffectGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 船首Spray Effect | surfaceWakeBowEffectGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 最低速度 | surfaceWakeMinimumSpeed | float | 0.05 | 0.0 〜 10000.0 | `0.5` |
| 最大強度速度 | surfaceWakeMaximumSpeed | float | 0.1 | 0.01 〜 10000.0 | `20.0` |
| 航跡幅 | surfaceWakeWidth | float | 0.05 | 0.01 〜 1000.0 | `1.5` |
| Foam寿命 | surfaceWakeLifetime | float | 0.05 | 0.01 〜 120.0 | `4.0` |
| 最大発生数/秒 | surfaceWakeMaximumEmissionRate | float | 1.0 | 0.0 〜 100000.0 | `80.0` |
| 水面へ局所波を与える | surfaceWakeAffectOceanSurface | bool | — | true / false | `false` |
| 局所波の強度 | surfaceWakeWaveAmplitudeScale | float | 0.01 | 0.0 〜 2.0 | `0.10` |
| 局所波の影響半径 | surfaceWakeWaveRadiusScale | float | 0.1 | 0.5 〜 20.0 | `4.0` |

#### TrajectoryRenderer

表示名「軌道プレビュー」／カテゴリ「描画・レンダリング」／Inspector描画 `DrawTrajectoryRendererComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 弾道予測 | trajectoryPredictionGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 透明度 | trajectoryAlpha | float | 0.01 | 0.0 〜 1.0 | `0.9` |
| 太さ | trajectoryThickness | float | 0.1 | 0.1 〜 20.0 | `2.0` |
| 最大Point数 | trajectoryMaximumPoints | int32 | 1 | — | `128` |
| Scene View | trajectoryShowInSceneView | bool | — | true / false | `true` |
| Game View | trajectoryShowInGameView | bool | — | true / false | `true` |
| 着弾点 | trajectoryShowImpactPoint | bool | — | true / false | `true` |

#### WaterSurfaceState

表示名「水面出入り状態」／カテゴリ「海・水面」／Inspector描画 `DrawWaterSurfaceStateComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Ocean | waterSurfaceOceanGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動検索 | `-1` |
| ローカル判定位置 | waterSurfaceLocalOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| Clearance | waterSurfaceClearance | float | 0.01 | -1000.0 〜 1000.0 | `0.0` |
| Action対象 | waterSurfaceActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 入水Action | waterSurfaceEnteredActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaterEntered"` |
| 出水Action | waterSurfaceExitedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWaterExited"` |

#### OceanProbeSet

表示名「海面前方プローブ」／カテゴリ「海・水面」／Inspector描画 `DrawOceanProbeSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Ocean | oceanProbeOceanGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 自動検索 | `-1` |
| ローカル原点 | oceanProbeLocalOriginOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 0.0, 0.0}` |
| ローカル方向 | oceanProbeLocalDirection | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 0.0, 1.0}` |
| 距離 | probeEntry.distance | float | 0.1 | 0.0 〜 1000000.0 | — |

#### AttackCollisionFilter

表示名「攻撃コリジョンフィルター」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawAttackCollisionFilterComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 発射責任者 | attackFilterInstigatorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Filter所有者 | `-1` |
| 発射者を無視 | attackFilterIgnoreInstigator | bool | — | true / false | `true` |
| 発射者の子も無視 | attackFilterIgnoreInstigatorHierarchy | bool | — | true / false | `true` |
| Teamルール | attackFilterTeamRule | int32(選択) | — | 0=すべて / 1=異なるTeamのみ / 2=同じTeamのみ | `1` |
| Neutralを無視 | attackFilterIgnoreNeutral | bool | — | true / false | `false` |
| Arming距離 | attackFilterArmingDistance | float | 0.1 | 0.0 〜 100000.0 | `1.0` |
| 無視Object | attackFilterIgnoredGameObjectIds | GameObject参照 (int32 ID) | — | 未設定は 未設定 | — |

#### TurretAim

表示名「砲塔照準」／カテゴリ「照準」／Inspector描画 `DrawTurretAimComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 明示Target | turretTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Selectorを使用 | `-1` |
| Target Selector | turretTargetSelectorGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Yaw Pivot | turretYawPivotGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Pitch Pivot | turretPitchPivotGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Yaw Pivot | `-1` |
| Yaw最小 deg | turretYawMinimumDegrees | float | 0.1 | -360.0 〜 360.0 | `-180.0` |
| Yaw最大 deg | turretYawMaximumDegrees | float | 0.1 | -360.0 〜 360.0 | `180.0` |
| Pitch最小 deg | turretPitchMinimumDegrees | float | 0.1 | -180.0 〜 180.0 | `-10.0` |
| Pitch最大 deg | turretPitchMaximumDegrees | float | 0.1 | -180.0 〜 180.0 | `75.0` |
| Yaw速度 deg/s | turretYawSpeedDegrees | float | 1.0 | 0.0 〜 100000.0 | `90.0` |
| Pitch速度 deg/s | turretPitchSpeedDegrees | float | 1.0 | 0.0 〜 100000.0 | `60.0` |
| 照準許容角 deg | turretAimToleranceDegrees | float | 0.1 | 0.0 〜 180.0 | `2.0` |
| Target予測秒 | turretPredictionSeconds | float | 0.01 | 0.0 〜 60.0 | `0.0` |

#### WeaponGroup

表示名「武器グループ」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWeaponGroupComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 発射方式 | weaponGroupMode | int32(選択) | — | 0=単発 / 1=バースト / 2=斉射 / 3=扇状拡散 / 4=発射点順番 / 5=チャージ | `0` |
| 順次間隔 | weaponGroupInterval | float | 0.01 | 0.0 〜 3600.0 | `0.1` |
| 全武器Ready必須 | weaponGroupRequireAllReady | bool | — | true / false | `true` |
| Weapon | [要素].weaponGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| 使用 | [要素].isEnabled | bool | — | true / false | `true` |
| 完了Action対象 | weaponGroupActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 完了Action | weaponGroupCompletedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnWeaponGroupCompleted"` |

#### ProjectileImpactPhysics

表示名「弾体貫通・跳弾」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawProjectileImpactPhysicsComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 初期貫通Energy | projectileImpactPenetrationEnergy | float | 0.1 | 0.0 〜 1000000.0 | `0.0` |
| 基本貫通損失 | projectileImpactPenetrationLoss | float | 0.1 | 0.0 〜 1000000.0 | `1.0` |
| 最大貫通回数 | projectileImpactMaximumPenetrations | int32 | 1 | — | `0` |
| 跳弾開始角 deg | projectileImpactRicochetAngleDegrees | float | 0.1 | 0.0 〜 90.0 | `75.0` |
| 速度保持率 | projectileImpactEnergyRetention | float | 0.01 | 0.0 〜 1.0 | `0.65` |
| Damage保持率 | projectileImpactDamageRetention | float | 0.01 | 0.0 〜 1.0 | `0.75` |
| 最大跳弾回数 | projectileImpactMaximumRicochets | int32 | 1 | — | `0` |
| Surface Tag | [要素].surfaceTag | string | — | 文字列 | — |
| 貫通損失倍率 | [要素].penetrationLossMultiplier | float | 0.01 | 0.0 〜 1000.0 | `1.0` |
| 跳弾角Offset | [要素].ricochetAngleOffset | float | 0.1 | -90.0 〜 90.0 | `0.0` |
| Energy保持倍率 | [要素].energyRetentionMultiplier | float | 0.01 | 0.0 〜 1000.0 | `1.0` |

#### CameraHorizonStabilizer

表示名「水平線スタビライザー」／カテゴリ「カメラ」／Inspector描画 `DrawCameraHorizonStabilizerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 追従Source | horizonSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 未設定 | `-1` |
| ローカル位置Offset | horizonLocalPositionOffset | Vector3 | 0.01 | -100000.0 〜 100000.0 | `{0.0, 2.0, -6.0}` |
| 回転Offset deg | horizonRotationOffsetDegrees | Vector3 | 0.1 | -360.0 〜 360.0 | `{0.0, 0.0, 0.0}` |
| 位置を追従 | horizonFollowPosition | bool | — | true / false | `true` |
| Pitch継承 | horizonPitchInheritance | float | 0.01 | 0.0 〜 1.0 | `0.35` |
| Yaw継承 | horizonYawInheritance | float | 0.01 | 0.0 〜 1.0 | `1.0` |
| Roll継承 | horizonRollInheritance | float | 0.01 | 0.0 〜 1.0 | `0.2` |
| World Up | horizonWorldUp | Vector3 | 0.01 | -1.0 〜 1.0 | `{0.0, 1.0, 0.0}` |
| 減衰 | horizonDamping | float | 0.1 | 0.0 〜 1000.0 | `8.0` |
| 最大Roll deg | horizonMaximumRollDegrees | float | 0.1 | 0.0 〜 180.0 | `8.0` |

#### FireLineCheck

表示名「発射前射線チェック」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawFireLineCheckComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 砲口 | fireLineMuzzleGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 前方向Source | fireLineDirectionGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 砲口 | `-1` |
| 許可Target | fireLineAllowedTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は なし | `-1` |
| 検査距離 | fireLineDistance | float | 0.1 | 0.0 〜 1000000.0 | `10.0` |
| 検査半径 | fireLineRadius | float | 0.01 | 0.0 〜 100000.0 | `0.05` |
| Block Layer Mask | fireLineLayerMask | int32 | 1 | — | `-1` |
| 無視Object | fireLineIgnoredGameObjectIds | GameObject参照 (int32 ID) | — | 未設定は 未設定 | — |

#### StatusEffectSet

表示名「状態効果セット」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawStatusEffectSetComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Action対象 | statusEffectActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Effect ID | [要素].effectId | string | — | 文字列 | — |
| Duration | [要素].duration | float | 0.05 | 0.001 〜 1000000.0 | `5.0` |
| Stack Mode | [要素].stackMode | int32(選択) | — | 0=Refresh / 1=Stack / 2=Ignore | `0` |
| 最大Stack | [要素].maximumStacks | int32 | 1 | — | `1` |
| Tick間隔 | [要素].tickInterval | float | 0.05 | 0.0 〜 1000000.0 | `1.0` |
| 開始Action | [要素].startedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnStatusEffectStarted"` |
| Tick Action | [要素].tickActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnStatusEffectTick"` |
| 終了Action | [要素].endedActionName | string(Action名) | — | 対象ScriptのAction候補 | `"OnStatusEffectEnded"` |

#### RailSpeedProfile

表示名「レール速度プロファイル」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRailSpeedProfileComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| プロファイルを使用 | railSpeedProfileEnabled | bool | — | true / false | `true` |
| 進行率 | speedKey.normalizedProgress | float | 0.01 | 0.0 〜 1.0 | — |
| 速度倍率 | speedKey.speedMultiplier | float | 0.01 | 0.0 〜 100.0 | — |

#### RailZone

表示名「レール区間」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawRailZoneComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Action対象 | railZoneActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Zone ID | [要素].zoneId | string | — | 文字列 | — |
| 開始進行率 | [要素].startNormalized | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 終了進行率 | [要素].endNormalized | float | 0.01 | 0.0 〜 1.0 | `0.25` |
| 速度倍率 | [要素].speedMultiplier | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| 移動範囲を上書き | [要素].overrideMovementRange | bool | — | true / false | `false` |
| 左右・上下範囲 | [要素].movementRange | Vector2 | 0.1 | 0.0 〜 10000.0 | `{5.0, 3.0}` |
| 進入Action | [要素].enteredActionName | string | — | 文字列 | `"OnRailZoneEntered"` |
| 退出Action | [要素].exitedActionName | string | — | 文字列 | `"OnRailZoneExited"` |

#### CameraFollowComposer

表示名「カメラ追従コンポーザー」／カテゴリ「カメラ」／Inspector描画 `DrawCameraFollowComposerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 追従対象 | cameraComposerTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Cameraの接続先 | `-1` |
| 追従Offset | cameraComposerFollowOffset | Vector3 | 0.1 | -10000.0 〜 10000.0 | `{0.0, 3.0, -8.0}` |
| 注視Offset | cameraComposerLookAtOffset | Vector3 | 0.1 | -10000.0 〜 10000.0 | `{0.0, 1.0, 6.0}` |
| 位置減衰 | cameraComposerPositionDamping | float | 0.1 | 0.0 〜 1000.0 | `6.0` |
| 回転減衰 | cameraComposerRotationDamping | float | 0.1 | 0.0 〜 1000.0 | `8.0` |
| 速度先読み秒 | cameraComposerLookAheadSeconds | float | 0.01 | 0.0 〜 10.0 | `0.25` |
| デッドゾーン | cameraComposerDeadZone | Vector2 | 0.01 | 0.0 〜 1000.0 | `{0.0, 0.0}` |
| 1Frame最大追従距離 | cameraComposerMaximumDistance | float | 0.1 | 0.0 〜 10000.0 | `30.0` |
| 対象Yawを継承 | cameraComposerInheritTargetYaw | bool | — | true / false | `true` |
| Pitch/Rollを安定化 | cameraComposerStabilizePitchRoll | bool | — | true / false | `true` |

#### SpeedFeedback

表示名「速度フィードバック」／カテゴリ「カメラ」／Inspector描画 `DrawSpeedFeedbackComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 速度Source | speedFeedbackSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| 対象Camera | speedFeedbackCameraGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 最高Priority Camera | `-1` |
| 最小速度 | speedFeedbackMinimumSpeed | float | 0.1 | 0.0 〜 10000.0 | `0.0` |
| 最大速度 | speedFeedbackMaximumSpeed | float | 0.1 | 0.01 〜 10000.0 | `40.0` |
| 最小FOV | speedFeedbackMinimumFovDegrees | float | 0.1 | 1.0 〜 179.0 | `60.0` |
| 最大FOV | speedFeedbackMaximumFovDegrees | float | 0.1 | 1.0 〜 179.0 | `78.0` |
| 最小Blur | speedFeedbackMinimumMotionBlur | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 最大Blur | speedFeedbackMaximumMotionBlur | float | 0.01 | 0.0 〜 1.0 | `0.2` |
| Camera強度加算 | speedFeedbackCameraStrength | float | 0.01 | 0.0 〜 10.0 | `0.35` |
| 応答速度 | speedFeedbackResponseSpeed | float | 0.1 | 0.0 〜 1000.0 | `5.0` |

#### SpawnedObjectSetup

表示名「生成オブジェクト設定」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawSpawnedObjectSetupComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Rail Path | spawnedSetupRailPathGameObjectId | GameObject参照 (int32 ID) | — | 未設定は Template設定を使用 | `-1` |
| 開始進行率 | spawnedSetupRailStartNormalized | float | 0.01 | 0.0 〜 1.0 | `0.0` |
| 個体ごとの進行率差 | spawnedSetupRailStartStep | float | 0.001 | -1.0 〜 1.0 | `0.0` |
| Rail速度倍率 | spawnedSetupRailSpeedMultiplier | float | 0.01 | 0.0 〜 100.0 | `1.0` |
| Teamを上書き | spawnedSetupOverrideTeam | bool | — | true / false | `false` |
| Team ID | spawnedSetupTeamId | int32 | 1 | — | `1` |
| 生成時にRuntime状態をReset | spawnedSetupResetRuntimeState | bool | — | true / false | `true` |
| Action対象 | spawnedSetupActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このWave | `-1` |
| 適用完了Action | spawnedSetupAppliedActionName | string | — | 文字列 | `"OnSpawnedObjectSetup"` |

#### WaveMotionProfile

表示名「ウェーブ移動プロファイル」／カテゴリ「ゲームプレイ」／Inspector描画 `DrawWaveMotionProfileComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 移動パターン | waveMotionMode | int32(選択) | — | 0=なし / 1=Sine / 2=8の字 / 3=交互運動 | `0` |
| 左右・上下振幅 | waveMotionAmplitude | Vector2 | 0.1 | 0.0 〜 10000.0 | `{6.0, 2.0}` |
| 周波数 | waveMotionFrequency | float | 0.01 | 0.0 〜 1000.0 | `0.35` |
| 個体ごとの位相差 | waveMotionPhaseStep | float | 0.01 | -100.0 〜 100.0 | `0.7` |
| Blend In秒 | waveMotionBlendInSeconds | float | 0.01 | 0.0 〜 1000.0 | `0.5` |

#### DistanceActivation

表示名「距離アクティベーション」／カテゴリ「最適化」／Inspector描画 `DrawDistanceActivationComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 距離基準 | distanceActivationReferenceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 最高Priority Camera | `-1` |
| 有効化距離 | distanceActivationEnterDistance | float | 1.0 | 0.0 〜 1000000.0 | `250.0` |
| 無効化距離 | distanceActivationExitDistance | float | 1.0 | 0.0 〜 1000000.0 | `300.0` |
| 子階層も対象 | distanceActivationAffectHierarchy | bool | — | true / false | `true` |

#### SimulationLOD

表示名「シミュレーション LOD」／カテゴリ「最適化」／Inspector描画 `DrawSimulationLodComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 距離基準 | simulationLodReferenceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 最高Priority Camera | `-1` |
| Medium距離 | simulationLodMediumDistance | float | 1.0 | 0.0 〜 1000000.0 | `100.0` |
| Far距離 | simulationLodFarDistance | float | 1.0 | 0.0 〜 1000000.0 | `250.0` |
| Culled距離 | simulationLodCulledDistance | float | 1.0 | 0.0 〜 1000000.0 | `500.0` |
| Medium Script更新秒 | simulationLodMediumScriptInterval | float | 0.01 | 0.0 〜 10.0 | `1.0 / 30.0` |
| Far Script更新秒 | simulationLodFarScriptInterval | float | 0.01 | 0.0 〜 10.0 | `0.2` |
| FarでPhysics停止 | simulationLodDisablePhysicsAtFar | bool | — | true / false | `true` |
| FarでScript停止 | simulationLodDisableScriptsAtFar | bool | — | true / false | `false` |
| FarでAI停止 | simulationLodDisableAiAtFar | bool | — | true / false | `true` |
| FarでAnimation停止 | simulationLodDisableAnimationAtFar | bool | — | true / false | `true` |
| FarでEffect停止 | simulationLodDisableEffectsAtFar | bool | — | true / false | `true` |
| 子階層も対象 | simulationLodAffectHierarchy | bool | — | true / false | `true` |

#### RailEventMarker

表示名「レールイベントマーカー」／カテゴリ「入力・イベント」／Inspector描画 `DrawRailEventMarkerComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Action対象 | railEventMarkerActionTargetGameObjectId | GameObject参照 (int32 ID) | — | 未設定は このObject | `-1` |
| Marker ID | [要素].markerId | string | — | 文字列 | — |
| 進行率 | [要素].normalizedProgress | float | 0.01 | 0.0 〜 1.0 | `0.5` |
| 通過方向 | [要素].directionMode | int32(選択) | — | 0=両方向 / 1=順方向のみ / 2=逆方向のみ | `0` |
| Play中1回だけ | [要素].triggerOnce | bool | — | true / false | `true` |
| Action | [要素].actionName | string | — | 文字列 | `"OnRailMarker"` |

#### SceneStreaming

表示名「Scene Streaming」／カテゴリ「最適化」／Inspector描画 `DrawSceneStreamingComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| Scene Path | sceneStreamingScenePath | string | — | 文字列 | — |
| 距離基準 | sceneStreamingReferenceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 最高Priority Camera | `-1` |
| 読込距離 | sceneStreamingLoadDistance | float | 1.0 | 0.0 〜 1000000.0 | `500.0` |
| 解除距離 | sceneStreamingUnloadDistance | float | 1.0 | 0.0 〜 1000000.0 | `650.0` |
| 遠距離でSceneを破棄 | sceneStreamingUnloadWhenFar | bool | — | true / false | `true` |

#### TextEffect

表示名「テキストエフェクト」／カテゴリ「UI」／Inspector描画 `DrawTextEffectComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 出現効果 | textAppearEffectType | int32(選択) | — | 0=なし / 1=フェードイン / 2=タイプライター / 3=スケールポップ | `0` |
| 継続時間(秒) | textAppearDuration | float | 0.01 | 0.01 〜 60.0 | `0.6` |
| 開始遅延(秒) | textAppearDelay | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| 文字/秒 | textAppearParamA | float | 0.1 | 0.1 〜 200.0 | `12.0` |
| オーバーシュート倍率 | textAppearParamA | float | 0.01 | 1.0 〜 3.0 | `12.0` |
| 常時効果 | textContinuousEffectType | int32(選択) | — | 0=なし / 1=点滅 / 2=レインボー / 3=波 / 4=発光 / 5=輪郭の発光 / 6=色収差 / 7=微振動 / 8=不規則点滅 / 9=脈動 / 10=残像 / 11=簡易グリッチ | `0` |
| 開始遅延(秒) | textContinuousDelay | float | 0.01 | 0.0 〜 60.0 | `0.0` |
| 点滅間隔(秒) | textContinuousParamA | float | 0.01 | 0.02 〜 5.0 | `8.0` |
| 色相回転速度(周/秒) | textContinuousParamA | float | 0.01 | 0.01 〜 10.0 | `8.0` |
| 文字ごとの色ずれ | textContinuousParamB | float | 0.01 | 0.0 〜 1.0 | `0.35` |
| 振幅(px) | textContinuousParamA | float | 0.1 | 0.0 〜 200.0 | `8.0` |
| 文字ごとの位相 | textContinuousParamB | float | 0.01 | 0.0 〜 3.0 | `0.35` |
| 揺れる速さ | textContinuousParamC | float | 0.1 | 0.0 〜 20.0 | `3.0` |
| Glow半径(px) | textContinuousParamA | float | 0.1 | 0.0 〜 60.0 | `8.0` |
| 脈動速度(0で静止) | textContinuousParamB | float | 0.05 | 0.0 〜 20.0 | `0.35` |
| 脈動の深さ | textContinuousParamC | float | 0.01 | 0.0 〜 1.0 | `3.0` |
| 輪郭太さ(px) | textContinuousParamA | float | 0.1 | 0.5 〜 20.0 | `8.0` |
| ズレ量(px) | textContinuousParamA | float | 0.1 | 0.0 〜 20.0 | `8.0` |
| 振れ幅(px) | textContinuousParamA | float | 0.1 | 0.0 〜 40.0 | `8.0` |
| 速さ | textContinuousParamB | float | 0.1 | 0.1 〜 60.0 | `0.35` |
| 最低輝度(0-1) | textContinuousParamA | float | 0.01 | 0.0 〜 1.0 | `8.0` |
| フリッカー速さ | textContinuousParamB | float | 0.1 | 0.1 〜 30.0 | `0.35` |
| 最大拡大率 | textContinuousParamA | float | 0.01 | 1.0 〜 2.0 | `8.0` |
| 残像の数 | textContinuousParamC | float | 1.0 | 2.0 〜 6.0 | `3.0` |
| 発生頻度(回/秒) | textContinuousParamB | float | 0.05 | 0.05 〜 10.0 | `0.35` |
| 強さ(0-1) | textContinuousParamC | float | 0.01 | 0.0 〜 1.0 | `3.0` |

#### SceneTransition

表示名「シーン遷移」／カテゴリ「エフェクト」／Inspector描画 `DrawSceneTransitionComponent`

| Inspector表示 | 内部Field | 型 | ドラッグ量 | 範囲・選択肢 | 既定値 |
| --- | --- | --- | --- | --- | --- |
| 種類 | sceneTransitionType | int32(選択) | — | 0=なし / 1=色フェード / 2=ワイプ / 3=Camera Dive | `0` |
| 遷移先 Scene | sceneTransitionTargetScenePath | string | — | 文字列 | `""` |
| 覆うまでの時間(秒) | sceneTransitionOutDuration | float | 0.01 | 0.01 〜 30.0 | `0.8` |
| 静止時間(秒) | sceneTransitionHoldSeconds | float | 0.01 | 0.0 〜 30.0 | `0.2` |
| 見せる時間(秒) | sceneTransitionInDuration | float | 0.01 | 0.01 〜 30.0 | `0.8` |
| 色 | sceneTransitionColor | Vector3 | 0.01 | 0.0 〜 1.0 | `{1.0, 1.0, 1.0}` |
| Dive基準Object | sceneTransitionCameraDiveSourceGameObjectId | GameObject参照 (int32 ID) | — | 未設定は 遷移開始時のCamera位置 | `-1` |
| Dive開始位置 Offset | sceneTransitionCameraDivePositionOffset | Vector3 | 0.1 | -100000.0 〜 100000.0 | `{0.0, 60.0, 0.0}` |
| Dive開始角度 deg | sceneTransitionCameraDiveRotationDegrees | Vector3 | 0.1 | -360.0 〜 360.0 | `{90.0, 0.0, 0.0}` |
