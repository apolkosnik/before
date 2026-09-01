/*
  Previous - event.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_EVENT_H
#define PREV_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Types for special event */
enum {
	SPECIAL_EVENT_REPAINT,
	SPECIAL_EVENT_ND_DISPLAY,
	SPECIAL_EVENT_PAUSE,
	SPECIAL_EVENT_UNPAUSE,
	SPECIAL_EVENT_HALT
};

/* These functions must be provided through host or cross-platform API. */
extern void GuiEvent_InitEventQueue(void);
extern void GuiEvent_InitSpecialEvent(void);
extern void GuiEvent_SendSpecialEvent(int type);
extern void GuiEvent_ResetKeys(void);
extern void GuiEvent_WarpMouse(void);
extern void GuiEvent_EventHandler(void);
extern void GuiEvent_EventQueueHandler(void);

extern void UI_Init(void);
extern void UI_UnInit(void);
extern void UI_Quit(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ifndef PREV_EVENT_H */
