#!/usr/bin/env python3
"""Point Formula/wally.rb at a published release.

    stamp-formula.py VERSION PLATFORM=SHA256 [PLATFORM=SHA256 ...]

    stamp-formula.py 0.4.0 macos-arm64=abc... linux-x86_64=def...

Homebrew reads a tap's formula from the default branch rather than from the tag,
so a release alone cannot update what users install. The release workflow calls
this to produce a reviewed formula artifact; applying that artifact to the
canonical tap remains an explicit post-release step.

The formula carries one url/sha256 pair per platform inside on_macos/on_linux
blocks. Each pair is found by the platform slug in its url, and the sha256
updated is the first one following that url. Every platform named on the command
line must match exactly one url, and the version must appear exactly once, so a
formula that has grown a stanza this does not understand stops the release
rather than being half-rewritten.
"""

import re
import sys

FORMULA = "Formula/wally.rb"
# Repo stays RunanywhereAI/RCLI -- only the binary, formula and tap file are
# named wally.
RELEASES = "https://github.com/RunanywhereAI/RCLI/releases/download"

# The archive extension per platform, since Windows would be a zip if it were
# ever served by Homebrew. Adding a platform here and to the formula is all a
# new one needs.
SUFFIX = {
    "macos-arm64": "tar.gz",
    "macos-x86_64": "tar.gz",
    "linux-x86_64": "tar.gz",
    "linux-aarch64": "tar.gz",
}


def stamp_platform(source: str, version: str, platform: str, digest: str) -> str:
    """Rewrite the url naming `platform` and the sha256 line beneath it."""
    if platform not in SUFFIX:
        raise SystemExit(f"unknown platform {platform!r}; known: {', '.join(SUFFIX)}")
    asset = f"wally-{version}-{platform}.{SUFFIX[platform]}"
    url = f"{RELEASES}/v{version}/{asset}"

    # Any url mentioning this platform, whatever version it currently names.
    pattern = re.compile(
        rf'^(?P<indent>\s*)url\s+"[^"]*{re.escape(platform)}[^"]*"', re.M
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise SystemExit(
            f"{FORMULA}: expected exactly one url for {platform}, found {len(matches)}"
        )
    match = matches[0]
    source = (
        source[: match.start()]
        + f'{match.group("indent")}url "{url}"'
        + source[match.end() :]
    )

    # The sha256 belonging to that url is the next one after it. Anchoring to
    # the url rather than to the block keeps this working whether the platform
    # is wrapped in on_arm, on_intel or nothing at all.
    tail_at = source.index(url) + len(url)
    sha = re.compile(r'^(?P<indent>\s*)sha256\s+"[0-9a-f]*"', re.M)
    found = sha.search(source, tail_at)
    if found is None:
        raise SystemExit(f"{FORMULA}: no sha256 line follows the {platform} url")
    return (
        source[: found.start()]
        + f'{found.group("indent")}sha256 "{digest}"'
        + source[found.end() :]
    )


def stamp(source: str, version: str, digests: dict) -> str:
    for platform, digest in digests.items():
        source = stamp_platform(source, version, platform, digest)
    source, count = re.subn(
        r'^(\s*version\s+")[^"]*(")', rf"\g<1>{version}\g<2>", source, flags=re.M
    )
    if count != 1:
        raise SystemExit(f"{FORMULA}: expected exactly one version line, found {count}")
    return source


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    version = sys.argv[1]
    digests = {}
    for pair in sys.argv[2:]:
        if "=" not in pair:
            raise SystemExit(f"expected PLATFORM=SHA256, got {pair!r}")
        platform, digest = pair.split("=", 1)
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise SystemExit(f"not a sha256 digest for {platform}: {digest!r}")
        digests[platform] = digest

    with open(FORMULA) as handle:
        source = handle.read()
    # Rewritten in full before the file is opened for writing. Opening it
    # truncates, so validating inside the write block leaves an empty formula on
    # main every time a check fires, which is worse than the stale sha256 this
    # script exists to prevent.
    stamped = stamp(source, version, digests)
    with open(FORMULA, "w") as handle:
        handle.write(stamped)
    for platform, digest in digests.items():
        print(f"{FORMULA}: v{version} {platform} {digest[:12]}")


if __name__ == "__main__":
    main()
