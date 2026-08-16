#!/usr/bin/env python3
"""Regenerate and validate the Hookz float-vector document twice."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any


BEGIN = "---HOOKZ_FLOAT_VECTORS_BEGIN---"
END = "---HOOKZ_FLOAT_VECTORS_END---"
SCHEMA = "xahau.xfl-multiply-divide-oracle.v1"
PROFILE = "xahauFloatV1"
SOURCE_COMMIT = "bb244ef7729503a0317bcff0f8fdaa93ca5cb7d2"
INVALID_FLOAT = "-10024"
DIVISION_BY_ZERO = "-25"
XFL_OVERFLOW = "-30"

MULTIPLY_IDS = {
    "multiply.zero-left",
    "multiply.zero-right",
    "multiply.zero-zero",
    "multiply.positive-positive",
    "multiply.negative-positive",
    "multiply.positive-negative",
    "multiply.negative-negative",
    "multiply.min-mantissa-by-one",
    "multiply.max-mantissa-by-one",
    "multiply.min-exponent-by-one",
    "multiply.max-exponent-by-one",
    "multiply.normalization-carry",
    "multiply.truncation-tail",
    "multiply.negative-truncation-tail",
    "multiply.order-16-below",
    "multiply.order-16-above",
    "multiply.order-17-below",
    "multiply.order-17-above",
    "multiply.order-17-retained-digit",
    "multiply.max-by-max",
    "multiply.underflow-to-zero",
    "multiply.overflow",
    "multiply.invalid-left-zero",
    "multiply.zero-invalid-right",
    "multiply.invalid-left-valid",
}

DIVIDE_IDS = {
    "divide.zero-numerator",
    "divide.denominator-zero",
    "divide.zero-zero",
    "divide.positive-positive",
    "divide.negative-positive",
    "divide.positive-negative",
    "divide.negative-negative",
    "divide.min-mantissa-by-one",
    "divide.max-mantissa-by-one",
    "divide.min-mantissa-numerator-generic",
    "divide.max-mantissa-numerator-generic",
    "divide.min-mantissa-denominator-generic",
    "divide.max-mantissa-denominator-generic",
    "divide.min-exponent-by-one",
    "divide.max-exponent-by-one",
    "divide.max-exponent-numerator-zero",
    "divide.zero-max-exponent-denominator",
    "divide.max-exponent-numerator-generic",
    "divide.max-exponent-denominator-generic",
    "divide.order-15-below",
    "divide.order-15-above",
    "divide.order-15-inward-correction",
    "divide.restoring-digit-10",
    "divide.restoring-coefficient-18",
    "divide.min-exponent-numerator-generic",
    "divide.min-exponent-denominator-generic",
    "divide.smaller-denominator",
    "divide.larger-denominator",
    "divide.fixed-last-digit",
    "divide.repeating-one-third",
    "divide.last-digit-neighbor",
    "divide.underflow-to-zero",
    "divide.overflow",
    "divide.invalid-numerator-zero",
    "divide.zero-invalid-denominator",
    "divide.valid-invalid-denominator",
}


def git(repo_root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())
    return result.stdout.strip()


def capture(binary: Path, repo_root: Path) -> bytes:
    result = subprocess.run(
        [str(binary), "--unittest=ripple.app.HookzFloatVectors"],
        cwd=repo_root,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "HookzFloatVectors failed:\n"
            + result.stderr.decode("utf-8", errors="replace")
        )

    begin = (BEGIN + "\n").encode()
    end = END.encode()
    if result.stdout.count(begin) != 1 or result.stdout.count(end) != 1:
        raise RuntimeError("expected exactly one Hookz vector marker pair")
    return result.stdout.split(begin, 1)[1].split(end, 1)[0]


def typed_value(node: Any, expected_type: str) -> str:
    if not isinstance(node, dict) or set(node) != {"type", "val"}:
        raise AssertionError(f"invalid typed value: {node!r}")
    assert node["type"] == expected_type, node
    value = node["val"]
    assert isinstance(value, str) and re.fullmatch(r"-?[0-9]+", value), node
    return value


def result_value(node: Any) -> tuple[bool, str]:
    assert isinstance(node, dict) and isinstance(node.get("ok"), bool), node
    if node["ok"]:
        assert set(node) == {"ok", "value"}, node
        return True, typed_value(node["value"], "u64")
    assert set(node) == {"error", "ok"}, node
    return False, typed_value(node["error"], "error")


def validate_cases(
    cases: Any, operation: str, required_ids: set[str]
) -> dict[str, dict[str, Any]]:
    assert isinstance(cases, list)
    by_id: dict[str, dict[str, Any]] = {}
    for row in cases:
        assert isinstance(row, dict), row
        assert row.get("operation") == operation, row
        identifier = row.get("id")
        category = row.get("category")
        assert isinstance(identifier, str) and identifier.startswith(operation + ".")
        assert isinstance(category, str) and category
        assert identifier not in by_id, f"duplicate row id: {identifier}"
        typed_value(row.get("a"), "u64")
        typed_value(row.get("b"), "u64")
        result_value(row.get("result"))
        by_id[identifier] = row
    assert set(by_id) == required_ids, (
        sorted(required_ids - set(by_id)),
        sorted(set(by_id) - required_ids),
    )
    return by_id


def assert_result(
    cases: dict[str, dict[str, Any]], identifier: str, ok: bool, value: str
) -> None:
    assert result_value(cases[identifier]["result"]) == (ok, value), identifier


def assert_case(
    cases: dict[str, dict[str, Any]],
    identifier: str,
    a: str,
    b: str,
    ok: bool,
    value: str,
) -> None:
    row = cases[identifier]
    assert typed_value(row["a"], "u64") == a, identifier
    assert typed_value(row["b"], "u64") == b, identifier
    assert_result(cases, identifier, ok, value)


def validate(document: Any, repo_root: Path, require_clean: bool) -> tuple[int, int]:
    assert isinstance(document, dict)
    assert document.get("version") == 1
    assert document.get("suite") == "ripple.app.HookzFloatVectors"
    oracle = document.get("xfl_multiply_divide")
    assert isinstance(oracle, dict)
    assert oracle.get("schema") == SCHEMA
    assert oracle.get("profile") == PROFILE
    assert oracle.get("xahaud_source_commit") == SOURCE_COMMIT
    assert oracle.get("fix_float_divide") is True

    head = git(repo_root, "rev-parse", "HEAD")
    branch = git(repo_root, "rev-parse", "--abbrev-ref", "HEAD")
    dirty = bool(git(repo_root, "status", "--porcelain"))
    if require_clean:
        assert not dirty, "--require-clean used with a dirty source tree"
    build = oracle.get("build")
    assert build == {
        "branch": branch,
        "commit": head + ("-dirty" if dirty else ""),
        "commit_hash": head,
        "dirty": dirty,
    }, build

    multiply = validate_cases(oracle.get("multiply_cases"), "multiply", MULTIPLY_IDS)
    divide = validate_cases(oracle.get("divide_cases"), "divide", DIVIDE_IDS)
    assert set(multiply).isdisjoint(divide)
    assert document.get("float_multiply") == oracle["multiply_cases"]
    assert document.get("float_divide") == oracle["divide_cases"]

    assert_result(multiply, "multiply.zero-left", True, "0")
    assert_result(multiply, "multiply.zero-right", True, "0")
    assert_result(multiply, "multiply.underflow-to-zero", True, "0")
    assert_result(multiply, "multiply.overflow", False, XFL_OVERFLOW)
    assert_case(
        multiply,
        "multiply.truncation-tail",
        "6360082673847140353",
        "6360082673847140353",
        True,
        "6630298651489370114",
    )
    assert_case(
        multiply,
        "multiply.negative-truncation-tail",
        "1748396655419752449",
        "6360082673847140353",
        True,
        "2018612633061982210",
    )
    assert_case(
        multiply,
        "multiply.order-16-below",
        "6369082673847140330",
        "6089866696204910592",
        True,
        "6369082673847140330",
    )
    assert_case(
        multiply,
        "multiply.order-16-above",
        "6369082673847140331",
        "6089866696204910592",
        True,
        "6369082673847140322",
    )
    assert_case(
        multiply,
        "multiply.order-17-below",
        "6369082673847140351",
        "6369082673847140312",
        True,
        "6657313049998852055",
    )
    assert_case(
        multiply,
        "multiply.order-17-above",
        "6369082673847140351",
        "6369082673847140313",
        True,
        "6657313049998852056",
    )
    assert_case(
        multiply,
        "multiply.max-by-max",
        "6369082673847140351",
        "6369082673847140351",
        True,
        "6666327448508334080",
    )
    assert_case(
        multiply,
        "multiply.order-17-retained-digit",
        "6369082673847140351",
        "6369082673847140314",
        True,
        "6657313049998852056",
    )
    positive_tail = int(
        result_value(multiply["multiply.truncation-tail"]["result"])[1]
    )
    negative_tail = int(
        result_value(multiply["multiply.negative-truncation-tail"]["result"])[1]
    )
    assert negative_tail == positive_tail ^ (1 << 62)
    assert_result(multiply, "multiply.invalid-left-zero", False, INVALID_FLOAT)
    assert_result(multiply, "multiply.zero-invalid-right", False, INVALID_FLOAT)

    assert_result(divide, "divide.zero-numerator", True, "0")
    assert_result(divide, "divide.denominator-zero", False, DIVISION_BY_ZERO)
    assert_result(divide, "divide.zero-zero", False, DIVISION_BY_ZERO)
    assert_result(divide, "divide.underflow-to-zero", True, "0")
    assert_result(divide, "divide.overflow", False, XFL_OVERFLOW)
    assert_result(
        divide,
        "divide.min-mantissa-numerator-generic",
        True,
        "6075852297695428608",
    )
    assert_result(
        divide,
        "divide.max-mantissa-numerator-generic",
        True,
        "6093866696204910592",
    )
    assert_result(
        divide,
        "divide.min-mantissa-denominator-generic",
        True,
        "6090866696204910592",
    )
    assert_result(
        divide,
        "divide.max-mantissa-denominator-generic",
        True,
        "6072852297695428608",
    )
    assert_case(
        divide,
        "divide.max-exponent-by-one",
        "7810234554605699071",
        "6089866696204910592",
        True,
        "7810234554605699071",
    )
    assert_case(
        divide,
        "divide.max-exponent-numerator-zero",
        "7810234554605699071",
        "0",
        False,
        DIVISION_BY_ZERO,
    )
    assert_case(
        divide,
        "divide.zero-max-exponent-denominator",
        "0",
        "7810234554605699071",
        True,
        "0",
    )
    assert_case(
        divide,
        "divide.max-exponent-numerator-generic",
        "7810234554605699071",
        "6361082673847140352",
        False,
        INVALID_FLOAT,
    )
    assert_case(
        divide,
        "divide.max-exponent-denominator-generic",
        "6361082673847140352",
        "7810234554605699071",
        False,
        INVALID_FLOAT,
    )
    assert_case(
        divide,
        "divide.order-15-below",
        "6361082673847140347",
        "6361082673847140352",
        True,
        "6080852297695428578",
    )
    assert_case(
        divide,
        "divide.order-15-above",
        "6361082673847140348",
        "6361082673847140352",
        True,
        "6080852297695428588",
    )
    assert_case(
        divide,
        "divide.order-15-inward-correction",
        "6360082673847140361",
        "6360082673847140362",
        True,
        "6089866696204910592",
    )
    assert_case(
        divide,
        "divide.restoring-digit-10",
        "6361082673847140361",
        "6360082673847140361",
        True,
        "6090866696204910592",
    )
    assert_case(
        divide,
        "divide.restoring-coefficient-18",
        "6360082673847140362",
        "6360982673847140352",
        True,
        "6076115455590165588",
    )
    assert_case(
        divide,
        "divide.min-exponent-numerator-generic",
        "4630700416936869888",
        "4630700416936869889",
        True,
        "6089866696204910592",
    )
    assert_case(
        divide,
        "divide.min-exponent-denominator-generic",
        "4630700416936869889",
        "4630700416936869888",
        True,
        "6089866696204910593",
    )
    assert_result(divide, "divide.invalid-numerator-zero", False, INVALID_FLOAT)
    assert_result(
        divide, "divide.zero-invalid-denominator", False, INVALID_FLOAT
    )

    guard = oracle.get("guard_fix_float_divide")
    assert isinstance(guard, dict)
    assert guard.get("id") == "guard.fix-float-divide-last-digit"
    typed_value(guard.get("a"), "u64")
    typed_value(guard.get("b"), "u64")
    fixed = result_value(guard.get("fix_true"))
    unfixed = result_value(guard.get("fix_false"))
    assert fixed[0] and unfixed[0]
    assert int(fixed[1]) == int(unfixed[1]) + 1
    assert fixed == result_value(divide["divide.fixed-last-digit"]["result"])

    categories = {row["category"] for row in multiply.values()}
    assert {
        "zero_order",
        "sign",
        "mantissa_extrema",
        "exponent_extrema",
        "normalization",
        "last_digit",
        "decimal_order_transition",
        "underflow_to_zero",
        "overflow",
        "precedence",
    } <= categories
    categories = {row["category"] for row in divide.values()}
    assert {
        "zero_order",
        "division_by_zero",
        "sign",
        "mantissa_extrema",
        "mantissa_extrema_generic",
        "exponent_extrema",
        "input_normalization_precedence",
        "input_normalization",
        "decimal_order_transition",
        "restoring_loop",
        "exponent_extrema_generic",
        "normalization",
        "fixed_divide",
        "last_digit",
        "underflow_to_zero",
        "overflow",
        "precedence",
    } <= categories
    return len(multiply), len(divide)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()

    binary = args.binary.resolve()
    repo_root = args.repo_root.resolve()
    if not binary.is_file():
        parser.error(f"rippled binary not found: {binary}")

    first = capture(binary, repo_root)
    second = capture(binary, repo_root)
    assert first == second, "two consecutive vector captures differed byte-for-byte"
    document = json.loads(first)
    multiply_count, divide_count = validate(
        document, repo_root, args.require_clean
    )
    guard = document["xfl_multiply_divide"]["guard_fix_float_divide"]
    print(
        "Hookz vector contract passed: "
        f"{multiply_count} multiply, {divide_count} divide, "
        f"byte-identical; guard false="
        f"{guard['fix_false']['value']['val']} true="
        f"{guard['fix_true']['value']['val']} @ "
        f"{document['xfl_multiply_divide']['build']['commit']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
