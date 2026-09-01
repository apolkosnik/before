/*
  Previous - dlgNFS.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgNFS_fileid[] = "Previous dlgNFS.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "str.h"


#define NFSDLG_OFFSET      2
#define NFSDLG_INTERVAL    5

#define NFSDLG_NAME        2
#define NFSDLG_SELECT      3
#define NFSDLG_PATH        4

#define TO_BUTTON(x)       (((x)-(NFSDLG_OFFSET))%(NFSDLG_INTERVAL))
#define TO_SHARE(x)        (((x)-(NFSDLG_OFFSET))/(NFSDLG_INTERVAL))
#define FROM_BUTTON(x,y)   (((x)*(NFSDLG_INTERVAL))+(NFSDLG_OFFSET)+(y))

#define NFSDLG_EXIT        23


/* The NFS dialog: */
static SGOBJ nfsdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 54,33, NULL },
	{ SGTEXT, 0, 0, 16,1, 22,1, "NFS shared directories" },

	{ SGBOX, 0, 0, 2,3, 50,5, NULL },
	{ SGTEXT, 0, 0, 3,4, 12,1, "NFS Share 0:" },
	{ SGTEXT, 0, 0, 16,4, 3,1, "nfs" },
	{ SGBUTTON, 0, 0, 43,4, 8,1, "Select" },
	{ SGTEXT, 0, 0, 3,6, 48,1, NULL },
	
	{ SGBOX, 0, 0, 2,9, 50,5, NULL },
	{ SGTEXT, 0, 0, 3,10, 12,1, "NFS Share 1:" },
	{ SGEDITFIELD, 0, 0, 16,10, 23,1, NULL },
	{ SGBUTTON, 0, 0, 43,10, 8,1, "Select" },
	{ SGTEXT, 0, 0, 3,12, 48,1, NULL },
	
	{ SGBOX, 0, 0, 2,15, 50,5, NULL },
	{ SGTEXT, 0, 0, 3,16, 12,1, "NFS Share 2:" },
	{ SGEDITFIELD, 0, 0, 16,16, 23,1, NULL },
	{ SGBUTTON, 0, 0, 43,16, 8,1, "Select" },
	{ SGTEXT, 0, 0, 3,18, 48,1, NULL },

	{ SGBOX, 0, 0, 2,21, 50,5, NULL },
	{ SGTEXT, 0, 0, 3,22, 12,1, "NFS Share 3:" },
	{ SGEDITFIELD, 0, 0, 16,22, 23,1, NULL },
	{ SGBUTTON, 0, 0, 43,22, 8,1, "Select" },
	{ SGTEXT, 0, 0, 3,24, 48,1, NULL },

	{ SGTEXT, 0, 0, 5,27, 44,1, "Note: The first NFS share cannot be renamed." },

	{ SGBUTTON, SG_DEFAULT, 0, 16,30, 21,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/**
 * Show and process the NFS dialog.
 */
void DlgNFS(void)
{
	int but;
	int i;
	int share;
	char dlgnfs_path[EN_MAX_SHARES][64];
	char dlgnfs_name[EN_MAX_SHARES][24];

	SDLGui_CenterDlg(nfsdlg);

	/* NFS shared directories: */
	for (i = 0; i < EN_MAX_SHARES; i++) {
		File_ShrinkName(dlgnfs_path[i], ConfigureParams.Ethernet.nfs[i].szPathName,
						nfsdlg[FROM_BUTTON(i,NFSDLG_PATH)].w);
		nfsdlg[FROM_BUTTON(i,NFSDLG_PATH)].txt = dlgnfs_path[i];
		if (i) {
			Str_Copy(dlgnfs_name[i], ConfigureParams.Ethernet.nfs[i].szHostName, sizeof(dlgnfs_name[i]));
			nfsdlg[FROM_BUTTON(i,NFSDLG_NAME)].txt = dlgnfs_name[i];
		}
	}
	
	/* Draw and process the dialog */
	do {
		for (i = 1; i < EN_MAX_SHARES; i++) {
			if (ConfigureParams.Ethernet.nfs[i].szPathName[0] == '\0') {
				nfsdlg[FROM_BUTTON(i, NFSDLG_SELECT)].txt = "Select";
			} else {
				nfsdlg[FROM_BUTTON(i, NFSDLG_SELECT)].txt = "Remove";
			}
		}
		but = SDLGui_DoDialog(nfsdlg);
		
		if (but >= NFSDLG_OFFSET && but < NFSDLG_INTERVAL * EN_MAX_SHARES + NFSDLG_OFFSET) {
			share = TO_SHARE(but);
			
			switch (TO_BUTTON(but)) {
				case NFSDLG_SELECT:
					if (ConfigureParams.Ethernet.nfs[share].szPathName[0] == '\0' || share == 0) {
						SDLGui_DirConfSelect(dlgnfs_path[share],
											 ConfigureParams.Ethernet.nfs[share].szPathName,
											 nfsdlg[FROM_BUTTON(share,NFSDLG_PATH)].w);
						if (ConfigureParams.Ethernet.nfs[share].szPathName[0] != '\0' &&
							dlgnfs_name[share][0] == '\0' && share != 0) {
							snprintf(dlgnfs_name[share], sizeof(dlgnfs_name[share]), "nfs%d", share);
						}
					} else {
						ConfigureParams.Ethernet.nfs[share].szPathName[0] = '\0';
						dlgnfs_path[share][0] = '\0';
					}
					break;
					
				default:
					break;
			}
		}
	}
	while (but != NFSDLG_EXIT && but != SDLGUI_QUIT
			&& but != SDLGUI_ERROR && !bQuitProgram);
	
	/* Read values from dialog */
	for (i = 1; i < EN_MAX_SHARES; i++) {
		Str_Copy(ConfigureParams.Ethernet.nfs[i].szHostName, dlgnfs_name[i], 
				 sizeof(ConfigureParams.Ethernet.nfs[i].szHostName));
	}
}
