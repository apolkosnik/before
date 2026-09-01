/*
  Previous - bmap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_BMAP_H
#define PREV_BMAP_H

extern uae_u32 bmap_lget(uaecptr addr);
extern uae_u32 bmap_wget(uaecptr addr);
extern uae_u32 bmap_bget(uaecptr addr);

extern void bmap_lput(uaecptr addr, uae_u32 l);
extern void bmap_wput(uaecptr addr, uae_u32 w);
extern void bmap_bput(uaecptr addr, uae_u32 b);

extern int bmap_rom_local;
extern int bmap_tpe_select;
extern int bmap_hreq_enable;
extern int bmap_txdn_enable;

extern void BMAP_Reset(void);

#endif /* PREV_BMAP_H */
