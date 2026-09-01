/***************************************************************************

    i860.c

    Interface file for the Intel i860 emulator.

    Copyright (C) 1995-present Jason Eckhardt (jle@rice.edu)
    Released for general non-commercial use under the MAME license
    with the additional requirement that you are free to use and
    redistribute this code in modified or unmodified form, provided
    you list me in the credits.
    Visit http://mamedev.org for licensing and usage restrictions.

    Changes for previous/NeXTdimension by Simon Schubiger (SC)

***************************************************************************/

#include <stdlib.h>
#if defined _WIN32
#undef mkdir
#endif
#include <unistd.h>

#include "i860.hpp"
#include "dimension.hpp"
#include "main.h"
#include "event.h"
#include "log.h"

extern "C" {
    static void i860_run_nop(int nHostCycles) {}

    i860_run_func i860_Run = i860_run_nop;

    static void i860_run_thread(int nHostCycles) {
        nd_nbic_interrupt();
    }

    static void i860_run_no_thread(int nHostCycles) {
        int cycles;
        
        FOR_EACH_SLOT(slot) {
            IF_NEXT_DIMENSION(slot, nd) {
                nd->handle_msgs();
                
                if(nd->i860.is_halted()) return;
                
                cycles = nHostCycles * 33; // i860 @ 33MHz
                cycles /= ConfigureParams.System.nCpuFreq;
                while (cycles > 0) {
                    nd->i860.run_cycle();
                    cycles --;
                }
            }
        }
        nd_nbic_interrupt();
    }    
}

i860_cpu_device::i860_cpu_device(NextDimension* nd) : nd(nd) {
    m_thread = NULL;
    m_halt   = true;
    
    snprintf(m_thread_name, sizeof(m_thread_name), "[Previous] i860 at slot %d", nd->slot);
    
    for(int i = 0; i < 8192; i++) {
        int upper6 = i >> 7;
        switch (upper6) {
            case 0x12:
                decoder_tbl[i] = fp_decode_tbl[i & 0x7f];
                break;
            case 0x13:
                decoder_tbl[i] = core_esc_decode_tbl[i&3];
                break;
            default:
                decoder_tbl[i] = decode_tbl[upper6];
        }
    }
}

int i860_cpu_device::thread(void* data) {
    host_thread_priority(0);
    ((i860_cpu_device*)data)->run();
    return 0;
}

void i860_cpu_device::set_mem_access(bool be) {
    if(be) {
        rdmem[1]  = NextDimension::i860_rd8_be;
        rdmem[2]  = NextDimension::i860_rd16_be;
        rdmem[4]  = NextDimension::i860_rd32_be;
        rdmem[8]  = NextDimension::i860_rd64_be;
        rdmem[16] = NextDimension::i860_rd128_be;
        
        wrmem[1]  = NextDimension::i860_wr8_be;
        wrmem[2]  = NextDimension::i860_wr16_be;
        wrmem[4]  = NextDimension::i860_wr32_be;
        wrmem[8]  = NextDimension::i860_wr64_be;
        wrmem[16] = NextDimension::i860_wr128_be;
    } else {
        rdmem[1]  = NextDimension::i860_rd8_le;
        rdmem[2]  = NextDimension::i860_rd16_le;
        rdmem[4]  = NextDimension::i860_rd32_le;
        rdmem[8]  = NextDimension::i860_rd64_le;
        rdmem[16] = NextDimension::i860_rd128_le;
        
        wrmem[1]  = NextDimension::i860_wr8_le;
        wrmem[2]  = NextDimension::i860_wr16_le;
        wrmem[4]  = NextDimension::i860_wr32_le;
        wrmem[8]  = NextDimension::i860_wr64_le;
        wrmem[16] = NextDimension::i860_wr128_le;
    }
}

inline UINT8 i860_cpu_device::rdcs8(UINT32 addr) {
    return NextDimension::i860_cs8get(nd, addr);
}

inline UINT32 i860_cpu_device::get_iregval(int gr) {
    return m_iregs[gr];
}

inline void i860_cpu_device::set_iregval(int gr, UINT32 val) {
    m_iregs[gr] = val;
    m_iregs[0]  = 0; // make sure r0 is always 0
}

inline FLOAT32 i860_cpu_device::get_fregval_s (int fr) {
    return *(FLOAT32*)(&m_fregs[fr * 4]);
}

inline void i860_cpu_device::set_fregval_s (int fr, FLOAT32 s) {
    if(fr > 1)
        *(FLOAT32*)(&m_fregs[fr * 4]) = s;
}

inline FLOAT64 i860_cpu_device::get_fregval_d (int fr) {
    return *(FLOAT64*)(&m_fregs[fr * 4]);
}

inline void i860_cpu_device::set_fregval_d (int fr, FLOAT64 d) {
    if(fr > 1)
        *(FLOAT64*)(&m_fregs[fr * 4]) = d;
}

inline void i860_cpu_device::SET_PSR_CC(int val) {
    if(!(m_dim_cc_valid))
        m_cregs[CR_PSR] = (m_cregs[CR_PSR] & ~(1 << 2)) | ((val & 1) << 2);
}

const char* i860_cpu_device::trap_info() {
    static char buffer[256];
    buffer[0] = 0;
    strcat(buffer, "TRAP");
    if(m_flow & TRAP_NORMAL)        strcat(buffer, " [Normal]");
    if(m_flow & TRAP_IN_DELAY_SLOT) strcat(buffer, " [Delay Slot]");
    if(m_flow & TRAP_WAS_EXTERNAL)  strcat(buffer, " [External]");
    if(!(GET_PSR_IT() || GET_PSR_FT() || GET_PSR_IAT() || GET_PSR_DAT() || GET_PSR_IN()))
        strcat(buffer, " >Reset<");
    else {
        if(GET_PSR_IT())  strcat(buffer, " >Instruction Fault<");
        if(GET_PSR_FT())  strcat(buffer, " >Floating Point Fault<");
        if(GET_PSR_IAT()) strcat(buffer, " >Instruction Access Fault<");
        if(GET_PSR_DAT()) strcat(buffer, " >Data Access Fault<");
        if(GET_PSR_IN())  strcat(buffer, " >Interrupt<");
    }
    
    return buffer;
}

void i860_cpu_device::handle_trap(UINT32 savepc) {
    if(!(m_single_stepping) && !((GET_PSR_IAT() || GET_PSR_DAT() || GET_PSR_IN())))
        debugger('d', trap_info());
    
    if(m_dim) {
        Log_Printf(LOG_DEBUG, "[i860] Trap while DIM %s pc=%08X m_flow=%08X", trap_info(), savepc, m_flow);
    }
    
    /* If we need to trap, change PC to trap address.
     Also set supervisor mode, copy U and IM to their
     previous versions, clear IM.  */
    if (m_flow & TRAP_IN_DELAY_SLOT)
        m_cregs[CR_FIR] = m_delay_slot_pc;
    else
        m_cregs[CR_FIR] = savepc;
    
    m_flow |= FIR_GETS_TRAP;
    SET_PSR_PU (GET_PSR_U ());
    SET_PSR_PIM (GET_PSR_IM ());
    SET_PSR_U (0);
    SET_PSR_IM (0);

    if (m_dim)
        SET_PSR_DIM (1);
    else
        SET_PSR_DIM (0);
    
    if (((m_dim == DIM_NONE) &&  (m_flow & DIM_OP)) ||
        ((m_dim == DIM_TEMP) && !(m_flow & DIM_OP)))
        SET_PSR_DS (1);
    else
        SET_PSR_DS (0);

    m_dim_cc        = false;
    m_dim_cc_valid  = false;
    
    m_pc = 0xffffff00;
}

void i860_cpu_device::ret_from_trap() {
    if (GET_PSR_DIM()) {
        m_dim = DIM_FULL;
        if (GET_PSR_DS()) {
            m_flow &= ~DIM_OP;
        } else {
            m_flow |= DIM_OP;
        }
    } else {
        m_dim = DIM_NONE;
        if (GET_PSR_DS()) {
            m_flow |= DIM_OP;
        } else {
            m_flow &= ~DIM_OP;
        }
    }

    m_flow &= ~FIR_GETS_TRAP;
}

inline void i860_cpu_device::dim_switch() {
    switch (m_dim) {
        case DIM_NONE:
            if(m_flow & DIM_OP)
                m_dim = DIM_TEMP;
            break;
        case DIM_TEMP:
            m_dim = m_flow & DIM_OP ? DIM_FULL : DIM_NONE;
            break;
        case DIM_FULL:
            if(!(m_flow & DIM_OP))
                m_dim = DIM_TEMP;
            break;
    }
    m_flow &= ~DIM_OP;
}

void i860_cpu_device::run_cycle() {
    CLEAR_FLOW();
    m_dim_cc_valid = false;
    UINT32 savepc  = m_pc;
    UINT64 insn64  = ifetch64(m_pc);
    
    if(!(m_pc & 4)) {
#if ENABLE_DEBUGGER
        if(m_single_stepping) debugger(0,0);
#endif
        
        UINT32 insnLow = (UINT32)insn64;
        if(insnLow == INSN_FNOP_DIM) {
            if(m_dim) m_flow |=  DIM_OP;
            else      m_flow &= ~DIM_OP;
        } else if((insnLow & INSN_MASK_DIM) == INSN_FP_DIM)
            m_flow |= DIM_OP;
        
        if ((insnLow & INSN_MASK) == INSN_FP && GET_PSR_KNF())
            m_flow |= FP_OP_SKIPPED;
        else
            decode_exec(insnLow);

        if (PENDING_TRAP()) {
            handle_trap(savepc);
            goto done;
        } else if(GET_PC_UPDATED()) {
            goto done;
        } else {
            // If the PC wasn't updated by a control flow instruction, just bump to next sequential instruction.
            m_pc   += 4;
            CLEAR_FLOW();
        }
    }
    
    if(m_pc & 4) {
        if (!m_dim)
            savepc  = m_pc;
        
#if ENABLE_DEBUGGER
        if(m_single_stepping && !(m_dim)) debugger(0,0);
#endif

        UINT32 insnHigh = insn64 >> 32;
        
        if ((insnHigh & INSN_MASK) == INSN_FP && GET_PSR_KNF() && !(m_flow & FP_OP_SKIPPED))
            m_flow |= FP_OP_SKIPPED;
        else
            decode_exec(insnHigh);
        
        if (PENDING_TRAP()) {
            handle_trap(savepc);
            // If core instruction did trap in DIM, do not reset KNF.
            if (m_dim)
                m_flow &= ~FP_OP_SKIPPED;
        } else if (!(GET_PC_UPDATED())) {
            // If the PC wasn't updated by a control flow instruction, just bump to next sequential instruction.
            m_pc += 4;
        }
    }
    
done:
    if (m_flow & FP_OP_SKIPPED) {
        m_flow &= ~FP_OP_SKIPPED;
        SET_PSR_KNF(0);
    }
    
    // If at 64-bit boundary, switch DIM for next instruction.
    if (!(m_pc & 4))
        dim_switch();
    
    // Check for external interrupts and trap if an interrupt is pending.
    gen_interrupt();
    if (m_flow & TRAP_WAS_EXTERNAL)
        handle_trap(m_pc);
}

int i860_cpu_device::memtest(bool be) {
    const UINT32 P_TEST_ADDR = 0x8000000;
    
    m_cregs[CR_DIRBASE] = 0; // turn VM off

    const UINT8  uint8  = 0x01;
    const UINT16 uint16 = 0x0123;
    const UINT32 uint32 = 0x01234567;
    const UINT64 uint64 = 0x0123456789ABCDEFLL;
    
    UINT8  tmp8;
    UINT16 tmp16;
    UINT32 tmp32;
    
    int err = be ? 20000 : 30000;
    
    // intel manual example
    SET_EPSR_BE(0);
    set_mem_access(false);
    
    tmp8 = 'A'; wrmem[1](nd, P_TEST_ADDR+0, (UINT32*)&tmp8);
    tmp8 = 'B'; wrmem[1](nd, P_TEST_ADDR+1, (UINT32*)&tmp8);
    tmp8 = 'C'; wrmem[1](nd, P_TEST_ADDR+2, (UINT32*)&tmp8);
    tmp8 = 'D'; wrmem[1](nd, P_TEST_ADDR+3, (UINT32*)&tmp8);
    tmp8 = 'E'; wrmem[1](nd, P_TEST_ADDR+4, (UINT32*)&tmp8);
    tmp8 = 'F'; wrmem[1](nd, P_TEST_ADDR+5, (UINT32*)&tmp8);
    tmp8 = 'G'; wrmem[1](nd, P_TEST_ADDR+6, (UINT32*)&tmp8);
    tmp8 = 'H'; wrmem[1](nd, P_TEST_ADDR+7, (UINT32*)&tmp8);
    
    rdmem[1](nd, P_TEST_ADDR+0, (UINT32*)&tmp8); if(tmp8 != 'A') return err + 100;
    rdmem[1](nd, P_TEST_ADDR+1, (UINT32*)&tmp8); if(tmp8 != 'B') return err + 101;
    rdmem[1](nd, P_TEST_ADDR+2, (UINT32*)&tmp8); if(tmp8 != 'C') return err + 102;
    rdmem[1](nd, P_TEST_ADDR+3, (UINT32*)&tmp8); if(tmp8 != 'D') return err + 103;
    rdmem[1](nd, P_TEST_ADDR+4, (UINT32*)&tmp8); if(tmp8 != 'E') return err + 104;
    rdmem[1](nd, P_TEST_ADDR+5, (UINT32*)&tmp8); if(tmp8 != 'F') return err + 105;
    rdmem[1](nd, P_TEST_ADDR+6, (UINT32*)&tmp8); if(tmp8 != 'G') return err + 106;
    rdmem[1](nd, P_TEST_ADDR+7, (UINT32*)&tmp8); if(tmp8 != 'H') return err + 107;
    
    rdmem[2](nd, P_TEST_ADDR+0, (UINT32*)&tmp16); if(tmp16 != (('B'<<8)|('A'))) return err + 110;
    rdmem[2](nd, P_TEST_ADDR+2, (UINT32*)&tmp16); if(tmp16 != (('D'<<8)|('C'))) return err + 111;
    rdmem[2](nd, P_TEST_ADDR+4, (UINT32*)&tmp16); if(tmp16 != (('F'<<8)|('E'))) return err + 112;
    rdmem[2](nd, P_TEST_ADDR+6, (UINT32*)&tmp16); if(tmp16 != (('H'<<8)|('G'))) return err + 113;

    rdmem[4](nd, P_TEST_ADDR+0, &tmp32); if(tmp32 != (('D'<<24)|('C'<<16)|('B'<<8)|('A'))) return err + 120;
    rdmem[4](nd, P_TEST_ADDR+4, &tmp32); if(tmp32 != (('H'<<24)|('G'<<16)|('F'<<8)|('E'))) return err + 121;

    SET_EPSR_BE(1);
    set_mem_access(true);

    rdmem[1](nd, P_TEST_ADDR+0, (UINT32*)&tmp8); if(tmp8 != 'H') return err + 200;
    rdmem[1](nd, P_TEST_ADDR+1, (UINT32*)&tmp8); if(tmp8 != 'G') return err + 201;
    rdmem[1](nd, P_TEST_ADDR+2, (UINT32*)&tmp8); if(tmp8 != 'F') return err + 202;
    rdmem[1](nd, P_TEST_ADDR+3, (UINT32*)&tmp8); if(tmp8 != 'E') return err + 203;
    rdmem[1](nd, P_TEST_ADDR+4, (UINT32*)&tmp8); if(tmp8  != 'D') return err + 204;
    rdmem[1](nd, P_TEST_ADDR+5, (UINT32*)&tmp8); if(tmp8  != 'C') return err + 205;
    rdmem[1](nd, P_TEST_ADDR+6, (UINT32*)&tmp8); if(tmp8  != 'B') return err + 206;
    rdmem[1](nd, P_TEST_ADDR+7, (UINT32*)&tmp8); if(tmp8  != 'A') return err + 207;
    
    rdmem[2](nd, P_TEST_ADDR+0, (UINT32*)&tmp16); if(tmp16 != (('H'<<8)|('G'))) return err + 210;
    rdmem[2](nd, P_TEST_ADDR+2, (UINT32*)&tmp16); if(tmp16 != (('F'<<8)|('E'))) return err + 211;
    rdmem[2](nd, P_TEST_ADDR+4, (UINT32*)&tmp16); if(tmp16 != (('D'<<8)|('C'))) return err + 212;
    rdmem[2](nd, P_TEST_ADDR+6, (UINT32*)&tmp16); if(tmp16 != (('B'<<8)|('A'))) return err + 213;
    
    rdmem[4](nd, P_TEST_ADDR+0, &tmp32); if(tmp32 != (('H'<<24)|('G'<<16)|('F'<<8)|('E'))) return err + 220;
    rdmem[4](nd, P_TEST_ADDR+4, &tmp32); if(tmp32 != (('D'<<24)|('C'<<16)|('B'<<8)|('A'))) return err + 221;
    
    // some register and mem r/w tests
    
    SET_EPSR_BE(be);
    set_mem_access(be);

    wrmem[1](nd, P_TEST_ADDR, (UINT32*)&uint8);
    rdmem[1](nd, P_TEST_ADDR, (UINT32*)&tmp8);
    if(tmp8 != 0x01) return err;
    
    wrmem[2](nd, P_TEST_ADDR, (UINT32*)&uint16);
    rdmem[2](nd, P_TEST_ADDR, (UINT32*)&tmp16);
    if(tmp16 != 0x0123) return err+1;
    
    wrmem[4](nd, P_TEST_ADDR, &uint32);
    rdmem[4](nd, P_TEST_ADDR, &tmp32); if(tmp32 != 0x01234567) return err+2;
    
    readmem_emu(P_TEST_ADDR, 4, (UINT8*)&uint32);
    if(uint32 != 0x01234567) return err+3;
    
    writemem_emu(P_TEST_ADDR, 4, (UINT8*)&uint32, 0xff);
    rdmem[4](nd, P_TEST_ADDR+0, &tmp32); if(tmp32 != 0x01234567) return err+4;
    
    UINT8* uint8p = (UINT8*)&uint64;
    set_fregval_d(2, *((FLOAT64*)uint8p));
    writemem_emu(P_TEST_ADDR, 8, &m_fregs[8], 0xff);
    readmem_emu (P_TEST_ADDR, 8, &m_fregs[8]);
    *((FLOAT64*)&uint64) = get_fregval_d(2);
    if(uint64 != 0x0123456789ABCDEFLL) return err+5;

    UINT32 lo;
    UINT32 hi;

    rdmem[4](nd, P_TEST_ADDR+0, &lo);
    rdmem[4](nd, P_TEST_ADDR+4, &hi);
    
    if(lo != 0x01234567) return err+6;
    if(hi != 0x89ABCDEF) return err+7;
    
    return 0;
}

void i860_cpu_device::set_run_func(void) {
    i860_Run = ConfigureParams.Dimension.bI860Thread ? i860_run_thread : i860_run_no_thread;
}

void i860_cpu_device::init(void) {
    /* Configurations - keep in sync with i860cfg.h */
    static const char* CFGS[8];
    for(int i = 0; i < 8; i++) CFGS[i] = "Unknown emulator configuration";
    CFGS[CONF_I860_SPEED]     = CONF_STR(CONF_I860_SPEED);
    CFGS[CONF_I860_DEV]       = CONF_STR(CONF_I860_DEV);
    CFGS[CONF_I860_NO_THREAD] = CONF_STR(CONF_I860_NO_THREAD);
    Log_Printf(LOG_WARN, "[i860] Emulator configured for %s, %d logical cores detected, %s",
               CFGS[CONF_I860], host_num_cpus(),
               ConfigureParams.Dimension.bI860Thread ? "using seperate thread for i860" : "i860 running on m68k thread. WARNING: expect slow emulation");
    
    reset_fpcs(&m_fpcs);
    
    m_single_stepping   = 0;
    m_lastcmd           = 0;
    m_console_idx       = 0;
    m_break_on_next_msg = false;
    m_dim               = DIM_NONE;
    m_way               = 0;
    m_traceback_idx     = 0;
    memset(m_fregs, 0, sizeof(m_fregs));
    
    set_mem_access(false);

    // some sanity checks for endianess
    int    err    = 0;
    {
        UINT32 uint32 = 0x01234567;
        UINT8* uint8p = (UINT8*)&uint32;
        if(uint8p[3] != 0x01) {err = 1; goto error;}
        if(uint8p[2] != 0x23) {err = 2; goto error;}
        if(uint8p[1] != 0x45) {err = 3; goto error;}
        if(uint8p[0] != 0x67) {err = 4; goto error;}
        
        for(int i = 0; i < 32; i++) {
            uint8p[3] = i;
            set_fregval_s(i, *((FLOAT32*)uint8p));
        }
        if(get_fregval_s(0) != 0)   {err = 198; goto error;}
        if(get_fregval_s(1) != 0)   {err = 199; goto error;}
        for(int i = 2; i < 32; i++) {
            uint8p[3] = i;
            if(get_fregval_s(i) != *((FLOAT32*)uint8p))
                {err = 100+i; goto error;}
        }
        for(int i = 2; i < 32; i++) {
            if(m_fregs[i*4+3] != i)    {err = 200+i; goto error;}
            if(m_fregs[i*4+2] != 0x23) {err = 200+i; goto error;}
            if(m_fregs[i*4+1] != 0x45) {err = 200+i; goto error;}
            if(m_fregs[i*4+0] != 0x67) {err = 200+i; goto error;}
        }
    }
    
    {
        UINT64 uint64 = 0x0123456789ABCDEFLL;
        UINT8* uint8p = (UINT8*)&uint64;
        if(uint8p[7] != 0x01) {err = 10001; goto error;}
        if(uint8p[6] != 0x23) {err = 10002; goto error;}
        if(uint8p[5] != 0x45) {err = 10003; goto error;}
        if(uint8p[4] != 0x67) {err = 10004; goto error;}
        if(uint8p[3] != 0x89) {err = 10005; goto error;}
        if(uint8p[2] != 0xAB) {err = 10006; goto error;}
        if(uint8p[1] != 0xCD) {err = 10007; goto error;}
        if(uint8p[0] != 0xEF) {err = 10008; goto error;}
        
        for(int i = 0; i < 16; i++) {
            uint8p[7] = i;
            set_fregval_d(i*2, *((FLOAT64*)uint8p));
        }
        if(get_fregval_d(0) != 0)
            {err = 10199; goto error;}
        for(int i = 1; i < 16; i++) {
            uint8p[7] = i;
            if(get_fregval_d(i*2) != *((FLOAT64*)uint8p))
                {err = 10100+i; goto error;}
        }
        for(int i = 2; i < 32; i += 2) {
            FLOAT32 hi = get_fregval_s(i+1);
            FLOAT32 lo = get_fregval_s(i+0);
            if((*(UINT32*)&hi) != (UINT32)(0x00234567 | (i<<23))) {err = 10100+i; goto error;}
            if((*(UINT32*)&lo) != (UINT32) 0x89ABCDEF)            {err = 10100+i; goto error;}
        }
        for(int i = 1; i < 16; i++) {
            if(m_fregs[i*8+7] != i)    {err = 10200+i; goto error;}
            if(m_fregs[i*8+6] != 0x23) {err = 10200+i; goto error;}
            if(m_fregs[i*8+5] != 0x45) {err = 10200+i; goto error;}
            if(m_fregs[i*8+4] != 0x67) {err = 10200+i; goto error;}
            if(m_fregs[i*8+3] != 0x89) {err = 10200+i; goto error;}
            if(m_fregs[i*8+2] != 0xAB) {err = 10200+i; goto error;}
            if(m_fregs[i*8+1] != 0xCD) {err = 10200+i; goto error;}
            if(m_fregs[i*8+0] != 0xEF) {err = 10200+i; goto error;}
        }
    }
    
    if (ConfigureParams.Dimension.board[ND_NUM(nd->slot)].nMemoryBankSize[0] > 0) {
        err = memtest(true); if(err) goto error;
        err = memtest(false); if(err) goto error;
    } else {
        Log_Printf(LOG_WARN, "[i860] No main memory detected. NeXTdimension requires at least 4 MB of memory in bank 0.");
    }
    
error:
    if(err) {
        fprintf(stderr, "NeXTdimension i860 emulator requires a little-endian host. This system seems to be big endian. Error %d. Exiting.\n", err);
        fflush(stderr);
        exit(err);
    }

    nd->send_msg(MSG_I860_RESET);
    if(ConfigureParams.Dimension.bI860Thread) {
        i860_Run = i860_run_thread;
        m_thread = host_thread_create(i860_cpu_device::thread, m_thread_name, this);
    } else {
        i860_Run = i860_run_no_thread;
    }
}

void i860_cpu_device::uninit() {
    halt(true);

    if(m_thread) {
        nd->send_msg(MSG_I860_KILL);
        host_thread_wait(m_thread);
        m_thread = NULL;
    }
}

/* Message disaptcher - executed on i860 thread, safe to call i860 methods */
bool i860_cpu_device::handle_msgs(int msg) {
    if(msg & MSG_I860_KILL)
        return false;
    
    if(msg & MSG_I860_RESET)
        reset();
    else if(msg & MSG_RAISE_INTR)
        raise_intr();
    else if(msg & MSG_LOWER_INTR)
        lower_intr();
    if(msg & MSG_DBG_BREAK)
        debugger('d', "BREAK at pc=%08X", m_pc);
    return true;
}

void i860_cpu_device::run() {
    while(nd->handle_msgs()) {
        
        /* Sleep a bit if halted */
        if(is_halted()) {
            host_sleep_ms(100);
            continue;
        }
        
        if (host_atomic_get(&i860cycles) > 0) {
            /* Run some i860 cycles before re-checking messages */
            for(int i = 16; --i >= 0;)
                run_cycle();
            
            host_atomic_add(&i860cycles, -16);
        } else {
            host_sleep_ms(1);
        }
    }
}

const char* i860_cpu_device::reports(uint64_t realTime, uint64_t hostTime) {
    double dVT = (hostTime - m_last_vt) / 1000000.0;
    
    if(is_halted()) {
        m_report[0] = 0;
    } else {
        if(dVT == 0) dVT = 0.0001;
        snprintf(m_report, sizeof(m_report),
                 "i860:{MIPS=%.1f icache_hit=%lld%% tlb_hit=%lld%% tlb_search=%lld%% icach_inval/s=%.0f tlb_inval/s=%.0f intr/s=%0.f}",
                 (float) (m_insn_decoded / (dVT*1000*1000)),
                 m_icache_hit+m_icache_miss == 0 ? 0LL : (100LL * m_icache_hit) / (m_icache_hit+m_icache_miss),
                 m_tlb_hit+m_tlb_miss       == 0 ? 0LL : (100LL * m_tlb_hit)    / (m_tlb_hit+m_tlb_miss),
                 m_tlb_hit+m_tlb_miss       == 0 ? 0LL : (100LL * m_tlb_search) / (m_tlb_hit+m_tlb_miss),
                 (float) (m_icache_inval)/dVT,
                 (float) (m_tlb_inval)/dVT,
                 (float) (m_intrs)/dVT
                 );
        
        m_insn_decoded  = 0;
        m_icache_hit    = 0;
        m_icache_miss   = 0;
        m_icache_inval  = 0;
        m_tlb_hit       = 0;
        m_tlb_search    = 0;
        m_tlb_miss      = 0;
        m_tlb_inval     = 0;
        m_intrs         = 0;

        m_last_rt = realTime;
        m_last_vt = hostTime;
    }
    
    return m_report;
}

offs_t i860_cpu_device::disasm(char* buffer, offs_t pc) {
    return pc + i860_disassembler((UINT32)pc, ifetch_notrap((UINT32)pc), buffer);
}

/**************************************************************************
 * The actual decode and execute code.
 **************************************************************************/
#include "i860dec.cpp"

/**************************************************************************
 * The debugger code.
 **************************************************************************/
#include "i860dbg.cpp"
