# ManoEngine 全体内部設計

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

`Tools/ManoLauncher`はEngine/Project配布、Version固定、参加コードを扱う別Executable、`Tools/ManoTeamServer`は共同制作の中継とRevision確定を扱う別Executableである。

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

共同制作の内部詳細は本書を正とする。Engine本体はScene/Asset/Lock/Presence/TeamItemを扱い、ManoTeamServerはProject識別、Heartbeat、Revision、履歴中継を担当する。Launcherは参加コードと初期Project/Engine取得を担当する。

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
| Project配布 | ManoLauncher、Version固定、Hub、Invite、参加コード |
| 共同制作 | TCP/LAN/VPN、ManoTeamServer、Protocol 3 |

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

`.mano-invite`はProject、Hub、Engine、共同制作接続先を記録するJSON案内Fileで、Passwordや秘密Tokenを保存しない。

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

発光マテリアルから生成する簡易Emissive Lightは別枠で最大32灯。CPU側の`kMaxEmissiveLights`とHLSL側の`MANO_MAX_EMISSIVE_LIGHTS`は同じ値を保つ。

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
| `CollaborationProtocol.h` | EditorとManoTeamServerが共有するProtocol Version、Message名、上限、Project ID検証 |
| `ManoTeamServerMain.cpp` | Project単位のRevision確定、Change配信、履歴再送、Heartbeat、最大接続数、状態/PID File |
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
  -> Editor / ManoTeamServer間でHandshake
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
- ManoTeamServerの`--project-id`

Launcherで新規Projectを作る場合はUUIDをProject IDとして生成する。Project公開時はProject Metadata側のIDを優先し、配布設定に残った過去のIDでManifestを作らない。Invite生成も選択ProjectのIDを優先する。

ManoTeamServerはPID/状態FileへPort、Process ID、Revisionに加えてProject IDを記録する。Editorが同一PortのServerを調べ、現在Projectと異なるIDの古いServerであれば停止して現在IDで再起動する。これにより、過去ProjectのServerが残ってHandshakeだけ失敗し続ける状態を避ける。

### 4. Change EventとRevision

Editor上の同期操作は`EditorTeamChangeEvent`へ正規化する。主要な識別子は次のとおり。

| Field | 意味 |
| --- | --- |
| `changeId` | 変更自身の一意ID。再送・Commit照合にも使用 |
| `baseRevision` | 変更作成時に送信者が基準にしたRevision |
| `revision` | HostまたはManoTeamServerが確定した全体順序 |
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

ManoEngine内蔵Script Editorは存在しない。ScriptLineは行番号とコード文脈を保持するが、共同カーソル、選択範囲、同じコード範囲の警告、Gutter Icon、厳密な行Jumpは別機能が必要である。

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

EditorとManoTeamServerの双方を更新し、互換性を破る場合は`kCollaborationProtocolVersion`を上げる。旧Buildを暗黙に受け入れず、Handshakeで明確な拒否理由を返す。

### 14. 調査用チェックリスト

1. 接続診断のProtocol、Project ID、Engine Version、Role、Revisionを両端で比較する。
2. ManoTeamServerの状態Fileに現在Project IDがあるか確認する。
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
- `Tools/ManoTeamServer/ManoTeamServerMain.cpp`
- `Tools/ManoLauncher/LauncherExperience.h/.cpp`
- `Tools/ManoLauncher/LauncherGui.cpp`
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

`.haptic` は他の ManoEngine テキスト Asset と同じ `Key|Value` 形式。

```
ManoEngineHapticClip|1
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
Online::SetPlayerIdentity("player-0001", "Mano");

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

Engine が自動で付ける Header: `X-ManoEngine-Game-Id` / `X-ManoEngine-Environment` /
`X-ManoEngine-Client-Key` / `X-ManoEngine-Player-Id`。

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
| 91 | 最終構成 | ✓ | 下記ツリー（ファイル名は ManoEngine の命名へ合わせた） |

#### 最終構成（仕様書 91 項）

```
ManoEngine
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

ManoEngineの描画BackendはDirectX 12であり、OpenGL Backendは使用していない。この外部連携層にもOpenGL Context、GL Texture、GL Bufferは存在しない。

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
