#!/usr/bin/env bash
# test_sweep_core.sh — builds and runs tests/test_sweep_core.c, the lifecycle
# tests for Oni_Sweep.c (inert before Begin / after End, flush per record, both
# RNG streams seeded, tick loop).
#
# Oni_Sweep.c includes BFW.h, so the include and define lists come from the
# configured build rather than being duplicated here.
#
# Usage: tests/test_sweep_core.sh [build-dir]      (from repo root)
set -u
BUILD="${1:-build}"
FLAGS="$BUILD/OniProj/OniCMakeProjs/OniProj/CMakeFiles/Oni.dir/flags.make"

if [ ! -f "$FLAGS" ]; then
	echo "no configured build at $BUILD (looked for $FLAGS)"
	exit 1
fi

INC=$(grep -m1 '^C_INCLUDES' "$FLAGS" | sed 's/^C_INCLUDES = //')
# Drop the string-valued defines (ONI_VERSION and friends). Their embedded
# quotes are escaped for make, not for a shell, and nothing here reads them.
DEF=$(grep -m1 '^C_DEFINES' "$FLAGS" | sed 's/^C_DEFINES = //' | tr ' ' '\n' | grep -v '\\"' | tr '\n' ' ')

TMP=$(mktemp -d)
BIN="$TMP/test_sweep_core"

cc -std=gnu11 -Wno-multichar -Wno-incompatible-pointer-types \
	-Wno-incompatible-function-pointer-types \
	$DEF $INC \
	tests/test_sweep_core.c \
	OniProj/OniGameSource/Oni_Sweep.c \
	OniProj/OniGameSource/Oni_Sweep_Report.c \
	OniProj/OniGameSource/Oni_Sweep_Normalize.c \
	-o "$BIN" || { echo "build failed"; rm -rf "$TMP"; exit 1; }

"$BIN"
STATUS=$?
rm -rf "$TMP"
exit $STATUS
