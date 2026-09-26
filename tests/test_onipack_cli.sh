#!/usr/bin/env bash
# test_onipack_cli.sh — end-to-end CLI test on fixture .oni files.
# Usage: tests/test_onipack_cli.sh [path-to-onipack]
set -u
ONIPACK="${1:-build/bin/onipack}"
FIX=$(mktemp -d); OUT=$(mktemp -d); PASS=0; FAIL=0
check() { if eval "$1"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL: $2"; fi }

# fixtures: reuse the roundtrip test's generator (its main is #ifdef-switched;
# run this script from the OniARM64 repo root)
cc -Wall -DGEN_MAIN tests/test_onipack_roundtrip.c \
   tools/onipack/onipack_oni.c tools/onipack/onipack_writer.c -o "$FIX/gen"
"$FIX/gen" "$FIX"   # TXMPcliA / TXMPcliB / ONCCbad / TXMPanim .oni into $FIX
mkdir "$FIX/anim" && mv "$FIX/TXMPanim.oni" "$FIX/anim/"   # separate case

# ONCCbad trips the #62 guard, so the pack still writes but exits 3
check '"$ONIPACK" import-sep "$FIX" "$OUT/level0_CL1.dat" 2>"$OUT/log"; [ $? -eq 3 ]' "import-sep exits 3 (packed with skips)"
check '[ -f "$OUT/level0_CL1.dat" ] && [ -f "$OUT/level0_CL1.raw" ] && [ -f "$OUT/level0_CL1.sep" ]' "three outputs"
check 'grep -q "ONCCbad" "$OUT/log"' "non-TXMP input reported by name"
check '! "$ONIPACK" import-sep "$FIX" "$OUT/level0_Final.dat" 2>/dev/null' "Final suffix refused"
check '! "$ONIPACK" import-sep /nonexistent "$OUT/level0_CL2.dat" 2>/dev/null' "missing dir errors"

# inventory sanity via txmp-format-index (SKYENV placeholder excluded)
cc -O2 -o "$OUT/idx" scripts/txmp-format-index.c
N=$("$OUT/idx" "$OUT/level0_CL1.dat" | wc -l | tr -d ' ')
check '[ "$N" = "2" ]' "two named TXMPs in pack (got $N)"

# animated group: clean exit and the pack gains its TXAN (template
# descriptor checksum 0x0000000a8b134387, little-endian byte string)
check '"$ONIPACK" import-sep "$FIX/anim" "$OUT/level0_CL3.dat" 2>>"$OUT/log"' "animated import-sep runs clean"
check 'xxd -p "$OUT/level0_CL3.dat" | tr -d "\n" | grep -q 8743138b0a000000' "TXAN template descriptor present"

# leaf-length cap (#111, #112): the engine allows 31 characters for
# level<N>_<Suffix>.dat, so a 34-char leaf is refused with a message naming
# 31 and nothing is written; a 31-char leaf still packs.
LONG23=SyntheticLongName23Abcd     # level0_ + 23 + .dat = 34
OK20=SyntheticLongName20A          # level0_ + 20 + .dat = 31
EDGE21=SyntheticLongName21Ab       # level0_ + 21 + .dat = 32: the first refused length
check '! "$ONIPACK" import-sep "$FIX/anim" "$OUT/level0_$LONG23.dat" 2>"$OUT/long.log"' "34-char output leaf refused"
check 'grep -q "31" "$OUT/long.log"' "refusal names the 31-character limit: $(cat "$OUT/long.log" 2>/dev/null)"
check '[ ! -f "$OUT/level0_$LONG23.dat" ] && [ ! -f "$OUT/level0_$LONG23.raw" ]' "nothing written for a refused leaf"
check '"$ONIPACK" import-sep "$FIX/anim" "$OUT/level0_$OK20.dat" 2>>"$OUT/log"' "31-char output leaf accepted"
check '! "$ONIPACK" import-sep "$FIX/anim" "$OUT/level0_$EDGE21.dat" 2>/dev/null' "32-char output leaf refused (boundary)"

echo "$PASS passed, $FAIL failed"; rm -rf "$FIX" "$OUT"; exit $((FAIL>0))
