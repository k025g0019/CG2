#pragma once

#ifdef USE_IMGUI

#include <array>

#include "Application/Fireworks/FireworkPlayer.h"
#include "math/Matrix.h"

namespace fireworks {

class FireworkEditor {
public:
	FireworkEditor(FireworkShow& show, FireworkPlayer& player);

	void Draw(const Matrix4x4& viewProjection, bool& useBillboard, std::size_t particleCount);
	void ToggleVisible();
	void SetStatus(std::string status);

private:
	enum class FileAction { None, New, Load, Example };

	void DrawTimelineWindow(std::size_t particleCount);
	void DrawTimelineCanvas();
	void DrawInspector(bool& useBillboard);
	bool DrawEffectPresetLibrary(FireworkCue& cue);
	void DrawPreviewPlacement(const Matrix4x4& viewProjection);
	void DrawFileActions();
	void RequestFileAction(FileAction action);
	void ApplyFileAction(FileAction action);
	void AddCue();
	void MarkEdited();
	FireworkCue* SelectedCue();

	FireworkShow& show_;
	FireworkPlayer& player_;
	std::string status_;
	std::array<char, 128> presetName_{};
	std::size_t addPresetIndex_ = 0;
	int selectedCue_ = -1;
	int draggedCue_ = -1;
	int dragOffsetFrames_ = 0;
	FileAction pendingFileAction_ = FileAction::None;
	bool visible_ = true;
	bool dirty_ = false;
	bool scrubbing_ = false;
	bool placing_ = false;
};

} // namespace fireworks

#endif
