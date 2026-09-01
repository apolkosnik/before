/*
  Previous - dma.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the Fujitsu MB610313 Integrated
  Channel Processor (ISP) for direct memory access. The ISP controls
  twelve DMA channels with 128 byte internal buffers.
*/
const char Dma_fileid[] = "Previous dma.c";

#include "main.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "scsi.h"
#include "esp.h"
#include "mo.h"
#include "scc.h"
#include "sysReg.h"
#include "dma.h"
#include "configuration.h"
#include "ethernet.h"
#include "floppy.h"
#include "printer.h"
#include "snd.h"
#include "dsp.h"
#include "mmu_common.h"

#define LOG_DMA_LEVEL LOG_DEBUG

typedef enum {
    CHANNEL_SCSI,       /* 0x00000010 */
    CHANNEL_SOUNDOUT,   /* 0x00000040 */
    CHANNEL_DISK,       /* 0x00000050 */
    CHANNEL_SOUNDIN,    /* 0x00000080 */
    CHANNEL_PRINTER,    /* 0x00000090 */
    CHANNEL_SCC,        /* 0x000000c0 */
    CHANNEL_DSP,        /* 0x000000d0 */
    CHANNEL_EN_TX,      /* 0x00000110 */
    CHANNEL_EN_RX,      /* 0x00000150 */
    CHANNEL_VIDEO,      /* 0x00000180 */
    CHANNEL_M2R,        /* 0x000001d0 */
    CHANNEL_R2M         /* 0x000001c0 */
} DMA_CHANNEL;

struct {
    uint32_t csr;
    uint32_t saved_next;
    uint32_t saved_limit;
    uint32_t saved_start;
    uint32_t saved_stop;
    uint32_t next;
    uint32_t limit;
    uint32_t start;
    uint32_t stop;
    
    uint32_t direction;
} dma[12];


/* DMA internal buffers */
#define DMA_BURST_SIZE  16

int     espdma_buf_size = 0;
int     espdma_buf_limit = 0;
uint8_t espdma_buf[DMA_BURST_SIZE];
int     modma_buf_size = 0;
int     modma_buf_limit = 0;
uint8_t modma_buf[DMA_BURST_SIZE];


/* Read and write CSR bits */

/* read CSR bits */
#define DMA_ENABLE      0x01000000   /* enable dma transfer */
#define DMA_SUPDATE     0x02000000   /* single update */
#define DMA_COMPLETE    0x08000000   /* current dma has completed */
#define DMA_BUSEXC      0x10000000   /* bus exception occurred */
/* write CSR bits */
#define DMA_SETENABLE   0x00010000   /* set enable */
#define DMA_SETSUPDATE  0x00020000   /* set single update */
#define DMA_M2DEV       0x00000000   /* dma from mem to dev */
#define DMA_DEV2M       0x00040000   /* dma from dev to mem */
#define DMA_CLRCOMPLETE 0x00080000   /* clear complete conditional */
#define DMA_RESET       0x00100000   /* clr cmplt, sup, enable */
#define DMA_INITBUF     0x00200000   /* initialize DMA buffers */

/* CSR masks */
#define DMA_CMD_MASK    (DMA_SETENABLE|DMA_SETSUPDATE|DMA_CLRCOMPLETE|DMA_RESET|DMA_INITBUF)
#define DMA_STAT_MASK   (DMA_ENABLE|DMA_SUPDATE|DMA_COMPLETE|DMA_BUSEXC)


static inline uint32_t dma_getlong(uint8_t *buf, uint32_t pos) {
    return ((uint32_t)buf[pos] << 24) | (buf[pos+1] << 16) | (buf[pos+2] << 8) | buf[pos+3];
}

static inline void dma_putlong(uint32_t val, uint8_t *buf, uint32_t pos) {
    buf[pos] = val >> 24;
    buf[pos+1] = val >> 16;
    buf[pos+2] = val >> 8;
    buf[pos+3] = val;
}


static int get_channel(uint32_t address) {
    int channel = address & 0x0001ffff;

    switch (channel) {
        case 0x010: Log_Printf(LOG_DMA_LEVEL,"channel SCSI:");        return CHANNEL_SCSI;
        case 0x040: Log_Printf(LOG_DMA_LEVEL,"channel Sound Out:");   return CHANNEL_SOUNDOUT;
        case 0x050: Log_Printf(LOG_DMA_LEVEL,"channel MO Disk:");     return CHANNEL_DISK;
        case 0x080: Log_Printf(LOG_DMA_LEVEL,"channel Sound in:");    return CHANNEL_SOUNDIN;
        case 0x090: Log_Printf(LOG_DMA_LEVEL,"channel Printer:");     return CHANNEL_PRINTER;
        case 0x0c0: Log_Printf(LOG_DMA_LEVEL,"channel SCC:");         return CHANNEL_SCC;
        case 0x0d0: Log_Printf(LOG_DMA_LEVEL,"channel DSP:");         return CHANNEL_DSP;
        case 0x110: Log_Printf(LOG_DMA_LEVEL,"channel Ethernet Tx:"); return CHANNEL_EN_TX;
        case 0x150: Log_Printf(LOG_DMA_LEVEL,"channel Ethernet Rx:"); return CHANNEL_EN_RX;
        case 0x180: Log_Printf(LOG_DMA_LEVEL,"channel Video:");       return CHANNEL_VIDEO;
        case 0x1d0: Log_Printf(LOG_DMA_LEVEL,"channel M2R:");         return CHANNEL_M2R;
        case 0x1c0: Log_Printf(LOG_DMA_LEVEL,"channel R2M:");         return CHANNEL_R2M;

        default:
            Log_Printf(LOG_WARN, "Unknown DMA channel!\n");
            abort();
    }
}

static int get_interrupt_type(int channel) {
    switch (channel) {
        case CHANNEL_SCSI:     return INT_SCSI_DMA;
        case CHANNEL_SOUNDOUT: return INT_SND_OUT_DMA;
        case CHANNEL_DISK:     return INT_DISK_DMA;
        case CHANNEL_SOUNDIN:  return INT_SND_IN_DMA;
        case CHANNEL_PRINTER:  return INT_PRINTER_DMA;
        case CHANNEL_SCC:      return INT_SCC_DMA;
        case CHANNEL_DSP:      return INT_DSP_DMA;
        case CHANNEL_EN_TX:    return INT_EN_TX_DMA;
        case CHANNEL_EN_RX:    return INT_EN_RX_DMA;
        case CHANNEL_VIDEO:    return INT_VIDEO;
        case CHANNEL_M2R:      return INT_M2R_DMA;
        case CHANNEL_R2M:      return INT_R2M_DMA;

        default:
            Log_Printf(LOG_WARN, "Unknown DMA interrupt!\n");
            abort();
    }
}


static void dma_initialize_buffer(int channel, uint8_t offset) {
    if (offset>0) {
        Log_Printf(LOG_WARN, "DMA Initializing buffer with offset %i", offset);
    }
    switch (channel) {
        case CHANNEL_SCSI:
            esp_dma.status   = 0; /* just a guess */
            espdma_buf_size  = 0;
            espdma_buf_limit = offset;
            break;
        case CHANNEL_DISK:
            modma_buf_size   = 0;
            modma_buf_limit  = offset;
            break;
        default:
            break;
    }
}


/* DMA register access functions */

void DMA_CSR_Read(void) { /* 0x02000010 */
    int channel = get_channel(IoAccessCurrentAddress);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].csr);
    Log_Printf(LOG_DMA_LEVEL,"DMA CSR read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].csr, m68k_getpc());
}

void DMA_CSR_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress);
    int interrupt = get_interrupt_type(channel);
    
    uint32_t writecsr = IoMem_ReadLong(IoAccessCurrentAddress);
    
    Log_Printf(LOG_DMA_LEVEL,"DMA CSR write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, writecsr, m68k_getpc());
    
    /* For debugging */
    if(writecsr&DMA_DEV2M)
        Log_Printf(LOG_DMA_LEVEL,"DMA from dev to mem");
    else
        Log_Printf(LOG_DMA_LEVEL,"DMA from mem to dev");
    
    switch (writecsr&DMA_CMD_MASK) {
        case DMA_RESET:
        case (DMA_RESET | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA reset"); break;
        case DMA_INITBUF:
            Log_Printf(LOG_DMA_LEVEL,"DMA initialize buffers"); break;
        case (DMA_RESET | DMA_INITBUF):
        case (DMA_RESET | DMA_INITBUF | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA reset and initialize buffers"); break;
        case DMA_CLRCOMPLETE:
            Log_Printf(LOG_DMA_LEVEL,"DMA end chaining"); break;
        case (DMA_SETSUPDATE | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA continue chaining"); break;
        case DMA_SETENABLE:
            Log_Printf(LOG_DMA_LEVEL,"DMA start single transfer"); break;
        case (DMA_SETENABLE | DMA_SETSUPDATE):
        case (DMA_SETENABLE | DMA_SETSUPDATE | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA start chaining"); break;
        case 0:
            Log_Printf(LOG_DMA_LEVEL,"DMA no command"); break;
        default:
            Log_Printf(LOG_WARN,"DMA: unknown command (%02x)!", writecsr); break;
    }

    /* Handle CSR bits */
    dma[channel].direction = writecsr&DMA_DEV2M;

    if (writecsr&DMA_RESET) {
        dma[channel].csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
    }
    if (writecsr&DMA_INITBUF) {
        dma_initialize_buffer(channel, 0);
    }
    if (writecsr&DMA_SETSUPDATE) {
        dma[channel].csr |= DMA_SUPDATE;
    }
    if (writecsr&DMA_SETENABLE) {
        dma[channel].csr |= DMA_ENABLE;
        
        /* Enable Memory to Memory DMA, if read and write channels are enabled */
        if (channel == CHANNEL_R2M || channel == CHANNEL_M2R) {
            if (dma[channel].next==dma[channel].limit) {
                dma[channel].csr &= ~DMA_ENABLE;
            }
            dma_m2m();
        }
    }
    if (writecsr&DMA_CLRCOMPLETE) {
        dma[channel].csr &= ~DMA_COMPLETE;
    }
    if (!(dma[channel].csr&DMA_COMPLETE)) {
        set_interrupt(interrupt, RELEASE_INT);
    }
}

void DMA_Saved_Next_Read(void) { /* 0x02004000 */
    int channel = get_channel(IoAccessCurrentAddress-0x3FF0);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].saved_next);
    Log_Printf(LOG_DMA_LEVEL,"DMA SNext read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_next, m68k_getpc());
}

void DMA_Saved_Next_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF0);
    dma[channel].saved_next = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA SNext write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_next, m68k_getpc());
}

void DMA_Saved_Limit_Read(void) { /* 0x02004004 */
    int channel = get_channel(IoAccessCurrentAddress-0x3FF4);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].saved_limit);
    Log_Printf(LOG_DMA_LEVEL,"DMA SLimit read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_limit, m68k_getpc());
}

void DMA_Saved_Limit_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF4);
    dma[channel].saved_limit = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA SLimit write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_limit, m68k_getpc());
}

void DMA_Saved_Start_Read(void) { /* 0x02004008 */
    int channel = get_channel(IoAccessCurrentAddress-0x3FF8);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].saved_start);
    Log_Printf(LOG_DMA_LEVEL,"DMA SStart read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_start, m68k_getpc());
}

void DMA_Saved_Start_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF8);
    dma[channel].saved_start = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA SStart write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_start, m68k_getpc());
}

void DMA_Saved_Stop_Read(void) { /* 0x0200400c */
    int channel = get_channel(IoAccessCurrentAddress-0x3FFC);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].saved_stop);
    Log_Printf(LOG_DMA_LEVEL,"DMA SStop read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_stop, m68k_getpc());
}

void DMA_Saved_Stop_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FFC);
    dma[channel].saved_stop = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA SStop write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].saved_stop, m68k_getpc());
}

void DMA_Next_Read(void) { /* 0x02004010 */
    int channel = get_channel(IoAccessCurrentAddress-0x4000);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].next);
    Log_Printf(LOG_DMA_LEVEL,"DMA Next read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].next, m68k_getpc());
}

void DMA_Next_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4000);
    dma[channel].next = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA Next write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].next, m68k_getpc());
}

void DMA_Limit_Read(void) { /* 0x02004014 */
    int channel = get_channel(IoAccessCurrentAddress-0x4004);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].limit);
    Log_Printf(LOG_DMA_LEVEL,"DMA Limit read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].limit, m68k_getpc());
}

void DMA_Limit_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4004);
    dma[channel].limit = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA Limit write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].limit, m68k_getpc());
}

void DMA_Start_Read(void) { /* 0x02004018 */
    int channel = get_channel(IoAccessCurrentAddress-0x4008);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].start);
    Log_Printf(LOG_DMA_LEVEL,"DMA Start read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].start, m68k_getpc());
}

void DMA_Start_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4008);
    dma[channel].start = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA Start write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].start, m68k_getpc());
}

void DMA_Stop_Read(void) { /* 0x0200401c */
    int channel = get_channel(IoAccessCurrentAddress-0x400C);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].stop);
    Log_Printf(LOG_DMA_LEVEL,"DMA Stop read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].stop, m68k_getpc());
}

void DMA_Stop_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x400C);
    dma[channel].stop = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_DMA_LEVEL,"DMA Stop write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].stop, m68k_getpc());
}

void DMA_Init_Read(void) { /* 0x02004210 */
    int channel = get_channel(IoAccessCurrentAddress-0x4200);
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].next);
    Log_Printf(LOG_DMA_LEVEL,"DMA Init read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].next, m68k_getpc());
}

void DMA_Init_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4200);
    dma[channel].next = IoMem_ReadLong(IoAccessCurrentAddress);
    dma_initialize_buffer(channel, dma[channel].next&0xF);
    Log_Printf(LOG_DMA_LEVEL,"DMA Init write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].next, m68k_getpc());
}


/* DMA interrupt functions */

static void dma_interrupt(int channel) {
    int interrupt = get_interrupt_type(channel);

    /* If we have reached limit, generate an interrupt and set the flags */
    if (dma[channel].next==dma[channel].limit) {
        
        dma[channel].csr |= DMA_COMPLETE;
        
        if (dma[channel].csr & DMA_SUPDATE) { /* if we are in chaining mode */
            dma[channel].next = dma[channel].start;
            dma[channel].limit = dma[channel].stop;
            /* Set bits in CSR */
            dma[channel].csr &= ~DMA_SUPDATE; /* 1st done */
        } else {
            dma[channel].csr &= ~DMA_ENABLE; /* all done */
        }
    }
    if (dma[channel].csr&DMA_COMPLETE) {
        set_interrupt(interrupt, SET_INT);
    }
}


/* DMA Read and Write Memory Functions */

/* Channel SCSI (shared with floppy drive) */
void dma_esp_write_memory(void) {
    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Write to memory at $%08x, %i bytes (ESP counter %i)",
               dma[CHANNEL_SCSI].next,dma[CHANNEL_SCSI].limit-dma[CHANNEL_SCSI].next,esp_counter);
    
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Error! DMA not enabled!");
        return;
    }
    if ((dma[CHANNEL_SCSI].limit%DMA_BURST_SIZE) || (dma[CHANNEL_SCSI].next%4)) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_SCSI].next, dma[CHANNEL_SCSI].limit);
        abort();
    }

    TRY(prb) {
        if (espdma_buf_size>0) {
            Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Starting with %i residual bytes in DMA buffer.", espdma_buf_size);
        }

        while (dma[CHANNEL_SCSI].next<=dma[CHANNEL_SCSI].limit) {
            /* Fill DMA channel FIFO (only if limit < FIFO size) */
            if (espdma_buf_limit<DMA_BURST_SIZE) {
                if (floppy_select) {
                    while (espdma_buf_limit<DMA_BURST_SIZE && flp_buffer.size>0) {
                        espdma_buf[espdma_buf_limit]=flp_buffer.data[flp_buffer.limit-flp_buffer.size];
                        flp_buffer.size--;
                        espdma_buf_limit++;
                        espdma_buf_size++;
                    }
                } else {
                    while (espdma_buf_limit<DMA_BURST_SIZE && ESP_Send_Ready()) {
                        espdma_buf[espdma_buf_limit]=ESP_Send_Data();
                        espdma_buf_limit++;
                        espdma_buf_size++;
                    }
                }
            }
            
            if (espdma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: No more data. Stopping with %i residual bytes.",
                           espdma_buf_size);
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                ESP_DMA_set_status();

                while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit && espdma_buf_size>0) {
                    put_long(dma[CHANNEL_SCSI].next, dma_getlong(espdma_buf, DMA_BURST_SIZE-espdma_buf_size));
                    dma[CHANNEL_SCSI].next+=4;
                    espdma_buf_size-=4;
                }
                if (espdma_buf_size>0) { /* Not complete, stop */
                    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Channel limit reached. Stopping with %i residual bytes.",
                               espdma_buf_size);
                    break;
                }
                espdma_buf_limit = espdma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Bus error while writing to %08x",dma[CHANNEL_SCSI].next);
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    dma_interrupt(CHANNEL_SCSI);
}

void dma_esp_flush_buffer(void) {
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Not flushing buffer. DMA not enabled.");
        return;
    }
    if (dma[CHANNEL_SCSI].direction!=DMA_DEV2M) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Not flushing buffer. Bad direction!");
        return;
    }
    if ((dma[CHANNEL_SCSI].limit%DMA_BURST_SIZE) || (dma[CHANNEL_SCSI].next%4)) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_SCSI].next, dma[CHANNEL_SCSI].limit);
        abort();
    }

    TRY(prb) {
        if (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
            Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Flush buffer to memory at $%08x, 4 bytes",dma[CHANNEL_SCSI].next);
            /* If there is less than one long word in the buffer, fill the gap with 0 */
            while (espdma_buf_size < 4) {
                espdma_buf[espdma_buf_limit] = 0;
                espdma_buf_limit++;
                espdma_buf_size++;
            }
            /* Write one long word to memory */
            put_long(dma[CHANNEL_SCSI].next, dma_getlong(espdma_buf, espdma_buf_limit-espdma_buf_size));
            dma[CHANNEL_SCSI].next += 4;
            espdma_buf_size -= 4;
            if (espdma_buf_size == 0) {
                espdma_buf_limit = espdma_buf_size;
            }
        } else {
            Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Not flushing buffer. DMA done.");
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Bus error while flushing to %08x",dma[CHANNEL_SCSI].next);
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    dma_interrupt(CHANNEL_SCSI);
}

void dma_esp_read_memory(void) {
    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Read from memory at $%08x, %i bytes (ESP counter %i)",
               dma[CHANNEL_SCSI].next,dma[CHANNEL_SCSI].limit-dma[CHANNEL_SCSI].next,esp_counter);
    
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Error! DMA not enabled!");
        return;
    }
    if ((dma[CHANNEL_SCSI].limit%DMA_BURST_SIZE) || (dma[CHANNEL_SCSI].next%4)) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_SCSI].next, dma[CHANNEL_SCSI].limit);
        abort();
    }
    
    TRY(prb) {
        if (espdma_buf_size>0) {
            Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Starting with %i residual bytes in DMA buffer.", espdma_buf_size);
        }
        
        while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
            /* Read data from memory to DMA channel FIFO (only if limit < FIFO size) */
            if (espdma_buf_limit<DMA_BURST_SIZE) {
                while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit && espdma_buf_limit<DMA_BURST_SIZE) {
                    dma_putlong(get_long(dma[CHANNEL_SCSI].next), espdma_buf, espdma_buf_limit);
                    dma[CHANNEL_SCSI].next+=4;
                    espdma_buf_limit+=4;
                    espdma_buf_size+=4;
                }
            }
            
            if (espdma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Channel limit reached. Stopping with %i residual bytes.",
                           espdma_buf_size);
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                ESP_DMA_set_status();

                if (floppy_select) {
                    while (espdma_buf_size>0 && flp_buffer.size<flp_buffer.limit) {
                        flp_buffer.data[flp_buffer.size]=espdma_buf[espdma_buf_limit-espdma_buf_size];
                        flp_buffer.size++;
                        espdma_buf_size--;
                    }
                } else {
                    while (espdma_buf_size>0 && ESP_Receive_Ready()) {
                        ESP_Receive_Data(espdma_buf[espdma_buf_limit-espdma_buf_size]);
                        espdma_buf_size--;
                    }
                }
                if (espdma_buf_size>0) { /* Not complete, stop */
                    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: No more data request. Stopping with %i residual bytes.",
                               espdma_buf_size);
                    break;
                }
                espdma_buf_limit = espdma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Bus error while reading from %08x",dma[CHANNEL_SCSI].next);
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    if ((floppy_select && flp_buffer.size<flp_buffer.limit) || SCSIbus.phase==PHASE_DO) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Warning! Data not yet written to disk.");
        if (espdma_buf_size!=0) {
            Log_Printf(LOG_WARN, "[DMA] Channel SCSI: WARNING: Loss of data in DMA buffer possible!");
        }
    }
    
    dma_interrupt(CHANNEL_SCSI);
}


/* Channel MO */
void dma_mo_write_memory(void) {
    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Write to memory at $%08x, %i bytes",
               dma[CHANNEL_DISK].next,dma[CHANNEL_DISK].limit-dma[CHANNEL_DISK].next);
    
    if (!(dma[CHANNEL_DISK].csr&DMA_ENABLE)) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Error! DMA not enabled!");
        return;
    }
    if ((dma[CHANNEL_DISK].limit%DMA_BURST_SIZE) || (dma[CHANNEL_DISK].next%4)) {
        Log_Printf(LOG_WARN, "[DMA] Channel MO: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_DISK].next, dma[CHANNEL_DISK].limit);
        abort();
    }
    
    TRY(prb) {
        if (modma_buf_size>0) {
            Log_Printf(LOG_WARN, "[DMA] Channel MO: Starting with %i residual bytes in DMA buffer.", modma_buf_size);
        }
        
        while (dma[CHANNEL_DISK].next<=dma[CHANNEL_DISK].limit) {
            /* Fill DMA channel FIFO (only if limit < FIFO size) */
            if (modma_buf_limit<DMA_BURST_SIZE) {
                while (modma_buf_limit<DMA_BURST_SIZE && ecc_buffer[eccout].size>0) {
                    modma_buf[modma_buf_limit]=ecc_buffer[eccout].data[ecc_buffer[eccout].limit-ecc_buffer[eccout].size];
                    ecc_buffer[eccout].size--;
                    modma_buf_limit++;
                    modma_buf_size++;
                }
            }
            
            if (modma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: No more data. Stopping with %i residual bytes.",
                           modma_buf_size);
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit && modma_buf_size>0) {
                    put_long(dma[CHANNEL_DISK].next, dma_getlong(modma_buf, DMA_BURST_SIZE-modma_buf_size));
                    dma[CHANNEL_DISK].next+=4;
                    modma_buf_size-=4;
                }
                if (modma_buf_size>0) { /* Not complete, stop */
                    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Channel limit reached. Stopping with %i residual bytes.",
                               modma_buf_size);
                    break;
                }
                modma_buf_limit = modma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel MO: Bus error while writing to %08x",dma[CHANNEL_DISK].next);
        dma[CHANNEL_DISK].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DISK].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    dma_interrupt(CHANNEL_DISK);
}

void dma_mo_read_memory(void) {
    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Read from memory at $%08x, %i bytes",
               dma[CHANNEL_DISK].next,dma[CHANNEL_DISK].limit-dma[CHANNEL_DISK].next);
    
    if (!(dma[CHANNEL_DISK].csr&DMA_ENABLE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel MO: Error! DMA not enabled!");
        return;
    }
    if ((dma[CHANNEL_DISK].limit%DMA_BURST_SIZE) || (dma[CHANNEL_DISK].next%4)) {
        Log_Printf(LOG_WARN, "[DMA] Channel MO: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_DISK].next, dma[CHANNEL_DISK].limit);
        abort();
    }
    
    TRY(prb) {
        if (modma_buf_size>0) {
            Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Starting with %i residual bytes in DMA buffer.", modma_buf_size);
        }
        
        while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit) {
            /* Read data from memory to DMA channel FIFO (only if limit < FIFO size) */
            if (modma_buf_limit<DMA_BURST_SIZE) {
                while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit && modma_buf_limit<DMA_BURST_SIZE) {
                    dma_putlong(get_long(dma[CHANNEL_DISK].next), modma_buf, modma_buf_limit);
                    dma[CHANNEL_DISK].next+=4;
                    modma_buf_limit+=4;
                    modma_buf_size+=4;
                }
            }
            
            if (modma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Channel limit reached. Stopping with %i residual bytes.",
                           modma_buf_size);
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                while (modma_buf_size>0 && ecc_buffer[eccin].size<ecc_buffer[eccin].limit) {
                    ecc_buffer[eccin].data[ecc_buffer[eccin].size]=modma_buf[modma_buf_limit-modma_buf_size];
                    ecc_buffer[eccin].size++;
                    modma_buf_size--;
                }
                if (modma_buf_size>0) { /* Not complete, stop */
                    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: No more data request. Stopping with %i residual bytes.",
                               modma_buf_size);
                    break;
                }
                modma_buf_limit = modma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel MO: Bus error while reading from %08x",dma[CHANNEL_DISK].next);
        dma[CHANNEL_DISK].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DISK].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    if (ecc_buffer[eccin].size<ecc_buffer[eccin].limit) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel MO: Warning! Data not yet written to disk.");
        if (modma_buf_size!=0) {
            Log_Printf(LOG_WARN, "[DMA] Channel MO: WARNING: Loss of data in DMA buffer possible!");
        }
    }
    
    dma_interrupt(CHANNEL_DISK);
}


void dma_sndout_read_memory(void) {
    if (dma[CHANNEL_SOUNDOUT].csr&DMA_ENABLE) {
        
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Sound Out: Read from memory at $%08x, %i bytes",
                   dma[CHANNEL_SOUNDOUT].next,dma[CHANNEL_SOUNDOUT].limit-dma[CHANNEL_SOUNDOUT].next);
        
        if ((dma[CHANNEL_SOUNDOUT].limit&3) || (dma[CHANNEL_SOUNDOUT].next&3)) {
            Log_Printf(LOG_WARN, "[DMA] Channel Sound Out: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                       dma[CHANNEL_SOUNDOUT].next, dma[CHANNEL_SOUNDOUT].limit);
            dma[CHANNEL_SOUNDOUT].next &= ~3;
            dma[CHANNEL_SOUNDOUT].limit &= ~3;
        }
        
        TRY(prb) {
            while (dma[CHANNEL_SOUNDOUT].next<dma[CHANNEL_SOUNDOUT].limit && snd_buffer_len<SND_BUFFER_LIMIT) {
                snd_buffer[snd_buffer_len] = get_byte(dma[CHANNEL_SOUNDOUT].next);
                dma[CHANNEL_SOUNDOUT].next++;
                snd_buffer_len++;
            }
        } CATCH(prb) {
            Log_Printf(LOG_WARN, "[DMA] Channel Sound Out: Bus error reading from %08x",dma[CHANNEL_SOUNDOUT].next);
            dma[CHANNEL_SOUNDOUT].csr &= ~DMA_ENABLE;
            dma[CHANNEL_SOUNDOUT].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
    }
}

void dma_sndout_intr(void) {
    snd_buffer_len = 0;

    if (dma[CHANNEL_SOUNDOUT].csr&DMA_ENABLE) {
        dma_interrupt(CHANNEL_SOUNDOUT);
    }
}

bool dma_sndin_write_memory(uint32_t val) {
    if (dma[CHANNEL_SOUNDIN].csr&DMA_ENABLE) {
        
        if ((dma[CHANNEL_SOUNDIN].next%4) || (dma[CHANNEL_SOUNDIN].limit%4)) {
            Log_Printf(LOG_WARN, "[DMA] Channel Sound In: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                       dma[CHANNEL_SOUNDIN].next, dma[CHANNEL_SOUNDIN].limit);
            abort();
        }

        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Sound In: Write to memory at $%08x, %i bytes",
                   dma[CHANNEL_SOUNDIN].next,dma[CHANNEL_SOUNDIN].limit-dma[CHANNEL_SOUNDIN].next);
        
        TRY(prb) {
            if (dma[CHANNEL_SOUNDIN].next<dma[CHANNEL_SOUNDIN].limit) {
                put_long(dma[CHANNEL_SOUNDIN].next, val);
                dma[CHANNEL_SOUNDIN].next+=4;
            }
        } CATCH(prb) {
            Log_Printf(LOG_WARN, "[DMA] Channel Sound In: Bus error reading from %08x",dma[CHANNEL_SOUNDIN].next);
            dma[CHANNEL_SOUNDIN].csr &= ~DMA_ENABLE;
            dma[CHANNEL_SOUNDIN].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
        
        dma[CHANNEL_SOUNDIN].saved_limit = dma[CHANNEL_SOUNDIN].next;
        
        return (dma[CHANNEL_SOUNDIN].next==dma[CHANNEL_SOUNDIN].limit);
    }
    return true;
}

bool dma_sndin_intr(void) {
    if (dma[CHANNEL_SOUNDIN].csr&DMA_ENABLE) {
        dma_interrupt(CHANNEL_SOUNDIN);
    }
    return !(dma[CHANNEL_SOUNDIN].csr&DMA_ENABLE);
}

/* Channel Printer */
void dma_printer_read_memory(void) {
    if (dma[CHANNEL_PRINTER].csr&DMA_ENABLE) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Printer: Read from memory at $%08x, %i bytes",
                   dma[CHANNEL_PRINTER].next,dma[CHANNEL_PRINTER].limit-dma[CHANNEL_PRINTER].next);
        
        if ((dma[CHANNEL_PRINTER].limit%4) || (dma[CHANNEL_PRINTER].next%4)) {
            Log_Printf(LOG_WARN, "[DMA] Channel Printer: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                       dma[CHANNEL_PRINTER].next, dma[CHANNEL_PRINTER].limit);
            abort();
        }
        
        TRY(prb) {
            while (dma[CHANNEL_PRINTER].next<dma[CHANNEL_PRINTER].limit && lp_buffer.size<lp_buffer.limit) {
                lp_buffer.data[lp_buffer.size]=get_byte(dma[CHANNEL_PRINTER].next);
                lp_buffer.size++;
                dma[CHANNEL_PRINTER].next++;
            }
        } CATCH(prb) {
            Log_Printf(LOG_WARN, "[DMA] Channel Printer: Bus error reading from %08x",dma[CHANNEL_PRINTER].next);
            dma[CHANNEL_PRINTER].csr &= ~DMA_ENABLE;
            dma[CHANNEL_PRINTER].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
        
        dma_interrupt(CHANNEL_PRINTER);
    }
}


/* Channel Ethernet (this channel does not use DMA buffering) */
#define EN_EOP      0x80000000 /* end of packet */
#define EN_BOP      0x40000000 /* beginning of packet */
#define ENADDR(x)   ((x)&~(EN_EOP|EN_BOP))

static void dma_enet_interrupt(int channel) {
    int interrupt = get_interrupt_type(channel);
    
    dma[channel].csr |= DMA_COMPLETE;
    
    if (dma[channel].csr & DMA_SUPDATE) { /* if we are in chaining mode */
        /* Update pointers */
        dma[channel].next = dma[channel].start;
        dma[channel].limit = dma[channel].stop;
        if (ConfigureParams.System.bTurbo) {
            dma[channel].saved_next = dma[channel].next;
        }
        /* Set bits in CSR */
        dma[channel].csr &= ~DMA_SUPDATE; /* 1st done */
    } else {
        dma[channel].csr &= ~DMA_ENABLE; /* all done */
    }
    set_interrupt(interrupt, SET_INT);
}

void dma_enet_write_memory(bool eop) {
    Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Ethernet Receive: Write to memory at $%08x, %i bytes",
               dma[CHANNEL_EN_RX].next,dma[CHANNEL_EN_RX].limit-dma[CHANNEL_EN_RX].next);
    
    if (!(dma[CHANNEL_EN_RX].csr&DMA_ENABLE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel Ethernet Receive: Error! DMA not enabled!");
        return;
    }
    if ((dma[CHANNEL_EN_RX].limit%DMA_BURST_SIZE) || (dma[CHANNEL_EN_RX].next%DMA_BURST_SIZE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel Ethernet Receive: Error! Bad alignment! (Next: $%08X, Limit: $%08X)",
                   dma[CHANNEL_EN_RX].next, dma[CHANNEL_EN_RX].limit);
        abort();
    }
    
    if (enet_rx_buffer.size == enet_rx_buffer.limit) {
        dma[CHANNEL_EN_RX].saved_next = dma[CHANNEL_EN_RX].next; /* confirmed */
    }
    
    TRY(prb) {
        while (dma[CHANNEL_EN_RX].next<dma[CHANNEL_EN_RX].limit && enet_rx_buffer.size>0) {
            put_byte(dma[CHANNEL_EN_RX].next, enet_rx_buffer.data[enet_rx_buffer.limit-enet_rx_buffer.size]);
            enet_rx_buffer.size--;
            dma[CHANNEL_EN_RX].next++;
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel Ethernet Receive: Bus error while writing to %08x",dma[CHANNEL_EN_RX].next);
        dma[CHANNEL_EN_RX].csr &= ~DMA_ENABLE;
        dma[CHANNEL_EN_RX].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    if (enet_rx_buffer.size==0) {
        /* Save last byte count */
        dma[CHANNEL_EN_RX].saved_limit = dma[CHANNEL_EN_RX].next;
        if (ConfigureParams.System.bTurbo) {
            saved_nibble = dma[CHANNEL_EN_RX].next%DMA_BURST_SIZE;
            if (!(dma[CHANNEL_EN_RX].csr&DMA_SUPDATE)) {
                dma[CHANNEL_EN_RX].next -= saved_nibble;
                if (dma[CHANNEL_EN_RX].next<dma[CHANNEL_EN_RX].limit) {
                    dma[CHANNEL_EN_RX].next += DMA_BURST_SIZE;
                }
            }
        }
        if (eop) { /* TODO: check if this is correct */
            Log_Printf(LOG_WARN, "[DMA] Channel Ethernet Receive: Last buffer of chain done.");
            dma[CHANNEL_EN_RX].next|=EN_BOP;
        }
    }

    dma_enet_interrupt(CHANNEL_EN_RX);
}

bool dma_enet_read_memory(void) {
    if (dma[CHANNEL_EN_TX].csr&DMA_ENABLE) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Ethernet Transmit: Read from memory at $%08x, %i bytes",
                   dma[CHANNEL_EN_TX].next,ENADDR(dma[CHANNEL_EN_TX].limit)-dma[CHANNEL_EN_TX].next);
        
        if (enet_tx_buffer.size == 0) {
            dma[CHANNEL_EN_TX].saved_next = dma[CHANNEL_EN_TX].next;
        }
        
        TRY(prb) {
            while (dma[CHANNEL_EN_TX].next<ENADDR(dma[CHANNEL_EN_TX].limit) && enet_tx_buffer.size<enet_tx_buffer.limit) {
                enet_tx_buffer.data[enet_tx_buffer.size]=get_byte(dma[CHANNEL_EN_TX].next);
                enet_tx_buffer.size++;
                dma[CHANNEL_EN_TX].next++;
            }
        } CATCH(prb) {
            Log_Printf(LOG_WARN, "[DMA] Channel Ethernet Transmit: Bus error while writing to %08x",dma[CHANNEL_EN_TX].next);
            dma[CHANNEL_EN_TX].csr &= ~DMA_ENABLE;
            dma[CHANNEL_EN_TX].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
        
        if (dma[CHANNEL_EN_TX].limit&EN_EOP) {
            Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel Ethernet Transmit: Packet done.");
            dma_enet_interrupt(CHANNEL_EN_TX);
            return true;
        }
        dma_enet_interrupt(CHANNEL_EN_TX);
    }
    return false;
}


/* Memory to Memory */

uint32_t m2m_buffer[DMA_BURST_SIZE];
int m2m_buffer_size;

void DMA_M2M_IO_Handler(void) {
    if (dma[CHANNEL_R2M].csr&DMA_ENABLE) {
        dma_m2m_write_memory();
        CycInt_UpdateCyclesEvent(4, EVENT_DMA_M2M_IO);
    }
}

void dma_m2m(void) {
    if ((dma[CHANNEL_M2R].csr&DMA_ENABLE) && (dma[CHANNEL_R2M].csr&DMA_ENABLE)) {
        if (((dma[CHANNEL_R2M].limit-dma[CHANNEL_R2M].next)%DMA_BURST_SIZE) ||
            ((dma[CHANNEL_M2R].limit-dma[CHANNEL_M2R].next)%DMA_BURST_SIZE)) {
            Log_Printf(LOG_WARN, "[DMA] Channel M2M: Error! Memory not burst size aligned!");
            return;
        }
        
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel M2M: Copying %i bytes from $%08X to %i bytes at $%08X.",
                   dma[CHANNEL_M2R].limit-dma[CHANNEL_M2R].next,dma[CHANNEL_M2R].next,
                   dma[CHANNEL_R2M].limit-dma[CHANNEL_R2M].next,dma[CHANNEL_R2M].next);
        
        CycInt_AddCyclesEvent(4, EVENT_DMA_M2M_IO);
    }
}

void dma_m2m_write_memory(void) {
    
    if (dma[CHANNEL_R2M].next<dma[CHANNEL_R2M].limit) {

        if (dma[CHANNEL_M2R].next<dma[CHANNEL_M2R].limit) {
            /* (Re)fill the buffer, if there is still data to read */
            m2m_buffer_size = 0;

            TRY(prb) {
                while (m2m_buffer_size < DMA_BURST_SIZE) {
                    m2m_buffer[m2m_buffer_size]=get_byte(dma[CHANNEL_M2R].next);
                    m2m_buffer_size++;
                    dma[CHANNEL_M2R].next++;
                }
            } CATCH(prb) {
                Log_Printf(LOG_WARN, "[DMA] Channel M2M: Bus error while reading from %08x",dma[CHANNEL_M2R].next);
                dma[CHANNEL_M2R].csr &= ~DMA_ENABLE;
                dma[CHANNEL_M2R].csr |= (DMA_COMPLETE|DMA_BUSEXC);
            } ENDTRY
            
            dma_interrupt(CHANNEL_M2R);
        } else {
            /* Re-use data in buffer */
            m2m_buffer_size = DMA_BURST_SIZE;
        }
        
        TRY(prb) {
            /* Write the contents of the buffer to memory */
            while (m2m_buffer_size > 0) {
                put_byte(dma[CHANNEL_R2M].next, m2m_buffer[DMA_BURST_SIZE-m2m_buffer_size]);
                m2m_buffer_size--;
                dma[CHANNEL_R2M].next++;
            }
        } CATCH(prb) {
            Log_Printf(LOG_WARN, "[DMA] Channel M2M: Bus error while writing to %08x",dma[CHANNEL_R2M].next);
            dma[CHANNEL_R2M].csr &= ~DMA_ENABLE;
            dma[CHANNEL_R2M].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
    }
    
    dma_interrupt(CHANNEL_R2M);
}


/* Channel DSP */
#define LOG_DMA_DSP_LEVEL LOG_DEBUG

void dma_dsp_write_memory(uint8_t val) {
    Log_Printf(LOG_DMA_DSP_LEVEL, "[DMA] Channel DSP: Write to memory at $%08x, %i bytes",
               dma[CHANNEL_DSP].next,dma[CHANNEL_DSP].limit-dma[CHANNEL_DSP].next);
    
    if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel DSP: Error! DMA not enabled!");
        return;
    }
    
    TRY(prb) {
        if (dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit) {
            put_byte(dma[CHANNEL_DSP].next, val);
            dma[CHANNEL_DSP].next++;
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel DSP: Bus error while writing to %08x",dma[CHANNEL_DSP].next);
        dma[CHANNEL_DSP].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DSP].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    if (dma[CHANNEL_DSP].next==dma[CHANNEL_DSP].limit) {
        DSP_SetIRQB();
        dma_interrupt(CHANNEL_DSP);
    }
}

uint8_t dma_dsp_read_memory(void) {
    uint8_t val = 0;
    
    Log_Printf(LOG_DMA_DSP_LEVEL, "[DMA] Channel DSP: Read from memory at $%08x, %i bytes",
               dma[CHANNEL_DSP].next,dma[CHANNEL_DSP].limit-dma[CHANNEL_DSP].next);
    
    if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE)) {
        Log_Printf(LOG_WARN, "[DMA] Channel DSP: Error! DMA not enabled!");
        return val;
    }
    
    TRY(prb) {
        if (dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit) {
            val = get_byte(dma[CHANNEL_DSP].next);
            dma[CHANNEL_DSP].next++;
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel DSP: Bus error while writing to %08x",dma[CHANNEL_DSP].next);
        dma[CHANNEL_DSP].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DSP].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    if (dma[CHANNEL_DSP].next==dma[CHANNEL_DSP].limit) {
        DSP_SetIRQB();
        dma_interrupt(CHANNEL_DSP);
    }
    return val;
}

bool dma_dsp_ready(void) {
    if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE) ||
        !(dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit)) {
        Log_Printf(LOG_DEBUG, "[DMA] Channel DSP: Not ready!");
        return false;
    } else {
        return true;
    }
}


/* ---------------------- DMA Scratchpad ---------------------- */

/* This is used to interrupt at vertical screen retrace.
 * TODO: find out how the interrupt is generated in real
 * hardware using the Limit register of the DMA chip.
 * (0xEA * 1024 = visible videomem size)
 */


/* Interrupt Handler (called from Video_InterruptHandler in video.c) */
void dma_video_interrupt(void) {
    if (dma[CHANNEL_VIDEO].limit==0xEA) {
        dma[CHANNEL_VIDEO].csr |= DMA_COMPLETE;
        set_interrupt(INT_VIDEO, SET_INT); /* interrupt is released by writing to CSR */
    } else if (dma[CHANNEL_VIDEO].limit && dma[CHANNEL_VIDEO].limit!=0xEA) {
        Log_Printf(LOG_WARN, "[DMA] Channel Video: Limit not supported: %08x", dma[CHANNEL_VIDEO].limit);
    }
}


/* Channel SCC */

uint8_t dma_scc_read_memory(void) {
    uint8_t val = 0;
    
    if (dma[CHANNEL_SCC].csr&DMA_ENABLE) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCC: Read from memory at $%08x", dma[CHANNEL_SCC].next);
        
        if (dma[CHANNEL_SCC].next<dma[CHANNEL_SCC].limit) {
            val = get_byte(dma[CHANNEL_SCC].next);
            dma[CHANNEL_SCC].next++;
        }
        dma_interrupt(CHANNEL_SCC);
    } else {
        Log_Printf(LOG_WARN, "[DMA] Channel SCC: Error! DMA not enabled!");
    }
    
    return val;
}

bool dma_scc_ready(void) {
    if (!(dma[CHANNEL_SCC].csr&DMA_ENABLE) ||
        !(dma[CHANNEL_SCC].next<dma[CHANNEL_SCC].limit)) {
        Log_Printf(LOG_DEBUG, "[DMA] Channel SCC: Not ready!");
        return false;
    } else {
        return true;
    }
}


/* DMA CSR on Turbo systems */

/* CSR read bits */
#define DMA_BYTECOUNT_MASK 0x00000007
#define DMA_WRITEPTR_MASK  0x00000018
#define DMA_READPTR_MASK   0x00000060
#define DMA_DIRTY_MASK     0x00000180
#define DMA_BUFSEL         0x00000200

/* CSR write bits */
#define DMA_SETCOMPLETE    0x00200000
#define DMA_FLUSH          0x00400000
#define DMA_BUFRESET       0x00800000

/* CSR masks */
#define DMA_CMD_MASK_T     0x00FB0000

void TDMA_CSR_Read(void) { /* 0x02000010 */
    int channel = get_channel(IoAccessCurrentAddress);
    
    IoMem_WriteLong(IoAccessCurrentAddress, dma[channel].csr);

    Log_Printf(LOG_DMA_LEVEL,"DMA CSR read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[channel].csr<<24, m68k_getpc());
}

void TDMA_CSR_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress);
    int interrupt = get_interrupt_type(channel);
    uint32_t writecsr = IoMem_ReadLong(IoAccessCurrentAddress);

    Log_Printf(LOG_DMA_LEVEL,"DMA CSR write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, writecsr, m68k_getpc());
    
    /* For debugging */
    if(writecsr&DMA_DEV2M)
        Log_Printf(LOG_DMA_LEVEL,"DMA from dev to mem");
    else
        Log_Printf(LOG_DMA_LEVEL,"DMA from mem to dev");
    
    switch (writecsr&DMA_CMD_MASK_T) {
        case DMA_RESET:
        case (DMA_RESET | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA reset"); break;
        case DMA_BUFRESET:
            Log_Printf(LOG_DMA_LEVEL,"DMA initialize buffers"); break;
        case (DMA_RESET | DMA_BUFRESET):
        case (DMA_RESET | DMA_BUFRESET | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA reset and initialize buffers"); break;
        case DMA_CLRCOMPLETE:
            Log_Printf(LOG_DMA_LEVEL,"DMA end chaining"); break;
        case (DMA_SETSUPDATE | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA continue chaining"); break;
        case DMA_SETENABLE:
            Log_Printf(LOG_DMA_LEVEL,"DMA start single transfer"); break;
        case (DMA_SETENABLE | DMA_SETSUPDATE):
        case (DMA_SETENABLE | DMA_SETSUPDATE | DMA_CLRCOMPLETE):
            Log_Printf(LOG_DMA_LEVEL,"DMA start chaining"); break;
        case 0:
            Log_Printf(LOG_DMA_LEVEL,"DMA no command"); break;
        default:
            Log_Printf(LOG_WARN,"DMA: unknown command (%08x)!", writecsr); break;
    }
    
    /* Handle CSR bits */
    dma[channel].direction = writecsr&DMA_DEV2M;
    
    if (writecsr&DMA_RESET) {
        dma[channel].csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
    }
    if (writecsr&DMA_BUFRESET) {
        dma_initialize_buffer(channel, 0);
    }
    if (writecsr&DMA_SETSUPDATE) {
        dma[channel].csr |= DMA_SUPDATE;
    }
    if (writecsr&DMA_SETENABLE) {
        dma[channel].csr |= DMA_ENABLE;
    }
    if (writecsr&DMA_CLRCOMPLETE) {
        dma[channel].csr &= ~DMA_COMPLETE;
    }
    if (!(dma[channel].csr&DMA_COMPLETE)) {
        set_interrupt(interrupt, RELEASE_INT);
    }
}

void TDMA_Saved_Limit_Read(void) { /* 0x02004050 */
    IoMem_WriteLong(IoAccessCurrentAddress, dma[CHANNEL_EN_RX].saved_limit);
    Log_Printf(LOG_DMA_LEVEL,"TDMA SNext read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, dma[CHANNEL_EN_RX].saved_limit, m68k_getpc());
}

/* Flush DMA buffer */
/* FIXME: Implement function for all buffered channels */
void tdma_esp_flush_buffer(void) {
    int i;
    
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Not flushing buffer. DMA not enabled.");
        return;
    }
    if (dma[CHANNEL_SCSI].direction!=DMA_DEV2M) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Not flushing buffer. Bad direction!");
        return;
    }
    
    TRY(prb) {
        Log_Printf(LOG_DMA_LEVEL, "[DMA] Channel SCSI: Flush buffer to memory at $%08x, %i bytes",
                   dma[CHANNEL_SCSI].next,espdma_buf_size);
        
        for (i = 0; i < DMA_BURST_SIZE; i++) {
            if (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
                if (espdma_buf_size > 0) {
                    put_byte(dma[CHANNEL_SCSI].next, espdma_buf[espdma_buf_limit-espdma_buf_size]);
                    espdma_buf_size--;
                } else {
                    put_byte(dma[CHANNEL_SCSI].next, 0);
                }
                dma[CHANNEL_SCSI].next++;
            }
        }
        if (espdma_buf_size == 0) {
            espdma_buf_limit = espdma_buf_size;
        }
    } CATCH(prb) {
        Log_Printf(LOG_WARN, "[DMA] Channel SCSI: Bus error while flushing to %08x",dma[CHANNEL_SCSI].next);
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    
    dma_interrupt(CHANNEL_SCSI);
}

void DMA_Reset(void) {
    int i;
    
    Log_Printf(LOG_WARN, "[DMA] Reset");
    
    for (i = 0; i < 12; i++) {
        dma[i].csr = 0;
        dma_initialize_buffer(i, 0);
    }
    CycInt_RemovePendingEvent(EVENT_DMA_M2M_IO);
}
