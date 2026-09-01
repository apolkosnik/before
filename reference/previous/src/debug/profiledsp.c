/*
 * Hatari - profiledsp.c
 * 
 * Copyright (C) 2010-2019 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * profiledsp.c - functions for profiling DSP and showing the results.
 */
const char Profiledsp_fileid[] = "Hatari profiledsp.c";

#include "main.h"
#include "profile.h"


/* ------------------ DSP profile results ----------------- */

/**
 * Get DSP cycles, count and count percentage for given address.
 * Return true if data was available and non-zero, false otherwise.
 */
bool Profile_DspAddressData(uint16_t addr, float *percentage, uint64_t *count,
                            uint64_t *cycles, uint16_t *cycle_diff)
{
	return false;
}


/* ------------------ DSP profile control ----------------- */

/**
 * Free data from last profiling run, if any
 */
void Profile_DspFree(void)
{
}

/**
 * Initialize DSP profiling when necessary.  Return true if profiling.
 */
bool Profile_DspStart(void)
{
	return false;
}

/**
 * Update DSP cycle and count statistics for PC address.
 *
 * This is called after instruction is executed and PC points
 * to next instruction i.e. info is for previous PC address.
 */
void Profile_DspUpdate(void)
{
}

/**
 * Stop and process the DSP profiling data; collect stats and
 * prepare for more optimal sorting.
 */
void Profile_DspStop(void)
{
}
