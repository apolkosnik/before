/*
  Previous - sdlscreen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SDLSCREEN_H
#define PREV_SDLSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <SDL3/SDL.h>

extern SDL_Window  *sdlWindow;
extern SDL_Surface *sdlscrn;

extern void Screen_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
extern void Screen_UpdateRect(SDL_Surface *screen, int32_t x, int32_t y, int32_t w, int32_t h);
extern void Screen_BlitDimension(uint32_t* vram, SDL_Texture* tex);
extern void Screen_Blank(SDL_Texture* tex);

extern bool Screen_Repaint(void);
extern void Screen_SizeChanged(void);

extern void Screen_Init(void);
extern void Screen_UnInit(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_SDLSCREEN_H */
