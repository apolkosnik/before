/*
  Previous - cycInt.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles cycle accurate program interruption. We add any pending
  callback handler into a queue so that we do not need to test for every
  possible event. We support two time units: CPU cycles and microseconds. 
  Microseconds are either bound to the host CPU's performance counter in 
  realtime mode or to the emulated CPU cycles if non-realtime mode.
*/

const char CycInt_fileid[] = "Previous cycInt.c";

#include "main.h"
#include "cycInt.h"
#include "configuration.h"
#include "timing.h"
#include "video.h"
#include "sysReg.h"
#include "esp.h"
#include "mo.h"
#include "ethernet.h"
#include "dma.h"
#include "floppy.h"
#include "snd.h"
#include "printer.h"
#include "scc.h"
#include "tablet.h"
#include "dimension.hpp"


#define CHECK_INTERVAL 100

uint64_t nCyclesMainCounter; /* Main cycles counter, counts emulated CPU cycles since reset */

typedef enum {
	TYPE_NONE,
	TYPE_CYCLES,
	TYPE_TIME
} event_type;

typedef struct {
	void (*func)(void);
	event_type type;
	uint64_t time;
	event_id prev;
	event_id next;
} cycint_event;

static cycint_event EventList[NUM_EVENTS];

static uint64_t nCheckCycles;
static uint64_t nTimeNow;
static event_id nCyclesFirst;
static event_id nTimeFirst;

/* List of possible event handlers to be stored in event function pointers */
static void (*const pEventHandlers[NUM_EVENTS])(void) =
{
	NULL,
	Hardclock_Interrupt_Handler,
	ESP_Interrupt_Handler,
	ESP_IO_Handler,
	MO_Interrupt_Handler,
	MO_IO_Handler,
	MO_ECC_IO_Handler,
	Floppy_IO_Handler,
	Ethernet_IO_Handler,
	Printer_IO_Handler,
	SCC_IO_Handler,
	Tablet_IO_Handler,
	DMA_M2M_IO_Handler,
	SND_DSP_Handler,
	SND_In_Handler,
	SND_Out_Handler,
	Video_VBL_Handler,
	ND_VBL_Handler,
	ND_Video_VBL_Handler,
	Main_EventHandler
};


/*-----------------------------------------------------------------------*/
/**
 * Reset events and handlers.
 */
void CycInt_Reset(void) {
	event_id i;

	/* Reset counts */
	nCyclesMainCounter = 0;
	nCheckCycles       = ConfigureParams.System.bRealtime ? 0 : UINT64_MAX;
	nTimeNow           = 0;
	
	/* Reset entry points */
	nCyclesFirst = EVENT_NULL;
	nTimeFirst   = EVENT_NULL;

	/* Reset event table */
	for (i = EVENT_NULL; i < NUM_EVENTS; i++) {
		EventList[i].func = pEventHandlers[i];
		EventList[i].type = TYPE_NONE;
		EventList[i].time = UINT64_MAX;
		EventList[i].prev = EVENT_NULL;
		EventList[i].next = EVENT_NULL;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Add cycles and process pending events.
 */
void CycInt_AddCycles(int Cycles) {
	nCyclesMainCounter += Cycles;
	while (EventList[nCyclesFirst].time <= nCyclesMainCounter) {
		event_id i = nCyclesFirst;
		EventList[i].type = TYPE_NONE;
		nCyclesFirst = EventList[i].next;
		EventList[nCyclesFirst].prev = EVENT_NULL;
		EventList[i].func();
	}
	if (nCheckCycles <= nCyclesMainCounter) {
		nTimeNow = Timing_GetTime();
		while (nTimeFirst) {
			int64_t diff = EventList[nTimeFirst].time - nTimeNow;
			if (diff > 0) {
				if (diff < CHECK_INTERVAL) {
					nCheckCycles = nCyclesMainCounter + diff * ConfigureParams.System.nCpuFreq;
					return;
				}
				break;
			} else {
				event_id i = nTimeFirst;
				EventList[i].type = TYPE_NONE;
				nTimeFirst = EventList[i].next;
				EventList[nTimeFirst].prev = EVENT_NULL;
				EventList[i].func();
			}
		}
		nCheckCycles = nCyclesMainCounter + CHECK_INTERVAL * ConfigureParams.System.nCpuFreq;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Add event to the queue.
 */
static inline event_id CycInt_AddEvent(event_id first, event_id i) {
	event_id next, prev;

	next = first;
	prev = EVENT_NULL;

	while (EventList[next].time < EventList[i].time) {
		prev = next;
		next = EventList[next].next;
	}
	if (next == first) {
		first = i;
	}
	EventList[i].prev = prev;
	EventList[i].next = next;
	if (prev) {
		EventList[prev].next = i;
	}
	if (next) {
		EventList[next].prev = i;
	}
	return first;
}

/*-----------------------------------------------------------------------*/
/**
 * Set or update cycle event and add it to the queue.
 */
void CycInt_AddCyclesEvent(uint64_t Cycles, event_id i) {
	if (EventList[i].type) {
		CycInt_RemovePendingEvent(i);
	}
	EventList[i].type = TYPE_CYCLES;
	EventList[i].time = nCyclesMainCounter + Cycles;
	nCyclesFirst = CycInt_AddEvent(nCyclesFirst, i);
}
void CycInt_UpdateCyclesEvent(uint64_t Cycles, event_id i) {
	if (EventList[i].type) {
		CycInt_RemovePendingEvent(i);
	}
	EventList[i].type = TYPE_CYCLES;
	EventList[i].time += Cycles;
	nCyclesFirst = CycInt_AddEvent(nCyclesFirst, i);
}

/*-----------------------------------------------------------------------*/
/**
 * Set or update microsecond time event and add it to the queue.
 */
void CycInt_AddTimeEvent(uint64_t RealTime, uint64_t FastTime, event_id i) {
	if (EventList[i].type) {
		CycInt_RemovePendingEvent(i);
	}
	if (ConfigureParams.System.bRealtime) {
		RealTime = FastTime ? FastTime : RealTime;
		EventList[i].type = TYPE_TIME;
		EventList[i].time = Timing_GetTime() + RealTime;
		nTimeFirst = CycInt_AddEvent(nTimeFirst, i);
		if (RealTime < CHECK_INTERVAL && i == nTimeFirst) {
			nCheckCycles = nCyclesMainCounter + RealTime * ConfigureParams.System.nCpuFreq;
		}
	} else {
		EventList[i].type = TYPE_CYCLES;
		EventList[i].time = nCyclesMainCounter + RealTime * ConfigureParams.System.nCpuFreq;
		nCyclesFirst = CycInt_AddEvent(nCyclesFirst, i);
	}
}
void CycInt_UpdateTimeEvent(uint64_t RealTime, uint64_t FastTime, event_id i) {
	if (EventList[i].type) {
		CycInt_RemovePendingEvent(i);
	}
	if (ConfigureParams.System.bRealtime) {
		RealTime = FastTime ? FastTime : RealTime;
		nTimeNow = Timing_GetTime();
		if ((nTimeNow - EventList[i].time) > (RealTime >> 1)) {
			EventList[i].time = nTimeNow;
		}
		EventList[i].type = TYPE_TIME;
		EventList[i].time += RealTime;
		nTimeFirst = CycInt_AddEvent(nTimeFirst, i);
	} else {
		EventList[i].type = TYPE_CYCLES;
		EventList[i].time += RealTime * ConfigureParams.System.nCpuFreq;
		nCyclesFirst = CycInt_AddEvent(nCyclesFirst, i);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Convert microseconds to cycles and set or update cycle event.
 */
void CycInt_AddCycleTimeEvent(uint64_t CycleTime, uint64_t FastTime, event_id i) {
	if (ConfigureParams.System.bRealtime && FastTime) {
		CycleTime = FastTime;
	}
	CycInt_AddCyclesEvent(CycleTime * ConfigureParams.System.nCpuFreq, i);
}
void CycInt_UpdateCycleTimeEvent(uint64_t CycleTime, uint64_t FastTime, event_id i) {
	if (ConfigureParams.System.bRealtime && FastTime) {
		CycleTime = FastTime;
	}
	CycInt_UpdateCyclesEvent(CycleTime * ConfigureParams.System.nCpuFreq, i);
}

/*-----------------------------------------------------------------------*/
/**
 * Remove event from the corresponding queue.
 */
void CycInt_RemovePendingEvent(event_id i) {
	if (EventList[i].type == TYPE_CYCLES) {
		if (i == nCyclesFirst) {
			nCyclesFirst = EventList[i].next;
		}
	} else if (EventList[i].type == TYPE_TIME) {
		if (i == nTimeFirst) {
			nTimeFirst = EventList[i].next;
		}
	} else {
		return;
	}
	if (EventList[i].prev) {
		EventList[EventList[i].prev].next = EventList[i].next;
	}
	if (EventList[i].next) {
		EventList[EventList[i].next].prev = EventList[i].prev;
	}
	EventList[i].type = TYPE_NONE;
}

/*-----------------------------------------------------------------------*/
/**
 * Return true if the event is queued.
 */
bool CycInt_EventPending(event_id i) {
	return (EventList[i].type != TYPE_NONE);
}
