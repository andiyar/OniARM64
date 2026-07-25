#!/usr/bin/env bash
# test_sweep_diff_cli.sh — end-to-end test of the sweep_diff CLI: NDJSON and
# baseline parsing, severity filtering, and every exit code (0/1/2/3).
# The diff logic itself is unit-tested in tests/test_sweep_diff.c.
#
# Usage: tests/test_sweep_diff_cli.sh [path-to-sweep_diff]     (from repo root)
# Builds the tool itself if no path is given.
set -u
DIFF="${1:-}"
TMP=$(mktemp -d); PASS=0; FAIL=0
check() { if eval "$1"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); echo "FAIL: $2"; fi }

if [ -z "$DIFF" ]; then
	DIFF="$TMP/sweep_diff"
	cc -Wall -Wextra -o "$DIFF" tools/sweep_diff.c || { echo "build failed"; exit 1; }
elif [ ! -x "$DIFF" ] || [ -d "$DIFF" ]; then
	# The other sweep runners (test_sweep_core.sh, test_sweep_console_tap.sh)
	# take a BUILD DIRECTORY; this one takes a BINARY. Handing it the wrong one
	# used to run every case against an unrunnable path and report "3 passed,
	# 56 failed", which reads like a broken tool rather than a mistyped
	# argument. Say so instead.
	echo "usage: $0 [path-to-sweep_diff]     (a binary, not a build directory)"
	echo "  got: $DIFF"
	rm -rf "$TMP"
	exit 2
fi

rec() { # rec <renderer> <level> <phase> <subject> <severity> <key> <msg>
	printf '{"renderer":"%s","level":%s,"phase":"%s","subject":"%s","severity":"%s","key":"%s","msg":"%s"}\n' \
		"$1" "$2" "$3" "$4" "$5" "$6" "$7"
}

: > "$TMP/empty.ndjson"   # no records at all — rejected, see the exit-3 block
: > "$TMP/empty.txt"      # nothing baselined, which is the legitimate start state

# ---------------------------------------------------------------- exit 0
rec gl 2 particles w10_sni_p01 warn too-large "x" > "$TMP/clean.ndjson"
printf '2 particles w10_sni_p01 too-large  # accepted, #10\n' > "$TMP/clean.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/clean.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' "clean run exits 0"
check 'grep -q "0 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "clean summary line"

# blank lines, whole-line comments, leading space and inline comments all parse
{ echo ""; echo "# a comment"; echo "   # indented comment"
  echo "  2 particles w10_sni_p01 too-large   # inline"; } > "$TMP/comments.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/comments.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "comments and blanks ignored"

# a comment sitting where the fourth field would be must not be read as a key.
# The line is then short a field, so it is rejected outright (exit 3) rather
# than quietly accepting "#" as the key — the trailing-comment case above
# passes either way, since sscanf stops after four fields.
printf '2 particles w10_sni_p01 # too-large\n' > "$TMP/comment_as_field.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/comment_as_field.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "comment in field position is not a key"
check 'grep -q "is not a baseline entry" "$TMP/err"' "short baseline line explained"

# the baseline holds no renderer, so the same file gates either renderer
rec metal 2 particles w10_sni_p01 warn too-large "x" > "$TMP/metal.ndjson"
check '"$DIFF" "$TMP/metal.ndjson" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "renderer field not part of identity"

# skipped and leak are reported by the sweep but must never gate
{ rec gl 5 load level5 skipped no-data "skipped"
  rec gl 5 shutdown heap leak bytes-leaked "leaked"; } > "$TMP/nogate.ndjson"
check '"$DIFF" "$TMP/nogate.ndjson" "$TMP/empty.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' "skipped and leak never gate"
check 'grep -q "0 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "skipped and leak not counted"

# escaped quote in subject: the writer emits \" and the reader must match the
# baseline's literal a"b (whitespace-delimited, so the quote is just a char)
rec gl 2 particles 'a\"b' warn too-large "x" > "$TMP/esc.ndjson"
printf '2 particles a"b too-large\n' > "$TMP/esc.txt"
check '"$DIFF" "$TMP/esc.ndjson" "$TMP/esc.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' 'escaped quote in subject round-trips'

# a msg carrying a lookalike field must not shadow the real one: msg is last and
# every quote inside it is escaped, so the first strstr hit is always genuine
rec gl 2 particles w10_sni_p01 warn too-large 'saw \"key\":\"decoy\" here' > "$TMP/decoy.ndjson"
check '"$DIFF" "$TMP/decoy.ndjson" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "lookalike field in msg ignored"

# ---------------------------------------------------------------- exit 1
rec gl 12 scripts spawn_guards error null-deref "boom" > "$TMP/regress.ndjson"
check '"$DIFF" "$TMP/regress.ndjson" "$TMP/empty.txt" > "$TMP/out" 2>&1; [ $? -eq 1 ]' "regression exits 1"
check 'grep -q "^REGRESSION level 12  scripts  spawn_guards  null-deref$" "$TMP/out"' "regression line printed"

# an abort fails even when the baseline accepts that identity
rec gl 9 load level9 abort crash "died" > "$TMP/abort.ndjson"
printf '9 load level9 crash\n' > "$TMP/abort.txt"
check '"$DIFF" "$TMP/abort.ndjson" "$TMP/abort.txt" > "$TMP/out" 2>&1; [ $? -eq 1 ]' "baselined abort still exits 1"
check 'grep -q "^ABORT      level 9  load  level9  crash$" "$TMP/out"' "abort line printed"
check 'grep -q "0 regression(s), 0 stale, 1 abort(s)" "$TMP/out"' "abort not double-counted as regression"

# duplicates of one un-baselined identity are one regression, not many
{ rec gl 2 particles dup warn too-large "x"
  rec gl 2 particles dup warn too-large "x"
  rec gl 2 particles dup warn too-large "x"; } > "$TMP/dup.ndjson"
check '"$DIFF" "$TMP/dup.ndjson" "$TMP/empty.txt" > "$TMP/out" 2>&1; [ $? -eq 1 ]' "duplicates exit 1"
check 'grep -q "1 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "duplicates counted once"
check '[ "$(grep -c REGRESSION "$TMP/out")" = "1" ]' "duplicates printed once"
# the count is dropped but the volume is not: multiplicity rides along as a suffix
check 'grep -q "^REGRESSION level 2  particles  dup  too-large  x3$" "$TMP/out"' "multiplicity suffix printed"
check '! grep -q "x1$" "$TMP/out"' "no suffix on a singleton"

# distinct findings must each be counted — dedupe collapses repeats within an
# identity, never across identities. This is the failure mode that hides bugs.
{ rec gl 2  particles class_a      warn  too-large  "x"
  rec gl 7  textures  wall_01      warn  not-square "x"
  rec gl 12 scripts   spawn_guards error null-deref "x"
  rec gl 2  particles class_a      warn  too-large  "x"; } > "$TMP/distinct.ndjson"
check '"$DIFF" "$TMP/distinct.ndjson" "$TMP/empty.txt" > "$TMP/out" 2>&1; [ $? -eq 1 ]' "distinct findings exit 1"
check 'grep -q "3 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "distinct findings all counted"
check '[ "$(grep -c REGRESSION "$TMP/out")" = "3" ]' "distinct findings all printed"

# ---------------------------------------------------------------- exit 2
# a report of nothing but skipped/leak records parses fine and gates on nothing,
# so the baseline it does not cover comes back stale
check '"$DIFF" "$TMP/nogate.ndjson" "$TMP/clean.txt" > "$TMP/out" 2>&1; [ $? -eq 2 ]' "stale-only exits 2"
check 'grep -q "^STALE      level 2  particles  w10_sni_p01  too-large$" "$TMP/out"' "stale line printed"

# a skipped cell drops its records, so that level's baseline reads as stale.
# Correct and deliberate: exit 2 is churn, not failure, and the skip is visible
# in the report — but a partial sweep should not be diffed as if it were whole.
rec gl 2 load level2 skipped no-data "skipped" > "$TMP/skip.ndjson"
check '"$DIFF" "$TMP/skip.ndjson" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 2 ]' "skipped cell leaves its baseline stale"

# ---------------------------------------------------------------- exit 3
# A malformed baseline line is a typo in a hand-edited file. Skipping it would
# leave the maintainer believing they accepted a finding while the gate keeps
# reporting it, so it is named and fatal.
printf '2 particles w10_sni_p01 too-large\nnot a baseline line\n' > "$TMP/malformed.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/malformed.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "malformed baseline line is fatal"
check 'grep -q "malformed.txt:2 is not a baseline entry" "$TMP/err"' "malformed baseline names the line"

# A report that parses to nothing must never read as clean. fopen succeeds on a
# directory and an empty file has no records, so without these the gate would
# wave through exactly the runs it exists to catch: a runner that died early.
check '"$DIFF" "$TMP/empty.ndjson" "$TMP/empty.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "empty report is fatal"
check 'grep -q "holds no sweep records" "$TMP/err"' "empty report explained"
check '"$DIFF" "$TMP" "$TMP/empty.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "directory as report is fatal"
check 'grep -q "cannot read report" "$TMP/err"' "directory as report explained"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "directory as baseline is fatal"

# an unparseable line is a truncated or interleaved write, not something to skip
printf 'total 48\ndrwxr-xr-x  5 me  staff  160 Jul 25 10:00 .\n' > "$TMP/garbage.ndjson"
check '"$DIFF" "$TMP/garbage.ndjson" "$TMP/empty.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "garbage report is fatal"
check 'grep -q "garbage.ndjson:1 is not a sweep record" "$TMP/err"' "garbage report names the line"

# one good record followed by a half-written one: the run cannot be trusted
{ rec gl 2 particles w10_sni_p01 warn too-large "x"
  printf '{"renderer":"gl","level":2,"phase":"particles","subj\n'; } > "$TMP/partial.ndjson"
check '"$DIFF" "$TMP/partial.ndjson" "$TMP/clean.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "half-written record is fatal"
check 'grep -q "partial.ndjson:2 is not a sweep record" "$TMP/err"' "half-written record names the line"

# a non-numeric level must be rejected, not silently read as level 0 (a real
# level — the main menu — so atoi would have gated it against level 0's baseline)
printf '{"renderer":"gl","level":x2,"phase":"p","subject":"s","severity":"warn","key":"k","msg":"m"}\n' > "$TMP/badlevel.ndjson"
check '"$DIFF" "$TMP/badlevel.ndjson" "$TMP/empty.txt" 2>/dev/null >/dev/null; [ $? -eq 3 ]' "non-numeric level is fatal"

# a level outside any plausible range is corruption too, and strtol would
# otherwise hand back a negative or a clamped LONG_MAX as if it were a level
printf '{"renderer":"gl","level":-5,"phase":"p","subject":"s","severity":"warn","key":"k","msg":"m"}\n' > "$TMP/neglevel.ndjson"
check '"$DIFF" "$TMP/neglevel.ndjson" "$TMP/empty.txt" 2>/dev/null >/dev/null; [ $? -eq 3 ]' "negative level is fatal"
printf '{"renderer":"gl","level":99999999999999,"phase":"p","subject":"s","severity":"warn","key":"k","msg":"m"}\n' > "$TMP/biglevel.ndjson"
check '"$DIFF" "$TMP/biglevel.ndjson" "$TMP/empty.txt" 2>/dev/null >/dev/null; [ $? -eq 3 ]' "out-of-range level is fatal"

# blank lines in the middle of a report are tolerated (trailing newlines from
# concatenating per-cell files), they just are not records
{ rec gl 2 particles w10_sni_p01 warn too-large "x"; echo ""; echo "   "; } > "$TMP/blanks.ndjson"
check '"$DIFF" "$TMP/blanks.ndjson" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "blank lines in the report tolerated"

# the line buffer only has to cover the prefix through "key" — msg comes last,
# so a record far longer than the buffer still parses, and its overflow must not
# come back as a phantom extra line
MSG=$(awk 'BEGIN{for(i=0;i<3000;i++)printf "z"}')
rec gl 2 particles w10_sni_p01 warn too-large "$MSG" > "$TMP/longmsg.ndjson"
check '[ "$(wc -c < "$TMP/longmsg.ndjson")" -gt 2048 ]' "long-msg record exceeds the line buffer"
check '"$DIFF" "$TMP/longmsg.ndjson" "$TMP/clean.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' "record longer than the line buffer still parses"
check 'grep -q "0 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "long-msg overflow is not a phantom line"


check '"$DIFF" >/dev/null 2>&1; [ $? -eq 3 ]' "no arguments exits 3"
check '"$DIFF" "$TMP/clean.ndjson" >/dev/null 2>&1; [ $? -eq 3 ]' "one argument exits 3"
check '"$DIFF" a b c >/dev/null 2>&1; [ $? -eq 3 ]' "three arguments exits 3"
# surplus args must be refused outright, not ignored because the extra path
# happens not to be opened — both real files here, so only argc can reject it
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/clean.txt" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 3 ]' "surplus argument refused even when valid"
check '"$DIFF" "$TMP/nope.ndjson" "$TMP/clean.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "missing report exits 3"
check 'grep -q "cannot read report" "$TMP/err"' "missing report names the file"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/nope.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "missing baseline exits 3"
check 'grep -q "cannot read baseline" "$TMP/err"' "missing baseline names the file"

# overflow: more findings than the fixed set holds must abort the gate, never
# silently diff a truncated report
awk 'BEGIN{for(i=0;i<4097;i++)printf "{\"renderer\":\"gl\",\"level\":2,\"phase\":\"particles\",\"subject\":\"s%d\",\"severity\":\"warn\",\"key\":\"too-large\",\"msg\":\"x\"}\n", i}' \
	> "$TMP/huge.ndjson"
check '"$DIFF" "$TMP/huge.ndjson" "$TMP/empty.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "report overflow exits 3"
check 'grep -q "finding limit 4096 exceeded" "$TMP/err"' "overflow reported on stderr"
check '! grep -q "regression(s)" "$TMP/err"' "overflow reports no verdict"

awk 'BEGIN{for(i=0;i<4097;i++)printf "2 particles s%d too-large\n", i}' > "$TMP/huge.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/huge.txt" 2>"$TMP/err" >/dev/null; [ $? -eq 3 ]' "baseline overflow exits 3"
check 'grep -q "finding limit 4096 exceeded" "$TMP/err"' "baseline overflow reported on stderr"

# exactly at the limit is fine
awk 'BEGIN{for(i=0;i<4096;i++)printf "{\"renderer\":\"gl\",\"level\":2,\"phase\":\"particles\",\"subject\":\"s%d\",\"severity\":\"warn\",\"key\":\"too-large\",\"msg\":\"x\"}\n", i}' \
	> "$TMP/atlimit.ndjson"
awk 'BEGIN{for(i=0;i<4096;i++)printf "2 particles s%d too-large\n", i}' > "$TMP/atlimit.txt"
check '"$DIFF" "$TMP/atlimit.ndjson" "$TMP/atlimit.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' "exactly at the limit is clean"
check 'grep -q "0 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "full-size run diffs correctly"

echo "$PASS passed, $FAIL failed"; rm -rf "$TMP"; exit $((FAIL>0))
