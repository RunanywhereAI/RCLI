#include "rag/index_builder.h"
#include "rag/vector_index.h"
#include "rag/bm25_index.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

namespace rastack {

bool IndexBuilder::write_chunk_store(
    const std::string& path,
    const std::vector<DocumentChunk>& chunks,
    std::vector<ChunkMeta>& metas)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    metas.resize(chunks.size());
    uint32_t offset = 0;

    for (size_t i = 0; i < chunks.size(); i++) {
        const auto& chunk = chunks[i];

        metas[i].chunk_id    = static_cast<uint32_t>(i);
        metas[i].doc_id      = 0; // TODO: multi-doc tracking
        metas[i].page_number = static_cast<uint16_t>(chunk.page_number);
        metas[i].chunk_index = static_cast<uint16_t>(chunk.chunk_index);
        metas[i].text_offset = offset;
        metas[i].text_length = static_cast<uint32_t>(chunk.text.size());

        ofs.write(chunk.text.data(), chunk.text.size());
        offset += static_cast<uint32_t>(chunk.text.size());
    }

    fprintf(stderr, "[IDX] Wrote chunk store: %zu chunks, %u bytes\n",
            chunks.size(), offset);
    return true;
}

bool IndexBuilder::write_chunk_meta(
    const std::string& path,
    const std::vector<ChunkMeta>& metas)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    uint32_t count = static_cast<uint32_t>(metas.size());
    ofs.write(reinterpret_cast<const char*>(&count), 4);
    ofs.write(reinterpret_cast<const char*>(metas.data()),
              metas.size() * sizeof(ChunkMeta));

    return true;
}

bool IndexBuilder::write_manifest(
    const std::string& path,
    size_t num_chunks, int embedding_dim,
    const std::string& docs_path)
{
    std::ofstream ofs(path);
    if (!ofs) return false;

    ofs << "{\n";
    ofs << "  \"version\": 1,\n";
    ofs << "  \"num_chunks\": " << num_chunks << ",\n";
    ofs << "  \"embedding_dim\": " << embedding_dim << ",\n";
    ofs << "  \"docs_path\": \"" << docs_path << "\",\n";
    ofs << "  \"vector_index\": \"vectors.usearch\",\n";
    ofs << "  \"bm25_index\": \"bm25.bin\",\n";
    ofs << "  \"chunk_store\": \"chunks.bin\",\n";
    ofs << "  \"chunk_meta\": \"chunk_meta.bin\"\n";
    ofs << "}\n";

    return true;
}

bool IndexBuilder::build(
    const BuildConfig& config,
    std::function<void(int done, int total)> progress)
{
    auto t_start = std::chrono::steady_clock::now();

    // Create output directory
    fs::create_directories(config.output_path);

    // Step 1: Process documents → chunks
    fprintf(stderr, "\n=== Step 1: Processing documents ===\n");
    DocumentProcessor processor(config.proc_config);
    auto chunks = processor.process_path(config.docs_path);

    if (chunks.empty()) {
        fprintf(stderr, "[IDX] No chunks extracted. Aborting.\n");
        return false;
    }
    fprintf(stderr, "[IDX] Total chunks: %zu\n", chunks.size());

    // Step 2: Initialize embedding engine
    fprintf(stderr, "\n=== Step 2: Initializing embedding engine ===\n");
    EmbeddingEngine embedder;
    if (!embedder.init(config.embed_config)) {
        fprintf(stderr, "[IDX] Failed to initialize embedding engine\n");
        return false;
    }

    // Step 3: Embed all chunks
    fprintf(stderr, "\n=== Step 3: Embedding %zu chunks ===\n", chunks.size());
    std::vector<std::string> texts;
    texts.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        texts.push_back(chunk.text);
    }

    auto embeddings = embedder.embed_batch(texts, config.batch_size);
    if (embeddings.size() != chunks.size()) {
        fprintf(stderr, "[IDX] Embedding count mismatch: %zu vs %zu\n",
                embeddings.size(), chunks.size());
        return false;
    }

    // Step 4: Build vector index
    fprintf(stderr, "\n=== Step 4: Building vector index ===\n");
    VectorIndex vec_index;
    if (!vec_index.create(embedder.embedding_dim(), chunks.size())) {
        fprintf(stderr, "[IDX] Failed to create vector index\n");
        return false;
    }

    for (size_t i = 0; i < embeddings.size(); i++) {
        vec_index.add(static_cast<uint32_t>(i), embeddings[i].data());
        if (progress && (i + 1) % 100 == 0) {
            progress(static_cast<int>(i + 1), static_cast<int>(chunks.size()));
        }
    }

    if (!vec_index.save(config.output_path + "/vectors.usearch")) {
        fprintf(stderr, "[IDX] Failed to save vector index\n");
        return false;
    }

    // Step 5: Build BM25 index
    fprintf(stderr, "\n=== Step 5: Building BM25 index ===\n");
    BM25Index bm25;
    for (size_t i = 0; i < chunks.size(); i++) {
        bm25.add_document(static_cast<uint32_t>(i), chunks[i].text);
    }
    bm25.build();

    if (!bm25.save(config.output_path + "/bm25.bin")) {
        fprintf(stderr, "[IDX] Failed to save BM25 index\n");
        return false;
    }

    // Step 6: Write chunk store
    fprintf(stderr, "\n=== Step 6: Writing chunk store ===\n");
    std::vector<ChunkMeta> metas;
    if (!write_chunk_store(config.output_path + "/chunks.bin", chunks, metas)) {
        fprintf(stderr, "[IDX] Failed to write chunk store\n");
        return false;
    }

    // Step 7: Write chunk metadata
    if (!write_chunk_meta(config.output_path + "/chunk_meta.bin", metas)) {
        fprintf(stderr, "[IDX] Failed to write chunk metadata\n");
        return false;
    }

    // Step 8: Write manifest
    if (!write_manifest(config.output_path + "/manifest.json",
                        chunks.size(), embedder.embedding_dim(),
                        config.docs_path)) {
        fprintf(stderr, "[IDX] Failed to write manifest\n");
        return false;
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();

    fprintf(stderr, "\n=== Index built successfully ===\n");
    fprintf(stderr, "  Chunks:    %zu\n", chunks.size());
    fprintf(stderr, "  Dim:       %d\n", embedder.embedding_dim());
    fprintf(stderr, "  Time:      %.1fs\n", elapsed_s);
    fprintf(stderr, "  Output:    %s\n", config.output_path.c_str());
    fprintf(stderr, "================================\n\n");

    return true;
}

} // namespace rastack
