#pragma once

#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// 外部認識・オンライン連携の Debug Window
//================================================================
// 仕様書 14 / 32 / 53 / 81 項のデバッグ表示をまとめて 1 つの Window で出す。
// 音声認識、画像認識、オンライン、Haptics のタブを持つ。

class EditorExternalFeatureWindowManager {
public:
	void Initialize();
	void Update();  // Play していない間も Haptics Preview を進め、Console ログを流す。
	void Draw();

private:
	void DrawSpeechTab();
	void DrawVisionTab();
	void DrawOnlineTab();
	void DrawHapticsTab();

	float previewScale_ = 2.0f;  // 画像認識 Preview の表示倍率。
	int32_t selectedCameraGameObjectId_ = -1;
};

#pragma warning(pop)
