import CWallyApp
import Darwin
import Foundation
import MLXRuntime

/// MLXRuntime logs its own registration result through `os.Logger`
/// (runanywhere-sdks/bindings/swift/Sources/MLXRuntime/MLX.swift), a
/// different logging system in a different repo that
/// `wally_quiet_sdk_logging()` has no reach into -- confirmed live: neither
/// `-q` nor `--json` touch it either, and OS_ACTIVITY_MODE=disable doesn't
/// on this OS version. Muting stderr for just this call is the only handle
/// wally has on it. Safe to lose whatever else prints in that window: the
/// call's own return value is what the caller below actually acts on.
@MainActor
func registerMLXQuietly() -> Bool {
    let saved = dup(STDERR_FILENO)
    let devNull = open("/dev/null", O_WRONLY)
    if saved >= 0, devNull >= 0 {
        dup2(devNull, STDERR_FILENO)
    }
    defer {
        if saved >= 0 {
            dup2(saved, STDERR_FILENO)
            close(saved)
        }
        if devNull >= 0 {
            close(devNull)
        }
    }
    return MLX.register()
}

/// Product `wally` on Apple: install MLX callbacks, then the same C++ CLI
/// (`wally_run_main`) that Windows runs as `wally.exe`. One command surface.
@main
struct WallyMLX {
    static func main() {
        var arguments = CommandLine.arguments.map { strdup($0) }
        defer { arguments.forEach { free($0) } }

        // Ahead of MLX.register(): it logs through the same SDK logger this
        // silences, and wally_run_main() below only mutes what comes after it.
        wally_quiet_sdk_logging()

        if !registerMLXQuietly() {
            FileHandle.standardError.write(
                Data("wally: MLX callbacks did not register; other engines still available\n".utf8))
        }

        let status = arguments.withUnsafeMutableBufferPointer { buffer in
            wally_run_main(Int32(buffer.count), buffer.baseAddress)
        }
        Darwin.exit(status)
    }
}
