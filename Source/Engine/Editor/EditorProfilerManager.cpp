#include "EditorProfilerManager.h"

#include <algorithm>

EditorProfilerManager::Scope::Scope(
	EditorProfilerManager& profilerManager,
	const char* sampleName)
	: profilerManager_(&profilerManager),
	  sampleName_(sampleName),
	  startTime_(std::chrono::steady_clock::now()) {
}

EditorProfilerManager::Scope::~Scope() {
	if (profilerManager_ == nullptr || sampleName_ == nullptr) {
		return;
	}

	const std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
	const float elapsedMilliseconds =
		std::chrono::duration<float, std::milli>(endTime - startTime_).count();
	profilerManager_->Record(sampleName_, elapsedMilliseconds);
}

void EditorProfilerManager::Record(const char* sampleName, float elapsedMilliseconds) {
	if (sampleName == nullptr || sampleName[0] == '\0') {
		return;
	}

	EditorProfilerSample& sample = samples_[sampleName];
	sample.name = sampleName;
	sample.latestMilliseconds = (std::max)(elapsedMilliseconds, 0.0f);
	sample.peakMilliseconds = (std::max)(sample.peakMilliseconds, sample.latestMilliseconds);
	sample.sampleCount++;

	if (sample.sampleCount == 1u) {
		sample.averageMilliseconds = sample.latestMilliseconds;
	}
	else {
		constexpr float smoothingRatio = 0.1f;
		sample.averageMilliseconds +=
			(sample.latestMilliseconds - sample.averageMilliseconds) * smoothingRatio;
	}
}

void EditorProfilerManager::Reset() {
	samples_.clear();
}

std::vector<EditorProfilerSample> EditorProfilerManager::GetSortedSamples() const {
	std::vector<EditorProfilerSample> sortedSamples;
	sortedSamples.reserve(samples_.size());

	for (const auto& samplePair : samples_) {
		sortedSamples.push_back(samplePair.second);
	}

	std::sort(
		sortedSamples.begin(),
		sortedSamples.end(),
		[](const EditorProfilerSample& firstSample, const EditorProfilerSample& secondSample) {
			return firstSample.averageMilliseconds > secondSample.averageMilliseconds;
		});
	return sortedSamples;
}
