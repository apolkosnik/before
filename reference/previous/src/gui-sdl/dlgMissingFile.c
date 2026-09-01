/*
  Previous - dlgMissingFile.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgMissingFile_fileid[] = "Previous dlgMissingFile.c";

#include "main.h"
#include "configuration.h"
#include "sdlscreen.h"
#include "screen.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"


/* Missing ROM dialog */
#define DLGMISROM_ALERT     1

#define DLGMISROM_BROWSE    4
#define DLGMISROM_DEFAULT   5
#define DLGMISROM_NAME      6

#define DLGMISROM_SELECT    7
#define DLGMISROM_REMOVE    8
#define DLGMISROM_QUIT      9


static SGOBJ missingromdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,15, NULL },
	{ SGTEXT, 0, 0, 2,1, 38,1, NULL },
	{ SGTEXT, 0, 0, 2,4, 43,1, "Please select a valid ROM file:" },
	
	{ SGBOX, 0, 0, 1,6, 50,4, NULL },
	{ SGBUTTON, 0, 0, 2,7, 8,1, "Browse" },
	{ SGBUTTON, 0, 0, 11,7, 9,1, "Default" },
	{ SGTEXT, 0, 0, 2,8, 46,1, NULL },
	
	{ SGBUTTON, SG_DEFAULT, 0, 4,12, 10,1, "Select" },
	{ SGBUTTON, 0, 0, 16,12, 10,1, "Remove" },
	{ SGBUTTON, 0, 0, 38,12, 10,1, "Quit" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* Missing disk dialog */
#define DLGMISDSK_ALERT     1

#define DLGMISDSK_DRIVE     4
#define DLGMISDSK_BROWSE    5
#define DLGMISDSK_PROTECT   6
#define DLGMISDSK_NAME      7

#define DLGMISDSK_SELECT    8
#define DLGMISDSK_REMOVE    9
#define DLGMISDSK_QUIT      10


static SGOBJ missingdiskdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,15, NULL },
	{ SGTEXT, 0, 0, 6,1, 38,1, NULL },
	{ SGTEXT, 0, 0, 2,4, 43,1, "Please remove or select a valid disk image:" },
	
	{ SGBOX, 0, 0, 1,6, 50,4, NULL },
	{ SGTEXT, 0, 0, 2,7, 15,1, NULL },
	{ SGBUTTON, 0, 0, 39,7, 10,1, "Browse" },
	{ SGTEXT, 0, 0, 15,7, 10,1, NULL },
	{ SGTEXT, 0, 0, 2,8, 46,1, NULL },
	
	{ SGBUTTON, SG_DEFAULT, 0, 4,12, 10,1, "Select" },
	{ SGBUTTON, 0, 0, 16,12, 10,1, "Remove" },
	{ SGBUTTON, 0, 0, 38,12, 10,1, "Quit" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* Missing direcory dialog */
#define DLGMISDIR_ALERT     1

#define DLGMISDIR_BROWSE    4
#define DLGMISDIR_DEFAULT   5
#define DLGMISDIR_NAME      6

#define DLGMISDIR_SELECT    7
#define DLGMISDIR_QUIT      8


static SGOBJ missingdirdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,15, NULL },
	{ SGTEXT, 0, 0, 2,1, 38,1, NULL },
	{ SGTEXT, 0, 0, 2,4, 43,1, "Please select a valid directory:" },
	
	{ SGBOX, 0, 0, 1,6, 50,4, NULL },
	{ SGBUTTON, 0, 0, 2,7, 8,1, "Browse" },
	{ SGBUTTON, 0, 0, 11,7, 9,1, "Default" },
	{ SGTEXT, 0, 0, 2,8, 46,1, NULL },
	
	{ SGBUTTON, SG_DEFAULT, 0, 4,12, 10,1, "Select" },
	{ SGBUTTON, 0, 0, 38,12, 10,1, "Quit" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Show and process the missing ROM dialog.
 */
void DlgMissing_Rom(const char* type, char *imgname, const char *defname, bool *enabled) {
	int but;
	
	char dlgname_missingrom[64];
	char missingrom_alert[64];
	
	bool bOldMouseVisibility;
	bOldMouseVisibility = Screen_ShowCursor(true);
	
	SDLGui_CenterDlg(missingromdlg);
	
	/* Set up dialog to actual values: */
	snprintf(missingrom_alert, sizeof(missingrom_alert), "%s: ROM file not found!", type);
	missingromdlg[DLGMISROM_ALERT].txt = missingrom_alert;
	
	File_ShrinkName(dlgname_missingrom, imgname, missingromdlg[DLGMISROM_NAME].w);
	
	missingromdlg[DLGMISROM_NAME].txt = dlgname_missingrom;
	
	if (*enabled) {
		missingromdlg[DLGMISROM_REMOVE].type = SGBUTTON;
		missingromdlg[DLGMISROM_REMOVE].txt = "Remove";
	} else {
		missingromdlg[DLGMISROM_REMOVE].type = SGTEXT;
		missingromdlg[DLGMISROM_REMOVE].txt = "";
	}
	
	
	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(missingromdlg);
		switch (but)
		{
				
			case DLGMISROM_BROWSE:
				SDLGui_FileConfSelect(dlgname_missingrom, imgname,
									  missingromdlg[DLGMISROM_NAME].w,
									  NULL, false);
				break;
			case DLGMISROM_DEFAULT:
				snprintf(imgname, FILENAME_MAX, "%s", defname);
				File_ShrinkName(dlgname_missingrom, imgname, missingromdlg[DLGMISROM_NAME].w);
				break;
			case DLGMISROM_REMOVE:
				*enabled = false;
				*imgname = '\0';
				break;
			case DLGMISROM_QUIT:
				bQuitProgram = true;
				break;
				
			default:
				break;
		}
	}
	while (but != DLGMISROM_SELECT && but != DLGMISROM_REMOVE &&
		   but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
	
	Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
	Screen_ShowCursor(bOldMouseVisibility);
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the missing disk dialog.
 */
void DlgMissing_Disk(const char* type, int num, char *imgname, bool *inserted, bool *wp)
{
	int but;
	
	char dlgname_missingdisk[64];
	char missingdisk_alert[64];
	char missingdisk_disk[64];
	
	bool bOldMouseVisibility;
	bOldMouseVisibility = Screen_ShowCursor(true);

	SDLGui_CenterDlg(missingdiskdlg);
	
	/* Set up dialog to actual values: */
	snprintf(missingdisk_alert, sizeof(missingdisk_alert), "%s drive %i: disk image not found!", type, num);
	missingdiskdlg[DLGMISDSK_ALERT].txt = missingdisk_alert;
	
	snprintf(missingdisk_disk, sizeof(missingdisk_disk), "%s %i:", type, num);
	missingdiskdlg[DLGMISDSK_DRIVE].txt = missingdisk_disk;
	
	File_ShrinkName(dlgname_missingdisk, imgname, missingdiskdlg[DLGMISDSK_NAME].w);
	
	missingdiskdlg[DLGMISDSK_NAME].txt = dlgname_missingdisk;
	
	
	/* Draw and process the dialog */
	do
	{
		if (*wp)
			missingdiskdlg[DLGMISDSK_PROTECT].txt = "read-only";
		else
			missingdiskdlg[DLGMISDSK_PROTECT].txt = "";

		but = SDLGui_DoDialog(missingdiskdlg);
		switch (but)
		{
			case DLGMISDSK_BROWSE:
				SDLGui_FileConfSelect(dlgname_missingdisk, imgname, missingdiskdlg[DLGMISDSK_NAME].w, wp, false);
				break;
			case DLGMISDSK_REMOVE:
				*inserted = false;
				*wp = false;
				*imgname = '\0';
				break;
			case DLGMISDSK_QUIT:
				bQuitProgram = true;
				break;
				
			default:
				break;
		}
	}
	while (but != DLGMISDSK_SELECT && but != DLGMISDSK_REMOVE &&
		   but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
	
	Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
	Screen_ShowCursor(bOldMouseVisibility);
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the missing disk dialog.
 */
void DlgMissing_Dir(const char* type, char *dirname, const char *defname)
{
	int but;
	
	char dlgname_missingdir[64];
	char missingdir_alert[64];
	char missingdir_dir[64];
	
	bool bOldMouseVisibility;
	bOldMouseVisibility = Screen_ShowCursor(true);
	
	SDLGui_CenterDlg(missingdirdlg);
	
	/* Set up dialog to actual values: */
	snprintf(missingdir_alert, sizeof(missingdir_alert), "%s directory not found!", type);
	missingdirdlg[DLGMISDIR_ALERT].txt = missingdir_alert;
	
	snprintf(missingdir_dir, sizeof(missingdir_dir), "%s directory:", type);
	missingdirdlg[DLGMISDIR_NAME].txt = missingdir_dir;
	
	File_ShrinkName(dlgname_missingdir, dirname, missingdirdlg[DLGMISDIR_NAME].w);
	
	missingdirdlg[DLGMISDIR_NAME].txt = dlgname_missingdir;
	
	
	/* Draw and process the dialog */
	do
	{		
		but = SDLGui_DoDialog(missingdirdlg);
		switch (but)
		{
			case DLGMISDIR_BROWSE:
				SDLGui_DirConfSelect(dlgname_missingdir, dirname, missingdirdlg[DLGMISDIR_NAME].w);
				break;
			case DLGMISDIR_DEFAULT:
				snprintf(dirname, FILENAME_MAX, "%s", defname);
				File_ShrinkName(dlgname_missingdir, dirname, missingdirdlg[DLGMISDIR_NAME].w);
				break;
			case DLGMISDIR_QUIT:
				bQuitProgram = true;
				break;

			default:
				break;
		}
	}
	while (but != DLGMISDIR_SELECT && 
		   but != SDLGUI_QUIT && but != SDLGUI_ERROR && !bQuitProgram);
	
	Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
	Screen_ShowCursor(bOldMouseVisibility);
}
