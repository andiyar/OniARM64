#!/usr/bin/env bash
# ======================================================================
# build-hd-corrections.sh — regenerate channel-corrected TXMP overrides
# for the Curated HD pack (issue #63).
#
# THE PROBLEM
#   Some bundled community HD mods ship textures whose RED and BLUE
#   channels are swapped vs the vanilla design. Our engine renders the
#   stored bytes faithfully (proven: OniSplit's BGR path round-trips and
#   the RGB888->RGB_Bytes converter is byte-order correct), so it is bad
#   data in the mod — the fix belongs in the pack, not the renderer.
#
#   For SAME-RESOLUTION restyle junk (HD-Screens' menu widgets) the pack
#   build's TEXTURE_EXCLUDE reverts to vanilla (no quality lost). For a
#   GENUINE HD texture worth keeping we R/B-correct it here instead:
#     - TXMPMOTORCYCLE02 (24104-HQ-Airport, 512x512 BGR): the level-6
#       intro bike ships a red body (#BC2A2A) that R/B-swaps to a coherent
#       blue "RONIN" bike. Full-texture swap ("all").
#
# OUTPUT
#   $OUT_DIR/level<N>/TXMP<name>.oni — committed to the repo and staged
#   LAST (highest priority) by build-hd-overlays.sh, overriding the mod.
#
# DEPS: OniSplit (mono) + ImageMagick (magick). The committed .oni files
#   are what the normal pack build uses; only regeneration needs these.
#
# ADD A CORRECTION: append a row to CORRECTIONS below —
#   name | source .oni (relative to HD_MODS_ROOT) | level | region | format | mip
#   region = "all" (whole image) or WxH+X+Y (swap that rectangle only).
# ======================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

HD_MODS_ROOT="${HD_MODS_ROOT:-/Users/andiyar/Developer/oni/HDTextureMods}"
ONISPLIT_EXE="${ONISPLIT_EXE:-/Users/andiyar/Developer/oni/community-svn/Oni2/OniSplit/bin/Release/OniSplit.exe}"
MONO_BIN="${MONO_BIN:-mono}"
MAGICK_BIN="${MAGICK_BIN:-magick}"
OUT_DIR="${1:-$REPO_ROOT/hd-pack-corrections}"

for t in "$MONO_BIN" "$MAGICK_BIN"; do
    command -v "$t" >/dev/null 2>&1 || { echo "ERROR: '$t' not found." >&2; exit 1; }
done
[ -f "$ONISPLIT_EXE" ] || { echo "ERROR: OniSplit not found at $ONISPLIT_EXE." >&2; exit 1; }

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hd-corr.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
RB_SWAP="0 0 1 0 0  0 1 0 0 0  1 0 0 0 0  0 0 0 1 0  0 0 0 0 1"   # swap R<->B, keep G+A

# name | source (rel to HD_MODS_ROOT) | level | region | format | mip(1/0)
CORRECTIONS="\
MOTORCYCLE02|Tier2/24104-HQ-Airport-Textures/24104HQAirportTextures/oni/level0_Final/TXMPMOTORCYCLE02.oni|0|all|bgr|1"

echo "== HD pack corrections (issue #63) =="
echo "output: $OUT_DIR"; echo

printf '%s\n' "$CORRECTIONS" | while IFS='|' read -r name src lvl region fmt mip; do
    [ -n "$name" ] || continue
    srcpath="$HD_MODS_ROOT/$src"
    [ -f "$srcpath" ] || { echo "ERROR: source not found for '$name': $srcpath" >&2; exit 1; }

    tdir="$WORK_DIR/$name"; mkdir -p "$tdir"
    "$MONO_BIN" "$ONISPLIT_EXE" -extract:xml "$tdir" "$srcpath" >/dev/null 2>&1
    srctga="$(find "$tdir" -name 'TXMP'"$name"'.tga' | head -n1)"
    [ -n "$srctga" ] || { echo "ERROR: could not extract TGA for '$name'." >&2; exit 1; }

    # BGR has no alpha; keep it TrueColor. Alpha formats keep the channel.
    if [ "$fmt" = "bgr" ] || [ "$fmt" = "dxt1" ]; then otype=TrueColor; else otype=TrueColorAlpha; fi
    fixed="$tdir/${name}.tga"
    if [ "$region" = "all" ]; then
        "$MAGICK_BIN" "$srctga" -color-matrix "$RB_SWAP" -type "$otype" -depth 8 -compress none "$fixed"
    else
        wh="${region%%+*}"; w="${wh%%x*}"; h="${wh##*x}"
        xy="${region#*+}"; x="${xy%%+*}"; y="${xy##*+}"
        fulldim="$("$MAGICK_BIN" "$srctga" -format '%wx%h' info:)"; fw="${fulldim%%x*}"; fh="${fulldim##*x}"
        stack=""
        [ "$y" -gt 0 ] && { "$MAGICK_BIN" "$srctga" -crop "${fw}x${y}+0+0" +repage "$tdir/a.png"; stack="$stack $tdir/a.png"; }
        "$MAGICK_BIN" "$srctga" -crop "${w}x${h}+${x}+${y}" +repage -color-matrix "$RB_SWAP" "$tdir/b.png"; stack="$stack $tdir/b.png"
        below=$(( fh - y - h ))
        [ "$below" -gt 0 ] && { "$MAGICK_BIN" "$srctga" -crop "${fw}x${below}+0+$(( y + h ))" +repage "$tdir/c.png"; stack="$stack $tdir/c.png"; }
        # shellcheck disable=SC2086
        "$MAGICK_BIN" $stack -append -type "$otype" -depth 8 -compress none "$fixed"
    fi

    outdir="$OUT_DIR/level$lvl"; mkdir -p "$outdir"
    mipflag=""; [ "$mip" = "1" ] || mipflag="-nomipmaps"
    # shellcheck disable=SC2086
    "$MONO_BIN" "$ONISPLIT_EXE" -create:txmp "$outdir" $mipflag -format:"$fmt" "$fixed" >/dev/null 2>&1
    [ -f "$outdir/TXMP${name}.oni" ] || { echo "ERROR: OniSplit did not produce TXMP${name}.oni" >&2; exit 1; }
    echo "  corrected TXMP$name (region=$region fmt=$fmt) -> level$lvl/TXMP$name.oni"
done

echo; echo "Done. Corrections in $OUT_DIR are staged last (override) by build-hd-overlays.sh."
