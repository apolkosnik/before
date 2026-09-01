/*
  Hatari

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Reset emulation state.
*/
const char Reset_fileid[] = "Hatari reset.c";

#include "main.h"
#include "configuration.h"
#include "timing.h"
#include "cycInt.h"
#include "m68000.h"
#include "reset.h"
#include "scc.h"
#include "tablet.h"
#include "screen.h"
#include "tmc.h"
#include "ncc.h"
#include "bmap.h"
#include "video.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "scsi.h"
#include "esp.h"
#include "mo.h"
#include "dma.h"
#include "sysReg.h"
#include "rtcnvram.h"
#include "ethernet.h"
#include "floppy.h"
#include "snd.h"
#include "printer.h"
#include "dsp.h"
#include "kms.h"
#include "keymap.h"
#include "NextBus.hpp"

/*-----------------------------------------------------------------------*/
/**
 * Reset NEXT emulator states, chips, interrupts and registers.
 */
static int Reset_NeXT(bool bCold)
{
	if (bCold) {
		int ret = memory_init();  /* Reset memory */
		if (ret) {
			return ret;
		}
		Keymap_Init();            /* Reset keymap */
		Timing_Reset();           /* Reset timing system */
		CycInt_Reset();           /* Reset interrupts */
		Video_Reset();            /* Reset video */
		Screen_Reset();           /* Reset screen */
		DebugCpu_SetDebugging();  /* Reset debugging flag if needed */
		DebugDsp_SetDebugging();  /* Reset debugging flag if needed */
		Main_SpeedReset();        /* Reset speed reporting system */
	}

	M68000_Reset();               /* Reset CPU */
	TMC_Reset();                  /* Reset TMC Registers */
	NCC_Reset();                  /* Reset NCC Registers and Cache */
	BMAP_Reset();                 /* Reset BMAP Registers */
	SCR_Reset();                  /* Reset System Control Registers */
	RTC_Reset();                  /* Reset RTC and NVRAM */
	DMA_Reset();                  /* Reset DMA controller */
	ESP_Reset();                  /* Reset SCSI controller */
	SCSI_Reset();                 /* Reset SCSI disks */
	MO_Reset();                   /* Reset MO disks */
	Floppy_Reset();               /* Reset Floppy disks */
	SCC_Reset();                  /* Reset SCC */
	Tablet_Reset();               /* Reset Tablet */
	Ethernet_Reset(true);         /* Reset Ethernet */
	KMS_Reset();                  /* Reset KMS */
	Sound_Reset();                /* Reset Sound */
	Printer_Reset();              /* Reset Printer */
	DSP_Reset();                  /* Reset DSP */
	NextBus_Reset();              /* Reset NextBus */

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Cold reset NeXT (reset memory, all registers and reboot)
 */
int Reset_Cold(void)
{
	return Reset_NeXT(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Warm reset NeXT (reset registers, leave in same state and reboot)
 */
int Reset_Warm(void)
{
	return Reset_NeXT(false);
}
