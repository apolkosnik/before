/*
  Hatari - m68000.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/


#ifndef HATARI_M68000_H
#define HATARI_M68000_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "sysdeps.h"
#include "memory.h"
#include "newcpu.h"     /* for regs */
#include "cycInt.h"
#include "log.h"
#include "configuration.h"

/* 68000 register defines */
enum {
  REG_D0,    /* D0.. */
  REG_D1,
  REG_D2,
  REG_D3,
  REG_D4,
  REG_D5,
  REG_D6,
  REG_D7,    /* ..D7 */
  REG_A0,    /* A0.. */
  REG_A1,
  REG_A2,
  REG_A3,
  REG_A4,
  REG_A5,
  REG_A6,
  REG_A7    /* ..A7 (also SP) */
};


/* Ugly hacks to adapt the main code to the different CPU cores: */

#define Regs regs.regs

# define M68000_GetPC()     m68k_getpc()
# define M68000_SetPC(val)  m68k_setpc(val)

static inline uint16_t M68000_GetSR(void)
{
	MakeSR();
	return regs.sr;
}
static inline void M68000_SetSR(uint16_t v)
{
	regs.sr = v;
	MakeFromSR();
}


# define M68000_InstrPC		regs.instruction_pc

# define M68000_SetSpecial(flags)   set_special(flags)
# define M68000_UnsetSpecial(flags) unset_special(flags)


/* Some define's for bus error (see newcpu.c) */
/* Bus error read/write mode */
#define BUS_ERROR_WRITE		0
#define BUS_ERROR_READ		1
/* Bus error access size */
#define BUS_ERROR_SIZE_BYTE	1
#define BUS_ERROR_SIZE_WORD	2
#define BUS_ERROR_SIZE_LONG	4
/* Bus error access type */
#define BUS_ERROR_ACCESS_INSTR	0
#define BUS_ERROR_ACCESS_DATA	1


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles.
 */
static inline void M68000_AddCycles(int cycles) {
	CycInt_AddCycles(cycles);
}

extern void M68000_Init(void);
extern void M68000_Reset(void);
extern void M68000_Stop(void);
extern void M68000_Start(void);
extern void M68000_CheckInterrupt(void);
extern void M68000_CheckCpuSettings(void);
extern void M68000_BusError (uint32_t addr, int ReadWrite, int Size, int AccessType, uae_u32 val);

extern uint32_t M68000_ReadLong(uint32_t addr);
extern uint16_t M68000_ReadWord(uint32_t addr);
extern uint8_t  M68000_ReadByte(uint32_t addr);
extern void M68000_WriteLong(uint32_t addr,uint32_t val);
extern void M68000_WriteWord(uint32_t addr,uint16_t val);
extern void M68000_WriteByte(uint32_t addr,uint8_t  val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
