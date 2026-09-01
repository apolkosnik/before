/*
  Previous - sdlstatusbar.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef PREV_SDLSTATUSBAR_H
#define PREV_SDLSTATUSBAR_H

#include <SDL3/SDL.h>

extern int Statusbar_SetHeight(int ScreenWidth, int ScreenHeight);
extern int Statusbar_GetHeight(void);

extern void Statusbar_Init(SDL_Surface *screen);
extern void Statusbar_Update(SDL_Surface *screen);

#endif /* PREV_SDLSTATUSBAR_H */
