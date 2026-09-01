/*
  Previous - cycInt.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_CYCINT_H
#define PREV_CYCINT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt handlers in system */
typedef enum {
	EVENT_NULL,
	EVENT_HARDCLOCK_INTERRUPT,
	EVENT_ESP_INTERRUPT,
	EVENT_ESP_IO,
	EVENT_MO_INTERRUPT,
	EVENT_MO_IO,
	EVENT_MO_ECC_IO,
	EVENT_FLOPPY_IO,
	EVENT_ETHERNET_IO,
	EVENT_PRINTER_IO,
	EVENT_SCC_IO,
	EVENT_TABLET_IO,
	EVENT_DMA_M2M_IO,
	EVENT_SND_DSP_INPUT,
	EVENT_SND_INPUT,
	EVENT_SND_OUTPUT,
	EVENT_VIDEO_VBL,
	EVENT_ND_VBL,
	EVENT_ND_VIDEO_VBL,
	EVENT_MAIN_EVENT,
	NUM_EVENTS
} event_id;

extern uint64_t nCyclesMainCounter;

extern void CycInt_Reset(void);
extern void CycInt_AddCycles(int Cycles);
extern void CycInt_AddCyclesEvent(uint64_t Cycles, event_id i);
extern void CycInt_UpdateCyclesEvent(uint64_t Cycles, event_id i);
extern void CycInt_AddTimeEvent(uint64_t RealTime, uint64_t FastTime, event_id i);
extern void CycInt_UpdateTimeEvent(uint64_t RealTime, uint64_t FastTime, event_id i);
extern void CycInt_AddCycleTimeEvent(uint64_t CycleTime, uint64_t FastTime, event_id i);
extern void CycInt_UpdateCycleTimeEvent(uint64_t CycleTime, uint64_t FastTime, event_id i);
extern void CycInt_RemovePendingEvent(event_id i);
extern bool CycInt_EventPending(event_id i);

#ifdef __cplusplus
}
#endif

#endif /* ifndef PREV_CYCINT_H */
