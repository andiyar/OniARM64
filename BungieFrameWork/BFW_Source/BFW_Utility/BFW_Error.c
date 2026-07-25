/*
	FILE:	BFW_Error.c

	AUTHOR:	Brent H. Pease

	CREATED: May 18, 1997

	PURPOSE: Interface to the Motoko 3D engine               (uhh... hehe)

	Copyright 1997

*/


/*---------- headers */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "BFW.h"
#include "BFW_Console.h"
#include "BFW_Error.h"
#include "BFW_AppUtilities.h"

/*---------- constants */

#define UUmMaxHandlers	20
#define UUmMaxCodes		200

enum {
	_button_abort= 1,
	_button_retry,
	_button_ignore,

	MAC_ERROR_ALERT_RES_ID= 129,

	MAX_STRING_LENGTH= 256
};

/*---------- globals */

#if defined(DEBUGGING_STOPONERROR) && DEBUGGING_STOPONERROR
UUtBool UUgError_HaltOnError = UUcTrue;
#else
UUtBool UUgError_HaltOnError = UUcFalse;
#endif

UUtBool	UUgError_PrintWarning = UUcFalse;

FILE *iDebugFile = NULL;

/*---------- code */

// Issue #70 — verbose per-frame / per-instance diagnostic families default OFF
// for release users. ONI_DIAG_VERBOSE=1 re-enables them; the [tag]s stay in
// the code so they remain greppable across sessions (feedback_keep_diagnostics).
UUtBool UUrDiagVerbose(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *v = getenv("ONI_DIAG_VERBOSE");
		cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
	}
	return (UUtBool)cached;
}

// Issue #70 — append-mode logs grew unbounded (91 MB observed). At ~10 MB the
// existing log is renamed to <name>.old (replacing the previous .old) so a
// session always starts with headroom and history survives one generation.
#define ONI_LOG_ROTATE_BYTES (10u * 1024u * 1024u)
static void iRotateLogIfHuge(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > (off_t)ONI_LOG_ROTATE_BYTES) {
		char oldPath[1024];
		if (snprintf(oldPath, sizeof(oldPath), "%s.old", path) < (int)sizeof(oldPath)) {
			rename(path, oldPath);
		}
	}
}

// Open a log file with cwd-first, ~/Library/Logs/OniARM64/ fallback on macOS.
// Preserves the bare-binary workflow (OniNative/Oni writes to OniNative/startup.txt
// unchanged) while making the .app workflow (cwd = /) write to a known location.
// On non-Apple platforms, behaves identically to fopen(filename, mode).
static FILE *iOpenLogFile(const char *filename, const char *mode)
{
	iRotateLogIfHuge(filename);
	FILE *fp = fopen(filename, mode);
	if (fp != NULL) {
		return fp;
	}

#ifdef __APPLE__
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return NULL;
	}
	char dir[1024];
	if (snprintf(dir, sizeof(dir), "%s/Library/Logs/OniARM64", home) >= (int)sizeof(dir)) {
		return NULL;
	}
	// mkdir -p semantics: ignore EEXIST, fail on anything else only if mkdir fails to leave a usable dir.
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		return NULL;
	}
	char path[1024];
	if (snprintf(path, sizeof(path), "%s/%s", dir, filename) >= (int)sizeof(path)) {
		return NULL;
	}
	iRotateLogIfHuge(path);
	fp = fopen(path, mode);
#endif
	return fp;
}

// Issue #17 — append a "===== session start <timestamp> =====" banner so consecutive
// sessions in append-mode log files are visually delimited and greppable.
// Issue #46 — the banner also carries the build identity ("<branch>@<short-sha>
// [-dirty]", captured at CMake configure time) so interleaved sessions from
// coexisting dev builds are attributable to the binary that wrote them.
#ifndef ONI_BUILD_STAMP
#define ONI_BUILD_STAMP "unknown@unknown"
#endif
// Issue #72 — the release version (from macos/Info.plist at configure time)
// joins the banner so a user-supplied log names the release it came from.
#ifndef ONI_VERSION
#define ONI_VERSION "unknown"
#endif
static void iWriteSessionBanner(FILE *stream)
{
	if (stream == NULL) return;
	time_t now = time(NULL);
	struct tm *lt = localtime(&now);
	if (lt == NULL) return;
	fprintf(stream, "\n===== session start %04d-%02d-%02d %02d:%02d:%02d v" ONI_VERSION " [%s] =====\n",
		lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
		lt->tm_hour, lt->tm_min, lt->tm_sec, ONI_BUILD_STAMP);
	fflush(stream);
}

static void iAppendDebugFileMessage(char *msg)
{
	if (NULL == iDebugFile)
	{
#if defined(DEBUGGING) && DEBUGGING
                iDebugFile = stderr;
#else
		// Issue #17 — append mode preserves prior sessions across launches.
		iDebugFile = iOpenLogFile("debugger.txt", "ab");
		iWriteSessionBanner(iDebugFile);
#endif
	}

	if (iDebugFile != NULL)
	{
		fprintf(iDebugFile, "%s"UUmNL, msg);
		fflush(iDebugFile);
	}
}

#if (UUmPlatform == UUmPlatform_Win32)
static UUtBool iDebuggerMessageBox(char *msg)
{
	UUtBool return_value = UUcFalse;	// don't enter debugger

	iAppendDebugFileMessage(msg);

#if TOOL_VERSION
	OutputDebugString(msg);		// send mesage to the console (does not break)
	OutputDebugString(UUmNL);	// line break
	{
		char buffer[MAX_STRING_LENGTH];
		int msgbox_result;
		BOOL bQuit;
		MSG quitMsg;

		UUrString_Copy(buffer, msg, MAX_STRING_LENGTH);

		// remove a quit message if required (otherwise the dialog quits for us, thanks!)
		bQuit = PeekMessage(&quitMsg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE);

		// bring up the dialog
		UUrString_Cat(buffer, UUmNL UUmNL "(Retry will enter the debugger)", MAX_STRING_LENGTH);
		ShowCursor(TRUE);
		msgbox_result = MessageBox(NULL, buffer, "Assertion Failed",
							MB_TASKMODAL|MB_ICONHAND|MB_ABORTRETRYIGNORE|MB_SETFOREGROUND);
		ShowCursor(FALSE);

		// restore the quit message if required
		if (bQuit)
			PostQuitMessage(quitMsg.wParam);

		switch(msgbox_result)
		{
			case IDABORT:
				exit(3);				// quit application
			break;

			case IDRETRY:
				return_value = UUcTrue;		// enter debugger
			break;

			case IDIGNORE:
				return_value = UUcFalse;		// don't enter debugger
			break;
		}
	}
#endif

	return return_value;
}
#elif (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
static UUtBool iDebuggerMessageBox(
	char *msg)
{
	UUtBool return_value= UUcFalse;	// don't enter debugger

	iAppendDebugFileMessage(msg);

#if TOOL_VERSION
	{
		char buffer[MAX_STRING_LENGTH];
		int alert_result;

		extern void mac_hide_cursor(void);
		extern void mac_show_cursor(void);

		UUrString_Copy(buffer, msg, MAX_STRING_LENGTH);

		// bring up the dialog
		UUrString_Cat(buffer, " (Retry will enter the debugger)", MAX_STRING_LENGTH);

		mac_show_cursor();

		c2pstrcpy((StringPtr)buffer, buffer); // this call can do in-place conversion, so this is OK
		ParamText((StringPtr)buffer, NULL, NULL, NULL);
		alert_result= Alert(MAC_ERROR_ALERT_RES_ID, NULL);

		mac_hide_cursor();

		switch (alert_result)
		{
			case _button_abort:
				exit(3);
				break;
			case _button_retry:
				return_value= UUcTrue; // enter debugger
				break;
			case _button_ignore:
				return_value= UUcFalse; // don't enter debugger
				break;
		}
	}
#endif

	return return_value;
}
#endif

#if (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)

static void iDebugCStrG(char *str)
{
#if TOOL_VERSION
	unsigned char	buffer[MAX_STRING_LENGTH];
	UUtUns8			length = strlen(str) > (MAX_STRING_LENGTH-2) ? (MAX_STRING_LENGTH-2) : strlen(str);

	UUrString_Copy((char *)buffer + 1, str, (MAX_STRING_LENGTH-1));
	buffer[length + 1] = ';';
	buffer[length + 2] = 'g';
	buffer[0] = length + 2;
	DebugStr(buffer);
#endif
}

#endif

void UUrError_Report_Internal(
	const char*		file,
	unsigned long	line,
	UUtError		error,
	const char*		message)
{
	char buffer[MAX_STRING_LENGTH];

	sprintf(buffer, "Error %x reported from File: %s, Line: %d (message follows)",error, file, line);

	iAppendDebugFileMessage(buffer);

	sprintf(buffer, "%s", message);

#if TOOL_VERSION
	#if (UUmPlatform == UUmPlatform_Win32)
	{
		UUtBool enter_debugger;

		enter_debugger= iDebuggerMessageBox(buffer);
		if (enter_debugger)
		{
			__debugbreak();
		}
	}
	#elif (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
	{
		UUtBool enter_debugger;

		enter_debugger= iDebuggerMessageBox(buffer);
		if (enter_debugger)
		{
			DebugStr("\p");
		}
	}
	#endif
#endif

	iAppendDebugFileMessage("");

	return;
}

/* Port-exposed (#68): these were UUtUns32, truncating every char* passed
   through a %s format at the prototyped call — the error path then
   dereferenced the truncated value. uintptr_t carries both ints and pointers.
   Integer callers stay correct: on AArch64 the first variadic-consumed slots
   are 8-byte registers, so a %d in the format reads the low 32 bits of the
   widened value — identical to the old behaviour. %d keeps working, %s
   starts working. */
void UUrError_ReportP_Internal(
	const char*		file,
	unsigned long	line,
	UUtError		error,
	const char*		message,
	uintptr_t		value1,
	uintptr_t		value2,
	uintptr_t		value3)
{
	char buffer[MAX_STRING_LENGTH];

	sprintf(buffer, message, value1, value2, value3);
	UUrError_Report_Internal(file, line, error, buffer);
}

UUtError UUrError_Initialize(
	void)
{
	return UUcError_None;
}

void UUrError_Terminate(
	void)
{

}

#if UUmPlatform == UUmPlatform_Win32

#define PRINT_CASE(x) case (x): name= (#x); break;

char *UUrError_DirectDraw2Str(
	HRESULT	ddResult)
{
	char *name;

	switch(ddResult)
	{
		PRINT_CASE(DD_OK);
		PRINT_CASE(DDERR_ALREADYINITIALIZED);
		PRINT_CASE(DDERR_CANNOTATTACHSURFACE);
		PRINT_CASE(DDERR_CANNOTDETACHSURFACE);
		PRINT_CASE(DDERR_CURRENTLYNOTAVAIL);
		PRINT_CASE(DDERR_EXCEPTION);
		PRINT_CASE(DDERR_GENERIC);
		PRINT_CASE(DDERR_HEIGHTALIGN);
		PRINT_CASE(DDERR_INCOMPATIBLEPRIMARY);
		PRINT_CASE(DDERR_INVALIDCAPS);
		PRINT_CASE(DDERR_INVALIDCLIPLIST);
		PRINT_CASE(DDERR_INVALIDMODE);
		PRINT_CASE(DDERR_INVALIDOBJECT);
		PRINT_CASE(DDERR_INVALIDPARAMS);
		PRINT_CASE(DDERR_INVALIDPIXELFORMAT);
		PRINT_CASE(DDERR_INVALIDRECT);
		PRINT_CASE(DDERR_LOCKEDSURFACES);
		PRINT_CASE(DDERR_NO3D);
		PRINT_CASE(DDERR_NOALPHAHW);
		PRINT_CASE(DDERR_NOCLIPLIST);
		PRINT_CASE(DDERR_NOCOLORCONVHW);
		PRINT_CASE(DDERR_NOCOOPERATIVELEVELSET);
		PRINT_CASE(DDERR_NOCOLORKEY);
		PRINT_CASE(DDERR_NOCOLORKEYHW);
		PRINT_CASE(DDERR_NODIRECTDRAWSUPPORT);
		PRINT_CASE(DDERR_NOEXCLUSIVEMODE);
		PRINT_CASE(DDERR_NOFLIPHW);
		PRINT_CASE(DDERR_NOGDI);
		PRINT_CASE(DDERR_NOMIRRORHW);
		PRINT_CASE(DDERR_NOTFOUND);
		PRINT_CASE(DDERR_NOOVERLAYHW);
		PRINT_CASE(DDERR_NORASTEROPHW);
		PRINT_CASE(DDERR_NOROTATIONHW);
		PRINT_CASE(DDERR_NOSTRETCHHW);
		PRINT_CASE(DDERR_NOT4BITCOLOR);
		PRINT_CASE(DDERR_NOT4BITCOLORINDEX);
		PRINT_CASE(DDERR_NOT8BITCOLOR);
		PRINT_CASE(DDERR_NOTEXTUREHW);
		PRINT_CASE(DDERR_NOVSYNCHW);
		PRINT_CASE(DDERR_NOZBUFFERHW);
		PRINT_CASE(DDERR_NOZOVERLAYHW);
		PRINT_CASE(DDERR_OUTOFCAPS);
		PRINT_CASE(DDERR_OUTOFMEMORY);
		PRINT_CASE(DDERR_OUTOFVIDEOMEMORY);
		PRINT_CASE(DDERR_OVERLAYCANTCLIP);
		PRINT_CASE(DDERR_OVERLAYCOLORKEYONLYONEACTIVE);
		PRINT_CASE(DDERR_PALETTEBUSY);
		PRINT_CASE(DDERR_COLORKEYNOTSET);
		PRINT_CASE(DDERR_SURFACEALREADYATTACHED);
		PRINT_CASE(DDERR_SURFACEALREADYDEPENDENT);
		PRINT_CASE(DDERR_SURFACEBUSY);
		PRINT_CASE(DDERR_SURFACEISOBSCURED);
		PRINT_CASE(DDERR_SURFACELOST);
		PRINT_CASE(DDERR_SURFACENOTATTACHED);
		PRINT_CASE(DDERR_TOOBIGHEIGHT);
		PRINT_CASE(DDERR_TOOBIGSIZE);
		PRINT_CASE(DDERR_TOOBIGWIDTH);
		PRINT_CASE(DDERR_UNSUPPORTED);
		PRINT_CASE(DDERR_UNSUPPORTEDFORMAT);
		PRINT_CASE(DDERR_UNSUPPORTEDMASK);
		PRINT_CASE(DDERR_VERTICALBLANKINPROGRESS);
		PRINT_CASE(DDERR_WASSTILLDRAWING);
		PRINT_CASE(DDERR_XALIGN);
		PRINT_CASE(DDERR_INVALIDDIRECTDRAWGUID);
		PRINT_CASE(DDERR_DIRECTDRAWALREADYCREATED);
		PRINT_CASE(DDERR_NODIRECTDRAWHW);
		PRINT_CASE(DDERR_PRIMARYSURFACEALREADYEXISTS);
		PRINT_CASE(DDERR_NOEMULATION);
		PRINT_CASE(DDERR_REGIONTOOSMALL);
		PRINT_CASE(DDERR_CLIPPERISUSINGHWND);
		PRINT_CASE(DDERR_NOCLIPPERATTACHED);
		PRINT_CASE(DDERR_NOHWND);
		PRINT_CASE(DDERR_HWNDSUBCLASSED);
		PRINT_CASE(DDERR_HWNDALREADYSET);
		PRINT_CASE(DDERR_NOPALETTEATTACHED);
		PRINT_CASE(DDERR_NOPALETTEHW);
		PRINT_CASE(DDERR_BLTFASTCANTCLIP);
		PRINT_CASE(DDERR_NOBLTHW);
		PRINT_CASE(DDERR_NODDROPSHW);
		PRINT_CASE(DDERR_OVERLAYNOTVISIBLE);
		PRINT_CASE(DDERR_NOOVERLAYDEST);
		PRINT_CASE(DDERR_INVALIDPOSITION);
		PRINT_CASE(DDERR_NOTAOVERLAYSURFACE);
		PRINT_CASE(DDERR_EXCLUSIVEMODEALREADYSET);
		PRINT_CASE(DDERR_NOTFLIPPABLE);
		PRINT_CASE(DDERR_CANTDUPLICATE);
		PRINT_CASE(DDERR_NOTLOCKED);
		PRINT_CASE(DDERR_CANTCREATEDC);
		PRINT_CASE(DDERR_NODC);
		PRINT_CASE(DDERR_WRONGMODE);
		PRINT_CASE(DDERR_IMPLICITLYCREATED);
		PRINT_CASE(DDERR_NOTPALETTIZED);
		PRINT_CASE(DDERR_UNSUPPORTEDMODE);
		default:
			name= "< unknown >";
			break;
	}

	return name;
}

#endif

int UUrEnterDebuggerInline(
	char	*msg)
{
	int enterDebugger = 0;


	#if defined(DEBUGGING) && DEBUGGING

		#if (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
			// enterDebugger= iDebuggerMessageBox(msg);
			enterDebugger = 1;
		#elif UUmPlatform == UUmPlatform_Win32
			// enterDebugger= iDebuggerMessageBox(msg);
			enterDebugger = 1;
		#else

			#error implement me

		#endif

	#else

		/* XXX - Someday print a message to the console or something */

	#endif

	return enterDebugger;
}

void UUcArglist_Call UUrEnterDebugger(const char *format, ...)
{
	char msg[257]; /* [length byte] + [255 string bytes] + [null] */
	va_list arglist;
	int return_value;

	va_start(arglist, format);
	return_value= vsprintf(msg, format, arglist);
	va_end(arglist);


	iAppendDebugFileMessage(msg);

	#if defined(DEBUGGING) && DEBUGGING

		#if (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
			{
				UUtBool enter_debugger;

				enter_debugger= iDebuggerMessageBox(msg);

				if (enter_debugger)
				{
					DebugStr("\p");
				}
			}
		#elif UUmPlatform == UUmPlatform_Win32
			{
				UUtBool enter_debugger;

				enter_debugger= iDebuggerMessageBox(msg);

				if (enter_debugger)
				{
					__asm
					{
						int 3
					}
				}
			}
		#else

			#error implement me

		#endif

	#else

		/* XXX - Someday print a message to the console or something */

	#endif
}


void UUcArglist_Call UUrDebuggerMessage(
	 const char *format,
	...)
{
	char buffer[2048];
	va_list arglist;
	int return_value;

	va_start(arglist, format);
	return_value= vsprintf(buffer, format, arglist);
	va_end(arglist);

	iAppendDebugFileMessage(buffer);
#if defined(DEBUGGING) && DEBUGGING
#if (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
	iDebugCStrG(buffer);
#elif UUmPlatform == UUmPlatform_Win32
	OutputDebugString(buffer);
#else
        #error implement me
#endif
#endif
}

void UUcArglist_Call UUrStartupMessage(
	 const char *format,
	...)
{
	static FILE *stream = NULL;
	char buffer[8192];
	va_list arglist;
	int return_value;

	va_start(arglist, format);
	return_value= vsnprintf(buffer, sizeof(buffer), format, arglist);
	va_end(arglist);

	if (NULL == stream) {
		// Issue #17 — append mode preserves prior sessions across launches.
		stream = iOpenLogFile("startup.txt", "a");
		iWriteSessionBanner(stream);
	}

	if (NULL != stream) {
		fprintf(stream, "%s\n", buffer);
		fflush(stream);
	}

	return;
}

// Issue #103 — the level sweep runs unattended, and the AUrMessageBox below is
// a blocking modal, so one warning would hang a whole run. A registered tap
// gets first look at the formatted text and can claim the warning; with no tap
// registered (the default, and every non-sweep build) the flow below is the
// stock one.
static UUtWarningTap UUgWarningTap = NULL;

void UUrError_SetWarningTap(UUtWarningTap inTap)
{
	UUgWarningTap = inTap;
}

void UUcArglist_Call UUrPrintWarning(
	const char 			*format,
	...)
{
	char buffer[MAX_STRING_LENGTH];
	va_list arglist;
	UUtBool consumed = UUcFalse;

	va_start(arglist, format);
	// vsnprintf, not vsprintf: MAX_STRING_LENGTH is 256 and nothing bounds the
	// formatted result, so a long %s argument would smash the stack. Truncating
	// changes nothing for any current caller and removes the overflow.
	vsnprintf(buffer, sizeof(buffer), format, arglist);
	va_end(arglist);

	fprintf(stderr, "%s"UUmNL, buffer);

	if (UUgWarningTap != NULL)
	{
		consumed = UUgWarningTap(buffer);
	}

	if (!consumed)
	{
		AUrMessageBox(AUcMBType_OK, "%s", buffer);
	}
}

