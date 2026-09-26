#!/usr/bin/env bash
# test_onimod_installer.sh — end-to-end test of the OniMod Installer CLI
# mode against generated fixtures laid out like real depot downloads.
# Usage: tests/test_onimod_installer.sh [path-to-onipack] [path-to-txmp-format-index]
# Run from the OniARM64 repo root.
set -u
ONIPACK="${1:-build/bin/onipack}"
INDEX="${2:-build/bin/txmp-format-index}"
W=$(mktemp -d); PASS=0; FAIL=0
check() { if eval "$1"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL: $2"; fi }

# Build the installer binary headless (same sources the bundle uses).
swiftc -O tools/OniModInstaller/Installer.swift tools/OniModInstaller/main.swift \
    -framework AppKit -framework UniformTypeIdentifiers -o "$W/inst" || { echo "swiftc failed"; exit 1; }
INST="$W/inst"
export ONIMOD_ONIPACK="$ONIPACK" ONIMOD_INDEX="$INDEX"

# Fixture textures.
cc -Wall -DGEN_MAIN tests/test_onipack_roundtrip.c tools/onipack/onipack_oni.c \
   tools/onipack/onipack_writer.c -o "$W/gen" && "$W/gen" "$W"

# Mod A: depot shape — wrapper dir, Mod_Info.cfg, lowercase level dir,
# textures nested one deeper, a non-TXMP file that must be ignored, and a
# level-3 texture two dirs deep. Zipped like a depot download.
A="$W/23999-Test-Mod-A/23999TestModA"
mkdir -p "$A/oni/common/level0_final/faces" "$A/oni/common/level3_Final/level3_Final/x"
printf 'AEInstallVersion -> 2.0\nNameOfMod -> Test Mod A!\nCreator -> someone\n' > "$A/Mod_Info.cfg"
cp "$W/TXMPcliA.oni" "$A/oni/common/level0_final/faces/"
cp "$W/ONCCbad.oni"  "$A/oni/common/level0_final/faces/"
cp "$W/TXMPcliB.oni" "$A/oni/common/level3_Final/level3_Final/x/"
(cd "$W/23999-Test-Mod-A" && ditto -c -k --keepParent 23999TestModA "$W/23999-Test-Mod-A.zip")

DEST="$W/TexturePacks"

# 1. zip install
"$INST" --install "$W/23999-Test-Mod-A.zip" --dest "$DEST" --gamedata none > "$W/out1" 2>&1; rc=$?
check '[ $rc -eq 0 ]' "zip install exits 0 (rc=$rc): $(cat "$W/out1")"
check '[ -f "$DEST/TestModA/level0_TestModA.dat" ] && [ -f "$DEST/TestModA/level0_TestModA.raw" ] && [ -f "$DEST/TestModA/level0_TestModA.sep" ]' "level0 triple written under sanitised mod name"
check '[ -f "$DEST/TestModA/level3_TestModA.dat" ]' "nested level3 dir found (case-insensitive, doubled dir)"
check '! ls "$DEST/TestModA" | grep -q Final' "no _Final output"
check 'grep -q "level 0" "$W/out1" && grep -q "level 3" "$W/out1"' "report names both levels"
check '[ -f "$DEST/TestModA/Mod_Info.txt" ]' "Mod_Info carried into the pack folder"
N=$("$INDEX" "$DEST/TestModA/level0_TestModA.dat" | wc -l | tr -d ' ')
check '[ "$N" = "1" ]' "level0 pack holds exactly the one TXMP, ONCC ignored (got $N)"

# 2. re-install refused without --replace, accepted with it
"$INST" --install "$W/23999-Test-Mod-A.zip" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc=$?
check '[ $rc -eq 4 ]' "existing pack refused without --replace (rc=$rc)"
"$INST" --install "$W/23999-Test-Mod-A.zip" --dest "$DEST" --gamedata none --replace >/dev/null 2>&1; rc=$?
check '[ $rc -eq 0 ]' "--replace re-installs (rc=$rc)"

# 3. folder install, no Mod_Info: name from the folder with the depot ID stripped
B="$W/24001-Second Mod"
mkdir -p "$B/oni/level0_Final"
cp "$W/TXMPcliA.oni" "$B/oni/level0_Final/"
"$INST" --install "$B" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ -f "$DEST/SecondMod/level0_SecondMod.dat" ]' "folder install, name derived from folder (rc=$rc)"

# 4. nothing packable → non-zero, nothing written
C="$W/empty-mod"; mkdir -p "$C/oni/level0_Final"; cp "$W/ONCCbad.oni" "$C/oni/level0_Final/"
"$INST" --install "$C" --dest "$DEST" --gamedata none > "$W/out4" 2>&1; rc=$?
check '[ $rc -eq 1 ] && [ ! -d "$DEST/emptymod" ]' "no TXMP files → rc 1, no folder (rc=$rc)"
check 'grep -qi "no texture" "$W/out4"' "report says why"

# 5. a mod literally named Final gets a safe suffix
D="$W/Final"; mkdir -p "$D/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$D/oni/level0_Final/"
"$INST" --install "$D" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ -f "$DEST/FinalMod/level0_FinalMod.dat" ]' "reserved name 'Final' remapped (rc=$rc)"

# 6. alpha guard: --gamedata pointing at a folder with no level dats → guard skipped, still installs
mkdir -p "$W/gd"
"$INST" --install "$B" --dest "$DEST" --gamedata "$W/gd" --replace > "$W/out6" 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -qi "alpha guard" "$W/out6"' "missing retail data reported, install proceeds (rc=$rc)"

# 7. alpha guard with a real index: retail pack says TXMPcliA is BGRA4444-class?
#    Build a "retail" dat from the fixtures so the guard has something to read,
#    then check the guard reports on rather than off.
mkdir -p "$W/gd2" && "$ONIPACK" import-sep "$W" "$W/gd2/level0_Final.dat" >/dev/null 2>&1 || true
# onipack refuses the Final suffix by design; stage under another name, then rename the triple.
"$ONIPACK" import-sep "$W" "$W/gd2/level0_RT.dat" >/dev/null 2>&1
for ext in dat raw sep; do [ -f "$W/gd2/level0_RT.$ext" ] && mv "$W/gd2/level0_RT.$ext" "$W/gd2/level0_Final.$ext"; done
"$INST" --install "$B" --dest "$DEST" --gamedata "$W/gd2" --replace > "$W/out7" 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -q "alpha guard: on" "$W/out7"' "retail index built from level*_Final.dat, guard on (rc=$rc): $(grep -i alpha "$W/out7")"

# 8. long names (#111, #112): a 30-char NameOfMod becomes a 19-char pack name
#    (13-char prefix + 6-char digest), every leaf stays <= 31 even for level10_,
#    the report says so, and the shortening is deterministic (re-install hits
#    the already-installed check).
E="$W/long-a"; mkdir -p "$E/oni/common/level0_Final" "$E/oni/common/level10_Final"
printf 'NameOfMod -> BetterWarehouseTrainingRooms12\n' > "$E/Mod_Info.cfg"    # 30 chars
cp "$W/TXMPcliA.oni" "$E/oni/common/level0_Final/"; cp "$W/TXMPcliB.oni" "$E/oni/common/level10_Final/"
"$INST" --install "$E" --dest "$DEST" --gamedata none > "$W/out8" 2>&1; rc=$?
PACK8=$(ls "$DEST" | grep '^BetterWarehou' | head -1)
check '[ $rc -eq 0 ] && [ "$PACK8" = "BetterWarehouwf6c4j" ]' "30-char name shortened to the pinned 19-char pack name BetterWarehouwf6c4j (rc=$rc, got '$PACK8')"
LONGLEAF=0; for f in "$DEST/$PACK8"/level*; do b=$(basename "$f"); [ "${#b}" -le 31 ] || LONGLEAF=1; done
check '[ "$LONGLEAF" = 0 ] && [ -f "$DEST/$PACK8/level10_$PACK8.dat" ]' "every leaf <= 31 chars, level10_ included"
check 'grep -qi "shortened" "$W/out8"' "report says the name was shortened"
"$INST" --install "$E" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc=$?
check '[ $rc -eq 4 ]' "shortened name is deterministic: re-install is refused as already installed (rc=$rc)"

# 9. two long names sharing a 19-char prefix install as two distinct packs
#    with the pinned digests (the leaf must never change between versions,
#    or re-installs would duplicate packs).
for n in CharacterRetexturePt1KonokoCops CharacterRetexturePt3TCTF; do
    mkdir -p "$W/$n/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/$n/oni/level0_Final/"
done
"$INST" --install "$W/CharacterRetexturePt1KonokoCops" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc1=$?
"$INST" --install "$W/CharacterRetexturePt3TCTF" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc2=$?
check '[ $rc1 -eq 0 ] && [ $rc2 -eq 0 ] && [ -f "$DEST/CharacterRetee5cm8v/level0_CharacterRetee5cm8v.dat" ] && [ -f "$DEST/CharacterRete9je5ta/level0_CharacterRete9je5ta.dat" ]' "prefix-sharing names get distinct pinned digests (rc=$rc1/$rc2): $(ls "$DEST" | grep CharacterRete | tr '\n' ' ')"

# 10. engine file-id collision: the id is level<<25 | weighted-letter-sum<<1 | 1,
#     and A*1+B*2 == C*1+A*2 == 5, so "AB" and "CA" collide at level 0. The
#     second install is refused with exit 5 naming the first; nothing is written.
for n in AB CA; do mkdir -p "$W/$n/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/$n/oni/level0_Final/"; done
"$INST" --install "$W/AB" --dest "$DEST" --gamedata none >/dev/null 2>&1; rc1=$?
"$INST" --install "$W/CA" --dest "$DEST" --gamedata none > "$W/out10" 2>&1; rc2=$?
check '[ $rc1 -eq 0 ] && [ $rc2 -eq 5 ] && grep -q "pack '\''AB'\''" "$W/out10"' "id collision refused with exit 5 naming the installed pack (rc=$rc1/$rc2): $(cat "$W/out10")"
check '[ ! -d "$DEST/CA" ]' "nothing written on collision"
"$INST" --install "$W/AB" --dest "$DEST" --gamedata none --replace >/dev/null 2>&1; rc=$?
check '[ $rc -eq 0 ]' "--replace does not collide with the pack it replaces (rc=$rc)"
"$INST" --install "$W/CA" --dest "$DEST" --gamedata none --replace >/dev/null 2>&1; rc=$?
check '[ $rc -eq 5 ] && [ ! -d "$DEST/CA" ]' "--replace still refuses a collision with a different installed pack (rc=$rc)"
mkdir -p "$W/0H/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/0H/oni/level0_Final/"
"$INST" --install "$W/0H" --dest "$DEST" --gamedata none > "$W/out10b" 2>&1; rc=$?
check '[ $rc -eq 5 ] && grep -q "level0_Final" "$W/out10b"' "a name hashing to 0 ('0H': -16*1 + 8*2), the retail Final id, is refused with exit 5 (rc=$rc)"

# 11. the Swift port of the engine file id matches onipack's C (opk_file_id)
cat > "$W/fid.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "onipack_format.h"
int main(int argc, char **argv) { (void)argc; printf("0x%08x\n", opk_file_id(atoi(argv[1]), argv[2])); return 0; }
EOF
cc -I tools/onipack -o "$W/fid" "$W/fid.c" || echo "FAIL: fid.c did not compile"
PARITY=""
for lv in 0 10; do for s in HD1 CharacterRetextureP Pt4Synd1 CharacterRetee5cm8v z9 Final; do
    a=$("$INST" --file-id $lv $s); b=$("$W/fid" $lv $s); [ "$a" = "$b" ] || PARITY="$PARITY $lv/$s:$a!=$b"
done; done
check '[ -z "$PARITY" ] && [ "$("$INST" --file-id 0 HD1)" = "0x01ffffc7" ]' "Swift file id == C opk_file_id for letters, digits, Final, level 10 (mismatches:$PARITY)"

# 12. the installed-pack scan matches the engine's (#111, #112): only regular
#     files count (the engine's iterator takes DT_REG only), and the suffix
#     runs to the first '.', so level0_AF.bar.dat carries id "AF". Pairs:
#     A*1+D*2 == E*1+B*2 == 9, A*1+F*2 == I*1+B*2 == 13, both at level 0.
mkdir -p "$DEST/Decoy12a/level0_AD.dat" "$DEST/Decoy12b"
: > "$DEST/Decoy12b/level0_AF.bar.dat"
for n in EB IB; do mkdir -p "$W/$n/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/$n/oni/level0_Final/"; done
"$INST" --install "$W/EB" --dest "$DEST" --gamedata none > "$W/out12a" 2>&1; rc=$?
check '[ $rc -eq 0 ]' "a directory named level0_AD.dat is not an installed leaf (rc=$rc): $(cat "$W/out12a")"
"$INST" --install "$W/IB" --dest "$DEST" --gamedata none > "$W/out12b" 2>&1; rc=$?
check '[ $rc -eq 5 ] && [ ! -d "$DEST/IB" ]' "dotted leaf level0_AF.bar.dat parsed as suffix AF, collision refused (rc=$rc)"
# a symlinked pack folder is followed (the engine stats through the link):
# A*1+H*2 == C*1+G*2 == 17 at level 0.
mkdir -p "$W/linked12"; : > "$W/linked12/level0_AH.dat"; ln -s "$W/linked12" "$DEST/Decoy12c"
mkdir -p "$W/CG/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/CG/oni/level0_Final/"
"$INST" --install "$W/CG" --dest "$DEST" --gamedata none > "$W/out12c" 2>&1; rc=$?
check '[ $rc -eq 5 ] && grep -q "Decoy12c" "$W/out12c"' "leaf inside a symlinked pack folder is scanned, collision refused naming Decoy12c (rc=$rc)"

echo "$PASS passed, $FAIL failed"; rm -rf "$W"; exit $((FAIL>0))
