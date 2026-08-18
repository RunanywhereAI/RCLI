#ifndef RCLI_SDK_SESSION_H
#define RCLI_SDK_SESSION_H

#include <string>
#include <string_view>
#include <vector>

namespace rcli::sdk {

/// An engine the SDK registered, and what it can serve on this machine.
struct BackendInfo {
    std::string name;
    int priority = 0;
    std::vector<std::string> primitives;
};

/// Brings the SDK up once for the process: desktop platform adapter, storage
/// root, rac_init, the curl HTTP transport, then whichever engines this build
/// compiled in.
///
/// Failure is reported rather than thrown; the UI shows what came up and what
/// did not, which is more useful than refusing to start. An engine missing here
/// is normal — MLX needs Swift callbacks, NeuRT needs an Apple machine.
class Session {
   public:
    static Session& Instance();

    bool Start();
    bool started() const { return started_; }
    /// Empty while everything is fine.
    std::string_view error() const { return error_; }

    /// Storage root: $RUNANYWHERE_HOME, else the SDK's desktop default. Shared
    /// with every other RunAnywhere desktop app, which is why a model pulled by
    /// one is visible to the others.
    std::string_view home() const { return home_; }

    const std::vector<BackendInfo>& backends() const { return backends_; }

   private:
    Session() = default;

    bool started_ = false;
    std::string error_;
    std::string home_;
    std::vector<BackendInfo> backends_;
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_SESSION_H
