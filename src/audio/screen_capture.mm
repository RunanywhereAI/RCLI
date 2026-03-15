#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include "screen_capture.h"
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <cmath>
#include <atomic>
#include <mutex>
#include <thread>

extern char** environ;

// ---------------------------------------------------------------------------
// Helper: downscale a JPEG on disk if it exceeds max dimension (for VLM)
// ---------------------------------------------------------------------------
static void downscale_jpeg_if_needed(const char* path, int max_dim) {
    @autoreleasepool {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSData *data = [NSData dataWithContentsOfFile:nsPath];
        if (!data) return;

        NSBitmapImageRep *srcRep = [NSBitmapImageRep imageRepWithData:data];
        if (!srcRep) return;

        NSInteger w = srcRep.pixelsWide;
        NSInteger h = srcRep.pixelsHigh;
        if (w <= max_dim && h <= max_dim) return;

        CGFloat scale = (CGFloat)max_dim / fmax((CGFloat)w, (CGFloat)h);
        NSInteger nw = (NSInteger)floor(w * scale);
        NSInteger nh = (NSInteger)floor(h * scale);

        NSBitmapImageRep *dstRep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:nw
                          pixelsHigh:nh
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];

        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext *ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:dstRep];
        [NSGraphicsContext setCurrentContext:ctx];
        [ctx setImageInterpolation:NSImageInterpolationHigh];

        NSImage *nsImage = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)w, (CGFloat)h)];
        [nsImage addRepresentation:srcRep];
        [nsImage drawInRect:NSMakeRect(0, 0, (CGFloat)nw, (CGFloat)nh)
                   fromRect:NSZeroRect
                  operation:NSCompositingOperationCopy
                   fraction:1.0];

        [NSGraphicsContext restoreGraphicsState];

        NSData *jpegData = [dstRep representationUsingType:NSBitmapImageFileTypeJPEG
                                                properties:@{NSImageCompressionFactor: @0.85}];
        if (jpegData && jpegData.length > 0) {
            [jpegData writeToFile:nsPath atomically:YES];
        }
    }
}

// ---------------------------------------------------------------------------
// Helper: run screencapture with given args, verify output
// ---------------------------------------------------------------------------
static int run_screencapture(const char* const argv[], const char* output_path) {
    pid_t pid;
    int status = 0;
    if (posix_spawnp(&pid, "screencapture", nullptr, nullptr,
                     const_cast<char* const*>(argv), environ) != 0) {
        return -1;
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;

    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) return -1;

    downscale_jpeg_if_needed(output_path, 2048);
    return 0;
}

// ===========================================================================
// Visual overlay — spawns rcli_overlay helper process (separate Cocoa app)
// because AppKit window management requires the main thread, which FTXUI owns.
// Communication via stdin/stdout pipes.
// ===========================================================================

static pid_t g_overlay_pid = 0;
static FILE *g_overlay_stdin = nullptr;   // we write commands here
static FILE *g_overlay_stdout = nullptr;  // we read responses here
static std::atomic<bool> g_overlay_visible{false};

// Find rcli_overlay binary next to the rcli binary
static std::string find_overlay_binary() {
    // Try next to our own executable
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string dir(path);
        auto slash = dir.rfind('/');
        if (slash != std::string::npos) {
            std::string candidate = dir.substr(0, slash + 1) + "rcli_overlay";
            if (access(candidate.c_str(), X_OK) == 0) return candidate;
        }
    }
    // Fallback: try PATH
    return "rcli_overlay";
}

// Send a command to the overlay process and read the response line
static std::string overlay_cmd(const char* cmd) {
    if (!g_overlay_stdin || !g_overlay_stdout) return "";
    fprintf(g_overlay_stdin, "%s\n", cmd);
    fflush(g_overlay_stdin);
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), g_overlay_stdout)) {
        // Strip trailing newline
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return std::string(buf);
    }
    return "";
}

void screen_capture_show_overlay(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h; // TODO: pass initial rect to helper

    if (g_overlay_pid > 0) {
        // Already running — just return
        return;
    }

    std::string binary = find_overlay_binary();

    // Create pipes: parent→child stdin, child→parent stdout
    int pipe_in[2], pipe_out[2];
    if (pipe(pipe_in) != 0 || pipe(pipe_out) != 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        // Child: wire up pipes
        close(pipe_in[1]);   // close write end of stdin pipe
        close(pipe_out[0]);  // close read end of stdout pipe
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        // Redirect stderr to /dev/null to keep terminal clean
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl(binary.c_str(), "rcli_overlay", nullptr);
        _exit(1);
    }

    // Parent
    close(pipe_in[0]);
    close(pipe_out[1]);
    g_overlay_pid = pid;
    g_overlay_stdin = fdopen(pipe_in[1], "w");
    g_overlay_stdout = fdopen(pipe_out[0], "r");

    // Wait for "ready" from child
    char buf[64] = {0};
    if (g_overlay_stdout && fgets(buf, sizeof(buf), g_overlay_stdout)) {
        g_overlay_visible.store(true);
    }
}

void screen_capture_hide_overlay(void) {
    if (g_overlay_pid <= 0) return;

    overlay_cmd("quit");

    // Clean up
    if (g_overlay_stdin) { fclose(g_overlay_stdin); g_overlay_stdin = nullptr; }
    if (g_overlay_stdout) { fclose(g_overlay_stdout); g_overlay_stdout = nullptr; }
    int status;
    waitpid(g_overlay_pid, &status, 0);
    g_overlay_pid = 0;
    g_overlay_visible.store(false);
}

int screen_capture_overlay_active(void) {
    return g_overlay_visible.load() ? 1 : 0;
}

int screen_capture_overlay_region(const char* output_path) {
    if (!g_overlay_visible.load() || g_overlay_pid <= 0) return -1;

    // Get frame coordinates (top-left origin)
    std::string frame_str = overlay_cmd("frame");
    if (frame_str.empty()) return -1;

    // Hide overlay for capture
    overlay_cmd("hide");

    // Capture the region
    char region[128];
    strlcpy(region, frame_str.c_str(), sizeof(region));
    const char* argv[] = {
        "screencapture", "-x", "-t", "jpg", "-R", region, output_path, nullptr
    };
    int result = run_screencapture(argv, output_path);

    // Show overlay again
    overlay_cmd("show");

    return result;
}

// ---------------------------------------------------------------------------
// Track the previously active app (before our terminal got focus)
// Polls frontmostApplication every 200ms on a background thread.
// NSWorkspace notifications don't work in CLI apps (no NSApplication run loop).
// ---------------------------------------------------------------------------

static std::atomic<pid_t> g_prev_active_pid{0};
static pid_t g_our_terminal_pid = 0;
static char g_prev_app_name[256] = {0};
static std::mutex g_name_mutex;

// Walk up process tree to find which ancestor owns a window (our terminal)
static pid_t find_terminal_pid() {
    @autoreleasepool {
        pid_t cur = getpid();
        pid_t ancestors[8];
        int n = 0;
        while (cur > 1 && n < 8) {
            ancestors[n++] = cur;
            struct kinfo_proc kp;
            size_t length = sizeof(kp);
            int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, cur };
            if (sysctl(mib, 4, &kp, &length, NULL, 0) != 0) break;
            pid_t ppid = kp.kp_eproc.e_ppid;
            if (ppid == cur) break;
            cur = ppid;
        }

        // Check which ancestor owns on-screen windows — that's the terminal
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CFArrayRef windowList = CGWindowListCopyWindowInfo(
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
            kCGNullWindowID);
        #pragma clang diagnostic pop
        if (windowList) {
            NSArray *windows = CFBridgingRelease(windowList);
            for (int i = n - 1; i >= 0; i--) {
                for (NSDictionary *info in windows) {
                    pid_t ownerPid = [[info objectForKey:(NSString *)kCGWindowOwnerPID] intValue];
                    if (ownerPid == ancestors[i]) {
                        return ancestors[i];
                    }
                }
            }
        }
        return (n >= 3) ? ancestors[2] : getppid();
    }
}

// Background poller — tracks which non-terminal app is frontmost
__attribute__((constructor))
static void start_app_tracking() {
    @autoreleasepool {
        g_our_terminal_pid = find_terminal_pid();

        // Seed with current frontmost app if it's not our terminal
        NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front && front.processIdentifier != g_our_terminal_pid) {
            g_prev_active_pid.store(front.processIdentifier, std::memory_order_relaxed);
            NSString *name = front.localizedName ?: @"unknown";
            std::lock_guard<std::mutex> lock(g_name_mutex);
            strlcpy(g_prev_app_name, [name UTF8String], sizeof(g_prev_app_name));
        }

        // Poll frontmostApplication every 200ms on a background thread
        std::thread([]() {
            pthread_setname_np("rcli.app_tracker");
            pid_t last_seen_pid = 0;
            while (true) {
                @autoreleasepool {
                    NSRunningApplication *front =
                        [[NSWorkspace sharedWorkspace] frontmostApplication];
                    if (front) {
                        pid_t pid = front.processIdentifier;
                        // If a non-terminal app is frontmost and it changed, record it
                        if (pid != g_our_terminal_pid && pid != last_seen_pid) {
                            last_seen_pid = pid;
                            g_prev_active_pid.store(pid, std::memory_order_relaxed);
                            NSString *name = front.localizedName ?: @"unknown";
                            std::lock_guard<std::mutex> lock(g_name_mutex);
                            strlcpy(g_prev_app_name, [name UTF8String],
                                    sizeof(g_prev_app_name));
                        }
                    }
                }
                usleep(200000); // 200ms
            }
        }).detach();
    }
}

// ---------------------------------------------------------------------------
// Window lookup helpers
// ---------------------------------------------------------------------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static bool is_normal_window(NSDictionary *info) {
    NSDictionary *bounds = [info objectForKey:(NSString *)kCGWindowBounds];
    if (!bounds) return false;
    CGFloat w = [[bounds objectForKey:@"Width"] floatValue];
    CGFloat h = [[bounds objectForKey:@"Height"] floatValue];
    return (w >= 100 && h >= 100);
}

// Find a normal window belonging to a specific PID
static CGWindowID find_window_for_pid(pid_t target_pid) {
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windowList) return kCGNullWindowID;

    NSArray *windows = CFBridgingRelease(windowList);
    for (NSDictionary *info in windows) {
        pid_t ownerPid = [[info objectForKey:(NSString *)kCGWindowOwnerPID] intValue];
        if (ownerPid != target_pid) continue;
        if (!is_normal_window(info)) continue;
        return [[info objectForKey:(NSString *)kCGWindowNumber] unsignedIntValue];
    }
    return kCGNullWindowID;
}

// Find the frontmost normal window of the frontmost app
static CGWindowID get_frontmost_window_id() {
    @autoreleasepool {
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (!frontApp) return kCGNullWindowID;
        return find_window_for_pid(frontApp.processIdentifier);
    }
}

// Find the window of the previously active app (before terminal got focus)
static CGWindowID get_previous_app_window_id() {
    @autoreleasepool {
        pid_t prev_pid = g_prev_active_pid.load(std::memory_order_relaxed);
        if (prev_pid <= 0) return kCGNullWindowID;
        return find_window_for_pid(prev_pid);
    }
}

#pragma clang diagnostic pop

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static int capture_window_id(CGWindowID wid, const char* output_path) {
    if (wid == kCGNullWindowID) return -1;
    char wid_str[32];
    snprintf(wid_str, sizeof(wid_str), "%u", wid);
    const char* argv[] = {
        "screencapture", "-x", "-t", "jpg", "-l", wid_str, output_path, nullptr
    };
    return run_screencapture(argv, output_path);
}

int screen_capture_active_window(const char* output_path) {
    CGWindowID wid = get_frontmost_window_id();
    if (wid == kCGNullWindowID) {
        return screen_capture_full_screen(output_path);
    }
    return capture_window_id(wid, output_path);
}

int screen_capture_behind_terminal(const char* output_path) {
    // Use the tracked previously-active app (before terminal got focus)
    {
        std::lock_guard<std::mutex> lock(g_name_mutex);
        pid_t prev = g_prev_active_pid.load(std::memory_order_relaxed);
        fprintf(stderr, "[Screen] Targeting: %s (PID %d)\n",
                g_prev_app_name[0] ? g_prev_app_name : "none", prev);
    }
    CGWindowID wid = get_previous_app_window_id();
    if (wid == kCGNullWindowID) {
        fprintf(stderr, "[Screen] No previous app window found, falling back to full screen\n");
        return screen_capture_full_screen(output_path);
    }
    return capture_window_id(wid, output_path);
}

int screen_capture_full_screen(const char* output_path) {
    const char* argv[] = {
        "screencapture", "-x", "-t", "jpg", output_path, nullptr
    };
    return run_screencapture(argv, output_path);
}

int screen_capture_screenshot(const char* output_path) {
    // Prefer overlay if active, then active window, then full screen
    if (screen_capture_overlay_active()) {
        return screen_capture_overlay_region(output_path);
    }
    return screen_capture_active_window(output_path);
}

const char* screen_capture_target_app_name(char* buf, int buf_size) {
    std::lock_guard<std::mutex> lock(g_name_mutex);
    if (g_prev_app_name[0]) {
        strlcpy(buf, g_prev_app_name, buf_size);
    } else {
        strlcpy(buf, "unknown", buf_size);
    }
    return buf;
}
