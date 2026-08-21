#ifndef RCLI_SDK_LLM_H
#define RCLI_SDK_LLM_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rcli::sdk {

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
    /// The download finished. The SDK writes its manifest as files land rather
    /// than at the end, so a partial carries one too; what settles it is
    /// whether every file the catalog names is present.
    bool complete = true;
};

/// Models present on this machine right now, found by walking the storage tree.
/// Reports what has finished downloading and says nothing about the catalog.
std::vector<LocalModel> LocalModels();

/// Deletes a downloaded model's directory and reports how many bytes went.
///
/// Refuses anything that is not a direct child of a framework directory under
/// the storage root: this removes a tree recursively, and a path that came out
/// of a catalog id has no business escaping the models folder.
bool Remove(const std::string& id, std::int64_t* freed, std::string* error);

/// A turn in the conversation. The SDK takes the whole exchange per request and
/// renders the chat template itself, so history lives here rather than being
/// pasted into the prompt.
struct Turn {
    bool from_user = true;
    std::string text;
};

enum class Piece { Answer, Thinking };

/// What the engine reported about the run it just finished. Every field is
/// zero when the engine did not report it, which is why nothing here is
/// re-derived from wall-clock time: a measured number and a guessed one should
/// not be indistinguishable.
struct Metrics {
    std::int64_t ttft_ms = 0;
    std::int64_t prefill_ms = 0;
    double tokens_per_second = 0.0;
    int input_tokens = 0;
    int output_tokens = 0;
    /// The engine estimated the token counts rather than counting them.
    bool estimated = false;
};

/// The loaded language model.
///
/// Everything goes through the SDK's proto path: the lifecycle load honours the
/// accelerator, engine pin and context length from settings, and the streaming
/// generate reports reasoning as its own event kind instead of leaving <think>
/// tags in the token stream for someone to parse back out.
class Llm {
   public:
    /// `id` is a catalog id when possible, which is what lets the load carry
    /// placement settings. A model only present on disk still loads, without
    /// them.
    bool Load(const std::string& id, std::string* error);
    bool loaded() const { return !model_id_.empty(); }
    const std::string& model_id() const { return model_id_; }

    /// `history` excludes the prompt being sent. A non-empty `image_path` sends
    /// the turn through the vision path instead, which needs a model whose
    /// engine fills the VLM slot. Returns false if busy.
    bool Generate(std::string prompt, std::vector<Turn> history, std::string image_path,
                  std::function<void(Piece, std::string)> on_piece,
                  std::function<void(std::string, Metrics)> on_done);

    /// True when the loaded model's catalog entry is multimodal, which is what
    /// makes an image attachment meaningful rather than silently ignored.
    bool multimodal() const { return multimodal_; }

    bool busy() const { return busy_->load(); }
    void Cancel();

   private:
    std::string model_id_;
    bool multimodal_ = false;
    std::shared_ptr<std::atomic<bool>> busy_ = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_LLM_H
