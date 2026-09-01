/*
  Previous - dimension.hpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef __DIMENSION_H__
#define __DIMENSION_H__

/* NeXTdimension memory controller revision (0 and 1 allowed) */
#define ND_STEP 1

#define ND_SLOT(num) ((num)*2+2)
#define ND_NUM(slot) ((slot)/2-1)

#define ND_LOG_IO_RD LOG_NONE
#define ND_LOG_IO_WR LOG_NONE

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    typedef void (*i860_run_func)(int);
    extern i860_run_func i860_Run;

    extern void        ND_VBL_Handler(void);
    extern void        ND_Video_VBL_Handler(void);
    extern void        nd_start_interrupts(void);
    extern void        nd_display_repaint(void);
    extern bool        nd_video_enabled(int slot);
    extern uint32_t*   nd_vram_for_slot(int slot);
    extern void        nd_start_debugger(void);
    extern const char* nd_reports(uint64_t realTime, uint64_t hostTime);
#ifdef __cplusplus
}

#include "NextBus.hpp"
#include "i860.hpp"
#include "nd_nbic.hpp"
#include "nd_mem.hpp"
#include "nd_sdl.hpp"
#include "ramdac.h"

class NextDimension;

class MC {
    NextDimension* nd;
public:
    uint32_t csr0;
    uint32_t csr1;
    uint32_t csr2;
    uint32_t sid;
    uint32_t dma_csr;
    uint32_t dma_start;
    uint32_t dma_width;
    uint32_t dma_pstart;
    uint32_t dma_pwidth;
    uint32_t dma_sstart;
    uint32_t dma_swidth;
    uint32_t dma_bsstart;
    uint32_t dma_bswidth;
    uint32_t dma_top;
    uint32_t dma_bottom;
    uint32_t dma_line_a;
    uint32_t dma_curr_a;
    uint32_t dma_scurr_a;
    uint32_t dma_out_a;
    uint32_t vram;
    uint32_t dram;
    
    MC(NextDimension* nd);
    void init(void);
    void check_interrupt(void);
    uint32_t read(uint32_t addr);
    void   write(uint32_t addr, uint32_t val);
};

class DP {
    NextDimension* nd;
public:
    uint8_t  iic_addr;
    uint8_t  iic_msg;
    uint32_t iic_msgsz;
    int      iic_busy;
    uint32_t doff; // (SC) wild guess - vram offset in pixels?
    uint32_t csr;
    uint32_t alpha;
    uint32_t dma;
    uint32_t cpu_x;
    uint32_t cpu_y;
    uint32_t dma_x;
    uint32_t dma_y;
    uint32_t iic_stat_addr;
    uint32_t iic_data;
    
    DP(NextDimension* nd);
    void init(void);
    uint32_t lget(uint32_t addr);
    void   lput(uint32_t addr, uint32_t val);
    void   iicmsg(void);
    void   led(uint32_t val);
};

#define ND_DMCD_NUM_REG 25
class DMCD {
    NextDimension* nd;
public:
    uint8_t addr;
    uint8_t reg[ND_DMCD_NUM_REG];

    DMCD(NextDimension* nd);
    
    void write(uint32_t step, uint8_t data);
};

class DCSC {
    NextDimension* nd;
    int dev;
public:
    uint8_t addr;
    uint8_t ctrl;
    uint8_t lut[256];
    
    DCSC(NextDimension* nd, int dev);
    
    void write(uint32_t step, uint8_t data);
};

class NextDimension : public NextBusBoard {
    /* Message port for host->dimension communication */
    atomic_int      m_port;
public:
    ND_Addrbank**   mem_banks;
    ND_Addrbank*    illegal_bank;
    ND_Addrbank*    ram_bank[4];
    ND_Addrbank*    vram_bank;
    ND_Addrbank*    rom_bank;
    ND_Addrbank*    dmem_bank;
    ND_Addrbank*    io_bank;
    ND_Addrbank*    ramdac_bank;
    ND_Addrbank*    csr_bank;
    
    uint8_t*        ram;
    uint8_t*        vram;
    uint8_t*        rom;
    uint8_t*        dmem;
    
    uint8_t         rom_command;
    uint32_t        rom_last_addr;
    uint32_t        bankmask[4];
    
    volatile bool   display_vbl;
    volatile bool   video_vbl;
    
    NDSDL           sdl;
    i860_cpu_device i860;
    NBIC            nbic;
    MC              mc;
    DP              dp;
    DMCD            dmcd;
    DCSC            dcsc0;
    DCSC            dcsc1;
    bt463           ramdac;
    
    NextDimension(int slot);
    void mem_init(void);
    void mem_uninit(void);
    void init_mem_banks(void);
    void map_banks (ND_Addrbank *bank, int start, int size);

    virtual uint32_t board_lget(uint32_t addr);
    virtual uint16_t board_wget(uint32_t addr);
    virtual uint8_t  board_bget(uint32_t addr);
    virtual void     board_lput(uint32_t addr, uint32_t val);
    virtual void     board_wput(uint32_t addr, uint16_t val);
    virtual void     board_bput(uint32_t addr, uint8_t val);

    virtual uint32_t slot_lget(uint32_t addr);
    virtual uint16_t slot_wget(uint32_t addr);
    virtual uint8_t  slot_bget(uint32_t addr);
    virtual void     slot_lput(uint32_t addr, uint32_t val);
    virtual void     slot_wput(uint32_t addr, uint16_t val);
    virtual void     slot_bput(uint32_t addr, uint8_t val);

    virtual void     reset(void);
    virtual void     pause(bool pause);

    static uint8_t   i860_cs8get  (const NextDimension* nd, uint32_t addr);
    static void      i860_rd8_be  (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd16_be (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd32_be (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd64_be (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd128_be(const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_wr8_be  (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr16_be (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr32_be (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr64_be (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr128_be(const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_rd8_le  (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd16_le (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd32_le (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd64_le (const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_rd128_le(const NextDimension* nd, uint32_t addr, uint32_t* val);
    static void      i860_wr8_le  (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr16_le (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr32_le (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr64_le (const NextDimension* nd, uint32_t addr, const uint32_t* val);
    static void      i860_wr128_le(const NextDimension* nd, uint32_t addr, const uint32_t* val);

    uint8_t rom_read(uint32_t addr);
    void    rom_write(uint32_t addr, uint8_t val);
    void    rom_load();
        
    bool    handle_msgs(void);  /* i860 thread message handler */
    void    send_msg(int msg);

    void    set_blank_state(int src, bool state);
    bool    unblanked(void);
    void    video_dev_write(uint8_t addr, uint32_t step, uint8_t data);

    bool    dbg_cmd(const char* buf);

    virtual ~NextDimension();
};

#endif /* __cplusplus */

#endif /* __DIMENSION_H__ */
