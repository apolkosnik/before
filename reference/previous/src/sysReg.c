/*
  Previous - sysReg.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the System Control Registers.
*/
const char SysReg_fileid[] = "Previous sysReg.c";

#include <inttypes.h>
#include "main.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "video.h"
#include "configuration.h"
#include "m68000.h"
#include "dsp.h"
#include "sysReg.h"
#include "rtcnvram.h"
#include "bmap.h"
#include "statusbar.h"
#include "timing.h"

#define LOG_SCR_LEVEL       LOG_DEBUG
#define LOG_HARDCLOCK_LEVEL LOG_DEBUG
#define LOG_SOFTINT_LEVEL   LOG_DEBUG
#define LOG_DSP_LEVEL       LOG_DEBUG


/* Results from real machines:
 *
 * NeXT Computer (CPU 68030 25 MHz, memory 100 nS, Memory size 64MB)
 * intrstat: ?
 * intrmask: ?
 * scr1:     00010102
 * scr2:     ?
 *
 * NeXTstation (CPU MC68040 25 MHz, memory 100 nS, Memory size 20MB)
 * intrstat: 00000020 (likely called while running boot animation)
 * intrmask: 88027640 (POT enabled)
 * scr1:     00011102 (likely new clock chip)
 * scr2:     00ff0c80
 *
 * NeXTstation with new clock chip (CPU MC68040 25 MHz, memory 100 nS, Memory size 20MB)
 * intrstat: 00001000 (likely no SCSI disk installed)
 * intrmask: 80027640 (no POT)
 * scr1:     00011102
 * scr2:     00ff0c80
 *
 * NeXTcube (CPU MC68040 25 MHz, memory 100 nS, Memory size 12MB)
 * intrstat: 00000000
 * intrmask: 80027640
 * scr1:     00012002
 * scr2:     00ff0c80
 *
 * NeXTstation Color (CPU MC68040 25 MHz, memory 100 nS, Memory size 32MB)
 * intrstat: 00000000
 * intrmask: 80027640
 * scr1:     00013002
 * scr2:     00000c80
 *
 * NeXTstation with Turbo chipset (CPU MC68040 25 MHz, memory 60 nS, Memory size 128MB)
 * intrstat: 00000000
 * intrmask: 00000000
 * scr1:     ffff4fce (really tmc scr1)
 * scr2:     00001080
 *
 * NeXTstation Turbo (CPU MC68040 33 MHz, memory 60 nS, Memory size 128MB)
 * intrstat: 00000000
 * intrmask: 00000000
 * scr1:     ffff4fcf (really tmc scr1)
 * scr2:     00001080
 * 200c000:  f0004000 (original scr1)
 *
 * NeXTstation Turbo Color (CPU MC68040 33 MHz, memory 70 nS, Memory size 128MB)
 * intrstat: 00000000
 * intrmask: 00000000
 * scr1:     ffff5fdf (really tmc scr1)
 * scr2:     00001080
 * 200c000:  f0004000 (original scr1)
 * 200c004:  f0004000 (address mask missing in Previous)
 *
 * intrmask after writing 00000000
 * non-Turbo:80027640
 * Turbo:    00000000
 *
 * intrmask after writing ffffffff
 * non-Turbo:ffffffff
 * Turbo:    3dd189ff
 */

static uint32_t scr1  = 0x00000000;

static uint8_t scr2_0 = 0x00;
static uint8_t scr2_1 = 0x00;
static uint8_t scr2_2 = 0x00;
static uint8_t scr2_3 = 0x00;

static uint8_t scr_have_dsp_memreset = 0;

static uint8_t col_vid_intr   = 0;
static uint8_t bright_reg     = 0;

static uint8_t  hardclock_csr     = 0;
static uint16_t hardclock_counter = 0;

static uint8_t scr_local_only = 0;

uint8_t dsp_dma_unpacked      = 0;
uint8_t dsp_intr_at_block_end = 0;
uint8_t dsp_hreq_intr         = 0;
uint8_t dsp_txdn_intr         = 0;

uint32_t scrIntStat = 0x00000000;
uint32_t scrIntMask = 0x00000000;

int scrIntLevel = 0;

/* System Control Register 1
 *
 * These values are valid for all non-Turbo systems:
 * -------- -------- -------- ------xx  bits 0:1   --> cpu speed
 * -------- -------- -------- ----xx--  bits 2:3   --> reserved
 * -------- -------- -------- --xx----  bits 4:5   --> main memory speed
 * -------- -------- -------- xx------  bits 6:7   --> video memory speed
 * -------- -------- ----xxxx --------  bits 8:11  --> board revision
 * -------- -------- xxxx---- --------  bits 12:15 --> cpu type
 * -------- xxxxxxxx -------- --------  bits 16:23 --> dma revision
 * ----xxxx -------- -------- --------  bits 24:27 --> reserved
 * xxxx---- -------- -------- --------  bits 28:31 --> slot id
 *
 * cpu speed:       0 = 40MHz, 1 = 20MHz, 2 = 25MHz, 3 = 33MHz
 * main mem speed:  0 = 120ns, 1 = 100ns, 2 = 80ns,  3 = 60ns
 * video mem speed: 0 = 120ns, 1 = 100ns, 2 = 80ns,  3 = 60ns
 * board revision:  for 030 Cube:
 *                  0 = DCD input inverted
 *                  1 = DCD polarity fixed
 *                  2 = must disable DSP mem before reset
 *                  for 040 non-Turbo:
 *                  0 = original
 *                  1 = cost reduced, 5K gate array, new clock chip
 * cpu type:        0 = NeXT Computer (68030)
 *                  1 = NeXTstation monochrome
 *                  2 = NeXTcube
 *                  3 = NeXTstation color
 *                  4 = all Turbo systems
 * dma revision:    1 on all systems ?
 * slot id:         f on Turbo systems (cube too?), 0 on other systems
 *
 *
 * These bits are always 0 on all Turbo systems:
 * ----xxxx xxxxxxxx ----xxxx xxxxxxxx
 */

#define SLOT_ID      0

#define DMA_REVISION 1

#define TYPE_NEXT    0
#define TYPE_SLAB    1
#define TYPE_CUBE    2
#define TYPE_COLOR   3
#define TYPE_TURBO   4

#define BOARD_REV0   0
#define BOARD_REV1   1
#define BOARD_REV2   2

#define MEM_120NS    0
#define MEM_100NS    0
#define MEM_80NS     2
#define MEM_60NS     3

#define CPU_16MHZ    0
#define CPU_20MHZ    1
#define CPU_25MHZ    2
#define CPU_33MHZ    3

void SCR_Reset(void) {
    uint8_t system_type = 0;
    uint8_t board_rev = 0;
    uint8_t cpu_speed = 0;
    uint8_t memory_speed = 0;
    
    scr_local_only = 1;
    hardclock_csr = 0;
    hardclock_counter = 0xFFFF;
    col_vid_intr = 0;
    bright_reg = 0;
    dsp_intr_at_block_end = 0;
    dsp_dma_unpacked = 0;
    dsp_hreq_intr = 0;
    dsp_txdn_intr = 0;
    
    scr1=0x00000000;
    scr2_0=0x00;
    scr2_1=0x00;
    scr2_2=0x00;
    scr2_3=0x00;
    scrIntStat=0x00000000;
    scrIntMask=0x00000000;
    scrIntLevel=0;
    
    Statusbar_SetSystemLed(false);
    rtc_interface_reset();
    
    /* Turbo */
    if (ConfigureParams.System.bTurbo) {
        scr2_2=0x10; /* video mode is 25 MHz */
        scr2_3=0x80; /* local only resets to 1 */
        
        scr_have_dsp_memreset = 1;
        
        if (ConfigureParams.System.nMachineType==NEXT_STATION) {
            scr1 |= (uint32_t)0xF<<28;
        } else {
            scr1 |= (uint32_t)SLOT_ID<<28;
        }
        scr1 |= TYPE_TURBO<<12;
        return;
    }
    
    /* Non-Turbo */
    scr1 |= SLOT_ID<<28;
    scr1 |= DMA_REVISION<<16;
    
    switch (ConfigureParams.System.nMachineType) {
        case NEXT_CUBE030:
            system_type = TYPE_NEXT;
            board_rev   = BOARD_REV1;
            break;
        case NEXT_CUBE040:
            system_type = TYPE_CUBE;
            board_rev   = (ConfigureParams.System.nRTC == MCCS1850) ? BOARD_REV1 : BOARD_REV0;
            break;
        case NEXT_STATION:
            if (ConfigureParams.System.bColor) {
                system_type = TYPE_COLOR;
                board_rev   = (ConfigureParams.System.nRTC == MCCS1850) ? BOARD_REV1 : BOARD_REV0;
            } else {
                system_type = TYPE_SLAB;
                board_rev   = (ConfigureParams.System.nRTC == MCCS1850) ? BOARD_REV1 : BOARD_REV0;
            }
            break;
        default:
            break;
    }
    scr1 |= system_type<<12;
    scr1 |= board_rev<<8;
    
    scr1 |= MEM_100NS<<6; /* video memory */
    
    switch (ConfigureParams.Memory.nMemorySpeed) {
        case MEMORY_60NS:  memory_speed = MEM_60NS;  break;
        case MEMORY_80NS:  memory_speed = MEM_80NS;  break;
        case MEMORY_100NS:
        case MEMORY_120NS: memory_speed = MEM_120NS; break;
        default: break;
    }
    scr1 |= memory_speed<<4; /* main memory */

    if (ConfigureParams.System.nCpuFreq<20) {
        cpu_speed = CPU_16MHZ;
    } else if (ConfigureParams.System.nCpuFreq<25) {
        cpu_speed = CPU_20MHZ;
    } else if (ConfigureParams.System.nCpuFreq<33) {
        cpu_speed = CPU_25MHZ;
    } else {
        cpu_speed = CPU_33MHZ;
    }
    scr1 |= cpu_speed;

    if (system_type != TYPE_NEXT || board_rev > BOARD_REV1) {
        scr_have_dsp_memreset = 1;
    } else {
        scr_have_dsp_memreset = 0;
    }
}

void SCR1_Read(void)
{
    Log_Printf(LOG_SCR_LEVEL,"SCR1 read at $%08x PC=$%08x\n", IoAccessCurrentAddress,m68k_getpc());
    IoMem_WriteLong(IoAccessCurrentAddress, scr1);
}


/* System Control Register 2 
 
 s_dsp_reset : 1,
 s_dsp_block_end : 1,
 s_dsp_unpacked : 1,
 s_dsp_mode_B : 1,
 s_dsp_mode_A : 1,
 s_remote_int : 1,
 s_local_int : 2,
 s_dram_256K : 4,
 s_dram_1M : 4,
 s_timer_on_ipl7 : 1,
 s_rom_wait_states : 3,
 s_rom_1M : 1,
 s_rtdata : 1,
 s_rtclk : 1,
 s_rtce : 1,
 s_rom_overlay : 1,
 s_dsp_int_en : 1,
 s_dsp_mem_en : 1,
 s_reserved : 4,
 s_led : 1;
 
 */

/* byte 0 */
#define SCR2_DSP_RESET      0x80
#define SCR2_DSP_BLK_END    0x40
#define SCR2_DSP_UNPKD      0x20
#define SCR2_DSP_MODE_B     0x10
#define SCR2_DSP_MODE_A     0x08
#define SCR2_SOFTINT2       0x02
#define SCR2_SOFTINT1       0x01

/* byte 1 */
#define SCR2_DSP_TXD_EN     0x80 /* only on Turbo */

/* byte 2 */
#define SCR2_TIMERIPL7      0x80
#define SCR2_RTDATA         0x04
#define SCR2_RTCLK          0x02
#define SCR2_RTCE           0x01

/* byte 3 */
#define SCR2_ROM            0x80
#define SCR2_DSP_INT_EN     0x40 /* only on non-Turbo */
#define SCR2_DSP_MEM_EN     0x20 /* inverted on non-Turbo */
#define SCR2_LED            0x01


void SCR2_Write0(void)
{
    uint8_t changed_bits=scr2_0;
    Log_Printf(LOG_SCR_LEVEL,"SCR2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    scr2_0 = IoMem_ReadByte(IoAccessCurrentAddress);
    changed_bits ^= scr2_0;
    
    if (changed_bits&SCR2_SOFTINT1) {
        Log_Printf(LOG_SOFTINT_LEVEL,"[SCR2] SOFTINT1 change at $%08x val=%x PC=$%08x\n",
                   IoAccessCurrentAddress,scr2_0&SCR2_SOFTINT1,m68k_getpc());
        if (scr2_0&SCR2_SOFTINT1)
            set_interrupt(INT_SOFT1,SET_INT);
        else
            set_interrupt(INT_SOFT1,RELEASE_INT);
    }
    
    if (changed_bits&SCR2_SOFTINT2) {
        Log_Printf(LOG_SOFTINT_LEVEL,"[SCR2] SOFTINT2 change at $%08x val=%x PC=$%08x\n",
                           IoAccessCurrentAddress,scr2_0&SCR2_SOFTINT2,m68k_getpc());
        if (scr2_0&SCR2_SOFTINT2) 
            set_interrupt(INT_SOFT2,SET_INT);
        else
            set_interrupt(INT_SOFT2,RELEASE_INT);
    }
    
    /* DSP bits */
    if (changed_bits&SCR2_DSP_RESET) {
        if (scr2_0&SCR2_DSP_RESET) {
            if (!(scr2_0&SCR2_DSP_MODE_A)) {
                Log_Printf(LOG_DSP_LEVEL,"[SCR2] DSP Mode A");
            }
            if (!(scr2_0&SCR2_DSP_MODE_B)) {
                Log_Printf(LOG_DSP_LEVEL,"[SCR2] DSP Mode B");
            }
            Log_Printf(LOG_DSP_LEVEL,"[SCR2] DSP Start (mode %i)",(~(scr2_0>>3))&3);
            DSP_Start((~(scr2_0>>3))&3);
            if (!scr_have_dsp_memreset) {
                Log_Printf(LOG_WARN,"[SCR2] Enable DSP memory");
                DSP_EnableMemory();
            }
        } else {
            Log_Printf(LOG_DSP_LEVEL,"[SCR2] DSP Reset");
            DSP_Reset();
            if (!scr_have_dsp_memreset) {
                Log_Printf(LOG_WARN,"[SCR2] Disable DSP memory");
                DSP_DisableMemory();
            }
        }
    }
    if (changed_bits&SCR2_DSP_BLK_END) {
        dsp_intr_at_block_end = scr2_0&SCR2_DSP_BLK_END;
        Log_Printf(LOG_DSP_LEVEL,"[SCR2] %s DSP interrupt from DMA at block end",dsp_intr_at_block_end?"enable":"disable");
    }
    if (changed_bits&SCR2_DSP_UNPKD) {
        dsp_dma_unpacked = scr2_0&SCR2_DSP_UNPKD;
        Log_Printf(LOG_DSP_LEVEL,"[SCR2] %s DSP DMA unpacked mode",dsp_dma_unpacked?"enable":"disable");
    }
}

void SCR2_Read0(void)
{
    Log_Printf(LOG_SCR_LEVEL,"SCR2 read at $%08x PC=$%08x\n", IoAccessCurrentAddress,m68k_getpc());
    IoMem_WriteByte(IoAccessCurrentAddress, scr2_0);
}

void SCR2_Write1(void)
{
    uint8_t changed_bits=scr2_1;
    Log_Printf(LOG_SCR_LEVEL,"SCR2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    scr2_1 = IoMem_ReadByte(IoAccessCurrentAddress);
    changed_bits^=scr2_1;
    
    if (ConfigureParams.System.bTurbo) {
        if (changed_bits&SCR2_DSP_TXD_EN) {
            Log_Printf(LOG_WARN,"[SCR2] %s DSP TXD interrupt",(scr2_1&SCR2_DSP_TXD_EN)?"enable":"disable");
            scr_check_dsp_interrupt();
        }
    }
}

void SCR2_Read1(void)
{
    Log_Printf(LOG_SCR_LEVEL,"SCR2 read at $%08x PC=$%08x\n", IoAccessCurrentAddress,m68k_getpc());
    IoMem_WriteByte(IoAccessCurrentAddress, scr2_1);
}

void SCR2_Write2(void)
{
    uint8_t changed_bits=scr2_2;
    Log_Printf(LOG_SCR_LEVEL,"SCR2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    scr2_2 = IoMem_ReadByte(IoAccessCurrentAddress);
    changed_bits^=scr2_2;
    
    if (changed_bits&SCR2_TIMERIPL7) {
        Log_Printf(LOG_WARN,"[SCR2] TIMER IPL7 change at $%08x val=%x PC=$%08x\n",
                   IoAccessCurrentAddress,scr2_2&SCR2_TIMERIPL7,m68k_getpc());
    }

    /* RTC enabled */
    if (scr2_2&SCR2_RTCE) {
        if (changed_bits&SCR2_RTCE) {
            rtc_interface_start();
        }
        if ((changed_bits&SCR2_RTCLK) && !(scr2_2&SCR2_RTCLK)) {
            rtc_interface_write(scr2_2&SCR2_RTDATA);
        }
    } else if (changed_bits&SCR2_RTCE) {
        rtc_interface_reset();
    }
}

void SCR2_Read2(void)
{
    Log_Printf(LOG_SCR_LEVEL,"SCR2 read at $%08x PC=$%08x\n", IoAccessCurrentAddress,m68k_getpc());
    if (rtc_interface_read()) {
        scr2_2 |= SCR2_RTDATA;
    } else {
        scr2_2 &= ~SCR2_RTDATA;
    }
    IoMem_WriteByte(IoAccessCurrentAddress, scr2_2);
}

void SCR2_Write3(void)
{    
    uint8_t changed_bits=scr2_3;
    Log_Printf(LOG_SCR_LEVEL,"SCR2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    scr2_3 = IoMem_ReadByte(IoAccessCurrentAddress);
    changed_bits^=scr2_3;
    
    if (changed_bits&SCR2_ROM) {
        scr_local_only=scr2_3&SCR2_ROM;
        if (!ConfigureParams.System.bTurbo) {
            scr_local_only^=SCR2_ROM;
        }
        Log_Printf(LOG_WARN,"[SCR2] %s local only",scr_local_only?"Enable":"Disable");
    }
    if (changed_bits&SCR2_LED) {
        Log_Printf(LOG_DEBUG,"[SCR2] %s LED",(scr2_3&SCR2_LED)?"Enable":"Disable");
        Statusbar_SetSystemLed(scr2_3&SCR2_LED);
    }
    
    if (!ConfigureParams.System.bTurbo) {
        if (changed_bits&SCR2_DSP_INT_EN) {
            Log_Printf(LOG_DSP_LEVEL,"[SCR2] DSP interrupt at level %i",(scr2_3&SCR2_DSP_INT_EN)?4:3);
            if (scrIntStat&(INT_DSP_L3|INT_DSP_L4)) {
                Log_Printf(LOG_DSP_LEVEL,"[SCR2] Switching DSP interrupt to level %i",(scr2_3&SCR2_DSP_INT_EN)?4:3);
                set_interrupt(INT_DSP_L3|INT_DSP_L4, RELEASE_INT);
                scr_check_dsp_interrupt();
            }
        }
    }
    if (changed_bits&SCR2_DSP_MEM_EN) {
        if (ConfigureParams.System.bTurbo) {
            Log_Printf(LOG_WARN,"[SCR2] %s DSP memory",(scr2_3&SCR2_DSP_MEM_EN)?"enable":"disable");
            if (scr2_3&SCR2_DSP_MEM_EN) {
                DSP_EnableMemory();
            } else {
                DSP_DisableMemory();
            }
       } else if (scr_have_dsp_memreset) {
            Log_Printf(LOG_WARN,"[SCR2] %s DSP memory",(scr2_3&SCR2_DSP_MEM_EN)?"disable":"enable");
            if (scr2_3&SCR2_DSP_MEM_EN) {
                DSP_DisableMemory();
            } else {
                DSP_EnableMemory();
            }
        }
    }
}

void SCR2_Read3(void)
{
    Log_Printf(LOG_SCR_LEVEL,"SCR2 read at $%08x PC=$%08x\n", IoAccessCurrentAddress,m68k_getpc());
    IoMem_WriteByte(IoAccessCurrentAddress, scr2_3);
}


/* Interrupt Status Register */

void IntRegStatRead(void) {
    IoMem_WriteLong(IoAccessCurrentAddress, scrIntStat);
}

void IntRegStatWrite(void) {
    Log_Printf(LOG_WARN, "[INT] Interrupt status register is read-only.");
}


/* DSP interrupt */
void scr_check_dsp_interrupt(void) {
    uint8_t state = RELEASE_INT;
    
    if (ConfigureParams.System.bTurbo) {
        state = dsp_hreq_intr;
        if (scr2_1&SCR2_DSP_TXD_EN) {
            state |= dsp_txdn_intr;
        }
        set_interrupt(INT_DSP_L4, state);
    } else {
        if (scr2_3&SCR2_DSP_INT_EN) {
            if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
                state = dsp_hreq_intr | dsp_txdn_intr;
            } else {
                state = (dsp_hreq_intr & bmap_hreq_enable) | (dsp_txdn_intr & bmap_txdn_enable);
            }
            set_interrupt(INT_DSP_L4, state);
        } else {
            state = dsp_hreq_intr; /* diagnostics expect this */
            set_interrupt(INT_DSP_L3, state);
        }
    }
}

/* Set interrupt level from interrupt status and mask registers */
static inline void scr_get_interrupt_level(void) {
    uint32_t interrupt = scrIntStat&scrIntMask;
    
    if (!interrupt) {
        scrIntLevel = 0;
    } else if (interrupt&INT_L7_MASK) {
        scrIntLevel = 7;
    } else if ((interrupt&INT_TIMER) && (scr2_2&SCR2_TIMERIPL7)) {
        scrIntLevel = 7;
    } else if (interrupt&INT_L6_MASK) {
        scrIntLevel = 6;
    } else if (interrupt&INT_L5_MASK) {
        scrIntLevel = 5;
    } else if (interrupt&INT_L4_MASK) {
        scrIntLevel = 4;
    } else if (interrupt&INT_L3_MASK) {
        scrIntLevel = 3;
    } else if (interrupt&INT_L2_MASK) {
        scrIntLevel = 2;
    } else if (interrupt&INT_L1_MASK) {
        scrIntLevel = 1;
    } else {
        scrIntLevel = 0;
    }
    M68000_CheckInterrupt();
}

/* Set or clear interrupt bits in the interrupt status register */
void set_interrupt(uint32_t intr, uint8_t state) {
    if (state==SET_INT) {
        scrIntStat |= intr;
    } else {
        scrIntStat &= ~intr;
    }
    scr_get_interrupt_level();
}

/* Interrupt Mask Register */
#define INT_NONMASKABLE 0x80027640
#define INT_ZEROBITS    0xC22E7600 /* Turbo */

void IntRegMaskRead(void) {
    if (ConfigureParams.System.bTurbo) {
        IoMem_WriteLong(IoAccessCurrentAddress, scrIntMask&~INT_ZEROBITS);
    } else {
        IoMem_WriteLong(IoAccessCurrentAddress, scrIntMask);
    }
}

void IntRegMaskWrite(void) {
    scrIntMask = IoMem_ReadLong(IoAccessCurrentAddress);
    if (ConfigureParams.System.bTurbo) {
        scrIntMask |= INT_ZEROBITS;
    } else {
        scrIntMask |= INT_NONMASKABLE;
    }
    scr_get_interrupt_level();
    
    Log_Printf(LOG_DEBUG, "Interrupt mask: %08x", scrIntMask);
}


/* Hardclock internal interrupt */

#define HARDCLOCK_ENABLE 0x80
#define HARDCLOCK_LATCH  0x40
#define HARDCLOCK_ZERO   0x3F

static uint16_t hardclock_latch;
static uint64_t hardclock_last_time;

void Hardclock_Interrupt_Handler(void) {
    uint64_t now;
    set_interrupt(INT_TIMER, SET_INT);
    now = Timing_GetTime();
    Log_Printf(LOG_HARDCLOCK_LEVEL, "[Hardclock] Interrupting at %"PRIu64" us", now);
    Timing_Hardclock(hardclock_counter, (int)(now - hardclock_last_time));
    hardclock_counter = hardclock_latch;
    if (hardclock_counter) {
        hardclock_last_time = now;
        CycInt_UpdateTimeEvent(hardclock_counter, 0, EVENT_HARDCLOCK_INTERRUPT);
    }
}

void HardclockRead0(void) {
    if ((hardclock_csr & HARDCLOCK_ENABLE) && hardclock_counter) {
        uint64_t diff = Timing_GetTime() - hardclock_last_time;
        hardclock_counter = (diff > hardclock_counter) ? 0 : (hardclock_counter - diff);
        Log_Printf(LOG_WARN, "[Hardclock] Active counter read %u", hardclock_counter);
    }
    IoMem_WriteByte(IoAccessCurrentAddress, hardclock_counter >> 8);
    Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Read at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
}
void HardclockRead1(void) {
    IoMem_WriteByte(IoAccessCurrentAddress, hardclock_counter & 0xFF);
    Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Read at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
}

void HardclockWrite0(void) {
    Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Write at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    hardclock_latch &= 0x00FF;
    hardclock_latch |= (uint16_t)IoMem_ReadByte(IoAccessCurrentAddress) << 8;
}
void HardclockWrite1(void) {
    Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Write at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    hardclock_latch &= 0xFF00;
    hardclock_latch |= IoMem_ReadByte(IoAccessCurrentAddress);
}

void HardclockWriteCSR(void) {
    uint8_t changed_bits = hardclock_csr;
    Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Write at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    hardclock_csr = IoMem_ReadByte(IoAccessCurrentAddress);
    changed_bits ^= hardclock_csr;
    if (hardclock_csr & HARDCLOCK_LATCH) {
        hardclock_counter = hardclock_latch;
    }
    if ((hardclock_csr & HARDCLOCK_ENABLE) && ((hardclock_csr & HARDCLOCK_LATCH) || (changed_bits & HARDCLOCK_ENABLE))) {
        Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Enable periodic interrupt (%d microseconds).", hardclock_counter);
        if (hardclock_counter) {
            hardclock_last_time = Timing_GetTime();
            CycInt_AddTimeEvent(hardclock_counter, 0, EVENT_HARDCLOCK_INTERRUPT);
        }
    } else if (!(hardclock_csr & HARDCLOCK_ENABLE) && (changed_bits & HARDCLOCK_ENABLE)) {
        uint64_t diff = Timing_GetTime() - hardclock_last_time;
        hardclock_counter = (diff > hardclock_counter) ? 0 : (hardclock_counter - diff);
        Log_Printf(LOG_HARDCLOCK_LEVEL,"[Hardclock] Disable periodic interrupt.");
        CycInt_RemovePendingEvent(EVENT_HARDCLOCK_INTERRUPT);
    }
    hardclock_csr &= ~HARDCLOCK_LATCH;
    set_interrupt(INT_TIMER, RELEASE_INT);
}
void HardclockReadCSR(void) {
    IoMem_WriteByte(IoAccessCurrentAddress, hardclock_csr);
    Log_Printf(LOG_DEBUG, "[Hardclock] Read at $%08x val=%02x PC=$%08x", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),m68k_getpc());
    set_interrupt(INT_TIMER, RELEASE_INT);
}


/* Event counter register */

static uint64_t eventcounter_offset;

void System_Timer_Read(void) {
    IoMem_WriteLong(IoAccessCurrentAddress, (Timing_GetTime() - eventcounter_offset) & 0xFFFFF);
}

void System_Timer_Write(void) {
    eventcounter_offset = Timing_GetTime(); /* Reset event counter (undocumented but required by dvt040) */
}

/* Color Video Interrupt Register */

#define VID_CMD_CLEAR_INT    0x01
#define VID_CMD_ENABLE_INT   0x02
#define VID_CMD_UNBLANK      0x04

void ColorVideo_CMD_Write(void) {
    col_vid_intr = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_DEBUG,"[Color Video] Command write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    if (col_vid_intr&VID_CMD_CLEAR_INT) {
        set_interrupt(INT_DISK, RELEASE_INT);
    }
}

bool color_video_enabled(void) {
    return (col_vid_intr&VID_CMD_UNBLANK);
}

void color_video_interrupt(void) {
    if (col_vid_intr&VID_CMD_ENABLE_INT) {
        set_interrupt(INT_DISK, SET_INT);
        col_vid_intr &= ~VID_CMD_ENABLE_INT;
    }
}

/* Brightness Register */

#define BRIGHTNESS_UNBLANK 0x40
#define BRIGHTNESS_MASK    0x3F

bool brighness_video_enabled(void) {
    return (bright_reg&BRIGHTNESS_UNBLANK);
}

void Brightness_Write(void) {
    bright_reg = IoMem_ReadBytePort();
    
    Log_Printf(LOG_DEBUG,"[Brightness] Write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    if (bright_reg&BRIGHTNESS_UNBLANK) {
        Log_Printf(LOG_WARN,"[Brightness] Setting brightness to %02x\n", bright_reg&BRIGHTNESS_MASK);
    }
}
