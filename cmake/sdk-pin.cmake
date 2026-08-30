# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
set(RCLI_PINNED_SDK_VERSION "0.20.31")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "a8da7b6fa4cd361c2331f196cefdd12526dc2ae568d59f8851836f418389ff75")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "ac4545e4be62702df4381cf8fdcb07cd57697bc547d9d388a3cb78bd227f3732")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "f1b5ff301462fbe5910bc79ca3de208b69f9994dc89890c435f125788ff3b23f")
