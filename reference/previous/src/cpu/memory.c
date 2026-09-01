 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Memory management
  *
  * (c) 1995 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  * Adaptation to Previous by Andreas Grabher
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
  */
const char Memory_fileid[] = "Previous memory.c";

#include "main.h"
#include "sysdeps.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "memory.h"

#include "rom.h"
#include "bmap.h"
#include "tmc.h"
#include "ncc.h"
#include "nbic.h"
#include "ioMem.h"
#include "reset.h"
#include "m68000.h"
#include "configuration.h"
#include "NextBus.hpp"

#include "newcpu.h"


/* Set illegal_mem to 1 for debug output: */
#define illegal_mem 1

#define illegal_trace(s) {static int count=0; if (count++<50) { s; }}

/*
 * NeXT memory map (example for 68030 NeXT Computer)
 *
 * Local bus:
 * 0x00000000 - 0x00FFFFFF: ROM
 * 0x01000000 - 0x01FFFFFF: Diagnsotic ROM
 *
 * 0x02000000 - 0x020FFFFF: Device space
 *
 * 0x04000000 - 0x04FFFFFF: RAM bank 0
 * 0x05000000 - 0x05FFFFFF: RAM bank 1
 * 0x06000000 - 0x06FFFFFF: RAM bank 2
 * 0x07000000 - 0x07FFFFFF: RAM bank 3
 *
 * 0x0B000000 - 0x0BFFFFFF: VRAM
 *
 * 0x0C000000 - 0x0CFFFFFF: VRAM mirror for MWF0
 * 0x0D000000 - 0x0DFFFFFF: VRAM mirror for MWF1
 * 0x0E000000 - 0x0EFFFFFF: VRAM mirror for MWF2
 * 0x0F000000 - 0x0FFFFFFF: VRAM mirror for MWF3
 *
 * 0x10000000 - 0x13FFFFFF: RAM mirror for MWF0
 * 0x14000000 - 0x17FFFFFF: RAM mirror for MWF1
 * 0x18000000 - 0x1BFFFFFF: RAM mirror for MWF2
 * 0x1C000000 - 0x1FFFFFFF: RAM mirror for MWF3
 *
 * NextBus: (Note: Boards can be configured to occupy 1 or 2 slots)
 * 0x00000000 - 0x1FFFFFFF: NeXTbus board space for slot 0
 * 0x20000000 - 0x3FFFFFFF: NeXTbus board space for slot 2
 * 0x40000000 - 0x5FFFFFFF: NeXTbus board space for slot 4
 * 0x60000000 - 0x7FFFFFFF: NeXTbus board space for slot 6
 *
 * 0xF0000000 - 0xF0FFFFFF: NeXTbus slot space for slot 0
 * 0xF2000000 - 0xF2FFFFFF: NeXTbus slot space for slot 2
 * 0xF4000000 - 0xF4FFFFFF: NeXTbus slot space for slot 4
 * 0xF6000000 - 0xF6FFFFFF: NeXTbus slot space for slot 6
 */

/* Pointers to memory */
uae_u8* NEXTVideo = NULL;
uae_u8* NEXTRam   = NULL;
uae_u8* NEXTRom   = NULL;
uae_u8* NEXTIo    = NULL;

/* Unused stuff */
uae_u8 ce_banktype[65536], ce_cachable[65536];


/* ROM */
#define NEXT_EPROM_START        0x00000000
#define NEXT_EPROM_DIAG_START   0x01000000
#define NEXT_EPROM_BMAP_START   0x01000000
#define NEXT_EPROM_SIZE         0x01000000
#define NEXT_EPROM_ALLOC        0x00020000
#define NEXT_EPROM_MASK         0x0001FFFF

/* Main memory */
#define N_BANKS 4

#define NEXT_RAM_START          0x04000000
#define NEXT_RAM_SIZE           0x04000000
#define NEXT_RAM_MASK           0x03FFFFFF

#define NEXT_RAM_BANK_SIZE      0x01000000
#define NEXT_RAM_BANK_SIZE_C    0x00800000
#define NEXT_RAM_BANK_SIZE_T    0x02000000

#define NEXT_RAM_BANK_SEL       0x03000000
#define NEXT_RAM_BANK_SEL_C     0x01800000
#define NEXT_RAM_BANK_SEL_T     0x06000000

#define NEXT_RAM_ALLOC          (N_BANKS*NEXT_RAM_BANK_SIZE)
#define NEXT_RAM_ALLOC_C        (N_BANKS*NEXT_RAM_BANK_SIZE_C)
#define NEXT_RAM_ALLOC_T        (N_BANKS*NEXT_RAM_BANK_SIZE_T)

static uae_u32 next_ram_bank_size;
static uae_u32 next_ram_bank_mask;
static uae_u32 next_ram_bank0_mask;
static uae_u32 next_ram_bank1_mask;
static uae_u32 next_ram_bank2_mask;
static uae_u32 next_ram_bank3_mask;

/* Main memory with memory write functions */
#define NEXT_RAM_MWF0_START     0x10000000
#define NEXT_RAM_MWF1_START     0x14000000
#define NEXT_RAM_MWF2_START     0x18000000
#define NEXT_RAM_MWF3_START     0x1C000000

/* VRAM monochrome */
#define NEXT_VRAM_START         0x0B000000
#define NEXT_VRAM_SIZE          0x01000000
#define NEXT_VRAM_ALLOC         0x00040000
#define NEXT_VRAM_MASK          0x0003FFFF

/* VRAM monochrome with memory write functions */
#define NEXT_VRAM_MWF0_START    0x0C000000
#define NEXT_VRAM_MWF1_START    0x0D000000
#define NEXT_VRAM_MWF2_START    0x0E000000
#define NEXT_VRAM_MWF3_START    0x0F000000

/* VRAM color */
#define NEXT_VRAM_COLOR_START   0x2C000000
#define NEXT_VRAM_COLOR_SIZE    0x01000000
#define NEXT_VRAM_COLOR_ALLOC   0x00200000
#define NEXT_VRAM_COLOR_MASK    0x001FFFFF

/* VRAM turbo monochrome and color */
#define NEXT_VRAM_TURBO_START   0x0C000000

/* IO memory */
#define NEXT_IO_START           0x02000000
#define NEXT_IO_BMAP_START      0x02100000
#define NEXT_IO_SIZE            0x00020000
#define NEXT_IO_ALLOC           0x00020000
#define NEXT_IO_MASK            0x0001FFFF

#define NEXT_BMAP_START         0x020C0000
#define NEXT_BMAP_START2        0x820C0000
#define NEXT_BMAP_SIZE          0x00010000
#define NEXT_BMAP_MASK          0x0000003F

#define NEXT_TMC_START          0x02200000
#define NEXT_TMC_SIZE           0x00010000
#define NEXT_TMC_MASK           0x0000FFFF

#define NEXT_NCC_START          0x02210000
#define NEXT_NCC_SIZE           0x00010000
#define NEXT_NCC_MASK           0x0000FFFF

#define NEXT_NBIC_START         0x02020000
#define NEXT_NBIC_SIZE          0x00010000
#define NEXT_NBIC_MASK          0x00000007

/* Cache memory for nitro systems */
#define NEXT_CACHE_TAG_START    0x03E00000
#define NEXT_CACHE_TAG_SIZE     0x00100000
#define NEXT_CACHE_TAG_MASK     0x000FFFFF

#define NEXT_CACHE_START        0x03F00000
#define NEXT_CACHE_SIZE         0x00100000
#define NEXT_CACHE_MASK         0x000FFFFF

/* NeXTbus slot memory space */
#define NEXTBUS_SLOT_START(x)   (0xF0000000|((x)<<24))
#define NEXTBUS_SLOT_SIZE       0x01000000
#define NEXTBUS_SLOT_MASK       0x00FFFFFF

/* NeXTbus board memory space */
#define NEXTBUS_BOARD_START(x)  ((x)<<28)
#define NEXTBUS_BOARD_SIZE      0x10000000
#define NEXTBUS_BOARD_MASK      0x0FFFFFFF


/* **** A dummy bank that only contains zeros **** */
#if 0 /* unused */
static uae_u32 dummy_lget(uaecptr addr)
{
	illegal_trace(write_log ("Illegal lget at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
	return 0;
}
#endif
static uae_u32 dummy_wget(uaecptr addr)
{
	illegal_trace(write_log ("Illegal wget at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
	return 0;
}

static uae_u32 dummy_bget(uaecptr addr)
{
	illegal_trace(write_log ("Illegal bget at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
	return 0;
}
#if 0 /* unused */
static void dummy_lput(uaecptr addr, uae_u32 l)
{
	illegal_trace(write_log ("Illegal lput at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
}
#endif
static void dummy_wput(uaecptr addr, uae_u32 w)
{
	illegal_trace(write_log ("Illegal wput at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
}

static void dummy_bput(uaecptr addr, uae_u32 b)
{
	illegal_trace(write_log ("Illegal bput at %08lx PC=%08x\n", (long)addr,m68k_getpc()));
}


/* **** This memory bank only generates bus errors **** */

static uae_u32 BusErrMem_lget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Bus error lget at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static uae_u32 BusErrMem_wget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Bus error wget at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static uae_u32 BusErrMem_bget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Bus error bget at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static void BusErrMem_lput(uaecptr addr, uae_u32 l)
{
	if (illegal_mem)
		write_log ("Bus error lput at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
}

static void BusErrMem_wput(uaecptr addr, uae_u32 w)
{
	if (illegal_mem)
		write_log ("Bus error wput at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
}

static void BusErrMem_bput(uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Bus error bput at %08lx\n", (long)addr);
	
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
}


/* **** ROM **** */

static uae_u32 mem_rom_lget(uaecptr addr)
{
	addr &= NEXT_EPROM_MASK;
	return do_get_mem_long(NEXTRom + addr);
}

static uae_u32 mem_rom_wget(uaecptr addr)
{
	addr &= NEXT_EPROM_MASK;
	return do_get_mem_word(NEXTRom + addr);
	
}

static uae_u32 mem_rom_bget(uaecptr addr)
{
	addr &= NEXT_EPROM_MASK;
	return NEXTRom[addr];
}

static void mem_rom_lput(uaecptr addr, uae_u32 l)
{
	illegal_trace(write_log ("Illegal ROMmem lput at %08lx\n", (long)addr));
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
}

static void mem_rom_wput(uaecptr addr, uae_u32 w)
{
	illegal_trace(write_log ("Illegal ROMmem wput at %08lx\n", (long)addr));
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
}

static void mem_rom_bput(uaecptr addr, uae_u32 b)
{
	illegal_trace(write_log ("Illegal ROMmem bput at %08lx\n", (long)addr));
	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
}


/* **** Main memory **** */

static uae_u32 mem_ram_bank0_lget(uaecptr addr)
{
	addr &= next_ram_bank0_mask;
	return do_get_mem_long(NEXTRam + addr);
}

static uae_u32 mem_ram_bank0_wget(uaecptr addr)
{
	addr &= next_ram_bank0_mask;
	return do_get_mem_word(NEXTRam + addr);
}

static uae_u32 mem_ram_bank0_bget(uaecptr addr)
{
	addr &= next_ram_bank0_mask;
	return NEXTRam[addr];
}

static void mem_ram_bank0_lput(uaecptr addr, uae_u32 l)
{
	addr &= next_ram_bank0_mask;
	do_put_mem_long(NEXTRam + addr, l);
}

static void mem_ram_bank0_wput(uaecptr addr, uae_u32 w)
{
	addr &= next_ram_bank0_mask;
	do_put_mem_word(NEXTRam + addr, w);
}

static void mem_ram_bank0_bput(uaecptr addr, uae_u32 b)
{
	addr &= next_ram_bank0_mask;
	NEXTRam[addr] = b;
}


static uae_u32 mem_ram_bank1_lget(uaecptr addr)
{
	addr &= next_ram_bank1_mask;
	return do_get_mem_long(NEXTRam + addr);
}

static uae_u32 mem_ram_bank1_wget(uaecptr addr)
{
	addr &= next_ram_bank1_mask;
	return do_get_mem_word(NEXTRam + addr);
}

static uae_u32 mem_ram_bank1_bget(uaecptr addr)
{
	addr &= next_ram_bank1_mask;
	return NEXTRam[addr];
}

static void mem_ram_bank1_lput(uaecptr addr, uae_u32 l)
{
	addr &= next_ram_bank1_mask;
	do_put_mem_long(NEXTRam + addr, l);
}

static void mem_ram_bank1_wput(uaecptr addr, uae_u32 w)
{
	addr &= next_ram_bank1_mask;
	do_put_mem_word(NEXTRam + addr, w);
}

static void mem_ram_bank1_bput(uaecptr addr, uae_u32 b)
{
	addr &= next_ram_bank1_mask;
	NEXTRam[addr] = b;
}


static uae_u32 mem_ram_bank2_lget(uaecptr addr)
{
	addr &= next_ram_bank2_mask;
	return do_get_mem_long(NEXTRam + addr);
}

static uae_u32 mem_ram_bank2_wget(uaecptr addr)
{
	addr &= next_ram_bank2_mask;
	return do_get_mem_word(NEXTRam + addr);
}

static uae_u32 mem_ram_bank2_bget(uaecptr addr)
{
	addr &= next_ram_bank2_mask;
	return NEXTRam[addr];
}

static void mem_ram_bank2_lput(uaecptr addr, uae_u32 l)
{
	addr &= next_ram_bank2_mask;
	do_put_mem_long(NEXTRam + addr, l);
}

static void mem_ram_bank2_wput(uaecptr addr, uae_u32 w)
{
	addr &= next_ram_bank2_mask;
	do_put_mem_word(NEXTRam + addr, w);
}

static void mem_ram_bank2_bput(uaecptr addr, uae_u32 b)
{
	addr &= next_ram_bank2_mask;
	NEXTRam[addr] = b;
}


static uae_u32 mem_ram_bank3_lget(uaecptr addr)
{
	addr &= next_ram_bank3_mask;
	return do_get_mem_long(NEXTRam + addr);
}

static uae_u32 mem_ram_bank3_wget(uaecptr addr)
{
	addr &= next_ram_bank3_mask;
	return do_get_mem_word(NEXTRam + addr);
}

static uae_u32 mem_ram_bank3_bget(uaecptr addr)
{
	addr &= next_ram_bank3_mask;
	return NEXTRam[addr];
}

static void mem_ram_bank3_lput(uaecptr addr, uae_u32 l)
{
	addr &= next_ram_bank3_mask;
	do_put_mem_long(NEXTRam + addr, l);
}

static void mem_ram_bank3_wput(uaecptr addr, uae_u32 w)
{
	addr &= next_ram_bank3_mask;
	do_put_mem_word(NEXTRam + addr, w);
}

static void mem_ram_bank3_bput(uaecptr addr, uae_u32 b)
{
	addr &= next_ram_bank3_mask;
	NEXTRam[addr] = b;
}


/* **** Main memory empty areas **** */

static uae_u32 mem_ram_empty_lget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Empty mem area lget at %08lx\n", (long)addr);
	
	return addr;
}

static uae_u32 mem_ram_empty_wget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Empty mem area wget at %08lx\n", (long)addr);
	
	return addr;
}

static uae_u32 mem_ram_empty_bget(uaecptr addr)
{
	if (illegal_mem)
		write_log ("Empty mem area bget at %08lx\n", (long)addr);
	
	return addr;
}

static void mem_ram_empty_lput(uaecptr addr, uae_u32 l)
{
	if (illegal_mem)
		write_log ("Empty mem area lput at %08lx\n", (long)addr);
}

static void mem_ram_empty_wput(uaecptr addr, uae_u32 w)
{
	if (illegal_mem)
		write_log ("Empty mem area wput at %08lx\n", (long)addr);
}

static void mem_ram_empty_bput(uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Empty mem area bput at %08lx\n", (long)addr);
}


/* **** VRAM for monochrome systems **** */

static uae_u32 mem_video_lget(uaecptr addr)
{
	addr &= NEXT_VRAM_MASK;
	return do_get_mem_long(NEXTVideo + addr);
}

static uae_u32 mem_video_wget(uaecptr addr)
{
	addr &= NEXT_VRAM_MASK;
	return do_get_mem_word(NEXTVideo + addr);
}

static uae_u32 mem_video_bget(uaecptr addr)
{
	addr &= NEXT_VRAM_MASK;
	return NEXTVideo[addr];
}

static void mem_video_lput(uaecptr addr, uae_u32 l)
{
	addr &= NEXT_VRAM_MASK;
	do_put_mem_long(NEXTVideo + addr, l);
}

static void mem_video_wput(uaecptr addr, uae_u32 w)
{
	addr &= NEXT_VRAM_MASK;
	do_put_mem_word(NEXTVideo + addr, w);
}

static void mem_video_bput(uaecptr addr, uae_u32 b)
{
	addr &= NEXT_VRAM_MASK;
	NEXTVideo[addr] = b;
}


/* **** Memory banks with write functions **** */

static uae_u8 mwf0[4][4] = { /* AB */
	{ 0, 0, 0, 0 },
	{ 0, 0, 1, 1 },
	{ 0, 1, 1, 2 },
	{ 0, 1, 2, 3 }
};

static uae_u8 mwf1[4][4] = { /* ceil(A+B) */
	{ 0, 1, 2, 3 },
	{ 1, 2, 3, 3 },
	{ 2, 3, 3, 3 },
	{ 3, 3, 3, 3 }
};

static uae_u8 mwf2[4][4] = { /* (1-A)B */
	{ 0, 0, 0, 0 },
	{ 1, 1, 0, 0 },
	{ 2, 1, 1, 0 },
	{ 3, 2, 1, 0 }
};

static uae_u8 mwf3[4][4] = { /* A+B-AB */
	{ 0, 1, 2, 3 },
	{ 1, 2, 2, 3 },
	{ 2, 2, 3, 3 },
	{ 3, 3, 3, 3 }
};

static uae_u32 memory_write_func(uae_u32 old, uae_u32 new, int function, int size)
{
	int a,b,i;
	uae_u32 v=0;
#if 0
	write_log("[MWF] Function%i: size=%i, old=%08X, new=%08X\n",function,size,old,new);
#endif
	
	switch (function) {
		case 0:
			for (i=0; i<(size*4); i++) {
				a=old>>(i*2)&3;
				b=new>>(i*2)&3;
				v|=(uae_u32)mwf0[a][b]<<(i*2);
			}
			return v;
		case 1:
			for (i=0; i<(size*4); i++) {
				a=old>>(i*2)&3;
				b=new>>(i*2)&3;
				v|=(uae_u32)mwf1[a][b]<<(i*2);
			}
			return v;
		case 2:
			for (i=0; i<(size*4); i++) {
				a=old>>(i*2)&3;
				b=new>>(i*2)&3;
				v|=(uae_u32)mwf2[a][b]<<(i*2);
			}
			return v;
		case 3:
			for (i=0; i<(size*4); i++) {
				a=old>>(i*2)&3;
				b=new>>(i*2)&3;
				v|=(uae_u32)mwf3[a][b]<<(i*2);
			}
			return v;
			
		default:
			write_log("Unknown memory write function!\n");
			abort();
	}
}

static uae_u32 mem_ram_mwf_lget(uaecptr addr)
{
	int function = (addr>>26)&0x3;
	
	return function==0?0xFFFFFFFF:0;
}

static uae_u32 mem_ram_mwf_wget(uaecptr addr)
{
	int function = (addr>>26)&0x3;
	
	return function==0?0xFFFF:0;
}

static uae_u32 mem_ram_mwf_bget(uaecptr addr)
{
	int function = (addr>>26)&0x3;
	
	return function==0?0xFF:0;
}

static void mem_ram_mwf_lput(uaecptr addr, uae_u32 l)
{
	int function = (addr>>26)&0x3;
	addr = NEXT_RAM_START|(addr&NEXT_RAM_MASK);
	
	uae_u32 old = get_long(addr);
	uae_u32 val = memory_write_func(old, l, function, 4);
	
	put_long(addr, val);
}

static void mem_ram_mwf_wput(uaecptr addr, uae_u32 w)
{
	int function = (addr>>26)&0x3;
	addr = NEXT_RAM_START|(addr&NEXT_RAM_MASK);
	
	uae_u32 old = get_word(addr);
	uae_u32 val = memory_write_func(old, w, function, 2);
	
	put_word(addr, val);
}

static void mem_ram_mwf_bput(uaecptr addr, uae_u32 b)
{
	int function = (addr>>26)&0x3;
	addr = NEXT_RAM_START|(addr&NEXT_RAM_MASK);
	
	uae_u32 old = get_byte(addr);
	uae_u32 val = memory_write_func(old, b, function, 1);
	
	put_byte(addr, val);
}


static uae_u32 mem_video_mwf_lget(uaecptr addr)
{
	int function = (addr>>24)&0x3;
	
	return function==0?0xFFFFFFFF:0;
}

static uae_u32 mem_video_mwf_wget(uaecptr addr)
{
	int function = (addr>>24)&0x3;
	
	return function==0?0xFFFF:0;
}

static uae_u32 mem_video_mwf_bget(uaecptr addr)
{
	int function = (addr>>24)&0x3;
	
	return function==0?0xFF:0;
}

static void mem_video_mwf_lput(uaecptr addr, uae_u32 l)
{
	int function = (addr>>24)&0x3;
	addr = NEXT_VRAM_START|(addr&NEXT_VRAM_MASK);
	
	uae_u32 old = get_long(addr);
	uae_u32 val = memory_write_func(old, l, function, 4);
	
	put_long(addr, val);
}

static void mem_video_mwf_wput(uaecptr addr, uae_u32 w)
{
	int function = (addr>>24)&0x3;
	addr = NEXT_VRAM_START|(addr&NEXT_VRAM_MASK);
	
	uae_u32 old = get_word(addr);
	uae_u32 val = memory_write_func(old, w, function, 2);
	
	put_word(addr, val);
}

static void mem_video_mwf_bput(uaecptr addr, uae_u32 b)
{
	int function = (addr>>24)&0x3;
	addr = NEXT_VRAM_START|(addr&NEXT_VRAM_MASK);
	
	uae_u32 old = get_byte(addr);
	uae_u32 val = memory_write_func(old, b, function, 1);
	
	put_byte(addr, val);
}


/* **** VRAM for color systems **** */

static uae_u32 mem_color_video_lget(uaecptr addr)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	return do_get_mem_long(NEXTVideo + addr);
}

static uae_u32 mem_color_video_wget(uaecptr addr)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	return do_get_mem_word(NEXTVideo + addr);
}

static uae_u32 mem_color_video_bget(uaecptr addr)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	return NEXTVideo[addr];
}

static void mem_color_video_lput(uaecptr addr, uae_u32 l)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	do_put_mem_long(NEXTVideo + addr, l);
}

static void mem_color_video_wput(uaecptr addr, uae_u32 w)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	do_put_mem_word(NEXTVideo + addr, w);
}

static void mem_color_video_bput(uaecptr addr, uae_u32 b)
{
	addr &= NEXT_VRAM_COLOR_MASK;
	NEXTVideo[addr] = b;
}


/* **** Access to BMAP chip **** */

static uae_u32 mem_bmap_lget(uaecptr addr)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}
	addr &= NEXT_BMAP_MASK;
	return bmap_lget(addr);
}

static uae_u32 mem_bmap_wget(uaecptr addr)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}
	addr &= NEXT_BMAP_MASK;
	return bmap_wget(addr);
}

static uae_u32 mem_bmap_bget(uaecptr addr)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}
	addr &= NEXT_BMAP_MASK;
	return bmap_bget(addr);
}

static void mem_bmap_lput(uaecptr addr, uae_u32 l)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
	}
	addr &= NEXT_BMAP_MASK;
	bmap_lput(addr, l);
}

static void mem_bmap_wput(uaecptr addr, uae_u32 w)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
	}
	addr &= NEXT_BMAP_MASK;
	bmap_wput(addr, w);
}

static void mem_bmap_bput(uaecptr addr, uae_u32 b)
{
	if ((addr&(NEXT_BMAP_SIZE-1))>NEXT_BMAP_MASK) {
		write_log ("bmap bus error at %08lx PC=%08x\n", (long)addr,m68k_getpc());
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
	}
	addr &= NEXT_BMAP_MASK;
	bmap_bput(addr, b);
}


/* **** Address banks **** */
#if 0 /* unused */
static addrbank dummy_bank =
{
	dummy_lget, dummy_wget, dummy_bget,
	dummy_lput, dummy_wput, dummy_bput
};
#endif
static addrbank BusErrMem_bank =
{
	BusErrMem_lget, BusErrMem_wget, BusErrMem_bget,
	BusErrMem_lput, BusErrMem_wput, BusErrMem_bput
};

static addrbank ROM_bank =
{
	mem_rom_lget, mem_rom_wget, mem_rom_bget,
	mem_rom_lput, mem_rom_wput, mem_rom_bput
};

static addrbank RAM_bank0 =
{
	mem_ram_bank0_lget, mem_ram_bank0_wget, mem_ram_bank0_bget,
	mem_ram_bank0_lput, mem_ram_bank0_wput, mem_ram_bank0_bput
};

static addrbank RAM_bank1 =
{
	mem_ram_bank1_lget, mem_ram_bank1_wget, mem_ram_bank1_bget,
	mem_ram_bank1_lput, mem_ram_bank1_wput, mem_ram_bank1_bput
};

static addrbank RAM_bank2 =
{
	mem_ram_bank2_lget, mem_ram_bank2_wget, mem_ram_bank2_bget,
	mem_ram_bank2_lput, mem_ram_bank2_wput, mem_ram_bank2_bput
};

static addrbank RAM_bank3 =
{
	mem_ram_bank3_lget, mem_ram_bank3_wget, mem_ram_bank3_bget,
	mem_ram_bank3_lput, mem_ram_bank3_wput, mem_ram_bank3_bput
};

static addrbank RAM_empty_bank =
{
	mem_ram_empty_lget, mem_ram_empty_wget, mem_ram_empty_bget,
	mem_ram_empty_lput, mem_ram_empty_wput, mem_ram_empty_bput
};

static addrbank RAM_mwf_bank =
{
	mem_ram_mwf_lget, mem_ram_mwf_wget, mem_ram_mwf_bget,
	mem_ram_mwf_lput, mem_ram_mwf_wput, mem_ram_mwf_bput
};

static addrbank VRAM_bank =
{
	mem_video_lget, mem_video_wget, mem_video_bget,
	mem_video_lput, mem_video_wput, mem_video_bput
};

static addrbank VRAM_mwf_bank =
{
	mem_video_mwf_lget, mem_video_mwf_wget, mem_video_mwf_bget,
	mem_video_mwf_lput, mem_video_mwf_wput, mem_video_mwf_bput
};

static addrbank VRAM_color_bank =
{
	mem_color_video_lget, mem_color_video_wget, mem_color_video_bget,
	mem_color_video_lput, mem_color_video_wput, mem_color_video_bput
};

static addrbank IO_bank =
{
	IoMem_lget, IoMem_wget, IoMem_bget,
	IoMem_lput, IoMem_wput, IoMem_bput
};

static addrbank BMAP_bank =
{
	mem_bmap_lget, mem_bmap_wget, mem_bmap_bget,
	mem_bmap_lput, mem_bmap_wput, mem_bmap_bput
};

static addrbank TMC_bank =
{
	tmc_lget, tmc_wget, tmc_bget,
	tmc_lput, tmc_wput, tmc_bput
};

static addrbank NCC_bank =
{
	ncc_lget, dummy_wget, dummy_bget,
	ncc_lput, dummy_wput, dummy_bput
};

static addrbank NCC_cache_bank =
{
	ncc_cache_lget, ncc_cache_wget, ncc_cache_bget,
	ncc_cache_lput, ncc_cache_wput, ncc_cache_bput
};

static addrbank NCC_tag_bank =
{
	ncc_tag_lget, dummy_wget, dummy_bget,
	ncc_tag_lput, dummy_wput, dummy_bput
};

static addrbank NBIC_bank =
{
	nbic_reg_lget, nbic_reg_wget, nbic_reg_bget,
	nbic_reg_lput, nbic_reg_wput, nbic_reg_bput
};

static addrbank NEXTBUS_slot_bank =
{
	nextbus_slot_lget, nextbus_slot_wget, nextbus_slot_bget,
	nextbus_slot_lput, nextbus_slot_wput, nextbus_slot_bput
};

static addrbank NEXTBUS_board_bank =
{
	nextbus_board_lget, nextbus_board_wget, nextbus_board_bget,
	nextbus_board_lput, nextbus_board_wput, nextbus_board_bput
};



static void init_mem_banks (void)
{
	uae_u32 i;
	for (i = 0; i < 65536; i++) {
		put_mem_bank (bank_lget, i<<16, BusErrMem_bank.lget);
		put_mem_bank (bank_wget, i<<16, BusErrMem_bank.wget);
		put_mem_bank (bank_bget, i<<16, BusErrMem_bank.bget);
		put_mem_bank (bank_lput, i<<16, BusErrMem_bank.lput);
		put_mem_bank (bank_wput, i<<16, BusErrMem_bank.wput);
		put_mem_bank (bank_bput, i<<16, BusErrMem_bank.bput);
	}
}

/* 
 * Arrays are ordered by access probability from profiling data.
 * Keep them in this order for cache locality.
 */
mem_get_func bank_wget[65536];
mem_get_func bank_lget[65536];

mem_put_func bank_wput[65536];
mem_put_func bank_lput[65536];

mem_get_func bank_bget[65536];
mem_put_func bank_bput[65536];

/*
 * Initialize the memory banks
 */
int memory_init (void)
{
	int i;
	
	uae_u32 bankstart[4];
	uae_u32 banksize[4];
	
	int ram_size;
	int vram_size;
	
	write_log("Memory init: Memory size: %iMB\n", Configuration_CheckMemory(ConfigureParams.Memory.nMemoryBankSize));
	
	/* Set machine dependent variables */
	if (ConfigureParams.System.bTurbo) {
		next_ram_bank_size = NEXT_RAM_BANK_SIZE_T;
		next_ram_bank_mask = NEXT_RAM_BANK_SEL_T;
		vram_size = ConfigureParams.System.bColor ? NEXT_VRAM_COLOR_ALLOC : NEXT_VRAM_ALLOC;
		ram_size  = NEXT_RAM_ALLOC_T;
	} else if (ConfigureParams.System.bColor) {
		next_ram_bank_size = NEXT_RAM_BANK_SIZE_C;
		next_ram_bank_mask = NEXT_RAM_BANK_SEL_C;
		vram_size = NEXT_VRAM_COLOR_ALLOC;
		ram_size  = NEXT_RAM_ALLOC_C;
	} else {
		next_ram_bank_size = NEXT_RAM_BANK_SIZE;
		next_ram_bank_mask = NEXT_RAM_BANK_SEL;
		vram_size = NEXT_VRAM_ALLOC;
		ram_size  = NEXT_RAM_ALLOC;
	}
	
	/* Free memory in case it has been allocated already */
	memory_uninit();
	
	/* Allocate memory */
	NEXTRam   = malloc_aligned(ram_size);
	NEXTVideo = malloc_aligned(vram_size);
	NEXTIo    = malloc_aligned(NEXT_IO_ALLOC);
	NEXTRom   = malloc_aligned(NEXT_EPROM_ALLOC);
	
	/* Check if memory allocation was successful */
	if (!(NEXTRom && NEXTVideo && NEXTRam && NEXTIo)) {
		write_log("Memory init: Cannot allocate memory\n");
		return 1;
	}
	
	/* Initialise memory */
	memset(NEXTRom, 0, NEXT_EPROM_ALLOC);
	memset(NEXTVideo, 0, vram_size);
	memset(NEXTRam, 0, ram_size);
	memset(NEXTIo, 0, NEXT_IO_ALLOC);
	
	/* Load ROM file */
	if (rom_load(NEXTRom, NEXT_EPROM_ALLOC)) {
		write_log("Memory init: Cannot load ROM\n");
		return 1;
	}
	
	/* Set I/O access functions */
	IoMem_Init();
	
	/* Get base address and size for each memory bank */
	for (i = 0; i < N_BANKS; i++) {
		bankstart[i] = NEXT_RAM_START + (next_ram_bank_size * i);
		banksize[i]  = ConfigureParams.Memory.nMemoryBankSize[i] << 20;
	}
	
	/* Fill every 65536 bank with dummy */
	init_mem_banks();
	
	/* Map ROM */
	map_banks(&ROM_bank, NEXT_EPROM_START>>16, NEXT_EPROM_SIZE>>16);
	write_log("Mapping ROM at $%08x: %ikB\n", NEXT_EPROM_START, NEXT_EPROM_ALLOC>>10);
	if (ConfigureParams.System.nMachineType != NEXT_CUBE030) {
		map_banks(&ROM_bank, NEXT_EPROM_BMAP_START>>16, NEXT_EPROM_SIZE>>16);
		write_log("Mapping ROM through BMAP at $%08x: %ikB\n", NEXT_EPROM_BMAP_START, NEXT_EPROM_ALLOC>>10);
	}
	
	/* Map main memory */
	if (banksize[0]) {
		next_ram_bank0_mask = next_ram_bank_mask|(banksize[0]-1);
		map_banks(&RAM_bank0, bankstart[0]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank0 at $%08x: %iMB\n", bankstart[0], banksize[0]>>20);
	} else {
		next_ram_bank0_mask = 0;
		map_banks(&RAM_empty_bank, bankstart[0]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank0 at $%08x: empty\n", bankstart[0]);
	}
	
	if (banksize[1]) {
		next_ram_bank1_mask = next_ram_bank_mask|(banksize[1]-1);
		map_banks(&RAM_bank1, bankstart[1]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank1 at $%08x: %iMB\n", bankstart[1], banksize[1]>>20);
	} else {
		next_ram_bank1_mask = 0;
		map_banks(&RAM_empty_bank, bankstart[1]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank1 at $%08x: empty\n", bankstart[1]);
	}
	
	if (banksize[2]) {
		next_ram_bank2_mask = next_ram_bank_mask|(banksize[2]-1);
		map_banks(&RAM_bank2, bankstart[2]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank2 at $%08x: %iMB\n", bankstart[2], banksize[2]>>20);
	} else {
		next_ram_bank2_mask = 0;
		map_banks(&RAM_empty_bank, bankstart[2]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank2 at $%08x: empty\n", bankstart[2]);
	}
	
	if (banksize[3]) {
		next_ram_bank3_mask = next_ram_bank_mask|(banksize[3]-1);
		map_banks(&RAM_bank3, bankstart[3]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank3 at $%08x: %iMB\n", bankstart[3], banksize[3]>>20);
	} else {
		next_ram_bank3_mask = 0;
		map_banks(&RAM_empty_bank, bankstart[3]>>16, next_ram_bank_size>>16);
		write_log("Mapping main memory bank3 at $%08x: empty\n", bankstart[3]);
	}
	
	/* Map mirrors of main memory for memory write functions */
	if (!ConfigureParams.System.bColor && !ConfigureParams.System.bTurbo) {
		map_banks(&RAM_mwf_bank, NEXT_RAM_MWF0_START>>16, NEXT_RAM_SIZE>>16);
		map_banks(&RAM_mwf_bank, NEXT_RAM_MWF1_START>>16, NEXT_RAM_SIZE>>16);
		map_banks(&RAM_mwf_bank, NEXT_RAM_MWF2_START>>16, NEXT_RAM_SIZE>>16);
		map_banks(&RAM_mwf_bank, NEXT_RAM_MWF3_START>>16, NEXT_RAM_SIZE>>16);
		write_log("Mapping mirrors of main memory for memory write functions:\n");
		for (i = 0; i < 4; i++) {
			write_log("Function%i at $%08x\n",i,NEXT_RAM_MWF0_START+NEXT_RAM_SIZE*i);
		}
	}
	
	/* Map video memory */
	if (ConfigureParams.System.bTurbo && ConfigureParams.System.bColor) {
		map_banks(&VRAM_color_bank, NEXT_VRAM_TURBO_START>>16, NEXT_VRAM_COLOR_SIZE>>16);
		write_log("Mapping video memory at $%08x: %ikB\n", NEXT_VRAM_TURBO_START, NEXT_VRAM_COLOR_ALLOC>>10);
	} else if (ConfigureParams.System.bTurbo) {
		map_banks(&VRAM_bank, NEXT_VRAM_TURBO_START>>16, NEXT_VRAM_SIZE>>16);
		write_log("Mapping video memory at $%08x: %ikB\n", NEXT_VRAM_TURBO_START, NEXT_VRAM_ALLOC>>10);
	} else if (ConfigureParams.System.bColor) {
		map_banks(&VRAM_color_bank, NEXT_VRAM_COLOR_START>>16, NEXT_VRAM_COLOR_SIZE>>16);
		write_log("Mapping video memory at $%08x: %ikB\n", NEXT_VRAM_COLOR_START, NEXT_VRAM_COLOR_ALLOC>>10);
	} else {
		map_banks(&VRAM_bank, NEXT_VRAM_START>>16, NEXT_VRAM_SIZE>>16);
		write_log("Mapping video memory at $%08x: %ikB\n", NEXT_VRAM_START, NEXT_VRAM_ALLOC>>10);
		
		map_banks(&VRAM_mwf_bank, NEXT_VRAM_MWF0_START>>16, NEXT_VRAM_SIZE>>16);
		map_banks(&VRAM_mwf_bank, NEXT_VRAM_MWF1_START>>16, NEXT_VRAM_SIZE>>16);
		map_banks(&VRAM_mwf_bank, NEXT_VRAM_MWF2_START>>16, NEXT_VRAM_SIZE>>16);
		map_banks(&VRAM_mwf_bank, NEXT_VRAM_MWF3_START>>16, NEXT_VRAM_SIZE>>16);
		write_log("Mapping mirrors of video memory for memory write functions:\n");
		for (i = 0; i<4; i++) {
			write_log("Function%i at $%08x\n",i,NEXT_VRAM_MWF0_START+NEXT_VRAM_SIZE*i);
		}
	}
	
	/* Map device space */
	map_banks(&IO_bank, NEXT_IO_START>>16, NEXT_IO_SIZE>>16);
	write_log("Mapping device space at $%08x\n", NEXT_IO_START);
	
	if (ConfigureParams.System.nMachineType != NEXT_CUBE030) {
		map_banks(&IO_bank, NEXT_IO_BMAP_START>>16, NEXT_IO_SIZE>>16);
		if (!ConfigureParams.System.bTurbo) {
			map_banks(&BMAP_bank, NEXT_BMAP_START>>16, NEXT_BMAP_SIZE>>16);
			map_banks(&BMAP_bank, NEXT_BMAP_START2>>16, NEXT_BMAP_SIZE>>16);
			write_log("Mapping BMAP device space at $%08x\n", NEXT_IO_BMAP_START);
		} else {
			write_log("Mapping device space at $%08x\n", NEXT_IO_BMAP_START);
		}
	}
	
	if (ConfigureParams.System.bTurbo) {
		map_banks(&TMC_bank, NEXT_TMC_START>>16, NEXT_TMC_SIZE>>16);
		write_log("Mapping TMC device space at $%08x\n", NEXT_TMC_START);
		
		if (ConfigureParams.System.nCpuFreq==40) {
			map_banks(&NCC_bank, NEXT_NCC_START>>16, NEXT_NCC_SIZE>>16);
			write_log("Mapping cache controller at $%08x\n", NEXT_NCC_START);
			map_banks(&NCC_cache_bank, NEXT_CACHE_START>>16, NEXT_CACHE_SIZE>>16);
			write_log("Mapping cache memory at $%08x\n", NEXT_CACHE_START);
			map_banks(&NCC_tag_bank, NEXT_CACHE_TAG_START>>16, NEXT_CACHE_TAG_SIZE>>16);
			write_log("Mapping cache tag memory at $%08x\n", NEXT_CACHE_TAG_START);
		}
	}
	
	/* Map NBIC and board spaces via NextBus */
	if (ConfigureParams.System.nMachineType!=NEXT_STATION && ConfigureParams.System.bNBIC) {
		if (!ConfigureParams.System.bTurbo) {
			map_banks(&NBIC_bank, NEXT_NBIC_START>>16, NEXT_NBIC_SIZE>>16);
			write_log("Mapping NextBus interface chip at $%08x\n", NEXT_NBIC_START);
		}
		for (i = 2; i < 8; i++) {
			map_banks(&NEXTBUS_board_bank, NEXTBUS_BOARD_START(i)>>16, NEXTBUS_BOARD_SIZE>>16);
			write_log("Mapping NextBus board memory for slot %i at $%08x\n", i, NEXTBUS_BOARD_START(i));
		}
		for (i = 0; i < 16; i++) {
			map_banks(&NEXTBUS_slot_bank, NEXTBUS_SLOT_START(i)>>16, NEXTBUS_SLOT_SIZE>>16);
		}
		write_log("Mapping NextBus slot memory at $%08x\n", NEXTBUS_SLOT_START(i));
	}
	
	/* Initialise boards on the NextBus */
	nextbus_init();
	
	return 0;
}


/*
 * Uninitialize the memory banks.
 */
void memory_uninit (void)
{
	nextbus_uninit();
	
	free(NEXTRom);
	free(NEXTVideo);
	free(NEXTRam);
	free(NEXTIo);
	
	NEXTRom = NEXTVideo = NEXTRam = NEXTIo = NULL;
}


void map_banks (addrbank *bank, uae_u32 start, uae_u32 size) {
	uae_u32 bnr;
	
	for (bnr = start; bnr < start + size; bnr++) {
		put_mem_bank (bank_lget, bnr << 16, bank->lget);
		put_mem_bank (bank_wget, bnr << 16, bank->wget);
		put_mem_bank (bank_bget, bnr << 16, bank->bget);
		put_mem_bank (bank_lput, bnr << 16, bank->lput);
		put_mem_bank (bank_wput, bnr << 16, bank->wput);
		put_mem_bank (bank_bput, bnr << 16, bank->bput);
	}
}
