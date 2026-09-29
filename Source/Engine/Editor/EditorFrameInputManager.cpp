#pragma warning(disable : 4189 4514)

#include "EditorFrameInputManager.h"

#include "Engine/Input/InputSystem.h"
#include "EditorSharedState.h"
#include "Source/Engine/Core/GamepadInput.h"

using namespace EditorSharedState;

namespace {
	struct EngineKeyBinding {
		int32_t dikCode;  // DirectInput の DIK_* 番号
		const char* keyPath;  // InputSystem が受け取る Keyboard/Space 形式の Path
	};

	const EngineKeyBinding kEngineKeyBindings[] = {
		{DIK_SPACE, "Keyboard/Space"},
		{DIK_RETURN, "Keyboard/Enter"},
		{DIK_ESCAPE, "Keyboard/Escape"},
		{DIK_LEFT, "Keyboard/Left"},
		{DIK_RIGHT, "Keyboard/Right"},
		{DIK_UP, "Keyboard/Up"},
		{DIK_DOWN, "Keyboard/Down"},
		{DIK_LSHIFT, "Keyboard/LeftShift"},
		{DIK_RSHIFT, "Keyboard/RightShift"},
		{DIK_LCONTROL, "Keyboard/LeftCtrl"},
		{DIK_0, "Keyboard/0"},
		{DIK_1, "Keyboard/1"},
		{DIK_2, "Keyboard/2"},
		{DIK_3, "Keyboard/3"},
		{DIK_4, "Keyboard/4"},
		{DIK_5, "Keyboard/5"},
		{DIK_6, "Keyboard/6"},
		{DIK_7, "Keyboard/7"},
		{DIK_8, "Keyboard/8"},
		{DIK_9, "Keyboard/9"},
		{DIK_A, "Keyboard/A"},
		{DIK_B, "Keyboard/B"},
		{DIK_C, "Keyboard/C"},
		{DIK_D, "Keyboard/D"},
		{DIK_E, "Keyboard/E"},
		{DIK_F, "Keyboard/F"},
		{DIK_G, "Keyboard/G"},
		{DIK_H, "Keyboard/H"},
		{DIK_I, "Keyboard/I"},
		{DIK_J, "Keyboard/J"},
		{DIK_K, "Keyboard/K"},
		{DIK_L, "Keyboard/L"},
		{DIK_M, "Keyboard/M"},
		{DIK_N, "Keyboard/N"},
		{DIK_O, "Keyboard/O"},
		{DIK_P, "Keyboard/P"},
		{DIK_Q, "Keyboard/Q"},
		{DIK_R, "Keyboard/R"},
		{DIK_S, "Keyboard/S"},
		{DIK_T, "Keyboard/T"},
		{DIK_U, "Keyboard/U"},
		{DIK_V, "Keyboard/V"},
		{DIK_W, "Keyboard/W"},
		{DIK_X, "Keyboard/X"},
		{DIK_Y, "Keyboard/Y"},
		{DIK_Z, "Keyboard/Z"},
	};
}

void EditorFrameInputManager::Initialize() {
}

void EditorFrameInputManager::Update() {
	// DirectInput や Window が未初期化または解放済みなら、入力デバイスに触らない。
	if (!g_isInitialized || g_isFinalized) {
		return;
	}

	//================================================================
	// DirectInput の入力取得
	//================================================================

	// Keyboard と Mouse の取得、前フレーム状態の退避は Input クラスが一手に行う。
	// ここで 2 回呼ぶと「押した瞬間」が消えるため、1 フレームに 1 回だけ呼ぶ。
	if (g_input == nullptr) {
		return;
	}

	g_input->Update();

	// 後続の Manager は 256 バイト配列をそのまま受け取る形なので、読み取り専用で借りる。
	const BYTE* keyStates = g_input->GetKeyStates();
	const BYTE* previousKeyStates = g_input->GetPreviousKeyStates();

	// ESCの意味は起動形態で変える。書き出し済みPlayerでは従来通りアプリ終了要求。
	// Editor内では「視点操作=常時」等でPlay中にGameViewから抜けにくくなった時、
	// ESCがエディタごと終了する唯一の脱出手段になってしまっていたため、
	// Play中はStopとして扱い、Editorそのものは閉じないようにする。
	if (g_input->TriggerKey(DIK_ESCAPE)) {
		if (g_isStandaloneGame) {
			PostQuitMessage(0);
		}
		else if (g_editorRuntimeManager.IsPlaying()) {
			g_editorRuntimeManager.TogglePlay();
		}
	}

	//================================================================
	// Unity Input System 風 InputSystem へのキー状態反映
	//================================================================

	InputSystem& inputSystem = Engine::GetInputSystem();  // OS イベントではなくフレーム更新側で Action 判定する入力システム本体。
	for (const EngineKeyBinding& keyBinding : kEngineKeyBindings) {
		const bool isPressed = g_input->PushKey(keyBinding.dikCode);
		inputSystem.SetKeyState(keyBinding.keyPath, isPressed);  // DirectInput の現在押下状態を Keyboard/Space 形式で流し込む。
	}
	inputSystem.Update();  // 前フレーム状態との差分から started / performed / canceled を決めてコールバックを呼ぶ。

	//================================================================
	// FeelKitHaptics フレーム更新
	//================================================================

	g_feelKitHaptics.update();

	//================================================================
	// Gamepad (XInput) 状態の取得
	//================================================================
	// Input Action の Gamepad Binding はここで更新した値を読む。
	// 未接続・抜き差しは GamepadInput 側が中立値へ倒して安全に扱う。
	GamepadInput::Get().Update();

	//================================================================
	// Cursor の固定
	//================================================================

	// Mouse の取得自体は上の Input::Update() で済んでいる。ここは Play 中の
	// 視点操作で Cursor が Window の外へ出ないようにするだけ。
	if (g_runtimeCursorLocked) {
		ApplyRuntimeCursorLock(true);
	}

	//================================================================
	// エディターカメラのキーボード操作
	//================================================================

	// Play 中はゲーム入力を優先し、エディターカメラのショートカットと衝突しないようにする。
	g_editorSceneCameraController.UpdateKeyboard(
		keyStates,
		previousKeyStates,
		g_editorRuntimeManager.IsPlaying(),
		g_cameraTransform,
		g_uvTransform,
		g_editorCameraMoveSpeed,
		g_editorCameraRotateSpeed,
		g_editorCameraFastRate);

	//================================================================
	// ウィンドウリサイズに合わせた描画ターゲット更新
	//================================================================

	// Client 領域は外枠やタイトルバーを含まない、描画に使う領域だけ。
	// 最小化中の 0px 丸めは WinApp 側で済ませているので、ここでは受け取るだけ。
	uint32_t nextRenderWidth = 1u;
	uint32_t nextRenderHeight = 1u;

	if (g_winApp == nullptr || !g_winApp->GetClientSize(nextRenderWidth, nextRenderHeight)) {
		return;  // Window が無い状態では SwapChain を作り直さない
	}

	ResizeRenderTargets(nextRenderWidth, nextRenderHeight);  // SwapChain / DepthStencil / RTV を必要な時だけ作り直す。
	g_editorWindowWidth = static_cast<float>(g_renderWidth);  // ImGui と SceneView は float 座標で扱うため、描画サイズを float に明示変換する。
	g_editorWindowHeight = static_cast<float>(g_renderHeight);
}

void EditorFrameInputManager::Draw() {
}
