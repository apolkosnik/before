/*
  Previous - sdlscreen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the SDL interface for video output.
*/
const char SDLscreen_fileid[] = "Previous sdlscreen.c";

#include "main.h"
#include "configuration.h"
#include "screen.h"
#include "sdlscreen.h"
#include "statusbar.h"
#include "sdlstatusbar.h"
#include "event.h"
#include "dimension.hpp"
#include "nd_sdl.hpp"
#include "video.h"
#include "keymap.h"
#include "m68000.h"


/* NeXT screen resolution */
const int NeXT_SCRN_W = 1120;
const int NeXT_SCRN_H = 832;

SDL_Window*   sdlWindow;
SDL_Surface*  sdlscrn = NULL;        /* The SDL screen surface */

/* extern for shortcuts */
volatile bool bGrabMouse    = false; /* Grab the mouse cursor in the window */
volatile bool bInFullScreen = false; /* true if in full screen */

/* extern for tablet */
int screen_w = NeXT_SCRN_W;
int screen_h = NeXT_SCRN_H;

static int width  = NeXT_SCRN_W;
static int height = NeXT_SCRN_H;

static SDL_Renderer* sdlRenderer;
static SDL_Texture*  uiTexture;
static SDL_Texture*  fbTexture;
static SDL_Texture*  groupTexture[NUM_MONITORS];
static SDL_FRect     uiRect;
static SDL_FRect     fbRect;
static SDL_FRect     groupRect[NUM_MONITORS];
static SDL_AtomicInt blitUI;
static SDL_Rect      statusBar;
static SDL_Rect      saveWindowBounds; /* Window bounds before going fullscreen. Used to restore window size & position. */
static SCREENMODE    saveScreenMode;   /* Save screen mode to restore on return from fullscreen */
static SCREENMODE    initScreenMode;   /* Save screen mode present at last init */
static int           initScreenWidth;  /* Save screen width at last init */
static int           initScreenHeight; /* Save screen height at last init */
static uint32_t      mask;             /* green screen mask for transparent UI areas */
static void*         uiBuffer;         /* uiBuffer used for user interface texture */
static SDL_SpinLock  uiBufferLock;     /* Lock for concurrent access to UI buffer between m68k thread and repainter */
#ifdef ENABLE_RENDERING_THREAD
static volatile bool doRepaint;        /* Repaint thread runs while true */
static SDL_Thread*   repaintThread;
#endif


static uint32_t BW2RGB[0x100][4];
static uint32_t COL2RGB[0x10000];

static uint32_t bw2rgb(SDL_Surface* surf, int bw) {
	int c = (~bw & 3) * 0x55;
	return SDL_MapSurfaceRGB(surf, c, c, c);
}

static uint32_t col2rgb(SDL_Surface* surf, int col) {
	int r = ((col >> 12) & 0x0F) * 0x11;
	int g = ((col >>  8) & 0x0F) * 0x11;
	int b = ((col >>  4) & 0x0F) * 0x11;
	return SDL_MapSurfaceRGB(surf, r, g, b);
}

/*
 BW format is 2 bit per pixel
 */
static void blitBW(SDL_Texture* tex) {
	void* pixels;
	uint8_t* src;
	uint8_t* dst;
	int pitch, src_padding, dst_padding, x, y;

	SDL_LockTexture(tex, NULL, &pixels, &pitch);
	src = NEXTVideo;
	dst = (uint8_t*)pixels;
	src_padding = ConfigureParams.System.bTurbo ? 0 : (32 / 4);
	dst_padding = pitch - NeXT_SCRN_W * 4;
	for (y = 0; y < NeXT_SCRN_H; y++) {
		for (x = 0; x < NeXT_SCRN_W / 4; x++) {
			memcpy(dst, BW2RGB[*src++], 16);
			dst += 16;
		}
		src += src_padding;
		dst += dst_padding;
	}
	SDL_UnlockTexture(tex);
}

/*
 Color format is 4 bit per pixel, big-endian: RGBX
 */
static void blitColor(SDL_Texture* tex) {
	void* pixels;
	uint16_t* src;
	uint32_t* dst;
	int pitch, src_padding, dst_padding, x, y;

	SDL_LockTexture(tex, NULL, &pixels, &pitch);
	src = (uint16_t*)NEXTVideo;
	dst = (uint32_t*)pixels;
	src_padding = ConfigureParams.System.bTurbo ? 0 : 32;
	dst_padding = pitch / 4 - NeXT_SCRN_W;
	for (y = 0; y < NeXT_SCRN_H; y++) {
		for (x = 0; x < NeXT_SCRN_W; x++) {
			*dst++ = COL2RGB[*src++];
		}
		src += src_padding;
		dst += dst_padding;
	}
	SDL_UnlockTexture(tex);
}

/*
 Dimension format is 8 bit per pixel, big-endian: BBGGRRAA
 */
void Screen_BlitDimension(uint32_t* vram, SDL_Texture* tex) {
	void* src;
	void* dst;
	int src_pitch, dst_pitch;
	SDL_PixelFormat src_format, dst_format;

#if ND_STEP
	src = &vram[0];
#else
	src = &vram[4];
#endif
	src_pitch  = (NeXT_SCRN_W + 32) * 4;
	src_format = SDL_PIXELFORMAT_BGRA32;
	dst_format = tex->format;

	SDL_LockTexture(tex, NULL, &dst, &dst_pitch);
	SDL_ConvertPixels(NeXT_SCRN_W, NeXT_SCRN_H, src_format, src, src_pitch, dst_format, dst, dst_pitch);
	SDL_UnlockTexture(tex);
}

/*
 Blank screen
 */
void Screen_Blank(SDL_Texture* tex) {
	void* pixels;
	int   pitch;
	SDL_LockTexture(tex, NULL, &pixels, &pitch);
	SDL_memset4(pixels, COL2RGB[0], pitch * NeXT_SCRN_H / 4);
	SDL_UnlockTexture(tex);
}

/*
 Blit NeXT framebuffer to texture.
 */
static bool blitScreen(int slot, SDL_Texture* tex) {
	if (slot > 0) {
		uint32_t* vram = nd_vram_for_slot(slot);
		if (vram) {
			if (nd_video_enabled(slot)) {
				Screen_BlitDimension(vram, tex);
			} else {
				Screen_Blank(tex);
			}
			return true;
		}
	} else {
		if (NEXTVideo) {
			if (Video_Enabled()) {
				if (ConfigureParams.System.bColor) {
					blitColor(tex);
				} else {
					blitBW(tex);
				}
			} else {
				Screen_Blank(tex);
			}
			return true;
		}
	}
	return false;
}

/*
 Blit user interface to texture.
 */
static void blitUserInterface(SDL_Texture* tex) {
	void* pixels;
	int   pitch;
	SDL_LockTexture(tex, NULL, &pixels, &pitch);
	SDL_LockSpinlock(&uiBufferLock);
	memcpy(pixels, uiBuffer, height * pitch);
	SDL_SetAtomicInt(&blitUI, 0);
	SDL_UnlockSpinlock(&uiBufferLock);
	SDL_UnlockTexture(tex);
}

/*
 Blits the NeXT framebuffer to the fbTexture, blends with the GUI surface and shows it.
 */
static bool Screen_SingleRepaint(void) {
	bool updateScreen = false;

	/* Blit the NeXT framebuffer to texture */
	if (bEmulationActive) {
		updateScreen = blitScreen(ConfigureParams.Screen.nSingleModeSlot, fbTexture);
	}

	/* Copy UI surface to texture */
	if (SDL_GetAtomicInt(&blitUI)) {
		blitUserInterface(uiTexture);
		updateScreen = true;
	}

	if (updateScreen) {
		SDL_RenderClear(sdlRenderer);
		/* Render NeXT framebuffer texture */
		SDL_RenderTexture(sdlRenderer, fbTexture, NULL, &fbRect);
		SDL_RenderTexture(sdlRenderer, uiTexture, NULL, &uiRect);
		/* Sleeps until next VSYNC if enabled in ScreenInit */
		SDL_RenderPresent(sdlRenderer);
	}

	return updateScreen;
}

static bool Screen_GroupRepaint(void) {
	bool updateScreen = false;
	int i;
	
	/* Blit the NeXT framebuffer to texture */
	if (bEmulationActive) {
		for (i = 0; i < NUM_MONITORS; i++) {
			if (groupTexture[i]) {
				if (blitScreen(i * 2, groupTexture[i])) {
					updateScreen = true;
				}
			}
		}
	}
	
	/* Copy UI surface to texture */
	if (SDL_GetAtomicInt(&blitUI)) {
		blitUserInterface(uiTexture);
		updateScreen = true;
	}
	
	if (updateScreen) {
		SDL_RenderClear(sdlRenderer);
		/* Render NeXT framebuffer texture */
		for (i = 0; i < NUM_MONITORS; i++) {
			if (groupTexture[i]) {
				SDL_RenderTexture(sdlRenderer, groupTexture[i], NULL, &groupRect[i]);
			}
		}
		SDL_RenderTexture(sdlRenderer, uiTexture, NULL, &uiRect);
		/* Sleeps until next VSYNC if enabled in ScreenInit */
		SDL_RenderPresent(sdlRenderer);
	}
	
	return updateScreen;
}

bool Screen_Repaint(void) {
	if (initScreenMode == SCREEN_GROUP) {
		return Screen_GroupRepaint();
	}
	return Screen_SingleRepaint();
}

#ifdef ENABLE_RENDERING_THREAD
static int repainter(void* unused) {
	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_NORMAL);

	/* Enter repaint loop */
	while (doRepaint) {
		if (!Screen_Repaint()) {
			SDL_Delay(10);
		}
	}
	return 0;
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * Force repaint after window size or full screen change
 */
static void Screen_ForceRepaint(void) {
	SDL_SetAtomicInt(&blitUI, 1);
#ifndef ENABLE_RENDERING_THREAD
	if (!bEmulationActive) {
		Screen_Repaint();
	}
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * Set Previous window title. Use NULL for default
 */
static void Screen_SetTitle(const char *title) {
	if (title)
		SDL_SetWindowTitle(sdlWindow, title);
	else
		SDL_SetWindowTitle(sdlWindow, PROG_NAME);
}

/*-----------------------------------------------------------------------*/
/**
 * Force things associated with changing between single and all screens.
 */
static void Screen_ModeChanged(void) {
	if (!sdlscrn) {
		/* screen not yet initialized */
		return;
	}

	/* Do not use multiple windows in full screen mode */
	if (bInFullScreen) {
		saveScreenMode = ConfigureParams.Screen.nMode;
		if (ConfigureParams.Screen.nMode == SCREEN_ALL) {
			ConfigureParams.Screen.nMode = SCREEN_SINGLE;
		}
	}
	if (ConfigureParams.Screen.nMode == SCREEN_ALL) {
		nd_sdl_show();
	} else {
		nd_sdl_hide();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Calculate window size and position to fit into host display bounds.
 */
static void Screen_GetWindowBounds(SDL_Rect* r) {
	SDL_DisplayID d;
	SDL_Rect usable;

	float maxscale = 1.0;
	float scale    = 1.0;

	d = SDL_GetDisplayForWindow(sdlWindow);

	r->x = SDL_WINDOWPOS_CENTERED_DISPLAY(d);
	r->y = SDL_WINDOWPOS_CENTERED_DISPLAY(d);
	r->w = width;
	r->h = height;

	if (width == initScreenWidth && abs(height - initScreenHeight) < NeXT_SCRN_H) {
		int x, y, w, h;
		if (bInFullScreen) {
			r->x = saveWindowBounds.x;
			r->y = saveWindowBounds.y;
			scale = (float)saveWindowBounds.w / width;
		} else {
			if (SDL_GetWindowPosition(sdlWindow, &x, &y)) {
				r->x = x;
				r->y = y;
			}
			if (SDL_GetWindowSize(sdlWindow, &w, &h)) {
				scale = (float)w / width;
			}
		}
	}
	if (SDL_GetDisplayUsableBounds(d, &usable)) {
		int top, left, bottom, right;
		float hscale, wscale;
		if (SDL_GetWindowBordersSize(sdlWindow, &top, &left, &bottom, &right) == false) {
			top = bottom = 50;
			left = right = 25;
		} else if (!ConfigureParams.Screen.bShowStatusbar) {
			bottom += 24; /* make sure there is enough space to show statusbar */
		}
		hscale = (float)(usable.h - top - bottom) / height;
		wscale = (float)(usable.w - left - right) / width;
		maxscale = wscale < hscale ? wscale : hscale;
	}
	if (scale > maxscale) {
		scale = maxscale;
		r->x = SDL_WINDOWPOS_CENTERED_DISPLAY(d);
		r->y = SDL_WINDOWPOS_CENTERED_DISPLAY(d);
	}
	if (scale > 0.0 && scale != 1.0) {
		fprintf(stderr, "SDL screen scale: %.3f\n", scale);
		r->w = (int)SDL_lroundf((float)r->w * scale);
		r->h = (int)SDL_lroundf((float)r->h * scale);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Create texture with default parameters.
 */
static SDL_Texture* Screen_CreateFramebufferTexture(SDL_PixelFormat format, int w, int h) {
	SDL_Texture* tex;
	
	tex = SDL_CreateTexture(sdlRenderer, format, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (tex == NULL) {
		Main_ErrorExit("Failed to create texture:", SDL_GetError(), -1);
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
	
	return tex;
}

/*-----------------------------------------------------------------------*/
/**
 * (Re-)initialise screen or handle mode change 
 */
void Screen_Reset(void) {
	int d, i;

	SDL_PixelFormat format = SDL_PIXELFORMAT_BGRA32;

#ifdef ENABLE_RENDERING_THREAD
	if (doRepaint) {
		doRepaint = false;
		SDL_WaitThread(repaintThread, &d);
	}
#endif

	/* Set initial window resolution */
	if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
		int xmax = 0;
		int ymax = 0;

		int xpos, ypos;

		for (i = 0; i < NUM_MONITORS; i++) {
			if (ConfigureParams.Screen.nGroupModePos[i] >= 0) {
				assert(ConfigureParams.Screen.nGroupModePos[i] < (NUM_MONITORS * NUM_MONITORS));

				xpos = ConfigureParams.Screen.nGroupModePos[i] % NUM_MONITORS;
				ypos = ConfigureParams.Screen.nGroupModePos[i] / NUM_MONITORS;

				xmax = xpos > xmax ? xpos : xmax;
				ymax = ypos > ymax ? ypos : ymax;

				groupRect[i].w = NeXT_SCRN_W;
				groupRect[i].h = NeXT_SCRN_H;
				groupRect[i].x = xpos * NeXT_SCRN_W;
				groupRect[i].y = ypos * NeXT_SCRN_H;
			}
		}
		screen_w = (xmax + 1) * NeXT_SCRN_W;
		screen_h = (ymax + 1) * NeXT_SCRN_H;
	} else {
		fbRect.x = 0;
		fbRect.y = 0;
		fbRect.w = NeXT_SCRN_W;
		fbRect.h = NeXT_SCRN_H;

		screen_w = NeXT_SCRN_W;
		screen_h = NeXT_SCRN_H;
	}

	width  = screen_w;
	height = screen_h;

	/* Grow to fit statusbar */
	height += Statusbar_SetHeight(screen_w, screen_h);

	/* Statusbar */
	statusBar.x = 0;
	statusBar.y = screen_h;
	statusBar.w = screen_w;
	statusBar.h = Statusbar_GetHeight();

	/* User interface including statusbar */
	uiRect.x = 0;
	uiRect.y = 0;
	uiRect.w = width;
	uiRect.h = height;

	/* Set new video mode only if necessary */
	if (width != initScreenWidth || height != initScreenHeight) {
		SDL_Rect windowBounds;
		uint32_t r, g, b, a;

		fprintf(stderr, "SDL screen request: %d x %d (%s)\n", width, height, bInFullScreen ? "fullscreen" : "windowed");

		Screen_GetWindowBounds(&windowBounds);

		SDL_SetWindowAspectRatio(sdlWindow, (float)width/height, (float)width/height);

		if (bInFullScreen) {
			/* If we are in full screen change saved window sizes */
			saveWindowBounds = windowBounds;
			SDL_SetRenderLogicalPresentation(sdlRenderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		} else {
			/* Set new window size */
			SDL_SetRenderLogicalPresentation(sdlRenderer, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);
			SDL_SetWindowSize(sdlWindow, windowBounds.w, windowBounds.h);
			SDL_SetWindowPosition(sdlWindow, windowBounds.x, windowBounds.y);
		}

		/* (Re-)initialise UI texture */
		if (uiTexture) {
			SDL_DestroyTexture(uiTexture);
			uiTexture = NULL;
		}
		uiTexture = SDL_CreateTexture(sdlRenderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
		if (!uiTexture) {
			Main_ErrorExit("Failed to create texture:", SDL_GetError(), -1);
		}
		SDL_SetTextureBlendMode(uiTexture, SDL_BLENDMODE_BLEND);

		/* Get color masks */
		SDL_GetMasksForPixelFormat(format, &d, &r, &g, &b, &a);
		mask = g | a;

		/* (Re-)initialise UI surface */
		if (sdlscrn) {
			SDL_DestroySurface(sdlscrn);
			sdlscrn = NULL;
		}
		sdlscrn = SDL_CreateSurface(width, height, format);
		if (!sdlscrn) {
			Main_ErrorExit("Could not set video mode:", SDL_GetError(), -2);
		}

		/* Clear UI with mask */
		SDL_FillSurfaceRect(sdlscrn, NULL, mask);

		/* Allocate buffer for copy routines */
		if (uiBuffer) {
			free(uiBuffer);
			uiBuffer = NULL;
		}
		uiBuffer = calloc(1, sdlscrn->h * sdlscrn->pitch);
	}

	/* Handle mode change */
	if (ConfigureParams.Screen.nMode != initScreenMode) {
		Screen_ModeChanged();
	}

	/* Create framebuffer textures and start with blank screen */
	for (i = 0; i < NUM_MONITORS; i++) {
		if (ConfigureParams.Screen.nGroupModePos[i] < 0 || ConfigureParams.Screen.nMode != SCREEN_GROUP) {
			if (groupTexture[i]) {
				SDL_DestroyTexture(groupTexture[i]);
				groupTexture[i] = NULL;
			}
		} else if (groupTexture[i] == NULL) {
			groupTexture[i] = Screen_CreateFramebufferTexture(format, NeXT_SCRN_W, NeXT_SCRN_H);
			Screen_Blank(groupTexture[i]);
		}
	}
	if (ConfigureParams.Screen.nMode == SCREEN_GROUP) {
		if (fbTexture) {
			SDL_DestroyTexture(fbTexture);
			fbTexture = NULL;
		}
	} else if (fbTexture == NULL) {
		fbTexture = Screen_CreateFramebufferTexture(format, NeXT_SCRN_W, NeXT_SCRN_H);
		Screen_Blank(fbTexture);
	}

	/* Save mode and sizes */
	initScreenMode   = ConfigureParams.Screen.nMode;
	initScreenWidth  = width;
	initScreenHeight = height;

	/* Initialise statusbar and set visibility */
	if (ConfigureParams.Screen.bShowStatusbar) {
		Statusbar_Init(sdlscrn);
		Statusbar_Update(sdlscrn);
	}

#ifdef ENABLE_RENDERING_THREAD
	/* Start repaint thread */
	doRepaint = true;
	repaintThread = SDL_CreateThread(repainter, "[Previous] Screen at slot 0", NULL);
#endif

	/* Make sure screen is painted in case emulation is paused */
	Screen_ForceRepaint();
}

/*-----------------------------------------------------------------------*/
/**
 * Init Screen, create window, renderer and textures
 */
void Screen_Init(void) {
	int i;

	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (SDL_CreateWindowAndRenderer(PROG_NAME, width, height, flags, &sdlWindow, &sdlRenderer) == false) {
		Main_ErrorExit("Failed to create window and renderer:", SDL_GetError(), -1);
	}
#ifdef ENABLE_RENDERING_THREAD
	SDL_SetRenderVSync(sdlRenderer, 1);
#endif

	/* Initialise textures and screen surface */
	Screen_Reset();

	/* Setup lookup tables */
	for (i = 0; i < 0x100; i++) {
		BW2RGB[i][0] = bw2rgb(sdlscrn, i>>6);
		BW2RGB[i][1] = bw2rgb(sdlscrn, i>>4);
		BW2RGB[i][2] = bw2rgb(sdlscrn, i>>2);
		BW2RGB[i][3] = bw2rgb(sdlscrn, i>>0);
	}
	for (i = 0; i < 0x10000; i++) {
		COL2RGB[SDL_Swap16BE(i)] = col2rgb(sdlscrn, i);
	}

	/* Set title, cursor visibility and mouse grab */
	Screen_SetTitle(NULL);
	Screen_ShowCursor(false);
	Screen_SetMouseGrab(bGrabMouse);

	/* Set titlebar visibility and change to fullscreen if requested */
	if (!ConfigureParams.Screen.bShowTitlebar) {
		Screen_TitlebarChanged();
	}
	if (ConfigureParams.Screen.bFullScreen) {
		Screen_EnterFullScreen();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Free screen bitmap and allocated resources
 */
void Screen_UnInit(void) {
	int i;
#ifdef ENABLE_RENDERING_THREAD
	int s;
	doRepaint = false; /* stop repaint thread */
	SDL_WaitThread(repaintThread, &s);
#endif
	free(uiBuffer);
	SDL_DestroySurface(sdlscrn);
	SDL_DestroyTexture(uiTexture);
	if (fbTexture) {
		SDL_DestroyTexture(fbTexture);
	}
	for (i = 0; i < NUM_MONITORS; i++) {
		if (groupTexture[i]) {
			SDL_DestroyTexture(groupTexture[i]);
		}
	}
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(sdlWindow);
}

/*-----------------------------------------------------------------------*/
/**
 * Enter Full screen mode
 */
void Screen_EnterFullScreen(void) {
	bool bWasRunning;

	if (!bInFullScreen) {
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = true;

		SDL_GetWindowPosition(sdlWindow, &saveWindowBounds.x, &saveWindowBounds.y);
		SDL_GetWindowSize(sdlWindow, &saveWindowBounds.w, &saveWindowBounds.h);
		SDL_SetRenderLogicalPresentation(sdlRenderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		SDL_SetWindowFullscreen(sdlWindow, true);
		SDL_SyncWindow(sdlWindow); /* To give monitor time to change to new resolution */

		/* If using multiple screen windows, save and go to single window mode */
		Screen_ModeChanged();

		if (bWasRunning) {
			/* And off we go... */
			Main_UnPauseEmulation();
		}

		/* Always grab mouse pointer in full screen mode */
		Screen_SetMouseGrab(true);

		/* Make sure screen is painted in case emulation is paused */
		Screen_ForceRepaint();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Return from Full screen mode back to a window
 */
void Screen_ReturnFromFullScreen(void) {
	bool bWasRunning;

	if (bInFullScreen) {
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = false;

		SDL_SetWindowFullscreen(sdlWindow, false);
		SDL_SyncWindow(sdlWindow); /* To give monitor time to switch resolution */
		SDL_SetWindowSize(sdlWindow, saveWindowBounds.w, saveWindowBounds.h);
		SDL_SetWindowPosition(sdlWindow, saveWindowBounds.x, saveWindowBounds.y);
		SDL_SetRenderLogicalPresentation(sdlRenderer, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);

		/* Return to windowed monitor mode */
		if (saveScreenMode == SCREEN_ALL) {
			ConfigureParams.Screen.nMode = saveScreenMode;
			Screen_ModeChanged();
		}

		if (bWasRunning) {
			/* And off we go... */
			Main_UnPauseEmulation();
		}

		/* Go back to windowed mode mouse grab settings */
		Screen_SetMouseGrab(bGrabMouse);

		/* Make sure screen is painted in case emulation is paused */
		Screen_ForceRepaint();
	}
}

/* ----------------------------------------------------------------------- */
/**
 * Set mouse grab.
 */
void Screen_SetMouseGrab(bool grab) {
	/* If emulation is active, set the mouse cursor mode now: */
	if (grab) {
		if (bEmulationActive) {
			Screen_CenterCursor(); /* Cursor must be inside window */
			SDL_SetWindowRelativeMouseMode(sdlWindow, true);
			SDL_SetWindowKeyboardGrab(sdlWindow, true);
			SDL_SetWindowMouseGrab(sdlWindow, true);
			if (ConfigureParams.Mouse.bEnableAutoGrab) {
				Screen_SetTitle("Mouse is locked. Ctrl-click to release.");
			} else {
				char message[64];
				
				snprintf(message, sizeof(message), "Mouse is locked. Press ctrl-alt-%s to release.", 
						 Keymap_GetKeyName(ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB]));
				Screen_SetTitle(message);
			}
		}
	} else {
		SDL_SetWindowRelativeMouseMode(sdlWindow, false);
		SDL_SetWindowKeyboardGrab(sdlWindow, false);
		SDL_SetWindowMouseGrab(sdlWindow, false);
		Screen_SetTitle(NULL);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Show main window
 */
void Screen_ShowMainWindow(void) {
	if (!bInFullScreen) {
		SDL_RestoreWindow(sdlWindow);
		SDL_RaiseWindow(sdlWindow);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Force things associated with changing screen size
 */
void Screen_SizeChanged(void) {
	int h;

	if (!bInFullScreen) {
		SDL_GetWindowSize(sdlWindow, NULL, &h);
		nd_sdl_resize((float)h/height);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Set visibilty of title bar.
 */
void Screen_TitlebarChanged(void) {
	if (sdlscrn && !bInFullScreen) {
		SDL_SetWindowBordered(sdlWindow, ConfigureParams.Screen.bShowTitlebar);
		nd_sdl_titlebar(ConfigureParams.Screen.bShowTitlebar);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Wrapper for Statusbar_AddMessage() and Statusbar_Update() in one go.
 */
void Screen_StatusbarMessage(const char *msg, uint32_t msecs)
{
	Statusbar_AddMessage(msg, msecs);
	Statusbar_Update(sdlscrn);
}

/*-----------------------------------------------------------------------*/
/**
 * Wrapper for Statusbar_Update().
 */
void Screen_StatusbarUpdate(void) {
	Statusbar_Update(sdlscrn);
}

/*-----------------------------------------------------------------------*/
/**
 * Check if we need to update full user interface or just the statusbar 
 * and copy user interface surface to buffer. Replace mask pixels with 
 * transparent pixels for blending with framebuffer texture.
 */
void Screen_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects) {
	bool doUIblit = true;

	while (numrects--) {
		doUIblit = (rects->y < statusBar.y);
		if (doUIblit) {
			break;
		}
		rects++;
	}

	SDL_LockSurface(sdlscrn);
	SDL_LockSpinlock(&uiBufferLock);
	if (doUIblit) {
		/* Copy user interface surface and replace mask pixels. */
		int i;
		uint32_t* src = (uint32_t*)sdlscrn->pixels;
		uint32_t* dst = (uint32_t*)uiBuffer;
		/* Primitive green-screen - would be nice if SDL had more blending modes. */
		for (i = sdlscrn->w * sdlscrn->h; --i >= 0; src++) *dst++ = *src == mask ? 0 : *src;
	} else {
		/* Copy statusbar without transparent pixels. */
		void* src = (uint8_t*)sdlscrn->pixels + statusBar.y * sdlscrn->pitch;
		void* dst = (uint8_t*)uiBuffer + statusBar.y * sdlscrn->pitch;
		memcpy(dst, src, statusBar.h * sdlscrn->pitch);
	}
	SDL_SetAtomicInt(&blitUI, 1);
	SDL_UnlockSpinlock(&uiBufferLock);
	SDL_UnlockSurface(sdlscrn);

#ifndef ENABLE_RENDERING_THREAD
	if (!bEmulationActive) {
		Screen_Repaint();
	}
#endif
}

void Screen_UpdateRect(SDL_Surface *screen, int32_t x, int32_t y, int32_t w, int32_t h) {
	SDL_Rect rect = { x, y, w, h };
	Screen_UpdateRects(screen, 1, &rect);
}

/* ----------------------------------------------------------------------- */
/**
 * Set mouse cursor visibility and return if it was visible before.
 */
bool Screen_ShowCursor(bool show) {
	bool bOldVisibility;
	
	bOldVisibility = SDL_CursorVisible();
	if (bOldVisibility != show) {
		if (show) {
			SDL_ShowCursor();
		} else {
			SDL_HideCursor();
		}
	}
	return bOldVisibility;
}

/* ----------------------------------------------------------------------- */
/**
 * Set mouse cursor to the center of the screen.
 */
void Screen_CenterCursor(void) {
	SDL_WarpMouseInWindow(sdlWindow, sdlscrn->w/2, sdlscrn->h/2);
	GuiEvent_WarpMouse();
}
