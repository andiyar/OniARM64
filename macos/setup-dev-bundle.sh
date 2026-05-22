#!/usr/bin/env bash
# One-shot dev-.app setup: rebuild the bundle if missing, symlink game data
# into Contents/Resources/gamedata, and strip the com.apple.quarantine xattr.
#
# Use after `make oni_app` (or invoke directly — the script will run that for
# you if the .app doesn't exist).
#
# Usage:
#   ./macos/setup-dev-bundle.sh /path/to/your/Oni/GameDataFolder

set -euo pipefail

GAMEDATA_SRC="${1:?usage: setup-dev-bundle.sh /path/to/GameDataFolder}"

if [ ! -d "$GAMEDATA_SRC" ]; then
    echo "setup-dev-bundle.sh: ERROR: $GAMEDATA_SRC is not a directory" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$REPO_ROOT/build/bin/OniARM64.app"

if [ ! -d "$APP" ]; then
    echo "setup-dev-bundle.sh: $APP not present, running 'make oni_app'..."
    (cd "$REPO_ROOT/build" && make oni_app)
fi

if [ ! -d "$APP" ]; then
    echo "setup-dev-bundle.sh: ERROR: $APP still not present after build" >&2
    exit 1
fi

ln -sfn "$GAMEDATA_SRC" "$APP/Contents/Resources/gamedata"
echo "Symlinked $APP/Contents/Resources/gamedata -> $GAMEDATA_SRC"

if xattr -p com.apple.quarantine "$APP" >/dev/null 2>&1; then
    xattr -d com.apple.quarantine "$APP"
    echo "Removed com.apple.quarantine from $APP"
fi

echo "Done. Launch: open $APP"
