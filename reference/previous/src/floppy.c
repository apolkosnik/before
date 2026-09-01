/*
  Previous - floppy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the Intel 82077AA Floppy controller.
*/
const char Floppy_fileid[] = "Previous floppy.c";

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "configuration.h"
#include "sysReg.h"
#include "dma.h"
#include "floppy.h"
#include "cycInt.h"
#include "file.h"
#include "statusbar.h"

#define LOG_FLP_REG_LEVEL   LOG_DEBUG
#define LOG_FLP_CMD_LEVEL   LOG_DEBUG


FloppyBuffer flp_buffer;

/* Controller */
struct {
    uint8_t sra;      /* Status Register A (ro) */
    uint8_t srb;      /* Status Register B (ro) */
    uint8_t dor;      /* Digital Output Register (rw) */
    uint8_t msr;      /* Main Status Register (ro) */
    uint8_t dsr;      /* Data Rate Select Register (wo) */
    uint8_t fifo[16]; /* Data FIFO (rw) */
    uint8_t din;      /* Digital Input Register (wo) */
    uint8_t ccr;      /* Configuration Control Register (ro) */
    uint8_t csr;      /* External Control (rw) */
    
    uint8_t st[4];    /* Internal Status Registers */
    uint8_t pcn;      /* Cylinder Number */
    
    uint8_t sel;
    uint8_t eis;
} flp;

uint8_t floppy_select = 0;

/* Drives */
struct {
    uint8_t cyl;
    uint8_t head;
    uint8_t sector;
    uint8_t blocksize;
    
    FILE* dsk;
    uint32_t floppysize;
    
    uint32_t seekoffset;
    
    bool spinning;
    
    bool protected;
    bool inserted;
    bool connected;
} flpdrv[FLP_MAX_DRIVES];


/* Register bits */

#define SRA_INT         0x80
#define SRA_DRV1_N      0x40
#define SRA_STEP        0x20
#define SRA_TRK0_N      0x10
#define SRA_HDSEL       0x08
#define SRA_INDEX_N     0x04
#define SRA_WP_N        0x02
#define SRA_DIR         0x01

#define SRB_ALL1        0xC0
#define SRB_DRVSEL0_N   0x20
#define SRB_W_TOGGLE    0x10
#define SRB_R_TOGGLE    0x08
#define SRB_W_ENABLE    0x04
#define SRB_MOTEN_MASK  0x03

#define DOR_MOT3EN      0x80
#define DOR_MOT2EN      0x40
#define DOR_MOT1EN      0x20
#define DOR_MOT0EN      0x10
#define DOR_MOTEN_MASK  0xF0
#define DOR_DMA_N       0x08
#define DOR_RESET_N     0x04
#define DOR_SEL_MSK     0x03

#define MSR_RQM         0x80
#define MSR_DIO         0x40    /* read = 1 */
#define MSR_NONDMA      0x20
#define MSR_CMDBSY      0x10
#define MSR_DRV3BSY     0x08
#define MSR_DRV2BSY     0x04
#define MSR_DRV1BSY     0x02
#define MSR_DRV0BSY     0x01
#define MSR_BSY_MASK    0x0F

#define DSR_RESET       0x80
#define DSR_PDOWN       0x40
#define DSR_0           0x20
#define DSR_PRE_MASK    0x1C
#define DSR_RATE_MASK   0x03

#define DIR_DSKCHG      0x80
#define DIR_HIDENS_N    0x01

#define CCR_RATE_MASK   0x03

#define CTRL_EJECT      0x80
#define CTRL_82077      0x40
#define CTRL_RESET      0x20
#define CTRL_DRV_ID     0x04
#define CTRL_MEDIA_ID1  0x02
#define CTRL_MEDIA_ID0  0x01


enum {
    FLP_STATE_WRITE,
    FLP_STATE_READ,
    FLP_STATE_FORMAT,
    FLP_STATE_INTERRUPT,
    FLP_STATE_DONE
} flp_io_state;

int flp_sector_counter = 0;
int flp_io_drv = 0;


/* Internal registers */

#define ST0_IC      0xC0    /* Interrupt Code (0 = normal termination) */
#define ST0_SE      0x20    /* Seek End */
#define ST0_EC      0x10    /* Equipment Check */
#define ST0_H       0x04    /* Head Address */
#define ST0_DS      0x03    /* Drive Select */

#define ST1_EN      0x80    /* End of Cylinder */
#define ST1_DE      0x20    /* Data Error */
#define ST1_OR      0x10    /* Overrun/Underrun */
#define ST1_ND      0x04    /* No Data */
#define ST1_NW      0x02    /* Not writable */
#define ST1_MA      0x01    /* Missing Address Mark */

#define ST2_CM      0x40    /* Control Mark */
#define ST2_DD      0x20    /* Data Error in Data Field */
#define ST2_WC      0x10    /* Wrong Cylinder */
#define ST2_BC      0x02    /* Bad Cylinder */
#define ST2_MD      0x01    /* Missing Data Address Mark */

#define ST3_WP      0x40    /* Write Protected */
#define ST3_T0      0x10    /* Track 0 */
#define ST3_HD      0x04    /* Head Address */
#define ST3_DS      0x03    /* Drive Select */


#define IC_NORMAL   0x00
#define IC_ABNORMAL 0x40
#define IC_INV_CMD  0x80



static void floppy_start(void) {
    flp.sra &= ~(SRA_INT|SRA_STEP|SRA_HDSEL|SRA_DIR);
    flp.srb &= ~(SRB_R_TOGGLE|SRB_W_TOGGLE);
    flp.msr &= ~MSR_DIO;
    flp.msr |= MSR_RQM;
    
    set_interrupt(INT_PHONE, RELEASE_INT);
    
    /* Single poll interrupt after reset */
    flp_io_state = FLP_STATE_INTERRUPT;
    CycInt_AddTimeEvent(1000*1000, 0, EVENT_FLOPPY_IO);
}

static void floppy_stop(void) {
    flp.sra &= ~SRA_INT;
    set_interrupt(INT_PHONE, RELEASE_INT);
    flp_io_state = FLP_STATE_DONE;
    CycInt_RemovePendingEvent(EVENT_FLOPPY_IO);
}

static void floppy_reset(void) {
    Log_Printf(LOG_WARN,"[Floppy] Reset");
    
    flp.dor = flp.sel = 0;
    
    flp.sra &= ~(SRA_INT|SRA_STEP|SRA_HDSEL|SRA_DIR);
    flp.srb &= ~(SRB_R_TOGGLE|SRB_W_TOGGLE);
    flp.srb &= ~(SRB_MOTEN_MASK|SRB_DRVSEL0_N);
    flp.din &= ~DIR_HIDENS_N;
    
    set_interrupt(INT_PHONE, RELEASE_INT);
    
    flp.st[0] = flp.st[1] = flp.st[2] = flp.st[3] = 0;
    flp.pcn = 0;
    
    floppy_stop();
}


/* Floppy commands */

#define CMD_MT_MSK      0x80
#define CMD_MFM_MSK     0x40
#define CMD_SK_MSK      0x20
#define CMD_OPCODE_MSK  0x1F

#define CMD_READ        0x06
#define CMD_READ_DEL    0x0C
#define CMD_WRITE       0x05
#define CMD_WRITE_DEL   0x09
#define CMD_READ_TRK    0x02
#define CMD_VERIFY      0x16
#define CMD_VERSION     0x10
#define CMD_FORMAT      0x0D
#define CMD_SCAN_E      0x11
#define CMD_SCAN_LE     0x19
#define CMD_SCAN_HE     0x1D
#define CMD_RECAL       0x07
#define CMD_INTSTAT     0x08
#define CMD_SPECIFY     0x03
#define CMD_DRV_STATUS  0x04
#define CMD_SEEK        0x0F
#define CMD_CONFIGURE   0x13
#define CMD_DUMPREG     0x0E
#define CMD_READ_ID     0x0A
#define CMD_PERPEND     0x12
#define CMD_LOCK        0x14

uint8_t cmd_data_size[] = {
    0,0,8,2,1,8,8,1,
    0,8,1,0,8,5,0,2,
    0,8,1,3,0,0,8,0,
    0,8,0,0,0,8,0,0
};

static bool cmd_phase = false;
static int cmd_size = 0;
static int cmd_limit = 0;
static uint8_t command;
static uint8_t cmd_data[8];

static int result_size = 0;

static void floppy_interrupt(void) {
    Log_Printf(LOG_FLP_CMD_LEVEL,"[Floppy] Interrupt.");
    
    if (result_size>0) {
        /* Go to result phase */
        flp.msr |= (MSR_RQM|MSR_DIO);
    } else {
        flp.msr |= MSR_RQM;
    }
    
    flp.msr &= ~(MSR_BSY_MASK);
    flp.sra |= SRA_INT;
    set_interrupt(INT_PHONE, SET_INT);
}

/* -- Helpers -- */

/* Geometry */
#define SIZE_720K    737280
#define SIZE_1440K  1474560
#define SIZE_2880K  2949120

#define NUM_CYLINDERS   80
#define TRACKS_PER_CYL  2

static uint32_t get_logical_sec(int drive) {
    uint32_t blocksize = 0x80<<flpdrv[drive].blocksize;
    uint32_t spt = flpdrv[drive].floppysize/blocksize/TRACKS_PER_CYL/NUM_CYLINDERS;
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Geometry: Cylinders: %i, Tracks per cylinder: %i, Sectors per track: %i, Blocksize: %i",
               NUM_CYLINDERS,TRACKS_PER_CYL,spt,blocksize);
    
    if (flpdrv[drive].sector>spt) {
        Log_Printf(LOG_WARN, "[Floppy] Geometry error: sector (%i) beyond limit (%i)!",flpdrv[drive].sector,spt);
        flp.st[0] |= IC_ABNORMAL;
        flp.st[1] |= ST1_EN;
    }
    if (flpdrv[drive].cyl>=NUM_CYLINDERS) {
        Log_Printf(LOG_WARN, "[Floppy] Geometry error: cyclinder (%i) beyond limit (%i)!",flpdrv[drive].cyl,NUM_CYLINDERS-1);
        flp.st[0] |= IC_ABNORMAL;
        flp.st[1] |= ST1_ND;
    }
    
    return (((flpdrv[drive].cyl*TRACKS_PER_CYL)+flpdrv[drive].head)*spt)+flpdrv[drive].sector-1;
}

static void check_blocksize(int drive, uint8_t blocksize) {
    if (blocksize!=flpdrv[drive].blocksize) {
        Log_Printf(LOG_WARN, "[Floppy] Geometry error: Blocksize not supported (%i)!",blocksize);
        flp.st[0] |= IC_ABNORMAL;
        flp.st[1] |= ST1_ND;
    }
}

static void check_protection(int drive) {
    if (flpdrv[drive].protected) {
        Log_Printf(LOG_WARN, "[Floppy] Protection error: Disk is read-only!");
        flp.st[0] |= IC_ABNORMAL;
        flp.st[1] |= ST1_NW;
    }
}

static void floppy_seek_track(uint8_t c, int drive) {
    flp.st[0] |= ST0_SE;
    
    flpdrv[drive].seekoffset = (flpdrv[drive].cyl < c) ? (c - flpdrv[drive].cyl) : (flpdrv[drive].cyl - c);
    flpdrv[drive].cyl = c;
}

/* Timings */
#define FLP_SEEK_TIME 200000 /* 200 ms */

static int get_sector_time(int drive) {
    switch (flpdrv[drive].floppysize) {
        case SIZE_720K: return 22000;
        case SIZE_1440K: return 11000;
        case SIZE_2880K: return 5500;
        default: return 1000;
    }
}

static int get_seek_time(int drive) {
    if (flpdrv[drive].seekoffset > NUM_CYLINDERS) {
        return FLP_SEEK_TIME;
    }
    
    return (flpdrv[drive].seekoffset * FLP_SEEK_TIME / NUM_CYLINDERS);
}

/* Media IDs for control register */
#define MEDIA_ID_NONE   0
#define MEDIA_ID_720K   3
#define MEDIA_ID_1440K  2
#define MEDIA_ID_2880K  1
#define MEDIA_ID_MSK    3

static uint8_t get_media_id(int drive) {
    if (!flpdrv[drive].inserted) {
        return MEDIA_ID_NONE;
    } else {
        switch (flpdrv[drive].floppysize) {
            case SIZE_720K: return MEDIA_ID_720K;
            case SIZE_1440K: return MEDIA_ID_1440K;
            case SIZE_2880K: return MEDIA_ID_2880K;
            default: return MEDIA_ID_NONE;
        }
    }
}

/* Validate data rate */
#define CCR_RATE250     0x02
#define CCR_RATE500     0x00
#define CCR_RATE1000    0x03

static void check_data_rate(int drive) {
    switch (flpdrv[drive].floppysize) {
        case SIZE_720K:
            if ((flp.ccr&CCR_RATE_MASK)==CCR_RATE250) {
                return;
            }
            break;
        case SIZE_1440K:
            if ((flp.ccr&CCR_RATE_MASK)==CCR_RATE500) {
                return;
            }
            break;
        case SIZE_2880K:
            if ((flp.ccr&CCR_RATE_MASK)==CCR_RATE1000) {
                return;
            }
            break;

        default:
            break;
    }
    Log_Printf(LOG_WARN, "[Floppy] Invalid data rate %02X",flp.ccr&CCR_RATE_MASK);
    flp.st[0] |= IC_ABNORMAL;
    flp.st[1] |= ST1_ND;
}

static void send_rw_status(int drive) {
    flp.st[0] |= drive|(flpdrv[drive].head<<2);
    flp.fifo[0] = flp.st[0];
    flp.fifo[1] = flp.st[1];
    flp.fifo[2] = flp.st[2];
    flp.fifo[3] = flpdrv[drive].cyl;
    flp.fifo[4] = flpdrv[drive].head;
    flp.fifo[5] = flpdrv[drive].sector;
    flp.fifo[6] = flpdrv[drive].blocksize;
    result_size = 7;
}

/* -- Floppy commands -- */

static void floppy_read(void) {
    int drive = cmd_data[0]&0x03;
    int head = (cmd_data[0]&0x04)>>2;
    uint8_t c = cmd_data[1];
    uint8_t h = cmd_data[2];
    uint8_t s = cmd_data[3];
    uint8_t bs = cmd_data[4];
    
    uint32_t sector_size, num_sectors;
    
    flp.st[0] = flp.st[1] = flp.st[2] = 0;
    
    /* Select head */
    flpdrv[drive].head = head;

    /* If implied seek is enabled, seek track */
    if (flp.eis) {
        floppy_seek_track(c, drive);
    }
    /* Set start sector */
    flpdrv[drive].sector = s;
    
    /* Match actual track with specified track */
    if (flpdrv[drive].cyl!=c || flpdrv[drive].head!=h) {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Read: track mismatch!");
        flp.st[0]|=IC_ABNORMAL;
        flp.st[2]|=ST2_WC;
    }
    /* Validate blocksize */
    check_blocksize(drive,bs);
    sector_size = 0x80<<bs;
    
    /* Validate data rate */
    check_data_rate(drive);

    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Read: Cylinder=%i, Head=%i, Sector=%i, Blocksize=%i",
               flpdrv[drive].cyl,flpdrv[drive].head,flpdrv[drive].sector,sector_size);
    
    /* Get sector transfer count and logical sector offset */
    num_sectors = cmd_data[5]-cmd_data[3]+1;
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Read %i sectors at offset %i",num_sectors,get_logical_sec(drive));
    
    if (flp.st[0]&IC_ABNORMAL) {
        send_rw_status(drive);
        flp_io_state = FLP_STATE_INTERRUPT;
    } else {
        flp_buffer.size = 0;
        flp_buffer.limit = sector_size;
        flp_sector_counter = num_sectors;
        flp_io_drv = drive;
        flp_io_state = FLP_STATE_READ;
    }
    CycInt_AddTimeEvent(get_seek_time(drive) + get_sector_time(drive), 100, EVENT_FLOPPY_IO);
}

static void floppy_write(void) {
    int drive = cmd_data[0]&0x03;
    int head = (cmd_data[0]&0x04)>>2;
    uint8_t c = cmd_data[1];
    uint8_t h = cmd_data[2];
    uint8_t s = cmd_data[3];
    uint8_t bs = cmd_data[4];
    
    uint32_t sector_size, num_sectors;
    
    flp.st[0] = flp.st[1] = flp.st[2] = 0;
    
    /* Select head */
    flpdrv[drive].head = head;

    /* If implied seek is enabled, seek track */
    if (flp.eis) {
        floppy_seek_track(c, drive);
    }
    /* Set start sector */
    flpdrv[drive].sector = s;
    
    /* Match actual track with specified track */
    if (flpdrv[drive].cyl!=c || flpdrv[drive].head!=h) {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Write: track mismatch!");
        flp.st[0]|=IC_ABNORMAL;
        flp.st[2]|=ST2_WC;
    }
    /* Validate blocksize */
    check_blocksize(drive,bs);
    sector_size = 0x80<<bs;
    
    /* Validate data rate */
    check_data_rate(drive);
    
    /* Check protection */
    check_protection(drive);
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Write: Cylinder=%i, Head=%i, Sector=%i, Blocksize=%i",
               flpdrv[drive].cyl,flpdrv[drive].head,flpdrv[drive].sector,sector_size);
    
    /* Get sector transfer count and logical sector offset */
    num_sectors = cmd_data[5]-cmd_data[3]+1;
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Write %i sectors at offset %i",num_sectors,get_logical_sec(drive));
    
    if (flp.st[0]&IC_ABNORMAL) {
        send_rw_status(drive);
        flp_io_state = FLP_STATE_INTERRUPT;
    } else {
        flp_buffer.size = 0;
        flp_buffer.limit = sector_size;
        flp_sector_counter = num_sectors;
        flp_io_drv = drive;
        flp_io_state = FLP_STATE_WRITE;
    }
    CycInt_AddTimeEvent(get_seek_time(drive) + get_sector_time(drive), 100, EVENT_FLOPPY_IO);
}

static void floppy_format(void) {
    int drive = cmd_data[0]&0x03;
    int head = (cmd_data[0]&0x04)>>2;
    uint8_t bs = cmd_data[1];

    uint32_t num_sectors;
    
    flp.st[0] = flp.st[1] = flp.st[2] = 0;
    
    /* Select head */
    flpdrv[drive].head = head;
    
    /* Set start sector */
    flpdrv[drive].sector = 1;
    
    /* Validate blocksize */
    check_blocksize(drive,bs);
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Format: Cylinder=%i, Head=%i, Sector=%i, Blocksize=%i",
               flpdrv[drive].cyl,flpdrv[drive].head,flpdrv[drive].sector,0x80<<bs);
    
    /* Get sector transfer count and logical sector offset */
    num_sectors = cmd_data[2];
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Format %i sectors at offset %i",num_sectors,get_logical_sec(drive));

    /* Validate data rate */
    check_data_rate(drive);
    
    /* Check protection */
    check_protection(drive);
    
    if (flp.st[0]&IC_ABNORMAL) {
        send_rw_status(drive);
        flp_io_state = FLP_STATE_INTERRUPT;
    } else {
        flp_buffer.size = 0;
        flp_buffer.limit = 4;
        flp_sector_counter = num_sectors;
        flp_io_drv = drive;
        flp_io_state = FLP_STATE_FORMAT;
        CycInt_AddTimeEvent(get_sector_time(drive), 100, EVENT_FLOPPY_IO);
    }
}

static void floppy_read_id(void) {
    int drive = cmd_data[0]&0x03;
    int head = (cmd_data[0]&0x04)>>2;
        
    flpdrv[drive].head = head;
    
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Read ID: Cylinder=%i, Head=%i, Sector=%i, Blocksize=%i",
               flpdrv[drive].cyl,flpdrv[drive].head,flpdrv[drive].sector,0x80<<flpdrv[drive].blocksize);
    
    flp.st[0] = flp.st[1] = flp.st[2] = 0;
    
    check_data_rate(drive); /* check data rate */
    send_rw_status(drive);
    
    flp_io_state = FLP_STATE_INTERRUPT;
    CycInt_AddTimeEvent(get_sector_time(drive), 100, EVENT_FLOPPY_IO);
}

static void floppy_recalibrate(void) {
    int drive = cmd_data[0]&0x03;
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Recalibrate");

    if (flpdrv[drive].connected) {
        flp.msr |= (1<<drive); /* drive busy */
        flpdrv[drive].cyl = flp.pcn = 0;

        flp.st[0] = flp.st[1] = flp.st[2] = 0;
        floppy_seek_track(0, drive);

        /* Done */
        flp.st[0] = IC_NORMAL|ST0_SE;
        flp.sra &= ~SRA_TRK0_N;
        
        flp_io_state = FLP_STATE_INTERRUPT;
        CycInt_AddTimeEvent(get_seek_time(drive), 100, EVENT_FLOPPY_IO);
    }
}

static void floppy_seek(uint8_t relative) {
    int drive = cmd_data[0]&0x03;
    int head = (cmd_data[0]&0x04)>>2;
    flp.st[0] = flp.st[1] = flp.st[2] = 0;
    
    flpdrv[drive].head = head;
    
    if (relative) {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Relative seek: Head %i: %i cylinders",head,0);
        abort();
    } else {
        flp.st[0] = IC_NORMAL;
                
        floppy_seek_track(cmd_data[1], drive);
        
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Seek: Head %i to cylinder %i",head,flpdrv[drive].cyl);

        if (!(flp.st[0]&IC_ABNORMAL)) {
            flp.pcn = flpdrv[drive].cyl;
        }
    }
        
    flp_io_state = FLP_STATE_INTERRUPT;
    CycInt_AddTimeEvent(get_seek_time(drive), 100, EVENT_FLOPPY_IO);
}

static void floppy_interrupt_status(void) {
    /* Release interrupt */
    flp.sra &= ~SRA_INT;
    set_interrupt(INT_PHONE, RELEASE_INT);
    /* Go to result phase */
    flp.msr |= (MSR_RQM|MSR_DIO);
    /* Return data */
    flp.fifo[0] = flp.st[0];
    flp.fifo[1] = flp.pcn;
    result_size = 2;
}

static void floppy_specify(void) {
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Specify: %02X %02X",cmd_data[0],cmd_data[1]);

    if (cmd_data[1]&0x01) {
        Log_Printf(LOG_WARN, "[Floppy] Specify: Non-DMA mode");
    } else {
        Log_Printf(LOG_WARN, "[Floppy] Specify: DMA mode");
    }
    flp.msr |= MSR_RQM;
}

static void floppy_configure(void) {
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Configure: %02X %02X %02X",cmd_data[0],cmd_data[1],cmd_data[2]);
    
    flp.eis = cmd_data[1]&0x40; /* Enable or disable implied seek */

    if (cmd_data[1]&0x10) {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Configure: disable polling");
        if (CycInt_EventPending(EVENT_FLOPPY_IO)) {
            Log_Printf(LOG_WARN, "[Floppy] Disable pending reset poll interrupt");
            CycInt_RemovePendingEvent(EVENT_FLOPPY_IO);
        }
    }
    if (cmd_data[2]) {
        abort();
    }
    flp.msr |= MSR_RQM;
}

static void floppy_perpendicular(void) {
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Perpendicular: %02X",cmd_data[0]);
    flp.msr |= MSR_RQM;
}

static void floppy_unimplemented(void) {
    flp.st[0] = IC_INV_CMD;
    
    flp.fifo[0] = flp.st[0];
    result_size = 1;
    
    flp_io_state = FLP_STATE_INTERRUPT;
    CycInt_AddTimeEvent(1000, 100, EVENT_FLOPPY_IO);
}

static void floppy_execute_cmd(void) {
    Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Executing %02X",command);
    
    switch (command&CMD_OPCODE_MSK) {
        case CMD_READ:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Read");
            floppy_read();
            break;
        case CMD_READ_DEL:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Read and delete");
            abort();
            break;
        case CMD_WRITE:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Write");
            floppy_write();
            break;
        case CMD_WRITE_DEL:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Write and delete");
            abort();
            break;
        case CMD_READ_TRK:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Read track");
            abort();
            break;
        case CMD_VERIFY:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Verify");
            abort();
            break;
        case CMD_VERSION:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Version");
            abort();
            break;
        case CMD_FORMAT:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Format");
            floppy_format();
            break;
        case CMD_SCAN_E:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Scan equal");
            abort();
            break;
        case CMD_SCAN_LE:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Scan lower or equal");
            abort();
            break;
        case CMD_SCAN_HE:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Scan higher or equal");
            abort();
            break;
        case CMD_RECAL:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Recalibrate");
            floppy_recalibrate();
            break;
        case CMD_INTSTAT:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Interrupt status");
            floppy_interrupt_status();
            break;
        case CMD_SPECIFY:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Specify");
            floppy_specify();
            break;
        case CMD_DRV_STATUS:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Drive status");
            abort();
            break;
        case CMD_SEEK:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Seek");
            floppy_seek(command&0x80);
            break;
        case CMD_CONFIGURE:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Configure");
            floppy_configure();
            break;
        case CMD_DUMPREG:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Dump register");
            abort();
            break;
        case CMD_READ_ID:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Read ID");
            floppy_read_id();
            break;
        case CMD_PERPEND:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Perpendicular");
            floppy_perpendicular();
            break;
        case CMD_LOCK:
            Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Command: Lock");
            abort();
            break;

        default:
            Log_Printf(LOG_WARN, "[Floppy] Command: Unknown");
            floppy_unimplemented();
            break;
    }
    Statusbar_BlinkLed(DEVICE_LED_FD);
}

static void floppy_fifo_write(uint8_t val) {
    
    flp.msr &= ~MSR_RQM;
    
    if (!cmd_phase) {
        cmd_phase = true;
        command = val;
        cmd_limit = cmd_size = cmd_data_size[val&CMD_OPCODE_MSK];
        flp.msr |= MSR_RQM;
    } else if (cmd_size>0) {
        cmd_data[cmd_limit-cmd_size]=val;
        cmd_size--;
        flp.msr |= MSR_RQM;
    }
    
    if (cmd_size==0) {
        cmd_phase = false;
        floppy_execute_cmd();
    }
}

static uint8_t floppy_fifo_read(void) {
    int i;
    uint8_t val;
    if (result_size>0) {
        val = flp.fifo[0];
        for (i=0; i<(16-1); i++)
            flp.fifo[i]=flp.fifo[i+1];
        flp.fifo[16-1] = 0x00;
        result_size--;
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] FIFO reading byte, val=%02x", val);
    } else {
        Log_Printf(LOG_WARN,"[Floppy] FIFO is emtpy!");
        val = 0;
    }
    
    if (result_size==0) {
        flp.msr |= MSR_RQM;
        flp.msr &= ~MSR_DIO;
        flp.sra &= ~SRA_INT;
        set_interrupt(INT_PHONE, RELEASE_INT);
    }
    
    return val;
}

static void floppy_dor_write(uint8_t val) {
    uint8_t changed = flp.dor ^ val;
    
    flp.dor = val;
    flp.sel = val&DOR_SEL_MSK;

    if (changed&DOR_SEL_MSK) {
        Log_Printf(LOG_WARN,"[Floppy] Selecting drive %i.",val&DOR_SEL_MSK);
    }
    if (changed&DOR_RESET_N) {
        if (flp.dor&DOR_RESET_N) {
            Log_Printf(LOG_WARN,"[Floppy] Leaving reset state.");
            floppy_start();
        } else {
            Log_Printf(LOG_WARN,"[Floppy] Entering reset state.");
            floppy_stop();
        }
    }
    if (changed&DOR_MOTEN_MASK) {
        if (flpdrv[flp.sel].connected) {
            if (flp.dor&(DOR_MOT0EN<<flp.sel)) { /* motor enable */
                Log_Printf(LOG_WARN,"[Floppy] Starting motor of drive %i.",flp.sel);
                flpdrv[flp.sel].spinning = true;
            } else {
                Log_Printf(LOG_WARN,"[Floppy] Stopping motor of drive %i.",flp.sel);
                flpdrv[flp.sel].spinning = false;
            }
        }
    }
}

static uint8_t floppy_dor_read(void) {
    int i;
    uint8_t val = flp.dor&~DOR_MOTEN_MASK;  /* clear motor bits */
    for (i = 0; i < FLP_MAX_DRIVES; i++) {
        if (flpdrv[i].spinning) {           /* set motor bit if motor is on */
            val |= DOR_MOT0EN<<i;
        }
    }
    return val;
}

static uint8_t floppy_sra_read(void) {
    uint8_t val = flp.sra;
    if (!flpdrv[flp.sel].protected) {
        val|=SRA_WP_N;
    }
    return val;
}

static void floppy_ctrl_write(uint8_t val) {
    if (ConfigureParams.System.nMachineType != NEXT_CUBE030) {
        set_floppy_select(val&CTRL_82077,false);
    }
    if (val&CTRL_RESET) {
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Resetting floppy controller");
        floppy_reset();
    }
    if (val&CTRL_EJECT) {
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Ejecting floppy disk");
        Floppy_Eject(-1);
    }
}

static uint8_t floppy_stat_read(void) {
    flp.csr &= ~(CTRL_DRV_ID|CTRL_MEDIA_ID0|CTRL_MEDIA_ID1);
    
    if (flpdrv[flp.sel].connected) {
        flp.csr |= get_media_id(flp.sel);
    } else if (ConfigureParams.System.nMachineType != NEXT_CUBE030) {
        flp.csr |= CTRL_DRV_ID;
    }
    return flp.csr;
}


/* -- Floppy I/O functions -- */

static void floppy_rw_nodata(void) {
    Log_Printf(LOG_WARN, "[Floppy] No more data from DMA. Stopping.");
    /* Stop transfer */
    flp_sector_counter=0;
    flp.st[0] |= IC_ABNORMAL;
    flp.st[1] |= ST1_OR;
    send_rw_status(flp_io_drv);
}

static void floppy_read_sector(void) {
    int drive = flp_io_drv;
    
    /* Read from image */
    uint32_t sec_size = 0x80<<flpdrv[drive].blocksize;
    uint32_t logical_sec = get_logical_sec(drive);
    
    if (flp.st[0]&IC_ABNORMAL) {
        Log_Printf(LOG_WARN, "[Floppy] Read error. Bad sector offset (%i).",logical_sec);
        flp_sector_counter=0; /* stop the transfer */
        flp_io_state = FLP_STATE_INTERRUPT;
        send_rw_status(drive);
        return;
    } else {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Read sector at offset %i",logical_sec);

        flp_buffer.size = flp_buffer.limit = sec_size;
        File_Read(flp_buffer.data, flp_buffer.size, logical_sec*sec_size, flpdrv[drive].dsk);
        flpdrv[drive].sector++;
        flp_sector_counter--;
    }
    
    if (flp_sector_counter==0) {
        flp.st[0] = IC_ABNORMAL; /* Strange behavior of NeXT hardware */
        flp.st[1] |= ST1_EN;
        send_rw_status(drive);
    }
}

static void floppy_write_sector(void) {
    int drive = flp_io_drv;
    
    /* Write to image */
    uint32_t sec_size = 0x80<<flpdrv[drive].blocksize;
    uint32_t logical_sec = get_logical_sec(drive);
    
    if (flp.st[0]&IC_ABNORMAL) {
        Log_Printf(LOG_WARN, "[Floppy] Write error. Bad sector offset (%i).",logical_sec);
        flp_sector_counter=0; /* stop the transfer */
        flp_io_state = FLP_STATE_INTERRUPT;
        send_rw_status(drive);
        return;
    } else {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Write sector at offset %i",logical_sec);
        
        File_Write(flp_buffer.data, flp_buffer.size, logical_sec*sec_size, flpdrv[drive].dsk);
        flp_buffer.size = 0;
        flp_buffer.limit = sec_size;
        flpdrv[drive].sector++;
        flp_sector_counter--;
    }
    
    if (flp_sector_counter==0) {
        flp.st[0] = IC_ABNORMAL; /* Strange behavior of NeXT hardware */
        flp.st[1] |= ST1_EN;
        send_rw_status(drive);
    }
}

static void floppy_format_sector(void) {
    int drive = flp_io_drv;
    uint8_t c = flp_buffer.data[0];
    uint8_t h = flp_buffer.data[1];
    uint8_t s = flp_buffer.data[2];
    uint8_t bs = flp_buffer.data[3];
    
    if (c!=flpdrv[drive].cyl || h!=flpdrv[drive].head || s!=flpdrv[drive].sector || bs!=flpdrv[drive].blocksize) {
        Log_Printf(LOG_WARN, "[Floppy] Format error. Bad sector data. Stopping.");
        flp_sector_counter=0; /* stop the transfer */
        flp_io_state = FLP_STATE_INTERRUPT;
        send_rw_status(drive);
        return;
    }
    
    /* Erase data */
    uint32_t sec_size = 0x80<<flpdrv[drive].blocksize;
    uint32_t logical_sec = get_logical_sec(drive);
    flp_buffer.size = sec_size;
    memset(flp_buffer.data, 0, flp_buffer.size);

    if (flp.st[0]&IC_ABNORMAL) {
        Log_Printf(LOG_WARN, "[Floppy] Format error. Bad sector offset (%i).",logical_sec);
        flp_buffer.size = flp_buffer.limit = 0;
        flp_sector_counter = 0;
        send_rw_status(drive);
    } else {
        Log_Printf(LOG_FLP_CMD_LEVEL, "[Floppy] Format sector at offset %i",logical_sec);
        File_Write(flp_buffer.data, flp_buffer.size, logical_sec*sec_size, flpdrv[drive].dsk);
        flp_buffer.size = 0;
        flp_buffer.limit = 4;
        flpdrv[drive].sector++;
        flp_sector_counter--;
    }
    
    if (flp_sector_counter==0) {
        send_rw_status(drive);
    }
}


void Floppy_IO_Handler(void) {
    uint32_t old_size;
    
    switch (flp_io_state) {
        case FLP_STATE_WRITE:
            if (flp_buffer.size==flp_buffer.limit) {
                floppy_write_sector();
                if (flp_sector_counter==0) { /* done */
                    floppy_interrupt();
                    flp_io_state = FLP_STATE_DONE;
                    return;
                }
            } else if (flp_buffer.size<flp_buffer.limit) { /* loop in filling mode */
                old_size = flp_buffer.size;
                dma_esp_read_memory();
                if (flp_buffer.size==old_size) {
                    floppy_rw_nodata();
                    floppy_interrupt();
                    flp_io_state = FLP_STATE_DONE;
                    return;
                }
            }
            break;
            
        case FLP_STATE_READ:
            if (flp_buffer.size==0 && flp_sector_counter>0) {
                floppy_read_sector();
            }
            if (flp_buffer.size>0) { /* loop in draining mode */
                old_size = flp_buffer.size;
                dma_esp_write_memory();
                if (flp_buffer.size==old_size) {
                    floppy_rw_nodata();
                    floppy_interrupt();
                    flp_io_state = FLP_STATE_DONE;
                    return;
                }
            }
            if (flp_buffer.size==0 && flp_sector_counter==0) { /* done */
                floppy_interrupt();
                flp_io_state = FLP_STATE_DONE;
                return;
            }
            break;
            
        case FLP_STATE_FORMAT:
            if (flp_buffer.size==flp_buffer.limit) {
                floppy_format_sector();
                if (flp_sector_counter==0) { /* done */
                    floppy_interrupt();
                    flp_io_state = FLP_STATE_DONE;
                    return;
                }
            } else if (flp_buffer.size<flp_buffer.limit) { /* loop in filling mode */
                old_size = flp_buffer.size;
                dma_esp_read_memory();
                if (flp_buffer.size==old_size) {
                    floppy_rw_nodata();
                    floppy_interrupt();
                    flp_io_state = FLP_STATE_DONE;
                    return;
                }
            }
            break;
            
        case FLP_STATE_INTERRUPT:
            floppy_interrupt();
            flp_io_state = FLP_STATE_DONE;
            /* fall through */
            
        case FLP_STATE_DONE:
            return;
            
        default:
            return;
    }
    
    CycInt_UpdateTimeEvent(get_sector_time(flp_io_drv), 250, EVENT_FLOPPY_IO);
}


/* Initialize/Uninitialize floppy disks */

static void Floppy_Init(void) {
    Log_Printf(LOG_WARN, "Loading floppy disks:");
    int i;
    
    for (i = 0; i < FLP_MAX_DRIVES; i++) {
        flpdrv[i].spinning = false;
        /* Check if files exist. */
        if (ConfigureParams.Floppy.drive[i].bDriveConnected) {
            flpdrv[i].connected = true;
            if (ConfigureParams.Floppy.drive[i].bDiskInserted) {
                Floppy_Insert(i);
            } else {
                flpdrv[i].dsk = File_Close(flpdrv[i].dsk);
                flpdrv[i].inserted = false;
            }
        } else {
            flpdrv[i].connected = false;
        }
    }
    
    floppy_reset();
}

static void Floppy_Uninit(void) {
    int i;
    
    for (i = 0; i < FLP_MAX_DRIVES; i++) {
        flpdrv[i].dsk = File_Close(flpdrv[i].dsk);
        flpdrv[i].inserted = false;
    }
}

static uint32_t Floppy_CheckSize(int drive) {
    off_t size = File_Length(ConfigureParams.Floppy.drive[drive].szImageName);
    
    switch (size) {
        case SIZE_720K:
        case SIZE_1440K:
        case SIZE_2880K:
            return (uint32_t)size;
            
        default:
            Log_Printf(LOG_WARN, "Floppy disk %i: Invalid size (%lld byte)\n",drive,(long long)size);
            return 0;
    }
}

int Floppy_Insert(int drive) {
    flpdrv[drive].floppysize = Floppy_CheckSize(drive);
    
    Log_Printf(LOG_WARN, "Floppy disk %i: Insert %s, %iK", drive,
               ConfigureParams.Floppy.drive[drive].szImageName, flpdrv[drive].floppysize/1024);
    
    if (!ConfigureParams.Floppy.drive[drive].bWriteProtected) {
        flpdrv[drive].dsk = File_Open(ConfigureParams.Floppy.drive[drive].szImageName, "rb+");
        flpdrv[drive].protected = false;
    }
    if (ConfigureParams.Floppy.drive[drive].bWriteProtected || flpdrv[drive].dsk == NULL) {
        flpdrv[drive].dsk = File_Open(ConfigureParams.Floppy.drive[drive].szImageName, "rb");
        flpdrv[drive].protected = true;
    }
    if (flpdrv[drive].dsk == NULL || flpdrv[drive].floppysize == 0) {
        Log_Printf(LOG_WARN, "Floppy disk %i: Cannot open image file %s", drive,
                   ConfigureParams.Floppy.drive[drive].szImageName);
        flpdrv[drive].inserted = false;
        flpdrv[drive].protected = false;
        Statusbar_AddMessage("Cannot insert floppy disk", 0);
        return 1;
    }
    
    flpdrv[drive].inserted = true;
    flpdrv[drive].spinning = false;
    flpdrv[drive].blocksize = 2; /* 512 byte */
    flpdrv[drive].cyl = flpdrv[drive].head = flpdrv[drive].sector = 0;
    flpdrv[drive].seekoffset = 0;
    Statusbar_AddMessage("Inserting floppy disk", 0);
    return 0;
}

void Floppy_Eject(int drive) {
    if (drive < 0) { /* Called from emulator, else called from GUI */
        drive = flp.sel;
        
        Statusbar_AddMessage("Ejecting floppy disk", 0);
    }
    
    Log_Printf(LOG_WARN, "Floppy disk %i: Eject", drive);
    
    flpdrv[drive].dsk = File_Close(flpdrv[drive].dsk);
    flpdrv[drive].floppysize = 0;
    flpdrv[drive].blocksize = 0;
    flpdrv[drive].inserted = false;
    flpdrv[drive].spinning = false;
    
    ConfigureParams.Floppy.drive[drive].bDiskInserted = false;
    ConfigureParams.Floppy.drive[drive].szImageName[0] = '\0';
}

void Floppy_Reset(void) {
    Floppy_Uninit();
    Floppy_Init();
}

void set_floppy_select(uint8_t sel, bool osp) {
    if (sel) {
        Log_Printf(LOG_DEBUG,"[%s] Selecting floppy controller",osp?"OSP":"Floppy");
        floppy_select = 1;
    } else {
        Log_Printf(LOG_FLP_REG_LEVEL,"[%s] Selecting SCSI controller",osp?"OSP":"Floppy");
        floppy_select = 0;
    }
}

static bool floppy_controller_present(int read) {
    if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
        int i;
        for (i = 0; i < FLP_MAX_DRIVES; i++) {
            if (ConfigureParams.Floppy.drive[i].bDriveConnected) {
                return true;
            }
        }
        Log_Printf(LOG_WARN,"[Floppy] Controller not present. Bus error at %08x.", IoAccessCurrentAddress);
        M68000_BusError(IoAccessCurrentAddress, read, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
        return false;
    }
    return true;
}

void FLP_StatA_Read(void) { /* 0x02014100 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, floppy_sra_read());
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Status A read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_StatB_Read(void) { /* 0x02014101 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, flp.srb|SRB_ALL1);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Status B read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_DataOut_Read(void) { /* 0x02014102 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, floppy_dor_read());
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Data out read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_DataOut_Write(void) {
    if (floppy_controller_present(BUS_ERROR_WRITE)) {
        floppy_dor_write(IoMem_ReadByte(IoAccessCurrentAddress));
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Data out write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_MainStatus_Read(void) { /* 0x02014104 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, flp.msr);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Main status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_DataRate_Write(void) {
    if (floppy_controller_present(BUS_ERROR_WRITE)) {
        flp.dsr = IoMem_ReadByte(IoAccessCurrentAddress);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Data rate write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
        
        if (flp.dsr&DSR_RESET) {
            Log_Printf(LOG_WARN,"[Floppy] Entering and leaving reset state.");
            floppy_stop();
            floppy_start();
            flp.dsr&=~DSR_RESET;
        }
    }
}

void FLP_FIFO_Read(void) { /* 0x02014105 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, floppy_fifo_read());
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] FIFO read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_FIFO_Write(void) {
    if (floppy_controller_present(BUS_ERROR_WRITE)) {
        uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] FIFO write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
        
        floppy_fifo_write(val);
    }
}

void FLP_DataIn_Read(void) { /* 0x02014107 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        IoMem_WriteByte(IoAccessCurrentAddress, flp.din);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Data in read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_Configuration_Write(void) {
    if (floppy_controller_present(BUS_ERROR_WRITE)) {
        flp.ccr = IoMem_ReadByte(IoAccessCurrentAddress);
        Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Configuration write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_Reserved_Read(void) { /* 0x02014103 and 0x02014106 */
    if (floppy_controller_present(BUS_ERROR_READ)) {
        uint8_t val = 0;
        switch (IoAccessCurrentAddress & 7) {
            case 3:
                if (ConfigureParams.System.bTurbo) {
                    if (ConfigureParams.System.nMachineType == NEXT_STATION) {
                        val = ConfigureParams.System.bColor ? 0x03 : 0x02;
                    }
                } else {
                    val = 0x03;
                }
                break;
            case 6:
                if (ConfigureParams.System.bTurbo) {
                    if (ConfigureParams.System.nMachineType == NEXT_STATION) {
                        val = 0xc0;
                    } else {
                        val = 0xf0;
                    }
                } else {
                    val = 0x41;
                }
                break;
            default:
                break;
        }
        IoMem_WriteByte(IoAccessCurrentAddress, val);
        Log_Printf(LOG_WARN,"[Floppy] Reserved read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_Reserved_Write(void) {
    if (floppy_controller_present(BUS_ERROR_WRITE)) {
        Log_Printf(LOG_WARN,"[Floppy] Reserved write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    }
}

void FLP_Status_Read(void) { /* 0x02014108 */
    IoMem_WriteByte(IoAccessCurrentAddress, floppy_stat_read());
    Log_Printf(LOG_FLP_REG_LEVEL,"[Floppy] Control read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void FLP_Control_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_DEBUG,"[Floppy] Select write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    floppy_ctrl_write(val);
}
