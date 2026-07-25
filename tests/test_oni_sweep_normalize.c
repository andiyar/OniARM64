// ======================================================================
// test_oni_sweep_normalize.c
//
// Standalone unit tests for the pure normalization helper in
// Oni_Sweep_Normalize.c. No framework needed — compile and run directly:
//
//   cc -Wall -Wextra tests/test_oni_sweep_normalize.c \
//      OniProj/OniGameSource/Oni_Sweep_Normalize.c \
//      -o /tmp/test_oni_sweep_normalize
//   /tmp/test_oni_sweep_normalize
//
// Covers the five normalisation rules, the truncation path (the only code
// that can silently destabilise a committed baseline), the guarantee that a
// key does not depend on the caller's buffer size, and the small-buffer /
// NULL argument safety cases.
// ======================================================================
#include "../OniProj/OniGameSource/Oni_Sweep_Normalize.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void report(const char *label, const char *input, const char *expected, const char *actual)
{
	if (strcmp(actual, expected) == 0) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n  input:    %s\n  expected: %s\n  actual:   %s\n",
			label, input, expected, actual);
	}
}

static void check(const char *label, const char *input, const char *expected)
{
	char out[128];
	ONrSweep_NormalizeKey(input, out, sizeof(out));
	report(label, input ? input : "(NULL)", expected, out);
}

// Same as check(), but hands the normaliser an explicit outSize smaller than
// the real array, so the small-buffer paths can be exercised safely.
static void check_sized(const char *label, const char *input, size_t bufSize, const char *expected)
{
	char out[256];
	ONrSweep_NormalizeKey(input, out, bufSize);
	report(label, input, expected, out);
}

static void check_true(const char *label, int cond)
{
	if (cond) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n", label);
	}
}

// A message whose slug runs well past the 96-char cap. Expected key counted by
// hand, word by word — the running total is the key length after each word and
// its trailing separator:
//   particle 9, class 15, s 17, failed 24, to 27, load 32, because 40, the 44,
//   geometry 53, stream 60, referenced 71, an 74, instance 83, that 88,
//   does 93, not 96 — "not" lands on exactly the 96th character, so everything
//   from " exist" onwards is dropped and no trailing separator survives.
static const char kLongMessage[] =
	"Particle class 'x' failed to load because the geometry stream referenced "
	"an instance that does not exist in this level at all";
static const char kLongExpected[] =
	"particle-class-s-failed-to-load-because-the-geometry-stream-referenced-"
	"an-instance-that-does-not";

int main(void)
{
	// the w10_sni_p01 finding, the motivating case
	check("particle size class",
		"Particle class 'w10_sni_p01' is too large (268) for largest size class (256)!",
		"particle-class-s-is-too-large-n-for-largest-size-class-n");

	// two runs differing only in the class name must produce the same key
	check("subject varies, key stable",
		"Particle class 'foo_bar_99' is too large (512) for largest size class (256)!",
		"particle-class-s-is-too-large-n-for-largest-size-class-n");

	check("pointer stripped",
		"bad node at 0x7ffee4b2c110",
		"bad-node-at-p");

	check("negative and float numbers",
		"flag was not inside a BNV -12.500000 3.000000 0.250000",
		"flag-was-not-inside-a-bnv-n-n-n");

	check("double quotes",
		"error loading script file \"level2_logic.bsl\"",
		"error-loading-script-file-s");

	check("empty input", "", "");

	check("punctuation collapses",
		"Could not   initialize:: game state...",
		"could-not-initialize-game-state");

	// --- truncation ----------------------------------------------------
	// The hand count above is only worth trusting if it is itself checked.
	check_true("expected long key is exactly 96 chars",
		strlen(kLongExpected) == 96);
	check_true("long message really does overflow the cap",
		strlen(kLongMessage) > 96);

	// Given a buffer far larger than the cap, the cap is still what bounds
	// the key. Removing the cap makes this fail.
	check_sized("truncates at 96 chars", kLongMessage, 256, kLongExpected);

	// The property ONcSweep_KeyBufferSize exists to guarantee: the game and
	// the sweep_diff CLI must derive the same key for the same message.
	{
		char keyGame[ONcSweep_KeyBufferSize];
		char keyTool[256];

		ONrSweep_NormalizeKey(kLongMessage, keyGame, sizeof(keyGame));
		ONrSweep_NormalizeKey(kLongMessage, keyTool, sizeof(keyTool));

		check_true("key is independent of caller buffer size",
			strcmp(keyGame, keyTool) == 0);
		check_true("both buffer sizes yield the capped key",
			strcmp(keyGame, kLongExpected) == 0);
	}

	// --- argument safety ------------------------------------------------
	check_sized("outSize of 1 yields empty key", "hello world", 1, "");
	check_sized("outSize of 2 yields one char", "hello world", 2, "h");
	check("NULL message", NULL, "");

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
