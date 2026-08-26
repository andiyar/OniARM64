// ======================================================================
// Oni_RendererPref.c
//
// Persisted renderer preference (#89, Metal M5). See Oni_RendererPref.h.
// ======================================================================

// ======================================================================
// includes
// ======================================================================
#include "Oni_RendererPref.h"
#include "ONi_BundlePath.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// ======================================================================
// defines
// ======================================================================
#define ONcRendererPref_FileName	"renderer.txt"
#define ONcRendererPref_MaxLine		64

// ======================================================================
// functions
// ======================================================================
// ----------------------------------------------------------------------
ONtRendererPref
ONrRendererPref_ParseToken(
	const char				*inToken)
{
	char					trimmed[ONcRendererPref_MaxLine];
	char					*itr;
	size_t					length;
	size_t					start;
	size_t					end;

	if (inToken == NULL) { return ONcRendererPref_None; }

	length = strlen(inToken);
	if (length >= sizeof(trimmed)) { return ONcRendererPref_None; }

	start = 0;
	while ((start < length) && isspace((unsigned char)inToken[start])) { start++; }
	end = length;
	while ((end > start) && isspace((unsigned char)inToken[end - 1])) { end--; }
	if (end == start) { return ONcRendererPref_None; }

	memcpy(trimmed, inToken + start, end - start);
	trimmed[end - start] = '\0';
	for (itr = trimmed; *itr != '\0'; itr++)
	{
		*itr = (char)tolower((unsigned char)*itr);
	}

	if (strcmp(trimmed, "metal") == 0) { return ONcRendererPref_Metal; }
	if ((strcmp(trimmed, "opengl") == 0) || (strcmp(trimmed, "gl") == 0)) { return ONcRendererPref_OpenGL; }

	return ONcRendererPref_None;
}

// ----------------------------------------------------------------------
ONtRendererPref
ONrRendererPref_ReadFromPath(
	const char				*inPath)
{
	FILE					*file;
	char					line[ONcRendererPref_MaxLine];
	ONtRendererPref			pref;

	if (inPath == NULL) { return ONcRendererPref_None; }

	file = fopen(inPath, "r");
	if (file == NULL) { return ONcRendererPref_None; }

	pref = ONcRendererPref_None;
	if (fgets(line, sizeof(line), file) != NULL)
	{
		// A first line longer than the buffer comes back full and unterminated;
		// that is garbage, and parsing its truncation could turn "metalxxx..."
		// into a valid choice.
		UUtBool truncated =
			((strchr(line, '\n') == NULL) && (strlen(line) == sizeof(line) - 1));

		if (!truncated) { pref = ONrRendererPref_ParseToken(line); }
	}
	fclose(file);

	return pref;
}

// ----------------------------------------------------------------------
UUtBool
ONrRendererPref_WriteToPath(
	const char				*inPath,
	UUtBool					inUseMetal)
{
	FILE					*file;
	int						wrote;

	if (inPath == NULL) { return UUcFalse; }

	file = fopen(inPath, "w");
	if (file == NULL) { return UUcFalse; }

	wrote = fprintf(file, "%s\n", inUseMetal ? "metal" : "opengl");
	if (fclose(file) != 0) { return UUcFalse; }

	return (wrote > 0) ? UUcTrue : UUcFalse;
}

// ----------------------------------------------------------------------
ONtRendererPref
ONrRendererPref_Read(
	void)
{
	char					path[1024];

	if (ONiBundlePath_ResolveStateFile(ONcRendererPref_FileName, path, sizeof(path)) != UUcError_None)
	{
		return ONcRendererPref_None;
	}

	return ONrRendererPref_ReadFromPath(path);
}

// ----------------------------------------------------------------------
UUtBool
ONrRendererPref_Write(
	UUtBool					inUseMetal)
{
	char					path[1024];

	if (ONiBundlePath_ResolveStateFile(ONcRendererPref_FileName, path, sizeof(path)) != UUcError_None)
	{
		return UUcFalse;
	}

	return ONrRendererPref_WriteToPath(path, inUseMetal);
}
