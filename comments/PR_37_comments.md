# PR #37 – Comment Triage

- Repo: RunanywhereAI/RCLI
- PR Title: feat: one macOS rcli with MLX and the other backends
- PR URL: https://github.com/RunanywhereAI/RCLI/pull/37
- Total comments (including replies): 10 (9 review + 1 issue walkthrough)
- GitHub issues created: **none** (explicitly not requested)

## PR Description

Single macOS `rcli` product binary with Swift MLX host. NeuRT / QHexRT via private overlay. README is the Ollama-simple install-and-run doc.

---

## Section 1 – Quick & Easy Fixes

### QEF-1 – persist-credentials: false on read-only checkouts

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3846625905
- Author: coderabbitai
- File / location: `.github/workflows/ci.yml`, `.github/workflows/release.yml`
- LUS: 3 / CS: 1 / Type: other

**Original Comment:** Disable persisted GitHub credentials for all read-only checkouts.

**Status:** Fixed — `persist-credentials: false` on every `actions/checkout@v4` in `ci.yml` (3) and `release.yml` (4).

### QEF-2 – Escape backend names in grep -E

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3846625923
- Author: coderabbitai
- File / location: `scripts/assert-backends.sh:39`
- LUS: 3 / CS: 1 / Type: bug

**Status:** Fixed — names are escaped before interpolation into `grep -E`.

### QEF-3 – Include `${BUILD}/rcli.exe` in package candidates

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3846625935
- Author: coderabbitai
- File / location: `scripts/package-rcli.sh:39`
- LUS: 4 / CS: 1 / Type: bug

**Status:** Fixed — candidate list includes `${BUILD}/rcli.exe`.

### QEF-4 – Fail macOS package if MLX bundle is missing

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3846625943
- Author: coderabbitai
- File / location: `scripts/package-rcli.sh:56`
- LUS: 4 / CS: 1 / Type: bug

**Status:** Fixed — Darwin packaging exits if `mlx-swift_Cmlx.bundle` is absent.

### QEF-5 – Overlay DLL staging incremental-safe

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3847000420
- Author: coderabbitai
- File / location: `CMakeLists.txt:153`
- LUS: 3 / CS: 2 / Type: bug

**Status:** Fixed — configure-time `file(GLOB)` replaced with `cmake/copy-overlay-dlls.cmake` (GLOB at build time) + `DEPENDS` via POST_BUILD `cmake -P`.

### QEF-6 – VERBATIM on Windows add_custom_command

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3847000429
- Author: coderabbitai
- File / location: `CMakeLists.txt:155`
- LUS: 3 / CS: 1 / Type: nit

**Status:** Fixed — both Windows POST_BUILD copy commands use `VERBATIM`.

### QEF-7 – Check local overlay before token gate

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3847000438
- Author: coderabbitai
- File / location: `scripts/fetch-private-pack.sh:47`
- LUS: 4 / CS: 1 / Type: bug

**Status:** Fixed — local tarball (and `RCLI_PRIVATE_OVERLAY`) apply before any token check. Unused `TOKEN` gate removed (no remote download path exists).

### QEF-8 – Qualify `rcli serve` as macOS/Linux

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3847049713
- Author: coderabbitai
- File / location: `README.md:44`
- LUS: 3 / CS: 1 / Type: docs

**Status:** Fixed — getting-started example and commands table mark serve as macOS/Linux.

### QEF-9 – Document Apple MLX host build

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#discussion_r3847049720
- Author: coderabbitai
- File / location: `README.md:168`
- LUS: 4 / CS: 2 / Type: docs

**Status:** Fixed — generic CMake is labeled C++-only; Apple product path documents `RCLI_SDK_SWIFT_PATH` and `scripts/build-mlx.sh`.

### QEF-10 – Core ML is NeuRT (user)

- Source: user request on this PR
- File / location: `README.md` backends table + `src/commands/cmd_image.cpp`
- LUS: 5 / CS: 1 / Type: docs

**Original Comment:** Core ML is the same as NeuRT, not a separate backend. NeuRT should be able to do image gen; enable it if not.

**Status:** Fixed — Core ML row removed. NeuRT is ANE + Core ML. `sd15` is image gen via NeuRT. Command is already gated on `RCLI_HAS_NEURT` (overlay). Error string says NeuRT, not a fake Core ML backend.

### QEF-11 – CodeRabbit walkthrough (issue comment)

- Source comment: https://github.com/RunanywhereAI/RCLI/pull/37#issuecomment-5399923964
- Author: coderabbitai
- LUS: 1 / CS: 1 / Type: other

**Status:** Informational walkthrough + docstring-coverage warning. No code change (coverage threshold is a bot check, not a product defect). The “failed checks” listed in that walkthrough are the QEF items above.

---

## Section 2 – Larger / Structural Issues

None. User asked not to open GitHub issues. Nothing here needed a follow-up issue.

---

## Summary & Status

- Comments triaged: **10 / 10**
- Quick fixes identified: **10** (9 CodeRabbit + 1 user Core ML/NeuRT)
- Quick fixes applied: **10**
- Larger issues / GitHub issues: **0** (none opened)
- Remaining: consume SDK desktop-kit artifacts **after** runanywhere-sdks#776 is green, merged, and a kit-bearing SDK release exists; then bump `cmake/sdk-pin.cmake` and merge this PR so the tag/release workflow can fire.
