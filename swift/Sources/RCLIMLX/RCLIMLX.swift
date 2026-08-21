import CRCLIApp
import Darwin
import Foundation
import MLXRuntime

/// The Apple entry point.
///
/// MLX's inference lives in Swift: the engine inside commons is a table of
/// callbacks and nothing fills it in from C++. So the process starts here,
/// registers those callbacks, and then runs the same `rcli_run()` the plain
/// C++ binary runs — one application, two ways in.
@main
struct RCLIMLX {
    static func main() {
        var arguments = CommandLine.arguments.map { strdup($0) }
        defer { arguments.forEach { free($0) } }

        // Before MLX.register(), which logs: rcli_begin decides where the
        // engines' output goes and keeps a handle on the real stderr for the
        // CLI's own progress bars and errors.
        arguments.withUnsafeMutableBufferPointer { buffer in
            rcli_begin(Int32(buffer.count), buffer.baseAddress)
        }

        if !MLX.register() {
            // Not fatal: every other engine still works, and a CLI that refuses
            // to start because one accelerator is unavailable is worse than one
            // that starts without it.
            FileHandle.standardError.write(
                Data("rcli: MLX callbacks did not register; other engines still available\n".utf8))
        }

        let status = arguments.withUnsafeMutableBufferPointer { buffer in
            rcli_run(Int32(buffer.count), buffer.baseAddress)
        }
        Darwin.exit(status)
    }
}
