/*
* UAE - The Un*x Amiga Emulator
*
* memory management
*
* Copyright 1995 Bernd Schmidt
*/

#ifndef UAE_MEMORY_H
#define UAE_MEMORY_H

#include "sysdeps.h"
#include "maccess.h"

#ifdef JIT
extern int special_mem;
extern int special_mem_default;
extern int jit_n_addr_unsafe;
#endif

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))

extern uae_u8* NEXTVideo;
extern uae_u8* NEXTRam;
extern uae_u8* NEXTRom;
extern uae_u8* NEXTIo;

typedef uae_u32 (*mem_get_func)(uaecptr) REGPARAM;
typedef void (*mem_put_func)(uaecptr, uae_u32) REGPARAM;

typedef struct addrbank {
	/* These ones should be self-explanatory... */
	mem_get_func lget, wget, bget;
	mem_put_func lput, wput, bput;
} addrbank;

#define bankindex(addr) (((uaecptr)(addr)) >> 16)

extern mem_get_func bank_lget[65536];
extern mem_get_func bank_wget[65536];
extern mem_get_func bank_bget[65536];

extern mem_put_func bank_lput[65536];
extern mem_put_func bank_wput[65536];
extern mem_put_func bank_bput[65536];

#define get_mem_bank(bank, addr)    (bank[bankindex(addr)])
#define put_mem_bank(bank, addr, b) (bank[bankindex(addr)] = (b))

int  memory_init(void);
void memory_uninit(void);
void map_banks(addrbank *bank, uae_u32 start, uae_u32 size);

#define get_long(addr)   (call_mem_get_func(get_mem_bank(bank_lget, addr), addr))
#define get_word(addr)   (call_mem_get_func(get_mem_bank(bank_wget, addr), addr))
#define get_byte(addr)   (call_mem_get_func(get_mem_bank(bank_bget, addr), addr))
#define get_longi(addr)  get_long(addr)
#define get_wordi(addr)  get_word(addr)
#define put_long(addr,l) (call_mem_put_func(get_mem_bank(bank_lput, addr), addr, l))
#define put_word(addr,w) (call_mem_put_func(get_mem_bank(bank_wput, addr), addr, w))
#define put_byte(addr,b) (call_mem_put_func(get_mem_bank(bank_bput, addr), addr, b))

#define CACHE_ENABLE_DATA 0x01
#define CACHE_ENABLE_DATA_BURST 0x02
#define CACHE_ENABLE_COPYBACK 0x020
#define CACHE_ENABLE_INS 0x80
#define CACHE_ENABLE_INS_BURST 0x40
#define CACHE_ENABLE_BOTH (CACHE_ENABLE_DATA | CACHE_ENABLE_INS)
#define CACHE_ENABLE_ALL (CACHE_ENABLE_BOTH | CACHE_ENABLE_INS_BURST | CACHE_ENABLE_DATA_BURST)
#define CACHE_DISABLE_ALLOCATE 0x08
#define CACHE_DISABLE_MMU 0x10
extern uae_u8 ce_banktype[65536], ce_cachable[65536];

#endif /* UAE_MEMORY_H */
