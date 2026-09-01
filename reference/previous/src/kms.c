/*
  Previous - kms.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains keyboard, mouse and sound handling. This logic is 
  located in the Megapixel Display or Soundbox. See snd.c for sound I/O.
*/
const char Kms_fileid[] = "Previous kms.c";

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "reset.h"
#include "kms.h"
#include "sysReg.h"
#include "dma.h"
#include "rtcnvram.h"
#include "snd.h"

#define LOG_KMS_REG_LEVEL LOG_DEBUG
#define LOG_KMS_LEVEL     LOG_DEBUG


struct {
    /* Registers */
    struct {
        uint8_t sound;
        uint8_t km;
        uint8_t transmit;
        uint8_t cmd;
    } status;
    
    uint8_t  command;
    uint32_t data;
    uint32_t kmdata;
    
    /* Internal */
    uint8_t  rev;
    uint32_t km_addr;
    uint32_t km_data[16];
    uint32_t km_mask;
} kms;


/* KMS control and status register (0x0200E000) 
 *
 * x--- ---- ---- ---- ---- ---- ---- ----  sound out enable (r/w)
 * -x-- ---- ---- ---- ---- ---- ---- ----  sound output request (r)
 * --x- ---- ---- ---- ---- ---- ---- ----  sound output underrun detected (r/w)
 * ---- x--- ---- ---- ---- ---- ---- ----  sound in enable (r/w)
 * ---- -x-- ---- ---- ---- ---- ---- ----  sound input request (r)
 * ---- --x- ---- ---- ---- ---- ---- ----  sound input overrun detected (r/w)
 *
 * ---- ---- x--- ---- ---- ---- ---- ----  keyboard interrupt (r)
 * ---- ---- -x-- ---- ---- ---- ---- ----  keyboard data received (r)
 * ---- ---- --x- ---- ---- ---- ---- ----  keyboard data overrun detected (r/w)
 * ---- ---- ---x ---- ---- ---- ---- ----  non-maskable interrupt received (tilde and left or right cmd key) (r/w)
 * ---- ---- ---- x--- ---- ---- ---- ----  kms interrupt (r)
 * ---- ---- ---- -x-- ---- ---- ---- ----  kms data received (r)
 * ---- ---- ---- --x- ---- ---- ---- ----  kms data overrun detected (r/w)
 *
 * ---- ---- ---- ---- x--- ---- ---- ----  dma sound out transmit pending (r)
 * ---- ---- ---- ---- -x-- ---- ---- ----  dma sound out transmit in progress (r)
 * ---- ---- ---- ---- --x- ---- ---- ----  cpu data transmit pending (r)
 * ---- ---- ---- ---- ---x ---- ---- ----  cpu data transmit in progress (r)
 * ---- ---- ---- ---- ---- x--- ---- ----  receive operation pending (r)
 * ---- ---- ---- ---- ---- -x-- ---- ----  receive operation in progress (r)
 * ---- ---- ---- ---- ---- --x- ---- ----  kms enable (return from reset state) (r/w)
 * ---- ---- ---- ---- ---- ---x ---- ----  loop back transmitter data (r/w)
 *
 * ---- ---- ---- ---- ---- ---- xxxx xxxx  command to apply on kms data (r/w)
 *
 * ---x ---x ---- ---x ---- ---- ---- ----  zero bits
 */

/* Sound byte */
#define SNDOUT_DMA_ENABLE   0x80
#define SNDOUT_DMA_REQUEST  0x40
#define SNDOUT_DMA_UNDERRUN 0x20
#define SNDIN_DMA_ENABLE    0x08
#define SNDIN_DMA_REQUEST   0x04
#define SNDIN_DMA_OVERRUN   0x02

/* Keyboard/Mouse byte */
#define KM_INT              0x80
#define KM_RECEIVED         0x40
#define KM_OVERRUN          0x20
#define NMI_RECEIVED        0x10
#define KMS_INT             0x08
#define KMS_RECEIVED        0x04
#define KMS_OVERRUN         0x02

/* Transmitter byte */
#define TX_DMA_PENDING      0x80
#define TX_DMA              0x40
#define TX_CPU_PENDING      0x20
#define TX_CPU              0x10
#define TX_RX_PEND          0x08
#define TX_RX               0x04
#define KMS_ENABLE          0x02
#define TX_LOOP             0x01

/* Commands from KMS to CPU */
#define KMSCMD_CODEC_IN     0xC7    /* CODEC sound in */
#define KMSCMD_KM_RECV      0xC6    /* receive data from keyboard/mouse */
#define KMSCMD_SO_REQ       0x07    /* sound out request */
#define KMSCMD_SO_UNDR      0x0F    /* sound out underrun */

/* Commands from CPU to KMS */
#define KMSCMD_RESET        0xFF
#define KMSCMD_ASNDOUT      0xC7    /* analog sound out */
#define KMSCMD_KMPOLL       0xC6    /* set keyboard or mouse register */
#define KMSCMD_KMREG        0xC5    /* access keyboard or mouse register */
#define KMSCMD_CTRLOUT      0xC4    /* access volume control logic */
#define KMSCMD_VOLCTRL      0xC2    /* simplified access to volume control */

#define KMSCMD_SND_IN       0x03    /* sound in */
#define KMSCMD_SND_OUT      0x07    /* sound out */
#define KMSCMD_SIO_MASK     0xC7    /* mask for sound in/out */

#define KMSCMD_SIO_ENABLE   0x08    /* 1=enable, 0=disable sound */
#define KMSCMD_SIO_DBL_SMPL 0x10    /* 1=double sample, 0=normal */
#define KMSCMD_SIO_ZERO     0x20    /* double sample by 1=zero filling, 0=repetition */

static void kms_sndout_underrun(void) {
    kms.status.sound |= SNDOUT_DMA_UNDERRUN;
    set_interrupt(INT_SOUND_OVRUN, SET_INT);
}

static void kms_sndin_overrun(void) {
    kms.status.sound |= SNDIN_DMA_OVERRUN;
    set_interrupt(INT_SOUND_OVRUN, SET_INT);
}


/* KMS keyboard and mouse data register (0x0200E008) *
 *
 * x--- ---- ---- ---- ---- ---- ---- ----  always 0
 * -x-- ---- ---- ---- ---- ---- ---- ----  1 = no response error, 0 = normal event
 * --x- ---- ---- ---- ---- ---- ---- ----  1 = user poll, 0 = internal poll
 * ---x ---- ---- ---- ---- ---- ---- ----  1 = invalid/master, 0 = valid/slave (user/internal)
 * ---- xxxx ---- ---- ---- ---- ---- ----  device address (lowest bit 1 = mouse, 0 = keyboard)
 * ---- ---- xxxx xxxx ---- ---- ---- ----  chip revision: 0 = old, 1 = new, 2 = digital
 *
 * Mouse data:
 * ---- ---- ---- ---- xxxx xxx- ---- ----  mouse y
 * ---- ---- ---- ---- ---- ---x ---- ----  right button up (1) or down (0)
 * ---- ---- ---- ---- ---- ---- xxxx xxx-  mouse x
 * ---- ---- ---- ---- ---- ---- ---- ---x  left button up (1) or down (0)
 *
 * Keyboard data:
 * ---- ---- ---- ---- x--- ---- ---- ----  valid (1) or invalid (0)
 * ---- ---- ---- ---- -x-- ---- ---- ----  right alt
 * ---- ---- ---- ---- --x- ---- ---- ----  left alt
 * ---- ---- ---- ---- ---x ---- ---- ----  right command
 * ---- ---- ---- ---- ---- x--- ---- ----  left command
 * ---- ---- ---- ---- ---- -x-- ---- ----  right shift
 * ---- ---- ---- ---- ---- --x- ---- ----  left shift
 * ---- ---- ---- ---- ---- ---x ---- ----  control
 * ---- ---- ---- ---- ---- ---- x--- ----  key up (1) or down (0)
 * ---- ---- ---- ---- ---- ---- -xxx xxxx  keycode
 */

/* Address byte */
#define KM_NO_RESPONSE  0x40
#define KM_USER_POLL    0x20
#define KM_INVALID      0x10
#define KM_MASTER       0x10
#define KM_ADDR_MASK    0x0F
#define KM_ADDR         0x0E
#define KM_MOUSE        0x01

/* Revision byte */
#define REV_OLD         0x00
#define REV_NEW         0x01
#define REV_DIGITAL     0x02

/* Mouse data */
#define MOUSE_Y         0xFE00
#define MOUSE_RIGHT_UP  0x0100
#define MOUSE_X         0x00FE
#define MOUSE_LEFT_UP   0x0001

/* Keyboard data */
#define KBD_KEY_VALID   0x8000
#define KBD_MOD_MASK    0x7F00
#define KBD_KEY_UP      0x0080
#define KBD_KEY_MASK    0x007F

/* Magic key combinations */
#define KMS_MAGIC_MASK      0x7000FFFF /* must be normal internal event from master device */
#define KMS_MAGIC_RESET     0x1000A825 /* asterisk and left alt and left command key */
#define KMS_MAGIC_NMI_L     0x10008826 /* backquote and left command key */
#define KMS_MAGIC_NMI_R     0x10009026 /* backquote and right command key */
#define KMS_MAGIC_NMI_LR    0x10009826 /* backquote and both command keys */

static void kms_km_receive(uint32_t data) {
    /* Compare to magic key combinations */
    switch (data&KMS_MAGIC_MASK) {
        case KMS_MAGIC_RESET:
            Log_Printf(LOG_WARN, "Keyboard initiated CPU reset!");
            Reset_Warm();
            return;
        case KMS_MAGIC_NMI_L:
        case KMS_MAGIC_NMI_R:
        case KMS_MAGIC_NMI_LR:
            Log_Printf(LOG_WARN, "Keyboard initiated NMI!");
            set_interrupt(INT_NMI, SET_INT);
            return;
        default:
            break;
    }
    
    /* Normal keyboard or mouse data */
    kms.kmdata = data;
    
    if (kms.status.km&KM_RECEIVED) {
        kms.status.km |= KM_OVERRUN;
    }
    kms.status.km |= (KM_RECEIVED|KM_INT);
    set_interrupt(INT_KEYMOUSE, SET_INT);
}

static bool kms_codec_dma_blockend;

static void kms_codec_receive(uint32_t data) {
    kms.status.sound |= SNDIN_DMA_REQUEST;
    
    kms_codec_dma_blockend = dma_sndin_write_memory(data);
    if (kms_codec_dma_blockend) {
        if (dma_sndin_intr()) {
            kms_sndin_overrun();
        }
    }
}

static void kms_sndout_request(void) {
    kms.status.sound |= SNDOUT_DMA_REQUEST;
    
    if (snd_buffer_len) {
        dma_sndout_intr();
    }
    dma_sndout_read_memory();
}

static void kms_command_out(uint8_t command, uint32_t data) {
    if (!(kms.status.transmit&KMS_ENABLE)) {
        return;
    }

    switch (command) {
        case KMSCMD_CODEC_IN:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] CODEC sound in");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            kms_codec_receive(data);
            break;
        case KMSCMD_KM_RECV:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Receive keyboard/mouse data");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            kms_km_receive(data);
            break;
        case KMSCMD_SO_REQ:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out request");
            kms.status.cmd = command;
            kms_sndout_request();
            break;
        case KMSCMD_SO_UNDR:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out underrun");
            kms.status.cmd = command;
            kms_sndout_underrun();
            break;
            
        default:
            Log_Printf(LOG_WARN, "[KMS] Unknown command out (%02X)",command);
            Log_Printf(LOG_WARN, "[KMS] Data = %08X",data);
            break;
    }
}

bool kms_send_codec_receive(uint32_t data) {
    kms_command_out(KMSCMD_CODEC_IN, data);
    return kms_codec_dma_blockend;
}

bool kms_can_receive_codec(void) {
    if (dma_sndin_intr()) {
        kms_sndin_overrun();
        return false;
    }
    return true;
}

void kms_send_sndout_underrun(void) {
    kms_command_out(KMSCMD_SO_UNDR, 0);
}

void kms_send_sndout_request(void) {
    kms_command_out(KMSCMD_SO_REQ, 0);
}

/* Internal poll mask
 *
 * xxxx xxxx xxxx xxxx xxxx xxxx xxxx ----  polled devices (7 device addresses, 4 bit each)
 * ---- ---- ---- ---- ---- ---- ---- xxxx  poll speed
 */

static void km_set_addr(uint8_t addr) {
    if (kms.rev==REV_OLD) {
        kms.km_addr = addr&KM_ADDR;
    } else {
        kms.km_addr = 0;
    }
}

static void km_internal_poll(uint8_t addr) {
    int i,mask,device;
    
    device = addr&KM_ADDR_MASK;
    
    if (device==0xF) {
        Log_Printf(LOG_KMS_LEVEL, "[KMS] Invalid device address (%02X)",device);
        return;
    }
    for (i=28; i>4; i-=4) {
        mask=(kms.km_mask>>i)&KM_ADDR_MASK;
        if (mask==device) {
            kms_command_out(KMSCMD_KM_RECV, kms.km_data[device] | (addr<<24));
            return;
        }
    }
    Log_Printf(LOG_KMS_LEVEL, "[KMS] Device %i disabled (mask: %08X)",device,kms.km_mask);
}

static void km_user_poll(uint8_t addr) {
    uint32_t data = 0;
    
    data = kms.km_data[addr&KM_ADDR_MASK];
    
    addr &= ~KM_ADDR_MASK;
    addr |= KM_USER_POLL;
    
    kms_command_out(KMSCMD_KM_RECV, data | (addr<<24));
}


/* Keyboard/Mouse commands */
#define KM_CMD_MASK     0xE0
#define KM_CMD_READ     0x10
#define KM_CMD_RESET    0x0F
#define KM_CMD_SET_ADDR 0xEF

/* Keyboard/Mouse versions */
#define KM_KBD_REV    1
#define KM_KBD_ID     1
#define KM_MOUSE_REV  1
#define KM_MOUSE_ID   2

static void km_no_response(void) {
    uint8_t addr = 0;
    
    addr |= KM_USER_POLL;
    addr |= (KM_NO_RESPONSE|KM_INVALID); /* checked on real hardware */
    
    kms_command_out(KMSCMD_KM_RECV, addr<<24);
}

static void km_version(uint8_t addr) {
    uint32_t data = 0;
    
    data |= kms.rev << 16;
    data |= ((addr&KM_MOUSE)?KM_MOUSE_REV:KM_KBD_REV)<<8;
    data |= ((addr&KM_MOUSE)?KM_MOUSE_ID:KM_KBD_ID);
    
    addr &= ~KM_ADDR_MASK;
    addr |= KM_USER_POLL;
    
    kms_command_out(KMSCMD_KM_RECV, data | (addr<<24));
}


/* Mouse button states */
static bool m_button_right;
static bool m_button_left;

void kms_keydown(uint8_t modkeys, uint8_t keycode) {
    uint8_t  addr = kms.km_addr|KM_MASTER;
    uint16_t data = 0;

    if (keycode == NEXTKEY_POWER) { /* Power key */
        rtc_request_power_down();
        return;
    }
    
    data |= (modkeys<<8)|keycode|KBD_KEY_VALID;
    
    kms.km_data[addr&KM_ADDR_MASK] = data;

    km_internal_poll(addr);
}

void kms_keyup(uint8_t modkeys, uint8_t keycode) {
    uint8_t  addr = kms.km_addr|KM_MASTER;
    uint16_t data = 0;

    if (keycode == NEXTKEY_POWER) {
        rtc_stop_pdown_request();
        return;
    }
    
    data |= (modkeys<<8)|keycode|KBD_KEY_VALID|KBD_KEY_UP;
    
    kms.km_data[addr&KM_ADDR_MASK] = data;
    
    km_internal_poll(addr);
}

void kms_mouse_button(bool left, bool down) {
    uint8_t  addr = kms.km_addr|KM_MOUSE;
    uint16_t data = 0;

    if (left) {
        m_button_left = down;
    } else {
        m_button_right = down;
    }
    
    data |= m_button_left?0:MOUSE_LEFT_UP;
    data |= m_button_right?0:MOUSE_RIGHT_UP;
    
    kms.km_data[addr&KM_ADDR_MASK] = data;

    km_internal_poll(addr);
}

void kms_mouse_move(int x, int y) {
    bool move_right, move_down;
    uint8_t  addr = kms.km_addr|KM_MOUSE;
    uint16_t data = 0;
    
    if (x < 0) {
        x = -x;
        move_right = false;
    } else {
        move_right = true;
    }
    if (y < 0) {
        y = -y;
        move_down = false;
    } else {
        move_down = true;
    }
    
    if (x > 0x3F) {
        x = 0x3F;
    }
    if (y > 0x3F) {
        y = 0x3F;
    }
    
    if (move_right && x > 0) {
        x = (0x40 - x) | 0x40;
    }
    if (move_down && y > 0) {
        y = (0x40 - y) | 0x40;
    }
    
    data |= (x<<1)&MOUSE_X;
    data |= (y<<9)&MOUSE_Y;
    
    data |= m_button_left?0:MOUSE_LEFT_UP;
    data |= m_button_right?0:MOUSE_RIGHT_UP;
    
    kms.km_data[addr&KM_ADDR_MASK] = data;
    
    km_internal_poll(addr);
}

static void km_reset(void) {
    int i;
    
    kms.km_addr = 0;
    kms.km_mask = 0;
    for (i=0; i<16; i++) {
        kms.km_data[i] = 0;
    }
}

static void km_access(uint32_t data) {
    uint8_t command = (data>>24)&0xFF;
    uint8_t cmddata = (data>>16)&0xFF;
    
    uint8_t device_cmd  = (command&KM_CMD_MASK)>>5;
    uint8_t device_addr = (command&KM_ADDR_MASK);
    bool    device_kbd  = (command&KM_MOUSE) ? false : true;
    
    if (command==KM_CMD_RESET) {
        Log_Printf(LOG_KMS_LEVEL, "[KMS] Keyboard/Mouse: Reset");
        km_reset();
    } else if (command==KM_CMD_SET_ADDR) {
        Log_Printf(LOG_KMS_LEVEL, "[KMS] Keyboard/Mouse: Set address to %d",(cmddata&KM_ADDR)>>1);
        km_set_addr(cmddata);
    } else if (command&KM_CMD_READ) {
        switch (device_cmd) {
            case 0:
                Log_Printf(LOG_KMS_LEVEL, "[KMS] %s %d: Poll",device_kbd?"Keyboard":"Mouse",device_addr>>1);
                km_user_poll(device_addr);
                break;
            case 7:
                Log_Printf(LOG_KMS_LEVEL, "[KMS] %s %d: Read version",device_kbd?"Keyboard":"Mouse",device_addr>>1);
                km_version(device_addr);
                return;
            default:
                Log_Printf(LOG_WARN, "[KMS] %s %d: Unknown read command (%d)",device_kbd?"Keyboard":"Mouse",device_addr>>1,device_cmd);
                break;
        }
    } else { /* device write */
        switch (device_cmd) {
            case 0:
                if (device_kbd) {
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Keyboard %d: Turn %s left LED",device_addr>>1,(cmddata&1)?"on":"off");
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Keyboard %d: Turn %s right LED",device_addr>>1,(cmddata&2)?"on":"off");
                    break;
                }
            default:
                Log_Printf(LOG_WARN, "[KMS] %s %d: Unknown write command (%d)",device_kbd?"Keyboard":"Mouse",device_addr>>1,device_cmd);
                break;
        }
    }
    km_no_response();
}

static void kms_reset(void) {
    Log_Printf(LOG_WARN, "[KMS] Reset");

    kms.status.sound    = 0;
    kms.status.km       = 0;
    kms.status.transmit = 0;
    kms.status.cmd      = 0;
    kms.command         = 0;
    kms.data            = 0;
    kms.kmdata          = 0;
    
    snd_stop_output();
    snd_stop_input();
    km_reset();
    set_interrupt(INT_KEYMOUSE|INT_SOUND_OVRUN|INT_MONITOR|INT_NMI, RELEASE_INT);
}

static void kms_command_in(uint8_t command, uint32_t data) {
    if (!(kms.status.transmit&KMS_ENABLE)) {
        return;
    }
    
    switch (command) {
        case KMSCMD_RESET:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Reset");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            if (data==0xFFFFFFFF) {
                kms_reset();
            }
            break;
        case KMSCMD_ASNDOUT:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Analog sound out");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            snd_send_sample(data);
            break;
        case KMSCMD_KMPOLL:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Set keyboard/mouse poll");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            kms.km_mask=data;
            break;
        case KMSCMD_KMREG:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Access keyboard/mouse");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            km_access(data);
            break;
        case KMSCMD_CTRLOUT:
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Access volume control logic");
            Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
            snd_gpo_access(data>>24);
            break;
        case KMSCMD_VOLCTRL:
            if (kms.rev>REV_OLD) {
                Log_Printf(LOG_KMS_LEVEL, "[KMS] Set volume");
                Log_Printf(LOG_KMS_LEVEL, "[KMS] Data = %08X",data);
                snd_vol_access(data>>24);
                break;
            }
        default: /* commands without data */
            if ((command&KMSCMD_SIO_MASK)==KMSCMD_SND_OUT) {
                Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out command:");
                
                if (command&KMSCMD_SIO_ENABLE) {
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out enable.");
                    if (command&KMSCMD_SIO_DBL_SMPL) {
                        Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out double sample.");
                        if (command&KMSCMD_SIO_ZERO) {
                            Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out double sample by zero filling.");
                        } else {
                            Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out double sample by repetition.");
                        }
                    } else {
                        Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out normal sample.");
                    }
                    snd_start_output(command&(KMSCMD_SIO_DBL_SMPL|KMSCMD_SIO_ZERO));
                } else {
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound out disable.");
                    kms.status.sound &= ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
                    snd_stop_output();
                }
            } else if ((command&KMSCMD_SIO_MASK)==KMSCMD_SND_IN) {
                Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound in command");
                
                if (command&KMSCMD_SIO_ENABLE) {
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound in enable.");
                    snd_start_input();
                } else {
                    Log_Printf(LOG_KMS_LEVEL, "[KMS] Sound in disable.");
                    kms.status.sound &= ~(SNDIN_DMA_OVERRUN|SNDIN_DMA_REQUEST);
                    snd_stop_input();
                }
            } else {
                Log_Printf(LOG_WARN, "[KMS] Unknown command (%02X)",command);
                Log_Printf(LOG_WARN, "[KMS] Data = %08X",data);
            }
            break;
    }
}


/* KMS Interface */
void KMS_Stat_Snd_Read(void) { /* 0x0200e000 */
    IoMem_WriteByte(IoAccessCurrentAddress, kms.status.sound);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Sound status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void KMS_Ctrl_Snd_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Sound control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    kms.status.sound &= ~(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE);
    kms.status.sound |= (val&(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE));
    
    if (val&SNDOUT_DMA_UNDERRUN && (!snd_output_active())) {
        kms.status.sound &= ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
        set_interrupt(INT_SOUND_OVRUN, RELEASE_INT);
    }
    if (val&SNDIN_DMA_OVERRUN && (!snd_input_active())) {
        kms.status.sound &= ~(SNDIN_DMA_OVERRUN|SNDIN_DMA_REQUEST);
        set_interrupt(INT_SOUND_OVRUN, RELEASE_INT);
    }
}

void KMS_Stat_KM_Read(void) { /* 0x0200e001 */
    IoMem_WriteByte(IoAccessCurrentAddress, kms.status.km);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Keyboard/Mouse status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void KMS_Ctrl_KM_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Keyboard/Mouse control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());

    if (val&KM_OVERRUN) {
        kms.status.km &= ~(KM_RECEIVED|KM_OVERRUN|KM_INT);
        set_interrupt(INT_KEYMOUSE, RELEASE_INT);
    }
    if (val&NMI_RECEIVED) {
        kms.status.km &= ~NMI_RECEIVED;
        set_interrupt(INT_NMI, RELEASE_INT);
    }
    if (val&KMS_OVERRUN) {
        kms.status.km &= ~(KMS_RECEIVED|KMS_OVERRUN|KMS_INT);
        set_interrupt(INT_MONITOR, RELEASE_INT);
    }
}

void KMS_Stat_TX_Read(void) { /* 0x0200e002 */
    IoMem_WriteByte(IoAccessCurrentAddress, kms.status.transmit);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Tansmitter status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void KMS_Ctrl_TX_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Tansmitter control write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    if ((kms.status.transmit&KMS_ENABLE) && !(val&KMS_ENABLE)) {
        kms_reset();
    }
    kms.status.transmit &= ~(KMS_ENABLE|TX_LOOP);
    kms.status.transmit |= (val&(KMS_ENABLE|TX_LOOP));
}

void KMS_Stat_Cmd_Read(void) { /* 0x0200e003 */
    IoMem_WriteByte(IoAccessCurrentAddress, kms.status.cmd);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Command read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void KMS_Ctrl_Cmd_Write(void) {
    kms.command = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Command write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void KMS_Data_Read(void) { /* 0x0200e004 */
    IoMem_WriteLong(IoAccessCurrentAddress, kms.data);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Data read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, kms.data, m68k_getpc());
}

void KMS_Data_Write(void) {
    uint32_t val = IoMem_ReadLong(IoAccessCurrentAddress);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Data write at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, val, m68k_getpc());
    if (kms.status.transmit&TX_LOOP) {
        kms_command_out(kms.command, val);
    } else {
        kms_command_in(kms.command, val);
    }
}

void KMS_KM_Data_Read(void) { /* 0x0200e008 */
    IoMem_WriteLong(IoAccessCurrentAddress, kms.kmdata);
    Log_Printf(LOG_KMS_REG_LEVEL,"[KMS] Keyboard/Mouse data read at $%08x val=$%08x PC=$%08x\n", IoAccessCurrentAddress, kms.kmdata, m68k_getpc());

    kms.status.km &= ~(KM_RECEIVED|KM_INT);
    set_interrupt(INT_KEYMOUSE, RELEASE_INT);
}

/* Reset */
void KMS_Reset(void) {
    kms.rev = ConfigureParams.System.bTurbo?REV_NEW:REV_OLD;
    kms_reset();
}
