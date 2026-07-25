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
fi

rec() { # rec <renderer> <level> <phase> <subject> <severity> <key> <msg>
	printf '{"renderer":"%s","level":%s,"phase":"%s","subject":"%s","severity":"%s","key":"%s","msg":"%s"}\n' \
		"$1" "$2" "$3" "$4" "$5" "$6" "$7"
}

: > "$TMP/empty.ndjson"   # no findings at all
: > "$TMP/empty.txt"      # nothing baselined

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
# Empty report, so a bogus entry would show up as stale (exit 2) instead of 0 —
# the trailing-comment case above passes either way, since sscanf stops at four.
printf '2 particles w10_sni_p01 # too-large\n' > "$TMP/comment_as_field.txt"
check '"$DIFF" "$TMP/empty.ndjson" "$TMP/comment_as_field.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "comment in field position is not a key"

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

# ---------------------------------------------------------------- exit 2
check '"$DIFF" "$TMP/empty.ndjson" "$TMP/clean.txt" > "$TMP/out" 2>&1; [ $? -eq 2 ]' "stale-only exits 2"
check 'grep -q "^STALE      level 2  particles  w10_sni_p01  too-large$" "$TMP/out"' "stale line printed"

# a skipped cell drops its records, so that level's baseline reads as stale.
# Correct and deliberate: exit 2 is churn, not failure, and the skip is visible
# in the report — but a partial sweep should not be diffed as if it were whole.
rec gl 2 load level2 skipped no-data "skipped" > "$TMP/skip.ndjson"
check '"$DIFF" "$TMP/skip.ndjson" "$TMP/clean.txt" >/dev/null 2>&1; [ $? -eq 2 ]' "skipped cell leaves its baseline stale"

# malformed baseline lines (fewer than four fields) are skipped, not fatal
printf '2 particles w10_sni_p01\nnot a baseline line\n2 particles w10_sni_p01 too-large\n' > "$TMP/malformed.txt"
check '"$DIFF" "$TMP/clean.ndjson" "$TMP/malformed.txt" >/dev/null 2>&1; [ $? -eq 0 ]' "malformed baseline lines skipped"

# ---------------------------------------------------------------- exit 3
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
check '"$DIFF" "$TMP/empty.ndjson" "$TMP/huge.txt" 2>/dev/null >/dev/null; [ $? -eq 3 ]' "baseline overflow exits 3"

# exactly at the limit is fine
awk 'BEGIN{for(i=0;i<4096;i++)printf "{\"renderer\":\"gl\",\"level\":2,\"phase\":\"particles\",\"subject\":\"s%d\",\"severity\":\"warn\",\"key\":\"too-large\",\"msg\":\"x\"}\n", i}' \
	> "$TMP/atlimit.ndjson"
awk 'BEGIN{for(i=0;i<4096;i++)printf "2 particles s%d too-large\n", i}' > "$TMP/atlimit.txt"
check '"$DIFF" "$TMP/atlimit.ndjson" "$TMP/atlimit.txt" > "$TMP/out" 2>&1; [ $? -eq 0 ]' "exactly at the limit is clean"
check 'grep -q "0 regression(s), 0 stale, 0 abort(s)" "$TMP/out"' "full-size run diffs correctly"

echo "$PASS passed, $FAIL failed"; rm -rf "$TMP"; exit $((FAIL>0))
