/*
  Hatari - vars.c

  Copyright (c) 2016 by Eero Tamminen

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  vars.c - Hatari internal variable value and OS call number accessors
  for conditional breakpoint and evaluate commands.
*/
const char Vars_fileid[] = "Hatari vars.c";

#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "configuration.h"
#include "m68000.h"

#include "debugInfo.h"
#include "debugcpu.h"
#include "debugdsp.h"
#include "debugui.h"
#include "symbols.h"
#include "68kDisass.h"
#include "vars.h"


static uint32_t GetCycleCounter(void)
{
	/* 64-bit, so only lower 32-bits are returned */
	return nCyclesMainCounter;
}

static uint32_t GetNextPC(void)
{
	return Disasm_GetNextPC(M68000_GetPC());
}

/* sorted by variable name so that this can be bisected */
static const var_addr_t hatari_vars[] = {
	{ "CpuCallDepth", (uint32_t*)DebugCpu_CallDepth, VALUE_TYPE_FUNCTION32, 0, NULL }, /* call depth for 'next subreturn' */
	{ "CpuInstr", (uint32_t*)DebugCpu_InstrCount, VALUE_TYPE_FUNCTION32, 0, "CPU instructions count" },
	{ "CpuOpcodeType", (uint32_t*)DebugCpu_OpcodeType, VALUE_TYPE_FUNCTION32, 0, NULL }, /* opcode type for 'next' */
	{ "CycleCounter", (uint32_t*)GetCycleCounter, VALUE_TYPE_FUNCTION32, 0, "global cycles counter (lower 32 bits)" },
#if ENABLE_DSP_EMU
	{ "DspCallDepth", (uint32_t*)DebugDsp_CallDepth, VALUE_TYPE_FUNCTION32, 0, NULL }, /* call depth for 'dspnext subreturn' */
	{ "DspInstr", (uint32_t*)DebugDsp_InstrCount, VALUE_TYPE_FUNCTION32, 0, "DSP instructions count" },
	{ "DspOpcodeType", (uint32_t*)DebugDsp_OpcodeType, VALUE_TYPE_FUNCTION32, 0, NULL }, /* opcode type for 'dspnext' */
#endif
	{ "NextPC", (uint32_t*)GetNextPC, VALUE_TYPE_FUNCTION32, 0, "Next instruction address" }
};


/**
 * Readline match callback for Hatari variable and CPU variable/symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Vars_MatchCpuVariable(const char *text, int state)
{
	static int i, len;
	const char *name;

	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAY_SIZE(hatari_vars)) {
		name = hatari_vars[i++].name;
		if (strncasecmp(name, text, len) == 0)
			return (strdup(name));
	}
	/* no variable match, check all CPU symbols */
	return Symbols_MatchCpuAddress(text, state);
}


/**
 * If given string matches Hatari variable name, return its struct pointer,
 * otherwise return NULL.
 */
const var_addr_t *Vars_ParseVariable(const char *name)
{
	const var_addr_t *hvar;
	/* left, right, middle, direction */
        int l, r, m, dir;

	/* bisect */
	l = 0;
	r = ARRAY_SIZE(hatari_vars) - 1;
	do {
		m = (l+r) >> 1;
		hvar = hatari_vars + m;
		dir = strcasecmp(name, hvar->name);
		if (dir == 0) {
			return hvar;
		}
		if (dir < 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return NULL;
}


/**
 * Return uint32_t value from given Hatari variable struct*
 */
uint32_t Vars_GetValue(const var_addr_t *hvar)
{
	switch (hvar->vtype) {
	case VALUE_TYPE_FUNCTION32:
		return ((uint32_t(*)(void))(hvar->addr))();
	case VALUE_TYPE_VAR32:
		return *(hvar->addr);
	default:
		fprintf(stderr, "ERROR: variable '%s' has unsupported type '%d'\n",
			hvar->name, hvar->vtype);
		exit(-1);
	}
}


/**
 * If given string is a Hatari variable name, set value to given
 * variable's value and return true, otherwise return false.
 */
bool Vars_GetVariableValue(const char *name, uint32_t *value)
{
	const var_addr_t *hvar;

	if (!(hvar = Vars_ParseVariable(name))) {
		return false;
	}
	*value = Vars_GetValue(hvar);
	return true;
}


/**
 * List Hatari variable names & current values
 */
int Vars_List(int nArgc, char *psArgv[])
{
	uint32_t value;
	char numstr[16];
	int i, maxlen = 0;
	for (i = 0; i < ARRAY_SIZE(hatari_vars); i++) {
		int len = strlen(hatari_vars[i].name);
		if (len > maxlen) {
			maxlen = len;
		}
	}
	fputs("Hatari debugger builtin symbols and their values are:\n", stderr);
	for (i = 0; i < ARRAY_SIZE(hatari_vars); i++) {
		const var_addr_t *hvar = &hatari_vars[i];
		if (!hvar->info) {
			/* debugger internal variables don't have descriptions */
			continue;
		}
		value = Vars_GetValue(hvar);
		if (hvar->bits == 16) {
			fprintf(stderr, " %*s:     $%04X", maxlen, hvar->name, value);
		} else {
			fprintf(stderr, " %*s: $%08X", maxlen, hvar->name, value);
		}
		sprintf(numstr, "(%d)", value);
		fprintf(stderr, " %-*s %s\n", 12, numstr, hvar->info);
	}
	fputs("Some of the variables are valid only in specific situations.\n", stderr);
	return DEBUGGER_CMDDONE;
}
