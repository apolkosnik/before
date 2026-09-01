/*
  Previous - printer.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the NeXT Laser Printer.
*/
const char Printer_fileid[] = "Previous printer.c";

#include "ioMem.h"
#include "m68000.h"
#include "configuration.h"
#include "printer.h"
#include "sysReg.h"
#include "dma.h"
#include "statusbar.h"
#include "grab.h"

#define LOG_LP_REG_LEVEL    LOG_DEBUG
#define LOG_LP_LEVEL        LOG_DEBUG
#define LOG_LP_PRINT_LEVEL  LOG_DEBUG


/* Functions for printing to an image file */
PrinterBuffer lp_buffer;

static uint8_t* print_data;
static int print_dpi;
static int print_size;
static int print_limit;
static int print_width;

/* 14 inches is the longest paper and 400 DPI is the highest resolution. */
static const int MAX_PAGE_LEN = 14 * 400; 

static void lp_print_start(uint32_t data) {
    if (print_data) {
        free(print_data);
    }
    print_width = ((data >> 16) & 0x7F) * 4;
    if (print_width) {
        print_limit = MAX_PAGE_LEN * print_width;
        print_data  = (uint8_t*)malloc(print_limit);
        print_size  = 0;
    }
}

static void lp_print_data(void) {
    if (print_data) {
        if (lp_buffer.size > print_limit - print_size) {
            lp_buffer.size = print_limit - print_size;
        }
        memcpy(print_data + print_size, lp_buffer.data, lp_buffer.size);
        print_size += lp_buffer.size;
    }
}

static void lp_print_finish(void) {
    if (print_data) {
        Grab_Print(print_data, print_width * 8, print_size / print_width, print_dpi);
        free(print_data);
        print_data = NULL;
        print_size = 0;
    }
}


struct {
    /* Registers */
    struct {
        uint8_t dma;
        uint8_t printer;
        uint8_t transmit;
        uint8_t cmd;
    } csr;
    
    uint8_t  command;
    uint32_t data;
    
    /* Internal */
    uint8_t  stat;
    uint8_t  statmask;
    uint32_t margins;
} lp;

static bool lp_data_transfer = false;

/* Laser Printer control and status register (0x0200F000)
 *
 * x--- ---- ---- ---- ---- ---- ---- ----  dma out enable (r/w)
 * -x-- ---- ---- ---- ---- ---- ---- ----  dma out request (r)
 * --x- ---- ---- ---- ---- ---- ---- ----  dma out underrun detected (r/w)
 * ---- x--- ---- ---- ---- ---- ---- ----  dma in enable (r/w)
 * ---- -x-- ---- ---- ---- ---- ---- ----  dma in request (r)
 * ---- --x- ---- ---- ---- ---- ---- ----  dma in overrun detected (r/w)
 * ---- ---x ---- ---- ---- ---- ---- ----  increment buffer read pointer (r/w)
 *
 * ---- ---- x--- ---- ---- ---- ---- ----  printer on/off (r/w)
 * ---- ---- -x-- ---- ---- ---- ---- ----  behave like monitor interface (r/w)
 * ---- ---- ---- x--- ---- ---- ---- ----  printer interrupt (r)
 * ---- ---- ---- -x-- ---- ---- ---- ----  printer data available (r)
 * ---- ---- ---- --x- ---- ---- ---- ----  printer data overrun (r/w)
 *
 * ---- ---- ---- ---- x--- ---- ---- ----  dma out transmit pending (r)
 * ---- ---- ---- ---- -x-- ---- ---- ----  dma out transmit in progress (r)
 * ---- ---- ---- ---- --x- ---- ---- ----  cpu data transmit pending (r)
 * ---- ---- ---- ---- ---x ---- ---- ----  cpu data transmit in progress (r)
 * ---- ---- ---- ---- ---- --x- ---- ----  printer interface enable (return from reset state) (r/w)
 * ---- ---- ---- ---- ---- ---x ---- ----  loop back transmitter data (r/w)
 *
 * ---- ---- ---- ---- ---- ---- xxxx xxxx  command to append on printer data (r/w)
 *
 * ---x ---- --xx ---x ---- xx-- ---- ----  zero bits
 */

#define LP_DMA_OUT_EN   0x80
#define LP_DMA_OUT_REQ  0x40
#define LP_DMA_OUT_UNDR 0x20
#define LP_DMA_IN_EN    0x08
#define LP_DMA_IN_REQ   0x04
#define LP_DMA_IN_OVR   0x02
#define LP_DMA_INCR     0x01

#define LP_ON           0x80
#define LP_NDI          0x40
#define LP_INT          0x08
#define LP_DATA         0x04
#define LP_DATA_OVR     0x02

#define LP_TX_DMA_PEND  0x80
#define LP_TX_DMA       0x40
#define LP_TX_CPU_PEND  0x20
#define LP_TX_CPU       0x10
#define LP_TX_EN        0x02
#define LP_TX_LOOP      0x01

/* Printer interrupt functions */
static void lp_set_interrupt(void) {
    lp.csr.printer |= LP_INT;
    set_interrupt(INT_PRINTER, SET_INT);
}

static void lp_release_interrupt(void) {
    lp.csr.printer &= ~LP_INT;
    set_interrupt(INT_PRINTER, RELEASE_INT);
}

static void lp_check_interrupt(void) {
    if ((lp.csr.printer&(LP_DATA|LP_DATA_OVR)) || (lp.csr.dma&(LP_DMA_OUT_UNDR|LP_DMA_IN_OVR))) {
        lp_set_interrupt();
    } else {
        lp_release_interrupt();
    }
}


/* Commands from CPU to Printer */
#define LP_CMD_RESET    0xff
#define LP_CMD_DATA_OUT 0xc7
#define LP_CMD_GPI_MASK 0xc5
#define LP_CMD_GPO      0xc4
#define LP_CMD_MARGINS  0xc2
#define LP_CMD_GPI_REQ  0x04

#define LP_CMD_MASK     0xc7
#define LP_CMD_NODATA   0x07
#define LP_CMD_DATA_EN  0x08
#define LP_CMD_300DPI   0x10
#define LP_CMD_NORMAL   0x20

/* Commands from Printer to CPU */
#define LP_CMD_DATA_IN  0xc7
#define LP_CMD_GPI      0xc4
#define LP_CMD_COPY     0xe7
#define LP_CMD_DATA_REQ 0x07
#define LP_CMD_UNDERRUN 0x0f


/* Data for commands */
#define LP_GPI_PWR_RDY  0x10
#define LP_GPI_RDY      0x08
#define LP_GPI_VSREQ    0x04
#define LP_GPI_BUSY     0x02
#define LP_GPI_STAT_BIT 0x01

#define LP_GPO_DENSITY  0x40
#define LP_GPO_VSYNC    0x20
#define LP_GPO_ENABLE   0x10
#define LP_GPO_PWR_RDY  0x08
#define LP_GPO_CLOCK    0x04
#define LP_GPO_CMD_BIT  0x02
#define LP_GPO_BUSY     0x01

static void lp_send_data(uint32_t data) {
    lp.data = data;
    
    if (lp.csr.printer&LP_DATA) {
        lp.csr.printer |= LP_DATA_OVR;
    } else {
        lp.csr.printer |= LP_DATA;
    }
    lp_set_interrupt();
}

static void lp_gpi(uint32_t data) {
    lp_send_data(data);
}

static void lp_boot_message(uint32_t data) {
    lp_send_data(data);
}

static void lp_dma_request(void) {
    lp.csr.dma |= LP_DMA_OUT_REQ;
    
    dma_printer_read_memory();
}

static void lp_dma_underrun(void) {
    lp.csr.dma |= LP_DMA_OUT_UNDR;
    lp_set_interrupt();
}

static void lp_command_out(uint8_t command, uint32_t data) {
    if (!(lp.csr.transmit&LP_TX_EN)) {
        return;
    }

    switch (command) {
        case LP_CMD_DATA_IN:
            Log_Printf(LOG_LP_LEVEL, "[LP] Scanner data in");
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            break;
        case LP_CMD_GPI:
            Log_Printf(LOG_LP_LEVEL, "[LP] General purpose input");
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp.csr.cmd = command;
            lp_gpi(data);
            break;
        case LP_CMD_COPY:
            Log_Printf(LOG_LP_LEVEL, "[LP] Copyright message");
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp.csr.cmd = command;
            lp_boot_message(data);
            break;
        case LP_CMD_DATA_REQ:
            Log_Printf(LOG_LP_LEVEL, "[LP] Data out request");
            lp.csr.cmd = command;
            lp_dma_request();
            break;
        case LP_CMD_UNDERRUN:
            Log_Printf(LOG_LP_LEVEL, "[LP] Data out underrun");
            lp.csr.cmd = command;
            lp_dma_underrun();
            break;
            
        default:
            Log_Printf(LOG_WARN, "[LP] Unknown command out (%02X)",command);
            Log_Printf(LOG_WARN, "[LP] Data = %08X",data);
            break;
    }
}


/* Printer internal command and status via serial interface */
#define CMD_STATUS0     0x01
#define CMD_STATUS1     0x02
#define CMD_STATUS2     0x04
#define CMD_STATUS4     0x08
#define CMD_STATUS5     0x0b
#define CMD_STATUS15    0x1f

#define CMD_EXT_CLK     0x40
#define CMD_PRINTER_CLK 0x43
#define CMD_PAUSE       0x45
#define CMD_UNPAUSE     0x46
#define CMD_DRUM_ON     0x49
#define CMD_DRUM_OFF    0x4a
#define CMD_CASSFEED    0x4c
#define CMD_HANDFEED    0x4f
#define CMD_RETRANSCANC 0x5d

#define STAT0_PRINTREQ  0x40
#define STAT0_PAPERDLVR 0x20
#define STAT0_DATARETR  0x10
#define STAT0_WAIT      0x08
#define STAT0_PAUSE     0x04
#define STAT0_CALL      0x02

#define STAT1_NOCART    0x40
#define STAT1_NOPAPER   0x10
#define STAT1_JAM       0x08
#define STAT1_DOOROPEN  0x04
#define STAT1_TESTPRINT 0x02

#define STAT2_FIXINGASM 0x40
#define STAT2_POORBDSIG 0x20
#define STAT2_SCANMOTOR 0x10

#define STAT5_NOCASS    0x01
#define STAT5_A4        0x02
#define STAT5_LETTER    0x08
#define STAT5_B5        0x13
#define STAT5_LEGAL     0x19

#define STAT15_NOTONER  0x04

static uint8_t lp_serial_status[16] = {
    0,0,0,0,
    0,STAT5_A4,0,0,
    0,0,0,0,
    0,0,0,0
};

static uint8_t lp_serial_phase = 0;

static void lp_interface_status(uint8_t changed_bits, bool set) {
    uint8_t old_stat = lp.stat;
    
    Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Interface status %s: %02X (mask: %02X)",set?"set":"release",changed_bits,lp.statmask);
    
    if (set) {
        lp.stat |= changed_bits;
    } else {
        lp.stat &= ~changed_bits;
    }
    
    if ((old_stat^lp.stat)&lp.statmask) {
        lp_command_out(LP_CMD_GPI, ~((uint32_t)lp.stat)<<24);
    }
}

static uint8_t lp_printer_status(uint8_t num) {
    int i;
    uint8_t val;
    
    lp_serial_phase = 8;
    lp_interface_status(LP_GPI_BUSY, true);
    
    if (num==5) {
        switch (ConfigureParams.Printer.nPaperSize) {
            case PAPER_A4: val = STAT5_A4; break;
            case PAPER_LETTER: val = STAT5_LETTER; break;
            case PAPER_B5: val = STAT5_B5; break;
            case PAPER_LEGAL: val = STAT5_LEGAL; break;
            default: val = PAPER_A4; break;
        }
    } else if (num<16) {
        val = lp_serial_status[num];
    } else {
        val = 0x00;
    }
    
    /* odd parity */
    val |= 1;
    for (i = 1; i < 8; i++) {
        if (val & (1 << i)) {
            val ^= 1;
        }
    }
    return val;
}

/* COPY. NeXT 1987 */
static const uint32_t lp_copyright_message[4] = { 0x00434f50, 0x522e204e, 0x65585420, 0x31393837 };
static int lp_copyright_sequence = 0;

static void lp_check_boot_sequence(void) {
    if (lp_copyright_sequence>0) {
        lp_command_out(LP_CMD_COPY, lp_copyright_message[4-lp_copyright_sequence]);
        lp_copyright_sequence--;
    }
}

static void lp_start_boot_sequence(void) {
    lp_copyright_sequence = 4;
    lp_check_boot_sequence();
}

static void lp_printer_reset(void) {
    int i;
    for (i = 0; i < 16; i++) {
        lp_serial_status[i] = 0;
    }
    lp_serial_phase = 0;
    lp_copyright_sequence = 0;
    lp_data_transfer = false;
    lp_print_finish();
}

static void lp_interface_on(void) {
    if (lp.stat&LP_GPI_PWR_RDY) {
        lp_start_boot_sequence();
    }
}

static void lp_interface_off(void) {
    lp_copyright_sequence = 0;
    
    lp.csr.dma = 0;
    lp.csr.printer = 0;
    lp.csr.transmit = 0;
    lp.csr.cmd = 0;
    lp.command = 0;
    lp.data = 0;
    
    set_interrupt(INT_PRINTER, RELEASE_INT);
}

static void lp_power_on(void) {
    Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Power on");
    lp_interface_status(LP_GPI_PWR_RDY|LP_GPI_RDY,true);
}

static void lp_power_off(void) {
    Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Power off");
    lp_printer_reset();
    lp.statmask = 0;
    lp.stat = 0;
}

static uint8_t lp_printer_command(uint8_t cmd) {
    switch (cmd) {
        case CMD_STATUS0:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 0");
            return lp_printer_status(0);
        case CMD_STATUS1:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 1");
            return lp_printer_status(1);
        case CMD_STATUS2:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 2");
            return lp_printer_status(2);
        case CMD_STATUS4:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 4");
            return lp_printer_status(4);
        case CMD_STATUS5:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 5");
            return lp_printer_status(5);
        case CMD_STATUS15:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Read status register 15");
            return lp_printer_status(15);
        /* Commands with no status */
        case CMD_EXT_CLK:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] External clock");
            return lp_printer_status(16);
        case CMD_PRINTER_CLK:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Printer clock");
            return lp_printer_status(16);
        case CMD_PAUSE:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Pause");
            return lp_printer_status(16);
        case CMD_UNPAUSE:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Unpause");
            return lp_printer_status(16);
        case CMD_DRUM_ON:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Drum on");
            return lp_printer_status(16);
        case CMD_DRUM_OFF:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Drum off");
            return lp_printer_status(16);
        case CMD_CASSFEED:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Cassette feed");
            return lp_printer_status(16);
        case CMD_HANDFEED:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Hand feed");
            return lp_printer_status(16);
        case CMD_RETRANSCANC:
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Cancel retransmission");
            return lp_printer_status(16);
            
        default:
            Log_Printf(LOG_WARN, "[Printer] Unknown command!");
            return lp_printer_status(16);
    }
}

static void lp_gpo_access(uint8_t data) {
    static uint8_t lp_cmd = 0;
    static uint8_t lp_stat = 0;
    static uint8_t lp_old_data = 0;
    
    uint8_t changed_bits = data ^ lp_old_data;
    
    Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Control logic access: %02X",data);
    
    if (changed_bits&LP_GPO_BUSY) {
        if (data&LP_GPO_BUSY) {
            lp_cmd = 0;
        } else {
            Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Command: %02X",lp_cmd);
            lp_stat = lp_printer_command(lp_cmd);
        }
    }
    if (data&LP_GPO_BUSY) {
        if ((changed_bits&LP_GPO_CLOCK) && (data&LP_GPO_CLOCK)) {
            lp_cmd <<= 1;
            lp_cmd |= (data&LP_GPO_CMD_BIT)?1:0;
        }
    } else if (lp.stat&LP_GPI_BUSY) {
        if ((changed_bits&LP_GPO_CLOCK) && (data&LP_GPO_CLOCK)) {
            lp_serial_phase--;
            if (lp_serial_phase==0) {
                lp_interface_status(LP_GPI_BUSY, false);
                Log_Printf(LOG_LP_PRINT_LEVEL, "[Printer] Status: %02X",lp_stat);
            }
            lp_interface_status(LP_GPI_STAT_BIT,(lp_stat&(1<<lp_serial_phase))?true:false);
        }
    }
    lp_old_data = data;
}

static void lp_gpi_request(void) {
    lp_command_out(LP_CMD_GPI, ~((uint32_t)lp.stat)<<24);
}

static void lp_gpo(uint8_t cmd) {
    if (cmd&LP_GPO_DENSITY) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] 300 DPI mode");
    }
    if (cmd&LP_GPO_VSYNC) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] VSYNC enable");
        lp_interface_status(LP_GPI_VSREQ, false);
    }
    if (cmd&LP_GPO_ENABLE) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Enable");
        lp_interface_status(LP_GPI_VSREQ, true);
    }
    if (cmd&LP_GPO_PWR_RDY) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Controller power ready");
    }
    if (cmd&LP_GPO_CLOCK) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Serial data clock");
    }
    if (cmd&LP_GPO_CMD_BIT) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Serial data command bits");
    }
    if (cmd&LP_GPO_BUSY) {
        Log_Printf(LOG_LP_PRINT_LEVEL,"[Printer] Controller busy");
    }
    lp_gpo_access(cmd);
}

static void lp_command_in(uint8_t cmd, uint32_t data) {
    if (!(lp.csr.transmit&LP_TX_EN)) {
        return;
    }
    if (!(lp.stat&LP_GPI_PWR_RDY)) {
        return;
    }

    switch (cmd) {
        case LP_CMD_RESET:
            Log_Printf(LOG_LP_LEVEL, "[LP] Reset (%08X)",data);
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            if (data==0xFFFFFFFF) {
                lp_printer_reset();
            }
            break;
        case LP_CMD_DATA_OUT:
            Log_Printf(LOG_LP_LEVEL, "[LP] Printer data out (%08X)",data);
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp_buffer.data[lp_buffer.size+0] = (data>>24)&0xFF;
            lp_buffer.data[lp_buffer.size+1] = (data>>16)&0xFF;
            lp_buffer.data[lp_buffer.size+2] = (data>>8)&0xFF;
            lp_buffer.data[lp_buffer.size+3] = data&0xFF;
            lp_buffer.size +=4;
            break;
        case LP_CMD_GPO:
            Log_Printf(LOG_LP_LEVEL, "[LP] General purpose out (%02X)",(~data)>>24);
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp_gpo((~data)>>24);
            break;
        case LP_CMD_GPI_MASK:
            Log_Printf(LOG_LP_LEVEL, "[LP] General purpose input mask (%02X)",(~data)>>24);
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp.statmask = (~data)>>24;
            break;
        case LP_CMD_GPI_REQ:
            Log_Printf(LOG_LP_LEVEL, "[LP] General purpose input request");
            lp_gpi_request();
            break;
        case LP_CMD_MARGINS:
            Log_Printf(LOG_LP_LEVEL, "[LP] Set margins (%08X)",data);
            Log_Printf(LOG_LP_LEVEL, "[LP] Data = %08X",data);
            lp.margins = data;
            break;
            
        default: /* Commands with no data */
            if ((cmd&LP_CMD_MASK)==LP_CMD_NODATA) {
                Log_Printf(LOG_LP_LEVEL, "[LP] No data command:");

                if (cmd&LP_CMD_DATA_EN) {
                    Log_Printf(LOG_LP_LEVEL,"[LP] Enable printer data transfer");
                    /* Setup printing buffer */
                    lp_print_start(lp.margins);
                    lp_data_transfer = true;
                    if (lp_buffer.size) {
                        lp_print_data();
                        lp_buffer.size = 0;
                    }
                    Statusbar_AddMessage("Laser printer printing page", 0);
                    CycInt_AddTimeEvent(1000, 100, EVENT_PRINTER_IO);
                } else {
                    Log_Printf(LOG_LP_LEVEL, "[LP] Disable printer data transfer");
                    if (lp_data_transfer) {
                        /* Save buffered printing data to image file */
                        lp_print_finish();
                    }
                    lp_data_transfer = false;
                }
                if (cmd&LP_CMD_300DPI) {
                    Log_Printf(LOG_LP_LEVEL, "[LP] 300 DPI mode");
                    print_dpi = 300;
                } else {
                    Log_Printf(LOG_LP_LEVEL, "[LP] 400 DPI mode");
                    print_dpi = 400;
                }
                if (cmd&LP_CMD_NORMAL) {
                    Log_Printf(LOG_LP_LEVEL, "[LP] Normal requests for data transfer");
                } else {
                    Log_Printf(LOG_LP_LEVEL, "[LP] Early requests for data transfer");
                }
            } else {
                Log_Printf(LOG_WARN, "[LP] Unknown command!");
                Log_Printf(LOG_WARN, "[LP] Data = %08X",data);
            }
            break;
    }
}


/* Printer DMA and printing function */
void Printer_IO_Handler(void) {
    if (lp_data_transfer) {
        lp_buffer.limit = 4096;
        lp_command_out(LP_CMD_DATA_REQ, 0);
        
        if (lp_buffer.size==0) {
            Log_Printf(LOG_LP_LEVEL,"[LP] Printing done.");
            lp_command_out(LP_CMD_UNDERRUN, 0);
            return;
        }
        /* Save data to printing buffer */
        lp_print_data();
        lp_buffer.size = 0;
        
        CycInt_UpdateTimeEvent(10000, 1000, EVENT_PRINTER_IO);
    }
}

/* Free printing buffer */
void Printer_UnInit(void) {
    lp_print_finish();
}

/* Printer reset function */
void Printer_Reset(void) {
    lp_interface_off();
    lp_power_off();
}


/* Printer interface registers */
void LP_CSR0_Read(void) { /* 0x0200F000 */
    IoMem_WriteByte(IoAccessCurrentAddress, lp.csr.dma);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] DMA status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void LP_CSR0_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] DMA control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    lp.csr.dma &= ~(LP_DMA_OUT_EN|LP_DMA_IN_EN);
    lp.csr.dma |= (val&(LP_DMA_OUT_EN|LP_DMA_IN_EN));

    if (val&LP_DMA_OUT_UNDR) {
        lp.csr.dma &= ~(LP_DMA_OUT_UNDR|LP_DMA_OUT_REQ);
    }
    if (val&LP_DMA_IN_OVR) {
        lp.csr.dma &= ~(LP_DMA_IN_OVR|LP_DMA_IN_REQ);
    }
    lp_check_interrupt();
}

void LP_CSR1_Read(void) { /* 0x0200F001 */
    IoMem_WriteByte(IoAccessCurrentAddress, lp.csr.printer);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Printer status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void LP_CSR1_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Printer control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    if (((val&LP_ON) != (lp.csr.printer&LP_ON)) && ConfigureParams.Printer.bPrinterConnected) {
        if (val&LP_ON) {
            Statusbar_AddMessage("Switching laser printer ON", 0);
            lp_power_on();
        } else {
            Statusbar_AddMessage("Switching laser printer OFF", 0);
            lp_power_off();
        }
    }
    lp.csr.printer = val;
    
    if (val&LP_DATA_OVR) {
        lp.csr.printer &= ~(LP_DATA_OVR|LP_DATA);
    }
    lp_check_interrupt();
}

void LP_CSR2_Read(void) { /* 0x0200F002 */
    IoMem_WriteByte(IoAccessCurrentAddress, lp.csr.transmit);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Transmitter status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void LP_CSR2_Write(void) {
    uint8_t old = lp.csr.transmit;
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Transmitter control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    lp.csr.transmit &= ~(LP_TX_EN|LP_TX_LOOP);
    lp.csr.transmit |= (val&(LP_TX_EN|LP_TX_LOOP));

    if ((val&LP_TX_EN) != (old&LP_TX_EN)) {
        if (val&LP_TX_EN) {
            Log_Printf(LOG_LP_LEVEL,"[LP] Enable serial interface.");
            lp_interface_on();
        } else {
            Log_Printf(LOG_LP_LEVEL,"[LP] Disable serial interface.");
            lp_interface_off();
        }
    }
}

void LP_CSR3_Read(void) { /* 0x0200F003 */
    IoMem_WriteByte(IoAccessCurrentAddress, lp.csr.cmd);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Command read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void LP_CSR3_Write(void) {
    lp.command = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Command write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void LP_Data_Read(void) { /* 0x0200F004 (access must be 32-bit) */
    IoMem_WriteLong(IoAccessCurrentAddress, lp.data);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Data read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, lp.data, m68k_getpc());
    
    lp.csr.printer &= ~LP_DATA;

    lp_check_boot_sequence();
    lp_check_interrupt();
}

void LP_Data_Write(void) {
    uint32_t val = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_LP_REG_LEVEL,"[LP] Data write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, val, m68k_getpc());
    
    if (lp.csr.transmit&LP_TX_LOOP) {
        lp_command_out(lp.command, val);
    } else {
        lp_command_in(lp.command, val);
    }
}
