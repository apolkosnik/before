/*
  Previous - printer.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_PRINTER_H
#define PREV_PRINTER_H

extern void LP_CSR0_Read(void);
extern void LP_CSR0_Write(void);
extern void LP_CSR1_Read(void);
extern void LP_CSR1_Write(void);
extern void LP_CSR2_Read(void);
extern void LP_CSR2_Write(void);
extern void LP_CSR3_Read(void);
extern void LP_CSR3_Write(void);
extern void LP_Data_Read(void);
extern void LP_Data_Write(void);

typedef struct {
    uint8_t data[64*1024];
    int size;
    int limit;
} PrinterBuffer;

extern PrinterBuffer lp_buffer;

extern void Printer_UnInit(void);
extern void Printer_Reset(void);
extern void Printer_IO_Handler(void);

#endif /* PREV_PRINTER_H */
