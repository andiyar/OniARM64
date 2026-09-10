#!/usr/bin/env bash
# Assembles "OniMod Installer.app" (#20) from the Swift droplet sources plus
# the two helper tools built by cmake (onipack, txmp-format-index).
# Idempotent; replaces any existing bundle.
#
# Usage: build-installer.sh <SOURCE_DIR> <BINARY_DIR> [SIGN_IDENTITY]
#   SIGN_IDENTITY: "-" (ad-hoc, default) or a Developer ID string, which
#   switches on hardened runtime + timestamp (same as build-bundle.sh).
# Produces: $BINARY_DIR/bin/OniMod Installer.app
set -euo pipefail

SOURCE_DIR="${1:?source dir required}"
BINARY_DIR="${2:?binary dir required}"
SIGN_IDENTITY="${3:--}"

SRC="$SOURCE_DIR/tools/OniModInstaller"
APP="$BINARY_DIR/bin/OniMod Installer.app"
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"

for tool in onipack txmp-format-index; do
    [ -x "$BINARY_DIR/bin/$tool" ] || { echo "build-installer.sh: ERROR: $BINARY_DIR/bin/$tool missing (make onipack txmp_format_index)" >&2; exit 1; }
done

rm -rf "$APP"
mkdir -p "$MACOS_DIR" "$RESOURCES"

# 1. Swift binary. Top-level code lives in main.swift, so no -parse-as-library.
swiftc -O -target arm64-apple-macosx15.0 \
    "$SRC/Installer.swift" "$SRC/main.swift" \
    -framework AppKit -framework UniformTypeIdentifiers \
    -o "$MACOS_DIR/OniModInstaller"

# 2. Helpers beside the executable (main.swift resolves them relative to it).
cp "$BINARY_DIR/bin/onipack" "$BINARY_DIR/bin/txmp-format-index" "$MACOS_DIR/"

# 3. Plist + icon. Version string tracks the game's Info.plist so the pair
#    always ships matched.
cp "$SRC/Info.plist" "$CONTENTS/Info.plist"
GAME_VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$SOURCE_DIR/macos/Info.plist")
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $GAME_VERSION" "$CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $GAME_VERSION" "$CONTENTS/Info.plist"
printf 'APPL????' > "$CONTENTS/PkgInfo"
# The game's icon for now; #75 may give the installer its own.
cp "$SOURCE_DIR/macos/assets/Oni.icns" "$RESOURCES/OniModInstaller.icns"

# 4. Sign inside-out: helpers, main binary, bundle.
SIGN_ARGS=(--force --sign "$SIGN_IDENTITY")
if [ "$SIGN_IDENTITY" != "-" ]; then
    SIGN_ARGS+=(--options runtime --timestamp)
fi
codesign "${SIGN_ARGS[@]}" "$MACOS_DIR/onipack"
codesign "${SIGN_ARGS[@]}" "$MACOS_DIR/txmp-format-index"
codesign "${SIGN_ARGS[@]}" "$MACOS_DIR/OniModInstaller"
codesign "${SIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict "$APP"

echo "build-installer.sh: $APP assembled (signed as '$SIGN_IDENTITY')."
