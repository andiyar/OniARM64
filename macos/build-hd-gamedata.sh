#!/usr/bin/env bash
# build-hd-gamedata.sh
#
# Bakes community HD texture mods into Oni's GameDataFolder by per-level
# OniSplit overlay. Reads from a vanilla clone (read-only), writes IN PLACE
# into the working GameDataFolder that the .app symlinks to.
#
# Layout this expects:
#   CXOni/.../GameDataFolder.vanilla/  ← READ FROM (immutable clone)
#   CXOni/.../GameDataFolder/          ← WRITE INTO (what the .app sees)
#   .app/Contents/Resources/gamedata → CXOni/.../GameDataFolder  (untouched)
#
# To restore vanilla after a bake:
#   rm -rf  CXOni/.../GameDataFolder
#   cp -R   CXOni/.../GameDataFolder.vanilla   CXOni/.../GameDataFolder
#
# Filters: Format=0 (BGRA4444) TXMP files larger than 256² are skipped to
# avoid smashing the 256² OGLgCommon.convertedDataBuffer in the renderer.
# That cap is a separate bug to fix in C; until then we keep the affected
# textures vanilla (small handful of glass/glow surfaces).

set -euo pipefail

VANILLA_GDF="${1:-/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/GameDataFolder.vanilla}"
MODS_ROOT="${2:-/Users/andiyar/Developer/oni/HDTextureMods}"
OUTPUT_GDF="${3:-/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/GameDataFolder}"
ONISPLIT="${ONISPLIT:-/Users/andiyar/Developer/oni/community-svn/Oni2/OniSplit/bin/Release/OniSplit.exe}"
WORK="${WORK:-/tmp/oni-hd-bake}"

err() { echo "build-hd-gamedata.sh: ERROR: $*" >&2; exit 1; }
[ -d "$VANILLA_GDF" ] || err "vanilla GDF not found: $VANILLA_GDF"
[ -d "$MODS_ROOT" ]   || err "mods root not found:   $MODS_ROOT"
[ -d "$OUTPUT_GDF" ]  || err "output GDF not found:  $OUTPUT_GDF"
[ -f "$ONISPLIT" ]    || err "OniSplit.exe not found at $ONISPLIT"
command -v mono >/dev/null || err "mono not in PATH (brew install mono)"

mkdir -p "$WORK"

# ---- 1. export vanilla levels + build instance index ----
# Uses OniSplit -export (produces all instances, named and unnamed) instead
# of -list (named only). Character body textures like k4_head are unnamed in
# some levels but present in the export — the old -list approach missed them,
# causing HD character mods (23951 etc.) to silently fail.
INDEX_DIR="$WORK/index"
EXPORT_DIR="$WORK/vanilla"
mkdir -p "$INDEX_DIR" "$EXPORT_DIR"
echo "[1/4] exporting vanilla levels + building instance index ..."
for dat in "$VANILLA_GDF"/level*.dat; do
    level=$(basename "$dat" .dat)
    vexp="$EXPORT_DIR/$level"
    idx="$INDEX_DIR/$level.txt"
    if [ -d "$vexp" ] && [ ! "$dat" -nt "$vexp" ]; then
        continue
    fi
    printf "  %-22s " "$level"
    rm -rf "$vexp"
    mono "$ONISPLIT" -export "$vexp" "$dat" >/dev/null 2>&1
    ls "$vexp" | sed 's/%2F/\//g; s/\.oni$//' > "$idx"
    printf "(%d instances)\n" "$(wc -l < "$idx" | tr -d ' ')"
done
# Rebuild any stale indexes (export cached but index missing)
for vexp in "$EXPORT_DIR"/level*; do
    [ -d "$vexp" ] || continue
    level=$(basename "$vexp")
    idx="$INDEX_DIR/$level.txt"
    [ -f "$idx" ] && continue
    ls "$vexp" | sed 's/%2F/\//g; s/\.oni$//' > "$idx"
done

# ---- 2. mod-file → instance-name map ----
MOD_FILES_TSV="$WORK/mod_files.tsv"
: > "$MOD_FILES_TSV"
echo "[2/4] scanning mod .oni files ..."
while IFS= read -r -d '' f; do
    basename_oni=$(basename "$f")
    instance=$(printf '%s' "$basename_oni" | sed 's/%2F/\//g; s/\.oni$//')
    printf '%s\t%s\n' "$instance" "$f" >> "$MOD_FILES_TSV"
done < <(find "$MODS_ROOT" -name '*.oni' -print0)
mod_count=$(wc -l < "$MOD_FILES_TSV" | tr -d ' ')
echo "  scanned $mod_count mod .oni files"

# ---- helper: is this a Format=0 TXMP larger than 256²? skip if so ----
# Returns 0 (= skip) or 1 (= keep).
should_skip() {
    local f="$1"
    local fname
    fname=$(basename "$f")
    [[ "$fname" == TXMP* ]] || return 1   # non-TXMP: keep
    local fmt
    fmt=$(xxd -p -s 0x110 -l 1 "$f" 2>/dev/null)
    [ "${fmt:-FF}" = "00" ] || return 1   # not BGRA4444: keep
    local w_hex h_hex w h
    w_hex=$(xxd -p -s 0x10C -l 2 "$f")
    h_hex=$(xxd -p -s 0x10E -l 2 "$f")
    w=$((16#${w_hex:2:2}${w_hex:0:2}))
    h=$((16#${h_hex:2:2}${h_hex:0:2}))
    [ $((w * h)) -gt 65536 ] && return 0  # >256²: skip
    return 1                              # ≤256²: keep
}

# ---- 3. per-level bake ----
echo "[3/4] baking levels (Format=0 >256² files filtered out) ..."
baked_levels=()
skipped_unsafe_total=0
for dat in "$VANILLA_GDF"/level*.dat; do
    level=$(basename "$dat" .dat)
    idx="$INDEX_DIR/$level.txt"
    vexp="$EXPORT_DIR/$level"

    matches=$(awk -F'\t' '
        NR==FNR { lev[$0]=1; next }
        ($1 in lev) { print $2 }
    ' "$idx" "$MOD_FILES_TSV")

    if [ -z "$matches" ]; then
        continue
    fi

    stage="$WORK/stage_$level"
    rm -rf "$stage"
    mkdir -p "$stage"
    staged=0
    skipped=0
    while IFS= read -r modfile; do
        [ -z "$modfile" ] && continue
        if should_skip "$modfile"; then
            skipped=$((skipped+1))
            continue
        fi
        cp "$modfile" "$stage/"
        staged=$((staged+1))
    done <<< "$matches"

    if [ "$staged" -eq 0 ]; then
        printf "  %-22s  (all candidates filtered)\n" "$level"
        continue
    fi
    if [ "$skipped" -gt 0 ]; then
        printf "  %-22s  %3d overlays  (%d filtered as unsafe)\n" "$level" "$staged" "$skipped"
        skipped_unsafe_total=$((skipped_unsafe_total + skipped))
    else
        printf "  %-22s  %3d overlays\n" "$level" "$staged"
    fi

    # Import: vanilla export + mod overlay → output .dat + .raw
    # OniSplit deduplicates by instance name; last directory wins.
    out="$WORK/out_$level"
    rm -rf "$out"
    mkdir -p "$out"
    ( cd "$out" && mono "$ONISPLIT" -import:nosep "$vexp" "$stage" "$level.dat" >/dev/null 2>&1 )

    [ -f "$out/$level.dat" ] || err "bake failed for $level (no .dat)"
    [ -f "$out/$level.raw" ] || err "bake failed for $level (no .raw)"

    # Install in place — overwrites the existing vanilla copies at OUTPUT_GDF
    cp "$out/$level.dat" "$OUTPUT_GDF/$level.dat"
    cp "$out/$level.raw" "$OUTPUT_GDF/$level.raw"
    baked_levels+=("$level")
done

# ---- 4. summary ----
echo "[4/4] done"
echo "  baked:   ${#baked_levels[@]} level(s)"
echo "  unsafe files filtered (Format=0 BGRA4444 >256²): $skipped_unsafe_total"
echo ""
echo "Restore vanilla anytime with:"
echo "  rm -rf  '$OUTPUT_GDF'"
echo "  cp -R   '$VANILLA_GDF'  '$OUTPUT_GDF'"
