# CG2Engine 利用者ガイド

更新基準: 2026-09-29

この文書はEngine利用者向け情報の集約先である。Project作成、Scene編集、Play確認、Window、実践ワークフロー、トラブルシューティング、配布、共同制作、外部認識・Online・Hapticsをこの1冊で扱う。実装状態と内部構造は`engine-internals.md`、個別Componentの全Fieldは`component-reference.md`、Script API全件は`script-api-reference.md`を参照する。

主要部は「基本制作手順」「実践ワークフロー」「Editor Window・設定」「トラブルシューティング」「配布・Version管理」「共同制作」「遠隔共同制作」「外部認識・Online・Haptics」の順に並ぶ。文書内検索では機能名、Component名、Window名、症状名を使う。

## 1. 最初に理解する5つ

1. **Project**は`Assets`、`resources`、`NativeScripts`、`ProjectSettings`をまとめたFolderである。
2. **Scene**はGameObjectの配置とComponent設定を保存する。
3. **GameObject**は名前とTransformを持つ入れ物で、機能はComponentとして追加する。
4. **Play**は編集SceneのCopy上で実行され、StopするとPlay前へ戻る。
5. **Build**は起動Sceneと参照Assetを集め、Editor外で実行できるFolderを作る。

## 2. LauncherからProjectを開く

### 新規Project

1. Launcherで導入済みEngine Versionを確認する。
2. `Projectを作成`からProject名、保存先、Engine Version、Templateを選ぶ。
3. LauncherがProject ID、Collaboration ID、Owner、Project Versionを生成する。
4. Project一覧から開く。

### 既存Project

Project FolderをLauncherへ登録して開く。Projectが要求するEngine Versionと異なる場合は、Launcherで該当Versionを導入する。Version Metadataが無い旧Projectは、Backup付きMigrationを確認してから開く。

### 参加コード

共同制作者から`XXXX-XXXX`形式のコードを受け取った場合は、LauncherのProject画面へ入力する。Launcherは設定済みHubからProject Snapshot、固定Engine、共同制作接続情報を取得する。コードだけではHubを特定できないため、初回は正しいHub設定が必要である。

HubがTailscaleの`*.ts.net`アドレスの場合、Launcherは現在接続中のTailscaleネットワークを確認する。参加先と違う場合は切替確認が表示され、登録済みなら参加先へ自動で切り替わる。未登録の場合はブラウザ認証が開くため、参加先ネットワークを選んで認証し、Launcherの「認証完了」で続行する。Tailscaleの認証情報をCG2Engineへ入力する必要はない。

参加完了時は、最終的な保存先と「保存先をExplorerで開きますか？」が表示される。あとから開く場合はProject一覧で対象を選び、`保存先を開く`を押す。`場所を変更`は既存Projectフォルダーを再登録する操作であり、Explorerを開く操作とは分けている。

## 3. Editorの基本画面

| 画面 | 主な用途 |
| --- | --- |
| Hierarchy | Scene内GameObjectと親子関係 |
| Scene View | 配置、選択、Camera操作、Gizmo、Debug表示 |
| Game View | Camera Componentからの実行画面 |
| Inspector | 選択GameObjectのTransformとComponent設定 |
| Project | Asset作成、選択、移動、Reimport、依存確認 |
| Console | Error、Warning、Runtime、Build、共同制作Message |
| Animation | ClipとKeyframeの編集 |
| Diagnostics | Profiler、Scene検査、Runtime統計 |
| TEAM | 共同制作の接続、履歴、付箋、競合、復旧 |

Layoutが崩れた場合はメニューからLayout再構築を実行する。Windowを閉じた場合はWindowメニューから再表示する。

## 4. Sceneを作る

1. Project ViewでSceneを作成するか、既存`.scene`を開く。
2. HierarchyまたはGameObjectメニューから空Object、Model、Light、Camera等を作る。
3. Scene Viewで選択し、`W`移動、`E`回転、`R`拡縮を使う。
4. Inspectorで名前、Active、Transform、Componentを編集する。
5. Sceneを保存する。

Transformは親に対するLocal値である。親を持つObjectのWorld位置を合わせる場合はLocal/World Modeを確認する。負のScaleは表示・Collider面方向に影響するため、意図しない反転がないか確認する。

## 5. Assetを入れる

推奨手順:

1. Project Viewで目的Folderを作る。
2. Model、Texture、Audio等をProject内へ追加する。
3. Assetを選択してImport StateとImport Settingsを確認する。
4. 必要なら`Reimport`する。
5. 依存関係とMissing参照を確認する。
6. SceneへDrag & DropするかComponentのAsset欄へ指定する。

移動・改名はProject Viewから行う。Explorerで直接移動するとRegistryが同一Assetと断定できず、新しいAsset IDになる場合がある。

## 6. Componentを追加する

1. GameObjectを選択する。
2. Inspector下部のComponent追加を開く。
3. カテゴリまたは検索から追加する。
4. 必須参照と設定値を入れる。
5. ConsoleとInspectorのStatusを確認する。

よく使う組合せ:

| 目的 | Component例 |
| --- | --- |
| 3D表示 | MeshFilter + MeshRenderer |
| 落下・衝突 | Rigidbody + Collider |
| Game画面 | Camera + AudioListener |
| 音を鳴らす | AudioSource |
| 入力で動く | PlayerInput/Input + RigidbodyまたはCharacterController |
| Animation | Animator/Animation + Clip/Graph |
| 敵 | Health/DamageReceiver + Target/AI + Weapon |
| 大量生成 | ObjectPool + PrefabSpawner/WaveSpawner |
| UI | Canvas + Text/Image/Button/Slider等 |

Componentを付けただけで動かない場合は、Active、参照Asset、対象GameObject、Play中のみの機能かを確認する。

### GameObject上部のTag、Layer、Staticについて

2026-09-29時点では、Inspector上部の`Tag`、`Layer`、`Static`は配置確認用の仮UIであり、GameObjectごとの値として保存されない。Tagは`Untagged`、Layerは`Default`だけで、Scene保存、Prefab、Undo、共同編集、Native Script検索へ接続されていない。

ゲーム上の分類には目的別の既存機能を使う。

| 目的 | 使用するもの |
| --- | --- |
| 特定機能を持つObjectを探す | Component種類による検索 |
| 物理衝突・Raycastを分ける | Collider等の衝突レイヤーとLayer Collision Matrix |
| Bullet、Explosion等のDamage分類 | Weapon/AreaDamageのDamage TagとDamageTagModifier |
| Metal、Water、Wood等の命中表面 | SurfaceTypeのSurface Tag |
| 味方・敵等の陣営 | Team Component |

汎用GameObject Tagが保存・検索できる前提でSceneを設計しない。内部の違いと処理経路は`engine-internals.md`の「0.13 『Tag』と呼ばれている機能の実態」を参照する。

## 7. CameraとGame View

1. Camera Componentを持つGameObjectを作る。
2. Priorityと有効状態を確認する。
3. FOV、Near/Far、Projection、Orthographic Sizeを設定する。
4. PlayしてGame Viewを確認する。

Cameraが複数ある場合はPriorityとActive Camera切替を確認する。Scene ViewのCameraは編集専用で、Game View Cameraとは別である。

## 8. LightingとPostProcess

1. Directional/Point/Spot Lightを配置する。
2. Shadow、Environment、Reflection Probe、Light Probeを必要に応じて設定する。
3. PostProcess Componentを追加する。
4. Bloom、AA、Tone Mapping、Exposure等を調整する。
5. Scene ViewとGame Viewの両方で確認する。

AAはNone/FXAA/SMAA/Temporalから1つを選ぶ。TemporalはCamera移動やScene/Game View切替でHistoryを使用するため、静止画だけでなく動かして残像も確認する。

## 9. Physics

1. 動くObjectへRigidbodyを追加する。
2. Box/Sphere/Capsule/Mesh/AutoConvex等のColliderを追加する。
3. Static/Dynamic/Kinematic、Mass、Gravity、Layer、Triggerを設定する。
4. Scene Viewの物理Debugを有効にする。
5. Playして実Shape、Contact、Castを確認する。

当たり判定がずれる場合はTransform、Collider Center/Size、親Transform、負Scale、Play中の実Jolt Shapeを比較する。水色枠はScene CameraのZoomでWorld Sizeが変化しない。

## 10. Input

1. Player GameObjectへPlayerInputを追加する。
2. Input Action Assetを作成または指定する。
3. Action MapとAction名を設定する。
4. Keyboard/Mouse/Gamepad Bindingを確認する。
5. Script Actionまたは移動Componentへ接続する。

GamepadはDead Zoneと感度をProject Settingsで調整する。複数台の場合はPlayer Indexと接続状態を確認する。

## 11. C++ Script

1. Project ViewからNative Script Templateを作成する。
2. Visual Studio等でDLLをBuildする。
3. Script/MonoBehaviour ComponentへDLLを指定する。
4. Inspectorの公開Fieldを設定する。
5. PlayしてStart/Update/FixedUpdate/Eventを確認する。

DLL更新時はInspectorの状態とConsoleを見る。新DLLがLoad検証に失敗した場合は旧DLLを維持する。Native Script内のCrashはEngineが安全継続できないため、Development BuildとDebuggerを使う。

## 12. Animation

- 単純な値AnimationはAnimation WindowでClip、Track、Keyを編集する。
- 状態遷移はAnimator GraphでParameter、State、Transition、Blend Sampleを作る。
- Animation EventはScript ActionやEffectへ接続する。
- Root Motionを使う場合はRigidbody/Movementとの二重移動に注意する。

Layer、Avatar Mask、Nested State Machineは現在未対応である。

## 13. Audio・Effect・UI

### Audio

AudioSourceへClip、Volume、Pitch、Loop、3D設定、Busを指定する。Project SettingsのMaster/各Bus Volumeも確認する。

### Effect

使用Assetに応じてParticleSystem、VisualEffect、Effekseer等を選ぶ。Play開始、Script、Animation Event、Weapon Hitから発生させられる。

### UI

Canvas配下にText/Image/Button/Slider/Toggleを置く。NavigationとInteractableを設定し、ScriptまたはUiBindingから値を更新する。

## 14. Prefab

1. HierarchyでPrefab化するRootを選ぶ。
2. Prefabとして保存する。
3. Project ViewからSceneへ配置する。
4. Instance変更を元へ反映する場合はApply、破棄する場合はRevertを使う。

Variantと一部Overrideは対応するが、任意Propertyの完全な差分表示やUnity相当のNested Prefab編集を前提にしない。

## 15. Play確認

Play前にSceneを保存する。Play中は次を確認する。

- Console Error
- Game View Camera
- Physics/Collision
- Input
- Audio
- Animation/Effect
- UI
- DiagnosticsのFrame/GPU/VRAM

Stop後にRuntime変更が残らないことは正常である。残したい設定はEdit Modeで変更する。

## 16. 共同制作

TEAM Windowで接続状態、Project ID、Protocol、Revision、Memberを確認する。Remote Cursor、選択、Camera、Lock、差分、Asset進捗を順に確認する。

付箋/Ping/Chat/ReviewはScene View、Hierarchy、Inspector、Project Viewから対象へ直接付けられる。TEAM Windowでは検索、返信、編集、削除、担当、解決、Review状態を操作する。

片方がPlay中でも、Playしていない人の編集は失われない。Play側ではStop後まで保留され、Play前Scene復元後に反映される。

## 17. Diagnosticsと性能確認

DiagnosticsでCPU/GPU Frame History、各Subsystem、VRAM、Draw/Dispatch、Physics Body等を確認する。一瞬の引っ掛かりは平均値だけでなくFrame HistoryのSpikeを見る。

PerformanceSettingsでScene/Game View、Shadow、Planar Reflection、Ocean FFT、Light Probe等の更新頻度を調整できる。値を下げる前に、どのPassが重いかをProfilerで特定する。

## 18. 外部認識・オンライン・Haptics

InspectorのComponent追加には`外部認識`カテゴリがあり、音声認識、Camera入力、画像認識を追加できる。HapticSourceは`FeelKit`カテゴリにある。Project SettingsにはHapticsの全体強度とOnline Services設定がある。

最初は次の小さい構成で確認する。

- Speech: Keyword 1個を既存Input Actionへ接続する。
- Vision: Camera 1台、640×480、30 FPS、内蔵の動体検出を使う。
- Haptics: Inspectorプレビューで一定Patternを1回鳴らす。
- Online: Development Workerの`/health`確認後、開発用Leaderboardへ送る。

Camera/マイクの権限拒否、Deviceなし、Networkなしでもゲームを継続できる設計にする。Camera画像とマイク音声は標準実装ではCloudflareへ送信せず、Local Backendで処理する。Client Keyは秘密鍵ではなく、秘密情報はWorker側のSecretへ置く。

詳しい設定例、C++ Script例、Memory目安、配布前確認、症状別切り分けは本書を参照する。内部のThread、Buffer、DirectX 12/OpenGL境界は[engine-internals.md](engine-internals.md)を参照する。

## 19. Build

1. `ゲームをビルド`を開く。
2. Product名、出力先、Development/Releaseを選ぶ。
3. 起動Sceneと遷移可能Sceneを選ぶ。
4. 通常は参照Assetだけを出力する。
5. Buildし、`BuildLogs`を確認する。
6. 出力FolderをProject外または別PCへ移して起動する。

Editorで動くだけでは配布完了ではない。DLL、Shader、Asset、起動Scene、保存先権限をStandaloneで確認する。

## 20. 保存してから終了するもの

Editorを閉じると、Scene Pathの有無に関係なく「シーンを保存して終了しますか？」を表示する。
「はい」はSceneを保存して終了し、「いいえ」は保存せず終了し、「キャンセル」はEditorへ戻る。
新規Sceneで「はい」を選んだ場合は保存先を指定する。

- Scene
- Project Settings
- Input Action
- Animation Clip/Graph
- Effect/Material等の編集Asset
- TEAM接続設定
- Native Script Source/DLL

Play中の現在HP、生成Object、現在Particle数等は編集設定ではない。

## 21. 次に読む文書

- 問題が起きた: 本書
- 全機能の状態: [engine-internals.md](engine-internals.md)
- Component全Field: [component-reference.md](component-reference.md)
- Script全API: [script-api-reference.md](script-api-reference.md)
- Window詳細: 本書
- 共同制作: 本書
- 配布とVersion: 本書
- 保存形式と復旧: [engine-internals.md](engine-internals.md)
- 外部認識の利用手順: 本書
- 外部認識の内部設計: [engine-internals.md](engine-internals.md)

---

## 実践ワークフロー

このページは、CG2Engine を初めて開いた人が実際に制作・確認・配布まで行うための手順書です。機能一覧ではなく、操作と失敗時の確認先を記載します。

### 1. Scene 上で物理を確認する

1. 対象 GameObject に `AutoConvexCollision` を追加し、モデル Asset と `最大 Hull 数` を設定します。
2. Scene View の物理 Debug で `Collider` を有効にします。
3. Edit 中は最終 AutoConvex Hull の三角面ワイヤーが、Play 中は Jolt が実際に使用している Shape の三角面ワイヤーが表示されます。
4. Play 中に `Contact` と `Cast` を有効にすると、接触点と法線、Ray/Sphere/Capsule Cast の始点・終点・Hit 点が表示されます。

期待と違う場合は、まず `AutoConvexCollision` が有効か、モデル参照が切れていないか、Console に「Auto Convex 生成に失敗」の表示がないかを確認します。生成に失敗した場合は安全のため Box 近似になります。MeshCollider はモデル形状そのものを使うため、誤解を招く Bounds 箱は表示しません。

Box系の水色Debug枠はGameObjectの位置・回転・ScaleとColliderのCenter/SizeをWorld座標へ変換して描画します。Scene Cameraの拡大縮小はCollider自体の大きさを変更しません。ImGuiの線にはDepth判定がないため、現在はカメラを向いた面に属する外形線だけを表示し、モデル奥側の枠が透けてずれて見える状態を抑えています。負のScaleでも面法線はCollider外向きへ補正します。枠と物理判定が一致しない場合は、画面上の線だけで判断せず、対象Transform、Collider Center/Size、実際のJolt Shapeを比較してください。

### 2. Asset を安全に更新する

1. Project Window で Asset を選択します。
2. 詳細の `Reimport` を押します。成功時は Import State が `Imported` になり、Console に `Reimport OK` が出ます。
3. 参照が変わった Asset は `依存関係を再取得` を押します。`依存関係` に表示される赤い `[Missing]` は、参照先ファイルが無い状態です。
4. 削除前は逆依存一覧を確認し、参照元を直すか削除を中止します。

日本語を含むパスは UTF-8 として扱います。ファイル名を変更・移動する場合は Project Window の移動操作を使ってください。エクスプローラーで移動してから再走査すると、新規 Asset と見なされ GUID を維持できません。

`Failed`、`Missing Source`、または `RequiresManualAction` の場合は、State の直下にある Error と Console を確認します。Animation と Script は Play 中の安全な自動差替えを行わない場合があるため、Stop 後に再読込または再ビルドします。

### 3. ゲームを書き出して別の PC で確認する

1. `File > ゲームをビルド...` を開きます。
2. `ゲーム名`、`出力先`、`ビルド構成` を選びます。`Development (Debug)` は調査用、`Release` は配布用です。
3. 必要な Scene をチェックし、そのうち一つを `起動` にします。
4. 通常は `参照される Asset だけを出力` を有効にします。起動 Scene から辿れる Asset と依存、実行 DLL、ThirdParty、共通 Shader が含まれます。
5. `ゲームを書き出す` を押し、出力フォルダー内のゲーム exe をプロジェクト外の場所から起動します。

失敗時は Console に出る `Build:` メッセージと、`BuildLogs/DevelopmentGameBuild.log` または `BuildLogs/ReleaseGameBuild.log` を確認します。起動 Scene 未選択、Scene 消失、DLL コピー失敗、Asset コピー失敗は原因ファイルまで表示されます。出力したフォルダーだけを別 PC にコピーして起動確認してください。

### 4. Post Process と画面を一致させる

1. Scene に `PostProcess` を追加します。
2. `アンチエイリアス` は None / FXAA / SMAA / Temporal のいずれか一つを選びます。
3. SMAA は `しきい値` と `角丸め`、Temporal は `シャープ` と `履歴ブレンド` を Inspector で調整します。
4. Play と Stop を切り替え、Scene View と Game View の双方で確認します。

これらの値は表示だけでなく RenderManager の SMAA/Temporal 実行へ渡されます。Temporal の履歴は Scene View と Game View で分離されています。差が見えるときは、両方の Viewport が表示中か、PostProcess Component が有効か、AA mode が意図した値かを確認します。

### 5. C++ Script を調査する

1. GameObject に `Script` または `MonoBehaviour` を追加し DLL を指定します。
2. Inspector の `状態メッセージ` で、DLL の有無、Load 結果、最後の再読込結果を確認します。
3. Play 中は DLL 更新を検出します。新 DLL が LoadLibrary 検証を通らない場合は Console に失敗理由を残し、旧 DLL を継続します。
4. 値を追う場合は Log Monitor で対象 GameObject / Component / System Field を Watch に追加し、RuntimeLog を保存します。

コンパイルエラーは DLL を作る IDE の出力（ファイル名・行番号）で解決します。Engine 側では DLL の読み込み・API 初期化・Hot Reload の結果を Console と Inspector に残します。ネイティブ DLL 内のアクセス違反は Engine が安全に継続できないため、Development build と Visual Studio の例外設定で原因行を調査してください。

### 6. Navigation を確認する

1. 移動できる床に `NavMeshSurface`、移動者に `NavigationAgent` を設定します。
2. 障害物には `NavMeshObstacle`、飛び越えなどには二点を結ぶ `NavMeshLink` を設定します。
3. Play を押すと Scene View に、Surface は緑、Obstacle は赤、Off-Mesh Link は青、Agent の計算経路は黄で表示されます。
4. Script から Destination を指定した後、`HasPath`、`GetRemainingDistance`、`GetLastPathFailureReason` で実行状態を確認します。

経路が出ない場合は、Destination が Surface 外ではないか、Obstacle が通路を完全に塞いでいないか、Console と `GetLastPathFailureReason` を確認します。現在の経路計算は障害物を考慮した折れ線であり、複雑な迷路向けの完全 A* NavMesh ではありません。

### 7. NVIDIA Blast でObjectを破壊する

1. 通常の `MeshFilter` / `ModelRenderer` / `Collider` を持つ壁へ `DestructiblePart` を追加します。
2. `NVIDIA Blastを使用` をONにします。既定の `Voronoi / 20 Chunk / Auto Bake ON` のままで利用できます。
3. Playを押します。Source Meshと設定に一致するCacheが無ければ、Play開始前にFracture Mesh、Chunk用Auto Convex Collider、Bond定義を自動生成します。生成ChunkはHierarchyへ表示されません。
4. 通常のHitscan / ProjectileがDamage Contextを作る場合は、命中位置とDamageが自動でBlastへ流れます。独自攻撃や爆発はPlay中にC++ Scriptから命中位置・半径・Damage・Impulseを渡します。最初のActor分裂時に元MeshとColliderを止め、分離したChunkだけJolt Dynamic Bodyへ切り替えます。

```cpp
const EditorScriptVector3 hitPosition{hit.point.x, hit.point.y, hit.point.z};
BlastDestruction::ApplyDamage(
    destructibleRootId,
    hitPosition,
    2.0f,   // Damage半径
    120.0f, // Bondへ与えるDamage
    15.0f); // 分離Chunkへ与えるImpulse
```

全破壊は `BlastDestruction::FractureAll(rootId, impulse)`、状態確認は `IsFractured`、`GetChunkCount`、`GetActorCount` を使います。Health SourceのHealthが0になった場合も全Bond破断として処理されます。
半径とImpulseをInspectorの既定値に任せる場合は、簡略版 `BlastDestruction::ApplyDamage(rootId, hitPosition, damage)` を使います。

Inspectorの `Status` は `Needs Bake`、`Baking`、`Ready`、`Failed`、`Missing Source` のいずれかです。設定変更後に直ちに作り直す場合は `Rebake`、生成済みデータを捨てる場合は `Clear Cache` を押してからPlayします。`Failed` の場合は同じ欄の理由とConsoleを確認してください。閉じていないMesh、壊れたIndex、Authoring DLL不足はCrashさせず理由を表示します。

CacheキーはSource Meshの頂点・Index、Chunk Count、Random Seed、Fracture Method、Collider品質、近傍Bond数、Generator Versionから作られます。同じPrefabを複製してもGeometry、Collider入力、Bond定義は共有し、Damage状態、Blast Actor、Transform、Jolt Bodyだけを配置ごとに持ちます。Release Buildは未Bake AssetをBuild前に自動Bakeし、`Library/FractureCache`をゲームへ収集します。Bake専用の`NvBlastExtAuthoring.dll`は配布せず、RuntimeはBake済みデータだけを使います。

外部DCCで分割済みMeshを使う旧方式は `Advanced: 外部事前分割Mesh` の `直下の子をChunkとして使用` をONにした場合だけ有効です。通常ワークフローでは、破片Mesh、Chunk子GameObject、Collider、Bondを手作業で用意しません。

#### 破片を軽くする（破片軽量化）

既定はOFFです。OFFのままなら分離Chunkを全てRigidbody化する本来の破壊が動き、この節の設定は一切効きません。重い場合だけInspectorの `破片軽量化 (GPU破片 / 物理数制限 / Cluster / LOD)` を開き、`破片軽量化を使う` をONにします。Bake結果には影響しないため、Rebakeは不要です。

ONにすると、分裂した破片を次の順番で振り分けます。3つは排他ではなく同時に成立します。

1. `物理Chunk上限` の数まで、体積の大きい破片をRigidbody化します。`0` なら物理破片を作りません。
2. `あふれをClusterでまとめる` がONなら、あふれた破片を1.の破片の子として運びます。Rigidbodyは増えません。1つが運ぶ数は `Cluster内Chunk数 - 1` までで、`ばらけ開始秒数` 後に見た目だけ `ばらけ距離` までずれます。
3. `残りをGPU破片にする` がONなら、さらにあふれた破片をGameObjectを作らずGPU Particleとして飛ばします。`GPU破片の寿命` で消え、重力・減衰・風・回転はGPU側で計算します。
4. 2.も3.も使えない破片は、消さずにRigidbodyへ戻します。設定の組み合わせで破片が消えることはありません。

| やりたいこと | 設定 |
| --- | --- |
| 本来の破壊 | `破片軽量化を使う` をOFF |
| 見た目20個・物理5個 | 上限 `5` / Cluster ON（Cluster内 `5`）/ GPU破片 OFF |
| 爆発で大量に壊す | 上限 `5` / Cluster ON（Cluster内 `2`）/ GPU破片 ON |
| 物理を使わず見た目だけ | 上限 `0` / Cluster OFF / GPU破片 ON |

`物理化を続ける秒数` に0より大きい値を入れると、Rigidbodyになった破片はその秒数だけ飛び散ってからJolt Worldを外れ、描画だけの瓦礫として残ります。瓦礫を長時間残しても物理負荷が残りません。`0` なら解除しません。

`距離で軽量化を強める` はGame ViewのCameraからObjectまでの距離で予算を絞ります。`近距離までの距離` 未満は設定どおり、そこから `遠距離になる距離` までは `物理Chunk上限` を半分にしてGPU破片を有効化し、それ以上離れていると物理とClusterを止めて `遠距離のGPU破片数上限` 個のGPU破片だけにします。判定は最初の分裂時に1回だけ行うため、破壊途中で方式が変わりません。Game View用のCamera Componentが無いSceneでは原点からの距離になります。

Play中はConsoleへ `optimize=on physics=5 cluster=10 gpuDebris=5 vanished=0` の内訳が出ます。GPU破片が出ない場合はここで `gpuDebris=0` を確認し、Chunk MeshのPathと `残りをGPU破片にする` を見直します。`GPU破片のMesh種類上限` はMesh種類ごとにGPU Draw Callが1つ増えるため、既定の `4` のまま使うことを勧めます。`0` にすると全Chunkが個別Meshになります。

これらの値はScene / Prefabへ保存されます。設定を入れていない既存Sceneを読み込んだ場合は、`破片軽量化を使う` がOFFの状態になります。

#### 破片を片付ける（沈下・消滅）

瓦礫を残したままにするとDraw Callと画像SRVを占有し続けます。`破片の後片付け (沈下・消滅)` の `沈み始める秒数` に0より大きい値を入れると、Rigidbody化した破片がその秒数後に沈み始め、`沈む秒数` かけて `沈む距離` だけ下がり、沈み切った時点でGameObjectごと消えます。破棄と同時にDraw Call、Constant Buffer、共有Texture参照が解放されます。

この設定は**破片軽量化のOnOffとは独立**です。本来の破壊（軽量化OFF）のまま瓦礫だけ片付けることができます。既定は `0` なので、何も設定しなければ従来どおり瓦礫は残り続けます。

沈み始める瞬間にJolt Worldから外れるため、沈んでいる間の破片は他のObjectと当たりません。Cluster追従の破片はCarrierの子なので一緒に消えます。GPU破片はGameObjectを持たないので、この設定ではなく `GPU破片の寿命` で消えます。

GPU破片の飛び方は `GPU破片の運動` で決めます。既定の `爆発` は破壊中心から外向きへ加速し続け、`GPU破片の上昇気流`（既定6.0）で直後だけ舞い上がってから落ちます。`渦` は中心まわりを旋回しながら上昇するので、煙や竜巻のような見え方になります。`直線` は初速だけで放物線を描きます。これらはGPU側が毎フレーム計算するため、数百個でも負荷はほとんど変わりません。

同時に多数のObjectを壊す場合は、Project Settingsの `Scene全体の物理破片上限`（既定100）も効きます。DestructiblePartごとの `物理Chunk上限` を5にしていても、100個の壁を同時に壊せば物理破片は500個になるため、Scene全体の総数はこちらで止めます。上限に達した以降の破片はCluster追従かGPU破片へ回ります。

同じ画像を使う破片は内部で1枚を共有するので、破片を増やしても画像SRVの消費は増えません。Chunkごとに画像を読み直していた頃は、破片300個で当時の上限（826枚）に達して描画が壊れることがありました。現在は共有に加えて上限自体も65338枚へ広げています。

### 8. 参加コードで共同制作へ参加する

1. 配布者はLauncherで対象Projectを選び、HubへProjectを公開します。
2. 公開成功時に表示される`XXXX-XXXX`形式の参加コードを共同制作者へ渡します。同じProject IDである限りコードは変わりません。
3. 参加者は対応版LauncherのProject画面へコードを入力します。Launcherは設定済みHubからProject、指定Engine Version、共同制作接続設定を取得してEditorを起動します。
4. Editorの`TEAM - 共同制作`でProtocol 3、Project ID、Role、Revisionを確認します。両端のProject IDは同じ値でなければなりません。
5. Scene ViewのRemote Cursor、相手視点への移動、GameObject変更、TeamItemのBadgeと通知を順に確認します。

`Project IDが一致しません`と出る場合は、参加コードそのものではなく、Hubへ公開したProject、Launcher登録、Project Metadata、起動中CG2TeamServerのIDが食い違っています。古いProject用Serverが同じPortに残っていれば対応版Editorが検出して起動し直します。それでも直らない場合は両端の接続診断に表示されたServer/Client IDを比較してください。

片方だけPlayした場合、その端末のRuntime変更はStopで元へ戻ります。Playしていない相手の編集はPlay側で保留され、Stop時にPlay開始前Sceneを戻した後で反映されます。Play中に生成されたObjectと、相手がEdit中に追加したObjectを混同しないでください。

付箋、Ping、チャット、レビューはTEAM Windowだけでなく、Scene View、Hierarchy、Inspector、Project Viewの`TEAM N` BadgeとContext Menuから対象へ直接作成・表示できます。作成済みItemはTEAM Windowで返信、編集、削除、担当、解決状態、Review状態、検索を操作します。詳細は本書を参照してください。

---

## Editor Window・設定リファレンス

このファイルは、ChatGPT Work が使用者向けサイトを作るための「エディタウィンドウ」「メニュー操作」「Project設定」詳細素材である。
`docs/documentation-authoring.md` は調査仕様、`docs/component-reference.md` はComponent詳細、`docs/script-api-reference.md` はC++ Script API詳細、このファイルはそれ以外（ウィンドウ・設定）のページ本文下書きとして使う。

調査対象は上部メニューバー「ウィンドウ」内の全項目と、「編集 > 設定を開く」で表示されるProject設定である。

### 共通で必ず書くこと

各ウィンドウページには、次を必ず入れる。

| 項目 | 書く内容 |
| --- | --- |
| 目的 | そのウィンドウが何をするか。 |
| 開き方 | メニューのどこから開くか、既定表示か。 |
| 前提条件 | 使うために何が必要か（Component、選択状態、Play中か等）。 |
| 画面構成 | 主なUI要素とその意味。 |
| 主な操作 | ボタン・値をどう使うか。 |
| Play中の違い | 編集中とPlay中で挙動が変わる点。 |
| 保存 | Scene / Prefab / 設定Fileへ何が保存されるか。 |
| 制限・注意 | 未実装、設定のみ、既知の癖。 |
| 確認手順 | 最小構成での動作確認方法。 |

根拠は `Source/Engine/Editor/` 配下のファイル名・クラス名・関数名で示す。

---

### ウィンドウメニュー全体像

上部メニューバー「ウィンドウ」を開くと、次の項目が並ぶ。

根拠: `Source/Engine/Editor/EditorMainMenuBar.cpp` : メニュー構築処理（`BeginMenu("ウィンドウ")` 以降）

| 表示名 | 種別 | 対応する変数 / 関数 |
| --- | --- | --- |
| アニメーション | チェックボックス付きWindow表示切替 | `g_isAnimationWindowVisible` |
| Spline Editor | チェックボックス付きWindow表示切替 | `g_isSplineEditorVisible` |
| Event Timeline | チェックボックス付きWindow表示切替 | `g_isGameplayTimelineWindowVisible` |
| State Graph | チェックボックス付きWindow表示切替 | `g_isStateGraphWindowVisible` |
| 診断・Profiler | チェックボックス付きWindow表示切替 | `g_isDiagnosticsWindowVisible` |
| ログ監視 | チェックボックス付きWindow表示切替 | `g_isLogMonitorWindowVisible` |
| 共同制作 | チェックボックス付きWindow表示切替 | `g_isTeamCollaborationWindowVisible` |
| Hook / Wire デバッグ | チェックボックス付きWindow表示切替 | `g_isHookWireDebugWindowVisible` |
| 描画負荷テスト Scene を作成 | 即実行（確認Popup経由） | `CreateRenderStressScene` |
| ゲーム基盤検証 Scene を作成 | 即実行（確認Popup経由） | `CreateGameplayFoundationValidationScene` |
| Console 表示 | 即実行 | `g_isConsoleCleared = false` |
| 選択解除 | 即実行 | `ClearSelectedGameObjects()` |
| レイアウト再構築 | 即実行（次回起動時反映） | `g_isDockLayoutInitialized = false` |

「アニメーション」〜「Hook / Wire デバッグ」の8項目は独立したDockableウィンドウで、チェックを外すまで開いたままになる。Scene保存とは無関係で、Windowの表示状態自体はSceneファイルへ保存されない（Editor起動ごとに既定の表示状態へ戻る想定）。

---

### 1. アニメーション ウィンドウ

- 目的: GameObjectのTransform・Light・Materialの値を時間軸で編集する `.animclip` アセットを、Timeline UIで作成・編集する。Unityの Animation Window に相当する。
- 開き方: メニュー「ウィンドウ > アニメーション」。
- 前提条件: 対象GameObjectに `Animation` Componentが必要（無ければ「Clipを割り当て」操作時に自動追加される）。Clip保存先は `Assets/Animation/` を想定。
- 実装: `Source/Engine/Editor/EditorAnimationWindowManager.h/.cpp`、Clip実体は `Source/Engine/Animation/PropertyAnimationClip.h`。
- 構成: `BeginTabBar` により2タブ構成(2026-09-12更新)。「Clip (.animclip)」タブが本章の内容、「Animator Graph (.animgraph)」タブは次項「1-2」を参照。

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| ツールバー | 保存、再読込、Preview開始/停止、Record開始/停止、時間設定（`DrawToolbar`）。 |
| Track一覧 | 追加済みProperty Trackの選択・削除。Combo Boxから新規Track対象Propertyを選び追加する（`DrawTrackList` / `AddPropertyTrack`）。 |
| Timeline | 秒目盛り、Track行、Keyframe、再生ヘッドを描画（`DrawTimeline`）。 |
| 選択Key編集 | 選択中Keyframeの時刻・値・接線（In/Out Tangent）・補間方式を編集（`DrawSelectedKeyEditor`）。 |
| Event編集 | 指定時刻でC++ Script / Effectへ通知するAnimation Eventを編集（`DrawEventEditor`）。 |

#### アニメーション可能なProperty（`AnimationPropertyTarget`）

Transform: 位置X/Y/Z、回転X/Y/Z（度）、スケールX/Y/Z。
Light: 強さ、範囲。
Material: BaseColor R/G/B、Metallic、Roughness、Alpha、Emission強さ、Emission色R/G/B。

根拠: `Source/Engine/Animation/PropertyAnimationClip.h` : `enum class AnimationPropertyTarget`

#### 補間方式（`AnimationCurveInterpolation`）

| 値 | 意味 |
| --- | --- |
| Step | 次のKeyまで値を変えない。ON/OFFや瞬間切替向け。 |
| Linear | 2Key間を一定速度で補間する。 |
| CubicHermite | 入出力接線を使い、滑らかな加減速を作る。 |

#### 書き込みモード（`AnimationPropertyWriteMode`）

| 値 | 意味 |
| --- | --- |
| Override | カーブ値で元の値を置き換える。 |
| Additive | Play開始時の値へカーブ値を加える。 |
| Multiply | Play開始時の値へカーブ値を掛ける。 |

#### Record（自動記録）の仕組み

`BeginRecording` でPreview姿勢を表示し、選択GameObjectの現在値を基準値として記録する。以後 `UpdateRecording` が毎Frame値を比較し、変化したPropertyだけを現在時刻のKeyとして自動追加する（`AddOrUpdateKey`）。「現在Transformを一括記録」ボタン（`RecordCurrentTransform`）は位置・回転・スケールを現在時刻へまとめてKey化する。

#### Preview

`BeginPreview` で選択GameObjectを丸ごとBackupしてからPreview値を適用し、`RestorePreview` でBackupへ戻す。Preview中はSceneView上のGameObjectが実際にClipの値で動くため、Play中でなくても見た目を確認できる。`isPreviewPlaying_` がtrueの間はTimelineが実時間で進む。

#### Play中の違い

Play中は物理・Script・Animatorが同じGameObjectを同時に書き換える可能性があるため、Preview編集とPlay実行を同時に行うと競合する。編集はPlay停止中に行うことを前提とする。

#### 保存

`SaveAnimationClip` が編集中Clipを `animationClipPath_` （選択中の `.animclip` パス）へ書き戻す。`AssignClipToSelectedGameObject` は選択GameObjectへ `Animation` Componentを追加/更新し、ClipのAssetPathを設定する。

#### 確認手順

1. Cubeを作成し `Animation` Componentを追加。
2. アニメーションウィンドウでPosition Y のTrackを追加。
3. 0秒と1秒にKeyframeを打ち、値を変える。
4. Previewを再生し、Cubeが上下することを確認する。

---

### 1-2. Animator Graph タブ(2026-09-12追加)

- 目的: State Machine / Blend Treeを保存する `.animgraph` アセットを、アニメーションウィンドウの「Animator Graph」タブからEditorだけで作成・編集できるようにする。追加前は `AnimationGraph::LoadFromJson` のみで `SaveToJson` が存在せず、`.animgraph` はJSON手書きでしか作れなかった。
- 開き方: メニュー「ウィンドウ > アニメーション」を開き、「Animator Graph (.animgraph)」タブを選ぶ。
- 前提条件: 対象GameObjectに `Animator` Componentが必要(無ければ「選択オブジェクトへ設定」操作時に自動追加される)。Graph保存先は `Assets/Animation/` を想定。
- 実装: `Source/Engine/Editor/EditorAnimationWindowManager.h/.cpp`(`DrawAnimatorGraphTab` 以下)、Graph実体は `Source/Engine/Animation/AnimationGraph.h/.cpp`。

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| ツールバー | 新規Graph作成、保存、再読込、選択オブジェクトへ設定。Play中は実行中State名を表示する(`DrawGraphToolbar`)。 |
| Parameter一覧 | Float/Int/Bool/Trigger/Vector2/Vector3の追加・削除・名前・既定値編集(`DrawGraphParameterList`)。 |
| State一覧 | State追加・削除・選択・Entry State指定。Blend Tree種別(単一Clip/1D/2D方向/2D座標/Direct)ごとにアイコン相当の説明を行内表示する(`DrawGraphStateList`)。 |
| State編集 | 選択StateのClip番号(Model実Clip名付きCombo)、再生速度、ループ、Blend Tree設定、Blend Sample(位置・速度・Weightパラメータ)の追加/削除(`DrawGraphStateEditor`)。 |
| Transition一覧・編集 | 遷移元(`Any State`選択可)・遷移先・Cross Fade秒(`blendDuration`)・Exit Time・割り込み可否・複数遷移条件(パラメータ/演算子/しきい値)の追加/削除(`DrawGraphTransitionList` / `DrawGraphTransitionEditor`)。 |
| Animation Event | 指定Clip・時刻でC++ Script / Effectへ通知するEventの追加・編集(`DrawGraphEventList`)。Graph全体に属し、State単位ではない。 |

#### Clip番号の扱い

State / Sample / EventのClip番号は、選択GameObjectが参照するModel(ModelRenderer→SkinnedMeshRenderer→MeshFilterの優先順)から実際のAnimation Clip名を取得し、Combo形式で選ばせる(`RefreshGraphClipNames`)。Model未設定またはClip番号がModelの範囲外の場合は、数値入力のみ許可した上で警告テキストを表示する(実行時はPoseなしとして安全に無視される)。

#### Any State遷移

`AnimationGraphTransition::sourceState` に負値を設定すると、Runtime(`EditorAnimationManager::EvaluateStateMachine`)は「どのStateからでも遷移可」として扱う。Editorの遷移元Comboでは「Any State」を選択肢として表示する。攻撃・被弾など、現在Stateに関わらず割り込みたい遷移に使う。

#### 保存

`SaveAnimationGraph` が編集中Graphを `animationGraphPath_`(選択中の `.animgraph` パス)へ書き戻す。State 0個のGraphは `LoadFromJson` 側が読み直せなくなるため、`SaveToJson` は保存前に拒否する。`AssignGraphToSelectedGameObject` は選択GameObjectへ `Animator` Componentを追加/更新し、GraphのAssetPathを設定する。

#### 確認手順

1. GameObjectへ `Animator` Componentを追加(Model / SkinnedMeshRendererを持つものが望ましい)。
2. アニメーションウィンドウ「Animator Graph」タブで「新規Graph」を作成し、「選択オブジェクトへ設定」を押す。
3. State を1つ追加し、Clip番号をComboから選ぶ。
4. Transitionを追加し、遷移元を「Any State」、条件にTrigger Parameterを設定する。
5. 保存後、Playして対象Parameterを操作し、遷移することを確認する。

---

### 2. Spline Editor ウィンドウ

- 目的: `RailMovement` が参照するSpline（制御点の階層）を、上面(XZ)/側面(ZY)の2D投影キャンバスでマウス編集する。
- 開き方: メニュー「ウィンドウ > Spline Editor」。
- 前提条件: 2点以上の子GameObject（制御点）を持つ「Path」GameObjectが対象。名前に"Path"を含む、または子に"Point"で始まる名前が2つ以上あると自動的にSpline候補として認識される（`IsRailPathCandidate`）。
- 実装: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawSplineEditor`。

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| Spline選択Combo | Scene内のSpline候補から選ぶ。選択中GameObjectに `RailMovement` があれば自動追従する。 |
| 「新規Spline」ボタン | 4点を持つ標準SplineをSceneへ追加する（`CreateSpline`）。 |
| 「選択点の次へ追加」ボタン | 選択Rail末尾へ制御点を1つ追加する（`AddControlPoint`）。 |
| 上面XZ / 側面ZY ラジオボタン | キャンバスの投影軸を切り替える（`showsSideView_`）。 |
| Play Preview（Play中のみ表示） | 進行率スライダー、停止/再開ボタン、順方向/逆方向ボタンで実際のRail走行位置を操作できる。`EditorRailMovementManager::GetNormalizedProgress` / `SetNormalizedProgress` / `SetPaused` / `SetReverse` を呼ぶ。 |
| 制御点リスト（左） | 各制御点を選択、位置をDragFloat3で直接編集、「選択点を削除」（3点以上残る場合のみ）。 |
| 2Dキャンバス（右） | 制御点を円で表示し、ドラッグで移動できる。水色の線はCatmull-Romスプライン補間したプレビュー経路。 |

#### プレビュー曲線の計算

`railUseSmoothCurve` がtrueならCatmull-Rom補間（`EvaluateSplineCatmullRom`、1区間16サンプル）、falseなら制御点間を直線でつなぐ。`railLoop` がtrueなら始点と終点をつないだループとして補間する。

#### Play中の違い

Play中は「Play Preview」欄が追加表示され、実際に走行中のRail進行を確認・操作できる。制御点のドラッグ編集自体はPlay中も可能だが、Rail追従中のFollowerの挙動に即座に反映される。

#### 保存

制御点はGameObjectのTransform（`translate`）として保存されるため、通常のScene保存でそのまま保存される。専用のSpline Asset形式は無い。

#### 確認手順

1. メニューから「新規Spline」を押す。
2. 制御点をキャンバス上でドラッグして経路を変える。
3. `RailMovement` を持つGameObjectを作り、`railPathGameObjectId` にこのSplineを設定してPlay。
4. 意図した経路を通ることを確認する。

---

### 3. Event Timeline ウィンドウ

- 目的: 「経過秒」または「Rail進行率」のどちらかの軸上に、名前付きScript Action（`TimelineEvent` Component）とWave開始（`WaveSpawner` の `waveTriggerMode=1`）をマーカーとして配置・編集する。
- 開き方: メニュー「ウィンドウ > Event Timeline」。
- 重要な設計原則: **このウィンドウ自体はゲームルールを実行しない。** 時刻/進行率と名前付きActionを結び付けるだけで、実際に何が起きるか（攻撃、演出、BGM切替等）は、そのAction名を受け取るC++ Script側が決める。

根拠: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawEventTimeline` 内コメント「Event Timelineは時刻またはRail進行率と名前付きScript Actionだけを接続します。ゲームルールは実行しません。」

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| 表示軸Combo | 「経過秒」または「Rail進行率」を選ぶ（`timelineSourceMode_`）。 |
| 表示時間 | 経過秒モード時のみ。Timeline全体の長さ（秒）（`timelineDurationSeconds_`、1〜3600秒）。 |
| 「Eventを追加」ボタン | 現在の表示軸・選択GameObjectを使う `TimelineEvent` Componentを新規GameObjectとして追加する（`CreateTimelineEvent`）。 |
| Wave雛形関連 | 「選択ObjectをWave雛形にする」→選択GameObjectを複製元に指定。Wave個数（1〜64）、初期配置（横列/V字/円/グリッド）、配置間隔を設定し「Waveを作成」でPool付きWaveSpawnerを一括生成する（`CreateWaveFromSelection`）。 |
| Eventsキャンバス | 横軸が時刻(秒)または進行率(%)。各行が1つのEvent/Wave。水色マーカー=名前付きEvent、橙色マーカー=Wave開始。マーカーをドラッグすると発火時刻/進行率が変わる。 |

#### マーカーのドラッグ

マウスでマーカーをクリック＆ドラッグすると、`timelineComponent->timelineTriggerValue`（経過秒モードなら秒、進行率モードなら0〜1）または `waveComponent->waveTriggerValue`（0〜1固定）が直接書き換わる。

#### Play中の違い

編集内容はPlay開始後に評価される。Timelineウィンドウ自体はPlay中の進行位置を表示するプレイヘッド機能を持たない（現在時刻/進行率のインジケータ描画は無い。実行状況はGame ViewかLog監視で確認する）。

#### 保存

`TimelineEvent` と `WaveSpawner` はいずれも通常のComponentなので、Scene保存に含まれる。

#### 確認手順

1. 表示軸を「経過秒」にし、「Eventを追加」。
2. マーカーを5秒地点へドラッグ。
3. 対応するC++ Scriptに同名のAction関数を実装してPlay。
4. 5秒後にAction関数が呼ばれることを確認する。

---

### 4. State Graph ウィンドウ

- 目的: 1つの数値（Health比率またはRail進行率）を3段階の「State」に分け、State切替時に名前付きScript Actionを通知する `ThresholdState` Componentを、フローチャート風のUIで編集する。
- 開き方: メニュー「ウィンドウ > State Graph」。
- 重要な設計原則: Event Timelineと同様、**「Boss」「敵」「攻撃」等のゲーム固有概念はGraph側に無い。** 各Actionの意味は受信側C++ Scriptが決める。

根拠: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawStateGraph` 内コメント「各Actionの意味は受信するC++ Scriptが決めます。Boss、敵、攻撃などの固有概念はGraph側にありません。」

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| State Graph選択Combo | `ThresholdState` を持つGameObjectから選ぶ。 |
| 「選択ObjectへThreshold Stateを追加」ボタン | 選択中GameObjectに未追加なら `ThresholdState` Componentを追加する。 |
| 値Source Combo | 「Health比率」または「Rail進行率」（`thresholdSourceMode`）。 |
| State 2 境界 / State 3 境界 | 0.0〜1.0のDragFloat。値がこの境界を超えるとStateが切り替わる（`thresholdSecondValue` / `thresholdThirdValue`）。 |
| State 1/2/3 Action | 各Stateに入った時に通知するAction名の文字列入力。 |
| フローチャート表示 | State1→State2→State3を矢印付きノードで表示。ノード内にAction名を表示する。 |

#### 遷移方向の説明文

`thresholdSourceMode==0`（Health比率）なら「値が低下するとState 1 -> 2 -> 3」、`thresholdSourceMode==1`（Rail進行率）なら「値が上昇するとState 1 -> 2 -> 3」という説明文がGraph上部に表示される。

#### 保存

`ThresholdState` は通常のComponentとしてSceneへ保存される。

#### 確認手順

1. Healthを持つGameObjectを選び「Threshold Stateを追加」。
2. State 2 境界=0.6、State 3 境界=0.3等に設定。
3. 各StateのActionへ異なる名前を設定し、対応するC++ Scriptハンドラを実装。
4. Play中にダメージを与え、Health比率が境界を超えるたびに対応するActionが呼ばれることを確認する。

---

### 5. 診断・Profiler ウィンドウ

- 目的: 実行時の負荷計測、VFX状態、Scene静的検査、Replay記録・再生、GBuffer各種可視化、Auto Exposure Histogram、Render Graph構成確認を1つのタブ付きウィンドウにまとめる。
- 開き方: メニュー「ウィンドウ > 診断・Profiler」。
- 実装: `Source/Engine/Editor/EditorDiagnosticsWindowManager.h/.cpp`。9個のタブを持つ。

根拠: `Source/Engine/Editor/EditorDiagnosticsWindowManager.cpp` : `BeginTabItem("Profiler")` 以降9タブ分。

#### 5-1. Profiler タブ

| 要素 | 説明 |
| --- | --- |
| CPU Frame / GPU Frame | 画面上部に常時表示。CPU Frameは直近値・平均値・平均から算出したFPSを表示する（`EditorProfilerManager::GetLastCpuFrameMilliseconds` / `GetAverageCpuFrameMilliseconds`）。 |
| Frame Historyグラフ（2026-09-12追加） | 直近240Frame分のCPU/GPU時間を`ImGui::PlotLines`で時系列表示する。集計値では潰れる単発スパイクの発生時点を見つけるためのもの（`EditorProfilerManager::PushFrameHistory` / `GetFrameHistory`、リングバッファ）。 |
| Runtime Stats（2026-09-12追加、既定で開いた状態） | Objects/Instances数、VRAM使用量/予算(MB)、Play中のみ: Audio Voice(現在/上限/読込済Clip数)・Physics Body数・VFX(Effect/Emitter/Particle数)、Asset Registry登録数、計測中のDraw Call/Dispatch/Alloc合計、直近の物理Body生成失敗を1画面にまとめる。「重い原因・増え続けるResource・壊れたScene」を、下の手動計測を開始しなくても大まかに特定できるようにする。 |
| 計測時間（秒）入力 | 手動計測の長さを指定（`EditorProfilerManager::SetMeasurementDurationSeconds`）。 |
| 「Editor / Play負荷計測を開始」ボタン | Edit中・Play中のどちらでも計測を開始する（`SetEnabled(true)`）。計測中は進捗バーと「計測を停止」ボタンに切り替わる。 |
| 「結果を消去」ボタン | `Reset()`。 |
| 「結果をコピー」ボタン | 現在結果を13列CSVへ整形してClipboardへコピーする。 |
| 「ファイルに保存…」ボタン | Save Dialogを開き、UTF-8 BOM付きCSVを保存する。既定名は`ProfilerResult.csv`で、拡張子未指定なら`.csv`を補う。 |
| 重い判定(平均ms) | 0以上。既定1.0ms。平均時間がしきい値以上の行を赤く強調する。 |
| 「呼出階層で表示」チェック | ONだとThread→GameObject→呼出Pathの順で階層ソートし、インデントで表示する。 |
| 結果テーブル（13列） | イベント名、処理元、GameObject、Thread、呼出回数、合計ms、平均ms、Self ms、最大ms、DrawCall、Dispatch、Alloc回数、Alloc KB。EngineだけでなくNative Script DLL側も同じ期間で集計する。 |

CSVはComma、引用符、改行を含むTextを二重引用符でEscapeする。GameObjectは`名前 (ID)`、削除済みなら削除済み表示になる。階層表示OFF時は数値列をHeader ClickでSortでき、初期Sortは呼出回数の降順。階層表示ON時は呼出関係を崩さないため列Sortを無効にする。Editor Update/DrawのSubsystem、Play Runtime、RendererのGPU Particle Update/Draw Eventも計測範囲に含まれるが、結果が空の場合は計測時間と対象処理が実際に通ったかを確認する。

#### Profilerの使い方: 重いFrameを特定する

1. Playし、再現したい場面（敵の大量出現、海面、Particleなど）を動かす。
2. まずFrame HistoryでCPU/GPUどちらが跳ねたかを見る。単発の山は平均値だけでは見つけられない。
3. 「Editor / Play負荷計測を開始」を押し、スパイクが起きる操作を行ってから停止する。
4. 結果テーブルを`Self ms`または`最大ms`で並べ、原因候補を絞る。親の合計時間ではなく、その処理自身の負荷を見たい時は`Self ms`を使う。
5. Runtime Statsで同時に増えている値を確認する。Particle数、Audio Voice、Physics Body、VRAM、Draw/Dispatchが増え続けていれば、時間だけでなくリークまたは生成過多を疑う。
6. 結果をCSVへ保存またはコピーし、修正前後は同じScene・同じ操作・同じ計測秒数で比較する。

Frame HistoryとRuntime Statsは原因候補を見つけるための表示であり、最適化の成功判定は同条件で再計測して行う。全体Heap使用量は表示しないため、VRAMやAllocが安定していてもOS全体のメモリ増加までは断定できない。

#### 5-2. VFX タブ

Play中のみ内容を表示する。Stage1 VFX（Billboard/Flipbook/Ribbon/Ring）の状態を表示する。

| 表示項目 | 内容 |
| --- | --- |
| Active Effect数 / Active Emitter(Node)数 / Particle数 | `EditorVfxManager::GetDebugStats()` から取得。 |
| Effect Pool使用数 | 使用中 / 総容量。 |
| Effect一覧テーブル | Effect名、Node数、Particle数、描画方式、LOD Spawn倍率 / 追従有無。 |

#### 5-3. Scene Validator タブ

Sceneの静的整合性を検査する（実処理確認ではなく構造チェック）。

| 検査項目 | 検出条件 |
| --- | --- |
| GameObject ID重複 | Error |
| 親GameObjectがScene内に存在しない | Error |
| 子一覧と親IDの不一致 | Error |
| Dynamic RigidbodyがMesh Colliderを使用 | Warning（Auto Convexまたは単純Colliderを推奨） |
| Active Cameraが0台 | Error（Game ViewはScene Cameraへフォールバック） |
| Active Cameraが4台超 | Warning |
| Active Lightが8個超 | Warning |
| Particle最大数合計が100000超 | Warning |
| 大量WaveがObjectPool方式でない（`waveSpawnCount>32` かつ `waveSpawnSourceMode!=0`） | Warning |
| Wave生成予定数合計が500体超 | Warning |

「今すぐ検査」ボタンで手動実行、「自動検査」チェックでON時は定期的に自動実行される（`shouldAutoValidate_`）。問題行をクリックすると該当GameObjectを選択できる。

#### 5-4. Replay タブ

Keyboard 256キーと各FrameのdeltaTimeを記録・再生する。

| 操作 | 内容 |
| --- | --- |
| 「Scene先頭から記録」 | Play中なら一度停止し、記録開始とともに再度Playする。 |
| 「記録停止・保存」 | `runtime_cache/replays/last.cgreplay` へ保存。 |
| 「Scene先頭から再生」 | 記録済みFrameがある場合のみ表示。 |
| 「再生停止」 | Playback中のみ表示。 |
| 「前回Replayを読込」 | 保存済みFileを読み込む。 |

状態表示: 停止/記録中/再生中、現在Frame/総Frame数。

#### 5-5. 描画バッファ タブ

GBufferの各チャンネルを画面全体でプレビューする。表示Comboで Albedo / Normal / Material（R=Roughness G=Metallic B=AO A=F0）/ Emission（EmissionとTransmission）/ Motion Vector（RG成分）を切り替える。

#### 5-6. Before / After タブ

HDR入力（PostProcess適用前）と最終合成（PostProcess適用後）を左右に並べて比較する。

#### 5-7. Material Preview タブ

Base Color / World Normal / Roughness・Metallic・AO / Emission・Transmission の4チャンネルを2x2で並べて表示する。

#### 5-8. Scopes タブ

Auto Exposure実行後の輝度Log Histogram（-12EV〜+8EV）をヒストグラム表示する。Auto Exposure未実行時は「Auto Exposure実行後に輝度Histogramを表示します。」と表示される。

#### 5-9. Render Graph タブ

主要9 Passの入出力とPSO準備状態（Ready / Missing）を一覧表示する（GBuffer、GTAO、SSGI、SSR、Ocean、Volumetric Cloud、Bloom / Glare、Final Composite、AA / Filter）。実際のPass順序の説明であり、Passを個別に無効化する機能ではない。

#### 確認手順

Play開始→「負荷イベント計測を開始」→数秒待って「計測を停止」→結果テーブルで重いイベントを確認、という流れが基本。

---

### 6. ログ監視 ウィンドウ

- 目的: GameObject / Component / System の任意の値を選んで監視し、`logs/RuntimeLog.log`（pipe区切り、機械可読）へ記録する。Console(下部パネル)のように流れて消えるログとは別に、後から数値を追って調べ直せるようにする診断機構。
- 開き方: メニュー「ウィンドウ > ログ監視」。
- 実装: `Source/Engine/Editor/EditorLogMonitorManager.h/.cpp`（監視ロジック本体）、`EditorLogMonitorWindowManager.h/.cpp`（UI）。

#### タブ構成

| タブ | 内容 |
| --- | --- |
| オブジェクト / コンポーネント | 選択GameObjectのField（位置/回転/スケール等）とComponent各値をチェックボックスで監視対象へ追加。Componentごとに折りたたみ（既定は閉じた状態）で、Inspectorと同じ日本語名で表示される。 |
| システム | Manager/Global State由来の値（Weapon、Physics、Rendering、GameState、Profiler(CPU ms)等）をカテゴリ別に折りたたみ表示（既定は閉じた状態）。日本語名で表示される。 |
| 監視中一覧 | 現在登録されている全Watch Entryをテーブルで一覧し、有効/無効、種別、カテゴリ、対象、状態を確認・編集する。 |

#### 監視対象の3種類

| 種別 | 説明 |
| --- | --- |
| GameObjectField | `id`/`name`/`isActive`/`translate`/`rotate`/`scale`。登録不要で全GameObjectへ列挙できる。 |
| ComponentField | `EditorLogFieldRegistry.generated.h` 経由。Inspector描画コードから自動生成されたFieldのみ選べる。`(componentType, componentFieldKey)` という安定した文字列識別子で保存する（Registry再生成で並びが変わっても過去のWatch設定が壊れない）。 |
| SystemField | `EditorLogSystemProviders.h` 経由。Manager/Global State由来。 |

#### 記録モード（`LogCaptureMode`）

| 値 | 意味 |
| --- | --- |
| EveryFrame | 毎Frame記録する。 |
| IntervalSeconds | 指定秒間隔で記録する。 |
| OnChange（既定） | 値が変化した時だけ記録する。負荷を抑えるための既定値。 |
| IntervalFrames | 指定Frame数ごとに記録する。 |
| Manual | 自動発火しない。手動記録ボタン押下時だけ記録する。 |

Float/Vector3のOnChangeには「変化Threshold」があり、`abs(現在値-前回値) >= threshold` の時だけ記録する（Vector3全体はベクトル距離で判定）。Bool/Int/GameObject参照は常に完全一致比較。

#### RuntimeLogの形式

```text
Timestamp|Frame|Category|TargetKind|SourceId|SourceName|Component|Field|Value
```

Playを開始するたびに空になる。値が変化した時だけ行が増える（EveryFrame以外）ため、ある時刻の全項目を見るには近い時刻の複数行をまとめて読む必要がある。

#### 安全設計

- GameObject解決は `EditorScene::FindGameObject` のO(1) Hash Map引きを使い、毎Frame全Scene走査はしない。
- Component側はEntryごとにRegistry index / Component slot indexをcacheし、型が一致する限り再探索しない。
- 対象が見つからない場合はMissing状態への遷移/復帰の時だけ1回記録し、毎Frame大量記録しない。
- Buffer上限、1Frame最大件数、File Size上限を超えた場合はDrop件数をログへ記録する（黙って落とさない）。

#### 確認手順

1. 敵GameObjectのHealth Componentを選び、「HP」等のFieldにチェック。
2. Playして敵を攻撃。
3. `logs/RuntimeLog.log` を開き、値が変化した行が記録されていることを確認する。

---

### 7. 共同制作 ウィンドウ

- 目的: 複数人が同じProjectをネットワーク経由で同時編集するための、Scene変更同期・Asset変更同期・選択ロック・競合解決を行う。
- 開き方: メニュー「ウィンドウ > 共同制作」。
- 実装: `Source/Engine/Editor/EditorTeamCollaborationManager.h/.cpp`。ウィンドウタイトルは「TEAM - 共同制作」。

#### 状態（`EditorTeamConnectionStatus`）

Offline / Connecting / Online / Synchronizing / Conflict / Disconnected の6状態。

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| Status / Revision / Members / Unsynced Changes / Conflicts / Editing Locks | 現在の接続状態と同期状況の数値表示。 |
| Membersリスト | 参加中ユーザー名とOnline/Offline状態を箇条書き表示。 |
| User Name / Host / Port 入力 | 接続設定。既定Port 45678。 |
| 「このPCをHostにする」チェック | ONなら「サーバー開始」ボタン、OFFなら「Hostへ接続」ボタンが表示される。 |
| 「Start Team Server Automatically」チェック | 起動時に自動でサーバーを開始するか。 |
| 「設定保存」ボタン | ユーザー名・Host・Port等をFileへ保存する（`SaveSettings`）。 |
| Conflicts欄（競合発生時のみ表示） | 競合したユーザー名・Scene・操作・Property・保存された競合コピーのFileパスを表示。「自分側を採用」「相手側を採用」「両方をMerge」（C++ Scriptアセットのみ）ボタンで解決する。 |

#### 同期の仕組み（概要）

- Scene変更は `EditorTeamChangeEvent`（operation、property、oldValue、newValue、Scene全体のSnapshotデータ等を含む）としてキャプチャされ配信される（`CaptureSceneChanges`）。
- Asset変更は個別にScan・Hash比較され、変更があれば配信される（`ScanAssetChanges`）。
- 選択中GameObjectは他ユーザーへロック通知され、他ユーザーが同じObjectを編集しようとするとロック中である旨が分かる（`UpdateSelectionLock`、`IsGameObjectLockedByAnotherUser`）。
- 競合発生時は変更を即座に上書きせず、競合コピーを別ディレクトリへ保存してから使用者に選ばせる（`PrepareConflictCopies`）。

#### 制限・注意

詳細仕様は本書を参照する。現在の実装には通信暗号化、User認証、権限Role、Host Migrationがない。信頼できるLAN/VPN内だけで使用し、InternetへPortを直接公開しない。Host 1台へ接続できるRemoteは最大2台、同期Assetは生File 128 MiBまでである。

#### 確認手順

1. PC Aで「このPCをHostにする」をON→「サーバー開始」。
2. PC Bで同じPort・PC AのIPを指定して「Hostへ接続」。
3. PC AでGameObjectを1つ動かし、PC Bへ反映されることを確認する。
4. 同じComponent Propertyを同時編集し、Component単位Lockと競合Copyが働くことを確認する。
5. 512 KiB超のAssetでChunk転送進捗を確認し、削除Assetが`.team/trash/Revision_N`へ退避されることを確認する。

---

### 8. 描画負荷テスト Scene を作成

- 目的: 描画負荷（Ocean、Terrain、Foliage、不透明、半透明OIT、屈折、Skinned Mesh、Particle衝突）を一括生成し、Profiler計測の基準Sceneを作る。
- 開き方: メニュー「ウィンドウ > 描画負荷テスト Scene を作成」→確認Popup「作成する」。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `CreateRenderStressScene`。

#### 重要な注意

**現在の未保存変更は破棄され、`Assets/Scenes/RenderStress.scene` へ上書き保存される。** 実行前に必要なら現在のSceneを保存しておくこと。実行前にPlay中なら自動的に停止する。

#### 生成される内容

| 要素 | 数量・設定 |
| --- | --- |
| PostProcess | AA Mode=3、SSR有効、Bloom強度0.65。 |
| Ocean | Grid解像度2048、サイズ320。 |
| Terrain | Heightmap `resources/model/huzisann.png`、Collider 260x28x260。 |
| Foliage | Grid Texture `resources/editorDefault/sibahu.png`、Particle最大8192、半径170。 |
| 不透明Object | ICOCube 48体、4行12列グリッド配置、Y軸回転あり。 |
| 半透明OIT Object | Box 32体、AlphaMode=Transparent、Alpha 0.34。 |
| 屈折Object | ICOCube 12体、Transmission 0.92、IOR 1.52、Roughness 0.06。 |
| Skinned Mesh | `Assets/ai.fbx` 4体、Animation + Animator付き。 |
| Particle | Depth衝突・SDF衝突の2系統、各8192個上限、Rate 1800/秒。 |

#### 確認手順

1. 実行後、Playして診断・ProfilerウィンドウのProfilerタブで計測。
2. GPU Frame msの内訳から重いPassを特定する。

---

### 9. ゲーム基盤検証 Scene を作成

- 目的: Prefab階層・Rail/Branch/Wave・照準/武器/Pool/被弾・汎用Sequence・非同期加算Scene・Checkpointという、汎用ゲーム基盤一式が動作することを1つのSceneで検証する。
- 開き方: メニュー「ウィンドウ > ゲーム基盤検証 Scene を作成」→確認Popup「作成する」。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `CreateGameplayFoundationValidationScene`。

#### 重要な注意

**現在の未保存変更は破棄され、`Assets/Scenes/GameplayFoundationValidation.scene` へ上書き保存される。** 実行前にPlay中なら自動的に停止する。

#### 生成される内容

| 分類 | 内容 |
| --- | --- |
| Input Actions | `Assets/GameplayFoundationValidation.inputactions` を新規作成。 |
| 加算Scene | `Assets/Scenes/GameplayFoundationAdditive.scene`（`Saveable` Componentを持つマーカーのみ）を別途保存。 |
| Prefab | Box+子を持つ階層をPrefab保存し、通常InstanceとVariant相当のInstanceを両方配置。 |
| Rail / Branch | Rail Path A（4点）とRail Path B（3点）、`RailMovement`+`RailBranch` を持つFollowerが進行率0.55でBへ分岐。 |
| Wave | Health付きTemplate、ObjectPool（初期3）、`WaveSpawner`（V字/グリッド系フォーメーション、Follower進行率0.25でTrigger）。 |
| Damage対象 | Box Collider + Health + DamageReceiver + Saveable（初期非Active）。 |
| 照準/武器 | PlayerInput + ScreenAim + HitscanWeapon + ProjectileEmitter を持つ検証用GameObject。Projectile ObjectPool（初期16、拡張可）。 |
| PrefabSpawner | Poolから一定間隔で生成するSpawner。 |
| Action Sequence | 待機0.5秒→対象Active化→加算Scene読込、の3ステップ。 |
| Checkpoint | Slot名 `gameplay_foundation_validation`、Start時保存。 |

#### 確認手順

1. 実行後Play。
2. Rail Followerが分岐点で正しくPath Bへ移るか確認。
3. Wave発火でPoolから敵が湧くか確認。
4. マウスクリックで照準先へProjectile/Hitscanが飛ぶか確認。
5. 一定時間後にAction Sequenceが加算Sceneを読み込むか確認。

---

### 10. Console 表示

- 目的: 各Managerが積んだログメッセージ（物理エラー、Asset読込結果、保存結果など）を表示する下部パネルの「Console」タブを表示状態にする。
- 開き方: メニュー「ウィンドウ > Console 表示」。実体は下部パネル（`EditorBottomPanel`）内の1タブで、「Project」タブと並んでいる。
- 実装: `Source/Engine/Editor/EditorBottomPanel.cpp` : `BeginTabItem("Console")`。

#### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| 「ログ消去」ボタン | `consoleMessages` 配列を空にし、`isConsoleCleared=true` にする（以後は非表示状態になる）。 |
| 「ログ表示」ボタン | `isConsoleCleared=false` にして再表示する。メニューの「Console 表示」も同じ効果。 |
| Scene / Play 状態表示 | 現在のSceneViewサイズと、Playing/Stoppedの状態を表示。 |
| メッセージ一覧 | 各Managerが `consoleMessages.push_back(...)` で積んだ文字列を、折り返し表示（`TextWrapped`）で古い順に列挙する。 |

#### 注意

Consoleは全Managerの出力が同じ配列に時系列で混ざって流れるため、特定の値を追いたい場合は本ドキュメントの「6. ログ監視」の方が向く（物理Body生成の失敗理由などはConsoleとRuntimeLogの両方へ出す設計になっている箇所がある）。

---

### 11. 選択解除

- 目的: 現在選択中のGameObjectをすべて解除する。
- 開き方: メニュー「ウィンドウ > 選択解除」。
- 実装: `ClearSelectedGameObjects()` を呼ぶだけ。Inspectorは選択GameObjectが無くなるため、Project設定画面（本ドキュメント「12. 設定」参照）へ自動的に切り替わる。

---

### 12. レイアウト再構築

- 目的: Dockingレイアウト（各Windowの配置・サイズ）を既定状態へ戻す。
- 開き方: メニュー「ウィンドウ > レイアウト再構築」。
- 実装: `g_isDockLayoutInitialized = false` にするだけで、その場では画面が変わらない。Consoleへ「Window: レイアウト再構築は次回起動時に反映されます」と表示される。
- 注意: **即座には反映されない。** 次回Editor起動時に既定Dockレイアウトが再構築される。ウィンドウ配置を壊してしまった場合の復旧手段として使う。

---

### 13. 設定（Project設定 / メニュー「編集 > 設定を開く」）

- 目的: 特定のGameObjectに紐付かない、Project全体の設定（環境光、物理、操作ツール、Sceneカメラ操作感度等）をInspectorへ表示する。
- 開き方: メニュー「編集 > 設定を開く」、または「ウィンドウ > 選択解除」で選択GameObjectが無い状態にする。実体はGameObject未選択時のInspector描画内容そのもの。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `OpenProjectSettings`（選択解除するだけ）、`Source/Engine/Editor/EditorInspectorPanel.cpp` : 各 `Draw*Panel` 関数群。

#### 13-0. プロジェクト設定（2026-09-12追加）

GameObjectを選択していない時のInspector、またはメニュー「編集 > 設定を開く」から表示する。保存先は`ProjectSettings/ProjectSettings.cg2`で、UTF-8 BOM + CRLFで書き出す。

| 項目 | Runtimeへの接続 | 反映タイミング |
| --- | --- | --- |
| 解像度 | `WinApp`のWindow作成サイズ | 次回起動時 |
| Window Mode | ボーダーレス全画面の有効/無効 | 次回起動時 |
| VSync | `EditorRenderManager`の`Present`同期interval | 即時 |
| FPS上限 | `GameScene`フレーム末尾のLimiter | 即時 |
| Audio 6系統音量 | Play開始時のAudio Bus初期値 | 次のPlay開始時 |
| Gamepad Dead Zone / 感度 / Triggerしきい値 | `GamepadInput`の入力正規化 | 即時 |
| Haptics 振動の強さ | `HapticSystem`のMaster Intensity | 次のPlay開始時。Debug Windowでは実行中に変更可 |
| Online Services 有効 / Provider / Environment | `OnlineService`の有効化と接続先選択 | 次のPlay開始時 |
| Online本番/開発URL、Game ID、Client Key | Worker接続HeaderとRequest Scope | 次のPlay開始時 |
| Online Timeout / 再送Queue上限 | WinHTTP Timeoutと平文Pending Queue上限 | 次のPlay開始時 |
| 起動Scene / Build Scene一覧 | `GameBuildSettings.cg2`と共有 | 保存後のBuild / Standalone起動時 |

解像度`1280x720`を設定して再起動し、実クライアント領域が`1280x720`になることを測定済みである。Audio音量とFPS上限の体感確認、実機Gamepadでの確認は別途必要とする。

OnlineのClient Keyは配布Gameから読める公開値だけを入れる。管理鍵、Cloudflare Token、Database資格情報はWorker側のSecretへ置く。EnvironmentをProductionへ切り替える前にURLだけでなくD1/KV/R2も本番用へ分離する。

#### 13-1. オブジェクト操作

選択中GameObjectがある時のInspectorにも同名の折りたたみが表示される。「複製」「削除」「Undo」「Redo」「Scene保存」「Scene読込」、Prefab関連（「Prefabとして保存」「Variantとして保存」「PrefabをSceneへ生成」「Prefabへ反映」「Prefabへ戻す」）のボタン群。

#### 13-2. 環境 / 背景

| 項目 | 説明 |
| --- | --- |
| 背景色 | SceneViewのClear Color（`ColorEdit4`）。 |
| 環境画像を使う | 天球にHDR/DDS/PNG/JPG画像を使うか。ONの場合、選択中Assetが対応拡張子なら「選択中アセットを環境画像に設定」ボタンが使える。「環境画像を解除」で無効化。 |
| 環境画像の強さ / 回転 / 粗さ補正 | 環境画像のIntensity、Y軸回転（ラジアン）、Mip Bias。 |
| 天球上色 / 天球下色 | 環境画像未使用時の疑似スカイのグラデーション色。 |
| 天球明るさ / 天球放射 / 環境光 / 反射寄与 / 空の切替 | スカイの明るさ、Emission、Ambient強度、反射への寄与、地平線のシャープさ。 |
| ギズモ表示 / ライトアイコン / カメラアイコン | SceneView上の補助アイコン表示切替。 |

#### 13-3. 物理設定

| 項目 | 説明 |
| --- | --- |
| 重力 | Vector3（既定Y負方向）。 |
| 固定更新時間 | `fixedTimeStep`（0.001〜0.1秒）。Jolt Physicsの固定Step幅。 |
| 衝突ステップ | `collisionStepCount`（1〜8）。 |
| デバッグ表示チェック群 | 当たり判定の形 / 接触点・法線 / Ray・ShapeCast / 速度・角速度 / 力・場の向き / 影響範囲・流体領域 / ばね・Joint接続 / 選択中だけ表示、の8種類をSceneView上に描画するか個別に切り替える。 |
| ベクトル表示倍率 | デバッグ矢印の長さ倍率（0.01〜10）。 |
| Layer Collision Matrix | Default / Player / Enemy / Ground / Projectile / Trigger / UI / Ignore Raycast の8レイヤー同士が衝突するかを対称行列で設定する。 |

#### 13-4. モデル / マテリアル

レガシーPreview用の簡易マテリアル設定（ライティングON/OFF、マテリアル色）。

#### 13-5. 操作ツール

| 項目 | 説明 |
| --- | --- |
| ローカル座標 | Gizmoをワールド/ローカルどちらの軸で動かすか。 |
| スナップ | ONでGizmo操作を指定値単位に丸める。 |
| スナップ値 | X/Y/Zそれぞれのスナップ単位。 |
| 移動 / 回転 / 拡縮 / 統合 | Gizmoの操作モードを切り替えるラジオボタン（`activeEditorTool`）。 |
| Scene操作ヘルプ | SceneView操作方法のヘルプ表示切替。 |

#### 13-6. シーンカメラ操作

移動速度、回転感度、ホイール速度、中ボタン移動速度、Shift倍率、をDragFloatで調整する。操作方法ヒント（右ドラッグ=回転、中ドラッグ=平行移動、ホイール=前後、WASD=視点基準移動、Q/E=上下、Shift=高速）も表示される。

#### 13-7. Input Actions（`.inputactions` Asset選択時）

選択中Assetが `.inputactions` の場合のみ表示される専用エディタ。Action一覧をロードし、GUIで編集・保存できる（詳細はC++ Script APIドキュメントの Input Action 節を参照）。1 ActionにはKeyboard、Mouse、`GamepadStick`、`Gamepad`を複数Bindingとして登録でき、いずれかの入力が同じAction値へ統合される。XInputは最大4台、円形Dead Zone、感度、Triggerしきい値をProject Settingsで調整する。未接続または切断済みGamepadは中立値として扱う。Runtime Rebind UIとControl Schemeは未実装である。

#### Gamepad入力の使い方

1. Project Settingsで`Gamepad Dead Zone`、`Look Sensitivity`、`Trigger Threshold`を設定する。Stickが触れていないのに動く場合はDead Zoneを上げ、視点が遅い場合は感度を上げる。
2. Project Windowで`.inputactions`を選び、対象ActionへKeyboard Bindingを残したまま`GamepadStick`または`Gamepad` Bindingを追加する。既存Keyboard操作を消す必要はない。
3. 移動・照準のようなVector2 Actionは`GamepadStick`で`Left`または`Right`を指定する。十字キーをVector2として使う場合は`DPad`を指定する。
4. 決定・攻撃などのButton Actionは`Gamepad`で`A`、`B`、Shoulder、Triggerなどを指定する。TriggerはProject Settingsのしきい値を超えた時にButtonとして発火する。
5. `PlayerInput`のActionsとAction Mapを対象Assetの名前に合わせ、PlayしてKeyboardとGamepadのどちらでも同じActionが届くことを確認する。

接続していないGamepadは1秒間隔で再検出するため、未接続時に毎Frame問い合わせない。Play中に切断すると値は必ず中立へ戻る。実機で抜き差しと、Stickを傾けたまま切断した時に移動が残らないことを確認する。Runtime中に利用者へキー割当を変更させるRebind UIは現時点では提供しない。

#### 13-8. Legacy Preview（選択種別による切替）

`selectedSceneObject` の値によって「モデル プレビュー」「スプライト プレビュー」「平行光源」「デバッグ カメラ」のいずれかを表示する古いプレビュー機構。UV Transform（UVスケール/回転/移動）もモデルプレビュー時のみ表示される。

#### 保存

Project共通の画面・VSync・FPS・Audio・Gamepad設定は`ProjectSettings/ProjectSettings.cg2`へ保存する。起動SceneとBuild Scene一覧は`ProjectSettings/GameBuildSettings.cg2`へ保存する。Scene固有の物理・環境・操作ツール設定はScene保存経路を使うため、Project Settingsと混同しない。Scene保存→再読込での個別値保持は別途確認する。

#### 確認手順

1. 「編集 > 設定を開く」または「選択解除」。
2. 物理設定の「重力」をY=-20等に変更してPlay。
3. 落下速度が変わることを確認する。
4. Scene保存→再読込し、値が保持されているか確認する。

---

### 14. Hook / Wire デバッグ ウィンドウ

根拠: `Source/Engine/Editor/EditorHookWireDebugWindowManager.cpp`、SceneView側は `Source/Engine/Editor/EditorSceneViewManager.cpp` の `DrawHookWireDebug`

Wireパズルでは「どの物体のどこにHookを置いたか」がそのままレベルデザインになる。
「Wireがおかしい」の実体はHook設定のミス（力を伝えるRigidbody違い、Anchorずれ、Collider不足）で
あることが多いため、それをScene編集中に見つけるための検査Window。

#### 画面構成・主な操作

Window上部に「SceneViewへHookの構成を重ねる」チェックボックス（`g_isHookWireSceneGizmoVisible`、既定ON）があり、
その下がタブで分かれる。

**Hook構成タブ** — Scene内の`WireConnectable`を持つGameObjectを列挙する。
設定不備があるHookは見出しへ `[要確認 n]` を出し、既定で開いた状態にする。

| 検出する不備 | 表示される症状 |
| --- | --- |
| Rendererがない | Hookの見た目と状態色を表示できない。 |
| Colliderがない | 狙って選択できない（`FindBestHook`に当たらない）。 |
| 力を伝えるRigidbodyの参照先が見つからない | Wireの力が伝わらない。 |
| 力を伝える先にRigidbodyがない | 引いても動かない（Static扱い）。 |
| 子Hookなのに伝達先がHook自身 | 親の物体ではなくHookへ力が掛かる。 |

各Hookでは親、力を伝えるRigidbody、Hook World位置、Anchorのローカル値、選択可能、最大接続本数を表示する。
`このHookを選択`でHierarchy選択へ移動できる。Play中は現在の接続Wire数も出す。

**Runtime Wireタブ** — Play中のみ内容を表示する。Wire Handle、両端のHook名、現在長、
最小長、現在の上限長、張力、破断張力、収縮速度を出し、破断・非Activeも表示する。

#### SceneViewギズモ

Anchorの実World位置（`wireConnectableLocalAnchor`をHookのWorld行列で変換した位置）に円を描き、
そこから「力を伝えるRigidbody」のWorld位置へ線を引く。伝達先が無効、または伝達先にRigidbodyがない
構成は橙色で描くため、配置作業中に取り違えへ気付ける。伝達先がHook自身の場合は線を引かず円だけ描く。

#### Play中の違い

Hook構成タブの接続Wire数とRuntime Wireタブは、Play中だけ意味のある値を出す。
SceneViewギズモはPlay中・停止中どちらでも描く。

#### 保存

Windowの表示状態、ギズモのON/OFFはSceneへ保存しない（Editor起動ごとに既定へ戻る）。
このWindowはHookとWireを読み取って表示するだけで、値を書き換える機能は持たない。

#### 確認手順

1. Hierarchyで物体を選び、`作成 > Hook（選択物体の子）`でHookを作る。
2. `ウィンドウ > Hook / Wire デバッグ`を開き、Hook構成タブに`[要確認]`が出ないことを確認する。
3. HookのHookPointで「力を伝えるRigidbody」を空(-1)にすると`[要確認 1]`が出て、SceneViewの円が橙になる。
4. Playしてワイヤーを接続し、Runtime Wireタブに長さと張力が出ることを確認する。

### 15. 外部認識・オンライン ウィンドウ

- 表示: メニュー`外部認識・オンライン`。
- 対象: `SpeechRecognizer`、`CameraInput`、`ImageRecognizer`、`HapticSource`、Project SettingsのOnline Services。
- 目的: 外部Deviceや通信を、Consoleだけでなく現在状態・直近結果・性能値まで含めて切り分ける。

#### 音声認識タブ

| 表示・操作 | 内容 |
| --- | --- |
| 状態 / Backend / マイク | 共通状態、実際に選ばれたBackendとDevice |
| マイク入力 / 認識中 | Deviceが開いているか、Backendが認識中か |
| 音量 | 0〜1のProgress Bar。常に0なら権限・Deviceから確認 |
| 直近文字列 / Confidence / Keyword / 確定回数 | 最後にBackendから得た結果 |
| Component一覧 | GameObject IDごとの認識中/停止と直近結果 |
| 開始 / 停止 | Play中だけ、対象Sessionを個別操作 |

#### 画像認識タブ

CameraごとにDevice、解像度、取得FPS、累計Frame、Errorを表示する。Previewは専用GPU Textureを作らず、CPU BGRA Frameを64×48 Blockへ縮小Sampleし、ImDrawListの矩形で表示する。物体/顔Bounds、色追跡Bounds/中心、動体中心を同じPreviewへ重ねる。

Recognizerごとに状態、Backend、推論ms、処理Frame数、物体Label、分類、顔数、頭部方向、色、動き、Errorを表示する。Camera FPSが0ならRecognizer設定より先にCamera取得を直す。

#### オンラインタブ

| 表示・操作 | 内容 |
| --- | --- |
| 接続状態 / 環境 / Base URL | Development/Productionの実際の接続先 |
| Game ID / Player | Request Scopeと実行中Identity |
| 最終Request/Body/Status/時間/Response | 直近通信。Body/Response表示は512文字まで |
| 送信中 / 再送待ち / 成功 / 失敗 | Queueと累計Counter |
| いま再送する | Retry Timerを0にし、次UpdateでQueue先頭を再送 |
| 再送Queueを空にする | Pending Requestを削除して保存Fileも更新。未送信Dataは戻らない |
| 疎通確認 | `GET /health`を非同期送信 |

Queue削除は未送信Score/Saveを失う操作なので、原因を直す前に押さない。Status 0は通信未到達、4xx/5xxはWorker到達済みとして分ける。

#### Hapticsタブ

Backend、Device、Device/System状態、現在出力強度、再生Voiceを表示する。`全体の強さ`は実行中Systemへ即時反映する。`Device再検出`は接続を取り直し、`すべて停止`は全Voiceを止める。個別Componentの試聴はInspectorのプレビューを使う。

#### Play中の違い

Play中は`EditorExternalFeatureManager`が全Systemを更新する。Edit中はWindow ManagerがHaptic Previewと外部ログFlushだけを進める。Speech/Vision/OnlineのRuntime結果はPlay停止時に破棄され、Sceneへ保存されない。

#### 保存

WindowのRuntime表示値、認識結果、通信Response、Handleは保存しない。Component設定はScene/Prefab、Haptic全体強度とOnline設定はProject Settings、未送信Online Requestは`SaveData/OnlinePendingQueue.cg2`へ保存する。

#### 確認手順

1. ComponentまたはProject Settingsを設定する。
2. Playし、共通状態とDevice/Backend名を見る。
3. Speechは音量、VisionはCamera FPS、OnlineはStatus、Hapticsは出力強度を先に確認する。
4. 次に認識結果、Input/Script Action、再送Queue、再生Voiceを見る。
5. Error CodeとMessageをConsoleの同時刻Messageと照合する。

詳細は本書と`engine-internals.md`を参照する。

---

## トラブルシューティング

更新基準: 2026-09-25

症状から確認場所へ進む利用者向け資料である。推測で値を変え続けず、Console、Status、実座標、保存File、接続診断を順に確認する。

### 1. Editorが起動しない

確認順:

1. LauncherからProject指定で開いているか。
2. Projectが要求するEngine Versionが導入済みか。
3. Project Format/Script APIの互換Errorが出ていないか。
4. Environment Checkの必須DLL/SDK失敗。
5. `BuildLogs`、Crash Dump、Windows Event Log。

旧ProjectのMigrationを行う前にBackup先を確認する。互換性Errorを無視してProject Fileを直接書き換えない。

### 2. 画面が白い・何も描画されない

- Scene/Game Viewが表示されているか。
- Active Cameraがあるか。PriorityとNear/Farを確認する。
- Model RendererとMesh FilterがActiveか。
- Asset PathがMissingになっていないか。
- Window Resize後のViewportが0になっていないか。
- ConsoleのDX12、Shader Compile、Descriptor Errorを見る。

Build成功だけでは描画成功を証明しない。Editorを起動してScene ViewとGame Viewを別々に確認する。

### 3. ModelやTextureが表示されない

1. Project ViewでAssetを選択する。
2. Import State、Source Path、Errorを確認する。
3. Reimportする。
4. Forward DependencyとMissingを確認する。
5. MeshFilter/RendererのAssetとMaterial参照を確認する。
6. Camera Culling、Object Active、Scale、位置を確認する。

外部ToolでFileを移動した場合はAsset IDが変わっている可能性がある。Project View経由で移動する。

### 4. 当たり判定がずれる

画面上の印象ではなく次の実値を比較する。

- GameObject World位置・回転・Scale
- Collider Center、Size/Radius/Height
- 親ObjectのWorld Transform
- 負Scaleの有無
- Play中にJoltへ作られたShape
- 接触点と法線
- Ray/Projectileの開始、終了、半径

水色Box枠はCamera Facing面の外形だけを描く。Scene CameraのZoomでCollider World Sizeは変化しない。Mesh ColliderではBounds箱を出さない。

### 5. Objectが落ちない・すり抜ける

- RigidbodyがDynamicか。
- Gravityが有効か。
- ColliderがActiveか。
- Collision Layer Matrixで無効になっていないか。
- Triggerになっていないか。
- Mass/Scaleが極端でないか。
- 高速体でContinuous設定が必要か。
- Play中Physics Debugの実Shapeが存在するか。

AutoConvex生成失敗時はConsoleに理由を出し、Box近似へFallbackする。

### 6. 入力が効かない

- Game ViewまたはStandaloneにFocusがあるか。
- PlayerInput/Input ComponentがActiveか。
- Input Action Asset、Map、Action名、Bindingが一致するか。
- Script公開Action名とInspector設定が一致するか。
- GamepadのPlayer Index、接続、Dead Zoneを確認する。
- Pause/Time Scaleと入力のUnscaled処理を混同していないか。

Scene Camera用WASDとGame入力は別経路である。

### 7. C++ Scriptが動かない

1. InspectorのDLL Pathと状態Messageを見る。
2. DLLが存在するか。
3. Debug/ReleaseとArchitectureがEngineに合うか。
4. Runtime API Versionが13か。
5. 必須Export関数があるか。
6. Visual StudioのBuild Errorを解決する。
7. Hot Reload失敗時は旧DLL継続かStop後再読込かを確認する。

Access ViolationはEngine設定ではなくNative DLLの原因行をDebuggerで調べる。

### 8. Collision/Trigger Callbackが来ない

- 両Objectに必要なCollider/Rigidbodyがあるか。
- Layer Collisionが許可されているか。
- TriggerとCollisionのCallbackを取り違えていないか。
- Script Instanceが対象GameObjectに生成されているか。
- Play中のContact Debugに接触が出ているか。
- FixedUpdate最大4 Stepを超える極端なFrame Dropがないか。

### 9. Animationが動かない

- Clip/Animator Graph Pathが正しいか。
- Animator/Animation ComponentがActiveか。
- State EntryとTransition条件が成立するか。
- Parameter名と型が一致するか。
- Modelに対象Bone/Propertyがあるか。
- Root Motionと別Movementが同時にTransformを更新していないか。

Asset更新後はPlayをStopして再開する。

### 10. Audioが鳴らない

- AudioSourceとClipがActiveか。
- Masterと対象Bus Volumeが0でないか。
- Play On Awake/Script再生条件。
- 3D SourceとListenerの距離。
- Loop/Pause/Voice Handle状態。
- Import StateとDecoder Error。

実音確認はConsoleの再生成功Messageだけで代用しない。

### 11. Particle/Effectが出ない

- Asset形式に合うComponentか。
- Spawn/Play条件が発生しているか。
- Lifetime、Scale、Color Alpha、Emission数。
- Camera位置、Billboard Mode、Culling。
- GPU Particle Resource/Shader Error。
- Effekseer DLL/Asset Load Error。

### 12. PostProcessが効かない

- ActiveなPostProcessまたはVolumeがSceneにあるか。
- Volume Weight/Intensity。
- Bloom IntensityとThreshold。
- AA Modeが目的の方式か。
- Scene/Game Viewのどちらを描画しているか。
- Auto Exposureで明るさ変化が相殺されていないか。
- Diagnosticsで対象Passが実行されているか。

Temporalの問題はScene ViewとGame ViewのHistoryを別々に確認する。

### 13. Scene保存・読込に失敗する

- Project Versionが保存可能か。
- Play中ではないか。
- 保存先がProject内か。
- FileがRead Onlyまたは別ProcessでLockされていないか。
- 一時File/renameのError。
- 不正な参照Pathまたは壊れた行。

既存Sceneは安全置換される。失敗時に元Fileを手動削除しない。

### 14. Prefab Apply/Revertが期待と違う

- 選択ObjectがPrefab Rootか。
- Link元Pathが存在するか。
- Variant BaseとSourceを混同していないか。
- Prefab外Object参照がないか。
- 明示Override対象のFieldか。

完全な任意Property差分やNested Prefab編集ではない。Apply前にSource Control差分を確認する。

### 15. 音声・Camera・画像認識・Haptics・Onlineが動かない

最初にメニューの`外部認識・オンライン`を開き、利用不可、準備完了、実行中、エラーのどれかを確認する。

#### Speech

- Windowsのマイク権限と既定入力Device。
- Debug Windowの音量が0より大きいか。
- `ja-JP`等の言語、Keyword表記、Confidence。
- Input Action Map名とAction名。
- Whisperは導入済みEngineの`Tools/Whisper`にある`whisper-cli.exe`、実行用DLL、`ggml-*.bin`モデルを使用する。これらはEngine更新Manifestへ含まれ、更新・Verify・Repairの対象になる。日本語では`base`は短文を誤認識しやすいため、通常は多言語版の`ggml-small.bin`以上を推奨する。Inspectorの「モデル」には、配置したモデル（例: `Tools/Whisper/ggml-small.bin`）を指定する。
- Whisperは既定で「無音で発話終了」が有効で、発話後の無音を検出すると最大録音秒を待たずに非同期推論を開始する。Inspectorで最大録音秒、発話終了の無音秒、音声判定音量を調整できる。
- ONNX音声Backendは未実装である。

#### Vision

- Camera権限、他Applicationの占有、Device名。
- Camera State、取得FPS、実解像度。
- ImageRecognizerの映像元Camera参照。
- 色/動きは内蔵、物体/分類/顔はONNX Modelが必要。
- Modelの入力Shape、色順、正規化、出力Layout、Label順。
- 高負荷時は推論Interval、解像度、FPS、Recognizer数を順に下げる。

#### Haptics

- Device StateとBackend名。
- Project Settingsの全体強度が0でないか。
- Component強度、Channel、Duration。
- Inspectorプレビューの出力強度が上がるか。
- Deviceなしは正常な利用不可状態で、Gameplayは継続する。

#### Online

- Enabled、Environment、Active Base URL、Game ID、Client Key。
- HTTP Status 0は通信未到達、4xx/5xxはWorker到達済みとして分ける。
- Pending Queueが増える場合はNetwork/Workerを復旧してから再送する。
- DevelopmentとProductionのURL、D1、KV、R2を混ぜていないか。
- `SaveData/OnlinePendingQueue.cg2`は平文であり、秘密情報を入れない。

より詳しい切り分けは本書を参照する。

### 16. Buildに失敗する

- 起動Sceneが選択されているか。
- Build Sceneが存在するか。
- Missing依存Assetがないか。
- 出力先の権限と空き容量。
- Runtime DLL/Shader/ThirdParty Copy Error。
- Native ScriptのRelease DLL。
- 未Bake Blast等の生成Asset。

`BuildLogs/DevelopmentGameBuild.log`または`ReleaseGameBuild.log`を見る。

### 17. Editorでは動くがStandaloneで動かない

- `game.build`の起動Scene。
- Build対象SceneとScene遷移先。
- Project外Absolute Pathを使っていないか。
- DLLとAssetが出力Folderにあるか。
- Current Directory前提のPathがないか。
- Save先が書込可能か。

出力Folderだけを別の場所へ移して再現確認する。

### 18. 共同制作へ接続できない

- Server Host/Portへ到達できるか。
- Protocol 3か。
- Engine Version、Project Format、Script API、Channelが一致するか。
- Server/ClientのProject IDが一致するか。
- TailscaleではMagicDNS Hostnameが解決できるか。
- FirewallとServer Process。

`Project IDが一致しません`はコード入力ではなく、Hub公開Project、Launcher登録、Project Metadata、CG2TeamServerのID不一致を示す。

### 19. 参加コードが見つからない

- 正しいHubをLauncherが参照しているか。
- 配布者がProjectをHubへ公開済みか。
- 入力時の空白や記号。
- 公開CatalogのProject IDと表示したコードが同じProject由来か。

参加コードは秘密鍵ではなく、Hub Catalogを検索する短縮キーである。

### 20. 共同制作の変更が消えたように見える

片方がPlay中の場合、Remote編集はPlay側で即時適用せずStop後まで保留する。Stop処理は次の順である。

1. Local Runtimeを停止。
2. Play前Sceneを復元。
3. Play中に届いたRemote変更をRevision順に適用。

Consoleの保留開始・反映完了・反映失敗Messageを見る。Local Playで生成したObjectが消えるのは正常である。

### 21. TeamItemが見つからない

- 種類/状態/Review/Search Filter。
- `targetType`と対象ID。
- 解決済み付箋を非表示にしていないか。
- Pingの期限と履歴保持設定。
- 親Item削除で返信も削除されていないか。
- Change Logが読み込まれているか。

ScriptLineは外部Editorを開くが、内蔵Editorの正確な行Scrollはない。

### 22. 重い・カクつく

1. DiagnosticsのCPU/GPU Frame HistoryでSpikeを特定する。
2. Draw/Dispatch、VRAM、Physics Body、Particle、Audio Voice、Asset数を見る。
3. Shadow、Planar Reflection、Ocean FFT、Light Probe、SSR/SSGI、Temporalを分けて確認する。
4. Object生成HitchならPool/Prewarmを使う。
5. 大量PhysicsならCollider形状、Body数、Fixed Stepを確認する。
6. 共同制作なら転送Byte、巨大Asset、PingではなくScene Snapshot頻度を確認する。

平均FPSだけでなくFrame Timeの最大値を見る。

### 23. 復旧場所

| 対象 | 場所 |
| --- | --- |
| 共同制作履歴 | `.team/change-log.jsonl` |
| Scene/Asset Backup | `.team/backups` |
| Remote削除Asset | `.team/trash/Revision_N` |
| 競合Copy | `.team/conflicts/<change-id>` |
| Project Migration | Migration時に表示されたBackup Folder |
| Build Log | `BuildLogs` |

復旧元を確認する前に上書き保存やCleanを行わない。

### 24. 報告時に添える情報

- Engine Version
- Project Format / Script API
- EditorまたはStandalone
- Scene Pathと対象GameObject UUID
- Component名と設定値
- 再現手順
- Console全文
- Build Log/Crash Dump
- 期待結果と実結果
- 共同制作ならRole、Project ID、Revision、Protocol、相手人数
- 描画ならScene/Game View、Camera、解像度
- Physicsなら両ObjectのWorld位置・形状・Size

### 25. 関連文書

- 基本操作: 本書
- 現行機能と制限: [engine-internals.md](engine-internals.md)
- 保存、互換、Backup: [engine-internals.md](engine-internals.md)
- Runtime更新順: [engine-internals.md](engine-internals.md)
- 描画PassとResource: [engine-internals.md](engine-internals.md)
- 共同制作: 本書
- 外部認識の利用手順: 本書
- 外部認識の内部設計: [engine-internals.md](engine-internals.md)

---

## 配布・Launcher・Version管理

更新基準: 2026-09-15

### 共同制作者が受け取るもの

通常の共同制作者へ直接渡すものは、次の1ファイルだけです。

```text
CG2Launcher.exe
```

Launcherには既定の配布Hubが組み込まれており、起動時にProject一覧を自動取得します。PowerShell、Manifest URL、DLLコピー、PATH設定、Project ZIPは不要です。別ネットワークから使う場合は、事前にTailscaleへ参加して配布Hubへ到達できる必要があります。

```text
Launcherを起動
→ 公開Projectが一覧へ自動表示
→ プロジェクトを選び「プロジェクトへ参加」
→ 必要なEngineをDownload・検証・導入
→ Project初期SnapshotからProject Folderを自動作成
→ Scene・Asset・ScriptをDownload・SHA-256検証
→ Editor起動
→ 共同制作サーバーへ自動接続
```

Projectは `%USERPROFILE%\Documents\CG2Engine Projects\<Project名>` に作成します。同名Folderがある場合は上書きせず連番を付けます。取得中は `.joining` Folderを使い、全FileのSizeとSHA-256が一致した後だけ正式Projectへ切り替えます。途中失敗したProjectをEditorで開くことはありません。

`.cg2-invite` は手動招待や別Hubへの接続用として引き続き利用できます。新形式Inviteを開いた場合も、Project初期Snapshot取得からEditor起動まで同じ処理を通ります。

### 配布者がProjectを公開する

```text
CG2Engine Hub
→ 配布者
→ Project ID・Project名・公開アドレス・共同制作ポートを保存
→ 「プロジェクトを公開」
→ 公開するProject Folderを選択
```

公開対象は `Assets`、`resources`、`NativeScripts`、`ProjectSettings` です。ScriptのSource一式と、Editorが直ちに読み込む `resources/scripts/*/x64/Release/*.dll` は含めます。`Library`、`.team`、中間生成物、個人用の共同制作設定は除外します。Snapshot本体を確定後にProject ManifestとProject Catalogを切り替えるため、公開途中の不完全Projectは参加者へ見せません。

Snapshotには `SnapshotRevision`、`CollaborationId`、`OwnerId` を記録します。参加者のEditorはSnapshot Revisionを同期済み基準として使い、接続時にServerへ不足分だけを要求します。

### 新規Projectを作る

```text
CG2Engine ランチャー
→ 導入済みEngineを選択
→ 「新規プロジェクト」
→ 空の保存先Folderを選択
→ 標準または最小Templateを選択
```

作成時にEngine Versionを固定し、`ProjectId`、`CollaborationId`、`OwnerId`、`LastSyncedRevision`を自動生成します。これらは `ProjectSettings/ProjectCollaboration.cg2` とLauncher登録情報から確認できます。

### 再接続と競合解決

Editorは接続していない間もScene・Asset・Script変更をOffline ChangeLogへ記録します。再接続時は次の順です。

```text
LastSyncedRevisionを送信
→ Serverが不足Commitを履歴から返す
→ Base / Server / Localで3-way比較
→ 非競合のScript行は自動Merge
→ 同じ行だけ競合UIへ表示
```

競合UIではBase、Server、Localを並べて確認できます。ScriptはLocal欄を直接編集して「手動編集結果を採用」を押せます。自動Mergeできる場合は「両方をMerge」、片方をそのまま使う場合は「自分側を採用」「相手側を採用」を使います。

### GUI Launcher

Launcherを引数なしで起動するとGUIを表示します。

```text
CG2 Launcher

Projects
  MyGame   Engine 0.9.4+152 Stable   Status: Ready
  [Update and Open] [Verify] [Repair] [Use Previous] [Project Location]

Installed Engines
  0.9.4+152
  0.9.5+161

[招待を開く] [設定] [配布者]
```

- `Update and Open`: 更新検出、Download、Verify、Install、互換性確認、Editor起動を連続実行します。処理中もLauncher画面は応答し、進捗を表示します。Pinned Projectは勝手に更新しません。
- `Verify`: Projectが使うVersionの保存済みManifestからSizeとSHA-256を検証します。
- `Repair`: 欠落・破損FileだけHubから再取得します。
- `Use Previous`: 1世代前の導入済みEngineへ切り替えます。旧Engineが現在のProject Formatに対応しない場合は拒否します。
- `Project Location`: Project同期とは分離したまま、取得済みProjectの場所をLauncherへ登録します。
- `Settings`: Launcher VersionとInstall Rootを表示します。
- `配布者`: Engine開発者向けの配布設定、公開内容の確認、公開、履歴、サーバー管理を開きます。

Launcher Versionは`1.0.0+1`です。Engine Version、Project Format Version、Script API Versionとは別に管理します。Hubの`launcherVersion`が異なる場合は更新通知を表示します。Launcher本体の自動入替は将来用で、現時点では通知までです。

### Invite形式

Inviteは漏れても認証情報を奪われない接続案内です。Password、Tailscale Node Key、秘密Token、管理者Credentialを保存してはいけません。

```json
{
  "formatVersion": 1,
  "projectId": "my-game",
  "projectName": "MyGame",
  "hub": "ms.tailnet-name.ts.net:8080",
  "updateChannel": "Stable",
  "requiredEngineVersion": "0.9.4+152",
  "projectEndpoint": "/projects/my-game/project.json",
  "engineManifestEndpoint": ""
}
```

`hub`は`HTTP/HTTPSで到達可能なCG2 Hub`です。Tailscale専用の意味はありません。MagicDNS名を推奨し、Raw IPはAdvanced用途に限定します。Schemeを省略したHostはHTTPSを先に試し、その後HTTPを試します。固定Portを使う場合はHostへ含めます。

`engineManifestEndpoint`は通常空にします。Launcherは`/cg2-hub.json`からChannelのManifest位置を取得し、取得できない旧Hubでは`/update/stable/engine.manifest`などの規約へフォールバックします。

Project Endpointの例:

```json
{
  "formatVersion": 1,
  "projectId": "my-game",
  "requiredEngineVersion": "0.9.4+152",
  "updateChannel": "Stable",
  "syncAdapter": "TeamCollaboration"
}
```

### Install Layout

```text
%LOCALAPPDATA%\CG2Engine\
├─ Launcher\
├─ Engines\
│  ├─ 0.9.4+152\
│  └─ 0.9.5+161\
├─ Cache\
│  ├─ Hub\
│  └─ Staging\
└─ LauncherState\
   ├─ launcher.state
   ├─ projects.registry
   ├─ publisher.settings
   ├─ publish-history.log
   ├─ Manifests\
   └─ launcher-update.notice
```

EngineはVersionごとにSide-by-Side導入します。Install/Updateは`Cache/Staging`へDownloadし、全FileのSize/Hash確認後だけ`Engines/<Version>`をReadyにします。失敗途中のDirectoryをReady扱いせず、現在Versionを維持します。

旧Launcherの`Versions`とroot直下`launcher.state`も読み込めるため、既存導入環境は壊しません。

### 配布者の通常手順

```text
ランチャーの「配布者」を開く
→ 初回だけリリース元 / 配布フォルダー / 公開アドレス等を保存
→ [次のBuildを作成して公開]
→ Build完了後の「公開内容の確認」で差分を確認
→ 「このPC」方式なら配布サーバーを[開始]
→ 初回だけ[招待を作成]
→ .cg2-inviteを共同制作者へ渡す
```

通常運用でPowerShell、バージョン入力、Manifestの場所入力は不要です。「配布者」画面は`engine-version.json`を正本として、現在のEngineバージョン、Project形式、Script API、各公開先のバージョンを表示します。

「配布者」画面の初回設定:

```text
リリース元              C:\kogakuin\LE1\CG2\x64\Release
配布フォルダー          C:\CG2Hub
公開アドレス            http://ms.tailf0bf0a.ts.net:8080
既定の公開先            安定版 / ベータ版 / 開発版
共同制作サーバー        ...\CG2TeamServer.exe
配布方法                このPC / 外部サーバー
```

`次のBuildを作成して公開`は、`engine-version.json`のbuild番号を一つ進め、EngineのReleaseビルドを実行してから、公開内容を作成します。番号を手入力する必要はありません。Buildに失敗したときは元の番号と生成Version Headerへ戻します。Buildが成功した後は、公開前に公開内容を確認できます。

`現在のBuildを公開`は、すでにBuild済みの未公開成果物を公開するための操作です。どちらの操作も必須EXE/DLL、配布フォルダーへの書き込み、バージョン逆行、Project固有Assets混入を事前確認します。「公開内容の確認」にはバージョン、公開先、ファイル数、合計サイズ、追加/変更/削除、公開アドレスを表示します。同じバージョンの上書きは拒否します。major / minor / patchを変えるときだけ、`engine-version.json`を手動で変更します。

実処理は`公開準備 → SHA-256/Manifest生成 → 検証 → バージョン/公開先切替`です。公開先の切り替え途中で失敗した場合は以前のManifestを復元し、GUIに旧バージョン維持と再試行可否を表示します。履歴にはバージョン、公開先、日時、ファイル数、サイズ、成功/失敗を最大100件保存します。

「このPC」方式では「配布者」画面から配布サーバーと共同制作サーバーを開始・停止・再起動できます。ランチャーを閉じてもサーバーは継続します。「外部サーバー」方式では開始・停止を行わず、ホスト名とポートへの到達性・応答時間を確認します。MagicDNSも通常のホスト名として扱い、Tailscale APIには依存しません。

以下のCLIはAutomation/Debug用として残っています。GUIと同じ`PublisherService`を呼び、公開処理を二重実装していません。

```powershell
x64\Release\CG2Launcher.exe publish-engine `
  --release C:\kogakuin\LE1\CG2\x64\Release `
  --output C:\CG2Hub `
  --version 0.9.4+152 `
  --channel Stable `
  --hub http://ms.tailnet-name.ts.net:8080

x64\Release\CG2Launcher.exe create-invite `
  --output C:\CG2Hub\MyGame.cg2-invite `
  --project-id my-game `
  --project-name MyGame `
  --hub ms.tailnet-name.ts.net:8080 `
  --channel Stable `
  --required-engine 0.9.4+152 `
  --project-endpoint /projects/my-game/project.json `
  --collaboration-host ms.tailnet-name.ts.net `
  --collaboration-port 48000
```

`--collaboration-host` / `--collaboration-port` は任意です。指定すると、Inviteへ
`collaborationHost` / `collaborationPort` が書き込まれ、`setup-invite` 実行時に
Project側へ `ProjectSettings/TeamCollaboration.invite` として置かれます。
Editorはこれを読み、**Team設定側で未設定の項目だけ**を補完します（手動設定は上書きしない）。
これにより、LauncherでHostを設定したのにEditorで同じHostを再入力する、という手間が無くなります。

`--hub` は配布用のHTTP Hub、`--collaboration-host` は共同制作の中継Server
（`CG2TeamServer.exe`）で、**別のもの**です。同じPCで両方動かす場合でもPortは分けます。

Collaboration項目を持たない旧形式のInviteもそのまま読み込めます（項目は任意扱い）。

公開Folderは次の構造になります。

```text
CG2Hub\
├─ cg2-hub.json
├─ engines\
│  └─ 0.9.4+152\
│     ├─ CG2.exe
│     ├─ 必要DLL・Shader・Resource
│     └─ engine.manifest
└─ update\
   ├─ stable\engine.manifest
   ├─ beta\engine.manifest
   └─ dev\engine.manifest
```

Tailscaleは通信経路の1つです。同じ構造をLAN、Cloudflare、VPSへ置いてもInviteのHub Hostを変えるだけです。MagicDNS利用時は共同制作者が同じTailnetへ参加し、HubのPortへ到達できる必要があります。Hub PCが停止している間は新規DownloadやRepairはできませんが、導入済みEngineの起動は可能です。

### Versionと互換性

Versionの正本は`Engine/Version/engine-version.json`です。Build前に`Tools/Versioning/Sync-Version.ps1`がC++ Headerと公開サイト用TypeScriptを生成します。

```text
Launcher 1.0.0+1
Engine 0.9.4+152 / Stable
Project Format 1 / Scene Format 1 / Prefab Format 1
Script API 13
```

Projectは`ProjectSettings/ProjectVersion.cg2`へRequired Engine、Minimum/Pinned、Channel、Format Versionを保存します。Project FormatがEditorより新しければOpen/保存を拒否し、古ければBackup後にMigrationへ進みます。RollbackはEngine選択だけを変え、SceneやAssetを巻き戻しません。

`RequiredScriptApiVersion`もProject metadataへ保存します。旧Version 1 metadataにキーが無い場合は現行値を補い、保存時に追記します。LauncherはInstalled Manifestの`ScriptApiVersion`と照合し、不一致ならEditor起動前に理由を表示して拒否します。

### Advanced / Automation CLI

通常の共同制作者は使用しません。CI、Hub構築、障害調査用に既存CLIを維持しています。

```text
create-manifest / publish-engine / create-invite
setup-invite / install / update / check-update
verify / repair / rollback / open / status / set-channel
check-project / migrate / check-peer / env
```

`game.build`とEngine Update Manifestは完全に別です。Engine Manifestは`Path / Size / SHA-256 / RemovedFile / RequiredProjectFormat / ScriptApiVersion`を持ちます。

---

## 共同制作 利用仕様

更新基準: 2026-09-26

この文書は「TEAM - 共同制作」Windowの操作、参加コード、Scene/Asset同期、Play中の変更分離、付箋・Ping・チャット・レビュー、Lock、競合、Backup、通信制限をまとめる。利用者向けの入口は本書第7章、本書は同期契約と復旧方法の詳細を扱う。コード構造と内部処理は`engine-internals.md`を参照する。

### 1. 対象と前提

- 1台をHostにし、既定で最大4台のRemote Clientを接続する（Hostを含めて最大5台）。上限は1〜32台で設定でき、3台共同制作では2台以上にする。
- HostがRevisionを確定・配信する。Host Migrationは実装しない。
- 共有対象は選択した`Assets/Scenes`配下のFolderにあるSceneと、それらが参照するProject Assetである。
- Editing中のSceneはGameObject/Component UUIDを使って変更対象を識別する。
- 同期中でもGameplay固有のServer AuthorityやPlay Session共有を行う機能ではない。
- Socket接続直後にEngine Version、Project Format、Script API Version、Update Channelを交換する。不一致PeerとはScene/Asset/Lockを交換せず`Incompatible`として拒否する。

### 2. 保存先と一時File

| Path | 内容 |
| --- | --- |
| `ProjectSettings/TeamCollaboration.settings` | User Name、Host、Port、Host設定、自動開始、共有Scene Folder等 |
| `.team/change-log.jsonl` | 変更履歴 |
| `.team/live/current.scene` | 現在Scene Snapshot |
| `.team/live/incoming.scene` | 受信Snapshotの検証・読込用 |
| `.team/live/fragment.scene` | GameObject Fragment生成用 |
| `.team/asset-uuids.txt` | 共同制作側Asset UUID対応 |
| `.team/conflicts/<change-id>/` | 競合コピー |
| `.team/backups/` | Scene復旧BackupとMetadata |
| `.team/backups/assets/Revision_N/` | 上書き前Asset Backup |
| `.team/trash/Revision_N/` | Remote削除を即時消去せず退避する場所 |

`.team`自体は同期対象外である。競合・Backup・Trashを相手へ再送して循環させない。

### 3. Window

#### 3.1 接続設定

| UI | 契約 |
| --- | --- |
| User Name | Member表示、Lock所有者、Change履歴へ使用 |
| Host | Clientが接続するIP/Host名 |
| 共有Sceneフォルダ | Directoryで、`Assets/Scenes`配下だけを許可 |
| Projectから選択 | Project Windowで選択したDirectoryを入力。File選択は不可 |
| Port | 1〜65535。既定45678 |
| このPCをHostにする | Host/Client動作の切替 |
| Start Team Server Automatically | 設定読込後の自動Host開始 |
| サーバー開始 / Hostへ接続 | OfflineまたはDisconnected時に接続開始 |
| Server Stop / 切断 | SocketとOnline状態を終了 |
| 設定保存 | 現在値をSettingsへ保存 |

状態はOffline、Connecting、Online、Synchronizing、Conflict、Incompatible、Disconnected。上部にEngine Version、Project Format、Script API Version、Channel、Revision、Members、Unsynced Changes、Conflicts、Editing Locksを表示する。

#### 3.2 参加コードとProject ID

- 参加コードはLauncherがProject IDから作る固定の8文字コード（表示は`XXXX-XXXX`）である。見間違えやすい文字は使用しない。
- 配布者はLauncherでProjectをHubへ公開した後に参加コードを表示し、参加者はLauncherのProject画面へコードを入力する。
- Launcherは設定済みHubのProject一覧を取得し、コードが一致したProjectのSnapshot、固定Engine Version、共同制作接続設定を導入してEditorを起動する。
- 参加コード自体にProject、Engine、認証情報を埋め込まない。Hubに公開されていないProjectへコードだけで参加することはできない。
- `ProjectSettings/ProjectCollaboration.cg2`、Launcherの登録情報、HubのProject Manifest、`TeamCollaboration.settings`、`TeamCollaboration.invite`、CG2TeamServerの`--project-id`は同じProject IDを指す必要がある。
- 接続時はProtocol VersionとProject IDを検証する。異なるProject IDを受け入れて同期することはせず、`Project IDが一致しません`として拒否する。
- 専用Serverの状態FileにはProject IDも記録する。Editorが同じPortに残っている旧Project用Serverを検出した場合は停止して、現在ProjectのIDで起動し直す。

参加コードの生成・照合・Project取得はLauncherの機能である。参加コードUIを配布する場合はLauncherを更新する。接続後のProject ID検証と共同制作同期はEngineおよびCG2TeamServerの機能なので、配布物は対応するLauncher・Engine・CG2TeamServerを同じReleaseとして揃える。

#### 3.3 共有内容と進捗

- 「共有中の依存Asset」はPath、File Size、同期済み/未同期を表示する。
- Sceneが参照するPathにFileがなければ`参照あり / ファイルなし`として明示する。
- 「転送予定 / 進捗」は待機Assetと、実際に送受信したByte数をFile単位のProgress Barで表示する。
- 「Scene状態 / 履歴」は共有Folder内Sceneの同期状態、相手が編集中か、最終更新者を表示し、最近30件の変更を列挙する。

### 4. 更新周期・接続上限

| 項目 | 値 |
| --- | --- |
| Scene Snapshot比較 | 0.75秒 |
| Asset Scan | 2秒 |
| Lock Heartbeat | 10秒 |
| Lock Timeout | 30秒 |
| Presence送信 | 5秒 |
| Presence Timeout | 15秒 |
| Remote Member上限 | 既定4（設定範囲1〜32、Hostは含まない） |
| Network受信Buffer上限 | 192 MiB |
| 同期Assetの生File上限 | 128 MiB |
| Chunk化開始 | 512 KiB超 |
| 1 Transport Payload | 256 KiB |

Asset本体はBase64化されるため、Network Bufferは生File上限より大きい。128 MiBを超えるFileは同期対象として扱わない。

### 5. Scene同期

#### 5.1 差分単位

- 小さいProperty変更は対象GameObjectだけのFragmentを送る。
- Component追加/削除、GameObject追加/削除・複製、Hierarchy等の構造変更は構造変更として扱い、必要に応じFull Snapshotを送る。
- Scene、GameObject、ComponentのUUIDで相手側の対象を解決する。表示名や配列Indexだけへ依存しない。
- HostがRevisionを割り当て、受信側は順序と現在状態を更新する。

Play中とRemote変更適用中は通常のScene Scanを止める。Play中にこの端末だけで発生したRuntime変更を共同制作差分として送らず、Remote適用で生じたローカル差分を再送するEchoを防ぐためである。Offline時は通信を止めても編集用Scene/AssetのScanを継続し、発生したローカル変更をUnsyncedとして保持して再接続後の送信対象にする。

#### 5.2 Play中の変更分離

- Play開始時に`EditorRuntimeManager`が編集SceneをBackupする。
- Playした端末のRuntime変更は共同制作へ送らない。Stop時にPlay開始前Backupへ戻るため、生成・削除・Transform変更等は通常のPlayと同じく破棄される。
- 他のPlayしていない端末から届いたScene/Asset変更は、通信、Revision確定、Change Log保存だけを継続し、Play中のSceneへは適用しない。
- Stop時は、最初にRuntimeManagerがPlay開始前Sceneを復元し、その後で保留したRemote変更をRevision順に適用する。これにより他の人の編集は消えない。
- TeamItem（付箋・Ping・チャット・レビュー）はScene本体を変更しないため、Play中も通常どおり受信・表示する。
- 保留変更の適用に失敗した場合は未適用分をQueueへ残し、Consoleへ`Play中に保留した共同制作変更を反映できませんでした`を表示する。

#### 5.3 参加時Catch-up

Hostは新しく参加したClientだけへ、現在Scene、共有Scene Folder、Lock状態、共有AssetのHash情報を送る。Clientは存在しないFileまたはHashが異なるFileだけを要求する。3台目以降が参加しても、接続済みClientへScene Snapshotを再適用せず、編集中の状態を巻き戻さない。Sceneは受信後、`.team/live/incoming.scene`へ一度書いてLoadできるか確認してから現在Sceneへ適用する。

### 6. Asset同期

#### 6.1 対象

共有Scene Folder内のSceneと、Scene Textから抽出した`Assets/` / `resources/`参照を対象にする。Script Moduleに必要なSource/Header/Build Scriptは対象になり得るが、`x64`成果物は除外する。依存抽出はText Path参照が基準であり、全Asset形式の内部依存を完全解析するものではない。

#### 6.2 除外

- `_Archive/`
- `Library/`
- `Builds/`
- `.team/`
- `x64/`
- `.pdb`, `.lib`, `.exp`, `.ilk`等の中間生成物
- Path中に`.bak`を含むBackupと`imgui.ini`
- `Assets/Shaders/lygia/`, `FidelityFX/`, `RTXGIDDGI/`, `NRD-4.17.3/`, `RTXGI-SDK/`のVendor/大容量Shader Tree
- 共有Folder外の別ゲームScene
- 共有Sceneから参照されていないAsset

#### 6.3 転送と検証

512 KiBを超えるSnapshot/Assetは`assetBegin`、複数`assetChunk`、`assetEnd`へ分割する。受信側は宣言されたSizeとHashを確認し、Pathは絶対Path、`..`、`Assets/`/`resources/`外を拒否する。転送完了後は共通`AssetManager`へ変更通知し、種別別Hot Reload結果を記録する。

AnimationはPlay再開、Scriptは再Buildが必要である。Model、Texture、Audio、VFXの反映範囲は`engine-internals.md`を参照する。

#### 6.4 上書きと削除の復旧

受信Assetが既存Fileを上書きする前に`.team/backups/assets/Revision_N`へ退避する。Remote削除は原則`.team/trash/Revision_N`へMoveし、Move失敗時はCopy後に元Fileを削除する。したがってUI上の同期削除でも、直ちに復旧不能な物理削除にはしない。

### 7. 付箋・Ping・チャット・レビュー

#### 7.1 共通TeamItem

4機能は別々の一時UIではなく、共通のTeamItemとしてRevision、履歴、Offline再送、競合検出の対象になる。TEAM Windowで種類、対象、担当者、メンション、本文等を入力でき、作成後は編集、返信、対象へ移動、削除ができる。親Itemを削除すると返信もまとめて削除する。

| 種類 | 主用途 | 固有動作 |
| --- | --- | --- |
| 付箋（Note） | 対象へ残す課題・メモ | 担当者、未解決/解決済み、再オープン、検索 |
| Ping | 一時的な「ここを見て」通知 | Scene表示期限、履歴を残さない設定では作成者が期限後に同期削除 |
| チャット（Chat） | 対象を添付した会話 | 返信、メンション、対象カードから移動 |
| レビュー（Review） | 対象または変更履歴への確認 | 未確認/承認/要修正、対象Revision以後の変更による古いReview表示 |

#### 7.2 対象とEditor統合

| 画面 | 対象 | 操作・表示 |
| --- | --- | --- |
| Scene View | Scene座標、選択GameObject | 右クリックで付箋/Ping作成。座標Markerを表示してクリックで開く |
| Hierarchy | GameObject | `TEAM N`件数Badge、右クリックで4種類を作成 |
| Inspector | Component、Property | Component/Property単位の件数Badgeと付箋/Ping作成 |
| Project View | Scene、Prefab、Asset、Script、ScriptLine | Asset行の`TEAM N`、4種類の作成、Script行番号を指定した付箋 |
| TEAM Window | すべて | 種類・状態・Review状態・本文の検索、編集、返信、削除、対象へ移動 |

対象IDにはPathではなくGameObject/Component UUIDを優先して使う。PropertyはComponent UUIDとProperty名、ScriptLineはScript Pathと行番号・コード文脈を保持する。GameObjectに付いたScene Markerは現在のWorld位置へ追従する。

#### 7.3 通知、移動、制限

- 自分宛ての担当、メンション、Pingを通知一覧へ追加し、通知から対象TeamItemを開ける。
- GameObject/Component/Propertyは対象を選択し、Scene座標はScene Cameraを移動し、Asset/Prefab/Scriptは該当Assetを選択する。
- Scriptは外部Editorで開く。CG2Engine内蔵のScript Editorはないため、コード行への内蔵Gutter表示、選択範囲の共同表示、厳密な行スクロールは未対応である。
- Propertyへの移動は対象Object/Component選択までで、Inspector内の任意行へ必ず自動Scrollするものではない。
- TeamItemの同時編集はItem単位で競合を検出し、自分側、相手側、本文の手動Mergeから選ぶ。別IDとして作られた返信同士は独立して同期する。

### 8. Editing Lock

| 操作 | Lock粒度 |
| --- | --- |
| 通常のComponent Property編集 | Component UUID単位 |
| Transform Gizmo | Transform Component単位。複数選択も各対象を確認 |
| Component追加/削除 | GameObject構造Lock |
| GameObject削除/複製、Hierarchy構造変更 | GameObject構造Lock |

GameObject構造Lockは配下Component Lockより強く、相手がObject全体を編集中ならComponent編集も拒否する。選択・編集対象はHeartbeatで更新し、30秒更新がなければ期限切れにする。UIには所有者名と「Component編集中」等を表示する。Lockは競合確率を下げる仕組みであり、Network断や同時要求を含むすべての競合を数学的に排除するものではない。

### 9. 競合解決

Conflictごとに相手、Scene、操作、Property、競合Copy Pathを表示する。

| 選択 | 動作 |
| --- | --- |
| 自分側を採用 | Local状態を正として解決 |
| 相手側を採用 | Remote状態を適用 |
| 両方をMerge | C++ Script Assetだけの限定Merge。任意Binary/Sceneの万能Mergeではない |

競合中はCopyを`.team/conflicts`へ保存し、選択前に一方を破棄しない。解決後はRevision、Unsynced、Conflict件数を確認する。

TeamItemはScene/Assetの競合Copyを作らず、Item内Revisionで同一Itemの同時編集を検出する。TEAM Windowの`TeamItem競合`から自分側、相手側、または本文の手動Mergeを選択する。

### 10. Scene Backupと復元

「復旧バックアップ」は最大30件を表示する。Metadataから対象Scene、編集者、時刻を表示し、現在Sceneとの比較で追加Object数、削除Object数、主要変更数を算出する。主要変更はUUIDで対応付けたObjectのName、Parent、Component数、Transform等を比較する。

復元前には現在Sceneを`BeforeUiRestore_Revision_N.scene`として安全Copyし、その後BackupをLoad、保存、Snapshot再取得する。復元操作自体が新しい同期状態になるため、他MemberとRevisionを確認してから実行する。

### 11. セキュリティと運用制限

- 通信内容を暗号化するTLS処理はない。
- User認証、権限Role、秘密Tokenの検証はない。
- 信頼できる同一LAN/VPN内だけで使用し、InternetへPortを直接公開しない。
- Host停止時の自動Host Migrationはない。Hostを再決定して再接続する。
- Source同期はVersion Controlの代替ではない。Commit/Branch/Review履歴はGitで残す。
- 最大Remote数、File Size、除外規則を超えるProjectでは別の配布手段を使う。

### 12. 推奨確認手順

1. 両PCで同じ基準CommitとProjectを用意し、Gitの未保存差分を確認する。
2. 配布者がLauncherでProjectをHubへ公開し、表示された参加コードを参加者へ渡す。
3. 参加者がLauncherへコードを入力し、Project/Engine取得後にEditorを起動する。接続診断のProject IDとProtocol 3が両端で一致することを確認する。
4. Hostで`Assets/Scenes`配下のゲーム専用Folderを指定してServerを開始する。
5. Client接続後、Members=2、Revision、共有Scene/依存Asset一覧を確認する。
6. 一方でTransformをDragし、相手側にLock所有者が出て編集を拒否することを確認する。
7. Float Property変更がFragmentで届き、Component追加が構造変更として届くことを確認する。
8. 片方をPlayしたまま、もう片方でGameObjectを変更する。Play側では即時に編集Sceneへ混ぜず、Stop後にRemote変更だけが残ることを確認する。
9. Scene、Hierarchy、Inspector、Project Viewから付箋/Ping/Chat/Reviewを作成し、相手側のBadge、通知、対象移動、返信、編集、削除を確認する。
10. 同じTeamItemを同時編集し、TeamItem競合の3つの解決方法を確認する。
11. 1 MiB超のAssetを変更し、送受信Byte進捗とHash一致を確認する。
12. Texture/Audio/VFXのHot Reload、Animation/Scriptの手動対応表示を種別ごとに確認する。
13. 同時編集でConflictを作り、自分側/相手側とScript Mergeの範囲を確認する。
14. Backup比較後に復元し、`BeforeUiRestore`安全Copyが作られることを確認する。
15. Remote削除Assetが`.team/trash/Revision_N`から復旧できることを確認する。

この文書はソース契約の照合結果を含む。2026-09-25時点でRemote cursor、他ユーザー視点への移動、GameObject同期は実機成功の報告がある。一方、参加コードを含む全導線、Play中保留、TeamItem競合、通信遮断、Backup復元を一連で行う回帰確認は別途必要である。

### 13. 関連ファイル

- `Source/Engine/Editor/EditorTeamCollaborationManager.h/.cpp`
- `Source/Engine/Editor/EditorTeamUuid.h/.cpp`
- `Source/Engine/Asset/AssetManager.h/.cpp`
- `Source/Engine/Asset/AssetRegistry.h/.cpp`
- `Source/Engine/Editor/EditorAssetManagerAdapters.h/.cpp`
- `Source/Engine/Collaboration/CollaborationProtocol.h`
- `Tools/CG2Launcher/LauncherExperience.h/.cpp`
- `Tools/CG2Launcher/LauncherGui.cpp`
- `Tools/CG2TeamServer/CG2TeamServerMain.cpp`
- `docs/engine-internals.md`

---

## 遠隔共同制作・Tailscale

更新基準: 2026-09-25

LAN内で動いている共同制作（Scene/Asset/Script同期、Lock、Presence、Revision、Conflict）を、
別ネットワーク・遠隔地からも同じ操作感で使うための構成をまとめる。
**CG2Engine本体はTailscale専用設計ではない。** Tailscaleは単なる到達経路であり、
Engineから見ると「解決できるHost名とPort」以上の意味を持たない。

### 全体構成

```text
               CG2TeamServer.exe
                     │
        ┌────────────┼────────────┐
        │            │            │
     Editor A     Editor B     Editor C
     自宅          大学          遠隔地
```

Scene差分・Asset同期・Script同期・分割転送・Conflict判定・Local/Remote採用・Merge・
Backup/Trash・Change Log は既存実装をそのまま使う。今回変更したのは
**通信経路・接続管理・遠隔接続時の再接続・Server構成**だけである。

### Transport抽象化

```text
ICollaborationTransport          … Connect / Listen / Send / Poll / Disconnect / GetState
└─ TcpCollaborationTransport     … LAN と Tailscale の両方をこの1実装で処理
```

- `Source/Engine/Collaboration/ICollaborationTransport.h`
- `Source/Engine/Collaboration/TcpCollaborationTransport.h/.cpp`
- `Source/Engine/Collaboration/CollaborationProtocol.h`（Editor と Server の共通規約）

Scene同期やLock処理の内部に `if (tailscale)` のような分岐は無い。
将来 `WebSocketCollaborationTransport` を追加すれば、共同制作システム本体を作り直さずに
WSS / Cloudflare / VPS へ移せる。

### 接続先の書き方

| 経路 | Server Host の例 |
| --- | --- |
| LAN | `192.168.1.20` |
| Tailscale | `ms.tailxxxx.ts.net`（MagicDNS hostname） |

ホスト名・IPv4・IPv6のいずれも `getaddrinfo` で解決する。Server側はIPv6 Dual Stackで
待ち受け、できない環境ではIPv4へFallbackする。

**Tailscaleの `100.x.x.x` を直接書かず、MagicDNS hostname を使うこと。**
IPは再割り当てで変わり得るが、hostnameは変わらない。

### CG2TeamServer の起動

```bat
x64\Release\CG2TeamServer.exe --project-id my-game --port 48000 --max-clients 4
```

| 引数 | 既定 | 意味 |
| --- | --- | --- |
| `--project-id` | 必須 | 接続してくるEditorのProject IDと一致させる |
| `--port` | 48000 | 待ち受けPort |
| `--max-clients` | 4 | 同時接続数（1〜32） |
| `--data` | `./CG2TeamServerData` | Revision と Change Log の保存先 |

Serverが扱うのは接続受付・Project識別・Presence・Lock状態・Revision・Conflict情報・
Scene/Asset/Script転送・Change Logである。**Sceneの意味解釈（差分の中身・Merge・
Conflict解決）はEditor側のまま**で、Serverは中継と状態管理に徹する。

Editor内Host方式（従来どおり誰かのEditorがHostになる）もローカルテスト用・互換用として残している。

### Editor側の設定

Team Collaboration Window で設定する。

```text
Server Host : ms.tailxxxx.ts.net
Port        : 48000
Project ID  : my-game
Auto Connect: ON
Max Clients : 4   (このPCがHostの時のみ有効)
```

保存先は `ProjectSettings/TeamCollaboration.settings`。
Launcherの `.cg2-invite` に `collaborationHost` / `collaborationPort` がある場合、
`setup-invite` が `ProjectSettings/TeamCollaboration.invite` を置き、Editorは
**Team設定側で未設定の項目だけ**をそこから補完する（手動設定は上書きしない）。
IPを手入力することを通常のWorkflowにしない。

現在はLauncherのProject画面から参加コードでも参加できる。参加コードはProject IDから生成され、
Launcherが設定済みHubのProject Catalogと照合してProject Snapshot、固定Engine Version、
共同制作接続設定を取得する。参加コードのUIと照合処理はLauncher側、接続後のProject ID検証は
EditorとCG2TeamServer側の責務である。対応版は3つを同一Releaseとして配布する。

Hub URLがTailscale MagicDNS（`*.ts.net`）の場合、Launcherは参加処理の前に
`tailscale status --json`から現在のMagicDNS suffixを確認する。参加先と異なる場合は、
利用者へ現在のTailscale通信が切り替わることを確認したうえで、`tailscale switch --list`の
登録済みProfileを調べ、参加先suffixと一致するProfileへ自動切替する。該当Profileが未登録なら
`tailscale login`でブラウザ認証を開き、利用者が認証を完了してから再検出・自動切替する。
LAN IPや一般公開URLではこの処理を行わない。

### 接続状態

| 状態 | 意味 |
| --- | --- |
| Offline | 未接続 |
| Connecting | 接続試行中 |
| Online | 接続済み。Handshake完了 |
| Synchronizing | 接続済みだがHandshake未完了、または同期中 |
| Reconnecting | 一度確立した接続が切れ、再接続中 |
| Conflict | Revision競合を検出 |
| Incompatible | Protocol / Project ID / Engine互換性で接続拒否 |
| Disconnected | 切断された |

Socketのエラー番号だけを表示することはしない。
Team Collaboration Window の **Connection Diagnostics** で次を確認できる。

```text
Server    ms.tailxxxx.ts.net:48000 (100.101.102.103)
Status    Connected
Latency   48 ms
Protocol  3
Engine    0.9.5+161
Project   my-game
Role      Client
Peers     2
Revision  4207
Last Sync 14:32:18
```

Tailscale固有の情報は表示しない。Latencyは診断用で、必須機能はこの値へ依存させていない。

### Protocol互換性

Handshake時に次を突き合わせる。

- `CollaborationProtocolVersion`（現行 **3**）
- `EngineVersion`
- `ProjectFormatVersion`
- `ScriptApiVersion`
- `ProjectId`

不一致は理由付きで拒否し、壊れた同期を続行しない。

```text
Connection rejected:
Collaboration Protocol 3 required. Client: 99
```

Handshakeを終えていないClientからのPayloadは中継・適用しない。

### Heartbeat / Timeout / Lock

遠隔ではPCスリープ、Wi-Fi切替、Tailscale再接続で、TCPが切れないまま無言になることがある。
Lockが永久に残らないよう、Heartbeatで生存を確認する。

```text
Client ── heartbeat (5秒間隔) ──▶ Server
Client ◀── heartbeatAck ─────────  Server   (送信時刻をそのまま返しLatency算出)
```

無応答が `20秒 + Grace 5秒` を超えたらDisconnected扱いとし、そのUserが持っていたLockを解放して
残りのClientへ `peerLeft` を配る。既存Lock設計に合わせ、瞬断で他人がすぐ触り始めないよう
Grace付きの安全側にしている。

### 自動再接続

```text
Connected → Network Lost → Reconnecting → Connected → Handshake → Revision確認 → 必要差分だけ再同期
```

再接続しただけでScene全体を無条件に上書きすることはしない。
`handshakeOk` でServerのRevisionを受け取り、既存のRevision比較・Conflict判定経路へ渡す。

### Asset転送の安全性

既存の512KiB超分割転送・Size/Hash検証・Temp受信→完了後置換をそのまま使う。
加えてServer側で中継前に検証する。

- 1Message 8MiB上限（それ以上は分割転送されているはずなので拒否）
- 1File 128MiB上限（既存制限を維持）
- Pathは深さを数えながら正規化し、`../`・絶対Path・ドライブ文字はProject外を指すため拒否

途中まで受け取った壊れたAssetでProjectを上書きしない。

### Tailscale自体の管理

Launcherは参加時の迷いやすい操作だけを補助する。現在のTailnet確認、登録済みProfileの検索、
確認後のProfile切替、未登録時のブラウザ認証起動をTailscale公式CLI経由で行う。
パスワード入力、SSO承認、Node Key生成、Invite承認、Tailnet作成、ACL変更は行わない。
秘密情報を受け取らず、認証そのものは必ずTailscaleのブラウザ画面で利用者が完了する。

参加先と現在のTailnetが異なる場合、Launcherは勝手に切り替えず確認画面を出す。
登録済みProfileを探索して参加先が見つからなかった場合は元のProfileへ戻す。
Tailscale未導入の場合は、一般的な接続エラーではなく導入が必要であることを表示する。

```text
Collaboration Serverへ接続できません
Host: ms.tailxxxx.ts.net:48000
```

Tailscale Node Key等の秘密情報をProjectやInviteへ保存しない。

### LANとの両立

Tailscaleが無い環境でも従来のLAN共同制作はそのまま使える。
`Server Host` に `192.168.x.x` を入れるか、従来どおりEditor内Host方式を使う。
Transportは同一実装なので、経路が変わっても上位の挙動は変わらない。

### 既知の制限

- **転送単位の再送は未対応。** 途中切断したAsset転送は再接続後に最初からやり直す。
  ただし不完全Fileを正式Assetとして扱わない保証（Temp受信・Size/Hash確認・完了後置換）はある。
- Serverは中継とLock/Revision管理のみで、Sceneの内容を解釈しない。
  そのためServer単体では、後から参加したClientへの初期Scene配信はできず、
  既存どおりHost/先行Client側の同期経路に依存する。
- Editor内Host方式では、Server側の集中Lock管理は使われない。
- Latency表示はClientのみ（Heartbeat往復から算出）。Host側は「未計測」表示になる。
- Owner/Editor/Viewer等の本格的なPermission Systemは持たない。

---

## 外部認識・Online・Haptics 利用ガイド

更新基準: 2026-09-26

この文書はゲーム制作者向けの操作ガイドである。Engine内部のThread、Memory、Backend所有関係は`engine-internals.md`、全Field一覧は`component-reference.md`、C++関数一覧は`script-api-reference.md`を参照する。

### 1. 最初に知ること

| 機能 | 最小構成 | 追加で必要なもの |
| --- | --- | --- |
| 音声Keyword | `SpeechRecognizer` | Windowsのマイク権限、Keyword、Input ActionまたはScript Action |
| 文字起こし | `SpeechRecognizer` | Windows Speech APIと言語環境 |
| 色/動き検出 | `CameraInput` + `ImageRecognizer` | WindowsのCamera権限 |
| 物体/分類/顔検出 | 上記2 Component | 対応ONNX ModelとLabel File |
| Haptics | `HapticSource` | 対応Device。無くてもゲームは継続 |
| Leaderboard/Cloud Save | Project SettingsのOnline Services | 公開済みWorker URL、Game ID、Client Key、Player ID |

外部DeviceやNetworkは必ず利用できるとは限らない。認識、振動、Online成功をゲーム開始の必須条件にせず、使えない場合の操作手段と保存手段を用意する。

### 2. 状態表示の読み方

| 表示 | 意味 | 次に見る場所 |
| --- | --- | --- |
| 利用不可 | Backend、Device、Model、設定が不足 | InspectorのBackend/Device/PathとError |
| 準備完了 | 初期化済みだが開始前、または入力待ち | Start On Play、ScriptのStart呼出 |
| 実行中 | 取得・認識・通信・振動が進行中 | 結果、FPS、推論時間、HTTP状態 |
| エラー | 開始または処理に失敗 | ConsoleとDebug Windowの最終Error |

メニューの`外部認識・オンライン` WindowにはSpeech、Vision、Online、Hapticsの状態をまとめて表示する。問題が起きたら最初に開く。

### 3. 音声KeywordでInput Actionを押す

1. 対象GameObjectを作る。
2. InspectorのComponent追加から`外部認識 → 音声認識`を追加する。
3. 認識モードを`キーワード`、Backendを`自動選択`または`Windows Speech API`にする。
4. 言語を`ja-JP`にする。
5. `マイク一覧を更新`し、空欄で既定マイクを使うかDeviceを選ぶ。
6. Keyword行へ例として`ジャンプ`、対応Input Actionへ`Jump`を設定する。
7. Action MapをInput Action Asset側のMap名と一致させる。
8. `Play 開始で認識開始`をONにしてPlayする。
9. Debug Windowで音量、認識文字列、Confidence、Backendを確認する。

同じActionへKeyboard/Gamepad Bindingも残しておくと、マイクが使えない環境でも操作できる。

Keywordが反応しない場合は、音量が0でないこと、言語、Keyword表記、Confidence、Action Map名、Action名の順で確認する。英字は大文字小文字と空白を正規化するが、日本語の別表記や同音語を自動統合しない。

### 4. 音声結果をC++ Scriptで受ける

```cpp
void VoiceController::Start() {
    Speech speech(GetGameObject());
    speech.Start();
}

void VoiceController::Update(float deltaTime) {
    Speech speech(GetGameObject());
    const std::string text = speech.GetLastText();
    const float confidence = speech.GetLastConfidence();

    if (!text.empty() && text != lastHandledText_) {
        lastHandledText_ = text;
        // 確定済みの直近結果を処理する。
    }
}
```

ゲーム内のTextへ状態と認識文字列を表示する場合は、音声認識ObjectとText Objectを分けて次のように更新する。

```cpp
void VoiceController::Update(float deltaTime) {
    Speech speech(GetGameObject());
    speech.SetUiText(statusTextObject_);

    // 文言を独自にしたい場合は個別状態を使う。
    if (speech.IsSpeaking()) {
        Ui(statusTextObject_).SetText("話しています...");
    }
    else if (speech.IsProcessing()) {
        Ui(statusTextObject_).SetText("音声を文字にしています...");
    }
}
```

`GetActivityText()`は`話し中`、`推論中`、`認識待機中`、`停止中`のいずれかを返す。`GetDisplayText()`は発話・推論中なら状態を返し、完了後は直近の認識文字列を返す。Whisperは推論が終わるまで発話内容の文字列を持たないため、発話中に未確定文字列を推測表示しない。

通常はKeyword→Input ActionまたはInspectorの`認識時 Action`を使う。毎FrameのPollingが必要なUI表示だけ`Speech`の表示APIを使う。

### 5. Cameraで動きを検出する

1. Camera取得用GameObjectへ`外部認識 → カメラ入力`を追加する。
2. `Camera一覧を更新`し、Deviceを選ぶ。空欄なら既定Cameraを使う。
3. 最初は640×480、30 FPSにする。
4. 認識用GameObjectへ`外部認識 → 画像認識`を追加する。
5. 認識モードを`動体検出`、Backendを`自動選択`または`内蔵`にする。
6. `映像元 Camera`へ手順1のGameObjectを指定する。未指定なら開いているCameraのうちIDが最小のものを使う。
7. 推論間隔を0.1〜0.2秒程度から始める。
8. 発火条件を`動き検出`、Action名を例として`Wave`にする。
9. Playし、Camera FPS、動き量、検出中心を確認する。

検出位置は`WavePosition`のように`Action名 + Position`のVector2 Actionへも流れる。中央が原点になる-1〜1の値として扱う。

### 6. 特定色を追跡する

1. ImageRecognizerの認識モードを`色追跡`にする。
2. `追跡色 (RGB)`を0〜1で設定する。
3. 許容差を小さく始め、照明変化で途切れる場合だけ上げる。
4. 小さなノイズを拾う場合は最小面積比を上げる。
5. 発火条件を`色検出`にする。

色追跡は追加Model不要である。Cameraの自動露出、White Balance、部屋の照明でRGBが変わるため、実際の設置環境で調整する。

### 7. ONNXで物体・分類・顔を認識する

1. 認識モードを`物体検出`、`画像分類`、`顔検出`のいずれかにする。
2. Backendを`ONNX Runtime`または`自動選択`にする。
3. Project内の`.onnx`をModelへ指定する。
4. 1行1Labelの`.txt`または`.names`を指定する。空欄ならModelと同名のFileを探す。
5. Confidenceと推論間隔を設定する。
6. Debug WindowでBackend名、推論ms、Label、Boundsを確認する。

ONNX Fileであれば何でも同じ出力になるわけではない。入力Shape、色順、正規化、出力LayoutがEngineのDecode方式と合う必要がある。LoadできてもLabelやBoxが不正ならModel契約を確認する。

顔ランドマーク、頭部方向、OpenCV、MediaPipeは現在未対応である。選んだ場合は別機能へ置き換えず利用不可になる。

### 8. Cameraと認識の性能調整

調整順は次にする。

1. Cameraを複数Recognizerで共有する。
2. 推論間隔を0.2秒、0.33秒などへ増やす。
3. Camera FPS上限を30または15へ下げる。
4. 解像度を1280×720から640×480へ下げる。
5. Debug Previewを不要時にOFFにし、外部認識Windowも閉じる。Previewは64×48の色付き矩形をCPU Frameから毎回作る。
6. 複数ONNX Modelを同時実行せず、ゲーム状態に応じてStart/Stopする。

BGRA Frameは640×480で約1.17 MiB、1920×1080で約7.91 MiBである。内部Copy、推論Tensor、Preview Textureもあるため、解像度×4 Byteだけが全使用量ではない。

### 9. Hapticsを追加する

1. 振動させるGameObjectへ`FeelKit → FeelKit 触覚ソース`を追加する。
2. 強さ、持続時間、Pattern、Channel、周波数を設定する。
3. Inspectorの`プレビュー`でPlay前に確認する。
4. `自動再生`をONにするとPlay開始時に再生する。
5. Project Settingsの`振動の強さ`で全体倍率を設定する。

`.haptic` Clipを指定した場合はClip値を基準にする。Clipが空ならInspector値を使う。

#### 衝突に合わせる

1. `Physics Reactive`をONにする。
2. `最大 Impulse`へ強度1.0になる衝突Impulseを設定する。
3. 衝突処理から`Haptic(gameObject).PlayFromImpulse(contactImpulse)`を呼ぶ。

小さい衝突まで最大振動になる場合は最大Impulseを上げる。内部で勝手なDamage倍率は加えない。

#### Audioに合わせる

1. Audio Assetを`サウンド`へ指定する。
2. `Audio Reactive`をONにする。
3. 低域/全域/高域、感度、強度倍率を調整する。

Device未接続でもGameplayは継続する。振動したかどうかを攻撃成功やScene進行の判定に使わない。

### 10. HapticsをScriptから制御する

```cpp
Haptic haptic(GetGameObject());
HapticVoice voice = haptic.Play();

if (voice.IsValid()) {
    voice.SetIntensity(0.6f);
    voice.SetFrequency(18.0f);
    voice.SetLooping(false);
}
```

`HapticVoice`は再生Handleを包む。再生終了後やStop後のHandle操作は失敗するため、戻り値を確認する。Sceneを越えてHandleを保存しない。

### 11. Online Servicesを設定する

1. Cloudflare Workerを`Tools/CloudflareWorker/README.md`の手順で準備する。
2. 開発用D1、KV、R2を作り、`wrangler.toml`のIDを設定する。
3. `wrangler secret put DEVELOPMENT_CLIENT_KEY`で公開照合用Keyを登録する。
4. `wrangler dev`または`wrangler deploy`でURLを得る。
5. Project Settingsの`Online Services`を有効にする。
6. Provider、Development Base URL、Production Base URL、Game ID、Client Key、Timeout、Queue上限を設定する。
7. 開発中はEnvironmentをDevelopmentにする。
8. Player IDとPlayer Nameをゲーム開始時にScriptから設定する。
9. Debug WindowでActive URL、接続状態、HTTP Status、Pending Queueを確認する。

本番は`--env production`で別のD1/KV/R2へDeployする。開発ScoreやSaveを本番Databaseへ混ぜない。

### 12. Leaderboardの使用例

```cpp
void OnlineController::Start() {
    Online::SetPlayerIdentity("player-001", "Player");
    Online::SubmitScore("main", 12500, OnlineLeaderboardScope::Global);
    Online::RequestTopScores("main", 20, OnlineLeaderboardScope::Global);
}

void OnlineController::Update(float deltaTime) {
    const int32_t count = Online::GetLeaderboardCount();
    for (int32_t index = 0; index < count; ++index) {
        OnlineLeaderboardEntry entry{};
        if (Online::TryGetLeaderboardEntry(index, entry)) {
            // entry.playerName、entry.score、entry.rankをUIへ表示する。
        }
    }
}
```

取得系は非同期である。Request直後の同じ行で結果が返る前提にせず、以降のFrameで件数を確認する。通信失敗時にGETは再送Queueへ残らないため、画面側に再読込Buttonを用意する。

### 13. Player DataとCloud Save

- Player Dataは小さいKey/Value設定に使う。
- Cloud SaveはSlot名とSave文字列を使う。
- Uploadは通信断時の再送対象になる。
- Downloadは古いRequestを自動再送しない。
- 64 KiBを超えるSaveは参照WorkerがR2へ切り替える。ただし現行のC++高水準Wrapper `Online::GetCloudSave()`は65,536 Byteの固定Bufferを使うため、ゲームScriptから安全に往復させるSave本文は終端を含め65,536 Byte未満にする。大型R2 Data用の可変長Script APIは未実装である。
- Local Saveを消してからCloud Upload成功を確認する運用にしない。

Cloud Saveの競合解決、世代管理、暗号化、Account認証はゲーム側要件に応じて追加する。現在のPlayer IDだけを本人認証とみなさない。

### 14. Online Securityと個人情報

- Client Keyは配布Gameから読める公開値である。
- `ADMIN_KEY`、Cloudflare Token、Database資格情報をProject Settingsへ入れない。
- Pending Queueは平文なのでPasswordや秘密Tokenを送信Bodyへ入れない。
- Player IDへ本名、メールAddress、端末固有IDをそのまま使わない。
- Camera画像とマイク音声を参照Workerへ送る実装ではない。認識はLocal Backendで行う。
- Camera/マイクを使う理由とON/OFF手段をゲーム内で説明する。
- Server側でScore、Player ID、Rate Limit、Save Sizeを必ず再検証する。

### 15. Debug Windowの見方

#### Speech

- 音量が0ならDevice、Windows権限、既定入力を確認する。
- 認識文字列は出るがActionが来ない場合はKeyword、Action Map、Action名を見る。
- Confidence未満の確定結果はSession側で捨てられる。

#### Vision

- Camera State、Device名、解像度、取得FPSを見る。
- 認識State、Backend名、推論msを見る。
- Camera FPSが0ならRecognizerより先にCamera側を直す。
- Cameraは動くが結果がない場合はMode、Model、Label、Confidence、Intervalを見る。

#### Online

- Active Base URLがEnvironmentに合うか確認する。
- Status 0は通信未到達、HTTP 4xx/5xxはServer到達済みとして分ける。
- Pending Queueが増え続ける場合はURL、Network、Worker状態を直す。

#### Haptics

- Device State、Backend名、再生中Voice数、出力強度を見る。
- 出力強度があるのに振動しない場合はDevice接続とBackend対応を確認する。

### 16. よくある症状

| 症状 | 確認順 |
| --- | --- |
| マイクが出ない | Windows権限 → 他Applicationの占有 → Device更新 → 既定Device |
| Keywordが来ない | 音量 → 言語 → Keyword表記 → Confidence → Action名 |
| Cameraが黒い | Windows権限 → 他Application → Device名 → 対応解像度 → FPS |
| 推論が重い | Interval → 解像度 → FPS → Recognizer数 → Model |
| ONNX結果が不正 | 入力Shape → RGB/BGR → 正規化 → 出力Layout → Label順 |
| 振動しない | Device State → Master Intensity → Component強度 → Channel |
| Scoreが送れない | Enabled → URL → Environment → Game ID/Client Key → HTTP Status |
| Pendingが消えない | Network/Worker復旧 → `今すぐ再送` → Retry上限 → Body Size |
| Script DLLがLoadしない | DLLがEngineより新しいAPIを要求していないか、x64、Debug/Release、依存DLLを確認。古いAPIのDLLは互換Loadされる |

Engine更新でScript API Versionが変わった場合も、既存Scriptを削除して同名で作り直す必要はない。InspectorのScript Componentから`Debug DLLをビルド`または`Release DLLをビルド`を押すと、ユーザーが編集する`.h` / `.cpp`は保持したまま、Engine管理の`.Generated.cpp`、`build_debug.bat`、`build_release.bat`を現行Engine用へ更新してからビルドする。補助ファイルが欠けている場合も同じ操作で再生成する。

### 17. Scene、Prefab、複製時の注意

- ImageRecognizerのCamera参照はPrefab/Scene取込時に新IDへRemapされる。
- Prefab外のCameraを参照したまま単体保存すると、配置先で参照が`-1`になる場合がある。
- その場合はInspectorでCameraを再指定するか、同じPrefab階層にCameraInputを含める。
- Runtimeの認識結果、通信結果、HandleはSceneへ保存されない。
- Play中に得た結果を初期値として残したい場合は、必要な値だけSaveSystemへ明示保存する。

### 18. Standalone配布前チェック

- [ ] Camera/マイクを使わない環境でも起動して遊べる。
- [ ] WindowsのCamera/マイク権限拒否時に説明が出る。
- [ ] ONNX ModelとLabel AssetがBuildへ含まれる。
- [ ] 新しいAPIを使用するScriptだけAPI Version 15 Headerで再Buildした。既存APIだけの旧DLLはそのまま使用できる。
- [ ] FeelKitと必要Runtime DLLの配布条件を確認した。
- [ ] Development URL/Databaseを本番Buildへ残していない。
- [ ] Production Worker、D1、KV、R2を分離した。
- [ ] SecretをGit、Scene、Project Settings、Logへ入れていない。
- [ ] Offline時のLocal Save、再試行UI、Fallback操作を用意した。
- [ ] Debug Previewと過剰ログを製品設定でOFFにした。
- [ ] 別PC、別User権限、Deviceなし、Networkなしを確認した。

### 19. 現在の制限

- Whisper音声認識はローカルの`whisper-cli.exe`とwhisper.cpp用Modelが必要。
- ONNX音声認識Backendは未実装。
- 顔ランドマークと頭部方向は未実装。
- OpenCV/MediaPipe Vision Backendは未実装。
- Camera FrameはCPU BGRA経路で、GPU Zero-copyではない。
- ONNXの任意Model形式を自動判定するものではない。
- OnlineはAccount認証Serviceそのものではない。
- Workerは64 KiB超をR2へ置けるが、C++高水準WrapperのCloud Save取得は固定65,536 Byte Bufferである。
- Pending Queueは暗号化Storeではない。
- HapticsはDevice能力により左右ChannelやFrequency表現が近似される。
- OpenGL Backendはなく、描画はDirectX 12である。

### 20. 関連文書

- [engine-internals.md](engine-internals.md)
- [engine-internals.md](engine-internals.md)
- [component-reference.md](component-reference.md)
- [script-api-reference.md](script-api-reference.md)
