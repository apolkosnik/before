/*
  Previous - nd_mem.hpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef __ND_MEM_H__
#define __ND_MEM_H__

#include <sysdeps.h>
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* NeXTdimension board and slot memory */
#define ND_BOARD_SIZE   0x10000000
#define ND_BOARD_MASK   0x0FFFFFFF
#define ND_BOARD_BITS   0xF0000000
    
#define ND_SLOT_SIZE    0x01000000
#define ND_SLOT_MASK    0x00FFFFFF
#define ND_SLOT_BITS    0x0F000000
    
#define ND_NBIC_SPACE   0xFFFFFFE8

#define LOG_ND_MEM      LOG_NONE
    
typedef uae_u32 (*nd_mem_get_func)(int, uaecptr) REGPARAM;
typedef void (*nd_mem_put_func)(int, uaecptr, uae_u32) REGPARAM;

#define nd_bankindex(addr) (((uaecptr)(addr)) >> 16)
#define nd_put_mem_bank(addr, b) (mem_banks[nd_bankindex(addr)] = (b))

#ifdef __cplusplus
}

class ND_Addrbank {
protected:
    NextDimension* nd;
public:
    int flags;
    
    virtual uint32_t lget(uint32_t addr) const;
    virtual uint32_t wget(uint32_t addr) const;
    virtual uint32_t bget(uint32_t addr) const;
    virtual uint32_t cs8get(uint32_t addr) const;
    
    virtual void lput(uint32_t addr, uint32_t val) const;
    virtual void wput(uint32_t addr, uint32_t val) const;
    virtual void bput(uint32_t addr, uint32_t val) const;
    
    ND_Addrbank(NextDimension* nd);
    virtual ~ND_Addrbank();
};

#endif /* __cplusplus */

#endif /* __ND_MEM_H__ */
