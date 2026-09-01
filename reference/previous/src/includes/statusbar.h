/*
  Previous - statusbar.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef PREV_STATUSBAR_H
#define PREV_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	DEVICE_LED_ENET,
	DEVICE_LED_OD,
	DEVICE_LED_SCSI,
    DEVICE_LED_FD,
    NUM_DEVICE_LEDS
} drive_index_t;

typedef enum {
	LED_STATE_OFF,
	LED_STATE_ON,
	LED_STATE_ON_BUSY,
	MAX_LED_STATE
} drive_led_t;

/* These functions must be provided through host or cross-platform API. */
extern void Statusbar_BlinkLed(drive_index_t drive);
extern void Statusbar_SetSystemLed(bool state);
extern void Statusbar_SetDspLed(bool state);
extern void Statusbar_SetNdLed(int state);

extern void Statusbar_UpdateInfo(void);
extern void Statusbar_AddMessage(const char *msg, uint32_t msecs);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PREV_STATUSBAR_H */
