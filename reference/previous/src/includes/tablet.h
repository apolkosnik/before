/*
  Previous - tablet.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_TABLET_H
#define PREV_TABLET_H

extern void tablet_receive(uint8_t val);
extern void tablet_pen_move(int xrel, int yrel, int x, int y);
extern void tablet_pen_button(int tip, int pressed);

extern void Tablet_IO_Handler(void);
extern void Tablet_Reset(void);

extern bool bTabletEnabled;

#endif /* PREV_TABLET_H */
