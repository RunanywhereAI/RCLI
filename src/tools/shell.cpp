#include "tools/shell.h"

#include <array>
#include <chrono>
#include <cstdio>

namespace rcli::tools {
namespace {

constexpr std::size_t kMaxOutputBytes = 64 * 1024;

}  // namespace

Output Run(const std::string& command, int timeout_seconds) {
    Output output;

    // 2>&1 because a failing command usually explains itself on stderr, and
    // splitting the streams would hide exactly the part worth reading.
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        output.text = "could not start a shell";
        return output;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    std::array<char, 4096> chunk{};
    while (std::fgets(chunk.data(), static_cast<int>(chunk.size()), pipe) != nullptr) {
        output.text.append(chunk.data());
        // Truncating rather than streaming keeps a runaway command from filling
        // memory; the transcript says so instead of silently stopping.
        if (output.text.size() > kMaxOutputBytes) {
            output.text.resize(kMaxOutputBytes);
            output.text += "\n… output truncated";
            break;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            output.timed_out = true;
            output.text += "\n… stopped after " + std::to_string(timeout_seconds) + "s";
            break;
        }
    }

    const int closed = pclose(pipe);
    output.status = closed == -1 ? -1 : (closed / 256);
    return output;
}

}  // namespace rcli::tools
