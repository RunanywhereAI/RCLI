// swift-tools-version: 6.0
import PackageDescription

// The Apple build of rcli.
//
// MLX inference is written in Swift, and the MLX engine inside commons is only
// a table of callbacks that Swift fills in. A pure C++ binary therefore links
// the engine and can never use it. This package is the other entry point: it
// registers the callbacks and then calls the same `rcli_run()` the C++ binary
// calls, so there is one application rather than two that drift.
//
// SwiftPM owns the final link on purpose. It is the half of the build that
// knows how to lay MLX's Metal shader bundles down beside the executable, and
// getting that wrong is a runtime failure rather than a build one.
//
// The C++ side arrives as linker arguments rather than as anything named here:
// `scripts/build-mlx.sh` merges what CMake built into one archive and passes it
// with -Xlinker. A manifest that read those paths from a file would go stale
// silently, because SwiftPM caches the manifest and would not know the file
// had changed.

let package = Package(
    name: "rcli-mlx",
    // Matches the SDK; the MLX product will not resolve below it.
    platforms: [.macOS("14.5")],
    dependencies: [
        .package(path: "../../runanywhere-sdks"),
    ],
    targets: [
        // Declares rcli_run() for Swift, and defines the Metal resource anchor
        // that MLXRuntime expects its host to provide.
        .target(name: "CRCLIApp"),

        .executableTarget(
            name: "RCLIMLX",
            dependencies: [
                "CRCLIApp",
                .product(name: "RunAnywhereMLXRuntime", package: "runanywhere-sdks"),
            ]
        ),
    ]
)
