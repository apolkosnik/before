/*
  Previous - dimension.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <stdlib.h>

#include "main.h"
#include "event.h"
#include "timing.h"
#include "configuration.h"
#include "m68000.h"
#include "dimension.hpp"
#include "nd_nbic.hpp"
#include "nd_mem.hpp"
#include "nd_sdl.hpp"

#define nd_get_mem_bank(addr)    (nd->mem_banks[nd_bankindex((addr)|ND_BOARD_BITS)])
#define nd68k_get_mem_bank(addr) (mem_banks[nd_bankindex(addr)])

#define nd_longget(addr)   (nd_get_mem_bank(addr)->lget(addr))
#define nd_wordget(addr)   (nd_get_mem_bank(addr)->wget(addr))
#define nd_byteget(addr)   (nd_get_mem_bank(addr)->bget(addr))
#define nd_longput(addr,l) (nd_get_mem_bank(addr)->lput(addr, l))
#define nd_wordput(addr,w) (nd_get_mem_bank(addr)->wput(addr, w))
#define nd_byteput(addr,b) (nd_get_mem_bank(addr)->bput(addr, b))
#define nd_cs8get(addr)    (nd_get_mem_bank(addr)->cs8get(addr))

#define nd68k_longget(addr)   (nd68k_get_mem_bank(addr)->lget(addr))
#define nd68k_wordget(addr)   (nd68k_get_mem_bank(addr)->wget(addr))
#define nd68k_byteget(addr)   (nd68k_get_mem_bank(addr)->bget(addr))
#define nd68k_longput(addr,l) (nd68k_get_mem_bank(addr)->lput(addr, l))
#define nd68k_wordput(addr,w) (nd68k_get_mem_bank(addr)->wput(addr, w))
#define nd68k_byteput(addr,b) (nd68k_get_mem_bank(addr)->bput(addr, b))
#define nd68k_cs8get(addr)    (nd68k_get_mem_bank(addr)->cs8geti(addr))

NextDimension::NextDimension(int slot) :
    NextBusBoard(slot),
    mem_banks(new ND_Addrbank*[65536]),
    ram(malloc_aligned(64*1024*1024)),
    vram(malloc_aligned(4*1024*1024)),
    rom(malloc_aligned(128*1024)),
    dmem(malloc_aligned(512)),
    rom_command(0),
    rom_last_addr(0),
    display_vbl(false),
    video_vbl(false),
    sdl(slot, (uint32_t*)vram),
    i860(this),
    nbic(slot, ND_NBIC_ID),
    mc(this),
    dp(this),
    dmcd(this),
    dcsc0(this, 0),
    dcsc1(this, 1)
{
    host_atomic_set(&m_port, 0);
    i860.uninit();
    nbic.init();
    mc.init();
    dp.init();
    mem_init();
    i860.init();
    sdl.init();
}

NextDimension::~NextDimension() {
    i860.uninit();
    sdl.destroy();
    
    mem_uninit();
    delete[] mem_banks;
    free(ram);
    free(vram);
    free(rom);
    free(dmem);
}

void NextDimension::reset(void) {
    i860.set_run_func();
    nd_start_interrupts();
}

void NextDimension::pause(bool pause) {
    i860.pause(pause);
}

/* NeXTdimension board memory access (m68k) */

 uint32_t NextDimension::board_lget(uint32_t addr) {
    addr |= ND_BOARD_BITS;
    return nd68k_longget(addr);
}

 uint16_t NextDimension::board_wget(uint32_t addr) {
    addr |= ND_BOARD_BITS;
    return nd68k_wordget(addr);
}

 uint8_t NextDimension::board_bget(uint32_t addr) {
    addr |= ND_BOARD_BITS;
    return nd68k_byteget(addr);
}

 void NextDimension::board_lput(uint32_t addr, uint32_t l) {
     addr |= ND_BOARD_BITS;
     nd68k_longput(addr, l);
}

 void NextDimension::board_wput(uint32_t addr, uint16_t w) {
    addr |= ND_BOARD_BITS;
    nd68k_wordput(addr, w);
}

 void NextDimension::board_bput(uint32_t addr, uint8_t b) {
    addr |= ND_BOARD_BITS;
    nd68k_byteput(addr, b);
}

/* NeXTdimension slot memory access */
uint32_t NextDimension::slot_lget(uint32_t addr) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        return nd68k_longget(addr);
    } else {
        return nbic.lget(addr);
    }
}

uint16_t NextDimension::slot_wget(uint32_t addr) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        return nd68k_wordget(addr);
    } else {
        return nbic.wget(addr);
    }
}

uint8_t NextDimension::slot_bget(uint32_t addr) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        return nd68k_byteget(addr);
    } else {
        return nbic.bget(addr);
    }
}

void NextDimension::slot_lput(uint32_t addr, uint32_t l) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        nd68k_longput(addr, l);
    } else {
        nbic.lput(addr, l);
    }
}

void NextDimension::slot_wput(uint32_t addr, uint16_t w) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        nd68k_wordput(addr, w);
    } else {
        nbic.wput(addr, w);
    }
}

void NextDimension::slot_bput(uint32_t addr, uint8_t b) {
    addr |= ND_SLOT_BITS;
    
    if (addr<ND_NBIC_SPACE) {
        nd68k_byteput(addr, b);
    } else {
        nbic.bput(addr, b);
    }
}

void NextDimension::send_msg(int msg) {
    int old_value, new_value;
    do {
        old_value = host_atomic_get(&m_port);
        new_value = old_value | msg;
        switch (msg) {
            case MSG_LOWER_INTR: new_value &= ~MSG_RAISE_INTR; break;
            case MSG_RAISE_INTR: new_value &= ~MSG_LOWER_INTR; break;
            default: break;
        }
    } while (!host_atomic_cas(&m_port, old_value, new_value));
}

/* NeXTdimension board memory access (i860) */

uint8_t  NextDimension::i860_cs8get(const NextDimension* nd, uint32_t addr) {
    return nd_cs8get(addr);
}

void   NextDimension::i860_rd8_be(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    *((uint8_t*)val) = nd_byteget(addr);
}

void   NextDimension::i860_rd16_be(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    *((uint16_t*)val) = nd_wordget(addr);
}

void   NextDimension::i860_rd32_be(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    val[0] = nd_longget(addr);
}

void   NextDimension::i860_rd64_be(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    val[0] = ab->lget(addr+4);
    val[1] = ab->lget(addr+0);
}

void   NextDimension::i860_rd128_be(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    val[0]  = ab->lget(addr+4);
    val[1]  = ab->lget(addr+0);
    val[2]  = ab->lget(addr+12);
    val[3]  = ab->lget(addr+8);
}

void   NextDimension::i860_wr8_be(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_byteput(addr, *((const uint8_t*)val));
}

void   NextDimension::i860_wr16_be(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_wordput(addr, *((const uint16_t*)val));
}

void   NextDimension::i860_wr32_be(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_longput(addr, val[0]);
}

void   NextDimension::i860_wr64_be(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    ab->lput(addr+4, val[0]);
    ab->lput(addr+0, val[1]);
}

void   NextDimension::i860_wr128_be(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    ab->lput(addr+4,  val[0]);
    ab->lput(addr+0,  val[1]);
    ab->lput(addr+12, val[2]);
    ab->lput(addr+8,  val[3]);
}

void   NextDimension::i860_rd8_le(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    *((uint8_t*)val) = nd_byteget(addr^7);
}

void   NextDimension::i860_rd16_le(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    *((uint16_t*)val) = nd_wordget(addr^6);
}

void   NextDimension::i860_rd32_le(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    val[0] = nd_longget(addr^4);
}

void   NextDimension::i860_rd64_le(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    val[0] = ab->lget(addr+0);
    val[1] = ab->lget(addr+4);
}

void   NextDimension::i860_rd128_le(const NextDimension* nd, uint32_t addr, uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    val[0]  = ab->lget(addr+0);
    val[1]  = ab->lget(addr+4);
    val[2]  = ab->lget(addr+8);
    val[3]  = ab->lget(addr+12);
}

void   NextDimension::i860_wr8_le(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_byteput(addr^7, *((const uint8_t*)val));
}

void   NextDimension::i860_wr16_le(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_wordput(addr^6, *((const uint16_t*)val));
}

void   NextDimension::i860_wr32_le(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    nd_longput(addr^4, val[0]);
}

void   NextDimension::i860_wr64_le(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    ab->lput(addr+0, val[0]);
    ab->lput(addr+4, val[1]);
}

void   NextDimension::i860_wr128_le(const NextDimension* nd, uint32_t addr, const uint32_t* val) {
    const ND_Addrbank* ab = nd_get_mem_bank(addr);
    ab->lput(addr+0,  val[0]);
    ab->lput(addr+4,  val[1]);
    ab->lput(addr+8,  val[2]);
    ab->lput(addr+12, val[3]);
}

/* Message disaptcher - executed on i860 thread, safe to call i860 methods */
bool NextDimension::handle_msgs(void) {
    int msg = host_atomic_set(&m_port, 0);
    
    if(msg & MSG_DISPLAY_BLANK)
        set_blank_state(ND_DISPLAY, display_vbl);
    if(msg & MSG_VIDEO_BLANK)
        set_blank_state(ND_VIDEO, video_vbl);

    return i860.handle_msgs(msg);
}

extern "C" {
    void ND_VBL_Handler(void) {
        static bool bBlankToggle = false;
        
#ifndef ENABLE_RENDERING_THREAD
        if (!bBlankToggle) {
            if (ConfigureParams.Screen.nMode == SCREEN_ALL) {
                GuiEvent_SendSpecialEvent(SPECIAL_EVENT_ND_DISPLAY);
            } else if (Configuration_SingleColorScreen()) {
                GuiEvent_SendSpecialEvent(SPECIAL_EVENT_REPAINT);
            }
        }
#endif
        Timing_BlankCount(ND_DISPLAY, bBlankToggle);
        
        FOR_EACH_SLOT(slot) {
            IF_NEXT_DIMENSION(slot, nd) {
                nd->display_vbl = bBlankToggle;
                nd->send_msg(MSG_DISPLAY_BLANK);
                host_atomic_set(&nd->i860.i860cycles, (1000*1000*33)/136);
            }
        }
        bBlankToggle = !bBlankToggle;
        
        // 136Hz with toggle gives 68Hz, blank time is 1/2 frame time
        CycInt_UpdateTimeEvent((1000*1000)/136, 0, EVENT_ND_VBL);
    }

    void ND_Video_VBL_Handler(void) {
        static bool bBlankToggle = false;
        
        Timing_BlankCount(ND_VIDEO, bBlankToggle);
        
        FOR_EACH_SLOT(slot) {
            IF_NEXT_DIMENSION(slot, nd) {
                nd->video_vbl = bBlankToggle;
                nd->send_msg(MSG_VIDEO_BLANK);
            }
        }
        bBlankToggle = !bBlankToggle;
        
        // 120Hz with toggle gives 60Hz NTSC, blank time is 1/2 frame time
        CycInt_UpdateTimeEvent((1000*1000)/120, 0, EVENT_ND_VIDEO_VBL);
    }

    void nd_start_interrupts(void) {
        CycInt_AddTimeEvent(1000, 0, EVENT_ND_VBL);
        CycInt_AddTimeEvent(1000, 0, EVENT_ND_VIDEO_VBL);
    }

#ifndef ENABLE_RENDERING_THREAD
    void nd_display_repaint(void) {
        nd_sdl_repaint();
    }
#endif

    bool nd_video_enabled(int slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            return nd->unblanked();
        } else {
            return false;
        }
    }

    uint32_t* nd_vram_for_slot(int slot) {
        IF_NEXT_DIMENSION(slot, nd) {
            return (uint32_t*)nd->vram;
        } else {
            return NULL;
        }
    }

    void nd_start_debugger(void) {
        FOR_EACH_SLOT(slot) {
            IF_NEXT_DIMENSION(slot, nd) {
                nd->send_msg(MSG_DBG_BREAK);
            }
        }
    }

    const char* nd_reports(uint64_t realTime, uint64_t hostTime) {
        FOR_EACH_SLOT(slot) {
            IF_NEXT_DIMENSION(slot, nd) {
                return nd->i860.reports(realTime, hostTime);
            }
        }
        return "";
    }
}
