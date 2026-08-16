#!/usr/bin/env python3
"""Load and exercise the optional Hookz Xahau C-ABI oracle."""

from __future__ import annotations

import argparse
import ctypes
import subprocess
from pathlib import Path


INVALID_FLOAT = -10024
DIVISION_BY_ZERO = -25


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()

    library_path = args.library.resolve()
    repo_root = args.repo_root.resolve()
    if not library_path.is_file():
        parser.error(f"oracle library not found: {library_path}")

    library = ctypes.CDLL(str(library_path))
    library.hookz_xahaud_oracle_abi_version.argtypes = []
    library.hookz_xahaud_oracle_abi_version.restype = ctypes.c_uint32
    library.hookz_xahaud_oracle_git_commit.argtypes = []
    library.hookz_xahaud_oracle_git_commit.restype = ctypes.c_char_p
    library.hookz_xahaud_float_mulratio.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
    ]
    library.hookz_xahaud_float_mulratio.restype = ctypes.c_int64

    assert library.hookz_xahaud_oracle_abi_version() == 1
    commit_raw = library.hookz_xahaud_oracle_git_commit()
    assert commit_raw is not None
    commit = commit_raw.decode("ascii")
    head = git(repo_root, "rev-parse", "HEAD")
    dirty = bool(git(repo_root, "status", "--porcelain"))
    expected_commit = head + ("-dirty" if dirty else "")
    assert commit == expected_commit, (commit, expected_commit)

    one = (1 << 62) | ((-15 + 97) << 54) | 1_000_000_000_000_000
    mulratio = library.hookz_xahaud_float_mulratio
    assert mulratio(0, 0, 1, 0) == 0
    assert mulratio(one, 0, 1, 1) == one
    assert mulratio(one, 1, 1, 1) == one
    assert mulratio(one, 0, 1, 0) == DIVISION_BY_ZERO
    assert mulratio(1 << 63, 0, 1, 1) == INVALID_FLOAT

    print(f"oracle ABI v1 passed: {library_path.name} @ {commit}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
