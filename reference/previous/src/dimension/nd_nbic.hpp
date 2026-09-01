/*
  Previous - nd_nbic.hpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef __ND_NBIC_H__
#define __ND_NBIC_H__

#define ND_NBIC_ID        0xC0000001

#ifdef __cplusplus

class NBIC {
    int    slot;
  //  uint32_t control; // unused
    uint32_t id;
    uint8_t  intstatus;
    uint8_t  intmask;
public:
    static volatile uint32_t remInter;
    static volatile uint32_t remInterMask;

    NBIC(int slot, int id);
    
    uint8_t read(int addr);
    void  write(int addr, uint8_t val);
    
    uint32_t lget(uint32_t addr);
    uint16_t wget(uint32_t addr);
    uint8_t  bget(uint32_t addr);
    void   lput(uint32_t addr, uint32_t l);
    void   wput(uint32_t addr, uint16_t w);
    void   bput(uint32_t addr, uint8_t b);
    
    void   init(void);
    void   set_intstatus(bool set);
};

extern "C" {
#endif /* __cplusplus */
    void   nd_nbic_interrupt(void);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __ND_NBIC_H__ */
