/*
  Previous - dlgMain.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The main dialog.
*/
const char DlgMain_fileid[] = "Previous dlgMain.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "sdlscreen.h"
#include "dimension.hpp"


#define MAINDLG_ABOUT    1
#define MAINDLG_SYSTEM   2
#define MAINDLG_ROM      3
#define MAINDLG_GRAPH    4
#define MAINDLG_NET      5
#define MAINDLG_BOOT     6
#define MAINDLG_SCSI     7
#define MAINDLG_MO       8
#define MAINDLG_FLOPPY   9
#define MAINDLG_KEYBD    10
#define MAINDLG_MOUSE    11
#define MAINDLG_SOUND    12
#define MAINDLG_PRINTER  13
#define MAINDLG_LOADCFG  14
#define MAINDLG_SAVECFG  15
#define MAINDLG_RESET    16
#define MAINDLG_SHOW     17
#define MAINDLG_OK       18
#define MAINDLG_QUIT     19
#define MAINDLG_CANCEL   20


/* The main dialog: */
static SGOBJ maindlg[] =
{
	{ SGBOX, 0, 0, 0,0, 50,19, NULL },
	{ SGTEXT, SG_EXIT, 0, 15,1, 20,1, "Previous - Main menu" },
	{ SGBUTTON, 0, 0, 2,4, 13,1, "System" },
	{ SGBUTTON, 0, 0, 2,6, 13,1, "ROM" },
	{ SGBUTTON, 0, 0, 2,8, 13,1, "Display" },
	{ SGBUTTON, 0, 0, 2,10, 13,1, "Network" },
	{ SGBUTTON, 0, 0, 17,4, 16,1, "Boot options" },
	{ SGBUTTON, 0, 0, 17,6, 16,1, "SCSI disks" },
	{ SGBUTTON, 0, 0, 17,8, 16,1, "MO disks" },
	{ SGBUTTON, 0, 0, 17,10, 16,1, "Floppy disks" },
	{ SGBUTTON, 0, 0, 35,4, 13,1, "Keyboard" },
	{ SGBUTTON, 0, 0, 35,6, 13,1, "Mouse" },
	{ SGBUTTON, 0, 0, 35,8, 13,1, "Sound" },
	{ SGBUTTON, 0, 0, 35,10, 13,1, "Printer" },
	{ SGBUTTON, 0, 0, 7,13, 16,1, "Load config." },
	{ SGBUTTON, 0, 0, 27,13, 16,1, "Save config." },
	{ SGCHECKBOX, 0, 0, 2,15, 15,1, "Reset machine" },
	{ SGCHECKBOX, 0, 0, 2,17, 17,1, "Show at startup" },
	{ SGBUTTON, SG_DEFAULT, 0, 21,15, 8,3, "OK" },
	{ SGBUTTON, 0, 0, 36,15, 10,1, "Quit" },
	{ SGBUTTON, SG_CANCEL, 0, 36,17, 10,1, "Cancel" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/**
 * This functions sets up the actual font and then displays the main dialog.
 */
int Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot)
{
	int retbut;
	bool bOldMouseVisibility;
	char *psNewCfg;

	*bReset = false;
	*bLoadedSnapshot = false;

	if (SDLGui_SetScreen(sdlscrn))
		return false;

	bOldMouseVisibility = Screen_ShowCursor(true);

	SDLGui_CenterDlg(maindlg);

	maindlg[MAINDLG_RESET].state &= ~SG_SELECTED;

	if(ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup) {
		maindlg[MAINDLG_SHOW].state |= SG_SELECTED;
	} else {
		maindlg[MAINDLG_SHOW].state &= ~SG_SELECTED;
	}

	do
	{
		retbut = SDLGui_DoDialog(maindlg);
		switch (retbut)
		{
		 case MAINDLG_ABOUT:
			Dialog_AboutDlg();
			break;
		 case MAINDLG_GRAPH:
			Dialog_GraphicsDlg();
			break;
		 case MAINDLG_NET:
			DlgEthernet_Main();
			break;
		 case MAINDLG_SCSI:
			DlgSCSI_Main();
			break;
		 case MAINDLG_ROM:
			DlgRom_Main();
			break;
		 case MAINDLG_MO:
			DlgOptical_Main();
			break;
		 case MAINDLG_FLOPPY:
			DlgFloppy_Main();
			break;
		 case MAINDLG_SYSTEM:
			Dialog_SystemDlg();
			break;
		 case MAINDLG_PRINTER:
			DlgPrinter_Main();
			break;
		 case MAINDLG_BOOT:
			DlgBoot_Main();
			break;
		 case MAINDLG_KEYBD:
			Dialog_KeyboardDlg();
			break;
		 case MAINDLG_MOUSE:
			Dialog_MouseDlg();
			break;
		 case MAINDLG_SOUND:
			DlgSound_Main();
			break;
		 case MAINDLG_LOADCFG:
			psNewCfg = SDLGui_FileSelect("Load configuration:", sConfigFileName, NULL, NULL, false);
			if (psNewCfg)
			{
				strcpy(sConfigFileName, psNewCfg);
				Configuration_Load(NULL);
				free(psNewCfg);
			}
			break;
		 case MAINDLG_SAVECFG:
			psNewCfg = SDLGui_FileSelect("Save configuration:", sConfigFileName, NULL, NULL, true);
			if (psNewCfg)
			{
				strcpy(sConfigFileName, psNewCfg);
				Configuration_Save();
				free(psNewCfg);
			}
			break;
		 case MAINDLG_SHOW:
			if (maindlg[MAINDLG_SHOW].state & SG_SELECTED)
				ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup = true;
			else
				ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup = false;
			break;
		 case MAINDLG_QUIT:
			bQuitProgram = true;
			break;
		}
	}
	while (retbut != MAINDLG_OK && retbut != MAINDLG_CANCEL && retbut != SDLGUI_QUIT
	        && retbut != SDLGUI_ERROR && !bQuitProgram);


	if (maindlg[MAINDLG_RESET].state & SG_SELECTED)
		*bReset = true;

	Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
	Screen_ShowCursor(bOldMouseVisibility);

	return (retbut == MAINDLG_OK);
}
