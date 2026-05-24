#!/usr/bin/env bash
# Wraps an already-signed-+-notarized-+-stapled OniARM64.app in a DMG with
# a drag-to-Applications layout, then signs + notarizes + staples the DMG
# itself. End result: zero-Gatekeeper-warning download experience for users.
#
# Usage: package-dmg.sh <BINARY_DIR> <SIGN_IDENTITY> [KEYCHAIN_PROFILE]
#   BINARY_DIR        - cmake binary dir (parent of bin/OniARM64.app)
#   SIGN_IDENTITY     - full "Developer ID Application: Name (TEAMID)" string
#   KEYCHAIN_PROFILE  - keychain entry from `notarytool store-credentials`
#                       (default: oniarm64-notarize)
#
# Requires: `brew install create-dmg`

set -euo pipefail

BINARY_DIR="${1:?binary dir required}"
SIGN_IDENTITY="${2:?sign identity required}"
PROFILE="${3:-oniarm64-notarize}"
APP="$BINARY_DIR/bin/OniARM64.app"
DMG="$BINARY_DIR/OniARM64.dmg"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VOLICON="$SCRIPT_DIR/assets/Oni.icns"

# Preflight: app must already be stapled.
if ! xcrun stapler validate "$APP" >/dev/null 2>&1; then
    echo "package-dmg.sh: ERROR: $APP not stapled. Run notarize-bundle.sh first." >&2
    exit 1
fi

# Preflight: create-dmg available.
if ! command -v create-dmg >/dev/null 2>&1; then
    echo "package-dmg.sh: ERROR: create-dmg not found. Run: brew install create-dmg" >&2
    exit 1
fi

# 1. Build the DMG. Default-white background, drag-to-Applications hint via
#    a positioned Applications symlink. Custom background image is deferred
#    polish (see spec out-of-scope).
rm -f "$DMG"
create-dmg \
    --volname "OniARM64" \
    --volicon "$VOLICON" \
    --window-size 600 400 \
    --icon-size 100 \
    --icon "OniARM64.app" 150 200 \
    --app-drop-link 450 200 \
    --hide-extension "OniARM64.app" \
    --no-internet-enable \
    "$DMG" \
    "$APP"

# 2. Sign the DMG with Developer ID + timestamp.
codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"

# 3. Notarize the DMG itself (separate submission from the .app's).
echo "package-dmg.sh: submitting $DMG to Apple's notary service..."
xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait

# 4. Staple the ticket onto the DMG so mounting has zero Gatekeeper warning.
xcrun stapler staple "$DMG"

# 5. Final Gatekeeper verification on the DMG.
spctl --assess --type open --context context:primary-signature --verbose=4 "$DMG"

echo "package-dmg.sh: $DMG ready (signed + notarized + stapled)."
