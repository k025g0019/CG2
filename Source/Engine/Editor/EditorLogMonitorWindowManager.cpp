#pragma warning(disable : 4189 4514)

#include "EditorLogMonitorWindowManager.h"

#include "EditorLogMonitorManager.h"
#include "EditorSharedState.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace EditorSharedState;

namespace {
	constexpr char kPresetDirectory[] = "Logs/Presets";

	int32_t FindGameObjectFieldEntryIndex(
		std::vector<LogWatchEntry>& entries, int32_t gameObjectId, const char* fieldName, int32_t vectorComponent) {
		for (size_t index = 0u; index < entries.size(); ++index) {
			const LogWatchEntry& entry = entries[index];
			if (entry.targetKind == LogWatchTargetKind::GameObjectField &&
				entry.gameObjectId == gameObjectId &&
				entry.gameObjectFieldName == fieldName &&
				entry.vectorComponent == vectorComponent) {
				return static_cast<int32_t>(index);
			}
		}
		return -1;
	}

	int32_t FindComponentFieldEntryIndex(
		std::vector<LogWatchEntry>& entries,
		int32_t gameObjectId,
		EditorComponentType componentType,
		const std::string& fieldKey,
		int32_t vectorComponent) {
		for (size_t index = 0u; index < entries.size(); ++index) {
			const LogWatchEntry& entry = entries[index];
			if (entry.targetKind == LogWatchTargetKind::ComponentField &&
				entry.gameObjectId == gameObjectId &&
				entry.componentType == componentType &&
				entry.componentFieldKey == fieldKey &&
				entry.vectorComponent == vectorComponent) {
				return static_cast<int32_t>(index);
			}
		}
		return -1;
	}
}

void EditorLogMonitorWindowManager::Initialize() {
}

void EditorLogMonitorWindowManager::Update() {
}

void EditorLogMonitorWindowManager::Draw() {
#ifdef USE_IMGUI
	if (!g_isLogMonitorWindowVisible) {
		return;
	}

	if (!ImGui::Begin("ログ監視###LogMonitor", &g_isLogMonitorWindowVisible, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();

	if (ImGui::Button("今すぐ記録")) {
		logMonitor.CaptureNow();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("取得Modeに関係なく、有効な対象を今この瞬間の値でLogへ1回書き出す。");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("出力先: Logs/RuntimeLog.log (pipe区切り, Play開始ごとにリセット)");

	if (logMonitor.GetDroppedEntryCount() > 0u) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Drop累計: %u", logMonitor.GetDroppedEntryCount());
	}

	ImGui::Separator();
	DrawPresetBar();
	ImGui::Separator();

	if (ImGui::BeginTabBar("LogMonitorTabs")) {
		if (ImGui::BeginTabItem("GameObject / Component")) {
			DrawTargetTab();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("System")) {
			DrawSystemTab();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("監視中一覧")) {
			DrawWatchListTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
#endif
}

void EditorLogMonitorWindowManager::DrawPresetBar() {
#ifdef USE_IMGUI
	EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();

	if (presetNameBuffer_[0] == '\0') {
		const std::string& currentPresetName = logMonitor.GetPresetName();
		const size_t copyLength = (std::min)(currentPresetName.size(), sizeof(presetNameBuffer_) - 1u);
		std::copy_n(currentPresetName.data(), copyLength, presetNameBuffer_);
		presetNameBuffer_[copyLength] = '\0';
	}

	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputText("Preset名", presetNameBuffer_, sizeof(presetNameBuffer_));
	ImGui::SameLine();

	const std::string presetName = presetNameBuffer_[0] != '\0' ? presetNameBuffer_ : "Default";
	const std::string presetPath = std::string(kPresetDirectory) + "/" + presetName + ".txt";

	if (ImGui::Button("Presetへ保存")) {
		logMonitor.SetPresetName(presetName);
		logMonitor.SaveConfig(presetPath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Presetから読込")) {
		logMonitor.LoadConfig(presetPath);
		logMonitor.SetPresetName(presetName);
	}

	ImGui::TextDisabled("保存例: Physics Debug / Rendering Debug / AI Debug / Projectile Debug / Performance / Custom");

	std::error_code directoryError;
	if (std::filesystem::exists(kPresetDirectory, directoryError)) {
		ImGui::TextUnformatted("既存のPreset:");
		ImGui::SameLine();

		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(kPresetDirectory, directoryError)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::string existingName = entry.path().stem().string();
			if (ImGui::SmallButton(existingName.c_str())) {
				const size_t copyLength = (std::min)(existingName.size(), sizeof(presetNameBuffer_) - 1u);
				std::copy_n(existingName.data(), copyLength, presetNameBuffer_);
				presetNameBuffer_[copyLength] = '\0';
				logMonitor.LoadConfig(entry.path().string());
				logMonitor.SetPresetName(existingName);
			}
			ImGui::SameLine();
		}

		ImGui::NewLine();
	}
#endif
}

void EditorLogMonitorWindowManager::DrawTargetTab() {
#ifdef USE_IMGUI
	ImGui::InputText("検索", nameFilter_, sizeof(nameFilter_));
	ImGui::BeginChild("LogMonitorGameObjectTree", ImVec2(260.0f, 0.0f), true);
	DrawGameObjectTree();
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("LogMonitorFieldList", ImVec2(0.0f, 0.0f), true);
	DrawSelectedGameObjectFields();
	ImGui::EndChild();
#endif
}

void EditorLogMonitorWindowManager::DrawGameObjectTree() {
#ifdef USE_IMGUI
	for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
		if (gameObject.parentId < 0) {
			DrawGameObjectNode(gameObject.id, 0);
		}
	}
#endif
}

void EditorLogMonitorWindowManager::DrawGameObjectNode(int32_t gameObjectId, int32_t depth) {
#ifdef USE_IMGUI
	EditorGameObject* gameObject = g_editorScene.FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	if (nameFilter_[0] != '\0' && gameObject->name.find(nameFilter_) == std::string::npos) {
		// 自分自身は検索に一致しなくても、子には一致があるかもしれないので子だけ辿る
		for (int32_t childId : gameObject->children) {
			DrawGameObjectNode(childId, depth + 1);
		}
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (gameObjectId == selectedGameObjectId_) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (gameObject->children.empty()) {
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	const bool isOpen = ImGui::TreeNodeEx(
		(gameObject->name + "##LogMonitorNode" + std::to_string(gameObjectId)).c_str(), flags);

	if (ImGui::IsItemClicked()) {
		selectedGameObjectId_ = gameObjectId;
	}

	if (isOpen && !gameObject->children.empty()) {
		for (int32_t childId : gameObject->children) {
			DrawGameObjectNode(childId, depth + 1);
		}
		ImGui::TreePop();
	}
#endif
}

void EditorLogMonitorWindowManager::DrawVectorFieldCheckboxes(
	const char* label,
	const char* idSuffix,
	const std::function<int32_t(int32_t)>& findEntryIndex,
	const std::function<void(int32_t)>& addEntry) {
#ifdef USE_IMGUI
	ImGui::TextUnformatted(label);
	ImGui::SameLine();

	static const char* kVectorSuffixes[] = {"全体", "X", "Y", "Z"};
	static const int32_t kVectorComponents[] = {-1, 0, 1, 2};

	for (size_t slot = 0u; slot < 4u; ++slot) {
		const int32_t vectorComponent = kVectorComponents[slot];
		const int32_t existingIndex = findEntryIndex(vectorComponent);
		bool isWatched = existingIndex >= 0;

		ImGui::SameLine();
		if (ImGui::Checkbox(
				(std::string(kVectorSuffixes[slot]) + "##" + idSuffix + std::to_string(vectorComponent)).c_str(),
				&isWatched)) {
			if (isWatched) {
				addEntry(vectorComponent);
			}
			else {
				EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();
				logMonitor.RemoveEntry(static_cast<size_t>(existingIndex));
			}
		}
	}

	ImGui::NewLine();
#endif
}

void EditorLogMonitorWindowManager::DrawSelectedGameObjectFields() {
#ifdef USE_IMGUI
	EditorGameObject* gameObject = g_editorScene.FindGameObject(selectedGameObjectId_);
	if (gameObject == nullptr) {
		ImGui::TextDisabled("左のTreeからGameObjectを選んでください。");
		return;
	}

	EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();
	const int32_t gameObjectId = gameObject->id;
	ImGui::Text("%s (ID:%d)", gameObject->name.c_str(), gameObjectId);
	ImGui::Separator();
	ImGui::TextUnformatted("Transform");

	// 非Vector3系(ID/名前/有効)は単一Checkbox
	static const char* kScalarFieldNames[] = {"id", "name", "isActive"};
	static const char* kScalarFieldLabels[] = {"ID", "名前", "有効"};

	for (size_t fieldSlot = 0u; fieldSlot < 3u; ++fieldSlot) {
		const char* fieldName = kScalarFieldNames[fieldSlot];
		std::vector<LogWatchEntry>& entries = logMonitor.GetEntries();
		const int32_t existingIndex = FindGameObjectFieldEntryIndex(entries, gameObjectId, fieldName, -1);
		bool isWatched = existingIndex >= 0;

		if (ImGui::Checkbox((std::string(kScalarFieldLabels[fieldSlot]) + "##GOField" + fieldName).c_str(), &isWatched)) {
			if (isWatched) {
				LogWatchEntry newEntry;
				newEntry.targetKind = LogWatchTargetKind::GameObjectField;
				newEntry.gameObjectId = gameObjectId;
				newEntry.gameObjectFieldName = fieldName;
				newEntry.category = "Transform";
				logMonitor.AddEntry(newEntry);
			}
			else {
				logMonitor.RemoveEntry(static_cast<size_t>(existingIndex));
			}
		}
	}

	// Vector3系(位置/回転/拡縮)は全体+X/Y/Zを個別選択可能にする
	static const char* kVectorFieldNames[] = {"translate", "rotate", "scale"};
	static const char* kVectorFieldLabels[] = {"位置", "回転", "拡縮"};

	for (size_t fieldSlot = 0u; fieldSlot < 3u; ++fieldSlot) {
		const char* fieldName = kVectorFieldNames[fieldSlot];
		DrawVectorFieldCheckboxes(
			kVectorFieldLabels[fieldSlot],
			(std::string("GOVec") + fieldName).c_str(),
			[&logMonitor, gameObjectId, fieldName](int32_t vectorComponent) {
				return FindGameObjectFieldEntryIndex(logMonitor.GetEntries(), gameObjectId, fieldName, vectorComponent);
			},
			[&logMonitor, gameObjectId, fieldName](int32_t vectorComponent) {
				LogWatchEntry newEntry;
				newEntry.targetKind = LogWatchTargetKind::GameObjectField;
				newEntry.gameObjectId = gameObjectId;
				newEntry.gameObjectFieldName = fieldName;
				newEntry.vectorComponent = vectorComponent;
				newEntry.category = "Transform";
				logMonitor.AddEntry(newEntry);
			});
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Component");

	const std::vector<LogComponentFieldDescriptor>& registry = logMonitor.GetComponentFieldRegistry();

	for (const EditorComponent& component : gameObject->components) {
		const EditorComponentType componentType = component.type;
		bool hasAnyField = false;
		for (const LogComponentFieldDescriptor& descriptor : registry) {
			if (descriptor.componentType == componentType) {
				hasAnyField = true;
				break;
			}
		}

		if (!hasAnyField) {
			continue;
		}

		if (ImGui::TreeNodeEx(
				(ToString(componentType) + "##LogMonitorComponent" + std::to_string(gameObjectId)).c_str(),
				ImGuiTreeNodeFlags_DefaultOpen)) {
			for (const LogComponentFieldDescriptor& descriptor : registry) {
				if (descriptor.componentType != componentType) {
					continue;
				}

				const std::string fieldKey = descriptor.fieldKey;

				if (descriptor.kind == LogFieldValueKind::Vector3) {
					DrawVectorFieldCheckboxes(
						descriptor.displayName,
						(std::string("CVec") + fieldKey).c_str(),
						[&logMonitor, gameObjectId, componentType, fieldKey](int32_t vectorComponent) {
							return FindComponentFieldEntryIndex(
								logMonitor.GetEntries(), gameObjectId, componentType, fieldKey, vectorComponent);
						},
						[&logMonitor, gameObjectId, componentType, fieldKey](int32_t vectorComponent) {
							LogWatchEntry newEntry;
							newEntry.targetKind = LogWatchTargetKind::ComponentField;
							newEntry.gameObjectId = gameObjectId;
							newEntry.componentType = componentType;
							newEntry.componentFieldKey = fieldKey;
							newEntry.vectorComponent = vectorComponent;
							newEntry.category = ToString(componentType);
							logMonitor.AddEntry(newEntry);
						});
					continue;
				}

				std::vector<LogWatchEntry>& entries = logMonitor.GetEntries();
				const int32_t existingIndex =
					FindComponentFieldEntryIndex(entries, gameObjectId, componentType, fieldKey, -1);
				bool isWatched = existingIndex >= 0;

				if (ImGui::Checkbox(
						(std::string(descriptor.displayName) + "##CField" + fieldKey).c_str(),
						&isWatched)) {
					if (isWatched) {
						LogWatchEntry newEntry;
						newEntry.targetKind = LogWatchTargetKind::ComponentField;
						newEntry.gameObjectId = gameObjectId;
						newEntry.componentType = componentType;
						newEntry.componentFieldKey = fieldKey;
						newEntry.category = ToString(componentType);
						logMonitor.AddEntry(newEntry);
					}
					else {
						logMonitor.RemoveEntry(static_cast<size_t>(existingIndex));
					}
				}
			}

			ImGui::TreePop();
		}
	}
#endif
}

void EditorLogMonitorWindowManager::DrawSystemTab() {
#ifdef USE_IMGUI
	EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();
	const std::vector<std::pair<std::string, std::string>> fields = logMonitor.GetAvailableSystemFields();

	std::string currentCategory;
	for (const std::pair<std::string, std::string>& field : fields) {
		const std::string& category = field.first;
		const std::string& name = field.second;

		if (category != currentCategory) {
			ImGui::SeparatorText(category.c_str());
			currentCategory = category;
		}

		std::vector<LogWatchEntry>& entries = logMonitor.GetEntries();
		int32_t existingIndex = -1;

		for (size_t entryIndex = 0u; entryIndex < entries.size(); ++entryIndex) {
			const LogWatchEntry& entry = entries[entryIndex];
			if (entry.targetKind == LogWatchTargetKind::SystemField &&
				entry.category == category && entry.systemFieldName == name) {
				existingIndex = static_cast<int32_t>(entryIndex);
				break;
			}
		}

		bool isWatched = existingIndex >= 0;
		if (ImGui::Checkbox((name + "##SysField" + category).c_str(), &isWatched)) {
			if (isWatched) {
				LogWatchEntry newEntry;
				newEntry.targetKind = LogWatchTargetKind::SystemField;
				newEntry.category = category;
				newEntry.systemFieldName = name;
				logMonitor.AddEntry(newEntry);
			}
			else {
				logMonitor.RemoveEntry(static_cast<size_t>(existingIndex));
			}
		}
	}
#endif
}

void EditorLogMonitorWindowManager::DrawWatchListTab() {
#ifdef USE_IMGUI
	EditorLogMonitorManager& logMonitor = g_editorRuntimeManager.GetLogMonitorManager();
	std::vector<LogWatchEntry>& entries = logMonitor.GetEntries();

	if (entries.empty()) {
		ImGui::TextDisabled("監視中の対象はありません。GameObject / Component / System タブから追加してください。");
		return;
	}

	static const char* kCaptureModeLabels[] = {"毎Frame", "間隔(秒)", "変化時のみ", "間隔(Frame数)", "手動(記録ボタンのみ)"};

	if (ImGui::BeginTable("WatchList", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("有効");
		ImGui::TableSetupColumn("種別");
		ImGui::TableSetupColumn("カテゴリ");
		ImGui::TableSetupColumn("対象");
		ImGui::TableSetupColumn("状態");
		ImGui::TableSetupColumn("取得Mode");
		ImGui::TableSetupColumn("Threshold");
		ImGui::TableSetupColumn("削除");
		ImGui::TableHeadersRow();

		int32_t removeIndex = -1;
		for (size_t entryIndex = 0u; entryIndex < entries.size(); ++entryIndex) {
			LogWatchEntry& entry = entries[entryIndex];
			ImGui::PushID(static_cast<int32_t>(entryIndex));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("##Enabled", &entry.enabled);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(
				entry.targetKind == LogWatchTargetKind::GameObjectField ? "GameObject" :
				entry.targetKind == LogWatchTargetKind::ComponentField ? "Component" : "System");
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(entry.category.c_str());
			ImGui::TableSetColumnIndex(3);

			if (entry.targetKind == LogWatchTargetKind::SystemField) {
				ImGui::TextUnformatted(entry.systemFieldName.c_str());
			}
			else {
				const EditorGameObject* gameObject = g_editorScene.FindGameObject(entry.gameObjectId);
				std::string fieldLabel = entry.targetKind == LogWatchTargetKind::GameObjectField
					? entry.gameObjectFieldName
					: entry.componentFieldKey;
				if (entry.targetKind == LogWatchTargetKind::ComponentField &&
					entry.cachedRegistryIndex >= 0 &&
					static_cast<size_t>(entry.cachedRegistryIndex) < logMonitor.GetComponentFieldRegistry().size()) {
					fieldLabel = logMonitor.GetComponentFieldRegistry()[static_cast<size_t>(entry.cachedRegistryIndex)].displayName;
				}
				if (entry.vectorComponent == 0) fieldLabel += ".X";
				else if (entry.vectorComponent == 1) fieldLabel += ".Y";
				else if (entry.vectorComponent == 2) fieldLabel += ".Z";
				ImGui::Text(
					"%s (ID:%d) / %s",
					gameObject != nullptr ? gameObject->name.c_str() : "?",
					entry.gameObjectId,
					fieldLabel.c_str());
			}

			ImGui::TableSetColumnIndex(4);
			if (entry.wasMissingLastFrame) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Missing");
			}
			else {
				ImGui::TextDisabled("OK");
			}

			ImGui::TableSetColumnIndex(5);
			int32_t captureModeIndex = static_cast<int32_t>(entry.captureMode);
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::Combo("##CaptureMode", &captureModeIndex, kCaptureModeLabels, 5)) {
				entry.captureMode = static_cast<LogCaptureMode>(captureModeIndex);
			}

			if (entry.captureMode == LogCaptureMode::IntervalSeconds) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				ImGui::DragFloat("##Interval", &entry.intervalSeconds, 0.05f, 0.01f, 3600.0f, "%.2fs");
			}
			else if (entry.captureMode == LogCaptureMode::IntervalFrames) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				ImGui::DragInt("##IntervalFrames", &entry.intervalFrames, 1.0f, 1, 100000, "%dFrame");
			}

			ImGui::TableSetColumnIndex(6);
			if (entry.captureMode == LogCaptureMode::OnChange) {
				ImGui::SetNextItemWidth(80.0f);
				ImGui::DragFloat("##Threshold", &entry.changeThreshold, 0.01f, 0.0f, 1000000.0f, "%.3f");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Float/Vector3のみ有効。abs(current-previous) >= Threshold(Vector3は距離)の時だけ記録する。Bool/Int/Reference/文字列は常に完全一致比較。");
				}
			}
			else {
				ImGui::TextDisabled("-");
			}

			ImGui::TableSetColumnIndex(7);
			if (ImGui::SmallButton("削除")) {
				removeIndex = static_cast<int32_t>(entryIndex);
			}

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (removeIndex >= 0) {
			logMonitor.RemoveEntry(static_cast<size_t>(removeIndex));
		}
	}
#endif
}
