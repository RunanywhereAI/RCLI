#pragma once

#include "core/types.h"
#include "engines/embedding_engine.h"
#include "rag/document_processor.h"
#include <string>
#include <functional>

namespace rastack {

class IndexBuilder {
public:
    struct BuildConfig {
        std::string     docs_path;       // Directory of PDFs
        std::string     output_path;     // Index output directory
        EmbeddingConfig embed_config;
        ProcessorConfig proc_config;
        int             batch_size = 32; // Embedding batch size
    };

    // Build the full index: PDFs → chunks → embeddings → vector + BM25 indexes
    // progress callback: (done, total)
    bool build(const BuildConfig& config,
               std::function<void(int done, int total)> progress = nullptr);

private:
    bool write_chunk_store(const std::string& path,
                           const std::vector<DocumentChunk>& chunks,
                           std::vector<ChunkMeta>& metas);

    bool write_chunk_meta(const std::string& path,
                          const std::vector<ChunkMeta>& metas);

    bool write_manifest(const std::string& path,
                        size_t num_chunks, int embedding_dim,
                        const std::string& docs_path);
};

} // namespace rastack
