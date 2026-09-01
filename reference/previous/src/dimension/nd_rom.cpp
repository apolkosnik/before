/*
  Previous - nd_rom.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "file.h"
#include "dimension.hpp"
#include "nd_mem.hpp"
#include "log.h"

/* --------- NEXTDIMENSION EEPROM --------- *
 *                                          *
 *  Intel 28F010 (1024k CMOS Flash Memory)  *
 *                                          */

/* NeXTdimension board memory */
#define ROM_CMD_READ    0x00
#define ROM_CMD_READ_ID 0x90
#define ROM_CMD_ERASE   0x20
#define ROM_CMD_WRITE   0x40
#define ROM_CMD_VERIFY  0x80
#define ROM_CMD_RESET   0xFF

#define ROM_ID_MFG      0x89
#define ROM_ID_DEV      0xB4

uint8_t NextDimension::rom_read(uint32_t addr) {
    switch (rom_command) {
        case ROM_CMD_READ:
            return rom[addr];
        case ROM_CMD_READ_ID:
            switch (addr) {
                case 0: return ROM_ID_MFG;
                case 1: return ROM_ID_DEV;
                default: return ROM_ID_DEV;
            }
        case (ROM_CMD_ERASE|ROM_CMD_VERIFY):
            rom_command = ROM_CMD_READ;
            return rom[addr];
        case (ROM_CMD_WRITE|ROM_CMD_VERIFY):
            rom_command = ROM_CMD_READ;
            return rom[rom_last_addr];
        case ROM_CMD_RESET:
        default:
            return 0;
    }
}

void NextDimension::rom_write(uint32_t addr, uint8_t val) {
    int i;
    
    switch (rom_command) {
        case ROM_CMD_WRITE:
            Log_Printf(LOG_WARN, "[ND] Slot %i: Writing ROM (addr=%04X, val=%02X)", slot,addr,val);
            rom[addr]     = val;
            rom_last_addr = addr;
            rom_command   = ROM_CMD_READ;
            break;
        case ROM_CMD_ERASE:
            if (val==ROM_CMD_ERASE) {
                Log_Printf(LOG_WARN, "[ND] Slot %i: Erasing ROM",slot);
                for (i = 0; i < (128*1024); i++)
                    rom[i] = 0xFF;
                break;
            } /* else fall through */
        default:
            Log_Printf(LOG_WARN, "[ND] Slot %i: ROM Command %02X",slot,val);
            rom_command = val;
            break;
    }
}


/* Load NeXTdimension ROM from file */
void NextDimension::rom_load() {
    FILE* romfile;
    
    rom_command   = ROM_CMD_READ;
    rom_last_addr = 0;
    
    if (!File_Exists(ConfigureParams.Dimension.board[ND_NUM(slot)].szRomFileName)) {
        Log_Printf(LOG_WARN, "[ND] Error: ROM file does not exist or is not readable");
        return;
    }
    
    romfile = File_Open(ConfigureParams.Dimension.board[ND_NUM(slot)].szRomFileName, "rb");
    
    if (romfile==NULL) {
        Log_Printf(LOG_WARN, "[ND] Error: Cannot open ROM file");
        return;
    }
    
    fread(rom, 1, 128 * 1024, romfile);
    
    Log_Printf(LOG_WARN, "[ND] Read ROM from %s",ConfigureParams.Dimension.board[ND_NUM(slot)].szRomFileName);
    
    File_Close(romfile);
}
