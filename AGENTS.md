## Goal
- Implement PBR/IBL rendering with cubemap reflections and fix planar reflection

## Constraints & Preferences
- IBL must affect objects even without direct light (never black)
- All light types must cast shadows
- Pre-baked DDS files (`environment_cube.dds` etc.) when available; fall back to neutral gray
- Keep forward rendering pipeline (HDR RT + post-process chain)
- Sky dome/reflection objects need separate culling rules

## Progress
### Done
- Deleted SSR entirely (replaced by IBL)
- Created `IBLRuntime.hlsli`, bake shaders, and PBR rewrite of `Object3d.PS.hlsl`
- Added IBL globals/descriptor indices 24–30, extended root signature to 11 params + 3 samplers
- IBL DDS loading with fallback gray 2×2 placeholders
- IBL SRV binding in both sprite and mesh draw paths
- Fixed RTV heap count (11→12) and command list reset for IBL upload
- Fixed placeholder texture zero-init → neutral gray
- Fixed duplicate function errors in `PBRCommon.hlsli` / `Math.hlsli`
- Fixed shadow guard (all light types) and shadow matrix (position-based for Point/Spot/Area)
- **Skybox culling**: added `cullMode` field to `EditorSceneObject`, created `g_cullFrontPipelineState` PSO (`CullMode=FRONT`, `DepthWrite=0`), wired in `drawSceneObjects`, name-based detection ("SkyDome"/"skydome"/"tennkyuu"/"天球") in synchronizer
- **FBX texture SRV collision**: changed custom texture start index 16→64, release guard `<16`→`<64` — prevents overwriting HDR RT (16) / PostProcess (19) / etc.
- **Planar Reflection PSO override bug**: removed unconditional `graphicsPipelineState` reset in `drawSceneObjects`; added `defaultDrawPso` variable to preserve caller's PSO (`planarScenePipelineState` for reflection pass)
- `PlanarReflection.PS.hlsl` (proper projection shader) already compiled and used — descriptor bindings correct (t2=materialMask index28, t3=planarReflectionRT index29, consecutive)
- **Reflection camera target fix**: changed from `position + reflectedForward` to `ReflectPoint(mainCameraTarget, ...)` for MakeLookAtMatrix
- **Depth buffer barrier fix**: added PS→DEPTH_WRITE barrier before reflection pass; removed unnecessary SSAO→DEPTH_WRITE transition; guarded depth-barrier with draw conditions
- **Depth clear before main scene**: added `ClearDepthStencilView(dsvHandle)` after reflection pass to prevent reflection depth corruption
- **ReflectionMask PSO fix**: changed `DepthFunc` from `LESS` to `LESS_EQUAL` so floor renders full mask (equal-depth test passes)
- **Material mask DEBUGGED**: triangular magenta areas move with camera = NOT partial mask coverage. Debug shader confirms composite IS running. The issue is reflectionUV calculation (worldPos reconstruction or reflectionViewProjection mismatch).
- **Matrix convention TRACED**: row-vector (`v * M`), left-handed (+Z forward), `MakeLookAtMatrix` and `Inverse(R*T)` both produce consistent view matrices. The projection matrix is LH (D3D style, Z→NDC[0,1]). No fundamental convention mismatch found.

### In Progress
- **Root cause**: reflectionUV is wrong — large portions of floor are magenta (UV out-of-bounds or `reflectionClip.w <= 0`), and the visible reflection is distorted. Pattern shifts with camera, proving it's NOT materialMask partial coverage.
- **Debug step 1**: sample `gPlanarReflectionTexture` with `screenUV` (no depth reconstruction) to verify reflection RT content is correct
- **Debug step 2**: display reflection RT full-screen to check which scene elements are visible from reflected camera

### Blocked
- Cubemap reflection doesn't change — IBL textures are still neutral gray placeholders (DDS files not created yet)
- Reflection Probe component not listed in editor "Add Component" menu without searching; registered in code but hidden by some filter
- Bright emissive sky dome mesh acts as a point light source at its world position (unnatural)

## Key Decisions
- Replace SSR with IBL (glTF 2.0 PBR: GGX+Smith+Fresnel-Schlick)
- Single orthographic shadow projection for Point/Spot (no cubemap shadow)
- Custom texture SRVs start at descriptor heap index 64 (above runtime SRVs 0–30) to avoid collision
- Per-object `cullMode` field on `EditorSceneObject` rather than material flag, with PSO switching in render loop
- `drawSceneObjects` uses `defaultDrawPso` to allow per-pass PSO (main vs. planar reflection)

## Debug Plan
1. **Sample reflection RT with screenUV** — modify composite shader to output `gPlanarReflectionColor.Sample(gSampler, input.texcoord).rgb` directly (no depth, no VP). If this shows the correct scene from the reflected camera viewpoint, the RT is fine and the issue is the UV math.
2. **Display reflection RT full-screen** — draw the RT across the entire screen to see what the reflected camera sees (which faces of cubes, which side of objects, etc.)
3. **If RT correct → fix depth-to-world reconstruction** — verify `inverseViewProjection` matrix matches projection used for depth rendering
4. **If RT wrong → check reflection camera setup** — wrong view matrix, wrong projection matrix, or main camera contamination
5. Generate real IBL DDS files from HDRI
6. Test Cubemap reflection with metal=1/roughness=0
7. Fix Reflection Probe component menu visibility
8. Fix emissive sky dome light emission at wrong position

## Relevant Files
- `Source/Engine/Editor/EditorRenderManager.cpp`: main draw loop, planar reflection pass, cullMode PSO switching, `defaultDrawPso`, depth buffer management
- `Source/Engine/Editor/EditorPlatformManager.cpp`: PSO creation (cullFront, planarScene, planarReflection, reflectionMask), IBL loading
- `Source/Engine/Editor/EditorSceneObject.h`: added `int32_t cullMode`
- `Source/Engine/Editor/EditorSceneObjectManager.cpp`: custom texture index 64+, `cullMode` init
- `Source/Engine/Editor/EditorSceneSynchronizer.cpp`: cullMode name detection ("SkyDome"/"skydome"/"tennkyuu"/"天球")
- `Source/Engine/Core/EditorSharedState.h`: `g_cullFrontPipelineState`, descriptor indices (24–30, 64+)
- `Source/Engine/Core/EditorCommonTypes.h`: Material struct with `reflectionReserved` (used for roughness)
- `Assets/Shaders/Reflection/PlanarReflection.PS.hlsl`: composite shader — currently in debug mode (no mask, UV out→magenta/green, OK→raw reflection RT color)
- `Assets/Shaders/Object3d.PS.hlsl`: PBR + IBL pixel shader
- `Assets/Shaders/Lighting/IBLRuntime.hlsli`: IBL evaluation
- `Assets/Shaders/Object3dReflectionMask.PS.hlsl`: writes reflectMask/smoothness/reflectionMode to material mask RT
