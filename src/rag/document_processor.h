#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace rastack {

struct DocumentChunk {
    std::string text;
    uint32_t    page_number;
    uint32_t    chunk_index;    // Position within document
    std::string section_title;  // Extracted heading (if any)
    std::string source_file;    // Original filename
};

struct ProcessorConfig {
    int  min_chunk_tokens = 64;      // Skip tiny chunks
    int  max_chunk_tokens = 512;     // Max tokens per chunk
    int  overlap_tokens   = 50;      // Overlap between consecutive chunks
    bool preserve_paragraphs = true; // Split on paragraph boundaries
};

class DocumentProcessor {
public:
    explicit DocumentProcessor(const ProcessorConfig& config = {});
    ~DocumentProcessor();

    // Process a single PDF → chunks
    std::vector<DocumentChunk> process_pdf(const std::string& pdf_path);

    // Process a single .docx → chunks
    std::vector<DocumentChunk> process_docx(const std::string& docx_path);

    // Process a single file (auto-detects type by extension)
    std::vector<DocumentChunk> process_file(const std::string& file_path);

    // Process all supported files in a directory (recursive)
    std::vector<DocumentChunk> process_directory(const std::string& dir_path);

    // Process a path that could be a file or directory
    std::vector<DocumentChunk> process_path(const std::string& path);

    // Process raw text (for non-PDF sources)
    std::vector<DocumentChunk> process_text(
        const std::string& text, const std::string& source_name);

private:
    ProcessorConfig config_;

    // Extract text from PDF using poppler (pdftotext)
    std::string extract_pdf_text(const std::string& path);

    // Extract text from .docx (Office Open XML) by unzipping word/document.xml
    std::string extract_docx_text(const std::string& path);

    // Semantic chunking: split on paragraph/section boundaries
    std::vector<DocumentChunk> semantic_chunk(
        const std::string& text, const std::string& source);

    // Detect section boundaries
    bool is_section_boundary(const std::string& line) const;

    // Rough token count estimation (~4 chars per token)
    int estimate_tokens(const std::string& text) const;
};

} // namespace rastack
