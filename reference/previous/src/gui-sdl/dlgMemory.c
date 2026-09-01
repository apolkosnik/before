/*
  Previous - dlgMemory.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgMemory_fileid[] = "Previous dlgMemory.c";

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "screen.h"


#define DLGMEM_8MB      4
#define DLGMEM_16MB     5
#define DLGMEM_32MB     6
#define DLGMEM_64MB     8
#define DLGMEM_128MB    9
#define DLGMEM_CUSTOM   7

#define DLGMEM_120NS    12
#define DLGMEM_100NS    13
#define DLGMEM_80NS     14
#define DLGMEM_60NS     15

#define DLGMEM_EXIT     16

char custom_memsize[16] = "Customise";

/* The memory dialog: */
static SGOBJ memorydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 41,18, NULL },
	{ SGTEXT, 0, 0, 14,1, 12,1, "Memory options" },
	{ SGBOX, 0, 0, 1,3, 19,10, NULL },
	{ SGTEXT, 0, 0, 2,4, 12,1, "Memory size" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,6, 6,1, "8 MB" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,7, 7,1, "16 MB" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,8, 7,1, "32 MB" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,11, 11,1, custom_memsize },
	{ SGRADIOBUT, SG_EXIT, 0, 3,9, 7,1, "64 MB" },
	{ SGRADIOBUT, SG_EXIT, 0, 3,10, 8,1, "128 MB" },
	{ SGBOX, 0, 0, 21,3, 19,10, NULL },
	{ SGTEXT, 0, 0, 22,4, 12,1, "Memory speed" },
	{ SGRADIOBUT, 0, 0, 23,6, 8,1, "120 ns" },
	{ SGRADIOBUT, 0, 0, 23,7, 8,1, "100 ns" },
	{ SGRADIOBUT, 0, 0, 23,8, 7,1, "80 ns" },
	{ SGRADIOBUT, 0, 0, 23,9, 7,1, "60 ns" },
	{ SGBUTTON, SG_DEFAULT, 0, 10,15, 23,1, "Back to system menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* Variable objects */
SGOBJ disable_128mb_option = { SGTEXT, 0, 0, 3,10, 8,1, " " };
SGOBJ enable_128mb_option = { SGRADIOBUT, SG_EXIT, 0, 3,10, 8,1, "128 MB" };
SGOBJ disable_64mb_option = { SGTEXT, 0, 0, 3,9, 7,1, " " };
SGOBJ enable_64mb_option = { SGRADIOBUT, SG_EXIT, 0, 3,9, 7,1, "64 MB" };

/* Default configuration constants */
int defsize[9][4] = {
	{ 4, 4, 0, 0},  /*  8 MB for monochrome non-turbo */
	{ 8, 0, 0, 0},  /*  8 MB for turbo and non-turbo color */
	{16, 0, 0, 0},  /* 16 MB for monochrome non-turbo */
	{ 8, 8, 0, 0},  /* 16 MB for turbo and non-turbo color */
	{16,16, 0, 0},  /* 32 MB for monochrome non-turbo */
	{ 8, 8, 8, 8},  /* 32 MB for turbo and non-turbo color */
	{16,16,16,16},  /* 64 MB for monochrome non-turbo */
	{32,32, 0, 0},  /* 64 MB for turbo */
	{32,32,32,32}   /* 128 MB for turbo */
};
int defsizecount=0; /* Compare defsizes up to this value */

static void Dialog_MemDlgDraw(void) {
	int i;
	int memsum, memsize;

	snprintf(custom_memsize, sizeof(custom_memsize), "Customise");

	for (i = DLGMEM_8MB; i <= DLGMEM_128MB; i++)
	{
		memorydlg[i].state &= ~SG_SELECTED;
	}

	for (i = DLGMEM_120NS; i <= DLGMEM_60NS; i++)
	{
		memorydlg[i].state &= ~SG_SELECTED;
	}

	/* Check memory configuration and find out if it is one of our default configurations */
	memsum = Configuration_CheckMemory(ConfigureParams.Memory.nMemoryBankSize);

	for (i = 0; i < defsizecount; i++) {
		if (memcmp(ConfigureParams.Memory.nMemoryBankSize, defsize[i],
		           sizeof(ConfigureParams.Memory.nMemoryBankSize))) {
			memsize = 0;
		} else {
			memsize = memsum;
			break;
		}
	}

	switch (memsize)
	{
		case 8:
			memorydlg[DLGMEM_8MB].state |= SG_SELECTED;
			break;
		case 16:
			memorydlg[DLGMEM_16MB].state |= SG_SELECTED;
			break;
		case 32:
			memorydlg[DLGMEM_32MB].state |= SG_SELECTED;
			break;
		case 64:
			memorydlg[DLGMEM_64MB].state |= SG_SELECTED;
			break;
		case 128:
			memorydlg[DLGMEM_128MB].state |= SG_SELECTED;
			break;
		default:
			memorydlg[DLGMEM_CUSTOM].state |= SG_SELECTED;
			snprintf(custom_memsize, sizeof(custom_memsize), "Custom (%i MB)", memsum);
			break;
	}

	switch (ConfigureParams.Memory.nMemorySpeed) {
		case MEMORY_120NS:
			memorydlg[DLGMEM_120NS].state |= SG_SELECTED;
			break;
		case MEMORY_100NS:
			memorydlg[DLGMEM_100NS].state |= SG_SELECTED;
			break;
		case MEMORY_80NS:
			memorydlg[DLGMEM_80NS].state |= SG_SELECTED;
			break;
		case MEMORY_60NS:
			memorydlg[DLGMEM_60NS].state |= SG_SELECTED;
			break;
			
		default:
			ConfigureParams.Memory.nMemorySpeed = MEMORY_100NS;
			memorydlg[DLGMEM_100NS].state |= SG_SELECTED;
			break;
	}
}

/**
 * Show and process the memory dialog.
 * @return  true if a memory snapshot has been loaded, false otherwise
 */
bool Dialog_MemDlg(void)
{
	int but;

	SDLGui_CenterDlg(memorydlg);

	/* Remove 64 and 128MB option if system is non-Turbo Slab,
	 * remove 128MB option if system is not Turbo */
	if (ConfigureParams.System.bTurbo) {
		memorydlg[DLGMEM_64MB] = enable_64mb_option;
		memorydlg[DLGMEM_128MB] = enable_128mb_option;
		defsizecount = 9;
	} else if (ConfigureParams.System.bColor ||
			   ConfigureParams.System.nMachineType == NEXT_STATION) {
		memorydlg[DLGMEM_64MB] = disable_64mb_option;
		memorydlg[DLGMEM_128MB] = disable_128mb_option;
		defsizecount = 6;
	} else {
		memorydlg[DLGMEM_64MB] = enable_64mb_option;
		memorydlg[DLGMEM_128MB] = disable_128mb_option;
		defsizecount = 8;
	}

	/* Display memory speed options depending on system type */
	if (ConfigureParams.System.bTurbo) {
		memorydlg[DLGMEM_120NS].txt = "60 ns";
		memorydlg[DLGMEM_100NS].txt = "70 ns";
		memorydlg[DLGMEM_80NS].txt = "80 ns";
		memorydlg[DLGMEM_60NS].txt = "100 ns";
	} else {
		memorydlg[DLGMEM_120NS].txt = "120 ns";
		memorydlg[DLGMEM_100NS].txt = "100 ns";
		memorydlg[DLGMEM_80NS].txt = "80 ns";
		memorydlg[DLGMEM_60NS].txt = "60 ns";
	}

	/* Draw dialog from actual values */

	Dialog_MemDlgDraw();

	do
	{
		but = SDLGui_DoDialog(memorydlg);
		
		switch (but)
		{
			case DLGMEM_8MB:
				if (ConfigureParams.System.bColor || ConfigureParams.System.bTurbo) {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[1],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				} else {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[0],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				}
				snprintf(custom_memsize, sizeof(custom_memsize), "Customise");
				break;
			case DLGMEM_16MB:
				if (ConfigureParams.System.bColor || ConfigureParams.System.bTurbo) {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[3],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				} else {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[2],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				}
				snprintf(custom_memsize, sizeof(custom_memsize), "Customise");
				break;
			case DLGMEM_32MB:
				if (ConfigureParams.System.bColor || ConfigureParams.System.bTurbo) {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[5],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				} else {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[4],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				}
				snprintf(custom_memsize, sizeof(custom_memsize), "Customise");
				break;
			case DLGMEM_64MB:
				if (ConfigureParams.System.bTurbo) {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[7],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				} else {
					memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[6],
						   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				}
				snprintf(custom_memsize, sizeof(custom_memsize), "Customise");
				break;
			case DLGMEM_128MB:
				memcpy(ConfigureParams.Memory.nMemoryBankSize, defsize[8],
					   sizeof(ConfigureParams.Memory.nMemoryBankSize));
				snprintf(custom_memsize, sizeof(custom_memsize), "Customise");
				break;
			case DLGMEM_CUSTOM:
				Dialog_MemAdvancedDlg(ConfigureParams.Memory.nMemoryBankSize);
				Dialog_MemDlgDraw();
				break;
		}
	}
	while (but != DLGMEM_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram );

	/* Read new values from dialog: */

	if (memorydlg[DLGMEM_120NS].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySpeed = MEMORY_120NS;
	else if (memorydlg[DLGMEM_100NS].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySpeed = MEMORY_100NS;
	else if (memorydlg[DLGMEM_80NS].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySpeed = MEMORY_80NS;
	else
		ConfigureParams.Memory.nMemorySpeed = MEMORY_60NS;

	return false;
}
