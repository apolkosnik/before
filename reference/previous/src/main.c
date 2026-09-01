/*
  Previous - main.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Main_fileid[] = "Previous main.c";

#include <time.h>
#include <errno.h>
#include <signal.h>

#include "main.h"
#include "event.h"
#include "timing.h"
#include "configuration.h"
#include "dialog.h"
#include "ioMem.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "paths.h"
#include "reset.h"
#include "screen.h"
#include "snd.h"
#include "ethernet.h"
#include "printer.h"
#include "statusbar.h"
#include "str.h"
#include "debugui.h"
#include "file.h"
#include "dsp.h"
#include "host.h"
#include "grab.h"
#include "dimension.hpp"

#include "hatari-glue.h"
#include "NextBus.hpp"

#ifdef WIN32
#include "gui-win/opencon.h"
#endif

volatile bool bQuitProgram = false;            /* Flag to quit program cleanly */
volatile bool bEmulationActive = false;        /* Do not run emulation during initialization */

#ifndef ENABLE_RENDERING_THREAD
static thread_t*    nextThread;
static semaphore_t* pauseFlag;
#endif

typedef const char* (*report_func)(uint64_t realTime, uint64_t hostTime);

typedef struct {
	const char*       label;
	const report_func report;
} report_t;

static uint64_t lastRT;
static uint64_t lastCycles;
static double   speedFactor = 1.0;
static char     speedMsg[32];

static void Main_Speed(uint64_t realTime, uint64_t hostTime) {
	uint64_t dRT  = realTime - lastRT;
	speedFactor   = nCyclesMainCounter - lastCycles;
	speedFactor  /= ConfigureParams.System.nCpuFreq;
	speedFactor  /= dRT;
	lastRT        = realTime;
	lastCycles    = nCyclesMainCounter;
}

void Main_SpeedReset(void) {
	uint64_t realTime, hostTime;
	Timing_GetTimes(&realTime, &hostTime);
	lastRT     = realTime;
	lastCycles = nCyclesMainCounter;

	Log_Printf(LOG_WARN, "Realtime mode %s.\n", ConfigureParams.System.bRealtime ? "enabled" : "disabled");
}

const char* Main_SpeedMsg(void) {
	if (ConfigureParams.System.bRealtime) {
		snprintf(speedMsg, sizeof(speedMsg), "%dMHz/", (int)(ConfigureParams.System.nCpuFreq * speedFactor + 0.5));
	} else if (speedFactor < 0.9 || speedFactor > 1.1) {
		snprintf(speedMsg, sizeof(speedMsg), "%.1fx%dMHz/", speedFactor, ConfigureParams.System.nCpuFreq);
	} else {
		snprintf(speedMsg, sizeof(speedMsg), "%dMHz/", ConfigureParams.System.nCpuFreq);
	}
	return speedMsg;
}

#if ENABLE_TESTING
static const report_t reports[] = {
	{"ND",    nd_reports},
	{"Host",  Timing_Report},
};
#endif

/*-----------------------------------------------------------------------*/
/**
 * Pause emulation, stop sound.  'visualize' should be set true,
 * unless unpause will be called immediately afterwards.
 * 
 * @return true if paused now, false if was already paused
 */
bool Main_PauseEmulation(bool visualize) {
	if ( !bEmulationActive )
		return false;

	bEmulationActive = false;
#ifndef ENABLE_RENDERING_THREAD
	/* Wait until 68k thread is paused */
	if (host_semaphore_wait_timeout(pauseFlag, 1000))
		Log_Printf(LOG_WARN, "Warning: Pause flag timeout!");
#endif
	Timing_Pause(true);
	Sound_Pause(true);
	NextBus_Pause(true);

	if (visualize) {
		Screen_StatusbarMessage("Emulation paused", 100);

		/* Un-grab mouse pointer */
		Screen_SetMouseGrab(false);
	}

	/* Show mouse pointer and set it to the middle of the screen */
	Screen_ShowCursor(true);
	Screen_CenterCursor();

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Start/continue emulation
 * 
 * @return true if continued, false if was already running
 */
bool Main_UnPauseEmulation(void) {
	if ( bEmulationActive )
		return false;

	NextBus_Pause(false);
	Sound_Pause(false);
	Timing_Pause(false);

	/* Set mouse pointer to the middle of the screen and hide it */
	Screen_CenterCursor();
	Screen_ShowCursor(false);

	GuiEvent_ResetKeys();

	bEmulationActive = true;

	Screen_SetMouseGrab(bGrabMouse);

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Pause emulation if a fatal CPU error occured and ask if user wants to 
 * reset or quit.
 */
void Main_HaltDialog(void) {
	Main_PauseEmulation(true);
	Log_Printf(LOG_WARN, "Fatal error: CPU halted!");
	if (!DlgAlert_Query("Fatal error: CPU halted!\n\nPress OK to restart CPU or cancel to quit.")) {
		Main_RequestQuit(false);
	}
	Main_UnPauseEmulation();
}
void Main_Halt(void) {
#ifdef ENABLE_RENDERING_THREAD
	Main_HaltDialog();
#else
	GuiEvent_SendSpecialEvent(SPECIAL_EVENT_HALT);
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * Optionally ask user whether to quit and set bQuitProgram accordingly
 */
void Main_RequestQuit(bool confirm) {
	bool bWasActive;

	if (confirm) {
		bWasActive = Main_PauseEmulation(true);
		bQuitProgram = false;	/* if set true, dialog exits */
		bQuitProgram = DlgAlert_Query("All unsaved data will be lost.\nDo you really want to quit?");
		if (bWasActive) {
			Main_UnPauseEmulation();
		}
	} else {
		bQuitProgram = true;
	}

	if (bQuitProgram) {
		/* Assure that CPU core shuts down */
		M68000_Stop();
	}
}

/* ----------------------------------------------------------------------- */
/**
 * Emulator message handler. Called from emulator.
 */
void Main_EventHandler(void) {
	static int statusBarUpdate = 0;
#ifndef ENABLE_RENDERING_THREAD
	if (!bEmulationActive) {
		host_semaphore_signal(pauseFlag);
		do {
			host_sleep_ms(20);
		} while(!bEmulationActive);
	}
#endif
	if (++statusBarUpdate > 400) {
		uint64_t vt;
		uint64_t rt;
		Timing_GetTimes(&rt, &vt);
#if ENABLE_TESTING
		fprintf(stderr, "[reports]");
		for(int i = 0; i < ARRAY_SIZE(reports); i++) {
			const char* msg = reports[i].report(rt, vt);
			if(msg[0]) fprintf(stderr, " %s:%s", reports[i].label, msg);
		}
		fprintf(stderr, "\n");
		fflush(stderr);
#endif
		Main_Speed(rt, vt);
		Statusbar_UpdateInfo();
		statusBarUpdate = 0;
	}

#ifdef ENABLE_RENDERING_THREAD
	GuiEvent_EventHandler();
#else
	GuiEvent_EventQueueHandler();
#endif

	Timing_Sync();

	CycInt_AddTimeEvent((1000*1000)/200, 0, EVENT_MAIN_EVENT); /* Poll events at 200 Hz */
}

#ifndef ENABLE_RENDERING_THREAD
/* ----------------------------------------------------------------------- */
/**
 * Emulator thread. Start emulation and keep it running.
 */
static int Main_Thread(void* unused) {
	host_thread_priority(1);

	while (!bQuitProgram) {
		/* Start EventHandler */
		CycInt_AddTimeEvent(1000, 0, EVENT_MAIN_EVENT);

		/* Start emulation */
		M68000_Start();
	}

	bEmulationActive = false;

	return 0;
}
#endif /* !ENABLE_RENDERING_THREAD */


/* ----------------------------------------------------------------------- */
/**
 * Main loop. Start emulation and loop.
 */
static void Main_Loop(void) {
	/* Get an event ID for our special event */
	GuiEvent_InitSpecialEvent();

	/* Enable emulation */
	Main_UnPauseEmulation();

#ifdef ENABLE_RENDERING_THREAD
	/* Start EventHandler */
	CycInt_AddTimeEvent(1000, 0, EVENT_MAIN_EVENT);

	/* Start emulation */
	M68000_Start();
#else
	/* Initialize event queue */
	GuiEvent_InitEventQueue();

	/* Start emulator thread */
	pauseFlag  = host_semaphore_create(0);
	nextThread = host_thread_create(Main_Thread, "[Previous] 68k at slot 0", NULL);

	/* Start EventHandler */
	while (!bQuitProgram) {
		GuiEvent_EventHandler();
	}
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * Show dialog at start.
 * 
 * @return true if configuration is ready, false if we need to quit
 */
static bool Main_StartMenu(void) {
	if (!File_Exists(sConfigFileName) || ConfigureParams.ConfigDialog.bShowConfigDialogAtStartup) {
		Dialog_DoProperty();
	}
	if (!bQuitProgram) {
		Dialog_CheckFiles();
	}
	return !bQuitProgram;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize emulation
 * 
 * @return true if initialization succeeded, false if it failed
 */
static bool Main_Init(void) {
	/* Open debug log file */
	if (!Log_Init())
	{
		Main_ErrorExit("Logging/tracing initialization failed", NULL, -1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

	/* Init user interface */
	UI_Init();

	/* Init emulation */
	M68000_Init();
	DSP_Init();
	IoMem_Init();
	CycInt_Reset();
	/* Done as last, needs CPU & DSP running... */
	DebugUI_Init();

	/* Call menu at startup */
	if (Main_StartMenu()) {
		/* Reset emulated machine */
		return !Reset_Cold();
	}
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialise emulation
 */
static void Main_UnInit(void) {
#ifndef ENABLE_RENDERING_THREAD
	/* Make sure emulator thread exits */
	bEmulationActive = true;
	host_thread_wait(nextThread);
	host_semaphore_destroy(pauseFlag);
#endif
	Sound_Pause(true);
	Printer_UnInit();
	Ethernet_UnInit();
	IoMem_UnInit();
	UI_UnInit();
	Exit680x0();

	/* Close debug log file */
	DebugUI_UnInit();
	Log_UnInit();

	Paths_UnInit();

	/* Quit UI API as late as possible */
	UI_Quit();
}


/*-----------------------------------------------------------------------*/
/**
 * Load initial configuration file(s)
 */
static void Main_LoadInitialConfig(void) {
	char *psGlobalConfig;

	psGlobalConfig = malloc(FILENAME_MAX);
	if (psGlobalConfig)
	{
		File_MakePathBuf(psGlobalConfig, FILENAME_MAX, CONFDIR,
		                 "previous", "cfg");
		/* Try to load the global configuration file */
		Configuration_Load(psGlobalConfig);

		free(psGlobalConfig);
	}

	/* Now try the users configuration file */
	Configuration_Load(NULL);
}

/*-----------------------------------------------------------------------*/
/**
 * Set system information and initial help message
 */
static void Main_StatusbarSetup(void) {
	const char *name = NULL;
	int key;

	key = ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS];
	if (!key)
		key = ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS];
	if (key)
		name = Keymap_GetKeyName(key);
	if (name)
	{
		char message[24], *keyname;

		keyname = Str_ToUpper(strdup(name));
		snprintf(message, sizeof(message), "Press %s for Options", keyname);
		free(keyname);

		Statusbar_AddMessage(message, 6000);
	}
	/* update information loaded by Main_Init() */
	Statusbar_UpdateInfo();
}

/**
 * Set signal handlers to catch signals
 */
static void Main_SetSignalHandlers(void) {
#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	signal(SIGFPE, SIG_IGN);
}

/**
 * Error exit wrapper, to make sure user sees the error messages
 * also on Windows.
 *
 * If message is given, Windows console is opened to show it,
 * otherwise it's assumed to be already open and relevant
 * messages shown before calling this.
 *
 * User input is waited on Windows, to make sure user sees
 * the message before console closes.
 *
 * Value overrides nQuitValue as process exit/return value.
 */
void Main_ErrorExit(const char *msg1, const char *msg2, int errval)
{
	if (msg1)
	{
#ifdef WIN32
		Win_ForceCon();
#endif
		if (msg2)
			fprintf(stderr, "ERROR: %s\n\t%s\n", msg1, msg2);
		else
			fprintf(stderr, "ERROR: %s!\n", msg1);
	}

	UI_UnInit();

#ifdef WIN32
	fputs("<press Enter to exit>\n", stderr);
	(void)fgetc(stdin);
#endif
	exit(errval);
}

/**
 * Main
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[])
{
	/* Generate random seed */
	srand((unsigned)time(NULL));

	/* Set signal handlers */
	Main_SetSignalHandlers();

	/* Initialize directory strings */
	Paths_Init(argv[0]);

	/* Set default configuration values */
	Configuration_SetDefault();

	/* Now load the values from the configuration file */
	Main_LoadInitialConfig();

	/* monitor type option might require "reset" -> true */
	Configuration_Apply(true);

#ifdef WIN32
	Win_OpenCon();
#endif

	/* Init emulator system */
	if (Main_Init()) {
		/* Set initial Statusbar information */
		Main_StatusbarSetup();

		/* Run emulation */
		Main_Loop();
	}

	/* Stop recording */
	Grab_Stop();

	/* Return from full screen */
	Screen_ReturnFromFullScreen();

	/* Un-init emulation system */
	Main_UnInit();

	return 0;
}
