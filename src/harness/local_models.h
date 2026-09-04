#ifndef WALLY_HARNESS_LOCAL_MODELS_H
#define WALLY_HARNESS_LOCAL_MODELS_H

#include <cstdint>
#include <string>
#include <vector>

namespace wally::harness {

/// A model already on disk under the RunAnywhere home.
struct LocalModel {
    std::string id;
    /// The engine directory it was found in: LlamaCpp, Sherpa, MLX, ...
    std::string framework;
    /// The model's own directory.
    std::string dir;
    /// The first weight file inside it, or empty for a model whose weights are
    /// directories (CoreML .mlmodelc) rather than files.
    std::string path;
    std::int64_t bytes = 0;
};

/// Models present under `home` right now, found by walking the storage tree.
///
/// Walking rather than asking the registry: this only has to answer "is there
/// something here the local server can open", and the walk says that about a
/// model placed by hand as readily as one that was downloaded.
std::vector<LocalModel> LocalModels(const std::string& home);

}  // namespace wally::harness

#endif  // WALLY_HARNESS_LOCAL_MODELS_H
