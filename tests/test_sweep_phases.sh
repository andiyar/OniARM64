#!/usr/bin/env bash
# test_sweep_phases.sh — builds and runs tests/test_sweep_phases.c, the tests
# for the load and character sweep phases (tasks 7 and 8 of issue #103).
#
# Same shape as test_sweep_core.sh: the real Oni_Sweep.c is linked against
# stubbed engine symbols, so the assertions are about the shipped code rather
# than a transcription of it. Include and define lists come from the configured
# build because Oni_Sweep.c includes BFW.h.
#
# Usage: tests/test_sweep_phases.sh [build-dir]     (from repo root)
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
STATUS=0

# Both configurations ship — the Oni binary compiles the console tap out, the
# OniSweep binary compiles it in — so the phases are exercised under both.
run_config() {  # $1 = label, $2... = extra defines
	local label="$1"; shift
	local bin="$TMP/test_sweep_phases_$label"
	local cflags="-std=gnu11 -Wno-multichar -Wno-incompatible-pointer-types
		-Wno-incompatible-function-pointer-types"

	cc $cflags $DEF "$@" $INC -Itests \
		tests/test_sweep_phases.c \
		tests/test_sweep_engine_stubs.c \
		OniProj/OniGameSource/Oni_Sweep.c \
		OniProj/OniGameSource/Oni_Sweep_Report.c \
		OniProj/OniGameSource/Oni_Sweep_Normalize.c \
		-o "$bin" || { echo "build failed ($label)"; STATUS=1; return; }

	echo "--- $label"
	"$bin" || STATUS=1
}

run_config shipping
run_config sweep -DONI_SWEEP_CONSOLE=1

rm -rf "$TMP"
exit $STATUS
