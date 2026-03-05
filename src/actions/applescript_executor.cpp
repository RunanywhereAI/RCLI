#include "actions/applescript_executor.h"
#include <cstdio>
#include <cstring>
#include <array>
#include <chrono>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

namespace rcli {

// Internal: run a command with args[], capture stdout+stderr, enforce timeout
static ScriptResult run_subprocess(const char* const argv[], int timeout_ms) {
    ScriptResult result;
    result.exit_code = -1;
    result.success = false;

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.error = "Failed to create pipes";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.error = "Fork failed";
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }

    // Parent
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    auto start = std::chrono::steady_clock::now();
    bool timed_out = false;

    // Read output with timeout checks
    std::string out_buf, err_buf;
    std::array<char, 4096> buf;

    // Non-blocking read loop
    fd_set fds;
    int max_fd = std::max(stdout_pipe[0], stderr_pipe[0]) + 1;
    bool stdout_done = false, stderr_done = false;

    while (!stdout_done || !stderr_done) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            kill(pid, SIGTERM);
            timed_out = true;
            break;
        }

        FD_ZERO(&fds);
        if (!stdout_done) FD_SET(stdout_pipe[0], &fds);
        if (!stderr_done) FD_SET(stderr_pipe[0], &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        int sel = select(max_fd, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        if (!stdout_done && FD_ISSET(stdout_pipe[0], &fds)) {
            ssize_t n = read(stdout_pipe[0], buf.data(), buf.size());
            if (n <= 0) stdout_done = true;
            else out_buf.append(buf.data(), n);
        }
        if (!stderr_done && FD_ISSET(stderr_pipe[0], &fds)) {
            ssize_t n = read(stderr_pipe[0], buf.data(), buf.size());
            if (n <= 0) stderr_done = true;
            else err_buf.append(buf.data(), n);
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (timed_out) {
        result.error = "Timed out after " + std::to_string(timeout_ms) + "ms";
        result.exit_code = -1;
        return result;
    }

    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.output = out_buf;
    result.error = err_buf;
    result.success = (result.exit_code == 0);

    // Trim trailing newlines
    while (!result.output.empty() && result.output.back() == '\n')
        result.output.pop_back();

    return result;
}

ScriptResult run_applescript(const std::string& script, int timeout_ms) {
    const char* argv[] = {"osascript", "-e", script.c_str(), nullptr};
    return run_subprocess(argv, timeout_ms);
}

ScriptResult run_jxa(const std::string& script, int timeout_ms) {
    const char* argv[] = {"osascript", "-l", "JavaScript", "-e", script.c_str(), nullptr};
    return run_subprocess(argv, timeout_ms);
}

ScriptResult run_shell(const std::string& command, int timeout_ms) {
    // Safety check: block destructive commands
    std::string lower = command;
    for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
    static const char* blocklist[] = {
        "rm -rf /", "rm -rf ~", "rm -rf /*", "sudo rm",
        "mkfs", "dd if=", ":(){ :|:", "chmod -r 777 /",
        "sudo su", "sudo bash", "sudo sh",
        "> /dev/sda", "shutdown", "reboot", "halt",
    };
    for (auto& blocked : blocklist) {
        if (lower.find(blocked) != std::string::npos) {
            return {false, "", "Blocked dangerous command", -1};
        }
    }

    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    return run_subprocess(argv, timeout_ms);
}

} // namespace rcli
