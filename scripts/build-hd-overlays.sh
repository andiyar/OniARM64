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
# ALPHA-MASK GUARD (issue #63): retail alpha-carrying textures (e.g.
# BGRA4444 faces/hair/glass) use the alpha channel as the env-map
# shininess mask. A mod that replaces one with an alpha-less format
# (BGR/DXT1) makes the engine expand A=1.0, so the additive env-map
# pass blows out white. The script indexes retail texel formats per
# TXMP name (scripts/txmp-format-index.c, reading the .dats directly);
# since #88 the DROP itself happens inside onipack at pack time
# (--alpha-guard), which logs each as ALPHA-SKIP and hard-fails if the
# index loads empty. Those surfaces stay vanilla; a v2 could composite
# the retail alpha onto the mod RGB instead.
#
# PACKING is native since #88: tools/onipack import-sep writes the
# .dat/.raw/.sep triple directly — no Mono, no OniSplit in this script.
#
# Input:  extracted AE-installer mod trees under $HD_MODS_ROOT/Tier1 and
#         Tier2 — TXMP .oni files under
#         <PkgID><Name>/oni/[common/]level<N>_Final/[subdir/]
#         (%2F in filenames = URL-encoded '/'; the packer decodes it).
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
#   ONIPACK_EXE        default: <repo>/build/bin/onipack (make onipack)
#   PACK_SUFFIX        default: HD1   (must NOT be "Final" — the engine
#                      rejects _Final overlays; see BFW_TM_Game.c
#                      TMiGame_OverlayDir_Scan)
#   RETAIL_DATA_DIR    default: CXOni PC reference install GameDataFolder
#                      (source of the retail texel-format index, #63)
#   RETAIL_INDEX       default: <repo>/dist/retail-txmp-formats.tsv —
#                      cached name<TAB>format<TAB>source index consumed
#                      by onipack --alpha-guard; a pre-#88 2-column
#                      cache is detected and rebuilt automatically
#   FORCE_RETAIL_INDEX set to 1 to rebuild the index even if cached
# ======================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

HD_MODS_ROOT="${HD_MODS_ROOT:-/Users/andiyar/Developer/oni/HDTextureMods}"
ONIPACK_EXE="${ONIPACK_EXE:-$REPO_ROOT/build/bin/onipack}"
PACK_SUFFIX="${PACK_SUFFIX:-HD1}"
OUT_DIR="${1:-$REPO_ROOT/dist/CuratedHD}"
RETAIL_DATA_DIR="${RETAIL_DATA_DIR:-/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/GameDataFolder}"
RETAIL_INDEX="${RETAIL_INDEX:-$REPO_ROOT/dist/retail-txmp-formats.tsv}"
FORCE_RETAIL_INDEX="${FORCE_RETAIL_INDEX:-0}"
# Instances dropped from the overlay so the engine uses the vanilla base
# version instead (issue #63). HD-Screens' menu widgets ship colour-swapped
# (red where vanilla is blue) and are only a same-resolution restyle, so
# reverting the menu chrome to vanilla costs no quality while keeping the
# mod's HD splash/level-intro/win screens. Space-separated instance names.
TEXTURE_EXCLUDE="${TEXTURE_EXCLUDE:-buttons navi}"
# Directory of our channel-corrected TXMP overrides (issue #63); staged last
# so they win. Holds R/B-corrected HD textures worth keeping (e.g. the blue
# bike) — contrast TEXTURE_EXCLUDE which reverts same-res restyle junk.
CORRECTIONS_DIR="${CORRECTIONS_DIR:-$REPO_ROOT/hd-pack-corrections}"

if [ "$PACK_SUFFIX" = "Final" ]; then
    echo "ERROR: PACK_SUFFIX must not be 'Final' (engine overlay contract)." >&2
    exit 1
fi
if [ ! -d "$HD_MODS_ROOT/Tier1" ] || [ ! -d "$HD_MODS_ROOT/Tier2" ]; then
    echo "ERROR: $HD_MODS_ROOT does not contain Tier1/ and Tier2/." >&2
    exit 1
fi
if [ ! -x "$ONIPACK_EXE" ]; then
    echo "ERROR: onipack not built at $ONIPACK_EXE — run: cd build && make onipack" >&2
    exit 1
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hd-overlays.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
STAGE_ROOT="$WORK_DIR/stage"
MANIFEST="$WORK_DIR/manifest.tsv"
DECISIONS="$WORK_DIR/decisions.tsv"
mkdir -p "$STAGE_ROOT" "$OUT_DIR"

# ----------------------------------------------------------------------
# 0. Retail texel-format index (issue #63) — one line per named retail
#    TXMP: name<TAB>format<TAB>source. Consumed by onipack --alpha-guard
#    (since #88 the downgrade drop lives in the tool; the script only
#    builds the index). onipack keeps the FIRST line per name, so
#    cross-level conflicts (same name, different format across retail
#    levels) must be resolved HERE, same rule as always: keep the
#    alpha-carrying format — that's the mask the guard protects.
#    Cached in $RETAIL_INDEX; rebuilt when missing, when the cache is
#    the pre-#88 2-column shape, or when FORCE_RETAIL_INDEX=1.
# ----------------------------------------------------------------------
TXMP_TOOL="$WORK_DIR/txmp-format-index"
cc -O2 -o "$TXMP_TOOL" "$SCRIPT_DIR/txmp-format-index.c" || {
    echo "ERROR: failed to compile scripts/txmp-format-index.c" >&2; exit 1; }

need_index=1
if [ -s "$RETAIL_INDEX" ] && [ "$FORCE_RETAIL_INDEX" != "1" ]; then
    if head -n 1 "$RETAIL_INDEX" | awk -F '\t' 'NF >= 3 { exit 0 } { exit 1 }'; then
        need_index=0
    else
        echo "-- retail index cache is the pre-#88 2-column shape; rebuilding --"
    fi
fi
if [ "$need_index" = "1" ]; then
    if [ ! -d "$RETAIL_DATA_DIR" ]; then
        echo "ERROR: retail data dir not found: $RETAIL_DATA_DIR" >&2
        exit 1
    fi
    echo "-- building retail TXMP format index --"
    mkdir -p "$(dirname "$RETAIL_INDEX")"
    "$TXMP_TOOL" "$RETAIL_DATA_DIR"/level*_Final.dat > "$WORK_DIR/retail-raw.tsv"
    awk -F '\t' '
        function alpha(f) { return (f == "BGRA4444" || f == "BGRA5551" || f == "RGBA" || f == "A8" || f == "A4I4") }
        $1 == "-" { next }   # unnamed instances cannot be overlaid by name
        {
            if (!($1 in fmt)) { fmt[$1] = $2; order[++n] = $1 }
            else if (fmt[$1] != $2) {
                keep = (alpha($2) && !alpha(fmt[$1])) ? $2 : fmt[$1]
                printf "  NOTE: %s format differs across levels (%s vs %s); keeping %s\n", \
                    $1, fmt[$1], $2, keep > "/dev/stderr"
                fmt[$1] = keep
            }
        }
        END { for (i = 1; i <= n; i++) print order[i] "\t" fmt[order[i]] "\tretail-dedup" }
    ' "$WORK_DIR/retail-raw.tsv" | sort > "$RETAIL_INDEX"
    echo "  wrote $RETAIL_INDEX ($(wc -l < "$RETAIL_INDEX" | tr -d ' ') named TXMPs)"
    echo
else
    echo "-- retail TXMP format index: cached $RETAIL_INDEX ($(wc -l < "$RETAIL_INDEX" | tr -d ' ') names; FORCE_RETAIL_INDEX=1 rebuilds) --"
    echo
fi

# ----------------------------------------------------------------------
# 1. Enumerate mods in priority order (Tier1 desc-ID, then Tier2 desc-ID)
#    and build a manifest: prio, mod, md5, level, path (tab-separated;
#    paths contain spaces but never tabs).
# ----------------------------------------------------------------------
echo "== Curated HD overlay build =="
echo "mods root : $HD_MODS_ROOT"
echo "output    : $OUT_DIR"
echo

# Mods excluded entirely (issue #63): sky/skybox mods must NOT be packed.
# Retail sky images (night_*, day_*, …) are the environment reflection
# source sampled by every env-mapped surface — the shiny anime face/hair
# and vehicle paint. Overlaying HD sky TXMPs (which win by name even
# though the mod's ONSK template was dropped under #62) shifts every
# reflection at once: level-6's intro blew Konoko's head white and
# recoloured the motorbike (a vehicle with no overridden texture of its
# own — proof the change came through the shared reflection, not a skin).
# Skies were already out-of-v1-scope (#62 notes); this makes the pack
# match that. MOD_EXCLUDE is a '|'-separated substring list, overridable.
MOD_EXCLUDE="${MOD_EXCLUDE:-Realistic-Skies|RealSkies}"

MODS=()
for tier in Tier1 Tier2; do
    while IFS= read -r d; do
        [ -z "$d" ] && continue
        if printf '%s\n' "$d" | grep -qE "$MOD_EXCLUDE"; then
            echo "  (excluded mod: $tier/$d — see MOD_EXCLUDE / #63)"
            continue
        fi
        MODS+=("$tier/$d")
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

# (The former §2b staging-time alpha drop-loop is gone: since #88 the
#  guard lives in ONE place — onipack applies it at pack time from
#  $RETAIL_INDEX and logs ALPHA-SKIP per dropped texture. Note the
#  tool logs only drops; the old informational ALPHA-ADD lines are no
#  longer emitted.)

# ----------------------------------------------------------------------
# 3. Stage winners into flat per-level buckets (filenames kept intact —
#    the basename IS the instance name, %2F encoding and all).
# ----------------------------------------------------------------------
staged_total=0
excluded_total=0
while IFS="$(printf '\t')" read -r kind lvl path; do
    [ "$kind" = "STAGE" ] || continue
    # Texture-level exclusion (issue #63): drop specific instances from the
    # overlay so the engine falls back to the vanilla base version. Used to
    # revert HD-Screens' colour-swapped menu widgets (buttons/navi render red
    # where vanilla is blue; the mod is a same-resolution restyle, so vanilla
    # loses no quality) while keeping the rest of the pack. Instance name =
    # basename minus the TXMP prefix and .oni suffix.
    inst="$(basename "$path" .oni)"; inst="${inst#TXMP}"
    skip=0
    for ex in $TEXTURE_EXCLUDE; do
        if [ "$inst" = "$ex" ]; then skip=1; break; fi
    done
    if [ "$skip" = "1" ]; then
        excluded_total=$((excluded_total + 1))
        continue
    fi
    mkdir -p "$STAGE_ROOT/level$lvl"
    cp "$path" "$STAGE_ROOT/level$lvl/"
    staged_total=$((staged_total + 1))
done < "$DECISIONS"

# ----------------------------------------------------------------------
# 3b. Pack corrections (issue #63): our own channel-corrected TXMPs copied
#     in LAST so they OVERRIDE the staged mod winner for the same instance
#     name — e.g. the level-6 intro bike TXMPMOTORCYCLE02 (24104-HQ-Airport)
#     ships BGR with R/B swapped (red body, should be blue). Unlike the menu
#     widgets this is a genuine 512x512 HD texture, so we keep it and
#     R/B-correct it rather than revert to vanilla. Committed under
#     hd-pack-corrections/level<N>/; regenerate with build-hd-corrections.sh.
# ----------------------------------------------------------------------
corr_total=0
if [ -d "$CORRECTIONS_DIR" ]; then
    while IFS= read -r cf; do
        cbase="$(basename "$cf")"
        clvl="$(printf '%s\n' "$cf" | sed -nE 's|.*/level([0-9]+)(_[Ff]inal)?/.*|\1|p')"
        [ -n "$clvl" ] || clvl=0
        mkdir -p "$STAGE_ROOT/level$clvl"
        cp "$cf" "$STAGE_ROOT/level$clvl/$cbase"
        corr_total=$((corr_total + 1))
    done < <(find "$CORRECTIONS_DIR" -name 'TXMP*.oni')
fi

echo "-- staging --"
if [ "$excluded_total" -gt 0 ]; then
    echo "excluded $excluded_total instance(s) via TEXTURE_EXCLUDE=\"$TEXTURE_EXCLUDE\" -> vanilla base used (#63)"
fi
if [ "$corr_total" -gt 0 ]; then
    echo "applied $corr_total pack correction(s) overriding staged winners (#63)"
fi
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
# 4. Pack each level bucket with onipack import-sep (native .dat+.raw+.sep;
#    replaces OniSplit+Mono, #88). The alpha-mask guard (#63) runs IN the
#    tool: it drops downgrades itself, logging each as ALPHA-SKIP on
#    stderr, and hard-fails if the retail index loads empty. onipack
#    exits 3 when it packed but skipped some inputs — surfaced as a
#    warning, not a build failure (each SKIP line names file + reason).
# ----------------------------------------------------------------------
echo "-- packing (onipack import-sep) --"
for d in "$STAGE_ROOT"/level*; do
    lvl="${d##*level}"
    out="$OUT_DIR/level${lvl}_${PACK_SUFFIX}.dat"
    rc=0
    "$ONIPACK_EXE" import-sep --alpha-guard "$RETAIL_INDEX" "$d" "$out" || rc=$?
    if [ "$rc" -eq 3 ]; then
        echo "  WARNING: onipack skipped some inputs for $(basename "$out") (see SKIP lines above)" >&2
    elif [ "$rc" -ne 0 ]; then
        echo "ERROR: onipack failed for $(basename "$out") (exit $rc)" >&2
        exit "$rc"
    fi
done
echo

# ----------------------------------------------------------------------
# 5. Verify: independent cross-parse of every produced .dat — our
#    txmp-format-index vs kanabo (Iritscen's reader) via the parent-repo
#    harness. Replaces the OniSplit -list re-read; a mismatch or parse
#    failure aborts the build (set -e).
# ----------------------------------------------------------------------
echo "-- verification (txmp-format-index + kanabo cross-parse) --"
VERIFY_PACK="${VERIFY_PACK:-/Users/andiyar/Developer/oni/scripts/verify-pack.sh}"
for f in "$OUT_DIR"/level*_"${PACK_SUFFIX}".dat; do
    "$VERIFY_PACK" "$f"
done
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

echo "Build OK. Install by copying $OUT_DIR/* to"
echo "  ~/Library/Application Support/OniARM64/TexturePacks/CuratedHD/"
