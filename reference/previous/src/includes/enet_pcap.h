/*
  Previous - enet_pcap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_ENET_PCAP_H
#define PREV_ENET_PCAP_H

extern void enet_pcap_queue_poll(void);
extern void enet_pcap_input(uint8_t *pkt, int pkt_len);
extern void enet_pcap_stop(void);
extern void enet_pcap_start(uint8_t *mac);
extern void enet_pcap_uninit(void);

#endif /* PREV_ENET_PCAP_H */
