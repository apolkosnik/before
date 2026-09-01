/*
 * Routing information protocol
 * 
 * Copyright (c) 2021 Andreas Grabher
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
#include <unistd.h>
#include "ctl.h"

#if 0 /* unused */
static void rip_request(void)
{
    struct mbuf *m;
    struct rip_t *rrp;
    struct rtentry_t *rt;
    struct sockaddr_in saddr, daddr;
    
    if ((m = m_get()) == NULL)
        return;
    m->m_data += if_maxlinkhdr;
    rrp = (struct rip_t *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);
    memset(rrp, 0, sizeof(struct rip_t));
    
    saddr.sin_addr        = alias_addr;
    saddr.sin_port        = htons(RIP_ROUTER);
    daddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    daddr.sin_port        = htons(RIP_ROUTER);
    
    rrp->rp_cmd  = RIP_REQUEST;
    rrp->rp_vers = RIP_VERSION;
    
    rt = &rrp->rp_rt[0];
    
    /* Send invalid routing table entry */
    rt->af             = htons(AF_UNSPEC);
    rt->rt_addr.s_addr = htonl(INADDR_ANY);
    rt->metric         = htonl(RIP_HOP_UNREACH);
    
    m->m_len = sizeof(struct rip_t) -
    sizeof(struct ip) - sizeof(struct udphdr);
    
    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}
#endif

void rip_broadcast(void)
{
    struct mbuf *m;
    struct rip_t *rrp;
    struct rtentry_t *rt;
    struct sockaddr_in saddr, daddr;
    
    if ((m = m_get()) == NULL)
        return;
    m->m_data += if_maxlinkhdr;
    rrp = (struct rip_t *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);
    memset(rrp, 0, sizeof(struct rip_t));
    
    saddr.sin_addr        = alias_addr;
    saddr.sin_port        = htons(RIP_ROUTER);
    daddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    daddr.sin_port        = htons(RIP_ROUTER);
    
    rrp->rp_cmd  = RIP_REPLY;
    rrp->rp_vers = RIP_VERSION;
    
    rt = &rrp->rp_rt[0];
    
    /* Send default routing table entry */
    rt->af             = htons(AF_INET);
    rt->rt_addr.s_addr = htonl(INADDR_ANY);
    rt->metric         = htonl(RIP_HOP_DEFAULT);
    
    m->m_len = sizeof(struct rip_t) -
    sizeof(struct ip) - sizeof(struct udphdr);
    
    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}

static void rip_reply(struct rip_t *rp)
{
    struct mbuf *m;
    struct rip_t *rrp;
    struct rtentry_t *rt;
    struct ip *ip;
    struct sockaddr_in saddr, daddr;

    if ((m = m_get()) == NULL)
        return;
    m->m_data += if_maxlinkhdr;
    rrp = (struct rip_t *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);
    memset(rrp, 0, sizeof(struct rip_t));

    ip = &rp->ip;

    saddr.sin_addr = alias_addr;
    saddr.sin_port = htons(RIP_ROUTER);
    daddr.sin_addr = ip->ip_src;
    daddr.sin_port = htons(RIP_ROUTER);

    rrp->rp_cmd  = RIP_REPLY;
    rrp->rp_vers = RIP_VERSION;

    rt = &rrp->rp_rt[0];

    /* Send default routing table entry */
    rt->af             = htons(AF_INET);
    rt->rt_addr.s_addr = htonl(INADDR_ANY);
    rt->metric         = htonl(RIP_HOP_DEFAULT);

    m->m_len = sizeof(struct rip_t) -
    sizeof(struct ip) - sizeof(struct udphdr);

    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}

void rip_input(struct mbuf *m)
{
    struct rip_t *rp = mtod(m, struct rip_t *);

    if (rp->rp_cmd == RIP_REQUEST && rp->rp_vers == RIP_VERSION) {
        rip_reply(rp);
    }
}
