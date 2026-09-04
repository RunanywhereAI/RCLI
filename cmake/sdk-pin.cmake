# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
#
# 0.20.33 moves the IDL pin to 1.1.1 / ec50b7ca, retiring the 0.20.32 note that
# held it at 1.1.0. That note was correct for its release: 0.20.32 published via
# `publish_from_run_id`, reusing artifacts built before the monorepo bumped
# idl/VERSION for a COMMENT-ONLY .proto edit, so the kit genuinely carried the
# older lock and pinning 1.1.1 would have failed fetch-kit.sh against the very
# kit it validates. 0.20.33's kits were built after that bump, so these values
# are read from the shipped SCHEMA_LOCK rather than carried forward.
set(RCLI_PINNED_SDK_VERSION "0.20.36")
set(RCLI_PINNED_IDL_VERSION "1.2.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "571199c430eead0638472fc55355513831aeb2139e6fee5d5cd841f1b01d5a43")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "ed2ddeb64c22936a8c3769ba15734455e4a5aa1a719a33ca97b5e765a5848da6")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "9014f0c76fb4320a7cee8658b9309aeb9f173baec7b9907510a38930d5693fcb")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "4a877ab7a326fdf11fa8593f25170dd15b94949c15aa04a774547eeaea12de75")
