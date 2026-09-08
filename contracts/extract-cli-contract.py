#!/usr/bin/env python3
"""Carve the CLI-facing slice out of the full control-plane contract.

The CLI uses six of the control plane's operations. Rather than vendor the whole
8000-line `control-plane-v1.openapi.json`, this extracts those operations and the
transitive closure of the schemas they reference into a self-contained, valid
OpenAPI document, `wally-cli-v1.openapi.json`, which is what gets pinned and fed
to `generate_console_binding.py`.

    python3 contracts/extract-cli-contract.py \\
        ../RA-Cloud-WorkSpace/InferenceInfra/contracts/control-plane-v1.openapi.json

Run this only when re-vendoring after the upstream contract changes; then run
generate_console_binding.py and commit both outputs together.
"""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "wally-cli-v1.openapi.json"

CLI_OPERATION_IDS = {
    "startCliAuthorization",
    "pollCliAuthorization",
    "refreshCliAuthorization",
    "revokeCliAuthorization",
    "getCurrentIdentity",
    "getCliUsage",
}
HTTP_METHODS = {"get", "post", "put", "delete", "patch"}


def _refs(value: object, out: set[str]) -> None:
    if isinstance(value, dict):
        if "$ref" in value:
            out.add(value["$ref"].split("/")[-1])
        for child in value.values():
            _refs(child, out)
    elif isinstance(value, list):
        for child in value:
            _refs(child, out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="path to control-plane-v1.openapi.json")
    args = parser.parse_args()

    source = json.loads(args.source.read_text(encoding="utf-8"))
    schemas = source["components"]["schemas"]

    closure: set[str] = set()

    def visit(name: str) -> None:
        if name in closure or name not in schemas:
            return
        closure.add(name)
        found: set[str] = set()
        _refs(schemas[name], found)
        for dependency in found:
            visit(dependency)

    paths: "collections.OrderedDict[str, dict]" = collections.OrderedDict()
    for path, item in source["paths"].items():
        kept = {}
        for method, operation in item.items():
            if method in HTTP_METHODS and operation.get("operationId") in CLI_OPERATION_IDS:
                kept[method] = operation
                found: set[str] = set()
                _refs(operation, found)
                for dependency in found:
                    visit(dependency)
        if kept:
            paths[path] = kept

    extract = {
        "openapi": source["openapi"],
        "info": {
            "title": "Wally CLI control-plane contract (extract)",
            "version": source["info"]["version"],
            "description": (
                "CLI-facing operations extracted from control-plane-v1.openapi.json by "
                "contracts/extract-cli-contract.py. Do not hand-edit."
            ),
        },
        "paths": paths,
        "components": {"schemas": {name: schemas[name] for name in sorted(closure)}},
    }
    OUT.write_text(json.dumps(extract, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    operations = sum(len(methods) for methods in paths.values())
    print(f"wrote {OUT}: {operations} operations, {len(closure)} schemas")


if __name__ == "__main__":
    main()
