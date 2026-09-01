#include "EditorProfilerManager.h"

#include "EditorProfilerAllocationTracker.h"

#include <d3d12.h>

#include <algorithm>
#include <filesystem>
#include <thread>

namespace {
	struct ActiveCpuProfilerScope {
		std::string callPath;
		float childMilliseconds = 0.0f;
	};

	thread_local std::vector<ActiveCpuProfilerScope> g_activeCpuProfilerScopes;
	EditorProfilerManager* g_activeGpuProfilerManager = nullptr;

	std::uint64_t GetCurrentProfilerThreadId() {
		return static_cast<std::uint64_t>(
			std::hash<std::thread::id>{}(std::this_thread::get_id()));
	}
}

EditorProfilerManager::Scope::Scope(
	EditorProfilerManager& profilerManager,
	const char* sampleName,
	const char* sampleSource,
	int32_t gameObjectId) {
	if (!profilerManager.IsEnabled() || sampleName == nullptr || sampleName[0] == '\0') {
		return;
	}

	profilerManager_ = &profilerManager;
	sampleName_ = sampleName;
	sampleSource_ = sampleSource != nullptr && sampleSource[0] != '\0' ? sampleSource : "Engine";
	threadId_ = GetCurrentProfilerThreadId();
	gameObjectId_ = gameObjectId;
	callDepth_ = static_cast<int32_t>(g_activeCpuProfilerScopes.size());
	parentPath_ = g_activeCpuProfilerScopes.empty()
		? std::string{}
		: g_activeCpuProfilerScopes.back().callPath;
	callPath_ = parentPath_.empty() ? sampleName_ : parentPath_ + " > " + sampleName_;
	g_activeCpuProfilerScopes.push_back({callPath_, 0.0f});
	const EditorProfilerAllocationSnapshot allocationSnapshot =
		GetEditorProfilerAllocationSnapshot();
	allocationCountAtStart_ = allocationSnapshot.allocationCount;
	allocatedBytesAtStart_ = allocationSnapshot.allocatedBytes;
	startTime_ = std::chrono::steady_clock::now();
	isMeasuring_ = true;
}

EditorProfilerManager::Scope::Scope(
	EditorProfilerManager* profilerManager,
	const char* scriptEventName,
	const std::string& scriptDllPath,
	int32_t gameObjectId) {
	if (profilerManager == nullptr || !profilerManager->IsEnabled() ||
		scriptEventName == nullptr || scriptEventName[0] == '\0') {
		return;
	}

	const std::filesystem::path sourcePath(scriptDllPath);
	sampleSource_ = sourcePath.stem().string();

	if (sampleSource_.empty()) {
		sampleSource_ = "Native Script";
	}

	sampleName_ = sampleSource_ + "." + scriptEventName;
	profilerManager_ = profilerManager;
	threadId_ = GetCurrentProfilerThreadId();
	gameObjectId_ = gameObjectId;
	callDepth_ = static_cast<int32_t>(g_activeCpuProfilerScopes.size());
	parentPath_ = g_activeCpuProfilerScopes.empty()
		? std::string{}
		: g_activeCpuProfilerScopes.back().callPath;
	callPath_ = parentPath_.empty() ? sampleName_ : parentPath_ + " > " + sampleName_;
	g_activeCpuProfilerScopes.push_back({callPath_, 0.0f});
	const EditorProfilerAllocationSnapshot allocationSnapshot =
		GetEditorProfilerAllocationSnapshot();
	allocationCountAtStart_ = allocationSnapshot.allocationCount;
	allocatedBytesAtStart_ = allocationSnapshot.allocatedBytes;
	startTime_ = std::chrono::steady_clock::now();
	isMeasuring_ = true;
}

EditorProfilerManager::Scope::~Scope() {
	if (!isMeasuring_ || profilerManager_ == nullptr) {
		return;
	}

	const std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
	const EditorProfilerAllocationSnapshot allocationSnapshot =
		GetEditorProfilerAllocationSnapshot();
	const float elapsedMilliseconds =
		std::chrono::duration<float, std::milli>(endTime - startTime_).count();
	float childMilliseconds = 0.0f;

	if (!g_activeCpuProfilerScopes.empty()) {
		childMilliseconds = g_activeCpuProfilerScopes.back().childMilliseconds;
		g_activeCpuProfilerScopes.pop_back();
	}

	if (!g_activeCpuProfilerScopes.empty()) {
		g_activeCpuProfilerScopes.back().childMilliseconds += elapsedMilliseconds;
	}

	profilerManager_->Record(
		sampleName_.c_str(),
		sampleSource_.c_str(),
		parentPath_.c_str(),
		callPath_.c_str(),
		elapsedMilliseconds,
		(std::max)(elapsedMilliseconds - childMilliseconds, 0.0f),
		allocationSnapshot.allocationCount - allocationCountAtStart_,
		allocationSnapshot.allocatedBytes - allocatedBytesAtStart_,
		threadId_,
		gameObjectId_,
		callDepth_);
}

void EditorProfilerManager::SetEnabled(bool isEnabled) {
	if (IsEnabled() == isEnabled) {
		return;
	}

	if (isEnabled) {
		Reset();
		mainThreadId_ = GetCurrentProfilerThreadId();
		measurementStartTime_ = std::chrono::steady_clock::now();
		isEnabled_.store(true, std::memory_order_release);
		SetEditorProfilerAllocationTrackingEnabled(true);
		return;
	}

	completedMeasurementSeconds_ = GetElapsedMeasurementSeconds();
	isEnabled_.store(false, std::memory_order_release);
	SetEditorProfilerAllocationTrackingEnabled(false);
}

bool EditorProfilerManager::IsEnabled() const {
	return isEnabled_.load(std::memory_order_acquire);
}

void EditorProfilerManager::SetMeasurementDurationSeconds(float durationSeconds) {
	if (IsEnabled()) {
		return;
	}

	measurementDurationSeconds_ = (std::clamp)(durationSeconds, 0.5f, 60.0f);
}

float EditorProfilerManager::GetMeasurementDurationSeconds() const {
	return measurementDurationSeconds_;
}

float EditorProfilerManager::GetElapsedMeasurementSeconds() const {
	if (!IsEnabled()) {
		return completedMeasurementSeconds_;
	}

	const std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
	return std::chrono::duration<float>(currentTime - measurementStartTime_).count();
}

void EditorProfilerManager::UpdateMeasurement() {
	if (!IsEnabled()) {
		return;
	}

	if (GetElapsedMeasurementSeconds() >= measurementDurationSeconds_) {
		SetEnabled(false);
	}
}

void EditorProfilerManager::BeginGpuFrame() {
	pendingGpuSamples_.clear();
	activeGpuSampleIndices_.clear();
	nextGpuQueryIndex_ = 2u;
	g_activeGpuProfilerManager = IsEnabled() ? this : nullptr;
}

uint32_t EditorProfilerManager::BeginGpuEvent(
	ID3D12GraphicsCommandList* commandList,
	ID3D12QueryHeap* queryHeap,
	const char* eventName) {
	if (!IsEnabled() || commandList == nullptr || queryHeap == nullptr ||
		eventName == nullptr || eventName[0] == '\0' ||
		nextGpuQueryIndex_ + 1u >= kEditorProfilerTimestampQueryCapacity) {
		return kInvalidEditorProfilerGpuEvent;
	}

	PendingGpuSample pendingSample{};
	pendingSample.name = eventName;
	pendingSample.parentPath = activeGpuSampleIndices_.empty()
		? std::string{}
		: pendingGpuSamples_[activeGpuSampleIndices_.back()].callPath;
	pendingSample.callPath = pendingSample.parentPath.empty()
		? pendingSample.name
		: pendingSample.parentPath + " > " + pendingSample.name;
	pendingSample.beginQueryIndex = nextGpuQueryIndex_;
	pendingSample.endQueryIndex = nextGpuQueryIndex_ + 1u;
	pendingSample.callDepth = static_cast<int32_t>(activeGpuSampleIndices_.size());
	nextGpuQueryIndex_ += 2u;
	commandList->EndQuery(
		queryHeap,
		D3D12_QUERY_TYPE_TIMESTAMP,
		pendingSample.beginQueryIndex);
	pendingGpuSamples_.push_back(std::move(pendingSample));
	const uint32_t eventIndex = static_cast<uint32_t>(pendingGpuSamples_.size() - 1u);
	activeGpuSampleIndices_.push_back(eventIndex);
	return eventIndex;
}

void EditorProfilerManager::EndGpuEvent(
	ID3D12GraphicsCommandList* commandList,
	ID3D12QueryHeap* queryHeap,
	uint32_t eventIndex) {
	if (eventIndex == kInvalidEditorProfilerGpuEvent || commandList == nullptr ||
		queryHeap == nullptr || eventIndex >= pendingGpuSamples_.size()) {
		return;
	}

	commandList->EndQuery(
		queryHeap,
		D3D12_QUERY_TYPE_TIMESTAMP,
		pendingGpuSamples_[eventIndex].endQueryIndex);

	if (!activeGpuSampleIndices_.empty() && activeGpuSampleIndices_.back() == eventIndex) {
		activeGpuSampleIndices_.pop_back();
	}
}

uint32_t EditorProfilerManager::GetGpuTimestampQueryCount() const {
	return nextGpuQueryIndex_;
}

void EditorProfilerManager::ResolveGpuFrame(
	const std::uint64_t* timestampData,
	uint32_t timestampCount,
	std::uint64_t timestampFrequency) {
	g_activeGpuProfilerManager = nullptr;

	if (!IsEnabled() || timestampData == nullptr || timestampFrequency == 0u) {
		return;
	}

	std::vector<float> elapsedMillisecondsList(pendingGpuSamples_.size(), 0.0f);
	std::vector<float> childMillisecondsList(pendingGpuSamples_.size(), 0.0f);
	std::vector<std::uint64_t> aggregatedDrawCallCounts(pendingGpuSamples_.size(), 0u);
	std::vector<std::uint64_t> aggregatedDispatchCounts(pendingGpuSamples_.size(), 0u);

	for (std::size_t sampleIndex = 0u; sampleIndex < pendingGpuSamples_.size(); sampleIndex++) {
		const PendingGpuSample& pendingSample = pendingGpuSamples_[sampleIndex];
		aggregatedDrawCallCounts[sampleIndex] = pendingSample.drawCallCount;
		aggregatedDispatchCounts[sampleIndex] = pendingSample.dispatchCount;

		if (pendingSample.endQueryIndex >= timestampCount ||
			timestampData[pendingSample.endQueryIndex] < timestampData[pendingSample.beginQueryIndex]) {
			continue;
		}

		const double elapsedTicks = static_cast<double>(
			timestampData[pendingSample.endQueryIndex] - timestampData[pendingSample.beginQueryIndex]);
		elapsedMillisecondsList[sampleIndex] = static_cast<float>(
			elapsedTicks * 1000.0 / static_cast<double>(timestampFrequency));
	}

	//================================================================
	// GPU階層の子時間とDraw/Dispatch数を親へ集約
	//================================================================

	for (std::size_t childIndex = pendingGpuSamples_.size(); childIndex > 0u; childIndex--) {
		const std::size_t sampleIndex = childIndex - 1u;
		const PendingGpuSample& childSample = pendingGpuSamples_[sampleIndex];

		if (childSample.parentPath.empty()) {
			continue;
		}

		for (std::size_t parentIndex = 0u; parentIndex < sampleIndex; parentIndex++) {
			if (pendingGpuSamples_[parentIndex].callPath != childSample.parentPath) {
				continue;
			}

			childMillisecondsList[parentIndex] += elapsedMillisecondsList[sampleIndex];
			aggregatedDrawCallCounts[parentIndex] += aggregatedDrawCallCounts[sampleIndex];
			aggregatedDispatchCounts[parentIndex] += aggregatedDispatchCounts[sampleIndex];
			break;
		}
	}

	std::lock_guard<std::mutex> sampleLock(samplesMutex_);

	for (std::size_t sampleIndex = 0u; sampleIndex < pendingGpuSamples_.size(); sampleIndex++) {
		const PendingGpuSample& pendingSample = pendingGpuSamples_[sampleIndex];
		const float elapsedMilliseconds = elapsedMillisecondsList[sampleIndex];

		if (elapsedMilliseconds <= 0.0f) {
			continue;
		}

		const float selfMilliseconds = (std::max)(
			elapsedMilliseconds - childMillisecondsList[sampleIndex],
			0.0f);
		const std::string sampleKey = "GPU\n" + pendingSample.callPath;
		EditorProfilerSample& sample = samples_[sampleKey];
		sample.name = pendingSample.name;
		sample.source = "GPU";
		sample.parentPath = pendingSample.parentPath;
		sample.callPath = pendingSample.callPath;
		sample.threadName = "GPU Queue";
		sample.callDepth = pendingSample.callDepth;
		sample.isGpuSample = true;
		sample.latestMilliseconds = elapsedMilliseconds;
		sample.peakMilliseconds = (std::max)(sample.peakMilliseconds, elapsedMilliseconds);
		sample.totalMilliseconds += elapsedMilliseconds;
		sample.selfMilliseconds += selfMilliseconds;
		sample.sampleCount++;
		sample.drawCallCount += aggregatedDrawCallCounts[sampleIndex];
		sample.dispatchCount += aggregatedDispatchCounts[sampleIndex];
		sample.averageMilliseconds =
			sample.totalMilliseconds / static_cast<float>(sample.sampleCount);
	}
}

void EditorProfilerManager::RecordDrawCall() {
	if (!IsEnabled() || activeGpuSampleIndices_.empty()) {
		return;
	}

	pendingGpuSamples_[activeGpuSampleIndices_.back()].drawCallCount++;
}

void EditorProfilerManager::RecordDispatch() {
	if (!IsEnabled() || activeGpuSampleIndices_.empty()) {
		return;
	}

	pendingGpuSamples_[activeGpuSampleIndices_.back()].dispatchCount++;
}

void EditorProfilerManager::Record(
	const char* sampleName,
	const char* sampleSource,
	const char* parentPath,
	const char* callPath,
	float elapsedMilliseconds,
	float selfMilliseconds,
	std::uint64_t allocationCount,
	std::uint64_t allocatedBytes,
	std::uint64_t threadId,
	int32_t gameObjectId,
	int32_t callDepth) {
	if (!IsEnabled() || sampleName == nullptr || sampleName[0] == '\0') {
		return;
	}

	const std::string sourceName =
		sampleSource != nullptr && sampleSource[0] != '\0' ? sampleSource : "Engine";
	const std::string resolvedCallPath =
		callPath != nullptr && callPath[0] != '\0' ? callPath : sampleName;
	const std::string sampleKey =
		std::to_string(threadId) + "\n" + std::to_string(gameObjectId) + "\n" +
		sourceName + "\n" + resolvedCallPath;
	std::lock_guard<std::mutex> sampleLock(samplesMutex_);
	EditorProfilerSample& sample = samples_[sampleKey];
	sample.name = sampleName;
	sample.source = sourceName;
	sample.parentPath = parentPath != nullptr ? parentPath : "";
	sample.callPath = resolvedCallPath;
	sample.threadId = threadId;
	sample.threadName = threadId == mainThreadId_
		? "Main"
		: "Worker " + std::to_string(threadId);
	sample.gameObjectId = gameObjectId;
	sample.callDepth = callDepth;
	sample.latestMilliseconds = (std::max)(elapsedMilliseconds, 0.0f);
	sample.peakMilliseconds = (std::max)(sample.peakMilliseconds, sample.latestMilliseconds);
	sample.totalMilliseconds += sample.latestMilliseconds;
	sample.selfMilliseconds += (std::max)(selfMilliseconds, 0.0f);
	sample.sampleCount++;
	sample.allocationCount += allocationCount;
	sample.allocatedBytes += allocatedBytes;
	sample.averageMilliseconds =
		sample.totalMilliseconds / static_cast<float>(sample.sampleCount);
}

void EditorProfilerManager::Reset() {
	std::lock_guard<std::mutex> sampleLock(samplesMutex_);
	samples_.clear();
	completedMeasurementSeconds_ = 0.0f;
}

std::vector<EditorProfilerSample> EditorProfilerManager::GetSortedSamples() const {
	std::vector<EditorProfilerSample> sortedSamples;
	std::lock_guard<std::mutex> sampleLock(samplesMutex_);
	sortedSamples.reserve(samples_.size());

	for (const auto& samplePair : samples_) {
		sortedSamples.push_back(samplePair.second);
	}

	std::sort(
		sortedSamples.begin(),
		sortedSamples.end(),
		[](const EditorProfilerSample& firstSample, const EditorProfilerSample& secondSample) {
			return firstSample.totalMilliseconds > secondSample.totalMilliseconds;
		});
	return sortedSamples;
}

void RecordEditorProfilerDrawCall() {
	if (g_activeGpuProfilerManager != nullptr) {
		g_activeGpuProfilerManager->RecordDrawCall();
	}
}

void RecordEditorProfilerDispatch() {
	if (g_activeGpuProfilerManager != nullptr) {
		g_activeGpuProfilerManager->RecordDispatch();
	}
}
