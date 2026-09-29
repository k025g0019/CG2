# CG2Engine 全体内部設計

更新基準: 2026-09-26

この文書は実装者向け情報の集約先である。現行機能と未対応範囲、コード構造、所有権、Frame順、Runtime、DirectX 12描画、Memory、保存形式、Asset、Lighting、共同制作、外部認識・Online・Hapticsをこの1冊で扱う。Component全FieldとScript API全関数だけは検索量が大きいため専用リファレンスに分離する。

主要部は「全体構造」「現行機能仕様」「Runtime」「描画パイプライン」「保存・互換性」「Asset・Hot Reload」「Lighting・GI」「共同制作内部」「外部認識・Online・Haptics実装」の順に並ぶ。文書内検索ではManager名、Component名、保存行名、Pass名、Thread名を使う。

## 1. 実行形態

同じ実行ファイルが起動条件によって複数の役割を持つ。

| 形態 | 判定・入口 | 動作 |
| --- | --- | --- |
| Editor | 通常起動 | Project互換確認後、全Editor WindowとPlay Runtimeを初期化 |
| Standalone Game | 実行ファイル横の`game.build` | Editor UIを省き、起動SceneをLoadして直ちにPlay |
| Game Build CLI | `--build-game` | `GameBuildSettings.cg2`を読み、配布物を書き出して終了 |
| Migration | `--migrate-project` | Project BackupとFormat Migrationを実行して終了 |
| Environment Check | `--check-environment` | 必須DLL、SDK、Runtime等を検査して結果を保存 |
| 開発用Scene生成 | 専用Command Line | Water Rail Shooter等のScene Builderを実行して終了 |

Windowsの入口は`WinMain`である。Project指定がある場合は最初にCurrent DirectoryをProject Rootへ変更する。Editor起動前にEngine Version、Project Format、Script API Versionの互換性を評価し、保存できない組合せでは処理を続けない。

## 2. 最上位の所有関係

```text
WinMain
└─ GameScene
   ├─ EditorPlatformManager       Win32 / DX12 / Input / Audio / ImGui基盤
   ├─ EditorSceneLifecycleManager Scene / Runtime / Selectionの初期化と更新
   ├─ EditorFrameInputManager     1Frameの入力とScene Camera
   ├─ 各Editor Window Manager     Menu / Scene / Game / Hierarchy / Inspector等
   ├─ EditorTeamCollaborationManager
   └─ EditorRenderManager         GPU Command発行とPresent

EditorSceneLifecycleManager
└─ EditorRuntimeManager
   ├─ Physics / Script / Input / Animation / Audio / Effect
   ├─ AI / Navigation / Damage / Weapon / Pool
   ├─ Rail / Wave / Sequence / Save / Replay
   └─ Profiler / Log Monitor
```

`main -> GameScene -> GameObject -> Component`を基本階層とし、`GameScene`は順序制御だけを担当する。Gameplay処理は`EditorRuntimeManager`配下のManagerへ分け、各Managerは共通の`EditorScene`を参照する。

## 3. Source Directoryの責務

| Directory | 内容 |
| --- | --- |
| `Core` | 起動、Window、Version、Project Settings、Script ABI、共有型、Math |
| `Editor` | Scene、Component、Editor UI、Play Runtime、Gameplay Manager、Build |
| `Renderer` | GBuffer、Temporal、PostProcess、GPU Culling、Depth、Probe、Ocean FFT |
| `Asset` | Asset種別、Registry、Import Settings、Hot Reload通知 |
| `Physics` | 基本Collision Utilityと物理SDK接続補助 |
| `Animation` | Animation GraphとProperty Animation Clip |
| `Audio` | Audio Asset/Decoder等の音声基盤 |
| `Input` | Input Action AssetとBinding |
| `Navigation` | Navigation Query等の経路基盤 |
| `AI`相当 | 主に`EditorAIManager`とThirdParty BehaviorTree/OpenSteer/Recast接続 |
| `Effect` | 独自Effect、VFX、Effekseer Runtime |
| `Collaboration` | Transport Interface、TCP実装、共通Protocol |
| `External` / `Speech` / `Vision` / `Haptics` / `Online` | 外部Device・認識・通信の共通状態、Backend Interface、OS/SDK接続 |

`Tools/CG2Launcher`はEngine/Project配布、Version固定、参加コードを扱う別Executable、`Tools/CG2TeamServer`は共同制作の中継とRevision確定を扱う別Executableである。

## 4. 共有状態と依存注入

Editor全体で頻繁に参照するScene、選択、Viewport、Camera、GPU Resource、Window表示状態は`EditorSharedState`に置かれている。既存構造との互換性のためGlobal Stateを使用しているが、新しいGameplay機能はGlobalへ直接増やさず、所有ManagerへPointerを渡す。

主な依存注入は`EditorRuntimeManager::Initialize()`で行う。例としてScript ManagerにはInput、Animation、Effect、Audio、AI、Physicsを渡し、Weapon ManagerにはTargeting、Physics、Damage、Pool、Script、VFX、Audio、Camera Effectを渡す。Manager間の循環所有は避け、Pointerは参照のみで寿命はRuntime Managerが保証する。

## 5. 起動順

1. Standalone ManifestまたはEditor Projectを判定する。
2. `EditorPlatformManager`がWin32 Window、DirectX 12、DirectInput、XAudio2、ImGuiを初期化する。
3. Asset Manager Adapterを登録する。
4. `EditorSceneLifecycleManager`がScene、Runtime、選択、Scene Object同期を初期化する。
5. Standaloneなら起動SceneをLoadしてPlayへ入る。
6. Editor Window Managerを初期化する。
7. Editor時だけAsset RegistryをDiskから一度構築し、共同制作Managerを初期化する。
8. 最後にRendererを初期化する。

Asset Registryの初期走査は常駐File Watcherではない。その後の更新はProject Window、Importer、共同制作等から`NotifyFileChanged`系の明示通知で伝える。

## 6. 1Frameの処理順

### Update

```text
Platform message
 -> Frame Input / Scene Camera / resize
 -> Scene Lifecycle（Play RuntimeとScene同期）
 -> ImGui NewFrame
 -> Editor各WindowのUpdate
 -> Team Collaboration Update
 -> Render Manager Update
```

StandaloneはImGui Frame開始後、Game ViewとRendererだけを更新する。終了要求が出たFrameは後段を呼ばない。

### Draw

```text
Platform Draw準備
 -> Runtime Debug Draw
 -> Main Menu / Dock /各WindowのImGui構築
 -> ImGui DrawData確定
 -> Rendererが3D/2D/PostProcess/ImGuiをGPUへ発行
 -> Multi-Viewport Window描画
 -> Profiler Frame History
 -> FPS Limit
```

PV撮影Modeでは通常Dock Windowを隠し、Game Viewと撮影用調整UIを優先する。StandaloneではGame View、ImGui、Rendererだけを描画する。

## 7. SceneとGameObjectデータモデル

`EditorScene`はScene UUIDと`EditorGameObject`配列を所有する。GameObjectは安定UUID、整数Runtime ID、Name、Parent、Local Transform、Active状態、Component配列、Prefab Linkを持つ。Componentは`EditorComponentType`と多数の型別Fieldを持つ共通構造体である。

この共通構造は次の機能で同じ値を共有するために使われる。

- Inspector編集
- Scene/Prefab Serialization
- Runtime ManagerによるComponent実行
- C++ Script Runtime API
- 共同制作の差分識別
- Undo/Redo、Clipboard、Duplicate
- Log MonitorとDiagnostics

GameObjectとComponentの同期識別には表示名や配列IndexではなくUUIDを使う。整数IDは現在Process内の高速参照用で、Prefab配置、Scene Merge、Additive Load時には衝突を避けて再割当する。

## 8. Scene保存と互換性

Scene/PrefabはUTF-8の行指向Text形式で、先頭にFormat種別とVersionを持つ。保存は一時Fileへ全内容を書き、成功後に置換する。途中失敗で既存Sceneを破壊しない。

Load時に現Buildが解釈できない行は`unknownSerializedLines_`へ保持し、再保存時に末尾へ書き戻す。新しいEngineで作った追加情報を古いBuildが開いただけで消す事故を抑える。ただし未知FieldをRuntime実行できるわけではない。

Prefabは保存、Instantiate、Apply、Revert、Variantと複数の明示Overrideを持つ。UnityのNested Prefab/Property Override全般と同等ではないため、外部参照、階層、Variantの往復を個別に確認する。

## 9. Play ModeとRuntime寿命

Play開始時に編集Scene全体を`sceneBackup_`へCopyし、Runtime Systemを順序付きでStartする。Stop時はManagerを逆依存順に停止し、一時ObjectやPhysics参照を破棄した後、編集SceneをBackupへ戻す。

Startの重要な順序は次のとおり。

1. Pool準備、Blast Chunk準備
2. Physics開始
3. Scene Optimization、Pool、Damage、Targeting、Weapon
4. Effect、Animation、Movement、Rail、AI、Navigation、Audio
5. Script Start
6. Pool Prewarm
7. Runtime Property、Gameplay Event、Save、Log Monitor

Blastは子Chunkを無効化してからJolt Bodyを構築する。Stopでは先にJoltを停止してからBlastの一時Chunkを破棄する。Pool PrewarmはPlay中の単発Hitchを避けるためScript/Physics Start後にまとめて行う。

## 10. Runtime Update順

Play中の主順序は次のとおり。

1. Replay入力を解決し、Unscaled TimeとGame Time Scaleを確定する。
2. Input Actionを更新する。
3. Scene Optimization、Targeting、C++ Script Update、Damage。
4. Scene Load/Unload要求を処理する。
5. Local/Rail/Rolling Movement、Rail Branch。
6. Action Sequence、Wave、Object Pool、Gameplay Event。
7. Weapon、Loadout、Blast、Runtime Property。
8. AI、Navigation。
9. Physics Fixed Step。1Frame最大4 Step。
10. Physics EventをScriptへ渡して同回数のScript FixedUpdate。
11. Animation、Constraint、Effect、Effekseer、VFX。
12. Audio、Haptics、UI Binding、Camera Effect、Log Monitor。

Scene Transition、非同期Load、Automatic Streaming中はSceneが差し替わるため、通常Gameplay Updateを早期終了して専用処理へ集中する。

## 11. C++ Script内部契約

Script DLLは`EditorScriptLoadFn`を公開し、Engineが`EditorScriptRuntimeApi`の関数Tableを渡す。現行API Versionは15である。新規関数Pointerは構造体末尾へ追加して既存Entry位置を維持する。Version 15以降の生成DLLは`EditorScript_GetRequiredApiVersion`を公開し、EngineはそのVersion範囲で初期化する。Exportを持たない旧DLLはVersionを降順探索するため、Engineより古いAPIだけを使うDLLは再Buildせず互換Loadできる。

`EditorScriptManager`はDLL Load、Instance生成、公開Field、Update/FixedUpdate、Collision/Trigger、Action、Scene要求を管理する。高水準WrapperはRuntime API Pointerが無い場合やHandleが無効な場合に失敗値を返し、直接Crashしない設計にする。

Engine更新後の既存Scriptは`EditorNativeScriptAssetManager::RefreshNativeScriptSupportFiles`で移行する。ユーザーの`.h` / `.cpp`は保持し、Engine管理の`.Generated.cpp`、`build_debug.bat`、`build_release.bat`だけを再生成する。Inspectorのビルドボタンがこの更新を先に行うため、Scriptの削除・同名再作成は不要である。

Hot Reloadは新DLLを別途Loadして必須EntryとAPI初期化を検証し、成功時だけ旧Moduleと交換する。Native DLL内部のAccess ViolationまではEngine内で安全継続できない。

## 12. Physics

`EditorPhysicsManager`が共通窓口、`EditorJoltPhysicsManager`が3D Simulationを担当する。Play開始時にSceneのRigidbody/ColliderからBodyを作り、Fixed Step後にTransformと接触EventをSceneへ戻す。

Collider DebugはEdit中のComponent形状とPlay中の実Jolt Shapeを区別する。Box系のImGui OverlayはDepth Bufferを使えないため、Cameraを向いた面の外形線だけを描く。Scene CameraのZoomはWorld Collider Sizeを変えない。

Terrainは描画と同じHeight式でCollider格子を作る。AutoConvexは生成失敗時にBoxへFallbackする。Mesh Colliderは形状を使い、誤解を招くBounds BoxをDebug表示しない。

## 13. Rendering

RendererはDirectX 12を使用し、Scene ViewとGame ViewのViewport、Camera、Transform Bufferを分けて処理する。主要な流れは次のとおり。

```text
Shadow / Reflection準備
 -> Opaque / Alpha Cutout / GBuffer
 -> Planar Reflection / Reflection Mask
 -> Weighted OIT / Refractive Surface
 -> Depth Pyramid / Normal Reconstruction
 -> Frustum + Hi-Z Culling
 -> SSAO / SSGI / SSR / Volumetric
 -> Temporal Resolve
 -> Underwater / Bloom / Glare / DOF / Motion Blur
 -> Auto Exposure / Final Composite / Filter
 -> AA（None / FXAA / SMAA / Temporal）
 -> ImGui / Present
```

PostProcess ComponentはBloom threshold/soft knee/scatter、AA mode、SMAA threshold/corner rounding、Temporal sharpness/blend、Final Compositeの露出・White Point・Tone Mapping・彩度・Contrast・Vignette・Film Grain・Chromatic Aberration・AO強度等へ接続されている。AA Noneは専用Passthrough PSOを使用する。

Temporal ManagerはScene ViewとGame Viewに別のHistory、Previous Depth、Write Indexを持つ。両Viewportを処理するFrameでもCamera履歴を混ぜない。

GPU CullingはFrustum/Hi-Z判定をGPUで行い、結果を次Frameの`SetPredication`から直接参照する。CPU Readbackは行わないが、`ExecuteIndirect`による完全GPU駆動描画ではない。

## 14. Asset管理

`AssetManager`は変更通知と種別別Reload、`AssetRegistry`はAsset ID、Path、Hash、依存・逆依存を担当する。Pathからの種別判定は`DetermineAssetTypeFromPath()`へ集約する。

RegistryはScene/Prefab等のTextから`Assets/`と`resources/`参照を抽出し、Project WindowのMissing表示、削除警告、Build収集、共同制作Catch-upへ使う。移動はRegistry経由で行うことでAsset IDを維持する。

Model、Texture、Audio、VFX等は可能な範囲でHot Reloadする。AnimationとScriptはPlay状態やDLL安全性によりStop/再Buildを要求する場合がある。

## 15. Animation・Effect・Audio・Input

- Animation: Property Clip、Animator Graph、State/Transition/Any State/Blend Tree/Event、Root Motionを扱う。
- Effect: 独自Effect、CPU VFX、GPU Particle、Effekseerを別Managerに分離する。
- Audio: XAudio2 Voice、2D/3D、Bus、Fade、Loop、Pitch、距離減衰を管理する。
- Input: Keyboard、Mouse、XInput最大4台、Input Action/Bindingを共通化する。
- Haptics: Audio連動SourceとFeelKit系ComponentをRuntimeから更新する。
- External Integration: Speech/Vision/Online/HapticsはComponent → Manager → System → Interface → Backendの層を守り、SDK型とWorker ThreadをGameplayへ出さない。詳細は本書を参照する。

Animation Layer、Avatar Mask、Nested State Machineは現行Animator Graphの対象外である。Effect系はAsset形式ごとにRuntimeが異なるため、同じHandleに見えてもPause/Transform等の対応範囲が異なる。

## 16. Navigation・AI・Gameplay基盤

NavigationはSurface、Agent、Obstacle、Linkと経路Queryを持つ。現行経路は障害物を考慮した折れ線で、複雑な迷路向けの完全なNavMesh A*実装ではない。

AIはSensor、Behavior Tree、Steering、Blackboard相当のComponent/Runtimeを管理する。Gameplay基盤はTargeting、Damage、Weapon、Projectile、Object Pool、Wave、Rail、Sequence、Difficulty、Objective、Save、Replay等をManagerへ分離し、Script Actionで接続する。

汎用ComponentはScene固有名へ分岐せず、対象ID、Action名、設定値をInspectorまたはScriptから渡す。特定ゲームの永続Scene生成はScene Builder側へ置く。

## 17. Editor UI

ImGui Dockingを使用し、Main Menu、Scene View、Game View、Hierarchy、Inspector、Project/Console、Animation、Gameplay Tools、Diagnostics、Log Monitor、TEAM、Hook/Wire Debug、PV ShootをWindow Managerへ分離する。

Scene Viewは選択、Gizmo、Camera、Drag & Drop、物理Debug、Navigation Debug、共同制作Markerを統合する。Hierarchy/Inspector/Project Viewは同じScene/Assetデータを編集し、変更後にSelection、Scene Object、Asset Registryへ通知する。

UI操作の多くはImGui矩形が確定するDraw中に発生するため、Window ManagerのUpdateが空でも異常ではない。

## 18. 共同制作

共同制作の内部詳細は本書を正とする。Engine本体はScene/Asset/Lock/Presence/TeamItemを扱い、CG2TeamServerはProject識別、Heartbeat、Revision、履歴中継を担当する。Launcherは参加コードと初期Project/Engine取得を担当する。

Play中のLocal Runtime変更は送信せず、Remote編集はStop後まで保留する。Stop時は編集Scene Backupを復元してからRemote変更をRevision順に重ねる。

## 19. Project、Launcher、Build

Project Version Metadataは必要Engine Version、Project Format、Script API、Update Channelを持つ。LauncherはEngineをVersion別Directoryへ導入し、Projectを特定Versionへ固定する。

Game Buildは起動Sceneと遷移可能Scene、参照Asset、Runtime DLL、Shader、ThirdPartyを収集する。DevelopmentとReleaseを分け、結果を`BuildLogs`へ残す。Release Build前には必要な生成Assetを検査し、Blast等の未Bakeデータを処理する。

## 20. Diagnosticsと安全策

- ProfilerはCPU/GPU Sample、Frame History、VRAM、Subsystem統計を保持する。
- Log MonitorはGameObject/Component/System Fieldを横断して監視する。
- Scene Saveは一時File置換、Asset上書きはBackup、共同削除はTrashを使う。
- Crash Handlerは致命的例外の情報を残すが、破損したNative Scriptを継続実行する保証ではない。
- Project MigrationはBackup後に行い、互換性が無い場合は保存を禁止する。

## 21. 拡張規約

### Component追加

1. `EditorComponentType`の既存順を変えず末尾へ追加する。
2. Default、Inspector、Save、Load、Duplicate、Prefab、Runtime、Script API、Log Registryを揃える。
3. Runtime値と編集設定を分け、Play結果をScene初期値へ保存しない。
4. Component詳細文書と全Field Registryを更新する。

### Manager追加

1. 所有者を`GameScene`または`EditorRuntimeManager`のどちらかに決める。
2. Initialize/Start/Update/Stopの依存順を明記する。
3. 他Managerを所有せず、必要なInterface/Pointerだけを受け取る。
4. Profiler Sample、Console Error、無効状態の安全動作を用意する。

### Renderer追加

1. Resource所有ManagerとPass実行位置を決める。
2. Scene/Game ViewのCamera、Viewport、Temporal Historyを混同しない。
3. Resource State、Descriptor予算、Resize、History Resetを定義する。
4. Component設定を追加した場合はInspector/Serialization/Render Pathまで接続する。

### Script API追加

1. Runtime API構造体末尾へ追加する。
2. API Versionを上げ、旧Version Guardを付ける。
3. 高水準Wrapperと無効Handle Testを追加する。
4. Script詳細文書の全関数表へ追記する。

## 22. 既知の構造的負債

- `EditorScene`と`EditorRenderManager`は責務・File Sizeが大きい。
- `EditorSharedState`にGlobal ResourceとUI状態が集中している。
- GPU Cullingは完全なGPU駆動描画ではない。
- 一部の詳細文書は追加時点の件数や評価を履歴として含み、現行値との区別が必要である。
- Build成功はPlay、描画、2台通信、実機入力の成功を意味しない。検証結果は段階別に記録する。

## 23. 関連文書

- [component-reference.md](component-reference.md)
- [script-api-reference.md](script-api-reference.md)
- [user-guide.md](user-guide.md)
- [user-guide.md](user-guide.md)
- [user-guide.md](user-guide.md)

---

## 現行機能仕様と対応範囲

更新基準: 2026-09-27

この文書は「現行Buildに何があり、どこまで動作する設計か」を分野横断で一覧化する。個別Fieldや全APIの重複記載は避け、詳細文書への入口と未対応範囲を明示する。内部コード構造は本書を参照する。

### 1. 対象環境

| 項目 | 現行仕様 |
| --- | --- |
| OS | Windows 64 bit |
| Graphics | DirectX 12 |
| Editor UI | Dear ImGui Docking + ImGuizmo |
| Audio | XAudio2 |
| 3D Physics | Jolt Physics |
| Model Import | Autodesk FBX SDK、OBJ等 |
| Native Script | C++ DLL、Runtime API Version 15 |
| Effect | 独自Effect/VFX/GPU Particle/Effekseer |
| AI/Navigation | BehaviorTree.CPP/OpenSteer/Recast系依存とEngine側Manager |
| Project配布 | CG2Launcher、Version固定、Hub、Invite、参加コード |
| 共同制作 | TCP/LAN/VPN、CG2TeamServer、Protocol 3 |

Linux/macOS Editor、Vulkan/Metal Renderer、Web Buildは現行仕様に含まれない。

### 2. 実装状態の表記

| 表記 | 意味 |
| --- | --- |
| 実装済み | Source経路があり、設定から実処理まで接続されている |
| 部分対応 | 主経路はあるが、一般的な同種製品の全機能または一部導線がない |
| 要実機確認 | Source上は接続済みだが、この文書更新時にGUI/複数PC/実デバイスで再確認していない |
| 未対応 | 現行Sourceに機能経路がない、または明示的に対象外 |

### 3. 起動・Project・Version

- 通常Editor、Standalone Game、Build CLI、Migration、Environment Checkを同一Engine実行ファイルから起動できる。
- Projectは必要Engine Version、Project Format、Script API Version、Update ChannelをMetadataに保存する。
- 互換性が無いProjectは保存を許可せず、必要に応じBackup付きMigrationまたはLauncherでのEngine切替を案内する。
- Project Settingsは解像度、Window Mode、VSync、FPS上限、Audio Bus、Gamepad Dead Zone/感度、起動Scene/Build Sceneを持つ。
- Standaloneは`game.build`から起動Sceneを読み、Editor UIを表示せずPlayを開始する。

### 4. Editor Window

| Window/機能 | 状態 | 主用途 |
| --- | --- | --- |
| Main Menu | 実装済み | Scene、Project設定、Build、Play/Stop、Window表示 |
| Scene View | 実装済み | 選択、Camera、Gizmo、配置、Debug、共同制作Marker |
| Game View | 実装済み | Active Cameraからのゲーム出力 |
| Hierarchy | 実装済み | 親子構造、複数選択、生成、削除、Duplicate、TEAM Badge |
| Inspector | 実装済み | Transform/Component編集、追加削除、Runtime値、TEAM Badge |
| Project View | 実装済み | Asset作成、選択、移動、Reimport、依存、TEAM Badge |
| Console | 実装済み | Engine/Runtime/Build/共同制作Message |
| Animation | 実装済み | Property Clip、Timeline、Key、Event、Preview/Record |
| Animator Graph | 部分対応 | State、Transition、Any State、Blend Sample。Layer/Avatar Mask/Nested SMなし |
| Gameplay Tools | 実装済み | Spline、Event Timeline、State Graph |
| Diagnostics/Profiler | 実装済み | CPU/GPU履歴、VRAM、Subsystem統計、Scene検査 |
| Log Monitor | 実装済み | Object/Component/System Field監視、条件、出力 |
| TEAM | 実装済み | 接続、同期、Lock、履歴、TeamItem、競合、復旧 |
| Hook/Wire Debug | 実装済み | Event Hook設定とRuntime Wire検査 |
| PV Shoot | 実装済み | 撮影用Camera/PostProcess/Time Scale調整 |

各Windowの操作、保存場所、失敗時の確認先は`user-guide.md`と`user-guide.md`を参照する。

### 5. Scene・GameObject・Transform

- SceneはUUIDを持ち、GameObjectは安定UUID、Runtime整数ID、Name、Parent、Local Transform、Active、Componentを持つ。
- Hierarchy親子関係からWorld Transformを計算する。位置単位はm、回転入力はdegree、Scaleは倍率で扱う。
- Scene Saveは一時File経由で置換し、失敗時に既存Sceneを維持する。
- 未知Serialization行を保持し、旧Buildで開いて再保存した際の情報消失を抑える。
- Duplicate、Clipboard、複数選択、Gizmo Local/World、Snap、範囲選択を持つ。
- Play中のScene保存/通常Loadは編集Scene破壊を避けるため制限する。

### 6. Prefab・Scene Streaming

| 機能 | 状態 | 注記 |
| --- | --- | --- |
| Prefab保存/Instantiate | 実装済み | UUID/IDを再割当してSceneへ配置 |
| Apply/Revert | 実装済み | Link元Prefabへ反映、または元から再生成 |
| Prefab Variant | 部分対応 | Base Pathと明示Overrideを保存 |
| Property Override | 部分対応 | Component Active/Asset、Transform等の明示形式。任意Field一般化ではない |
| Nested Prefab | 部分対応 | 完全なNested編集/Override階層ではなく、保存時の実体化を含む |
| 同期Scene Load | 実装済み | Build Scene範囲を検証 |
| Async Load | 実装済み | 遷移状態とProgressを管理 |
| Additive Load/Unload | 実装済み | UUID衝突を解消し、既存Scriptを全再Startしない |
| Automatic Streaming | 実装済み | Trigger/距離条件に応じたLoad/Unload |

### 7. Component

現行ComponentはTransform、描画、Camera、Light、Environment、3D/2D Physics、Animation、Audio、UI、Input、AI、Navigation、Effect、Terrain、Gameplay、Rail、Weapon、Save、Replay、Diagnostics等を含む。全種類と全Fieldは`component-reference.md`を正とする。

Componentの共通契約:

- `isActive=false`はRuntime Managerの実行対象外になる。
- 編集設定はScene/Prefabへ保存し、現在HP、現在Target、Particle Alive数等のRuntime値は初期設定として保存しない。
- 対象参照は可能な範囲でUUIDまたは安定Asset IDを使う。
- 新規ComponentはEnum末尾へ追加し、既存番号を変えない。
- Inspectorだけ追加してRuntime未接続、またはRuntimeだけ追加して保存不能、という状態を完成扱いにしない。

### 8. Play Mode

- Play開始前に編集SceneをMemory Backupする。
- Stop時にRuntime Systemを停止し、編集SceneをPlay前へ戻す。
- Play中に生成、削除、移動したObjectは通常のPlay契約として破棄される。
- Scene Transition、Async/Additive Load、Save、Pool、ReplayはPlay Runtime内で管理する。
- 共同制作中に他端末から届いた編集はPlay側で保留し、StopでBackup復元後に適用する。
- Playした端末自身のRuntime変化は共同制作差分にしない。

### 9. Rendering

#### 基本描画

- DirectX 12、HDR中間Target、Depth、Shadow、Material/Reflection Mask、GBufferを使用する。
- Model、Skinned Model、Sprite/UI、Terrain、Ocean、Particle/VFX、Line/Debugを描画する。
- Directional/Point/Spot Light、Shadow、Light Probe GI、Reflection Probe、Planar Reflectionを持つ。
- Transparentは通常Alphaに加えてWeighted OITとRefractive Surface経路を持つ。

#### Temporal・反射・間接光

| 機能 | 状態 | 注記 |
| --- | --- | --- |
| Depth Pyramid/Normal Reconstruction | 実装済み | Hi-Z、SSR等で使用 |
| Frustum/Hi-Z GPU Culling | 部分対応 | GPU判定結果を次FrameのPredicationへ直接使用。CPU Readbackなし。完全ExecuteIndirectではない |
| SSR Trace/Resolve/Temporal/Denoise/Composite | 実装済み | PostProcess設定で有効化 |
| SSGI | 実装済み | Temporal経路あり |
| Camera Velocity/Reactive/Disocclusion | 実装済み | Temporal補助Buffer |
| Scene/Game Temporal分離 | 実装済み | ViewごとにHistory/Previous Depth/Indexを分離 |

#### Post Process

- BloomはThreshold、Soft Knee、Scatter、IntensityをComponentからMulti-stage経路へ渡す。
- AAはNone、FXAA、SMAA、Temporalを単一`aaMode`で排他的に選択する。
- SMAA Threshold/Corner Rounding、Temporal Sharpness/Blend RatioをInspectorから実処理へ渡す。
- NoneはPassthrough PSOを使用する。
- Final CompositeはExposure、White Point、Tone Mapping、Bloom、Saturation、Contrast、Vignette、Film Grain、Chromatic Aberration、AO、Auto Exposure、Color Gradingを扱う。
- Glare、Filter、DOF、Motion Blur、Underwater、Volumetric Light Shaftを持つ。

描画の詳細は本書、内部Pass順は本書を参照する。GUIでの全組合せ目視は別途必要である。

### 10. Physics・Collision

#### 3D

- JoltによるStatic/Dynamic/Kinematic Body、Collider、Trigger、Collision Event、Ray/Sphere/Capsule Castを扱う。
- Box/Sphere/Capsule/Mesh/AutoConvex/Terrain等の形状を持つ。
- Constraint、Rope/Wire、Vehicle/浮力/空力等のComponentを持つ。
- Fixed Stepは1Frame最大4回。Physics結果後に同回数のScript FixedUpdateを呼ぶ。
- NVIDIA BlastはBake済みChunk/BondとJolt Dynamic Bodyを接続する。
- 破片軽量化はComponentのOnOffで切り替える。OFFなら分離Chunkを全てRigidbody化する本来の経路になる。
- ONのときは体積上位=物理破片、あふれ=物理Chunkの子として運ぶCluster追従、さらにあふれ=GPU破片の順に振り分ける。3つは併用できる。
- GPU破片はEditorEffectManagerのGPU Particleへ積み、GameObject / Rigidbody / Colliderを作らない。
- 短時間だけ物理化する設定では、経過後にBodyをJolt Worldから外して描画だけの瓦礫にする。距離LODは最初の分裂時のGame Camera距離だけで予算を絞る。
- GPU破片はParticleのCompute Shaderが初速・重力・Drag・乱流・外向き加速・旋回・上昇気流・回転・寿命を毎フレーム積分する。JoltのForceは通さない。
- Scene全体で同時にRigidbody化する破片数はProjectSettingsの`maxScenePhysicsDebris`で制限し、Blast側が集合で数える。Component単位の上限とは別枠。
- 破片の沈下・消滅は破片軽量化とは独立に動く。沈み切った破片はGameObjectごと破棄し、Draw Call / Constant Buffer / 共有Texture参照を解放する。
- Model / Sprite の画像はPath単位で共有し参照カウントで管理する。読み込み失敗もCacheへ残し、毎フレームの再読込とGPU全同期を避ける。SRVが空いた時だけ1回再試行する。
- 画像SRVは共通Descriptor Heap(65536)の198番以降を使う。容量と予約数は`EditorSharedState`の`kRuntimeSrvDescriptorHeapCapacity`/`kRuntimeReservedSrvDescriptorCount`で一元管理する。

#### Debug表示

- Edit中はComponent設定形状、Play中は実Physics Shapeを表示する。
- Box系の水色枠はWorld Transformへ変換し、Scene Camera Zoomでは大きさを変えない。
- ImGui線にDepth判定がないため、Camera Facing面の外形線だけを表示する。
- Mesh Colliderへ誤解を招くBounds Boxは表示しない。
- Contact、Normal、Castの開始/終了/HitをPlay中に確認できる。

#### 2D

2D Rigidbody/Collider等のComponent定義とInspector/Serializationは存在する。3D Joltと同等の全機能・Debug・Constraint互換を前提にせず、使用ComponentごとにRuntime接続を確認する。

### 11. Input

- Keyboard、Mouse、GamepadをInput ActionへBindingする。
- XInputは最大4台、Stick Dead Zone、Trigger Threshold、Look SensitivityをProject Settingsから読む。
- KeyboardとGamepadの複数Binding、Action Event、Player Inputを持つ。
- Runtime Rebind専用UI、Control Scheme Assetの高度な切替は未対応。
- 実Gamepadの機種差、抜き差し、複数台同時操作は要実機確認。

### 12. Audio・Haptics・外部認識・Online

- 2D/3D再生、Loop、Volume、Pitch、Pause/Resume、再生位置、Fade、Busを扱う。
- Master/SFX/BGM/Ambience/UI/VoiceのProject VolumeをRuntimeへ反映する。
- 3D AudioはListenerとSourceの位置、距離減衰、Physics Occlusion等を使用する。
- Handle方式Script APIで個別Voiceを制御する。
- HapticSourceはPattern、左右Channel、Clip、Audio/Physics Reactive、Editor Preview、Handle方式Script APIを持つ。
- SpeechRecognizerはWindows Speech APIのKeyword/Dictation、Input Action、Script Actionを扱う。
- CameraInputはMedia FoundationでCPU BGRA Frameを取得し、ImageRecognizerへ渡す。
- ImageRecognizerは内蔵の色/動き、ONNX Runtimeの物体/分類/顔検出を扱う。
- OnlineServiceはWinHTTP Worker、Leaderboard、Player Data、Cloud Save、再送Queue、Development/Production分離を持つ。
- 外部機能WindowでDevice、Backend、Camera FPS、推論ms、HTTP Status、Queue、Haptic Voiceを表示する。
- OpenGL Backendはなく、描画はDirectX 12。Camera認識はGPU Zero-copyではない。
- 実Speaker、マイク、Camera、触覚Device、実Networkは要実機確認。

### 13. Animation

- Property Animation Clip、Timeline、Keyframe、Interpolation、Event、Preview/Recordを持つ。
- Animator GraphはParameter、State、Transition、Any State、Blend Tree Sample、Event、Root Motionを扱う。
- Animation EventはScript ActionやEffectへ接続できる。
- Layer、Avatar Mask、Nested State Machineは未対応。
- FBX AnimationのRig差異や複雑なRetargetingはAssetごとの確認が必要である。

### 14. Effect・Particle

- 独自`.effectdef`、ParticleSystem、VisualEffect、GPU Particle、Effekseerを扱う。
- Billboard、Y軸固定、Velocity Facing、World固定、Model Particleを持つ。
- Linear/Orbit/Vortex/Wave/Attractor/Cloud/Explosion/Projectile/Mist等の運動Modeを持つ。
- Flipbook、Ribbon、Ring、Trail、Ocean Spray等の表示経路がある。
- Script HandleでSpawn、Stop、Pause、Resume、Restart、速度、Transform、Particle数を操作する。
- 一部操作はEffekseerまたは独自Effectの片方だけに対応するため、Asset形式別制限を確認する。

### 15. UI

- Canvas、Text、Image、Button、Slider、Toggle等を扱う。
- TextはFont Asset、Size、横/縦Alignment、Wrap、Overflow、Outline、Shadowを持つ。
- Keyboard/Gamepad NavigationとInteractable状態を持つ。
- Script APIからText、Color、Font Size、Interactable、Slider、Toggleを操作できる。
- Rich TextのInline Color/Bold等とTextMeshPro完全互換は未対応。

### 16. Navigation・AI

- NavMeshSurface、NavigationAgent、NavMeshObstacle、NavMeshLinkを持つ。
- Destination、Stop/Resume、Warp、HasPath、Remaining Distance、失敗理由をRuntime/Scriptから扱う。
- Scene ViewにSurface、Obstacle、Link、Agent経路をDebug表示する。
- 現行経路は障害物を考慮する折れ線で、複雑な迷路向け完全A* NavMeshではない。
- AI Sensor、Behavior Tree、Steering、Target/Threat系ComponentとScript Queryを持つ。

### 17. Terrain・Foliage・Ocean

- Terrain Height MapをCPU展開し、描画と同じ式でColliderを生成する。
- ScriptからTerrain Heightと範囲内判定を取得できる。
- Foliage表示/配置Componentはあるが、本格的なTerrain高さ編集Brush、Splat Map、Foliage Paint/Erase Editorは未対応。
- OceanはFFT Spectrum、Surface描画、Buoyancy、Underwater、Segment Hit、Occlusion Query等を持つ。
- Ocean Gameplay Queryは描画だけでなくScript/Weapon/Physics補助から参照できる。

### 18. Gameplay基盤

実装済みの主な汎用基盤:

- Targeting、Screen Aim、Threat、Lock-on、Camera Follow/Composer/Shake/Blend
- Health、Damage Receiver、Status Effect、Hit History
- Hitscan、Projectile、Ballistic、Weapon Pattern、Accuracy、Cooldown、Loadout、Ammo/Reload
- Object Pool、Prefab Spawner、Wave Spawner、Formation、Encounter、Difficulty
- Rail Movement、Rail Branch、Marker、Sequence、Timeline Event
- Objective、Score、Combo、Stage Result、Pause/Time Scale
- Runtime Property、Tween、Action Relay、UI Binding
- Saveable、Slot、Checkpoint、Replay
- Simulation LOD、距離による休止/復帰

各機能はInspector設定とScript Action/APIを組み合わせる。特定ゲーム専用Scene生成はBuilderへ置き、Runtime ManagerへScene名分岐を入れない。

### 19. C++ Script

- Native DLLをLoadし、Initialize/Start/Update/FixedUpdate/StopとCollision/Trigger/Eventを呼ぶ。
- Inspector公開Field、型付きWrapper、Action、Scene Load、Asset/Component/Gameplay APIを持つ。
- 現行Runtime API Versionは15。API構造体は末尾追加で既存Entry位置を維持し、旧Version DLLは使用していた範囲のまま互換Loadする。新しいAPIをScriptから呼ぶ場合だけ現行Headerで再Buildする。
- DLL Hot Reloadは新DLLを検証して成功時だけ交換する。
- Engine内蔵Script Editor、共同Cursor、コード範囲Merge、Breakpoint共有は未対応。
- Native DLL内部のAccess ViolationをEngineが安全継続できる保証はない。

全関数とWrapperは`script-api-reference.md`を正とする。

### 20. Asset・Import・Hot Reload

| Asset | Registry/Import | Hot Reloadの基本 |
| --- | --- | --- |
| Model | 対応 |再Importして描画Resource更新。形状変更はPhysics再生成が必要な場合あり |
| Texture | 対応 | Texture差替え |
| Audio | 対応 | Decoder/Buffer更新。再生中Voiceは条件依存 |
| VFX | 対応 | Asset形式別に再読込 |
| Animation | 対応 | Play再開を要求する場合あり |
| Material | 対応 | Parameter/参照更新 |
| Prefab | 対応 | 既存Instanceへ万能な自動Override反映ではない |
| Input Action | 対応 | Binding再読込 |
| Script | 対応 | DLL再Build/検証/Hot Reload |

Asset RegistryはAsset ID、Path、Hash、依存、逆依存、Missingを管理する。Project View経由の移動でIDを維持する。詳細は本書を参照する。

### 21. 共同制作

- Remote Cursor、User名、選択Object、Component/Property操作、Scene Camera、他視点Jump/Follow、Presenceを扱う。
- Scene/GameObject/Component/Property差分、Asset、Lock、Conflict、History、Checkpoint/Snapshot、Backup/Trashを扱う。
- Note/Ping/Chat/Reviewは共通TeamItemとしてScene View、Hierarchy、Inspector、Project View、TEAM Windowへ統合される。
- TeamItemは返信、担当、解決、Review状態、検索、通知、対象移動、編集、削除、同時編集競合を持つ。
- 参加コードはLauncherでProject IDから生成し、Hub CatalogからProject/Engine/接続設定を取得する。
- Protocol 3とProject IDをHandshakeで検証する。
- Play中のRemote Scene/Asset変更はStop後まで保留する。
- Host Migration、Role Permission、TLS/認証、Engine内蔵Script共同編集は未対応。

詳細は`user-guide.md`と本書を参照する。

### 22. Build・配布

- Game Build SettingsでProduct名、出力先、Development/Release、起動Scene、Build Sceneを指定する。
- 参照Assetだけを収集するModeと必要Runtime DLL/Shader/ThirdPartyの収集を持つ。
- Build結果とErrorを`BuildLogs`へ保存する。
- LauncherはEngine Version別Install、Update Channel、Project固定、Hub公開、Invite、参加コードを扱う。
- Project SnapshotはManifest/Hashを検証し、指定Engine Versionを取得してからEditorを起動する。
- 参加コードは認証情報ではない。公開Hubと共同制作ServerのNetwork保護は別途必要である。

### 23. Diagnostics・Profiler・復旧

- CPU/GPU Sample、240 Frame履歴、VRAM、Draw/Dispatch、Physics/Audio/VFX/Asset統計を表示する。
- Scene静的検査はMissing参照、設定不足、Physics Body失敗等を報告する。
- Log Monitorは対象Fieldを監視し、条件一致をRuntime Logへ出力・保存する。
- Sceneは安全保存、共同Assetは上書き前Backupと削除Trash、競合はLocal/Base/Remote Copyを残す。
- Project MigrationはBackupを作る。

### 24. セキュリティと運用制限

- Native C++ ScriptはProject利用者と同じ権限で動く。信頼できないDLLをLoadしない。
- 共同制作TCP自体にTLS、User認証、Role Permissionはない。信頼できるLAN/VPN内で使う。
- Hub/参加コードはProject発見と配布導線であり、Access Controlではない。
- Source/Asset同期はGitのCommit/Branch/Review履歴の代替ではない。
- 大容量Vendor Tree、Build生成物、`.team`等は共同制作同期対象外である。
- Camera画像とマイク音声は標準実装ではOnline Workerへ送らない。Device利用理由とOFF手段はゲーム側UIで説明する。
- Online Client Keyは公開値であり、管理鍵とCloudflare資格情報はWorker Secretへ置く。Pending Queueは平文である。

### 25. 現行の主な未対応・部分対応

| 分野 | 内容 |
| --- | --- |
| Platform | Windows/DX12以外 |
| Prefab | Unity相当の完全Nested Prefab/任意Property Override |
| Animation | Layer、Avatar Mask、Nested State Machine |
| Navigation | 複雑な迷路向け完全A* NavMesh |
| UI Text | Rich Text、TextMeshPro完全互換 |
| Terrain | 高さ編集Brush、Splat Map、Foliage Paint/Erase |
| Input | Runtime Rebind UI、高度なControl Scheme |
| Rendering | GPU Cullingの完全`ExecuteIndirect`化 |
| Script Editor | 内蔵Editor、行Gutter、共同Cursor、範囲Merge、Breakpoint共有 |
| Speech | ONNX音声認識Backend、Whisper CLIの同一Process組込み |
| Vision | 顔ランドマーク、頭部方向、OpenCV、MediaPipe、GPU Zero-copy |
| Online | Account認証、大型R2 Objectの可変長Script取得、Pending Queue暗号化 |
| Collaboration | Host Migration、TLS、認証、Role Permission |
| Validation | 全機能のGUI/Play/複数PC/実デバイス自動回帰 |

### 26. 記入漏れ防止の更新対象

機能追加時は最低限、次を同時更新する。

| 変更内容 | 更新する文書 |
| --- | --- |
| Component/Field | `component-reference.md`、Field Registry、本書 |
| Script API/Wrapper | `script-api-reference.md`、API Version、本書 |
| Window/操作 | `user-guide.md`、`user-guide.md` |
| Renderer/Lighting | 本書、本書、内部設計書 |
| Asset/Importer | 本書、本書 |
| 共同制作 | `user-guide.md`、本書 |
| Launcher/Version | `user-guide.md`、本書 |
| 内部所有・処理順 | 本書 |
| Runtime Manager/更新順 | 本書、本書 |
| Render Pass/Resource | 本書、本書、本書 |
| 保存形式/互換/復旧 | 本書、本書 |
| 利用者手順/問題対処 | `user-guide.md`、`user-guide.md` |

### 27. 検証区分

記録では次を分ける。

1. Source接続確認
2. Serialization往復確認
3. Compile/Link確認
4. Editor起動確認
5. GUI操作確認
6. Play動作確認
7. 描画/音声/入力の目視・実機確認
8. Standalone Build確認
9. 複数PC共同制作確認

Compile成功だけでPlayや表示成功と書かない。今回の文書更新はSource照合であり、Build、Play、Standalone、複数PCの再実行は行っていない。

### 28. 関連文書

- `user-guide.md`
- `user-guide.md`
- `user-guide.md`
- `user-guide.md`
- `component-reference.md`
- `script-api-reference.md`
- `user-guide.md`
- `user-guide.md`
- `user-guide.md`

---

## Runtimeサブシステム

### 1. この文書の目的

この文書は、Play開始から停止までにRuntime系Managerが何を所有し、どの順番で動き、どのManagerへ結果を渡すかを実装者向けに定義する。

利用者向けの操作は[user-guide.md](user-guide.md)、Engine全体の所有関係は本書、保存と復元は本書を参照する。

### 2. 所有者と基本境界

`EditorRuntimeManager`がRuntime系Managerの所有者である。各Managerは原則として次の境界を守る。

- `EditorScene`はGameObject、Component、編集値の正本である。
- 各Runtime Managerは、自分の分野の一時状態だけを持つ。
- Manager間参照は`Initialize`で注入し、毎Frameの探索やGlobal参照を増やさない。
- Play開始時にRuntime状態を作り、停止時に破棄する。
- Play中の一時値をSceneの編集値として保存しない。
- Scriptは公開APIとAction/Eventを通して他分野へ要求を出す。

### 3. 状態遷移

| 状態 | `isPlaying_` | Scene | Runtime一時状態 |
| --- | --- | --- | --- |
| Edit | `false` | 編集中の正本 | 停止済み |
| Play開始準備 | `false`→`true` | 開始前SceneをBackup | Pool、Physics、Script等を開始 |
| Play | `true` | Runtime用に変化し得る | 毎Frame更新 |
| Scene遷移 | `true` | Load/Unload/Transition中 | Gameplay更新を一時停止する場合がある |
| Stop | `true`→`false` | Backupへ復元 | 各Managerを停止・破棄 |

`TogglePlay()`はPlay開始前にScene全体とScene pathを保持する。Stop時は非同期Loadを取消し、Runtimeを停止してからBackupを復元する。このため、単独Playで発生したTransform、生成Object、破壊、Runtime Propertyの変化は編集状態へ持ち越さない。

共同制作時における他ユーザーの編集とPlay中保留の扱いは本書を正とする。

### 4. 初期化時の依存関係

`Initialize(EditorScene*, consoleMessages*)`は共通のSceneとConsole出力先を配り、依存先を接続する。代表的な契約は次のとおり。

| Manager | 主入力 | 主出力・依存先 |
| --- | --- | --- |
| Input | Key state、Input設定 | Script、Rail、Targetingが参照するAction状態 |
| Script | Scene、Input、Animation、Effect、Audio、AI、Physics | Action、Scene Load、各Runtime API要求 |
| Physics | Collider、Rigidbody、固定時間 | 接触、Trigger、Cast、Transform、速度 |
| Animation | Animator、Physics後のTransform | Bone/Animation Event、Effect/Script通知 |
| AI | Scene、Physics | AI ComponentのRuntime状態 |
| Navigation | Scene、Physics | 経路、残距離、Debug表示 |
| Damage | Scene、Script、Physics、Pool | HP、Damage Event、死亡・返却要求 |
| Object Pool | Scene、Physics、Damage、Script | 再利用Object、Reset callback |
| Weapon | Input、Targeting、Physics、Damage、Pool、VFX、Audio | 発射、命中、Cooldown、Camera Effect |
| Runtime Property | Script、Weapon、Damage、Input、Audio、Effect | Time scale、Pause、公開Runtime値 |
| Effect / Effekseer / VFX | Scene、Animation/Script Event | Effect Instanceと描画データ |
| Audio | Scene、Physics | 2D/3D Voice、Bus、Listener状態 |
| Save | Scene、Physics、Script | Session値とSave/Load要求 |
| Replay | 入力と時間 | 再生時のFrame Input |
| Profiler / Log Monitor | 各更新区間、Runtime値 | Profiler Sample、監視ログ |

### 5. Play開始順

開始順には依存理由がある。単純に一覧を並べ替えてはいけない。

1. Object Poolの構成を準備する。
2. Blastを開始し、必要な子Chunkを非Active化する。
3. Physicsを開始し、確定したObject構成からBodyを作る。
4. Scene Optimization、Pool、Damage、Targeting、Weaponを開始する。
5. Camera Effect、Effect、Effekseer、VFX、Animationを開始する。
6. Movement、Rail、Sequence、Wave、AI、Navigationを開始する。
7. AudioとHapticsを開始する。
8. `EditorExternalFeatureManager`がSpeech、Camera、Vision、Onlineを初期化し、Start On Play対象を開始する。
9. Scriptを開始する。外部機能は先に開始済みなのでScriptの`Start`から利用できる。
10. Physics/Script登録が使える状態でPoolをPrewarmする。
11. Runtime Property、Gameplay Event、Save、Log Monitorを開始する。

Poolの実体化をPlay中の初回使用まで遅延するとHitchになり得るため、初期容量分は開始処理でまとめて作る。

### 6. 1 Frameの更新順

現在の更新順は次のとおりである。順番は、同じFrameで値を読めるか、物理結果がいつ確定するかを決める公開上の挙動でもある。

1. Scene Transition、非同期Load、Automatic Streamingを先に処理する。
2. Replayが入力を差し替える。
3. Runtime PropertyからTime Scaleを取得し、scaled `deltaTime`を作る。
4. Physics Debug Frameを開始する。
5. Input Actionを確定する。Inputだけはunscaled timeを使う。
6. Scene Optimizationを更新する。
7. Targetingを更新する。
8. C++ Script `Update`を呼ぶ。
9. Damageを更新する。
10. ScriptからのScene Load/Unload要求を処理する。
11. LocalMove、RailMovement、RailBranch、RollingMoveを更新する。
12. ActionSequence、WaveSpawner、ObjectPool、GameplayEventを更新する。
13. Weapon、WeaponLoadoutを更新する。
14. Blast、Runtime Propertyを更新する。
15. AI、Navigationを更新する。
16. 再度Scene要求を処理する。
17. Physics固定更新を0～4回進める。
18. Physics EventとWire EventをScriptへ渡し、同じ回数だけScript `FixedUpdate`を呼ぶ。
19. Animation、Constraintを更新する。
20. Effect、Effekseer、VFXを更新する。
21. Audio、Hapticsを更新する。
22. 同じ`Audio and Haptics`区分内で外部機能をunscaled timeにより更新し、Speech/Vision結果をInput ActionとScript Actionへ渡し、Online ResponseをMain Threadで処理する。
23. FreeTransform、UI Binding、Camera Effectを更新する。
24. Log Monitorを更新する。
25. Scene Runtime Stateを公開する。

### 7. 時間の契約

- `unscaledDeltaTime`は実時間に追従する入力などへ使う。
- `deltaTime`はGame Time Scaleと手動Time Scale適用後の値である。
- Physicsは固定時間を内部蓄積し、1描画Frameにつき最大4 stepに制限する。
- Script `FixedUpdate`はPhysicsが進んだ回数と同数だけ、Physics結果確定後に呼ぶ。
- Physics PauseはPhysicsだけを止め、他のRuntime更新を必ずしも止めない。
- Scene Transition中はScene差替えとの競合を避けるため、通常Gameplay更新を止める。

### 8. Object PoolのReset契約

Pool Itemを再利用するときは、`RuntimeStateReset` Componentの設定に従う。

- HealthをResetする。
- Runtime PropertyをResetする。
- Weapon CooldownをResetする。
- 任意のReset Actionを対象GameObjectへ送る。

Managerが独自のRuntime状態を追加した場合、Pool再利用時に残留しないかを確認し、必要ならReset callbackまたはManagerの`ResetRuntimeState`へ接続する。

### 9. PhysicsとScriptの境界

- PhysicsがFrame内の固定stepを完了してから、接触・Trigger・Wire EventをScriptへ渡す。
- Scriptの`FixedUpdate`はPhysics後なので、確定した接触結果を読める。
- Railの`FixedUpdate`と`PostFixedUpdate`はPhysicsのpre/post callbackとして登録される。
- Runtime中のTransform直接変更とPhysics Body変更が競合しないよう、ComponentのBody種別と同期方向を尊重する。
- Colliderの可視枠と実判定が一致しない場合は、Transform、親Scale、Camera投影を分けて診断する。

### 10. Scene LoadとStreaming

ScriptのScene要求はQueueとして受け取る。

- 通常同期Loadは必要ならTransition演出を開始する。
- 非同期またはAdditiveはLoad requestとして開始する。
- Unloadは対象Scene pathを指定する。
- Load中は通常Updateへ戻らず、Sceneが中途半端な状態でManagerを動かさない。
- Additive Sceneでは既存Scriptを全再Startせず、追加分を登録する経路を使う。

Scene差替え後は、Managerが古い`EditorGameObject*`やComponent pointerを保持し続けないこと。IDまたは再検索可能なHandleを優先する。

### 11. Runtime Debug Draw

Play中だけ、次のManagerがScene View向けDebug描画を提出する。

- Input
- Effect
- Audio
- AI
- LocalMove / RailMovement / RollingMove
- Navigation
- Physics

Debug表示は実ゲーム描画の結果ではない。Scene CameraのView/Projection、Viewport変換、DPI、Zoomを必ず適用する。

### 12. Stop順と参照解放

停止時は、要求を出す側を止め、参照されるResourceを安全に解放する。

- Log、Optimization、Save、Sequenceを停止する。
- Weapon、Runtime Property、Targeting、Damage、Poolを停止する。
- Physicsを停止してJoltのChunk参照を外してからBlastを停止する。
- Effect、Animation、Audio、Haptics、Scriptを停止する。
- 外部機能のCallbackを解除し、Online Pending Queueを保存し、Online/Camera WorkerをjoinしてからBackend Resourceを解放する。
- Movement、AI、Navigationを停止する。
- `isPlaying_`を最後に`false`へ戻す。

停止後にcallback、Voice、Effect handle、Physics Body、非同期TaskがSceneを参照しないことを確認する。

Speech/Vision/Online/Hapticsの詳細なThread・Memory・停止順は本書を参照する。外部Deviceや通信はGame Time Scale 0でも切断・Response処理が必要なため、外部機能更新にはunscaled timeを使う。

### 13. Event、Action、Requestの使い分け

| 種類 | 用途 | 例 |
| --- | --- | --- |
| Event | 既に起きた事実を通知 | Collision、Animation Event、Damage |
| Action | 対象へ処理を依頼 | Pool Reset Action、Gameplay Action |
| Request | Frame境界で安全に処理 | Scene Load/Unload、Save/Load |
| 直接参照 | 高頻度で明確な依存 | Input参照、Physics Cast |

Scene構造を変える操作はManager更新中に即時実行せず、Requestまたは安全な処理点へ寄せる。

### 14. エラー境界

- 初期化引数が無い場合は開始しない。
- GameObjectまたはComponentが消えている場合は再検索して無効扱いにする。
- Runtime APIは無効HandleでCrashせず、失敗値を返す。
- Scene Load失敗は現在Sceneを破壊せずConsoleへ理由を出す。
- 例外をManager境界の外へ無制限に伝播させない。
- 同じErrorを毎Frame大量出力しない。状態変化または間隔を設ける。

### 15. Profiler名

大区分は`Input`、`Scene Optimization`、`Targeting`、`C++ Script Update`、`Damage`、`Movement and Rail`、`Sequence and Wave`、`Weapon`、`Runtime Property`、`AI and Navigation`、`Physics`、`C++ Script FixedUpdate`、`Animation and Constraint`、`Effect`、`Audio and Haptics`、`UI and Camera`、`Log Monitor`である。

新しい重い更新を追加する場合は既存大区分へ子Sampleを追加し、単独で原因を特定できる名前にする。

### 16. 新しいRuntime Managerを追加する手順

1. 責務と所有する一時状態を1分野に限定する。
2. `Initialize`に必要な依存だけを渡す。
3. `Start`、`Update`、`Stop`、必要なら`Draw`を用意する。
4. Play開始・停止順へ依存理由と共に挿入する。
5. 更新順の「誰の前／後である必要があるか」を明記する。
6. Scene Load、Stop、Object Pool再利用時のResetを実装する。
7. Script公開が必要ならABI末尾へAPIを追加する。
8. Profiler sampleと失敗時のConsole診断を追加する。
9. Edit値とRuntime値が混ざらないことを確認する。
10. この文書と利用者向け文書を更新する。

### 17. 変更時チェックリスト

- [ ] Start前にUpdateされない。
- [ ] Stop後にScene pointerを使わない。
- [ ] 同一Frameの読み書き順が定義されている。
- [ ] scaled / unscaled / fixed timeのどれを使うか明確である。
- [ ] Scene Load中の更新を安全に停止できる。
- [ ] Pool再利用で一時状態が残らない。
- [ ] Play停止で編集値へ戻る。
- [ ] 無効ID・無効HandleでCrashしない。
- [ ] ProfilerとConsoleから原因を追える。
- [ ] Script API、Component、保存形式を変更した場合は対応文書も更新した。

### 18. 現在の制限

- Runtimeの大部分は単一`EditorRuntimeManager`から順序制御され、完全なTask Graphではない。
- 1描画FrameのPhysics stepは最大4回であり、大きなFrame落ちを完全には追いつかない。
- Scene差替え時の生pointer保持は禁止設計だが、個別Manager追加時にレビューが必要である。
- Play中の編集反映可否はComponentごとに一様ではない。
- 実行順を変えるとScriptやGameplayの見え方が変わるため、単なる整理として並べ替えてはいけない。

---

## 描画パイプライン

### 1. この文書の目的

この文書は、DirectX 12描画におけるFrameの流れ、Scene ViewとGame Viewの分離、Pass間Resource、Barrier、Temporal履歴、設定値の接続先を実装者向けに整理する。

利用者向け画質設定は[user-guide.md](user-guide.md)、照明個別仕様は本書、全体所有関係は本書を参照する。

### 2. 所有と責務

| 所有者 | 責務 |
| --- | --- |
| `EditorPlatformManager` | Device、SwapChain、Command Queue/List、Descriptor Heap、主要Resource/PSO生成 |
| `EditorRenderManager` | FrameごとのPass順、View選択、Barrier、Draw/Dispatch、Present |
| `EditorSharedState` | 現行構造で共有されるGPU Resource、Handle、Manager、Frame状態 |
| 各Renderer Manager | Depth hierarchy、GPU culling、Temporal、PostProcess、Ocean等の局所Pass |
| Component | Camera、Light、PostProcess、Material、Performance等の利用者設定 |

`EditorRenderManager::Draw()`は巨大なFrame orchestrationであり、各局所ManagerがResource生成まで勝手に広げない。Device依存Resourceは初期化・Resize・Finalizeの境界を明確にする。

### 3. Frameの大分類

描画は概念上、次の順に進む。

1. Frame開始、Profiler timestamp、前Frame readbackの取得。
2. Play状態と描画設定を確定する。
3. Scene View / Game ViewごとのCamera、Viewport、Scissorを確定する。
4. Shadow、Reflection、Probe等の更新対象を決める。
5. Opaque系GeometryとDepthを描く。
6. Depth Pyramid、Normal再構築、GPU Culling等のScreen-space前処理を行う。
7. SSAO、SSGI、SSR、Volumetric等を合成する。
8. Transparent、OIT、Refractive、Water等を描く。
9. Temporal、Motion Blur、DOF、Bloom等を処理する。
10. Exposure、Color grading、Final Composite、AAを適用する。
11. Editor UIとDebug表示を描く。
12. Back Bufferへ遷移しPresentする。
13. GPU timestamp、Ocean等の必要なreadbackを後処理する。

実コード上は最適化や依存のため一部Passが前後する。Pass追加時は入力Resourceが完成している位置へ置く。

### 4. Scene ViewとGame View

Scene ViewはEditor Camera、Game Viewは選択されたCamera Componentを使う。共有してよいものと分けるものを区別する。

| 状態 | Scene View | Game View |
| --- | --- | --- |
| View / Projection | Editor Camera | Active Camera Component |
| Viewport / Scissor | Scene panel領域 | Game panel領域 |
| Camera position/direction | Editor操作値 | Runtime/Scene Camera値 |
| Previous matrix | Scene用履歴 | Game用履歴 |
| Temporal history | Scene用 | Game用 |
| Jitter | Scene用Frame状態 | Game用Frame状態 |

異なるViewでPrevious matrixやTemporal historyを共有すると、視点切替時の残像、Velocityの飛び、SSR/SSGIの誤履歴が起きる。View識別子単位で履歴を保持し、Resize、Camera変更、Play開始・停止、Scene変更時に無効化する。

### 5. Camera入力

描画Cameraから最低限、次を確定する。

- Position、Forward、Up
- View matrix
- Projection matrix
- ViewProjectionと逆行列
- Near / Far clip
- Perspective / OrthographicとFOVまたはSize
- Current / Previous matrix
- Jitter offset
- ExposureとCamera固有のPostProcess設定

Camera Componentに値が無い場合のFallbackは一箇所で決める。Passごとに別の既定値を持たせない。

### 6. Resourceの寿命

#### 6.1 Engine寿命

Root Signature、PSO、共通Shader、固定Descriptor割当など、Deviceと同じ寿命を持つ。

#### 6.2 Window size依存

HDR target、Depth、PostProcess target、SSAO/SSGI/SSR target、Temporal history等は描画寸法に依存する。Resize時はGPU使用完了を保証してから解放・再作成する。

#### 6.3 Scene/View寿命

Shadow atlas、Reflection、Probe、Temporal previous stateなど、SceneまたはView変更で無効化されるResourceである。

#### 6.4 Frame寿命

Upload data、Draw list、可視Object list、一時定数、Profiler query等である。GPUが読み終える前にCPU側Memoryを再利用しない。

### 7. Descriptor管理

- SRV/UAV/CBV/RTV/DSVのHeap種別を混同しない。
- 固定Indexを追加する場合は既存範囲と衝突しないことを確認する。
- ResizeでResourceを作り直したらDescriptorも新Resourceへ更新する。
- ImGuiが参照するSRVを上書きしない。
- 一時Descriptorを導入する場合はFrame数分の再利用安全性を確保する。
- `ptr == 0`のHandleは無効としてPassを安全にSkipする。

### 8. Resource StateとBarrier

代表的な遷移は次のとおり。

| 使用 | State例 |
| --- | --- |
| Render Target書込 | `RENDER_TARGET` |
| Depth書込 | `DEPTH_WRITE` |
| Shader読込 | `PIXEL_SHADER_RESOURCE` / `NON_PIXEL_SHADER_RESOURCE` |
| Compute UAV書込 | `UNORDERED_ACCESS` |
| Copy元/先 | `COPY_SOURCE` / `COPY_DEST` |
| Present | `PRESENT` |
| Predication | `PREDICATION` |

UAVを連続して書く場合はTransitionだけでなくUAV Barrierが必要になる。Passの出口で次の使用状態へ戻すか、次Passの入口で明示的に遷移するかを統一する。

### 9. GeometryとDepth

Opaque GeometryはMaterial、Transform、Camera、Lightを使いHDR targetとDepthへ出力する。Alpha Cutoutは透明合成ではなく、必要に応じてDepth/ShadowへCutoutを反映する。

Depthは後段の次へ供給される。

- Depth Pyramid
- SSAO / SSGI / SSR
- Motion Blur / DOF
- Refractive / Underwater
- Scene上のSelection、Gizmo、Physics Debugの遮蔽判断

後段でSamplingする前に、Depthを読み取り可能な状態へ遷移し、必要ならOpaque時点のDepth copyを使う。

### 10. Shadow、Reflection、GI

- Shadow mapはLightとCaster状態の変化、更新間隔、Play切替を考慮して再生成する。
- Planar Reflectionは対象面とCameraから反射Cameraを作る。
- Reflection Probe、IBL irradiance/prefilter、BRDF LUTはMaterial lightingへ入力する。
- Light Probe GI、Sun Portal、Volumetricは本書の契約を使う。
- Caster/Receiverの設定をMaterialやComponentからRendererまで接続する。

### 11. Depth PyramidとGPU Culling

`EditorGpuCullingManager`は最大2048 Objectのworld AABBを受け取る。

1. CPUで確定したAABBをUpload Bufferへ書く。
2. Frustum computeで視錐台外を除外する。
3. Hi-Z Occlusion computeでDepth Pyramidと比較する。
4. 可視結果からDraw Argument bufferを作る。
5. 次FrameのDrawで`SetPredication`によりGPU結果を直接参照する。

現在の`ResolveReadback()`は互換用で、可視結果をCPUへMapしない。`ExecuteIndirect`ではなく既存DrawにPredicationを付ける方式である。Viewの局所UVはWindow全体のDepth Pyramid UVへ変換する。

### 12. Transparent、OIT、Refractive

- 通常Alpha Blendは順序依存がある。
- Weighted OITはAccumulation/Revealage等の中間Resourceへ書き、Compositeする。
- RefractiveはOpaque color/depthを読み、背景を歪ませる。
- WaterはReflection、Refraction、Depth、Ocean FFT等へ依存する。
- TransparentがDepthを書かない前提を後段Passで誤解しない。

### 13. Screen-space effect

| Effect | 主入力 | 主出力・注意 |
| --- | --- | --- |
| SSAO | Depth、Normal | AO。Blur後に強度をCompositeへ渡す |
| SSGI | Depth、Normal、Color | Temporal/upsampleを経て間接光へ加算 |
| SSR | Depth、Normal、Color、Previous | Trace→Resolve→Temporal→Denoise→Composite |
| Volumetric | Light、Depth、Camera | Light shaft/fogをHDRへ合成 |
| Underwater | Depth、Water状態 | Caustics、色吸収等を適用 |

Screen-space effectは画面外情報を持たない。失敗時のFallbackやProbe/IBLとの役割を明示する。

### 14. Temporal処理

Temporal系はCurrent color/depth、Velocity、Reactive mask、Disocclusion mask、Previous historyを使う。

- Camera matrixだけでなくObject transformのprevious値も必要である。
- 新規Object、Teleport、大きなCamera jumpでは履歴をRejectする。
- TransparentやEffectはReactive maskで過去色の混入を抑える。
- Depth差やMotionからDisocclusionを判定する。
- Scene ViewとGame Viewの履歴を分離する。
- Resize、Projection変更、Scene切替、Play切替でhistory validをResetする。

### 15. PostProcess設定の接続

`PostProcess` ComponentとCamera設定を描画側の単一設定へ集約し、次へ接続する。

- Bloom: threshold、soft knee、scatter等
- AA: None、FXAA、SMAA、Temporalの排他選択
- SMAA: threshold、corner rounding等
- Temporal: sharpness、blend ratio等
- Final Composite: exposure、saturation、contrast、vignette、grain、chromatic aberration、AO intensity
- DOF: Camera/Component値とDepth
- Motion Blur: Camera設定、Velocity、shutter相当値
- Color grading: LUT、強度

AA Noneは専用passthroughを使い、無効値を入れたFXAAとして扱わない。旧bool設定はLoad互換に留め、`aaMode`を現行の正本とする。

### 16. BloomとFinal Composite

BloomはHDR colorから閾値を超える輝度を抽出し、Downsample/Blur/Upsampleして元画像へ合成する。Final CompositeではTone mappingと利用者設定を適用し、表示可能な色域へ変換する。

順序の基本は次である。

1. HDR lighting/effectを完成させる。
2. Bloom等のHDR effectを作る。
3. ExposureとTone mappingを適用する。
4. Color grading、vignette、grain、CA等を適用する。
5. AAまたはSharpenを仕様どおり適用する。
6. UIを、意図した色空間と順序で合成する。

同じEffectを旧経路と新経路の両方で二重適用しない。

### 17. Auto ExposureとReadback

Histogram等をGPUで集計し、必要な小さな結果だけをReadbackする。同期MapでGPUを待たず、前Frameまでに完了した値を使う。

Readbackがある主な分野は次である。

- Auto Exposure histogram
- GPU timestamp
- Ocean FFT surface sample

GPU Culling可視結果は現在CPUへreadbackしない。

### 18. Performance Settings

Performance ComponentはPassの有効無効、更新間隔、解像度比、Sample比等へ接続する。設定を追加するときは次を守る。

- 画質Presetから個別設定への変換点を一箇所にする。
- 0除算や0寸法にならないよう下限を持つ。
- 無効PassのResourceを毎Frame作らない。
- 更新間隔を空けるPassはCamera/Sceneの大変化で強制更新する。
- Scene ViewとGame Viewのどちらへ適用されるか明記する。

### 19. Resize、Device loss、Finalize

Resizeでは次の順を守る。

1. GPU使用完了をFenceで待つ。
2. Size依存Resourceへの参照を外す。
3. SwapChain bufferと各targetを再作成する。
4. RTV/DSV/SRV/UAVを更新する。
5. Viewport/Scissorを更新する。
6. Temporal history、previous depth等を無効化する。

FinalizeはManager→Resource→Device参照の順に解放し、callbackやImGuiが解放済みDescriptorを参照しないようにする。

### 20. Profilerと診断

- Draw/Dispatch数、GPU time、VRAM、主要Pass timeを取得する。
- Passを追加したらtimestamp区間またはProfiler sampleを付ける。
- 画面が白・黒の場合は、最初にBack Buffer、HDR、PostProcessのどこまで正常かを切り分ける。
- Effectがずれる場合は、world→view→clip→viewportの各座標を比較する。
- Scene Viewだけ壊れる場合はEditor Cameraと局所Viewport変換を確認する。
- Game Viewだけ壊れる場合はActive Camera、Projection、Play時Component値を確認する。
- 拡大縮小でCollider枠だけずれる場合は、当たり判定本体ではなくDebug lineの投影・Viewport/DPI変換を先に確認する。

### 21. Pass追加手順

1. 入力、出力、解像度、Format、Color spaceを定義する。
2. どのViewに属する履歴かを定義する。
3. Resource寿命とResize処理を定義する。
4. Descriptor範囲を確保する。
5. Root Signature、PSO、Shaderを初期化する。
6. Pass前後のStateとBarrierを列挙する。
7. 無効時のpassthroughまたはfallbackを用意する。
8. Performance設定とComponent設定を接続する。
9. Profiler、Debug view、失敗ログを追加する。
10. Scene/Game両View、Resize、Play切替、Camera切替を確認する。
11. 利用者向け設定と内部仕様を更新する。

### 22. 変更時チェックリスト

- [ ] Input ResourceがPass開始前に完成している。
- [ ] FormatとColor spaceが一致している。
- [ ] BarrierとUAV orderingが足りている。
- [ ] Descriptor indexが衝突していない。
- [ ] Resize後に古いResourceを参照しない。
- [ ] Scene/GameのCameraと履歴が混ざらない。
- [ ] Camera jump、Scene変更、Play切替でTemporalをResetする。
- [ ] 無効設定で余計なDraw/Dispatchをしない。
- [ ] 旧経路との二重適用がない。
- [ ] GPU/CPU同期を増やしていない。
- [ ] Debug表示がViewport、DPI、Zoomへ追従する。
- [ ] Componentの値が最終Shader定数まで届く。

### 23. 現在の設計上の注意

- `EditorRenderManager::Draw()`と`EditorSharedState`の責務が大きく、Pass追加時の影響範囲が広い。
- GPU CullingはGPU Predication方式であり、完全な`ExecuteIndirect`描画統合ではない。
- Screen-space effectは視野外情報や薄いGeometryで破綻し得る。
- Temporal品質はVelocity、Reactive/Disocclusion mask、履歴Reset条件に依存する。
- Scene ViewとGame Viewを同じWindow texture上で扱う箇所では、局所UVから全体UVへの変換が必要である。
- Compile成功だけではResource state、Resize、残像、色空間の問題は確認できない。実画面確認を別途記録する。

---

## データ保存・互換性

更新基準: 2026-09-25

この文書はScene、Prefab、Project Settings、Asset Registry、Import Settings、Build Manifest、共同制作データの保存責務と互換性境界をまとめる。

### 1. 基本方針

- 人が調査できる設定・Sceneは原則UTF-8 Textで保存する。
- ファイルはUTF-8 BOM付きで扱えるようにする。
- Project相対Pathを優先し、配布先PCの絶対Pathを保存しない。
- GameObject/Component/Assetは表示名ではなく安定IDで識別する。
- 保存失敗で既存Fileを壊さない。
- 新Versionの未知情報を旧Versionで開いただけで消さない。
- Runtime現在値と編集初期値を分ける。

### 2. Project Directory

| Path | 責務 |
| --- | --- |
| `Assets/` | Scene、Prefab、Model、Texture、Material、Animation、Input、Effect等 |
| `resources/` | Runtime互換Assetと既存Resource |
| `NativeScripts/` | 利用者C++ Source/Project/DLL関連 |
| `ProjectSettings/` | Version、Game、Editor、Build、共同制作設定 |
| `Library/` | 再生成可能Cache |
| `Builds/` | Game Build出力候補 |
| `BuildLogs/` | Build/Environment Check結果 |
| `.team/` | 共同制作の履歴、Base、Backup、Conflict、Trash |

`Library`、Build生成物、`.team`は通常のProject Assetとして扱わない。

### 3. Project Version

`ProjectSettings/ProjectVersion.cg2`はProjectを開けるEngine条件を持つ。

- Required Engine Version
- Engine Version Policy
- Update Channel
- Project Format Version
- Required Script API Version

起動時に`ProjectVersionManager`がLoadして現Engineと比較する。Open不可、Save不可、Migration必要を分け、Save不可ではEditorがProjectを書き換えない。MigrationはBackup後にFormatを更新する。

### 4. Project Settings

`ProjectSettings/ProjectSettings.cg2`はゲーム共通設定を持つ。

- Game Width/Height
- Window Mode
- VSync/FPS Limit
- Master/SFX/BGM/Ambience/UI/Voice Volume
- Gamepad Dead Zone/Trigger Threshold/Look Sensitivity
- Startup Scene/Build Scene等の関連設定

起動時設定とRuntime即時反映を区別する。解像度・Window Mode等は次回起動反映になる場合がある。

### 5. Editor Settings

`ProjectSettings/EditorSettings.cg2`はEditor固有設定を持つ。Project利用者へ共有すべき設定とPC固有Layout/Window状態を混ぜない。ImGui Layoutは`imgui.ini`等の別保存となり得る。

### 6. Scene形式

Sceneは行指向Textで、先頭にFormat VersionとScene UUIDを持つ。主な行はScene設定、GameObject、Transform、Component、型別追加データである。

```text
FormatVersion|Scene|<version>
SceneUuid|<uuid>
GameObject|...
Transform|...
Component|...
```

実際のField順は`EditorScene::SaveScene()`と`LoadScene()`を正とする。文書例をParser仕様として固定しない。

#### ID

- Scene UUID: Scene自身。
- GameObject UUID: 保存・共同制作・参照用。
- GameObject ID: 実行中の高速参照用整数。
- Component UUID: Component参照、Lock、TeamItem等。

Load、Prefab Instantiate、Scene Mergeでは整数IDとUUID衝突を検出し、必要な参照を一括変換する。

#### 未知行

Load時に解釈できない行は保持し、Save時に末尾へ書き戻す。これはForward Compatibilityの損失低減であり、旧Buildが未知機能を実行できるという意味ではない。

### 7. 安全保存

Scene保存はDestinationへ直接上書きせず、一時Fileへ完全出力してから置換する。

```text
現在Scene
 -> TempへSerialize
 -> Stream/Error確認
 -> 既存Destinationを保持したまま置換
 -> 成功後に完了扱い
```

Prefab保存、共同制作がScene Snapshotを保存する経路も同じ安全保存を通る。

### 8. Runtime値を保存しない

次のような値はPlay中の現在状態であり、Scene初期値へ書き戻さない。

- 現在HP/Shield/Ammo/Cooldown
- 現在Target/Threat
- Physics Bodyの現在速度・睡眠状態
- Particle Alive数
- Audio Voice Handle/再生位置
- Runtime生成Object ID
- Pool貸出状態
- Animation現在State時間

保存するのはMax HP、初期Ammo、Asset Path、係数、Action名等の編集設定である。

### 9. Prefab形式

PrefabもScene Serializerを使用し、Format種別をPrefabとして保存する。Root以下のGameObjectを抽出し、Prefab外参照を無効化または変換する。

Prefab LinkはSource Path、Root、Variant Base等を保持する。Instantiate時は新しいID/UUIDを割り当て、内部参照を新IDへ変換する。

現行Overrideは明示形式で保存する。

- Component Add/Active/Asset
- Transform Position/Scale等
- Projectile Spawn Point等の専用構造

任意Fieldを汎用Diffとして保存するUnity互換形式ではない。

### 10. Additive Scene

Additive Loadは別SceneのObjectを現在SceneへMergeする。衝突するID/UUIDを再割当し、内部参照を変換する。Unloadに必要なSource Scene識別を保持する。

既存Scriptを全Stop/Startせず、追加Objectだけ`StartAdditive()`で開始する。保存対象とRuntime追加Sceneを混同しない。

### 11. Asset Registry

`ProjectSettings/AssetRegistry.txt`はAsset IDとPath等の対応を保存する。Registry Runtime Recordは次を扱う。

- Asset ID
- 正規化Path
- Asset Type
- File Size/更新時刻
- Content Hash
- Forward Dependency
- Reverse Dependency
- Missing Dependency
- Import/Reload状態

Project View経由Move/Renameでは同じIDを保つ。外部Tool移動は削除+追加と区別できない場合がある。

### 12. Hash

Content同一判定は64 bit HashとFile Size等を使用する。転送不要判定や変更検出用で、暗号学的署名や改ざん認証には使用しない。

Sizeと更新時刻が同じ場合はCache済みHashを返すため、それらを意図的に維持して内容だけ変える特殊操作では再計算されない可能性がある。

### 13. Asset依存

Text Assetから`Assets/`または`resources/`Path Tokenを抽出する。Modelは読込済みMaterial DataからTexture依存を得る。

依存GraphはBuild収集、Project View表示、削除警告、Missing表示、共同制作へ使う。全Binary内部参照、DLL依存、Shader Includeを完全解析するものではない。

### 14. Import Settings

`AssetImportSettingsStore`はAsset ID単位に設定と状態を保存する。

- Imported
- Needs Reimport
- Failed
- Missing Source
- Requires Manual Action

PathではなくAsset IDに紐付けるため、Project View経由Move後も設定を維持できる。設定変更は即時変換せず、Reimport操作で適用する場合がある。

### 15. Animation・Input・Effect等のText Asset

`.animgraph`、Animation Clip、`.inputactions`、`.effect`、`.effectdef`、Material等はそれぞれ専用Loaderを持つ。共通原則:

- Format/Versionを持つ。
- Project相対Asset Pathを使う。
- Parserが不正値をClampまたはErrorにする。
- Editor変更後は明示保存する。
- Registryへ変更通知する。

### 16. Native Script Metadata

Script ComponentはDLL Path、Class/Factory情報、公開Field値をSceneへ保存する。DLL内部のPointer、Instance Address、Runtime Handleは保存しない。

公開Fieldの型と名前が変わった場合は旧保存値を適用できないことがある。Script API Version変更時はHeader更新後にDLLを再Buildする。

#### 外部認識・Online

SpeechRecognizer、CameraInput、ImageRecognizer、HapticSourceの設定はScene/PrefabのExtension行へ保存する。認識文字列、Camera Frame、ONNX Session、Haptic Voice/Handle、HTTP Handle、Callback、OS Handle、Threadは保存しない。

ImageRecognizerのCamera参照はGameObject ID参照であり、Prefab配置、Duplicate、Scene Merge、Additive Load時にRemapする。参照先が複製範囲外なら`-1`へ戻り、実行時は開いているCameraのうちIDが最小のものを使う。

Onlineの送信失敗QueueだけはSceneとは別に`SaveData/OnlinePendingQueue.cg2`へ保存する。これは再送用の平文Requestであり、Save Slot、Cloud Save本体の正本、秘密情報Storeではない。Method、Endpoint、Retry Count、Bodyを保存し、最大件数と再送上限をProject Settings/System側で制御する。

詳細は本書の「Scene保存と参照Remap」「Online内部」を参照する。

### 17. Game Build Settings

`ProjectSettings/GameBuildSettings.cg2`は次を持つ。

- Product Name
- Output Path
- Configuration
- Startup Scene
- Build Scene一覧
- Asset収集Mode

Build時はSceneから依存を辿り、Asset、DLL、Shader、ThirdParty、Runtime設定を出力する。Standalone側は`game.build`を見てEditorではなくGameとして起動する。

### 18. Launcher登録データ

LauncherはEngine Install一覧とRegistered Projectをユーザー側状態Directoryへ保存する。Project登録にはProject ID、Name、Hub、Channel、Required Engine、Project Root、Collaboration情報等を含む。

Launcher登録はProject内Metadataの代替ではない。公開時はProject Metadata側のProject IDを優先する。

### 19. Project配布Manifest

Hubへ公開するProjectはCatalog、Project Manifest、Snapshotを持つ。

- Project ID/Name
- Required Engine Version
- Update Channel
- Snapshot Revision
- File一覧/Hash/Download URL
- Collaboration Host/Port/ID

公開処理はPending Pathへ生成してから正式Manifestへ置換し、不完全公開を見せない。

### 20. Inviteと参加コード

`.cg2-invite`はProject、Hub、Engine、共同制作接続先を記録するJSON案内Fileで、Passwordや秘密Tokenを保存しない。

参加コードはProject ID由来の決定的短縮コードで、LauncherがHub Catalogと照合する。Project IDそのもの、認証Credential、Snapshot内容はコードへ埋め込まない。

### 21. 共同制作Settings

`ProjectSettings/TeamCollaboration.settings`はUser、Host、Port、Project ID、Host/Auto Connect、Revision、共有Scene Folder等を持つ。

`TeamCollaboration.invite`はLauncher取得情報から未設定項目を補完する。利用者が手動設定した値を無条件に上書きしない。

### 22. 共同制作Change Log

`.team/change-log.jsonl`は1行1Change Eventで、Revision順の履歴とTeamItemを保存する。

- Change ID
- User
- Timestamp
- Base/Committed Revision
- Scene/Object/Component/Property
- Operation
- Old/New Value
- Snapshot/Asset Metadata

TeamItem専用Databaseはなく、`TeamItemUpsert/Delete`を履歴再生して現在Mapを復元する。

### 23. 共同制作Base・Live・Conflict

| Path | 内容 |
| --- | --- |
| `.team/base/current.scene` | 3-way比較Base |
| `.team/live/current.scene` | 現在Snapshot |
| `.team/live/incoming.scene` | 受信検証用 |
| `.team/conflicts/<id>` | Local/Base/Remote/Resolution |
| `.team/backups` | 上書き前Scene/Asset |
| `.team/trash` | Remote削除Asset |

`.team`は同期対象外にし、Backup/Conflictの再同期循環を防ぐ。

### 24. Save・Checkpoint・Replay

Game SaveはScene編集保存と別で、Saveable ComponentとSlot/Checkpointを使用する。保存対象は明示登録されたGameplay値である。

Replayは入力とdeltaTime、開始状態を記録する。Scene AssetそのもののVersion Controlではない。Component構成やScriptが変われば同じ結果を保証できない。

### 25. Backup方針

| 操作 | Backup |
| --- | --- |
| Project Migration | Project Backup |
| Scene復元 | `BeforeUiRestore` Copy |
| Remote Asset上書き | Revision別Asset Backup |
| Remote Asset削除 | Revision別Trash |
| 競合 | Local/Base/Remote Copy |
| Scene通常保存 | Temp + replace |

Backup作成成功前に元を消さない。

### 26. 文字コード

- Source/MarkdownはUTF-8 BOM付き。
- Scene/Settings/LogもUTF-8を基準にする。
- Windows Wide Pathとの変換はUTF-8/UTF-16を明示する。
- 日本語PathをANSI APIへ暗黙変換しない。

### 27. Format変更時の規則

1. 既存Field順を不用意に変更しない。
2. 追加Fieldが無い旧FileのDefaultを定義する。
3. Load -> Saveで既存情報が消えないことを確認する。
4. Scene、Prefab、Duplicate、Clipboard、共同制作、Buildを同時確認する。
5. Project FormatまたはScript APIを上げる必要を判断する。
6. MigrationとBackupを用意する。
7. 詳細仕様と現行仕様を更新する。

### 28. 破損調査

1. File先頭のFormat/Version。
2. UTF-8と改行。
3. 最後に成功したBackup。
4. Parserが報告した行番号/Token。
5. Unknown行の保持状況。
6. Asset ID/Path対応。
7. Project VersionとEngine Version。
8. 共同制作ならChange ID/Revision/Base。

元Fileを編集する前にCopyを取る。

---

## Asset管理・Hot Reload

更新基準: 2026-09-11

この文書は`AssetManager`、`AssetRegistry`、Editor Adapterの現在の契約をまとめる。Project Windowの見た目ではなく、Asset変更を検出した後に何が無効化され、何が自動反映されず、識別子と依存関係がどう維持されるかを一次情報に沿って説明する。

### 1. 責務

| 層 | 責務 | 保持しないもの |
| --- | --- | --- |
| `AssetManager` | 拡張子から種別を決め、種別別HandlerへReload/Invalidate/Unloadを委譲し、Hashと依存関係を提供する | Project全Assetの一覧、Editor Managerの直接参照 |
| `AssetRegistry` | Path、AssetId、種別、Hash、依存、更新時刻、Load/Reload状態を記録する | ModelやTexture等の実データCache |
| `EditorAssetManagerAdapters` | Engine共通層と既存Editor ManagerのCache無効化処理を接続する | 新しいAsset Loaderや別Cache |

`GameScene::Initialize`でAdapterを登録し、その後`AssetRegistry::RefreshFromDisk()`を呼ぶ。これによりHandlerが未登録の時間帯を作らず、起動時に`Assets/`と`resources/`を登録する。

### 2. Asset種別

拡張子は小文字化して判定する。

| AssetType | 拡張子 |
| --- | --- |
| Model | `.fbx`, `.obj` |
| Texture | `.png`, `.jpg`, `.jpeg`, `.tga`, `.dds` |
| Audio | `.wav`, `.mp3`, `.ogg` |
| Vfx | `.effect`, `.effectdef` |
| Animation | `.animclip`, `.animgraph` |
| Material | `.material`, `.mtl` |
| Prefab | `.prefab` |
| InputAction | `.inputactions` |
| Script | `.cpp`, `.h`, `.hpp` |
| Unknown | 上記以外 |

`.scene`はRegistry走査・依存抽出の対象だが、現行`AssetType`にはScene列挙子がないため種別値は`Unknown`になる。Scene同期と読込は共同制作/Scene Manager側の専用経路で扱い、`.scene`を一般Asset Hot Reloadへ通知すると`Unknown`になる。

### 3. 変更通知の結果

`AssetManager::NotifyFileChanged(path)`はboolではなく`AssetNotifyResult`を返す。

| Result | 意味 | 利用者の対応 |
| --- | --- | --- |
| `Applied` | Cacheを無効化・更新した、またはCacheがなく既に最新 | 通常は追加操作不要 |
| `RequiresManualAction` | 安全なPath単位Hot Reloadができない | `reason`を表示し、Play再開や再Buildを行う |
| `NoHandler` | 種別は既知だがAdapter未登録 | 初期化順とHandler登録を確認 |
| `Unknown` | 拡張子から種別を判定できない | 対応拡張子か専用処理を追加 |

Handlerの`supportsHotReload=false`ならReload関数を呼ばず`RequiresManualAction`を返す。Hot Reload対応HandlerでもReload関数がfalseを返した場合は同じ結果になる。Registryは直近結果と理由をRecordへ保持し、`Applied`時だけLoad Stateを`Loaded`へ進める。

### 4. 種別別の反映範囲

| 種別 | 現在の処理 | 結果・制限 |
| --- | --- | --- |
| Model | Model Asset CacheとScene ObjectのAsset GPU Resourceを無効化し、Scene Synchronizerを更新 | 次の参照で新内容を使用 |
| Texture | Scene ObjectのAsset GPU Resourceを無効化し、Scene Synchronizerを更新 | Model Cacheは対象外 |
| Audio | `EditorAudioManager::InvalidateClip` | 対象Clipを使う再生中Voiceを停止し、Bufferを破棄。次回再生で再読込 |
| Vfx | Stage1 VFX定義Cacheと旧Effect Asset Cacheの両方を無効化 | VFX側は生存Instanceを安全に停止してから定義を破棄 |
| Animation | 自動無効化なし | CacheがGameObject ID単位で元Pathを保持しないため`RequiresManualAction`。Playをやり直す |
| Material | No-op Handler | 独立`.material` Loader/Cacheは現状なし。OBJの`.mtl`は対応OBJのModel Cacheを別途無効化した時に読み直され、`.mtl`変更通知単独ではModel Cacheを破棄しない |
| Prefab | No-op Handler | 使用時にFileを読み直すため`Applied` |
| InputAction | No-op Handler | 使用時にFileを読み直すため`Applied` |
| Script | 自動DLL差替えなし | 実行中DLLを安全のため交換せず`RequiresManualAction`。明示的に再Build |

`Invalidate`は遅延再読込用、`Unload`はCache破棄とRegistry削除用である。Project上の削除処理は`AssetManager::Unload`を通し、種別固有CacheとRegistryの両方を片付ける。

### 5. Asset Registry

#### 5.1 永続化

保存先は`ProjectSettings/AssetRegistry.txt`。1行は`relative/path|ASSET-ID`で、永続化するのはPathとAssetIdだけである。Hash、依存、更新時刻、Load State、直近Reload結果はセッションごとに再計算する。

AssetIdはWindowsの`CoCreateGuid`から36文字の大文字GUIDを生成する。GUID生成に失敗した場合は処理停止を避けるためPath文字列を代替IDにするが、この場合は移動追跡能力が落ちる。

#### 5.2 追加・変更・削除・移動

| 通知 | 動作 |
| --- | --- |
| `NotifyAssetAdded` | 既知Pathなら同じRecord、未知Pathなら新IDを作りFile情報を取得 |
| `NotifyAssetChanged` | 未登録なら追加扱い、登録済みなら同じIDのRecordを更新 |
| `NotifyAssetRemoved` | Path対応とAsset Recordを削除。IDを予約し続けない |
| `NotifyAssetMoved(old,new)` | 呼出側が移動と確定できる場合だけ、同じIDのPathを変更 |

`RefreshFromDisk`は同一内容を見ても移動を推測しない。誤って複製Fileを同一Assetと扱わないためである。File移動を単なる「旧Path消失＋新Path発見」として再走査した場合、新Pathには新IDが付く。IDを維持したい移動操作は、Project側から`NotifyAssetMoved`を明示的に呼ぶ。

#### 5.3 走査

走査Rootは`Assets/`と`resources/`。再帰走査し、権限エラーはSkipする。通常Fileだけを登録し、走査後に存在しない既知Pathを除去してRegistryを保存する。これは常駐File Watcherではなく、起動時または呼出側が明示した時のSnapshot走査である。

### 6. Hash

File Hashは64-bit FNV-1aで、64 KiBずつ読み込み、大文字16進文字列を返す。共同制作側と同じアルゴリズム・形式で、内容同一判定と不要な再転送防止に使う。

- 暗号学的Hashではない。改ざん検出や認証用途に使わない。
- File Sizeと更新時刻が前回と同じならCache済みHashを返す。
- Fileを開けなければ空文字列になる。
- Sizeと更新時刻を保ったまま内容だけ変える特殊な操作では再計算されない可能性がある。

### 7. 依存関係

`AssetRegistry`は各AssetのPath依存を`AssetDependencyLink { path, id }`として保持する。参照先がRegistryに存在すれば`id`を解決し、存在しなければ空IDのまま保持する。これにより「参照がある」と「現在その参照先が見つからない」を区別できる。

| API | 返すもの | 用途 |
| --- | --- | --- |
| `GetForwardDependencies(assetId)` | 対象Assetが参照する`AssetDependencyLink`一覧 | Project Windowの依存先表示、Build時の追跡。 |
| `GetReverseDependencies(assetId)` | 対象Assetを参照するAssetId一覧 | 削除時の参照元警告。 |
| `GetMissingDependencies(assetId)` | 解決できない参照Path一覧 | Project Windowの赤字Missing表示。 |
| `RefreshDependencies(path)` | 対象Recordの依存再抽出と索引再構築 | Reimport、追加、変更時。 |

テキスト形式は`.scene`、`.prefab`に限らず、`.animgraph`、`.effect`、`.effectdef`、`.material`、`.inputactions`など13種の対象形式から`Assets/`または`resources/`で始まるPath Tokenを抽出する。区切りは`|`、改行、引用符、Backslashで、重複は除去する。正規化後に絶対Path、`..`、対象Root外を拒否する。

ModelはEditor Adapterの`getDependencies`経路で、読込済みMaterial DataからBase Color、Normal、Metallic、Roughness、AO、Emission等のTexture Pathを抽出する。FBX/OBJの解析結果に存在するTextureだけを登録するため、RegistryがFBX内部の全参照やShader Includeを完全に列挙するものではない。

依存索引は追加、変更、削除、明示的なMove時に再構築する。Project Window経由のMove/RenameではAssetIdを維持するため、依存元と依存先の関係も維持される。外部Toolによる単純再走査は従来どおりMoveと断定しない。

### 8. 利用フロー

1. Project Windowや共同制作がFile追加・変更・移動・削除を検知する。
2. RegistryへPath状態を通知する。
3. 内容変更なら`AssetManager::NotifyFileChanged`へ集約する。
4. Handlerが既存ManagerのCacheを安全に無効化する。
5. `AssetNotifyResult`をRegistryへ戻し、UI/Logへ結果と理由を表示する。
6. `RequiresManualAction`ならAnimationはPlay再開、Scriptは再Buildを利用者へ要求する。
7. Project Windowは選択Assetの依存先・参照元・Missing参照を表示する。削除前は参照元を警告し、Reimport後は`RefreshDependencies`で関係を取り直す。

### 9. 既知の制限と禁止事項

- Registry単体にはFile Watcher UIも常時監視Threadもない。
- PathとAssetIdのRegistryをScene参照の全面的なUUID化へ置き換えたわけではない。
- Unknown Assetは自動反映しない。
- Script DLLの実行中差替えをHot Reload対応と記述しない。
- AnimationをFile単位で安全にInvalidateできると記述しない。
- `.material`を独立Loaderが自動再読込すると記述しない。
- Hashをセキュリティ検証へ流用しない。
- Script DLL依存は依存Graphの対象外である。Pathが古い参照を自動修復せず、Missingとして表示する。

### 10. 確認項目

| 試験 | 期待結果 |
| --- | --- |
| Textureを上書き | GPU Resourceが無効化され、再同期後に表示が更新 |
| Audioを上書き | 使用中Voiceが停止し、次回Playで新Clipを読込 |
| `.effect`を上書き | 新旧2系統のEffect Cacheが残らない |
| Animationを上書き | 自動成功扱いにせず、Play再開を案内 |
| Scriptを上書き | 実行中DLLを差し替えず、再Buildを案内 |
| Project Window経由の移動 | AssetIdが同じままPathだけ更新 |
| 外部Toolで移動後に単純再走査 | 自動同一視せず新Pathへ新IDとなり得る |
| Registry再起動 | Path/IDは維持、Hash/依存/状態は再計算 |
| ModelのTexture差替え | ModelのForward DependencyへTextureが現れ、Texture側のReverse DependencyへModelが現れる |
| 参照中Textureの削除 | 削除確認に参照元が出て、依存先はMissingとして表示される |
| Project Window経由のReimport | 依存関係が再抽出される |

この文書の確認はソース契約の照合であり、上記Hot Reloadの画面・音・2台間同期を実行確認した結果ではない。

### 11. 関連ファイル

- `Source/Engine/Asset/AssetType.h/.cpp`
- `Source/Engine/Asset/AssetManager.h/.cpp`
- `Source/Engine/Asset/AssetRegistry.h/.cpp`
- `Source/Engine/Editor/EditorAssetManagerAdapters.h/.cpp`
- `Source/Engine/Core/GameScene.cpp`
---

## Lighting・GI

更新基準: 2026-09-25

このファイルは、光まわり(直接光・影・間接光・空気中の散乱)の**実装仕様**をまとめたもの。
使用者向けの説明は `docs/component-reference.md` にあり、こちらは
「どのパスが何を計算し、どのデータがどこを通るか」を書く。

---

### 1. パス構成

1 フレームの光まわりの処理順は次のとおり。

| 順 | パス | 実体 | 何を作るか |
| --- | --- | --- | --- |
| 1 | Shadow Map | `ShadowDepth.VS/PS.hlsl` | Sun の CSM(4段) と Point Light のキューブ影(6面)を 1 枚の Atlas へ |
| 2 | **Light Probe Bake** | `GI/ProbeCapture.*`, `GI/Probe*.CS.hlsl` | Probe の SH9 係数と八面体の距離モーメント |
| 3 | Scene HDR | `Object3d.VS/PS.hlsl` | 直接光 + 間接光 + 反射を HDR へ |
| 4 | GBuffer | `GBuffer/GBuffer.PS.hlsl` | 後段の AO / SSR / SSGI 用 |
| 5 | SSGI | `PostProcess/SSGI.PS.hlsl` → `SsgiTemporal.PS.hlsl` → `SsgiUpsample.PS.hlsl` | 画面内の近傍からの間接光(任意)。半解像度 → Temporal → 加算合成 |
| 6 | **Volumetric Light Shaft** | `PostProcess/VolumetricLightShaft.PS.hlsl` | 空気中の光の筋(God Ray) |
| 7 | Bloom / Glare / Final Composite | `PostProcess/*` | 露出・トーンマッピング・合成 |

Light Probe Bake が Shadow Map の直後にあるのは、Bake される間接光を
**同じフレームの影と整合させる**ため。

---

### 2. 直接光

通常のLight Componentは、Sunを含めて1フレーム最大16灯を評価する。候補が16灯を超える場合はSunを優先し、その後はカメラに近い順に選ぶ。Shadow Atlasは従来どおり5×5タイルのため、収まるライトだけが影を描画し、残りは照明を維持したまま影だけを無効化する。

発光マテリアルから生成する簡易Emissive Lightは別枠で最大32灯。CPU側の`kMaxEmissiveLights`とHLSL側の`CG2_MAX_EMISSIVE_LIGHTS`は同じ値を保つ。

#### 2.1 拡散反射は Lambert

`Object3d.PS.hlsl` の直接光は素の Lambert(`NdotL < 0` で 0)。

```
EvaluateDiffuseCosine(NdotL, wrap) = saturate((NdotL + wrap) / (1 + wrap))
```

`wrap` はマテリアルの `Subsurface` そのもの。**既定 0 なので純粋な Lambert** になる。
肌・葉など、意図して光を回り込ませたい材質だけ `Subsurface` を上げる。

> 補足: 以前は `surfaceMode == 2` の材質に最低 0.38 の wrap が強制されており、
> Subsurface を 0 にしても横向きの面が約 25% 光っていた。これは物理的に誤りなので撤廃した。
> **側面や裏が真っ黒にならないのは ambient / IBL / GI の仕事**であり、
> 直接光側に wrap を混ぜて誤魔化す設計にはしない。

#### 2.2 影

`Shadow/ShadowSampling.hlsli` に集約。以前は Object3d / Volumetric / ProbeCapture の
3 か所へ写経されかけていたため、テクスチャとサンプラーを引数で受け取る形で共有化した。

- Sun: カメラ距離で 4 段の Cascade を選び、境界は 88% 地点から次段へ補間
- Point: 光源→ピクセルの最大成分軸で 6 面から 1 面を選ぶ
- バイアス: 平行投影は `NdotL` で 0.0020〜0.00035 を補間。
  透視投影(Point のキューブ面)は深度が 1/z 分布なので、
  ワールド距離で決めたバイアスをその深度での NDC 変化率へ換算する。

```
ndcPerWorldUnit = (near * far) / ((far - near) * viewDepth^2)
bias            = clamp(worldBias * ndcPerWorldUnit, 0.00002, 0.01)
```

---

### 3. 間接光 (Light Probe GI)

DDGI(Dynamic Diffuse Global Illumination)準拠。

#### 3.1 データ

| 内容 | 形式 | 置き場所 |
| --- | --- | --- |
| 放射照度 | SH9 (L2, RGB 27 係数を float4 × 9 で保持) | `StructuredBuffer<float4>` t20 |
| 可視性 | 八面体 16×16 の距離モーメント(平均, 2乗平均) | `Texture2D<float2>` t21 |
| グリッド定義 | `LightProbeGridData` (64 byte) | b2 (`EmissiveLightArray` 末尾) |

Probe の総数上限は 4096。Descriptor は SRV Heap の **57-62 番**を使う。

#### 3.2 Bake

1回のFull Rebake要求につき全Probeを一巡し、1回のBake Batchでは最大8 Probeを処理する。全Probeを終えると停止し、次のScene状態変更要求まで結果を再利用する。常時ラウンドロビンで焼き続ける実装ではない。

1. **キャプチャ** — Probe 位置から 90° FOV × 6 面を 32×32 で描画。
   MRT で RT0 = 放射輝度、RT1 = Probe からの距離。
   材質の拡散のみを計算する(Irradiance Probe に鏡面反射を焼くと破綻するため)。
2. **SH 投影** (`ProbeShProjection.CS.hlsl`) — 1 スレッドグループ = 1 Probe。
   6 面の全テクセルを立体角で重み付けして SH9 へ積分する。

   ```
   dω = (4 / N^2) / (1 + s^2 + t^2)^1.5
   L_k = Σ radiance * Y_k(dir) * dω
   ```

   **何にも当たらなかったテクセル**(距離が遠方センチネル)は、その方向の解析的な空を評価する。
   これにより囲まれた Probe は空を一切拾わず、室内が正しく暗くなる。
3. **可視性** (`ProbeVisibility.CS.hlsl`) — 八面体の各テクセルについて、
   その方向へ 9 サンプルの円錐で距離を集め、平均と 2 乗平均を書く。

設定には`時間平滑`（ヒステリシス）があるが、現在のSchedulerが実行するFull Rebake中は`hysteresis=0`として上書きする。グリッド作成直後だけでなく、Scene状態変更による一巡も前回値との時間補間を行わない。`needsFullRebake=false`時はBake自体を開始しないため、通常運用で設定値のヒステリシスを使う継続更新経路はない。

#### 3.3 マルチバウンス

キャプチャのPixel Shaderには前回Probeを間接光として読み戻せるData Pathがある。ただし現在はFull Rebakeを一巡した後にSchedulerが停止し、Scene状態が変わらない限り次の一巡を開始しない。したがって、**静止Sceneで自動的に何巡も回ってMulti Bounceへ収束するとは保証しない**。再Bake時に前回係数をCapture側が参照する可能性はあるが、連続収束機能として利用者へ約束しない。

#### 3.4 実行時の参照

`GI/ProbeSampling.hlsli` の `SampleLightProbeGi`。

周囲 8 Probe を次の重みの積で合成する。

| 重み | 式 | 目的 |
| --- | --- | --- |
| トライリニア | 各軸の補間係数の積 | Probe 間の滑らかな遷移 |
| 背面 | `(dot(dirToProbe, N) * 0.5 + 0.5)^2 + 0.2` | 面の裏にある Probe の寄与を落とす |
| 可視性 | Chebyshev の不等式 | Probe と対象点の間に壁がある Probe を弾く |

```
if (dist > mean) {
    variance  = max(mean2 - mean^2, 0)
    chebyshev = variance / (variance + (dist - mean)^2)
    weight   *= chebyshev^3          // 3乗して残り火をはっきり落とす
}
```

八面体タイルは**タイル内でクランプした手動バイリニア**で読む。
ハードウェアのバイリニアだと隣の Probe のタイルへ滲むため。

戻り値は既存の Irradiance Cube と同じ `E / π` の尺度なので、
`diffuseEnvironment` をそのまま置き換えられる。

#### 3.5 従来の環境光との関係

```
indirect = Probe が有効な場所 ? probeIrradiance
                              : skyIrradiance * ambientShadowFactor
```

Probe が無い(または範囲外の)場所は従来どおり空 + Sun 遮蔽近似へ戻る。
`ambientShadowFactor` は Sun の Shadow Map を 1 タップ読んで
`lerp(0.15, 1.0, visibility)` する**大雑把な近似**で、Probe が使える場所では出番がない。

---

### 4. Sun Portal

窓を簡易的な Area Light として扱う軽量機能。フル GI ではない。

- 光の向きは Portal の外向き法線の逆で**固定**(放射状の計算はしない)
- 遮蔽は「**Sun → Portal 自身**」だけを既存の Cascaded Shadow で見る。
  「Sun → 対象ピクセル」の遮蔽は無関係なので使わない
  (対象点が壁の影でも、窓に日が当たっていれば光る)
- 「Portal → 対象ピクセル」の遮蔽は**判定しない**。隣室への漏れは `到達距離`で調整する

---

### 5. Volumetric Light Shaft (Sun Beams)

深度バッファ全体をレイマーチする**独立したポストエフェクトパス**。

表面 Pixel の陰影へ散乱を足すだけでは、何も無い空気中に浮かぶ光の筋は描けない
(空気だけの場所には Pixel Shader が走らないため)。そのため専用パスにしている。

1. 深度から各画面 Pixel のワールド位置を復元
2. カメラからそこまでを 24 ステップでレイマーチ(画面座標のディザで開始位置をずらす)
3. 各点で Sun の Shadow Map を 1 タップ読み、照らされている区間だけ散乱を積算
4. Henyey-Greenstein の位相関数で前方散乱を再現し、HDR へ加算合成

```
HG(cosθ, g) = (1 - g^2) / (4π * (1 + g^2 - 2g cosθ)^1.5)
```

奥行きは深度バッファそのものを使うため、物体がある所も遠方 Clip(空)も同じ式で扱える。

専用の Root Signature を持つ。既存の PostProcess Root Signature は 32bit 定数が
48 値までで、Cascaded Shadow を渡すデータ量に足りないため。

---

### 6. Root Signature と Descriptor の予算

Object 用 Root Signature は **64 DWORD ちょうど**を使い切っている。

| 内訳 | 個数 | DWORD |
| --- | --- | --- |
| Root CBV (b0 PS, b0 VS, b1, b2, b4) | 5 | 10 |
| Root SRV (t14, t15, t16, t17) | 4 | 8 |
| Descriptor Table (材質マップ 7 個を含む) | 16 | 16 |
| Light Probe の t20/t21 (1 テーブルへ集約) | 1 | 1 |
| 32bit 定数 b3 (WaterView / ShadowVP / ProbeCaptureView 兼用) | - | 29 |
| 合計 | | **64 / 64** |

**Root Parameter をこれ以上追加できない。** 追加が必要な場合は、
b3 の 29 定数を削るか、既存の Descriptor Table へ相乗りさせること。
Light Probe の SH と可視性を 1 テーブルにまとめているのはこの制約のため。

SRV Heap(1024 個)の割り当て:

| 範囲 | 用途 |
| --- | --- |
| 0-30 | 標準テクスチャ、HDR/Bloom/SSAO などの RT |
| 31-56 | 深度ピラミッド、再構築法線 |
| **57-62** | **Light Probe (SH SRV/UAV, 可視性 SRV/UAV, キャプチャ SRV × 2)** |
| 83-114 | GPU Culling、PostProcess Quality、Color Grading |
| 120-122 | OIT |
| 123-159 | ImGui |
| 160-197 | Temporal 履歴 |
| 198- | 動的テクスチャ(`EditorSceneObjectManager` が確保) |

57-62 は SRV(57,58) / UAV(59,60) / キャプチャ(61,62) が
それぞれ連続していないと Descriptor Table にまとめられない。**順番を変えないこと。**

---

### 7. 既知の制限

- Light Probe の Bake 結果はファイルへ保存しない。起動のたびに焼き直す(Lightmap 未対応)。
- Probe の総数上限は 4096。Scene に置ける LightProbeGroup は 1 つ。
- 半透明 Object と Ocean は Probe のキャプチャ対象外。
- Sun Portal は「Portal → 対象ピクセル」の遮蔽を見ない。
- Reflection Probe / IBL はシーンをその場で撮っておらず、外部で焼いた cubemap ファイルを読む。
  ファイルが無い場合は 32×32 の単色へフォールバックする。
- SSGI は半解像度 → Temporal → フル解像度へ加算、の 3 パス構成。
  Viewport の左上が奇数 pixel のとき半解像度側が半 pixel ずれるが、
  間接光は低周波なので実用上は問題にならない。
- 屋内外での Probe 切り替えは未実装。
- 自動Full Rebake要求はEdit中だけで、Play中の動的LightやShadow Caster移動はProbe GIへ自動追従しない。
- Full Rebake要求のState HashはLight/Shadow MatrixとModel ObjectのID・World Matrix・Mesh情報等を含むが、すべてのMaterial Property変更を網羅する保証はない。
- Oceanを置くだけでは暗黙のPlanar Reflection Probeを作らない。明示的なReflectionProbeだけをCapture対象にし、Ocean自身は水面Shader側で反射Textureを参照する。

---

### 8. PerformanceSettingsによる更新制御

Sceneに有効な`PerformanceSettings`がある場合、描画品質と高負荷更新の上限をScene単位で指定する。複数ある場合はScene順で最初の有効Componentだけを使用し、Gameplay Updateは間引かない。

| 対象 | 明示値 | Auto / 実際の挙動 |
| --- | --- | --- |
| View描画 | Sceneのみ / Gameのみ / 両方 | AutoはEdit中Scene、Play中Gameを優先 |
| Glare | 0.25〜1.0 | PostProcessのサンプル倍率とScene上限の小さい方 |
| Shadow Map | 1〜8 Frame | Play中のみ間引き可。GPU時間が目標Frame時間の85%以下/超過/120%超過で1/2/3 Frame |
| Planar Reflection | 1〜8 Frame | Play中のみ間引き可。同条件で2/3/4 Frame |
| Ocean FFT | 1〜8 Frame | 0=Autoは2 Frame。初期化、Spectrum/設定変更時は間隔外でも更新 |
| Light Probe Bake | ON/OFF | 間引き許可時はBake Batchを2 Frameに1回だけ進める |

ShadowはState Hashが変わった更新FrameだけAtlasを再描画し、それ以外は前回結果を再利用する。Planar ReflectionはEdit中にTarget/Scene状態変更、Play中に指定間隔で更新する。目標FPSは15〜240で、Frame予算は`1000 / targetFps` ms、Softしきい値は85%、Hardしきい値は120%である。

Light Probe Full Rebake要求は、Edit中にLight/Shadow Caster系State Hashが変わった時に発生する。`PerformanceSettings`のBake間引きをONにしても1 Batchの最大8 Probeは変わらず、Batchを開始できるFrameが半分になる。

---

### 9. 現行のBake開始・停止条件

| 条件 | 結果 |
| --- | --- |
| LightProbeGroup無効 / Resource未準備 | Dummy Descriptorは維持するがCaptureしない |
| Resource/Grid形状を新規作成 | `needsFullRebake=true`、Probe 0から開始 |
| Grid配置変更 | Resourceを再利用してProbe 0からFull Rebake |
| Edit中にScene/Light State Hash変更 | `RequestFullRebake()` |
| Play中 | Rendererから自動Full Rebakeを要求しない |
| 全Probe一巡完了 | `needsFullRebake=false`として停止 |
| 変更なしの次Frame | 6面CaptureもCompute投影も行わず前回結果を利用 |

---

### 10. PostProcess・Temporalとの接続状況

Lighting結果を受けるPostProcess経路は、PostProcess Componentの設定からRendererへ接続されている。

| 項目 | 現行接続 |
| --- | --- |
| Bloom | Intensity、Threshold、Soft Knee、ScatterをMulti-stage Bloomへ渡す |
| AA | `aaMode`でNone/FXAA/SMAA/Temporalを排他的に選択。NoneはPassthrough PSO |
| SMAA | Threshold、Corner Roundingを3-pass SMAAへ渡す |
| Temporal | Sharpness、History BlendをTemporal Resolveへ渡す |
| Final Composite | Exposure、White Point、Tone Mapping、Bloom、Saturation、Contrast、Vignette、Film Grain、Chromatic Aberration、AOをRoot Constantsへ渡す |
| View履歴 | Scene ViewとGame ViewでColor/SSR/Previous Depth/Write Indexを分離する |

GPU CullingはFrustum/Hi-Z Computeを使用し、結果を次FrameのGPU Predicationへ直接使う。CPU Readbackは行わないが、完全な`ExecuteIndirect`描画ではない。Renderer全体のPass順とResource所有は本書、機能状態は本書を参照する。

---

### 11. 関連ファイル

| 種別 | パス |
| --- | --- |
| 共有ライトデータ | `Assets/Shaders/Common/SceneLightData.hlsli` |
| 影サンプリング | `Assets/Shaders/Shadow/ShadowSampling.hlsli` |
| GI 数学 | `Assets/Shaders/GI/ProbeCommon.hlsli` |
| GI 実行時参照 | `Assets/Shaders/GI/ProbeSampling.hlsli` |
| GI Bake | `Assets/Shaders/GI/ProbeCapture.VS/PS.hlsl`, `ProbeShProjection.CS.hlsl`, `ProbeVisibility.CS.hlsl` |
| 光の筋 | `Assets/Shaders/PostProcess/VolumetricLightShaft.PS.hlsl` |
| GI 管理 | `Source/Engine/Renderer/EditorLightProbeManager.h/.cpp` |
| Scene描画・Bake Scheduling | `Source/Engine/Editor/EditorRenderManager.cpp` |
| Planar Reflection対象収集 | `Source/Engine/Renderer/EditorPlanarReflectionManager.cpp` |
| 定数バッファ定義 | `Source/Engine/Core/EditorCommonTypes.h` |

---

## 共同制作 内部設計

更新基準: 2026-09-26

この文書は共同制作機能を変更・調査する開発者向けに、コードの責務、通信経路、Revision、永続化、Play Mode分離、TeamItem、競合処理を説明する。利用者向けの操作と制限は`user-guide.md`を正とする。

### 1. 構成と責務

| 実装 | 主な責務 |
| --- | --- |
| `EditorTeamCollaborationManager` | Editor側の接続状態、差分検出、Revision、Lock、Presence、履歴、競合、TeamItem、UI |
| `CollaborationProtocol.h` | EditorとCG2TeamServerが共有するProtocol Version、Message名、上限、Project ID検証 |
| `CG2TeamServerMain.cpp` | Project単位のRevision確定、Change配信、履歴再送、Heartbeat、最大接続数、状態/PID File |
| `LauncherExperience` | Project登録、参加コード生成・照合、HubからのProject/Engine取得、Invite処理 |
| `LauncherGui` | Project公開、参加コード表示・入力、Tailscale Tailnet事前確認と切替、参加後の保存先表示・Explorer起動、Launcher上の導線 |
| `EditorRuntimeManager` | Play開始前のScene BackupとStop時の復元 |
| `EditorScene` | GameObject/Component/Property差分の適用、Scene Snapshotの保存・読込 |
| `AssetManager` / `AssetRegistry` | 受信Assetの登録、依存関係、Hot Reload通知 |

`EditorTeamCollaborationManager`がEditor側の唯一の同期データ所有者である。Hierarchy、Inspector、Project View、Scene ViewはTeamItem本体を保持せず、公開関数から対象別集計を取得してBadgeを描画する。

### 2. 接続の流れ

```text
LauncherでProjectをHubへ公開
  -> Project IDから参加コードを生成
  -> 参加者LauncherがHubのCatalogからコードを照合
  -> *.ts.net利用時は現在のMagicDNS suffixを確認し、確認後に該当Profileへ切替
  -> Project Snapshotと固定Engine Versionを取得
  -> ProjectSettingsへProject IDと共同制作接続先を保存
  -> Editor起動
  -> Editor / CG2TeamServer間でHandshake
  -> Protocol、Project ID、Engine互換情報を検証
  -> lastSyncedRevision以後の履歴を取得
  -> Scene/Asset Hash差分だけをCatch-up
```

参加コードはProject IDを64 bitの決定的Hashへ通して8文字を作る短い検索キーであり、秘密Tokenではない。LauncherはコードからProject IDを逆算せず、HubのProject Catalogを列挙して各Projectのコードと比較する。そのため、正しいHubが設定済みで、対象Projectが公開済みであることが前提になる。

Tailscale事前確認はHub URLのhostnameが`.ts.net`で終わる場合だけ実行する。必要suffixはhostnameの先頭Node名を除いた部分から求める。`tailscale status --json`の`MagicDNSSuffix`が一致すれば何もしない。不一致なら利用者の許可後に`tailscale switch --list`のProfile IDを順番に切り替え、各Profileのsuffixを照合する。該当しなければ元のActive Profileへ戻し、`tailscale login`で公式認証画面を開く。Launcherは認証情報・Auth Key・Node Keyを保存しない。

現在の共同制作ProtocolはVersion 3である。Version 2にProject ID、Protocol Version、Heartbeatが追加され、Version 3でSnapshot Revision、履歴再送、Offline 3-way同期が追加された。HandshakeではProtocolまたはProject IDが一致しなければPayloadを適用しない。

#### 2.1 複数Clientの認証と途中参加

Editor HostのTransportは複数Socketを保持し、送信Messageを接続中Clientへ配る。Protocol層では次の規則により、3台目以降の参加が既存参加者へ影響しないようにする。

- 認証済み状態は共通の真偽値ではなく、Handshakeを通過した`userId`の集合で保持する。
- Hostは未認証`userId`から届いたScene、Asset、Lock、Presence等のPayloadを適用しない。
- `handshakeOk`、`handshakeNg`、Heartbeat応答、途中参加用Scene Snapshot、Asset Offer、Lock状態には`targetUserId`を付ける。
- Clientは自分以外の`targetUserId`を持つMessageを破棄する。
- 互換性またはProject IDが違う3台目を拒否しても、Host全体を`Incompatible`にせず、接続済みClientの認証を維持する。
- 通常の確定変更は認証済みPeer全体へ配る。途中参加時のCatch-upだけを対象Clientへ限定する。

既定のRemote Client上限は4で、設定範囲は1〜32である。UI上の「最大接続数」はHost自身を含まないため、Hostと参加PC 2台の合計3台で作業する場合は2以上を指定する。

### 3. Project IDの単一性

Project IDは次の場所で同じ値を使う。

- `ProjectSettings/ProjectCollaboration.cg2`
- `ProjectSettings/TeamCollaboration.settings`
- `ProjectSettings/TeamCollaboration.invite`
- Launcherの登録Project
- HubのProject Catalog / Manifest / Snapshot Path
- CG2TeamServerの`--project-id`

Launcherで新規Projectを作る場合はUUIDをProject IDとして生成する。Project公開時はProject Metadata側のIDを優先し、配布設定に残った過去のIDでManifestを作らない。Invite生成も選択ProjectのIDを優先する。

CG2TeamServerはPID/状態FileへPort、Process ID、Revisionに加えてProject IDを記録する。Editorが同一PortのServerを調べ、現在Projectと異なるIDの古いServerであれば停止して現在IDで再起動する。これにより、過去ProjectのServerが残ってHandshakeだけ失敗し続ける状態を避ける。

### 4. Change EventとRevision

Editor上の同期操作は`EditorTeamChangeEvent`へ正規化する。主要な識別子は次のとおり。

| Field | 意味 |
| --- | --- |
| `changeId` | 変更自身の一意ID。再送・Commit照合にも使用 |
| `baseRevision` | 変更作成時に送信者が基準にしたRevision |
| `revision` | HostまたはCG2TeamServerが確定した全体順序 |
| `sceneUuid` / `scenePath` | Sceneの識別 |
| `objectUuid` | GameObjectの識別 |
| `componentUuid` | Componentの識別 |
| `property` | Propertyまたは特殊操作種別 |
| `operation` | Add/Delete/更新、`TeamItemUpsert`、`TeamItemDelete`等 |
| `oldValue` / `newValue` | 比較・適用する値。TeamItemは`newValue`へSerializeする |
| `snapshotData` | Scene Fragment、Full Snapshot、Asset本体等 |

Clientは変更を`change`として送り、Host/ServerがRevisionを付けた`commit`を全Peerへ配信する。Clientは自分のCommitを受け取るとUnsyncedから同じ`changeId`を除去し、`lastSyncedRevision`を更新する。Host自身のローカル変更はServerからCommitを受け直さない構成があるため、Host側でRevision確定とTeamItem反映を行う。

確定変更は`.team/change-log.jsonl`へ追記する。接続が切れてもローカル編集をUnsyncedへ保持し、再接続時にRemote履歴と`baseRevision`を比較する。

### 5. Scene・Asset同期

通常Updateでは次の処理を分離して行う。

1. Transport状態と受信Messageを処理する。
2. Heartbeat、Presence、Lockを更新する。
3. Edit中のSceneを0.75秒周期で比較する。
4. Assetを2秒周期で走査する。
5. 差分をLocal Queueへ入れ、Onlineなら送信する。

小さいProperty変更はGameObject Fragmentを使用し、構造変更は必要に応じFull Snapshotを使う。受信Sceneは一時Fileへ書いてLoad可能性を確認してから現在Sceneへ適用する。Remote適用中は`isApplyingRemoteChange_`を立て、同じ変更をローカル編集として再検出するEchoを防ぐ。

Assetは512 KiBを超えると`assetBegin`、`assetChunk`、`assetEnd`へ分割する。受信側は宣言Size、Chunk数、Hash、許可Pathを検証し、完成するまで本番Pathへ適用しない。上書き前はBackup、削除時はTrashを作る。

### 6. Play Modeの変更分離

Play開始時、`EditorRuntimeManager::TogglePlay()`は編集Scene全体とScene Pathを保存する。Stop時はRuntime Systemを停止してからBackupを`EditorScene`へ戻す。これは共同制作の有無に関係しない通常のPlay契約である。

共同制作ManagerはPlay中もSocket、Heartbeat、Revision、履歴を止めない。ただしScene/Assetの通常Scanを止め、Remote変更を`deferredPlayModeChanges_`へ積む。

```text
Play中にRemote変更を受信
  -> 競合とRevisionを確定
  -> Change Logへ保存
  -> Play中Sceneには適用せずDeferred Queueへ追加

Stop
  -> RuntimeManagerがPlay開始前Sceneを復元
  -> Team ManagerがDeferred Queueを先頭から適用
  -> 全件成功ならQueueを空にする
  -> 失敗したEvent以後はQueueへ残してError表示
```

自分がPlay前に送った変更のCommitはPlay開始前Backupにすでに含まれるので、Deferred Queueへ重複追加しない。TeamItemはScene/Assetを変更しないため、この保留経路へ入れずPlay中も即時反映する。

### 7. TeamItemのデータモデル

Note、Ping、Chat、Reviewは`TeamItem`構造体を共有する。

| Field群 | 内容 |
| --- | --- |
| Identity | `id`、`kind`、`creatorUserId/Name`、作成/更新時刻、更新者 |
| Target | `targetType`、`targetId`、`scenePath`、`componentUuid`、`propertyName` |
| Content | `text`、`parentId`、`assigneeUserId`、`mentionedUserId` |
| Scene | `worldPosition`、`hasWorldPosition` |
| Script | `scriptLine`、`codeContext`、`functionName` |
| Review | `reviewStatus`、`targetChangeId`、`targetRevision` |
| Ping | `expiresAtUnixMilliseconds`、`keepsPingHistory` |
| State | `isResolved`、TeamItem自身の`revision` |

TeamItemはScene Fileへ埋め込まず、`TeamItemUpsert`または`TeamItemDelete`のChange Eventとして通常のRevision/Change Log経路へ流す。このためOffline再送、途中参加の履歴再生、検索用の変更履歴に含められる。専用ServerはTeamItemの内部形式を解釈せず、通常のChangeとして順序付けできる。

返信は親本文へ追記せず、新しいIDと`parentId`を持つ独立TeamItemにする。これにより別の利用者が同時に返信しても同一Item編集競合にならない。親削除時は子孫を収集して削除Eventを送る。

### 8. TeamItem対象ID

| `targetType` | `targetId`の考え方 |
| --- | --- |
| `ScenePosition` | Scene Pathと生成UUID。World座標を別Fieldに保持 |
| `GameObject` | GameObject UUID |
| `Component` | Component UUIDを含む安定ID |
| `Property` | Component UUIDとProperty名を含むID |
| `Scene` / `Prefab` / `Asset` / `Script` | Project相対Path |
| `ScriptLine` | Script Pathを基準に行番号と文脈を別Fieldに保持 |
| `History` / `ChangeEvent` | 対象Change IDまたはRevision情報 |

Hierarchy、Inspector、Project Viewは`GetEditorTeamTargetItemSummary()`を呼び、Note/Ping/Chat/Review件数と未解決件数だけを受け取る。作成・一覧表示は`OpenEditorTeamTargetItems()`、Scene座標は`OpenEditorTeamScenePositionItems()`からTEAM Windowへ集約する。

Scene MarkerはTeamItemから毎Frame再構築する。ScenePositionは保存座標、GameObjectは現在TransformのWorld位置を使うので、Object移動後もMarkerが追従する。履歴を残すPingはItemとして残るが、期限後はScene上の一時表示を終了する。履歴を残さないPingは作成者だけが期限切れDeleteを送る。

### 9. TeamItemの競合

同一TeamItemを複数人が編集した場合、Hostは現在ItemのRevisionと受信Eventの`baseRevision`を比較する。現在Revisionが新しく、更新者も異なる場合は自動上書きせず`TeamItemConflict`へ積む。

解決方法は次の3つである。

- 自分側を採用: Local Itemを新しい変更として送る。
- 相手側を採用: Remote Itemを新しい変更として送る。
- 手動Merge: Local Itemを基準に、本文だけを手動入力内容へ置き換えて送る。

競合判定はTeamItem単位であり、本文だけでなく担当者、解決状態、Review状態等も同一Revisionに含む。Field単位の自動Mergeではない。同じItemの削除と編集も競合検出の入口は共通だが、現在の競合UIはItem内容を採用してUpsertする形式で、削除状態そのものを採用する専用Buttonはない。削除を最終結果にする場合は競合解決後に改めて削除する。

### 10. 通知と対象への移動

受信Itemが自分への担当、メンション、またはPingであれば通知Item IDへ追加する。通知と添付カードは対象TeamItemを開き、そこから対象へ移動する。

- GameObject、Component、Property: Scene対象を選択する。
- ScenePosition: Scene Cameraを保存位置へ移動する。
- Scene、Prefab、Asset、Script: Project Assetを選択または外部Editorで開く。
- History/ChangeEvent: 変更履歴Filterと該当Changeを開く。

CG2Engine内蔵Script Editorは存在しない。ScriptLineは行番号とコード文脈を保持するが、共同カーソル、選択範囲、同じコード範囲の警告、Gutter Icon、厳密な行Jumpは別機能が必要である。

### 11. Lock、Presence、Remote Cursor

PresenceにはUser、Panel、Action、Scene、Asset、Object UUID、Component UUID、Property、Cursor座標、Scene Camera、Play/Build状態を含む。Editor全体のRemote CursorはTEAM Windowを閉じていても描画する。

Lockは主にComponent UUIDまたはGameObject構造へ掛け、Heartbeatで所有状態を更新する。Network切断やTimeout後は猶予を置いて解放する。Lockは先行編集を見せて競合を減らす仕組みであり、最終整合性はRevisionと競合処理が担当する。

### 12. 永続化と復旧

| 保存物 | 用途 |
| --- | --- |
| `TeamCollaboration.settings` | User、接続先、Project ID、Revision、自動接続 |
| `change-log.jsonl` | 確定変更とTeamItemの再構築 |
| `live/current.scene` | 現在の比較基準 |
| `base/current.scene` | 3-way比較のBase |
| `conflicts/<change-id>` | Local/Base/Remote/Resolutionの退避 |
| `backups` | Scene/Asset上書き前の復旧 |
| `trash` | Remote削除Assetの復旧 |

TeamItem専用Databaseは持たず、Change Log再生で`teamItems_`を再構築する。変更履歴を手作業で切り詰める場合はTeamItemも失われ得るため、Checkpoint/Snapshotと一緒に扱う。

### 13. 拡張時の手順

#### TeamItem種類を増やす

1. `TeamItem.kind`の値と表示Labelを追加する。
2. Composer、Filter、一覧、履歴Category、対象集計を更新する。
3. Serialize/Deserializeで必要Fieldを往復できるようにする。
4. 旧Change LogでFieldが無い場合の既定値を決める。
5. Offline作成、途中参加、同時編集、削除、返信、通知を確認する。

#### TeamItem対象を増やす

1. 再起動後も安定する`targetId`を決める。表示名や配列Indexだけを使わない。
2. 対象画面は集計APIだけを呼び、TeamItemの複製を所有しない。
3. 作成導線、Badge、対象への移動、対象消失時の表示を実装する。
4. Scene/Prefab/Assetの移動・改名時にIDが維持できるかを確認する。

#### 通信形式を変える

EditorとCG2TeamServerの双方を更新し、互換性を破る場合は`kCollaborationProtocolVersion`を上げる。旧Buildを暗黙に受け入れず、Handshakeで明確な拒否理由を返す。

### 14. 調査用チェックリスト

1. 接続診断のProtocol、Project ID、Engine Version、Role、Revisionを両端で比較する。
2. CG2TeamServerの状態Fileに現在Project IDがあるか確認する。
3. `Unsynced Changes`と`Last Synced Revision`が進んでいるか確認する。
4. `.team/change-log.jsonl`へ対象`changeId`とTeamItem操作があるか確認する。
5. Play中の問題はDeferred Queue追加と、Stop後のScene復元・Queue適用の順を確認する。
6. Asset問題はBegin/Chunk/End、宣言Size、Hash、Backup/Trashを確認する。
7. UI Badge問題はTeamItem本体ではなく、`targetType`と`targetId`の一致を確認する。
8. ScriptLine問題は外部Editor連携の制限と、保存された行番号・文脈を分けて確認する。

### 15. 関連ソース

- `Source/Engine/Editor/EditorTeamCollaborationManager.h/.cpp`
- `Source/Engine/Editor/EditorTeamUuid.h/.cpp`
- `Source/Engine/Editor/EditorRuntimeManager.h/.cpp`
- `Source/Engine/Editor/EditorScene.h/.cpp`
- `Source/Engine/Editor/EditorSceneViewManager.cpp`
- `Source/Engine/Editor/EditorHierarchyPanel.cpp`
- `Source/Engine/Editor/EditorInspectorPanel.cpp`
- `Source/Engine/Editor/EditorBottomPanel.cpp`
- `Source/Engine/Collaboration/CollaborationProtocol.h`
- `Tools/CG2TeamServer/CG2TeamServerMain.cpp`
- `Tools/CG2Launcher/LauncherExperience.h/.cpp`
- `Tools/CG2Launcher/LauncherGui.cpp`
- `docs/user-guide.md`
- `docs/user-guide.md`
- `docs/user-guide.md`
---

## 外部認識・Online・Haptics 実装仕様

音声認識 / 画像認識 / Cloudflare オンライン連携 / FeelKit Haptics の 4 機能を、
それぞれ独立したモジュールとして実装したときの構成と現状をまとめる。

利用者向けの導入・設定・Script例・配布確認は[user-guide.md](user-guide.md)、実装者向けの所有権・Thread・Memory・DirectX 12/OpenGL境界は本書を参照する。

4 機能とも次の順序を守り、外部ライブラリや SDK の型をゲームコードへ露出させない。

```
Game Component
     ↓
Engine API (SpeechSystem / VisionSystem / OnlineService / HapticSystem)
     ↓
Interface (ISpeechBackend / IVisionBackend / IOnlineBackend / IHapticBackend)
     ↓
Backend 実装
     ↓
External Library / Service
```

### ファイル構成

```
Source/Engine/
├ External/
│ └ ExternalFeature.h/.cpp      共通状態(Ready/Running/Unavailable/Error)、Error、Console ログ
├ Speech/
│ ├ SpeechTypes.h               SpeechResult / SpeechConfig / 認識モード
│ ├ ISpeechBackend.h            Backend 抽象
│ ├ WindowsSpeechApiBackend     SAPI 実装(Keyword 文法 / Dictation)
│ ├ WhisperSpeechBackend        マイク PCM 収録 + whisper.cpp CLI 非同期推論
│ ├ NullSpeechBackend.h         未実装 Backend を選んだ時の Unavailable 応答
│ └ SpeechSystem.h/.cpp         Session 管理、結果配布、Event、Keyword 判定
├ Vision/
│ ├ VisionTypes.h               ImageFrame / 各認識結果 / VisionConfig
│ ├ IVisionBackend.h            Backend 抽象
│ ├ ICameraSource.h             Camera 入力抽象
│ ├ MediaFoundationCameraSource Camera 取り込み(Worker Thread、BGRA)
│ ├ BuiltinVisionBackend        追加ライブラリ無しの色追跡 / 動体検出
│ ├ OnnxVisionBackend           ONNX Runtime の物体検出 / 画像分類 / 顔検出
│ └ VisionSystem.h/.cpp         Camera と認識の対応付け、推論間隔の間引き
├ Online/
│ ├ OnlineTypes.h               OnlineRequest / OnlineResponse / Leaderboard 他
│ ├ IOnlineBackend.h            Backend 抽象
│ ├ OnlineJson.h/.cpp           Online 専用の最小 JSON
│ ├ WinHttpOnlineBackend        HTTPS 送信(Worker Thread、Main Thread は止めない)
│ └ OnlineService.h/.cpp        Leaderboard / PlayerData / CloudSave / 再送 Queue
└ Haptics/
  ├ HapticTypes.h               HapticData / HapticClipData / Device 状態
  ├ IHapticBackend.h            Device 抽象
  ├ FeelKitHapticBackend        FeelKitHaptics 実装
  └ HapticSystem.h/.cpp         再生、強度、Pattern、Clip Asset、Audio/Physics 連携

Source/Engine/Editor/
├ EditorExternalFeatureManager  Component 設定 → 各 System、Input Action 反映、Script 通知
└ EditorExternalFeatureWindowManager  デバッグ Window(4 タブ)

Tools/CloudflareWorker/          Worker 参照実装、D1 スキーマ、wrangler 設定
```

### Component

| Component | 役割 |
|-----------|------|
| `SpeechRecognizer` | マイク入力の認識。Keyword Mode / Speech-to-Text Mode |
| `CameraInput` | Camera Device の映像取得 |
| `ImageRecognizer` | Camera フレームの認識 |
| `HapticSource` | 触覚再生（既存 Component を拡張） |

追加メニューは Inspector の「外部認識」カテゴリ（`HapticSource` は従来どおり「FeelKit」）。
Scene / Prefab へは `SpeechRecognizerExtension` + `SpeechRecognizerWhisperTimingExtension` / `CameraInputDeviceExtension` /
`ImageRecognizerExtension` / `HapticSourceExtension` 行として保存する。
既存 Scene の列位置は変えていないため、旧 Scene もそのまま読める。
PR #12 初期版が出力した8列の `CameraInputExtension` も互換読込する。23列以上の同名行は従来の Camera 制御設定として扱う。
認識結果や通信状態は保存しない。

### 現状の対応範囲

「未対応」は勝手に別処理へ置き換えず、`Unavailable` を返す。

#### 音声認識

| 項目 | 状態 |
|------|------|
| Keyword Mode（文法認識） | 実装済み（Windows Speech API） |
| Speech-to-Text Mode（Dictation） | 実装済み（Windows Speech API） |
| マイク選択 / 一覧 | 実装済み（指定不可なら既定マイクへ自動フォールバック） |
| 音量・認識状態・直近文字列のデバッグ表示 | 実装済み |
| Keyword → Input Action | 実装済み（キーワードごとに Action 名を指定） |
| Confidence しきい値 / 連続認識 | 実装済み |
| Whisper Backend | 実装済み（ローカル whisper.cpp CLI + GGML Model） |
| ONNX Backend | 未実装（選ぶと Unavailable） |

Whisper は 16 kHz / mono / PCM16 でマイクを収録する。既定では音量しきい値を超えた発話の後に
設定時間の無音が続いた時点で区切り、最大録音秒を待たずにWorker Threadから `whisper-cli.exe` へ渡す。
無音区切りをOFFにした場合だけ最大録音秒ごとの固定分割になる。最大録音秒は無音を検出できない場合の安全上限にも使う。
CLI は `Tools/Whisper`、Engine 実行ファイルの隣、
または `PATH` から検索する。モデルは `SpeechRecognizer` の「モデル」で指定し、
`ja-JP` のような言語名は Whisper の `ja` へ変換する。CLI は文全体の Confidence を返さないため、
空でない確定結果の Confidence は `1.0` として扱う。CLI またはモデルが無い場合は代替せず `Unavailable` を返す。

マイクと認識エンジンは 1 つのため Backend も 1 個だけ持つ。
複数 `SpeechRecognizer` がある場合、
どれか 1 つでも Speech-to-Text なら Dictation、全部 Keyword なら全 Keyword を合わせた文法で認識し、
Keyword 一致と Confidence 判定は Component ごとに行う。

#### 画像認識

| 項目 | 状態 |
|------|------|
| Camera 取得（Media Foundation） | 実装済み |
| Camera 一覧 / 解像度 / FPS 上限 | 実装済み |
| Color Tracking | 実装済み（内蔵 Backend、CPU） |
| Motion Detection | 実装済み（内蔵 Backend、CPU） |
| Object Detection | 実装済み（ONNX Runtime + YOLO 系モデル） |
| Image Classification | 実装済み（ONNX Runtime） |
| Face Detection | 顔検出モデルを指定した場合のみ |
| Face Landmark / Head Pose | 未対応（Unavailable） |
| Recognition Interval による間引き | 実装済み |
| Debug Preview（映像 + Bounding Box + ラベル） | 実装済み |
| 認識結果 → Input Action | 実装済み（ラベル / 頭部角度 / 動き / 色） |

ONNX の対応出力レイアウト:

- 分類 `[1, クラス数]`
- 検出(v8) `[1, 4 + クラス数, ボックス数]`
- 検出(v5) `[1, ボックス数, 5 + クラス数]`

ラベルは `Label (.txt)` で指定、空ならモデル横の同名 `.txt` / `.names` を探す。

#### Cloudflare オンライン連携

| 項目 | 状態 |
|------|------|
| 非同期 HTTPS 通信（Main Thread を止めない） | 実装済み |
| Leaderboard（Submit / Top、Global/Daily/Weekly/Season/Custom） | 実装済み |
| Player Data | 実装済み |
| Cloud Save（D1 / R2 自動切替） | 実装済み |
| 共有データ / メッセージ / イベント / デイリー / 簡易マッチ | 実装済み |
| オフライン状態保持と再送 Queue（終了後も復元） | 実装済み |
| Development / Production 分離 | 実装済み（Base URL・Header・Worker 環境とも別） |
| Debug Window（状態 / 最後の Request・Response / 通信時間 / Queue） | 実装済み |
| Worker / D1 / KV / R2 参照実装 | `Tools/CloudflareWorker/` |

再送 Queue は `SaveData/OnlinePendingQueue.cg2` へ保存する。
送信系（Score 送信、Player Data 保存、Cloud Save）だけを積み、取得系は積まない。

Workerは64KB超のCloud SaveをR2へ切り替えられるが、Version 14のC++高水準Wrapper `Online::GetCloudSave()`は65,536 Byte固定Bufferである。ゲームScriptから完全に取得する本文は65,535 Byte以下とし、大型R2 Object用の可変長/Chunk APIは未対応とする。

Project Settings（Inspector →「プロジェクト設定 → Online Services」）:

```
有効 / Provider / Environment / API Base URL(本番) / API Base URL(開発)
Game ID / Client Key / タイムアウト / 再送 Queue 上限
```

Client Key は公開鍵のみ。管理鍵は Worker 側の Secret に置く。

#### FeelKit Haptics

| 項目 | 状態 |
|------|------|
| 再生 / 停止 / 強度変更 | 実装済み |
| Pattern（一定 / パルス / 立ち上がり / 減衰 / 衝撃） | 実装済み |
| Runtime 中の Intensity / Frequency / Speed / Loop 変更 | 実装済み（Handle 経由） |
| HapticClip Asset（`.haptic`） | 実装済み |
| Audio Reactive | 実装済み（FeelKit の音声解析を使用） |
| Physics Reactive（Impulse → 強度） | 実装済み（Script から Impulse を渡す） |
| Device 状態（Unavailable / Disconnected / Connected / Error） | 実装済み |
| Device 未接続時もゲームは継続 | 実装済み（再生要求を捨てるだけ） |
| Editor Preview（Play 不要） | 実装済み |
| Debug 表示（Device / 再生中 Clip / 強度 / 残り時間） | 実装済み |

`.haptic` は他の CG2Engine テキスト Asset と同じ `Key|Value` 形式。

```
CG2EngineHapticClip|1
Name|Explosion
Duration|0.35
Intensity|0.9
Frequency|14
Pattern|4
Loop|0
Channel|0
```

### API 一覧（C++ Script から使うもの）

`EditorNativeScript.h` を include している Script から、そのまま使える公開 API の全一覧。
戻り値が `bool` のものは、機能が使えない / 対象が無い場合に `false` を返すだけで例外は出ない。

#### 共通 enum

```cpp
enum class ExternalFeatureStatus { Unavailable = 0, Ready = 1, Running = 2, Error = 3 };
enum class HapticDeviceStatus   { Unavailable = 0, Disconnected = 1, Connected = 2, Error = 3 };
enum class OnlineStatus         { Offline = 0, Connecting = 1, Online = 2, Error = 3 };
enum class OnlineLeaderboardScope { Global = 0, Daily = 1, Weekly = 2, Season = 3, Custom = 4 };
```

#### `Speech`（音声認識）

`Speech speech(gameObject);` で作る。対象は `SpeechRecognizer` Component を持つ GameObject。

| メソッド | 戻り値 | 内容 |
|---------|--------|------|
| `Start()` | `bool` | 認識を開始する。Play 開始で自動開始させる場合は Inspector の「Play 開始で認識開始」でよい |
| `Stop()` | `bool` | 認識を停止する |
| `IsRecognizing()` | `bool` | 認識中か |
| `IsSpeaking()` | `bool` | Backendが実際の発話区間を検出しているか |
| `IsProcessing()` | `bool` | Whisperが録音済み音声を推論中か |
| `GetActivityText()` | `std::string` | `話し中` / `推論中` / `認識待機中` / `停止中` |
| `GetLastText()` | `std::string` | 直近の確定文字列。まだ無ければ空文字列 |
| `GetLastConfidence()` | `float` | 直近の確定結果の Confidence（0.0〜1.0） |
| `GetDisplayText()` | `std::string` | 発話・推論中は状態、完了後は直近の認識文字列 |
| `SetUiText(uiGameObject)` | `bool` | `GetDisplayText()`を対象のText Componentへ設定する |
| `WasKeywordRecognized(keyword)` | `bool` | このフレームに登録キーワードを認識したか |

```cpp
Speech speech(gameObject);
speech.Start();

if (speech.WasKeywordRecognized("Jump")) {
    // ジャンプ処理
}

const std::string spoken = speech.GetLastText();  // 文字起こしモード時
speech.SetUiText(statusTextObject);                // 状態または認識文字列をUIへ表示
```

Whisperは発話中の部分文字列を生成せず、無音区間で録音を確定してからCLI推論する。そのため`IsSpeaking()`から`IsProcessing()`への遷移は表示できるが、推論完了前の発話内容を`GetLastText()`として返さない。

#### `Vision`（画像認識）

`Vision vision(gameObject);` で作る。対象は `ImageRecognizer` Component を持つ GameObject
（`StartCamera` / `StopCamera` は `CameraInput` Component を持つ GameObject に対して呼ぶ）。

| メソッド | 戻り値 | 内容 |
|---------|--------|------|
| `StartCamera()` | `bool` | Camera 映像の取得を開始する |
| `StopCamera()` | `bool` | Camera 映像の取得を停止する |
| `StartRecognition()` | `bool` | 認識を開始する |
| `StopRecognition()` | `bool` | 認識を停止する |
| `GetStatus()` | `ExternalFeatureStatus` | 認識の状態。未対応モードは `Unavailable` |
| `GetObjectCount()` | `int32_t` | 直近フレームで検出した物体数 |
| `TryGetObject(index, out)` | `bool` | 物体 1 件を `VisionObject` で受け取る |
| `IsObjectDetected(label)` | `bool` | 指定ラベルの物体が見えているか。`nullptr` で「何か見えているか」 |
| `GetTopClassification()` | `std::string` | 画像分類の最上位ラベル |
| `GetFaceCount()` | `int32_t` | 検出した顔の数 |
| `TryGetFace(index, out)` | `bool` | 顔 1 件の位置を受け取る |
| `TryGetHeadPose(yaw, pitch, roll)` | `bool` | 頭部方向。未対応 Backend では `false` |
| `HasMotion()` | `bool` | 動きを検出したか |
| `GetMotionMagnitude()` | `float` | 動き量（0.0〜1.0） |
| `TryGetTrackedColorCenter(x, y)` | `bool` | 追跡色の中心（0.0〜1.0）。未検出なら `false` |

```cpp
struct VisionObject {
    std::string label;
    float confidence;
    float x, y, width, height;  // 0.0〜1.0 の正規化座標
};
```

```cpp
Vision vision(gameObject);
vision.StartCamera();
vision.StartRecognition();

if (vision.IsObjectDetected("hand")) {
    // 手が映っている
}

float centerX = 0.0f, centerY = 0.0f;
if (vision.TryGetTrackedColorCenter(centerX, centerY)) {
    // 追跡色の位置で照準を動かす
}
```

#### `Haptic` / `HapticVoice`（触覚）

`Haptic haptic(gameObject);` で作る。対象は `HapticSource` Component を持つ GameObject。
Device が無くても呼べる（無効 Handle が返るだけ）。

| メソッド | 戻り値 | 内容 |
|---------|--------|------|
| `Play()` | `HapticVoice` | Component 設定で再生する |
| `PlayFromImpulse(impulse)` | `HapticVoice` | 衝突の強さから強度を作って再生する（Physics Reactive が有効な時だけ鳴る） |
| `Stop()` | `bool` | その GameObject の振動を止める |
| `Haptic::PlayClip(path, ownerId = -1)` | `HapticVoice` | Component 無しで `.haptic` を直接鳴らす（static） |
| `Haptic::SetMasterIntensity(v)` | `void` | 全体の強さ 0.0〜1.0（static） |
| `Haptic::GetDeviceStatus()` | `HapticDeviceStatus` | Device の接続状態（static） |

`HapticVoice` は再生中の振動 1 本を操作する Handle。

| メソッド | 戻り値 | 内容 |
|---------|--------|------|
| `IsValid()` | `bool` | 有効な Handle か（Device 無しの場合は `false`） |
| `IsPlaying()` | `bool` | まだ鳴っているか |
| `Stop()` | `bool` | この振動だけ止める |
| `SetIntensity(v)` | `bool` | 強度 0.0〜1.0 を再生中に変える |
| `SetFrequency(v)` | `bool` | 周波数（1 秒あたり回数）を変える |
| `SetPlaybackSpeed(v)` | `bool` | 再生速度倍率を変える |
| `SetLooping(b)` | `bool` | ループ切替 |
| `GetHandle()` | `uint32_t` | 内部 Handle 値 |

```cpp
HapticVoice voice = Haptic(gameObject).Play();
voice.SetIntensity(0.5f);

// 衝突イベントから
Haptic(gameObject).PlayFromImpulse(impulse);
```

#### `Online`（オンライン）

すべて `static`。インスタンス生成は不要。
取得系は「要求 → 次以降のフレームで参照」で使う（通信で Main Thread を止めないため）。

| メソッド | 戻り値 | 内容 |
|---------|--------|------|
| `SetPlayerIdentity(playerId, playerName)` | `void` | Player を決める。生成方式はゲーム側が選ぶ |
| `IsEnabled()` | `bool` | Project Settings で有効になっているか |
| `GetStatus()` | `OnlineStatus` | 接続状態 |
| `GetPendingRequestCount()` | `int32_t` | 送れずに再送待ちしている件数 |
| `SubmitScore(board, score, scope = Global)` | `bool` | Score を送る（失敗時は再送 Queue へ入る） |
| `RequestTopScores(board, count = 100, scope = Global)` | `bool` | 上位取得を要求する |
| `GetLeaderboardCount()` | `int32_t` | 受け取った件数 |
| `TryGetLeaderboardEntry(index, out)` | `bool` | 1 行を `OnlineLeaderboardEntry` で受け取る |
| `SetPlayerValue(key, value)` | `bool` | Player Data を 1 項目保存する |
| `RequestPlayerData()` | `bool` | Player Data の取得を要求する |
| `GetPlayerValue(key)` | `std::string` | 受け取った Player Data の値。無ければ空文字列 |
| `UploadCloudSave(slot, saveText)` | `bool` | セーブデータを送る |
| `RequestCloudSave(slot)` | `bool` | セーブデータの取得を要求する |
| `GetCloudSave()` | `std::string` | 受け取ったセーブデータ。まだなら空文字列 |

```cpp
struct OnlineLeaderboardEntry {
    std::string playerId;
    std::string playerName;
    int64_t score;
    int32_t rank;
};
```

```cpp
// 起動時に 1 回
Online::SetPlayerIdentity("player-0001", "CG2");

// スコア送信
Online::SubmitScore("Score", 12500);
Online::SubmitScore("Score", 12500, OnlineLeaderboardScope::Daily);

// ランキング取得
Online::RequestTopScores("Score", 100);
// …数フレーム後…
for (int32_t index = 0; index < Online::GetLeaderboardCount(); ++index) {
    OnlineLeaderboardEntry entry{};
    if (Online::TryGetLeaderboardEntry(index, entry)) {
        // entry.rank / entry.playerName / entry.score
    }
}

// プレイヤーデータ / クラウドセーブ
Online::SetPlayerValue("Coins", "1200");
Online::RequestPlayerData();
const std::string coins = Online::GetPlayerValue("Coins");
Online::UploadCloudSave("slot0", saveText);
Online::RequestCloudSave("slot0");
const std::string restored = Online::GetCloudSave();
```

### Script を書かずに使う場合

Inspector の設定だけで Input Action へ繋がる。Script は不要。

| やりたいこと | 設定場所 |
|------------|---------|
| 「Jump」と言ったら Input Action `Jump` を押した扱いにする | `SpeechRecognizer` の Keyword 行に「キーワード=Jump」「Input Action=Jump」 |
| 手が映ったら Input Action `Interact` | `ImageRecognizer` の 発火条件=ラベル検出 / 検出ラベル=hand / Action 名=Interact |
| 頭を右に向けたら `LookRight` | 発火条件=頭部 Yaw 右 / 角度しきい値=20 / Action 名=LookRight（※頭部方向 Backend が入ってから有効） |
| 動きがあったら `Wave` | 発火条件=動き検出 / Action 名=Wave |
| 認識したら任意の Script Action を呼ぶ | 「認識時 Action」「検出時 Action」に `BindAction` した名前を入れる |

検出位置は `Action 名 + "Position"` の Vector2 Action としても流れる（画面中央が原点、-1〜1）。

### DLL 境界の関数ポインタ（低レベル）

`EditorScriptRuntimeApi`（`EditorScriptApi.h`）の外部連携44本に、音声UI状態2本を追加した。
通常は上の高水準クラスを使う。独自 Wrapper を作る場合だけ直接参照する。
`kEditorScriptApiVersion` は 14 → 15。新しい2 Entryは構造体末尾へ置いて既存 Entry の配置を維持する。Version 14以前のDLLは従来Entryだけを使う限り互換Loadでき、Version 15の音声UI状態APIを使うScriptだけ再Buildする。

| 系統 | 関数 |
|------|------|
| 音声認識（7） | `SpeechStartRecognition` / `SpeechStopRecognition` / `SpeechIsRecognizing` / `SpeechGetLastResult` / `SpeechWasKeywordRecognized` / `SpeechIsSpeaking` / `SpeechIsProcessing` |
| 画像認識（13） | `VisionStartCamera` / `VisionStopCamera` / `VisionStartRecognition` / `VisionStopRecognition` / `VisionGetState` / `VisionGetObjectCount` / `VisionGetObject` / `VisionGetTopClassification` / `VisionGetFaceCount` / `VisionGetFace` / `VisionGetHeadPose` / `VisionGetMotion` / `VisionGetColorTracking` |
| Haptics（12） | `HapticPlaySource` / `HapticPlayClipAsset` / `HapticPlayFromImpulse` / `HapticStopSource` / `HapticStopHandle` / `HapticIsPlayingHandle` / `HapticSetHandleIntensity` / `HapticSetHandleFrequency` / `HapticSetHandlePlaybackSpeed` / `HapticSetHandleLooping` / `HapticSetMasterIntensity` / `HapticGetDeviceState` |
| Online（14） | `OnlineSetPlayerIdentity` / `OnlineGetConnectionState` / `OnlineIsEnabled` / `OnlineGetPendingRequestCount` / `OnlineSubmitScore` / `OnlineRequestTopScores` / `OnlineGetLeaderboardCount` / `OnlineGetLeaderboardEntry` / `OnlineSetPlayerValue` / `OnlineRequestPlayerData` / `OnlineGetPlayerValue` / `OnlineUploadCloudSave` / `OnlineRequestCloudSave` / `OnlineGetCloudSave` |

### Engine 内部 API（Engine 側 C++ から直接呼ぶ場合）

Editor / Manager を書き足すときに使う。ゲーム Script からは上の高水準クラスを使う。

#### `SpeechSystem`（`Source/Engine/Speech/SpeechSystem.h`）

```
static SpeechSystem& Get()
void SetBackend(std::unique_ptr<ISpeechBackend>)     void SelectBackend(SpeechBackendKind)
bool Initialize()                                    void Shutdown()
void Update(float deltaTime)
ExternalFeatureState GetState() const                ExternalFeatureError GetLastError() const
SpeechRuntimeStatus GetStatus() const                void EnumerateDevices(std::vector<SpeechDeviceInfo>&) const
void RegisterSession(int32_t, const SpeechConfig&)   void UnregisterSession(int32_t)
void UnregisterAllSessions()
bool StartRecognition(int32_t)                       bool StopRecognition(int32_t)
bool IsRecognizing(int32_t) const                    bool HasSession(int32_t) const
bool TryGetLatestResult(int32_t, SpeechResult&) const
const std::vector<SpeechResult>& GetFrameResults(int32_t) const
bool WasKeywordRecognized(int32_t, const std::string&) const
void SetOnSpeechStarted / SetOnSpeechEnded / SetOnSpeechRecognized
void SetOnKeywordRecognized / SetOnSpeechError
static bool IsKeywordMatch(const std::string& text, const std::string& keyword)
```

#### `VisionSystem`（`Source/Engine/Vision/VisionSystem.h`）

```
static VisionSystem& Get()
void Shutdown()                                      void Update(float deltaTime)
bool OpenCamera(int32_t, const CameraInputConfig&)    void CloseCamera(int32_t)
bool IsCameraOpen(int32_t) const                      bool HasCamera(int32_t) const
const ImageFrame* GetLatestFrame(int32_t) const        // Debug Preview 用
void EnumerateCameraDevices(std::vector<CameraDeviceInfo>&) const
std::vector<int32_t> GetCameraGameObjectIds() const   VisionRuntimeStatus GetCameraStatus(int32_t) const
void RegisterRecognizer(int32_t gameObjectId, int32_t cameraGameObjectId, const VisionConfig&)
void UnregisterRecognizer(int32_t)
bool StartRecognition(int32_t)                        bool StopRecognition(int32_t)
bool IsRecognizing(int32_t) const                     bool HasRecognizer(int32_t) const
bool TryGetResult(int32_t, VisionResult&) const        VisionRuntimeStatus GetRecognizerStatus(int32_t) const
std::vector<int32_t> GetRecognizerGameObjectIds() const
ExternalFeatureState GetState() const                  ExternalFeatureError GetLastError() const
```

#### `HapticSystem`（`Source/Engine/Haptics/HapticSystem.h`）

```
static HapticSystem& Get()
void SetBackend(std::unique_ptr<IHapticBackend>)      bool Initialize()        void Shutdown()
void Update(float deltaTime)
ExternalFeatureState GetState() const                  ExternalFeatureError GetLastError() const
HapticDeviceInfo GetDeviceInfo() const                 bool RefreshDevice()
const char* GetBackendName() const
HapticHandle Play(const HapticData&, int32_t ownerGameObjectId = -1, const std::string& displayName = {})
HapticHandle PlayClip(const HapticClipData&, int32_t ownerGameObjectId = -1)
HapticHandle PlayClipAsset(const std::string& clipAssetPath, int32_t ownerGameObjectId = -1)
HapticHandle PlayFromAudioFile(const std::string& audioFilePath, const HapticData&, int32_t = -1)
bool Stop(HapticHandle)        void StopGameObject(int32_t)      void StopAll()
bool IsPlaying(HapticHandle) const
bool SetIntensity / SetFrequency / SetPlaybackSpeed / SetLooping (HapticHandle, 値)
void SetMasterIntensity(float)                         float GetMasterIntensity() const
bool LoadClip(const std::string&, HapticClipData&)      bool SaveClip(const std::string&, const HapticClipData&)
void ClearClipCache()
bool TryAnalyzeAudioFile(const std::string&, HapticAudioAnalysis&)
static float MakeIntensityFromAudio(const HapticAudioAnalysis&, int32_t frequencyRange, float sensitivity, float intensityScale)
static float MakeIntensityFromImpulse(float impulse, float maximumImpulse)
float GetCurrentOutputIntensity() const                void GetPlaybackStatus(std::vector<HapticPlaybackStatus>&) const
int32_t GetActiveVoiceCount() const
```

#### `OnlineService`（`Source/Engine/Online/OnlineService.h`）

```
static OnlineService& Get()
void SetBackend(std::unique_ptr<IOnlineBackend>)       void Configure(const OnlineConfig&)
const OnlineConfig& GetConfig() const
void SetPlayerIdentity(const std::string& playerId, const std::string& playerName)
bool Initialize()        void Shutdown()               void Update(float deltaTime)
bool IsEnabled() const                                 ExternalFeatureState GetState() const
OnlineConnectionState GetConnectionState() const       ExternalFeatureError GetLastError() const
OnlineDebugInfo GetDebugInfo() const

// 汎用
OnlineRequestHandle RequestAsync(const OnlineRequest&, ResponseCallback = {})

// Callback 版（Engine 内部向け）
OnlineRequestHandle SubmitScore(board, score, scope = Global, ResponseCallback = {})
OnlineRequestHandle GetTopScores(board, entryCount, scope, LeaderboardCallback)
OnlineRequestHandle GetPlayerData(PlayerDataCallback)
OnlineRequestHandle SetPlayerValue(key, value, ResponseCallback = {})
OnlineRequestHandle SetPlayerData(const std::vector<PlayerDataEntry>&, ResponseCallback = {})
OnlineRequestHandle UploadCloudSave(slot, saveText, ResponseCallback = {})
OnlineRequestHandle DownloadCloudSave(slot, CloudSaveCallback)
OnlineRequestHandle GetSharedData(key, ResponseCallback)      OnlineRequestHandle SetSharedData(key, value, ResponseCallback = {})
OnlineRequestHandle GetMessages(ResponseCallback)             OnlineRequestHandle GetGlobalEvents(ResponseCallback)
OnlineRequestHandle GetDailyInfo(ResponseCallback)            OnlineRequestHandle GetMatchInfo(matchGroup, ResponseCallback)

// キャッシュ版（Script API が使う。Callback を持てない呼び出し元向け）
void RequestTopScoresCached(board, entryCount, scope)   const std::vector<LeaderboardEntry>& GetCachedLeaderboard() const
void RequestPlayerDataCached()                         const std::vector<PlayerDataEntry>& GetCachedPlayerData() const
bool TryGetCachedPlayerValue(key, std::string& out) const
void RequestCloudSaveCached(slot)                      const std::string& GetCachedCloudSave() const
bool IsLeaderboardRequestPending / IsPlayerDataRequestPending / IsCloudSaveRequestPending () const

// 再送 Queue
int32_t GetPendingQueueCount() const   void ClearPendingQueue()   void RetryPendingNow()
bool LoadPendingQueue()                bool SavePendingQueue() const

// 応答 JSON の解析
static bool ParseLeaderboard(const std::string& body, std::vector<LeaderboardEntry>&)
static bool ParsePlayerData(const std::string& body, std::vector<PlayerDataEntry>&)
static bool ParseCloudSave(const std::string& body, std::string& outSaveText)
```

#### `EditorExternalFeatureManager`（`Source/Engine/Editor/EditorExternalFeatureManager.h`）

Component 設定を各 System へ渡す実行担当。`g_editorRuntimeManager.GetExternalFeatureManager()` で取れる。

```
void Initialize(EditorScene*, EditorInputManager*, EditorScriptManager*, std::vector<std::string>* console)
void Start()      void Update(float deltaTime)      void Stop()
bool StartSpeechRecognition(int32_t)      bool StopSpeechRecognition(int32_t)
bool IsSpeechRecognizing(int32_t) const
bool TryGetSpeechResult(int32_t, SpeechResult&) const
bool WasSpeechKeywordRecognized(int32_t, const std::string&) const
bool StartCameraCapture(int32_t)          bool StopCameraCapture(int32_t)
bool StartImageRecognition(int32_t)       bool StopImageRecognition(int32_t)
bool TryGetVisionResult(int32_t, VisionResult&) const
HapticHandle PlayHapticSource(int32_t)    bool StopHapticSource(int32_t)
HapticHandle PlayHapticFromImpulse(int32_t, float impulse)
HapticHandle PreviewHapticComponent(const EditorComponent&, int32_t)   // Editor プレビュー
```

### デバッグ Window

メニュー「ウィンドウ（表示）→ 外部認識・オンライン」で開く。タブは
音声認識 / 画像認識 / オンライン / Haptics の 4 つ。
Play していない間も Haptics の Preview とログ反映だけは進む。

Console へ出すログは Backend 初期化、Device 接続 / 切断、認識エラー、
通信エラー、API エラー、FeelKit エラーに限り、同じ文言は数秒間抑制する。

### 実行順序

Play 中は `EditorRuntimeManager::Update` の中で

```
Input → (Speech / Vision / Haptics / Online) → Script → ...
```

の順に更新する。認識と通信はゲーム内 TimeScale の影響を受けない
（`unscaledDeltaTime` を使う）。

### ビルド設定

`CG2.vcxproj` へ次を追加している。

- 新規 `.cpp` / `.h`（`Source/Engine/External|Speech|Vision|Online|Haptics`、Editor 2 ファイル）
- リンク: `winhttp.lib` / `mfplat.lib` / `mfreadwrite.lib` / `mfuuid.lib` / `mf.lib`
- ONNX Runtime と FeelKitHaptics は既存の設定をそのまま使う

### データ型

仕様書で定義された構造体は、次の名前で実装している。座標はすべて 0.0〜1.0 の正規化値。

#### 音声認識（仕様書 5 項）

```cpp
struct SpeechResult {
    std::string text;         // 認識文字列
    float confidence;         // 0.0〜1.0
    bool isFinal;             // 確定結果なら true(途中結果は false)
    // 付加情報
    float startSeconds;       // 認識開始時刻(Play 開始からの秒)
    float endSeconds;         // 認識終了時刻
    std::string language;     // 使用言語
    std::string backendName;  // Backend 名
    std::string matchedKeyword;  // 一致した登録語。一致なしは空
};
```

`SpeechConfig`（Inspector 設定）、`SpeechDeviceInfo`（マイク一覧）、
`SpeechRuntimeStatus`（デバッグ表示）も同じ `SpeechTypes.h` にある。

#### 画像認識（仕様書 20 / 21 / 22 / 23 / 24 / 26 / 27 項）

```cpp
struct ImageFrame {              // Camera → Backend へ渡す 1 フレーム(BGRA)
    int32_t width, height, frameIndex;
    double timestampSeconds;
    std::vector<uint8_t> pixels;
};

struct ObjectDetectionResult {   // 複数同時に返す
    std::string label; float confidence;
    float x, y, width, height;
};

struct ImageClassificationResult { std::string label; float confidence; };

struct FaceDetectionResult { float confidence; float x, y, width, height; };

struct FaceLandmarkPoint { std::string name; float x, y; };  // 目 / 鼻 / 口 / 輪郭

struct HeadPoseResult { bool isValid; float yaw, pitch, roll; };

struct MotionResult { bool motion; float motionMagnitude; float centerX, centerY; };

struct ColorTrackingResult {
    bool isDetected; float centerX, centerY; float areaRatio;
    float boundsX, boundsY, boundsWidth, boundsHeight;
};

struct VisionResult {            // Backend が返す集約結果(仕様書 28 項)
    bool isValid; int32_t frameIndex; double timestampSeconds;
    VisionRecognitionMode mode;
    ExternalFeatureState state; ExternalFeatureError error;
    std::vector<ObjectDetectionResult> objects;
    std::vector<ImageClassificationResult> classifications;
    std::vector<FaceDetectionResult> faces;
    std::vector<FaceLandmarkPoint> faceLandmarks;
    HeadPoseResult headPose; MotionResult motion; ColorTrackingResult colorTracking;
    float inferenceMilliseconds;
};
```

`FaceLandmarkPoint` と `HeadPoseResult` は型と表示経路だけ用意してあり、
対応 Backend が無いため実行時は `Unavailable` になる。

#### オンライン（仕様書 38 / 44 / 45 項）

```cpp
struct OnlineRequest {
    std::string endpoint;  // "/leaderboard/submit" のように Base URL からの相対パス
    std::string method;    // "GET" / "POST"
    std::string body;      // JSON
    bool isQueueable;      // 送信系だけ true。再送 Queue へ積んでよいか
};

struct OnlineResponse {
    int32_t statusCode; std::string body; bool success;
    ExternalFeatureError error;      // 通信そのものが失敗した理由
    float elapsedMilliseconds;       // Debug Window の通信時間
};

struct LeaderboardEntry {
    std::string playerId; std::string playerName;
    int64_t score; int32_t rank;
};

struct PlayerDataEntry { std::string key; std::string value; };
```

#### Haptics（仕様書 65 項）

```cpp
struct HapticData {       // Backend へ渡す 1 回分の指示
    float intensity;          // 0.0〜1.0
    float frequency;          // パルス / 衝撃の 1 秒あたり回数
    float durationSeconds;
    float playbackSpeed;
    HapticPattern pattern;    // Constant / Pulse / RampUp / RampDown / Burst
    HapticChannel channel;    // Both / Left / Right
    bool isLooping;
};

struct HapticClipData {   // .haptic Asset
    std::string name; std::string assetPath;
    float durationSeconds; float intensity; float frequency;
    HapticPattern pattern; HapticChannel channel; bool isLooping;
};

struct HapticDeviceInfo { HapticDeviceState state; std::string deviceName, backendName; };
struct HapticPlaybackStatus { /* Clip 名 / 強度 / 周波数 / 残り時間 / 所有 Object */ };
```

#### 共通（仕様書 88 項）

```cpp
enum class ExternalFeatureState { Unavailable, Ready, Running, Error };

struct ExternalFeatureError {
    int32_t code;          // 0 はエラーなし
    std::string message;
};
```

### Event（仕様書 13 項）

`SpeechSystem` は 5 つの通知口を持つ。Editor 側（`EditorExternalFeatureManager`）が
どこへ繋いでいるかは次のとおり。

| Event | System API | Editor 側の接続先 |
|-------|-----------|------------------|
| `OnSpeechStarted` | `SetOnSpeechStarted` | 未接続（Script Action へは繋いでいない） |
| `OnSpeechEnded` | `SetOnSpeechEnded` | 未接続 |
| `OnSpeechRecognized` | `SetOnSpeechRecognized` | Inspector の「認識時 Action」へ Script 通知 |
| `OnKeywordRecognized` | `SetOnKeywordRecognized` | Console ログ + Keyword ごとの Input Action（`ApplySpeechToInput`） |
| `OnSpeechError` | `SetOnSpeechError` | Console へエラー出力 |

画像認識は Inspector の「検出時 Action」が発火条件の立ち上がりで Script 通知される。
Haptics は Script から `Haptic(gameObject).Play()` / `PlayFromImpulse()` を呼ぶ形で、
Collision / Damage から自動で鳴る配線は入れていない（仕様書 71 項は Script 経由で実現）。

### Inspector 項目

#### SpeechRecognizer（仕様書 12 項）

| 仕様書の項目 | Inspector |
|---|---|
| Enabled | Component 共通の有効チェック |
| Recognition Mode | 認識モード（キーワード / 文字起こし） |
| Language | 言語 |
| Microphone Device | マイク（一覧から選択、空欄なら既定） |
| Confidence Threshold | Confidence しきい値 |
| Continuous Recognition | 連続認識 |
| Keyword List | キーワード N ＋ Input Action N（可変数、追加 / 削除） |
| Backend | Backend |

追加項目: Play 開始で認識開始 / モデル / Whisperの無音終了・最大録音秒・終了無音秒・音声判定音量 / Action Map /
認識時 Action / 実行状態（状態・Backend・Device・音量・直近文字列・エラー）。

#### CameraInput（仕様書 17 項）

Camera（一覧から選択）/ 解像度 幅・高さ / FPS 上限 / Play 開始で取得開始 / Debug 表示。

#### ImageRecognizer（仕様書 30 項）

| 仕様書の項目 | Inspector |
|---|---|
| Camera Device / Resolution / FPS | CameraInput 側で設定し、「映像元 Camera」で参照する |
| Recognition Mode | 認識モード（7 種） |
| Model | Model (.onnx) ＋ Label (.txt) |
| Confidence Threshold | Confidence しきい値 |
| Update Interval | 推論間隔(秒) |
| Backend | Backend |
| Debug Preview | Debug 表示 |

追加項目: 色追跡（追跡色 / 許容差 / 最小面積比）、動体検出（動き量しきい値）、
Input Action 連携（Action Map / Action 名 / 発火条件 / 検出ラベル / 角度しきい値）、
検出時 Action、実行状態と直近の認識結果一覧。

#### HapticSource（仕様書 79 項）

| 仕様書の項目 | Inspector |
|---|---|
| HapticClip | Haptic Clip（空欄なら下の値を直接使う） |
| Play On Start | 自動再生 |
| Loop | ループ |
| Intensity | 強さ |
| Frequency | 周波数(回/秒) |
| Audio Reactive | Audio Reactive ＋ 周波数帯 / 感度 / 強度倍率 |
| Physics Reactive | Physics Reactive ＋ 最大 Impulse |
| Target Device | 対象 Device |

追加項目: 持続時間(ms) / パターン / チャンネル / サウンド / Editor プレビュー
（プレビュー・停止ボタン、再生本数と出力強度）。

### Worker Endpoint（仕様書 54 項）

| Method | Path | 用途 | ストレージ |
|--------|------|------|-----------|
| GET | `/health` | 疎通確認 | - |
| POST | `/leaderboard/submit` | Score 送信 | D1 |
| GET | `/leaderboard/top` | 上位取得 | D1 |
| GET | `/player` | Player Data 取得 | D1 |
| POST | `/player` | Player Data 保存 | D1 |
| GET | `/save` | Cloud Save 取得 | D1 / R2 |
| POST | `/save` | Cloud Save 保存 | D1（64KB 超は R2） |
| GET | `/shared` | 共有データ取得 | KV |
| POST | `/shared` | 共有データ書き込み（管理鍵必須） | KV |
| GET | `/messages` | ゲーム内メッセージ | D1 |
| GET | `/events` | グローバルイベント | D1 |
| GET | `/daily` | デイリー情報 | D1 |
| GET | `/match` | 簡易マッチ情報 | D1 |

Engine が自動で付ける Header: `X-CG2Engine-Game-Id` / `X-CG2Engine-Environment` /
`X-CG2Engine-Client-Key` / `X-CG2Engine-Player-Id`。

D1 テーブルは `leaderboard` / `player_data` / `cloud_save` / `game_message` /
`global_event` / `daily_info`。すべて `environment` 列で開発と本番を分ける。
KV は `shared:<gameId>:<key>` と Rate Limit カウンタ `rate:<gameId>:<playerId>`。
R2 は `<gameId>/<environment>/<playerId>/<slot>.save`。

### 仕様書 項目対応表

✓ = 実装済み / △ = 一部または前提つき / ❌ = 未対応（`Unavailable` を返す）

#### 音声認識（1〜14 項）

| 項 | 内容 | 状態 | 実装 |
|----|------|------|------|
| 1 | 4 機能を独立モジュール化 | ✓ | `Source/Engine/{Speech,Vision,Online,Haptics}` |
| 2 | 目的（音声コマンド / STT / イベント発火） | ✓ | `SpeechSystem` |
| 3 | 基本構成 | ✓ | SAPI 内部入力、または Whisper Backend の waveIn PCM Capture |
| 4 | `SpeechRecognizerComponent` | ✓ | `EditorComponentType::SpeechRecognizer` |
| 5 | `SpeechResult`（＋付加情報） | ✓ | `SpeechTypes.h` |
| 6 | 認識モード 2 種 | ✓ | `SpeechRecognitionMode` |
| 7 | Keyword Mode | ✓ | SAPI 文法 + `OnKeywordRecognized` |
| 8 | Speech-to-Text Mode | ✓ | SAPI Dictation |
| 9 | Input System 連携 | ✓ | Keyword ごとに Input Action 名を指定 |
| 10 | `ISpeechBackend` | ✓ | 仕様の 5 メソッド + 設定 / 音量 / Device 列挙 |
| 11 | Backend 候補 4 種 | △ | Windows Speech API / Whisperを実装。ONNXは `Unavailable` |
| 12 | Inspector 8 項目 | ✓ | 上記「Inspector 項目」 |
| 13 | Event 5 種 | △ | 5 種すべて System API にある。Started / Ended は Script へ未接続 |
| 14 | デバッグ表示 6 項目 | ✓ | Debug Window「音声認識」タブ |

`AIVoiceCommand` の既定の音声類似ModeはWhisperの認識文字列を類似変換せず、登録語だけで構成したSAPI Grammarから音響候補を得る。1位Scoreと2位Scoreの差をComponentごとに判定し、補正強度は `0.0` でScoreと候補差の両方を1.0とする厳格判定、`1.0` でInspector設定のしきい値と候補差まで最大に緩和する。任意の文字類似・完全一致Modeだけ同じGameObjectのSpeechRecognizer結果を使う。補正強度、一致しきい値、候補差、Cooldown、言語、マイク、登録語は `AIVoiceCommandExtension` に保存する。

#### 画像認識（15〜32 項）

| 項 | 内容 | 状態 | 実装 |
|----|------|------|------|
| 15 | 目的 | ✓ | `VisionSystem` |
| 16 | 基本構成 | ✓ | `ICameraSource` → `IVisionBackend` → `VisionResult` |
| 17 | `CameraInputComponent` | ✓ | Device 選択 / 開始 / 停止 / Frame / 解像度 / FPS |
| 18 | `ImageRecognizerComponent` | ✓ | `EditorComponentType::ImageRecognizer` |
| 19 | 認識モード 7 種 | △ | Landmark と HeadPose は選べるが実行時 `Unavailable` |
| 20 | Object Detection（複数同時） | ✓ | ONNX（YOLO 系出力 + NMS） |
| 21 | Image Classification | ✓ | ONNX（Softmax 上位 5 件） |
| 22 | Face Detection | △ | 顔として学習した検出モデルを指定した場合のみ |
| 23 | Face Landmark | ❌ | 型と描画のみ。専用 Backend が無い |
| 24 | Head Pose | ❌ | 同上 |
| 25 | Input System 連携 | ✓ | ラベル / 頭部角度 / 動き / 色を発火条件に選べる（頭部角度は 23・24 が入るまで発火しない） |
| 26 | Motion Detection | ✓ | 内蔵 Backend（縮小輝度の差分） |
| 27 | Color Tracking | ✓ | 内蔵 Backend（RGB 距離 + 面積比） |
| 28 | `IVisionBackend` | ✓ | `Initialize` / `ProcessFrame` / `GetResult` + 設定 / 対応判定 |
| 29 | Backend 候補 5 種 | △ | 内蔵と ONNX Runtime のみ。OpenCV / MediaPipe は `Unavailable` |
| 30 | Inspector 9 項目 | ✓ | 上記「Inspector 項目」 |
| 31 | 負荷対策（Recognition Interval / 低解像度） | ✓ | 推論間隔 + Camera 解像度指定 + モデル入力への縮小 |
| 32 | Debug 表示 | ✓ | 映像 Preview + Bounding Box + ラベル + Confidence + 色 / 動き |

#### Cloudflare オンライン連携（33〜58 項）

| 項 | 内容 | 状態 | 実装 |
|----|------|------|------|
| 33 | 目的 | ✓ | `OnlineService` |
| 34 | 想定構成 | ✓ | Game → HTTPS → Workers → D1 / KV / R2 |
| 35 | `OnlineService` に集約 | ✓ | ゲーム側は HTTP を書かない |
| 36 | 初期対象 8 機能 | ✓ | Leaderboard / PlayerData / CloudSave / メッセージ / 共有データ / イベント / デイリー / 簡易マッチ |
| 37 | Leaderboard 送信・取得 | ✓ | `SubmitScore` / `GetTopScores` |
| 38 | `LeaderboardEntry` | ✓ | `OnlineTypes.h` |
| 39 | ランキング種類 5 種 | ✓ | `LeaderboardScope`（Worker 側で日付 / 週 / 四半期 Bucket） |
| 40 | Player Data | ✓ | `GetPlayerData` / `SetPlayerValue` / `SetPlayerData` |
| 41 | Cloud Save | ✓ | `UploadCloudSave` / `DownloadCloudSave` |
| 42 | 小規模は D1・KV、大規模は R2 | ✓ | 64KB を境に Worker が自動で切り替え |
| 43 | 共通通信 API | ✓ | `OnlineRequest` / `OnlineResponse` |
| 44 | `OnlineRequest` | ✓ | endpoint / method / body（+ 再送可否） |
| 45 | `OnlineResponse` | ✓ | statusCode / body / success（+ error / 通信時間） |
| 46 | 非同期（Main Thread を止めない） | ✓ | `RequestAsync` + Worker Thread + 完了 Callback |
| 47 | オフライン対応 4 状態 | ✓ | `OnlineConnectionState` |
| 48 | 再送 Queue | ✓ | 8 秒間隔 / 最大 8 回 / `SaveData/OnlinePendingQueue.cg2` へ永続化 |
| 49 | 認証（秘密情報を埋め込まない） | ✓ | クライアントは公開 Client Key のみ。共有データ書き込みは Worker 側管理鍵が必要 |
| 50 | Player ID | ✓ | `Online::SetPlayerIdentity`。生成方式はゲーム側が決める |
| 51 | Project Settings | ✓ | Enabled / Provider / Base URL / Game ID / Environment（+ Client Key / タイムアウト / Queue 上限） |
| 52 | Development / Production 分離 | ✓ | Base URL・Header・Worker 環境・D1 / KV / R2 とも別 |
| 53 | Debug Window 7 項目 | ✓ | 接続状態 / 最後の Request・Response / Status Code / 通信時間 / Queue / Error |
| 54 | Worker Endpoint | ✓ | 上記「Worker Endpoint」 |
| 55 | D1 の用途 | ✓ | Leaderboard / PlayerData / メッセージ / イベント |
| 56 | KV の用途 | ✓ | 共有データ / Rate Limit / キャッシュ |
| 57 | R2 の用途 | ✓ | 大型 Save（Replay や画像も同じ経路で置ける） |
| 58 | サーバー側検証 | ✓ | 異常 Score / 大量 Request / 不正 Player ID / Client Key / サイズ上限 |

#### FeelKit Haptics（59〜81 項）

| 項 | 内容 | 状態 | 実装 |
|----|------|------|------|
| 59 | 目的（FeelKit 固有コードを呼ばない） | ✓ | `HapticSystem` + `IHapticBackend` |
| 60 | 基本構成 | ✓ | Game → Haptics API → FeelKit Backend → FeelKit |
| 61 | `HapticSystem` | ✓ | 再生 / 停止 / 強度 / Device / Pattern |
| 62 | `HapticSourceComponent` | ✓ | 既存 `HapticSource` を拡張 |
| 63 | 用途（銃撃・爆発・衝突ほか） | ✓ | Pattern + Audio Reactive + Physics Reactive で表現 |
| 64 | `HapticClip` を Asset として扱う | ✓ | `.haptic`（`Key\|Value` テキスト） |
| 65 | Clip 情報 6 項目 | ✓ | Duration / Intensity / Frequency / Pattern / Loop / Channel |
| 66 | 再生 API | ✓ | `HapticSystem::Play` / `PlayClip` / `PlayClipAsset` |
| 67 | GameObject から再生 | ✓ | `Haptic(gameObject).Play()` |
| 68 | 停止 | ✓ | `Haptic(gameObject).Stop()` / Handle 単位 / 全停止 |
| 69 | 強度 0.0〜1.0 | ✓ | Voice 単位 + 全体倍率 |
| 70 | Runtime 制御 4 項目 | ✓ | `HapticVoice::SetIntensity / SetFrequency / SetPlaybackSpeed / SetLooping` |
| 71 | Event 連携（Collision / Damage） | △ | Script から `Play()` / `PlayFromImpulse()` を呼ぶ。自動配線は未実装 |
| 72 | Audio 連携 | ✓ | FeelKit の音声解析結果から強度を作る |
| 73 | Audio Reactive 設定 4 項目 | ✓ | Audio Source（サウンド）/ 周波数帯 / 感度 / 強度倍率 |
| 74 | Physics 連携 | ✓ | `Clamp(impulse / MaxImpulse, 0, 1)` |
| 75 | `IHapticBackend` | ✓ | `Initialize` / `Play` / `Stop` / `SetIntensity` + Device 状態 |
| 76 | FeelKit Backend | ✓ | `FeelKitHapticBackend` |
| 77 | Device 検出 4 状態 | ✓ | Unavailable / Disconnected / Connected / Error |
| 78 | Device 未接続時も継続 | ✓ | 無効 Handle を返して捨てるだけ |
| 79 | Inspector 8 項目 | ✓ | 上記「Inspector 項目」 |
| 80 | Editor Preview | ✓ | Inspector のプレビュー / 停止ボタン（Play 不要） |
| 81 | Debug 7 項目 | ✓ | Debug Window「Haptics」タブ |

#### 共通設計（82〜91 項）

| 項 | 内容 | 状態 | 実装 |
|----|------|------|------|
| 82 | 外部実装を Component へ直接書かない | ✓ | Component は設定値だけを持ち、実処理は System 側 |
| 83 | Backend 4 種の Interface | ✓ | `ISpeechBackend` / `IVisionBackend` / `IOnlineBackend` / `IHapticBackend` |
| 84 | Backend 変更で Scene / Component / Script API を変えない | ✓ | Backend 種別は Inspector の選択値のみ |
| 85 | 存在しない機能を補完しない | ✓ | 未実装 Backend・未対応モードは `Unavailable` を返す |
| 86 | C++ Script から利用可能 | ✓ | `Speech` / `Vision` / `Haptic` / `Online` クラス |
| 87 | Editor から状態確認（Ready / Running / Unavailable / Error） | ✓ | `ExternalFeatureState` を Inspector と Debug Window に表示 |
| 88 | `ExternalFeatureError`（code / message） | ✓ | `ExternalFeature.h` |
| 89 | Console ログ（毎フレーム大量に出さない） | ✓ | 同一文言を 3 秒抑制、1 フレーム 64 行上限、Worker Thread 分は Main Thread で flush |
| 90 | Component 設定は Scene / Prefab へ保存、認識結果は保存しない | ✓ | `*Extension` 行のみ保存 |
| 91 | 最終構成 | ✓ | 下記ツリー（ファイル名は CG2Engine の命名へ合わせた） |

#### 最終構成（仕様書 91 項）

```
CG2Engine
├ Speech
│ ├ SpeechRecognizerComponent   … EditorComponentType::SpeechRecognizer
│ ├ ISpeechBackend              … WindowsSpeechApiBackend / NullSpeechBackend
│ └ SpeechResult                … SpeechTypes.h
├ Vision
│ ├ CameraInputComponent        … EditorComponentType::CameraInput
│ ├ ImageRecognizerComponent    … EditorComponentType::ImageRecognizer
│ ├ IVisionBackend              … BuiltinVisionBackend / OnnxVisionBackend
│ │                               （ICameraSource … MediaFoundationCameraSource）
│ └ VisionResult                … VisionTypes.h
├ Online
│ ├ OnlineService
│ ├ Leaderboard                 … OnlineService::SubmitScore / GetTopScores
│ ├ CloudSave                   … OnlineService::UploadCloudSave / DownloadCloudSave
│ ├ PlayerData                  … OnlineService::GetPlayerData / SetPlayerValue
│ └ Cloudflare Backend          … WinHttpOnlineBackend + Tools/CloudflareWorker
└ Haptics
  ├ HapticSystem
  ├ HapticSourceComponent       … EditorComponentType::HapticSource
  ├ HapticClip                  … HapticClipData / .haptic
  ├ IHapticBackend
  └ FeelKitBackend              … FeelKitHapticBackend
```

### 今後 Backend を足すときの手順

1. 対応する Interface（`ISpeechBackend` など）を実装した class を追加する
2. `SpeechSystem::SelectBackend` / `VisionSystem::CreateBackend` の分岐へ追加する
3. Inspector の Backend コンボの表示名から「(未実装)」を外す

Scene / GameObject / Component / Script API は変更しない（仕様書 84 項）。

---

## 外部認識・Online・Haptics 内部設計

更新基準: 2026-09-26

この文書はSpeech、Vision、Online、Hapticsの実装者向け内部設計書である。利用手順は`user-guide.md`、Component Fieldは`component-reference.md`、公開Script APIは`script-api-reference.md`、仕様項目との対応は本書を正とする。

### 1. 設計の目的

外部Device、OS API、SDK、Web Serviceは、未接続、権限拒否、通信断、モデル不一致、Device抜去が通常状態として発生する。GameplayコードへSDK型やOS Handleを直接出さず、失敗してもEditor本体とゲーム進行を止めないことを優先する。

```text
EditorComponent / C++ Script Wrapper
              ↓
EditorExternalFeatureManager / OnlineService
              ↓
SpeechSystem / VisionSystem / HapticSystem
              ↓
ISpeechBackend / ICameraSource / IVisionBackend / IHapticBackend / IOnlineBackend
              ↓
SAPI / Media Foundation / ONNX Runtime / FeelKit / WinHTTP
```

上位層が保持する型はEngine独自のPlain Dataだけである。`HINTERNET`、Media Foundation Interface、ONNX Session、FeelKit固有型はBackend内部から出さない。

### 2. Sourceと責務

| Directory / Class | 責務 |
| --- | --- |
| `Source/Engine/External` | 共通状態、共通Error、Thread-safeログQueue |
| `Source/Engine/Speech` | Session統合、Keyword振り分け、SAPI Backend |
| `Source/Engine/Vision` | Camera取得、Recognizer、内蔵/ONNX Backend |
| `Source/Engine/Haptics` | Voice合成、Clip Cache、Device Backend |
| `Source/Engine/Online` | 非同期Request、Response Cache、再送Queue、WinHTTP |
| `EditorExternalFeatureManager` | Component設定をSystemへ同期し、Input ActionとScript Actionへ結果を渡す |
| `EditorExternalFeatureWindowManager` | Speech/Vision/Online/Hapticsの観測UI |
| `EditorScriptManager` | DLL境界の44 Entryを各Systemへ橋渡しする |

`EditorExternalFeatureManager`はSDKを直接呼ばない。ComponentとSystemの接続だけを担当する。Device列挙UIも公開System APIを通す。

### 3. 所有権と寿命

各SystemはProcess内Singletonであり、Backendを`std::unique_ptr`で単独所有する。ComponentはBackend PointerやOS Handleを保存しない。

| 所有者 | 所有するもの | 破棄契約 |
| --- | --- | --- |
| `SpeechSystem` | Speech Backend 1個、GameObject ID別Session | Stopで認識停止、Session消去、Backend Shutdown |
| `VisionSystem` | Camera ID別`ICameraSource`、Recognizer ID別`IVisionBackend` | Recognizer Shutdown後、Camera Close、Map消去 |
| `HapticSystem` | Haptic Backend 1個、再生Voice、Clip Cache | Device出力を0へ戻してVoiceを破棄。Clip Cacheは明示ClearまたはProcess終了まで保持 |
| `OnlineService` | Online Backend 1個、In-flight Map、結果Cache、Pending Queue | Pending保存後、Worker停止、In-flight消去 |
| `MediaFoundationCameraSource` | Media Source/Reader、Capture Thread、最新Frame | Stop要求、Thread join、Media Source Shutdown、MF/COM解放 |
| `WinHttpOnlineBackend` | WinHTTP Session、Worker Thread、Request/Response Queue | Stop通知、Thread join、Session Handle Close、Queue消去 |

Play開始時にProject SettingsとComponent設定を同期し、自動開始対象だけDevice/認識を開始する。Play停止時はCallbackを外してから各SystemをShutdownし、次のPlayへ前回Sessionを残さない。HapticのInspectorプレビューだけはEdit中にもBackendを使うため、Play状態とは別に停止操作を持つ。

### 4. Thread契約

#### Main Thread

次はMain Threadだけから呼ぶ。

- Component走査とSession登録解除
- Speech/Vision/Haptics/Onlineの`Update`
- Input Action反映とScript Action Queue
- Script Runtime API Entry
- Debug Windowの状態取得と描画
- Online Response Callbackの実行

Gameplay側CallbackはWorker Threadから呼ばない。OnlineはWorkerがResponse Queueへ積み、`OnlineService::Update`がMain Threadで取り出してCallbackを実行する。

#### Camera Capture Thread

`MediaFoundationCameraSource`はCapture専用Threadを1本持つ。Worker側でも`CoInitializeEx(COINIT_MULTITHREADED)`を行い、Frame取得後に最新BGRA FrameをMutex保護領域へ移す。Main Threadの`TryGetFrame`は新しいFrameだけを受け取る。

Close順は次で固定する。

1. 停止Flagを立てる。
2. Capture Threadを`join`する。
3. Source ReaderとMedia Sourceを解放する。
4. `MFShutdown`と対応する`CoUninitialize`を行う。
5. 最新Frameと配信済みIndexを初期化する。

COMを初期化したThreadと解放するThreadの対応を崩さない。Capture ThreadからImGui、EditorScene、Scriptへ触れない。

#### Online Worker Thread

`WinHttpOnlineBackend`はWorker Threadを1本だけ持ち、Requestを直列実行する。`Send`はMutex内でRequestをQueueへ積み、Condition VariableでWorkerを起こす。Workerは同期WinHTTPを実行するが、Main Threadは待たない。

ShutdownではStop Flag、`notify_all`、`join`の順を守る。ThreadがWinHTTP Sessionを参照中にSession Handleを閉じてはならない。Response Queueと`lastError_`はMutexで保護する。

#### SpeechとHaptics

現在のSystem更新契約はMain Threadである。SAPIやFeelKit側が内部Threadを持つ場合も、Backend外へSDK Callbackの生Pointerを渡さず、Backendの`Update`または結果取得APIでEngine型へ変換する。

### 5. 共通状態とError

`ExternalFeatureState`は次の4状態だけを公開する。

| 状態 | 意味 | Gameplay側の扱い |
| --- | --- | --- |
| `Unavailable` | Backend、Device、モデル、設定のいずれかが使えない | 機能なしとして継続。別Backendへ勝手に代替しない |
| `Ready` | 初期化済みまたは開始待ち | Start可能。結果はまだない |
| `Running` | 取得、認識、通信、再生のいずれかが進行中 | Updateで状態を読む |
| `Error` | 処理が失敗した | `ExternalFeatureError`とConsoleを確認する |

Errorは整数Codeと人向けMessageを持つ。CodeだけでUI文言を推測しない。Backendが理由を返さなかった場合はSystemが一般的なMessageを補う。

`ExternalFeatureLog`はWorkerからConsole配列へ直接書かず、Mutex保護Queueへ積む。Main Threadの`Flush`でConsoleへ移し、同一Messageの連打を抑制する。Shutdown後は出力先PointerとPending Messageを破棄する。

### 6. Speech内部

Speech Backendはマイク資源を共有するため1個だけで、ComponentごとにSessionを持つ。複数Sessionが同時に有効な場合の統合規則は次である。

- 1つでもSpeech-to-TextならBackend全体をDictationへする。
- 全SessionのKeywordを重複除去して1つの文法へ渡す。
- Backendへ渡すConfidenceはSession中の最小値とする。
- 最終的なConfidence判定とKeyword振り分けはSessionごとに行う。
- Keyword比較はASCII空白を除去し英字を小文字化する。日本語UTF-8 Byte列はそのまま比較する。
- `frameResults`と`recognizedKeywords`は毎Frame先頭で消去する。
- `lastResult`は直近確定結果としてSessionに保持する。

Session設定変更でMode、Language、Microphone、Model、Keywordが変わった場合だけBackend設定を再適用する。認識要求中のSessionが0になればBackendを停止する。

Whisperは`WhisperSpeechBackend`、ONNX音声認識はNull Backendで`Unavailable`を返す。Auto選択が利用不可Backendへ黙ってFallbackする設計にはしない。

### 7. Vision内部

#### CameraとRecognizerの分離

CameraはGameObject IDごとに`CameraEntry`を持ち、Recognizerは別のGameObject IDごとに`RecognizerEntry`を持つ。1 Cameraを複数Recognizerから参照できる。RecognizerのCamera IDが`-1`なら、開いているCameraのうち最小GameObject IDを選ぶ。

RecognizerごとにBackendを持つ理由は、Model、Mode、Label、しきい値が異なるためである。ModeまたはBackend種別が変わり、現在Backendが新Modeを扱えない場合だけBackendを作り直す。

#### Frame形式

Engine境界では8bit BGRAを使用する。必要Byte数は次である。

```text
frameBytes = width × height × 4
640 × 480   = 1,228,800 bytes  約1.17 MiB
1280 × 720  = 3,686,400 bytes  約3.52 MiB
1920 × 1080 = 8,294,400 bytes  約7.91 MiB
```

Capture Sourceの`latestFrame_`と`VisionSystem::CameraEntry::latestFrame`の両方が存在し、受け渡し時にFrame BufferのCopyが発生する経路がある。さらにONNX入力TensorやResize用Buffer、Debug Preview Textureが別途必要になる。上表をProcess全体の最大Memoryと誤解しない。

Camera数を増やすとFrame BufferとCapture ThreadもCamera単位で増える。高解像度Cameraを複数開くより、1 Cameraを複数Recognizerで共有する。

#### 推論間引き

Camera取得FPSと推論頻度は別である。Cameraは`frameRateLimit`、Recognizerは`recognitionIntervalSeconds`で制御する。Interval到達後も`processedFrameIndex`と同じFrameなら再推論しない。

内蔵Backendは色追跡と動体検出に使う。物体検出、画像分類、顔検出はONNX RuntimeとModelが必要である。顔ランドマーク、頭部方向、OpenCV、MediaPipeは`Unavailable`である。

#### ONNX資源

Recognizer BackendはSession、SessionOptions、CPU MemoryInfo、入出力名、Label、Tensor用Memoryを所有する。SessionはModelまたはBackend再選択時に作り直し、Recognizer解除時にShutdownする。`SetIntraOpNumThreads(1)`により1 Session内のCPU Thread増加を抑えているが、Recognizerを増やせばSession数は増える。

Model入力Shape、色順、出力LayoutはModelごとに異なる。現在対応するDecode規則に合わないModelは、Load成功だけで正しい検出を保証しない。

### 8. Graphics API、GL、GPUとの境界

CG2Engineの描画BackendはDirectX 12であり、OpenGL Backendは使用していない。この外部連携層にもOpenGL Context、GL Texture、GL Bufferは存在しない。

Camera取得と認識の標準経路はCPU BGRA Memoryである。Media Foundation FrameをそのままDirectX 12 Textureとして共有するZero-copy経路や、DirectML/CUDA GPU推論経路は現在の契約に含まれない。ONNX BackendもCPU MemoryInfoを使う。

Debug Previewを画面へ出す場合の境界は次である。

```text
Media Foundation Camera
 -> CPU BGRA ImageFrame
 -> VisionSystem latestFrame
 -> EditorExternalFeatureWindowManager
 -> 64×48 Blockへ縮小Sample
 -> ImDrawListの色付き矩形
 -> 既存ImGui DX12描画へ合流
```

現行Previewは専用D3D12 Texture、Upload Resource、Descriptorを作らない。CPU Frameから3,072個のBlock色を取り、ImGuiの通常Draw Listへ追加する。したがってTexture寿命問題はない一方、Windowを開いたままにすると矩形頂点数とCPU Sampling Costが増える。Vision BackendへDX12 DeviceやDescriptor Handleを渡さない。

将来OpenGLを追加する場合も`ImageFrame`境界を維持し、GL固有型はPreview Adapter内へ閉じ込める。Engine公開Headerへ`GLuint`等を追加しない。

### 9. Haptics内部

1回の再生は`Voice`として保持し、単調増加する`HapticHandle`で識別する。Handle 0は無効である。各FrameでPatternと経過時間から左右Levelを計算し、Master Intensityを掛けてBackendへ送る。

| Pattern | 評価概要 |
| --- | --- |
| Constant | 指定強度を維持 |
| Pulse | 周波数からON/OFF周期を作る |
| RampUp | 0から指定強度へ増加 |
| RampDown | 指定強度から0へ減少 |
| Burst | 開始直後を強くし急減衰 |

複数Voiceは左右Channelごとに合成し、Deviceへ渡す直前に0〜1へClampする。終了VoiceはUpdate末尾でeraseする。Loopは経過時間を周回させる。

`.haptic` ClipはPathをKeyに`unordered_map`へCacheする。通常のPlay停止ではCacheを消さないため、Asset変更を即時反映したい場合は`ClearClipCache`が必要である。Audio ReactiveはAudio解析結果から強度を作り、Physics ReactiveはImpulse/Maximum Impulseを0〜1へ正規化する。

Device未接続時はGameplayを停止せず無効Handleまたは失敗値を返す。Haptics成功をゲーム進行条件に使わない。

### 10. Online内部

#### Requestの流れ

```text
Script / OnlineService高水準API
 -> Game ID / Player ID / EnvironmentをJSONへ付加
 -> WinHttpOnlineBackend::Send
 -> Worker Request Queue
 -> HTTPS
 -> Worker Response Queue
 -> OnlineService::Update
 -> Cache更新 / Main Thread Callback / Debug情報
```

`OnlineRequestHandle`はProcess内の対応付けにだけ使う。永続IDやServer Request IDではない。

#### 再送Queue

取得系GETは古い結果を後から取得しても意味が変わるためQueueへ残さない。Score送信、Player Data保存、Cloud Save Upload等の`isQueueable`な送信だけを対象にする。

- 通信自体が届かずStatus Codeが0以下の場合にQueueへ戻す。
- HTTP 4xx/5xxはServerへ到達しているため自動再送Queueへ積まない。
- 8秒ごとに1件ずつ再送する。
- 8回を超えたRequestは警告して破棄する。
- Queue上限到達時は最古Requestを破棄する。
- `SaveData/OnlinePendingQueue.cg2`へ終了時または変更確定時に保存する。
- Method、Endpoint、Retry Count、Bodyを区切り文字Escape付きTextで保存する。

Pending QueueにはRequest Bodyが平文で残る。Password、Token、個人情報、秘密鍵をBodyへ入れてはならない。Queue Fileは暗号化Storeではない。

#### MemoryとBack pressure

Request Queue、Response Queue、In-flight Map、Pending QueueはRequest/Response Bodyを`std::string`として保持する。大きなCloud Saveを同時に多数送ると、その件数分のMemoryを消費する。`maximumPendingRequests`は通信回数だけでなくMemory上限としても設定する。

Debug Windowへ保持するRequest/Response本文は先頭512文字へ切り詰める。実Body自体は通信完了またはQueue消去まで保持される。

参照Workerは64 KiBを超えるCloud SaveをR2へ置ける。一方、Version 14の`Online::GetCloudSave()`はDLL境界に65,536 Byteの固定Bufferを渡すため、これを超える本文は高水準Wrapperで完全取得できない。大型R2 ObjectをScriptへ公開するには、必要長照会またはChunk読込をABI末尾へ追加する必要がある。現状のゲームScriptではCloud Save本文を65,535 Byte以下に制限する。

#### Security境界

Client Keyは公開鍵であり、配布Gameから秘密にできない。重要な認可はCloudflare Worker側で行う。`ADMIN_KEY`等の秘密は`wrangler secret`へ登録し、Project Settings、Scene、C++ Script、Gitへ置かない。

Server側でGame ID、Player ID、Score範囲、Rate Limit、Save Sizeを再検証する。クライアントのInspector値やScript値を信頼しない。DevelopmentとProductionはURLだけでなくD1/KV/R2 Resourceも分ける。

### 11. Script DLL境界

Version 14でSpeech 5、Vision 13、Haptics 12、Online 14の計44 Entryを`EditorScriptRuntimeApi`末尾へ追加した。既存Entry位置は維持するが、生成Moduleは`apiVersion == kEditorScriptApiVersion`を要求する。このためVersion 13以前のDLLは現行Headerで再Buildする。

低水準Entryは次を検査する。

- Runtime Managerが存在するか。
- GameObject IDと対象Componentが有効か。
- 出力Pointerと文字列容量が有効か。
- Handleが0でないか、現在も存在するか。
- Online機能が有効か。

ゲーム側は原則として`Speech`、`Vision`、`Haptic`、`HapticVoice`、`Online` Wrapperを使う。Backend Pointer、Frame Buffer Pointer、Response CallbackをDLL境界越しに保持しない。

### 12. Scene保存と参照Remap

| Component | Extension行 |
| --- | --- |
| SpeechRecognizer | `SpeechRecognizerExtension` + `SpeechRecognizerWhisperTimingExtension` |
| CameraInput | `CameraInputDeviceExtension` |
| ImageRecognizer | `ImageRecognizerExtension` |
| HapticSource追加値 | `HapticSourceExtension` |

旧PR #12の8〜22列`CameraInputExtension`はCameraInput Device設定として互換読込し、23列以上は従来Camera制御設定として読む。新規保存は必ず`CameraInputDeviceExtension`を使う。

`visionCameraGameObjectId`はPrefab Instantiate、Duplicate、Scene Merge、Additive LoadでRemapする。参照先が複製範囲外で`clearIfNotFound`の場合は`-1`へ戻し、元Sceneの整数IDを誤参照しない。

Runtime状態、認識結果、Haptic Handle、HTTP Handle、OS Handle、Thread、PointerはSceneへ保存しない。

### 13. Input ActionとScript Action

Speech KeywordとAction名は同じIndexで対応する。VisionはLabel、頭部角度、動き、色の条件を評価し、FalseからTrueへ変わったEdgeでActionを発火する。位置は`ActionName + "Position"`へ正規化Vector2として流す。

Script ActionはMain ThreadのQueueへ積み、認識Callbackの途中でScript Instanceを直接再入させない。Action名が空、対象Scriptがない、Bindされていない場合は何も呼ばない。

### 14. 性能とMemoryの見積り

| 要因 | 増え方 | 主な調整 |
| --- | --- | --- |
| Camera Frame | Camera数 × 解像度 × 4 Byte × 保持Copy数 | Camera共有、解像度、FPS上限 |
| ONNX Session | Recognizer/Modelごと | Recognizer共有、不要時停止、推論Interval |
| Vision結果 | 検出件数、Label文字列数 | Confidence、検出上限、Interval |
| Speech | Session/Keyword数 | Keyword整理、不要Session停止 |
| Haptics | 同時Voice数、Clip Cache数 | Voice停止、Cache Clear |
| Online | Queue件数 × Body Size | Pending上限、Save分割、同時要求抑制 |
| Debug Preview | CameraごとのCPU Sampleと3,072 Block分のImGui描画 | 製品時OFF、必要時だけWindow表示 |

FrameごとのHeap確保を増やさないため、将来最適化する場合はCamera Buffer Pool、固定容量結果Buffer、Online Queueの`deque`化、Previewの低解像度CPU Buffer再利用を検討する。ただし公開型や保存形式を変えずに行う。

### 15. 失敗時の確認順

1. ComponentとGameObjectがActiveか。
2. Project Settingsで機能が有効か。
3. Debug Windowの共通状態がUnavailable/Ready/Running/Errorのどれか。
4. `lastError.code`とMessage。
5. Device名、Backend名、Camera解像度、取得FPS、推論ms。
6. Input Action Map/Action名、Script Action名。
7. OnlineならEnvironment、Base URL、Game ID、HTTP Status、Pending件数。
8. Worker Thread停止やDevice Close待ちが発生していないか。
9. Memory増加がFrame Buffer、Model Session、Request Bodyのどれか。

### 16. Backend追加チェックリスト

- Interfaceだけを実装し、SDK型を公開Headerへ出していない。
- Initialize/Shutdownを複数回呼んでも破綻しない。
- 失敗時に`Unavailable`または`Error`と理由を返す。
- Worker CallbackからEditorScene、ImGui、Scriptを直接呼ばない。
- Stop Flag、join、Handle解放の順が定義されている。
- Buffer容量、最大Queue数、文字列長を検査する。
- Device抜去、通信断、Model不一致をCrashにしない。
- Component、Default、Inspector、Save/Load、Remap、Runtime、Script API、Debug UI、文書を同時に更新する。
- DirectX 12/OpenGL等のGraphics固有処理はAdapterへ閉じ込める。
- Build成功と実Device/実通信の確認結果を分けて記録する。

### 17. 関連文書

- [user-guide.md](user-guide.md)
- [component-reference.md](component-reference.md)
- [script-api-reference.md](script-api-reference.md)

## 設計判断と技術選択

更新基準: 2026-09-29

この章は主要システムごとに「方式 / 処理 / 採用理由 / 他候補 / 不採用理由 / 弱点 / 改善候補」を1組にまとめる。
他章が「何をどう実装しているか」を述べるのに対し、この章は**その方式を選んだ判断**と**現在の限界**を残す。

記載規則:

- 新機能の追加、既存方式・処理フロー・実行順・CPU/GPUの役割・所有関係・性能特性・制約・弱点の変更時は、実装と同じ変更内で本書と`ReadMe.md`を更新する。
- 方式・処理・弱点は**ソースで確認した事実**のみを書き、確認箇所を `file:line` で示す。
- 「採用理由」は実装から一意に決まらない。コードから読み取れる制約に基づく推定は**（推定）**と明記する。
  設計者本人の判断と食い違う場合は、この章を正として書き換える。
- 数値（Cascade数、Atlasタイル数など）は定数の実値を書く。変更したらこの章も更新する。

---

### R-1. GameObject / Component

**方式**: データ集約型。Componentは多態クラスではなく、**全種類の Field を1つの struct が持ち、`type` enum で解釈を切り替える**。

**処理**
`EditorGameObject` が `std::vector<EditorComponent> components` を持つ（`EditorScene.h:2605`）。
`EditorComponent` は 1,820行・トップレベル Field 1,596個の単一 struct（`EditorScene.h:773-2593`）。
`EditorComponentType` は 289 種類。`EditorScene::CreateComponent(type)` が全 Field へ既定値を入れて返す（`EditorScene.cpp:8091`、1,895行）。
Update / Draw は Component 側の仮想関数ではなく、各 Manager が `components` を走査して自分の担当 `type` だけを処理する。

**採用理由（推定）**

- Component ごとのクラス階層と仮想関数テーブルを作らずに、Inspector・Serialize・Undo・共同編集・Script API の5系統へ**同じ1つのデータ形で**流せる。
- Field 追加が「struct へ1行 + `CreateComponent` へ1行 + Inspector へ1行」で済み、新しいクラス・ファクトリ・登録処理が不要。
- Component が POD 中心なので、そのまま Text へ書き出せて差分比較もできる（共同編集の Property 単位 Revision がこれに依存している）。

**他候補**

1. 多態 Component（`class Component` 基底 + `virtual Update()`）— Unity / 一般的な ECS 以前の設計。
2. 本来の ECS（Component 型ごとに密な配列、Entity は ID のみ）。

**不採用理由（推定）**

- 1 は Inspector 表示・Serialize・Undo・ネットワーク差分を Component 型ごとに実装する必要があり、289種類では実装量が現実的でない。
- 2 はデータ局所性で有利だが、Editor が要求する「任意 Component の任意 Field を実行中に編集して即反映」に対し、型ごとの Storage と型消去の仕組みを自作する必要がある。

**弱点（実測）**

- `EditorComponent` は**どの Component 型でも全 Field 分のメモリを占める**。Transform だけの GameObject も 1,596 Field 分を持つ。
- 既定値が宣言から約2,000行離れた別ファイルにある。Field 追加時に `CreateComponent` 側を忘れても**コンパイルは通り、静かに 0 / false になる**（現状は取りこぼし0件を確認済み。`EditorComponent` の全 POD Field に既定値が入っている）。
- `type` に応じた Field の有効・無効が型で表現されないので、無関係な Field を読み書きしてもコンパイラが止めない。
- Component 間依存（例: Collider が RigidBody を要求）は型ではなく Manager 側の実行時チェックで表現している。

**改善候補**

- 既定値を struct の Member 初期化子へ移す。宣言と既定値が同じ行に来て、`CreateComponent` は数行になる。ただし Scene 既定値が変わらないことの実機確認が必要。
- Field 群を `type` ごとのサブ struct（`std::variant` か個別 struct + 共通ヘッダ）へ分ける。メモリと型安全性は改善するが、Serialize と共同編集の Property 経路を全面的に書き換えることになる。

---

### R-2. Scene管理 と Serialization

**方式**: UTF-8 Text の**行単位・列位置依存フォーマット**。GameObject / Component は表示名ではなく UUID で識別する。

**処理**
Scene は `EditorScene` が `std::vector<EditorGameObject>` として保持。ID → 配列 index の hash 索引 `gameObjectIndexById_` を併用し、索引サイズと実配列サイズがずれたときだけ再構築する（`EditorScene.cpp:9987`）。
保存は `EditorScene::SaveScene`（2,603行）、読込は `LoadScene`（3,721行）。
行の要素数で版を判定する（例: `if (elements.size() >= 13u)`）。新 Field は**既存の列位置を変えず末尾へ追加**するか、`*Extension` 行として別行で追記する。

**採用理由（推定）**

- 人が diff を読める。共同制作で衝突箇所を目で確認でき、Git の行単位マージがそのまま効く。
- Binary と違い、Engine の版が違う相手の Scene を開いても壊れた場所を特定できる。

**他候補**: JSON / YAML、Binary、独自バイナリ + 版番号ヘッダ。

**不採用理由（推定）**

- JSON は 1,596 Field × Component 数のキー文字列でサイズが膨らみ、行単位マージが効かない（1つのオブジェクトが1行に潰れるか、階層が深くなる）。
- Binary は diff とマージが不可能で、共同制作の Property 単位差分と両立しない。

**安定性のためにやっていること（実装確認済み）**

- 保存は一時ファイルへ書いて rename する。失敗時に既存 Scene を壊さない。
- **未知の行を保持する**。新版で追加された行を旧版で開いて再保存しても消えない。
- Play 中の Scene 保存 / 読込をガードする。
- Undo スタックに上限を設ける。

**弱点（実測）**

- 列位置が意味を持つため、**列の挿入・並べ替えができない**。フォーマット変更のコストが極端に高い。
- 版判定が `elements.size()` の閾値比較なので、版が増えるほど分岐が増える。`LoadScene` が 3,721行ある主因。
- スキーマが型として存在せず、保存側（`SaveScene`）と読込側（`LoadScene`）の列順が**人手で一致させる約束**になっている。片方だけ直すとズレる。

**改善候補**: 列位置依存を `key=value` 行へ移す（未知キー保持は既にあるので拡張しやすい）。ただし既存 Scene の移行が必要。

---

### R-3. メインループ

**方式**: 固定順の単一スレッドループ。`Update` 系を全部終えてから `Draw` 系へ入る。

**処理**（`main.cpp:195` → `GameScene::Update` / `GameScene::Draw`）

```text
Update: Platform(OSメッセージ) -> 終了要求なら即 return
        -> FrameInput -> SceneLifecycle(Play中のPhysics/Script/同期)
        -> ImGui新フレーム開始
        -> Animation / GameplayTools / Diagnostics / ExternalFeature / PVShoot / TeamCollaboration
Draw:   終了要求なら即 return
        -> Platform(描画フラグを下げる) -> SceneLifecycle(Runtime描画)
        -> MainMenu -> PVShoot -> (PV撮影中以外) Docking / SceneView
        -> GameView -> (PV撮影中以外) 各Window
        -> ImGui確定 -> Renderer.Draw -> ImGui別Window -> FrameRate制限
```

**なぜこの順番か（コードから読み取れる根拠）**

- OS メッセージを最初に処理するのは、`WM_QUIT` とリサイズを他の処理より前に確定させるため。終了要求フレームでは以降を全部飛ばし、**破棄済みリソースを触らせない**。
- ImGui のフレーム開始を Update 側に置き、各 Window の `Draw` で ImGui の Item 状態（クリック・ドラッグ・矩形）を使うため、Window Manager 側に `Update` が無い（10クラスは `Update` を持たない）。
- Physics は Play 中のみ `SceneLifecycle` 内で固定時間刻み `1/60秒` で回す（`EditorSceneLifecycleManager.cpp:83`）。描画フレームレートと切り離す。
- Renderer の `Draw` を ImGui 確定の後に置き、ImGui の DrawData を同じ CommandList へ積んで1回の Present で出す。

**弱点（実測）**

- 完全に単一スレッド。Asset ロード・Shader コンパイル・Physics が全部メインスレッドを止める。
- `TeamCollaborationManager::Update` へ渡す delta time が `1.0f / 60.0f` 固定（`GameScene.cpp`）。実フレームレートが 60fps でないとき共同編集側のタイマーがずれる。
- Physics の固定刻みは `1/60` 固定で、1フレームに複数ステップ進める catch-up を行っていない。重い描画でフレームが落ちると物理の進みが実時間より遅れる。

---

### R-4. Renderer

**方式**: **Forward 描画を主軸**にし、後段が必要とする情報だけを別 Pass で GBuffer へ書く**部分 Deferred**。完全な Deferred Shading ではない。

**根拠**: `EditorGBufferManager` の宣言コメントが「後段の AO / Reflection が参照する GBuffer を管理する」（`EditorGBufferManager.h:13`）。Lighting は GBuffer から解かず、Forward の Opaque Pass 内で行う。

**処理順**（`EditorRenderManager::Draw`、5,190行）

```text
Shadow / Reflection準備 -> Opaque / Alpha Cutout -> GBuffer(材質値と法線)
-> Planar Reflection / Reflection Mask -> Weighted OIT / Refractive Surface
-> Depth Pyramid / Normal再構成 -> Frustum + Hi-Z Culling
-> SSAO / SSGI / SSR / Volumetric -> Temporal Resolve
-> Underwater -> Bloom -> Glare -> DOF -> Motion Blur
-> Auto Exposure -> Final Composite -> Filter -> Sharpen
-> AA(None / FXAA / SMAA / Temporal) -> ImGui -> Present
```

**採用理由（推定）**

- Forward は材質ごとの Lighting Model 切り替え（`lightingMode` 0=なし / 1=Lambert / 2=Half Lambert / 3=PBR、`EditorScene.h:797`）が素直に書ける。Deferred だと GBuffer に Lighting Model の分岐を載せる必要がある。
- 半透明（Weighted OIT）と屈折面を同じパイプラインで扱える。Deferred は半透明を別経路にせざるを得ない。
- GBuffer を「AO / Reflection の入力」に限定すれば、フル GBuffer より帯域が小さい。

**他候補**: 完全 Deferred、Forward+ / Tiled Forward、Visibility Buffer。

**不採用理由（推定）**

- 完全 Deferred は上記の材質別 Lighting Model と半透明の要求に合わない。
- Forward+ のタイル別ライトリストは、現在のライト規模（後述の通り Shadow 付きは Sun 1 + Point 3 に制限）では利点が出る前に実装コストが勝つ。

**Draw Call 削減（実装確認済み）**

- **自動バッチング**: 同一条件の SceneObject をまとめ、Instance Buffer 経由で `DrawIndexedInstanced` を1回発行する。`batch.size() < 2` のときはバッチ化せず通常描画（`EditorRenderManager.cpp:3074`）。容量上限 `kEditorBatchInstanceCapacity`(65,536 instance) を超える分もバッチ化しない。
- **GPU Culling + ExecuteIndirect**: 後述 R-9。

**弱点（実測）**

- `Draw()` が 5,190行の単一関数。内部に関数内 `static` 15個（フレーム跨ぎの隠れ状態）を持つ。最大の塊は「Scene rendering to HDR RT」1,353行。
- Render Queue が明示的なデータ構造として存在しない。描画順は関数内の記述順そのもの。Pass の追加・並べ替えが差分として読みにくい。
- Pass 間の RenderTarget 受け渡しが局所変数（`hdrPostSourceSrvHandle` / `hdrPostSourceResource`）の書き換えで表現されている。読む側が「今どちらの RT が最新か」を追跡する必要がある。

**改善済み（2026-09-29）**: 後段ポストプロセス9 Pass を関数へ分離し、`PostProcessSource`（SRV + Resource）型で受け渡しを明示した。5,441行 → 5,190行。

---

### R-5. ライティング

**方式**: Forward 内で解く。材質ごとに Lighting Model を選択（0=なし / 1=Lambert / 2=Half Lambert / 3=PBR、既定 3）。

**処理**: Directional / Point / Spot / Area を扱う。Spot は Point と同じ位置ベース経路で、内外角を `spotCosInner` / `spotCosOuter`（cos 値）として渡す（`EditorRenderManager.cpp:556-557`）。既定は内角20°/外角30°。

**採用理由（推定）**

- PBR を既定にしつつ Lambert / Half Lambert を残したのは、学習用途と表現用途（トゥーン寄りの見た目）を同じ Engine で出せるようにするため。
- cos 値を CPU 側で計算して渡すのは、Pixel Shader 内で `cos` を毎ピクセル評価しないため。

**他候補**: Blinn-Phong 固定、PBR 固定。

**不採用理由（推定）**: Blinn-Phong 固定では金属・粗さの表現ができない。PBR 固定では非写実表現の選択肢を失う。

**弱点（実測）**

- Shadow を落とせるライト数が Atlas のタイル予算で決まる（次項）。Shadow なしライトの上限は別途確認が必要。
- ライトの選別（画面への寄与が大きい上位N灯だけ使う等）を行っているかは未確認。ライト数増加時の挙動は要検証。

---

### R-6. Shadow（Directional / Point / Spot）

**方式**: **単一の 5×5 Shadow Atlas（各タイル 1024×1024、計25タイル）**に全種類の Shadow を詰める。

**タイル予算**（`EditorSharedState.h:750-751` のコメントが明記）

```
Sun cascade 4 + Point Light 最大3灯 × 6面 = 22 タイル / 25 タイル
```

**Directional: CSM 4 Cascade**（`EditorRenderManager.cpp:2235` `kSunCascadeCount = 4u`）

Cascade 分割は**対数分割と均等分割の加重混合**。重み `kCascadeLogarithmicWeight = 0.68f`（`EditorRenderManager.cpp:2283-2296`）。

```
split = 対数分割 × 0.68 + 均等分割 × 0.32
```

**採用理由（推定）**

- 単一 Directional Shadow Map では、広い Scene で近距離の Texel 密度が足りない。距離で分割して近距離に解像度を寄せる。
- 純粋な対数分割は最近接 Cascade が極端に狭くなり Cascade 切り替えが目立つ。均等分割は遠方の密度が落ちる。0.68 の混合はその中間を取る実用手法。

**Point: Cube Shadow Map（6面/灯、最大3灯）**

`kCubeFaceCount = 6u`、`MakePointLightCubeFaceViewProjectionMatrix(...faceIndex)` で面ごとの View Projection を作る（`EditorRenderManager.cpp:833-870`）。

> **注意**: 「Point Light は1方向 Shadow」という理解は現在のコードと一致しない。6面すべてを描いている。
> 制限は面数ではなく**灯数（3灯）**で、これは Atlas のタイル予算から来ている。

**Spot**: Point / Spot / Area 共通の位置ベース経路（「Point/Spot/Area: position-based, look from light toward center」`EditorRenderManager.cpp:810`）。

**他候補**: ライトごとに独立した Shadow Map テクスチャ、Virtual Shadow Map、Shadow Cache。

**不採用理由（推定）**

- ライトごとの独立テクスチャは Descriptor 数と State 遷移が灯数に比例して増える。単一 Atlas なら SRV 1本で済み、Barrier も1回。

**弱点（実測）**

- **Shadow 付き Point Light は3灯まで**。4灯目以降は Shadow が出ない。Atlas を広げるかタイルを小さくするかの選択になる。
- Point Light 1灯で6 Pass 増える。灯数に対する描画回数の増え方が急。
- Cascade 境界は**末尾 12% でブレンドする**（`ShadowSampling.hlsli:167` `lerp(near, far, 0.88f)`）。
  最終 Cascade はブレンドしない。`cascadeBlend <= 0` で早期離脱し、大半のピクセルで 2 回目の 9-tap を省く。
- Cascade 選択は**カメラからの距離**（`length(worldPosition - light.cameraPosition)`）を 3 つの閾値と比較して
  分岐なしで加算する（`ShadowSampling.hlsli:141-146`）。View 深度ではなく距離なので、画面端でも Cascade が一定。
- Filtering は **9-tap PCF**（`SampleSoftShadow9Tap`）。ハードウェア比較サンプラ（`SampleCmp`）は使わず、
  静的サンプラの `ComparisonFunc` は `NEVER`（`EditorPlatformManager.cpp:1957`）。深度比較を自前で行っている。
- Acne 対策は 2 段。
  - Rasterizer: `DepthBias 1200` / `SlopeScaledDepthBias 1.5` / `DepthBiasClamp 0.01`（`EditorPlatformManager.cpp:2589-2591`）。
  - Shader: 受光面の傾きに応じた `worldBias = lerp(0.055f, 0.014f, saturate(normalDotLight))` を
    NDC 単位へ換算し `[0.00002, 0.01]` にクランプする（`ShadowSampling.hlsli:52-57`）。
    真横を向いた面ほどバイアスを大きく、正面を向いた面ほど小さくする。`DepthBiasClamp` と Shader 側クランプの
    両方が Peter Panning（影の浮き）の上限として働く。
- Atlas のタイル境界は **2 テクセル内側**に寄せてサンプルする（`ShadowSampling.hlsli:36-41`）。
  隣のタイルの深度を拾って影が漏れるのを防ぐ。単一 Atlas 方式に固有の対策。
- Shadow の更新頻度はハッシュ比較で抑えている（`submittedShadowStateHash`）が、Cache の粒度はライト単位ではなく全体。

---

### R-7. Physics

**方式**: **Jolt Physics 5.5.0** を採用。固定時間刻み。

**根拠**: `EditorJoltPhysicsManager.cpp` に `JPH::` 参照が 455箇所。並存する `EditorPhysicsManager.cpp`（4,764行）には `JPH::` 参照が 0 で、独自実装（浮力・破壊連携など Jolt の外側の処理）を担う。

**処理**: Play 中のみ `SceneLifecycle` 内で `1/60秒` 固定刻み（`EditorSceneLifecycleManager.cpp:83`）。`physicsSettings.fixedTimeStep` の既定も `1/60`（`EditorScene.cpp:730`）。

**採用理由（推定）**

- Jolt は Broad Phase / Narrow Phase / 拘束解決が完成しており、決定性と並列性の設計が明示されている。自作すると Broad Phase の空間分割と拘束ソルバの安定化に時間を取られる。
- 固定刻みは、フレームレートが変動しても物理挙動が変わらないようにするため（可変刻みだと反発・摩擦・拘束の収束が変わる）。

**他候補**: PhysX（`ThirdParty/PhysX` と `PhysicsSdk` が同梱されている）、自作。

**不採用理由（推定）**: PhysX も同梱されているが、Blast（破壊）だけ PhysX 系の `NvBlast` を使い、剛体は Jolt に寄せている。破壊と剛体で別 SDK を使う構成になっている理由は設計者の確認が必要。

**弱点（実測）**

- 固定刻みの catch-up が無い。重いフレームで物理が実時間から遅れる。
- 剛体 = Jolt、破壊 = NvBlast、浮力など = 自作、と3系統に分かれている。どこがどれを担当するかがファイル名から読み取れない（`EditorPhysicsManager` が Jolt を使っていない）。

---

### R-8. Asset管理

**方式**: ハッシュ基準の Cache + 永続 ID（`AssetRegistry`）。

**処理**: `AssetManager` が `std::unordered_map<std::string, HashCacheEntry> hashCache_` を持ち、ファイル内容のハッシュで再読込の必要を判定する（`AssetManager.h:73-80`）。`AssetRegistry` は Asset に永続 ID を与え、`NotLoaded` 等の状態を持つ（`AssetRegistry.h:18`）。Component 側は `assetPath` と `assetId` を両方保持し、**`assetId` があれば移動・Rename 後もそちらを優先して Path を解決する**。

**採用理由（推定）**

- Path だけで参照すると、Asset を移動・Rename した時点で全 Scene の参照が切れる。永続 ID を正として Path を Fallback にすることで、ファイル整理が Scene を壊さない。
- ハッシュ比較なら、タイムスタンプだけの判定と違い「内容が変わっていないのに再読込する」を避けられる。

**他候補**: Path のみ、タイムスタンプ比較のみ。

**不採用理由（推定）**: 上記のとおり Path のみは移動に弱く、タイムスタンプのみは Git のチェックアウトで全 Asset が再読込になる。

**弱点（実測）**

- **非同期ロードが無い**（`async` / `thread` / `future` の記述が `AssetManager.h` / `AssetRegistry.h` に無い）。大きな Texture / Model のロード中はメインスレッドが止まる。
- Unload / 参照カウントの扱いは未確認。要検証。

---

### R-9. GPU を使った最適化

**方式**: Culling・海面・Particle・Skinning を GPU 側へ出す。

**GPU Culling（Frustum + Hi-Z Occlusion）**

Depth Pyramid から Hi-Z を作り、GPU 上で Frustum と Occlusion を判定する。結果を **CPU へ Readback せず**、次フレームの `SetPredication` から直接参照する。
さらに **`ExecuteIndirect` による GPU 駆動描画を持つ**: `ExecuteIndirectDraw` / `ExecuteIndirectDrawIndexed` が Command Signature（`drawCommandSignature_` / `drawIndexedCommandSignature_`）経由で発行し、失敗時のみ通常 Draw へ戻る（`EditorGpuCullingManager.cpp:321,348`、呼び出しは `EditorRenderManager.cpp:3728-3751`）。

> **注意**: 他章にある「`ExecuteIndirect` による完全GPU駆動描画ではない」という記述は現在のコードと一致しない。

**採用理由（推定）**: CPU へ Readback すると GPU の完了待ちが発生し、1フレーム分のパイプラインが崩れる。Predication と ExecuteIndirect なら GPU 内で判定と発行が閉じる。

**Ocean FFT**: Compute Shader で波を解く。
**採用理由（推定）**: Sin 波の重ね合わせでは、実際の海面のスペクトル（波長ごとのエネルギー分布）を再現できず、波形が周期的に見える。FFT は波数空間でスペクトルを与えて逆変換するので、非周期に見える海面が得られる。計算量が大きく、格子全点を毎フレーム解くため GPU 側でなければ間に合わない。

**GPU Particle**: `EditorGpuParticleManager`。Spawn を Upload Buffer 経由で渡し、更新と描画を GPU 側で回す。

**GPU Skinning**: Bone 行列を StructuredBuffer で頂点 Shader へ渡す（`EditorSceneObject.h:195-196`）。
**さらに前フレームの Bone 行列も保持する**（`previousSkinMatrixResource`、`EditorSceneObject.h:197-198`）。
**採用理由**: Motion Vector を正しく出すため。前フレーム行列が無いと、スキンメッシュの Motion Blur と Temporal AA が破綻する。CPU Skinning では頂点を毎フレーム CPU で変換して Upload することになり、頂点数に比例した転送が発生する。

**弱点（実測）**

- GPU 破片（破壊表現）は GameObject 化していないため、個別に操作・参照できない。
- GPU Particle の Spawn は Upload Buffer 容量 `kMaxParticleCount` で上限が決まる。超過分は捨てる。
- GPU 同期コスト（Fence 待ち）の計測粒度は Pass 単位。どの Dispatch が支配的かは Profiler の GPU イベント名の粒度に依存する。

---

### R-10. Profiler とボトルネック特定

**方式**: CPU 時間・GPU 時間・Draw Call・確保量を**同じ Scope で同時に取る**。

**処理**

- `EditorProfilerManager::Scope` を RAII で置く。入れ子を `callPath`（`親 > 子`）として持ち、呼び出し階層のまま集計する（`EditorProfilerManager.cpp:26-52`）。
- GPU 時間は Timestamp Query Heap へ `BeginGpuEvent` / `EndGpuEvent` を積み、Pass 単位で読む。
- 確保量は**グローバル `operator new` を置き換えて**計測する（`EditorProfilerAllocationTracker.cpp`）。Scope の開始・終了でスナップショットを取り、差分をその Scope の確保量とする。
- Draw Call は `RecordEditorProfilerDrawCall()` を発行箇所に置いて数える。

**採用理由（推定）**

- CPU 時間だけでは「速いが確保が多い」処理を見つけられない。確保量を同じ Scope で取ると、GC の無い C++ でもフレーム内の確保スパイクを特定できる。
- `operator new` の置き換えにしたのは、Engine 内の自前アロケータを通らない確保（`std::string` / `std::vector` / ThirdParty）も含めて数えるため。

**他候補**: 外部プロファイラのみ（PIX / Superluminal）、自前アロケータ経由の計測のみ。

**不採用理由（推定）**: 外部プロファイラは Editor 実行中に利用者が見られない。自前アロケータ経由だけでは標準ライブラリと ThirdParty の確保が漏れる。

**弱点と対策（2026-09-29 に修正）**

- `operator new` 置き換えは**20種の確保関数を全部揃えないと危険**。1つでも欠けるとその形だけ CRT 側の実装が使われ、`malloc` 系と `_aligned_malloc` 系が混ざって Heap 破壊になる。aligned かつ nothrow の4種が欠けていたので補完した（16/20 → 20/20）。
- 計測 OFF 時も Engine 全体の `new` が毎回**別 TU の非 inline 関数**を呼んでいた。判定を Header へ移して inline 展開させ、OFF 時は relaxed load 1回と分岐だけにした。
- Scope が `std::string` を4つ持つ。計測 ON 時は Scope ごとに文字列連結（`親 > 子`）が走る。計測自体の負荷が測定対象へ乗る。

**ボトルネック特定の手順**

1. 診断 Window の Profiler タブで `callPath` を降順に見て、CPU 時間の支配項を特定する。
2. 同じ Scope の確保量を見る。時間が大きく確保も大きいなら、確保の除去（バッファ再利用）が先。
3. GPU 時間が支配的なら Pass 名で切り分ける。Draw Call 数と Dispatch 数を併読して、発行回数律速か帯域律速かを分ける。
4. 「描画バッファ」タブで各 RenderTarget を目視し、期待しない Pass が動いていないかを確認する。

---

### R-11. 共同編集

**方式**: **Revision 番号ベースの Property 単位差分同期 + Object Lock の併用**。
ファイル丸ごとの同期は行わない。

> **この節は初版で Lock 機構とプロトコルの実体を落としていた。完全版は R-39 を参照する。**
> R-39 に Transport の抽象化（Tailscale の扱い）、JSON over TCP のメッセージ種別、
> Heartbeat による切断検出（5秒/20秒/猶予5秒の3段）、Lock 解放の猶予、
> ProjectId の Path Traversal 対策を記載した。

**処理**

- 変更は `baseRevision` / `revision` を持つ単位で表現する（`EditorTeamCollaborationManager.h:54-55`）。
- クライアントは `currentRevision_` / `lastSyncedRevision_` / `serverRevision_` の3つを持ち、どこまで送信済み・どこまでサーバが受理済みかを区別する（同 `280-282`）。
- Property ごとに最終 Revision を保持する `lastPropertyRevision_`（同 `218`）。これが競合検出の粒度を決める。
- `LoadChangeLog()` で ChangeLog を永続化し、再接続時に差分から復帰する（同 `324`）。
- 通信は TCP（`TcpCollaborationTransport.cpp`）。
- Play 中に届いた他ユーザーの変更は、通信と Revision 確定は継続しつつ適用を保留する（同 `236` のコメント）。

**採用理由（推定）**

- ファイル丸ごと同期は、2人が別の GameObject を編集しただけでも衝突する。Property 単位なら同じ Scene の別箇所を同時に編集できる。
- Revision を単調増加の整数にすると、順序と「どこまで同期したか」が1つの数で表せる。ベクタークロックや CRDT より実装が小さい。
- UUID で識別するので、配列位置や表示名が変わっても変更の宛先が壊れない（R-1 / R-2 の UUID 方針に依存）。

**他候補**: ファイル同期（Git / rsync 的）、OT（Operational Transformation）、CRDT。

**不採用理由（推定）**

- OT / CRDT はテキスト編集の同時性には強いが、Scene の構造化データに対しては実装量が大きく、Property 単位 Revision で要求を満たせる。
- ファイル同期は上記のとおり衝突粒度が粗すぎる。

**TCP を選んだ理由（推定）**

- Scene の変更は**取りこぼしが許されない**。1つの Property 変更が落ちると全員の Scene が食い違う。順序保証と再送を自前で作らずに済む TCP が適する。
- UDP が有利なのは「最新値だけ届けばよい」用途（対戦ゲームの位置同期など）。編集操作は累積なので当てはまらない。

**弱点（実測）**

- Play 中の他ユーザー変更を保留するため、Play を長く続けると保留分が積む。
- `EditorTeamCollaborationManager::Draw` が 1,362行（filesystem 6回・map find 21回・sort 2回を含む）。Window 可視時のみ実行される点は確認済みだが、開いている間は毎フレームこのコストを払う。
- `Update` へ渡す delta time が `1/60` 固定（R-3 の弱点と同じ)。

---

### R-12. この章の未検証項目

**この一覧は R-27 で更新した。** 8項目は検証して本文へ反映済み。残りは R-27 の後半を参照する。

以下は初版時点の一覧（履歴として残す）。

| 項目 | 理由 |
| --- | --- |
| Shadow の Bias / Normal Offset / Slope Scaled Bias の具体値 | Acne と Peter Panning 対策を必ず聞かれる |
| Cascade 境界の Blend をしているか | CSM の定番の弱点 |
| PCF / Filtering の種類とサンプル数 | 同上 |
| SSAO と GTAO のどちらか、Sample数・半径・Blur・Temporal | 方式名を間違えると印象が悪い |
| SSR の Step 数、画面外の Fallback（Reflection Probe へ落とすか） | 「画面外情報が無い問題をどうしたか」は必ず聞かれる |
| GI の方式（Probe / Baked / Lightmap / SSGI の実際の組み合わせ）と Probe 補間 | 章 `Lighting・GI` にあるが方式選択の記述が無い |
| Shadow なしライトの上限と選別方式 | ライト数増加時の挙動 |
| Frustum Culling の Bounding 形状（Box か Sphere か）と除外段階 | GPU Occlusion との役割分担 |
| Asset の Unload と参照カウント | リーク対策 |
| Shader Variant と Compile 失敗時の扱い | 起動時に全 Shader を検査して失敗 Path をまとめる仕組みはある |
| Undo / Redo の方式（Command Pattern か全体 Snapshot か）とメモリ使用量 | 上限化済みという記録はある |
| Multithreading | 現状は実質単一スレッド。「なぜ並列化しないか」を答える必要がある |
| SunPortal が何を解決する機能か | 独自機能なので必ず深掘りされる |

---

## 設計判断と技術選択 — 基礎編

更新基準: 2026-09-29

R章が「どのシステムをどう作ったか」を扱うのに対し、F章は**その土台になっている約束事**を扱う。
座標系・行列規約・深度・色空間・DirectX 12 の使い方など、間違えて説明すると全体の信頼を失う項目を、
実際の実装値で固定する。

---

### F-1. 座標系と行列規約

**規約**: 左手系。**行ベクトル × 行優先行列**（`v * M`）。平行移動は行3（`matrix[3][*]`）。

**根拠**（`Source/Engine/Core/Vector&Matrix.cpp:6-14`）

```cpp
result.x = vector.x * matrix.matrix[0][0] + vector.y * matrix.matrix[1][0]
         + vector.z * matrix.matrix[2][0] + matrix.matrix[3][0];
```

ベクトルの成分が**行列の行**と組み合わさり、平行移動が `matrix[3][*]` から来ている。
これは行ベクトル規約（`v * M`）であり、Direct3D / DirectXMath の並びと同じ。

したがって合成の順序は**適用したい順に左から掛ける**。

```
World = Scale * Rotate * Translate
WVP   = World * View * Projection
```

OpenGL / GLM の列ベクトル規約（`M * v`、`P * V * M`）とは**掛ける順が逆**になる。

**射影行列**（`Source/Engine/Core/Matrix.cpp:177-188`）

```cpp
float f = 1.0f / std::tan(fovY / 2.0f);
result.matrix[0][0] = f / aspect;
result.matrix[1][1] = f;
result.matrix[2][2] = farZ / (farZ - nearZ);
result.matrix[2][3] = 1.0f;                       // clip.w = view.z
result.matrix[3][2] = (-nearZ * farZ) / (farZ - nearZ);
```

- `matrix[2][3] = 1.0f` なので `clip.w = view.z`。**view.z が正のとき w が正** → **左手系**（+Z が画面奥）。
- 深度は `z = near` で 0、`z = far` で 1 になる。**深度範囲 [0,1]、標準 Z**（Reverse-Z ではない）。

**採用理由**

- Direct3D の既定と一致させると、HLSL 側で転置や符号反転を入れずに済む。
- 深度 [0,1] は D3D の既定クリップ空間。OpenGL の [-1,1] 前提の式を持ち込むと near 付近が壊れる。

**弱点**

- Reverse-Z を使っていないので、遠方の深度精度が浮動小数の分布と噛み合わない。
  near を小さく、far を大きく取ると遠方で Z ファイトが出やすい。
- Reverse-Z へ移すには、射影行列・深度クリア値（1.0 → 0.0）・比較関数（`LESS_EQUAL` → `GREATER_EQUAL`）・
  深度を線形化している全シェーダを同時に直す必要がある。

---

### F-2. Depth Buffer と Z ファイト

**主 Depth**: `DXGI_FORMAT_D24_UNORM_S8_UINT`。Resource 自体は `DXGI_FORMAT_R24G8_TYPELESS` で作る
（`EditorPlatformManager.cpp:954,960,976`）。

**なぜ TYPELESS か**: 同じリソースを **DSV（深度書き込み）としても SRV（シェーダ読み取り）としても**使うため。
TYPELESS で確保し、DSV は `D24_UNORM_S8_UINT`、SRV は `R24_UNORM_X8_TYPELESS` で張る。
SSAO・SSR・SSGI・DOF・Motion Blur・Volumetric が全部この深度を読むので、読み取り可能であることが前提になっている。

**比較関数**: `D3D12_COMPARISON_FUNC_LESS_EQUAL`（`EditorPlatformManager.cpp:2207`）。クリア値 1.0。
水面だけ `LESS`（`:2427`）。同じ深度値の再描画を弾きたいため。

**Shadow 用 Depth**: `DXGI_FORMAT_D32_FLOAT`（`:1047,1075`）。

**なぜ Shadow だけ 32bit float か**

- Shadow は深度値そのものを比較に使うので、24bit 固定小数より精度が要る。
- Shadow には Stencil が不要なので、8bit を Stencil に割く意味がない。
- Cascade ごとに深度範囲が違うため、固定小数の一様分布より float の方が扱いやすい。

**Z ファイト対策（実装確認済み）**

- Shadow 描画 PSO に Rasterizer バイアスを入れる（`EditorPlatformManager.cpp:2589-2591`）。

```cpp
shadowPipelineStateDesc.RasterizerState.DepthBias            = 1200;
shadowPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
shadowPipelineStateDesc.RasterizerState.DepthBiasClamp       = 0.01f;
```

`DepthBias` は固定オフセット、`SlopeScaledDepthBias` は面の傾きに比例したオフセット。
傾いた面ほど 1 テクセル内の深度差が大きいので、傾き比例分がないと斜面で Acne が残る。
`DepthBiasClamp` は、ほぼ真横を向いた面で傾き比例分が発散して Peter Panning（影の浮き）になるのを止める上限。

---

### F-3. 色空間と HDR

**テクスチャ読み込み**: 既定で sRGB として読む（`EditorSharedState.h:171,203`）。

```cpp
bool forceSrgb = true,
...
forceSrgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_FORCE_LINEAR,
```

**なぜ sRGB を既定にするか**

- PNG / JPEG のカラーテクスチャは sRGB で保存されている。これをリニアとして扱うと、
  ライティングの掛け算が間違った空間で行われ、暗部が持ち上がりすぎる。
- sRGB フォーマットで SRV を張ると、**サンプリング時にハードウェアがリニアへ変換する**。
  シェーダ内で `pow(color, 2.2)` を書く必要がなく、バイリニア補間もリニア空間で正しく行われる。
- Normal Map / Roughness / Metallic は色ではなく数値なので、これらは `forceSrgb = false` で読む必要がある
  （Import Settings 側で切り替える）。

**中間バッファ**: HDR 系の RenderTarget は `DXGI_FORMAT_R16G16B16A16_FLOAT`。
Bloom / Glare / DOF / Motion Blur / SSR / SSGI がこの形式で繋がる。

**なぜ 16bit float か**

- 1.0 を超える輝度を保持しないと Bloom の明部抽出ができない。8bit UNORM では 1.0 で飽和する。
- 32bit float は帯域が倍になる。ポストプロセスは何度も全画面を読み書きするので、帯域が支配的になる。

**AO バッファ**: `DXGI_FORMAT_R8_UNORM`（1チャンネル 8bit）。遮蔽率は 0〜1 のスカラーなので 1 チャンネルで足りる。

**流れ**

```
sRGB Texture --(HWでリニア化)--> リニア空間でライティング
  --> HDR R16G16B16A16_FLOAT --> ポストプロセス群
  --> Auto Exposure --> Tone Mapping --> Final Composite
  --> LDR Back Buffer (R8G8B8A8_UNORM) --> Present
```

**なぜトーンマップを最後に置くか**: Bloom・DOF・Motion Blur は**リニアな輝度**に対して行わないと
物理的に正しくならない。トーンマップ後の圧縮された値でブラーをかけると、明部の広がり方が変わる。

---

### F-4. DirectX 12 の使い方（このエンジンの実際の構成）

**Command 系**: `CommandQueue` 1本 + `CommandAllocator` + `CommandList` を 1 組。
毎フレーム `Reset` して積み直す（失敗は `EditorRenderManager.cpp:2283,2290` でログに残す）。

**なぜ 1 本か**: 描画が単一スレッドなので、複数の CommandList を並列に積む相手がいない。
並列化するなら、まず CommandList をスレッドごとに分けるのが入口になる。

**同期**: `ID3D12Fence` + `HANDLE fenceEvent` + `WaitForSingleObject`。
`waitForGpu` ラムダが `Signal` → 未完了なら `SetEventOnCompletion` → 待機、の形。

**Descriptor Heap**: RTV / DSV / SRV(CBV_SRV_UAV) の3本。SRV Heap は Shader Visible。
インデックスを定数で固定して割り当てる（例: `kRuntimeDepthSrvDescriptorIndex`）。

**なぜインデックス固定か**: 動的なアロケータを持たない代わりに、どのスロットが何かが定数で読める。
反面、**スロットが定数で予約されているので数を増やすと定数を全部見直す必要がある**。

**Root Signature**: Descriptor Table を並べる方式。
IBL は t3=irradiance / t4=prefilter / t5=environment cube / t6=BRDF LUT を
別々の Root Parameter（7〜10番）として持つ（`EditorPlatformManager.cpp:1765-1885`）。

**Resource Barrier**: `D3D12_RESOURCE_BARRIER_TYPE_TRANSITION` を手動で張る。
ポストプロセスの各 Pass が「`PIXEL_SHADER_RESOURCE` → `RENDER_TARGET` → 描画 → `PIXEL_SHADER_RESOURCE`」
を自分で行う（R-4 で切り出した各 Pass 関数の中に見える）。

**なぜ手動か**: D3D12 は自動遷移をしない。Enhanced Barriers / Render Graph を入れれば宣言的にできるが、
Pass の依存関係をデータとして持つ仕組み（Render Graph）が現状ない（R-4 の弱点）。

**Present**: `swapChain->Present`。back buffer は 2枚（`kRuntimeSwapChainBufferCount`）。

---

### F-5. 半透明とブレンド

**方式**: 不透明 → Alpha Cutout → **Weighted Blended OIT** → 屈折面 の順。

**なぜ Weighted Blended OIT か**

- 通常のアルファブレンドは**描画順に依存する**。半透明物体を奥から手前へソートしないと結果が変わる。
- ソートは (1) 物体単位では交差・貫通する形状で破綻し、(2) 毎フレームのソートが CPU コストになる。
- Weighted Blended OIT は**順序に依存しない**近似。重み付き加算で累積し、最後に一度合成する。

**他候補**: 物体単位ソート、Depth Peeling、Per-Pixel Linked List。

**不採用理由（推定）**

- Depth Peeling / Linked List は層数分のパスかメモリが必要で、レイヤ数が読めない。
- 物体単位ソートは上記のとおり破綻ケースがある。

**弱点**: 近似なので、濃い半透明が重なると正しい順序合成と差が出る。
また Alpha Cutout（穴あき）は OIT ではなく不透明側で処理する。こちらは `discard` なので順序非依存。

---

### F-6. Shader の扱い

**コンパイル**: DXC（`IDxcCompiler`）で `ps_6_0` / `cs_6_0` / `vs_6_0` を**起動時に**コンパイルする。

**失敗時**: 最初の失敗で止めず、**最後まで検査して失敗した全 Path を1回の画面表示へまとめる**
（`EditorSharedState.h:126` `g_shaderCompilationFailures`）。
失敗一覧は診断 Window の「DirectX失敗」タブでも見られる。

**なぜ最後まで検査するか**: 1 本目で止めると、直して再起動 → 2 本目で止まる、を繰り返すことになる。
全部出せば一度に直せる。

**重要な実測値**

```
Assets/Shaders 配下の自前 Shader  : 797 本
Source からコードで参照されている  :  79 本
```

**残り約718本はコードから参照されていない。** 存在するが**パイプラインに繋がっていない**。
機能として説明してはいけないものが含まれる。確認した未参照の例:

| Shader | 状態 |
| --- | --- |
| `Compute/TiledLightCulling.CS.hlsl` | 未参照。**Tiled Light Culling は動いていない** |
| `AO/SSAO.PS.hlsl`、`AO/SSAOBlur.PS.hlsl`、`Compute/SSAODenoise.CS.hlsl` | 未参照。AO の実体は GTAO（R-13） |
| `Bake/IntegrateBRDF.CS.hlsl`、`Bake/IrradianceConvolution.CS.hlsl`、`Bake/PrefilterEnvGGX.CS.hlsl`、`Bake/EquirectToCube.PS.hlsl` | 未参照。IBL 自体は動いているが、これらの Compute 経路ではなく C++ 側の手続き生成と Cube 読み込みで用意している |

**弱点**: 「Shader があるから機能がある」と読めない状態になっている。
`Variant` の仕組みは持たず、PSO ごとに個別の Blob を作る。実行中の Shader Reload も持たない
（Asset の Hot Reload は別系統で、Shader は起動時のみ）。

---

### F-7. CPU と GPU の同期

**構成**: フレーム末尾で `waitForGpu`（Fence Signal → 未完了なら Event 待ち）。

**なぜ毎フレーム待つのか**: Upload Buffer や Instance Buffer を**CPU から書き換えて再利用している**ため。
GPU がまだ読んでいる最中に上書きすると壊れる。フレームごとに待てば、二重バッファを用意せずに済む。

**弱点**: CPU と GPU が交互に待つので、**パイプラインが重ならない**。
GPU が動いている間 CPU は遊び、CPU が積んでいる間 GPU は遊ぶ。
改善するなら Upload 系をフレーム数分（2〜3）用意して、Fence 値でリング管理する。

**GPU Culling だけは例外**: 判定結果を CPU へ Readback せず、`SetPredication` と `ExecuteIndirect` で
GPU 内に閉じている（R-9）。Readback すると GPU 完了待ちが 1 回増えるため。

---

### F-8. メモリと所有権

**現状**: 生ポインタ中心 + `ComPtr`（`Microsoft::WRL::ComPtr`）併用。`ComPtr` 使用は 405箇所。

`EditorSharedState` の D3D リソースは生ポインタのグローバル（`inline ID3D12Resource* g_...` が48個）。
解放は `Finalize()` でまとめて行い、再生成する場合は `recreateRenderTarget` ラムダが
**生成前に必ず `Release()` する**（`EditorSharedState.h:1608`）。

**採用理由（推定）**

- グローバルで寿命がプロセス全体と一致するものは、スマートポインタにしても解放順の制御が増えるだけ。
- 一方、関数内で一時的に持つものは `ComPtr` にして早期 return で漏らさないようにしている。

**弱点**

- 所有者が型に現れない。`g_hdrRenderTarget` を誰が解放するかはコードを追わないと分からない。
- リサイズ経路と `Finalize` の2か所で解放しているので、解放漏れ・二重解放の検査が人手になる。
  （2026-09-29 に `Finalize` の無条件 `Release` へ null ガードを追加した）

**検査手段**: ASan ビルド構成がある。`CG2ENGINE_ASAN=1` で `/fsanitize=address` が付き、
出力先が `x64\ReleaseASan\` に分かれる（`Directory.Build.targets`）。
Debug 構成では終了時に `IDXGIDebug1::ReportLiveObjects` で D3D オブジェクトの残存を出す。

---

## 設計判断と技術選択 — 続き（R-13 以降）

---

### R-13. Ambient Occlusion

**方式**: **GTAO**（Ground Truth Ambient Occlusion 系の水平角ベース）。名前に反して SSAO ではない。

> **注意**: C++ 側の変数名は `ssaoPipelineState` / `g_ssaoRenderTargets` だが、
> コンパイルしている Shader は `Assets/Shaders/AO/GTAO.PS.hlsl`（`EditorPlatformManager.cpp:1407-1409`）で、
> PSO 名も `"GTAO"` を渡している（`:2928-2929`）。
> ディスク上の `AO/SSAO.PS.hlsl` は**未参照**。「SSAO を使っている」と説明すると実装と食い違う。

**処理**（`Assets/Shaders/AO/GTAO.PS.hlsl`）

- 入力: 深度（t0）+ 法線（t1）。出力: `DXGI_FORMAT_R8_UNORM`。
- **8方向 × 2ステップ = 16サンプル**（`[unroll]` で展開）。
- 方向はピクセル毎の疑似乱数で回転させる。`frac(sin(dot(pos, float2(12.9898, 78.233))) * 43758.5453) * 2π`。
- 重みを3つ掛ける。
  - `rangeWeight`: 深度差が `max(centerLinearDepth * 0.075, 0.08)` を超えると 0。遠すぎる点を無視する。
  - `normalWeight`: `smoothstep(0.15, 0.92, dot(centerNormal, sampleNormal))`。法線が違いすぎる点を無視する。
  - `horizonWeight`: `bias` を起点に深度差で立ち上がる。自己遮蔽を弾く。

**第2パス**: `Shadow/ContactShadow.PS.hlsl` を `ssaoBlurPipelineState` としてコンパイルしている
（`EditorPlatformManager.cpp:1410-1413`）。

> ファイル名は ContactShadow だが、中身は **AO テクスチャに対する深度考慮バイラテラルフィルタ**。
> 入力は `gAoTexture`（t0）と `gDepthTexture`（t1）で、`spatialSigma` と `depthSharpness` を持つ。
> 最終合成が読むのは第2パスの出力（`ssaoSrvHandlesGPU[1]`）。

**採用理由（推定）**

- 古典的 SSAO（半球内のランダム点サンプリング）はサンプル数を増やさないとノイズが取れない。
  GTAO 系は水平角を求める定式化なので、少ないサンプルでも形が安定する。
- 16サンプルに抑えたのは全画面フルレゾで回すため。方向をピクセル毎に回転させて、
  足りないサンプル数をフィルタで補う設計。
- バイラテラルにしたのは、通常のガウシアンだと深度の段差を越えて AO が滲み、輪郭が甘くなるため。

**他候補**: SSAO、HBAO、レイトレーシング AO。

**不採用理由（推定）**: SSAO はノイズ、レイトレは対応 GPU とコストの問題。

**弱点**

- 16サンプルは少ない。広い半径を指定すると隙間だらけになる。
- 深度と法線しか使わないので、**画面外と遮蔽物の裏は評価できない**（スクリーンスペース手法の原理的限界）。
- Temporal 蓄積を AO 単体では行っていない。カメラを動かすとフィルタ後もちらつきが残りうる。
- 変数名（ssao）とファイル名（ContactShadow）が実体と合っていない。読む人が必ず誤解する。

---

### R-14. SSR（Screen Space Reflection）

**方式**: 5段構成の Compute パイプライン。全部 Compute Shader。

```
Reflection/SSRTrace.CS.hlsl           深度バッファに対する Ray Marching
 -> Reflection/SSRResolve.CS.hlsl     ヒット位置から色を解決
 -> Reflection/SSRTemporalResolve.CS  前フレーム履歴と混ぜる
 -> Reflection/SSRDenoise.CS.hlsl     空間フィルタ
 -> Reflection/SSRComposite.CS.hlsl   HDR へ合成
```

**採用理由（推定）**

- Reflection Probe（事前ベイク）だけでは、**動く物体の映り込み**と**近距離の接地反射**が出ない。
  床に立つキャラクターの足元がその代表。
- Compute にしたのは、Trace が隣接ピクセルと同じ深度テクスチャを何度も読むため、
  スレッドグループ内でのアクセス局所性が効くこと、および UAV へ直接書けること。
- Temporal を挟むのは、Ray Marching のステップ数を抑えるとヒット位置がばらつくため。
  時間方向に蓄積して実効サンプル数を稼ぐ。

**他候補**: Reflection Probe のみ、Planar Reflection のみ、レイトレース反射。

**使い分け（実装確認済み）**: 3つを併用している。

| 手法 | 用途 |
| --- | --- |
| Planar Reflection（`Reflection/PlanarReflection.PS.hlsl`） | 平面（水面・床）。シーンをもう一度描くので正確 |
| SSR | 任意形状の映り込み。画面内に限る |
| Parallax Corrected Cubemap / 環境キューブ | 画面外の Fallback と粗い反射 |

**弱点**

- **画面外の情報が無い**。画面端で反射が切れる。カメラを回すと映り込みが消える。
- 遮蔽物の裏に回った反射は出ない（深度バッファは1層しかない）。
- Temporal を挟むので、速く動く反射にゴーストが出る。
- Trace のステップ数は要確認（まだ数えていない）。ステップ数と最大距離のトレードオフは説明できるようにしておく。

---

### R-15. GI（Global Illumination）

**方式**: **Light Probe + 球面調和（SH）+ Visibility** を主軸に、**SSGI** を画面空間の補完として併用。

**Probe 側**（`Assets/Shaders/GI/`）

| Shader | 役割 |
| --- | --- |
| `ProbeCapture.VS/PS.hlsl` | Probe 位置からシーンを描く |
| `ProbeShProjection.CS.hlsl` | キャプチャ結果を SH 係数へ射影する |
| `ProbeVisibility.CS.hlsl` | Probe とサンプル点の可視性を持つ |

Probe グリッドは `LightProbeGridData`（origin / spacing / countX,Y,Z / normalBias / intensity）。
`EditorLightProbeManager::UpdateGrid` が設定差分を見て、変わったときだけ Probe リソースを作り直す。
GI 無効時も Descriptor は生かしたまま、実体を 1 Probe に落とす（`EditorLightProbeManager.cpp:662-667`）。

**Bake は 1 フレームに数個ずつ**に分散する（`EditorRenderManager.cpp` の Light Probe GI 節、
`lightProbeBakeThrottleFrameIndex`）。影を描いた直後に走らせ、同じフレームの影を間接光へ含める。

**なぜ SH か**

- Probe ごとにキューブマップを持つとメモリが Probe 数 × 6面分になる。
  低周波の間接光（拡散のみ）なら SH 2次（9係数）で十分近似でき、Probe あたり数十バイトで済む。
- SH は線形なので、**Probe 間の補間が係数の線形補間でそのまま成立する**。

**なぜ Visibility を持つのか**

- Probe グリッドは壁を無視して配置される。壁の向こうの Probe をそのまま補間すると、
  **光が壁を透けて漏れる**（light leaking）。可視性を持って重みを落とすことで防ぐ。

**なぜ完全リアルタイム GI ではないか（推定）**

- Path Tracing / Voxel Cone Tracing / DDGI をフルで回すコストに対し、
  このエンジンの想定規模（1〜5人のWindows向け制作）では Probe + SSGI で足りる。
- Bake を分散して「数フレームかけて更新する」形にすれば、編集中も止まらずに結果が追従する。

**SSGI**（`PostProcess/SSGI.PS.hlsl` → `SsgiTemporal.PS.hlsl` → `SsgiUpsample.PS.hlsl`）

**半解像度で解いて Temporal で均し、フル解像度の HDR へ加算する。**
半解像度にする理由がコードのコメントに明記されている（少数サンプルで重い割に低周波な情報なので、
フル解像度で解く必要がない）。

**弱点**

- Probe は静的な配置。動く大きな物体による間接光の遮り替えには追従しない。
- Probe 間隔より細かい間接光の変化は表現できない。
- SSGI は SSR と同じスクリーンスペースの限界（画面外なし）を持つ。
- Bake 分散なので、ライトを大きく動かした直後は間接光が古い。

---

### R-16. Post Process の順序

**順序**（`EditorRenderManager::Draw`、R-4 の一覧の後半）

```
1  Underwater / Caustics
2  多段 Bloom（4回 downsample + 3回 upsample）
3  Glare（Ghosts / Streaks / Fog Glow、mode 2〜7）
4  Depth of Field
5  Motion Blur
6  Auto Exposure（1x1 の履歴テクスチャを更新）
7  Final Composite（Tone Mapping + Bloom 合成 + AO + Vignette + Grain + CA）
8  Filter（3x3 畳み込み、mode 1〜8）
9  Sharpen
10 Antialias（None / FXAA / SMAA / Temporal は排他）
11 Back Buffer へ出力
```

**なぜこの順番か**

- **Bloom / DOF / Motion Blur はトーンマップより前**。これらはリニアな輝度に対して行わないと、
  明部の広がりとボケの重みが物理的におかしくなる（F-3）。
- **Auto Exposure は Final Composite の直前**。露出はトーンマップの入力なので、
  トーンマップ前の HDR 輝度から測る必要がある。1x1 の履歴テクスチャに入れて時間方向に追従させる。
- **AA は最後**。輪郭検出に使う画面は、最終的に出す画面そのものでないと意味がない。
  トーンマップ前にかけると、圧縮後に輪郭が再び立つ。
- **Sharpen は AA の直前**。トーンマップ後にかけ、AA で輪郭が甘くなる分を補う意図がコメントにある。
- **Filter（3x3）は Final Composite の後**。色調変換なので、最終的な見た目に対して掛ける。

**Auto Exposure の実装**: `Compute/HistogramExposure.CS.hlsl` を使う（コードから参照済み）。
時間の刻みは `[1/240, 0.1]` にクランプする（`ExecuteAutoExposurePass`）。

**なぜクランプするか**: フレームが飛んだり停止から復帰した直後に、
delta time が大きくなって露出が一気に飛ぶのを防ぐ。

**弱点**

- Pass 数が多く、各 Pass が全画面を読み書きする。**帯域が支配的**になりやすい。
- Pass の依存関係がデータになっていないので、順序を変えると手で全部追う必要がある（R-4 の弱点）。

---

### R-17. 水面（Ocean）

**方式**: **FFT Ocean**。Compute Shader で波数空間から逆変換する。

**なぜ Sin 波の重ね合わせではないか**

- Sin 波を数本足す（Gerstner 波など）方式は、波長の種類が本数で決まる。
  数本では周期が目に見え、遠景で同じ模様が繰り返して見える。
- 実際の海面は波長ごとにエネルギーが分布している（Phillips スペクトルなど）。
  FFT は**波数空間でスペクトルを与えて逆変換する**ので、1回の変換で全波長を同時に含む海面が得られる。
- 結果として非周期に見え、風速・風向でスペクトルを変えるだけで見た目が変わる。

**なぜ GPU Compute か**

- FFT は格子全点に対して、毎フレーム、2次元分の変換段数を回す。
  CPU では格子サイズを上げた時点で間に合わない。
- 出力（変位・法線・折り重なり）はそのまま頂点シェーダとピクセルシェーダが読むテクスチャなので、
  GPU 上で生成して GPU 上で消費すれば CPU 転送が発生しない。

**反射・屈折**

- **Planar Reflection**（`Reflection/PlanarReflection.PS.hlsl`）を平面反射に使う。シーンをもう一度描く。
- **屈折面**は水面合成後の色と不透明 Depth を参照する専用 Pass（`shouldRenderRefractiveSurface`）。
- SSR との使い分けは R-14 の表のとおり。水面のような平面は Planar が正確なので Planar を優先する。

**水面の Depth 比較だけ `LESS`**（`EditorPlatformManager.cpp:2427`）。同じ深度の再描画を弾く。

**弱点**

- Planar Reflection は**シーンをもう一度描く**。描画コストが実質 2倍になる面がある。
- FFT の格子サイズと更新頻度が負荷を直接決める。`oceanReflectionUpdateFrameIndex` で
  反射の更新を間引いている（毎フレームではない）。
- 波の見た目は格子の範囲でタイルする。範囲外は繰り返しになる。

---

### R-18. 破壊表現

**方式**: **NvBlast**（PhysX 系の破壊ライブラリ）。CPU 破片と GPU 破片を分ける。

**CPU 破片**: GameObject として実体化する。個別に参照・操作・当たり判定できる。
**GPU 破片**: GameObject 化せず、Instance Buffer で描画のみ行う。

**なぜ GPU 破片を GameObject 化しないか（推定）**

- 破片は1回の破壊で数百〜数千出る。それぞれを GameObject にすると、
  Scene の配列・UUID・Inspector・Undo・共同編集の全経路にその数だけ乗る。
- 破片は**生成されて飛んで消えるだけ**で、後から参照する必要がない。
  「撃ちっぱなし」にすれば Instance Buffer への書き込みだけで済む。

**制約（そのまま弱点）**

- GPU 破片は**個別に操作・参照できない**。特定の破片を拾う、当たり判定を取る、といったことができない。
- Physics を全破片へ適用すると、剛体数が一気に増えて Broad Phase が重くなる。
  だから GPU 破片は物理を持たない（または簡易）。
- Blast は `NvBlast.lib` / `NvBlast.dll` に依存する。剛体が Jolt なので**破壊と剛体で別 SDK**になっている（R-7）。

**検証**: `Tests/BlastLowLevelSmoke.cpp` が低レベル API で「1回の破壊で 2 Actor に分割される」ことを確認する。
`Tests/RunNativeSmokeTests.ps1` で実行できる。

---

### R-19. Animation

**方式**: **GPU Skinning**。Bone 行列を StructuredBuffer で頂点シェーダへ渡す。
さらに**前フレームの Bone 行列も保持する**。

**根拠**（`EditorSceneObject.h:195-198`）

```cpp
ID3D12Resource* currentSkinMatrixResource;   // 現在フレームの Bone 行列（StructuredBuffer）
Matrix4x4*      currentSkinMatrixData;       // Map 済み書き込み先
ID3D12Resource* previousSkinMatrixResource;  // 前フレームの Bone 行列
Matrix4x4*      previousSkinMatrixData;
```

Shader は `Assets/Shaders/Animation/SkinnedMesh.VS.hlsl` / `SkinnedObject.PS.hlsl` / `Skinning.CS.hlsl`。

**なぜ GPU Skinning か**

- CPU Skinning は頂点数に比例して CPU 時間と Upload 転送が増える。
  同じメッシュを複数インスタンス出すと、インスタンスごとに頂点全部を変換することになる。
- GPU なら Bone 行列（数十〜数百個）だけ転送すれば、頂点変換は頂点シェーダが並列に行う。

**なぜ前フレームの Bone 行列を持つのか（ここは推定でなく実装の必然）**

- Motion Vector（velocity）を出すには、**同じ頂点の前フレーム位置**が必要。
- スキンメッシュは Transform だけでなく Bone でも動くので、
  前フレームの Transform だけでは velocity が間違う。
- velocity が間違うと **Temporal AA と Motion Blur が破綻する**（残像・にじみ）。
  aaMode 3（Temporal）と Motion Blur を持つ以上、前フレーム Bone 行列は必須になる。

**非 Skin 頂点の扱い**: `g_identitySkinMatrixResource` を用意し、
非 Skin でも t16 / t17 を常に有効な SRV にする（`EditorSharedState.h:1060-1061`）。

**なぜそうするか**: Root Signature が同じなら、Skin の有無で Binding を切り替えずに済む。
未 Bind の SRV を読むと未定義動作になるため、恒等行列を差しておく。

**弱点**

- Bone 行列バッファをオブジェクトごとに 2本（現在・前）持つので、メモリが倍。
- Animation State 管理・Blend の詳細は未検証（`EditorAnimationManager` 2,041行、`AnimationGraph.cpp` あり）。

---

### R-20. Camera と Frustum Culling

**Camera**: Component 化済み。FOV / near / far / projection / DOF / motionBlur / exposure を持つ。

**なぜ Component 化したか（推定）**

- Camera が Engine 固定のグローバルだと、Scene に複数カメラを置けず、
  カットシーンや分割画面が作れない。Component なら GameObject の Transform をそのまま View に使える。
- DOF / Motion Blur / Exposure を Camera 側に持たせると、
  「このカメラで撮ったときの見た目」がデータとして Scene に残る。

**Frustum Culling**: 2段構えになっている。

| 段 | 場所 | 内容 |
| --- | --- | --- |
| CPU | Draw の前段 | Bounding に対する平面判定で明らかに外のものを落とす |
| GPU | `Culling/FrustumCulling.CS.hlsl` + `Culling/OcclusionCulling.CS.hlsl` | Frustum + Hi-Z Occlusion を GPU で判定 |

**なぜ GPU Occlusion だけにしないか**

- GPU Culling の結果は**次フレームの Predication**で使う（Readback しないため 1 フレーム遅れる）。
  CPU 側で明らかに外のものを先に落としておかないと、CommandList へ積む作業そのものが無駄になる。
- GPU 判定は「積んだ上で描画をスキップする」。CPU 判定は「積まない」。
  Draw Call の発行コスト自体を削るには CPU 側が必要。

**Hi-Z Occlusion の depthBias**: `0.0015f`（`EditorGpuCullingManager.cpp:137`）。

**弱点**

- GPU 判定が 1 フレーム遅れるので、カメラが速く動くと出るべきものが 1 フレーム消える可能性がある。
- Bounding 形状（Box か Sphere か）と CPU 判定の具体的な段は未検証。

---

### R-21. Editor と Runtime の分離

**方式**: 同一プロセス・同一 Manager 群で、**フラグと別 Manager で切り替える**。別プロセスにはしない。

- `isStandaloneGame_` が true のとき、`GameScene::Update` / `Draw` は Editor UI を全部飛ばす
  （`GameScene.cpp` の early return）。
- Play Mode は `EditorRuntimeManager::IsPlaying()`。Play 中だけ `EditorSceneLifecycleManager` が
  Physics / Script / Input Component を回す。
- Scene View と Game View は別 Viewport・別 Camera 行列。Temporal も**別々の履歴**を持つ
  （両 Viewport を処理するフレームでもカメラ履歴を混ぜない）。

**なぜ同一プロセスか（推定）**

- 別プロセスにすると、Play 中の値を Inspector で編集して即反映する経路が IPC になる。
  編集の即応性を優先している。
- 配布 Game は同じ実行体を `isStandaloneGame_` で動かすので、
  「Editor で見た通りに動く」経路が1本で済む。

**弱点**

- Editor 専用の状態と Runtime の状態が同じ `EditorSharedState` に同居する。
  配布 Game に Editor 用のグローバルが載る。
- Play 中の Scene 保存/読込はガードで禁止している（安全側だが機能制限）。
- Play 中に届いた他ユーザーの共同編集変更は適用を保留する（R-11）。

---

### R-22. Undo / Redo と Snapshot

**Undo**: スタックに上限を設けている（2026-09-13 の改善記録）。
方式（Command Pattern か変更前データ保持か）と1件あたりのメモリ量は**未検証**。

**Scene 保存の安全化（実装確認済み）**

- 一時ファイル + rename。失敗時に元データを壊さない。
- 未知の行を保持。新版の情報を旧版で開いて再保存しても消えない。
- Play 中の保存/読込ガード。

**共同編集の Snapshot / ChangeLog**: `LoadChangeLog()` で永続化し、再接続時に差分から復帰する（R-11）。
Snapshot の粒度・保存頻度・容量対策は**未検証**。

---

### R-23. Script API の境界

**方式**: C++ Native Script。Engine 内部と Game コードの境界を**関数ポインタテーブル**で切る。

- `Source/Engine/Core/EditorScriptApi.h` に公開 API を定義。`kEditorScriptApiVersion` で版を持つ（現在 **15**）。
- `EditorScriptManager::BuildRuntimeApi()`（417行）がテーブルを組む。
- Script は DLL としてロードする（`LoadModule`、173行）。
- 利用者向け C++ クラスは 79件、Runtime API は 415 Entry、Script Template は 26件。

**なぜ関数ポインタテーブル + DLL か（推定）**

- Engine 内部クラス（`EditorScene` など）をそのまま公開すると、
  内部の struct レイアウトが Game 側の ABI になる。Engine を直すたびに Game が壊れる。
- テーブル経由なら、**追加は末尾へ足すだけ**で既存 Script は再コンパイル不要。
  だから規約が「`EditorScriptApi.h` の末尾へ追加し、版番号を上げる」になっている。
- DLL 分離により、Script だけ再ビルドして Play し直せる。

**安全性（実装確認済み）**: 全 Wrapper が「Runtime API 未接続」「対象が存在しない」の両方で
`false` を返してクラッシュしないことを `Tests/CameraAudioRendererVfxApiSmoke.cpp` で確認している
（`RunNativeSmokeTests.ps1` で実行可能、現在 PASS）。

**弱点**

- 末尾追加しかできないので、**API の削除・シグネチャ変更が事実上できない**。古い API が残り続ける。
- 415 Entry が1つのテーブルなので、分野ごとの分割がない。

---

### R-24. Multithreading

**現状**: 描画・物理・Asset ロード・Scene 処理は**すべてメインスレッド**。
`std::thread` の使用は **9箇所 / 5ファイル**のみで、全部が I/O 待ちの分離。

| ファイル | 用途 |
| --- | --- |
| `Collaboration/TcpCollaborationTransport.cpp` | TCP 送受信 |
| `Online/WinHttpOnlineBackend.cpp` | HTTP 非同期 |
| `Speech/WhisperSpeechBackend.cpp` | 音声認識 |
| `Vision/MediaFoundationCameraSource.cpp` | カメラ取り込み |
| `Editor/EditorProfilerManager.cpp` | 計測 |

**なぜ全部並列化しないか**

- **I/O 待ちは別スレッドにする価値が明確**。ソケット・HTTP・マイク・カメラはブロックするので、
  メインスレッドに置くとフレームが止まる。ここは分離済み。
- **描画は CommandList が1本**で、複数スレッドから積む設計になっていない（F-4）。
  分離するには CommandList をスレッドごとに持ち、Barrier と実行順を管理する必要がある。
- **Editor が全状態を `EditorSharedState` の可変グローバル 332個で共有している**。
  この状態で並列化すると Race Condition が広範囲に出る。
  並列化の前提として、まず状態の所有者を分ける作業が必要になる。

**弱点**

- Asset ロードが同期なので、大きな Texture / Model のロード中にフレームが止まる（R-8）。
- Shader は起動時に全部コンパイルするので、起動時間がシェーダ数に比例する。
- 並列化の順序としては「Asset ロード → Shader コンパイル → CommandList 分割」が現実的だが、
  いずれも `EditorSharedState` への同時アクセスを整理してからでないと危険。

---

### R-25. SunPortal

**何を解決する機能か**: **屋内へ差し込む太陽光**を、Shadow Map や GI に頼らず解析的に与える。

**方式**: 矩形の「窓」を Scene に置き、その矩形を通して太陽光が室内へ入る量をピクセル単位で評価する。

**データ**（`EditorCommonTypes.h:213-224`、最大4枚 `kMaxSunPortals = 4`）

```cpp
struct SunPortalLight {
    Vector3 position;       // Portal面の中心（ワールド）
    float   halfWidth;      // 半幅（ローカルX方向）
    Vector3 outwardNormal;  // 外向き（Sun側）法線
    float   halfHeight;     // 半高（ローカルY方向）
    Vector3 right;          // ローカル右方向
    float   range;          // 室内側へ光が届く最大距離
    Vector3 up;             // ローカル上方向
    float   spreadRate;     // 室内へ入るほど照らす範囲が広がる割合
    Vector3 tint;           // Sunの色に掛ける色味
    float   intensityScale; // 強さの倍率
};
```

評価は `Assets/Shaders/Object3d.PS.hlsl:276` の `EvaluateSunPortalLight`。
直接光の計算に加算する（`:1119`）。Component は `EditorComponentType::SunPortal`。
`colliderSize.x/y` を半幅・半高に、`colliderRadius` を range に読み替えている
（`EditorRenderManager.cpp:2004-2006`）。

**なぜ必要になったか（推定）**

- Shadow Map だけだと、窓のある室内は「窓の形の光が床に落ちる」以上のことが起きない。
  壁や天井への回り込み（間接光）が無いので、室内が不自然に暗くなる。
- Probe GI はそれを補うが、Probe 間隔より細かい窓枠の形は出ない。Bake も分散なので即時性がない。
- Portal を矩形の面光源として解析的に評価すれば、**Probe 解像度に依存せず、Bake 待ちもなく**、
  窓から入る光の広がりを直接制御できる。

**他候補**: Probe 密度を上げる、窓位置に Area Light を置く、屋内専用の Lightmap をベイクする。

**不採用理由（推定）**

- Probe 密度を上げるとメモリと Bake 時間が効く。
- 汎用 Area Light では「室内へ入るほど広がる」`spreadRate` のような制御ができない。

**制約・弱点**

- **最大4枚**。窓が多い建物では足りない。
- 矩形のみ。丸窓やアーチは矩形近似になる。
- 遮蔽を考慮しない解析評価なので、Portal と評価点の間に物があっても光が通る。
- Sun のみ対象（`sunLight.intensity <= 0.0001f` で早期 return）。他のライトには効かない。

---

### R-26. 最適化（実際に効いた箇所）

**GPU 側**

| 手法 | 内容 |
| --- | --- |
| GPU Culling | Frustum + Hi-Z を GPU 判定、Readback せず Predication と `ExecuteIndirect`（R-9） |
| SSGI 半解像度 | 少数サンプルで低周波な情報なのでフル解像度で解かない（R-15） |
| Bloom 多段 | 4回 downsample + 3回 upsample。広い半径をフル解像度のブラーで作らない |
| 更新間引き | Shadow は状態ハッシュ比較で再描画を抑える。Ocean 反射・Light Probe Bake はフレーム分散 |
| GPU Skinning | Bone 行列のみ転送（R-19） |
| Instancing | 同一条件の SceneObject を Instance Buffer で1回の `DrawIndexedInstanced` に（上限 65,536） |

**CPU 側**

| 手法 | 内容 |
| --- | --- |
| GameObject 検索 | ID → index の hash 索引。Miss 時に全索引を再構築しない（`EditorScene.cpp:9987`） |
| Window の可視判定 | 非表示 Window は Draw 冒頭で return。重い処理の前に落とす |
| スクラッチバッファ再利用 | `Draw()` 内の `static std::vector` で毎フレームの確保を避ける（5個） |
| dirty フラグ | 環境 Texture / Color LUT の再読込はフラグが立ったときだけ |
| 空実装の除去 | `GameScene::Update` の no-op 呼び出し 10件を削除（2026-09-29） |
| 確保計測の inline 化 | Profiler OFF 時の `operator new` から関数呼び出しを除去（2026-09-29） |

**ボトルネック特定の手順**: R-10 参照。

**まだ効いていない / できていないこと**

- Asset ロードと Shader コンパイルが同期（R-8、R-24）。
- 毎フレームの Fence 待ちで CPU/GPU のパイプラインが重ならない（F-7）。
- Tiled Light Culling は Shader があるが未接続（F-6）。ライト数が増えたときの手が打たれていない。
- LOD の有無は未検証。

---

### R-27. 未検証項目（更新版）

R-12 の13項目のうち、以下を検証して本文へ反映した。

| 項目 | 結果 |
| --- | --- |
| Shadow の Bias | DepthBias 1200 / SlopeScaled 1.5 / Clamp 0.01（F-2）、Shader 側は傾斜依存 0.055〜0.014 を NDC 換算しクランプ（R-6） |
| Cascade Blend | **実装あり**。各 Cascade 末尾 12% で lerp、最終 Cascade は除外（R-6） |
| PCF | **9-tap**（`SampleSoftShadow9Tap`）。ハードウェア比較サンプラは未使用（R-6） |
| SSAO か GTAO か | **GTAO**。8方向×2ステップ=16サンプル、R8_UNORM（R-13） |
| SSR の構成 | Trace → Resolve → Temporal → Denoise → Composite の5段 Compute（R-14） |
| GI の方式 | Probe + SH + Visibility を主軸、SSGI を併用（R-15） |
| Multithreading | I/O のみ 9箇所/5ファイル。描画・物理・Asset はメインスレッド（R-24） |
| SunPortal | 矩形の窓による太陽光の解析評価、最大4枚（R-25） |

**まだ残っている項目 → R-28 以降で全部検証した。** 以下は検証前の一覧（履歴として残す）。

| 項目 | 必要な調査 |
| --- | --- |
| SSR の Ray Marching ステップ数と最大距離 | `SSRTrace.CS.hlsl` を読む |
| Shadow なしライトの上限と選別方式 | ライト配列の上限定数と選別ロジック |
| Frustum Culling の Bounding 形状と CPU 判定の段 | `FrustumCulling.CS.hlsl` と Draw 前段 |
| Asset の Unload と参照カウント | `AssetManager` / `AssetRegistry` の解放経路 |
| Undo / Redo の方式と1件あたりのメモリ | Undo スタックの実装 |
| Animation の Blend と State 管理 | `EditorAnimationManager`、`AnimationGraph.cpp` |
| Snapshot の粒度・頻度・容量対策 | 共同編集の Snapshot 経路 |
| LOD の有無 | Mesh の LOD 切り替えがあるか |
| Material Instance の扱い | Material が何を保持し Shader / Texture とどう繋がるか |
| Prefab の Override 反映と Nested の実体化条件 | Prefab Apply / Revert / Variant |

---

## 設計判断と技術選択 — 残り項目の検証結果（R-28 以降）

更新基準: 2026-09-29

R-27 後半に残していた10項目を検証した。**R-27 の「まだ残っている項目」表は、この節で全部埋まった。**

---

### R-28. SSR の Ray Marching（詳細）

R-14 で「ステップ数は要確認」としていた部分の実測値。**単純な線形マーチではなく Hi-Z 階層マーチ**だった。

**実装**（`Assets/Shaders/Reflection/SSRTrace.CS.hlsl`）

| 項目 | 実測値 | 場所 |
| --- | --- | --- |
| 最大ステップ数 | **72** | `for (uint stepIndex = 0u; stepIndex < 72u; stepIndex++)` :82 |
| 二分細分の反復 | **5回** | `for (uint refinementIndex = 0u; refinementIndex < 5u; ...)` :129 |
| 最大距離 | `max(gTemporalParameters.w, 1.0f)`（定数で外から与える） | :72 |
| 基本ステップ幅 | `max(maximumDistance / 640.0f, 0.04f)` | :76 |
| 開始 Mip レベル | **4** | `uint depthLevel = 4u;` :79 |
| 開始オフセット | `baseStepDistance * 16.0f` | :78 |
| 光線始点のオフセット | `lerp(0.025f, 0.08f, roughness)` | :74 |
| 厚み判定 | `0.0015f + accumulatedDistance * 0.00015f` | :106 |

**Hi-Z 階層マーチの仕組み**

`SampleDepthPyramid(uv, depthLevel)` で Depth Pyramid の粗い Mip を読み、
ステップ幅を `baseStepDistance * (1u << depthLevel)` で決める（:111, :120）。
空いている領域では粗い Mip で大きく進み、交差が近いと Mip を下げて細かく進む。
最後に**二分探索5回**で交差位置を詰める（:125-150）。

**なぜ階層マーチか（推定）**

- 均等ステップだと、遠くまで届かせるにはステップ数を増やすしかない。
  72ステップで `maximumDistance` を均等に刻むと 1ステップが粗すぎて薄い物体を貫通する。
- Depth Pyramid（R-9 で既に作っている）を使い回せば、追加コストなしで「空いている距離」を1回で飛べる。
- 二分細分を最後に入れるので、粗く飛んだ分の精度を取り戻せる。

**厚み（thickness）を距離に比例させる理由**

`0.0015f + accumulatedDistance * 0.00015f` と、進んだ距離に応じて厚み許容を広げている。
遠方は1ピクセルが覆う奥行きが大きいので、厚みを固定にすると遠方で交差を取り逃がす。

**画面外問題への対処（3種の Fade）**（:161-167）

| Fade | 式 | 目的 |
| --- | --- | --- |
| `edgeFade` | 画面端で減衰 | **画面外情報が無い問題**。端で反射を切らずに徐々に消す |
| `facingFade` | `saturate(1.0f - abs(dot(viewDirection, worldNormal)) * 0.35f)` | 正面を向いた面ほど SSR を弱める（歪みが目立つため） |
| `roughnessFade` | `1.0f - smoothstep(0.55f, 0.98f, roughness)` | 粗い面は SSR を切り、環境キューブへ任せる |

**弱点（更新）**

- `edgeFade` は「切れ目を隠す」対処であり、画面外の情報を作れるわけではない。
  カメラを回すと反射の量そのものが変わる。
- `roughnessFade` により roughness 0.98 以上は SSR が完全に切れる。
  この帯は Parallax Corrected Cubemap / 環境キューブが担当する（R-14 の使い分け表）。
- 二分細分5回は固定。薄い物体が重なる場面では精度が足りない。

---

### R-29. ライト数の上限と選別

| 項目 | 実測値 | 場所 |
| --- | --- | --- |
| **通常ライトの評価上限** | **16灯** | `EditorRenderManager.cpp:2236` のコメントが明記 |
| Shadow を落とせるライト | **5×5 Atlas に収まる分だけ** | 同上 |
| Emissive（放射光源）配列 | **32** | `EditorCommonTypes.h:141` `kMaxEmissiveLights = 32` |
| SunPortal | **4** | `EditorCommonTypes.h:208` `kMaxSunPortals = 4` |

```
// 通常ライトは16灯まで評価するが、影は5x5 Atlasに収まる分だけ描画する。
```

**Atlas 予算の再掲**（R-6）: Sun cascade 4 + Point 3灯 × 6面 = 22 / 25タイル。

**選別方式**: 「上位N灯だけ使う」ための明示的なスコア計算・ソートは**見つからなかった**。
Shadow については Atlas のタイル割り当て順で決まる。
つまり**ライトが多いシーンでどのライトが影を落とすかは、配置順に依存する**可能性がある。

**採用理由（推定）**

- 16灯を固定配列で Constant Buffer に積むと、可変長バッファや Structured Buffer を使わずに済む。
  Root Signature が単純になる。
- Shadow を Atlas 容量で切るのは、描画パス数（Point 1灯で6パス）が灯数に直接効くため。
  上限を設けないとフレーム時間が予測できない。

**他候補**: Tiled / Clustered Light Culling（画面タイルごとに影響ライトのリストを作る）。

**不採用理由（事実）**: `Assets/Shaders/Compute/TiledLightCulling.CS.hlsl` は**存在するがコードから未参照**（F-6）。
書きかけで繋がっていない。16灯という上限がそもそも Tiled の利点が出る規模に達していない。

**弱点**

- 17灯目以降は**評価されない**（暗くなる）。ライトを多用するシーンで破綻する。
- 影を落とすライトの選ばれ方が明示的でない。意図した主要ライトが影を落とさない可能性がある。
- ライト数を増やす場合は Tiled / Clustered を完成させるか、距離と輝度による明示的な選別を入れる必要がある。

---

### R-30. Frustum Culling の判定形状

**方式**: **AABB の 8 頂点をクリップ空間へ変換し、全頂点が同じクリップ平面の外にあるものだけ除外する。**

**根拠**（`Assets/Shaders/Culling/FrustumCulling.CS.hlsl`）

```
// AABB の全頂点が同じクリップ平面外にある物体だけを除外する
const float4 clipPosition = mul(float4(cornerPosition, 1.0f), gViewProjection);
```

**採用理由（推定）**

- Bounding Sphere の平面距離判定より**保守的で安全**。
  Sphere は AABB を包むので過大評価になり、細長い物体で無駄が出る。
- 「全頂点が同じ平面の外」という条件は**偽陽性（消してはいけないものを消す）を出さない**。
  逆に偽陰性（消せるのに残る）は出るが、残しても描画結果は正しい。
- クリップ空間での比較なので、平面方程式を別途作らずに ViewProjection 行列だけで済む。

**他候補**: Bounding Sphere の平面距離判定、OBB、階層的 Bounding Volume（BVH）。

**不採用理由（推定）**

- Sphere は上記のとおり過大評価。
- BVH はシーンが動的（Editor で常に編集される）ため、木の再構築コストが効く。

**CPU 段と GPU 段の役割分担**（R-20 の再掲・確定）

| 段 | 効果 |
| --- | --- |
| CPU | Draw Call を**積まない**。CommandList への書き込みコスト自体を削る |
| GPU（Frustum + Hi-Z Occlusion） | 積んだ上で**描画をスキップ**する。Predication / ExecuteIndirect |

**弱点**

- 8頂点変換を全オブジェクトに行うので、オブジェクト数に比例する（GPU 側なので並列だが無料ではない）。
- 「全頂点が同じ平面の外」判定は、視錐台の角をまたぐ大きな物体を除外できない（保守的な側の限界）。

---

### R-31. Asset の Unload と参照カウント

**方式**: **参照カウントを持たない。** 明示的な `Unload(path)` のみ。

**根拠**（`Source/Engine/Asset/AssetManager.h`）

```cpp
// Load / Reload / Unload / Invalidate / 変更検知 / 依存関係 / Hash を共通化する。
void Unload(const std::string& path);
```

`refCount` / `referenceCount` に相当するメンバは存在しない。
Cache は `std::unordered_map<std::string, HashCacheEntry> hashCache_`（`:80`）で、
内容ハッシュにより再読込の必要を判定する。
`AssetRegistry` は `NotLoaded` 等の状態を持つ（`AssetRegistry.h:18`）。

**採用理由（推定）**

- Editor では Asset は「プロジェクトを開いている間ずっと生きている」のが普通で、
  細かく解放する需要が小さい。
- 参照カウントを入れると、Component の `assetPath` / `assetId` の増減を全経路で追う必要がある。
  Undo・共同編集・Prefab 展開まで含めると漏れが出やすい。

**他候補**: `shared_ptr` による自動解放、世代別 GC、LRU による自動 Unload。

**不採用理由（推定）**: 上記の追跡コスト。Editor の寿命モデルに対して過剰。

**弱点**

- **使われなくなった Asset がメモリに残り続ける**。大きな Scene を切り替えながら長時間編集すると増える。
- 逆に `Unload` を明示的に呼ぶと、まだ参照している Component があっても止められない。
  安全性は呼び出し側の責任になっている。
- **非同期ロードが無い**（R-8）。ロード中はメインスレッドが止まる。

---

### R-32. Undo / Redo の方式とメモリ

**方式**: **Command Pattern ではない。Scene 全体のスナップショットを積む。**

**根拠**（`Source/Engine/Editor/EditorScene.h:2663,2703-2704`）

```cpp
void PushUndo();  // 現在の Scene 状態を Undo スタックへ積む
...
std::vector<std::vector<EditorGameObject>> undoStack_;
std::vector<std::vector<EditorGameObject>> redoStack_;
```

1 エントリが `std::vector<EditorGameObject>`、つまり**全 GameObject の完全なコピー**。

**上限**（`EditorScene.cpp:7987-7990`）

```cpp
constexpr size_t kMaximumUndoEntryCount = 64u;
while (undoStack_.size() > kMaximumUndoEntryCount) {
    undoStack_.erase(undoStack_.begin());
}
```

**64エントリ**まで。超えたら先頭（最古）から捨てる FIFO。

**メモリの見積もり**

1 エントリ = GameObject 数 × (GameObject 自体 + Component 数 × `sizeof(EditorComponent)`)。
`EditorComponent` は Field 1,596個で、うち `std::string` が多数含まれる
（`EditorScene.h` 全体で `std::string` 宣言が196箇所）。
**つまり「Component 1個あたり数 KB 級 × Component 総数 × 64」がワーストケース**になる。
Object 100体 × Component 3個で 1 エントリ数 MB、64 エントリで数百 MB に達しうる。

**採用理由（推定）**

- Component が R-1 の「fat struct + type tag」なので、**状態をコピーするだけで完全な復元ができる**。
  Command Pattern だと 289種類の Component × Field 1,596個分の「変更前/変更後」表現を作る必要がある。
- Undo の正しさが自明。差分適用のバグで Scene が壊れる経路がない。
- 共同編集が Property 単位 Revision（R-11）で差分を持つのに対し、Undo はローカル操作なので
  差分表現を共有する必要がない。

**他候補**: Command Pattern（操作オブジェクト）、Property 単位の差分記録。

**不採用理由（推定）**: 上記の実装量。R-1 のデータ設計が丸ごとコピーを安くしている（POD 中心）。

**弱点**

- **メモリ使用量が Scene 規模 × 64 に比例する。** 大きな Scene では支配的になりうる。
- 1 操作ごとに全 Scene をコピーするので、**Object 数が多いと `PushUndo` 自体が重い**。
- 上限 64 を超えると古い履歴が消える（上限化は 2026-09-13 の安定化改善で入った。それ以前は無制限だった）。

**改善候補**: Property 単位の差分記録へ移す（共同編集の `lastPropertyRevision_` の仕組みが流用できる）。
ただし Undo の正しさの自明性を失う。

---

### R-33. Animation の Blend と State 管理

**方式**: **State Machine + Blend Tree**。Root Motion とアニメーションイベントを持つ。

**処理順**（`EditorAnimationManager.h:35`）

```
State Machine -> Blend Tree -> Event -> Root Motion の順に更新する
```

**State Machine**（`EditorAnimationManager.h:142`）

`EvaluateStateMachine` が条件を満たした Transition を開始する。
遷移中は `transitionTime` / `transitionDuration` を持ち、**2つの Pose を混ぜる**（`:107-108`）。

**Blend Tree の種類**（`EditorAnimationManager.h:145-148`、`AnimationGraph.cpp:87-89`）

| 種類 | 内容 |
| --- | --- |
| `Blend1D` | 速度など1軸で2 Sample を混ぜる |
| `SampleDirectionalBlend` | 方向と大きさから周囲 Sample を混ぜる |
| `SampleCartesianBlend` | 任意2D Sample の近傍を混ぜる |
| `SampleDirectBlend` | Sample ごとの Parameter を直接ウェイトとして使う |

**採用理由（推定）**

- 移動アニメーション（歩き / 走り / 方向）は1軸または2軸のパラメータで表現するのが自然で、
  Blend1D と Directional があれば大半の移動表現が作れる。
- Direct Blend を持つのは、パラメータを直接ウェイトにしたい表情・部分ブレンド用途。

**未対応（docs に明記）**

- **Animation Layer なし**
- **Avatar Mask なし**
- **Nested State Machine なし**

（`engine-internals.md` の対応範囲表: 「Animator Graph 部分対応 — State、Transition、Any State、Blend Sample。Layer/Avatar Mask/Nested SM なし」）

**弱点**

- Layer が無いので、「下半身は歩き・上半身は攻撃」のような**部分的な重ね合わせができない**。
  Avatar Mask も無いため、Bone 単位の適用範囲指定もできない。
- Nested State Machine が無いので、状態数が増えると 1 階層に平たく並ぶ。

**GPU Skinning との関係**: R-19 参照。前フレーム Bone 行列を保持して Motion Vector を出す。

---

### R-34. Snapshot（共同編集）

**何を Snapshot として保存するか**: **Scene 全体の Text Payload。**

**根拠**（`EditorTeamCollaborationManager.h`）

| メンバ | 役割 |
| --- | --- |
| `std::string snapshotData`（:50） | Scene全体を安全に反映するためのPayload。**競合表示用の値とは分離する** |
| `std::string lastSnapshotText_`（:275） | 直近に送った Snapshot 本文 |
| `std::uint64_t lastSnapshotHash_`（:283） | 同一内容の Snapshot を送り直さないためのハッシュ |
| `float snapshotElapsedSeconds_`（:284） | 前回からの経過秒。**時間基準で送る** |
| `QueueCurrentSceneSnapshotForUser(targetUserId)`（:338） | **特定ユーザー宛て**に現在の Scene を送る |
| `ApplySceneSnapshot(changeEvent)`（:359） | 受け取った Snapshot を適用する |

**差分との関係**

- 通常の同期は Property 単位の差分（R-11）。
- Snapshot は**差分で追いつけない場合の土台**。
  `QueueCurrentSceneSnapshotForUser` が特定ユーザー宛てなので、
  **新規参加・再接続したユーザーへ現在の全体像を送る**用途。
- 以降はその Snapshot を基点に Revision 差分で進む。

**採用理由（推定）**

- 差分だけで同期すると、参加した瞬間に全履歴を再生する必要がある。
  ChangeLog が長いほど参加コストが増える。Snapshot があれば定数時間で土台が揃う。
- Scene が Text 形式（R-2）なので、Snapshot は保存形式そのままで作れる。専用の直列化を持たなくてよい。

**容量対策（実装確認済み）**

- **ハッシュ比較**（`lastSnapshotHash_`）で、内容が変わっていなければ送らない。
- **時間基準**（`snapshotElapsedSeconds_`）で頻度を抑える。毎フレーム送らない。
- 宛先を絞る（`...ForUser`）。全員へブロードキャストしない。

**弱点**

- Snapshot は Scene 全体なので、**大きな Scene では 1 回の送信量が大きい**。
- 送信間隔の具体値と、Snapshot と ChangeLog の切り替え条件は未確認。
- Play 中に届いた変更は適用を保留する（R-11）ので、Play が長いと保留分が積む。

---

### R-35. LOD

**方式**: **汎用的な Mesh LOD は持たない。** 特定システムだけが個別に LOD を持つ。

| 対象 | LOD の内容 | 場所 |
| --- | --- | --- |
| **Terrain** | **近 / 中 / 遠の3段階**。Shadow は一段低い LOD を使う | `EditorSceneObject.h:153` `terrainLodVertexOffsets`、`EditorInspectorPanel.cpp:8062` |
| **Ocean** | **連続 LOD**。`gridResolution = 2048` を仮想分割数とし、実頂点数は連続 LOD で抑える。カメラ追従の LOD 中心を持つ | `EditorSceneObject.h:73,75`、`EditorCommonTypes.h:271` |
| **破壊の破片** | 破片軽量化（GPU破片 / 物理数制限 / Cluster / LOD） | `EditorInspectorPanel.cpp:5257` |
| **Simulation** | `SimulationLOD` Component。距離で更新頻度を落とす | `EditorInspectorPanel.cpp:493,4646` |

**通常の Model Component には LOD 切り替えが無い。**

**なぜ Terrain / Ocean だけ持つか（推定）**

- Terrain と Ocean は**1オブジェクトで画面全体を覆う**。距離に応じて頂点密度を落とさないと、
  遠方の1ピクセルに何百頂点も来る。LOD が無いと成立しない。
- 通常の Model は**Frustum Culling と Occlusion Culling で消える**方が効く。
  LOD より先に「描かない」判定が効く規模を想定している。

**Shadow に一段低い LOD を使う理由**

- 影の輪郭は元形状より精度が要らない。Shadow パスは Cascade 数 × オブジェクト数だけ描くので、
  頂点数を落とす効果が本描画より大きい。

**弱点**

- **通常 Model の LOD が無い**ので、遠景に高ポリゴンのモデルを多数置くと頂点処理が支配的になる。
  Culling で消えない（見えている）場合に打つ手がない。
- LOD の自動生成（`meshoptimizer` は ThirdParty に同梱されている）を使った簡略化メッシュの生成経路は未確認。

---

### R-36. Material と Material Instance

**方式**: **独立した Material Asset / Material Instance は存在しない。**
Material パラメータは **Component の Field として持つ**。

**根拠**

- `MaterialData`（`EditorCommonTypes.h:306-324`）は**インポート時の構造体**。
  各 Field のコメントが「元アセットが持つ〜。なければ〜」であり、FBX / MTL から読んだ値を保持する。
- 実際に描画で使う値は `EditorComponent` の Field
  （`color` / `metallic` / `roughness` / `ior` / `alpha` / `emissionStrength` / `normalScale` /
  `clearCoat` / `transmission` / `anisotropy` / `sheen` / `alphaMode` / `doubleSided` /
  `uvTiling` / `uvOffset` 等、R-1 の 1,596 Field の一部）。
- Texture は `textureAssetPath` / `normalTextureAssetPath` / `metallicTextureAssetPath` /
  `roughnessTextureAssetPath` / `ambientOcclusionTextureAssetPath` / `emissionTextureAssetPath` /
  `heightTextureAssetPath` / `opacityTextureAssetPath` を Component が直接持つ。
- `useImportedMaterialTextures` が true のとき、**手動指定が空のスロットへ FBX 内の画像を自動適用する**
  （`EditorScene.h:790` のコメント）。

**採用理由（推定）**

- R-1 のデータ設計（fat struct）と一貫する。Material を別 Asset にすると、
  Inspector / Serialize / Undo / 共同編集の5経路に「Material 参照」という別の間接層が増える。
- Component に直接持てば、Inspector で値を触った瞬間に描画へ反映できる。
  Material Asset 経由だと、どの Component が影響を受けるかの逆引きが必要になる。

**他候補**: Unity 型の Material Asset + Material Instance（Shader + パラメータ集合を Asset 化）。

**不採用理由（推定）**: 上記の間接層コスト。

**弱点（これは明確な機能不足）**

- **同じ材質を複数オブジェクトで共有できない。** 100体の同じ材質を変えるには 100体を個別に編集する。
- Material 単位の使い回し・差分（Instance）が無いので、
  「基準材質を1つ直したら全部に反映」ができない。
- Shader とパラメータの組を Asset として保存できない。
  Shader は PSO として C++ 側に固定されている（F-6、Variant なし）。

**改善候補**: Material Asset を導入し、Component は Material への参照 + Override だけを持つ形へ。
ただし Serialize 形式（R-2 の列位置依存）と共同編集の Property 経路の変更が必要。

---

### R-37. Prefab の Override と Nested

**方式**: **汎用の Property Override 差分は持たない。** 明示的な Override と、保存時の実体化。

**Instance が持つデータ**（`EditorScene.h:2606-2608`）

```cpp
std::string prefabSourcePath;       // Prefab Instance の生成元 Asset。通常 Object は空
int32_t     prefabSourceObjectId;   // Prefab Asset 内で対応する元 Object ID（-1 で無し）
std::string prefabVariantBasePath;  // Variant Asset が継承する基底 Prefab。通常 Prefab は空
```

`EditorPrefab`（`:2629-2631`）は `gameObject` と `sourcePath` を持つ。

**対応範囲**（`engine-internals.md` の対応範囲表 / 138行目）

- 保存、Instantiate、**Apply**、**Revert**、**Variant**、複数の明示 Override に対応。
- **Nested Prefab は「部分対応」。完全な Nested 編集 / Override 階層ではなく、保存時の実体化を含む。**

つまり **Unity の Nested Prefab / Property Override 全般とは同等ではない**（docs が明言）。

**採用理由（推定）**

- 汎用 Property Override は「どの Field が Prefab から外れているか」を Field 単位で記録する必要がある。
  R-1 の Field 1,596個 × Component 数だけの差分フラグを持つことになり、
  Serialize 形式（R-2 の列位置依存）に載せるのが困難。
- `prefabSourceObjectId` で元 Object との対応だけ持てば、Apply / Revert は「その Object を丸ごと入れ替える」
  で実現できる。差分表現を持たずに済む。

**他候補**: Unity 型の Property Override（Field 単位の差分リスト）。

**不採用理由（推定）**: 上記の Serialize 形式との相性。

**弱点**

- **Field 単位の Override が無い**ので、「Prefab を更新したいが、この Instance の位置だけは保ちたい」
  といった細かい制御が明示 Override の範囲に限られる。
- **Nested は保存時に実体化される場合がある。** 入れ子 Prefab を保存すると親子関係が平坦化され、
  子 Prefab を後から直しても反映されない経路がある。
- Variant は `prefabVariantBasePath` で基底を1つ持つだけなので、多段継承は想定外。

---

### R-38. 未検証項目（現状）

R-12（初版13項目）と R-27（残り10項目）は**すべて検証して本文へ反映した。**

現時点で本文に「未確認」と書いてある残りは次の細目のみ。いずれも聞かれたら
「そこは確認していません」と答えて構わない粒度。

| 項目 | 節 |
| --- | --- |
| Snapshot の送信間隔の具体値と、ChangeLog との切り替え条件 | R-34 |
| `meshoptimizer` を使った簡略化メッシュ生成経路の有無 | R-35 |
| Shadow を落とすライトの Atlas 割り当て順の具体ロジック | R-29 |
| CPU 側 Frustum 判定が Draw のどの段で走るか | R-30 |

**この章の維持方法**

- 定数（72ステップ、64エントリ、16灯、4 Cascade、25タイル等）を変えたら、この章の該当行も直す。
- 「（推定）」付きの採用理由は、設計者本人の判断が確定したら「推定」を外す。
- 新しいシステムを追加したら、R 章に1節を追加する。節番号は連番で足す。

---

## 設計判断と技術選択 — 基礎編II（F-9 以降）

更新基準: 2026-09-29

F-1〜F-8 が「座標系・深度・色空間・API の使い方」だったのに対し、
F-9 以降は**描画の1段ずつ**を扱う。当たり前とされる段でも、このエンジンが実際にどの値を使い、
どこを省略しているかを実装値で固定する。

「一般にはこうする」ではなく「**このエンジンはこうしている**」を書く。
省略している段は、省略していることを明記する。

---

### F-9. 描画パイプラインの段と、このエンジンが使う段

**使っている段**

| 段 | このエンジンでの扱い |
| --- | --- |
| Input Assembler | 頂点5属性 + Index Buffer。Topology は `TRIANGLELIST`（ポストプロセスは頂点バッファ無しの3頂点） |
| Vertex Shader | ワールド変換、Skinning、Ocean 変位 |
| Hull / Domain（テッセレーション） | **水面のみ**（`waterTessellationPipelineState`）。通常モデルは使わない |
| Geometry Shader | **使わない** |
| Rasterizer | `FILL_MODE_SOLID`、既定 `CULL_MODE_BACK` |
| Pixel Shader | ライティング、影、PBR、材質 |
| Output Merger | Depth Test / Blend / RenderTarget 書き込み |
| Compute | Culling、Depth Pyramid、Ocean FFT、SSR、SSGI、Skinning、Particle、Exposure |

**Geometry Shader を使わない理由（推定）**

- GS は多くの GPU で実装効率が悪く、出力頂点の増幅がボトルネックになりやすい。
- GS でやりたくなること（Cube Shadow の6面同時描画、法線可視化、ビルボード展開）は、
  このエンジンでは別の手段で足りている。Point Light の6面は**6回描画**で行っている（R-6）。

**ポストプロセスが頂点バッファを持たない理由**

全画面パスは `DrawInstanced(3, 1, 0, 0)` で**3頂点**を出す。
頂点シェーダが `SV_VertexID` から画面を覆う三角形（全画面三角形）を生成する。
四角形（6頂点・2三角形）より、対角線上のピクセルが2回評価されないぶん効率が良い。

---

### F-10. 頂点レイアウトと Input Assembler

**実装**（`EditorPlatformManager.cpp:2171-2192`）

| # | Semantic | Format | 意味 |
| --- | --- | --- | --- |
| 0 | `POSITION` | `R32G32B32A32_FLOAT` | 位置。**float4**（w も持つ） |
| 1 | `TEXCOORD` | `R32G32_FLOAT` | UV |
| 2 | `NORMAL` | `R32G32B32_FLOAT` | 法線 |
| 3 | `BLENDINDICES` | `R32G32B32A32_UINT` | Bone index ×4 |
| 4 | `BLENDWEIGHT` | `R32G32B32A32_FLOAT` | Bone weight ×4 |

オフセットは全要素 `D3D12_APPEND_ALIGNED_ELEMENT`（前の要素の直後へ自動配置）。

**1頂点のサイズ**: 16 + 8 + 12 + 16 + 16 = **68 byte**。

**重要な点**

- **Skinning は 1頂点あたり最大4 Bone**。これは業界標準の上限で、5本目以降の影響は捨てる。
- **Tangent 属性が無い。** 接空間はピクセルシェーダで導出する（F-18）。
- **位置が float4**。float3 で足りるところを float4 にしているので、1頂点あたり 4 byte 余分。
  Ocean の変位計算が `float4 localPosition` を受け渡すため、揃えている。
- 非 Skin メッシュも同じレイアウトを使う。Bone index / weight には無害な値が入り、
  頂点シェーダ側は恒等行列（`g_identitySkinMatrixResource`）を引く（R-19）。

**採用理由（推定）**

- レイアウトを1種類に固定すると、Skin / 非 Skin で PSO と Root Signature を分ける必要がない。
  頂点サイズは増えるが、PSO 数と分岐が減る。

**弱点**

- 68 byte / 頂点は大きい。位置を float3、法線を `R10G10B10A2` などへ圧縮すれば 3〜4割減らせる。
  頂点数が多い Scene では帯域が効く。
- Tangent が無いぶん頂点は小さいが、その代わりピクセル側で毎ピクセル導出コストを払う（F-18）。

---

### F-11. Constant Buffer と 256 byte 境界

**D3D12 の制約**: Constant Buffer View のオフセットとサイズは **256 byte 境界**に揃える必要がある。

**このエンジンの対応**

- `EditorSceneObjectManager.h:94` に `static constexpr size_t kConstantBufferAlignment = 256u;`
- オブジェクトごとの Transform Buffer を 256 byte 単位で並べ、1本の大きな Upload Buffer から
  オフセットで切り出す。

**特筆すべき実装: `static_assert` でレイアウトを固定している**

`EditorCommonTypes.h` に **14個の `static_assert`** がある。例（`:203`）

```cpp
static_assert(offsetof(DirectionalLight, shadowCascadeCount) == 256u);
```

**なぜこれが重要か**

- Constant Buffer の struct は **C++ 側と HLSL 側で同じバイト配置**でなければならない。
- HLSL は float4（16 byte）境界をまたぐ変数を自動で次の境界へ押し出す（packing rule）。
  C++ 側の素直な struct 定義とズレる典型例がここ。
- ズレると**何もエラーが出ずに値が1つずれて読まれる**。ライトの色が影の枚数になる、といった
  デバッグの難しい壊れ方をする。
- `static_assert` はコンパイル時に落ちるので、**ズレた瞬間にビルドが失敗する**。
  これは「実行して目で見る」以外の検査手段が無い領域に、静的検査を入れている例。

**弱点**: `static_assert` は C++ 側のオフセットしか見ない。HLSL 側の struct を直したときは
検出できない。両者の対応はコメントで担保している
（例: `EditorCommonTypes.h:226-227` 「`Assets/Shaders/GI/ProbeCommon.hlsli` の
`LightProbeGridData` と同じ並びにする」）。

---

### F-12. Heap Type と Upload の流れ

**使っている Heap**

| Heap Type | 用途 |
| --- | --- |
| `D3D12_HEAP_TYPE_UPLOAD` | CPU から毎フレーム書き換えるもの。Transform、ライト、Instance Buffer、頂点の一部 |
| `D3D12_HEAP_TYPE_DEFAULT` | GPU だけが読むもの。Texture 本体、Depth、RenderTarget、Shadow Map |

**Texture の Upload の流れ**

1. `DEFAULT` Heap に本体を作る（`CreateTextureResource`）。
2. `UPLOAD` Heap に中間リソースを作る（`g_intermediateResources`）。
3. `UPLOAD` へ CPU から書き、`CopyTextureRegion` で `DEFAULT` へコピーする（`UploadTextureData`）。
4. Barrier で `COPY_DEST` → `PIXEL_SHADER_RESOURCE` へ遷移する。

**なぜ2段構えか**

- `DEFAULT` Heap は CPU から直接 Map できない（GPU 専用メモリ）。
  代わりに GPU からのアクセスが速い。
- `UPLOAD` Heap は CPU から Map できるが、GPU からは PCIe 越しで遅い。
  毎フレーム読むテクスチャを置く場所ではない。
- だから「CPU が書ける場所へ置いて、GPU 専用メモリへコピーする」が必要になる。

**毎フレーム書き換えるものは UPLOAD に置いたまま使う**

Transform / ライト / Instance Buffer は毎フレーム内容が変わるので、コピーせず `UPLOAD` から直接読む。
コピーのコストの方が高いため。**Map を解除せず持ち続ける**（`Matrix4x4* g_batchInstanceData` のように
Map 済みポインタをグローバルに保持）。

**これが F-7 のフレーム末尾 Fence 待ちの理由**: GPU が `UPLOAD` を読んでいる最中に
CPU が上書きすると壊れる。二重バッファを持たない代わりに毎フレーム待っている。

---

### F-13. PSO（Pipeline State Object）

**PSO とは**: 頂点レイアウト・シェーダ・Rasterizer・Blend・DepthStencil・RenderTarget フォーマットを
**1つの不変オブジェクトにまとめたもの**。D3D11 の個別 `Set*State` の代わり。

**なぜ存在するか**

- D3D11 は状態を個別に設定でき、Draw の直前にドライバが「この組み合わせ用のシェーダ」を
  内部でコンパイル・キャッシュしていた（実行時のシェーダパッチ）。これが予測できないスパイクを生んだ。
- D3D12 は組み合わせを**事前に固める**ことで、Draw 時のドライバ作業をなくした。
  代わりに**組み合わせの数だけ PSO を作る**必要がある。

**このエンジンの PSO 群**（`EditorPlatformManager::Initialize` 内で全部作る）

同じシェーダに対して Rasterizer / DepthStencil だけ変えた派生を明示的に並べている。

| PSO | 変えている点 |
| --- | --- |
| `graphicsPipelineState` | 基本。`CULL_BACK`、`DepthWriteMask ALL`、`DepthFunc LESS_EQUAL` |
| `cullFrontPipelineState` | `CULL_FRONT` + `DepthWriteMask ZERO` |
| `cullNonePipelineState` | `CULL_NONE` |
| `transparentPipelineState` | Blend 有効 + `DepthWriteMask ZERO` |
| `batchedCullFront/None...` | 上記の Instancing 版（頂点レイアウトに Instance 要素が付く） |
| `planarScenePipelineState` | `CULL_FRONT`（F-14 の理由） |
| `planarSurfacePipelineState` | `CULL_NONE` |
| `objectReflectionMaskPipelineState` | `CULL_NONE` + `DepthWriteMask ZERO` |
| `shadowPipelineState` ほか | Depth 専用 + Rasterizer バイアス（F-2） |
| `waterSurface/waterTessellation` | 水面。テッセレーション有り／無し |
| ポストプロセス各種 | `CreatePostProcessPSO(name, psBlob, format)` で量産 |

**弱点**

- **Shader Variant の仕組みが無い**（F-6）。`#ifdef` で分岐した派生シェーダを自動展開する経路がない。
  条件分岐はシェーダ内の `if` で行うため、使わない側の命令コストを払う場合がある。
- PSO をすべて起動時に作るので、**起動時間が PSO 数に比例する**。
  PSO Cache（`ID3D12PipelineLibrary`）は使っていない。

---

### F-14. Rasterizer State と面の向き

**既定**（`EditorPlatformManager.cpp:2199-2200`）

```cpp
rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
```

`FrontCounterClockwise` は設定していない → **既定 FALSE = 時計回り（CW）が表面**。
D3D の既定であり、左手系（F-1）と整合する。

**平面反射だけ `CULL_MODE_FRONT` にする理由**（`:2249`）

```cpp
planarScenePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
```

平面反射はシーンを鏡像として描く。鏡像変換は**行列式が負**の変換なので、
**三角形の巻き方向が反転する**。CW だった表面が CCW になる。
`CULL_BACK` のままだと**表面が消えて裏面が描かれる**（見た目が裏返る／消える）。
だから反射シーン専用 PSO で `CULL_FRONT` にして、反転を打ち消している。

**`CULL_MODE_NONE` を使う箇所と理由**

| PSO | 理由 |
| --- | --- |
| `planarSurfacePipelineState` | 反射面（水面・床）を両面から見えるようにする |
| `objectReflectionMaskPipelineState` | マスクなので表裏を問わず塗る必要がある |
| `cullNone` / `alphaCutoutShadowCullNone` | 板ポリの葉・布など、片面メッシュで裏からも見えるもの |

**`DepthWriteMask ZERO` を使う箇所と理由**

| PSO | 理由 |
| --- | --- |
| `transparentPipelineState` | 半透明は**深度を読むが書かない**。書くと後ろの半透明が消える |
| `objectReflectionMaskPipelineState` | マスク出力だけで深度を汚さない |
| `cullFront` / `batchedCullFront` | 裏面描画で深度を上書きしない |

---

### F-15. ブレンドと Depth の関係

**半透明のブレンド係数**（`EditorPlatformManager.cpp:2380-2387`）

```cpp
BlendEnable   = TRUE;
SrcBlend      = D3D12_BLEND_SRC_ALPHA;       // 色: src * a
DestBlend     = D3D12_BLEND_INV_SRC_ALPHA;   //     + dst * (1-a)
BlendOp       = D3D12_BLEND_OP_ADD;
SrcBlendAlpha  = D3D12_BLEND_ONE;            // α: src.a
DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;  //     + dst.a * (1-a)
BlendOpAlpha   = D3D12_BLEND_OP_ADD;
DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
```

色は標準のアルファブレンド（`src*a + dst*(1-a)`）。
**アルファチャンネルだけ `ONE / INV_SRC_ALPHA`** にしているのがポイントで、
これは「重ねた結果の不透明度」を正しく累積する式（`a_out = a_src + a_dst*(1-a_src)`）。
色と同じ係数にすると、後段（Bloom のマスクや Composite）でアルファを使うときに値がずれる。

**3つの透明表現の使い分け**

| 方式 | 順序依存 | 使う場面 |
| --- | --- | --- |
| Alpha Cutout（`discard`） | **なし** | 葉・金網・穴あき。不透明パスで処理し、深度も書く |
| 通常アルファブレンド | **ある** | 上記 PSO。深度は書かない |
| Weighted Blended OIT | **なし**（近似） | 順序に依存させたくない半透明（F-5、R-4） |

**Alpha Cutout を不透明パスで処理する理由**

`discard` は「描くか描かないか」の二択で、中間のブレンドをしない。
だから**深度を書いてよい**し、順序に依存しない。Shadow も同様に
`alphaCutoutShadowPipelineState` で穴を空けた影を落とせる。

---

### F-16. テクスチャのサンプリングと Mipmap

**Static Sampler は3本**（`EditorPlatformManager.cpp:1953-1973`）

| # | Filter | Address | Comparison |
| --- | --- | --- | --- |
| 0 | `MIN_MAG_MIP_LINEAR` | `WRAP` | `NEVER` |
| 1 | `MIN_MAG_MIP_LINEAR` | `CLAMP` | `NEVER` |
| 2 | `MIN_MAG_MIP_LINEAR` | `CLAMP` | `NEVER` |

`MIN_MAG_MIP_LINEAR` = **トライリニア**（縮小・拡大・Mip 間すべて線形補間）。

**`ComparisonFunc = NEVER` の意味**

比較サンプラ（`SamplerComparisonState` + `SampleCmp`）を**使っていない**ということ。
Shadow の深度比較を自前で行っている（R-6）。
ハードウェア比較サンプラを使えば 1 回の `SampleCmp` で 2×2 の比較結果を補間して得られるが、
このエンジンは 9-tap の各サンプルを手動比較している。

**Mipmap**

読み込み時に **CPU 側（DirectXTex）で生成**する（`EditorSharedState.h:172,245`）。

```cpp
bool generateMipmaps = true,   // 既定で生成する
...
hr = DirectX::GenerateMipMaps(...);
```

sRGB テクスチャには sRGB 対応のフィルタを選ぶ（`useSrgbMipFilter`）。
`.hdr` と `.dds` は既に Mip を持つ／リニアなので生成しない。

**なぜ Mipmap が必要か**

Mip が無いと、縮小表示されたテクスチャで**1ピクセルが元テクスチャの多数のテクセルをまたぐ**。
1点だけサンプルするとエイリアシング（ちらつき・モアレ）が出る。
Mip は事前に縮小した版を用意して、画面上のサイズに合う段を選ばせる仕組み。

**弱点: 異方性フィルタ（Anisotropic）を使っていない**

`D3D12_FILTER_ANISOTROPIC` / `MaxAnisotropy` の設定箇所が**コード内に存在しない**。
トライリニアのみなので、**地面のように視線と浅い角度で見る面が遠方でぼける**。
Mip の選択が最も粗い軸に合わせられるため。
異方性を入れるには Sampler の Filter を変え、`MaxAnisotropy` を設定するだけで済む
（Static Sampler なので Root Signature の再作成が必要）。

---

### F-17. 法線の変換（2026-09-29 修正済み）

> **2026-09-29 に修正した。** 以下は修正前の症状、修正内容、検証結果。

#### 修正前（バグ）

```hlsl
output.normal = normalize(mul(float4(localNormal, 0.0f), gTransformationMatrix.World).xyz);
```

**World 行列をそのまま法線に掛けていた。** 逆転置行列（inverse transpose）を使っていなかった。
**同じ誤りが 7 つの Vertex Shader にあった。**

| Shader | 該当式 |
| --- | --- |
| `Object3d.VS.hlsl` | `gTransformationMatrix.World` |
| `GBuffer/GBuffer.VS.hlsl` | 同 |
| `GI/ProbeCapture.VS.hlsl` | 同 |
| `Instancing/BatchedGBuffer.VS.hlsl` | `instanceData.World` |
| `Instancing/BatchedObject.VS.hlsl` | 同 |
| `Material/Water.VS.hlsl` | `gWaterTransform.World` |
| `Animation/SkinnedMesh.VS.hlsl` | `gSkinnedTransform.World` |

#### 修正内容

`Assets/Shaders/Common/NormalTransform.hlsli` を新設し、7 ファイルすべてを
`TransformNormalToWorld(localNormal, worldMatrix)` へ置き換えた。

Constant Buffer へ法線用行列を追加する方式は採らず、**シェーダ側で World から
余因子行列を作る**方式にした。理由:

- C++ / HLSL 共有構造体（`TransformationMatrix`）のレイアウトを変えずに済む。
  レイアウトのずれは値が静かに 1 つずれる形で壊れる（F-11）ので、変更を避けたい。
- Constant Buffer のサイズと帯域が増えない。
- 法線用行列を C++ 側で追加生成しなくて済む（スカラー実装なので CPU コストが乗る、R-40）。

**数式**: 逆行列の公式 `M⁻¹ = adj(M)/det(M)` より、行ベクトル規約では

```
(M⁻¹)ᵀ の各行 = cross(r1, r2), cross(r2, r0), cross(r0, r1)
```

（`r0..r2` は World 上 3×3 の各行）。これは余因子行列そのもので、
本来の逆転置行列を `det(M)` 倍したもの。**結果を正規化するのでスカラー倍の `1/det` は省略できる。**

この式は**せん断（shear）を含む任意の可逆行列に対して正しい**。
親子階層で非一様スケールと回転が交互に掛かるとせん断が生じるため（M-9）、
「各行をその長さで割る」簡易版ではなく余因子を使っている。

#### 検証（数値）

正しい逆転置 `(M3x3⁻¹)ᵀ` を掛けた結果との角度差を測った
（行ベクトル規約 / 行優先 / `Scale → RotX → RotZ` の合成、エンジンと同じ順序）。

| ケース | 修正後の誤差 | 修正前の誤差 |
| --- | ---: | ---: |
| 回転のみ `(1,1,1)` RotZ45 | 0.0000° | 0.0000° |
| 一様スケール `(2,2,2)` RotZ45 | 0.0000° | 0.0000° |
| 非一様 `(2,1,1)` RotZ45 / 斜面法線 | 0.0000° | **36.87°** |
| 非一様 `(2,1,1)` RotZ0 / 斜面法線 | 0.0000° | **36.87°** |
| 非一様 `(3,1,0.5)` RotX30 RotZ60 | 0.0000° | **55.52°** |
| 極端な非一様 `(10,1,1)` RotZ30 | 0.0000° | **77.91°** |

**回転のみ・一様スケールでは修正前後で結果が一致する。**
直交基底では `cross(r1,r2) == r0` になるため。したがって**既存シーンの見た目は変わらない**。
非一様スケールを掛けたオブジェクトのライティングだけが正しくなる。

#### 検証（ビルド）

- DXC（`vs_6_0`）で 7 本すべてコンパイル確認。
- Debug / Release / Development の 3 構成で 0 警告 0 エラー。
- `Tests/RunNativeSmokeTests.ps1` 3/3 PASS、`Tools/CheckSourceHygiene.ps1` 違反 0。
- **実機（エディタ起動）での目視確認は未実施。** 非一様スケールを掛けたオブジェクトの
  陰影が変わるので、確認する場合はそこを見る。

#### 鏡像変換（det < 0）の扱い

`1/det` の符号を落としているため法線が反転する。
平面反射は `CullMode` を `FRONT` へ切り替えて巻き方向の反転を打ち消しており（F-14）、
法線も同時に反転するのが整合する。

#### 修正前に何が起きていたか（説明用に残す）

**なぜこれが問題か**

法線は「面に垂直な方向」であって、位置と同じ変換則ではない。
World に**非一様スケール**（例: X だけ 2倍）が入ると、法線を同じ行列で変換すると
**面に垂直でなくなる**。正しくは逆転置行列 `(M^-1)^T` を掛ける。

- 回転のみ、または一様スケールのみなら、逆転置は元の行列のスカラー倍になる。
  `normalize` で正規化するので**結果は同じ**。だから多くの場面で問題が出ない。
- 非一様スケールのとき（`scale = (2, 1, 1)` など）**ライティングが目に見えて間違う**。
  引き伸ばした面の陰影が、引き伸ばしていない形状のものに見える。

**`float4(localNormal, 0.0f)` の `w = 0` は正しい**

w を 0 にすることで平行移動成分（`matrix[3][*]`）が効かない。
法線は方向なので平行移動してはいけない。これは合っている。

**`row_major` の明示**（`Object3d.VS.hlsl:4`）

```hlsl
row_major float4x4 World;
```

HLSL の既定は列優先（column_major）なので、C++ 側の行優先配置（F-1）と合わせるために
`row_major` を明示している。これが無いと**転置された行列が使われて全部壊れる**。
明示してあるので C++ 側で転置して送る必要がない。

**改善候補**

- Transform Buffer に法線用の逆転置行列（3×3 で足りる）を追加して渡す。
- または「非一様スケールを禁止する」と決めて、Inspector で警告を出す。

---

### F-18. 接空間と Normal Map

**方式**: **頂点に Tangent を持たず、ピクセルシェーダで画面空間微分から導出する。**

**実装**（`Assets/Shaders/Object3d.PS.hlsl:373-403` `BuildCotangentFrame`）

```hlsl
float3 positionDx = ddx(worldPosition);
float3 positionDy = ddy(worldPosition);
float2 uvDx = ddx(texcoord);
float2 uvDy = ddy(texcoord);
...
tangent   = normalize((positionDx * uvDy.y - positionDy * uvDx.y) * inverseDeterminant);
bitangent = normalize((positionDy * uvDx.x - positionDx * uvDy.x) * inverseDeterminant);
```

行列式が退化している（UV が潰れている）場合は外積でフォールバックする（`:397-398`）。

```hlsl
tangent   = normalize(cross(fallbackAxis, surfaceNormal));
bitangent = normalize(cross(surfaceNormal, tangent));
```

さらに UV 回転に対応して接空間を回す（`:403`）。

```hlsl
float3 rotatedTangent = tangent * rotationCos + bitangent * rotationSin;
```

**なぜ頂点 Tangent を持たないか（推定）**

- 頂点サイズが 68 byte（F-10）からさらに 12〜16 byte 増える。
- FBX に Tangent が無いモデルに対して、インポート時に生成する処理が必要になる
  （UV の継ぎ目でスムージングをどう扱うかの判断も必要）。
- `ddx/ddy` は**どんなメッシュでも動く**。Tangent の有無を気にしなくてよい。
- UV Tiling / Offset / Rotation を Component で変えても、導出なら自動で追従する。

**弱点**

- `ddx/ddy` は 2×2 ピクセルクアッド単位の差分なので、**接空間が三角形ごとに一定（faceted）**になる。
  頂点 Tangent を補間する方式より滑らかさで劣る。
  低ポリゴンで強い Normal Map を使うと、面の境目が見える。
- 毎ピクセルで 4 回の微分と正規化を行うコストがある（Tangent 補間なら VS で1回 + 補間）。
- ミラー UV（UV が反転しているメッシュ）で bitangent の符号が変わる場合、
  頂点 Tangent 方式なら符号（handedness）を持てるが、導出方式は行列式の符号に依存する。

---

### F-19. PBR の BRDF（実装の中身）

**方式**: Cook-Torrance 型のマイクロファセット BRDF。**異方性と Clear Coat を持つ。**

**基本の3項**（`Object3d.PS.hlsl:612-618`）

```hlsl
float  distribution = DistributionGGX(normal, halfVector, roughness);
float  geometry     = GeometrySmith(normal, viewDirection, lightDirection, roughness);
float3 fresnel      = FresnelSchlick(...);
return distribution * geometry * fresnel / denominator * radiance * normalDotLight * clearCoat;
```

| 項 | 役割 | 実装 |
| --- | --- | --- |
| D（法線分布） | 微小面の向きの分布。roughness が小さいほど鋭いハイライト | **GGX**（Trowbridge-Reitz） |
| G（幾何項） | 微小面同士の遮蔽・影 | **Smith** |
| F（フレネル） | 視線と面の角度による反射率の変化 | **Schlick 近似** |

**なぜ GGX / Smith / Schlick か（業界標準の理由）**

- **GGX** は裾（tail）が広く、実測値に近い。Blinn-Phong より金属のハイライトが自然。
- **Smith** は D と整合する形で導出されており、エネルギー保存が崩れにくい。
- **Schlick** はフレネルの厳密式（複素屈折率）を 1 つの指数で近似する。
  誤差が小さく、計算が 5 命令程度で済む。

**F0（垂直入射反射率）の作り方**（`:547-557` `BuildExtendedF0`）

```hlsl
const float iorF0 = AdvancedPbrIorToF0(gMaterial.ior);
const float dielectricF0 = IsOceanSurfacePass()
    ? max(iorF0, 0.001f)
    : max(0.04f, iorF0);
// metallic で「誘電体の F0」と「baseColor で着色した F0」を補間する
```

- **誘電体（非金属）の F0 は約 0.04**（IOR 1.5 相当）。この 0.04 が下限。
- IOR を材質が持っているので、ガラス・水のように IOR が違うものは F0 も変わる
  （`AdvancedPbrIorToF0` が `((n-1)/(n+1))^2` を計算する）。
- **金属は F0 が baseColor になる**（金は金色に反射する）。だから metallic で補間する。
- 水面パスだけ下限を 0.001 まで下げる。水は IOR 1.33 で F0 ≈ 0.02 なので、0.04 の下限が邪魔になる。

**水面の特別扱い**（`:997-998`）

```hlsl
const float waterF0Root = (waterIor - 1.0f) / (waterIor + 1.0f);
F0 = float3(1.0f, 1.0f, 1.0f) * waterF0Root * waterF0Root;
```

フレネルの厳密な F0 式 `((n-1)/(n+1))^2` を直接使っている。

**異方性**（`:409-471`）

`DistributionGGXAnisotropic` / `GeometrySmithAnisotropic` / `GeometryAnisotropicDirection` を持つ。
材質の `anisotropy` / `anisotropyRotation` が 0 でないとき、接空間の2方向で別の roughness を使う。
ヘアライン仕上げの金属、髪、ブラシ加工の表現用。

**Clear Coat**: `clearCoat` / `clearCoatRoughness` を持ち、基本層の上に薄い透明層の反射を足す。
車の塗装、ニスを塗った木材など。

**Lighting Model の切り替え**（R-5）

`lightingMode` が 0=なし / 1=Lambert / 2=Half Lambert / 3=PBR。
PBR 以外を選ぶと上記の BRDF は使わず、拡散だけの簡易計算になる。

**拡散の式**（`:241-243`）

```hlsl
float EvaluateDiffuseCosine(float normalDotLight, float subsurfaceWrap)
{
    return saturate((normalDotLight + subsurfaceWrap) / (1.0f + subsurfaceWrap));
}
```

通常は `saturate(N·L)`。`subsurfaceWrap`（材質の `subsurface`）が 0 でないとき、
**光の回り込み（wrap lighting）**を許す。分母で正規化してエネルギーが増えないようにしている。
水面・草・皮膚のような半透過素材用。

**弱点**

- IBL（環境光）の分割和近似（split-sum）用のプリコンピュート Compute Shader は**未参照**（F-6）。
  irradiance / prefilter / BRDF LUT 自体は Root Signature に載っていて動いているが、
  生成経路は C++ 側の手続き生成と Cube 読み込み。
- エネルギー保存の補正（multi-scatter GGX 補償）は確認していない。
  roughness が高い金属で暗くなる既知の現象があるが、対策の有無は未検証。

---

### F-20. SwapChain と Present

**設定**（`EditorPlatformManager.cpp:901-904`）

```cpp
swapChainDesc.SampleDesc.Count = 1;
swapChainDesc.BufferCount      = 2;
swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
```

**Present**（`EditorRenderManager.cpp:6832-6833`）

```cpp
const UINT presentSyncInterval = ProjectSettings::Get().GetData().vsyncEnabled ? 1u : 0u;
hr = swapChain->Present(presentSyncInterval, 0);
```

**`FLIP_DISCARD` を使う理由**

- 旧来の `DISCARD` / `SEQUENTIAL` は、Window へ出すときに**コピー**が入る（BitBlt モデル）。
- `FLIP_*` は back buffer を DWM へ**そのまま渡す**（フリップモデル）。コピーが消える。
- `FLIP_DISCARD` は前フレームの内容を保持しないので、`FLIP_SEQUENTIAL` より効率が良い。
  毎フレーム全画面を描き直すこのエンジンでは保持が不要。

**`BufferCount = 2` の意味**

back buffer が2枚（ダブルバッファ）。GPU が1枚へ描いている間、もう1枚が表示されている。
1枚だと描画中の内容が表示されてしまう（ティアリング／ちらつき）。

**vsync の扱い**

- `SyncInterval = 1`: 垂直同期を待つ。**ティアリング（画面の裂け）が出ない**が、
  フレームレートがリフレッシュレートの約数に制限される。
- `SyncInterval = 0`: 待たない。フレームレートは出るが**ティアリングが出る**。
- Project Settings で切り替えられる。`DXGI_PRESENT_ALLOW_TEARING` フラグと
  `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` は**使っていない**ので、
  可変リフレッシュレート（G-SYNC / FreeSync）の最適な経路にはなっていない。

**フレームレート制限**: Present とは別に `GameScene::ApplyFrameRateLimit()` を持つ。
vsync を切った状態でも上限を設けられる。

**弱点**

- `BufferCount = 2` + フレーム末尾 Fence 待ち（F-7）なので、**フレームが重ならない**。
  3枚（トリプルバッファ）+ フレームインフライト管理にすると CPU と GPU を並行させられる。
- Tearing 許可フラグが無いので、VRR ディスプレイで vsync を切ると単純なティアリングになる。

---

### F-21. MSAA を使わない理由

**事実**: `SampleDesc.Count` は SwapChain・Depth・Shadow・RenderTarget・PSO の**すべてで 1**
（`EditorPlatformManager.cpp:839,901,977,1058,1110,2229`）。**MSAA は一切使っていない。**

代わりにポストプロセス AA（**None / FXAA / SMAA / Temporal** の排他選択）を使う。

**なぜ MSAA を使わないか**

- **MSAA はジオメトリのエッジにしか効かない。** シェーダ内で生じるエイリアシング
  （Normal Map のちらつき、スペキュラのちらつき、Alpha Cutout の境界）には無効。
  このエンジンは PBR + Normal Map が主なので、そちらのエイリアシングの方が目立つ。
- **HDR との相性が悪い。** `R16G16B16A16_FLOAT` の 4x MSAA はメモリと帯域が 4 倍。
  ポストプロセスを何段も通す構成（R-16）では現実的でない。
- **Depth を SRV として読む構成と衝突する。** SSAO / SSR / SSGI / DOF / Motion Blur が
  深度を読む（F-2）。MSAA Depth は Resolve が必要で、Resolve すると情報が落ちる。
- **Temporal AA と両立しない。** aaMode 3 は前フレームの履歴とジッタで解像するので、
  MSAA のサブサンプルとは別の仕組み。両方やる意味が薄い。

**他候補と使い分け**

| 方式 | 特性 |
| --- | --- |
| FXAA | 1パス、安い、輪郭検出だけ。文字やディテールが甘くなる |
| SMAA | 3パス（Edge → Blend Weight → Neighborhood）。FXAA より輪郭が正確 |
| Temporal | ジッタ + 履歴。最も安定するが velocity が正しい必要がある（R-19） |
| None | Passthrough PSO で素通し |

**AA が排他である理由**: 複数かけると輪郭が二重にぼける。
`aaMode` を単一の権威として持ち（0=None / 1=FXAA / 2=SMAA / 3=Temporal）、
旧 bool（`smaaEnabled` / `taaEnabled`）は保存・読込の変換時のみのフォールバックにしている。

**弱点**

- Temporal 以外は**時間的な安定性が無い**。静止画は綺麗でもカメラを動かすとちらつく。
- Temporal は velocity に依存するので、velocity が出ない要素（一部のエフェクト等）でゴーストが出る。

---

### F-22. この基礎編で「やっていない」と確認した項目

聞かれたときに「使っていません」と答えるべき項目の一覧。
**無いことを知っているのと、知らないのは別。**

| 項目 | 状態 | 影響 |
| --- | --- | --- |
| Reverse-Z | 未使用（標準 Z） | 遠方の深度精度（F-1） |
| MSAA | 未使用 | ポストプロセス AA で代替（F-21） |
| 異方性フィルタ | 未使用（トライリニアのみ） | 浅い角度の面が遠方でぼける（F-16） |
| 法線の逆転置行列 | **2026-09-29 に対応済み**（余因子方式） | 非一様スケールでも正しい（F-17） |
| 頂点 Tangent | 持たない（ピクセルで導出） | 接空間が faceted（F-18） |
| Geometry Shader | 未使用 | Point Light は6回描画（F-9） |
| ハードウェア比較サンプラ | 未使用 | Shadow の深度比較を自前で行う（F-16、R-6） |
| Shader Variant | 仕組みが無い | シェーダ内 `if` で分岐（F-13） |
| PSO Cache（`ID3D12PipelineLibrary`） | 未使用 | 起動時間が PSO 数に比例（F-13） |
| Tearing 許可フラグ | 未設定 | VRR で最適経路にならない（F-20） |
| トリプルバッファ / フレームインフライト | 未使用（2枚 + 毎フレーム待ち） | CPU と GPU が重ならない（F-7、F-20） |
| Tiled / Clustered Light Culling | Shader はあるが未接続 | ライト上限 16灯（R-29、F-6） |
| 汎用 Mesh LOD | 未実装（Terrain / Ocean のみ） | 遠景の高ポリゴンに打つ手がない（R-35） |
| Material Asset / Instance | 存在しない | 材質の共有ができない（R-36） |
| Asset の参照カウント | 持たない | 使われない Asset が残る（R-31） |
| 非同期 Asset ロード | 未実装 | ロード中フレームが止まる（R-8） |
| 描画・物理の並列化 | 未実装（I/O のみ別スレッド） | 単一スレッド（R-24） |

---

## 3D の原理と方式の比較（G章）

更新基準: 2026-09-29

R章が「このエンジンの判断」、F章が「このエンジンの実装値」を扱うのに対し、
G章は**その土台になっている原理**と、**採らなかった方式が実際にどう動くのか**を扱う。

「なぜ A を選んだか」を説明するには、B と C の仕組みを知っている必要がある。
B の仕組みを知らずに「B は遅いので」と言うと、そこを掘られた時点で止まる。

G章は一般的な原理を扱うので、このエンジン固有の値は「**本エンジン**」と明記する。

---

### G-1. 座標変換の全段と、なぜ段を分けるのか

**全段**

```
ローカル座標 (モデルの原点基準)
  × World       → ワールド座標 (シーンの原点基準)
  × View        → ビュー座標 (カメラ原点、カメラの向きが軸)
  × Projection  → クリップ座標 (同次座標、w を持つ)
  ÷ w           → NDC (正規化デバイス座標)
  × Viewport    → スクリーン座標 (ピクセル単位) + 深度
```

**各段が何をしているか**

| 段 | 何をするか | なぜ必要か |
| --- | --- | --- |
| World | 回転・スケール・平行移動でシーンに配置 | モデルは原点基準で作られている。同じモデルを複数配置するため |
| View | カメラを原点・軸に合わせる座標系へ移す | カメラの位置と向きを「原点から -Z（GL）/ +Z（D3D）を見る」に正規化すると、以降の式が1通りになる |
| Projection | 視錐台を立方体へ変形し、遠近を w へ入れる | 遠いものを小さく見せる（透視）。同時にクリップ判定を単純な範囲比較にする |
| ÷ w | 透視除算 | 実際に遠近を反映させる段。ここまでは線形変換で、除算だけが非線形 |
| Viewport | NDC をピクセル座標へ | 画面サイズに依存する部分をここだけに閉じ込める |

**なぜ View と Projection を分けるのか**

- View はカメラの「どこから・どこを見て」、Projection は「どんなレンズで」に相当する。
  分けておくと、FOV を変えてもカメラ位置の計算に触らずに済む。
- ライティングは**ワールド空間**（またはビュー空間）で行う必要がある。
  Projection 後は遠近が入っていて、距離・角度の意味が壊れる。
  だから本エンジンは `worldPosition` を Pixel Shader へ渡している（F-17 の VS 出力）。

**なぜクリップ判定を Projection 後に行うのか**

Projection 後のクリップ座標では、視錐台の内外判定が

```
-w ≤ x ≤ w,  -w ≤ y ≤ w,  0 ≤ z ≤ w   (D3D)
-w ≤ x ≤ w,  -w ≤ y ≤ w, -w ≤ z ≤ w   (GL)
```

という**単純な範囲比較**になる。ワールド空間で6枚の平面と内積を取るより安い。
除算の前に判定するのは、w ≤ 0（カメラの背後）の点で除算すると符号が壊れるため。

**本エンジンの Frustum Culling はこの性質を使っている**: AABB の8頂点をクリップ空間へ変換し、
全頂点が同じ平面の外かを見る（R-30）。平面方程式を別に作らず ViewProjection だけで済むのはこの理由。

---

### G-2. 同次座標と w — なぜ4次元にするのか

**理由1: 平行移動を行列の掛け算で表せる**

3×3 行列では回転とスケールしか表せない。平行移動は加算であって乗算ではない。
4次元目を足して `w = 1` を置くと、

```
[x y z 1] × | 1  0  0  0 |   =  [x+tx  y+ty  z+tz  1]
            | 0  1  0  0 |
            | 0  0  1  0 |
            | tx ty tz 1 |
```

と、**平行移動も乗算に統一できる**。だから複数の変換を1つの行列に合成できる。
これがなければ、頂点ごとに「行列を掛けて、ベクトルを足す」を毎回書くことになる。

**w = 1 が点、w = 0 が方向**

- `w = 1`: 位置。平行移動の影響を受ける。
- `w = 0`: 方向（法線・ライト方向）。平行移動成分が掛からない。

本エンジンの法線変換が `mul(float4(localNormal, 0.0f), World)` になっているのはこれ（F-17）。
`w = 0` なので `matrix[3][*]`（平行移動）が効かない。**これは正しい。**

**理由2: 透視投影を線形変換として書ける**

遠近法は「距離で割る」操作で、本質的に非線形。
しかし「z を w へコピーする」線形変換を行い、**後段のハードウェアが一律に w で割る**という
2段構えにすれば、投影行列自体は線形のままにできる。

本エンジンの投影行列（F-1）

```cpp
result.matrix[2][3] = 1.0f;   // clip.w = view.z  ← z を w へコピー
```

この1行が「透視の正体」。あとは GPU が `x/w, y/w, z/w` を行う。

**なぜハードウェアに割らせるのか**

クリップ（G-1）を除算の前に行う必要があるため。
また、割る前の値（クリップ座標）でないと**透視補正補間**ができない（G-3）。

---

### G-3. 透視補正補間 — 三角形の中身をどう埋めるか

**問題**: 三角形の3頂点に UV が付いている。内部のピクセルの UV はどう決めるか。

**素朴な答え（間違い）**: スクリーン上の位置で線形補間する。

これをやると、**斜めに置いた床のテクスチャが歪む**。
手前と奥でピクセルあたりの実距離が違うのに、スクリーン上で等間隔に補間してしまうため。
初期のゲーム機（PlayStation 1 など）のテクスチャの揺れがこれ。

**正しい答え: 1/w で補間する**

頂点属性 `a` を補間するとき、スクリーン空間で線形補間できるのは `a/w` と `1/w`。

```
補間後の a = (a/w を線形補間した値) / (1/w を線形補間した値)
```

`1/w` はスクリーン空間で線形になる（これが数学的な鍵）。
GPU はこれを自動で行う。HLSL / GLSL で何も書かなくても補正される。

**`nointerpolation` の意味**

本エンジンの `Object3d.VS.hlsl:46-48` に

```hlsl
nointerpolation float3 oceanWorldAxisX : TEXCOORD5;
```

がある。`nointerpolation` は**補間せず、1頂点目の値をそのまま全ピクセルへ渡す**指定。
三角形内で一定の値（この場合 Ocean のワールド軸）は補間する意味がないので、
補間器のコストを省く。GLSL では `flat` に相当する。

**関連: 深度は線形補間される**

深度（NDC の z）はスクリーン空間で線形に補間される。
だから**ワールド空間の距離とは線形関係にない**。これが G-5 の深度精度の話につながる。

---

### G-4. ラスタライズの仕組み — なぜ 2×2 単位なのか

**三角形の内外判定（エッジ関数）**

三角形の各辺について、点がどちら側にあるかを符号付き面積で判定する。

```
E(x, y) = (x - x0)(y1 - y0) - (y - y0)(x1 - x0)
```

3辺すべてで同じ符号なら内側。この式は加減算だけで、しかも隣のピクセルへは差分で進める
（`E(x+1, y) = E(x, y) + (y1 - y0)`）ので、専用ハードウェアで極めて安く並列実行できる。

**符号でカリングも決まる**

3辺の符号がすべて正なら CCW、すべて負なら CW。つまり**三角形の巻き方向は
符号付き面積の符号で判定される**。これが `CULL_MODE_BACK` / `FRONT` の実装。

**だから F-14 の話が成立する**: 鏡像変換は行列式が負なので、符号付き面積の符号が反転し、
CW が CCW になる。本エンジンが平面反射で `CULL_FRONT` にしているのはこの反転を打ち消すため。

**2×2 ピクセルクアッド**

GPU は Pixel Shader を**必ず 2×2 の 4 ピクセル単位**で実行する。
三角形が 1 ピクセルしか覆っていなくても 4 ピクセル分走り、範囲外の3つは結果を捨てる
（ヘルパーピクセル）。

**なぜ 2×2 か**: **微分（`ddx` / `ddy`）を取るため。**
隣のピクセルの値との差分がないと、テクスチャの Mip レベルを決められない（G-6）。
2×2 あれば x 方向と y 方向の差分が両方取れる。

**この性質の帰結**

- **本エンジンの接空間導出（F-18）が成立するのはこのため。** `ddx(worldPosition)` は
  「隣のピクセルのワールド座標との差」なので、クアッド単位で一定。
  だから接空間が三角形ごとに一定（faceted）になる。
- **小さい三角形は効率が悪い。** 1ピクセルの三角形でも4ピクセル分のシェーダが走る。
  高ポリゴンモデルを遠景に置くとこれが効く（R-35 の LOD 不足の弱点と同じ話）。
- `discard`（Alpha Cutout）してもクアッドの他のピクセルは走る。分岐の節約にならない。

---

### G-5. 深度バッファの仕組みと精度の分布

**仕組み**: 各ピクセルに「今まで描いた中で最も近い深度」を保持し、
新しいピクセルの深度と比較して、近ければ書き、遠ければ捨てる。

**深度値の正体（D3D、標準 Z）**

投影行列（F-1）から

```
z_ndc = (far / (far - near)) - (near * far / ((far - near) * z_view))
```

つまり **z_ndc は z_view の逆数に比例する**。
結果として深度値の分布は**手前に密、奥に粗**になる。

**数値例**（near = 0.1、far = 1000）

| z_view | z_ndc |
| --- | ---: |
| 0.1 | 0.000 |
| 1.0 | 0.900 |
| 10 | 0.990 |
| 100 | 0.999 |
| 1000 | 1.000 |

**z = 10 より奥の全域が、深度値の残り 1% に押し込まれている。**
本エンジンは `D24_UNORM`（24bit 固定小数、F-2）なので、この 1% に約 16 万段階しかない。
遠方で Z ファイトが出るのはこれが原因。

**なぜ Reverse-Z が効くのか**

浮動小数は**0 付近で精度が高い**（指数部があるため）。
標準 Z は「奥 = 1.0 付近」に値が集まるので、**浮動小数の精度が高い領域と、
深度値が密集する領域が逆**になっている。最悪の組み合わせ。

Reverse-Z は near を 1.0、far を 0.0 に割り当てる。すると

- 深度値が密集するのは奥 → 奥が 0.0 付近 → **浮動小数の精度が高い領域と一致する**
- `D32_FLOAT` と組み合わせると、遠方の精度が劇的に改善する

**移行に必要な変更**（本エンジンは未対応、F-1 の弱点）

1. 投影行列の near / far を入れ替える
2. 深度クリア値 1.0 → 0.0
3. 比較関数 `LESS_EQUAL` → `GREATER_EQUAL`
4. 深度を線形化している全シェーダの式を直す（本エンジンは GTAO / SSR / SSGI / DOF / Motion Blur / Volumetric が該当）
5. Depth フォーマットを `D32_FLOAT` へ（固定小数だと Reverse-Z の利点が出ない）

**Early-Z と、それが効かなくなる条件**

GPU は Pixel Shader の**前に**深度テストを行おうとする（Early-Z）。
捨てられるピクセルのシェーダを走らせないので大きな節約になる。

しかし次の場合は Early-Z が無効になり、シェーダ実行後の Late-Z になる。

| 条件 | 理由 |
| --- | --- |
| `discard` を使う | 描くかどうかがシェーダ実行後に決まる |
| `SV_Depth` を書く | 深度がシェーダで変わる |
| UAV への書き込みがある | 副作用の順序が保証できない |

**本エンジンへの影響**: Alpha Cutout は `discard` を使うので Early-Z が効かない。
だから不透明パスの中でも Alpha Cutout を別 PSO に分けている意味がある
（`alphaCutoutShadowPipelineState` 等）。

**Z-Prepass（深度先行パス）という手法**

1回目: 深度だけ描く（Pixel Shader ほぼ空）。
2回目: 深度テストを `EQUAL` にして本描画。隠れたピクセルは Pixel Shader が走らない。

- **利点**: Pixel Shader が重い場合（PBR + 影 + IBL など）、無駄な実行を完全に消せる。
- **欠点**: 頂点処理が2回になる。頂点が重い（Skinning、テッセレーション）と逆効果。

本エンジンは `Depth/DepthOnly.VS.hlsl` / `DepthOnly.PS.hlsl` を持つが、
Z-Prepass として使っているかは未検証（R-38 に追加すべき項目）。
Depth Pyramid（R-9）の生成元として使っている可能性が高い。

---

### G-6. テクスチャの仕組み

**テクセルとピクセルは別物**

- テクセル: テクスチャ側の1マス。
- ピクセル（フラグメント）: 画面側の1マス。

UV は 0〜1 の連続値で、テクセルの中心は `(i + 0.5) / width` にある。
この 0.5 のずれを忘れると、**1テクセル分ずれた画像**になる（ポストプロセスでよくある不具合）。

**フィルタの仕組み**

| フィルタ | 動作 |
| --- | --- |
| Point / Nearest | 最も近いテクセルを1つ取る。拡大するとブロックが見える |
| Bilinear | 近傍 4 テクセルを UV の小数部で加重平均 |
| Trilinear | Bilinear を隣接する 2 つの Mip で行い、さらに Mip 間で補間 |
| Anisotropic | 画面上の1ピクセルがテクスチャ上で覆う「楕円」に沿って複数回サンプルする |

本エンジンは **Trilinear のみ**（`MIN_MAG_MIP_LINEAR`、F-16）。

**Mip レベルの選び方（LOD 計算）**

GPU は `ddx(uv)` / `ddy(uv)` から「1ピクセルがテクスチャ上で何テクセル進むか」を求める。

```
ρ = max(|∂uv/∂x|, |∂uv/∂y|) × テクスチャサイズ
LOD = log2(ρ)
```

ρ = 1（1ピクセル = 1テクセル）なら LOD 0、ρ = 2 なら LOD 1。
**G-4 の 2×2 クアッドが必要な理由がこれ。** 微分が取れないと LOD が決まらない。

**異方性フィルタの原理と、無いと何が起きるか**

床を浅い角度で見ると、1ピクセルがテクスチャ上で**細長い領域**を覆う。
横方向は 1 テクセル、縦方向（奥行き方向）は 20 テクセル、のような状態。

- Trilinear は `max` を取るので、**長い軸に合わせた粗い Mip を選ぶ**。
  結果、横方向も不必要にぼける。**これが本エンジンで地面が遠方でぼける理由**（F-16 の弱点）。
- 異方性フィルタは楕円の長軸に沿って複数サンプルを取るので、細い軸の解像度を保てる。
  `MaxAnisotropy = 16` なら最大16回サンプルする。

**Address Mode**

| モード | 動作 | 使う場面 |
| --- | --- | --- |
| `WRAP` | UV が 1 を超えたら 0 に戻る（繰り返し） | タイリングする材質。本エンジンの sampler 0 |
| `CLAMP` | 端の値を維持 | ポストプロセス。端でループすると反対側の色が来る。本エンジンの sampler 1, 2 |
| `MIRROR` | 折り返す | シームを隠したいとき |
| `BORDER` | 指定色を返す | Shadow Map の外側を「影なし」にしたいとき |

**本エンジンが Shadow で `BORDER` を使わず、UV を2テクセル内側へ寄せている理由**（R-6）:
単一 Atlas に複数のライトを詰めているので、「外側」が隣のライトのタイルになる。
`BORDER` では隣のタイルを避けられない。だから UV 側で内側に寄せている。

**sRGB テクスチャの仕組み**

sRGB フォーマットで SRV を張ると、**サンプリング時にハードウェアがリニアへ変換する**。
重要なのは変換の順序で、

1. テクセルを読む
2. **各テクセルを個別にリニア化する**
3. リニア値でバイリニア補間する

シェーダ内で `pow(color, 2.2)` を書くと、順序が「補間 → リニア化」になり**間違う**。
補間は線形空間で行わなければならない。これが F-3 で「sRGB フォーマットを使う」理由。

---

### G-7. 法線の逆転置行列 — なぜ必要か（数学）

**前提**: 法線 `n` は面上の任意の接ベクトル `t` と直交する。`n · t = 0`。

変換 `M` を掛けた後も直交していなければならない。
接ベクトルは位置の差なので `t' = M t` と変換される。
法線を別の行列 `N` で変換すると `n' = N n`。

直交条件を保つには

```
n' · t' = 0
(N n)ᵀ (M t) = 0
nᵀ Nᵀ M t = 0
```

これが任意の `t` で成り立つには `Nᵀ M = I`、つまり

```
N = (M⁻¹)ᵀ
```

**これが逆転置行列。**

**なぜ回転だけなら不要か**

回転行列 `R` は直交行列なので `R⁻¹ = Rᵀ`。したがって

```
(R⁻¹)ᵀ = (Rᵀ)ᵀ = R
```

**逆転置が元の行列と一致する。** だから回転だけなら World をそのまま掛けてよい。

**一様スケール `sI` の場合**

```
((sI)⁻¹)ᵀ = (1/s) I
```

元の行列のスカラー倍（`1/s²` 倍）。`normalize` するので**結果は同じ**。

**非一様スケールで壊れる例**

`scale = (2, 1, 1)`（X だけ2倍）のとき、45°の斜面の法線は

- 正しい変換: 斜面が横に伸びるので、法線は**より垂直に近づく**
- World をそのまま掛けた場合: 法線の X 成分が2倍になり、**より水平に近づく**（逆方向）

つまり陰影が逆に動く。**本エンジンはこの状態**（F-17）。

**なぜ多くのエンジンで問題が顕在化しないか**

非一様スケールを使う場面が限られるため。
ただし「板を引き伸ばして壁にする」「キャラクターを縦に伸ばす」は普通に行われるので、
使えば必ず出る。

**実装コスト**: 逆転置は 3×3 で足りる（平行移動は `w=0` で消えるため）。
Transform Buffer に 3×3（または float3x3 を float4×3 でパディング）を追加するだけ。

---

### G-8. 接空間（TBN）— なぜ必要か

**問題**: Normal Map はテクスチャに法線を格納している。しかしテクスチャの値は
**接空間（タンジェント空間）**の法線であって、ワールド空間ではない。

**なぜ接空間で保存するのか**

- ワールド空間で保存すると、**モデルを回転させた瞬間に法線が間違う**。
  テクスチャを作り直すことになる。
- 接空間なら「面に対して相対的な凹凸」を表すので、モデルがどう回転しても正しい。
- 平らな面の法線が常に `(0, 0, 1)` になるので、値が `(0.5, 0.5, 1.0)` 付近に集まり、
  圧縮効率も良い（Normal Map が青紫に見えるのはこれ）。

**TBN 行列**

```
T (Tangent)   : UV の u 方向に対応する面上のベクトル
B (Bitangent) : UV の v 方向に対応する面上のベクトル
N (Normal)    : 面の法線
```

この3本を並べた行列で、接空間 → ワールド空間へ変換する。

```
worldNormal = normalize(T * tn.x + B * tn.y + N * tn.z)
```

**T と B の求め方 — 2つの方式**

| 方式 | 仕組み | 長所 | 短所 |
| --- | --- | --- | --- |
| **頂点属性として持つ** | インポート時に、三角形の UV 勾配から計算し、頂点で平均化（スムージング）して格納 | 頂点間で補間されるので**滑らか**。ミラー UV の handedness を符号で持てる | 頂点が 12〜16 byte 増える。インポート時の生成処理が必要。UV の継ぎ目の扱いを決める必要がある |
| **ピクセルで導出**（本エンジン、F-18） | `ddx/ddy` で位置と UV の画面空間勾配を取り、そこから T / B を解く | 頂点属性が不要。どんなメッシュでも動く。UV 回転に自動追従 | クアッド単位なので**三角形ごとに一定（faceted）**。毎ピクセルのコスト。ミラー UV の符号が行列式依存 |

**導出の数学**（本エンジンの `BuildCotangentFrame`）

面上の位置変化と UV 変化の関係は

```
∂p/∂x = T (∂u/∂x) + B (∂v/∂x)
∂p/∂y = T (∂u/∂y) + B (∂v/∂y)
```

これを T, B について解く。2×2 の逆行列を使うと

```
T = ( ∂p/∂x · ∂v/∂y - ∂p/∂y · ∂v/∂x ) / det
B = ( ∂p/∂y · ∂u/∂x - ∂p/∂x · ∂u/∂y ) / det
det = ∂u/∂x · ∂v/∂y - ∂u/∂y · ∂v/∂x
```

本エンジンのコード（F-18）がこの式そのもの。
`det` が 0 に近い（UV が潰れている）ときは外積でフォールバックする。

**Object Space Normal Map という第3の選択肢**

接空間を使わず、モデルのローカル空間で法線を保存する方式。

- 長所: TBN が不要。UV の継ぎ目で法線が破綻しない。
- 短所: **モデル専用になる**（同じ Normal Map を別モデルへ使えない）。
  タイリングできない。スキニングで変形すると法線が追従しない。

だから汎用エンジンは接空間を使う。

---

### G-9. GPU の仕組み — なぜ分岐が遅く、なぜ並列が速いのか

**SIMD 実行**

GPU は 32個（NVIDIA: warp）または 64個（AMD: wave）の**スレッドを1つの命令で同時に動かす**。
1命令で32ピクセル分の計算が進む。これが並列性の源。

**分岐発散（divergence）**

```hlsl
if (condition) { A(); } else { B(); }
```

warp 内の32スレッドで `condition` が**バラバラだと、A と B の両方を実行する**。
条件を満たさないスレッドは結果を書かない（マスクされる）だけで、時間は消費する。

つまり**最悪ケースで A + B の合計時間**がかかる。

**帰結**

- warp 内の全スレッドが同じ分岐を通るなら、コストは片方だけ。
  画面上で連続した領域は条件が揃いやすいので、**空間的に固まった分岐は安い**。
- ピクセルごとにランダムな分岐（材質 ID による分岐など）は高い。
- 早期 `return` は、**warp 内の全スレッドが return しない限り**節約にならない。

**本エンジンへの影響**

- `lightingMode` の分岐（0/1/2/3）はオブジェクト単位で揃うので安い。
- GTAO の `[unroll]`（F-13 / R-13）でループを展開しているのは、
  ループのオーバーヘッドと分岐を消すため。サンプル数が固定（8×2）だから展開できる。
- Shader Variant が無い（F-13）ため、使わない機能の `if` を毎回通る。
  Variant があれば分岐ごと消せる。

**メモリ階層とレイテンシ隠蔽**

GPU のメモリアクセスは数百サイクルかかる。CPU のように大きなキャッシュで隠すのではなく、
**待っている warp を別の warp に切り替える**ことで隠す。

- 同時に走らせられる warp 数（**occupancy**）が高いほど隠蔽できる。
- occupancy はシェーダが使うレジスタ数と共有メモリ量で決まる。
  **レジスタを使いすぎるシェーダは occupancy が下がり、メモリ待ちが露出する。**
- 巨大なシェーダ（本エンジンの `Object3d.PS.hlsl` は 1,384行）はレジスタ圧が高くなりやすい。

**Compute Shader を使う理由がここにある**

- **共有メモリ（groupshared）が使える。** スレッドグループ内でデータを共有できるので、
  同じテクセルを何度も読む処理（ブラー、FFT、SSR の Trace）でメモリアクセスを減らせる。
- Pixel Shader はクアッド単位に縛られるが、Compute は任意のスレッド配置ができる。
- UAV へ自由に書ける（Pixel Shader は基本 RenderTarget へ1ピクセル）。

本エンジンが SSR / SSGI / Depth Pyramid / Ocean FFT / Culling / Skinning を
Compute にしているのはこの理由（R-9、R-14、R-15）。

---

### G-10. Draw Call がなぜ重いのか

**1回の Draw Call で起きること**

| 側 | 作業 |
| --- | --- |
| CPU | 状態の検証、コマンドのエンコード、ドライバのバッファへ書き込み |
| ドライバ | 状態差分の解決（D3D11 では必要に応じてシェーダのパッチ） |
| GPU | 状態の切り替え、パイプラインのフラッシュ（状態が変わると前の作業を完了させる必要がある） |

**D3D11 と D3D12 の違いがここに出る**

- **D3D11**: 状態を個別に設定でき、Draw の直前にドライバが「この組み合わせ」を解決していた。
  組み合わせが未知だと**その場でシェーダをコンパイル**することがあり、予測できないスパイクが出た。
- **D3D12**: PSO で事前に固める（F-13）ので、Draw 時のドライバ作業がほぼない。
  代わりに**組み合わせの数だけ PSO を作る**必要があり、起動時間に移った。

**削減の手段と、それぞれが何を削るか**

| 手法 | 何を削るか | 本エンジン |
| --- | --- | --- |
| **Instancing** | 同じメッシュ・同じ材質の N 個を 1 Draw に。CPU のエンコードと GPU の状態切り替えを N→1 | あり（上限 65,536、`batch.size() < 2` はしない、R-4） |
| **Batching（頂点結合）** | 別メッシュを1つの頂点バッファへ結合して 1 Draw に | 自動バッチングあり |
| **Frustum Culling（CPU）** | **Draw Call そのものを発行しない** | あり（R-30） |
| **GPU Culling + Predication** | 発行はするが GPU が実行をスキップ。CPU コストは残る | あり（R-9） |
| **ExecuteIndirect** | 引数を GPU が書き、CPU は1回だけ発行 | あり（R-9） |
| **状態のソート** | PSO / 材質ごとにまとめて状態切り替え回数を減らす | 描画順は `Draw()` の記述順。明示的な Render Queue ソートは持たない（R-4 の弱点） |
| **Bindless / Descriptor Heap 直参照** | Descriptor Table の切り替えをなくす | 未使用（インデックス固定方式、F-4） |

**`kGpuCullingMinimumObjectCount = 256`（`EditorRenderManager.cpp:2196`）の意味**

オブジェクト数が 256 未満のときは GPU Culling を使わない。
GPU Culling 自体に Dispatch と Barrier のコストがあるので、
**オブジェクトが少ないと元が取れない**。閾値を置くのは正しい判断。

---

### G-11. Forward / Deferred / Forward+ — 仕組みの違い

| 方式 | 仕組み | ライト数 | 材質の自由度 | 半透明 | 帯域 |
| --- | --- | --- | --- | --- | --- |
| **Forward** | 1回の描画で、そのピクセルの全ライトをループして解く | 少ない（ピクセル × ライト数） | **高い**（材質ごとに BRDF を変えられる） | **そのまま扱える** | 小 |
| **Deferred** | 1パス目で材質値を GBuffer へ書く。2パス目で GBuffer を読んでライトごとに解く | 多い（ライト × 画面のみ） | 低い（GBuffer の形式に収まるものだけ） | **扱えない**（別パス必須） | 大（GBuffer の書き読み） |
| **Forward+ / Tiled Forward** | 画面をタイルに分け、Compute でタイルごとの影響ライトリストを作る。Forward 描画でそのリストだけループ | 多い | **高い** | 扱える | 中 |
| **Visibility Buffer** | 三角形 ID だけ書き、後から属性を再構築して解く | 多い | 高い | 難しい | 小 |

**Forward の「ピクセル × ライト数」がボトルネックになる理由**

Forward は「描いたピクセルごとに全ライトを評価する」。
オーバードローがあると、**隠れるピクセルでもライト計算をする**。
ライト16灯 × オーバードロー3回 = 48回の無駄が出る。

**Deferred が効く理由**

GBuffer を作った時点で「各ピクセルの最終的な材質」が1つに確定する。
ライト計算は画面のピクセル数 × ライト数だけで済み、オーバードローが消える。

**Deferred が半透明を扱えない理由**

GBuffer は1ピクセルに1つの材質しか持てない。
半透明は「同じピクセルに複数の材質が重なる」ので表現できない。
だから Deferred のエンジンは半透明を Forward で別途描く（ハイブリッド）。

**本エンジンの選択（R-4）**: **Forward 主軸 + 部分 GBuffer。**
GBuffer は「後段の AO / Reflection が参照する」ためだけに作る（法線と材質値）。
ライティングは Forward で解く。

- 材質ごとに `lightingMode`（なし/Lambert/Half Lambert/PBR）を切り替えたい → Forward が必要
- 半透明（Weighted OIT）と屈折面を同じパイプラインで扱いたい → Forward が有利
- ライトが 16灯上限（R-29）なので、Deferred / Forward+ の利点が出る規模に達していない

**Forward+ を採らなかった事実の裏付け**: `Compute/TiledLightCulling.CS.hlsl` が
**存在するが未参照**（F-6）。書きかけて繋がっていない。

---

### G-12. 影の方式 — 仕組みの違い

**Shadow Map の原理（全方式の土台）**

1. ライトの位置・向きからシーンを描き、**深度だけ**を保存する。
2. 本描画で、ピクセルのワールド座標をライト空間へ変換する。
3. その位置の深度と Shadow Map の深度を比較する。
   Shadow Map の方が手前なら、間に何かある → 影。

**方式の比較**

| 方式 | 仕組み | 長所 | 短所 |
| --- | --- | --- | --- |
| **単一 Shadow Map** | 1枚で全範囲を覆う | 単純、1パス | 広い範囲だと近距離の**テクセル密度が不足**。影がギザギザ |
| **CSM（Cascaded）** | 視錐台を距離で分割し、Cascade ごとに Shadow Map を持つ | 近距離に解像度を寄せられる | パス数 × Cascade 数。**境界が見える**。調整項目が増える |
| **Cube Shadow Map** | Point Light 用。6面ぶん描く | 全方向を正しく覆える | **1灯で6パス**。灯数に対するコストが急 |
| **Dual Paraboloid** | Point Light を2枚（前後の放物面）で覆う | 6面→2面に削減 | 歪みが大きく、精度が落ちる。端の品質が悪い |
| **Perspective Shadow Map** | Spot Light 用。ライトの FOV で透視投影 | Spot に自然に合う | Spot 以外に使えない |
| **Shadow Volume（Stencil）** | 影の体積をジオメトリとして作り、Stencil で塗る | **完全にシャープ**、解像度非依存 | ジオメトリ量が爆発。ソフトシャドウ不可。現代では使われない |
| **Ray Traced Shadow** | 光線を飛ばして遮蔽を判定 | 正確、解像度非依存、ソフト対応 | 対応 GPU 必須、コスト大 |

**本エンジンの選択（R-6）**

- Directional: **CSM 4 Cascade**。分割は対数 × 0.68 + 均等 × 0.32。
- Point: **Cube Shadow Map（6面）**。最大3灯。
- Spot: Point と同じ位置ベース経路。
- 全部を**単一の 5×5 Atlas（各 1024×1024、25タイル）**に詰める。予算は 4 + 3×6 = 22。

**Atlas に詰める理由**: ライトごとに別テクスチャだと Descriptor 数と State 遷移が灯数に比例する。
Atlas なら SRV 1本、Barrier 1回で済む。
代償として**タイル境界の漏れ対策**（UV を2テクセル内側へ、R-6）が必要になる。

**Dual Paraboloid を採らなかったのは妥当**: 6→2 パスの削減は魅力だが、
Atlas のタイル予算（22/25）に収まっている現状では、品質を落とす理由がない。
灯数を増やしたくなった時点で再検討する価値がある。

---

### G-13. 影のフィルタリング — 仕組みの違い

**なぜフィルタが必要か**: Shadow Map の1テクセルが画面上で複数ピクセルを覆うと、
比較結果が 0 か 1 のどちらかになり、**階段状のギザギザ**が出る。

| 方式 | 仕組み | 長所 | 短所 |
| --- | --- | --- | --- |
| **PCF（Percentage Closer Filtering）** | 近傍 N 点を**それぞれ比較**し、影の割合を平均する | 単純、安定 | 半径が固定。**距離に応じたぼけ方（本影／半影）が出ない** |
| **ハードウェア比較サンプラ** | `SampleCmp` で 2×2 の比較結果をハードウェアが補間 | 1命令で 4 点分 | 2×2 に固定。カーネル形状を選べない |
| **PCSS（Percentage Closer Soft Shadow）** | まず遮蔽物の平均距離を探し、その距離から**フィルタ半径を決めて** PCF する | 遮蔽物が遠いほどぼける（物理的に正しい） | サンプル数が多い。2段構え |
| **VSM（Variance Shadow Map）** | 深度とその2乗を保存し、分散から影の割合を推定 | **事前ブラーできる**（テクスチャとして扱える） | Light Bleeding（明部が漏れる）が出る |
| **ESM / EVSM** | 指数関数で近似。VSM の改良 | Bleeding が減る | 調整が難しい |
| **Contact Hardening** | 接地点は鋭く、離れるほどぼかす | 見た目が自然 | PCSS の一種として実装が要る |

**本エンジンの選択（R-6）**: **9-tap PCF**。ハードウェア比較サンプラは使わない
（静的サンプラの `ComparisonFunc = NEVER`、F-16）。

**9-tap PCF の意味**: 3×3 の 9 点をそれぞれ手動比較して平均する。
ハードウェア比較サンプラ（2×2 × 4 回で 16 点相当）より点数は少ないが、
カーネル形状とバイアスを自分で制御できる。

**なぜ手動比較にしたか（推定）**: 本エンジンは受光面の傾きに応じた
**可変バイアス**（`lerp(0.055, 0.014, N·L)` を NDC 換算、R-6）を掛けている。
ハードウェア比較サンプラは比較値を1つ渡す形なので、サンプルごとにバイアスを変える余地が小さい。

**弱点**: PCF なので**半影が距離に依らず一定**。
接地点も遠方も同じぼけ方になる。PCSS へ拡張するには遮蔽物距離の探索パスが増える。

---

### G-14. AO の方式 — 仕組みの違い

| 方式 | 仕組み | サンプル効率 | 品質 |
| --- | --- | --- | --- |
| **SSAO（原典）** | 半球内にランダム点を撒き、深度バッファと比較して「埋まっている点」の割合を数える | 低い（ノイズが多く、多数サンプルが必要） | ノイズを消すために強いブラーが必要 |
| **HBAO（Horizon Based）** | 方向ごとに**水平角**（地平線の角度）を求め、そこから遮蔽を積分する | 中 | SSAO より形が安定 |
| **GTAO（Ground Truth）** | HBAO を発展させ、**可視性の積分を解析的に解く**。コサイン項を含めて正規化する | **高い**（少サンプルで安定） | 参照解（レイトレ）に近い |
| **RTAO（Ray Traced）** | 実際に光線を飛ばす | — | 正確。画面外も評価できる |
| **Bent Normal 併用** | 遮蔽の少ない方向を法線として返し、IBL の入力に使う | — | 間接光の方向が正しくなる |

**SSAO と GTAO の本質的な違い**

- SSAO は「点をランダムに撒いて数える」**モンテカルロ法**。誤差は √サンプル数 に反比例するので、
  ノイズを半分にするには4倍のサンプルが必要。
- GTAO は「水平角を求めて積分する」**解析的手法**。方向ごとに1つの角度を求めるので、
  方向数を増やせば線形に精度が上がる。少ないサンプルで形が決まる。

**本エンジンの選択（R-13）**: **GTAO、8方向 × 2ステップ = 16サンプル**、`R8_UNORM` 出力。

- 方向をピクセル毎に疑似乱数で回転（`frac(sin(dot(pos, (12.9898, 78.233))) * 43758.5453)`）。
  少ない方向数を、空間的にばらして後段フィルタで平均する。
- 第2パスは `ContactShadow.PS.hlsl`（名前と中身が違う）で**深度考慮バイラテラルフィルタ**。
  通常のガウシアンだと深度の段差を越えて AO が滲み、輪郭が甘くなる。

**スクリーンスペース AO 全体の原理的限界**

深度バッファは**1層しかない**（各ピクセルに最も近い深度だけ）。したがって

- **画面外の遮蔽物は評価できない**
- **遮蔽物の裏側は評価できない**（薄い壁の向こうが見えない）
- カメラを動かすと AO が変化する（本来は静的なのに）

これは SSAO / HBAO / GTAO 共通の限界で、手法を変えても解決しない。
解決するには RTAO か、事前ベイクした AO（Ambient Occlusion Map）が必要。

---

### G-15. 反射の方式 — 仕組みの違い

| 方式 | 仕組み | 正確さ | コスト | 画面外 |
| --- | --- | --- | --- | --- |
| **Planar Reflection** | 反射面について**シーンをもう一度描く**（カメラを鏡像位置へ） | 平面なら**正確** | 描画が実質2倍 | **取れる** |
| **Cubemap（静的）** | 事前に6面をベイクし、反射方向でサンプル | 粗い。位置ずれがある | 極小 | 取れる |
| **Parallax Corrected Cubemap** | Cubemap を「箱」と見なし、反射光線と箱の交点で補正 | 位置ずれが減る | 小 | 取れる |
| **SSR** | 深度バッファに対して Ray Marching | 画面内なら正確。動く物体も映る | 中〜大 | **取れない** |
| **Ray Traced Reflection** | 実際にシーンへ光線を飛ばす | 正確 | 大。対応 GPU 必須 | 取れる |

**Planar Reflection の仕組みと、なぜ平面限定か**

反射面の平面について、カメラを鏡像位置・鏡像向きへ置いてシーンを描く。
平面なら「鏡像変換」が1つの行列で書けるので成立する。
曲面だと反射方向がピクセルごとに違うため、1回の描画では表せない。

**このとき巻き方向が反転する** → 本エンジンは `CULL_FRONT` で打ち消す（F-14、G-4）。

**SSR の仕組み（本エンジンの実装、R-28）**

1. ピクセルの法線と視線から反射方向を求める。
2. その方向へ**深度バッファ上を進む**（Ray Marching）。
3. 進んだ先の深度が、光線の深度より手前になったら「交差した」と判定する。
4. 交差位置の色を持ってくる。

**Hi-Z 階層マーチ**（本エンジン）: Depth Pyramid の粗い Mip を読み、
「この区間には何も無い」と分かれば大きく飛ぶ。開始 Mip 4、最大 72 ステップ、二分細分5回。

**なぜ画面外が取れないか**: 深度バッファは「今の画面に映っているもの」しか持たない。
反射方向が画面外を向いたら、参照するデータが存在しない。
本エンジンは `edgeFade` で端を減衰させ、切れ目を目立たなくしている（対症療法）。

**本エンジンの使い分け（R-14）**: 3つを併用する。

- 平面（水面・床）→ **Planar**（正確さ優先）
- 任意形状で画面内 → **SSR**
- 画面外と粗い反射（roughness > 0.98）→ **Parallax Corrected Cubemap / 環境キューブ**
  （`roughnessFade` で SSR を切る、R-28）

これは「1つの方式で全部を解かず、得意な範囲を分担させる」という実用的な設計。

---

### G-16. GI の方式 — 仕組みの違い

**GI（大域照明）とは**: 光が面で反射して他の面を照らす、間接的な照明。
直接光だけだと、影の中が完全に真っ暗になる。

| 方式 | 仕組み | 動的対応 | メモリ | 精度 |
| --- | --- | --- | --- | --- |
| **Lightmap（ベイク）** | 静的な面ごとに間接光をテクスチャへ焼く | **不可**（静的のみ） | 大（UV 展開が必要） | 高 |
| **Light Probe + SH** | 空間にプローブを置き、各点の入射光を球面調和で保存。動的物体はプローブを補間して受ける | 物体は動ける。ライトは要再ベイク | **小**（SH 2次 = 9係数） | 低周波のみ |
| **Voxel Cone Tracing** | シーンをボクセル化し、円錐でトレースして間接光を集める | 可 | 大（3Dテクスチャ） | 中 |
| **DDGI（動的プローブ）** | プローブを毎フレーム少しずつレイトレで更新 | **可** | 中 | 中〜高 |
| **SSGI** | 画面空間で、近傍ピクセルの色を間接光として集める | 可 | 極小 | **画面内のみ** |
| **Path Tracing** | 光線を多数飛ばして積分 | 可 | 小 | 最高。コスト最大 |

**SH（球面調和）の仕組みと、なぜ使うのか**

ある点に**全方向から入ってくる光の分布**を、方向の関数として表したい。
これを愚直にやるとキューブマップ（6面 × 解像度²）になる。

SH は球面上の関数を、**低次の基底関数の係数**で表す方法（フーリエ級数の球面版）。
2次（9係数）まで取れば、**拡散反射（Lambert）に必要な低周波成分をほぼ再現できる**。

- プローブあたり 9係数 × RGB = 27 float = 108 byte。キューブマップより桁違いに小さい。
- **SH は線形**なので、プローブ間の補間が**係数の線形補間でそのまま成立する**。
  これが「グリッド上のプローブを三線形補間する」設計を可能にしている。
- ただし低周波しか表せないので、**鏡面反射（高周波）には使えない**。
  だから鏡面は別途 Cubemap / SSR が必要（G-15）。

**Light Leaking（光漏れ）と Visibility**

プローブグリッドは壁を無視して配置される。壁の向こう側のプローブを補間に使うと、
**壁を透けて光が漏れる**。

本エンジンは `GI/ProbeVisibility.CS.hlsl` で可視性を持ち、
見えないプローブの重みを落とす（R-15）。これが無いと屋内が外の光で明るくなる。

**本エンジンの選択（R-15）**

- 主軸: **Probe + SH + Visibility**。Bake は**1フレームに数個ずつ分散**（`lightProbeBakeThrottleFrameIndex`）。
- 補完: **SSGI**（半解像度で解き、Temporal で均し、フル解像度へ加算）。
- 完全リアルタイム GI を採らない理由: 想定規模（1〜5人の Windows 向け制作）に対して
  Probe + SSGI で足り、Bake 分散なら編集中も止まらない。

**Bake を分散する仕組みの意味**: 全プローブを一度に焼くとフレームが止まる。
数個ずつなら、数フレームかけて結果が追従する。編集中のツールとして重要。
代償として**ライトを大きく動かした直後は間接光が古い**。

**SunPortal がこの文脈で必要になった理由**（R-25）

Probe は間隔より細かい変化を表せない。窓枠の形をした光は Probe 解像度では出ない。
Bake 分散なので即時性もない。
そこで**窓を矩形の面光源として解析評価**すれば、Probe 解像度に依存せず Bake 待ちもない。
代償は遮蔽を考慮しないこと、最大4枚、矩形のみ。

---

### G-17. アンチエイリアスの方式 — 仕組みの違い

**エイリアシングの正体**: 1ピクセルが「覆っている領域の平均」であるべきなのに、
**1点だけサンプルしている**ことによる標本化誤差。

| 方式 | 仕組み | ジオメトリ境界 | シェーダ内 | 時間安定 | コスト |
| --- | --- | --- | --- | --- | --- |
| **SSAA / DSAA** | 高解像度で描いて縮小 | ◎ | ◎ | ◎ | 極大（N倍の全部） |
| **MSAA** | **深度・被覆率だけ**をサブサンプル数分持ち、Pixel Shader は1回。エッジのピクセルだけ複数値を混ぜる | ◎ | **×** | △ | 中（帯域がサンプル数倍） |
| **FXAA** | 完成画像の輝度差から輪郭を推定し、その方向にぼかす | △ | △ | × | **極小**（1パス） |
| **SMAA** | 輪郭検出 → **パターン分類して混合重みを計算** → 近傍合成の3パス | ○ | △ | × | 小（3パス） |
| **TAA** | 毎フレーム**サブピクセル単位でジッタ**をかけ、velocity で前フレームを引き当てて累積 | ◎ | **◎** | **◎** | 中（履歴バッファ） |

**MSAA がシェーダ内エイリアシングに効かない理由**

MSAA は「1ピクセル内のどのサブサンプルが三角形に覆われているか」を持つ。
しかし **Pixel Shader は1回だけ実行される**（ピクセル中心で）。
だから Normal Map のちらつき、スペキュラのちらつきは**1回の結果がサブサンプル全部にコピーされる**。
改善しない。

**TAA がシェーダ内エイリアシングに効く理由**

毎フレーム**投影行列にサブピクセルのオフセット（ジッタ）**を入れる。
フレームごとに違う点をサンプルするので、時間方向に平均すると実効サンプル数が増える。
シェーダの結果自体がフレームごとに違う点で評価されるので、シェーダ内エイリアシングにも効く。

**TAA の必須条件: velocity（Motion Vector）**

前フレームのどこに同じ表面があったかを知る必要がある。
カメラの動きだけでは足りず、**物体自身の動き**も必要。

**だから本エンジンは前フレームの Bone 行列を保持している**（R-19、F-10）。
スキンメッシュは Transform だけでなく Bone でも動くので、
前フレーム Transform だけでは velocity が間違い、TAA が残像を出す。

**TAA の弱点**: 履歴が誤ると**ゴースト**が出る。速く動く物体、
急に現れる物体（disocclusion）で発生する。だから本エンジンは
Disocclusion Mask と Reactive Mask を持つ（AGENTS.md の PR #8 の内容）。

**本エンジンの選択（F-21、R-16）**: **MSAA を使わず、None / FXAA / SMAA / Temporal を排他選択。**

MSAA を避けた4つの理由:
1. ジオメトリ境界にしか効かない（PBR + Normal Map の方が目立つ）
2. HDR `R16G16B16A16_FLOAT` の 4x MSAA は帯域4倍
3. Depth を SRV で読む構成と衝突（Resolve で情報が落ちる）
4. TAA と機構が重複する

**AA が排他である理由**: 複数かけると輪郭が二重にぼける。
`aaMode`（0=None / 1=FXAA / 2=SMAA / 3=Temporal）を単一の権威として持つ。

---

### G-18. 半透明の方式 — 仕組みの違い

**根本問題**: アルファブレンドの式 `dst = src*a + dst*(1-a)` は**可換でない**。
つまり描く順序を変えると結果が変わる。

| 方式 | 仕組み | 順序依存 | 正確さ | コスト |
| --- | --- | --- | --- | --- |
| **物体単位ソート** | 奥から手前へ並べて描く | 解消（不完全） | 交差・貫通する形状で**破綻** | ソートの CPU コスト |
| **ピクセル単位ソート** | 不可能（GPU は描画順を保証しない） | — | — | — |
| **Alpha Test（`discard`）** | 閾値で描く／描かないの二択 | **なし** | 中間の半透明が表せない | 極小。ただし Early-Z 無効（G-5） |
| **Depth Peeling** | 最前面から1層ずつ「剥がして」複数パスで描く | **なし** | **正確** | 層数分のパス。層数が読めない |
| **Per-Pixel Linked List** | UAV にピクセルごとのフラグメントリストを作り、後でソートして合成 | **なし** | 正確 | メモリが読めない。ソートが重い |
| **WBOIT（Weighted Blended）** | 深度に応じた重みで**加算累積**し、最後に1回合成 | **なし** | 近似 | 小（2つの RT） |

**WBOIT の仕組み**

各フラグメントについて、深度から重み `w(z)` を計算し、

```
accum += (color * a) * w
revealage *= (1 - a)
```

を**加算**（順序非依存）で累積する。最後に

```
final = accum / max(accum.a, ε) * (1 - revealage) + background * revealage
```

で合成する。加算と乗算だけなので順序に依存しない。

**近似である理由**: 重み `w(z)` で「手前のものを強く」を近似しているが、
正しい順序合成とは一致しない。濃い半透明が何層も重なると差が出る。

**本エンジンの選択（F-5、F-15）**: 3つを併用する。

| 用途 | 方式 | 深度書き込み |
| --- | --- | --- |
| 葉・金網・穴あき | **Alpha Cutout（`discard`）**。不透明パス内で処理 | **書く** |
| 一般の半透明 | 通常アルファブレンド（`SRC_ALPHA` / `INV_SRC_ALPHA`） | 書かない |
| 順序に依存させたくない半透明 | **WBOIT** | 書かない |

**Alpha Cutout を不透明パスで処理する理由**: `discard` は二択なので
**深度を書いてよく、順序に依存しない**。だから不透明として扱える。
Shadow も `alphaCutoutShadowPipelineState` で穴を空けた影を落とせる。

**アルファチャンネルだけ係数が違う理由**（F-15）

```
SrcBlendAlpha  = ONE
DestBlendAlpha = INV_SRC_ALPHA
```

これは `a_out = a_src + a_dst*(1-a_src)`、つまり「重ねた結果の不透明度」の正しい式。
色と同じ係数にすると、後段（Bloom のマスク、Final Composite）でアルファを使うときにずれる。

---

### G-19. Tone Mapping の方式 — 仕組みの違い

**目的**: HDR（1.0 を超える輝度）を、表示できる 0〜1 へ圧縮する。

| 方式 | 式 | 特性 |
| --- | --- | --- |
| **クランプ** | `min(c, 1)` | 明部が真っ白に飛ぶ（白飛び）。情報が消える |
| **Reinhard** | `c / (1 + c)` | 単純。全域がやや灰色っぽくなる。コントラストが落ちる |
| **Reinhard（白点付き）** | `c(1 + c/W²) / (1 + c)` | 白点 W を指定して、それ以上を白にする |
| **Filmic / Uncharted 2** | 多項式近似 | 暗部を持ち上げ、明部を緩やかに。フィルムらしい見た目 |
| **ACES** | 業界標準の変換曲線 | 色相の保持が良い。彩度の高い明部が破綻しにくい |

**なぜ単純なクランプでは駄目か**

太陽のような高輝度をクランプすると、RGB が別々に飽和して**色が変わる**。
例: `(5.0, 2.0, 1.0)` をクランプすると `(1, 1, 1)` になり、オレンジが白になる。
Tone Mapping 曲線を通すと、飽和する前に色相を保ちながら圧縮できる。

**Auto Exposure（自動露出）との関係**

Tone Mapping 曲線は「入力のどの輝度を中間グレーに置くか」を決めないと使えない。
暗いシーンと明るいシーンで同じ曲線を使うと、暗いシーンは真っ黒になる。

**本エンジンの Auto Exposure（R-16、R-32 の手前）**

- `Compute/HistogramExposure.CS.hlsl` で**輝度ヒストグラム**を作る。
  平均だけだと、画面の一部に極端な明部があると引っ張られる。ヒストグラムなら
  上下の外れ値を捨てて中央付近を採れる。
- 結果を **1×1 の履歴テクスチャ**へ入れ、時間方向に追従させる
  （`compositeExposureAdaptationSpeed`）。急に変わると目が痛いので、人間の暗順応を模す。
- delta time を `[1/240, 0.1]` にクランプ（`ExecuteAutoExposurePass`）。
  フレームが飛んだ直後に露出が一気に飛ぶのを防ぐ。

**処理順が重要な理由**（R-16）

Auto Exposure は **Final Composite（Tone Mapping）の直前**。
露出はトーンマップの入力なので、**トーンマップ前の HDR 輝度**から測る必要がある。
トーンマップ後に測ると、既に圧縮された値を測ることになり意味がない。

---

### G-20. Bloom の方式 — 仕組みの違い

**目的**: 明るい部分の光が周囲へにじむ現象（レンズ・眼球の散乱）を再現する。

| 方式 | 仕組み | 半径 | コスト |
| --- | --- | --- | --- |
| **閾値 + 単一ブラー** | 明部を抽出し、ガウシアンブラーを1回 | **狭い**（カーネル幅に制限） | 半径に比例して急増 |
| **多段ダウンサンプル + アップサンプル** | 段階的に縮小してブラー、段階的に拡大して加算 | **広い**（段数で決まる） | 縮小するので安い |
| **FFT 畳み込み** | 周波数空間で PSF（点広がり関数）と畳み込む | 任意 | 大。ただし任意の PSF が使える |

**多段方式が効く理由**

半径 R のガウシアンブラーは、素朴には R に比例したサンプル数が必要。
しかし**画像を 1/2 に縮小すれば、同じカーネル幅で 2 倍の実効半径**になる。
4 回縮小すれば 16 倍の半径が、同じコストで得られる。

**本エンジンの実装（R-16、AGENTS.md）**: **4回 downsample + 3回 upsample**。

アップサンプル時に各段の結果を加算していくので、
**複数のスケールの光のにじみが重なる**（近くの強いにじみ + 遠くの弱いにじみ）。
これが単一ブラーより自然に見える理由。

**Glare（Ghost / Streak / Fog Glow）との関係**（R-16）

Bloom は等方的なにじみ。実際のレンズは

- **Ghost**: レンズ内の多重反射による、光源の反対側に出る像
- **Streak**: 絞り羽根による放射状の筋
- **Fog Glow**: 広くて弱いにじみ

本エンジンは Bloom の出力（明部）を入力に、mode 2〜7 で個別に変換する。
Bloom が成功した場合だけ Glare が走る（`isQualityBloomExecuted`）。

**二重着色の問題**（本エンジンが持つ実装上の工夫）

Ghost / Streak は**シェーダ内で自分の色を焼き込み済み**。
最終合成で「ブルームの色」をもう一度掛けると二重着色になる。
だから `wasFinalGlareTextureTinted` で、最後に使われたテクスチャが着色済みかを追跡し、
着色済みなら Final Composite で色を掛けない（R-16、`ExecuteGlarePasses` の戻り値）。

**なぜトーンマップ前か**（G-19、F-3）

Bloom はリニアな輝度に対して行わないと、明部の広がり方が物理的におかしくなる。
トーンマップ後の圧縮された値でブラーすると、明るさの比率が変わっている。

---

### G-21. 水面の方式 — 仕組みの違い

| 方式 | 仕組み | 見た目 | コスト |
| --- | --- | --- | --- |
| **Sin 波の重ね合わせ** | 数本の正弦波を足す | 周期が見える。遠景で模様が繰り返す | 極小 |
| **Gerstner 波** | 頂点を円運動させる（波頭が尖る） | Sin より自然。本数分の波長のみ | 小 |
| **FFT Ocean** | 波数空間でスペクトルを与え、逆 FFT で変位場を作る | **非周期に見える**。全波長を同時に含む | 大（GPU 必須） |
| **流体シミュレーション** | Navier-Stokes を解く | 局所的な相互作用が出る | 極大 |

**FFT が Sin 波の重ね合わせと本質的に違う理由**

- Sin / Gerstner は「波を N 本足す」。波長の種類が N 個しかないので、
  最小公倍数で**周期が現れる**。遠景で同じ模様が並ぶ。
- 実際の海面は波長ごとにエネルギーが分布している（Phillips スペクトルなど）。
  FFT は**波数空間で連続的なスペクトルを与えて逆変換する**ので、
  1回の変換で全波長を同時に含む面が得られる。
- 風速・風向でスペクトルを変えるだけで、波の性質が一貫して変わる。

**なぜ GPU Compute でなければならないか**（R-17）

FFT は格子全点に対して、2次元分の変換段数（格子が N なら log N 段 × 2方向）を毎フレーム回す。
格子 512×512 でも数百万回の複素演算になる。CPU では間に合わない。

さらに出力（変位・法線・折り重なり）は**そのまま頂点シェーダとピクセルシェーダが読むテクスチャ**なので、
GPU で作って GPU で消費すれば CPU 転送が発生しない。

**本エンジンの LOD**（R-35）: `gridResolution = 2048` を仮想分割数とし、
実頂点数は**連続 LOD** で抑える。カメラ追従の LOD 中心を持つ（`oceanParams5.zw`）。

水面は1オブジェクトで画面全体を覆うので、LOD がないと遠方の1ピクセルに何百頂点も来る。

**テッセレーションを使う理由**（F-9）

本エンジンは水面だけ Hull / Domain Shader を持つ（`waterTessellationPipelineState`）。
カメラ近傍だけ動的に細分できるので、固定格子より頂点を節約できる。

**反射の使い分け**（R-17、G-15）: 水面は平面なので **Planar Reflection** が正確。
SSR は任意形状用で、水面には Planar を優先する。

---

### G-22. Skinning の方式 — 仕組みの違い

**原理**: 各頂点が複数の Bone に影響される。Bone 行列を重みで混ぜて頂点を変換する。

```
p' = Σ (weight[i] × boneMatrix[index[i]] × p)
```

本エンジンは**最大4 Bone**（`BLENDINDICES` / `BLENDWEIGHT` が float4/uint4、F-10）。

| 方式 | 実行場所 | 転送量 | 長所 | 短所 |
| --- | --- | --- | --- | --- |
| **CPU Skinning** | CPU | **変換後の全頂点**を毎フレーム Upload | 変換後の頂点を CPU で使える（当たり判定など） | 頂点数に比例した CPU 時間と転送。インスタンスごとに全頂点 |
| **Vertex Shader Skinning** | VS | **Bone 行列のみ**（数十〜数百個） | 安い。実装が単純 | 変換後の頂点を他のパスで再利用できない（パスごとに再計算） |
| **Compute Skinning** | CS | Bone 行列のみ | 変換後の頂点を**バッファへ書ける**ので、複数パスで再利用できる。レイトレの BLAS 更新にも使える | パスが1つ増える。バッファのメモリが必要 |

**本エンジンの選択（R-19）**

`SkinnedMesh.VS.hlsl` と `Skinning.CS.hlsl` の**両方が参照されている**ので、
VS Skinning と Compute Skinning を併用している。Bone 行列は StructuredBuffer で渡す。

**前フレーム Bone 行列を持つ理由**（G-17）

TAA と Motion Blur は velocity を必要とする。
スキンメッシュは Transform だけでなく Bone でも動くので、
**前フレームの Bone 行列がないと velocity が間違う**。

`previousSkinMatrixResource` / `previousSkinMatrixData`（`EditorSceneObject.h:197-198`）がこれ。
メモリが倍になる代償を払っている。

**非 Skin メッシュでも同じ Root Signature を使う工夫**

`g_identitySkinMatrixResource` に恒等行列を置き、t16 / t17 を常に有効な SRV にする
（`EditorSharedState.h:1060-1061`）。
未 Bind の SRV を読むと未定義動作になるので、恒等行列を差して Binding の切り替えを避けている。

---

### G-23. Culling の方式 — 仕組みの違い

**「描かない」を決める段は複数ある。段ごとに削れるコストが違う。**

| 段 | 何を削るか | 仕組み |
| --- | --- | --- |
| **Frustum Culling（CPU）** | **Draw Call の発行そのもの** | Bounding が視錐台の外か判定 |
| **Occlusion Culling（CPU）** | 同上 | Software ラスタライズや Portal / PVS で遮蔽を判定 |
| **GPU Frustum / Hi-Z Culling** | GPU の頂点・ピクセル処理 | Compute で判定し、Predication か ExecuteIndirect で反映 |
| **Backface Culling（HW）** | ピクセル処理 | 符号付き面積の符号（G-4） |
| **Early-Z（HW）** | ピクセル処理 | 深度テストを PS 前に（G-5） |
| **Hierarchical Z（HZB）** | 同上 | タイルごとの最大深度で三角形を早期棄却 |

**Frustum Culling の判定形状の違い**

| 形状 | 判定 | 特性 |
| --- | --- | --- |
| **Bounding Sphere** | 6平面との距離比較（6回の内積） | 最速。細長い物体で**過大評価**（余分に残る） |
| **AABB** | 8頂点 or 平面ごとの最遠点比較 | Sphere より締まる。回転すると膨らむ |
| **OBB** | 分離軸判定 | 最も締まるが判定が重い |

**本エンジンの選択（R-30）**: **AABB の8頂点をクリップ空間へ変換し、
全頂点が同じクリップ平面の外にあるものだけ除外する。**

- クリップ空間での比較なので、平面方程式を別に作らず ViewProjection だけで済む（G-1）。
- 「全頂点が同じ平面の外」は**偽陽性を出さない**（消してはいけないものを消さない）。
  偽陰性（消せるのに残る）は出るが、残しても描画結果は正しい。

**Hi-Z Occlusion Culling の仕組み**

1. 深度バッファから **Depth Pyramid**（Mip チェーン）を作る。各 Mip は「その領域の最遠深度」。
2. オブジェクトの画面上のバウンディング矩形を求める。
3. その矩形サイズに合う Mip レベルを選び、数テクセル読む。
4. オブジェクトの最近深度が、その領域の最遠深度より遠ければ**完全に隠れている**。

矩形サイズに合う Mip を選ぶので、**大きなオブジェクトでも数テクセルの読みで判定できる**。

**なぜ CPU へ Readback しないか**（R-9、F-7）

Readback すると GPU の完了を待つ必要があり、パイプラインが1回止まる。
Predication（`SetPredication`）と ExecuteIndirect なら、判定も反映も GPU 内に閉じる。

**代償**: 判定結果が**次フレーム**に使われる（1フレーム遅れ）。
カメラが速く動くと、出るべきものが1フレーム消える可能性がある。

**なぜ CPU 段も必要か**

GPU Culling は「積んだ上でスキップする」。**CommandList へ書き込むコストは残る**。
Draw Call の発行そのものを削るには CPU 側の判定が必要。
だから本エンジンは2段構え（R-20、R-30）。

**`kGpuCullingMinimumObjectCount = 256`**（G-10）: オブジェクトが少ないと
GPU Culling 自体の Dispatch / Barrier コストで元が取れない。

---

### G-24. Physics の仕組み — 段と方式の違い

**1ステップの流れ**

```
Broad Phase (大まかな衝突候補の列挙)
  → Narrow Phase (実際の接触点の計算)
  → Constraint Solver (接触・関節の解決)
  → Integration (速度と位置の更新)
```

**Broad Phase の方式**

| 方式 | 仕組み | 特性 |
| --- | --- | --- |
| **総当たり** | 全ペアを調べる | O(n²)。数十個まで |
| **Sweep and Prune** | 1軸へ投影して区間の重なりを調べる。フレーム間で順序が変わりにくいので増分更新できる | 動きが少ないシーンで高速 |
| **Uniform Grid** | 空間を格子に切り、同じセルの物体だけ調べる | 物体サイズが揃っていれば高速。サイズ差が大きいと破綻 |
| **BVH / AABB Tree** | 階層的なバウンディングボリューム | サイズ差に強い。動的な再構築コストがある |

**Narrow Phase**: 形状ペアごとの専用アルゴリズム。
球-球は距離比較、凸多面体同士は **GJK / EPA**（分離軸を反復的に求める）など。

**Constraint Solver の方式**

| 方式 | 仕組み | 特性 |
| --- | --- | --- |
| **Impulse（逐次インパルス）** | 制約ごとに速度を補正するインパルスを反復適用 | 実装が単純。反復数で精度が決まる。積み上げが沈む |
| **Sequential Impulse + Warm Starting** | 前フレームのインパルスを初期値に使う | 収束が速い。現代の主流 |
| **Position Based Dynamics** | 位置を直接補正する | 安定。剛体の物理的正確さは落ちる |
| **Featherstone / 解析法** | 関節系を解析的に解く | ロボティクス向け。正確だがコスト大 |

**Fixed Time Step がなぜ必要か**

可変刻みで積分すると、**同じ入力でも結果が変わる**。

- 反発・摩擦・拘束の収束が刻み幅に依存する。
- 60fps で動いた挙動が 144fps で変わる。
- リプレイ・ネットワーク同期が成立しない。

だから物理は固定刻みで回し、描画フレームレートと切り離す。

**本エンジンの選択（R-7、R-3）**

- **Jolt Physics 5.5.0**（`EditorJoltPhysicsManager.cpp` に `JPH::` が 455箇所）
- 固定刻み **1/60 秒**（`EditorSceneLifecycleManager.cpp:83`、`EditorScene.cpp:730`）
- 破壊は **NvBlast**（PhysX 系）、浮力など独自処理は `EditorPhysicsManager.cpp`（4,764行、`JPH::` は 0箇所）

**自作しない理由（推定）**: Broad Phase の空間分割と拘束ソルバの安定化は、
「動くもの」を作るのと「積み上げても沈まないもの」を作るのの差が大きい。
Jolt は決定性と並列性の設計が明示されている。

**本エンジンの弱点**（R-3、R-7）

- **catch-up が無い**。1フレームに複数ステップ進める処理がないので、
  重い描画でフレームが落ちると**物理の進みが実時間より遅れる**。
  一般的な実装は「累積時間が刻みを超えた分だけ、上限回数までループする」。
- 剛体 = Jolt、破壊 = NvBlast、浮力等 = 自作の**3系統**に分かれている。
  ファイル名から担当が読めない（`EditorPhysicsManager` が Jolt を使っていない）。

---

### G-25. グラフィックス API の仕組みの違い

| | OpenGL | D3D11 | D3D12 / Vulkan |
| --- | --- | --- | --- |
| **モデル** | グローバルな状態機械 | 状態機械（型付き） | **明示的（explicit）** |
| **状態設定** | `glBindTexture` 等でグローバル状態を変更 | `context->PSSetShaderResources` | **PSO と Descriptor Table で事前に固める** |
| **コマンド** | 即時実行（見た目上） | Deferred Context で一部並列化可 | **CommandList を明示的に記録・提出** |
| **同期** | ドライバが暗黙に処理 | ドライバが暗黙に処理 | **Fence / Barrier を自分で書く** |
| **メモリ** | ドライバ管理 | ドライバ管理 | **Heap を自分で確保・配置** |
| **リソース状態** | 暗黙 | 暗黙 | **Resource Barrier で明示的に遷移** |
| **マルチスレッド** | 実質不可（コンテキストが1つ） | 限定的 | **CommandList をスレッドごとに記録できる** |

**「状態機械」と「明示的」の違いが意味すること**

OpenGL は

```c
glBindTexture(GL_TEXTURE_2D, tex);
glUseProgram(prog);
glDrawArrays(...);
```

のように、**グローバルな「現在の状態」を変更してから描く**。
どの状態が設定されているかはコードを追わないと分からず、
ドライバは Draw の直前に「この組み合わせで動くか」を検証し、必要なら内部でコンパイルする。

D3D12 / Vulkan は組み合わせを **PSO として事前に固める**（F-13）。
Draw 時の検証とコンパイルが消え、**フレーム時間が予測可能になる**。
代償は「組み合わせの数だけ PSO を作る」「Barrier を自分で書く」「同期を自分で管理する」。

**Resource Barrier を自分で書く理由**

GPU は複数の作業を並行・順不同で実行する。
「RenderTarget へ書いた結果を、次のパスで Texture として読む」とき、
**書き込みの完了と、キャッシュのフラッシュ、レイアウトの変換**が必要。

- OpenGL / D3D11: ドライバがリソースの使われ方を追跡し、暗黙に挿入する。安全だが保守的で、
  必要のない同期も入る。
- D3D12 / Vulkan: **アプリが `ResourceBarrier` で明示する**。必要な箇所だけ同期できるので速いが、
  書き忘れると**動く GPU と動かない GPU が出る**（未定義動作）。

本エンジンはポストプロセスの各 Pass が自分で
`PIXEL_SHADER_RESOURCE` → `RENDER_TARGET` → 描画 → `PIXEL_SHADER_RESOURCE` を行う（F-4）。

**Descriptor の仕組みの違い**

- OpenGL: テクスチャユニット（`GL_TEXTURE0` 〜）に**番号でバインド**する。
- D3D11: シェーダステージごとのスロット（`t0`, `t1`, ...）にバインドする。
- D3D12: **Descriptor Heap**（GPU 上のメモリ）に Descriptor を書き、
  Root Signature が「Heap のどこからどれだけ使うか」を定義する。
  Draw 時は Descriptor Table の先頭ハンドルを渡すだけ。

本エンジンは Heap のインデックスを定数で固定している（`kRuntimeDepthSrvDescriptorIndex` 等、F-4）。
動的アロケータを持たない代わりに、どのスロットが何かがコードで読める。
代償は「スロットを増やすと定数を全部見直す」。

**Bindless という発展形**: Descriptor Heap 全体をシェーダから
インデックスで直接引く方式。Descriptor Table の切り替えが不要になる。本エンジンは未使用（G-10）。

---

### G-26. 規約の違い — 移植で壊れるところ

**同じ 3D なのに API で違う規約。混ぜると壊れる。**

| 項目 | OpenGL | Direct3D | 本エンジン |
| --- | --- | --- | --- |
| **座標系** | 右手系（+Z が手前） | 左手系（+Z が奥） | **左手系**（F-1） |
| **NDC の深度範囲** | `[-1, 1]`（GL 4.5 以降 `glClipControl` で `[0,1]` 可） | `[0, 1]` | **`[0, 1]`**（F-1） |
| **NDC の Y 方向** | 上が +1 | 上が +1 | 同じ |
| **テクスチャの原点** | **左下**（UV の v が下から上） | **左上**（v が上から下） | **左上**（D3D 準拠） |
| **行列の格納** | 列優先（column-major） | 行優先（row-major） | **行優先**（F-1） |
| **ベクトルの扱い** | 列ベクトル（`M * v`） | 行ベクトル（`v * M`） | **行ベクトル**（F-1） |
| **合成の順序** | `P * V * M * v` | `v * M * V * P` | **`v * M * V * P`**（F-1） |
| **表面の巻き方向** | 既定 CCW | 既定 CW | **CW**（`FrontCounterClockwise = FALSE`、F-14） |
| **クリップ空間の Z** | `-w ≤ z ≤ w` | `0 ≤ z ≤ w` | D3D 準拠 |

**「列優先 / 行優先」と「列ベクトル / 行ベクトル」は別の話**

- **格納順**（memory layout）: 16 個の float をどう並べるか。
- **ベクトル規約**: `M * v` か `v * M` か。

この2つは独立に選べるが、**組み合わせによって「同じ数式が同じ結果になる」**。
GL の（列優先 + 列ベクトル）と D3D の（行優先 + 行ベクトル）は、
**メモリ上のバイト列が同一**になる。だから多くのライブラリが両方で動く。

ただし**掛ける順序はコード上で逆になる**。ここを間違えると、
平行移動と回転の順序が入れ替わった変換になる（オブジェクトが原点を中心に公転する等）。

**HLSL の `row_major` 指定**（F-17）

HLSL の既定は**列優先**。C++ 側が行優先なら、明示しないと転置された行列が使われる。
本エンジンは `row_major float4x4 World;`（`Object3d.VS.hlsl:4`）と明示しているので、
C++ 側で転置して送る必要がない。

明示しない実装では、C++ 側で `transpose()` してから送るのが定石。
**どちらの流儀か分からないまま触ると必ず壊れる箇所。**

**テクスチャ原点の違いが引き起こす典型的な不具合**

GL 用に作った UV を D3D でそのまま使うと、**テクスチャが上下反転する**。
対処は (1) インポート時に v を反転、(2) シェーダで `1.0 - v`、(3) テクスチャ自体を反転。
どれを選んでも良いが、**混在させると一部だけ反転する**という最悪の状態になる。

**深度範囲の違い**

GL の `[-1, 1]` 前提の投影行列を D3D で使うと、
**near 付近の物体が消える**（z < 0 がクリップされる）。
逆に D3D の行列を GL で使うと、深度の半分しか使われず精度が落ちる。

**Vulkan は D3D 寄り**: 深度 `[0, 1]`、ただし **NDC の Y が下向き**（GL と逆）。
GL から Vulkan への移植で最も踏むのがこれ。

---

### G-27. Component システムの方式 — 仕組みの違い

**「GameObject に機能を付ける」の実装方式は大きく3つある。**

| 方式 | データ配置 | Update の回し方 | 型安全性 | 例 |
| --- | --- | --- | --- | --- |
| **継承中心** | 派生クラスのインスタンス | 基底の `virtual Update()` を呼ぶ | **高い**（型が機能を表す） | 初期の cocos2d、多くの学習用エンジン |
| **多態 Component** | `vector<unique_ptr<Component>>` | 各 Component の `virtual Update()` | 高い | Unity（利用者から見た形） |
| **ECS** | Component 型ごとに密な配列。Entity は ID のみ | System が型ごとの配列を走査 | 高い | Unity DOTS、Bevy、EnTT |
| **データ集約（fat struct + type tag）** | 1つの struct が全 Field を持ち、`type` で解釈を切り替え | Manager が配列を走査し、自分の担当 `type` だけ処理 | **低い** | **本エンジン**（R-1） |

**継承中心の何が問題か**

`Player : Character : GameObject` のような階層を作ると、
「飛べる Player」「泳げる Enemy」を足すたびに階層が破綻する（**菱形継承問題**）。
機能の組み合わせが階層で表現できない。だから Component 方式が生まれた。

**多態 Component の仕組みとコスト**

```cpp
for (auto& component : components) {
    component->Update();   // 仮想関数呼び出し
}
```

- **仮想関数呼び出しは間接ジャンプ**。呼び先が毎回変わるので分岐予測が外れる。
- Component が個別に `new` されるので**メモリが散る**。キャッシュミスが増える。
- Component 型ごとに Inspector 表示・Serialize・Undo・ネットワーク差分を実装する必要がある。

**ECS の仕組みと、なぜ速いか**

```
Position 配列: [p0][p1][p2][p3]...   ← 密に並ぶ
Velocity 配列: [v0][v1][v2][v3]...
System: for (i) { position[i] += velocity[i] * dt; }
```

- 同じ型が**連続メモリに並ぶ**ので、キャッシュラインを無駄なく使える。
- 仮想関数がない。ループが SIMD 化できる。
- 代償: 「任意 Component の任意 Field を実行中に編集して即反映」という Editor の要求に対して、
  **型ごとの Storage と型消去の仕組みを自作する必要がある**。
  Archetype の変更（Component の追加・削除）がコピーを伴う。

**本エンジンの方式（fat struct + type tag）の仕組み**

```cpp
struct EditorComponent {
    EditorComponentType type;     // 289 種類
    // 以下、全種類の Field が並ぶ（トップレベル 1,596 個）
    Vector3 color; float metallic; float roughness; ...
    int32_t prefabSpawnerMode; float prefabSpawnerInterval; ...
    Vector3 wireRendererColor; ...
};
std::vector<EditorComponent> components;   // GameObject が持つ
```

**なぜこれを選んだか（推定、R-1）**

- Inspector・Serialize・Undo・共同編集・Script API の**5系統すべてが同じ1つのデータ形を扱える**。
  Component 型が増えても、この5系統に新しいコードが要らない。
- Field 追加が「struct へ1行 + `CreateComponent` へ1行 + Inspector へ1行」で済む。
  新しいクラス・ファクトリ・登録処理が不要。
- **POD 中心なのでコピーが安い。** これが Undo を「Scene 丸ごとコピー」にできる理由（R-32）。
  多態 Component だと deep copy に仮想関数（`Clone()`）が必要になる。
- Text へそのまま書き出せて、Field 単位で差分比較できる。
  **共同編集の Property 単位 Revision（R-11）がこれに依存している。**

**代償（R-1 の弱点）**

- **どの Component 型でも全 Field 分のメモリを占める。** Transform だけの GameObject も 1,596 Field 分。
- `type` に応じた Field の有効・無効が**型で表現されない**。無関係な Field を読み書きしても
  コンパイラが止めない。
- Component 間依存（Collider が RigidBody を要求）は型ではなく Manager の実行時チェック。
- 既定値が宣言から約 2,000 行離れた別ファイル（`CreateComponent`）にある。

**この設計判断の評価**

Editor を持つエンジンとしては**筋が通っている**。
ECS の利点（キャッシュ効率）は「大量の同種オブジェクトを毎フレーム更新する」場面で出るが、
Editor の主な負荷は描画と UI であって Component の走査ではない。

一方、メモリと型安全性は明確に犠牲になっている。
「Runtime の規模が大きくなったら ECS 的な Storage へ移す」という段階的な道は残っている。

---

### G-28. Scene の直列化方式 — 仕組みの違い

| 方式 | 仕組み | 人が読めるか | diff / マージ | サイズ | 版互換 |
| --- | --- | --- | --- | --- | --- |
| **Binary（独自）** | struct をそのまま書く | ✗ | **不可** | 最小 | 版番号ヘッダで分岐 |
| **JSON / YAML** | キーと値の木構造 | ○ | キー単位なら可。行単位マージは効きにくい | 大（キー文字列） | 未知キーを無視できる |
| **行単位・列位置依存 Text**（本エンジン） | 1行 = 1エンティティ。列の位置で意味が決まる | ○ | **行単位マージがそのまま効く** | 中 | 列数で版判定 |
| **key=value 行** | 1行 = 1プロパティ | ◎ | 効く | 大 | 未知キーを保持できる |

**本エンジンの方式（R-2）**

```
（GameObject 行）（Component 行）...
if (elements.size() >= 13u) { ... }   ← 要素数で版を判定
```

新 Field は**既存の列位置を変えず末尾へ追加**、または `*Extension` 行として別行で追記。

**なぜ Binary を採らなかったか（推定）**

- **共同制作で diff が読めない。** 衝突箇所を目で確認できない。
- Git の行単位マージが効かない。1バイト違うだけで全体がコンフリクトする。
- Engine の版が違う相手の Scene を開いたとき、壊れた場所を特定できない。

**なぜ JSON を採らなかったか（推定）**

- Field が 1,596 個 × Component 数だけキー文字列が並ぶ。サイズが膨らむ。
- JSON は階層構造なので、1つのオブジェクトが1行に潰れるか、階層が深くなる。
  **どちらも行単位マージと相性が悪い。**

**列位置依存の代償（R-2 の弱点）**

- **列の挿入・並べ替えができない。** フォーマット変更のコストが極端に高い。
- 版判定が `elements.size()` の閾値比較なので、版が増えるほど分岐が増える。
  `LoadScene` が 3,721 行ある主因。
- スキーマが型として存在しない。`SaveScene` と `LoadScene` の列順が**人手で一致させる約束**。

**安定性のための工夫（実装済み、R-2）**

| 工夫 | 何を防ぐか |
| --- | --- |
| 一時ファイル + rename | **保存失敗で既存 Scene を壊さない**。書き込み中のクラッシュでも元が残る |
| **未知の行を保持** | 新版で追加された行を旧版で開いて再保存しても消えない。版が混在するチームで必須 |
| Play 中の保存/読込ガード | Runtime が状態を持っている最中の上書きを防ぐ |
| UUID で識別 | 表示名や配列位置が変わっても参照が壊れない |

**「未知の行を保持する」の重要性**

これが無いと、**新版で作った Scene を旧版の人が開いて保存した瞬間に、新機能のデータが消える**。
チーム開発で Engine の版が揃わない状況では、これが最も被害の大きい不具合になる。

**UUID を使う理由**（R-1、R-11）

- 配列位置で参照すると、GameObject を1つ削除した時点で以降の参照が全部ずれる。
- 表示名で参照すると、リネームで壊れる。同名が複数あると曖昧。
- UUID なら**移動・リネーム・並べ替えに耐える**。
  共同編集で「誰の変更がどの対象か」を識別するのにも同じ ID を使える。

---

### G-29. 共同編集の同期方式 — 仕組みの違い

| 方式 | 仕組み | 衝突粒度 | 実装量 | 適する対象 |
| --- | --- | --- | --- | --- |
| **ファイル同期（Git / rsync 的）** | ファイル単位で送り、衝突はファイル単位 | **ファイル全体** | 小 | ソースコード |
| **ロック（排他）** | 編集中のファイルをロックして他者を止める | ファイル全体 | 小 | バイナリアセット（Perforce の運用） |
| **OT（Operational Transformation）** | 操作を送り、同時操作を「変換」して順序を揃える | 文字単位 | **大** | テキスト共同編集（Google Docs） |
| **CRDT** | 数学的に可換なデータ構造を使い、順序に依存せず収束させる | 要素単位 | **大** | 分散テキスト、オフライン編集 |
| **Revision + Property 差分**（本エンジン） | 単調増加の Revision 番号と、Property ごとの最終 Revision | **Property 単位** | 中 | 構造化データ（Scene） |

**本エンジンの仕組み（R-11、R-34）**

```
baseRevision / revision            変更が「どの版に対する変更か」
currentRevision_                   自分のローカル版
lastSyncedRevision_                サーバへ送信済みの版
serverRevision_                    サーバが受理済みの版
lastPropertyRevision_[propertyKey] Property ごとの最終 Revision  ← 競合検出の粒度
```

- 通信は **TCP**（`TcpCollaborationTransport.cpp`、別スレッド）。
- **ChangeLog** を永続化し、再接続時に差分から復帰（`LoadChangeLog()`）。
- **Snapshot**（Scene 全体の Text）を、新規参加・再接続したユーザーへ**宛先を絞って**送る
  （`QueueCurrentSceneSnapshotForUser`）。以降はその Snapshot を基点に差分で進む。
- Play 中に届いた他者の変更は、**通信と Revision 確定は継続しつつ適用を保留**する。

**なぜファイル丸ごと同期を採らなかったか**

Scene ファイルは1つ。2人が**別々の GameObject を編集しただけでも衝突する**。
Property 単位なら同じ Scene の別箇所を同時に編集できる。これが最大の理由。

**なぜ OT / CRDT を採らなかったか（推定）**

- OT / CRDT はテキストの**文字挿入位置のずれ**を解決する仕組み。
  Scene は「この GameObject のこの Property を 3.5 にする」という**上書き操作**が主。
  上書きは可換ではないが、「最後の書き込みが勝つ（LWW）」で実務上足りる。
- Revision 番号1つで「順序」と「どこまで同期したか」が表せる。
  ベクタークロックや CRDT のメタデータより実装が小さい。

**なぜ TCP で、UDP ではないのか**（R-11）

| | TCP | UDP |
| --- | --- | --- |
| 到達保証 | あり | なし |
| 順序保証 | あり | なし |
| 再送 | 自動 | 自前実装 |
| 遅延 | 再送待ちで増える | 最小 |
| 適する用途 | **取りこぼしが許されないもの** | 最新値だけ届けばよいもの |

Scene の変更は**累積的**。「位置を +1 する」「Component を追加する」が1つ落ちると、
**全員の Scene が食い違って戻せない**。

一方、対戦ゲームの位置同期は「最新の位置だけ届けばよい」ので、
古いパケットを捨てられる UDP が有利。**用途が逆。**

だから編集操作は TCP が正しい選択。

**Snapshot と差分の役割分担（G-28 の Text 形式が効いている）**

- 差分だけで同期すると、参加した瞬間に**全履歴を再生**する必要がある。
  ChangeLog が長いほど参加コストが増える。
- Snapshot があれば定数時間で土台が揃う。
- Scene が Text 形式（R-2）なので、**Snapshot は保存形式そのままで作れる**。
  専用の直列化を持たなくてよい。

**容量対策（実装済み、R-34）**: ハッシュ比較で同一内容なら送らない、
時間基準で頻度を抑える、宛先を絞る。

**弱点**

- Play 中の保留分が積む（Play が長いと増える）。
- `EditorTeamCollaborationManager::Draw` が 1,362行（filesystem 6回・map find 21回・sort 2回）。
  Window を開いている間は毎フレームこのコストを払う。
- `Update` へ渡す delta time が `1/60` 固定（R-3 の弱点と同じ）。

---

### G-30. Script のホスト方式 — 仕組みの違い

| 方式 | 仕組み | 実行速度 | 反復速度 | ABI の安定性 |
| --- | --- | --- | --- | --- |
| **エンジンに直接コンパイル** | ゲームコードをエンジンと一緒にビルド | 最速 | **遅い**（全体再ビルド） | 問題にならない |
| **C++ DLL + 関数ポインタテーブル**（本エンジン） | ゲームコードを DLL にし、エンジンが API テーブルを渡す | 最速 | DLL だけ再ビルド | **テーブルで隔離できる** |
| **スクリプト言語（Lua / Python）** | VM がバイトコードを実行 | 遅い（10〜100倍） | **最速**（再起動不要） | バインディングで隔離 |
| **C# / マネージド** | JIT / AOT。GC あり | 中（C++ の 1〜2 倍） | 速い | リフレクションで柔軟 |
| **ビジュアルスクリプト** | ノードグラフを解釈 | 遅い | 最速 | データなので安定 |

**本エンジンの方式（R-23）**

- `Source/Engine/Core/EditorScriptApi.h` に公開 API を定義。`kEditorScriptApiVersion` = **15**。
- `EditorScriptManager::BuildRuntimeApi()`（417行）が**関数ポインタテーブル**を組む。
- Script は **DLL** としてロード（`LoadModule`、173行）。
- 利用者向け C++ クラス 79件、Runtime API **415 Entry**、Script Template 26件。

**なぜ関数ポインタテーブルにするのか**

エンジン内部クラス（`EditorScene` など）を直接公開すると、
**内部の struct レイアウトが Game 側の ABI になる**。
`EditorComponent` に Field を1つ足しただけで、既存の Game DLL が壊れる（サイズが変わる）。

テーブル経由なら、エンジンは**テーブルの末尾へ関数を足すだけ**。
既存 Script は再コンパイル不要で動き続ける。
だから規約が「`EditorScriptApi.h` の末尾へ追加し、版番号を上げる」になっている。

**DLL にする理由**: Script だけ再ビルドして Play し直せる。
エンジン全体（163,000 行）の再ビルドを待たない。

**なぜ Lua / C# を採らなかったか（推定）**

- 学習・ポートフォリオ用途で「C++ で書ける」ことを保ちたい。
  スクリプト言語を入れると、利用者が2つの言語を覚える必要がある。
- VM / ランタイムの同梱と、バインディングの自動生成の実装コスト。
- C++ DLL なら**デバッガがそのまま使える**（ブレークポイント、ステップ実行）。

**安全性の担保（実装済み、R-23）**

全 Wrapper が「Runtime API 未接続」「対象が存在しない」の**両方で `false` を返してクラッシュしない**ことを
`Tests/CameraAudioRendererVfxApiSmoke.cpp` で自動確認。`RunNativeSmokeTests.ps1` で実行でき、現在 PASS。

**これが重要な理由**: Script は利用者が書くコードから呼ばれる。
無効なハンドルを渡されたときにクラッシュすると、**エンジンのバグとして報告される**。
API 境界での防御が、サポートコストを直接下げる。

**弱点（R-23）**

- 末尾追加しかできないので、**API の削除・シグネチャ変更が事実上できない**。古い API が残り続ける。
- 415 Entry が1つのテーブル。分野ごとの分割がない。
- DLL の Hot Reload（Play 中の差し替え）は持たない。

---

### G-31. Profiler の計測方式 — 仕組みの違い

| 方式 | 仕組み | オーバーヘッド | 得られるもの |
| --- | --- | --- | --- |
| **サンプリング** | 一定間隔でコールスタックを取る | **小**（コード変更不要） | 統計的な分布。短い関数は取りこぼす |
| **インストルメンテーション（計装）** | 測りたい範囲にコードを埋める | 中（範囲の数に比例） | **正確な範囲ごとの時間**。呼び出し階層 |
| **GPU Timestamp Query** | CommandList に「時刻を書け」という命令を挟む | 小 | **GPU 側の実時間**。Pass 単位 |
| **PIX / Nsight（外部）** | ドライバレベルでキャプチャ | 大（キャプチャ時） | 最も詳細。Wave 占有率、命令単位 |

**CPU 時間と GPU 時間を分けて測る必要がある理由**

- CPU 時間は「CommandList を積むのにかかった時間」。
- GPU 時間は「GPU がそれを実行するのにかかった時間」。

**この2つは全く別**。CPU が 2ms で積んだコマンドを GPU が 20ms 実行することもある。
CPU だけ測って「速い」と判断すると、GPU 律速を見逃す。

GPU は非同期に動くので、CPU 側の `QueryPerformanceCounter` では GPU の時間を測れない。
**Timestamp Query**（CommandList に時刻記録命令を挟み、後でバッファから読む）が必要。

**本エンジンの実装（R-10）**

| 測るもの | 方法 |
| --- | --- |
| CPU 時間 | `EditorProfilerManager::Scope`（RAII）。入れ子を `callPath`（`親 > 子`）で持つ |
| GPU 時間 | `BeginGpuEvent` / `EndGpuEvent` → Timestamp Query Heap → Readback |
| 確保量 | **グローバル `operator new` を置き換え**、Scope の開始・終了でスナップショット差分 |
| Draw Call | `RecordEditorProfilerDrawCall()` を発行箇所に置いて数える |

**確保量を同じ Scope で測る意味**

CPU 時間だけでは「速いが確保が多い」処理を見つけられない。
C++ に GC はないが、**フレーム内の確保スパイクは確実にコストになる**
（アロケータのロック、ページフォルト、キャッシュ汚染）。

同じ Scope で時間と確保量を並べると、
「時間が大きく確保も大きい」→ バッファ再利用で直る、と判断できる。

**なぜ `operator new` の置き換えにしたか**

自前アロケータ経由だけを測ると、**`std::string` / `std::vector` / ThirdParty の確保が漏れる**。
実際のフレーム内の確保の大半はこれら。グローバル置き換えなら全部通る。

**この方式の危険と、本エンジンが踏んだ罠**（F-8、R-10）

- `operator new` の置き換えは**20種の確保関数を全部揃えないと危険**。
  1つ欠けるとその形だけ CRT 側が使われ、`malloc` 系と `_aligned_malloc` 系が混ざる。
  `free` と `_aligned_free` は互換でないので **Heap 破壊**になる。
  → aligned かつ nothrow の4種が欠けていた（16/20）。2026-09-29 に 20/20 へ補完。
- 計測 OFF 時も、Engine 全体の `new` が毎回**別 TU の非 inline 関数**を呼んでいた。
  → 判定を Header へ移して inline 展開。OFF 時は relaxed load 1回と分岐だけに。

**計測自体のコスト（残る弱点）**

`Scope` が `std::string` を4つ持つ（`sampleName_` / `sampleSource_` / `parentPath_` / `callPath_`）。
計測 ON 時は Scope ごとに文字列連結（`親 > 子`）が走る。
**計測の負荷が測定対象へ乗る**（観測者効果）。
改善するなら、文字列ポインタと ID で持ち、表示時だけ連結する。

**ボトルネック特定の手順（R-10）**

1. `callPath` を降順に見て CPU 時間の支配項を特定。
2. 同じ Scope の確保量を見る。両方大きければ確保の除去が先。
3. GPU 時間が支配的なら Pass 名で切り分け。Draw Call 数と Dispatch 数を併読して、
   **発行回数律速か帯域律速か**を分ける。
4. 「描画バッファ」タブで各 RenderTarget を目視し、期待しない Pass が動いていないか確認。

**GPU 律速の種類を見分ける観点**

| 症状 | 原因の候補 |
| --- | --- |
| 解像度を下げると劇的に速くなる | **ピクセル律速**（帯域 or Pixel Shader） |
| 解像度に鈍感、オブジェクト数に敏感 | **頂点律速 or Draw Call 律速** |
| RenderTarget フォーマットを小さくすると速くなる | **帯域律速** |
| シェーダの命令を削ると速くなる | **演算律速** |

---

### G-32. GPU 破片と CPU 破片 — なぜ分けるのか

**破壊表現の2つの実装（R-18）**

| | CPU 破片 | GPU 破片 |
| --- | --- | --- |
| 実体 | **GameObject として生成** | GameObject にしない |
| 描画 | 通常の SceneObject として | **Instance Buffer で描画のみ** |
| 物理 | Jolt の剛体を持つ | 持たない（または簡易） |
| 個別操作 | **できる**（参照・移動・当たり判定） | **できない** |
| 数 | 少数（重要な破片） | 数百〜数千 |

**なぜ GPU 破片を GameObject 化しないか（R-18）**

1回の破壊で数百〜数千の破片が出る。それぞれを GameObject にすると、

- `std::vector<EditorGameObject>` に数千要素が追加される。
  `EditorComponent` が Field 1,596 個（R-1）なので**メモリが爆発する**。
- UUID の発行、Inspector の Hierarchy 表示、**Undo のスナップショット**（R-32 は Scene 丸ごとコピー）、
  共同編集の Property 差分、これら全部に数千個分が乗る。
- 破片は**生成されて飛んで消えるだけ**で、後から参照する必要がない。

つまり「GameObject であることの利点を1つも使わないのに、コストを全部払う」状態になる。
だから Instance Buffer への書き込みだけで済ませる（撃ちっぱなし）。

**これは R-1 の設計（fat struct）の帰結**

もし Component が ECS 的な密配列（G-27）なら、破片を Entity にするコストは小さかった。
fat struct なので 1 Entity のコストが高く、**大量生成に向かない**。
設計の選択が、別の場所の設計を決めている例。

**Physics を適用すると何が重くなるか**

破片を剛体にすると、Broad Phase（G-24）の対象が数千増える。

- Broad Phase は O(n log n) 程度でも、n が 3桁増えると効く。
- 破片同士の接触が大量に発生し、Narrow Phase と Solver の反復が急増する。
- 積み重なった破片は収束が遅く、**反復数を増やさないと沈む**。

だから GPU 破片は物理を持たず、簡易な軌道（放物運動など）で飛ばす。

**制約（そのまま弱点）**

- GPU 破片は個別に操作・参照できない。特定の破片を拾う、当たり判定を取るができない。
- Instance Buffer の容量が上限（`kMaxParticleCount` 相当）。超過分は捨てる。
- 破壊は NvBlast、剛体は Jolt なので**2つの SDK が並存**（R-7）。

**検証**: `Tests/BlastLowLevelSmoke.cpp` が低レベル API で
「1回の破壊で 2 Actor に分割される」ことを確認（`RunNativeSmokeTests.ps1` で PASS）。

---

### G-33. Editor と Runtime の分離方式 — 仕組みの違い

| 方式 | 仕組み | Play の即応性 | 配布物 | 安全性 |
| --- | --- | --- | --- | --- |
| **同一プロセス・フラグ切り替え**（本エンジン） | `isStandaloneGame_` と `IsPlaying()` で経路を分ける | **最速**（メモリを直接共有） | Editor の状態も載る | Play 中のバグが Editor を落とす |
| **同一プロセス・別 World** | Editor World と Play World を別インスタンスで持つ | 速い | 分離しやすい | Play 中の破壊が Editor に及びにくい |
| **別プロセス** | Play を子プロセスで起動し、IPC で通信 | **遅い**（IPC 越し） | 完全に分離 | Play がクラッシュしても Editor は生きる |
| **別ビルド** | Editor ビルドと Game ビルドを分ける（`#if WITH_EDITOR`） | — | **Editor コードが入らない** | — |

**本エンジンの方式（R-21）**

- `isStandaloneGame_` が true のとき、`GameScene::Update` / `Draw` は Editor UI を全部飛ばす（early return）。
- Play Mode は `EditorRuntimeManager::IsPlaying()`。Play 中だけ `EditorSceneLifecycleManager` が
  Physics / Script / Input Component を回す。
- Scene View と Game View は別 Viewport・別 Camera 行列。**Temporal も別々の履歴**を持つ
  （両 Viewport を処理するフレームでもカメラ履歴を混ぜない）。

**なぜ同一プロセスか（推定）**

- 別プロセスにすると、**Play 中の値を Inspector で編集して即反映する経路が IPC になる**。
  編集の即応性（触った瞬間に見た目が変わる）を優先している。
- 配布 Game は同じ実行体を `isStandaloneGame_` で動かすので、
  「Editor で見た通りに動く」経路が1本で済む。**挙動の差異が生まれない。**

**代償（R-21 の弱点）**

- Editor 専用の状態と Runtime の状態が同じ `EditorSharedState`（可変グローバル 332個）に同居する。
  **配布 Game に Editor 用のグローバルが載る。**
- Play 中のクラッシュが Editor を落とす。編集中のデータが失われる
  （Undo 上限 64 と自動保存が緩和策）。
- Play 中の Scene 保存/読込はガードで禁止（安全側だが機能制限）。

**Temporal の履歴を Viewport ごとに分ける理由（G-17 と関連）**

TAA は前フレームの履歴を velocity で引き当てる。
Scene View と Game View は**カメラが違う**ので、履歴を共有すると
「別のカメラの画像を前フレームとして参照する」ことになり、画面が崩れる。
だから Temporal Manager が Viewport ごとに History / Previous Depth / Write Index を持つ。

**`#if WITH_EDITOR` 的な分離をしていない影響**

配布バイナリに Editor のコード（ImGui、Inspector、共同編集など）が含まれる。
バイナリサイズと起動時の初期化が増える。
分離するには `EditorSharedState` の Editor 部分と Runtime 部分を切る作業が必要で、
これは R-1 / R-24 の「状態の所有者を分ける」作業と同じ根を持つ。

---

### G-34. Asset 管理の方式 — 仕組みの違い

| 方式 | 参照の持ち方 | 移動・Rename | 解放 |
| --- | --- | --- | --- |
| **パス直参照** | 文字列パス | **壊れる** | 明示 |
| **GUID / 永続 ID**（本エンジン） | ID を正、パスを Fallback | **耐える** | 明示 |
| **`shared_ptr` 参照カウント** | ハンドルの所有 | ID と併用が普通 | **自動**（最後の参照が消えたら） |
| **世代別 GC** | 弱参照 + マーク | — | 自動（停止時間が出る） |
| **LRU キャッシュ** | ID + 使用時刻 | — | 自動（容量超過で古いものから） |

**本エンジンの仕組み（R-8、R-31）**

- Component は `assetPath` と `assetId` を**両方**持つ。
  **`assetId` があれば移動・Rename 後もそちらを優先してパスを解決する**。
- `AssetManager` が `std::unordered_map<std::string, HashCacheEntry> hashCache_` を持ち、
  **ファイル内容のハッシュ**で再読込の必要を判定する。
- `AssetRegistry` が永続 ID と `NotLoaded` 等の状態を持つ。
- **参照カウントは無い。** `Unload(path)` の明示呼び出しのみ。
- **非同期ロードも無い。**

**なぜ ID を正にするのか**

パスだけで参照すると、Asset を移動・Rename した時点で**全 Scene の参照が切れる**。
ファイル整理ができないエンジンになる。
永続 ID を正、パスを Fallback にすれば、整理が Scene を壊さない。
（G-28 の「UUID で識別する」と同じ思想。）

**なぜタイムスタンプではなくハッシュか**

タイムスタンプ比較だと、**Git のチェックアウトで全 Asset が再読込**になる
（内容が同じでも更新時刻が変わる）。
ハッシュなら「内容が変わっていないのに再読込する」を避けられる。
代償はハッシュ計算のコスト（ファイル全体を読む必要がある）。

**なぜ参照カウントを持たないのか（推定、R-31）**

- Editor では Asset は「プロジェクトを開いている間ずっと生きている」のが普通。
  細かく解放する需要が小さい。
- 参照カウントを入れると、Component の `assetPath` / `assetId` の増減を**全経路で追う**必要がある。
  Undo（Scene 丸ごとコピー、R-32）・共同編集・Prefab 展開まで含めると漏れが出る。
  漏れると「解放されない」か「使用中に解放される」のどちらかになる。

**弱点（R-31）**

- **使われなくなった Asset がメモリに残り続ける。**
  大きな Scene を切り替えながら長時間編集すると増える。
- `Unload` を明示的に呼ぶと、**まだ参照している Component があっても止められない**。
  安全性は呼び出し側の責任。
- 非同期ロードが無いので、大きな Texture / Model のロード中に**メインスレッドが止まる**（R-24）。

**非同期ロードを入れるときの障壁**

単に別スレッドで読むだけでは済まない。

- D3D12 リソースの生成と Upload はコマンド発行を伴うので、CommandList の扱いを決める必要がある。
- ロード完了までの「まだ無い」状態を、描画側が扱えるようにする必要がある
  （Placeholder テクスチャなど。本エンジンは `makePlaceholder` を持つ）。
- `EditorSharedState` の可変グローバルへの同時アクセスを整理する必要がある（R-24 と同じ根）。

---

### G-35. Mipmap 生成の方式 — 仕組みの違い

| 方式 | 実行場所 | 速度 | 品質 |
| --- | --- | --- | --- |
| **CPU（DirectXTex 等）**（本エンジン） | ロード時に CPU | 遅い（ロード時間に乗る） | フィルタを選べる。sRGB 対応 |
| **GPU Compute** | ロード後に Compute Shader | 速い | フィルタを自前で書く |
| **事前生成（DDS に含める）** | オフライン | **ロード時ゼロ** | 最良（オフラインで時間をかけられる） |
| **ハードウェア生成（D3D11 `GenerateMips`）** | ドライバ | 速い | フィルタが選べない |

**本エンジンの方式（F-16）**

読み込み時に `DirectX::GenerateMipMaps`（CPU 側、DirectXTex）で生成。既定 `generateMipmaps = true`。
`.hdr` と `.dds` は既に Mip を持つ／リニアなので生成しない。

**sRGB 対応フィルタを選んでいる理由**

Mip を作るのは「縮小して平均する」操作。
**平均はリニア空間で行わなければならない。**
sRGB の値をそのまま平均すると、暗部が持ち上がった Mip ができる。

本エンジンは `useSrgbMipFilter` で sRGB 対応フィルタを選んでいる（F-3、G-6）。
`.hdr` / `.dds` はリニアなので `false` にする。

**DDS 事前生成を採っていない影響**

- ロード時に Mip 生成のコストを払う。**非同期ロードが無い（G-34）ので、
  その間メインスレッドが止まる。**
- 一方、任意の画像形式（PNG / JPEG）をそのまま置ける利便性がある。
  Editor として「ファイルを置けば使える」ことを優先した判断（推定）。

`Compute/GenerateMip.CS.hlsl` は存在するが、コードから参照されているかは
Shader 797本のうち 79本しか使われていない状況（F-6）を考えると要確認。

---

### G-36. この章で扱った方式の一覧（引きやすさのため）

| 分野 | 本エンジンの方式 | 主な代替 | 節 |
| --- | --- | --- | --- |
| 描画構成 | Forward + 部分 GBuffer | Deferred / Forward+ / Visibility Buffer | G-11 |
| 影 | CSM 4 Cascade + Cube 6面、単一 5×5 Atlas | 単一 Map / Dual Paraboloid / Shadow Volume / RT | G-12 |
| 影のフィルタ | 9-tap PCF、傾斜依存バイアス | HW 比較サンプラ / PCSS / VSM / ESM | G-13 |
| AO | GTAO 8方向×2ステップ + バイラテラル | SSAO / HBAO / RTAO | G-14 |
| 反射 | Planar + SSR（Hi-Z 72step）+ Cubemap の3併用 | 単一方式 / RT | G-15 |
| GI | Probe + SH + Visibility、Bake 分散 + SSGI | Lightmap / Voxel Cone / DDGI / Path Trace | G-16 |
| AA | None / FXAA / SMAA / Temporal の排他 | MSAA / SSAA | G-17 |
| 半透明 | Alpha Cutout + 通常ブレンド + WBOIT | ソート / Depth Peeling / Linked List | G-18 |
| Tone Mapping | ヒストグラム Auto Exposure + Final Composite | Reinhard / Filmic / ACES 単独 | G-19 |
| Bloom | 4段 down + 3段 up + Glare | 閾値+単一ブラー / FFT | G-20 |
| 水面 | FFT Ocean + Tessellation + 連続 LOD | Sin 波 / Gerstner / 流体 | G-21 |
| Skinning | GPU（VS + CS）、前フレーム Bone 行列保持 | CPU Skinning | G-22 |
| Culling | CPU AABB 8頂点 + GPU Frustum/Hi-Z + Predication/ExecuteIndirect | Sphere 判定 / Software Occlusion | G-23 |
| Physics | Jolt 5.5.0、固定 1/60 | PhysX / 自作 | G-24 |
| API | D3D12 | OpenGL / D3D11 / Vulkan | G-25, G-26 |
| Component | fat struct + type tag | 継承 / 多態 Component / ECS | G-27 |
| Scene 保存 | 行単位・列位置依存 Text、未知行保持 | Binary / JSON / key=value | G-28 |
| 共同編集 | Revision + Property 差分、TCP、Snapshot | ファイル同期 / ロック / OT / CRDT | G-29 |
| Script | C++ DLL + 関数ポインタテーブル（Version 15） | 直接コンパイル / Lua / C# | G-30 |
| Profiler | 計装 Scope + GPU Timestamp + `operator new` 置換 | サンプリング / 外部ツール | G-31 |
| 破壊 | CPU 破片 = GameObject、GPU 破片 = Instance のみ | 全部 GameObject 化 | G-32 |
| Editor 分離 | 同一プロセス・フラグ切り替え | 別 World / 別プロセス / 別ビルド | G-33 |
| Asset | 永続 ID + ハッシュ Cache、参照カウント無し | パス直参照 / shared_ptr / GC / LRU | G-34 |
| Mipmap | CPU 生成（DirectXTex）、sRGB 対応フィルタ | GPU Compute / DDS 事前生成 / HW | G-35 |

---

## 外部依存とファイル形式（H章）

更新基準: 2026-09-29

「何を自作し、何を借りているか」「どのファイル形式を選び、なぜその形式か」をまとめる。
面接では**借り物と自作の線引き**と、**その線をどこに引いたかの理由**を聞かれる。

判定方法: `CG2.vcxproj` の `AdditionalDependencies`、`#pragma comment(lib, ...)`、
および `Source` 配下から実際に include / 参照しているファイル数を数えた。
**同梱されているが使っていないものは「未使用」と明記する**（F-6 の Shader 797本/79本と同じ問題）。

---

### H-1. 使っている外部ライブラリ

| ライブラリ | 版 | 用途 | 参照ファイル数 | 選定理由（推定） |
| --- | --- | --- | ---: | --- |
| **Jolt Physics** | 5.5.0 | 剛体・Collider・拘束 | 1（`EditorJoltPhysicsManager`） | Broad/Narrow Phase と拘束ソルバが完成。決定性と並列性の設計が明示されている。自作すると「動く」と「積んでも沈まない」の差に時間を取られる（R-7、G-24） |
| **NvBlast** | Blast 1.1.5 | 破壊（メッシュの分割・破片生成） | 5 | 破壊の分割アルゴリズムは専門性が高い。Chunk/Bond のグラフ構造と分割条件を自作する労力が大きい（R-18、G-32） |
| **Effekseer** | 1.70e | パーティクル / エフェクト | 13 | エフェクトの**オーサリングツールが付属**する。エディタ・ランタイム・ファイル形式（`.efk`）が揃っており、アーティストが直接編集できる |
| **DirectXTex** | — | 画像の読み込み・Mip 生成・フォーマット変換 | 2 | PNG/JPEG/HDR/DDS/TGA を1つの API で扱え、**sRGB 対応の Mip フィルタ**を持つ（F-3、G-35）。WIC を直接叩くより扱いやすい |
| **Dear ImGui**（docking 版） | — | Editor UI 全体 | 11 | 即時モード UI なので、状態の二重管理が起きない。Editor の値と UI の値がずれない（後述） |
| **ImGuizmo** | — | Scene View の移動・回転・拡縮ギズモ | 6 | ImGui と同じ即時モードで統合でき、行列を直接受け渡せる |
| **Recast / Detour** | 1.6.0 | NavMesh 生成と経路探索 | 6 | NavMesh のボクセル化・領域分割・輪郭抽出は実装量が大きい。業界標準（Unity も同系） |
| **BehaviorTree.CPP** | 4.9.0 | AI の Behavior Tree | 7 | XML でツリーを定義でき、Groot でのビジュアル編集に対応。ノードの再利用が効く |
| **OpenSteer** | — | 群れ・操舵挙動（Steering） | 3 | Seek / Flee / Separation / Cohesion などの操舵挙動の定番実装 |
| **ONNX Runtime** | 1.27.0（GPU/CUDA13） | 画像認識の推論（検出 / 分類 / 顔） | 3 | 学習済みモデルを `.onnx` で差し替えられる。フレームワーク（PyTorch/TF）に縛られない |
| **FBX SDK** | — | FBX のインポート | 2 | FBX は仕様が公開されていないため、**実質的に公式 SDK 以外の選択肢がない** |
| **meshoptimizer** | — | **頂点の重複排除とリマップ**（インポート時） | 1 | 後述 |
| **FeelKit Haptics** | — | ハプティクス（触覚）Backend | 11 | Device SDK。自作不可 |
| **FidelityFX SDK** | 1.1.4 | （限定的、後述） | 3 | — |
| **DXC**（`dxcompiler.lib`） | — | HLSL の Shader Model 6 コンパイル | — | `ps_6_0` 以降は DXC が必須（FXC は 5.1 まで）。SM6 のウェーブ命令等を将来使える |

**meshoptimizer の用途は LOD 生成ではない**（重要）

```cpp
// EditorAssetUtility.cpp:184-204
meshopt_generateVertexRemap(...)   // 同一頂点を検出して番号を振り直す
meshopt_remapIndexBuffer(...)
meshopt_remapVertexBuffer(...)
```

使っているのは**頂点の重複排除（dedup）とリマップ**だけ。
`meshopt_simplify`（LOD 用の簡略化）は使っていない。
だから **R-35 の「汎用 Mesh LOD が無い」という弱点は、ライブラリが無いからではなく、
機能として実装していないから**。ライブラリは既に手元にある。

**Dear ImGui を選んだ理由（即時モード UI の利点）**

保持モード（retained mode、WPF や Qt）の UI は、**UI 側にも状態のコピーを持つ**。
Editor の値を変えたら UI へ通知し、UI を触ったらモデルへ通知する、という双方向の同期が必要。
この同期漏れが「Inspector の表示が古い」という不具合になる。

即時モードは毎フレーム UI を構築し直す。

```cpp
ImGui::DragFloat("Roughness", &component.roughness);
```

**UI が直接データを指す**ので、同期そのものが存在しない。
Field 1,596 個（R-1）を Inspector に出す規模では、この差が決定的。

代償: 毎フレーム UI を構築するので CPU コストが常に掛かる。
だから本エンジンは Window の可視判定を Draw 冒頭に置いている（R-26）。

---

### H-2. 同梱しているが使っていないもの

**「あるから使っている」と説明してはいけないもの。**

| ライブラリ | 状態 | 根拠 |
| --- | --- | --- |
| **PhysX** | **未使用** | `PxPhysics` / `PxRigid` を参照するファイルが **0**。剛体は Jolt（H-1）。同じ PhysX ファミリの NvBlast だけ使っている |
| **imgui-node-editor** | **未使用** | `imgui_node_editor` を参照するファイルが **0**。Animator Graph の編集は自前描画 |
| **FidelityFX SDK** | **限定的**（3ファイルが参照） | CAS（シャープネス）等が同梱されているが、本エンジンの Sharpen は自前 PSO（R-16）。SDK のどこを使っているかは要確認 |

**PhysX が残っている理由（推定）**: NvBlast が PhysX ファミリのビルド構成（`PhysicsSdk/lib/release`）を共有しているため。
`NvBlast.lib` / `NvBlast.dll` だけ必要で、剛体部分は使わない。

**これを正確に言えることの価値**: 「Physics は PhysX を使っています」と答えると、
`JPH::` が 455箇所ある `EditorJoltPhysicsManager.cpp` を見られた時点で食い違う。
**「剛体は Jolt、破壊は NvBlast、浮力などは自作の3系統」**が正確な答え（R-7）。

---

### H-3. 扱うファイル形式と、各形式を選んだ理由

**コード内で扱っている拡張子を数えた結果の主要なもの。**

#### Scene / Prefab / 設定

| 形式 | 用途 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **`.scene`（独自 Text）** | Scene 本体 | **行単位で diff・マージできる**。Git の行マージがそのまま効く。人が壊れた箇所を特定できる。**未知の行を保持**できる（R-2、G-28） | 列位置依存なので列の挿入・並べ替え不可。`LoadScene` が 3,721行になる主因 |
| **`.json`** | Project Settings、Engine Version、共同編集メッセージ | 構造を持てる。未知キーを無視/保持しやすい。人が読める。**既存のパーサが豊富** | キー文字列でサイズが膨らむ。行単位マージが効きにくい |
| **`.meta`** | Asset の Import Settings と永続 ID | Asset 本体を書き換えずに設定を持てる。**Asset を移動しても ID が追随**（R-8、G-34） | ファイルが2倍になる。片方だけ移動すると壊れる |
| **`.team`** | 共同編集の設定 | 同上 | — |
| **`.save` / `.gdata`** | セーブデータ / ゲームデータ | — | 未検証 |

**なぜ Scene を JSON にしなかったか**（G-28）

Field が 1,596 個 × Component 数だけキー文字列が並ぶ。
さらに JSON は階層構造なので、1つの GameObject が1行に潰れるか、階層が深くなる。
**どちらも行単位マージと相性が悪い。** 共同制作を前提にすると、行単位で差分が取れる形式が要る。

**なぜ設定は JSON にしたか**

設定は Scene と違って**量が少なく、階層構造が自然**（`build.platform.windows.arch` のような）。
マージの需要も小さい。既存のパーサが使えて、人が直接編集できる利点が勝つ。

#### モデル

| 形式 | 利点 | 欠点 |
| --- | --- | --- |
| **`.fbx`** | **業界標準**。マテリアル・スケルトン・アニメーション・複数メッシュを1ファイルに持てる。DCC ツール（Maya / Blender / 3ds Max）が全部出力できる | 仕様非公開で**公式 SDK が実質必須**。SDK が重い（`libfbxsdk.lib`）。バイナリなので中身を確認できない |
| **`.obj` + `.mtl`** | **テキストで中身が読める**。仕様が単純でパーサを自作できる。デバッグ用の最小モデルを手書きできる | スケルトン・アニメーション・複数 UV を持てない。マテリアルの表現力が低い |

**両方対応している理由（推定）**: `.obj` は**自作パーサの検証と最小再現に使える**。
FBX SDK を通さずに読めるので、SDK 側の問題か自前の問題かを切り分けられる。
実制作は `.fbx`。

#### テクスチャ

| 形式 | 利点 | 欠点 | 本エンジンの扱い |
| --- | --- | --- | --- |
| **`.png`** | 可逆圧縮。アルファを持てる。どのツールでも出力できる | ファイルサイズ大。**GPU が直接読めない**（展開が必要） | sRGB として読む（既定）。Mip を CPU 生成 |
| **`.jpg` / `.jpeg`** | サイズが小さい | **非可逆**。アルファ無し。圧縮ノイズが Normal Map を壊す | 同上。カラーテクスチャのみ想定 |
| **`.tga`** | 可逆。アルファを持てる。古いツールとの互換 | サイズ大 | 同上 |
| **`.hdr`** | **1.0 を超える輝度を保持**（HDR）。IBL / 環境マップに必須 | サイズ大 | **リニアとして読む**（sRGB 変換しない）。Mip を生成しない |
| **`.dds`** | **GPU の圧縮形式（BC1〜BC7）をそのまま格納**。Mip チェーンを含められる。**読み込み時の展開と Mip 生成が不要** | 作成にツールが必要。中身を確認しにくい | **リニアとして読む**。Mip は既にあるので生成しない |

**`.dds` の利点が本質的に大きい理由**

PNG/JPEG は CPU で展開してから GPU へ Upload する（`DEFAULT` Heap へコピー、F-12）。
さらに Mip を CPU 生成する（G-35）。**この2つがロード時間に直接乗る**。
本エンジンは非同期ロードが無い（R-31）ので、**その間メインスレッドが止まる**。

`.dds` なら
- 圧縮形式（BC7 など）のまま GPU へ置ける → **VRAM 使用量が 1/4〜1/6**
- Mip が既に入っている → 生成コストゼロ
- 展開が不要 → CPU コストほぼゼロ

**つまり配布ビルドでは `.dds` へ変換しておくのが本来正しい。**
現状 PNG/JPEG をそのまま置ける利便性（Editor で「ファイルを置けば使える」）を優先している。
これは**判断として妥当だが、配布時の最適化手段が残っている**ということでもある。

#### 音声

| 形式 | 利点 | 欠点 |
| --- | --- | --- |
| **`.wav`** | **非圧縮なので展開コストゼロ**。ヘッダが単純でパーサを自作できる（本エンジンは `SoundLoadWave` を自作） | ファイルサイズ大。長い BGM に向かない |
| **`.ogg`** | 圧縮率が高くライセンスが自由（Vorbis） | デコードが必要。ストリーミング実装が要る |

`.ogg` は拡張子の参照が1件のみ。実際にデコードしているかは未検証。
**`.wav` 中心なら「BGM が長いとメモリを食う」が弱点**になる。

#### Shader / スクリプト / モデル推論

| 形式 | 用途 | 利点 |
| --- | --- | --- |
| **`.hlsl` / `.hlsli`** | Shader ソース | テキストなので diff が取れる。起動時に DXC でコンパイルし、**失敗した全 Path を1回でまとめて表示**（F-6） |
| **`.dll`** | Native Script（ゲームコード） | **C++ のまま書ける**。デバッガがそのまま使える。Script だけ再ビルドできる（R-23、G-30） |
| **`.onnx`** | 画像認識モデル | **学習フレームワークに依存しない**中間表現。モデルを差し替えるだけで認識内容を変えられる |
| **`.efk`** | Effekseer エフェクト | 専用オーサリングツールで編集でき、**プログラマを介さずにエフェクトを調整できる** |
| **`.xml`** | Behavior Tree の定義 | BehaviorTree.CPP の標準形式。Groot でビジュアル編集できる |
| **`.py`** | ツール（Field Registry 生成など） | ビルド環境に依存せず書ける（`Tools/generate_log_field_registry.py`） |
| **`.tmp`** | 保存時の一時ファイル | **rename による原子的保存**（R-2）。保存失敗で元データを壊さない |

---

### H-4. OS / プラットフォーム API

| API | 用途 | 代替と比較 |
| --- | --- | --- |
| **Direct3D 12**（`d3d12.lib`、`dxgi.lib`） | 描画 | OpenGL / Vulkan との比較は G-25、G-26 |
| **DXC**（`dxcompiler.lib`） | Shader コンパイル | FXC は Shader Model 5.1 まで。SM6 には DXC が必須 |
| **XAudio2**（`xaudio2.lib`） | 音声再生 | 後述 R-41 |
| **DirectInput**（`dinput8.lib`） | キーボード・マウス | 後述 R-42 |
| **XInput**（`xinput.lib`） | ゲームパッド | 後述 R-42 |
| **Media Foundation**（`mfplat` / `mfreadwrite` / `mf` / `mfuuid`） | カメラ映像の取り込み | DirectShow の後継。Windows 標準でカメラを扱える |
| **WinHTTP**（`winhttp.lib`） | HTTP 通信（Online 機能） | WinINet より**サービス/バックグラウンド向け**。非同期 API を持つ |
| **Winsock**（`ws2_32.lib`） | TCP（共同編集） | 後述 R-39 |
| **DbgHelp**（`dbghelp.lib`） | クラッシュ時の MiniDump 出力 | `MiniDumpWriteDump` で `.dmp` を吐く（`CrashHandler.cpp`） |
| **COM / Shell**（`ole32` / `shell32` / `comdlg32`） | ファイルダイアログ等 | Editor のファイル選択 |
| **winmm** | マルチメディアタイマ | フレームレート制限の精度向上（推定） |

**MiniDump を出す価値**

Release 配布物がクラッシュしたとき、**利用者の環境で何が起きたか**を知る唯一の手段。
`Dumps/日時.dmp` を Visual Studio で開けば、そのマシンのコールスタックが読める。
`InstallCrashHandler()` で `SetUnhandledExceptionFilter` に登録している。

ただし現状は 68行の最小実装で、`MiniDumpNormal`（スタックのみ）。
ヒープを含めないので変数の値は限定的にしか見えない。

---

## 設計判断と技術選択 — 欠落していたサブシステム（R-39 以降）

---

### R-39. 共同編集の通信プロトコル（完全版）

R-11 / R-34 で「Revision + Property 差分 / TCP / Snapshot」までは書いたが、
**プロトコルの実体と Lock 機構を落としていた。** ここで補う。

#### Transport の抽象化と Tailscale の扱い

**設計判断が `ICollaborationTransport.h` に明文化されている。**

```
// LAN / Tailscale / 将来のWebSocket(WSS) / Cloud Backend は、この実装差し替えだけで対応する。
// Scene同期・Lock・Conflict等の上位処理へ `if (tailscale)` のような分岐を持ち込まないこと。
// Tailscale固有のAPI・Node Key・Tailnet管理はここにも上位にも入れない。
// 通常設定ではTailscaleの 100.x.x.x を直接保存せず、MagicDNS hostname を優先する。
```

**この判断の意味**

Tailscale は WireGuard ベースの VPN で、**NAT 越えと暗号化を肩代わりする**。
アプリから見ると「相手の IP に TCP で繋がる」だけで、LAN と区別がつかない。

だから**「Tailscale 対応」という機能を作らない**という設計になっている。
`ICollaborationTransport` を差し替えられるようにしておき、
経路の違い（LAN の `192.168.x.x` か Tailscale の `100.x.x.x` か）は接続先文字列の差だけにする。

**なぜ MagicDNS hostname を優先して保存するか**

Tailscale の `100.x.x.x` は**再接続やデバイス再登録で変わりうる**。
MagicDNS の hostname（`my-pc.tailnet-name.ts.net` 等）は安定するので、
設定ファイルに保存するならそちらが正しい。

**なぜ NAT 越えを自作しなかったか（推定）**

自作するなら STUN / TURN / ICE（ホールパンチング）を実装し、
さらに暗号化と認証を自前で持つ必要がある。
Tailscale に任せれば、アプリは**ただの TCP クライアント**でいられる。
1〜5人規模の制作ツールに対して、自作する理由がない。

#### プロトコルの実体（`CollaborationProtocol.h`）

**JSON over TCP。** メッセージ種別は JSON の `"type"` フィールドと一致する。

| 定数 | 値 | 意味 |
| --- | ---: | --- |
| `kCollaborationProtocolVersion` | **3** | 接続時に必ず突き合わせる |
| `kHandshakeTimeoutSeconds` | 10.0 | 超えたら旧 Build か別プロトコルとみなす |
| `kHeartbeatIntervalSeconds` | 5.0 | Client が送る間隔 |
| `kHeartbeatTimeoutSeconds` | 20.0 | Server が「消えた」と判断する猶予 |
| `kLockReleaseGracePeriodSeconds` | 5.0 | Timeout 後、Lock を解放するまでの追加猶予 |
| `kMaximumSynchronizedFileBytes` | 128 MB | 1ファイルの転送上限 |
| `kMaximumMessageBytes` | 8 MB | 1メッセージの上限。超えたら**壊れた送信元とみなす** |

**プロトコル版の履歴（ヘッダに記録されている）**

```
1 : Editor内Host同士のLAN接続（暗黙。handshakeにprotocol項目が無い旧Build）
2 : CG2TeamServer導入。projectId / protocolVersion / heartbeat を追加
3 : Snapshot Revision、履歴再送、Offline 3-way同期を追加
```

**メッセージ種別**

| type | 方向 | 役割 |
| --- | --- | --- |
| `handshake` | C→S | 接続要求（projectId / protocolVersion を含む） |
| `handshakeOk` / `handshakeNg` | S→C | 受理 / 拒否（**理由付き**） |
| `heartbeat` / `heartbeatAck` | C→S / S→C | 生存確認。**Ack で Latency を計測** |
| `peerLeft` | S→C | 離脱通知（**Lock 解放の通知**） |
| `historyRequest` | C→S | `afterRevision` 以降の履歴を要求 |
| `historyBegin` / `historyEnd` | S→C | 履歴送信の開始 / 完了 |

**なぜ JSON にしたか（推定）**

- Scene が Text 形式（R-2、G-28）なので、Snapshot を**そのまま文字列として載せられる**。
  バイナリプロトコルだとエスケープや長さ管理が要る。
- **異なる Engine Build が接続し得る**（ヘッダが明言）。
  JSON なら未知フィールドを無視でき、版差に耐えやすい。
- デバッグでパケットを目で読める。
- 代償: サイズが大きい。だから 1メッセージ 8MB、1ファイル 128MB の上限を置いている。

#### 切断検出の仕組み（3段構え）

```
Heartbeat 5秒間隔で送る
  → 20秒応答が無ければ Server が「消えた」と判断
  → さらに 5秒の猶予を置いてから Lock を解放
```

**なぜ3段なのか。ヘッダのコメントが理由を書いている。**

```
// 遠隔ではPCスリープ・Wi-Fi切替・Tailscale再接続で無言のまま消えることがあるため、
// 応答が無いClientのLockを永久に残さないようにする。

// Lockは安全側に倒し、Timeout後すぐには解放せず短い猶予を置く。
// (一時的なネットワーク瞬断で他人が同じObjectを触り始めるのを避ける)
```

**TCP は切断を教えてくれない**、というのが本質。
`recv` がブロックしたまま返らないケース（PC スリープ、Wi-Fi 切替）があるので、
**アプリ層の Heartbeat が必要**。

そして「消えた」と判断してすぐ Lock を解放すると、瞬断から復帰した本人と
新たに触り始めた別人が**同じ Object を編集する**。だから猶予を置く。
これは**可用性（Lock を永久に残さない）と安全性（同時編集を防ぐ）のトレードオフを、
時間で分けて解決している**例。

#### Lock 機構（R-11 で落としていた要素）

本エンジンは **Property 単位の Revision 差分（R-11）と Object Lock を併用している。**

- G-29 の比較表で「ロック（排他）」を代替方式として挙げたが、**実際は併用している**。
- Lock は `peerLeft` で解放通知され、Timeout + 猶予でも解放される。

**なぜ両方必要なのか（推定）**

| 手段 | 防げるもの |
| --- | --- |
| Property 単位 Revision | **別々の箇所**を同時に編集できるようにする（衝突粒度を細かく） |
| Object Lock | **同じ Object を2人が同時に触る**のを事前に止める（衝突を起こさせない） |

Revision 差分は「衝突が起きた後に解決する」仕組み。
Lock は「衝突を起こさせない」仕組み。
編集操作は取り消しが難しい（相手の作業が消える）ので、**起こさせない方が体験が良い**。
一方 Lock だけでは粒度が粗く並行作業ができないので、両方を使う。

#### セキュリティ配慮

```cpp
inline bool IsValidProjectIdCharacter(char character) {
    return (a-z) || (A-Z) || (0-9) || '-' || '_' || '.';
}
inline bool IsValidProjectId(const std::string& projectId) {
    if (projectId.empty() || projectId.size() > 64u) return false;
    ...
}
```

コメントが理由を書いている: 「**ProjectId として使える文字かどうか。
Path要素へ流用されても危険が無い範囲へ限定する。**」

つまり **Path Traversal 対策**。`projectId` が `../../etc/passwd` のような値だと、
サーバ側でファイルパスに使ったときに任意の場所を読み書きされる。
文字種と長さを入口で絞っている。

**これは「信頼できない入力を最初に検証する」原則**の実装。
ネットワークから来る値をそのままパスに使わない。

#### 弱点（更新）

- **再送とメッセージ分割の実装が未確認。** `kMaximumMessageBytes` を「超えるものは分割転送されている
  はずなので、壊れた送信元とみなす」とあるので分割はあるが、実装箇所は要確認。
- Play 中の他者変更は保留するので、Play が長いと保留分が積む（R-11）。
- `EditorTeamCollaborationManager::Draw` が 1,362行（filesystem 6回・map find 21回・sort 2回）。
- `Update` の delta time が `1/60` 固定（R-3）。
- サーバ（`Tools/CG2TeamServer`）は**単一の中継サーバ**。P2P ではないので、
  サーバが落ちると同期が止まる。

---

### R-40. 数学ライブラリ

**方式**: **完全自作、スカラー実装。** DirectXMath も SIMD も使っていない。

**根拠**

```
DirectXMath / XMMATRIX / XMVECTOR の参照 : 0 ファイル
__m128 / _mm_ / SIMD の参照              : 0 ファイル
自作の規模: Matrix.cpp 273行 + Vector&Matrix.cpp 240行 + Vector.cpp 64行 = 577行
```

行列は `struct Matrix4x4 { float matrix[4][4]; }`、変換は素朴な積和（F-1）。

**採用理由（推定）**

- **学習目的。** 行列の中身を自分で書くことが目的の一部だった可能性が高い。
  実際に `Transform` の実装がコメント付きで「各列との積和に平行移動成分を足す」と説明している。
- 依存を減らせる。DirectXMath は Windows SDK に含まれるので依存自体は軽いが、
  型（`XMVECTOR` = `__m128`）を使うと**アライメント制約（16 byte）**が全体に波及する。
  `EditorComponent`（Field 1,596個、R-1）のような巨大 struct にアライメント型を混ぜると
  パディングが増え、Serialize（R-2）の扱いも複雑になる。
- HLSL 側と同じ `row_major` の並びをそのまま持てる（F-17）。
  DirectXMath は行優先だが、`XMMATRIX` は 4 つの `XMVECTOR` なので扱いが変わる。

**他候補**

| 候補 | 特性 |
| --- | --- |
| **DirectXMath** | Windows SDK 同梱。SSE/AVX を使う。`XMVECTOR` は 16 byte アライン必須 |
| **GLM** | OpenGL 流儀（列優先・列ベクトル）。ヘッダオンリー。D3D と規約が逆（G-26） |
| **Eigen** | 汎用線形代数。行列サイズが大きい用途向け。ゲームには重い |
| **自作**（本エンジン） | 完全な制御。学習価値。**最適化は自分でやる** |

**不採用理由（推定）**: GLM は規約が逆で、D3D 向けに使うと混乱の元（G-26）。
Eigen はゲームの 4×4 用途には過剰。DirectXMath はアライメント制約が波及する。

**弱点（性能上の実害）**

スカラー実装なので、行列積 1 回が **16 回の乗算 + 12 回の加算を逐次実行**する。
SIMD なら 4 要素を 1 命令で処理でき、理論上 4 倍。

**影響が出る場所**

| 処理 | 頻度 | 影響 |
| --- | --- | --- |
| Transform の World 行列更新 | オブジェクト数 × 毎フレーム | 中 |
| **Skinning の Bone 行列計算** | Bone 数 × オブジェクト数 × 毎フレーム | **大**（GPU Skinning でも Bone 行列は CPU で作る、R-19） |
| **Frustum Culling の AABB 8頂点変換** | オブジェクト数 × 8 × 毎フレーム | **大**（R-30） |
| Shadow の Cascade ごとの View Projection | Cascade 数 × ライト数 | 小 |
| Physics との Transform 同期 | 剛体数 × 毎フレーム | 中 |

**ただし現状ボトルネックとは確認していない。** Profiler（R-10）で
これらの Scope の CPU 時間を見て、支配的かどうかを確かめるのが先。
「SIMD 化すれば速くなる」は原理的には正しいが、**測らずに言うべきではない**。

**改善候補**

- 行列積とベクトル変換だけ SIMD 化する（API は変えない）。
  `Matrix4x4` を 16 byte アラインすれば `_mm_load_ps` が使える。
- または DirectXMath を**内部実装としてだけ**使い、公開型は現状の `Matrix4x4` を保つ。
- どちらも**まず Profiler で支配項を確認してから**。

---

### R-41. Audio

**方式**: **XAudio2** を直接使用。3D は `spatialBlend` による 2D/3D 手動ブレンド。

**構成**（`EditorAudioManager.cpp`、1,545行）

| 要素 | 実装 |
| --- | --- |
| 初期化 | `XAudio2Create` → `CreateMasteringVoice`（`g_masterVoice`） |
| 再生 | 音ごとに `CreateSourceVoice`。個別に音量・Fade・Pause・再生速度を制御（Script API Version 12 で追加） |
| WAV 読み込み | **自作**（`SoundLoadWave`、`EditorSharedState.h`）。RIFF / fmt / data チャンクを手で解析 |
| エフェクト | **XAudio2FX の Reverb**（`XAudio2CreateReverb`）。`XAUDIO2FX_REVERB_PARAMETERS` を設定 |
| 空間音響 | `spatialBlend`（0〜1）で 2D 音量と 3D 音量を線形補間 |

**空間音響の実装**

```cpp
// EditorAudioManager.cpp:508
const float finalVolume = volume2d * (1.0f - it->spatialBlend) + volume3d * it->spatialBlend;
```

**X3DAudio（Windows 標準の 3D 音響 API）は使っていない。**
距離減衰を自前で計算し、2D 音量との線形補間で「どれだけ 3D に聞こえるか」を決める。
これは **Unity の Spatial Blend と同じ考え方**。

**なぜ X3DAudio を使わないか（推定）**

- X3DAudio は HRTF やスピーカー行列（`SetOutputMatrix`）を使った本格的な定位を行う。
  設定項目が多く、リスナー・エミッタの Cone / Curve を作る必要がある。
- `spatialBlend` 方式なら「2D の BGM」と「3D の効果音」を**同じパラメータ1つで連続的に扱える**。
  UI 音は 0、環境音は 0.5、足音は 1 のように設計者が直感的に決められる。
- 代償: **真の定位（左右・前後の聞き分け）は出ない**。音量の減衰だけ。

**XAudio2 を選んだ理由（推定）**

| 候補 | 特性 |
| --- | --- |
| **XAudio2**（本エンジン） | Windows 標準。低レベルだが十分な機能（Submix / Effect / Voice）。追加 DLL 不要 |
| **WASAPI 直** | さらに低レベル。ミキシングを全部自作する必要がある |
| **FMOD / Wwise** | 高機能・オーサリングツール付き。**商用ライセンス** |
| **OpenAL / miniaudio** | クロスプラットフォーム。Windows 専用なら利点が薄い |

Windows 専用（`isStandaloneGame_` も Windows 前提）なら XAudio2 が素直。
FMOD / Wwise はライセンスと導入コストが見合わない規模。

**弱点**

- **`.wav` 中心（非圧縮）**なので、長い BGM がメモリを食う（H-3）。
  ストリーミング再生の実装が要る。
- 真の 3D 定位が無い（上記）。
- `.ogg` の参照が1件のみで、デコード実装は未検証。
- Voice の数に上限があるはず（XAudio2 の実用上）。上限管理の実装は未確認。

---

### R-42. Input

**方式**: **DirectInput（キーボード・マウス）+ XInput（ゲームパッド）+ 独自 InputAction 層。**

#### 低レベル層

| デバイス | API | 実装 |
| --- | --- | --- |
| キーボード | **DirectInput** | `GetDeviceState` で **DIK_* の 256 バイト配列**を取る（`EditorFrameInputManager.cpp:82`） |
| マウス | **DirectInput** | `DIMOUSESTATE`（相対移動量とボタン） |
| ゲームパッド | **XInput** | `XInputGetState`（`GamepadInput.cpp:91`） |

**前フレームの状態を保持**（`g_key` / `g_preKey`、`g_mouseState` / `g_preMouseState`）。
「押した瞬間」「離した瞬間」の判定に使う。

**XInput の最適化（コメントに理由が書かれている）**

```cpp
// 未接続Slotの再検出間隔。XInputGetStateは未接続Slotに対して重いため、毎フレームは叩かない。
// 未接続Slotは毎フレーム問い合わせない(XInputGetStateが未接続時に重いため)。
```

`XInputGetState` は**未接続のスロットに対して数百マイクロ秒かかる**という既知の挙動がある
（内部でデバイス列挙が走る）。4スロット × 毎フレームやると無視できないコストになる。
**未接続スロットだけ再検出間隔を空けている。**

これは「知らないと踏む」種類の最適化で、対処済みであることに価値がある。

#### 高レベル層（InputAction）

`Engine/Input/` に独立したモジュールがある（13ファイル）。

| ファイル | 役割 |
| --- | --- |
| `InputAction.h/.cpp` | 論理的な操作（「ジャンプ」「移動」） |
| `InputActionMap.h/.cpp` | Action の集合 |
| `InputActionAsset.h/.cpp` | Asset としての保存 |
| `InputBinding.h` | 物理入力 → Action の対応 |
| `InputContext.h` | 状況による切り替え（UI 中 / Play 中） |
| `InputPhase.h` | Started / Performed / Canceled の区別 |
| `InputSystem.h/.cpp` | 全体の駆動 |
| `KeyboardState.h/.cpp` | キー状態 |

**なぜ抽象層を挟むか**

- ゲームコードが `DIK_SPACE` を直接見ると、**キー変更のたびにコードを直す**ことになる。
  「ジャンプ」という Action を見れば、バインドはデータ（Asset）側で変えられる。
- キーボード・パッド・マウスの**どれで入力されても同じ Action** にできる。
- `InputContext` で「UI を開いている間は移動を止める」を宣言的に書ける。
- `InputPhase`（Started / Performed / Canceled）は Unity の Input System と同じ区分。
  押した瞬間・継続・キャンセルを区別できる。

**この設計は Unity の Input System を踏襲している**（構造が一致する）。

**なぜ DirectInput を使うか（推定）**

Windows では**キーボード入力の取得手段が複数ある**。

| 手段 | 特性 |
| --- | --- |
| `GetAsyncKeyState` | 最も簡単。ただしキーリピートやフォーカスの扱いが粗い |
| **Windows メッセージ**（`WM_KEYDOWN`） | 文字入力に正しい（IME 対応）。ゲーム的な「押されている」の取得には向かない |
| **DirectInput**（本エンジン） | 256キーの押下状態を一括で取れる。ゲーム向け |
| **Raw Input** | 最も低レベル。複数デバイスの区別ができる。Microsoft の推奨 |

**DirectInput は Microsoft が非推奨としている**（キーボード・マウスについては Raw Input 推奨）。
動くが、**将来的な推奨からは外れている**点は弱点として認識しておく価値がある。

**弱点**

- DirectInput はキーボード・マウスについて非推奨。Raw Input への移行が本来の方向。
- IME（日本語入力）は DirectInput では扱えない。テキスト入力は ImGui 側（Win32 メッセージ）が担当。
- `Engine/Input/` がトップレベルにあり、`Source/Engine/*` の配置規則と揃っていない（既知の負債）。

---

### R-43. Effect / VFX

**方式**: **Effekseer 1.70e** + 自前の GPU Particle + 自前 VFX Renderer。

**構成**（`Source/Engine/Effect` 3,588行 + `EditorVfxManager` 1,298行 + `EditorGpuParticleManager`）

| 層 | 実装 | 用途 |
| --- | --- | --- |
| **Effekseer** | `EffekseerRendererDX12.lib` / `Effekseer.lib` / `LLGI.lib` | `.efk` ファイルのエフェクト再生 |
| **GPU Particle** | 自前（`EditorGpuParticleManager`） | 大量パーティクルを GPU で更新・描画 |
| **VFX Renderer / EffectDefinition** | 自前 | Effect の定義とプリミティブ描画 |

**Effekseer を選んだ理由（推定）**

- **オーサリングツール（Effekseer Editor）が付属する。**
  エフェクトの作成・調整を**プログラマを介さずに**行える。
  自前で作るなら、タイムライン・カーブエディタ・プレビューを含む専用ツールが必要。
- `.efk` というファイル形式があり、Asset として扱える。
- DX12 レンダラが公式に提供されている（`EffekseerRendererDX12`）。

**それでも自前 GPU Particle を持つ理由（推定）**

Effekseer は CPU 側でパーティクルを更新して頂点を作る方式が基本。
**数万〜数十万のパーティクル**（破片、雨、火花）では CPU が持たない。

自前 GPU Particle は Spawn を Upload Buffer で渡し、更新と描画を GPU 内で回す（R-9）。
上限は `kMaxParticleCount` で、超過分は捨てる。

**つまり「表現力が要るものは Effekseer、数が要るものは自前 GPU」という使い分け。**

**弱点**

- 2系統あるので、エフェクト作成者が「どちらで作るか」を判断する必要がある。
- Effekseer のバージョン更新はレンダラ側の対応が必要（DX12 レンダラが追随している必要がある）。
- GPU Particle は個別操作ができない（R-18 の GPU 破片と同じ制約）。

---

### R-44. Navigation と AI

**方式**: **Recast/Detour（NavMesh）+ BehaviorTree.CPP（判断）+ OpenSteer（操舵）** の3層。

| 層 | ライブラリ | 役割 |
| --- | --- | --- |
| **経路** | Recast（NavMesh 生成）+ Detour（経路探索・Crowd） | どこを通れるか、どう行くか |
| **判断** | BehaviorTree.CPP 4.9.0 | 何をするか（巡回 / 追跡 / 攻撃 / 退避） |
| **操舵** | OpenSteer | どう動くか（滑らかな旋回、群れ、回避） |

**3層に分ける理由**

これは AI の定番の階層分けで、それぞれ**変更の頻度と担当者が違う**。

- NavMesh はレベルデザインが変わったときに再生成する（静的）。
- Behavior Tree はゲームデザイナーが調整する（XML / Groot で編集可能）。
- Steering はプログラマが数値を詰める（移動の気持ちよさ）。

**Recast/Detour を選んだ理由（推定）**

NavMesh の生成は「シーンをボクセル化 → 歩ける面を抽出 → 領域分割 → 輪郭抽出 → 三角形化」
という多段の処理で、実装量が大きい。Recast は**業界標準**（Unity の NavMesh も同系の手法）。

Detour の **Crowd** は、複数エージェントの相互回避（RVO 系）を含む。
これを自作すると「すり抜ける」「詰まる」の調整に時間がかかる。

**BehaviorTree.CPP を選んだ理由（推定）**

- **XML でツリーを定義できる**ので、再コンパイルなしで挙動を変えられる。
- **Groot**（ビジュアルエディタ）でツリーを編集・可視化できる。
- ノード（Condition / Action / Decorator）を再利用できる。

**他候補との比較**

| 方式 | 特性 |
| --- | --- |
| **State Machine（FSM）** | 単純。状態数が増えると遷移が組み合わせ爆発する |
| **Behavior Tree**（本エンジン） | 階層化できる。ノード再利用が効く。デザイナーが編集できる |
| **GOAP（目標指向）** | 柔軟。挙動の予測が難しくデバッグが困難 |
| **Utility AI** | スコアで選ぶ。調整が数値なので扱いやすいが、意図した順序を保証しにくい |

**弱点**

- 3つの外部ライブラリに依存するので、それぞれのバージョン更新と API 変更に追随する必要がある。
- NavMesh の動的更新（破壊で通路が変わる等）に対応しているかは未検証。
- `EditorAIManager`（1,937行）と `EditorNavigationManager`（1,088行）の責務分担は未検証。

---

### R-45. 外部認識・オンライン・ハプティクス（4モジュール）

**AGENTS.md が「新規モジュール」として挙げている 4 系統。合計 8,638行。**

| モジュール | 行数 | Backend | 状態 |
| --- | ---: | --- | --- |
| **Speech**（音声認識） | 2,612 | **SAPI**（Windows Speech API）実装済み。Whisper / ONNX は**未実装で Unavailable** | 部分 |
| **Vision**（画像認識） | 2,420 | **Media Foundation** で取り込み + 内蔵（色 / 動き）+ **ONNX**（検出 / 分類 / 顔）。Landmark / HeadPose は**未対応** | 部分 |
| **Online** | 2,422 | **WinHTTP** 非同期 + Leaderboard / PlayerData / CloudSave / 再送 Queue | 実装済み |
| **Haptics** | 1,184 | **FeelKit** Backend + Clip Asset + Audio / Physics 連携 | 実装済み |

**最も重要な設計判断: `Unavailable` を返す**

`Source/Engine/External/ExternalFeature.h:21` に明記されている。

```cpp
Unavailable = 0,  // Device または Backend が使えない。勝手に別機能で代替しない。
```

さらに `NullSpeechBackend.h:11`

```cpp
// 別機能へ勝手に置き換えず、Unavailable を返し続ける(仕様書 85 項)。
```

**なぜこれが重要な判断なのか**

外部デバイスに依存する機能は「使えないこともある」が普通。
マイクが無い、カメラが刺さっていない、ネットが切れている、ハプティクスデバイスが無い。

このとき**安易な代替をすると、利用者が原因を特定できなくなる**。

- 音声認識が使えないときにキーボード入力へ勝手にフォールバックすると、
  「認識が動いていない」ことに気づかないまま開発が進む。
- カメラが無いときにダミー画像を返すと、認識結果が常に同じで「動いている」と誤認する。

`Unavailable` を返し続ければ、**利用者は「使えない」ことを明確に知る**。
その上でゲーム側が代替を選べる。

**もう1つの約束**: 「**Device 未接続でもゲームロジックは止めない**」（AGENTS.md）。

つまり `Unavailable` は**エラーではなく状態**。例外を投げず、クラッシュせず、
ゲームループは回り続ける。これは R-23 の Script API の方針（無効ハンドルで `false` を返す）と同じ思想。

**この設計パターンの名前**: Null Object パターン。
`NullSpeechBackend` が実際に「何もしないが正しく振る舞う実装」として存在する。
呼び出し側に `if (backend != nullptr)` を書かせない。

**Online の再送 Queue**

WinHTTP の非同期 API を使い、失敗したリクエストを Queue に積んで再送する。
モバイル回線や不安定な Wi-Fi で「送ったつもりで消えた」を防ぐ。

**なぜ WinHTTP か（推定）**: WinINet はユーザー対話（プロキシ認証ダイアログ等）を前提とし、
サービス/バックグラウンド用途では非推奨。WinHTTP は非同期 API を持ち、
ダイアログを出さない。ゲームからの通信に適する。

**Cloudflare Worker（`Tools/CloudflareWorker`）**

サーバ側の参照実装。D1（SQLite）/ KV / R2（オブジェクトストレージ）を使い、
**サーバ側の検証つき**（AGENTS.md）。

**なぜサーバ側検証が必要か**: Leaderboard のスコアをクライアントが送るだけだと、
改造したクライアントが任意のスコアを送れる。サーバで妥当性を検証する必要がある。

**弱点（そのまま未実装の一覧）**

- Speech: **Whisper / ONNX Backend が未実装**。SAPI のみ。SAPI は日本語の精度が限定的。
- Vision: **Landmark / HeadPose が未対応**。検出・分類・顔の矩形までで、表情や姿勢は取れない。
- ONNX Runtime が GPU/CUDA13 版なので、**CUDA が無い環境の挙動**は要確認。
- 4モジュールとも実機デバイスでの検証記録が docs にあるか未確認。

---

### R-46. UI / Text

**方式**: ImGui とは**別系統**のゲーム内 UI。Text 体裁を Component の Field で持つ。

**実装済み（docs の 2026-09-13 記録）**

| 項目 | 内容 |
| --- | --- |
| Text 体裁 | **13 Field**（Font Asset / Size / 横縦揃え / 折返し / はみ出し3モード / 縁取り / 影） |
| Font | **Project 内の Font Asset を Runtime 読込** |
| Script API | `Ui` クラス（Text / Color / FontSize / Interactable / Slider / Toggle） |
| ナビゲーション | **Gamepad UI Navigation** |
| Binding | `EditorUiBindingManager` |

**未対応（docs が明記）**

- **Rich Text（インライン色指定 / 太字指定）は未対応**
- TextMeshPro 完全互換は非対象

**ImGui と分ける理由**

ImGui は**Editor の UI** であって、ゲームの UI ではない。

| | ImGui | ゲーム内 UI |
| --- | --- | --- |
| 対象 | 開発者 | プレイヤー |
| 見た目 | 固定（Editor のスタイル） | ゲームごとに自由 |
| 配布物に含むか | 含めたくない | 含める |
| 解像度対応 | Editor の DPI | ゲームの解像度・アスペクト |

ImGui でゲーム UI を作ると、見た目が Editor に縛られ、配布ビルドに ImGui が必須になる。
（ただし現状は同一プロセス構成なので ImGui は配布物に載る、R-21 / G-33。）

**Gamepad UI Navigation を持つ理由**: コンソール風の操作（十字キーで項目移動）は、
マウス前提の UI と**フォーカス管理の仕組みが別**。
「今どの項目が選ばれているか」と「上下左右でどこへ移るか」を UI が持つ必要がある。

**弱点**

- Rich Text 無しなので、「一部だけ赤くする」「一部だけ太字」ができない。
  ダメージ表示や強調表現で困る。
- Font は Runtime 読込だが、**動的な字形生成（SDF / MSDF）**を使っているかは未検証。
  ビットマップフォントだと拡大でぼける。

---

### R-47. Build と配布

**方式**: Editor から Game Build を出力し、専用 Launcher で配布・更新する。

| 要素 | 実装 |
| --- | --- |
| Build | `EditorGameBuildManager`（1,150行）。`Builds/` へ出力 |
| Launcher | `Tools/CG2Launcher`（別 vcxproj、HTTP ダウンロード付き） |
| Version 固定 | `Engine/Version/engine-version.json`、`Tools/Versioning/Sync-Version.ps1` を**ビルド前に自動実行**（`Directory.Build.targets`） |
| 環境診断 | `EngineEnvironmentCheck.cpp`、結果を `BuildLogs/` へ |
| Migration | `ProjectVersionManager.cpp` |
| 配布テスト | `Tests/RunLauncherDistributionTests.ps1`、`RunVersionFoundationTests.ps1`、`RunPublisherGuiFoundationTests.ps1` |

**Version をビルド前に自動同期する理由**

`Directory.Build.targets` が `BeforeTargets="ClCompile"` で `Sync-Version.ps1` を実行する。

**手で版を上げる運用は必ず忘れる。** ビルドのたびに自動で同期すれば、
バイナリに埋まる版と `engine-version.json` が食い違わない。
共同編集のプロトコル版チェック（R-39）や Migration が版に依存するので、ここがずれると原因不明の不具合になる。

**環境診断を持つ理由（推定）**

配布先の PC で動かないとき、原因の候補が多い（GPU が DX12 非対応、ドライバが古い、
Visual C++ ランタイムが無い、CUDA が無い）。
起動時に診断して `BuildLogs/` へ出せば、**利用者からログを送ってもらえば切り分けられる**。

**弱点**

- 配布時に `.dds` への変換や Shader のプリコンパイルを行っていない（H-3、F-13）。
  最適化の余地が残っている。
- 配布物に Editor コードが含まれる（R-21、G-33）。
- ThirdParty の絶対パス参照（`C:\kogakuin\LE1\CG2\ThirdParty\PhysicsSdk\lib\release`）があり、
  **他マシンでクローンするとリンクできなかった**。**2026-09-29 に修正済み。**
  `Build/PhysicsSdk/PhysicsSdk.props` が既に `$(MSBuildThisFileDirectory)..\..\ThirdParty\PhysicsSdk\` と
  構成別（`debug` / `release`）に正しく解決していたため、`CG2.vcxproj` の `Release|x64` にあった
  絶対パス 1 行は**同じディレクトリを重複指定する冗長な行**だった。削除して props 側へ一本化した。
  Debug / Release / Development の 3 構成で 0 警告 0 エラーを確認。

---

### R-48. Spot Light Shadow（詳細）

R-6 で「Point と同じ位置ベース経路」までしか書けていなかった部分。

**確認できたこと**

- Spot は Point / Area と**共通の位置ベース経路**を通る
  （`EditorRenderManager.cpp:810` 「Point/Spot/Area: position-based, look from light toward center」）。
- 内外角を **cos 値**で渡す（`spotCosInner` / `spotCosOuter`、`:556-557`）。
  既定は内角 20° / 外角 30°（`:376-377`）。
- Shadow は Sun / Point と同じ **5×5 Atlas** から読む
  （`ShadowSampling.hlsli:4` 「Sun(Cascade) / Point(Cube 6面) / Spot を同じAtlasから読む」）。
- Filtering とバイアスは Sun / Point と共通（9-tap PCF、傾斜依存バイアス、R-6）。

**cos 値を CPU 側で計算して渡す理由**

Pixel Shader で角度判定をするなら `acos(dot(...))` が必要になるが、
**cos 値同士の比較なら内積をそのまま比較できる**（`dot(...) > spotCosOuter`）。
逆三角関数を毎ピクセル評価しない。

内外角の2つを持つのは、境界を **smoothstep で滑らかにする**ため
（内角の内側は全光量、外角の外側は 0、間を補間）。

**未検証のまま残る点**

- Spot 専用の Perspective Shadow Map（Spot の FOV に合わせた透視投影）を作っているか、
  それとも Point の Cube の1面を流用しているか。**ここは要確認。**
- Spot Shadow のタイル解像度（Atlas の 1024×1024 タイルを1枚使うのか）。
- Spot Angle と Shadow の投影 FOV の対応（一致させているか、余裕を持たせているか）。

---

### R-49. その他の Manager（未着手だった主要なもの）

`Source/Engine/Editor` に Manager が **56個**ある。R章で扱っていない主要なものの位置づけ。

| Manager | 行数 | 役割 | 備考 |
| --- | ---: | --- | --- |
| `EditorReplayManager` | — | 入力の記録・再生 | `kKeyStateCount = 256` のキー状態を記録。**決定性が前提**（固定刻み物理、R-7）。乱数シードの扱いは未検証 |
| `EditorObjectPoolManager` | — | GameObject の再利用 | 弾・エフェクトの生成/破棄コストを避ける。`PrefabSpawner` と連携 |
| `EditorSaveManager` | — | セーブデータ | `.save` / `.gdata` |
| `EditorSceneOptimizationManager` | 150（Update） | `SimulationLOD` の距離判定 | 距離で更新頻度を落とす（R-35） |
| `EditorConstraintManager` | — | 物理拘束 | Jolt の Constraint ラッパ（推定） |
| `EditorCameraEffectManager` | — | カメラシェイク等 | — |
| `EditorRuntimePropertyManager` | 3,200 | 実行中の Property 編集 | Script API と Inspector の橋渡し |
| `EditorSceneSynchronizer` | 1,329 | GameObject ↔ SceneObject の同期 | データ層と描画層の対応付け |
| `EditorSelectionManager` | — | 選択状態 | Hierarchy / SceneView / Inspector で共有 |
| `EditorTargetingManager` | 1,692 | ロックオン | ゲームプレイ基盤 |
| `EditorWeaponManager` / `WeaponLoadoutManager` | 2,594 / — | 武器 | ゲームプレイ基盤 |
| `EditorDamageManager` / `GameplayEventManager` | — | ダメージ・イベント | ゲームプレイ基盤 |
| `EditorRailMovementManager` / `RailBranchManager` | 2,930 / — | レール移動・分岐 | レールシューター向け |
| `EditorWaveSpawnerManager` | — | 敵の波 | ゲームプレイ基盤 |
| `EditorBlastDestructionManager` | 1,939 | 破壊 | NvBlast のラッパ（R-18） |
| `EditorLogMonitorManager` | — | ログ監視 | Field Registry（2,011 Field）を使った値の監視 |

**Manager が 56個ある構造の評価**

- **利点**: 責務が分かれており、1つの Manager を読めばその機能が分かる。
  `GameScene` が呼ぶ順序も明示的（R-3）。
- **弱点**: Manager 間の依存が `EditorSharedState` の可変グローバル 332個経由なので、
  **どの Manager がどの状態を所有するかが型で表現されていない**（R-1、R-24）。
  56個の Manager が同じグローバルを触りうる。
- ゲームプレイ固有の Manager（Weapon / Rail / Wave / Targeting）が Engine 側にある。
  本来は Script 側（ゲームコード）に置く方が Engine の汎用性が上がる。
  レールシューターという特定ジャンルの制作と並行して作られた結果と推測される。

---

### R-50. 未検証項目（現状・第3版）

| 項目 | 節 |
| --- | --- |
| Spot 専用 Perspective Shadow Map を作っているか / Spot の Shadow 解像度 | R-48 |
| 共同編集のメッセージ分割と再送の実装箇所 | R-39 |
| FidelityFX SDK のどの機能を使っているか（3ファイルが参照） | H-2 |
| `.ogg` のデコード実装の有無 | R-41、H-3 |
| Font が SDF / MSDF かビットマップか | R-46 |
| NavMesh の動的更新対応 | R-44 |
| `EditorAIManager` と `EditorNavigationManager` の責務分担 | R-44 |
| Replay の乱数シード・決定性の担保方法 | R-49 |
| Voice 数の上限管理 | R-41 |
| Snapshot の送信間隔と ChangeLog との切り替え条件 | R-34 |
| 影を落とすライトの Atlas 割り当て順の具体ロジック | R-29 |
| CPU 側 Frustum 判定が Draw のどの段で走るか | R-30 |
| `Compute/GenerateMip.CS.hlsl` が使われているか | G-35 |
| 数学ライブラリがボトルネックかどうか（Profiler で測る） | R-40 |

---

## 行列と回転の数学（M章）

更新基準: 2026-09-29

F-1 で規約（左手系・行ベクトル・行優先）、G-1 で変換の段、G-2 で同次座標、
G-7 で逆転置行列を扱った。M章は**行列そのものの中身**と**回転の表現**を扱う。

「行列の各要素が何を意味するか」「なぜこの順で掛けるか」「オイラー角とクォータニオンの違い」は
面接で直接聞かれる範囲であり、かつ**本エンジンに明確な設計上の弱点がある**領域。

実装は `Source/Engine/Core/Matrix.cpp`（273行）、`Vector&Matrix.cpp`（240行）、
`Vector.cpp`（64行）の計 577行。**完全自作・スカラー実装**（R-40）。

---

### M-1. 行列型と、各基本行列の中身

**型**: `struct Matrix4x4 { float matrix[4][4]; }`。行優先格納、行ベクトル規約（`v * M`）。

本エンジンが持つ関数（`Matrix.cpp` / `Vector&Matrix.cpp`）

| 関数 | 役割 |
| --- | --- |
| `Add` / `Subtract` / `Multiply` | 行列の加減乗 |
| `Inverse` | 逆行列（M-5） |
| `Transpose` | 転置 |
| `MakeIdentity4x4` | 単位行列 |
| `MakeTranslationMatrix` | 平行移動 |
| `MakeScaleMatrix` | 拡縮 |
| `MakeRotateXMatrix` / `Y` / `Z` | 軸ごとの回転 |
| `MakeAffineMatrix` | **SRT 合成**（M-3） |
| `MakePerspectiveFovMatrix` | 透視投影（M-7） |
| `MakeOrthographicMatrix` | 正射影 |
| `MakeViewportMatrix` | ビューポート変換（M-8） |
| `Transform` | ベクトルへの適用（F-1） |

**持っていない関数**

- `MakeViewMatrix` / `LookAt`（行列版）— **View 行列は `EditorRenderManager` 内にインラインで手書き**（M-6）
- クォータニオン関連一式（M-4）
- `Slerp` / 回転の補間（M-10）

#### 平行移動行列

```
| 1   0   0   0 |
| 0   1   0   0 |
| 0   0   1   0 |
| tx  ty  tz  1 |     ← 行ベクトル規約では平行移動が「行3」
```

`v * M` を展開すると `x' = x*1 + y*0 + z*0 + 1*tx = x + tx`。
`w = 1` なので `matrix[3][*]` が効く。**法線（`w = 0`）では効かない**（G-2）。

列ベクトル規約（GL）では平行移動が**列3**（`matrix[0][3]`, `[1][3]`, `[2][3]`）に来る。
**転置の関係**にあるので、格納順まで含めるとバイト列は同じになる（G-26）。

#### スケール行列

```
| sx  0   0   0 |
| 0   sy  0   0 |
| 0   0   sz  0 |
| 0   0   0   1 |
```

対角成分がそのまま倍率。**非一様スケール（sx≠sy≠sz）が法線を壊す**原因（G-7、F-17）。

#### 回転行列（各軸）

**X 軸回転**（`MakeRotateXMatrix`）

```
| 1    0     0    0 |
| 0   cos   sin   0 |
| 0  -sin   cos   0 |
| 0    0     0    1 |
```

**Y 軸回転**

```
| cos   0  -sin   0 |
|  0    1    0    0 |
| sin   0   cos   0 |
|  0    0    0    1 |
```

**Z 軸回転**

```
|  cos  sin   0   0 |
| -sin  cos   0   0 |
|   0    0    1   0 |
|   0    0    0   1 |
```

**符号の位置が規約で変わる**。行ベクトル規約・左手系ではこの配置。
列ベクトル規約（GL）では `sin` と `-sin` が入れ替わる（転置になるため）。
**ここを間違えると回転方向が逆になる**。「回転が逆向きになる」不具合の典型的な原因。

**回転行列が直交行列であることの意味**

各行（各列）が単位ベクトルで互いに直交する。したがって

```
R⁻¹ = Rᵀ
```

**逆行列が転置で求まる**。これは
- View 行列の構成（M-6）
- 法線変換で逆転置が不要になる理由（G-7）

の両方で使われる重要な性質。

---

### M-2. 行列の各行が意味するもの

**World 行列の行 0〜2 は、そのオブジェクトのローカル軸のワールド方向**。

```
| Xx  Xy  Xz  0 |   ← ローカル X 軸のワールド方向（長さ = X のスケール）
| Yx  Yy  Yz  0 |   ← ローカル Y 軸
| Zx  Zy  Zz  0 |   ← ローカル Z 軸
| Tx  Ty  Tz  1 |   ← ワールド位置
```

**この読み方が実用上重要**

- 行 3 を読めば**ワールド位置**が取れる（行列を分解せずに）。
- 行 0〜2 を正規化すれば**各軸の向き**が取れる（前方向・右方向・上方向）。
- 行 0〜2 の**長さがスケール**になる。
- 行 0〜2 の**行列式の符号**で、鏡像かどうかが分かる（負なら反転、G-4 / F-14）。

**本エンジンで実際に使われている例**

`SunPortalLight`（R-25）が `outwardNormal` / `right` / `up` を個別に持つのは、
Portal の矩形のローカル軸をワールド方向として渡すため。行列を渡さず 3 本のベクトルにしている。

Ocean が `oceanWorldAxisX` / `Y` / `Z` を `nointerpolation` で渡すのも同じ考え方（G-3）。

---

### M-3. TRS 合成 — 順序と、変えると何が起きるか

**本エンジンの実装**（`Vector&Matrix.cpp:75-88` `MakeAffineMatrix`）

```cpp
Matrix4x4 rotateXYZ = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
affineMatrix = Multiply(Multiply(scaleMatrix, rotateXYZ), translateMatrix);
```

つまり

```
World = Scale × RotateX × RotateY × RotateZ × Translate
```

**行ベクトル規約（`v * M`）なので、左から順に適用される。**
つまり実際の適用順は **Scale → RotateX → RotateY → RotateZ → Translate**。

これは正しい順序。理由は次の通り。

**なぜ Scale が先か**

Scale を後に掛けると、**回転した後の軸方向に拡縮される**。
「X だけ 2 倍」が意図した方向でなく、回転後の斜め方向に効く。
ローカル軸に沿って拡縮したいので、Scale が先。

**なぜ Translate が最後か**

Translate を先に掛けると、**平行移動した位置を中心に回転・拡縮される**。
原点から離れた位置にあるオブジェクトが、原点を中心に公転する。
「自分の位置で自分の中心を軸に回る」ためには Translate が最後。

**順序を間違えたときに起きる現象（面接で聞かれる形）**

| 誤った順序 | 症状 |
| --- | --- |
| `T × R × S` | オブジェクトが原点を中心に公転する |
| `R × S × T` | 回転後の軸でスケールされ、形が歪む |
| 列ベクトル規約の順序をそのまま使う | 上記が混ざった状態になる |

**回転順序 X → Y → Z の意味と問題**

オイラー角は**3 つの軸回転の合成**だが、**回転は可換でない**。
`RotateX(30°) × RotateY(30°)` と `RotateY(30°) × RotateX(30°)` は**違う姿勢**になる。

したがって「オイラー角 (30, 30, 0)」という値は、**回転順序を決めないと姿勢が定まらない**。
本エンジンは X → Y → Z に固定している。

**これが他ツールとの互換性問題になる**

| ツール | 一般的な順序 |
| --- | --- |
| Unity | Z → X → Y（内部的には ZXY） |
| Blender | 既定 XYZ（変更可能） |
| Maya | 既定 XYZ（変更可能） |
| **本エンジン** | **X → Y → Z** |

FBX からインポートした回転値をそのまま使うと、**元ツールの順序と一致しなければ姿勢が変わる**。
FBX には回転順序の情報が含まれるので、**それを読んで合わせているかは未検証**（R-50 へ追加すべき項目）。

---

### M-4. 回転の表現 — オイラー角 / クォータニオン / 行列

**本エンジンは全面的にオイラー角（`Vector3 rotation`、ラジアン）で保持する。**
クォータニオンは **Jolt との境界でだけ**使い、戻すときに Euler へ変換する。

```cpp
// EditorJoltPhysicsManager.cpp:301-307
JPH::Quat MakeJoltRotation(const Vector3& rotate) {
    // Editor 側の Euler 回転値を Jolt の Quaternion へ変換する
    return JPH::Quat::sEulerAngles(JPH::Vec3(rotate.x, rotate.y, rotate.z));
}
Vector3 MakeEditorRotation(JPH::QuatArg rotation) {
    // Jolt の Quaternion を Editor 側の Euler 回転値へ戻す
    ...
}
```

**3 つの表現の比較**

| 表現 | 要素数 | 補間 | ジンバルロック | 人が読めるか | 合成コスト |
| --- | ---: | --- | --- | --- | --- |
| **オイラー角**（本エンジン） | 3 | **不自然**（最短経路にならない） | **ある** | **読める**（「Y 軸 90 度」） | 3 回の行列積 |
| **クォータニオン** | 4 | **Slerp で最短経路** | ない | 読めない | 乗算 1 回（16 積和） |
| **回転行列** | 9（4×4 なら 16） | 補間できない（直交性が崩れる） | ない | やや読める（M-2） | 行列積 |
| **軸 + 角度** | 4 | 単一軸なら自然 | ない | 読める | 変換が必要 |

#### ジンバルロックとは何か

オイラー角は「3 つの軸を順番に回す」。このとき**中間の軸が ±90° になると、
残り 2 つの軸が同じ回転を表してしまう**。

本エンジンの X → Y → Z 順では、**Y が ±90° のとき** X 回転と Z 回転が同じ効果になる。
つまり **3 自由度のうち 1 つが失われる**。

- 真上・真下を向いたカメラで、左右の回転ができなくなる
- 補間すると、その付近で**急に回転が飛ぶ**

**なぜクォータニオンには起きないか**

クォータニオンは回転を「軸 + 角度」として 4 次元で持つ（`(x, y, z, w)`、
`w = cos(θ/2)`、`(x,y,z) = axis * sin(θ/2)`）。
軸を順番に適用する構造ではないので、特異点が存在しない。

**4 要素で 3 自由度を表す**のが冗長だが、その冗長性が特異点を消している
（球面上の点として連続に動ける）。

#### Quat → Euler 変換の問題（本エンジンが踏んでいる）

**この変換は一意ではない。**

- 同じ姿勢に対して**複数のオイラー角が対応する**（例: `(0, 180, 0)` と `(180, 0, 180)`）。
- `asin` / `atan2` を使うので、**値域が制限される**（通常 Y は -90〜90）。
- ジンバルロック付近では**数値が不安定**になる。

本エンジンは物理から Transform を戻すとき（`MakeEditorRotation`）に毎フレームこの変換をする。
つまり**剛体が回転している間、Euler 値が連続的に変化しない可能性がある**。

具体的な症状として起こりうること:

| 症状 | 原因 |
| --- | --- |
| Inspector の回転値が急に飛ぶ（0 → 359 など） | 変換の値域の折り返し |
| 真上・真下付近で回転が不安定になる | ジンバルロック付近の数値不安定 |
| 物理で回した後の値が元と違う | 変換が一意でない |

**ただし本エンジンでこの症状が実際に出ているかは未検証。**
Jolt 内部はクォータニオンで持っているので、**物理挙動そのものは正しい**。
壊れるのは「Editor に表示される値」と「その値を再度物理へ渡したとき」。

#### なぜオイラー角にしたか（推定）

- **Inspector で人が読める・打ち込める。** 「Y 軸 90 度」と入力できる。
  クォータニオンの `(0, 0.707, 0, 0.707)` を手で打つ人はいない。
- **Serialize が単純。** `Vector3` 1 つで済み、列位置依存フォーマット（R-2）に素直に載る。
- **共同編集の Property 単位差分（R-11）で扱いやすい。** 3 つの float として比較できる。
- クォータニオン一式（積・共役・正規化・Slerp・Euler 変換・軸角変換）を実装する労力。

**多くのエンジンが採る解決策**

Unity / Unreal は**内部はクォータニオン、Inspector 表示だけオイラー角**にしている。
表示用のオイラー角をキャッシュして、ユーザーが触ったときだけクォータニオンへ変換する。
こうすると「読める」と「特異点がない」を両立できる。

**本エンジンの改善候補**

1. `Transform` の内部表現をクォータニオンにし、Inspector 表示だけ Euler へ変換する。
   ただし Serialize 形式（R-2 の列位置依存）と共同編集の Property 経路が変わる。
2. せめて **Jolt からの戻しをクォータニオンのまま保持**する
   （物理で動いている間は Euler へ戻さない）。
3. アニメーションの回転補間（M-10）だけクォータニオンにする。

---

### M-5. 逆行列 — 実装方式と高速化の余地

**本エンジンの実装**（`Matrix.cpp:43-110`、68行）

**ガウス・ジョルダン法（掃き出し法）+ 部分ピボット選択。**

```cpp
float augmented[4][8] = {};          // 左半分に元行列、右半分に単位行列
...
int pivotRow = i;                    // 現在列で絶対値が最大の行を探す
for (int j = i + 1; j < 4; ++j) {
    if (std::fabs(augmented[j][i]) > std::fabs(augmented[pivotRow][i])) {
        pivotRow = j;
    }
}
```

**部分ピボット選択をしている理由**

掃き出し法は「対角成分で割る」操作をする。
対角成分が 0 に近いと**除算で誤差が爆発する**。
各列で絶対値が最大の行を選んで入れ替えれば、除算の分母が最も大きくなり誤差が抑えられる。

**これは数値計算として正しい実装。** 教科書通りの配慮が入っている。

**他の実装方式との比較**

| 方式 | コスト | 特性 |
| --- | --- | --- |
| **余因子展開（クラメル）** | 4×4 で行列式 + 16 個の 3×3 行列式 | 分岐なし。SIMD 化しやすい。**除算 1 回**（1/det） |
| **ガウス・ジョルダン**（本エンジン） | 掃き出し 4 回 + ピボット探索 | 汎用。**分岐がある**（行入れ替え） |
| **アフィン特化** | 回転部分の転置 + 平行移動の変換 | **圧倒的に速い**。ただしアフィン変換に限る |

**アフィン特化が速い理由（重要な最適化余地）**

World 行列や View 行列は**アフィン変換**（最下行が `(0,0,0,1)`）。
この場合、一般の逆行列を解く必要がない。

```
M = | R  0 |        M⁻¹ = |    R⁻¹     0 |
    | T  1 |              | -T·R⁻¹     1 |
```

さらに R が**回転のみ**（スケールなし）なら `R⁻¹ = Rᵀ`（M-1）なので、
**転置と 1 回のベクトル変換だけ**で逆行列が求まる。
掃き出し法の数十倍速い。

**本エンジンでの影響**

View 行列は `Inverse()` を使わず手書きで構成している（M-6）ので、そこは済んでいる。
しかし他の場所で `Inverse()` をアフィン行列に対して呼んでいる箇所があれば、
**そこは特化版に置き換える余地がある**。

一般の `Inverse()` が必要なのは、射影行列の逆（スクリーン座標からワールドへ戻す、
SSR / SSGI / DOF などで使う）くらい。

---

### M-6. View 行列 — 作り方と導出

**本エンジンは `MakeViewMatrix` 関数を持たず、`EditorRenderManager` 内で手書きしている**
（`EditorRenderManager.cpp:734-741` 付近）。

```cpp
Matrix4x4 viewMatrix{};
viewMatrix.matrix[0][0] = xAxis.x;
viewMatrix.matrix[0][1] = yAxis.x;
viewMatrix.matrix[0][2] = zAxis.x;
viewMatrix.matrix[0][3] = 0.0f;
viewMatrix.matrix[1][0] = xAxis.y;
viewMatrix.matrix[1][1] = yAxis.y;
viewMatrix.matrix[1][2] = zAxis.y;
...
```

**これが何をしているか**

カメラの基底ベクトル（`xAxis` = 右、`yAxis` = 上、`zAxis` = 前）を、
**列方向に並べている**（`matrix[0][0]=x.x, [0][1]=y.x, [0][2]=z.x` は基底を列に置く配置）。

つまり**カメラの回転行列の転置**を作っている。

**なぜ転置か（導出）**

View 変換の目的は「ワールド座標を、カメラを原点・軸に合わせた座標系へ移す」。
これは**カメラのワールド変換の逆変換**。

カメラのワールド行列を `M_cam = R × T`（回転してから移動）とすると、

```
View = M_cam⁻¹ = (R × T)⁻¹ = T⁻¹ × R⁻¹
```

R は回転行列なので直交 → `R⁻¹ = Rᵀ`（M-1）。
`T⁻¹` は平行移動の符号反転。したがって

```
View の回転部分 = Rᵀ            ← 基底を転置して置く
View の平行移動 = -eye · 各基底  ← eye をカメラ基底へ射影して符号反転
```

**LookAt の構成手順（一般形）**

```
zAxis = normalize(target - eye)        // 前方向（左手系なので target - eye）
xAxis = normalize(cross(up, zAxis))    // 右方向
yAxis = cross(zAxis, xAxis)            // 上方向（再計算して直交を保証）
```

`yAxis` を**再計算する理由**: 与えられた `up` は `zAxis` と直交していない場合がある
（カメラを上下に振ると `up` が前方向と平行に近づく）。
外積で作り直せば必ず直交する。

**右手系（GL）では `zAxis = normalize(eye - target)`** と符号が逆になる。
左手系は +Z が奥なので、前方向が `target - eye`（G-26）。

**`MakeViewMatrix` 関数が無いことの評価**

- 弱点: View 行列の構成が `EditorRenderManager::Draw()`（5,190行）の中にあり、
  再利用されていない。Shadow の Light View、Planar Reflection のミラー View、
  Probe Capture の 6 面 View がそれぞれ別に書かれている可能性がある。
- `MakeViewMatrix(eye, target, up)` として `Matrix.cpp` へ切り出せば、
  少なくとも 4 箇所で共有できるはず（Camera / Shadow / Planar / Probe）。

---

### M-7. 射影行列 — 各要素の導出

**本エンジンの実装**（`Matrix.cpp:177-188`）

```cpp
float f = 1.0f / std::tan(fovY / 2.0f);
result.matrix[0][0] = f / aspect;
result.matrix[1][1] = f;
result.matrix[2][2] = farZ / (farZ - nearZ);
result.matrix[2][3] = 1.0f;
result.matrix[3][2] = (-nearZ * farZ) / (farZ - nearZ);
```

#### `matrix[1][1] = 1/tan(fovY/2)` の導出

視野角 `fovY` の半分の角度を持つ直角三角形を考える。
距離 `z` にある高さ `h` の物体が、視野の端に来る条件は

```
tan(fovY/2) = h / z
```

これを NDC の範囲 `[-1, 1]` へ写したい。つまり視野の端が ±1 になってほしい。

```
y_ndc = y_view / (z_view · tan(fovY/2)) = (y_view / z_view) · (1/tan(fovY/2))
```

透視除算で `z_view`（= w）で割られるので、行列には `1/tan(fovY/2)` を置けばよい。

#### `matrix[0][0] = f / aspect` の理由

画面は横長（aspect = width/height > 1）。
縦の視野角を基準にすると、**横は aspect 倍広く見える**。
NDC は縦横ともに `[-1, 1]` なので、横方向を `1/aspect` して縮める必要がある。

**つまり `fovY` を基準にしている**。`fovX` 基準なら `matrix[0][0] = f`、`matrix[1][1] = f * aspect` になる。
どちらを基準にするかで、画面比率を変えたときの見え方が変わる
（縦基準なら横が広がる、横基準なら縦が狭まる）。

#### `matrix[2][3] = 1.0f` — 透視の本体

これで `clip.w = z_view`。**この 1 行が透視除算の入力を作っている**（G-2）。

`w` が正になるのは `z_view` が正のとき → **左手系**（F-1）。
右手系（GL）では `matrix[2][3] = -1.0f` になる。

#### `matrix[2][2]` と `matrix[3][2]` — 深度のマッピング

`z_clip = z_view · m[2][2] + 1 · m[3][2]`、`w = z_view` なので

```
z_ndc = m[2][2] + m[3][2] / z_view
```

条件は 2 つ。

- `z_view = near` のとき `z_ndc = 0`
- `z_view = far` のとき `z_ndc = 1`

代入して解くと

```
m[2][2] = far / (far - near)
m[3][2] = -near · far / (far - near)
```

**実装と一致する。** 検算すると

- `z = near`: `far/(far-near) - near·far/((far-near)·near) = far/(far-near) - far/(far-near) = 0` ✓
- `z = far`: `far/(far-near) - near·far/((far-near)·far) = (far - near)/(far - near) = 1` ✓

**GL の場合**は `[-1, 1]` へ写すので

```
m[2][2] = (far + near) / (far - near)
m[3][2] = -2·near·far / (far - near)
```

**この式の違いが G-26 の「深度範囲の違い」の実体**。
GL の行列を D3D で使うと near 付近の物体が `z < 0` でクリップされて消える。

#### 深度が `1/z_view` に比例することの帰結

`z_ndc = m[2][2] + m[3][2]/z_view` の形から、**深度は `z_view` の逆数に比例する**。
これが G-5 の深度精度の分布（手前に密、奥に粗）の原因。

**Reverse-Z は `near` と `far` を入れ替えるだけ**で、
`z = near → 1`、`z = far → 0` になる。式の形は変わらない。

---

### M-8. Viewport 行列

**本エンジンは `MakeViewportMatrix` を持つ**（`Matrix.cpp:214`）。

NDC `[-1, 1]` をピクセル座標へ写す。

```
x_screen = (x_ndc + 1) · width/2  + left
y_screen = (1 - y_ndc) · height/2 + top     ← Y が反転する
z_screen = z_ndc · (maxDepth - minDepth) + minDepth
```

**Y が反転する理由**

NDC は**上が +1**。しかしスクリーン座標は**上が 0**（左上原点）。
だから `(1 - y_ndc)` で反転する。

これは**テクスチャ原点の違い（G-26）とは別の話**だが、混同しやすい。

| | 方向 |
| --- | --- |
| NDC の Y | 上が +1（GL / D3D 共通） |
| スクリーン座標の Y | 上が 0（GL / D3D 共通） |
| **テクスチャの V** | **GL は下が 0、D3D は上が 0**（ここだけ違う） |

**`MakeViewportMatrix` が実際に使われているか**

D3D12 では `RSSetViewports` でハードウェアが Viewport 変換を行うので、
**行列として持つ必要は通常ない**。

本エンジンで `MakeViewportMatrix` が残っているのは、
**CPU 側でスクリーン座標を計算する用途**（デバッグ描画、ギズモの当たり判定、
マウスピッキング）と推測される。実際の呼び出し箇所は未検証。

---

### M-9. Transform 階層の合成

**原理**: 子のワールド行列 = 子のローカル行列 × 親のワールド行列。

```
World_child = Local_child × World_parent
```

行ベクトル規約なので**子が左**。列ベクトル規約（GL）では `World_parent × Local_child` と逆になる。

**再帰的に適用される**

```
World = Local_self × Local_parent × Local_grandparent × ... × Identity
```

**実装上の要点**

| 要点 | 理由 |
| --- | --- |
| **親から順に計算する** | 子は親のワールド行列を必要とする。順序を保証しないと 1 フレーム遅れる |
| **毎フレーム再計算するか、dirty のときだけか** | 全再計算は単純だがオブジェクト数に比例。dirty 管理は速いが親の変更を子へ伝播させる必要がある |
| **親のスケールが子に掛かる** | 非一様スケールの親を持つ子は、法線が壊れる（G-7）。スケールの継承を切る選択肢もある |

**本エンジンの実装**

`EditorSceneSynchronizer`（1,329行）が GameObject → SceneObject の同期を担い、
`sceneObject.worldMatrix = MakeAffineMatrix(...)` でワールド行列を作る
（`EditorSceneSynchronizer.cpp:1022`）。

**階層の合成順序と dirty 管理の詳細は未検証**（R-50 へ追加すべき項目）。
`MakeAffineMatrix` を直接呼んでいる箇所があるので、
**親子の合成をしている経路と、していない経路が混在している可能性がある**。

---

### M-10. 回転の補間 — Lerp と Slerp

**問題**: 2 つの姿勢の間を滑らかに繋ぎたい（アニメーションの Blend、カメラの追従）。

#### オイラー角の線形補間（本エンジンが持つ唯一の手段）

```
rotation = lerp(rotationA, rotationB, t)     // 3 成分を独立に補間
```

**問題が 3 つある。**

1. **最短経路にならない。** `(0, 0, 0)` から `(0, 350°, 0)` へ補間すると、
   350° 分回る。実際の最短は -10°。
2. **各成分を独立に補間するので、中間の姿勢が意図しないものになる。**
   回転は可換でない（M-3）ので、成分ごとの線形補間は「回転の間を通る」保証がない。
3. **ジンバルロック付近で破綻する**（M-4）。

#### クォータニオンの Slerp（球面線形補間）

```
Slerp(qA, qB, t) = (sin((1-t)θ) · qA + sin(tθ) · qB) / sin(θ)
       ただし cos(θ) = dot(qA, qB)
```

**利点**

- **最短経路を通る。** `dot(qA, qB) < 0` なら片方を反転すれば常に短い側を通る
  （クォータニオンは `q` と `-q` が同じ姿勢を表すため）。
- **角速度が一定。** 補間中の回転速度が変わらない。
- 特異点がない。

**Nlerp（正規化線形補間）という中間案**

```
Nlerp(qA, qB, t) = normalize(lerp(qA, qB, t))
```

Slerp より安く、最短経路も通る。**角速度が一定でない**（両端で速く、中央で遅い）が、
差が小さい場合は実用上問題にならない。アニメーションの Blend で多用される。

#### 本エンジンのアニメーション Blend（R-33）への影響

`EditorAnimationManager` は `Blend1D` / `Directional` / `Cartesian` / `Direct` の
4 種の Blend Tree を持ち、`SampledTransform` を混ぜる。

**回転をどう混ぜているかは未検証だが、クォータニオンを持たない（M-4）ので
オイラー角の線形補間である可能性が高い。**

もしそうなら
- 大きな角度差のあるアニメーション間の Blend で、**遠回りする**
- 真上・真下を向くアニメーションで**破綻する**

**これは確認すべき優先度が高い項目。** アニメーションの品質に直接出る。

**State Machine の Transition も同じ**（`transitionTime` / `transitionDuration` で 2 つの Pose を混ぜる、R-33）。

#### 位置とスケールは線形補間で正しい

位置は 3 次元ユークリッド空間の点なので、線形補間が最短経路。
スケールも同様（ただし 0 を跨ぐ場合や、対数補間が自然な場合はある）。

**回転だけが特別**なのは、回転の集合が「平坦な空間」ではなく
**球面（SO(3)）**だから。球面上の 2 点を直線で結ぶと球面から外れる。
だから球面上を通る補間（Slerp）が必要になる。

---

### M-11. 数学ライブラリの死んだコード（2026-09-29 削除済み）

> **2026-09-29 に削除した。** 以下は削除前の状態と、削除した理由。

**削除前**: `Novice`（学習用 2D ライブラリ）への参照が 3 ファイルに 11 箇所残っていた。

| ファイル | 内容 |
| --- | --- |
| `Source/Engine/Core/Matrix.cpp` | `// Novice::DrawLine(...)`（2 箇所） |
| `Source/Engine/Core/Vector&Matrix.cpp` | コメントアウトされた `DrawSphere`（約 50 行）。`Novice::DrawLine` を使う |
| `Source/Engine/Core/Vector.cpp` | `// Novice::ScreenPrintf(...)`（3 箇所。Vector の画面表示用） |

**全部コメントアウトされているのでコンパイルには影響しない。**
ただし
- `Novice` は学習課題用のライブラリで、**このエンジンには存在しない依存**。
  読む人が「Novice を使っているのか」と誤解する。
- `DrawSphere` 50 行は**デバッグ描画の残骸**。
  現在のデバッグ描画は `EditorVfxRenderer` や ImGui 側にある。

**削除内容**: `Novice` を含むコメント行とその連続コメントブロックを削除した。
すべて `//` で始まる死んだコードなので、**コンパイル結果は変わらない**。

| ファイル | 削除行数 | 変化 |
| --- | ---: | --- |
| `Source/Engine/Core/Vector.cpp` | 5 | 65 → 60 行 |
| `Source/Engine/Core/Matrix.cpp` | 37 | 274 → 237 行 |
| `Source/Engine/Core/Vector&Matrix.cpp` | 104 | 241 → 137 行 |

**削除しても関数が消えていないことを確認済み**:
`Matrix.cpp` の 12 関数、`Vector&Matrix.cpp` の 9 関数がすべて残っている。
Debug / Release / Development の 3 構成で 0 警告 0 エラー。

**なぜ削除したか**: `Novice` はこのエンジンに存在しない依存。
読む人が「Novice を使っているのか」と誤解する。
`Vector&Matrix.cpp` は 241 行のうち 104 行（43%）が死んだコメントだった。

**あわせて `Vector&Matrix.cpp` の名前**: ファイル名に `&` を含むため、
`CG2.vcxproj` では `Vector&amp;Matrix.cpp` と XML エスケープされている。
ツールやスクリプトでパスを扱うときに引っかかりやすい
（実際にこのセッションの調査で 1 度誤検出した）。

---

### M-12. 行列まわりで未検証の項目

| 項目 | なぜ重要か | 節 |
| --- | --- | --- |
| **アニメーション Blend の回転補間方式** | オイラー角の線形補間だと大角度差で遠回りし、真上真下で破綻する | M-10、R-33 |
| **FBX の回転順序情報を読んで合わせているか** | 元ツールと順序が違うと姿勢が変わる | M-3 |
| **Transform 階層の合成順序と dirty 管理** | 親から順に計算していないと 1 フレーム遅れる | M-9 |
| **`Inverse()` をアフィン行列に対して呼んでいる箇所** | アフィン特化で数十倍速くなる余地 | M-5 |
| **`MakeViewportMatrix` の呼び出し箇所** | D3D12 では通常不要。CPU 側ピッキング用か | M-8 |
| **Shadow / Planar / Probe の View 行列構成が重複していないか** | `MakeViewMatrix` が無いので個別に書かれている可能性 | M-6 |
| **Quat → Euler 変換で Inspector の値が飛ぶ症状が実際に出るか** | 物理で回した後の値の連続性 | M-4 |

---

## 3D基礎の補足（G-37以降）

ここまでは描画方式と行列を中心に説明した。この節では、特定のEngineに依存せず、3Dプログラミングの面接で前提として確認されやすい数学と更新方式をまとめる。一般原理とCG2Engineの現状を混同しないこと。

### G-37. ベクトル、内積、外積、正規化

**点とベクトルの違い**

- 点は空間内の位置、ベクトルは向きと大きさを表す。
- 2点`A`、`B`の差`B - A`は、AからBへ向かうベクトルになる。
- 行列の同次座標では点を`w=1`、方向を`w=0`として区別する。方向には平行移動を適用しない。

**内積**

```text
dot(a, b) = ax*bx + ay*by + az*bz = |a||b|cos(theta)
```

- 正なら同じ側、0なら直交、負なら反対側を向く。
- `dot(normal, lightDirection)`はLambert拡散、`dot(forward, targetDirection)`は視野判定に使える。
- 単位ベクトル同士なら内積がそのまま角度のcosになる。正規化していない場合は長さの影響を受ける。
- ベクトル`v`を単位軸`n`へ射影する成分は`dot(v,n)*n`。速度を面の法線成分と接線成分へ分けるときにも使う。

**外積**

```text
cross(a, b) = aとbの両方に垂直なベクトル
|cross(a,b)| = |a||b|sin(theta)
```

- Cameraのright/up/forward基底、三角形法線、Torqueの`r × F`に使う。
- 引数順を逆にすると符号が反転する。左手系・右手系という名前だけで順番を決めず、Engineの座標規約と実際の式を確認する。

**正規化の注意**

- `normalize(v)=v/|v|`。方向だけを使う計算では必要だが、元の長さは失われる。
- 長さ0のベクトルを割るとNaNになる。`lengthSquared`を小さい閾値と比較してから正規化する。
- 距離比較だけなら平方根を取らず、距離の2乗同士を比較した方が軽い。

**CG2Engineとの接続**: `Vector.cpp`と`Vector&Matrix.cpp`にDot、Cross、Length、Normalize等の自作実装がある。座標規約はF-1、Camera基底はM-6、法線変換はG-7を参照する。

### G-38. Ray、平面、三角形、Bounding Volume

**Rayの式**

```text
P(t) = origin + direction * t,  t >= 0
```

`direction`を正規化しておけば`t`を距離として扱える。正規化しない場合、交差位置は求められても`t`の意味が変わる。

| 判定 | 基本方式 | 長所 | 注意点 |
| --- | --- | --- | --- |
| Ray / Plane | 平面式へRayを代入して`t`を解く | 最小の計算量 | 平行に近いと分母が0へ近づく |
| Ray / Sphere | 二次方程式または最近点距離 | 回転を考えなくてよい | 細長い物体を過大評価する |
| Ray / AABB | 各軸の進入・退出`t`を求めるSlab法 | 高速 | World回転を含む形には緩いBoundsになる |
| Ray / OBB | RayをBoxのLocal空間へ変換してAABB判定 | 回転物体へ密着する | 逆行列と座標変換が必要 |
| Ray / Triangle | Moller-Trumbore等で重心座標を解く | 実Mesh表面を判定できる | 三角形数に比例するので加速構造が必要 |

**Broad PhaseとNarrow Phase**

1. Broad PhaseでSphere、AABB、BVH等を使い、当たる可能性のない対象を大量に除外する。
2. Narrow Phaseで実Colliderや三角形同士の詳細判定を行う。
3. 接触点、法線、貫通量を求め、Solverへ渡す。

すべての三角形へ直接判定する方式は正確だが、Object数×Triangle数まで増える。Bounding Volumeは精度のためではなく、詳細判定の候補を減らすために使う。

**CG2Engineとの接続**: 物理衝突はJoltへ委譲し、描画CullingではAABBの8頂点を使う（R-7、R-30、G-23）。画面からWorldへのPicking RayはView/Projectionの逆変換を使うため、M-5～M-8と同じ座標変換の理解が必要になる。

### G-39. 浮動小数点、誤差、座標スケール

- `float`は値が大きくなるほど隣り合う表現可能値の間隔が広がる。巨大なWorld座標では、小さな移動や接触点を正確に表せない。
- `a == b`で計算結果を比較せず、用途に応じた絶対誤差または相対誤差を使う。ただしID、整数化された状態、明示的に代入した0まで無条件にepsilon比較へ変える必要はない。
- 正規化、逆行列、Rayの平行判定では、0除算とNaNを防ぐ閾値が必要。
- CameraのNear Clipを極端に小さくし、Far Clipを極端に大きくするとDepth精度が悪化する。詳細はG-5。
- 大規模WorldではOrigin Rebasing、Camera Relative Rendering、倍精度座標などを検討する。採用するとPhysics、Particle、Network座標、保存形式まで影響する。
- 決定論が必要なReplayやNetwork同期では、CPU命令、並列順序、浮動小数点の丸め差も考慮する。見た目の一致とBit単位の一致は別の要件である。

### G-40. 時間刻み、積分、補間

| 方式 | 処理 | 長所 | 弱点 |
| --- | --- | --- | --- |
| Variable Delta Time | 毎Frameの`deltaTime`で更新 | 実時間へ追従しやすい | Frameごとに結果が変わり、物理が不安定になりやすい |
| Fixed Time Step | 一定秒でSimulationを進める | PhysicsとReplayの再現性を上げやすい | 遅れたFrameでCatch-upが必要 |
| Fixed + Render補間 | 固定更新結果2点の間を描画時に補間 | 滑らか | 状態を2世代保持する必要がある |

**積分方式**

- Explicit Euler: `position += velocity*dt`後に速度を更新する。簡単だがEnergyが増えやすい。
- Semi-Implicit Euler: 速度を先に更新し、その速度で位置を更新する。ゲーム物理でよく使われ、同じコストで比較的安定する。
- Verlet / RK系: 軌道精度を上げられるが、状態量や計算回数が増える。剛体接触Solver全体の安定性は積分器だけでは決まらない。

**CG2Engineとの接続**: Physicsは固定`1/60秒`だが、重いFrameで複数Stepを進めるAccumulator/Catch-upを持たない（R-3）。面接では「固定更新だから完全に実時間と一致する」とは答えず、遅延時の弱点まで説明する。

---

## プログラミングとC++の面接基礎（P章）

この章はCG2Engine固有APIの暗記ではなく、実装を説明するために必要な一般原理を扱う。各節の最後にCG2Engineとの接続先を示す。

### P-1. 値、参照、Pointer、`const`

| 形 | 意味 | 主な用途 | 注意点 |
| --- | --- | --- | --- |
| 値 | ObjectをCopyして所有 | 小さいPOD、独立したSnapshot | 大きいObjectはCopy Costが出る |
| `T&` | 必ず存在する別Objectへの参照 | 必須の入出力引数 | 参照先より長く保持してはいけない |
| `const T&` | Copyせず読取だけ | 大きい入力Object | 非同期処理へ持ち越すと寿命確認が必要 |
| `T*` | nullを表現できる参照 | 任意依存、非所有参照、C API | 所有者と寿命が型だけでは分からない |
| `const T*` | null可能な読取参照 | 任意の入力 | Pointer自体の有効期間は別問題 |

- `const`は「絶対に変わらない」ではなく、その経路から変更しないという契約。
- 参照やPointerをMemberへ保存する場合、参照先が先に破棄されない所有関係が必要。
- Scene差し替え後も古い`EditorGameObject*`を保持するとDangling Pointerになる。長期参照にはUUIDや再検索可能なHandleを使う。

### P-2. Stack、Heap、RAII、所有権

**Stack**はScope終了で自動破棄され、確保が軽い。サイズと寿命が局所的な値に向く。**Heap**は動的なサイズ・寿命を扱えるが、確保解放Cost、断片化、所有権管理が必要になる。

**RAII**はResource取得をObjectの生成、解放をDestructorへ結び付ける考え方。途中returnや例外でも解放漏れを防ぐ。

| 表現 | 所有権 | 使う場面 |
| --- | --- | --- |
| 値Member | 親Objectが直接所有 | 寿命が同じ、小～中サイズ |
| `std::unique_ptr<T>` | 所有者1つ | Backend、Tree Node、移譲可能なResource |
| `std::shared_ptr<T>` | 参照数で共有所有 | 本当に寿命を共有する場合だけ |
| `std::weak_ptr<T>` | shared所有へ参加しない | 循環参照を避ける観測者 |
| raw pointer / reference | 原則として非所有 | 外部所有Objectを一時参照 |
| Handle + Generation | IDで間接参照 | GPU Resource、Physics Body、Runtime Voice等 |

`shared_ptr`は安全装置ではない。循環参照、Atomicな参照数更新Cost、破棄Threadの不定化がある。所有者が1つなら`unique_ptr`、所有しないならraw pointerまたはHandleの方が契約を表しやすい。

**CG2Engineとの接続**: D3D COM Objectは`ComPtr`とraw pointerが混在し、共有状態のResource寿命が読みづらい（F-8）。ScriptやEffectは無効化可能なHandleを使い、Scene保存へRuntime Pointerを書かない。

### P-3. Copy、Move、Object Lifetime

- Copyは元と先が独立して使える状態を作る。Resource Handleを単純Copyすると二重解放や同一Resourceの意図しない共有が起きる。
- Moveは所有権を移し、元Objectを有効だが未規定の状態へ置く。`vector`拡張や関数の戻り値で重要になる。
- Rule of Zero: 標準ContainerやRAII型だけで構成し、Destructor/Copy/Moveを自作しない形が最も安全。
- Rule of Five: raw Resourceを直接所有する型はDestructor、Copy constructor、Copy assignment、Move constructor、Move assignmentの整合を考える。
- `vector`の再確保後は、要素を指していたPointer、Reference、Iteratorが無効になる。GameObject配列へ要素を追加した後も古いPointerを保持しない。

### P-4. 計算量とContainer選択

Big-Oは入力数が増えたときの増え方を示す。実時間は定数Cost、Cache Miss、Allocationにも左右されるが、大量Object時の設計比較に必要になる。

| Container / 処理 | 代表的Cost | 向く用途 | 注意点 |
| --- | --- | --- | --- |
| `vector`末尾追加 | 平均O(1) | 順次走査、密なObject配列 | 再確保でPointer無効化 |
| `vector`途中削除 | O(n) | 削除が少ない配列 | 後続要素を移動する |
| `unordered_map`検索 | 平均O(1) | UUID→Index等 | 最悪O(n)、MemoryとHash Cost |
| `map`検索 | O(log n) | 順序が必要 | Node AllocationとCache Miss |
| 全Object二重Loop | O(n^2) | 小規模だけ | Object数増加で急激に重くなる |
| Sort | O(n log n) | Render Queue、状態順 | 毎Frame実行するならKey生成もCost |

**CG2Engineとの接続**: Sceneは`vector`で順次処理し、ID検索はHash索引を併用する（R-2）。描画Culling、Broad Phase、Object Poolは、全組み合わせや毎回生成を避けるための仕組みとして説明できる。

### P-5. 継承、多態、Composition、Data-Oriented Design

- 継承は「is-a」を表し、共通Interfaceと動的Dispatchが必要な場合に有効。
- Compositionは小さい機能を組み合わせ、「has-a」を表す。機能の組み替えに強い。
- `virtual`呼び出しはvtable経由の間接分岐になる。多くの場合その1回より、ObjectがHeap上へ散らばることによるCache Missの方が大きい。
- Data-Oriented Designは処理単位で必要Dataを連続配置し、CacheとSIMDを活かす。ECSはその一方式だが、ECSを使えば自動的に高速になるわけではない。
- `std::variant`やtagged unionは閉じた型集合を型安全に表せるが、型追加時にVisitor全体の更新が必要。

**CG2Engineとの接続**: Componentは継承型Objectではなく、単一`EditorComponent`と`type`で表す（R-1、G-27）。Inspector・Serialize・Undo・共同編集へ同じDataを流せる一方、全Componentが全Field分のMemoryを持ち、型安全性が弱い。

### P-6. Cache、Allocation、AoS / SoA

CPUは演算よりMemory待ちが支配的になることがある。連続Memoryを順番に読む`vector`は、Pointerを辿るNode構造よりCacheへ載りやすい。

| Layout | 例 | 長所 | 弱点 |
| --- | --- | --- | --- |
| AoS | `{position, velocity, color}`の配列 | 1Objectをまとめて扱いやすい | positionだけ読む処理でも他FieldをCacheへ読む |
| SoA | position配列、velocity配列、color配列 | 同じFieldの一括処理、SIMDに向く | 1Objectの編集・Serializeが複雑 |

- 毎Frameの`new/delete`、一時`vector`生成、文字列結合はAllocationと同期を増やす。
- `reserve`、Object Pool、Frame Allocator、容量再利用で回数を減らせる。
- 最適化前にProfilerで回数、時間、Allocation量を測る。Containerを変えただけで速いと断定しない。

### P-7. Thread、Race Condition、同期

- Race Conditionは複数Threadが同じ状態へ同期なしでAccessし、実行順によって結果が変わる状態。
- Data RaceはC++では未定義動作。たまたま動くFrameがあっても正しくない。
- Mutexは複合状態を守れるが、Lock競合、順序逆転によるDeadlock、長いCritical Sectionが問題になる。
- Atomicは単一値の同期に向く。複数値の整合性やContainer全体を自動では守らない。
- Condition VariableはPollingを避けて待機できる。Predicateを再確認し、Spurious Wakeupへ対応する。
- Job Systemは仕事を小分けしてWorkerへ配る。依存関係、完了Fence、書込先の分離が必要。
- Double Bufferは読取中Dataと書込中Dataを分ける。1Frame遅延とMemory増加との交換になる。

**CG2Engineとの接続**: Main Loop、Renderer、Physicsは原則Main Threadで、Camera CaptureとOnline Worker等だけを分離している（R-24）。全部を並列化しない理由は、Sceneの可変共有状態とManager間の実行順依存が大きく、Raceを避ける同期Costが先に増えるため。

### P-8. CPU / GPU非同期とFence

- Command Listへ記録した時点ではGPU処理は完了していない。
- FenceはCPUとGPUの完了点を結ぶ。Resourceを再利用・解放する前に、そのResourceを使うGPU処理の完了を確認する。
- 毎FrameCPUがGPU完了を待つと並列性が消える。Frame Resourceを2～3世代持ち、Fence値でRing管理するとCPUとGPUを重ねやすい。
- ReadbackはGPU→CPU転送と同期を伴う。結果を同じFrameで必要とするとPipeline Stallになりやすい。
- UAV BarrierはResource State遷移とは別に、UAV書込間の順序と可視性を保証する。

CG2Engineの具体的なCommand Queue、Barrier、Readback、ExecuteIndirectはF-4、F-7、R-9を参照する。

### P-9. API、ABI、Versioning

**API**は呼び出し方や意味の契約。**ABI**はBinary上の関数名、Calling Convention、構造体Layout、Alignment、vtable、Symbol等の契約。

- Headerがコンパイルできるだけでは、古いDLLとのABI互換性は保証されない。
- 関数Pointer TableへEntryを追加する場合、既存Entryの順序を変えず末尾へ追加すれば古いOffsetを維持しやすい。
- 構造体Memberの途中追加、型変更、Packing変更は後続Offsetを変える。
- Version番号だけでなく、旧Versionがどの範囲まで安全に動くかを定義する。
- 保存形式は未知Fieldを無視するだけでなく、再保存時に未知Dataを保持するかも互換性に影響する。

**CG2Engineとの接続**: Script Runtime APIは関数Table末尾追加、必要API Version、旧DLL探索で互換性を維持する。Sceneは列位置を変えず末尾またはExtension行へ追加する（R-2、R-23）。

### P-10. Error Handling、Assertion、診断

| 手段 | 用途 | 本番での扱い |
| --- | --- | --- |
| 戻り値 / Result | 予想可能な失敗 | 呼出側が処理する |
| Exception | 失敗を上位へ伝播 | Engine方針とDLL境界を決める |
| `assert` | Programmer Error、不変条件 | Releaseで消える場合がある |
| Log | 後から経路を追う | Error原因と対象IDを残す |
| Crash Dump | 継続不能な障害の解析 | 発生時点のStack等を保存 |

- File欠損、Device未接続、Network切断は起こり得る状態であり、assertだけで処理しない。
- HRESULTを`assert(SUCCEEDED(...))`だけで確認するとReleaseで検査が消える。CG2Engineは`EDITOR_HR_OK` / `EDITOR_HR_VERIFY`を使う。
- Null Guardを追加するだけでは原因を直したことにならない。入力、所有者、寿命、失敗したAPIとError Codeを記録する。
- Build成功はRuntime、描画結果、性能、2台通信の成功を意味しない。検証した範囲を分けて報告する。

### P-11. Test、再現、Profilerを使った問題分解

1. 症状を再現する最小条件を固定する。
2. 正常／異常のCommit境界、入力、Scene、設定を記録する。
3. 仮説ごとに観測値を追加する。衝突ならRay、Collider中心・寸法、候補判定、最終Hitを出す。
4. CPU/GPU、Update/Draw、Load/Runtimeなど大きな区分をProfilerで分離する。
5. 修正後は元の症状だけでなく、境界値、失敗経路、旧Data互換を確認する。

**Unit Test**は小さい関数、**Integration Test**は複数Managerや保存読込、**Play Test**は入力・描画・時間依存を確認する。どれか1つで全部を証明したことにはならない。

性能では平均だけでなく最大値、Percentile、単発Spikeを見る。計測自体のOverheadを把握し、最適化前後で同じScene・解像度・Frame範囲を比較する。

### P-12. 設計原則とPatternを使う判断

- Single Responsibilityは「Classを小さくすること」ではなく、変更理由を一つへ寄せること。
- Dependency Inversionは上位方針が具体Backendへ直接依存せず、Interfaceへ依存すること。Backend差し替えが無い箇所へ無理にInterfaceを増やすと複雑になる。
- Command PatternはUndo/Redoと相性がよいが、全状態Snapshotの方が単純で安全な規模もある。
- Observer/Eventは送信側と受信側を分離できるが、実行順と購読解除が見えにくくなる。
- Factoryは生成規則を集約できる。型ごとに生成処理が全く違う場合に有効だが、単純なconstructorまで隠す必要はない。
- Object Poolは生成Costを均すが、状態Reset漏れと世代違いのHandleが問題になる。
- Singleton/Global StateはAccessが容易だが、依存関係、Test、初期化順、並列化を難しくする。

Pattern名を答えることより、「どの変更を容易にするために導入し、何を複雑にしたか」を説明する方が重要である。

### P-13. AI模擬面接で確認する質問

| 分野 | 基本質問 | 深掘り |
| --- | --- | --- |
| 3D数学 | 内積と外積をどこで使うか | 0ベクトル、座標系、正規化Cost |
| Transform | World/View/Projectionを説明 | 掛け順、親子、法線の逆転置 |
| Camera | PerspectiveとDepthを説明 | Near/Far、Reverse-Z、Picking Ray |
| Culling | FrustumとOcclusionの違い | Bounds、False Positive、CPU/GPU役割 |
| Physics | Broad/Narrow/Solverを説明 | Fixed Step、積分、Transform同期 |
| Memory | 所有権をどう決めるか | RAII、Pointer無効化、Handle世代 |
| Container | vectorとHashをどう選ぶか | O記法、Cache、再確保 |
| Thread | 何を並列化できるか | Race、Lock範囲、Double Buffer |
| API | APIとABIの違い | DLL互換、構造体末尾追加 |
| Debug | Crashや当たり判定をどう切り分けるか | 観測値、再現条件、検証範囲 |
| 設計 | Component方式を選んだ理由 | 継承/ECSとの比較、弱点 |
| 最適化 | 何をどう改善したか | 計測条件、Before/After、副作用 |

AI面接官には、回答が一般論だけなら「CG2EngineではどのClassと処理が対応するか」、Engine固有の暗記だけなら「一般方式と代替案は何か」を追加質問させる。これにより、文書の読み上げではなく理解を確認できる。

---

## 3D基礎の追加深掘り（G-41以降）

### G-41. Mesh、頂点、Index、Topology

Meshは頂点の集合だけではなく、**頂点をどう接続して面を作るか**まで含む。

| 要素 | 役割 | 典型的な内容 |
| --- | --- | --- |
| Vertex Buffer | 頂点属性 | Position、Normal、UV、Color、Tangent、Bone Index/Weight |
| Index Buffer | 頂点を参照する番号 | 同じ頂点を複数三角形で共有する |
| Primitive Topology | 頂点の解釈 | Triangle List、Line List、Triangle Strip等 |
| Submesh | 同一Mesh内の描画単位 | MaterialやIndex範囲が異なる部分 |

**Indexを使う理由**: Cubeを三角形ごとに完全展開すると36頂点必要だが、位置だけなら8頂点をIndexで共有できる。ただしNormalやUVが面ごとに異なる頂点は、同じ位置でも別Vertexへ分ける必要がある。

**頂点共有の注意**

- Positionが同じでもNormalが違えばHard Edgeになるため分離する。
- UV Seamでは同じ3D位置に複数UVが必要なので分離する。
- Tangentの向きやBone Weightが違う場合も分離する。
- 「頂点数」は一意な位置数ではなく、Vertex Attributeの組み合わせ数で決まる。

**Winding**: 三角形の頂点順が表裏を決める。座標系変換で1軸を反転するとWindingも反転するため、Index順を入れ替えるかCull設定を合わせる。OBJ読込でX反転後に頂点順を反転するのはこのため。

**Normalの作り方**

```text
faceNormal = normalize(cross(p1 - p0, p2 - p0))
```

- Flat Shadingは面ごとに同じNormalを使う。
- Smooth Shadingは共有頂点に接する面法線を、面積や角度で重み付けして平均する。
- 無条件平均すると、直角の角まで丸く見える。Smoothing GroupやCrease Angleが必要になる。

### G-42. Clipping、Back-face Culling、Rasterization

処理を混同しないこと。

1. **Frustum Culling**: Object単位。Draw前にBoundsが視錐台外なら除外する。
2. **Clipping**: Primitive単位。Clip Spaceの境界を跨ぐ三角形をGPUが切り詰める。
3. **Back-face Culling**: 三角形単位。Windingから裏面を除外する。
4. **Rasterization**: 残った三角形が覆うPixel SampleをFragment候補にする。
5. **Depth Test**: 既存Depthと比較し、見えているSampleだけ残す。

Frustum Cullingを行っても、画面端を跨ぐ三角形のClippingは必要。Back-face Cullingは遮蔽判定ではなく、閉じたMeshの裏面を描かない最適化である。両面Material、葉、布、Water等ではCull Noneが必要な場合がある。

**Early-Z**はPixel Shader前にDepth Testする最適化。Pixel Shaderが`SV_Depth`を書く、discardを多用する、UAV Side Effectを持つ等の場合は制限される。Depth PrepassはOverdrawを減らせるが、Geometryをもう一度描くCostとの交換になる。

### G-43. 重心座標と三角形内補間

三角形内の点`P`は3頂点`A/B/C`の重みで表せる。

```text
P = u*A + v*B + w*C
u + v + w = 1
```

- 全Weightが0以上なら点は三角形内にある。
- UV、頂点Color、Normal、World Position等は重心座標で補間する。
- Screen Spaceでそのまま線形補間するとPerspectiveで歪むため、G-3の`1/w`補正が必要。
- Ray/Triangle交差のMoller-Trumboreは、交差距離と同時に重心座標`u/v`を求められる。

Normalを補間した後は長さ1とは限らないので、Lighting前に再正規化する。TangentとNormalを独立補間すると直交性が崩れるため、Gram-Schmidtで直交化する場合がある。

### G-44. SAT、GJK、EPA、CCD

**SAT（Separating Axis Theorem）**: 2つの凸形状が分離できる軸を1本でも見つければ非衝突。Box同士では面法線とEdge同士の外積を候補軸にする。軸数が決まっている形に向く。

**GJK**: Minkowski Difference上で原点を含むかをSimplexで探索し、任意の凸形状の交差判定に使える。Shapeごとに「指定方向で最も遠い点」を返すSupport Functionがあればよい。

**EPA**: GJKで衝突した後、Minkowski DifferenceのPolytopeを拡張し、貫通法線と深さを求める。

**CCD（Continuous Collision Detection）**: 離散Frameの位置だけを見ると、高速な弾が薄い壁を飛び越すTunnelingが起きる。Swept ShapeやTime of Impactを求め、移動区間全体で衝突を判定する。

| 方式 | 得意 | 弱点 |
| --- | --- | --- |
| 離散判定 | 通常速度の剛体 | 高速物体がすり抜ける |
| Ray / Shape Cast | 弾、Character移動 | 回転を含む連続運動は近似になる |
| Speculative Contact | 将来接触を先に作る | 接触が早く見える場合がある |
| TOI CCD | 正確な衝突時刻 | Costが高くSolverも複雑 |

CG2EngineはJoltのShapeとSolverを使うため、SAT/GJK/EPAを自作したと説明しない。面接では「SDKへ任せた範囲」と「Engine側でCollider設定・同期・Layer・Queryを接続した範囲」を分ける。

### G-45. 空間分割と加速構造

| 構造 | 特徴 | 得意なScene | 更新Cost |
| --- | --- | --- | --- |
| Uniform Grid | 空間を固定Cellへ分割 | Objectサイズが近い | 移動時のCell更新が簡単 |
| Spatial Hash | 必要CellだけHash化 | 広い疎な空間 | Hash衝突とCell重複 |
| Quadtree / Octree | 空間を再帰分割 | 密度差のある静的空間 | 動的Objectで再配置が必要 |
| BVH | Object Boundsを階層化 | Ray Trace、衝突、Culling | Refitまたは再構築 |
| KD-tree | 軸で空間分割 | 静的Ray Query | 動的更新に弱い |
| Sweep and Prune | 軸上IntervalをSort | 動きが連続する物理 | 大きなTeleportでSort Cost増加 |

加速構造の目的はO(n)やO(n²)の候補列挙を減らすこと。ただしObject数が少ない場合、構築・更新Costの方が高い。静的／動的、Object数、サイズ差、Query種類を見て選ぶ。

### G-46. 力、Impulse、Torque、慣性

```text
Force:   F = m*a
Impulse: J = integral(F dt) ≈ Δp = m*Δv
Torque:  τ = r × F
Angular: τ = I*α
```

- Forceは時間にわたって加える。Frame時間を考慮して積分する。
- Impulseは瞬間的な運動量変化。衝突反応や発射反動に向く。
- 同じForceでも重心から離して加えるとTorqueが発生する。
- 慣性Tensorは「回りにくさ」の方向依存を表す。同じ質量でも細長い棒と球では回転応答が違う。
- Center of Massを下げると転倒しにくくなるが、Colliderそのものを下げたことにはならない。
- Staticは動かない、KinematicはTransform主導で動く、DynamicはSolverが動かす。KinematicへForceを加えても通常のDynamicと同じ結果にはならない。

摩擦は単純に速度を減らす値ではなく、接触面の接線方向Impulseを制限する。反発係数は法線方向の相対速度をどれだけ戻すかを表す。

### G-47. FK、IK、Animation Blend

**Forward Kinematics（FK）**: 親Boneから子BoneへLocal Transformを順に合成し、End Effector位置を得る。Animation Clip再生の基本。

**Inverse Kinematics（IK）**: 手や足などの目標位置から、必要な関節回転を逆算する。

| IK方式 | 特徴 |
| --- | --- |
| Two Bone IK | 腕・脚の2関節。解析解で高速 |
| CCD IK | 末端から順に回す。実装が単純 |
| FABRIK | 関節位置を前後反復。制約を入れやすい |
| Jacobian | 多自由度へ一般化できるが重い |

**Skinning**では各頂点を複数Bone Matrixで変換しWeight合成する。Weight合計は通常1へ正規化する。Linear Blend Skinningは関節で体積が潰れる「Candy Wrapper」があり、Dual Quaternion Skinningは回転品質を改善するがScale/Shearの扱いが難しい。

Animation BlendではPosition/ScaleはLerp、RotationはQuaternion Slerp/Nlerpが基本。Additive Animationは基準Poseとの差分を重ねる。Root MotionはRoot Boneの移動をGameObjectへ移すため、PhysicsやNavigationとの所有権を決める必要がある。

### G-48. Sampling、Aliasing、Nyquist

連続信号を離散Sampleへ変換するとき、Sample周波数の半分を超える成分は別の低周波へ化ける。これがNyquistの考え方とAliasing。

- Texture縮小時のちらつきは、1 Pixelより細かい模様をSampleしているため起きる。Mipmapは事前にLow-pass Filterした縮小画像を使う。
- Geometry EdgeのAliasingはMSAA/TAA等で扱う。
- Specular Aliasingは粗さ、Normal分布、Pre-filterで扱う必要があり、MSAAだけでは消えない。
- Shadow MapのJaggyはShadow Texelの離散化。PCFは比較結果をFilterする。
- Temporal SamplingではCamera/Object移動によりSample位置を変え、履歴を蓄積する。誤った履歴はGhostになる。

Filter Kernelを広げるとNoiseは減るがDetailも失う。空間Filter、時間Filter、解像度、Sample数は品質とCostのTrade-offになる。

### G-49. 色、輝度、Alphaの基礎

- sRGBは表示・保存向けの非線形符号化。Lightingや補間はLinear空間で行う。
- HDRは1.0を超えるScene輝度を保持する。Tone MappingでDisplay範囲へ圧縮する。
- LuminanceはRGBの単純平均ではなく、人の感度を考慮した重み付き値を使う。
- Straight AlphaはRGBを元色のまま持つ。Premultiplied AlphaはRGBへAlphaを掛けて持ち、補間時の縁色や合成を扱いやすい。
- Alpha Blendの順序は一般に可換でない。通常Blendは奥から手前のSortが必要。
- Gamma補正とTone Mappingは別処理。Tone MappingはHDR圧縮、sRGB EncodeはDisplay用の符号化。

### G-50. Screen座標からWorld Rayを作る

Pickingや「Mouse位置へ撃つ」処理の基本。

1. Mouse PixelをViewport基準のNDCへ変換する。
2. Near/FarのClip座標を作る。
3. Projectionの逆行列でView空間へ戻す。
4. Viewの逆行列でWorld空間へ戻す。
5. `farWorld - nearWorld`を正規化してRay方向にする。

注意点:

- Window全体ではなく、実際のScene/Game Viewportの位置と寸法を使う。
- NDCのYとScreenのYは向きが逆。
- Row/Column Vector規約と行列の掛け順を合わせる。
- Camera Rayが正しくても、Weapon側がMuzzle位置やSpreadで最終方向を変える場合がある。入力変換だけでなく最終発射Ray/Velocityまで確認する。

---

## C++・プログラミング追加深掘り（P-14以降）

### P-14. Compile、Link、Translation Unit、ODR

C++は概ね次の段階を通る。

```text
Source/Header -> Preprocess -> 各.cppをCompile -> Object File -> Link -> EXE/DLL
```

- Headerは単独で実行されず、`#include`先へTextとして展開される。
- 同じHeaderを複数回Includeしないよう`#pragma once`またはInclude Guardを使う。
- Declarationは存在と型を知らせ、Definitionは実体を作る。
- Link Errorの「unresolved external」は宣言は見えたが定義が無い、またはSignatureが一致しない場合に起こる。
- Duplicate Symbolは同じ外部Linkageの定義が複数Translation Unitにある場合に起こる。
- ODR（One Definition Rule）に違反したProgramは、Link Errorになる場合と、通って未定義動作になる場合がある。
- Header内関数は`inline`、Template、Class内定義等の規則を理解して多重定義を避ける。
- Forward DeclarationはInclude依存を減らすが、値Memberや継承など完全型が必要な場所では使えない。

### P-15. Template、Generic Programming、Type Erasure

- Templateは型ごとにCompile時展開され、型安全でInline化しやすい。使う型が増えるとCode SizeとBuild時間が増える。
- ConceptはTemplate引数へ必要な操作を明示し、Errorを読みやすくする。
- Runtime多態はvirtual/interface、Compile時多態はTemplate、閉じた型集合は`variant`が候補。
- Type Erasureは具体型を隠して共通操作だけ保持する。`std::function`が代表例。Allocationや間接呼出Costがあり得る。
- Templateを採用する理由を「速いから」だけにしない。型集合がCompile時に決まるか、Binary境界を跨ぐか、Errorの分かりやすさを考える。

### P-16. Alignment、Padding、False Sharing

- 型はAlignment境界へ配置され、CompilerがMember間や末尾へPaddingを入れる。
- Member順を変えるだけで`sizeof`が変わる場合がある。ABIや保存Binaryへ生Structを書き出す設計では特に危険。
- GPU Constant BufferはD3D12で配置規則と256 Byte単位のAddress Alignmentを考える（F-11）。C++とHLSLで型幅・Paddingを一致させる。
- SIMD LoadはAlignment要件を持つ場合がある。現在のCPUではUnaligned対応でも、Cache Line跨ぎは別Costになる。
- False Sharingは別Threadが別変数を書いていても、同じCache Line上にあるため互いのCacheを無効化する現象。Counter配列等で起こる。

### P-17. 未定義動作、型変換、境界

代表的な未定義動作:

- 配列範囲外Access、解放後Pointer、Null Dereference
- 符号付き整数Overflow
- 初期化前値の読取
- Data Race
- 寿命が始まっていないObjectを別型として読む不正なAlias
- 無効Iteratorの使用

未定義動作は「Errorが出る」保証すらなく、Debugだけ動きReleaseで壊れることがある。

- `static_cast`は通常の明示変換、`dynamic_cast`は多態型のRuntime確認、`reinterpret_cast`はBit表現を別型として扱う低水準操作。
- Signed/Unsigned比較は負数が巨大なUnsignedへ変換される場合がある。
- Narrowingで大きい値や小数部が失われる。Asset SizeやFile Offsetでは型幅を確認する。
- `memcpy`で安全にCopyできるのはTrivially Copyableな型。`std::string`や`vector`を生Bytesとして保存しない。

### P-18. Handle、Generation、Object Pool

単純な配列Index Handleは、削除後に同じSlotを別Objectが使うと古いHandleが新Objectを指すABA問題を起こす。

```text
Handle = { index, generation }
```

- Slot再利用時にGenerationを増やす。
- Access時にHandleとSlotのGenerationが一致するか確認する。
- Invalid値を定義し、失敗時は安全な戻り値を返す。
- Poolへ返すとき、Transform、Velocity、Timer、Callback、Owner ID等をResetする。
- Prewarmは初回生成Spikeを減らすが、Memoryを先に使う。
- Pool上限を超えた場合に、拒否、拡張、最古再利用のどれを選ぶか決める。

CG2EngineではEffect、Haptic、Wire、Object Pool等でHandleや世代管理の考え方が関係する。各SubsystemがGenerationまで持つかは個別実装を確認し、一般論を実装済みと断言しない。

### P-19. Serialization、Schema、Migration

保存形式で決める項目:

1. 型とField名をどう識別するか。
2. Versionをどこへ持つか。
3. Field追加、削除、名前変更、型変更をどう扱うか。
4. 未知Fieldを読んだとき無視するか保持するか。
5. Object参照をPointerではなくUUID等へ変換する方法。
6. Atomic Save、Backup、破損検出をどう行うか。
7. Endianness、Alignment、文字Encodingをどう固定するか。

| 方式 | 長所 | 弱点 |
| --- | --- | --- |
| Text key-value / JSON | 可読、差分、未知Key対応 | Size、Parse Cost |
| 列位置Text | 小さく単純 | 挿入・順序変更へ弱い |
| Binary Schemaなし | 高速・小さい | Version移行が難しい |
| Schema付きBinary | 高速、互換Ruleを持てる | ToolとSchema管理が必要 |

Migrationは「古いDataを読める」だけでなく、読み込んで再保存したとき意味を失わないことまで確認する。

### P-20. Network基礎: TCP、UDP、Framing、Latency

| 観点 | TCP | UDP |
| --- | --- | --- |
| 接続 | Connection-oriented | Connectionless |
| 順序 | 保証 | 保証なし |
| 再送 | あり | Application側 |
| Packet境界 | Byte Streamなので保持しない | Datagram単位 |
| Head-of-Line | ある | Application設計次第 |

**TCPで重要な点**: 1回の`send`と1回の`recv`は対応しない。HeaderにLengthを持つ、改行区切りにする等のFramingが必要。部分受信、複数Message同時受信、切断途中を扱う。

**UDPで必要になるもの**: Sequence、Ack、再送、重複排除、順序並べ替え、MTU超過対策を用途ごとに設計する。すべてをReliableにするとTCPに近づく。

- Latencyは応答までの時間、Bandwidthは単位時間の転送量、Throughputは実際に処理できた量。
- Heartbeatは無通信と切断を区別するために使うが、Timeout値が短すぎると一時停止を切断扱いする。
- Network入力を信用せず、Length、ID、Path、Version、権限を検証する。
- Endianness、整数幅、文字EncodingをProtocolとして固定する。

CG2Engine共同編集のJSON over TCP、Revision、UUID、Lock、Heartbeat、再接続はR-39を参照する。Tailscaleは到達可能なPrivate Networkを作る層であり、Application Messageの整合性や競合解決を代行しない。

### P-21. State Machine、Event、Callback

**State Machine**は状態と遷移条件を明示する。多数のBoolの組み合わせで状態を表すと、存在してはいけない組み合わせが生まれる。

- Entry/Exit処理を定義する。
- 遷移の優先順位と同Frame多重遷移を決める。
- 親子Stateや並行Stateが必要なら階層State Machineを検討する。
- State変更中にContainerを変更する場合、Iteration無効化を避ける。

**Event/Callback**では購読解除と寿命が重要。送信元より受信先が先に破棄された場合、Dangling Callbackになる。Token/Connection Object、weak参照、Owner単位解除等を使う。

Event Queueは実行順を制御できる一方、即時反映ではなくなる。同期Eventと非同期Eventを名前だけで混ぜない。

### P-22. Real-time処理、Latency、Throughput、Frame Budget

60fpsのFrame Budgetは約16.67ms、120fpsは約8.33ms。ただしCPUとGPUが並列なら単純合計ではなく、遅い側がFrameを支配する。

- Averageが16msでも、ときどき50msなら操作感は悪い。最大値、Percentile、Spike原因を見る。
- Loading、Shader Compile、Pool初回生成、Container再確保はSpikeになりやすい。
- Throughputを上げてもLatencyが悪化するBatch設計がある。
- Amdahlの法則により、全体の10%しか占めない処理を無限に高速化しても全体は最大約1.11倍。
- GPU Bound時にCPUだけ最適化してもfpsは上がらない。ただしFrame Latencyや余力には影響する。
- 最適化は品質、Memory、開発複雑度とのTrade-offを記録する。

### P-23. Securityと入力検証

EditorやToolでも、外部File・Network・Plugin・Scriptを扱うなら安全性が必要。

- Pathを正規化し、Project Root外への`../` Traversalを拒否する。
- Archive展開時はZip Slip、絶対Path、巨大展開Sizeを検証する。
- File Size、配列件数、文字列長、再帰深度へ上限を設ける。
- Network MessageのType、Length、Project ID、Revision、UUIDを検証する。
- Cloud SaveやLeaderboardはClient値を信用せず、Server側で範囲と権限を確認する。
- DLL/PluginはNative CodeとしてProcess権限を持つ。単なるData Assetと同じ安全境界ではない。
- LogへToken、Password、個人情報を出さない。
- Error時に勝手なFallback処理へ置き換えると、成功したように見えてDataを壊す。Unavailableを明示する方が安全な場合がある。

### P-24. Build Configuration、Debug/Release差

- DebugはAssertion、Iterator Debug、最適化なし等で問題を見つけやすいが、Performance特性はReleaseと異なる。
- Releaseは`NDEBUG`等でassertが消え、Compiler最適化により未定義動作が表面化しやすい。
- Runtime Library、Compiler Version、Packing、Preprocessor DefineがDLL間で違うとABI問題になる。
- `/WX`は警告をError化する。ThirdParty警告まで一律に有効にするか境界を分ける。
- Incremental Build成功後もClean環境、別PC、CIで必要Fileと絶対Path依存を確認する。
- Buildできたことと、起動・描画・操作・性能が正しいことは別。

### P-25. 面接で説明するときの回答構造

技術質問は次の順で答えると、暗記ではなく判断を説明しやすい。

```text
1. 結論: 採用方式を一文で言う
2. 目的: 何を解決する機能か
3. 処理: 入力から出力までを順番に言う
4. 理由: 要件に対して何が合ったか
5. 代替: 比較した方式
6. Trade-off: 得たものと失ったもの
7. 実装: CPU/GPU、主要Class、Dataの流れ
8. 検証: 何を測った／確認したか
9. 改善: 次に直すなら何か
```

知らない数値を推測して答えない。「方式は説明できるが、その定数は未確認。確認するなら○○を見る」と答える方がよい。

---

## AI模擬面接・段階別質問集（Q章）

AIは一度に1問だけ出し、回答に応じて表の右列を追加質問する。正解文の暗唱ではなく、式・処理順・Trade-off・CG2Engineへの接続を評価する。

### Q-1. C++基礎

| 基本質問 | 深掘り |
| --- | --- |
| PointerとReferenceの違いは何か | null、再代入、寿命、非所有契約 |
| StackとHeapをどう使い分けるか | Allocation Cost、断片化、Scope |
| RAIIとは何か | 途中return、Exception、COM Resource |
| `unique_ptr`と`shared_ptr`の違い | 循環参照、参照数Cost、`weak_ptr` |
| CopyとMoveの違い | Rule of Zero/Five、Moved-from状態 |
| `const`は何を保証するか | Logical Const、Pointerのconst位置 |
| `vector`のPointerが無効になる条件 | 再確保、erase、reserve |
| virtual関数の仕組み | vtable、Destructor、Cache |
| Templateとvirtualをどう選ぶか | Compile時/Runtime、Code Size、ABI |
| Undefined Behaviorとは何か | Releaseだけ壊れる理由、例を3つ |
| AlignmentとPaddingはなぜ必要か | ABI、GPU Buffer、False Sharing |
| HeaderとSourceを分ける理由 | Translation Unit、依存、Build時間 |
| Link ErrorはCompile Errorと何が違うか | unresolved、duplicate、ODR |
| `std::function`のCostは何か | Type Erasure、Allocation、間接呼出 |
| Exceptionを使わない場合どうErrorを返すか | Result、Error Code、Log、契約 |

### Q-2. データ構造・アルゴリズム

| 基本質問 | 深掘り |
| --- | --- |
| Big-Oは何を表すか | 定数Cost、Worst/Average、Cache |
| vectorとlinked listをどう選ぶか | 連続Memory、挿入、実測性能 |
| Hash Mapが平均O(1)なのはなぜか | Hash衝突、rehash、最悪O(n) |
| TreeとHashの違い | 順序、Range Query、Memory |
| BFSとDFSの違い | 最短路、Memory、Scene階層 |
| DijkstraとA*の違い | Heuristic、Admissible、Navigation |
| Object数が増えた時O(n²)をどう見つけるか | Profiler、二重Loop、空間分割 |
| SortのCostをいつ許容するか | Render Queue、Key、安定Sort |
| Poolを使う理由 | Spike、Reset漏れ、上限超過 |
| UUIDと配列Indexをどう使い分けるか | 永続参照、Lookup、再配置 |

### Q-3. 3D数学

| 基本質問 | 深掘り |
| --- | --- |
| 内積をどこで使うか | 角度、射影、Lighting、視野 |
| 外積をどこで使うか | 法線、基底、Torque、引数順 |
| 点と方向の違いは何か | 同次座標w、平行移動 |
| Local/World/View/Clip/NDCを説明 | 変換順、Viewport、規約 |
| 行列の掛け順を変えるとどうなるか | Row/Column Vector、TRS |
| 逆行列は何に使うか | Camera、Picking、Local変換 |
| NormalへWorld行列をそのまま掛けられないのはなぜか | 非一様Scale、逆転置 |
| EulerとQuaternionの違い | Gimbal Lock、Slerp、可読性 |
| LerpとSlerpの違い | 一定角速度、最短経路、Nlerp |
| Rayをどう表すか | 正規化、t、Segmentとの違い |
| AABB/OBB/Sphereを比較 | Cost、過大評価、回転 |
| 浮動小数点誤差をどう扱うか | 絶対/相対epsilon、巨大座標 |

### Q-4. Rendering/GPU

| 基本質問 | 深掘り |
| --- | --- |
| Draw Callはなぜ重いか | State、Driver、Command記録、Batch |
| ForwardとDeferredの違い | Light数、Material、透明、帯域 |
| Vertex ShaderとPixel Shaderの役割 | 補間、Clip、Early-Z |
| GBufferとは何か | 格納値、帯域、Deferredとの違い |
| Depth Bufferの精度はどう分布するか | Near/Far、Reverse-Z |
| Mipmapはなぜ必要か | Nyquist、Cache、生成Filter |
| sRGBとLinearの違い | Lighting、補間、Encode |
| Normal MapはなぜTBNが必要か | Tangent Space、Handedness |
| PBRのD/F/Gは何か | GGX、Fresnel、Geometry項 |
| Shadow AcneとPeter Panningの原因 | Bias、Normal Offset、Slope |
| CSMはなぜ必要か | Texel密度、分割、Blend |
| SSRの画面外問題をどう補うか | Probe/IBL Fallback、Temporal |
| CPU/GPU同期が起きる箇所 | Fence、Readback、Map、Present |
| GPU Cullingの結果をどうDrawへ反映するか | Predication、ExecuteIndirect |
| Temporal処理でGhostが出る理由 | Motion Vector、Disocclusion、History Clamp |

### Q-5. Physics/Collision

| 基本質問 | 深掘り |
| --- | --- |
| Broad PhaseとNarrow Phaseの違い | BVH、SAP、詳細Shape |
| TriggerとCollisionの違い | Solverへ渡すか、Eventのみか |
| ForceとImpulseの違い | dt、運動量、用途 |
| Torqueはどう発生するか | `r × F`、重心 |
| Static/Kinematic/Dynamicの違い | 所有者、Force、同期 |
| Fixed Time Stepを使う理由 | 安定性、Catch-up、補間 |
| Tunnelingとは何か | CCD、Shape Cast、TOI |
| 摩擦と反発をどう解釈するか | 接線Impulse、法線速度 |
| Colliderと見た目がずれた時何を見るか | World Transform、Center/Size、Shape |
| SDK利用部分と自作部分はどこか | Jolt、NvBlast、Engine接続 |

### Q-6. Engine設計

| 基本質問 | 深掘り |
| --- | --- |
| Component方式を選んだ理由 | 継承/ECS比較、Memory弱点 |
| Scene Objectをどう識別するか | UUID、Index、Hash、寿命 |
| Serialize互換をどう維持するか | Version、未知Field、Migration |
| EditorとRuntimeをなぜ分けるか | 配布Size、依存、Play移行 |
| Asset重複Loadをどう防ぐか | Cache Key、参照、Invalidate |
| PrefabとScene Objectの違い | Source、Override、Nested |
| Undo/Redoをどう実装するか | Command vs Snapshot、Memory |
| Script API変更をどう互換化するか | API/ABI、末尾追加、Version |
| Manager間依存をどう管理するか | 初期化順、DI、Global State |
| GameObject削除時に何を破棄するか | Physics、Audio、Effect、Callback |

### Q-7. Thread/Network

| 基本質問 | 深掘り |
| --- | --- |
| Race Conditionとは何か | Data Race、未定義動作、再現困難 |
| MutexとAtomicの違い | 複合不変条件、Memory Order |
| Deadlockをどう防ぐか | Lock順、Scope、Try Lock |
| Double Bufferをなぜ使うか | 1Frame遅延、Memory、交換点 |
| TCPとUDPをどう選ぶか | 順序、再送、Latency、Framing |
| TCPでMessage境界をどう作るか | Length Header、改行、部分受信 |
| 切断をどう検出するか | Heartbeat、Timeout、OS Error |
| 再接続時に何を同期するか | Snapshot、ChangeLog、Revision |
| 共同編集競合をどう検出するか | UUID、Property Revision、Lock |
| Tailscaleは何を解決し、何を解決しないか | 到達性とApplication Protocolの違い |

### Q-8. Debug/Performance/実務

| 基本質問 | 深掘り |
| --- | --- |
| Crashをどう調べるか | Dump、Call Stack、入力、Commit境界 |
| 当たり判定不具合をどう調べるか | Ray/弾と対象のWorld形状を記録 |
| CPU BoundとGPU Boundをどう見分けるか | Timestamp、Frame待ち、解像度変更 |
| Average以外に何を見るか | Max、Percentile、Spike、履歴 |
| 最適化のBefore/Afterをどう比較するか | 同一Scene、Frame範囲、品質固定 |
| Build成功で何が証明できるか | Compile/Linkのみ、Runtimeは別 |
| Releaseだけ壊れる理由は何か | UB、assert消失、Timing、最適化 |
| 大きな変更でRegressionを防ぐには | 小さい境界、Test、旧Data、Review |
| 分からない質問へどう答えるか | 未確認を明示し、確認場所と仮説を分ける |
| 自分が改善した箇所を説明 | 問題、計測、変更、結果、残る弱点 |

---

## 3D・GPU追加追補（G-51以降）

### G-51. Texture Formatと圧縮

Textureは解像度だけでなくFormatがMemory、品質、Sampling可否を決める。

| Format例 | 特徴 | 用途 |
| --- | --- | --- |
| `R8G8B8A8_UNORM` | 各Channel 8bit、0～1 | Color、Mask、UI |
| `R8G8B8A8_UNORM_SRGB` | Sample時にsRGB→Linear | Base Color等 |
| `R16G16B16A16_FLOAT` | HDR、64bit/Pixel | HDR RenderTarget |
| `R32_FLOAT` | 32bit単Channel | Depth由来Data、計算Buffer |
| `D32_FLOAT` | Depth専用 | Depth Buffer |
| `R11G11B10_FLOAT` | Alpha無しの軽量HDR | Lighting Buffer候補 |

**Block Compression**

| 方式 | 向くTexture | 注意点 |
| --- | --- | --- |
| BC1 | Alpha不要のColor | 低容量、Block Noise |
| BC3 | 明示Alpha付きColor | BC1より容量増 |
| BC4 | 1Channel Mask/Height | Gray Data向け |
| BC5 | 2Channel Normal | XYを保持してZを再構築 |
| BC6H | HDR | 環境Map等 |
| BC7 | 高品質Color/Alpha | Encodeが重い |

圧縮Textureは通常4×4 Block単位。細い線、Mask境界、NormalでArtifactが出る。File圧縮PNG/JPEGとGPU Texture圧縮BCnは別物で、PNGをGPUへそのままSamplingすることはできない。

Format選択では、Shaderが必要なChannel、HDR範囲、Filter可否、RenderTarget/UAV対応、Memory帯域を確認する。高精度Formatを無条件に使うとVRAMと帯域が増える。

### G-52. Compute Shader、Thread Group、同期

Compute ShaderはDraw Callではなく`Dispatch(groupX, groupY, groupZ)`で実行する。Shader側の`numthreads(x,y,z)`と掛けた数が起動Thread数になる。

```text
総Thread数 = Dispatch Group数 × numthreads
```

- 境界がGroup Sizeで割り切れない場合、Shader内でIndexがTexture/Buffer範囲内か確認する。
- Group Shared Memoryは同一Group内で共有できる高速なMemory。Group間では共有できない。
- `GroupMemoryBarrierWithGroupSync`はGroup内の書込とThread到達を同期する。分岐により一部Threadだけが到達するとDeadlock相当になる。
- UAVへ書いた結果を次のPassで読む場合、Resource StateとUAV書込順序を保証するBarrierが必要。
- AtomicはCounterやAppendに使えるが、同じAddressへ集中すると直列化する。

**Group Sizeの考え方**: GPUはWave/Warp単位で実行する。小さすぎるGroupは稼働率が下がり、大きすぎるGroupはRegister/Shared Memoryを使い切って同時実行Group数が減る。64/128/256 Thread等からProfilerで比較する。

CG2EngineではOcean FFT、Culling、Depth Pyramid、SSR、SSGI、Particle、Skinning、Exposure等がCompute対象。存在するShaderと実際にDispatchされるShaderを区別する。

### G-53. LOD、Screen Size、Impostor

LODは距離だけでなく、画面上で占める大きさを基準にするとFOVや解像度へ適応しやすい。

```text
projectedSize ≈ objectWorldSize / distance × projectionScale
```

| 手法 | 削減対象 | 弱点 |
| --- | --- | --- |
| Mesh LOD | Vertex/Triangle | 切替時の形状Pop |
| Material LOD | Shader命令/Texture | 見た目の差 |
| Simulation LOD | Update/AI/Physics頻度 | Gameplay差異 |
| Impostor/Billboard | Geometryを画像化 | 視点変化、Lighting差 |
| HLOD | 複数Objectを結合 | 動的Objectに不向き |

切替境界ではHysteresisを持たせ、距離が境界付近で振動して毎Frame LODが切り替わるのを防ぐ。Cross Fade/Dither FadeはPopを減らすが、両LODを同時描画する期間がある。

LOD生成では形状誤差、Silhouette、UV、Skinning、Material境界を守る。単にTriangle数を半分にしても品質は保証されない。

### G-54. Camera、FOV、Aspect、焦点距離

- Vertical FOVは画面縦方向の視野角。Aspect比が変わればHorizontal FOVも変わる。
- Perspective Projectionでは遠い物体ほど小さくなる。Orthographicでは距離による大きさ変化がない。
- FOVを広げると周辺のPerspective歪みが増え、同じ物体が画面上で小さくなる。
- Near/Farは表示範囲だけでなくDepth精度へ影響する。
- 物理Cameraの焦点距離とSensor SizeからFOVを求められるが、ゲームCameraは直接FOVを指定することが多い。
- Camera ShakeはView行列へ加える演出Offsetと、Gameplay上のCamera Transformを分離すると照準やCullingを壊しにくい。
- JitterはTAA Sample位置を変える小さいProjection Offset。UIやPickingには非Jitter行列が必要になる。

### G-55. Particle Systemの設計

Particleは大量の短寿命要素を扱うため、1ParticleをGameObjectにするとAllocation、Update Dispatch、Transform、SerializeのCostが大きい。

**CPU Particle**

- 発生条件やGameplay連携が容易。
- CPUから各Particleを更新し、Instance BufferへUploadする。
- 大量になるとCPU Updateと転送がBottle Neck。

**GPU Particle**

- ComputeでSpawn/Updateし、GPU BufferからIndirect Drawできる。
- 大量Particleに向くが、個別ParticleをCPU Gameplayから操作・Queryしにくい。
- Dead/Alive List、Atomic Counter、Buffer Capacity、Overflow時の扱いが必要。
- CollisionはDepth Buffer近似、SDF、Scene Collider Upload等の方式がある。

BillboardはCamera Facing基底を作りQuadを向ける。Soft ParticleはScene Depthとの差から交差部をFadeし、地面との硬い切れ目を抑える。透明ParticleはSort Costが高いため、AdditiveやWeighted OITを使う場合がある。

### G-56. Noiseと手続き生成

- White NoiseはSample間に相関がなく、細かいちらつきになる。
- Perlin/Simplex Noiseは滑らかな勾配Noiseで、Terrain、雲、揺らぎに使う。
- FBMは周波数と振幅を変えたNoiseを複数Octave重ねる。Detailは増えるがSample回数も増える。
- Blue Noiseは低周波成分が少なく、空間SamplingやTemporal Ditherの誤差を目立ちにくくする。
- Hash NoiseはTexture不要だが、品質とInstruction Costを確認する。

Random Seedを固定すれば再現可能な生成ができる。ReplayやNetwork同期では、乱数呼出順が変わるだけで結果がずれるため、SystemごとのRandom Streamを分ける方法がある。

---

## プログラミング・Computer Science追補（P-26以降）

### P-26. 探索、Sort、Graph Algorithm

**BFS**はQueueを使い、Edge Costが同じGraphの最短Step数を求められる。幅広く展開するためMemoryを使う。**DFS**はStack/再帰で深く進み、到達判定、Cycle検出、Hierarchy走査に向くが、最短路は保証しない。

**Dijkstra**は非負Costの最短路。Priority Queueから現在Cost最小Nodeを取り出す。**A***は`f(n)=g(n)+h(n)`でGoalまでの推定Costを加え、探索範囲を絞る。

- Heuristicが実Costを超えないAdmissibleなら最短性を保ちやすい。
- `h=0`ならDijkstraと同じ。
- Navigation MeshではNode数だけでなく、経路点の平滑化とAgent半径を考える。

**Sort**

- Stable Sortは同じKeyの元順序を保持する。
- Render QueueではOpaqueをState/Depth、TransparentをDepth順にする等、目的によりKeyが違う。
- 毎Frame全件Sortする前に、変更時だけSort、Bucket分け、Radix Sort等を検討する。

### P-27. CPU、Cache、Branch、SIMD

CPUは命令をPipeline実行し、Branch Predictionで次の経路を先読みする。予測失敗するとPipelineを捨てて再開する。

- Dataが連続していればCache Line単位の先読みが効く。
- Pointer Chaseは次のAddressが分かるまで待つため遅い。
- Branchless化は常に速いわけではない。両側の計算量やCompiler生成Codeを測る。
- SIMDは複数Dataを1命令で処理する。Data Layout、Alignment、分岐、端数処理が重要。
- Auto Vectorizationを妨げるAliasや複雑なLoop依存がある。
- GPUへ移す前に、CPU側でCache MissやAllocationが支配していないか測る。

### P-28. Virtual MemoryとMemory Map

- Processが見るVirtual Addressと物理MemoryはOSのPage Tableで対応付けられる。
- CommitしたMemoryが即すべて物理RAMへ常駐するとは限らない。
- Page Faultは必要Pageが未配置のとき発生し、Diskまで行くMajor Faultは大きなSpikeになる。
- Working SetがCache/RAMを超えるとThrashingが起きる。
- Memory Mapped FileはFileをAddress空間へMappingし、OS Page Cacheを利用できる。Random Accessに強いが、Mapping寿命とFile変更を扱う必要がある。
- GPU Upload/Readback HeapのMapは通常Heap Allocationの意味とは異なる。GPU Resourceの可視性と同期が必要。

### P-29. C++ Memory ModelとAtomic

Atomicは値の破損を防ぐだけでなく、Thread間のMemory可視性をOrderingで定義する。

| Order | 意味の概要 |
| --- | --- |
| Relaxed | Atomic値の一貫性だけ。周辺Memory順序を作らない |
| Acquire | 以後の読取を前へ出さず、Release側の書込を見る |
| Release | 以前の書込を後へ出さず、Acquire側へ公開する |
| AcqRel | Read/Modify/Writeで両方 |
| SeqCst | 最も強い全体順序。理解しやすいが制約が大きい |

Lock-freeは「速い」と同義ではない。CAS Retry、Cache Line競合、ABA、Memory Reclamationが複雑になる。まずMutexで正しく作り、Profilerで競合が支配的と確認してから検討する。

### P-30. File、Encoding、Endianness

- UTF-8は可変長。Byte数と文字数は一致しない。途中Byteで切ると壊れる。
- BOMはUTF-8では必須ではないが、ToolがEncodingを誤認する環境では識別に使う。
- UTF-16の`wchar_t`幅はPlatform依存。Windowsでは通常16bitでSurrogate Pairがある。
- File PathをANSIへ落とすと日本語Pathが失われる。Windows APIではWide Pathを使い、内部Encoding変換を明示する。
- Little/Big EndianでMulti-byte整数のByte順が違う。Network/Binary Formatは順序を固定する。
- Text Modeは改行変換を行う場合がある。Binary DataはBinary Modeで読む。
- File書込は一時Fileへ完了後Renameすることで、途中Crashによる元File破損を減らせる。

### P-31. Dependency、Module境界、循環参照

- High-level PolicyがLow-level具体実装へ直接依存すると、Backend交換やTestが難しい。
- Interfaceは境界が変わる可能性、複数実装、Test Doubleが必要な場所へ置く。
- すべてをInterface化するとCall経路と所有権が見えにくくなる。
- Header相互IncludeはBuild依存と循環を増やす。Forward Declaration、Pimpl、責務分割を使う。
- Event Busで依存を隠すだけでは解消にならない。誰が送信し、誰が購読し、いつ処理するかを文書化する。
- Global Stateは導入が速いが、初期化順、Test、複数Scene、Thread Safetyを難しくする。

### P-32. 要件、制約、Trade-off

技術選定は方式の優劣ではなく要件との適合で決める。

```text
要件: 必ず満たす結果
制約: Platform、期間、Memory、対応Hardware等の上限
方式: 要件を満たす実装候補
Trade-off: 方式を選んで得るものと失うもの
検証: 要件を満たしたと判断する観測方法
```

「業界標準だから」「高速だから」だけでは理由にならない。Scene規模、Light数、対象GPU、Editorでの編集性、実装期間、保守人数を具体化する。

面接では、現在の方式の弱点を隠すより、制約下でなぜ採用し、規模が変わったら何へ移行するかを説明する。

### P-33. Code Reviewで見る項目

- 正しさ: 境界値、失敗経路、null、寿命、Thread Safety
- 仕様: 依頼範囲、既存API、保存互換、所有関係
- 可読性: 名前、関数Size、責務、Commentの意図
- 性能: Hot Path、Allocation、全探索、同期、GPU Barrier
- 安全性: Path、Network入力、Buffer Size、Integer Overflow
- 検証: Buildだけか、Unit/Integration/Play/Visualまで行ったか
- 文書: 新方式、処理順、制約、弱点が`engine-internals.md`と`ReadMe.md`へ反映されたか

Reviewでは好みの書き換えとBug/保守性問題を分ける。依頼外の大規模Refactorを同時に入れると、原因境界とReviewが難しくなる。

---

## 理解の深さを示す説明訓練（U章）

この章は用語集ではない。面接官から「本当に理解して実装したのか」を確認されたときに、原理から実装、失敗条件、検証方法まで一続きで説明するための訓練項目である。

良い回答は、次の5層を行き来できる。

1. **目的**: 何を解決する処理か。
2. **原理**: 数式、Hardware、言語仕様としてなぜ動くか。
3. **実装**: Engine内のDataがどのClass、Buffer、Passを通るか。
4. **Trade-off**: 何を得て、何を失い、どの規模で限界になるか。
5. **検証**: 正しいことを何の値、画像、時間、Logで確認するか。

「○○を使っています」で止まらず、「入力が何で、途中状態がどう変化し、出力が何になり、どこで壊れるか」まで説明する。

### U-1. 回答を組み立てる共通形式

面接では、最初から長い説明を一方的に行わない。まず30秒程度で全体像を示し、質問に応じて深くする。

**30秒の回答**

```text
この機能は［解決する問題］のためのものです。
現在は［採用方式］を使い、［主要な入力］から［主要な出力］を作ります。
［要件・制約］に合うため採用しましたが、［主要な弱点］があります。
```

**2分の回答**

```text
1. 目的と前提
2. 1Frame内の処理順
3. CPUとGPU、または所有者と利用者の分担
4. 採用理由と比較した方式
5. 現在の限界
6. 正しさと性能の確認方法
```

**さらに深掘りされた場合**

- 数式を暗記して読むのではなく、各項が何を意味するか説明する。
- Class名だけでなく、Dataの生成者、所有者、更新者、破棄者を分ける。
- 「高速」と言う場合は、削減する量がCPU時間、GPU時間、帯域、Draw Call、Allocationのどれかを示す。
- 「安全」と言う場合は、どのFailureを防ぐのかを示す。
- 未実装や未検証は隠さず、確認方法まで答える。

### U-2. 座標変換を理解していると伝わる説明

**浅い回答**: 「World、View、Projection行列を掛けて画面へ出します。」

**深い回答で含める内容**

1. Modelの頂点はObject固有のLocal座標にある。
2. World変換でScene内の位置、回転、Scaleを反映する。
3. View変換はCameraを動かす代わりに、World全体をCameraの逆TransformでCamera座標へ移す。
4. Projection変換はFrustumをClip空間へ写し、透視投影では`w`へ距離情報を入れる。
5. Rasterizer前に`xyz / w`のPerspective Divideを行い、NDCをViewportへ写す。
6. Clip空間では`w`を失う前にClippingする。先にDivideするとCamera後方やNear Plane交差を正しく扱えない。

**理解確認として説明できること**

- 行列の積順を変えると、移動軸や回転中心が変わる。
- 点は平行移動の影響を受けるため同次座標の`w=1`、方向は受けないため`w=0`として扱う。
- Normalは位置と同じ変換ではなく、面への直交関係を守るため逆転置相当が必要になる。
- Screen Pickingではこの経路を逆にたどり、Near/Farの点を逆View ProjectionでWorldへ戻してRayを作る。

**追加質問**

| 質問 | 理解している回答の要点 |
| --- | --- |
| View行列はCameraのWorld行列と同じか | 同じではなく、Camera World Transformの逆変換 |
| 非一様ScaleでNormalが壊れる理由 | TangentとNormalの直交性を通常のWorld行列が保存しない |
| `w=0`の方向へ移動成分が加わらない理由 | 4×4行列の平行移動列に0が掛かるため |
| 行列規約が違うAPIへ移植すると何が壊れるか | 乗算順、行列配置、Handedness、Depth範囲、Front Faceを個別確認する |

### U-3. 1つのObjectが描画されるまでを説明する

「Drawを呼ぶ」だけではGPUが描画できない。最低でも次のData経路を説明する。

```text
Scene/GameObject
  → Transform・Mesh・Materialを収集
  → Frustum等で候補を絞る
  → Render Queueへ分類・Sort
  → PSO、Root Signature、Descriptor、BufferをBinding
  → Vertex/Indexを入力してDraw/Indirect Draw
  → Vertex ShaderでClip座標を生成
  → RasterizerがTriangleをFragment候補へ変換
  → Pixel ShaderがMaterial・Lightを評価
  → Depth/Blend Testを通った値をRender Targetへ書く
  → Post ProcessとPresent
```

**CPU側が主に行うこと**

- Sceneから描画対象を見つけ、可視性と描画順を決める。
- GPU Resourceを用意し、StateとDescriptorを設定してCommandを記録する。
- Frame間で再利用するResourceの寿命とFenceを管理する。

**GPU側が主に行うこと**

- 頂点変換、Rasterization、Pixel単位のMaterial/Lighting評価を大量並列で処理する。
- Compute PassでCulling、Post Process、Particle等を処理する。

**理解度が出る説明**

- Draw Call削減はTriangle数削減と同じではない。主にCPUのCommand/State設定Costを減らす。
- Instancingは同じMesh/MaterialのInstance Data差分をまとめる。異なるStateを無条件にはまとめられない。
- GPU Culling後に結果をCPUへReadbackして通常Drawすると、同期で利点を失う場合がある。GPU結果をIndirect Argumentへ接続して初めてCPU往復を避けられる。
- Transparentは通常、Depth Writeを抑えて奥から手前へ描く必要があり、Opaqueと同じBatch戦略をそのまま使えない。

### U-4. PBRを数式の意味から説明する

PBRは「綺麗に見せるShader」ではなく、Material Parameterを物理的な意味へ寄せ、Lightや環境が変わっても一貫した応答を得る考え方である。

Microfacet BRDFのSpecularは概念的に次の積で構成する。

```text
Specular = D × F × G / (4 × NdotL × NdotV)
```

- `D`: MicrofacetのNormalがHalf Vector方向を向く分布。Roughnessが小さいほど狭く強いHighlightになる。
- `F`: Fresnel。視線が面へ平行に近づくほど反射が強くなる。金属ではBase Colorが反射色へ影響する。
- `G`: Microfacet同士のMasking/Shadowing。凹凸によりLightやViewが遮られる割合。
- 分母: 入射・出射方向と投影面積を正規化する項。

DiffuseとSpecularを別々に最大出力するとEnergyが増えてしまう。Fresnelで反射した分をDiffuseから減らすなど、Energy Conservationを考える。

**Parameterの意味**

| Parameter | 変えるもの | 誤解しやすい点 |
| --- | --- | --- |
| Base Color | 非金属のDiffuse色、金属の反射色 | 単なる最終色ではない |
| Metallic | 導体と誘電体の補間 | 汚れ等を除けば0か1寄りが基本 |
| Roughness | Microfacet分布の広がり | 単純にHighlightの明るさだけではない |
| Normal | 微細な面方向 | World Normalへそのまま足さない |
| AO | 間接光の局所遮蔽 | Direct Lightを黒く潰す用途ではない |

**深掘りへの答え**

- Gamma空間の値をそのままLightingへ入れると、加算と補間が物理量として不正確になる。LightingはLinear空間で行い、表示時に変換する。
- Normal MapはTangent空間からTBN基底でWorld/View空間へ移す。
- IBLではDiffuse IrradianceとSpecular Prefilterを分け、Roughnessに応じてMipを選ぶ。
- PBRでもMaterial値、Light単位、Tone Mappingが不整合なら物理的に正しくはならない。

### U-5. Shadow Mapの問題を原因から説明する

Shadow MappingはLight視点のDepthと、現在のPixelをLight空間へ写したDepthを比較する方式である。同じ表面を別のSampling/Projectionで比較するため誤差が出る。

**Shadow Acne**

- 原因はDepth量子化、Surfaceの傾き、Shadow MapのTexelとCamera Pixelの対応差。
- Biasを増やすと自己Shadowは減るが、影が物体から離れるPeter Panningが増える。
- Constant BiasだけでなくSlope-scaled Bias、Normal Offset、解像度、Near/Far範囲を組み合わせて調整する。

**Peter Panning**

- 比較Depthを押しすぎて、本来遮蔽される接触部がLitになる。
- Biasをただ戻すだけでなく、CasterのFront/Back Face、Normal Offset、Contact Shadow等を用途に応じて検討する。

**CSM**

- Camera Frustumを距離範囲へ分け、近距離へ高いTexel密度を割り当てる。
- 分割を線形だけにすると遠方寄り、対数だけにすると近方寄りになるため、BlendしたPractical Splitが使われる。
- Camera移動でLight Projectionが連続的にずれると影が泳ぐ。ProjectionをShadow TexelへSnapするStable CSMで軽減できる。
- Cascade境界は解像度とBias差が見えやすい。範囲を重ねてBlendする方法がある。

**面接で示したい判断**

「ArtifactがあるからBiasを上げる」ではなく、Acne、Aliasing、境界、Temporal Shimmer、Light Leakのどれかを分類し、それぞれ別の原因と対策を説明する。

### U-6. Temporal処理を履歴の信頼性から説明する

TAA、Temporal SSR、Temporal AO等は、過去FrameのSampleを再利用して現在Frameの不足Sampleを補う。ただしCameraやObjectが動くため、同じScreen座標をそのまま使えない。

```text
現在Pixel
  → Motion Vectorで前Frame位置を推定
  → 履歴をSample
  → Depth/Normal/Material等で同じSurfaceか判定
  → 履歴をClamp
  → 現在値とBlend
```

**必要な判断**

- Motion Vectorが無い、またはCamera分だけなら、動的ObjectにGhostingが残る。
- Disocclusionは前Frameで隠れていた領域が現れた状態で、使える履歴がない。
- Blend率を高くするとNoiseは減るが追従が遅くなり、Ghostingが増える。
- Neighborhood Clampは履歴色を現在近傍の範囲へ制限するが、細い高輝度Detailを消す場合がある。
- Scene ViewとGame Viewが別Cameraなら、Viewごとに履歴Textureと前Frame行列を分離する必要がある。
- Camera Cut、解像度変更、FOV変更、Scene切替では履歴を無効化する。

Temporal処理を説明するときは、「履歴を混ぜる」で終わらず、**履歴をどの座標へ戻し、何を根拠に信用し、いつ捨てるか**を説明する。

### U-7. PhysicsとTransform同期を説明する

PhysicsとGameObject Transformは両方が位置を持てるため、どちらを正とするか決めないとFeedback Loopになる。

| Body種別 | 基本的なData方向 |
| --- | --- |
| Static | Scene TransformからPhysicsへ登録し、通常は動かさない |
| Kinematic | Game/Animationの目標TransformをPhysicsへ渡す |
| Dynamic | Physics計算結果をGameObject/Renderingへ反映する |

**Fixed Step**

可変`deltaTime`をそのまま物理積分へ入れると、Frame Rateにより安定性と結果が変わる。AccumulatorへFrame時間を加え、固定幅で0回以上Stepする。

```text
accumulator += frameDelta
while accumulator >= fixedDelta:
    physicsStep(fixedDelta)
    accumulator -= fixedDelta
alpha = accumulator / fixedDelta
renderTransform = interpolate(previous, current, alpha)
```

- Frame落ち時に無制限Catch-upすると、物理Step増加がさらにFrame落ちを生む。最大Step数や時間上限を設ける。
- Rendering補間は表示を滑らかにするが、Physics結果そのものを未来へ進めるわけではない。
- 弾が薄いColliderを飛び越えるTunnelingは、Fixed Stepを細かくするだけでなくCCD、Shape Cast、Ray Castを検討する。

**衝突不具合の調査**

弾と敵の名前だけをLogへ出すのではなく、両者のWorld座標、Shape中心、半径/Extents、速度、前Frame位置、Collision Layerを同じFrameで記録する。見た目のMeshとColliderのTransformが同一とは限らない。

### U-8. 所有権と寿命を具体例で説明する

Pointerの種類を答えるだけでは不十分で、誰がObjectを生存させ、誰が参照し、破棄通知をどう扱うかを説明する。

**例: GameObjectとComponent**

- GameObjectがComponentを所有するなら、GameObject破棄時にComponentも破棄される。
- Componentが別GameObjectを参照する場合、その参照は所有ではないことが多い。
- Raw Pointerを非所有参照として使うなら、参照先の先行破棄でDangling Pointerになる。
- UUID/Handleで都度解決すれば無効化を検出しやすいが、Lookup Costと、解決できなかった場合の処理が必要になる。
- `shared_ptr`へ置き換えるだけでは循環参照や意図しない寿命延長が起きる。

**破棄を1Frame遅延する理由**

Update中のContainerから即時EraseするとIteratorや参照を無効化する。Destroy要求をMarkし、安定した同期点でPhysics、Audio、Render Proxy、Callback等を解除してから本体を破棄する方式がある。

**説明すべき不変条件**

- 生存ObjectだけがRegistryから解決できる。
- 破棄済みComponentへUpdate/Callbackしない。
- GPUが参照中のResourceをFence完了前に再利用・解放しない。
- Deserialize中の一時的な未解決参照を、Load完了後に解決する。

### U-9. Serializationを「保存できる」以上に説明する

SerializationはMemoryのByte列をそのまま保存することではない。Runtime Address、Pointer、Padding、Platform依存型は永続IDや定義済み表現へ変換する。

**保存時**

```text
Objectを列挙
  → 型とUUIDを保存
  → ComponentとFieldをSchemaに従って保存
  → Object参照をUUIDへ変換
  → Version情報を付与
  → 一時Fileへ書込み、成功後に置換
```

**読込時**

```text
Format/Versionを検証
  → Objectの器を生成してUUID表を作る
  → Componentと値を復元
  → UUID参照を実Objectへ解決
  → 欠損・未知Fieldを既定値または保持方針で処理
  → Post-load処理を実行
```

2段階に分けるのは、参照先がFile後方にあり、読込時点でまだ生成されていない場合があるため。

**互換性**

- Field追加は既定値を用意する。
- Renameは旧名のAliasまたはMigrationを持つ。
- 削除Fieldを無視するかUnknown Dataとして保持するかを決める。
- 型変更は暗黙変換に頼らずVersion Migrationする。
- Component順序をID代わりにすると、途中追加で旧Dataの意味がずれる。安定IDまたは末尾拡張規則が必要になる。

### U-10. Multi-threadingを依存関係から説明する

「Threadを増やせば速い」ではない。Taskを分けられる条件は、入力が確定しており、出力先が競合せず、同期Costより仕事量が大きいことである。

**Task Graphとして考える例**

```text
Input確定
  ├─ Animation評価 ─┐
  ├─ AI更新 ───────┼─ Transform確定 ─ Culling ─ Render Command
  └─ Physics Step ─┘
Asset I/O ─ Decode ─ GPU Upload ─ Fence完了 ─ 利用可能
```

矢印は依存であり、同じ段のTaskだけが必ず並列化できるとは限らない。Shared Worldへ書く場合は所有範囲を分けるか、Thread Localへ結果を出して最後にMergeする。

**理解している説明**

- Data Raceは結果がたまたま正しく見えてもC++上は未定義動作になる。
- Mutexは複数値の不変条件を守れる。Atomic単体へ置換しても複合状態の整合性は守れない。
- False Sharingは別変数でも同じCache Lineへ書くことでLineを奪い合う。
- Jobを細かくしすぎるとQueue、Wake、Atomic CounterのOverheadが支配する。
- GPUとCPUの並列性を保つには、直前Frame結果のReadback待ちや過剰なFence待ちを避ける。
- 並列化後は平均時間だけでなく、競合時のTail Latencyと再現性を測る。

### U-11. TCP共同編集をMessage単位で説明する

TCPは信頼できるByte Streamであり、`send`1回と`recv`1回が対応するMessage APIではない。

**Framingの例**

```text
[固定長Header: payloadSize, type, revision]
[payloadSize byteのPayload]
```

- Header自体が分割受信されるため、必要Byte数が揃うまでBufferへ蓄積する。
- 複数Messageが1回で届く場合も、Lengthを使って順番に切り出す。
- Payload Sizeへ上限を設け、巨大値によるMemory確保を防ぐ。
- Network Byte Orderや文字Encodingを固定する。

**再接続**

1. Clientが最後に適用したRevisionを送る。
2. Serverが保持するChangeLog範囲内なら差分を再送する。
3. 範囲外ならSnapshotを送り、そのRevision以降の差分を続ける。
4. UUIDとProject IDを検証し、別Projectの変更を混ぜない。

**競合**

- File全体のLast Write Winsは別Fieldの変更まで失う。
- Object/Component/Property単位のRevisionなら競合範囲を狭められる。
- Lockは競合を減らすが、切断時のLease/Timeout、Granularity、待機UXが必要になる。
- 順序保証があってもApplication Levelで重複適用、権限、Validation、Schema差を処理する必要がある。

### U-12. Profilerを使った仮説検証を説明する

最適化では、症状から原因を決めつけず、測定で範囲を狭める。

**例: Frame Rateが低い**

1. Frame TimeをCPUとGPUへ分ける。
2. GPU待ちでCPUが止まっている時間を、CPU処理時間と誤認しない。
3. GPU Boundなら解像度を一時的に下げ、時間が比例して下がるかを見る。
4. PassごとのGPU TimestampでShadow、Lighting、Post Process等へ分ける。
5. CPU BoundならUpdate、Culling、Command生成、Allocation、Lock Waitへ分ける。
6. 代表Frameだけでなく、中央値、95/99 Percentile、Spikeを確認する。

**改善の報告形式**

```text
条件: Scene、解像度、Object/Light数、Build構成
問題: どのMetricがBudgetを超えたか
仮説: 何が原因と考えたか
変更: 何の仕事量・同期・帯域を減らしたか
結果: Before/Afterと誤差
品質: 見た目や機能を維持したか
残課題: 別規模や別GPUでの限界
```

「速くなりました」だけでは理解を示せない。例えばDraw Callが半減してもGPU Pixel Costが支配的ならFrame Timeはほぼ変わらないため、因果関係まで説明する。

### U-13. 規模を変えたときの限界を説明する

設計を理解しているかは、現在動く理由だけでなく、入力規模を増やしたときにどこから壊れるかで分かる。

| 規模変更 | 最初に疑うもの | 次の方式候補 |
| --- | --- | --- |
| Object数が10倍 | Update全走査、Culling、Draw Call、Allocation | Spatial Index、Batch、GPU/Indirect Culling、Update LOD |
| Light数が10倍 | Object×Light評価、Shadow Pass、Buffer上限 | Tiled/Clustered Lighting、Shadow選別/Cache |
| Texture数・解像度が増加 | VRAM、Upload、Descriptor、Load時間 | Streaming、圧縮、Mip Budget、Residency管理 |
| Dynamic Bodyが増加 | Broad Phase、Contact、Solver、同期 | Layer、Sleep、Island、Simulation LOD |
| Particle数が増加 | CPU Update、Upload、透明Overdraw | GPU Simulation、Indirect Draw、低解像度描画 |
| 同時編集者が増加 | Broadcast量、Lock競合、ChangeLog | Interest管理、Property Revision、Server分割 |
| Sceneが巨大化 | Float精度、Serialize時間、Editor操作 | World Origin、Chunk/Cell、Incremental Save |

回答ではBig-Oだけでなく、実際の定数CostとHardware側の限界を分ける。例えばGPU Particleは更新Costを移せても、透明PixelのOverdrawは残る。

### U-14. 「改善するなら」を設計変更として説明する

改善案は新技術名を挙げるだけでなく、導入境界と移行順を説明する。

```text
1. 現在の制約をMetricで定義
2. 新方式が減らすCostを特定
3. 必要Dataと既存Dataとの差を確認
4. 最小導入範囲を決める
5. 旧方式とのFallback/互換を決める
6. 正しさ・性能・品質の合格条件を決める
7. 段階的に切り替え、比較後に旧経路を整理
```

例としてGPU Cullingを完成させる場合、「Compute Shaderを使う」だけでは足りない。GPUで生成したVisible ListをIndirect Argumentへ変換し、Command SignatureとResource Barrierを整え、CPU ReadbackをFrame Loopから外し、非対応経路のFallbackとDebug可視化を用意する必要がある。

### U-15. 弱点を正直に説明して評価へ変える

未完成や弱点を隠すと、少しの追加質問で矛盾する。次の順で答える。

```text
現在できている範囲
→ できていない範囲
→ できない技術的理由または未接続箇所
→ 現在の影響
→ 確認方法
→ 改善するときの最初の一手
```

例えば「Shader Fileがある」ことと、「PSOへ組み込まれRuntimeでDispatch/Drawされる」ことは別である。Data Layer、UI、Serializeまであっても、Runtime処理が無ければ機能完成とは言わない。Build成功はCompile/Linkの確認であり、Play時の挙動や描画品質の確認とは分ける。

### U-16. AI面接官の採点基準

AIへ模擬面接を依頼するときは、回答を次の4段階で採点させる。

| 段階 | 状態 | 判定例 |
| --- | --- | --- |
| 0: 未理解 | 用語が不正確、処理の前後が繋がらない | 「GPUだから全部速い」 |
| 1: 用語理解 | 定義は言えるが、実装と弱点へ繋がらない | 「CSMは影を分割する方式」だけ |
| 2: 実装理解 | Data Flow、Class/Pass、失敗条件を説明できる | 入出力、順序、所有者を説明できる |
| 3: 設計理解 | 代替、規模限界、計測、移行案まで説明できる | 要件が変わった場合の方式変更を提案できる |

**AI面接官へ渡す指示**

```text
回答中の専門用語を1つ選び、「それは内部で何をしているか」と追加質問してください。
数式が出た場合は、各項の意味と0または極端な値でどうなるかを質問してください。
高速・安全・綺麗という表現が出た場合は、比較対象と計測方法を質問してください。
Engine固有の回答には、主要Class、Data所有者、CPU/GPUの役割、1Frame内の順番を質問してください。
一般論の回答には、このEngineで実装済みか、未実装か、どのSourceで確認できるかを質問してください。
弱点を答えたら、入力規模を10倍にした場合と、改善の最小変更を質問してください。
採点は上表の0〜3とし、不足した層を具体的に返してください。
```

### U-17. 理解を確認する実践課題

次の課題は、暗記だけでは回答しにくい。口頭で図や擬似Codeを使いながら説明する。

1. World行列に非一様Scaleを含むObjectで、位置は正しいのにLightingだけが壊れる理由を説明する。
2. GPU Culling結果をCPUへReadbackする実装と、`ExecuteIndirect`へ接続する実装のFrame同期を比較する。
3. Cameraを急回転したときTemporal SSRに残像が出る経路を、Motion Vectorから履歴棄却まで追う。
4. 1万ObjectのComponent Updateが重いとき、計測せずECSへ全面移行してはいけない理由を説明する。
5. Scene FileへComponent Fieldを途中追加したとき、旧Versionで値がずれる設計と、ずれない設計を比較する。
6. Dynamic BodyのTransformをGame側とPhysics側の両方から毎Frame書いた場合に起きる問題を説明する。
7. TCPでJSONを送ったところ、まれに2件が連結してParse Errorになる理由と修正方法を説明する。
8. Point Lightを100個へ増やしたとき、CPU、GPU、Shadow Mapのどこが増えるかを式または仕事量で説明する。
9. Draw Callを半分にしてもFrame Rateが変わらない状況を3つ挙げ、判別するProfiler値を答える。
10. GameObject削除直後に別ComponentのCallbackが発火するCrashについて、所有権、登録解除、遅延破棄の観点から調査する。
11. Roughnessを下げるとHighlightがどう変わり、なぜ単純な明るさ調整ではないか説明する。
12. Camera Near Planeを極端に小さくしたとき、遠方のDepth Precisionが悪化する理由を説明する。
13. Compute ShaderのGroup Sizeを大きくすれば必ず速くなるわけではない理由を説明する。
14. Assetを非同期Loadするとき、File I/O完了とGPUで利用可能になる時点が別である理由を説明する。
15. 共同編集でProperty単位のRevisionを使っても解決できない競合例を挙げ、方針を説明する。

各課題への回答は、**現象 → 原理 → Data Flow → 観測値 → 修正候補 → 副作用**の順で組み立てる。
