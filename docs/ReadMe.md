# CG2Engine ドキュメント案内

## 概要

更新基準: 2026-09-26

このFolderは、CG2Engineの利用手順、内部設計、Component、C++ Script APIを6文書に集約する。機能ごとに文書を増やさず、利用者向け内容は`user-guide.md`、実装者向け内容は`engine-internals.md`の章として追加する。

Engine配布、Launcher、Version固定、Migration、環境診断は[user-guide.md](user-guide.md)を参照する。

| 文書 | 対象 |
| --- | --- |
| [user-guide.md](user-guide.md) | Project作成、Editor操作、Window、実践手順、問題対処、配布、共同制作、外部機能の利用方法 |
| [engine-internals.md](engine-internals.md) | 現行機能、所有関係、Runtime、描画、保存、Asset、Lighting、共同制作、外部機能の内部契約 |
| [component-reference.md](component-reference.md) | 全287 Component、Inspector Field、既定値、依存、Runtime契約 |
| [script-api-reference.md](script-api-reference.md) | Native Script lifecycle、413 Runtime API、Wrapper、26 Template、Field API |
| [documentation-authoring.md](documentation-authoring.md) | 文書を更新するときの調査方法、根拠、記載品質、完成監査 |
| [ReadMe.md](ReadMe.md) | 文書構成、現行監査値、主要更新履歴 |

### 現行監査値

| 対象 | 2026-09-26時点 |
| --- | --- |
| Component | 287種類（追加可能283＋Legacy互換4） |
| Runtime API | 415 Entry（Version 15でSpeechの発話中 / 推論中状態2件を追加） |
| 利用者向けC++ Class | 79件（Version 14でSpeech / Vision / Haptic / HapticVoice / Onlineを追加） |
| Script Template | 26件 |
| Inspector Field期待値 | 2,011 Field / 278 Component |
| チェックイン済みField Registry | 2,011 Field / 278 Component（期待値と一致） |

Field Registry（`EditorLogFieldRegistry.generated.cpp`）は`Tools/generate_log_field_registry.py`の生成物である。**Inspectorへ行を追加・変更したら必ず再実行してコミットする**。2026-09-13の再実行で、それまで残っていた「19 Field不足・PerformanceSettings未登録・LightProbeGroupの旧Field参照」を解消した。

Version 14のSpeech / Vision / Haptics / Onlineは専用Wrapperと専用Debug Windowを持つ。Version 15ではSpeechへ発話中 / 推論中状態とUI表示補助を追加した。2026-09-26に生成Registryも再生成し、外部連携の数値・Bool・Vector3・GameObject参照33 Fieldを汎用Property/ログ監視へ追加した。文字列と可変長配列は生成Registryの対象外であり、専用Wrapperを使う。

過去章に「追加時点の件数」が残る場合は履歴であり、現行値にはこの表と各文書の最新監査章を使う。文書上のソース照合と、Build、Play、描画目視、音声出力、2台間通信の実機確認は分けて記録する。

### 現行仕様の読み方

`README.md`には初期実装時点の評価・未対応表が履歴として残っている。現在の分野横断状態、コードの所有関係、処理順は`engine-internals.md`を正とする。個別Field/APIはComponent・Scriptリファレンスを参照し、矛盾する古い評価行は現行仕様章を優先する。

## 2026-09-29 更新: Release構成の無検査経路を閉じる / 確保器の取り違え防止 / 毎フレームの空処理削除

保守性・安全性・安定性・可読性・無駄の排除を対象に、機能追加を伴わない改善を行った。
Debug/x64 と Release/x64 の両方で 0警告0エラー、`Tests/RunNativeSmokeTests.ps1` 3件成功、
`Tools/CheckSourceHygiene.ps1` 違反0を確認している。

| 分野 | 内容 | 根拠 |
| --- | --- | --- |
| **HRESULT検査** | `assert(SUCCEEDED(x))` 49箇所を `EDITOR_HR_OK` / `EDITOR_HR_VERIFY` へ置換。`assert`はRelease(NDEBUG)で消えるため、従来 `ResizeBuffers` / `GetBuffer` / `Map` / `CreateCommittedResource` の失敗がRelease構成で完全に無検査だった | `Source/Engine/Core/EditorHrCheck.h` |
| **null参照の遮断** | 上記の失敗後にnullを触っていた6経路へガードを追加。GpuParticleの`Map`失敗時のnull書き込み、SwapChain resize失敗時の`CreateRenderTargetView(nullptr)`、Depth生成失敗時のDSV作成、`Finalize`の無条件`Release` | `EditorPlatformManager.cpp` / `EditorSharedState.h` / `EditorGpuParticleManager.cpp` |
| **確保器の取り違え防止** | グローバル`operator new`置き換えで aligned かつ nothrow の4種が欠けていた。欠けた形だけCRT側が使われ、`_aligned_free`へ渡る組み合わせが成立しうる。20種すべてを揃えた | `EditorProfilerAllocationTracker.cpp` |
| **確保コストの削減** | Engine全体の`new`が毎回通る`RecordEditorProfilerAllocation`が別TUの非inline関数だった。Profiler OFF時の判定をHeaderへ移し、確保ごとの関数呼び出しを無くした | `EditorProfilerAllocationTracker.h` |
| **毎フレームの空処理** | `GameScene::Update()`が呼んでいた空実装`Update()`を10件削除（宣言・定義とも）。「Updateは空実装」と書かれたコメント8行も1つの説明へ統合 | `GameScene.cpp` ほか10クラス |
| **失敗の可視化** | 診断Windowへ「DirectX失敗」タブを追加。HRESULT失敗とShader compile失敗の件数をタブ名に出す。失敗行は`logs/<日時>.Log`にも残る | `EditorDiagnosticsWindowManager.cpp` |
| **テストのビルド** | `Tests/*.cpp` 3件はどの構成にも登録されておらずビルドされていなかった。`Tests/RunNativeSmokeTests.ps1`で個別exeとしてビルド・実行する（3件成功） | `Tests/RunNativeSmokeTests.ps1` |
| **文字化けの再発防止** | BOMなしUTF-8だった14ファイルへBOMを付与。既存の文字化け4,239個（漢字部分は先行バイトが失われ復元不能）を上限とし、増加・BOM欠落・`assert(SUCCEEDED(`再導入を検査する | `Tools/CheckSourceHygiene.ps1` |
| **巨大関数の分割** | `EditorRenderManager::Draw()` 5,441行 → **5,190行**。後段ポストプロセスの9 Passを無名namespaceの関数へ切り出した（Filter / Sharpen / Bloom / Glare / DoF / MotionBlur / AutoExposure / SMAA / BackBuffer合成）。GPU計測イベントの粒度は維持している | `EditorRenderManager.cpp` |
| **Development構成の修復** | `Development|x64`が`NDEBUG`も`_DEBUG`も定義しておらず、Blastヘッダを含む翻訳単位が**コンパイル不能**だった（変更前から）。同構成は`PhysicsSdk\lib\release`をリンクするため`NDEBUG`を追加。3構成すべて0警告0エラーになった | `CG2.vcxproj` |
| **手順の欠落** | `Tools/generate_log_field_registry.py` が`.gitignore`の`Tools/*`で未追跡だった。「Inspectorへ行を追加したら再実行してコミットする」と規定しているのに、クローン先に生成器が無い状態だったため追跡対象へ戻した。再実行して差分0（チェックイン済みRegistryは最新）。あわせて上表のField数を実測値2,011へ修正（1,981は30件古い） | `.gitignore` |

調査して**問題が無いと確認できた**もの（今回は変更していない）:

- 描画ループ内のファイルI/O（環境Texture・Color LUT）はdirtyフラグで制御済み
- 毎フレームのPSO / RootSignature生成は無し
- Window Panelは可視判定より前に重い処理をしていない
- `FindGameObject`はhash索引つきで、Miss時の索引再構築storm対策もある
- `EditorLightProbeManager::UpdateGrid` / `FillGridData` は設定差分判定と早期returnで済んでいる
- `EditorComponent`の全POD Fieldは`CreateComponent`が既定値を入れている（取りこぼし0件）
- 初期化子なしの`EditorComponent`宣言は0件（未初期化POD読み出しは発生しない）

### `Draw()` 分割の手順（残り約17 Passも同じ形で進める）

作者が区切りコメントで26 Passに分けているので、これを1つずつ関数へ移す。手順は次の通り。

1. `Draw()` 冒頭の約90行は `auto& commandList = g_commandList;` のような **EditorSharedState グローバルへの別名定義だけ**である。したがって抽出先の関数はこれらを引数で受けず、`g_` を直接参照する。引数はフレームごとに変わる値（Pass間で受け渡すSRV Handle、`ppSettings`、Viewport）に絞る。
2. 入力と同じRenderTargetへ書けないPassは、`PostProcessSource`（SRV Handle + Resource）を受けて返す形にする。HDRとCompositeを交互に使う既存の作法をそのまま関数境界へ写せる。
3. GPU計測イベント（`profilerManager.BeginGpuEvent` / `EndGpuEvent`）はPass境界にまたがるので`Draw()`側に残す。関数へ取り込むと計測粒度が落ちる。
4. 変数の渡し漏れ・型違いはコンパイラが検出する。1 Passごとにビルドして進める（実際に`bloomModeIndex`の取り残しをコンパイラが検出した）。
5. `PostProcessSettings`は`.cpp`の無名namespace内の型なので、抽出先はメンバ関数ではなく同じ無名namespaceの関数にする。ヘッダを触らずに済み、Pass処理は元々実装詳細なので設計上もこちらが正しい。

残る課題（未着手。いずれも実機確認を伴うため分離して行う）:

- `EditorRenderManager::Draw()`は5,190行。残る最大の塊は「Scene rendering to HDR RT」1,353行で、ここは別途分解が必要
- `Draw()`内に関数内`static`が15個ある（フレーム跨ぎの隠れ状態）。分割を進めるならメンバ変数へ移すのが前提になる
- `EditorScene::LoadScene()` 3,720行 / `SaveScene()` 2,602行 / `CreateComponent()` 1,894行
- `CG2.vcxproj`がThirdPartyライブラリを`C:\kogakuin\LE1\CG2\...`の絶対パスで参照している。別のマシンへcloneするとリンクできない
- `EditorComponent`（1,820行・1,596 Field）の既定値が宣言から約2,000行離れた別ファイルにある。宣言側のMember初期化子へ移すと1箇所管理になるが、Scene既定値が変わらないことの実機確認が必要
- `EditorSharedState.h`が可変グローバル332個を持ち47ファイルから`using namespace`されている
- `Engine/Input`がトップレベルにあり`Source/Engine/*`の配置規則と揃っていない
- CIが無く、上記スクリプトはすべて手動実行

## 2026-09-13 更新: UI/Text・Prefab/Scene Streaming・Terrain/Foliage・Editor制作安定性の改善

インディー基準70点を目標に、次の4分野をまとめて改善した（1〜5人規模のWindows向けゲーム制作でその分野が大きな障害にならないことを狙う。Unity相当・巨大なWorld Partition/Nanite相当は対象外）。

| 分野 | 前 → 後 | 実装した制作導線 | 残る制限 |
| --- | ---: | --- | --- |
| UI / Text | 55〜75 → **70** | Text体裁13 Field（Font Asset / Size / 横縦揃え / 折返し / はみ出し3モード / 縁取り / 影）、Project内Font AssetのRuntime読込、Script API `Ui`クラス（Text/Color/FontSize/Interactable/Slider/Toggle）、Gamepad UI Navigation | Rich Text（インライン色/太字指定）は未対応。TextMeshPro完全互換は非対象 |
| Prefab / Scene Streaming | 55 → **68** | Additive Load/Unloadで全Scriptを再Startしない`EditorScriptManager::StartAdditive()`、`MergeScene`のUUID衝突解消。PrefabはApply / Revert / Variant / 明示Overrideに対応 | Unity相当の任意Property差分表示と完全なNested Prefab編集は未対応。Nestedは保存時に実体化される場合がある |
| Terrain / Foliage | 55 → **65** | `EditorTerrainHeightField`でHeightMapをCPU展開し、描画と同じ式でTerrainColliderを生成（従来は平らな箱にFallbackし当たり判定が地形に追従しなかった）、Script API `Terrain`クラス（GetHeightAt/Contains） | 高さ編集ブラシ・スプラットマップ・Foliage Paint/Eraseは未対応 |
| Editor Architecture / 制作安定性 | 55〜60 → **70** | `SaveScene`を一時ファイル+rename化（失敗時に元データを破壊しない）、未知の行を保持して再保存で消さない、Play中のScene保存/読込ガード、Missing Texture通知、Undoスタック上限化 | 巨大ファイルの責務分離（EditorScene.cpp等）自体は未着手。既存Public APIは全て維持 |

- Terrain Colliderの追従はコードレベルで検証（`Tests/TerrainHeightFieldSmoke.cpp`）: 頂点シェーダと同じ式で低地/高地/境界補間/範囲外Clamp/heightScale=0/Collider格子全頂点の高さが一致することを確認。
- Scene Save/Load・Additive Streamingの安全化は、既存の`SaveScene`/`MergeScene`/`StartAdditive`呼び出し経路（Scene・Prefab・共同編集の保存を含む）に1箇所ずつ手を入れる形で行い、大規模なRewriteはしていない。
- 検証: Debug/x64 Build 0警告0エラー、Editor起動30秒クラッシュなし、新規Script API 14件（UI 12 + Terrain 2）を含む全Wrapperの無効Handle安全性を自動確認。
- 詳細はTerrain/UIそれぞれ[component-reference.md](component-reference.md)・[script-api-reference.md](script-api-reference.md)の「Version 13で追加されたUI / Terrain API」を正とする。

## 2026-09-13 更新: Script APIの薄い4分野を実用レベルへ（ABI Version 12）

Runtime Script APIのうち、Component側に機能があるのにScriptから触れなかった4分野を拡張した。Runtime API総数は292→**355 Entry**、`kEditorScriptApiVersion`は11→**12**。追加Entryは全て構造体末尾へ置き、既存Entryの並びは変更していない（ABI維持）。追加分は既存Runtimeが毎フレーム参照している値・再生Instanceへ接続しており、Script側の書き込みは次のFrameから反映される。

| 分野 | 前 → 後 | 追加した操作 | 制限 |
| --- | ---: | --- | --- |
| Camera | 2 → **21** | FOV、Near / Far Clip、投影方式、Orthographic Size、Priority、有効状態、Active Camera取得・切替、LookAt、視線方向 | Viewport矩形の分割指定は対象外 |
| Audio | 6 → **25** | Handle方式の個別Voice制御（3D / 2D再生、Stop、Pause / Resume、Volume、Pitch、Loop、Bus、再生位置Get/Set、長さ、Fade） | Loop解除は現在の再生終了時に効く（XAudio2のBuffer属性のため） |
| Renderer / Material | 3 → **15** | 描画ON/OFF、Color、Opacity、Emission、Material Float / Color / Texture の名前指定Get/Set | Cast Shadowの個別切替はComponentに設定が無いため対象外 |
| VFX / Particle | 9 → **22** | Handle方式のInstance制御（Spawn / SpawnAttached、Stop、Pause / Resume、Restart、再生速度、位置、Particle数、Rotation / Scale） | 回転・拡縮はEffekseerのみ、Particle数は`.effectdef`のみ |

- 新規Handle型は`EditorScriptAudioHandle` / `EditorScriptVfxHandle`（`uint64_t`、無効値0）。内部のIXAudio2 Voiceポインタや`.effectdef` / Effekseerの区別はScriptへ公開せず、VFX Handleは上位bitへ再生経路を埋め込んで1つの型で両経路を扱う。
- Runtime機能追加は最小限に留めた。Camera Orthographic Size（従来10.0固定）をComponent化、Audioへ個別Voice制御とFade、VFXへPause / 再生速度 / Restartを追加した。
- 高水準Wrapperは`Camera`、`AudioVoice`、`VfxInstance`の3クラスを新設し、`Audio`と`Renderer`へメソッドを追加した。
- 検証: Debug/x64 Build 0警告0エラー、Editor起動クラッシュなし、`Tests/CameraAudioRendererVfxApiSmoke.cpp`で新規63 Wrapperが「Runtime API未接続」「対象が存在しない」の双方で`false`を返しCrashしないことを自動確認。**実Play中のGUI操作による目視確認（FOV変化、音量変化、Effect表示）は未実施**。
- 詳細な関数一覧と契約は[script-api-reference.md](script-api-reference.md)の「Version 12で追加されたCamera / Audio / Renderer / VFX API」を正とする。

## 2026-09-12 更新: インディー基準70点へ向けた Step 1〜6

次の6分野を、実装・Debug/x64 Build・起動確認まで完了した作業として記録する。各分野の「70」はインディー自作エンジンの標準評価を100とする評価表の途中到達点であり、エンジン総合点または完成宣言ではない。

| Step | 分野 | 前 → 後 | 実装した制作導線 | 未確認または意図的な制限 |
| --- | --- | ---: | --- | --- |
| 1 | Animation State Machine / Blend Tree | 40 → **70** | `.animgraph`保存、Animator Graph編集タブ、Parameter/State/Transition/Any State/Blend Sample/Event編集、選択Objectへの割当。 | GUI実クリックによるIdle/Walk/Run/Jump/Attack遷移。Layer、Avatar Mask、Nested State Machineは対象外。 |
| 2 | Asset Dependency | 45 → **70** | AssetIdベースの順・逆依存、Missing依存、移動時の索引更新、13種Text Asset抽出、Model→Texture抽出、Project Windowの依存表示・削除警告・Reimport再取得。 | 依存UIの目視、DLL依存、古いPathの自動修復、Node Editor型可視化は対象外。 |
| 3 | Project Settings | 50 → **70** | `ProjectSettings.cg2`、解像度、Window Mode、VSync、FPS上限、Audio 6系統音量、起動Scene/Build Sceneの編集をRuntimeへ接続。 | Audio音量とFPS上限の体感確認。解像度とWindow Modeは次回起動時に反映する。 |
| 4 | Input System | 55 → **70** | XInput最大4台、Dead Zone、感度、Trigger/DPad、切断時中立化、Keyboard+Gamepad複数Binding、Project Settings連携。 | 実機Gamepadの入力・抜き差し。Runtime Rebind UIとControl Schemeは未実装。 |
| 5 | Diagnostics / Profiler | 55 → **70** | CPU/GPU 240Frame履歴、時系列表示、VRAM、Audio/Physics/VFX/Asset数、Draw/Dispatch/Alloc、物理Body失敗をRuntime Statsとして集約。 | グラフとPlay中Counterの目視。全体Heap計測とRender Graph Node Editorは対象外。 |
| 6 | Navigation / AI | 55 → **70** | Destination、Stop/Resume、Path状態/失敗理由、残距離、Warp、経路計算、Debug Line、C++ Script API 9関数を追加。Play中のScene ViewにはSurface/Obstacle/Link/実経路を描く。 | 実SceneでのEnemy追跡。経路は完全A*ではない。 |

### 検証記録

- 全Step: Debug/x64 Build成功、警告ゼロ、Editor起動後の正常終了を確認済みという作業報告がある。
- Step 3: `1280x720`を設定して起動し、実クライアント領域が`1280x720`になることを測定済み。
- GUI実クリック、実機Gamepad、実SceneでのNavigationなど、表に記した未確認項目は完了扱いにしない。

Step 1の詳細な操作、保存契約、制限、手動確認手順は[user-guide.md](user-guide.md)を正とする。

初めてプロジェクトを開く利用者は、実装報告ではなく[Editor ワークフロー](user-guide.md)から始める。ここには各機能のクリック場所、設定値、失敗時に見る画面をまとめている。

## 旧評価課題メモ

旧内容の評価対象は、必須内容61点、球、Lambert/Half Lambert、複数Model、Utah Teapot、Stanford Bunny、MultiMesh、Suzanne、Lighting方式、FBX読込、ドキュメントであった。現行の詳細仕様は上記文書を正とする。

旧公開リファレンス: https://cg2-engine-docs.ms123112.chatgpt.site/
