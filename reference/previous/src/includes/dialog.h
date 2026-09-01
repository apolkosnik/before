/*
  Previous - dialog.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_DIALOG_H
#define PREV_DIALOG_H

#include "configuration.h"

/* prototypes for gui-sdl/dlg*.c functions: */
extern int  Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot);
extern void Dialog_AboutDlg(void);
extern bool DlgAlert_Notice(const char *text);
extern bool DlgAlert_Query(const char *text);
extern void DlgFloppy_Main(void);
extern void DlgSCSI_Main(void);
extern void DlgOptical_Main(void);
extern void DlgEthernet_Main(void);
extern void DlgNFS(void);
extern bool DlgEthernetAdvanced_ConfigurePCAP(void);
extern void DlgEthernetAdvanced_ConfigureMAC(uint8_t *mac);
extern void DlgEthernetAdvanced_GetMAC(uint8_t *mac);
extern void DlgSound_Main(void);
extern void DlgPrinter_Main(void);
extern void Dialog_KeyboardDlg(void);
extern void Dialog_MouseDlg(void);
extern bool Dialog_MemDlg(void);
extern void Dialog_MemAdvancedDlg(int *membanks);
extern void Dialog_SystemDlg(void);
extern void Dialog_AdvancedDlg(void);
extern void Dialog_GraphicsCheckConsole(int board);
extern void Dialog_GraphicsDlg(void);
extern void Dialog_DimensionDlg(int board);
extern void DlgRom_Main(void);
extern void DlgBoot_Main(void);
extern void DlgMissing_Rom(const char* type, char *imgname, const char *defname, bool *enabled);
extern void DlgMissing_Dir(const char* type, char *dirname, const char *defname);
extern void DlgMissing_Disk(const char* type, int num, char *imgname, bool *ins, bool *wp);
/* and dialog.c */
extern bool Dialog_DoProperty(void);
extern void Dialog_CheckFiles(void);

#endif /* PREV_DIALOG_H */
