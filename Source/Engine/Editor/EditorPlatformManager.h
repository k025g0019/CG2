#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPlatformManager {
public:
	//================================================================
	// OS と GPU リソースの土台を扱う Manager
	//================================================================

	void Initialize(_In_ HINSTANCE instanceHandle);  // Win32 ウィンドウ、DirectInput、XAudio2、DirectX12、ImGui を生成する。
	void Update();  // Windows メッセージを処理し、WM_QUIT を終了フラグに変換する。
	void Draw();  // フレーム先頭で描画要求を下げる処理は Update 側にあるため、ここは空。
	bool IsEndRequested() const;  // main ループを抜けるための終了要求フラグを返す。
	bool HasInitializationFailed() const;  // 初期化失敗で後続 Manager を動かさないためのフラグを返す。
	int Finalize();  // DirectX / ImGui / 入力 / 音声 / Window を解放して終了コードを返す。
};

#pragma warning(pop)
