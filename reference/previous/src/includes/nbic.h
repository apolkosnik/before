/*
  Previous - nbic.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_NBIC_H
#define PREV_NBIC_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

uae_u32 nbic_reg_lget(uaecptr addr);
uae_u32 nbic_reg_wget(uaecptr addr);
uae_u32 nbic_reg_bget(uaecptr addr);
void nbic_reg_lput(uaecptr addr, uae_u32 val);
void nbic_reg_wput(uaecptr addr, uae_u32 val);
void nbic_reg_bput(uaecptr addr, uae_u32 val);

uint32_t nb_cpu_slot_lget(uint32_t addr);
uint16_t nb_cpu_slot_wget(uint32_t addr);
uint8_t  nb_cpu_slot_bget(uint32_t addr);
void nb_cpu_slot_lput(uint32_t addr, uint32_t l);
void nb_cpu_slot_wput(uint32_t addr, uint16_t w);
void nb_cpu_slot_bput(uint32_t addr, uint8_t  b);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_NBIC_H */
