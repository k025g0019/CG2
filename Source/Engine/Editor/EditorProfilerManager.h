#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorProfilerSample {
	std::string name;
	float latestMilliseconds = 0.0f;
	float averageMilliseconds = 0.0f;
	float peakMilliseconds = 0.0f;
	std::uint64_t sampleCount = 0u;
};

class EditorProfilerManager {
public:
	class Scope {
	public:
		Scope(EditorProfilerManager& profilerManager, const char* sampleName);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		EditorProfilerManager* profilerManager_ = nullptr;
		const char* sampleName_ = nullptr;
		std::chrono::steady_clock::time_point startTime_{};
	};

	void Record(const char* sampleName, float elapsedMilliseconds);
	void Reset();
	std::vector<EditorProfilerSample> GetSortedSamples() const;

private:
	std::unordered_map<std::string, EditorProfilerSample> samples_;
};

#pragma warning(pop)
