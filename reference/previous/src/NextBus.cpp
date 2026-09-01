/*
  Previous - NextBus.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "configuration.h"
#include "m68000.h"
#include "NextBus.hpp"
#include "nbic.h"
#include "dimension.hpp"

static uint8_t bus_error(uint32_t addr, int read, int size, uint32_t val, const char* acc) {
    Log_Printf(LOG_WARN, "[NextBus] Bus error %s at %08X", acc, addr);
    M68000_BusError(addr, read, size, BUS_ERROR_ACCESS_DATA, val);
    return 0;
}

NextBusSlot::NextBusSlot(int slot) : slot(slot) {}
NextBusSlot::~NextBusSlot() {}

uint32_t NextBusSlot::slot_lget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, 0, "lget");}
uint16_t NextBusSlot::slot_wget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, 0, "wget");}
uint8_t  NextBusSlot::slot_bget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, 0, "bget");}

uint32_t NextBusSlot::board_lget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, 0, "lget");}
uint16_t NextBusSlot::board_wget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, 0, "wget");}
uint8_t  NextBusSlot::board_bget(uint32_t addr) {return bus_error(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, 0, "bget");}

void NextBusSlot::slot_lput(uint32_t addr, uint32_t val) {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, val, "lput");}
void NextBusSlot::slot_wput(uint32_t addr, uint16_t val) {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, val, "wput");}
void NextBusSlot::slot_bput(uint32_t addr, uint8_t val)  {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, val, "bput");}

void NextBusSlot::board_lput(uint32_t addr, uint32_t val) {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, val, "lput");}
void NextBusSlot::board_wput(uint32_t addr, uint16_t val) {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, val, "wput");}
void NextBusSlot::board_bput(uint32_t addr, uint8_t val)  {bus_error(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, val, "bput");}

void NextBusSlot::reset(void) {}
void NextBusSlot::pause(bool pause) {}

NextBusBoard::NextBusBoard(int slot) : NextBusSlot(slot) {}

class M68KBoard : public NextBusBoard {
public:
    M68KBoard(int slot) : NextBusBoard(slot) {}
        
    virtual uint32_t slot_lget(uint32_t addr) {return nb_cpu_slot_lget(addr);}
    virtual uint16_t slot_wget(uint32_t addr) {return nb_cpu_slot_wget(addr);}
    virtual uint8_t  slot_bget(uint32_t addr) {return nb_cpu_slot_bget(addr);}
    virtual void   slot_lput(uint32_t addr, uint32_t val) {nb_cpu_slot_lput(addr, val);}
    virtual void   slot_wput(uint32_t addr, uint16_t val) {nb_cpu_slot_wput(addr, val);}
    virtual void   slot_bput(uint32_t addr, uint8_t val)  {nb_cpu_slot_bput(addr, val);}
};

NextBusSlot* nextbus[16] = {
    new NextBusSlot(0),
    new NextBusSlot(1),
    new NextBusSlot(2),
    new NextBusSlot(3),
    new NextBusSlot(4),
    new NextBusSlot(5),
    new NextBusSlot(6),
    new NextBusSlot(7),
    new NextBusSlot(8),
    new NextBusSlot(9),
    new NextBusSlot(10),
    new NextBusSlot(11),
    new NextBusSlot(12),
    new NextBusSlot(13),
    new NextBusSlot(14),
    new NextBusSlot(15),
};

#define SLOT(addr) (((addr)  & 0x0F000000)>>24)
#define BOARD(addr) (((addr) & 0xF0000000)>>28)

extern "C" {
    /* Slot memory */
    uae_u32 nextbus_slot_lget(uaecptr addr) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: lget at %08X",slot,addr);
        
        return nextbus[slot]->slot_lget(addr);
    }
    
    uae_u32 nextbus_slot_wget(uaecptr addr) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: wget at %08X",slot,addr);
        
        return nextbus[slot]->slot_wget(addr);
    }
    
    uae_u32 nextbus_slot_bget(uaecptr addr) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: bget at %08X",slot,addr);
        
        return nextbus[slot]->slot_bget(addr);
    }
    
    void nextbus_slot_lput(uaecptr addr, uae_u32 val) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: lput at %08X, val %08X",slot,addr,val);
        
        nextbus[slot]->slot_lput(addr, val);
    }
    
    void nextbus_slot_wput(uaecptr addr, uae_u32 val) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: wput at %08X, val %04X",slot,addr,val);
        
        nextbus[slot]->slot_wput(addr, val);
    }
    
    void nextbus_slot_bput(uaecptr addr, uae_u32 val) {
        int slot = SLOT(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Slot %i: bput at %08X, val %02X",slot,addr,val);
        
        nextbus[slot]->slot_bput(addr, val);
    }
    
    /* Board memory */

    uae_u32 nextbus_board_lget(uaecptr addr) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: lget at %08X",board,addr);
        
        return nextbus[board]->board_lget(addr);
    }
    
    uae_u32 nextbus_board_wget(uaecptr addr) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: wget at %08X",board,addr);
        
        return nextbus[board]->board_wget(addr);
    }
    
    uae_u32 nextbus_board_bget(uaecptr addr) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: bget at %08X",board,addr);
        
        return nextbus[board]->board_bget(addr);
    }
    
    void nextbus_board_lput(uaecptr addr, uae_u32 val) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: lput at %08X, val %08X",board,addr,val);
        
        nextbus[board]->board_lput(addr, val);
    }
    
    void nextbus_board_wput(uaecptr addr, uae_u32 val) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: wput at %08X, val %04X",board,addr,val);
        
        nextbus[board]->board_wput(addr, val);
    }
    
    void nextbus_board_bput(uaecptr addr, uae_u32 val) {
        int board = BOARD(addr);
        Log_Printf(LOG_NEXTBUS_LEVEL, "[NextBus] Board %i: bput at %08X, val %02X",board,addr,val);
        
        nextbus[board]->board_bput(addr, val);
    }
    
    static void remove_board(int slot) {
        delete nextbus[slot];
        nextbus[slot] = new NextBusSlot(slot);
    }

    static void insert_board(NextBusBoard* board) {
        delete nextbus[board->slot];
        nextbus[board->slot] = board;
    }
    
    /* Init function for NextBus */
    void nextbus_init(void) {
        insert_board(new M68KBoard(0));
        
        for (int i = 0; i < ND_MAX_BOARDS; i++) {
            remove_board(ND_SLOT(i));
            if (ConfigureParams.Dimension.board[i].bEnabled && ConfigureParams.System.nMachineType != NEXT_STATION) {
                Log_Printf(LOG_WARN, "[NextBus] NeXTdimension board at slot %i", ND_SLOT(i));
                insert_board(new NextDimension(ND_SLOT(i)));
            }
        }
    }
    
    void nextbus_uninit(void) {
        for (int i = 0; i < ND_MAX_BOARDS; i++) {
            remove_board(ND_SLOT(i));
        }
    }
    
    void NextBus_Reset(void) {
        for(int slot = 0; slot < 16; slot++)
            nextbus[slot]->reset();
    }
    
    void NextBus_Pause(bool pause) {
        for(int slot = 0; slot < 16; slot++)
            nextbus[slot]->pause(pause);
    }
}
