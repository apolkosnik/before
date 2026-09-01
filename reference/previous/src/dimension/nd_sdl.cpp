/*
  Previous - nd_sdl.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "nd_sdl.hpp"
#include "configuration.h"
#include "dimension.hpp"
#include "sdlscreen.h"
#include "screen.h"


#ifdef ENABLE_RENDERING_THREAD
NDSDL::NDSDL(int slot, uint32_t* vram) : slot(slot), vram(vram), ndWindow(NULL), ndRenderer(NULL), ndTexture(NULL), doRepaint(true), repaintThread(NULL) {}

int NDSDL::repainter(void *_this) {
    return ((NDSDL*)_this)->repainter();
}

int NDSDL::repainter(void) {
    host_thread_priority(1);

    while (doRepaint) {
        if (bEmulationActive && SDL_GetAtomicInt(&blitNDFB)) {
            repaint();
        } else {
            host_sleep_ms(100);
        }
    }

    return 0;
}
#else // !ENABLE_RENDERING_THREAD
NDSDL::NDSDL(int slot, uint32_t* vram) : slot(slot), vram(vram), ndWindow(NULL), ndRenderer(NULL), ndTexture(NULL) {}
#endif // !ENABLE_RENDERING_THREAD

void NDSDL::repaint(void) {
    if (nd_video_enabled(slot)) {
        Screen_BlitDimension(vram, ndTexture);
    } else {
        Screen_Blank(ndTexture);
    }
    SDL_RenderClear(ndRenderer);
    SDL_RenderTexture(ndRenderer, ndTexture, NULL, NULL);
    SDL_RenderPresent(ndRenderer);
}

void NDSDL::init(void) {
    int x, y, w, h;
    char name[32];

    if (ConfigureParams.Screen.nMode == SCREEN_ALL) {
        SDL_GetWindowPosition(sdlWindow, &x, &y);
        SDL_GetWindowSize(sdlWindow, &w, &h);
        h = (w * NeXT_SCRN_H) / NeXT_SCRN_W;

        if (!ndWindow) {
            snprintf(name, sizeof(name), "NeXTdimension (Slot %i)", slot);

            if (SDL_CreateWindowAndRenderer(name, w, h, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY, &ndWindow, &ndRenderer) == false) {
                fprintf(stderr,"[ND] Slot %i: Failed to create window and renderer! (%s)\n", slot, SDL_GetError());
                return;
            }
            SDL_SetWindowPosition(ndWindow, x+14*slot, y+14*slot);
            SDL_SetRenderLogicalPresentation(ndRenderer, NeXT_SCRN_W, NeXT_SCRN_H, SDL_LOGICAL_PRESENTATION_STRETCH);
            ndTexture = SDL_CreateTexture(ndRenderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, NeXT_SCRN_W, NeXT_SCRN_H);
            SDL_SetTextureBlendMode(ndTexture, SDL_BLENDMODE_NONE);
#ifdef ENABLE_RENDERING_THREAD
            SDL_SetRenderVSync(ndRenderer, 1);

            snprintf(name, sizeof(name), "[Previous] Screen at slot %d", slot);
            repaintThread = SDL_CreateThread(NDSDL::repainter, name, this);
#endif
        } else {
            SDL_SetWindowPosition(ndWindow, x+14*slot, y+14*slot);
            SDL_SetWindowSize(ndWindow, w, h);
        }

        titlebar(ConfigureParams.Screen.bShowTitlebar);

        SDL_ShowWindow(ndWindow);
#ifdef ENABLE_RENDERING_THREAD
        SDL_SetAtomicInt(&blitNDFB, 1);
#endif
    }
}

void NDSDL::uninit(void) {
#ifdef ENABLE_RENDERING_THREAD
    SDL_SetAtomicInt(&blitNDFB, 0);
#endif
    SDL_HideWindow(ndWindow);
}

void NDSDL::destroy(void) {
#ifdef ENABLE_RENDERING_THREAD
    doRepaint = false; // stop repaint thread
    int s;
    SDL_WaitThread(repaintThread, &s);
#endif
    SDL_DestroyTexture(ndTexture);
    SDL_DestroyRenderer(ndRenderer);
    SDL_DestroyWindow(ndWindow);
}

void NDSDL::resize(float scale) {
    if (ndWindow) {
        SDL_SetWindowSize(ndWindow, (int)SDL_lroundf(scale*NeXT_SCRN_W), (int)SDL_lroundf(scale*NeXT_SCRN_H));
    }
}

void NDSDL::titlebar(bool show) {
    if (ndWindow) {
        SDL_SetWindowBordered(ndWindow, show);
    }
}

#ifndef ENABLE_RENDERING_THREAD
void nd_sdl_repaint(void) {
    FOR_EACH_SLOT(slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            nd->sdl.repaint();
        }
    }
}
#endif

void nd_sdl_titlebar(bool show) {
    FOR_EACH_SLOT(slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            nd->sdl.titlebar(show);
        }
    }
}

void nd_sdl_resize(float scale) {
    FOR_EACH_SLOT(slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            nd->sdl.resize(scale);
        }
    }
}

void nd_sdl_show(void) {
    FOR_EACH_SLOT(slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            nd->sdl.init();
        }
    }
}

void nd_sdl_hide(void) {
    FOR_EACH_SLOT(slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            nd->sdl.uninit();
        }
    }
}
