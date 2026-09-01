/*
  Previous - configuration.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is now stored in an ASCII format to allow the user
  to edit the file manually.
*/
const char Configuration_fileid[] = "Previous configuration.c";

#include "main.h"
#include "host.h"
#include "configuration.h"
#include "cfgopts.h"
#include "file.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "paths.h"
#include "68kDisass.h"
#include "grab.h"
#include "dimension.hpp"


CNF_PARAMS ConfigureParams;                 /* List of configuration for the emulator */
char sConfigFileName[FILENAME_MAX];         /* Stores the name of the configuration file */


/* Used to load/save logging options */
static const struct Config_Tag configs_Log[] =
{
	{ "sLogFileName", String_Tag, ConfigureParams.Log.sLogFileName },
	{ "sTraceFileName", String_Tag, ConfigureParams.Log.sTraceFileName },
	{ "nTextLogLevel", Int_Tag, &ConfigureParams.Log.nTextLogLevel },
	{ "nAlertDlgLogLevel", Int_Tag, &ConfigureParams.Log.nAlertDlgLogLevel },
	{ "bConfirmQuit", Bool_Tag, &ConfigureParams.Log.bConfirmQuit },
	{ "bConsoleWindow", Bool_Tag, &ConfigureParams.Log.bConsoleWindow },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save configuration dialog options */
static const struct Config_Tag configs_ConfigDialog[] =
{
	{ "bShowConfigDialogAtStartup", Bool_Tag, &ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save debugger options */
static const struct Config_Tag configs_Debugger[] =
{
	{ "nNumberBase", Int_Tag, &ConfigureParams.Debugger.nNumberBase },
	{ "nSymbolLines", Int_Tag, &ConfigureParams.Debugger.nSymbolLines },
	{ "nMemdumpLines", Int_Tag, &ConfigureParams.Debugger.nMemdumpLines },
	{ "nFindLines", Int_Tag, &ConfigureParams.Debugger.nFindLines },
	{ "nDisasmLines", Int_Tag, &ConfigureParams.Debugger.nDisasmLines },
	{ "nBacktraceLines", Int_Tag, &ConfigureParams.Debugger.nBacktraceLines },
	{ "nExceptionDebugMask", Int_Tag, &ConfigureParams.Debugger.nExceptionDebugMask },
	{ "nDisasmOptions", Int_Tag, &ConfigureParams.Debugger.nDisasmOptions },
	{ "bDisasmUAE", Bool_Tag, &ConfigureParams.Debugger.bDisasmUAE },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save screen options */
static const struct Config_Tag configs_Screen[] =
{
	{ "nMode", Int_Tag, &ConfigureParams.Screen.nMode },
	{ "nGroupModePos0", Int_Tag, &ConfigureParams.Screen.nGroupModePos[0] },
	{ "nGroupModePos1", Int_Tag, &ConfigureParams.Screen.nGroupModePos[1] },
	{ "nGroupModePos2", Int_Tag, &ConfigureParams.Screen.nGroupModePos[2] },
	{ "nGroupModePos3", Int_Tag, &ConfigureParams.Screen.nGroupModePos[3] },
	{ "nSingleModeSlot", Int_Tag, &ConfigureParams.Screen.nSingleModeSlot },
	{ "bFullScreen", Bool_Tag, &ConfigureParams.Screen.bFullScreen },
	{ "bShowStatusbar", Bool_Tag, &ConfigureParams.Screen.bShowStatusbar },
	{ "bShowTitlebar", Bool_Tag, &ConfigureParams.Screen.bShowTitlebar },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save keyboard options */
static const struct Config_Tag configs_Keyboard[] =
{
	{ "bSwapCmdAlt", Bool_Tag, &ConfigureParams.Keyboard.bSwapCmdAlt },
	{ "nKeymapType", Int_Tag, &ConfigureParams.Keyboard.nKeymapType },
	{ "szMappingFileName", String_Tag, ConfigureParams.Keyboard.szMappingFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save mouse options */
static const struct Config_Tag configs_Mouse[] =
{
	{ "bEnableAutoGrab", Bool_Tag, &ConfigureParams.Mouse.bEnableAutoGrab },
	{ "bEnableMapToKey", Bool_Tag, &ConfigureParams.Mouse.bEnableMapToKey },
	{ "bEnableMacClick", Bool_Tag, &ConfigureParams.Mouse.bEnableMacClick },
	{ "bUseRawMotion",   Bool_Tag, &ConfigureParams.Mouse.bUseRawMotion },
	{ "fLinScale",       Float_Tag, &ConfigureParams.Mouse.fLinScale },
	{ "fExpScale",       Float_Tag, &ConfigureParams.Mouse.fExpScale },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save tablet options */
static const struct Config_Tag configs_Tablet[] =
{
	{ "nTabletType", Int_Tag, &ConfigureParams.Tablet.nTabletType },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save shortcut key bindings with modifiers options */
static const struct Config_Tag configs_ShortCutWithMod[] =
{
	{ "kOptions",     Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] },
	{ "kFullScreen",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] },
	{ "kMouseMode",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB] },
	{ "kColdReset",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET] },
	{ "kScreenshot",  Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT] },
	{ "kRecord",      Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_RECORD] },
	{ "kSound",       Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND] },
	{ "kPause",       Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_PAUSE] },
	{ "kDebuggerM68K",Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG_M68K] },
	{ "kDebuggerI860",Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG_I860] },
	{ "kQuit",        Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT] },
	{ "kDimension",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_DIMENSION] },
	{ "kStatusbar",   Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_STATUSBAR] },
	{ "kTitlebar",    Key_Tag, &ConfigureParams.Shortcut.withModifier[SHORTCUT_TITLEBAR] },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save shortcut key bindings without modifiers options */
static const struct Config_Tag configs_ShortCutWithoutMod[] =
{
	{ "kOptions",     Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] },
	{ "kFullScreen",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] },
	{ "kMouseMode",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_MOUSEGRAB] },
	{ "kColdReset",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_COLDRESET] },
	{ "kScreenshot",  Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SCREENSHOT] },
	{ "kRecord",      Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_RECORD] },
	{ "kSound",       Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_SOUND] },
	{ "kPause",       Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] },
	{ "kDebuggerM68K",Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_DEBUG_M68K] },
	{ "kDebuggerI860",Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_DEBUG_I860] },
	{ "kQuit",        Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_QUIT] },
	{ "kDimension",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_DIMENSION] },
	{ "kStatusbar",   Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_STATUSBAR] },
	{ "kTitlebar",    Key_Tag, &ConfigureParams.Shortcut.withoutModifier[SHORTCUT_TITLEBAR] },
	{ NULL , Error_Tag, NULL }
};


/* Used to load/save sound options */
static const struct Config_Tag configs_Sound[] =
{
	{ "bEnableMicrophone", Bool_Tag, &ConfigureParams.Sound.bEnableMicrophone },
  	{ "bEnableSound", Bool_Tag, &ConfigureParams.Sound.bEnableSound },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save memory options */
static const struct Config_Tag configs_Memory[] =
{
	{ "nMemoryBankSize0", Int_Tag, &ConfigureParams.Memory.nMemoryBankSize[0] },
	{ "nMemoryBankSize1", Int_Tag, &ConfigureParams.Memory.nMemoryBankSize[1] },
	{ "nMemoryBankSize2", Int_Tag, &ConfigureParams.Memory.nMemoryBankSize[2] },
	{ "nMemoryBankSize3", Int_Tag, &ConfigureParams.Memory.nMemoryBankSize[3] },
	{ "nMemorySpeed", Int_Tag, &ConfigureParams.Memory.nMemorySpeed },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save boot options */
static const struct Config_Tag configs_Boot[] =
{
	{ "nBootDevice", Int_Tag, &ConfigureParams.Boot.nBootDevice },
	{ "bEnableDRAMTest", Bool_Tag, &ConfigureParams.Boot.bEnableDRAMTest },
	{ "bEnablePot", Bool_Tag, &ConfigureParams.Boot.bEnablePot },
	{ "bEnableSoundTest", Bool_Tag, &ConfigureParams.Boot.bEnableSoundTest },
	{ "bEnableSCSITest", Bool_Tag, &ConfigureParams.Boot.bEnableSCSITest },
	{ "bLoopPot", Bool_Tag, &ConfigureParams.Boot.bLoopPot },
	{ "bVerbose", Bool_Tag, &ConfigureParams.Boot.bVerbose },
	{ "bExtendedPot", Bool_Tag, &ConfigureParams.Boot.bExtendedPot },
	{ "bVisible", Bool_Tag, &ConfigureParams.Boot.bVisible },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save SCSI options */
static const struct Config_Tag configs_SCSI[] =
{
	{ "szImageName0", String_Tag, ConfigureParams.SCSI.target[0].szImageName },
	{ "nDeviceType0", Int_Tag, &ConfigureParams.SCSI.target[0].nDeviceType },
	{ "bDiskInserted0", Bool_Tag, &ConfigureParams.SCSI.target[0].bDiskInserted },
	{ "bWriteProtected0", Bool_Tag, &ConfigureParams.SCSI.target[0].bWriteProtected },
	
	{ "szImageName1", String_Tag, ConfigureParams.SCSI.target[1].szImageName },
	{ "nDeviceType1", Int_Tag, &ConfigureParams.SCSI.target[1].nDeviceType },
	{ "bDiskInserted1", Bool_Tag, &ConfigureParams.SCSI.target[1].bDiskInserted },
	{ "bWriteProtected1", Bool_Tag, &ConfigureParams.SCSI.target[1].bWriteProtected },

	{ "szImageName2", String_Tag, ConfigureParams.SCSI.target[2].szImageName },
	{ "nDeviceType2", Int_Tag, &ConfigureParams.SCSI.target[2].nDeviceType },
	{ "bDiskInserted2", Bool_Tag, &ConfigureParams.SCSI.target[2].bDiskInserted },
	{ "bWriteProtected2", Bool_Tag, &ConfigureParams.SCSI.target[2].bWriteProtected },

	{ "szImageName3", String_Tag, ConfigureParams.SCSI.target[3].szImageName },
	{ "nDeviceType3", Int_Tag, &ConfigureParams.SCSI.target[3].nDeviceType },
	{ "bDiskInserted3", Bool_Tag, &ConfigureParams.SCSI.target[3].bDiskInserted },
	{ "bWriteProtected3", Bool_Tag, &ConfigureParams.SCSI.target[3].bWriteProtected },

	{ "szImageName4", String_Tag, ConfigureParams.SCSI.target[4].szImageName },
	{ "nDeviceType4", Int_Tag, &ConfigureParams.SCSI.target[4].nDeviceType },
	{ "bDiskInserted4", Bool_Tag, &ConfigureParams.SCSI.target[4].bDiskInserted },
	{ "bWriteProtected4", Bool_Tag, &ConfigureParams.SCSI.target[4].bWriteProtected },

	{ "szImageName5", String_Tag, ConfigureParams.SCSI.target[5].szImageName },
	{ "nDeviceType5", Int_Tag, &ConfigureParams.SCSI.target[5].nDeviceType },
	{ "bDiskInserted5", Bool_Tag, &ConfigureParams.SCSI.target[5].bDiskInserted },
	{ "bWriteProtected5", Bool_Tag, &ConfigureParams.SCSI.target[5].bWriteProtected },

	{ "szImageName6", String_Tag, ConfigureParams.SCSI.target[6].szImageName },
	{ "nDeviceType6", Int_Tag, &ConfigureParams.SCSI.target[6].nDeviceType },
	{ "bDiskInserted6", Bool_Tag, &ConfigureParams.SCSI.target[6].bDiskInserted },
	{ "bWriteProtected6", Bool_Tag, &ConfigureParams.SCSI.target[6].bWriteProtected },

	{ "nWriteProtection", Int_Tag, &ConfigureParams.SCSI.nWriteProtection },

	{ NULL , Error_Tag, NULL }
};

/* Used to load/save MO options */
static const struct Config_Tag configs_MO[] =
{
	{ "szImageName0", String_Tag, ConfigureParams.MO.drive[0].szImageName },
	{ "bDriveConnected0", Bool_Tag, &ConfigureParams.MO.drive[0].bDriveConnected },
	{ "bDiskInserted0", Bool_Tag, &ConfigureParams.MO.drive[0].bDiskInserted },
	{ "bWriteProtected0", Bool_Tag, &ConfigureParams.MO.drive[0].bWriteProtected },

	{ "szImageName1", String_Tag, ConfigureParams.MO.drive[1].szImageName },
	{ "bDriveConnected1", Bool_Tag, &ConfigureParams.MO.drive[1].bDriveConnected },
	{ "bDiskInserted1", Bool_Tag, &ConfigureParams.MO.drive[1].bDiskInserted },
	{ "bWriteProtected1", Bool_Tag, &ConfigureParams.MO.drive[1].bWriteProtected },

	{ NULL , Error_Tag, NULL }
};

/* Used to load/save floppy options */
static const struct Config_Tag configs_Floppy[] =
{
	{ "szImageName0", String_Tag, ConfigureParams.Floppy.drive[0].szImageName },
	{ "bDriveConnected0", Bool_Tag, &ConfigureParams.Floppy.drive[0].bDriveConnected },
	{ "bDiskInserted0", Bool_Tag, &ConfigureParams.Floppy.drive[0].bDiskInserted },
	{ "bWriteProtected0", Bool_Tag, &ConfigureParams.Floppy.drive[0].bWriteProtected },

	{ "szImageName1", String_Tag, ConfigureParams.Floppy.drive[1].szImageName },
	{ "bDriveConnected1", Bool_Tag, &ConfigureParams.Floppy.drive[1].bDriveConnected },
	{ "bDiskInserted1", Bool_Tag, &ConfigureParams.Floppy.drive[1].bDiskInserted },
	{ "bWriteProtected1", Bool_Tag, &ConfigureParams.Floppy.drive[1].bWriteProtected },

	{ "szImageName2", String_Tag, ConfigureParams.Floppy.drive[2].szImageName },
	{ "bDriveConnected2", Bool_Tag, &ConfigureParams.Floppy.drive[2].bDriveConnected },
	{ "bDiskInserted2", Bool_Tag, &ConfigureParams.Floppy.drive[2].bDiskInserted },
	{ "bWriteProtected2", Bool_Tag, &ConfigureParams.Floppy.drive[2].bWriteProtected },

	{ "szImageName3", String_Tag, ConfigureParams.Floppy.drive[3].szImageName },
	{ "bDriveConnected3", Bool_Tag, &ConfigureParams.Floppy.drive[3].bDriveConnected },
	{ "bDiskInserted3", Bool_Tag, &ConfigureParams.Floppy.drive[3].bDiskInserted },
	{ "bWriteProtected3", Bool_Tag, &ConfigureParams.Floppy.drive[3].bWriteProtected },

	{ NULL , Error_Tag, NULL }
};

/* Used to load/save Ethernet options */
static const struct Config_Tag configs_Ethernet[] =
{
	{ "bEthernetConnected", Bool_Tag, &ConfigureParams.Ethernet.bEthernetConnected },
	{ "bTwistedPair", Bool_Tag, &ConfigureParams.Ethernet.bTwistedPair },

	{ "nHostInterface", Int_Tag, &ConfigureParams.Ethernet.nHostInterface },
	{ "szInterfaceName", String_Tag, ConfigureParams.Ethernet.szInterfaceName },
	{ "bNetworkTime", Bool_Tag, &ConfigureParams.Ethernet.bNetworkTime },

	{ "szNFSPathName0", String_Tag, ConfigureParams.Ethernet.nfs[0].szPathName },
	{ "szNFSHostName0", String_Tag, ConfigureParams.Ethernet.nfs[0].szHostName },

	{ "szNFSPathName1", String_Tag, ConfigureParams.Ethernet.nfs[1].szPathName },
	{ "szNFSHostName1", String_Tag, ConfigureParams.Ethernet.nfs[1].szHostName },

	{ "szNFSPathName2", String_Tag, ConfigureParams.Ethernet.nfs[2].szPathName },
	{ "szNFSHostName2", String_Tag, ConfigureParams.Ethernet.nfs[2].szHostName },

	{ "szNFSPathName3", String_Tag, ConfigureParams.Ethernet.nfs[3].szPathName },
	{ "szNFSHostName3", String_Tag, ConfigureParams.Ethernet.nfs[3].szHostName },

	{ NULL , Error_Tag, NULL }
};

/* Used to load/save ROM options */
static const struct Config_Tag configs_Rom[] =
{
	{ "szRom030FileName", String_Tag, ConfigureParams.Rom.szRom030FileName },
	{ "szRom040FileName", String_Tag, ConfigureParams.Rom.szRom040FileName },
	{ "szRomTurboFileName", String_Tag, ConfigureParams.Rom.szRomTurboFileName },

	{ "bUseCustomMac", Bool_Tag, &ConfigureParams.Rom.bUseCustomMac },
	{ "nRomCustomMac0", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[0] },
	{ "nRomCustomMac1", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[1] },
	{ "nRomCustomMac2", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[2] },
	{ "nRomCustomMac3", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[3] },
	{ "nRomCustomMac4", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[4] },
	{ "nRomCustomMac5", Int_Tag, &ConfigureParams.Rom.nRomCustomMac[5] },

	{ NULL , Error_Tag, NULL }
};

/* Used to load/save printer options */
static const struct Config_Tag configs_Printer[] =
{
	{ "bPrinterConnected", Bool_Tag, &ConfigureParams.Printer.bPrinterConnected },
	{ "nPaperSize", Int_Tag, &ConfigureParams.Printer.nPaperSize },
	{ "nFileFormat", Int_Tag, &ConfigureParams.Printer.nFileFormat },
	{ "szPrintToFileName", String_Tag, ConfigureParams.Printer.szPrintToFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save system options */
static const struct Config_Tag configs_System[] =
{
	{ "nMachineType", Int_Tag, &ConfigureParams.System.nMachineType },
	{ "bColor", Bool_Tag, &ConfigureParams.System.bColor },
	{ "bTurbo", Bool_Tag, &ConfigureParams.System.bTurbo },
	{ "bNBIC", Bool_Tag, &ConfigureParams.System.bNBIC },
	{ "bADB", Bool_Tag, &ConfigureParams.System.bADB },
	{ "nSCSI", Bool_Tag, &ConfigureParams.System.nSCSI },
	{ "nRTC", Bool_Tag, &ConfigureParams.System.nRTC },

	{ "nCpuLevel", Int_Tag, &ConfigureParams.System.nCpuLevel },
	{ "nCpuFreq", Int_Tag, &ConfigureParams.System.nCpuFreq },
	{ "bCompatibleCpu", Bool_Tag, &ConfigureParams.System.bCompatibleCpu },
	{ "bRealtime", Bool_Tag, &ConfigureParams.System.bRealtime },
	{ "nDSPType", Int_Tag, &ConfigureParams.System.nDSPType },
	{ "bDSPMemoryExpansion", Bool_Tag, &ConfigureParams.System.bDSPMemoryExpansion },
	{ "n_FPUType", Int_Tag, &ConfigureParams.System.n_FPUType },
	{ "bCompatibleFPU", Bool_Tag, &ConfigureParams.System.bCompatibleFPU },
	{ "bMMU", Bool_Tag, &ConfigureParams.System.bMMU },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save nextdimension options */
static const struct Config_Tag configs_Dimension[] =
{
	{ "bI860Thread",       Bool_Tag, &ConfigureParams.Dimension.bI860Thread },
	{ "nConsoleSlot",      Int_Tag,  &ConfigureParams.Dimension.nConsoleSlot },

	{ "bEnabled0",         Bool_Tag, &ConfigureParams.Dimension.board[0].bEnabled },
	{ "nMemoryBankSize00", Int_Tag,  &ConfigureParams.Dimension.board[0].nMemoryBankSize[0] },
	{ "nMemoryBankSize01", Int_Tag,  &ConfigureParams.Dimension.board[0].nMemoryBankSize[1] },
	{ "nMemoryBankSize02", Int_Tag,  &ConfigureParams.Dimension.board[0].nMemoryBankSize[2] },
	{ "nMemoryBankSize03", Int_Tag,  &ConfigureParams.Dimension.board[0].nMemoryBankSize[3] },
	{ "szRomFileName0", String_Tag,  ConfigureParams.Dimension.board[0].szRomFileName },

	{ "bEnabled1",         Bool_Tag, &ConfigureParams.Dimension.board[1].bEnabled },
	{ "nMemoryBankSize10", Int_Tag,  &ConfigureParams.Dimension.board[1].nMemoryBankSize[0] },
	{ "nMemoryBankSize11", Int_Tag,  &ConfigureParams.Dimension.board[1].nMemoryBankSize[1] },
	{ "nMemoryBankSize12", Int_Tag,  &ConfigureParams.Dimension.board[1].nMemoryBankSize[2] },
	{ "nMemoryBankSize13", Int_Tag,  &ConfigureParams.Dimension.board[1].nMemoryBankSize[3] },
	{ "szRomFileName1", String_Tag,  ConfigureParams.Dimension.board[1].szRomFileName },

	{ "bEnabled2",         Bool_Tag, &ConfigureParams.Dimension.board[2].bEnabled },
	{ "nMemoryBankSize20", Int_Tag,  &ConfigureParams.Dimension.board[2].nMemoryBankSize[0] },
	{ "nMemoryBankSize21", Int_Tag,  &ConfigureParams.Dimension.board[2].nMemoryBankSize[1] },
	{ "nMemoryBankSize22", Int_Tag,  &ConfigureParams.Dimension.board[2].nMemoryBankSize[2] },
	{ "nMemoryBankSize23", Int_Tag,  &ConfigureParams.Dimension.board[2].nMemoryBankSize[3] },
	{ "szRomFileName2", String_Tag,  ConfigureParams.Dimension.board[2].szRomFileName },

	{ NULL , Error_Tag, NULL }
};

/*-----------------------------------------------------------------------*/
/**
 * Set default configuration values.
 */
void Configuration_SetDefault(void)
{
	int i;
	const char *psHomeDir;
	const char *psWorkingDir;

	psHomeDir = Paths_GetHatariHome();
	psWorkingDir = Paths_GetWorkingDir();

	/* Clear parameters */
	memset(&ConfigureParams, 0, sizeof(CNF_PARAMS));

	/* Set defaults for logging and tracing */
	strcpy(ConfigureParams.Log.sLogFileName, "stderr");
	strcpy(ConfigureParams.Log.sTraceFileName, "stderr");
	ConfigureParams.Log.nTextLogLevel = LOG_INFO;
	ConfigureParams.Log.nAlertDlgLogLevel = LOG_ERROR;
	ConfigureParams.Log.bConfirmQuit = true;
	ConfigureParams.Log.bConsoleWindow = false;

	/* Set defaults for config dialog */
	ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup = true;

	/* Set defaults for debugger */
	ConfigureParams.Debugger.nNumberBase = 10;
	ConfigureParams.Debugger.nSymbolLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nMemdumpLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nFindLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nDisasmLines = -1; /* <0: use terminal size */
	ConfigureParams.Debugger.nBacktraceLines = 0; /* <=0: show all */
	ConfigureParams.Debugger.nExceptionDebugMask = DEFAULT_EXCEPTIONS;
	/* external one has nicer output, but isn't as complete as UAE one */
	ConfigureParams.Debugger.bDisasmUAE = true;
	ConfigureParams.Debugger.nDisasmOptions = Disasm_GetOptions();
	Disasm_Init();

	/* Set defaults for Boot options */
	ConfigureParams.Boot.nBootDevice = BOOT_ROM;
	ConfigureParams.Boot.bEnableDRAMTest = false;
	ConfigureParams.Boot.bEnablePot = true;
	ConfigureParams.Boot.bEnableSoundTest = true;
	ConfigureParams.Boot.bEnableSCSITest = true;
	ConfigureParams.Boot.bLoopPot = false;
	ConfigureParams.Boot.bVerbose = true;
	ConfigureParams.Boot.bExtendedPot = false;
	ConfigureParams.Boot.bVisible = false;

	/* Set defaults for SCSI disks */
	for (i = 0; i < ESP_MAX_DEVS; i++) {
		strcpy(ConfigureParams.SCSI.target[i].szImageName, psWorkingDir);
		ConfigureParams.SCSI.target[i].nDeviceType = SD_NONE;
		ConfigureParams.SCSI.target[i].bDiskInserted = false;
		ConfigureParams.SCSI.target[i].bWriteProtected = false;
	}
	ConfigureParams.SCSI.nWriteProtection = WRITEPROT_OFF;

	/* Set defaults for MO drives */
	for (i = 0; i < MO_MAX_DRIVES; i++) {
		strcpy(ConfigureParams.MO.drive[i].szImageName, psWorkingDir);
		ConfigureParams.MO.drive[i].bDriveConnected = false;
		ConfigureParams.MO.drive[i].bDiskInserted = false;
		ConfigureParams.MO.drive[i].bWriteProtected = false;
	}

	/* Set defaults for floppy drives */
	for (i = 0; i < FLP_MAX_DRIVES; i++) {
		strcpy(ConfigureParams.Floppy.drive[i].szImageName, psWorkingDir);
		ConfigureParams.Floppy.drive[i].bDriveConnected = false;
		ConfigureParams.Floppy.drive[i].bDiskInserted = false;
		ConfigureParams.Floppy.drive[i].bWriteProtected = false;
	}

	/* Set defaults for Ethernet */
	ConfigureParams.Ethernet.bEthernetConnected = false;
	ConfigureParams.Ethernet.bTwistedPair = false;
	ConfigureParams.Ethernet.bNetworkTime = false;
	ConfigureParams.Ethernet.nHostInterface = ENET_SLIRP;
	strcpy(ConfigureParams.Ethernet.szInterfaceName, "");
	File_MakePathBuf(ConfigureParams.Ethernet.nfs[0].szPathName,
	                 sizeof(ConfigureParams.Ethernet.nfs[0].szPathName),
	                 Paths_GetUserHome(), "", NULL);
	for (i = 1; i < EN_MAX_SHARES; i++) {
		strcpy(ConfigureParams.Ethernet.nfs[i].szPathName, "");
		snprintf(ConfigureParams.Ethernet.nfs[i].szHostName, 
				 sizeof(ConfigureParams.Ethernet.nfs[i].szHostName), "nfs%d", i);
	}

	/* Set defaults for Keyboard */
	ConfigureParams.Keyboard.bSwapCmdAlt = false;
	ConfigureParams.Keyboard.nKeymapType = KEYMAP_SCANCODE;
	strcpy(ConfigureParams.Keyboard.szMappingFileName, "");

	/* Set defaults for Mouse */
	ConfigureParams.Mouse.fLinScale = 1.0;
	ConfigureParams.Mouse.fExpScale = 0.75;
	ConfigureParams.Mouse.bUseRawMotion = false;
	ConfigureParams.Mouse.bEnableAutoGrab = true;
	ConfigureParams.Mouse.bEnableMapToKey = false;
	ConfigureParams.Mouse.bEnableMacClick = false;

	/* Set defaults for Tablet */
	ConfigureParams.Tablet.nTabletType = TABLET_NONE;

	/* Set defaults for Shortcuts */
	Keymap_InitShortcutDefaultKeys();

	/* Set defaults for Memory */
	memset(ConfigureParams.Memory.nMemoryBankSize, 16, 
	       sizeof(ConfigureParams.Memory.nMemoryBankSize)); /* 64 MiB */
	ConfigureParams.Memory.nMemorySpeed = MEMORY_100NS;

	/* Set defaults for Printer */
	ConfigureParams.Printer.bPrinterConnected = false;
	ConfigureParams.Printer.nPaperSize = PAPER_A4;
	ConfigureParams.Printer.nFileFormat = FORMAT_PNG;
	File_MakePathBuf(ConfigureParams.Printer.szPrintToFileName,
	                 sizeof(ConfigureParams.Printer.szPrintToFileName),
	                 Paths_GetUserHome(), "", NULL);

	/* Set defaults for Screen */
	ConfigureParams.Screen.nMode = SCREEN_SINGLE;
	ConfigureParams.Screen.nGroupModePos[0] = 0;
	for (i = 1; i < NUM_MONITORS; i++) {
		ConfigureParams.Screen.nGroupModePos[i] = -1;
	}
	ConfigureParams.Screen.nSingleModeSlot = 0;
	ConfigureParams.Screen.bFullScreen = false;
	ConfigureParams.Screen.bShowStatusbar = true;
	ConfigureParams.Screen.bShowTitlebar = true;

	/* Set defaults for Sound */
	ConfigureParams.Sound.bEnableMicrophone = true;
	ConfigureParams.Sound.bEnableSound = true;

	/* Set defaults for Rom */
	File_MakePathBuf(ConfigureParams.Rom.szRom030FileName,
	                 sizeof(ConfigureParams.Rom.szRom030FileName),
	                 Paths_GetDataDir(), "Rev_1.0_v41", "BIN");
	File_MakePathBuf(ConfigureParams.Rom.szRom040FileName,
	                 sizeof(ConfigureParams.Rom.szRom040FileName),
	                 Paths_GetDataDir(), "Rev_2.5_v66", "BIN");
	File_MakePathBuf(ConfigureParams.Rom.szRomTurboFileName,
	                 sizeof(ConfigureParams.Rom.szRomTurboFileName),
	                 Paths_GetDataDir(), "Rev_3.3_v74", "BIN");

	ConfigureParams.Rom.bUseCustomMac = false;
	memset(ConfigureParams.Rom.nRomCustomMac, 0,
	       sizeof(ConfigureParams.Rom.nRomCustomMac));
	ConfigureParams.Rom.nRomCustomMac[2] = 0x0f;

	/* Set defaults for System */
	ConfigureParams.System.nMachineType = NEXT_CUBE030;
	ConfigureParams.System.bColor = false;
	ConfigureParams.System.bTurbo = false;
	ConfigureParams.System.bNBIC = true;
	ConfigureParams.System.bADB = false;
	ConfigureParams.System.nSCSI = NCR53C90;
	ConfigureParams.System.nRTC = MC68HC68T1;

	ConfigureParams.System.nCpuLevel = 3;
	ConfigureParams.System.nCpuFreq = 25;
	ConfigureParams.System.bCompatibleCpu = false;
	ConfigureParams.System.bRealtime = false;
	ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
	ConfigureParams.System.bDSPMemoryExpansion = false;
	ConfigureParams.System.n_FPUType = FPU_68882;
	ConfigureParams.System.bCompatibleFPU = true;
	ConfigureParams.System.bMMU = true;

	/* Set defaults for Dimension */
	ConfigureParams.Dimension.bI860Thread  = host_num_cpus() != 1;
	ConfigureParams.Dimension.nConsoleSlot = 0;
	for (i = 0; i < ND_MAX_BOARDS; i++) {
		ConfigureParams.Dimension.board[i].bEnabled           = false;
		ConfigureParams.Dimension.board[i].nMemoryBankSize[0] = 4;
		ConfigureParams.Dimension.board[i].nMemoryBankSize[1] = 4;
		ConfigureParams.Dimension.board[i].nMemoryBankSize[2] = 4;
		ConfigureParams.Dimension.board[i].nMemoryBankSize[3] = 4;
		File_MakePathBuf(ConfigureParams.Dimension.board[i].szRomFileName,
		                 sizeof(ConfigureParams.Dimension.board[i].szRomFileName),
		                 Paths_GetDataDir(), "ND_step1_v43", "BIN");
	}

	/* Initialize the configuration file name */
	if (File_MakePathBuf(sConfigFileName, sizeof(sConfigFileName),
	                     psHomeDir, "previous", "cfg"))
	{
		strcpy(sConfigFileName, "previous.cfg");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Helper function for Configuration_Apply, check mouse speed settings
 * to be in the valid range between minimum and maximum value.
 */
static void Configuration_CheckFloatMinMax(float *val, float min, float max)
{
	if (*val<min)
		*val=min;
	if (*val>max)
		*val=max;
}

/*-----------------------------------------------------------------------*/
/**
 * Helper function for Configuration_Apply, check dimension settings
 * to be valid.
 */
static void Configuration_CheckDimensionSettings(void) {
	int i;

	ConfigureParams.Dimension.nConsoleSlot &= 6;
	ConfigureParams.Screen.nSingleModeSlot &= 6;

	for (i = 0; i < ND_MAX_BOARDS; i++) {
		if (ConfigureParams.System.nMachineType==NEXT_STATION) {
			ConfigureParams.Dimension.board[i].bEnabled = false;
		}
		if (ConfigureParams.Dimension.board[i].bEnabled) {
			Configuration_CheckDimensionMemory(ConfigureParams.Dimension.board[i].nMemoryBankSize);
			ConfigureParams.System.bNBIC = true;
		} else if (ConfigureParams.Dimension.nConsoleSlot == ND_SLOT(i)) {
			ConfigureParams.Screen.nSingleModeSlot = 0;
		}
	}

	ConfigureParams.Dimension.bI860Thread = host_num_cpus() != 1;
}

/*-----------------------------------------------------------------------*/
/**
 * Helper function for Configuration_Apply, check screen settings
 * to be valid. Check for duplicates and screens without board.
 */
static void Configuration_Check_ScreenSettings(void) {
	int i, j, n, s;
	if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
		for (i = 0, n = 0, s = 0; i < NUM_MONITORS; i++) {
			if (ConfigureParams.Screen.nGroupModePos[i] >= 0) {
				for (j = i + 1; j < NUM_MONITORS; j++) {
					if (ConfigureParams.Screen.nGroupModePos[j] >= 0) {
						if (ConfigureParams.Screen.nGroupModePos[j] == ConfigureParams.Screen.nGroupModePos[i]) {
							ConfigureParams.Screen.nGroupModePos[j] = -1;
						}
					}
				}
				if (ConfigureParams.Screen.nGroupModePos[i] >= (NUM_MONITORS * NUM_MONITORS)) {
					ConfigureParams.Screen.nGroupModePos[i] = -1;
				}
				if (i && !ConfigureParams.Dimension.board[ND_NUM(2 * i)].bEnabled) {
					ConfigureParams.Screen.nGroupModePos[i] = -1;
				}
				if (ConfigureParams.Screen.nGroupModePos[i] >= 0) {
					s = 2 * i;
					n++;
				}
			}
		}
		if (n < 2) {
			ConfigureParams.Screen.nMode = SCREEN_SINGLE;
			ConfigureParams.Screen.nSingleModeSlot = s;
		}
	}
	if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) {
		if (ConfigureParams.Screen.nSingleModeSlot > 0) {
			if (!ConfigureParams.Dimension.board[ND_NUM(ConfigureParams.Screen.nSingleModeSlot)].bEnabled) {
				ConfigureParams.Screen.nSingleModeSlot = 0;
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Helper function for Configuration_Apply, check ethernet settings
 * to be valid.
 */
static void Configuration_CheckEthernetSettings(void) {
	if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
		ConfigureParams.Ethernet.bTwistedPair = false;
	}
	if (ConfigureParams.Ethernet.nHostInterface == ENET_PCAP) {
		ConfigureParams.Ethernet.bNetworkTime = false;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Check memory bank sizes for compatibility with the selected system.
 */
#define RESTRICTIVE_MEMCHECK 0 /* Disable configurations with no memory */
#define SAFE_MEMCHECK        1 /* Disable configurations that crash */

int Configuration_CheckMemory(int *banksize) {
	int i;

#if RESTRICTIVE_MEMCHECK
	/* To boot we need at least 4 MB in bank0 */
	if (banksize[0]<4) {
		banksize[0]=4;
	}
#endif

	if (ConfigureParams.System.bTurbo) {
		for (i=0; i<4; i++) {
			if (banksize[i]<=0)
				banksize[i]=0;
			else if (banksize[i]<=2)
				banksize[i]=2;
			else if (banksize[i]<=8)
				banksize[i]=8;
			else if (banksize[i]<=32)
				banksize[i]=32;
			else
				banksize[i]=32;
		}
	} else if (ConfigureParams.System.bColor) {
		for (i=0; i<4; i++) {
			if (banksize[i]<=0)
				banksize[i]=0;
			else if (banksize[i]<=2)
				banksize[i]=2;
			else if (banksize[i]<=8)
				banksize[i]=8;
			else
				banksize[i]=8;
		}
	} else {
		for (i=0; i<4; i++) {
			if (banksize[i]<=0)
				banksize[i]=0;
			else if (banksize[i]<=1)
				banksize[i]=1;
			else if (banksize[i]<=4)
				banksize[i]=4;
			else if (banksize[i]<=16)
				banksize[i]=16;
			else
				banksize[i]=16;
		}
		/* Non-Turbo monochrome Slab: Only first two banks are physically accessible */
		if (ConfigureParams.System.nMachineType==NEXT_STATION) {
			banksize[2]=0;
			banksize[3]=0;
		}
	}
	return (banksize[0]+banksize[1]+banksize[2]+banksize[3]);
}

int Configuration_CheckDimensionMemory(int *banksize) {
	int i;

#if RESTRICTIVE_MEMCHECK
	/* To boot we need at least 4 MB in bank0 */
	if (banksize[0]<4) {
		banksize[0]=4;
	}
#endif
#if SAFE_MEMCHECK
	/* No memory in bank 0 while other banks have memory causes kernel panic on boot. */
	for (i=3; i>0; i--) {
		if (banksize[0]<=0) {
			if (banksize[i]>0) {
				banksize[0]=banksize[i];
				banksize[i]=0;
				break;
			}
		} else {
			break;
		}
	}
#endif
	for (i=0; i<4; i++) {
		if (banksize[i]<=0)
			banksize[i]=0;
		else if (banksize[i]<=4)
			banksize[i]=4;
		else if (banksize[i]<=16)
			banksize[i]=16;
		else
			banksize[i]=16;
	}
	return (banksize[0]+banksize[1]+banksize[2]+banksize[3]);
}

/*-----------------------------------------------------------------------*/
/**
 * Check selected peripherals for compatibility with the selected system.
 */
void Configuration_CheckPeripheralSettings(void) {
	if (!ConfigureParams.System.bTurbo) {
		ConfigureParams.System.bADB = false;
	}
	if (ConfigureParams.System.nMachineType == NEXT_STATION) {
		ConfigureParams.System.bNBIC = false;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details from configuration structure into global variables for system,
 * clean file names, etc...  Called from main.c and dialog.c files.
 */
void Configuration_Apply(bool bReset)
{
	int i;

	/* Mouse settings */
	if (ConfigureParams.Mouse.bEnableMacClick) {
		ConfigureParams.Mouse.bEnableAutoGrab = false;
	}
	Configuration_CheckFloatMinMax(&ConfigureParams.Mouse.fLinScale, MOUSE_LIN_MIN, MOUSE_LIN_MAX);
	Configuration_CheckFloatMinMax(&ConfigureParams.Mouse.fExpScale, MOUSE_EXP_MIN, MOUSE_EXP_MAX);

	/* Check/constrain CPU settings and change corresponding
	 * cpu_model/cpu_compatible/cpu_cycle_exact/... variables
	 */
	M68000_CheckCpuSettings();

	/* Check memory size for each bank and change to supported values */
	Configuration_CheckMemory(ConfigureParams.Memory.nMemoryBankSize);

	/* Check nextdimension memory size and screen options */
	Configuration_CheckDimensionSettings();

	/* Check screen settings */
	Configuration_Check_ScreenSettings();

	/* Make sure twisted pair ethernet is disabled on 68030 Cube */
	Configuration_CheckEthernetSettings();

	/* Make sure NBIC is only used on Cubes and ADB only on Turbo */
	Configuration_CheckPeripheralSettings();

	/* Clean file and directory names */
	File_MakeAbsoluteName(ConfigureParams.Rom.szRom030FileName);
	File_MakeAbsoluteName(ConfigureParams.Rom.szRom040FileName);
	File_MakeAbsoluteName(ConfigureParams.Rom.szRomTurboFileName);
	File_MakeAbsoluteName(ConfigureParams.Printer.szPrintToFileName);

	for (i = 0; i < ND_MAX_BOARDS; i++) {
		File_MakeAbsoluteName(ConfigureParams.Dimension.board[i].szRomFileName);
	}

	for (i = 0; i < ESP_MAX_DEVS; i++) {
		File_MakeAbsoluteName(ConfigureParams.SCSI.target[i].szImageName);
	}

	for (i = 0; i < MO_MAX_DRIVES; i++) {
		File_MakeAbsoluteName(ConfigureParams.MO.drive[i].szImageName);
	}

	for (i = 0; i < FLP_MAX_DRIVES; i++) {
		File_MakeAbsoluteName(ConfigureParams.Floppy.drive[i].szImageName);
	}

	/* make sure there are no trailing path separators */
	for (i = 0; i < EN_MAX_SHARES; i++) {
		File_CleanFileName(ConfigureParams.Ethernet.nfs[i].szPathName);
	}
	File_CleanFileName(ConfigureParams.Printer.szPrintToFileName);

	/* make path names absolute, but handle special file names */
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sLogFileName);
	File_MakeAbsoluteSpecialName(ConfigureParams.Log.sTraceFileName);
}


/*-----------------------------------------------------------------------*/
/**
 * Set defaults depending on selected machine type.
 */
void Configuration_SetSystemDefaults(void) {
	switch (ConfigureParams.System.nMachineType) {
		case NEXT_CUBE030:
			ConfigureParams.System.bTurbo = false;
			ConfigureParams.System.bColor = false;
			ConfigureParams.System.nCpuLevel = 3;
			ConfigureParams.System.nCpuFreq = 25;
			ConfigureParams.System.n_FPUType = FPU_68882;
			ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
			ConfigureParams.System.bDSPMemoryExpansion = false;
			ConfigureParams.System.nSCSI = NCR53C90;
			ConfigureParams.System.nRTC = MC68HC68T1;
			ConfigureParams.System.bNBIC = true;
			ConfigureParams.System.bADB = false;
			break;

		case NEXT_CUBE040:
			ConfigureParams.System.bColor = false;
			ConfigureParams.System.nCpuLevel = 4;
			if (ConfigureParams.System.bTurbo) {
				ConfigureParams.System.nCpuFreq = 33;
				ConfigureParams.System.nRTC = MCCS1850;
				ConfigureParams.System.bADB = true;
			} else {
				ConfigureParams.System.nCpuFreq = 25;
				ConfigureParams.System.nRTC = MC68HC68T1;
				ConfigureParams.System.bADB = false;
			}
			ConfigureParams.System.n_FPUType = FPU_CPU;
			ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
			ConfigureParams.System.bDSPMemoryExpansion = true;
			ConfigureParams.System.nSCSI = NCR53C90A;
			ConfigureParams.System.bNBIC = true;
			break;

		case NEXT_STATION:
			ConfigureParams.System.nCpuLevel = 4;
			if (ConfigureParams.System.bTurbo) {
				ConfigureParams.System.nCpuFreq = 33;
				ConfigureParams.System.nRTC = MCCS1850;
				ConfigureParams.System.bADB = true;
			} else {
				ConfigureParams.System.nCpuFreq = 25;
				ConfigureParams.System.nRTC = MC68HC68T1;
				ConfigureParams.System.bADB = false;
			}
			ConfigureParams.System.n_FPUType = FPU_CPU;
			ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
			ConfigureParams.System.bDSPMemoryExpansion = true;
			ConfigureParams.System.nSCSI = NCR53C90A;
			ConfigureParams.System.bNBIC = false;
			break;
		default:
			break;
	}

	if (ConfigureParams.System.bTurbo) {
		ConfigureParams.Memory.nMemoryBankSize[0] = 32;
		ConfigureParams.Memory.nMemoryBankSize[1] = 32;
		ConfigureParams.Memory.nMemoryBankSize[2] = 32;
		ConfigureParams.Memory.nMemoryBankSize[3] = 32;
	} else if (ConfigureParams.System.bColor) {
		ConfigureParams.Memory.nMemoryBankSize[0] = 8;
		ConfigureParams.Memory.nMemoryBankSize[1] = 8;
		ConfigureParams.Memory.nMemoryBankSize[2] = 8;
		ConfigureParams.Memory.nMemoryBankSize[3] = 8;
	} else {
		ConfigureParams.Memory.nMemoryBankSize[0] = 16;
		ConfigureParams.Memory.nMemoryBankSize[1] = 16;
		if (ConfigureParams.System.nMachineType==NEXT_STATION) {
			ConfigureParams.Memory.nMemoryBankSize[2] = 0;
			ConfigureParams.Memory.nMemoryBankSize[3] = 0;
		} else {
			ConfigureParams.Memory.nMemoryBankSize[2] = 16;
			ConfigureParams.Memory.nMemoryBankSize[3] = 16;
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Set default screen position for given slot. Skip if screen already
 * has a position set. Use next free screen if default position is 
 * already occupied by another board.
 */
void Configuration_SetDefaultScreen(int slot) {
	int i, n;
	n = slot / 2;
	if (n < 0 || n >= NUM_MONITORS) {
		return;
	}
	if (ConfigureParams.Screen.nGroupModePos[n] < 0) {
		if (Configuration_GetScreenFromPos(n) < 0) {
			ConfigureParams.Screen.nGroupModePos[n] = n;
		} else {
			for (i = 0; i < NUM_MONITORS; i++) {
				if (Configuration_GetScreenFromPos(i) < 0) {
					ConfigureParams.Screen.nGroupModePos[n] = i;
					break;
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Get screen number from position in monitor grid.
 */
int Configuration_GetScreenFromPos(int pos) {
	int i;
	for (i = 0; i < NUM_MONITORS; i++) {
		if (ConfigureParams.Screen.nGroupModePos[i] == pos) {
			return i;
		}
	}
	return -1;
}

/*-----------------------------------------------------------------------*/
/**
 * Return true if set to show color screen on single window.
 */
int Configuration_SingleColorScreen(void) {
	if (ConfigureParams.Screen.nMode == SCREEN_SINGLE) {
		return (ConfigureParams.Screen.nSingleModeSlot > 0);
	}
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Load a settings section from the configuration file.
 */
static int Configuration_LoadSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = input_config(pFilename, configs, pSection);

	if (ret < 0)
		Log_Printf(LOG_ERROR, "cannot load configuration file %s (section %s).\n",
		        pFilename, pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Load program setting from configuration file. If psFileName is NULL, use
 * the configuration file given in configuration / last selected by user.
 */
void Configuration_Load(const char *psFileName)
{
	if (psFileName == NULL)
		psFileName = sConfigFileName;

	if (!File_Exists(psFileName))
	{
		Log_Printf(LOG_DEBUG, "Configuration file %s not found.\n", psFileName);
		return;
	}

	Configuration_LoadSection(psFileName, configs_Log, "[Log]");
	Configuration_LoadSection(psFileName, configs_ConfigDialog, "[ConfigDialog]");
	Configuration_LoadSection(psFileName, configs_Debugger, "[Debugger]");
	Configuration_LoadSection(psFileName, configs_Screen, "[Screen]");
	Configuration_LoadSection(psFileName, configs_Keyboard, "[Keyboard]");
	Configuration_LoadSection(psFileName, configs_ShortCutWithMod, "[ShortcutsWithModifiers]");
	Configuration_LoadSection(psFileName, configs_ShortCutWithoutMod, "[ShortcutsWithoutModifiers]");
	Configuration_LoadSection(psFileName, configs_Mouse, "[Mouse]");
	Configuration_LoadSection(psFileName, configs_Tablet, "[Tablet]");
	Configuration_LoadSection(psFileName, configs_Sound, "[Sound]");
	Configuration_LoadSection(psFileName, configs_Memory, "[Memory]");
	Configuration_LoadSection(psFileName, configs_Boot, "[Boot]");
	Configuration_LoadSection(psFileName, configs_SCSI, "[HardDisk]");
	Configuration_LoadSection(psFileName, configs_MO, "[MagnetoOptical]");
	Configuration_LoadSection(psFileName, configs_Floppy, "[Floppy]");
	Configuration_LoadSection(psFileName, configs_Ethernet, "[Ethernet]");
	Configuration_LoadSection(psFileName, configs_Rom, "[ROM]");
	Configuration_LoadSection(psFileName, configs_Printer, "[Printer]");
	Configuration_LoadSection(psFileName, configs_System, "[System]");
	Configuration_LoadSection(psFileName, configs_Dimension, "[Dimension]");
}


/*-----------------------------------------------------------------------*/
/**
 * Save a settings section to configuration file
 */
static int Configuration_SaveSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = update_config(pFilename, configs, pSection);

	if (ret < 0)
		Log_Printf(LOG_ERROR, "cannot save configuration file %s (section %s)\n",
			   pFilename, pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Save program setting to configuration file
 */
void Configuration_Save(void)
{
	if (Configuration_SaveSection(sConfigFileName, configs_Log, "[Log]") < 0)
	{
		Log_AlertDlg(LOG_ERROR, "Error saving config file.");
		return;
	}
	Configuration_SaveSection(sConfigFileName, configs_ConfigDialog, "[ConfigDialog]");
	Configuration_SaveSection(sConfigFileName, configs_Debugger, "[Debugger]");
	Configuration_SaveSection(sConfigFileName, configs_Screen, "[Screen]");
	Configuration_SaveSection(sConfigFileName, configs_Keyboard, "[Keyboard]");
	Configuration_SaveSection(sConfigFileName, configs_ShortCutWithMod, "[ShortcutsWithModifiers]");
	Configuration_SaveSection(sConfigFileName, configs_ShortCutWithoutMod, "[ShortcutsWithoutModifiers]");
	Configuration_SaveSection(sConfigFileName, configs_Mouse, "[Mouse]");
	Configuration_SaveSection(sConfigFileName, configs_Tablet, "[Tablet]");
	Configuration_SaveSection(sConfigFileName, configs_Sound, "[Sound]");
	Configuration_SaveSection(sConfigFileName, configs_Memory, "[Memory]");
	Configuration_SaveSection(sConfigFileName, configs_Boot, "[Boot]");
	Configuration_SaveSection(sConfigFileName, configs_SCSI, "[HardDisk]");
	Configuration_SaveSection(sConfigFileName, configs_MO, "[MagnetoOptical]");
	Configuration_SaveSection(sConfigFileName, configs_Floppy, "[Floppy]");
	Configuration_SaveSection(sConfigFileName, configs_Ethernet, "[Ethernet]");
	Configuration_SaveSection(sConfigFileName, configs_Rom, "[ROM]");
	Configuration_SaveSection(sConfigFileName, configs_Printer, "[Printer]");
	Configuration_SaveSection(sConfigFileName, configs_System, "[System]");
	Configuration_SaveSection(sConfigFileName, configs_Dimension, "[Dimension]");
}

/*-----------------------------------------------------------------------*/
/**
 * Write given 'more' string to 'buffer' and return new end of 'buffer'
 */
static char *Configuration_AddInfo(char *buffer, const char *more)
{
	if (!more)
		return buffer;
	while (*more)
		*buffer++ = *more++;
	return buffer;
}

/**
 * Write Previous configuration info string to provided buffer,
 * used by statusbar.
 * Returns number of written chars.
 */
int Configuration_SetInfoString(char *buffer, int len)
{
	int size, num;
	char *end;

	/* Output needs to fit into statusbar, so max this
	 * currently writes is about 50 chars
	 */
	assert(len >= 60);
	end = buffer;

	if (bRecordingAiff)
	{
		/* Recording in progress */
		end = Configuration_AddInfo(end, "Recording sound");
	} 
	else if (Configuration_SingleColorScreen())
	{
		/* Message for NeXTdimension */
		num = ND_NUM(ConfigureParams.Screen.nSingleModeSlot);
		size = Configuration_CheckDimensionMemory(ConfigureParams.Dimension.board[num].nMemoryBankSize);
		end = Configuration_AddInfo(end, "33MHz/i860XR/");
		end += snprintf(end, len - (end - buffer), "%dMB/", size);
		end = Configuration_AddInfo(end, "NeXTdimension/");
		end += snprintf(end, len - (end - buffer), "Slot%d", ConfigureParams.Screen.nSingleModeSlot);
	} 
	else /* Default message */
	{
		/* CPU MHz */
		end = Configuration_AddInfo(end, Main_SpeedMsg());

		/* CPU type */
		if (ConfigureParams.System.nCpuLevel > 0)
		{
			*end++ = '6';
			*end++ = '8';
			*end++ = '0';
			if (ConfigureParams.System.nCpuLevel == 5) /* Special case: 68060 has nCpuLevel=5 */
				*end++ = '0' + 6;
			else
				*end++ = '0' + ConfigureParams.System.nCpuLevel % 10;
			*end++ = '0';
			*end++ = '/';
		}

		/* amount of memory in MB */
		size = Configuration_CheckMemory(ConfigureParams.Memory.nMemoryBankSize);
		end += snprintf(end, len - (end - buffer), "%dMB/", size);

		/* machine type */
		switch (ConfigureParams.System.nMachineType)
		{
			case NEXT_CUBE030:
				end = Configuration_AddInfo(end, "NeXT Computer");
				break;
			case NEXT_CUBE040:
				end = Configuration_AddInfo(end, "NeXTcube");
				break;
			case NEXT_STATION:
				end = Configuration_AddInfo(end, "NeXTstation");
				break;
				
			default:
				break;
		}
		if (ConfigureParams.System.bTurbo)
		{
			end = Configuration_AddInfo(end, (ConfigureParams.System.nCpuFreq == 40) ? " Nitro" : " Turbo");
		}
		if (ConfigureParams.System.bColor)
		{
			end = Configuration_AddInfo(end, " Color");
		}
	}

	*end++ = '\0';

	assert((end -buffer) < len);

	return end - buffer;
}
