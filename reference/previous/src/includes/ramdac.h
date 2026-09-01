/*
  Previous - ramdac.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_RAMDAC_H
#define PREV_RAMDAC_H

typedef struct {
    int      addr;
    int      idx;
    uint32_t wtt_tmp;
    uint32_t wtt[0x10];
    uint8_t  ccr[0xC];
    uint8_t  reg[0x30];
    uint8_t  ram[0x630];
} bt463;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern uint32_t bt463_bget(bt463* ramdac, uint32_t addr);
extern void     bt463_bput(bt463* ramdac, uint32_t addr, uint32_t b);

extern void RAMDAC_Read(void);
extern void RAMDAC_Write(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_RAMDAC_H */
