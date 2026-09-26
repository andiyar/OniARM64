#!/usr/bin/env bash
# test_oni_texture_installer.sh — end-to-end test of the Oni Texture Installer CLI
# mode against generated fixtures laid out like real depot downloads.
# Usage: tests/test_oni_texture_installer.sh [path-to-onipack] [path-to-txmp-format-index]
# Run from the OniARM64 repo root.
set -u
ONIPACK="${1:-build/bin/onipack}"
INDEX="${2:-build/bin/txmp-format-index}"
W=$(mktemp -d); PASS=0; FAIL=0
check() { if eval "$1"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL: $2"; fi }

# Build the installer binary headless (same sources the bundle uses).
swiftc -O $(ls tools/OniTextureInstaller/*.swift | grep -v /main.swift) tools/OniTextureInstaller/main.swift \
    -framework AppKit -framework UniformTypeIdentifiers -o "$W/inst" || { echo "swiftc failed"; exit 1; }
INST="$W/inst"
# Keep the report log ($HOME/Library/Logs/OniARM64/installer.txt, #123) out of the real home.
export HOME="$W/home"; mkdir -p "$HOME"
export ONIMOD_ONIPACK="$ONIPACK" ONIMOD_INDEX="$INDEX"

# Fixture textures.
cc -Wall -DGEN_MAIN tests/test_onipack_roundtrip.c tools/onipack/onipack_oni.c \
   tools/onipack/onipack_writer.c -o "$W/gen" && mkdir -p "$W/scr" && "$W/gen" "$W" "$W/scr"

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

# 13. HD Screens re-lay-out (#113): a mod TXMB whose grid differs from the
#     retail TXMB of the same name gets its tiles skipped (union of mod and
#     retail tile names); a same-grid screen keeps its tiles. The fake retail
#     folder holds one TXMB .oni per level file, which the index tool reads.
#     Retail screenB also names screenB_extra, which this mod doesn't ship.
mkdir -p "$W/gd13"
cp "$W/scr/retailTXMBscreenB.oni" "$W/gd13/level0_Final.dat"
cp "$W/scr/TXMBscreenA.oni" "$W/gd13/level1_Final.dat"
cp "$W/scr/TXMBscreenC.oni" "$W/gd13/level2_Final.dat"
S="$W/screens-mod"; mkdir -p "$S/oni/common/level0_Final"
cp "$W/scr/TXMBscreenB.oni" "$W/scr/TXMBscreenC.oni" "$S/oni/common/level0_Final/"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do cp "$W/scr/TXMPscreenB$i.oni" "$S/oni/common/level0_Final/"; done
for i in 1 2 3 4 5 6; do cp "$W/scr/TXMPscreenC$i.oni" "$S/oni/common/level0_Final/"; done
"$INST" --install "$S" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13" 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -q "screen check: on (retail screens indexed)" "$W/out13"' "re-laid-out screen mod installs, screen check on (rc=$rc): $(cat "$W/out13")"
check 'grep -qx "  screen tiles skipped: 12 (this mod re-lays-out the screen; not supported yet, see #121)" "$W/out13"' "report says 12 screen tiles skipped: $(cat "$W/out13")"
N=$("$INDEX" "$DEST/screensmod/level0_screensmod.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ "$N" = "6" ]' "level0 pack holds only the 6 same-grid tiles (got $N)"

# 13b. variant: the mod also ships TXMPscreenB_extra, named only by retail's
#      screenB TXMB; the union rule skips it too (13 skipped, 6 packed).
X="$W/screens-extra"; mkdir -p "$X"; cp -R "$S/oni" "$X/"
cp "$W/scr/TXMPscreenB_extra.oni" "$X/oni/common/level0_Final/"
"$INST" --install "$X" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13b" 2>&1; rc=$?
N=$("$INDEX" "$DEST/screensextra/level0_screensextra.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ $rc -eq 0 ] && grep -q "screen tiles skipped: 13 " "$W/out13b"' "retail-only tile name screenB_extra is skipped too (rc=$rc): $(cat "$W/out13b")"
check '[ "$N" = "6" ]' "variant level0 pack holds only the 6 same-grid tiles (got $N)"

# 13c. the HD Screens chrome (#113): buttons/navi restyled to match the
#      re-laid-out screens go with the skipped tiles; an ordinary texture stays.
CA="$W/chrome-a"; mkdir -p "$CA/oni/level0_Final"
cp "$W/scr/TXMBscreenB.oni" "$CA/oni/level0_Final/"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do cp "$W/scr/TXMPscreenB$i.oni" "$CA/oni/level0_Final/"; done
cp "$W/TXMPcliA.oni" "$CA/oni/level0_Final/TXMPbuttons.oni"
cp "$W/TXMPcliA.oni" "$CA/oni/level0_Final/TXMPnavi.oni"
cp "$W/TXMPcliA.oni" "$CA/oni/level0_Final/"
"$INST" --install "$CA" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13c" 2>&1; rc=$?
N=$("$INDEX" "$DEST/chromea/level0_chromea.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ $rc -eq 0 ] && grep -q "  menu chrome skipped: 2 " "$W/out13c"' "buttons/navi skipped with the re-laid-out screen (rc=$rc): $(cat "$W/out13c")"
check '[ "$N" = "1" ]' "chrome-a level0 pack holds only the ordinary texture (got $N)"

# 13c'. same, with mixed-case chrome names: the match ignores case.
CD="$W/chrome-d"; mkdir -p "$CD/oni/level0_Final"
cp "$W/scr/TXMBscreenB.oni" "$CD/oni/level0_Final/"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do cp "$W/scr/TXMPscreenB$i.oni" "$CD/oni/level0_Final/"; done
cp "$W/TXMPcliA.oni" "$CD/oni/level0_Final/TXMPButtons.oni"
cp "$W/TXMPcliA.oni" "$CD/oni/level0_Final/TXMPNAVI.oni"
cp "$W/TXMPcliA.oni" "$CD/oni/level0_Final/"
"$INST" --install "$CD" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13c2" 2>&1; rc=$?
N=$("$INDEX" "$DEST/chromed/level0_chromed.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ $rc -eq 0 ] && grep -q "  menu chrome skipped: 2 " "$W/out13c2"' "mixed-case Buttons/NAVI skipped with the re-laid-out screen (rc=$rc): $(cat "$W/out13c2")"
check '[ "$N" = "1" ]' "chrome-d level0 pack holds only the ordinary texture (got $N)"

# 13d. no screen mismatch: the chrome installs unchanged, no chrome line.
CB="$W/chrome-b"; mkdir -p "$CB/oni/level0_Final"
cp "$W/TXMPcliA.oni" "$CB/oni/level0_Final/TXMPbuttons.oni"
cp "$W/TXMPcliA.oni" "$CB/oni/level0_Final/TXMPnavi.oni"
"$INST" --install "$CB" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13d" 2>&1; rc=$?
N=$("$INDEX" "$DEST/chromeb/level0_chromeb.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ $rc -eq 0 ] && ! grep -q "menu chrome skipped" "$W/out13d" && [ "$N" = "2" ]' "same-grid mod keeps buttons/navi (rc=$rc, got $N): $(cat "$W/out13d")"

# 13e. re-laid-out screen plus chrome and nothing else: refused as a screen
#      mod, and the message counts the chrome.
CC="$W/chrome-c"; mkdir -p "$CC/oni/level0_Final"
cp "$W/scr/TXMBscreenB.oni" "$CC/oni/level0_Final/"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do cp "$W/scr/TXMPscreenB$i.oni" "$CC/oni/level0_Final/"; done
cp "$W/TXMPcliA.oni" "$CC/oni/level0_Final/TXMPbuttons.oni"
"$INST" --install "$CC" --dest "$DEST" --gamedata "$W/gd13" > "$W/out13e" 2>&1; rc=$?
check '[ $rc -eq 1 ] && grep -q "(12 tiles" "$W/out13e" && grep -qF "plus 1 menu chrome texture)" "$W/out13e"' "screen-plus-chrome-only mod refused, chrome counted (rc=$rc): $(cat "$W/out13e")"

# 14. same mod, no retail data: nothing is skipped, all 18 tiles packed.
"$INST" --install "$S" --dest "$DEST" --gamedata none --replace > "$W/out14" 2>&1; rc=$?
N=$("$INDEX" "$DEST/screensmod/level0_screensmod.dat" 2>/dev/null | wc -l | tr -d ' ')
check '[ $rc -eq 0 ] && [ "$N" = "18" ]' "no retail data: all 18 tiles packed (rc=$rc, got $N)"
check '! grep -q "screen tiles skipped" "$W/out14" && grep -q "screen check: off" "$W/out14"' "no retail data: no screen-skip line, screen check off"

# 15. a mod that is only a re-laid-out screen: refused as a screen mod
#     (exit 1), not as "no textures".
O="$W/screens-only"; mkdir -p "$O/oni/level0_Final"
cp "$W/scr/TXMBscreenB.oni" "$O/oni/level0_Final/"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do cp "$W/scr/TXMPscreenB$i.oni" "$O/oni/level0_Final/"; done
"$INST" --install "$O" --dest "$DEST" --gamedata "$W/gd13" > "$W/out15" 2>&1; rc=$?
check '[ $rc -eq 1 ] && grep -q "only re-lays-out screens" "$W/out15" && grep -q "(12 tiles" "$W/out15"' "screens-only mod refused as a screen mod (rc=$rc): $(cat "$W/out15")"

# 16. legacy long-named pack (#120): before the 19-char cap a 30-char name
#     was installed under its plain 32-char-capped name. A reinstall must find
#     that folder: refused as already installed without --replace (nothing
#     new written), and with --replace the old folder is removed and noted.
DEST16="$W/dest16"; L="$W/legacy16"
mkdir -p "$L/oni/common/level0_Final" "$DEST16/BetterWarehouseTrainingRooms12"
printf 'NameOfMod -> BetterWarehouseTrainingRooms12\n' > "$L/Mod_Info.cfg"
cp "$W/TXMPcliA.oni" "$L/oni/common/level0_Final/"
touch "$DEST16/BetterWarehouseTrainingRooms12/level0_BetterWarehouseTrainingRooms12.dat"
"$INST" --install "$L" --dest "$DEST16" --gamedata none > "$W/out16a" 2>&1; rc=$?
check '[ $rc -eq 4 ] && [ ! -d "$DEST16/BetterWarehouwf6c4j" ]' "legacy long-named folder counts as already installed, nothing new written (rc=$rc): $(cat "$W/out16a")"
"$INST" --install "$L" --dest "$DEST16" --gamedata none --replace > "$W/out16b" 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ ! -d "$DEST16/BetterWarehouseTrainingRooms12" ] && [ -f "$DEST16/BetterWarehouwf6c4j/level0_BetterWarehouwf6c4j.dat" ]' "--replace migrates the legacy folder to the shortened pack (rc=$rc): $(ls "$DEST16" | tr '\n' ' ')"
check 'grep -q "removed old pack folder BetterWarehouseTrainingRooms12" "$W/out16b"' "report notes the removed legacy folder: $(cat "$W/out16b")"
# a short name (legacy form == new form) installs with no migration note
mkdir -p "$W/TestModA16/oni/level0_Final"; cp "$W/TXMPcliA.oni" "$W/TestModA16/oni/level0_Final/"
"$INST" --install "$W/TestModA16" --dest "$DEST16" --gamedata none > "$W/out16c" 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ -d "$DEST16/TestModA16" ] && ! grep -q "old pack folder" "$W/out16c"' "short name installs with no migration note (rc=$rc)"


# 17. every CLI run appends its report to $HOME/Library/Logs/OniARM64/installer.txt (#123)
H17="$W/home17"; mkdir -p "$H17"
HOME="$H17" "$INST" --install "$W/23999-Test-Mod-A.zip" --dest "$W/dest17" --gamedata none > "$W/out17" 2>&1; rc=$?
LOG17="$H17/Library/Logs/OniARM64/installer.txt"
check '[ $rc -eq 0 ] && [ -f "$LOG17" ]' "install writes the log file under HOME (rc=$rc)"
check 'grep -q "^=== " "$LOG17" && grep -q "23999-Test-Mod-A.zip" "$LOG17" && grep -q "Installed \"Test Mod A!\"" "$LOG17"' "log entry has a timestamped header, the source, and the report text"
HOME="$H17" "$INST" --install "$W/empty-mod" --dest "$W/dest17" --gamedata none >/dev/null 2>&1
check '[ "$(grep -c "^=== " "$LOG17")" = "2" ] && grep -qi "no texture" "$LOG17"' "a failed run is logged too (2 entries)"

# 18. Depot index parser against a checked-in copy of jsoncache.zip (no network)
"$INST" --parse-index tests/fixtures/depot/jsoncache.zip > "$W/out18" 2>"$W/err18"; rc=$?
check '[ $rc -eq 0 ] && [ "$(wc -l < "$W/out18" | tr -d " ")" = "36" ]' "36 texture packages in Package format parsed (rc=$rc, got $(wc -l < "$W/out18")): $(cat "$W/err18")"
check 'grep -q "^70000	HD Screens	" "$W/out18"' "HD Screens row: number, tab, title"
check 'awk -F"\t" "\$7 !~ /^http:\/\/mods.oni2.net\/system\/files\/.*\.zip\$/ {bad++} END {exit bad>0}" "$W/out18"' "every row has a Depot download URL in column 7"
check '! grep -q "sky dome test" "$W/out18"' "a file-swap texture mod is excluded (Package format only)"
check '! grep -qi "Kanabo" "$W/out18"' "a Tool package is excluded"
check 'sort -t"	" -k2,2f "$W/out18" | diff -q - "$W/out18" >/dev/null' "rows sorted by title, case-insensitive"
mkdir -p "$W/badidx"; (cd "$W/badidx" && printf '[]' > vocabulary.json && ditto -c -k . "$W/bad.zip")
"$INST" --parse-index "$W/bad.zip" >/dev/null 2>"$W/err18b"; rc=$?
check '[ $rc -eq 3 ] && grep -qi "nodes.json\|vocabulary\|term" "$W/err18b"' "a zip without the index files exits 3 with a message naming what is missing (rc=$rc)"

# 19. installed-pack scan: one installed pack, one hand-made folder (no Mod_Info.txt), one empty folder
D19="$W/dest19"; mkdir -p "$D19"
"$INST" --install "$W/23999-Test-Mod-A.zip" --dest "$D19" --gamedata none >/dev/null 2>&1
mkdir -p "$D19/ByHand" && cp "$D19/TestModA/level0_TestModA.dat" "$D19/ByHand/level0_ByHand.dat" && cp "$D19/TestModA/level3_TestModA.dat" "$D19/ByHand/level3_ByHand.dat"
mkdir -p "$D19/Empty" "$D19/.hidden"
"$INST" --list-installed "$D19" > "$W/out19" 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ "$(wc -l < "$W/out19" | tr -d " ")" = "3" ]' "three packs listed, hidden folder skipped (rc=$rc): $(cat "$W/out19")"
check 'grep -q "^TestModA	2	[1-9][0-9]*	file:23999-Test-Mod-A.zip" "$W/out19"' "installed pack: name, 2 levels, size, source from Mod_Info.txt"
check 'grep -q "^ByHand	2	[1-9][0-9]*	by hand$" "$W/out19"' "hand-made pack shows with 'by hand'"
check 'grep -q "^Empty	0	0	by hand$" "$W/out19"' "empty folder shows with 0 levels"
"$INST" --list-installed "$W/does-not-exist" > "$W/out19b" 2>&1; rc=$?
check '[ $rc -eq 0 ] && [ ! -s "$W/out19b" ]' "missing TexturePacks dir lists nothing, exit 0 (rc=$rc)"

# 20. batch install and Mod_Info.txt source lines
D20="$W/dest20"; H20="$W/home20"; mkdir -p "$H20"
HOME="$H20" "$INST" --install "$W/23999-Test-Mod-A.zip" "$W/24001-Second Mod" "$W/empty-mod" --dest "$D20" --gamedata none > "$W/out20" 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -q "^Installed \"Test Mod A!\"" "$W/out20" && grep -q "^Installed \"Second Mod\"" "$W/out20"' "batch of three prints a section per item (rc=$rc)"
check 'grep -q "^2 installed, 0 skipped, 1 failed\.$" "$W/out20"' "trailing count line"
check 'grep -q "^Source: 23999-Test-Mod-A.zip$" "$D20/TestModA/Mod_Info.txt"' "Mod_Info.txt records the source file"
check '[ "$(grep -c "^=== " "$H20/Library/Logs/OniARM64/installer.txt")" = "4" ]' "each batch item logged separately, plus one batch summary"
HOME="$H20" "$INST" --install "$W/23999-Test-Mod-A.zip" --source-depot 70000 --dest "$D20" --gamedata none --replace >/dev/null 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -q "^DepotPackage: 70000$" "$D20/TestModA/Mod_Info.txt"' "--source-depot writes DepotPackage into Mod_Info.txt (rc=$rc)"
check '"$INST" --list-installed "$D20" | grep -q "^TestModA	2	[0-9]*	Depot 70000$"' "the scan reports the Depot number as the source"
HOME="$H20" "$INST" --install "$W/empty-mod" "$W/empty-mod" --dest "$D20" --gamedata none >/dev/null 2>&1; rc=$?
check '[ $rc -eq 1 ]' "all-failed batch exits with the first failure code (rc=$rc)"

# 21. index cache: a cached zip is parsed and dated; an empty cache reports none
C21="$W/cache21"; mkdir -p "$C21"; cp tests/fixtures/depot/jsoncache.zip "$C21/jsoncache.zip"; echo "2026-09-26T00:00:00Z" > "$C21/index-date.txt"
check '[ "$("$INST" --cache-info "$C21")" = "2026-09-26T00:00:00Z	36" ]' "cached index reports its date and package count"
check '[ "$("$INST" --cache-info "$W/nocache")" = "none" ]' "no cache reports none"
# polish: creator trimmed, JSON shape error wrapped
check '! grep -q " 	\|	 \| $" "$W/out18"' "no creator or field with leading/trailing space in the parsed rows"
mkdir -p "$W/badjson"; (cd "$W/badjson" && printf '{}' > vocabulary.json && printf '[]' > terms.json && printf '[]' > nodes.json && printf '[]' > files.json && ditto -c -k . "$W/badjson.zip")
"$INST" --parse-index "$W/badjson.zip" >/dev/null 2>"$W/err21"; rc=$?
check '[ $rc -eq 3 ] && grep -q "not the JSON shape" "$W/err21"' "a JSON file of the wrong shape reports the shape error, not a raw Cocoa error (rc=$rc): $(cat "$W/err21")"

echo "$PASS passed, $FAIL failed"; rm -rf "$W"; exit $((FAIL>0))
