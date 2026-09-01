/*
  Previous - floppy.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_FLOPPY_H
#define PREV_FLOPPY_H

extern void FLP_StatA_Read(void);
extern void FLP_StatB_Read(void);
extern void FLP_DataOut_Read(void);
extern void FLP_DataOut_Write(void);
extern void FLP_MainStatus_Read(void);
extern void FLP_DataRate_Write(void);
extern void FLP_FIFO_Read(void);
extern void FLP_FIFO_Write(void);
extern void FLP_DataIn_Read(void);
extern void FLP_Configuration_Write(void);
extern void FLP_Reserved_Read(void);
extern void FLP_Reserved_Write(void);

extern void FLP_Status_Read(void);
extern void FLP_Control_Write(void);

extern void Floppy_IO_Handler(void);

extern void Floppy_Reset(void);
extern int  Floppy_Insert(int drive);
extern void Floppy_Eject(int drive);

typedef struct {
    uint8_t data[1024];
    uint32_t size;
    uint32_t limit;
} FloppyBuffer;

extern FloppyBuffer flp_buffer;

extern uint8_t floppy_select;
extern void set_floppy_select(uint8_t sel, bool osp);

#endif /* PREV_FLOPPY_H */
