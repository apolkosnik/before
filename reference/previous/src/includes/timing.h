/*
  Previous - timing.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_TIMING_H
#define PREV_TIMING_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MAIN_DISPLAY,
    ND_DISPLAY,
    ND_VIDEO,
};

extern void        Timing_Pause(bool pause);
extern uint64_t    Timing_GetTime(void);
extern void        Timing_GetTimes(uint64_t* realTime, uint64_t* hostTime);
extern void        Timing_Sync(void);
extern uint64_t    Timing_GetSaveTime(void);
extern time_t      Timing_GetUnixTime(void);
extern void        Timing_SetUnixTime(time_t now);
extern struct tm*  Timing_GetUnixTimeStruct(void);
extern void        Timing_SetUnixTimeStruct(struct tm* now);
extern void        Timing_Hardclock(int expected, int actual);
extern void        Timing_BlankCount(int src, bool state);
extern const char* Timing_Report(uint64_t realTime, uint64_t hostTime);
extern void        Timing_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PREV_TIMING_H */
