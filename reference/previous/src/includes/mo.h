/*
  Previous - mo.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_MO_H
#define PREV_MO_H

extern void MO_Reset(void);
extern void MO_Insert(int disk);
extern void MO_Eject(int disk);

extern void MO_Interrupt_Handler(void);
extern void MO_IO_Handler(void);
extern void MO_ECC_IO_Handler(void);

typedef struct {
    uint8_t data[1296];
    uint32_t size;
    uint32_t limit;
} OpticalDiskBuffer;

extern OpticalDiskBuffer ecc_buffer[2];

extern int eccin;
extern int eccout;

extern void MO_TrackNumH_Read(void);
extern void MO_TrackNumH_Write(void);
extern void MO_TrackNumL_Read(void);
extern void MO_TrackNumL_Write(void);
extern void MO_SectorIncr_Read(void);
extern void MO_SectorIncr_Write(void);
extern void MO_SectorCnt_Read(void);
extern void MO_SectorCnt_Write(void);
extern void MO_IntStatus_Read(void);
extern void MO_IntStatus_Write(void);
extern void MO_IntMask_Read(void);
extern void MO_IntMask_Write(void);
extern void MOctrl_CSR2_Read(void);
extern void MOctrl_CSR2_Write(void);
extern void MOctrl_CSR1_Read(void);
extern void MOctrl_CSR1_Write(void);
extern void MO_CSR_L_Read(void);
extern void MO_CSR_L_Write(void);
extern void MO_CSR_H_Read(void);
extern void MO_CSR_H_Write(void);
extern void MO_ErrStat_Read(void);
extern void MO_EccCnt_Read(void);
extern void MO_Init_Write(void);
extern void MO_Format_Write(void);
extern void MO_Mark_Write(void);
extern void MO_Flag0_Read(void);
extern void MO_Flag0_Write(void);
extern void MO_Flag1_Read(void);
extern void MO_Flag1_Write(void);
extern void MO_Flag2_Read(void);
extern void MO_Flag2_Write(void);
extern void MO_Flag3_Read(void);
extern void MO_Flag3_Write(void);
extern void MO_Flag4_Read(void);
extern void MO_Flag4_Write(void);
extern void MO_Flag5_Read(void);
extern void MO_Flag5_Write(void);
extern void MO_Flag6_Read(void);
extern void MO_Flag6_Write(void);

#endif /* PREV_MO_H */
