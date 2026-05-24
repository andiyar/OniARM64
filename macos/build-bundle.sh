#!/usr/bin/env bash
# Assembles OniARM64.app from a freshly-built Oni binary + committed bundle
# templates + assets. Idempotent: safe to re-run; replaces existing .app.
#
# Usage: build-bundle.sh <SOURCE_DIR> <BINARY_DIR>
#   SOURCE_DIR: top-level OniARM64/ source tree (contains macos/, etc.)
#   BINARY_DIR: cmake binary dir (contains bin/Oni)
#
# Produces: $BINARY_DIR/bin/OniARM64.app/

set -euo pipefail

SOURCE_DIR="${1:?source dir required}"
BINARY_DIR="${2:?binary dir required}"
# Third arg: codesign identity. "-" = ad-hoc (default, fast dev loop).
# Pass a full "Developer ID Application: Name (TEAMID)" string for release
# builds, which switches the script into hardened-runtime + timestamp mode.
SIGN_IDENTITY="${3:--}"

APP="$BINARY_DIR/bin/OniARM64.app"
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
FRAMEWORKS="$CONTENTS/Frameworks"

BINARY_SRC="$BINARY_DIR/bin/Oni"
if [ ! -f "$BINARY_SRC" ]; then
    echo "build-bundle.sh: ERROR: $BINARY_SRC not found. Build the Oni target first." >&2
    exit 1
fi

# Wipe any stale bundle from a prior run.
rm -rf "$APP"
mkdir -p "$MACOS_DIR" "$RESOURCES" "$FRAMEWORKS"

# 1. Binary.
cp "$BINARY_SRC" "$MACOS_DIR/Oni"

# 2. Bundle templates.
cp "$SOURCE_DIR/macos/Info.plist" "$CONTENTS/Info.plist"
cp "$SOURCE_DIR/macos/PkgInfo"    "$CONTENTS/PkgInfo"

# 3. Assets.
cp "$SOURCE_DIR/macos/assets/Oni.icns"  "$RESOURCES/Oni.icns"
cp "$SOURCE_DIR/macos/assets/intro.mov" "$RESOURCES/intro.mov"
cp "$SOURCE_DIR/macos/assets/outro.mov" "$RESOURCES/outro.mov"

# 3a. Third-party license texts (compliance for redistributed bundle).
#     Copied as .txt so Finder shows it in the bundle's Show-Package-Contents
#     view without needing a markdown renderer.
cp "$SOURCE_DIR/THIRD_PARTY_LICENSES.md" "$RESOURCES/THIRD_PARTY_LICENSES.txt"

# 4. Bundle Homebrew dylibs (direct + transitive) into Contents/Frameworks/.
#    BFS walk: start from the binary, follow every /opt/homebrew/ LC_LOAD_DYLIB
#    entry to fixed point, bundle each discovered dylib, then rewrite all refs
#    (binary + bundled dylibs) so nothing points at /opt/homebrew/ anymore.
#    Result: a self-contained bundle that runs on machines without Homebrew.
#
#    System frameworks (/System/Library/..., /usr/lib/libSystem.B.dylib,
#    /usr/lib/libiconv.2.dylib, /usr/lib/libz.1.dylib) stay as absolute refs —
#    macOS guarantees them on every install.
BINARY_IN_BUNDLE="$MACOS_DIR/Oni"

# Print one /opt/homebrew/ dylib path per line.
# `NR>1` skips otool's first line (the file's own path, which itself contains
# /opt/homebrew/ when the input is a Homebrew dylib).
homebrew_deps_of() {
    otool -L "$1" | awk 'NR>1 && /\/opt\/homebrew\// {print $1}'
}

# 4a. Discover phase: BFS for every transitively-linked /opt/homebrew/ dylib.
#     Newline-separated strings as set/queue (bash 3.2 compatible).
seen=""
worklist=$(homebrew_deps_of "$BINARY_IN_BUNDLE")

while [ -n "$worklist" ]; do
    current=$(printf '%s\n' "$worklist" | head -n 1)
    worklist=$(printf '%s\n' "$worklist" | tail -n +2)

    [ -z "$current" ] && continue
    if printf '%s\n' "$seen" | grep -Fxq -- "$current"; then
        continue
    fi
    seen=$(printf '%s\n%s' "$seen" "$current")

    if [ ! -f "$current" ]; then
        echo "build-bundle.sh: WARNING: $current not found, skipping" >&2
        continue
    fi

    new_deps=$(homebrew_deps_of "$current")
    if [ -n "$new_deps" ]; then
        worklist=$(printf '%s\n%s' "$worklist" "$new_deps")
    fi
done

# 4b. Copy phase: copy each discovered dylib into Frameworks/ and set its own
#     LC_ID_DYLIB to the bundled path.
printf '%s\n' "$seen" | while IFS= read -r src_lib; do
    [ -z "$src_lib" ] && continue
    [ -f "$src_lib" ] || continue
    lib_basename=$(basename "$src_lib")
    dst_lib="$FRAMEWORKS/$lib_basename"

    cp "$src_lib" "$dst_lib"
    # Homebrew dylibs ship read-only; make writable so install_name_tool succeeds.
    chmod u+w "$dst_lib"
    install_name_tool -id "@executable_path/../Frameworks/$lib_basename" "$dst_lib"
done

# 4c. Rewrite phase: walk binary + every bundled dylib, rewrite every
#     /opt/homebrew/ LC_LOAD_DYLIB entry whose basename has a bundled copy.
rewrite_refs() {
    target="$1"
    homebrew_deps_of "$target" | while IFS= read -r ref; do
        [ -z "$ref" ] && continue
        ref_basename=$(basename "$ref")
        if [ -f "$FRAMEWORKS/$ref_basename" ]; then
            install_name_tool -change "$ref" "@executable_path/../Frameworks/$ref_basename" "$target"
        fi
    done
}

rewrite_refs "$BINARY_IN_BUNDLE"
for dylib in "$FRAMEWORKS"/*.dylib; do
    [ -f "$dylib" ] || continue
    rewrite_refs "$dylib"
done

# 4d. Re-sign phase: install_name_tool invalidates code signatures on Apple
#     Silicon, and dyld refuses to load Mach-Os with stale signatures
#     (SIGKILL: Code Signature Invalid). Re-sign every Mach-O so dyld accepts
#     it, then seal the bundle.
#
#     Order matters: dylibs first, main binary second, .app bundle third.
#     (Inside-out — required for hardened-runtime Developer ID signing;
#      harmless for ad-hoc.) Hardened-runtime + timestamp flags are added
#     when SIGN_IDENTITY != "-"; entitlements applied to the main binary only.
ENTITLEMENTS="$SOURCE_DIR/macos/entitlements.plist"
SIGN_ARGS=(--force --sign "$SIGN_IDENTITY")
if [ "$SIGN_IDENTITY" != "-" ]; then
    SIGN_ARGS+=(--options runtime --timestamp)
fi

# Dylibs first
for dylib in "$FRAMEWORKS"/*.dylib; do
    [ -f "$dylib" ] || continue
    codesign "${SIGN_ARGS[@]}" "$dylib"
done

# Main binary second (with entitlements only when using a real identity;
# ad-hoc + entitlements is a noisy no-op that some tools flag).
BINARY_SIGN_ARGS=("${SIGN_ARGS[@]}")
if [ "$SIGN_IDENTITY" != "-" ]; then
    BINARY_SIGN_ARGS+=(--entitlements "$ENTITLEMENTS")
fi
codesign "${BINARY_SIGN_ARGS[@]}" "$BINARY_IN_BUNDLE"

# Bundle itself third — seals Contents/_CodeSignature/CodeResources.
codesign "${SIGN_ARGS[@]}" "$APP"

# 5. Verify the bundle is structurally valid (seal intact, all Mach-Os signed).
#    Aborts the script on failure — better to catch sign-order or
#    missing-dylib bugs here than at notarization time.
codesign --verify --strict --verbose=2 "$APP"

bundled_count=$(find "$FRAMEWORKS" -name '*.dylib' | wc -l | tr -d ' ')
if [ "$SIGN_IDENTITY" = "-" ]; then
    echo "build-bundle.sh: $APP assembled ($bundled_count dylibs bundled, ad-hoc signed)."
else
    echo "build-bundle.sh: $APP assembled ($bundled_count dylibs bundled, signed as '$SIGN_IDENTITY')."
fi
