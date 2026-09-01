/*
  Previous - grab.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_GRAB_H
#define PREV_GRAB_H

extern void Grab_Screen(void);
extern void Grab_Print(uint8_t* data, int width, int height, int dpi);

extern void Grab_Sound(uint8_t* samples, int len);
extern void Grab_SoundToggle(void);

extern void Grab_Stop(void);

extern volatile bool bRecordingAiff;

#endif /* PREV_GRAB_H */
