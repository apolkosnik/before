/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
const char HatariGlue_fileid[] = "Hatari hatari-glue.c";


#include <stdio.h>

#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"
#include "hatari-glue.h"


struct uae_prefs currprefs, changed_prefs;

/**
 * Initialize 680x0 emulation
 */
int Init680x0(void)
{
	write_log("Init680x0() called\n");

	init_m68k();

	return true;
}


/**
 * Deinitialize 680x0 emulation
 */
void Exit680x0(void)
{
	memory_uninit();

	free(table68k);
	table68k = NULL;
}


TCHAR* buf_out (TCHAR *buffer, int *bufsize, const TCHAR *format, ...)
{
	va_list parms;
	int count;

	if (buffer == NULL)
	{
		return NULL;
	}

	va_start (parms, format);
	vsnprintf (buffer, (*bufsize) - 1, format, parms);
	va_end (parms);

	count = _tcslen (buffer);
	*bufsize -= count;

	return buffer + count;
}

void error_log(const TCHAR *format, ...)
{
	va_list parms;

	assert(format && *format);

	va_start(parms, format);
	vfprintf(stderr, format, parms);
	va_end(parms);

	if (format[strlen(format) - 1] != '\n')
	{
		fputc('\n', stderr);
	}
}
