# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
set(RCLI_PINNED_SDK_VERSION "0.20.29")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "592b8f91b4c159c19ca605ae96ded5a8ec00ace3ea44a28086c0bb27013c2e64")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "df4e4fc17c9df4bdf1c0ff645abc66dd4e8f4468cedf5e75799340590fb923d0")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "5961aef90d968045d8f7cea1baab1ea6adce3bd6961d4a5c16a5d507d2960d5b")
