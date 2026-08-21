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

/// The SDK's own model category, one to one.
///
/// An earlier version of this collapsed these into a coarser set for display
/// and then used that set to fill ModelInfo.category. VAD and TTS both became
/// "voice", so installing silero-vad asked the lifecycle to load a VAD graph as
/// a TTS voice and it failed. Display grouping and routing are different jobs;
/// this enum does routing, and Label() does the display.
enum class Category {
    Language,
    Multimodal,
    SpeechRecognition,
    SpeechSynthesis,
    VoiceActivityDetection,
    SpeakerDiarization,
    SemanticSegmentation,
    Embedding,
    ImageGeneration,
};

/// The on-disk shape, which decides how a model is fetched.
enum class Format { Gguf, Onnx, Safetensors, Mlpackage, Unspecified };

/// One artifact of a multi-file model: an MLX weight directory, a Sherpa
/// bundle, a GGUF paired with its mmproj.
struct File {
    std::string_view url;
    std::string_view filename;
    bool required;
    /// 0 when the catalog does not state one.
    std::int64_t bytes;
};

struct Model {
    std::string_view id;
    /// Short name accepted anywhere the id is. Empty when there isn't one.
    std::string_view alias;
    std::string_view name;
    Backend backend;
    Category category;
    std::int64_t bytes;
    /// Empty for a multi-file model; `files` carries the manifest instead.
    std::string_view url;
    Format format;
    /// 0 when the catalog does not state one.
    int context_length;
    /// The model wraps reasoning in think tags. Commons needs this on the
    /// registry entry to route the reasoning channel instead of guessing, and
    /// guessing is what leaks a bare `</think>` into the answer.
    bool thinks;
    /// Empty for a single-file model, which uses `url`.
    std::span<const File> files;
};

/// True when this entry can be downloaded with what the snapshot knows.
bool Installable(const Model& model);

std::span<const Model> All();

std::string_view Label(Backend backend);
std::string_view Label(Category category);
std::string_view Label(Format format);

/// "639 MB", "2.5 GB". Sizes come from the catalog and are exact artifact byte
/// counts, so the rounding here is the only approximation.
std::string HumanSize(std::int64_t bytes);

/// Case-insensitive substring match over id, alias and name. An empty query
/// matches everything, which is what an empty search box should do.
std::vector<const Model*> Search(std::string_view query);

}  // namespace rcli::catalog

#endif  // RCLI_CATALOG_CATALOG_H
