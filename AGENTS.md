## Goal
- **Connect existing component settings → rendering paths** (Bloom/AA/Composite/SSR)
- Scene View / Game View temporal separation
- GPU Culling completion or cleanup

## Current State

### PR #8 Additions (kagami branch, 49 files +4459 -113 lines)
All actively running in EditorRenderManager:

- Depth Pyramid + normal reconstruction (EditorDepthHierarchyManager)
- Frustum Culling + Hi-Z Occlusion Culling (EditorGpuCullingManager)
- Full SSR pipeline: Trace/Resolve/Temporal/Denoise/Composite
- Camera Velocity, Reactive Mask, Disocclusion Mask
- Multi-stage Bloom (4 downsample + 3 upsample)
- 3-pass SMAA (Edge Detection / Blend Weight / Neighborhood Blend)
- Final composite shader with bloom/SSAO/vignette/grain/CA
- Parallax Corrected Cubemap
- 4 C++ managers: GpuCulling, Temporal, PostProcessQuality, DepthHierarchy

### Gaps (settings exist but not connected to new paths)

| Area | Status |
|------|--------|
| **Bloom** | ✅ threshold/softKnee/scatter connected from PostProcess component (just now) |
| **AA mode** | ✅ None/FXAA/SMAA/Temporal exclusive selection (just now) |
| **Final composite** | ❌ Exposure/saturation/contrast/vignette/grain/CA/AO intensity hardcoded in `EditorRenderManager.cpp:1892-1904` |
| **SMAA params** | ❌ Threshold/corner rounding hardcoded in `EditorPostProcessQualityManager.cpp` |
| **Temporal params** | ❌ Sharpness/blending hardcoded in `EditorTemporalRenderingManager.cpp` |
| **Scene/Game View** | ❌ Temporal uses only Scene View camera matrices for all viewports |
| **GPU Culling** | ❌ Reads back to CPU, doesn't use `ExecuteIndirect` |
| **SSR design** | ❌ `AGENTS.md` says SSR deleted, but PR #8 re-adds it |

### Camera Component (recently added)
Data-layer complete (FOV/near/far/projection/DOF/motionBlur/exposure) and wired into game view projection. DOF/motion blur shader paths not implemented.

## Key Decisions
- **Prefer connecting existing settings** over adding new components or debug UI
- Keep `aaMode` int32 (0=None, 1=FXAA, 2=SMAA, 3=Temporal) as single authority; old bools `smaaEnabled`/`taaEnabled` are fallback-only in save/load converter
- SSR stays for now; design doc needs updating if kept
- New Bloom path is the primary path; old Bloom bypassed when quality bloom succeeds

## Next Steps (priority order)
1. **Final composite params**: Add exposure/saturation/contrast/vignette/grain/CA/AO fields to PostProcess component; pass as root constants
2. **SMAA/Temporal params**: Expose threshold/cornerRounding/sharpness/blendRatio in PostProcess UI
3. **AA mode 0 passthrough**: Currently uses FXAA PSO with disabled params; add dedicated copy PSO if performance matters
4. **PostProcess Runtime fields**: Add bloomPrefilter/passCount/aaQuality params for low-end configs

## Build
Pre-existing `/WX` errors from VS 2022 v17.14 + Win SDK 10.0.26100.0 (C4820 padding, C5045 Spectre). Not caused by our changes.

## 日本語コメントの階層ルール

コメントは処理の階層が見えるように、大見出し・中見出し・小コメントの3段階で記述する。

```cpp
//========================================
// 描画処理
//========================================
```

- 大見出しは`=`を使い、クラス内の主要な処理群、初期化処理、更新処理、描画処理、終了処理、AI処理、デバッグ処理などを分ける。
- 「初期化」「更新」だけで済ませず、「描画リソース初期化処理」「敵ステート更新処理」のように対象と役割を明記する。

```cpp
//------------------------------
// Shadow Map描画
//------------------------------
```

- 中見出しは`-`を使い、大見出し内を変数初期化、Transform初期化、入力処理、物理更新、Player描画などの具体的な処理単位へ分ける。
- 関数内の各Passや段階を大見出しで並べず、所属する主要処理の下へ中見出しとして置く。

```cpp
const float fixedDeltaTime = 1.0f / 60.0f; // 物理演算を安定させる固定更新間隔（秒）
```

- 小コメント・行末コメントは、変数の用途、数値の意味、特殊条件、処理理由、所有権、失敗時の影響を補足する。
- `hp = 100; // hpに100を代入`のように、コードをそのまま日本語へ置き換えただけのコメントは書かない。
- 入力、内部での変換、出力、次に利用する処理、採用理由、制約のうち、コードだけでは分からない内容を優先する。
- 同じ説明をファイル先頭、関数先頭、行末へ重複させない。
- コメント整理だけを行う場合は、コードの順序と動作を変更しない。
- 新規コードにも同じ形式を使用する。

## ドキュメント同期ルール

- 新機能を追加した場合、既存機能のアルゴリズム、処理フロー、実行順、CPU/GPUの役割、所有関係、性能特性、制約、弱点を変更した場合は、実装と同じ変更内で `docs/engine-internals.md` と `docs/ReadMe.md` を更新する。
- `docs/engine-internals.md` には、対象機能について「何をする機能か / 採用方式 / 処理 / 採用理由 / 他候補 / 不採用理由 / 制約・弱点 / 改善候補」を記載する。深掘り対象では、CPU側とGPU側の役割、主要クラス、負荷が増える場所も記載する。
- `docs/ReadMe.md` には、更新基準日、文書案内、現行監査値、主要更新履歴のうち影響する項目を反映する。詳細説明を重複させず、`engine-internals.md` の該当章へ案内する。
- 実装から一意に決まらない設計理由は「推定」、実行・描画・通信などを確認していない内容は「未検証」と明記する。Build成功だけで実機確認済みとは書かない。
- ソース行番号を根拠として記載している箇所は、対象ファイルの行数が変わった場合に参照先を更新する。
- コメント修正や空白整理だけで動作・設計が変わらない場合は、説明本文の更新は不要。ただし根拠行番号がずれた場合は参照先を更新する。

## 外部認識・オンライン連携（新規モジュール）

音声認識 / 画像認識 / Cloudflare オンライン / FeelKit Haptics を 4 つの独立モジュールとして追加。
詳細は `docs/engine-internals.md`。

- `Source/Engine/External` 共通状態・Error・Console ログ
- `Source/Engine/Speech` SAPI 実装（Keyword / Dictation）。Whisper / ONNX は未実装で Unavailable
- `Source/Engine/Vision` Media Foundation 取り込み + 内蔵（色 / 動き）+ ONNX（検出 / 分類 / 顔）。Landmark / HeadPose は未対応
- `Source/Engine/Online` WinHTTP 非同期 + Leaderboard / PlayerData / CloudSave / 再送 Queue
- `Source/Engine/Haptics` FeelKit Backend + Clip Asset + Audio / Physics 連携
- `Tools/CloudflareWorker` Worker 参照実装（D1 / KV / R2、サーバー側検証つき）

Component は `SpeechRecognizer` / `CameraInput` / `ImageRecognizer` を追加し、`HapticSource` を拡張。
Scene 保存は既存の列位置を変えず `*Extension` 行で追記する。
Script API は `EditorScriptApi.h` 末尾へ追加（`kEditorScriptApiVersion` 13 → 14）。

**設計の約束**: Backend が使えない機能は勝手に別処理へ置き換えず `Unavailable` を返す。
Device 未接続でもゲームロジックは止めない。

## ファイル・関数の規模の基準

行数は分割を検討するための目安とし、最終判断は責務の数、処理段階、依存関係、可読性、変更影響範囲に基づいて行う。
Serialization、登録表、規則的な変換処理は、長くても一体性を優先する場合がある。

### 調査を始める目安

| 対象 | 行数 | 扱い |
| --- | --- | --- |
| ファイル | 〜1,500 | そのままでよい |
| ファイル | 1,500〜3,000 | 構造を確認する |
| ファイル | 3,000〜5,000 | 分割候補として検討する |
| ファイル | 5,000〜10,000 | 優先的に構造を確認する |
| ファイル | 10,000 以上 | 原則として分割対象 |
| 関数 | 〜100 | そのままでよい |
| 関数 | 100〜300 | 機械的に触らない。細分化しすぎると追いにくくなる |
| 関数 | 300〜500 | 必要なものだけ |
| 関数 | 500〜1,000 | 分割候補として検討する |
| 関数 | 1,000 以上 | 分割対象 |

### 行数以外に見る兆候

分割すべき側:

- 処理段階が 3 つ以上混在する
- ネストが深い
- 大量の状態を書き換える
- 同じ条件分岐が何度も現れる
- Error 処理と本処理が入り混じる
- 一部分だけを単体で説明・テストできる
- コメントの大見出しが何個も必要になる
- 変数の有効範囲が数百行続く
- 修正時に関係ない処理まで読む必要がある

一体性を優先してよい側:

- 分岐密度が低く、上から順に読むだけで済む
- ネストが浅い
- 同じ形の文が続く（登録表、Field 既定値、Serialization）

### 判断の優先順

1. 責務が複数あるか
2. 処理段階を名前付き関数として切り出せるか
3. 状態や引数を不自然に大量受け渡しせずに分割できるか
4. 関数の長さ
5. ファイル全体の長さ

### 分割の手順

切り出しでは計算本体へ手を入れない。関数の先頭で抽出前と同じ識別子の参照別名を作り、
本体をそのまま移す。移した後、抽出前後で文の集合が一致することを機械的に確認する
（順序の入れ替えと、境界の `return` / 別名 / 文脈への書き出しだけが差分であるべき）。
