# CG2Engine ドキュメント案内

## 概要

更新基準: 2026-09-29

このFolderは、CG2Engineの利用手順、内部設計、Component、C++ Script APIを6文書に集約する。機能ごとに文書を増やさず、利用者向け内容は`user-guide.md`、実装者向け内容は`engine-internals.md`の章として追加する。

Engine配布、Launcher、Version固定、Migration、環境診断は[user-guide.md](user-guide.md)を参照する。

| 文書 | 対象 |
| --- | --- |
| [user-guide.md](user-guide.md) | Project作成、Editor操作、Window、実践手順、問題対処、配布、共同制作、外部機能の利用方法 |
| [engine-internals.md](engine-internals.md) | 現行機能、所有関係、Runtime、描画、保存、Asset、Lighting、共同制作、外部機能の内部契約、**設計判断と技術選択** |
| [component-reference.md](component-reference.md) | 全287 Component、Inspector Field、既定値、依存、Runtime契約 |
| [script-api-reference.md](script-api-reference.md) | Native Script lifecycle、415 Runtime API、Wrapper、26 Template、Field API |
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

### 実装変更時の文書更新ルール

新機能の追加、アルゴリズムや処理フローの変更、実行順、CPU/GPUの役割、所有関係、性能特性、制約、弱点の変更を行った場合は、実装と同じ変更内で次を更新する。

- `engine-internals.md`: 機能、採用方式、処理、採用理由、他候補、不採用理由、制約・弱点、改善候補を更新する。必要に応じてCPU/GPUの役割、主要クラス、負荷が増える場所も記載する。
- `ReadMe.md`: 更新基準日、文書案内、現行監査値、主要更新履歴のうち影響する項目を更新し、詳細は`engine-internals.md`の該当章へ案内する。
- 各処理の説明には、根拠となるソースを **`Source/.../File.cpp:開始行-終了行`** の形式で記載する。単なるファイル名だけで終わらせない。
- 公開API、所有権、保存データ構造を説明する場合は`.h`、実際の処理順や分岐を説明する場合は`.cpp`を示す。両方が根拠になる場合は両方を併記する。
- 行番号と一緒に`Class::Function`または型名も記載する。行番号がずれた場合でもSymbol検索で追跡できるようにする。
- 行番号の直後に「その付近で何をしているか」を1〜3文で記載する。関連ファイルを列挙するだけでは説明完了としない。
- 複数段の処理は「設定収集 → CPU側準備 → GPU Command発行 → Shader側評価」のように、追う順番と各段の実装位置を分けて示す。
- ソースの`file:line`を根拠にしている箇所は、対象ファイルの行数が変わった場合に参照先も更新する。新機能追加時は本文だけでなく`engine-internals.md`冒頭の「主要実装参照索引」も更新する。
- 読者がプログラミング経験者であることを前提にしない。最初に「何を実現する機能か」を日常語で説明し、その後に専門用語、処理順、コード上の根拠を段階的に示す。
- 初出の専門用語は、略語を展開するだけで終わらせず「何であるか」「何のために必要か」「このEngineではどこで使うか」を説明する。例としてDLLなら、動的Library、生成、Load、Export、関数呼出し、解放までをつなげる。
- 各主要機能には、必要な前提知識、入力、出力、処理主体、処理タイミング、失敗時の動作を記載する。「Managerが処理する」の一文だけで説明完了としない。
- 上位の説明から下位実装へ進めるよう、「身近な例え → Engine内の役割 → 処理フロー → 用語とデータ → 実装位置 → 制約」の順を基本形とする。例えは理解補助に限定し、実装事実と混同しない。
- `API`、`ABI`、`Handle`、`Pointer`、`Thread`、`Process`、`Buffer`、`Resource`等を説明なしで並べない。初心者向けの定義へ戻れる章を案内する。
- `Tag`、`Layer`、`Pipeline`等のように複数の意味がある語は、何を指すかを分ける。UIに項目があるだけで保存・Runtime・Script APIまで実装済みと断定せず、入力から結果までの接続を確認する。

実装参照の記載例:

```text
公開契約: Source/Engine/Editor/EditorPhysicsManager.h:18-245（EditorPhysicsManager）
固定更新: Source/Engine/Editor/EditorPhysicsManager.cpp:527-607（EditorPhysicsManager::Update）
```

参照範囲は説明対象を追える最小範囲にする。巨大な`.cpp`全体や`1-5000`のような範囲は根拠として扱わない。

推奨する記載単位は次のとおり。

```text
実装位置: Source/.../File.cpp:100-140（Class::Function）
この付近の処理: 設定値からPass一覧を組み立て、各Passの行列と出力先を決める。
次に追う場所: Source/.../File.cpp:300-340（実際のDraw Command発行）
```

実装から断定できない理由は「推定」、Build・Play・描画・通信などを確認していない結果は「未検証」と明記する。コメントや空白だけの変更では説明本文の更新は不要だが、根拠行番号がずれた場合は参照先を更新する。

### 設計判断を説明するとき

`engine-internals.md`の「設計判断と技術選択」章が、主要システムごとに
「方式 / 処理 / 採用理由 / 他候補 / 不採用理由 / 弱点 / 改善候補」を1組でまとめている。
他章が「何をどう実装しているか」を述べるのに対し、この章は**その方式を選んだ判断**を正とする。

| 節 | 範囲 |
| --- | --- |
| R-1〜R-11 | GameObject/Component、Scene/Serialization、メインループ、Renderer、Lighting、Shadow、Physics、Asset、GPU最適化、Profiler、共同編集 |
| F-1〜F-8（基礎編） | 座標系と行列規約、Depth BufferとZファイト、色空間とHDR、DirectX 12の使い方、半透明とブレンド、Shaderの扱い、CPU/GPU同期、メモリと所有権 |
| F-9〜F-22（基礎編II） | 描画パイプラインの段、頂点レイアウト、Constant Bufferと256byte境界、Heap Type、PSO、Rasterizerと面の向き、ブレンドとDepth Write、サンプリングとMipmap、法線の変換、接空間、PBRのBRDF、SwapChainとPresent、MSAAを使わない理由、**やっていないことの一覧** |
| R-13〜R-26 | AO、SSR、GI、Post Process順序、水面、破壊、Animation、Camera/Culling、Editor/Runtime分離、Undo/Snapshot、Script API、Multithreading、SunPortal、最適化 |
| R-28〜R-37 | SSRのRay Marching詳細、ライト数上限と選別、Frustum判定形状、AssetのUnload、Undo/Redoの方式とメモリ、AnimationのBlend/State、Snapshot、LOD、Material、Prefab Override |
| R-38 | 未検証として残っている細目（4件） |
| G-1〜G-10（3Dの原理） | 座標変換の全段、同次座標とw、透視補正補間、ラスタライズと2×2クアッド、深度精度の分布とEarly-Z、テクスチャとMip LOD、逆転置行列の数学的導出、接空間の数学、GPUのSIMDと分岐発散、Draw Callのコスト |
| G-11〜G-24（方式の仕組みと比較） | Forward/Deferred/Forward+、影の6方式、影のフィルタ6方式、AOの4方式、反射の5方式、GIの6方式、AAの5方式、半透明の6方式、Tone Mapping、Bloom、水面、Skinning、Culling、Physics |
| G-25〜G-26（API の違い） | OpenGL / D3D11 / D3D12 / Vulkan の仕組みの違い、規約の違い（座標系・深度範囲・行列・テクスチャ原点・巻き方向）と移植で壊れる箇所 |
| G-27〜G-36（設計方式の比較） | Component、Scene直列化、共同編集の同期、Scriptホスト、Profiler計測、GPU破片、Editor分離、Asset管理、Mipmap生成 + 全方式の一覧表 |
| G-37〜G-40（3D基礎補足） | ベクトル・内積・外積、Rayと交差判定、浮動小数点誤差と座標スケール、時間刻みと積分 |
| P-1〜P-13（プログラミング・C++基礎） | 値・参照・Pointer、RAIIと所有権、Copy/Move、計算量とContainer、継承とComposition、CacheとAllocation、Thread同期、CPU/GPU Fence、API/ABI、Error処理、Test・Profiler、設計原則、AI模擬面接用の質問表 |
| G-41〜G-50（3D追加深掘り） | Mesh/Topology、Clipping、重心座標、SAT/GJK/EPA/CCD、空間分割、力と慣性、FK/IK、Sampling、色とAlpha、ScreenからWorld Ray |
| P-14〜P-25（C++・実務追加深掘り） | Compile/Link、Template、Alignment、未定義動作、世代Handle、Serialization、TCP/UDP、State/Event、Real-time性能、Security、Build構成、面接回答の組み立て |
| Q-1〜Q-8（段階別質問集） | C++、データ構造、3D数学、Rendering/GPU、Physics、Engine設計、Thread/Network、Debug/Performanceの模擬面接問題 |
| G-51〜G-56（3D/GPU追補） | Texture Format/BC圧縮、Compute Thread Group、LOD、Camera/FOV、CPU/GPU Particle、Noiseと手続き生成 |
| P-26〜P-33（Computer Science追補） | BFS/DFS/Dijkstra/A*、CPU Cache/Branch/SIMD、Virtual Memory、C++ Memory Model、Encoding/Endianness、Module依存、要件とTrade-off、Code Review |
| U-1〜U-17（理解の深さを示す説明訓練） | 30秒/2分の回答構成、座標変換、描画Pipeline、PBR、Shadow、Temporal、Physics、所有権、Serialization、Thread、TCP、Profiler、規模限界、改善設計、弱点の説明、AI採点基準、実践課題 |

| H-1〜H-4（外部依存とファイル形式） | 使っている外部ライブラリ15件と選定理由、**同梱しているが未使用のもの**、扱うファイル形式と各形式の利点、OS/プラットフォームAPI |
| R-39〜R-50 | 共同編集プロトコル完全版（Tailscale / JSON over TCP / Heartbeat / **Lock機構** / ProjectId検証）、数学ライブラリ（自作・SIMD無し）、Audio、Input、Effect/VFX、Navigation・AI、外部認識4モジュール、UI/Text、Build配布、Spot Shadow詳細、その他Manager、未検証項目 |
| M-1〜M-12（行列と回転の数学） | 各基本行列の中身、行列の各行が意味するもの、TRS合成の順序と間違えたときの症状、**回転の表現（オイラー角/クォータニオン/行列）とジンバルロック**、逆行列の実装方式とアフィン特化の余地、View行列の導出、射影行列の各要素の導出、Viewport行列、Transform階層の合成、**回転の補間（Lerp/Slerp/Nlerp）**、死んだコード |

**G章の役割**: R章が「このエンジンの判断」、F章が「実装値」に対し、G章は**原理**と
**採らなかった方式が実際にどう動くのか**を扱う。「なぜAを選んだか」を説明するにはBとCの仕組みを
知っている必要があるため。G-36に全方式の対応表がある。

**P章の役割**: 特定Engineの機能説明だけでは答えられない、C++、Memory、Container、計算量、Thread、API/ABI、Error処理、Test、設計原則を扱う。AIを模擬面接官として使う場合は、R章のEngine固有質問とG/M/P章の一般原理を交互に質問させる。

**U章の役割**: 用語を知っているだけでなく、原理からData Flowを導出し、失敗条件、計測、規模限界、改善案まで説明できる状態を作る。模擬面接ではU-16の4段階基準で採点し、段階3の「設計理解」を目標にする。

### AI模擬面接での使い方

AIへ本書と`engine-internals.md`を渡す場合は、次の規則で質問させる。

1. 一度に1問だけ出し、最初は機能と採用方式を質問する。
2. 回答後に、処理フロー、採用理由、他候補、不採用理由、弱点、改善案を順に深掘りする。
3. Engine固有の回答には一般原理を、一般論だけの回答にはCG2Engineの主要Class・CPU/GPUの役割・負荷箇所を追加質問する。
4. 「推定」「未検証」は正解として断定せず、回答者へ根拠と確認方法を質問する。
5. 文書の表現を暗記しているかではなく、自分の言葉でTrade-offを説明できるかを評価する。
6. 古い章と新しい章が矛盾する場合は、更新基準が新しい章と現行Sourceを優先する。

**H章で特に注意する点**: `PhysX`（剛体用途）と`imgui-node-editor`は**同梱されているが未使用**。
剛体はJolt、破壊はNvBlast、浮力等は自作の3系統（R-7、H-2）。
`meshoptimizer`は頂点の重複排除にのみ使い、LOD簡略化（`meshopt_simplify`）は使っていない（H-1、R-35）。

**R-11は初版でLock機構とプロトコルの実体を落としていた。** 完全版はR-39を正とする。

**F-17の法線バグは2026-09-29に修正済み。** World行列をそのまま法線へ掛けていたため
非一様スケールでライティングが誤っていた（最大77.9度のずれを数値で確認）。
同じ誤りが7つのVertex Shaderにあり、`Assets/Shaders/Common/NormalTransform.hlsli` の
余因子方式へ統一した。回転のみ・一様スケールでは結果が一致するので**既存シーンの見た目は変わらない**。

**M章で判明した設計上の弱点**: 回転を全面的に**オイラー角（Vector3）**で保持しており、
クォータニオンはJoltとの境界だけで使って毎フレームEulerへ戻している（M-4）。
ジンバルロックがあり、Quat→Euler変換は一意でない。回転の補間手段がオイラー角の線形補間しかないため、
**アニメーションBlendで大角度差のときに遠回りし、真上真下で破綻する可能性がある**（M-10）。
UnityとUnrealは内部クォータニオン・表示のみオイラー角にしてこれを回避している。
実際に症状が出るかの確認はM-12の未検証項目。

R-12（初版13件）とR-27（10件）の未検証項目は**すべて検証してR-6・R-28〜R-37へ反映済み**。

記載規則が2つある。

- 方式・処理・弱点は**ソースで確認した事実のみ**を書き、確認箇所を`file:line`で示す。
- 実装から一意に決まらない「採用理由」は**（推定）**と明記する。設計者本人の判断と食い違う場合はこの章を書き換える。

**特に注意する実測値**: `Assets/Shaders`配下の自前Shaderは797本あるが、Sourceから参照されているのは**79本**。
残りはパイプラインに繋がっていない。`Compute/TiledLightCulling.CS.hlsl`、`AO/SSAO.PS.hlsl`、`Bake/`配下などが該当する。
Shaderの存在を機能の根拠にしてはいけない（F-6）。

**名前と実体が食い違っている箇所**（R-13）: `ssaoPipelineState`の実体は`AO/GTAO.PS.hlsl`、
`ssaoBlurPipelineState`の実体は`Shadow/ContactShadow.PS.hlsl`（中身はAOの深度考慮バイラテラルフィルタ）。

**F-22に「使っていない機能」の一覧**がある。Reverse-Z、MSAA、異方性フィルタ、法線の逆転置行列、
頂点Tangent、Geometry Shader、ハードウェア比較サンプラ、Shader Variant、PSO Cache、
Tearing許可フラグ、トリプルバッファ、Tiled Light Culling、汎用Mesh LOD、Material Asset、
Assetの参照カウント、非同期ロード、描画/物理の並列化。**無いことを把握しているのと、知らないのは別。**

**既知の不具合**（F-17）: 法線変換に逆転置行列を使っていないため、
**非一様スケールを掛けたオブジェクトのライティングが誤る**。回転と一様スケールのみなら正しい。

### 現行仕様の読み方

`README.md`には初期実装時点の評価・未対応表が履歴として残っている。現在の分野横断状態、コードの所有関係、処理順は`engine-internals.md`を正とする。個別Field/APIはComponent・Scriptリファレンスを参照し、矛盾する古い評価行は現行仕様章を優先する。

## 2026-09-30 更新: Window・入力・DirectX12 基盤をクラス化

Win32 / DirectInput / DirectX12 の基盤をグローバルから 3 つのクラスへ移した。
Debug / Development / Release の3構成すべてで0警告0エラー、
`Tests/RunNativeSmokeTests.ps1` 3件成功、`Tools/CheckSourceHygiene.ps1` 違反0。

### 追加したクラス

| クラス | 行数 | メンバ変数 | 所有するもの |
| --- | ---: | ---: | --- |
| `WinApp` | 76 / 348 | 4 | HWND、HINSTANCE、Window Class 登録状態、終了コード |
| `Input` | 110 / 223 | 7 | DirectInput 本体、Keyboard / Mouse Device、今フレームと前フレームの状態 |
| `DirectXCommon` | 179 / 565 | 18 | Device、Command 3種、SwapChain、Back Buffer、Descriptor Heap 3本、Fence、Timestamp |

`Source/Engine/Core/ApplicationWindow.h/.cpp`（自由関数とグローバル定数の集合）は
`WinApp` へ置き換えて削除した。

### グローバルから各クラスのメンバ変数へ移したもの

```
g_windowHandle                              -> WinApp::windowHandle_
g_directInput / g_keyboardDevice / g_mouseDevice
g_key[256] / g_preKey[256]
g_mouseState / g_preMouseState              -> Input のメンバ
g_device / g_commandQueue / g_commandAllocator / g_commandList
g_swapChain / g_swapChainDesc / g_swapChainResources[2] / g_rtvHandles[2]
g_rtvDescriptorHeap / g_srvDescriptorHeap / g_dsvDescriptorHeap
g_fence / g_fenceValue / g_fenceEvent
g_renderTimestampQueryHeap / g_renderTimestampReadback / g_renderTimestampFrequency
                                            -> DirectXCommon のメンバ
```

実体は `std::unique_ptr` で動的に確保し、`Initialize` で `make_unique`、
`Finalize` で `Finalize()` → `reset()` する。生成は WinApp → Input → DirectXCommon、
破棄は逆順（SwapChain が Window を参照しているため）。

### 毎フレームの描画の前処理と後処理

`Draw()` の中に散っていた Command のリセット、Back Buffer の状態遷移、
Close / Execute / Present / Fence 待ちを `DirectXCommon` の関数へ移した。

| 関数 | 中身 |
| --- | --- |
| `BeginFrame(pso)` | Command Allocator と List の巻き戻し。巻き戻す順番と失敗判定を含む |
| `BeginBackBufferPass()` | PRESENT -> RENDER_TARGET |
| `EndBackBufferPass()` | RENDER_TARGET -> PRESENT |
| `SubmitCommandList(log)` | Close + ExecuteCommandLists |
| `EndFrame(vsync, log)` | Present + Fence 待ち |

### Release の排除

`->Release()` を **83箇所 → 18箇所** へ減らした。DirectX12 オブジェクトは
基盤・Render Target・IBL・LUT・Texture・頂点 Buffer をすべて ComPtr へ移した。
ComPtr は484箇所、`.Get()` は951箇所。

残る18箇所とその理由:

| 箇所 | 件数 | 残した理由 |
| --- | ---: | --- |
| `EditorSceneObjectManager` の per-Object Resource | 12 | `usesSharedObjectBuffers` / `usesSharedCustomMesh` が true のとき共有 Pool からの借り物で、**所有権が条件で変わる**。ComPtr へ移すと参照カウントの意味が変わるため、実機で確認できるまで保留 |
| XAudio2 | 2 | DirectX12 ではない |
| Media Foundation | 2 | 同上 |
| SAPI | 2 | 同上 |

### 途中で見つけて直した問題

- **Descriptor Heap の二重解放**: `EditorPlatformManager::Finalize` が
  `srvDescriptorHeap` / `dsvDescriptorHeap` / `rtvDescriptorHeap` を `Release()` していた。
  所有権が `DirectXCommon` へ移ったため二重解放になる。削除した。
- **未使用の重複コード 79行**: `Initialize` 内の `waitForGpu` / `resizeRenderTargets`
  ラムダは一度も呼ばれておらず、`EditorSharedState::ResizeRenderTargets` と重複していた。
  Back Buffer の所有者が変わった今は二重解放の原因にもなるため削除した。

### 課題の達成条件に対する現状

| 条件 | 変更前 | 変更後 |
| --- | :-: | :-: |
| Input のクラス化（クラス自作 / メンバ関数 / 初期化と毎フレームの分離 / メンバ変数 / 動的管理 / bool 関数） | △3 ❌2 ✅1 | ✅6 |
| DirectX12 基盤のクラス化 | ❌ | ✅ |
| 毎フレームの描画の前処理と後処理を関数に | ❌ | ✅ |
| Release を排除し ComPtr へ | ❌ 83箇所 | △ 残18（うち DirectX12 は12、条件付き所有権のため保留） |
| ComPtr から生ポインタを取り出す | ✅ | ✅ 951箇所 |
| FPS 固定または可変対応 | ✅ | ✅ |
| WindowsAPI のクラス化 | ❌ | ✅ |
| 必要以上の include を書かない | △ | △（`WinApp.h` は4個、`Input.h` は4個。`EditorSharedState.h` の65個は未着手） |
| 通常と静的メンバ関数の使い分け | ✅ | ✅（`WinApp::WindowProc` / `SetStandaloneWindowTitle` を追加） |
| メンバ変数の getter | ✅ | ✅（`GetHwnd` / `GetDevice` / `GetCommandList` など） |
| クラスの定数 | ✅ | ✅（`kClientWidth` / `kKeyCount` / `kBackBufferCount` など11個） |
| 自作クラスのポインタを別クラスの関数へ | ✅ | ✅（`Input::Initialize(HINSTANCE, WinApp*)`、`DirectXCommon::Initialize(WinApp*, ostream&)`） |

**未検証**: Build と静的な突き合わせまで。実機で Window 生成、入力、描画、終了処理を
通していない。詳細は`engine-internals.md`の「Window・入力・DirectX12 基盤のクラス化（R-52）」。

## 2026-09-29 更新: 浮力処理を責務で分割 / 規模の判断基準をAGENTS.mdへ明文化

「行数は分割を検討するための目安とし、最終判断は責務の数、処理段階、依存関係、可読性、
変更影響範囲に基づいて行う」という基準を `AGENTS.md` へ追記し、それに沿って
1,000行超の関数を再評価した。Debug / Development / Release の3構成すべてで0警告0エラー、
`Tests/RunNativeSmokeTests.ps1` 3件成功、`Tools/CheckSourceHygiene.ps1` 違反0。

### 責務で再評価した結果

指標を実測し、行数だけでは判断できない差を確認した。

| 関数 | 行数 | 分岐密度 | 最大ネスト | 最頻出の先頭語 | 判断 |
| --- | ---: | ---: | ---: | --- | --- |
| `EditorRenderManager::Draw` | 4,965 | 7.96/100行 | 8タブ | — | 分割すべき（大見出し26＋中見出し19） |
| `EditorPlatformManager::Initialize` | 4,270 | 4.82/100行 | 7タブ | — | 分割すべき（Error処理が本処理へ混在） |
| `EditorScene::LoadScene` | 3,736 | 19.06/100行 | 8タブ | `component` 44% | 要判断（行種別ディスパッチャ） |
| `EditorScene::SaveScene` | 2,617 | 10.89/100行 | 7タブ | `<<` 79% | 一体性を優先（規則的なSerialization） |
| `EditorScene::CreateComponent` | 1,895 | 2.11/100行 | 3タブ | `component` 93% | **一体性を優先（登録表）** |
| `EditorPhysicsManager::ApplyBuoyancyForces` | 1,459 | 6.17/100行 | **10タブ** | — | 分割すべき（深い行86%） |
| `EditorWaterRailShooterSceneBuilder::Generate` | 1,232 | 3.90/100行 | 3タブ | — | 優先度低（大見出し8で読める） |

`CreateComponent` 1,895行は分岐密度2.11・最大ネスト3タブ・同じ形の文が93%で、
上から順に読むだけの登録表である。基準の「登録表は長くても一体性を優先する」に当てはまるため、
**分割対象から外した**。

### 浮力処理の分割

指標が最も悪かった `ApplyBuoyancyForces`（最大ネスト10タブ、深い行86%）を分割した。

| 関数 | 変更前 | 変更後 | 責務 |
| --- | ---: | ---: | --- |
| `ApplyBuoyancyForces` | 1,459 | **78** | 物体走査、有効性判定、World姿勢解決、2方式への振り分け |
| `ApplyShapeBuoyancyForces` | — | 79 | 局所水面の作成、水没体積の問い合わせ、流体力計算の呼び出し |
| `BuildLocalWaterSurface` | — | 206 | 5x5 ProbeでFFT水面を25点評価し最小二乗Planeを当てる |
| `LocalWaterSurfaceModel::Sample` | — | 116 | 任意位置の水面を双線形補間で返す |
| `ApplyHydrodynamicForces` | — | 897 | 付加質量・静水圧・Heave・回転放射減衰・面ごとの抗力・Slamming・造波抵抗 |
| `ApplyGridBuoyancyForces` | — | 265 | 体積取得へ対応しないShape用の安全策 |

`EditorPhysicsManager.cpp` に1,000行超の関数は無くなった（最大897行）。

鍵になったのは、面ごとの水深問い合わせを担っていた `[&]` 捕捉ラムダ（局所変数11個を参照）を
`LocalWaterSurfaceModel::Sample()` へ移したこと。ラムダのままでは関数境界を越えられず、
流体力計算を切り出せなかった。あわせて `RuntimeBuoyancySettings` を
`Source/Engine/Editor/EditorPhysicsBuoyancyTypes.h` へ移し、メンバ関数の引数に書けるようにした。

詳細は`engine-internals.md`の「浮力処理の構造（R-51）」を参照。

### 分割が計算を変えていないことの確認方法

切り出しでは計算本体へ手を入れず、関数の先頭で抽出前と同じ識別子の参照別名を作った。
そのうえで抽出前後の文を正規化して集合比較し、差分が境界の`return`・別名・文脈への
書き出しだけであることを機械的に確認した。今回は1,304文→1,308文で、増分4文すべてが
関数境界の追加分であることを突き合わせている。

**未検証**: 実機で浮力挙動を比較していない。Buildと文の集合一致までが確認範囲。

### Build手順の落とし穴（今回判明）

- `Release|x64` が中間ファイルの新旧混在で内部コンパイラError（`C1001` / `LNK1000`）になる。
  `imgui.cpp` が名指しされるが原因はLink時Code生成。HEADでも再現するため既存の問題。
  構成ごとの中間ファイル置き場を消してからBuildすると通る。
- `/t:Rebuild` は `ThirdParty/DirectXTex` の生成済みShader Headerを削除し、
  以降のBuildが `MSB3073 ... コード 9009` で止まる。復旧手順は`engine-internals.md`の
  「Build手順の注意（H-5）」に記載した。**中間ファイルを消すときは`/t:Rebuild`を使わない**。

### 次の対象

1. `EditorRenderManager::Draw` 4,965行 — ステージ2件を切り出して完了。中盤は`Draw`内で定義された`[&]`捕捉ラムダ約12個へ依存しており、浮力と同じくラムダを型へ移す作業が先に必要
2. `EditorPlatformManager::Initialize` 4,270行 — 局所変数423個、末尾232行でグローバルへ引き渡す構造
3. `EditorPhysicsManager::ApplyHydrodynamicForces` 897行 — 18段の力計算。共有する中間量が多く文脈構造体の設計が必要
4. `EditorInspectorPanel.cpp` 10,513行 / `EditorScriptManager.cpp` 9,151行 — 巨大関数は持たないファイル側の分割

## 2026-09-29 更新: 初心者向け前提、3D Pipeline、Component生成、Tag実態

`engine-internals.md`冒頭へ、プログラミング未経験者が後続章を読むための前提を追加した。一般的なGPU Graphics Pipelineと1FrameのRender Pass列を分け、CPU準備、Input Assembler、Vertex Shader、Rasterizer、Pixel Shader、Output Merger、Post Process、Presentまでを現行実装へ対応付けた。

Componentについては、現行が派生Class方式ではなく`EditorComponent`のfat structと`EditorComponentType`によるtype tag方式であることを明記した。Inspector追加、既定値生成、GameObject配列への格納、Managerによる型収集、Scene保存・読込、新Component追加時に必要な接続箇所を一続きで説明している。

Inspector上部のGameObject Tag / Layer / Staticは現在仮UIであり、GameObjectごとの保存先やRuntime接続を持たない。動作中のComponent Type、Physics Layer、Damage Tag、Surface Tagとは別物として説明し、`user-guide.md`にも利用上の注意を追加した。

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
| **法線変換の誤り** | World行列をそのまま法線へ掛けており、**非一様スケールでライティングが誤っていた**（数値検証で最大77.9度のずれ）。同じ誤りが7つのVertex Shaderにあった。`Assets/Shaders/Common/NormalTransform.hlsli` を新設し、余因子方式（せん断を含む任意の可逆行列で正しい）へ統一。回転のみ・一様スケールでは結果が一致するため既存シーンの見た目は不変 | F-17、G-7 |
| **ThirdPartyの絶対パス** | `CG2.vcxproj`の`Release|x64`が`C:\kogakuin\LE1\CG2\ThirdParty\...`を参照しており、**他マシンでクローンするとリンクできなかった**。`PhysicsSdk.props`が既に`$(MSBuildThisFileDirectory)`で構成別に正しく解決していたため、重複していた絶対パス1行を削除してprops側へ一本化 | R-47 |
| **死んだコードの削除** | 数学ライブラリに`Novice`（学習用2Dライブラリ）へのコメントアウト参照が3ファイル11箇所。`Vector&Matrix.cpp`は241行のうち104行（43%）が死んだコメントだった。計146行を削除（関数は全て残存を確認） | M-11 |
| **コメント階層の整備** | `EditorRenderManager::Draw()` が大見出し0・中見出し55で、**最大1,352行が見出し無し**だった。AGENTS.mdの3段階規約に合わせ、26の描画パスを大見出しへ昇格し、パス内部へ中見出し13件を追加。**見出し無しの最大区間 1,352行 → 421行**。あわせて今回追加した`EditorHrCheck.h/.cpp`と`NormalTransform.hlsli`、抽出した9 Pass関数も3段階へ統一 | AGENTS.md「日本語コメントの階層ルール」 |
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

- `EditorRenderManager::Draw()`は4,965行（機能判定と影Atlas配置の2ステージを切り出した後）。残る最大の塊は「Scene rendering to HDR RT」で、`Draw()`内定義の`[&]`捕捉ラムダ約12個に依存するため、ラムダを型へ移す作業が先に必要
- `Draw()`内に関数内`static`が15個ある（フレーム跨ぎの隠れ状態）。分割を進めるならメンバ変数へ移すのが前提になる
- `EditorScene::LoadScene()` 3,736行 / `SaveScene()` 2,617行。`CreateComponent()` 1,895行は登録表なので分割対象から外した（2026-09-29の再評価）
- `EditorComponent`（1,820行・1,596 Field）の既定値が宣言から約2,000行離れた別ファイルにある。宣言側のMember初期化子へ移すと1箇所管理になるが、Scene既定値が変わらないことの実機確認が必要
- `EditorSharedState.h`の可変グローバル。Window / 入力 / DirectX12 基盤の分は WinApp / Input / DirectXCommon のメンバへ移したが、Render Target や PSO はまだグローバルにある
- `Engine/Input`（Input Action の InputSystem）がトップレベルにあり`Source/Engine/*`の配置規則と揃っていない。DirectInput 側は`Source/Engine/Core/Input.h`へ移した
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
