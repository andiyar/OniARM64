#!/bin/sh
# ======================================================================
# sweep.sh — run the level sweep (#103) across every level and renderer,
# merge the reports, and diff against the committed baselines.
#
#   scripts/sweep.sh                 run all cells, gate against baselines
#   scripts/sweep.sh --accept        run all cells, rewrite the baselines
#   scripts/sweep.sh --diff-only     gate the merged reports from the last
#                                    run without re-running any cells
#   scripts/sweep.sh --accept-only   rewrite the baselines from the last
#                                    run without re-running any cells
#
# The *-only modes exist for the triage loop: run once, read the findings,
# accept, hand-edit the baselines, re-gate — all against the SAME run's
# data. Re-running between triage steps would gate against different
# findings than the ones triaged.
#
# Environment overrides:
#   SWEEP_LEVELS        levels to sweep    (default: all 15)
#   SWEEP_RENDERERS     renderers          (default: "gl metal")
#   SWEEP_CELL_TIMEOUT  per-cell watchdog, seconds  (default: 600)
#   SWEEP_REPORT_CAP    per-cell report cap, bytes  (default: 64 MiB)
#
# Exit: 0 clean, 1 regressions or aborts, 2 only stale entries,
#       3 setup/IO failure or unusable report.
#
# Each cell runs in its own scratch working directory containing a
# zero-byte persist.dat. That file is the whole safety story: the shipped
# BSL corpus holds 56 save_game calls, and ONrPersist() resolves its path
# at write time to ./persist.dat only when one exists in the cwd —
# otherwise it rewrites the player's real saved games in Application
# Support, once per cell. The zero-byte file also fails the engine's
# version check, so every cell starts from clean persisted defaults
# (default difficulty included), which pins the nondeterminism reported
# in #106. The script phase refuses to run without a cwd-local
# persist.dat, so a driver bug here fails safe and visible, not silent.
#
# Two things about a running cell that look wrong but are not:
#   - The window is real and steals focus. Each cell opens a normal SDL
#     window; a full run means 30 of them appearing and vanishing.
#     SDL_VIDEODRIVER=dummy is passed through untouched if you set it,
#     but it is UNVERIFIED and may break renderer init — if it does, the
#     cells will abort loudly rather than pass quietly.
#   - macOS will beachball the process. The sweep loop never pumps
#     events (UUrPlatform_Idle lives in the bypassed display path), so
#     "unresponsive" is the expected steady state, not a hang. The
#     watchdog is the only judge of hung.
#
# A cell's exit status only reports crashes: main() returns 0 even when
# engine init fails. The driver therefore judges each cell by its report:
# a healthy cell always ends with a "phase":"done" terminal record, and
# anything else gets an abort record injected here, so it fails the gate
# rather than thinning the merge silently.
# ======================================================================
set -u

LEVELS=${SWEEP_LEVELS:-"0 1 2 3 4 6 8 9 10 11 12 13 14 18 19"}
RENDERERS=${SWEEP_RENDERERS:-"gl metal"}
CELL_TIMEOUT=${SWEEP_CELL_TIMEOUT:-600}
REPORT_CAP=${SWEEP_REPORT_CAP:-67108864}

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BINARY="$REPO_ROOT/build/bin/OniSweep"
DIFF_TOOL="$REPO_ROOT/build/sweep_diff"
DIFF_SRC="$REPO_ROOT/tools/sweep_diff.c"
OUT_DIR="$REPO_ROOT/build/sweep-out"

MODE=gate
case "${1:-}" in
	"")            MODE=gate ;;
	--accept)      MODE=accept ;;
	--diff-only)   MODE=gate_only ;;
	--accept-only) MODE=accept_only ;;
	*)
		echo "usage: $0 [--accept | --diff-only | --accept-only]" >&2
		exit 3
		;;
esac

case "$MODE" in gate|accept) DO_RUN=1 ;; *) DO_RUN=0 ;; esac
case "$MODE" in accept|accept_only) DO_ACCEPT=1 ;; *) DO_ACCEPT=0 ;; esac

# The diff tool has no CMake target; build it here when missing or stale.
if [ ! -x "$DIFF_TOOL" ] || [ "$DIFF_SRC" -nt "$DIFF_TOOL" ]; then
	echo "sweep: building sweep_diff..."
	cc -Wall -Wextra -o "$DIFF_TOOL" "$DIFF_SRC" || {
		echo "sweep: could not build sweep_diff" >&2
		exit 3
	}
fi

if [ "$DO_RUN" -eq 1 ]; then
	# OniSweep, not Oni: same sources, but with the console tap un-gated so
	# console diagnostics reach the report. The target is EXCLUDE_FROM_ALL.
	[ -x "$BINARY" ] || {
		echo "sweep: missing $BINARY — build with: make OniSweep" >&2
		exit 3
	}

	# GL cells block during character-spawn/texture work when the display is
	# asleep (2026-07-26 run 2: with the display off, every GL cell past
	# level 0 hit the watchdog at the exact point run 1 passed display-on;
	# Metal was unaffected either way). Wake the display and hold it awake
	# for the run. -w $$ ties the assertion to this script's lifetime, and
	# the game stays a direct child of the cell subshell so the watchdog's
	# kill semantics are unchanged.
	if command -v caffeinate >/dev/null 2>&1; then
		caffeinate -u -t 1 2>/dev/null
		caffeinate -di -w $$ &
	fi

	rm -rf "$OUT_DIR"
	mkdir -p "$OUT_DIR"
else
	[ -d "$OUT_DIR" ] || {
		echo "sweep: $OUT_DIR does not exist — run scripts/sweep.sh first" >&2
		exit 3
	}
fi

# Append a driver-authored abort record. Field order and escaping mirror
# Oni_Sweep_Report.c; sweep_diff parses every non-blank line strictly, so
# a malformed record here would poison the whole merge. Arguments must not
# contain double quotes or backslashes — every caller below passes fixed
# ASCII.
inject_abort() {
	printf '{"renderer":"%s","level":%d,"phase":"driver","subject":"%s","severity":"abort","key":"%s","msg":"%s"}\n' \
		"$1" "$2" "$3" "$4" "$5" >> "$6"
}

# Run one cell. A hung tick loop produces no exit code and a runaway
# console loop produces no exit either — wall time and report size are
# the only two signals the driver can act on.
run_cell() {
	renderer=$1
	level=$2
	report="$OUT_DIR/$renderer-$level.ndjson"
	cell_log="$OUT_DIR/$renderer-$level.log"
	sandbox="$OUT_DIR/cell-$renderer-$level"

	renderer_flag=
	[ "$renderer" = "metal" ] && renderer_flag="-metal"

	mkdir -p "$sandbox"
	: > "$sandbox/persist.dat"

	# Re-fire the display wake per cell: the single run-start wake proved
	# unreliable on 2026-08-25 (display slept before the run; every GL cell
	# watchdogged in the load phase while Metal passed untouched). The -di
	# hold below only PREVENTS sleep, it does not undo sleep that already
	# happened, and -u is cheap.
	if [ "$renderer" = "gl" ] && command -v caffeinate >/dev/null 2>&1; then
		caffeinate -u -t 2 2>/dev/null
	fi

	cell_start=$(date +%s)

	# ONI_RENDERER is force-cleared: the engine honours it when no renderer
	# flag is given, so a stray ONI_RENDERER=metal in the caller's shell
	# would silently turn every GL cell into a Metal cell.
	# exec so $! is the engine process, not the subshell wrapping the cd.
	(
		cd "$sandbox" || exit 97
		ONI_RENDERER=
		export ONI_RENDERER
		exec "$BINARY" -sweep "$level" -sweepout "$report" $renderer_flag -nosound
	) > "$cell_log" 2>&1 &
	cell_pid=$!

	verdict=ran
	waited=0
	while kill -0 "$cell_pid" 2>/dev/null; do
		if [ -f "$report" ] && [ "$(stat -f %z "$report")" -gt "$REPORT_CAP" ]; then
			kill -9 "$cell_pid" 2>/dev/null
			verdict=cap
			break
		fi
		if [ "$waited" -ge "$CELL_TIMEOUT" ]; then
			kill -9 "$cell_pid" 2>/dev/null
			verdict=timeout
			break
		fi
		sleep 2
		waited=$((waited + 2))
	done

	wait "$cell_pid" 2>/dev/null
	status=$?
	cell_seconds=$(( $(date +%s) - cell_start ))

	touch "$report"

	# A kill can land mid-write; sweep_diff refuses the whole merge over one
	# torn line. If the last line has no newline it is that torn fragment —
	# drop it. The abort record injected below stands in for the loss.
	if [ -s "$report" ] && [ -n "$(tail -c 1 "$report")" ]; then
		sed -i '' -e '$d' "$report"
	fi

	case "$verdict" in
	timeout)
		inject_abort "$renderer" "$level" "watchdog" "cell-timed-out" \
			"cell exceeded ${CELL_TIMEOUT}s of wall time" "$report"
		outcome="TIMEOUT after ${CELL_TIMEOUT}s"
		;;
	cap)
		inject_abort "$renderer" "$level" "report" "report-cap-exceeded" \
			"report exceeded ${REPORT_CAP} bytes" "$report"
		outcome="REPORT CAP exceeded"
		;;
	*)
		if [ "$status" -ne 0 ]; then
			inject_abort "$renderer" "$level" "exit" "cell-exited-nonzero" \
				"exit status $status" "$report"
			outcome="EXIT $status"
		elif ! grep -q '"phase":"done"' "$report"; then
			# Exit 0 with no terminal record: main() returns 0 even when
			# engine init fails, so this is a cell that died quietly.
			inject_abort "$renderer" "$level" "lifecycle" "cell-died-early" \
				"exited 0 without the terminal done record" "$report"
			outcome="DIED EARLY (exit 0, no done record)"
		else
			outcome="ok"
		fi
		;;
	esac

	# The engine falls back to GL silently when Metal is unavailable, and
	# its records would then gate against the Metal baseline. Engine-written
	# records carry the renderer the engine actually used; driver-injected
	# ones carry the cell label, so grep for the engine's own evidence.
	if [ "$renderer" = "metal" ] && grep -q '"renderer":"gl"' "$report"; then
		inject_abort "$renderer" "$level" "renderer" "renderer-fell-back" \
			"metal cell produced gl records - engine fell back to OpenGL" "$report"
		outcome="$outcome + METAL FELL BACK TO GL"
	fi

	# Keep the engine's own per-cell logs for triage; they land cwd-local
	# in the sandbox (and would otherwise pollute the player's real logs).
	for log_name in startup.txt debugger.txt; do
		[ -f "$sandbox/$log_name" ] && \
			mv "$sandbox/$log_name" "$OUT_DIR/$renderer-$level-$log_name"
	done
	rm -rf "$sandbox"

	echo "$renderer $level $cell_seconds $outcome" >> "$OUT_DIR/timing.txt"
	echo "  $renderer level $level: $outcome (${cell_seconds}s)"
}

# Rewrite one renderer's baseline from its merged report.
# skipped/leak never gate so they never belong in a baseline; abort
# records always gate regardless of baseline, so accepting them would
# only manufacture permanent stale entries.
accept_baseline() {
	merged=$1
	baseline=$2

	{
		echo "# sweep baseline — regenerated by scripts/sweep.sh --accept"
		echo "# format: <level> <phase> <subject> <key>   # reason"
		sed -n 's/.*"level":\([0-9][0-9]*\),"phase":"\([^"]*\)","subject":"\([^"]*\)","severity":"\([^"]*\)","key":"\([^"]*\)".*/\4 \1 \2 \3 \5/p' "$merged" \
			| grep -v '^skipped ' | grep -v '^leak ' | grep -v '^abort ' \
			| awk '{ sub(/^[^ ]+ /, "");
				if (NF == 4) print $0 "   # accepted, review me";
				else         print "# UNPARSEABLE (a field contains spaces; fix by hand): " $0 }' \
			| sort -u
	} > "$baseline"

	if grep -q '^# UNPARSEABLE' "$baseline"; then
		echo "sweep: WARNING — $baseline has entries sweep_diff cannot parse; fix them by hand" >&2
	fi
	echo "baseline written: $baseline"
}

had_regression=0
had_stale=0
had_broken=0

for renderer in $RENDERERS; do
	merged="$OUT_DIR/merged-$renderer.ndjson"

	if [ "$DO_RUN" -eq 1 ]; then
		echo "sweeping $renderer..."
		for level in $LEVELS; do
			run_cell "$renderer" "$level"
		done
		cat "$OUT_DIR/$renderer"-*.ndjson > "$merged"
	fi

	[ -f "$merged" ] || {
		echo "sweep: $merged missing — run scripts/sweep.sh first" >&2
		exit 3
	}

	if [ "$DO_ACCEPT" -eq 1 ]; then
		# An aborted cell's findings are missing from the merge, so a
		# baseline cut from this run would report that cell's real content
		# as regressions forever after. Refuse rather than bake that in.
		if grep -q '"severity":"abort"' "$merged"; then
			echo "sweep: $renderer run contains abort records — fix those cells before accepting a baseline" >&2
			exit 1
		fi
		accept_baseline "$merged" "$REPO_ROOT/tests/sweep-baseline-$renderer.txt"
	else
		baseline="$REPO_ROOT/tests/sweep-baseline-$renderer.txt"
		if [ ! -f "$baseline" ]; then
			echo "note: $baseline does not exist yet — every finding below is a regression by definition"
			baseline=/dev/null
		fi
		echo "--- $renderer diff ---"
		"$DIFF_TOOL" "$merged" "$baseline"
		case $? in
			1) had_regression=1 ;;
			2) had_stale=1 ;;
			3) had_broken=1 ;;
		esac
	fi
done

[ -f "$OUT_DIR/timing.txt" ] && {
	echo "per-cell wall time (see also $OUT_DIR/timing.txt):"
	sort -k3 -rn "$OUT_DIR/timing.txt" | head -5 | \
		awk '{ printf "  slowest: %s level %s: %ss\n", $1, $2, $3 }'
}

if [ "$DO_ACCEPT" -eq 1 ]; then
	exit 0
fi
if [ "$had_broken" -eq 1 ]; then exit 3; fi
if [ "$had_regression" -eq 1 ]; then exit 1; fi
if [ "$had_stale" -eq 1 ]; then exit 2; fi
exit 0
