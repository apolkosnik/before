/*
  Hatari - configuration.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef HATARI_CONFIGURATION_H
#define HATARI_CONFIGURATION_H


#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define ENABLE_TESTING 0

/* Configuration Dialog */
typedef struct
{
  bool bShowConfigDialogAtStartup;
} CNF_CONFIGDLG;

/* Logging and tracing */
typedef struct
{
  char sLogFileName[FILENAME_MAX];
  char sTraceFileName[FILENAME_MAX];
  int nTextLogLevel;
  int nAlertDlgLogLevel;
  bool bConfirmQuit;
  bool bConsoleWindow;
} CNF_LOG;


/* Debugger */
typedef struct
{
  int nNumberBase;
  int nSymbolLines;
  int nMemdumpLines;
  int nFindLines;
  int nDisasmLines;
  int nBacktraceLines;
  int nExceptionDebugMask;
  int nDisasmOptions;
  bool bDisasmUAE;
} CNF_DEBUGGER;


/* ROM configuration */
typedef struct
{
  char szRom030FileName[FILENAME_MAX];
  char szRom040FileName[FILENAME_MAX];
  char szRomTurboFileName[FILENAME_MAX];
  bool bUseCustomMac;
  int nRomCustomMac[6];
} CNF_ROM;


/* Sound configuration */
typedef struct
{
  bool bEnableMicrophone;
  bool bEnableSound;
} CNF_SOUND;

/* Dialog Keyboard */
typedef enum
{
  KEYMAP_SYMBOLIC,  /* Use keymapping with symbolic (ASCII) key codes */
  KEYMAP_SCANCODE,  /* Use keymapping with PC keyboard scancodes */
  KEYMAP_LOADED     /* Use keymapping with a map configuration file */
} KEYMAPTYPE;

typedef struct
{
  bool bSwapCmdAlt;
  KEYMAPTYPE nKeymapType;
  char szMappingFileName[FILENAME_MAX];
} CNF_KEYBOARD;


typedef enum {
  SHORTCUT_OPTIONS,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_MOUSEGRAB,
  SHORTCUT_COLDRESET,
  SHORTCUT_SCREENSHOT,
  SHORTCUT_RECORD,
  SHORTCUT_SOUND,
  SHORTCUT_DEBUG_M68K,
  SHORTCUT_DEBUG_I860,
  SHORTCUT_PAUSE,
  SHORTCUT_QUIT,
  SHORTCUT_DIMENSION,
  SHORTCUT_STATUSBAR,
  SHORTCUT_TITLEBAR,
  SHORTCUT_KEYS,  /* number of shortcuts */
  SHORTCUT_NONE
} SHORTCUTKEYIDX;

typedef struct
{
  int withModifier[SHORTCUT_KEYS];
  int withoutModifier[SHORTCUT_KEYS];
} CNF_SHORTCUT;


/* Dialog Mouse */
#define MOUSE_LIN_MIN   0.01
#define MOUSE_LIN_MAX   10.0
#define MOUSE_EXP_MIN   0.5
#define MOUSE_EXP_MAX   1.0
typedef struct
{
  bool bEnableAutoGrab;
  bool bEnableMapToKey;
  bool bEnableMacClick;
  bool bUseRawMotion;
  float fLinScale;
  float fExpScale;
} CNF_MOUSE;


/* Tablet configuration */

typedef enum
{
  TABLET_NONE,
  TABLET_MM961,
  TABLET_MM1201,
  TABLET_SD210L,
  TABLET_SD310E,
  TABLET_SD320E,
  TABLET_SD420E,
  TABLET_SD510C
} TABLET_TYPE;

typedef struct
{
  TABLET_TYPE nTabletType;
} CNF_TABLET;

/* Memory configuration */

typedef enum
{
  MEMORY_120NS,
  MEMORY_100NS,
  MEMORY_80NS,
  MEMORY_60NS
} MEMORY_SPEED;

typedef struct
{
  int nMemoryBankSize[4];
  MEMORY_SPEED nMemorySpeed;
} CNF_MEMORY;


/* Dialog Boot options */

typedef enum
{
  BOOT_ROM,
  BOOT_SCSI,
  BOOT_ETHERNET,
  BOOT_MO,
  BOOT_FLOPPY
} BOOT_DEVICE;

typedef struct
{
  BOOT_DEVICE nBootDevice;
  bool bEnableDRAMTest;
  bool bEnablePot;
  bool bExtendedPot;
  bool bEnableSoundTest;
  bool bEnableSCSITest;
  bool bLoopPot;
  bool bVerbose;
  bool bVisible;
} CNF_BOOT;


/* Hard drives configuration */
#define ESP_MAX_DEVS 7
typedef enum {
  SD_NONE,
  SD_HARDDISK,
  SD_CD,
  SD_FLOPPY,
  NUM_SD
} SCSI_DEVTYPE;

typedef struct {
  char szImageName[FILENAME_MAX];
  SCSI_DEVTYPE nDeviceType;
  bool bDiskInserted;
  bool bWriteProtected;
} SCSIDISK;

typedef enum
{
  WRITEPROT_OFF,
  WRITEPROT_ON,
  WRITEPROT_AUTO
} WRITEPROTECTION;

typedef struct {
  SCSIDISK target[ESP_MAX_DEVS];
  int nWriteProtection;
} CNF_SCSI;


/* Magneto-optical drives configuration */
#define MO_MAX_DRIVES   2
typedef struct {
  char szImageName[FILENAME_MAX];
  bool bDriveConnected;
  bool bDiskInserted;
  bool bWriteProtected;
} MODISK;

typedef struct {
  MODISK drive[MO_MAX_DRIVES];
} CNF_MO;


/* Floppy disk drives configuration */
#define FLP_MAX_DRIVES   4
typedef struct {
  char szImageName[FILENAME_MAX];
  bool bDriveConnected;
  bool bDiskInserted;
  bool bWriteProtected;
} FLPDISK;

typedef struct {
  FLPDISK drive[FLP_MAX_DRIVES];
} CNF_FLOPPY;


/* Ethernet configuration */
#define EN_MAX_SHARES 4
typedef struct {
  char szPathName[FILENAME_MAX];
  char szHostName[64];
} EN_NFS;

typedef enum
{
  ENET_SLIRP,
  ENET_PCAP
} ENET_INTERFACE;

typedef struct {
  bool bEthernetConnected;
  bool bTwistedPair;
  ENET_INTERFACE nHostInterface;
  char szInterfaceName[FILENAME_MAX];
  bool bNetworkTime;
  EN_NFS nfs[EN_MAX_SHARES];
} CNF_ENET;

/* Screen configuration */
#define NUM_MONITORS 4
typedef enum
{
  SCREEN_SINGLE,
  SCREEN_ALL,
  SCREEN_GROUP
} SCREENMODE;

typedef struct
{
  SCREENMODE nMode;
  int nGroupModePos[NUM_MONITORS];
  int nSingleModeSlot;
  bool bFullScreen;
  bool bShowStatusbar;
  bool bShowTitlebar;
} CNF_SCREEN;


/* Printer configuration */
typedef enum
{
  PAPER_A4,
  PAPER_LETTER,
  PAPER_B5,
  PAPER_LEGAL
} PAPER_SIZE;

typedef enum
{
  FORMAT_PNG,
  FORMAT_TIFF
} PRINT_FORMAT;

typedef struct
{
  bool bPrinterConnected;
  PAPER_SIZE nPaperSize;
  PRINT_FORMAT nFileFormat;
  char szPrintToFileName[FILENAME_MAX];
} CNF_PRINTER;

/* Dialog System */
typedef enum
{
  NEXT_CUBE030,
  NEXT_CUBE040,
  NEXT_STATION
} MACHINETYPE;

typedef enum
{
  NCR53C90,
  NCR53C90A
} SCSICHIP;

#define NEXT_MIN_SEC     0           /* Thu Jan  1 00:00:00 1970 UTC */
#define NEXT_MAX_SEC     2147483647  /* Tue Jan 19 03:14:07 2038 UTC */
#define NEXT_START_SEC   552225600   /* Thu Jul  2 12:00:00 1987 UTC */
#define NEXT_LIMIT_SEC   2145916800  /* Fri Jan  1 00:00:00 2038 UTC */

#define NEXT_MIN_YEAR    (1970-1900)
#define NEXT_START_YEAR  (1988-1900)
#define NEXT_LIMIT_YEAR  (2038-1900)
typedef enum
{
  MC68HC68T1,
  MCCS1850
} RTCCHIP;

typedef enum
{
  DSP_TYPE_NONE,
  DSP_TYPE_ACCURATE,
  DSP_TYPE_EMU
} DSPTYPE;

typedef enum
{
  FPU_NONE = 0,
  FPU_68881 = 68881,
  FPU_68882 = 68882,
  FPU_CPU = 68040
} FPUTYPE;

typedef struct
{
  bool bColor;
  bool bTurbo;
  bool bNBIC;
  bool bADB;
  SCSICHIP nSCSI;
  RTCCHIP nRTC;
  int nCpuLevel;
  int nCpuFreq;
  bool bCompatibleCpu;            /* Prefetch mode */
  MACHINETYPE nMachineType;
  bool bRealtime;                 /* TRUE if realtime sources shoud be used */
  DSPTYPE nDSPType;               /* how to "emulate" DSP */
  bool bDSPMemoryExpansion;
  FPUTYPE n_FPUType;
  bool bCompatibleFPU;            /* More compatible FPU */
  bool bMMU;                      /* TRUE if MMU is enabled */
} CNF_SYSTEM;

/* NeXT Dimension configuration */
#define ND_MAX_BOARDS   3
typedef struct
{
  bool bEnabled;
  int  nMemoryBankSize[4];
  char szRomFileName[FILENAME_MAX];
} NDBOARD;

typedef struct {
  bool bI860Thread;
  int nConsoleSlot;
  NDBOARD board[ND_MAX_BOARDS];
} CNF_ND;

/* State of system is stored in this structure */
/* On reset, variables are copied into system globals and used. */
typedef struct
{
  /* Configure */
  CNF_CONFIGDLG ConfigDialog;
  CNF_LOG       Log;
  CNF_DEBUGGER  Debugger;
  CNF_SCREEN    Screen;
  CNF_KEYBOARD  Keyboard;
  CNF_SHORTCUT  Shortcut;
  CNF_MOUSE     Mouse;
  CNF_TABLET    Tablet;
  CNF_SOUND     Sound;
  CNF_MEMORY    Memory;
  CNF_BOOT      Boot;
  CNF_SCSI      SCSI;
  CNF_MO        MO;
  CNF_FLOPPY    Floppy;
  CNF_ENET      Ethernet;
  CNF_ROM       Rom;
  CNF_PRINTER   Printer;
  CNF_SYSTEM    System;
  CNF_ND        Dimension;
} CNF_PARAMS;


extern CNF_PARAMS ConfigureParams;
extern char sConfigFileName[FILENAME_MAX];

extern void Configuration_SetDefault(void);
extern void Configuration_SetSystemDefaults(void);
extern void Configuration_SetDefaultScreen(int slot);
extern int  Configuration_GetScreenFromPos(int pos);
extern int  Configuration_SingleColorScreen(void);
extern void Configuration_Apply(bool bReset);
extern int  Configuration_CheckMemory(int *banksize);
extern int  Configuration_CheckDimensionMemory(int *banksize);
extern void Configuration_CheckPeripheralSettings(void);
extern void Configuration_Load(const char *psFileName);
extern void Configuration_Save(void);
extern int  Configuration_SetInfoString(char *buffer, int len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
