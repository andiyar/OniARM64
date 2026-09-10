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

echo "$PASS passed, $FAIL failed"; rm -rf "$W"; exit $((FAIL>0))
