# Pinned SDK kit this RCLI tree is validated against.
# Bump together with the SHA-256 sidecars after an SDK release.
#
# The IDL pins are copied from the kit's share/runanywhere/SCHEMA_LOCK (which
# is idl/SCHEMA_LOCK from the SDK). RCLI never runs protoc — a mismatch here
# means consume a new kit and update this file, not regenerate headers.
#
# NOTE for 0.20.32: these stay at 1.1.0 / 377868bf on purpose. The published kit
# carries those values because release.yml published via `publish_from_run_id`,
# reusing artifacts built before the monorepo bumped idl/VERSION to 1.1.1 for a
# COMMENT-ONLY edit. Stripping comments from both revisions of the only changed
# .proto leaves them byte-identical, so no field, wire value or generated symbol
# differs — the digest moved because the lock hashes doc text too. Pinning what
# the kit ACTUALLY carries is the honest value; pinning 1.1.1 here would fail
# fetch-kit.sh against the very kit it is meant to validate.
set(RCLI_PINNED_SDK_VERSION "0.20.32")
set(RCLI_PINNED_IDL_VERSION "1.1.0")
set(RCLI_PINNED_IDL_SCHEMA_SHA256 "377868bf8e12b327c32978215cafb36c2b1b3157a9be05c7960ac474d55231d2")
set(RCLI_PINNED_IDL_PROTOC_VERSION "35.1")
set(RCLI_PINNED_KIT_SHA256_MACOS_ARM64 "e8eaaf0af9177ad1f97b36d88bc3d10130b8858967370943c9b58452934e68ef")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_X64 "96d96343d7a99fd18df7533b85c864b81dc8ee669b09691d87e707319cae6789")
set(RCLI_PINNED_KIT_SHA256_WINDOWS_ARM64 "2d753cad5df0d9cfc9d1a5e15b1f41769ba8c30a277a4e291a6a219efbf66700")
