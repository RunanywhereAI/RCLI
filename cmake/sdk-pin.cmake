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
set(RCLI_PINNED_SDK_VERSION "0.20.33")
set(RCLI_PINNED_IDL_VERSION "1.1.1")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "ec50b7ca4beff9fa065c5ae1f12dbbc0c996d8b43e64c44bf350ef86a1fa41aa")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "e941152803adc297256fc98afa9eecd8f0bb38dfda8bd5b2f3054a67d5e23ec5")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "2ed56a8acb860fadd0141407e8669cacd05458e21470a3616e9b7b2e73f8d927")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "673358bd8391bb28db0b947c8155ef193ffbe107cf518d088852ad6ecd39a9e1")
