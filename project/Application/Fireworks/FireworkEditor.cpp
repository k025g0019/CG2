#include "Application/Fireworks/FireworkEditor.h"

#ifdef USE_IMGUI

#include "Application/Fireworks/FireworkPresets.h"
#include "motion/StringUtility.h"
#include "math/Vector&Matrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <Windows.h>
#include <commdlg.h>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string_view>
#include <utility>

#include "externals/imgui/imgui.h"

#pragma comment(lib, "comdlg32.lib")

namespace fireworks {
namespace {

constexpr float kPanelMargin = 10.0f;
constexpr float kTimelineHeight = 276.0f;
constexpr float kInspectorWidth = 360.0f;
constexpr int kLaneCount = 3;
constexpr float kRulerHeight = 27.0f;
constexpr float kLaneHeight = 30.0f;
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
	| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
constexpr ImGuiSliderFlags kClampDrag = ImGuiSliderFlags_AlwaysClamp;
constexpr char kReplaceShowPopup[] = "現在の演出を入れ替えますか？";

struct CueClip {
	int index;
	ImVec2 topLeft;
	ImVec2 bottomRight;

	bool Contains(const ImVec2& point) const {
		return point.x >= topLeft.x && point.x <= bottomRight.x
			&& point.y >= topLeft.y && point.y <= bottomRight.y;
	}
};

const char* PresetLabel(const FireworkCue& cue) {
	const FireworkPreset* preset = FindPreset(cue.presetId);
	return preset != nullptr ? preset->label : "不明";
}

bool PresetButton(const char* label, bool selected, float width) {
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.40f, 0.65f, 1.0f));
	}
	const bool pressed = ImGui::Button(label, {width, 0.0f});
	if (selected) {
		ImGui::PopStyleColor();
	}
	return pressed;
}

bool ValidPresetName(std::string_view name) {
	return !name.empty() && name != "." && name != ".."
		&& name.find_first_of("\\/:*?\"<>|") == std::string_view::npos;
}

bool ChooseWaveFile(std::string& path) {
	std::array<wchar_t, MAX_PATH> fileName{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrFilter = L"WAV ファイル (*.wav)\0*.wav\0\0";
	dialog.lpstrFile = fileName.data();
	dialog.nMaxFile = static_cast<DWORD>(fileName.size());
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	dialog.lpstrDefExt = L"wav";
	if (!GetOpenFileNameW(&dialog)) {
		return false;
	}

	std::filesystem::path selected(fileName.data());
	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(
		selected, std::filesystem::current_path(error), error);
	if (!error && !relative.empty()
		&& *relative.begin() != std::filesystem::path("..")) {
		selected = relative;
	}
	path = StringUtility::ConvertString(selected.wstring());
	return true;
}

bool DrawSoundSlot(const char* label, const char* id, FireworkSound& sound, std::string& status) {
	ImGui::PushID(id);
	ImGui::SeparatorText(label);
	ImGui::TextWrapped("WAV: %s", sound.path.empty() ? "未指定" : sound.path.c_str());
	bool edited = false;
	if (ImGui::Button("WAVを選ぶ")) {
		std::string selectedPath;
		if (ChooseWaveFile(selectedPath)) {
			if (selectedPath.empty() || selectedPath.size() > 512) {
				status = "効果音のファイル名を確認してください";
			} else {
				sound.path = std::move(selectedPath);
				edited = true;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("解除") && !sound.path.empty()) {
		sound.path.clear();
		edited = true;
	}
	edited |= ImGui::DragFloat("遅延 (秒)", &sound.delaySeconds,
		0.01f, 0.0f, kMaxSoundDelaySeconds, "%.2f", kClampDrag);
	edited |= ImGui::SliderFloat("音量", &sound.volume, 0.0f, 1.0f, "%.2f");
	ImGui::PopID();
	return edited;
}

std::uint32_t NextSeed(const FireworkShow& show) {
	std::uint32_t largestSeed = 0;
	for (const FireworkCue& cue : show.cues) {
		largestSeed = (std::max)(largestSeed, cue.seed);
	}
	return largestSeed + 1;
}

std::vector<CueClip> MakeClips(const FireworkShow& show, const ImVec2& start, float width) {
	std::vector<int> order(show.cues.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&show](int left, int right) {
		return show.cues[left].startFrame < show.cues[right].startFrame;
	});

	std::array<float, kLaneCount> laneEnd{};
	laneEnd.fill(start.x);
	std::vector<CueClip> clips;
	clips.reserve(order.size());
	const float scale = width / show.durationFrames;
	const float right = start.x + width;
	for (int index : order) {
		const FireworkCue& cue = show.cues[index];
		const FireworkPreset* preset = FindPreset(cue.presetId);
		const bool hasChildren = preset != nullptr
			&& preset->pattern == FireworkPattern::ThousandChrysanthemum;
		float lifeSeconds = hasChildren
			? (std::max)(cue.effect.sparkLife, cue.child.fuseSeconds
				+ cue.child.fuseSpreadSeconds + cue.child.sparkLife)
			: cue.effect.sparkLife;
		if (preset != nullptr && preset->pattern == FireworkPattern::Palm) {
			lifeSeconds += cue.effect.palmBranchLifeSeconds;
		} else if (preset != nullptr && preset->pattern == FireworkPattern::Willow) {
			lifeSeconds += cue.effect.willowTrailLifeSeconds;
		}
		const int endFrame = (std::min)(show.durationFrames,
			cue.startFrame + ToFrames(cue.effect.stageSeconds + lifeSeconds));
		const float left = (std::min)(start.x + cue.startFrame * scale, right - 8.0f);
		const float endX = std::clamp(start.x + endFrame * scale, left + 8.0f, right);
		auto lane = std::min_element(laneEnd.begin(), laneEnd.end());
		const float top = start.y + kRulerHeight + (lane - laneEnd.begin()) * kLaneHeight;
		*lane = endX + 3.0f;
		clips.push_back({index, {left, top + 2.0f}, {endX, top + kLaneHeight - 2.0f}});
	}
	return clips;
}

void DrawCueClip(ImDrawList& drawList, const CueClip& clip, const FireworkCue& cue, bool selected) {
	const ImU32 color = ImGui::ColorConvertFloat4ToU32({
		cue.effect.color.x, cue.effect.color.y, cue.effect.color.z, 1.0f
	});
	// 色に関係なく名前を読めるよう、暗いブロックと色の帯を分ける。
	drawList.AddRectFilled(clip.topLeft, clip.bottomRight,
		selected ? IM_COL32(57, 72, 96, 255) : IM_COL32(42, 51, 69, 255), 4.0f);
	drawList.AddRectFilled(clip.topLeft,
		{(std::min)(clip.topLeft.x + 5.0f, clip.bottomRight.x), clip.bottomRight.y}, color, 4.0f);
	if (selected) {
		drawList.AddRect(clip.topLeft, clip.bottomRight, IM_COL32(255, 255, 255, 255), 4.0f);
	}
	const char* label = PresetLabel(cue);
	if (clip.bottomRight.x - clip.topLeft.x >= ImGui::CalcTextSize(label).x + 17.0f) {
		const float textY = clip.topLeft.y + (clip.bottomRight.y - clip.topLeft.y - ImGui::GetFontSize()) * 0.5f;
		drawList.AddText({clip.topLeft.x + 10.0f, textY}, IM_COL32(255, 255, 255, 255), label);
	}
}

bool DrawEffectFields(EffectParameters& effect, bool aerial, bool palm, bool willow) {
	const float maxStageSeconds = aerial ? kMaxRiseSeconds : kMaxFountainSeconds;
	const int maxCount = aerial ? kMaxBurstCount : kMaxFountainRate;
	ImGui::PushItemWidth(110.0f);
	bool edited = ImGui::DragFloat(aerial ? "開花まで (秒)" : "放出時間 (秒)", &effect.stageSeconds,
		0.02f, kMinStageSeconds, maxStageSeconds, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("打上速度", &effect.launchSpeed,
		0.1f, 0.0f, kMaxSpeed, "%.1f", kClampDrag);
	ImGui::TextUnformatted(aerial ? "打ち上げ方向" : "吹き上げ方向");
	edited |= ImGui::DragFloat("左右角 (度)", &effect.launchSideAngleDegrees,
		1.0f, -kMaxLaunchTiltDegrees, kMaxLaunchTiltDegrees, "%.0f", kClampDrag);
	edited |= ImGui::DragFloat("前後角 (度)", &effect.launchDepthAngleDegrees,
		1.0f, -kMaxLaunchTiltDegrees, kMaxLaunchTiltDegrees, "%.0f", kClampDrag);
	ImGui::TextDisabled("0度で真上 / 左右の＋は右 / 前後の＋はZ方向");
	if (palm) {
		const bool armsEdited = ImGui::DragInt("枝の数", &effect.palmArmCount,
			1.0f, kMinPalmArms, kMaxPalmArms, "%d", kClampDrag);
		edited |= armsEdited;
		if (armsEdited && effect.particleCount < effect.palmArmCount) {
			effect.particleCount = effect.palmArmCount;
			edited = true;
		}
	}
	const char* countLabel = palm ? "枝の火花数" : aerial ? "粒子数" : "粒子数 / 秒";
	edited |= ImGui::DragInt(countLabel, &effect.particleCount,
		aerial ? 1.0f : 20.0f, palm ? effect.palmArmCount : 1,
		maxCount, "%d", kClampDrag);
	const char* spreadLabel = willow ? "柳の放出速度"
		: aerial ? "開花の広がり" : "広がる速さ";
	edited |= ImGui::DragFloat(spreadLabel, &effect.spreadSpeed,
		0.1f, 0.0f, kMaxSpeed, "%.1f", kClampDrag);
	edited |= ImGui::DragFloat("火花が残る時間 (秒)", &effect.sparkLife,
		0.02f, kMinSparkLife, kMaxSparkLife, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("火花の太さ", &effect.sparkWidth,
		0.002f, kMinWidth, kMaxWidth, "%.3f", kClampDrag);
	effect.widthJitter = (std::min)(effect.widthJitter, effect.sparkWidth - kMinWidth);
	edited |= ImGui::DragFloat("火花の尾 (秒)", &effect.trailSeconds,
		0.002f, 0.0f, kMaxTrailSeconds, "%.3f", kClampDrag);
	float color[] = {effect.color.x, effect.color.y, effect.color.z, effect.color.w};
	if (ImGui::ColorEdit4("色", color)) {
		effect.color = {color[0], color[1], color[2], color[3]};
		edited = true;
	}
	ImGui::PopItemWidth();
	return edited;
}

bool DrawAdvancedEffectFields(EffectParameters& effect) {
	bool edited = ImGui::DragFloat("速度のばらつき", &effect.speedJitter,
		0.1f, 0.0f, kMaxSpeed, "%.1f", kClampDrag);
	edited |= ImGui::DragFloat("重力", &effect.gravity, 0.1f, 0.0f, kMaxGravity, "%.1f", kClampDrag);
	const float maxJitter = effect.sparkWidth - kMinWidth;
	ImGui::BeginDisabled(maxJitter == 0.0f);
	edited |= ImGui::DragFloat("幅のばらつき", &effect.widthJitter,
		0.002f, 0.0f, maxJitter, "%.3f", kClampDrag);
	ImGui::EndDisabled();
	return edited;
}

bool DrawLaunchTrailFields(EffectParameters& effect) {
	if (!ImGui::CollapsingHeader("打ち上げ軌跡", ImGuiTreeNodeFlags_DefaultOpen)) {
		return false;
	}
	// ラベルを読める幅を残す。0 本/秒にすると軌跡は描かれない。
	ImGui::PushItemWidth(110.0f);
	bool edited = ImGui::DragInt("線の数 / 秒", &effect.launchTrailRate,
		1.0f, 0, kMaxLaunchTrailRate, "%d", kClampDrag);
	edited |= ImGui::DragFloat("残る時間 (秒)", &effect.launchTrailLifeSeconds,
		0.01f, 0.0f, kMaxLaunchTrailLifeSeconds, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("線の太さ", &effect.launchTrailWidth,
		0.005f, 0.0f, kMaxLaunchTrailWidth, "%.3f", kClampDrag);
	edited |= ImGui::DragFloat("線の重なり", &effect.launchTrailOverlap,
		0.01f, 0.0f, kMaxLaunchTrailOverlap, "%.2f", kClampDrag);
	float color[] = {effect.launchTrailColor.x, effect.launchTrailColor.y,
		effect.launchTrailColor.z, effect.launchTrailColor.w};
	if (ImGui::ColorEdit4("軌跡の色", color)) {
		effect.launchTrailColor = {color[0], color[1], color[2], color[3]};
		edited = true;
	}
	ImGui::PopItemWidth();
	return edited;
}

bool DrawChildFields(ChildFireworkSettings& child) {
	if (!ImGui::CollapsingHeader("小花火の設定", ImGuiTreeNodeFlags_DefaultOpen)) {
		return false;
	}
	ImGui::TextWrapped("親玉から小玉が飛び、時間差で各位置に開きます。");
	ImGui::PushItemWidth(110.0f);
	bool edited = ImGui::DragInt("小玉の数", &child.count,
		1.0f, 1, kMaxChildFireworks, "%d", kClampDrag);
	edited |= ImGui::DragFloat("小玉の飛ぶ速さ", &child.travelSpeed,
		0.1f, 0.0f, kMaxSpeed, "%.1f", kClampDrag);
	edited |= ImGui::DragFloat("小玉が開くまで (秒)", &child.fuseSeconds,
		0.01f, 0.1f, kMaxChildFuseSeconds, "%.2f", kClampDrag);
	const float maxSpread = (std::min)(kMaxChildFuseSpreadSeconds, child.fuseSeconds - 0.05f);
	child.fuseSpreadSeconds = (std::min)(child.fuseSpreadSeconds, maxSpread);
	edited |= ImGui::DragFloat("開く時刻のばらつき", &child.fuseSpreadSeconds,
		0.01f, 0.0f, maxSpread, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("小玉の太さ", &child.shellWidth,
		0.002f, kMinWidth, kMaxWidth, "%.3f", kClampDrag);
	edited |= ImGui::DragFloat("小玉の尾 (秒)", &child.shellTrailSeconds,
		0.005f, 0.0f, kMaxTrailSeconds, "%.3f", kClampDrag);
	edited |= ImGui::DragInt("小花火の粒子数", &child.particleCount,
		1.0f, 1, kMaxChildSparkCount, "%d", kClampDrag);
	edited |= ImGui::DragFloat("小花火の広がる速さ", &child.burstSpeed,
		0.1f, 0.0f, kMaxSpeed, "%.1f", kClampDrag);
	edited |= ImGui::DragFloat("小花火の寿命 (秒)", &child.sparkLife,
		0.02f, kMinSparkLife, kMaxSparkLife, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("小花火の粒子幅", &child.sparkWidth,
		0.002f, kMinWidth, kMaxWidth, "%.3f", kClampDrag);
	edited |= ImGui::DragFloat("小花火の尾 (秒)", &child.trailSeconds,
		0.005f, 0.0f, kMaxTrailSeconds, "%.3f", kClampDrag);
	edited |= ImGui::DragFloat("小花火の重力", &child.gravity,
		0.1f, 0.0f, kMaxGravity, "%.1f", kClampDrag);
	float color[] = {child.color.x, child.color.y, child.color.z, child.color.w};
	if (ImGui::ColorEdit4("小花火の色", color)) {
		child.color = {color[0], color[1], color[2], color[3]};
		edited = true;
	}
	edited |= ImGui::DragFloat("小玉ごとの色の差", &child.colorVariation,
		0.01f, 0.0f, 1.0f, "%.2f", kClampDrag);
	ImGui::PopItemWidth();
	return edited;
}

bool DrawCrownFields(EffectParameters& effect) {
	if (!ImGui::CollapsingHeader("冠菊の垂れ方", ImGuiTreeNodeFlags_DefaultOpen)) {
		return false;
	}
	ImGui::TextWrapped("菊と同じ球状に開き、横の動きが遅くなって光が垂れます。");
	const float maxStart = (std::min)(kMaxCrownDroopStartSeconds,
		(std::max)(0.0f, effect.sparkLife - 0.01f));
	bool edited = false;
	if (effect.crownDroopStartSeconds > maxStart) {
		effect.crownDroopStartSeconds = maxStart;
		edited = true;
	}
	ImGui::PushItemWidth(110.0f);
	edited |= ImGui::DragFloat("垂れ始めるまで (秒)", &effect.crownDroopStartSeconds,
		0.01f, 0.0f, maxStart, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("横方向の減速", &effect.crownHorizontalDrag,
		0.05f, 0.0f, kMaxCrownHorizontalDrag, "%.2f", kClampDrag);
	ImGui::PopItemWidth();
	return edited;
}

bool DrawPalmFields(EffectParameters& effect) {
	if (!ImGui::CollapsingHeader("椰子の枝", ImGuiTreeNodeFlags_DefaultOpen)) {
		return false;
	}
	ImGui::TextWrapped("太い光が通った位置に枝を残し、重力で曲がる軌跡を描きます。");
	ImGui::PushItemWidth(110.0f);
	bool edited = ImGui::DragFloat("枝が残る時間 (秒)", &effect.palmBranchLifeSeconds,
		0.02f, 0.0f, kMaxPalmBranchLifeSeconds, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("枝の広がり", &effect.palmBranchSpread,
		0.002f, 0.0f, kMaxPalmBranchSpread, "%.3f", kClampDrag);
	ImGui::PopItemWidth();
	return edited;
}

bool DrawWillowFields(EffectParameters& effect) {
	if (!ImGui::CollapsingHeader("柳の枝", ImGuiTreeNodeFlags_DefaultOpen)) {
		return false;
	}
	ImGui::TextWrapped("弱い速度で一方向へまとまって飛び、重力で下へ垂れます。");
	ImGui::PushItemWidth(110.0f);
	bool edited = false;
	const float maxGlowHold = (std::max)(0.0f, effect.sparkLife - kMinSparkLife);
	if (effect.willowGlowHoldSeconds > maxGlowHold) {
		effect.willowGlowHoldSeconds = maxGlowHold;
		edited = true;
	}
	edited |= ImGui::DragInt("光の群れの数", &effect.willowGroupCount,
		1.0f, 1, kMaxWillowGroups, "%d", kClampDrag);
	edited |= ImGui::DragFloat("枝の広がり", &effect.willowGroupSpread,
		0.005f, 0.0f, kMaxWillowGroupSpread, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("放出方向 (度)", &effect.willowDirectionDegrees,
		1.0f, kMinWillowDirectionDegrees, kMaxWillowDirectionDegrees, "%.0f", kClampDrag);
	ImGui::TextDisabled("0度: 右 / 90度: 上");
	edited |= ImGui::DragFloat("枝が残る時間 (秒)", &effect.willowTrailLifeSeconds,
		0.02f, 0.0f, kMaxWillowTrailLifeSeconds, "%.2f", kClampDrag);
	edited |= ImGui::DragFloat("明るさを保つ時間 (秒)", &effect.willowGlowHoldSeconds,
		0.02f, 0.0f, maxGlowHold, "%.2f", kClampDrag);
	ImGui::PopItemWidth();
	return edited;
}

bool PickPosition(const Matrix4x4& viewProjection, const ImGuiViewport& viewport,
                  const ImVec2& mouse, Vector3& position) {
	const Matrix4x4 inverse = Inverse(viewProjection);
	const float x = 2.0f * (mouse.x - viewport.Pos.x) / viewport.Size.x - 1.0f;
	const float y = 1.0f - 2.0f * (mouse.y - viewport.Pos.y) / viewport.Size.y;
	const Vector3 nearPoint = Transform({x, y, 0.0f}, inverse);
	const Vector3 ray = Transform({x, y, 1.0f}, inverse) - nearPoint;
	if (std::abs(ray.z) < 0.0001f) {
		return false;
	}
	// 選択中のキューの Z を保ち、プレビュー上のクリックを同じ Z 平面へ落とす。
	const float distance = (position.z - nearPoint.z) / ray.z;
	const Vector3 hit = nearPoint + ray * distance;
	if (!(distance >= 0.0f && distance <= 1.0f
		&& std::abs(hit.x) <= kPositionLimit && std::abs(hit.y) <= kPositionLimit)) {
		return false;
	}
	position.x = hit.x;
	position.y = hit.y;
	return true;
}

void DrawLaunchMarker(const Matrix4x4& viewProjection, const ImGuiViewport& viewport,
                      const Vector3& position, bool placing) {
	const float w = position.x * viewProjection.matrix[0][3]
		+ position.y * viewProjection.matrix[1][3]
		+ position.z * viewProjection.matrix[2][3] + viewProjection.matrix[3][3];
	if (w <= 0.0f) {
		return;
	}
	const Vector3 projected = Transform(position, viewProjection);
	if (std::abs(projected.x) > 1.0f || std::abs(projected.y) > 1.0f
		|| projected.z < 0.0f || projected.z > 1.0f) {
		return;
	}

	const ImVec2 screen{
		viewport.Pos.x + (projected.x + 1.0f) * viewport.Size.x * 0.5f,
		viewport.Pos.y + (1.0f - projected.y) * viewport.Size.y * 0.5f
	};
	const ImU32 color = placing ? IM_COL32(255, 201, 101, 255) : IM_COL32(122, 211, 255, 255);
	ImDrawList& drawList = *ImGui::GetBackgroundDrawList();
	drawList.AddCircle(screen, 7.0f, color, 0, 1.5f);
	drawList.AddLine({screen.x - 11.0f, screen.y}, {screen.x + 11.0f, screen.y}, color);
	drawList.AddLine({screen.x, screen.y - 11.0f}, {screen.x, screen.y + 11.0f}, color);
}

} // namespace

FireworkEditor::FireworkEditor(FireworkShow& show, FireworkPlayer& player)
	: show_(show), player_(player) {
}

void FireworkEditor::Draw(const Matrix4x4& viewProjection, bool& useBillboard, std::size_t particleCount) {
	if (!visible_) {
		return;
	}
	DrawTimelineWindow(particleCount);
	DrawInspector(useBillboard);
	DrawPreviewPlacement(viewProjection);
}

void FireworkEditor::ToggleVisible() {
	visible_ = !visible_;
	placing_ = false;
	scrubbing_ = false;
	draggedCue_ = -1;
}

void FireworkEditor::SetStatus(std::string status) {
	status_ = std::move(status);
}

void FireworkEditor::DrawTimelineWindow(std::size_t particleCount) {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({viewport->WorkPos.x + kPanelMargin,
		viewport->WorkPos.y + viewport->WorkSize.y - kTimelineHeight - kPanelMargin});
	ImGui::SetNextWindowSize({viewport->WorkSize.x - 2.0f * kPanelMargin, kTimelineHeight});
	if (ImGui::Begin("花火のタイムライン", nullptr, kPanelFlags)) {
		if (ImGui::Button(player_.IsPlaying() ? "一時停止" : "再生", {90.0f, 0.0f})) {
			player_.TogglePlayback(show_);
		}
		ImGui::SameLine();
		if (ImGui::Button("最初から")) {
			player_.Restart();
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("時刻 %.1f / %.1f 秒", ToSeconds(player_.CurrentFrame()), ToSeconds(show_.durationFrames));
		ImGui::SameLine();
		DrawFileActions();
		float durationSeconds = ToSeconds(show_.durationFrames);
		int minimumFrames = kFramesPerSecond;
		for (const FireworkCue& cue : show_.cues) {
			minimumFrames = (std::max)(minimumFrames, cue.startFrame + 1);
		}
		if (ImGui::DragFloat("演出時間 (秒)", &durationSeconds, 0.1f,
			ToSeconds(minimumFrames), static_cast<float>(kMaxShowSeconds), "%.1f", kClampDrag)) {
			show_.durationFrames = std::clamp(ToFrames(durationSeconds),
				minimumFrames, kMaxShowSeconds * kFramesPerSecond);
			MarkEdited();
		}

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("1. 種類を選ぶ");
		ImGui::SameLine();
		const auto presets = GetPresets();
		for (std::size_t index = 0; index < presets.size(); ++index) {
			if (index > 0) {
				ImGui::SameLine();
			}
			if (PresetButton(presets[index].label, index == addPresetIndex_, 110.0f)) {
				addPresetIndex_ = index;
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(show_.cues.size() >= kMaxCues);
		if (ImGui::Button("この時刻に追加")) {
			AddCue();
		}
		ImGui::EndDisabled();

		DrawTimelineCanvas();
		ImGui::Text("粒子 %zu", particleCount);
		if (dirty_) {
			ImGui::SameLine();
			if (status_.empty()) {
				ImGui::TextUnformatted("未保存の変更があります");
			} else {
				ImGui::Text("未保存  |  %s", status_.c_str());
			}
		} else if (!status_.empty()) {
			ImGui::SameLine();
			ImGui::TextUnformatted(status_.c_str());
		}
	}
	ImGui::End();
}

void FireworkEditor::DrawTimelineCanvas() {
	const float width = ImGui::GetContentRegionAvail().x;
	if (width < 32.0f) {
		return;
	}
	const float height = kRulerHeight + kLaneCount * kLaneHeight;
	ImGui::InvisibleButton("##TimelineCanvas", {width, height});
	const ImVec2 start = ImGui::GetItemRectMin();
	const ImVec2 end = ImGui::GetItemRectMax();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const auto clips = MakeClips(show_, start, width);
	const auto frameAt = [&](float x) {
		const float fraction = std::clamp((x - start.x) / width, -1.0f, 2.0f);
		return static_cast<int>(std::lround(fraction * show_.durationFrames));
	};

	if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		draggedCue_ = -1;
		for (auto clip = clips.rbegin(); clip != clips.rend(); ++clip) {
			if (clip->Contains(mouse)) {
				if (draggedCue_ < 0 || clip->index == selectedCue_) {
					draggedCue_ = clip->index;
				}
			}
		}
		if (draggedCue_ >= 0) {
			selectedCue_ = draggedCue_;
			dragOffsetFrames_ = frameAt(mouse.x) - show_.cues[draggedCue_].startFrame;
		}
		scrubbing_ = draggedCue_ < 0;
		placing_ = false;
	}
	if (ImGui::IsItemActive() && ImGui::IsMousePosValid(&mouse)) {
		if (scrubbing_) {
			player_.SetPlaying(false);
			player_.Seek(std::clamp(frameAt(mouse.x), 0, show_.durationFrames));
		} else if (draggedCue_ >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			const int frame = std::clamp(frameAt(mouse.x) - dragOffsetFrames_, 0, show_.durationFrames - 1);
			FireworkCue& cue = show_.cues[draggedCue_];
			if (frame != cue.startFrame) {
				cue.startFrame = frame;
				player_.SetPlaying(false);
				MarkEdited();
			}
		}
	} else {
		scrubbing_ = false;
		draggedCue_ = -1;
	}

	ImDrawList& drawList = *ImGui::GetWindowDrawList();
	drawList.PushClipRect(start, end, true);
	drawList.AddRectFilled(start, end, IM_COL32(20, 24, 33, 255), 4.0f);
	const float pixelsPerFrame = width / show_.durationFrames;
	const float duration = ToSeconds(show_.durationFrames);
	const int tickSeconds = duration > 60.0f ? 10 : duration > 24.0f ? 5 : duration > 10.0f ? 2 : 1;
	for (int second = 0; second <= duration; second += tickSeconds) {
		const float x = start.x + second * kFramesPerSecond * pixelsPerFrame;
		drawList.AddLine({x, start.y}, {x, end.y}, IM_COL32(68, 75, 91, 150));
		char label[16]{};
		std::snprintf(label, sizeof(label), "%d秒", second);
		drawList.AddText({x + 4.0f, start.y + 2.0f}, IM_COL32(224, 232, 244, 255), label);
	}
	for (const CueClip& clip : clips) {
		if (clip.index != selectedCue_) {
			DrawCueClip(drawList, clip, show_.cues[clip.index], false);
		}
	}
	for (const CueClip& clip : clips) {
		if (clip.index == selectedCue_) {
			DrawCueClip(drawList, clip, show_.cues[clip.index], true);
			break;
		}
	}
	const float playheadX = start.x + player_.CurrentFrame() * pixelsPerFrame;
	drawList.AddLine({playheadX, start.y}, {playheadX, end.y}, IM_COL32(255, 185, 90, 255), 2.0f);
	drawList.PopClipRect();

	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		for (auto clip = clips.rbegin(); clip != clips.rend(); ++clip) {
			if (clip->Contains(mouse)) {
				const FireworkCue& cue = show_.cues[clip->index];
			ImGui::SetTooltip("%s  %.2f 秒  ドラッグで移動", PresetLabel(cue), ToSeconds(cue.startFrame));
				return;
			}
		}
		ImGui::SetTooltip("クリックで時刻を選択");
	}
}

void FireworkEditor::DrawInspector(bool& useBillboard) {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - kInspectorWidth - kPanelMargin,
		viewport->WorkPos.y + kPanelMargin});
	ImGui::SetNextWindowSize({kInspectorWidth,
		viewport->WorkSize.y - kTimelineHeight - 3.0f * kPanelMargin});

	if (!ImGui::Begin("花火の設定", nullptr, kPanelFlags)) {
		ImGui::End();
		return;
	}
	ImGui::TextUnformatted("2. 花火を調整");
	if (selectedCue_ < 0 && !show_.cues.empty()) {
		selectedCue_ = 0;
	}
	FireworkCue* cue = SelectedCue();
	if (cue == nullptr) {
		ImGui::TextWrapped("下で時刻と種類を選んで花火を追加してください。");
		ImGui::End();
		return;
	}
	ImGui::BeginDisabled(selectedCue_ == 0);
	if (ImGui::Button("前の花火")) {
		--selectedCue_;
		placing_ = false;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(static_cast<std::size_t>(selectedCue_ + 1) >= show_.cues.size());
	if (ImGui::Button("次の花火")) {
		++selectedCue_;
		placing_ = false;
	}
	ImGui::EndDisabled();
	cue = SelectedCue();
	ImGui::Text("%d / %zu  %s  %.2f 秒", selectedCue_ + 1,
		show_.cues.size(), PresetLabel(*cue), ToSeconds(cue->startFrame));

	ImGui::PushID(selectedCue_);
	bool edited = false;
	ImGui::TextUnformatted("種類");
	const auto presets = GetPresets();
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	for (std::size_t index = 0; index < presets.size(); ++index) {
		if (index % 2 == 1) {
			ImGui::SameLine();
		}
		const FireworkPreset& preset = presets[index];
		const bool same = preset.id == cue->presetId;
		if (PresetButton(preset.label, same, buttonWidth) && !same) {
			cue->presetId = preset.id;
			cue->effect = preset.defaults;
			cue->child = preset.childDefaults;
			edited = true;
		}
	}
	float seconds = ToSeconds(cue->startFrame);
	if (ImGui::DragFloat("開始時刻 (秒)", &seconds, 0.02f, 0.0f,
		ToSeconds(show_.durationFrames - 1), "%.2f", kClampDrag)) {
		cue->startFrame = (std::min)(ToFrames(seconds), show_.durationFrames - 1);
		edited = true;
	}
	if (ImGui::Button(placing_ ? "位置指定をやめる" : "画面で位置を指定")) {
		placing_ = !placing_;
	}
	if (placing_) {
		ImGui::TextWrapped("プレビューをクリックして発射位置を決めます。");
	}
	const FireworkPreset* preset = FindPreset(cue->presetId);
	const bool aerial = preset != nullptr && preset->IsAerial();
	const bool palm = preset != nullptr && preset->id == "palm";
	edited |= DrawEffectFields(cue->effect, aerial, palm,
		preset != nullptr && preset->pattern == FireworkPattern::Willow);
	if (preset != nullptr && preset->pattern == FireworkPattern::ThousandChrysanthemum) {
		edited |= DrawChildFields(cue->child);
	}
	if (preset != nullptr && preset->pattern == FireworkPattern::CrownChrysanthemum) {
		edited |= DrawCrownFields(cue->effect);
	}
	if (palm) {
		edited |= DrawPalmFields(cue->effect);
	}
	if (preset != nullptr && preset->pattern == FireworkPattern::Willow) {
		edited |= DrawWillowFields(cue->effect);
	}
	if (aerial) {
		edited |= DrawLaunchTrailFields(cue->effect);
	}
	const bool appliedPreset = DrawEffectPresetLibrary(*cue);
	edited |= appliedPreset;
	if (ImGui::CollapsingHeader("効果音", ImGuiTreeNodeFlags_DefaultOpen)) {
		edited |= DrawSoundSlot(aerial ? "打ち上げ音（ヒュー）" : "開始音",
			"LaunchSound", cue->launchSound, status_);
		edited |= DrawSoundSlot(aerial ? "開花音（ボン）" : "放出終了音",
			"BurstSound", cue->burstSound, status_);
	}
	if (ImGui::CollapsingHeader("詳細設定")) {
		float position[] = {cue->position.x, cue->position.y, cue->position.z};
		if (ImGui::DragFloat3("位置 XYZ", position, 0.1f,
			-kPositionLimit, kPositionLimit, "%.2f", kClampDrag)) {
			cue->position = {position[0], position[1], position[2]};
			edited = true;
		}
		edited |= DrawAdvancedEffectFields(cue->effect);
		ImGui::Checkbox("板ポリをカメラに向ける", &useBillboard);
		if (ImGui::Button("花火を削除")) {
			show_.cues.erase(show_.cues.begin() + selectedCue_);
			selectedCue_ = -1;
			placing_ = false;
			MarkEdited();
			ImGui::PopID();
			ImGui::End();
			return;
		}
	}
	if (edited) {
		MarkEdited();
	}
	if (appliedPreset) {
		status_ = "設定プリセットを適用しました";
	}
	ImGui::PopID();
	ImGui::End();
}

bool FireworkEditor::DrawEffectPresetLibrary(FireworkCue& cue) {
	if (!ImGui::CollapsingHeader("設定プリセット")) {
		return false;
	}
	ImGui::InputText("名前", presetName_.data(), presetName_.size());
	if (ImGui::Button("現在の設定を保存")) {
		const std::string_view name(presetName_.data());
		const std::wstring wideName = StringUtility::ConvertString(std::string(name));
		if (!ValidPresetName(name) || wideName.empty()) {
			status_ = "プリセット名を確認してください";
		} else {
			const std::filesystem::path path = std::filesystem::path(kEffectPresetDirectory)
				/ (wideName + L".fwpreset");
			std::string error;
			status_ = SaveEffectPreset(path, cue, error)
				? "設定プリセットを保存しました" : "保存できませんでした: " + error;
		}
	}

	std::vector<std::filesystem::path> files;
	std::error_code fileError;
	const std::filesystem::path directory(kEffectPresetDirectory);
	for (std::filesystem::directory_iterator it(directory, fileError), end;
		!fileError && it != end; it.increment(fileError)) {
		std::error_code entryError;
		if (it->is_regular_file(entryError) && it->path().extension() == L".fwpreset") {
			files.push_back(it->path());
		}
	}
	std::sort(files.begin(), files.end());
	if (fileError && fileError != std::errc::no_such_file_or_directory) {
		status_ = "設定プリセットの一覧を読めませんでした";
	}
	bool applied = false;
	if (ImGui::BeginChild("##PresetFiles", {0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
		for (const std::filesystem::path& path : files) {
			const std::string label = StringUtility::ConvertString(path.stem().wstring());
			if (ImGui::Button(label.c_str(), {-1.0f, 0.0f})) {
				std::string error;
				if (LoadEffectPreset(path, cue, error)) {
					applied = true;
				} else {
					status_ = "適用できませんでした: " + error;
				}
			}
		}
	}
	ImGui::EndChild();
	return applied;
}

void FireworkEditor::DrawPreviewPlacement(const Matrix4x4& viewProjection) {
	FireworkCue* cue = SelectedCue();
	if (cue == nullptr) {
		return;
	}
	const ImGuiViewport& viewport = *ImGui::GetMainViewport();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	Vector3 position = cue->position;
	const bool inPreview = !ImGui::GetIO().WantCaptureMouse && ImGui::IsMousePosValid(&mouse)
		&& viewport.Size.x > 0.0f && viewport.Size.y > 0.0f
		&& mouse.x >= viewport.Pos.x && mouse.x < viewport.Pos.x + viewport.Size.x
		&& mouse.y >= viewport.Pos.y && mouse.y < viewport.Pos.y + viewport.Size.y;
	if (placing_ && inPreview && PickPosition(viewProjection, viewport, mouse, position)) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			cue->position = position;
			placing_ = false;
			MarkEdited();
		}
	}
	DrawLaunchMarker(viewProjection, viewport, position, placing_);
}

void FireworkEditor::DrawFileActions() {
	if (ImGui::Button("新規")) {
		RequestFileAction(FileAction::New);
	}
	ImGui::SameLine();
	if (ImGui::Button("保存")) {
		if (SaveShow(kSavedShowPath, show_, status_)) {
			status_ = "保存しました";
			dirty_ = false;
		} else {
			status_ = "保存できませんでした: " + status_;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("読込")) {
		RequestFileAction(FileAction::Load);
	}
	ImGui::SameLine();
	if (ImGui::Button("見本")) {
		RequestFileAction(FileAction::Example);
	}

	if (ImGui::BeginPopupModal(kReplaceShowPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("保存していない変更があります。");
		if (ImGui::Button("入れ替える")) {
			ApplyFileAction(pendingFileAction_);
			pendingFileAction_ = FileAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("やめる")) {
			pendingFileAction_ = FileAction::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FireworkEditor::RequestFileAction(FileAction action) {
	if (dirty_) {
		pendingFileAction_ = action;
		ImGui::OpenPopup(kReplaceShowPopup);
		return;
	}
	ApplyFileAction(action);
}

void FireworkEditor::ApplyFileAction(FileAction action) {
	if (action == FileAction::None) {
		return;
	}
	if (action == FileAction::New) {
		show_ = FireworkShow{};
		status_ = "新しい演出を作成しました";
	} else {
		const char* path = action == FileAction::Load ? kSavedShowPath : kExampleShowPath;
		if (!LoadShow(path, show_, status_)) {
			status_ = "演出を読み込めませんでした: " + status_;
			return;
		}
		status_ = action == FileAction::Load ? "保存した演出を読み込みました" : "見本を読み込みました";
	}
	dirty_ = action == FileAction::New;
	selectedCue_ = -1;
	draggedCue_ = -1;
	placing_ = false;
	player_.SetPlaying(false);
	player_.Seek(0);
	player_.Refresh();
}

void FireworkEditor::AddCue() {
	if (show_.cues.size() >= kMaxCues) {
		return;
	}
	const int frame = (std::min)(player_.CurrentFrame(), show_.durationFrames - 1);
	FireworkCue cue = MakeCue(GetPresets()[addPresetIndex_], frame, NextSeed(show_));
	cue.startFrame = frame;
	show_.cues.push_back(std::move(cue));
	selectedCue_ = static_cast<int>(show_.cues.size()) - 1;
	placing_ = false;
	player_.Seek(frame);
	MarkEdited();
}

void FireworkEditor::MarkEdited() {
	dirty_ = true;
	status_.clear();
	player_.Refresh();
}

FireworkCue* FireworkEditor::SelectedCue() {
	if (selectedCue_ < 0 || static_cast<std::size_t>(selectedCue_) >= show_.cues.size()) {
		return nullptr;
	}
	return &show_.cues[selectedCue_];
}

} // namespace fireworks

#endif
