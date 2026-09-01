/*
  Previous - tmc.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_TMC_H
#define PREV_TMC_H

extern uae_u32 tmc_lget(uaecptr addr);
extern uae_u32 tmc_wget(uaecptr addr);
extern uae_u32 tmc_bget(uaecptr addr);

extern void tmc_lput(uaecptr addr, uae_u32 l);
extern void tmc_wput(uaecptr addr, uae_u32 w);
extern void tmc_bput(uaecptr addr, uae_u32 b);

extern bool tmc_video_enabled(void);
extern void tmc_video_interrupt(void);

extern void TMC_Reset(void);

#endif /* PREV_TMC_H */
