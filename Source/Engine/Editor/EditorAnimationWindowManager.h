#pragma once

#include "Source/Engine/Animation/PropertyAnimationClip.h"
#include "EditorScene.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAnimationWindowManager {
public:
	void Initialize();  // Animation Window の編集状態と実時間計測を初期化する。
	void Update();  // プレビュー時間、記録中の値、Scene へのサンプル適用を更新する。
	void Draw();  // Timeline、Track、Keyframe、Event の編集 UI を描画する。

private:
	static constexpr std::size_t kRecordedPropertyCount =
		static_cast<std::size_t>(AnimationPropertyTarget::MaterialEmissionColorB) + 1u;

	PropertyAnimationClip animationClip_{};  // 現在ウィンドウで開いている .animclip の編集コピー。
	std::string animationClipPath_;  // 保存・再読み込みに使う現在のアセットパス。
	EditorGameObject previewBackup_{};  // Preview 開始前の GameObject 全体。停止時の復元に使う。
	std::array<float, kRecordedPropertyCount> recordedPropertyValues_{};  // 自動記録が比較する各プロパティの直前値。
	std::array<bool, kRecordedPropertyCount> hasRecordedPropertyValue_{};  // 対応 Component があり比較可能な項目だけ true にする。
	std::chrono::steady_clock::time_point previousUpdateTime_{};  // UI Preview の実時間差分を求める基準。
	float currentTime_ = 0.0f;  // Timeline 上の再生ヘッド位置（秒）。
	float timelinePixelsPerSecond_ = 120.0f;  // 1 秒を何 pixel で描くか。
	int32_t selectedTrackIndex_ = -1;  // 現在編集している Track 番号。
	int32_t selectedKeyIndex_ = -1;  // 現在編集している Keyframe 番号。
	int32_t selectedEventIndex_ = -1;  // 現在編集している Animation Event 番号。
	int32_t addPropertyTargetIndex_ = 0;  // Track 追加 Combo で選択中の Property 番号。
	int32_t previewGameObjectId_ = -1;  // Preview を適用している GameObject ID。
	int32_t recordingGameObjectId_ = -1;  // 記録開始後に別 GameObject へ誤記録しないための対象 ID。
	bool isPreviewPlaying_ = false;  // true の間は Timeline を実時間で進める。
	bool isPreviewActive_ = false;  // Backup 済みで Preview 値が Scene へ入っているか。
	bool isRecording_ = false;  // true の間は Inspector / Gizmo の変更値を Key 化する。
	bool isDirty_ = false;  // 保存後に Clip 内容が変化したか。

	void SynchronizeSelectedAsset();  // Project または Animation Component の選択から開く Clip を決める。
	void CreateAnimationClip();  // Assets/Animation に空の Clip を作り、選択 GameObject へ設定する。
	bool LoadAnimationClip(const std::string& filePath);  // 編集中 Preview を戻してから .animclip を読み込む。
	void SaveAnimationClip();  // 現在の編集コピーを animationClipPath_ へ書き戻す。
	void AssignClipToSelectedGameObject();  // 選択 GameObject に Animation Component と Clip パスを設定する。
	void DrawToolbar();  // 保存、再読込、Preview、Record、時間設定を描画する。
	void DrawTrackList();  // Property Track の追加、選択、削除を描画する。
	void DrawTimeline();  // 秒目盛り、Track 行、Keyframe、再生ヘッドを描画する。
	void DrawSelectedKeyEditor();  // 選択 Key の時刻、値、接線、補間を編集する。
	void DrawEventEditor();  // C++ Script / Effect へ渡す Animation Event を編集する。
	void AddPropertyTrack();  // 重複していない Property Track を追加する。
	int32_t FindOrCreateTrack(AnimationPropertyTarget target, float initialValue);  // 自動記録対象の Track と 0 秒の基準 Key を用意する。
	void AddOrUpdateKey(int32_t trackIndex, float value);  // 現在時刻の Key を更新し、なければ追加する。
	void RecordCurrentTransform();  // 選択 GameObject の位置・回転・スケールを現在時間へ一括記録する。
	void DeleteSelectedKey();  // 選択中 Key を Track から削除する。
	void SortSelectedTrack(float selectedTime);  // Key 移動後に時間順へ並べ、選択を維持する。
	void BeginRecording();  // Preview 姿勢を表示し、変更検出用の基準値を取得して自動記録を開始する。
	void EndRecording(bool restorePreview);  // 自動記録を終了し、必要なら編集前の Scene 姿勢へ戻す。
	void CaptureRecordingValues(const EditorGameObject& gameObject);  // 全対応プロパティの現在値を変更検出基準へ保存する。
	void BeginPreview();  // 選択 GameObject を Backup して Preview を開始する。
	void RestorePreview();  // Preview 対象を開始前の GameObject 値へ戻す。
	void ApplyPreview();  // 現在時刻の全 Track を選択 GameObject へ適用する。
	void UpdateRecording();  // Track 対象値の変更を検出して Keyframe に記録する。
	bool ReadProperty(const EditorGameObject& gameObject, AnimationPropertyTarget target, float& value) const;  // Scene 値を float Track 値へ変換する。
	bool WriteProperty(EditorGameObject& gameObject, AnimationPropertyTarget target, float value) const;  // float Track 値を Scene へ書き戻す。
	void SynchronizeRenderedScene();  // GameObject の Preview 変更を SceneView 描画配列へ同期する。
};

#pragma warning(pop)
