/*
 * Hatari - profile.c
 *
 * Copyright (C) 2010-2026 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profile.c - profile caller info handling and debugger parsing functions
 */
const char Profile_fileid[] = "Hatari profile.c";

#include "main.h"
#include "debugui.h"
#include "profile.h"


/* ------------------- command parsing ---------------------- */

/**
 * Readline match callback to list profile subcommand names.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Profile_Match(const char *text, int state)
{
	return NULL;
}

const char Profile_Description[] =
	  "<profile command is not supported on Previous>";


/**
 * Command: CPU/DSP profiling enabling, exec stats, cycle and call stats.
 * Returns DEBUGGER_CMDDONE or DEBUGGER_CMDCONT.
 */
int Profile_Command(int nArgc, char *psArgs[], bool bForDsp)
{
	return DEBUGGER_CMDDONE;
}
