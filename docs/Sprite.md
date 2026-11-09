# スプライトの使い方

`SpriteCommon` は全スプライトで共通の描画設定、`Sprite` は1枚ごとの設定と描画バッファ、`TextureManager` は共有する画像とSRVを管理します。既存の数学ファイルを利用し、座標計算を `main.cpp` へ追加していません。

## 現在のシーンで確認する

起動後に **F2** を押すと、3枚のスプライトの表示を切り替えられます。初期状態では非表示です。編集画面が重なる場合は **F1** で編集画面を閉じてから確認します。ImGuiの入力欄を操作している間はF2の処理を行いません。

| 表示例 | 設定 |
|---|---|
| 1枚目 | `uvChecker.png` 全体を256×256で表示 |
| 2枚目 | 同じ画像の右上256×256を切り抜き、192×192で表示。中央アンカー、0.25ラジアン回転、左右・上下反転、色とアルファの変更 |
| 3枚目 | `monsterBall.png` を240×120で表示 |

設定の実例は `project/Application/GameScene.cpp` の `InitializeSprites` にあります。1枚目と2枚目は同じテクスチャハンドル・画像リソース・SRVを使います。花火の `hosi.png` も同じ `TextureManager` で管理するため、このシーンが読み込む画像は合計3種類です。

この変更ではビルド、シェーダーコンパイル、アプリの実行、テスト実行を行っていません。F2での実際の表示確認は未実施です。

## 初期化から描画まで

`TextureManager` と `SpriteCommon` は、スプライトより長く生存させます。同じDirectX基盤を渡し、テクスチャのロードと設定変更はメインスレッドから行います。パス文字列はUTF-8、相対パスの基準は作業ディレクトリです。現在のプロジェクトは `project` を作業ディレクトリとして使用します。

```cpp
// アプリケーションで1組用意する。
// 現在はSRVの0番をImGui、4番をパーティクルに使用している。
TextureManager textureManager;
textureManager.Initialize(directXCommon, 5, DirectXCommon::kMaxSRVCount - 5);
SpriteCommon spriteCommon;
spriteCommon.Initialize(directXCommon);

// SceneまたはGameObjectのメンバとして持つ。
Sprite sprite;
sprite.Initialize(&spriteCommon, &textureManager, "resources/uvChecker.png");
sprite.SetPosition({320.0f, 180.0f});
sprite.SetAnchorPoint({0.5f, 0.5f});
sprite.SetTextureRect({128.0f, 64.0f}, {128.0f, 128.0f});
sprite.SetSize({256.0f, 256.0f});
sprite.SetRotation(0.25f);
sprite.SetColor({1.0f, 0.8f, 0.8f, 0.75f});
sprite.SetIsFlipX(true);
sprite.SetIsFlipY(false);

// Scene / GameObjectのUpdateから呼ぶ。setterの変更を描画バッファへ反映する。
sprite.Update();

// DirectXCommon::PreDrawの後、3D描画を終えてからスプライトをまとめて描く。
spriteCommon.CommonSetting();
sprite.Draw();
```

実際のシーンでは `main → GameScene → Sprite` の順で初期化・更新・描画します。複数枚描く場合は別々の `Sprite` インスタンスを用意し、`CommonSetting` はそのまとまりの直前に一度呼びます。後から描いたスプライトが前面になります。3D描画へ戻る場合は、3D用ルートシグネチャ・PSOなどを設定し直してください。

## 設定項目

| 項目 | setter | getter | 意味 |
|---|---|---|---|
| 座標 | `SetPosition` | `GetPosition` | 左上を原点とする画面上のピクセル座標。アンカーの位置を指定 |
| 回転 | `SetRotation` | `GetRotation` | ラジアン。正の値で時計回り |
| 色 | `SetColor` | `GetColor` | RGBAをテクスチャの色へ乗算。通常は各成分0～1 |
| 表示サイズ | `SetSize` | `GetSize` | 画面上の幅・高さ。初期値は画像の幅・高さ。0なら描画を省略 |
| アンカー | `SetAnchorPoint` | `GetAnchorPoint` | `(0,0)`で左上、`(0.5,0.5)`で中央。回転の中心にもなる |
| 左右反転 | `SetIsFlipX` | `GetIsFlipX` | 切り抜いた範囲の左右を反転。座標とアンカー位置を維持 |
| 上下反転 | `SetIsFlipY` | `GetIsFlipY` | 切り抜いた範囲の上下を反転 |
| 切り抜き始点 | `SetTextureLeftTop` | `GetTextureLeftTop` | 画像上の左上座標。ピクセル単位 |
| 切り抜きサイズ | `SetTextureSize` | `GetTextureSize` | 画像から使う幅・高さ。ピクセル単位 |
| 切り抜き矩形 | `SetTextureRect` | 上記2つのgetter | 始点とサイズを一度に変更 |
| 画像ハンドル | `SetTexture` | `GetTextureHandle` | 同じTextureManagerが返したハンドルを指定 |
| 画像パス | `SetTextureFilePath` | `GetTextureFilePath` | 未読込の画像だけロード。getterは正規化した絶対パスを返す |

画像を変更すると切り抜き範囲は新しい画像全体へ戻ります。表示サイズは維持するため、必要なら `SetSize(sprite.GetTextureSize())` で新しい画像サイズに合わせます。切り抜きの変更も表示サイズを自動変更しません。

切り抜き範囲は画像内に収まる正の幅・高さで指定します。始点とサイズの両方を動かす場合は `SetTextureRect` を使うと、変更途中の範囲外指定を避けられます。不正な値や未初期化での呼び出しは例外になります。指定値へ自動的な倍率・範囲補正は加えません。

## ピクセル座標とUV

画像の幅を `W`、高さを `H`、切り抜き左上を `(x,y)`、幅・高さを `(w,h)` とすると、頂点のUVは次の式です。

```text
左U = x / W          右U = (x + w) / W
上V = y / H          下V = (y + h) / H
```

512×512の画像の右上256×256なら、`SetTextureRect({256,0}, {256,256})` でUは0.5～1、Vは0～0.5になります。左右反転ではUの両端、上下反転ではVの両端を交換します。線形フィルタとミップマップを使用するため、アトラスの境界で隣の領域が混ざる場合は画像側に余白を設けてください。

## 共有とリソースの寿命

`TextureManager::LoadTexture` はパスを正規化して検索し、読み込み済みなら既存ハンドルを返します。初回のみWICで画像を読み込み、ミップマップ生成・GPU転送・SRV作成を行います。PNG/JPEG/BMPなどWIC対応の2D画像が対象です。

`GetTexture(handle)` で画像の幅・高さとリソース、`GetSRVGPUDescriptorHandle(handle)` で3D描画にも渡せるSRV、`GetTextureCount()` で読込済みの画像数を参照できます。SRV数の上限は `Initialize` の `maxTextures` で指定し、予約範囲を超えるロードは例外になります。別の管理者が同じSRV範囲を使わないようにしてください。

転送専用のコマンドリストでロードを行い、完了を待ってから転送元の一時バッファを解放します。初回ロードは同期処理なので、通常はシーンの初期化中にまとめて行います。

各スプライトは1枚分の頂点・インデックス・色・変換行列のバッファを持ちます。現在の `DirectXCommon::PostDraw` が毎フレームGPU完了を待つことを前提に、次の `Update` で同じバッファを更新します。同じフレーム内で1つのSpriteを更新し直して複数枚分として使わず、枚数分のインスタンスを作ってください。将来フレームを並列化するときはバッファもフレームごとに分ける必要があります。

終了時はGPUの利用完了後、`Sprite::Finalize`、`SpriteCommon::Finalize`、`TextureManager::Finalize` の順で解放します。現在の `GameScene::Finalize` は各スプライトを解放します。
