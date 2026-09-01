/*
  Previous - nd_mem.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "dimension.hpp"
#include "nd_mem.hpp"
#include "log.h"

/* --------- NEXTDIMENSION MEMORY ---------- */

/* NeXTdimension board memory */
#define ND_RAM_START    0xF8000000
#define ND_RAM_SIZE     0x04000000
#define ND_RAM_MASK     0x03FFFFFF

#define ND_VRAM_START   0xFE000000
#define ND_VRAM_SIZE    0x00400000
#define ND_VRAM_MASK    0x003FFFFF

#define ND_EEPROM_START 0xFFF00000
#define ND_EEPROM_SIZE  0x00020000
#define ND_EEPROM_MASK  0x0001FFFF

/* RAM banks */
#define ND_RAM_BANKSIZE 0x01000000
#define ND_RAM_BANKMASK 0x03000000

/* NeXTdimension dither memory */
#define ND_DMEM_START   0xFF000000
#define ND_DMEM_SIZE    0x00000200
#define ND_DMEM_MASK    0x000001FF

/* NeXTdimension board devices */
#define ND_IO_START     0xFF800000
#define ND_IO_SIZE      0x00004000
#define ND_IO_MASK      0x00003FFF

/* NeXTdimension board RAMDAC */
#define ND_RAMDAC_START 0xFF200000
#define ND_RAMDAC_SIZE  0x00001000
#define ND_RAMDAC_MASK  0x00000FFF

/* NeXTdimension board data path */
#define ND_DP_START     0xF0000000
#define ND_DP_SIZE      0x00001000
#define ND_DP_MASK      0x00000FFF

/* NeXTdimension unknown registers */
#define ND_CSR_START    0xFF400000
#define ND_CSR_SIZE     0x00000200
#define ND_CSR_MASK     0x000001FF

/* Function to fix address for ROM access */
static uint32_t nd_rom_addr_fix(uint32_t addr)
{
#if ND_STEP
    addr >>= 2;
#else
    addr &= ~3;
    addr |= (addr>>17)&3;
#endif
    addr &= ND_EEPROM_MASK;
    return addr;
}

/* Memory banks */

/* NeXTdimension RAM */

class ND_RAM : public ND_Addrbank {
    uint8_t* base;
    uint32_t mask;
public:
    ND_RAM(NextDimension* nd, int bank) : ND_Addrbank(nd), base(nd->ram), mask(nd->bankmask[bank]) {}

    uint32_t lget(uint32_t addr) const {
        return do_get_mem_long(base + (addr & mask));
    }

    uint32_t wget(uint32_t addr) const {
        return do_get_mem_word(base + (addr & mask));
    }

    uint32_t bget(uint32_t addr) const {
        return base[addr & mask];
    }

    void lput(uint32_t addr, uint32_t l) const {
        do_put_mem_long(base + (addr & mask), l);
    }

    void wput(uint32_t addr, uint32_t w) const {
        do_put_mem_word(base + (addr & mask), w);
    }

    void bput(uint32_t addr, uint32_t b) const {
        base[addr & mask] = b;
    }
};

class ND_Empty : public ND_Addrbank {
public:
    ND_Empty(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank lget at %08X\n", nd->slot,addr);
        return 0;
    }

    uint32_t wget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank wget at %08X\n", nd->slot,addr);
        return 0;
    }

    uint32_t bget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank bget at %08X\n", nd->slot,addr);
        return 0;
    }

    void lput(uint32_t addr, uint32_t l) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank lput at %08X\n", nd->slot,addr);
    }

    void wput(uint32_t addr, uint32_t w) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank wput at %08X\n", nd->slot,addr);
    }

    void bput(uint32_t addr, uint32_t b) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: empty memory bank bput at %08X\n",nd->slot,addr);
    }
};

/* NeXTdimension VRAM */
/* stored as ARGB for faster blitting, assuming aligned access for 32 bit */

class ND_VRAM : public ND_Addrbank {
    uint8_t* base;
public:
    ND_VRAM(NextDimension* nd) : ND_Addrbank(nd), base(nd->vram) {
        // sanity checks for ARGB mem access
        lput(0, 0x12345678);
        if(lget(0) != 0x12345678) {fprintf(stderr, "ND_VRAM: 32 bit access check failed\n");  goto error;}

        if(bget(0) != 0x12)       {fprintf(stderr, "ND_VRAM: 8 bit access 0 check failed\n"); goto error;}
        if(bget(1) != 0x34)       {fprintf(stderr, "ND_VRAM: 8 bit access 1 check failed\n"); goto error;}
        if(bget(2) != 0x56)       {fprintf(stderr, "ND_VRAM: 8 bit access 2 check failed\n"); goto error;}
        if(bget(3) != 0x78)       {fprintf(stderr, "ND_VRAM: 8 bit access 3 check failed\n"); goto error;}

        if(wget(0) != 0x1234)     {fprintf(stderr, "ND_VRAM: 16 bit access 0 check failed\n"); goto error;}
        if(wget(2) != 0x5678)     {fprintf(stderr, "ND_VRAM: 16 bit access 2 check failed\n"); goto error;}

        wput(0, 0x7654);
        wput(2, 0x3210);
        if(lget(0) != 0x76543210) {fprintf(stderr, "ND_VRAM: 32 bit access check failed (wput)\n");  goto error;}

        bput(0, 0x12);
        bput(1, 0x34);
        bput(2, 0x56);
        bput(3, 0x78);
        if(lget(0) != 0x12345678) {fprintf(stderr, "ND_VRAM: 32 bit access check failed (bput)\n");  goto error;}

        return;
    error:
        exit(1);
    }

    uint32_t lget(uint32_t addr) const {
        addr &= ND_VRAM_MASK;
        switch (addr&3) {
            case 0: return (base[addr+2] << 24) | (base[addr+1] << 16) | (base[addr+0] << 8) | base[addr+3];
            case 1: return (base[addr+0] << 24) | (base[addr-1] << 16) | (base[addr+2] << 8) | base[addr+5];
            case 2: return (base[addr-2] << 24) | (base[addr+1] << 16) | (base[addr+4] << 8) | base[addr+3];
            case 3: return (base[addr+0] << 24) | (base[addr+3] << 16) | (base[addr+2] << 8) | base[addr+1];
        }
        return 0;
    }

    void lput(uint32_t addr, uint32_t l) const {
        addr &= ND_VRAM_MASK;
        switch (addr&3) {
            case 0: base[addr+2] = l >> 24; base[addr+1] = l >> 16; base[addr+0] = l >> 8; base[addr+3] = l; break;
            case 1: base[addr+0] = l >> 24; base[addr-1] = l >> 16; base[addr+2] = l >> 8; base[addr+5] = l; break;
            case 2: base[addr-2] = l >> 24; base[addr+1] = l >> 16; base[addr+4] = l >> 8; base[addr+3] = l; break;
            case 3: base[addr+0] = l >> 24; base[addr+3] = l >> 16; base[addr+2] = l >> 8; base[addr+1] = l; break;
        }
    }

    uint32_t wget(uint32_t addr) const {
        addr &= ND_VRAM_MASK;
        switch (addr&3) {
            case 0: return (base[addr+2] << 8) | base[addr+1];
            case 1: return (base[addr+0] << 8) | base[addr-1];
            case 2: return (base[addr-2] << 8) | base[addr+1];
            case 3: return (base[addr+0] << 8) | base[addr+3];
        }
        return 0;
    }

    void wput(uint32_t addr, uint32_t w) const {
        addr &= ND_VRAM_MASK;
        switch (addr&3) {
            case 0: base[addr+2] = w >> 8; base[addr+1] = w; break;
            case 1: base[addr+0] = w >> 8; base[addr-1] = w; break;
            case 2: base[addr-2] = w >> 8; base[addr+1] = w; break;
            case 3: base[addr+0] = w >> 8; base[addr+3] = w; break;
        }
    }

    uint32_t bget(uint32_t addr) const {
        addr &= ND_VRAM_MASK;
        switch(addr&3) {
            case 0: return base[addr+2];
            case 1: return base[addr+0];
            case 2: return base[addr-2];
            case 3: return base[addr+0];
        }
        return 0;
    }

    void bput(uint32_t addr, uint32_t b) const {
        addr &= ND_VRAM_MASK;
        switch(addr&3) {
            case 0: base[addr+2] = b; break;
            case 1: base[addr+0] = b; break;
            case 2: base[addr-2] = b; break;
            case 3: base[addr+0] = b; break;
        }
    }
};

/* NeXTdimension ROM */

class ND_ROM : public ND_Addrbank {
public:
    ND_ROM(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        addr = nd_rom_addr_fix(addr);
        return (nd->rom_read(addr) << 24); /* byte lane at msb */
    }

    uint32_t wget(uint32_t addr) const {
        addr = nd_rom_addr_fix(addr);
        return (nd->rom_read(addr) << 8); /* byte lane at msb */
    }

    uint32_t bget(uint32_t addr) const {
        addr = nd_rom_addr_fix(addr);
        return nd->rom_read(addr);
    }

    uint32_t cs8get(uint32_t addr) const {
        addr &= ND_EEPROM_MASK;
        return nd->rom[addr];
    }

    void lput(uint32_t addr, uint32_t l) const {
        addr = nd_rom_addr_fix(addr);
        nd->rom_write(addr, l >> 24);
    }

    void wput(uint32_t addr, uint32_t w) const {
        addr = nd_rom_addr_fix(addr);
        nd->rom_write(addr, w >> 8);
    }

    void bput(uint32_t addr, uint32_t b) const {
        addr = nd_rom_addr_fix(addr);
        nd->rom_write(addr, b);
    }
};

/* Unknown register access functions (memory controller step 1) */
#if ND_STEP

class ND_CSR : public ND_Addrbank {
public:
    ND_CSR(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR lget at %08X\n", nd->slot,addr);
        return 0;
    }

    uint32_t wget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR wget at %08X\n",nd->slot,addr);
        return 0;
    }

    uint32_t bget(uint32_t addr) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR bget at %08X\n",nd->slot,addr);
        return 0;
    }

    void lput(uint32_t addr, uint32_t l) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR lput at %08X: %08X\n",nd->slot,addr,l);
    }

    void wput(uint32_t addr, uint32_t w) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR wput at %08X: %04X\n",nd->slot,addr,w);
    }

    void bput(uint32_t addr, uint32_t b) const {
        Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Board CSR bput at %08X: %02X\n",nd->slot,addr,b);
    }
};
#endif

/* NeXTdimension dither memory & datapath */

class ND_DMEM : public ND_Addrbank {
public:
    ND_DMEM(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        addr &= ND_DP_MASK;
        return addr < ND_DMEM_SIZE ? do_get_mem_long(nd->dmem + addr) : nd->dp.lget(addr);
    }

    uint32_t wget(uint32_t addr) const {
        addr &= ND_DP_MASK;
        return addr < ND_DMEM_SIZE ? do_get_mem_word(nd->dmem + addr) : nd->dp.lget(addr);
    }

    uint32_t bget(uint32_t addr) const {
        addr &= ND_DP_MASK;
        return addr < ND_DMEM_SIZE ? do_get_mem_byte(nd->dmem + addr) : nd->dp.lget(addr);
    }

    void lput(uint32_t addr, uint32_t l) const {
        addr &= ND_DP_MASK;
        if(addr < ND_DMEM_SIZE)
            do_put_mem_long(nd->dmem + addr, l);
        else
            nd->dp.lput(addr, l);
    }

    void wput(uint32_t addr, uint32_t w) const {
        addr &= ND_DP_MASK;
        if(addr < ND_DMEM_SIZE)
            do_put_mem_word(nd->dmem + addr, w);
        else
            nd->dp.lput(addr, w);
    }

    void bput(uint32_t addr, uint32_t b) const {
        addr &= ND_DP_MASK;
        if(addr < ND_DMEM_SIZE)
            do_put_mem_byte(nd->dmem + addr, b);
        else
            nd->dp.lput(addr, b);
    }
};

/* Illegal access functions */

ND_Addrbank::ND_Addrbank(NextDimension* nd) : nd(nd) {}
ND_Addrbank::~ND_Addrbank() {}

uint32_t ND_Addrbank::lget(uint32_t addr) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal lget at %08X\n",nd->slot,addr);
    return 0;
}

uint32_t ND_Addrbank::wget(uint32_t addr) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal wget at %08X\n",nd->slot,addr);
    return 0;
}

uint32_t ND_Addrbank::bget(uint32_t addr) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal bget at %08X\n",nd->slot,addr);
    return 0;
}

uint32_t ND_Addrbank::cs8get(uint32_t addr) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal cs8get at %08X\n",nd->slot,addr);
    return 0;
}

void ND_Addrbank::lput(uint32_t addr, uint32_t l) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal lput at %08X\n",nd->slot,addr);
}

void ND_Addrbank::wput(uint32_t addr, uint32_t w) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal wput at %08X\n",nd->slot,addr);
}

void ND_Addrbank::bput(uint32_t addr, uint32_t b) const {
    Log_Printf(LOG_ND_MEM, "[ND] Slot %i: Illegal bput at %08X\n",nd->slot,addr);
}

/* NeXTdimension device space */

class ND_IO : public ND_Addrbank {
public:
    ND_IO(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        return nd->mc.read(addr);
    }

    uint32_t wget(uint32_t addr) const {return 0;}

    uint32_t bget(uint32_t addr) const {return 0;}

    void lput(uint32_t addr, uint32_t l) const {
        nd->mc.write(addr, l);
    }

    void wput(uint32_t addr, uint32_t w) const {}
    void bput(uint32_t addr, uint32_t b) const {}
};

/* NeXTdimension RAMDAC */

class ND_RAMDAC : public ND_Addrbank {
public:
    ND_RAMDAC(NextDimension* nd) : ND_Addrbank(nd) {}

    uint32_t lget(uint32_t addr) const {
        return bt463_bget(&nd->ramdac, addr >> 2) << 24;
    }

    uint32_t wget(uint32_t addr) const {
        return bt463_bget(&nd->ramdac, addr >> 2) << 8;
    }

    uint32_t bget(uint32_t addr) const {
        return bt463_bget(&nd->ramdac, addr >> 2);
    }

    void lput(uint32_t addr, uint32_t l) const {
        bt463_bput(&nd->ramdac, addr >> 2, l >> 24);
    }

    void wput(uint32_t addr, uint32_t w) const {
        bt463_bput(&nd->ramdac, addr >> 2, w >> 8);
    }

    void bput(uint32_t addr, uint32_t b) const {
        bt463_bput(&nd->ramdac, addr >> 2, b);
    }
};

void NextDimension::map_banks (ND_Addrbank *bank, int start, int size) {
    for (int bnr = start; bnr < start + size; bnr++)
        nd_put_mem_bank(bnr << 16, bank);
}

void NextDimension::init_mem_banks(void) {
    for (int i = 0; i < 65536; i++)
        nd_put_mem_bank(i << 16, illegal_bank);
}

void NextDimension::mem_init(void) {
    Log_Printf(LOG_WARN, "[ND] Slot %i: Memory init: Memory size: %iMB\n", slot,
               Configuration_CheckDimensionMemory(ConfigureParams.Dimension.board[ND_NUM(slot)].nMemoryBankSize));

    /* Initialize banks with error memory */
    illegal_bank = new ND_Addrbank(this);
    init_mem_banks();

    /* Clear at least first 4k of main memory for m68k ROM polling code */
    memset(ram, 0, 64*1024*1024);
    memset(vram, 0, 4*1024*1024);
    memset(rom, 0, 128*1024);
    memset(dmem, 0, 512);

    /* Load ROM from file */
    rom_load();

    /* Map main memory */
    for(int bank = 0; bank < 4; bank++) {
        if (ConfigureParams.Dimension.board[ND_NUM(slot)].nMemoryBankSize[bank]) {
            bankmask[bank] = ND_RAM_BANKMASK|((ConfigureParams.Dimension.board[ND_NUM(slot)].nMemoryBankSize[bank]<<20)-1);
            ram_bank[bank] = new ND_RAM(this, bank);
            map_banks(ram_bank[bank], (ND_RAM_START+(bank*ND_RAM_BANKSIZE))>>16, ND_RAM_BANKSIZE >> 16);
            Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping main memory bank%d at $%08x: %iMB\n", slot, bank,
                       (ND_RAM_START+(bank*ND_RAM_BANKSIZE)), ConfigureParams.Dimension.board[ND_NUM(slot)].nMemoryBankSize[bank]);
        } else {
            bankmask[bank] = 0;
            ram_bank[bank] = new ND_Empty(this);
            map_banks(ram_bank[bank], (ND_RAM_START+(bank*ND_RAM_BANKSIZE))>>16, ND_RAM_BANKSIZE >> 16);
            Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping main memory bank%d at $%08x: empty\n", slot, bank,
                       (ND_RAM_START+(bank*ND_RAM_BANKSIZE)));
        }
    }

    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping video memory at $%08x: %iMB\n", slot,
               ND_VRAM_START, ND_VRAM_SIZE>>20);
    vram_bank = new ND_VRAM(this);
    map_banks(vram_bank, ND_VRAM_START>>16, (4*ND_VRAM_SIZE)>>16);

    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping ROM at $%08x: %ikB\n", slot,
               ND_EEPROM_START, ND_EEPROM_SIZE>>10);
    rom_bank = new ND_ROM(this);
    map_banks(rom_bank, ND_EEPROM_START>>16, (ND_EEPROM_SIZE*8)>>16);

    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping dither memory and data path at $%08x: %ibyte\n", slot,
               ND_DMEM_START, ND_DMEM_SIZE);
    dmem_bank = new ND_DMEM(this);
    map_banks(dmem_bank, ND_DMEM_START>>16, 1);

    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping IO memory at $%08x\n", slot, ND_IO_START);
    io_bank = new ND_IO(this);
    map_banks(io_bank, ND_IO_START>>16, 1);

    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping RAMDAC registers at $%08x\n", slot, ND_RAMDAC_START);
    ramdac_bank = new ND_RAMDAC(this);
    map_banks(ramdac_bank, ND_RAMDAC_START>>16, 1);
#if ND_STEP
    Log_Printf(LOG_WARN, "[ND] Slot %i: Mapping board CSR at $%08x\n", slot, ND_CSR_START);
    csr_bank = new ND_CSR(this);
    map_banks(csr_bank, ND_CSR_START>>16, 1);
#endif
}

void NextDimension::mem_uninit(void) {
    for (int i = 0; i < 4; i++) {
        delete ram_bank[i];
    }
    delete illegal_bank;
    delete vram_bank;
    delete rom_bank;
    delete dmem_bank;
    delete io_bank;
    delete ramdac_bank;
#if ND_STEP
    delete csr_bank;
#endif
}
