#pragma once

#include "EditorSceneCameraController.h"

#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// PV撮影モード
//================================================================

// Play中にHierarchy/Inspector等の編集用パネルを隠し、GameViewだけを
// ビルド後の画面のように全画面表示しながらCamera/Exposure/Bloom/TimeScaleを
// 撮影用に調整できるようにするWindow。
class EditorPvShootWindowManager {
public:
	void Initialize();
	void Update();
	void Draw();

	bool IsActive() const;  // GameScene::Draw が他Windowの表示可否を判断するために使う。

private:
	void DrawPanel();  // モード切り替え・Camera・PostProcess・TimeScaleを1つの固定Windowにまとめて描画する。
	void DrawPlaybackControls();  // Playerの動きをRecord/Playbackし、撮影用の2回操作を可能にする。

	EditorSceneCameraController pvCameraController_;  // SceneViewと同じマウス/キーボード操作をPV Cameraへ適用する。
	bool isMiddleCameraDragging_ = false;  // 中ドラッグでのパン中かどうか。
	bool isRightCameraDragging_ = false;  // 右ドラッグでの回転中かどうか。
	bool lookAtPlayerEnabled_ = false;  // ONの間、Cameraの回転を注視対象へ強制的に向ける。
	int32_t lookAtTargetGameObjectId_ = -1;  // 注視対象GameObject。-1なら未選択（自動でPlayerらしき名前を探す）。
	float manualTimeScale_ = 1.0f;  // PV撮影モード中だけ乗算する再生速度。1で通常速度。
	bool isUiHidden_ = false;  // trueの間はF9で復帰する以外、PVパネルを一切描画しない（録画に映り込ませないため）。
};

#pragma warning(pop)
