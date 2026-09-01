/*
  Hatari - shortcut.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Shortcut keys
*/
const char ShortCut_fileid[] = "Hatari shortcut.c";

#include "main.h"
#include "dialog.h"
#include "dimension.hpp"
#include "grab.h"
#include "reset.h"
#include "screen.h"
#include "configuration.h"
#include "shortcut.h"
#include "debugui.h"
#include "snd.h"
#include "statusbar.h"

static SHORTCUTKEYIDX ShortCutKey = SHORTCUT_NONE;  /* current shortcut key */


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle full-screen
 */
static void ShortCut_FullScreen(void)
{
	if (!bInFullScreen)
	{
		Screen_EnterFullScreen();
	}
	else
	{
		Screen_ReturnFromFullScreen();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to toggle mouse grabbing mode
 */
static void ShortCut_MouseGrab(void)
{
	bGrabMouse = !bGrabMouse;        /* Toggle flag */
	Screen_SetMouseGrab(bGrabMouse);
}


/*-----------------------------------------------------------------------*/
/**
 * Shortcut to sound on/off
 */
static void ShortCut_SoundOnOff(void)
{
	ConfigureParams.Sound.bEnableSound = !ConfigureParams.Sound.bEnableSound;
	if (bEmulationActive) {
		Sound_Pause(!ConfigureParams.Sound.bEnableSound);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Shortcut to M68K debug interface
 */
static void ShortCut_Debug_M68K(void)
{
	bool running;

	running = Main_PauseEmulation(true);
	/* Call the debugger */
	DebugUI(REASON_USER);
	if (running)
		Main_UnPauseEmulation();
}

/*-----------------------------------------------------------------------*/
/**
 * Shortcut to I860 debug interface
 */
static void ShortCut_Debug_I860(void)
{
	if (bInFullScreen)
		Screen_ReturnFromFullScreen();

	/* i860 thread silently pauses 68k emulation if necessary */
	Screen_StatusbarMessage("I860 Console Debugger", 100);

	/* Call the debugger */
	nd_start_debugger();
}

/**
 * Shortcut to pausing
 */
static void ShortCut_Pause(void)
{
	if (!Main_UnPauseEmulation())
		Main_PauseEmulation(true);
}

/**
 * Shortcut to switch monochrome and dimension screen
 */
static void ShortCut_Dimension(void)
{
	if (ConfigureParams.System.nMachineType == NEXT_STATION ||
		ConfigureParams.Screen.nMode != SCREEN_SINGLE) {
		return;
	}

	for (;;) {
		ConfigureParams.Screen.nSingleModeSlot += 2;
		ConfigureParams.Screen.nSingleModeSlot &= 6;
		if (ConfigureParams.Screen.nSingleModeSlot == 0) {
			break;
		}
		if (ConfigureParams.Dimension.board[ND_NUM(ConfigureParams.Screen.nSingleModeSlot)].bEnabled) {
			break;
		}
	}

	Statusbar_UpdateInfo();
}

/**
 * Shortcut to show/hide statusbar
 */
static void ShortCut_StatusBar(void)
{
	ConfigureParams.Screen.bShowStatusbar = !ConfigureParams.Screen.bShowStatusbar;
	Screen_Reset();
}

/**
 * Shortcut to show/hide titlebar
 */
static void ShortCut_TitleBar(void)
{
	ConfigureParams.Screen.bShowTitlebar = !ConfigureParams.Screen.bShowTitlebar;
	Screen_TitlebarChanged();
}


/*-----------------------------------------------------------------------*/
/**
 * Check to see if pressed any shortcut keys, and call handling function
 */
void ShortCut_ActKey(void)
{
	if (ShortCutKey == SHORTCUT_NONE)
		return;

	switch (ShortCutKey)
	{
	 case SHORTCUT_OPTIONS:
		Dialog_DoProperty();           /* Show options dialog */
		break;
	 case SHORTCUT_FULLSCREEN:
		ShortCut_FullScreen();         /* Switch between fullscreen/windowed mode */
		break;
	 case SHORTCUT_MOUSEGRAB:
		ShortCut_MouseGrab();          /* Toggle mouse grab */
		break;
	 case SHORTCUT_COLDRESET:
		Main_PauseEmulation(false);
		if (Reset_Cold())              /* Reset emulator with 'cold' (clear all) */
			Main_RequestQuit(false);
		Main_UnPauseEmulation();
		break;
	 case SHORTCUT_SCREENSHOT:
		Grab_Screen();                 /* Grab screenshot */
		break;
	 case SHORTCUT_RECORD:
		Grab_SoundToggle();            /* Enable/disable sound recording */
		break;
	 case SHORTCUT_SOUND:
		ShortCut_SoundOnOff();         /* Enable/disable sound */
		break;
	 case SHORTCUT_DEBUG_M68K:
		ShortCut_Debug_M68K();         /* Invoke the Debug UI */
		break;
	 case SHORTCUT_DEBUG_I860:
		ShortCut_Debug_I860();         /* Invoke the M68K UI */
		break;
	 case SHORTCUT_PAUSE:
		ShortCut_Pause();              /* Invoke Pause */
		break;
	 case SHORTCUT_QUIT:
		Main_RequestQuit(true);
		break;
	 case SHORTCUT_DIMENSION:
		ShortCut_Dimension();
		break;
	 case SHORTCUT_STATUSBAR:
		ShortCut_StatusBar();
		break;
	case SHORTCUT_TITLEBAR:
		ShortCut_TitleBar();
		break;
	 case SHORTCUT_KEYS:
	 case SHORTCUT_NONE:
		/* ERROR: cannot happen, just make compiler happy */
	 default:
		break;
	}
	ShortCutKey = SHORTCUT_NONE;
}


/*-----------------------------------------------------------------------*/
/**
 * Invoke shortcut identified by name. This supports only keys for
 * functionality that cannot be invoked with command line options
 * or otherwise for remote GUIs etc.
 */
bool Shortcut_Invoke(const char *shortcut)
{
	struct {
		SHORTCUTKEYIDX id;
		const char *name;
	} shortcuts[] = {
		{ SHORTCUT_MOUSEGRAB, "mousegrab" },
		{ SHORTCUT_COLDRESET, "coldreset" },
		{ SHORTCUT_QUIT, "quit" },
		{ SHORTCUT_NONE, NULL }
	};
	int i;

	if (ShortCutKey != SHORTCUT_NONE)
	{
		fprintf(stderr, "WARNING: Shortcut invocation failed, shortcut already active\n");
		return false;
	}
	for (i = 0; shortcuts[i].name; i++)
	{
		if (strcmp(shortcut, shortcuts[i].name) == 0)
		{
			ShortCutKey = shortcuts[i].id;
			ShortCut_ActKey();
			ShortCutKey = SHORTCUT_NONE;
			return true;
		}
	}
	fprintf(stderr, "WARNING: unknown shortcut '%s'\n\n", shortcut);
	fprintf(stderr, "Hatari shortcuts are:\n");
	for (i = 0; shortcuts[i].name; i++)
	{
		fprintf(stderr, "- %s\n", shortcuts[i].name);
	}
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether given key was any of the ones in given shortcut array.
 * Return corresponding array index or SHORTCUT_NONE for no match
 */
static SHORTCUTKEYIDX ShortCut_CheckKey(int symkey, int *keys)
{
	SHORTCUTKEYIDX key;
	for (key = SHORTCUT_OPTIONS; key < SHORTCUT_KEYS; key++)
	{
		if (symkey == keys[key])
			return key;
	}
	return SHORTCUT_NONE;
}

/*-----------------------------------------------------------------------*/
/**
 * Check which Shortcut key is pressed/released.
 * If press is set, store the key array index.
 * Return true if key combo matched to a shortcut
 */
bool ShortCut_CheckKeys(int symkey, bool with_mod, bool press)
{
	SHORTCUTKEYIDX key;

	if (symkey == 0)
		return false;

	if (with_mod)
		key = ShortCut_CheckKey(symkey, ConfigureParams.Shortcut.withModifier);
	else
		key = ShortCut_CheckKey(symkey, ConfigureParams.Shortcut.withoutModifier);

	if (key == SHORTCUT_NONE)
		return false;
	if (press)
		ShortCutKey = key;
	return true;
}
