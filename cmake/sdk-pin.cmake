# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
set(RCLI_PINNED_SDK_VERSION "0.20.28")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "3ab234c2d4f485eae46989c3ca24eb954b5f02d517f020ccba150251346f4281")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "d073ddee925d339bb27ca2257441c8429ed65095b7fcc46b83b3b7548149d56b")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "efe87166e2c25160ac5e9b71decdf76c7637e492b96572a060b7b53f12970167")
