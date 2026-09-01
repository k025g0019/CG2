# CG2 ウィンドウ・設定 詳細下書き

このファイルは、ChatGPT Work が使用者向けサイトを作るための「エディタウィンドウ」「メニュー操作」「Project設定」詳細素材である。
`docs/user-documentation-research-spec.md` は調査仕様、`docs/component-documentation-detail-seed.md` はComponent詳細、`docs/cpp-script-documentation-detail-seed.md` はC++ Script API詳細、このファイルはそれ以外（ウィンドウ・設定）のページ本文下書きとして使う。

調査対象は上部メニューバー「ウィンドウ」内の全項目と、「編集 > 設定を開く」で表示されるProject設定である。

## 共通で必ず書くこと

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

## ウィンドウメニュー全体像

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
| 描画負荷テスト Scene を作成 | 即実行（確認Popup経由） | `CreateRenderStressScene` |
| ゲーム基盤検証 Scene を作成 | 即実行（確認Popup経由） | `CreateGameplayFoundationValidationScene` |
| Console 表示 | 即実行 | `g_isConsoleCleared = false` |
| 選択解除 | 即実行 | `ClearSelectedGameObjects()` |
| レイアウト再構築 | 即実行（次回起動時反映） | `g_isDockLayoutInitialized = false` |

「アニメーション」〜「共同制作」の7項目は独立したDockableウィンドウで、チェックを外すまで開いたままになる。Scene保存とは無関係で、Windowの表示状態自体はSceneファイルへ保存されない（Editor起動ごとに既定の表示状態へ戻る想定）。

---

## 1. アニメーション ウィンドウ

- 目的: GameObjectのTransform・Light・Materialの値を時間軸で編集する `.animclip` アセットを、Timeline UIで作成・編集する。Unityの Animation Window に相当する。
- 開き方: メニュー「ウィンドウ > アニメーション」。
- 前提条件: 対象GameObjectに `Animation` Componentが必要（無ければ「Clipを割り当て」操作時に自動追加される）。Clip保存先は `Assets/Animation/` を想定。
- 実装: `Source/Engine/Editor/EditorAnimationWindowManager.h/.cpp`、Clip実体は `Source/Engine/Animation/PropertyAnimationClip.h`。

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| ツールバー | 保存、再読込、Preview開始/停止、Record開始/停止、時間設定（`DrawToolbar`）。 |
| Track一覧 | 追加済みProperty Trackの選択・削除。Combo Boxから新規Track対象Propertyを選び追加する（`DrawTrackList` / `AddPropertyTrack`）。 |
| Timeline | 秒目盛り、Track行、Keyframe、再生ヘッドを描画（`DrawTimeline`）。 |
| 選択Key編集 | 選択中Keyframeの時刻・値・接線（In/Out Tangent）・補間方式を編集（`DrawSelectedKeyEditor`）。 |
| Event編集 | 指定時刻でC++ Script / Effectへ通知するAnimation Eventを編集（`DrawEventEditor`）。 |

### アニメーション可能なProperty（`AnimationPropertyTarget`）

Transform: 位置X/Y/Z、回転X/Y/Z（度）、スケールX/Y/Z。
Light: 強さ、範囲。
Material: BaseColor R/G/B、Metallic、Roughness、Alpha、Emission強さ、Emission色R/G/B。

根拠: `Source/Engine/Animation/PropertyAnimationClip.h` : `enum class AnimationPropertyTarget`

### 補間方式（`AnimationCurveInterpolation`）

| 値 | 意味 |
| --- | --- |
| Step | 次のKeyまで値を変えない。ON/OFFや瞬間切替向け。 |
| Linear | 2Key間を一定速度で補間する。 |
| CubicHermite | 入出力接線を使い、滑らかな加減速を作る。 |

### 書き込みモード（`AnimationPropertyWriteMode`）

| 値 | 意味 |
| --- | --- |
| Override | カーブ値で元の値を置き換える。 |
| Additive | Play開始時の値へカーブ値を加える。 |
| Multiply | Play開始時の値へカーブ値を掛ける。 |

### Record（自動記録）の仕組み

`BeginRecording` でPreview姿勢を表示し、選択GameObjectの現在値を基準値として記録する。以後 `UpdateRecording` が毎Frame値を比較し、変化したPropertyだけを現在時刻のKeyとして自動追加する（`AddOrUpdateKey`）。「現在Transformを一括記録」ボタン（`RecordCurrentTransform`）は位置・回転・スケールを現在時刻へまとめてKey化する。

### Preview

`BeginPreview` で選択GameObjectを丸ごとBackupしてからPreview値を適用し、`RestorePreview` でBackupへ戻す。Preview中はSceneView上のGameObjectが実際にClipの値で動くため、Play中でなくても見た目を確認できる。`isPreviewPlaying_` がtrueの間はTimelineが実時間で進む。

### Play中の違い

Play中は物理・Script・Animatorが同じGameObjectを同時に書き換える可能性があるため、Preview編集とPlay実行を同時に行うと競合する。編集はPlay停止中に行うことを前提とする。

### 保存

`SaveAnimationClip` が編集中Clipを `animationClipPath_` （選択中の `.animclip` パス）へ書き戻す。`AssignClipToSelectedGameObject` は選択GameObjectへ `Animation` Componentを追加/更新し、ClipのAssetPathを設定する。

### 確認手順

1. Cubeを作成し `Animation` Componentを追加。
2. アニメーションウィンドウでPosition Y のTrackを追加。
3. 0秒と1秒にKeyframeを打ち、値を変える。
4. Previewを再生し、Cubeが上下することを確認する。

---

## 2. Spline Editor ウィンドウ

- 目的: `RailMovement` が参照するSpline（制御点の階層）を、上面(XZ)/側面(ZY)の2D投影キャンバスでマウス編集する。
- 開き方: メニュー「ウィンドウ > Spline Editor」。
- 前提条件: 2点以上の子GameObject（制御点）を持つ「Path」GameObjectが対象。名前に"Path"を含む、または子に"Point"で始まる名前が2つ以上あると自動的にSpline候補として認識される（`IsRailPathCandidate`）。
- 実装: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawSplineEditor`。

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| Spline選択Combo | Scene内のSpline候補から選ぶ。選択中GameObjectに `RailMovement` があれば自動追従する。 |
| 「新規Spline」ボタン | 4点を持つ標準SplineをSceneへ追加する（`CreateSpline`）。 |
| 「選択点の次へ追加」ボタン | 選択Rail末尾へ制御点を1つ追加する（`AddControlPoint`）。 |
| 上面XZ / 側面ZY ラジオボタン | キャンバスの投影軸を切り替える（`showsSideView_`）。 |
| Play Preview（Play中のみ表示） | 進行率スライダー、停止/再開ボタン、順方向/逆方向ボタンで実際のRail走行位置を操作できる。`EditorRailMovementManager::GetNormalizedProgress` / `SetNormalizedProgress` / `SetPaused` / `SetReverse` を呼ぶ。 |
| 制御点リスト（左） | 各制御点を選択、位置をDragFloat3で直接編集、「選択点を削除」（3点以上残る場合のみ）。 |
| 2Dキャンバス（右） | 制御点を円で表示し、ドラッグで移動できる。水色の線はCatmull-Romスプライン補間したプレビュー経路。 |

### プレビュー曲線の計算

`railUseSmoothCurve` がtrueならCatmull-Rom補間（`EvaluateSplineCatmullRom`、1区間16サンプル）、falseなら制御点間を直線でつなぐ。`railLoop` がtrueなら始点と終点をつないだループとして補間する。

### Play中の違い

Play中は「Play Preview」欄が追加表示され、実際に走行中のRail進行を確認・操作できる。制御点のドラッグ編集自体はPlay中も可能だが、Rail追従中のFollowerの挙動に即座に反映される。

### 保存

制御点はGameObjectのTransform（`translate`）として保存されるため、通常のScene保存でそのまま保存される。専用のSpline Asset形式は無い。

### 確認手順

1. メニューから「新規Spline」を押す。
2. 制御点をキャンバス上でドラッグして経路を変える。
3. `RailMovement` を持つGameObjectを作り、`railPathGameObjectId` にこのSplineを設定してPlay。
4. 意図した経路を通ることを確認する。

---

## 3. Event Timeline ウィンドウ

- 目的: 「経過秒」または「Rail進行率」のどちらかの軸上に、名前付きScript Action（`TimelineEvent` Component）とWave開始（`WaveSpawner` の `waveTriggerMode=1`）をマーカーとして配置・編集する。
- 開き方: メニュー「ウィンドウ > Event Timeline」。
- 重要な設計原則: **このウィンドウ自体はゲームルールを実行しない。** 時刻/進行率と名前付きActionを結び付けるだけで、実際に何が起きるか（攻撃、演出、BGM切替等）は、そのAction名を受け取るC++ Script側が決める。

根拠: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawEventTimeline` 内コメント「Event Timelineは時刻またはRail進行率と名前付きScript Actionだけを接続します。ゲームルールは実行しません。」

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| 表示軸Combo | 「経過秒」または「Rail進行率」を選ぶ（`timelineSourceMode_`）。 |
| 表示時間 | 経過秒モード時のみ。Timeline全体の長さ（秒）（`timelineDurationSeconds_`、1〜3600秒）。 |
| 「Eventを追加」ボタン | 現在の表示軸・選択GameObjectを使う `TimelineEvent` Componentを新規GameObjectとして追加する（`CreateTimelineEvent`）。 |
| Wave雛形関連 | 「選択ObjectをWave雛形にする」→選択GameObjectを複製元に指定。Wave個数（1〜64）、初期配置（横列/V字/円/グリッド）、配置間隔を設定し「Waveを作成」でPool付きWaveSpawnerを一括生成する（`CreateWaveFromSelection`）。 |
| Eventsキャンバス | 横軸が時刻(秒)または進行率(%)。各行が1つのEvent/Wave。水色マーカー=名前付きEvent、橙色マーカー=Wave開始。マーカーをドラッグすると発火時刻/進行率が変わる。 |

### マーカーのドラッグ

マウスでマーカーをクリック＆ドラッグすると、`timelineComponent->timelineTriggerValue`（経過秒モードなら秒、進行率モードなら0〜1）または `waveComponent->waveTriggerValue`（0〜1固定）が直接書き換わる。

### Play中の違い

編集内容はPlay開始後に評価される。Timelineウィンドウ自体はPlay中の進行位置を表示するプレイヘッド機能を持たない（現在時刻/進行率のインジケータ描画は無い。実行状況はGame ViewかLog監視で確認する）。

### 保存

`TimelineEvent` と `WaveSpawner` はいずれも通常のComponentなので、Scene保存に含まれる。

### 確認手順

1. 表示軸を「経過秒」にし、「Eventを追加」。
2. マーカーを5秒地点へドラッグ。
3. 対応するC++ Scriptに同名のAction関数を実装してPlay。
4. 5秒後にAction関数が呼ばれることを確認する。

---

## 4. State Graph ウィンドウ

- 目的: 1つの数値（Health比率またはRail進行率）を3段階の「State」に分け、State切替時に名前付きScript Actionを通知する `ThresholdState` Componentを、フローチャート風のUIで編集する。
- 開き方: メニュー「ウィンドウ > State Graph」。
- 重要な設計原則: Event Timelineと同様、**「Boss」「敵」「攻撃」等のゲーム固有概念はGraph側に無い。** 各Actionの意味は受信側C++ Scriptが決める。

根拠: `Source/Engine/Editor/EditorGameplayToolsWindowManager.cpp` : `DrawStateGraph` 内コメント「各Actionの意味は受信するC++ Scriptが決めます。Boss、敵、攻撃などの固有概念はGraph側にありません。」

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| State Graph選択Combo | `ThresholdState` を持つGameObjectから選ぶ。 |
| 「選択ObjectへThreshold Stateを追加」ボタン | 選択中GameObjectに未追加なら `ThresholdState` Componentを追加する。 |
| 値Source Combo | 「Health比率」または「Rail進行率」（`thresholdSourceMode`）。 |
| State 2 境界 / State 3 境界 | 0.0〜1.0のDragFloat。値がこの境界を超えるとStateが切り替わる（`thresholdSecondValue` / `thresholdThirdValue`）。 |
| State 1/2/3 Action | 各Stateに入った時に通知するAction名の文字列入力。 |
| フローチャート表示 | State1→State2→State3を矢印付きノードで表示。ノード内にAction名を表示する。 |

### 遷移方向の説明文

`thresholdSourceMode==0`（Health比率）なら「値が低下するとState 1 -> 2 -> 3」、`thresholdSourceMode==1`（Rail進行率）なら「値が上昇するとState 1 -> 2 -> 3」という説明文がGraph上部に表示される。

### 保存

`ThresholdState` は通常のComponentとしてSceneへ保存される。

### 確認手順

1. Healthを持つGameObjectを選び「Threshold Stateを追加」。
2. State 2 境界=0.6、State 3 境界=0.3等に設定。
3. 各StateのActionへ異なる名前を設定し、対応するC++ Scriptハンドラを実装。
4. Play中にダメージを与え、Health比率が境界を超えるたびに対応するActionが呼ばれることを確認する。

---

## 5. 診断・Profiler ウィンドウ

- 目的: 実行時の負荷計測、VFX状態、Scene静的検査、Replay記録・再生、GBuffer各種可視化、Auto Exposure Histogram、Render Graph構成確認を1つのタブ付きウィンドウにまとめる。
- 開き方: メニュー「ウィンドウ > 診断・Profiler」。
- 実装: `Source/Engine/Editor/EditorDiagnosticsWindowManager.h/.cpp`。9個のタブを持つ。

根拠: `Source/Engine/Editor/EditorDiagnosticsWindowManager.cpp` : `BeginTabItem("Profiler")` 以降9タブ分。

### 5-1. Profiler タブ

| 要素 | 説明 |
| --- | --- |
| GPU Frame / Objects / Instances | 画面上部に常時表示（`g_renderProfile`）。 |
| 計測時間（秒）入力 | 手動計測の長さを指定（`EditorProfilerManager::SetMeasurementDurationSeconds`）。 |
| 「負荷イベント計測を開始」ボタン | Play中のみ有効。計測を開始する（`SetEnabled(true)`）。計測中は進捗バーと「計測を停止」ボタンに切り替わる。 |
| 「結果を消去」ボタン | `Reset()`。 |
| 「呼出階層で表示」チェック | ONだとThread→GameObject→呼出Pathの順で階層ソートし、インデントで表示する。 |
| 結果テーブル（12列） | イベント名、処理元、GameObject、Thread、呼出回数、合計ms、Self ms、最大ms、DrawCall、Dispatch、Alloc回数、Alloc KB。EngineだけでなくNative Script DLL側の計測も同じ期間で集計する。 |

### 5-2. VFX タブ

Play中のみ内容を表示する。Stage1 VFX（Billboard/Flipbook/Ribbon/Ring）の状態を表示する。

| 表示項目 | 内容 |
| --- | --- |
| Active Effect数 / Active Emitter(Node)数 / Particle数 | `EditorVfxManager::GetDebugStats()` から取得。 |
| Effect Pool使用数 | 使用中 / 総容量。 |
| Effect一覧テーブル | Effect名、Node数、Particle数、描画方式、LOD Spawn倍率 / 追従有無。 |

### 5-3. Scene Validator タブ

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

### 5-4. Replay タブ

Keyboard 256キーと各FrameのdeltaTimeを記録・再生する。

| 操作 | 内容 |
| --- | --- |
| 「Scene先頭から記録」 | Play中なら一度停止し、記録開始とともに再度Playする。 |
| 「記録停止・保存」 | `runtime_cache/replays/last.cgreplay` へ保存。 |
| 「Scene先頭から再生」 | 記録済みFrameがある場合のみ表示。 |
| 「再生停止」 | Playback中のみ表示。 |
| 「前回Replayを読込」 | 保存済みFileを読み込む。 |

状態表示: 停止/記録中/再生中、現在Frame/総Frame数。

### 5-5. 描画バッファ タブ

GBufferの各チャンネルを画面全体でプレビューする。表示Comboで Albedo / Normal / Material（R=Roughness G=Metallic B=AO A=F0）/ Emission（EmissionとTransmission）/ Motion Vector（RG成分）を切り替える。

### 5-6. Before / After タブ

HDR入力（PostProcess適用前）と最終合成（PostProcess適用後）を左右に並べて比較する。

### 5-7. Material Preview タブ

Base Color / World Normal / Roughness・Metallic・AO / Emission・Transmission の4チャンネルを2x2で並べて表示する。

### 5-8. Scopes タブ

Auto Exposure実行後の輝度Log Histogram（-12EV〜+8EV）をヒストグラム表示する。Auto Exposure未実行時は「Auto Exposure実行後に輝度Histogramを表示します。」と表示される。

### 5-9. Render Graph タブ

主要9 Passの入出力とPSO準備状態（Ready / Missing）を一覧表示する（GBuffer、GTAO、SSGI、SSR、Ocean、Volumetric Cloud、Bloom / Glare、Final Composite、AA / Filter）。実際のPass順序の説明であり、Passを個別に無効化する機能ではない。

### 確認手順

Play開始→「負荷イベント計測を開始」→数秒待って「計測を停止」→結果テーブルで重いイベントを確認、という流れが基本。

---

## 6. ログ監視 ウィンドウ

- 目的: GameObject / Component / System の任意の値を選んで監視し、`logs/RuntimeLog.log`（pipe区切り、機械可読）へ記録する。Console(下部パネル)のように流れて消えるログとは別に、後から数値を追って調べ直せるようにする診断機構。
- 開き方: メニュー「ウィンドウ > ログ監視」。
- 実装: `Source/Engine/Editor/EditorLogMonitorManager.h/.cpp`（監視ロジック本体）、`EditorLogMonitorWindowManager.h/.cpp`（UI）。

### タブ構成

| タブ | 内容 |
| --- | --- |
| オブジェクト / コンポーネント | 選択GameObjectのField（位置/回転/スケール等）とComponent各値をチェックボックスで監視対象へ追加。Componentごとに折りたたみ（既定は閉じた状態）で、Inspectorと同じ日本語名で表示される。 |
| システム | Manager/Global State由来の値（Weapon、Physics、Rendering、GameState、Profiler(CPU ms)等）をカテゴリ別に折りたたみ表示（既定は閉じた状態）。日本語名で表示される。 |
| 監視中一覧 | 現在登録されている全Watch Entryをテーブルで一覧し、有効/無効、種別、カテゴリ、対象、状態を確認・編集する。 |

### 監視対象の3種類

| 種別 | 説明 |
| --- | --- |
| GameObjectField | `id`/`name`/`isActive`/`translate`/`rotate`/`scale`。登録不要で全GameObjectへ列挙できる。 |
| ComponentField | `EditorLogFieldRegistry.generated.h` 経由。Inspector描画コードから自動生成されたFieldのみ選べる。`(componentType, componentFieldKey)` という安定した文字列識別子で保存する（Registry再生成で並びが変わっても過去のWatch設定が壊れない）。 |
| SystemField | `EditorLogSystemProviders.h` 経由。Manager/Global State由来。 |

### 記録モード（`LogCaptureMode`）

| 値 | 意味 |
| --- | --- |
| EveryFrame | 毎Frame記録する。 |
| IntervalSeconds | 指定秒間隔で記録する。 |
| OnChange（既定） | 値が変化した時だけ記録する。負荷を抑えるための既定値。 |
| IntervalFrames | 指定Frame数ごとに記録する。 |
| Manual | 自動発火しない。手動記録ボタン押下時だけ記録する。 |

Float/Vector3のOnChangeには「変化Threshold」があり、`abs(現在値-前回値) >= threshold` の時だけ記録する（Vector3全体はベクトル距離で判定）。Bool/Int/GameObject参照は常に完全一致比較。

### RuntimeLogの形式

```text
Timestamp|Frame|Category|TargetKind|SourceId|SourceName|Component|Field|Value
```

Playを開始するたびに空になる。値が変化した時だけ行が増える（EveryFrame以外）ため、ある時刻の全項目を見るには近い時刻の複数行をまとめて読む必要がある。

### 安全設計

- GameObject解決は `EditorScene::FindGameObject` のO(1) Hash Map引きを使い、毎Frame全Scene走査はしない。
- Component側はEntryごとにRegistry index / Component slot indexをcacheし、型が一致する限り再探索しない。
- 対象が見つからない場合はMissing状態への遷移/復帰の時だけ1回記録し、毎Frame大量記録しない。
- Buffer上限、1Frame最大件数、File Size上限を超えた場合はDrop件数をログへ記録する（黙って落とさない）。

### 確認手順

1. 敵GameObjectのHealth Componentを選び、「HP」等のFieldにチェック。
2. Playして敵を攻撃。
3. `logs/RuntimeLog.log` を開き、値が変化した行が記録されていることを確認する。

---

## 7. 共同制作 ウィンドウ

- 目的: 複数人が同じProjectをネットワーク経由で同時編集するための、Scene変更同期・Asset変更同期・選択ロック・競合解決を行う。
- 開き方: メニュー「ウィンドウ > 共同制作」。
- 実装: `Source/Engine/Editor/EditorTeamCollaborationManager.h/.cpp`。ウィンドウタイトルは「TEAM - 共同制作」。

### 状態（`EditorTeamConnectionStatus`）

Offline / Connecting / Online / Synchronizing / Conflict / Disconnected の6状態。

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| Status / Revision / Members / Unsynced Changes / Conflicts / Editing Locks | 現在の接続状態と同期状況の数値表示。 |
| Membersリスト | 参加中ユーザー名とOnline/Offline状態を箇条書き表示。 |
| User Name / Host / Port 入力 | 接続設定。既定Port 45678。 |
| 「このPCをHostにする」チェック | ONなら「サーバー開始」ボタン、OFFなら「Hostへ接続」ボタンが表示される。 |
| 「Start Team Server Automatically」チェック | 起動時に自動でサーバーを開始するか。 |
| 「設定保存」ボタン | ユーザー名・Host・Port等をFileへ保存する（`SaveSettings`）。 |
| Conflicts欄（競合発生時のみ表示） | 競合したユーザー名・Scene・操作・Property・保存された競合コピーのFileパスを表示。「自分側を採用」「相手側を採用」「両方をMerge」（C++ Scriptアセットのみ）ボタンで解決する。 |

### 同期の仕組み（概要）

- Scene変更は `EditorTeamChangeEvent`（operation、property、oldValue、newValue、Scene全体のSnapshotデータ等を含む）としてキャプチャされ配信される（`CaptureSceneChanges`）。
- Asset変更は個別にScan・Hash比較され、変更があれば配信される（`ScanAssetChanges`）。
- 選択中GameObjectは他ユーザーへロック通知され、他ユーザーが同じObjectを編集しようとするとロック中である旨が分かる（`UpdateSelectionLock`、`IsGameObjectLockedByAnotherUser`）。
- 競合発生時は変更を即座に上書きせず、競合コピーを別ディレクトリへ保存してから使用者に選ばせる（`PrepareConflictCopies`）。

### 制限・注意

このドキュメント調査の範囲では、暗号化やアクセス制御の詳細は未確認。同一LAN内での使用を想定した設計に見える（Host IPを直接指定する方式）。実運用前にネットワーク環境の安全性を確認すること。

### 確認手順

1. PC Aで「このPCをHostにする」をON→「サーバー開始」。
2. PC Bで同じPort・PC AのIPを指定して「Hostへ接続」。
3. PC AでGameObjectを1つ動かし、PC Bへ反映されることを確認する。

---

## 8. 描画負荷テスト Scene を作成

- 目的: 描画負荷（Ocean、Terrain、Foliage、不透明、半透明OIT、屈折、Skinned Mesh、Particle衝突）を一括生成し、Profiler計測の基準Sceneを作る。
- 開き方: メニュー「ウィンドウ > 描画負荷テスト Scene を作成」→確認Popup「作成する」。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `CreateRenderStressScene`。

### 重要な注意

**現在の未保存変更は破棄され、`Assets/Scenes/RenderStress.scene` へ上書き保存される。** 実行前に必要なら現在のSceneを保存しておくこと。実行前にPlay中なら自動的に停止する。

### 生成される内容

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

### 確認手順

1. 実行後、Playして診断・ProfilerウィンドウのProfilerタブで計測。
2. GPU Frame msの内訳から重いPassを特定する。

---

## 9. ゲーム基盤検証 Scene を作成

- 目的: Prefab階層・Rail/Branch/Wave・照準/武器/Pool/被弾・汎用Sequence・非同期加算Scene・Checkpointという、汎用ゲーム基盤一式が動作することを1つのSceneで検証する。
- 開き方: メニュー「ウィンドウ > ゲーム基盤検証 Scene を作成」→確認Popup「作成する」。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `CreateGameplayFoundationValidationScene`。

### 重要な注意

**現在の未保存変更は破棄され、`Assets/Scenes/GameplayFoundationValidation.scene` へ上書き保存される。** 実行前にPlay中なら自動的に停止する。

### 生成される内容

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

### 確認手順

1. 実行後Play。
2. Rail Followerが分岐点で正しくPath Bへ移るか確認。
3. Wave発火でPoolから敵が湧くか確認。
4. マウスクリックで照準先へProjectile/Hitscanが飛ぶか確認。
5. 一定時間後にAction Sequenceが加算Sceneを読み込むか確認。

---

## 10. Console 表示

- 目的: 各Managerが積んだログメッセージ（物理エラー、Asset読込結果、保存結果など）を表示する下部パネルの「Console」タブを表示状態にする。
- 開き方: メニュー「ウィンドウ > Console 表示」。実体は下部パネル（`EditorBottomPanel`）内の1タブで、「Project」タブと並んでいる。
- 実装: `Source/Engine/Editor/EditorBottomPanel.cpp` : `BeginTabItem("Console")`。

### 画面構成・主な操作

| 要素 | 説明 |
| --- | --- |
| 「ログ消去」ボタン | `consoleMessages` 配列を空にし、`isConsoleCleared=true` にする（以後は非表示状態になる）。 |
| 「ログ表示」ボタン | `isConsoleCleared=false` にして再表示する。メニューの「Console 表示」も同じ効果。 |
| Scene / Play 状態表示 | 現在のSceneViewサイズと、Playing/Stoppedの状態を表示。 |
| メッセージ一覧 | 各Managerが `consoleMessages.push_back(...)` で積んだ文字列を、折り返し表示（`TextWrapped`）で古い順に列挙する。 |

### 注意

Consoleは全Managerの出力が同じ配列に時系列で混ざって流れるため、特定の値を追いたい場合は本ドキュメントの「6. ログ監視」の方が向く（物理Body生成の失敗理由などはConsoleとRuntimeLogの両方へ出す設計になっている箇所がある）。

---

## 11. 選択解除

- 目的: 現在選択中のGameObjectをすべて解除する。
- 開き方: メニュー「ウィンドウ > 選択解除」。
- 実装: `ClearSelectedGameObjects()` を呼ぶだけ。Inspectorは選択GameObjectが無くなるため、Project設定画面（本ドキュメント「12. 設定」参照）へ自動的に切り替わる。

---

## 12. レイアウト再構築

- 目的: Dockingレイアウト（各Windowの配置・サイズ）を既定状態へ戻す。
- 開き方: メニュー「ウィンドウ > レイアウト再構築」。
- 実装: `g_isDockLayoutInitialized = false` にするだけで、その場では画面が変わらない。Consoleへ「Window: レイアウト再構築は次回起動時に反映されます」と表示される。
- 注意: **即座には反映されない。** 次回Editor起動時に既定Dockレイアウトが再構築される。ウィンドウ配置を壊してしまった場合の復旧手段として使う。

---

## 13. 設定（Project設定 / メニュー「編集 > 設定を開く」）

- 目的: 特定のGameObjectに紐付かない、Project全体の設定（環境光、物理、操作ツール、Sceneカメラ操作感度等）をInspectorへ表示する。
- 開き方: メニュー「編集 > 設定を開く」、または「ウィンドウ > 選択解除」で選択GameObjectが無い状態にする。実体はGameObject未選択時のInspector描画内容そのもの。
- 実装: `Source/Engine/Editor/EditorMainMenuBar.cpp` : `OpenProjectSettings`（選択解除するだけ）、`Source/Engine/Editor/EditorInspectorPanel.cpp` : 各 `Draw*Panel` 関数群。

### 13-1. オブジェクト操作

選択中GameObjectがある時のInspectorにも同名の折りたたみが表示される。「複製」「削除」「Undo」「Redo」「Scene保存」「Scene読込」、Prefab関連（「Prefabとして保存」「Variantとして保存」「PrefabをSceneへ生成」「Prefabへ反映」「Prefabへ戻す」）のボタン群。

### 13-2. 環境 / 背景

| 項目 | 説明 |
| --- | --- |
| 背景色 | SceneViewのClear Color（`ColorEdit4`）。 |
| 環境画像を使う | 天球にHDR/DDS/PNG/JPG画像を使うか。ONの場合、選択中Assetが対応拡張子なら「選択中アセットを環境画像に設定」ボタンが使える。「環境画像を解除」で無効化。 |
| 環境画像の強さ / 回転 / 粗さ補正 | 環境画像のIntensity、Y軸回転（ラジアン）、Mip Bias。 |
| 天球上色 / 天球下色 | 環境画像未使用時の疑似スカイのグラデーション色。 |
| 天球明るさ / 天球放射 / 環境光 / 反射寄与 / 空の切替 | スカイの明るさ、Emission、Ambient強度、反射への寄与、地平線のシャープさ。 |
| ギズモ表示 / ライトアイコン / カメラアイコン | SceneView上の補助アイコン表示切替。 |

### 13-3. 物理設定

| 項目 | 説明 |
| --- | --- |
| 重力 | Vector3（既定Y負方向）。 |
| 固定更新時間 | `fixedTimeStep`（0.001〜0.1秒）。Jolt Physicsの固定Step幅。 |
| 衝突ステップ | `collisionStepCount`（1〜8）。 |
| デバッグ表示チェック群 | 当たり判定の形 / 接触点・法線 / Ray・ShapeCast / 速度・角速度 / 力・場の向き / 影響範囲・流体領域 / ばね・Joint接続 / 選択中だけ表示、の8種類をSceneView上に描画するか個別に切り替える。 |
| ベクトル表示倍率 | デバッグ矢印の長さ倍率（0.01〜10）。 |
| Layer Collision Matrix | Default / Player / Enemy / Ground / Projectile / Trigger / UI / Ignore Raycast の8レイヤー同士が衝突するかを対称行列で設定する。 |

### 13-4. モデル / マテリアル

レガシーPreview用の簡易マテリアル設定（ライティングON/OFF、マテリアル色）。

### 13-5. 操作ツール

| 項目 | 説明 |
| --- | --- |
| ローカル座標 | Gizmoをワールド/ローカルどちらの軸で動かすか。 |
| スナップ | ONでGizmo操作を指定値単位に丸める。 |
| スナップ値 | X/Y/Zそれぞれのスナップ単位。 |
| 移動 / 回転 / 拡縮 / 統合 | Gizmoの操作モードを切り替えるラジオボタン（`activeEditorTool`）。 |
| Scene操作ヘルプ | SceneView操作方法のヘルプ表示切替。 |

### 13-6. シーンカメラ操作

移動速度、回転感度、ホイール速度、中ボタン移動速度、Shift倍率、をDragFloatで調整する。操作方法ヒント（右ドラッグ=回転、中ドラッグ=平行移動、ホイール=前後、WASD=視点基準移動、Q/E=上下、Shift=高速）も表示される。

### 13-7. Input Actions（`.inputactions` Asset選択時）

選択中Assetが `.inputactions` の場合のみ表示される専用エディタ。Action一覧をロードし、GUIで編集・保存できる（詳細はC++ Script APIドキュメントの Input Action 節を参照）。

### 13-8. Legacy Preview（選択種別による切替）

`selectedSceneObject` の値によって「モデル プレビュー」「スプライト プレビュー」「平行光源」「デバッグ カメラ」のいずれかを表示する古いプレビュー機構。UV Transform（UVスケール/回転/移動）もモデルプレビュー時のみ表示される。

### 保存

環境設定・物理設定・操作ツール設定はProject Settings Fileへ保存される想定だが、本調査では具体的な保存先File（例: `ProjectSettings/*.cg2` 系）までは全項目突き合わせていない。**未確認**。Scene固有の値（`EditorPhysicsSettings` はSceneが保持する `GetPhysicsSettings()` 経由）はScene保存に含まれる可能性が高いが、これも実機で保存→再読込して値が保持されるか確認することを推奨する。

### 確認手順

1. 「編集 > 設定を開く」または「選択解除」。
2. 物理設定の「重力」をY=-20等に変更してPlay。
3. 落下速度が変わることを確認する。
4. Scene保存→再読込し、値が保持されているか確認する。
