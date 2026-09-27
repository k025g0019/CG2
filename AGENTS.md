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
