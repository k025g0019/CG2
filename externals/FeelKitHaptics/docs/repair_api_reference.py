#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
API_REFERENCE.md の補完章を再生成するためのメンテナンススクリプト。

目的:
    - 文字化けした補完章を破棄する
    - 公開ヘッダから宣言を取り直す
    - 関数ごとに「何の関数か」を日本語で残す
"""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
REFERENCE_PATH = ROOT_DIR / "API_REFERENCE.md"

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

ORDERED_FUNCTIONS = [
    "SetBgmCallback",
    "SetStopBgmCallback",
    "SetBgmSwitchCallback",
    "SetSpatialSoundCallback",
    "SetVoiceCallback",
    "LoadWaveform",
    "MakeSlowMotionFromAudio",
    "PlaySoundEffect",
    "PlayBgmSwitch",
    "PlaySpatialSound",
    "PlayVoice",
    "GetMasterVolume",
    "SpawnEffectFromAudio",
    "CreateLightFromAudio",
    "CreateCameraMotionFromAudio",
    "CreateSlowMotionFromAudio",
    "SetVibrationCallback",
    "SetVibrationPatternCallback",
    "SetVibrationFromSoundCallback",
    "SetGamepadVibrationCallback",
    "SetHdRumbleCallback",
    "SetDualSenseHapticsCallback",
    "SetTriggerFeedbackCallback",
    "VibratePattern",
    "VibrateFromSound",
    "SetScreenShakeCallback",
    "SetScreenZoomCallback",
    "SetHitStopFrameCallback",
    "SetScreenFlashCallback",
    "SetScreenColorChangeCallback",
    "SetSlowMotionFrameCallback",
    "PlayScreenShake",
    "PlayScreenZoom",
    "PlayHitStop",
    "PlayScreenFlash",
    "PlayScreenColorChange",
    "PlaySlowMotion",
    "SetExplosionCallback",
    "SetSparkCallback",
    "SetSmokeCallback",
    "SetImpactEffectCallback",
    "SetParticleCallback",
    "PlayExplosionRequest",
    "PlaySpark",
    "PlaySmoke",
    "PlayImpactEffect",
    "PlayParticle",
    "EffectPoolGetActiveCount",
    "EffectPoolKillAll",
    "EffectPoolKill",
    "EffectPoolIsAlive",
    "ReleaseTrail",
    "ReleaseAfterImage",
    "StopEmit",
    "AddScreenEffect",
    "GenerateImpactHaptics",
    "GenerateImpactShake",
    "SetTextureDataProvider",
    "SetGifDataProvider",
    "SetAnimationHapticPattern",
    "PlaySequence",
    "Timeline",
    "TimelineAddEvent",
    "TimelinePlay",
    "TimelineStop",
    "StartEffectRecording",
    "ReplayEffect",
    "SetTextDrawCallback",
    "SetFragmentDrawCallback",
    "SetScreenEffectDrawCallback",
    "GetOcclusionFactor",
    "SetOcclusionFactor",
    "GetTextEffectCount",
    "GetFragmentCount",
    "GetScreenEffectCount",
    "GetTrailCount",
    "GetTrailHandle",
    "shutdown",
    "update",
    "refreshDevice",
    "getDeviceState",
    "setEnabled",
    "isVibrationEnabled",
    "isReady",
    "setMasterScale",
    "getMasterScale",
    "clearSoundVibrations",
    "vibrateSound",
    "stop",
]

CATEGORY_BY_NAME = {
    "SetBgmCallback": "音 callback",
    "SetStopBgmCallback": "音 callback",
    "SetBgmSwitchCallback": "音 callback",
    "SetSpatialSoundCallback": "音 callback",
    "SetVoiceCallback": "音 callback",
    "LoadWaveform": "音解析 / 再生 request",
    "MakeSlowMotionFromAudio": "音解析 / 再生 request",
    "PlaySoundEffect": "音解析 / 再生 request",
    "PlayBgmSwitch": "音解析 / 再生 request",
    "PlaySpatialSound": "音解析 / 再生 request",
    "PlayVoice": "音解析 / 再生 request",
    "GetMasterVolume": "音解析 / 再生 request",
    "SpawnEffectFromAudio": "音から演出生成",
    "CreateLightFromAudio": "音から演出生成",
    "CreateCameraMotionFromAudio": "音から演出生成",
    "CreateSlowMotionFromAudio": "音から演出生成",
    "SetVibrationCallback": "振動 callback",
    "SetVibrationPatternCallback": "振動 callback",
    "SetVibrationFromSoundCallback": "振動 callback",
    "SetGamepadVibrationCallback": "振動 callback",
    "SetHdRumbleCallback": "振動 callback",
    "SetDualSenseHapticsCallback": "振動 callback",
    "SetTriggerFeedbackCallback": "振動 callback",
    "VibratePattern": "振動直接実行",
    "VibrateFromSound": "振動直接実行",
    "SetScreenShakeCallback": "画面演出 callback",
    "SetScreenZoomCallback": "画面演出 callback",
    "SetHitStopFrameCallback": "画面演出 callback",
    "SetScreenFlashCallback": "画面演出 callback",
    "SetScreenColorChangeCallback": "画面演出 callback",
    "SetSlowMotionFrameCallback": "画面演出 callback",
    "PlayScreenShake": "画面演出 request",
    "PlayScreenZoom": "画面演出 request",
    "PlayHitStop": "画面演出 request",
    "PlayScreenFlash": "画面演出 request",
    "PlayScreenColorChange": "画面演出 request",
    "PlaySlowMotion": "画面演出 request",
    "SetExplosionCallback": "エフェクト callback",
    "SetSparkCallback": "エフェクト callback",
    "SetSmokeCallback": "エフェクト callback",
    "SetImpactEffectCallback": "エフェクト callback",
    "SetParticleCallback": "エフェクト callback",
    "PlayExplosionRequest": "エフェクト request",
    "PlaySpark": "エフェクト request",
    "PlaySmoke": "エフェクト request",
    "PlayImpactEffect": "エフェクト request",
    "PlayParticle": "エフェクト request",
    "EffectPoolGetActiveCount": "エフェクト pool",
    "EffectPoolKillAll": "エフェクト pool",
    "EffectPoolKill": "エフェクト pool",
    "EffectPoolIsAlive": "エフェクト pool",
    "ReleaseTrail": "軌跡 / 残像 / emitter",
    "ReleaseAfterImage": "軌跡 / 残像 / emitter",
    "StopEmit": "軌跡 / 残像 / emitter",
    "AddScreenEffect": "軌跡 / 残像 / emitter",
    "GenerateImpactHaptics": "自動生成 helper",
    "GenerateImpactShake": "自動生成 helper",
    "SetTextureDataProvider": "素材 provider",
    "SetGifDataProvider": "素材 provider",
    "SetAnimationHapticPattern": "素材 provider",
    "PlaySequence": "シーケンス / timeline",
    "Timeline": "シーケンス / timeline",
    "TimelineAddEvent": "シーケンス / timeline",
    "TimelinePlay": "シーケンス / timeline",
    "TimelineStop": "シーケンス / timeline",
    "StartEffectRecording": "記録 / 描画 callback",
    "ReplayEffect": "記録 / 描画 callback",
    "SetTextDrawCallback": "記録 / 描画 callback",
    "SetFragmentDrawCallback": "記録 / 描画 callback",
    "SetScreenEffectDrawCallback": "記録 / 描画 callback",
    "GetOcclusionFactor": "状態取得",
    "SetOcclusionFactor": "状態取得",
    "GetTextEffectCount": "状態取得",
    "GetFragmentCount": "状態取得",
    "GetScreenEffectCount": "状態取得",
    "GetTrailCount": "状態取得",
    "GetTrailHandle": "状態取得",
    "shutdown": "振動専用 member",
    "update": "振動専用 member",
    "refreshDevice": "振動専用 member",
    "getDeviceState": "振動専用 member",
    "setEnabled": "振動専用 member",
    "isVibrationEnabled": "振動専用 member",
    "isReady": "振動専用 member",
    "setMasterScale": "振動専用 member",
    "getMasterScale": "振動専用 member",
    "clearSoundVibrations": "振動専用 member",
    "vibrateSound": "振動専用 member",
    "stop": "振動専用 member",
}

PURPOSE_BY_NAME = {
    "SetBgmCallback": "BGM 再生 callback を登録します。",
    "SetStopBgmCallback": "BGM 停止 callback を登録します。",
    "SetBgmSwitchCallback": "BGM 切り替え callback を登録します。",
    "SetSpatialSoundCallback": "3D 音響 callback を登録します。",
    "SetVoiceCallback": "ボイス再生 callback を登録します。",
    "LoadWaveform": "波形データを登録して handle を作ります。",
    "MakeSlowMotionFromAudio": "音声特徴量からスローモーション設定を作ります。",
    "PlaySoundEffect": "SE 再生 request を実行します。",
    "PlayBgmSwitch": "BGM 切り替え request を実行します。",
    "PlaySpatialSound": "位置つき 3D 音響 request を実行します。",
    "PlayVoice": "ボイス再生 request を実行します。",
    "GetMasterVolume": "現在の master volume を取得します。",
    "SpawnEffectFromAudio": "音の強さから effect 強度を作って発生させます。",
    "CreateLightFromAudio": "音からライト点滅演出を作ります。",
    "CreateCameraMotionFromAudio": "音からカメラ揺れやカメラ動作を作ります。",
    "CreateSlowMotionFromAudio": "音からスローモーション演出を作ります。",
    "SetVibrationCallback": "単純振動 callback を登録します。",
    "SetVibrationPatternCallback": "名前付き振動 pattern callback を登録します。",
    "SetVibrationFromSoundCallback": "音由来振動 callback を登録します。",
    "SetGamepadVibrationCallback": "gamepad 振動 callback を登録します。",
    "SetHdRumbleCallback": "HD Rumble callback を登録します。",
    "SetDualSenseHapticsCallback": "DualSense haptics callback を登録します。",
    "SetTriggerFeedbackCallback": "trigger feedback callback を登録します。",
    "VibratePattern": "名前付き pattern で振動します。",
    "VibrateFromSound": "音声ファイルから振動を作って再生します。",
    "SetScreenShakeCallback": "画面揺れ callback を登録します。",
    "SetScreenZoomCallback": "ズーム callback を登録します。",
    "SetHitStopFrameCallback": "hit stop callback を登録します。",
    "SetScreenFlashCallback": "flash callback を登録します。",
    "SetScreenColorChangeCallback": "画面色変化 callback を登録します。",
    "SetSlowMotionFrameCallback": "slow motion callback を登録します。",
    "PlayScreenShake": "画面揺れ request を実行します。",
    "PlayScreenZoom": "ズーム request を実行します。",
    "PlayHitStop": "hit stop request を実行します。",
    "PlayScreenFlash": "flash request を実行します。",
    "PlayScreenColorChange": "画面色変化 request を実行します。",
    "PlaySlowMotion": "slow motion request を実行します。",
    "SetExplosionCallback": "爆発 effect callback を登録します。",
    "SetSparkCallback": "火花 effect callback を登録します。",
    "SetSmokeCallback": "煙 effect callback を登録します。",
    "SetImpactEffectCallback": "着弾 effect callback を登録します。",
    "SetParticleCallback": "particle callback を登録します。",
    "PlayExplosionRequest": "爆発 effect request を実行します。",
    "PlaySpark": "火花 effect request を実行します。",
    "PlaySmoke": "煙 effect request を実行します。",
    "PlayImpactEffect": "着弾 effect request を実行します。",
    "PlayParticle": "particle request を実行します。",
    "EffectPoolGetActiveCount": "active effect 数を取得します。",
    "EffectPoolKillAll": "全 active effect を停止します。",
    "EffectPoolKill": "指定 effect を停止します。",
    "EffectPoolIsAlive": "指定 effect が再生中か確認します。",
    "ReleaseTrail": "trail handle を解放します。",
    "ReleaseAfterImage": "afterimage handle を解放します。",
    "StopEmit": "emitter handle を停止します。",
    "AddScreenEffect": "画面空間 effect を追加します。",
    "GenerateImpactHaptics": "衝突速度と質量から振動を自動生成します。",
    "GenerateImpactShake": "衝突速度と質量から画面揺れを自動生成します。",
    "SetTextureDataProvider": "texture 情報 provider を登録します。",
    "SetGifDataProvider": "GIF frame provider を登録します。",
    "SetAnimationHapticPattern": "animation 名と振動 pattern を登録します。",
    "PlaySequence": "複数 event の sequence を実行します。",
    "Timeline": "timeline handle を作ります。",
    "TimelineAddEvent": "timeline に event を追加します。",
    "TimelinePlay": "timeline を再生します。",
    "TimelineStop": "timeline を停止します。",
    "StartEffectRecording": "effect 発火履歴の記録を開始します。",
    "ReplayEffect": "記録した effect 発火履歴を再生します。",
    "SetTextDrawCallback": "text effect 描画 callback を登録します。",
    "SetFragmentDrawCallback": "fragment 描画 callback を登録します。",
    "SetScreenEffectDrawCallback": "screen effect 描画 callback を登録します。",
    "GetOcclusionFactor": "occlusion 係数を取得します。",
    "SetOcclusionFactor": "occlusion 係数を設定します。",
    "GetTextEffectCount": "active text effect 数を取得します。",
    "GetFragmentCount": "active fragment 数を取得します。",
    "GetScreenEffectCount": "active screen effect 数を取得します。",
    "GetTrailCount": "trail 数を取得します。",
    "GetTrailHandle": "index から trail handle を取得します。",
    "shutdown": "FeelKitHaptics を終了します。",
    "update": "FeelKitHaptics の振動停止タイマーを更新します。",
    "refreshDevice": "controller 接続状態を再取得します。",
    "getDeviceState": "controller の状態を取得します。",
    "setEnabled": "振動の有効 / 無効を切り替えます。",
    "isVibrationEnabled": "振動が有効か確認します。",
    "isReady": "振動 device が使用可能か確認します。",
    "setMasterScale": "振動全体の倍率を設定します。",
    "getMasterScale": "振動全体の倍率を取得します。",
    "clearSoundVibrations": "音名振動の登録を全消去します。",
    "vibrateSound": "音名に紐づく振動を再生します。",
    "stop": "現在の振動を停止します。",
}


def collect_declarations() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """ヘッダから宣言と宣言場所を取得する。"""

    declarations: dict[str, list[str]] = defaultdict(list)
    locations: dict[str, list[str]] = defaultdict(list)

    for header_path in HEADER_PATHS:
        lines = header_path.read_text(encoding="utf-8-sig").splitlines()
        index = 0

        while index < len(lines):
            line = lines[index]
            match = FUNCTION_DECLARATION_RE.match(line)

            if match is None:
                index += 1
                continue

            name = match.group(1)

            if name in INTERNAL_FUNCTION_NAMES:
                index += 1
                continue

            declaration_lines = [line.strip()]

            while ";" not in declaration_lines[-1] and index + 1 < len(lines):
                index += 1
                declaration_lines.append(lines[index].strip())

            declarations[name].append("\n".join(declaration_lines))
            locations[name].append(f"{header_path.relative_to(ROOT_DIR).as_posix()}:{index + 1}")
            index += 1

    declarations["PlayExplosionRequest"] = ["bool PlayExplosion(const ExplosionRequest& request);"]
    locations["PlayExplosionRequest"] = ["effects/FeelKitEffects.h:57"]
    return declarations, locations


def argument_text(name: str) -> str:
    """引数説明を作る。"""

    if name.startswith("Set") and "Provider" not in name and name not in {"SetAnimationHapticPattern", "SetOcclusionFactor"}:
        return "`callback` : ゲーム側の処理を受け取る関数オブジェクト。"

    if "Provider" in name:
        return "`provider` : 素材情報を FeelKit に渡すための関数オブジェクト。"

    if name in {"GenerateImpactHaptics", "GenerateImpactShake"}:
        return "- `velocity` : 衝突速度\n- `mass` : 衝突した物体の質量"

    if name in {"ReleaseTrail", "ReleaseAfterImage", "StopEmit", "TimelinePlay", "TimelineStop"}:
        return "`handle` : 対象 handle。"

    if name == "TimelineAddEvent":
        return "- `handle` : 対象 timeline\n- `timeSeconds` : event 発火秒数\n- `eventName` : event 名"

    if name == "PlaySequence":
        return "- `sequenceName` : 登録済み sequence 名\n- `events` : 直接渡す event 配列\n- `eventCount` : event 数"

    if name == "SetAnimationHapticPattern":
        return "- `animationName` : animation 名\n- `pattern` : 対応する振動 pattern"

    if name == "SetOcclusionFactor":
        return "`factor` : 遮蔽係数。小さいほど音や演出を弱くします。"

    if name == "vibrateSound":
        return "- `soundPath` : 音声パスまたは音名\n- `shouldVibrate` : false の場合は振動しない\n- `vibrationDesc` : その場で使う振動設定"

    if name.startswith("Play"):
        return "`request` または各引数で、再生する演出の種類、位置、強さ、時間を指定します。"

    return "なし"


def return_text(name: str) -> str:
    """戻り値説明を作る。"""

    if name.startswith("Get") or name in {"isReady", "isVibrationEnabled", "refreshDevice", "vibrateSound"}:
        return "現在値、状態、または成功可否を返します。"

    if name.startswith("Play") and name not in {"PlaySequence"}:
        return "- `true` : 実行成功\n- `false` : callback 未登録、引数不正、無効状態など"

    if name in {"LoadWaveform", "Timeline", "GetTrailHandle"}:
        return "作成または取得した handle を返します。"

    return "なし"


def sample_code(name: str) -> str:
    """サンプルコードを作る。"""

    if name == "Timeline":
        return "TimelineHandle timeline = FeelKit::Timeline();"

    if name == "SetBgmCallback":
        return "FeelKit::SetBgmCallback([](const char* path, bool loop) {\n\taudio.PlayBgm(path, loop);\n});"

    if name == "PlaySoundEffect":
        return "FeelKit::SoundEffectRequest request;\nrequest.path = \"hit.wav\";\nrequest.volume = 1.0f;\nFeelKit::PlaySoundEffect(request);"

    if name == "SpawnEffectFromAudio":
        return "FeelKit::EffectDesc effectDesc;\nFeelKit::SpawnEffectFromAudio(effectDesc, \"explosion.wav\", FeelKit::Vec3{0.0f, 0.0f, 0.0f});"

    if name == "vibrateSound":
        return "haptics.vibrateSound(\"hit.wav\", true);"

    if name[0].islower():
        return f"haptics.{name}();"

    return f"FeelKit::{name}();"


def build_completion_section() -> str:
    """補完章を作る。"""

    declarations, locations = collect_declarations()
    lines: list[str] = []
    current_category = ""

    lines.append("# 公開 API 補完リファレンス")
    lines.append("")
    lines.append("この章は、上のカテゴリ別説明でまとめられていた request / callback / 管理 API を、関数単位で引けるように補完したものです。")
    lines.append("")

    for name in ORDERED_FUNCTIONS:
        category = CATEGORY_BY_NAME[name]

        if category != current_category:
            lines.append(f"# {category}")
            lines.append("")
            current_category = category

        display_name = "PlayExplosion" if name == "PlayExplosionRequest" else name
        lines.append(f"## {display_name}")
        lines.append("")
        lines.append("### 宣言")
        lines.append("")
        lines.append("```cpp")

        for declaration in declarations.get(name, []):
            lines.append(declaration)

        lines.append("```")
        lines.append("")
        lines.append("### 概略")
        lines.append("")
        lines.append(PURPOSE_BY_NAME[name])
        lines.append("")
        lines.append("### 引数")
        lines.append("")
        lines.append(argument_text(name))
        lines.append("")
        lines.append("### 戻り値")
        lines.append("")
        lines.append(return_text(name))
        lines.append("")
        lines.append("### 解説")
        lines.append("")
        lines.append("FeelKit 本体は engine 非依存を保つため、描画、音再生、専用 device 操作は callback または provider 経由でゲーム側に渡します。")
        lines.append("")
        lines.append("### サンプルコード")
        lines.append("")
        lines.append("```cpp")
        lines.append(sample_code(name))
        lines.append("```")
        lines.append("")
        lines.append("### 実装状態")
        lines.append("")
        lines.append("本実装。宣言場所: " + ", ".join(locations.get(name, [])))
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    """API_REFERENCE.md の補完章を置き換える。"""

    text = REFERENCE_PATH.read_text(encoding="utf-8-sig")
    markers = ["\n# 公開 API 補完リファレンス", "\n# ?? API ????????"]
    split_index = -1

    for marker in markers:
        split_index = text.find(marker)

        if split_index >= 0:
            break

    if split_index < 0:
        raise RuntimeError("補完章の開始位置が見つかりません。")

    repaired_text = text[:split_index].rstrip() + "\n\n" + build_completion_section()
    REFERENCE_PATH.write_text(repaired_text, encoding="utf-8-sig")
    print("repaired API_REFERENCE.md completion section")


if __name__ == "__main__":
    main()
