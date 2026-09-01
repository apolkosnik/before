/*
  Previous - keymap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_KEYMAP_H
#define PREV_KEYMAP_H

/* These functions must be provided through host or cross-platform API. */
extern void Keymap_Init(void);
extern void Keymap_InitShortcutDefaultKeys(void);

extern int Keymap_GetKeyFromName(const char *name);
extern const char *Keymap_GetKeyName(int keycode);

#endif /* PREV_KEYMAP_H */
