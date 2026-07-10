#pragma warning(disable : 4189 4514)

#include "EditorFrameInputManager.h"

#include "Engine/Input/InputSystem.h"
#include "EditorSharedState.h"

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
	// DirectInput キーボード状態の取得
	//================================================================

	std::memcpy(g_preKey, g_key, sizeof(g_key));  // g_preKey は前フレーム、g_key は今フレームの押下状態。差分でトリガー入力を判定する。
	g_hr = g_keyboardDevice->Acquire();  // Acquire はフォーカス復帰後に入力取得を再開するために必要。
	g_hr = g_keyboardDevice->GetDeviceState(sizeof(g_key), g_key);  // GetDeviceState は DIK_* ごとの押下状態を 256 バイト配列に詰める。

	// 入力取得に失敗した場合は、デバイスを取り直して同じフレーム内で再取得する。
	if (FAILED(g_hr)) {
		g_keyboardDevice->Acquire();
		g_hr = g_keyboardDevice->GetDeviceState(sizeof(g_key), g_key);
	}

	// ESC 押下はアプリ終了要求として WM_QUIT を発行する。
	if (g_key[DIK_ESCAPE]) {
		PostQuitMessage(0);
	}

	//================================================================
	// Unity Input System 風 InputSystem へのキー状態反映
	//================================================================

	InputSystem& inputSystem = Engine::GetInputSystem();  // OS イベントではなくフレーム更新側で Action 判定する入力システム本体。
	for (const EngineKeyBinding& keyBinding : kEngineKeyBindings) {
		const bool isPressed = (g_key[keyBinding.dikCode] & 0x80u) != 0u;
		inputSystem.SetKeyState(keyBinding.keyPath, isPressed);  // DirectInput の現在押下状態を Keyboard/Space 形式で流し込む。
	}
	inputSystem.Update();  // 前フレーム状態との差分から started / performed / canceled を決めてコールバックを呼ぶ。

	//================================================================
	// FeelKitHaptics フレーム更新
	//================================================================

	g_feelKitHaptics.update();

	//================================================================
	// DirectInput マウス状態の取得
	//================================================================

	if (g_mouseDevice != nullptr) {
		g_hr = g_mouseDevice->Acquire();
		g_hr = g_mouseDevice->GetDeviceState(sizeof(g_mouseState), &g_mouseState);
		if (FAILED(g_hr)) {
			g_mouseDevice->Acquire();
			g_hr = g_mouseDevice->GetDeviceState(sizeof(g_mouseState), &g_mouseState);
		}
	}

	//================================================================
	// エディターカメラのキーボード操作
	//================================================================

	// Play 中はゲーム入力を優先し、エディターカメラのショートカットと衝突しないようにする。
	g_editorSceneCameraController.UpdateKeyboard(
		g_key,
		g_preKey,
		g_editorRuntimeManager.IsPlaying(),
		g_cameraTransform,
		g_uvTransform,
		g_editorCameraMoveSpeed,
		g_editorCameraRotateSpeed,
		g_editorCameraFastRate);

	// currentClientRect は Windows クライアント領域の現在サイズ。外枠やタイトルバーは含めない。
	RECT currentClientRect{};
	GetClientRect(g_windowHandle, &currentClientRect);

	//================================================================
	// ウィンドウリサイズに合わせた描画ターゲット更新
	//================================================================

	// right - left はクライアント幅。0 になる最小化中でも 1px は確保する。
	uint32_t nextRenderWidth =
		(std::max)(1u, static_cast<uint32_t>(currentClientRect.right - currentClientRect.left));

	// bottom - top はクライアント高さ。0 の SwapChain を作れないため 1px 以上に丸める。
	uint32_t nextRenderHeight =
		(std::max)(1u, static_cast<uint32_t>(currentClientRect.bottom - currentClientRect.top));

	ResizeRenderTargets(nextRenderWidth, nextRenderHeight);  // SwapChain / DepthStencil / RTV を必要な時だけ作り直す。
	g_editorWindowWidth = static_cast<float>(g_renderWidth);  // ImGui と SceneView は float 座標で扱うため、描画サイズを float に明示変換する。
	g_editorWindowHeight = static_cast<float>(g_renderHeight);
}

void EditorFrameInputManager::Draw() {
}
