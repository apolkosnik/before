/*
  Previous - nbic.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the NextBus interface chip (NBIC).
*/
const char Nbic_fileid[] = "Previous nbic.c";

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "nbic.h"

#define LOG_NEXTBUS_LEVEL   LOG_NONE


/* NBIC Registers */
struct {
	uint32_t control;
	uint32_t id;
	uint8_t intstatus;
	uint8_t intmask;
} nbic;

#define LOG_NBIC_LEVEL LOG_NONE

/* Control: 0x02020000 (rw), only non-Turbo */
#define NBIC_CTRL_IGNSID0	0x10000000 /* Ignore slot ID bit 0 */
#define NBIC_CTRL_STFWD		0x08000000 /* Store forward */
#define NBIC_CTRL_RMCOL		0x04000000 /* Read modify cycle collision */

/* ID: 0x02020004 (w), 0xF0FFFFFx (r), only non-Turbo */
#define NBIC_ID_VALID		0x80000000
#define NBIC_ID_M_MASK		0x7FFF0000 /* Manufacturer ID */
#define NBIC_ID_B_MASK		0x0000FFFF /* Board ID */

/* Interrupt: 0xF0FFFFE8 (r) */
#define NBIC_INT_STATUS		0x80

/* Interrupt mask: 0xF0FFFFEC (rw) */
#define NBIC_INT_MASK		0x80


/* Register access functions */
static uint8_t nbic_control_read0(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 0) read at %08X",addr);
	return (nbic.control>>24);
}
static uint8_t nbic_control_read1(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 1) read at %08X",addr);
	return (nbic.control>>16);
}
static uint8_t nbic_control_read2(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 2) read at %08X",addr);
	return (nbic.control>>8);
}
static uint8_t nbic_control_read3(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 3) read at %08X",addr);
	return nbic.control;
}

static void nbic_control_write0(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 0) write %02X at %08X",val,addr);
	nbic.control &= 0x00FFFFFF;
	nbic.control |= (uint32_t)val<<24;
}
static void nbic_control_write1(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 1) write %02X at %08X",val,addr);
	nbic.control &= 0xFF00FFFF;
	nbic.control |= val<<16;
}
static void nbic_control_write2(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 2) write %02X at %08X",val,addr);
	nbic.control &= 0xFFFF00FF;
	nbic.control |= val<<8;
}
static void nbic_control_write3(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Control (byte 3) write %02X at %08X",val,addr);
	nbic.control &= 0xFFFFFF00;
	nbic.control |= val;
}


static uint8_t nbic_id_read0(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 0) read at %08X",addr);
	return (nbic.id>>24);
}
static uint8_t nbic_id_read1(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 1) read at %08X",addr);
	return (nbic.id>>16);
}
static uint8_t nbic_id_read2(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 2) read at %08X",addr);
	return (nbic.id>>8);
}
static uint8_t nbic_id_read3(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 3) read at %08X",addr);
	return nbic.id;
}

static void nbic_id_write0(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 0) write %02X at %08X",val,addr);
	nbic.id &= 0x00FFFFFF;
	nbic.id |= (uint32_t)val<<24;
}
static void nbic_id_write1(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 1) write %02X at %08X",val,addr);
	nbic.id &= 0xFF00FFFF;
	nbic.id |= val<<16;
}
static void nbic_id_write2(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 2) write %02X at %08X",val,addr);
	nbic.id &= 0xFFFF00FF;
	nbic.id |= val<<8;
}
static void nbic_id_write3(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] ID (byte 3) write %02X at %08X",val,addr);
	nbic.id &= 0xFFFFFF00;
	nbic.id |= val;
}

static uint8_t nbic_intstatus_read(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Interrupt status read at %08X",addr);
	return nbic.intstatus;
}

static uint8_t nbic_intmask_read(uint32_t addr) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Interrupt mask read at %08X",addr);
	return nbic.intmask;
}
static void nbic_intmask_write(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_NBIC_LEVEL, "[NBIC] Interrupt mask write %02X at %08X",val,addr);
	nbic.intmask = val;
}

static uint8_t nbic_zero_read(uint32_t addr) {
	Log_Printf(LOG_WARN, "[NBIC] zero read at %08X",addr);
	return 0;
}
static void nbic_zero_write(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_WARN, "[NBIC] zero write %02X at %08X",val,addr);
}

static uint8_t nbic_bus_error_read(uint32_t addr) {
	Log_Printf(LOG_WARN, "[NBIC] bus error read at %08X",addr);
	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}
static void nbic_bus_error_write(uint32_t addr, uint8_t val) {
	Log_Printf(LOG_WARN, "[NBIC] bus error write at %08X",addr);
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, val);
}


/* Direct register access */
static uint8_t (*nbic_read_reg[8])(uint32_t) = {
	nbic_control_read0, nbic_control_read1, nbic_control_read2, nbic_control_read3,
	nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read
};

static void (*nbic_write_reg[8])(uint32_t, uint8_t) = {
	nbic_control_write0, nbic_control_write1, nbic_control_write2, nbic_control_write3,
	nbic_id_write0, nbic_id_write1, nbic_id_write2, nbic_id_write3
};


uae_u32 nbic_reg_lget(uaecptr addr) {
	uae_u32 val = 0;
	
	if (addr&3) {
		Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
		abort();
	}
	
	if ((addr&0x0000FFFF)>7) {
		nbic_bus_error_read(addr);
	} else {
		val = nbic_read_reg[addr&7](addr)<<24;
		val |= nbic_read_reg[(addr&7)+1](addr+1)<<16;
		val |= nbic_read_reg[(addr&7)+2](addr+2)<<8;
		val |= nbic_read_reg[(addr&7)+3](addr+3);
	}
	
	return val;
}

uae_u32 nbic_reg_wget(uaecptr addr) {
	uae_u32 val = 0;
	
	if (addr&1) {
		Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
		abort();
	}
	
	if ((addr&0x0000FFFF)>7) {
		nbic_bus_error_read(addr);
	} else {
		val = nbic_read_reg[addr&7](addr)<<8;
		val |= nbic_read_reg[(addr&7)+1](addr);
	}
	
	return val;
}

uae_u32 nbic_reg_bget(uaecptr addr) {
	if ((addr&0x0000FFFF)>7) {
		return nbic_bus_error_read(addr);
	} else {
		return nbic_read_reg[addr&7](addr);
	}
}

void nbic_reg_lput(uaecptr addr, uae_u32 l) {
	if (addr&3) {
		Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
		abort();
	}
	
	if ((addr&0x0000FFFF)>7) {
		nbic_bus_error_write(addr,0);
	} else {
		nbic_write_reg[addr&7](addr,l>>24);
		nbic_write_reg[(addr&7)+1](addr,l>>16);
		nbic_write_reg[(addr&7)+2](addr,l>>8);
		nbic_write_reg[(addr&7)+3](addr,l);
	}
}

void nbic_reg_wput(uaecptr addr, uae_u32 w) {
	if (addr&1) {
		Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
		abort();
	}
	
	if ((addr&0x0000FFFF)>7) {
		nbic_bus_error_write(addr,0);
	} else {
		nbic_write_reg[addr&7](addr,w>>8);
		nbic_write_reg[(addr&7)+1](addr,w);
	}
}

void nbic_reg_bput(uaecptr addr, uae_u32 b) {
	if ((addr&0x0000FFFF)>7) {
		nbic_bus_error_write(addr,0);
	} else {
		nbic_write_reg[addr&7](addr,b);
	}
}


/* NeXTbus CPU board slot space access */
static uint8_t (*nbic_read_cpu_slot[32])(uint32_t) = {
	nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read,
	nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read, nbic_bus_error_read,
	nbic_intstatus_read, nbic_zero_read, nbic_zero_read, nbic_zero_read,
	nbic_intmask_read, nbic_zero_read, nbic_zero_read, nbic_zero_read,

	nbic_id_read0, nbic_zero_read, nbic_zero_read, nbic_zero_read,
	nbic_id_read1, nbic_zero_read, nbic_zero_read, nbic_zero_read,
	nbic_id_read2, nbic_zero_read, nbic_zero_read, nbic_zero_read,
	nbic_id_read3, nbic_zero_read, nbic_zero_read, nbic_zero_read,
};

static void (*nbic_write_cpu_slot[32])(uint32_t, uint8_t) = {
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_intmask_write, nbic_zero_write, nbic_zero_write, nbic_zero_write,
	
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write,
	nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write, nbic_bus_error_write
};

uint32_t nb_cpu_slot_lget(uint32_t addr) {
    uint32_t val = 0;
    
    if (addr&3) {
        Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
        abort();
    }
    
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        nbic_bus_error_read(addr);
    } else {
        val = (uint32_t)nbic_read_cpu_slot[addr&0x1F](addr)<<24;
        val |= nbic_read_cpu_slot[(addr&0x1F)+1](addr+1)<<16;
        val |= nbic_read_cpu_slot[(addr&0x1F)+2](addr+2)<<8;
        val |= nbic_read_cpu_slot[(addr&0x1F)+3](addr+3);
    }
    
    return val;
}

uint16_t nb_cpu_slot_wget(uint32_t addr) {
    uint32_t val = 0;
    
    if (addr&1) {
        Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
        abort();
    }
    
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        nbic_bus_error_read(addr);
    } else {
        val = nbic_read_cpu_slot[addr&0x1F](addr)<<8;
        val |= nbic_read_cpu_slot[(addr&0x1F)+1](addr+1)<<16;
    }
    
    return val;
}

uint8_t nb_cpu_slot_bget(uint32_t addr) {
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        return nbic_bus_error_read(addr);
    } else {
        return nbic_read_cpu_slot[addr&0x1F](addr);
    }
}

void nb_cpu_slot_lput(uint32_t addr, uint32_t l) {
    if (addr&3) {
        Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
        abort();
    }
    
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        nbic_bus_error_write(addr,0);
    } else {
        nbic_write_cpu_slot[addr&0x1F](addr,l>>24);
        nbic_write_cpu_slot[(addr&0x1F)+1](addr,l>>16);
        nbic_write_cpu_slot[(addr&0x1F)+2](addr,l>>8);
        nbic_write_cpu_slot[(addr&0x1F)+3](addr,l);
    }
}

void nb_cpu_slot_wput(uint32_t addr, uint16_t w) {
    if (addr&1) {
        Log_Printf(LOG_WARN, "[NBIC] Unaligned access at %08X.",addr);
        abort();
    }
    
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        nbic_bus_error_write(addr,0);
    } else {
        nbic_write_cpu_slot[addr&0x1F](addr,w>>8);
        nbic_write_cpu_slot[(addr&0x1F)+1](addr,w);
    }
}

void nb_cpu_slot_bput(uint32_t addr, uint8_t b) {
    if ((addr&0x00FFFFFF)<0x00FFFFE8) {
        nbic_bus_error_write(addr,0);
    } else {
        nbic_write_cpu_slot[addr&0x1F](addr,b);
    }
}



