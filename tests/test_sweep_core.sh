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
STATUS=0

# Run twice: once with the shipping target's defines (console tap compiled out,
# which is what the Oni binary gets) and once with ONI_SWEEP_CONSOLE=1, which is
# what OniSweep gets. Both configurations ship, so both are tested.
run_config() {  # $1 = label, $2... = extra defines
	local label="$1"; shift
	local bin="$TMP/test_sweep_core_$label"
	local cflags="-std=gnu11 -Wno-multichar -Wno-incompatible-pointer-types
		-Wno-incompatible-function-pointer-types"

	# Oni_Sweep_Report.c is compiled with its entry point renamed so the test can
	# wrap it. That wrapper is the only seam inside ONrSweep_Record's call graph,
	# and without it the re-entrancy guard cannot be exercised at all.
	cc $cflags $DEF "$@" $INC \
		-DONrSweep_Report_FormatLine=ONrSweep_Report_FormatLine_real \
		-c OniProj/OniGameSource/Oni_Sweep_Report.c -o "$TMP/report_$label.o" \
		|| { echo "build failed ($label, report)"; STATUS=1; return; }

	# test_sweep_engine_stubs.c satisfies the engine symbols the sweep phases
	# call. These tests do not exercise the phases, but Oni_Sweep.c is linked
	# whole, so every external it has must resolve. The phases themselves are
	# tested in tests/test_sweep_phases.sh.
	cc $cflags $DEF "$@" $INC -Itests \
		tests/test_sweep_core.c \
		tests/test_sweep_engine_stubs.c \
		OniProj/OniGameSource/Oni_Sweep.c \
		"$TMP/report_$label.o" \
		OniProj/OniGameSource/Oni_Sweep_Normalize.c \
		-o "$bin" || { echo "build failed ($label)"; STATUS=1; return; }

	echo "--- $label"
	"$bin" || STATUS=1
}

run_config shipping
run_config sweep -DONI_SWEEP_CONSOLE=1

rm -rf "$TMP"
exit $STATUS
