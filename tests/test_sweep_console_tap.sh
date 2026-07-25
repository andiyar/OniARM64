#!/usr/bin/env bash
# test_sweep_console_tap.sh — builds and runs tests/test_sweep_console_tap.c
# against the real BFW_Console.c.
#
# That translation unit needs ~40 engine symbols it does not define. Rather
# than hand-maintain a stub list that goes stale, the script links once, reads
# the symbols the linker says are missing, generates a no-op for each, and
# links again. Nothing in COrConsole_Print's path calls through them.
#
# Runs twice:
#   sweep    — the OniSweep binary's own defines (SHIPPING_VERSION=1 plus
#              ONI_SWEEP_CONSOLE=1), i.e. the configuration that will actually
#              run a sweep.
#   debugfs  — SHIPPING_VERSION=0, which turns SUPPORT_DEBUG_FILES on so the
#              console's downstream sink is observable and "the tap observes,
#              it does not consume" can be measured rather than asserted.
#
# Usage: tests/test_sweep_console_tap.sh [build-dir]      (from repo root)
set -u
BUILD="${1:-build}"
FLAGS="$BUILD/OniProj/OniCMakeProjs/OniProj/CMakeFiles/Oni.dir/flags.make"

if [ ! -f "$FLAGS" ]; then
	echo "no configured build at $BUILD (looked for $FLAGS)"
	exit 1
fi

INC=$(grep -m1 '^C_INCLUDES' "$FLAGS" | sed 's/^C_INCLUDES = //')
# Drop the string-valued defines (their quotes are escaped for make, not a
# shell) and SHIPPING_VERSION, which each configuration below sets itself.
DEF=$(grep -m1 '^C_DEFINES' "$FLAGS" | sed 's/^C_DEFINES = //' \
	| tr ' ' '\n' | grep -v '\\"' | grep -v '^-DSHIPPING_VERSION' | tr '\n' ' ')

CONSOLE_SRC="BungieFrameWork/BFW_Source/BFW_Console/BFW_Console.c"
TMP=$(mktemp -d)
STATUS=0

run_config() {  # $1 = label, $2... = extra defines
	local label="$1"; shift
	local bin="$TMP/test_$label"
	local stubs="$TMP/stubs_$label.c"
	local cflags="-std=gnu11 -Wno-multichar -Wno-incompatible-pointer-types
		-Wno-incompatible-function-pointer-types -Wno-extra-tokens"

	echo "--- $label"

	# pass 1: let the linker name the missing symbols
	: > "$stubs"
	cc $cflags $DEF "$@" $INC tests/test_sweep_console_tap.c "$CONSOLE_SRC" \
		-o "$bin" 2> "$TMP/link1_$label.txt"

	if [ ! -x "$bin" ]; then
		# "  \"_COrTextArea_Print\", referenced from:" -> a no-op definition
		grep -oE '^  "_[A-Za-z0-9_]+"' "$TMP/link1_$label.txt" \
			| tr -d ' "' | sed 's/^_//' | sort -u \
			| awk '{ print "void " $0 "(void) { }" }' > "$stubs"

		if [ ! -s "$stubs" ]; then
			echo "build failed and no undefined symbols to stub:"
			tail -20 "$TMP/link1_$label.txt"
			STATUS=1
			return
		fi

		# pass 2: link with the generated stubs
		cc $cflags $DEF "$@" $INC tests/test_sweep_console_tap.c "$CONSOLE_SRC" \
			"$stubs" -o "$bin" 2> "$TMP/link2_$label.txt"
	fi

	if [ ! -x "$bin" ]; then
		echo "build failed:"
		tail -20 "$TMP/link2_$label.txt"
		STATUS=1
		return
	fi

	"$bin" || STATUS=1
}

run_config sweep   -DSHIPPING_VERSION=1 -DONI_SWEEP_CONSOLE=1
run_config debugfs -DSHIPPING_VERSION=0

rm -rf "$TMP"
exit $STATUS
