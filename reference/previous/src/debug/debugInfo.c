/*
  Hatari - debuginfo.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  debuginfo.c - functions needed to show info about the atari HW & OS
   components and "lock" that info to be shown on entering the debugger.
*/
const char DebugInfo_fileid[] = "Hatari debuginfo.c";

#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include "main.h"
#include "configuration.h"
#include "debugInfo.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugui.h"
#include "debug_priv.h"
#include "dsp.h"
#include "evaluate.h"
#include "file.h"
#include "history.h"
#include "ioMem.h"
#include "rtcnvram.h"
#include "m68000.h"
#include "newcpu.h"
#include "68kDisass.h"

/* ------------------------------------------------------------------
 * CPU and DSP information wrappers
 */

/**
 * Helper: call debugcpu.c and debugdsp.c debugger commands
 */
static void DebugInfo_CallCommand(int (*func)(int, char* []), const char *command, uint32_t arg)
{
	char cmdbuffer[16], argbuffer[12];
	char *argv[] = { cmdbuffer, NULL };
	int argc = 1;

	assert(strlen(command) < sizeof(cmdbuffer));
	strcpy(cmdbuffer, command);
	if (arg) {
		sprintf(argbuffer, "$%x", arg);
		argv[argc++] = argbuffer;
	}
	func(argc, argv);
}

static void DebugInfo_CpuRegister(FILE *fp, uint32_t arg)
{
	DebugInfo_CallCommand(DebugCpu_Register, "register", arg);
}
static void DebugInfo_CpuDisAsm(FILE *fp, uint32_t arg)
{
	DebugInfo_CallCommand(DebugCpu_DisAsm, "disasm", arg);
}
static void DebugInfo_CpuMemDump(FILE *fp, uint32_t arg)
{
	DebugInfo_CallCommand(DebugCpu_MemDump, "memdump", arg);
}

#if ENABLE_DSP_EMU

static void DebugInfo_DspRegister(FILE *fp, uint32_t arg)
{
	DebugInfo_CallCommand(DebugDsp_Register, "dspreg", arg);
}
static void DebugInfo_DspDisAsm(FILE *fp, uint32_t arg)
{
	DebugInfo_CallCommand(DebugDsp_DisAsm, "dspdisasm", arg);
}

static void DebugInfo_DspMemDump(FILE *fp, uint32_t arg)
{
	char cmdbuf[] = "dspmemdump";
	char addrbuf[6], spacebuf[2] = "X";
	char *argv[] = { cmdbuf, spacebuf, addrbuf };
	spacebuf[0] = (arg>>16)&0xff;
	sprintf(addrbuf, "$%x", (uint16_t)(arg&0xffff));
	DebugDsp_MemDump(3, argv);
}

/**
 * Convert arguments to uint32_t arg suitable for DSP memdump callback
 */
static uint32_t DebugInfo_DspMemArgs(int argc, char *argv[])
{
	uint32_t value;
	char space;
	if (argc != 2) {
		return 0;
	}
	space = toupper((unsigned char)argv[0][0]);
	if ((space != 'X' && space != 'Y' && space != 'P') || argv[0][1]) {
		fprintf(stderr, "ERROR: invalid DSP address space '%s'!\n", argv[0]);
		return 0;
	}
	if (!Eval_Number(argv[1], &value, NUM_TYPE_DSP) || value > 0xffff) {
		fprintf(stderr, "ERROR: invalid DSP address '%s'!\n", argv[1]);
		return 0;
	}
	return ((uint32_t)space<<16) | value;
}

#endif  /* ENABLE_DSP_EMU */


static void DebugInfo_RegAddr(FILE *fp, uint32_t arg)
{
	bool forDsp;
	char regname[3];
	uint32_t *reg32, regvalue, mask;
	char cmdbuf[6], addrbuf[12];
	char *argv[] = { cmdbuf, addrbuf };

	regname[0] = (arg>>24)&0xff;
	regname[1] = (arg>>16)&0xff;
	regname[2] = '\0';

	if (DebugCpu_GetRegisterAddress(regname, &reg32)) {
		regvalue = *reg32;
		mask = 0xffffffff;
		forDsp = false;
	} else {
		int regsize = DSP_GetRegisterAddress(regname, &reg32, &mask);
		switch (regsize) {
			/* currently regaddr supports only 32-bit Rx regs, but maybe later... */
		case 16:
			regvalue = *((uint16_t*)reg32);
			break;
		case 32:
			regvalue = *reg32;
			break;
		default:
			fprintf(stderr, "ERROR: invalid address/data register '%s'!\n", regname);
			return;
		}
		forDsp = true;
	}
       	sprintf(addrbuf, "$%x", regvalue & mask);

	if ((arg & 0xff) == 'D') {
		if (forDsp) {
#if ENABLE_DSP_EMU
			strcpy(cmdbuf, "dd");
			DebugDsp_DisAsm(2, argv);
#endif
		} else {
			strcpy(cmdbuf, "d");
			DebugCpu_DisAsm(2, argv);
		}
	} else {
		if (forDsp) {
#if ENABLE_DSP_EMU
			/* use "Y" address space */
			char cmd[] = "dm"; char space[] = "y";
			char *dargv[] = { cmd, space, addrbuf };
			DebugDsp_MemDump(3, dargv);
#endif
		} else {
			strcpy(cmdbuf, "m");
			DebugCpu_MemDump(2, argv);
		}
	}
}

/**
 * Convert arguments to uint32_t arg suitable for RegAddr callback
 */
static uint32_t DebugInfo_RegAddrArgs(int argc, char *argv[])
{
	uint32_t value, *regaddr;
	if (argc != 2) {
		return 0;
	}

	if (strcmp(argv[0], "disasm") == 0) {
		value = 'D';
	} else if (strcmp(argv[0], "memdump") == 0) {
		value = 'M';
	} else {
		fprintf(stderr, "ERROR: regaddr operation can be only 'disasm' or 'memdump', not '%s'!\n", argv[0]);
		return 0;
	}

	if (strlen(argv[1]) != 2 ||
	    (!DebugCpu_GetRegisterAddress(argv[1], &regaddr) &&
	     (toupper((unsigned char)argv[1][0]) != 'R'
	      || !isdigit((unsigned char)argv[1][1]) || argv[1][2]))) {
		/* not CPU register or Rx DSP register */
		fprintf(stderr, "ERROR: invalid address/data register '%s'!\n", argv[1]);
		return 0;
	}

	value |= argv[1][0] << 24;
	value |= argv[1][1] << 16;
	value &= 0xffff00ff;
	return value;
}


/* ------------------------------------------------------------------
 * wrappers for command to parse debugger input file
 */

/* file name to be given before calling the Parse function,
 * needs to be set separately as it's a host pointer which
 * can be 64-bit i.e. may not fit into uint32_t.
 */
static char *parse_filename;

/**
 * Parse and exec commands in the previously given debugger input file
 */
static void DebugInfo_FileParse(FILE *fp, uint32_t dummy)
{
	if (parse_filename) {
		DebugUI_ParseFile(parse_filename, true, true);
	} else {
		fputs("ERROR: debugger input file name to parse isn't set!\n", stderr);
	}
}

/**
 * Set which input file to parse.
 * Return true if file exists, false on error
 */
static uint32_t DebugInfo_FileArgs(int argc, char *argv[])
{
	if (argc != 1) {
		return false;
	}
	if (!File_Exists(argv[0])) {
		fprintf(stderr, "ERROR: given file '%s' doesn't exist!\n", argv[0]);
		return false;
	}
	if (parse_filename) {
		free(parse_filename);
	}
	parse_filename = strdup(argv[0]);
	return true;
}


/* ------------------------------------------------------------------
 * Debugger & readline TAB completion integration
 */

/**
 * Default information on entering the debugger
 */
static void DebugInfo_Default(FILE *fp, uint32_t dummy)
{
        uaecptr nextpc, pc = M68000_GetPC();
	fprintf(fp, "\nCPU=$%x, DSP=", pc);
	if (bDspEnabled)
		fprintf(fp, "$%x\n", DSP_GetPC());
	else
		fprintf(fp, "N/A\n");
	return; /* FIXME: for now we do not disassemble. Disasm crashes if not in super user mode. */

	Disasm(fp, pc, &nextpc, 1);
}

static const struct {
	/* if overlaps with other functionality, list only for lock command */
	bool lock;
	const char *name;
	info_func_t func;
	/* convert args in argv into single uint32_t for func */
	uint32_t (*args)(int argc, char *argv[]);
	const char *info;
} infotable[] = {
	{ true, "default",   DebugInfo_Default,    NULL, "Show default debugger entry information" },
	{ true, "disasm",    DebugInfo_CpuDisAsm,  NULL, "Disasm CPU from PC or given <address>" },
#if ENABLE_DSP_EMU
	{ true, "dspdisasm", DebugInfo_DspDisAsm,  NULL, "Disasm DSP from given <address>" },
	{ true, "dspmemdump",DebugInfo_DspMemDump, DebugInfo_DspMemArgs, "Dump DSP memory from given <space> <address>" },
	{ true, "dspregs",   DebugInfo_DspRegister,NULL, "Show DSP register contents" },
#endif
	{ true, "file",      DebugInfo_FileParse, DebugInfo_FileArgs, "Parse commands from given debugger input <file>" },
	{ true, "history",   History_Show,         NULL, "Show history of last <count> instructions" },
	{ true, "memdump",   DebugInfo_CpuMemDump, NULL, "Dump CPU memory from given <address>" },
	{ true, "regaddr",   DebugInfo_RegAddr, DebugInfo_RegAddrArgs, "Show <disasm|memdump> from CPU/DSP address pointed by <register>" },
	{ true, "registers", DebugInfo_CpuRegister,NULL, "Show CPU register contents" },
	{ false,"nvram",     NVRAM_Info,           NULL, "Show parameters stored in NVRAM" }
};

static int LockedFunction = 0; /* index for the "default" function */
static uint32_t LockedArgument;

/**
 * Show selected debugger session information
 * (when debugger is (again) entered)
 */
void DebugInfo_ShowSessionInfo(void)
{
	infotable[LockedFunction].func(stderr, LockedArgument);
}

/**
 * Return info function matching the given name, or NULL for no match
 */
info_func_t DebugInfo_GetInfoFunc(const char *name)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(infotable); i++) {
		if (strcmp(name, infotable[i].name) == 0) {
			return infotable[i].func;
		}
	}
	return NULL;
}

/**
 * Readline match callback for info subcommand name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
static char *DebugInfo_Match(const char *text, int state, bool lock)
{
	static int i, len;
	const char *name;

	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i++ < ARRAY_SIZE(infotable)) {
		if (!lock && infotable[i-1].lock) {
			continue;
		}
		name = infotable[i-1].name;
		if (strncmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}
char *DebugInfo_MatchLock(const char *text, int state)
{
	return DebugInfo_Match(text, state, true);
}
char *DebugInfo_MatchInfo(const char *text, int state)
{
	return DebugInfo_Match(text, state, false);
}


/**
 * Show requested command information.
 */
int DebugInfo_Command(int nArgc, char *psArgs[])
{
	uint32_t value;
	const char *cmd;
	bool ok, lock;
	int i, sub;

	sub = -1;
	if (nArgc > 1) {
		cmd = psArgs[1];
		/* which subcommand? */
		for (i = 0; i < ARRAY_SIZE(infotable); i++) {
			if (strcmp(cmd, infotable[i].name) == 0) {
				sub = i;
				break;
			}
		}
	}

	if (sub >= 0 && infotable[sub].args) {
		/* value needs callback specific conversion */
		value = infotable[sub].args(nArgc-2, psArgs+2);
		ok = !!value;
	} else {
		if (nArgc > 2) {
			/* value is normal number */
			ok = Eval_Number(psArgs[2], &value, NUM_TYPE_NORMAL);
		} else {
			value = 0;
			ok = true;
		}
	}

	lock = (strcmp(psArgs[0], "lock") == 0);

	if (sub < 0 || !ok) {
		/* no subcommand or something wrong with value, show info */
		fprintf(stderr, "%s subcommands are:\n", psArgs[0]);
		for (i = 0; i < ARRAY_SIZE(infotable); i++) {
			if (!lock && infotable[i].lock) {
				continue;
			}
			fprintf(stderr, "- %s: %s\n",
				infotable[i].name, infotable[i].info);
		}
		return DEBUGGER_CMDDONE;
	}

	if (lock) {
		/* lock given subcommand and value */
		LockedFunction = sub;
		LockedArgument = value;
		fprintf(stderr, "Locked %s output.\n", psArgs[1]);
	} else {
		/* do actual work */
		infotable[sub].func(stderr, value);
	}
	return DEBUGGER_CMDDONE;
}
