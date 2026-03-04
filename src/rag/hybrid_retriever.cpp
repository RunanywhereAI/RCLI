#include "rag/hybrid_retriever.h"
#include "core/types.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace rastack {

HybridRetriever::HybridRetriever() = default;

HybridRetriever::~HybridRetriever() {
    if (chunk_store_ && chunk_store_size_ > 0) {
        munmap(chunk_store_, chunk_store_size_);
    }
    if (chunk_store_fd_ >= 0) {
        close(chunk_store_fd_);
    }
}

bool HybridRetriever::load_chunk_store(const std::string& path) {
    chunk_store_fd_ = open(path.c_str(), O_RDONLY);
    if (chunk_store_fd_ < 0) {
        fprintf(stderr, "[RET] Failed to open chunk store: %s\n", path.c_str());
        return false;
    }

    struct stat st;
    fstat(chunk_store_fd_, &st);
    chunk_store_size_ = st.st_size;

    chunk_store_ = static_cast<char*>(
        mmap(nullptr, chunk_store_size_, PROT_READ, MAP_PRIVATE, chunk_store_fd_, 0));

    if (chunk_store_ == MAP_FAILED) {
        fprintf(stderr, "[RET] Failed to mmap chunk store\n");
        chunk_store_ = nullptr;
        close(chunk_store_fd_);
        chunk_store_fd_ = -1;
        return false;
    }

    // Advise sequential access for prefetching
    madvise(chunk_store_, chunk_store_size_, MADV_SEQUENTIAL);

    fprintf(stderr, "[RET] Loaded chunk store: %zu bytes\n", chunk_store_size_);
    return true;
}

bool HybridRetriever::load_chunk_meta(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        fprintf(stderr, "[RET] Failed to open chunk meta: %s\n", path.c_str());
        return false;
    }

    // Read count
    uint32_t count;
    ifs.read(reinterpret_cast<char*>(&count), 4);

    chunk_metas_.resize(count);
    ifs.read(reinterpret_cast<char*>(chunk_metas_.data()),
             count * sizeof(ChunkMeta));

    fprintf(stderr, "[RET] Loaded %u chunk metadata entries\n", count);
    return true;
}

std::string_view HybridRetriever::get_chunk_text(uint32_t chunk_id) const {
    if (!chunk_store_ || chunk_id >= chunk_metas_.size()) return {};

    const auto& meta = chunk_metas_[chunk_id];
    if (meta.text_offset + meta.text_length > chunk_store_size_) return {};

    return std::string_view(chunk_store_ + meta.text_offset, meta.text_length);
}

bool HybridRetriever::init(const std::string& index_path, const RAGConfig& config) {
    rrf_k_ = config.rrf_k;
    vector_candidates_ = config.vector_candidates;
    bm25_candidates_ = config.bm25_candidates;

    // Load vector index
    std::string vec_path = index_path + "/vectors.usearch";
    if (!vector_index_.load(vec_path)) return false;

    // Load BM25 index
    std::string bm25_path = index_path + "/bm25.bin";
    if (!bm25_index_.load(bm25_path)) return false;

    // Load chunk store
    std::string chunks_path = index_path + "/chunks.bin";
    if (!load_chunk_store(chunks_path)) return false;

    // Load chunk metadata
    std::string meta_path = index_path + "/chunk_meta.bin";
    if (!load_chunk_meta(meta_path)) return false;

    // Pre-allocate RRF fusion buffers
    size_t n = chunk_metas_.size();
    rrf_scores_buf_.resize(n, 0.0f);
    vec_scores_buf_.resize(n, 0.0f);
    bm25_scores_buf_.resize(n, 0.0f);
    touched_.reserve(n);
    sort_buf_.reserve(n);

    fprintf(stderr, "[RET] Initialized hybrid retriever: %zu vectors, %zu BM25 docs, %zu chunks\n",
            vector_index_.size(), bm25_index_.num_documents(), chunk_metas_.size());
    fprintf(stderr, "[RET] RRF fusion buffers: %zu entries pre-allocated\n", n);
    return true;
}

// --- Split search methods for parallel BM25+embedding ---

std::vector<BM25Index::SearchResult> HybridRetriever::search_bm25(
    const std::string& query_text, int candidates)
{
    int64_t t0 = now_us();
    auto results = bm25_index_.search(query_text, candidates);
    last_bm25_us_ = now_us() - t0;
    return results;
}

std::vector<VectorIndex::SearchResult> HybridRetriever::search_vector(
    const float* embedding, int candidates)
{
    int64_t t0 = now_us();
    auto results = vector_index_.search(embedding, candidates);
    last_vector_us_ = now_us() - t0;
    return results;
}

std::vector<RetrievalResult> HybridRetriever::fuse(
    const std::vector<VectorIndex::SearchResult>& vec_results,
    const std::vector<BM25Index::SearchResult>& bm25_results,
    int top_k)
{
    int64_t t0 = now_us();

    // Reset touched entries (O(touched) instead of O(n))
    for (auto id : touched_) {
        rrf_scores_buf_[id] = 0.0f;
        vec_scores_buf_[id] = 0.0f;
        bm25_scores_buf_[id] = 0.0f;
    }
    touched_.clear();

    // Vector results
    for (size_t i = 0; i < vec_results.size(); i++) {
        uint32_t id = vec_results[i].chunk_id;
        if (id >= rrf_scores_buf_.size()) continue;
        if (rrf_scores_buf_[id] == 0.0f && bm25_scores_buf_[id] == 0.0f) {
            touched_.push_back(id);
        }
        rrf_scores_buf_[id] += 1.0f / (rrf_k_ + static_cast<float>(i + 1));
        vec_scores_buf_[id] = 1.0f - vec_results[i].distance;
    }

    // BM25 results
    for (size_t i = 0; i < bm25_results.size(); i++) {
        uint32_t id = bm25_results[i].chunk_id;
        if (id >= rrf_scores_buf_.size()) continue;
        if (rrf_scores_buf_[id] == 0.0f && vec_scores_buf_[id] == 0.0f) {
            touched_.push_back(id);
        }
        rrf_scores_buf_[id] += 1.0f / (rrf_k_ + static_cast<float>(i + 1));
        bm25_scores_buf_[id] = bm25_results[i].score;
    }

    // Sort touched entries by fused score
    sort_buf_.clear();
    for (auto id : touched_) {
        sort_buf_.push_back({rrf_scores_buf_[id], id});
    }
    std::sort(sort_buf_.begin(), sort_buf_.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Build final results
    std::vector<RetrievalResult> results;
    int count = std::min(top_k, static_cast<int>(sort_buf_.size()));

    for (int i = 0; i < count; i++) {
        uint32_t id = sort_buf_[i].second;

        RetrievalResult r;
        r.chunk_id     = id;
        r.score        = sort_buf_[i].first;
        r.vector_score = vec_scores_buf_[id];
        r.bm25_score   = bm25_scores_buf_[id];
        r.text         = get_chunk_text(id);

        if (id < chunk_metas_.size()) {
            r.meta = chunk_metas_[id];
        }

        results.push_back(std::move(r));
    }

    last_fusion_us_ = now_us() - t0;
    return results;
}

std::vector<RetrievalResult> HybridRetriever::retrieve(
    const std::string& query_text,
    const std::vector<float>& query_embedding,
    int top_k)
{
    auto vec_results = search_vector(query_embedding.data(), vector_candidates_);
    auto bm25_results = search_bm25(query_text, bm25_candidates_);
    return fuse(vec_results, bm25_results, top_k);
}

} // namespace rastack
