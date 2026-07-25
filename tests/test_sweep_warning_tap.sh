#!/usr/bin/env bash
# test_sweep_warning_tap.sh — builds and runs tests/test_sweep_warning_tap.c,
# which links the real BFW_Error.c and proves UUrPrintWarning never reaches its
# blocking AUrMessageBox while the sweep tap is registered.
#
# Usage: tests/test_sweep_warning_tap.sh          (from repo root)
set -u
TMP=$(mktemp -d)
BIN="$TMP/test_sweep_warning_tap"

cc -std=gnu11 -Wno-multichar -DUUmSDL=1 -isystem /opt/homebrew/include \
	-IBungieFrameWork/BFW_Headers \
	-IBungieFrameWork/BFW_Source/BFW_Console \
	-IBungieFrameWork/BFW_Source/BFW_Motoko \
	-IBungieFrameWork/BFW_Source/BFW_Utility \
	-IBungieFrameWork/BFW_Source/BFW_MathLib \
	-IBungieFrameWork/BFW_Source/BFW_TemplateManager \
	tests/test_sweep_warning_tap.c \
	BungieFrameWork/BFW_Source/BFW_Utility/BFW_Error.c \
	-o "$BIN" || { echo "build failed"; rm -rf "$TMP"; exit 1; }

# stderr is the warnings themselves — UUrPrintWarning prints every one there,
# which is expected and not part of the result.
"$BIN" 2>/dev/null
STATUS=$?
rm -rf "$TMP"
exit $STATUS
