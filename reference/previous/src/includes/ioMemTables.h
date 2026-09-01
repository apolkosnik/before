/*
  Hatari - ioMemTables.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IOMEMTABLES_H
#define HATARI_IOMEMTABLES_H

/* Hardware address details */
typedef struct
{
	const uint32_t Address;   /* Hardware address */
	const uint32_t Mask;      /* Mask */
	const int SpanInBytes;    /* SIZE_BYTE, SIZE_WORD or SIZE_LONG */
	void (*ReadFunc)(void);   /* Read function */
	void (*WriteFunc)(void);  /* Write function */
} INTERCEPT_ACCESS_FUNC;

extern const INTERCEPT_ACCESS_FUNC IoMemTable_NEXT[];
extern const INTERCEPT_ACCESS_FUNC IoMemTable_Turbo[];

#endif
