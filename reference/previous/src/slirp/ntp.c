/*
 * Network time protocol
 * 
 * Copyright (c) 2023 Andreas Grabher
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <slirp.h>

#define    JAN_1970    2208988800

static void ntp_get_time(uint32_t* t) {
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    t[0] = htonl((uint32_t)(JAN_1970 + tv.tv_sec));
    t[1] = htonl((uint32_t)((float)tv.tv_usec * 4294.967295));
}

static void ntp_reply(struct ntp_t *np)
{
    struct mbuf *m;
    struct ntp_t *rnp;
    struct ip *ip;
    struct udphdr *udp;
    struct sockaddr_in saddr, daddr;

    uint32_t now[2];

    ntp_get_time(now);

    if ((m = m_get()) == NULL)
        return;
    m->m_data += if_maxlinkhdr;
    rnp = (struct ntp_t *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);
    memset(rnp, 0, sizeof(struct ntp_t));

    ip  = &np->ip;
    udp = &np->udp;

    saddr.sin_addr = ip->ip_dst;
    saddr.sin_port = udp->uh_dport;
    daddr.sin_addr = ip->ip_src;
    daddr.sin_port = udp->uh_sport;

    rnp->status    = (NTP_LEAP << 6) | (NTP_VERSION << 3) | NTP_MODE_SEVER;
    rnp->stratum   = NTP_STRATUM;
    rnp->poll      = NTP_POLL;
    rnp->precision = NTP_PRECISION;

    rnp->refid = htonl(NTP_IDENTITY);

    rnp->distance[0] = htons(0x0000);
    rnp->distance[1] = htons(0x0000);

    rnp->drift[0] = htons(0x0000);
    rnp->drift[1] = htons(0x0008);

    rnp->reftime[0] = htonl(ntohl(now[0])-4); /* 4 seconds and some  */
    rnp->reftime[1] = ~(now[1]^rnp->xmt[1]);  /* random fraction ago */

    rnp->org[0] = np->xmt[0];
    rnp->org[1] = np->xmt[1];

    rnp->rec[0] = now[0];
    rnp->rec[1] = now[1];

    ntp_get_time(now);

    rnp->xmt[0] = now[0];
    rnp->xmt[1] = now[1];

    m->m_len = sizeof(struct ntp_t) - sizeof(struct ip) - sizeof(struct udphdr);

    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}

void ntp_input(struct mbuf *m)
{
    struct ntp_t *np = mtod(m, struct ntp_t *);

    if (np->ip.ip_dst.s_addr == alias_addr.s_addr && ((np->status>>3)&7) == NTP_VERSION) {
        ntp_reply(np);
    }
}
