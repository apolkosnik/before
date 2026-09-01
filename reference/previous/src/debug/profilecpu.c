/*
 * Hatari - profilecpu.c
 * 
 * Copyright (C) 2010-2026 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profilecpu.c - functions for profiling CPU and showing the results.
 */
const char Profilecpu_fileid[] = "Hatari profilecpu.c";

#include "main.h"
#include "profile.h"


/* ------------------ CPU profile results ----------------- */

/**
 * Return true if there's profile data for given address, false otherwise
 */
bool Profile_CpuAddr_HasData(uint32_t addr)
{
	return false;
}

/**
 * Write string containing CPU cache stats, cycles, count, count percentage
 * for given address to provided buffer.
 *
 * Return zero if there's no profiling data for given address,
 * otherwise return the number of bytes consumed from the given buffer.
 */
int Profile_CpuAddr_DataStr(char *buffer, int maxlen, uint32_t addr)
{
	return 0;
}

/* ------------------ CPU profile control ----------------- */

/**
 * Free data from last profiling run, if any
 */
void Profile_CpuFree(void)
{
}

/**
 * Initialize CPU profiling when necessary.  Return true if profiling.
 */
bool Profile_CpuStart(void)
{
	return false;
}

/**
 * Update CPU cycle and count statistics for PC address.
 *
 * This gets called after instruction has executed and PC
 * has advanced to next instruction.
 */
void Profile_CpuUpdate(void)
{
}

/**
 * Stop and process the CPU profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_CpuStop(void)
{
}
