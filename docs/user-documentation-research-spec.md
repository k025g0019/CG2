# CG2 使用者向けドキュメント作成用 調査仕様書

## 1. この仕様書の目的

この仕様書は、CG2 エディタの使用者向けドキュメントサイトを作る前に、エンジンの機能、操作方法、コンポーネント、C++ スクリプト API、制限事項をコードから調査するための指示書である。

完成サイトの文章そのものではない。調査担当者がコード全体を毎回読み直さなくても、ChatGPT Work などの文書作成担当へ正確な一次情報を渡せる状態を作ることが目的である。

調査結果は、次の質問へ使用者視点で答えられる内容にする。

- 何ができる機能か。
- どのような場面で使うか。
- エディタのどこから追加、作成、設定、実行するか。
- 使用するために何が必要か。
- Inspector の各項目は何を意味するか。
- 編集中と Play 中で何が変わるか。
- Scene や Prefab に何が保存されるか。
- C++ スクリプトからどの API を、いつ、どの値で呼ぶか。
- 現在どこまで動き、何が未完成か。
- 動かない場合に何をどの順番で確認するか。

## 2. 調査の基本原則

### 2.1 使用者が触れる機能だけを中心にする

主な対象は、エディタ画面、メニュー、Inspector、Scene、GameObject、Component、Asset、Play、C++ スクリプト API である。

Descriptor Heap、Pipeline State、内部キャッシュ、Jolt Physics への変換式などは、使用手順や制限に影響する場合だけ使用者向けの言葉へ変換して記録する。

### 2.2 README の記述だけで実装状態を断定しない

既存の `README.md` と `docs/` は参考資料として使用できるが、実装状態の最終根拠にはしない。コード変更に追従していない可能性があるため、必ず現在の C++、HLSL、保存処理、Inspector を確認する。

### 2.3 「追加できる」と「動く」を分ける

コンポーネントに enum、追加メニュー、Inspector 表示が存在しても、Play 中の処理が存在するとは限らない。各コンポーネントは最低でも次の 5 層を個別に判定する。

| 層 | 確認内容 |
| --- | --- |
| 追加 | 「コンポーネントを追加」から選べるか。 |
| 編集 | Inspector で値を変更できるか。 |
| 保存 | Scene / Prefab へ値が保存され、再読み込みできるか。 |
| 実行 | Play 中に実際の描画、物理、入力、音、AI などへ反映されるか。 |
| API | C++ スクリプトから取得または操作できるか。 |

### 2.4 不明なことを推測で埋めない

コードから確認できない項目は「未確認」と記載する。名称から Unity と同じ挙動を推測してはいけない。

### 2.5 使用者が再現できる手順にする

機能説明には、メニュー名、ボタン名、コンポーネント名、必要なファイル、設定値、Play の有無を含める。「設定する」「使用する」だけで終わらせない。

## 3. 推奨する調査・文章生成モデル

ChatGPT Work へ渡す場合は、利用可能なモデルの中で最も長いコンテキストと高い推論性能を持つモデルを使う。高速回答用の小型モデルより、複数ファイルの整合性確認と長文構成を優先する。

モデルへは、この仕様書と調査結果を同時に渡す。ソースコード全体を最初から投入するのではなく、調査結果に不足がある場合だけ該当コードを追加する。

## 4. 根拠の優先順位

記述が食い違う場合は、次の順で信頼する。

1. 現在の Play 実行経路と各 Manager の実処理。
2. Inspector、メニュー、Project、Hierarchy などの現在の UI 実装。
3. Scene / Prefab の保存・読み込み処理。
4. 公開 C++ スクリプト API と DLL 呼び出し処理。
5. Shader の接続処理と実際に呼ばれる Pass。
6. `README.md` と `docs/` の既存説明。
7. ファイル名、enum 名、コメントだけから得られる情報。

## 5. 実装状態の統一表記

すべての機能とコンポーネントに、次のいずれかを付ける。

| 状態 | 判定基準 |
| --- | --- |
| 使用可能 | UI、保存、Play 実処理が接続され、基本的な使用例を成立させられる。 |
| 一部使用可能 | 基本動作はあるが、設定の一部、組み合わせ、精度、保存、API のいずれかが不足している。 |
| 設定画面のみ | 追加と Inspector 表示はあるが、Play 実処理へ接続されていない。 |
| 実験的 | 実処理はあるが、クラッシュ、描画破綻、精度不足などの既知リスクが大きい。 |
| 未実装 | 使用者が到達できる処理が存在しない。 |
| 未確認 | 静的調査だけでは動作を断定できず、実機確認が必要である。 |
| 既知の不具合あり | 使用可能または一部使用可能だが、再現条件が分かっている不具合がある。 |

「使用可能」とする場合は、根拠となる UI、保存、実行処理を最低 1 件ずつ示す。

## 6. 根拠の記録形式

各主張には、可能な限り次の形式で根拠を付ける。

```text
根拠:
- Source/Engine/Editor/EditorInspectorPanel.cpp : DrawRigidBodyComponent
- Source/Engine/Editor/EditorScene.cpp : SaveScene / LoadScene
- Source/Engine/Editor/EditorJoltPhysicsManager.cpp : Update / CreateBody
```

行番号だけに依存せず、ファイル名と関数名、型名、定数名を併記する。行番号は変更でずれるため補助情報として扱う。

実機確認を行った場合は次も記録する。

```text
実機確認:
- 構成: Cube + Rigidbody + 箱の当たり判定 + 床
- 操作: Play を押して 5 秒待機
- 期待結果: Cube が落下して床で停止する
- 結果: 成功 / 失敗 / 一部成功
- 確認日: YYYY-MM-DD
- ビルド: Debug または Release、x64
```

## 7. 最初に作る全体インベントリ

調査開始時に、次の一覧を作る。

| 一覧 | 主な根拠 |
| --- | --- |
| エディタウィンドウ一覧 | `Source/Engine/Editor/*Panel*`、`*WindowManager*`、Docking 関連 |
| 上部メニュー一覧 | `EditorMainMenuBar.cpp`、`EditorMainMenuManager.cpp` |
| GameObject 作成一覧 | Main Menu、Hierarchy、Asset Factory、Scene Object Manager |
| Component 全一覧 | `EditorScene.h` の `EditorComponentType` |
| Component 追加時の日本語名と分類 | `EditorInspectorPanel.cpp` の `kComponentAddEntries` |
| Inspector 項目一覧 | `EditorInspectorPanel.cpp` の各 `Draw...Component` |
| Scene / Prefab 保存項目一覧 | `EditorScene.cpp` の保存・読み込み |
| Play 実行 Manager 一覧 | `EditorRuntimeManager.h/.cpp` |
| C++ Script API 全一覧 | `Source/Engine/Core/EditorScriptApi.h` |
| 対応 Asset 拡張子一覧 | `EditorAssetUtility.cpp`、`EditorBottomPanel.cpp`、Asset Factory |
| 描画 Pass 一覧 | `EditorPlatformManager.cpp`、`EditorRenderManager.cpp`、Renderer Manager 群 |
| Shader 一覧 | `Assets/Shaders/` と Visual Studio プロジェクト登録 |
| 外部ライブラリ一覧 | `CG2.vcxproj`、`ThirdParty/`、`externals/`、include / lib 設定 |

## 8. 調査対象となる主要ソース

### 8.1 GameObject と Component データ

| ファイル | 調査内容 |
| --- | --- |
| `Source/Engine/Editor/EditorScene.h` | Component 種類、GameObject、Component 全フィールド、Prefab 構造。 |
| `Source/Engine/Editor/EditorScene.cpp` | 初期値、追加時設定、Scene / Prefab 保存、読み込み、型名変換。 |
| `Source/Engine/Editor/EditorComponentUtility.cpp` | Component の検索や共通操作。 |
| `Source/Engine/Editor/EditorSceneObjectManager.cpp` | 描画用 SceneObject の生成、削除、モデル種別。 |
| `Source/Engine/Editor/EditorSceneSynchronizer.cpp` | Editor Component の値が描画データへどう反映されるか。 |

### 8.2 UI と操作

| ファイル | 調査内容 |
| --- | --- |
| `EditorMainMenuBar.cpp` | メニューバーに表示される項目。 |
| `EditorMainMenuManager.cpp` | 新規作成、保存、読み込み、設定、終了などの実処理。 |
| `EditorHierarchyPanel.cpp` | 選択、親子、削除、複製、検索。 |
| `EditorSceneViewManager.cpp` | Scene View、ギズモ、ドラッグ配置、デバッグ表示。 |
| `EditorGameViewManager.cpp` | Game View と Camera の使用条件。 |
| `EditorInspectorPanel.cpp` | 全 Inspector 項目、Component 追加、項目名、入力範囲。 |
| `EditorBottomPanel.cpp` | Project、Console、Asset 操作。 |
| `EditorDockingManager.cpp` | Docking と初期レイアウト。 |
| `EditorSceneCameraController.cpp` | Scene カメラ操作とキー割り当て。 |

### 8.3 Play 実処理

| ファイル | 調査内容 |
| --- | --- |
| `EditorRuntimeManager.cpp` | Play 開始、毎フレーム更新、固定更新、停止時の呼び出し順。 |
| `EditorPhysicsManager.cpp` | 物理全体の呼び出しと設定。 |
| `EditorJoltPhysicsManager.cpp` | Rigidbody、Collider、Trigger、Raycast、Joint、Character。 |
| `EditorInputManager.cpp` | Input / PlayerInput の状態更新。 |
| `EditorScriptManager.cpp` | DLL 読み込み、ライフサイクル、公開変数、Input Action、API 呼び出し。 |
| `EditorAudioManager.cpp` | AudioSource、再生、停止、Filter の実処理。 |
| `EditorAnimationManager.cpp` | Animation / Animator の更新と読み込み範囲。 |
| `EditorConstraintManager.cpp` | Animation Constraint の反映。 |
| `EditorNavigationManager.cpp` | NavMesh、Agent、Obstacle、経路更新。 |
| `EditorAIManager.cpp` | AI Component、外部プロセス、Sensor、Python 連携。 |
| `EditorLocalMoveManager.cpp` | ローカル移動 Component。 |
| `EditorRollingMoveManager.cpp` | トルク、摩擦を利用する転がり移動 Component。 |
| `EditorFreeTransformManager.cpp` | 軸選択付きの非物理移動・回転。 |
| `EditorRailMovementManager.cpp` | Spline Sample、距離基準移動、RailFollower Runtime API。 |
| `EditorWaveSpawnerManager.cpp` | Wave設定を保持し、出現時だけObjectPoolから実体生成する。編隊Offset、Rail再初期化、貸出世代、全生成・全撃破Action。旧Sceneだけ子方式へフォールバック。 |
| `EditorGameplayEventManager.cpp` | Timeline EventとThreshold Stateの条件評価、Action通知。 |
| `EditorUiBindingManager.cpp` | Health / Rail / ActiveからText / Sliderへの値反映。 |
| `EditorOceanSystem.cpp` | Oceanの選択、波面Sample、Buoyancyへの共通波面提供。 |
| `EditorGameBuildManager.cpp` | Build Settings保存、Release Player書き出し、Standalone起動。 |

### 8.4 描画

| ファイル | 調査内容 |
| --- | --- |
| `EditorPlatformManager.cpp` | DirectX12 初期化、Resource、PSO、Shader コンパイル、Render Target。 |
| `EditorRenderManager.cpp` | Scene / Game の描画順、Light、Shadow、Reflection、PostProcess。 |
| `EditorGBufferManager.*` | GBuffer の生成と使用箇所。 |
| `EditorDepthHierarchyManager.*` | Depth Pyramid と深度系 Pass。 |
| `EditorGpuCullingManager.*` | Frustum / Occlusion / Indirect Args。 |
| `EditorTemporalRenderingManager.*` | Temporal 履歴、Scene / Game カメラ分離、TAA。 |
| `EditorPostProcessQualityManager.*` | Bloom、SMAA、AA、品質設定。 |
| `EditorPlanarReflectionManager.*` | 平面反射 Resource と反射面管理。 |
| `Assets/Shaders/` | 実際にコンパイル・接続される HLSL と未接続 HLSL の区別。 |

## 9. エディタ全体の調査項目

### 9.1 起動から終了まで

次を順番に調査する。

1. 起動時に自動作成される Scene、GameObject、Asset があるか。
2. 初期レイアウトに表示されるパネル。
3. Scene View と Game View の初期状態。
4. Play、Stop、終了要求の流れ。
5. Play 停止時に復元される値と復元されない値。
6. 起動失敗、Shader コンパイル失敗、DLL 読み込み失敗の表示先。

### 9.2 各ウィンドウ

次のウィンドウを個別ページ候補として調査する。

- ヒエラルキー。
- シーン。
- ゲーム。
- インスペクター。
- Project。
- Console。
- Project Settings または設定画面。
- Docking、分離、再配置。
- Spline Editor。
- Event Timeline。
- State Graph。
- Game Build Settings。

各ウィンドウについて、役割、開き方、選択方法、右クリック、ダブルクリック、Delete、ドラッグ＆ドロップ、検索、スクロール、保存への影響を記録する。

### 9.3 基本操作

次の操作について実装箇所と現在のショートカットを確認する。

- GameObject の作成、名前変更、有効化、無効化、削除、複製。
- 複数選択と範囲選択。
- 親子付けと親解除。
- Scene カメラの回転、平行移動、前後移動、フォーカス。
- 移動、回転、拡縮ギズモ。
- Local / World。
- Snap。
- Undo / Redo。
- Scene 保存、名前を付けて保存、読み込み、新規作成。
- Prefab 作成、配置、更新。
- Asset の作成、削除、フォルダー作成、ダブルクリック。

## 10. GameObject 調査テンプレート

GameObject の説明は次の項目を埋める。

```text
機能名:
概要:
作成場所:
初期状態:
名前の変更方法:
有効 / 無効の意味:
Tag の用途:
Layer の用途:
Static の用途:
親子関係の作り方:
Transform の継承範囲:
複製方法:
削除方法:
Scene 保存対象:
Prefab 保存対象:
Play 停止時の復元:
現在の制限:
根拠:
```

## 11. Component 全件調査

`EditorScene.h` の `EditorComponentType` を全件抽出し、1 種類につき 1 レコードを作る。追加メニューに表示されない型も省略しない。

現在の大分類として最低限、次を含める。

- 基本。
- 描画・レンダリング。
- カメラ。
- ライト・環境。
- 3D 物理。
- 2D 物理。
- アニメーション。
- オーディオ。
- UI。
- 入力・イベント。
- ゲームプレイ。
- ナビゲーション。
- AI。
- エフェクト。
- 地形・タイルマップ。
- 触覚。
- ポストプロセス。

### 11.1 Component レコードの必須項目

```text
内部型名:
日本語表示名:
カテゴリ:
実装状態:
概要:
使用場面:
追加場所:
追加手順:
必要な他 Component:
推奨する組み合わせ:
併用できない Component:
追加時の初期値:
Inspector 項目:
編集時の動作:
Play 開始時の動作:
毎フレームの動作:
FixedUpdate 時の動作:
停止時の動作:
Scene 保存:
Prefab 保存:
C++ Script API:
単位と有効範囲:
制限事項:
既知の不具合:
確認手順:
根拠:
```

### 11.2 Inspector 項目の記録形式

Inspector に表示される項目を、Component ごとに次の表へする。

| 表示名 | 内部フィールド | 型 | 初期値 | 単位 | 推奨範囲 | 値を変えた結果 | 保存 | Play 反映 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

表示されない内部フィールドは、使用者が直接触れない限り Inspector 表へ入れない。別欄に「内部固定値」として記録する。

### 11.3 Component 状態判定の手順

1. `EditorComponentType` に存在するか確認する。
2. `kComponentAddEntries` に日本語名とカテゴリがあるか確認する。
3. `Draw...Component` で編集項目を確認する。
4. `CreateComponent` で初期値を確認する。
5. `SaveScene` と `LoadScene` で保存対象を確認する。
6. 該当 Manager で Play 実処理を確認する。
7. C++ Script API から操作できるか確認する。
8. 実機確認が必要なら「未確認」とテスト構成を記録する。

## 12. 描画・Material・Shader の調査

### 12.1 Model Renderer と Material

次の項目を別々に調査する。

- Mesh Filter の Asset Path。
- Mesh Renderer の有効状態。
- Base Color と Base Color Texture。
- UV 確認画像と実描画 Texture の違い。
- Imported Material Texture の自動適用条件。
- Normal Map と Normal Scale。
- Metallic、Roughness、IOR、Alpha。
- Metallic / Roughness / AO / Emission / Height / Opacity Map。
- Emission Color と Emission Strength。
- Clear Coat、Transmission、Subsurface、Anisotropy。
- Specular Tint、Sheen。
- UV Tiling と UV Offset。
- Opaque、Mask、Transparent。
- Alpha Cutoff。
- Double Sided。
- Cast Shadow と Receive Shadow の有無。
- Reflection Strength。

各値について、Inspector の入力値が `EditorSceneSynchronizer` を通り、Constant Buffer または Material Data へ渡り、最終的にどの Shader で使われるか確認する。

緑色、固定光、Texture 未反映などの既知不具合がある場合は、再現条件と現在の修正状況を別記する。

### 12.2 Light

最低限、次を種類別に調査する。

- Sun / Directional。
- Point。
- Spot。
- Area。
- 色、強度、位置、方向、範囲、角度、面積。
- 複数 Light の上限。
- Light が 0 個の時の見え方。
- Shadow の有効化、解像度、Bias、Softness。
- Gizmo の形と操作。
- Scene View と Game View での差。
- Light の保存と複製。

### 12.3 Camera

- Perspective / Orthographic。
- FOV、Orthographic Size、Near、Far。
- Position / Rotation と Game View の同期。
- Scene Camera と Game Camera の分離。
- Exposure。
- DOF、Motion Blur の設定と実際の Pass。
- Culling Mask、Clear Color、Target Texture、Viewport の有無。
- 複数 Camera の選択方法と優先順位。
- Camera Gizmo と Frustum 表示。

### 12.4 PostProcess

エフェクトごとに、追加順、複数追加、折りたたみ、同種エフェクト重複、実行順を確認する。

最低限、次を個別機能として扱う。

- Tone Mapping。
- Bloom。
- Glare。
- Ghost。
- Streak。
- Fog Glow。
- Star。
- Sun Beam。
- Kernel。
- Box Sharpen。
- Diamond Sharpen。
- Laplace。
- Sobel。
- Prewitt。
- Kirsch。
- Shadow Filter。
- FXAA。
- SMAA。
- Temporal AA。
- Sharpen。
- SSAO / GTAO。
- Depth Of Field。
- Motion Blur。
- Vignette。
- Film Grain。
- Chromatic Aberration。
- Auto Exposure。

Shader ファイルが存在するだけでは「使用可能」としない。`EditorPlatformManager` でコンパイルされ、PSO が作成され、`EditorRenderManager` または Renderer Manager から実際に Dispatch / Draw され、Inspector の値が定数へ渡ることを確認する。

### 12.5 Reflection

- Screen Space Reflection。
- Planar Reflection。
- Reflection Probe / Cubemap。
- HDRI / IBL。
- Parallax Corrected Cubemap。
- 反射対象 Layer。
- 解像度、更新頻度、粗さ、強度。
- 複数反射面の扱い。
- Scene 外やカメラ外にある物体が映る方式か。
- 反射面自身の除外。
- 現在の既知不具合。

### 12.6 RenderTexture と中間 Buffer

使用者が設定できるものだけを中心に、次を確認する。

- Camera Target Texture の有無。
- HDR Render Target。
- Depth Texture。
- GBuffer。
- Shadow Map。
- Reflection Render Target。
- PostProcess 中間 Texture。
- Scene View と Game View で共有か分離か。

## 13. Asset と Import の調査

### 13.1 対応形式

コードで検出、表示、読み込み、配置できる拡張子を分けて記録する。

| 拡張子 | Project 表示 | ダブルクリック | Scene 配置 | Import 内容 | 制限 |
| --- | --- | --- | --- | --- | --- |

候補には少なくとも次を含める。

- `.fbx`
- `.obj`
- `.mtl`
- `.png`
- `.jpg` / `.jpeg`
- `.wav`
- `.scene`
- `.prefab`
- `.inputactions`
- `.cpp` / `.h`
- `.dll`
- `.py`
- AI 設定用 `.json` / `.xml`

### 13.2 FBX / OBJ

次を実コードで確認する。

- 使用する SDK または Loader。
- 複数 Mesh。
- Node 階層。
- 複数 Material。
- Texture Path。
- UV、Normal、Tangent。
- Smooth / Hard Edge。
- Import Scale。
- 軸変換と単位変換。
- Bone、Skin Weight。
- Animation Clip。
- Blend Shape。
- Collision Mesh。
- 描画 Mesh と当たり判定 Mesh の分離。
- meshoptimizer の適用箇所。
- ファイル名で形状を推測する処理が残っているか。

### 13.3 Texture

- 対応形式。
- sRGB と Linear の扱い。
- Base Color 用画像の設定場所。
- UV 確認画像の設定場所。
- Normal / Metallic / Roughness など各 Map の設定場所。
- 相対 Path と絶対 Path。
- Asset 移動、削除、参照切れ時の動作。
- Reload 方法。

## 14. Scene と Prefab の調査

### 14.1 Scene

- 新規 Scene。
- 通常保存。
- 名前を付けて保存。
- 開く。
- 最近使った Scene の有無。
- 保存先と拡張子。
- GameObject、Component、親子関係の保存。
- Material、Physics、AI、Input、Script 公開変数の保存。
- 壊れた Scene の読み込み結果。
- Play 中の保存可否。
- Stop 後の復元範囲。

### 14.2 Prefab

- Prefab 作成手順。
- Project への表示。
- Scene への配置。
- 元 Prefab と Instance の関係。
- Apply / Revert の有無。
- Override の保存。
- Nested Prefab の有無。
- Script / Asset 参照の保存。
- 元ファイル削除時の動作。

## 15. 3D 物理の調査

Jolt Physics を使っていることだけで機能を断定せず、CG2 から設定・実行できる範囲を確認する。

### 15.1 Rigidbody

- Dynamic / Static / Kinematic の決定方法。
- Mass。
- Use Gravity。
- Velocity / Angular Velocity。
- Linear Drag / Angular Drag。
- Interpolation。
- Collision Detection。
- Freeze Position / Rotation。
- Sleep / Wake。
- AddForce / AddImpulse / AddTorque / SetVelocity。
- Transform と Jolt Body の同期方向。

### 15.2 Collider

- Box、Sphere、Capsule、Mesh、Terrain、Wheel。
- Center、Size、Radius、Height、Direction。
- Is Trigger。
- Physics Material。
- Layer。
- Static / Dynamic Mesh Collider の制限。
- Convex / Triangle Mesh の選択。
- モデル Scale と Collider Size の一致。
- Scene View Debug 表示。

### 15.3 問い合わせとイベント

- Raycast。
- SphereCast。
- CapsuleCast。
- OverlapSphere。
- OverlapBox。
- Collision Enter / Stay / Exit。
- Trigger Enter / Stay / Exit。
- Hit 情報の GameObject ID、位置、法線、距離、相対速度。
- C++ Script へ通知されるタイミング。

### 15.4 Character と Joint

- Character Controller の移動、重力、接地、Slope、Step。
- Constant Force。
- Hinge、Fixed、Spring、Configurable、Character Joint。
- Joint の相手指定方法。
- Play 停止後の復元。

## 16. 2D 物理の調査

2D 物理は 3D 物理と別に、追加、Inspector、保存、Play 実処理を確認する。3D Jolt 実装が存在することを理由に 2D 物理を「使用可能」としてはいけない。

Rigidbody2D、各 Collider2D、Joint2D、Effector2D を全件調査し、Play 実処理がなければ「設定画面のみ」とする。

## 17. Input System の調査

### 17.1 Asset と Project 設定

- Input Actions Asset の作成場所。
- 保存先と拡張子。
- Action Map の追加、削除、名前変更。
- Action の追加、削除、名前変更。
- Binding Path の選択方法。
- Button / Vector2 の対応。
- Project 全体で使用する Asset の登録場所。
- Scene / GameObject ごとの PlayerInput 参照。

### 17.2 PlayerInput

- Actions。
- Default Map。
- Behavior。
- Action Event と C++ 関数名の対応。
- Map の Enable / Disable。
- started / performed / canceled。
- 押しっぱなしと押した瞬間の違い。
- Keyboard、Mouse、Gamepad の現在対応範囲。

### 17.3 説明用の完成例

W キーで前進する例は、次を途中で省略せず記載する。

1. Input Actions Asset を作る。
2. `Player` Action Map を作る。
3. `Move` または `Forward` Action を作る。
4. `Keyboard/W` を割り当てる。
5. Project 設定または PlayerInput へ Asset を設定する。
6. GameObject に PlayerInput と C++ Script を追加する。
7. C++ 側で Action を取得または Bind する。
8. Update / FixedUpdate で Transform、Velocity、Force のいずれを変えるか説明する。
9. Play して Console と移動を確認する。

## 18. C++ スクリプトの調査

### 18.1 作成から実行まで

次の一連の流れを実際のボタン名、ファイル名、出力先で説明する。

1. GameObject を選択する。
2. Script または MonoBehaviour 相当 Component を追加する。
3. C++ クラス名を入力する。
4. テンプレートを生成する。
5. `.h` と `.cpp` を開く。
6. Debug / Release DLL をビルドする。
7. DLL Path を Component へ設定する。
8. Play 時に DLL を読み込む。
9. Hot Reload または再読み込みを行う。
10. Console で Load 成功、失敗を確認する。

ビルド構成、Visual Studio のバージョン、x64、Runtime Library、API Version 不一致、古い DLL のロック、出力 Path の違いをトラブルシューティングへ含める。

### 18.2 DLL ライフサイクル

DLL ABIはEngineが`.Generated.cpp`へ自動生成する内部実装として調査する。使用者向け手順やコード例にexport関数を記載せず、ユーザー `.cpp` へ手書きさせない。

- ユーザー側: `Script`を継承し、必要な`Start()`、`Update(float)`、`FixedUpdate(float)`、Collision / Trigger、`Stop()`だけを書く。
- Engine側: DLL読込、Instance生成・破棄、ライフサイクル、Physics / Animation Event、Field、ActionをGeneratedコードから転送する。
- 所有Object: `GetGameObject()`または`GetComponent<T>()`から辿り、毎回`gameObjectId`を引数で受け取らない。

各関数について、呼ばれる回数、タイミング、引数、保持してよい状態、してはいけない処理、失敗時の動作を確認する。

### 18.3 Runtime API

`Source/Engine/Core/EditorScriptApi.h` の `EditorScriptRuntimeApi` を基準に、関数ポインタを全件抽出する。

API ごとに次を記録する。

```text
API 名:
所属:
目的:
呼び出せるライフサイクル:
必要 Component:
引数:
戻り値:
単位:
座標系:
副作用:
失敗条件:
失敗時の戻り値:
最小コード例:
実用コード例:
関連 API:
根拠:
```

最低限、次の API 群を分類する。

- Log。
- Transform 取得・設定。
- Velocity / Angular Velocity 取得・設定。
- AddForce / AddImpulse / AddTorque。
- Key 入力。
- Input Action Button / Vector2。
- Action Bind / Unbind。
- Raycast / Cast / Overlap。
- Physics Event。
- GameObject 検索、名前、Tag、Layer。
- Component 存在確認。
- Material 取得。
- Animation 取得・操作。
- AI Sensor 結果取得。
- 公開変数登録。

### 18.4 公開変数と Inspector

- 対応型: bool、int、float、Vector2、Vector3、string など。
- 表示名。
- 初期値。
- Min / Max / Step。
- DLL Reload 後の値保持。
- Scene / Prefab 保存。
- 同名変数の扱い。
- 型変更時の扱い。

### 18.5 C++ コード例

サイトには、巨大な万能テンプレートを載せない。最小テンプレートと目的別サンプルを分ける。

目的別サンプル候補は次とする。

- W キーで Transform を前進。
- Input Action の Vector2 で移動。
- Rigidbody に Force を加える。
- Torque で球を転がす。
- Jump Action で Impulse。
- Collision Enter で Console 出力。
- Trigger Enter でアイテムを取得。
- Raycast で前方を調べる。
- Material の色を変更する。
- Animation を再生する。
- 視界 Sensor で対象へ向く。
- 音声認識文字列を比較する。

## 19. Audio の調査

- AudioSource の Asset、再生、Loop、Volume、Pitch、Spatial。
- AudioListener の選択方法。
- WAV の読み込み範囲。
- 同時再生数。
- 3D 距離減衰。
- Reverb Zone。
- LowPass、HighPass、Echo、Distortion、Reverb、Chorus。
- 各 Filter が Inspector 表示だけか、XAudio2 の実処理へ接続されるか。
- Play / Stop / Scene Load 時の停止と再開。
- C++ Script API の有無。

## 20. Animation の調査

- FBX Animation Clip の読み込み。
- Animation Component の Clip 選択、再生、速度、Loop。
- Animator State、Transition、Parameter。
- Playable Director。
- Skinned Mesh、Bone、Skinning。
- GPU Skinning。
- Root Motion。
- Animation Event。
- Parent / Position / Rotation / Scale / Aim / LookAt Constraint。
- 編集時プレビューと Play 実行。
- C++ Script API。

## 21. UI の調査

Canvas、RectTransform、Image、RawImage、Text、TextMeshPro、Button、Toggle、Slider、Scrollbar、Dropdown、InputField、ScrollRect、Mask、Layout Group を全件調査する。

各 UI Component について、Game View に描画されるか、Mouse / Keyboard / Gamepad Event が動くか、EventSystem が必要か、Anchor / Pivot / Scale が保存されるかを確認する。

追加と Inspector 表示だけの場合は、明確に「設定画面のみ」とする。

## 22. Navigation と AI の調査

### 22.1 Navigation

- NavMesh Surface の Build 方法。
- Mesh 障害物を形状通りに扱うか、AABB 近似か。
- Agent の目的地指定方法。
- Obstacle、Modifier、Modifier Volume、Link。
- Recast Navigation の使用範囲。
- Play 中の再構築と Dynamic Obstacle。
- Scene Debug 表示。

### 22.2 AI

次を方式別に分ける。

- Behavior Tree。
- State Machine。
- GOAP。
- HTN。
- Pathfinding。
- Recast Crowd。
- Steering。
- Vision Sensor。
- OpenCV Camera / Object Detection / Color Tracking / Motion Detection。
- Whisper Speech Recognition / Voice Command。

AI Component ごとに、必要な外部ライブラリ、必要ファイル、Python 使用の有無、起動方法、入力、戻り値、更新頻度、失敗時の状態を記録する。

Sensor の戻り値を一つの曖昧な説明にまとめない。種類ごとに実際に有効なフィールドを分ける。

例:

| Sensor | 主な入力 | 主な戻り値 | 使用例 |
| --- | --- | --- | --- |
| Vision | GameObject、Range、Angle | detectedGameObjectId、distance、direction | 追跡開始。 |
| Object Detection | Camera Image | label、confidence、bounds | person の検出。 |
| Motion | 画像または対象位置 | motion、motionMagnitude | 侵入検知。 |
| Whisper | 音声 | text、confidence | 認識文字列の比較。 |
| Voice Command | 音声と Command 定義 | command、commandId | Move / Stop 分岐。 |

## 23. Effect、Terrain、Tilemap、触覚

次の Component は、追加 UI だけか実処理があるかを必ず分ける。

- ParticleSystem。
- VisualEffect。
- LensFlare。
- Projector。
- DecalProjector。
- Terrain。
- Tilemap / TilemapRenderer / Grid。
- HapticSource / FeelKitHaptics。

Particle は Emission、Shape、Lifetime、Velocity、Color、Size、Collision、Trail、Material を項目別に確認する。

Terrain は Height、Brush、Texture、Tree、Grass、LOD、Collider、NavMesh を項目別に確認する。

触覚は、接続デバイス、Effect Asset、Strength、Duration、Loop、Play 条件、停止条件、外部ライブラリの初期化を確認する。

## 24. Console とデバッグの調査

- Info / Warning / Error の分類。
- Clear、Collapse、Filter。
- 最大保持件数。
- 物理 Event の連続出力制御。
- C++ Script Build ログ。
- DLL Load / Reload ログ。
- Shader Compile ログ。
- Stack Trace。
- ログをクリックした時の Source 移動。
- Collider、Light、Camera、NavMesh、Raycast、Reflection の Debug 表示。

## 25. Build と配布の調査

- CG2 Editor 自体の Debug / Release Build。
- Visual Studio 2022 / 2026 の違い。
- x64 のみか。
- 必要 DLL と Asset Folder。
- DirectXTex などの実行時 Path。
- C++ Script DLL Build。
- ゲーム単体 EXE の Build 機能の有無。
- 配布時に必要な `resources`、`Assets`、`ThirdParty`。
- 別 PC で必要な Runtime。

## 26. チュートリアルとして必ず用意する作例

調査結果から、次のチュートリアルを作れる情報を揃える。

1. 空の Scene を作り保存する。
2. Cube を配置し、移動、回転、拡縮する。
3. FBX を Project へ置き Scene に配置する。
4. Base Color Texture と Material 値を設定する。
5. Camera と Light を置いて Game View に表示する。
6. Rigidbody と箱の当たり判定で箱を落とす。
7. Physics Material で摩擦と反発を変える。
8. Input Actions Asset を作り W キーで前進する。
9. C++ Script を作成、Build、Attach、Play する。
10. Collision / Trigger を C++ で受け取る。
11. Prefab を作り複数配置する。
12. AudioSource で WAV を再生する。
13. PostProcess を追加し Bloom と AA を設定する。
14. NavMesh を作り Agent を目的地へ移動させる。

各チュートリアルは、開始状態、必要 Asset、操作手順、設定値、完成状態、失敗時の確認順を含める。

## 27. トラブルシューティングとして必ず調査する現象

- エディタが起動直後に終了する。
- ImGui を含め画面全体が白い。
- Scene View が黒い。
- Game View に何も映らない。
- Model が表示されない。
- Model がカメラ端で突然消える。
- Model が緑色になる。
- Light がないのに光って見える。
- Texture が反映されない。
- UV がずれる。
- FBX の Scale が大きすぎる。
- Collider と見た目が一致しない。
- Dynamic Mesh Collider が貫通する。
- Rigidbody が落下しない。
- Trigger / Collision が通知されない。
- Input Actions Asset が未設定と出る。
- W キーを押しても C++ Script が動かない。
- C++ Script DLL の読み込みに失敗する。
- DLL を再 Build しても変更が反映されない。
- API Version 不一致で Load に失敗する。
- Scene が保存されない。
- Scene を読み込んでも別の状態になる。
- Prefab の変更が反映されない。
- Play 停止後に Transform が戻らない。
- Audio が再生されない。
- PostProcess の値を変えても見た目が変わらない。
- Reflection が半透明に見える、位置や拡大率がずれる。
- Grid や UI がフレームごとに増殖する。

各項目には、原因候補だけでなく確認順を付ける。

```text
1. 必要 Component が付いているか。
2. Component と GameObject が有効か。
3. Asset Path が存在するか。
4. Console に Warning / Error がないか。
5. Play が必要な機能か。
6. Scene 保存後に値が再読み込みされているか。
7. 実装上の既知制限に該当しないか。
```

## 28. 調査結果の成果物構成

調査担当は、最終的に次の Markdown を作る。1 ファイルが大きくなりすぎる場合は分割する。

```text
docs/work-source/
├─ 00-status-and-evidence.md
├─ 01-getting-started.md
├─ 02-editor-windows-and-controls.md
├─ 03-gameobject-scene-prefab.md
├─ 04-assets-and-import.md
├─ 05-components-basic-rendering.md
├─ 06-components-physics.md
├─ 07-components-input-script.md
├─ 08-components-audio-animation-ui.md
├─ 09-components-navigation-ai.md
├─ 10-components-effects-terrain-haptics.md
├─ 11-rendering-material-postprocess.md
├─ 12-cpp-script-api.md
├─ 13-tutorials.md
├─ 14-troubleshooting.md
├─ 15-limitations-and-known-issues.md
└─ component-inventory.tsv
```

### 28.1 `component-inventory.tsv`

最低限、次の列を持つ。

```text
内部型名
日本語名
カテゴリ
追加可能
Inspector
保存
Play実処理
Script API
状態
根拠ファイル
確認メモ
```

### 28.2 `00-status-and-evidence.md`

全機能の実装状態と根拠を一覧にする。ChatGPT Work はこのファイルを事実確認の基準として使う。

## 29. ChatGPT Work へ渡す指示文

調査結果と一緒に、次の指示を渡す。

```text
あなたは CG2 自作ゲームエンジンの使用者向けドキュメントサイトを構成します。

添付した調査結果を唯一の事実基準として使用してください。
Unity や Unreal の一般仕様から CG2 の機能を推測しないでください。
「コンポーネントを追加できる」と「Play 中に実際に動く」を区別してください。
未確認、未実装、既知の不具合を隠さないでください。

対象読者は、CG2 の内部コードを知らず、エディタから GameObject、Component、Asset、C++ Script を使ってゲームを作る人です。

各機能ページには、概要、使用場面、追加場所、手順、必要条件、Inspector 項目、初期値、単位、Play 時の動作、保存、関連 Component、C++ API、コード例、制限、トラブルシューティングを含めてください。

内部実装の説明は、使用方法や制限の理解に必要な範囲だけにしてください。
画面名と Component 名は、実際の日本語 UI 表記を優先してください。
コード例は C++ で作成してください。
巨大な万能コードではなく、目的別の最小サンプルへ分けてください。

既存資料と調査結果が矛盾する場合は、根拠コードが付いた新しい調査結果を優先してください。
情報が不足している箇所は創作せず、「追加調査が必要」と明記してください。
```

## 30. サイト構成案

```text
はじめに
├─ CG2 でできること
├─ 必要環境
├─ インストールと起動
└─ 最初の Scene

エディタ
├─ 画面構成
├─ ヒエラルキー
├─ シーン
├─ ゲーム
├─ インスペクター
├─ Project
├─ Console
└─ 基本操作とショートカット

制作
├─ GameObject
├─ Scene
├─ Prefab
├─ Asset Import
├─ Material
├─ Light
├─ Camera
└─ PostProcess

コンポーネント
├─ 基本
├─ 描画
├─ 3D 物理
├─ 2D 物理
├─ 入力
├─ Audio
├─ Animation
├─ UI
├─ Navigation
├─ AI
├─ Effect
├─ Terrain / Tilemap
└─ Haptics

C++ スクリプト
├─ 作成と Build
├─ ライフサイクル
├─ 公開変数
├─ Input Action
├─ Physics Event
├─ Runtime API
└─ サンプル

実践
├─ チュートリアル
├─ トラブルシューティング
├─ 既知の不具合
└─ 未実装・制限事項
```

## 31. 完了条件

調査は、次をすべて満たした時に完了とする。

- `EditorComponentType` の全型が Component Inventory に存在する。
- すべての Component に追加、Inspector、保存、Play、API の判定がある。
- Inspector に見える全項目に意味、初期値、単位、Play 反映の説明がある。
- UI に表示される全メニュー、主要ボタン、ショートカットが記録されている。
- Scene、Prefab、Asset、Play / Stop の一連の手順が説明できる。
- `EditorScriptRuntimeApi` の全公開 API に引数、戻り値、使用条件、コード例がある。
- Input Actions Asset 作成から C++ 関数実行までを途中で省略せず説明できる。
- 3D 物理の基本作例を再現できる情報がある。
- 描画機能は Shader の存在ではなく、実際の接続まで判定されている。
- Audio、Animation、UI、Navigation、AI は「表示のみ」と「実動作」が区別されている。
- 既知の不具合と未確認事項が一覧化されている。
- 各ページの事実に根拠ファイルまたは実機確認記録が付いている。
- ChatGPT Work が追加のコード推測をせず、調査結果だけでサイト本文を構成できる。

## 32. 調査時の禁止事項

- enum 名だけを見て「実装済み」と書かない。
- Inspector があるだけで「使用可能」と書かない。
- Shader ファイルがあるだけで描画 Pass が接続済みと書かない。
- 外部ライブラリが配置されているだけで使用中と書かない。
- Unity と同名であることを理由に Unity と同じ挙動と書かない。
- README の古い説明を無条件に転載しない。
- 使用者が触れない private 関数を公開 API として掲載しない。
- C++ ではなく C# の例を掲載しない。
- すべての API を一つの巨大なテンプレートへ詰め込まない。
- 未確認の初期値、単位、範囲を創作しない。
- 既知の不具合を正常仕様として説明しない。

## 33. 最終確認用チェックシート

```text
[ ] 全 Component を抽出した
[ ] 日本語表示名とカテゴリを照合した
[ ] 全 Inspector 項目を抽出した
[ ] 初期値を CreateComponent で確認した
[ ] Scene 保存と読み込みを確認した
[ ] Prefab 保存と生成を確認した
[ ] Play Manager への接続を確認した
[ ] C++ Script API を全件抽出した
[ ] Asset 拡張子と Import 範囲を確認した
[ ] Shader のコンパイルと描画接続を区別した
[ ] Scene View と Game View の差を確認した
[ ] 物理、入力、Audio、Animation、UI、Navigation、AI を個別確認した
[ ] Tutorial 用の完成手順を用意した
[ ] Troubleshooting の確認順を用意した
[ ] 未確認と未実装を明記した
[ ] 根拠ファイルと関数名を付けた
[ ] ChatGPT Work 用の入力資料へ分割した
```

## 34. パラメーター説明の必須品質

使用者向けドキュメントでは、パラメーター名を一覧にするだけでは不十分である。すべての Inspector パラメーターについて、最低限次の情報を調査して記録する。

| 項目 | 必ず調べる内容 |
| --- | --- |
| 表示名 | Inspector に実際に表示される日本語名。 |
| 内部名 | `EditorComponent` などにある C++ フィールド名。 |
| 所属 | どの Component、設定画面、Asset に属するか。 |
| 型 | bool、int32_t、float、Vector2、Vector3、string、enum、Asset Path。 |
| 操作 UI | Checkbox、DragFloat、InputText、Combo、Color、Asset Picker など。 |
| 初期値 | Component 追加直後に入る値。 |
| 最小値 | Inspector または実処理で Clamp される最小値。 |
| 最大値 | Inspector または実処理で Clamp される最大値。 |
| Step | ドラッグ操作 1 単位でどれだけ変化するか。 |
| 単位 | 度、ラジアン、秒、ミリ秒、距離、個/秒、0～1、EV など。 |
| 座標系 | World、Local、Screen Pixel、Screen UV、Object Local。 |
| 値 0 の意味 | 無効、最小、完全停止など、0 が特別な意味を持つか。 |
| 値 1 の意味 | 等倍、完全適用、100% など、1 が基準値か。 |
| 値を増やす結果 | 見た目、速度、安定性、負荷がどう変わるか。 |
| 値を減らす結果 | 0 に近づけた時の挙動を含む。 |
| 必要条件 | 他 Component、Asset、Play、Light、Camera、Collider など。 |
| 競合条件 | 他設定に上書きされるか、同時使用できないか。 |
| 反映タイミング | 即時、次フレーム、Play 開始、FixedUpdate、再読み込み、再 Build。 |
| 保存 | Scene、Prefab、Project Settings のどこへ保存されるか。 |
| 復元 | Play 停止時、Scene 再読み込み時に戻るか。 |
| 負荷 | 値を上げると CPU、GPU、Memory 負荷が増えるか。 |
| 制限 | 現在未接続、近似、未対応の組み合わせ。 |
| 確認方法 | 使用者が変化を目で確認できる最小の手順。 |

### 34.1 悪い説明と必要な説明

悪い例:

```text
粗さ: 表面の粗さを設定します。
```

必要な例:

```text
粗さ
- 所属: メッシュレンダラー > マテリアル
- 内部名: roughness
- 範囲: 0.0～1.0
- 初期値: CreateComponent で確認する
- 0.0: 鏡面反射が鋭くなります。反射元がない場合は暗く見えることがあります。
- 1.0: 反射が広くぼけ、拡散面に近づきます。
- 必要条件: MeshRenderer と描画可能な Mesh。環境反射を見る場合は Environment、Reflection Probe、SSR などの反射元が必要です。
- Texture: Roughness Map が設定されている場合、数値は Map へ乗算されるか上書きされるかを Shader で確認します。
- 反映: Scene View / Game View の次回描画から反映されるか確認します。
- 保存: Scene / Prefab の両方で値が維持されるか確認します。
- 確認: 球へ Light と Environment を置き、粗さを 0、0.5、1 の順に変更してハイライト幅を比較します。
```

## 35. 単位・座標・色の説明規則

### 35.1 Transform

- Inspector の回転表示が度でも、`EditorGameObject::rotate` はラジアンで保持される。使用者向け説明は度を優先し、C++ API の `EditorScriptTransform::rotation` が度かラジアンかを実処理で確認して別記する。
- Position は World か Local かを、親 GameObject がある場合とない場合で確認する。
- Scale は 1.0 が等倍である。負 Scale、0 Scale、非一様 Scale の扱いも確認する。
- Scene Gizmo の Local / World は Transform の保存方式とは別の操作モードなので混同しない。

### 35.2 時間

- `deltaTime` と `fixedDeltaTime` は秒単位として扱われているか確認する。
- ミリ秒を使う項目は、`hapticDurationMs` のように明示する。
- Timer が Frame 数か秒かを必ず区別する。

### 35.3 色

- UI が 0～255 で表示し、内部が 0.0～1.0 の場合は変換を説明する。
- Base Color、Light Color、Emission Color、Background Color を別物として説明する。
- Texture の sRGB / Linear を確認し、Base Color と Normal / Metallic / Roughness で扱いが異なる場合は明記する。
- Alpha は色の Alpha、Material の Alpha、Opacity Map、Alpha Mode、Alpha Cutoff を分ける。

### 35.4 方向と角度

- Light、Joint Axis、Aim Axis、Camera Forward がどの軸を前方とするか確認する。
- Inspector が度、内部保存がラジアン、Shader が Cos 値の場合は、それぞれの境界を説明する。
- Screen Position は Pixel か 0～1 UV かを確認する。

### 35.5 物理量

- Mass を kg、Force を N と断定する場合は Jolt と CG2 の Scale が現実単位前提であることを確認する。確認できない場合は「エンジン内の質量単位」「力の大きさ」と書く。
- Velocity は 1 秒あたりの World 移動量か確認する。
- Angular Velocity と Torque の軸、単位、FixedUpdate 推奨を説明する。

## 36. Transform パラメーター調査票

| 表示項目 | 内部データ | 調査する意味 | 必須確認 |
| --- | --- | --- | --- |
| 位置 X/Y/Z | `EditorGameObject::translate` | GameObject の位置。 | World / Local、親あり、ギズモ、保存、Script SetTransform。 |
| 回転 X/Y/Z | `EditorGameObject::rotate` | 各軸の回転。 | UI は度か、内部はラジアン、回転順、親の影響。 |
| スケール X/Y/Z | `EditorGameObject::scale` | Mesh と子の拡縮。 | 1=等倍、0、負数、Collider と Light Gizmo への影響。 |
| Local / World | Gizmo 状態 | Gizmo の軸方向。 | Transform 値そのものの保存には影響しないか。 |
| Snap | Gizmo 状態 | 一定間隔での操作。 | 移動、回転、拡縮ごとの間隔と ON/OFF 方法。 |

Transform の説明には、数値入力、ドラッグ編集、ギズモ操作の 3 通りを記載する。数値入力後に Scene Camera が初期化されないことも確認対象にする。

## 37. Material パラメーター詳細調査票

### 37.1 Texture Slot

| 表示候補 | 内部名 | 使用目的 | 調査・説明する内容 |
| --- | --- | --- | --- |
| ベースカラー画像 | `textureAssetPath` | 表面の色模様。 | 設定ボタン、対応形式、sRGB、FBX 自動画像との優先順位、未設定時の白 Texture。 |
| UV 確認画像 | `uvLayoutTextureAssetPath` | UV 配置の確認。 | 実描画 Base Color と別であること、初期 Checker が実 Texture へ混ざらないこと。 |
| 法線画像 | `normalTextureAssetPath` | 表面の細かな凹凸。 | Linear 読み込み、Tangent 必須、Normal Scale、Y 方向規約。 |
| メタリック画像 | `metallicTextureAssetPath` | 部位ごとの金属度。 | 使用 Channel、数値 Metallic との合成方法。 |
| 粗さ画像 | `roughnessTextureAssetPath` | 部位ごとの粗さ。 | Roughness / Smoothness 反転の有無、使用 Channel。 |
| AO 画像 | `ambientOcclusionTextureAssetPath` | 間接光の遮蔽。 | 使用 Channel、AO Strength、直接光へ掛けないか。 |
| 放射画像 | `emissionTextureAssetPath` | 自発光模様。 | Emission Color / Strength との積、Bloom との関係。 |
| 高さ画像 | `heightTextureAssetPath` | 視差または高さ。 | Height Scale、UV Offset、実際に Parallax へ接続されるか。 |
| 不透明度画像 | `opacityTextureAssetPath` | 部位ごとの透明度。 | Alpha Mode、Alpha Cutoff、Blend、Depth Write。 |

### 37.2 数値 Material

| パラメーター | 内部名 | 使用者へ説明する要点 | 代表確認値 |
| --- | --- | --- | --- |
| 色 | `color` | Texture へ乗算される色か、Texture 未設定時の色か。白でも緑になる場合の確認先。 | 白、赤、中間灰。 |
| 強さ | `intensity` | Base Color の明るさか Light 強度かを Component ごとに分ける。 | 0、1、2。 |
| メタリック | `metallic` | 0=非金属、1=金属。金属では Base Color が反射色になるか。 | 0、0.5、1。 |
| 粗さ | `roughness` | 0=鋭い反射、1=ぼけた反射。 | 0、0.25、0.5、1。 |
| 屈折率 | `ior` | Fresnel と屈折にどう使われるか。Transmission が 0 でも影響するか。 | 1.0、1.33、1.5、2.4。 |
| アルファ | `alpha` | Alpha Mode ごとの意味。 | 0、0.5、1。 |
| 反射 | `reflectionStrength` | SSR、Probe、IBL、Planar のどこへ掛かるか。 | 0、0.5、1。 |
| 放射 | `emissionStrength` | Light を照らすのか、見た目だけか、Bloom が必要か。 | 0、1、5。 |
| 放射色 | `emissionColor` | 放射 Texture と合成される色。 | 白、赤、青。 |
| 法線強度 | `normalScale` | Normal Map の凹凸倍率。 | 0、1、2。 |
| AO 強度 | `ambientOcclusionStrength` | AO Map の寄与。 | 0、0.5、1。 |
| 高さ | `heightScale` | Parallax の深さと破綻範囲。 | 0、0.01、0.05。 |
| Alpha Cutoff | `alphaCutoff` | Mask で破棄する境界。 | 0.1、0.5、0.9。 |
| クリアコート | `clearCoat` | 塗装上の透明反射層。 | 0、0.5、1。 |
| コート粗さ | `clearCoatRoughness` | Clear Coat の反射幅。 | 0、0.5、1。 |
| 透過 | `transmission` | ガラス、水、薄い物体の透過。屈折 RenderTexture の有無。 | 0、0.5、1。 |
| 表面下散乱 | `subsurface` | 皮膚や蝋の光回り込み。近似か本実装か。 | 0、0.5、1。 |
| 異方性 | `anisotropy` | 金属ブラシや髪の方向性反射。Tangent が必要か。 | -1、0、1。 |
| 異方性回転 | `anisotropyRotation` | 接線方向の回転。度か正規化値か。 | 0、0.25、0.5、1。 |
| 鏡面色 | `specularTint` | Dielectric の Specular へ Base Color を混ぜる割合。 | 0、0.5、1。 |
| Sheen | `sheen` | 布の縁反射。 | 0、0.5、1。 |
| Sheen 色 | `sheenTint` | Sheen の色付け。 | 0、0.5、1。 |
| Alpha Mode | `alphaMode` | Opaque / Mask / Transparent。 | 3 モードすべて。 |
| 両面 | `doubleSided` | Back Face を描くか。法線反転と影も確認する。 | OFF / ON。 |
| UV 繰り返し | `uvTiling` | Texture の繰り返し数。 | 1,1 / 2,2 / 0.5,0.5。 |
| UV ずらし | `uvOffset` | Texture の開始位置。 | 0,0 / 0.5,0 / 0,0.5。 |

### 37.3 Material の親切な使用手順

ドキュメントには最低でも次の 3 例を載せられる情報を揃える。

1. 白い不透明 Material: Color 白、Metallic 0、Roughness 0.5、Alpha 1、Opaque。
2. 金属 Material: Environment または Reflection Source を用意し、Metallic 1、Roughness 0.2、Reflection 1。
3. 発光 Material: Emission Color、Emission Strength、Bloom を順番に設定し、Light を照らす機能とは別であることを説明する。

## 38. Light パラメーター詳細調査票

| 表示項目 | 保存先候補 | 説明する内容 | 確認方法 |
| --- | --- | --- | --- |
| 種類 | Component の型・共用フィールド | Sun / Point / Spot / Area の違い。 | 同じ位置、色、強度で比較。 |
| 色 | `color` | RGB と色温度の有無。 | 白、赤、緑。 |
| 強さ | `intensity` | Light 種類ごとの単位、減衰前の強さ。 | 0、1、10、100。 |
| 位置 | GameObject Transform | Point / Spot / Area の光源位置。 | Gizmo と照射位置を比較。 |
| 回転 | GameObject Transform | Sun / Spot / Area の方向。 | 90 度ずつ回転。 |
| 半径・距離 | `colliderRadius` | Point / Spot の影響距離。 | 距離外で光が消えるか。 |
| 内側角度 | `colliderSize.x` | Spot の完全照射角。 | Cone Gizmo と境界を比較。 |
| 外側角度 | `colliderSize.y` | Spot の減衰終了角。 | Inner より大きい必要があるか。 |
| 面積・広がり | `colliderSize.z` 等 | Area Light のサイズまたは Softness。 | 影の柔らかさと範囲を比較。 |
| 影 | Shadow 関連設定 | Cast、Resolution、Bias、Softness、更新。 | 床と Cube で確認。 |

Light が 0 個の場合に固定色の仮 Light が入らないこと、Environment が 0 の場合に Model 固定方向から光らないことを確認項目へ含める。

## 39. Rigidbody と Physics Material 詳細調査票

### 39.1 Rigidbody

| パラメーター | 内部名 | 説明する内容 | Inspector 範囲候補 |
| --- | --- | --- | --- |
| 質量 | `mass` | 加速、Impulse、衝突時の動きへの影響。重力加速度そのものは質量で変わるか。 | 0.01～100。 |
| Colliderから質量を計算 | `automaticMassFromCollider` | Jolt ShapeのMass Propertiesを既定密度1000 kg/m3で体積へ戻し、実質密度を掛けて質量と慣性を決める。 | OFF / ON。 |
| 実質密度 | `bodyDensity` | 中空、積荷、Ballastを含む物体全体の質量を排水外形体積で割った密度。材質単体密度と混同しない。 | 0.01以上。 |
| 線形減衰 | `drag` | 移動速度を止める抵抗。 | 0～20。 |
| 角度減衰 | `angularDrag` | 回転速度を止める抵抗。 | Inspector から範囲を抽出。 |
| 重力を使用 | `useGravity` | Scene Physics Gravity を使うか。 | OFF / ON。 |
| キネマティック | `isKinematic` | 物理 Force で動かず、Transform 操作で動かすか。 | OFF / ON。 |
| 速度 | `velocity` | World の移動速度。 | X/Y/Z。 |
| 角速度 | `angularVelocity` | 回転軸と速さ。 | X/Y/Z。 |
| 補間 | `interpolationMode` | なし / 補間 / 外挿の見た目と遅延。 | 3 モード。 |
| 衝突検出 | `collisionDetectionMode` | 離散 / 連続。高速物体のすり抜けと負荷。 | 2 モード。 |
| 位置固定 | `freezePositionX/Y/Z` | 指定 World 軸の移動を止める。 | 軸ごと。 |
| 回転固定 | `freezeRotationX/Y/Z` | 指定 World 軸または Local 軸の回転を止める。 | 軸ごと。 |

説明には「Rigidbody だけでは形がないため Collider が必要」「Play 中に Transform を毎フレーム上書きすると物理結果と競合する可能性」を含める。

### 39.2 Physics Material

| パラメーター | 内部名 | 意味 | 確認例 |
| --- | --- | --- | --- |
| 動摩擦 | `dynamicFriction` | 滑っている時の摩擦。 | 0 の氷、0.6 の床。 |
| 静止摩擦 | `staticFriction` | 止まっている時、坂で滑り始めるまでの摩擦。 | 斜面で比較。 |
| 弾力性 | `bounciness` | 衝突後の跳ね返り。 | 0、0.5、1。 |
| 摩擦の合成 | `frictionCombineMode` | 平均 / 最小 / 最大 / 乗算。接触する 2 物体の値の合成。 | 片方を氷、片方を床。 |
| 弾力性の合成 | `bouncinessCombineMode` | 2 物体の反発値の合成。 | 床と球の値を変える。 |
| 接触イベント | `generateContactEvents` | Enter / Stay / Exit を生成するか。 | Console / Script Event。 |
| 物理レイヤー | `physicsLayer` | Layer Collision Matrix の行列。 | Player と Projectile。 |

## 40. Collider と Joint 詳細調査票

### 40.1 Collider 共通

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 中心 | `colliderCenter` | GameObject 原点からの Local Offset。Transform 回転・Scale の影響。 |
| サイズ | `colliderSize` | Box または範囲の Local Size。Half Extent か Full Size か。 |
| 半径 | `colliderRadius` | Sphere / Capsule の半径。Scale のどの軸を使うか。 |
| 高さ | `colliderSize.y` | Capsule 全高か円柱部分の高さか。半径より小さい場合の処理。 |
| Trigger | `isTrigger` | 押し返さず Event だけを生成する。 |
| Contact Event | `generateContactEvents` | Event が必要な場合の ON 条件。 |

Mesh Collider は Convex、Triangle Mesh、動的使用、穴の保持、BVH、meshoptimizer、Collision Mesh 分離を必ず調査する。

### 40.2 Joint

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 接続先 | `connectedGameObjectId` | Hierarchy 名ではなく ID で保存されるか。未設定 -1。 |
| 軸 | `jointAxis` | World / Local、正規化の必要、Hinge の回転軸。 |
| 最小角度 | `jointMinLimit` | UI 度、保存ラジアンの変換。 |
| 最大角度 | `jointMaxLimit` | Min <= Max の制約。 |
| 最短距離 | `jointMinDistance` | Spring / Distance の縮み側制限。 |
| 最長距離 | `jointMaxDistance` | 伸び側制限。 |
| 周波数 | `jointSpringFrequency` | 戻る速さと振動。 |
| 減衰 | `jointSpringDamping` | 揺れを止める強さ。 |

## 41. Input パラメーター詳細調査票

### 41.1 旧 Input Component

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 移動速度 | `inputMoveSpeed` | 1 秒あたりの速度か、毎フレーム加算か。 |
| 前進 | `inputForwardKey` | DirectInput Key をプルダウンで選ぶ。固定 W ではない。 |
| 後退 | `inputBackKey` | 任意 Key へ変更できるか。 |
| 左移動 | `inputLeftKey` | World / Camera Relative / Local のどれか。 |
| 右移動 | `inputRightKey` | 反対入力同時押し時の扱い。 |
| ジャンプ | `inputJumpKey` | 押した瞬間か押しっぱなしか、接地条件。 |
| マウス感度 | `inputMouseSensitivity` | Mouse Delta への倍率。 |
| Y 軸反転 | `inputInvertY` | Look 入力の上下反転。 |

### 41.2 PlayerInput

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| Actions | `assetPath` | `.inputactions` Asset Path と未設定時の表示。 |
| Default Map | `inputActionMapName` | Play 開始時に有効にする Action Map。 |
| Behavior | `inputBehavior` | C++ Events など通知方式。現在実装される方式だけを記載。 |
| Action Map | `EditorInputEventBinding::actionMapName` | Event ごとの Map。 |
| Action | `actionName` | Asset 内の Action 名と完全一致が必要か。 |
| 関数 | `functionName` | DLL が export / dispatch できる関数名。 |
| 値型 | `valueType` | Button / Vector2。 |

Input Action の説明には、Key Path、Action、Action Map、Project 登録、PlayerInput、C++ 関数という 5 層を図または手順で示す。

## 42. Audio パラメーター詳細調査票

| パラメーター | 内部名 | 使用者へ説明する内容 | 範囲候補 |
| --- | --- | --- | --- |
| Audio Asset | `assetPath` | 対応形式、Project からの選択、再読み込み。 | Path。 |
| 音量 | `audioVolume` | 0=無音、1=原音。複数音の合成後に Clip するか。 | 0～1。 |
| ピッチ | `audioPitch` | 1=通常、0 の扱い、速度と音程への影響。 | 0～3。 |
| ループ | `audioLoop` | 終端で繰り返す。 | bool。 |
| 自動再生 | `audioPlayOnAwake` | Play 開始時に再生する。Editor 起動時ではない。 | bool。 |
| 空間ブレンド | `audioSpatialBlend` | 0=2D、1=3D、中間値の合成。 | 0～1。 |
| 最小距離 | `audioMinDistance` | この距離まで最大音量か。 | 0～1000。 |
| 最大距離 | `audioMaxDistance` | この距離以遠で無音か最小音量か。 | 0～10000。 |

Audio Filter は Filter ごとに、Inspector パラメーター、XAudio2 Effect 生成、Effect Chain 接続、順序、Bypass、保存を確認する。UI だけなら「設定画面のみ」とする。

## 43. Navigation パラメーター詳細調査票

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| Agent 半径 | `navAgentRadius` | 壁との距離、狭い通路を通れるか。 |
| Agent 高さ | `navAgentHeight` | 低い天井、NavMesh Build 条件。 |
| 最大速度 | `navMaxSpeed` | 経路上の移動上限。 |
| 最大加速度 | `navMaxAcceleration` | 速度へ到達する速さ、急停止。 |
| 停止距離 | `navStoppingDistance` | 目的地からどこで止まるか。 |
| 自動再経路 | `navAutoRepath` | 障害物や目的地変更で経路を再計算するか。 |
| Carve | `navCarve` | Obstacle が NavMesh へ穴を開けるか。 |
| 最大傾斜 | `navMaxSlope` | 登れる面の角度、度単位。 |
| 最大段差 | `navMaxClimb` | 登れる段差高さ。 |
| Area 上書き | `navAreaOverride` | Modifier の Area を使うか。 |
| Area | `navArea` | 通行コストまたは通行可否区分。 |
| Build 除外 | `navIgnoreFromBuild` | 対象 Mesh を NavMesh 生成から除外。 |
| 双方向 | `navBidirectional` | Link を両方向に通れるか。 |
| Cost | `navCostModifier` | Link を選ぶ経路コスト倍率。 |

目的地 ID という表示がある場合は、どの GameObject の ID を指定し、Hierarchy から選べるか、削除時にどうなるかを説明する。

## 44. Constraint と Animation パラメーター詳細調査票

### 44.1 Constraint

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 接続先 | `connectedGameObjectId` | 追従対象 GameObject。 |
| 重み | `constraintWeight` | 0=追従なし、1=完全追従。 |
| 位置 Offset | `constraintPositionOffset` | Target からずらす位置。 |
| 回転 Offset | `constraintRotationOffset` | Target からずらす回転。度 / ラジアン。 |
| Aim Axis | `constraintAimAxis` | +X / -X / +Y / -Y / +Z / -Z。 |
| Up Axis | `constraintUpAxis` | LookAt の上下方向。 |
| Roll | `constraintRoll` | LookAt 後の軸回り回転。 |
| Freeze X/Y/Z | `constraintFreezeAxisX/Y/Z` | Scale などで変更しない軸。 |

### 44.2 Animation

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| Asset | `assetPath` | FBX または Clip の Path。 |
| 再生速度 | `animationSpeed` | 1=等速、0=停止、負数の対応。 |
| ループ | `animationLoop` | Clip 終端後の挙動。 |
| 自動再生 | `animationPlayOnAwake` | Play 開始時に再生。 |
| 種類 | `animationType` | FBX Clip / Float / Rotate / Pulse / Bob。 |
| 振幅 | `animationAmplitude` | Procedural Animation の移動・回転量。 |
| Clip | `animationClipIndex` | FBX 内 Clip 配列と表示名。 |
| Animator State | `animatorState` | State 名ではなく Index か、遷移条件。 |

FBX Clip の読み込みが存在しても、Bone Skinning、State Transition、Root Motion が接続されるかを別々に判定する。

## 45. Particle、Camera、Environment パラメーター詳細調査票

### 45.1 Particle

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 発生レート | `particleRate` | 1 秒あたりの生成数。 | 
| 寿命 | `particleLifetime` | 生成から消滅までの秒数。 |
| 速度 | `particleSpeed` | 初期方向、Local / World。 |
| サイズ | `particleSize` | World Size か Screen Size。 |
| 自動再生 | `animationPlayOnAwake` | Play 開始時に Emit するか。 |

Rate、Lifetime、Speed、Size だけで描画まで実装されているか、Renderer、Material、Texture、Blend、最大数、再利用を確認する。

### 45.2 Camera

| パラメーター | 内部名 | 説明する内容 | 範囲候補 |
| --- | --- | --- | --- |
| 投影 | `cameraProjectionMode` | Perspective / Orthographic。 | 2 モード。 |
| 視野角 | `cameraFieldOfView` | 縦 FOV か横 FOV。Perspective のみ。 | 1～179 度。 |
| Near | `cameraNearClip` | これより近い物体を描かない。小さすぎる値の深度精度。 | 0.01～100。 |
| Far | `cameraFarClip` | これより遠い物体を描かない。 | 0.1～10000。 |
| 露出 | `cameraExposure` | EV 補正。PostProcess Exposure との合成。 | -10～10 EV。 |
| DOF | `cameraDofEnabled` | Depth Of Field Pass の有効化。 | bool。 |
| Focus Distance | `cameraDofFocusDistance` | Camera から焦点面まで。 | 0.1～1000。 |
| Aperture | `cameraDofAperture` | 現実の F 値か 0～1 のボケ量か。 | 0～1。 |
| Focal Length | `cameraDofFocalLength` | mm。FOV と同時に使うか。 | 1～300 mm。 |
| Motion Blur | `cameraMotionBlurEnabled` | Camera / Object Velocity のどちらを使うか。 | bool。 |
| Blur Intensity | `cameraMotionBlurIntensity` | ブラー量。 | 0～1。 |

### 45.3 Environment

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 環境画像 | `assetPath` または専用 Path | HDR / Cubemap / Equirectangular の対応。 |
| 環境画像を使用 | `environmentTextureEnabled` | OFF 時に画像を Sample しないこと。 |
| 空の上色 | `color` 等 | Zenith Color。 |
| 空の下色 | `skyLowerColor` | Horizon / Ground Color。 |
| 回転 | `environmentTextureRotation` | 水平回転。UI は度、内部ラジアンか。 |
| Mip Bias | `environmentTextureMipBias` | 反射のぼけと LOD。 |
| 強度 | `intensity` など | Ambient、Sky、Reflection のどこへ掛かるか。 |

Environment が存在しない場合に Ambient が 0 になるか、Sky Background と IBL Lighting を別々に OFF にできるかを確認する。

## 46. PostProcess パラメーター詳細調査票

### 46.1 Bloom

| パラメーター | 内部名 | 説明する内容 | 代表値 |
| --- | --- | --- | --- |
| 強さ | `bloomIntensity` | 最終画像へ足す Bloom の量。0=OFF。 | 0、0.5、1、2。 |
| 明部しきい値 | `bloomThreshold` | Bloom 対象にする HDR 輝度。 | 0、1、2、5。 |
| しきい値遷移 | `bloomSoftKnee` | Threshold 境界を滑らかにする割合。 | 0、0.5、1。 |
| にじみ | `bloomScatter` | Downsample / Upsample での広がり。 | 0、0.5、1。 |

### 46.2 AA

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| AA Mode | `aaMode` | None / FXAA / SMAA / Temporal の排他選択。 |
| SMAA Threshold | `smaaThreshold` | Edge と判断する輝度差。小さいほど多く処理。 |
| Corner Rounding | `smaaCornerRounding` | 角の Blend 量。実際の Shader 定数への接続。 |
| Temporal Sharpness | `temporalSharpness` | 履歴合成後の輪郭復元。 |
| Temporal Blend | `temporalBlendRatio` | Current / History の混合比。Ghosting とちらつきの Tradeoff。 |

None が FXAA の無効設定ではなく Passthrough PSO を使うか確認する。Scene View と Game View の履歴 Texture、Previous Matrix、Resize 時 Clear を分けて確認する。

### 46.3 Glare

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 方式 | `glareModeMask` | Bloom / Ghosts / Streaks / Fog Glow / Star / Sun Beams / Kernel を複数追加できるか。 |
| 強さ | `glareIntensity` | 原画像へ加算する量。 |
| 広がり | `glareSize` | Sample 間隔、光条長。 |
| 角度 | `glareAngle` | Streak / Star の基準角度、度。 |
| 光条数 | `glareStreakCount` | 放射方向数、2～8。 |
| 減衰 | `glareFade` | 中心から離れるほど暗くする割合。 |
| 色ずれ | `glareColorModulation` | Ghost / Streak の RGB 分離。 |
| 光源 X/Y | `glareCenter.x/y` | Sun Beam の Screen UV。 |

各 Glare を追加ボタンから積み、個別に折りたたみ、削除できるか確認する。同じ方式を複数追加できない設計なら、その制限を記載する。

### 46.4 Filter

| Filter | `filterModeMask` の対象 | 使用目的 | 必須説明 |
| --- | --- | --- | --- |
| Soften | bit 1 | 画像をぼかす。 | Kernel Size と Strength。 |
| Box Sharpen | bit 2 | 十字または矩形の輪郭強調。 | Noise 増加。 |
| Diamond Sharpen | bit 3 | 斜めを含む輪郭強調。 | Box との差。 |
| Laplace | bit 4 | 全方向の Edge。 | 出力が Edge のみか合成か。 |
| Sobel | bit 5 | 水平・垂直勾配。 | Edge 色と原画像合成。 |
| Prewitt | bit 6 | Sobel より単純な勾配。 | Sobel との差。 |
| Kirsch | bit 7 | 8 方向 Edge。 | 負荷と方向選択。 |
| Shadow | bit 8 | Offset した暗い像。 | Offset、Color、Alpha。 |

共通の `filterStrength` が全 Filter に共用されるか、Filter ごとの値を持つかを確認する。

### 46.5 Final Composite

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 明るさ | `finalBrightness` | Tone Mapping 前後のどちらへ掛かるか。 |
| 露出 | `compositeExposure` | Camera Exposure との合成順。 |
| White Point | `compositeWhitePoint` | Tone Mapping の白基準。 |
| Tone Mapping | `compositeToneMappingMode` | Reinhard / Filmic / Timothy / Uncharted2 / ACES。 |
| Bloom 合成 | `compositeBloomIntensity` | Bloom Pass 強さとの二重適用に注意。 |
| 彩度 | `compositeSaturation` | 0=Grayscale、1=元色か。 |
| Contrast | `compositeContrast` | 1=元画像か。Pivot。 |
| Vignette | `compositeVignetteStrength` | 画面端の暗さ。 |
| Vignette Radius | `compositeVignetteRadius` | 暗くなり始める範囲。 |
| Film Grain | `compositeFilmGrain` | Noise 強度、時間変化。 |
| Chromatic Aberration | `compositeChromaticAberration` | RGB Sample Offset。 |
| AO Strength | `compositeAmbientOcclusionStrength` | GTAO / SSAO Texture への倍率。 |

## 47. UI Component パラメーター詳細調査票

### 47.1 Button

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| ラベル | `buttonLabel` | Game View に表示する文字、文字コード、Font。 |
| 位置 | `buttonPosition` | Game View 左上基準 Pixel か Canvas 座標。 |
| サイズ | `buttonSize` | Pixel、Scale、Window Resize。 |
| 操作可能 | `buttonInteractable` | false 時の色、Event 無効化。 |
| Hover 色 | `buttonHoverColor` | Mouse Over 中の色。 |
| Pressed 色 | `buttonPressedColor` | 押下中の色。 |
| OnClick | `buttonOnClickFunction` | 同じ GameObject の C++ Script 関数名。 |

### 47.2 Toggle / Slider

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| Toggle Value | `toggleValue` | 現在の bool 値、初期値、Scene 保存。 |
| Toggle Event | `toggleOnValueChangedFunction` | 変更時に渡す値と C++ 関数。 |
| Slider Value | `sliderValue` | 現在値。Min / Max 外の Clamp。 |
| Slider Min | `sliderMinValue` | 下限。 |
| Slider Max | `sliderMaxValue` | 上限。Min > Max の処理。 |
| Slider Event | `sliderOnValueChangedFunction` | float 値を C++ へどう渡すか。 |

Button、Toggle、Slider は Inspector があるだけでなく、Game View 描画、Mouse Hit Test、Event Dispatch まで存在するか確認する。

## 48. Scene Physics Settings 詳細調査票

| パラメーター | 内部名 | 説明する内容 |
| --- | --- | --- |
| 重力 | `gravity` | Scene 全体の World 加速度。Y 負で下方向。 |
| Fixed Time Step | `fixedTimeStep` | 物理更新間隔。小さいほど精度と負荷が上がる。 |
| Collision Step | `collisionStepCount` | 1 Update 内の衝突分割数。 |
| Collider Debug | `drawColliderDebug` | Scene View へ形状表示。 |
| Contact Debug | `drawContactDebug` | 接触点と法線表示。 |
| Cast Debug | `drawCastDebug` | Raycast / ShapeCast 表示。 |
| Velocity Debug | `drawVelocityDebug` | Rigidbody の速度と角速度を矢印表示。 |
| Force Direction Debug | `drawForceDirectionDebug` | Scene重力、ConstantForce、風、重力場、回転軸、流れ、電場、磁場、Joint軸を矢印表示。 |
| Field Volume Debug | `drawFieldVolumeDebug` | 風・重力・回転・電磁場の半径、FluidVolume、Buoyancy領域、空気力学の圧力中心を表示。 |
| Connection Debug | `drawConnectionDebug` | SpringForce のAnchor間とJointの接続先を線表示。 |
| Selected Only | `drawSelectedOnlyDebug` | 選択中GameObjectだけへ表示対象を絞る。 |
| Vector Scale | `debugVectorScale` | 速度と力の矢印長を0.01から10.0で調整。 |
| Layer Matrix | `layerCollisionMatrix` | 8 Layer 間の衝突可否。対称更新か。 |

### 48.1 物理デバッグ表示の操作

1. Scene View左側の`物理`を押す。
2. `当たり判定の形`、`速度 / 角速度`、`力 / 場の向き`、`影響範囲 / 流体領域`、`ばね / Joint 接続`から必要な種類だけ有効にする。
3. 表示が密集する場合は`選択中だけ表示`を有効にして、Hierarchyで確認対象を選択する。
4. 矢印が短すぎる、または長すぎる場合は`ベクトル倍率`を変更する。
5. 接触点、接触法線、Raycast、SphereCast、CapsuleCastの実行結果はPlay中に確認する。

同じ設定はGameObjectを選択していない時のInspectorにある`物理設定 > デバッグ表示`からも変更できる。値はSceneの`PhysicsSettings`行へ保存され、Project全体ではなくSceneごとに保持される。

Colliderは通常が青、Triggerが緑、選択中が黄。速度は水色、角速度と磁場は紫、力は橙、風と流体は緑、重力は赤、電場は黄で表示する。Sphere/Capsule Castは開始形状と到達形状も表示し、命中点と命中法線は赤で表示する。

今回追加した物理ComponentはInspectorと追加メニューで`空気力学`、`風ゾーン`、`重力場`、`回転座標系`、`流体ボリューム`、`ばね力`、`ロープ拘束`、`ねじりばね`、`推進力`、`滑車拘束`、`物理サーボ`、`渦流場`、`圧力場`、`サスペンション`、`姿勢安定化`、`電磁気ボディ`、`電磁場`と日本語表示する。Scene保存互換とC++ Scriptの型名指定に使う内部型名はAerodynamicsやRopeConstraintなどの英語名を維持する。

## 49. C++ Script Runtime API 個別調査票

現在の `EditorScriptRuntimeApi` には次の API がある。各 API を 1 項目ずつ説明し、存在しない API をあるものとして書かない。

| API | 調査する引数 | 戻り値 | 使用者向けの主用途 |
| --- | --- | --- | --- |
| `Log` | UTF-8 文字列 | なし | Console 出力。Null と文字列寿命。 |
| `IsKeyDown` | Key Code | bool | 現在押されているか。押しっぱなし。 |
| `IsKeyPressed` | Key Code | bool | 押した瞬間か。前 Frame 比較。 |
| `GetActionVector2` | GameObject ID、Map、Action | Vector2 | Move / Look など。未設定時 0。 |
| `IsActionPressed` | GameObject ID、Map、Action | bool | Button Action の現在状態。 |
| `WasActionJustPressed` | GameObject ID、Map、Action | bool | Button Action の押した瞬間。 |
| `GetMousePosition` | なし | Vector2 | Window / Client / Game View のどの座標か。 |
| `GetTransform` | GameObject ID | Transform | Position / Rotation / Scale 取得。見つからない ID。 |
| `SetTransform` | GameObject ID、Transform Pointer | なし | Transform 更新。Physics Body との同期。 |
| `GetVelocity` | GameObject ID | Vector3 | Rigidbody 速度取得。Rigidbody なしの場合。 |
| `SetVelocity` | GameObject ID、Vector3 Pointer | なし | Dynamic Rigidbody 速度設定。 |
| `GetAngularVelocity` | GameObject ID | Vector3 | 角速度取得。 |
| `SetAngularVelocity` | GameObject ID、Vector3 Pointer | なし | 角速度設定。 |
| `AddForce` | GameObject ID、Force Pointer | bool | 継続的な力。成功条件。 |
| `AddForceAtPosition` | GameObject ID、Force、World作用点 | bool | 重心から外れた力で並進とTorqueを発生。 |
| `AddImpulse` | GameObject ID、Impulse Pointer | bool | 瞬間的な速度変化。 |
| `AddTorque` | GameObject ID、Torque Pointer | bool | 回転力。Rolling の例。 |
| `AddExplosionImpulse` | 中心、半径、Impulse、上向き補正 | int | 範囲内Dynamic Rigidbodyへ距離減衰Impulse。 |
| `AttachRope` / `DetachRope` | 所有者、対象、Anchor、最大長 | bool | 実行中のロープ接続と解除。 |
| `SetRopeLength` / `RepairRope` | 所有者、最大長 | bool | ウインチと破断修復。 |
| `GetRopeState` | 所有者 | Rope State | 接続、破断、現在長、現在張力。 |
| `SetComponentActive` | GameObject ID、内部型名、Active | bool | 任意Componentの実行ON/OFF。 |
| `IsComponentActive` | GameObject ID、内部型名 | bool | 任意Componentの実行状態取得。 |
| `GetAiSensorState` | GameObject ID、Sensor Kind | State | Vision、Object、Motion、Speech の結果。 |
| `GetMaterialState` | GameObject ID | Material State | Renderer Material の読み取り。書き込み API の有無。 |
| `GetAnimationState` | GameObject ID | Animation State | Clip、再生状態、時間の読み取り。再生 API の有無。 |

### 49.1 API で必ず確認する失敗条件

- `runtimeApi == nullptr`。
- GameObject ID が存在しない。
- 必要 Component がない。
- Component が無効。
- Play 中ではない。
- DLL API Version が一致しない。
- Pointer 引数が `nullptr`。
- Action Map / Action 名が存在しない。
- Rigidbody が Dynamic ではない。
- Sensor の外部処理が起動していない。

### 49.2 Input Key の説明

`EditorScriptKeyCodeW` を「前進キー」と説明してはいけない。これは W キーの識別子であり、前進に使うかはユーザーコードが決める。

`Key::W`、`Key::A` などの短い Alias がある場合は、次のように説明する。

```cpp
if (runtimeApi->IsKeyDown(Key::W)) {
    // W キーが押されている間に行う任意の処理を書く。
}
```

「W=前進」「Q/E=回転」と固定用途にしない。

## 50. Script 共有構造体のフィールド説明

### 50.1 `EditorScriptTransform`

| フィールド | 説明 |
| --- | --- |
| `position` | GameObject の位置。World / Local を実処理で確認する。 |
| `rotation` | X/Y/Z 回転。度かラジアンかを確認する。 |
| `scale` | X/Y/Z Scale。1 が等倍。 |

### 50.2 `EditorScriptPhysicsEvent`

| フィールド | 説明 |
| --- | --- |
| `type` | Collision / Trigger の Enter / Stay / Exit。 |
| `selfGameObjectId` | Event を受け取る自分の ID。 |
| `otherGameObjectId` | 接触相手の ID。 |
| `point` | 代表接触位置。World 座標か確認する。 |
| `normal` | 接触法線。自分から相手か相手から自分かを確認する。 |
| `relativeVelocity` | 接触前後どちらの相対速度か。 |
| `separation` | 正値 / 負値が距離・めり込みのどちらを意味するか。 |
| `isTrigger` | 押し返しなしの Trigger Event か。 |

### 50.3 `EditorScriptInputActionContext`

| フィールド | 説明 |
| --- | --- |
| `gameObjectId` | PlayerInput を持つ対象。 |
| `phase` | Started / Performed / Canceled。 |
| `valueType` | Button / Vector2。 |
| `buttonValue` | Button の 0 / 1 または Analog 値。 |
| `vector2Value` | Move / Look の X/Y。 |
| `actionMapName` | Action Map 名。 |
| `actionName` | Action 名。 |
| `bindingPath` | 実際に反応した `Keyboard/W` など。 |

### 50.4 `EditorScriptMaterialState`

| フィールド | 説明 |
| --- | --- |
| `hasComponent` | Renderer Component があるか。 |
| `hasTexture` | Base Color Texture があるか。 |
| `hasUvLayoutTexture` | UV 確認画像があるか。 |
| `useLighting` | Lighting Path を使用するか。 |
| `intensity` | Material 強度。 |
| `metallic` | 金属度。 |
| `roughness` | 粗さ。 |
| `ior` | 屈折率。 |
| `alpha` | 透明度。 |
| `reflectionStrength` | 反射寄与。 |
| `color` | Base Color。 |
| `rendererAssetPath` | Renderer の Model Asset。 |
| `materialName` | Material 名。 |
| `texturePath` | Base Color Texture Path。 |
| `uvLayoutTexturePath` | UV 確認画像 Path。 |

読み取り専用か、Set API が存在するかを明記する。現在の Runtime API に SetMaterial がなければ「取得のみ」とする。

### 50.5 `EditorScriptAnimationState`

| フィールド | 説明 |
| --- | --- |
| `hasComponent` | Animation Component があるか。 |
| `isPlaying` | 現在再生中か。 |
| `isLoop` | Loop 設定。 |
| `playOnAwake` | Play 開始時の自動再生。 |
| `animationType` | FBX / Procedural の種類。 |
| `clipCount` | 読み込まれた Clip 数。 |
| `animationSpeed` | 再生速度倍率。 |
| `animationAmplitude` | Procedural の振幅。 |
| `currentTime` | 現在時刻、秒。 |
| `currentClipDuration` | Clip 長、秒。 |
| `assetPath` | Animation Asset。 |
| `currentClipName` | 現在 Clip 名。 |

### 50.6 `EditorScriptAiSensorState`

共通構造体でも、Sensor ごとに有効な値が異なる。種類別の表を必ず作る。

| Sensor | 有効値候補 | 使用例 |
| --- | --- | --- |
| Vision | `isDetected`、`detectedGameObjectId`、`range`、`angleDegrees`、`distance`、`direction` | 発見した対象へ回転、追跡開始。 |
| Object Detection | `label`、`confidence`、`screenPosition`、`boundsPosition`、`boundsSize` | `person` を見つけた時に反応。 |
| Color Tracking | `label`、`confidence`、`screenPosition`、`boundsPosition`、`boundsSize` | 赤い Marker の中心へ向く。 |
| Motion Detection | `motion`、`motionMagnitude`、`confidence` | 一定以上の動きで警戒。 |
| Whisper | `text`、`confidence` | 認識文が「進め」か比較。 |
| Voice Command | `command`、`commandId`、`text`、`confidence` | Command ID で Move / Stop を分岐。 |

`hasComponent`、`isActive`、`isDetected`、`hasDetails` の違いを例付きで説明する。

## 51. 使用手順を書くための統一フォーマット

使用者向け手順は、次の順で書く。途中の前提を省略しない。

```text
目的:
完成状態:
必要な Asset:
必要な Component:
事前確認:

手順:
1. どのウィンドウを開くか。
2. どの GameObject を選ぶか。
3. どのボタンを押すか。
4. どのカテゴリから Component を追加するか。
5. Inspector のどの項目へ何を入れるか。
6. 追加で必要な Component を設定する。
7. Scene を保存する。
8. Play を押す。
9. Scene / Game / Console のどこを確認するか。

期待結果:
失敗した場合の確認順:
現在の制限:
```

「任意の値を設定してください」ではなく、最初に動作を確認できる具体的な推奨値を示す。その後に調整範囲を説明する。

## 52. 調査担当者向けの実作業手順

### 52.1 Component 1 件を調べる手順

1. `EditorScene.h` で enum と保存フィールドを確認する。
2. `kComponentAddEntries` で日本語名とカテゴリを確認する。
3. `EditorInspectorPanel.cpp` で表示項目、範囲、Step、Combo の選択肢を抽出する。
4. `EditorScene::CreateComponent` で初期値を抽出する。
5. `SaveScene` の出力と `LoadScene` の入力を照合する。
6. `EditorRuntimeManager` から呼ばれる Manager を探す。
7. Manager 内で Component Type を検索し、実処理と前提条件を読む。
8. 描画 Component なら Synchronizer、Render Manager、Shader まで追跡する。
9. C++ API がある場合は Runtime API の関数と失敗値を追跡する。
10. Component Inventory と詳細レコードへ根拠を記録する。
11. 静的確認で断定できない項目を「実機確認待ち」にする。

### 52.2 Parameter 1 件を調べる手順

1. UI Label を検索する。
2. UI が読み書きするフィールド名を記録する。
3. UI の Min、Max、Step、Combo Item を記録する。
4. CreateComponent の初期値を記録する。
5. Save / Load の列位置または Key を照合する。
6. Runtime の読み取り箇所を検索する。
7. Shader Constant へ渡る場合は C++ と HLSL の Layout を照合する。
8. 値 0、初期値、中間値、最大値で期待変化を定義する。
9. 他 Parameter に上書きされる条件を記録する。
10. 使用者が確認できる 3～10 Step の手順を書く。

### 52.3 実装状態を決める手順

```text
追加 UI なし                        -> 未実装または内部専用
追加 UI あり、Inspector なし        -> 一部使用可能または未完成
Inspector あり、保存なし            -> 設定画面のみ
保存あり、Runtime 読み取りなし       -> 設定画面のみ
Runtime 読み取りあり、結果未確認     -> 未確認または実験的
Runtime と最小使用例が成立           -> 使用可能または一部使用可能
再現する重大不具合あり               -> 既知の不具合ありを併記
```

## 53. 画面キャプチャの要求

サイト作成用の調査結果には、可能なら次の画面を用意する。画像が用意できない場合は「必要な画面」として撮影指示を残す。

- 初期レイアウト全体。
- Hierarchy の作成・親子・検索。
- Scene の移動 / 回転 / 拡縮 Gizmo。
- Game View と Camera。
- Inspector の GameObject Header。
- Component 追加 Popup とカテゴリ。
- Material Texture Slot。
- Rigidbody と Collider。
- Input Actions Asset Editor。
- C++ Script Component と Build Button。
- Physics Settings と Layer Matrix。
- PostProcess の各折りたたみ項目。
- Light と Camera Gizmo。
- Project の Folder / Asset。
- Console の Info / Warning / Error。

画像には、どこを押すかが分かる Caption と番号を付ける。古い UI の Screenshot は現在コードと一致するか確認する。

## 54. 親切さの受け入れ条件

最終的な調査結果は、初めて CG2 を触る使用者が次を質問せず実行できる粒度にする。

- どの GameObject を選べばよいか。
- どのカテゴリに目的の Component があるか。
- Component をどの順に追加するか。
- Asset をどの Folder へ置くか。
- Path は手入力か選択か。
- 初回確認ではどの値を入力すればよいか。
- Play が必要か、編集画面ですぐ変わるか。
- Scene View と Game View のどちらを見るか。
- Console のどの Message を成功と判断するか。
- 保存は通常保存か名前を付けて保存か。
- C++ のどの関数へコードを書くか。
- Update と FixedUpdate のどちらを使うか。
- Transform、Velocity、Force のどれを使うべきか。
- 動かない時に最初に何を確認するか。

説明を読んでも上記のいずれかが不明な場合、そのページは未完成とする。

## 55. 追加で必ず調査する不足領域

前章までで Component と主要 API の調査単位は定義しているが、使用者向けサイトとしてはまだ不足しやすい領域がある。
次の項目は、全機能ページの前提資料として必ず調査する。

### 55.1 動作環境

使用者が最初に詰まりやすいので、次を明記する。

| 項目 | 調査内容 |
| --- | --- |
| OS | 対応 Windows、確認済みバージョン、未確認バージョン。 |
| CPU | 必須命令セット、推奨コア数。 |
| GPU | DirectX 12 対応、Feature Level、推奨 VRAM。 |
| Driver | NVIDIA / AMD / Intel で確認済み Driver。 |
| Visual Studio | VS2022 / VS2026 の扱い、必要 Workload、MSVC Toolset。 |
| Windows SDK | 使用 SDK Version、DirectXTex / DirectXTK との整合。 |
| Python | AI / Tool で使う場合の Version、仮想環境、PATH。 |
| CUDA / cuDNN / ONNX Runtime | AI Component で必要な場合だけ、必須 / 任意を分ける。 |
| 外部 DLL | 実行時に必要な DLL と配置場所。 |

### 55.2 初回セットアップ

次の手順を途中で省略しない。

1. Repo を配置する場所。
2. 必要な外部ライブラリの配置場所。
3. Visual Studio で開く Solution。
4. Configuration と Platform。
5. 初回 Build。
6. 初回起動。
7. Scene を開く。
8. モデルを置く。
9. Play する。
10. Console で成功 / 失敗を見る。

各手順には、成功時に画面や Console に何が出るかを書く。

### 55.3 外部ライブラリ一覧

`externals`、`ThirdParty`、`Assets/Shaders`、`ThirdParty/Shader` を調べ、次の形式で一覧化する。

| ライブラリ | Version | 配置場所 | 用途 | 必須 / 任意 | 使用中 / 未接続 | License | 再配布時の注意 |
| --- | --- | --- | --- | --- | --- | --- | --- |

特に次は、使用者が触る機能に関係するため必ず書く。

- Jolt Physics。
- FBX SDK。
- DirectXTex。
- DirectXTK。
- meshoptimizer。
- FidelityFX。
- LYGIA / Noise 系 HLSL Include。
- NVIDIA NRD / RTXGI / DDGI 系。
- Recast Navigation。
- BehaviorTree.CPP。
- ONNX Runtime / CUDA / cuDNN。
- ImGui / ImGuizmo / imgui-node-editor。

「置いてある」だけと「Engine が実際に Include / Link / Runtime 呼び出ししている」は別の状態として扱う。

### 55.4 Project Settings

Project Settings 相当の画面または設定ファイルを全て調査する。

| 設定分類 | 必ず説明する内容 |
| --- | --- |
| Input | Project-wide Actions、Action Asset、Action Map、Binding、PlayerInput との関係。 |
| Physics | Gravity、Fixed Delta Time、Solver、Layer Collision Matrix。 |
| Render | HDR、AA、Bloom、SSR、Planar Reflection、Shadow、ToneMapping。 |
| Quality | 低 / 中 / 高の違い、GPU負荷。 |
| Tags / Layers | 作成、割り当て、物理 / 描画 / 検索への影響。 |
| Audio | Output、Volume、Mixer、3D Audio の有無。 |
| AI | Python Path、Model Path、GPU / CPU、実行頻度。 |
| Build | 出力先、必要 DLL、Asset コピー。 |

設定が存在しない場合も「未実装」と書き、使用者がどこを探せばよいか迷わないようにする。

## 56. C++ スクリプトで必要な関数と説明範囲

C++ スクリプトは、使用者向けサイトで最優先に詳細化する。
理由は、GameObject を動かす、入力を読む、物理を動かす、イベントを受ける、AI や Material の状態を見る入口になるためである。

### 56.1 必ず説明するユーザーライフサイクル

| 関数 | 呼ばれるタイミング | 使用目的 | 使用者が書く内容 | 注意点 |
| --- | --- | --- | --- | --- |
| `Start()` | Inspector値反映後、対象Componentごとに1回。 | 初期化。 | 速度、HP、初期状態、ログ。 | 不要なら宣言・実装しない。 |
| `Update(float deltaTime)` | ActiveなComponentへ毎フレーム。 | 入力、見た目の更新、通常の移動。 | `GetGameObject()`、Input、簡単な制御。 | Rigidbody物理と直接Transform更新を競合させない。 |
| `FixedUpdate(float fixedDeltaTime)` | ActiveなComponentへ固定時間物理更新。 | 力、速度、物理操作。 | `GetComponent<Rigidbody>()`から物理操作する。 | 不要なら宣言・実装しない。 |
| Collision / Trigger | 所有Objectの物理Event発生時。 | 接触イベント処理。 | 相手ID、接触点、法線、相対速度で分岐。 | 不要なEvent関数は書かない。 |
| `OnAnimationEvent(...)` | Animation Event時刻を通過した時。 | 足音、攻撃判定、Effect、任意処理。 | Event名、Effect Path、時刻、Local Offsetで分岐。 | Event内文字列PointerはCallback中だけ有効。 |
| `Stop()` | Play停止、Object破棄、Script停止。 | 終了通知。 | 購読解除などを行う。 | 不要なら宣言・実装しない。 |

### 56.2 Inspector 公開変数関数

使用者が「C++ で作った変数を Inspector に出す」ために必須である。
存在する場合は必ず説明し、未対応部分は制限事項に書く。

| 関数 | 目的 | 説明すべきこと |
| --- | --- | --- |
| `EditorScript_GetFieldCount` | 公開変数の数を返す。 | 0 の場合は Inspector に変数が出ない。 |
| `EditorScript_GetFieldDescriptor` | 変数名、表示名、型、初期値、Range を返す。 | `name` と `displayName` の違い、Min / Max / Step の意味。 |
| `EditorScript_GetFieldValueInstance` | Component実体の現在値を返す。 | `instance`が指すクラスの通常メンバーを読む。 |
| `EditorScript_SetFieldValueInstance` | Inspectorから変更された値を対象実体へ渡す。 | 型チェック、範囲外値、文字列長、保存対象。 |

説明には、Bool、Int32、Float、Vector2、Vector3、String のサンプルを分けて載せる。

### 56.3 Runtime API 関数の説明必須項目

各関数は、次の項目を必ず持つ。

| 項目 | 内容 |
| --- | --- |
| 関数名 | `runtimeApi->GetTransform(...)` のように実際の呼び方を書く。 |
| 呼べるタイミング | Load / Start / Update / FixedUpdate / PhysicsEvent / Stop のどこで使うか。 |
| 必要 Component | Rigidbody、Collider、PlayerInput、MeshRenderer など。 |
| 引数 | `gameObjectId`、Action Map 名、Action 名、Vector pointer など。 |
| 戻り値 | 成功 / 失敗、0、空文字、false の意味。 |
| 単位 | 秒、メートル相当、度 / ラジアン、ワールド座標 / ローカル座標。 |
| 副作用 | Transform が変わる、物理 Body が起きる、Velocity が上書きされるなど。 |
| 競合 | Transform 直接変更と Rigidbody、SetVelocity と AddForce など。 |
| 最小コード | 5～20 行程度の実用例。 |
| よくある失敗 | DLL未読込、Componentなし、Action名違い、Debug/Release違い。 |

### 56.4 入力 API

| API | 何を説明するか |
| --- | --- |
| `IsKeyDown` | キーが押されている間 true。キー定数は「用途」ではなく「物理キー名」として説明する。 |
| `IsKeyPressed` | 押した瞬間だけ true か、実装を確認して説明する。 |
| `GetActionVector2` | `ActionMap/Action` から Vector2 を読む。Action Asset、PlayerInput、Binding が必要。 |
| `IsActionPressed` | Button Action が押されているか読む。 |
| `WasActionJustPressed` | Button Action の押した瞬間を読む。 |
| `GetMousePosition` | 座標系、Scene / Game View のどちら基準か確認する。 |

キー名の説明では、`W = 前進` のように固定しない。
例を書く場合は「この例では W を前進に割り当てる」と明記する。

### 56.5 Transform API

| API | 使用場面 | 注意点 |
| --- | --- | --- |
| `GetTransform` | 現在位置、回転、スケールを読む。 | 回転が度かラジアンかをコードとUIで確認する。 |
| `SetTransform` | Transform を直接書き換える。 | Rigidbody と併用すると物理結果を上書きする可能性がある。 |

Transform 例は、次を分けて用意する。

- 入力で位置を動かす例。
- 入力で回転する例。
- ローカル方向ではなくワールド方向へ動く例。
- Rigidbody がある場合に直接 `SetTransform` を避ける例。

### 56.6 物理 API

| API | 使用場面 | 必要条件 |
| --- | --- | --- |
| `GetVelocity` | 現在速度を読む。 | Rigidbody が必要。 |
| `SetVelocity` | 速度を直接指定する。 | Rigidbody が Dynamic であることを確認する。 |
| `GetAngularVelocity` | 回転速度を読む。 | Rigidbody が必要。 |
| `SetAngularVelocity` | 回転速度を直接指定する。 | 回転固定軸と競合する場合を説明する。 |
| `AddForce` | 継続的な力を加える。 | 原則 `FixedUpdate` で使う。 |
| `AddForceAtPosition` | 作用点付きの力で並進と回転を発生させる。 | World作用点を渡す。 |
| `AddImpulse` | 瞬間的な衝撃を加える。 | ジャンプ、弾かれ、爆発など。 |
| `AddTorque` | 回転力を加える。 | 球や車輪を転がす例に使う。 |
| `AddExplosionImpulse` | 範囲内へ距離減衰付き衝撃を加える。 | Dynamic RigidbodyとColliderが必要。 |
| `RopeConstraint` Wrapper | 接続、解除、巻取り、修復、状態取得。 | RopeConstraintを事前追加する。 |

物理例は、次の作例を必ず用意する。

- W を押したら前方向へ速度を入れる。
- Space を押した瞬間に上方向へ Impulse を入れる。
- 球に Torque を入れて転がす。
- CollisionEnter で相手の名前または ID を見る。
- TriggerEnter でアイテム取得を行う。
- Eを押した瞬間にRopeConstraintを対象へ接続し、再度Eで解除する。
- ロープ長を徐々に短くしてウインチを作り、破断後にRepairできることを確認する。
- 爆心から距離の異なる複数BodyへExplosion Impulseを加え、距離減衰と上向き補正を比較する。

### 56.7 AI / Material / Animation 取得 API

| API | 説明に必要な内容 |
| --- | --- |
| `GetAiSensorState` | Sensor 種類ごとに返る値を分けて説明する。音声、画像、視界、動き検出を同じ説明にまとめない。 |
| `GetMaterialState` | 色、Texture Path、Metallic、Roughness、IOR、Alpha、Reflection、Emission を取得する目的を書く。 |
| `GetAnimationState` | 現在 Clip、再生中、速度、Loop、時間を取得する目的を書く。 |

AI は特に、次のように用途ごとに分ける。

| Sensor | 主に見る値 | 使用例 |
| --- | --- | --- |
| 視界 | `isDetected`、`detectedGameObjectId`、`distance`、`direction`。 | 見つけた対象へ向く。 |
| 画像物体検出 | `label`、`confidence`、`boundsPosition`、`boundsSize`。 | `label == "person"` で追跡開始。 |
| 色追跡 | `label`、`screenPosition`、`confidence`。 | 赤い目標を追う。 |
| 動き検出 | `motion`、`motionMagnitude`、`confidence`。 | 動き量が閾値以上なら警戒。 |
| Whisper 音声認識 | `text`、`confidence`。 | `text` に「止まれ」が含まれるか判定。 |
| 音声コマンド | `command`、`commandId`、`text`。 | `command == "Move"` で移動開始。 |

### 56.8 C++ スクリプトの失敗診断

次の現象ごとに、確認順を用意する。

| 現象 | 確認順 |
| --- | --- |
| DLL 読み込み失敗 | Path、Debug / Release、x64、API Version、依存 DLL、古い DLL ロック。 |
| Build しても反映されない | 実行中 Engine が DLL を掴んでいないか、出力先が Component の DLL Path と一致するか。 |
| W などキー入力が反応しない | Play 中か、Game / Scene View focus、KeyCode、Action Asset、PlayerInput、Component 有効状態。 |
| Action が反応しない | Project Settings、Action Map Enable、Binding Path、Action 名、Map 名、PlayerInput 接続。 |
| 物理 API が効かない | Rigidbody 有無、Kinematic、Freeze、Gravity、Collider、FixedUpdate から呼んでいるか。 |
| 回転しない | Rotation 単位、Freeze Rotation、SetTransform と物理の競合、Torque 軸。 |
| 速度を入れても戻される | 他 Component が SetVelocity / SetTransform していないか。 |
| クラッシュする | `runtimeApi == nullptr`、破棄済み state、null pointer、構造体 Version 不一致。 |

## 57. 最優先で全部やる対象

「最優先」は一部だけを選ぶ意味ではなく、使用者向けサイトの土台として全て完了させる対象を指す。
次の 12 項目は最優先扱いにする。

| 優先 | 対象 | 完了条件 |
| --- | --- | --- |
| 1 | C++ Script | 作成、Build、Attach、Play、Input、Transform、Physics、Event、Inspector公開変数まで一通り説明できる。 |
| 2 | Component 全一覧 | 追加場所、実装状態、Inspector項目、保存、Runtime動作が表で分かる。 |
| 3 | Input | Action Asset 作成から C++ 関数実行まで省略なく説明できる。 |
| 4 | Physics | Rigidbody、Collider、Trigger、Collision、Material、Layer、FixedUpdate、C++ API を説明できる。 |
| 5 | Material / Texture | Base Color、Texture、UV、Normal、Metallic、Roughness、Alpha、Emission、Reflection の使い方が分かる。 |
| 6 | Rendering / PostProcess | Bloom、AA、SSR、Planar、Shadow、ToneMapping、Environment の設定と制限が分かる。 |
| 7 | Camera / Scene / Game View | 編集用 Scene と実行用 Game の違い、Camera 設定、Target Texture、View 切替が分かる。 |
| 8 | Asset Import | FBX、OBJ、PNG、WAV、JSON、C++、DLL の配置、読み込み、制限が分かる。 |
| 9 | Scene / Prefab 保存 | 名前を付けて保存、通常保存、読み込み、保存対象、未保存対象が分かる。 |
| 10 | UI / Audio / Animation / AI | 追加できるだけか、実際に動くかを明確に分ける。 |
| 11 | Troubleshooting | 使用者が遭遇する代表的な失敗を確認順付きで解決できる。 |
| 12 | サンプル | 最小 Scene、入力移動、物理落下、C++ Script、反射、Audio、AI の作例がある。 |

## 58. C++ スクリプトページの必須構成

ChatGPT Work がサイト化する時、C++ スクリプトのページは次の構成にする。

```text
C++ スクリプト
├─ 概要
├─ できること
├─ 作成手順
├─ DLL Build 手順
├─ GameObject へ追加する手順
├─ ライフサイクル
├─ Inspector 公開変数
├─ Input Action 連携
├─ Transform 操作
├─ Rigidbody / Physics 操作
├─ Collision / Trigger
├─ Material 取得
├─ Animation 取得
├─ AI Sensor 取得
├─ UI から関数を呼ぶ
├─ よく使うコード例
├─ API 一覧
├─ 構造体一覧
├─ よくある失敗
└─ 制限事項
```

### 58.1 最小テンプレートの方針

テンプレートには全 API を詰め込まない。
初期テンプレートは、使用者が読み始めやすい最小構成にする。

空Templateのユーザー `.cpp` に入れるもの:

- `#include "ScriptName.h"`
- `void ScriptName::Update(float deltaTime)`
- ゲーム処理を書く位置を示す短いComment。

DLL ABI、Runtime API保持、ComponentごとのInstance、Field転送、Action転送は`.Generated.cpp`だけへ生成する。
- `GetTransform` と `SetTransform` の最小例。
- `WasActionJustPressed` の最小例。

テンプレートに入れないもの:

- 全 AI Sensor 例。
- 全 Material 例。
- 全 Animation 例。
- 長い物理サンプル。
- 複雑な Input Action 登録例。

詳細例はリファレンスページへ分離する。

### 58.2 コード例の書き方

コード例は、必ず次を満たす。

- C++ のみを載せる。C# の例は載せない。
- `runtimeApi == nullptr` の確認は載せる。
- `gameObjectId` が何を指すか説明する。
- `deltaTime` と `fixedDeltaTime` の違いを説明する。
- どの Component が必要か直前に書く。
- どの Inspector 項目を設定するか直前に書く。
- そのコードで何が起きるか直後に書く。

## 59. 使用者向けサンプルとして必ず作る C++ 例

次の例は、サイト作成時に独立したページまたは折りたたみとして用意する。

| 例 | 必要 Component | 主に使う API | 説明する内容 |
| --- | --- | --- | --- |
| W で前へ動く | C++ Script、必要なら PlayerInput。 | `IsKeyDown` または `GetActionVector2`。 | キー固定版と Input Action 版を分ける。 |
| Space でジャンプ | Rigidbody、Collider、C++ Script。 | `WasActionJustPressed`、`AddImpulse`。 | `Update` で入力、`FixedUpdate` で物理を分ける方法。 |
| 球を Torque で転がす | Rigidbody、Sphere Collider。 | `AddTorque`。 | Torque 軸、摩擦、Freeze Rotation。 |
| 触れたらログ | Collider、Rigidbody、C++ Script。 | `EditorScript_OnPhysicsEvent`。 | Collision と Trigger の違い。 |
| アイテム取得 | Trigger Collider、C++ Script。 | `OnPhysicsEvent`。 | `otherGameObjectId` の使い方。 |
| Material を読む | MeshRenderer、C++ Script。 | `GetMaterialState`。 | Texture Path や Metallic を条件にする。 |
| Animation 状態を見る | Animation / Animator、C++ Script。 | `GetAnimationState`。 | 再生中 Clip 名で処理分岐。 |
| AI 視界で追跡 | AI 視界センサー、C++ Script。 | `GetAiSensorState`。 | `direction` と `distance` を使う。 |
| 音声コマンド | AI 音声コマンド、C++ Script。 | `GetAiSensorState`。 | `command` と `commandId` の分岐。 |
| UI Button から呼ぶ | UI Button、C++ Script。 | `EditorScript_InvokeAction` または Button Event。 | 関数名を文字列で対応付ける。 |

## 60. 最終ドキュメントに不足がないかの追加チェック

次の質問に答えられないページは、使用者向けとして未完成である。

- どこから追加するのか。
- 追加後、どの Component が一緒に必要か。
- 何を入力すれば最小確認できるか。
- その値の単位は何か。
- Play 前に変わるのか、Play 中だけ変わるのか。
- Scene View と Game View のどちらで確認するのか。
- 保存されるのか。
- C++ から読めるのか。
- C++ から書けるのか。
- 失敗した時に Console へ何が出るのか。
- 既知の不具合があるか。
- 現在の実装レベルは何か。

## 61. これ以上追加するなら必要な大項目

さらに親切にするなら、次も別章として調査する。

- 用語集。
- ショートカット一覧。
- メニュー一覧。
- 右クリックメニュー一覧。
- Inspector 共通操作。
- 複数選択時の挙動。
- Undo / Redo 対応表。
- Scene と Game View の違い。
- Asset 参照切れの復旧方法。
- Build した exe に含めるファイル。
- 外部ライブラリの License 表記。
- バージョンアップ時の移行手順。
- 既知の不具合一覧。
- 今後実装予定一覧。

## 62. Component 全一覧は必須成果物

Component は使用者が最も直接触る機能なので、全一覧と詳細ページを必ず作る。
「代表的なものだけ」「実装済みだけ」「よく使うものだけ」に絞らない。

### 62.1 Component 一覧に必要な列

Component 一覧ページは、最低限次の列を持つ。

| 列 | 内容 |
| --- | --- |
| カテゴリ | Add Component Popup 上の分類。 |
| 表示名 | Inspector / Add Component に出る日本語名。 |
| 内部型 | `EditorComponentType` の名前。 |
| 概要 | 何をする Component か。 |
| 主な使用場面 | ゲーム制作でどんな時に使うか。 |
| 必要 Component | Rigidbody に Collider が必要、UI Button に Canvas が必要など。 |
| 実装状態 | 使用可能 / 一部使用可能 / 設定のみ / 未実装 / 実験的 / 既知不具合あり。 |
| Play 時の動作 | Play 中に何が起きるか。 |
| 保存 | Scene / Prefab に保存されるか。 |
| C++ API | C++ Script から読める / 操作できる API。 |
| 主な制限 | 現在できないこと。 |

### 62.2 Component 詳細ページに必要な項目

全 Component に、次の詳細ページを作る。

```text
Component 名
├─ 概要
├─ 追加場所
├─ 使用場面
├─ 最小使用手順
├─ 必要 Component
├─ Inspector 項目
├─ 初期値
├─ 値の単位と範囲
├─ Play 時の動作
├─ 編集時の動作
├─ Scene / Prefab 保存
├─ C++ Script API
├─ 他 Component との関係
├─ 使用例
├─ よくある問題
├─ 現在の制限
└─ 実装状態
```

Inspector 項目は、表示名だけではなく `EditorComponent` の内部フィールド名、初期値、保存有無、Runtime 使用有無まで書く。

## 63. 現在 Add Component に出る Component 全一覧

この一覧は `Source/Engine/Editor/EditorInspectorPanel.cpp` の `kComponentAddEntries` を基準にする。
ChatGPT Work へ渡す調査データでは、この表に「概要、Inspector 項目、実装状態、制限」を追加して完成させる。

| カテゴリ | 表示名 | 内部型 |
| --- | --- | --- |
| 基本 | トランスフォーム | `Transform` |
| 基本 | レクトトランスフォーム | `RectTransform` |
| 基本 | キャンバス | `Canvas` |
| 基本 | ゲームオブジェクト + スクリプト | `Script` |
| 基本 | モノビヘイビア | `MonoBehaviour` |
| 描画・レンダリング | メッシュフィルター | `MeshFilter` |
| 描画・レンダリング | メッシュレンダラー | `ModelRenderer` |
| 描画・レンダリング | スキンメッシュレンダラー | `SkinnedMeshRenderer` |
| 描画・レンダリング | スプライトレンダラー | `SpriteRenderer` |
| 描画・レンダリング | ラインレンダラー | `LineRenderer` |
| 描画・レンダリング | トレイルレンダラー | `TrailRenderer` |
| 描画・レンダリング | ビルボードレンダラー | `BillboardRenderer` |
| 描画・レンダリング | キャンバスレンダラー | `CanvasRenderer` |
| 描画・レンダリング | パーティクルシステムレンダラー | `ParticleSystemRenderer` |
| 描画・レンダリング | Ocean | `Ocean` |
| カメラ | カメラ | `Camera` |
| カメラ | オーディオリスナー | `AudioListener` |
| カメラ | フレアレイヤー | `FlareLayer` |
| カメラ | Cinemachine カメラ | `CinemachineCamera` |
| ライト・環境 | ライト | `Light` |
| ライト・環境 | リフレクションプローブ | `ReflectionProbe` |
| ライト・環境 | ライトプローブグループ | `LightProbeGroup` |
| ライト・環境 | ライトプローブプロキシボリューム | `LightProbeProxyVolume` |
| ライト・環境 | ボリューム | `Volume` |
| ライト・環境 | ポストプロセス | `PostProcess` |
| ライト・環境 | 環境 | `Environment` |
| 3D物理 | リジッドボディ | `RigidBody` |
| 3D物理 | 箱の当たり判定 | `BoxCollider` |
| 3D物理 | 球の当たり判定 | `SphereCollider` |
| 3D物理 | カプセル当たり判定 | `CapsuleCollider` |
| 3D物理 | メッシュ当たり判定 | `MeshCollider` |
| 3D物理 | Auto Convex Collision | `AutoConvexCollision` |
| 物理 | Buoyancy | `Buoyancy` |
| 3D物理 | 地形の当たり判定 | `TerrainCollider` |
| 3D物理 | 車輪の当たり判定 | `WheelCollider` |
| 3D物理 | キャラクターコントローラー | `CharacterController` |
| 3D物理 | コンスタントフォース | `ConstantForce` |
| 3D物理 | 空気力学 | `Aerodynamics` |
| 3D物理 | 風ゾーン | `WindZone` |
| 3D物理 | 重力場 | `GravityField` |
| 3D物理 | 回転座標系 | `RotatingFrame` |
| 3D物理 | 流体ボリューム | `FluidVolume` |
| 3D物理 | ばね力 | `SpringForce` |
| 3D物理 | ロープ拘束 | `RopeConstraint` |
| 3D物理 | ねじりばね | `TorsionSpring` |
| 3D物理 | 推進力 | `Thruster` |
| 3D物理 | 滑車拘束 | `PulleyConstraint` |
| 3D物理 | 物理サーボ | `PhysicsServo` |
| 3D物理 | 渦流場 | `VortexField` |
| 3D物理 | 圧力場 | `PressureField` |
| 3D物理 | サスペンション | `Suspension` |
| 3D物理 | 姿勢安定化 | `UprightStabilizer` |
| 3D物理 | 電磁気ボディ | `ElectromagneticBody` |
| 3D物理 | 電磁場 | `ElectromagneticField` |
| 3D物理 | ヒンジジョイント | `HingeJoint` |
| 3D物理 | 固定ジョイント | `FixedJoint` |
| 3D物理 | スプリングジョイント | `SpringJoint` |
| 3D物理 | コンフィギュラブルジョイント | `ConfigurableJoint` |
| 3D物理 | キャラクタージョイント | `CharacterJoint` |
| 2D物理 | リジッドボディ 2D | `RigidBody2D` |
| 2D物理 | 四角の当たり判定 2D | `BoxCollider2D` |
| 2D物理 | 円の当たり判定 2D | `CircleCollider2D` |
| 2D物理 | カプセル当たり判定 2D | `CapsuleCollider2D` |
| 2D物理 | 多角形の当たり判定 2D | `PolygonCollider2D` |
| 2D物理 | 線の当たり判定 2D | `EdgeCollider2D` |
| 2D物理 | 複合当たり判定 2D | `CompositeCollider2D` |
| 2D物理 | タイルマップ当たり判定 2D | `TilemapCollider2D` |
| 2D物理 | カスタム当たり判定 2D | `CustomCollider2D` |
| 2D物理 | ディスタンスジョイント 2D | `DistanceJoint2D` |
| 2D物理 | ヒンジジョイント 2D | `HingeJoint2D` |
| 2D物理 | スプリングジョイント 2D | `SpringJoint2D` |
| 2D物理 | 固定ジョイント 2D | `FixedJoint2D` |
| 2D物理 | スライダージョイント 2D | `SliderJoint2D` |
| 2D物理 | ホイールジョイント 2D | `WheelJoint2D` |
| 2D物理 | プラットフォームエフェクター 2D | `PlatformEffector2D` |
| 2D物理 | サーフェスエフェクター 2D | `SurfaceEffector2D` |
| 2D物理 | エリアエフェクター 2D | `AreaEffector2D` |
| 2D物理 | ポイントエフェクター 2D | `PointEffector2D` |
| 2D物理 | 浮力エフェクター 2D | `BuoyancyEffector2D` |
| アニメーション | アニメーター | `Animator` |
| アニメーション | アニメーション | `Animation` |
| アニメーション | アバターマスク | `AvatarMask` |
| アニメーション | プレイアブルディレクター | `PlayableDirector` |
| アニメーション | エイム制約 | `AimConstraint` |
| アニメーション | ルックアット制約 | `LookAtConstraint` |
| アニメーション | 親制約 | `ParentConstraint` |
| アニメーション | 位置制約 | `PositionConstraint` |
| アニメーション | 回転制約 | `RotationConstraint` |
| アニメーション | スケール制約 | `ScaleConstraint` |
| オーディオ | オーディオソース | `AudioSource` |
| オーディオ | オーディオリスナー | `AudioListener` |
| オーディオ | オーディオリバーブゾーン | `AudioReverbZone` |
| オーディオ | オーディオローパスフィルター | `AudioLowPassFilter` |
| オーディオ | オーディオハイパスフィルター | `AudioHighPassFilter` |
| オーディオ | オーディオエコーフィルター | `AudioEchoFilter` |
| オーディオ | オーディオディストーションフィルター | `AudioDistortionFilter` |
| オーディオ | オーディオリバーブフィルター | `AudioReverbFilter` |
| オーディオ | オーディオコーラスフィルター | `AudioChorusFilter` |
| UI | キャンバス | `Canvas` |
| UI | キャンバススケーラー | `CanvasScaler` |
| UI | グラフィックレイキャスター | `GraphicRaycaster` |
| UI | イメージ | `Image` |
| UI | Raw イメージ | `RawImage` |
| UI | テキスト | `Text` |
| UI | TextMeshPro UGUI | `TextMeshProUGUI` |
| UI | ボタン | `Button` |
| UI | トグル | `Toggle` |
| UI | スライダー | `Slider` |
| UI | スクロールバー | `Scrollbar` |
| UI | ドロップダウン | `Dropdown` |
| UI | TMP ドロップダウン | `TMPDropdown` |
| UI | 入力フィールド | `InputField` |
| UI | TMP 入力フィールド | `TMPInputField` |
| UI | スクロールレクト | `ScrollRect` |
| UI | マスク | `Mask` |
| UI | レクトマスク 2D | `RectMask2D` |
| UI | 水平レイアウトグループ | `HorizontalLayoutGroup` |
| UI | 垂直レイアウトグループ | `VerticalLayoutGroup` |
| UI | グリッドレイアウトグループ | `GridLayoutGroup` |
| UI | コンテンツサイズフィッター | `ContentSizeFitter` |
| UI | アスペクト比フィッター | `AspectRatioFitter` |
| UI | レイアウトエレメント | `LayoutElement` |
| UI | Scene ボタン | `SceneButton` |
| UI | 値バインディング | `UIValueBinding` |
| 入力・イベント | イベントシステム | `EventSystem` |
| 入力・イベント | スタンドアロン入力モジュール | `StandaloneInputModule` |
| 入力・イベント | Input System UI 入力モジュール | `InputSystemUIInputModule` |
| 入力・イベント | プレイヤー入力 | `PlayerInput` |
| 入力・イベント | プレイヤー入力マネージャー | `PlayerInputManager` |
| 入力・イベント | タッチ入力モジュール | `TouchInputModule` |
| 入力・イベント | 入力 | `Input` |
| 入力・イベント | Timeline Event | `TimelineEvent` |
| 入力・イベント | Threshold State | `ThresholdState` |
| ゲームプレイ | ローカル移動 | `LocalMove` |
| ゲームプレイ | ローリング移動 | `RollingMove` |
| ゲームプレイ | 自由移動/回転 | `FreeTransform` |
| ゲームプレイ | レール移動 | `RailMovement` |
| ゲームプレイ | 体力 | `Health` |
| ゲームプレイ | Wave Spawner | `WaveSpawner` |
| ナビゲーション | NavMesh エージェント | `NavigationAgent` |
| ナビゲーション | NavMesh 障害物 | `NavMeshObstacle` |
| ナビゲーション | NavMesh サーフェス | `NavMeshSurface` |
| ナビゲーション | NavMesh モディファイア | `NavMeshModifier` |
| ナビゲーション | NavMesh モディファイアボリューム | `NavMeshModifierVolume` |
| ナビゲーション | NavMesh リンク | `NavMeshLink` |
| AI | 行動ツリー | `AIBehaviorTree` |
| AI | 共有データ（Blackboard） | `AIBehaviorBlackboard` |
| AI | 条件分岐（Selector） | `AIBehaviorSelector` |
| AI | 順番実行（Sequence） | `AIBehaviorSequence` |
| AI | 実行処理（Task） | `AIBehaviorTask` |
| AI | 条件装飾（Decorator） | `AIBehaviorDecorator` |
| AI | 状態制御 | `AIStateMachine` |
| AI | 状態 | `AIState` |
| AI | 状態遷移 | `AIStateTransition` |
| AI | 目標計画 | `AIGoapPlanner` |
| AI | 目標条件 | `AIGoapGoal` |
| AI | 計画行動 | `AIGoapAction` |
| AI | 世界状態 | `AIGoapWorldState` |
| AI | タスク計画 | `AIHtnPlanner` |
| AI | タスク領域 | `AIHtnDomain` |
| AI | タスク | `AIHtnTask` |
| AI | タスク分解 | `AIHtnMethod` |
| AI | 経路探索エージェント | `AIPathfindingAgent` |
| AI | グリッド経路 | `AIMicroPatherGrid` |
| AI | ナビメッシュ生成 | `AIRecastNavMeshBuilder` |
| AI | 群衆エージェント | `AIRecastCrowdAgent` |
| AI | 経路要求 | `AIPathRequest` |
| AI | 動的障害物 | `AIDynamicObstacle` |
| AI | 操舵エージェント | `AISteeringAgent` |
| AI | 接近操舵 | `AISeekSteering` |
| AI | 逃走操舵 | `AIFleeSteering` |
| AI | 到着操舵 | `AIArriveSteering` |
| AI | 追跡操舵 | `AIPursuitSteering` |
| AI | 徘徊操舵 | `AIWanderSteering` |
| AI | 障害物回避操舵 | `AIObstacleAvoidanceSteering` |
| AI | 群れ操舵 | `AIFlockSteering` |
| AI | 視界センサー | `AIVisionSensor` |
| AI | 画像入力カメラ | `AIOpenCvCamera` |
| AI | 画像物体検出 | `AIOpenCvObjectDetector` |
| AI | 画像色追跡 | `AIOpenCvColorTracker` |
| AI | 動きセンサー | `AIMotionSensor` |
| AI | Whisper 音声認識 | `AIWhisperSpeechRecognizer` |
| AI | 音声コマンド | `AIVoiceCommand` |
| エフェクト | パーティクルシステム | `ParticleSystem` |
| エフェクト | ビジュアルエフェクト | `VisualEffect` |
| エフェクト | トレイルレンダラー | `TrailRenderer` |
| エフェクト | ラインレンダラー | `LineRenderer` |
| エフェクト | レンズフレア | `LensFlare` |
| エフェクト | プロジェクター | `Projector` |
| エフェクト | デカールプロジェクター | `DecalProjector` |
| 地形・タイルマップ | テレイン | `Terrain` |
| 地形・タイルマップ | 地形の当たり判定 | `TerrainCollider` |
| 地形・タイルマップ | タイルマップ | `Tilemap` |
| 地形・タイルマップ | タイルマップレンダラー | `TilemapRenderer` |
| 地形・タイルマップ | タイルマップ当たり判定 2D | `TilemapCollider2D` |
| 地形・タイルマップ | グリッド | `Grid` |
| 地形・タイルマップ | フォリッジ | `Foliage` |
| FeelKit | FeelKit 触覚ソース | `HapticSource` |

### 63.1 一覧で重複している Component の扱い

同じ内部型が複数カテゴリに出る場合がある。
例として `Canvas`、`AudioListener`、`TerrainCollider`、`TilemapCollider2D`、`TrailRenderer`、`LineRenderer` がある。

サイトでは、次のように扱う。

- 一覧では Add Component の見え方を優先し、重複もそのまま載せる。
- 詳細ページは内部型ごとに 1 ページへ統合する。
- 重複カテゴリからは同じ詳細ページへリンクする。
- 「この Component は複数カテゴリから追加できます」と注記する。

### 63.2 Component 詳細調査で必ず確認するファイル

| 目的 | 確認先 |
| --- | --- |
| 内部型とフィールド | `Source/Engine/Editor/EditorScene.h` |
| 追加メニューのカテゴリと表示名 | `Source/Engine/Editor/EditorInspectorPanel.cpp` の `kComponentAddEntries` |
| 初期値 | `Source/Engine/Editor/EditorScene.cpp` の `CreateComponent` |
| Inspector 表示 | `Source/Engine/Editor/EditorInspectorPanel.cpp` |
| 保存と読み込み | Scene Save / Load 実装 |
| Runtime 実処理 | `Source/Engine/Editor/*Manager.cpp` |
| C++ API | `Source/Engine/Core/EditorScriptApi.h` と `EditorScriptManager.cpp` |
| 描画 Shader | `Assets/Shaders` |

## 64. Component 詳細ページの完成判定

Component 詳細ページは、次を満たすまで完成扱いにしない。

- Add Component からの追加手順が書いてある。
- Inspector の全項目が、表示名、内部フィールド名、初期値、単位、範囲付きで書いてある。
- その Component だけで動くか、他 Component が必要か書いてある。
- Play 前に確認できることと、Play 中にしか確認できないことが分かれている。
- 保存される値と保存されない値が分かれている。
- C++ Script から読める値、書ける値、イベントで受け取れる値が分かれている。
- 実装状態が断定されている。
- 未実装または設定のみの機能は、そう明記されている。
- 失敗時に最初に見る Console / Inspector / Asset Path が書いてある。

## 65. 2026-07-19 時点の Animation / Effect 実装確定情報

この章は調査予定ではなく、現在のコードへ接続済みの機能をサイト作成担当へ渡すための確定情報である。
説明ページでは、ここに書かれた実働範囲と制限を分けて掲載する。

### 65.1 Animation Clip を作成して開く手順

1. Hierarchy で動かしたい GameObject を選択する。
2. 上部メニューの `ウィンドウ` から `アニメーション` を開く。
3. `新規Clip` を押す。
4. `Assets/Animation/NewAnimationClip.animclip` が作られる。同名がある場合は末尾へ番号が付く。
5. 選択 GameObject に Animation Component がなければ自動追加され、新しい Clip が設定される。
6. Timeline の現在時間を選び、`● 自動記録`を押す。
7. Scene の Gizmo または Inspector で位置、回転、スケールなどを変更する。
8. 変更した項目だけ Track と Keyframe が自動作成される。
9. `プレビュー再生`で動きを確認し、菱形Keyの時刻や値を微調整してから`保存`を押す。

Project の `+ > Animation Clip` から任意フォルダーへ作る従来手順も使用できる。

既存の `.animclip` は Project で選択してから Animation Window を開く。
GameObject の Animation Component に `.animclip` が設定済みの場合は、GameObject 選択からも自動的に開く。
未保存の変更がある間は別 Asset へ自動切替しない。これは編集内容の消失を防ぐためである。

### 65.2 Animation Window のツールバー

| 表示項目 | 初期値 | 単位・範囲 | 実行内容 |
| --- | --- | --- | --- |
| Clip | 未選択 | Asset Path | 現在開いている `.animclip` と未保存を表す `*` を表示する。 |
| 新規Clip | - | - | `Assets/Animation`へ空Clipを作り、選択GameObjectへ設定する。使用者がJSONを書く必要はない。 |
| 保存 | - | - | 現在の Clip を同じパスへ JSON 保存する。保存先が未選択なら Console に警告を出す。 |
| 再読込 | - | - | Disk 上の内容を読み直し、未保存の編集コピーを置き換える。 |
| 選択オブジェクトへ設定 | - | - | 選択 GameObject に Animation Component を追加または再利用し、Clip Path を設定する。 |
| プレビュー再生 / 一時停止 | 停止 | - | Edit Mode のまま Timeline Preview を進める。上部のゲーム実行用Playとは別である。 |
| 停止 | - | - | Preview と記録を止め、時間を 0 秒へ戻し、GameObject を Preview 前の状態へ復元する。 |
| ● 自動記録 | 停止 | bool | InspectorまたはGizmoで変化した項目を検出し、TrackとKeyを現在時刻へ自動作成する。 |
| 現在の姿勢をキー | - | - | Transformの位置・回転・スケール全9軸を現在時刻へ一括記録する。 |
| 名前 | `NewAnimationClip` | UTF-8 文字列 | Clip の表示名。ファイル名は自動変更しない。 |
| 長さ | `1.0` | 秒、`0.01`から`3600.0` | Timeline と Runtime Loop の長さ。 |
| サンプルレート | `30` | fps、`1`から`240` | Key 追加時の同一時刻許容幅と時間目盛りに使う。Runtime は連続時間で補間する。 |
| ループ | true | bool | 終端後に 0 秒へ戻るか。false の場合は終端で停止する。 |
| 現在時間 | `0.0` | 秒、`0`からClip長 | Playhead を移動し、選択 GameObjectへその時刻の値をPreviewする。 |
| 表示倍率 | `120` | pixel/秒、`40`から`500` | Timeline の横方向表示だけを変更し、Clipデータは変更しない。 |

Preview は GameObject 全体を開始前に退避する。
停止、Window Close、Preview 対象変更時には退避値へ戻す。
加算 Track を毎フレーム累積しないよう、各 Sample 前に退避値を基準へ戻してから全 Track を評価する。

### 65.3 使用可能な Property Track 全一覧

| UI表示 | JSON `property` | 値・単位 | 必要な Component |
| --- | --- | --- | --- |
| Transform / 位置 X | `Transform.LocalPositionX` | float、エンジン座標 | Transform |
| Transform / 位置 Y | `Transform.LocalPositionY` | float、エンジン座標 | Transform |
| Transform / 位置 Z | `Transform.LocalPositionZ` | float、エンジン座標 | Transform |
| Transform / 回転 X | `Transform.LocalRotationX` | 度 | Transform |
| Transform / 回転 Y | `Transform.LocalRotationY` | 度 | Transform |
| Transform / 回転 Z | `Transform.LocalRotationZ` | 度 | Transform |
| Transform / スケール X | `Transform.LocalScaleX` | 倍率 | Transform |
| Transform / スケール Y | `Transform.LocalScaleY` | 倍率 | Transform |
| Transform / スケール Z | `Transform.LocalScaleZ` | 倍率 | Transform |
| ライト / 強さ | `Light.Intensity` | float | Light |
| ライト / 範囲 | `Light.Range` | エンジン座標 | Light |
| マテリアル / ベースカラー R | `Material.BaseColorR` | 線形色、通常`0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / ベースカラー G | `Material.BaseColorG` | 線形色、通常`0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / ベースカラー B | `Material.BaseColorB` | 線形色、通常`0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / メタリック | `Material.Metallic` | 通常`0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / 粗さ | `Material.Roughness` | 通常`0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / アルファ | `Material.Alpha` | `0.0`から`1.0` | ModelRenderer または SpriteRenderer |
| マテリアル / 放射の強さ | `Material.EmissionStrength` | `0.0`以上 | ModelRenderer または SpriteRenderer |
| マテリアル / 放射色 R | `Material.EmissionColorR` | 線形色 | ModelRenderer または SpriteRenderer |
| マテリアル / 放射色 G | `Material.EmissionColorG` | 線形色 | ModelRenderer または SpriteRenderer |
| マテリアル / 放射色 B | `Material.EmissionColorB` | 線形色 | ModelRenderer または SpriteRenderer |

同じ Property の Track は 1 Clip 内に 1 本だけ作成できる。
対象 Component がない Track は Preview と Runtime の両方で読み飛ばされ、別 Component を自動追加しない。
回転は使用者向け UI と `.animclip` では度、Scene 内部へ反映する時にラジアンへ変換する。

### 65.4 Track の適用方法

| 適用方法 | JSON値 | 計算 | 使用例 |
| --- | --- | --- | --- |
| 上書き | `Override` | `結果 = カーブ値` | 座標を指定位置へ動かす、色を決める。 |
| 加算 | `Additive` | `結果 = PreviewまたはPlay開始値 + カーブ値` | 現在位置を保った上下動、現在角度から一回転。 |
| 乗算 | `Multiply` | `結果 = PreviewまたはPlay開始値 * カーブ値` | 元スケールや元色の明るさを比率で変える。 |

加算と乗算の基準は Preview 開始時または Play 開始時の値である。
途中フレームの結果を次フレームの基準にはしない。

### 65.5 Keyframe と補間

| 項目 | 意味 |
| --- | --- |
| 時刻 | Clip 開始からの秒数。`0`からClip長へ制限される。 |
| 値 | Trackが対象とする単一float値。回転だけは度。 |
| 入る接線 | Cubic HermiteでKeyへ入る傾き。単位は値/秒。 |
| 出る接線 | Cubic HermiteでKeyから出る傾き。単位は値/秒。 |
| 一定 | 次Keyの時刻まで現在Keyの値を維持する。瞬間切替向け。 |
| 直線 | 2 Key間を一定比率で線形補間する。 |
| 滑らか | 値と前後の接線を使ったCubic Hermite補間。加減速向け。 |

Key は Timeline 上の菱形を左クリックして選択する。
選択したまま横へドラッグすると時刻を変更でき、Mouseを離すと時間順へ再整列される。
`現在値をキー追加`は選択 GameObject の現在値を現在時間へ追加する。
同じ Sample 時刻付近に既存Keyがあれば新規作成せず更新する。
Key Editor の`このキーを削除`で選択Keyを削除する。

### 65.6 記録モード

`● 自動記録`は先にTrackを作る必要がない。
記録開始時に選択GameObjectの対応値を基準として保持し、その後に変化した項目だけをTrack化する。
例えばGizmoでX方向だけ動かした場合は`Transform / 位置 X`だけが作られ、Y、Z、MaterialなどのKeyは作られない。
途中時刻で初めて変更したTrackには、変化前の値を持つ0秒Keyも自動追加される。これにより最初のKeyが1秒の場合でも、0秒から1秒まで元の姿勢を保って補間できる。
同じ時刻付近にKeyがある場合は新規追加ではなく値を更新する。

Timelineをクリックまたは`現在時間`を変更すると、その時刻の姿勢がSceneへ表示される。
記録中にPlayheadを移動しただけではKeyを作らず、その後にGizmoまたはInspectorを変更した時だけ記録する。
選択GameObjectを記録途中で変えると、別オブジェクトへの誤記録を防ぐため自動停止する。

上部のゲーム実行用`Play`中にも自動記録は使用できるが、Runtime Physics、Script、Animationが変更した値も区別せず記録対象になる。
手でキーフレームを編集する場合はAnimation Windowの`プレビュー再生`を使用する。

推奨手順:

1. GameObjectを選ぶ。
2. `新規Clip`を押す。
3. 現在時間を`0.0`秒にし、`● 自動記録`を押してGizmoで開始位置を調整する。
4. 現在時間を`1.0`秒へ移動し、GizmoでYを`3.0`へ動かす。
5. 現在時間を`2.0`秒へ移動し、GizmoでYを`0.0`へ戻す。
6. `● 記録中`を押して記録を終了し、`プレビュー再生`で往復を確認する。
7. 必要なKeyをCubic Hermiteへ変え、接線を調整する。
8. 保存する。

### 65.7 Animation Event

`現在時間へイベント追加`で、現在のPlayhead位置へEventを作る。

| 項目 | 型 | 実行内容 |
| --- | --- | --- |
| イベント時刻 | float秒 | 前フレームからこの時刻を通過した時に1回発火する。 |
| イベント名 | UTF-8文字列 | Generated側を経由してユーザーScriptの`OnAnimationEvent(...)`へ渡す。 |
| Effectパス | `.effect` Path | 空でなければ同時にEffectを再生する。 |
| ローカル発生位置 | Vector3 | 所有GameObject位置へ加えるEffect発生Offset。 |

Loopで終端から先頭へ戻った場合も、通過区間に含まれるEventを再評価する。
Script Componentが複数ある場合は、同じGameObjectへ付いた各DLLに通知する。

```cpp
void PlayerScript::OnAnimationEvent(
    const EditorScriptAnimationEvent& animationEvent) {

    if (std::string(animationEvent.name) == "Footstep") {
        // 足音処理をここへ記述する。
    }
}
```

`animationEvent`内の文字列PointerはCallback中だけ有効である。
`name`や`effectAssetPath`を後で使う場合は、関数内で`std::string`へコピーする。

### 65.8 Animation Component と Animator Component の使い分け

| Component | 用途 | Asset | 実装状態 |
| --- | --- | --- | --- |
| Animation | 1つのFBX Clip、`.animclip`、簡易プロシージャルAnimationを再生する。 | FBXまたは`.animclip` | 実働。再生、停止、速度、Loop、時間変更に対応。 |
| Animator | State Machine、Transition、Blend Tree、Action、Root Motion、Eventを実行する。 | `.animgraph`とモデル内Clip | 実働。ただしBone Skinningは制限あり。 |

Animationの基本手順:

1. GameObjectへ`アニメーション > アニメーション`を追加する。
2. ProjectでFBXまたは`.animclip`を選択する。
3. `選択中 Animation Clip を設定`を押す。
4. 速度、ループ、自動再生を設定する。
5. Playを押す。
6. Play中はInspectorの再生中、現在時間、先頭から再生、停止を使用できる。

Animatorの基本手順:

1. Animationを持つFBXモデルをGameObjectへ設定する。
2. Projectの`+ > Animation Graph`で`.animgraph`を作る。
3. GameObjectへ`アニメーション > アニメーター`を追加する。
4. Projectで`.animgraph`を選び、Animatorの選択Asset設定Buttonを押す。
5. Graph内のClip番号がFBX内Clip一覧と一致することを確認する。
6. C++ ScriptからParameterを更新する。
7. Play中にInspectorの現在StateとParameter値を確認する。

### 65.9 Animation Graph のデータと動作

Parameter Typeは`Float`、`Int`、`Bool`、`Trigger`、`Vector2`、`Vector3`に対応する。
Transition条件は`Greater`、`Less`、`Equal`、`NotEqual`、`True`、`False`、`Triggered`に対応する。
Triggerは遷移で消費される。遷移前に取り消す場合は`ResetAnimatorTrigger`を使う。

Blend Tree:

| 種類 | 主な入力 | 動作 |
| --- | --- | --- |
| Clip | Clip番号 | 単一Clipを再生する。 |
| Blend1D | Float Parameter | Sample位置の前後2つを線形合成する。速度によるIdle/Walk/Run向け。 |
| Blend2DDirectional | X/Y Parameter | 入力方向と大きさから周囲Sampleを合成する。前後左右移動向け。 |
| Blend2DCartesian | X/Y Parameter | 2D座標距離に基づき近傍Sampleを合成する。方向以外の2軸値向け。 |
| Direct | 各SampleのWeight Parameter | Scriptが指定した各Weightを直接使用する。 |

Actionは攻撃、被弾、回避など一時ClipをBase Stateへ重ねる。
`priority`が大きいActionを優先し、`blendIn`と`blendOut`は秒、`playbackSpeed`は再生倍率、`loop`は終了後に継続するかを表す。

### 65.10 C++ Script Animation API 全一覧

すべて`EditorScriptRuntimeApi`の関数Pointerとして呼ぶ。
`gameObjectId`は原則としてLifecycle関数へ渡された自分自身のIDを使用する。
Animator Parameter系はAnimator Componentと読み込み成功した`.animgraph`が必要であり、名前と型が一致しない場合は`false`を返す。

| API | 引数 | 戻り値 | 使用タイミング・副作用 |
| --- | --- | --- | --- |
| `GetAnimationState` | GameObject ID | `EditorScriptAnimationState` | Update等。Component有無、再生状態、Loop、速度、時間、Clip名をまとめて読む。 |
| `SetAnimatorFloat` | ID、Parameter名、float | bool | AnimatorのFloatを変更。遷移やBlend1Dに反映。 |
| `SetAnimatorInt` | ID、Parameter名、int32 | bool | AnimatorのIntを変更。 |
| `SetAnimatorBool` | ID、Parameter名、bool | bool | AnimatorのBoolを変更。 |
| `SetAnimatorTrigger` | ID、Parameter名 | bool | Triggerを有効化。遷移評価時に消費される。 |
| `ResetAnimatorTrigger` | ID、Parameter名 | bool | 未消費Triggerをfalseへ戻す。 |
| `SetAnimatorVector2` | ID、Parameter名、Vector2 Pointer | bool | 2D Parameterを変更。PointerはNull不可。 |
| `SetAnimatorVector3` | ID、Parameter名、Vector3 Pointer | bool | 3D Parameterを変更。PointerはNull不可。 |
| `GetAnimatorFloat` | ID、Parameter名、float出力Pointer | bool | 型一致した現在値を出力。失敗時の出力値は使用しない。 |
| `GetAnimatorInt` | ID、Parameter名、int32出力Pointer | bool | 型一致した現在値を出力。 |
| `GetAnimatorBool` | ID、Parameter名、bool出力Pointer | bool | BoolまたはTriggerの現在値を出力。 |
| `GetAnimatorVector2` | ID、Parameter名、Vector2出力Pointer | bool | Vector2の現在値を出力。 |
| `GetAnimatorVector3` | ID、Parameter名、Vector3出力Pointer | bool | Vector3の現在値を出力。 |
| `GetAnimatorStateName` | ID、char Buffer、Buffer容量 | bool | 現在State名をNull終端でコピーする。容量はbyte数、0以下は失敗。 |
| `PlayAnimation` | ID | bool | Animation Componentを0秒から再生する。 |
| `StopAnimation` | ID | bool | 再生中Animationを停止し、管理値を開始前へ戻す。再生していなければfalse。 |
| `IsAnimationPlaying` | ID | bool | AnimationまたはAnimatorが実行中か。 |
| `GetAnimationTime` | ID | float秒 | Animationまたは現在Animator Stateの再生秒。見つからなければ0。 |
| `SetAnimationTime` | ID、float秒 | bool | AnimationまたはAnimatorの再生位置を変更。負値は0へ制限。 |
| `SetAnimationSpeed` | ID、float倍率 | bool | Animation Componentの再生倍率を変更する。 |
| `PlayAnimationAction` | ID、Clip番号、BlendIn秒、BlendOut秒、速度、Priority、Loop | bool | Animatorの一時Actionを開始。Clip番号不正などでfalse。 |

移動入力を方向Blendへ渡す例:

```cpp
void MovementScript::Update(float deltaTime) {
    const int32_t gameObjectId = GetGameObjectId();
    const EditorScriptRuntimeApi* runtimeApi =
        EditorNativeScriptRuntime::GetRuntimeApi();

    if (runtimeApi == nullptr) {
        return;
    }

    const EditorScriptVector2 move = runtimeApi->GetActionVector2(
        gameObjectId,
        "Player",
        "Move");

    runtimeApi->SetAnimatorFloat(gameObjectId, "MoveX", move.x);
    runtimeApi->SetAnimatorFloat(gameObjectId, "MoveY", move.y);
    runtimeApi->SetAnimatorFloat(
        gameObjectId,
        "Speed",
        std::sqrt(move.x * move.x + move.y * move.y));

    (void)deltaTime;
}
```

TriggerとActionの例:

```cpp
if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Attack")) {
    runtimeApi->SetAnimatorTrigger(gameObjectId, "Attack");

    runtimeApi->PlayAnimationAction(
        gameObjectId,
        9,
        0.08f,
        0.12f,
        1.0f,
        10,
        false);
}
```

State名取得例:

```cpp
char stateName[64]{};
if (runtimeApi->GetAnimatorStateName(gameObjectId, stateName, 64)) {
    runtimeApi->Log(stateName);
}
```

### 65.11 Effect Asset と C++ API

Effect AssetはProjectの`+ > Effect Asset`から`.effect`として作成する。
ParticleSystemまたはVisualEffect ComponentへAsset Pathを設定して使う。
現在の実行系はCPU Particleであり、Effekseer形式はこの章の対応対象ではない。

| `.effect`項目 | 初期値 | 単位・意味 |
| --- | --- | --- |
| renderAssetPath | 空 | 1 Particleの描画に使うFBX/OBJ。空なら既定形状。 |
| billboardMode | 0 | Render Asset未設定時の板の向き。0=Camera Facing、1=Y軸固定、2=Velocity Facing、3=World XY固定。 |
| billboardStretch | 1.0 | Velocity Facing時に速度方向へ伸ばす倍率。0.01以上。 |
| duration | 2.0 | Emitter 1周の秒数。 |
| startDelay | 0.0 | 再生要求から発生開始までの秒数。 |
| emissionRate | 10.0 | 1秒当たりの継続発生数。 |
| lifetime | 1.0 | Particle基本寿命秒。 |
| speed | 1.0 | 初速度の大きさ。 |
| startSize / endSize | 0.2 / 0.0 | 発生時と寿命終了時の一様Scale。 |
| gravity | 0.0 | World下方向への加速度。 |
| drag | 0.0 | 1秒当たりの速度減衰係数。 |
| rotationSpeed | 0.0 | Y軸回転速度、radian/秒。 |
| shapeRadius | 0.5 | Sphere/Cone発生半径。 |
| shapeAngleDegrees | 25.0 | Cone広がり角、度。 |
| speedRandomness | 0.0 | 初速度へ加える比率乱数。 |
| lifetimeRandomness | 0.0 | 寿命へ加える比率乱数。 |
| sizeRandomness | 0.0 | Sizeへ加える比率乱数。 |
| startAlpha / endAlpha | 1.0 / 0.0 | 発生時と終了時の不透明度。 |
| emissionStrength | 1.0 | Particle Materialの放射強度。 |
| endSpeedMultiplier | 1.0 | 終了時速度を開始時の何倍にするか。 |
| noiseStrength | 0.0 | 乱流Noiseが速度へ加える加速度。 |
| noiseFrequency | 1.0 | 乱流Noiseの時間周波数。 |
| collisionBounce | 0.35 | Ground衝突後に残す垂直速度率。 |
| collisionFriction | 0.2 | Ground衝突で失う水平速度率。 |
| maxCount | 256 | 同一Emitterの最大Particle数。 |
| burstCount | 0 | 周回開始時にまとめて出す数。 |
| shape | 0 | `0=Point`、`1=Sphere`、`2=Cone`、`3=Box`。 |
| simulationSpace | 0 | `0=World`、`1=Local`。 |
| playOnAwake | true | Play開始時の自動再生。 |
| looping | true | Duration終了後の再発生。 |
| collision | false | Ground簡易衝突。一般Mesh衝突ではない。 |
| prewarm | false | Loop Effectを進行済み分布で開始する。 |
| startColor / endColor | 白 / 橙 | 寿命に沿って補間する線形RGB。 |
| direction | `(0,1,0)` | 初速度の基準方向。 |
| boxSize | `(1,1,1)` | Box Shapeの全幅。 |

| C++ API | 引数 | 戻り値・動作 |
| --- | --- | --- |
| `PlayEffect` | GameObject ID | Component設定のEffectを再生し、成功時true。 |
| `PlayEffectAt` | ID、`.effect` Path、Local Offset Pointer | 指定AssetをそのGameObject基準位置で再生し、成功時true。 |
| `StopEffect` | GameObject ID | Emitterの継続発生を停止する。戻り値なし。 |
| `IsEffectPlaying` | GameObject ID | Emitter再生中または所有Particleが残っていればtrue。 |
| `GetAliveParticleCount` | GameObject ID | 現在生存するParticle数。Managerなしまたは対象なしは0。 |

```cpp
if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Fire")) {
    const EditorScriptVector3 muzzleOffset{0.0f, 0.5f, 1.0f};
    runtimeApi->PlayEffectAt(
        gameObjectId,
        "Assets/Effects/DefaultBurst.effect",
        &muzzleOffset);
}
```

### 65.12 GameObject検索と有効状態のC++ API

| API | 引数 | 戻り値・注意 |
| --- | --- | --- |
| `FindGameObjectByName` | UTF-8の完全一致名 | 最初に一致したGameObject ID。未発見またはNullは`-1`。同名を一意に識別しない。 |
| `SetGameObjectActive` | ID、bool | `isActive`を変更し、成功時true。Scene同期の見え方は呼出Frameの更新順に依存する。 |
| `IsGameObjectActive` | ID | 存在し、有効ならtrue。未発見もfalseなので区別にはFindを使う。 |

```cpp
const int32_t doorId = runtimeApi->FindGameObjectByName("ExitDoor");
if (doorId >= 0) {
    runtimeApi->SetGameObjectActive(doorId, true);
}
```

### 65.13 現在のAnimation制限事項

- Animation WindowはTransform、Light、Materialの単一float Property Trackを編集できる。
- Position、Rotation、ScaleはX/Y/Zを別Trackとして編集する。
- ColorはR/G/Bを別Trackとして編集する。Color Picker形式の複合Curve Editorは未実装。
- Timeline、Dopesheet形式のKey配置、Key移動、Step/Linear/Cubic Hermite、自動Track記録、Preview、Loop、Event、JSON保存は実働する。
- Graph Curveを線として編集する専用Curve Editor、接線HandleのMouse操作は未実装。接線は数値入力する。
- 複数GameObjectの同時記録、子階層へのProperty Path、任意C++公開変数のTrack化は未実装。
- 現行Model ImporterはFBX Clusterから最大4本のBone Index / Weightを頂点へ保持し、Runtimeは現在・前FrameのBone Matrix BufferをSkinned描画、GBuffer、Shadow、Motion Vectorへ渡す。使用可能判定には実FBXで変形、影、Temporal残像を確認する。
- Animation WindowでBone単位のPoseを直接選択・記録する視覚編集、Humanoid Retarget、IK、Avatar編集は未実装である。GPU Skinningの実行とBone Pose編集UIを混同しない。
- FBX Clipは現行Importerが取得できるAnimation ClipとNode Transform範囲で再生する。AvatarMaskは1行1Bone名のMaskアセットを読み、列挙BoneだけへAnimation姿勢を適用する。Unity相当のAvatar編集、Humanoid Retarget、IKは未実装。
- `.animgraph`はJSON編集が中心で、Node Graphの視覚編集Windowは未実装。
- `.animclip` Saveは現在開いているAssetへ上書きする。別名保存はProjectで新しいClipを作成して編集する。

### 65.14 Animationのトラブルシューティング

| 現象 | 確認順 |
| --- | --- |
| Animation WindowにClipが出ない | Projectで`.animclip`を選択、Windowが開いているか、Consoleの読込失敗、JSON構文、Pathを確認する。 |
| 保存できない | Window上部のClipが`未選択`でないか確認する。ProjectからClipを選び直す。 |
| Previewしても変わらない | GameObject選択、TrackにKeyがあるか、Light/Rendererなど必要Componentがあるか確認する。 |
| 記録してもKeyが増えない | Clipが未選択でないか、`● 記録中`表示か、GameObject選択、対象Component、GizmoまたはInspectorで実際に値が変わったかを順に確認する。 |
| Playで動かない | Animation Component有効、`.animclip` Path、種類がFBX/Property Clip、自動再生、速度が0でないか確認する。 |
| Animator Parameterがfalseを返す | Animator起動済みか、Graph読込成功、Parameter名の大文字小文字、型一致を確認する。 |
| Clip番号が違う | FBX内Clip一覧の順番と`.animgraph`の`clip`番号をInspectorで照合する。 |
| Eventが来ない | Event時刻がClip長内か、Script DLLが同じGameObjectにあるか、Export関数名が完全一致か確認する。 |
| Effectだけ出ない | `.effect` Path、Effect Asset JSON、Particle/VisualEffect Component、Consoleを確認する。 |
| Stop後に値が残る | Preview停止またはPlay停止が呼ばれたか、別Scriptが同じPropertyを書いていないか確認する。 |

## 66. 2026-08-01 時点で追加調査が必要な全差分

この章は、65章までに具体的な使用手順が載っていなかった現在の実装を、調査対象から漏らさないための差分台帳である。
「Shaderファイルが存在する」「Component enumが存在する」だけで完成扱いにせず、UI、保存、実行、組み合わせ、制限を確認する。

| 分類 | 追加対象 | 主な根拠 |
| --- | --- | --- |
| Editor Window | Spline Editor、Event Timeline、State Graph | `EditorGameplayToolsWindowManager.cpp` |
| Build | Game Build Settings、Standalone Player、`game.build` | `EditorGameBuildManager.cpp`、`EditorMainMenuBar.cpp` |
| Scene | Path遷移、Build Index遷移、SceneButton | `EditorScriptManager.cpp`、`EditorGameViewManager.cpp` |
| Component | FreeTransform、AutoConvexCollision、Ocean、Buoyancy、RailMovement、Health | `EditorScene.h`、`EditorInspectorPanel.cpp` |
| 汎用進行 | WaveSpawner、TimelineEvent、ThresholdState、UIValueBinding | 各Runtime Manager |
| Terrain | Height Map、3段階LOD、Shadow LOD | `EditorSceneSynchronizer.cpp`、`EditorRenderManager.cpp` |
| Foliage | Density Map、GPU Instancing、距離密度、風変形 | 同上、`SurfaceDeformation.hlsli` |
| Particle | 8運動方式、Depth / Physics SDF Collision、Mesh Particle | `EditorGpuParticleManager.cpp`、Particle Shader |
| Audio | Bus、Voice制限、再発音間隔、Doppler、Spread、Cone、Occlusion、Reverb、初期反射 | `EditorAudioManager.cpp` |
| Material | Clear Coat、Transmission、Subsurface、Anisotropy、Specular Tint、Sheen | `EditorScene.h`、`Object3d.PS.hlsl` |
| Reflection | SSR、Cubemap、Planar、Object Mask、材質反射強度 | Reflection Manager / Shader |
| Transparency | Weighted OIT、Refractive Surface、水面専用Pass | Transparency Shader、`EditorRenderManager.cpp` |
| Temporal | Scene / Game履歴分離、Skinned Motion Vector、Reactive / Disocclusion | Temporal Manager / Shader |
| PostProcess | Auto Exposure、色温度、Tint、Lift / Gamma / Gain | PostProcess Manager / Shader |
| Water | GPU FFT、泡、吸収、屈折、浅瀬、Underwater / Caustics、共通浮力Sample | Ocean Manager / Water Shader |
| C++ Script | 高水準`Input`、`GameObject`、`Rigidbody`、`SceneManager`、`RailFollower` | `EditorNativeScript.h` |
| Script拡張 | `ExposeGameObject`、`ExposeScene`、`BindAction`、Action候補Export | Native Script / Script Manager |

旧`LegacyRailShooterEnemy`、`LegacyRailShooterShip`、`LegacyRailShooterEnemyMotion`、`LegacyRailShooterStage`は新機能として説明しない。
これらは旧Scene列を読み捨てず保持するための互換型で、現在のEngine Runtimeは専用ゲームルールを実行しない。

## 67. 初期SceneとResource構成の調査

### 67.1 初期Scene

新規Sceneで自動配置されるGameObjectを実機で確認し、名前、Component、初期Transform、削除可能かを記録する。

最低限、次を確認する。

- Environment Light。
- Main Camera。
- Point Light。
- 新規Scene保存前後でIDと参照が維持されるか。
- `editorScene.scene`が起動に必要な場合、その探索Path、欠損時のError、Fallback動作。

### 67.2 実行に必要なResource

`resources`直下の全ファイルを「消してよい」「Editor既定」「評価課題」「使用者Asset」に分類する。

| 分類 | 調査内容 |
| --- | --- |
| Editor既定 | `resources/editorDefault`のPrimitive Mesh、Fallback Texture、Icon。 |
| 起動Scene | `editorScene.scene`または現在のDefault Scene Path。 |
| 評価課題 | `resources/evaluationTaskResources`。Engine起動要件と混同しない。 |
| Legacy Model | `resources/model/legacy`。参照が残るSceneだけで必要か確認する。 |
| 使用者Asset | `Assets`または使用者が作ったResource Folder。 |

削除可否はファイル名で判断せず、起動時の直接参照、Default Scene参照、Fallback参照、Build Copy対象を検索して決める。

## 68. Game Build SettingsとStandalone実行

### 68.1 EditorをBuildする手順

1. `CG2.sln`をVisual Studioで開く。
2. Platformを`x64`にする。
3. Editor確認はDebugまたはReleaseを選び、`CG2`をStartup Projectにする。
4. Solution Build後、`x64/Debug/CG2.exe`または`x64/Release/CG2.exe`が作られたことを確認する。
5. 大量の`.obj`はCompilerの中間Objectであり、実行ファイルの代わりではない。Link成功と`.exe`の存在を別に確認する。

### 68.2 ゲームを書き出す手順

1. ゲームで使う各Sceneを`Assets`または`resources`へ`.scene`として保存する。
2. 先にSolutionの`x64 / Release`をBuildし、`x64/Release/CG2.exe`を作る。
3. Editorで`ファイル > ゲームをビルド...`を開く。
4. `ゲーム名`へ出力EXE名を入力する。`.exe`がなければ自動で付くかを確認する。
5. `出力先`を設定する。既定は`Builds/CG2Game`。
6. Scene一覧のCheckboxで使用Sceneをすべて有効にする。
7. 1つのSceneを`起動`Radio Buttonで選ぶ。
8. `ゲームを書き出す`を押す。
9. Consoleの`Build: ゲームを書き出しました`と出力Pathを確認する。
10. 出力先のゲームEXEをEditor外から起動し、起動Scene、Scene遷移、Asset、Audio、Script DLLを確認する。

### 68.3 書き出されるもの

現行実装は次を出力先へCopyする。

- `x64/Release/CG2.exe`をゲーム名へRenameしたEXE。
- `x64/Release`直下の実行用DLL。
- Projectの`Assets`全体。
- Projectの`resources`全体。
- `x64/Release/ThirdParty`。
- Product名、出力先、起動Scene、Scene一覧を持つ`game.build`。

`x64/Release`自体を出力先に指定してはいけない。起動SceneがScene一覧にない、Sceneファイルが存在しない、Release EXEがない場合は書き出しを失敗させる。

### 68.4 StandaloneのScene遷移

Scene遷移は次の2方式を別ページで説明する。

- SceneButton: ScriptなしでGame View ButtonからPath遷移する。
- C++ Script: `SceneManager::LoadScene(path)`または`SceneManager::LoadScene(buildIndex)`を使う。

Build IndexはBuild SettingsのScene順と照合する。Path遷移でもStandaloneにCopyされていないSceneへは遷移できない。

## 69. Spline EditorとRailMovement

### 69.1 最小作成手順

1. 移動対象GameObjectへ`レール移動`を追加する。
2. `ウィンドウ > Spline Editor`を開く。
3. 移動対象をHierarchyで選び、`新規Spline`を押す。
4. `Spline Path`と4つの子制御点が作成され、Rail Path参照へ自動設定されることを確認する。
5. PointをHierarchyまたはSpline Editorの一覧から選ぶ。
6. Scene Gizmo、Inspector位置、Spline Editor CanvasのいずれかでPointを移動する。
7. 上面編集は`上面 XZ`、高さ編集は`側面 ZY`を使う。
8. 追加は`制御点を追加`、削除はPoint選択後`選択点を削除`を使う。2点以下にはしない。
9. RailMovement Inspectorで速度、加減速、開始位置、向き、Loop、曲線方式を設定する。
10. Rigidbodyで動かす場合は`移動方式`を`Dynamic Rigidbody 物理追従`へ変更し、位置/回転の追従軸、ばね、減衰、最大加速度を設定する。
11. OceanのBuoyancyと併用する場合は`浮力併用プリセット`を押し、Y位置とX/Z回転を浮力側へ任せる。
12. Play中は進行率Slider、停止 / 再開、順方向 / 逆方向で確認する。

### 69.2 動作仕様

- 制御点はRail Path直下の子をHierarchy順に使う。
- 滑らかな曲線ONではCatmull-Rom Sampleを使う。
- 移動は制御点番号ではなくPath全長に対する距離で進む。
- PathまたはPointが編集された場合はSampleを再構築する。
- `進行方向へ回転`は先読み位置からPitch / Yawを計算する。
- Loop OFFかつ終端停止ONでは終端到達後に停止する。
- 終端到達はC++の`ConsumeEndReached`で1回ずつ受け取る。
- `Transform 追従`は従来通り経路位置を直接設定する。`Dynamic Rigidbody 物理追従`はJolt固定更新ごとにPD制御の力とトルクを加え、衝突、浮力、慣性を残す。
- 物理追従には有効なDynamic Rigidbodyと3D Colliderが必要である。位置追従軸を0にした方向は外力だけで動き、回転追従軸を0にした軸は浮力や接触トルクを上書きしない。

### 69.3 Scene Viewでの確認項目

Scene Viewでは、RailMovementが参照しているRail Pathを線で表示する。
使用者向けドキュメントでは、次の見え方を必ず説明する。

- 選択中のRail Pathは橙色で太く表示される。
- 選択中ではないRail Pathは水色で細く表示される。
- Catmull-Romが有効な場合は、実際の移動と同じ曲線Samplingで描画する。
- 制御点は小さいMarkerと番号Labelで表示する。
- 進行方向はPath上の矢印で表示する。
- RailMovementの左右移動可能範囲は水色の横線、上下移動可能範囲は緑の縦線で表示する。
- Game Viewでは編集用のRail線を表示しない。Scene View上で経路設計を確認する。

ドキュメントには「線が見えない時」の確認順も入れる。

1. Scene Viewタブを開いているか。
2. 移動対象GameObjectまたはRail Pathを選択しているか。
3. RailMovementのRail Path参照が空ではないか。
4. Rail Path直下に2点以上の子制御点があるか。
5. 制御点のScaleが0、または全点が同じ位置になっていないか。

### 69.4 Spline Editorの編集仕様

Spline Editorの2D Canvasは、直線の点つなぎではなくRuntimeと同じ曲線Previewを表示する。
上面XZは横X・縦Z、側面ZYは横Z・縦Yとして編集する。

制御点の追加は`選択点の次へ追加`として説明する。

- 制御点が選択されている場合、選択点の次に新しいPointを挿入する。
- 選択点の次にPointがある場合は、選択点と次Pointの中間位置へ挿入する。
- 選択点が最後の場合は、最後の進行方向へ外挿した位置へ挿入する。
- 制御点が未選択の場合は末尾へ追加する。
- 追加後はHierarchy順が移動順になるように子順序を更新する。
- Canvas上でPointをDragした場合、Drag終了時にScene同期を行い、Inspector、Scene View、Runtime Sampleを更新する。

削除は2点未満にしない。
2点未満になる操作は警告し、RailMovementの動作対象から外す。

### 69.5 RailMovement Inspectorのプリセット

RailMovement Inspectorには、用途別に設定をまとめて適用するプリセットを説明する。

| プリセット | 用途 | 変更される代表設定 |
| --- | --- | --- |
| 標準移動 | Transformで単純にPathへ沿わせる。移動床、カメラの下書き、敵の単純移動。 | Transform追従、進行方向へ回転ON、滑らかな曲線ON、終端停止ON。 |
| カメラ経路 | CameraをSplineに沿わせ、見た目の確認をしやすくする。 | 速度を低め、先読みを長め、左右上下Offsetを小さめ、LoopはOFF。 |
| 物理乗物 | Rigidbodyの慣性と衝突を残しながらPathへ追従する。 | Dynamic Rigidbody物理追従、位置/回転PD、最大加速度、Collider警告。 |
| 浮力併用 | Ocean上の船をRailに沿わせるが、上下動と傾きはBuoyancyへ任せる。 | X/Z位置とYawをRail、Y位置とPitch/Rollを浮力側へ残す。 |

プリセットは完成したゲームルールではない。
攻撃、敵全滅待ち、スコア、ステージクリア条件はC++ Script、TimelineEvent、ThresholdState、WaveSpawner、ActionSequenceで組み合わせる。

### 69.6 責務の境界

RailMovementは経路移動だけを行う。敵、攻撃、船、Camera、Wave、Boss、Goalのルールを持たせない。
ゲーム側はC++ Script、WaveSpawner、TimelineEvent、ThresholdStateなどを必要な分だけ組み合わせる。

## 70. Event Timeline、WaveSpawner、State Graph

### 70.1 Event Timeline

1. `ウィンドウ > Event Timeline`を開く。
2. 表示軸を`経過秒`または`Rail進行率`から選ぶ。
3. 経過秒では表示時間を設定する。
4. `Eventを追加`でTimelineEventを持つGameObjectを作る。
5. Markerを横へDragして発火秒または進行率を変更する。
6. InspectorでSource、Action対象、Action名、一度だけを設定する。
7. 水色MarkerがTimeline Event、橙色MarkerがRail条件のWave開始であることを説明する。

TimelineはBGM、攻撃、Bossなどを直接実行しない。名前付きActionを通知し、実際の処理は受信側Script / Componentが決める。

### 70.2 Waveを視覚的に作る

1. 雛形にするGameObjectまたはPrefab Instanceを選ぶ。
2. Event Timelineで`選択ObjectをWave雛形にする`を押す。
3. 個数1～64、横列 / V字 / 円 / Grid、配置間隔を設定する。
4. `Waveを作成`を押す。
5. 作成されたWave親と複製された子をHierarchyで確認する。
6. 子のComponent値やPrefabを必要に応じて個別編集する。
7. WaveSpawnerの開始条件、間隔、Actionを設定する。

この補助機能は初期配置を作るだけで、Formation移動や敵AIをEngineへ固定しない。

### 70.3 State Graph

1. `ウィンドウ > State Graph`を開く。
2. ThresholdStateを持つObjectを選ぶ。未追加なら`選択ObjectへThreshold Stateを追加`を押す。
3. SourceをHealth比率またはRail進行率から選ぶ。
4. State 2 / 3境界を0～1で設定する。
5. State 1 / 2 / 3 Action名を設定する。
6. InspectorでSource ObjectとAction対象を設定する。
7. Graph上の3 NodeとAction名を確認する。
8. Playし、値が境界を越えた時だけActionが来ることをConsoleまたは受信Scriptで確認する。

## 71. Script Actionの選択的拡張

### 71.1 対象にするComponent

Script Hookは全Componentへ機械的に追加しない。現在の自動候補UI対象は次である。

| Component | Hookを置く理由 |
| --- | --- |
| WaveSpawner | 開始、各生成、完了という明確なライフサイクルがある。 |
| TimelineEvent | 条件成立時に任意処理を呼ぶこと自体が責務である。 |
| ThresholdState | 状態変更時の処理をゲーム側へ委譲する必要がある。 |

Transform、Renderer、Collider、Healthなどへ、用途不明の開始 / 終了Hookを一律追加しない。

### 71.2 登録から選択まで

1. 新規C++ Scriptを作る。
2. `Script`継承ClassのConstructorで`BindAction`する。
3. DLLをBuildする。
4. 受信GameObjectへScriptまたはMonoBehaviourを追加する。
5. 通知Componentの`Action 対象`へ受信GameObjectを指定する。
6. Action名を直接入力するか、`... 候補`Comboから選ぶ。
7. Sceneを保存してPlayする。

候補はGenerated側が対象GameObjectの登録済みActionから公開する。使用者はExport関数を記述しない。

### 71.3 受信値と失敗診断

| Event | `buttonValue` |
| --- | --- |
| Wave開始 | 1.0。 |
| Wave各生成 | 生成GameObject ID。 |
| Wave完了 | 子数。 |
| Timeline | 発火設定値。 |
| Threshold | State番号1～3。 |

候補が出ない場合はAction対象、Script Component、DLL Path、DLL Build、Action Export、Action名の順で確認する。
通知されない場合はGameObject / Component有効状態、Source参照、条件値、Play状態、Consoleの未登録Action Warningを追加で確認する。

## 72. Ocean、Buoyancy、Underwater

### 72.1 Oceanの作成

1. `ゲームオブジェクト > 3D Object > Ocean`を選ぶ。
2. Transform Yを基準水位にする。
3. 最初はGrid 256または512、海面サイズ240程度で調整する。
4. 主波の高さ、最大波高、波長、速度、Choppiness、方向を設定する。
5. 副波方向を主波とずらし、副波と細波の強さを上げる。
6. 風速、水深、方向分散、うねり、Seed、波頭の尖りを設定する。
7. 泡、粗さ、反射、屈折、微細法線、吸収距離、屈折歪みを設定する。
8. 浅瀬色と深海色を設定する。
9. 最終品質で1024または2048を試し、FPSとGPU時間を記録する。

Sceneに明示的なPlanar Reflection Probeがない場合、有効なOceanを候補にし、Scene View / Game ViewそれぞれのCameraへ最も近い水域のTransform Y面を基準に反射Captureを自動生成する。水面Shaderは環境反射を基礎に、自動Planar Capture、画面内SSRの順で信頼度合成する。Ocean自身はCaptureから除外して再帰参照を防ぎ、自動Captureを鏡用の全画面Planar合成へ流用しない。反射強度0のOceanは暗黙Capture候補から除外する。明示Planar ProbeがあるSceneでは既存の鏡面合成を優先し、OceanはEnvironment / SSRへFallbackする。自動CaptureはSceneを追加描画するため、最終品質確認では反射あり/なしのGPU時間も比較する。

### 72.2 Buoyancy

1. 浮かせるObjectへRigidbodyを追加する。
2. BoxCollider、AutoConvexCollision、MeshColliderのいずれかを追加する。MeshColliderを削除してAuto Convexへ置換する必要はない。
3. Buoyancyを追加する。
4. Ocean参照を設定するか自動検出を使う。
5. 船体SizeとCenterを見た目へ合わせる。
6. Playし、浮力、上下減衰、前後/横/上下の水抵抗、着水衝撃、波の横押し、回転抵抗を順に調整する。
7. RailMovementを使う船は`Dynamic Rigidbody 物理追従`へ変更し、`浮力併用プリセット`を適用する。

描画波面と浮力は同じOcean Sampleを使うことを確認する。通常の3D Shape経路は船体範囲を覆う固定5x5の25点へ最小二乗Planeを当て、Joltの実Shapeを切った排水体積と浮心を求める。同じ25点を各水力面へ双線形補間し、実Collider表面へ局所水位・法線・表面速度に基づく静水圧、抗力、揚力、Slammingを分布させる。Joltが体積または表面を返せない特殊Shapeだけ、船体Sizeに応じた8～512セルの旧分布へフォールバックする。Readback前のCPU有限水深SpectrumはOcean設定変更時だけ16波を再生成し、同じOceanへの25点SampleやCastでキャッシュを共有する。GPU Managerの主Oceanと設定が異なるOceanは主FFTを流用せず、そのOcean自身のCPU Spectrumで物理・Gameplay Queryを継続する。

### 72.3 Underwater / Caustics

次を別々に確認する。

- Cameraが水面より上では全画面水中処理が掛からない。
- Cameraが波面を横切る時、境界が固定平面ではなく変位波面へ追従する。
- 水深と視線の水中通過距離でRGB Absorption / In-scatteringが連続して変わる。
- CausticsがFFT波面のSlopeとCurvatureへ追従して物体表面へ投影される。
- Oceanが複数ある場合、各ViewportのCamera位置に最も適した同一FFT設定の水域が選ばれ、色・吸収・屈折・Transformが別Oceanと混ざらない。
- Scene ViewとGame ViewでCamera位置を混同しない。
- 屈折SampleがScene View / Game ViewのViewport境界を越えて隣のViewを読まない。

### 72.3.1 Ocean Gameplay Sampleと航跡

- `WaterSurfaceState` Inspectorの`砕波・泡率`が波頭で0～1へ変化する。
- `OceanProbeSet`の各Entryが相対高さ、法線、速度に加えて泡率を表示する。
- C++ Scriptは`Ocean::SampleDetailed`、`WaterSurfaceState::GetFoam`、`OceanProbeSet::GetFoam`で泡率を取得できる。
- `SurfaceWakeEmitter`は船の水平速度からOcean Surface Velocityを引いた相対速度を使う。浮力による上下動だけで航跡が増えず、波頭では泡率に応じて発生量がわずかに増えることを確認する。

### 72.4 Aerodynamics / WindZone

1. Dynamic RigidbodyとColliderを持つGameObjectへAerodynamicsを追加する。
2. 空気密度、抗力係数、代表面積を設定し、`1/2*rho*Cd*A*v^2`で速度の二乗に比例して減速することを確認する。
3. 翼を使う場合はLocal `+Z`を前、`+Y`を上へ向け、基礎揚力係数、揚力傾斜、翼面積、ゼロ揚力迎角、失速迎角を設定する。
4. 横滑りを止める場合は横力係数と側面積、回転球の軌道を曲げる場合はMagnus係数を設定する。
5. 圧力中心を重心からずらし、合力がPitch / Yaw / Roll Torqueへ変換されることを確認する。
6. 共通風を使う場合は別GameObjectへWindZoneを追加し、方向風または放射風、速度、半径、乱流を設定する。

描画FrameではなくPhysics FixedUpdateごとに外力を加える。Aerodynamicsの基礎風速と、範囲内にある全WindZoneの風速は加算する。WindZoneを持つだけでは物体は動かず、力を受ける側にAerodynamicsが必要である。

### 72.5 GravityField

1. 重力中心にするGameObjectへGravityFieldを追加する。
2. 物理式を使う場合は`Newton 逆二乗`を選び、G、引力源質量、最小計算距離を設定する。
3. Game向けの均一な点重力なら`定加速度`を選び、加速度を設定する。
4. 大量のRigidbodyがあるSceneでは影響半径を設定する。
5. 中心付近で暴走する場合は最小計算距離と加速度上限を上げる。
6. 点重力だけを使うRigidbodyは通常の`重力を使用`を無効にする。

逆二乗モードは`a=G*M/r^2`、Rigidbodyへ加える力は`F=m*a`とする。GravityField自身への自己力は加えず、複数GravityFieldの力は加算する。

### 72.6 RotatingFrame

1. 回転中心GameObjectへRotatingFrameを追加する。
2. World角速度、角加速度、中心の線速度、影響半径を設定する。
3. 範囲内のDynamic Rigidbodyに対し、遠心加速度`-omega x (omega x r)`を確認する。
4. Rigidbodyへ回転座標系に対する相対速度を与え、Coriolis加速度`-2*omega x v`を確認する。
5. 角加速度を設定し、Euler加速度`-alpha x r`を確認する。
6. 中心から遠い物体で加速度が過大になる場合は影響半径と加速度上限を設定する。

RotatingFrameは見た目のTransform回転や床Colliderの接触を代行しない。回転する見た目、接触面、疑似力は別々の責務として組み合わせる。

### 72.7 FluidVolume

1. 空のGameObjectへFluidVolumeを追加し、位置・回転・Scaleと`サイズ`で有限の箱領域を作る。
2. 対象へDynamic Rigidbodyと3D Colliderを追加する。
3. 密度、粘性、二次抗力係数、流速、角粘性、Force上限を設定する。
4. Colliderが境界へ半分入った状態で、全浸水時の約半分から浮力と抵抗が増えることを確認する。
5. 物体を傾けて片側だけ浸水させ、重なり体積中心への位置付きForceから復元Torqueが発生することを確認する。
6. 流速を設定し、物体速度との差へStokes抵抗と二次抗力が働くことを確認する。

浮力は`density*displacedVolume*|gravity|`、低速粘性抵抗は等価球による`-6*pi*mu*r*vRelative`、高速抵抗は`-1/2*rho*Cd*A*|vRelative|*vRelative`を使う。Ocean波面を使うBuoyancyとは別機能であり、同じ物体へ両方の領域を重ねた場合は外力が加算される。

### 72.8 SpringForce

1. 所有者へDynamic Rigidbody、Collider、SpringForceを追加する。
2. World固定点を使うか、接続先GameObjectを指定する。
3. 所有者Anchor、接続先Anchor、自然長、ばね定数、減衰、Force上限を設定する。
4. 自然長より伸ばした時に接続先方向、縮めた時に反対方向へ力が働くことを確認する。
5. Anchorを重心からずらし、位置付きForceでTorqueが発生することを確認する。
6. 接続先をDynamic Rigidbodyにして反作用を切り替え、運動量の受け渡しを確認する。

力は`F=k*(length-restLength)-c*vRelativeAlongSpring`とする。取付点速度は線速度だけでなく`angularVelocity x anchorOffset`を含む。SpringJointのような拘束は作らないため、Force上限を越える外力があれば自然長から離れられる。

### 72.9 ElectromagneticBody / ElectromagneticField

1. 場を定義するGameObjectへElectromagneticFieldを追加する。
2. 一様場では電場E、磁束密度B、影響半径を設定する。
3. 点電荷では源電荷、Coulomb定数、最小計算距離、影響半径を設定する。
4. 対象へDynamic Rigidbody、Collider、ElectromagneticBodyを追加し、正または負の電荷を設定する。
5. 静止物体で`F=qE`、初速を持つ物体で`F=q(v x B)`を個別確認する。
6. Local磁気Momentを設定し、`Torque=moment x B`で磁場へ姿勢がそろうことを確認する。
7. 複数Fieldを置き、電場・磁場が加算されることと、Body側のForce/Torque上限を確認する。

点電荷は自己力を除外し、中心特異点を最小距離で制限する。これは準静的な剛体向けモデルであり、誘導電流、電磁波、磁場勾配による並進力は対象外として明記する。

### 72.10 追加外力の共通確認

- 全てPhysics FixedUpdateで評価され、描画FPSを変えても単位時間あたりの力積が変化しない。
- 無効GameObject、無効Component、Kinematic Rigidbodyへ力を加えない。
- 影響源は固定更新ごとに1回収集し、対象ごとのScene全探索を避ける。
- 複数の外力Componentは加算されるため、Ocean BuoyancyとFluidVolumeなどの重複配置を確認する。
- Force/Torque上限は異常値対策であり、Mass、係数、Scene単位の調整を代替しない。
- 2D物理Componentへは接続しない。

## 73. Terrain、Foliage、Particle

### 73.1 Terrain

1. Terrainを追加する。
2. Height Mapを設定する。
3. Size X / Height / Zを設定する。
4. 最高LOD解像度16～256を設定する。
5. Cameraを近・中・遠へ動かし、3段階LODと境界の継ぎ目を確認する。
6. Shadowが一段低いLODでも形状破綻しないか確認する。
7. TerrainColliderが必要なら別途追加し、見た目と判定高さを比較する。

### 73.2 Foliage

1. 草木Meshを持つObjectへFoliageを追加する。
2. Density Map、配置範囲、密度、最大Instance、LOD距離を設定する。
3. 風向き、揺れ幅、風速、空間周波数、時間倍率を設定する。
4. Cameraを移動し、Density LOD、Frustum / Hi-Z Culling、Shadow LODを確認する。
5. Alpha Cutout、両面、透過光、Motion Vectorを確認する。

### 73.3 Particle

Particleの説明はMain、Emission、Shape、Motion、Lifetime Appearance、Render Modelへ分ける。

| 項目 | 現在の選択肢 |
| --- | --- |
| Motion | Linear、Orbit、Vortex、Wave、Attractor、Cloud、Explosion / Splash、Projectile Trail。 |
| Collision | Depth、Physics SDF。 |
| Render | Camera Facing、Y軸固定、Velocity Facing、World XY固定のBillboard、指定FBX / OBJ Mesh。 |
| Asset | `.effect`、`.efk`、`.efkefc`。 |

Depth Collisionは画面内の見えているDepthへ使い、画面外や裏面まで必要な物理ObjectにはPhysics SDFを使う。Collision OFF、Depth、SDFを同じSceneで比較し、反発、摩擦、薄いCollider、画面外挙動を記録する。

Render Assetが空のParticleは、World XYへ固定した板ではなく、描画中のView Cameraから得たRight / Up基底でQuadを組み立てる。Scene ViewとGame ViewはそれぞれのCamera基底を渡すため、片方のCameraへだけ正対してはいけない。FBX / OBJを指定したParticleは通常の3D Model描画を使い、Billboard ModeとStretchを適用しない。

## 74. Audioの現在機能と使用手順

### 74.1 AudioSource

1. AudioListenerをMain Cameraへ追加する。
2. 音源GameObjectへAudioSourceを追加する。
3. WAV Asset Pathを設定する。
4. Volume、Pitch、Loop、Play On Awakeを設定する。
5. BusをSFX、BGM、Ambience、UIから選ぶ。
6. 同時発音数1～32と再発音間隔を設定する。
7. 2D音はSpatial Blend 0、3D音は1へ近づける。
8. Min / Max Distanceを設定する。
9. Doppler、Spread、Cone Inner / Outer、Outer Volumeを設定する。
10. Occlusion、Reverb Send、Early Reflectionを設定する。
11. Play中にInspectorのPlay / Stopで確認する。

### 74.2 実処理として確認する項目

- Voice上限を超えた時に最も古い同一AudioSource Voiceを停止する。
- 再発音間隔内の連打を抑止する。
- Listenerとの距離で減衰する。
- Listener右方向からPanを計算する。
- Source前方向とListener方向からCone減衰する。
- 相対速度からDoppler Pitchを計算する。
- Collider RaycastでOcclusionを計算し、VolumeとLow Passへ反映する。
- AudioLowPassFilter、AudioHighPassFilter、AudioReverbFilterの実接続範囲を確認する。
- Reverb Zone内では最も強いZone量を使う。
- Dry音をMasterへ残し、Reverb / Early ReflectionをSubmixへ並列送信する。

Echo、Distortion、Chorusなど、Inspector表示があっても実際のEffect Chainへ未接続なら「設定のみ」と明記する。

## 75. Material、Reflection、Transparency

### 75.1 Advanced Material

既存のBase Color、Normal、Metallic、Roughness、AO、Emission、Height、Opacityに加え、次を個別に調査する。

- Clear Coat / Clear Coat Roughness。
- Transmission。
- Subsurface。
- Anisotropy / Anisotropy Rotation。
- Specular Tint。
- Sheen / Sheen Tint。
- Alpha Mode: Opaque、Mask、Transparent。
- Double Sided。

各値はInspector変更、Scene保存、Shader Constant、最終見た目を照合する。値が存在してもShaderで未使用なら設定のみとする。

### 75.2 反射の確認順

1. MaterialのMetallic、Roughness、IOR、Reflection Strengthを設定する。
2. EnvironmentのReflection Contributionと環境画像を設定する。
3. ReflectionProbeを追加し、Screen Space / Cubemap / Planarを1方式ずつ確認する。
4. SSRは画面内だけ、Cubemapは環境、Planarは平面Scene Captureであることを分ける。
5. Planarは反射面のWorld Plane、Mesh Center / Bounds、Scene / Game Cameraを確認する。
6. 反射対象外Object Maskと反射面自身の二重描画を確認する。
7. Reflection Strength 0、0.5、1、2で差が出ることを確認する。

### 75.3 TransparencyとOIT

- Alpha Mode Transparentの通常半透明はWeighted Blended OIT対象。
- 水面と屈折Glassは専用Pathとして通常OITと分離する。
- OITは通常半透明の描画順依存を減らすが、厳密な前後順や多層屈折を保証しない。
- Mask材質はAlpha Cutout Shadowを確認する。
- OIT合成前後の水面、Effect、SSR、PostProcess順序を記録する。

## 76. Temporal、Skinned Motion Vector、PostProcess

### 76.1 Temporal

次をScene ViewとGame Viewで別々に確認する。

- Previous View Projection。
- Camera Velocity。
- Object Motion Vector。
- Skinned Meshの現在 / 前Frame Bone Matrix。
- Reactive Mask。
- Disocclusion Mask。
- Velocity Dilate。
- History ClampとTemporal Resolve。
- Resize、Scene切替、Camera切替時のHistory Clear。

Skinned Motion VectorはShaderがあるだけでは完成ではない。FBX Bone Weight / Index、Current / Previous Bone Palette、GBuffer出力まで接続されているかを確認する。

### 76.2 PostProcess

Auto Exposureは固定Exposureと同時に作用する順序を調査する。Minimum / Maximum、Adaptation Speed、Target Luminanceを暗所から明所、明所から暗所で測る。

Color GradingはTemperature、Tint、Lift、Gamma、Gainを中立値へ戻せること、Scene保存で値が維持されること、Final Compositeへ渡ることを確認する。

## 77. 性能と実機確認

### 77.1 描画負荷テストScene

`ウィンドウ > 描画負荷テスト Scene を作成`は、Ocean、Terrain、Foliage、OIT、Refraction、Skinned Mesh、Particle Collisionを同時配置する検証用Sceneを作る。

1. 未保存変更を退避する。
2. Menuから負荷Sceneを作成する。
3. `Assets/Scenes/RenderStress.scene`が作成されたことを確認する。
4. DebugとReleaseで同じCamera位置を使う。
5. FPSだけでなくCPU Frame、GPU Frame、VRAM、Draw Call、Particle数を記録する。
6. Ocean解像度、Foliage Instance数、Particle数、AA、SSRを1項目ずつ変える。

### 77.2 最低限の組み合わせ試験

| 試験 | 構成 |
| --- | --- |
| Ocean物理 | Ocean + 船体 + Rigidbody + Collider + Buoyancy。 |
| 空気力学 | Rigidbody + Collider + Aerodynamics + WindZone。抗力、揚力、失速、横滑り、Magnus、圧力中心Torqueを個別確認する。 |
| 点重力 | Rigidbody + Collider + GravityField。逆二乗、定加速度、半径、中心特異点制限を個別確認する。 |
| 回転座標系 | Rigidbody + Collider + RotatingFrame。遠心、Coriolis、Eulerの各項を速度条件別に確認する。 |
| 有限流体 | Rigidbody + Collider + FluidVolume。部分浸水体積、Archimedes浮力、Stokes抵抗、二次抗力、流速、圧力中心Torqueを確認する。 |
| 遠隔ばね | Rigidbody + Collider + SpringForce。World固定点、Body接続、自然長、減衰、反作用、Anchor Torqueを確認する。 |
| 電磁気 | Rigidbody + Collider + ElectromagneticBody + ElectromagneticField。Coulomb、Lorentz、磁気Torque、点電荷特異点制限を確認する。 |
| Rail進行 | RailMovement + Spline + TimelineEvent + C++ Script。 |
| Wave | WaveSpawner + 子3体 + Action受信Script。 |
| HUD | Health + Text + Slider + UIValueBinding。 |
| Scene遷移 | Title.scene + Stage.scene + SceneButton + Build Settings。 |
| Audio | Listener + 3D AudioSource + Collider遮蔽 + Reverb Zone。 |
| Transparency | Opaque + Mask + OIT Transparent + Glass + Ocean。 |
| Temporal | Camera移動 + Object移動 + Skinned Animation + Particle。 |
| 画面照準 | PlayerInput + ScreenAim + RectTransform。マウスとGamepadを個別確認する。 |
| 即時射撃 | ScreenAim + HitscanWeapon + Collider + Health + DamageReceiver。 |
| 弾射撃 | ScreenAim + ProjectileEmitter + ObjectPool + Collider + Health + DamageReceiver。高速移動時のすり抜けを確認する。 |
| Pool再利用 | ObjectPool + PrefabSpawner + Health + DamageReceiver。死亡後に同じItemを再生成してHealth復元を確認する。 |
| Camera演出 | 優先度の異なるCamera 2台 + CameraBlend + CameraShake。 |
| Rail分岐 | RailMovement + RailBranch + Rail Path 2本。自動進行率とScript手動実行を確認する。 |

### 77.3 完了判定

この追補範囲は次を満たすまで完了扱いにしない。

- 63章のComponent一覧が現在の`kComponentAddEntries`と一致する。
- 追加ComponentごとにInspector全項目、初期値、保存、Runtime、制限がある。
- Spline Editor、Event Timeline、State Graphを画面操作で再現できる。
- Game Build SettingsからStandalone EXEを出し、起動SceneとScene遷移を確認する。
- 新規C++ Script DLLでAction候補が表示され、Wave / Timeline / Thresholdから受信できる。
- Ocean描画とBuoyancyが同じ波面を参照することを確認する。
- Reflection方式、OIT、水、UnderwaterのPass順を確認する。
- Audioの3D方向、遮蔽、Doppler、Reverbを実際に聞いて確認する。
- Debug / ReleaseのBuild成功だけでなく、Editor UIとStandaloneを手動確認する。
- 実機未確認項目を「使用可能」と断定しない。

## 78. 汎用ゲームプレイ基盤の確認手順

### 78.1 オンレール射撃Sceneを汎用Componentで組む

1. Rail Path親と子制御点を作り、船またはCameraへ`レール移動`を追加する。
2. Canvas、Image、RectTransformで照準UIを作り、入力Objectへ`画面照準`を追加する。
3. PlayerInputへ照準Vector2と発射Buttonを登録する。
4. 即時武器なら`レイ射撃`、見える弾なら`オブジェクトプール`と`弾発射`を追加する。
5. 命中対象へCollider、Health、`ダメージ受信`を追加する。
6. 生成元へ`プレハブ生成`または`ウェーブ生成`を追加する。
7. 経路変更は`レール分岐`、視点演出は`カメラブレンド`と`カメラシェイク`を使う。
8. 敵行動、誘導、スコア、ステージ条件はC++ Scriptの公開変数とActionで接続する。

### 78.2 拡張性の合格条件

- RailMovementは攻撃や敵種を知らない。
- Weaponはスコア、リロード、敵AIを知らない。
- DamageReceiverは攻撃種別をEnumへ固定しない。
- Spawnerは生成後の敵行動を知らない。
- RailBranchは敵全滅条件を知らず、外部から`Trigger`できる。
- CameraBlendとCameraShakeはゲームジャンルを知らない。
- ゲーム固有条件はC++ ScriptまたはデータAsset側に置き、Engine Managerへ追加しない。

## 79. Prefab・Scene Streaming・Sequence・Save の実利用手順

### 79.1 Prefab Asset

1. Hierarchyで雛形の親GameObjectを選ぶ。子Hierarchy、Component、参照先も保存対象になる。
2. Inspectorの`オブジェクト操作 > Prefab Asset`へ`Assets/Prefabs/Enemy.prefab`を入力する。
3. `Prefabとして保存`を押す。保存後の親には元Prefab Pathと元Object IDが記録される。
4. Projectの`.prefab`をダブルクリック、またはScene ViewへドラッグしてInstanceを生成する。
5. Instanceの値を変更して`Prefabへ適用`を押すと、現在のInstance階層でPrefab Assetを更新する。
6. `Prefabへ戻す`を押すと、現在のInstance階層を削除してPrefab Assetから再生成する。
7. 派生雛形は別Pathを入力して`Variantとして保存`を使う。Variantは元Prefab Pathを保持するが、現在は差分Assetではなく独立して生成可能な完全Snapshotとして保存される。

内部GameObject参照はPrefab内IDから新しいScene IDへ再割当てする。Prefab外を指す参照は自動解決できないため、生成後にInspectorまたはScriptで設定する。Play中のProjectダブルクリック生成は禁止し、編集Sceneを実行時に書き換えない。

### 79.2 非同期SceneとAdditive Scene

- `SceneManager::LoadSceneAsync(path)`: Worker Threadで解析し、完了FrameでPrimary Sceneを置換する。
- `SceneManager::LoadSceneAdditiveAsync(path)`: 現在のSceneへGameObjectをID再割当てして追加する。
- `SceneManager::UnloadScene(path)`: Additiveで追加したSceneだけを破棄する。Primary SceneはUnload対象にしない。
- `SceneManager::GetLoadProgress()`: 未開始0、解析中0.1から0.9、適用完了1を返す。ファイルByte数ではなく段階的な進捗である。
- `SceneManager::IsLoading()`: Future待機中ならtrue。
- `SceneManager::IsLoaded(path)`: PrimaryまたはAdditive一覧にPathがあればtrue。
- `SceneManager::SetFloat/SetString`: Primary Scene置換をまたいで次Sceneへ渡す一時データ。Save Slotへは保存しない。

Additive適用とUnloadでは既存Action Sequence、RailFollower進行、Saveセッション値を保持する。Primary Scene置換ではAction SequenceとRailFollowerの実行状態を初期化する。Scriptは描画ThreadでSceneへ適用されるまで、読み込み中SceneのGameObjectへアクセスしてはいけない。

### 79.3 Action Sequence

1. 親GameObjectへ`アクションシーケンス`を追加する。
2. `子Stepを追加`で子GameObjectと`シーケンスステップ`を作る。実行順はHierarchy順である。
3. Step種類をScript Action、待機、Active変更、Scene読込、条件、Signal待機から選ぶ。
4. `並列Group`が同じ0以上で連続するStepは同時開始する。-1は順次実行である。
5. Active条件はGameObject有効状態、体力比率、Rail進行率を比較できる。
6. Signal待機はC++から`ActionSequence{object}.Signal("BossDefeated")`を送るまで停止する。
7. ゲーム固有の敵全滅、会話、スコア条件は専用Enumへ追加せず、Script ActionまたはSignalで接続する。

### 79.4 SaveableとCheckpoint

1. 保存したいGameObjectへ`保存対象`を追加し、Scene内で重複しない保存Keyを設定する。
2. Transform、Active、Health、Rigidbody速度、C++ Script公開変数から必要な項目だけをONにする。
3. ゲーム全体の数値と文字列は`SaveSystem::SetFloat`、`SaveSystem::SetString`で登録する。
4. `SaveSystem::Save(slot)`で`SaveData`配下へVersion付きSlotを書き、`Load`で現在Sceneの同じ保存Keyへ復元する。
5. 保存Keyが重複するSceneではSave/Loadを失敗させ、誤ったGameObjectへ復元しない。
6. CheckpointはSlot名、Play開始時Save/Load、成功時Script Actionを設定できる。
7. Additive再構築でPlay開始時Checkpointを重複発火させない。同じPlayセッションで一度だけ初期処理する。

### 79.5 一括検証Scene

`ウィンドウ > ゲーム基盤検証 Scene を作成`を実行すると、次を作成する。

- `Assets/Scenes/GameplayFoundationValidation.scene`
- `Assets/Scenes/GameplayFoundationAdditive.scene`
- `Assets/Prefabs/ValidationHierarchy.prefab`
- `Assets/GameplayFoundationValidation.inputactions`

Play後、Rail進行と分岐、Wave、マウス左発射、Pool弾、Damage、Sequenceによる対象Active化、Additive読込、Checkpoint保存をConsoleとHierarchyで確認する。これは機能確認用Assetの生成であり、既存Sceneへ自動追加しない。

## 80. C++ Script Templateの調査と説明

### 80.1 Templateの基本方針

C++ Script Templateは、Engine Componentではなく、使用者がゲーム固有処理を書き始めるための下書きである。
ドキュメントでは、TemplateとComponentを混同しない。

- ComponentはScene上で再利用できる汎用機能である。
- TemplateはC++ Script Asset作成時に選ぶ初期コードである。
- Templateに書かれている処理は、使用者が編集してよい。
- Templateは固定ゲームジャンルをEngineへ埋め込むものではない。
- Templateは既存Componentを取得して呼び出す例を示す。
- TemplateはAction、公開Field、Runtime APIの接続例を含む。

### 80.2 Template選択UIで説明する項目

ProjectでC++ Script Assetを作成する時、Templateを選択できる。
Template選択UIには、最低限次を説明する。

| 項目 | 説明 |
| --- | --- |
| Category | Templateの大分類。移動、戦闘、UI、Audioなど。 |
| Display Name | UIに出る日本語名。 |
| Description | 何を始めるためのTemplateか。 |
| Recommended Components | そのTemplateと一緒に使うことが多いComponent。 |
| Policy | ゲームルールはTemplate側で編集し、Engine Managerへ固定しないこと。 |

### 80.3 現在のTemplate一覧

この一覧は`EditorNativeScriptAssetManager`のTemplate enumとTemplate infoを基準にする。
使用者向けサイトでは、英語の内部名だけでなく日本語名と用途を書く。

| Template | 日本語表示 | 主な用途 | 併用Component |
| --- | --- | --- | --- |
| Empty | 空のスクリプト | 最小構成から独自処理を書く。 | Script / MonoBehaviour |
| PlayerController | プレイヤー移動 | Vector2入力でTransformを移動する。 | PlayerInput / Input / FreeTransform |
| RailPlayer | レール移動操作 | 入力をRailMovementの左右・上下Offsetへ渡す。 | RailMovement / PlayerInput / MovementModifier |
| EnemyController | 敵の基本制御 | TargetSelectorの結果を使う敵処理の開始コード。 | TargetSelector / Health / HitscanWeapon / ProjectileEmitter |
| TurretController | 砲塔制御 | 選択TargetへYaw/Pitchを向けて射撃する開始コード。 | TargetSelector / HitscanWeapon / ProjectileEmitter |
| HomingController | 追尾制御 | TargetSteeringへ追尾開始・終了条件を追加する。 | TargetSelector / TargetSteering / Rigidbody |
| BossController | 体力フェーズ制御 | Health比率からフェーズを切り替える開始コード。 | Health / ThresholdState / ActionSequence |
| StageController | Scene進行 | 入力やゲーム条件からSceneを切り替える。 | TimelineEvent / ActionSequence / Scene Asset |
| LoadoutController | 武器切替 | WeaponLoadoutの切替・射撃・リロードを入力へ接続する。 | WeaponLoadout / WeaponLoadoutSlot / PlayerInput |
| PhysicsController | Rigidbody移動 | FixedUpdateで入力方向へ力を加える。 | RigidBody / Collider / ConstantForce |
| HealthDamageController | 体力・破壊 | Healthを監視し0以下の終了処理を書く開始コード。 | Health / DamageReceiver / Collider |
| SpawnPoolController | 生成・Pool | PrefabSpawnerまたはObjectPoolからObjectを生成する。 | PrefabSpawner / ObjectPool / WaveSpawner |
| CameraEffectsController | カメラ演出 | Camera BlendとShakeを入力・イベントから再生する。 | Camera / CameraBlend / CameraShake |
| AnimationEffectController | Animation・Effect | AnimationとParticle/VFXを同時に起動する開始コード。 | Animator / Animation / ParticleSystem / VisualEffect |
| AudioController | 音量・音響制御 | AudioSourceの公開Propertyをゲーム中に変更する。 | AudioSource / AudioReverbZone / Audio Filter |
| UiController | UIイベント | Button等からBindActionを呼ぶUI処理の開始コード。 | Canvas / Button / Text / Image / UIValueBinding |
| ActionEventController | Action・Sequence | ActionRelayとActionSequenceをゲーム条件へ接続する。 | ActionRelay / ActionSequence / TimelineEvent / PropertyTween |
| SaveCheckpointController | Save・Checkpoint | Save SlotとCheckpointを入力・イベントへ接続する。 | Saveable / Checkpoint |
| OceanBuoyancyController | 海面問い合わせ | 描画と浮力が共有するOcean表面情報を取得する。 | Ocean / Buoyancy / RigidBody |
| NavigationAiController | Target・経路AI | Target取得後のNavigation/Steering条件を書く開始コード。 | NavigationAgent / AIPathRequest / TargetSelector / AISteeringAgent |
| RuntimePropertyController | Component Property操作 | Component存在確認と公開Property変更を行う。 | 任意Component / PropertyTween / ActionRelay |

### 80.4 Templateごとに書くべき詳細

各Templateのページには、次を必ず書く。

- 作成手順: ProjectでC++ Scriptを作成し、Templateを選択し、DLLをBuildしてGameObjectへ設定する。
- 生成される主な関数: `Start`、`Update`、`FixedUpdate`、`OnAction`、物理Event、Animation Eventなど。
- 生成される公開Field: Inspectorから編集できる値と初期値。
- 生成されるAction: Inspector候補に出るAction名。
- 推奨Component: 同じGameObjectに置くもの、参照Fieldで指定するものを分ける。
- そのまま使える確認方法: Playして何を押すと何が起きるか。
- 書き換えるべき場所: ゲーム固有ルールを書く関数。
- 書き換えない方がよい場所: DLL Export、Runtime API初期化、Instance転送。
- よくある失敗: DLL Path違い、x64構成違い、Action名違い、Component未追加。

### 80.5 レールシューティングでのTemplate組み合わせ例

レールシューティング専用Managerを増やすのではなく、次のようにTemplateとComponentを組み合わせる。

1. 船またはCameraへRailMovementを追加し、Spline EditorでPathを作る。
2. Player側Scriptは`RailPlayer` Templateから作り、RailFollowerへ入力Offsetを渡す。
3. 照準はScreenAim、射撃はHitscanWeaponまたはProjectileWeaponを使う。
4. 敵はHealth、DamageReceiver、Colliderを持つPrefabにし、行動Scriptは`EnemyController`または`TurretController` Templateから作る。
5. Wave生成はWaveSpawner、演出はTimelineEventまたはActionSequenceを使う。
6. Bossは`BossController` TemplateでHealth比率を見てPhaseを切り替える。
7. BGMやSEは`AudioController` TemplateでActionに接続する。
8. HUDはCanvas、Text、Slider、UIValueBinding、`UiController` Templateで作る。

この構成では、RailMovementは「移動」、Weaponは「撃つ」、WaveSpawnerは「出す」、Scriptは「ゲーム固有条件」を担当する。

## 81. 全体ドキュメントの漏れ防止チェック

### 81.1 ページ構成の完成条件

使用者向けサイトは、機能を羅列するだけでは完成ではない。
最終成果物には、最低限次のページ群を用意する。

| ページ | 必須内容 |
| --- | --- |
| はじめに | CG2で何を作れるか、Editor / Game / Buildの違い、最初に開くScene。 |
| Project | Assets、resources、Scenes、Prefabs、Scripts、Shaders、Build対象の扱い。 |
| Scene | `.scene`の作成、保存、ダブルクリックで開く、Additive、Build Settings。 |
| Hierarchy | GameObject作成、親子関係、子Transform、Prefab Instance、Active。 |
| Inspector | Transform、Component追加、参照設定、警告、Preset、Runtime値。 |
| Scene View | Gizmo、Rail線、Collider、Physics Debug、Camera Frustum、選択操作。 |
| Game View | Play中の描画、UI、入力Focus、Standaloneとの差。 |
| Component一覧 | 全Componentのカテゴリ、表示名、内部型名、概要、必要Component。 |
| Component詳細 | 各Componentの作成手順、Inspector項目、Runtime、C++連携、制限。 |
| C++ Script | 作成、Template、Build、DLL Path、API、Action、公開Field、失敗診断。 |
| Input | Key、Mouse、GamePad、Input Action、UI Input、Action Event。 |
| Physics | Rigidbody、Collider、Joints、Forces、Buoyancy、Debug表示、Layer。 |
| Rendering | Model、Sprite、Material、Light、Reflection、PostProcess、Water。 |
| Audio | AudioSource、Listener、3D音響、遮蔽、Doppler、Reverb、Bus。 |
| UI | Canvas、Text、Image、Button、Slider、UIValueBinding、解像度。 |
| Animation / Effect | Animation、Animator、Event、Particle、VisualEffect、Pool。 |
| Gameplay基盤 | RailMovement、Weapon、Health、Wave、Sequence、Save、Prefab。 |
| Build | Editor Build、Game Build、Debug / Release、Resource Copy、起動確認。 |
| Troubleshooting | 起動しない、表示されない、音が出ない、Actionが来ない、Build失敗。 |

### 81.2 1ページごとの共通必須項目

各ページには次を入れる。

- 目的: 何をするページか。
- 使う場面: どんな作業で読むか。
- 最小手順: 初めて使う人が動かせる手順。
- 詳細手順: Inspector、Project、Hierarchy、Scene Viewを含む手順。
- 設定一覧: 表示名、型、初期値、単位、範囲、保存、Runtime反映。
- C++ Script連携: API、Template、Action、RuntimeProperty。
- 組み合わせ: 一緒に使うComponent、競合するComponent。
- Debug表示: Scene View、Console、Inspector Runtime値。
- Build時の注意: Standaloneで必要なAsset、Path、Scene登録。
- 失敗診断: 現象別の確認順。
- 制限: 未実装、近似、Editor専用、確認不足を明記。

### 81.3 使用者操作の書き方

操作手順は、実際のEditor上の導線で書く。

良い書き方:

1. Projectで`Assets/Scenes`を開く。
2. `.scene`をダブルクリックしてSceneを開く。
3. HierarchyでGameObjectを選ぶ。
4. Inspectorの`コンポーネントを追加`を押す。
5. `ゲームプレイ > レール移動`を追加する。
6. `Splineを作成して接続`を押す。
7. Scene ViewでRail線と制御点を確認する。
8. PlayしてGame Viewで動作を見る。

避ける書き方:

- 内部Manager名だけで説明する。
- ファイルPathを直接編集する前提だけにする。
- Editor上のボタン名や配置を書かない。
- BuildやStandaloneでの差を書かない。
- 実装未確認のものを「できます」と断定する。

### 81.4 Component詳細の追加調査手順

新しいComponentや既存Componentの変更が入ったら、次の順で調べる。

1. `EditorComponentType`のenum名を確認する。
2. Add Component Popupのカテゴリと日本語表示名を確認する。
3. `CreateComponent`の初期値を確認する。
4. Inspectorで表示される項目名、範囲、ボタンを確認する。
5. Scene Save / Loadの対象Fieldを確認する。
6. Runtime Managerが読んでいるFieldを確認する。
7. Render / Physics / Audio / Input / UIへ接続されているか確認する。
8. C++ Script Wrapper、Runtime API、RuntimeProperty登録を確認する。
9. Action送受信がある場合、Action名と値を確認する。
10. Debug表示やScene View Gizmoがあるか確認する。
11. Build後に必要なAssetやResource Copyを確認する。

### 81.5 C++ Script詳細の追加調査手順

C++ Script APIやTemplateが増えたら、次を更新する。

1. `EditorScriptApi.h`のRuntime API構造体を確認する。
2. ABI互換のため末尾追加かどうか確認する。
3. `EditorNativeScript.h`の高水準Wrapperを確認する。
4. `EditorScriptManager`のBridge関数を確認する。
5. 失敗時にfalseを返す条件を確認する。
6. 生成TemplateのHeader / Sourceに何が出るか確認する。
7. Action候補Exportが生成されるか確認する。
8. InspectorのTemplate選択UIに表示される説明と推奨Componentを確認する。
9. サンプルコードが実際の関数名と一致しているか確認する。
10. 古いDLLとの互換、直接入力、API Version違いを説明する。

### 81.6 Rail / レールシューティング関連の漏れ防止

レールシューティングを作る説明では、ゲーム専用Managerを前提にしない。
次を個別に説明する。

| 作業 | 説明対象 |
| --- | --- |
| レールを作る | Spline Editor、Rail Path、制御点、Scene View線表示。 |
| レールを動く | RailMovement、速度、加速、進行率、終端、Loop。 |
| レール内で動く | 左右範囲、上下範囲、PlayerInput、Script入力。 |
| 物理で動く | Dynamic Rigidbody追従、Collider、追従軸、ばね、減衰。 |
| 海で動く | Ocean、Buoyancy、浮力併用プリセット、描画波と物理波の一致。 |
| 経路を分ける | RailBranch、Script Trigger、進行率維持。 |
| 敵を出す | WaveSpawner、PrefabSpawner、ObjectPool。 |
| 撃つ | ScreenAim、HitscanWeapon、ProjectileEmitter、DamageReceiver。 |
| 演出する | TimelineEvent、ActionSequence、CameraBlend、CameraShake、AudioSource。 |
| 表示する | Canvas、Text、Slider、UIValueBinding、Reticle。 |
| 保存する | Saveable、Checkpoint、Scene間一時データ。 |

### 81.7 トラブルシュートの最低項目

Troubleshootingには、現象別に確認順を書く。

| 現象 | 確認順 |
| --- | --- |
| 起動しない | Debug/Release構成、作業Directory、必要Scene、resources、DLL、Console。 |
| Sceneが開かない | `.scene`存在、Project上のPath、JSON破損、参照先Asset。 |
| GameObjectが出ない | Active、Layer、Camera Culling、Transform、Scale、Renderer、Material。 |
| Spriteが出ない | Texture Path、SRV、Material、Canvas/World、Alpha、Draw順。 |
| Modelが暗い | Light、Environment、Material、Normal、Reflection、PostProcess。 |
| Rail線が見えない | Scene View、Rail Path参照、制御点2点以上、選択状態、全点同一位置。 |
| Railが動かない | Play中、Paused、Speed、Path長、Rigidbody/Collider、追従軸。 |
| 物理が効かない | Rigidbody Dynamic、Collider、Layer、Kinematic、Freeze、FixedUpdate。 |
| 浮力がおかしい | Ocean参照、Dynamic Rigidbody、排水形状に使うCollider、船体Size、重心、浮力、抵抗、Rail追従軸。通常経路はColliderの実Shape体積を使うため、上部構造を含む過大なBoxを船体Colliderにしない。 |
| Actionが来ない | Action対象、DLL Path、Template Export、Action名、Component有効。 |
| UIが反応しない | Canvas、EventSystem、Input Module、Raycast Target、Focus。 |
| 音が出ない | AudioListener、Clip、Volume、Bus、3D距離、Voice上限。 |
| Buildで動かない | Build Settings、Scene登録、Asset Copy、相対Path、Debug/Release差。 |

### 81.8 未確認を残さないための表記

調査が終わっていない機能には、次のどれかを書く。

| 表記 | 意味 |
| --- | --- |
| 実装済み | Editor操作、保存、Runtime、Buildで確認済み。 |
| Editor設定のみ | Inspector項目はあるがRuntime未接続。 |
| Runtime接続あり | Play時に反映されるがBuild未確認。 |
| Editor専用 | Scene ViewやInspectorだけで使う機能。 |
| 互換用 | 旧Scene読み込みのために残している型。 |
| 未確認 | コード上は存在するが動作確認が足りない。 |

「多分」「おそらく」「Unityと同じ」のような表現で完成扱いにしない。

### 81.9 最終レビュー手順

ドキュメント更新後は、次を確認してから完成扱いにする。

1. 3つのseedファイルがUTF-8 BOM付きで保存されている。
2. `git diff --check`が通る。
3. 新しいComponent、API、Templateが3ファイルのどこかに反映されている。
4. 実装名とドキュメント名が一致している。
5. 日本語表示名と内部型名を混同していない。
6. 使用者操作がProject / Scene / Hierarchy / Inspector基準で書かれている。
7. ゲーム固有処理をEngine基礎機能として説明していない。
8. 未実装や未確認を断定していない。
9. Troubleshootingに失敗時の確認順がある。
10. Build / Standaloneの注意がある。

## 82. 再利用Gameplay機能の現行実装状況

外部サイトの記述だけで実装有無を判断せず、Component enum、Inspector追加一覧、Scene保存、Runtime Manager、C++ Script bridgeの5箇所を確認する。現行コードでは次の機能が実体まで接続済みである。

| 機能 | Inspector | Scene保存 | Runtime | C++ API | 主用途 |
| --- | --- | --- | --- | --- | --- |
| WeaponLoadout / Slot | あり | あり | WeaponLoadoutManager | `WeaponLoadout` | 可変装備、弾薬、Reload、Visual切替 |
| TargetSelector | あり | あり | TargetingManager | `Targeting` | 距離、角度、遮蔽、Team、Priority選択 |
| TargetSteering | あり | あり | TargetingManager | RuntimeProperty連携 | 追尾、砲塔、Drone、Projectile |
| PropertyTween | あり | あり | RuntimePropertyManager | `PropertyTween` | 公開Propertyの時間補間 |
| Runtime Property | 対応Component側 | Scene値を操作 | RuntimePropertyManager | `RuntimeProperty` | Ocean、Light、Rail、Audio等の共通操作 |
| Ocean Query | Ocean側 | Ocean設定を利用 | Ocean System | `Physics::SampleOceanSurface` | 描画・Buoyancyと同じ波面の取得 |
| TargetPoint | あり | あり | TargetingManager | Target参照として取得 | 大型敵の部位、弱点、注視点 |
| Team | あり | あり | TargetingManager | RuntimeProperty | 敵味方、Neutral、Target除外 |
| DamageContext | DamageReceiverと連携 | Runtime情報 | DamageManager | `Health` | 攻撃者、命中位置、法線、Impulse、UserTag |

### 82.1 大型敵へ部位Targetを作る手順

1. 敵RootへCollider、Health、DamageReceiver、Teamを追加する。
2. Team IDをゲーム側の敵IDへ設定する。
3. Rootの子へ主砲、レーダー、エンジン等の空GameObjectを作る。
4. 各子へTargetPointを追加し、Scene Viewの球を見ながら位置と半径を調整する。
5. 重要部位ほどPriorityを大きくする。
6. Player側TargetSelectorを「別Team」「優先値」へ設定する。
7. Play中の現在Target IDとTarget変更Actionを確認する。

### 82.2 Loadoutから射撃までの手順

1. PlayerまたはWeaponRootへWeaponLoadoutを追加する。
2. 子Slotを必要数作る。
3. 各SlotのWeapon ObjectへHitscanWeaponまたはProjectileEmitter所有者を指定する。
4. Visual、Magazine、Reserve、Reload時間を設定する。
5. LoadoutController Templateを作成し、InputへNext、Previous、Reload、Fireを割り当てる。
6. HUDは`GetAmmo`またはUI Value Bindingへ接続する。
7. 発射できない場合はSlot選択、残弾、Reload中、Weapon参照、Weapon cooldown、Input Actionの順に確認する。

### 82.3 Damage情報をゲーム処理へ使う手順

1. 被弾対象へHealthとDamageReceiverを追加する。
2. DamageReceiverの被弾ActionをC++ Scriptへ接続する。
3. Action内で`GetLastDamageContext`を読む。
4. `instigatorGameObjectId`からScoreやKill判定の所有者を決める。
5. `hitPosition`と`hitNormal`へHit EffectやDecalを配置する。
6. `impulse`は必要な攻撃だけ設定し、Damage値から常時自動生成しない。
7. `userTag`の意味はゲーム側Data AssetまたはScript定数で管理する。

### 82.4 互換性と制限

- Teamを持たない旧SceneはNeutralとして扱い、初期設定ではTarget候補へ含める。
- Team IDの意味はEngineが固定しない。Player、Enemy、Boss等のEnumはゲーム側へ置く。
- TargetPointはTarget位置を提供する。部位ごとのHealthやDamage倍率は子Collider、DamageReceiver、Scriptで構成する。
- DamageContextは最後に適用された1件を対象ごとに保持する。同一Frameの全Hit履歴が必要な場合は被弾Action内でゲーム側配列へ転記する。
- PropertyTweenは登録済みPropertyだけを操作する。任意メンバー名への未検証アクセスは行わない。

## 83. 全機能ドキュメント完成監査

### 83.1 現行コードを正とする対象数

現行コードでは、`EditorScene.h`の`EditorComponentType`に280 Component（`Count`を除く。うち「コンポーネントを追加」から選べるのは276、残り4は旧Scene読み込み専用のLegacy互換スロット）、`EditorScriptApi.h`の`EditorScriptRuntimeApi`に229 API Entry、`EditorNativeScript.h`に61の高水準Wrapper Class、`EditorNativeScriptAssetManager.cpp`に26のC++ Script Templateがある。文書完成判定では、最近追加した機能だけでなく、この全件を機械照合する。

全件の実データは、Componentが`component-documentation-detail-seed.md`の「全280 Component Inspector値表」、API・型・Wrapperが`cpp-script-documentation-detail-seed.md`の「Runtime API・型・Wrapper 完全リファレンス」にある。この章より下に出てくる旧件数は、その章を書いた時点の履歴であり、最新値はこの節を正とする。

| 対象 | 正本 | 詳細を書く文書 | 完成条件 |
| --- | --- | --- | --- |
| Component 280件 | `EditorScene.h`の`EditorComponentType` | `component-documentation-detail-seed.md` | 全内部名が存在し、似たComponentとの差、設定、依存、Runtime状態、Debugが説明される。 |
| Runtime API 229件 | `EditorScriptApi.h`の`EditorScriptRuntimeApi` | `cpp-script-documentation-detail-seed.md` | 全Entry名が存在し、推奨Wrapper、引数、戻り値、失敗条件、必要Componentが説明される。 |
| C++ Script Template | `EditorNativeScriptAssetManager`のTemplate定義 | C++ Script文書と利用者文書 | 作成手順、生成物、公開Field、Action、推奨Component、変更箇所が説明される。 |
| Editor Window/Workflow | Menu、Window Manager、Project/Hierarchy/Inspector実装 | 本文書 | Projectから作る実操作、保存先、Play/Build、失敗時確認が説明される。 |

名称が1回出るだけでは完成ではない。次の品質監査を別に通す。

1. その機能が何を担当し、何を担当しないか分かる。
2. Project/Hierarchy/Inspectorの操作順が分かる。
3. 必須Component、任意Component、競合Componentが分かる。
4. 主要Inspector項目の単位、範囲、初期値、Runtime反映時期が分かる。
5. C++ Script、Action、Runtime Propertyのどれから操作できるか分かる。
6. Scene保存、Prefab、Buildへの持越し条件が分かる。
7. Scene View Debug、Console、戻り値を使った確認順が分かる。
8. 型だけ存在する機能を実装済みと誤記していない。

### 83.2 利用者の開始地点

ドキュメントはEngine内部Classから始めない。利用者がゲームを作る順番を主導線にする。

1. `CG2.sln`を開き、DebugまたはRelease x64を選ぶ。
2. CG2 Editorを起動し、Projectウィンドウの`Assets/Scenes`を開く。
3. `.scene`をダブルクリックし、Scene View、Hierarchy、Inspectorへ開く。
4. GameObjectを作り、Add Componentから機能を組み合わせる。
5. AssetをProjectからSceneまたはInspector参照欄へ割り当てる。
6. Sceneを保存し、PlayでScene ViewとGame Viewを確認する。
7. C++ Scriptが必要ならProjectで作成し、TemplateまたはEmptyから始める。
8. Build Settingsへ起動Sceneと遷移Sceneを登録する。
9. Standalone Buildを作り、Editorを介さずexe、Scene、Asset、Resourceを確認する。

Project外の絶対Pathを直接選ばせる手順は、Import機能の説明以外では主導線にしない。AssetはProjectに入り、ProjectからSceneへ使う。

### 83.3 Editor基本機能の詳細対象

| 機能群 | 必ず書く内容 | 最低確認 |
| --- | --- | --- |
| Project | Assets/resources/Scenesの意味、Import、作成、Rename、Move、Delete、Search、Refresh。 | Move後の参照維持、Build copy、拡張子別Preview。 |
| Scene | 新規作成、保存、別名保存、ダブルクリックOpen、複数Scene、Additive、未保存警告。 | Title/Stage/Result間遷移、再起動後の再読込。 |
| Hierarchy | 親子化、子順、折り畳み、複数選択、複製、Prefab、Active。 | 親移動/回転/Scaleが子World Transformへ反映される。 |
| Inspector | Component追加/削除/並替、複数編集、参照割当、Reset、Preset。 | Scene保存後に全値が戻る。Play中変更の保持範囲。 |
| Scene View | 移動/回転/Scale Gizmo、Local/World、Snap、Focus、Camera操作、Debug種別。 | Collider、Rail、Physics vector、Audio cone、Nav path。 |
| Game View | 実Camera、解像度、Aspect、Input focus、UI、Play/Pause/Step、FPS。 | Scene View専用Gizmoが混入しない。 |
| Console | Log/Warning/Error、Source、Clear、Filter、Play開始時処理。 | Script失敗、Asset欠落、Shader compile、Scene loadを追える。 |
| Build Settings | Scene順、起動Scene、Debug/Release、出力先、Asset copy。 | 出力exeだけで起動し、必要Sceneを遷移できる。 |

### 83.4 描画と見た目の詳細対象

描画文書は「綺麗になる」とだけ書かず、入力Buffer、設定Component、Pass順、品質差、失敗時の見え方を書く。

| 領域 | 詳細対象 | 接続確認 |
| --- | --- | --- |
| Model/Sprite/UI | Mesh、SubMesh、Material、Texture、Sampler、Alpha、Sort、SDF Text。 | PNG/FBX/OBJをProjectから配置し、Standaloneでも表示する。 |
| Lighting | Directional/Point/Spot、Environment、Shadow、Probe、IBL。 | Lightなし、Environmentのみ、複数Lightで比較する。 |
| Material/PBR | Base Color、Normal、Metallic、Roughness、AO、Emission、Reflectance。 | 値変更がGBuffer/Forward pathへ届く。 |
| Reflection | ReflectionProbe、SSR、Planar、Cubemap、Fresnel、roughness mip。 | Probeなし/あり、画面外、平面位置、粗さを比較する。 |
| Ocean | FFT Spectrum、LOD、Normal、Foam、Refraction、Absorption、Shallow、Sun glint。 | 高低差、滑らかさ、光角度、遠景、継ぎ目、Buoyancy一致。 |
| Transparency | Alpha Blend、Weighted OIT、水/ガラス専用Pass、Depth。 | 前後順、屈折Object、Particle、水面重なり。 |
| PostProcess | Exposure、Bloom、AA、AO、SSR、Tone Map、Vignette、Grain、CA。 | Inspector値がHardcodeで上書きされない。Scene/Game temporalを分離する。 |
| Optimization | Frustum、Hi-Z、LOD、Instancing、Indirect、VRAM、Shader variant。 | CPU readback、Draw call、GPU ms、見た目差を計測する。 |

### 83.5 3D物理の詳細対象

| 領域 | 詳細対象 | 検証Scene |
| --- | --- | --- |
| Body/Collider | Mass、Inertia、Center of Mass、Motion Type、CCD、Friction、Restitution、Layer。 | 落下、斜面、積重ね、高速弾、Trigger。 |
| Force/Torque | Force、Impulse、Force At Position、Torque、ConstantForce、Gravity Field。 | 同質量/異質量、重心Offset、Fixed timestep。 |
| Joint/Constraint | Hinge、Fixed、Spring、Configurable、Character、Transform Constraint。 | Limit、Motor、Break、親子/Animation競合。 |
| Vehicle/Aero | Wheel、Suspension、Thruster、Aerodynamics、Wind、Upright。 | 車、船、飛行機で抗力・揚力・横力・圧力中心を見る。 |
| Rope/Mechanism | Rope、Pulley、Torsion Spring、Servo。 | Attach/Detach、巻取り、破断/Repair、張力Debug。 |
| Field/Fluid | FluidVolume、Vortex、Pressure、Electromagnetic、RotatingFrame。 | 範囲境界、最大Force、反作用、Debug vector。 |
| Ocean physics | Ocean Sample、実Shape水没体積、浮心、水線面積、面分布静水圧、二次抗力、Slamming、Rail併用。 | 描画頂点と同じ時間・Spectrumを固定5x5 Sampleし、実Collider表面へ補間した局所水位・法線・速度、部分浸水、浮心移動、`dV/dh`水線面積、排水体積履歴、Pool再利用を確認する。 |

2D物理型は互換データであり、現行Runtime実装として案内しない。この方針はComponent文書へ型名単位で記載する。

### 83.6 ゲームプレイ基盤の詳細対象

| 作りたい処理 | 基盤Component/API | ゲーム側へ残す判断 |
| --- | --- | --- |
| 経路移動 | RailMovement、MovementModifier、RailBranch、RailFollower API。 | 敵全滅時停止、Stage分岐条件、Score条件。 |
| 照準とTarget | ScreenAim、TargetSelector、TargetPoint、Team、Physics Cast。 | Lock優先規則のゲーム固有補正、弱点倍率。 |
| 射撃 | HitscanWeapon、ProjectileEmitter、WeaponLoadout、ObjectPool。 | 武器種類、弾Data、入力、演出、Upgrade。 |
| Damage | Health、DamageReceiver、DamageContext。 | Score、属性、部位破壊、死亡後処理。 |
| Spawn | PrefabSpawner、WaveSpawner、ObjectPool。 | どの敵をいつ出すか、勝敗条件。 |
| 順次処理 | ActionSequence、ActionRelay、TimelineEvent、ThresholdState。 | Boss登場などAction名の意味。 |
| Camera | Camera、CinemachineCamera、Blend、Shake、Constraint。 | Stageごとの構図、演出Timing。 |
| UI/HUD | Canvas、TMP Text、Image、Slider、UIValueBinding、Input UI。 | 配置、Art、表示値の意味。 |
| Audio | Source、Listener、Bus、3D距離、Cone、Occlusion、Filter、Reverb。 | BGM/SE選択、演出Timing。 |
| Save/Scene | Saveable、Checkpoint、SceneManager、Scene/Save Data。 | 何を保存するか、Scene構成、Slot UI。 |

Engineは「レール上を移動する」「対象を選ぶ」「武器を発射する」まで提供する。「レール上を移動してPlayerを攻撃する敵」の完成ルールを単一Componentへ固定しない。よく使う組合せはC++ Script Templateとして提供する。

### 83.7 Animation・Effect・Audioの詳細対象

| 領域 | 必ず区別する項目 |
| --- | --- |
| Animation | Clip再生とAnimator State Machine、Parameter、Blend、Layer、AvatarMask、Root Motion、IK、Animation Event、Skinned Motion Vector。 |
| Constraint | Animation前後の評価順、Transform親子、Physicsとの競合、Weight補間。 |
| Particle | SimulationとRenderer、Spawn/Lifetime、Collision、Depth Collisionの限界、Physics/SDFとの使い分け、Pool。 |
| Visual Effect | Effect Asset、公開Parameter、Play/Stop、World/Local、Decal/Light/Audio連携。 |
| Audio playback | SE/BGM/Voice、Loop、Fade、Pitch、Pause、Voice上限、Bus。 |
| 3D Audio | Listener、距離減衰、Cone方向、Doppler、Occlusion、Reverb Zone。 |
| Audio Filter | Low/High Pass、Echo、Distortion、Reverb、Chorusの順序と二重適用。 |

### 83.8 AI・Navigationの詳細対象

AIは名称を並べるだけでは使えない。各方式でData作成、実行単位、状態可視化、外部依存を分ける。

| 方式 | 作成Data | 実行Debug | 外部/前提 |
| --- | --- | --- | --- |
| Behavior Tree | Root、Composite、Task、Decorator、Blackboard。 | Running Node、Success/Failure、Abort。 | Script Action、Blackboard key。 |
| State Machine | State、Transition、条件、Enter/Exit。 | Current State、成立Transition。 | Action target。 |
| GOAP | Goal、Action前提/効果、World State、Cost。 | 選択Goal、Plan、再計画理由。 | World State供給。 |
| HTN | Domain、Task、Method、分解条件。 | 分解Tree、実行Task、失敗位置。 | Domain Asset/Hierarchy。 |
| Pathfinding | Surface/Grid、Agent、Obstacle、Link、Request。 | Path、Corner、到達/失敗、再探索。 | Bake data、Recast/MicroPather設定。 |
| Steering | Seek/Flee/Arrive/Pursuit/Wander/Avoid/Flock。 | 各Force vector、合成Weight、速度。 | Target、Neighbor、Physics Query。 |
| Vision/ML/Voice | Camera/Audio input、Model、Threshold、Command。 | Raw input、検出結果、Confidence。 | OpenCV/Whisper Model、Device。 |

### 83.9 C++ Script説明の完成条件

各APIページに次を固定順で置く。

1. 何ができるかと責務外。
2. 必要ComponentとInspector設定。
3. 最小の型付きWrapper例。
4. 引数の座標系、単位、寿命、null許可。
5. 戻り値とfalse/負値の全条件。
6. Update、FixedUpdate、Actionのどこで呼ぶか。
7. Scene reload、Object destroy、Pool返却時の参照寿命。
8. Console、Scene View、Component状態によるDebug。
9. Runtime APIとの正確な対応名。
10. Standaloneで必要なDLL、Asset、Scene。

Templateページは完成コードとして扱わない。公開Field、推奨Component、生成Action、変更する関数、削除可能な例示部分を明記する。Templateにゲーム固有Managerを隠して依存させない。

### 83.10 全体Troubleshootingの調査順

| 症状 | 最初に見る層 | 次に見る層 | 最後に見る層 |
| --- | --- | --- | --- |
| 起動/白画面 | exe作業Directory、起動Scene、Resource。 | 初期化Log、Shader/Texture load、GPU fence。 | RenderDoc/Debugger。Main loop到達だけで直ったとしない。 |
| Scene内容が違う | 開いているScene Path、未保存、Hierarchy Active。 | Scene deserialize、Asset参照、Prefab override。 | Runtime Synchronizer。 |
| 見えない | Camera/Layer/Transform/Scale。 | Renderer/Material/Texture/Light。 | Draw call、PSO、Descriptor、Shader output。 |
| 物理が違う | Dynamic/Collider/Layer/重力/Freeze。 | Fixed timestep、Mass/Inertia、力の作用点。 | Solver contact、CCD、Debug vector。 |
| Scriptが動かない | Script Component、DLL path、Active。 | API Version、Export、Action名、戻り値。 | Bridge Log、Debugger attach。 |
| Buildだけ失敗 | Configuration、Platform、Output path。 | Scene/Asset/DLL copy、相対Path。 | Debug/Release定義差、Runtime library。 |

### 83.11 更新時の機械監査

ComponentまたはRuntime APIを追加・削除した変更では、文書更新後に次を実行する。単純文字列照合は説明品質を保証しないが、完全な記載漏れを検出できる。

```powershell
$scene = Get-Content Source\Engine\Editor\EditorScene.cpp -Raw
$componentBlock = [regex]::Match(
    $scene,
    'kEditorComponentTypeNames\[\]\s*=\s*\{(?<body>[\s\S]*?)\};').Groups['body'].Value
$componentNames = [regex]::Matches($componentBlock, '"([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value }
$componentDoc = Get-Content docs\component-documentation-detail-seed.md -Raw
$componentNames | Where-Object {
    $componentDoc -notmatch [regex]::Escape($_)
}

$apiHeader = Get-Content Source\Engine\Core\EditorScriptApi.h -Raw
$apiBlock = [regex]::Match(
    $apiHeader,
    'struct EditorScriptRuntimeApi\s*\{(?<body>[\s\S]*?)\n\};').Groups['body'].Value
$apiNames = [regex]::Matches($apiBlock, '\(\*([A-Za-z0-9_]+)\)') |
    ForEach-Object { $_.Groups[1].Value }
$apiDoc = Get-Content docs\cpp-script-documentation-detail-seed.md -Raw
$apiNames | Where-Object {
    $apiDoc -notmatch [regex]::Escape($_)
}
```

出力が0件であることに加え、UTF-8 BOM、`git diff --check`、内部名と日本語表示名、実装状態表記、Projectから始まる操作手順を人手で確認する。

## 84. 一つずつ詳細に書くための完成基準

### 84.1 Componentページは一覧表だけで完成にしない

280 Component（追加可能276＋Legacy互換4）は、カテゴリ説明に名前が含まれるだけでは未完成である。各Componentについて、最低でも次の情報を個別に確定する。

| 必須項目 | 書く内容 | 調査元 |
| --- | --- | --- |
| 表示名と内部名 | Add Componentの日本語名、`EditorComponentType`内部名、カテゴリ。 | `EditorInspectorPanel.cpp`のCatalog、`EditorScene.cpp`。 |
| 目的 | 何を入力として何を出力するComponentか。 | Runtime Manager、Renderer、Physics Bridge。 |
| 責務外 | 何をこのComponentへ入れず別Component/Scriptへ分けるか。 | Manager境界、設計方針。 |
| 追加手順 | HierarchyでどのObjectへ追加し、Projectの何を割り当てるか。 | Add Component UI、Inspector。 |
| Inspector全項目 | 日本語Label、内部Field、型、単位、範囲、初期値。 | Inspector描画関数、`EditorComponent`、初期化処理。 |
| 参照 | GameObject/Asset/Scene参照、空時のFallback、削除時の挙動。 | Scene同期、Runtime Manager。 |
| 必須構成 | 同じObject、親、子、Scene内に必要なComponent。 | Runtimeの検索処理。 |
| Runtime | Update/FixedUpdate/Drawのどこで、何を変更するか。 | ManagerのUpdate順。 |
| 保存 | Scene/Prefabへ保存する値、Play開始時初期化値、Runtimeのみの値。 | Serialize/Deserialize、Play lifecycle。 |
| C++連携 | 専用Wrapper、Runtime Property、Action、読み取り専用値。 | Native Script Header、Runtime Property Registry。 |
| Debug | Scene View表示、Inspector Runtime値、Console、戻り値。 | Debug Draw、Manager Log。 |
| 競合 | 同じTransform、Material、Body、UI値を更新する他Component。 | Update順、実装上の書込先。 |
| 制限 | 未実装、互換用、Editor専用、Build未確認、外部依存。 | 実コードとBuild結果。 |
| 最小例 | 追加からPlay確認までの短い手順。 | 実Editor操作。 |

「Rigidbody系」「UI系」「AI系」のようなまとめ説明の後にも、各型の差分を個別に書く。共通項目は共通表を参照してよいが、そのComponentで意味を持つ設定と意味を持たない設定を明記する。

### 84.2 Componentの状態表記はコード経路単位にする

| 状態 | 必要な証拠 |
| --- | --- |
| 型定義のみ | Enum/Scene Fieldだけがある。 |
| Inspector設定あり | Add ComponentとInspector編集、Scene保存がある。 |
| Runtime接続あり | ManagerがPlay中に読んで実処理へ渡す。 |
| 描画接続あり | Draw Queue/Pass/Shaderへ値が届く。 |
| 物理接続あり | Physics Body/Shape/Force/Queryへ値が届く。 |
| Script接続あり | Wrapper、Action、Runtime Propertyのいずれかがある。 |
| Standalone確認済み | EditorなしのexeでAssetを含めて動作確認済み。 |

1つのComponentが複数状態を持つ場合は、「Inspector/保存は実装済み、Runtimeは未接続」のように分けて書く。型名がUnityに存在することを実装証拠にしない。

### 84.3 C++ APIページは関数名一覧だけで完成にしない

229 Runtime API Entryと61の型付きWrapper Classについて、最低でも次を個別に書く。

| 必須項目 | 書く内容 |
| --- | --- |
| 完全Signature | Return type、関数名、全引数、const、参照/Pointer。 |
| 高水準入口 | 通常Scriptが使うClassとMethod。Wrapperがなければ低水準のみと明記する。 |
| API Version | Entryを安全に使える最低Version。末尾追加によるABI互換。 |
| 引数 | null/空許可、範囲、単位、Local/World、入力Buffer寿命。 |
| 戻り値 | true/false、負ID、0、既定構造体の正確な意味。 |
| 必須Component | Owner/Targetに必要なComponentとActive条件。 |
| 呼出Timing | Start、Update、FixedUpdate、Action、停止時のどこで使うか。 |
| 副作用 | Transform、Physics、Scene、Save、Pool、Action Queueの何を変えるか。 |
| 参照寿命 | Scene切替、Destroy、Pool返却後にID/Pointerが使えるか。 |
| Thread | Main thread限定、Async結果の受取方法。 |
| 失敗条件 | APIなし、Version不足、参照不正、Component不足、未命中、範囲外を分ける。 |
| 最小コード | 正常系だけでなく戻り値確認を含める。 |
| Debug | Consoleへ何を出し、Inspector/Scene Viewのどの状態を見るか。 |

### 84.4 Native Script自体の説明範囲

C++説明はAPIだけでなく、ゲームを作るためのScript作成と運用全体を含める。

1. ProjectでC++ Scriptを作成する場所とTemplate選択。
2. 生成Header/Source、DLL Project、Build output。
3. Debug/Release x64の違いと依存Runtime DLL。
4. Factory/Load/Unload/Instance生成のExport。
5. `EditorNativeScriptRuntime`へAPIを設定するTiming。
6. GameObjectごとのInstance分離。
7. 公開Fieldの型、Address寿命、Inspector同期。
8. Action候補ExportとInspector候補表示。
9. Start/FixedUpdate/Update/Action/Stopの順序。
10. Collision/Trigger callbackのEnter/Stay/Exitと相手ID。
11. Scene reload、Play Stop、Pool再利用時のreset。
12. Console Log、Debugger attach、PDB配置。
13. Standalone BuildへのDLL/依存DLL copy。
14. API Version不一致時の安全な失敗。

### 84.5 記載内容と実装の再照合

詳細を書いた後は、文章量ではなく次を確認する。

1. Inspector Labelが現在の日本語表示と一致する。
2. 範囲Clampと初期値がコードと一致する。
3. degree/radian、m、m/s、N、N m、秒、ms、0-1を区別している。
4. Transform更新とPhysics Forceを混同していない。
5. Action通知と直接API呼出を混同していない。
6. Scene一時DataとSave Slotを混同していない。
7. ObjectPool返却とGameObject無効化を混同していない。
8. `true`を「命中」「完了」と誤解せず、そのAPIの成功段階を記載している。
9. 互換Componentと新規推奨Componentを区別している。
10. Editorで設定できるだけの機能をStandalone完成扱いしていない。

### 84.6 3文書の役割分担

| 文書 | 詳細の中心 | 重複しても残す情報 |
| --- | --- | --- |
| `component-documentation-detail-seed.md` | 280 Componentの個別設定、全Inspector値、Runtime、連携、Debug。 | 必須Component、C++入口、失敗確認。 |
| `cpp-script-documentation-detail-seed.md` | Native Script lifecycle、61 Wrapper Class、229 Runtime API、共有型全件、コード例。 | 対応Component、単位、失敗条件。 |
| `user-documentation-research-spec.md` | ProjectからBuildまでの利用者導線、調査方法、完成監査。 | 実装状態、確認手順、文書間の参照先。 |

3文書は同じ文章を複製するのではなく、Componentから探す利用者、C++から探す利用者、ゲーム制作手順から探す利用者の3つの入口を提供する。

## 85. 水上3Dレールシューティング向け不足機能の利用者導線

この章はゲーム固有のRailShooter Managerを増やす手順ではない。既存のRailMovement、WeaponLoadout、TargetSelector、TargetSteering、TargetPoint、Team、DamageContext、Ocean Query、Runtime Property、PropertyTweenへ、汎用基盤を組み合わせる手順である。

### 85.1 現行実装状態

| 機能 | Add Component | Inspector | Scene保存 | Play Runtime | C++ Wrapper/Action |
| --- | --- | --- | --- | --- | --- |
| Timer / Scheduler | タイマー | 時間、Repeat、自動開始、Pause、Action | 実装済み | 1回/反復、停止/再開、残り秒 | `Timer`、PayloadなしAction |
| Generic State Machine | 汎用ステートマシン | 初期/現在State、変更Action | 実装済み | 任意文字列State保持 | `GenericStateMachine`、String Payload |
| Attribute / Resource | 属性・リソース | Name、Min/Max/Current、毎秒回復、Action | 実装済み | Clamp、正負の毎秒変化 | `Attribute`、Float Payload |
| Typed Action Payload | Componentではない | Action選択UIを使用 | Action名/対象を保存 | Queue経由でContextへ格納 | `ActionPayload`、API v7 |
| Destructible Part | 破壊可能部位 | Health、無効化対象、子、Action | 実装済み | Health 0で1回処理 | GameObject Payload |
| Formation Follower | 編隊追従 | Leader、Local Offset、追従速度 | 実装済み | Leader回転込みTransform追従 | Runtime Propertyで値調整可 |
| Target Lock | ターゲットロック | Selector、時間、猶予、3 Action | 実装済み | 進行、完了、喪失 | `TargetLock`、GameObject Payload |

「実装済み」はDebug x64のコンパイルと`CG2.exe`生成を指す。Editor操作の視覚確認、Standalone Releaseでの長時間Play、大量同時Object負荷は別の受入試験として残す。

### 85.2 ProjectからSceneへ設定する共通手順

1. Projectの`Assets/Scenes`から対象`.scene`をダブルクリックして開く。
2. Hierarchyで機能を所有するGameObjectを選ぶ。
3. Inspectorの「コンポーネントを追加」から日本語表示名を検索して追加する。
4. GameObject参照欄へHierarchy Objectを割り当てる。未設定時Ownerになる欄と、必須のLeader欄を区別する。
5. Action対象へC++ Script Componentを持つGameObjectを設定する。
6. Action欄では、そのScriptが`BindAction`で公開した候補を選ぶ。文字列を手入力した場合は大文字小文字を一致させる。
7. Sceneを保存し、PlayでRuntime表示値とAction結果を確認する。
8. Stop後にRuntime値が編集初期値へ戻ることを確認する。

Component追加だけでゲームルールは完成しない。Timerが発火しても攻撃内容はC++ ScriptまたはWeapon Component側、Stateが変化しても各Stateの動作はC++ Script側、TargetLockが完了しても発射可否はWeapon側で決める。

### 85.3 ミサイルのTargetLock構成

推奨Hierarchy例:

```text
PlayerShip
├ TargetSelector
├ TargetLock
├ WeaponLoadout
├ C++ Script: PlayerWeaponController
└ MissileSpawnPoint
```

設定手順:

1. `TargetSelector`で検索距離、角度、遮蔽、Team Filter、Selection Modeを設定する。
2. 同じObjectへ`ターゲットロック`を追加し、TargetSelector参照を未設定のままOwner fallbackにするか明示参照する。
3. Lock時間と喪失猶予を設定する。
4. 完了ActionをPlayerWeaponControllerの`OnLockCompleted`へ設定する。
5. C++側でPayload TypeがGameObjectか検査し、Targetを保持する。
6. 発射入力時に`TargetLock::GetState`を再確認してからWeaponLoadoutを発射する。
7. 発射ProjectileへTargetSteeringの明示Targetを渡す。

確認項目は、別Targetへ移った時に進行率が0へ戻ること、短時間の遮蔽では猶予内保持されること、猶予超過で解除Actionが1回だけ来ること、破棄済みTargetを撃たないことである。

### 85.4 敵射撃と再装填のTimer構成

```text
EnemyTurret
├ Timer
├ TargetSelector
├ ProjectileEmitter
└ C++ Script: TurretController
```

Timerを反復、自動開始、発火Action=`TryFire`にする。`TryFire`内ではTarget、射線、Weapon cooldownを確認し、撃てないFrameでもTimer自体へ敵固有の判断を埋め込まない。攻撃間隔を動的変更する場合はRuntime Propertyの`Timer.Duration`を変更する。

再装填のように開始時刻が外部イベントで決まる処理では自動開始をfalseにし、弾切れ時に`Timer::Start()`、中断時に`Pause()`、再開時に`Resume()`を使う。`Start()`をResumeとして使うと残り時間が初期化されるため区別する。

### 85.5 Boss Stateと部位破壊の構成

```text
BossShip
├ Health
├ GenericStateMachine
├ C++ Script: BossController
├ LeftTurret
│  ├ Health
│  ├ DestructiblePart
│  └ ProjectileEmitter
└ RightTurret
   ├ Health
   ├ DestructiblePart
   └ ProjectileEmitter
```

部位のDestructiblePartは自分のHealthを監視し、無効化Componentへ`ProjectileEmitter`、必要なら子砲身を無効化する。破壊ActionのGameObject PayloadをBossControllerが受け、残存部位数や演出をゲーム側で更新する。両砲塔破壊時に`GenericStateMachine::ChangeState("Retreat")`を呼ぶ。

DestructiblePartへ「両砲塔破壊なら撤退」を設定しない。これは複数部位を横断するゲーム条件であり、BossControllerまたは条件/Sequence側の責務である。修復可能な敵を作る場合はHealth回復だけでは`破壊済み`が戻らないため、Pool再生成または専用のゲーム側reset設計を行う。

### 85.6 編隊とRailMovementの構成

```text
FormationLeader
├ RailMovement または TargetSteering
├ EnemyA + FormationFollower Offset(-8, 0, 0)
├ EnemyB + FormationFollower Offset( 0, 0,-4)
└ EnemyC + FormationFollower Offset( 8, 0, 0)
```

FollowerのLocal OffsetはLeader回転を反映するため、曲がるRail上でも横一列やV字を維持する。FormationFollowerはTransform追従であり、各FollowerへDynamic Rigidbodyや別のRailMovementを同時に付けない。波面へ浮かせる船団で各船が独立したBuoyancyを必要とする場合、単純Transform追従ではなく、Leaderから目標XZ/Yawだけを得てPhysicsServoまたはゲームScriptのForce制御へ渡す構成を選ぶ。

WaveSpawnerは生成/有効化を担当し、FormationFollowerは生成後の位置関係だけを担当する。生成順、攻撃、撃破条件、Stage進行をFormationFollowerへ追加しない。

### 85.7 AttributeのHUD接続

Boost、Heat、Shield、FuelはAttributeへ分け、変更ActionのFloat PayloadをHUD Controllerへ送る。HUD側はPayloadを受けてCanvas Text/Gaugeへ反映し、毎FrameHierarchy検索しない。

確認手順:

1. AttributeのMin=0、Max=100、Current=100を設定する。
2. Heatなら毎秒回復を負数、Boost回復なら正数にする。
3. 変更ActionをHUD Controllerへ接続する。
4. C++でPayload TypeがFloatであることを確認する。
5. Gauge表示を`current / maximum`へ正規化する。Maximum 0を除外する。
6. Stop/Scene切替/Pool返却時にHUDが古いObject IDを保持しないことを確認する。

### 85.8 受入試験

| 試験 | 操作 | 合格条件 |
| --- | --- | --- |
| Timer 1回 | 0.2秒、Repeat off、自動開始 | Actionが1回だけ発生しPauseになる。 |
| Timer停止再開 | 途中Pause、待機、Resume | Pause中に残り時間が減らず、Resume後に続きから発火する。 |
| State Payload | `Initial`から`Attack` | 変更Actionが1回、String=`Attack`。同名再指定では発火しない。 |
| Attribute Clamp | Max 100へ150をSet | Current 100、Float Payload 100。 |
| 部位破壊 | 部位Healthを0 | 指定Component/子が無効、物理も停止、Action 1回。 |
| 編隊回転 | Leaderを90度回転 | Local OffsetがWorld上で回転し、Followerが追従する。 |
| Lock完了 | 同一TargetをLock時間維持 | Progress 1、Locked true、完了Action 1回。 |
| Lock猶予 | 一時遮蔽後、猶予内復帰 | Progressを保持して同Targetを継続する。 |
| Lock解除 | 猶予超過 | Locked false、Target空、解除Payloadに失ったTarget。 |
| Scene保存 | 保存、別Scene、再度開く | 編集設定と参照が復元され、Runtime値はPlay時再初期化される。 |
| Standalone | Release Buildして起動 | Scene、C++ DLL、依存DLLを含め同じAction連携が動く。 |

### 85.9 文書とコードの機械照合更新

この時点の基準値はComponent 239件、Runtime API Entry 169件である。Component EnumまたはRuntime API末尾へ追加した場合、次の3箇所を同時更新する。

1. `component-documentation-detail-seed.md`の個別Component契約。
2. `cpp-script-documentation-detail-seed.md`のSignature、Wrapper、失敗条件、コード例。
3. 本書のProject-to-Scene導線、受入試験、機械照合件数。

名称が文書に1回現れるだけでは網羅済みにしない。Inspector Label、初期値、保存、Runtime更新、Action Payload、C++入口、失敗条件、Standalone確認の各証拠を分けて記録する。

## 86. 複数Lock・Target HUD・汎用値の制作手順

### 86.1 複数ミサイルLock

1. プレイヤーへTargetSelectorを追加し、距離、角度、Team、遮蔽、最大候補数を設定する。
2. 同じObjectへMultiTargetLockを追加する。
3. 最大Lock数と1体のLock時間を設定する。敵をHierarchyの子Slotとして手動登録しない。
4. C++ Scriptで`MultiTargetLock::GetEntries()`を読み、`isLocked`のEntryだけを発射Queueへ渡す。
5. 発射直前に`target.HasReference()`と必要なTargetPoint/Healthを確認する。

### 86.2 敵マーカーと画面外警告

1. Canvas配下へImageを作成し、通常の画像、色、Sizeを設定する。
2. 画面内表示にはWorldTargetMarker、画面端表示にはOffScreenIndicatorを追加する。
3. 明示Target、TargetSelector、TargetLockのいずれか1つを接続する。
4. Game View Cameraを動かし、画面内/外の切替、後方非表示、端余白、方向回転を確認する。
5. 解像度とCanvasScalerを変更し、同じTargetへ追従することを確認する。

### 86.3 AttributeSet・Counter・Condition

1. AttributeSetへ`Boost`、`Heat`、`Shield`等をEntryとして追加する。
2. Counterへ撃破数等の初期値、範囲、閾値、比較方法を設定する。
3. ConditionのSource Typeを選び、比較元ObjectとProperty/属性名を指定する。
4. 小さい接続はTrue/False ActionでActionRelayへ渡し、複雑なゲームルールはC++ Scriptへ置く。
5. Stop後にCurrent、Lock進行率、Condition結果が次回Playへ漏れないことを確認する。

### 86.4 Gameplay Data Asset

1. メニュー`アセット > Gameplay Data 作成`を実行する。
2. `Assets/Data/NewGameplayData.gdata`を選択する。
3. GameplayData Componentの`選択中.gdataを設定`を押す。
4. `.gdata`へ`Entry|Key|Type|Value`形式で武器・敵・Upgrade値を記述する。
5. Play開始後、`GameplayData::GetFloat`等で保存Typeと一致するGetterを使う。

### 86.5 追加受入試験

| 試験 | 合格条件 |
| --- | --- |
| 8体Multi Lock | Targetごとに独立Progressを持ち、完了Actionが各1回。 |
| 一時遮蔽 | 喪失猶予内はEntryとProgressを保持し、超過後だけ解除。 |
| World Marker | Camera移動、FOV、解像度変更後も同じWorld位置へ追従。 |
| Off-Screen | 画面内で隠れ、画面外では端余白内に収まり方向へ回転。 |
| AttributeSet | 複数Nameが同一Objectで独立更新され、Min/MaxへClamp。 |
| Counter | 閾値を跨いだ時だけ一回発火設定が機能する。 |
| Condition | 比較元がない時にCrashせずfalseとなる。 |
| Gameplay Data | `.gdata`のString/Int/Float/Boolを型一致Getterで取得できる。 |
| Scene再読込 | 7 ComponentのInspector設定と可変Entryが復元される。 |

## 87. Damage・Projectile・Threat・Pool再利用の制作手順

### 87.1 ミサイルPrefabを作る

1. ProjectでミサイルPrefab用GameObjectをSceneへ置き、Collider、必要ならRigidbody、ProjectileDetonator、AreaDamageを追加する。
2. AreaDamageへ半径、基礎Damage、距離減衰、Layer Mask、`Explosion`等のDamage Tagを設定する。
3. ProjectileDetonatorの接触、近接、寿命条件を選び、AreaDamage参照を同じObjectへ設定する。
4. ObjectPoolのTemplateへこのObjectを設定し、ProjectileEmitterから同じPoolを発射する。
5. 直接命中Damageと爆発Damageを重ねたくない場合、ProjectileEmitterのDamageを0へする。
6. Playし、接触位置、近接半径、寿命終了位置、C++手動起爆の各1回だけでPoolへ戻ることを確認する。

### 87.2 艦橋・装甲・エンジンの部位Damage

1. 敵本体へHealth、DamageReceiver、必要ならDamageTagModifierを追加する。
2. 各部位の子GameObjectへColliderとHitZoneを追加する。
3. 共有HPなら全HitZoneのHealth対象を本体へ設定する。部位破壊なら部位ごとのHealthへ設定する。
4. 艦橋2.0、装甲0.5、エンジン1.5等の部位倍率を設定する。
5. DamageTagModifierへ`Bullet`、`Explosion`等のEntryを直接追加する。Tagごとの子GameObjectは作らない。
6. HitscanWeapon、ProjectileEmitter、AreaDamageのDamage Tagと完全一致することを確認する。

最終Damageの調査時は、基礎Damage、HitZone倍率、DamageReceiver倍率、DamageTag倍率を個別表示し、どこで倍率が重複したか追えるようにする。

### 87.3 ミサイル警告HUD

1. プレイヤーまたは警護対象へThreatTrackerを追加する。
2. 最大距離、最低接近速度、最大逸れ距離、最大脅威数を設定する。
3. CanvasへText/Imageを作り、C++ HUD Controllerで`ThreatTracker::GetEntries()`を読む。
4. 到達予測時間の短い順に`MISSILE xN`、方向、秒数を表示する。
5. 追加/解除ActionのGameObject Payloadを使い、警告音やMarker生成をEvent駆動にする。
6. 画面を外れた弾、離れていく弾、横を大きく逸れる弾が警告へ残らないことを確認する。

ThreatTrackerはProjectile一覧を生成するだけで、特定HUDレイアウトや自動回避をEngineへ埋め込まない。

### 87.4 Pool Itemの完全Reset

1. Pool TemplateのRootへRuntimeStateResetを追加する。
2. Health、State、Attribute/Counter、Lock、Timer、Destructible、Cooldownのうち再利用で戻す項目を選ぶ。
3. Template配下の破壊可能部位、無効化武器、子Objectも1回破壊してからPoolへ返す。
4. 再貸出し、HealthだけでなくState、Lock進行、Timer、部位表示、Cooldown、Threat一覧が初期化されたことを確認する。
5. Reset後にゲーム固有状態も必要ならReset ActionをC++ Scriptへ接続する。

RuntimeStateResetがないItemも全対応項目を互換Resetするが、意図をSceneへ明示し一部状態を維持したい場合はComponentを追加する。

### 87.5 名前付きCooldown

1. プレイヤーへCooldownSetを追加する。
2. `Boost`、`Special`、`Dodge`等をEntryとして直接追加し、時間と開始時使用可能を設定する。
3. C++ Scriptで使用前に`IsReady`、使用時に`Start`を呼ぶ。
4. HUDは`Get`の残り秒を表示するか、完了ActionのString Payloadで表示状態を更新する。
5. Pool再利用またはStage再開時はRuntimeStateResetのCooldown対象で初期化する。

### 87.6 保存・Standalone・回帰試験

| 試験 | 合格条件 |
| --- | --- |
| Area重複 | 1 Healthへ複数Colliderがあっても1回だけDamage。 |
| 距離減衰 | 中心、半径中間、端で設定した曲線と最低倍率になる。 |
| HitZone転送 | 子Collider命中が指定Healthへ入り、部位倍率が1回だけ乗る。 |
| Tag耐性 | BulletとExplosionを別倍率にし、未登録Tagは既定倍率になる。 |
| 起爆条件 | 接触、近接、寿命、手動が各1回で、起爆後の二重起爆なし。 |
| Threat予測 | 接近弾だけが到達予測時間順に最大件数まで並ぶ。 |
| Pool Reset | 破壊・Lock・Timer・Cooldownを変更しても再貸出時に初期状態。 |
| Cooldown可変数 | 同一Objectの複数Nameが独立し、子GameObject不要。 |
| Scene保存 | Tag配列、Cooldown配列、参照、Flagが再読込後も一致する。 |
| Debug / Release | 両構成でBuildし、StandaloneでもScene設定とC++ APIが一致する。 |

### 87.7 機械照合基準

この時点の基準値はComponent 239件、Runtime API Entry 169件である。新規7 Componentは内部型名、Add Component日本語名、Inspector全項目、初期値、Extension保存・読込、Runtime Manager、C++ Wrapper、受入試験が揃って初めて実装済みとする。

## 88. 武器Pattern・命中Surface・HitStopの制作手順

### 88.1 対空砲の3点Burst

1. Sceneの砲ObjectへHitscanWeaponまたはProjectileEmitterを追加する。
2. 同じObjectへWeaponFirePatternを追加し、Mode=`Burst`、Count=3、Interval=0.08を設定する。
3. WeaponAccuracyを追加し、Base Spread、Per Shot、Recovery、Maximumを設定する。
4. WeaponRecoilを追加し、Rigidbody Impulse、砲身Visual Target、CameraShakeの必要なものだけ接続する。
5. Playし、1回のInputで3実Shot、Shot間隔、Spread増加、停止後の復帰を確認する。

PatternはWeaponを自動生成しない。Damage、射程、Pool、発射入力は元Weaponに設定し、弾薬はWeaponLoadoutへ置く。

### 88.2 複数Lockミサイル斉射

1. プレイヤーへTargetSelectorとMultiTargetLockを追加し、候補条件と最大Lock数を設定する。
2. ProjectileEmitterへTargetAssignmentを追加し、MultiTargetLock参照、最大発射数、間隔、Lock完了だけを設定する。
3. Missile Pool TemplateへTargetSteeringとProjectileDetonatorを追加する。
4. `Weapon{player}.FireProjectile()`を1回呼ぶ。ScriptでTarget配列をLoopして弾を個別生成しない。
5. 各生成弾のTargetSteering Targetと近接起爆Targetが別々のLock対象になったことを確認する。
6. Pool不足時もCrashせず、生成できた弾だけ飛び、完了Actionが列末尾で1回になることを確認する。

TargetAssignmentはLock一覧を消費・解除しない。発射後のLock解除や同一Target再Lock規則はプレイヤー用C++ Scriptで決める。

### 88.3 水面・金属・岩の着弾演出

1. 海面RootへSurfaceType=`Water`、船体Rootへ`Metal`、岩へ`Rock`を追加する。
2. WeaponへImpactResponderを追加する。
3. `Explosion + Water`へ水柱Effectと着水音、`Bullet + Metal`へ火花Effectと金属音を設定する。
4. 最後へDamage Tag空、Surface Tag空のFallback Entryを置く。
5. AudioSourceは3D音響、距離減衰、Busを事前設定し、CameraShakeは別Componentで振幅と時間を設定する。
6. 子ColliderへSurfaceTypeがなくても親RootのTagが使われることを確認する。

ImpactResponderのEntry順がPriorityである。Wildcardを先頭へ置くと後続の具体条件へ到達しない。Effect/Audio失敗とDamage処理は独立している。

### 88.4 大型砲HitStopとSlow Motion

1. 演出制御ObjectへTimeScaleを追加する。
2. HitStopならScale=0、Duration=0.05-0.12、Blend=0を基準にする。
3. Slow MotionならScale=0.2-0.5、DurationとBlendを設定する。
4. Damage/Impact ActionをC++ Scriptへ接続し、`TimeScale{controller}.HitStop(duration)`を呼ぶ。
5. 停止中も入力Device状態とTimeScaleの残り時間が更新され、指定時間後に1倍へ戻ることを確認する。
6. Pause Menuの永続停止と同じTimeScale Componentを共有しない。

### 88.5 C++ Script連携

```cpp
void FireLockedMissiles(const GameObject& weaponObject) {
	Weapon weapon{weaponObject};

	if (!weapon.FireProjectile()) {
		return;
	}

	const float spreadDegrees = weapon.GetAccuracySpread();
	(void)spreadDegrees;
}

void OnHeavyImpact(const GameObject& timeController) {
	TimeScale{timeController}.HitStop(0.08f);
}
```

Componentの有無で挙動を合成するため、通常弾、Burst、Spread、複数Lock斉射ごとの専用Script APIは作らない。ゲームScriptは「いつ撃つか」「発射後にLockをどう扱うか」「HitStopをどの攻撃へ使うか」を担当する。

### 88.6 保存・Runtime・回帰試験

| 試験 | 合格条件 |
| --- | --- |
| Single互換 | 新Componentなしで従来どおり1要求1発。 |
| Burst | CountとIntervalどおり。Cooldown中の重複要求なし。 |
| Salvo/Spread | 同Frame複数発、Spread端点が設定角度内。 |
| Sequence | Spawn Point配列を順番に使い、Count超過で循環。 |
| Charge | 指定非負秒後に1発。Scale 0中はゲーム時間として停止。 |
| Target Assignment | Lock完了Targetごとに別Projectile Target。Hierarchy Slot不要。 |
| Accuracy | Shotごとに増加し、時間で回復、Pool Resetで0。 |
| Recoil | Body、Visual、Cameraを個別に無効化でき、Stop後にVisual残差なし。 |
| Surface継承 | 子Colliderから親SurfaceTypeを解決し、Default fallbackが動く。 |
| Impact順序 | 最初の一致EntryだけがEffect/Audio/Actionを実行。 |
| HitStop復帰 | Scale 0でも非スケールDuration後に1へ戻る。 |
| Scene保存 | Pattern Spawn Point配列、Impact Entry、参照、文字列Tagが再読込後一致。 |
| C++ API | `GetAccuracySpread`、`Play/HitStop/GetCurrent`がDebug/Releaseで一致。 |

### 88.7 機械照合基準

この章の追加開始時点の基準値はComponent 246件、Runtime API Entry 172件である。前段で追加した7内部型名、7日本語Add Component名、7専用Extension、WeaponManager実行経路、非スケールTimeScale更新、3 Runtime API Entry、高水準Wrapper、3資料の説明と受入試験を同時に照合する。この章の完了時点ではComponent 268件、Runtime API Entry 211件だった。

Runtime APIの追加9 Entryは既存順序を変更せず構造体末尾へ置く。文書件数は`EditorScene.cpp`の`kEditorComponentTypeNames`と`EditorScriptApi.h`の`EditorScriptRuntimeApi`を再計数して更新する。

## 89. 照準補助・Mission・Encounterを使う制作手順

### 89.1 プレイヤー照準へ補助を追加する

1. プレイヤー制御Objectへ`ScreenAim`と`TargetSelector`を追加し、Game Viewの照準入力と敵候補条件を先に確認する。
2. 同じObjectへ`照準補助`を追加する。参照が同じObjectなら画面照準とTarget Selectorは`このObject`のままでよい。
3. 補助半径0.08-0.15、補助強度0.2-0.4、追従速度6-12を開始値にする。
4. 入力中の抑制を0.5以上にし、StickをTargetと逆へ倒した時にプレイヤー入力が勝つことを確認する。
5. TargetPointを敵の中心、弱点、砲塔へ置き、Collider中心ではなく狙わせたいWorld位置へ補助されることを確認する。

照準補助はTarget選択、Lock進行、射撃、Damageを行わない。`TargetSelector -> AimAssist -> TargetLock/MultiTargetLock -> Weapon`を別責務として組み合わせる。

### 89.2 非誘導弾のLead Markerを出す

1. 砲または照準制御Objectへ`迎撃予測`を追加する。
2. 明示Targetを固定するか、既存TargetSelectorを参照する。
3. ProjectileEmitterと同じProjectile速度を設定し、最大予測秒を実際の射程/弾速より少し長くする。
4. C++ Scriptで`Targeting::GetInterceptPrediction`を取得し、World Target Markerの追従位置へ渡す。
5. Target Rigidbodyの速度変更、Target停止、弾速不足でMarkerが表示/非表示になることを確認する。

迎撃予測は重力弾道を含まない。海上砲の落下を使う場合は、返された位置を初期値としてゲームScript側の弾道Solverへ渡す。

### 89.3 Mission Objectiveを作る

1. Sceneへ`Mission` GameObjectを作り、`目標トラッカー`を追加する。
2. `DestroyRadar`、`ProtectTransport`等のID、表示名、目標値をEntryへ直接追加する。Objectiveごとの子GameObjectは作らない。
3. Stage Controller Scriptの開始時に対象Objectiveを`Active`へ変更する。
4. 敵撃破や護衛DamageのActionから現在値を更新する。
5. 変更ActionのString PayloadをHUD Controllerへ渡し、該当IDの表示だけ更新する。
6. Completed/FailedをScene遷移、報酬、次Encounterへ接続する処理はゲームScript側へ置く。

### 89.4 複数Waveを1 Encounterとして並べる

1. 各敵Waveを`WaveSpawner`として作成し、開始条件を`外部開始`へ変更する。
2. Sceneへ`Encounter` GameObjectを作り、`エンカウンター制御`を追加する。
3. WaveSpawner参照を順番に追加し、各EntryへDelayと`全撃破を待つ`を設定する。
4. 自動戦闘ならPlay開始時実行を有効にし、Rail地点やBoss演出から始めるなら無効のままC++の`EncounterController::Start`を呼ぶ。
5. 全生成待ちなら前Waveの敵が残った状態で増援が始まり、全撃破待ちなら最後の敵がPoolへ返ってから次Waveが始まることを確認する。
6. 完了ActionをObjective、Rail再開、Camera Blend等へ接続する。

EncounterはWaveSpawnerを自動作成せず、敵の移動・攻撃も変更しない。WaveSpawnerはPrefab/Pool/Formation、EncounterはWaveの順序だけを担当する。

### 89.5 Spawn Point Setで配置を量産する

1. WaveSpawnerと同じGameObjectへ`生成地点セット`を追加する。
2. Scene上へ空GameObjectを配置し、左前、右前、遠方などのWorld Transformを作る。
3. 各地点をComponent内部のEntryへ参照する。Hierarchy上の子である必要はない。
4. 順番、ランダム、重み付きのいずれかを選ぶ。Weight 0は選択されず、全0なら等確率へFallbackする。
5. 同じ地点が連続すると不自然な場合は`直前を避ける`を有効にする。
6. 海域内の自由配置にはVolume Modeを使い、Volume SizeのYを0にすれば同じ海面基準高さへ生成できる。

### 89.6 Difficulty Presetを適用する

1. Project/Sceneの設定Objectへ`難易度パラメーターセット`を追加する。
2. Easy、Normal、Hard等の難易度名を登録する。
3. Runtime Property資料から正式なComponent名とProperty名を確認し、敵HP、Damage、発射間隔、Lock時間等をOverrideへ追加する。
4. タイトル/Stage Selectから渡したIndexをC++の`DifficultyParameterSet::Apply`へ渡す。
5. 適用ActionのString PayloadでHUD表示、Save値、Analytics用の難易度名を更新する。
6. 不正PropertyがConsole警告またはfalseになっても、他の有効Overrideが適用されることを確認する。

### 89.7 被弾方向HUDとCamera Feedbackを作る

1. Playerへ`被弾方向表示`を追加し、表示秒、Fade、最低Damageを設定する。
2. Canvasへ方向Imageを作り、HUD Scriptで`DamageDirectionIndicator::Get`を読む。
3. 画面中心へDirection×画面端半径を加えてImageを配置し、Alphaへ取得値を設定する。
4. Cameraを旋回し、同じWorld SourceからのDamageが画面基準の正しい方向に出ることを確認する。
5. SceneのCamera演出設定Objectへ`カメラフィードバックミキサー`を1つ置く。
6. 武器、爆発、着水のCamera ShakeへPriorityを設定し、加算または最高Priority Modeを選ぶ。
7. 最大同時数と軸別最大振幅を下げ、複数爆発でもCameraが破綻しないことを確認する。

### 89.8 回帰試験

| 試験 | 合格条件 |
| --- | --- |
| Scene保存 | Objective/Wave/Spawn/Difficultyの可変配列と全参照が再読込後一致する。 |
| AimAssist | 入力中に補助が弱まり、Target範囲外で位置を変えない。 |
| Intercept | 静止Target、横移動Target、解なしの3ケースで結果が安定する。 |
| Encounter | 外部開始Waveが設定順で進み、再Startで先頭へ戻る。 |
| SpawnPoint | 直前回避、Weight 0、無効参照、VolumeをCrashなしで処理する。 |
| Difficulty | Float/Int/Boolを型一致で適用し、Index範囲外でfalse。 |
| Damage HUD | Camera yaw変更後も方向が画面と一致し、時間後Alpha 0になる。 |
| Camera Mixer | 最大同時数、Priority、Clamp、Global強度が独立して効く。 |
| C++ API | 7高水準呼出がDebug/Releaseで同じ成功・失敗を返す。 |

この章の機械照合基準は、8内部Component型、8日本語Add Component名、8専用Extension、Camera Shake Priority互換列、7 Runtime API Entry、高水準Wrapper、Debug/Releaseビルドである。この章の完了時点の総数はComponent 268件、Runtime API Entry 211件だった。

## 90. Scene自動保存と復旧

### 90.1 自動保存を設定する

1. メニュー`ファイル > 自動保存`で有効・無効を切り替える。初期状態は有効である。
2. `ファイル > 自動保存間隔`から30秒、1分、2分、5分、10分のいずれかを選ぶ。初期値は2分である。
3. 設定変更は`ProjectSettings/EditorSettings.cg2`へUTF-8 BOM付きで保存され、次回起動時に同じ設定を復元する。
4. `ファイル > 今すぐ自動保存`を選ぶと、設定間隔を待たずに現在の編集Sceneを保存する。
5. File Menu下部の`次回まで`と`状態`で、残り時間、変更なし、保存先、失敗状態を確認する。

自動保存の時間はEditorの描画時間で進む。Play中はScene開始前の編集内容をRuntime状態で上書きしないため、自動保存を停止する。Stop後は残り時間から再開する。

### 90.2 保存済みSceneの動作

保存先が決まっているSceneでは、直接ファイルを途中まで書き換えない。次の順で保存する。

1. 同じフォルダーへ`<Scene名>.scene.autosave.tmp`を完全出力する。
2. 現在のSceneファイルと一時ファイルをバイト比較する。
3. 内容が同じ場合は一時ファイルを削除し、元Sceneの更新日時を変更しない。
4. 内容が違う場合は、更新前Sceneを`Library/AutoSave/<Scene名>_<PathHash>.previous.scene`へ退避する。
5. 一時ファイルをWindowsの置換APIで元Sceneへ原子的に移動する。

PathHashを付けるため、異なるフォルダーに同名Sceneがあっても復旧ファイルは衝突しない。正常に置換できた場合だけConsoleへ自動保存結果を出す。

### 90.3 未保存Sceneの動作

新規Sceneをまだ`名前を付けて保存`していない場合は、元Sceneの保存先を勝手に決めず、`Library/AutoSave/UnsavedScene.scene`へ復旧用コピーを保存する。

正式なScene Assetにする場合は、復旧ファイルを直接制作先として使い続けず、Editorで読み込んでから`ファイル > 名前を付けて保存`を使い、`Assets/Scenes`配下へ保存する。

### 90.4 保存失敗から復旧する

1. File Menuの状態表示またはConsoleで`自動保存に失敗`を確認する。
2. 状態に`.autosave.tmp`のPathが表示された場合、そのファイルは置換失敗時の復旧候補として残っている。
3. CG2を終了する前に、元Scene、`.previous.scene`、`.autosave.tmp`の更新日時と内容を確認する。
4. 元Sceneが壊れている場合は、`Library/AutoSave`の`.previous.scene`を`Assets/Scenes`へ別名コピーしてEditorから読み込む。
5. 最新内容が一時ファイルにだけある場合は、`.autosave.tmp`を別名の`.scene`として`Assets/Scenes`へ移して読み込む。

`Library/AutoSave`は復旧用生成物でありGit管理対象ではない。正式なSceneの履歴管理は引き続きGitを使う。

### 90.5 手動保存・Scene切替との関係

- `保存`または`名前を付けて保存`に成功すると、自動保存タイマーを0へ戻す。
- Projectで別Sceneを開いた場合もタイマーを0へ戻し、開いた直後の不要な書き込みを防ぐ。
- 自動保存はUndo履歴を消去せず、選択ObjectやCamera操作を変更しない。
- 自動保存はSceneだけを対象とする。Shader、Texture、Model、C++ Script等の外部ファイルは各編集ツール側で保存する。
- 自動保存間隔ごとにSceneを一度シリアライズするが、変更がなければ元ファイルを書き換えない。

### 90.6 受入試験

| 試験 | 合格条件 |
| --- | --- |
| 保存済みScene変更 | 指定時間後に元Sceneへ反映され、更新前版が`Library/AutoSave`へ残る。 |
| 変更なし | 指定時間後も元Sceneの内容と更新日時が変わらず、Consoleへ保存成功を連続表示しない。 |
| 未保存Scene | `Library/AutoSave/UnsavedScene.scene`が作成され、現在Scene Pathは空のまま。 |
| 今すぐ保存 | Menu実行フレームで保存され、次回までの時間が設定値へ戻る。 |
| Play中 | RuntimeでTransformやComponent値が変化してもSceneファイルへ書き込まれない。 |
| Stop後 | 編集Sceneへ戻った後に自動保存タイマーが再開する。 |
| 手動保存後 | 自動保存タイマーが0になり、直後に重複保存しない。 |
| Scene切替 | 新しく開いたSceneでタイマーが0から開始し、前Sceneへ書き込まない。 |
| 同名Scene | 別フォルダーの同名Sceneが異なるPathHashのBackupへ退避される。 |
| 置換失敗 | 元Sceneを失わず、復旧用`.autosave.tmp`のPathが状態とConsoleへ出る。 |
| 設定再起動 | 有効状態と保存間隔が`EditorSettings.cg2`から復元される。 |
| 文字コード | 設定、Scene、復旧SceneがUTF-8 BOM付きで保存され、日本語名が文字化けしない。 |

## 91. 弾道・被弾履歴・Pause・水面航跡・軌道表示

### 91.1 艦砲の弾道予測を設定する

1. 砲口GameObjectへ`照準 > 弾道予測`を追加する。
2. `Target`を直接指定するか、`Target Selector`所有GameObjectを指定する。直接Targetを指定した場合はこちらを優先する。
3. `初速`へ実際のProjectile初速と同じ値を入れる。
4. `重力`は通常`(0, -9.81, 0)`、空気抵抗を使わない場合は`抗力=0`とする。
5. Targetが一定加速度で移動すると仮定する場合だけ`Target加速度`を設定する。RigidBody速度はRuntimeから自動取得する。
6. `最大予測秒`は射程外判定、`計算刻み`は探索と軌道点間隔、`最大点数`はCPU負荷と表示密度を決める。

Runtimeは飛行時間を走査して、重力と線形抗力を含む初速度の大きさが設定初速と一致する最初の解を二分探索する。解がある場合は`有効`、`発射方向`、`着弾位置`、`飛行秒`、`軌道点列`を同じ計算から更新する。Targetなし、初速0、最大秒内に解なしの場合は無効となり、古い点列を残さない。

### 91.2 軌道をScene/Gameへ表示する

1. 砲口または照準用GameObjectへ`描画・レンダリング > 軌道プレビュー`を追加する。
2. 同じGameObjectの弾道予測を使う場合は`予測元=-1`、別GameObjectならその参照を設定する。
3. 色、Alpha、太さ、最大表示点数を設定する。
4. `Scene Viewへ表示`は編集確認、`Game Viewへ表示`はプレイヤー向け照準、`着弾点を表示`は終端円の表示を制御する。

軌道プレビューは計算を行わず、`BallisticPrediction`の点列だけを描く。これにより表示を無効にしても射撃計算は変わらず、表示点数を減らしても着弾解は変わらない。Game ViewではGame Camera、Scene ViewではScene Cameraを使って投影する。

### 91.3 複数方向の被弾HUDを作る

1. Healthを持つプレイヤーへ`UI > 複数被弾履歴`を追加する。
2. `最大件数`で同時表示数、`表示秒`で寿命、`最小Damage`で小Damageの除外を設定する。
3. `同じ攻撃元を統合`を有効にすると、同じSourceから連続したDamageを1件へ加算して寿命を延長する。
4. C++ Scriptで`DamageEventBuffer::GetEntries()`を読み、各EntryをCanvas Imageへ割り当てる。

EntryはSource GameObject、TargetからSourceへ向くWorld方向、実適用Damage、Damage Tag ID、残り秒を持つ。ComponentはHUDを直接生成せず、画面座標変換、色、Icon、左右舷表示、同時Indicator配置はゲームUI側が決める。Object Pool再利用時は履歴をClearする。

### 91.4 永続Pauseを設定する

1. Scene進行管理GameObjectへ`ゲームプレイ > ゲーム一時停止`を追加する。
2. `ゲーム時間を停止`、`Physicsを停止`、`Audioを停止`をゲーム仕様に合わせて個別設定する。
3. `Gameplay Map`と`UI Map`をPlayerInputのActionMap名と完全一致させる。Pause中はUI Mapだけを入力対象にする。
4. Pause MenuのOpen Actionから`GamePause::Pause()`、Resume Buttonから`GamePause::Resume()`を呼ぶ。
5. `Pause Action`と`Resume Action`を使う場合は、表示切替先ScriptへBool Payloadが届く。

`TimeScale`はHitStop/Slow Motion、`GamePause`はユーザーが解除するまで続く停止であり責務を分ける。Pause中もInput ManagerとScript Updateは動き、UI ActionからResumeできる。ゲーム時間停止時は通常Updateへ0秒を渡し、Physics停止時は固定Stepを実行しない。AudioはVoiceを破棄せずStop/Startするため、再生位置を維持する。

### 91.5 船の水面航跡を設定する

1. 船Rootへ`海・水面 > 水面航跡エミッター`を追加する。
2. 船の左後方、右後方、船首へ子GameObjectを作り、それぞれParticle SystemまたはVisual Effectを追加する。
3. 3点を`左航跡`、`右航跡`、`船首飛沫`へ割り当てる。
4. Oceanを限定する場合は`Ocean`へ参照を設定し、未設定なら各点を覆うOceanを自動検索する。
5. `開始速度`、`最大速度`、`幅`、`寿命`、`最大発生数`を設定する。

Runtimeは船RootのWorld移動量から速度と0～1強度を求める。各Effect点のYだけを描画・浮力と共通のOcean Sampleへ合わせ、Particle Rate、Size、Lifetimeを更新する。停止中は毎Frame Play/Stopせず、発生状態が切り替わった時だけEffect Managerへ通知する。FFT変位へ局所波を注入するComponentではなく、泡・飛沫・Trailを軽量に合成するComponentである。

### 91.6 Score・Combo・Stage Resultテンプレート

`C++ Scriptを作成`から次を選べる。

| Template | 生成される接続 | ゲーム側で変更する箇所 |
| --- | --- | --- |
| スコア制御 | `OnClick`のFloat/Int PayloadをGenericCounterへ加算する。 | 敵種類、部位、難易度、Combo倍率による加算式。 |
| コンボ制御 | 命中ActionでCounterを加算しTimerを再開始、Timer Actionで0へ戻す。 | Combo猶予、倍率段階、UI演出。TimerのAction名は`OnValueChanged`へ接続する。 |
| ステージ結果 | CounterからScoreを読みS/A/B/Cを決め、`StageScore`と`StageRank`をScene間データへ保存する。 | Rank境界、命中率等の追加評価、Result Scene遷移。 |

これらはエンジンの固定ゲームルールではない。生成されたC++ Scriptをプロジェクト側で編集し、Component/APIは値保持と通知だけを担当する。

### 91.7 受入試験

| 試験 | 合格条件 |
| --- | --- |
| 静止Target弾道 | 抗力0で軌道終端がTargetへ近づき、発射方向が正規化される。 |
| 移動Target弾道 | RigidBody速度と設定加速度を反映し、飛行時間後のTarget位置を先読みする。 |
| 解なし | 初速不足または最大秒超過でValid=false、軌道点0となる。 |
| 表示分離 | Scene/Game表示Flagと最大表示点数を変えても弾道結果が変化しない。 |
| 複数被弾 | 異なるSourceを複数保持し、同一Source統合、最大件数、寿命削除が独立して働く。 |
| Pause | Gameplay入力、Physics、Audio、ゲーム時間が設定どおり止まり、UI MapからResumeできる。 |
| 航跡 | 開始速度以下で停止し、速度増加でRateが増え、3点のYが共通Ocean Sampleへ追従する。 |
| Scene保存 | 5 Componentの全設定値と参照が再読込後に一致し、Runtime値はPlay開始時に初期化される。 |
| C++ API | 7追加Entryと4高水準WrapperがDebug/Releaseで同じ成功・失敗を返す。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件、C++ Script Template 24件だった。

## 92. FFT海面をゲーム判定へ接続する

### 92.1 目的と責務境界

Oceanを描画背景としてだけ使わず、照準遮蔽、弾の着水、水面通過、前方波面予測へ参加させる。判定はOcean描画MeshのColliderではなく、描画・浮力と共通の`SampleEditorOceanSurface`を使う。GPU FFTのReadback値が利用可能ならそれを優先し、未取得時は同じOcean設定から有限水深CPU SampleへFallbackする。最初のGPU結果へ切り替わる時だけ0.12秒のSmoothStepで位置・法線・表面速度を移行し、浮力や判定の瞬間的な跳びを抑える。移行完了後はGPU Sampleを無加工で返すため、定常時の船体応答へ追加遅延を入れない。

Engineは交点、法線、速度、距離、Clearance等の幾何情報だけを返す。`大波ならLock禁止`、`水中弾へ変更`等のゲームルールはC++ ScriptまたはAction接続で決める。

### 92.2 Ocean Segment Castの判定方法

線分を指定数へ粗分割し、各点とFFT水面の符号付き距離を調べる。水面法線方向の距離が`Clearance`以下へ変化した最初の区間を二分探索し、最初の交点を返す。RaycastはDirectionを正規化し、`Start + Direction * MaximumDistance`を終点として同じ処理を使う。

| 出力 | 内容 |
| --- | --- |
| Ocean GameObject | 命中したOceanのID。Ocean参照を省略した場合は位置を覆う有効Oceanを検索する。 |
| Position | FFT水面上の交点。入力線分上の点ではなくSampleした水面位置。 |
| Normal | 描画・浮力と共通の波面法線。 |
| Surface Velocity | 交点での水面速度。飛沫、相対速度、魚雷移行判定に使える。 |
| Distance | Startから交点までのWorld距離。 |
| Normalized Distance | 線分全長に対する0～1の位置。 |

粗分割数は2～64、二分探索回数は0～12へ制限する。長いRayで小さい高周波波を厳密に拾う場合は分割数を増やすが、1 Frameに大量発行せず、固定ProbeやTarget候補数を制限する。

### 92.3 TargetSelectorで波を遮蔽物にする

TargetSelectorの`遮蔽方式`を次から選ぶ。

| 値 | 動作 |
| --- | --- |
| なし | PhysicsとOceanの遮蔽を調べない。 |
| Physics | 従来どおりCollider Raycastだけを使う。 |
| Ocean | Targetまでの線分とFFT波面だけを調べる。 |
| Physics + Ocean | 両方を調べ、どちらか一方が遮れば候補から除外する。 |

`Ocean Clearance`を0より大きくすると、水面を直接横切らなくても水面へ指定距離以内まで近づいた視線を遮蔽扱いにできる。TargetLockはSelectorの結果を受けるため、波で候補が消えた時は既存のLock喪失猶予へ移行する。`RuntimeProperty::SetInt(..., "TargetSelector", "OcclusionMode", 0..3)`と`SetFloat(..., "OceanClearance", value)`でPlay中にも変更できる。

### 92.4 HitscanとProjectileの着水

Hitscan WeaponとProjectile Emitterには`FFT水面へ命中`を追加する。有効時はPhysics HitとOcean Hitを同じ発射区間で求め、Startから近い方だけを採用する。

Oceanが先ならHealthへDamageを送らず、Surface Tagを`Water`としてImpactResponder、接触起爆、命中Actionへ渡す。これにより水柱、着水音、デカール以外の水面Effect、弾消滅を既存のWeapon接続で構成できる。Oceanの下にあるColliderが先なら通常どおりPhysics Hitを採用する。OceanにMesh Colliderを重ねる必要はない。

### 92.5 水面出入り状態Component

1. 判定対象へ`海・水面 > 水面出入り状態`を追加する。
2. Oceanを限定する場合は参照を設定し、省略時は判定点を覆うOceanを自動検索する。
3. `ローカル判定位置`を船底、弾頭、残骸中心等へ合わせる。
4. `出入り余白`で水面付近の細かい反転を抑える。
5. `進入Action`と`離脱Action`をC++ Scriptへ接続する。

状態は`AboveWater`、`EnteringWater`、`Underwater`、`LeavingWater`である。Entering/Leavingは境界を越えたFrameだけの遷移状態で、次FrameにはAbove/Underwaterへ安定する。Play開始時は現在位置から初期状態を決めるが、開始直後に進入Actionを誤発火しない。Action PayloadのGameObject IDは命中Oceanである。

### 92.6 海面前方プローブComponent

1. 船、Camera、AI等へ`海・水面 > 海面前方プローブ`を追加する。
2. `ローカル原点`と`ローカル方向`を設定する。方向はOwnerのWorld回転を反映する。
3. Probe Entryを追加し、距離を10m、25m、50m等に設定する。
4. C++ Scriptから各IndexのPosition、Normal、Velocity、Relative Heightを読む。

各EntryはHierarchyの子GameObjectを要求せず、一つのComponent内で可変配列として管理する。`Relative Height`はSampleした水面YとProbe基準点Yとの差であり、波の意味付けは行わない。ProbeがOcean範囲外、無効Ocean、Index範囲外の場合はValid=falseまたはAPIがfalseを返す。

### 92.7 受入試験

| 試験 | 合格条件 |
| --- | --- |
| Segment Cast | 波面を横切る線分で最初の交点、法線、Ocean ID、距離が返る。水面上だけの線分では命中しない。 |
| Raycast | 非正規化Directionでも同じ方向へMaximum Distanceまで判定し、ゼロDirectionは失敗する。 |
| Ocean選択 | Ocean指定時はそのOceanだけを使い、未指定時は位置を覆う有効Oceanを使う。 |
| Target遮蔽 | Physics/Ocean/Bothの各Modeが独立し、Ocean遮蔽解除後は既存Lock猶予に従って再取得する。 |
| Hitscan | PhysicsとOceanの近い方だけがImpactResponderへ届き、OceanへHealth Damageを送らない。 |
| Projectile | 1 Frameの移動区間でFFT水面を越えた弾がすり抜けず、Water Impact後に解放される。 |
| 水面状態 | Above→Entering→Underwater、Underwater→Leaving→Aboveの順で遷移し、開始時にActionを誤発火しない。 |
| Probe | Owner回転、Local Origin、可変距離を反映し、3点以上でも子GameObjectを要求しない。 |
| 保存 | Ocean参照、Mode、Clearance、Collision Flag、状態設定、Probe配列がScene再読込後に一致する。 |
| C++ API | 5追加Entryと3高水準Wrapper群がDebug/Releaseで同じ成功・失敗を返す。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件、C++ Script Template 24件だった。

## 93. 自艦誤爆を防ぎ、砲塔と複数艦砲を構成する

### 93.1 責務の分け方

この機能群は水上レールシューティング固有の敵AIやBoss攻撃をEngineへ固定しない。Engine側は攻撃Cast除外、砲塔回転、複数Weaponの発射順、弾体継続、Camera姿勢補正という再利用可能な処理だけを持つ。

| 必要な処理 | 担当 | 担当しないもの |
| --- | --- | --- |
| 誰へ攻撃Castを当てないか | 攻撃コリジョンフィルター | 敵選択、Damage式、Friendly Fireのゲームルール表示。 |
| Targetへ砲身を向ける | 砲塔照準 | 自動発射、攻撃Pattern、弾薬。 |
| 複数Weaponをまとめて要求する | 武器グループ | 個別WeaponのDamage、Cooldown、Target割当。 |
| 貫通・跳弾後も弾を進める | 弾体貫通・跳弾 | 実材質の物理厚、破壊形状、装甲HP。 |
| 船体の揺れをCameraへ部分継承する | 水平線スタビライザー | Camera Shake波形、演出Cut、入力。 |
| 補給・Upgradeで弾薬値を変える | WeaponLoadout C++ API | Pickup条件、価格、Save Data規則。 |

### 93.2 発射直後の自艦命中を防ぐ

1. Projectで艦艇PrefabまたはSceneを開き、HierarchyからWeapon Rootを選ぶ。
2. Inspectorの`コンポーネントを追加 > ゲームプレイ > 攻撃コリジョンフィルター`を追加する。
3. `Instigator`へ艦艇Rootを指定する。省略時はFilterまたはWeapon所有Objectになるため、複数階層の艦艇では明示指定を推奨する。
4. `Instigatorを無視`と`Instigator階層を無視`を有効にする。これでRoot、砲塔、砲身、砲口のColliderを同じCast除外へ入れる。
5. 敵だけへ当てる通常武器は`Team Rule=Different Team`にする。地形や港湾施設にも当てるため、Team ComponentがないObjectは除外しない。
6. ミサイルや大型砲弾は`Arming Distance`を砲口から自艦外殻を抜ける距離へ設定する。
7. 特定の護衛Objectや発射レールを除外する場合だけ`Ignore Objects`へ追加する。

Hitscanでは発射Ray全体、ProjectileではArming後の前Frame位置から現在位置までの連続CastへFilterを適用する。Projectileが高速でも離散位置だけで判定せず、通過区間を検査する。

### 93.3 Yaw台座とPitch砲身を作る

推奨Hierarchyは次である。

```text
Ship Root
└─ Main Battery
   └─ Yaw Pivot
      └─ Pitch Pivot
         └─ Muzzle
```

1. `Main Battery`へ`コンポーネントを追加 > 照準 > 砲塔照準`を追加する。
2. `Yaw Pivot`と`Pitch Pivot`へ上の子Objectを割り当てる。
3. 明示Targetを常時指定しない場合は、`Main Battery`または別ObjectへTarget Selectorを置き、`Target Selector`参照を設定する。
4. Yaw/Pitchの最小・最大角を実モデルの可動域に合わせる。
5. 旋回速度を設定し、Play中にInspectorの`到達可能`、`照準完了`、Yaw/Pitch誤差を見る。
6. 移動Targetを単純先読みする場合は`Target予測秒`を設定する。重力弾の正確な先読みは弾道予測の発射方向をゲームScriptで使用する。

Yaw PivotとPitch Pivotを同じObjectにすると両軸を一つのTransformへ適用できるが、砲塔モデルでは別Objectを推奨する。可動域外でも砲塔は端まで追従し、Runtimeの`Can Reach Target`がfalseになるため、Scriptは発射を抑止できる。

### 93.4 主砲A/B/Cを一つの砲撃単位にする

1. 各砲口または各砲塔へHitscan WeaponかProjectile Emitterを設定する。
2. 共通の`Main Battery`へ`コンポーネントを追加 > ゲームプレイ > 武器グループ`を追加する。
3. Weapons Entryを追加し、各Weapon所有GameObjectを登録する。Entry有効Flagで破壊済み砲塔等を一時除外できる。
4. 一斉射は`Simultaneous`、左から順に撃つ場合は`Sequential`、1門ずつ循環する場合は`RoundRobin`を選ぶ。
5. Sequentialでは`Interval`を設定する。
6. 全砲が発射可能になるまで待つ場合は`Require All Ready`を有効にする。無効時は発射できる砲だけが処理される。
7. 完了後に別処理へ進む場合は`Completion Action`をC++ ScriptまたはAction Relayへ接続する。

Weapon GroupはWeapon Objectを自動生成しない。各Weaponの発射間隔、Projectile Prefab、Damage、Accuracy、Recoil、Impactは元Componentへ設定する。一つのGroupに登録するためだけにHierarchyへ砲数分の管理Componentを増やさない。

### 93.5 補給・UpgradeをC++ Scriptから行う

`WeaponLoadout` Wrapperで任意Slotを操作する。IndexはWeaponLoadout直下のWeaponLoadoutSlotをHierarchy順に収集した番号で、`-1`は選択中Slotである。

```cpp
WeaponLoadout loadout{playerObject};

// 弾薬箱: Slot 0の予備弾を20追加する。
loadout.AddReserveAmmo(0, 20);

// Upgrade: Slot 0のMagazine上限を12へ変更する。
loadout.SetMaximumAmmo(0, 12);

// Stage開始時だけMagazineを上限まで満たす。
loadout.RefillMagazine(0);
```

通常Reloadは`Reload()`を使う。`RefillMagazine()`はReserveを消費せず即時にMaximumへするため、補給地点、Stage初期化、Debug用途として区別する。Reserve=-1は無限弾であり、AddReserveAmmoでは-1を維持する。

### 93.6 貫通・跳弾を設定する

1. Projectile PrefabまたはProjectile Emitter所有Objectへ`コンポーネントを追加 > ゲームプレイ > 弾体貫通・跳弾`を追加する。
2. `有効`をOnにし、初期Energy、貫通損失、最大貫通数を設定する。
3. 跳弾を使う場合は跳弾角、最大跳弾数、速度保持率を設定する。
4. Metal、Wood等で差を付ける場合はSurface Modifierを追加し、Surface TypeのTagと同じ文字列を設定する。
5. Play中に一発ずつ撃ち、Damage、Impact Effect、速度低下、方向変化、最終消滅または起爆を確認する。

この実装はSurfaceごとの固定Energy損失であり、Collider内部の実距離を装甲厚として積分しない。精密な戦車Simulationではなく、艦砲、ミサイル、機関砲へ一貫したゲーム向け貫通挙動を与える用途で使う。

### 93.7 波で揺れる船のCameraを安定させる

1. Game Cameraへ`コンポーネントを追加 > カメラ > 水平線スタビライザー`を追加する。
2. `Follow Source`へBuoyancyで揺れる船体Rootを指定する。
3. `Local Position Offset`で船体基準のCamera位置を決める。
4. 最初はPitch=0.35、Yaw=1.0、Roll=0.2、Maximum Roll=8度から調整する。
5. 波を強く感じさせたい場合はPitch/Roll継承を増やし、画面酔いを抑える場合は減らす。
6. Positionを別Componentで管理する場合は`Position Follow`を無効にし、回転安定化だけを使う。
7. Camera Shakeとの合成を確認する。Stabilizerは追従姿勢を作り、Shakeはその後の演出Offsetとして扱う。

### 93.8 受入試験

| 試験 | 合格条件 |
| --- | --- |
| 自艦Collider階層 | Root/子/孫Colliderを持つ艦から発射しても自艦Damageや直後消滅が起きない。 |
| 味方・地形 | Different Teamで味方を通過し、Teamなし地形には命中する。 |
| Arming Distance | 指定距離までは命中せず、越えたFrameの残り区間から連続判定する。 |
| 砲塔可動域 | 範囲内TargetでIs Aimed、範囲外TargetでCan Reach=falseとなる。 |
| 砲列 | Simultaneous/Sequential/RoundRobin、全Ready条件、Entry無効化が独立して働く。 |
| 弾薬 | Slot選択、上下限、無限Reserve、補給、上限変更、即時補充が正しい。 |
| 貫通・跳弾 | 最大回数とSurface補正を超えて継続せず、最終Hitで通常の消滅・起爆へ戻る。 |
| Camera | 船体Yawを追い、Pitch/Rollだけを指定率へ減らし、最大Rollを超えない。 |
| Scene再読込 | 5 Componentの全参照、配列、文字列、数値、Flagが復元される。 |
| C++ ABI | 10追加Runtime APIと3高水準Wrapper群がDebug/Releaseで同じ結果を返す。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件、C++ Script Template 24件だった。

## 94. 移動母体からの射撃、爆発遮蔽、発射前安全検査、時間制Effect

### 94.1 責務を分離する

| 処理 | Engine側の担当 | ゲーム側へ残すもの |
| --- | --- | --- |
| 移動中の発射速度 | Projectile Emitterと弾道予測が同じRigidbody速度を使う。 | どの武器が母体速度を継承するか、継承率の調整。 |
| 爆発遮蔽 | Area DamageがPhysics/Ocean遮蔽とTeam条件を評価する。 | 爆発の発生条件、Damage値、Friendly Fire方針。 |
| 発射前安全確認 | 発射前射線チェックが砲口前方の遮蔽物を検出する。 | Block中の待機、別Target選択、警告UI。 |
| 時間制状態 | 状態効果セットがID、時間、Stack、Tick、Actionを管理する。 | `Fire`、`Flood`、`EMP`の意味、Damage式、Visual、Audio。 |
| 弾道照準接続 | Projectile Emitterが弾道予測の解をAim Sourceとして使う。 | いつ発射するか、解なし時のFallback。 |

`AttackCollisionFilter`は発射後に命中させない対象を決め、`FireLineCheck`は発射前に安全かを決める。自艦Hierarchyを攻撃Castから除外しても、砲口前方の艦橋や別砲塔を安全検査から自動除外しない。

### 94.2 移動する船から弾を発射する

1. Projectで対象Sceneをダブルクリックし、Hierarchyから砲またはProjectile Emitter所有Objectを選ぶ。
2. InspectorのProjectile Emitterで`発射元速度を継承`を有効にする。
3. `速度Source`へ船体Rigidbodyを持つObjectを指定する。砲塔が船体の子なら、未指定のまま`親Rigidbodyを検索`を有効にできる。
4. `並進速度継承=1.0`で船体のWorld並進速度を全て加える。
5. 砲口が船体中心から離れている場合は`角速度継承=1.0`にする。作用点速度として`angularVelocity cross (muzzlePosition - rigidBodyOrigin)`が加わる。
6. 重力弾では弾道予測にも同じ速度Source、親検索、並進/角速度継承率を設定する。
7. Projectile Emitterの`照準Source=弾道予測`、`弾道予測`参照を設定する。

実弾の初期World速度と予測の初期World速度は次で統一する。

```text
sourceVelocity = rigidBodyLinearVelocity * linearScale
               + cross(rigidBodyAngularVelocity, muzzlePosition - rigidBodyOrigin) * angularScale

projectileWorldVelocity = launchDirection * muzzleSpeed + sourceVelocity
```

弾道予測ModeではProjectile Emitterの通常速度ではなく、参照したBallisticPredictionの初速、重力、線形Drag、発射方向、発射元速度を使用する。BallisticPredictionが`Valid=false`なら発射しない。

### 94.3 爆発の遮蔽とTeamを設定する

1. 爆発ObjectへArea Damageを追加し、半径、基礎Damage、距離減衰、Damage対象Layerを設定する。
2. `遮蔽判定=Physics`なら壁、艦橋、岩礁等のColliderだけを使う。FFT波面も遮蔽物にする場合は`Physics + Ocean`を選ぶ。
3. `遮蔽Layer Mask`はDamage対象Layerとは別に設定する。Damageを受けない地形でも爆風を遮れる。
4. `遮蔽Sample数`を1～9で設定する。複数SampleはTargetの上下へ分散し、一部だけ露出した対象の遮蔽率を求める。
5. `遮蔽時倍率=0`なら完全遮断、0より大きければ遮蔽越しDamage/Impulseを残す。
6. `Team Source`へ攻撃者を指定する。未指定ではInstigatorを使う。
7. `Teamルール`を`すべて`、`異なるTeamのみ`、`同じTeamのみ`から選び、必要ならNeutralを無視する。

遮蔽倍率はDamageとImpulseの両方へ同じ比率で掛かる。Sample数を増やすほどRay/Ocean queryが増えるため、通常爆発は1～3、大型爆発で輪郭精度が必要な場合だけ増やす。

### 94.4 砲口前方の安全を確認する

1. Projectile EmitterまたはHitscan Weaponと同じObject、またはその親へ`コンポーネントを追加 > ゲームプレイ > 発射前射線チェック`を追加する。
2. `砲口`へMuzzle Object、`前方向Source`へ砲身の向きを持つObjectを設定する。
3. `検査距離`を砲口から自艦外殻を抜ける距離、`検査半径`を砲弾半径または砲身の安全余白へ設定する。半径0はRay、0より大きい値はSphere Castとして扱う。
4. `Block Layer Mask`へ自艦構造物、地形、障害物Layerを含める。
5. 現在狙っているObjectを射線終端として許可する場合は`許可Target`へ設定する。
6. 砲口自身や明確に安全な補助Colliderだけを`無視Object`へ追加する。自艦Hierarchy全体は追加しない。
7. Play中に`Runtime`、`Blocking Object`、`Blocking距離`を確認する。

Weapon ManagerはHitscan/Projectileの発射要求時にこの結果を確認し、Blockedなら弾薬消費、Projectile貸出、発射Actionを行わない。安全検査だけを行うため、命中後のDamage Filter、Arming Distance、Team条件は`AttackCollisionFilter`へ残す。

### 94.5 状態効果を作り、C++ Scriptへ意味を接続する

1. 状態を受けるGameObjectへ`コンポーネントを追加 > ゲームプレイ > 状態効果セット`を追加する。
2. `Action対象`へ処理するC++ Script所有Objectを設定する。
3. Effect定義を追加し、固定Enumではなく文字列`Effect ID`を設定する。
4. `Duration`、`Stack Mode`、`最大Stack`、`Tick間隔`を設定する。
5. 開始/Tick/終了ActionをScriptの公開Actionへ接続する。
6. 攻撃、Trigger、Script等から`StatusEffectSet::Apply("Fire", source)`を呼ぶ。
7. Action ContextのString PayloadからEffect IDを読み、Damage、速度低下、Component無効化、Effect再生等をゲーム側で行う。

| Stack Mode | 同じIDを再適用した時 |
| --- | --- |
| Refresh | Stack数を変えず残り時間とTick待ちを初期値へ戻す。 |
| Stack | 最大Stackまで増やし、残り時間を初期値へ戻す。 |
| Ignore | 既に存在する間は新しい適用要求を無視する。 |

Engineは`Fire`を毎秒Damageへ変換しない。開始/Tick/終了通知とRuntime Entryを提供し、効果の意味はAction受信Scriptが決める。Object Poolの全状態ResetではRuntime EntryをClearする。

### 94.6 受入試験

| 試験 | 合格条件 |
| --- | --- |
| 並進継承 | 静止/移動母体から同方向へ撃ったWorld速度差が母体速度と一致する。 |
| 角速度継承 | 回転中心から離れた砲口だけ`omega cross r`分の接線速度を得る。 |
| 予測一致 | 同一速度Source、Gravity、Dragで予測軌道とProjectile軌道が一致する。 |
| Area Team | 直撃Filterと爆発Team Ruleが矛盾せず、Neutral設定も独立する。 |
| Area遮蔽 | Physics/Ocean Mode、Layer、Sample数、遮蔽倍率がDamageとImpulseへ反映される。 |
| 発射前検査 | 艦橋が前方にある時は発射せず、旋回後Clearになると発射できる。 |
| 状態効果 | Refresh/Stack/Ignore、開始/Tick/終了回数、Source、残り時間、Pool Resetが正しい。 |
| 保存 | 新規2 Componentと既存3 Componentの全設定がScene再読込後に一致する。 |
| C++ ABI | 7追加Entryと2高水準WrapperがDebug/Releaseで同じ結果を返す。 |

この章の追加時点の機械照合基準はComponent 268件、Runtime API Entry 211件、C++ Script Template 24件だった。

## 95. Particle / VisualEffectのBillboard設定

### 95.1 Projectから設定する

1. Project内の`.scene`をダブルクリックしてSceneを開く。
2. HierarchyでParticleSystemまたはVisualEffectを持つGameObjectを選択する。
3. Inspectorの`描画モデル`を開く。
4. FBX / OBJを粒として描く場合は`Render Asset`へAssetを設定する。板Particleを使う場合は空にする。
5. `板の向き`から用途に合う方式を選ぶ。
6. `速度方向`を選んだ場合だけ`速度方向の長さ`を調整する。
7. Scene ViewとGame ViewのCamera位置、回転、Rollを別々に変え、両方で正しい向きになることを確認する。
8. Sceneを保存して開き直し、選択値が維持されることを確認する。

| 板の向き | 用途 | 向きの決め方 | 注意点 |
| --- | --- | --- | --- |
| カメラ正対 | 爆発、煙、光、円形の飛沫 | 描画対象View CameraのRight / Up。 | 既定値。Texture正面を常にCameraへ向ける。 |
| Y軸固定 | 縦煙、炎、地上Marker | World YをUpとし、Camera方向へ水平回転する。 | 上下から見た完全正対より、垂直維持を優先する。 |
| 速度方向 | 曳光、雨、船首飛沫、細長い粒 | VelocityをCamera Planeへ射影してUpとする。 | 速度が小さい時はCamera Facingへ戻る。長さ倍率を使える。 |
| World XY固定 | 旧Scene互換、固定Plane表現 | World X / Yへ固定する。 | Cameraが回る一般的な3D Effectには通常使わない。 |

### 95.2 Effect AssetとPlay中の変更

`.effect`には`billboardMode`と`billboardStretch`を保存できる。Effect AssetをComponentへ適用すると、他のEmissionやLifetime設定と一緒にBillboard設定も反映される。`billboardMode`は0～3、`billboardStretch`は0.01以上である。

C++ ScriptからPlay中に切り替える場合はRuntime Propertyを使う。

```cpp
GameObject effect = GameObject::Find("BowSplash");

RuntimeProperty::SetInt(
	effect,
	"VisualEffect",
	"BillboardMode",
	2);
RuntimeProperty::SetFloat(
	effect,
	"VisualEffect",
	"BillboardStretch",
	3.5f);
```

Component内部名は`ParticleSystem`または`VisualEffect`、Property名は`BillboardMode`と`BillboardStretch`である。文字列の大文字小文字と値型が違う場合は変更に失敗する。FBX / OBJ Render Assetが設定されている時も値は保存できるが、Model Particle描画には適用されない。

### 95.3 受入試験

| 試験 | 合格条件 |
| --- | --- |
| Camera Facing | CameraのYaw / Pitch / Rollを変えてもQuad正面が対象Viewへ向く。 |
| View分離 | Scene ViewとGame Viewが異なるCameraでも、それぞれのViewへ正対する。 |
| Y軸固定 | Cameraを上下へ動かしてもParticleのWorld Upが横倒れしない。 |
| Velocity Facing | 速度方向が変わると板方向も変わり、Stretch 1と3で長さの差が出る。 |
| 停止粒子 | Velocityがほぼ0でもNaNや消失を起こさずCamera Facingへ戻る。 |
| Model粒子 | FBX / OBJ粒子の3D姿勢がBillboard設定で変化しない。 |
| 保存 | Sceneと`.effect`のMode / Stretchが再読込後に一致する。 |
| Runtime Property | int Modeとfloat Stretchの正しい型だけ成功し、不正Component名・Property名・型はfalseになる。 |

この変更は既存Componentの描画設定追加であり、この章の追加時点ではComponent 268件、Runtime API Entry 211件、C++ Script Template 24件のままだった。

## 96. Camera追従と船体向けRailMovement

### 96.1 Playerの向きへCameraを追従させる

1. Project内の`.scene`をダブルクリックしてSceneを開く。
2. HierarchyでGame Cameraを選択する。
3. CameraまたはCinemachine Cameraの`追従対象`へPlayerや船を設定する。
4. `プレイヤー追従プリセット`を押す。Camera Transformは位置`(0, 2, -6)`、回転`(0, 0, 0)`になり、位置Offset基準は`対象Local`、回転方式は`対象回転を継承`になる。
5. Camera GameObjectの位置を、追従対象から見た右・上・前後Offsetとして調整する。
6. Playし、PlayerがYaw回転した時にCamera位置と向きが一緒に回ることを確認する。

| 位置オフセット基準 | 動作 | 主な用途 |
| --- | --- | --- |
| World固定 | Camera位置をWorld軸のOffsetとしてTarget位置へ加える。Targetが回ってもCameraの配置方向は変わらない。 | 固定方向から追う演出、旧Scene互換。 |
| 対象Local | Camera位置をTargetのWorld回転で回してからTarget位置へ加える。 | Player後方Camera、船尾Camera、追従視点。 |

| 回転方式 | 動作 | Camera Transform回転の意味 |
| --- | --- | --- |
| Camera角度を固定 | Target位置だけを追い、CameraのWorld角度を維持する。 | 旧Scene互換、固定俯瞰。 |
| 対象回転を継承 | TargetのWorld回転へCamera回転を加える。 | 後方追従、機体視点。 |
| 対象を見る | Camera位置からTarget位置へLook Atし、Camera回転を追加Offsetにする。 | 注視Camera、周回Camera。 |

追従対象を未設定、削除、非Activeにした場合はCamera自身のWorld Transformを使う。位置Offsetが完全な0の場合は`(0, 2, -6)`へFallbackする。船体Pitch/Rollを一部だけ継承したい場合は、通常追従ではなく`水平線スタビライザー`を使う。

### 96.2 RailMovementの移動方式を選ぶ

1. Hierarchyで船を選択し、`ゲームプレイ > レール移動`を追加する。
2. `Splineを作成して接続`を押し、Scene ViewまたはSpline Editorで制御点を編集する。
3. 船へDynamic Rigidbody、Collider、Buoyancyを追加する。RigidbodyのKinematicはOFFにする。
4. RailMovementの`船体推進`クイック設定を押す。
5. Modelの船首に一致する`船首ローカル軸`を`+Z / -Z / +X / -X`から選ぶ。
6. 水上船では`推力を水平にする`をONにし、波によるPitch/Rollで推力が海底や空へ向き過ぎないようにする。
7. `横ずれ補助率`を調整する。0は推力とYaw操舵だけ、1はRail中央へ戻す横方向サーボを全適用する。船らしさを残す開始値は0.1～0.3である。
8. `浮力併用プリセット`で位置追従軸を`(1,0,1)`、回転追従軸を`(0,1,0)`にし、Y位置とPitch/RollをBuoyancyへ任せる。

| 移動方式 | 力の向き | 動き | 用途 |
| --- | --- | --- | --- |
| Transform追従 | Forceを使わずTransformを直接更新する。 | Pathへ正確に一致する。 | Camera経路、演出、物理不要Object。 |
| Dynamic Rigidbody 物理サーボ | Spline先読み接線へ前進Forceを加え、目標位置へPD補正する。 | Rail拘束が強く、経路へ戻りやすい。 | 車両、移動床、強いオンレール制御。 |
| Dynamic Rigidbody 船体推進 | 現在の船首ローカル軸へ推力を加え、Spline接線へYaw Torqueで旋回する。 | 船首が向くまで横滑りや旋回遅れが残る。 | Buoyancyを使う船、水上乗物。 |

左右・上下OffsetとMovementModifierは、Transform方式だけでなく両方のDynamic Rigidbody方式の目標位置にも反映される。船体推進で横ずれ補助率を0にしても、Spline方向へ向くYaw操舵は継続する。`進行方向へ回転`をOFFにするとYaw操舵も行わない。

### 96.3 受入試験

| 試験 | 合格条件 |
| --- | --- |
| Camera Local位置 | Playerを90度Yaw回転すると、後方Offsetも同じ角度だけ回る。 |
| Camera回転継承 | PlayerのYawがCamera向きへ反映され、Camera回転Offsetも維持される。 |
| Look At | Camera位置を移動してもTarget中心を向き、零距離でNaNにならない。 |
| 旧Scene互換 | ExtensionのないSceneはWorld固定位置・Camera角度固定で開く。 |
| 物理サーボ | 船首がPathとずれていてもSpline接線方向へ直接追従Forceが出る。 |
| 船体推進 | 船首軸を変えると推力方向も変わり、Spline接線へ直接横押しされない。 |
| 浮力併用 | RailがY/Pitch/Rollを上書きせず、Buoyancyの上下動と傾きが残る。 |
| 保存 | Camera方式、Rail方式、船首軸、水平推力、横ずれ補助率が再読込後も一致する。 |

この変更は既存Camera / CinemachineCamera / RailMovementの拡張であり、この章の追加時点ではComponent 268件、Runtime API Entry 211件、C++ Script Template 24件のままだった。

## 97. Ocean Buoyancyを実Physics Shapeへ接続する

### 97.1 旧AABBセル方式の問題

旧経路はBuoyancyの船体サイズまたはCollider AABBを直方体セルへ分割し、Rigidbody Massを全セルへ均等配分していた。この方式ではCollider外の空間、船体の細い部分、上部構造を含むAABBまで排水体積として扱う。完全水没時の浮力も実Shape体積ではなく`Mass x 浮力設定`で決まり、部分浸水時の浮心はセル中央のWorld Yだけを変更した近似だった。

旧復元Torqueと自動Collider用の喫水制限は、このAABB近似が起こす転覆と上部構造への誤作用を抑える補助だった。物体をWorld Upへ戻すTorqueは形状と質量分布を無視するため廃止し、喫水制限だけを特殊Shapeの計算範囲保護として残す。復元は実Shapeの静水圧作用点とRigidbody重心の位置差から発生させる。

### 97.2 Runtime計算順

1. Buoyancy対象のWorld Transform、船体軸、実Colliderの中心と寸法を固定更新内で取得する。自動物理では手入力の船体サイズと浮力中心を使わない。
2. 船幅・船長を覆う固定5x5の25点で`SampleEditorOceanSurface`を呼び、描画と同じFFT時刻、変位、法線、表面速度を取得する。水力面数に関係なくFFT Sample数は25点で固定する。
3. 25点の高さへ最小二乗Planeを当て、FFT Sample法線を15%混合して、体積と水線面積を求める安定水面Planeを作る。同じ25点は双線形補間し、各水力面頂点と中心の局所水位、法線、表面速度にも使う。欠損Sampleを含む区画は最小二乗Planeへ戻す。
4. `EditorJoltPhysicsManager::GetSubmergedVolume`がPlay中の実Body ShapeをPlaneで切り、総体積、水没体積、水没部分の重心、Body重心、Shape境界寸法を返す。
5. 自動物理は`effectiveDensity = buoyancyWaterDensity`とし、Joltへ反映済みの実質量と`density x displacedVolume x |gravity|`の釣り合いで喫水を決める。手動互換方式だけ`effectiveDensity = mass x buoyancyStrength / (|gravity| x totalVolume)`へ変換する。静水圧合力の大きさは表面三角形へ置き換えず、常にJoltの排水体積から求める。これにより開いたCollider Assetや反転法線が合計浮力を壊さない。
6. 水面PlaneをShape高さの2%だけ上げてもう一度切り、`(raisedSubmergedVolume - submergedVolume) / deltaHeight`から水線面積を求める。`effectiveDensity x |gravity| x waterplaneArea`を上下剛性とし、自動物理は減衰比0.7、手動方式は`上下減衰 / 10`を使う。自動物理では水線二次Moment、排水体積、浮心と重心の高さからRoll/Pitchメタセンタ高さを求め、減衰比0.35の放射減衰Torqueを追加する。
7. Auto ConvexとDynamic MeshColliderは、判定用Compoundの区間境界面を水力面へ混ぜず、Collider Assetの元三角形を最終Jolt重心基準へ変換して保存する。Primitiveとその他ShapeはBody ShapeをLeafまで展開する。512面を越える場合は総面積を保持する面積分位Samplingへ縮約する。
8. 保存した各面を現在のBody TransformでWorldへ移し、各頂点で5x5補間水面との差を求めてSutherland-Hodgman Clipする。完全に水上の面は除外し、水面を横切る面は水没部分だけの面積、外向き法線、面積重心を求める。各面の`局所水深 x 面積 x 上向き投影率`を積分し、合計浮力の作用点だけをJolt浮心から最大Shape半径35%まで移す。作用点Offsetは0.08秒時定数で平滑化し、積分できないShapeはJolt浮心へ戻す。
9. 各面中心の速度を`linearVelocity + angularDragScale x (angularVelocity x (panelCenter - centerOfMass))`で求め、その面位置で補間したFFT表面速度を引く。外向き法線へ入る速度成分から圧力抵抗、接線速度からReynolds数依存の表面摩擦を計算し、各面の力と`(panelCenter - centerOfMass) x panelForce`を合計する。面三角形は運動抵抗だけに使い、総静水圧はJolt排水体積が担当する。
10. 自動物理では前回の浮心水相対速度との差を0.08秒時定数で平滑化する。正面、横、上下の投影面積を最大投影面積で正規化した係数と`effectiveDensity x submergedVolume`を掛け、各軸の並進付加質量Forceを浮心へ加える。前回角速度との差も0.1秒時定数で平滑化し、各回転軸の投影面積比、排水流体質量、形状二次Momentから回転付加慣性Torqueを求める。初回接水では履歴だけを作り、並進は6G、回転は`mass x shapeRadius x gravity x 4`を上限にする。
11. 自動物理では船首軸方向の相対速度、Gravity、Shape船長からFroude数を求める。`submergedVolume^(2/3)`の基準面積、幅/長さ比、Fn 0.38付近の抵抗Hump、高速遷移を造波抵抗へ変換する。前後速度を1固定更新で反転させず、2Gを越えない。
12. Objectごとの前回排水体積を保持し、排水体積の増加率と相対入水速度からSlammingを計算する。初回Sample、静止中、完全水没後は偽の着水衝撃を発生させない。
13. 通常経路と特殊Shapeフォールバックの両方で、人工的なWorld Up復元Torqueを加えない。Jolt浮心または局所静水圧作用点と、Collider形状および`重心オフセット`から決まるJolt重心の距離だけで復元Momentを発生させる。放射減衰Torqueと回転付加慣性Torqueは角度を0へ強制しない。

### 97.3 Shapeとフォールバック

| Collider / Jolt Shape | Runtime浮力 |
| --- | --- |
| Box、Sphere、Capsule | Joltの実Shape水没体積、浮心、表面パネルを使う。Boxは船首と船尾の形が同じなので、船型差の検証には使わない。 |
| Auto Convex | Model三角形を最長軸の最大1～16非重複区間へClipし、区間ごとのConvexHullをCompound化する。排水体積と浮心は複数Hull、圧力抗力と表面摩擦は元Collider Meshを使うため、合計浮力を保ちながら鋭い船首、船尾、平たい横腹の運動抵抗差を反映する。 |
| Dynamic MeshCollider | 物理Body生成時にDynamic用ConvexHullへ変換されるため、そのHullを使う。三角形MeshCollider設定と精密接触BVHは削除しない。 |
| Compound / Decorated Convex | Joltが子Shapeを再帰積算して体積と浮心を返す。水力面はLeaf Shapeを収集し、子の位置、回転、Scaleを反映して合成する。 |
| 体積計算非対応の特殊Shape | 旧分布グリッドへフォールバックする。表面を取得できない場合は実Shape境界寸法の投影抗力へフォールバックする。AABB経路を通常船体の主計算には使わない。 |

自動物理では実Colliderの中心と寸法が5x5 FFTプローブの広がりとフォールバック範囲を決める。手動方式だけ`船体サイズ`と`浮力中心`を使う。排水形状を変える場合はCollider Asset、Auto Convexの最大凸包数を調整する。重量物が下部へ集中する物体はRigidbodyの`重心オフセット`を設定し、形状とは独立した実際の質量分布を入力する。

### 97.4 力の方向と設定の意味

静水圧は波面法線方向ではなくGravityの反対方向へ加える。合計浮力はJolt排水体積から変えず、局所FFT水深は作用点だけを船首、船尾、左右へ移す。これにより船体より短い波でも船首だけが持ち上がるMomentを作り、単一Planeの浮力総量は維持する。`波の横押し`は互換名を維持するが、実Shape経路では着水衝撃方向をWorld上方向からFFT法線へ寄せる比率として使い、船を波の斜面方向へ常時横押ししない。

自動物理の`目標水没率`は、`bodyDensity = waterDensity x targetSubmersionRatio`としてRigidbody自動質量へ接続する。0.55は平水面でCollider排水体積の約55%を沈める基準であり、実際の姿勢と喫水はShape、重心、積荷、波面によって変わる。手動方式の`浮力`は完全水没時の基準加速度で、概算平衡水没率は`|Gravity| / 浮力`である。

`上下減衰`は実Shape経路では単純な速度倍率ではない。値を10で割ったものを水線面積から求める臨界減衰比として使う。5は0.5倍、10は臨界減衰、20は臨界減衰の2倍である。

前後・横・上下の水抵抗は、各水没面の外向き法線がBoat Forward / Right / Upへどれだけ向くかで混合する圧力抗力係数である。相対流へ正面を向く面だけが強い圧力を受けるため、鋭い船首で前進する場合、平たい横腹で同じ方向へ進む場合、船尾を前へした後進、斜航で結果が変わる。`前後の水抵抗`はITTC表面摩擦の粗さ倍率にも使う。

`回転抵抗`は、各面中心へ生じる回転速度成分の倍率である。面ごとの圧力と摩擦は実際の作用点からTorqueへ変換されるため、通常Shape経路で船体寸法だけを使う一様なAngular Dragは重ねない。

自動物理の付加質量は物体が周囲の水も加速させる効果である。並進と回転の両方で船首方向、横方向、上下方向へ別Componentや船種Presetを置かず、実Shapeの軸別投影面積比から係数を決める。回転放射減衰は水面へ波を放射して失うエネルギーの低速近似であり、二次抗力がほぼ0になる小さいPitch / Roll角速度でも振動を抑える。造波抵抗は速度二乗だけでは表せない船長依存の抵抗増加をFroude数で補う。

### 97.5 Lifecycleと性能

- 水没体積、相対速度、角速度、平滑化加速度、局所浮力作用点の履歴はSceneへ保存せずPlay Runtimeだけに保持する。
- Play開始、Play停止、Simulation無効化、Pool再配置を伴うTransform同期で履歴を消去する。
- 1 BodyあたりのFFT Sampleは固定25点であり、水力面が最大512面まで増えてもSample数は変わらない。旧方式の最大256列Sampleより負荷を予測しやすく、各面の局所水面は25点から双線形補間する。
- Jolt Shapeの体積積分は固定更新ごとに現在水面で1回、水没中は水線面積算出用の上昇水面で追加1回行う。CPU負荷はBody数に比例するため、大量の小破片へBuoyancyを付ける場合はComponentを無効化するかObject Pool側で接水対象を制限する。
- Shape三角形の列挙とLeaf Shape展開はBodyごとの初回だけ行い、Root COM空間でCacheする。以降は最大512面のWorld変換、水面Clip、圧力・摩擦積算だけを固定更新で行う。Play停止時にCacheを破棄し、Body IDの世代番号をCache Keyへ含めて再生成Bodyとの取り違えを防ぐ。
- 512面を越えるShapeは、元三角形の並びに対する面積分位Samplingを使う。各代表面へ受け持つ面積倍率を保持するため総表面積は失わないが、極端に細かい局所凹凸までをCFDのように再現するものではない。
- 物理固定更新はPre Fixed Step Scriptを実行した後、SceneのComponent配列をGameObjectごとに1回だけ走査する。Rigidbody、Buoyancy、空力、拘束、外力、各FieldのPointerをその固定更新内だけCacheし、17系統の物理処理が同じSceneとComponentを個別に再検索しない。
- WindZone、GravityField、RotatingFrame、FluidVolume、VortexField、PressureField、ElectromagneticFieldは共通Cacheから有効な発生源だけを参照する。固定更新ごとのローカル`vector`生成を行わず、前回確保したCapacityを再利用する。
- 水力面は法線方向と接線方向の相対速度が両方ほぼ0の場合だけ、結果が0になる圧力係数、Reynolds数、ITTC摩擦係数の計算を省く。排水体積、浮心、水線面積、上下減衰、着水判定、最大512面の形状精度は変更しない。
- 浮力計算は固定物理更新だけで実行し、描画処理へ物理更新を混在させない。

### 97.6 受入試験

| 試験 | 合格条件 |
| --- | --- |
| 平水面 | 設定から予想した喫水付近で静止し、継続的な上下発散がない。 |
| 片側浸水 | 水没側へ浮心が移動し、重心との差から自然なTorqueが出る。 |
| 波長が船長より短い波 | 船首・船尾のSample差が局所Planeへ反映され、中央1点だけの上下移動にならない。 |
| 局所浮力作用点 | 合計浮力が`density x displacedVolume x gravity`から変わらず、船首だけが波へ乗る時は作用点移動によるPitch Momentが出る。 |
| 急な回転 | Pitch / Roll / Yaw開始時に投影面積依存の回転付加慣性が出るが、初回接水や一定角速度で偽のTorqueを継続しない。 |
| 速度域 | 同じ船体でもFroude数0.38付近で造波抵抗が増え、1固定更新で前後速度を反転させない。 |
| 船首先行と横腹先行 | 同じAuto Convex船体と同じ速度で、鋭い船首を前へした方が横腹を前へした場合より圧力抵抗が小さい。 |
| 前進と後進 | 船首と船尾の形が非対称なら、180度向きを変えた同速度移動で抵抗が変わる。 |
| 横滑り | 前後抵抗より横抵抗を大きくした場合、前進を過度に失わず横速度が減衰する。舷側の水没面積が増えるほど抵抗も増える。 |
| 斜航 | 船首を進行方向から外すと左右面の圧力作用点差からYaw Momentが生じる。重心へ一括した抵抗だけにならない。 |
| 部分浸水面 | 水面を横切る三角形は全面積ではなく、水面下へClipされた面積だけが圧力と摩擦を受ける。 |
| 静止 | FFT表面速度と船体面速度が等しい場合は圧力抵抗と表面摩擦が発生しない。 |
| 着水 | 空中からの入水時だけSlammingが出て、完全水没後に毎固定更新繰り返さない。 |
| Auto Convex | AABBの角や上部構造ではなく、生成Hullの水没体積で喫水と浮心が変わる。 |
| Pool再利用 | 再配置前の排水体積が残らず、初回固定更新で偽の着水衝撃を出さない。 |
| Rail併用 | RailがY / Pitch / Rollを直接上書きせず、Buoyancyによる上下動と傾きが残る。 |

この変更は既存BuoyancyとJolt内部Queryの強化であり、Component数、公開Runtime API Entry数、C++ Script Template数は変わらない。

## 98. レールシューティングSceneの制作手順

この節は専用ゲーム画面を追加する仕様ではない。通常の`Project -> Scene -> Hierarchy -> Inspector -> Play`で、汎用Componentを組み合わせる。

### 98.1 プレイヤー用レール

1. ProjectのScenesフォルダーから対象Sceneをダブルクリックして開く。
2. Hierarchyでプレイヤー艇を選び、`レール移動`を追加する。
3. `Splineを作成して接続`を押し、Scene ViewまたはSpline Editorで制御点を追加する。
4. 長い直線だけにせず、旋回、上り下り、海面の見せ場を制御点で作る。
5. 船体へDynamic RigidbodyとBuoyancyがある場合は`船体推進`と`浮力併用プリセット`を選ぶ。RailはXZ/Yaw、BuoyancyはY/Pitch/Rollを担当する。
6. プレイヤー艇へ`レール速度プロファイル`を追加し、0～1の進行率へ巡航、加速、旋回減速、終端減速のキーを置く。
7. `レール区間`を追加し、高速区間、照準制限区間、Boss接近区間へZone IDを付ける。

RailSpeedProfileは連続速度、RailZoneは名前付き区間と一時上書きを担当する。同じ用途を重複設定せず、地形カーブに沿った恒常的な速度はProfile、ゲーム進行で意味を持つ区間はZoneへ置く。

### 98.2 Camera

1. Main Cameraへ`カメラ追従コンポーザー`を追加する。
2. 追従対象へプレイヤー艇を指定する。
3. 追従Offsetを船体後方上、注視Offsetを船体前方へ設定する。
4. 対象Yaw継承を有効にし、船が曲がっても後方位置が船基準で回ることを確認する。
5. 水面ゲームではPitch/Roll安定化を有効にし、波の傾きをCameraへ100%渡さない。
6. プレイヤー艇へ`速度フィードバック`を追加し、最低/最高速度とFOV範囲を実ゲーム速度に合わせる。

Camera Blend中はBlendを優先する。Blend完了後は追従へ戻る。Camera Shake、Horizon Stabilizer、Feedback MixerはComposerを置き換えず最終演出として合成する。

### 98.3 敵Wave

1. 敵Prefab TemplateへModel、Collider、Health、DamageReceiver、Team、RailMovementまたは汎用移動Componentを付ける。
2. Object PoolへTemplateとPool容量を設定する。
3. Wave Rootへ`ウェーブ生成`を置き、Pool、生成数、間隔、Formationを設定する。
4. 同じWave Rootへ`生成オブジェクト設定`を追加し、Rail Path、開始進行率、個体間の進行率差、速度倍率、Teamを設定する。
5. 編隊全体に動きが必要なら`ウェーブ移動プロファイル`を追加する。
6. Wave Spawned Actionで敵ScriptへTargetや難易度Dataを渡す。攻撃内容はWaveSpawnerへ入れない。

敵数を増やすときにHierarchyへ敵を1体ずつ子として増やさない。Pool方式ならSpawn Countだけを変更し、生成個体の共通設定はSpawnedObjectSetup、位置関係はFormation、周期運動はWaveMotionProfileへ分ける。旧Sceneの子方式は互換として残す。

### 98.4 推奨構成

| Hierarchy Object | 推奨Component | 責務 |
| --- | --- | --- |
| Player Ship | Rigidbody、Buoyancy、RailMovement、RailSpeedProfile、RailZone、SpeedFeedback | 物理船体、レール進行、可動範囲、速度演出。 |
| Main Camera | Camera、CameraFollowComposer、CameraFeedbackMixer | 追従姿勢と最終Camera演出。 |
| Enemy Wave Root | WaveSpawner、SpawnedObjectSetup、WaveMotionProfile | 多数生成、共通初期化、編隊運動。 |
| Enemy Pool | ObjectPool | 実体の再利用とRuntime Reset。 |
| Enemy Template | Renderer、Collider、Health、Team、RailMovement、C++ Script | 1体分の見た目、判定、移動、ゲーム固有行動。 |

### 98.5 Play確認

1. Play前にScene ViewのSpline線、進行方向、左右・上下範囲を確認する。
2. Play後にRail StateのCurrent Speed、Target Speed、OffsetとRailZone Runtime Indexを確認する。
3. Cameraが対象のWorld位置だけでなくYawへ追従し、曲線上でも後方に留まることを確認する。
4. SpeedFeedbackのRuntime速度率が0～1で変化し、FOVが速度に応じて連続変化することを確認する。
5. Spawn Countを増やし、生成個体のRail Path、Team、速度が全個体へ適用されることを確認する。
6. Poolへ返した個体を再生成し、速度倍率、Health、State、Timer等が前回状態を引き継がないことを確認する。
7. Sine、8の字、交互運動でFormation中心が崩れず、Rail可動範囲を越えないことを確認する。

### 98.6 責務境界

- RailSpeedProfileは「どの進行率で何倍速か」までを持ち、敵全滅で停止する規則は持たない。
- RailZoneは区間とAction通知までを持ち、Boss、BGM、会話の意味は持たない。
- CameraFollowComposerはCamera姿勢を合成し、プレイヤー入力や船体物理を変更しない。
- SpeedFeedbackは実速度を演出値へ変換し、Rail速度やRigidbody速度を変更しない。
- SpawnedObjectSetupは生成直後の共通設定だけを行い、敵AIや攻撃を実行しない。
- WaveMotionProfileはRail Offsetだけを作り、Damage、Target、Weaponを持たない。

この章追加後の機械照合基準はComponent 274件、Runtime API Entry 214件、C++ Script Template 24件である。

## 99. 大量配置の距離最適化とレールMarker制作手順

この節は専用のレールシューティング画面を作る手順ではない。通常のScene編集で、既存の敵Root、港湾Root、Effect Root、Rail移動Objectへ汎用Componentを追加する。

### 99.1 DistanceActivationで遠距離Objectを休止する

1. Projectの`Assets/Scenes`から対象`.scene`をダブルクリックして開く。
2. Hierarchyで、まとめて休止したい敵編隊Rootまたは背景小物Rootを選ぶ。
3. Inspectorの`コンポーネントを追加 -> 最適化 -> 距離アクティベーション`を選ぶ。
4. 距離基準を未設定にすれば最高Priority Camera、明示したい場合はPlayerまたはCamera Objectを割り当てる。
5. 有効化距離を実体が必要になる距離、無効化距離をそれより大きい値へ設定する。例: 250m / 300m。
6. Root配下をまとめて止める場合だけ`子階層も対象`を有効にする。
7. PlayしてCameraを往復させ、300m超で休止し、250m以下で復帰することを確認する。

境界値を同じにするとCamera揺れや船の波動でActive切替が頻発しやすい。無効化距離を有効化距離より10～25%大きくする。Object Poolからまだ生成していない敵を生成する機能ではなく、既に存在するObjectのRuntime負荷を止める機能である。

### 99.2 SimulationLODで遠距離処理だけを落とす

1. Hierarchyで敵、群衆、演出Root等を選ぶ。
2. `コンポーネントを追加 -> 最適化 -> シミュレーション LOD`を追加する。
3. Medium、Far、Culledを昇順で設定する。例: 100m / 250m / 500m。
4. Farで不要なPhysics、Script、AI、Animation、Effectだけを選ぶ。
5. 描画モデルのLODも必要なら、Renderer側の描画LODを別途設定する。SimulationLODだけではMeshを低ポリゴンへ差し替えない。
6. Play中にInspectorの`Runtime LOD`がNear、Medium、Far、Culledへ変わることを確認する。
7. Nearへ戻した時、Play前に有効だったComponentだけが復帰することを確認する。

MediumはC++ Scriptへ段階を渡すための区分であり、現行EngineはMediumだけを理由にComponentを停止しない。Farは選択系統を停止し、CulledはObjectを休止する。遠距離でも低頻度Scriptを残したい場合は`FarでScript停止=false`にして、`SimulationLod::GetLevel`の値から独自更新周期を変える。

親Rootと子Objectへ同時に最適化Componentを置く場合、両方で`子階層も対象=true`にしない。親が編隊全体、子が個別Effectなど別の距離基準を持つ場合は、片方の階層適用をfalseにして制御範囲を分離する。

### 99.3 RailEventMarkerで進行地点へ処理を置く

1. RailMovementを持つPlayer、Camera Rig、移動ObjectをHierarchyで選ぶ。
2. `コンポーネントを追加 -> 入力・イベント -> レールイベントマーカー`を追加する。
3. `Markerを追加`し、Marker ID、0～1進行率、通過方向、1回制限、Action名を設定する。
4. Action対象へ処理を受けるC++ Script Objectを指定する。未設定ならRail移動Object自身が対象になる。
5. C++ Script Asset作成で`移動・イベント / レールイベント受信`Templateを選ぶ。
6. 生成ScriptをAction対象へ追加し、Marker側Action名を`OnRailMarker`へ合わせる。
7. 生成コードの`markerId`分岐に、ActionRelay、WaveSpawner、Camera Blend等の呼出しを書く。
8. Playして高速通過、逆走、Loop境界でも設定方向どおり1回だけ通知されることを確認する。

Markerごとに空ObjectをHierarchyへ作らない。1つのRailEventMarker内部へ可変Entryとして保存する。視覚的な位置は`進行率 x Rail全長`で決まり、制御点を編集してRail長が変わっても正規化位置を維持する。

### 99.4 推奨する配置単位

| 対象 | Component | 推奨範囲 |
| --- | --- | --- |
| 敵編隊Root | DistanceActivation + SimulationLOD | 編隊全体の実体化と、近距離だけのAI / Animation / Physics。 |
| 港湾・岩礁小物Root | DistanceActivation | Cameraから遠い静的小物をまとめて休止する。 |
| 遠距離Effect Root | SimulationLOD | FarでParticle / VFX、CulledでRootを止める。 |
| Player Rail Object | RailMovement + RailSpeedProfile + RailZone + RailEventMarker | 連続速度、区間状態、地点通過Actionを分離する。 |
| Action受信Object | C++ Script / ActionRelay / ActionSequence | Marker IDをゲーム規則へ変換する。 |

DistanceActivationとSimulationLODは同じObjectへ併用できる。最終実体状態はPlay開始時Active、距離Activation許可、LODがCulled未満のすべてを満たす時だけActiveになる。DistanceActivationを300m、SimulationLOD Culledを500mにした場合は300m側が先に休止するため、意図した最短境界を確認する。

### 99.5 Play中の確認順

1. Game ViewのFPSだけでなく、HierarchyのActive、InspectorのRuntime LOD、Physics挙動を同時に確認する。
2. NearからFarへ移動し、AI、Animation、Effect、Physicsのうち選択した系統だけが止まることを確認する。
3. Culled後にCameraを戻し、元のActive状態とPool Item状態へ復帰することを確認する。
4. Railを高速度にしてMarkerを1Frameで飛び越えてもActionが欠落しないことを確認する。
5. Loop RailとReverseで、順方向のみ / 逆方向のみが混線しないことを確認する。
6. Play停止後、最適化がScene編集値へ書き戻されず、Marker通知済み表示がRuntime状態として消えることを確認する。

### 99.6 責務境界

- DistanceActivationは既存ObjectのActiveとPhysics実行を距離で切り替え、Prefab生成、敵状態の仮想化、描画LOD Asset生成を行わない。
- SimulationLODは距離段階と実行系統停止を担当し、AIの判断内容、Animation品質、Mesh LOD段数を決めない。
- RailEventMarkerは進行率通過とAction通知だけを担当し、Wave、Boss、BGM、会話というゲーム固有意味を持たない。
- レールイベント受信TemplateはMarker IDを受ける開始コードであり、ステージ進行をEngine Managerへ固定しない。
- シミュレーションLOD参照TemplateはRuntime Levelを読むだけで、EngineのActive制御をScript側へ重複実装しない。

この節の追加時点の機械照合基準はComponent 277件、Runtime API Entry 216件、C++ Script Template 26件だった。

## 100. 遠距離Waveを実体化しない制作手順

### 100.1 距離でWaveを開始する

1. 敵Prefab TemplateをObject Poolへ登録し、同時出現数に合わせて初期容量と最大容量を設定する。
2. Wave RootへWaveSpawnerを追加し、生成元を`ObjectPool 生成`にする。
3. 生成基準位置へ敵が現れる地点を指定する。未設定ならWave RootのTransformを使う。
4. 開始条件を`距離`へ変更する。
5. 距離 SourceへPlayer ShipまたはPlayer Camera Rigを指定する。
6. 開始距離を、敵が必要になる少し手前へ設定する。例: 描画距離500mなら開始距離450m。
7. 生成数と編隊を設定し、`1Frame最大生成数`を4～16程度から調整する。
8. Play開始直後はPool貸出個体がなく、Source接近後だけ複数Frameへ分散して生成されることを確認する。

Wave RootをPlayerの子にしない。距離基準点がPlayerと一緒に動くと距離が変化しない。生成地点と接近Sourceを別Objectにする。

### 100.2 Rail進行率、距離、外部開始の選択

| 開始方式 | 適する用途 | 必須設定 |
| --- | --- | --- |
| Play開始 | Title演出直後など必ず即開始。 | なし。 |
| RailFollower進行率 | 経路進行へ厳密に同期する敵配置。 | Rail Sourceと0～1進行率。 |
| 距離 | 分岐Rail、自由Camera、広いSceneで接近時だけ必要な敵。 | 距離 Source、生成地点、開始距離。 |
| 外部開始 | 敵全滅、会話終了、Button、任意ゲーム条件。 | C++ Scriptから`WaveSpawner::Start()`。 |

ゲーム条件をWaveSpawner内部へ追加しない。条件判定はGenericCondition、RailEventMarker、C++ Script等で行い、外部開始APIへ接続する。

### 100.3 生成Spikeを確認する

1. 生成間隔を0、生成数を100、1Frame最大生成数を8にする。
2. 距離条件を成立させる。
3. 1Frameで100体出ず、最大8体ずつ生成されることをHierarchyと生成Actionで確認する。
4. Pool容量不足時に処理が停止せず、空きができた後に残りが生成されることを確認する。
5. 全生成完了Actionと全撃破Actionが別タイミングで一度だけ発生することを確認する。
6. 実行中に外部Startしてfalseになり、既存敵の追跡が失われないことを確認する。

### 100.4 Script Update負荷を落とす

1. 多数の敵Rootまたは個体へSimulationLODを追加する。
2. Medium / Far / Culled距離を設定する。
3. Medium Script更新秒を`0.033`、Far Script更新秒を`0.2`から試す。
4. Farでも低頻度状態処理を残すなら`FarでScript停止=false`、完全停止するならtrueにする。
5. C++ Scriptの移動量、Timer、補間が受け取ったdeltaTimeを使用していることを確認する。
6. BindAction入力とRail Marker通知がMedium間隔待ちにならないことを確認する。

毎Frame必要なCamera追従やPlayer照準へ大きい更新秒を設定しない。敵の索敵、遠距離状態監視、環境小物Scriptなど、低頻度でも見た目や操作へ影響しない対象へ使う。

### 100.5 組合せ例

| Object | 設定 | 結果 |
| --- | --- | --- |
| Future Wave Root | WaveSpawner距離開始450m、最大8体/Frame | 接近前は個体を貸し出さず、接近後に分散生成。 |
| Spawned Enemy | SimulationLOD 120/300/600m | MediumでScript 30Hz、Farで5Hzまたは停止、600mで休止。 |
| Enemy Pool | 初期32、最大128、拡張可 | 同時出現数だけ実体を保持して再利用。 |
| Player Rail | RailEventMarker | 厳密な演出地点は距離ではなく進行率Actionを使う。 |

この節の追加時点の機械照合基準はComponent 277件、Runtime API Entry 218件、C++ Script Template 26件だった。
