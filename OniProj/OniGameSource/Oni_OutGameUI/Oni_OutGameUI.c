// ======================================================================
// Oni_OutGameUI.c
// ======================================================================

// ======================================================================
// includes
// ======================================================================
#include "BFW.h"
#include "BFW_WindowManager.h"
#include "BFW_SoundSystem2.h"
#include "BFW_Timer.h"
#include "Motoko_Manager.h"

#include "WM_CheckBox.h"
#include "WM_Text.h"
#include "WM_PartSpecification.h"
#include "BFW_TextSystem.h"
#include <string.h>
#include "WM_PopupMenu.h"

#include "Oni_GameState.h"
#include "Oni_OutGameUI.h"
#include "Oni_Motoko.h"
#include "Oni_Sound2.h"
#include "Oni_Windows.h"
#include "Oni_Persistance.h"
#include "Oni_Level.h"
#include "Oni.h"
#include "Oni_RendererPref.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <spawn.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
extern char **environ;
#endif


// ======================================================================
// defines
// ======================================================================
#define ONcMainMusic_PlayLength		(20 * 60)	/* 20 seconds */
#define OScMainMusic_StoppingLength (2 * 60)	/* 2 seconds */

#define OScMainMusic_FadeInTime		(0.5f)		/* in seconds */
#define OScMainMusic_FadeOutTime	(0.5f)		/* in seconds */

// ======================================================================
// enums
// ======================================================================
enum
{
	ONcOGU_MainMenuID				= 150,
	ONcOGU_LoadGameID				= 151,
	ONcOGU_OptionsID_PC				= 152,
	ONcOGU_QuitYesNoID				= 153,
	ONcOGU_NewGameID				= 154,
//	ONcOGU_LevelLoadID				= 155,	in Oni_Window.h
	ONcOGU_ChangeRestart			= 156,
	ONcOGU_OptionsID_Mac			= 157
};


enum
{
	ONcOptions_Sldr_Quality			= 100,
	ONcOptions_PM_Resolution		= 101,
	ONcOptions_Sldr_OverallVol		= 102,
//	ONcOptions_CB_DialogOn			= 103,
//	ONcOptions_Sldr_DialogVol		= 104,
//	ONcOptions_CB_MusicOn			= 105,
//	ONcOptions_Sldr_MusicVol		= 106,
	ONcOptions_CB_SubtitlesOn		= 107,
	ONcOptions_PM_Difficulty		= 108,
	ONcOptions_CB_InvertMouseOn		= 109,
	ONcOptions_Sldr_Gamma			= 110,
	ONcOptions_CB_MetalRenderer		= 111,		/* #89: created at runtime, not in the shipping template */
	ONcOptions_CB_OpenGLRenderer	= 112		/* #89: created at runtime, not in the shipping template */
};

enum
{
	ONcMainMenu_Btn_NewGame			= 100,
	ONcMainMenu_Btn_LoadGame		= 101,
	ONcMainMenu_Btn_Options			= 102,
	ONcMainMenu_Btn_Quit			= 103,
	ONcMainMenu_Btn_Resume			= 104
};

enum
{
	ONcLoadGame_LB_Levels			= 100,
	ONcLoadGame_Btn_Load			= WMcDialogItem_OK,
	ONcLoadGame_Btn_Cancel			= WMcDialogItem_Cancel
};

enum
{
	ONcNewGame_Btn_Yes				= WMcDialogItem_OK,
	ONcNewGame_Btn_No				= WMcDialogItem_Cancel
};

enum
{
	ONcQuitYesNo_Btn_Yes			= WMcDialogItem_OK,
	ONcQuitYesNo_Btn_No				= WMcDialogItem_Cancel
};

enum
{
	ONcChangeRestart_Btn_OK			= WMcDialogItem_OK
};

#ifdef __APPLE__
// ======================================================================
// #89: runtime tweaks to template dialogs (Options renderer row, restart and
// relaunch dialogs)
// ======================================================================
enum
{
	/* WMrDialog_ModalEnd message from the Options dialog meaning "a relaunch
	 * is armed and the game is quitting": the main menu closes itself too */
	ONcOptions_Result_Relaunch		= 0x524C4E43	/* 'RLNC' */
};

// ----------------------------------------------------------------------
/* The template dialogs' text labels ("Sound", "Overall Volume:", the restart
 * and quit prompts) carry no usable item ID, so they are found by walking the
 * dialog's children and matching the title. inExact requires the whole title
 * to match, otherwise inTitle is a prefix. Only text items count. */
static WMtWindow*
ONiOGU_FindTextByTitle(
	WMtDialog				*inDialog,
	const char				*inTitle,
	UUtBool					inExact)
{
	WMtWindow				*child;
	size_t					length;

	length = strlen(inTitle);
	for (child = WMrWindow_GetFirstChild(inDialog); child != NULL; child = WMrWindow_GetNextSibling(child))
	{
		WMtWindowClass		*window_class;
		const char			*title;

		window_class = WMrWindow_GetClass(child);
		if ((window_class == NULL) || (window_class->type != WMcWindowType_Text)) { continue; }

		title = WMrWindow_GetTitlePtr(child);
		if (title == NULL) { continue; }

		if (inExact ? (strcmp(title, inTitle) == 0) : (strncmp(title, inTitle, length) == 0))
		{
			return child;
		}
	}

	return NULL;
}

// ----------------------------------------------------------------------
/* Width of inString in the given font, measured with a private text context
 * so the draw context's shared font state is left alone. Returns -1 if the
 * font can't be measured. */
static UUtInt16
ONiOGU_MeasureTitle(
	const TStFontInfo		*inFontInfo,
	const char				*inString)
{
	TStTextContext			*text_context;
	UUtRect					rect;
	char					buffer[WMcMaxTitleLength + 1];
	UUtError				error;

	if ((inFontInfo == NULL) || (inFontInfo->font_family == NULL)) { return -1; }

	text_context = NULL;
	error =
		TSrContext_New(
			inFontInfo->font_family,
			inFontInfo->font_size,
			inFontInfo->font_style,
			TSc_HLeft | TSc_VCenter,
			UUcFalse,
			&text_context);
	if ((error != UUcError_None) || (text_context == NULL))
	{
		/* TSrContext_New can allocate and then fail in a later setter */
		if (text_context != NULL) { TSrContext_Delete(text_context); }
		return -1;
	}

	UUrString_Copy(buffer, inString, WMcMaxTitleLength);
	rect.left = rect.top = rect.right = rect.bottom = 0;
	error = TSrContext_GetStringRect(text_context, buffer, &rect);
	TSrContext_Delete(text_context);
	if (error != UUcError_None) { return -1; }

	return (UUtInt16)(rect.right - rect.left);
}

// ----------------------------------------------------------------------
/* Retitle a template item and widen it if the new title would not fit.
 * Centred items (buttons always; text items with HCenter) grow both ways,
 * right-aligned text grows leftward, others grow rightward. inBounds, if
 * given, keeps the widened item inside it (shifting, then capping the width).
 * inFallbackOldLength is the old title's length, used to scale the width if
 * the font can't be measured. Returns the final width. */
static UUtInt16
ONiOGU_RetitleAndFit(
	WMtWindow				*inItem,
	const char				*inTitle,
	UUtInt16				inFallbackOldLength,
	const UUtRect			*inBounds)
{
	WMtWindowClass			*window_class;
	UUtRect					rect;
	UUtRect					moved;
	UUtInt16				width;
	UUtInt16				height;
	UUtInt16				needed;
	UUtInt16				new_width;
	UUtInt16				new_left;
	UUtUns32				style;
	TStFontInfo				font_info;
	UUtBool					centred;
	UUtBool					right_aligned;

	WMrWindow_GetRect(inItem, &rect);
	WMrWindow_GetSize(inItem, &width, &height);
	WMrWindow_GetFontInfo(inItem, &font_info);
	WMrWindow_SetTitle(inItem, inTitle, WMcMaxTitleLength);

	needed = ONiOGU_MeasureTitle(&font_info, inTitle);
	if (needed < 0)
	{
		needed = (UUtInt16)((width * (UUtInt16)strlen(inTitle)) / ((inFallbackOldLength > 0) ? inFallbackOldLength : 1));
	}
	needed = (UUtInt16)(needed + 8);	/* slack against glyph overhang and button edges */
	if (needed <= width) { return width; }

	window_class = WMrWindow_GetClass(inItem);
	style = WMrWindow_GetStyle(inItem);
	centred = UUcFalse;
	right_aligned = UUcFalse;
	if ((window_class != NULL) && (window_class->type == WMcWindowType_Text))
	{
		centred = (UUtBool)((style & WMcTextStyle_HCenter) != 0);
		right_aligned = (UUtBool)((style & WMcTextStyle_HRight) != 0);
	}
	else
	{
		centred = UUcTrue;
	}

	new_width = needed;
	new_left = rect.left;
	if (centred) { new_left = (UUtInt16)(rect.left - (new_width - width) / 2); }
	else if (right_aligned) { new_left = (UUtInt16)(rect.right - new_width); }

	if (inBounds != NULL)
	{
		if (new_width > (inBounds->right - inBounds->left) - 8)
		{
			new_width = (UUtInt16)((inBounds->right - inBounds->left) - 8);
			if (new_left < inBounds->left + 4) { new_left = (UUtInt16)(inBounds->left + 4); }
		}
		if (new_left + new_width > inBounds->right - 4) { new_left = (UUtInt16)(inBounds->right - 4 - new_width); }
		if (new_left < inBounds->left + 4) { new_left = (UUtInt16)(inBounds->left + 4); }
	}
	if (new_width <= width) { return width; }

	WMrWindow_SetSize(inItem, new_width, height);
	if (new_left != rect.left)
	{
		/* SetLocation is parent-relative and the WM has no getter for it:
		 * park the item at the parent origin to learn where that is */
		WMrWindow_SetLocation(inItem, 0, 0);
		WMrWindow_GetRect(inItem, &moved);
		WMrWindow_SetLocation(
			inItem,
			(UUtInt16)(new_left - moved.left),
			(UUtInt16)(rect.top - moved.top));
	}

	return new_width;
}

// ----------------------------------------------------------------------
/* Z-order (#89 root cause): WMrWindow_New appends the new child at the END of
 * the dialog's child list, and WMiWindow_Draw paints that list from last to
 * first. The Options template's last item is pict_options_background, so an
 * appended control is painted before the background art and ends up
 * underneath it. Moving it to the head of the list puts it on top, like every
 * template control. */
static void
ONiOGU_Options_BringToFront(
	WMtWindow				*inWindow)
{
	WMrWindow_SetPosition(
		inWindow,
		NULL,
		0, 0, 0, 0,
		WMcPosChangeFlag_NoMove | WMcPosChangeFlag_NoSize);
}

// ----------------------------------------------------------------------
/* Retitle the Sound box heading to "Sound and Renderer", widening the text
 * item if the longer title would not fit. */
static void
ONiOGU_Options_RetitleSoundBox(
	WMtDialog				*inDialog)
{
	static const char		old_title[] = "Sound";
	static const char		new_title[] = "Sound and Renderer";
	WMtWindow				*heading;
	UUtInt16				width;
	UUtInt16				height;
	UUtInt16				new_width;
	UUtRect					final_rect;

	heading = ONiOGU_FindTextByTitle(inDialog, old_title, UUcTrue);
	if (heading == NULL)
	{
		UUrStartupMessage("options renderer toggle: heading MISSING - no \"%s\" text item, title unchanged", old_title);
		return;
	}

	WMrWindow_GetSize(heading, &width, &height);
	new_width = ONiOGU_RetitleAndFit(heading, new_title, (UUtInt16)(sizeof(old_title) - 1), NULL);
	WMrWindow_GetRect(heading, &final_rect);
	UUrStartupMessage(
		"options renderer toggle: heading \"%s\" -> \"%s\" (text item retitled; width %d -> %d; rect %d,%d-%d,%d)",
		old_title, new_title,
		(int)width, (int)new_width,
		(int)final_rect.left, (int)final_rect.top, (int)final_rect.right, (int)final_rect.bottom);
}

// ----------------------------------------------------------------------
/* The renderer row: two mutually exclusive checkboxes, OpenGL and Metal, on
 * the row under Overall Volume inside the Sound box, starting at the label
 * column and sized to their titles. The running renderer's box is titled
 * "<name> (active)"; the check marks the preference for the next launch.
 * inAnchor is the Invert Mouse checkbox (or the gamma slider fallback): its
 * height, style word and font are copied so the new row draws like the
 * template's own checkboxes. */
static void
ONiOGU_Options_AddRendererRow(
	WMtDialog				*inDialog,
	WMtWindow				*inAnchor,
	UUtBool					inAnchorIsCheckbox)
{
	extern UUtBool			metal_is_available(void);

	WMtWindow				*slider;
	WMtWindow				*label;
	WMtWindow				*font_donor;
	WMtWindow				*gl_box;
	WMtWindow				*metal_box;
	UUtRect					slider_rect;
	UUtRect					label_rect;
	UUtRect					created_rect;
	UUtRect					dialog_rect;
	UUtInt16				anchor_width;
	UUtInt16				height;
	UUtInt16				glyph_width;
	UUtInt16				glyph_height;
	UUtInt16				gl_text;
	UUtInt16				metal_text;
	UUtInt16				gl_width;
	UUtInt16				metal_width;
	UUtInt16				gap;
	UUtInt16				left;
	UUtInt16				top;
	UUtInt16				origin_x;
	UUtInt16				origin_y;
	UUtUns32				style;
	TStFontInfo				font_info;
	PStPartSpecUI			*partspec_ui;
	ONtRendererPref			pref;
	UUtBool					metal_available;
	UUtBool					want_metal;
	UUtBool					running_metal;
	const char				*label_source;
	const char				*gl_title;
	const char				*metal_title;

	WMrWindow_GetSize(inAnchor, &anchor_width, &height);
	UUrStartupMessage("options renderer toggle: anchor size %dx%d", (int)anchor_width, (int)height);

	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_OverallVol);
	if (slider == NULL)
	{
		UUrStartupMessage("options renderer toggle: MISSING - no Overall Volume slider, no renderer row");
		return;
	}
	WMrWindow_GetRect(slider, &slider_rect);

	/* the label column: the left edge of "Overall Volume:" */
	label = ONiOGU_FindTextByTitle(inDialog, "Overall Volume", UUcFalse);
	label_source = "overall-volume label";
	if (label == NULL)
	{
		label = ONiOGU_FindTextByTitle(inDialog, "Invert Mouse", UUcFalse);
		label_source = "invert-mouse label (fallback)";
	}
	if (label != NULL)
	{
		WMrWindow_GetRect(label, &label_rect);
		left = label_rect.left;
	}
	else
	{
		left = (UUtInt16)(slider_rect.left - 100);
		label_source = "slider left - 100 (fallback)";
	}

	/* copy the Invert Mouse checkbox's style word so the new row draws like
	 * the template's; a slider's style means nothing to a checkbox */
	style = inAnchorIsCheckbox ? WMrWindow_GetStyle(inAnchor) : WMcCheckBoxStyle_TextCheckBox;

	// borrow the font from a checkbox that already draws a title:
	// the anchor itself when it is Invert Mouse, else Subtitles
	font_donor = inAnchorIsCheckbox ? inAnchor : WMrDialog_GetItemByID(inDialog, ONcOptions_CB_SubtitlesOn);
	if (font_donor == NULL) { font_donor = inAnchor; }
	WMrWindow_GetFontInfo(font_donor, &font_info);

	/* mark the renderer running now; the checks show the next launch's */
	running_metal = ONgCommandLine.useMetal;
	gl_title = running_metal ? "OpenGL" : "OpenGL (active)";
	metal_title = running_metal ? "Metal (active)" : "Metal";

	gl_box =
		WMrWindow_New(
			WMcWindowType_CheckBox,
			(char *)gl_title,
			WMcWindowFlag_Visible | WMcWindowFlag_Child,
			style,
			ONcOptions_CB_OpenGLRenderer,
			0, 0,
			height, height,
			inDialog,
			0);
	metal_box =
		WMrWindow_New(
			WMcWindowType_CheckBox,
			(char *)metal_title,
			WMcWindowFlag_Visible | WMcWindowFlag_Child,
			style,
			ONcOptions_CB_MetalRenderer,
			0, 0,
			height, height,
			inDialog,
			0);
	if ((gl_box == NULL) || (metal_box == NULL))
	{
		UUrStartupMessage("options renderer toggle: WMrWindow_New returned NULL - no checkbox (opengl=%p metal=%p)",
			(void *)gl_box, (void *)metal_box);
		if (gl_box != NULL) { WMrWindow_Delete(gl_box); }
		if (metal_box != NULL) { WMrWindow_Delete(metal_box); }
		return;
	}
	WMrWindow_SetFontInfo(gl_box, &font_info);
	WMrWindow_SetFontInfo(metal_box, &font_info);

	/* WMrWindow_New takes parent-relative coordinates and the WM has no getter
	 * for them; both boxes were created at the parent origin, so the screen
	 * rect of a fresh one gives the origin to subtract. */
	WMrWindow_GetRect(gl_box, &created_rect);
	origin_x = created_rect.left;
	origin_y = created_rect.top;

	/* the checkbox glyph, drawn at the left of the control, then a 2 pixel
	 * buffer (WMcCheckBox_Buffer) before the title */
	glyph_width = 0;
	glyph_height = 0;
	partspec_ui = PSrPartSpecUI_GetActive();
	if ((partspec_ui != NULL) && (partspec_ui->checkbox_on != NULL))
	{
		PSrPartSpec_GetSize(partspec_ui->checkbox_on, PScPart_LeftTop, &glyph_width, &glyph_height);
	}
	gl_text = ONiOGU_MeasureTitle(&font_info, gl_title);
	metal_text = ONiOGU_MeasureTitle(&font_info, metal_title);
	if ((glyph_width > 0) && (gl_text > 0) && (metal_text > 0))
	{
		gl_width = (UUtInt16)(glyph_width + 2 + gl_text + 4);
		metal_width = (UUtInt16)(glyph_width + 2 + metal_text + 4);
		gap = glyph_width;
	}
	else
	{
		/* the " (active)" box gets the extra room */
		gl_width = (UUtInt16)(height * (running_metal ? 6 : 10));
		metal_width = (UUtInt16)(height * (running_metal ? 9 : 5));
		gap = height;
		UUrStartupMessage("options renderer toggle: measure fallback (glyph %d, text %d/%d) - using height multiples",
			(int)glyph_width, (int)gl_text, (int)metal_text);
	}
	UUrStartupMessage("options renderer toggle: measured glyph %dx%d, text \"%s\"=%d \"%s\"=%d -> widths %d/%d gap %d; label column %d from %s",
		(int)glyph_width, (int)glyph_height, gl_title, (int)gl_text, metal_title, (int)metal_text,
		(int)gl_width, (int)metal_width, (int)gap, (int)left, label_source);

	/* keep the row inside the Sound box: its right edge must not pass the
	 * Overall Volume slider's right edge. Shrink the gap first; titles are
	 * never truncated. */
	if (left + gl_width + gap + metal_width > slider_rect.right)
	{
		UUtInt16		over;

		over = (UUtInt16)(left + gl_width + gap + metal_width - slider_rect.right);
		gap = (over >= gap) ? 0 : (UUtInt16)(gap - over);
		UUrStartupMessage("options renderer toggle: row clamped (over by %d, gap now %d, row right %d vs slider right %d)",
			(int)over, (int)gap, (int)(left + gl_width + gap + metal_width), (int)slider_rect.right);
		if (left + gl_width + gap + metal_width > slider_rect.right)
		{
			UUrStartupMessage("options renderer toggle: row still over at gap 0 by %d (left %d + widths %d/%d vs slider right %d)",
				(int)(left + gl_width + metal_width - slider_rect.right),
				(int)left, (int)gl_width, (int)metal_width, (int)slider_rect.right);
		}
	}

	/* one row below the slider with a half-row gap; clamp inside the dialog
	 * if that would run off its bottom, never overlapping the slider */
	top = (UUtInt16)(slider_rect.bottom + height / 2);
	WMrWindow_GetRect(inDialog, &dialog_rect);
	if (top + height > dialog_rect.bottom)
	{
		top = (UUtInt16)(dialog_rect.bottom - height - height / 2);
		if (top < slider_rect.bottom) { top = slider_rect.bottom; }
		UUrStartupMessage("options renderer toggle: clamped (dialog bottom %d, top now %d)", (int)dialog_rect.bottom, (int)top);
	}

	WMrWindow_SetSize(gl_box, gl_width, height);
	WMrWindow_SetSize(metal_box, metal_width, height);
	WMrWindow_SetLocation(gl_box, (UUtInt16)(left - origin_x), (UUtInt16)(top - origin_y));
	WMrWindow_SetLocation(metal_box, (UUtInt16)(left + gl_width + gap - origin_x), (UUtInt16)(top - origin_y));

	ONiOGU_Options_BringToFront(gl_box);
	ONiOGU_Options_BringToFront(metal_box);
	UUrStartupMessage("options renderer toggle: moved to front of z-order (above pict_options_background)");

	{
		UUtRect			gl_rect;
		UUtRect			metal_rect;
		UUtRect			anchor_rect;

		WMrWindow_GetRect(gl_box, &gl_rect);
		WMrWindow_GetRect(metal_box, &metal_rect);
		WMrWindow_GetRect(inAnchor, &anchor_rect);
		UUrStartupMessage(
			"options renderer toggle: anchor rect %d,%d-%d,%d; slider rect %d,%d-%d,%d; created at %d,%d-%d,%d; placed OpenGL at %d,%d-%d,%d visible=%d; Metal at %d,%d-%d,%d visible=%d",
			(int)anchor_rect.left, (int)anchor_rect.top, (int)anchor_rect.right, (int)anchor_rect.bottom,
			(int)slider_rect.left, (int)slider_rect.top, (int)slider_rect.right, (int)slider_rect.bottom,
			(int)created_rect.left, (int)created_rect.top, (int)created_rect.right, (int)created_rect.bottom,
			(int)gl_rect.left, (int)gl_rect.top, (int)gl_rect.right, (int)gl_rect.bottom,
			(int)WMrWindow_GetVisible(gl_box),
			(int)metal_rect.left, (int)metal_rect.top, (int)metal_rect.right, (int)metal_rect.bottom,
			(int)WMrWindow_GetVisible(metal_box));
	}

	ONiOGU_Options_RetitleSoundBox(inDialog);

	// initial state: exactly one checked
	pref = ONrRendererPref_Read();
	want_metal =
		(pref != ONcRendererPref_None) ?
			(UUtBool)(pref == ONcRendererPref_Metal) :
			ONgCommandLine.useMetal;
	metal_available = metal_is_available();
	if (!metal_available)
	{
		want_metal = UUcFalse;
		WMrWindow_SetEnabled(metal_box, UUcFalse);
	}
	WMrCheckBox_SetCheck(gl_box, (UUtBool)!want_metal);
	WMrCheckBox_SetCheck(metal_box, want_metal);
	UUrStartupMessage("options renderer toggle: initial state pref=%d -> %s checked; running %s; metal available=%d",
		(int)pref, want_metal ? "Metal" : "OpenGL", running_metal ? "Metal" : "OpenGL", (int)metal_available);
}

// ----------------------------------------------------------------------
/* After a renderer switch, relaunch the game once this process has exited.
 * A detached shell waits for this PID to go, then opens the .app again with
 * `open -n` (so LaunchServices treats it as the app) or, for a bare binary,
 * execs it from the same cwd. No arguments are passed through. The caller
 * then quits through the normal path, so teardown is the ordinary one.
 * Returns UUcFalse (with a log line) if the relaunch could not be armed. */
static UUtBool
ONiOGU_RelaunchAfterQuit(
	void)
{
	char					exe[PATH_MAX];
	char					resolved[PATH_MAX];
	char					cwd[PATH_MAX];
	char					cmd[PATH_MAX * 3];
	uint32_t				size;
	const char				*macos_dir;
	char					*argv[4];
	pid_t					pid;
	int						rc;
	int						written;
	posix_spawnattr_t		attr;

	size = sizeof(exe);
	if (_NSGetExecutablePath(exe, &size) != 0)
	{
		UUrStartupMessage("relaunch: not armed - executable path unavailable");
		return UUcFalse;
	}
	if (realpath(exe, resolved) != NULL)
	{
		UUrString_Copy(exe, resolved, sizeof(exe));
	}
	/* every path goes inside single quotes; a path holding one is refused
	 * rather than escaped */
	if (strchr(exe, '\'') != NULL)
	{
		UUrStartupMessage("relaunch: not armed - executable path contains a quote (%s)", exe);
		return UUcFalse;
	}

	macos_dir = strstr(exe, ".app/Contents/MacOS/");
	if (macos_dir != NULL)
	{
		written = snprintf(cmd, sizeof(cmd),
			"while kill -0 %d 2>/dev/null; do sleep 0.2; done; exec /usr/bin/open -n '%.*s'",
			(int)getpid(), (int)(macos_dir + 4 - exe), exe);
	}
	else
	{
		if ((getcwd(cwd, sizeof(cwd)) == NULL) || (strchr(cwd, '\'') != NULL))
		{
			UUrStartupMessage("relaunch: not armed - cwd unavailable or contains a quote");
			return UUcFalse;
		}
		written = snprintf(cmd, sizeof(cmd),
			"while kill -0 %d 2>/dev/null; do sleep 0.2; done; cd '%s' && exec '%s'",
			(int)getpid(), cwd, exe);
	}
	if ((written < 0) || ((size_t)written >= sizeof(cmd)))
	{
		UUrStartupMessage("relaunch: not armed - command too long");
		return UUcFalse;
	}

	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = cmd;
	argv[3] = NULL;

	/* own session, so the waiter isn't tied to this process's group */
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	rc = posix_spawn(&pid, "/bin/sh", NULL, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
	if (rc != 0)
	{
		UUrStartupMessage("relaunch: not armed - posix_spawn failed (%d) (%s)", rc, cmd);
		return UUcFalse;
	}

	UUrStartupMessage("relaunch: armed (waiter pid %d: %s)", (int)pid, cmd);
	return UUcTrue;
}
#endif


// ======================================================================
// functions
// ======================================================================
// ----------------------------------------------------------------------
static UUtBool
ONiOGU_ChangeRestart_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
#ifdef __APPLE__
		case WMcMessage_InitDialog:
			{
				/* #89: the template (156) says "take affect"; fix it on screen */
				static const char	old_text[] = "You must restart Oni for the change to take affect.";
				static const char	new_text[] = "You must restart Oni for the change to take effect.";
				WMtWindow			*text;

				text = ONiOGU_FindTextByTitle(inDialog, old_text, UUcTrue);
				if (text != NULL)
				{
					ONiOGU_RetitleAndFit(text, new_text, (UUtInt16)(sizeof(old_text) - 1), NULL);
				}
				UUrStartupMessage("restart dialog: \"take affect\" text %s", (text != NULL) ? "retitled to \"take effect\"" : "not found, unchanged");
			}
		break;
#endif

		case WMcMessage_Command:
			if (UUmHighWord(inParam1) != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, UUmLowWord(inParam1));
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
static UUtUns32
ONiOutGameUI_ChangeRestart_Display(
	void)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// display the dialog
	WMrDialog_ModalBegin(
		ONcOGU_ChangeRestart,
		NULL,
		ONiOGU_ChangeRestart_Callback,
		0,
		&message);

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return message;
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static UUtBool
ONiOGU_QuitYesNo_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_Command:
			if (UUmHighWord(inParam1) != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, UUmLowWord(inParam1));
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
UUtUns32
ONrOutGameUI_QuitYesNo_Display(
	void)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// display the dialog
	WMrDialog_ModalBegin(
		ONcOGU_QuitYesNoID,
		NULL,
		ONiOGU_QuitYesNo_Callback,
		0,
		&message);

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return message;
}

#ifdef __APPLE__
// ----------------------------------------------------------------------
/* #89: "switch renderer and relaunch?" reuses the quit Yes/No template (153),
 * retitled at init: the prompt names the renderer, OK becomes Relaunch and
 * Cancel stays Cancel. The modal result is the button id. */
static UUtBool
ONiOGU_RelaunchYesNo_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_InitDialog:
		{
			static const char	old_prompt[] = "Are you sure you want to quit?";
			char				prompt[64];
			UUtBool				want_metal;
			WMtWindow			*text;
			WMtWindow			*child;
			WMtWindow			*button;
			UUtRect				dialog_rect;
			int					text_count;

			want_metal = (UUtBool)(WMrDialog_GetUserData(inDialog) != 0);
			snprintf(prompt, sizeof(prompt), "Switch to %s? Oni will relaunch.", want_metal ? "Metal" : "OpenGL");
			WMrWindow_GetRect(inDialog, &dialog_rect);

			/* diagnostics: what the template holds */
			text_count = 0;
			for (child = WMrWindow_GetFirstChild(inDialog); child != NULL; child = WMrWindow_GetNextSibling(child))
			{
				WMtWindowClass	*window_class = WMrWindow_GetClass(child);
				UUtRect			r;

				WMrWindow_GetRect(child, &r);
				if ((window_class != NULL) && (window_class->type == WMcWindowType_Text)) { text_count++; }
				UUrStartupMessage("relaunch dialog: child type %d id %d \"%s\" rect %d,%d-%d,%d",
					(window_class != NULL) ? (int)window_class->type : -1,
					(int)WMrWindow_GetID(child),
					WMrWindow_GetTitlePtr(child) ? WMrWindow_GetTitlePtr(child) : "",
					(int)r.left, (int)r.top, (int)r.right, (int)r.bottom);
			}

			text = ONiOGU_FindTextByTitle(inDialog, old_prompt, UUcTrue);
			if (text == NULL)
			{
				/* not the expected prompt: take the first text item */
				for (child = WMrWindow_GetFirstChild(inDialog); child != NULL; child = WMrWindow_GetNextSibling(child))
				{
					WMtWindowClass	*window_class = WMrWindow_GetClass(child);
					if ((window_class != NULL) && (window_class->type == WMcWindowType_Text)) { text = child; break; }
				}
			}
			if (text != NULL)
			{
				ONiOGU_RetitleAndFit(text, prompt, (UUtInt16)(sizeof(old_prompt) - 1), &dialog_rect);
			}

			button = WMrDialog_GetItemByID(inDialog, ONcQuitYesNo_Btn_Yes);
			if (button != NULL) { ONiOGU_RetitleAndFit(button, "Relaunch", 3, &dialog_rect); }
			button = WMrDialog_GetItemByID(inDialog, ONcQuitYesNo_Btn_No);
			if (button != NULL) { ONiOGU_RetitleAndFit(button, "Cancel", 2, &dialog_rect); }

			UUrStartupMessage("relaunch dialog: %d text item(s); prompt %s -> \"%s\"; OK -> Relaunch %s; Cancel %s",
				text_count,
				(text != NULL) ? "retitled" : "MISSING",
				prompt,
				(WMrDialog_GetItemByID(inDialog, ONcQuitYesNo_Btn_Yes) != NULL) ? "done" : "MISSING",
				(WMrDialog_GetItemByID(inDialog, ONcQuitYesNo_Btn_No) != NULL) ? "done" : "MISSING");
		}
		break;

		case WMcMessage_Command:
			if (UUmHighWord(inParam1) != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, UUmLowWord(inParam1));
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
static UUtUns32
ONiOGU_RelaunchYesNo_Display(
	UUtBool					inWantMetal)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */
	UUtError				error;

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	message = 0;
	error =
		WMrDialog_ModalBegin(
			ONcOGU_QuitYesNoID,
			NULL,
			ONiOGU_RelaunchYesNo_Callback,
			(uintptr_t)(inWantMetal ? 1 : 0),
			&message);
	if (error != UUcError_None) { message = ONcQuitYesNo_Btn_No; }

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return (UUtUns32)message;
}
#endif


// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static void
ONiResolution_Switch(
	WMtDialog				*inOptionsDialog,
	WMtWindow				*inPopupMenu,
	UUtUns16				inMode)
{
	M3tDrawEngineCaps		*current_draw_engine_caps;
	UUtUns16				activeDrawEngine;
	UUtUns16				activeDevice;
	UUtUns16				activeMode;
	M3tDisplayMode			new_resolution;
	UUtBool					restart, voodoo_fullscreen, s3_crappy_card_fullscreen;
	UUtBool					osx= UUcFalse;

	extern UUtBool gl_voodoo_card_full_screen(void); // gl_utility.c
	extern UUtBool gl_s3_crappy_card_full_screen(void); // gl_utility.c

	// get the index of the active draw engine
	M3rManager_GetActiveDrawEngine(&activeDrawEngine, &activeDevice, &activeMode);
	if (inMode == activeMode) { return; }

	// get a pointer to the current draw engine's caps
	current_draw_engine_caps = M3rDrawEngine_GetCaps(activeDrawEngine);
	if (current_draw_engine_caps == NULL) { return; }

        new_resolution = current_draw_engine_caps->displayDevices[activeDevice].displayModes[inMode];

	restart = UUcFalse;

	voodoo_fullscreen= gl_voodoo_card_full_screen();
	s3_crappy_card_fullscreen= gl_s3_crappy_card_full_screen();
#if defined(UUmPlatform) && (UUmPlatform == UUmPlatform_Mac) && !defined(UUmSDL)
	{
		// OSX doesn't handle res changes properly (if at all) as of OS X public beta
		// so we force a restart
		OSErr err;
		unsigned long value;

		err= Gestalt(gestaltSystemVersion, &value);
		if (err == noErr)
		{
			unsigned long major_version;

			// value will look like this: 0x00000904 (OS 9.0.4)
			major_version= (value & 0x0000FF00)>>8;
			if (major_version >= 10)
			{
				osx= UUcTrue;
			}
		}
	}
#elif defined(UUmPlatform) && (UUmPlatform == UUmPlatform_Mac) && defined(UUmSDL)
	// SDL handles runtime resolution switching fine via viewport scaling
	osx= UUcFalse;
#endif

	if (voodoo_fullscreen || s3_crappy_card_fullscreen || osx)
	{
                restart = UUcTrue;
	}

	if ((ONrLevel_GetCurrentLevel() == 0) &&
		(ONrMotoko_SetResolution(&new_resolution) == UUcTrue) &&
		(ONrMotoko_SetupDrawing(&ONgPlatformData) == UUcError_None))
	{
		// update the window manager
		WMrSetResolution(new_resolution.width, new_resolution.height);
	}
	else
	{
		restart = UUcTrue;
	}
	ONrPersist_SetResolution(&new_resolution);

	// 3Dfx cards don't handle resolution switching correctly in full-screen mode
	// and after doing so the out-of-game UI will not be visible. Diito for S3
	if (voodoo_fullscreen || s3_crappy_card_fullscreen || osx)
	{
		extern void OniExit(void); // Oni.c

		// alert won't be visible afterwards
		AUrMessageBox(AUcMBType_OK, "You must restart Oni for your changes to take effect; Oni will now exit.");
		OniExit();
		exit(0);
	}

	if (restart == UUcTrue)
	{
		// tell the user they have to restart the game
		ONiOutGameUI_ChangeRestart_Display();
	}
}

/*
static void
OWiResolution_Switch(
	WMtDialog				*inOptionsDialog,
	WMtWindow				*inPopupMenu,
	UUtUns16				inMode)
{
	M3tDrawEngineCaps		*current_draw_engine_caps;
	UUtUns16				activeDrawEngine;
	UUtUns16				activeDevice;
	UUtUns16				activeMode;

	// get the index of the active draw engine
	M3rManager_GetActiveDrawEngine(&activeDrawEngine, &activeDevice, &activeMode);
	if (inMode == activeMode) { return; }

	// get a pointer to the current draw engine's caps
	current_draw_engine_caps = M3rDrawEngine_GetCaps(activeDrawEngine);
	if (current_draw_engine_caps == NULL) { return; }

	{
		UUtInt16 dialog_width, dialog_height;

		if (WMrWindow_GetSize(inOptionsDialog, &dialog_width, &dialog_height))
		{
			M3tDisplayMode new_resolution;
			UUtInt16 main_menu_width, main_menu_height;
			UUtUns16 options_x, options_y;
			UUtUns16 main_menu_x, main_menu_y;

			new_resolution.bit_depth = current_draw_engine_caps->displayDevices[activeDevice].displayModes[inMode].bitDepth;
			new_resolution.width = current_draw_engine_caps->displayDevices[activeDevice].displayModes[inMode].width;
			new_resolution.height = current_draw_engine_caps->displayDevices[activeDevice].displayModes[inMode].height;

			// center options dialog in main window
			options_x= UUmMax(((new_resolution.width - dialog_width)/2), 0);
			options_y= UUmMax(((new_resolution.height - dialog_height)/2), 0);

			// center main menu in main window
			if ((main_game_menu != NULL) &&
				WMrWindow_GetSize(main_game_menu, &main_menu_width, &main_menu_height))
			{

				main_menu_x= UUmMax(((new_resolution.width - main_menu_width)/2), 0);
				main_menu_y= UUmMax(((new_resolution.height - main_menu_height)/2), 0);
			}

			if (ONrMotoko_SetResolution(&new_resolution) &&
				OWrWindow_Resize(new_resolution.width, new_resolution.height) &&
				WMrWindow_SetPosition(inOptionsDialog, NULL,
					options_x, options_y, dialog_width, dialog_height, WMcPosChangeFlag_NoZOrder) &&
				((main_game_menu == NULL) ? UUcTrue :
					WMrWindow_SetPosition(main_game_menu, NULL,
						main_menu_x, main_menu_y,
						new_resolution.width, new_resolution.height,
						WMcPosChangeFlag_NoZOrder)))
			{
				ONrPersist_SetResolution(&new_resolution);
			}
			else
			{
				// many older cards don't support res-switching on the fly
				ONrPersist_SetResolution(&new_resolution);
				AUrMessageBox(AUcMBType_OK, "You must restart Oni for the changes to take effect; Oni will now exit");
				exit(0);
			}
		}
	}

	return;
}
*/
// ----------------------------------------------------------------------
static void
ONiResolutions_AddToPopup(
	WMtWindow				*inPopupMenu)
{
	M3tDrawEngineCaps		*current_draw_engine_caps;
	UUtUns16				activeDrawEngine;
	UUtUns16				activeDevice;
	UUtUns16				activeMode;
	UUtUns16				i;
	UUtUns16				num_modes;

	// reset the popup
	WMrPopupMenu_Reset(inPopupMenu);

	// get the index of the active draw engine
	M3rManager_GetActiveDrawEngine(&activeDrawEngine, &activeDevice, &activeMode);

	// get a pointer to the current draw engine's caps
	current_draw_engine_caps = M3rDrawEngine_GetCaps(activeDrawEngine);
	if (current_draw_engine_caps == NULL) { return; }

	// add a list of all of the available
	num_modes = current_draw_engine_caps->displayDevices[activeDevice].numDisplayModes;
	for (i = 0; i < num_modes; i++)
	{
		M3tDisplayMode			*mode;
		char					title[128];

		mode = &current_draw_engine_caps->displayDevices[activeDevice].displayModes[i];

		sprintf(title, "%d x %d", mode->width, mode->height);
		WMrPopupMenu_AppendItem_Light(inPopupMenu, i, title);
	}

	// select the current item
	WMrPopupMenu_SetSelection(inPopupMenu, activeMode);
}

// ----------------------------------------------------------------------
static void
ONiOBU_Options_SetControls(
	WMtDialog				*inDialog)
{
	WMtWindow				*slider;
	WMtWindow				*popup;
	WMtWindow				*checkbox;

	// set the sliders
	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_Quality);
	WMrSlider_SetPosition(slider, (UUtInt32)ONrPersist_GetGraphicsQuality());

	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_Gamma);
	if (slider != NULL) {
		WMrSlider_SetPosition(slider, MUrFloat_Round_To_Int(100 * ONrPersist_GetGamma()));
	}

	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_OverallVol);
	WMrSlider_SetPosition(slider, (UUtInt32)(ONrPersist_GetOverallVolume() * 100.0f));

//	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_DialogVol);
//	WMrSlider_SetPosition(slider, (UUtInt32)(ONrPersist_GetDialogVolume() * 100.0f));

//	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_MusicVol);
//	WMrSlider_SetPosition(slider, (UUtInt32)(ONrPersist_GetMusicVolume() * 100.0f));

	// set the checkbox
//	checkbox = WMrDialog_GetItemByID(inDialog, ONcOptions_CB_DialogOn);
//	WMrCheckBox_SetCheck(checkbox, ONrPersist_IsDialogOn());

//	checkbox = WMrDialog_GetItemByID(inDialog, ONcOptions_CB_MusicOn);
//	WMrCheckBox_SetCheck(checkbox, ONrPersist_IsMusicOn());

	checkbox = WMrDialog_GetItemByID(inDialog, ONcOptions_CB_SubtitlesOn);
	WMrCheckBox_SetCheck(checkbox, ONrPersist_AreSubtitlesOn());

	checkbox = WMrDialog_GetItemByID(inDialog, ONcOptions_CB_InvertMouseOn);
	WMrCheckBox_SetCheck(checkbox, ONrPersist_IsInvertMouseOn());

	// set the popup menu
	popup = WMrDialog_GetItemByID(inDialog, ONcOptions_PM_Difficulty);
	WMrPopupMenu_SetSelection(popup, (UUtInt16)ONrPersist_GetDifficulty());
}

// ----------------------------------------------------------------------
static void
ONiOGU_Options_InitDialog(
	WMtDialog				*inDialog)
{
	WMtWindow				*slider;
	WMtWindow				*popup;

	// set the range on the sliders
	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_Quality);
	WMrSlider_SetRange(
		slider,
		(UUtInt32)ONcGraphicsQuality_Min,
		(UUtInt32)ONcGraphicsQuality_Max);

	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_Gamma);
	if (slider != NULL) {
		WMrSlider_SetRange(slider, 0, 100);
	}

	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_OverallVol);
	WMrSlider_SetRange(slider, 0, 100);

//	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_DialogVol);
//	WMrSlider_SetRange(slider, 0, 100);

//	slider = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_MusicVol);
//	WMrSlider_SetRange(slider, 0, 100);

	// build the popup menu
	popup = WMrDialog_GetItemByID(inDialog, ONcOptions_PM_Resolution);
	ONiResolutions_AddToPopup(popup);

	// set the fields
	ONiOBU_Options_SetControls(inDialog);

#ifdef __APPLE__
	/* #89: the shipping Options template can't gain controls by editing game
	 * data, so the renderer choice is created here: the Sound box becomes
	 * "Sound and Renderer" and gains an OpenGL / Metal checkbox row under
	 * Overall Volume. See ONiOGU_Options_AddRendererRow. */
	{
		WMtWindow				*anchor;
		UUtBool					anchor_is_checkbox;

		// the template's own checkbox; the gamma slider is the fallback
		anchor = WMrDialog_GetItemByID(inDialog, ONcOptions_CB_InvertMouseOn);
		anchor_is_checkbox = (UUtBool)(anchor != NULL);
		if (anchor == NULL)
		{
			anchor = WMrDialog_GetItemByID(inDialog, ONcOptions_Sldr_Gamma);
		}
		// #89 diagnostics: every step reports until the on-screen result is
		// confirmed.
		UUrStartupMessage("options renderer toggle: %s",
			anchor_is_checkbox ? "anchor invert-mouse" :
			(anchor != NULL) ? "anchor gamma (fallback)" : "MISSING - no checkbox");
		if ((anchor != NULL) &&
			(WMrDialog_GetItemByID(inDialog, ONcOptions_CB_MetalRenderer) == NULL) &&
			(WMrDialog_GetItemByID(inDialog, ONcOptions_CB_OpenGLRenderer) == NULL))
		{
			ONiOGU_Options_AddRendererRow(inDialog, anchor, anchor_is_checkbox);
		}
	}
#endif
}

// ----------------------------------------------------------------------
static void
ONiOGU_Options_HandleCommand(
	WMtDialog				*inDialog,
	uintptr_t				inParam1,
	WMtWindow				*inControl)
{
	UUtUns16				control_id;
	UUtUns16				command_type;
	float					volume;

	control_id = UUmLowWord(inParam1);
	command_type = UUmHighWord(inParam1);

	switch (control_id)
	{
		case ONcOptions_Sldr_Gamma:
			if (command_type == SLcNotify_NewPosition)
			{
				float gamma_slider_position = (float) WMrSlider_GetPosition(inControl);

				ONrPersist_SetGamma(gamma_slider_position / 100.f);
			}
		break;

		case ONcOptions_Sldr_Quality:
			if (command_type == SLcNotify_NewPosition)
			{
				ONtGraphicsQuality		quality;

				quality = (ONtGraphicsQuality)WMrSlider_GetPosition(inControl);
				ONrPersist_SetGraphicsQuality(quality);
			}
		break;

		case ONcOptions_Sldr_OverallVol:
			if (command_type != SLcNotify_NewPosition) { break; }
			volume = ((float)WMrSlider_GetPosition(inControl)) / 100.0f;
			ONrPersist_SetOverallVolume(volume);
		break;

/*		case ONcOptions_CB_DialogOn:
			if (command_type != WMcNotify_Click) { break; }
			ONrPersist_SetDialogOn(WMrCheckBox_GetCheck(inControl));
		break;

		case ONcOptions_Sldr_DialogVol:
			if (command_type != SLcNotify_NewPosition) { break; }
			volume = ((float)WMrSlider_GetPosition(inControl)) / 100.0f;
			ONrPersist_SetDialogVolume(volume);
		break;

		case ONcOptions_CB_MusicOn:
			if (command_type != WMcNotify_Click) { break; }
			ONrPersist_SetMusicOn(WMrCheckBox_GetCheck(inControl));
		break;

		case ONcOptions_Sldr_MusicVol:
			if (command_type != SLcNotify_NewPosition) { break; }
			volume = ((float)WMrSlider_GetPosition(inControl)) / 100.0f;
			ONrPersist_SetMusicVolume(volume);
		break;*/

		case ONcOptions_CB_SubtitlesOn:
			if (command_type != WMcNotify_Click) { break; }
			ONrPersist_SetSubtitlesOn(WMrCheckBox_GetCheck(inControl));
		break;

		case ONcOptions_CB_InvertMouseOn:
			if (command_type != WMcNotify_Click) { break; }
			ONrPersist_SetInvertMouseOn(WMrCheckBox_GetCheck(inControl));
		break;

#ifdef __APPLE__
		case ONcOptions_CB_OpenGLRenderer:
		case ONcOptions_CB_MetalRenderer:
			if (command_type != WMcNotify_Click) { break; }
			{
				UUtBool			want_metal;
				WMtWindow		*other;
				UUtUns32		answer;

				want_metal = (UUtBool)(control_id == ONcOptions_CB_MetalRenderer);
				other = WMrDialog_GetItemByID(
					inDialog,
					want_metal ? ONcOptions_CB_OpenGLRenderer : ONcOptions_CB_MetalRenderer);

				/* the click has already toggled the box: if it is now off, it
				 * was the checked one, so put it back and do nothing else */
				if (!WMrCheckBox_GetCheck(inControl))
				{
					WMrCheckBox_SetCheck(inControl, UUcTrue);
					break;
				}
				if (other != NULL) { WMrCheckBox_SetCheck(other, UUcFalse); }

				if (want_metal == ONgCommandLine.useMetal)
				{
					/* back to the renderer running now: just save it */
					if (!ONrRendererPref_Write(want_metal))
					{
						WMrCheckBox_SetCheck(inControl, UUcFalse);
						if (other != NULL) { WMrCheckBox_SetCheck(other, UUcTrue); }
						UUrStartupMessage("options renderer: could not save the renderer preference (%s); selection restored",
							want_metal ? "Metal" : "OpenGL");
						break;
					}
					UUrStartupMessage("options renderer: preference back to the running renderer (%s), no relaunch",
						want_metal ? "Metal" : "OpenGL");
					break;
				}

				answer = ONiOGU_RelaunchYesNo_Display(want_metal);
				if (answer != ONcQuitYesNo_Btn_Yes)
				{
					WMrCheckBox_SetCheck(inControl, UUcFalse);
					if (other != NULL) { WMrCheckBox_SetCheck(other, UUcTrue); }
					UUrStartupMessage("options renderer: switch to %s cancelled (answer %u), nothing written",
						want_metal ? "Metal" : "OpenGL", (unsigned)answer);
					break;
				}

				if (!ONrRendererPref_Write(want_metal))
				{
					WMrCheckBox_SetCheck(inControl, UUcFalse);
					if (other != NULL) { WMrCheckBox_SetCheck(other, UUcTrue); }
					UUrStartupMessage("options renderer: could not save the renderer preference (%s); selection restored, no relaunch",
						want_metal ? "Metal" : "OpenGL");
					break;
				}
				UUrStartupMessage("options renderer: preference saved (%s), relaunching", want_metal ? "Metal" : "OpenGL");

				if (!ONiOGU_RelaunchAfterQuit())
				{
					/* saved, but can't relaunch: fall back to asking for a manual restart */
					ONiOutGameUI_ChangeRestart_Display();
					break;
				}

				/* quit through the normal path: close Options (the main menu
				 * sees the result and closes itself), post quit, end the game */
				WMrDialog_ModalEnd(inDialog, ONcOptions_Result_Relaunch);
				WMrMessage_Post(NULL, WMcMessage_Quit, 0, 0);
				ONgTerminateGame = UUcTrue;
			}
		break;
#endif

		case WMcDialogItem_Cancel:
			if (command_type != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, 0);
		break;
	}
}

// ----------------------------------------------------------------------
static void
ONiOGU_Options_HandleMenuCommand(
	WMtDialog				*inDialog,
	uintptr_t				inParam1,
	WMtWindow				*inMenu)
{
	UUtUns16				item_id;

	item_id = UUmLowWord(inParam1);

	switch (WMrWindow_GetID(inMenu))
	{
		case ONcOptions_PM_Resolution:
			ONiResolution_Switch(inDialog, inMenu, item_id);
		break;

		case ONcOptions_PM_Difficulty:
			ONrPersist_SetDifficulty((ONtDifficultyLevel)item_id);
		break;
	}
}

// ----------------------------------------------------------------------
static UUtBool
ONiOGU_Options_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_InitDialog:
			ONiOGU_Options_InitDialog(inDialog);
		break;

		case WMcMessage_Command:
			ONiOGU_Options_HandleCommand(inDialog, inParam1, (WMtWindow*)inParam2);
		break;

		case WMcMessage_MenuCommand:
			ONiOGU_Options_HandleMenuCommand(inDialog, inParam1, (WMtWindow*)inParam2);
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
UUtUns32
ONrOutGameUI_Options_Display(
	void)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */
	WMtDialogID				dialog_id;

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// Shipping data is Windows-only; the Mac dialog (157) doesn't exist
	// in the .dat files, so requesting it silently no-ops. Use the PC
	// dialog (152) on all platforms when running against shipping data.
	dialog_id = ONcOGU_OptionsID_PC;

	// display the dialog
	WMrDialog_ModalBegin(
		dialog_id,
		NULL,
		ONiOGU_Options_Callback,
		0,
		&message);

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return message;
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static void
ONiOGU_LoadGame_InitDialog(
	WMtDialog				*inDialog)
{
	OWrLevelList_Initialize(inDialog, ONcLoadGame_LB_Levels);
	WMrWindow_SetFocus(WMrDialog_GetItemByID(inDialog, OWcLevelLoad_LB_Level));
}

// ----------------------------------------------------------------------
static void
ONiOGU_LoadGame_HandleCommand(
	WMtDialog				*inDialog,
	uintptr_t				inParam1,
	WMtWindow				*inControl)
{
	UUtUns16				control_id;
	UUtUns16				command_type;
	UUtUns32				level;

	control_id = UUmLowWord(inParam1);
	command_type = UUmHighWord(inParam1);

	switch (control_id)
	{
		case ONcLoadGame_LB_Levels:
			if (command_type != WMcNotify_DoubleClick) { break; }
			level = OWrLevelList_GetLevelNumber(inDialog, ONcLoadGame_LB_Levels);
			if (level != (UUtUns16)(-1))
			{
				WMrDialog_ModalEnd(inDialog, level);
			}
		break;

		case ONcLoadGame_Btn_Load:
			if (command_type != WMcNotify_Click) { break; }
			level = OWrLevelList_GetLevelNumber(inDialog, ONcLoadGame_LB_Levels);
			if (level != (UUtUns16)(-1))
			{
				WMrDialog_ModalEnd(inDialog, level);
			}
		break;

		case ONcLoadGame_Btn_Cancel:
			if (command_type != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, 0);
		break;
	}
}

// ----------------------------------------------------------------------
static UUtBool
ONiOGU_LoadGame_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_InitDialog:
			ONiOGU_LoadGame_InitDialog(inDialog);
		break;

		case WMcMessage_Command:
			ONiOGU_LoadGame_HandleCommand(inDialog, inParam1, (WMtWindow*)inParam2);
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
UUtUns32
ONrOutGameUI_LoadGame_Display(
	void)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// display the dialog
	WMrDialog_ModalBegin(
		ONcOGU_LoadGameID,
		NULL,
		ONiOGU_LoadGame_Callback,
		0,
		&message);

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return message;
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static UUtBool
ONiOGU_NewGame_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_Command:
			if (UUmHighWord(inParam1) != WMcNotify_Click) { break; }
			WMrDialog_ModalEnd(inDialog, UUmLowWord(inParam1));
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
UUtUns32
ONrOutGameUI_NewGame_Display(
	void)
{
	PStPartSpecUI			*partspec_ui;
	PStPartSpecUI			*temp_ui;
	uintptr_t				message;	/* #69 — ModalBegin writes a uintptr_t */

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// display the dialog
	WMrDialog_ModalBegin(
		ONcOGU_NewGameID,
		NULL,
		ONiOGU_NewGame_Callback,
		0,
		&message);

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return message;
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_FadeMusic(
	void)
{
	// fade out the music first
	if (OSrMusic_IsPlaying())
	{
		M3tPoint3D position;
		M3tVector3D facing;
		UUtUns32 count;

		MUmVector_Set(position, 0.0f, 0.0f, 0.0f);
		MUmVector_Set(facing, 0.0f, 0.0f, 0.0f);

		OSrMusic_SetVolume(0.0f, OScMainMusic_FadeOutTime);
		OSrMusic_Stop();

		count = UUrMachineTime_Sixtieths() + (UUtUns32)(60.0f * OScMainMusic_FadeOutTime);
		do
		{
			OSrUpdate(&position, &facing);
			SS2rUpdate();
		}
		while (count > UUrMachineTime_Sixtieths());

		OSrMusic_Halt();
	}
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_InitDialog(
	WMtDialog				*inDialog)
{
	WMtWindow				*options;
	WMtWindow				*resume;

	options = WMrDialog_GetItemByID(inDialog, ONcMainMenu_Btn_Options);
	resume = WMrDialog_GetItemByID(inDialog, ONcMainMenu_Btn_Resume);

	if (ONrLevel_GetCurrentLevel() > 0)
	{
		WMrWindow_SetVisible(options, UUcFalse);
		WMrWindow_SetVisible(resume, UUcTrue);
	}
	else
	{
		WMrWindow_SetVisible(options, UUcTrue);
		WMrWindow_SetVisible(resume, UUcFalse);
	}

	// stop all currently playing sounds, only allow music
	OSrSetScriptOnly(UUcTrue);
	SSrPlayingChannels_Pause();

	OSrMusic_Start(OScMusicScore_Win, 0.0f);
	OSrMusic_SetVolume(1.0f, OScMainMusic_FadeInTime);
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_Destroy(
	WMtDialog				*inDialog)
{
	if (OSrMusic_IsPlaying() == UUcTrue)
	{
		OSrMusic_SetVolume(0.0f, 0.5f);
		OSrMusic_Stop();
	}

	// allow more than just music
	OSrSetScriptOnly(UUcFalse);
	SSrPlayingChannels_Resume();
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_HandleNewGame(
	WMtDialog				*inDialog)
{
	UUtUns32				result;

	// display the new game dialog
	result = ONrOutGameUI_NewGame_Display();
	if (result == ONcNewGame_Btn_Yes)
	{
		// fade out the music first
		ONiOGU_MainMenu_FadeMusic();

		// if the user selects start, then close the main menu
		WMrDialog_ModalEnd(inDialog, 0);

		// update the screen
		WMrUpdate();
		WMrDisplay();

		// load the level
		OWrLevelLoad_StartLevel(1);
	}
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_HandleLoadGame(
	WMtDialog				*inDialog)
{
	UUtUns16				result;

	// display the load game dialog
	result = (UUtUns16)ONrOutGameUI_LoadGame_Display();
	if (result != 0)
	{
		// fade out the music first
		ONiOGU_MainMenu_FadeMusic();

		// if the user selects load, then close the main menu
		WMrDialog_ModalEnd(inDialog, 0);

		// update the screen
		WMrUpdate();
		WMrDisplay();

		// load the level
		OWrLevelLoad_StartLevel(result);
	}
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_HandleOptions(
	WMtDialog				*inDialog)
{
#ifdef __APPLE__
	if (ONrOutGameUI_Options_Display() == ONcOptions_Result_Relaunch)
	{
		/* #89: a renderer switch armed a relaunch and posted the quit; close
		 * the main menu the way the Quit button does */
		ONiOGU_MainMenu_FadeMusic();
		WMrDialog_ModalEnd(inDialog, 0);
		ONgTerminateGame = UUcTrue;
	}
#else
	ONrOutGameUI_Options_Display();
#endif
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_HandleQuit(
	WMtDialog				*inDialog)
{
	UUtUns32				result;

	// display the Are you sure you want to quit dialog
	result = ONrOutGameUI_QuitYesNo_Display();
	if (result == ONcQuitYesNo_Btn_Yes)
	{
		// fade out the music first
		ONiOGU_MainMenu_FadeMusic();

		// if the user selects load, then close the main menu
		WMrDialog_ModalEnd(inDialog, 0);

		WMrMessage_Post(NULL, WMcMessage_Quit, 0, 0);

		// end the game
		ONgTerminateGame = UUcTrue;
	}
}

// ----------------------------------------------------------------------
static void
ONiOGU_MainMenu_HandleCommand(
	WMtDialog				*inDialog,
	uintptr_t				inParam1,
	WMtWindow				*inControl)
{
	UUtUns16				control_id;
	UUtUns16				command_type;

	control_id = UUmLowWord(inParam1);
	command_type = UUmHighWord(inParam1);

	if (command_type != WMcNotify_Click) { return; }

	switch (control_id)
	{
		case ONcMainMenu_Btn_NewGame:
			ONiOGU_MainMenu_HandleNewGame(inDialog);
		break;

		case ONcMainMenu_Btn_LoadGame:
			ONiOGU_MainMenu_HandleLoadGame(inDialog);
		break;

		case ONcMainMenu_Btn_Options:
			ONiOGU_MainMenu_HandleOptions(inDialog);
		break;

		case ONcMainMenu_Btn_Quit:
			ONiOGU_MainMenu_HandleQuit(inDialog);
		break;

		case ONcMainMenu_Btn_Resume:
			WMrDialog_ModalEnd(inDialog, 0);
		break;

		case WMcDialogItem_Cancel:
			if (ONrLevel_GetCurrentLevel() != 0)
			{
				WMrDialog_ModalEnd(inDialog, 0);
			}
		break;
	}
}

// ----------------------------------------------------------------------
static UUtBool
ONiOGU_MainMenu_Callback(
	WMtDialog				*inDialog,
	WMtMessage				inMessage,
	uintptr_t				inParam1,
	uintptr_t				inParam2)
{
	UUtBool					handled;

	handled = UUcTrue;

	switch (inMessage)
	{
		case WMcMessage_InitDialog:
			ONiOGU_MainMenu_InitDialog(inDialog);
		break;

		case WMcMessage_Destroy:
			ONiOGU_MainMenu_Destroy(inDialog);
		break;

		case WMcMessage_Command:
			ONiOGU_MainMenu_HandleCommand(
				inDialog,
				inParam1,
				(WMtWindow*)inParam2);
		break;

		default:
			handled = UUcFalse;
		break;
	}

	return handled;
}

// ----------------------------------------------------------------------
UUtUns32
ONrOutGameUI_MainMenu_Display(
	void)
{
	PStPartSpecUI				*partspec_ui;
	PStPartSpecUI				*temp_ui;

	// save the current ui
	partspec_ui = PSrPartSpecUI_GetActive();

	// set the ui to the out of game ui
	temp_ui = PSrPartSpecUI_GetByName(ONcOutGameUIName);
	if (temp_ui != NULL) { PSrPartSpecUI_SetActive(temp_ui); }

	// set the background to black
	WMrSetDesktopBackground(PSrPartSpec_LoadByType(PScPartSpecType_BackgroundColor_Black));

	// display the dialog
	WMrDialog_ModalBegin(
		ONcOGU_MainMenuID,
		NULL,
		ONiOGU_MainMenu_Callback,
		0,
		NULL);

	// set the desktop background to none
	WMrSetDesktopBackground(PSrPartSpec_LoadByType(PScPartSpecType_BackgroundColor_None));

	// reset the active ui
	PSrPartSpecUI_SetActive(partspec_ui);

	return UUcError_None;
}
