/*
  Previous - bmap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the BMAP chip.
*/
const char Bmap_fileid[] = "Previous bmap.c";

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "sysReg.h"
#include "reset.h"
#include "bmap.h"

#define LOG_BMAP_LEVEL  LOG_DEBUG


static uae_u32 NEXTbmap[16];

#define BMAP_SID        0x0
#define BMAP_RCNTL      0x1
#define BMAP_BUSERR     0x2
#define BMAP_BURWREN    0x3
#define BMAP_TSTATUS1   0x4
#define BMAP_TSTATUS0   0x5
#define BMAP_ASCNTL     0x6
#define BMAP_ASEN       0x7
#define BMAP_STSAMPLE   0x8
#define BMAP_DDIR       0xC
#define BMAP_DRW        0xD
#define BMAP_AMR        0xE

/* Bits in ROM control */
#define BMAP_LO         0x40000000

/* Bits in DSP interrupt control */
#define BMAP_DSP_HREQ   0x20000000
#define BMAP_DSP_TXD    0x10000000

/* Bits in data dir */
#define BMAP_RESET      0x40000000

/* Bits in data RW */
#define BMAP_TPE_RXSEL  0x80000000
#define BMAP_HEARTBEAT  0x20000000
#define BMAP_TPE_ILBC   0x10000000
#define BMAP_TPE        (BMAP_TPE_RXSEL|BMAP_TPE_ILBC)

/* Externally accessible variables */
int bmap_rom_local   = 0;
int bmap_tpe_select  = 0;
int bmap_hreq_enable = 0;
int bmap_txdn_enable = 0;

static uae_u32 bmap_get(uae_u32 bmap_reg) {
    uae_u32 val = NEXTbmap[bmap_reg];
    
    Log_Printf(LOG_BMAP_LEVEL, "BMAP read register %d val=$%08x", bmap_reg, val);
    
    switch (bmap_reg) {
        case BMAP_DRW:
            /* This is for detecting thin wire ethernet.
             * It prevents from switching ethernet
             * transceiver to loopback mode.
             */            
            val &= ~(BMAP_HEARTBEAT | BMAP_TPE_ILBC);
            
            if (ConfigureParams.Ethernet.bEthernetConnected && ConfigureParams.Ethernet.bTwistedPair) {
                val |= BMAP_TPE_ILBC;
            } else {
                val |= BMAP_HEARTBEAT;
            }
            break;
            
        default:
            break;
    }
    
    return val;
}

static void bmap_put(uae_u32 bmap_reg, uae_u32 val) {
    Log_Printf(LOG_BMAP_LEVEL, "[BMAP] write register %d val=$%08x", bmap_reg, val);
    
    switch (bmap_reg) {
        case BMAP_RCNTL:
            if (!bmap_rom_local && (val&BMAP_LO)) {
                Log_Printf(LOG_WARN, "[BMAP] Enable local only.");
                bmap_rom_local = 1;
            } else if (bmap_rom_local && !(val&BMAP_LO)) {
                Log_Printf(LOG_WARN, "[BMAP] Disable local only.");
                bmap_rom_local = 0;
            }
            break;
        case BMAP_BURWREN:
            if (!bmap_hreq_enable && (val&BMAP_DSP_HREQ)) {
                Log_Printf(LOG_BMAP_LEVEL, "[BMAP] Enable DSP HREQ interrupt.");
                bmap_hreq_enable = 1;
            } else if (bmap_hreq_enable && !(val&BMAP_DSP_HREQ)) {
                Log_Printf(LOG_BMAP_LEVEL, "[BMAP] Disable DSP HREQ interrupt.");
                bmap_hreq_enable = 0;
            }
            if (!bmap_txdn_enable && (val&BMAP_DSP_TXD)) {
                Log_Printf(LOG_WARN, "[BMAP] Enable DSP TXD interrupt.");
                bmap_txdn_enable = 1;
            } else if (bmap_txdn_enable && !(val&BMAP_DSP_TXD)) {
                Log_Printf(LOG_WARN, "[BMAP] Disable DSP TXD interrupt.");
                bmap_txdn_enable = 0;
            }
            scr_check_dsp_interrupt();
            break;
        case BMAP_DDIR:
            if (val&BMAP_RESET) {
                Log_Printf(LOG_WARN, "[BMAP] CPU reset.");
                Reset_Warm();
            }
            break;
        case BMAP_DRW:
            if ((val&BMAP_TPE) != (NEXTbmap[bmap_reg]&BMAP_TPE)) {
                if ((val&BMAP_TPE)==BMAP_TPE) {
                    Log_Printf(LOG_WARN, "[BMAP] Switching to twisted pair ethernet.");
                    bmap_tpe_select = 1;
                } else if ((val&BMAP_TPE)==0) {
                    Log_Printf(LOG_WARN, "[BMAP] Switching to thin ethernet.");
                    bmap_tpe_select = 0;
                }
            }
            break;
            
        default:
            break;
    }
    
    NEXTbmap[bmap_reg] = val;
}


uae_u32 bmap_lget(uaecptr addr) {
    uae_u32 l;
    
    if (addr&3) {
        Log_Printf(LOG_WARN, "[BMAP] Unaligned access.");
        return 0;
    }
    
    l = bmap_get(addr>>2);

    return l;
}

uae_u32 bmap_wget(uaecptr addr) {
    uae_u32 w;
    int shift;
    
    if (addr&1) {
        Log_Printf(LOG_WARN, "[BMAP] Unaligned access.");
        return 0;
    }

    shift = (2 - (addr&2)) * 8;
    
    w = bmap_get(addr>>2);
    w >>= shift;
    w &= 0xFFFF;
    
    return w;
}

uae_u32 bmap_bget(uaecptr addr) {
    uae_u32 b;
    int shift;
    
    shift = (3 - (addr&3)) * 8;
    
    b = bmap_get(addr>>2);
    b >>= shift;
    b &= 0xFF;
    
    return b;
}

void bmap_lput(uaecptr addr, uae_u32 l) {
    if (addr&3) {
        Log_Printf(LOG_WARN, "[BMAP] Unaligned access.");
        return;
    }
    
    bmap_put(addr>>2, l);
}

void bmap_wput(uaecptr addr, uae_u32 w) {
    uae_u32 val;
    int shift;
    
    if (addr&1) {
        Log_Printf(LOG_WARN, "[BMAP] Unaligned access.");
        return;
    }

    shift = (2 - (addr&2)) * 8;
    
    val = NEXTbmap[addr>>2];
    val &= ~(0xFFFF << shift);
    val |= w << shift;
    
    bmap_put(addr>>2, val);
}

void bmap_bput(uaecptr addr, uae_u32 b) {
    uae_u32 val;
    int shift;
    
    shift = (3 - (addr&3)) * 8;
    
    val = NEXTbmap[addr>>2];
    val &= ~((uint32_t)0xFF << shift);
    val |= b << shift;
    
    bmap_put(addr>>2, val);
}

void BMAP_Reset(void) {
    int i;
    
    for (i = 0; i < 16; i++) {
        NEXTbmap[i] = 0;
    }
    NEXTbmap[BMAP_RCNTL] = BMAP_LO;
    
    bmap_rom_local   = 1;
    bmap_tpe_select  = 0;
    bmap_hreq_enable = 0;
    bmap_txdn_enable = 0;
}
