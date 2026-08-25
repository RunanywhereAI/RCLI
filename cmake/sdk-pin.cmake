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
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "d3e70605c87357b5c04b518ea99c356bf25ccaca11776178b9a20fcc7c298bd0")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "e7e432aefdc16b80598982fc83d06b2178de6ee18dd194734efee9204e9f2dd2")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "c96f2c848dc76e49a91f5483b7df3906fdc1b72f92944fb25f32890490596a76")
