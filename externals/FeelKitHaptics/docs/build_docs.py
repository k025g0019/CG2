#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
FeelKit の Markdown リファレンスを、人間が読みやすい HTML に変換するスクリプト。

目的:
    - API_REFERENCE.md を OpenCode / 引き継ぎ用として残す
    - docs/api-reference.html を人間が検索・クリック・コピーしやすい UI にする
"""

from __future__ import annotations

import hashlib
import html
import re
from pathlib import Path


# ================================================================
# パス設定
# ================================================================

ROOT_DIR = Path(__file__).resolve().parents[1]
REFERENCE_PATH = ROOT_DIR / "API_REFERENCE.md"
OUTPUT_PATH = ROOT_DIR / "docs" / "api-reference.html"


# ================================================================
# 公開 API 名の収集
# ================================================================

HEADER_PATHS = [
    ROOT_DIR / "audio" / "FeelKitAudio.h",
    ROOT_DIR / "haptics" / "FeelKitVibration.h",
    ROOT_DIR / "screen" / "FeelKitScreen.h",
    ROOT_DIR / "effects" / "FeelKitEffects.h",
    ROOT_DIR / "generation" / "FeelKitImpactGeneration.h",
    ROOT_DIR / "core" / "FeelKitCore.h",
    ROOT_DIR / "debug" / "FeelKitDebugView.h",
    ROOT_DIR / "FeelKitHaptics.h",
]

INTERNAL_FUNCTION_NAMES = {
    "clampFloat",
    "clampInt",
    "vec3Length",
    "updateTimer",
    "getCurrentFrameIndex",
    "makeDrawInfo",
    "advanceTimelines",
    "readAudioFileFeatures",
    "extractAudioFrames",
    "materialMultiplier",
    "readWav",
    "runInteractiveMenu",
}

FUNCTION_DECLARATION_RE = re.compile(
    r"^\s*(?:void|bool|int|float|double|[A-Za-z_][A-Za-z0-9_:<>]*)"
    r"(?:\s+|\s*&\s*|\s*\*\s*)([A-Za-z_][A-Za-z0-9_]*)\s*\("
)

CATEGORY_LABELS = {
    "Overview": "概要",
    "目次": "目次",
    "共通ライフサイクル": "共通ライフサイクル",
    "Audio": "音",
    "Haptics": "振動",
    "Screen": "画面演出",
    "Effects": "エフェクト",
    "Generation": "自動生成",
    "Debug / Query": "デバッグ / 状態取得",
    "FeelKitHaptics": "振動専用クラス",
    "Audio callback API": "音 callback",
    "Audio analysis / request API": "音解析 / 再生 request",
    "Audio generated effect API": "音から演出生成",
    "Haptics callback API": "振動 callback",
    "Haptics direct API": "振動直接実行",
    "Screen callback API": "画面演出 callback",
    "Screen request API": "画面演出 request",
    "Effect callback API": "エフェクト callback",
    "Effect request API": "エフェクト request",
    "Effect pool API": "エフェクト pool",
    "Trail / afterimage / emitter API": "軌跡 / 残像 / emitter",
    "Generation helper API": "自動生成 helper",
    "Generation provider API": "素材 provider",
    "Sequence / timeline API": "シーケンス / timeline",
    "Recording / draw callback API": "記録 / 描画 callback",
    "State query API": "状態取得",
    "FeelKitHaptics member API": "振動専用 member",
}

ROLE_ORDER = [
    "準備",
    "実行",
    "計算",
    "状態取得",
    "管理",
    "デバッグ",
]

DOMAIN_ORDER = [
    "共通ライフサイクル",
    "音",
    "振動",
    "画面演出",
    "エフェクト",
    "衝突 / 爆発",
    "素材変換",
    "アニメーション",
    "デバッグ / 状態取得",
    "描画受け渡し",
    "振動専用クラス",
    "その他",
]

NAV_SUMMARY_OVERRIDES = {
    "SetHapticsBackend": "振動連携先を設定",
    "SetSoundEffectCallback": "SE の再生先を設定",
    "SetBgmCallback": "BGM の再生先を設定",
    "SetStopBgmCallback": "BGM の停止先を設定",
    "SetBgmSwitchCallback": "BGM 切り替え先を設定",
    "SetSpatialSoundCallback": "3D 音響の再生先を設定",
    "SetVoiceCallback": "ボイスの再生先を設定",
    "SetScreenShakeCallback": "画面揺れの連携先を設定",
    "SetScreenZoomCallback": "ズームの連携先を設定",
    "SetScreenFlashCallback": "フラッシュの連携先を設定",
    "SetScreenColorChangeCallback": "画面色変化の連携先を設定",
    "SetScreenEffectDrawCallback": "画面演出の描画先を設定",
    "SetEffectDrawCallback": "エフェクトの描画先を設定",
    "SetTextDrawCallback": "文字演出の描画先を設定",
    "SetFragmentDrawCallback": "破片の描画先を設定",
    "SetImageDataProvider": "画像の読み込み元を設定",
    "SetGifDataProvider": "GIF の読み込み元を設定",
    "SetAnimationHapticPattern": "アニメーション用振動パターンを設定",
    "PlayHdRumble": "HD Rumble を再生",
    "PlayDualSenseHaptics": "DualSense ハプティクスを再生",
    "PlayTriggerFeedback": "トリガー抵抗を再生",
    "PlayBGM": "BGM を再生",
    "StopBGM": "BGM を停止",
    "PlaySoundEffect": "SE を再生",
    "PlayBgmSwitch": "BGM を切り替え",
    "PlaySpatialSound": "3D 音響を再生",
    "PlayVoice": "ボイスを再生",
    "PlayParticle": "パーティクルを再生",
    "Draw": "描画先へ現在の演出状態を渡す",
}

EXPLICIT_DOMAIN_MAP = {
    "Init": "共通ライフサイクル",
    "Update": "共通ライフサイクル",
    "Draw": "共通ライフサイクル",
    "Shutdown": "共通ライフサイクル",
    "PlaySE": "音",
    "PlaySoundEffect": "音",
    "PlayBGM": "音",
    "StopBGM": "音",
    "SetMasterVolume": "音",
    "PlayBgmSwitch": "音",
    "PlaySpatialSound": "音",
    "PlayVoice": "音",
    "AnalyzeAudioFile": "音",
    "AnalyzeAudioFeatures": "音",
    "LoadWaveform": "音",
    "MakeHapticsFromAudio": "振動",
    "MakeShakeFromAudio": "画面演出",
    "MakeFlashFromAudio": "画面演出",
    "MakeParticlesFromAudio": "エフェクト",
    "MakeSlowMotionFromAudio": "画面演出",
    "PlayHapticFromAudio": "振動",
    "PlayShakeFromAudio": "画面演出",
    "PlayFlashFromAudio": "画面演出",
    "CreateParticlesFromAudio": "エフェクト",
    "SpawnEffectFromAudio": "エフェクト",
    "CreateLightFromAudio": "画面演出",
    "CreateCameraMotionFromAudio": "画面演出",
    "CreateSlowMotionFromAudio": "画面演出",
    "AnalyzeImpact": "衝突 / 爆発",
    "AnalyzeExplosion": "衝突 / 爆発",
    "GenerateImpactHaptics": "衝突 / 爆発",
    "GenerateImpactShake": "衝突 / 爆発",
    "GenerateImpactFeedback": "衝突 / 爆発",
    "GenerateExplosionFeedback": "衝突 / 爆発",
    "PlayImpact": "衝突 / 爆発",
    "PlayExplosion": "衝突 / 爆発",
    "ApplyOcclusion": "衝突 / 爆発",
    "CreateParticlesFromImage": "素材変換",
    "CreateCollisionFromImage": "素材変換",
    "BreakImage": "素材変換",
    "ImportGif": "素材変換",
    "SpawnTextEffect": "素材変換",
    "GenerateHapticsFromAnimation": "アニメーション",
    "GenerateFootsteps": "アニメーション",
    "DrawAISenses": "デバッグ / 状態取得",
}

RELATED_FUNCTIONS = {
    "AnalyzeAudioFile": [
        ("AnalyzeAudioFeatures", "共通特徴量に変換"),
        ("MakeHapticsFromAudio", "振動値を作る"),
        ("MakeShakeFromAudio", "揺れ値を作る"),
        ("MakeFlashFromAudio", "flash値を作る"),
        ("CreateParticlesFromAudio", "粒子まで生成"),
    ],
    "AnalyzeAudioFeatures": [
        ("AnalyzeAudioFile", "WAV解析元"),
        ("MakeHapticsFromAudio", "振動値へ変換"),
        ("MakeShakeFromAudio", "画面揺れへ変換"),
        ("MakeFlashFromAudio", "flashへ変換"),
        ("MakeParticlesFromAudio", "particle値へ変換"),
        ("MakeSlowMotionFromAudio", "slow motionへ変換"),
    ],
    "LoadWaveform": [
        ("AnalyzeAudioFeatures", "音声特徴量を見る"),
        ("MakeHapticsFromAudio", "振動へ使う"),
        ("MakeShakeFromAudio", "揺れへ使う"),
    ],
    "MakeHapticsFromAudio": [
        ("PlayHapticFromAudio", "解析して再生まで"),
        ("Vibrate", "生成値を手動再生"),
        ("SetHapticsBackend", "実機振動へ接続"),
    ],
    "MakeShakeFromAudio": [
        ("PlayShakeFromAudio", "解析して揺れまで"),
        ("Shake", "生成値を手動再生"),
        ("GetScreenState", "揺れ状態を取得"),
    ],
    "MakeFlashFromAudio": [
        ("PlayFlashFromAudio", "解析してflashまで"),
        ("Flash", "生成値を手動再生"),
        ("GetScreenState", "flash状態を取得"),
    ],
    "MakeParticlesFromAudio": [
        ("CreateParticlesFromAudio", "音から粒子生成"),
        ("SpawnEffectFromAudio", "effect強度へ反映"),
        ("PlayParticle", "particle request再生"),
    ],
    "MakeSlowMotionFromAudio": [
        ("CreateSlowMotionFromAudio", "音からslow motion"),
        ("SlowMotion", "手動で時間制御"),
    ],
    "PlayHapticFromAudio": [
        ("AnalyzeAudioFeatures", "解析だけ見る"),
        ("MakeHapticsFromAudio", "変換だけ見る"),
        ("SetHapticsBackend", "振動device接続"),
        ("VibrateFromSound", "callback式の音振動"),
    ],
    "PlayShakeFromAudio": [
        ("AnalyzeAudioFeatures", "解析だけ見る"),
        ("MakeShakeFromAudio", "変換だけ見る"),
        ("Shake", "直接揺らす"),
        ("GetScreenState", "結果を描画側で使う"),
    ],
    "PlayFlashFromAudio": [
        ("AnalyzeAudioFeatures", "解析だけ見る"),
        ("MakeFlashFromAudio", "変換だけ見る"),
        ("Flash", "直接flash"),
        ("GetScreenState", "結果を描画側で使う"),
    ],
    "CreateParticlesFromAudio": [
        ("AnalyzeAudioFeatures", "解析だけ見る"),
        ("MakeParticlesFromAudio", "粒子値を作る"),
        ("PlayParticle", "callbackで粒子再生"),
        ("Emit", "emitterを使う"),
    ],
    "SpawnEffectFromAudio": [
        ("AnalyzeAudioFeatures", "音を解析"),
        ("MakeParticlesFromAudio", "強度を作る"),
        ("PlayEffect", "effectを直接再生"),
    ],
    "CreateLightFromAudio": [
        ("AnalyzeAudioFeatures", "音を解析"),
        ("MakeFlashFromAudio", "明るさ変化に使う"),
        ("PlayFlashFromAudio", "画面flashとして実行"),
    ],
    "CreateCameraMotionFromAudio": [
        ("AnalyzeAudioFeatures", "音を解析"),
        ("MakeShakeFromAudio", "揺れ値に変換"),
        ("PlayShakeFromAudio", "画面揺れとして実行"),
    ],
    "CreateSlowMotionFromAudio": [
        ("AnalyzeAudioFeatures", "音を解析"),
        ("MakeSlowMotionFromAudio", "slow motion値に変換"),
        ("SlowMotion", "時間制御を実行"),
    ],
    "SetSoundEffectCallback": [
        ("PlaySE", "簡単SE再生"),
        ("PlaySoundEffect", "request式SE再生"),
        ("GenerateImpactFeedback", "impact SEにも使う"),
    ],
    "PlaySE": [
        ("SetSoundEffectCallback", "再生先を登録"),
        ("PlaySoundEffect", "request式で再生"),
        ("GenerateImpactFeedback", "impact内で呼ばれる"),
    ],
    "PlaySoundEffect": [
        ("SetSoundEffectCallback", "再生先を登録"),
        ("PlaySE", "簡単に呼ぶ"),
    ],
    "SetBgmCallback": [
        ("PlayBGM", "BGM再生"),
        ("PlayBgmSwitch", "切替request"),
    ],
    "SetStopBgmCallback": [
        ("StopBGM", "BGM停止"),
        ("PlayBGM", "BGM再生"),
    ],
    "SetBgmSwitchCallback": [
        ("PlayBgmSwitch", "BGM切替"),
        ("PlayBGM", "通常再生"),
    ],
    "PlayBGM": [
        ("SetBgmCallback", "再生先を登録"),
        ("StopBGM", "停止"),
        ("SetMasterVolume", "音量調整"),
    ],
    "StopBGM": [
        ("SetStopBgmCallback", "停止先を登録"),
        ("PlayBGM", "再生"),
    ],
    "PlayBgmSwitch": [
        ("SetBgmSwitchCallback", "切替先を登録"),
        ("PlayBGM", "通常再生"),
    ],
    "SetSpatialSoundCallback": [
        ("PlaySpatialSound", "3D音響再生"),
    ],
    "PlaySpatialSound": [
        ("SetSpatialSoundCallback", "再生先を登録"),
    ],
    "SetVoiceCallback": [
        ("PlayVoice", "ボイス再生"),
    ],
    "PlayVoice": [
        ("SetVoiceCallback", "再生先を登録"),
    ],
    "SetMasterVolume": [
        ("GetMasterVolume", "現在値を確認"),
        ("PlaySE", "SEに反映"),
        ("PlayBGM", "BGMに反映"),
    ],
    "GetMasterVolume": [
        ("SetMasterVolume", "音量を設定"),
    ],
    "SetHapticsBackend": [
        ("Vibrate", "直接振動"),
        ("PlayHapticFromAudio", "音から振動"),
        ("GenerateImpactHaptics", "衝突から振動"),
    ],
    "Vibrate": [
        ("SetHapticsBackend", "device接続"),
        ("GenerateImpactHaptics", "衝突から自動振動"),
        ("PlayHapticFromAudio", "音から自動振動"),
    ],
    "VibratePattern": [
        ("SetVibrationPatternCallback", "pattern先を登録"),
        ("Vibrate", "直接振動"),
    ],
    "VibrateFromSound": [
        ("SetVibrationFromSoundCallback", "音振動先を登録"),
        ("PlayHapticFromAudio", "解析込み音振動"),
    ],
    "PlayGamepadVibration": [
        ("SetGamepadVibrationCallback", "callback登録"),
        ("SetHapticsBackend", "標準backend接続"),
    ],
    "PlayHdRumble": [
        ("SetHdRumbleCallback", "callback登録"),
    ],
    "PlayDualSenseHaptics": [
        ("SetDualSenseHapticsCallback", "callback登録"),
    ],
    "PlayTriggerFeedback": [
        ("SetTriggerFeedbackCallback", "callback登録"),
    ],
    "Shake": [
        ("PlayScreenShake", "request式"),
        ("PlayShakeFromAudio", "音から揺れ"),
        ("GenerateImpactShake", "衝突から揺れ"),
        ("GetScreenState", "描画側で取得"),
    ],
    "Flash": [
        ("PlayScreenFlash", "request式"),
        ("PlayFlashFromAudio", "音からflash"),
        ("GetScreenState", "描画側で取得"),
    ],
    "Zoom": [
        ("PlayScreenZoom", "request式"),
        ("GetScreenState", "描画側で取得"),
    ],
    "HitStop": [
        ("PlayHitStop", "request式"),
        ("GenerateImpactFeedback", "衝突feedbackで実行"),
        ("GetScreenState", "停止状態を取得"),
    ],
    "SlowMotion": [
        ("PlaySlowMotion", "request式"),
        ("CreateSlowMotionFromAudio", "音からslow motion"),
        ("GetScreenState", "時間状態を取得"),
    ],
    "GetScreenState": [
        ("Shake", "揺れ状態"),
        ("Flash", "flash状態"),
        ("Zoom", "zoom状態"),
        ("HitStop", "hit stop状態"),
        ("SlowMotion", "slow motion状態"),
    ],
    "PlayScreenShake": [
        ("SetScreenShakeCallback", "callback登録"),
        ("Shake", "直接実行"),
    ],
    "PlayScreenZoom": [
        ("SetScreenZoomCallback", "callback登録"),
        ("Zoom", "直接実行"),
    ],
    "PlayHitStop": [
        ("SetHitStopFrameCallback", "callback登録"),
        ("HitStop", "直接実行"),
    ],
    "PlayScreenFlash": [
        ("SetScreenFlashCallback", "callback登録"),
        ("Flash", "直接実行"),
    ],
    "PlayScreenColorChange": [
        ("SetScreenColorChangeCallback", "callback登録"),
        ("AddScreenEffect", "画面effectとして管理"),
    ],
    "PlaySlowMotion": [
        ("SetSlowMotionFrameCallback", "callback登録"),
        ("SlowMotion", "直接実行"),
    ],
    "LoadEffect": [
        ("PlayEffect", "登録済みeffectを再生"),
    ],
    "PlayEffect": [
        ("LoadEffect", "名前登録"),
        ("SetEffectDrawCallback", "描画先登録"),
        ("EffectPoolGetActiveCount", "再生数確認"),
        ("EffectPoolKill", "停止"),
    ],
    "SetEffectDrawCallback": [
        ("PlayEffect", "描画情報を受ける"),
        ("Draw", "callbackを流す"),
    ],
    "EffectPoolSetMax": [
        ("PlayEffect", "poolで再生"),
        ("EffectPoolGetActiveCount", "数を確認"),
        ("EffectPoolKillAll", "全停止"),
    ],
    "EffectPoolGetActiveCount": [
        ("PlayEffect", "再生元"),
        ("ShowEffectDebugger", "debug表示"),
    ],
    "EffectPoolKill": [
        ("PlayEffect", "停止対象"),
        ("EffectPoolIsAlive", "生存確認"),
    ],
    "EffectPoolKillAll": [
        ("EffectPoolGetActiveCount", "停止前確認"),
    ],
    "EffectPoolIsAlive": [
        ("PlayEffect", "対象idを作る"),
        ("EffectPoolKill", "停止"),
    ],
    "SpawnDecal": [
        ("PlayImpactEffect", "着弾から出す"),
        ("EffectPoolSetMax", "大量管理"),
    ],
    "CreateTrail": [
        ("ReleaseTrail", "解放"),
        ("GetTrailCount", "数を確認"),
        ("GetTrailHandle", "handle取得"),
    ],
    "ReleaseTrail": [
        ("CreateTrail", "作成元"),
    ],
    "CreateAfterImage": [
        ("ReleaseAfterImage", "解放"),
    ],
    "ReleaseAfterImage": [
        ("CreateAfterImage", "作成元"),
    ],
    "Emit": [
        ("StopEmit", "停止"),
        ("PlayParticle", "request式粒子"),
    ],
    "StopEmit": [
        ("Emit", "作成元"),
    ],
    "PlayEffectLod": [
        ("LoadEffect", "effect登録"),
        ("PlayEffect", "通常再生"),
    ],
    "AddScreenEffect": [
        ("SetScreenEffectDrawCallback", "描画先登録"),
        ("Draw", "描画情報を流す"),
    ],
    "PlayExplosion": [
        ("GenerateExplosionFeedback", "爆発feedback生成"),
        ("PlayEffect", "見た目effect"),
        ("Vibrate", "振動"),
        ("Shake", "画面揺れ"),
        ("PlaySE", "爆発音"),
    ],
    "PlaySpark": [
        ("SetSparkCallback", "callback登録"),
        ("PlayImpactEffect", "着弾effect"),
    ],
    "PlaySmoke": [
        ("SetSmokeCallback", "callback登録"),
        ("PlayExplosion", "爆発演出と併用"),
    ],
    "PlayImpactEffect": [
        ("SetImpactEffectCallback", "callback登録"),
        ("GenerateImpactFeedback", "衝突feedback"),
        ("SpawnDecal", "弾痕など"),
    ],
    "PlayParticle": [
        ("SetParticleCallback", "callback登録"),
        ("Emit", "emitter式"),
    ],
    "AnalyzeImpact": [
        ("GenerateImpactHaptics", "振動まで実行"),
        ("GenerateImpactShake", "揺れまで実行"),
        ("GenerateImpactFeedback", "全部まとめて実行"),
        ("PlayImpact", "intensityで統合実行"),
    ],
    "GenerateImpactHaptics": [
        ("AnalyzeImpact", "解析だけ見る"),
        ("Vibrate", "実際の振動"),
        ("GenerateImpactFeedback", "全部まとめる"),
    ],
    "GenerateImpactShake": [
        ("AnalyzeImpact", "解析だけ見る"),
        ("Shake", "実際の揺れ"),
        ("GenerateImpactFeedback", "全部まとめる"),
    ],
    "GenerateImpactFeedback": [
        ("AnalyzeImpact", "基礎解析"),
        ("Vibrate", "内部で呼ぶ"),
        ("Shake", "内部で呼ぶ"),
        ("HitStop", "内部で呼ぶ"),
        ("PlaySE", "内部で呼ぶ"),
        ("PlayImpact", "簡易統合実行"),
    ],
    "GenerateExplosionFeedback": [
        ("PlayExplosion", "爆発演出を実行"),
        ("Vibrate", "爆発振動"),
        ("Shake", "爆発揺れ"),
        ("PlaySE", "爆発音"),
    ],
    "PlayImpact": [
        ("GenerateImpactFeedback", "詳細計算版"),
        ("Vibrate", "振動"),
        ("Shake", "画面揺れ"),
        ("HitStop", "hit stop"),
        ("PlaySE", "impact音"),
    ],
    "SetImageDataProvider": [
        ("CreateParticlesFromImage", "画像から粒子"),
        ("CreateCollisionFromImage", "画像から当たり判定"),
    ],
    "SetTextureDataProvider": [
        ("BreakImage", "textureを破片化"),
    ],
    "SetGifDataProvider": [
        ("ImportGif", "GIF取り込み"),
    ],
    "SetAnimationHapticPattern": [
        ("GenerateHapticsFromAnimation", "animationから振動"),
        ("GenerateFootsteps", "足音生成"),
    ],
    "CreateParticlesFromImage": [
        ("SetImageDataProvider", "画像情報を渡す"),
        ("Emit", "粒子発生"),
    ],
    "CreateCollisionFromImage": [
        ("SetImageDataProvider", "画像情報を渡す"),
    ],
    "BreakImage": [
        ("SetTextureDataProvider", "texture情報を渡す"),
        ("SetFragmentDrawCallback", "破片描画"),
        ("Draw", "描画情報を流す"),
    ],
    "ImportGif": [
        ("SetGifDataProvider", "GIF情報を渡す"),
        ("PlayEffect", "sprite sheetとして再生"),
    ],
    "SpawnTextEffect": [
        ("SetTextDrawCallback", "文字描画"),
        ("Draw", "描画情報を流す"),
    ],
    "GenerateHapticsFromAnimation": [
        ("SetAnimationHapticPattern", "pattern登録"),
        ("Vibrate", "実際の振動"),
    ],
    "GenerateFootsteps": [
        ("SetAnimationHapticPattern", "pattern登録"),
        ("PlaySE", "足音SE"),
        ("Vibrate", "足音振動"),
    ],
    "SetTextDrawCallback": [
        ("SpawnTextEffect", "text effect生成"),
        ("Draw", "callback実行"),
    ],
    "SetFragmentDrawCallback": [
        ("BreakImage", "fragment生成"),
        ("Draw", "callback実行"),
    ],
    "SetScreenEffectDrawCallback": [
        ("AddScreenEffect", "screen effect追加"),
        ("Draw", "callback実行"),
    ],
    "PlaySequence": [
        ("Timeline", "timelineを作る"),
        ("TimelineAddEvent", "event追加"),
        ("TimelinePlay", "再生"),
    ],
    "Timeline": [
        ("TimelineAddEvent", "event追加"),
        ("TimelinePlay", "再生"),
        ("TimelineStop", "停止"),
    ],
    "TimelineAddEvent": [
        ("Timeline", "作成元"),
        ("TimelinePlay", "再生"),
    ],
    "TimelinePlay": [
        ("Timeline", "作成元"),
        ("TimelineStop", "停止"),
    ],
    "TimelineStop": [
        ("TimelinePlay", "再生元"),
    ],
    "StartEffectRecording": [
        ("ReplayEffect", "記録を再生"),
        ("PlayEffect", "記録対象"),
    ],
    "ReplayEffect": [
        ("StartEffectRecording", "記録開始"),
    ],
    "SetOcclusionFactor": [
        ("GetOcclusionFactor", "現在値確認"),
        ("ApplyOcclusion", "遮蔽を適用"),
        ("GenerateImpactFeedback", "音量や強度に反映"),
    ],
    "GetOcclusionFactor": [
        ("SetOcclusionFactor", "値を設定"),
    ],
    "ApplyOcclusion": [
        ("SetOcclusionFactor", "係数を設定"),
        ("GetOcclusionFactor", "現在値確認"),
    ],
    "GetTextEffectCount": [
        ("SpawnTextEffect", "生成元"),
        ("SetTextDrawCallback", "描画先"),
    ],
    "GetFragmentCount": [
        ("BreakImage", "生成元"),
        ("SetFragmentDrawCallback", "描画先"),
    ],
    "GetScreenEffectCount": [
        ("AddScreenEffect", "生成元"),
        ("SetScreenEffectDrawCallback", "描画先"),
    ],
    "GetTrailCount": [
        ("CreateTrail", "生成元"),
        ("GetTrailHandle", "handle取得"),
    ],
    "GetTrailHandle": [
        ("CreateTrail", "生成元"),
        ("GetTrailCount", "数を確認"),
    ],
    "ShowEffectDebugger": [
        ("SetDebugLogCallback", "ログ出力先"),
        ("EffectPoolGetActiveCount", "再生数確認"),
        ("GetScreenState", "画面演出状態"),
    ],
    "ShowHapticTimeline": [
        ("SetDebugLogCallback", "ログ出力先"),
        ("Vibrate", "振動確認"),
    ],
    "DrawAISenses": [
        ("SetDebugLogCallback", "ログ出力先"),
        ("ShowEffectDebugger", "debug確認"),
    ],
    "SetDebugLogCallback": [
        ("ShowEffectDebugger", "effect debug"),
        ("ShowHapticTimeline", "haptic debug"),
        ("DrawAISenses", "AI感覚debug"),
        ("ApplyOcclusion", "遮蔽debug"),
    ],
    "initialize": [
        ("setEnabled", "振動ON/OFF"),
        ("playOneShot", "振動再生"),
        ("shutdown", "終了"),
    ],
    "shutdown": [
        ("initialize", "初期化"),
        ("stop", "停止"),
    ],
    "update": [
        ("playOneShot", "停止タイマー更新"),
    ],
    "refreshDevice": [
        ("getDeviceState", "状態確認"),
        ("isReady", "利用可能か確認"),
    ],
    "getDeviceState": [
        ("refreshDevice", "再取得"),
        ("isReady", "簡易確認"),
    ],
    "setEnabled": [
        ("isVibrationEnabled", "現在値確認"),
        ("playOneShot", "振動再生"),
    ],
    "isVibrationEnabled": [
        ("setEnabled", "ON/OFF設定"),
    ],
    "isReady": [
        ("initialize", "初期化"),
        ("refreshDevice", "再取得"),
    ],
    "setMasterScale": [
        ("getMasterScale", "現在値確認"),
        ("playOneShot", "振動に反映"),
    ],
    "getMasterScale": [
        ("setMasterScale", "倍率設定"),
    ],
    "registerSoundVibration": [
        ("vibrateSound", "登録した振動を再生"),
        ("clearSoundVibrations", "登録を消す"),
    ],
    "vibrateSound": [
        ("registerSoundVibration", "事前登録"),
        ("playOneShot", "直接振動"),
    ],
    "clearSoundVibrations": [
        ("registerSoundVibration", "登録する"),
    ],
    "playOneShot": [
        ("setEnabled", "ON/OFF"),
        ("setMasterScale", "倍率"),
        ("stop", "停止"),
    ],
    "stop": [
        ("playOneShot", "再生元"),
        ("shutdown", "終了"),
    ],
}


def collect_public_function_names() -> set[str]:
    """公開ヘッダから関数名を集める。"""

    function_names: set[str] = set()

    for header_path in HEADER_PATHS:
        if not header_path.exists():
            continue

        for line in header_path.read_text(encoding="utf-8-sig").splitlines():
            match = FUNCTION_DECLARATION_RE.match(line)

            if match is None:
                continue

            function_name = match.group(1)

            if function_name in INTERNAL_FUNCTION_NAMES:
                continue

            function_names.add(function_name)

    return function_names


# ================================================================
# HTML 変換ユーティリティ
# ================================================================

INLINE_CODE_RE = re.compile(r"`([^`]+)`")
URL_RE = re.compile(r"(https?://[^\s<]+)")


def make_slug(text: str, used_slugs: set[str]) -> str:
    """見出し文字列から重複しない id を作る。"""

    ascii_slug = re.sub(r"[^A-Za-z0-9_-]+", "-", text.strip()).strip("-").lower()

    if not ascii_slug:
        digest = hashlib.sha1(text.encode("utf-8")).hexdigest()[:10]
        ascii_slug = f"section-{digest}"

    slug = ascii_slug
    suffix = 2

    while slug in used_slugs:
        slug = f"{ascii_slug}-{suffix}"
        suffix += 1

    used_slugs.add(slug)
    return slug


def inline_markup(text: str) -> str:
    """最小限の Markdown inline 表現を HTML に変換する。"""

    escaped = html.escape(text)
    escaped = INLINE_CODE_RE.sub(r"<code>\1</code>", escaped)
    escaped = URL_RE.sub(r'<a href="\1" target="_blank" rel="noreferrer">\1</a>', escaped)
    return escaped


def translate_category(category_name: str) -> str:
    """左ナビに出すカテゴリ名を日本語へ変換する。"""

    return CATEGORY_LABELS.get(category_name, category_name)


def infer_domain(function_name: str, source_category: str) -> str:
    """関数名から、左ナビの上位分類を決める。"""

    if function_name in EXPLICIT_DOMAIN_MAP:
        return EXPLICIT_DOMAIN_MAP[function_name]

    if function_name.startswith(("SetSound", "SetBgm", "SetStopBgm", "SetSpatialSound", "SetVoice")):
        return "音"

    if function_name.startswith(("SetHaptics", "Vibrate", "PlayAdaptiveTrigger", "PlayResistanceTrigger", "PlayHaptic")):
        return "振動"

    if function_name.startswith(("SetScreen", "Shake", "Flash", "Zoom", "HitStop", "SlowMotion", "CreateCameraMotion", "CreateLight")):
        return "画面演出"

    if function_name.startswith(("SetEffect", "PlayEffect", "PlayParticle", "PlaySequence", "CreateTrail", "CreateAfterImage", "Emit", "SpawnDecal")):
        return "エフェクト"

    if function_name.startswith(("SetImageDataProvider", "SetGifDataProvider", "SetAnimationHapticPattern")):
        return "素材変換"

    if function_name.startswith(("Get", "Show")):
        return "デバッグ / 状態取得"

    if function_name.startswith("SetDraw"):
        return "描画受け渡し"

    if source_category == "振動専用クラス":
        return "振動専用クラス"

    if source_category in DOMAIN_ORDER:
        return source_category

    return "その他"


def infer_role(function_name: str) -> str:
    """関数名から、左ナビ内の下位分類を決める。"""

    if function_name in {"Init", "Update", "Draw", "Shutdown"}:
        return "管理"

    if function_name.startswith(("Set", "Load")):
        return "準備"

    if function_name.startswith(("Analyze", "Make")):
        return "計算"

    if function_name.startswith("Get"):
        return "状態取得"

    if function_name.startswith("Show"):
        return "デバッグ"

    return "実行"


def is_user_facing_function(function_name: str) -> bool:
    """メインの API リファレンスへ載せる関数だけを通す。"""

    if function_name in {"Init", "Update", "Draw", "Shutdown", "LoadEffect"}:
        return True

    function_role = infer_role(function_name)
    return function_role == "実行"


def is_hidden_reference_heading(heading_name: str) -> bool:
    """メインのリファレンスから外す章見出しかを返す。"""

    hidden_keywords = ("callback", "request", "provider", "query")
    normalized_heading = heading_name.lower()
    return any(keyword in normalized_heading for keyword in hidden_keywords)


def strip_inline_markdown(text: str) -> str:
    """ナビ説明用に Markdown 記号を落とす。"""

    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def normalize_summary_phrasing(text: str) -> str:
    """左ナビ用に、実装寄りの言い回しを利用者向けへ丸める。"""

    normalized_text = text
    normalized_text = re.sub(r"([^\s]+)\s+callback\s+を登録", r"\1の連携先を設定", normalized_text)
    normalized_text = re.sub(r"callback\s+を登録", "連携先を設定", normalized_text)
    normalized_text = re.sub(r"callback登録", "連携先を設定", normalized_text)
    normalized_text = re.sub(r"callback\s+経由で要求", "再生", normalized_text)
    normalized_text = re.sub(r"callback\s+経由で", "", normalized_text)
    normalized_text = re.sub(r"描画\s+callback", "描画先", normalized_text)
    normalized_text = re.sub(r"再生\s+callback", "再生先", normalized_text)
    normalized_text = re.sub(r"request\s+を実行", "を再生", normalized_text)
    normalized_text = re.sub(r"request式", "詳細指定", normalized_text)
    normalized_text = normalized_text.replace("frame provider", "フレーム情報")
    normalized_text = normalized_text.replace("provider を登録", "読み込み元を設定")
    normalized_text = normalized_text.replace("request再生", "再生")
    normalized_text = normalized_text.replace("request", "")
    normalized_text = normalized_text.replace("callback", "連携")
    normalized_text = normalized_text.replace("backend", "連携先")
    normalized_text = normalized_text.replace("再生を要求", "再生")
    normalized_text = normalized_text.replace("停止を要求", "停止")
    normalized_text = normalized_text.replace("切り替えを要求", "切り替え")

    normalized_text = re.sub(r"\s+", " ", normalized_text)
    normalized_text = normalized_text.replace("  ", " ").strip()
    return normalized_text


def shorten_summary(text: str, max_length: int = 34) -> str:
    """左ナビに入る長さへ概要文を短くする。"""

    summary = normalize_summary_phrasing(strip_inline_markdown(text))

    for suffix in ["します。", "です。", "ます。", "します", "です", "ます"]:
        if summary.endswith(suffix):
            summary = summary[: -len(suffix)]
            break

    if len(summary) <= max_length:
        return summary

    return summary[: max_length - 1] + "…"


def summarize_function(function_name: str, source_text: str) -> str:
    """関数名ごとの補正を含めて左ナビ概要を作る。"""

    if function_name in NAV_SUMMARY_OVERRIDES:
        return NAV_SUMMARY_OVERRIDES[function_name]

    return shorten_summary(source_text)


def extract_function_summaries(markdown_lines: list[str], public_function_names: set[str]) -> dict[str, str]:
    """各関数の「概略」欄から、左ナビ用の短い説明を作る。"""

    summaries: dict[str, str] = {}
    current_function = ""
    is_reading_overview = False
    overview_lines: list[str] = []

    def flush_overview() -> None:
        nonlocal overview_lines, current_function

        if not current_function or not overview_lines:
            overview_lines = []
            return

        overview_text = " ".join(line.strip() for line in overview_lines if line.strip())

        if overview_text:
            summaries[current_function] = summarize_function(current_function, overview_text)

        overview_lines = []

    for line in markdown_lines:
        if line.startswith("## "):
            flush_overview()
            heading = line[3:].strip()
            current_function = heading if heading in public_function_names else ""
            is_reading_overview = False
            continue

        if not current_function:
            continue

        if line.startswith("### "):
            if is_reading_overview:
                flush_overview()

            is_reading_overview = line[4:].strip() == "概略"
            continue

        if is_reading_overview:
            overview_lines.append(line)

    flush_overview()
    return summaries


def close_paragraph(html_lines: list[str], paragraph_lines: list[str]) -> None:
    """段落を閉じる。"""

    if not paragraph_lines:
        return

    paragraph_text = " ".join(line.strip() for line in paragraph_lines if line.strip())
    html_lines.append(f"<p>{inline_markup(paragraph_text)}</p>")
    paragraph_lines.clear()


def close_list(html_lines: list[str], is_list_open: bool) -> bool:
    """リストを閉じる。"""

    if is_list_open:
        html_lines.append("</ul>")

    return False


def build_navigation(markdown_lines: list[str], public_function_names: set[str]) -> list[dict[str, str]]:
    """左ナビ用のカテゴリと関数一覧を作る。"""

    used_slugs: set[str] = set()
    current_category = "Overview"
    nav_items: list[dict[str, str]] = []
    function_summaries = extract_function_summaries(markdown_lines, public_function_names)

    for line in markdown_lines:
        if line.startswith("# "):
            heading = line[2:].strip()

            if heading != "FeelKit API Reference":
                current_category = translate_category(heading)

            continue

        if not line.startswith("## "):
            continue

        heading = line[3:].strip()

        if heading in public_function_names:
            if not is_user_facing_function(heading):
                continue

            category_name = infer_domain(heading, current_category)

            if category_name in {"デバッグ / 状態取得", "振動専用クラス", "その他"}:
                continue

            summary = function_summaries.get(heading, "関数")
            nav_items.append(
                {
                    "title": heading,
                    "category": category_name,
                    "role": infer_role(heading),
                    "summary": summary,
                    "slug": make_slug(heading, used_slugs),
                }
            )
        else:
            current_category = translate_category(heading)

    return nav_items


def render_nav(nav_items: list[dict[str, str]]) -> str:
    """左ナビ HTML を生成する。"""

    grouped_items: dict[str, dict[str, list[dict[str, str]]]] = {}

    for item in nav_items:
        category_name = item["category"]
        role_name = item.get("role", "実行")
        grouped_items.setdefault(category_name, {}).setdefault(role_name, []).append(item)

    blocks: list[str] = []

    ordered_categories = sorted(
        grouped_items.keys(),
        key=lambda category: (
            DOMAIN_ORDER.index(category) if category in DOMAIN_ORDER else len(DOMAIN_ORDER),
            category,
        ),
    )

    for category in ordered_categories:
        role_groups = grouped_items[category]
        item_count = sum(len(items) for items in role_groups.values())
        safe_category = html.escape(category)
        blocks.append('<details class="nav-group">')
        blocks.append(f"<summary>{safe_category}<span>{item_count}</span></summary>")
        blocks.append('<div class="nav-group-list">')

        ordered_roles = sorted(
            role_groups.keys(),
            key=lambda role: (ROLE_ORDER.index(role) if role in ROLE_ORDER else len(ROLE_ORDER), role),
        )

        for role in ordered_roles:
            blocks.append('<section class="nav-role-section">')
            blocks.append(f'<div class="nav-role-title">{html.escape(role)}</div>')

            for item in role_groups[role]:
                safe_title = html.escape(item["title"])
                safe_summary = html.escape(item["summary"])
                safe_slug = html.escape(item["slug"])
                safe_keywords = html.escape(
                    f'{item["title"]} {item["category"]} {item["role"]} {item["summary"]}'.lower()
                )
                blocks.append(
                    f'<a class="nav-item" href="#{safe_slug}" '
                    f'data-keywords="{safe_keywords}" title="{safe_title}: {safe_summary}">'
                    f'<span class="nav-name">{safe_title}</span>'
                    f'<span class="nav-desc">{safe_summary}</span>'
                    f"</a>"
                )

            blocks.append("</section>")

        blocks.append("</div>")
        blocks.append("</details>")

    return "\n".join(blocks)


def build_slug_lookup(nav_items: list[dict[str, str]]) -> dict[str, str]:
    """関数名から最初の見出し id を引ける辞書を作る。"""

    slug_lookup: dict[str, str] = {}

    for item in nav_items:
        slug_lookup.setdefault(item["title"], item["slug"])

    return slug_lookup


def render_related_links(
    function_name: str,
    slug_lookup: dict[str, str],
    visible_function_names: set[str],
) -> str:
    """関数カードに表示する関連関数リンクを作る。"""

    related_items = RELATED_FUNCTIONS.get(function_name, [])

    if not related_items:
        return ""

    chips: list[str] = []

    for target_name, relation_note in related_items:
        if target_name not in visible_function_names:
            continue

        target_slug = slug_lookup.get(target_name)
        safe_name = html.escape(target_name)
        safe_note = html.escape(relation_note)

        if target_slug:
            safe_slug = html.escape(target_slug)
            chips.append(
                f'<a class="related-chip" href="#{safe_slug}">'
                f'<span class="related-name">{safe_name}</span>'
                f'<span class="related-note">{safe_note}</span>'
                f"</a>"
            )
        else:
            chips.append(
                f'<span class="related-chip related-chip-missing">'
                f'<span class="related-name">{safe_name}</span>'
                f'<span class="related-note">{safe_note}</span>'
                f"</span>"
            )

    if not chips:
        return ""

    return (
        '<div class="related-links">'
        '<span class="related-label">関連関数</span>'
        '<div class="related-chip-list">'
        + "".join(chips)
        + "</div>"
        + "</div>"
    )


def render_markdown(markdown_lines: list[str], nav_items: list[dict[str, str]], public_function_names: set[str]) -> str:
    """Markdown 本文を HTML 本文へ変換する。"""

    used_slugs: set[str] = set()
    function_slug_queue: dict[str, list[str]] = {}
    slug_lookup = build_slug_lookup(nav_items)
    visible_function_names = {item["title"] for item in nav_items}
    should_skip_hidden_chapter = False
    should_skip_hidden_function = False

    for item in nav_items:
        function_slug_queue.setdefault(item["title"], []).append(item["slug"])

    html_lines: list[str] = []
    paragraph_lines: list[str] = []
    code_lines: list[str] = []
    is_code_open = False
    is_list_open = False
    is_card_open = False

    def close_card() -> None:
        nonlocal is_card_open

        if is_card_open:
            html_lines.append("</section>")
            is_card_open = False

    def skip_hidden_line(line: str) -> bool:
        nonlocal is_code_open

        if should_skip_hidden_chapter:
            if line.startswith("# "):
                return False

            return True

        if not should_skip_hidden_function:
            return False

        if line.startswith("```"):
            if is_code_open:
                code_lines.clear()
                is_code_open = False
            else:
                is_code_open = True

            return True

        if is_code_open:
            return True

        if line.startswith("## "):
            return False

        return True

    for line in markdown_lines:
        if skip_hidden_line(line):
            continue

        # ------------------------------------------------------------
        # コードブロック
        # ------------------------------------------------------------

        if line.startswith("```"):

            if is_code_open:
                escaped_code = html.escape("\n".join(code_lines))
                html_lines.append('<div class="code-block">')
                html_lines.append('<button class="copy-button" type="button">Copy</button>')
                html_lines.append(f"<pre><code>{escaped_code}</code></pre>")
                html_lines.append("</div>")
                code_lines.clear()
                is_code_open = False
            else:
                close_paragraph(html_lines, paragraph_lines)
                is_list_open = close_list(html_lines, is_list_open)
                is_code_open = True

            continue

        if is_code_open:
            code_lines.append(line)
            continue

        # ------------------------------------------------------------
        # 見出し
        # ------------------------------------------------------------

        if line.startswith("# "):
            close_paragraph(html_lines, paragraph_lines)
            is_list_open = close_list(html_lines, is_list_open)
            close_card()
            should_skip_hidden_chapter = False
            should_skip_hidden_function = False

            heading = line[2:].strip()

            if heading == "FeelKit API Reference":
                continue

            if is_hidden_reference_heading(heading):
                should_skip_hidden_chapter = True
                continue

            slug = make_slug(heading, used_slugs)
            html_lines.append(f'<section class="chapter" id="{slug}">')
            html_lines.append(f"<h2>{inline_markup(heading)}</h2>")
            html_lines.append("</section>")
            continue

        if line.startswith("## "):
            close_paragraph(html_lines, paragraph_lines)
            is_list_open = close_list(html_lines, is_list_open)
            close_card()

            heading = line[3:].strip()

            if heading in public_function_names and heading not in visible_function_names:
                should_skip_hidden_function = True
                continue

            should_skip_hidden_function = False

            if heading in public_function_names and function_slug_queue.get(heading):
                slug = function_slug_queue[heading].pop(0)
                safe_keywords = html.escape(heading.lower())
                html_lines.append(
                    f'<section class="api-card searchable-section" id="{slug}" '
                    f'data-keywords="{safe_keywords}">'
                )
                html_lines.append('<div class="api-card-head">')
                html_lines.append(f"<h2>{inline_markup(heading)}</h2>")
                html_lines.append(f'<a class="anchor-link" href="#{slug}">#</a>')
                html_lines.append("</div>")
                related_html = render_related_links(heading, slug_lookup, visible_function_names)

                if related_html:
                    html_lines.append(related_html)

                is_card_open = True
            else:
                if is_hidden_reference_heading(heading):
                    should_skip_hidden_function = True
                    continue

                slug = make_slug(heading, used_slugs)
                html_lines.append(f'<h2 class="chapter-subtitle" id="{slug}">{inline_markup(heading)}</h2>')

            continue

        if should_skip_hidden_function:
            continue

        if line.startswith("### "):
            close_paragraph(html_lines, paragraph_lines)
            is_list_open = close_list(html_lines, is_list_open)
            heading = line[4:].strip()
            html_lines.append(f'<h3 class="api-subheading">{inline_markup(heading)}</h3>')
            continue

        # ------------------------------------------------------------
        # リスト
        # ------------------------------------------------------------

        stripped_line = line.strip()

        if stripped_line.startswith("- "):
            close_paragraph(html_lines, paragraph_lines)

            if not is_list_open:
                html_lines.append("<ul>")
                is_list_open = True

            item_text = stripped_line[2:].strip()
            html_lines.append(f"<li>{inline_markup(item_text)}</li>")
            continue

        # ------------------------------------------------------------
        # 通常段落
        # ------------------------------------------------------------

        if not stripped_line:
            close_paragraph(html_lines, paragraph_lines)
            is_list_open = close_list(html_lines, is_list_open)
            continue

        paragraph_lines.append(line)

    close_paragraph(html_lines, paragraph_lines)
    close_list(html_lines, is_list_open)
    close_card()

    return "\n".join(html_lines)


# ================================================================
# HTML テンプレート
# ================================================================

def build_html(nav_html: str, content_html: str, api_count: int) -> str:
    """完成 HTML を組み立てる。"""

    return f"""<!doctype html>
<html lang="ja">
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<title>FeelKit API Reference</title>
	<link rel="stylesheet" href="docs.css">
</head>
<body>
	<header class="topbar">
		<div class="brand">
			<a href="index.html" class="brand-mark">FK</a>
			<div>
				<p class="eyebrow">FeelKit Documentation</p>
				<h1>API Reference</h1>
			</div>
		</div>
		<div class="top-actions">
			<a class="ghost-button" href="index.html">Home</a>
			<button class="ghost-button" type="button" id="themeToggle">Theme</button>
			<button class="menu-button" type="button" id="menuToggle">Menu</button>
		</div>
	</header>

	<div class="layout">
		<aside class="sidebar" id="sidebar">
			<div class="search-panel">
				<label for="apiSearch">関数検索</label>
				<div class="search-row">
					<input id="apiSearch" type="search" placeholder="PlayEffect / 音 / Shake">
					<button type="button" id="clearSearch">Clear</button>
				</div>
				<p class="search-count"><span id="visibleCount">{api_count}</span> / {api_count} 関数</p>
			</div>
			<nav class="nav-list" aria-label="API navigation">
{nav_html}
			</nav>
		</aside>

		<main class="content">
			<section class="hero small-hero">
				<p class="eyebrow">Generated from API_REFERENCE.md</p>
				<h2>クリックして飛べる、検索できる、コードをコピーできるリファレンス</h2>
				<p>左の検索で関数を絞り込み、関数名クリックで該当項目へ移動できます。Markdown は引き継ぎ用、この HTML は人間が読む用です。</p>
				<div class="hero-actions">
					<a class="primary-button" href="#playeffect">PlayEffectを見る</a>
					<a class="secondary-button" href="https://www.youtube.com/results?search_query=game+feel+screen+shake+haptics" target="_blank" rel="noreferrer">YouTube参考検索</a>
				</div>
			</section>

			<div class="reference-body">
{content_html}
			</div>
		</main>
	</div>

	<button class="back-to-top" type="button" id="backToTop">↑</button>
	<script src="docs.js"></script>
</body>
</html>
"""


def sanitize_reference_html(html_text: str) -> str:
    """表示用 HTML の説明文を分かりやすい日本語へ寄せる。"""

    replacements = [
        ("描画 callback へ、現在の演出状態を流します。", "描画先へ、現在の演出状態を渡します。"),
        ("本実装。描画自体ではなく callback 通知を行います。", "本実装。描画そのものは行わず、描画先へ状態を渡します。"),
        ("SE 再生用 callback を登録します。", "SE の再生先を設定します。"),
        ("この callback を通してゲーム側の音声システムを呼びます。", "この再生先を通してゲーム側の音声システムを呼びます。"),
        ("callback を内部 state に保存します。", "連携先を内部 state に保存します。"),
        ("で登録した callback を呼びます。", "で設定した連携先を呼びます。"),
        ("callback が未登録の場合は何もしません。", "連携先が未設定の場合は何もしません。"),
        ("callback 経由でゲーム側の音再生に接続します。", "ゲーム側の音再生先と接続します。"),
        ("BGM 再生を要求します。", "BGM を再生します。"),
        ("ループ再生を要求します", "ループ再生します"),
        ("音声再生そのものはゲーム側 callback に任せます。", "音声再生そのものはゲーム側の再生先に任せます。"),
        ("BGM 停止を要求します。", "BGM を停止します。"),
        ("<code>SetSoundEffectCallback()</code> で設定した連携先を呼びます。 連携先が未設定の場合は何もしません。", "設定済みの SE 再生先を呼びます。 未設定の場合は何もしません。"),
        ("<code>SetBgmCallback()</code> で設定した連携先を呼びます。", "設定済みの BGM 再生先を呼びます。"),
        ("<code>SetStopBgmCallback()</code> で設定した連携先を呼びます。", "設定済みの BGM 停止先を呼びます。"),
        ("<code>SetHapticsBackend()</code> が登録されていれば <code>FeelKitHaptics</code> へ渡します。 未登録の場合は <code>SetGamepadVibrationCallback()</code> の callback を呼びます。", "設定済みの振動連携先へゲームパッド振動を渡します。 連携先が無い場合は再生しません。"),
        ("<code>GetScreenState()</code> で取得できる <code>shakeOffset</code> と <code>shakeRotation</code> に反映されます。 実際のカメラ適用はゲーム側で行います。", "現在の画面揺れ状態に反映されます。 実際のカメラ適用はゲーム側で行います。"),
        ("<code>GetScreenState()</code> の <code>flashColor</code>、<code>flashAlpha</code>、<code>flashStrength</code> に反映されます。", "現在のフラッシュ状態に反映されます。"),
        ("<code>SetEffectDrawCallback()</code>、<code>SetTextDrawCallback()</code>、<code>SetFragmentDrawCallback()</code>、<code>SetScreenEffectDrawCallback()</code> を登録している場合に、それぞれの描画情報をゲーム側へ渡します。 実際の描画はゲーム側で行います。", "設定済みの描画先へ現在の演出情報を渡します。 実際の描画はゲーム側で行います。"),
        ("HD Rumble 再生を callback 経由で要求します。", "HD Rumble を再生します。"),
        ("DualSense ハプティクス再生を callback 経由で要求します。", "DualSense ハプティクスを再生します。"),
        ("左右トリガー抵抗の再生を callback 経由で要求します。", "左右トリガー抵抗を再生します。"),
        ("callback を呼べた", "連携先を呼べた"),
        ("callback 未登録", "連携先未設定"),
        ("標準 backend はまだ持たず、ゲーム側 callback に処理を渡します。", "標準 backend はまだ持たず、ゲーム側の連携先へ処理を渡します。"),
        ("PC 向け DualSense backend はライブラリ内に固定せず、ゲーム側 callback に任せます。", "PC 向け DualSense backend はライブラリ内に固定せず、ゲーム側の連携先に任せます。"),
        ("callback 接続実装。", "連携先接続実装。"),
        ("effect 描画 callback を登録します。", "effect の描画先を設定します。"),
        ("現在描画すべき frame 情報を callback に渡します。", "現在描画すべき frame 情報を描画先へ渡します。"),
        ("`Draw()` や callback でゲーム側へ渡されます。", "`Draw()` や描画先を通してゲーム側へ渡されます。"),
        ("デバッグ出力 callback を登録します。", "デバッグ出力先を設定します。"),
        ("文字列を受け取る callback", "文字列を受け取る出力先"),
        ("GUI ではなく callback / log 出力です。", "GUI ではなく、設定した出力先または log へ出します。"),
        ("callback に流し、未登録なら console へ出力します。", "設定済みの出力先へ流し、未設定なら console へ出力します。"),
        ("描画 callback を使わずに、ゲーム側で状態を直接参照したい場合に使います。", "描画先を使わずに、ゲーム側で状態を直接参照したい場合に使います。"),
        ("再生要求成功", "再生成功"),
        ("登録または再生要求成功", "設定または再生成功"),
        ("request / callback / 管理 API", "管理 API"),
        ("callback または provider 経由でゲーム側に渡します。", "ゲーム側の連携先やデータ供給元へ渡します。"),
        ("SE 再生 request を実行します。", "SE を再生します。"),
        ("BGM 切り替え request を実行します。", "BGM を切り替えます。"),
        ("位置つき 3D 音響 request を実行します。", "位置つき 3D 音響を再生します。"),
        ("ボイス再生 request を実行します。", "ボイスを再生します。"),
        ("画面揺れ request を実行します。", "画面揺れを再生します。"),
        ("ズーム request を実行します。", "ズームを再生します。"),
        ("hit stop request を実行します。", "hit stop を再生します。"),
        ("flash request を実行します。", "flash を再生します。"),
        ("画面色変化 request を実行します。", "画面色変化を再生します。"),
        ("slow motion request を実行します。", "slow motion を再生します。"),
        ("爆発 effect request を実行します。", "爆発 effect を再生します。"),
        ("火花 effect request を実行します。", "火花 effect を再生します。"),
        ("煙 effect request を実行します。", "煙 effect を再生します。"),
        ("着弾 effect request を実行します。", "着弾 effect を再生します。"),
        ("particle request を実行します。", "particle を再生します。"),
        ("particle request再生", "particle 再生"),
        ("request式SE再生", "詳細指定でSE再生"),
        ("request式で再生", "詳細指定で再生"),
        ("request式", "詳細指定"),
        ("callback登録", "連携先設定"),
        ("callback式の音振動", "音から振動"),
        ("callbackで粒子再生", "粒子を再生"),
        ("FeelKit::ScreenState state = FeelKit::GetScreenState();\ncamera.x += state.shakeOffset.x;\ncamera.y += state.shakeOffset.y;", "// Update() 後にゲーム側のカメラへ揺れを反映します"),
        ("<h2>音 callback</h2>", "<h2>音の連携設定</h2>"),
        ("<h2>音解析 / 再生 request</h2>", "<h2>音の詳細再生</h2>"),
        ("<h2>振動 callback</h2>", "<h2>振動の連携設定</h2>"),
        ("<h2>画面演出 callback</h2>", "<h2>画面演出の連携設定</h2>"),
        ("<h2>画面演出 request</h2>", "<h2>画面演出の詳細再生</h2>"),
        ("<h2>エフェクト callback</h2>", "<h2>エフェクトの連携設定</h2>"),
        ("<h2>エフェクト request</h2>", "<h2>エフェクトの詳細再生</h2>"),
        ("<h2>記録 / 描画 callback</h2>", "<h2>記録 / 描画の連携設定</h2>"),
    ]

    sanitized_text = html_text

    for source_text, target_text in replacements:
        sanitized_text = sanitized_text.replace(source_text, target_text)

    sanitized_text = re.sub(
        r"<p><code>SetSoundEffectCallback\(\)</code>.*?</p>",
        "<p>設定済みの SE 再生先を呼びます。 未設定の場合は何もしません。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>SetBgmCallback\(\)</code>.*?</p>",
        "<p>設定済みの BGM 再生先を呼びます。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>SetStopBgmCallback\(\)</code>.*?</p>",
        "<p>設定済みの BGM 停止先を呼びます。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>SetHapticsBackend\(\)</code>.*?</p>",
        "<p>設定済みの振動連携先へゲームパッド振動を渡します。 連携先が無い場合は再生しません。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>GetScreenState\(\)</code>.*?実際のカメラ適用はゲーム側で行います。</p>",
        "<p>現在の画面揺れ状態に反映されます。 実際のカメラ適用はゲーム側で行います。</p>",
        sanitized_text,
    )
    sanitized_text = sanitized_text.replace(
        "FeelKit::ScreenState state = FeelKit::GetScreenState();\ncamera.x += state.shakeOffset.x;\ncamera.y += state.shakeOffset.y;",
        "// Update() 後にゲーム側のカメラへ揺れを反映します",
    )
    sanitized_text = re.sub(
        r"<p><code>GetScreenState\(\)</code> の <code>flashColor</code>.*?</p>",
        "<p>現在のフラッシュ状態に反映されます。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>GetScreenState\(\)</code> の <code>zoomScale</code> に反映されます。</p>",
        "<p>現在のズーム状態に反映されます。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>GetScreenState\(\)\.isHitStop</code> で現在状態を確認します。 ゲーム更新停止そのものはゲーム側で行います。</p>",
        "<p>現在のヒットストップ状態を使って、ゲーム更新停止はゲーム側で行います。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"if \(FeelKit::GetScreenState\(\)\.isHitStop\) \{\s*return;\s*\}",
        "// ゲーム側でヒットストップ中の更新停止を行います",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>GetScreenState\(\)\.slowMotionScale</code> をゲーム側の time scale に反映して使います。</p>",
        "<p>現在のスローモーション状態をゲーム側の time scale に反映して使います。</p>",
        sanitized_text,
    )
    sanitized_text = re.sub(
        r"<p><code>SetEffectDrawCallback\(\)</code>、<code>SetTextDrawCallback\(\)</code>、<code>SetFragmentDrawCallback\(\)</code>、<code>SetScreenEffectDrawCallback\(\)</code>.*?</p>",
        "<p>設定済みの描画先へ現在の演出情報を渡します。 実際の描画はゲーム側で行います。</p>",
        sanitized_text,
    )

    return sanitized_text


def main() -> None:
    """HTML リファレンスを生成する。"""

    markdown_lines = REFERENCE_PATH.read_text(encoding="utf-8-sig").splitlines()
    public_function_names = collect_public_function_names()
    nav_items = build_navigation(markdown_lines, public_function_names)

    nav_html = render_nav(nav_items)
    content_html = render_markdown(markdown_lines, nav_items, public_function_names)
    html_text = build_html(nav_html, content_html, len(nav_items))
    html_text = sanitize_reference_html(html_text)

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(html_text, encoding="utf-8-sig")

    print(f"generated: {OUTPUT_PATH}")
    print(f"functions: {len(nav_items)}")


if __name__ == "__main__":
    main()
