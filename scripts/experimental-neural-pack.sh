#!/usr/bin/env bash
# ======================================================================
# STATUS: EXPERIMENTAL / PARKED (#64 closed 2026-07-07). Level-8 pilot built and
# played; verdict: perceptual gain at gameplay distance does not justify stage 2/3 —
# CuratedHD hand-authored art wins on characters. Kept for the record; the real
# payoff of the pilot was exposing the filtering bottleneck fixed as #65 (aniso).
# DEPS (not in repo): mono+OniSplit, ImageMagick, realesrgan-ncnn-vulkan portable
# build + models (paths near the top assume the 2026-07-07 scratchpad layout).
# build-neural-pilot.sh — issue #64 stage-2 PILOT: neural-upscaled
# overlay pack for ONE level, generated from vanilla retail textures.
#
# Pipeline per TXMP (named, static, 32..256px, not sky/env/glass):
#   retail .dat -> .oni -> .xml + .tga          (OniSplit)
#   RGB:   Real-ESRGAN x4 (general), blended STRENGTH% over Lanczos x4
#   alpha: Mitchell x4 (conservative — alpha is a shininess mask, #63)
#   format: RGBA (fmt 7) if alpha-carrying, BGR (fmt 8) if opaque
#   .xml + upscaled .tga -> .oni -> level<N>_NX1.dat  (OniSplit)
#
# Skips (stay vanilla, all logged): animated (TXAN frames), skies/env
# maps (shared reflection sources, #63), GLASS* (dither patterns read
# as noise), NONE, tiny (<32px) and already-large (>256px) sources.
# ======================================================================
set -euo pipefail

LEVEL="${LEVEL:-8}"
STRENGTH="${STRENGTH:-60}"           # % neural in the blend
MAXOUT="${MAXOUT:-512}"              # output dimension cap
MODEL="${MODEL:-realesrgan-x4plus}"  # general = most faithful
DAT="${DAT:-/Users/andiyar/Developer/oni/OniNative/GameDataFolder/level${LEVEL}_Final.dat}"
ONISPLIT_EXE="${ONISPLIT_EXE:-/Users/andiyar/Developer/oni/community-svn/Oni2/OniSplit/bin/Release/OniSplit.exe}"
SCRATCH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESRGAN="$SCRATCH/realesrgan/realesrgan-ncnn-vulkan"
ESRGAN_MODELS="$SCRATCH/realesrgan/models"
OUT_DIR="${1:-$SCRATCH/neuralpack}"
SKIP_RE='^(env|sky|night_|day_|NONE$)|GLASS'   # case-insensitive, on instance name

W="$SCRATCH/npwork"
rm -rf "$W" "$OUT_DIR"
mkdir -p "$W/xml" "$W/sr_in" "$W/sr_out" "$W/build" "$W/onipack" "$OUT_DIR"
MANIFEST="$W/manifest.tsv"; : > "$MANIFEST"

echo "== neural pilot pack: level $LEVEL, strength ${STRENGTH}%, model $MODEL =="

# 1. split the retail dat into per-instance .oni, then batch-extract
#    every TXMP as xml+tga (one OniSplit run each).
echo "-- exporting retail instances --"
mono "$ONISPLIT_EXE" -export "$W/oni" "$DAT" >/dev/null
ntx=$(ls "$W/oni" | grep -c '^TXMP' || true)
echo "   $(ls "$W/oni" | wc -l | tr -d ' ') instances, $ntx TXMPs"
echo "-- extracting TXMP xml+tga --"
mono "$ONISPLIT_EXE" -extract:xml "$W/xml" "$W/oni/TXMP*.oni" >/dev/null
echo "   $(ls "$W/xml"/*.xml | wc -l | tr -d ' ') xml files"

# 2. classify + prepare RGB planes for the batch SR pass.
echo "-- classifying --"
kept=0
for x in "$W/xml"/TXMP*.xml; do
    b="$(basename "$x" .xml)"; inst="${b#TXMP}"
    tga="$W/xml/$b.tga"
    nimg=$(grep -c '<Image>' "$x" || true)
    if [ "$nimg" -ne 1 ]; then
        printf 'SKIP\tanimated(%s frames)\t%s\n' "$nimg" "$inst" >> "$MANIFEST"; continue
    fi
    if printf '%s' "$inst" | grep -qiE "$SKIP_RE"; then
        printf 'SKIP\tclass-filter\t%s\n' "$inst" >> "$MANIFEST"; continue
    fi
    read -r wpx hpx amin <<< "$(magick identify -format '%w %h %[fx:minima.a]' "$tga")"
    maxd=$(( wpx > hpx ? wpx : hpx ))
    if [ "$maxd" -lt 32 ]; then
        printf 'SKIP\ttiny(%sx%s)\t%s\n' "$wpx" "$hpx" "$inst" >> "$MANIFEST"; continue
    fi
    if [ "$maxd" -gt 256 ]; then
        printf 'SKIP\tlarge(%sx%s)\t%s\n' "$wpx" "$hpx" "$inst" >> "$MANIFEST"; continue
    fi
    opaque=0; [ "$amin" = "1" ] && opaque=1
    printf 'KEEP\t%s\t%s\t%s\t%s\t%s\n' "$inst" "$wpx" "$hpx" "$opaque" "$b" >> "$MANIFEST"
    magick "$tga" -alpha off "$W/sr_in/$b.png"
    kept=$((kept+1))
done
echo "   kept $kept; skipped $(grep -c '^SKIP' "$MANIFEST" || true) (see manifest)"

# 3. one batch SR pass on the GPU.
echo "-- Real-ESRGAN x4 batch ($kept textures) --"
"$ESRGAN" -i "$W/sr_in" -o "$W/sr_out" -n "$MODEL" -s 4 -m "$ESRGAN_MODELS" 2>/dev/null

# 4. blend, alpha, recombine, rewrite xml.
echo "-- blend ${STRENGTH}% + alpha + xml rewrite --"
while IFS=$'\t' read -r kind inst wpx hpx opaque b; do
    [ "$kind" = "KEEP" ] || continue
    tga="$W/xml/$b.tga"
    ow=$(( wpx * 4 )); oh=$(( hpx * 4 ))
    maxd=$(( ow > oh ? ow : oh ))
    if [ "$maxd" -gt "$MAXOUT" ]; then ow=$(( ow * MAXOUT / maxd )); oh=$(( oh * MAXOUT / maxd )); fi
    magick "$W/sr_in/$b.png" -filter Lanczos -resize "${ow}x${oh}!" "$W/build/lz.png"
    magick "$W/sr_out/$b.png" -resize "${ow}x${oh}!" "$W/build/sr.png"
    magick "$W/build/lz.png" "$W/build/sr.png" -define compose:args="$STRENGTH" -compose blend -composite "$W/build/rgb.png"
    if [ "$opaque" = "1" ]; then
        magick "$W/build/rgb.png" -alpha off -type TrueColor -depth 8 -compress none "$W/build/$b.tga"
        newfmt=BGR
    else
        magick "$tga" -alpha extract -filter Mitchell -resize "${ow}x${oh}!" "$W/build/a.png"
        magick "$W/build/rgb.png" "$W/build/a.png" -alpha off -compose CopyOpacity -composite \
               -type TrueColorAlpha -depth 8 -compress none "$W/build/$b.tga"
        newfmt=RGBA
    fi
    sed -E "s|<Format>[^<]+</Format>|<Format>$newfmt</Format>|" "$W/xml/$b.xml" > "$W/build/$b.xml"
done < "$MANIFEST"
echo "   built $(ls "$W/build"/*.xml | wc -l | tr -d ' ') textures"

# 5. xml -> .oni, then pack the overlay dat (Mac sep format, like CuratedHD).
echo "-- OniSplit -create (.oni) --"
mono "$ONISPLIT_EXE" -create "$W/onipack" "$W/build/TXMP*.xml" >/dev/null
echo "   $(ls "$W/onipack" | wc -l | tr -d ' ') .oni files"
echo "-- OniSplit -import:sep (pack) --"
mono "$ONISPLIT_EXE" -import:sep "$W/onipack" "$OUT_DIR/level${LEVEL}_NX1.dat" >/dev/null

# 6. verify: named-instance count in the pack vs what we staged.
want=$(ls "$W/onipack" | grep -c '\.oni$')
got=$(mono "$ONISPLIT_EXE" -list "$OUT_DIR/level${LEVEL}_NX1.dat" | wc -l | tr -d ' ')
echo "-- verify: staged $want .oni, pack lists $got instances --"
du -h "$OUT_DIR"/* | sed 's/^/   /'
echo "manifest: $MANIFEST"
