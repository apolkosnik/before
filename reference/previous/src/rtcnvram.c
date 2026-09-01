/*
  Previous - rtcnvram.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the MC68HC68T1 and MCCS1850 Real Time
  Clock chips and NVRAM configuration.
*/
const char Rtcnvram_fileid[] = "Previous rtcnvram.c";

#include "main.h"
#include "timing.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "log.h"
#include "configuration.h"
#include "dimension.hpp"
#include "sysReg.h"
#include "rtcnvram.h"

#include <time.h>

#define LOG_RTC_LEVEL   LOG_DEBUG


/* ---------------- RTC serial interface ---------------- */
#define RTC_ADDR_WRITE  0x80
#define RTC_ADDR_CLOCK  0x20
#define RTC_ADDR_MASK   0x7F
static uint8_t rtc_addr = 0;
static uint8_t rtc_val  = 0;
static uint8_t rtc_data = 0;
static int     phase    = 0;


/* --------------------- MC68HC68T1 --------------------- *
 *
 * RTC NVRAM is located at address 0x00 to 0x1F (32 bytes),
 * time registers, alarm registers and control/status
 * registers are located at address 0x20 to 0x32.
 */

static char rtc_treg_name[8][8] = {
    "second ",
    "minute ",
    "hour   ",
    "wday   ",
    "day    ",
    "month  ",
    "year   ",
    "unknown"
};

typedef struct {
    uint8_t sec;      /* 00 - 59 */
    uint8_t min;      /* 00 - 59 */
    uint8_t hour;     /* 01 - 12 or 00 - 24; bit 7: 1 = 12 hr, 0 = 24 hr; bit 5: 1 = pm, 0 = am */
    uint8_t wday;     /* 01 - 07; 1 = sunday */
    uint8_t mday;     /* 01 - 31 */
    uint8_t month;    /* 01 - 12; 1 = january */
    uint8_t year;     /* 00 - 99 */
} RTC_TIME;

typedef struct {
    uint8_t sec;      /* 00 - 59 */
    uint8_t min;      /* 00 - 59 */
    uint8_t hour;     /* 01 - 12 or 00 - 24; bit 5: 1 = pm, 0 = am in 24 hr mode */
} RTC_ALARM;

struct {
    uint8_t ram[32];    /* 0x00 - 0x1F (r), 0x80 - 0x9F (w) */
    RTC_TIME time;      /* 0x20 - 0x26 (r), 0xA0 - 0xA6 (w) */
    RTC_ALARM alarm;    /* 0xA8 - 0xAA (w) */
    uint8_t status;     /* 0x30 (r) */
    uint8_t clkctrl;    /* 0x31 (r), 0xB1 (w) */
    uint8_t intctrl;    /* 0x32 (r), 0xB2 (w) */
} rtc;

/* All time values in RTC clock are in packed decimal format.
 * Year is supported up to 2050 through overflow of decimal decade.
 */
static uint8_t toBCD(int val) {
    return (((val / 10) % 10) << 4) | (val % 10);
}

static uint8_t toBCDyr(int val) {
    return (((val / 10) & 0xF) << 4) | (val % 10);
}

static uint8_t toBCDhr(int val) {
    if (rtc.time.hour & 0x80) {
        if ((val % 12) == 0) val += 12;
        if (val > 12) {
            return toBCD(val-12) | 0xA0;
        } else {
            return toBCD(val) | 0x80;
        }
    } else {
        return toBCD(val);
    }
}

static int fromBCD(uint8_t bcd) {
    return ((bcd & 0xF0) >> 4) * 10 + (bcd & 0xF);
}

static int fromBCDhr(uint8_t bcd) {
    if (bcd & 0x80) {
        if ((bcd & 0x1F) == 0x12) bcd &= 0xA0;
        if (bcd & 0x20) {
            return fromBCD(bcd & 0x1F) + 12;
        } else {
            return fromBCD(bcd & 0x1F);
        }
    } else {
        return fromBCD(bcd);
    }
}

static void oldrtc_get_time(void) {
    struct tm* t = Timing_GetUnixTimeStruct();
    
    rtc.time.sec   = toBCD(t->tm_sec);
    rtc.time.min   = toBCD(t->tm_min);
    rtc.time.hour  = toBCDhr(t->tm_hour);
    rtc.time.wday  = toBCD(t->tm_wday + 1);
    rtc.time.mday  = toBCD(t->tm_mday);
    rtc.time.month = toBCD(t->tm_mon + 1);
    rtc.time.year  = toBCDyr(t->tm_year);
}

static void oldrtc_set_time(void) {
    static struct tm t;
    
    t.tm_sec  = fromBCD(rtc.time.sec);
    t.tm_min  = fromBCD(rtc.time.min);
    t.tm_hour = fromBCDhr(rtc.time.hour);
    t.tm_wday = fromBCD(rtc.time.wday) - 1;
    t.tm_mday = fromBCD(rtc.time.mday);
    t.tm_mon  = fromBCD(rtc.time.month) - 1;
    t.tm_year = fromBCD(rtc.time.year);
    
    Timing_SetUnixTimeStruct(&t);
}

static void oldrtc_check_time(void) {
    uint8_t year;
    
    oldrtc_get_time();
    year = fromBCD(rtc.time.year);
    if (year >= NEXT_LIMIT_YEAR || year < NEXT_MIN_YEAR) {
        Log_Printf(LOG_WARN,"[RTC] Year %d beyond valid range. Setting year to %d.", 1900+year, 1900+NEXT_START_YEAR);
        rtc.time.year = toBCDyr(NEXT_START_YEAR);
        oldrtc_set_time();
    }
}


/* There are 3 status and control registers inside the chip */

/* RTC Status Register at 0x30 (r only)
 *
 * 0-0- ----    always 0 (0 in bit 7 identifies MC68HC68T1 chip)
 * -x-- ----    watchdog detected cpu failure
 * ---x ----    first time up
 * ---- x---    interrupt true (one of following interrupts is valid)
 * ---- -x--    power sense interrupt
 * ---- --x-    alarm interrupt
 * ---- ---x    clock interrupt
 */

#define RTC_CPUFAIL     0x40
#define RTC_FIRSTUP     0x10
#define RTC_INT         0x08
#define RTC_INT_SENSE   0x04
#define RTC_INT_ALARM   0x02
#define RTC_INT_CLOCK   0x01


/* RTC Clock Control Register at 0x31 (r) or 0xB1 (w)
 *
 * x--- ----    1 = start, 0 = stop counter
 * -x-- ----    1 = enable line input, 0 = enable chrystal input
 * --xx ----    chrystal select: 0 = 4.194304 MHz, 1 = 2.097152, 2 = 1.048576, 3 = 32.768 kHz
 * ---- x---    1 = line input 50 Hz, 0 = line input 60 Hz
 * ---- -xxx    clock out frequency:
 *
 * 0 = chrystal
 * 1 = chrystal/2
 * 2 = chrystal/4
 * 3 = chrystal/8
 * 4 = disable
 * 5 = 1 Hz
 * 6 = 2 Hz
 * 7 = 50/60 Hz for line operation; 64 Hz for chrystal operation
 */

#define RTC_START   0x80
#define RTC_STOP    0x00
#define RTC_LINE    0x40
#define RTC_XTAL    0x30
#define RTC_L50HZ   0x08
#define RTC_FREQ    0x07


/* RTC Interrupt Control Register at 0x32 (r) or 0xB2 (w)
 *
 * x--- ----    watchdog enable
 * -x-- ----    initiate power down
 * --x- ----    power sense
 * ---x ----    enable alarm
 * ---- xxxx    select frequency of periodic interrupt:
 *
 * 0 = diable
 * chrystal:
 * 1 = 2048 Hz, 2 = 1024 Hz, ... , C = 1 Hz
 * D = 1 per min, E = 1 per hour, F = 1 per day
 * line:
 * 6 = 50 or 60 Hz, B = 2 Hz, C = 1 Hz
 * D = 1 per min, E = 1 per hour, F = 1 per day
 */

#define RTC_WATCHDOG    0x80
#define RTC_POWERDOWN   0x40
#define RTC_PWRSENSE    0x20
#define RTC_ENABLEALRM  0x10
#define RTC_PERIODIC    0x0F


/* Read and write RTC registers */
static uint8_t oldrtc_get_clock(uint8_t addr) {
    uint8_t val = 0x00;
        
    switch (rtc_addr&RTC_ADDR_MASK) {
        case 0x20: /* seconds */
            val = rtc.time.sec; break;
        case 0x21: /* minutes */
            val = rtc.time.min; break;
        case 0x22: /* hours */
            val = rtc.time.hour; break;
        case 0x23: /* day of week (sunday = 1) */
            val = rtc.time.wday; break;
        case 0x24: /* day of month */
            val = rtc.time.mday; break;
        case 0x25: /* month */
            val = rtc.time.month; break;
        case 0x26: /* year (0 - 99) */
            val = rtc.time.year; break;            
        case 0x30: /* status register */
            val = rtc.status & 0x5F;
            rtc.status &= RTC_INT_SENSE;
            break;
        case 0x31: /* clock control register */
            val = rtc.clkctrl; break;
        case 0x32: /* interrupt control register */
            val = rtc.intctrl; break;
            
        default: break;
    }
    
    return val;
}

static void oldrtc_put_clock(uint8_t addr, uint8_t val) {
    uint8_t changed = val;
    
    switch (addr&RTC_ADDR_MASK) {
        case 0x20: /* seconds */
            rtc.time.sec = val;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x21: /* minutes */
            rtc.time.min = val;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x22: /* hours */
            rtc.time.hour = val&0xBF;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x23: /* day of week (sunday = 1) */
            rtc.time.wday = val&0x07;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x24: /* day of month */
            rtc.time.mday = val;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x25: /* month */
            rtc.time.month = val;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
        case 0x26: /* year (0 - 99) */
            rtc.time.year = val;
            if (rtc.clkctrl&RTC_START) {
                oldrtc_set_time();
            }
            break;
            
        case 0x28: /* alarm: seconds */
            rtc.alarm.sec = val;
            break;
        case 0x29: /* alarm: minutes */
            rtc.alarm.min = val;
            break;
        case 0x2A: /* alarm: hours */
            rtc.alarm.hour = val&0x3F;
            break;
            
        case 0x31: /* clock control register */
            changed ^= rtc.clkctrl;
            rtc.clkctrl = val;
            if (changed&RTC_START) {
                if (rtc.clkctrl&RTC_START) {
                    Log_Printf(LOG_WARN, "[RTC] Start clock");
                    oldrtc_set_time();
                } else {
                    Log_Printf(LOG_WARN, "[RTC] Stop clock");
                }
            }
            if (changed&RTC_LINE) {
                Log_Printf(LOG_RTC_LEVEL, "[RTC] %s input", (rtc.intctrl&RTC_LINE) ? "Line" : "Chrystal");
            }
            break;
        case 0x32: /* interrupt control register */
            changed ^= rtc.intctrl;
            rtc.intctrl = val;
            if (rtc.intctrl&RTC_POWERDOWN) {
                Log_Printf(LOG_WARN, "[RTC] Power down!");
                Main_RequestQuit(false);
                return;
            }
            if (changed&RTC_WATCHDOG) {
                Log_Printf(LOG_WARN, "[RTC] %s watchdog (unimplemented)", (rtc.intctrl&RTC_WATCHDOG) ? "Enable" : "Disable");
            }
            if (changed&RTC_PWRSENSE) {
                Log_Printf(LOG_WARN, "[RTC] %s power sense (unimplemented)", (rtc.intctrl&RTC_PWRSENSE) ? "Enable" : "Disable");
            }
            if (changed&RTC_ENABLEALRM) {
                Log_Printf(LOG_WARN, "[RTC] %s alarm", (rtc.intctrl&RTC_ENABLEALRM) ? "Enable" : "Disable");
            }
            break;
            
        default: break;
    }
    if ((addr&RTC_ADDR_MASK) <= 0x26) {
        Log_Printf(LOG_WARN,"[RTC] setting %s to %02x",rtc_treg_name[addr&7],val);
    }
}

/* RTC alarm */
#if 0
static void oldrtc_check_alarm(void) {
    if (rtc.intctrl&RTC_ENABLEALRM) {
        if (rtc.clkctrl&RTC_START) {
            oldrtc_get_time();
        }
        if ((rtc.time.hour&0x3F) == (rtc.alarm.hour&0x3F) &&
            rtc.time.sec == rtc.alarm.sec &&
            rtc.time.min == rtc.alarm.min) {
            rtc.status |= (RTC_INT_ALARM | RTC_INT);
        }
    }
}
#endif

/* Interrupt pin of RTC is not connected. Interrupts are generated from the keyboard. */
static void oldrtc_request_power_down(void) {
    set_interrupt(INT_POWER, SET_INT);
}

static void oldrtc_stop_pdown_request(void) {
    set_interrupt(INT_POWER, RELEASE_INT);
}


static int oldrtc_interface_io(uint8_t rtdatabit) {
    
    phase++;
    
    if (phase<=8) {
        rtc_addr = (rtc_addr<<1)|(rtdatabit?1:0);
    } else {
        
        if (phase==9) {
            if (!(rtc_addr&RTC_ADDR_WRITE)) {
                if (rtc_addr&RTC_ADDR_CLOCK) {
                    rtc_val = oldrtc_get_clock(rtc_addr);
                } else {
                    rtc_val = rtc.ram[rtc_addr&RTC_ADDR_MASK];
                }
                
                Log_Printf(LOG_RTC_LEVEL,"[RTC] reading val $%02X from addr $%02X",rtc_val,rtc_addr);
            }
        }
        
        if (rtc_addr&RTC_ADDR_WRITE) {
            rtc_val = (rtc_val<<1)|(rtdatabit?1:0);
        } else {
            rtdatabit = (rtc_val&(1<<(16-phase)))?1:0;
        }
        
        if (phase==16) {
            if (rtc_addr&RTC_ADDR_WRITE) {
                Log_Printf(LOG_RTC_LEVEL,"[RTC] writing val $%02X to addr $%02X",rtc_val,rtc_addr);
                
                if (rtc_addr&RTC_ADDR_CLOCK) {
                    oldrtc_put_clock(rtc_addr, rtc_val);
                } else {
                    rtc.ram[rtc_addr&RTC_ADDR_MASK] = rtc_val;
                }
            }
            
            switch (rtc_addr) {
                case 0x1F:
                case 0x9F: rtc_addr = 0x00; break;
                case 0x32:
                case 0xB2: rtc_addr = 0x20; break;
                default: rtc_addr++; break;
            }
            phase-=8;
        }
    }
    
    /* RTC returns 0 or 1 */
    return rtdatabit;
}

/* Time counter is freezed while serial interface is active */
static void oldrtc_interface_start(void) {
    if (rtc.clkctrl&RTC_START) {
        oldrtc_get_time();
    }
}


/* ------------------------- MCCS1850 ------------------------- */

/* RTC NVRAM (64 bytes) is located at address 0x00 to 0x1F
 * and 0x40 to 0x5F, time registers, alarm registers and 
 * control/status registers are located at address 0x20 to 0x31.
 */


/* New RTC has two 32 bit counters, one for time and one for alarm */

/* There are 3 status and control registers inside the chip */

/* RTC Status Register at 0x30 (r only)
 *
 * 1--- ----    always 1 (identifies MCCS1850 chip)
 * -0-- ----    always 0
 * --x- ----    test mode status
 * ---x ----    first time up
 * ---- x---    interrupt true (one of following interrupts is valid)
 * ---- -x--    low battery interrupt
 * ---- --x-    alarm interrupt
 * ---- ---x    power down interrupt
 */

#define NRTC_NEWCHIP    0x80
#define NRTC_TMODE      0x20
#define NRTC_FIRSTUP    0x10
#define NRTC_INT        0x08
#define NRTC_INT_LBAT   0x04
#define NRTC_INT_ALARM  0x02
#define NRTC_INT_PDOWN  0x01


/* RTC Control Register at 0x31 (r) or 0xB1 (w)
 *
 * x--- ----    1 = start, 0 = stop counter
 * -x-- ----    initiate power down
 * --x- ----    enable auto restart sequence
 * ---x ----    enable alarm
 * ---- x---    alarm clear (clear alarm int bit in status)
 * ---- -x--    first time up clear (clear first up bit in status)
 * ---- --x-    enable low battery interrupting
 * ---- ---x    request power down clear (clear power down int bit in status)
 *
 * ---- xx-x    always read as 0
 */

#define NRTC_CTRL_0     0x4D

#define NRTC_START      0x80
#define NRTC_STOP       0x00
#define NRTC_POWERDOWN  0x40
#define NRTC_AR         0x20
#define NRTC_ENABLEALRM 0x10
#define NRTC_CLRALARM   0x08
#define NRTC_CLRFTU     0x04
#define NRTC_LBE        0x02
#define NRTC_CLRPDOWN   0x01


struct {
    /* --> see old chip  * 0x00 - 0x1F (r), 0x80 - 0x9F (w) */
    uint32_t timecntr;    /* 0x20 - 0x23 (r), 0xA0 - 0xA3 (w) */
    uint32_t alarmcntr;   /* 0x24 - 0x27 (r), 0xA4 - 0xA7 (w) */
    uint8_t status;       /* 0x30 (r) */
    uint8_t control;      /* 0x31 (r), 0xB1 (w) */
    uint8_t ram2[32];     /* 0x40 - 0x5F (r), 0xC0 - 0xDF (w) */
} newrtc;

#define RTC_ADDR_NEWRAM 0x40

/* RTC interrupts */
static void newrtc_check_interrupt(void) {
    if (((newrtc.status&NRTC_INT_ALARM) && (newrtc.control&NRTC_ENABLEALRM)) ||
        ((newrtc.status&NRTC_INT_LBAT)  && (newrtc.control&NRTC_LBE))        ||
        ((newrtc.status&NRTC_FIRSTUP)   || (newrtc.status&NRTC_INT_PDOWN))) {
        newrtc.status |= NRTC_INT;
        set_interrupt(INT_POWER, SET_INT);
    } else {
        newrtc.status &= ~NRTC_INT;
        set_interrupt(INT_POWER, RELEASE_INT);
    }
}

/* Read and write RTC */
static uint8_t newrtc_get_clock(uint8_t addr) {
    uint8_t val = 0x00;
        
    switch (rtc_addr&RTC_ADDR_MASK) {
        case 0x20:
            val = (newrtc.timecntr>>24);
            break;
        case 0x21:
            val = (newrtc.timecntr>>16);
            break;
        case 0x22:
            val = (newrtc.timecntr>>8);
            break;
        case 0x23:
            val = newrtc.timecntr;
            break;
            
        case 0x24:
            val = (newrtc.alarmcntr>>24);
            break;
        case 0x25:
            val = (newrtc.alarmcntr>>16);
            break;
        case 0x26:
            val = (newrtc.alarmcntr>>8);
            break;
        case 0x27:
            val = newrtc.alarmcntr;
            break;

        case 0x30: /* status register */
            val = (newrtc.status&0x3F)|NRTC_NEWCHIP;
            break;
        case 0x31: /* control register */
            val = newrtc.control&~NRTC_CTRL_0;
            break;
            
        default:
            break;
    }
    
    return val;
}

static void newrtc_put_clock(uint8_t addr, uint8_t val) {
    uint8_t changed = val;
    
    switch (rtc_addr&RTC_ADDR_MASK) {
        case 0x20:
            newrtc.timecntr &= 0x00FFFFFF;
            newrtc.timecntr |= val << 24;
            if (newrtc.control&NRTC_START) {
                Timing_SetUnixTime(newrtc.timecntr);
            }
            break;
        case 0x21:
            newrtc.timecntr &= 0xFF00FFFF;
            newrtc.timecntr |= val << 16;
            if (newrtc.control&NRTC_START) {
                Timing_SetUnixTime(newrtc.timecntr);
            }
            break;
        case 0x22:
            newrtc.timecntr &= 0xFFFF00FF;
            newrtc.timecntr |= val << 8;
            if (newrtc.control&NRTC_START) {
                Timing_SetUnixTime(newrtc.timecntr);
            }
            break;
        case 0x23:
            newrtc.timecntr &= 0xFFFFFF00;
            newrtc.timecntr |= val;
            if (newrtc.control&NRTC_START) {
                Timing_SetUnixTime(newrtc.timecntr);
            }
            break;
        case 0x24:
            newrtc.alarmcntr &= 0x00FFFFFF;
            newrtc.alarmcntr |= val << 24;
            break;
        case 0x25:
            newrtc.alarmcntr &= 0xFF00FFFF;
            newrtc.alarmcntr |= val << 16;
            break;
        case 0x26:
            newrtc.alarmcntr &= 0xFFFF00FF;
            newrtc.alarmcntr |= val << 8;
            break;
        case 0x27:
            newrtc.alarmcntr &= 0xFFFFFF00;
            newrtc.alarmcntr |= val;
            break;
            
        case 0x31: /* control register */
            changed ^= newrtc.control;
            newrtc.control = val;
            if (newrtc.control&NRTC_POWERDOWN) {
                Log_Printf(LOG_WARN, "[RTC] Power down!");
                Main_RequestQuit(false);
                return;
            }
            if (changed&NRTC_START) {
                if (newrtc.control&RTC_START) {
                    Log_Printf(LOG_WARN, "[RTC] Start clock");
                    Timing_SetUnixTime(newrtc.timecntr);
                } else {
                    Log_Printf(LOG_WARN, "[RTC] Stop clock");
                }
            }
            if (changed&NRTC_AR) {
                Log_Printf(LOG_WARN, "[RTC] %s auto reset (unimplemented)", (newrtc.control&NRTC_AR) ? "Enable" : "Disable");
            }
            if (changed&NRTC_ENABLEALRM) {
                Log_Printf(LOG_WARN, "[RTC] %s alarm", (newrtc.control&NRTC_ENABLEALRM) ? "Enable" : "Disable");
            }
            if (newrtc.control&NRTC_CLRFTU) {
                newrtc.status &= ~NRTC_FIRSTUP;
            }
            if (newrtc.control&NRTC_CLRALARM) {
                newrtc.status &= ~NRTC_INT_ALARM;
            }
            if (newrtc.control&NRTC_CLRPDOWN) {
                newrtc.status &= ~NRTC_INT_PDOWN;
            }
            if (!(newrtc.control&NRTC_LBE)) {
                newrtc.status &= ~NRTC_INT_LBAT;
            }
            newrtc_check_interrupt();
            break;
            
        default: break;
    }
}

#if 0
static void newrtc_check_alarm(void) {
    if (newrtc.control&NRTC_START) {
        newrtc.timecntr = (uint32_t)host_unix_time();
    }
    if (newrtc.alarmcntr == newrtc.timecntr) {
        newrtc.status |= NRTC_INT_ALARM;
        newrtc_check_interrupt();
    }
}
#endif

static void newrtc_check_time(void) {
    newrtc.timecntr = (uint32_t)Timing_GetUnixTime();
    if (newrtc.timecntr >= NEXT_LIMIT_SEC || newrtc.timecntr < NEXT_MIN_SEC) {
        Log_Printf(LOG_WARN,"[RTC] Time %d beyond valid range. Setting to %d", newrtc.timecntr, NEXT_START_SEC);
        newrtc.timecntr = NEXT_START_SEC;
        Timing_SetUnixTime(newrtc.timecntr);
    }
}

static void newrtc_request_power_down(void) {
    newrtc.status |= NRTC_INT_PDOWN;
    newrtc_check_interrupt();
}

static void newrtc_stop_pdown_request(void) {}


static int newrtc_interface_io(uint8_t rtdatabit) {
    
    phase++;
    
    if (phase<=8) {
        rtc_addr = (rtc_addr<<1)|(rtdatabit?1:0);
    } else {
        
        if (phase==9) {
            if (!(rtc_addr&RTC_ADDR_WRITE)) {
                if (rtc_addr&RTC_ADDR_CLOCK) {
                    rtc_val = newrtc_get_clock(rtc_addr);
                } else {
                    if (rtc_addr&RTC_ADDR_NEWRAM) {
                        rtc_val = newrtc.ram2[rtc_addr&0x1F];
                    } else {
                        rtc_val = rtc.ram[rtc_addr&0x1F];
                    }
                }
                
                Log_Printf(LOG_RTC_LEVEL,"[RTC] reading val $%02X from addr $%02X",rtc_val,rtc_addr);
            }
        }
        
        if (rtc_addr&RTC_ADDR_WRITE) {
            rtc_val = (rtc_val<<1)|(rtdatabit?1:0);
        } else {
            rtdatabit = (rtc_val&(1<<(16-phase)))?1:0;
        }
        
        if (phase==16) {
            if (rtc_addr&RTC_ADDR_WRITE) {
                Log_Printf(LOG_RTC_LEVEL,"[RTC] writing val $%02X to addr $%02X",rtc_val,rtc_addr);
                
                if (rtc_addr&RTC_ADDR_CLOCK) {
                    newrtc_put_clock(rtc_addr, rtc_val);
                } else {
                    if (rtc_addr&RTC_ADDR_NEWRAM) {
                        newrtc.ram2[rtc_addr&0x1F] = rtc_val;
                    } else {
                        rtc.ram[rtc_addr&0x1F] = rtc_val;
                    }
                }
            }
            
            switch (rtc_addr) {
                case 0x7F: rtc_addr = 0x00; break;
                case 0xFF: rtc_addr = 0x80; break;
                default: rtc_addr++; break;
            }
            phase-=8;
        }
    }
    
    /* RTC returns 0 or 1 */
    return rtdatabit;
}

/* Data sheet is wrong about when the time counter latch is loaded. But is this correct? */
static void newrtc_interface_start(void) {
    if (newrtc.control&NRTC_START) {
        newrtc.timecntr = (uint32_t)Timing_GetUnixTime();
    }
}


/* ----------------- Common RTC interface ---------------- */
void rtc_interface_write(uint8_t rtdatabit) {
    if (ConfigureParams.System.nRTC == MCCS1850) {
        rtc_data = newrtc_interface_io(rtdatabit);
    } else {
        rtc_data = oldrtc_interface_io(rtdatabit);
    }
}

uint8_t rtc_interface_read(void) {
    return rtc_data;
}

void rtc_interface_start(void) {
    if (ConfigureParams.System.nRTC == MCCS1850) {
        newrtc_interface_start();
    } else {
        oldrtc_interface_start();
    }
}

void rtc_interface_reset(void) {
    Log_Printf(LOG_RTC_LEVEL, "[RTC] interface reset");
    
    phase    = 0;
    rtc_addr = 0;
    rtc_val  = 0;
    if (ConfigureParams.System.bTurbo) {
        rtc_data = 0;
    } else {
        rtc_data = 1;
    }
}


/* ----------------- RTC power interface ----------------- */
void rtc_request_power_down(void) {
    if (ConfigureParams.System.nRTC == MCCS1850) {
        newrtc_request_power_down();
    } else {
        oldrtc_request_power_down();
    }
}

void rtc_stop_pdown_request(void) {
    if (ConfigureParams.System.nRTC == MCCS1850) {
        newrtc_stop_pdown_request();
    } else {
        oldrtc_stop_pdown_request();
    }
}


/* -------------------- RTC time check ------------------- */
static void rtc_check_time(void) {
    if (ConfigureParams.System.nRTC == MCCS1850) {
        newrtc_check_time();
    } else {
        oldrtc_check_time();
    }
}


/* ---------------------- RTC reset ---------------------- */
void RTC_Reset(void) {
    Log_Printf(LOG_WARN, "[RTC] Reset");
    rtc.clkctrl = newrtc.control = RTC_START;
    rtc_check_time();
    nvram_init();
    rtc_interface_reset();
}



/* ---------------------- RTC NVRAM ---------------------- */

/* file mon/nvram.h
 * struct nvram_info {
 * #define	NI_RESET	9
 * 	u_int	ni_reset : 4,
 * #define	SCC_ALT_CONS	0x08000000
 * 		ni_alt_cons : 1,
 * #define	ALLOW_EJECT	0x04000000
 * 		ni_allow_eject : 1,
 * 		ni_vol_r : 6,
 * 		ni_brightness : 6,
 * #define	HW_PWD	0x6
 * 		ni_hw_pwd : 4,
 * 		ni_vol_l : 6,
 * 		ni_spkren : 1,
 * 		ni_lowpass : 1,
 * #define	BOOT_ANY	0x00000002
 * 		ni_boot_any : 1,
 * #define	ANY_CMD		0x00000001
 * 		ni_any_cmd : 1;
 * #define	NVRAM_HW_PASSWD	6
 * 	u_char ni_ep[NVRAM_HW_PASSWD];
 * #define	ni_enetaddr	ni_ep
 * #define	ni_hw_passwd	ni_ep
 * 	u_short ni_simm;		        (4 SIMMs, 4 bits per SIMM)
 * 	char ni_adobe[2];
 * 	u_char ni_pot[3];
 * 	u_char	ni_new_clock_chip : 1,
 * 		ni_auto_poweron : 1,
 * 		ni_use_console_slot : 1,	(console slot was set by user)
 * 		ni_console_slot : 2,		(preferred console dev slot>>1)
 * 		ni_use_parity_mem : 1,	    (use parity RAM if available)
 * 		: 2;
 * #define	NVRAM_BOOTCMD	12
 * 	char ni_bootcmd[NVRAM_BOOTCMD];
 * 	u_short ni_cksum;
 * };
 *
 * #define	N_brightness	0
 * #define	N_volume_l	1
 * #define	N_volume_r	2
 *
 * nominal values during self test
 * #define	BRIGHT_NOM	20
 * #define	VOL_NOM		0
 */

/* bits in ni_pot[0] */
#define POT_ON              0x01
#define EXTENDED_POT        0x02
#define LOOP_POT            0x04
#define VERBOSE_POT         0x08
#define TEST_DRAM_POT       0x10
#define BOOT_POT            0x20
#define TEST_MONITOR_POT    0x40

/* bits in byte 17 */
#define NEW_CLOCK_CHIP      0x80
#define AUTO_POWERON        0x40
#define USE_CONSOLE_SLOT    0x20
#define CONSOLE_SLOT        0x18
#define USE_PARITY_MEM      0x04

/* bits in ni_simm (rtc ram byte 10 and 11) *
 * -------- -----xxx bit 0 - 2: 1st simm: bit 1+2 define size, bit 3 defines page mode
 * -------- --xxx--- bit 3 - 5: 2nd simm: bit 1+2 define size, bit 3 defines page mode
 * -------x xx------ bit 6 - 8: 3rd simm: bit 1+2 define size, bit 3 defines page mode
 * ----xxx- -------- bit 9 -11: 4th simm: bit 1+2 define size, bit 3 defines page mode
 * xxxx---- -------- bit 12-15: defines parity, 1 bit for each simm (ignored on 68030)
 */
/* for 68030 and monochrome non-turbo systems */
#define SIMM_EMPTY          0x0
#define SIMM_16MB           0x1 /* Group of four 4 Mbyte SIMMs */
#define SIMM_4MB            0x2 /* Group of four 1 Mbyte SIMMs */
#define SIMM_1MB            0x3 /* Group of four 256 KByte SIMMs */
#define SIMM_PAGE_MODE      0x4 /* SIMM type is page mode, else nibble mode */
/* for all 68040 systems */
#define SIMM_PARITY         0x8 /* SIMMs support parity */
/* for non-turbo color systems */
#define SIMM_8MB_C          0x1 /* Pair of 4 Mbyte SIMMs */
#define SIMM_2MB_C          0x2 /* Pair of 1 Mbyte SIMMs */
#define SIMM_EMPTY2         0x3 /* reserved */
/* for turbo systems */
#define SIMM_32MB_T         0x1 /* Pair of 16 or 32 MByte SIMMs (front or back) */
#define SIMM_8MB_T          0x2 /* Pair of 4 or 8 MByte SIMMs (front or back) */
#define SIMM_2MB_T          0x3 /* Pair of 1 or 2 MByte SIMMs (front or back) */

/* bits in ni_reset (rtc ram byte 0 to 3) *
 * -------- -------- -------- -------x bit 0:     any cmd
 * -------- -------- -------- ------x- bit 1:     boot any
 * -------- -------- -------- -----x-- bit 2:     enable lowpass filter
 * -------- -------- -------- ----x--- bit 3:     disable speaker
 * -------- -------- ------xx xxxx---- bit 4-9:   volume left (max 0, min 0x2B)
 * -------- -------- --xxxx-- -------- bit 10-13: hardware password
 * -------- ----xxxx xx------ -------- bit 14-19: brightness (max 0x3D, min 0)
 * ------xx xxxx---- -------- -------- bit 20-25: volume right (max 0, min 0x2B)
 * -----x-- -------- -------- -------- bit 26:    allow eject
 * ----x--- -------- -------- -------- bit 27:    alt cons
 * xxxx---- -------- -------- -------- bit 28-31: reset
 */

/* RTC RAM */
uint8_t nvram_default[32]={
    0x94,0x0f,0x40,0x00, /* byte 0 - 3: volume, brightness, ... */
    0x00,0x00,0x00,0x00,0x00,0x00, /* byte 4 - 9: hardware password, ethernet address (?) */
    0x00,0x00, /* byte 10, 11: simm type and size (4 simms, 4 bits per simm), see bits in ni_simm above */
    0x00,0x00, /* byte 12, 13: adobe (?) */
    0x4b,0x00,0x00, /* byte 14: POT, byte 15: oldest ..., byte 16: most recent selftest error code */
    0x00, /* byte 17: bit7:clock chip; 6:auto poweron; 5:enable console slot; 3,4:console slot; 2:parity mem */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, /* byte 18 - 29: boot command */
    0x0F,0x13 /* byte 30, 31: checksum */
};

void nvram_init(void) {
    /* Reset RTC RAM */
    memset(rtc.ram, 0, 32);
    
    /* Build configuration bytes */
    uint32_t config = 0x94000000; /* reset = 9, allow eject = 1 */
    config |= 0x3D<<14; /* brightness */
    
    rtc.ram[0] = config>>24;
    rtc.ram[1] = config>>16;
    rtc.ram[2] = config>>8;
    rtc.ram[3] = config;
    
    /* Build boot command */
    switch (ConfigureParams.Boot.nBootDevice) {
        case BOOT_ROM:
            rtc.ram[18] = 0x00;
            rtc.ram[19] = 0x00;
            break;
        case BOOT_SCSI:
            rtc.ram[18] = 's';
            rtc.ram[19] = 'd';
            break;
        case BOOT_ETHERNET:
            rtc.ram[18] = 'e';
            rtc.ram[19] = 'n';
            break;
        case BOOT_MO:
            rtc.ram[18] = 'o';
            rtc.ram[19] = 'd';
            break;
        case BOOT_FLOPPY:
            rtc.ram[18] = 'f';
            rtc.ram[19] = 'd';
            break;
            
        default: break;
    }
    
    /* Copy ethernet address from ROM to RTC RAM */
    int i;
    for (i = 0; i<6; i++) {
        rtc.ram[i+4]=NEXTRom[i+8];
    }
    
    /* Build SIMM bytes */
    uint16_t SIMMconfig = 0x0000;
    uint8_t simm[4];
    uint8_t parity = 0xF0;
    if (ConfigureParams.System.bTurbo) {
        parity = 0x00;
        for (i = 0; i<4; i++) {
            switch (ConfigureParams.Memory.nMemoryBankSize[i]) {
                case 0: simm[i] = SIMM_EMPTY; break;
                case 2: simm[i] = SIMM_2MB_T; break;
                case 8: simm[i] = SIMM_8MB_T; break;
                case 32: simm[i] = SIMM_32MB_T; break;
                default: simm[i] = SIMM_EMPTY; break;
            }
        }
        
    } else if (ConfigureParams.System.bColor) {
        for (i = 0; i<4; i++) {
            switch (ConfigureParams.Memory.nMemoryBankSize[i]) {
                case 0: simm[i] = SIMM_EMPTY; parity &= ~(0x10<<i); break;
                case 2: simm[i] = SIMM_2MB_C; break;
                case 8: simm[i] = SIMM_8MB_C; break;
                default: simm[i] = SIMM_EMPTY; break;
            }
        }
        
    } else {
        for (i = 0; i<4; i++) {
            switch (ConfigureParams.Memory.nMemoryBankSize[i]) {
                case 0: simm[i] = SIMM_EMPTY; parity &= ~(0x10<<i); break;
                case 1: simm[i] = SIMM_1MB | SIMM_PAGE_MODE; break;
                case 4: simm[i] = SIMM_4MB | SIMM_PAGE_MODE; break;
                case 16: simm[i] = SIMM_16MB | SIMM_PAGE_MODE; break;
                default: simm[i] = SIMM_EMPTY | SIMM_PAGE_MODE; break;
            }
        }
    }
    
    SIMMconfig = ((parity&0xF0)<<8) | (simm[3]<<9) | (simm[2]<<6) | (simm[1]<<3) | simm[0];
    rtc.ram[10] = (SIMMconfig>>8)&0xFF;
    rtc.ram[11] = SIMMconfig&0xFF;
    
    /* Build POT byte[0] */
    rtc.ram[14] = 0x00;
    if (ConfigureParams.Boot.bEnableDRAMTest)
        rtc.ram[14] |= TEST_DRAM_POT;
    if (ConfigureParams.Boot.bEnablePot)
        rtc.ram[14] |= POT_ON;
    if (ConfigureParams.Boot.bEnableSoundTest)
        rtc.ram[14] |= TEST_MONITOR_POT;
    if (ConfigureParams.Boot.bEnableSCSITest)
        rtc.ram[14] |= EXTENDED_POT;
    if (ConfigureParams.Boot.bLoopPot)
        rtc.ram[14] |= LOOP_POT;
    if (ConfigureParams.Boot.bVerbose)
        rtc.ram[14] |= VERBOSE_POT;
    if (ConfigureParams.Boot.bExtendedPot)
        rtc.ram[14] |= BOOT_POT;
    
    /* Set clock chip bit */
    switch (ConfigureParams.System.nRTC) {
        case MCCS1850: rtc.ram[17] |= NEW_CLOCK_CHIP; break;
        case MC68HC68T1: rtc.ram[17] &= ~NEW_CLOCK_CHIP; break;
        default: break;
    }

    /* Set prefered console slot */
    rtc.ram[17] |= USE_CONSOLE_SLOT;
    for (i = 0; i < ND_MAX_BOARDS; i++) {
        if ((ConfigureParams.Dimension.nConsoleSlot == ND_SLOT(i)) &&
            ConfigureParams.Dimension.board[i].bEnabled) {
            rtc.ram[17] |= (ND_SLOT(i)>>1)<<3;
            break;
        }
    }

    /* Re-calculate checksum */
    nvram_checksum(1);
}

void nvram_checksum(int force) {
    int sum,i;
    sum=0;
    for (i=0;i<30;i+=2) {
        sum+=(rtc.ram[i]<<8)|(rtc.ram[i+1]);
        if (sum>=0x10000) {
            sum-=0x10000;
            sum+=1;
        }
    }
    
    sum=0xFFFF-sum;
    
    if (force) {
        rtc.ram[30]=(sum&0xFF00)>>8;
        rtc.ram[31]=(sum&0xFF);
        Log_Printf(LOG_WARN,"Forcing RTC checksum to %x %x",rtc.ram[30],rtc.ram[31]);
    } else {
        Log_Printf(LOG_WARN,"Check RTC checksum to %x %x %x %x",
                   rtc.ram[30],(sum&0xFF00)>>8,
                   rtc.ram[31],(sum&0xFF));
    }
}


/* Print parameters stored in NVRAM (for debugger) */
void NVRAM_Info(FILE *fp, uint32_t dummy) {
    fprintf(fp, "Parameters stored in NVRAM\n");
    fprintf(fp, "Reset:             %x\n", (rtc.ram[0]>>4)&0xF);
    fprintf(fp, "Alternate console: %d\n", (rtc.ram[0]>>3)&0x1);
    fprintf(fp, "Allow eject:       %d\n", (rtc.ram[0]>>2)&0x1);
    fprintf(fp, "Enable HW PWD:     %x\n", (rtc.ram[2]>>2)&0xF);
    fprintf(fp, "Brightness:        %d\n", ((rtc.ram[1]&0xF)<<2)|((rtc.ram[2]>>6)&0x3));
    fprintf(fp, "Volume right:      %d\n", ((rtc.ram[0]&0x3)<<4)|((rtc.ram[1]>>4)&0xF));
    fprintf(fp, "Volume left:       %d\n", ((rtc.ram[2]&0x3)<<4)|((rtc.ram[3]>>4)&0xF));
    fprintf(fp, "Speaker enabled:   %d\n", (rtc.ram[3]>>3)&0x1);
    fprintf(fp, "Lowpass enabled:   %d\n", (rtc.ram[3]>>2)&0x1);
    fprintf(fp, "Any boot:          %d\n", (rtc.ram[3]>>1)&0x1);
    fprintf(fp, "Any command:       %d\n", (rtc.ram[3])&0x1);
    fprintf(fp, "Ethernet address:  %02x:%02x:%02x:%02x:%02x:%02x\n",
            rtc.ram[4], rtc.ram[5], rtc.ram[6], rtc.ram[7], rtc.ram[8], rtc.ram[9]);
    fprintf(fp, "SIMM config:       %02x%02x\n", rtc.ram[10], rtc.ram[11]);
    fprintf(fp, "Adobe:             %c%c\n", rtc.ram[12], rtc.ram[13]);
    fprintf(fp, "Enable POT:        %d\n", (rtc.ram[14])&0x1);
    fprintf(fp, "Sound test:        %d\n", (rtc.ram[14]>>6)&0x1);
    fprintf(fp, "Boot diagnostics:  %d\n", (rtc.ram[14]>>5)&0x1);
    fprintf(fp, "DRAM test:         %d\n", (rtc.ram[14]>>4)&0x1);
    fprintf(fp, "Verbose test:      %d\n", (rtc.ram[14]>>3)&0x1);
    fprintf(fp, "Loop test:         %d\n", (rtc.ram[14]>>2)&0x1);
    fprintf(fp, "SCSI test:         %d\n", (rtc.ram[14]>>1)&0x1);
    fprintf(fp, "Old POT error:     %X\n", rtc.ram[15]);
    fprintf(fp, "Recent POT error:  %X\n", rtc.ram[16]);
    fprintf(fp, "New clock chip:    %d\n", (rtc.ram[17]>>7)&0x1);
    fprintf(fp, "Auto power-on:     %d\n", (rtc.ram[17]>>6)&0x1);
    fprintf(fp, "Use console slot:  %d\n", (rtc.ram[17]>>5)&0x1);
    fprintf(fp, "Console slot:      %d\n", ((rtc.ram[17]>>3)&0x3)<<1);
    fprintf(fp, "Use parity memory: %d\n", (rtc.ram[17]>>2)&0x1);
    fprintf(fp, "Boot command:      %c%c%c%c%c%c%c%c%c%c%c%c\n",
            rtc.ram[18], rtc.ram[19], rtc.ram[20], rtc.ram[21], rtc.ram[22], rtc.ram[23],
            rtc.ram[24], rtc.ram[25], rtc.ram[26], rtc.ram[27], rtc.ram[28], rtc.ram[29]);
    fprintf(fp, "Checksum:          %02x%02x\n\n", rtc.ram[30], rtc.ram[31]);
}
