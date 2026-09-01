/*
  Previous - dlgGraphics.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgGraphics_fileid[] = "Previous dlgGraphics.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "dimension.hpp"


#define GDLG_BOARD0         5
#define GDLG_BOARD1         7
#define GDLG_BOARD2         9
#define GDLG_MONOCHROME     12
#define GDLG_COLOR          13
#define GDLG_ALL            14
#define GDLG_DISPLAY        17
#define GDLG_EXIT           19

static SGOBJ graphicsdlg[] =
{
	{ SGBOX,      0,       0,  0, 0, 56,24, NULL },
	{ SGTEXT,     0,       0, 20, 1, 16, 1, "Graphics options" },
	
	{ SGBOX,      0,       0,  2, 3, 32, 9, NULL },
	{ SGTEXT,     0,       0,  3, 4, 21, 1, "NeXTdimension boards:" },
	{ SGTEXT,     0,       0,  4, 6, 16, 1, "Board at slot 2:" },
	{ SGBUTTON,   0,       0, 22, 6, 10, 1, "Add" },
	{ SGTEXT,     0,       0,  4, 8, 16, 1, "Board at slot 4:" },
	{ SGBUTTON,   0,       0, 22, 8, 10, 1, "Add" },
	{ SGTEXT,     0,       0,  4,10, 16, 1, "Board at slot 6:" },
	{ SGBUTTON,   0,       0, 22,10, 10, 1, "Add" },
	
	{ SGBOX,      0,       0, 35, 3, 19, 9, NULL },
	{ SGTEXT,     0,       0, 36, 4, 13, 1, "Show display:" },
	{ SGRADIOBUT, SG_EXIT, 0, 37, 6, 12, 1, "Monochrome" },
	{ SGRADIOBUT, SG_EXIT, 0, 37, 8,  7, 1, "Color" },
	{ SGRADIOBUT, SG_EXIT, 0, 37,10,  5, 1, "All" },

	{ SGBOX,      0,       0,  2,13, 52, 3, NULL },
	{ SGTEXT,     0,       0,  3,14, 13, 1, "Main display:" },
	{ SGCHECKBOX, SG_EXIT, 0, 18,14, 26, 1, "Console on NeXTdimension" },

	{ SGTEXT,     0,       0,  2,18, 52, 1, "Note: NeXTdimension does not work with NeXTstations." },
	
	{ SGBUTTON, SG_DEFAULT,0, 17,21, 21, 1, "Back to main menu" },

	{ SGSTOP,     0,       0,  0, 0,  0, 0, NULL }
};


#define SDLG_SLOT2  2
#define SDLG_SLOT4  3
#define SDLG_SLOT6  4
#define SDLG_SELECT 5

static SGOBJ slotdlg[] =
{
	{ SGBOX,      0, 0, 0,  0, 20,12, NULL },
	{ SGTEXT,     0, 0, 4,  1, 12, 1, "Select slot:" },
	
	{ SGRADIOBUT, 0, 0, 6,  3,  8, 1, "Slot 2" },
	{ SGRADIOBUT, 0, 0, 6,  5,  8, 1, "Slot 4" },
	{ SGRADIOBUT, 0, 0, 6,  7,  8, 1, "Slot 6" },
	
	{ SGBUTTON, SG_DEFAULT, 0, 5,10, 10,1, "Select" },
	
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


#define MONDLG_SLOT0  2
#define MONDLG_SLOT2  3
#define MONDLG_SLOT4  4
#define MONDLG_SLOT6  5
#define MONDLG_NONE   6

static SGOBJ mondlg[] =
{
	{ SGBOX,    0, 0, 0,  0, 30,16, NULL },
	{ SGTEXT,   0, 0, 8,  1, 14, 1, "Select monitor" },
	
	{ SGBUTTON, 0, 0, 2,  4, 26, 1, "NeXT Main CPU (Slot 0)" },
	{ SGBUTTON, 0, 0, 2,  6, 26, 1, "NeXTdimension (Slot 2)" },
	{ SGBUTTON, 0, 0, 2,  8, 26, 1, "NeXTdimension (Slot 4)" },
	{ SGBUTTON, 0, 0, 2, 10, 26, 1, "NeXTdimension (Slot 6)" },
	{ SGBUTTON, 0, 0, 2, 13, 26, 1, "None" },
	
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


#define ALLDLG_MULTI  2
#define ALLDLG_GROUP  3
#define ALLDLG_GRID   5
#define ALLDLG_SELECT 21

static SGOBJ alldlg[] =
{
	{ SGBOX,      0, 0, 0,  0, 39,30, NULL },
	{ SGTEXT,     0, 0, 2,  1, 26, 1, "Present multiple monitors:" },
	
	{ SGRADIOBUT, SG_EXIT, 0, 4,  3, 27, 1, "Each in a separate window" },
	{ SGRADIOBUT, SG_EXIT, 0, 4,  5, 28, 1, "Grouped in a single window" },
	
	{ SGTEXT,     0, 0, 2,  8, 26, 1, "Monitor group arrangement:" },
	
	{ SGBUTTON,   0, 0, 4, 10,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 12,10,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 20,10,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 28,10,  7, 3, NULL },

	{ SGBUTTON,   0, 0, 4, 14,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 12,14,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 20,14,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 28,14,  7, 3, NULL },

	{ SGBUTTON,   0, 0, 4, 18,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 12,18,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 20,18,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 28,18,  7, 3, NULL },

	{ SGBUTTON,   0, 0, 4, 22,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 12,22,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 20,22,  7, 3, NULL },
	{ SGBUTTON,   0, 0, 28,22,  7, 3, NULL },

	{ SGBUTTON, SG_DEFAULT, 0, 15,27, 10,1, "Select" },
	
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Monitor select" dialog.
 */
static int Dialog_MonitorSelect(void)
{
	SDLGui_CenterDlg(mondlg);
	
	/* Draw and process the dialog: */
	
	switch (SDLGui_DoDialog(mondlg)) {
		case MONDLG_SLOT0: return 0;
		case MONDLG_SLOT2: return 1;
		case MONDLG_SLOT4: return 2;
		case MONDLG_SLOT6: return 3;
		default: return -1;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Monitor mode select" dialog.
 */
static char* MonitorNum(int num) {
	switch (num) {
		case 0:  return "0";
		case 1:  return "1";
		case 2:  return "2";
		case 3:  return "3";			
		default: return "";
	}
}

static void Dialog_MonitorModeSelect(void)
{
	int i, but, num, pos;
	
	SDLGui_CenterDlg(alldlg);
	
	/* Draw and process the dialog: */
	
	do {
		/* Set up dialog from actual values: */
		alldlg[ALLDLG_MULTI].state &= ~SG_SELECTED;
		alldlg[ALLDLG_GROUP].state &= ~SG_SELECTED;
		
		if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
			alldlg[ALLDLG_GROUP].state |= SG_SELECTED;
		} else {
			alldlg[ALLDLG_MULTI].state |= SG_SELECTED;
		}
		
		for (i = 0; i < 16; i++) {
			alldlg[ALLDLG_GRID + i].state &= ~SG_SELECTED;
			alldlg[ALLDLG_GRID + i].txt = "";
		}
		for (i = 0; i < NUM_MONITORS; i++) {
			pos = ConfigureParams.Screen.nGroupModePos[i];
			if (pos >= 0) {
				alldlg[ALLDLG_GRID + pos].state |= SG_SELECTED;
				alldlg[ALLDLG_GRID + pos].txt = MonitorNum(i);
			}
		}

		but = SDLGui_DoDialog(alldlg);
		
		if (but >= ALLDLG_GRID && but < (ALLDLG_GRID + 16)) {
			pos = but - ALLDLG_GRID;
			num = Configuration_GetScreenFromPos(pos);
			if (num >= 0) {
				ConfigureParams.Screen.nGroupModePos[num] = -1;
			}
			num = Dialog_MonitorSelect();
			ConfigureParams.Screen.nGroupModePos[num] = pos;
			ConfigureParams.Screen.nMode = SCREEN_GROUP;
		} else if (but == ALLDLG_GROUP) {
			ConfigureParams.Screen.nMode = SCREEN_GROUP;
		} else if (but == ALLDLG_MULTI) {
			ConfigureParams.Screen.nMode = SCREEN_ALL;
			ConfigureParams.Screen.nSingleModeSlot = 0;
		}
	}
	while (but != ALLDLG_SELECT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Slot select" dialog.
 */
static void Dialog_SlotSelect(int *slot)
{
	int but;
	
	SDLGui_CenterDlg(slotdlg);
	
	/* Set up dialog from actual values: */
	slotdlg[SDLG_SLOT2].state &= ~SG_SELECTED;
	slotdlg[SDLG_SLOT4].state &= ~SG_SELECTED;
	slotdlg[SDLG_SLOT6].state &= ~SG_SELECTED;
	
	switch (*slot) {
		case 4:
			slotdlg[SDLG_SLOT4].state |= SG_SELECTED;
			break;
		case 6:
			slotdlg[SDLG_SLOT6].state |= SG_SELECTED;
			break;
			
		default:
			slotdlg[SDLG_SLOT2].state |= SG_SELECTED;
			break;
	}
	
	/* Draw and process the dialog: */
	
	do {
		but = SDLGui_DoDialog(slotdlg);
	}
	while (but != SDLG_SELECT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
	
	if (slotdlg[SDLG_SLOT6].state&SG_SELECTED) {
		*slot = 6;
	} else if (slotdlg[SDLG_SLOT4].state&SG_SELECTED) {
		*slot = 4;
	} else {
		*slot = 2;
	}
}


/*-----------------------------------------------------------------------*/
/**
  * Show and process the "Graphics" dialog.
  */

#define GDLG_SIZE_NOTE "NOTE: Console on NeXTdimension may not show up if NeXTdimension has more than 32 MB of memory."

void Dialog_GraphicsCheckConsole(int slot)
{
	int *membank = ConfigureParams.Dimension.board[ND_NUM(slot)].nMemoryBankSize;
	
	if (ConfigureParams.Dimension.nConsoleSlot == slot) {
		if (ConfigureParams.Dimension.board[ND_NUM(slot)].bEnabled) {
			if (Configuration_CheckDimensionMemory(membank) > 32) {
				DlgAlert_Notice(GDLG_SIZE_NOTE);
			}
		}
	}
}

void Dialog_GraphicsDlg(void)
{
	int but;
	char colordisplay[16];
	char maindisplay[64];

	SDLGui_CenterDlg(graphicsdlg);

	/* Set up dialog from actual values: */
	
	if (ConfigureParams.Dimension.nConsoleSlot > 0) {
		graphicsdlg[GDLG_DISPLAY].state |= SG_SELECTED;
	} else {
		graphicsdlg[GDLG_DISPLAY].state &= ~SG_SELECTED;
	}

	graphicsdlg[GDLG_COLOR].state      &= ~SG_SELECTED;
	graphicsdlg[GDLG_MONOCHROME].state &= ~SG_SELECTED;
	graphicsdlg[GDLG_ALL].state        &= ~SG_SELECTED;
	
	if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) {
		if (ConfigureParams.Screen.nSingleModeSlot > 0) {
			graphicsdlg[GDLG_COLOR].state |= SG_SELECTED;
		} else {
			graphicsdlg[GDLG_MONOCHROME].state |= SG_SELECTED;
		}
	} else {
		graphicsdlg[GDLG_ALL].state |= SG_SELECTED;
	}
	
	/* Draw and process the dialog: */

	do {
		if (ConfigureParams.Dimension.board[0].bEnabled) {
			graphicsdlg[GDLG_BOARD0].txt = "Remove";
		} else {
			graphicsdlg[GDLG_BOARD0].txt = "Add";
		}
		
		if (ConfigureParams.Dimension.board[1].bEnabled) {
			graphicsdlg[GDLG_BOARD1].txt = "Remove";
		} else {
			graphicsdlg[GDLG_BOARD1].txt = "Add";
		}
		
		if (ConfigureParams.Dimension.board[2].bEnabled) {
			graphicsdlg[GDLG_BOARD2].txt = "Remove";
		} else {
			graphicsdlg[GDLG_BOARD2].txt = "Add";
		}
		
		if (ConfigureParams.Dimension.nConsoleSlot > 0) {
			snprintf(maindisplay, sizeof(maindisplay), "Console on NeXTdimension (Slot %i)", ConfigureParams.Dimension.nConsoleSlot);
			graphicsdlg[GDLG_DISPLAY].txt = maindisplay;
		} else {
			graphicsdlg[GDLG_DISPLAY].txt = "Console on NeXTdimension";
		}
		
		if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) { 
			if (ConfigureParams.Screen.nSingleModeSlot > 0) {
				snprintf(colordisplay, sizeof(colordisplay), "Color (Slot %i)", ConfigureParams.Screen.nSingleModeSlot);
				graphicsdlg[GDLG_COLOR].txt = colordisplay;
			} else {
				graphicsdlg[GDLG_COLOR].txt = "Color";
			}
			graphicsdlg[GDLG_ALL].txt = "All";
		} else {
			if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
				graphicsdlg[GDLG_ALL].txt = "All (Grouped)";
			} else {
				graphicsdlg[GDLG_ALL].txt = "All";
			}
			graphicsdlg[GDLG_COLOR].txt = "Color";
		}
		
		but = SDLGui_DoDialog(graphicsdlg);
		
		switch (but) {
			case GDLG_BOARD0:
				Dialog_DimensionDlg(0);
				Dialog_GraphicsCheckConsole(ND_SLOT(0));
				break;
				
			case GDLG_BOARD1:
				Dialog_DimensionDlg(1);
				Dialog_GraphicsCheckConsole(ND_SLOT(1));
				break;
				
			case GDLG_BOARD2:
				Dialog_DimensionDlg(2);
				Dialog_GraphicsCheckConsole(ND_SLOT(2));
				break;
				
			case GDLG_ALL:
				if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) {
					ConfigureParams.Screen.nMode = SCREEN_ALL;
				}
				Dialog_MonitorModeSelect();
				break;
				
			case GDLG_MONOCHROME:
				ConfigureParams.Screen.nMode = SCREEN_SINGLE;
				ConfigureParams.Screen.nSingleModeSlot = 0;
				break;

			case GDLG_COLOR:
				ConfigureParams.Screen.nMode = SCREEN_SINGLE;
				ConfigureParams.Screen.nSingleModeSlot = ConfigureParams.Dimension.nConsoleSlot;
				Dialog_SlotSelect(&ConfigureParams.Screen.nSingleModeSlot);
				break;

			case GDLG_DISPLAY:
				if (ConfigureParams.Dimension.nConsoleSlot > 0) {
					ConfigureParams.Dimension.nConsoleSlot = 0;
				} else {
					Dialog_SlotSelect(&ConfigureParams.Dimension.nConsoleSlot);
					Dialog_GraphicsCheckConsole(ConfigureParams.Dimension.nConsoleSlot);
				}
				break;

			default:
				break;
		}
	}
	while (but != GDLG_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
}
