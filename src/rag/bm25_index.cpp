#include "rag/bm25_index.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <queue>

namespace rastack {

BM25Index::BM25Index() {
    init_stopwords();
}

BM25Index::~BM25Index() = default;

void BM25Index::init_stopwords() {
    stopwords_ = {
        "a", "an", "the", "and", "or", "but", "in", "on", "at", "to",
        "for", "of", "with", "by", "from", "is", "it", "its", "this",
        "that", "was", "are", "were", "been", "be", "have", "has", "had",
        "do", "does", "did", "will", "would", "could", "should", "may",
        "might", "can", "shall", "not", "no", "nor", "as", "if", "then",
        "than", "too", "very", "so", "just", "about", "above", "after",
        "again", "all", "also", "am", "any", "because", "before", "being",
        "between", "both", "during", "each", "few", "further", "get",
        "got", "he", "her", "here", "hers", "herself", "him", "himself",
        "his", "how", "i", "into", "me", "more", "most", "my", "myself",
        "now", "only", "other", "our", "ours", "ourselves", "out", "over",
        "own", "s", "same", "she", "some", "such", "t", "their", "theirs",
        "them", "themselves", "there", "these", "they", "those", "through",
        "under", "until", "up", "we", "what", "when", "where", "which",
        "while", "who", "whom", "why", "you", "your", "yours", "yourself"
    };
}

std::vector<std::string> BM25Index::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;

    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Replace non-alphanumeric with space
    for (auto& c : lower) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = ' ';
        }
    }

    std::istringstream iss(lower);
    std::string word;
    while (iss >> word) {
        if (word.length() < 2) continue;        // Skip single chars
        if (stopwords_.count(word)) continue;    // Skip stopwords
        tokens.push_back(word);
    }

    return tokens;
}

void BM25Index::build_scratch_buffers() {
    // Find max chunk_id for flat lookup table
    max_chunk_id_ = 0;
    for (auto id : chunk_ids_) {
        if (id > max_chunk_id_) max_chunk_id_ = id;
    }

    // Build flat chunk_id → doc_index lookup
    chunk_id_to_doc_idx_.assign(max_chunk_id_ + 1, SIZE_MAX);
    for (size_t i = 0; i < chunk_ids_.size(); i++) {
        chunk_id_to_doc_idx_[chunk_ids_[i]] = i;
    }

    // Pre-allocate score accumulator and touched list
    scores_buf_.resize(num_docs_, 0.0f);
    touched_docs_.reserve(num_docs_);

    fprintf(stderr, "[BM25] Scratch buffers: %zu-entry lookup, %zu-entry scores\n",
            chunk_id_to_doc_idx_.size(), scores_buf_.size());
}

void BM25Index::add_document(uint32_t chunk_id, const std::string& text) {
    auto tokens = tokenize(text);

    // Count term frequencies
    std::unordered_map<std::string, uint16_t> tf;
    for (const auto& tok : tokens) {
        tf[tok]++;
    }

    // Add to inverted index
    for (const auto& [term, freq] : tf) {
        index_[term].push_back({chunk_id, freq});
    }

    chunk_ids_.push_back(chunk_id);
    doc_lengths_.push_back(static_cast<uint16_t>(tokens.size()));
    num_docs_++;
}

void BM25Index::build() {
    // Compute average document length
    uint64_t total_len = 0;
    for (auto len : doc_lengths_) {
        total_len += len;
    }
    avg_doc_length_ = num_docs_ > 0 ? static_cast<float>(total_len) / num_docs_ : 0.0f;

    // Compute IDF for each term
    // IDF = log((N - df + 0.5) / (df + 0.5) + 1.0)
    float N = static_cast<float>(num_docs_);
    for (const auto& [term, postings] : index_) {
        float df = static_cast<float>(postings.size());
        idf_[term] = std::log((N - df + 0.5f) / (df + 0.5f) + 1.0f);
    }

    build_scratch_buffers();

    fprintf(stderr, "[BM25] Built index: %zu docs, %zu terms, avg_len=%.1f\n",
            num_docs_, idf_.size(), avg_doc_length_);
}

std::vector<BM25Index::SearchResult> BM25Index::search(
    const std::string& query, int top_k)
{
    auto query_tokens = tokenize(query);
    if (query_tokens.empty()) return {};

    // Reset touched scores (O(touched) instead of O(num_docs))
    for (auto doc_idx : touched_docs_) {
        scores_buf_[doc_idx] = 0.0f;
    }
    touched_docs_.clear();

    // Accumulate BM25 scores using flat arrays
    for (const auto& term : query_tokens) {
        auto idf_it = idf_.find(term);
        if (idf_it == idf_.end()) continue;
        float idf_val = idf_it->second;

        auto idx_it = index_.find(term);
        if (idx_it == index_.end()) continue;

        for (const auto& posting : idx_it->second) {
            if (posting.chunk_id > max_chunk_id_) continue;
            size_t doc_idx = chunk_id_to_doc_idx_[posting.chunk_id];
            if (doc_idx == SIZE_MAX) continue;

            float dl = static_cast<float>(doc_lengths_[doc_idx]);
            float tf = static_cast<float>(posting.freq);

            // BM25 formula: IDF * (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * dl / avgdl))
            float numerator = tf * (k1_ + 1.0f);
            float denominator = tf + k1_ * (1.0f - b_ + b_ * dl / avg_doc_length_);
            float score = idf_val * numerator / denominator;

            if (scores_buf_[doc_idx] == 0.0f) {
                touched_docs_.push_back(static_cast<uint32_t>(doc_idx));
            }
            scores_buf_[doc_idx] += score;
        }
    }

    // Top-K using min-heap
    using ScorePair = std::pair<float, uint32_t>;
    std::priority_queue<ScorePair, std::vector<ScorePair>, std::greater<ScorePair>> min_heap;

    for (auto doc_idx : touched_docs_) {
        float score = scores_buf_[doc_idx];
        uint32_t chunk_id = chunk_ids_[doc_idx];
        if (static_cast<int>(min_heap.size()) < top_k) {
            min_heap.push({score, chunk_id});
        } else if (score > min_heap.top().first) {
            min_heap.pop();
            min_heap.push({score, chunk_id});
        }
    }

    // Extract results sorted by score descending
    std::vector<SearchResult> results;
    while (!min_heap.empty()) {
        auto [score, chunk_id] = min_heap.top();
        min_heap.pop();
        results.push_back({chunk_id, score});
    }
    std::reverse(results.begin(), results.end());

    return results;
}

bool BM25Index::save(const std::string& path) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        fprintf(stderr, "[BM25] Failed to open %s for writing\n", path.c_str());
        return false;
    }

    // Magic + version
    uint32_t magic = 0x424D3235; // "BM25"
    uint32_t version = 1;
    ofs.write(reinterpret_cast<const char*>(&magic), 4);
    ofs.write(reinterpret_cast<const char*>(&version), 4);

    // Metadata
    uint32_t n_docs = static_cast<uint32_t>(num_docs_);
    uint32_t n_terms = static_cast<uint32_t>(idf_.size());
    ofs.write(reinterpret_cast<const char*>(&n_docs), 4);
    ofs.write(reinterpret_cast<const char*>(&n_terms), 4);
    ofs.write(reinterpret_cast<const char*>(&avg_doc_length_), 4);

    // Chunk IDs
    ofs.write(reinterpret_cast<const char*>(chunk_ids_.data()),
              chunk_ids_.size() * sizeof(uint32_t));

    // Doc lengths
    ofs.write(reinterpret_cast<const char*>(doc_lengths_.data()),
              doc_lengths_.size() * sizeof(uint16_t));

    // Terms + IDF + posting lists
    for (const auto& [term, idf_val] : idf_) {
        // Term string (length-prefixed)
        uint16_t term_len = static_cast<uint16_t>(term.size());
        ofs.write(reinterpret_cast<const char*>(&term_len), 2);
        ofs.write(term.data(), term_len);

        // IDF value
        ofs.write(reinterpret_cast<const char*>(&idf_val), 4);

        // Posting list
        auto it = index_.find(term);
        uint32_t n_postings = it != index_.end() ? static_cast<uint32_t>(it->second.size()) : 0;
        ofs.write(reinterpret_cast<const char*>(&n_postings), 4);

        if (it != index_.end()) {
            for (const auto& p : it->second) {
                ofs.write(reinterpret_cast<const char*>(&p.chunk_id), 4);
                ofs.write(reinterpret_cast<const char*>(&p.freq), 2);
            }
        }
    }

    fprintf(stderr, "[BM25] Saved index: %u docs, %u terms to %s\n",
            n_docs, n_terms, path.c_str());
    return true;
}

bool BM25Index::load(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        fprintf(stderr, "[BM25] Failed to open %s for reading\n", path.c_str());
        return false;
    }

    // Magic + version
    uint32_t magic, version;
    ifs.read(reinterpret_cast<char*>(&magic), 4);
    ifs.read(reinterpret_cast<char*>(&version), 4);

    if (magic != 0x424D3235 || version != 1) {
        fprintf(stderr, "[BM25] Invalid file format\n");
        return false;
    }

    // Metadata
    uint32_t n_docs, n_terms;
    ifs.read(reinterpret_cast<char*>(&n_docs), 4);
    ifs.read(reinterpret_cast<char*>(&n_terms), 4);
    ifs.read(reinterpret_cast<char*>(&avg_doc_length_), 4);
    num_docs_ = n_docs;

    // Chunk IDs
    chunk_ids_.resize(n_docs);
    ifs.read(reinterpret_cast<char*>(chunk_ids_.data()), n_docs * sizeof(uint32_t));

    // Doc lengths
    doc_lengths_.resize(n_docs);
    ifs.read(reinterpret_cast<char*>(doc_lengths_.data()), n_docs * sizeof(uint16_t));

    // Terms + IDF + posting lists
    index_.clear();
    idf_.clear();

    for (uint32_t t = 0; t < n_terms; t++) {
        // Term
        uint16_t term_len;
        ifs.read(reinterpret_cast<char*>(&term_len), 2);
        std::string term(term_len, '\0');
        ifs.read(term.data(), term_len);

        // IDF
        float idf_val;
        ifs.read(reinterpret_cast<char*>(&idf_val), 4);
        idf_[term] = idf_val;

        // Postings
        uint32_t n_postings;
        ifs.read(reinterpret_cast<char*>(&n_postings), 4);

        std::vector<Posting> postings(n_postings);
        for (uint32_t p = 0; p < n_postings; p++) {
            ifs.read(reinterpret_cast<char*>(&postings[p].chunk_id), 4);
            ifs.read(reinterpret_cast<char*>(&postings[p].freq), 2);
        }
        index_[term] = std::move(postings);
    }

    build_scratch_buffers();

    fprintf(stderr, "[BM25] Loaded index: %u docs, %u terms from %s\n",
            n_docs, n_terms, path.c_str());
    return true;
}

} // namespace rastack
