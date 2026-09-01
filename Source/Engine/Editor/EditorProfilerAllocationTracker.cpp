#include "EditorProfilerAllocationTracker.h"

#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace {
	std::atomic_bool g_isAllocationTrackingEnabled = false;
	thread_local EditorProfilerAllocationSnapshot g_threadAllocationSnapshot{};

	void* AllocateMemory(std::size_t allocationSize) {
		const std::size_t actualSize = allocationSize > 0u ? allocationSize : 1u;
		void* allocation = std::malloc(actualSize);

		if (allocation == nullptr) {
			throw std::bad_alloc();
		}

		RecordEditorProfilerAllocation(actualSize);
		return allocation;
	}

	void* AllocateAlignedMemory(std::size_t allocationSize, std::size_t alignment) {
		const std::size_t actualSize = allocationSize > 0u ? allocationSize : 1u;
		void* allocation = _aligned_malloc(actualSize, alignment);

		if (allocation == nullptr) {
			throw std::bad_alloc();
		}

		RecordEditorProfilerAllocation(actualSize);
		return allocation;
	}
}

void SetEditorProfilerAllocationTrackingEnabled(bool isEnabled) {
	g_isAllocationTrackingEnabled.store(isEnabled, std::memory_order_release);
}

void RecordEditorProfilerAllocation(std::size_t allocationSize) {
	if (!g_isAllocationTrackingEnabled.load(std::memory_order_relaxed)) {
		return;
	}

	g_threadAllocationSnapshot.allocationCount++;
	g_threadAllocationSnapshot.allocatedBytes += static_cast<std::uint64_t>(allocationSize);
}

EditorProfilerAllocationSnapshot GetEditorProfilerAllocationSnapshot() {
	return g_threadAllocationSnapshot;
}

void* operator new(std::size_t allocationSize) {
	return AllocateMemory(allocationSize);
}

void* operator new[](std::size_t allocationSize) {
	return AllocateMemory(allocationSize);
}

void operator delete(void* allocation) noexcept {
	std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
	std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
	std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
	std::free(allocation);
}

void* operator new(std::size_t allocationSize, const std::nothrow_t&) noexcept {
	try {
		return AllocateMemory(allocationSize);
	}
	catch (...) {
		return nullptr;
	}
}

void* operator new[](std::size_t allocationSize, const std::nothrow_t&) noexcept {
	try {
		return AllocateMemory(allocationSize);
	}
	catch (...) {
		return nullptr;
	}
}

void operator delete(void* allocation, const std::nothrow_t&) noexcept {
	std::free(allocation);
}

void operator delete[](void* allocation, const std::nothrow_t&) noexcept {
	std::free(allocation);
}

void* operator new(std::size_t allocationSize, std::align_val_t alignment) {
	return AllocateAlignedMemory(allocationSize, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t allocationSize, std::align_val_t alignment) {
	return AllocateAlignedMemory(allocationSize, static_cast<std::size_t>(alignment));
}

void operator delete(void* allocation, std::align_val_t) noexcept {
	_aligned_free(allocation);
}

void operator delete[](void* allocation, std::align_val_t) noexcept {
	_aligned_free(allocation);
}

void operator delete(void* allocation, std::size_t, std::align_val_t) noexcept {
	_aligned_free(allocation);
}

void operator delete[](void* allocation, std::size_t, std::align_val_t) noexcept {
	_aligned_free(allocation);
}
