# xahau-codec

Small host binary over Xahau's real `libxrpl` codec. Same job as
ripple's `xrpl-codec`: dump static protocol tables and encode/decode
ledger types so other repos can export test vectors without standing
up `xahaud`.

It is not part of the default `rippled`/`xahaud` build.

Building the target requires Python 3.10+ and `projected-source` 0.1.0 on
`PATH`. A known-compatible install is:

```sh
uv tool install \
  'projected-source @ git+https://github.com/sublimator/projected-source.git@258eed7ed950ee6aaf6e9f1d8bbae4bb267ab465'
```

The generator itself uses only the Python standard library. Tree-sitter and
its language packages belong to the isolated `projected-source` installation;
the build does not depend on a checkout-local virtual environment.

```sh
cmake --build build --target xahau-codec
./build/xahau-codec --help
```

## Commands

```sh
xahau-codec server-definitions
xahau-codec show-source stobject
xahau-codec --version-json
xahau-codec encode '{"Account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}'
xahau-codec decode 811439249EE0886DE835D4F4D47DA9D9B1D2AED83C11
xahau-codec debug-json 811439249EE0886DE835D4F4D47DA9D9B1D2AED83C11
xahau-codec debug-json '{"Account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}'
xahau-codec encode --codec-type amount '"1000000"'
xahau-codec keylet account rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh
xahau-codec keylet hook rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh
```

`debug-json` writes a fixture envelope: `codec_type`, `commit` (hash plus
`-dirty` when the source tree was dirty at build), `branch`, source `json`,
original accepted `blob`, `canonical_blob` reconstructed from the materialized
`fields`, and `fields`. The blobs may differ when decoding normalizes accepted
wire, including omitted NOPs and out-of-order STObject fields. Decode and
debug-json consume the entire supplied blob; truncated or trailing data is an
error. Hex that fails to decode is an error; it does not then try JSON.

`server-definitions` is the same static JSON as `xahaud --definitions`
(`getStaticServerDefinitions()`). Live amendment votes are not included;
those still come from the RPC handler.

`--codec-type` accepts: `stobject` (default), `amount`, `currency`,
`issue`, `iou-value`, `number`, `ledger-entry-type`,
`transaction-type`, `bridge`, `pathset`, `vector256`, `quality`.

Xahau has no granular Permission table, so there is no `permission`
codec type. Hook / URIToken / Cron / ImportVL / UNLReport keylets are
included.

Input may be an argument or stdin.

`uritoken` / `credential` blobs: `hex:<even hex>`, `raw:<bytes>`, or
unprefixed even-length hex (decoded) / non-hex (raw). Odd-length
all-hex is an error unless prefixed with `raw:`.

## Embedded source documentation

`show-source` gives fixture consumers the annotated wire-format source used by
this build without requiring a source checkout at runtime:

```sh
./build/xahau-codec show-source
./build/xahau-codec show-source amount
./build/xahau-codec show-source bridge
./build/xahau-codec show-source currency
./build/xahau-codec show-source iou-value
./build/xahau-codec show-source issue
./build/xahau-codec show-source ledger-entry-type
./build/xahau-codec show-source number
./build/xahau-codec show-source pathset
./build/xahau-codec show-source quality
./build/xahau-codec show-source serializer
./build/xahau-codec show-source stobject
./build/xahau-codec show-source transaction-type
./build/xahau-codec show-source vector256
```

Topics are discovered from
`tools/xahau-codec/projected-source/templates/*.md.j2`, sorted by name,
rendered when the target is built, and embedded in the executable. The current
topics map to these templates:

| Topic | Template | Purpose |
| :--- | :--- | :--- |
| `amount` | `amount.md.j2` | Native, classic IOU, and MPT amount layouts |
| `bridge` | `bridge.md.j2` | Ordered bridge doors/issues and account VL framing |
| `currency` | `currency.md.j2` | Fixed-width native, ISO-like, and hex currency values |
| `iou-value` | `iou-value.md.j2` | Standalone 8-byte issued-number prefix |
| `issue` | `issue.md.j2` | Native, classic, and MPT issue discrimination |
| `ledger-entry-type` | `ledger-entry-type.md.j2` | Ledger format name and two-byte type mapping |
| `number` | `number.md.j2` | Decimal normalization and 12-byte Number layout |
| `pathset` | `pathset.md.j2` | Element masks, path boundaries, and final terminator |
| `quality` | `quality.md.j2` | Sortable eight-byte `in / out` offer rate |
| `serializer` | `serializer.md.j2` | Supporting field IDs, VL prefixes, and scope terminators |
| `stobject` | `stobject.md.j2` | Object parsing, ordering, and nested framing |
| `transaction-type` | `transaction-type.md.j2` | Transaction format name and two-byte type mapping |
| `vector256` | `vector256.md.j2` | VL-prefixed concatenated 256-bit values |

Every type advertised by `--codec-type` must have a same-named topic. The
generator rejects a missing codec topic, while the executable contract test
derives the advertised type list from `--help`, compares it with topic
discovery, and checks source evidence in every rendered document. `serializer`
is the one additional supporting topic.

The framing claims and intentionally unresolved parser asymmetries are covered
by byte-level executable contracts in `test_cli.py`.

The rendered Markdown contains permalinks to the Git commit observed at build
time. Dirty extracted source is marked as uncommitted by `projected-source`.
The same build-time Git snapshot feeds `--version-json`, debug-json fixture
provenance, and the optional Hookz oracle.

The generator performs three gates before updating the generated C++ file:

1. members named by the documentation mapping tables must still exist in the
   corresponding C++ class/struct definitions;
2. every nested key emitted by debug-json must occur in a mapping table;
3. every declared template must render successfully and non-empty.

A missing `projected-source`, broken symbolic extraction, or empty topic fails
the target. The generated C++ file is replaced only when its contents change,
so an unchanged rebuild does not relink the codec.

### Reviewing or adding a topic

Render and validate the documents directly before building:

```sh
projected-source render \
  tools/xahau-codec/projected-source/templates/stobject.md.j2 - --no-header
projected-source render \
  tools/xahau-codec/projected-source/templates/amount.md.j2 - --no-header
projected-source render \
  tools/xahau-codec/projected-source/templates/serializer.md.j2 - --no-header
python3 tools/xahau-codec/projected-source/extract_codec_source.py --verify-only
```

To add a codec type, add its `<codec-type>.md.j2` beneath
`tools/xahau-codec/projected-source/templates/` with stable symbolic `code()`
selectors, add mapping rows for any new debug-json keys, register its
authoritative members and required topic in `extract_codec_source.py`, and
extend the runtime/source-evidence contracts in `tools/xahau-codec/test_cli.py`.
A supporting topic follows the same process without entering the required-codec
set. No CMake source-list edit is needed because the always-checked generator
discovers templates at build time.

## Automated contract test

Build and run the real CLI contract suite (including normalized wire,
malformed/trailing input, definitions equivalence, keylets, stdin, and every
advertised codec type) with:

```sh
cmake --build build --target xahau-codec-test
```
