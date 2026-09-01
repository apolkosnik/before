/*
  Previous - ncc.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_NCC_H
#define PREV_NCC_H

extern uae_u32 ncc_lget(uaecptr addr);
extern void ncc_lput(uaecptr addr, uae_u32 val);

extern uae_u32 ncc_tag_lget(uaecptr addr);
extern void ncc_tag_lput(uaecptr addr, uae_u32 val);

extern uae_u32 ncc_cache_lget(uaecptr addr);
extern uae_u32 ncc_cache_wget(uaecptr addr);
extern uae_u32 ncc_cache_bget(uaecptr addr);

extern void ncc_cache_lput(uaecptr addr, uae_u32 l);
extern void ncc_cache_wput(uaecptr addr, uae_u32 w);
extern void ncc_cache_bput(uaecptr addr, uae_u32 b);

extern void NCC_Reset(void);

#endif /* PREV_NCC_H */
