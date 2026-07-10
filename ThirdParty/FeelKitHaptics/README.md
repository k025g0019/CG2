# FeelKit

`FeelKit` は、ゲームの演出とフィードバックをまとめる C++ ライブラリです。

主な目的は次の 2 つです。

- 直接呼べる演出関数を揃える
- 素材やデータから演出を自動生成する

## 収録しているもの

- `FeelKitHaptics`
  - XInput ベースの振動ライブラリ
  - 音名ごとの振動登録
  - ワンショット振動
- `FeelKit`
  - 音、振動、画面演出、エフェクト、生成系、デバッグ系の API 群

## ファイル入口

### FeelKit 本体

```cpp
#include "FeelKit.h"
```

### 振動専用

```cpp
#include "FeelKitHaptics.h"
```

## ディレクトリ構成

```text
FeelKit.h
FeelKitHaptics.h
core/
audio/
haptics/
screen/
effects/
generation/
debug/
```

## 最小例

```cpp
#include "FeelKit.h"
#include "FeelKitHaptics.h"

FeelKitHaptics haptics;

void InitializeGame() {
	haptics.initialize();

	FeelKit::Init();
	FeelKit::SetHapticsBackend(&haptics);
}

void UpdateGame() {
	FeelKit::Update(1.0f / 60.0f);
}

void OnHit() {
	FeelKit::PlaySE("hit.wav", 1.0f);
	FeelKit::Vibrate(0.8f, 0.12f);
	FeelKit::Shake(4.0f, 0.15f);
	FeelKit::HitStop(0.05f);
}

void FinalizeGame() {
	FeelKit::Shutdown();
	haptics.shutdown();
}
```

## API の考え方

`FeelKit` の API は大きく 3 種類あります。

### 1. 直接実行系

ゲームコードからそのまま呼ぶ関数です。

- `PlaySE()`
- `Vibrate()`
- `Shake()`
- `Flash()`
- `PlayEffect()`
- `HitStop()`

### 2. 変換系

素材や入力データを解析して、演出に変換する関数です。

- `PlayHapticFromAudio()`
- `PlayShakeFromAudio()`
- `PlayFlashFromAudio()`
- `CreateParticlesFromAudio()`
- `CreateParticlesFromImage()`
- `CreateCollisionFromImage()`
- `GenerateImpactFeedback()`
- `GenerateExplosionFeedback()`

### 3. 管理・デバッグ系

状態保持、寿命管理、可視化補助のための関数です。

- `Update()`
- `Draw()`
- `Timeline()`
- `PlaySequence()`
- `ShowEffectDebugger()`
- `ShowHapticTimeline()`

## カテゴリ概要

### Audio

- SE / BGM / Voice / Spatial Sound callback
- WAV 解析
- 音から振動、揺れ、フラッシュ、粒子への変換

### Haptics

- 通常振動
- 音名から振動
- `FeelKitHaptics` backend 連携
- HD Rumble / DualSense / Trigger callback

### Screen

- Shake
- Flash
- Zoom
- HitStop
- SlowMotion

### Effects

- 直接 effect 再生
- effect 名登録
- decal / trail / afterimage / emitter
- LOD 切り替え
- screen effect 追加

### Generation

- 衝突から演出生成
- 音から粒子生成
- 画像から粒子 / collision / break
- GIF 取り込み
- テキスト effect
- アニメーション名から haptics / footsteps

### Debug

- timeline 表示
- effect debugger
- AI 感覚表示
- occlusion 係数更新

## 現状の注意点

- 一部の生成系は provider 依存です
  - `SetImageDataProvider()`
  - `SetTextureDataProvider()`
  - `SetGifDataProvider()`
- `GenerateHapticsFromAnimation()` と `GenerateFootsteps()` は registry 優先ですが、未登録時は名前ヒューリスティックを使います
- `ShowEffectDebugger()` などのデバッグ系は、描画 UI ではなく callback / debug log 前提です
- `FeelKitHaptics` は振動専用です。画面演出や effect は `FeelKit` 側を使います

## callback / provider の接続

### 例: SE callback

```cpp
FeelKit::SetSoundEffectCallback(
	[](const char* path, float volume) {
		soundSystem.PlaySe(path, volume);
	});
```

### 例: effect draw callback

```cpp
FeelKit::SetEffectDrawCallback(
	[](const FeelKit::EffectDrawInfo& drawInfo) {
		// drawInfo.textureHandle などを使って描画
	});
```

### 例: 画像 provider

```cpp
FeelKit::SetImageDataProvider(
	[](const char* imagePath, FeelKit::ImageData& outImageData) -> bool {
		// 実画像ロードとピクセル情報設定
		return true;
	});
```

## ドキュメント

人間が読む用の HTML ドキュメントは次を開いてください。

- `C:\kogakuin\ziko\FeelKitHaptics\FeelKitHaptics\docs\index.html`
- `C:\kogakuin\ziko\FeelKitHaptics\FeelKitHaptics\docs\api-reference.html`

HTML 側は、関数検索、左ナビ、見出しジャンプ、コードコピー、外部リンクに対応しています。

`API_REFERENCE.md` を更新した場合は、次で HTML を再生成します。

```powershell
python docs\build_docs.py
```

## 詳細

すべての公開 API は次を参照してください。

- `C:\kogakuin\ziko\FeelKitHaptics\FeelKitHaptics\API_REFERENCE.md`
