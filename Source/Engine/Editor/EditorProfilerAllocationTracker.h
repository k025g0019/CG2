#pragma once

#include <cstddef>
#include <cstdint>

struct EditorProfilerAllocationSnapshot {
	std::uint64_t allocationCount = 0u;
	std::uint64_t allocatedBytes = 0u;
};

void SetEditorProfilerAllocationTrackingEnabled(bool isEnabled);
void RecordEditorProfilerAllocation(std::size_t allocationSize);
EditorProfilerAllocationSnapshot GetEditorProfilerAllocationSnapshot();
