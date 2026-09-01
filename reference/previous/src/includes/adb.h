/*
  Previous - adb.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_ADB_H
#define PREV_ADB_H

/* -------- ADB scancodes -------- */
#define APPLEKEY_a               0x00
#define APPLEKEY_s               0x01
#define APPLEKEY_d               0x02
#define APPLEKEY_f               0x03
#define APPLEKEY_h               0x04
#define APPLEKEY_g               0x05
#define APPLEKEY_z               0x06
#define APPLEKEY_x               0x07
#define APPLEKEY_c               0x08
#define APPLEKEY_v               0x09
#define APPLEKEY_LESS            0x0a
#define APPLEKEY_b               0x0b
#define APPLEKEY_q               0x0c
#define APPLEKEY_w               0x0d
#define APPLEKEY_e               0x0e
#define APPLEKEY_r               0x0f
#define APPLEKEY_y               0x10
#define APPLEKEY_t               0x11
#define APPLEKEY_1               0x12
#define APPLEKEY_2               0x13
#define APPLEKEY_3               0x14
#define APPLEKEY_4               0x15
#define APPLEKEY_6               0x16
#define APPLEKEY_5               0x17
#define APPLEKEY_EQUALS          0x18
#define APPLEKEY_9               0x19
#define APPLEKEY_7               0x1a
#define APPLEKEY_MINUS           0x1b
#define APPLEKEY_8               0x1c
#define APPLEKEY_0               0x1d
#define APPLEKEY_CLOSEBRACKET    0x1e
#define APPLEKEY_o               0x1f
#define APPLEKEY_u               0x20
#define APPLEKEY_OPENBRACKET     0x21
#define APPLEKEY_i               0x22
#define APPLEKEY_p               0x23
#define APPLEKEY_RETURN          0x24
#define APPLEKEY_l               0x25
#define APPLEKEY_j               0x26
#define APPLEKEY_QUOTE           0x27
#define APPLEKEY_k               0x28
#define APPLEKEY_SEMICOLON       0x29
#define APPLEKEY_BACKSLASH       0x2a
#define APPLEKEY_COMMA           0x2b
#define APPLEKEY_SLASH           0x2c
#define APPLEKEY_n               0x2d
#define APPLEKEY_m               0x2e
#define APPLEKEY_PERIOD          0x2f
#define APPLEKEY_TAB             0x30
#define APPLEKEY_SPACE           0x31
#define APPLEKEY_BACKQUOTE       0x32
#define APPLEKEY_DELETE          0x33
#define APPLEKEY_ESC             0x35
#define APPLEKEY_CTL_LEFT        0x36
#define APPLEKEY_APPLE_LEFT      0x37
#define APPLEKEY_SHIFT_LEFT      0x38
#define APPLEKEY_CAPS_LOCK       0x39
#define APPLEKEY_OPTION_LEFT     0x3a
#define APPLEKEY_LEFT_ARROW      0x3b
#define APPLEKEY_RIGHT_ARROW     0x3c
#define APPLEKEY_DOWN_ARROW      0x3d
#define APPLEKEY_UP_ARROW        0x3e
#define APPLEKEY_KEYPAD_PERIOD   0x41
#define APPLEKEY_KEYPAD_MULTIPLY 0x43
#define APPLEKEY_KEYPAD_PLUS     0x45
#define APPLEKEY_KEYPAD_DIVIDE   0x4b
#define APPLEKEY_KEYPAD_ENTER    0x4c
#define APPLEKEY_KEYPAD_MINUS    0x4e
#define APPLEKEY_KEYPAD_EQUALS   0x51
#define APPLEKEY_KEYPAD_0        0x52
#define APPLEKEY_KEYPAD_1        0x53
#define APPLEKEY_KEYPAD_2        0x54
#define APPLEKEY_KEYPAD_3        0x55
#define APPLEKEY_KEYPAD_4        0x56
#define APPLEKEY_KEYPAD_5        0x57
#define APPLEKEY_KEYPAD_6        0x58
#define APPLEKEY_KEYPAD_7        0x59
#define APPLEKEY_KEYPAD_8        0x5b
#define APPLEKEY_KEYPAD_9        0x5c
#define APPLEKEY_HELP            0x72
#define APPLEKEY_VOLUME_UP       0x73
#define APPLEKEY_BRIGHTNESS_UP   0x74
#define APPLEKEY_VOLUME_DOWN     0x77
#define APPLEKEY_BRIGHTNESS_DOWN 0x79
#define APPLEKEY_SHIFT_RIGHT     0x7b
#define APPLEKEY_OPTION_RIGHT    0x7c
#define APPLEKEY_POWER           0x7f

extern uint32_t adb_lget(uint32_t addr);
extern uint16_t adb_wget(uint32_t addr);
extern uint8_t  adb_bget(uint32_t addr);

extern void adb_lput(uint32_t addr, uint32_t l);
extern void adb_wput(uint32_t addr, uint16_t w);
extern void adb_bput(uint32_t addr, uint8_t  b);

extern void adb_keyup(uint8_t key);
extern void adb_keydown(uint8_t key);
extern void adb_mouse_button(bool left, bool down);
extern void adb_mouse_move(int x, int y);

extern void adb_reset(void);

#endif /* PREV_ADB_H */
