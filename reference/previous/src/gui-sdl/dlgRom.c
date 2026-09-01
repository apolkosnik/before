/*
  Previous - dlgRom.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgRom_fileid[] = "Previous dlgRom.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "paths.h"


#define DLGROM_ROM030_DEFAULT     4
#define DLGROM_ROM030_BROWSE      5
#define DLGROM_ROM030_NAME        6

#define DLGROM_ROM040_DEFAULT     9
#define DLGROM_ROM040_BROWSE     10
#define DLGROM_ROM040_NAME       11

#define DLGROM_ROMTURBO_DEFAULT  14
#define DLGROM_ROMTURBO_BROWSE   15
#define DLGROM_ROMTURBO_NAME     16

#define DLGROM_EXIT              18


/* The ROM dialog: */
static SGOBJ romdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 63,27, NULL },
	{ SGTEXT, 0, 0, 27,1, 9,1, "ROM setup" },

	{ SGBOX, 0, 0, 2,3, 59,5, NULL },
	{ SGTEXT, 0, 0, 3,4, 28,1, "ROM for 68030 based systems:" },
	{ SGBUTTON, 0, 0, 40,4, 9,1, "Default" },
	{ SGBUTTON, 0, 0, 51,4, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,6, 56,1, NULL },
	
	{ SGBOX, 0, 0, 2,9, 59,5, NULL },
	{ SGTEXT, 0, 0, 3,10, 28,1, "ROM for 68040 based systems:" },
	{ SGBUTTON, 0, 0, 40,10, 9,1, "Default" },
	{ SGBUTTON, 0, 0, 51,10, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,12, 56,1, NULL },
	
	{ SGBOX, 0, 0, 2,15, 59,5, NULL },
	{ SGTEXT, 0, 0, 3,16, 28,1, "ROM for 68040 Turbo systems:" },
	{ SGBUTTON, 0, 0, 40,16, 9,1, "Default" },
	{ SGBUTTON, 0, 0, 51,16, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,18, 56,1, NULL },
	
	{ SGTEXT, 0, 0, 8,21, 47,1, "A reset is needed after changing these options." },
	{ SGBUTTON, SG_DEFAULT, 0, 21,24, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Show and process the ROM dialog.
 */
void DlgRom_Main(void)
{
	char szDlgRom030Name[57];
	char szDlgRom040Name[57];
	char szDlgRomTurboName[57];
	int but;

	SDLGui_CenterDlg(romdlg);

	File_ShrinkName(szDlgRom030Name, ConfigureParams.Rom.szRom030FileName, sizeof(szDlgRom030Name)-1);
	romdlg[DLGROM_ROM030_NAME].txt = szDlgRom030Name;

	File_ShrinkName(szDlgRom040Name, ConfigureParams.Rom.szRom040FileName, sizeof(szDlgRom040Name)-1);
	romdlg[DLGROM_ROM040_NAME].txt = szDlgRom040Name;
	
	File_ShrinkName(szDlgRomTurboName, ConfigureParams.Rom.szRomTurboFileName, sizeof(szDlgRomTurboName)-1);
	romdlg[DLGROM_ROMTURBO_NAME].txt = szDlgRomTurboName;

	do
	{
		but = SDLGui_DoDialog(romdlg);
		switch (but)
		{
			case DLGROM_ROM030_DEFAULT:
				File_MakePathBuf(ConfigureParams.Rom.szRom030FileName,
				                 sizeof(ConfigureParams.Rom.szRom030FileName),
				                 Paths_GetDataDir(), "Rev_1.0_v41", "BIN");
				File_ShrinkName(szDlgRom030Name, ConfigureParams.Rom.szRom030FileName, sizeof(szDlgRom030Name)-1);
				break;
				
			case DLGROM_ROM030_BROWSE:
				/* Show and process the file selection dlg */
				SDLGui_FileConfSelect(szDlgRom030Name,
				                      ConfigureParams.Rom.szRom030FileName,
				                      sizeof(szDlgRom030Name)-1,
				                      NULL, false);
				break;
				
			case DLGROM_ROM040_DEFAULT:
				File_MakePathBuf(ConfigureParams.Rom.szRom040FileName,
				                 sizeof(ConfigureParams.Rom.szRom040FileName),
				                 Paths_GetDataDir(), "Rev_2.5_v66", "BIN");
				File_ShrinkName(szDlgRom040Name, ConfigureParams.Rom.szRom040FileName, sizeof(szDlgRom040Name)-1);
				break;
				
			case DLGROM_ROM040_BROWSE:
				/* Show and process the file selection dlg */
				SDLGui_FileConfSelect(szDlgRom040Name,
				                      ConfigureParams.Rom.szRom040FileName,
				                      sizeof(szDlgRom040Name)-1,
				                      NULL, false);
				break;
				
			case DLGROM_ROMTURBO_DEFAULT:
				File_MakePathBuf(ConfigureParams.Rom.szRomTurboFileName,
				                 sizeof(ConfigureParams.Rom.szRomTurboFileName),
				                 Paths_GetDataDir(), "Rev_3.3_v74", "BIN");
				File_ShrinkName(szDlgRomTurboName, ConfigureParams.Rom.szRomTurboFileName, sizeof(szDlgRomTurboName)-1);
				break;
				
			case DLGROM_ROMTURBO_BROWSE:
				/* Show and process the file selection dlg */
				SDLGui_FileConfSelect(szDlgRomTurboName,
				                      ConfigureParams.Rom.szRomTurboFileName,
				                      sizeof(szDlgRomTurboName)-1,
				                      NULL, false);
				break;

		}
	}
	while (but != DLGROM_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);
}
