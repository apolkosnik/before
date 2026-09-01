/* Domain Name System */

#ifndef _VDNS_H_
#define _VDNS_H_

#define PORT_DNS          53

#define PROG_DNS          200053

#include <stdio.h>

#include "udpsocket.h"
#include "mbuf.h"

int  vdns_match(struct mbuf *m, uint32_t addr, int dport);
void vdns_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO);
void vdns_udp_map_from_local_port(uint16_t port, struct in_addr* saddrNBO, uint16_t* sin_portNBO);

void vdns_add_rec(const char* name, uint32_t addr);
void vdns_remove_rec(uint32_t addr);

void vdns_init(void);
void vdns_uninit(void);

#endif /* _DNS_H_ */
