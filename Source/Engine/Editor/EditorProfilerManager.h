#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D12GraphicsCommandList;
struct ID3D12QueryHeap;

constexpr uint32_t kEditorProfilerTimestampQueryCapacity = 128u;
constexpr uint32_t kInvalidEditorProfilerGpuEvent = UINT32_MAX;

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorProfilerSample {
	std::string name;
	std::string source;
	std::string parentPath;
	std::string callPath;
	std::string threadName;
	float latestMilliseconds = 0.0f;
	float averageMilliseconds = 0.0f;
	float peakMilliseconds = 0.0f;
	float totalMilliseconds = 0.0f;
	float selfMilliseconds = 0.0f;
	std::uint64_t sampleCount = 0u;
	std::uint64_t allocationCount = 0u;
	std::uint64_t allocatedBytes = 0u;
	std::uint64_t drawCallCount = 0u;
	std::uint64_t dispatchCount = 0u;
	std::uint64_t threadId = 0u;
	int32_t gameObjectId = -1;
	int32_t callDepth = 0;
	bool isGpuSample = false;
};

class EditorProfilerManager {
public:
	class Scope {
	public:
		Scope(
			EditorProfilerManager& profilerManager,
			const char* sampleName,
			const char* sampleSource = "Engine",
			int32_t gameObjectId = -1);
		Scope(
			EditorProfilerManager* profilerManager,
			const char* scriptEventName,
			const std::string& scriptDllPath,
			int32_t gameObjectId = -1);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		EditorProfilerManager* profilerManager_ = nullptr;
		std::string sampleName_;
		std::string sampleSource_;
		std::string parentPath_;
		std::string callPath_;
		std::chrono::steady_clock::time_point startTime_{};
		std::uint64_t allocationCountAtStart_ = 0u;
		std::uint64_t allocatedBytesAtStart_ = 0u;
		std::uint64_t threadId_ = 0u;
		int32_t gameObjectId_ = -1;
		int32_t callDepth_ = 0;
		bool isMeasuring_ = false;
	};

	void SetEnabled(bool isEnabled);
	bool IsEnabled() const;
	void SetMeasurementDurationSeconds(float durationSeconds);
	float GetMeasurementDurationSeconds() const;
	float GetElapsedMeasurementSeconds() const;
	void UpdateMeasurement();
	void BeginGpuFrame();
	uint32_t BeginGpuEvent(
		ID3D12GraphicsCommandList* commandList,
		ID3D12QueryHeap* queryHeap,
		const char* eventName);
	void EndGpuEvent(
		ID3D12GraphicsCommandList* commandList,
		ID3D12QueryHeap* queryHeap,
		uint32_t eventIndex);
	uint32_t GetGpuTimestampQueryCount() const;
	void ResolveGpuFrame(
		const std::uint64_t* timestampData,
		uint32_t timestampCount,
		std::uint64_t timestampFrequency);
	void RecordDrawCall();
	void RecordDispatch();
	void Record(
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
		int32_t callDepth);
	void Reset();
	std::vector<EditorProfilerSample> GetSortedSamples() const;

	//================================================================
	// Frame History(直近フレームの CPU / GPU 時間)
	//================================================================
	// 「いつ重くなったか」を見るための時系列。集計値だけでは分からないスパイクを拾う。
	static constexpr int32_t kFrameHistoryCapacity = 240;

	// 毎フレーム1回呼ぶ。CPU時間は前回呼び出しからの実時間、GPU時間はResolve済みの値を使う。
	void PushFrameHistory(float cpuMilliseconds, float gpuMilliseconds);
	// 古い順に並べた履歴を返す(ImGui::PlotLines へそのまま渡せる形)。
	void GetFrameHistory(
		std::vector<float>& outCpuMilliseconds,
		std::vector<float>& outGpuMilliseconds) const;
	float GetLastCpuFrameMilliseconds() const;
	float GetLastGpuFrameMilliseconds() const;
	float GetAverageCpuFrameMilliseconds() const;

private:
	struct PendingGpuSample {
		std::string name;
		std::string parentPath;
		std::string callPath;
		uint32_t beginQueryIndex = 0u;
		uint32_t endQueryIndex = 0u;
		std::uint64_t drawCallCount = 0u;
		std::uint64_t dispatchCount = 0u;
		int32_t callDepth = 0;
	};

	std::unordered_map<std::string, EditorProfilerSample> samples_;
	std::vector<PendingGpuSample> pendingGpuSamples_;
	std::vector<uint32_t> activeGpuSampleIndices_;
	std::chrono::steady_clock::time_point measurementStartTime_{};
	mutable std::mutex samplesMutex_;
	float measurementDurationSeconds_ = 5.0f;
	float completedMeasurementSeconds_ = 0.0f;
	std::uint64_t mainThreadId_ = 0u;
	uint32_t nextGpuQueryIndex_ = 2u;
	std::atomic_bool isEnabled_ = false;

	// Frame History はリングバッファで保持し、確保を増やさず一定量だけ残す。
	std::vector<float> cpuFrameHistory_;
	std::vector<float> gpuFrameHistory_;
	int32_t frameHistoryWriteIndex_ = 0;
	int32_t frameHistoryCount_ = 0;
	float lastCpuFrameMilliseconds_ = 0.0f;
	float lastGpuFrameMilliseconds_ = 0.0f;
};

void RecordEditorProfilerDrawCall();
void RecordEditorProfilerDispatch();

#pragma warning(pop)
