/*
  Previous - scc.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_SCC_H
#define PREV_SCC_H

extern void SCC_ControlA_Read(void);
extern void SCC_ControlA_Write(void);
extern void SCC_ControlB_Read(void);
extern void SCC_ControlB_Write(void);
extern void SCC_DataA_Read(void);
extern void SCC_DataA_Write(void);
extern void SCC_DataB_Read(void);
extern void SCC_DataB_Write(void);
extern void SCC_Clock_Read(void);
extern void SCC_Clock_Write(void);

extern void SCC_Reset(void);

extern void SCC_IO_Handler(void);

extern void scc_receive(int ch, uint8_t val);

#endif /* PREV_SCC_H */
