/*
  Previous - dlgDimension.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgDimension_fileid[] = "Previous dlgDimension.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "dimension.hpp"


#define DLGND_CUSTOMISE     5
#define DLGND_MEMSIZE       12
#define DLGND_BROWSE        16
#define DLGND_NAME          17
#define DLGND_EXIT          18

/* Variable strings */
char dimension_memory[16] = "64 MB";
char dimension_slot[16]   = "2:";

static SGOBJ dimensiondlg[] =
{
	{ SGBOX,      0, 0, 0,  0, 58, 22, NULL },
	{ SGTEXT,     0, 0, 18, 1, 21, 1, "NeXTdimension options" },

	{ SGBOX,      0, 0, 2,  3, 54, 8, NULL },
	{ SGTEXT,     0, 0, 3,  4, 28, 1, "NeXTdimension board at slot " },
	{ SGTEXT,     0, 0, 31, 4, 2,  1, dimension_slot },
	{ SGBUTTON,   0, 0, 35, 4, 11, 1, "Customise" },

	{ SGTEXT, 0, 0, 4 ,6, 13,1, "CPU type:" },
	{ SGTEXT, 0, 0, 18,6, 13,1, "i860XR" },
	{ SGTEXT, 0, 0, 4 ,7, 13,1, "CPU clock:" },
	{ SGTEXT, 0, 0, 18,7, 13,1, "33 MHz" },
	{ SGTEXT, 0, 0, 4 ,8, 13,1, "Memory size:" },
	{ SGTEXT, 0, 0, 18,8, 13,1, dimension_memory },
	{ SGTEXT, 0, 0, 4 ,9, 13,1, "NBIC:" },
	{ SGTEXT, 0, 0, 18,9, 13,1, "present" },

	{ SGBOX, 0, 0, 2,12, 54,5, NULL },
	{ SGTEXT, 0, 0, 3,13, 22,1, "ROM for NeXTdimension:" },
	{ SGBUTTON, 0, 0, 27,13, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 4,15, 50,1, NULL },

	{ SGBUTTON, SG_DEFAULT, 0, 24,19, 10,1, "Done" },

	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* Function to set NeXTdimension memory options */
void Dialog_NDMemDlg(int *membank);

/* Function to print system overview */
static void print_nd_overview(int n) {
	snprintf(dimension_memory, sizeof(dimension_memory), "%i MB",
	         Configuration_CheckDimensionMemory(ConfigureParams.Dimension.board[n].nMemoryBankSize));
}


/*-----------------------------------------------------------------------*/
/**
  * Show and process the "System" dialog (specific to winUAE cpu).
  */
void Dialog_DimensionDlg(int n)
{
	int but;
	char dlgname_ndrom[64];

	if (ConfigureParams.Dimension.board[n].bEnabled) {
		ConfigureParams.Dimension.board[n].bEnabled = false;
		return;
	}
 
	SDLGui_CenterDlg(dimensiondlg);
 
	/* Set up dialog from actual values: */
	
	snprintf(dimension_slot, sizeof(dimension_slot), "%d:", ND_SLOT(n));

	File_ShrinkName(dlgname_ndrom, ConfigureParams.Dimension.board[n].szRomFileName,
						dimensiondlg[DLGND_NAME].w);
	dimensiondlg[DLGND_NAME].txt = dlgname_ndrom;

	/* System overview */
	print_nd_overview(n);

	/* Draw and process the dialog: */

	do {
		but = SDLGui_DoDialog(dimensiondlg);
		
		switch (but) {
			case DLGND_BROWSE:
				SDLGui_FileConfSelect(dlgname_ndrom,
									  ConfigureParams.Dimension.board[n].szRomFileName,
									  dimensiondlg[DLGND_NAME].w,
									  NULL, false);
				break;
				
			case DLGND_CUSTOMISE:
				Dialog_NDMemDlg(ConfigureParams.Dimension.board[n].nMemoryBankSize);
				print_nd_overview(n);
				break;
				
			default:
				break;
		}
	}
	while (but != DLGND_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);

	ConfigureParams.Dimension.board[n].bEnabled = true;
	ConfigureParams.System.bNBIC = true;
	Configuration_SetDefaultScreen(ND_SLOT(n));
}


#define DLGNDMEM_BANK0_4MB     4
#define DLGNDMEM_BANK0_16MB    5
#define DLGNDMEM_BANK0_NONE    6

#define DLGNDMEM_BANK1_4MB     9
#define DLGNDMEM_BANK1_16MB    10
#define DLGNDMEM_BANK1_NONE    11

#define DLGNDMEM_BANK2_4MB     14
#define DLGNDMEM_BANK2_16MB    15
#define DLGNDMEM_BANK2_NONE    16

#define DLGNDMEM_BANK3_4MB     19
#define DLGNDMEM_BANK3_16MB    20
#define DLGNDMEM_BANK3_NONE    21

#define DLGNDMEM_DEFAULT       23
#define DLGNDMEM_EXIT          24

static SGOBJ ndmemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 55,18, NULL },
	{ SGTEXT, 0, 0, 18,1, 19,1, "Custom memory setup" },
	
	{ SGBOX, 0, 0, 2,4, 12,7, NULL },
	{ SGTEXT, 0, 0, 3,5, 14,1, "Bank0" },
	{ SGRADIOBUT, 0, 0, 4,7, 6,1, "4 MB" },
	{ SGRADIOBUT, 0, 0, 4,8, 7,1, "16 MB" },
	{ SGRADIOBUT, 0, 0, 4,9, 6,1, "none" },
	
	{ SGBOX, 0, 0, 15,4, 12,7, NULL },
	{ SGTEXT, 0, 0, 16,5, 14,1, "Bank1" },
	{ SGRADIOBUT, 0, 0, 17,7, 6,1, "4 MB" },
	{ SGRADIOBUT, 0, 0, 17,8, 7,1, "16 MB" },
	{ SGRADIOBUT, 0, 0, 17,9, 6,1, "none" },
	
	{ SGBOX, 0, 0, 28,4, 12,7, NULL },
	{ SGTEXT, 0, 0, 29,5, 14,1, "Bank2" },
	{ SGRADIOBUT, 0, 0, 30,7, 6,1, "4 MB" },
	{ SGRADIOBUT, 0, 0, 30,8, 7,1, "16 MB" },
	{ SGRADIOBUT, 0, 0, 30,9, 6,1, "none" },
	
	{ SGBOX, 0, 0, 41,4, 12,7, NULL },
	{ SGTEXT, 0, 0, 42,5, 14,1, "Bank3" },
	{ SGRADIOBUT, 0, 0, 43,7, 6,1, "4 MB" },
	{ SGRADIOBUT, 0, 0, 43,8, 7,1, "16 MB" },
	{ SGRADIOBUT, 0, 0, 43,9, 6,1, "none" },
	
	{ SGTEXT, 0, 0, 5,12, 14,1, "Bank0 must contain at least 4 MB of memory." },
	
	{ SGBUTTON, 0, 0, 15,15, 10,1, "Defaults" },
	{ SGBUTTON, SG_DEFAULT, 0, 30,15, 10,1, "OK" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* Function to set up the dialog from the actual values */
static void Dialog_NDMemDraw(int *bank) {
	int i, j;
	for (i = 0; i<4; i++) {
		
		for (j = (DLGNDMEM_BANK0_4MB+(5*i)); j <= (DLGNDMEM_BANK0_NONE+(5*i)); j++)
		{
			ndmemdlg[j].state &= ~SG_SELECTED;
		}
		
		switch (bank[i])
		{
			case 0:
				ndmemdlg[DLGNDMEM_BANK0_NONE+(i*5)].state |= SG_SELECTED;
				break;
			case 4:
				ndmemdlg[DLGNDMEM_BANK0_4MB+(i*5)].state |= SG_SELECTED;
				break;
			case 16:
				ndmemdlg[DLGNDMEM_BANK0_16MB+(i*5)].state |= SG_SELECTED;
				break;
				
			default:
				break;
		}
	}
}


/* Function to read the actual values from the dialog */
static void Dialog_NDMemRead(int *bank) {
	int i;
	for (i = 0; i<4; i++) {
		if (ndmemdlg[(DLGNDMEM_BANK0_4MB)+(i*5)].state & SG_SELECTED)
			bank[i] = 4;
		else if (ndmemdlg[(DLGNDMEM_BANK0_16MB)+(i*5)].state & SG_SELECTED)
			bank[i] = 16;
		else
			bank[i] = 0;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Memory Advanced" dialog.
 */
void Dialog_NDMemDlg(int *membank) {
	
	int but;
	
	SDLGui_CenterDlg(ndmemdlg);
	
	/* Set up dialog from actual values: */
	Dialog_NDMemDraw(membank);
	
	/* Draw and process the dialog: */
	do
	{
		but = SDLGui_DoDialog(ndmemdlg);

		switch (but) {
			case DLGNDMEM_DEFAULT:
				membank[0] = membank[1] = membank[2] = membank[3] = 4;
				Configuration_CheckDimensionMemory(membank);
				Dialog_NDMemDraw(membank);
				break;
			default:
				break;
		}
	}
	while (but != DLGNDMEM_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
	
	/* Read values from dialog: */
	Dialog_NDMemRead(membank);
}
