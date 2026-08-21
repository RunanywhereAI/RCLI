#!/usr/bin/env python3
"""Point Formula/rcli.rb at a published release.

    stamp-formula.py VERSION ASSET_NAME SHA256

Homebrew reads a tap's formula from the default branch rather than from the tag,
so the formula on main has to name the tarball a release just published. This
was a manual step and the sha256 went stale as a matter of course; the release
workflow calls this instead.

Every field must match exactly once. A formula that has grown a second url or
sha256 line (per-arch blocks, a HEAD stanza) needs a deliberate decision about
which one to stamp, so this stops rather than guessing.
"""

import re
import sys

FORMULA = "Formula/rcli.rb"
RELEASES = "https://github.com/RunanywhereAI/RCLI/releases/download"


def stamp(source: str, version: str, asset: str, digest: str) -> str:
    fields = {
        "url": f"{RELEASES}/v{version}/{asset}",
        "sha256": digest,
        "version": version,
    }
    counts = {}
    for field, value in fields.items():
        source, counts[field] = re.subn(
            rf'^(\s*{field}\s+")[^"]*(")',
            lambda m: m.group(1) + value + m.group(2),
            source,
            flags=re.M,
        )
    wrong = {f: n for f, n in counts.items() if n != 1}
    if wrong:
        raise SystemExit(
            f"{FORMULA}: expected exactly one line per field, matched {wrong}"
        )
    return source


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    version, asset, digest = sys.argv[1:4]
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise SystemExit(f"not a sha256 digest: {digest!r}")
    with open(FORMULA) as handle:
        source = handle.read()
    with open(FORMULA, "w") as handle:
        handle.write(stamp(source, version, asset, digest))
    print(f"{FORMULA}: v{version} {asset} {digest[:12]}")


if __name__ == "__main__":
    main()
