#!/usr/bin/env bash
# Notarizes and staples a signed OniARM64.app via Apple's notary service.
# Assumes build-bundle.sh has already produced a Developer-ID-signed .app at
# $BINARY_DIR/bin/OniARM64.app.
#
# Usage: notarize-bundle.sh <BINARY_DIR> [KEYCHAIN_PROFILE]
#   BINARY_DIR        - cmake binary dir (parent of bin/OniARM64.app)
#   KEYCHAIN_PROFILE  - keychain entry from `xcrun notarytool store-credentials`
#                       (default: oniarm64-notarize)
#
# One-time setup (NOT done by this script):
#   xcrun notarytool store-credentials oniarm64-notarize \
#       --apple-id "<your-apple-id>" \
#       --team-id "<your-team-id>" \
#       --password "<app-specific-password>"
#
# Recovery for "Invalid" verdicts:
#   xcrun notarytool log <submission-id> --keychain-profile oniarm64-notarize

set -euo pipefail

BINARY_DIR="${1:?binary dir required}"
PROFILE="${2:-oniarm64-notarize}"
APP="$BINARY_DIR/bin/OniARM64.app"
ZIP="$BINARY_DIR/OniARM64-notarize.zip"

if [ ! -d "$APP" ]; then
    echo "notarize-bundle.sh: ERROR: $APP not found. Run build-bundle.sh first." >&2
    exit 1
fi

# 1. Zip via ditto — plain `zip` strips macOS xattrs and notarytool rejects.
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
echo "notarize-bundle.sh: submitting $ZIP to Apple's notary service..."

# 2. Submit and block until verdict (Apple SLA ~5 min, typically 1-2).
xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait

# 3. Staple Apple's ticket onto the .app for offline Gatekeeper acceptance.
xcrun stapler staple "$APP"

# 4. Validate the stapled ticket.
xcrun stapler validate -v "$APP"

# 5. Final Gatekeeper check — the real ship-readiness signal.
spctl --assess --type execute --verbose=4 "$APP"

# 6. Cleanup.
rm -f "$ZIP"
echo "notarize-bundle.sh: $APP notarized + stapled + Gatekeeper-accepted."
