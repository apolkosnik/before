/*
  Previous - enet_slirp.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_ENET_SLIRP_H
#define PREV_ENET_SLIRP_H

extern void enet_slirp_queue_poll(void);
extern void enet_slirp_input(uint8_t *pkt, int pkt_len);
extern void enet_slirp_stop(void);
extern void enet_slirp_start(uint8_t *mac);
extern void enet_slirp_uninit(void);

#endif /* PREV_ENET_SLIRP_H */
