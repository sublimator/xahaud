#!/bin/bash
set -eu

SCRIPT_DIR=$(dirname "$0")
SCRIPT_DIR=$(cd "$SCRIPT_DIR" && pwd)

RIPPLED_ROOT="$SCRIPT_DIR/../include/xrpl"
LEDGER_FORMATS="$RIPPLED_ROOT/protocol/LedgerFormats.h"

echo '// Generated using generate_lsflags.sh'
echo ''
echo '#ifndef HOOKLSFLAGS_INCLUDED'
echo '#define HOOKLSFLAGS_INCLUDED 1'
echo ''
awk '
    function ltrim(s) { sub(/^[[:space:]]+/, "", s); return s }
    function rtrim(s) { sub(/[[:space:]]+$/, "", s); return s }
    function trim(s) { return rtrim(ltrim(s)) }

    function hook_group(name) {
        if (name == "AccountRoot") return "ltACCOUNT_ROOT"
        if (name == "Offer") return "ltOFFER"
        if (name == "RippleState") return "ltRIPPLE_STATE"
        if (name == "SignerList") return "ltSIGNER_LIST"
        if (name == "DirectoryNode") return "ltDIR_NODE"
        if (name == "NFTokenOffer") return "ltNFTOKEN_OFFER"
        if (name == "URIToken") return "ltURI_TOKEN"
        if (name == "Remark") return "remarks"
        if (name == "MPTokenIssuance") return "ltMPTOKEN_ISSUANCE"
        if (name == "MPToken") return "ltMPTOKEN"
        if (name == "Credential") return "ltCREDENTIAL"
        return ""
    }

    function flush_group() {
        if (entry_count > 0 && group != "") {
            printf "enum %s {\n", group
            for (i = 1; i <= entry_count; i++) {
                printf "    %s,\n", entries[i]
            }
            printf "};\n"
        }
        delete entries
        entry_count = 0
    }

    # Parse the XMACRO table, not the generated enum.
    /#define XMACRO\(LEDGER_OBJECT, LSF_FLAG, LSF_FLAG2\)/ { inside = 1; next }
    inside && /^#define TO_VALUE/ { flush_group(); inside = 0; next }
    !inside { next }

    {
        line = $0
        sub(/\/\/.*/, "", line)
        line = trim(line)
        if (line == "") next

        if (match(line, /LEDGER_OBJECT\(/)) {
            flush_group()
            rest = substr(line, RSTART + RLENGTH)
            sub(/,.*/, "", rest)
            group = hook_group(trim(rest))
            next
        }

        # Defining flags only (LSF_FLAG2 reuses an enumerator).
        if (line ~ /LSF_FLAG2\(/)
            next
        if (match(line, /LSF_FLAG\(/)) {
            rest = substr(line, RSTART + RLENGTH)
            n = index(rest, ",")
            fname = trim(substr(rest, 1, n - 1))
            val = substr(rest, n + 1)
            sub(/\).*/, "", val)
            entries[++entry_count] = fname " = " trim(val)
        }
    }

    BEGIN {
        inside = 0
        group = ""
        entry_count = 0
    }
' "$LEDGER_FORMATS"
echo ''
echo '#endif // HOOKLSFLAGS_INCLUDED'
