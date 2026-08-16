# hookz Xahau oracle

This optional shared library exposes a deliberately small C ABI over Xahau's
real arithmetic for live differential tests in `hooks-testing`.

Build from the configured Xahau worktree:

```sh
cmake --build build --target hookz_xahaud_oracle
```

On macOS the result is `build/libhookz_xahaud_oracle.dylib`. It is not part of
the normal `rippled` build. ABI v1 currently exports `float_mulratio` plus ABI
and source-commit identification functions.

Load and exercise the built C ABI, including exact build provenance and basic
`float_mulratio` success/error paths, with:

```sh
cmake --build build --target hookz_xahaud_oracle_test
```

Regenerate the complete `ripple.app.HookzFloatVectors` document twice and
validate its schema, typed integer leaves, build identity, exact required row
IDs, duplicate protection, fixed/unfixed divide guard, and byte stability with:

```sh
cmake --build build --target hookz_float_vectors_test
```

The multiply/divide portion is versioned as
`xahau.xfl-multiply-divide-oracle.v1`. It contains 25 independently labeled
multiply rows and 36 divide rows. A final release receipt should also run the
validator against the committed build with a clean-worktree requirement:

```sh
python3 tools/hookz-oracle/test_vectors.py \
  --binary build/rippled \
  --repo-root . \
  --require-clean
```

The executable semantic contract is enforced by `test_vectors.py`.

Run the optional hookz differential sweep with:

```sh
HOOKZ_XAHAUD_ORACLE="$PWD/build/libhookz_xahaud_oracle.dylib" \
  /path/to/hooks-testing/.venv/bin/pytest \
  /path/to/hooks-testing/tests/test_xahaud_oracle.py
```

Linux builds use the corresponding `.so` path. The test refuses a library
built from a different Xahau commit than the one pinned by hookz.

The reported commit and `-dirty` suffix are refreshed at build time. Frozen
live-host vectors remain the portable evidence, and the differential test
checks the dylib against those vectors before running generated cases. The
local dylib may export transitive `libxrpl` symbols and carry build-tree RPATHs;
only `hookz_xahaud_*` is a supported interface.

ABI v1 deliberately remains limited to `float_mulratio`: the current live
consumer calls only that operation. Downstream `float_sto`/`float_sto_set`
coverage uses its dedicated committed byte fixtures derived from Xahau's
`SetHook_test`, so adding live entry points here would not close a demonstrated
coverage gap. Expand the ABI only when a consumer needs a live comparison that
the frozen fixtures cannot provide.
