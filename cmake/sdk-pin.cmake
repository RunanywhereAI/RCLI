# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
set(RCLI_PINNED_SDK_VERSION "0.20.25")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "c02a0df3e70ea551383dcfb738e2e7f77c25740ba2773bf6891c32eb51ba21a7")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "fa006b23e0f06511addd31bd7d4a9f99576561e3e9593dbe9be79c8497ae2a12")
