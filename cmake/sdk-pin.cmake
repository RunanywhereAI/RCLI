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
set(RCLI_PINNED_SDK_VERSION "0.20.34")
set(RCLI_PINNED_IDL_VERSION "1.1.1")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "ec50b7ca4beff9fa065c5ae1f12dbbc0c996d8b43e64c44bf350ef86a1fa41aa")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "8bf2019b27f10001b33d78338b5dcf78a14977c70fcded3f8a3e0647ca8da188")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "d472ada77c67bdf4445cd0ba12cb029e9f9cc3ff09937b18fe8025d5e28e5c6f")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "4d1984ec8c3867a2fe23ed660e586b5ff8515b31211acf8eff7144dfee9cea22")
