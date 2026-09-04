# RunAnywhere CLI — Releasing

Contributor and release-engineering guide for shipping `rcli` binaries.

## Control plane

Two SDK environments:

| Who | Flags | Auth | Backend |
|---|---|---|---|
| OSS / no key | `--environment development` (default) | Keyless | Baked staging backend → PUBLIC org |
| Team testing | `--environment production --base-url <https://…> --api-key $KEY` | JWT | Your team backend |
| Customers | `--environment production --api-key $KEY` | JWT | Production backend |

| Flag | Env var | Meaning |
|---|---|---|
| `--environment <development\|production>` | `RUNANYWHERE_ENVIRONMENT` | `development` (default) = keyless OSS telemetry. `production` = API key + https. |
| `--base-url <url>` | `RUNANYWHERE_BASE_URL` | Optional in development (baked staging URL in release builds). Required https for production. |
| `--api-key <key>` | `RUNANYWHERE_API_KEY` | Required for production (≥ 10 chars). Omit for keyless development. |

```console
# OSS keyless blast → staging backend (PUBLIC org)
# Unset ambient RUNANYWHERE_API_KEY or an invalid key will force a failed JWT login.
$ unset RUNANYWHERE_API_KEY RUNANYWHERE_BASE_URL
$ rcli --environment development \
    --base-url "$STAGING_BASE_URL" \
    telemetry blast --processing-ms 42.5
# Release builds can omit --base-url (baked STAGING_BASE_URL).
# CI gate (path is relative to the repo root):
#   STAGING_BASE_URL=… ./scripts/ci/oss_keyless_telemetry_blast.sh

# Team / customer authed path
$ rcli --environment production \
    --base-url https://api.example.com \
    --api-key $KEY auth login

$ rcli --environment production \
    --base-url https://api.example.com \
    --api-key $KEY telemetry blast
MODALITY      RESULT    STATUS      RECEIVED    STORED    SKIPPED
llm           ok        HTTP 200    1           1         0
…                                            (one row per modality, 12 total)
```

- `auth login` runs the authenticated handshake (`/api/v1/auth/sdk/authenticate` → `/api/v1/devices/register` → model assignments). Production only.
- `telemetry emit|blast` drive the real commons telemetry pipeline to `/api/v2/sdk/telemetry/{modality}`. Development is keyless (no JWT). Production logs in first. Modalities: `llm stt tts vlm rag imagegen embeddings vad voice lora model system`. Exit is non-zero when any POST fails or any tracked event never reached the backend.

## Published asset contract

The current `release.yml` publishes two archives and matching SHA-256
sidecars. It does not build or advertise a Linux release:

| Platform | Asset | Required archive root |
|---|---|---|
| macOS Apple Silicon | `rcli-X.Y.Z-macos-arm64.tar.gz` | `rcli-macos-arm64/` |
| Windows x86_64 | `rcli-X.Y.Z-windows-x86_64.zip` | `rcli-windows-x86_64/` |

Each root contains a non-empty `README.md` and `bin/rcli` or `bin/rcli.exe`.
Windows DLLs stay beside `bin/rcli.exe`. The macOS archive contains the Swift
MLX host and its resource bundles. `scripts/verify-release-assets.py` verifies
the sidecar digest, filename, single-root layout, required files, executable
mode, duplicate paths, traversal, links, and expansion limits. Packaging jobs
and the publish job all run it before a release is created.

## Signing reality and production gates

Credential-free macOS packaging is ad-hoc signed. `scripts/package-rcli.sh`
checks that signature and can sign with an already-installed Developer ID
identity via `RCLI_CODESIGN_IDENTITY` and optional `RCLI_CODESIGN_KEYCHAIN`.
Set `RCLI_REQUIRE_DEVELOPER_ID=1` to make ad-hoc signing an error. The GitHub
workflow does **not** currently import an identity, notarize an archive, create
a DMG, or staple a ticket.

The Windows workflow currently packages an unsigned executable. It does not
import a PFX, Authenticode-sign, or validate a certificate chain.

Therefore these are launch gates, not completed workflow features:

- import a Developer ID Application identity into an ephemeral keychain, sign
  nested code and the host with hardened runtime/timestamp, notarize the exact
  distributed artifact, and validate Gatekeeper acceptance;
- Authenticode-sign `rcli.exe` and DLLs as required, then validate signatures on
  a clean Windows host;
- provide the signing/notarization credentials through protected release
  environments and keep pull-request jobs credential-free.

Do not describe an asset as notarized or Authenticode-signed until the workflow
and the downloaded release prove it. Useful post-download checks are
`codesign -dv --verbose=4`, `codesign --verify --strict`, and `spctl --assess`
on macOS, and `Get-AuthenticodeSignature` on Windows.

## CI and release workflow

- `ci.yml` builds and tests macOS and Windows. Its distribution job also tests
  archive verification, shell syntax, formula syntax, and stamping code.
- `release.yml` builds macOS and Windows, runs product e2e, packages, verifies
  each archive twice, then publishes the two archives and sidecars.
- The publish job generates a `rcli-homebrew-formula` workflow artifact from
  the verified macOS checksum. It does not pretend that an ephemeral checkout
  updated a default branch.

Homebrew still needs one ownership decision: `install.sh` taps the RCLI repo as
`runanywhereai/rcli`, while the historical update script targeted a separate
`homebrew-tap` repo. Until one is declared canonical, pass `RCLI_TAP_REPO`
explicitly to `scripts/update-tap.sh` and apply the generated formula to the
same tap users install from.
