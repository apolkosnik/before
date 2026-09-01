/*
	DSP M56001 emulation
	Dummy emulation, Hatari glue

	(C) 2001-2008 ARAnyM developer team
	Adaption to Hatari (C) 2008 by Thomas Huth

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DSP_H
#define DSP_H

#if ENABLE_DSP_EMU
# include "dsp_core.h"
#endif

extern bool bDspEnabled;

/* Dsp commands */
extern void DSP_Init(void);
extern void DSP_UnInit(void);
extern void DSP_Reset(void);
extern void DSP_Start(uint8_t mode);
extern void DSP_Run(int nHostCycles);
extern void DSP_EnableMemory(void);
extern void DSP_DisableMemory(void);
extern void DSP_InitSSI(uint32_t value);

/* Save Dsp state to snapshot */
extern void DSP_MemorySnapShot_Capture(bool bSave);

/* Dsp Debugger commands */
extern void DSP_SetDebugging(bool enabled);
extern uint16_t DSP_GetPC(void);
extern uint16_t DSP_GetNextPC(uint16_t pc);
extern uint16_t DSP_GetInstrCycles(void);
extern uint32_t DSP_ReadMemory(uint16_t addr, char space, const char **mem_str);
extern uint16_t DSP_DisasmMemory(FILE *fp, uint16_t dsp_memdump_addr, uint16_t dsp_memdump_upper, char space);
extern uint16_t DSP_DisasmAddress(FILE *out, uint16_t lowerAdr, uint16_t UpperAdr);
extern void DSP_Info(FILE *fp, uint32_t dummy);
extern void DSP_DisasmRegisters(FILE *fp);
extern int DSP_GetRegisterAddress(const char *arg, uint32_t **addr, uint32_t *mask);
extern bool DSP_Disasm_SetRegister(const char *arg, uint32_t value);

/* Dsp SSI commands */
extern uint32_t DSP_SsiReadTxValue(void);
extern void DSP_SsiWriteRxValue(uint32_t value);
extern void DSP_SsiReceive_SC0(void);
extern void DSP_SsiReceive_SC1(uint32_t value);
extern void DSP_SsiReceive_SC2(uint32_t value);
extern void DSP_SsiReceive_SCK(void);
extern void DSP_SsiTransmit_SC0(void);
extern void DSP_SsiTransmit_SC1(void);
extern void DSP_SsiTransmit_SC2(uint32_t frame);
extern void DSP_SsiTransmit_SCK(void);

/* Dsp SCI functions */
extern void DSP_HandleTXD(int set);

/* Dsp Host interface commands */
extern void DSP_HandleReadAccess(void);
extern void DSP_HandleWriteAccess(void);

extern void DSP_ICR_Read(void);
extern void DSP_ICR_Write(void);
extern void DSP_CVR_Read(void);
extern void DSP_CVR_Write(void);
extern void DSP_ISR_Read(void);
extern void DSP_ISR_Write(void);
extern void DSP_IVR_Read(void);
extern void DSP_IVR_Write(void);

extern void DSP_Data0_Read(void);
extern void DSP_Data0_Write(void);
extern void DSP_Data1_Read(void);
extern void DSP_Data1_Write(void);
extern void DSP_Data2_Read(void);
extern void DSP_Data2_Write(void);
extern void DSP_Data3_Read(void);
extern void DSP_Data3_Write(void);

extern void DSP_SetIRQB(void);


#endif /* DSP_H */
