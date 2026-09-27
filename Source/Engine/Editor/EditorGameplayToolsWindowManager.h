#pragma once

#include "EditorScene.h"

#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorGameplayToolsWindowManager {
public:
	void Initialize();  // 汎用Gameplay編集Windowの選択状態を初期化する。
	void Update();  // 編集値はImGui操作時に反映するため更新処理を持たない。
	void Draw();  // Spline、Event Timeline、State Graphを独立Windowとして描画する。

private:
	int32_t selectedRailPathGameObjectId_ = -1;
	int32_t selectedControlPointGameObjectId_ = -1;
	int32_t draggingControlPointGameObjectId_ = -1;
	bool showsSideView_ = false;
	int32_t timelineSourceMode_ = 0;
	int32_t selectedTimelineGameObjectId_ = -1;
	int32_t draggingTimelineGameObjectId_ = -1;
	bool isDraggingWaveMarker_ = false;
	float timelineDurationSeconds_ = 30.0f;
	int32_t waveTemplateGameObjectId_ = -1;
	int32_t waveCount_ = 5;
	int32_t waveFormationPattern_ = 0;
	float waveSpacing_ = 3.0f;
	int32_t selectedStateGameObjectId_ = -1;

	void DrawSplineEditor();  // 制御点をリストと2Dキャンバスで編集する。
	void DrawEventTimeline();  // 時刻またはRail進行率と任意Script Actionの関係を編集する。
	void DrawStateGraph();  // 汎用値を3状態へ分けるThresholdとActionを編集する。
	void CreateSpline();  // 4点を持つ標準SplineをSceneへ追加する。
	void AddControlPoint();  // 選択Rail末尾へ制御点を追加する。
	void CreateTimelineEvent();  // 選択中Source/Targetを使う汎用EventをSceneへ追加する。
	void CreateWaveFromSelection();  // 選択中GameObjectの複製を子に持つ汎用Waveを追加する。
};

#pragma warning(pop)
