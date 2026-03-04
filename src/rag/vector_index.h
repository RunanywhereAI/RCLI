#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace rastack {

class VectorIndex {
public:
    struct SearchResult {
        uint32_t chunk_id;
        float    distance;  // Cosine distance (1 - similarity)
    };

    VectorIndex();
    ~VectorIndex();

    // --- Build-time ---
    bool create(int dimensions, size_t capacity);
    bool add(uint32_t chunk_id, const float* embedding);
    bool save(const std::string& path);

    // --- Query-time ---
    bool load(const std::string& path);  // mmap-based load
    std::vector<SearchResult> search(const float* query, int top_k) const;

    size_t size() const;
    int dimensions() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace rastack
