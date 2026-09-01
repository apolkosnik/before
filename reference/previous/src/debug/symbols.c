/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010-2026 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address information is read either from:
 * - A program file's symbol table (in DRI/GST, a.out, or ELF format), or
 * - ASCII file which contents are subset of "nm" output i.e. composed of
 *   a hexadecimal addresses followed by a space, letter indicating symbol
 *   type (T = text/code, D = data, B = BSS), space and the symbol name.
 *   Empty lines and lines starting with '#' are ignored.  It's AHCC SYM
 *   output compatible.
 */
const char Symbols_fileid[] = "Hatari symbols.c";

#include "main.h"
#include "symbols.h"
#include "debugui.h"

/**
 * Free all symbols (at exit).
 */
void Symbols_FreeAll(void)
{
}

/**
 * Readline match callbacks for CPU symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchCpuAddress(const char *text, int state)
{
	return NULL;
}
char* Symbols_MatchCpuCodeAddress(const char *text, int state)
{
	return NULL;
}
char* Symbols_MatchCpuDataAddress(const char *text, int state)
{
	return NULL;
}

/**
 * Readline match callback to list matching CPU symbols & file names.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchCpuAddrFile(const char *text, int state)
{
	return NULL;
}

/**
 * Readline match callback for DSP symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchDspAddress(const char *text, int state)
{
	return NULL;
}
char* Symbols_MatchDspCodeAddress(const char *text, int state)
{
	return NULL;
}
char* Symbols_MatchDspDataAddress(const char *text, int state)
{
	return NULL;
}

/**
 * Set given symbol's address to variable and return true if one
 * was found from given list.
 */
bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, uint32_t *addr)
{
	return false;
}
bool Symbols_GetDspAddress(symtype_t symtype, const char *name, uint32_t *addr)
{
	return false;
}

/**
 * Match (C++ method) symbol name substring, if it matches a specific
 * part of it completely, i.e. is not surrounded by valid sym char(s).
 *
 * (See debugger manual for details.)
 *
 * If there's exactly one match, set symbol's address to 'addr'
 * variable and return true, return false otherwise.
 */
bool Symbols_GetCpuMethodAddress(symtype_t symtype, const char *sub, uint32_t *addr)
{
	return false;
}

/**
 * Search symbol in given list by type & address.
 * Return symbol name if there's a match, NULL otherwise.
 * Code symbols will be matched before other symbol types.
 * Returned name is valid only until next Symbols_* function call.
 */
const char* Symbols_GetByCpuAddress(uint32_t addr, symtype_t type)
{
	return NULL;
}
const char* Symbols_GetByDspAddress(uint32_t addr, symtype_t type)
{
	return NULL;
}

/**
 * Load symbols for last opened program when symbol autoloading is enabled.
 *
 * Called when debugger is invoked and by GEMDOS HD when "exec" symload
 * is requested.
 */
void Symbols_AutoLoadCurrentProgram(const uint32_t *offsets, uint32_t maxaddr)
{
}

/* ---------------- command parsing ------------------ */

/**
 * Readline match callback for CPU symbols command.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchCpuCommand(const char *text, int state)
{
	return NULL;
}

/**
 * Readline match callback to list DSP symbols command.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchDspCommand(const char *text, int state)
{
	return NULL;
}

const char Symbols_Description[] =
	"<symbols command is not supported on Previous>";

/**
 * Handle debugger 'symbols' command and its arguments
 */
int Symbols_Command(int nArgc, char *psArgs[])
{
	return DEBUGGER_CMDDONE;
}
