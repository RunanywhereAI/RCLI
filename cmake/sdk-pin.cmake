# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
set(RCLI_PINNED_SDK_VERSION "0.20.30")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "025884f4c1e4f54dccd2196c5534fde4b45e22bfd6ae1bc00a6270a8f7275136")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "217263ee047531db4c3f584bf1157602401a996f0d83bbddc377f3f97e8ca629")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "5b89a95618498debf7619cb21b62f854d487eda6cce3fe6ec2c2e05bbe4d829c")
