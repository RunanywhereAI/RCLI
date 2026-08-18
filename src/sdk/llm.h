#ifndef RCLI_SDK_LLM_H
#define RCLI_SDK_LLM_H

#include <atomic>
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
    std::string path;
};

/// Models present on this machine right now.
///
/// This walks the storage tree rather than asking the registry, because the
/// registry's discovery path goes through the proto lifecycle ABI. It sees
/// exactly what has finished downloading, which is the question chat needs
/// answered, and it deliberately reports nothing about the catalog.
std::vector<LocalModel> LocalModels();

/// One loaded language model, streaming tokens as they arrive.
///
/// Generation runs on a worker thread: the SDK's stream call blocks until the
/// answer is finished, and the render loop cannot be blocked for the length of
/// a reply. `on_token` and `on_done` are therefore called from that thread, and
/// the caller is responsible for getting back to the UI thread.
class Llm {
   public:
    ~Llm();

    bool Load(const LocalModel& model, std::string* error);
    bool loaded() const { return handle_ != nullptr; }
    const std::string& model_id() const { return model_id_; }

    /// Returns false when a generation is already running.
    bool Generate(std::string prompt, std::function<void(std::string)> on_token,
                  std::function<void(std::string)> on_done);

    bool busy() const { return busy_->load(); }
    void Cancel();

   private:
    void* handle_ = nullptr;
    std::string model_id_;
    /// Shared with the worker so the flag outlives this object if a generation
    /// is still running when the app tears down.
    std::shared_ptr<std::atomic<bool>> busy_ = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_LLM_H
