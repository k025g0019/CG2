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

次の export 関数を個別に説明する。

- `EditorScript_Load`
- `EditorScript_Unload`
- `EditorScript_Start`
- `EditorScript_Update`
- `EditorScript_FixedUpdate`
- `EditorScript_OnPhysicsEvent`
- `EditorScript_Stop`

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
| Layer Matrix | `layerCollisionMatrix` | 8 Layer 間の衝突可否。対称更新か。 |

Physics Settings への到達方法、保存先、Scene ごとか Project 全体かを確認する。

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
| `AddImpulse` | GameObject ID、Impulse Pointer | bool | 瞬間的な速度変化。 |
| `AddTorque` | GameObject ID、Torque Pointer | bool | 回転力。Rolling の例。 |
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

### 56.1 必ず説明するライフサイクル関数

| 関数 | 呼ばれるタイミング | 使用目的 | 使用者が書く内容 | 注意点 |
| --- | --- | --- | --- | --- |
| `EditorScript_Load` | DLL 読み込み時に 1 回。 | API Version 確認、`runtimeApi` の保持。 | `apiVersion` と `api` を確認して `runtimeApi` を保存する。 | ここで GameObject 固有処理を書かない。 |
| `EditorScript_Unload` | DLL 解放時。 | 全 Script 状態の破棄。 | `unordered_map` などを clear する。 | `runtimeApi` を `nullptr` に戻す。 |
| `EditorScript_Start` | Play 開始時、対象 GameObject ごとに 1 回。 | 初期化。 | 速度、HP、初期状態、ログ。 | Transform 取得は可能だが、毎フレーム処理は書かない。 |
| `EditorScript_Update` | 毎フレーム。 | 入力、見た目の更新、通常の移動。 | `GetTransform`、Input Action、簡単な制御。 | Rigidbody 物理を使う場合は直接 Transform 更新と競合する。 |
| `EditorScript_FixedUpdate` | 固定時間物理更新。 | 力、速度、物理操作。 | `AddForce`、`AddImpulse`、`AddTorque`、`SetVelocity`。 | 物理系は原則ここで説明する。 |
| `EditorScript_OnPhysicsEvent` | Collision / Trigger 発生時。 | 接触イベント処理。 | 相手 ID、接触点、法線、相対速度で分岐。 | 毎フレームログを出す例は避ける。 |
| `EditorScript_Stop` | Play 停止、Object 破棄、Script 停止。 | GameObject ごとの状態削除。 | `scriptStates.erase(gameObjectId)`。 | 破棄済み状態を残さない。 |
| `EditorScript_InvokeAction` | Input Action Event から関数名で呼ばれる時。 | UI / Input から任意関数実行。 | `functionName` を比較し、対応処理を呼ぶ。 | 関数名の一覧と引数 Context を説明する。 |

### 56.2 Inspector 公開変数関数

使用者が「C++ で作った変数を Inspector に出す」ために必須である。
存在する場合は必ず説明し、未対応部分は制限事項に書く。

| 関数 | 目的 | 説明すべきこと |
| --- | --- | --- |
| `EditorScript_GetFieldCount` | 公開変数の数を返す。 | 0 の場合は Inspector に変数が出ない。 |
| `EditorScript_GetFieldDescriptor` | 変数名、表示名、型、初期値、Range を返す。 | `name` と `displayName` の違い、Min / Max / Step の意味。 |
| `EditorScript_GetFieldValue` | GameObject ごとの現在値を返す。 | `gameObjectId` ごとに値を保持する必要がある。 |
| `EditorScript_SetFieldValue` | Inspector から変更された値を受け取る。 | 型チェック、範囲外値、文字列長、保存対象。 |

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
| `AddImpulse` | 瞬間的な衝撃を加える。 | ジャンプ、弾かれ、爆発など。 |
| `AddTorque` | 回転力を加える。 | 球や車輪を転がす例に使う。 |

物理例は、次の作例を必ず用意する。

- W を押したら前方向へ速度を入れる。
- Space を押した瞬間に上方向へ Impulse を入れる。
- 球に Torque を入れて転がす。
- CollisionEnter で相手の名前または ID を見る。
- TriggerEnter でアイテム取得を行う。

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

テンプレートに入れるもの:

- `EditorScript_Load`
- `EditorScript_Unload`
- `EditorScript_Start`
- `EditorScript_Update`
- `EditorScript_FixedUpdate`
- `EditorScript_OnPhysicsEvent`
- `EditorScript_Stop`
- `runtimeApi` 保持。
- `scriptStates` の最小例。
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
| 3D物理 | 地形の当たり判定 | `TerrainCollider` |
| 3D物理 | 車輪の当たり判定 | `WheelCollider` |
| 3D物理 | キャラクターコントローラー | `CharacterController` |
| 3D物理 | コンスタントフォース | `ConstantForce` |
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
| 入力・イベント | イベントシステム | `EventSystem` |
| 入力・イベント | スタンドアロン入力モジュール | `StandaloneInputModule` |
| 入力・イベント | Input System UI 入力モジュール | `InputSystemUIInputModule` |
| 入力・イベント | プレイヤー入力 | `PlayerInput` |
| 入力・イベント | プレイヤー入力マネージャー | `PlayerInputManager` |
| 入力・イベント | タッチ入力モジュール | `TouchInputModule` |
| 入力・イベント | 入力 | `Input` |
| ゲームプレイ | ローカル移動 | `LocalMove` |
| ゲームプレイ | ローリング移動 | `RollingMove` |
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
