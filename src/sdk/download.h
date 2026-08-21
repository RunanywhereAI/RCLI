#ifndef RCLI_SDK_DOWNLOAD_H
#define RCLI_SDK_DOWNLOAD_H

#include <cstdint>
#include <functional>
#include <map>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <string>

#include "catalog/catalog.h"

namespace rcli::sdk {

enum class Phase { Idle, Pending, Running, Extracting, Done, Failed, Cancelled };

struct Download {
    Phase phase = Phase::Idle;
    /// 0..1 across every planned file and stage.
    float fraction = 0.0F;
    std::int64_t bytes = 0;
    std::int64_t total = 0;
    float bytes_per_second = 0.0F;
    /// The failure message, or the file being fetched. Empty when neither.
    std::string detail;
};

/// Every download in the process, and the one place the SDK's progress
/// callback is registered.
///
/// The callback is process-wide in commons and fires on a worker thread, so a
/// screen cannot own it: it would have to be re-registered on every screen
/// switch and would race with a transfer already in flight. This owns it for
/// the lifetime of the process and hands screens a snapshot instead.
class Downloads {
   public:
    static Downloads& Instance();

    /// Begins a transfer, or returns false with `error` set. A model already
    /// running is not restarted.
    bool Start(const catalog::Model& model, std::string* error);
    void Cancel(const std::string& id);

    Download Get(const std::string& id) const;
    bool Busy() const;

    /// A transfer that is still going, for anything that wants to show one.
    struct Active {
        std::string id;
        Phase phase = Phase::Idle;
        float fraction = 0.0F;
    };
    std::vector<Active> InFlight() const;

    /// Runs on the SDK's thread whenever progress moves; used to ask the
    /// terminal for a repaint. Every listener is kept: this used to hold one
    /// handler, so the second screen to register silently unhooked the first.
    void Listen(std::function<void()> handler);

    /// Blocks until `id` reaches a terminal state. For callers that need the
    /// model on disk before they can go on, and would otherwise poll.
    Phase Await(const std::string& id);

   private:
    Downloads();

    friend void OnProgress(const std::uint8_t*, std::size_t, void*);

    mutable std::mutex mutex_;
    std::condition_variable settled_;
    std::map<std::string, Download> state_;
    std::vector<std::function<void()>> listeners_;
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_DOWNLOAD_H
