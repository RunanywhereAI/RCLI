#pragma once

#include "types.h"
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <atomic>
#include <stdexcept>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace rastack {

// Pre-allocated memory pool with zero runtime allocation.
// All memory is allocated once at startup. After init, alloc() never calls malloc.
//
// Usage:
//   MemoryPool pool(64 * 1024 * 1024);  // 64MB pool
//   float* buf = pool.alloc<float>(4096);  // 4096 floats, cache-aligned
//
class MemoryPool {
public:
    explicit MemoryPool(size_t total_bytes)
        : total_size_(total_bytes)
        , used_(0)
        , high_water_(0)
    {
        bool used_huge_pages = false;

#ifdef __APPLE__
        // macOS: Try superpage (2MB) allocation for large pools (>= 4MB)
        if (total_bytes >= 4 * 1024 * 1024) {
            // Round up to 2MB boundary for superpages
            size_t superpage_size = 2 * 1024 * 1024;
            size_t rounded = (total_bytes + superpage_size - 1) & ~(superpage_size - 1);
            vm_address_t addr = 0;
            kern_return_t kr = vm_allocate(
                mach_task_self(), &addr, rounded,
                VM_FLAGS_ANYWHERE | VM_FLAGS_SUPERPAGE_SIZE_2MB);
            if (kr == KERN_SUCCESS) {
                base_ = reinterpret_cast<void*>(addr);
                total_size_ = rounded;  // update to actual allocated size
                used_huge_pages = true;
                used_huge_pages_ = true;
            }
        }
#elif defined(__linux__)
        // Linux: allocate with mmap + MADV_HUGEPAGE hint
        if (total_bytes >= 4 * 1024 * 1024) {
            size_t superpage_size = 2 * 1024 * 1024;
            size_t rounded = (total_bytes + superpage_size - 1) & ~(superpage_size - 1);
            base_ = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (base_ != MAP_FAILED) {
                madvise(base_, rounded, MADV_HUGEPAGE);
                total_size_ = rounded;
                used_huge_pages = true;
                used_huge_pages_ = true;
            } else {
                base_ = nullptr;
            }
        }
#endif

        // Fallback: standard page-aligned allocation
        if (!used_huge_pages) {
            if (posix_memalign(&base_, 4096, total_bytes) != 0) {
                throw std::runtime_error("MemoryPool: failed to allocate " +
                                         std::to_string(total_bytes) + " bytes");
            }
        }

        // Zero-fill to avoid page faults during runtime
        std::memset(base_, 0, total_size_);

        if (used_huge_pages) {
            fprintf(stderr, "[Pool] Using huge pages (2MB superpages)\n");
        }
    }

    ~MemoryPool() {
        if (base_) {
#ifdef __APPLE__
            if (used_huge_pages_) {
                vm_deallocate(mach_task_self(),
                    reinterpret_cast<vm_address_t>(base_), total_size_);
            } else {
                std::free(base_);
            }
#elif defined(__linux__)
            if (used_huge_pages_) {
                munmap(base_, total_size_);
            } else {
                std::free(base_);
            }
#else
            std::free(base_);
#endif
            base_ = nullptr;
        }
    }

    // Non-copyable, non-movable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // Allocate count elements of type T, cache-line aligned.
    // This is wait-free and never calls malloc.
    template<typename T>
    T* alloc(size_t count) {
        size_t bytes = count * sizeof(T);
        return reinterpret_cast<T*>(alloc_raw(bytes, alignof(T) < CACHE_LINE_SIZE ? CACHE_LINE_SIZE : alignof(T)));
    }

    // Allocate raw bytes with specified alignment
    void* alloc_raw(size_t bytes, size_t alignment = CACHE_LINE_SIZE) {
        // Align up the current offset
        size_t current = used_.load(std::memory_order_relaxed);
        size_t aligned = (current + alignment - 1) & ~(alignment - 1);
        size_t new_used = aligned + bytes;

        if (new_used > total_size_) {
            // In release: return nullptr (caller checks).
            // In debug: crash immediately.
            assert(false && "MemoryPool: out of memory");
            return nullptr;
        }

        // CAS loop for thread safety (though typically single-threaded at init)
        while (!used_.compare_exchange_weak(current, new_used,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
            aligned = (current + alignment - 1) & ~(alignment - 1);
            new_used = aligned + bytes;
            if (new_used > total_size_) {
                return nullptr;
            }
        }

        // Update high-water mark
        size_t hw = high_water_.load(std::memory_order_relaxed);
        while (new_used > hw && !high_water_.compare_exchange_weak(
            hw, new_used, std::memory_order_relaxed)) {}

        return static_cast<char*>(base_) + aligned;
    }

    // Reset pool (reuse all memory). Only call when no references are live.
    void reset() {
        used_.store(0, std::memory_order_release);
    }

    // Scratch region support: mark current position, allocate temporaries,
    // then reset back to mark when done. O(1) and wait-free.
    size_t mark() const { return used_.load(std::memory_order_relaxed); }
    void reset_to_mark(size_t m) { used_.store(m, std::memory_order_release); }

    // Stats
    size_t total_size()     const { return total_size_; }
    size_t used_bytes()     const { return used_.load(std::memory_order_relaxed); }
    size_t remaining()      const { return total_size_ - used_bytes(); }
    size_t high_water_mark()const { return high_water_.load(std::memory_order_relaxed); }

    float utilization_pct() const {
        return 100.0f * static_cast<float>(used_bytes()) / static_cast<float>(total_size_);
    }

private:
    void*             base_ = nullptr;
    size_t            total_size_;
    bool              used_huge_pages_ = false;
    std::atomic<size_t> used_;
    std::atomic<size_t> high_water_;
};

} // namespace rastack
