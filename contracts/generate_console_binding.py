#!/usr/bin/env python3
"""Generate the typed C++ binding for the CLI's control-plane HTTP calls.

The source of truth is the pinned OpenAPI extract `wally-cli-v1.openapi.json`
(itself carved from InferenceInfra's `control-plane-v1.openapi.json`). This
reads that artifact and emits `src/account/console_contract.h`: an enum class
per string enum, a struct per object schema, and nlohmann to/from-json for each,
so `console.cpp` never hand-builds a request body or parses a response field by
name. A `kContractSha256` constant pins the exact artifact the header was built
from; `test_wally_contract` fails the build if the two drift.

    python3 contracts/generate_console_binding.py            # write the header
    python3 contracts/generate_console_binding.py --check    # fail if stale

Run it and commit the header whenever the pinned contract changes. Never edit
the header by hand.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CONTRACT = ROOT / "wally-cli-v1.openapi.json"
HEADER = ROOT.parent / "src" / "account" / "console_contract.h"

INT = "std::int64_t"


def _snake_to_pascal(name: str) -> str:
    return "".join(part.capitalize() for part in name.replace("-", "_").split("_"))


def _enum_constant(value: str) -> str:
    # "24h" -> k24H, "claude_code" -> kClaudeCode. A digit-leading value is
    # still a legal identifier once prefixed with k.
    return "k" + _snake_to_pascal(value)


def _resolve_type(schema: dict, schemas: dict) -> tuple[str, bool]:
    """Return (c++ type, is_optional). Nullable/anyOf-null collapses to optional."""
    if "$ref" in schema:
        name = schema["$ref"].split("/")[-1]
        target = schemas.get(name, {})
        # A constrained/plain string newtype (type "string", not an enum, not an
        # object) has no emitted type of its own -- only objects and enums get
        # one -- so inline it as std::string rather than name an undefined type.
        if target.get("type") == "string" and "enum" not in target:
            return "std::string", False
        return name, False
    if "const" in schema:
        # A fixed literal (e.g. `object: {const: "model"}`). Typed by its value;
        # the reader still parses it, it just can only be that one value.
        const = schema["const"]
        if isinstance(const, bool):
            return "bool", False
        if isinstance(const, int):
            return INT, False
        return "std::string", False
    if "anyOf" in schema:
        branches = [b for b in schema["anyOf"] if b.get("type") != "null"]
        had_null = any(b.get("type") == "null" for b in schema["anyOf"])
        inner, _ = _resolve_type(branches[0], schemas)
        return inner, had_null
    kind = schema.get("type")
    # JSON Schema nullable form `type: ["integer", "null"]`: strip the null,
    # resolve the remaining type, and mark it optional. Same meaning as an
    # anyOf-with-null, just spelled the compact way OpenAPI 3.1 emits.
    if isinstance(kind, list):
        non_null = [t for t in kind if t != "null"]
        had_null = "null" in kind
        inner, _ = _resolve_type({**schema, "type": non_null[0]}, schemas)
        return inner, had_null
    if kind == "string":
        return "std::string", False
    if kind == "integer":
        return INT, False
    if kind == "boolean":
        return "bool", False
    if kind == "array":
        item, _ = _resolve_type(schema["items"], schemas)
        return f"std::vector<{item}>", False
    raise SystemExit(f"unsupported schema shape: {schema}")


def _is_enum(schema: dict) -> bool:
    return "enum" in schema and schema.get("type") == "string"


def _emit_enum(name: str, schema: dict) -> str:
    values = schema["enum"]
    lines = [f"enum class {name} {{"]
    lines += [f"    {_enum_constant(v)}," for v in values]
    lines.append("};")
    lines.append("")
    # from_json: reject an unknown value rather than silently defaulting.
    lines.append(f"inline void from_json(const nlohmann::json& j, {name}& value) {{")
    lines.append("    const std::string raw = j.get<std::string>();")
    for v in values:
        lines.append(f'    if (raw == "{v}") {{ value = {name}::{_enum_constant(v)}; return; }}')
    lines.append(
        f'    throw nlohmann::json::type_error::create(302, "unknown {name}: " + raw, &j);'
    )
    lines.append("}")
    lines.append("")
    lines.append(f"inline void to_json(nlohmann::json& j, const {name}& value) {{")
    lines.append("    switch (value) {")
    for v in values:
        lines.append(f'        case {name}::{_enum_constant(v)}: j = "{v}"; return;')
    lines.append("    }")
    lines.append("}")
    return "\n".join(lines)


def _emit_struct(name: str, schema: dict, schemas: dict) -> str:
    required = set(schema.get("required", []))
    props = schema.get("properties", {})
    fields = []
    for prop, pschema in props.items():
        base, nullable = _resolve_type(pschema, schemas)
        optional = nullable or prop not in required
        cpp = f"std::optional<{base}>" if optional else base
        fields.append((prop, cpp, base, optional))

    lines = [f"struct {name} {{"]
    for prop, cpp, _base, _opt in fields:
        lines.append(f"    {cpp} {prop};")
    lines.append("};")
    lines.append("")

    # from_json is a tolerant reader: a missing or null field defaults rather
    # than throwing, so a server that predates a field this build knows about
    # still parses. A present field is strictly typed -- a wrong type or an
    # unknown enum value is still an error. Requests never go through here (they
    # are built in code), so only responses feel the tolerance, which is the
    # right posture for a client that deploys independently of the server. A
    # value-initialized scoped enum is its first member, a fine default.
    lines.append(f"inline void from_json(const nlohmann::json& j, {name}& value) {{")
    for prop, _cpp, base, optional in fields:
        lines.append(f'    if (j.contains("{prop}") && !j.at("{prop}").is_null()) {{')
        if optional:
            lines.append(f'        value.{prop} = j.at("{prop}").get<{base}>();')
            lines.append("    } else {")
            lines.append(f"        value.{prop} = std::nullopt;")
        else:
            lines.append(f'        value.{prop} = j.at("{prop}").get<{base}>();')
            lines.append("    } else {")
            lines.append(f"        value.{prop} = {base}{{}};")
        lines.append("    }")
    lines.append("}")
    lines.append("")

    # to_json: emit required fields always, optionals only when set.
    lines.append(f"inline void to_json(nlohmann::json& j, const {name}& value) {{")
    lines.append("    j = nlohmann::json::object();")
    for prop, _cpp, _base, optional in fields:
        if optional:
            lines.append(f"    if (value.{prop}.has_value()) {{")
            lines.append(f'        j["{prop}"] = *value.{prop};')
            lines.append("    }")
        else:
            lines.append(f'    j["{prop}"] = value.{prop};')
    lines.append("}")
    return "\n".join(lines)


def render() -> str:
    raw = CONTRACT.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    document = json.loads(raw)
    schemas = document["components"]["schemas"]

    enums = [n for n in sorted(schemas) if _is_enum(schemas[n])]
    objects = [
        n for n in sorted(schemas) if schemas[n].get("type") == "object" and not _is_enum(schemas[n])
    ]

    # Objects in dependency order: a struct must be declared after the structs
    # it names. A topological sort over $ref edges between object schemas.
    ordered: list[str] = []
    visiting: set[str] = set()

    def refs(name: str) -> list[str]:
        found: list[str] = []

        def walk(v: object) -> None:
            if isinstance(v, dict):
                if "$ref" in v:
                    found.append(v["$ref"].split("/")[-1])
                for x in v.values():
                    walk(x)
            elif isinstance(v, list):
                for x in v:
                    walk(x)

        walk(schemas[name])
        return found

    def visit(name: str) -> None:
        if name in ordered or name not in objects:
            return
        visiting.add(name)
        for dependency in refs(name):
            if dependency in objects and dependency not in visiting:
                visit(dependency)
        visiting.discard(name)
        if name not in ordered:
            ordered.append(name)

    for name in objects:
        visit(name)

    out = [
        "// Generated by contracts/generate_console_binding.py from",
        "// contracts/wally-cli-v1.openapi.json. DO NOT EDIT.",
        "//",
        "// Typed request and response models for the CLI's control-plane calls, so",
        "// console.cpp neither builds a request body by hand nor reads a response",
        "// field by name. Regenerate and commit whenever the pinned contract moves.",
        "#ifndef WALLY_ACCOUNT_CONSOLE_CONTRACT_H",
        "#define WALLY_ACCOUNT_CONSOLE_CONTRACT_H",
        "",
        "#include <cstdint>",
        "#include <optional>",
        "#include <string>",
        "#include <vector>",
        "",
        "#include <nlohmann/json.hpp>",
        "",
        "namespace wally::account::contract {",
        "",
        "// SHA-256 of contracts/wally-cli-v1.openapi.json this header was built from.",
        f'inline constexpr char kContractSha256[] = "{digest}";',
        "",
    ]
    for name in enums:
        out.append(_emit_enum(name, schemas[name]))
        out.append("")
    for name in ordered:
        out.append(_emit_struct(name, schemas[name], schemas))
        out.append("")
    out.append("}  // namespace wally::account::contract")
    out.append("")
    out.append("#endif  // WALLY_ACCOUNT_CONSOLE_CONTRACT_H")
    out.append("")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the header is stale")
    args = parser.parse_args()
    rendered = render()
    if args.check:
        current = HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""
        if current != rendered:
            sys.stderr.write(
                "console_contract.h is stale. Run:\n"
                "  python3 contracts/generate_console_binding.py\n"
                "and commit the result.\n"
            )
            sys.exit(1)
        print("console_contract.h matches the pinned contract")
        return
    HEADER.write_text(rendered, encoding="utf-8")
    print(f"wrote {HEADER}")


if __name__ == "__main__":
    main()
