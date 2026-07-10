# FeelKit API Reference

このファイルは `FeelKit.h` から使う公開 API のリファレンスです。

各項目は、関数を引くために次の形式で書いています。

- 宣言
- 概略
- 引数
- 戻り値
- 解説
- サンプルコード
- 実装状態

## 目次

- 共通ライフサイクル
- Audio
- Haptics
- Screen
- Effects
- Generation
- Debug / Query
- FeelKitHaptics

# 共通ライフサイクル

## Init

### 宣言

```cpp
void FeelKit::Init();
```

### 概略

`FeelKit` の内部状態を初期化します。

### 引数

なし

### 戻り値

なし

### 解説

active effect、timeline、waveform、emitter、text effect、fragment、screen effect などの内部 state を初期値に戻します。
ゲーム開始時や、演出管理を明示的にリセットしたいタイミングで呼びます。

### サンプルコード

```cpp
FeelKit::Init();
```

### 実装状態

本実装。内部 state を初期化します。

## Update

### 宣言

```cpp
void FeelKit::Update(float deltaTime);
```

### 概略

`FeelKit` の内部演出を更新します。

### 引数

- `deltaTime`
  - 前フレームからの経過秒数
  - 負数を渡した場合は `0.0f` として扱います

### 戻り値

なし

### 解説

active effect、画面揺れ、フラッシュ、ズーム、ヒットストップ、スローモーション、timeline、text effect、fragment、screen effect、trail point の寿命を進めます。
ゲームの更新処理内で毎フレーム呼びます。

### サンプルコード

```cpp
FeelKit::Update(1.0f / 60.0f);
```

### 実装状態

本実装。複数カテゴリの寿命管理を行います。

## Draw

### 宣言

```cpp
void FeelKit::Draw();
```

### 概略

描画 callback へ、現在の演出状態を流します。

### 引数

なし

### 戻り値

なし

### 解説

`SetEffectDrawCallback()`、`SetTextDrawCallback()`、`SetFragmentDrawCallback()`、`SetScreenEffectDrawCallback()` を登録している場合に、それぞれの描画情報をゲーム側へ渡します。
実際の描画はゲーム側で行います。

### サンプルコード

```cpp
FeelKit::Draw();
```

### 実装状態

本実装。描画自体ではなく callback 通知を行います。

## Shutdown

### 宣言

```cpp
void FeelKit::Shutdown();
```

### 概略

`FeelKit` の内部 state を破棄します。

### 引数

なし

### 戻り値

なし

### 解説

active effect、decal、timeline、waveform、emitter、LOD effect、text effect、collision、fragment、screen effect、trail point をクリアします。

### サンプルコード

```cpp
FeelKit::Shutdown();
```

### 実装状態

本実装。

# Audio

## SetSoundEffectCallback

### 宣言

```cpp
void FeelKit::SetSoundEffectCallback(const SoundEffectCallback& callback);
```

### 概略

SE 再生用 callback を登録します。

### 引数

- `???`
  - `const char* path`
  - `float volume`

### 戻り値

なし

### 解説

`PlaySE()` と `PlaySoundEffect()` は、この callback を通してゲーム側の音声システムを呼びます。
`FeelKit` 自体は音声 backend を持ちません。

### サンプルコード

```cpp
FeelKit::SetSoundEffectCallback(
	[](const char* path, float volume) {
		soundSystem.PlaySe(path, volume);
	});
```

### 実装状態

本実装。callback を内部 state に保存します。

## PlaySE

### 宣言

```cpp
void FeelKit::PlaySE(const char* path);
void FeelKit::PlaySE(const char* path, float volume);
```

### 概略

効果音を再生します。

### 引数

- `path`
  - 再生する音声ファイル名または識別子
- `volume`
  - 音量
  - `0.0f` から `1.0f` の範囲に補正されます

### 戻り値

なし

### 解説

`SetSoundEffectCallback()` で登録した callback を呼びます。
callback が未登録の場合は何もしません。

### サンプルコード

```cpp
FeelKit::PlaySE("hit.wav", 1.0f);
```

### 実装状態

本実装。callback 経由でゲーム側の音再生に接続します。

## PlayBGM

### 宣言

```cpp
void FeelKit::PlayBGM(const char* path, bool loop);
```

### 概略

BGM 再生を要求します。

### 引数

- `path`
  - BGM のファイル名または識別子
- `loop`
  - `true` の場合はループ再生を要求します

### 戻り値

なし

### 解説

`SetBgmCallback()` で登録した callback を呼びます。

### サンプルコード

```cpp
FeelKit::PlayBGM("stage01.ogg", true);
```

### 実装状態

本実装。音声再生そのものはゲーム側 callback に任せます。

## StopBGM

### 宣言

```cpp
void FeelKit::StopBGM();
```

### 概略

BGM 停止を要求します。

### 引数

なし

### 戻り値

なし

### 解説

`SetStopBgmCallback()` で登録した callback を呼びます。

### サンプルコード

```cpp
FeelKit::StopBGM();
```

### 実装状態

本実装。

## SetMasterVolume

### 宣言

```cpp
void FeelKit::SetMasterVolume(float volume);
float FeelKit::GetMasterVolume();
```

### 概略

`FeelKit` 側の音量係数を設定、取得します。

### 引数

- `volume`
  - 音量係数
  - `0.0f` から `1.0f` に補正されます

### 戻り値

- `GetMasterVolume()`
  - 現在の音量係数

### 解説

`PlaySE()`、`PlaySpatialSound()`、`PlayVoice()` などの音量に反映されます。

### サンプルコード

```cpp
FeelKit::SetMasterVolume(0.8f);
float volume = FeelKit::GetMasterVolume();
```

### 実装状態

本実装。

## AnalyzeAudioFile

### 宣言

```cpp
AudioAnalysisResult FeelKit::AnalyzeAudioFile(const char* soundPath);
```

### 概略

WAV ファイルを解析し、音量や簡易周波数特徴を取得します。

### 引数

- `soundPath`
  - 解析する WAV ファイルパス

### 戻り値

- `AudioAnalysisResult`
  - `isValid`
  - `durationSeconds`
  - `averageAmplitude`
  - `peakAmplitude`
  - `lowBandEnergy`
  - `highBandEnergy`
  - `sampleRate`

### 解説

音から振動、揺れ、フラッシュなどを生成するための基礎解析です。
読めないファイルや未対応形式では `isValid == false` になります。

### サンプルコード

```cpp
AudioAnalysisResult result = FeelKit::AnalyzeAudioFile("explosion.wav");

if (result.isValid) {
	FeelKit::Shake(result.lowBandEnergy * 5.0f, 0.2f);
}
```

### 実装状態

本実装。WAV 解析に対応しています。

## AnalyzeAudioFeatures

### 宣言

```cpp
AudioFeatureData FeelKit::AnalyzeAudioFeatures(const char* soundPath);
```

### 概略

音声を共通 feature として解析します。

### 引数

- `soundPath`
  - 解析する音声ファイルパス

### 戻り値

- `AudioFeatureData`
  - 音から各種演出へ変換するための共通特徴量

### 解説

音から振動、画面揺れ、フラッシュ、粒子、スローモーションへ変換するための共通解析結果を返します。

### サンプルコード

```cpp
AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures("boss.wav");
ShakeParams shake = FeelKit::MakeShakeFromAudio(feature);
FeelKit::Shake(shake.power, shake.seconds);
```

### 実装状態

本実装。音解析共有層として使います。

## MakeHapticsFromAudio

### 宣言

```cpp
HapticParams FeelKit::MakeHapticsFromAudio(const AudioFeatureData& feature);
```

### 概略

音の特徴量から振動パラメータを作ります。

### 引数

- `feature`
  - `AnalyzeAudioFeatures()` の解析結果

### 戻り値

- `HapticParams`
  - 振動強度と再生時間

### 解説

音解析と振動実行を分離するための変換関数です。
実際の振動は `Vibrate()` または `PlayHapticFromAudio()` で行います。

### サンプルコード

```cpp
AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures("hit.wav");
HapticParams haptic = FeelKit::MakeHapticsFromAudio(feature);
FeelKit::Vibrate(haptic.power, haptic.seconds);
```

### 実装状態

本実装。

## MakeShakeFromAudio

### 宣言

```cpp
ShakeParams FeelKit::MakeShakeFromAudio(const AudioFeatureData& feature);
```

### 概略

音の特徴量から画面揺れパラメータを作ります。

### 引数

- `feature`
  - 音の共通特徴量

### 戻り値

- `ShakeParams`
  - 揺れ強度と時間

### 解説

低音や音量の強さをもとに揺れを決めます。
高精度な音楽解析ではなく、ゲーム演出向けの軽量変換です。

### サンプルコード

```cpp
AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures("earthquake.wav");
ShakeParams shake = FeelKit::MakeShakeFromAudio(feature);
FeelKit::Shake(shake.power, shake.seconds);
```

### 実装状態

本実装。

## MakeFlashFromAudio

### 宣言

```cpp
FlashParams FeelKit::MakeFlashFromAudio(const AudioFeatureData& feature);
```

### 概略

音の特徴量からフラッシュパラメータを作ります。

### 引数

- `feature`
  - 音の共通特徴量

### 戻り値

- `FlashParams`
  - 色、強度、時間

### 解説

ピークや振幅を元に、画面フラッシュの強さを作ります。

### サンプルコード

```cpp
AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures("lightning.wav");
FlashParams flash = FeelKit::MakeFlashFromAudio(feature);
FeelKit::Flash(flash.color, flash.seconds, flash.strength);
```

### 実装状態

本実装。

## MakeParticlesFromAudio

### 宣言

```cpp
ParticleParams FeelKit::MakeParticlesFromAudio(const AudioFeatureData& feature);
```

### 概略

音の特徴量から粒子生成パラメータを作ります。

### 引数

- `feature`
  - 音の共通特徴量

### 戻り値

- `ParticleParams`
  - 粒子数、サイズ、速度、寿命などの変換結果

### 解説

低音、高音、音量を使って emitter 用の値を作ります。

### サンプルコード

```cpp
AudioFeatureData feature = FeelKit::AnalyzeAudioFeatures("explosion.wav");
ParticleParams particle = FeelKit::MakeParticlesFromAudio(feature);
```

### 実装状態

本実装。`CreateParticlesFromAudio()` の変換元です。

## PlayHapticFromAudio

### 宣言

```cpp
void FeelKit::PlayHapticFromAudio(const char* soundPath);
```

### 概略

音声ファイルを解析して振動を再生します。

### 引数

- `soundPath`
  - 解析する WAV ファイルパス

### 戻り値

なし

### 解説

音を解析し、振動パラメータに変換して `Vibrate()` を呼びます。
音から振動を作る代表的な API です。

### サンプルコード

```cpp
FeelKit::PlayHapticFromAudio("explosion.wav");
```

### 実装状態

本実装。

## PlayShakeFromAudio

### 宣言

```cpp
void FeelKit::PlayShakeFromAudio(const char* soundPath);
```

### 概略

音声ファイルを解析して画面揺れを生成します。

### 引数

- `soundPath`
  - 解析する WAV ファイルパス

### 戻り値

なし

### 解説

音の feature をフレーム単位に分割し、timeline へ揺れイベントを登録します。
ゲーム側は `Update()` を呼ぶことで timeline を進めます。

### サンプルコード

```cpp
FeelKit::PlayShakeFromAudio("earthquake.wav");
```

### 実装状態

本実装。簡易な timeline 生成です。

## PlayFlashFromAudio

### 宣言

```cpp
void FeelKit::PlayFlashFromAudio(const char* soundPath, unsigned int color = 0xFFFFFFFFu);
```

### 概略

音声ファイルを解析して画面フラッシュを生成します。

### 引数

- `soundPath`
  - 解析する WAV ファイルパス
- `color`
  - フラッシュ色

### 戻り値

なし

### 解説

音量やピークをもとに、timeline 上へフラッシュイベントを登録します。

### サンプルコード

```cpp
FeelKit::PlayFlashFromAudio("lightning.wav", 0xFFFFFFFFu);
```

### 実装状態

本実装。簡易な timeline 生成です。

## CreateParticlesFromAudio

### 宣言

```cpp
void FeelKit::CreateParticlesFromAudio(const char* soundPath);
```

### 概略

音声ファイルを解析して粒子 emitter を生成します。

### 引数

- `soundPath`
  - 解析する WAV ファイルパス

### 戻り値

なし

### 解説

音量、低音、高音の特徴から emitter の発生量、粒子サイズ、速度、色を決めます。
現状は音の時系列に完全追従する粒子列ではなく、解析結果から emitter を作る設計です。

### サンプルコード

```cpp
FeelKit::CreateParticlesFromAudio("explosion.wav");
```

### 実装状態

実用最低限。ワンショット emitter 生成です。

# Haptics

## SetHapticsBackend

### 宣言

```cpp
void FeelKit::SetHapticsBackend(FeelKitHaptics* haptics);
```

### 概略

`FeelKitHaptics` を振動 backend として登録します。

### 引数

- `haptics`
  - 初期化済みの `FeelKitHaptics` インスタンス

### 戻り値

なし

### 解説

`PlayGamepadVibration()` が `FeelKitHaptics::playOneShot()` を使えるようになります。

### サンプルコード

```cpp
FeelKitHaptics haptics;
haptics.initialize();
FeelKit::SetHapticsBackend(&haptics);
```

### 実装状態

本実装。

## Vibrate

### 宣言

```cpp
void FeelKit::Vibrate(float power, float seconds);
```

### 概略

指定した強さと時間で振動を再生します。

### 引数

- `power`
  - 振動強度
  - `0.0f` から `1.0f` に補正されます
- `seconds`
  - 振動時間

### 戻り値

なし

### 解説

`SetVibrationCallback()` に callback がある場合はそれを呼びます。
`FeelKitHaptics` backend を使う場合は `PlayGamepadVibration()` を使います。

### サンプルコード

```cpp
FeelKit::Vibrate(0.8f, 0.12f);
```

### 実装状態

本実装。

## PlayGamepadVibration

### 宣言

```cpp
bool FeelKit::PlayGamepadVibration(const GamepadVibrationRequest& request);
```

### 概略

左右強度を指定してゲームパッド振動を再生します。

### 引数

- `request.leftStrength`
  - 左モーター強度
- `request.rightStrength`
  - 右モーター強度
- `request.durationMs`
  - 振動時間ミリ秒
- `request.isEnabled`
  - `false` の場合は再生しません

### 戻り値

- `true`
  - backend または callback に再生要求を渡せた
- `false`
  - 無効状態、または backend / callback が無い

### 解説

`SetHapticsBackend()` が登録されていれば `FeelKitHaptics` へ渡します。
未登録の場合は `SetGamepadVibrationCallback()` の callback を呼びます。

### サンプルコード

```cpp
FeelKit::GamepadVibrationRequest request;
request.leftStrength = 0.8f;
request.rightStrength = 0.4f;
request.durationMs = 120;
request.isEnabled = true;

FeelKit::PlayGamepadVibration(request);
```

### 実装状態

本実装。

## PlayHdRumble

### 宣言

```cpp
bool FeelKit::PlayHdRumble(const HdRumbleRequest& request);
```

### 概略

HD Rumble 再生を callback 経由で要求します。

### 引数

- `request.lowFrequency`
  - 低周波
- `request.highFrequency`
  - 高周波
- `request.amplitude`
  - 振幅
- `request.durationMs`
  - 再生時間ミリ秒

### 戻り値

- `true`
  - callback を呼べた
- `false`
  - callback 未登録

### 解説

標準 backend はまだ持たず、ゲーム側 callback に処理を渡します。

### サンプルコード

```cpp
FeelKit::SetHdRumbleCallback(
	[](float low, float high, float amplitude, int ms) {
		hdRumble.Play(low, high, amplitude, ms);
	});
```

### 実装状態

callback 接続実装。

## PlayDualSenseHaptics

### 宣言

```cpp
bool FeelKit::PlayDualSenseHaptics(const DualSenseHapticsRequest& request);
```

### 概略

DualSense ハプティクス再生を callback 経由で要求します。

### 引数

- `request.leftStrength`
  - 左側強度
- `request.rightStrength`
  - 右側強度
- `request.durationMs`
  - 再生時間ミリ秒

### 戻り値

- `true`
  - callback を呼べた
- `false`
  - callback 未登録

### 解説

PC 向け DualSense backend はライブラリ内に固定せず、ゲーム側 callback に任せます。

### サンプルコード

```cpp
FeelKit::PlayDualSenseHaptics({0.7f, 0.7f, 100});
```

### 実装状態

callback 接続実装。

## PlayTriggerFeedback

### 宣言

```cpp
bool FeelKit::PlayTriggerFeedback(const TriggerFeedbackRequest& request);
```

### 概略

左右トリガー抵抗の再生を callback 経由で要求します。

### 引数

- `request.leftResistance`
  - 左トリガー抵抗
- `request.rightResistance`
  - 右トリガー抵抗
- `request.durationMs`
  - 再生時間ミリ秒

### 戻り値

- `true`
  - callback を呼べた
- `false`
  - callback 未登録

### 解説

DualSense adaptive trigger などの実機依存処理はゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::TriggerFeedbackRequest request;
request.leftResistance = 0.5f;
request.rightResistance = 0.2f;
request.durationMs = 150;

FeelKit::PlayTriggerFeedback(request);
```

### 実装状態

callback 接続実装。

# Screen

## Shake

### 宣言

```cpp
void FeelKit::Shake(float power, float seconds);
```

### 概略

画面揺れを開始します。

### 引数

- `power`
  - 揺れ強度
- `seconds`
  - 揺れ時間

### 戻り値

なし

### 解説

`GetScreenState()` で取得できる `shakeOffset` と `shakeRotation` に反映されます。
実際のカメラ適用はゲーム側で行います。

### サンプルコード

```cpp
FeelKit::Shake(5.0f, 0.2f);

FeelKit::ScreenState state = FeelKit::GetScreenState();
camera.x += state.shakeOffset.x;
camera.y += state.shakeOffset.y;
```

### 実装状態

本実装。

## Flash

### 宣言

```cpp
void FeelKit::Flash(unsigned int color, float seconds);
void FeelKit::Flash(unsigned int color, float seconds, float strength);
```

### 概略

画面フラッシュを開始します。

### 引数

- `color`
  - RGBA 色
- `seconds`
  - 表示時間
- `strength`
  - フラッシュ強度
  - `0.0f` から `1.0f` に補正されます

### 戻り値

なし

### 解説

`GetScreenState()` の `flashColor`、`flashAlpha`、`flashStrength` に反映されます。

### サンプルコード

```cpp
FeelKit::Flash(0xFFFFFFFFu, 0.1f, 0.8f);
```

### 実装状態

本実装。

## Zoom

### 宣言

```cpp
void FeelKit::Zoom(float scale, float seconds);
```

### 概略

一定時間ズーム係数を設定します。

### 引数

- `scale`
  - ズーム倍率
- `seconds`
  - 継続時間

### 戻り値

なし

### 解説

`GetScreenState()` の `zoomScale` に反映されます。

### サンプルコード

```cpp
FeelKit::Zoom(1.08f, 0.15f);
```

### 実装状態

本実装。

## HitStop

### 宣言

```cpp
void FeelKit::HitStop(float seconds);
```

### 概略

ヒットストップ状態を開始します。

### 引数

- `seconds`
  - ヒットストップ時間

### 戻り値

なし

### 解説

`GetScreenState().isHitStop` で現在状態を確認します。
ゲーム更新停止そのものはゲーム側で行います。

### サンプルコード

```cpp
FeelKit::HitStop(0.05f);

if (FeelKit::GetScreenState().isHitStop) {
	return;
}
```

### 実装状態

本実装。

## SlowMotion

### 宣言

```cpp
void FeelKit::SlowMotion(float scale, float seconds);
```

### 概略

スローモーション係数を一定時間設定します。

### 引数

- `scale`
  - 時間倍率
  - `0.0f` から `1.0f` に補正されます
- `seconds`
  - 継続時間

### 戻り値

なし

### 解説

`GetScreenState().slowMotionScale` をゲーム側の time scale に反映して使います。

### サンプルコード

```cpp
FeelKit::SlowMotion(0.3f, 0.2f);
```

### 実装状態

本実装。

## GetScreenState

### 宣言

```cpp
ScreenState FeelKit::GetScreenState();
```

### 概略

現在の画面演出状態を取得します。

### 引数

なし

### 戻り値

- `ScreenState`
  - `shakeOffset`
  - `shakeRotation`
  - `zoomScale`
  - `slowMotionScale`
  - `flashColor`
  - `flashAlpha`
  - `flashStrength`
  - `isHitStop`

### 解説

`FeelKit` はカメラや描画 backend を直接操作しません。
取得した状態をゲーム側でカメラ、ポストエフェクト、時間制御に反映します。

### サンプルコード

```cpp
FeelKit::ScreenState state = FeelKit::GetScreenState();
camera.zoom = state.zoomScale;
```

### 実装状態

本実装。

# Effects

## LoadEffect

### 宣言

```cpp
bool FeelKit::LoadEffect(const char* effectName, const EffectDesc& effectDesc);
```

### 概略

名前付き effect を登録します。

### 引数

- `effectName`
  - effect 名
- `effectDesc`
  - texture、layout、frame 数、再生時間などの設定

### 戻り値

- `true`
  - 登録成功
- `false`
  - `effectName` が `nullptr` または空文字

### 解説

登録した effect は `PlayEffect("name", pos)` で再生できます。

### サンプルコード

```cpp
FeelKit::EffectDesc explosion;
explosion.textureHandle = explosionTexture;
explosion.layout = FeelKit::EffectLayout::grid;
explosion.frameWidth = 64;
explosion.frameHeight = 64;
explosion.frameCount = 16;
explosion.columns = 4;
explosion.rows = 4;
explosion.seconds = 0.5f;
explosion.loop = false;

FeelKit::LoadEffect("explosion", explosion);
```

### 実装状態

本実装。

## PlayEffect

### 宣言

```cpp
int FeelKit::PlayEffect(int textureHandle, float x, float y, int frameCount, float seconds, bool loop);
int FeelKit::PlayEffect(int textureHandle, float x, float y, int frameWidth, int frameHeight, int frameCount, float seconds, bool loop);
int FeelKit::PlayEffect(const EffectDesc& effectDesc, Vec2 pos);
int FeelKit::PlayEffect(const EffectDesc& effectDesc, Vec3 pos);
int FeelKit::PlayEffect(const char* effectName, Vec2 pos);
int FeelKit::PlayEffect(const char* effectName, Vec3 pos);
```

### 概略

effect を再生します。

### 引数

- `textureHandle`
  - ゲーム側 texture 管理の handle
- `x`, `y`
  - 2D 座標
- `frameWidth`, `frameHeight`
  - スプライトシート 1 フレームのサイズ
- `frameCount`
  - フレーム数
- `seconds`
  - 再生時間
- `loop`
  - ループ再生するか
- `effectDesc`
  - 詳細設定
- `effectName`
  - `LoadEffect()` で登録した名前
- `pos`
  - 2D または 3D 座標

### 戻り値

- `0` 以上
  - effect ID
- `-1`
  - 名前付き effect が見つからない

### 解説

再生された effect は `Update()` で寿命管理され、`Draw()` 時に `SetEffectDrawCallback()` へ `EffectDrawInfo` が渡されます。

### サンプルコード

```cpp
int effectId = FeelKit::PlayEffect("explosion", FeelKit::Vec2{320.0f, 240.0f});
```

### 実装状態

本実装。

## SetEffectDrawCallback

### 宣言

```cpp
void FeelKit::SetEffectDrawCallback(const EffectDrawCallback& callback);
```

### 概略

effect 描画 callback を登録します。

### 引数

- `???`
  - `EffectDrawInfo` を受け取る関数

### 戻り値

なし

### 解説

`FeelKit` は texture を直接描画しません。
`Draw()` のタイミングで、現在描画すべき frame 情報を callback に渡します。

### サンプルコード

```cpp
FeelKit::SetEffectDrawCallback(
	[](const FeelKit::EffectDrawInfo& info) {
		DrawSprite(info.textureHandle, info.x, info.y, info.sourceX, info.sourceY);
	});
```

### 実装状態

本実装。

## EffectPoolSetMax

### 宣言

```cpp
void FeelKit::EffectPoolSetMax(int maxEffects);
int FeelKit::EffectPoolGetActiveCount();
void FeelKit::EffectPoolKillAll();
void FeelKit::EffectPoolKill(int effectId);
bool FeelKit::EffectPoolIsAlive(int effectId);
```

### 概略

active effect の数と寿命を管理します。

### 引数

- `maxEffects`
  - 同時再生数の上限
- `effectId`
  - `PlayEffect()` が返した ID

### 戻り値

- `EffectPoolGetActiveCount()`
  - 現在の active effect 数
- `EffectPoolIsAlive()`
  - 指定 effect が生存している場合 `true`

### 解説

大量の effect が残り続けることを防ぐための管理 API です。

### サンプルコード

```cpp
FeelKit::EffectPoolSetMax(256);
int count = FeelKit::EffectPoolGetActiveCount();
```

### 実装状態

本実装。

## SpawnDecal

### 宣言

```cpp
int FeelKit::SpawnDecal(const DecalDesc& decalDesc);
```

### 概略

弾痕、血痕、足跡などの decal を内部 pool に追加します。

### 引数

- `decalDesc`
  - decal 名、位置、寿命、最大数など

### 戻り値

- decal ID

### 解説

最大数を超えた場合は古い decal が削除されます。

### サンプルコード

```cpp
FeelKit::DecalDesc decal;
decal.textureName = "bullet_mark";
decal.positionX = 120.0f;
decal.positionY = 64.0f;
decal.lifeSeconds = 10.0f;
decal.maxDecals = 128;

int decalId = FeelKit::SpawnDecal(decal);
```

### 実装状態

実用最低限。描画はゲーム側で行います。

## CreateTrail

### 宣言

```cpp
TrailHandle FeelKit::CreateTrail(const TrailDesc& trailDesc);
void FeelKit::ReleaseTrail(TrailHandle handle);
```

### 概略

軌跡表現用の trail を作成、解放します。

### 引数

- `trailDesc`
  - 寿命、幅、最大点数など
- `handle`
  - 作成された trail handle

### 戻り値

- `CreateTrail()`
  - trail handle
- `ReleaseTrail()`
  - なし

### 解説

trail point の寿命は `Update()` で進みます。
描画用 mesh 生成までは行いません。

### サンプルコード

```cpp
FeelKit::TrailDesc trail;
trail.lifeSeconds = 0.4f;
trail.width = 8.0f;
trail.maxPoints = 32;

FeelKit::TrailHandle handle = FeelKit::CreateTrail(trail);
```

### 実装状態

実用最低限。

## CreateAfterImage

### 宣言

```cpp
AfterImageHandle FeelKit::CreateAfterImage(const AfterImageDesc& afterImageDesc);
void FeelKit::ReleaseAfterImage(AfterImageHandle handle);
```

### 概略

残像管理を作成、解放します。

### 引数

- `afterImageDesc`
  - 残像間隔、寿命、フェード速度など
- `handle`
  - afterimage handle

### 戻り値

- `CreateAfterImage()`
  - afterimage handle

### 解説

アクションゲームのダッシュ残像などに使うための基盤です。
描画内容そのものはゲーム側で扱います。

### サンプルコード

```cpp
FeelKit::AfterImageDesc desc;
desc.intervalSeconds = 0.05f;
desc.lifeSeconds = 0.3f;
desc.fadeSpeed = 1.0f;

FeelKit::AfterImageHandle handle = FeelKit::CreateAfterImage(desc);
```

### 実装状態

実用最低限。

## Emit

### 宣言

```cpp
EmitterHandle FeelKit::Emit(const EmitterDesc& emitterDesc);
void FeelKit::StopEmit(EmitterHandle handle);
```

### 概略

粒子 emitter を作成、停止します。

### 引数

- `emitterDesc`
  - 形状、半径、粒子数、寿命、速度など
- `handle`
  - emitter handle

### 戻り値

- `Emit()`
  - emitter handle

### 解説

円、扇形、球、円柱、箱、mesh surface などの emitter 形状を指定できます。
粒子描画 backend はゲーム側に任せます。

### サンプルコード

```cpp
FeelKit::EmitterDesc emitter;
emitter.shape = FeelKit::EmitterShape::circle;
emitter.radius = 64.0f;
emitter.particleDesc.spawnRate = 30.0f;

FeelKit::EmitterHandle handle = FeelKit::Emit(emitter);
```

### 実装状態

実用最低限。

## PlayEffectLod

### 宣言

```cpp
int FeelKit::PlayEffectLod(const char* effectName, const Vec3& position, float distance);
```

### 概略

距離に応じて LOD 版 effect を選んで再生します。

### 引数

- `effectName`
  - 基本 effect 名
- `position`
  - 再生位置
- `distance`
  - カメラなどからの距離

### 戻り値

- `0` 以上
  - effect ID
- `-1`
  - effect が見つからない

### 解説

`_mid`、`_far` のような名前付き effect があれば優先し、無い場合は基本名へ fallback します。

### サンプルコード

```cpp
FeelKit::PlayEffectLod("explosion", FeelKit::Vec3{0.0f, 0.0f, 30.0f}, 320.0f);
```

### 実装状態

実用最低限。

# Generation

## AnalyzeImpact

### 宣言

```cpp
ImpactAnalysisResult FeelKit::AnalyzeImpact(float velocity, float mass);
ImpactAnalysisResult FeelKit::AnalyzeImpact(Vec3 velocity, float mass);
```

### 概略

速度と質量から衝撃の強さを解析するだけの helper です。

### 引数

- `velocity`
  - 衝突速度
  - `Vec3` 版では長さを速度として扱います
- `mass`
  - 質量

### 戻り値

- `ImpactAnalysisResult`
  - `power`
  - `seconds`

### 解説

この関数は振動、画面揺れ、SE、ヒットストップを実行しません。

衝突から演出を作るための基礎計算だけを行い、`ImpactAnalysisResult` を返します。

実際に演出まで実行したい場合は、次を使います。

- `GenerateImpactHaptics()`
  - 衝突から振動を自動生成して再生
- `GenerateImpactShake()`
  - 衝突から画面揺れを自動生成して再生
- `GenerateImpactFeedback()`
  - 衝突から振動、画面揺れ、ヒットストップ、SE をまとめて実行
- `PlayImpact()`
  - 位置と intensity から統合 impact 演出を実行

### サンプルコード

```cpp
ImpactAnalysisResult impact = FeelKit::AnalyzeImpact(12.0f, 2.0f);

// AnalyzeImpact() 自体は実行しないので、必要な演出へ自分で渡す
FeelKit::Vibrate(impact.power, impact.seconds);
```

### 実装状態

本実装。軽量な運動量ベースです。

## GenerateImpactFeedback

### 宣言

```cpp
ImpactFeedbackResult FeelKit::GenerateImpactFeedback(float velocity, float mass, float angle, MaterialType material = MaterialType::metal);
ImpactFeedbackResult FeelKit::GenerateImpactFeedback(Vec3 velocity, float mass, MaterialType material = MaterialType::metal);
```

### 概略

物理衝突から振動、画面揺れ、ヒットストップ、SE 音量、粒子強度を生成します。

### 引数

- `velocity`
  - 衝突速度
- `mass`
  - 質量
- `angle`
  - 衝突角度
- `material`
  - 材質

### 戻り値

- `ImpactFeedbackResult`
  - `vibrationPower`
  - `shakePower`
  - `hitStopSeconds`
  - `seVolume`
  - `particleIntensity`

### 解説

`MaterialType` と occlusion 係数を加味し、`Vibrate()`、`Shake()`、`HitStop()`、`PlaySE()` を呼びます。

### サンプルコード

```cpp
FeelKit::GenerateImpactFeedback(
	FeelKit::Vec3{12.0f, 0.0f, 0.0f},
	2.0f,
	FeelKit::MaterialType::metal);
```

### 実装状態

本実装。材質と occlusion に対応しています。

## GenerateExplosionFeedback

### 宣言

```cpp
ExplosionFeedbackResult FeelKit::GenerateExplosionFeedback(Vec3 position, float radius, float power, MaterialType material = MaterialType::metal);
```

### 概略

爆発パラメータから振動、揺れ、SE、effect 強度を生成します。

### 引数

- `position`
  - 爆発位置
- `radius`
  - 爆発半径
- `power`
  - 爆発威力
- `material`
  - 爆発対象材質

### 戻り値

- `ExplosionFeedbackResult`
  - `vibrationPower`
  - `shakePower`
  - `seVolume`
  - `effectIntensity`

### 解説

距離、威力、材質、occlusion 係数から演出値を作り、振動、画面揺れ、SE、effect を呼びます。

### サンプルコード

```cpp
FeelKit::GenerateExplosionFeedback(
	FeelKit::Vec3{0.0f, 0.0f, 10.0f},
	20.0f,
	1.0f,
	FeelKit::MaterialType::stone);
```

### 実装状態

本実装。

## GenerateHapticsFromAnimation

### 宣言

```cpp
void FeelKit::GenerateHapticsFromAnimation(const char* animationName);
```

### 概略

アニメーション名から振動を生成します。

### 引数

- `animationName`
  - アニメーション名

### 戻り値

なし

### 解説

`SetAnimationHapticPattern()` で登録された pattern を優先します。
未登録の場合は `run`、`walk`、`jump`、`attack`、`hit` などの名前ヒューリスティックで振動を決めます。

### サンプルコード

```cpp
FeelKit::AnimationHapticPattern pattern;
pattern.power = 0.6f;
pattern.durationSeconds = 0.08f;

FeelKit::SetAnimationHapticPattern("attack_slash", pattern);
FeelKit::GenerateHapticsFromAnimation("attack_slash");
```

### 実装状態

実用最低限。registry 優先、未登録時は名前ヒューリスティックです。

## GenerateFootsteps

### 宣言

```cpp
void FeelKit::GenerateFootsteps(const char* animationName);
```

### 概略

アニメーション名から足音と足元振動を生成します。

### 引数

- `animationName`
  - 歩行、走行などのアニメーション名

### 戻り値

なし

### 解説

登録済み pattern または名前ヒューリスティックを使い、左右足の交互処理を含む footstep 演出を出します。
厳密な接地フレーム解析はゲーム側イベントとの連携が必要です。

### サンプルコード

```cpp
FeelKit::GenerateFootsteps("run_forward");
```

### 実装状態

実用最低限。

## SetImageDataProvider

### 宣言

```cpp
void FeelKit::SetImageDataProvider(ImageDataProvider provider);
void FeelKit::SetTextureDataProvider(TextureDataProvider provider);
void FeelKit::SetGifDataProvider(GifDataProvider provider);
```

### 概略

画像、texture、GIF の実データ取得 provider を登録します。

### 引数

- `provider`
  - ゲーム側または外部ライブラリ側のデータ供給関数

### 戻り値

なし

### 解説

`CreateParticlesFromImage()`、`CreateCollisionFromImage()`、`BreakImage()`、`ImportGif()` が実データを使うための入口です。

### サンプルコード

```cpp
FeelKit::SetImageDataProvider(
	[](const char* path, FeelKit::ImageData& outImage) {
		return LoadImagePixels(path, outImage);
	});
```

### 実装状態

本実装。実データの読み込み自体は provider 側で行います。

## CreateParticlesFromImage

### 宣言

```cpp
void FeelKit::CreateParticlesFromImage(const char* imagePath);
```

### 概略

画像の内容から粒子 emitter を生成します。

### 引数

- `imagePath`
  - 画像ファイルパス

### 戻り値

なし

### 解説

`ImageDataProvider` から得たピクセルを使い、非透明部分や色を元に emitter を作ります。
provider が無い場合の挙動は実装側の fallback に依存します。

### サンプルコード

```cpp
FeelKit::CreateParticlesFromImage("logo.png");
```

### 実装状態

実用最低限。provider 接続時に実画像ベースで動きます。

## CreateCollisionFromImage

### 宣言

```cpp
void FeelKit::CreateCollisionFromImage(const char* imagePath);
```

### 概略

画像の非透明部分から簡易 collision を生成します。

### 引数

- `imagePath`
  - 画像ファイルパス

### 戻り値

なし

### 解説

画像ピクセルから content 領域を抽出し、内部 collision rect に変換します。
生成結果の取得 API はまだ限定的です。

### サンプルコード

```cpp
FeelKit::CreateCollisionFromImage("rock.png");
```

### 実装状態

実用最低限。

## BreakImage

### 宣言

```cpp
void FeelKit::BreakImage(int textureHandle);
```

### 概略

texture を破片データに変換します。

### 引数

- `textureHandle`
  - 破片化する texture handle

### 戻り値

なし

### 解説

`TextureDataProvider` の情報を使い、中心から外側へ飛ぶ破片データを生成します。
破片は `Update()` で移動と寿命が更新され、`Draw()` や callback でゲーム側へ渡されます。

### サンプルコード

```cpp
FeelKit::BreakImage(textureHandle);
```

### 実装状態

実用最低限。provider 連携が前提です。

## ImportGif

### 宣言

```cpp
void FeelKit::ImportGif(const char* gifPath);
```

### 概略

GIF を effect として取り込みます。

### 引数

- `gifPath`
  - GIF ファイルパス

### 戻り値

なし

### 解説

`GifDataProvider` から frame 情報を受け取り、内部で `EffectDesc` として登録します。

### サンプルコード

```cpp
FeelKit::ImportGif("explosion.gif");
FeelKit::PlayEffect("_gif_explosion.gif", FeelKit::Vec2{320.0f, 240.0f});
```

### 実装状態

実用最低限。GIF decode は provider 側です。

## SpawnTextEffect

### 宣言

```cpp
void FeelKit::SpawnTextEffect(const char* text);
```

### 概略

文字列からテキスト演出を生成します。

### 引数

- `text`
  - 表示する文字列

### 戻り値

なし

### 解説

文字数、大文字、記号の比率からサイズ、散らばり、寿命を決めます。
描画は `SetTextDrawCallback()` または query API を使ってゲーム側で行います。

### サンプルコード

```cpp
FeelKit::SpawnTextEffect("CRITICAL!");
```

### 実装状態

実用最低限。描画 backend はゲーム側に任せます。

## PlayImpact

### 宣言

```cpp
void FeelKit::PlayImpact(const Vec3& position, float intensity);
```

### 概略

衝撃演出を一括再生します。

### 引数

- `position`
  - 発生位置
- `intensity`
  - 衝撃強度

### 戻り値

なし

### 解説

振動、画面揺れ、ヒットストップ、SE をまとめて発火します。

### サンプルコード

```cpp
FeelKit::PlayImpact(FeelKit::Vec3{0.0f, 0.0f, 0.0f}, 0.8f);
```

### 実装状態

本実装。

## PlayExplosion

### 宣言

```cpp
void FeelKit::PlayExplosion(const Vec3& position, float radius, float power);
```

### 概略

爆発演出を一括再生します。

### 引数

- `position`
  - 爆発位置
- `radius`
  - 爆発半径
- `power`
  - 爆発威力

### 戻り値

なし

### 解説

内部で `GenerateExplosionFeedback()` を呼び、振動、揺れ、SE、effect を生成します。

### サンプルコード

```cpp
FeelKit::PlayExplosion(FeelKit::Vec3{0.0f, 0.0f, 8.0f}, 20.0f, 1.0f);
```

### 実装状態

本実装。

# Debug / Query

## SetDebugLogCallback

### 宣言

```cpp
void FeelKit::SetDebugLogCallback(DebugLogCallback cb);
```

### 概略

デバッグ出力 callback を登録します。

### 引数

- `cb`
  - 文字列を受け取る callback

### 戻り値

なし

### 解説

`ShowEffectDebugger()`、`ShowHapticTimeline()`、`DrawAISenses()`、`ApplyOcclusion()` などのログ出力に使います。

### サンプルコード

```cpp
FeelKit::SetDebugLogCallback(
	[](const char* message) {
		DebugPrint(message);
	});
```

### 実装状態

本実装。

## ShowEffectDebugger

### 宣言

```cpp
void FeelKit::ShowEffectDebugger();
```

### 概略

現在の effect 関連 state をデバッグ出力します。

### 引数

なし

### 戻り値

なし

### 解説

active effect 数、registered effect 数、shake、flash、hit stop、slow motion、emitter、timeline などの状態を出力します。

### サンプルコード

```cpp
FeelKit::ShowEffectDebugger();
```

### 実装状態

デバッグ用実装。GUI ではなく callback / log 出力です。

## ShowHapticTimeline

### 宣言

```cpp
void FeelKit::ShowHapticTimeline();
```

### 概略

振動 timeline log をデバッグ出力します。

### 引数

なし

### 戻り値

なし

### 解説

音、衝撃、アニメーションなどから生成された haptics 関連 log を確認します。

### サンプルコード

```cpp
FeelKit::ShowHapticTimeline();
```

### 実装状態

デバッグ用実装。

## DrawAISenses

### 宣言

```cpp
void FeelKit::DrawAISenses(const AISenseDebugDesc& debugDesc);
```

### 概略

AI の視界、聴覚、警戒状態を debug log として出力します。

### 引数

- `debugDesc`
  - AI 感覚 debug 用の位置、視界範囲、聴覚範囲、警戒レベル

### 戻り値

なし

### 解説

この関数は実際の線や円を描画しません。

`SetDebugLogCallback()` が登録されていれば callback に流し、未登録なら console へ出力します。

ゲーム側で本当に可視化したい場合は、出力された値を使って renderer 側で視界円や聴覚範囲を描画します。

### サンプルコード

```cpp
AISenseDebugDesc debugDesc = {};
debugDesc.position = {0.0f, 0.0f, 0.0f};
debugDesc.sightRange = 12.0f;
debugDesc.hearingRange = 20.0f;
debugDesc.alertLevel = 2;

FeelKit::DrawAISenses(debugDesc);
```

### 実装状態

本実装。debug log 出力です。

## ApplyOcclusion

### 宣言

```cpp
void FeelKit::ApplyOcclusion(const Vec3& listenerPosition, const Vec3& sourcePosition);
```

### 概略

listener と sound source の距離から occlusion 係数を更新します。

### 引数

- `listenerPosition`
  - 聞き手の位置
- `sourcePosition`
  - 音源位置

### 戻り値

なし

### 解説

距離から `g_occlusionFactor` を更新します。
高度な遮蔽物判定は行いません。

### サンプルコード

```cpp
FeelKit::ApplyOcclusion(playerPosition, explosionPosition);
```

### 実装状態

実用最低限。

## Query API

### 宣言

```cpp
int FeelKit::GetTextEffectCount();
const Internal::TextEffectData* FeelKit::GetTextEffect(int index);
int FeelKit::GetFragmentCount();
const Internal::FragmentData* FeelKit::GetFragment(int index);
int FeelKit::GetScreenEffectCount();
const Internal::ScreenEffectEntry* FeelKit::GetScreenEffect(int index);
int FeelKit::GetTrailCount();
TrailHandle FeelKit::GetTrailHandle(int index);
```

### 概略

内部で管理している演出 state を取得します。

### 引数

- `index`
  - 取得する要素番号

### 戻り値

- count 系
  - 要素数
- pointer 系
  - 成功時は要素 pointer
  - 範囲外は `nullptr`
- `GetTrailHandle()`
  - trail handle

### 解説

描画 callback を使わずに、ゲーム側で状態を直接参照したい場合に使います。
戻り値に `Internal::` 型が含まれるため、公開 API としては今後整理余地があります。

### サンプルコード

```cpp
for (int i = 0; i < FeelKit::GetTextEffectCount(); i++) {
	const auto* text = FeelKit::GetTextEffect(i);

	if (text != nullptr) {
		DrawText(text->text.c_str(), text->posX, text->posY);
	}
}
```

### 実装状態

実用最低限。

# FeelKitHaptics

## initialize

### 宣言

```cpp
bool FeelKitHaptics::initialize(const FeelKitHapticsInitializeDesc& initializeDesc = {});
```

### 概略

振動 backend を初期化します。

### 引数

- `initializeDesc`
  - 優先デバイス、XInput user、worker thread、master scale など

### 戻り値

- `true`
  - 初期化成功
- `false`
  - 初期化失敗

### 解説

`FeelKitHaptics` は振動専用のクラスです。
`FeelKit::SetHapticsBackend()` に渡すことで、`FeelKit` 側の gamepad vibration と接続できます。

### サンプルコード

```cpp
FeelKitHaptics haptics;

if (haptics.initialize()) {
	FeelKit::SetHapticsBackend(&haptics);
}
```

### 実装状態

本実装。

## playOneShot

### 宣言

```cpp
bool FeelKitHaptics::playOneShot(float leftStrength, float rightStrength, int durationMs, bool shouldVibrate = true);
bool FeelKitHaptics::playOneShot(const FeelKitHapticsVibrationDesc& vibrationDesc);
```

### 概略

ゲームパッドを指定強度で一度だけ振動させます。

### 引数

- `leftStrength`
  - 左モーター強度
- `rightStrength`
  - 右モーター強度
- `durationMs`
  - 振動時間ミリ秒
- `shouldVibrate`
  - `false` の場合は再生しません
- `vibrationDesc`
  - 振動設定構造体

### 戻り値

- `true`
  - 再生要求成功
- `false`
  - デバイス未準備、無効状態、または失敗

### 解説

XInput などの実デバイスへ振動を送ります。

### サンプルコード

```cpp
haptics.playOneShot(0.8f, 0.4f, 120, true);
```

### 実装状態

本実装。

## registerSoundVibration

### 宣言

```cpp
bool FeelKitHaptics::registerSoundVibration(const char* soundPath, const FeelKitHapticsVibrationDesc& vibrationDesc);
bool FeelKitHaptics::registerSoundVibration(const wchar_t* soundPath, const FeelKitHapticsVibrationDesc& vibrationDesc);
bool FeelKitHaptics::vibrateSound(const char* soundPath, bool shouldVibrate = true);
bool FeelKitHaptics::vibrateSound(const wchar_t* soundPath, bool shouldVibrate = true);
bool FeelKitHaptics::vibrateSound(const char* soundPath, const FeelKitHapticsVibrationDesc& vibrationDesc);
bool FeelKitHaptics::vibrateSound(const wchar_t* soundPath, const FeelKitHapticsVibrationDesc& vibrationDesc);
void FeelKitHaptics::clearSoundVibrations();
```

### 概略

音名と振動設定を紐づけて再生します。

### 引数

- `soundPath`
  - 音名または音声パス
- `vibrationDesc`
  - 振動設定
- `shouldVibrate`
  - 振動するかどうか

### 戻り値

- `true`
  - 登録または再生要求成功
- `false`
  - 引数不正、デバイス未準備、または失敗

### 解説

SE 再生コードの近くで `vibrateSound()` を呼ぶ用途を想定しています。

### サンプルコード

```cpp
FeelKitHapticsVibrationDesc hit;
hit.leftStrength = 0.8f;
hit.rightStrength = 0.4f;
hit.durationMs = 120;
hit.isEnabled = true;

haptics.registerSoundVibration("hit.wav", hit);

sound.Play("hit.wav");
haptics.vibrateSound("hit.wav", true);
```

### 実装状態

本実装。

# 公開 API 補完リファレンス

この章は、上のカテゴリ別説明でまとめられていた request / callback / 管理 API を、関数単位で引けるように補完したものです。

# 音 callback

## SetBgmCallback

### 宣言

```cpp
void SetBgmCallback(const BgmCallback& callback);
```

### 概略

BGM 再生 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetBgmCallback([](const char* path, bool loop) {
	audio.PlayBgm(path, loop);
});
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:33

## SetStopBgmCallback

### 宣言

```cpp
void SetStopBgmCallback(const StopBgmCallback& callback);
```

### 概略

BGM 停止 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetStopBgmCallback();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:34

## SetBgmSwitchCallback

### 宣言

```cpp
void SetBgmSwitchCallback(const BgmSwitchCallback& callback);
```

### 概略

BGM 切り替え callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetBgmSwitchCallback();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:35

## SetSpatialSoundCallback

### 宣言

```cpp
void SetSpatialSoundCallback(const SpatialSoundCallback& callback);
```

### 概略

3D 音響 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetSpatialSoundCallback();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:36

## SetVoiceCallback

### 宣言

```cpp
void SetVoiceCallback(const VoiceCallback& callback);
```

### 概略

ボイス再生 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetVoiceCallback();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:37

# 音解析 / 再生 request

## LoadWaveform

### 宣言

```cpp
WaveformHandle LoadWaveform(const WaveformDesc& waveformDesc);
```

### 概略

波形データを登録して handle を作ります。

### 引数

なし

### 戻り値

作成または取得した handle を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::LoadWaveform();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:41

## MakeSlowMotionFromAudio

### 宣言

```cpp
SlowMotionParams MakeSlowMotionFromAudio(const AudioFeatureData& feature);
```

### 概略

音声特徴量からスローモーション設定を作ります。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::MakeSlowMotionFromAudio();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:52

## PlaySoundEffect

### 宣言

```cpp
bool PlaySoundEffect(const SoundEffectRequest& request);
```

### 概略

SE 再生 request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SoundEffectRequest request;
request.path = "hit.wav";
request.volume = 1.0f;
FeelKit::PlaySoundEffect(request);
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:54

## PlayBgmSwitch

### 宣言

```cpp
bool PlayBgmSwitch(const BgmSwitchRequest& request);
```

### 概略

BGM 切り替え request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayBgmSwitch();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:55

## PlaySpatialSound

### 宣言

```cpp
bool PlaySpatialSound(const SpatialSoundRequest& request);
```

### 概略

位置つき 3D 音響 request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlaySpatialSound();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:56

## PlayVoice

### 宣言

```cpp
bool PlayVoice(const VoiceRequest& request);
```

### 概略

ボイス再生 request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayVoice();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:57

## GetMasterVolume

### 宣言

```cpp
float GetMasterVolume();
```

### 概略

現在の master volume を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetMasterVolume();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:64

# 音から演出生成

## SpawnEffectFromAudio

### 宣言

```cpp
void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec2 pos);
void SpawnEffectFromAudio(const EffectDesc& desc, const char* soundPath, Vec3 pos);
```

### 概略

音の強さから effect 強度を作って発生させます。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::EffectDesc effectDesc;
FeelKit::SpawnEffectFromAudio(effectDesc, "explosion.wav", FeelKit::Vec3{0.0f, 0.0f, 0.0f});
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:69, audio/FeelKitAudio.h:70

## CreateLightFromAudio

### 宣言

```cpp
void CreateLightFromAudio(const char* soundPath);
```

### 概略

音からライト点滅演出を作ります。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::CreateLightFromAudio();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:71

## CreateCameraMotionFromAudio

### 宣言

```cpp
void CreateCameraMotionFromAudio(const char* soundPath);
```

### 概略

音からカメラ揺れやカメラ動作を作ります。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::CreateCameraMotionFromAudio();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:72

## CreateSlowMotionFromAudio

### 宣言

```cpp
void CreateSlowMotionFromAudio(const char* soundPath);
```

### 概略

音からスローモーション演出を作ります。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::CreateSlowMotionFromAudio();
```

### 実装状態

本実装。宣言場所: audio/FeelKitAudio.h:73

# 振動 callback

## SetVibrationCallback

### 宣言

```cpp
void SetVibrationCallback(const VibrationCallback& callback);
```

### 概略

単純振動 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetVibrationCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:36

## SetVibrationPatternCallback

### 宣言

```cpp
void SetVibrationPatternCallback(const VibrationPatternCallback& callback);
```

### 概略

名前付き振動 pattern callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetVibrationPatternCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:37

## SetVibrationFromSoundCallback

### 宣言

```cpp
void SetVibrationFromSoundCallback(const VibrationFromSoundCallback& callback);
```

### 概略

音由来振動 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetVibrationFromSoundCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:38

## SetGamepadVibrationCallback

### 宣言

```cpp
void SetGamepadVibrationCallback(const GamepadVibrationCallback& callback);
```

### 概略

gamepad 振動 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetGamepadVibrationCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:39

## SetHdRumbleCallback

### 宣言

```cpp
void SetHdRumbleCallback(const HdRumbleCallback& callback);
```

### 概略

HD Rumble callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetHdRumbleCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:40

## SetDualSenseHapticsCallback

### 宣言

```cpp
void SetDualSenseHapticsCallback(const DualSenseHapticsCallback& callback);
```

### 概略

DualSense haptics callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetDualSenseHapticsCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:41

## SetTriggerFeedbackCallback

### 宣言

```cpp
void SetTriggerFeedbackCallback(const TriggerFeedbackCallback& callback);
```

### 概略

trigger feedback callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetTriggerFeedbackCallback();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:42

# 振動直接実行

## VibratePattern

### 宣言

```cpp
void VibratePattern(const char* patternName);
```

### 概略

名前付き pattern で振動します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::VibratePattern();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:50

## VibrateFromSound

### 宣言

```cpp
void VibrateFromSound(const char* soundPath);
```

### 概略

音声ファイルから振動を作って再生します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::VibrateFromSound();
```

### 実装状態

本実装。宣言場所: haptics/FeelKitVibration.h:51

# 画面演出 callback

## SetScreenShakeCallback

### 宣言

```cpp
void SetScreenShakeCallback(const ScreenShakeCallback& callback);
```

### 概略

画面揺れ callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetScreenShakeCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:38

## SetScreenZoomCallback

### 宣言

```cpp
void SetScreenZoomCallback(const ScreenZoomCallback& callback);
```

### 概略

ズーム callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetScreenZoomCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:39

## SetHitStopFrameCallback

### 宣言

```cpp
void SetHitStopFrameCallback(const HitStopFrameCallback& callback);
```

### 概略

hit stop callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetHitStopFrameCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:40

## SetScreenFlashCallback

### 宣言

```cpp
void SetScreenFlashCallback(const ScreenFlashCallback& callback);
```

### 概略

flash callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetScreenFlashCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:41

## SetScreenColorChangeCallback

### 宣言

```cpp
void SetScreenColorChangeCallback(const ScreenColorChangeCallback& callback);
```

### 概略

画面色変化 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetScreenColorChangeCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:42

## SetSlowMotionFrameCallback

### 宣言

```cpp
void SetSlowMotionFrameCallback(const SlowMotionFrameCallback& callback);
```

### 概略

slow motion callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetSlowMotionFrameCallback();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:43

# 画面演出 request

## PlayScreenShake

### 宣言

```cpp
bool PlayScreenShake(const ScreenShakeRequest& request);
```

### 概略

画面揺れ request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayScreenShake();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:45

## PlayScreenZoom

### 宣言

```cpp
bool PlayScreenZoom(const ScreenZoomRequest& request);
```

### 概略

ズーム request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayScreenZoom();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:46

## PlayHitStop

### 宣言

```cpp
bool PlayHitStop(const HitStopRequest& request);
```

### 概略

hit stop request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayHitStop();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:47

## PlayScreenFlash

### 宣言

```cpp
bool PlayScreenFlash(const ScreenFlashRequest& request);
```

### 概略

flash request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayScreenFlash();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:48

## PlayScreenColorChange

### 宣言

```cpp
bool PlayScreenColorChange(const ScreenColorChangeRequest& request);
```

### 概略

画面色変化 request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayScreenColorChange();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:49

## PlaySlowMotion

### 宣言

```cpp
bool PlaySlowMotion(const SlowMotionRequest& request);
```

### 概略

slow motion request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlaySlowMotion();
```

### 実装状態

本実装。宣言場所: screen/FeelKitScreen.h:50

# エフェクト callback

## SetExplosionCallback

### 宣言

```cpp
void SetExplosionCallback(const ExplosionCallback& callback);
```

### 概略

爆発 effect callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetExplosionCallback();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:50

## SetSparkCallback

### 宣言

```cpp
void SetSparkCallback(const SparkCallback& callback);
```

### 概略

火花 effect callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetSparkCallback();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:51

## SetSmokeCallback

### 宣言

```cpp
void SetSmokeCallback(const SmokeCallback& callback);
```

### 概略

煙 effect callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetSmokeCallback();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:52

## SetImpactEffectCallback

### 宣言

```cpp
void SetImpactEffectCallback(const ImpactEffectCallback& callback);
```

### 概略

着弾 effect callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetImpactEffectCallback();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:53

## SetParticleCallback

### 宣言

```cpp
void SetParticleCallback(const ParticleCallback& callback);
```

### 概略

particle callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetParticleCallback();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:54

# エフェクト request

## PlayExplosion

### 宣言

```cpp
bool PlayExplosion(const ExplosionRequest& request);
```

### 概略

爆発 effect request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayExplosionRequest();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:57

## PlaySpark

### 宣言

```cpp
bool PlaySpark(const SparkRequest& request);
```

### 概略

火花 effect request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlaySpark();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:58

## PlaySmoke

### 宣言

```cpp
bool PlaySmoke(const SmokeRequest& request);
```

### 概略

煙 effect request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlaySmoke();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:59

## PlayImpactEffect

### 宣言

```cpp
bool PlayImpactEffect(const ImpactEffectRequest& request);
```

### 概略

着弾 effect request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayImpactEffect();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:60

## PlayParticle

### 宣言

```cpp
bool PlayParticle(const ParticleRequest& request);
```

### 概略

particle request を実行します。

### 引数

`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。

### 戻り値

- `true` : 実行成功
- `false` : callback 未登録、引数不正、無効状態など

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlayParticle();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:61

# エフェクト pool

## EffectPoolGetActiveCount

### 宣言

```cpp
int EffectPoolGetActiveCount();
```

### 概略

active effect 数を取得します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::EffectPoolGetActiveCount();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:78

## EffectPoolKillAll

### 宣言

```cpp
void EffectPoolKillAll();
```

### 概略

全 active effect を停止します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::EffectPoolKillAll();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:79

## EffectPoolKill

### 宣言

```cpp
void EffectPoolKill(int effectId);
```

### 概略

指定 effect を停止します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::EffectPoolKill();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:80

## EffectPoolIsAlive

### 宣言

```cpp
bool EffectPoolIsAlive(int effectId);
```

### 概略

指定 effect が再生中か確認します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::EffectPoolIsAlive();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:81

# 軌跡 / 残像 / emitter

## ReleaseTrail

### 宣言

```cpp
void ReleaseTrail(TrailHandle handle);
```

### 概略

trail handle を解放します。

### 引数

`handle` : 対象 handle。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::ReleaseTrail();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:88

## ReleaseAfterImage

### 宣言

```cpp
void ReleaseAfterImage(AfterImageHandle handle);
```

### 概略

afterimage handle を解放します。

### 引数

`handle` : 対象 handle。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::ReleaseAfterImage();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:90

## StopEmit

### 宣言

```cpp
void StopEmit(EmitterHandle handle);
```

### 概略

emitter handle を停止します。

### 引数

`handle` : 対象 handle。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::StopEmit();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:94

## AddScreenEffect

### 宣言

```cpp
void AddScreenEffect(const ScreenEffectDesc& effectDesc);
```

### 概略

画面空間 effect を追加します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::AddScreenEffect();
```

### 実装状態

本実装。宣言場所: effects/FeelKitEffects.h:100

# 自動生成 helper

## GenerateImpactHaptics

### 宣言

```cpp
void GenerateImpactHaptics(float velocity, float mass);
void GenerateImpactHaptics(Vec3 velocity, float mass);
```

### 概略

衝突速度と質量から振動を自動生成します。

### 引数

- `velocity` : 衝突速度
- `mass` : 衝突した物体の質量

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GenerateImpactHaptics();
```

### 実装状態

本実装。宣言場所: generation/FeelKitImpactGeneration.h:10, generation/FeelKitImpactGeneration.h:11

## GenerateImpactShake

### 宣言

```cpp
void GenerateImpactShake(float velocity, float mass);
void GenerateImpactShake(Vec3 velocity, float mass);
```

### 概略

衝突速度と質量から画面揺れを自動生成します。

### 引数

- `velocity` : 衝突速度
- `mass` : 衝突した物体の質量

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GenerateImpactShake();
```

### 実装状態

本実装。宣言場所: generation/FeelKitImpactGeneration.h:12, generation/FeelKitImpactGeneration.h:13

# 素材 provider

## SetTextureDataProvider

### 宣言

```cpp
void SetTextureDataProvider(TextureDataProvider provider);
```

### 概略

texture 情報 provider を登録します。

### 引数

`provider` : 素材情報を FeelKit に渡すための関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetTextureDataProvider();
```

### 実装状態

本実装。宣言場所: generation/FeelKitImpactGeneration.h:33

## SetGifDataProvider

### 宣言

```cpp
void SetGifDataProvider(GifDataProvider provider);
```

### 概略

GIF frame provider を登録します。

### 引数

`provider` : 素材情報を FeelKit に渡すための関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetGifDataProvider();
```

### 実装状態

本実装。宣言場所: generation/FeelKitImpactGeneration.h:34

## SetAnimationHapticPattern

### 宣言

```cpp
void SetAnimationHapticPattern(const char* animationName,
const AnimationHapticPattern& pattern);
```

### 概略

animation 名と振動 pattern を登録します。

### 引数

- `animationName` : animation 名
- `pattern` : 対応する振動 pattern

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetAnimationHapticPattern();
```

### 実装状態

本実装。宣言場所: generation/FeelKitImpactGeneration.h:36

# シーケンス / timeline

## PlaySequence

### 宣言

```cpp
void PlaySequence(const char* sequenceName);
void PlaySequence(const SequenceEvent* events, int eventCount);
```

### 概略

複数 event の sequence を実行します。

### 引数

- `sequenceName` : 登録済み sequence 名
- `events` : 直接渡す event 配列
- `eventCount` : event 数

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::PlaySequence();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:630, core/FeelKitCore.h:631

## Timeline

### 宣言

```cpp
TimelineHandle Timeline();
```

### 概略

timeline handle を作ります。

### 引数

なし

### 戻り値

作成または取得した handle を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
TimelineHandle timeline = FeelKit::Timeline();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:632

## TimelineAddEvent

### 宣言

```cpp
void TimelineAddEvent(TimelineHandle handle, float timeSeconds, const char* eventName);
```

### 概略

timeline に event を追加します。

### 引数

- `handle` : 対象 timeline
- `timeSeconds` : event 発火秒数
- `eventName` : event 名

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::TimelineAddEvent();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:633

## TimelinePlay

### 宣言

```cpp
void TimelinePlay(TimelineHandle handle);
```

### 概略

timeline を再生します。

### 引数

`handle` : 対象 handle。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::TimelinePlay();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:634

## TimelineStop

### 宣言

```cpp
void TimelineStop(TimelineHandle handle);
```

### 概略

timeline を停止します。

### 引数

`handle` : 対象 handle。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::TimelineStop();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:635

# 記録 / 描画 callback

## StartEffectRecording

### 宣言

```cpp
void StartEffectRecording();
```

### 概略

effect 発火履歴の記録を開始します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::StartEffectRecording();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:637

## ReplayEffect

### 宣言

```cpp
void ReplayEffect();
```

### 概略

記録した effect 発火履歴を再生します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::ReplayEffect();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:638

## SetTextDrawCallback

### 宣言

```cpp
void SetTextDrawCallback(TextDrawCallback cb);
```

### 概略

text effect 描画 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetTextDrawCallback();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:641

## SetFragmentDrawCallback

### 宣言

```cpp
void SetFragmentDrawCallback(FragmentDrawCallback cb);
```

### 概略

fragment 描画 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetFragmentDrawCallback();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:642

## SetScreenEffectDrawCallback

### 宣言

```cpp
void SetScreenEffectDrawCallback(ScreenEffectDrawCallback cb);
```

### 概略

screen effect 描画 callback を登録します。

### 引数

`???` : ゲーム側の処理を受け取る関数オブジェクト。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetScreenEffectDrawCallback();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:643

# 状態取得

## GetOcclusionFactor

### 宣言

```cpp
float GetOcclusionFactor();
```

### 概略

occlusion 係数を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetOcclusionFactor();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:647

## SetOcclusionFactor

### 宣言

```cpp
void SetOcclusionFactor(float factor);
```

### 概略

occlusion 係数を設定します。

### 引数

`factor` : 遮蔽係数。小さいほど音や演出を弱くします。

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::SetOcclusionFactor();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:648

## GetTextEffectCount

### 宣言

```cpp
int GetTextEffectCount();
```

### 概略

active text effect 数を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetTextEffectCount();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:651

## GetFragmentCount

### 宣言

```cpp
int GetFragmentCount();
```

### 概略

active fragment 数を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetFragmentCount();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:653

## GetScreenEffectCount

### 宣言

```cpp
int GetScreenEffectCount();
```

### 概略

active screen effect 数を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetScreenEffectCount();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:655

## GetTrailCount

### 宣言

```cpp
int GetTrailCount();
```

### 概略

trail 数を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetTrailCount();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:657

## GetTrailHandle

### 宣言

```cpp
TrailHandle GetTrailHandle(int index);
```

### 概略

index から trail handle を取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
FeelKit::GetTrailHandle();
```

### 実装状態

本実装。宣言場所: core/FeelKitCore.h:658

# 振動専用 member

## shutdown

### 宣言

```cpp
void shutdown();
```

### 概略

FeelKitHaptics を終了します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.shutdown();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:55

## update

### 宣言

```cpp
void update();
```

### 概略

FeelKitHaptics の振動停止タイマーを更新します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.update();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:56

## refreshDevice

### 宣言

```cpp
bool refreshDevice();
```

### 概略

controller 接続状態を再取得します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.refreshDevice();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:58

## getDeviceState

### 宣言

```cpp
FeelKitHapticsDeviceState getDeviceState() const;
```

### 概略

controller の状態を取得します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.getDeviceState();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:59

## setEnabled

### 宣言

```cpp
void setEnabled(bool isEnabled);
```

### 概略

振動の有効 / 無効を切り替えます。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.setEnabled();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:61

## isVibrationEnabled

### 宣言

```cpp
bool isVibrationEnabled() const;
```

### 概略

振動が有効か確認します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.isVibrationEnabled();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:62

## isReady

### 宣言

```cpp
bool isReady() const;
```

### 概略

振動 device が使用可能か確認します。

### 引数

なし

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.isReady();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:63

## setMasterScale

### 宣言

```cpp
void setMasterScale(float masterScale);
```

### 概略

振動全体の倍率を設定します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.setMasterScale();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:65

## getMasterScale

### 宣言

```cpp
float getMasterScale() const;
```

### 概略

振動全体の倍率を取得します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.getMasterScale();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:66

## clearSoundVibrations

### 宣言

```cpp
void clearSoundVibrations();
```

### 概略

音名振動の登録を全消去します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.clearSoundVibrations();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:72

## vibrateSound

### 宣言

```cpp
bool vibrateSound(const char* soundPath, bool shouldVibrate = true);
bool vibrateSound(const wchar_t* soundPath, bool shouldVibrate = true);
bool vibrateSound(const char* soundPath,
const FeelKitHapticsVibrationDesc& vibrationDesc);
bool vibrateSound(const wchar_t* soundPath,
const FeelKitHapticsVibrationDesc& vibrationDesc);
```

### 概略

音名に紐づく振動を再生します。

### 引数

- `soundPath` : 音声パスまたは音名
- `shouldVibrate` : false の場合は振動しない
- `vibrationDesc` : その場で使う振動設定

### 戻り値

現在値、状態、または成功可否を返します。

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.vibrateSound("hit.wav", true);
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:74, FeelKitHaptics.h:75, FeelKitHaptics.h:77, FeelKitHaptics.h:79

## stop

### 宣言

```cpp
void stop();
```

### 概略

現在の振動を停止します。

### 引数

なし

### 戻り値

なし

### 解説

FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。

### サンプルコード

```cpp
haptics.stop();
```

### 実装状態

本実装。宣言場所: FeelKitHaptics.h:87
