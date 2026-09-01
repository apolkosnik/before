/*
  Previous - dialog.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Code to handle our options dialog.
*/
const char Dialog_fileid[] = "Previous dialog.c";

#include "main.h"
#include "configuration.h"
#include "change.h"
#include "dialog.h"
#include "log.h"
#include "screen.h"
#include "paths.h"
#include "file.h"


/*-----------------------------------------------------------------------*/
/**
 * Open Property sheet Options dialog.
 * 
 * We keep all our configuration details in a structure called
 * 'ConfigureParams'. When we open our dialog we make a backup
 * of this structure. When the user finally clicks on 'OK',
 * we can compare and makes the necessary changes.
 * 
 * Return true if user chooses OK, or false if cancel!
 */
bool Dialog_DoProperty(void)
{
	bool bOKDialog;  /* Did user 'OK' dialog? */
	bool bWasActive;
	bool bForceReset;
	bool bLoadedSnapshot;
	CNF_PARAMS current;

	bWasActive = Main_PauseEmulation(true);
	bForceReset = false;

	/* Show the main window */
	Screen_ShowMainWindow();

	/* Copy details (this is so can restore if 'Cancel' dialog) */
	current = ConfigureParams;
	ConfigureParams.Screen.bFullScreen = bInFullScreen;
	bOKDialog = Dialog_MainDlg(&bForceReset, &bLoadedSnapshot);

	/* If a memory snapshot has been loaded, no further changes are required */
	if (bLoadedSnapshot)
	{
		Main_UnPauseEmulation();
		return true;
	}

	/* Check if reset is required and ask user if he really wants to continue then */
	if (bOKDialog && !bForceReset
	    && Change_DoNeedReset(&current, &ConfigureParams)) {
		bOKDialog = DlgAlert_Query("The emulated system must be "
		                           "reset to apply these changes. "
		                           "Apply changes now and reset "
		                           "the emulator?");
	}

	if (bQuitProgram)
		Main_RequestQuit(true);

	/* Copy details to configuration */
	if (bOKDialog) {
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, bForceReset);
	} else {
		ConfigureParams = current;
	}

	if (bQuitProgram)
		Main_RequestQuit(false);

	if (bWasActive)
		Main_UnPauseEmulation();
 
	return bOKDialog;
}


/* Function to check if all necessary files exist. If loading of a file fails, we bring up a dialog to let the
 * user choose another file. This is repeated for each missing file.
 */

void Dialog_CheckFiles(void) {
	int i;

	char *szMissingFile = NULL;
	char szDefault[FILENAME_MAX];
	char szMachine[64];
	bool bEnable = false;

	/* Check if ROM file exists. If it is missing present a dialog to select a new ROM file. */
	if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
		szMissingFile = ConfigureParams.Rom.szRom030FileName;
		snprintf(szMachine, sizeof(szMachine), "NeXT Computer");
		File_MakePathBuf(szDefault, sizeof(szDefault), Paths_GetDataDir(), "Rev_1.0_v41", "BIN");
	} else {
		if (ConfigureParams.System.bTurbo) {
			szMissingFile = ConfigureParams.Rom.szRomTurboFileName;
			snprintf(szMachine, sizeof(szMachine), "%s Turbo %s",
			         (ConfigureParams.System.nMachineType==NEXT_CUBE040)?"NeXTcube":"NeXTstation",
			         (ConfigureParams.System.bColor)?"Color":"");
			File_MakePathBuf(szDefault, sizeof(szDefault), Paths_GetDataDir(), "Rev_3.3_v74", "BIN");
		} else {
			szMissingFile = ConfigureParams.Rom.szRom040FileName;
			snprintf(szMachine, sizeof(szMachine), "%s %s",
			         (ConfigureParams.System.nMachineType==NEXT_CUBE040)?"NeXTcube":"NeXTstation",
			         (ConfigureParams.System.bColor)?"Color":"");
			File_MakePathBuf(szDefault, sizeof(szDefault), Paths_GetDataDir(), "Rev_2.5_v66", "BIN");
		}
	}
	while (!bQuitProgram && !File_Exists(szMissingFile)) {
		DlgMissing_Rom(szMachine, szMissingFile, szDefault, &bEnable);
	}
	for (i = 0; i < ND_MAX_BOARDS; i++) {
		while (!bQuitProgram &&
		       ConfigureParams.Dimension.board[i].bEnabled &&
		       !File_Exists(ConfigureParams.Dimension.board[i].szRomFileName)) {
			snprintf(szMachine, sizeof(szMachine), "NeXTdimension at slot %i", i*2+2);
			File_MakePathBuf(szDefault, sizeof(szDefault), Paths_GetDataDir(), "ND_step1_v43", "BIN");
			DlgMissing_Rom(szMachine, ConfigureParams.Dimension.board[i].szRomFileName,
			               szDefault, &ConfigureParams.Dimension.board[i].bEnabled);
		}
	}

	/* Check if SCSI disk images exist. Present a dialog to select missing files. */
	for (i = 0; i < ESP_MAX_DEVS; i++) {
		while (!bQuitProgram &&
		       (ConfigureParams.SCSI.target[i].nDeviceType!=SD_NONE) &&
		       ConfigureParams.SCSI.target[i].bDiskInserted &&
		       !File_Exists(ConfigureParams.SCSI.target[i].szImageName)) {
			DlgMissing_Disk("SCSI disk", i,
			                ConfigureParams.SCSI.target[i].szImageName,
			                &ConfigureParams.SCSI.target[i].bDiskInserted,
			                &ConfigureParams.SCSI.target[i].bWriteProtected);
			if (ConfigureParams.SCSI.target[i].nDeviceType==SD_HARDDISK &&
			    !ConfigureParams.SCSI.target[i].bDiskInserted) {
				ConfigureParams.SCSI.target[i].nDeviceType=SD_NONE;
			}
		}
	}

	/* Check if MO disk images exist. Present a dialog to select missing files. */
	for (i = 0; i < MO_MAX_DRIVES; i++) {
		while (!bQuitProgram &&
		       ConfigureParams.MO.drive[i].bDriveConnected &&
		       ConfigureParams.MO.drive[i].bDiskInserted &&
		       !File_Exists(ConfigureParams.MO.drive[i].szImageName)) {
			DlgMissing_Disk("MO disk", i,
			                ConfigureParams.MO.drive[i].szImageName,
			                &ConfigureParams.MO.drive[i].bDiskInserted,
			                &ConfigureParams.MO.drive[i].bWriteProtected);
		}
	}

	/* Check if floppy disk images exist. Present a dialog to select missing files. */
	for (i = 0; i < FLP_MAX_DRIVES; i++) {
		while (!bQuitProgram &&
		       ConfigureParams.Floppy.drive[i].bDriveConnected &&
		       ConfigureParams.Floppy.drive[i].bDiskInserted &&
		       !File_Exists(ConfigureParams.Floppy.drive[i].szImageName)) {
			DlgMissing_Disk("Floppy", i,
			                ConfigureParams.Floppy.drive[i].szImageName,
			                &ConfigureParams.Floppy.drive[i].bDiskInserted,
			                &ConfigureParams.Floppy.drive[i].bWriteProtected);
		}
	}
	
	/* Check if NFS shared direcory and printer output directory exist. */
	while (!bQuitProgram && !File_DirExists(ConfigureParams.Ethernet.nfs[0].szPathName)) {
		DlgMissing_Dir("NFS shared", ConfigureParams.Ethernet.nfs[0].szPathName, Paths_GetUserHome());
	}
	while (!bQuitProgram && !File_DirExists(ConfigureParams.Printer.szPrintToFileName)) {
		DlgMissing_Dir("Printer output", ConfigureParams.Printer.szPrintToFileName, Paths_GetUserHome());
	}
}
