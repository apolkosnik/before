/*
  Previous - dlgKeyboard.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgKeyboard_fileid[] = "Previous dlgKeyboard.c";

#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "str.h"
#include "keymap.h"

#define DLGKEY_SCPREV     4
#define DLGKEY_SCNAME     5
#define DLGKEY_SCNEXT     6
#define DLGKEY_SCMODVAL   7
#define DLGKEY_SCMODDEF   9
#define DLGKEY_SCNOMODVAL 11
#define DLGKEY_SCNOMODDEF 12
#define DLGKEY_EXIT       14

static char sc_modval[16];
static char sc_nomodval[16];

/* The keyboard shortcut dialog: */
static SGOBJ shortcutdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 46,17, NULL },
	{ SGTEXT, 0, 0, 16,1, 14,1, "Shortcut setup" },

	{ SGBOX, 0, 0, 1,3, 44,7, NULL },
	{ SGTEXT, 0, 0, 2,4, 9,1, "Function:" },
	{ SGBUTTON, 0, 0, 12,4, 3,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT, 0, 0, 20,4, 20,1, NULL },
	{ SGBUTTON, 0, 0, 16,4, 3,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT, 0, 0, 2,6, 17,1, "With modifier:" },
	{ SGTEXT, 0, 0, 20,6, 12,1, sc_modval },
	{ SGBUTTON, SG_TOUCHEXIT, 0, 36,6, 8,1, "Define" },
	{ SGTEXT, 0, 0, 2,8, 17,1, "Without modifier:" },
	{ SGTEXT, 0, 0, 20,8, 12,1, sc_nomodval },
	{ SGBUTTON, SG_TOUCHEXIT, 0, 36,8, 8,1, "Define" },

	{ SGTEXT, 0, 0, 3,11, 8,1, "Click define and press new shortcut key."},

	{ SGBUTTON, SG_DEFAULT, 0, 18,14, 10,1, "Done" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


static char sc_names[SHORTCUT_KEYS][20] = {
	"Show main menu",
	"Fullscreen on/off",
	"Lock/unlock mouse",
	"Cold reset",
	"Grab screen",
	"Recording on/off",
	"Sound on/off",
	"Debug 68k",
	"Debug i860",
	"Pause",
	"Quit",
	"Screen toggle",
	"Show/hide statusbar",
	"Show/hide titlebar"
};


/**
 * Show dialogs for defining shortcut keys and wait for a key press.
 */
static void DlgKbd_DefineShortcutKey(int sc, bool withMod)
{
	SDL_Event sdlEvent;
	int *pscs;
	int i;

	if (bQuitProgram)
		return;

	if (withMod)
		pscs = ConfigureParams.Shortcut.withModifier;
	else
		pscs = ConfigureParams.Shortcut.withoutModifier;

	/* drain buffered key events */
	SDL_Delay(200);
	while (SDL_PollEvent(&sdlEvent))
	{
		if (sdlEvent.type == SDL_EVENT_KEY_UP || sdlEvent.type == SDL_EVENT_KEY_DOWN)
			break;
	}

	/* get the real key */
	do
	{
		SDL_WaitEvent(&sdlEvent);
		switch (sdlEvent.type)
		{
		 case SDL_EVENT_KEY_DOWN:
			pscs[sc] = sdlEvent.key.key;
			break;
		 case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (sdlEvent.button.button == SDL_BUTTON_RIGHT)
			{
				pscs[sc] = 0;
				return;
			}
			else if (sdlEvent.button.button == SDL_BUTTON_LEFT)
			{
				SDL_PushEvent(&sdlEvent); /* Forward mouse click to shortcut dialog */
				return;
			}
			break;
		 case SDL_EVENT_QUIT:
			bQuitProgram = true;
			return;
		}
	} while (sdlEvent.type != SDL_EVENT_KEY_UP);

	/* Make sure that no other shortcut key has the same value */
	for (i = 0; i < SHORTCUT_KEYS; i++)
	{
		if (i == sc)
			continue;
		if (pscs[i] == pscs[sc])
		{
			pscs[i] = 0;
			DlgAlert_Notice("Removing key from other shortcut!");
		}
	}
}


/**
 * Set name for given shortcut, or show it's unset
 */
static void DlgKbd_SetName(char *str, size_t maxlen, int keysym)
{
	if (keysym)
		Str_Copy(str, Keymap_GetKeyName(keysym), maxlen);
	else
		Str_Copy(str, "<not set>", maxlen);
}


/**
 * Refresh the shortcut texts in the dialog
 */
static void DlgKbd_RefreshShortcuts(int sc)
{
	int keysym;

	/* with modifier */
	keysym = ConfigureParams.Shortcut.withModifier[sc];
	DlgKbd_SetName(sc_modval, sizeof(sc_modval), keysym);

	/* without modifier */
	keysym = ConfigureParams.Shortcut.withoutModifier[sc];
	DlgKbd_SetName(sc_nomodval, sizeof(sc_nomodval), keysym);

	shortcutdlg[DLGKEY_SCNAME].txt = sc_names[sc];
}

/**
 * Show and process the "Shortcut" dialog.
 */
static void Dialog_ShortcutDlg(void)
{
	int but;
	int cur_sc = 0;

	SDLGui_CenterDlg(shortcutdlg);

	/* Set up dialog from actual values: */	
	DlgKbd_RefreshShortcuts(cur_sc);

	/* Show the dialog: */
	do
	{
		but = SDLGui_DoDialog(shortcutdlg);

		switch (but)
		{
		 case DLGKEY_SCPREV:
			if (cur_sc > 0)
			{
				--cur_sc;
				DlgKbd_RefreshShortcuts(cur_sc);
			}
			break;
		 case DLGKEY_SCNEXT:
			if (cur_sc < SHORTCUT_KEYS-1)
			{
				++cur_sc;
				DlgKbd_RefreshShortcuts(cur_sc);
			}
			break;
		 case DLGKEY_SCMODDEF:
			DlgKbd_DefineShortcutKey(cur_sc, true);
			DlgKbd_RefreshShortcuts(cur_sc);
			shortcutdlg[but].state &= ~SG_SELECTED;
			break;
		 case DLGKEY_SCNOMODDEF:
			DlgKbd_DefineShortcutKey(cur_sc, false);
			DlgKbd_RefreshShortcuts(cur_sc);
			shortcutdlg[but].state &= ~SG_SELECTED;
			break;
		}
	}
	while (but != DLGKEY_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);
}


#define DLGKEYMAIN_SCANCODE  4
#define DLGKEYMAIN_SYMBOLIC  5
#define DLGKEYMAIN_SWAP      8
#define DLGKEYMAIN_DEFINE    12
#define DLGKEYMAIN_EXIT      49

static char key_names[SHORTCUT_KEYS][2][16];

/* The keyboard dialog: */
static SGOBJ keyboarddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 49,32, NULL },
	{ SGTEXT, 0, 0, 16,1, 16,1, "Keyboard options" },

	{ SGBOX, 0, 0, 2,3, 22,7, NULL },
	{ SGTEXT, 0, 0, 4,4, 17,1, "Keyboard mapping:" },
	{ SGRADIOBUT, 0, 0, 6,6, 10,1, "Scancode" },
	{ SGRADIOBUT, 0, 0, 6,8, 10,1, "Symbolic" },

	{ SGBOX, 0, 0, 25,3, 22,7, NULL },
	{ SGTEXT, 0, 0, 27,4, 12,1, "Key options:" },
	{ SGCHECKBOX, 0, 0, 27,6, 18,1, "Swap cmd and alt" },

	{ SGBOX,  0, 0,  2,11, 45,16, NULL },
	{ SGTEXT, 0, 0,  4,12, 10,1, "Shortcuts:" },
	{ SGTEXT, 0, 0, 18,12, 10,1, "ctrl-alt-X  or  Fn" },
	{ SGBUTTON, 0, 0, 38,12, 8,1, "Change" },

	{ SGTEXT, 0, 0,  6,14, 20,1, sc_names[SHORTCUT_OPTIONS] },
	{ SGTEXT, 0, 0, 26,14, 13,1, key_names[SHORTCUT_OPTIONS][0] },
	{ SGTEXT, 0, 0, 34,14, 11,1, key_names[SHORTCUT_OPTIONS][1] },
	{ SGTEXT, 0, 0,  6,15, 20,1, sc_names[SHORTCUT_PAUSE] },
	{ SGTEXT, 0, 0, 26,15,  8,1, key_names[SHORTCUT_PAUSE][0] },
	{ SGTEXT, 0, 0, 34,15, 11,1, key_names[SHORTCUT_PAUSE][1] },
	{ SGTEXT, 0, 0,  6,16, 20,1, sc_names[SHORTCUT_COLDRESET] },
	{ SGTEXT, 0, 0, 26,16,  8,1, key_names[SHORTCUT_COLDRESET][0] },
	{ SGTEXT, 0, 0, 34,16, 11,1, key_names[SHORTCUT_COLDRESET][1] },
	{ SGTEXT, 0, 0,  6,17, 20,1, sc_names[SHORTCUT_MOUSEGRAB] },
	{ SGTEXT, 0, 0, 26,17,  8,1, key_names[SHORTCUT_MOUSEGRAB][0] },
	{ SGTEXT, 0, 0, 34,17, 11,1, key_names[SHORTCUT_MOUSEGRAB][1] },
	{ SGTEXT, 0, 0,  6,18, 20,1, sc_names[SHORTCUT_DIMENSION] },
	{ SGTEXT, 0, 0, 26,18,  8,1, key_names[SHORTCUT_DIMENSION][0] },
	{ SGTEXT, 0, 0, 34,18, 11,1, key_names[SHORTCUT_DIMENSION][1] },
	{ SGTEXT, 0, 0,  6,19, 20,1, sc_names[SHORTCUT_SCREENSHOT] },
	{ SGTEXT, 0, 0, 26,19,  8,1, key_names[SHORTCUT_SCREENSHOT][0] },
	{ SGTEXT, 0, 0, 34,19, 11,1, key_names[SHORTCUT_SCREENSHOT][1] },
	{ SGTEXT, 0, 0,  6,20, 20,1, sc_names[SHORTCUT_RECORD] },
	{ SGTEXT, 0, 0, 26,20,  8,1, key_names[SHORTCUT_RECORD][0] },
	{ SGTEXT, 0, 0, 34,20, 11,1, key_names[SHORTCUT_RECORD][1] },
	{ SGTEXT, 0, 0,  6,21, 20,1, sc_names[SHORTCUT_FULLSCREEN] },
	{ SGTEXT, 0, 0, 26,21,  8,1, key_names[SHORTCUT_FULLSCREEN][0] },
	{ SGTEXT, 0, 0, 34,21, 11,1, key_names[SHORTCUT_FULLSCREEN][1] },
	{ SGTEXT, 0, 0,  6,22, 20,1, sc_names[SHORTCUT_STATUSBAR] },
	{ SGTEXT, 0, 0, 26,22,  8,1, key_names[SHORTCUT_STATUSBAR][0] },
	{ SGTEXT, 0, 0, 34,22, 11,1, key_names[SHORTCUT_STATUSBAR][1] },
	{ SGTEXT, 0, 0,  6,23, 20,1, sc_names[SHORTCUT_TITLEBAR] },
	{ SGTEXT, 0, 0, 26,23,  8,1, key_names[SHORTCUT_TITLEBAR][0] },
	{ SGTEXT, 0, 0, 34,23, 11,1, key_names[SHORTCUT_TITLEBAR][1] },
	{ SGTEXT, 0, 0,  6,24, 20,1, sc_names[SHORTCUT_SOUND] },
	{ SGTEXT, 0, 0, 26,24,  8,1, key_names[SHORTCUT_SOUND][0] },
	{ SGTEXT, 0, 0, 34,24, 11,1, key_names[SHORTCUT_SOUND][1] },
	{ SGTEXT, 0, 0,  6,25, 20,1, sc_names[SHORTCUT_QUIT] },
	{ SGTEXT, 0, 0, 26,25,  8,1, key_names[SHORTCUT_QUIT][0] },
	{ SGTEXT, 0, 0, 34,25, 11,1, key_names[SHORTCUT_QUIT][1] },

	{ SGBUTTON, SG_DEFAULT, 0, 14,29, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Keyboard" dialog.
 */
void Dialog_KeyboardDlg(void)
{
	int i, but;

	SDLGui_CenterDlg(keyboarddlg);

	/* Set up dialog from actual values: */
	keyboarddlg[DLGKEYMAIN_SCANCODE].state &= ~SG_SELECTED;
	keyboarddlg[DLGKEYMAIN_SYMBOLIC].state &= ~SG_SELECTED;

	switch (ConfigureParams.Keyboard.nKeymapType) {
		case KEYMAP_SCANCODE:
			keyboarddlg[DLGKEYMAIN_SCANCODE].state |= SG_SELECTED;
			break;
		case KEYMAP_SYMBOLIC:
			keyboarddlg[DLGKEYMAIN_SYMBOLIC].state |= SG_SELECTED;
			break;
			
		default:
			break;
	}
	
	if (ConfigureParams.Keyboard.bSwapCmdAlt) {
		keyboarddlg[DLGKEYMAIN_SWAP].state |= SG_SELECTED;
	}
	else {
		keyboarddlg[DLGKEYMAIN_SWAP].state &= ~SG_SELECTED;
	}

	/* Show the dialog: */
	do
	{
		/* Load actual shortcut keys */
		for (i = 0; i < SHORTCUT_KEYS; i++) {
			snprintf(key_names[i][0], 8, "-%s", Keymap_GetKeyName(ConfigureParams.Shortcut.withModifier[i]));
			snprintf(key_names[i][1], 13, "%s", Keymap_GetKeyName(ConfigureParams.Shortcut.withoutModifier[i]));
		}
		
		but = SDLGui_DoDialog(keyboarddlg);
		
		if (but == DLGKEYMAIN_DEFINE) {
			Dialog_ShortcutDlg();
		}
	}
	while (but != DLGKEYMAIN_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog: */
	if (keyboarddlg[DLGKEYMAIN_SCANCODE].state & SG_SELECTED)
		ConfigureParams.Keyboard.nKeymapType = KEYMAP_SCANCODE;
	else
		ConfigureParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;

	ConfigureParams.Keyboard.bSwapCmdAlt = (keyboarddlg[DLGKEYMAIN_SWAP].state & SG_SELECTED);
}
