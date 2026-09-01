/*
  Previous - scc.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the AMD AM8530H Serial Communication
  Controller. Incomplete.
*/
const char Scc_fileid[] = "Previous scc.c";

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "configuration.h"
#include "scc.h"
#include "sysReg.h"
#include "dma.h"
#include "tablet.h"

#define LOG_SCC_LEVEL     LOG_DEBUG
#define LOG_SCC_IO_LEVEL  LOG_DEBUG
#define LOG_SCC_REG_LEVEL LOG_DEBUG


/* SCC Registers */
struct {
    uint8_t rreg[16];
    uint8_t wreg[16];
    
    uint8_t data;
    uint8_t clock;
} scc[2];

uint8_t scc_register_pointer = 0;


/* Read Registers */
#define R_STATUS    0   /* Transmit/Receive buffer status and External status */
#define R_SPECSTAT  1   /* Special receive condition status */
#define R_INTVEC    2   /* Interrupt vector */
#define R_INTBITS   3   /* Interrupt pending bits */
#define R_RECBUF    8   /* Receive buffer */
#define R_MISCSTAT  10  /* Miscellaneous status */
#define R_BRG_LOW   12  /* Lower byte of baud rate generator time constant */
#define R_BRG_HIGH  13  /* Upper byte of baud rate generator time constant */
#define R_EXTSTAT   15  /* External/Status interrupt information */

/* Write Registers */
#define W_INIT      0   /* Initialization commands */
#define W_MODE      1   /* Transmit/Receive interrupt and data transfer mode */
#define W_INTVEC    2   /* Interrupt vector */
#define W_RECCONT   3   /* Receive parameters and control */
#define W_MISCMODE  4   /* Transmit/Receive miscellaneous parameters and modes */
#define W_TRANSCONT 5   /* Transmit parameters and controls */
#define W_SYNCCHARA 6   /* Sync characters or SDLC address field */
#define W_SYNCCHARF 7   /* Sync character or SDLC flag */
#define W_TRANSBUF  8   /* Transmit buffer */
#define W_MASTERINT 9   /* Master interrupt control and reset */
#define W_MISCCONT  10  /* Miscellaneous transmitter/receiver control bits */
#define W_CLOCK     11  /* Clock mode control */
#define W_BRG_LOW   12  /* Lower byte of baud rate generator time constant */
#define W_BRG_HIGH  13  /* Upper byte of baud rate generator time constant */
#define W_MISC      14  /* Miscellaneous control bits */
#define W_EXTSTAT   15  /* External/Status interrupt control */


/* SCC clock select register (0x02018004) */
#define PCLK_ESCLK      0x00000010
#define PCLK_3684_MHZ   0x00000000
#define SCLKB_4_MHZ     0x00000008
#define SCLKB_ESCLK     0x00000004
#define SCLKB_3684_MHZ  0x00000000
#define SCLKA_4_MHZ     0x00000002
#define SCLKA_ESCLK     0x00000001
#define SCLKA_3684_MHZ  0x00000000

/* Read register 0 */
#define RR0_RXAVAIL     0x01        /* Rx Character Available */
#define RR0_ZERO_COUNT  0x02        /* Zero count (timer) */
#define RR0_TXEMPTY     0x04        /* Tx Buffer Empty */
#define RR0_DCD         0x08        /* DCD changed */
#define RR0_SYNC_HUNT   0x10        /* Sync hunt mode */
#define RR0_CTS         0x20        /* CTS changed */
#define RR0_TXUNDER     0x40        /* Tx Underrun/EOM */
#define RR0_BREAK       0x80        /* Break/Abort */

/* Read register 1 */
#define RR1_ALLSENT     0x01        /* All Sent */
#define RR1_RESCODE2    0x02        /* Residue Code 2 */
#define RR1_RESCODE1    0x04        /* Residue Code 1 */
#define RR1_RESCODE0    0x08        /* Residue Code 0 */
#define RR1_PARITY      0x10        /* Parity Error */
#define RR1_RXOVER      0x20        /* Rx Overrun Error */
#define RR1_FRAME       0x40        /* CRC/Framing Error */
#define RR1_EOF         0x80        /* End of Frame (SDLC) */

/* Read/Write Register 2: 8 bits of interrupt vector
 * Channel A unmodified, channel B is modified by current UART status */

/* Read Register 3 -- ONLY CHANNEL A */
#define RR3_B_STATIP    0x01        /* Chanel B Ext/Status intr pending */
#define RR3_B_TXIP      0x02        /* Chanel B Tx interrupt pending */
#define RR3_B_RXIP      0x04        /* Chanel B Rx interrupt pending */
#define RR3_A_STATIP    0x08        /* Chanel A Ext/Status intr pending */
#define RR3_A_TXIP      0x10        /* Chanel A Tx interrupt pending */
#define RR3_A_RXIP      0x20        /* Chanel A Rx interrupt pending */

#define RR3_A_IP    (RR3_A_STATIP|RR3_A_TXIP|RR3_A_RXIP)
#define RR3_B_IP    (RR3_B_STATIP|RR3_B_TXIP|RR3_B_RXIP)

/* Read Register 8: same as data port (receive buffer) */

/* Read Register 10 */
#define RR10_ONLOOP     0x02        /* On Loop */
#define RR10_LOOPSEND   0x10        /* Loop Sending */
#define RR10_MISS2      0x40        /* Two Clocks Missing */
#define RR10_MISS1      0x80        /* One Clock Missing */

/* Read/Write Register 12/13 16 bits of time constant */

/* Read/Write Register 15 */
#define RW15_ZEROCNTIE  0x02        /* Enable interrupt on timer zero */
#define RW15_DCDIE      0x08        /* Enable interrupt on DCD change */
#define RW15_SYNCIE     0x10        /* Enable interrupt on Sync Hunt */
#define RW15_CTSIE      0x20        /* Enable interrupt on CTS change */
#define RW15_TXUNDERIE  0x40        /* Enable interrupt on Tx Underrun/EOM*/
#define RW15_BREAKIE    0x80        /* Enable interrupt on Bread/Abort */

/* Write register 0 */
#define WR0_REGMASK     0x0F        /* Mask for register select */
#define WR0_RESET_STAT  0x10        /* Reset Ext/Status Interrupts */
#define WR0_ABORT       0x18        /* Send abort */
#define WR0_NEXTRXIE    0x20        /* Enable interrupt on next char Rx */
#define WR0_RESETTXPEND 0x28        /* Reset Pending Tx Interrupt */
#define WR0_RESET       0x30        /* Error Reset */
#define WR0_RESETIUS    0x38        /* Reset Interrupt Under Service */
#define WR0_RESETRXCRC  0x40        /* Reset Rx CRC Checker */
#define WR0_RESETTXCRC  0x80        /* Reset Rx CRC Generator */
#define WR0_RESETEOM    0xC0        /* Reset Underrun/EOM latch */

/* Write register 1 */
#define WR1_EXTIE       0x01        /* External Interrupt Enable */
#define WR1_TXIE        0x02        /* Transmit Interrupt Enable */
#define WR1_PARSPEC     0x04        /* Parity is Special Condition */
#define WR1_RXFIRSTIE   0x08        /* Interrupt Enable on First Rx */
#define WR1_RXALLIE     0x10        /* Interrupt Enable on ALL Rx */
#define WR1_SPECIE      0x18        /* Interrupt on Special only */
#define WR1_REQRX       0x20        /* Request on Rx (else Tx) */
#define WR1_REQFUNC     0x40        /* DMA Request (else cpu wait) */
#define WR1_REQENABLE   0x80        /* Request/Wait enable */

/* Read/Write register 2, interrupt vector */

/* Write register 3 */
#define WR3_RXENABLE    0x01        /* Rx Enable */
#define WR3_SYNCINHIB   0x02        /* Sync Character Load Inhibit */
#define WR3_ADDRSRCH    0x04        /* Address search mode (SDLC) */
#define WR3_RXCRCENABLE 0x08        /* Rx CRC enable */
#define WR3_ENTERHUNT   0x10        /* Enter Hunt Mode */
#define WR3_AUTOENABLES 0x20        /* Auto Enables */
#define WR3_RX5         0x00        /* Rx 5 bit characters */
#define WR3_RX7         0x40        /* Rx 7 bit characters */
#define WR3_RX6         0x80        /* Rx 6 bit characters */
#define WR3_RX8         0xC0        /* Rx 8 bit characters */

/* Write register 4 */
#define WR4_PARENABLE   0x01        /* Parity enable */
#define WR4_PAREVEN     0x02        /* Even parity */
#define WR4_STOP1       0x04        /* 1 stop bit */
#define WR4_STOP15      0x08        /* 1.5 stop bits */
#define WR4_STOP2       0x0C        /* 2 stop bits */
#define WR4_SYNC8       0x00        /* 8 bit sync character */
#define WR4_SYNC16      0x10        /* 16 bit sync character */
#define WR4_SDLC        0x10        /* SDLC mode */
#define WR4_EXTSYNC     0x30        /* External sync */
#define WR4_X1CLOCK     0x00        /* x1 clock mode */
#define WR4_X16CLOCK    0x40        /* x16 clock mode */
#define WR4_X32CLOCK    0x80        /* x32 clock mode */
#define WR4_X64CLOCK    0xC0        /* x64 clock mode */

/* Write register 5 */
#define WR5_TXCRCENABLE 0x01        /* Enable CRC on Rx */
#define WR5_RTS         0x02        /* RTS */
#define WR5_CRC16       0x04        /* SDLC/CRC-16 */
#define WR5_TXENABLE    0x08        /* Enable transmitter */
#define WR5_BREAK       0x10        /* Send a break */
#define WR5_TX5         0x00        /* Tx 5 bit characters */
#define WR5_TX7         0x20        /* Tx 7 bit characters */
#define WR5_TX6         0x40        /* Tx 6 bit characters */
#define WR5_TX8         0x60        /* Tx 8 bit characters */
#define WR5_DTR         0x80        /* DTR */

/* Write register 6 (Sync characters or SDLC address) */

/* Write register 7 more sync/SDLC */

/* Write register 8, same as data port, (Tx buffer) */

/* Write register 9 -- ONLY 1, SHARED BETWEEN CHANNELS, ACCESSABLE FROM BOTH */
#define WR9_VIS         0x01        /* Vector Includes Status */
#define WR9_NV          0x02        /* No Vector (don't respond to IACK) */
#define WR9_DLC         0x04        /* Disable Lower Chain */
#define WR9_MIE         0x08        /* Master Interrupt Enable */
#define WR9_STATHIGH    0x10        /* Status high */
#define WR9_RESETB      0x40        /* Reset channel B */
#define WR9_RESETA      0x80        /* Reset channel A */
#define WR9_RESETHARD   0xC0        /* Hardware reset */

/* Write register 10 */
#define WR10_SYNC6      0x01        /* 6 bit sync */
#define WR10_LOOP       0x02        /* Loop mode */
#define WR10_ABORTUNDER 0x04        /* Abort on Underrun */
#define WR10_MARKIDLE   0x08        /* Mark Idle */
#define WR10_POLLACT    0x10        /* Go active on poll */
#define WR10_NRZ        0x00        /* NRZ */
#define WR10_NRZI       0x20        /* NRZI */
#define WR10_FM1        0x40        /* FM (Transition = 1) */
#define WR10_FM0        0x60        /* FM (Transition = 0) */
#define WR10_PRESET1    0x80        /* CRC Preset 1 */

/* Write register 11 */
#define WR11_XTAL       0x00        /* TRxC Out = XTAL Output */
#define WR11_TXCLOCK    0x01        /* TRxC Out = Transmit clock */
#define WR11_BRGEN      0x02        /* TRxC Out = BR Generator Output */
#define WR11_DPLL       0x03        /* TRxC Out = DPLL Output */
#define WR11_TRXCOUTEN  0x04        /* Enable TRxC as output */
#define WR11_TXCLKRTXC  0x00        /* Tx clock = RTxC pin */
#define WR11_TXCLKTRXC  0x08        /* Tx clock = TRxC pin */
#define WR11_TXCLKBRGEN 0x10        /* Tx clock = BR Generator Output */
#define WR11_TXCLKDPLL  0x18        /* Tx clock = DPLL Output */
#define WR11_RXCLKRTXC  0x00        /* Rx clock = RTxC pin */
#define WR11_RXCLKTRXC  0x20        /* Rx clock = TRxC pin */
#define WR11_RXCLKBRGEN 0x40        /* Rx clock = BR Generator Output */
#define WR11_RXCLKDPLL  0x60        /* Rx clock = DPLL Output */
#define WR11_RTXCXTAL   0x80        /* Crystal RTxC in (else TTL) */

/* Read/Write register 12/13 Time constant */

/* Write register 14 */
#define WR14_BRENABLE   0x01        /* BR Generator Enable */
#define WR14_BRPCLK     0x02        /* BR Generator CLK from PCLK */
#define WR14_DTSREQ     0x04        /* DTR low is DMA request */
#define WR14_AUTOECHO   0x08        /* Auto echo */
#define WR14_LOOPBACK   0x10        /* Local loopback */
#define WR14_SEARCH     0x20        /* Enter Search mode */
#define WR14_RESET      0x40        /* Reset Missing Clock */
#define WR14_DPLLDIS    0x60        /* Disable DPLL */
#define WR14_SRCBR      0x80        /* Set DPLL CLK Src = BR Generator */
#define WR14_SRCRTXC    0xA0        /* Set DPLL CLK Src = RTxC */
#define WR14_FM         0xC0        /* Set FM mode */
#define WR14_NRZI       0xE0        /* Set NRZI mode */

/* Clocks available to SCC */
#define PCLK_HZ 3684000
#define RTXC_HZ 4000000


/* Interrupts */
static void scc_check_interrupt(void) {
    if ((scc[0].rreg[R_INTBITS]&(RR3_A_IP|RR3_B_IP)) && (scc[0].wreg[W_MASTERINT]&WR9_MIE)) {
        set_interrupt(INT_SCC, SET_INT);
    } else {
        set_interrupt(INT_SCC, RELEASE_INT);
    }
}

static void scc_set_interrupt(uint8_t intr) {
    scc[0].rreg[R_INTBITS] |= intr;
    scc_check_interrupt();
}

static void scc_release_interrupt(uint8_t intr) {
    scc[0].rreg[R_INTBITS] &= ~intr;
    scc_check_interrupt();
}


/* Reset functions */
static void scc_channel_reset(int ch) {
    Log_Printf(LOG_WARN, "[SCC] Reset channel %c\n", ch?'B':'A');
    
    scc[ch].wreg[0] = 0x00;
    scc[ch].wreg[1] &= ~0xDB;
    scc[ch].wreg[3] &= ~0x01;
    scc[ch].wreg[4] |= 0x04;
    scc[ch].wreg[5] &= ~0x9E;
    scc[0].wreg[9] &= ~0x20;
    scc[1].wreg[9] &= ~0x20;
    scc[ch].wreg[10] &= ~0xCF;
    scc[ch].wreg[14] = (scc[ch].wreg[14]&~0x3C)|0x20;
    scc[ch].wreg[15] = 0xF8;
    
    scc[ch].rreg[0] = (scc[ch].rreg[0]&~0xC7)|0x44;
    scc[ch].rreg[1] = 0x06;
    scc[ch].rreg[3] = 0x00;
    scc[ch].rreg[10] = 0x00;
    
    set_interrupt(INT_SCC, RELEASE_INT);
}

static void scc_hard_reset(void) {
    scc_channel_reset(0);
    scc_channel_reset(1);
    
    scc[0].wreg[9] = (scc[0].wreg[9]&~0xFC)|0xC0;
    scc[1].wreg[9] = (scc[1].wreg[9]&~0xFC)|0xC0;
    scc[0].wreg[10] = 0x00;
    scc[1].wreg[10] = 0x00;
    scc[0].wreg[11] = 0x08;
    scc[1].wreg[11] = 0x08;
    scc[0].wreg[14] = (scc[0].wreg[14]&~0x3F)|0x20;
    scc[1].wreg[14] = (scc[1].wreg[14]&~0x3F)|0x20;

    set_interrupt(INT_SCC, RELEASE_INT);
    CycInt_RemovePendingEvent(EVENT_SCC_IO);
}


/* Receive and send data */
void scc_receive(int ch, uint8_t val) {
    Log_Printf(LOG_SCC_IO_LEVEL,"[SCC] Channel %c: Receiving %02X\n", ch?'B':'A', val);

    scc[ch].data = val;
    scc[ch].rreg[R_STATUS] |= RR0_RXAVAIL;
    
    if (scc[ch].wreg[W_MODE]&(WR1_RXALLIE|WR1_RXFIRSTIE)) {
        scc_set_interrupt(ch?RR3_B_RXIP:RR3_A_RXIP);
    }
}

static void scc_send(int ch, uint8_t val) {
    Log_Printf(LOG_SCC_IO_LEVEL,"[SCC] Channel %c: Sending %02X\n", ch?'B':'A', val);

    if (scc[ch].wreg[W_MISC]&WR14_LOOPBACK) {
        scc_receive(ch, val);
    } else {
        if (ch && ConfigureParams.Tablet.nTabletType) {
            tablet_receive(val);
        } else {
            /* send to real world */
        }
    }
    
    if (scc[ch].wreg[W_MODE]&WR1_TXIE) {
        scc_set_interrupt(ch?RR3_B_TXIP:RR3_A_TXIP);
    }
}

static void scc_send_dma(int ch, uint8_t val) {
    Log_Printf(LOG_SCC_IO_LEVEL,"[SCC] Channel %c: Sending %02X via DMA\n", ch?'B':'A', val);
    
    if (scc[ch].wreg[W_MISC]&WR14_LOOPBACK) {
        scc_receive(ch, val);
    } else {
        /* send to real world */
    }
}


/* Read and write data */
bool    scc_pio         = false;
uint8_t scc_pio_data    = 0;
int     scc_pio_channel = 0;

void SCC_IO_Handler(void) {
    int i;
    
    if (scc_pio) {
        scc_pio = false;
        scc_send(scc_pio_channel, scc_pio_data);
    } else { /* DMA */
        for (i = 0; i < 2; i++) {
            if (scc[i].wreg[W_MODE]&WR1_REQENABLE) {
                if (scc[i].wreg[W_MODE]&WR1_REQFUNC) {
                    if (dma_scc_ready() && !(scc[i].rreg[R_STATUS]&RR0_RXAVAIL)) {
                        scc_send_dma(i, dma_scc_read_memory());
                    }
                }
                CycInt_AddCyclesEvent(50, EVENT_SCC_IO);
            }
        }
    }
}

static uint8_t scc_data_read(int ch) {
    Log_Printf(LOG_SCC_LEVEL,"[SCC] Channel %c: Data read %02X\n", ch?'B':'A',scc[ch].data);
    
    scc[ch].rreg[R_STATUS] &= ~RR0_RXAVAIL;
    scc_release_interrupt(ch?RR3_B_RXIP:RR3_A_RXIP);
    
    return scc[ch].data;
}

static void scc_data_write(int ch, uint8_t val) {
    Log_Printf(LOG_SCC_LEVEL,"[SCC] Channel %c: Data write %02X\n", ch?'B':'A',val);
    
    scc_pio = true;
    scc_pio_data = val;
    scc_pio_channel = ch;
    CycInt_AddCyclesEvent(50, EVENT_SCC_IO);
}


/* Internal register functions */
static uint8_t scc_read_intvec(int ch) {
    uint8_t val = scc[0].wreg[W_INTVEC];
    
    /* Add status bits for channel B */
    if (ch) {
        if (scc[0].wreg[W_MASTERINT]&WR9_STATHIGH) {
            if (scc[0].rreg[R_INTBITS] == 0) {
                val |= 0x60;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_RXIP) {
                val |= 0x30;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_TXIP) {
                val |= 0x10;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_STATIP) {
                val |= 0x50;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_RXIP) {
                val |= 0x20;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_TXIP) {
                val |= 0x00;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_STATIP) {
                val |= 0x40;
            }
        } else {
            if (scc[0].rreg[R_INTBITS] == 0) {
                val |= 0x06;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_RXIP) {
                val |= 0x0C;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_TXIP) {
                val |= 0x08;
            } else if (scc[0].rreg[R_INTBITS]&RR3_A_STATIP) {
                val |= 0x0A;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_RXIP) {
                val |= 0x04;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_TXIP) {
                val |= 0x00;
            } else if (scc[0].rreg[R_INTBITS]&RR3_B_STATIP) {
                val |= 0x02;
            }
        }
    }
    return val;
}

static void scc_write_mode(int ch, uint8_t val) {
    if (val&WR1_REQENABLE) {
        scc_pio = false;
        CycInt_AddCyclesEvent(50, EVENT_SCC_IO);
    }
    /* FIXME: check if this is correct */
    if (!(val&WR1_TXIE)) {
        scc[0].rreg[R_INTBITS] &= ~(ch?RR3_B_TXIP:RR3_A_TXIP);
    }
    if (!(val&(WR1_RXALLIE|WR1_RXFIRSTIE))) {
        scc[0].rreg[R_INTBITS] &= ~(ch?RR3_B_RXIP:RR3_A_RXIP);
    }
    if (!(val&WR1_EXTIE)) {
        scc[0].rreg[R_INTBITS] &= ~(ch?RR3_B_STATIP:RR3_A_STATIP);
    }
    scc_check_interrupt();
}

static void scc_write_masterint(uint8_t val) {
    scc[0].wreg[W_MASTERINT] = val;
    scc_register_pointer     = 0;
    
    switch (val&WR9_RESETHARD) {
        case WR9_RESETA:
            scc_channel_reset(0);
            break;
        case WR9_RESETB:
            scc_channel_reset(1);
            break;
        case WR9_RESETHARD:
            scc_hard_reset();
            break;
            
        default:
            break;
    }
    
    scc_check_interrupt();
}

static void scc_write_init(int ch, uint8_t val) {
    switch (val&0x38) {
        case WR0_RESETTXPEND:
            scc_release_interrupt(ch?RR3_B_TXIP:RR3_A_TXIP);
            break;
            
        default:
            break;
    }
}


/* Internal register access */
static uint8_t scc_control_read(int ch) {
    uint8_t val = 0;
    
    switch (scc_register_pointer) {
        case R_STATUS:
        case R_SPECSTAT:
        case R_INTBITS:
        case R_MISCSTAT:
            val = scc[ch].rreg[scc_register_pointer];
            break;
        case R_BRG_LOW:
        case R_BRG_HIGH:
        case R_EXTSTAT:
            val = scc[ch].wreg[scc_register_pointer];
            break;
        case R_INTVEC:
            val = scc_read_intvec(ch);
            break;
        case R_RECBUF:
            val = scc_data_read(ch);
            break;
            
        default:
            Log_Printf(LOG_WARN, "[SCC] Invalid register (%i) read\n",scc_register_pointer);
            break;
    }
    
    Log_Printf(LOG_SCC_LEVEL,"[SCC] Channel %c: Register %i read %02X\n",
               ch?'B':'A',scc_register_pointer,val);
    
    scc_register_pointer = 0;
    
    return val;
}

static void scc_control_write(int ch, uint8_t val) {
    
    if (scc_register_pointer==W_INIT) {
        scc_register_pointer = val&7;
        if ((val&0x38)==8) {
            scc_register_pointer |= 8;
        }
        if (val&0xF0) {
            scc_write_init(ch, val);
        }
    } else {
        Log_Printf(LOG_SCC_LEVEL,"[SCC] Channel %c: Register %i write %02X\n",
                   ch?'B':'A',scc_register_pointer,val);
        
        switch (scc_register_pointer) {
            case W_MODE:
                scc_write_mode(ch, val);
                break;
            case W_MASTERINT:
                scc_write_masterint(val);
                return;
            case W_TRANSBUF:
                scc_data_write(ch, val);
                break;
            case W_INTVEC:
            case W_RECCONT:
            case W_MISCMODE:
            case W_TRANSCONT:
            case W_SYNCCHARA:
            case W_SYNCCHARF:
            case W_MISCCONT:
            case W_CLOCK:
            case W_BRG_LOW:
            case W_BRG_HIGH:
            case W_MISC:
            case W_EXTSTAT:
                break;
                
            default:
                Log_Printf(LOG_WARN, "[SCC] Invalid register (%i) write\n",scc_register_pointer);
                break;
        }
        scc[ch].wreg[scc_register_pointer] = val;
        
        scc_register_pointer = 0;
    }
}

static uint8_t scc_clock_read(void) {
    return scc[0].clock;
}

static void scc_clock_write(uint8_t val) {
    if (ConfigureParams.System.bTurbo) {
        if ((scc[0].clock&0x80) && !(val&0x80)) {
            Log_Printf(LOG_SCC_REG_LEVEL, "[SCC] System clock: Reset\n");
            scc_hard_reset();
        }
        switch ((val>>4)&3) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] System clock: 3.684 MHz\n"); break;
            case 1:
            case 2: Log_Printf(LOG_SCC_LEVEL, "[SCC] System clock: 4 MHz\n"); break;
            case 3: Log_Printf(LOG_SCC_LEVEL, "[SCC] System clock: 10 MHz\n"); break;
            default: break;
        }
        switch ((val>>2)&3) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: 3.684 MHz\n"); break;
            case 1: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: 10 MHz\n"); break;
            case 2: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: 4 MHz\n"); break;
            case 3: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: External\n"); break;
            default: break;
        }
        switch (val&3) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: 3.684 MHz\n"); break;
            case 1: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: 10 MHz\n"); break;
            case 2: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: 4 MHz\n"); break;
            case 3: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: External\n"); break;
            default: break;
        }
    } else {
        switch ((val>>4)&1) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] System clock: 3.684 MHz\n"); break;
            case 1: Log_Printf(LOG_SCC_LEVEL, "[SCC] System clock: External\n"); break;
            default: break;
        }
        switch ((val>>2)&3) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: 3.684 MHz\n"); break;
            case 1: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: External\n"); break;
            case 2: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: 4 MHz\n"); break;
            case 3: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel B clock: None\n"); break;
            default: break;
        }
        switch (val&3) {
            case 0: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: 3.684 MHz\n"); break;
            case 1: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: External\n"); break;
            case 2: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: 4 MHz\n"); break;
            case 3: Log_Printf(LOG_SCC_LEVEL, "[SCC] Channel A clock: RTxCA\n"); break;
            default: break;
        }
    }
    scc[0].clock = val;
}

void SCC_Reset(void) {
    scc_hard_reset();
    scc[0].clock = 0;
}


/* Registers */
void SCC_ControlB_Read(void) { /* 0x02018000 */
    IoMem_WriteByte(IoAccessCurrentAddress, scc_control_read(1));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel B control read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_ControlB_Write(void) {
    scc_control_write(1, IoMem_ReadByte(IoAccessCurrentAddress));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel B control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_ControlA_Read(void) { /* 0x02018001 */
    IoMem_WriteByte(IoAccessCurrentAddress, scc_control_read(0));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel A control read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_ControlA_Write(void) {
    scc_control_write(0, IoMem_ReadByte(IoAccessCurrentAddress));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel A control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_DataB_Read(void) { /* 0x02018002 */
    IoMem_WriteByte(IoAccessCurrentAddress, scc_data_read(1));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel B data read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_DataB_Write(void) {
    scc_data_write(1, IoMem_ReadByte(IoAccessCurrentAddress));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel B data write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_DataA_Read(void) { /* 0x02018003 */
    IoMem_WriteByte(IoAccessCurrentAddress, scc_data_read(0));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel A data read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_DataA_Write(void) {
    scc_data_write(0, IoMem_ReadByte(IoAccessCurrentAddress));
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Channel A data write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_Clock_Read(void) { /* 0x02018004 */
    IoMem_WriteByte(IoAccessCurrentAddress, scc_clock_read());
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Clock select read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void SCC_Clock_Write(void) {
    uint8_t val = IoMem_ReadBytePort();
    scc_clock_write(val);
    Log_Printf(LOG_SCC_REG_LEVEL,"[SCC] Clock select write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}
