#!/usr/bin/env bash
# ======================================================================
# build-hd-overlays.sh — build the Curated HD texture overlay pack
#
# Issue #16 (curated HD texture bundle), pack shape per the #61 spike
# verdict: all pack TXMPs are named instances and named lookups win by
# registration order; level-0 files are always loaded and searched
# first, so ONE level0_HD1.dat uber-pack serves every level.
#
# TXMP-ONLY (issue #62): the pack stages ONLY TXMP*.oni files. Several
# mods ship whole template webs — TRBS body sets (embedding ONCC),
# ONSK skies (embedding carrier ONLV/AKEV), TXMB splash screens and a
# WMDD pause screen. An always-loaded, searched-first level0 overlay
# carrying ANY of those hijacks the engine's ByNumber template
# enumerations: a carrier ONLV with no environment gets picked as "the
# level" and NULL-crashes AKrLevel_Begin (the 2026-07-05 warehouse-load
# SIGSEGV), and 20 pack ONCCs would shadow the costume system's class
# scan. Textures are leaf templates with no outbound web — safe.
# Consequences accepted for v1: level-end splashes and the pause screen
# stay vanilla (also removes the AE-branded art), Realistic Skies'
# ONSK-based skies don't apply, and character retextures apply only
# where the mod replaces same-named TXMPs (TRBS rebind strategy is a
# follow-up).
#
# The per-level routing below (same name, distinct content across
# levelN dirs) is retained — with TXMP-only input it currently
# self-deactivates, but it guards any future same-named-per-level TXMP.
#
# Input:  extracted AE-installer mod trees under $HD_MODS_ROOT/Tier1 and
#         Tier2 — TXMP .oni files under
#         <PkgID><Name>/oni/[common/]level<N>_Final/[subdir/]
#         (%2F in filenames = URL-encoded '/'; OniSplit decodes it).
# Output: $OUT_DIR/level<N>_HD1.dat/.raw/.sep + CREDITS.txt
#
# Collision policy (first staged file wins, per basename):
#   1. Tier1 mods before Tier2 mods       — Tier1 curation is protected.
#   2. Within a tier: descending package ID — matches the AE installer
#      convention where higher-numbered mods install later and override
#      (e.g. 26002 Konoko HD Anime Face deliberately overrides the face
#      from 23951 Character Retexture Pt1).
# All shadowed copies are reported, tagged identical/different content.
#
# Usage:
#   scripts/build-hd-overlays.sh [output-dir]
#     output-dir       default: <repo>/dist/CuratedHD (gitignored)
# Env overrides:
#   HD_MODS_ROOT       default: /Users/andiyar/Developer/oni/HDTextureMods
#   ONISPLIT_EXE       default: community-svn OniSplit Release build
#   MONO_BIN           default: mono
#   PACK_SUFFIX        default: HD1   (must NOT be "Final" — the engine
#                      rejects _Final overlays; see BFW_TM_Game.c
#                      TMiGame_OverlayDir_Scan)
# ======================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

HD_MODS_ROOT="${HD_MODS_ROOT:-/Users/andiyar/Developer/oni/HDTextureMods}"
ONISPLIT_EXE="${ONISPLIT_EXE:-/Users/andiyar/Developer/oni/community-svn/Oni2/OniSplit/bin/Release/OniSplit.exe}"
MONO_BIN="${MONO_BIN:-mono}"
PACK_SUFFIX="${PACK_SUFFIX:-HD1}"
OUT_DIR="${1:-$REPO_ROOT/dist/CuratedHD}"

if [ "$PACK_SUFFIX" = "Final" ]; then
    echo "ERROR: PACK_SUFFIX must not be 'Final' (engine overlay contract)." >&2
    exit 1
fi
if [ ! -d "$HD_MODS_ROOT/Tier1" ] || [ ! -d "$HD_MODS_ROOT/Tier2" ]; then
    echo "ERROR: $HD_MODS_ROOT does not contain Tier1/ and Tier2/." >&2
    exit 1
fi
if [ ! -f "$ONISPLIT_EXE" ]; then
    echo "ERROR: OniSplit not found at $ONISPLIT_EXE." >&2
    exit 1
fi
if ! command -v "$MONO_BIN" >/dev/null 2>&1; then
    echo "ERROR: mono runtime '$MONO_BIN' not found." >&2
    exit 1
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hd-overlays.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
STAGE_ROOT="$WORK_DIR/stage"
MANIFEST="$WORK_DIR/manifest.tsv"
DECISIONS="$WORK_DIR/decisions.tsv"
mkdir -p "$STAGE_ROOT" "$OUT_DIR"

# ----------------------------------------------------------------------
# 1. Enumerate mods in priority order (Tier1 desc-ID, then Tier2 desc-ID)
#    and build a manifest: prio, mod, md5, level, path (tab-separated;
#    paths contain spaces but never tabs).
# ----------------------------------------------------------------------
echo "== Curated HD overlay build =="
echo "mods root : $HD_MODS_ROOT"
echo "output    : $OUT_DIR"
echo

MODS=()
for tier in Tier1 Tier2; do
    while IFS= read -r d; do
        [ -n "$d" ] && MODS+=("$tier/$d")
    done < <(find "$HD_MODS_ROOT/$tier" -mindepth 1 -maxdepth 1 -type d \
                 -exec basename {} \; | sort -t- -k1,1rn)
done

: > "$MANIFEST"
prio=0
total_oni=0
echo "-- per-mod .oni counts (priority order; earlier wins collisions) --"
for mod in "${MODS[@]}"; do
    count=0
    while IFS= read -r f; do
        base="$(basename "$f")"
        # Level number comes from the level<N>_Final directory component;
        # anything outside one (common/, mod root) counts as level 0.
        lvl="$(printf '%s\n' "$f" | sed -nE 's|.*/[Ll]evel([0-9]+)_[Ff]inal/.*|\1|p')"
        [ -n "$lvl" ] || lvl=0
        md5sum="$(md5 -q "$f")"
        printf '%03d\t%s\t%s\t%s\t%s\t%s\n' \
            "$prio" "$mod" "$md5sum" "$lvl" "$base" "$f" >> "$MANIFEST"
        count=$((count + 1))
    done < <(find "$HD_MODS_ROOT/$mod" -type f -name 'TXMP*.oni' | sort)   # TXMP-only, issue #62
    printf '  %-55s %5d\n' "$mod" "$count"
    total_oni=$((total_oni + count))
    prio=$((prio + 1))
done
printf '  %-55s %5d\n' "TOTAL" "$total_oni"
echo

# ----------------------------------------------------------------------
# 2. Decide winners per basename (= per instance name).
#    Emits: STAGE <level> <path>   file to stage into level<N> bucket
#           SKIP  <base> <winner> <loser> <same|diff>   shadowed copy
#           WARN  <text>           same-level distinct-content dropped
# ----------------------------------------------------------------------
sort -t "$(printf '\t')" -k5,5 -k1,1 -k6,6 "$MANIFEST" | awk -F '\t' '
    function flush() {
        if (n == 0) return
        # winner = rows whose prio == min prio (one mod: first in priority)
        minp = prio[1]; winmod = mod[1]
        # distinct md5s among winner rows
        delete seenmd5; ndistinct = 0
        for (i = 1; i <= n; i++) {
            if (prio[i] != minp) continue
            if (!(md5[i] in seenmd5)) { seenmd5[md5[i]] = 1; ndistinct++ }
        }
        if (ndistinct <= 1) {
            # single artwork: stage the first winner copy into level 0
            for (i = 1; i <= n; i++) if (prio[i] == minp) {
                print "STAGE\t0\t" path[i]; break
            }
        } else {
            # per-level distinct artwork: stage first copy per level
            delete lvlseen; staged = 0
            for (i = 1; i <= n; i++) {
                if (prio[i] != minp) continue
                if (lvl[i] in lvlseen) continue
                lvlseen[lvl[i]] = 1; staged++
                print "STAGE\t" lvl[i] "\t" path[i]
            }
            if (staged < ndistinct)
                print "WARN\t" base[1] ": " ndistinct " distinct variants but only " staged " level slots; kept first per level"
        }
        # report shadowed copies from losing mods (one line per mod)
        delete losers
        for (i = 1; i <= n; i++) {
            if (prio[i] == minp || (mod[i] in losers)) continue
            losers[mod[i]] = 1
            samediff = "diff"
            for (j = 1; j <= n; j++)
                if (prio[j] == minp && md5[j] == md5[i]) { samediff = "same"; break }
            print "SKIP\t" base[i] "\t" winmod "\t" mod[i] "\t" samediff
        }
        n = 0
    }
    {
        if (n > 0 && $5 != base[1]) flush()
        n++; prio[n] = $1; mod[n] = $2; md5[n] = $3; lvl[n] = $4; base[n] = $5; path[n] = $6
    }
    END { flush() }
' > "$DECISIONS"

# ----------------------------------------------------------------------
# 3. Stage winners into flat per-level buckets (filenames kept intact —
#    the basename IS the instance name, %2F encoding and all).
# ----------------------------------------------------------------------
staged_total=0
while IFS="$(printf '\t')" read -r kind lvl path; do
    [ "$kind" = "STAGE" ] || continue
    mkdir -p "$STAGE_ROOT/level$lvl"
    cp "$path" "$STAGE_ROOT/level$lvl/"
    staged_total=$((staged_total + 1))
done < "$DECISIONS"

echo "-- staging --"
echo "staged $staged_total unique instances into per-level buckets:"
for d in "$STAGE_ROOT"/level*; do
    printf '  %-10s %5d files\n' "$(basename "$d")" "$(find "$d" -name '*.oni' | wc -l | tr -d ' ')"
done
echo

echo "-- collisions (first-input-wins; kept the earlier-priority mod) --"
if grep -q '^SKIP' "$DECISIONS"; then
    awk -F '\t' '$1 == "SKIP" { print "  " $2 ": kept " $3 ", skipped " $4 " (" ($5 == "same" ? "identical content" : "DIFFERENT content") ")" }' "$DECISIONS"
    printf '  total shadowed copies: %s (%s with different content)\n' \
        "$(grep -c '^SKIP' "$DECISIONS")" \
        "$(awk -F '\t' '$1=="SKIP" && $5=="diff"' "$DECISIONS" | wc -l | tr -d ' ')"
else
    echo "  none"
fi
grep '^WARN' "$DECISIONS" | sed 's/^WARN\t/  WARNING: /' || true
echo

# ----------------------------------------------------------------------
# 4. Pack each level bucket with OniSplit (-import:sep = Mac .dat+.raw+.sep).
# ----------------------------------------------------------------------
echo "-- packing (OniSplit -import:sep) --"
for d in "$STAGE_ROOT"/level*; do
    lvl="${d##*level}"
    out="$OUT_DIR/level${lvl}_${PACK_SUFFIX}.dat"
    "$MONO_BIN" "$ONISPLIT_EXE" -import:sep "$d" "$out"
done
echo

# ----------------------------------------------------------------------
# 5. Verify: re-read every produced .dat with OniSplit -list and compare
#    named-instance counts against what was staged.
# ----------------------------------------------------------------------
echo "-- verification (OniSplit -list re-read) --"
listed_total=0
verify_fail=0
for d in "$STAGE_ROOT"/level*; do
    lvl="${d##*level}"
    dat="$OUT_DIR/level${lvl}_${PACK_SUFFIX}.dat"
    want="$(find "$d" -name '*.oni' | wc -l | tr -d ' ')"
    got="$("$MONO_BIN" "$ONISPLIT_EXE" -list "$dat" | wc -l | tr -d ' ')"
    listed_total=$((listed_total + got))
    status="OK"
    if [ "$got" -ne "$want" ]; then status="MISMATCH"; verify_fail=1; fi
    printf '  %-18s staged %5d  listed %5d  %s\n' "$(basename "$dat")" "$want" "$got" "$status"
done
printf '  %-18s staged %5d  listed %5d\n' "TOTAL" "$staged_total" "$listed_total"
echo

# ----------------------------------------------------------------------
# 6. CREDITS.txt from the per-mod Mod_Info.cfg Creator fields.
# ----------------------------------------------------------------------
CREDITS="$OUT_DIR/CREDITS.txt"
{
    echo "Curated HD Texture Pack (CuratedHD) — credits"
    echo "=============================================="
    echo
    echo "Community HD texture mods from http://mods.oni2.net, bundled for the"
    echo "OniARM64 port with the authors' permission. All artwork belongs to"
    echo "its creators."
    echo
    for mod in "${MODS[@]}"; do
        cfg="$(find "$HD_MODS_ROOT/$mod" -maxdepth 3 -name 'Mod_Info.cfg' | head -n 1)"
        [ -n "$cfg" ] || continue
        name="$(sed -nE 's/^NameOfMod[[:space:]]*->[[:space:]]*//p' "$cfg" | head -n 1 | tr -d '\r')"
        creator="$(sed -nE 's/^Creator[[:space:]]*->[[:space:]]*//p' "$cfg" | head -n 1 | tr -d '\r')"
        pkgid="$(basename "$mod" | cut -d- -f1)"
        printf '  %-7s %-40s by %s\n' "$pkgid" "$name" "$creator"
    done
    echo
    echo "Contributing artists: Samer, SeverED, ViciousReilly, Bozzman,"
    echo "Uroboros, EdT, Iritscen, TOCS, The Professor."
    echo
    echo "HD Screens and the Anime Face adapt original Oni artwork by"
    echo "Lorraine Reyes McLees (Bungie, 2001)."
    echo
    echo "Original game (c) Bungie / Take-Two Interactive, 2001."
} > "$CREDITS"
echo "wrote $CREDITS"
echo

# ----------------------------------------------------------------------
# 7. Size summary.
# ----------------------------------------------------------------------
echo "-- artifact sizes --"
du -h "$OUT_DIR"/*."dat" "$OUT_DIR"/*."raw" "$OUT_DIR"/*."sep" 2>/dev/null | sed 's/^/  /'
printf '  total: %s\n' "$(du -ch "$OUT_DIR"/*.dat "$OUT_DIR"/*.raw "$OUT_DIR"/*.sep 2>/dev/null | tail -n 1 | cut -f1)"
echo

if [ "$verify_fail" -ne 0 ]; then
    echo "BUILD FINISHED WITH VERIFICATION MISMATCHES (see above)." >&2
    exit 1
fi
echo "Build OK. Install by copying $OUT_DIR/* to"
echo "  ~/Library/Application Support/OniARM64/TexturePacks/CuratedHD/"
