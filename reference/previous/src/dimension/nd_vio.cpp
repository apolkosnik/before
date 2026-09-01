/*
  Previous - nd_vio.cpp

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "dimension.hpp"
#include "i860cfg.h"
#include "nd_sdl.hpp"


#define LOG_VID_LEVEL   LOG_NONE

/* --------- NEXTDIMENSION VIDEO I/O ---------- *
 *                                              *
 * Code for NeXTdimension video I/O. For now    *
 * only some dummy devices.                     */


#define ND_VID_DMCD     0x8A
#define ND_VID_DCSC0    0xE0
#define ND_VID_DCSC1    0xE2


/* SAA7191 (Digital Multistandard Colour Decoder - Square Pixel) */

DMCD::DMCD(NextDimension* nd) : nd(nd) {}

void DMCD::write(uint32_t step, uint8_t data) {
    if (step == 0) {
        addr = data;
    } else {
        Log_Printf(LOG_VID_LEVEL, "[ND] Slot %i: DMCD: Writing register %i (%02X)", nd->slot, addr, data);
        
        if (addr < ND_DMCD_NUM_REG) {
            reg[addr] = data;
            addr++;
        } else {
            Log_Printf(LOG_WARN, "[ND] Slot %i: DMCD: Illegal register (%i)", nd->slot, addr);
        }
    }
}


/* SAA7192 (Digital Colour Space Converter) */

#define ND_DCSC_CTRL    0x00
#define ND_DCSC_CLUT    0x01

DCSC::DCSC(NextDimension* nd, int dev) : nd(nd), dev(dev) {}

void DCSC::write(uint32_t step, uint8_t data) {
    switch (step) {
        case 0:
            addr = data;
            break;
        case 1:
            if (addr == ND_DCSC_CTRL) {
                Log_Printf(LOG_VID_LEVEL, "[ND] Slot %i: DCSC%i: Writing control (%02X)", nd->slot, dev, data);
                
                ctrl = data;
                break;
            }
            /* else fall through */

        default:
            if (addr == ND_DCSC_CLUT) {
                Log_Printf(LOG_VID_LEVEL, "[ND] Slot %i: DCSC%i: Writing LUT at %i (%02X)", nd->slot, dev, step-1, data);

                lut[(step-1)&0xFF] = data;
            } else {
                Log_Printf(LOG_WARN, "[ND] Slot %i: DCSC%i: Unknown address (%02X)", nd->slot, dev, addr);
            }
            break;
    }
}


/* Receiver function (from IIC bus) */

void NextDimension::video_dev_write(uint8_t addr, uint32_t step, uint8_t data) {
    if (addr&0x01) {
        Log_Printf(LOG_WARN, "[ND] Slot %i: IIC bus: Unimplemented read from video device at %02X", slot, addr);
    }
    
    if (step == 0) {
        Log_Printf(LOG_VID_LEVEL, "[ND] Slot %i: IIC bus: selecting device at %02X (subaddress: %02X)", slot, addr, data);
    }
    
    switch (addr&0xFE) {
        case ND_VID_DMCD:
            dmcd.write(step, data);
            break;
        case ND_VID_DCSC0:
            dcsc0.write(step, data);
            break;
        case ND_VID_DCSC1:
            dcsc1.write(step, data);
            break;
            
        default:
            Log_Printf(LOG_WARN, "[ND] Slot %i: IIC bus: Unknown device (%02X)", slot, addr);
            break;
    }
}
