import CRCLIApp
import Darwin
import Foundation
import MLXRuntime

/// Product `rcli` on Apple: install MLX callbacks, then the same C++ CLI
/// (`rcli_run_main`) that Windows runs as `rcli.exe`. One command surface.
@main
struct RCLIMLX {
    static func main() {
        var arguments = CommandLine.arguments.map { strdup($0) }
        defer { arguments.forEach { free($0) } }

        if !MLX.register() {
            FileHandle.standardError.write(
                Data("rcli: MLX callbacks did not register; other engines still available\n".utf8))
        }

        let status = arguments.withUnsafeMutableBufferPointer { buffer in
            rcli_run_main(Int32(buffer.count), buffer.baseAddress)
        }
        Darwin.exit(status)
    }
}
