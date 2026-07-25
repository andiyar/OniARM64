#pragma once
// ======================================================================
// BFW_Console.h
// ======================================================================
#ifndef BFW_CONSOLE_H
#define BFW_CONSOLE_H

// ======================================================================
// includes
// ======================================================================
#include "BFW.h"
#include "BFW_LocalInput.h"
#include "BFW_Motoko.h"

// ======================================================================
// defines
// ======================================================================
#define COcMaxVarNameLength		128
#define COcMaxVarValueLength	64

#define COcMaxDescritionLength	256

#define	COcMaxCharsPerLine		512
#define	COcMaxPrefixLength		10
#define COcCharsPerLine			(COcMaxCharsPerLine - COcMaxPrefixLength)

#define COcMaxArgs				32

// ======================================================================
// typedefs
// ======================================================================
// ----------------------------------------------------------------------
typedef struct COtVariable	COtVariable;

// ----------------------------------------------------------------------
typedef enum COtCallbackAction
{
	COc_ValueChanged,
	COc_PrintRange,
	COc_GetValue

} COtCallbackAction;

// ----------------------------------------------------------------------
typedef void (*COtVarCallback)(
	COtVariable*		inVariable,
	COtCallbackAction	inCallbackAction,
	void*				inCallbackData,
	void*				inRefcon);

typedef void (*COtCommandCallback)(
	UUtUns32			inArgC,
	char**				inArgV,
	void*				inRefcon);

// ======================================================================
// prototypes
// ======================================================================

UUtError
COrInitialize(
	void);

void
COrTerminate(
	void);

void
COrClear(
	void);

typedef enum
{
	COcConfigFile_Ignore,
	COcConfigFile_Read
} COtReadConfigFile;

UUtError
COrConfigure(
	void);

UUtBool
COrRunConfigFile(
	char*	inFilename);

// ----------------------------------------------------------------------

typedef UUtBool
(*COtConsole_Hook)(
	const char		*inPrefix,
	UUtUns32		refCon,
	const char		*commandLine);

UUtError
COrConsole_AddHook(const char *prefix, UUtUns32 inRefCon, COtConsole_Hook inHook);

void
COrConsole_Activate(
	void);

void
COrConsole_Deactivate(
	void);

void
COrConsole_Display_Lines(
	void);

void
COrConsole_Display_Messages(
	void);

void
COrConsole_Display_Console(
	void);

void
COrConsole_TemporaryDisable(
	UUtBool inDisable);

void
COrMessage_TemporaryDisable(
	UUtBool inDisable);

typedef enum COtPriority {
	COcPriority_Console,
	COcPriority_Subtitle,
	COcPriority_Content
} COtPriority;

void COrConsole_SetPriority(COtPriority inPriority);

void COrConsole_Print(COtPriority inPriority, IMtShade inTextColor, IMtShade inTextShadow, const char *inString);

void COrConsole_PrintIdentified(COtPriority inPriority, IMtShade inTextColor, IMtShade inTextShadow,
								const char *inString, const char *inIdentifier, UUtUns32 inFadeTimer);

void COrMessage_Print(const char *inString, const char *inIdentifier, UUtUns32 inFadeTimer);

void COrInGameSubtitle_Print(const char *inString, const char *inIdentifier, UUtUns32 inFadeTimer);

UUtBool COrConsole_Visible(const char *inIdentifier);
UUtBool COrConsole_Remove(const char *inIdentifier);
UUtBool COrMessage_Visible(const char *inIdentifier);
UUtBool COrMessage_Remove(const char *inIdentifier);
UUtBool COrInGameSubtitle_Visible(const char *inIdentifier);
UUtBool COrInGameSubtitle_Remove(const char *inIdentifier);

void COrSubtitle_Clear(void);

UUtBool
COrCommand_Execute(char	*inCommandLine);

extern IMtShade COgDefaultTextShade;
extern IMtShade COgDefaultTextShadowShade;

// ONI_SWEEP_CONSOLE (issue #103) is defined only for the OniSweep target. The
// shipping build sets SHIPPING_VERSION=1, which makes THE_DAY_IS_MINE 0 and
// compiles COrConsole_Printf away to the inline no-op below at all ~1170 call
// sites — including SLrScript_ReportError, so BSL script errors would vanish
// during a sweep. The sweep binary needs the real thing.
#if THE_DAY_IS_MINE || defined(ONI_SWEEP_CONSOLE)
void UUcArglist_Call COrConsole_Printf(const char *format, ...);
#else
static UUcInline void UUcArglist_Call COrConsole_Printf(const char *format, ...)
{
	return;
}
#endif

void UUcArglist_Call COrConsole_Printf_Color(COtPriority inPriority, IMtShade inTextColor, IMtShade inTextShadow, const char *format, ...);

// Console tap, the sweep harness's second output tap (issue #103). It hangs off
// COrConsole_Print rather than the printf functions because that is the single
// sink both variants feed; tapping COrConsole_Printf alone would miss
// COrConsole_Printf_Color, which is what the AI error path uses.
//
// Observe-only, and the void return says so: unlike the warning tap there is
// nothing harmful downstream to suppress, so the message always continues to the
// console ring buffer and consoleLog.txt.
//
// COmConsole_SweepTap is a macro, and COrConsole_RunTap lives at the bottom of
// BFW_Console.c, for one reason: BFW_Console.c bakes __LINE__ into its
// UUmError_Return* call sites, so inserting even one physical line above them
// changes the shipping binary. The macro sits on an existing line and the
// definitions sit past the last __LINE__ use, which keeps that binary
// bit-for-bit what it was. Header line numbers are free — nothing here uses
// __LINE__.
#if THE_DAY_IS_MINE || defined(ONI_SWEEP_CONSOLE)
	typedef void (*COtConsoleTap)(const char *inString);

	void COrConsole_SetTap(COtConsoleTap inTap);
	void COrConsole_RunTap(const char *inString);

	#define COmConsole_SweepTap(s)	COrConsole_RunTap(s)
#else
	#define COmConsole_SweepTap(s)
#endif


#if defined(DEBUGGING) && DEBUGGING
#define COmAssert(x) if (!(x)) do { COrConsole_Printf("%s", #x); } while(0)
#else
#define COmAssert(x)
#endif

UUtError
COrConsole_Update(
	UUtUns32				inTicksPassed);


#define COcMaxStatusLine 256

typedef struct COtStatusLine COtStatusLine;
struct COtStatusLine
{
	char text[COcMaxStatusLine];
	UUtBool *inUse;

	// prev & next will be NULL if we are not in the display list
	COtStatusLine *prev;
	COtStatusLine *next;
};

void COrConsole_StatusLines_Begin(COtStatusLine *inStatusLines, UUtBool *inBoolPtr, UUtUns32 inCount);
void COrConsole_StatusLine_Add(COtStatusLine *inStatusLine);
void COrConsole_StatusLine_Remove(COtStatusLine *inStatusLine);
void COrConsole_StatusLine_Display(void);
UUtError COrConsole_StatusLine_LevelBegin(void);
void COrConsole_StatusLine_LevelEnd(void);


// ======================================================================
#endif /* BFW_CONSOLE_H */
