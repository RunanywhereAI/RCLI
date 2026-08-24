---
name: rcli-release
description: Cut an RCLI product release (independent of SDK version) — version bump, release:patch label, merge, auto-tag, bottles. Use when shipping rcli after a published SDK kit, or when Homebrew / notarization / private overlays must not leak into public bottles.
---

# RCLI release

Repo: `RunanywhereAI/RCLI`. Product version is `project(rcli VERSION x.y.z)` in
`CMakeLists.txt` (also stamped into `Formula/rcli.rb`). Independent of the SDK
kit pin.

Do this **after** the SDK GitHub Release this pin targets is **published**
(not draft). Companion: **rcli-kit-pin**, then this skill.

## 0. Facts

- Label form is `release:patch` / `release:minor` / `release:major` (**colon**).
  `.github/workflows/auto-tag.yml` matches that spelling only.
- Merge to `main` with that label → auto-tag pushes `v<CMakeLists VERSION>` and
  dispatches `release.yml`. Tag pushes with `GITHUB_TOKEN` do **not** trigger
  other workflows; the dispatch is required.
- Checkout in auto-tag uses `persist-credentials: true` so the tag push works.
- `release.yml` jobs: macos-arm64 bottle, windows-x64 zip, then GitHub Release.
  **macos runner must be macos-26** (Xcode 26 / Swift 6.2). macos-15 is Swift
  6.1 and cannot resolve the SDK package; macos-14 is Swift 5.10.
- Public bottle / zip is OSS only. NeuRT / QHexRT overlays are workflow
  artifacts or `RCLI_PRIVATE_OVERLAY`, never public release assets, never
  Homebrew bottles.
- Linux bottles are not a v1 merge blocker.

## 1. Pin the SDK kit first

If this train needs new engines (sherpa routable, windows-arm64 kit, NeuRT
image gen): bump `cmake/sdk-pin.cmake` + CI `ref: v$SDK` in the **same PR**
(**rcli-kit-pin**). CI must be green on that pin before you bump the product
version.

## 2. Product version + label

Next patch after `0.5.0` is `0.5.1`. Bump `CMakeLists.txt` `project(rcli
VERSION …)` and any Formula version stamp in the same commit. Open/update the
PR and apply **exactly one** `release:patch` (or minor/major).

`gh pr create --label release:patch` is not atomic with the `opened` event —
verify the label landed (`gh pr view --json labels`).

## 3. CI bar before merge

Required: macOS product e2e (`scripts/e2e.sh ./build/rcli`) and Windows e2e
against the pinned kit. `agents-sync.yml` must pass
(`scripts/ci/check-agents-sync.sh`).

Re-check immediately before merge (a rollup of SUCCESS can hide checks that
have not started):

```bash
gh pr checks <pr> --repo RunanywhereAI/RCLI --json name,state,bucket \
  --jq '[.[] | select(.state == null or .state == "")] | length'   # must be 0
```

Do not merge on a partial matrix. Confirm bypass eligibility the same way as
the SDK (`bypass_pull_request_allowances`) before `--admin`.

## 4. Merge triggers the release

```bash
gh pr merge <pr> --repo RunanywhereAI/RCLI --squash --admin   # only if allow-listed
```

`auto-tag.yml` tags `v$PRODUCT` and dispatches `release.yml`. Unlike the SDK
train, there is typically **no** pre-merge candidate to reuse — let this
release.yml run. Watch it; do not cancel `macos` for being "slow" (MLX host
link + e2e).

## 5. Verify the GitHub Release

```bash
gh release view v$PRODUCT --repo RunanywhereAI/RCLI --json isDraft,assets \
  --jq '{isDraft, names: [.assets[].name]}'
```

Expect `rcli-$PRODUCT-macos-arm64.tar.gz` (+ `.sha256`) and
`rcli-$PRODUCT-windows-x64.zip` (+ `.sha256`). **Must be empty** for
`*neurt*`, `*qhexrt*`, `*private*`.

If `release.yml` still creates a non-draft release, that is the live product
cut — confirm asset names before anyone bottles from it. Stamp Formula from
the macOS sidecar (`scripts/stamp-formula.py`) when that path is wired.

## 6. Overlays after the public cut

Private packs stay off the public release. For a machine that should load
NeuRT / QHexRT:

```bash
export RCLI_PRIVATE_OVERLAY=/path/to/RunAnywhere-cpp-desktop-macos-arm64-neurt-private-v$SDK.tar.gz
# or windows-arm64-qhexrt
```

`RCLI_REQUIRE_PRIVATE=1` fails closed when the overlay is missing. Default CI
must pass without it.

## Do not

- Attach `rcli-*` assets onto an SDK GitHub Release.
- Pin `fetch-kit.sh` at a draft SDK tag.
- Ship NeuRT / QHexRT inside the public bottle "just this once".
- Use macos-14 or macos-15 for the Apple job (need macos-26 / Swift 6.2).
- Weaken `assert-backends.sh` when `HAS_SHERPA`/`HAS_ONNX` is TRUE.
