#ifndef RCLI_SDK_IMAGINE_H
#define RCLI_SDK_IMAGINE_H

#include <cstdint>
#include <functional>
#include <string>

namespace rcli::sdk {

/// Image generation. Held apart from the language model for the same reason
/// speech is: a conversation can want text and a picture in the same session,
/// and swapping one out to reach the other would reload on every turn.
class Imagine {
   public:
    bool Load(const std::string& model_id, std::string* error);
    bool loaded() const { return !model_id_.empty(); }
    const std::string& model_id() const { return model_id_; }

    struct Result {
        /// Where the PNG was written. Empty on failure.
        std::string path;
        int width = 0;
        int height = 0;
        std::int64_t seed = 0;
    };

    /// Writes the image next to the other RunAnywhere data and returns its
    /// path. `on_step` is called from the worker with 0..1 progress.
    Result Draw(const std::string& prompt, std::function<void(float)> on_step,
                std::string* error);

    /// The first downloaded image model, so a first run needs no argument.
    static std::string DefaultModel();

   private:
    std::string model_id_;
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_IMAGINE_H
