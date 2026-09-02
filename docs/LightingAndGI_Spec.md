# CG2 ライティング / GI 仕様

このファイルは、光まわり(直接光・影・間接光・空気中の散乱)の**実装仕様**をまとめたもの。
使用者向けの説明は `docs/component-documentation-detail-seed.md` にあり、こちらは
「どのパスが何を計算し、どのデータがどこを通るか」を書く。

---

## 1. パス構成

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

## 2. 直接光

### 2.1 拡散反射は Lambert

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

### 2.2 影

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

## 3. 間接光 (Light Probe GI)

DDGI(Dynamic Diffuse Global Illumination)準拠。

### 3.1 データ

| 内容 | 形式 | 置き場所 |
| --- | --- | --- |
| 放射照度 | SH9 (L2, RGB 27 係数を float4 × 9 で保持) | `StructuredBuffer<float4>` t20 |
| 可視性 | 八面体 16×16 の距離モーメント(平均, 2乗平均) | `Texture2D<float2>` t21 |
| グリッド定義 | `LightProbeGridData` (64 byte) | b2 (`EmissiveLightArray` 末尾) |

Probe の総数上限は 4096。Descriptor は SRV Heap の **57-62 番**を使う。

### 3.2 Bake

1 フレームにつき 8 Probe。ラウンドロビンで焼き続けるので、光源が動いても追従する。

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

前回値とは `時間平滑`(ヒステリシス)で補間する。ただしグリッドを作り直した直後の
一巡だけは補間せず上書きし、初期値が残らないようにする。

### 3.3 マルチバウンス

キャプチャの Pixel Shader が**前回焼いた Probe を間接光として読み戻す**。
そのため焼き直すたびにバウンスが 1 段ずつ増え、時間をかけて収束する。
1 巡目は直接光のみ、2 巡目で 1 バウンス、と進む。

### 3.4 実行時の参照

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

### 3.5 従来の環境光との関係

```
indirect = Probe が有効な場所 ? probeIrradiance
                              : skyIrradiance * ambientShadowFactor
```

Probe が無い(または範囲外の)場所は従来どおり空 + Sun 遮蔽近似へ戻る。
`ambientShadowFactor` は Sun の Shadow Map を 1 タップ読んで
`lerp(0.15, 1.0, visibility)` する**大雑把な近似**で、Probe が使える場所では出番がない。

---

## 4. Sun Portal

窓を簡易的な Area Light として扱う軽量機能。フル GI ではない。

- 光の向きは Portal の外向き法線の逆で**固定**(放射状の計算はしない)
- 遮蔽は「**Sun → Portal 自身**」だけを既存の Cascaded Shadow で見る。
  「Sun → 対象ピクセル」の遮蔽は無関係なので使わない
  (対象点が壁の影でも、窓に日が当たっていれば光る)
- 「Portal → 対象ピクセル」の遮蔽は**判定しない**。隣室への漏れは `到達距離`で調整する

---

## 5. Volumetric Light Shaft (Sun Beams)

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

## 6. Root Signature と Descriptor の予算

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

## 7. 既知の制限

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

---

## 8. 関連ファイル

| 種別 | パス |
| --- | --- |
| 共有ライトデータ | `Assets/Shaders/Common/SceneLightData.hlsli` |
| 影サンプリング | `Assets/Shaders/Shadow/ShadowSampling.hlsli` |
| GI 数学 | `Assets/Shaders/GI/ProbeCommon.hlsli` |
| GI 実行時参照 | `Assets/Shaders/GI/ProbeSampling.hlsli` |
| GI Bake | `Assets/Shaders/GI/ProbeCapture.VS/PS.hlsl`, `ProbeShProjection.CS.hlsl`, `ProbeVisibility.CS.hlsl` |
| 光の筋 | `Assets/Shaders/PostProcess/VolumetricLightShaft.PS.hlsl` |
| GI 管理 | `Source/Engine/Renderer/EditorLightProbeManager.h/.cpp` |
| 定数バッファ定義 | `Source/Engine/Core/EditorCommonTypes.h` |
