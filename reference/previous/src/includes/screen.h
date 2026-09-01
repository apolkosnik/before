/*
  Previous - screen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SCREEN_H
#define PREV_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern volatile bool bGrabMouse;
extern volatile bool bInFullScreen;

extern const int NeXT_SCRN_W;
extern const int NeXT_SCRN_H;

extern int screen_w;
extern int screen_h;

/* These functions must be provided through host or cross-platform API. */
extern void Screen_EnterFullScreen(void);
extern void Screen_ReturnFromFullScreen(void);
extern void Screen_SetMouseGrab(bool grab);
extern void Screen_ShowMainWindow(void);
extern void Screen_TitlebarChanged(void);
extern void Screen_StatusbarMessage(const char *msg, uint32_t msecs);
extern void Screen_StatusbarUpdate(void);
extern bool Screen_ShowCursor(bool show);
extern void Screen_CenterCursor(void);
extern void Screen_Reset(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_SCREEN_H */
