#!/usr/bin/env python3
"""End-to-end contract tests for the built xahau-codec executable."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
ACCOUNT_2 = "raD9P4zYQnncGpFNgWLdQXFMd4bYJtvF8m"
ACCOUNT_FIELD = "8114B5F762798A53D543A014CAF8B297CFF8F2F937E8"
VECTOR_ID = "0" * 63 + "1"
SUPPORTED_CODEC_TYPES = [
    "stobject",
    "amount",
    "currency",
    "issue",
    "iou-value",
    "number",
    "ledger-entry-type",
    "transaction-type",
    "bridge",
    "pathset",
    "vector256",
    "quality",
]
EXPECTED_TOPICS = sorted([*SUPPORTED_CODEC_TYPES, "serializer"])
SOURCE_EVIDENCE = {
    "amount": [
        "STAmount::STAmount",
        "STAmount::add",
        "STAmount::canonicalize",
        "isLegalNet",
    ],
    "bridge": [
        "STXChainBridge::STXChainBridge",
        "STXChainBridge::add",
        "STAccount::add",
    ],
    "currency": ["currencyFromJson", "to_currency", "STCurrency::add"],
    "iou-value": ["amountFromString", "STAmount::STAmount", "STAmount::add"],
    "issue": [
        "assetFromJson",
        "STIssue::STIssue",
        "STIssue::add",
        "mptIssueFromJson",
    ],
    "ledger-entry-type": [
        "KnownFormats::findTypeByName",
        "LedgerFormats::LedgerFormats",
        "Serializer::add16",
    ],
    "number": ["parseNumberFromString", "Number::normalize", "STNumber::add"],
    "pathset": [
        "STPathElement::getNodeType",
        "STPathSet::STPathSet",
        "STPathSet::add",
    ],
    "quality": ["getRate", "amountFromQuality", "Quality::Quality"],
    "serializer": [
        "Serializer::addFieldID",
        "SerialIter::getFieldID",
        "Serializer::addVL",
    ],
    "stobject": [
        "STObject::set",
        "STObject::add",
        "STObject::getSortedFields",
    ],
    "transaction-type": [
        "KnownFormats::findTypeByName",
        "TxFormats::TxFormats",
        "Serializer::add16",
    ],
    "vector256": [
        "STVector256::STVector256",
        "STVector256::add",
        "Serializer::addVL",
    ],
}


class CodecContractTest(unittest.TestCase):
    binary: Path
    rippled: Path
    repo_root: Path

    @classmethod
    def run_command(
        cls,
        command: list[str | os.PathLike[str]],
        *,
        stdin: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(part) for part in command],
            cwd=cls.repo_root,
            input=stdin,
            text=True,
            capture_output=True,
            check=False,
        )

    @classmethod
    def codec(
        cls, *arguments: str, stdin: str | None = None
    ) -> subprocess.CompletedProcess[str]:
        return cls.run_command([cls.binary, *arguments], stdin=stdin)

    def assert_success(
        self, result: subprocess.CompletedProcess[str]
    ) -> subprocess.CompletedProcess[str]:
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
        return result

    def assert_failure(
        self, result: subprocess.CompletedProcess[str]
    ) -> subprocess.CompletedProcess[str]:
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertTrue(result.stderr, "failure must explain itself on stderr")
        return result

    def encode(self, codec_type: str, value: object | str) -> str:
        source = value if isinstance(value, str) else json.dumps(value)
        result = self.assert_success(
            self.codec("encode", "--codec-type", codec_type, source)
        )
        blob = result.stdout.strip()
        self.assertRegex(blob, r"^(?:[0-9A-F]{2})+$")
        return blob

    def decode(self, codec_type: str, blob: str) -> str:
        result = self.assert_success(
            self.codec("decode", "--codec-type", codec_type, blob)
        )
        return result.stdout.strip()

    def assert_round_trip(self, codec_type: str, value: object | str) -> None:
        blob = self.encode(codec_type, value)
        decoded = self.decode(codec_type, blob)
        self.assertEqual(self.encode(codec_type, decoded), blob)

    def git(self, *arguments: str) -> str:
        result = self.run_command(["git", *arguments])
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_help_topics_and_embedded_source(self) -> None:
        help_result = self.assert_success(self.codec("--help"))
        help_text = help_result.stdout + help_result.stderr
        self.assertIn("server-definitions", help_text)
        self.assertIn("debug-json", help_text)
        self.assertIn("show-source", help_text)
        supported_match = re.search(
            r"Supported:\s*(.*?)\n\s*--version", help_text, re.DOTALL
        )
        self.assertIsNotNone(supported_match)
        advertised = [
            item.strip()
            for item in supported_match.group(1).replace("\n", " ").split(",")
        ]
        self.assertEqual(advertised, SUPPORTED_CODEC_TYPES)

        topics_result = self.assert_success(self.codec("show-source"))
        topics = [
            line.removeprefix("  - ")
            for line in topics_result.stdout.splitlines()
            if line.startswith("  - ")
        ]
        self.assertEqual(topics, EXPECTED_TOPICS)

        head = self.git("rev-parse", "HEAD")
        for topic in EXPECTED_TOPICS:
            rendered = self.assert_success(
                self.codec("show-source", topic)
            ).stdout
            self.assertIn(f"/blob/{head}/", rendered)
            self.assertGreater(len(rendered), 300)
            self.assertNotIn("{{", rendered)
            self.assertNotIn("<!--", rendered)
            for evidence in SOURCE_EVIDENCE[topic]:
                self.assertIn(evidence, rendered)

        failure = self.assert_failure(self.codec("show-source", "not-a-topic"))
        self.assertIn("no source available", failure.stderr)

    def test_version_json_matches_build_tree(self) -> None:
        version = json.loads(
            self.assert_success(self.codec("--version-json")).stdout
        )
        head = self.git("rev-parse", "HEAD")
        branch = self.git("rev-parse", "--abbrev-ref", "HEAD")
        dirty = bool(self.git("status", "--porcelain"))
        self.assertEqual(version["commit_hash"], head)
        self.assertEqual(version["branch"], branch)
        self.assertEqual(version["dirty"], dirty)
        self.assertEqual(version["commit"], head + ("-dirty" if dirty else ""))

    def test_every_advertised_codec_type_round_trips(self) -> None:
        self.assert_round_trip("stobject", {"Account": ACCOUNT})
        self.assert_round_trip("amount", '"1000000"')
        self.assert_round_trip("currency", '"USD"')
        self.assert_round_trip(
            "issue", {"currency": "USD", "issuer": ACCOUNT}
        )
        self.assert_round_trip("iou-value", '"1.5"')
        self.assert_round_trip("number", '"42"')
        self.assert_round_trip("ledger-entry-type", '"AccountRoot"')
        self.assert_round_trip("transaction-type", '"Payment"')
        self.assert_round_trip(
            "bridge",
            {
                "LockingChainDoor": ACCOUNT,
                "LockingChainIssue": {"currency": "XAH"},
                "IssuingChainDoor": ACCOUNT_2,
                "IssuingChainIssue": {
                    "currency": "USD",
                    "issuer": ACCOUNT,
                },
            },
        )
        self.assert_round_trip("pathset", [[{"account": ACCOUNT}]])
        self.assert_round_trip("vector256", [VECTOR_ID])

        quality = self.encode("quality", {"out": "2", "in": "1"})
        self.assertEqual(self.decode("quality", quality), "0.5")

    def test_adversarial_standalone_framing_contracts(self) -> None:
        account_bytes = ACCOUNT_FIELD.removeprefix("8114")

        self.assertEqual(self.encode("amount", '"0"'), "4000000000000000")
        iou_zero = self.encode(
            "amount",
            {"value": "0", "currency": "USD", "issuer": ACCOUNT},
        )
        self.assertEqual(len(iou_zero), 48 * 2)
        self.assertTrue(iou_zero.startswith("8000000000000000"))
        amount_mpt_json = {
            "value": "1",
            "mpt_issuance_id": "00000001" + "01" * 20,
        }
        mpt_amount = self.encode("amount", amount_mpt_json)
        self.assertEqual(len(mpt_amount), 33 * 2)
        self.assertEqual(mpt_amount[:18], "60" + "0000000000000001")
        self.assertEqual(
            json.loads(self.decode("amount", mpt_amount)), amount_mpt_json
        )

        nested = self.encode(
            "stobject", {"Memos": [{"Memo": {"MemoData": "00"}}]}
        )
        self.assertEqual(nested, "F9EA7D0100E1F1")
        top_level = self.encode("stobject", {"Account": ACCOUNT})
        self.assertFalse(top_level.endswith("E1"))
        self.assert_success(
            self.codec(
                "decode", "--codec-type", "stobject", "99" * 63 + top_level
            )
        )
        self.assert_failure(
            self.codec(
                "decode", "--codec-type", "stobject", "99" * 64 + top_level
            )
        )

        self.assertEqual(self.encode("currency", '"XAH"'), "00" * 20)
        self.assertEqual(self.encode("currency", '""'), "00" * 20)
        self.assertEqual(
            self.encode("currency", '"USD"'),
            "00" * 12 + "555344" + "00" * 5,
        )
        no_currency = "00" * 19 + "01"
        self.assertEqual(self.decode("currency", no_currency), '"1"')
        self.assert_failure(
            self.codec("encode", "--codec-type", "currency", '"1"')
        )
        self.assert_failure(
            self.codec("encode", "--codec-type", "currency", '"xah"')
        )

        native_issue = self.encode("issue", {"currency": "XAH"})
        classic_issue = self.encode(
            "issue", {"currency": "USD", "issuer": ACCOUNT}
        )
        mpt_json = {
            "mpt_issuance_id": "00000001" + "01" * 20,
        }
        mpt_issue = self.encode("issue", mpt_json)
        self.assertEqual(len(native_issue), 20 * 2)
        self.assertEqual(len(classic_issue), 40 * 2)
        self.assertEqual(len(mpt_issue), 44 * 2)
        self.assertEqual(json.loads(self.decode("issue", mpt_issue)), mpt_json)
        self.assert_failure(
            self.codec(
                "encode",
                "--codec-type",
                "issue",
                json.dumps(
                    {
                        "currency": "USD",
                        "issuer": ACCOUNT,
                        **mpt_json,
                    }
                ),
            )
        )

        iou = self.encode("iou-value", '"1.5"')
        self.assertEqual(len(iou), 8 * 2)
        self.assert_failure(
            self.codec(
                "decode", "--codec-type", "iou-value", "4000000000000000"
            )
        )

        number = self.encode("number", '"1000000000000000501"')
        self.assertEqual(len(number), 12 * 2)
        self.assertEqual(self.decode("number", number), "1000000000000001e3")
        self.assertEqual(
            self.decode(
                "number", self.encode("number", '"1000000000000000500"')
            ),
            "1000000000000000e3",
        )
        self.assertEqual(
            self.decode(
                "number", self.encode("number", '"1000000000000001500"')
            ),
            "1000000000000002e3",
        )
        self.assert_failure(
            self.codec(
                "encode", "--codec-type", "number", '"1e2147483648"'
            )
        )

        self.assertEqual(
            self.encode("ledger-entry-type", '"AccountRoot"'), "0061"
        )
        self.assertEqual(self.encode("transaction-type", '"Payment"'), "0000")
        self.assert_failure(
            self.codec("encode", "--codec-type", "transaction-type", "0")
        )

        bridge_json = {
            "LockingChainDoor": ACCOUNT,
            "LockingChainIssue": {"currency": "XAH"},
            "IssuingChainDoor": ACCOUNT_2,
            "IssuingChainIssue": {"currency": "USD", "issuer": ACCOUNT},
        }
        bridge = self.encode("bridge", bridge_json)
        self.assertEqual(len(bridge), (21 + 20 + 21 + 40) * 2)
        self.assertTrue(bridge.startswith("14" + account_bytes))
        self.assert_failure(
            self.codec(
                "encode",
                "--codec-type",
                "bridge",
                json.dumps({**bridge_json, "extra": True}),
            )
        )
        bridge_with_mpt = {
            **bridge_json,
            "LockingChainIssue": mpt_json,
        }
        self.assert_failure(
            self.codec(
                "encode",
                "--codec-type",
                "bridge",
                json.dumps(bridge_with_mpt),
            )
        )

        pathset = self.encode("pathset", [[{"account": ACCOUNT}]])
        self.assertEqual(pathset, "01" + account_bytes + "00")
        self.assert_failure(
            self.codec("decode", "--codec-type", "pathset", "FF")
        )

        self.assertEqual(self.encode("vector256", []), "00")
        self.assertEqual(
            self.encode("vector256", [VECTOR_ID]), "20" + VECTOR_ID
        )
        self.assert_failure(
            self.codec("decode", "--codec-type", "vector256", "01FF")
        )

        quality = self.encode("quality", {"out": "2", "in": "1"})
        self.assertEqual(quality, "5411C37937E08000")
        self.assertEqual(self.decode("quality", quality), "0.5")

    def test_debug_json_preserves_accepted_and_canonical_wire(self) -> None:
        canonical = self.encode("stobject", {"Account": ACCOUNT})
        self.assertEqual(canonical, ACCOUNT_FIELD)

        padded = "99" + canonical
        fixture = json.loads(
            self.assert_success(self.codec("debug-json", padded)).stdout
        )
        self.assertEqual(fixture["codec_type"], "stobject")
        self.assertEqual(fixture["blob"], padded)
        self.assertEqual(fixture["canonical_blob"], canonical)
        self.assertEqual(fixture["json"], {"Account": ACCOUNT})
        self.assertEqual(fixture["fields"][0]["name"], "Account")
        self.assertIn("commit", fixture)
        self.assertIn("branch", fixture)

        sequence = "2400000001"
        out_of_order = canonical + sequence
        reordered = json.loads(
            self.assert_success(self.codec("debug-json", out_of_order)).stdout
        )
        self.assertEqual(reordered["blob"], out_of_order)
        self.assertEqual(reordered["canonical_blob"], sequence + canonical)

        path_json = [[{"account": ACCOUNT}]]
        path_blob = self.encode("pathset", path_json)
        path_fixture = json.loads(
            self.assert_success(
                self.codec(
                    "debug-json",
                    "--codec-type",
                    "pathset",
                    json.dumps(path_json),
                )
            ).stdout
        )
        self.assertEqual(path_fixture["blob"], path_blob)
        self.assertEqual(path_fixture["canonical_blob"], path_blob)

        vector_json = [VECTOR_ID]
        vector_blob = self.encode("vector256", vector_json)
        vector_fixture = json.loads(
            self.assert_success(
                self.codec(
                    "debug-json",
                    "--codec-type",
                    "vector256",
                    json.dumps(vector_json),
                )
            ).stdout
        )
        self.assertEqual(vector_fixture["blob"], vector_blob)
        self.assertEqual(vector_fixture["canonical_blob"], vector_blob)

    def test_malformed_truncated_and_trailing_input_fail(self) -> None:
        cases = [
            ("decode", "--codec-type", "amount", "GG"),
            ("decode", "--codec-type", "amount", "4000"),
            ("decode", "--codec-type", "amount", "4000000000000000FF"),
            ("decode", "--codec-type", "ledger-entry-type", "0061FF"),
            ("debug-json", "--codec-type", "amount", "4000000000000000FF"),
            ("decode", "--codec-type", "not-a-codec", "00"),
            ("encode", "--codec-type", "pathset", "[]"),
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                self.assert_failure(self.codec(*arguments))

    def test_stdin_and_keylets(self) -> None:
        source = json.dumps({"Account": ACCOUNT})
        positional = self.assert_success(self.codec("encode", source)).stdout
        via_stdin = self.assert_success(
            self.codec("encode", stdin=source + "\n")
        ).stdout
        self.assertEqual(via_stdin, positional)

        account_key = self.assert_success(
            self.codec("keylet", "account", ACCOUNT)
        ).stdout.strip()
        self.assertRegex(account_key, r"^[0-9A-F]{64}$")
        self.assert_failure(self.codec("keylet", "account", "not-an-account"))
        self.assert_failure(
            self.codec("keylet", "ticket", ACCOUNT, "4294967296")
        )

    def test_static_definitions_match_host_binary(self) -> None:
        codec_definitions = json.loads(
            self.assert_success(self.codec("server-definitions")).stdout
        )
        host = self.assert_success(
            self.run_command([self.rippled, "--definitions"])
        )
        host_definitions = json.loads(host.stdout)
        self.assertEqual(codec_definitions, host_definitions)

        fields = {name: details for name, details in codec_definitions["FIELDS"]}
        self.assertEqual(
            fields["ObjectEndMarker"],
            {
                "nth": 1,
                "isVLEncoded": False,
                "isSerialized": True,
                "isSigningField": True,
                "type": "STObject",
            },
        )
        self.assertEqual(
            fields["ArrayEndMarker"],
            {
                "nth": 1,
                "isVLEncoded": False,
                "isSerialized": True,
                "isSigningField": True,
                "type": "STArray",
            },
        )
        marker_bytes = {}
        for name in ("ObjectEndMarker", "ArrayEndMarker"):
            details = fields[name]
            type_code = codec_definitions["TYPES"][details["type"]]
            marker_bytes[name] = f"{(type_code << 4) | details['nth']:02X}"
        self.assertEqual(
            marker_bytes,
            {"ObjectEndMarker": "E1", "ArrayEndMarker": "F1"},
        )
        self.assertIn("TRANSACTION_FORMATS", codec_definitions)
        self.assertIn("LEDGER_ENTRY_FORMATS", codec_definitions)
        self.assertIn("INNER_OBJECT_FORMATS", codec_definitions)
        self.assertIn("LEDGER_ENTRY_FLAGS", codec_definitions)
        self.assertNotIn("FEATURES", codec_definitions)

    def test_projected_source_generator_contract(self) -> None:
        generator = (
            self.repo_root
            / "tools/xahau-codec/projected-source/extract_codec_source.py"
        )
        verified = self.assert_success(
            self.run_command([sys.executable, generator, "--verify-only"])
        )
        self.assertEqual(
            verified.stdout.strip(),
            "verified: "
            + ", ".join(EXPECTED_TOPICS)
            + " (symbols, schema, and renders)",
        )

        missing = self.assert_failure(
            self.run_command(
                [
                    sys.executable,
                    generator,
                    "--verify-only",
                    "--projected-source",
                    str(self.repo_root / "not-a-projected-source-binary"),
                ]
            )
        )
        self.assertIn("projected-source was not found", missing.stderr)

        with tempfile.TemporaryDirectory(prefix="xahau-codec-source-") as tmp:
            output = Path(tmp) / "GeneratedCodecSource.cpp"
            first = self.assert_success(
                self.run_command(
                    [sys.executable, generator, "--generate-cpp", output]
                )
            )
            self.assertIn("updated:", first.stdout)
            first_mtime = output.stat().st_mtime_ns
            second = self.assert_success(
                self.run_command(
                    [sys.executable, generator, "--generate-cpp", output]
                )
            )
            self.assertIn("unchanged:", second.stdout)
            self.assertEqual(output.stat().st_mtime_ns, first_mtime)

    def test_git_provenance_generator_tracks_state_content_stably(self) -> None:
        cmake = shutil.which("cmake")
        git = shutil.which("git")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(git)
        script = self.repo_root / "cmake/GenerateGitProvenance.cmake"

        with tempfile.TemporaryDirectory(prefix="xahau-provenance-") as tmp:
            repo = Path(tmp)
            tracked = repo / "tracked.txt"
            output = repo / "generated/BuildInfoGenerated.h"

            def run(*command: str | os.PathLike[str]) -> str:
                result = subprocess.run(
                    [str(part) for part in command],
                    cwd=repo,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                return result.stdout.strip()

            def generate() -> str:
                run(
                    cmake,
                    f"-DSOURCE_DIR={repo}",
                    f"-DOUTPUT_FILE={output}",
                    f"-DGIT_EXECUTABLE={git}",
                    "-P",
                    script,
                )
                return output.read_text(encoding="utf-8")

            run(git, "init", "-q")
            run(git, "config", "user.name", "Codec Test")
            run(git, "config", "user.email", "codec-test@example.invalid")
            tracked.write_text("one\n", encoding="utf-8")
            (repo / ".gitignore").write_text("generated/\n", encoding="utf-8")
            run(git, "add", "tracked.txt", ".gitignore")
            run(git, "commit", "-q", "-m", "one")

            first_head = run(git, "rev-parse", "HEAD")
            clean = generate()
            self.assertIn(f'#define GIT_COMMIT_HASH "{first_head}"', clean)
            self.assertIn("#define GIT_DIRTY 0", clean)
            clean_mtime = output.stat().st_mtime_ns
            self.assertEqual(generate(), clean)
            self.assertEqual(output.stat().st_mtime_ns, clean_mtime)

            (repo / "untracked.txt").write_text("dirty\n", encoding="utf-8")
            dirty = generate()
            self.assertIn("#define GIT_DIRTY 1", dirty)

            (repo / "untracked.txt").unlink()
            clean_again = generate()
            self.assertIn("#define GIT_DIRTY 0", clean_again)

            tracked.write_text("two\n", encoding="utf-8")
            modified = generate()
            self.assertIn("#define GIT_DIRTY 1", modified)
            run(git, "add", "tracked.txt")
            run(git, "commit", "-q", "-m", "two")
            second_head = run(git, "rev-parse", "HEAD")
            self.assertNotEqual(first_head, second_head)
            after_commit = generate()
            self.assertIn(f'#define GIT_COMMIT_HASH "{second_head}"', after_commit)
            self.assertIn("#define GIT_DIRTY 0", after_commit)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--rippled", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    args, unittest_args = parser.parse_known_args()

    CodecContractTest.binary = args.binary.resolve()
    CodecContractTest.rippled = args.rippled.resolve()
    CodecContractTest.repo_root = args.repo_root.resolve()
    if not CodecContractTest.binary.is_file():
        parser.error(f"codec binary not found: {CodecContractTest.binary}")
    if not CodecContractTest.rippled.is_file():
        parser.error(f"rippled binary not found: {CodecContractTest.rippled}")

    program = unittest.main(argv=[sys.argv[0], *unittest_args], exit=False)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
