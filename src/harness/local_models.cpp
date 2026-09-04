#include "harness/local_models.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace rcli::harness {
namespace {

namespace fs = std::filesystem;

bool IsWeightFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gguf" || ext == ".safetensors" || ext == ".onnx" || ext == ".bin";
}

/// Written by the download orchestrator as files land rather than once they all
/// have, so a partial download carries one too. Presence means "this came from
/// a download", not "the download finished".
constexpr std::string_view kManifest = ".rac-manifest.binpb";

}  // namespace

std::vector<LocalModel> LocalModels(const std::string& home) {
    std::vector<LocalModel> models;
    if (home.empty()) {
        return models;
    }

    // Both layouts are real: the SDK's path docs describe
    // {base}/RunAnywhere/Models, and the desktop default base directory already
    // ends in "runanywhere", so models land directly under {base}/Models.
    std::error_code ec;
    fs::path root = fs::path(home) / "RunAnywhere" / "Models";
    if (!fs::is_directory(root, ec)) {
        root = fs::path(home) / "Models";
    }
    if (!fs::is_directory(root, ec)) {
        return models;
    }

    for (const auto& framework : fs::directory_iterator(root, ec)) {
        if (!framework.is_directory()) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(framework.path(), ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::string weights;
            bool manifest = false;
            std::int64_t bytes = 0;
            for (const auto& file : fs::recursive_directory_iterator(entry.path(), ec)) {
                if (!file.is_regular_file()) {
                    continue;
                }
                bytes += static_cast<std::int64_t>(file.file_size(ec));
                if (file.path().filename() == kManifest) {
                    manifest = true;
                } else if (weights.empty() && IsWeightFile(file.path())) {
                    weights = file.path().string();
                }
            }
            // A directory holding weights but no manifest was placed by hand.
            // It is still loadable, so it still counts.
            if (!manifest && weights.empty()) {
                continue;
            }
            models.push_back({entry.path().filename().string(),
                              framework.path().filename().string(), entry.path().string(), weights,
                              bytes});
        }
    }

    std::sort(models.begin(), models.end(),
              [](const LocalModel& a, const LocalModel& b) { return a.id < b.id; });
    return models;
}

}  // namespace rcli::harness
