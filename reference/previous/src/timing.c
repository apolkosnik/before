/*
  Previous - timing.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains functions for managing host and guest times.
*/
const char Timing_fileid[] = "Previous timing.c";

#include "config.h"

#ifdef __MINGW32__
#include <unistd.h>
#define timegm _mkgmtime
#endif
#include <float.h>

#include "main.h"
#include "host.h"
#include "timing.h"
#include "configuration.h"
#include "log.h"
#include "m68000.h"


#define NUM_BLANKS 3
static atomic_int  vblCounter[NUM_BLANKS];
static const char* BLANKS[NUM_BLANKS] = {"main", "nd_main", "nd_video"};

static uint64_t     cycleCounterStart;
static uint64_t     cycleDivisor;
static uint64_t     perfCounterStart;
static uint64_t     perfFrequency;
static bool         perfCounterFreqInt;
static uint64_t     perfDivisor;
static double       perfMultiplicator;
static uint64_t     pauseTimeStamp;
static bool         enableRealtime;
static bool         osDarkmatter;
static bool         currentIsRealtime;
static uint64_t     hardClockExpected;
static uint64_t     hardClockActual;
static time_t       unixTimeStart;
static lock_t       timeLock;
static uint64_t     saveTime;


static inline uint64_t Timing_GetRealTime(void) {
	uint64_t rt = (host_get_counter() - perfCounterStart);
	if (perfCounterFreqInt) {
		rt /= perfDivisor;
	} else {
		rt *= perfMultiplicator;
	}
	return rt;
}

void Timing_Pause(bool pause) {
	if (pause) {
		pauseTimeStamp = host_get_counter();
	} else {
		perfCounterStart += host_get_counter() - pauseTimeStamp;
	}
}

/* Return current time as microseconds */
uint64_t Timing_GetTime(void) {
	bool state;
	uint64_t hostTime;
	
	/* switch to realtime if...
	 * 1) ...realtime mode is enabled and...
	 * 2) ...either we are running darkmatter or the m68k CPU is in user mode */
	state = (osDarkmatter || !(regs.s)) && enableRealtime;
	
	if (currentIsRealtime) {
		hostTime = Timing_GetRealTime();
	} else {
		hostTime  = nCyclesMainCounter - cycleCounterStart;
		hostTime /= cycleDivisor;
	}
	
	if (currentIsRealtime != state) {
		if (currentIsRealtime) {
			/* switching from real-time to cycle-time */
			cycleCounterStart = nCyclesMainCounter - hostTime * cycleDivisor;
		} else {
			/* switching from cycle-time to real-time */
			int64_t realTimeOffset = hostTime - Timing_GetRealTime();
			if (realTimeOffset > 0) {
				/* if hostTime is in the future, wait until realTime is there as well */
				if (realTimeOffset > 10000LL)
					host_sleep_us(realTimeOffset);
				else
					while (Timing_GetRealTime() < hostTime) {}
			}
		}
		currentIsRealtime = state;
	}
	
	return hostTime;
}

void Timing_GetTimes(uint64_t* realTime, uint64_t* hostTime) {
	*hostTime = Timing_GetTime();
	*realTime = Timing_GetRealTime();
}

void Timing_Sync(void) {
	int64_t realTimeOffset;
	uint64_t realTime, hostTime;
	Timing_GetTimes(&realTime, &hostTime);
	host_lock(&timeLock);
	saveTime = hostTime; /* save hostTime to be read by other threads */
	host_unlock(&timeLock);
	realTimeOffset = hostTime - realTime;
	if (realTimeOffset > 0) {
		host_sleep_us(realTimeOffset);
	}
}

/* This can be used by other threads to read hostTime */
uint64_t Timing_GetSaveTime(void) {
	uint64_t hostTime;
	host_lock(&timeLock);
	hostTime = saveTime;
	host_unlock(&timeLock);
	return hostTime / 1000000ULL;
}

/* Return current time as seconds */
static uint64_t Timing_GetTimeSec(void) {
	return Timing_GetTime() / 1000000ULL;
}

time_t Timing_GetUnixTime(void) {
	return unixTimeStart + Timing_GetTimeSec();
}

void Timing_SetUnixTime(time_t now) {
	unixTimeStart = now - Timing_GetTimeSec();
}

struct tm* Timing_GetUnixTimeStruct(void) {
	time_t tmp = Timing_GetUnixTime();
	return gmtime(&tmp);
}

void Timing_SetUnixTimeStruct(struct tm* now) {
	time_t tmp = timegm(now);
	Timing_SetUnixTime(tmp);
}

void Timing_Hardclock(int expected, int actual) {
	if (abs(actual - expected) > 1000) {
		Log_Printf(LOG_WARN, "[Hardclock] Expected: %d us, actual: %d us\n", expected, actual);
	} else {
		hardClockExpected += expected;
		hardClockActual   += actual;
	}
}

static char DARKMATTER[] = "darkmatter";

void Timing_BlankCount(int src, bool state) {
	if (state) {
		host_atomic_add(&vblCounter[src], 1);
	}
	
	/* Check first 4 bytes of version string in darkmatter/daydream kernel */
	osDarkmatter = get_long(0x04000246) == do_get_mem_long((uint8_t*)DARKMATTER);
}

static int Timing_ResetBlankCounter(int src) {
	return host_atomic_set(&vblCounter[src], 0);
}

const char* Timing_Report(uint64_t realTime, uint64_t hostTime) {
	static uint64_t lastVT;
	static char report[512];
	
	int    nBlank    = 0;
	char*  r         = report;
	double dVT       = hostTime - lastVT;
	double hardClock = hardClockExpected;
	
	dVT       /= 1000000.0;
	hardClock /= hardClockActual == 0 ? 1 : hardClockActual;
	
	r += sprintf(r, "[%s] hostTime:%"PRIu64" hardClock:%.3fMHz", enableRealtime ? "Variable" : "CycleTime", hostTime, hardClock);
	
	for(int i = NUM_BLANKS; --i >= 0;) {
		nBlank = Timing_ResetBlankCounter(i);
		r += sprintf(r, " %s:%.1fHz", BLANKS[i], (double)nBlank/dVT);
	}
	
	lastVT = hostTime;
	
	return report;
}

/* Report counter capacity */
#define DAY_TO_US (1000000ULL * 60 * 60 * 24)

static void Timing_ReportLimits(void) {
	uint64_t cycleCounterLimit, perfCounterLimit, perfCounter;
	
	Log_Printf(LOG_WARN, "[Timing] Timing system reset:");
	
	cycleCounterLimit  = UINT64_MAX - nCyclesMainCounter;
	cycleCounterLimit /= cycleDivisor;
	cycleCounterLimit /= DAY_TO_US;
	
	Log_Printf(LOG_WARN, "[Timing] Cycle counter value: %"PRIu64, nCyclesMainCounter);
	Log_Printf(LOG_WARN, "[Timing] Cycle counter frequency: %"PRIu64" MHz", cycleDivisor);
	Log_Printf(LOG_WARN, "[Timing] Cycle counter will overflow in %"PRIu64" days", cycleCounterLimit);
	
	perfCounter        = host_get_counter();
	perfCounterLimit   = UINT64_MAX - perfCounter;
	Log_Printf(LOG_WARN, "[Timing] Realtime counter value: %"PRIu64, perfCounter);
	if (perfCounterFreqInt) {
		perfCounterLimit /= perfDivisor;
		if (perfCounterLimit > INT64_MAX)
			perfCounterLimit = INT64_MAX;
		perfCounterLimit /= DAY_TO_US;
		Log_Printf(LOG_WARN, "[Timing] Realtime counter frequency: %"PRIu64" MHz", perfDivisor);
		Log_Printf(LOG_WARN, "[Timing] Realtime counter will overflow in %"PRIu64" days", perfCounterLimit);
	} else {
		if (perfCounterLimit > (1ULL<<DBL_MANT_DIG)-1)
			perfCounterLimit = (1ULL<<DBL_MANT_DIG)-1;
		if (perfMultiplicator < 1.0)
			perfCounterLimit *= perfMultiplicator;
		else
			Log_Printf(LOG_WARN, "[Timing] Warning: Realtime counter cannot resolve microseconds.");
		perfCounterLimit /= DAY_TO_US;
		Log_Printf(LOG_WARN, "[Timing] Realtime counter frequency: %f MHz", 1.0/perfMultiplicator);
		Log_Printf(LOG_WARN, "[Timing] Realtime timer will start losing precision in %"PRIu64" days", perfCounterLimit);
	}
}

/* Check NeXT specific UNIX time limits and adjust time if needed */
static void Timing_CheckUnixTime(void) {
	static const char* f = "%a %b %d %H:%M:%S %Y";
	struct tm* t;
	char s[32];
	
	t = gmtime(&unixTimeStart);
	if (t) {
		if (strftime(s, sizeof(s), f, t) > 0) {
			Log_Printf(LOG_WARN, "[Timing] Unix time: %s GMT", s);
		}
		if (t->tm_year < NEXT_MIN_YEAR || t->tm_year >= NEXT_LIMIT_YEAR) {
			t->tm_year = NEXT_START_YEAR;
			unixTimeStart = timegm(t);
			Log_Printf(LOG_WARN, "[Timing] Unix time is beyond valid range");
			Log_Printf(LOG_WARN, "[Timing] Unix time lower limit: Thu Jan  1 00:00:00 1970 GMT");
			Log_Printf(LOG_WARN, "[Timing] Unix time upper limit: Thu Dec 31 23:59:59 2037 GMT");
			if (strftime(s, sizeof(s), f, t) > 0) {
				Log_Printf(LOG_WARN, "[Timing] Setting Unix time to %s GMT", s);
			}
		}
	} else if (unixTimeStart < NEXT_MIN_SEC || unixTimeStart >= NEXT_LIMIT_SEC) {
		unixTimeStart = NEXT_START_SEC;
		Log_Printf(LOG_WARN, "[Timing] Unix time value is beyond valid range (%u through %u)", NEXT_MIN_SEC, NEXT_LIMIT_SEC);
		Log_Printf(LOG_WARN, "[Timing] Setting Unix time to Thu Jul  2 12:00:00 1987 GMT");
	}
	Log_Printf(LOG_WARN, "[Timing] Unix time will overflow in %f days", difftime(NEXT_MAX_SEC, unixTimeStart)/(24*60*60));
}

void Timing_Reset(void) {
	int i;
	
	perfCounterStart  = host_get_counter();
	pauseTimeStamp    = perfCounterStart;
	perfFrequency     = host_get_counter_frequency();
	unixTimeStart     = time(NULL);
	cycleCounterStart = 0;
	currentIsRealtime = false;
	hardClockExpected = 0;
	hardClockActual   = 0;
	enableRealtime    = ConfigureParams.System.bRealtime;
	osDarkmatter      = false;
	saveTime          = 0;
	
	for (i = NUM_BLANKS; --i >= 0;) {
		Timing_ResetBlankCounter(i);
	}
	
	cycleDivisor = ConfigureParams.System.nCpuFreq;
	
	perfCounterFreqInt = (perfFrequency % 1000000ULL) == 0;
	perfDivisor        = perfFrequency / 1000000ULL;
	perfMultiplicator  = 1000000.0 / perfFrequency;
	
	Timing_ReportLimits();
	Timing_CheckUnixTime();
}
