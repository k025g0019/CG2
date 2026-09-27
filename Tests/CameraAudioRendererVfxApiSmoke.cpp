#include "Source/Engine/Core/EditorNativeScript.h"

#include <cstdint>
#include <iostream>

// Camera / Audio / Renderer / VFX のScript APIについて、
//   1. Runtime API未接続(全Entryがnullptr)
//   2. Entryはあるが対象が存在しない・Handleが無効
// のどちらでもCrashせず、false または無効Handleを返すことを確認する。
// Editorを起動せずに「無効Handleを渡しても落ちない」という契約だけを機械的に検証する。

namespace {
	int32_t g_failureCount = 0;

	void Check(bool condition, const char* label) {
		if (!condition) {
			std::cout << "FAIL: " << label << "\n";
			g_failureCount++;
		}
	}

	// 対象が見つからない実行系を模したStub。全Entryが「失敗」を返す。
	bool FalseBoolGameObjectFloatOut(int32_t, float*) { return false; }
	bool FalseBoolGameObjectFloat(int32_t, float) { return false; }
	bool FalseBoolGameObjectIntOut(int32_t, int32_t*) { return false; }
	bool FalseBoolGameObjectInt(int32_t, int32_t) { return false; }
	bool FalseBoolGameObjectBoolOut(int32_t, bool*) { return false; }
	bool FalseBoolGameObjectBool(int32_t, bool) { return false; }
	bool FalseBoolGameObjectVectorIn(int32_t, const EditorScriptVector3*) { return false; }
	bool FalseBoolGameObjectVectorOut(int32_t, EditorScriptVector3*) { return false; }
	int32_t InvalidGameObjectId() { return -1; }
	bool FalseBoolGameObject(int32_t) { return false; }

	EditorScriptAudioHandle InvalidAudioFromGameObject(int32_t) {
		return kInvalidEditorScriptAudioHandle;
	}

	EditorScriptAudioHandle InvalidAudioFromClipPosition(
		const char*, const EditorScriptVector3*, int32_t, float, bool) {
		return kInvalidEditorScriptAudioHandle;
	}

	EditorScriptAudioHandle InvalidAudioFromClip2D(const char*, int32_t, float, bool) {
		return kInvalidEditorScriptAudioHandle;
	}

	bool FalseBoolAudioHandle(EditorScriptAudioHandle) { return false; }
	bool FalseBoolAudioHandleBool(EditorScriptAudioHandle, bool) { return false; }
	bool FalseBoolAudioHandleFloat(EditorScriptAudioHandle, float) { return false; }
	bool FalseBoolAudioHandleFloatOut(EditorScriptAudioHandle, float*) { return false; }
	bool FalseBoolAudioHandleBoolOut(EditorScriptAudioHandle, bool*) { return false; }
	bool FalseBoolAudioHandleInt(EditorScriptAudioHandle, int32_t) { return false; }
	bool FalseBoolAudioHandleVectorIn(EditorScriptAudioHandle, const EditorScriptVector3*) { return false; }
	bool FalseBoolAudioHandleFloatFloat(EditorScriptAudioHandle, float, float) { return false; }

	bool FalseBoolRendererFloat(int32_t, const char*, float) { return false; }
	bool FalseBoolRendererFloatOut(int32_t, const char*, float*) { return false; }
	bool FalseBoolRendererColorIn(int32_t, const char*, const EditorScriptVector3*) { return false; }
	bool FalseBoolRendererColorOut(int32_t, const char*, EditorScriptVector3*) { return false; }
	bool FalseBoolRendererTextureIn(int32_t, const char*, const char*) { return false; }
	bool FalseBoolRendererTextureOut(int32_t, const char*, char*, int32_t) { return false; }
	bool FalseBoolRendererEmission(int32_t, EditorScriptVector3*, float*) { return false; }

	EditorScriptVfxHandle InvalidVfxFromSpawn(
		const char*, const EditorScriptVector3*, const EditorScriptVector3*) {
		return kInvalidEditorScriptVfxHandle;
	}

	EditorScriptVfxHandle InvalidVfxFromSpawnAttached(
		const char*, int32_t, const EditorScriptVector3*) {
		return kInvalidEditorScriptVfxHandle;
	}

	bool FalseBoolVfxHandle(EditorScriptVfxHandle) { return false; }
	bool FalseBoolVfxHandleBool(EditorScriptVfxHandle, bool) { return false; }
	bool FalseBoolVfxHandleFloat(EditorScriptVfxHandle, float) { return false; }
	bool FalseBoolVfxHandleFloatOut(EditorScriptVfxHandle, float*) { return false; }
	bool FalseBoolVfxHandleIntOut(EditorScriptVfxHandle, int32_t*) { return false; }
	bool FalseBoolVfxHandleVectorIn(EditorScriptVfxHandle, const EditorScriptVector3*) { return false; }
	bool FalseBoolVfxHandleVectorOut(EditorScriptVfxHandle, EditorScriptVector3*) { return false; }

	bool FalseBoolUiText(int32_t, const char*) { return false; }
	bool FalseBoolUiTextOut(int32_t, char*, int32_t) { return false; }
	bool FalseBoolUiColorIn(int32_t, const EditorScriptVector3*, float) { return false; }
	bool FalseBoolUiColorOut(int32_t, EditorScriptVector3*, float*) { return false; }
	bool FalseBoolTerrainHeight(int32_t, float, float, float*) { return false; }
	bool FalseBoolTerrainContains(int32_t, float, float) { return false; }

	void FillFailingRuntimeApi(EditorScriptRuntimeApi& runtimeApi) {
		runtimeApi.CameraGetFieldOfView = FalseBoolGameObjectFloatOut;
		runtimeApi.CameraSetFieldOfView = FalseBoolGameObjectFloat;
		runtimeApi.CameraGetNearClip = FalseBoolGameObjectFloatOut;
		runtimeApi.CameraSetNearClip = FalseBoolGameObjectFloat;
		runtimeApi.CameraGetFarClip = FalseBoolGameObjectFloatOut;
		runtimeApi.CameraSetFarClip = FalseBoolGameObjectFloat;
		runtimeApi.CameraGetProjectionMode = FalseBoolGameObjectIntOut;
		runtimeApi.CameraSetProjectionMode = FalseBoolGameObjectInt;
		runtimeApi.CameraGetOrthographicSize = FalseBoolGameObjectFloatOut;
		runtimeApi.CameraSetOrthographicSize = FalseBoolGameObjectFloat;
		runtimeApi.CameraGetPriority = FalseBoolGameObjectIntOut;
		runtimeApi.CameraSetPriority = FalseBoolGameObjectInt;
		runtimeApi.CameraGetEnabled = FalseBoolGameObjectBoolOut;
		runtimeApi.CameraSetEnabled = FalseBoolGameObjectBool;
		runtimeApi.CameraGetActive = InvalidGameObjectId;
		runtimeApi.CameraSetActive = FalseBoolGameObject;
		runtimeApi.CameraLookAt = FalseBoolGameObjectVectorIn;
		runtimeApi.CameraGetLookDirection = FalseBoolGameObjectVectorOut;
		runtimeApi.CameraSetLookDirection = FalseBoolGameObjectVectorIn;

		runtimeApi.AudioPlayWithHandle = InvalidAudioFromGameObject;
		runtimeApi.AudioPlayClipAtPosition = InvalidAudioFromClipPosition;
		runtimeApi.AudioPlayClip2D = InvalidAudioFromClip2D;
		runtimeApi.AudioStopHandle = FalseBoolAudioHandle;
		runtimeApi.AudioSetPaused = FalseBoolAudioHandleBool;
		runtimeApi.AudioIsPlayingHandle = FalseBoolAudioHandle;
		runtimeApi.AudioIsHandleValid = FalseBoolAudioHandle;
		runtimeApi.AudioSetVolumeHandle = FalseBoolAudioHandleFloat;
		runtimeApi.AudioGetVolumeHandle = FalseBoolAudioHandleFloatOut;
		runtimeApi.AudioSetPitch = FalseBoolAudioHandleFloat;
		runtimeApi.AudioGetPitch = FalseBoolAudioHandleFloatOut;
		runtimeApi.AudioSetLoop = FalseBoolAudioHandleBool;
		runtimeApi.AudioGetLoop = FalseBoolAudioHandleBoolOut;
		runtimeApi.AudioSetPositionHandle = FalseBoolAudioHandleVectorIn;
		runtimeApi.AudioSetBus = FalseBoolAudioHandleInt;
		runtimeApi.AudioGetPlaybackPosition = FalseBoolAudioHandleFloatOut;
		runtimeApi.AudioSetPlaybackPosition = FalseBoolAudioHandleFloat;
		runtimeApi.AudioGetDuration = FalseBoolAudioHandleFloatOut;
		runtimeApi.AudioFadeTo = FalseBoolAudioHandleFloatFloat;

		runtimeApi.RendererGetColor = FalseBoolGameObjectVectorOut;
		runtimeApi.RendererSetEnabled = FalseBoolGameObjectBool;
		runtimeApi.RendererGetEnabled = FalseBoolGameObjectBoolOut;
		runtimeApi.RendererSetOpacity = FalseBoolGameObjectFloat;
		runtimeApi.RendererGetOpacity = FalseBoolGameObjectFloatOut;
		runtimeApi.RendererGetEmission = FalseBoolRendererEmission;
		runtimeApi.RendererSetMaterialFloat = FalseBoolRendererFloat;
		runtimeApi.RendererGetMaterialFloat = FalseBoolRendererFloatOut;
		runtimeApi.RendererSetMaterialColor = FalseBoolRendererColorIn;
		runtimeApi.RendererGetMaterialColor = FalseBoolRendererColorOut;
		runtimeApi.RendererSetMaterialTexture = FalseBoolRendererTextureIn;
		runtimeApi.RendererGetMaterialTexture = FalseBoolRendererTextureOut;

		runtimeApi.VfxSpawn = InvalidVfxFromSpawn;
		runtimeApi.VfxSpawnAttached = InvalidVfxFromSpawnAttached;
		runtimeApi.VfxStopHandle = FalseBoolVfxHandle;
		runtimeApi.VfxIsPlayingHandle = FalseBoolVfxHandle;
		runtimeApi.VfxSetPositionHandle = FalseBoolVfxHandleVectorIn;
		runtimeApi.VfxGetPositionHandle = FalseBoolVfxHandleVectorOut;
		runtimeApi.VfxSetPausedHandle = FalseBoolVfxHandleBool;
		runtimeApi.VfxSetPlaybackSpeed = FalseBoolVfxHandleFloat;
		runtimeApi.VfxGetPlaybackSpeed = FalseBoolVfxHandleFloatOut;
		runtimeApi.VfxRestartHandle = FalseBoolVfxHandle;
		runtimeApi.VfxGetParticleCountHandle = FalseBoolVfxHandleIntOut;
		runtimeApi.VfxSetRotationHandle = FalseBoolVfxHandleVectorIn;
		runtimeApi.VfxSetScaleHandle = FalseBoolVfxHandleVectorIn;

		runtimeApi.UiSetText = FalseBoolUiText;
		runtimeApi.UiGetText = FalseBoolUiTextOut;
		runtimeApi.UiSetTextColor = FalseBoolUiColorIn;
		runtimeApi.UiGetTextColor = FalseBoolUiColorOut;
		runtimeApi.UiSetFontSize = FalseBoolGameObjectFloat;
		runtimeApi.UiGetFontSize = FalseBoolGameObjectFloatOut;
		runtimeApi.UiSetInteractable = FalseBoolGameObjectBool;
		runtimeApi.UiGetInteractable = FalseBoolGameObjectBoolOut;
		runtimeApi.UiSetSliderValue = FalseBoolGameObjectFloat;
		runtimeApi.UiGetSliderValue = FalseBoolGameObjectFloatOut;
		runtimeApi.UiSetToggleValue = FalseBoolGameObjectBool;
		runtimeApi.UiGetToggleValue = FalseBoolGameObjectBoolOut;
		runtimeApi.TerrainGetHeightAtWorld = FalseBoolTerrainHeight;
		runtimeApi.TerrainContainsWorldPosition = FalseBoolTerrainContains;
	}

	// 削除済みGameObject / 無効Handleを模した対象へ、全操作を一通り呼ぶ。
	void ExerciseEveryWrapper(const char* phaseLabel) {
		const EditorScriptVector3 anyVector{1.0f, 2.0f, 3.0f};

		const Camera camera{-1};
		float floatValue = -12345.0f;
		int32_t intValue = -12345;
		bool boolValue = true;

		Check(!camera.GetFieldOfView(floatValue), phaseLabel);
		Check(!camera.SetFieldOfView(60.0f), phaseLabel);
		Check(!camera.GetNearClip(floatValue), phaseLabel);
		Check(!camera.SetNearClip(0.3f), phaseLabel);
		Check(!camera.GetFarClip(floatValue), phaseLabel);
		Check(!camera.SetFarClip(500.0f), phaseLabel);
		Check(!camera.GetProjectionMode(intValue), phaseLabel);
		Check(!camera.SetProjectionMode(1), phaseLabel);
		Check(!camera.GetOrthographicSize(floatValue), phaseLabel);
		Check(!camera.SetOrthographicSize(5.0f), phaseLabel);
		Check(!camera.GetPriority(intValue), phaseLabel);
		Check(!camera.SetPriority(10), phaseLabel);
		Check(!camera.GetEnabled(boolValue), phaseLabel);
		Check(!camera.SetEnabled(true), phaseLabel);
		Check(!camera.SetActive(), phaseLabel);
		Check(!camera.LookAt(anyVector), phaseLabel);
		Check(!camera.GetLookDirection(*const_cast<EditorScriptVector3*>(&anyVector)), phaseLabel);
		Check(!camera.SetLookDirection(anyVector), phaseLabel);
		Check(!camera.IsValid(), phaseLabel);
		Check(Camera::GetActive().GetInstanceId() < 0, phaseLabel);

		const AudioVoice voice{Audio::PlayAtPosition("Assets/Audio/Missing.wav", anyVector)};
		Check(voice.GetHandle() == kInvalidEditorScriptAudioHandle, phaseLabel);
		Check(Audio::Play2D("Assets/Audio/Missing.wav") == kInvalidEditorScriptAudioHandle, phaseLabel);
		Check(!voice.IsValid(), phaseLabel);
		Check(!voice.IsPlaying(), phaseLabel);
		Check(!voice.Stop(), phaseLabel);
		Check(!voice.Pause(), phaseLabel);
		Check(!voice.Resume(), phaseLabel);
		Check(!voice.SetVolume(0.5f), phaseLabel);
		Check(!voice.GetVolume(floatValue), phaseLabel);
		Check(!voice.SetPitch(1.2f), phaseLabel);
		Check(!voice.GetPitch(floatValue), phaseLabel);
		Check(!voice.SetLoop(true), phaseLabel);
		Check(!voice.GetLoop(boolValue), phaseLabel);
		Check(!voice.SetPosition(anyVector), phaseLabel);
		Check(!voice.SetBus(1), phaseLabel);
		Check(!voice.GetPlaybackPosition(floatValue), phaseLabel);
		Check(!voice.SetPlaybackPosition(1.0f), phaseLabel);
		Check(!voice.GetDuration(floatValue), phaseLabel);
		Check(!voice.FadeTo(0.0f, 1.0f), phaseLabel);
		Check(!voice.FadeIn(1.0f), phaseLabel);
		Check(!voice.FadeOut(1.0f), phaseLabel);

		const GameObject missingGameObject{};
		const Renderer renderer{missingGameObject};
		EditorScriptVector3 colorValue{};
		std::string texturePath = "unchanged";

		Check(!renderer.GetColor(colorValue), phaseLabel);
		Check(!renderer.GetEmission(colorValue, floatValue), phaseLabel);
		Check(!renderer.SetEnabled(false), phaseLabel);
		Check(!renderer.GetEnabled(boolValue), phaseLabel);
		Check(!renderer.SetOpacity(0.5f), phaseLabel);
		Check(!renderer.GetOpacity(floatValue), phaseLabel);
		Check(!renderer.SetMaterialFloat("Metallic", 1.0f), phaseLabel);
		Check(!renderer.GetMaterialFloat("Metallic", floatValue), phaseLabel);
		Check(!renderer.SetMaterialColor("Color", anyVector), phaseLabel);
		Check(!renderer.GetMaterialColor("Color", colorValue), phaseLabel);
		Check(!renderer.SetMaterialTexture("BaseColor", "Assets/Textures/Missing.png"), phaseLabel);
		Check(!renderer.GetMaterialTexture("BaseColor", texturePath), phaseLabel);
		Check(texturePath == "unchanged", phaseLabel);

		const VfxInstance vfx = VfxInstance::Spawn("MissingEffect", anyVector);
		Check(vfx.GetHandle() == kInvalidEditorScriptVfxHandle, phaseLabel);
		Check(VfxInstance::SpawnAttached("MissingEffect", missingGameObject).GetHandle() ==
			kInvalidEditorScriptVfxHandle, phaseLabel);
		Check(!vfx.IsPlaying(), phaseLabel);
		Check(!vfx.Stop(), phaseLabel);
		Check(!vfx.Pause(), phaseLabel);
		Check(!vfx.Resume(), phaseLabel);
		Check(!vfx.Restart(), phaseLabel);
		Check(!vfx.SetPosition(anyVector), phaseLabel);
		Check(!vfx.GetPosition(colorValue), phaseLabel);
		Check(!vfx.SetPlaybackSpeed(2.0f), phaseLabel);
		Check(!vfx.GetPlaybackSpeed(floatValue), phaseLabel);
		Check(!vfx.GetParticleCount(intValue), phaseLabel);
		Check(!vfx.SetRotation(anyVector), phaseLabel);
		Check(!vfx.SetScale(anyVector), phaseLabel);

		const Ui ui{missingGameObject};
		std::string uiText = "unchanged";
		Check(!ui.SetText("score"), phaseLabel);
		Check(!ui.GetText(uiText), phaseLabel);
		Check(uiText == "unchanged", phaseLabel);
		Check(!ui.SetColor(anyVector, 1.0f), phaseLabel);
		Check(!ui.GetColor(colorValue, floatValue), phaseLabel);
		Check(!ui.SetFontSize(24.0f), phaseLabel);
		Check(!ui.GetFontSize(floatValue), phaseLabel);
		Check(!ui.SetInteractable(true), phaseLabel);
		Check(!ui.GetInteractable(boolValue), phaseLabel);
		Check(!ui.SetSliderValue(0.5f), phaseLabel);
		Check(!ui.GetSliderValue(floatValue), phaseLabel);
		Check(!ui.SetToggleValue(true), phaseLabel);
		Check(!ui.GetToggleValue(boolValue), phaseLabel);

		const Terrain terrain{missingGameObject};
		Check(!terrain.GetHeightAt(10.0f, 20.0f, floatValue), phaseLabel);
		Check(!terrain.GetHeightAt(anyVector, floatValue), phaseLabel);
		Check(!terrain.Contains(10.0f, 20.0f), phaseLabel);

		// 出力用変数が書き換わっていない = 失敗時に未初期化値を掴ませていない。
		Check(floatValue == -12345.0f, phaseLabel);
		Check(intValue == -12345, phaseLabel);
	}
}

int main() {
	std::cout << "kEditorScriptApiVersion = " << kEditorScriptApiVersion << "\n";

	// 1. Runtime API自体が未接続(古いEditorへLoadされた場合と同じ状態)。
	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);
	ExerciseEveryWrapper("runtimeApi == nullptr");

	// 2. Entryはあるが対象が存在しない(削除済みGameObject / 再生終了済みHandle)。
	EditorScriptRuntimeApi runtimeApi{};
	FillFailingRuntimeApi(runtimeApi);
	EditorNativeScriptRuntime::SetRuntimeApi(&runtimeApi);
	ExerciseEveryWrapper("missing target");

	EditorNativeScriptRuntime::SetRuntimeApi(nullptr);

	if (g_failureCount != 0) {
		std::cout << "FAILED: " << g_failureCount << " check(s)\n";
		return EXIT_FAILURE;
	}

	std::cout << "OK: Camera / Audio / Renderer / VFX wrappers are safe for invalid targets\n";
	return EXIT_SUCCESS;
}
