#include "rag/document_processor.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <array>
#include <cctype>

namespace fs = std::filesystem;

namespace rastack {

DocumentProcessor::DocumentProcessor(const ProcessorConfig& config)
    : config_(config) {}

DocumentProcessor::~DocumentProcessor() = default;

std::string DocumentProcessor::extract_pdf_text(const std::string& path) {
    // Use pdftotext (poppler-utils) for extraction
    // -layout preserves formatting, -enc UTF-8 for encoding
    std::string cmd = "pdftotext -layout -enc UTF-8 \"" + path + "\" -";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        fprintf(stderr, "[DOC] Failed to run pdftotext on %s\n", path.c_str());
        return "";
    }

    std::string result;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);
    if (status != 0) {
        fprintf(stderr, "[DOC] pdftotext returned error code %d for %s\n", status, path.c_str());
    }

    return result;
}

bool DocumentProcessor::is_section_boundary(const std::string& line) const {
    if (line.empty()) return false;

    // Check if line is all uppercase (heading)
    bool all_upper = true;
    int alpha_count = 0;
    for (char c : line) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            alpha_count++;
            if (std::islower(static_cast<unsigned char>(c))) {
                all_upper = false;
                break;
            }
        }
    }
    if (all_upper && alpha_count >= 3 && line.size() < 100) return true;

    // Numbered sections: "1.", "1.1", "Chapter", "Section"
    if (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0]))) {
        if (line[1] == '.' || line[1] == ')') return true;
        if (line.size() > 3 && std::isdigit(static_cast<unsigned char>(line[1])) &&
            (line[2] == '.' || line[2] == ')')) return true;
    }

    // Common heading keywords
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.find("chapter ") == 0 || lower.find("section ") == 0 ||
        lower.find("appendix ") == 0 || lower.find("abstract") == 0 ||
        lower.find("introduction") == 0 || lower.find("conclusion") == 0 ||
        lower.find("references") == 0 || lower.find("bibliography") == 0) {
        return true;
    }

    return false;
}

int DocumentProcessor::estimate_tokens(const std::string& text) const {
    // Rough estimation: ~4 characters per token for English text
    return static_cast<int>(text.size()) / 4;
}

std::vector<DocumentChunk> DocumentProcessor::semantic_chunk(
    const std::string& text, const std::string& source)
{
    std::vector<DocumentChunk> chunks;

    // Split into paragraphs (double newline or form feed)
    std::vector<std::string> paragraphs;
    std::vector<uint32_t> para_pages;  // page number for each paragraph

    uint32_t current_page = 1;
    std::istringstream stream(text);
    std::string line;
    std::string current_para;

    while (std::getline(stream, line)) {
        // Form feed = new page (pdftotext convention)
        if (!line.empty() && line[0] == '\f') {
            current_page++;
            line = line.substr(1);
        }

        // Trim trailing whitespace
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }

        if (line.empty()) {
            // End of paragraph
            if (!current_para.empty()) {
                paragraphs.push_back(current_para);
                para_pages.push_back(current_page);
                current_para.clear();
            }
        } else {
            if (!current_para.empty()) current_para += ' ';
            current_para += line;
        }
    }
    // Don't forget last paragraph
    if (!current_para.empty()) {
        paragraphs.push_back(current_para);
        para_pages.push_back(current_page);
    }

    // Build chunks from paragraphs respecting token limits
    std::string chunk_text;
    std::string section_title;
    uint32_t chunk_page = 1;
    uint32_t chunk_idx = 0;

    for (size_t i = 0; i < paragraphs.size(); i++) {
        const auto& para = paragraphs[i];
        int para_tokens = estimate_tokens(para);

        // Detect section boundary → start new chunk
        if (is_section_boundary(para)) {
            // Flush current chunk
            if (!chunk_text.empty() && estimate_tokens(chunk_text) >= config_.min_chunk_tokens) {
                DocumentChunk chunk;
                chunk.text = chunk_text;
                chunk.page_number = chunk_page;
                chunk.chunk_index = chunk_idx++;
                chunk.section_title = section_title;
                chunk.source_file = source;
                chunks.push_back(std::move(chunk));
            }
            chunk_text.clear();
            section_title = para;
            chunk_page = para_pages[i];
            continue;
        }

        // Would adding this paragraph exceed max?
        int current_tokens = estimate_tokens(chunk_text);
        if (current_tokens + para_tokens > config_.max_chunk_tokens && !chunk_text.empty()) {
            // Flush current chunk
            if (current_tokens >= config_.min_chunk_tokens) {
                DocumentChunk chunk;
                chunk.text = chunk_text;
                chunk.page_number = chunk_page;
                chunk.chunk_index = chunk_idx++;
                chunk.section_title = section_title;
                chunk.source_file = source;
                chunks.push_back(std::move(chunk));
            }

            // Start new chunk with overlap from tail of previous
            if (config_.overlap_tokens > 0 && !chunk_text.empty()) {
                // Take last overlap_tokens worth of text
                int overlap_chars = config_.overlap_tokens * 4;
                if (static_cast<int>(chunk_text.size()) > overlap_chars) {
                    chunk_text = chunk_text.substr(chunk_text.size() - overlap_chars);
                }
                // else keep all of chunk_text as overlap
            } else {
                chunk_text.clear();
            }
            chunk_page = para_pages[i];
        }

        // Append paragraph
        if (!chunk_text.empty()) chunk_text += ' ';
        chunk_text += para;

        if (chunk_text.size() == para.size()) {
            chunk_page = para_pages[i];
        }
    }

    // Flush final chunk
    if (!chunk_text.empty() && estimate_tokens(chunk_text) >= config_.min_chunk_tokens) {
        DocumentChunk chunk;
        chunk.text = chunk_text;
        chunk.page_number = chunk_page;
        chunk.chunk_index = chunk_idx++;
        chunk.section_title = section_title;
        chunk.source_file = source;
        chunks.push_back(std::move(chunk));
    }

    return chunks;
}

std::vector<DocumentChunk> DocumentProcessor::process_pdf(const std::string& pdf_path) {
    fprintf(stderr, "[DOC] Processing PDF: %s\n", pdf_path.c_str());

    std::string text = extract_pdf_text(pdf_path);
    if (text.empty()) {
        fprintf(stderr, "[DOC] No text extracted from %s\n", pdf_path.c_str());
        return {};
    }

    std::string filename = fs::path(pdf_path).filename().string();
    auto chunks = semantic_chunk(text, filename);

    fprintf(stderr, "[DOC] Extracted %zu chunks from %s\n", chunks.size(), filename.c_str());
    return chunks;
}

std::string DocumentProcessor::extract_docx_text(const std::string& path) {
    // .docx is a ZIP containing word/document.xml
    // Use unzip to extract, then strip XML tags
    std::string cmd = "unzip -p \"" + path + "\" word/document.xml 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        fprintf(stderr, "[DOC] Failed to extract docx: %s\n", path.c_str());
        return "";
    }
    std::string xml;
    std::array<char, 4096> buf;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr)
        xml += buf.data();
    pclose(pipe);

    if (xml.empty()) return "";

    // Strip XML tags and decode common entities
    std::string text;
    text.reserve(xml.size() / 2);
    bool in_tag = false;
    bool prev_was_break = false;
    for (size_t i = 0; i < xml.size(); ++i) {
        if (xml[i] == '<') {
            // <w:p — paragraph break, <w:br — line break
            if (i + 4 < xml.size() &&
                ((xml[i+1] == 'w' && xml[i+2] == ':' && xml[i+3] == 'p' &&
                  (xml[i+4] == ' ' || xml[i+4] == '>' || xml[i+4] == '/')) ||
                 (xml[i+1] == 'w' && xml[i+2] == ':' && xml[i+3] == 'b' && xml[i+4] == 'r'))) {
                if (!text.empty() && text.back() != '\n') {
                    text += '\n';
                    prev_was_break = true;
                }
            }
            in_tag = true;
            continue;
        }
        if (xml[i] == '>') { in_tag = false; continue; }
        if (in_tag) continue;

        // Decode XML entities
        if (xml[i] == '&') {
            if (xml.compare(i, 4, "&lt;") == 0) { text += '<'; i += 3; continue; }
            if (xml.compare(i, 4, "&gt;") == 0) { text += '>'; i += 3; continue; }
            if (xml.compare(i, 5, "&amp;") == 0) { text += '&'; i += 4; continue; }
            if (xml.compare(i, 6, "&apos;") == 0) { text += '\''; i += 5; continue; }
            if (xml.compare(i, 6, "&quot;") == 0) { text += '"'; i += 5; continue; }
        }
        prev_was_break = false;
        text += xml[i];
    }
    return text;
}

std::vector<DocumentChunk> DocumentProcessor::process_docx(const std::string& docx_path) {
    fprintf(stderr, "[DOC] Processing DOCX: %s\n", docx_path.c_str());
    std::string text = extract_docx_text(docx_path);
    if (text.empty()) {
        fprintf(stderr, "[DOC] No text extracted from %s\n", docx_path.c_str());
        return {};
    }
    std::string filename = fs::path(docx_path).filename().string();
    auto chunks = semantic_chunk(text, filename);
    fprintf(stderr, "[DOC] Extracted %zu chunks from %s\n", chunks.size(), filename.c_str());
    return chunks;
}

std::vector<DocumentChunk> DocumentProcessor::process_file(const std::string& file_path) {
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        fprintf(stderr, "[DOC] File not found: %s\n", file_path.c_str());
        return {};
    }

    std::string ext = fs::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == ".pdf") return process_pdf(file_path);
    if (ext == ".docx") return process_docx(file_path);
    if (ext == ".doc") {
        fprintf(stderr, "[DOC] .doc (legacy Word) not supported — save as .docx or .pdf\n");
        return {};
    }

    // Treat as plain text
    std::ifstream ifs(file_path);
    if (!ifs) {
        fprintf(stderr, "[DOC] Failed to read %s\n", file_path.c_str());
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    std::string filename = fs::path(file_path).filename().string();
    auto chunks = process_text(content, filename);
    fprintf(stderr, "[DOC] Extracted %zu chunks from %s\n", chunks.size(), filename.c_str());
    return chunks;
}

std::vector<DocumentChunk> DocumentProcessor::process_path(const std::string& path) {
    if (fs::is_regular_file(path))
        return process_file(path);
    if (fs::is_directory(path))
        return process_directory(path);
    fprintf(stderr, "[DOC] Path not found: %s\n", path.c_str());
    return {};
}

std::vector<DocumentChunk> DocumentProcessor::process_directory(const std::string& dir_path) {
    std::vector<DocumentChunk> all_chunks;

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        fprintf(stderr, "[DOC] Directory not found: %s\n", dir_path.c_str());
        return {};
    }

    // Collect document files sorted by name
    std::vector<std::string> pdf_files;
    std::vector<std::string> docx_files;
    std::vector<std::string> txt_files;
    static const std::vector<std::string> text_exts = {
        ".txt", ".md", ".json", ".jsonl", ".csv", ".log",
        ".html", ".htm", ".xml", ".yaml", ".yml", ".ini",
        ".cfg", ".conf", ".rst", ".rtf",
    };
    for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext == ".pdf") {
                pdf_files.push_back(entry.path().string());
            } else if (ext == ".docx") {
                docx_files.push_back(entry.path().string());
            } else if (std::find(text_exts.begin(), text_exts.end(), ext) != text_exts.end()) {
                txt_files.push_back(entry.path().string());
            }
        }
    }
    std::sort(pdf_files.begin(), pdf_files.end());
    std::sort(docx_files.begin(), docx_files.end());
    std::sort(txt_files.begin(), txt_files.end());

    fprintf(stderr, "[DOC] Found %zu PDF, %zu DOCX, %zu text files in %s\n",
            pdf_files.size(), docx_files.size(), txt_files.size(), dir_path.c_str());

    for (const auto& pdf : pdf_files) {
        auto chunks = process_pdf(pdf);
        all_chunks.insert(all_chunks.end(),
                          std::make_move_iterator(chunks.begin()),
                          std::make_move_iterator(chunks.end()));
    }

    for (const auto& docx : docx_files) {
        auto chunks = process_docx(docx);
        all_chunks.insert(all_chunks.end(),
                          std::make_move_iterator(chunks.begin()),
                          std::make_move_iterator(chunks.end()));
    }

    // Process text files
    for (const auto& txt : txt_files) {
        fprintf(stderr, "[DOC] Processing TXT: %s\n", txt.c_str());
        std::ifstream ifs(txt);
        if (!ifs) {
            fprintf(stderr, "[DOC] Failed to read %s\n", txt.c_str());
            continue;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        std::string filename = fs::path(txt).filename().string();
        auto chunks = process_text(content, filename);
        fprintf(stderr, "[DOC] Extracted %zu chunks from %s\n", chunks.size(), filename.c_str());
        all_chunks.insert(all_chunks.end(),
                          std::make_move_iterator(chunks.begin()),
                          std::make_move_iterator(chunks.end()));
    }

    fprintf(stderr, "[DOC] Total chunks from directory: %zu\n", all_chunks.size());
    return all_chunks;
}

std::vector<DocumentChunk> DocumentProcessor::process_text(
    const std::string& text, const std::string& source_name)
{
    return semantic_chunk(text, source_name);
}

} // namespace rastack
