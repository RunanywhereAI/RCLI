#ifndef RCLI_CATALOG_CATALOG_H
#define RCLI_CATALOG_CATALOG_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rcli::catalog {

/// The engine that runs the model, which is what decides whether it can run on
/// this machine at all — MLX and NeuRT are Apple Silicon only.
enum class Backend { LlamaCpp, Mlx, NeuRT, Onnx, Sherpa };

/// What the model is for. Coarser than the SDK's category enum on purpose: this
/// is a grouping for the eye, not a routing decision.
enum class Modality { Language, Vision, Speech, Voice, Embedding, Image };

struct Model {
    std::string_view id;
    /// Short name accepted anywhere the id is. Empty when there isn't one.
    std::string_view alias;
    std::string_view name;
    Backend backend;
    Modality modality;
    std::int64_t bytes;
};

std::span<const Model> All();

std::string_view Label(Backend backend);
std::string_view Label(Modality modality);

/// "639 MB", "2.5 GB". Sizes come from the catalog and are exact artifact byte
/// counts, so the rounding here is the only approximation.
std::string HumanSize(std::int64_t bytes);

/// Case-insensitive substring match over id, alias and name. An empty query
/// matches everything, which is what an empty search box should do.
std::vector<const Model*> Search(std::string_view query);

}  // namespace rcli::catalog

#endif  // RCLI_CATALOG_CATALOG_H
