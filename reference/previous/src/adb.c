/*
  Previous - adb.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the Apple Desktop Bus.
*/
const char Adb_fileid[] = "Previous adb.c";

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "sysReg.h"
#include "rtcnvram.h"
#include "adb.h"

#define LOG_ADB_LEVEL      LOG_DEBUG
#define LOG_ADB_CMD_LEVEL  LOG_DEBUG


/* ADB devices */
#define ADB_ADDR_INVAL   0
#define ADB_ADDR_KBD     2
#define ADB_ADDR_MOUSE   3
#define ADB_ADDR_TABLET  4

#define ADB_TYPE_MOUSE   1
#define ADB_TYPE_KBD     2
#define ADB_TYPE_KBD_ISO 5

#define ADB_CONF_EXC  0x40
#define ADB_CONF_REQ  0x20
#define ADB_ADDR_MASK 0x0F

#define ADB_KBD_EVENTS  16

/* Keyboard */
static struct {
	uint8_t addr;
	uint8_t conf;
	uint8_t id;
	uint8_t event[ADB_KBD_EVENTS];
	int write;
	int read;
	int next;
} adb_kbd;

static void adb_kbd_flush(void) {
	adb_kbd.read = adb_kbd.write = 0;
}

static void adb_kbd_reset(void) {
	adb_kbd.addr = ADB_ADDR_KBD;
	adb_kbd.conf = ADB_CONF_EXC | ADB_CONF_REQ;
	adb_kbd.id   = ADB_TYPE_KBD;
	adb_kbd_flush();
}

static bool adb_kbd_request(void) {
	return (adb_kbd.write != adb_kbd.read);
}

static void adb_kbd_put(uint8_t event) {
	adb_kbd.next = adb_kbd.write + 1;
	if (adb_kbd.next >= ADB_KBD_EVENTS) {
		adb_kbd.next = 0;
	}
	if (adb_kbd.next == adb_kbd.read) {
		Log_Printf(LOG_WARN, "[ADB] Keyboard events queue overflow");
	} else {
		adb_kbd.event[adb_kbd.write] = event;
		adb_kbd.write = adb_kbd.next;
	}
}

static bool adb_kbd_get(uint8_t* event) {
	if (adb_kbd.write != adb_kbd.read) {
		adb_kbd.next = adb_kbd.read + 1;
		if (adb_kbd.next >= ADB_KBD_EVENTS) {
			adb_kbd.next = 0;
		}
		*event = adb_kbd.event[adb_kbd.read];
		adb_kbd.read = adb_kbd.next;
		return true;
	}
	*event = 0xff;
	return false;
}

void adb_keyup(uint8_t key) {
	if (key == APPLEKEY_POWER) {
		rtc_stop_pdown_request();
	} else if (key < 0x7f) {
		adb_kbd_put(0x80 | key);
	}
}

void adb_keydown(uint8_t key) {
	if (key == APPLEKEY_POWER) {
		rtc_request_power_down();
	} else if (key < 0x7f) {
		adb_kbd_put(key);
	}
}

/* Mouse */
static struct {
	uint8_t addr;
	uint8_t conf;
	uint8_t id;
	bool event;
	bool right;
	bool left;
	int x;
	int y;
} adb_mouse;

static void adb_mouse_flush(void) {
	adb_mouse.event = false;
}

static void adb_mouse_reset(void) {
	adb_mouse.addr = ADB_ADDR_MOUSE;
	adb_mouse.conf = ADB_CONF_EXC | ADB_CONF_REQ | 0x10;
	adb_mouse.id   = ADB_TYPE_MOUSE;
	adb_mouse_flush();
}

static bool adb_mouse_request(void) {
	return adb_mouse.event;
}

static bool adb_mouse_get(uint8_t* event) {
	if (adb_mouse.event) {
		if (adb_mouse.y < 0) {
			event[0] = (0x40 + adb_mouse.y) | 0x40;
		} else {
			event[0] = adb_mouse.y;
		}
		if (adb_mouse.x < 0) {
			event[1] = (0x40 + adb_mouse.x) | 0x40;
		} else {
			event[1] = adb_mouse.x;
		}
		if (adb_mouse.left == 0) {
			event[0] |= 0x80;
		}
		if (adb_mouse.right == 0) {
			event[1] |= 0x80;
		}
		
		adb_mouse.x = adb_mouse.y = 0;
		adb_mouse.event = false;
		
		return true;
	}
	return false;
}

void adb_mouse_move(int x, int y) {
	if (x == 0 && y == 0)
		return;
	
	if      (x < -63) x = -63;
	else if (x >  63) x =  63;
	if      (y < -63) y = -63;
	else if (y >  63) y =  63;
	
	adb_mouse.x = x;
	adb_mouse.y = y;
	adb_mouse.event = true;
}

void adb_mouse_button(bool left, bool down) {
	if (left) {
		adb_mouse.left  = down;
	} else {
		adb_mouse.right = down;
	}
	adb_mouse.event = true;
}


/* ADB registers */
static struct {
	uint32_t intstatus;
	uint32_t intmask;
	uint32_t config;
	uint32_t status;
	uint32_t command;
	uint32_t bitcount;
	uint32_t data0;
	uint32_t data1;
} adb;

#define ADB_INTSTATUS   0x00 /* rw */
#define ADB_INTMASK     0x08 /* rw */
#define ADB_SETINT      0x10 /* w */
#define ADB_CONFIG      0x18 /* rw */
#define ADB_CTRL        0x20 /* w */
#define ADB_STATUS      0x28 /* r */
#define ADB_CMD         0x30 /* rw */
#define ADB_COUNT       0x38 /* rw */

#define ADB_DATA0       0x80 /* rw */
#define ADB_DATA1       0x88 /* rw */


/* ADB interrupt registers */
#define ADB_INT_REJECT      0x01
#define ADB_INT_POLLSTOP    0x02
#define ADB_INT_ACCESS      0x04
#define ADB_INT_RESET       0x08

/* ADB configuration register */
#define ADB_CONF_SYSTEM     0x01
#define ADB_CONF_WATCHDOG   0x02

/* ADB control register */
#define ADB_CTRL_EN_POLL    0x01
#define ADB_CTRL_DIS_POLL   0x02
#define ADB_CTRL_XMIT_CMD   0x04
#define ADB_CTRL_RESET_ADB  0x08
#define ADB_CTRL_RESET_WD   0x10

/* ADB status register */
#define ADB_STAT_CONFLICT   0x01
#define ADB_STAT_REQUEST    0x02
#define ADB_STAT_TIMEOUT    0x04
#define ADB_STAT_DATAPEND   0x08
#define ADB_STAT_RESET      0x10
#define ADB_STAT_ACCESS     0x20
#define ADB_STAT_POLL_EN    0x40
#define ADB_STAT_POLL_OV    0x80

/* ADB command register */
#define ADB_CMD_REG_MASK    0x03
#define ADB_CMD_CMD_MASK    0x0C
#define ADB_CMD_ADDR_MASK   0xF0

/* ADB bit count register */
#define ADB_CNT_MASK        0x7F

static void adb_check_interrupt(void) {
	if (adb.intstatus&adb.intmask) {
		set_interrupt(INT_DISK, SET_INT);
	} else {
		set_interrupt(INT_DISK, RELEASE_INT);
	}
}

static void adb_interrupt(uint32_t intr) {
	if (adb.status&ADB_STAT_POLL_EN) {
		intr &= ~(ADB_INT_ACCESS);
	}
	adb.intstatus |= intr;
	adb_check_interrupt();
}

static void adb_timeout(void) {
	adb.status |= ADB_STAT_TIMEOUT;
	adb_interrupt(ADB_INT_ACCESS);
}

static uint32_t adb_read_data(uint8_t* data) {
	data[0] = adb.data0 >> 24;
	data[1] = adb.data0 >> 16;
	data[2] = adb.data0 >> 8;
	data[3] = adb.data0;
	data[4] = adb.data1 >> 24;
	data[5] = adb.data1 >> 16;
	data[6] = adb.data1 >> 8;
	data[7] = adb.data1;
	
	adb_interrupt(ADB_INT_ACCESS);

	return adb.bitcount >> 3;
}

static void adb_write_data(uint8_t* data, uint32_t len) {
	if (len > 8) {
		Log_Printf(LOG_WARN, "[ADB] Data: Lengh of data is too big (%d)", len);
		len = 8;
	}
	adb.data0 = ((uint32_t)data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	adb.data1 = ((uint32_t)data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
	
	adb.bitcount = len << 3;
	adb.status |= ADB_STAT_DATAPEND;
	adb_interrupt(ADB_INT_ACCESS);
}


/* ADB commands */
#define ADB_CMD_RESET   0
#define ADB_CMD_FLUSH   1
#define ADB_CMD_LISTEN  2
#define ADB_CMD_TALK    3

static void adb_command(void) {
	uint8_t buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	
	uint8_t reg  = (adb.command & ADB_CMD_REG_MASK);
	uint8_t cmd  = (adb.command & ADB_CMD_CMD_MASK)  >> 2;
	uint8_t addr = (adb.command & ADB_CMD_ADDR_MASK) >> 4;
	
	if (!ConfigureParams.System.bADB) {
		Log_Printf(LOG_WARN, "[ADB] Command: No device present");
		adb_timeout();
		return;
	}
	
	Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Command: %d, address: %d, register: %d", cmd, addr, reg);
	
	switch (cmd) {
		case ADB_CMD_LISTEN:
			Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Command: Listen");
			if (addr == adb_kbd.addr) {
				adb_read_data(buf);
				switch (reg) {
					case 3:
						Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Keyboard: Register 3 write (%02x%02x)", buf[0], buf[1]);
						if (buf[1] == 0) {
							adb_kbd.addr = buf[0] & ADB_ADDR_MASK;
							adb_kbd.conf = (adb_kbd.conf & ~ADB_CONF_REQ) | (buf[0] & ADB_CONF_REQ);
							Log_Printf(LOG_WARN, "[ADB] Keyboard: Set address to %d", adb_kbd.addr);
							Log_Printf(LOG_WARN, "[ADB] Keyboard: %sable request", (adb_kbd.conf&ADB_CONF_REQ)?"En":"Dis");
						} else if (buf[1] == 0xfd) {
							Log_Printf(LOG_WARN, "[ADB] Keyboard: Set address if activated (ignored)");
						} else if (buf[1] == 0xfe) {
							adb_kbd.addr = buf[0] & ADB_ADDR_MASK;
							Log_Printf(LOG_WARN, "[ADB] Keyboard: Set address to %d", adb_kbd.addr);
						} else if (buf[1] == 0xff) {
							Log_Printf(LOG_WARN, "[ADB] Keyboard: Start self test");
						} else {
							Log_Printf(LOG_WARN, "[ADB] Keyboard: Set handler ID to %x (ignored)", buf[1]);
						}
						break;
					default:
						Log_Printf(LOG_WARN, "[ADB] Keyboard: Unknown register write (%d)", reg);
						break;
				}
			} else if (addr == adb_mouse.addr) {
				adb_read_data(buf);
				switch (reg) {
					case 3:
						Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Mouse: Register 3 write (%02x%02x)", buf[0], buf[1]);
						if (buf[1] == 0) {
							adb_mouse.addr = buf[0] & ADB_ADDR_MASK;
							adb_mouse.conf = (adb_mouse.conf & ~ADB_CONF_REQ) | (buf[0] & ADB_CONF_REQ);
							Log_Printf(LOG_WARN, "[ADB] Mouse: Set address to %d", adb_mouse.addr);
							Log_Printf(LOG_WARN, "[ADB] Mouse: %sable request", (adb_mouse.conf&ADB_CONF_REQ)?"En":"Dis");
						} else if (buf[1] == 0xfd) {
							Log_Printf(LOG_WARN, "[ADB] Mouse: Set address if activated (ignored)");
						} else if (buf[1] == 0xfe) {
							adb_mouse.addr = buf[0] & ADB_ADDR_MASK;
							Log_Printf(LOG_WARN, "[ADB] Mouse: Set address to %d", adb_mouse.addr);
						} else if (buf[1] == 0xff) {
							Log_Printf(LOG_WARN, "[ADB] Mouse: Start self test");
						} else {
							Log_Printf(LOG_WARN, "[ADB] Mouse: Set handler ID to %x (ignored)", buf[1]);
						}
						break;
					default:
						Log_Printf(LOG_WARN, "[ADB] Mouse: Unknown register write (%d)", reg);
						break;
				}
			} else {
				Log_Printf(LOG_WARN, "[ADB] Listen: Unknown device (%d)", addr);
				adb_timeout();
			}
			break;
			
		case ADB_CMD_TALK:
			Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Command: Talk");
			if (addr == adb_kbd.addr) {
				switch (reg) {
					case 0:
						if (adb_kbd_get(&buf[0])) {
							adb_kbd_get(&buf[1]);
							adb_write_data(buf, 2);
						} else {
							adb_timeout();
						}
						break;
					case 3:
						buf[0] = adb_kbd.conf | (rand() & ADB_ADDR_MASK);
						buf[1] = adb_kbd.id;
						adb_write_data(buf, 2);
						break;
					default:
						Log_Printf(LOG_WARN, "[ADB] Keyboard: Unknown register read (%d)", reg);
						adb_timeout();
						break;
				}
			} else if (addr == adb_mouse.addr) {
				switch (reg) {
					case 0:
						if (adb_mouse_get(buf)) {
							adb_write_data(buf, 2);
						} else {
							adb_timeout();
						}
						break;
					case 3:
						buf[0] = adb_mouse.conf | (rand() & ADB_ADDR_MASK);
						buf[1] = adb_mouse.id;
						adb_write_data(buf, 2);
						break;
					default:
						Log_Printf(LOG_WARN, "[ADB] Mouse: Unknown register read (%d)", reg);
						adb_timeout();
						break;
				}
			} else {
				Log_Printf(LOG_WARN, "[ADB] Talk: Unknown device (%d)", addr);
				adb_timeout();
			}
			break;
			
		case 1: /* reserved */
			Log_Printf(LOG_WARN, "[ADB] Command: Invalid");
			break;
			
		default:
			cmd = reg;
			switch (cmd) {
				case ADB_CMD_RESET:
					Log_Printf(LOG_WARN, "[ADB] Command: Reset");
					adb_kbd_reset();
					adb_mouse_reset();
					adb_timeout();
					break;
					
				case ADB_CMD_FLUSH:
					Log_Printf(LOG_ADB_CMD_LEVEL, "[ADB] Command: Flush");
					if (addr == adb_kbd.addr) {
						Log_Printf(LOG_WARN, "[ADB] Keyboard: Flush");
						adb_kbd_flush();
					} else if (addr == adb_mouse.addr) {
						Log_Printf(LOG_WARN, "[ADB] Mouse: Flush");
						adb_mouse_flush();
					} else {
						Log_Printf(LOG_WARN, "[ADB] Flush: Unknown device (%d)", addr);
					}
					adb_timeout();
					break;
					
				default:
					Log_Printf(LOG_WARN, "[ADB] Command: Invalid");
					adb_timeout();
					break;
			}
			break;
	}
	
	if (adb_kbd_request() || adb_mouse_request()) {
		adb.status |= ADB_STAT_REQUEST;
		adb_interrupt(ADB_INT_ACCESS);
	}
}

static uint32_t adb_intstatus_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Interrupt status read at $%08X val=$%08X",addr,adb.intstatus);
	return adb.intstatus;
}

static void adb_intstatus_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Interrupt status write at $%08X val=$%08X",addr,val);
	adb.intstatus &= ~val;
	adb_check_interrupt();
}

static uint32_t adb_intmask_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Interrupt mask read at $%08X val=$%08X",addr,adb.intmask);
	return adb.intmask;
}

static void adb_intmask_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Interrupt mask write at $%08X val=$%08X",addr,val);
	adb.intmask = val;
	adb_check_interrupt();
}

static void adb_setint_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Set interrupt write at $%08X val=$%08X",addr,val);
	adb.intstatus |= val;
	adb_check_interrupt();
}

static uint32_t adb_config_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Configuration read at $%08X val=$%08X",addr,adb.config);
	return adb.config;
}

static void adb_config_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Configuration write at $%08X val=$%08X",addr,val);
	adb.config = val;
}

static void adb_control_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Control write at $%08X val=$%08X",addr,val);
	
	if (val&ADB_CTRL_RESET_ADB) {
		Log_Printf(LOG_WARN, "[ADB] Software reset");
		adb_kbd_reset();
		adb_mouse_reset();
		adb.status &= ~ADB_STAT_POLL_EN;
		adb_interrupt(ADB_INT_RESET);
	}
	if (val&ADB_CTRL_XMIT_CMD) {
		if (adb.status&(ADB_STAT_RESET|ADB_STAT_POLL_EN|ADB_STAT_ACCESS)) {
			adb_interrupt(ADB_INT_REJECT);
		} else {
			adb.status &= ~(ADB_STAT_DATAPEND|ADB_STAT_TIMEOUT|ADB_STAT_REQUEST);
			adb_command();
		}
	}
	if (val&ADB_CTRL_DIS_POLL) {
		adb.status &= ~ADB_STAT_POLL_EN;
	}
	if (val&ADB_CTRL_EN_POLL) {
		if ((adb.status&(ADB_STAT_RESET|ADB_STAT_ACCESS)) && !(adb.status&ADB_STAT_POLL_EN)) {
			adb_interrupt(ADB_INT_REJECT);
		} else {
			/* broken in real hardware      *
			adb.status |= ADB_STAT_POLL_EN; */
		}
	}
}

static uint32_t adb_status_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Status read at $%08X val=$%08X",addr,adb.status);
	return adb.status;
}

static uint32_t adb_command_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Command read at $%08X val=$%08X",addr,adb.command);
	return adb.command;
}

static void adb_command_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Command write at $%08X val=$%08X",addr,val);
	adb.command = val;
}

static uint32_t adb_bitcount_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Bitcount read at $%08X val=$%08X",addr,adb.bitcount);
	return adb.bitcount & ADB_CNT_MASK;
}

static void adb_bitcount_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Bitcount write at $%08X val=$%08X",addr,val);
	adb.bitcount = val & ADB_CNT_MASK;
}

static uint32_t adb_data0_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Data0 read at $%08X val=$%08X",addr,adb.data0);
	return adb.data0;
}

static void adb_data0_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Data0 write at $%08X val=$%08X",addr,val);
	adb.data0 = val;
}

static uint32_t adb_data1_read(uint32_t addr) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Data1 read at $%08X val=$%08X",addr,adb.data1);
	return adb.data1;
}

static void adb_data1_write(uint32_t addr, uint32_t val) {
	Log_Printf(LOG_ADB_LEVEL, "[ADB] Data1 write at $%08X val=$%08X",addr,val);
	adb.data1 = val;
}


static uint32_t adb_read_register(uint32_t addr, int size) {
	switch (addr&0xFF) {
		case ADB_INTSTATUS:
			return adb_intstatus_read(addr);
		case ADB_INTMASK:
			return adb_intmask_read(addr);
		case ADB_CONFIG:
			return adb_config_read(addr);
		case ADB_STATUS:
			return adb_status_read(addr);
		case ADB_CMD:
			return adb_command_read(addr);
		case ADB_COUNT:
			return adb_bitcount_read(addr);
		case ADB_DATA0:
			return adb_data0_read(addr);
		case ADB_DATA1:
			return adb_data1_read(addr);
			
		default:
			Log_Printf(LOG_WARN, "[ADB] Illegal read at $%08X",addr);
			M68000_BusError(addr, BUS_ERROR_READ, size, BUS_ERROR_ACCESS_DATA, 0);
			return 0;
	}
}

static void adb_write_register(uint32_t addr, uint32_t val, int size) {
	switch (addr&0xFF) {
		case ADB_INTSTATUS:
			adb_intstatus_write(addr, val);
			break;
		case ADB_INTMASK:
			adb_intmask_write(addr, val);
			break;
		case ADB_SETINT:
			adb_setint_write(addr, val);
			break;
		case ADB_CONFIG:
			adb_config_write(addr, val);
			break;
		case ADB_CTRL:
			adb_control_write(addr, val);
			break;
		case ADB_CMD:
			adb_command_write(addr, val);
			break;
		case ADB_COUNT:
			adb_bitcount_write(addr, val);
			break;
		case ADB_DATA0:
			adb_data0_write(addr, val);
			break;
		case ADB_DATA1:
			adb_data1_write(addr, val);
			break;
			
		default:
			Log_Printf(LOG_WARN, "[ADB] Illegal write at $%08X",addr);
			M68000_BusError(addr, BUS_ERROR_WRITE, size, BUS_ERROR_ACCESS_DATA, val);
			break;
	}
}



uint32_t adb_lget(uint32_t addr) {
	Log_Printf(LOG_DEBUG, "[ADB] lget at $%08X",addr);
	return adb_read_register(addr, BUS_ERROR_SIZE_LONG);
}

uint16_t adb_wget(uint32_t addr) {
	uint8_t shift;
	Log_Printf(LOG_WARN, "[ADB] wget at $%08X",addr);
	
	shift = (2-(addr&2))*8;
	addr &= ~3;
	return (adb_read_register(addr, BUS_ERROR_SIZE_WORD)>>shift)&0xFFFF;
}

uint8_t adb_bget(uint32_t addr) {
	uint8_t shift;
	Log_Printf(LOG_WARN, "[ADB] bget at $%08X",addr);
	
	shift = (3-(addr&3))*8;
	addr &= ~3;
	return (adb_read_register(addr, BUS_ERROR_SIZE_BYTE)>>shift)&0xFF;
}

void adb_lput(uint32_t addr, uint32_t l) {
	Log_Printf(LOG_DEBUG, "[ADB] lput at $%08X",addr);
	adb_write_register(addr, l, BUS_ERROR_SIZE_LONG);
}

void adb_wput(uint32_t addr, uint16_t w) {
	Log_Printf(LOG_WARN, "[ADB] illegal wput at $%08X -> bus error",addr);
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
}

void adb_bput(uint32_t addr, uint8_t b) {
	Log_Printf(LOG_WARN, "[ADB] illegal bput at $%08X -> bus error",addr);
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
}

void adb_reset(void) {
	Log_Printf(LOG_WARN, "[ADB] Reset");
	adb.intstatus = 0;
	adb.intmask = 0;
	adb.config = 0;
	adb.command = 0;
	adb.status = 0;
	adb.bitcount = 0;
	adb.data0 = 0;
	adb.data1 = 0;
	
	adb_kbd_reset();
	adb_mouse_reset();
}
