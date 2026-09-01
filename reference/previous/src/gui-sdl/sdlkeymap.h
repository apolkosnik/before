/*
  Previous - sdlkeymap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SDLKEYMAP_H
#define PREV_SDLKEYMAP_H

#include <SDL3/SDL.h>

extern void Keymap_MouseWheel(const SDL_MouseWheelEvent *sdlwheel);
extern void Keymap_MouseMove(const SDL_MouseMotionEvent *sdlmotion);
extern void Keymap_MouseDown(bool left);
extern void Keymap_MouseUp(bool left);

extern void Keymap_KeyDown(const SDL_KeyboardEvent *sdlkey);
extern void Keymap_KeyUp(const SDL_KeyboardEvent *sdlkey);

#endif /* PREV_SDLKEYMAP_H */
