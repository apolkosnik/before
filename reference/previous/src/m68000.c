/*
  Previous - m68000.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char M68000_fileid[] = "Previous m68000.c";

#include "main.h"
#include "configuration.h"
#include "hatari-glue.h"
#include "m68000.h"
#include "cpummu.h"
#include "cpummu030.h"


/**
 * One-time CPU initialization.
 */
void M68000_Init(void)
{
	/* Init UAE CPU core */
	Init680x0();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset 680x0 emualtion
 */
void M68000_Reset(void)
{
	M68000_SetSpecial(SPCFLAG_MODE_CHANGE);
}


/*-----------------------------------------------------------------------*/
/**
 * Stop 680x0 emulation
 */
void M68000_Stop(void)
{
	M68000_SetSpecial(SPCFLAG_BRK);
}


/*-----------------------------------------------------------------------*/
/**
 * Start 680x0 emulation
 */
void M68000_Start(void)
{
	m68k_go(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Set interrupt flag to indicate a change of the interrupt level
 */
void M68000_CheckInterrupt(void)
{
	M68000_SetSpecial(SPCFLAG_INT);
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether CPU settings have been changed.
 */
void M68000_CheckCpuSettings(void)
{
#if 0
	if (ConfigureParams.System.nCpuFreq < 20) {
		ConfigureParams.System.nCpuFreq = 16;
	} else if (ConfigureParams.System.nCpuFreq < 25) {
		ConfigureParams.System.nCpuFreq = 20;
	} else if (ConfigureParams.System.nCpuFreq < 33) {
		ConfigureParams.System.nCpuFreq = 25;
	} else if (ConfigureParams.System.nCpuFreq < 40) {
		ConfigureParams.System.nCpuFreq = 33;
	} else {
		if (ConfigureParams.System.bTurbo) {
			ConfigureParams.System.nCpuFreq = 40;
		} else {
			ConfigureParams.System.nCpuFreq = 33;
		}
	}
#else
	if (ConfigureParams.System.nCpuFreq < 1) {
		ConfigureParams.System.nCpuFreq = 1;
	}
	if (ConfigureParams.System.nCpuFreq > 1000) {
		ConfigureParams.System.nCpuFreq = 1000;
	}
#endif
	changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;

	switch (ConfigureParams.System.nCpuLevel) {
		case 0 : changed_prefs.cpu_model = 68000; break;
		case 1 : changed_prefs.cpu_model = 68010; break;
		case 2 : changed_prefs.cpu_model = 68020; break;
		case 3 : changed_prefs.cpu_model = 68030; break;
		case 4 : changed_prefs.cpu_model = 68040; break;
		case 5 : changed_prefs.cpu_model = 68060; break;
		default: fprintf (stderr, "M68000_CheckCpuSettings(): Error, cpu_model unknown\n");
	}
	changed_prefs.mmu_model = changed_prefs.cpu_model;
	changed_prefs.fpu_model = ConfigureParams.System.n_FPUType;
	
	switch (ConfigureParams.System.n_FPUType) {
		case 68881: changed_prefs.fpu_revision = 0x1f; break;
		case 68882: changed_prefs.fpu_revision = 0x20; break;
		case 68040:
			if (ConfigureParams.System.bTurbo)
				changed_prefs.fpu_revision = 0x41;
			else
				changed_prefs.fpu_revision = 0x40;
			break;
		default: fprintf (stderr, "M68000_CheckCpuSettings(): Error, fpu_model unknown\n");
	}

	/* Hard coded for Previous */
	changed_prefs.cpu_compatible = false;
	changed_prefs.cpu_cycle_exact = false;
	changed_prefs.cpu_memory_cycle_exact = false;
	changed_prefs.mmu_ec = false;
	changed_prefs.int_no_unimplemented = true;
	changed_prefs.fpu_no_unimplemented = true;
	changed_prefs.address_space_24 = false;
	changed_prefs.cpu_data_cache = false;
	changed_prefs.cachesize = 0;

	check_prefs_changed_cpu();
}


/**
 * BUSERROR - Access outside valid memory range.
 *   ReadWrite : BUS_ERROR_READ in case of a read or BUS_ERROR_WRITE in case of write
 *   Size : BUS_ERROR_SIZE_BYTE or BUS_ERROR_SIZE_WORD or BUS_ERROR_SIZE_LONG
 *   AccessType : BUS_ERROR_ACCESS_INSTR or BUS_ERROR_ACCESS_DATA
 *   val : value we wanted to write in case of a BUS_ERROR_WRITE
 */
void M68000_BusError ( uint32_t addr , int ReadWrite , int Size , int AccessType , uae_u32 val )
{
	LOG_TRACE(TRACE_CPU_EXCEPTION, "Bus error %s at address $%x PC=$%x.\n",
	          ReadWrite ? "reading" : "writing", addr, M68000_InstrPC);

#define WINUAE_HANDLE_BUS_ERROR
#ifdef WINUAE_HANDLE_BUS_ERROR

	bool	read , ins;
	int	size;

	if ( ReadWrite == BUS_ERROR_READ )		read = true; else read = false;
	if ( AccessType == BUS_ERROR_ACCESS_INSTR )	ins = true; else ins = false;
	if ( Size == BUS_ERROR_SIZE_BYTE )		size = sz_byte;
	else if ( Size == BUS_ERROR_SIZE_WORD )		size = sz_word;
	else						size = sz_long;
	hardware_exception2 ( addr , val , read , ins , size );
#else
	/* With WinUAE's cpu, on a bus error instruction will be correctly aborted before completing, */
	/* so we don't need to check if the opcode already generated a bus error or not */
	exception2 ( addr , ReadWrite , Size , AccessType );
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Debugger memory access
 */
uint32_t M68000_ReadLong(uint32_t addr)
{
	switch (ConfigureParams.System.nCpuLevel)
	{
		case 3: return get_long_mmu030(addr);
		case 4: return get_long_mmu040(addr);
		default: return 0;
	}
}

uint16_t M68000_ReadWord(uint32_t addr)
{
	switch (ConfigureParams.System.nCpuLevel)
	{
		case 3: return get_word_mmu030(addr);
		case 4: return get_word_mmu040(addr);
		default: return 0;
	}
}

uint8_t M68000_ReadByte(uint32_t addr)
{
	switch (ConfigureParams.System.nCpuLevel)
	{
		case 3: return get_byte_mmu030(addr);
		case 4: return get_byte_mmu040(addr);
		default: return 0;
	}
}

void M68000_WriteLong(uint32_t addr, uint32_t val)
{
	switch (ConfigureParams.System.nCpuLevel)
	{
		case 3: put_long_mmu030(addr, val); break;
		case 4: put_long_mmu040(addr, val); break;
		default: break;
	}
}

void M68000_WriteWord(uint32_t addr, uint16_t val) {
	switch (ConfigureParams.System.nCpuLevel) {
		case 3: put_word_mmu030(addr, val); break;
		case 4: put_word_mmu040(addr, val); break;
		default: break;
	}
}

void M68000_WriteByte(uint32_t addr, uint8_t val)
{
	switch (ConfigureParams.System.nCpuLevel)
	{
		case 3: put_byte_mmu030(addr, val); break;
		case 4: put_byte_mmu040(addr, val); break;
		default: break;
	}
}
