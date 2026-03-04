#pragma once

#include "core/types.h"
#include "rag/vector_index.h"
#include "rag/bm25_index.h"
#include <string>
#include <vector>

namespace rastack {

class HybridRetriever {
public:
    HybridRetriever();
    ~HybridRetriever();

    // Load index from directory
    bool init(const std::string& index_path, const RAGConfig& config);

    // Retrieve top-K chunks using hybrid search (vector + BM25 + RRF)
    std::vector<RetrievalResult> retrieve(
        const std::string& query_text,
        const std::vector<float>& query_embedding,
        int top_k);

    // Split search methods for parallel BM25+embedding
    std::vector<BM25Index::SearchResult> search_bm25(
        const std::string& query_text, int candidates);
    std::vector<VectorIndex::SearchResult> search_vector(
        const float* embedding, int candidates);
    std::vector<RetrievalResult> fuse(
        const std::vector<VectorIndex::SearchResult>& vec_results,
        const std::vector<BM25Index::SearchResult>& bm25_results,
        int top_k);

    // Timing stats from last retrieval
    int64_t last_vector_search_us() const { return last_vector_us_; }
    int64_t last_bm25_search_us() const { return last_bm25_us_; }
    int64_t last_fusion_us() const { return last_fusion_us_; }

    size_t num_chunks() const { return chunk_metas_.size(); }

private:
    VectorIndex vector_index_;
    BM25Index   bm25_index_;

    float rrf_k_;
    int   vector_candidates_;
    int   bm25_candidates_;

    // Chunk store: mmap'd text storage
    char*  chunk_store_ = nullptr;
    size_t chunk_store_size_ = 0;
    int    chunk_store_fd_ = -1;

    // Chunk metadata
    std::vector<ChunkMeta> chunk_metas_;

    // Timing
    int64_t last_vector_us_ = 0;
    int64_t last_bm25_us_   = 0;
    int64_t last_fusion_us_ = 0;

    // Pre-allocated RRF fusion buffers (avoid per-query heap allocs)
    std::vector<float>    rrf_scores_buf_;
    std::vector<float>    vec_scores_buf_;
    std::vector<float>    bm25_scores_buf_;
    std::vector<uint32_t> touched_;
    std::vector<std::pair<float, uint32_t>> sort_buf_;

    // Load chunk store via mmap
    bool load_chunk_store(const std::string& path);
    bool load_chunk_meta(const std::string& path);

    // Look up chunk text from mmap'd store (zero-copy into mmap)
    std::string_view get_chunk_text(uint32_t chunk_id) const;
};

} // namespace rastack
