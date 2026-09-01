/*
  Previous - change.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles run-time configuration changes. We keep all our
  configuration details in a structure called 'ConfigureParams'.  Before
  doing he changes, a backup copy is done of this structure. When
  the changes are done, these are compared to see whether emulator
  needs to be rebooted
*/
const char Change_fileid[] = "Previous change.c";

#include <ctype.h>
#include "main.h"
#include "configuration.h"
#include "change.h"
#include "dialog.h"
#include "ioMem.h"
#include "m68000.h"
#include "reset.h"
#include "screen.h"
#include "statusbar.h"
#include "video.h"
#include "hatari-glue.h"
#include "scsi.h"
#include "mo.h"
#include "floppy.h"
#include "ethernet.h"
#include "snd.h"
#include "tablet.h"
#include "keymap.h"

#define DEBUG 1
#if DEBUG
#define Dprintf(...) printf(__VA_ARGS__)
#else
#define Dprintf(...)
#endif

/*-----------------------------------------------------------------------*/
/**
 * Check if user needs to be warned that changes will take place after reset.
 * Return true if wants to reset.
 */
bool Change_DoNeedReset(CNF_PARAMS *current, CNF_PARAMS *changed)
{
	int i, j;

	/* Did we change ROM file? */
	if (current->System.nMachineType == NEXT_CUBE030 && strcmp(current->Rom.szRom030FileName, changed->Rom.szRom030FileName)) {
		printf("rom030 reset\n");
		return true;
	}
	if (current->System.nMachineType == NEXT_CUBE040 || current->System.nMachineType == NEXT_STATION) {
		if (!current->System.bTurbo && strcmp(current->Rom.szRom040FileName, changed->Rom.szRom040FileName)) {
			printf("rom040 reset\n");
			return true;
		}
		if (current->System.bTurbo && strcmp(current->Rom.szRomTurboFileName, changed->Rom.szRomTurboFileName)) {
			printf("romturbo reset\n");
			return true;
		}
	}

	/* Did we change MAC address? */
	if (current->Rom.bUseCustomMac != changed->Rom.bUseCustomMac) {
		printf("mac reset\n");
		return true;
	}
	if (current->Rom.bUseCustomMac) {
		for (i = 0; i < 6; i++) {
			if (current->Rom.nRomCustomMac[i] != changed->Rom.nRomCustomMac[i]) {
				printf("mac reset\n");
				return true;
			}
		}
	}

	/* Did we change network interface? */
	if ((current->Ethernet.nHostInterface != changed->Ethernet.nHostInterface) ||
		(current->Ethernet.bNetworkTime != changed->Ethernet.bNetworkTime)) {
		printf("network interface reset\n");
		return true;
	}

	/* Did we change machine type? */
	if ((current->System.nMachineType != changed->System.nMachineType) ||
		(current->System.bColor != changed->System.bColor) || 
		(current->System.bTurbo != changed->System.bTurbo)) {
		printf("machine type reset\n");
		return true;
	}

	/* Did we change CPU type? */
	if ((current->System.nCpuLevel != changed->System.nCpuLevel) ||
		(current->System.nCpuFreq != changed->System.nCpuFreq)) {
		printf("cpu type reset\n");
		return true;
	}

	/* Did we change the realtime flag? */
	if (current->System.bRealtime != changed->System.bRealtime) {
		printf("realtime flag reset\n");
		return true;
	}

	/* Did we change FPU type? */
	if (current->System.n_FPUType != changed->System.n_FPUType) {
		printf("fpu type reset\n");
		return true;
	}

#if ENABLE_DSP_EMU
	/* Did we change DSP type or memory? */
	if ((current->System.nDSPType != changed->System.nDSPType) ||
		(current->System.bDSPMemoryExpansion != changed->System.bDSPMemoryExpansion)) {
		printf("dsp type reset\n");
		return true;
	}
#endif

	/* Did we change SCSI controller? */
	if (current->System.nSCSI != changed->System.nSCSI) {
		printf("scsi controller reset\n");
		return true;
	}

	/* Did we change RTC chip? */
	if (current->System.nRTC != changed->System.nRTC) {
		printf("rtc chip reset\n");
		return true;
	}

	/* Did we change NBIC emulation? */
	if (current->System.bNBIC != changed->System.bNBIC) {
		printf("nbic reset\n");
		return true;
	}
	
	/* Did we change ADB emulation? */
	if (current->System.bADB != changed->System.bADB) {
		printf("adb reset\n");
		return true;
	}

	/* Did we change memory size? */
	for (i = 0; i < 4; i++) {
		if (current->Memory.nMemoryBankSize[i] != changed->Memory.nMemoryBankSize[i]) {
			printf("memory size reset\n");
			return true;
		}
	}

	/* Did we change boot options? */
	if ((current->Boot.nBootDevice != changed->Boot.nBootDevice) ||
		(current->Boot.bEnableDRAMTest != changed->Boot.bEnableDRAMTest) ||
		(current->Boot.bEnablePot != changed->Boot.bEnablePot) ||
		(current->Boot.bEnableSoundTest != changed->Boot.bEnableSoundTest) ||
		(current->Boot.bEnableSCSITest != changed->Boot.bEnableSCSITest) ||
		(current->Boot.bLoopPot != changed->Boot.bLoopPot) ||
		(current->Boot.bVerbose != changed->Boot.bVerbose) ||
		(current->Boot.bExtendedPot != changed->Boot.bExtendedPot) ||
		(current->Boot.bVisible != changed->Boot.bVisible)) {
		printf("boot options reset\n");
		return true;
	}

	/* Did we change SCSI disk? */
	for (i = 0; i < ESP_MAX_DEVS; i++) {
		if (current->SCSI.target[i].nDeviceType != changed->SCSI.target[i].nDeviceType ||
			(current->SCSI.target[i].nDeviceType == SD_HARDDISK &&
			 (current->SCSI.target[i].bWriteProtected != changed->SCSI.target[i].bWriteProtected ||
			  strcmp(current->SCSI.target[i].szImageName, changed->SCSI.target[i].szImageName)))) {
				 printf("scsi disk reset\n");
				 return true;
			 }
	}
	if (current->SCSI.nWriteProtection != changed->SCSI.nWriteProtection) {
		printf("scsi disk reset\n");
		return true;
	}

	/* Did we change MO drive? */
	for (i = 0; i < MO_MAX_DRIVES; i++) {
		if (current->MO.drive[i].bDriveConnected != changed->MO.drive[i].bDriveConnected) {
			printf("mo drive reset\n");
			return true;
		}
	}

	/* Did we change floppy drive? */
	for (i = 0; i < FLP_MAX_DRIVES; i++) {
		if (current->Floppy.drive[i].bDriveConnected != changed->Floppy.drive[i].bDriveConnected) {
			printf("floppy drive reset\n");
			return true;
		}
	}

	/* Did we change printer? */
	if (current->Printer.bPrinterConnected != changed->Printer.bPrinterConnected) {
		printf("printer reset\n");
		return true;
	}

	/* Did we change NeXTdimension? */
	for (i = 0; i < ND_MAX_BOARDS; i++) {
		if (current->Dimension.board[i].bEnabled != changed->Dimension.board[i].bEnabled ||
			strcmp(current->Dimension.board[i].szRomFileName, changed->Dimension.board[i].szRomFileName)) {
			printf("dimension reset\n");
			return true;
		}
		for (j = 0; j < 4; j++) {
			if (current->Dimension.board[i].nMemoryBankSize[j] != changed->Dimension.board[i].nMemoryBankSize[j]) {
				printf("dimension memory size reset\n");
				return true;
			}
		}
	}
	if (current->Dimension.bI860Thread != changed->Dimension.bI860Thread ||
		current->Dimension.nConsoleSlot != changed->Dimension.nConsoleSlot) {
		printf("dimension display reset\n");
		return true;
	}

	/* Else no reset is required */
	printf("No Reset needed!\n");
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details back to configuration and perform reset.
 */
void Change_CopyChangedParamsToConfiguration(CNF_PARAMS *current, CNF_PARAMS *changed, bool bForceReset)
{
	bool NeedReset;
	bool bReInitKeymap = false;
	bool bReInitTablet = false;
	bool bReInitEnetEmu = false;
	bool bReInitSoundEmu = false;
	bool bScreenModeChange = false;

	Dprintf("Changes for:\n");
	/* Do we need to warn user that changes will only take effect after reset? */
	if (bForceReset)
		NeedReset = bForceReset;
	else
		NeedReset = Change_DoNeedReset(current, changed);

	/* Note: SCSI, MO and floppy disk insert/eject called from GUI */

	if (!NeedReset) {
		int i;

		/* Do we need to change Keymap configuration? */
		if (current->Mouse.bUseRawMotion != changed->Mouse.bUseRawMotion) {
			bReInitKeymap = true;
		}

		/* Do we need to change Tablet configuration? */
		if (current->Tablet.nTabletType != changed->Tablet.nTabletType) {
			bReInitTablet = true;
		}

		/* Do we need to change Ethernet configuration? */
		for (i = 0; i < EN_MAX_SHARES; i++) {
			if (current->Ethernet.bEthernetConnected != changed->Ethernet.bEthernetConnected ||
				strcmp(current->Ethernet.szInterfaceName, changed->Ethernet.szInterfaceName) ||
				strcmp(current->Ethernet.nfs[i].szHostName, changed->Ethernet.nfs[i].szHostName) ||
				strcmp(current->Ethernet.nfs[i].szPathName, changed->Ethernet.nfs[i].szPathName)) {
				bReInitEnetEmu = true;
				break;
			}
		}

		/* Do we need to change Sound configuration? */
		if (current->Sound.bEnableSound != changed->Sound.bEnableSound ||
			current->Sound.bEnableMicrophone != changed->Sound.bEnableMicrophone) {
			bReInitSoundEmu = true;
		}

		/* Do we need to change Screen configuration? */
		if (current->Screen.nMode != changed->Screen.nMode) {
			bScreenModeChange = true;
		} else if (current->Screen.nMode == SCREEN_GROUP) {
			for (i = 0; i < NUM_MONITORS; i++) {
				if (current->Screen.nGroupModePos[i] != changed->Screen.nGroupModePos[i]) {
					bScreenModeChange = true;
				}
			}
		}
	}

	/* Copy details to configuration,
	 * so it can be saved out or set on reset
	 */
	if (changed != &ConfigureParams)
	{
		ConfigureParams = *changed;
	}

	/* Copy details to global, if we reset copy them all */
	Configuration_Apply(NeedReset);

	/* Re-init Ethernet? */
	if (bReInitEnetEmu) {
		Dprintf("- Ethernet\n");
		Ethernet_Reset(false);
	}

	/* Re-init Sound? */
	if (bReInitSoundEmu) {
		Dprintf("- Sound\n");
		Sound_Reset();
	}

	/* Re-init Keymap? */
	if (bReInitKeymap) {
		Dprintf("- Keymap\n");
		Keymap_Init();
	}

	/* Re-init Tablet? */
	if (bReInitTablet) {
		Dprintf("- Tablet\n");
		Tablet_Reset();
	}

	/* Force things associated with screen change */
	if (bScreenModeChange)
	{
		Dprintf("- Screen\n");
		Screen_Reset();
	}

	/* Do we need to perform reset? */
	if (NeedReset)
	{
		/* Check if all necessary files exist */
		Dialog_CheckFiles();
		if (bQuitProgram)
			return;

		Dprintf("- Reset\n");
		Reset_Cold();
	}

	/* Go into/return from full screen if flagged */
	if (!bInFullScreen && ConfigureParams.Screen.bFullScreen)
		Screen_EnterFullScreen();
	else if (bInFullScreen && !ConfigureParams.Screen.bFullScreen)
		Screen_ReturnFromFullScreen();

	/* update statusbar info (CPU, MHz, mem etc) */
	Statusbar_UpdateInfo();
	Dprintf("done.\n");
}


/*-----------------------------------------------------------------------*/
/**
 * Change given Hatari options
 * Return false if parsing failed, true otherwise
 */
static bool Change_Options(int argc, const char *argv[])
{
	bool bOK = false;
	CNF_PARAMS current;

	Main_PauseEmulation(false);

	/* get configuration changes */
	current = ConfigureParams;
	ConfigureParams.Screen.bFullScreen = bInFullScreen;

	/* Check if reset is required and ask user if he really wants to continue */
	if (Change_DoNeedReset(&current, &ConfigureParams)) {
		bOK = DlgAlert_Query("The emulated system must be "
				     "reset to apply these changes. "
				     "Apply changes now and reset "
				     "the emulator?");
	}
	/* Copy details to configuration */
	if (bOK) {
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, false);
	} else {
		ConfigureParams = current;
	}

	Main_UnPauseEmulation();
	return bOK;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse given command line and change Hatari options accordingly.
 * Given string must be stripped and not empty.
 * Return false if parsing failed or there were no args, true otherwise
 */
bool Change_ApplyCommandline(char *cmdline)
{
	int i, argc, inarg;
	const char **argv;
	bool ret;

	/* count args */
	inarg = argc = 0;
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace((unsigned char)cmdline[i]) && cmdline[i-1] != '\\')
		{
			inarg = 0;
			continue;
		}
		if (!inarg)
		{
			inarg++;
			argc++;
		}
	}
	if (!argc)
	{
		return false;
	}
	/* 2 = "hatari" + NULL */
	argv = malloc((argc+2) * sizeof(char*));
	if (!argv)
	{
		perror("command line alloc");
		return false;
	}

	/* parse them to array */
	fprintf(stderr, "Command line with '%d' arguments:\n", argc);
	inarg = argc = 0;
	argv[argc++] = "hatari";
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace((unsigned char)cmdline[i]))
		{
			if (cmdline[i-1] != '\\')
			{
				cmdline[i] = '\0';
				if (inarg)
				{
					fprintf(stderr, "- '%s'\n", argv[argc-1]);
				}
				inarg = 0;
				continue;
			}
			else
			{
				/* remove quote for space */
				memcpy(cmdline+i-1, cmdline+i, strlen(cmdline+i)+1);
				i--;
			}
		}
		if (!inarg)
		{
			argv[argc++] = &(cmdline[i]);
			inarg++;
		}
	}
	if (inarg)
	{
		fprintf(stderr, "- '%s'\n", argv[argc-1]);
	}
	argv[argc] = NULL;
	
	/* do args */
	ret = Change_Options(argc, argv);
	free((void *)argv);
	return ret;
}
