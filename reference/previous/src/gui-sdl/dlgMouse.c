/*
  Previous - dlgMouse.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgMouse_fileid[] = "Previous dlgMouse.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "tablet.h"


#define DLGMOUSE_CUSTOMISE     3
#define DLGMOUSE_LIN_VERYSLOW  6
#define DLGMOUSE_LIN_SLOW      7
#define DLGMOUSE_LIN_NORMAL    8
#define DLGMOUSE_LIN_FAST      9
#define DLGMOUSE_LIN_VERYFAST  10
#define DLGMOUSE_LIN_CUSTOM    11

#define DLGMOUSE_EXP_VERYSLOW  14
#define DLGMOUSE_EXP_SLOW      15
#define DLGMOUSE_EXP_NORMAL    16
#define DLGMOUSE_EXP_FAST      17
#define DLGMOUSE_EXP_VERYFAST  18
#define DLGMOUSE_EXP_CUSTOM    19

#define DLGMOUSE_CTRLCLCK      21
#define DLGMOUSE_MAPTOKEY      22
#define DLGMOUSE_AUTOLOCK      23
#define DLGMOUSE_TABLET        24
#define DLGMOUSE_TABLETCONF    25
#define DLGMOUSE_EXIT          26

/* The mouse options dialog: */
static SGOBJ mousedlg[] =
{
	{ SGBOX, 0, 0, 0,0, 45,28, NULL },
	{ SGTEXT, 0, 0, 16,1, 13,1, "Mouse options" },

	{ SGTEXT, 0, 0, 2,4, 30,1, "Mouse motion speed adjustment:" },
	{ SGBUTTON, 0, 0, 33,4, 11,1, "Customise" },

	{ SGBOX, 0, 0, 1,6, 21,10, NULL },
	{ SGTEXT, 0, 0, 2,7, 30,1, "Slow movement:" },
	{ SGRADIOBUT, 0, 0, 3,9,  11,1, "Very slow" },
	{ SGRADIOBUT, 0, 0, 3,10,  6,1, "Slow" },
	{ SGRADIOBUT, 0, 0, 3,11,  8,1, "Normal" },
	{ SGRADIOBUT, 0, 0, 3,12,  6,1, "Fast" },
	{ SGRADIOBUT, 0, 0, 3,13, 11,1, "Very fast" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,14, 8,1, "Custom" },

	{ SGBOX, 0, 0, 23,6, 21,10, NULL },
	{ SGTEXT, 0, 0, 24,7, 30,1, "Fast movement:" },
	{ SGRADIOBUT, 0, 0, 25,9,  11,1, "Very slow" },
	{ SGRADIOBUT, 0, 0, 25,10,  6,1, "Slow" },
	{ SGRADIOBUT, 0, 0, 25,11,  8,1, "Normal" },
	{ SGRADIOBUT, 0, 0, 25,12,  6,1, "Fast" },
	{ SGRADIOBUT, 0, 0, 25,13, 11,1, "Very fast" },
	{ SGRADIOBUT, SG_EXIT, 0, 25,14, 8,1, "Custom" },

	{ SGBOX, 0, 0, 1,17, 43,6, NULL },
	{ SGCHECKBOX, 0, 0, 2,18, 34,1, "Map control-click to right-click" },
	{ SGCHECKBOX, 0, 0, 2,19, 32,1, "Map scroll wheel to arrow keys" },
	{ SGCHECKBOX, 0, 0, 2,20, 21,1, "Enable auto-locking" },
	{ SGCHECKBOX, SG_EXIT, 0, 2,21, 25,1, "Use tablet if available" },
	{ SGBUTTON, 0, 0, 30,21, 11,1, "Configure" },

	{ SGBUTTON, SG_DEFAULT, 0, 12,25, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

#define DLGSPEED_USERAW 9
#define DLGSPEED_EXIT   10

static char lin_string[8];
static char exp_string[8];

/* The mouse speed adjustment dialog */
static SGOBJ speeddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 50,16, NULL },
	{ SGTEXT, 0, 0, 17,1, 13,1, "Mouse speed scale" },

	{ SGBOX, 0, 0, 1,4, 48,5, NULL },
	{ SGTEXT, 0, 0, 2,5, 32,1, "Linear adjustment:" },
	{ SGEDITFIELD, 0, 0, 26,5, 6,1, lin_string },
	{ SGTEXT, 0, 0, 34,5, 32,1, "(0.01 to 10.0)" },
	{ SGTEXT, 0, 0, 2,7, 38,1, "Exponential adjustment:" },
	{ SGEDITFIELD, 0, 0, 26,7, 6,1, exp_string },
	{ SGTEXT, 0, 0, 34,7, 32,1, "(0.50 to 1.00)" },

	{ SGCHECKBOX, 0, 0, 2,10, 42,1, "Use raw mouse movement data if available" },

	{ SGBUTTON, SG_DEFAULT, 0, 20,13, 10,1, "Done" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


static float read_float_string(char *s, float min, float max, int prec)
{
	int i;
	float result=0.0;
	
	for (i=0; i<8; i++) {
		if (*s>=(0+'0') && *s<=(9+'0')) {
			result *= 10.0;
			result += (float)(*s-'0');
			s++;
		} else {
			if (i==0 && *s!='.' && *s!=',') /* bad input, default to 1.0 */
				result=1.0;
			break;
		}
	}

	if (*s == '.' || *s == ',') {
		s++;
		for (i=1; i<=prec; i++) {
			if (*s>=(0+'0') && *s<=(9+'0')) {
				result += (float)(*s-'0')/pow(10.0, i);
				s++;
			} else {
				if (result==0.0) { /* bad input, default to 1.0 */
					result=1.0;
				}
				break;
			}
		}
		if (*s>=(0+'0') && *s<=(9+'0')) {
			if ((*s-'0')>=5) {
				result += 1.0/pow(10.0, i-1);
			}
		}
	}

	if (result<min)
		result=min;
	if (result>max)
		result=max;

	return result;
}

static void Dialog_SpeedDlg(void)
{
	int but;

	SDLGui_CenterDlg(speeddlg);

	/* Set up the dialog from actual values */
	snprintf(lin_string, sizeof(lin_string), "%#.3f", ConfigureParams.Mouse.fLinScale);	
	snprintf(exp_string, sizeof(exp_string), "%#.3f", ConfigureParams.Mouse.fExpScale);
	if (ConfigureParams.Mouse.bUseRawMotion) {
		speeddlg[DLGSPEED_USERAW].state |= SG_SELECTED;
	} else {
		speeddlg[DLGSPEED_USERAW].state &= ~SG_SELECTED;
	}

	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(speeddlg);
	}
	while (but != DLGSPEED_EXIT && but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);

	ConfigureParams.Mouse.fLinScale = read_float_string(lin_string, MOUSE_LIN_MIN, MOUSE_LIN_MAX, 3);
	ConfigureParams.Mouse.fExpScale = read_float_string(exp_string, MOUSE_EXP_MIN, MOUSE_EXP_MAX, 3);
	ConfigureParams.Mouse.bUseRawMotion = speeddlg[DLGSPEED_USERAW].state&SG_SELECTED ? true : false;
}

#define DLGTABLET_NONE   3
#define DLGTABLET_SD420  4
#define DLGTABLET_MM961  5
#define DLGTABLET_MM1201 6
#define DLGTABLET_EXIT   7

/* The tablet dialog */
static SGOBJ tabletdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 30,15, NULL },
	{ SGTEXT, 0, 0, 8,1, 14,1, "Tablet options" },
	
	{ SGBOX, 0, 0, 1,4, 28,6, NULL },
	{ SGRADIOBUT, 0, 0, 2,5, 11,1, "No tablet" },
	{ SGRADIOBUT, 0, 0, 2,6, 15,1, "WACOM SD-420E" },
	{ SGRADIOBUT, 0, 0, 2,7, 22,1, "SummaGraphics MM 961" },
	{ SGRADIOBUT, 0, 0, 2,8, 23,1, "SummaGraphics MM 1201" },
	
	{ SGBUTTON, SG_DEFAULT, 0, 10,12, 10,1, "Done" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

static void Dialog_TabletDlg(void)
{
	int but;
	TABLET_TYPE before, after;
	
	SDLGui_CenterDlg(tabletdlg);
	
	/* Set up the dialog from actual values */
	tabletdlg[DLGTABLET_NONE].state   &= ~SG_SELECTED;
	tabletdlg[DLGTABLET_SD420].state  &= ~SG_SELECTED;
	tabletdlg[DLGTABLET_MM961].state  &= ~SG_SELECTED;
	tabletdlg[DLGTABLET_MM1201].state &= ~SG_SELECTED;

	before = ConfigureParams.Tablet.nTabletType;
	switch (before) {
		case TABLET_NONE:
			tabletdlg[DLGTABLET_NONE].state |= SG_SELECTED;
			break;
		case TABLET_SD420E:
			tabletdlg[DLGTABLET_SD420].state |= SG_SELECTED;
			break;
		case TABLET_MM961:
			tabletdlg[DLGTABLET_MM961].state |= SG_SELECTED;
			break;
		case TABLET_MM1201:
			tabletdlg[DLGTABLET_MM1201].state |= SG_SELECTED;
			break;
		default:
			break;
	}
	
	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(tabletdlg);
	}
	while (but != DLGTABLET_EXIT && but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
	
	if (tabletdlg[DLGTABLET_NONE].state & SG_SELECTED) {
		after = TABLET_NONE;
	} else if (tabletdlg[DLGTABLET_SD420].state & SG_SELECTED) {
		after = TABLET_SD420E;
	} else if (tabletdlg[DLGTABLET_MM961].state & SG_SELECTED) {
		after = TABLET_MM961;
	} else if (tabletdlg[DLGTABLET_MM1201].state & SG_SELECTED) {
		after = TABLET_MM1201;
	}
	
	if (bTabletEnabled && (after != before)) {
		if (!DlgAlert_Query("Make sure tablet is disabled or uninstalled before changing model. "
							"Save changes anyway?")) {
			return;
		}
	}
	ConfigureParams.Tablet.nTabletType = after;
}


#define LIN_BASE     0.125
#define LIN_VERYFAST (10.0 * LIN_BASE)
#define LIN_FAST     (9.0 * LIN_BASE)
#define LIN_NORMAL   (8.0 * LIN_BASE)
#define LIN_SLOW     (7.0 * LIN_BASE)
#define LIN_VERYSLOW (6.0 * LIN_BASE)

#define EXP_BASE     0.125
#define EXP_VERYFAST (8.0 * EXP_BASE)
#define EXP_FAST     (7.0 * EXP_BASE)
#define EXP_NORMAL   (6.0 * EXP_BASE)
#define EXP_SLOW     (5.0 * EXP_BASE)
#define EXP_VERYSLOW (4.0 * EXP_BASE)

/* Set up the dialog from actual values */
static void DlgMouseSetup(void)
{
	int i;

	for (i = DLGMOUSE_LIN_VERYSLOW; i <= DLGMOUSE_LIN_CUSTOM; i++) {
		mousedlg[i].state &= ~SG_SELECTED;
	}
	for (i = DLGMOUSE_EXP_VERYSLOW; i <= DLGMOUSE_EXP_CUSTOM; i++) {
		mousedlg[i].state &= ~SG_SELECTED;
	}
	mousedlg[DLGMOUSE_CTRLCLCK].state &= ~SG_SELECTED;
	mousedlg[DLGMOUSE_MAPTOKEY].state &= ~SG_SELECTED;
	mousedlg[DLGMOUSE_TABLET].state   &= ~SG_SELECTED;
	mousedlg[DLGMOUSE_AUTOLOCK].state &= ~SG_SELECTED;

	if (ConfigureParams.Mouse.fLinScale == LIN_VERYSLOW) {
		mousedlg[DLGMOUSE_LIN_VERYSLOW].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fLinScale == LIN_SLOW) {
		mousedlg[DLGMOUSE_LIN_SLOW].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fLinScale == LIN_NORMAL) {
		mousedlg[DLGMOUSE_LIN_NORMAL].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fLinScale == LIN_FAST) {
		mousedlg[DLGMOUSE_LIN_FAST].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fLinScale == LIN_VERYFAST) {
		mousedlg[DLGMOUSE_LIN_VERYFAST].state |= SG_SELECTED;
	} else {
		mousedlg[DLGMOUSE_LIN_CUSTOM].state |= SG_SELECTED;
	}
	if (ConfigureParams.Mouse.fExpScale == EXP_VERYSLOW) {
		mousedlg[DLGMOUSE_EXP_VERYSLOW].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fExpScale == EXP_SLOW) {
		mousedlg[DLGMOUSE_EXP_SLOW].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fExpScale == EXP_NORMAL) {
		mousedlg[DLGMOUSE_EXP_NORMAL].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fExpScale == EXP_FAST) {
		mousedlg[DLGMOUSE_EXP_FAST].state |= SG_SELECTED;
	} else if (ConfigureParams.Mouse.fExpScale == EXP_VERYFAST) {
		mousedlg[DLGMOUSE_EXP_VERYFAST].state |= SG_SELECTED;
	} else {
		mousedlg[DLGMOUSE_EXP_CUSTOM].state |= SG_SELECTED;
	}
	if (ConfigureParams.Mouse.bEnableMacClick) {
		mousedlg[DLGMOUSE_CTRLCLCK].state |= SG_SELECTED;
	}
	if (ConfigureParams.Mouse.bEnableMapToKey) {
		mousedlg[DLGMOUSE_MAPTOKEY].state |= SG_SELECTED;
	}
	if (ConfigureParams.Mouse.bEnableAutoGrab) {
		mousedlg[DLGMOUSE_AUTOLOCK].state |= SG_SELECTED;
	}
	if (ConfigureParams.Tablet.nTabletType != TABLET_NONE) {
		mousedlg[DLGMOUSE_TABLET].state |= SG_SELECTED;
	}
}

/* Read values from dialog */
static void DlgMouseRead(void)
{
	if (mousedlg[DLGMOUSE_LIN_VERYSLOW].state&SG_SELECTED) {
		ConfigureParams.Mouse.fLinScale = LIN_VERYSLOW;
	} else if (mousedlg[DLGMOUSE_LIN_SLOW].state&SG_SELECTED) {
		ConfigureParams.Mouse.fLinScale = LIN_SLOW;
	} else if (mousedlg[DLGMOUSE_LIN_NORMAL].state&SG_SELECTED) {
		ConfigureParams.Mouse.fLinScale = LIN_NORMAL;
	} else if (mousedlg[DLGMOUSE_LIN_FAST].state&SG_SELECTED) {
		ConfigureParams.Mouse.fLinScale = LIN_FAST;
	} else if (mousedlg[DLGMOUSE_LIN_VERYFAST].state&SG_SELECTED) {
		ConfigureParams.Mouse.fLinScale = LIN_VERYFAST;
	}
	if (mousedlg[DLGMOUSE_EXP_VERYSLOW].state&SG_SELECTED) {
		ConfigureParams.Mouse.fExpScale = EXP_VERYSLOW;
	} else if (mousedlg[DLGMOUSE_EXP_SLOW].state&SG_SELECTED) {
		ConfigureParams.Mouse.fExpScale = EXP_SLOW;
	} else if (mousedlg[DLGMOUSE_EXP_NORMAL].state&SG_SELECTED) {
		ConfigureParams.Mouse.fExpScale = EXP_NORMAL;
	} else if (mousedlg[DLGMOUSE_EXP_FAST].state&SG_SELECTED) {
		ConfigureParams.Mouse.fExpScale = EXP_FAST;
	} else if (mousedlg[DLGMOUSE_EXP_VERYFAST].state&SG_SELECTED) {
		ConfigureParams.Mouse.fExpScale = EXP_VERYFAST;
	}
	ConfigureParams.Mouse.bEnableMacClick = mousedlg[DLGMOUSE_CTRLCLCK].state&SG_SELECTED ? true : false;
	ConfigureParams.Mouse.bEnableMapToKey = mousedlg[DLGMOUSE_MAPTOKEY].state&SG_SELECTED ? true : false;
	ConfigureParams.Mouse.bEnableAutoGrab = mousedlg[DLGMOUSE_AUTOLOCK].state&SG_SELECTED ? true : false;
	if (mousedlg[DLGMOUSE_TABLET].state&SG_SELECTED) {
		if (ConfigureParams.Tablet.nTabletType == TABLET_NONE) {
			ConfigureParams.Tablet.nTabletType = TABLET_MM1201;
		}
	} else {
		ConfigureParams.Tablet.nTabletType = TABLET_NONE;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process the Mouse options dialog.
 */
void Dialog_MouseDlg(void)
{
	int but;

	SDLGui_CenterDlg(mousedlg);

	/* Draw and process the dialog */
	do
	{
		DlgMouseSetup();
		
		but = SDLGui_DoDialog(mousedlg);
		
		DlgMouseRead();
		
		switch (but) {
			case DLGMOUSE_CUSTOMISE:
			case DLGMOUSE_LIN_CUSTOM:
			case DLGMOUSE_EXP_CUSTOM:
				Dialog_SpeedDlg();
				break;
			case DLGMOUSE_TABLETCONF:
				Dialog_TabletDlg();
				break;
				
			default:
				break;
		}
	}
	while (but != DLGMOUSE_EXIT && but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
}
