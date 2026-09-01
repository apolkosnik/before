/*
  Previous - sdlaudio.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SDLAUDIO_H
#define PREV_SDLAUDIO_H

#include <SDL3/SDL.h>

extern void Audio_DeviceConnected(bool recording);
extern void Audio_DeviceDisconnected(bool recording);
extern void Audio_FormatChanged(bool recording);

#endif /* PREV_SDLAUDIO_H */
