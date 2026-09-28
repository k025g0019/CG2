#include "EditorProfilerAllocationTracker.h"

#include <cstdlib>
#include <malloc.h>
#include <new>

namespace EditorProfilerAllocationTrackerDetail {
	std::atomic_bool g_isAllocationTrackingEnabled = false;
}

namespace {
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

	// nothrow 版は失敗を例外ではなく nullptr で返す契約なので、確保失敗を
	// そのまま外へ伝える。throw 版を try/catch で包むより意図が読みやすい。
	void* AllocateMemoryNoThrow(std::size_t allocationSize) noexcept {
		const std::size_t actualSize = allocationSize > 0u ? allocationSize : 1u;
		void* allocation = std::malloc(actualSize);

		if (allocation != nullptr) {
			RecordEditorProfilerAllocation(actualSize);
		}

		return allocation;
	}

	void* AllocateAlignedMemoryNoThrow(std::size_t allocationSize, std::size_t alignment) noexcept {
		const std::size_t actualSize = allocationSize > 0u ? allocationSize : 1u;
		void* allocation = _aligned_malloc(actualSize, alignment);

		if (allocation != nullptr) {
			RecordEditorProfilerAllocation(actualSize);
		}

		return allocation;
	}
}

void EditorProfilerAllocationTrackerDetail::RecordAllocation(std::size_t allocationSize) noexcept {
	g_threadAllocationSnapshot.allocationCount++;
	g_threadAllocationSnapshot.allocatedBytes += static_cast<std::uint64_t>(allocationSize);
}

void SetEditorProfilerAllocationTrackingEnabled(bool isEnabled) {
	EditorProfilerAllocationTrackerDetail::g_isAllocationTrackingEnabled.store(
		isEnabled, std::memory_order_release);
}

EditorProfilerAllocationSnapshot GetEditorProfilerAllocationSnapshot() {
	return g_threadAllocationSnapshot;
}

// ============================================================================
// グローバル operator new / delete の置き換え。
//
// 置き換えは「全部そろえる」ことが前提の機能である。1 つでも欠けると、
// 欠けた形だけ CRT 側の実装が使われ、malloc 系と _aligned_malloc 系の
// 取り違えが起きる。free / _aligned_free は互換ではないため、これは
// Heap 破壊として現れる。
//
// C++17 以降の置き換え可能な確保関数は 20 個ある。以前は aligned かつ
// nothrow の 4 個 (new/new[]/delete/delete[]) が欠けていたので、そこを補って
// 「malloc で確保したものは free、_aligned_malloc で確保したものは
// _aligned_free」の対応を全経路で閉じている。
// ============================================================================

//--------------------------------------------------------------------
// 既定アライメント : std::malloc / std::free の対
//--------------------------------------------------------------------

void* operator new(std::size_t allocationSize) {
	return AllocateMemory(allocationSize);
}

void* operator new[](std::size_t allocationSize) {
	return AllocateMemory(allocationSize);
}

void* operator new(std::size_t allocationSize, const std::nothrow_t&) noexcept {
	return AllocateMemoryNoThrow(allocationSize);
}

void* operator new[](std::size_t allocationSize, const std::nothrow_t&) noexcept {
	return AllocateMemoryNoThrow(allocationSize);
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

void operator delete(void* allocation, const std::nothrow_t&) noexcept {
	std::free(allocation);
}

void operator delete[](void* allocation, const std::nothrow_t&) noexcept {
	std::free(allocation);
}

//--------------------------------------------------------------------
// 拡張アライメント : _aligned_malloc / _aligned_free の対
//--------------------------------------------------------------------

void* operator new(std::size_t allocationSize, std::align_val_t alignment) {
	return AllocateAlignedMemory(allocationSize, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t allocationSize, std::align_val_t alignment) {
	return AllocateAlignedMemory(allocationSize, static_cast<std::size_t>(alignment));
}

// この 4 つが以前は欠けていた。欠けていると new (align, nothrow) だけ CRT 側の
// 確保が使われ、下の delete で _aligned_free に渡る組み合わせが成立してしまう。
void* operator new(
	std::size_t allocationSize, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	return AllocateAlignedMemoryNoThrow(allocationSize, static_cast<std::size_t>(alignment));
}

void* operator new[](
	std::size_t allocationSize, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	return AllocateAlignedMemoryNoThrow(allocationSize, static_cast<std::size_t>(alignment));
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

void operator delete(void* allocation, std::align_val_t, const std::nothrow_t&) noexcept {
	_aligned_free(allocation);
}

void operator delete[](void* allocation, std::align_val_t, const std::nothrow_t&) noexcept {
	_aligned_free(allocation);
}
