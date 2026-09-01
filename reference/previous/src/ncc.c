/*
  Previous - ncc.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the Nitro Cache Controller (NCC)
  with cache. (dummy)
*/
const char Ncc_fileid[] = "Previous ncc.c";

#include "main.h"
#include "m68000.h"
#include "ncc.h"

#define LOG_NCC_LEVEL LOG_DEBUG
#define LOG_TAG_LEVEL LOG_DEBUG

/* NeXT Nitro Cache Controller emulation */

#define NCC_CACHE_SIZE (128*1024)
#define NCC_CACHE_MASK (NCC_CACHE_SIZE-1)

#define NCC_TAG_SIZE   (NCC_CACHE_SIZE>>5)
#define NCC_TAG_MASK   (NCC_TAG_SIZE-1)

static struct {
	uint8_t  cache[NCC_CACHE_SIZE];
	uint32_t tag[NCC_TAG_SIZE];
	uint32_t ccr;
} ncc;

/* Nitro Cache Control Register:
 * -------- -------- -------- -------x  bit  0    --> cache enable
 * -------- -------- -------- ------x-  bit  1    --> sync
 * -------- -------- -------- -----x--  bit  2    --> tag speed
 * -------- -------- -------- ---xx---  bit  3:4  --> cache size
 * -------- -------- -------- xxx-----  bit  5:7  --> revision
 * -------- -------- -------x --------  bit  8    --> compare
 * ----xxxx xxxxxxxx xxxxxxx- --------  bit  9:27 --> reserved
 * xxxx---- -------- -------- --------  bit 28:31 --> slot id
 *
 * cache enable:    0 = disabled, 1 = enabled
 * sync:            0 = asynchronous, 1 = synchronous
 * tag speed:       0 = 3,1,1,1, 1 = 2,1,1,1
 * cache size:      0 = 64K, 1 = 128K, 2 = 256K, 3 = 512K
 * revision:        0 = first revision
 * compare:         0 = internal comparator, 1 = match signal
 */

#define NCC_CCR_ENABLE   0x00000001
#define NCC_CCR_SYNC     0x00000002
#define NCC_CCR_SPEED    0x00000004
#define NCC_CCR_SIZE     0x00000018
#define NCC_CCR_REV      0x000000E0
#define NCC_CCR_COMPARE  0x00000100
#define NCC_CCR_SID      0xF0000000

#define NCC_CCR_READABLE 0xF00001FF
#define NCC_CCR_WRITABLE 0x0000011F


/* Register read/write functions */
uae_u32 ncc_lget(uaecptr addr) {
	uae_u32 val = ncc.ccr&NCC_CCR_READABLE;
	
	Log_Printf(LOG_WARN, "[NCC] CCR read %08X at $%08X", val, addr);

	if (addr & 3) {
		Log_Printf(LOG_WARN, "[NCC] Unaligned access.");
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}
	
	return val;
}

void ncc_lput(uaecptr addr, uae_u32 val) {
	Log_Printf(LOG_WARN, "[NCC] CCR write %08X at $%08X", val, addr);

	if (addr & 3) {
		Log_Printf(LOG_WARN, "[NCC] Unaligned access.");
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, val);
	}
	
	val &= NCC_CCR_WRITABLE;
	
	if (ncc.ccr ^ val) {
		ncc.ccr = val;
		Log_Printf(LOG_WARN,      "[NCC] Cache %sabled", (ncc.ccr&NCC_CCR_ENABLE)?"en":"dis");
		Log_Printf(LOG_WARN,      "[NCC] Cache size: %dkB", 64<<((ncc.ccr&NCC_CCR_SIZE)>>3));
		Log_Printf(LOG_NCC_LEVEL, "[NCC] %synchronous mode", (ncc.ccr&NCC_CCR_SYNC)?"S":"As");
		Log_Printf(LOG_NCC_LEVEL, "[NCC] Tag speed: %d,1,1,1", (ncc.ccr&NCC_CCR_SPEED)?2:3);
		Log_Printf(LOG_NCC_LEVEL, "[NCC] Compare using %s", (ncc.ccr&NCC_CCR_COMPARE)?"match signal":"internal comparator");
	}
}


/* Nitro Cache Tag:
 *
 * -------- -------- -------- ------xx  bit 1:0   --> valid
 * -------- -------- xxxxxxxx xxxxxx--  bit 15:2  --> reserved
 * xxxxxxxx xxxxxxxx -------- --------  bit 16:31 --> tag
 *
 * valid:           bit 0: valid bit for subblock 0 (bit 4 of address is 0)
 *                  bit 1: valid bit for subblock 1 (bit 4 of address is 1)
 *
 * tag:             cache size:   bits compared:  index bits:
 *                   64K          16:31           5:15
 *                  128K          17:31           5:16
 *                  256K          18:31           5:17
 *                  512K          19:31           5:18
 *
 * Example physical address decode for 128K:
 * -------- -------- -------- ---x----  bit 4     --> subblock select
 * -------- -------x xxxxxxxx xxx-----  bit 5:16  --> index
 * xxxxxxxx xxxxxxx- -------- --------  bit 17:31 --> compare
 */

#define NCC_TAG_VALID0   0x00000001
#define NCC_TAG_VALID1   0x00000002

#define NCC_TAG_READABLE 0xFFFF0003
#define NCC_TAG_WRITABLE 0xFFFF0003


/* Cache tag read/write functions */
uae_u32 ncc_tag_lget(uaecptr addr) {
	Log_Printf(LOG_TAG_LEVEL, "[NCC] Tag read at $%08X", addr);
	
	addr = (addr >> 5) & NCC_TAG_MASK;
	return ncc.tag[addr] & NCC_TAG_READABLE;
}

void ncc_tag_lput(uaecptr addr, uae_u32 val) {
	Log_Printf(LOG_TAG_LEVEL, "[NCC] Tag write %08X at $%08X", val, addr);
	
	addr = (addr >> 5) & NCC_TAG_MASK;
	ncc.tag[addr] = val & NCC_TAG_WRITABLE;
}


/* Cache read/write functions */
uae_u32 ncc_cache_lget(uaecptr addr) {
	addr &= NCC_CACHE_MASK;

	return do_get_mem_long(ncc.cache + addr);
}

uae_u32 ncc_cache_wget(uaecptr addr) {
	addr &= NCC_CACHE_MASK;

	return do_get_mem_word(ncc.cache + addr);
}

uae_u32 ncc_cache_bget(uaecptr addr) {
	addr &= NCC_CACHE_MASK;

	return do_get_mem_byte(ncc.cache + addr);
}

void ncc_cache_lput(uaecptr addr, uae_u32 l) {
	addr &= NCC_CACHE_MASK;
	
	do_put_mem_long(ncc.cache + addr, l);
}

void ncc_cache_wput(uaecptr addr, uae_u32 w) {
	addr &= NCC_CACHE_MASK;

	do_put_mem_word(ncc.cache + addr, w);
}

void ncc_cache_bput(uaecptr addr, uae_u32 b) {
	addr &= NCC_CACHE_MASK;

	do_put_mem_byte(ncc.cache + addr, b);
}


/* NCC reset function */
void NCC_Reset(void) {
	ncc.ccr = 0;
	memset(ncc.cache, 0, sizeof(ncc.cache));
	memset(ncc.tag, 0, sizeof(ncc.tag));
}
