#!/usr/bin/env python3
"""Validate and embed projected codec documentation for xahau-codec.

The build-time script deliberately depends only on the Python standard
library. C++ extraction is delegated to the documented `projected-source`
command, which owns the tree-sitter dependency and fails the build when a
declared topic can no longer render.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TEMPLATES_DIR = Path(__file__).resolve().parent / "templates"

# Every value advertised by xahau-codec's --codec-type option must have a
# projected-source topic. Supporting topics may exist in addition to these.
REQUIRED_CODEC_TOPICS = {
    "amount",
    "bridge",
    "currency",
    "iou-value",
    "issue",
    "ledger-entry-type",
    "number",
    "pathset",
    "quality",
    "stobject",
    "transaction-type",
    "vector256",
}

# Class/struct members cited by the documentation mapping tables. This catches
# renamed or removed implementation concepts that prose-only rows would not
# otherwise validate through a code() extraction.
MEMBER_REGISTRY = [
    ("include/xrpl/protocol/STAmount.h", "STAmount", "mAsset"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "mValue"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "mOffset"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "mIsNegative"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "set"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "add"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "canonicalize"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cIssuedCurrency"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cPositive"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMPToken"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cValueMask"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMinValue"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMaxValue"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMinOffset"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMaxOffset"),
    ("include/xrpl/protocol/STAmount.h", "STAmount", "cMaxNativeN"),
    ("include/xrpl/protocol/Issue.h", "Issue", "currency"),
    ("include/xrpl/protocol/Issue.h", "Issue", "account"),
    ("include/xrpl/protocol/STObject.h", "STObject", "v_"),
    ("include/xrpl/protocol/STObject.h", "STObject", "getSortedFields"),
    ("include/xrpl/protocol/STObject.h", "STObject", "set"),
    ("include/xrpl/protocol/STObject.h", "STObject", "add"),
    ("include/xrpl/protocol/Serializer.h", "Serializer", "addFieldID"),
    ("include/xrpl/protocol/Serializer.h", "Serializer", "addVL"),
    ("include/xrpl/protocol/Serializer.h", "Serializer", "encodeLengthLength"),
    ("include/xrpl/protocol/Serializer.h", "SerialIter", "getFieldID"),
    ("include/xrpl/protocol/Serializer.h", "SerialIter", "getVLDataLength"),
    ("include/xrpl/protocol/STCurrency.h", "STCurrency", "currency_"),
    ("include/xrpl/protocol/STCurrency.h", "STCurrency", "add"),
    ("include/xrpl/protocol/STAccount.h", "STAccount", "value_"),
    ("include/xrpl/protocol/STAccount.h", "STAccount", "add"),
    ("include/xrpl/protocol/STIssue.h", "STIssue", "asset_"),
    ("include/xrpl/protocol/STIssue.h", "STIssue", "add"),
    ("include/xrpl/protocol/MPTIssue.h", "MPTIssue", "mptID_"),
    ("include/xrpl/protocol/MPTIssue.h", "MPTIssue", "getIssuer"),
    ("include/xrpl/protocol/STNumber.h", "STNumber", "value_"),
    ("include/xrpl/protocol/STNumber.h", "STNumber", "add"),
    ("include/xrpl/basics/Number.h", "Number", "mantissa_"),
    ("include/xrpl/basics/Number.h", "Number", "exponent_"),
    ("include/xrpl/basics/Number.h", "Number", "normalize"),
    (
        "include/xrpl/protocol/STXChainBridge.h",
        "STXChainBridge",
        "lockingChainDoor_",
    ),
    (
        "include/xrpl/protocol/STXChainBridge.h",
        "STXChainBridge",
        "lockingChainIssue_",
    ),
    (
        "include/xrpl/protocol/STXChainBridge.h",
        "STXChainBridge",
        "issuingChainDoor_",
    ),
    (
        "include/xrpl/protocol/STXChainBridge.h",
        "STXChainBridge",
        "issuingChainIssue_",
    ),
    ("include/xrpl/protocol/STXChainBridge.h", "STXChainBridge", "add"),
    ("include/xrpl/protocol/STPathSet.h", "STPathElement", "typeNone"),
    ("include/xrpl/protocol/STPathSet.h", "STPathElement", "typeBoundary"),
    ("include/xrpl/protocol/STPathSet.h", "STPathElement", "getNodeType"),
    ("include/xrpl/protocol/STPathSet.h", "STPathSet", "value"),
    ("include/xrpl/protocol/STPathSet.h", "STPathSet", "add"),
    ("include/xrpl/protocol/STVector256.h", "STVector256", "mValue"),
    ("include/xrpl/protocol/STVector256.h", "STVector256", "add"),
    ("include/xrpl/protocol/Quality.h", "Quality", "m_value"),
    ("include/xrpl/protocol/Quality.h", "Quality", "rate"),
    ("include/xrpl/protocol/KnownFormats.h", "KnownFormats", "findTypeByName"),
    ("include/xrpl/protocol/KnownFormats.h", "KnownFormats", "findByType"),
    ("include/xrpl/protocol/LedgerFormats.h", "LedgerFormats", "getInstance"),
    ("include/xrpl/protocol/TxFormats.h", "TxFormats", "getInstance"),
]

# Exhaustive nested keys emitted inside debug-json's `fields` value.
EMITTED_JSON_KEYS = {
    "name",
    "header",
    "vl",
    "value",
    "fields",
    "elements",
    "end_marker",
    "parts",
    "parts.type",
    "parts.drops",
    "parts.mantissa",
    "parts.exponent",
    "parts.negative",
    "parts.currency",
    "parts.issuer",
    "parts.zero",
    "parts.value",
    "parts.mpt_id",
}


def _blank_non_code(source: str) -> str:
    """Blank comments and literals while preserving offsets and newlines."""
    masked = list(source)
    size = len(source)
    i = 0

    def blank(start: int, end: int) -> None:
        for pos in range(start, min(end, size)):
            if masked[pos] != "\n":
                masked[pos] = " "

    while i < size:
        if source.startswith("//", i):
            end = source.find("\n", i + 2)
            end = size if end < 0 else end
            blank(i, end)
            i = end
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            end = size if end < 0 else end + 2
            blank(i, end)
            i = end
        elif source.startswith('R"', i):
            open_paren = source.find("(", i + 2, min(size, i + 20))
            if open_paren < 0:
                i += 2
                continue
            delimiter = source[i + 2 : open_paren]
            terminator = ")" + delimiter + '"'
            end = source.find(terminator, open_paren + 1)
            end = size if end < 0 else end + len(terminator)
            blank(i, end)
            i = end
        elif (
            source[i] == "'"
            and i > 0
            and i + 1 < size
            and source[i - 1] in "0123456789abcdefABCDEF"
            and source[i + 1] in "0123456789abcdefABCDEF"
        ):
            # C++ digit separator, not the start of a character literal.
            i += 1
        elif source[i] in {'"', "'"}:
            quote = source[i]
            start = i
            i += 1
            while i < size:
                if source[i] == "\\":
                    i += 2
                    continue
                if source[i] == quote:
                    i += 1
                    break
                i += 1
            blank(start, i)
        else:
            i += 1
    return "".join(masked)


def _class_body(source: str, class_name: str) -> str | None:
    masked = _blank_non_code(source)
    pattern = re.compile(
        rf"\b(?:class|struct)\s+(?:(?:[A-Za-z_]\w*|::)\s+)*"
        rf"{re.escape(class_name)}\b[^;{{]*{{",
        re.MULTILINE,
    )
    for match in pattern.finditer(masked):
        open_brace = masked.find("{", match.start(), match.end())
        depth = 0
        for pos in range(open_brace, len(masked)):
            if masked[pos] == "{":
                depth += 1
            elif masked[pos] == "}":
                depth -= 1
                if depth == 0:
                    return masked[open_brace + 1 : pos]
    return None


def verify_documented_symbols() -> None:
    header_cache: dict[str, str] = {}
    body_cache: dict[tuple[str, str], str | None] = {}
    failures: list[str] = []

    for rel_path, class_name, member_name in MEMBER_REGISTRY:
        header_path = REPO_ROOT / rel_path
        if not header_path.exists():
            failures.append(f"missing header: {rel_path}")
            continue
        source = header_cache.setdefault(
            rel_path, header_path.read_text(encoding="utf-8")
        )
        cache_key = (rel_path, class_name)
        if cache_key not in body_cache:
            body_cache[cache_key] = _class_body(source, class_name)
        body = body_cache[cache_key]
        if body is None:
            failures.append(f"{rel_path}: {class_name} definition not found")
        elif re.search(rf"\b{re.escape(member_name)}\b", body) is None:
            failures.append(
                f"{rel_path}: {class_name}::{member_name} not found in definition"
            )

    if failures:
        details = "\n".join(f"  - {failure}" for failure in failures)
        raise RuntimeError(
            "documentation mapping symbols no longer match the headers:\n" + details
        )


def verify_schema_key_coverage() -> None:
    documented_keys: set[str] = set()
    for template in sorted(TEMPLATES_DIR.glob("*.md.j2")):
        for line in template.read_text(encoding="utf-8").splitlines():
            if not line.startswith("| `"):
                continue
            columns = line.split("|")
            if len(columns) < 2:
                continue
            first = columns[1].strip()
            if first.startswith("`") and first.endswith("`"):
                documented_keys.add(first.strip("`"))

    missing = sorted(EMITTED_JSON_KEYS - documented_keys)
    if missing:
        raise RuntimeError(
            "debug-json keys missing from projected-source mapping tables: "
            + ", ".join(missing)
        )


def _projected_source_executable(requested: str | None) -> str:
    candidate = requested or "projected-source"
    resolved = shutil.which(candidate)
    if resolved:
        return resolved
    path = Path(candidate)
    if path.is_file():
        return str(path.resolve())
    raise RuntimeError(
        "projected-source was not found; install the documented build prerequisite "
        "or pass --projected-source"
    )


def render_template(template: Path, executable: str) -> str:
    result = subprocess.run(
        [
            executable,
            "render",
            str(template),
            "-",
            "--no-header",
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"failed to render {template.name}: {detail}")
    # Chunk/audit comments are useful to projected-source itself but are not
    # part of the human-facing source topic embedded in the binary.
    rendered = re.sub(r"<!--.*?-->", "", result.stdout, flags=re.DOTALL).strip()
    if not rendered:
        raise RuntimeError(f"declared show-source topic {template.name} rendered empty")
    return rendered


def get_codec_sources(projected_source: str | None = None) -> dict[str, str]:
    verify_documented_symbols()
    verify_schema_key_coverage()

    templates = sorted(TEMPLATES_DIR.glob("*.md.j2"))
    if not templates:
        raise RuntimeError(
            "no tools/xahau-codec/projected-source/templates/*.md.j2 "
            "show-source topics were found"
        )
    declared_topics = {
        template.name.removesuffix(".md.j2") for template in templates
    }
    missing_topics = sorted(REQUIRED_CODEC_TOPICS - declared_topics)
    if missing_topics:
        raise RuntimeError(
            "advertised codec types missing show-source topics: "
            + ", ".join(missing_topics)
        )
    executable = _projected_source_executable(projected_source)

    sources: dict[str, str] = {}
    for template in templates:
        topic = template.name.removesuffix(".md.j2")
        if topic in sources:
            raise RuntimeError(f"duplicate show-source topic: {topic}")
        sources[topic] = render_template(template, executable)
    return sources


def _raw_literal(source: str, index: int) -> tuple[str, str]:
    suffix = 0
    while True:
        delimiter = f"XAHAU{index}_{suffix}"
        if f'){delimiter}\"' not in source:
            return delimiter, source
        suffix += 1


def _write_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True


def generate_cpp(output_path: Path, projected_source: str | None = None) -> None:
    sources = get_codec_sources(projected_source)
    lines = [
        "//------------------------------------------------------------------------------",
        "// Auto-generated by tools/xahau-codec/projected-source/"
        "extract_codec_source.py. DO NOT EDIT.",
        "//==============================================================================",
        "",
        "#include <map>",
        "#include <optional>",
        "#include <string>",
        "#include <vector>",
        "",
        "namespace ripple {",
        "",
        "static std::map<std::string, std::string> const kCodecSourceMap = {",
    ]
    for index, (topic, source) in enumerate(sorted(sources.items())):
        delimiter, body = _raw_literal(source, index)
        lines.extend(
            [
                f'    {{"{topic}", R"{delimiter}(',
                body,
                f'){delimiter}"}},',
            ]
        )
    lines.extend(
        [
            "};",
            "",
            "std::optional<std::string>",
            "getCodecSource(std::string const& codecType)",
            "{",
            "    auto const it = kCodecSourceMap.find(codecType);",
            "    if (it != kCodecSourceMap.end())",
            "        return it->second;",
            "    return std::nullopt;",
            "}",
            "",
            "std::vector<std::string>",
            "getAvailableCodecSources()",
            "{",
            "    std::vector<std::string> result;",
            "    result.reserve(kCodecSourceMap.size());",
            "    for (auto const& entry : kCodecSourceMap)",
            "        result.push_back(entry.first);",
            "    return result;",
            "}",
            "",
            "}  // namespace ripple",
            "",
        ]
    )
    content = "\n".join(lines)
    changed = _write_if_changed(output_path, content)
    state = "updated" if changed else "unchanged"
    print(
        f"{state}: {output_path} "
        f"({len(sources)} topics; symbols and schema verified)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate and embed projected xahau-codec source documents."
    )
    parser.add_argument("codec_type", nargs="?", help="show-source topic")
    parser.add_argument("--generate-cpp", type=Path, help="generated C++ output")
    parser.add_argument(
        "--projected-source", help="projected-source executable (default: PATH)"
    )
    parser.add_argument(
        "--verify-only", action="store_true", help="validate every declared topic"
    )
    args = parser.parse_args()

    try:
        if args.generate_cpp:
            generate_cpp(args.generate_cpp, args.projected_source)
            return 0

        sources = get_codec_sources(args.projected_source)
        if args.verify_only:
            print(
                "verified: "
                + ", ".join(sorted(sources))
                + " (symbols, schema, and renders)"
            )
            return 0

        if args.codec_type:
            source = sources.get(args.codec_type)
            if source is None:
                available = ", ".join(sorted(sources))
                print(
                    f"error: unknown codec type {args.codec_type!r}; "
                    f"available: {available}",
                    file=sys.stderr,
                )
                return 1
            print(source)
            return 0

        for topic, source in sorted(sources.items()):
            print(f"=== {topic} ===")
            print(source)
        return 0
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
