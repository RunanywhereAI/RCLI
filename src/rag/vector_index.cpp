#include "rag/vector_index.h"
#include <usearch/index_dense.hpp>
#include <cstdio>

namespace rastack {

using namespace unum::usearch;

struct VectorIndex::Impl {
    std::unique_ptr<index_dense_t> index;
    int dims = 0;
    bool loaded = false;
};

VectorIndex::VectorIndex() : impl_(new Impl()) {}

VectorIndex::~VectorIndex() {
    delete impl_;
}

bool VectorIndex::create(int dimensions, size_t capacity) {
    impl_->dims = dimensions;

    index_dense_config_t config;
    config.connectivity = 16;
    config.expansion_add = 128;
    config.expansion_search = 64;

    metric_punned_t metric(dimensions, metric_kind_t::cos_k, scalar_kind_t::f32_k);
    impl_->index = std::make_unique<index_dense_t>(index_dense_t::make(metric, config));
    impl_->index->reserve(capacity);
    impl_->loaded = true;

    fprintf(stderr, "[VIDX] Created index: dims=%d, capacity=%zu\n", dimensions, capacity);
    return true;
}

bool VectorIndex::add(uint32_t chunk_id, const float* embedding) {
    if (!impl_->loaded || !impl_->index) return false;

    auto result = impl_->index->add(chunk_id, embedding);
    if (!result) {
        fprintf(stderr, "[VIDX] Failed to add chunk %u\n", chunk_id);
        return false;
    }
    return true;
}

bool VectorIndex::save(const std::string& path) {
    if (!impl_->loaded || !impl_->index) return false;

    auto result = impl_->index->save(path.c_str());
    if (!result) {
        fprintf(stderr, "[VIDX] Failed to save to %s\n", path.c_str());
        return false;
    }

    fprintf(stderr, "[VIDX] Saved index: %zu vectors to %s\n", impl_->index->size(), path.c_str());
    return true;
}

bool VectorIndex::load(const std::string& path) {
    // Use static factory: constructs fresh index from serialized file
    impl_->index.reset();
    auto state = index_dense_t::make(path.c_str(), /*view=*/false);
    if (!state) {
        fprintf(stderr, "[VIDX] Failed to load from %s\n", path.c_str());
        return false;
    }

    impl_->index = std::make_unique<index_dense_t>(std::move(state.index));
    impl_->dims = static_cast<int>(impl_->index->dimensions());
    impl_->loaded = true;

    fprintf(stderr, "[VIDX] Loaded index: %zu vectors, dims=%d from %s\n",
            impl_->index->size(), impl_->dims, path.c_str());
    return true;
}

std::vector<VectorIndex::SearchResult> VectorIndex::search(
    const float* query, int top_k) const
{
    if (!impl_->loaded || !impl_->index) return {};

    auto results = impl_->index->search(query, static_cast<size_t>(top_k));
    size_t n = results.size();

    // Extract into flat arrays via dump_to for safety
    std::vector<index_dense_t::vector_key_t> keys(n);
    std::vector<index_dense_t::distance_t> distances(n);
    results.dump_to(keys.data(), distances.data());

    std::vector<SearchResult> out;
    out.reserve(n);

    for (size_t i = 0; i < n; i++) {
        SearchResult sr;
        sr.chunk_id = static_cast<uint32_t>(keys[i]);
        sr.distance = distances[i];
        out.push_back(sr);
    }

    return out;
}

size_t VectorIndex::size() const {
    return (impl_->loaded && impl_->index) ? impl_->index->size() : 0;
}

int VectorIndex::dimensions() const {
    return impl_->dims;
}

} // namespace rastack
