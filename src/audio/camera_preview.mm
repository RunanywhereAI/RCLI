#include "camera_preview.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <mach-o/dyld.h>

static pid_t g_cam_pid = 0;
static FILE *g_cam_stdin  = nullptr;
static FILE *g_cam_stdout = nullptr;
static std::atomic<bool> g_cam_active{false};

static std::string find_camera_preview_binary() {
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string dir(path);
        auto slash = dir.rfind('/');
        if (slash != std::string::npos) {
            std::string candidate = dir.substr(0, slash + 1) + "rcli_camera_preview";
            if (access(candidate.c_str(), X_OK) == 0) return candidate;
        }
    }
    return "rcli_camera_preview";
}

static std::string cam_cmd(const char* cmd) {
    if (!g_cam_stdin || !g_cam_stdout) return "";
    fprintf(g_cam_stdin, "%s\n", cmd);
    fflush(g_cam_stdin);
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), g_cam_stdout)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return std::string(buf);
    }
    return "";
}

int camera_preview_start(void) {
    if (g_cam_pid > 0) return 0;

    std::string binary = find_camera_preview_binary();

    int pipe_in[2], pipe_out[2];
    if (pipe(pipe_in) != 0 || pipe(pipe_out) != 0) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl(binary.c_str(), "rcli_camera_preview", nullptr);
        _exit(1);
    }

    close(pipe_in[0]);
    close(pipe_out[1]);
    g_cam_pid = pid;
    g_cam_stdin  = fdopen(pipe_in[1], "w");
    g_cam_stdout = fdopen(pipe_out[0], "r");

    char buf[64] = {0};
    if (g_cam_stdout && fgets(buf, sizeof(buf), g_cam_stdout)) {
        g_cam_active.store(true);
        return 0;
    }

    camera_preview_stop();
    return -1;
}

void camera_preview_stop(void) {
    if (g_cam_pid <= 0) return;

    cam_cmd("quit");

    if (g_cam_stdin)  { fclose(g_cam_stdin);  g_cam_stdin  = nullptr; }
    if (g_cam_stdout) { fclose(g_cam_stdout); g_cam_stdout = nullptr; }
    int status;
    waitpid(g_cam_pid, &status, 0);
    g_cam_pid = 0;
    g_cam_active.store(false);
}

int camera_preview_active(void) {
    return g_cam_active.load() ? 1 : 0;
}

int camera_preview_capture(const char* output_path) {
    if (!g_cam_active.load()) return -1;
    std::string cmd = std::string("capture ") + output_path;
    std::string resp = cam_cmd(cmd.c_str());
    return (resp == "ok") ? 0 : -1;
}

int camera_preview_snap(const char* output_path) {
    if (!g_cam_active.load()) return -1;
    std::string cmd = std::string("snap ") + output_path;
    std::string resp = cam_cmd(cmd.c_str());
    return (resp == "ok") ? 0 : -1;
}

int camera_preview_freeze(void) {
    if (!g_cam_active.load()) return -1;
    std::string resp = cam_cmd("freeze");
    return (resp == "ok") ? 0 : -1;
}

int camera_preview_unfreeze(void) {
    if (!g_cam_active.load()) return -1;
    std::string resp = cam_cmd("unfreeze");
    return (resp == "ok") ? 0 : -1;
}
