/*
  Previous - dlgSystem.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgSystem_fileid[] = "Previous dlgSystem.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGSYS_CUBE030    4
#define DLGSYS_CUBE       5
#define DLGSYS_CUBETURBO  6
#define DLGSYS_SLAB       7
#define DLGSYS_SLABTURBO  8
#define DLGSYS_SLABCOLOR  9

#define DLGSYS_CUSTOMISE  10
#define DLGSYS_RESET      11

#define DLGSYS_EXIT       34

/* Variable strings */
char cpu_type[16]        = "68030";
char cpu_clock[16]       = "25 MHz";
char fpu_type[16]        = "68882";
char dsp_type[16]        = "56001";
char dsp_memory[16]      = "24 kB";
char main_memory[16]     = "8 MB";
char scsi_controller[16] = "NCR53C90";
char rtc_chip[16]        = "MC68HC68T1";
char nbic_present[16]    = "none";
char adb_used[16]        = "none";

/* Additional functions */
void print_system_overview(void);
void update_system_selection(void);
void get_default_values(void);


static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 58,27, NULL },
	{ SGTEXT, 0, 0, 22,1, 14,1, "System options" },
	
	{ SGBOX, 0, 0, 2,3, 25,17, NULL },
	{ SGTEXT, 0, 0, 3,4, 14,1, "Machine type" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,6, 15,1, "NeXT Computer" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,8, 10,1, "NeXTcube" },
	{ SGCHECKBOX, SG_EXIT, 0, 7,9, 7,1, "Turbo" },
	{ SGRADIOBUT, SG_EXIT, 0, 5,11, 13,1, "NeXTstation" },
	{ SGCHECKBOX, SG_EXIT, 0, 7,12, 7,1, "Turbo" },
	{ SGCHECKBOX, SG_EXIT, 0, 7,13, 7,1, "Color" },
	
	{ SGBUTTON, 0, 0, 5,16, 19,1, "Customise" },
	{ SGBUTTON, 0, 0, 5,18, 19,1, "System defaults" },

	{ SGTEXT, 0, 0, 30,4, 13,1, "System overview:" },
	{ SGTEXT, 0, 0, 30,6, 13,1, "CPU clock:" },
	{ SGTEXT, 0, 0, 44,6, 13,1, cpu_clock },
	{ SGTEXT, 0, 0, 30,7, 13,1, "CPU type:" },
	{ SGTEXT, 0, 0, 44,7, 13,1, cpu_type },
	{ SGTEXT, 0, 0, 30,8, 13,1, "FPU type:" },
	{ SGTEXT, 0, 0, 44,8, 13,1, fpu_type },
	{ SGTEXT, 0, 0, 30,9, 13,1, "DSP type:" },
	{ SGTEXT, 0, 0, 44,9, 13,1, dsp_type },
	{ SGTEXT, 0, 0, 30,10, 13,1, "DSP memory:" },
	{ SGTEXT, 0, 0, 44,10, 13,1, dsp_memory },
	{ SGTEXT, 0, 0, 30,11, 13,1, "Main memory:" },
	{ SGTEXT, 0, 0, 44,11, 13,1, main_memory },
	{ SGTEXT, 0, 0, 30,12, 13,1, "SCSI chip:" },
	{ SGTEXT, 0, 0, 44,12, 13,1, scsi_controller },
	{ SGTEXT, 0, 0, 30,13, 13,1, "RTC chip:" },
	{ SGTEXT, 0, 0, 44,13, 13,1, rtc_chip },
	{ SGTEXT, 0, 0, 30,14, 13,1, "NBIC:" },
	{ SGTEXT, 0, 0, 44,14, 13,1, nbic_present },
	{ SGTEXT, 0, 0, 30,15, 13,1, "ADB:" },
	{ SGTEXT, 0, 0, 44,15, 13,1, adb_used },

	{ SGTEXT, 0, 0, 4,21, 13,1, "Changing machine type resets all advanced options." },
	
	{ SGBUTTON, SG_DEFAULT, 0, 18,24, 21,1, "Back to main menu" },

	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* Function to print system overview */

void print_system_overview(void) {
	switch (ConfigureParams.System.nCpuLevel) {
		case 0:
			snprintf(cpu_type, sizeof(cpu_type), "68000"); break;
		case 1:
			snprintf(cpu_type, sizeof(cpu_type), "68010"); break;
		case 2:
			snprintf(cpu_type, sizeof(cpu_type), "68020"); break;
		case 3:
			snprintf(cpu_type, sizeof(cpu_type), "68030"); break;
		case 4:
			snprintf(cpu_type, sizeof(cpu_type), "68040"); break;
		case 5:
			snprintf(cpu_type, sizeof(cpu_type), "68060"); break;
		default: break;
	}
	
	if (ConfigureParams.System.bRealtime) snprintf(cpu_clock, sizeof(cpu_clock), "variable");
	else                                  snprintf(cpu_clock, sizeof(cpu_clock), "%i MHz", ConfigureParams.System.nCpuFreq);
	
	snprintf(main_memory, sizeof(main_memory), "%i MB", Configuration_CheckMemory(ConfigureParams.Memory.nMemoryBankSize));
	
	snprintf(dsp_memory, sizeof(dsp_memory), "%i kB", ConfigureParams.System.bDSPMemoryExpansion?96:24);
	
	switch (ConfigureParams.System.n_FPUType) {
		case FPU_NONE:
			snprintf(fpu_type, sizeof(fpu_type), "none"); break;
		case FPU_68881:
			snprintf(fpu_type, sizeof(fpu_type), "68881"); break;
		case FPU_68882:
			snprintf(fpu_type, sizeof(fpu_type), "68882"); break;
		case FPU_CPU:
			snprintf(fpu_type, sizeof(fpu_type), "68040"); break;
		default: break;
	}
	
	switch (ConfigureParams.System.nDSPType) {
		case DSP_TYPE_NONE:
			snprintf(dsp_type, sizeof(dsp_type), "none"); break;
		case DSP_TYPE_ACCURATE:
		case DSP_TYPE_EMU:
			snprintf(dsp_type, sizeof(dsp_type), "56001"); break;
		default: break;
	}
	
	switch (ConfigureParams.System.nSCSI) {
		case NCR53C90:
			snprintf(scsi_controller, sizeof(scsi_controller), "NCR53C90"); break;
		case NCR53C90A:
			snprintf(scsi_controller, sizeof(scsi_controller), "NCR53C90A"); break;
		default: break;
	}
	
	switch (ConfigureParams.System.nRTC) {
		case MC68HC68T1:
			snprintf(rtc_chip, sizeof(rtc_chip), "MC68HC68T1"); break;
		case MCCS1850:
			snprintf(rtc_chip, sizeof(rtc_chip), "MCCS1850"); break;
		default: break;
	}
	
	if (ConfigureParams.System.bNBIC) {
		snprintf(nbic_present, sizeof(nbic_present), "present");
	} else {
		snprintf(nbic_present, sizeof(nbic_present), "none");
	}
	
	if (ConfigureParams.System.bADB) {
		snprintf(adb_used, sizeof(adb_used), "used");
	} else if (ConfigureParams.System.bTurbo) {
		snprintf(adb_used, sizeof(adb_used), "unused");
	} else {
		snprintf(adb_used, sizeof(adb_used), "none");
	}
	
	update_system_selection();
}


/* Function to select and unselect system options */
void update_system_selection(void) {
	switch (ConfigureParams.System.nMachineType) {
		case NEXT_CUBE030:
			systemdlg[DLGSYS_CUBE030].state |= SG_SELECTED;
			systemdlg[DLGSYS_CUBE].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_CUBETURBO].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLAB].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLABCOLOR].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLABTURBO].state &= ~SG_SELECTED;
			break;
		case NEXT_CUBE040:
			systemdlg[DLGSYS_CUBE030].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_CUBE].state |= SG_SELECTED;
			if (ConfigureParams.System.bTurbo) {
				systemdlg[DLGSYS_CUBETURBO].state |= SG_SELECTED;
			} else {
				systemdlg[DLGSYS_CUBETURBO].state &= ~SG_SELECTED;
			}
			systemdlg[DLGSYS_SLAB].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLABCOLOR].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLABTURBO].state &= ~SG_SELECTED;
			break;
		case NEXT_STATION:
			systemdlg[DLGSYS_CUBE030].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_CUBE].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_CUBETURBO].state &= ~SG_SELECTED;
			systemdlg[DLGSYS_SLAB].state |= SG_SELECTED;
			if (ConfigureParams.System.bTurbo) {
				systemdlg[DLGSYS_SLABTURBO].state |= SG_SELECTED;
			} else {
				systemdlg[DLGSYS_SLABTURBO].state &= ~SG_SELECTED;
			}
			if (ConfigureParams.System.bColor) {
				systemdlg[DLGSYS_SLABCOLOR].state |= SG_SELECTED;
			} else {
				systemdlg[DLGSYS_SLABCOLOR].state &= ~SG_SELECTED;
			}
			break;
			
		default:
			break;
	}
}


/* Function to get default values for each system */
void get_default_values(void) {
	Configuration_SetSystemDefaults();
}


/*-----------------------------------------------------------------------*/
/**
  * Show and process the "System" dialog (specific to winUAE cpu).
  */
void Dialog_SystemDlg(void)
{
	int i;
	int but;
 
	SDLGui_CenterDlg(systemdlg);
 
	/* Set up dialog from actual values: */
	
	/* System type: */
	for (i = DLGSYS_CUBE030; i <= DLGSYS_SLAB; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nMachineType == NEXT_CUBE030)
		systemdlg[DLGSYS_CUBE030].state |= SG_SELECTED;
	else if (ConfigureParams.System.nMachineType == NEXT_STATION)
		systemdlg[DLGSYS_SLAB].state |= SG_SELECTED;
 
		 
	/* System overview */
	print_system_overview();
	 
		
	/* Draw and process the dialog: */

	do
	{
		but = SDLGui_DoDialog(systemdlg);
		switch (but) {
			case DLGSYS_CUBE030:
				ConfigureParams.System.nMachineType = NEXT_CUBE030;
				get_default_values();
				break;
				
			case DLGSYS_CUBE:
				if (ConfigureParams.System.nMachineType!=NEXT_CUBE040) {
					ConfigureParams.System.bTurbo = false;
				}
				ConfigureParams.System.nMachineType = NEXT_CUBE040;
				get_default_values();
				break;
				
			case DLGSYS_CUBETURBO:
				if (ConfigureParams.System.bTurbo &&
					ConfigureParams.System.nMachineType==NEXT_CUBE040) {
					ConfigureParams.System.bTurbo = false;
				} else {
					ConfigureParams.System.bTurbo = true;
				}
				ConfigureParams.System.nMachineType = NEXT_CUBE040;
				get_default_values();
				break;
				
			case DLGSYS_SLAB:
				if (ConfigureParams.System.nMachineType!=NEXT_STATION) {
					ConfigureParams.System.bTurbo = false;
				}
				ConfigureParams.System.nMachineType = NEXT_STATION;
				get_default_values();
				break;
				
			case DLGSYS_SLABCOLOR:
				if (ConfigureParams.System.nMachineType!=NEXT_STATION) {
					ConfigureParams.System.bTurbo = false;
				}
				if (ConfigureParams.System.bColor) {
					ConfigureParams.System.bColor = false;
				} else {
					ConfigureParams.System.bColor = true;
				}
				ConfigureParams.System.nMachineType = NEXT_STATION;
				get_default_values();
				break;
				
			case DLGSYS_SLABTURBO:
				if (ConfigureParams.System.bTurbo &&
					ConfigureParams.System.nMachineType==NEXT_STATION) {
					ConfigureParams.System.bTurbo = false;
				} else {
					ConfigureParams.System.bTurbo = true;
				}
				ConfigureParams.System.nMachineType = NEXT_STATION;
				get_default_values();
				break;
								
			case DLGSYS_CUSTOMISE:
				Dialog_AdvancedDlg();
				break;
				
			case DLGSYS_RESET:
				ConfigureParams.System.bRealtime = false;
				get_default_values();
				break;

			default:
				break;
		}
		
		print_system_overview();
	}
	while (but != DLGSYS_EXIT && but != SDLGUI_QUIT
		   && but != SDLGUI_ERROR && !bQuitProgram);
	
  
	/* Obsolete */
	ConfigureParams.System.bCompatibleCpu = 1;
	ConfigureParams.System.bCompatibleFPU = 1;
	ConfigureParams.System.bMMU = 1;
}
