#pragma once

#include "core/types.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

namespace rastack {

// Frequency-weighted LRU cache for embedding vectors.
// Eviction scoring: score = sqrt(frequency) * (1.0 / (1.0 + age_seconds))
// Pre-allocates all embedding storage at construction for zero runtime alloc.

class EmbeddingCache {
public:
    EmbeddingCache(size_t max_bytes, int embedding_dim)
        : embedding_dim_(embedding_dim)
    {
        size_t bytes_per_entry = embedding_dim * sizeof(float);
        max_entries_ = bytes_per_entry > 0 ? max_bytes / bytes_per_entry : 0;

        if (max_entries_ > 0) {
            pool_ = std::make_unique<float[]>(max_entries_ * embedding_dim);
        }

        entries_.reserve(max_entries_);
        id_to_idx_.reserve(max_entries_);
    }

    // Returns pointer to cached embedding, or nullptr if not found.
    // Updates frequency and recency on hit.
    const float* get(uint32_t chunk_id) {
        auto it = id_to_idx_.find(chunk_id);
        if (it == id_to_idx_.end()) {
            misses_++;
            return nullptr;
        }

        hits_++;
        auto& entry = entries_[it->second];
        entry.frequency++;
        entry.last_access = now_us();
        return entry.embedding;
    }

    // Insert embedding into cache. Evicts lowest-scoring entry if full.
    void put(uint32_t chunk_id, const float* embedding) {
        // Already cached?
        auto it = id_to_idx_.find(chunk_id);
        if (it != id_to_idx_.end()) {
            // Update existing
            auto& entry = entries_[it->second];
            std::memcpy(entry.embedding, embedding, embedding_dim_ * sizeof(float));
            entry.frequency++;
            entry.last_access = now_us();
            return;
        }

        // Need eviction?
        if (entries_.size() >= max_entries_) {
            evict_one();
        }

        if (max_entries_ == 0) return;

        // Insert new entry
        size_t idx = entries_.size();
        float* slot = pool_.get() + idx * embedding_dim_;
        std::memcpy(slot, embedding, embedding_dim_ * sizeof(float));

        CacheEntry entry;
        entry.chunk_id    = chunk_id;
        entry.embedding   = slot;
        entry.frequency   = 1;
        entry.last_access = now_us();

        entries_.push_back(entry);
        id_to_idx_[chunk_id] = idx;
    }

    size_t size() const { return entries_.size(); }
    size_t max_entries() const { return max_entries_; }
    size_t capacity_bytes() const { return max_entries_ * embedding_dim_ * sizeof(float); }

    float hit_rate() const {
        uint64_t total = hits_ + misses_;
        return total > 0 ? static_cast<float>(hits_) / total : 0.0f;
    }

    uint64_t eviction_count() const { return evictions_; }

private:
    struct CacheEntry {
        uint32_t chunk_id;
        float*   embedding;    // Points into pool_
        uint32_t frequency;
        int64_t  last_access;  // Microseconds

        float score(int64_t now) const {
            double age_sec = (now - last_access) / 1e6;
            return static_cast<float>(std::sqrt(frequency) / (1.0 + age_sec));
        }
    };

    int    embedding_dim_;
    size_t max_entries_;

    std::unordered_map<uint32_t, size_t> id_to_idx_;
    std::vector<CacheEntry> entries_;
    std::unique_ptr<float[]> pool_;

    uint64_t hits_      = 0;
    uint64_t misses_    = 0;
    uint64_t evictions_ = 0;

    void evict_one() {
        if (entries_.empty()) return;

        int64_t now = now_us();

        // Find entry with lowest score
        size_t victim = 0;
        float min_score = entries_[0].score(now);

        for (size_t i = 1; i < entries_.size(); i++) {
            float s = entries_[i].score(now);
            if (s < min_score) {
                min_score = s;
                victim = i;
            }
        }

        // Remove victim from map
        id_to_idx_.erase(entries_[victim].chunk_id);

        // Swap victim with last entry (to avoid shifting)
        if (victim != entries_.size() - 1) {
            size_t last_idx = entries_.size() - 1;
            auto& last_entry = entries_[last_idx];

            // Copy last entry's embedding to victim's slot
            std::memcpy(entries_[victim].embedding, last_entry.embedding,
                        embedding_dim_ * sizeof(float));

            // Update the moved entry
            entries_[victim].chunk_id    = last_entry.chunk_id;
            entries_[victim].frequency   = last_entry.frequency;
            entries_[victim].last_access = last_entry.last_access;
            // embedding pointer stays (it points to victim's pool slot)

            // Update map for moved entry
            id_to_idx_[entries_[victim].chunk_id] = victim;
        }

        entries_.pop_back();
        evictions_++;
    }
};

} // namespace rastack
