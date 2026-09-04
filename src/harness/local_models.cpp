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

    // Every step of the walk takes an error_code, including the increments.
    // Constructing the iterator with one and then advancing it with the
    // range-for's throwing operator++ only looks safe: a directory removed or
    // locked while this runs escapes as a filesystem_error out of what is a
    // best-effort listing, and this is called from the launch path.
    //
    // `walk` guards iteration and stops that level when advancing fails.
    // `probe` is for the status queries, whose false-on-error answer is already
    // the one we want, so it is reset rather than checked.
    constexpr auto kSkip = fs::directory_options::skip_permission_denied;
    std::error_code walk;
    std::error_code probe;

    for (fs::directory_iterator framework(root, kSkip, walk), no_more_frameworks;
         !walk && framework != no_more_frameworks; framework.increment(walk)) {
        if (!framework->is_directory(probe)) {
            probe.clear();
            continue;
        }
        std::error_code entries;
        for (fs::directory_iterator entry(framework->path(), kSkip, entries), no_more_entries;
             !entries && entry != no_more_entries; entry.increment(entries)) {
            if (!entry->is_directory(probe)) {
                probe.clear();
                continue;
            }
            std::string weights;
            bool manifest = false;
            std::int64_t bytes = 0;
            std::error_code files;
            for (fs::recursive_directory_iterator file(entry->path(), kSkip, files),
                 no_more_files;
                 !files && file != no_more_files; file.increment(files)) {
                if (!file->is_regular_file(probe)) {
                    probe.clear();
                    continue;
                }
                // A file that vanished between the listing and the stat reports
                // an error and an unspecified size; adding that unchecked cast
                // a -1 into a wildly wrong total.
                const std::uintmax_t size = file->file_size(probe);
                if (!probe) {
                    bytes += static_cast<std::int64_t>(size);
                }
                probe.clear();
                if (file->path().filename() == kManifest) {
                    manifest = true;
                } else if (weights.empty() && IsWeightFile(file->path())) {
                    weights = file->path().string();
                }
            }
            // A directory holding weights but no manifest was placed by hand.
            // It is still loadable, so it still counts.
            if (!manifest && weights.empty()) {
                continue;
            }
            models.push_back({entry->path().filename().string(),
                              framework->path().filename().string(), entry->path().string(),
                              weights, bytes});
        }
    }

    std::sort(models.begin(), models.end(),
              [](const LocalModel& a, const LocalModel& b) { return a.id < b.id; });
    return models;
}

}  // namespace rcli::harness
