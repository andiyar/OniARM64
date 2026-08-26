// ======================================================================
// test_oni_renderer_pref.c
//
// Standalone unit tests for the persisted renderer preference in
// Oni_RendererPref.c (#89). No framework needed — compile and run directly:
//
//   cc -Wall -Wextra -DUUmSDL=1 \
//      -IBungieFrameWork/BFW_Headers -IOniProj/OniGameSource \
//      -I/opt/homebrew/include \
//      tests/test_oni_renderer_pref.c OniProj/OniGameSource/Oni_RendererPref.c \
//      -o /tmp/test_oni_renderer_pref
//   /tmp/test_oni_renderer_pref
//
// (The BFW/SDL include flags are what BFW.h needs; -I/opt/homebrew/include
// is where SDL2 lives on this machine — `sdl2-config --cflags` names it.)
//
// Covers token parsing, the missing/garbage/empty/oversized file cases that
// must never be parsed into a renderer choice, the write/read round trip,
// and the resolver-backed wrappers via a stubbed ONiBundlePath_ResolveStateFile.
// ======================================================================
#include "Oni_RendererPref.h"
#include "ONi_BundlePath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)								\
	do {												\
		if (cond) {										\
			g_pass++;									\
		} else {										\
			g_fail++;									\
			printf("FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__);	\
		}												\
	} while (0)

// Stub of the bundle-path resolver: resolve into the test scratch dir so the
// wrapper functions are covered without touching the real App Support dir.
static char g_scratch[1024];

UUtError
ONiBundlePath_ResolveStateFile(
	const char				*filename,
	char					*outPath,
	size_t					outPathSize)
{
	snprintf(outPath, outPathSize, "%s/%s", g_scratch, filename);
	return UUcError_None;
}

int main(void)
{
	char					path[1100];
	char					cmd[1200];
	FILE					*file;
	int						i;

	snprintf(g_scratch, sizeof(g_scratch), "/tmp/oni-rp-test-%d", (int)getpid());
	snprintf(cmd, sizeof(cmd), "mkdir -p %s", g_scratch);
	if (system(cmd) != 0) { printf("could not create scratch dir\n"); return 1; }

	// --- ParseToken ---
	CHECK(ONrRendererPref_ParseToken("metal") == ONcRendererPref_Metal, "parse metal");
	CHECK(ONrRendererPref_ParseToken("Metal") == ONcRendererPref_Metal, "parse Metal");
	CHECK(ONrRendererPref_ParseToken("METAL\n") == ONcRendererPref_Metal, "parse METAL+newline");
	CHECK(ONrRendererPref_ParseToken("opengl") == ONcRendererPref_OpenGL, "parse opengl");
	CHECK(ONrRendererPref_ParseToken("gl") == ONcRendererPref_OpenGL, "parse gl");
	CHECK(ONrRendererPref_ParseToken("  opengl  \n") == ONcRendererPref_OpenGL, "parse padded opengl");
	CHECK(ONrRendererPref_ParseToken("vulkan") == ONcRendererPref_None, "parse unknown token");
	CHECK(ONrRendererPref_ParseToken("") == ONcRendererPref_None, "parse empty");
	CHECK(ONrRendererPref_ParseToken(NULL) == ONcRendererPref_None, "parse NULL");
	CHECK(ONrRendererPref_ParseToken("metallic") == ONcRendererPref_None, "parse metal prefix");
	CHECK(ONrRendererPref_ParseToken("   \n") == ONcRendererPref_None, "parse whitespace only");

	// --- ReadFromPath: missing file / NULL ---
	snprintf(path, sizeof(path), "%s/renderer.txt", g_scratch);
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_None, "read missing file");
	CHECK(ONrRendererPref_ReadFromPath(NULL) == ONcRendererPref_None, "read NULL path");

	// --- Write/Read round trip ---
	CHECK(ONrRendererPref_WriteToPath(path, UUcTrue) == UUcTrue, "write metal");
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_Metal, "read back metal");
	CHECK(ONrRendererPref_WriteToPath(path, UUcFalse) == UUcTrue, "write opengl");
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_OpenGL, "read back opengl");

	// --- Garbage file content ---
	file = fopen(path, "w");
	fputs("!!not a renderer!! extra junk line\nmore\n", file);
	fclose(file);
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_None, "read garbage");

	// --- Empty file ---
	file = fopen(path, "w");
	fclose(file);
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_None, "read empty file");

	// --- Oversized first line must not overflow or parse a truncation ---
	file = fopen(path, "w");
	for (i = 0; i < 4096; i++) { fputc('m', file); }
	fclose(file);
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_None, "read oversized line");

	// A first line that is oversized but starts "metal" must still be rejected
	// — truncation must never be mistaken for a valid token.
	file = fopen(path, "w");
	fputs("metal", file);
	for (i = 0; i < 4096; i++) { fputc('x', file); }
	fclose(file);
	CHECK(ONrRendererPref_ReadFromPath(path) == ONcRendererPref_None, "read oversized metal-prefixed line");

	// --- Resolver-backed wrappers (via the stub above) ---
	remove(path);
	CHECK(ONrRendererPref_Read() == ONcRendererPref_None, "wrapper read missing");
	CHECK(ONrRendererPref_Write(UUcTrue) == UUcTrue, "wrapper write metal");
	CHECK(ONrRendererPref_Read() == ONcRendererPref_Metal, "wrapper read metal");
	CHECK(ONrRendererPref_Write(UUcFalse) == UUcTrue, "wrapper write opengl");
	CHECK(ONrRendererPref_Read() == ONcRendererPref_OpenGL, "wrapper read opengl");

	// --- Unwritable path fails soft ---
	CHECK(ONrRendererPref_WriteToPath("/nonexistent-dir-xyz/renderer.txt", UUcTrue) == UUcFalse, "write to bad path");
	CHECK(ONrRendererPref_WriteToPath(NULL, UUcTrue) == UUcFalse, "write to NULL path");

	// clean up the scratch dir
	snprintf(cmd, sizeof(cmd), "rm -rf %s", g_scratch);
	if (system(cmd) != 0) { printf("could not remove scratch dir\n"); }

	printf("test_oni_renderer_pref: %d passed, %d failed\n", g_pass, g_fail);
	return (g_fail == 0) ? 0 : 1;
}
