#include "EditorLogMonitorManager.h"

#include "EditorComponentUtility.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
	constexpr char kLogFilePath[] = "Logs/RuntimeLog.log";
	constexpr float kFlushIntervalSeconds = 1.0f;

	std::string FormatFloat(float value) {
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(3) << value;
		return stream.str();
	}

	std::string FormatVector3(const Vector3& value) {
		return "(" + FormatFloat(value.x) + ", " + FormatFloat(value.y) + ", " + FormatFloat(value.z) + ")";
	}

	std::string SanitizePipeField(const std::string& value) {
		std::string sanitized = value;
		for (char& character : sanitized) {
			if (character == '|') {
				character = ';';
			}
			else if (character == '\n' || character == '\r') {
				character = ' ';
			}
		}
		return sanitized;
	}

	std::string DescribeTargetKindText(LogWatchTargetKind kind) {
		switch (kind) {
			case LogWatchTargetKind::GameObjectField: return "GameObject";
			case LogWatchTargetKind::ComponentField: return "Component";
			case LogWatchTargetKind::SystemField: return "System";
		}
		return "Unknown";
	}

	std::string AppendVectorSuffix(std::string fieldName, int32_t vectorComponent) {
		if (vectorComponent == 0) fieldName += ".X";
		else if (vectorComponent == 1) fieldName += ".Y";
		else if (vectorComponent == 2) fieldName += ".Z";
		return fieldName;
	}
}

void EditorLogMonitorManager::Initialize(
	EditorScene* editorScene,
	EditorProfilerManager* profilerManager,
	EditorWeaponManager* weaponManager,
	EditorRuntimePropertyManager* runtimePropertyManager) {
	editorScene_ = editorScene;
	profilerManager_ = profilerManager;
	weaponManager_ = weaponManager;
	runtimePropertyManager_ = runtimePropertyManager;
}

void EditorLogMonitorManager::Start() {
	frameCounter_ = 0u;
	elapsedSeconds_ = 0.0f;
	secondsSinceFlush_ = 0.0f;
	entriesCapturedThisFrame_ = 0u;
	framesDroppedSinceReport_ = 0u;
	totalDroppedEntryCount_ = 0u;
	loggingSuspendedDueToFileSize_ = false;
	logBuffer_.clear();

	for (LogWatchEntry& entry : entries_) {
		entry.elapsedSinceLastCapture = 0.0f;
		entry.elapsedFramesSinceLastCapture = 0u;
		entry.hasPreviousSnapshot = false;
		entry.previousSnapshot = LogFieldSnapshot{};
		entry.wasMissingLastFrame = false;
		entry.cachedRegistryIndex = -1;
		entry.cachedComponentSlot = -1;
	}

	std::error_code directoryError;
	std::filesystem::create_directories("Logs", directoryError);

	std::ofstream logFile(kLogFilePath, std::ios::trunc);
	if (logFile.is_open()) {
		logFile << "# RuntimeLog Preset=" << presetName_
			<< " pipe-delimited 9 columns. Playを開始するたびに空になる。\n";
		logFile << "Timestamp|Frame|Category|TargetKind|SourceId|SourceName|Component|Field|Value\n";
	}
}

void EditorLogMonitorManager::Stop() {
	Flush();
}

void EditorLogMonitorManager::Update(float deltaTime) {
	++frameCounter_;
	elapsedSeconds_ += deltaTime;
	entriesCapturedThisFrame_ = 0u;

	for (LogWatchEntry& entry : entries_) {
		if (!entry.enabled) {
			continue;
		}

		const LogFieldSnapshot snapshot = ResolveSnapshot(entry);
		HandleMissingTransition(entry, snapshot.isMissing);

		if (snapshot.isMissing) {
			continue;
		}

		if (ShouldCapture(entry, deltaTime, snapshot)) {
			TryRecordLine(entry, snapshot);
		}
	}

	secondsSinceFlush_ += deltaTime;
	if (secondsSinceFlush_ >= kFlushIntervalSeconds || logBuffer_.size() >= kMaxBufferBytes) {
		Flush();
	}
}

void EditorLogMonitorManager::CaptureNow() {
	AppendManualMarker();

	for (LogWatchEntry& entry : entries_) {
		if (!entry.enabled) {
			continue;
		}

		const LogFieldSnapshot snapshot = ResolveSnapshot(entry);
		HandleMissingTransition(entry, snapshot.isMissing);

		if (snapshot.isMissing) {
			continue;
		}

		TryRecordLine(entry, snapshot);

		// 定期取得Modeのタイマーをここでリセットし、直後のFrameで二重に発火しないようにする。
		entry.elapsedSinceLastCapture = 0.0f;
		entry.elapsedFramesSinceLastCapture = 0u;
		if (entry.captureMode == LogCaptureMode::OnChange) {
			entry.previousSnapshot = snapshot;
			entry.hasPreviousSnapshot = true;
		}
	}

	// 手動記録はその場で確認したい操作なので、即座にFileへ書き出す。
	Flush();
}

void EditorLogMonitorManager::RemoveEntry(size_t index) {
	if (index >= entries_.size()) {
		return;
	}
	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}

const std::vector<LogComponentFieldDescriptor>& EditorLogMonitorManager::GetComponentFieldRegistry() const {
	return GetLogComponentFieldRegistry();
}

std::vector<std::pair<std::string, std::string>> EditorLogMonitorManager::GetAvailableSystemFields() const {
	return ListLogSystemFields(BuildSystemFieldContext());
}

LogSystemFieldContext EditorLogMonitorManager::BuildSystemFieldContext() const {
	LogSystemFieldContext context;
	context.editorScene = editorScene_;
	context.profilerManager = profilerManager_;
	context.weaponManager = weaponManager_;
	context.runtimePropertyManager = runtimePropertyManager_;
	return context;
}

int32_t EditorLogMonitorManager::ResolveComponentRegistryIndex(LogWatchEntry& entry) const {
	const std::vector<LogComponentFieldDescriptor>& registry = GetLogComponentFieldRegistry();

	if (entry.cachedRegistryIndex >= 0 &&
		static_cast<size_t>(entry.cachedRegistryIndex) < registry.size()) {
		const LogComponentFieldDescriptor& cached = registry[static_cast<size_t>(entry.cachedRegistryIndex)];
		if (cached.componentType == entry.componentType && entry.componentFieldKey == cached.fieldKey) {
			return entry.cachedRegistryIndex;
		}
	}

	for (size_t index = 0u; index < registry.size(); ++index) {
		const LogComponentFieldDescriptor& descriptor = registry[index];
		if (descriptor.componentType == entry.componentType && entry.componentFieldKey == descriptor.fieldKey) {
			entry.cachedRegistryIndex = static_cast<int32_t>(index);
			return entry.cachedRegistryIndex;
		}
	}

	entry.cachedRegistryIndex = -1;
	return -1;
}

int32_t EditorLogMonitorManager::ResolveComponentSlot(EditorGameObject& gameObject, LogWatchEntry& entry) const {
	if (entry.cachedComponentSlot >= 0 &&
		static_cast<size_t>(entry.cachedComponentSlot) < gameObject.components.size() &&
		gameObject.components[static_cast<size_t>(entry.cachedComponentSlot)].type == entry.componentType) {
		return entry.cachedComponentSlot;
	}

	for (size_t index = 0u; index < gameObject.components.size(); ++index) {
		if (gameObject.components[index].type == entry.componentType) {
			entry.cachedComponentSlot = static_cast<int32_t>(index);
			return entry.cachedComponentSlot;
		}
	}

	entry.cachedComponentSlot = -1;
	return -1;
}

LogFieldSnapshot EditorLogMonitorManager::MakeVectorComponentSnapshot(const Vector3& value, int32_t vectorComponent) const {
	LogFieldSnapshot snapshot;
	snapshot.hasValue = true;

	if (vectorComponent == 0) {
		snapshot.kind = LogFieldValueKind::Float;
		snapshot.floatValue = value.x;
		snapshot.displayValue = FormatFloat(value.x);
	}
	else if (vectorComponent == 1) {
		snapshot.kind = LogFieldValueKind::Float;
		snapshot.floatValue = value.y;
		snapshot.displayValue = FormatFloat(value.y);
	}
	else if (vectorComponent == 2) {
		snapshot.kind = LogFieldValueKind::Float;
		snapshot.floatValue = value.z;
		snapshot.displayValue = FormatFloat(value.z);
	}
	else {
		snapshot.kind = LogFieldValueKind::Vector3;
		snapshot.vector3Value = value;
		snapshot.displayValue = FormatVector3(value);
	}

	return snapshot;
}

LogFieldSnapshot EditorLogMonitorManager::ResolveSnapshot(LogWatchEntry& entry) const {
	switch (entry.targetKind) {
		case LogWatchTargetKind::GameObjectField:
			return ResolveGameObjectFieldSnapshot(entry);
		case LogWatchTargetKind::ComponentField:
			return ResolveComponentFieldSnapshot(entry);
		case LogWatchTargetKind::SystemField:
			return ResolveSystemFieldSnapshot(entry);
	}

	LogFieldSnapshot snapshot;
	snapshot.isMissing = true;
	return snapshot;
}

LogFieldSnapshot EditorLogMonitorManager::ResolveGameObjectFieldSnapshot(LogWatchEntry& entry) const {
	LogFieldSnapshot snapshot;

	if (editorScene_ == nullptr) {
		snapshot.isMissing = true;
		return snapshot;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(entry.gameObjectId);
	if (gameObject == nullptr) {
		snapshot.isMissing = true;
		return snapshot;
	}

	if (entry.gameObjectFieldName == "id") {
		snapshot.hasValue = true;
		snapshot.kind = LogFieldValueKind::Int;
		snapshot.intValue = gameObject->id;
		snapshot.displayValue = std::to_string(gameObject->id);
	}
	else if (entry.gameObjectFieldName == "name") {
		snapshot.hasValue = true;
		snapshot.kind = LogFieldValueKind::Text;
		snapshot.displayValue = gameObject->name;
	}
	else if (entry.gameObjectFieldName == "isActive") {
		snapshot.hasValue = true;
		snapshot.kind = LogFieldValueKind::Bool;
		snapshot.boolValue = gameObject->isActive;
		snapshot.displayValue = snapshot.boolValue ? "true" : "false";
	}
	else if (entry.gameObjectFieldName == "translate") {
		snapshot = MakeVectorComponentSnapshot(gameObject->translate, entry.vectorComponent);
	}
	else if (entry.gameObjectFieldName == "rotate") {
		snapshot = MakeVectorComponentSnapshot(gameObject->rotate, entry.vectorComponent);
	}
	else if (entry.gameObjectFieldName == "scale") {
		snapshot = MakeVectorComponentSnapshot(gameObject->scale, entry.vectorComponent);
	}
	else {
		snapshot.isMissing = true;
	}

	return snapshot;
}

LogFieldSnapshot EditorLogMonitorManager::ResolveComponentFieldSnapshot(LogWatchEntry& entry) const {
	LogFieldSnapshot snapshot;

	if (editorScene_ == nullptr) {
		snapshot.isMissing = true;
		return snapshot;
	}

	const int32_t registryIndex = ResolveComponentRegistryIndex(entry);
	if (registryIndex < 0) {
		snapshot.isMissing = true;
		return snapshot;
	}

	EditorGameObject* gameObject = editorScene_->FindGameObject(entry.gameObjectId);
	if (gameObject == nullptr) {
		snapshot.isMissing = true;
		return snapshot;
	}

	const int32_t componentSlot = ResolveComponentSlot(*gameObject, entry);
	if (componentSlot < 0) {
		snapshot.isMissing = true;
		return snapshot;
	}

	EditorComponent& component = gameObject->components[static_cast<size_t>(componentSlot)];
	const LogComponentFieldDescriptor& descriptor =
		GetLogComponentFieldRegistry()[static_cast<size_t>(registryIndex)];

	switch (descriptor.kind) {
		case LogFieldValueKind::Float:
			if (descriptor.floatMember == nullptr) {
				snapshot.isMissing = true;
				return snapshot;
			}
			snapshot.hasValue = true;
			snapshot.kind = LogFieldValueKind::Float;
			snapshot.floatValue = component.*descriptor.floatMember;
			snapshot.displayValue = FormatFloat(snapshot.floatValue);
			return snapshot;
		case LogFieldValueKind::Int:
			if (descriptor.intMember == nullptr) {
				snapshot.isMissing = true;
				return snapshot;
			}
			snapshot.hasValue = true;
			snapshot.kind = LogFieldValueKind::Int;
			snapshot.intValue = component.*descriptor.intMember;
			snapshot.displayValue = std::to_string(snapshot.intValue);
			return snapshot;
		case LogFieldValueKind::Bool:
			if (descriptor.boolMember == nullptr) {
				snapshot.isMissing = true;
				return snapshot;
			}
			snapshot.hasValue = true;
			snapshot.kind = LogFieldValueKind::Bool;
			snapshot.boolValue = component.*descriptor.boolMember;
			snapshot.displayValue = snapshot.boolValue ? "true" : "false";
			return snapshot;
		case LogFieldValueKind::Vector3:
			if (descriptor.vector3Member == nullptr) {
				snapshot.isMissing = true;
				return snapshot;
			}
			return MakeVectorComponentSnapshot(component.*descriptor.vector3Member, entry.vectorComponent);
		case LogFieldValueKind::GameObjectReference: {
			if (descriptor.intMember == nullptr) {
				snapshot.isMissing = true;
				return snapshot;
			}
			const int32_t referencedId = component.*descriptor.intMember;
			const EditorGameObject* referencedGameObject = editorScene_->FindGameObject(referencedId);
			snapshot.hasValue = true;
			snapshot.kind = LogFieldValueKind::GameObjectReference;
			snapshot.intValue = referencedId;
			snapshot.displayValue = std::to_string(referencedId) + ":" +
				(referencedGameObject != nullptr ? referencedGameObject->name : "(None)");
			return snapshot;
		}
		case LogFieldValueKind::Text:
			break;
	}

	snapshot.isMissing = true;
	return snapshot;
}

LogFieldSnapshot EditorLogMonitorManager::ResolveSystemFieldSnapshot(const LogWatchEntry& entry) const {
	LogFieldSnapshot snapshot;
	std::string value;

	if (!ResolveLogSystemFieldValue(BuildSystemFieldContext(), entry.category, entry.systemFieldName, value)) {
		snapshot.isMissing = true;
		return snapshot;
	}

	snapshot.hasValue = true;
	snapshot.kind = LogFieldValueKind::Text;
	snapshot.displayValue = value;
	return snapshot;
}

bool EditorLogMonitorManager::ExceedsThreshold(
	const LogFieldSnapshot& previous, const LogFieldSnapshot& current, float threshold) const {
	switch (current.kind) {
		case LogFieldValueKind::Float:
			return std::fabs(current.floatValue - previous.floatValue) >= threshold;
		case LogFieldValueKind::Vector3: {
			const float dx = current.vector3Value.x - previous.vector3Value.x;
			const float dy = current.vector3Value.y - previous.vector3Value.y;
			const float dz = current.vector3Value.z - previous.vector3Value.z;
			return (dx * dx + dy * dy + dz * dz) >= (threshold * threshold);
		}
		case LogFieldValueKind::Int:
			return previous.intValue != current.intValue;
		case LogFieldValueKind::Bool:
			return previous.boolValue != current.boolValue;
		case LogFieldValueKind::GameObjectReference:
			return previous.intValue != current.intValue;
		case LogFieldValueKind::Text:
			return previous.displayValue != current.displayValue;
	}

	return previous.displayValue != current.displayValue;
}

bool EditorLogMonitorManager::ShouldCapture(LogWatchEntry& entry, float deltaTime, const LogFieldSnapshot& snapshot) {
	if (!snapshot.hasValue) {
		return false;
	}

	switch (entry.captureMode) {
		case LogCaptureMode::EveryFrame:
			return true;
		case LogCaptureMode::IntervalSeconds: {
			entry.elapsedSinceLastCapture += deltaTime;
			const float interval = (std::max)(entry.intervalSeconds, 0.01f);
			if (entry.elapsedSinceLastCapture < interval) {
				return false;
			}
			entry.elapsedSinceLastCapture = 0.0f;
			return true;
		}
		case LogCaptureMode::OnChange: {
			if (entry.hasPreviousSnapshot && !ExceedsThreshold(entry.previousSnapshot, snapshot, entry.changeThreshold)) {
				return false;
			}
			entry.previousSnapshot = snapshot;
			entry.hasPreviousSnapshot = true;
			return true;
		}
		case LogCaptureMode::IntervalFrames: {
			++entry.elapsedFramesSinceLastCapture;
			const uint64_t interval = static_cast<uint64_t>((std::max)(entry.intervalFrames, 1));
			if (entry.elapsedFramesSinceLastCapture < interval) {
				return false;
			}
			entry.elapsedFramesSinceLastCapture = 0u;
			return true;
		}
		case LogCaptureMode::Manual:
			// Update()内では発火しない。CaptureNow()から明示的に呼ばれた時だけ記録する。
			return false;
	}

	return false;
}

void EditorLogMonitorManager::HandleMissingTransition(LogWatchEntry& entry, bool isMissing) {
	if (isMissing == entry.wasMissingLastFrame) {
		return;
	}

	entry.wasMissingLastFrame = isMissing;

	const std::string sourceId = entry.targetKind == LogWatchTargetKind::SystemField
		? entry.systemFieldName
		: std::to_string(entry.gameObjectId);
	const std::string componentName = entry.targetKind == LogWatchTargetKind::ComponentField
		? ToString(entry.componentType)
		: "-";
	const std::string fieldName = entry.targetKind == LogWatchTargetKind::ComponentField
		? entry.componentFieldKey
		: entry.targetKind == LogWatchTargetKind::GameObjectField
			? entry.gameObjectFieldName
			: entry.systemFieldName;

	WriteRow(
		entry.category,
		DescribeTargetKindText(entry.targetKind),
		sourceId,
		"-",
		componentName,
		fieldName,
		isMissing ? "TargetMissing" : "TargetRestored");
}

bool EditorLogMonitorManager::TryRecordLine(const LogWatchEntry& entry, const LogFieldSnapshot& snapshot) {
	if (loggingSuspendedDueToFileSize_) {
		++totalDroppedEntryCount_;
		++framesDroppedSinceReport_;
		return false;
	}

	if (entriesCapturedThisFrame_ >= kMaxEntriesPerFrame) {
		++totalDroppedEntryCount_;
		++framesDroppedSinceReport_;
		return false;
	}

	++entriesCapturedThisFrame_;
	AppendLogLine(entry, snapshot.displayValue);
	return true;
}

void EditorLogMonitorManager::AppendLogLine(const LogWatchEntry& entry, const std::string& value) {
	const std::string targetKindText = DescribeTargetKindText(entry.targetKind);

	if (entry.targetKind == LogWatchTargetKind::SystemField) {
		WriteRow(entry.category, targetKindText, "-1", "System", "-", entry.systemFieldName, value);
		return;
	}

	const EditorGameObject* gameObject = editorScene_ != nullptr ? editorScene_->FindGameObject(entry.gameObjectId) : nullptr;
	const std::string sourceName = gameObject != nullptr ? gameObject->name : "?";
	const std::string sourceId = std::to_string(entry.gameObjectId);

	if (entry.targetKind == LogWatchTargetKind::GameObjectField) {
		const std::string field = AppendVectorSuffix(entry.gameObjectFieldName, entry.vectorComponent);
		WriteRow(entry.category, targetKindText, sourceId, sourceName, "Transform", field, value);
		return;
	}

	std::string fieldLabel = entry.componentFieldKey;
	const std::vector<LogComponentFieldDescriptor>& registry = GetLogComponentFieldRegistry();
	if (entry.cachedRegistryIndex >= 0 && static_cast<size_t>(entry.cachedRegistryIndex) < registry.size()) {
		fieldLabel = registry[static_cast<size_t>(entry.cachedRegistryIndex)].displayName;
	}
	fieldLabel = AppendVectorSuffix(fieldLabel, entry.vectorComponent);

	WriteRow(entry.category, targetKindText, sourceId, sourceName, ToString(entry.componentType), fieldLabel, value);
}

void EditorLogMonitorManager::AppendManualMarker() {
	WriteRow("Manual", "Marker", "-1", "-", "-", "-", "ManualSnapshot");
}

void EditorLogMonitorManager::ReportDroppedEntriesIfAny() {
	if (framesDroppedSinceReport_ == 0u) {
		return;
	}

	WriteRow("System", "Marker", "-1", "-", "-", "-", "LogEntriesDropped:" + std::to_string(framesDroppedSinceReport_));
	framesDroppedSinceReport_ = 0u;
}

void EditorLogMonitorManager::WriteRow(
	const std::string& category,
	const std::string& targetKind,
	const std::string& sourceId,
	const std::string& sourceName,
	const std::string& componentName,
	const std::string& fieldName,
	const std::string& value) {
	std::ostringstream line;
	line << std::fixed << std::setprecision(3) << elapsedSeconds_ << "|"
		<< frameCounter_ << "|"
		<< SanitizePipeField(category) << "|"
		<< targetKind << "|"
		<< sourceId << "|"
		<< SanitizePipeField(sourceName) << "|"
		<< SanitizePipeField(componentName) << "|"
		<< SanitizePipeField(fieldName) << "|"
		<< SanitizePipeField(value) << "\n";
	logBuffer_ += line.str();
}

void EditorLogMonitorManager::Flush() {
	ReportDroppedEntriesIfAny();

	if (logBuffer_.empty()) {
		return;
	}

	if (loggingSuspendedDueToFileSize_) {
		logBuffer_.clear();
		secondsSinceFlush_ = 0.0f;
		return;
	}

	std::error_code sizeError;
	const bool fileExists = std::filesystem::exists(kLogFilePath, sizeError);
	const uint64_t currentSize = (!sizeError && fileExists)
		? static_cast<uint64_t>(std::filesystem::file_size(kLogFilePath, sizeError))
		: 0u;

	if (!sizeError && currentSize >= kMaxLogFileBytes) {
		loggingSuspendedDueToFileSize_ = true;
		std::ofstream limitFile(kLogFilePath, std::ios::app);
		if (limitFile.is_open()) {
			limitFile << std::fixed << std::setprecision(3) << elapsedSeconds_ << "|" << frameCounter_
				<< "|System|Marker|-1|-|-|-|LogFileSizeLimitReached: further entries dropped\n";
		}
		logBuffer_.clear();
		secondsSinceFlush_ = 0.0f;
		return;
	}

	std::ofstream logFile(kLogFilePath, std::ios::app);
	if (logFile.is_open()) {
		logFile << logBuffer_;
	}

	logBuffer_.clear();
	secondsSinceFlush_ = 0.0f;
}

bool EditorLogMonitorManager::LoadConfig(const std::string& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	entries_.clear();

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) {
			continue;
		}

		if (line.rfind("# Preset=", 0) == 0) {
			presetName_ = line.substr(9);
			continue;
		}

		std::vector<std::string> elements;
		size_t start = 0;
		while (start <= line.size()) {
			const size_t separator = line.find('|', start);
			if (separator == std::string::npos) {
				elements.push_back(line.substr(start));
				break;
			}
			elements.push_back(line.substr(start, separator - start));
			start = separator + 1;
		}

		if (elements.empty() || elements[0] != "LogWatchEntry" || elements.size() < 12) {
			continue;
		}

		LogWatchEntry entry;
		entry.targetKind = static_cast<LogWatchTargetKind>(std::stoi(elements[1]));
		entry.enabled = std::stoi(elements[2]) != 0;
		entry.category = elements[3];
		entry.gameObjectId = std::stoi(elements[4]);
		entry.componentType = static_cast<EditorComponentType>(std::stoi(elements[5]));
		entry.componentFieldKey = elements[6];
		entry.gameObjectFieldName = elements[7];
		entry.systemFieldName = elements[8];
		entry.captureMode = static_cast<LogCaptureMode>(std::stoi(elements[9]));
		entry.intervalSeconds = std::stof(elements[10]);
		entry.intervalFrames = std::stoi(elements[11]);
		entry.vectorComponent = elements.size() > 12 ? std::stoi(elements[12]) : -1;
		entry.changeThreshold = elements.size() > 13 ? std::stof(elements[13]) : 0.0f;

		entries_.push_back(entry);
	}

	return true;
}

bool EditorLogMonitorManager::SaveConfig(const std::string& filePath) const {
	std::error_code directoryError;
	const std::filesystem::path configPath(filePath);
	if (configPath.has_parent_path()) {
		std::filesystem::create_directories(configPath.parent_path(), directoryError);
	}

	std::ofstream file(filePath, std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	file << "# Preset=" << presetName_ << "\n";
	file << "# LogWatchEntry|targetKind|enabled|category|gameObjectId|componentType|componentFieldKey|"
		"gameObjectFieldName|systemFieldName|captureMode|intervalSeconds|intervalFrames|vectorComponent|changeThreshold\n";

	for (const LogWatchEntry& entry : entries_) {
		file << "LogWatchEntry|"
			<< static_cast<int32_t>(entry.targetKind) << "|"
			<< (entry.enabled ? 1 : 0) << "|"
			<< entry.category << "|"
			<< entry.gameObjectId << "|"
			<< static_cast<int32_t>(entry.componentType) << "|"
			<< entry.componentFieldKey << "|"
			<< entry.gameObjectFieldName << "|"
			<< entry.systemFieldName << "|"
			<< static_cast<int32_t>(entry.captureMode) << "|"
			<< entry.intervalSeconds << "|"
			<< entry.intervalFrames << "|"
			<< entry.vectorComponent << "|"
			<< entry.changeThreshold << "\n";
	}

	return true;
}
