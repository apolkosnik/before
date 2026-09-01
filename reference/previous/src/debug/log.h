/*
  Hatari - log.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    
#include <stdbool.h>
#include <stdint.h>

/* Exception debugging
 * -------------------
 */

/* CPU exception flags
 */
#define	EXCEPT_NOHANDLER (1<<0)
#define	EXCEPT_BUS	 (1<<1)
#define	EXCEPT_ADDRESS 	 (1<<2)
#define	EXCEPT_ILLEGAL	 (1<<3)
#define	EXCEPT_ZERODIV	 (1<<4)
#define	EXCEPT_CHK	 (1<<5)
#define	EXCEPT_TRAPV	 (1<<6)
#define	EXCEPT_PRIVILEGE (1<<7)
#define	EXCEPT_TRACE     (1<<8)
#define	EXCEPT_LINEA     (1<<9)
#define	EXCEPT_LINEF     (1<<10)

/* DSP exception flags */
#define EXCEPT_DSP	 (1<<29)

/* whether to enable exception debugging on autostart */
#define EXCEPT_AUTOSTART (1<<30)

/* general flags */
#define	EXCEPT_NONE	 (0)
#define	EXCEPT_ALL	 (~EXCEPT_AUTOSTART)

/* defaults are same as with earlier -D option */
#define DEFAULT_EXCEPTIONS (EXCEPT_BUS|EXCEPT_ADDRESS|EXCEPT_DSP)

extern int ExceptionDebugMask;
extern const char* Log_SetExceptionDebugMask(const char *OptionsStr);


/* Logging
 * -------
 * Is always enabled as it's information that can be useful
 * to the Hatari users
 */
typedef enum
{
/* these present user with a dialog and log the issue */
	LOG_FATAL,	/* Hatari can't continue unless user resolves issue */
	LOG_ERROR,	/* something user did directly failed (e.g. save) */
/* these just log the issue */
	LOG_WARN,	/* something failed, but it's less serious */
	LOG_INFO,	/* user action success (e.g. TOS file load) */
	LOG_TODO,	/* functionality not yet being emulated */
	LOG_DEBUG,	/* information about internal Hatari working */
	LOG_NONE	/* invalid LOG level */
} LOGTYPE;

#define LOG_NAMES {"FATAL", "ERROR", "WARN ", "INFO ", "TODO ", "DEBUG"}


#ifndef __GNUC__
/* assuming attributes work only for GCC */
#define __attribute__(foo)
#endif

extern void Log_Default(void);
extern void Log_SetLevels(void);
extern int Log_Init(void);
extern int Log_SetAlertLevel(int level);
extern void Log_UnInit(void);
extern void Log_PrintfInt(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern LOGTYPE Log_ParseOptions(const char *OptionStr);
extern const char* Log_SetTraceOptions(const char *OptionsStr);
extern char *Log_MatchTrace(const char *text, int state);
extern void Log_ToggleMsgRepeat(void);
extern void Log_ResetMsgRepeat(void);
extern void Log_Trace(const char *format, ...)
	__attribute__ ((format (printf, 1, 2)));

#ifndef __GNUC__
#undef __attribute__
#endif

#define _Log_LOG_FATAL(nType, psFormat, ...) Log_PrintfInt(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_ERROR(nType, psFormat, ...) Log_PrintfInt(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_WARN(nType, psFormat, ...)  Log_PrintfInt(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_INFO(nType, psFormat, ...)
#define _Log_LOG_TODO(nType, psFormat, ...)
#define _Log_LOG_DEBUG(nType, psFormat, ...)
#define _Log_LOG_NONE(nType, psFormat, ...)
    
#define LOG_LEVEL_COMBINE(prefix, nType, psFormat, ...) prefix ## nType (nType, psFormat, ## __VA_ARGS__)
#define Log_Printf(nType, psFormat, ...) LOG_LEVEL_COMBINE(_Log_,nType, psFormat, ## __VA_ARGS__)

/* Tracing
 * -------
 * Tracing outputs information about what happens in the emulated
 * system and slows down the emulation.  As it's intended mainly
 * just for the Hatari developers, tracing support is compiled in
 * by default.
 * 
 * Tracing can be enabled by defining ENABLE_TRACING
 * in the top level config.h
 */
#include "config.h"

/* Up to 64 levels when using uint64_t for HatariTraceFlags */
enum {
	TRACE_BIT_CPU_DISASM,
	TRACE_BIT_CPU_EXCEPTION,
	TRACE_BIT_CPU_PAIRING,
	TRACE_BIT_CPU_REGS,
	TRACE_BIT_CPU_SYMBOLS,
	TRACE_BIT_CPU_VIDEO_CYCLES,

	TRACE_BIT_DSP_DISASM,
	TRACE_BIT_DSP_DISASM_MEM,
	TRACE_BIT_DSP_DISASM_REG,
	TRACE_BIT_DSP_HOST_COMMAND,
	TRACE_BIT_DSP_HOST_INTERFACE,
	TRACE_BIT_DSP_HOST_SSI,
	TRACE_BIT_DSP_INTERRUPT,
	TRACE_BIT_DSP_STATE,
	TRACE_BIT_DSP_SYMBOLS,

	TRACE_BIT_IOMEM_RD,
	TRACE_BIT_IOMEM_WR,
};

#define TRACE_CPU_DISASM         (1ll<<TRACE_BIT_CPU_DISASM)
#define TRACE_CPU_EXCEPTION      (1ll<<TRACE_BIT_CPU_EXCEPTION)
#define TRACE_CPU_PAIRING        (1ll<<TRACE_BIT_CPU_PAIRING)
#define TRACE_CPU_REGS           (1ll<<TRACE_BIT_CPU_REGS)
#define TRACE_CPU_SYMBOLS        (1ll<<TRACE_BIT_CPU_SYMBOLS)
#define TRACE_CPU_VIDEO_CYCLES   (1ll<<TRACE_BIT_CPU_VIDEO_CYCLES)

#define TRACE_DSP_DISASM         (1ll<<TRACE_BIT_DSP_DISASM)
#define TRACE_DSP_DISASM_MEM     (1ll<<TRACE_BIT_DSP_DISASM_MEM)
#define TRACE_DSP_DISASM_REG     (1ll<<TRACE_BIT_DSP_DISASM_REG)
#define TRACE_DSP_HOST_COMMAND   (1ll<<TRACE_BIT_DSP_HOST_COMMAND)
#define TRACE_DSP_HOST_INTERFACE (1ll<<TRACE_BIT_DSP_HOST_INTERFACE)
#define TRACE_DSP_HOST_SSI       (1ll<<TRACE_BIT_DSP_HOST_SSI)
#define TRACE_DSP_INTERRUPT      (1ll<<TRACE_BIT_DSP_INTERRUPT)
#define TRACE_DSP_STATE          (1ll<<TRACE_BIT_DSP_STATE)
#define TRACE_DSP_SYMBOLS        (1ll<<TRACE_BIT_DSP_SYMBOLS)

#define TRACE_IOMEM_RD           (1ll<<TRACE_BIT_IOMEM_RD)
#define TRACE_IOMEM_WR           (1ll<<TRACE_BIT_IOMEM_WR)

#define	TRACE_NONE		 (0)
#define	TRACE_ALL		 (~0ll)


#define	TRACE_CPU_ALL		( TRACE_CPU_PAIRING | TRACE_CPU_DISASM | TRACE_CPU_EXCEPTION | TRACE_CPU_VIDEO_CYCLES )

#define	TRACE_IOMEM_ALL		( TRACE_IOMEM_RD | TRACE_IOMEM_WR )

#define TRACE_DSP_ALL		( TRACE_DSP_HOST_INTERFACE | TRACE_DSP_HOST_COMMAND | TRACE_DSP_HOST_SSI | TRACE_DSP_DISASM \
		| TRACE_DSP_DISASM_REG | TRACE_DSP_DISASM_MEM | TRACE_DSP_STATE | TRACE_DSP_INTERRUPT )

extern FILE *TraceFile;
extern uint64_t LogTraceFlags;

#if ENABLE_TRACING

#define LOG_TRACE_LEVEL( level )	(unlikely(LogTraceFlags & (level)))

#define	LOG_TRACE(level, ...) \
	if (LOG_TRACE_LEVEL(level))	{ Log_Trace(__VA_ARGS__); }

#define LOG_TRACE_VAR

#else		/* ENABLE_TRACING */

#define LOG_TRACE(level, ...)	{}

#define LOG_TRACE_LEVEL( level )	(0)

#ifdef __GNUC__
#define LOG_TRACE_VAR __attribute__((unused))
#else
#define LOG_TRACE_VAR
#endif

#endif		/* ENABLE_TRACING */

/* Always defined in full to avoid compiler warnings about unused variables.
 * In code it's used in such a way that it will be optimized away when tracing
 * is disabled.
 */
#define LOG_TRACE_PRINT(...)	Log_Trace(__VA_ARGS__)

/* Skip message repeat suppression on multi-line output.
 * LOG_TRACE_DIRECT_INIT() should called before doing them and
 * LOG_TRACE_DIRECT_FLUSH() can be called after them
 */
#define LOG_TRACE_DIRECT(...)	    fprintf(TraceFile, __VA_ARGS__)
#define	LOG_TRACE_DIRECT_LEVEL(level, ...) \
	if (LOG_TRACE_LEVEL(level)) { fprintf(TraceFile, __VA_ARGS__); }
#define LOG_TRACE_DIRECT_INIT()	    Log_ResetMsgRepeat()
#define LOG_TRACE_DIRECT_FLUSH()    fflush(TraceFile)

#ifdef __cplusplus
}
#endif /* __cplusplus */
        
#endif		/* HATARI_LOG_H */
