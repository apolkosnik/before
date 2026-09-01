/*
  Previous - sdlkeymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Here we translate key presses and mouse motion.
*/
const char SDLkeymap_fileid[] = "Previous sdlkeymap.c";

#include <ctype.h>
#include "main.h"
#include "keymap.h"
#include "sdlkeymap.h"
#include "configuration.h"
#include "log.h"
#include "kms.h"
#include "adb.h"
#include "tablet.h"

#define  LOG_KEYMAP_LEVEL   LOG_DEBUG


/**
 * Initialization.
 */
void Keymap_Init(void)
{
	if (ConfigureParams.Mouse.bUseRawMotion) {
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
	} else {
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
	}
}

/**
 * Set defaults for shortcut keys
 */
void Keymap_InitShortcutDefaultKeys(void)
{
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS]    = SDLK_F12;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] = SDLK_F11;

	ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS]       = SDLK_O;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN]    = SDLK_F;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_PAUSE]         = SDLK_P;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG_M68K]    = SDLK_D;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG_I860]    = SDLK_I;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB]     = SDLK_M;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET]     = SDLK_C;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT]    = SDLK_G;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_RECORD]        = SDLK_R;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND]         = SDLK_S;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT]          = SDLK_Q;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_DIMENSION]     = SDLK_N;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_STATUSBAR]     = SDLK_B;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_TITLEBAR]      = SDLK_T;
}

/*-----------------------------------------------------------------------*/
/**
 * This function translates the scancodes provided by SDL to NeXT
 * scancode values.
 */
static uint8_t Keymap_GetKeyFromScancode(SDL_Scancode sdlscancode)
{
	Log_Printf(LOG_KEYMAP_LEVEL, "[Keymap] Scancode: %i (%s)\n", sdlscancode, SDL_GetScancodeName(sdlscancode));

	if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		switch (sdlscancode) {
			case SDL_SCANCODE_ESCAPE:         return APPLEKEY_ESC;
			case SDL_SCANCODE_GRAVE:          return APPLEKEY_BACKQUOTE;
			case SDL_SCANCODE_1:              return APPLEKEY_1;
			case SDL_SCANCODE_2:              return APPLEKEY_2;
			case SDL_SCANCODE_3:              return APPLEKEY_3;
			case SDL_SCANCODE_4:              return APPLEKEY_4;
			case SDL_SCANCODE_5:              return APPLEKEY_5;
			case SDL_SCANCODE_6:              return APPLEKEY_6;
			case SDL_SCANCODE_7:              return APPLEKEY_7;
			case SDL_SCANCODE_8:              return APPLEKEY_8;
			case SDL_SCANCODE_9:              return APPLEKEY_9;
			case SDL_SCANCODE_0:              return APPLEKEY_0;
			case SDL_SCANCODE_MINUS:          return APPLEKEY_MINUS;
			case SDL_SCANCODE_EQUALS:         return APPLEKEY_EQUALS;
			case SDL_SCANCODE_BACKSPACE:      return APPLEKEY_DELETE;
				
			case SDL_SCANCODE_TAB:            return APPLEKEY_TAB;
			case SDL_SCANCODE_Q:              return APPLEKEY_q;
			case SDL_SCANCODE_W:              return APPLEKEY_w;
			case SDL_SCANCODE_E:              return APPLEKEY_e;
			case SDL_SCANCODE_R:              return APPLEKEY_r;
			case SDL_SCANCODE_T:              return APPLEKEY_t;
			case SDL_SCANCODE_Y:              return APPLEKEY_y;
			case SDL_SCANCODE_U:              return APPLEKEY_u;
			case SDL_SCANCODE_I:              return APPLEKEY_i;
			case SDL_SCANCODE_O:              return APPLEKEY_o;
			case SDL_SCANCODE_P:              return APPLEKEY_p;
			case SDL_SCANCODE_LEFTBRACKET:    return APPLEKEY_OPENBRACKET;
			case SDL_SCANCODE_RIGHTBRACKET:   return APPLEKEY_CLOSEBRACKET;
			case SDL_SCANCODE_BACKSLASH:      return APPLEKEY_BACKSLASH;
				
			case SDL_SCANCODE_A:              return APPLEKEY_a;
			case SDL_SCANCODE_S:              return APPLEKEY_s;
			case SDL_SCANCODE_D:              return APPLEKEY_d;
			case SDL_SCANCODE_F:              return APPLEKEY_f;
			case SDL_SCANCODE_G:              return APPLEKEY_g;
			case SDL_SCANCODE_H:              return APPLEKEY_h;
			case SDL_SCANCODE_J:              return APPLEKEY_j;
			case SDL_SCANCODE_K:              return APPLEKEY_k;
			case SDL_SCANCODE_L:              return APPLEKEY_l;
			case SDL_SCANCODE_SEMICOLON:      return APPLEKEY_SEMICOLON;
			case SDL_SCANCODE_APOSTROPHE:     return APPLEKEY_QUOTE;
			case SDL_SCANCODE_RETURN:         return APPLEKEY_RETURN;
				
			case SDL_SCANCODE_NONUSBACKSLASH: return APPLEKEY_LESS;
			case SDL_SCANCODE_Z:              return APPLEKEY_z;
			case SDL_SCANCODE_X:              return APPLEKEY_x;
			case SDL_SCANCODE_C:              return APPLEKEY_c;
			case SDL_SCANCODE_V:              return APPLEKEY_v;
			case SDL_SCANCODE_B:              return APPLEKEY_b;
			case SDL_SCANCODE_N:              return APPLEKEY_n;
			case SDL_SCANCODE_M:              return APPLEKEY_m;
			case SDL_SCANCODE_COMMA:          return APPLEKEY_COMMA;
			case SDL_SCANCODE_PERIOD:         return APPLEKEY_PERIOD;
			case SDL_SCANCODE_SLASH:          return APPLEKEY_SLASH;
			case SDL_SCANCODE_SPACE:          return APPLEKEY_SPACE;
				
			case SDL_SCANCODE_NUMLOCKCLEAR:   return APPLEKEY_BACKQUOTE;
			case SDL_SCANCODE_KP_EQUALS:      return APPLEKEY_KEYPAD_EQUALS;
			case SDL_SCANCODE_KP_DIVIDE:      return APPLEKEY_KEYPAD_DIVIDE;
			case SDL_SCANCODE_KP_MULTIPLY:    return APPLEKEY_KEYPAD_MULTIPLY;
			case SDL_SCANCODE_KP_7:           return APPLEKEY_KEYPAD_7;
			case SDL_SCANCODE_KP_8:           return APPLEKEY_KEYPAD_8;
			case SDL_SCANCODE_KP_9:           return APPLEKEY_KEYPAD_9;
			case SDL_SCANCODE_KP_MINUS:       return APPLEKEY_KEYPAD_MINUS;
			case SDL_SCANCODE_KP_4:           return APPLEKEY_KEYPAD_4;
			case SDL_SCANCODE_KP_5:           return APPLEKEY_KEYPAD_5;
			case SDL_SCANCODE_KP_6:           return APPLEKEY_KEYPAD_6;
			case SDL_SCANCODE_KP_PLUS:        return APPLEKEY_KEYPAD_PLUS;
			case SDL_SCANCODE_KP_1:           return APPLEKEY_KEYPAD_1;
			case SDL_SCANCODE_KP_2:           return APPLEKEY_KEYPAD_2;
			case SDL_SCANCODE_KP_3:           return APPLEKEY_KEYPAD_3;
			case SDL_SCANCODE_KP_0:           return APPLEKEY_KEYPAD_0;
			case SDL_SCANCODE_KP_PERIOD:      return APPLEKEY_KEYPAD_PERIOD;
			case SDL_SCANCODE_KP_ENTER:       return APPLEKEY_KEYPAD_ENTER;
				
			case SDL_SCANCODE_LEFT:           return APPLEKEY_LEFT_ARROW;
			case SDL_SCANCODE_RIGHT:          return APPLEKEY_RIGHT_ARROW;
			case SDL_SCANCODE_UP:             return APPLEKEY_UP_ARROW;
			case SDL_SCANCODE_DOWN:           return APPLEKEY_DOWN_ARROW;
				
				/* Modifier keys */
			case SDL_SCANCODE_RSHIFT:
			case SDL_SCANCODE_LSHIFT:         return APPLEKEY_SHIFT_LEFT;
			case SDL_SCANCODE_RGUI:
			case SDL_SCANCODE_LGUI:           return ConfigureParams.Keyboard.bSwapCmdAlt?APPLEKEY_OPTION_LEFT:APPLEKEY_APPLE_LEFT;
			case SDL_SCANCODE_MENU:
			case SDL_SCANCODE_RCTRL:          return APPLEKEY_HELP;
			case SDL_SCANCODE_LCTRL:          return APPLEKEY_CTL_LEFT;
			case SDL_SCANCODE_RALT:
			case SDL_SCANCODE_LALT:           return ConfigureParams.Keyboard.bSwapCmdAlt?APPLEKEY_APPLE_LEFT:APPLEKEY_OPTION_LEFT;
			case SDL_SCANCODE_CAPSLOCK:       return APPLEKEY_CAPS_LOCK;
				
				/* Special keys */
			case SDL_SCANCODE_F10:
			case SDL_SCANCODE_DELETE:         return APPLEKEY_POWER;
			case SDL_SCANCODE_F5:
			case SDL_SCANCODE_END:            return APPLEKEY_VOLUME_DOWN;
			case SDL_SCANCODE_F6:
			case SDL_SCANCODE_HOME:           return APPLEKEY_VOLUME_UP;
			case SDL_SCANCODE_F1:
			case SDL_SCANCODE_PAGEDOWN:       return APPLEKEY_BRIGHTNESS_DOWN;
			case SDL_SCANCODE_F2:
			case SDL_SCANCODE_PAGEUP:         return APPLEKEY_BRIGHTNESS_UP;
				
			default:                          return 0xff;
		}
	} else {
		switch (sdlscancode) {
			case SDL_SCANCODE_ESCAPE:         return NEXTKEY_ESC;
			case SDL_SCANCODE_GRAVE:          return NEXTKEY_BACKQUOTE;
			case SDL_SCANCODE_1:              return NEXTKEY_1;
			case SDL_SCANCODE_2:              return NEXTKEY_2;
			case SDL_SCANCODE_3:              return NEXTKEY_3;
			case SDL_SCANCODE_4:              return NEXTKEY_4;
			case SDL_SCANCODE_5:              return NEXTKEY_5;
			case SDL_SCANCODE_6:              return NEXTKEY_6;
			case SDL_SCANCODE_7:              return NEXTKEY_7;
			case SDL_SCANCODE_8:              return NEXTKEY_8;
			case SDL_SCANCODE_9:              return NEXTKEY_9;
			case SDL_SCANCODE_0:              return NEXTKEY_0;
			case SDL_SCANCODE_MINUS:          return NEXTKEY_MINUS;
			case SDL_SCANCODE_EQUALS:         return NEXTKEY_EQUALS;
			case SDL_SCANCODE_BACKSPACE:      return NEXTKEY_DELETE;
				
			case SDL_SCANCODE_TAB:            return NEXTKEY_TAB;
			case SDL_SCANCODE_Q:              return NEXTKEY_q;
			case SDL_SCANCODE_W:              return NEXTKEY_w;
			case SDL_SCANCODE_E:              return NEXTKEY_e;
			case SDL_SCANCODE_R:              return NEXTKEY_r;
			case SDL_SCANCODE_T:              return NEXTKEY_t;
			case SDL_SCANCODE_Y:              return NEXTKEY_y;
			case SDL_SCANCODE_U:              return NEXTKEY_u;
			case SDL_SCANCODE_I:              return NEXTKEY_i;
			case SDL_SCANCODE_O:              return NEXTKEY_o;
			case SDL_SCANCODE_P:              return NEXTKEY_p;
			case SDL_SCANCODE_LEFTBRACKET:    return NEXTKEY_OPENBRACKET;
			case SDL_SCANCODE_RIGHTBRACKET:   return NEXTKEY_CLOSEBRACKET;
			case SDL_SCANCODE_BACKSLASH:      return NEXTKEY_BACKSLASH;
				
			case SDL_SCANCODE_A:              return NEXTKEY_a;
			case SDL_SCANCODE_S:              return NEXTKEY_s;
			case SDL_SCANCODE_D:              return NEXTKEY_d;
			case SDL_SCANCODE_F:              return NEXTKEY_f;
			case SDL_SCANCODE_G:              return NEXTKEY_g;
			case SDL_SCANCODE_H:              return NEXTKEY_h;
			case SDL_SCANCODE_J:              return NEXTKEY_j;
			case SDL_SCANCODE_K:              return NEXTKEY_k;
			case SDL_SCANCODE_L:              return NEXTKEY_l;
			case SDL_SCANCODE_SEMICOLON:      return NEXTKEY_SEMICOLON;
			case SDL_SCANCODE_APOSTROPHE:     return NEXTKEY_QUOTE;
			case SDL_SCANCODE_RETURN:         return NEXTKEY_RETURN;
				
			case SDL_SCANCODE_NONUSBACKSLASH: return NEXTKEY_BACKSLASH;
			case SDL_SCANCODE_Z:              return NEXTKEY_z;
			case SDL_SCANCODE_X:              return NEXTKEY_x;
			case SDL_SCANCODE_C:              return NEXTKEY_c;
			case SDL_SCANCODE_V:              return NEXTKEY_v;
			case SDL_SCANCODE_B:              return NEXTKEY_b;
			case SDL_SCANCODE_N:              return NEXTKEY_n;
			case SDL_SCANCODE_M:              return NEXTKEY_m;
			case SDL_SCANCODE_COMMA:          return NEXTKEY_COMMA;
			case SDL_SCANCODE_PERIOD:         return NEXTKEY_PERIOD;
			case SDL_SCANCODE_SLASH:          return NEXTKEY_SLASH;
			case SDL_SCANCODE_SPACE:          return NEXTKEY_SPACE;
				
			case SDL_SCANCODE_NUMLOCKCLEAR:   return NEXTKEY_BACKQUOTE;
			case SDL_SCANCODE_KP_EQUALS:      return NEXTKEY_KEYPAD_EQUALS;
			case SDL_SCANCODE_KP_DIVIDE:      return NEXTKEY_KEYPAD_DIVIDE;
			case SDL_SCANCODE_KP_MULTIPLY:    return NEXTKEY_KEYPAD_MULTIPLY;
			case SDL_SCANCODE_KP_7:           return NEXTKEY_KEYPAD_7;
			case SDL_SCANCODE_KP_8:           return NEXTKEY_KEYPAD_8;
			case SDL_SCANCODE_KP_9:           return NEXTKEY_KEYPAD_9;
			case SDL_SCANCODE_KP_MINUS:       return NEXTKEY_KEYPAD_MINUS;
			case SDL_SCANCODE_KP_4:           return NEXTKEY_KEYPAD_4;
			case SDL_SCANCODE_KP_5:           return NEXTKEY_KEYPAD_5;
			case SDL_SCANCODE_KP_6:           return NEXTKEY_KEYPAD_6;
			case SDL_SCANCODE_KP_PLUS:        return NEXTKEY_KEYPAD_PLUS;
			case SDL_SCANCODE_KP_1:           return NEXTKEY_KEYPAD_1;
			case SDL_SCANCODE_KP_2:           return NEXTKEY_KEYPAD_2;
			case SDL_SCANCODE_KP_3:           return NEXTKEY_KEYPAD_3;
			case SDL_SCANCODE_KP_0:           return NEXTKEY_KEYPAD_0;
			case SDL_SCANCODE_KP_PERIOD:      return NEXTKEY_KEYPAD_PERIOD;
			case SDL_SCANCODE_KP_ENTER:       return NEXTKEY_KEYPAD_ENTER;
				
			case SDL_SCANCODE_LEFT:           return NEXTKEY_LEFT_ARROW;
			case SDL_SCANCODE_RIGHT:          return NEXTKEY_RIGHT_ARROW;
			case SDL_SCANCODE_UP:             return NEXTKEY_UP_ARROW;
			case SDL_SCANCODE_DOWN:           return NEXTKEY_DOWN_ARROW;
				
				/* Special keys */
			case SDL_SCANCODE_F10:
			case SDL_SCANCODE_DELETE:         return NEXTKEY_POWER;
			case SDL_SCANCODE_F5:
			case SDL_SCANCODE_END:            return NEXTKEY_VOLUME_DOWN;
			case SDL_SCANCODE_F6:
			case SDL_SCANCODE_HOME:           return NEXTKEY_VOLUME_UP;
			case SDL_SCANCODE_F1:
			case SDL_SCANCODE_PAGEDOWN:       return NEXTKEY_BRGHTNESS_DOWN;
			case SDL_SCANCODE_F2:
			case SDL_SCANCODE_PAGEUP:         return NEXTKEY_BRIGHTNESS_UP;
				
			default:                          return NEXTKEY_NONE;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * This function translates the key symbols provided by SDL to NeXT
 * scancode values.
 */
static uint8_t Keymap_GetKeyFromSymbol(SDL_Keycode sdlkey)
{
	Log_Printf(LOG_KEYMAP_LEVEL, "[Keymap] Symkey: %s\n", SDL_GetKeyName(sdlkey));

	if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		switch (sdlkey) {
			case SDLK_BACKSLASH:              return APPLEKEY_BACKSLASH;
			case SDLK_RIGHTBRACKET:           return APPLEKEY_CLOSEBRACKET;
			case SDLK_LEFTBRACKET:            return APPLEKEY_OPENBRACKET;
			case SDLK_LESS:                   return APPLEKEY_LESS;
			case SDLK_I:                      return APPLEKEY_i;
			case SDLK_O:                      return APPLEKEY_o;
			case SDLK_P:                      return APPLEKEY_p;
			case SDLK_LEFT:                   return APPLEKEY_LEFT_ARROW;
			case SDLK_KP_0:                   return APPLEKEY_KEYPAD_0;
			case SDLK_KP_PERIOD:              return APPLEKEY_KEYPAD_PERIOD;
			case SDLK_KP_ENTER:               return APPLEKEY_KEYPAD_ENTER;
			case SDLK_DOWN:                   return APPLEKEY_DOWN_ARROW;
			case SDLK_RIGHT:                  return APPLEKEY_RIGHT_ARROW;
			case SDLK_KP_1:                   return APPLEKEY_KEYPAD_1;
			case SDLK_KP_4:                   return APPLEKEY_KEYPAD_4;
			case SDLK_KP_6:                   return APPLEKEY_KEYPAD_6;
			case SDLK_KP_3:                   return APPLEKEY_KEYPAD_3;
			case SDLK_KP_PLUS:                return APPLEKEY_KEYPAD_PLUS;
			case SDLK_UP:                     return APPLEKEY_UP_ARROW;
			case SDLK_KP_2:                   return APPLEKEY_KEYPAD_2;
			case SDLK_KP_5:                   return APPLEKEY_KEYPAD_5;
			case SDLK_BACKSPACE:              return APPLEKEY_DELETE;
			case SDLK_EQUALS:                 return APPLEKEY_EQUALS;
			case SDLK_MINUS:                  return APPLEKEY_MINUS;
			case SDLK_8:                      return APPLEKEY_8;
			case SDLK_9:                      return APPLEKEY_9;
			case SDLK_0:                      return APPLEKEY_0;
			case SDLK_KP_7:                   return APPLEKEY_KEYPAD_7;
			case SDLK_KP_8:                   return APPLEKEY_KEYPAD_8;
			case SDLK_KP_9:                   return APPLEKEY_KEYPAD_9;
			case SDLK_KP_MINUS:               return APPLEKEY_KEYPAD_MINUS;
			case SDLK_KP_MULTIPLY:            return APPLEKEY_KEYPAD_MULTIPLY;
			case SDLK_NUMLOCKCLEAR:           return APPLEKEY_BACKQUOTE;
			case SDLK_GRAVE:                  return APPLEKEY_BACKQUOTE;
			case SDLK_KP_EQUALS:              return APPLEKEY_KEYPAD_EQUALS;
			case SDLK_KP_DIVIDE:              return APPLEKEY_KEYPAD_DIVIDE;
			case SDLK_RETURN:                 return APPLEKEY_RETURN;
			case SDLK_APOSTROPHE:             return APPLEKEY_QUOTE;
			case SDLK_SEMICOLON:              return APPLEKEY_SEMICOLON;
			case SDLK_L:                      return APPLEKEY_l;
			case SDLK_COMMA:                  return APPLEKEY_COMMA;
			case SDLK_PERIOD:                 return APPLEKEY_PERIOD;
			case SDLK_SLASH:                  return APPLEKEY_SLASH;
			case SDLK_Z:                      return APPLEKEY_z;
			case SDLK_X:                      return APPLEKEY_x;
			case SDLK_C:                      return APPLEKEY_c;
			case SDLK_V:                      return APPLEKEY_v;
			case SDLK_B:                      return APPLEKEY_b;
			case SDLK_M:                      return APPLEKEY_m;
			case SDLK_N:                      return APPLEKEY_n;
			case SDLK_SPACE:                  return APPLEKEY_SPACE;
			case SDLK_A:                      return APPLEKEY_a;
			case SDLK_S:                      return APPLEKEY_s;
			case SDLK_D:                      return APPLEKEY_d;
			case SDLK_F:                      return APPLEKEY_f;
			case SDLK_G:                      return APPLEKEY_g;
			case SDLK_K:                      return APPLEKEY_k;
			case SDLK_J:                      return APPLEKEY_j;
			case SDLK_H:                      return APPLEKEY_h;
			case SDLK_TAB:                    return APPLEKEY_TAB;
			case SDLK_Q:                      return APPLEKEY_q;
			case SDLK_W:                      return APPLEKEY_w;
			case SDLK_E:                      return APPLEKEY_e;
			case SDLK_R:                      return APPLEKEY_r;
			case SDLK_U:                      return APPLEKEY_u;
			case SDLK_Y:                      return APPLEKEY_y;
			case SDLK_T:                      return APPLEKEY_t;
			case SDLK_ESCAPE:                 return APPLEKEY_ESC;
			case SDLK_1:                      return APPLEKEY_1;
			case SDLK_2:                      return APPLEKEY_2;
			case SDLK_3:                      return APPLEKEY_3;
			case SDLK_4:                      return APPLEKEY_4;
			case SDLK_7:                      return APPLEKEY_7;
			case SDLK_6:                      return APPLEKEY_6;
			case SDLK_5:                      return APPLEKEY_5;
				
				/* Modifier keys */
			case SDLK_RSHIFT:
			case SDLK_LSHIFT:                 return APPLEKEY_SHIFT_LEFT;
			case SDLK_RGUI:
			case SDLK_LGUI:                   return ConfigureParams.Keyboard.bSwapCmdAlt?APPLEKEY_OPTION_LEFT:APPLEKEY_APPLE_LEFT;
			case SDLK_MENU:
			case SDLK_RCTRL:                  return APPLEKEY_HELP;
			case SDLK_LCTRL:                  return APPLEKEY_CTL_LEFT;
			case SDLK_RALT:
			case SDLK_LALT:                   return ConfigureParams.Keyboard.bSwapCmdAlt?APPLEKEY_APPLE_LEFT:APPLEKEY_OPTION_LEFT;
			case SDLK_CAPSLOCK:               return APPLEKEY_CAPS_LOCK;
				
				/* Special Keys */
			case SDLK_F10:
			case SDLK_DELETE:                 return APPLEKEY_POWER;
			case SDLK_F5:
			case SDLK_END:                    return APPLEKEY_VOLUME_DOWN;
			case SDLK_F6:
			case SDLK_HOME:                   return APPLEKEY_VOLUME_UP;
			case SDLK_F1:
			case SDLK_PAGEDOWN:               return APPLEKEY_BRIGHTNESS_DOWN;
			case SDLK_F2:
			case SDLK_PAGEUP:                 return APPLEKEY_BRIGHTNESS_UP;
				
			default:                          return 0xff;
		}
	} else {
		switch (sdlkey) {
			case SDLK_BACKSLASH:              return NEXTKEY_BACKSLASH;
			case SDLK_RIGHTBRACKET:           return NEXTKEY_CLOSEBRACKET;
			case SDLK_LEFTBRACKET:            return NEXTKEY_OPENBRACKET;
			case SDLK_I:                      return NEXTKEY_i;
			case SDLK_O:                      return NEXTKEY_o;
			case SDLK_P:                      return NEXTKEY_p;
			case SDLK_LEFT:                   return NEXTKEY_LEFT_ARROW;
			case SDLK_KP_0:                   return NEXTKEY_KEYPAD_0;
			case SDLK_KP_PERIOD:              return NEXTKEY_KEYPAD_PERIOD;
			case SDLK_KP_ENTER:               return NEXTKEY_KEYPAD_ENTER;
			case SDLK_DOWN:                   return NEXTKEY_DOWN_ARROW;
			case SDLK_RIGHT:                  return NEXTKEY_RIGHT_ARROW;
			case SDLK_KP_1:                   return NEXTKEY_KEYPAD_1;
			case SDLK_KP_4:                   return NEXTKEY_KEYPAD_4;
			case SDLK_KP_6:                   return NEXTKEY_KEYPAD_6;
			case SDLK_KP_3:                   return NEXTKEY_KEYPAD_3;
			case SDLK_KP_PLUS:                return NEXTKEY_KEYPAD_PLUS;
			case SDLK_UP:                     return NEXTKEY_UP_ARROW;
			case SDLK_KP_2:                   return NEXTKEY_KEYPAD_2;
			case SDLK_KP_5:                   return NEXTKEY_KEYPAD_5;
			case SDLK_BACKSPACE:              return NEXTKEY_DELETE;
			case SDLK_EQUALS:                 return NEXTKEY_EQUALS;
			case SDLK_MINUS:                  return NEXTKEY_MINUS;
			case SDLK_8:                      return NEXTKEY_8;
			case SDLK_9:                      return NEXTKEY_9;
			case SDLK_0:                      return NEXTKEY_0;
			case SDLK_KP_7:                   return NEXTKEY_KEYPAD_7;
			case SDLK_KP_8:                   return NEXTKEY_KEYPAD_8;
			case SDLK_KP_9:                   return NEXTKEY_KEYPAD_9;
			case SDLK_KP_MINUS:               return NEXTKEY_KEYPAD_MINUS;
			case SDLK_KP_MULTIPLY:            return NEXTKEY_KEYPAD_MULTIPLY;
			case SDLK_NUMLOCKCLEAR:           return NEXTKEY_BACKQUOTE;
			case SDLK_GRAVE:                  return NEXTKEY_BACKQUOTE;
			case SDLK_KP_EQUALS:              return NEXTKEY_KEYPAD_EQUALS;
			case SDLK_KP_DIVIDE:              return NEXTKEY_KEYPAD_DIVIDE;
			case SDLK_RETURN:                 return NEXTKEY_RETURN;
			case SDLK_APOSTROPHE:             return NEXTKEY_QUOTE;
			case SDLK_SEMICOLON:              return NEXTKEY_SEMICOLON;
			case SDLK_L:                      return NEXTKEY_l;
			case SDLK_COMMA:                  return NEXTKEY_COMMA;
			case SDLK_PERIOD:                 return NEXTKEY_PERIOD;
			case SDLK_SLASH:                  return NEXTKEY_SLASH;
			case SDLK_Z:                      return NEXTKEY_z;
			case SDLK_X:                      return NEXTKEY_x;
			case SDLK_C:                      return NEXTKEY_c;
			case SDLK_V:                      return NEXTKEY_v;
			case SDLK_B:                      return NEXTKEY_b;
			case SDLK_M:                      return NEXTKEY_m;
			case SDLK_N:                      return NEXTKEY_n;
			case SDLK_SPACE:                  return NEXTKEY_SPACE;
			case SDLK_A:                      return NEXTKEY_a;
			case SDLK_S:                      return NEXTKEY_s;
			case SDLK_D:                      return NEXTKEY_d;
			case SDLK_F:                      return NEXTKEY_f;
			case SDLK_G:                      return NEXTKEY_g;
			case SDLK_K:                      return NEXTKEY_k;
			case SDLK_J:                      return NEXTKEY_j;
			case SDLK_H:                      return NEXTKEY_h;
			case SDLK_TAB:                    return NEXTKEY_TAB;
			case SDLK_Q:                      return NEXTKEY_q;
			case SDLK_W:                      return NEXTKEY_w;
			case SDLK_E:                      return NEXTKEY_e;
			case SDLK_R:                      return NEXTKEY_r;
			case SDLK_U:                      return NEXTKEY_u;
			case SDLK_Y:                      return NEXTKEY_y;
			case SDLK_T:                      return NEXTKEY_t;
			case SDLK_ESCAPE:                 return NEXTKEY_ESC;
			case SDLK_1:                      return NEXTKEY_1;
			case SDLK_2:                      return NEXTKEY_2;
			case SDLK_3:                      return NEXTKEY_3;
			case SDLK_4:                      return NEXTKEY_4;
			case SDLK_7:                      return NEXTKEY_7;
			case SDLK_6:                      return NEXTKEY_6;
			case SDLK_5:                      return NEXTKEY_5;
				
				/* Special Keys */
			case SDLK_F10:
			case SDLK_DELETE:                 return NEXTKEY_POWER;
			case SDLK_F5:
			case SDLK_END:                    return NEXTKEY_VOLUME_DOWN;
			case SDLK_F6:
			case SDLK_HOME:                   return NEXTKEY_VOLUME_UP;
			case SDLK_F1:
			case SDLK_PAGEDOWN:               return NEXTKEY_BRGHTNESS_DOWN;
			case SDLK_F2:
			case SDLK_PAGEUP:                 return NEXTKEY_BRIGHTNESS_UP;
				
			default:                          return NEXTKEY_NONE;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * This functions translates the modifieres provided by SDL to NeXT 
 * modifier bits.
 */
static uint8_t Keymap_GetModifiers(uint16_t mod)
{
	uint8_t modifiers = 0;

	if (mod & SDL_KMOD_CTRL) {
		modifiers |= NEXTKEY_MOD_META;
	}
	if (mod & SDL_KMOD_LSHIFT) {
		modifiers |= NEXTKEY_MOD_LSHIFT;
	}
	if (mod & SDL_KMOD_RSHIFT) {
		modifiers |= NEXTKEY_MOD_RSHIFT;
	}
	if (mod & SDL_KMOD_LGUI) {
		modifiers |= ConfigureParams.Keyboard.bSwapCmdAlt?NEXTKEY_MOD_LALT:NEXTKEY_MOD_LCTRL;
	}
	if (mod & SDL_KMOD_RGUI) {
		modifiers |= ConfigureParams.Keyboard.bSwapCmdAlt?NEXTKEY_MOD_RALT:NEXTKEY_MOD_RCTRL;
	}
	if (mod & SDL_KMOD_LALT) {
		modifiers |= ConfigureParams.Keyboard.bSwapCmdAlt?NEXTKEY_MOD_LCTRL:NEXTKEY_MOD_LALT;
	}
	if (mod & SDL_KMOD_RALT) {
		modifiers |= ConfigureParams.Keyboard.bSwapCmdAlt?NEXTKEY_MOD_RCTRL:NEXTKEY_MOD_RALT;
	}
	if (mod & SDL_KMOD_CAPS) {
		modifiers |= NEXTKEY_MOD_LSHIFT;
	}
	return modifiers;
}


/*-----------------------------------------------------------------------*/
/**
 * Mouse wheel mapped to cursor keys
 */
static void post_key_event(int sym, int scan)
{
	SDL_Event sdlevent;
	sdlevent.type = SDL_EVENT_KEY_DOWN;
	sdlevent.key.key      = sym;
	sdlevent.key.scancode = scan;
	sdlevent.key.mod      = SDL_KMOD_NONE;
	SDL_PushEvent(&sdlevent);
	sdlevent.type = SDL_EVENT_KEY_UP;
	sdlevent.key.key      = sym;
	sdlevent.key.scancode = scan;
	sdlevent.key.mod      = SDL_KMOD_NONE;
	SDL_PushEvent(&sdlevent);
}

void Keymap_MouseWheel(const SDL_MouseWheelEvent *sdlwheel)
{
	int32_t x, y;
	
	if (ConfigureParams.Mouse.bEnableMapToKey) {
		x = sdlwheel->x;
		y = sdlwheel->y;
		
		if (sdlwheel->direction == SDL_MOUSEWHEEL_FLIPPED) {
			x = -x;
			y = -y;
		}
		
		if      (x < 0) post_key_event(SDLK_LEFT,  SDL_SCANCODE_LEFT);
		else if (x > 0) post_key_event(SDLK_RIGHT, SDL_SCANCODE_RIGHT);
		
		if      (y < 0) post_key_event(SDLK_DOWN,  SDL_SCANCODE_DOWN);
		else if (y > 0) post_key_event(SDLK_UP,    SDL_SCANCODE_UP);
		
		Log_Printf(LOG_KEYMAP_LEVEL, "[Keymap] Scrolling x = %d, y = %d\n", x, y);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User pressed a key down
 */
void Keymap_KeyDown(const SDL_KeyboardEvent *sdlkey)
{
	uint8_t key;

	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_SYMBOLIC) {
		key = Keymap_GetKeyFromSymbol(sdlkey->key);
	} else {
		key = Keymap_GetKeyFromScancode(sdlkey->scancode);
	}

	Log_Printf(LOG_KEYMAP_LEVEL, "[Keymap] Press Keycode: $%02x\n", key);

	if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		adb_keydown(key);
	} else {
		kms_keydown(Keymap_GetModifiers(sdlkey->mod), key);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User released a key
 */
void Keymap_KeyUp(const SDL_KeyboardEvent *sdlkey)
{
	uint8_t key;

	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_SYMBOLIC) {
		key = Keymap_GetKeyFromSymbol(sdlkey->key);
	} else {
		key = Keymap_GetKeyFromScancode(sdlkey->scancode);
	}

	Log_Printf(LOG_KEYMAP_LEVEL, "[Keymap] Release Keycode: $%02x\n", key);

	if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		adb_keyup(key);
	} else {
		kms_keyup(Keymap_GetModifiers(sdlkey->mod), key);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User moved the mouse
 */
void Keymap_MouseMove(const SDL_MouseMotionEvent *sdlmotion)
{
	if (ConfigureParams.Tablet.nTabletType && bTabletEnabled) {
		tablet_pen_move(sdlmotion->xrel, sdlmotion->yrel, sdlmotion->x, sdlmotion->y);
	} else if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		adb_mouse_move(sdlmotion->xrel, sdlmotion->yrel);
	} else {
		kms_mouse_move(sdlmotion->xrel, sdlmotion->yrel);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User pressed a mouse button
 */
void Keymap_MouseDown(bool left)
{
	if (ConfigureParams.Tablet.nTabletType && bTabletEnabled) {
		tablet_pen_button(left, true);
	} else if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		adb_mouse_button(left, true);
	} else {
		kms_mouse_button(left, true);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User released a mouse button
 */
void Keymap_MouseUp(bool left)
{
	if (ConfigureParams.Tablet.nTabletType && bTabletEnabled) {
		tablet_pen_button(left, false);
	} else if (ConfigureParams.System.bADB && ConfigureParams.System.bTurbo) {
		adb_mouse_button(left, false);
	} else {
		kms_mouse_button(left, false);
	}
}


/**
 * Maps a key name to its SDL keycode
 */
int Keymap_GetKeyFromName(const char *name)
{
	return SDL_GetKeyFromName(name);
}


/**
 * Maps an SDL keycode to a name
 */
const char *Keymap_GetKeyName(int keycode)
{
	if (!keycode)
		return "";

	return SDL_GetKeyName(keycode);
}
