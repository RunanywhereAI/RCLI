#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace rastack {

class BM25Index {
public:
    struct SearchResult {
        uint32_t chunk_id;
        float    score;
    };

    BM25Index();
    ~BM25Index();

    // --- Build-time ---
    void add_document(uint32_t chunk_id, const std::string& text);
    void build();  // Compute IDF values, finalize
    bool save(const std::string& path);

    // --- Query-time ---
    bool load(const std::string& path);
    std::vector<SearchResult> search(const std::string& query, int top_k);

    size_t num_documents() const { return num_docs_; }
    size_t vocab_size() const { return idf_.size(); }

private:
    // BM25 parameters
    static constexpr float k1_ = 1.2f;
    static constexpr float b_  = 0.75f;

    // Tokenizer: lowercase, strip punct, remove stopwords
    std::vector<std::string> tokenize(const std::string& text) const;

    // Build pre-allocated scratch buffers after loading/building index
    void build_scratch_buffers();

    // Posting list entry
    struct Posting {
        uint32_t chunk_id;
        uint16_t freq;
    };

    // Inverted index: term → list of postings
    std::unordered_map<std::string, std::vector<Posting>> index_;

    // Document metadata
    std::vector<uint32_t> chunk_ids_;        // chunk_id for each doc index
    std::vector<uint16_t> doc_lengths_;      // token count per doc
    float avg_doc_length_ = 0.0f;
    size_t num_docs_ = 0;

    // IDF cache: term → IDF value
    std::unordered_map<std::string, float> idf_;

    // Pre-allocated scratch buffers (avoid per-query heap allocs)
    std::vector<size_t> chunk_id_to_doc_idx_;  // flat lookup: chunk_id → doc index
    std::vector<float>  scores_buf_;            // per-doc score accumulator
    std::vector<uint32_t> touched_docs_;        // which doc indices were written (for fast reset)
    uint32_t max_chunk_id_ = 0;

    // Stopwords
    std::unordered_set<std::string> stopwords_;

    void init_stopwords();
};

} // namespace rastack
