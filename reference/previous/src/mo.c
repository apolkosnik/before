/*
  Previous - mo.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
 
  This file contains a simulation of the Fujitsu MB600310 Optical Storage 
  Processor (OSP) and Canon magneto-optical disk drive.
 
  The OSP uses the Reed-Solomon algorithm for error correction. It has two 
  internal buffers of 1296 byte each and uses double-buffering to perform
  error correction.
*/
const char Mo_fileid[] = "Previous mo.c";

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "configuration.h"
#include "mo.h"
#include "sysReg.h"
#include "dma.h"
#include "floppy.h"
#include "file.h"
#include "rs.h"
#include "statusbar.h"

#define LOG_MO_REG_LEVEL    LOG_DEBUG
#define LOG_MO_CMD_LEVEL    LOG_DEBUG
#define LOG_MO_ECC_LEVEL    LOG_DEBUG
#define LOG_MO_IO_LEVEL     LOG_DEBUG


OpticalDiskBuffer ecc_buffer[2];

/* OSP registers */
struct {
    uint8_t tracknuml;
    uint8_t tracknumh;
    uint8_t sector_num;
    int     sector_count;
    uint8_t intstatus;
    uint8_t intmask;
    uint8_t ctrlr_csr2;
    uint8_t ctrlr_csr1;
    uint8_t csrl;
    uint8_t csrh;
    uint8_t err_stat;
    uint8_t ecc_cnt;
    uint8_t init;
    uint8_t format;
    uint8_t mark;
    uint8_t flag[7];
} osp;

/* MO drives */
struct {
    uint16_t status;
    uint16_t dstat;
    uint16_t estat;
    uint16_t hstat;
    
    uint8_t  head;
    
    uint32_t head_pos;
    uint32_t ho_head_pos;
    uint32_t sec_offset;
    
    FILE* dsk;
    
    bool spinning;
    bool spiraling;
    bool seeking;
    
    bool attn;
    bool complete;
    
    bool protected;
    bool inserted;
    bool enabled;
    bool connected;
} mo[MO_MAX_DRIVES];

static int dnum;


#define NO_HEAD     0
#define READ_HEAD   1
#define WRITE_HEAD  2
#define ERASE_HEAD  3
#define VERIFY_HEAD 4
#define RF_HEAD     5


/* Sector increment and number */
#define MOSEC_NUM_MASK      0x0F /* rw */
#define MOSEC_INCR_MASK     0xF0 /* wo */

/* Interrupt status */
#define MOINT_CMD_COMPL     0x01 /* ro */
#define MOINT_ATTN          0x02 /* ro */
#define MOINT_OPER_COMPL    0x04 /* rw */
#define MOINT_ECC_DONE      0x08 /* rw */
#define MOINT_TIMEOUT       0x10 /* rw */
#define MOINT_READ_FAULT    0x20 /* rw */
#define MOINT_PARITY_ERR    0x40 /* rw */
#define MOINT_DATA_ERR      0x80 /* rw */
#define MOINT_RESET         0x01 /* wo */
#define MOINT_GPO           0x02 /* wo */

#define MOINT_OSP_MASK      0xFC
#define MOINT_MO_MASK       0x03

/* Controller CSR 2 */
#define MOCSR2_DRIVE_SEL    0x01
#define MOCSR2_ECC_CMP      0x02
#define MOCSR2_BUF_TOGGLE   0x04
#define MOCSR2_CLR_BUFP     0x08
#define MOCSR2_ECC_BLOCKS   0x10
#define MOCSR2_ECC_MODE     0x20
#define MOCSR2_ECC_DIS      0x40
#define MOCSR2_SECT_TIMER   0x80

/* Controller CSR 1 */
/* see below (formatter commands) */

/* Drive CSR (lo and hi) */
/* see below (drive commands) */

/* Data error status */
#define ERRSTAT_ECC         0x01
#define ERRSTAT_CMP         0x02
#define ERRSTAT_TIMING      0x04
#define ERRSTAT_STARVE      0x08

/* Init */
#define MOINIT_ID_MASK      0x03
#define MOINIT_EVEN_PAR     0x04
#define MOINIT_DMA_STV_ENA  0x08
#define MOINIT_25_MHZ       0x10
#define MOINIT_ID_CMP_TRK   0x20
#define MOINIT_ECC_STV_DIS  0x40
#define MOINIT_SEC_GREATER  0x80

#define MOINIT_ID_34    0
#define MOINIT_ID_234   1
#define MOINIT_ID_1234  3
#define MOINIT_ID_0     2

/* Format */
#define MOFORM_RD_GATE_NOM  0x06
#define MOFORM_WR_GATE_NOM  0x30

#define MOFORM_RD_GATE_MIN  0x00
#define MOFORM_RD_GATE_MAX  0x0F
#define MOFORM_RD_GATE_MASK 0x0F


/* Disk layout */
#define MO_SEC_PER_TRACK    16
#define MO_TRACK_OFFSET     4096 /* offset to first logical sector of kernel driver is 4149 */
#define MO_TRACK_LIMIT      (19819-(MO_TRACK_OFFSET)) /* no more tracks beyond this offset */

#define MO_SECTORSIZE_DISK  1296 /* size of encoded sector, like stored on disk */
#define MO_SECTORSIZE_DATA  1024 /* size of decoded sector, like handled by software */


static uint32_t get_logical_sector(uint32_t sector_id) {
    int32_t tracknum = (sector_id&0xFFFF00)>>8;
    uint8_t sectornum = sector_id&0x0F;

    tracknum-=MO_TRACK_OFFSET;
    if (tracknum<0 || tracknum>=MO_TRACK_LIMIT) {
        Log_Printf(LOG_WARN, "MO disk %i: Error! Bad sector (%i)! Disk limit exceeded.", dnum,
                   (tracknum*MO_SEC_PER_TRACK)+osp.sector_num);
        abort();
    }

    return (tracknum*MO_SEC_PER_TRACK)+sectornum;
}


/* Timing constants */
#define SEEK_TIMING 1

#define SECTOR_IO_DELAY 1250
#define CMD_DELAY       40


/* Functions */
static void mo_start(void);
static void mo_stop(void);

static void mo_set_signals(bool complete, bool attn, int drive);
static void mo_set_signals_delayed(bool complete, bool attn, int delay);
static void mo_connect_signals(void);

static void mo_read_sector(uint32_t sector_id);
static void mo_write_sector(uint32_t sector_id);
static void mo_erase_sector(uint32_t sector_id);
static void mo_verify_sector(uint32_t sector_id);

static void mo_drive_cmd(uint16_t command);
static void mo_seek(uint16_t command);
static void mo_high_order_seek(uint16_t command);
static void mo_jump_head(uint16_t command);
static void mo_recalibrate(void);
static void mo_return_drive_status(void);
static void mo_return_track_addr(void);
static void mo_return_extended_status(void);
static void mo_return_hardware_status(void);
static void mo_return_version(void);
static void mo_select_head(int head);
static void mo_reset_attn_status(void);
static void mo_stop_spinning(void);
static void mo_start_spinning(void);
static void mo_eject_disk(int drive);
static void mo_start_spiraling(void);
static void mo_stop_spiraling(void);
static void mo_self_diagnostic(void);
static void mo_unimplemented_cmd(void);

static bool mo_empty(void);
static bool mo_protected(void);
static bool mo_stopped(void);
static void mo_spiraling_operation(void);
static void mo_insert_disk(int drive);

static void osp_formatter_cmd(void);
static void osp_formatter_cmd2(void);
static void osp_select(int drive);
static void osp_set_interrupts(void);
static void osp_interrupt(uint8_t interrupt);

static void ecc_read(void);
static void ecc_write(void);
static void ecc_verify(void);
static void ecc_toggle_buffer(void);
static void ecc_clear_buffer(void);
static void ecc_decode(void);
static void ecc_encode(void);
static void ecc_sequence_done(void);

static void fmt_sector_done(void);
static bool fmt_match_id(uint32_t sector_id);
static void fmt_io(uint32_t sector_id);

static int sector_increment = 0;


/* ------------------------ OPTICAL STORAGE PROCESSOR ------------------------ */

/* OSP registers */

void MO_TrackNumH_Read(void) { /* 0x02012000 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.tracknumh);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Track number hi read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_TrackNumH_Write(void) {
    osp.tracknumh = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Track number hi write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_TrackNumL_Read(void) { /* 0x02012001 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.tracknuml);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Track number lo read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_TrackNumL_Write(void) {
    osp.tracknuml = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Track number lo write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_SectorIncr_Read(void) { /* 0x02012002 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.sector_num&MOSEC_NUM_MASK);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Sector increment and number read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_SectorIncr_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    osp.sector_num = val&MOSEC_NUM_MASK;
    sector_increment = (val&MOSEC_INCR_MASK)>>4;
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Sector increment and number write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_SectorCnt_Read(void) { /* 0x02012003 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.sector_count&0xFF);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Sector count read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_SectorCnt_Write(void) {
    osp.sector_count = IoMem_ReadByte(IoAccessCurrentAddress);
    if (osp.sector_count==0) {
        osp.sector_count=0x100;
    }
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Sector count write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_IntStatus_Read(void) { /* 0x02012004 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.intstatus);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Interrupt status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_IntStatus_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    osp.intstatus &= ~(val&MOINT_OSP_MASK);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Interrupt status write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());

    if (val&MOINT_RESET) {
        mo_stop();
    } else {
        mo_start();
    }
    osp_set_interrupts();

    if (ConfigureParams.System.nMachineType==NEXT_CUBE030) {
        set_floppy_select(val&MOINT_GPO, true);
    }
}

void MO_IntMask_Read(void) { /* 0x02012005 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.intmask);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Interrupt mask read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_IntMask_Write(void) {
    osp.intmask = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Interrupt mask write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());

    osp_set_interrupts();
}

void MOctrl_CSR2_Read(void) { /* 0x02012006 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.ctrlr_csr2);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR2 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MOctrl_CSR2_Write(void) {
    osp.ctrlr_csr2 = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    osp_formatter_cmd2();
}

void MOctrl_CSR1_Read(void) { /* 0x02012007 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.ctrlr_csr1);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR1 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MOctrl_CSR1_Write(void) {
    osp.ctrlr_csr1 = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR1 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    osp_formatter_cmd();
}

void MO_CSR_H_Read(void) { /* 0x02012009 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.csrh);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR hi read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_CSR_H_Write(void) {
    osp.csrh = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR hi write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_CSR_L_Read(void) { /* 0x02012008 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.csrl);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR lo read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_CSR_L_Write(void) {
    osp.csrl = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] CSR lo write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    mo_drive_cmd((osp.csrh<<8) | osp.csrl);
}

void MO_ErrStat_Read(void) { /* 0x0201200a */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.err_stat);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Error status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_EccCnt_Read(void) { /* 0x0201200b */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.ecc_cnt);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] ECC count read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Init_Write(void) { /* 0x0201200c */
    osp.init = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Init write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Format_Write(void) { /* 0x0201200d */
    osp.format = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Format write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Mark_Write(void) { /* 0x0201200e */
    osp.mark = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Mark write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag0_Read(void) { /* 0x02012010 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[0]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 0 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag0_Write(void) {
    osp.flag[0] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 0 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag1_Read(void) { /* 0x02012011 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[1]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 1 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag1_Write(void) {
    osp.flag[1] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 1 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag2_Read(void) { /* 0x02012012 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[2]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 2 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag2_Write(void) {
    osp.flag[2] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag3_Read(void) { /* 0x02012013 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[3]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 3 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag3_Write(void) {
    osp.flag[3] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 3 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag4_Read(void) { /* 0x02012014 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[4]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 4 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag4_Write(void) {
    osp.flag[4] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 4 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag5_Read(void) { /* 0x02012015 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[5]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 5 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag5_Write(void) {
    osp.flag[5] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 5 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag6_Read(void) { /* 0x02012016 */
    IoMem_WriteByte(IoAccessCurrentAddress, osp.flag[6]);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 6 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void MO_Flag6_Write(void) {
    osp.flag[6] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_MO_REG_LEVEL,"[OSP] Flag 6 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

#if 0
/* Register debugging */
static void print_regs(void) {
    int i;
    Log_Printf(LOG_WARN,"sector ID:  %02X%02X%02X",osp.tracknumh,osp.tracknuml,osp.sector_num);
    Log_Printf(LOG_WARN,"head pos:   %04X",mo[dnum].head_pos);
    Log_Printf(LOG_WARN,"sector cnt: %02X",osp.sector_count&0xFF);
    Log_Printf(LOG_WARN,"intstatus:  %02X",osp.intstatus);
    Log_Printf(LOG_WARN,"intmask:    %02X",osp.intmask);
    Log_Printf(LOG_WARN,"ctrlr csr2: %02X",osp.ctrlr_csr2);
    Log_Printf(LOG_WARN,"ctrlr csr1: %02X",osp.ctrlr_csr1);
    Log_Printf(LOG_WARN,"drive csrl: %02X",osp.csrl);
    Log_Printf(LOG_WARN,"drive csrh: %02X",osp.csrh);
    Log_Printf(LOG_WARN,"errstat:    %02X",osp.err_stat);
    Log_Printf(LOG_WARN,"ecc count:  %02X",osp.ecc_cnt);
    Log_Printf(LOG_WARN,"init:       %02X",osp.init);
    Log_Printf(LOG_WARN,"format:     %02X",osp.format);
    Log_Printf(LOG_WARN,"mark:       %02X",osp.mark);
    for (i=0; i<7; i++) {
        Log_Printf(LOG_WARN,"flag %i:     %02X",i+1,osp.flag[i]);
    }
}
#endif

void osp_interrupt(uint8_t interrupt) {
    osp.intstatus|=interrupt;
    osp_set_interrupts();
}

void osp_set_interrupts(void) {
    if (osp.intstatus&osp.intmask) {
        set_interrupt(INT_DISK, SET_INT);
    } else {
        set_interrupt(INT_DISK, RELEASE_INT);
    }
}


enum {
    ECC_MODE_READ,
    ECC_MODE_WRITE,
    ECC_MODE_VERIFY
} ecc_mode;

enum {
    ECC_STATE_FILLING,
    ECC_STATE_DRAINING,
    ECC_STATE_ECCING,
    ECC_STATE_WAITING,
    ECC_STATE_DONE
} ecc_state;

/* Formatter commands */

#define FMT_RESET       0x00
#define FMT_ECC_READ    0x80
#define FMT_ECC_WRITE   0x40
#define FMT_RD_STAT     0x20
#define FMT_ID_READ     0x10
#define FMT_VERIFY      0x08
#define FMT_ERASE       0x04
#define FMT_READ        0x02
#define FMT_WRITE       0x01

enum {
    FMT_MODE_READ,
    FMT_MODE_WRITE,
    FMT_MODE_ERASE,
    FMT_MODE_VERIFY,
    FMT_MODE_READ_ID,
    FMT_MODE_IDLE
} fmt_mode;

bool write_timing;

void osp_formatter_cmd(void) {
    
    if (osp.ctrlr_csr1==FMT_RESET) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Reset (%02X)\n", osp.ctrlr_csr1);
        if (fmt_mode!=FMT_MODE_IDLE) {
            Log_Printf(LOG_WARN,"[OSP] Warning: Formatter reset while busy!\n");
        }
        fmt_mode = FMT_MODE_IDLE;
        ecc_state = ECC_STATE_DONE;
        osp.ecc_cnt=0;
        osp.err_stat=0;
        return;
    }
    if (osp.ctrlr_csr1&FMT_ECC_READ) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: ECC Read (%02X)\n", osp.ctrlr_csr1);
        ecc_read();
    }
    if (osp.ctrlr_csr1&FMT_ECC_WRITE) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: ECC Write (%02X)\n", osp.ctrlr_csr1);
        ecc_write();
    }
    if (osp.ctrlr_csr1&FMT_RD_STAT) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Read Status (%02X)\n", osp.ctrlr_csr1);
        osp.csrh = (mo[dnum].status>>8)&0xFF;
        osp.csrl = mo[dnum].status&0xFF;
    }
    if (osp.ctrlr_csr1&FMT_ID_READ) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: ID Read (%02X)\n", osp.ctrlr_csr1);
        fmt_mode = FMT_MODE_READ_ID;
    }
    if (osp.ctrlr_csr1&FMT_VERIFY) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Verify (%02X)\n", osp.ctrlr_csr1);
        fmt_mode = FMT_MODE_VERIFY;
    }
    if (osp.ctrlr_csr1&FMT_ERASE) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Erase (%02X)\n", osp.ctrlr_csr1);
        fmt_mode = FMT_MODE_ERASE;
    }
    if (osp.ctrlr_csr1&FMT_READ) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Read (%02X)\n", osp.ctrlr_csr1);
        fmt_mode = FMT_MODE_READ;
    }
    if (osp.ctrlr_csr1&FMT_WRITE) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[OSP] Formatter command: Write (%02X)\n", osp.ctrlr_csr1);
        write_timing = false;
        fmt_mode = FMT_MODE_WRITE;
    }
}

void fmt_sector_done(void) {
    uint16_t track = (osp.tracknumh<<8)|osp.tracknuml;
    osp.sector_num+=sector_increment;
    track+=osp.sector_num/MO_SEC_PER_TRACK;
    osp.sector_num%=MO_SEC_PER_TRACK;
    osp.tracknumh = (track>>8)&0xFF;
    osp.tracknuml = track&0xFF;
    osp.sector_count--;
    /* Check if the operation is complete */
    if (osp.sector_count==0) {
        fmt_mode = FMT_MODE_IDLE;
        osp_interrupt(MOINT_OPER_COMPL);
    }
}

int sector_timer=0;
#define SECTOR_TIMEOUT_COUNT 32
bool fmt_match_id(uint32_t sector_id) {
    uint32_t fmt_id = (osp.tracknumh<<16)|(osp.tracknuml<<8)|osp.sector_num;
    
    if ((osp.init&MOINIT_ID_MASK)==MOINIT_ID_0) {
        Log_Printf(LOG_WARN, "[OSP] Sector ID matching disabled!");
        abort(); /* CHECK: this routine is critical to disk image corruption, check if it gives correct results */
        return true;
    }
    if (osp.init&MOINIT_ID_CMP_TRK) {
        Log_Printf(LOG_WARN, "[OSP] Compare only track ID.");
        fmt_id&=0xFFFF00;
        sector_id&=0xFFFF00;
        abort(); /* CHECK: this could cause disk image corruption */
    }
    
    if (sector_id==fmt_id) {
        sector_timer=0;
        return true;
    } else {
        Log_Printf(LOG_MO_CMD_LEVEL, "[OSP] Sector ID mismatch (Sector ID=%06X, Looking for %06X)",
                   sector_id,fmt_id);
        if (osp.ctrlr_csr2&MOCSR2_SECT_TIMER) {
            sector_timer++;
            if (sector_timer>SECTOR_TIMEOUT_COUNT || sector_id>fmt_id) {
                Log_Printf(LOG_WARN, "[OSP] Sector timeout!");
                sector_timer=0;
                fmt_mode=FMT_MODE_IDLE;
                osp_interrupt(MOINT_TIMEOUT);
            }
        }
        return false;
    }
}

void fmt_io(uint32_t sector_id) {

    switch (fmt_mode) {
        case FMT_MODE_IDLE:
            return;
        case FMT_MODE_READ_ID:
            osp.tracknumh = (sector_id>>16)&0xFF;
            osp.tracknuml = (sector_id>>8)&0xFF;
            osp.sector_num = sector_id&0x0F;
            osp_interrupt(MOINT_OPER_COMPL);
            break;
        case FMT_MODE_READ:
            if (mo[dnum].head!=READ_HEAD) {
                abort();
            }
            if (fmt_match_id(sector_id)) {
                /* First read sector from disk to ECC buffer */
                mo_read_sector(sector_id);
                /* Then decode data and write to memory using DMA */
                ecc_read();
                fmt_sector_done();
            }
            break;
        case FMT_MODE_WRITE:
            if (mo[dnum].head!=WRITE_HEAD) {
                abort();
            }
            /* WARNING: first sector must be mismatch to pre-fill the ECC buffer for writing */
            if (fmt_match_id(sector_id) && write_timing) {
                /* Write sector from ECC buffer to disk */
                mo_write_sector(sector_id);
                fmt_sector_done();
            } else {
                write_timing = true;
            }
            /* (Re)fill ECC buffer from memory using DMA and encode data */
            ecc_write();
            break;
        case FMT_MODE_ERASE:
            if (mo[dnum].head!=ERASE_HEAD) {
                abort();
            }
            if (fmt_match_id(sector_id)) {
                mo_erase_sector(sector_id);
                fmt_sector_done();
            }
            break;
        case FMT_MODE_VERIFY:
            if (mo[dnum].head!=VERIFY_HEAD) {
                abort();
            }
            if (fmt_match_id(sector_id)) {
                /* First read sector from disk to ECC buffer */
                mo_verify_sector(sector_id);
                /* Then verify data */
                ecc_verify();
                fmt_sector_done();
            }
            break;
            
        default:
            abort();
            break;
    }
}


/* Drive selection and formatter command 2 (ECC) */
void osp_select(int drive) {
    Log_Printf(LOG_MO_CMD_LEVEL, "[OSP] Selecting drive %i",drive);
    if (!mo[drive].connected) {
        Log_Printf(LOG_WARN, "[OSP] Selected drive %i not connected.",drive);
    }
    if (drive!=dnum && mo[drive].attn) {
        Log_Printf(LOG_WARN, "[OSP] Releasing attention during selection (drive %i).",drive);
        mo[drive].attn = false; /* FIXME: this is required for stability but not very realistic */
    }
    dnum=drive;
    mo_connect_signals();
}

void osp_formatter_cmd2(void) {
    if (osp.ctrlr_csr2&MOCSR2_BUF_TOGGLE) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] Toggle ECC buffer.");
        ecc_toggle_buffer();
    }
    if (osp.ctrlr_csr2&MOCSR2_ECC_CMP) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC compare.");
    }
    if (osp.ctrlr_csr2&MOCSR2_CLR_BUFP) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] Clear ECC buffer.");
        ecc_clear_buffer();
    }
    if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC blocks.");
    }
    if (osp.ctrlr_csr2&MOCSR2_ECC_MODE) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC decoding mode.");
    } else {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC encoding mode.");
    }
    if (osp.ctrlr_csr2&MOCSR2_SECT_TIMER) {
        Log_Printf(LOG_MO_CMD_LEVEL, "[OSP] Sector timer enabled.");
    } else {
        Log_Printf(LOG_MO_CMD_LEVEL, "[OSP] Sector timer disabled.");
    }
    if (osp.ctrlr_csr2&MOCSR2_ECC_DIS) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] Disable ECC passthrough.");
    }
    
    osp_select(osp.ctrlr_csr2&MOCSR2_DRIVE_SEL);
}


/* ECC emulation:
 *
 * read     mem <----- buf <-de-- disk
 * write    mem -----> buf --en-> disk
 * verify              buf <-de-- disk
 *
 * ecc_dis
 * read     mem <-en-- buf
 * write    mem -----> buf
 *
 * ecc_dis|ecc_mode
 * read     mem <----- buf
 * write    mem --de-> buf
 *
 */

#define ECC_DELAY SECTOR_IO_DELAY/5 /* must be a fraction of sector delay */

bool ecc_repeat=false; /* This is for ECC blocks */

int eccin=0;
int eccout=1;

void ecc_toggle_buffer(void) {
    if (eccin==0) {
        eccout=0;
        eccin=1;
    } else {
        eccout=1;
        eccin=0;
    }
    Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC switching buffer (in: %i, out: %i)",eccin,eccout);
}
void ecc_clear_buffer(void) {
    ecc_buffer[eccin].size=ecc_buffer[eccout].size=0;
    ecc_buffer[eccin].limit=ecc_buffer[eccout].limit=MO_SECTORSIZE_DATA;
}

void ecc_decode(void) {
    if (osp.ctrlr_csr2&MOCSR2_ECC_DIS && !(osp.ctrlr_csr2&MOCSR2_ECC_MODE)) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC decoding disabled.");
        if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS && ecc_repeat==true) {
            ecc_toggle_buffer();
        }
    } else if (ecc_buffer[eccin].size==MO_SECTORSIZE_DISK) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC decoding buffer.");
        ecc_buffer[eccin].limit=ecc_buffer[eccin].size=MO_SECTORSIZE_DATA;
        
        int num_errors = rs_decode(ecc_buffer[eccin].data);
        
        if (num_errors<0) {
            Log_Printf(LOG_WARN, "[OSP] ECC: Sector has uncorrectable errors!");
            osp.err_stat = ERRSTAT_ECC;
            osp_interrupt(MOINT_DATA_ERR);
            /* CHECK: Stop ECC and formatter? */
        } else {
            Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC: Number of corrected errors: %i\n",num_errors);
            
            if (osp.ecc_cnt==0) {
                osp.ecc_cnt=num_errors;
            }
        }
        
        ecc_toggle_buffer();
    } else {
        Log_Printf(LOG_WARN, "[OSP] ECC buffer is not ready (%i bytes)!",ecc_buffer[eccin].size);
        abort();
    }
}
void ecc_encode(void) {
    if (osp.ctrlr_csr2&MOCSR2_ECC_DIS && osp.ctrlr_csr2&MOCSR2_ECC_MODE) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC encoding disabled.");
        if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS && ecc_repeat==false) {
            ecc_toggle_buffer();
        }
    } else if (ecc_buffer[eccin].limit==MO_SECTORSIZE_DATA) {
        Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC encoding buffer.");
        ecc_buffer[eccin].limit=ecc_buffer[eccin].size=MO_SECTORSIZE_DISK;

        rs_encode(ecc_buffer[eccin].data);

        ecc_toggle_buffer();
    } else {
        Log_Printf(LOG_WARN, "[OSP] ECC buffer is not ready (%i bytes)!",ecc_buffer[eccin].size);
        abort();
    }
}

void ecc_write(void) {
    if (ecc_state!=ECC_STATE_DONE) {
        Log_Printf(LOG_MO_ECC_LEVEL,"[OSP] Warning: ECC not accepting command (busy %i)", ecc_state);
        return;
    }
    ecc_mode=ECC_MODE_WRITE;
    ecc_state=ECC_STATE_FILLING;
    if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS) {
        ecc_repeat=true;
    }
    ecc_buffer[eccin].size=0; /* FIXME: find a better place for this */
    ecc_buffer[eccin].limit=MO_SECTORSIZE_DATA; /* and this */
    CycInt_AddCycleTimeEvent(ECC_DELAY, 80, EVENT_MO_ECC_IO);
}
void ecc_read(void) {
    if (ecc_state!=ECC_STATE_DONE) {
        Log_Printf(LOG_WARN,"[OSP] Warning: ECC not accepting command (busy %i)", ecc_state);
        return;
    }
    ecc_mode=ECC_MODE_READ;
    ecc_state=ECC_STATE_ECCING;
    if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS) {
        ecc_repeat=true;
    }
    CycInt_AddCycleTimeEvent(ECC_DELAY, 80, EVENT_MO_ECC_IO);
}
void ecc_verify(void) {
    if (ecc_state!=ECC_STATE_DONE) {
        Log_Printf(LOG_WARN,"[OSP] Warning: ECC not accepting command (busy %i)", ecc_state);
        return;
    }
    ecc_mode=ECC_MODE_VERIFY;
    ecc_state=ECC_STATE_ECCING;
    CycInt_AddCycleTimeEvent(ECC_DELAY, 80, EVENT_MO_ECC_IO);
}
void ecc_sequence_done(void) {
    if (ecc_repeat==true) {
        ecc_repeat=false;
        if (ecc_mode==ECC_MODE_WRITE) {
            ecc_buffer[eccin].size=0; /* FIXME: find a better place for this */
            ecc_state=ECC_STATE_FILLING;
        } else {
            ecc_state=ECC_STATE_ECCING;
        }
        CycInt_AddCycleTimeEvent(ECC_DELAY, 80, EVENT_MO_ECC_IO);
        return;
    }

    ecc_state=ECC_STATE_DONE;
    if (osp.ctrlr_csr2&MOCSR2_ECC_DIS) {
        osp_interrupt(MOINT_ECC_DONE);
    }
}

void MO_ECC_IO_Handler(void) {
    static uint32_t old_size;
    
    switch (ecc_state) {
        case ECC_STATE_FILLING:
            if (ecc_buffer[eccin].size<ecc_buffer[eccin].limit) {
                if (osp.ctrlr_csr2&MOCSR2_ECC_MODE) {
                    ecc_buffer[eccin].limit=MO_SECTORSIZE_DISK;
                } else {
                    ecc_buffer[eccin].limit=MO_SECTORSIZE_DATA;
                }
                old_size=ecc_buffer[eccin].size;
                dma_mo_read_memory();
                
                if (ecc_buffer[eccin].size==old_size) {
                    Log_Printf(LOG_WARN,"[OSP] No more data! ECC starve! (%i byte)", old_size);
                    osp.err_stat = ERRSTAT_STARVE;
                    osp_interrupt(MOINT_DATA_ERR);
                }
            }
            if (ecc_buffer[eccin].size==ecc_buffer[eccin].limit) {
                ecc_state=ECC_STATE_ECCING;
            }
            break;
        case ECC_STATE_ECCING:
            if (ecc_mode==ECC_MODE_WRITE) {
                if (osp.ctrlr_csr2&MOCSR2_ECC_DIS) {
                    ecc_decode();
                    ecc_sequence_done();
                    return;
                } else { /* Go to disk write */
                    ecc_encode();
                    ecc_state=ECC_STATE_WAITING;
                    if (osp.sector_count==1) {
                        osp_interrupt(MOINT_ECC_DONE);
                    }
                    break;
                }
            } else { /* mode is read or verify */
                if (osp.ctrlr_csr2&MOCSR2_ECC_DIS) {
                    if (osp.ctrlr_csr2&MOCSR2_ECC_BLOCKS) {
                        ecc_buffer[eccin].limit=ecc_buffer[eccin].size=MO_SECTORSIZE_DATA;
                    }
                    ecc_encode();
                } else { /* From disk read */
                    if (ecc_buffer[eccin].size!=MO_SECTORSIZE_DISK) {
                        Log_Printf(LOG_WARN, "[OSP] ECC waiting for disk read!");
                        break; /* Loop and wait for disk read */
                    }
                    ecc_decode();
                    if (osp.sector_count==0) {
                        osp_interrupt(MOINT_ECC_DONE);
                    }
                    if (ecc_mode==ECC_MODE_VERIFY) {
                        ecc_clear_buffer();
                        ecc_sequence_done();
                        return;
                    }
                }
                ecc_state=ECC_STATE_DRAINING;
                break;
            }
        case ECC_STATE_DRAINING:
            old_size=ecc_buffer[eccout].size;
            dma_mo_write_memory();
            if (ecc_buffer[eccout].size==old_size) {
                Log_Printf(LOG_WARN,"[OSP] DMA not ready! Stopping.");
                ecc_sequence_done();
                return;
            }
            if (ecc_buffer[eccout].size==0) {
                dma_mo_write_memory(); /* Flush buffer */
                ecc_sequence_done();
                return;
            }
            break;
        case ECC_STATE_WAITING:
            if (ecc_buffer[eccout].size==0) {
                ecc_sequence_done();
                if (osp.sector_count>0) {
                    ecc_write();
                }
                return;
            }
            Log_Printf(LOG_MO_ECC_LEVEL, "[OSP] ECC waiting for disk write!");
            break;

        default:
            Log_Printf(LOG_WARN, "[OSP] Warning: ECC was reset while busy!");
            return;
    }
    
    CycInt_UpdateCycleTimeEvent(ECC_DELAY, 80, EVENT_MO_ECC_IO);
}


/* ------------------------ MAGNETO-OPTICAL DISK DRIVE ------------------------ */

/* I/O functions */

void mo_read_sector(uint32_t sector_id) {
    uint32_t sector_num = get_logical_sector(sector_id);
    
    Log_Printf(LOG_MO_IO_LEVEL, "MO disk %i: Read sector at offset %i (%i sectors remaining)",
               dnum, sector_num, osp.sector_count-1);
    
    File_Read(ecc_buffer[eccin].data, MO_SECTORSIZE_DISK, sector_num*MO_SECTORSIZE_DISK, mo[dnum].dsk);
    
    ecc_buffer[eccin].limit = ecc_buffer[eccin].size = MO_SECTORSIZE_DISK;
}

void mo_write_sector(uint32_t sector_id) {
    uint32_t sector_num = get_logical_sector(sector_id);
    
    Log_Printf(LOG_MO_IO_LEVEL, "MO disk %i: Write sector at offset %i (%i sectors remaining)",
               dnum, sector_num, osp.sector_count-1);
    
    if (ecc_buffer[eccout].limit==MO_SECTORSIZE_DISK) {
        File_Write(ecc_buffer[eccout].data, MO_SECTORSIZE_DISK, sector_num*MO_SECTORSIZE_DISK, mo[dnum].dsk);

        ecc_buffer[eccout].size = 0;
        ecc_buffer[eccout].limit = MO_SECTORSIZE_DATA;
    } else {
        Log_Printf(LOG_WARN, "MO disk %i: Incomplete write (in: size=%i limit=%i, out: size=%i limit=%i)!", dnum,
                   ecc_buffer[eccin].size, ecc_buffer[eccin].limit, ecc_buffer[eccout].size, ecc_buffer[eccout].limit);
        abort();
    }
}

void mo_erase_sector(uint32_t sector_id) {
    uint32_t sector_num = get_logical_sector(sector_id);
    
    Log_Printf(LOG_MO_IO_LEVEL, "MO disk %i: Erase sector at offset %i (%i sectors remaining)",
               dnum, sector_num, osp.sector_count-1);
    
    uint8_t erase_buf[MO_SECTORSIZE_DISK];
    memset(erase_buf, 0xFF, MO_SECTORSIZE_DISK);
    
    File_Write(erase_buf, MO_SECTORSIZE_DISK, sector_num*MO_SECTORSIZE_DISK, mo[dnum].dsk);
}

void mo_verify_sector(uint32_t sector_id) {
    uint32_t sector_num = get_logical_sector(sector_id);
    
    Log_Printf(LOG_MO_IO_LEVEL, "MO disk %i: Verify sector at offset %i (%i sectors remaining)",
               dnum, sector_num, osp.sector_count-1);
    
    File_Read(ecc_buffer[eccin].data, MO_SECTORSIZE_DISK, sector_num*MO_SECTORSIZE_DISK, mo[dnum].dsk);
    
    ecc_buffer[eccin].limit = ecc_buffer[eccin].size = MO_SECTORSIZE_DISK;
}


/* Drive commands */

#define DRV_SEK     0x0000 /* seek (last 12 bits are track position) */
#define DRV_HOS     0xA000 /* high order seek (last 4 bits are high order (<<12) track position) */
#define DRV_REC     0x1000 /* recalibrate */
#define DRV_RDS     0x2000 /* return drive status */
#define DRV_RCA     0x2200 /* return current track address */
#define DRV_RES     0x2800 /* return extended status */
#define DRV_RHS     0x2A00 /* return hardware status */
#define DRV_RGC     0x3000 /* return general config */
#define DRV_RVI     0x3F00 /* return drive version information */
#define DRV_SRH     0x4100 /* select read head */
#define DRV_SVH     0x4200 /* select verify head */
#define DRV_SWH     0x4300 /* select write head */
#define DRV_SEH     0x4400 /* select erase head */
#define DRV_SFH     0x4500 /* select RF head */
#define DRV_RID     0x5000 /* reset attn and status */
#define DRV_RJ      0x5100 /* relative jump (see below) */
#define DRV_SPM     0x5200 /* stop motor */
#define DRV_STM     0x5300 /* start motor */
#define DRV_LC      0x5400 /* lock cartridge */
#define DRV_ULC     0x5500 /* unlock cartridge */
#define DRV_EC      0x5600 /* eject */
#define DRV_SOO     0x5900 /* spiral operation on */
#define DRV_SOF     0x5A00 /* spiral operation off */
#define DRV_RSD     0x8000 /* request self-diagnostic */
#define DRV_SD      0xB000 /* send data (last 12 bits used) */

/* Relative jump:
 * bits 0 to 3: offset (signed -8 (0x8) to +7 (0x7)
 * bits 4 to 6: head select
 */

/* Head select for relative jump */
#define RJ_READ     0x10
#define RJ_VERIFY   0x20
#define RJ_WRITE    0x30
#define RJ_ERASE    0x40

/* Drive status information */

/* Disk status (returned for DRV_RDS) */
#define DS_INSERT   0x0004 /* load completed */
#define DS_RESET    0x0008 /* power on reset */
#define DS_SEEK     0x0010 /* address fault */
#define DS_CMD      0x0020 /* invalid or unimplemented command */
#define DS_INTFC    0x0040 /* interface fault */
#define DS_I_PARITY 0x0080 /* interface parity error */
#define DS_STOPPED  0x0200 /* not spinning */
#define DS_SIDE     0x0400 /* media upside down */
#define DS_SERVO    0x0800 /* servo not ready */
#define DS_POWER    0x1000 /* laser power alarm */
#define DS_WP       0x2000 /* disk write protected */
#define DS_EMPTY    0x4000 /* no disk inserted */
#define DS_BUSY     0x8000 /* execute busy */

/* Extended status (returned for DRV_RES) */
#define ES_RF       0x0002 /* RF detected */
#define ES_WR_INH   0x0008 /* write inhibit (high temperature) */
#define ES_WRITE    0x0010 /* write mode failed */
#define ES_COARSE   0x0020 /* coarse seek failed */
#define ES_TEST     0x0040 /* test write failed */
#define ES_SLEEP    0x0080 /* sleep/wakeup failed */
#define ES_LENS     0x0100 /* lens out of range */
#define ES_TRACKING 0x0200 /* tracking servo failed */
#define ES_PLL      0x0400 /* PLL failed */
#define ES_FOCUS    0x0800 /* focus failed */
#define ES_SPEED    0x1000 /* not at speed */
#define ES_STUCK    0x2000 /* disk cartridge stuck */
#define ES_ENCODER  0x4000 /* linear encoder failed */
#define ES_LOST     0x8000 /* tracing failure */

/* Hardware status (returned for DRV_RHS) */
#define HS_LASER    0x0040 /* laser power failed */
#define HS_INIT     0x0080 /* drive init failed */
#define HS_TEMP     0x0100 /* high drive temperature */
#define HS_CLAMP    0x0200 /* spindle clamp misaligned */
#define HS_STOP     0x0400 /* spindle stop timeout */
#define HS_TEMPSENS 0x0800 /* temperature sensor failed */
#define HS_LENSPOS  0x1000 /* lens position failure */
#define HS_SERVOCMD 0x2000 /* servo command failure */
#define HS_SERVOTO  0x4000 /* servo timeout failure */
#define HS_HEAD     0x8000 /* head select failure */

/* Version information (returned for DRV_RVI) */
#define VI_VERSION  0x0880

void mo_drive_cmd(uint16_t command) {

    if (!mo[dnum].connected) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Drive %i not connected.\n", dnum);
        return;
    }
    if (!mo[dnum].complete) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Drive %i not ready.\n", dnum);
        mo[dnum].dstat|=DS_BUSY;
        mo_set_signals(true, true, dnum);
        return;
    }
    
    /* Command in progress */
    mo_set_signals(false, false, dnum);
    
    if ((command&0xF000)==DRV_SEK) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Seek (%04X)\n", command);
        mo_seek(command);
    } else if ((command&0xF000)==DRV_SD) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Send Data (%04X)\n", command);
        abort();
    } else if ((command&0xFF00)==DRV_RJ) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Relative Jump (%04X)\n", command);
        mo_jump_head(command);
    } else if ((command&0xFFF0)==DRV_HOS) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: High Order Seek (%04X)\n", command);
        mo_high_order_seek(command);
    } else {
    
        switch (command&0xFFFF) {
            case DRV_REC:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Recalibrate (%04X)\n", command);
                mo_recalibrate();
                break;
            case DRV_RDS:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return Drive Status (%04X)\n", command);
                mo_return_drive_status();
                break;
            case DRV_RCA:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return Current Track Address (%04X)\n", command);
                mo_return_track_addr();
                break;
            case DRV_RES:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return Extended Status (%04X)\n", command);
                mo_return_extended_status();
                break;
            case DRV_RHS:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return Hardware Status (%04X)\n", command);
                mo_return_hardware_status();
                break;
            case DRV_RGC:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return General Config (%04X)\n", command);
                abort();
                break;
            case DRV_RVI:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Return Version Information (%04X)\n", command);
                mo_return_version();
                break;
            case DRV_SRH:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Select Read Head (%04X)\n", command);
                mo_select_head(READ_HEAD);
                break;
            case DRV_SVH:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Select Verify Head (%04X)\n", command);
                mo_select_head(VERIFY_HEAD);
                break;
            case DRV_SWH:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Select Write Head (%04X)\n", command);
                mo_select_head(WRITE_HEAD);
                break;
            case DRV_SEH:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Select Erase Head (%04X)\n", command);
                mo_select_head(ERASE_HEAD);
                break;
            case DRV_SFH:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Select RF Head (%04X)\n", command);
                mo_select_head(RF_HEAD);
                break;
            case DRV_RID:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Reset Attn and Status (%04X)\n", command);
                mo_reset_attn_status();
                break;
            case DRV_SPM:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Stop Spindle Motor (%04X)\n", command);
                mo_stop_spinning();
                break;
            case DRV_STM:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Start Spindle Motor (%04X)\n", command);
                mo_start_spinning();
                break;
            case DRV_LC:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Lock Cartridge (%04X)\n", command);
                abort();
                break;
            case DRV_ULC:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Unlock Cartridge (%04X)\n", command);
                abort();
                break;
            case DRV_EC:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Eject (%04X)\n", command);
                mo_eject_disk(-1);
                break;
            case DRV_SOO:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Start Spiraling (%04X)\n", command);
                mo_start_spiraling();
                break;
            case DRV_SOF:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Stop Spiraling (%04X)\n", command);
                mo_stop_spiraling();
                break;
            case DRV_RSD:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Request Self-Diagnostic (%04X)\n", command);
                mo_self_diagnostic();
                break;
                
            default:
                Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Unimplemented command! (%04X)\n", command);
                mo_unimplemented_cmd();
                break;
        }
    }
    Statusbar_BlinkLed(DEVICE_LED_OD);
}


bool mo_empty(void) {
    if (!mo[dnum].inserted) {
        Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Drive command: Drive %i: No disk inserted.\n", dnum);
        mo_set_signals_delayed(true, true, CMD_DELAY);
        return true;
    }
    return false;
}

bool mo_protected(void) {
    if (mo[dnum].protected) {
        if (mo[dnum].head==ERASE_HEAD || mo[dnum].head==WRITE_HEAD) {
            Log_Printf(LOG_WARN,"[MO] Drive command: Drive %i: Disk is write protected!\n", dnum);
            mo[dnum].head=NO_HEAD;
            mo_set_signals_delayed(true, true, CMD_DELAY);
            return true;
        }
    }
    return false;
}

bool mo_stopped(void) {
    if (!mo[dnum].spinning) {
        Log_Printf(LOG_WARN,"[MO] Drive command: Drive %i: Disk not spinning.\n", dnum);
        mo_unimplemented_cmd();
        return true;
    }
    return false;
}

void mo_seek(uint16_t command) {
#if SEEK_TIMING
    uint32_t seek_time=mo[dnum].head_pos;
#endif
    if (mo_stopped()) {
        return;
    }
    mo[dnum].seeking = true;
    mo[dnum].head_pos = (mo[dnum].ho_head_pos&0xF000) | (command&0x0FFF);
#if SEEK_TIMING
    if (seek_time>mo[dnum].head_pos) {
        seek_time=seek_time-mo[dnum].head_pos;
    } else {
        seek_time=mo[dnum].head_pos-seek_time;
    }
    if (seek_time>95000) {
        seek_time=95000;
    }
    seek_time+=5000;

    mo_set_signals_delayed(true, false, seek_time);
#else
    mo_set_signals_delayed(true, false, CMD_DELAY);
#endif
}

void mo_high_order_seek(uint16_t command) {
    if (mo_stopped()) {
        return;
    }
    if ((command&0xF)>4) {
        mo_unimplemented_cmd();
        return;
    }
    mo[dnum].ho_head_pos = (command&0xF)<<12;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_jump_head(uint16_t command) {
    if (mo_stopped()) {
        return;
    }
    mo[dnum].seeking = true;

    int offset = command&0x7;
    if (command&0x8) {
        offset = 8 - offset;
        mo[dnum].head_pos-=offset;
    } else {
        mo[dnum].head_pos+=offset;
    }
    mo[dnum].sec_offset=0;
    
    switch (command&0xF0) {
        case RJ_READ:
            mo[dnum].head=READ_HEAD;
            break;
        case RJ_VERIFY:
            mo[dnum].head=VERIFY_HEAD;
            break;
        case RJ_WRITE:
            mo[dnum].head=WRITE_HEAD;
            break;
        case RJ_ERASE:
            mo[dnum].head=ERASE_HEAD;
            break;
            
        default:
            mo[dnum].head=NO_HEAD;
            break;
    }
    Log_Printf(LOG_MO_CMD_LEVEL,"[MO] Relative Jump: %i sectors %s (%s head)\n", offset*16,
               (command&0x8)?"back":"forward",
               (command&0xF0)==RJ_READ?"read":
               (command&0xF0)==RJ_VERIFY?"verify":
               (command&0xF0)==RJ_WRITE?"write":
               (command&0xF0)==RJ_ERASE?"erase":"unknown");
    
    if (mo_protected()) {
        return;
    }
#if SEEK_TIMING
    mo_set_signals_delayed(true, false, 1600);
#else
    mo_set_signals_delayed(true, false, CMD_DELAY);
#endif
}

void mo_recalibrate(void) {
    if (mo_stopped()) {
        return;
    }
    mo[dnum].head_pos = 0;
    mo[dnum].sec_offset = 0;
    mo[dnum].spiraling = false;
    
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_return_drive_status(void) {
    mo[dnum].dstat&=~(DS_STOPPED|DS_EMPTY|DS_WP);
    
    if (!mo[dnum].spinning) {
        mo[dnum].dstat|=DS_STOPPED;
    }
    if (!mo[dnum].inserted) {
        mo[dnum].dstat|=DS_EMPTY;
    }
    if (mo[dnum].protected) {
        mo[dnum].dstat|=DS_WP;
    }
    mo[dnum].status = mo[dnum].dstat;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_return_track_addr(void) {
    mo[dnum].status = mo[dnum].head_pos;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_return_extended_status(void) {
    mo[dnum].status = mo[dnum].estat;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_return_hardware_status(void) {
    mo[dnum].status = mo[dnum].hstat;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_return_version(void) {
    mo[dnum].status = VI_VERSION;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_select_head(int head) {
    if (mo_stopped()) {
        return;
    }
    mo[dnum].head = head;
    if (mo_protected()) {
        return;
    }
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_reset_attn_status(void) {
    mo[dnum].dstat=mo[dnum].estat=mo[dnum].hstat=0;
    mo[dnum].attn=false;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_stop_spinning(void) {
    if (mo_empty()) {
        return;
    }
    Statusbar_AddMessage("Stopping magneto-optical disk spin", 0);
    mo[dnum].spinning=false;
    mo[dnum].spiraling=false;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_start_spinning(void) {
    if (mo_empty()) {
        return;
    }
    Statusbar_AddMessage("Starting magneto-optical disk spin", 0);
    mo[dnum].spinning=true;
    mo_set_signals_delayed(true, false, 1600000);
}

void mo_eject_disk(int drive) {
    if (drive<0) { /* Called from emulator, else called from GUI */
        drive=dnum;
        if (mo_empty())
            return;
        
        Statusbar_AddMessage("Ejecting magneto-optical disk", 0);
        mo_set_signals_delayed(true, false, CMD_DELAY);
    }

    Log_Printf(LOG_WARN, "MO disk %i: Eject",drive);
    
    mo[drive].dsk=File_Close(mo[drive].dsk);;
    mo[drive].inserted=false;
    mo[drive].spinning=false;
    mo[drive].spiraling=false;
    
    ConfigureParams.MO.drive[drive].bDiskInserted=false;
    ConfigureParams.MO.drive[drive].szImageName[0]='\0';
}

void mo_insert_disk(int drive) {
    Log_Printf(LOG_WARN, "MO disk %i: Insert %s",drive,ConfigureParams.MO.drive[drive].szImageName);
    
    if (!ConfigureParams.MO.drive[drive].bWriteProtected) {
        mo[drive].dsk = File_Open(ConfigureParams.MO.drive[drive].szImageName, "rb+");
        mo[drive].protected=false;
    }
    if (ConfigureParams.MO.drive[drive].bWriteProtected || mo[drive].dsk == NULL) {
        mo[drive].dsk = File_Open(ConfigureParams.MO.drive[drive].szImageName, "rb");
        mo[drive].protected=true;
        if (mo[drive].dsk == NULL) {
            Log_Printf(LOG_WARN, "MO disk %i: Cannot open image file %s",
                       drive, ConfigureParams.MO.drive[drive].szImageName);
            mo[drive].inserted=false;
            mo[drive].protected=false;
            Statusbar_AddMessage("Cannot insert magneto-optical disk", 0);
            return;
        }
    }
    
    Statusbar_AddMessage("Inserting magneto-optical disk", 0);
    mo[drive].dstat|=DS_INSERT;
    mo[drive].inserted=true;
    mo[drive].spinning=false;
    mo[drive].spiraling=false;
    mo_set_signals(true, false, drive);
}

void mo_start_spiraling(void) {
    if (mo_stopped()) {
        return;
    }
    if (!mo[0].spiraling && !mo[1].spiraling) { /* periodic disk operation already active? */
        CycInt_AddCycleTimeEvent(SECTOR_IO_DELAY, 400, EVENT_MO_IO);
    }
    mo[dnum].spiraling=true;

    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_stop_spiraling(void) {
    if (mo_stopped()) {
        return;
    }
    mo[dnum].spiraling=false;
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void mo_spiraling_operation(void) {
    if (!mo[0].spiraling && !mo[1].spiraling) { /* this stops periodic disk operation */
        return; /* nothing to do */
    }
    
    int i;
    for (i=0; i<MO_MAX_DRIVES; i++) {
        if (mo[i].spiraling && !mo[i].seeking) {
            
            /* If the drive is selected, connect to formatter */
            if (i==dnum) {
                fmt_io((mo[i].head_pos<<8)|mo[i].sec_offset);
            }
            
            /* Continue spiraling */
            mo[i].sec_offset++;
            mo[i].head_pos+=mo[i].sec_offset/MO_SEC_PER_TRACK;
            mo[i].sec_offset%=MO_SEC_PER_TRACK;
        }
    }
}

void mo_self_diagnostic(void) {
    mo_set_signals_delayed(true, false, CMD_DELAY);
}

void MO_IO_Handler(void) {
    mo_spiraling_operation();
    
    CycInt_UpdateCycleTimeEvent(SECTOR_IO_DELAY, 400, EVENT_MO_IO);
}

void mo_unimplemented_cmd(void) {
    mo[dnum].dstat|=DS_CMD;
    mo_set_signals_delayed(true, true, CMD_DELAY);
}

void mo_stop(void) {
    Log_Printf(LOG_WARN,"[MO] Stopping drive %i", dnum);
    mo[dnum].enabled=false;
    
    mo[dnum].spinning=false;
    mo[dnum].spiraling=false;
    mo[dnum].seeking=false;
    
    mo[dnum].head=NO_HEAD;
    mo[dnum].head_pos=0;
    mo[dnum].ho_head_pos=0;
    mo[dnum].sec_offset=0;

    mo[dnum].dstat=mo[dnum].estat=mo[dnum].hstat=0;
    mo[dnum].attn=false;
    mo_set_signals(false, false, dnum);
}

void mo_start(void) {
    if (mo[dnum].connected && !mo[dnum].enabled) {
        Log_Printf(LOG_WARN,"[MO] Starting drive %i", dnum);
        mo[dnum].enabled=true;
        mo[dnum].dstat=DS_RESET;
        mo_set_signals_delayed(true, false, 500000);
    }
}


/* MO drive signals */
void mo_connect_signals(void) {
    if (mo[dnum].complete) {
        osp.intstatus |= MOINT_CMD_COMPL;
    } else {
        osp.intstatus &= ~MOINT_CMD_COMPL;
    }
    if (mo[dnum].attn) {
        osp.intstatus |= MOINT_ATTN;
    } else {
        osp.intstatus &= ~MOINT_ATTN;
    }
    osp_set_interrupts();
}

void mo_set_signals(bool complete, bool attn, int drive) {
    if (drive<0) {
        Log_Printf(LOG_WARN, "[MO] Error: No drive specified for delayed interrupt.");
        abort();
    }
    
    mo[drive].complete = complete;
    mo[drive].attn = mo[drive].attn || attn;
    
    if (mo[drive].attn) {
        mo[drive].spiraling=false;
    }
    mo[drive].seeking = false;
    
    mo_connect_signals();
}

bool delayed_compl;
bool delayed_attn;
int delayed_drive=-1;

void mo_set_signals_delayed(bool complete, bool attn, int delay) {
    if (delay>0) {
        if (delayed_drive>=0) {
            if (delayed_drive!=dnum) {
                Log_Printf(LOG_WARN, "[MO] Warning: Delayed interrupt from other drive (%i) in progress!",delayed_drive);
                mo_set_signals(delayed_compl, delayed_attn, delayed_drive);
                CycInt_RemovePendingEvent(EVENT_MO_INTERRUPT);
            } else {
                Log_Printf(LOG_WARN, "[MO] Warning: Delayed interrupt already in progress!");
            }
        }
        delayed_drive=dnum;
        delayed_compl=complete;
        delayed_attn=attn;
        CycInt_AddCycleTimeEvent(delay, CMD_DELAY, EVENT_MO_INTERRUPT);
    } else {
        mo_set_signals(complete, attn, dnum);
    }
}

void MO_Interrupt_Handler(void) {
    mo_set_signals(delayed_compl,delayed_attn,delayed_drive);
    
    delayed_compl=false;
    delayed_attn=false;
    delayed_drive=-1;
}


/* Initialize/Uninitialize MO disks */
void MO_Reset(void) {
    Log_Printf(LOG_WARN, "Loading magneto-optical disks:");
    
    for (dnum=0; dnum<MO_MAX_DRIVES; dnum++) {
        mo[dnum].dsk=File_Close(mo[dnum].dsk);
        mo[dnum].connected=false;
        mo[dnum].inserted=false;
        mo_stop();

        if (ConfigureParams.MO.drive[dnum].bDriveConnected) {
            mo[dnum].connected=true;
            mo[dnum].enabled=true;
            mo[dnum].complete=true;
            /* Insert disk */
            if (ConfigureParams.MO.drive[dnum].bDiskInserted) {
                MO_Insert(dnum);
            }
        }
    }
    
    osp_select(0);
    
    /* Initialize formatter variables */
    fmt_mode=FMT_MODE_IDLE;
    ecc_state=ECC_STATE_DONE;
    
    /* Stop all periodic operations */
    CycInt_RemovePendingEvent(EVENT_MO_INTERRUPT);
    CycInt_RemovePendingEvent(EVENT_MO_IO);
    CycInt_RemovePendingEvent(EVENT_MO_ECC_IO);
}

void MO_Insert(int drive) {
    Log_Printf(LOG_WARN, "MO disk %i: %s",drive,ConfigureParams.MO.drive[drive].szImageName);

    mo_insert_disk(drive);
}

void MO_Eject(int drive) {
    Log_Printf(LOG_WARN, "Unloading magneto-optical disk %i",drive);

    mo_eject_disk(drive);
}
