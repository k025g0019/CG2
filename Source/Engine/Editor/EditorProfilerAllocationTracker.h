#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

struct EditorProfilerAllocationSnapshot {
	std::uint64_t allocationCount = 0u;
	std::uint64_t allocatedBytes = 0u;
};

void SetEditorProfilerAllocationTrackingEnabled(bool isEnabled);
EditorProfilerAllocationSnapshot GetEditorProfilerAllocationSnapshot();

namespace EditorProfilerAllocationTrackerDetail {
	// Profiler が OFF のときに通る唯一の経路。Engine 全体の new が毎回ここを通るため、
	// 判定だけは Header 側へ置いて inline 展開させる。実体は .cpp にある。
	extern std::atomic_bool g_isAllocationTrackingEnabled;

	// 計測が ON のときだけ呼ぶ集計本体。thread_local へ触るのでこちらは非 inline。
	void RecordAllocation(std::size_t allocationSize) noexcept;
}

// Engine 全体の operator new から毎回呼ばれる。Profiler が OFF のときは
// atomic の relaxed load 1 回と分岐だけで戻るため、翻訳単位をまたぐ関数呼び出しが
// 発生しない。従来は .cpp 側の非 inline 関数だったので、Debug 構成では
// 「確保するたびに常に call が 1 回増える」状態だった。
inline void RecordEditorProfilerAllocation(std::size_t allocationSize) noexcept {
	if (!EditorProfilerAllocationTrackerDetail::g_isAllocationTrackingEnabled.load(
			std::memory_order_relaxed)) {
		return;
	}

	EditorProfilerAllocationTrackerDetail::RecordAllocation(allocationSize);
}
