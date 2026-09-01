/*
  Previous - tablet.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains a simulation of the SummaGraphics MM 1201 and MM 961 
  digitizers (later models also known as SummaSketch graphics tablets) and
  several SD series WACOM digitizers.
*/
const char Tablet_fileid[] = "Previous tablet.c";

#include "main.h"
#include "configuration.h"
#include "m68000.h"
#include "screen.h"
#include "statusbar.h"
#include "scc.h"
#include "tablet.h"

#define LOG_TABLET_LEVEL      LOG_WARN
#define LOG_TABLET_DATA_LEVEL LOG_DEBUG


/* Flag indicating that host has initialised for tablet input */
bool bTabletEnabled = false;

/* Tablet parameters and data */
static struct tablet_device {
	/* SummaGraphics */
	int enabled;
	int xresolution;
	int yresolution;
	int outputrate;
	uint8_t mode;
	uint8_t increment;
	uint8_t axisupdate;
	uint8_t origin;
	uint8_t selftest;
	uint8_t id;
	int baudrate;
	
	/* WACOM */
	uint8_t relative;
	uint8_t datamode;
	uint8_t pressure;
	uint8_t suppress;
	
	/* General */
	uint8_t command[10];
	uint8_t data[10];
	int cmdsize;
	int received;
	int statsize;
	int count;
	
	int32_t xsize;
	int32_t ysize;
	int32_t xmax;
	int32_t ymax;
	int32_t xpos;
	int32_t ypos;
	int32_t xdelta;
	int32_t ydelta;
	uint8_t flags;
	
	/* Functions */
	void (*reset)(void);
	void (*receive)(uint8_t val);
	void (*pen_move)(int xrel, int yrel, int x, int y);
	void (*pen_button)(int tip, int pressed);
} tablet;

/* Tablet data output */
static void tablet_send(uint8_t val) {
	Log_Printf(LOG_TABLET_DATA_LEVEL, "[Tablet] Sending %02x", val);
	scc_receive(1, val);
}

void Tablet_IO_Handler(void) {
	tablet_send(tablet.data[tablet.statsize - tablet.count]);
	if (--tablet.count > 0) {
		CycInt_AddTimeEvent(1000, 0, EVENT_TABLET_IO);
	}
}

static void tablet_send_data(int size) {
	tablet.statsize = tablet.count = size;
	CycInt_AddTimeEvent(1000, 0, EVENT_TABLET_IO);
}


/*------------------------------------------*
 *                                          *
 *          SummaGraphics Tablets           *
 *                                          *
 *------------------------------------------*/

/* Commands from host to tablet */
#define SUMMA_SETBAUDRATE  0x20 /* SP */

/* for model 961 */
#define SUMMA_HORIZONTAL   0x62 /* b */
#define SUMMA_VERTICAL     0x63 /* c */
/* for model 1201 */
#define SUMMA_UPPERLEFT    0x62 /* b */
#define SUMMA_LOWERLEFT    0x63 /* c */

#define SUMMA_COO_RATE2    0x54 /* T */
#define SUMMA_COO_RATE20   0x53 /* S */
#define SUMMA_COO_RATE50   0x52 /* R */
#define SUMMA_COO_RATE100  0x51 /* Q */

#define SUMMA_STREAM       0x40 /* @ */
#define SUMMA_SWITCHSTREAM 0x41 /* A */
#define SUMMA_POINT        0x42 /* B */
#define SUMMA_REMOTEREQ    0x44 /* D */
#define SUMMA_REQUEST      0x50 /* P */
#define SUMMA_DELTA        0x45 /* E */
#define SUMMA_AXISUPDATE   0x47 /* G followed by one byte update value plus 32 */
#define SUMMA_INCREMENT    0x49 /* I followed by one byte increment value plus 32 */

#define SUMMA_RES_10       0x66 /* f */
#define SUMMA_RES_20       0x69 /* i */
#define SUMMA_RES_40       0x71 /* q */
#define SUMMA_RES_100      0x64 /* d */
#define SUMMA_RES_200      0x65 /* e */
#define SUMMA_RES_400      0x67 /* g */
#define SUMMA_RES_500      0x68 /* h */
#define SUMMA_RES_1000     0x6A /* j */

#define SUMMA_ROUNDOFF_1   0x6C /* l */
#define SUMMA_ROUNDOFF_2   0x6E /* n */
#define SUMMA_ROUNDOFF_4   0x70 /* p */

#define SUMMA_SETXYSCALE   0x72 /* r followed by two byte X and two byte Y scale values */

#define SUMMA_STARTTRANS   0x11 /* DC1 (XON) */
#define SUMMA_STOPTRANS    0x13 /* DC3 (XOFF) */

#define SUMMA_IDENTIFY0    0x30 /* 0 */
#define SUMMA_IDENTIFY1    0x31 /* 1 */

#define SUMMA_RESET        0x00 /* NUL */

#define SUMMA_STATUS       0x61 /* a */

#define SUMMA_SELFTEST     0x74 /* t */
#define SUMMA_TESTRESULTS  0x77 /* w */
#define SUMMA_ECHO         0x6B /* k */
#define SUMMA_CHECKCODE    0x78 /* x */
#define SUMMA_FACTORYTEST  0x7A /* z */


/* Output data */
/* Byte 0: */
#define SUMMA_PHASINGBIT  0x80 /* always 1 */
#define SUMMA_PROXIMITY   0x40 /* 1 = in proximity, 0 = out of proximity */
#define SUMMA_IDENTIFIER  0x20 /* user selected tabled identifier (0 or 1) */
#define SUMMA_X_SIGN      0x10 /* X coordinate sign, 1 = positive, 0 = negative */
#define SUMMA_Y_SIGN      0x08 /* Y coordinate sign, 1 = positive, 0 = negative */
#define SUMMA_BUTTON3     0x04 /* cursor button #3 / stylus N/A pressed */
#define SUMMA_BUTTON2     0x02 /* cursor button #2 / stylus barrel switch pressed */
#define SUMMA_BUTTON1     0x01 /* cursor button #1 / stylus pen tip pressed */
/* Byte 1 to 4:
 * |  normal mode              |  delta mode
 * 1: X coordinate low  7 bit  |  X coordinate 7 bit
 * 2: X coordiante high 7 bit  |  Y cooridnate 7 bit
 * 3: X coordinate low  7 bit  |  - (not transmitted)
 * 4: X coordiante high 7 bit  |  - (not transmitted)
 */
#define SUMMA_COORD_MASK  0x7F
#define SUMMA_COORD_SHIFT 7

/* Tablet orientation and origin */
#define SUMMA_ORIG_LOWER  0
#define SUMMA_ORIG_UPPER  1

/* Tablet size in tenth of an inch (avoid decimal numbers) */
#define SUMMA_1201_SIZE   117  /* 11.7 inch */
#define SUMMA_961_XSIZE   60   /* 6 inch */
#define SUMMA_961_YSIZE   90   /* 9 inch */


static void summa_set_bounds(void) {
	tablet.xmax = (tablet.xsize * tablet.xresolution) / 10;
	tablet.ymax = (tablet.ysize * tablet.yresolution) / 10;
}

static void summa_reset(void) {
	/* Set default values: */
	tablet.enabled     = 1;
	tablet.xresolution = 500;
	tablet.yresolution = 500;
	tablet.outputrate  = 100;
	tablet.mode        = SUMMA_SWITCHSTREAM;
	tablet.increment   = 0;
	tablet.axisupdate  = 0;
	tablet.origin      = SUMMA_ORIG_LOWER;
	tablet.selftest    = 0x4F;
	tablet.id          = 0;
	tablet.baudrate    = 9600;
	
	/* Set bounds: */
	if (ConfigureParams.Tablet.nTabletType == TABLET_MM961) {
		tablet.xsize = SUMMA_961_XSIZE;
		tablet.ysize = SUMMA_961_YSIZE;
	} else { /* MM1201 */
		tablet.xsize = SUMMA_1201_SIZE;
		tablet.ysize = SUMMA_1201_SIZE;
	}
	summa_set_bounds();
	
	/* Set constant flags: */
	tablet.flags = SUMMA_PHASINGBIT;
	if (tablet.id) {
		tablet.flags |= SUMMA_IDENTIFIER;
	}
}

static void summa_set_origin(uint8_t origin) {
	if (ConfigureParams.Tablet.nTabletType == TABLET_MM961) {
		tablet.origin = SUMMA_ORIG_LOWER;
	} else {
		if (origin == SUMMA_LOWERLEFT) {
			tablet.origin = SUMMA_ORIG_LOWER;
		} else {
			tablet.origin = SUMMA_ORIG_UPPER;
		}
	}
	summa_set_bounds();
}

static void summa_set_resolution(int resolution) {
	tablet.xresolution = resolution;
	tablet.yresolution = resolution;
	summa_set_bounds();
}

static void summa_set_increment(void) {
	uint8_t val;
	val = tablet.command[1] & SUMMA_COORD_MASK;
	if (val >= 32) {
		tablet.increment = val - 32;
	}
}

static void summa_set_axisupdate(void) {
	uint8_t val;
	val = tablet.command[1] & SUMMA_COORD_MASK;
	if (val >= 32) {
		tablet.axisupdate = val - 32;
	}
}

static void summa_set_xy_scale(void) {
	uint32_t x, y, xlpi, ylpi;
	
	x = tablet.command[1] | ((uint32_t)tablet.command[2] << 8);
	y = tablet.command[3] | ((uint32_t)tablet.command[4] << 8);
	
	xlpi = (x * 10) / tablet.xsize;
	ylpi = (y * 10) / tablet.ysize;
	
	if (xlpi >= 1 && xlpi <= 508) {
		tablet.xresolution = x;
	}
	if (ylpi >= 1 && ylpi <= 508) {
		tablet.yresolution = y;
	}
	summa_set_bounds();
}

static void summa_send_configuration(void) {
	tablet.data[0] = SUMMA_PHASINGBIT;
	tablet.data[0] |= tablet.id ? SUMMA_IDENTIFIER : 0;
	tablet.data[0] |= SUMMA_X_SIGN | SUMMA_Y_SIGN;
	tablet.data[1] = tablet.xmax & SUMMA_COORD_MASK;
	tablet.data[2] = (tablet.xmax >> SUMMA_COORD_SHIFT) & SUMMA_COORD_MASK;
	tablet.data[3] = tablet.ymax & SUMMA_COORD_MASK;
	tablet.data[4] = (tablet.ymax >> SUMMA_COORD_SHIFT) & SUMMA_COORD_MASK;
	
	tablet_send_data(5);
}

static void summa_send_state(void) {
	tablet.data[0] = tablet.flags;
	if (tablet.mode == SUMMA_DELTA) {
		tablet.data[1] = tablet.xdelta & SUMMA_COORD_MASK;
		tablet.data[2] = tablet.ydelta & SUMMA_COORD_MASK;
		tablet_send_data(3);
		tablet.xdelta = tablet.ydelta = 0;
	} else {
		tablet.data[1] = tablet.xpos & SUMMA_COORD_MASK;
		tablet.data[2] = (tablet.xpos >> SUMMA_COORD_SHIFT) & SUMMA_COORD_MASK;
		tablet.data[3] = tablet.ypos & SUMMA_COORD_MASK;
		tablet.data[4] = (tablet.ypos >> SUMMA_COORD_SHIFT) & SUMMA_COORD_MASK;
		tablet_send_data(5);
	}
}

static void summa_send_checksum(void) {
	snprintf((char*)tablet.data, sizeof(tablet.data), ".#%04X", 0xb044);
	tablet_send_data(6);
}

static void summa_receive(uint8_t val) {
	Log_Printf(LOG_TABLET_DATA_LEVEL, "[Tablet] Receiving %02x", val);
	
	if (tablet.command[0] == SUMMA_ECHO && val != SUMMA_RESET) {
		tablet.data[0] = val;
		tablet_send_data(1);
		return;
	}
	
	if (tablet.received == 0) {
		if (val == SUMMA_INCREMENT || val == SUMMA_AXISUPDATE) {
			tablet.cmdsize = 2;
		} else if (val == SUMMA_SETXYSCALE) {
			tablet.cmdsize = 5;
		} else {
			tablet.cmdsize = 1;
		}
	}
	
	tablet.command[tablet.received++] = val;
	
	if (tablet.cmdsize > tablet.received) {
		return;
	}

	switch (tablet.command[0]) {
		case SUMMA_SETBAUDRATE:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Automatic baud rate");
			break;
		case SUMMA_HORIZONTAL:
			if (ConfigureParams.Tablet.nTabletType == TABLET_MM961) {
				Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Tablet origin: horizontal");
			} else { /* MM1201 */
				Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Tablet origin: upper left");
			}
			summa_set_origin(tablet.command[0]);
			break;
		case SUMMA_VERTICAL:
			if (ConfigureParams.Tablet.nTabletType == TABLET_MM961) {
				Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Tablet origin: vertical");
			} else { /* MM1201 */
				Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Tablet origin: lower left");
			}
			summa_set_origin(tablet.command[0]);
			break;
		case SUMMA_STREAM:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: stream");
			tablet.mode = SUMMA_STREAM;
			Statusbar_AddMessage("Starting tablet input (absolute mode)", 0);
			bTabletEnabled = true;
			break;
		case SUMMA_SWITCHSTREAM:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: switch stream");
			tablet.mode = SUMMA_SWITCHSTREAM;
			Statusbar_AddMessage("Stopping tablet input", 0);
			bTabletEnabled = false;
			break;
		case SUMMA_POINT:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: point");
			tablet.mode = SUMMA_POINT;
			break;
		case SUMMA_REMOTEREQ:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: remote request");
			tablet.mode = SUMMA_REMOTEREQ;
			break;
		case SUMMA_REQUEST:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Remote request");
			summa_send_state();
			break;
		case SUMMA_DELTA:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: delta");
			tablet.mode = SUMMA_DELTA;
			Statusbar_AddMessage("Starting tablet input (relative mode)", 0);
			bTabletEnabled = true;
			break;
		case SUMMA_INCREMENT:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: incremental");
			summa_set_increment();
			break;
		case SUMMA_AXISUPDATE:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Data collection mode: axis update");
			summa_set_axisupdate();
			break;
		case SUMMA_COO_RATE2:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Coordinate output rate: 2 pairs per second");
			tablet.outputrate = 2;
			break;
		case SUMMA_COO_RATE20:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Coordinate output rate: 20 pairs per second");
			tablet.outputrate = 20;
			break;
		case SUMMA_COO_RATE50:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Coordinate output rate: 50 pairs per second");
			tablet.outputrate = 50;
			break;
		case SUMMA_COO_RATE100:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Coordinate output rate: 100 pairs per second");
			tablet.outputrate = 100;
			break;
		case SUMMA_RES_10:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 10 lpmm");
			summa_set_resolution(254);
			break;
		case SUMMA_RES_20:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 20 lpmm");
			summa_set_resolution(508);
			break;
		case SUMMA_RES_40:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 40 lpmm");
			summa_set_resolution(1016);
			break;
		case SUMMA_RES_100:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 100 lpi");
			summa_set_resolution(100);
			break;
		case SUMMA_RES_200:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 200 lpi");
			summa_set_resolution(200);
			break;
		case SUMMA_RES_400:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 400 lpi");
			summa_set_resolution(400);
			break;
		case SUMMA_RES_500:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 500 lpi");
			summa_set_resolution(500);
			break;
		case SUMMA_RES_1000:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Resolution: 1000 lpi");
			summa_set_resolution(1000);
			break;
		case SUMMA_ROUNDOFF_1:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Grid roundoff: 1 lpi");
			summa_set_resolution(1);
			break;
		case SUMMA_ROUNDOFF_2:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Grid roundoff: 2 lpi");
			summa_set_resolution(2);
			break;
		case SUMMA_ROUNDOFF_4:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Grid roundoff: 4 lpi");
			summa_set_resolution(4);
			break;
		case SUMMA_SETXYSCALE:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Set X,Y scale");
			summa_set_xy_scale();
			break;
		case SUMMA_STOPTRANS:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Stop transmission");
			tablet.enabled = 0;
			break;
		case SUMMA_STARTTRANS:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Start transmission");
			tablet.enabled = 1;
			break;
		case SUMMA_IDENTIFY0:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Identifier: zero");
			tablet.id = 0;
			break;
		case SUMMA_IDENTIFY1:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Identifier: one");
			tablet.id = 1;
			break;
		case SUMMA_RESET:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Reset");
			summa_reset();
			break;
		case SUMMA_STATUS:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Send configuration");
			summa_send_configuration();
			break;
		case SUMMA_SELFTEST:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Self-test");
			break;
		case SUMMA_TESTRESULTS:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Send test results");
			break;
		case SUMMA_ECHO:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Echo");
			break;
		case SUMMA_CHECKCODE:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Check code");
			summa_send_checksum();
			break;
		case SUMMA_FACTORYTEST:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Factory test");
			break;

		default:
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Unknown input: %c", (char)val);
			break;
	}
	
	tablet.cmdsize = tablet.received =  0;
}

static void summa_pen_move(int xrel, int yrel, int x, int y) {
	if (tablet.mode == SUMMA_DELTA) {
		if (tablet.origin == SUMMA_ORIG_LOWER) {
			yrel = -yrel;
		}
		if (xrel >= 0) {
			tablet.flags |= SUMMA_X_SIGN;
		} else {
			tablet.flags &= ~SUMMA_X_SIGN;
			xrel = -xrel;
		}
		if (yrel >= 0) {
			tablet.flags |= SUMMA_Y_SIGN;
		} else {
			tablet.flags &= ~SUMMA_Y_SIGN;
			yrel = -yrel;
		}
		tablet.xdelta = xrel * 4; /* customised for mouse input */
		tablet.ydelta = yrel * 4;
		if (tablet.xdelta > SUMMA_COORD_MASK) {
			tablet.xdelta = SUMMA_COORD_MASK;
		}
		if (tablet.ydelta > SUMMA_COORD_MASK) {
			tablet.ydelta = SUMMA_COORD_MASK;
		}
	} else {
		x++;
		y++;
		if (x > screen_w) x = screen_w;
		else if (x < 0)   x = 0;
		if (y > screen_h) y = screen_h;
		else if (y < 0)   y = 0;
		tablet.xpos = (x * tablet.xmax) / screen_w;
		tablet.ypos = (y * tablet.ymax) / screen_h;
		if (tablet.origin == SUMMA_ORIG_LOWER) {
			tablet.ypos = tablet.ymax - tablet.ypos;
		}
		tablet.flags |= SUMMA_X_SIGN | SUMMA_Y_SIGN;
	}
	if (tablet.mode != SUMMA_REMOTEREQ) {
		summa_send_state();
	}
}

static void summa_pen_button(int tip, int pressed) {
	if (pressed) {
		tablet.flags |=  (tip ? SUMMA_BUTTON1 : SUMMA_BUTTON2);
	} else {
		tablet.flags &= ~(tip ? SUMMA_BUTTON1 : SUMMA_BUTTON2);
	}
	if (tablet.mode != SUMMA_REMOTEREQ) {
		summa_send_state();
	}
}


/*------------------------------------------*
 *                                          *
 *               WACOM Tablets              *
 *                                          *
 *------------------------------------------*/

/* Special characters */
#define WACOM_DONE    0x0D /* CR */
#define WACOM_UNSET   0x30 /* 0 */
#define WACOM_SET     0x31 /* 1 */
#define WACOM_INIT0   0x7E /* ~ */
#define WACOM_INIT1   0x23 /* # */

/* Commands without data */
#define WACOM_STREAM_MODE   "SR"
#define WACOM_SWITCH_MODE   "SW"

/* Commands with data */
#define WACOM_BINARY_MODE   "AS" /* data: '0' = ascii,    '1' = binary */
#define WACOM_RELATIVE_MODE "DE" /* data: '0' = absolute, '1' = relative */
#define WACOM_PRESSURE_MODE "PH" /* data: '0' = off,      '1' = on */
#define WACOM_SUPPRESS_MODE "SU" /* data: '0' = off,      '1' = on */

/* Command data values */
#define WACOM_ASCII        0
#define WACOM_BINARY       1
#define WACOM_ABSOLUTE     0
#define WACOM_RELATIVE     1
#define WACOM_PRESSURE_OFF 0
#define WACOM_PRESSURE_ON  1
#define WACOM_SUPRESS_OFF  0
#define WACOM_SUPRESS_ON   1

/* Operating modes */
#define WACOM_SWITCHSTREAM 0
#define WACOM_STREAM       1

/* Output data */
/* Byte 0 flags: */
#define WACOM_SYNCBIT      0x80 /* always 1 */
#define WACOM_PROXIMITY    0x40 /* 1 = in proximity, 0 = out of proximity */
#define WACOM_POINTINGDEV  0x20 /* 1 = stylus, 0 = cursor */
#define WACOM_PRESSURE     0x10 /* 1 = pressure supported, 0 = pressure not supported */

/* Coordinates: Byte 0 to 5:
 * 0: X coordinate high 3 bit: ---- -xxx
 * 1: X coordinate med  7 bit: -xxx xxxx
 * 2: X coordiante low  7 bit: -xxx xxxx
 * 3: Y coordinate high 3 bit: ---- -xxx
 * 4: Y coordinate med  7 bit: -xxx xxxx
 * 5: Y coordiante low  7 bit: -xxx xxxx
 */
#define WACOM_HCOORD_MASK  0x07
#define WACOM_MCOORD_MASK  0x7F
#define WACOM_LCOORD_MASK  0x7F
#define WACOM_HCOORD_SHIFT 14
#define WACOM_MCOORD_SHIFT 7
#define WACOM_LCOORD_SHIFT 0

/* Byte 6 flags (valid if not in pressure mode): */
#define WACOM_BUTTON4      0x08 /* cursor button #4 / stylus N/A pressed */
#define WACOM_BUTTON3      0x04 /* cursor button #3 / stylus N/A pressed */
#define WACOM_BUTTON2      0x02 /* cursor button #2 / stylus side switch pressed */
#define WACOM_BUTTON1      0x01 /* cursor button #1 / stylus pen tip pressed */
/* Byte 6 pressure (valid only in pressure mode) */
#define WACOM_PRESSUREMASK 0x7F


static void wacom_reset(void) {
	/* Set default values: */
	tablet.enabled  = 1;
	tablet.received = 0;
	switch (ConfigureParams.Tablet.nTabletType) {
		case TABLET_SD210L: /* 3.0 and later */
			tablet.xmax = 31750;
			tablet.ymax = 23100;
			break;
		case TABLET_SD310E: /* 3.0 and later */
			tablet.xmax = 21200;
			tablet.ymax = 15000;
			break;
		case TABLET_SD320E: /* 3.0 and later */
			tablet.xmax = 19050;
			tablet.ymax = 19050;
			break;
		case TABLET_SD510C: /* 3.0 and later */
			tablet.xmax = 4640;
			tablet.ymax = 3020;
			break;
		default: /* 2.0 and later */
			tablet.xmax = 15240;
			tablet.ymax = 15240;
			break;
	}
	tablet.mode     = WACOM_SWITCHSTREAM;
	tablet.relative = WACOM_ABSOLUTE;
	tablet.datamode = WACOM_ASCII;
	tablet.pressure = WACOM_PRESSURE_OFF;
	tablet.suppress = WACOM_SUPRESS_OFF;
	tablet.baudrate = 9600;
		
	/* Set constant flags: */
	tablet.flags = WACOM_SYNCBIT | WACOM_PROXIMITY | WACOM_POINTINGDEV;
}

static void wacom_send_config(void) {
	const char* type;
	
	switch (ConfigureParams.Tablet.nTabletType) {
		case TABLET_SD210L:
			type = "210";
			break;
		case TABLET_SD310E:
			type = "310";
			break;
		case TABLET_SD320E:
			type = "320";
			break;
		case TABLET_SD510C:
			type = "SD51C";
			break;
		default:
			type = "420";
			break;
	}
	
	Log_Printf(LOG_WARN, "[Tablet] Model %s", type);
	snprintf((char*)tablet.data, sizeof(tablet.data), "~#%s", type);
	tablet_send_data(strlen((char*)tablet.data));
}

static void wacom_send_state(void) {
	/* Set coordinates */
	tablet.data[0] = (tablet.xpos >> WACOM_HCOORD_SHIFT) & WACOM_HCOORD_MASK;
	tablet.data[1] = (tablet.xpos >> WACOM_MCOORD_SHIFT) & WACOM_MCOORD_MASK;
	tablet.data[2] = (tablet.xpos >> WACOM_LCOORD_SHIFT) & WACOM_LCOORD_MASK;
	tablet.data[3] = (tablet.ypos >> WACOM_HCOORD_SHIFT) & WACOM_HCOORD_MASK;
	tablet.data[4] = (tablet.ypos >> WACOM_MCOORD_SHIFT) & WACOM_MCOORD_MASK;
	tablet.data[5] = (tablet.ypos >> WACOM_LCOORD_SHIFT) & WACOM_LCOORD_MASK;
	
	/* Add buttons and flags */
	tablet.data[6] = tablet.flags & 0x0F;
	tablet.data[0]|= tablet.flags & 0xF0;
	
	tablet_send_data(7);
}

static void wacom_set_mode(uint8_t* mode, uint8_t val) {
	if (val == WACOM_SET || val == WACOM_UNSET) {
		*mode = val - WACOM_UNSET;
	} else {
		Log_Printf(LOG_WARN, "[Tablet] Unknown mode: %02x", val);
	}
}

static void wacom_receive(uint8_t val) {
	Log_Printf(LOG_TABLET_DATA_LEVEL, "[Tablet] Receiving %02x", val);
	
	if (val == WACOM_INIT0) {
		tablet.command[0] = val;
		return;
	} else if (val == WACOM_INIT1) {
		if (tablet.command[0] == WACOM_INIT0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Configure");
			wacom_reset();
			wacom_send_config();
			Statusbar_AddMessage("Starting tablet input", 0);
			bTabletEnabled = true;
		}
		return;
	}
	
	if (val == WACOM_DONE) {
		char* str = (char*)tablet.command;
		uint8_t opt = (char)tablet.command[2];
		
		if (strncmp(str, WACOM_STREAM_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Select stream mode");
			tablet.mode = WACOM_STREAM;
		} else if (strncmp(str, WACOM_SWITCH_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Select switchstream mode");
			tablet.mode = WACOM_SWITCHSTREAM;
			Statusbar_AddMessage("Stopping tablet input", 0);
			bTabletEnabled = false;
		} else if (strncmp(str, WACOM_RELATIVE_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Set relative mode to %c", (char)opt);
			wacom_set_mode(&tablet.relative, opt);
		} else if (strncmp(str, WACOM_BINARY_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Set binary mode to %c", (char)opt);
			wacom_set_mode(&tablet.datamode, opt);
		} else if (strncmp(str, WACOM_PRESSURE_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Set pressure mode to %c", (char)opt);
			wacom_set_mode(&tablet.pressure, opt);
		} else if (strncmp(str, WACOM_SUPPRESS_MODE, 2) == 0) {
			Log_Printf(LOG_TABLET_LEVEL, "[Tablet] Set suppress mode to %c", (char)opt);
			wacom_set_mode(&tablet.suppress, opt);
		} else {
			Log_Printf(LOG_WARN, "[Tablet] Unknown command: %.2s", str);
		}
		tablet.received = 0;
	} else if (tablet.received < (int)sizeof(tablet.command)) {
		tablet.command[tablet.received++] = val;
	} else if (tablet.received > 0) {
		Log_Printf(LOG_WARN, "[Tablet] Overflow while receiving %02x", val);
		tablet.received--;
	} else {
		Log_Printf(LOG_WARN, "[Tablet] Internal error");
	}
}

static void wacom_pen_move(int xrel, int yrel, int x, int y) {
	if (tablet.relative == WACOM_RELATIVE) {
		tablet.xpos = xrel * 4;
		tablet.ypos = yrel * 4;
	} else {
		x++;
		y++;
		if (x > screen_w) x = screen_w;
		else if (x < 0)   x = 0;
		if (y > screen_h) y = screen_h;
		else if (y < 0)   y = 0;
		tablet.xpos = (x * tablet.xmax) / screen_w;
		tablet.ypos = (y * tablet.ymax) / screen_h;
	}
	if (tablet.xpos > INT16_MAX) {
		tablet.xpos = INT16_MAX;
	} else if (tablet.xpos < INT16_MIN) {
		tablet.xpos = INT16_MIN;
	}
	if (tablet.ypos > INT16_MAX) {
		tablet.ypos = INT16_MAX;
	} else if (tablet.ypos < INT16_MIN) {
		tablet.ypos = INT16_MIN;
	}
	if (tablet.enabled) {
		wacom_send_state();
	}
}

static void wacom_pen_button(int tip, int pressed) {
	if (pressed) {
		tablet.flags |=  (tip ? WACOM_BUTTON1 : WACOM_BUTTON2);
	} else {
		tablet.flags &= ~(tip ? WACOM_BUTTON1 : WACOM_BUTTON2);
	}
	if (tablet.enabled) {
		wacom_send_state();
	}
}



/* Tablet interface functions */
void tablet_receive(uint8_t val) {
	if (tablet.receive) {
		tablet.receive(val);
	}
}

void tablet_pen_move(int xrel, int yrel, int x, int y) {
	if (tablet.pen_move) {
		tablet.pen_move(xrel, yrel, x, y);
	}
}

void tablet_pen_button(int tip, int pressed) {
	if (tablet.pen_button) {
		tablet.pen_button(tip, pressed);
	}
}

void Tablet_Reset(void) {
	Log_Printf(LOG_WARN, "[Tablet] Reset");
	
	/* Set tablet functions */
	switch (ConfigureParams.Tablet.nTabletType) {
		case TABLET_NONE:
			tablet.reset      = NULL;
			tablet.receive    = NULL;
			tablet.pen_move   = NULL;
			tablet.pen_button = NULL;
			break;
		case TABLET_MM961:
		case TABLET_MM1201:
			tablet.reset      = summa_reset;
			tablet.receive    = summa_receive;
			tablet.pen_move   = summa_pen_move;
			tablet.pen_button = summa_pen_button;
			break;
		default:
			tablet.reset      = wacom_reset;
			tablet.receive    = wacom_receive;
			tablet.pen_move   = wacom_pen_move;
			tablet.pen_button = wacom_pen_button;
			break;
	}
	
	/* Reset tablet */
	if (tablet.reset) {
		tablet.reset();
	}
	
	/* Uninit tablet */
	bTabletEnabled = false;
}
