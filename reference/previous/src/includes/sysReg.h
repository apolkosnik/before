/*
  Previous - sysReg.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_SYSREG_H
#define PREV_SYSREG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined (_WIN32) && defined (scr1)
#undef scr1
#endif

/* NeXT system registers emulation */

/* Interrupts */
#define INT_SOFT1       0x00000001  /* level 1 */
#define INT_SOFT2       0x00000002  /* level 2 */
#define INT_POWER       0x00000004  /* level 3 */
#define INT_KEYMOUSE    0x00000008
#define INT_MONITOR     0x00000010
#define INT_VIDEO       0x00000020
#define INT_DSP_L3      0x00000040
#define INT_PHONE       0x00000080  /* Floppy */
#define INT_SOUND_OVRUN 0x00000100
#define INT_EN_RX       0x00000200
#define INT_EN_TX       0x00000400
#define INT_PRINTER     0x00000800
#define INT_SCSI        0x00001000
#define INT_DISK        0x00002000  /* in color systems this is INT_C16VIDEO */
#define INT_DSP_L4      0x00004000  /* level 4 */
#define INT_BUS         0x00008000  /* level 5 */
#define INT_REMOTE      0x00010000
#define INT_SCC         0x00020000
#define INT_R2M_DMA     0x00040000  /* level 6 */
#define INT_M2R_DMA     0x00080000
#define INT_DSP_DMA     0x00100000
#define INT_SCC_DMA     0x00200000
#define INT_SND_IN_DMA  0x00400000
#define INT_SND_OUT_DMA 0x00800000
#define INT_PRINTER_DMA 0x01000000
#define INT_DISK_DMA    0x02000000
#define INT_SCSI_DMA    0x04000000
#define INT_EN_RX_DMA   0x08000000
#define INT_EN_TX_DMA   0x10000000
#define INT_TIMER       0x20000000
#define INT_PFAIL       0x40000000  /* level 7 */
#define INT_NMI         0x80000000

/* Interrupt Level Masks */
#define INT_L7_MASK     0xC0000000
#define INT_L6_MASK     0x3FFC0000
#define INT_L5_MASK     0x00038000
#define INT_L4_MASK     0x00004000
#define INT_L3_MASK     0x00003FFC
#define INT_L2_MASK     0x00000002
#define INT_L1_MASK     0x00000001

#define SET_INT         1
#define RELEASE_INT     0

extern void set_interrupt(uint32_t intr, uint8_t state);
extern void scr_check_dsp_interrupt(void);

extern uint32_t scrIntStat;
extern uint32_t scrIntMask;
extern int scrIntLevel;

extern uint8_t dsp_intr_at_block_end;
extern uint8_t dsp_dma_unpacked;
extern uint8_t dsp_hreq_intr;
extern uint8_t dsp_txdn_intr;

/**
 * Return interrupt number (1 - 7), 0 means no interrupt.
 * Note that the interrupt stays pending if it can't be executed yet
 * due to the interrupt level field in the SR.
 */
static inline int intlev(void) {
    /* Poll interrupt level from interrupt status and mask registers
     * --> see sysReg.c
     */
    return scrIntLevel;
}

extern void SCR_Reset(void);

extern void SCR1_Read(void);

extern void SCR2_Read0(void);
extern void SCR2_Write0(void);
extern void SCR2_Read1(void);
extern void SCR2_Write1(void);
extern void SCR2_Read2(void);
extern void SCR2_Write2(void);
extern void SCR2_Read3(void);
extern void SCR2_Write3(void);

extern void IntRegStatRead(void);
extern void IntRegStatWrite(void);
extern void IntRegMaskRead(void);
extern void IntRegMaskWrite(void);

extern void Hardclock_Interrupt_Handler(void);
extern void HardclockRead0(void);
extern void HardclockRead1(void);

extern void HardclockWrite0(void);
extern void HardclockWrite1(void);

extern void HardclockWriteCSR(void);
extern void HardclockReadCSR(void);

extern void System_Timer_Read(void);
extern void System_Timer_Write(void);

extern void ColorVideo_CMD_Write(void);
extern void color_video_interrupt(void);

extern void Brightness_Write(void);

extern bool color_video_enabled(void);
extern bool brighness_video_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PREV_SYSREG_H */
