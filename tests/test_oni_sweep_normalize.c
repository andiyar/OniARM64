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
// ======================================================================
#include "../OniProj/OniGameSource/Oni_Sweep_Normalize.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *label, const char *input, const char *expected)
{
	char out[128];
	ONrSweep_NormalizeKey(input, out, sizeof(out));
	if (strcmp(out, expected) == 0) {
		g_pass++;
	} else {
		g_fail++;
		printf("FAIL %s\n  input:    %s\n  expected: %s\n  actual:   %s\n",
			label, input, expected, out);
	}
}

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

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
