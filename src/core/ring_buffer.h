#pragma once

#include "types.h"
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace rastack {

// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
// Wait-free for both read and write. Zero-copy where possible.
//
// Uses power-of-2 sizing for fast modulo (bitwise AND).
// Cache-line padded to prevent false sharing between producer and consumer.
//
// Usage:
//   RingBuffer<float> buf(pool, 16384);  // 16K float samples
//   buf.write(audio_data, 480);          // producer writes
//   buf.read(output, 480);               // consumer reads
//
template<typename T>
class RingBuffer {
public:
    // Construct with external memory from pool (zero-malloc at runtime)
    RingBuffer(T* storage, size_t capacity)
        : data_(storage)
        , capacity_(next_power_of_2(capacity))
        , mask_(capacity_ - 1)
    {
        assert(storage != nullptr);
        assert((capacity_ & mask_) == 0 && "capacity must be power of 2");
    }

    // Construct by allocating from MemoryPool
    // Note: caller provides pool-allocated storage
    RingBuffer() : data_(nullptr), capacity_(0), mask_(0) {}

    void init(T* storage, size_t capacity) {
        data_ = storage;
        capacity_ = next_power_of_2(capacity);
        mask_ = capacity_ - 1;
    }

    // --- Producer (single thread) ---

    // Write up to count elements. Returns number actually written.
    size_t write(const T* src, size_t count) {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        const size_t available = capacity_ - (w - r);
        const size_t to_write = std::min(count, available);

        if (to_write == 0) return 0;

        const size_t w_idx = w & mask_;
        const size_t first_chunk = std::min(to_write, capacity_ - w_idx);
        std::memcpy(data_ + w_idx, src, first_chunk * sizeof(T));

        if (to_write > first_chunk) {
            // Wrap around
            std::memcpy(data_, src + first_chunk, (to_write - first_chunk) * sizeof(T));
        }

        write_pos_.store(w + to_write, std::memory_order_release);
        return to_write;
    }

    // --- Consumer (single thread) ---

    // Read up to count elements. Returns number actually read.
    size_t read(T* dst, size_t count) {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        const size_t available = w - r;
        const size_t to_read = std::min(count, available);

        if (to_read == 0) return 0;

        const size_t r_idx = r & mask_;
        const size_t first_chunk = std::min(to_read, capacity_ - r_idx);
        std::memcpy(dst, data_ + r_idx, first_chunk * sizeof(T));

        if (to_read > first_chunk) {
            std::memcpy(dst + first_chunk, data_, (to_read - first_chunk) * sizeof(T));
        }

        read_pos_.store(r + to_read, std::memory_order_release);
        return to_read;
    }

    // Peek at available data without consuming
    size_t peek(T* dst, size_t count) const {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        const size_t available = w - r;
        const size_t to_read = std::min(count, available);

        if (to_read == 0) return 0;

        const size_t r_idx = r & mask_;
        const size_t first_chunk = std::min(to_read, capacity_ - r_idx);
        std::memcpy(dst, data_ + r_idx, first_chunk * sizeof(T));

        if (to_read > first_chunk) {
            std::memcpy(dst + first_chunk, data_, (to_read - first_chunk) * sizeof(T));
        }
        return to_read;
    }

    // --- Queries ---
    size_t available_read() const {
        return write_pos_.load(std::memory_order_acquire) -
               read_pos_.load(std::memory_order_relaxed);
    }

    size_t available_write() const {
        return capacity_ - available_read();
    }

    size_t capacity() const { return capacity_; }

    bool empty() const { return available_read() == 0; }
    bool full()  const { return available_write() == 0; }

    void clear() {
        read_pos_.store(write_pos_.load(std::memory_order_relaxed),
                       std::memory_order_release);
    }

private:
    static size_t next_power_of_2(size_t v) {
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4;
        v |= v >> 8; v |= v >> 16; v |= v >> 32;
        return v + 1;
    }

    T*     data_;
    size_t capacity_;
    size_t mask_;

    // Padded to separate cache lines — prevents false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_{0};
};

} // namespace rastack
