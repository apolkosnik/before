/*
  Previous - ioMem.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the hardware.
*/
const char IoMem_fileid[] = "Previous ioMem.c";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"

#define IO_MASK 0x0001FFFF
#define IO_SIZE 0x00020000

static void (*pInterceptReadTable[IO_SIZE])(void);   /* Table with read access handlers */
static void (*pInterceptWriteTable[IO_SIZE])(void);  /* Table with write access handlers */

static uint32_t nAccessSize[IO_SIZE];
static uint32_t nAccessMask[IO_SIZE];

uint32_t IoAccessSize;                               /* Set to 1, 2 or 4 according to byte, word or long word access */
uint32_t IoAccessMask;                               /* Mask for deleting don't-care bits from the address */
uint32_t IoAccessBaseAddress;                        /* Stores the base address of the IO mem access */
uint32_t IoAccessCurrentAddress;                     /* Current byte address while handling WORD and LONG accesses */
static int nBusErrorAccesses;                        /* Needed to count bus error accesses */


/*-----------------------------------------------------------------------*/
/**
 * Set the read and write functions associated with a given 'addr' in IO mem
 */
void IoMem_Intercept ( uint32_t addr , void (*read_f)(void) , void (*write_f)(void) )
{
	pInterceptReadTable[addr] = read_f;
	pInterceptWriteTable[addr] = write_f;
}


/*-----------------------------------------------------------------------*/
/**
 * Fill a region with bus error handlers.
 */
static void IoMem_SetBusErrorRegion(uint32_t startaddr, uint32_t endaddr)
{
	uint32_t a;

	for (a = startaddr; a <= endaddr; a++)
	{
		if (a & 1)
			IoMem_Intercept ( a , IoMem_BusErrorOddReadAccess , IoMem_BusErrorOddWriteAccess );
		else
			IoMem_Intercept ( a , IoMem_BusErrorEvenReadAccess , IoMem_BusErrorEvenWriteAccess );

		nAccessSize[a] = 1;
		nAccessMask[a] = IO_MASK;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Create 'intercept' tables for hardware address access. Each 'intercept
 */
void IoMem_Init(void)
{
	uint32_t addr;
	uint32_t base;
	int i;
	const INTERCEPT_ACCESS_FUNC *pInterceptAccessFuncs = NULL;

	/* Set default IO access handler (-> bus error) */
	IoMem_SetBusErrorRegion(0, IO_SIZE - 1);

	if (ConfigureParams.System.bTurbo) {
		pInterceptAccessFuncs = IoMemTable_Turbo;
	} else {
		pInterceptAccessFuncs = IoMemTable_NEXT;
	}

	/* Now set the correct handlers */
	for (addr = 0; addr < IO_SIZE; addr++)
	{
		/* Does this hardware location/span appear in our list of possible intercepted functions? */
		for (i = 0; pInterceptAccessFuncs[i].Address != 0; i++)
		{
			base = addr & pInterceptAccessFuncs[i].Mask & ~(pInterceptAccessFuncs[i].SpanInBytes - 1);
			/* Search the list */
			if ((pInterceptAccessFuncs[i].Address & IO_MASK) == base)
			{
				/* Security checks... */
				if (pInterceptReadTable[addr] != IoMem_BusErrorEvenReadAccess && pInterceptReadTable[addr] != IoMem_BusErrorOddReadAccess)
					Log_Printf(LOG_WARN, "IoMem_Init: Warning: $%x (R) already defined\n", addr);
				if (pInterceptWriteTable[addr] != IoMem_BusErrorEvenWriteAccess && pInterceptWriteTable[addr] != IoMem_BusErrorOddWriteAccess)
					Log_Printf(LOG_WARN, "IoMem_Init: Warning: $%x (W) already defined\n", addr);

				/* This location needs to be intercepted, so add entry to list */
				IoMem_Intercept ( addr , pInterceptAccessFuncs[i].ReadFunc , pInterceptAccessFuncs[i].WriteFunc );
				nAccessSize[addr] = pInterceptAccessFuncs[i].SpanInBytes;
				nAccessMask[addr] = pInterceptAccessFuncs[i].Mask;
			}
		}
	}
}


/**
 * Uninitialize the IoMem code (currently unused).
 */
void IoMem_UnInit(void)
{
}


/*-----------------------------------------------------------------------*/
/**
 * Special function for handling long write access to byte wide port.
 */
uint8_t IoMem_ReadBytePort(void)
{
	uint32_t val = 0;

	Log_Printf(LOG_WARN, "IO byte port access at %08x", IoAccessBaseAddress);

	do {
		val |= IoMem_ReadByte(IoAccessCurrentAddress);
		IoAccessCurrentAddress += SIZE_BYTE;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_LONG);

	IoMem_WriteLong(IoAccessBaseAddress, val<<24);

	return val;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle byte read access from IO memory.
 */
uae_u32 IoMem_bget(uaecptr addr)
{
	uint8_t val;

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	pInterceptReadTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */

	/* Check if we read from a bus-error region */
	if (nBusErrorAccesses == SIZE_BYTE)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	val = IoMem_ReadByte(addr);

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.b $%08x = $%02x\n", addr, val);

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word read access from IO memory.
 */
uae_u32 IoMem_wget(uaecptr addr)
{
	uint16_t val;

	if (addr & (SIZE_WORD - 1))
	{
		Log_Printf(LOG_WARN, "IO wget: Unaligned address %08x", addr);
		return 0;
	}

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	do {
		pInterceptReadTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */
		IoAccessCurrentAddress += IoAccessSize;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_WORD);

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == SIZE_WORD)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	val = IoMem_ReadWord(addr);

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.w $%08x = $%04x\n", addr, val);

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word read access from IO memory.
 */
uae_u32 IoMem_lget(uaecptr addr)
{
	uint32_t val;

	if (addr & (SIZE_LONG - 1))
	{
		Log_Printf(LOG_WARN, "IO lget: Unaligned address %08x", addr);
		return 0;
	}

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	do {
		pInterceptReadTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */
		IoAccessCurrentAddress += IoAccessSize;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_LONG);

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == SIZE_LONG)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	val = IoMem_ReadLong(addr);

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.l $%08x = $%08x\n", addr, val);

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle byte write access to IO memory.
 */
void IoMem_bput(uaecptr addr, uae_u32 val)
{
	LOG_TRACE(TRACE_IOMEM_WR, "IO write.b $%08x = $%02x\n", addr, val&0xff);

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	switch (IoAccessSize) {
		case SIZE_LONG:
			IoMem_WriteByte(IoAccessBaseAddress + 3, val);
			IoMem_WriteByte(IoAccessBaseAddress + 2, val);
		case SIZE_WORD:
			IoMem_WriteByte(IoAccessBaseAddress + 1, val);
		default:
			IoMem_WriteByte(IoAccessBaseAddress, val);
			break;
	}

	do {
		pInterceptWriteTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */
		IoAccessCurrentAddress += IoAccessSize;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_BYTE);

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == SIZE_BYTE)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, val);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word write access to IO memory.
 */
void IoMem_wput(uaecptr addr, uae_u32 val)
{
	LOG_TRACE(TRACE_IOMEM_WR, "IO write.w $%08x = $%04x\n", addr, val&0xffff);

	if (addr & (SIZE_WORD - 1))
	{
		Log_Printf(LOG_WARN, "IO wput: Unaligned address %08x", addr);
		return;
	}

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	switch (IoAccessSize) {
		case SIZE_LONG:
			IoMem_WriteWord(IoAccessBaseAddress + 2, val);
		default:
			IoMem_WriteWord(IoAccessBaseAddress, val);
			break;
	}

	do {
		pInterceptWriteTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */
		IoAccessCurrentAddress += IoAccessSize;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_WORD);

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == SIZE_WORD)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, val);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word write access to IO memory.
 */
void IoMem_lput(uaecptr addr, uae_u32 val)
{
	LOG_TRACE(TRACE_IOMEM_WR, "IO write.l $%08x = $%08x\n", addr, val);

	if (addr & (SIZE_LONG - 1))
	{
		Log_Printf(LOG_WARN, "IO lput: Unaligned address %08x", addr);
		return;
	}

	IoAccessMask = nAccessMask[addr & IO_MASK];
	IoAccessSize = nAccessSize[addr & IO_MASK];
	IoAccessCurrentAddress = addr;  /* Store access location */
	IoAccessBaseAddress = addr & ~(IoAccessSize - 1);

	nBusErrorAccesses = 0;

	IoMem_WriteLong(IoAccessBaseAddress, val);

	do {
		pInterceptWriteTable[IoAccessCurrentAddress & IO_MASK]();   /* Call handler */
		IoAccessCurrentAddress += IoAccessSize;
	} while (IoAccessCurrentAddress < IoAccessBaseAddress + SIZE_LONG);

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == SIZE_LONG)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, val);
	}
}


/*-------------------------------------------------------------------------*/
/**
 * This handler will be called if a program tries to read from an address
 * that causes a bus error on a real machine. However, we can't call M68000_BusError()
 * directly: For example, a "move.b $ff8204,d0" triggers a bus error on a real ST,
 * while a "move.w $ff8204,d0" works! So we have to count the accesses to bus error
 * addresses and we only trigger a bus error later if the count matches the complete
 * access size (e.g. nBusErrorAccesses==4 for a long word access).
 */
void IoMem_BusErrorEvenReadAccess(void)
{
	nBusErrorAccesses += 1;
	Log_Printf(LOG_WARN,"Bus error $%08x PC=$%08x %s at %d", IoAccessCurrentAddress,regs.pc,__FILE__,__LINE__);
}

/**
 * We need two handler so that the IoMem_*get functions can distinguish
 * consecutive addresses.
 */
void IoMem_BusErrorOddReadAccess(void)
{
	nBusErrorAccesses += 1;
	Log_Printf(LOG_WARN,"Bus error $%08x PC=$%08x %s at %d", IoAccessCurrentAddress,regs.pc,__FILE__,__LINE__);
}

/*-------------------------------------------------------------------------*/
/**
 * Same as IoMem_BusErrorReadAccess() but for write access this time.
 */
void IoMem_BusErrorEvenWriteAccess(void)
{
	nBusErrorAccesses += 1;
	Log_Printf(LOG_WARN,"Bus error $%08x PC=$%08x %s at %d", IoAccessCurrentAddress,regs.pc,__FILE__,__LINE__);
}

/**
 * We need two handler so that the IoMem_*put functions can distinguish
 * consecutive addresses.
 */
void IoMem_BusErrorOddWriteAccess(void)
{
	nBusErrorAccesses += 1;
	Log_Printf(LOG_WARN,"Bus error $%08x PC=$%08x %s at %d", IoAccessCurrentAddress,regs.pc,__FILE__,__LINE__);
}

/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for read access.
 */
void IoMem_ReadWithoutInterception(void)
{
	/* Nothing... */
}

/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for write access.
 */
void IoMem_WriteWithoutInterception(void)
{
	/* Nothing... */
}

/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for read access.
 */
void IoMem_ReadWithoutInterceptionButTrace(void)
{
	Log_Printf(LOG_WARN,"IO read at $%08x PC=$%08x\n", IoAccessCurrentAddress,regs.pc);
}

/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for write access.
 */
void IoMem_WriteWithoutInterceptionButTrace(void)
{
	Log_Printf(LOG_WARN,"IO write at $%08x val=%02x PC=$%08x\n", IoAccessCurrentAddress,IoMem_ReadByte(IoAccessCurrentAddress),regs.pc);
}
