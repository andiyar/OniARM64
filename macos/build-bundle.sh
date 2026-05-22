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

# 4. Bundle Homebrew dylibs into Contents/Frameworks/, rewrite load paths.
#    System frameworks (OpenGL, OpenAL) are NOT bundled — they're macOS-provided.
BINARY_IN_BUNDLE="$MACOS_DIR/Oni"

# Discover the dylibs the binary links from Homebrew.
HOMEBREW_LIBS=$(otool -L "$BINARY_IN_BUNDLE" | awk '/\/opt\/homebrew\// {print $1}')

for src_lib in $HOMEBREW_LIBS; do
    if [ ! -f "$src_lib" ]; then
        echo "build-bundle.sh: WARNING: dylib not found at $src_lib, skipping" >&2
        continue
    fi
    lib_basename=$(basename "$src_lib")
    dst_lib="$FRAMEWORKS/$lib_basename"

    cp "$src_lib" "$dst_lib"
    # Some Homebrew dylibs are read-only; make them writable so install_name_tool succeeds.
    chmod u+w "$dst_lib"

    # Rewrite the dylib's OWN install name so it identifies itself by the bundled path.
    install_name_tool -id "@executable_path/../Frameworks/$lib_basename" "$dst_lib"

    # Rewrite the binary's reference to this dylib.
    install_name_tool -change "$src_lib" "@executable_path/../Frameworks/$lib_basename" "$BINARY_IN_BUNDLE"
done

# Second pass: dylibs themselves may link other Homebrew dylibs (e.g. libavcodec → libavutil).
# Rewrite those references too.
for dylib in "$FRAMEWORKS"/*.dylib; do
    [ -f "$dylib" ] || continue
    DEPS=$(otool -L "$dylib" | awk '/\/opt\/homebrew\// {print $1}')
    for dep in $DEPS; do
        dep_basename=$(basename "$dep")
        # Only rewrite if we have a bundled copy.
        if [ -f "$FRAMEWORKS/$dep_basename" ]; then
            install_name_tool -change "$dep" "@executable_path/../Frameworks/$dep_basename" "$dylib"
        fi
    done
done

echo "build-bundle.sh: $APP assembled."
