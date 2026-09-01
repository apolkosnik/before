/*
 * QEMU BOOTP/DHCP server
 * 
 * Copyright (c) 2004 Fabrice Bellard
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

#define NO_NEW_ADDR 1 /* required for Previous */

/* XXX: only DHCP is supported */

#define NB_ADDR 16
#define LEASE_TIME (24 * 3600)

typedef struct {
    uint8_t allocated;
    uint8_t macaddr[6];
} BOOTPClient;

BOOTPClient bootp_clients[NB_ADDR];

static const uint8_t rfc1533_cookie[] = { RFC1533_COOKIE };
static const uint8_t magic_next[]  = {'N','e','X','T'};
static const char    kernel_next[] = "mach";
static const char    tftp_root[]   = "/private/tftpboot/";
static const char    root_path[]   = "/";

static char hostname[NAME_HOST_MAX];

#ifndef NO_NEW_ADDR
static BOOTPClient *get_new_addr(struct in_addr *paddr)
{
    BOOTPClient *bc;
    int i;

    for(i = 0; i < NB_ADDR; i++) {
        if (!bootp_clients[i].allocated)
            goto found;
    }
    return NULL;
 found:
    bc = &bootp_clients[i];
    bc->allocated = 1;
    paddr->s_addr = htonl(ntohl(special_addr.s_addr) | (i + CTL_HOST));
    return bc;
}

static BOOTPClient *find_addr(struct in_addr *paddr, const uint8_t *macaddr)
{
    BOOTPClient *bc;
    int i;

    for(i = 0; i < NB_ADDR; i++) {
        if (!memcmp(macaddr, bootp_clients[i].macaddr, 6))
            goto found;
    }
    return NULL;
 found:
    bc = &bootp_clients[i];
    bc->allocated = 1;
    paddr->s_addr = htonl(ntohl(special_addr.s_addr) | (i + CTL_HOST));
    return bc;
}
#endif

static void dhcp_decode(const uint8_t *buf, int size,
                        int *pmsg_type)
{
    const uint8_t *p, *p_end;
    int len, tag;

    *pmsg_type = 0;    

    p = buf;
    p_end = buf + size;
    if (size < 5)
        return;
    if (memcmp(p, rfc1533_cookie, 4) != 0)
        return;
    p += 4;
    while (p < p_end) {
        tag = p[0];
        if (tag == RFC1533_PAD) {
            p++; 
        } else if (tag == RFC1533_END) {
            break;
        } else {
            p++;
            if (p >= p_end)
                break;
            len = *p++;

            switch(tag) {
            case RFC2132_MSG_TYPE:
                if (len >= 1)
                    *pmsg_type = p[0];
                break;
            default:
                break;
            }
            p += len;
        }
    }
}

static void bootp_reply(struct bootp_t *bp)
{
#ifndef NO_NEW_ADDR
    BOOTPClient *bc;
#endif
    struct mbuf *m;
    struct bootp_t *rbp;
    struct sockaddr_in saddr, daddr;
    struct in_addr dns_addr;
    uint32_t val;
    int dhcp_msg_type;
    uint8_t *q;

    strcpy(hostname, NAME_HOST);
    
    /* extract exact DHCP msg type */
    dhcp_decode(bp->bp_vend, DHCP_OPT_LEN, &dhcp_msg_type);
    
    if (dhcp_msg_type == 0)
        dhcp_msg_type = DHCPREQUEST; /* Force reply for old BOOTP clients */
        
    if (dhcp_msg_type != DHCPDISCOVER && 
        dhcp_msg_type != DHCPREQUEST)
        return;
    /* XXX: this is a hack to get the client mac address */
    /* memcpy(client_ethaddr, bp->bp_hwaddr, 6); */
    
    if ((m = m_get()) == NULL)
        return;
    m->m_data += if_maxlinkhdr;
    rbp = (struct bootp_t *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);
    memset(rbp, 0, sizeof(struct bootp_t));

#ifndef NO_NEW_ADDR
    if (dhcp_msg_type == DHCPDISCOVER) {
    new_addr:
        bc = get_new_addr(&daddr.sin_addr);
        if (!bc)
            return;
        memcpy(bc->macaddr, client_ethaddr, 6);
    } else {
        bc = find_addr(&daddr.sin_addr, bp->bp_hwaddr);
        if (!bc) {
            /* if never assigned, behaves as if it was already
               assigned (windows fix because it remembers its address) */
            goto new_addr;
        }
    }
#else
    /* XXX: always assign same IP address for Previous */
    daddr.sin_addr.s_addr = special_addr.s_addr | htonl(CTL_HOST);
#endif

    saddr.sin_addr.s_addr = special_addr.s_addr | htonl(CTL_NFSD);
    saddr.sin_port        = htons(BOOTP_SERVER);
    daddr.sin_port        = htons(BOOTP_CLIENT);

    rbp->bp_op    = BOOTP_REPLY;
    rbp->bp_xid   = bp->bp_xid;
    rbp->bp_htype = 1;
    rbp->bp_hlen  = 6;
    memcpy(rbp->bp_hwaddr, bp->bp_hwaddr, 6);

    rbp->bp_yiaddr        = daddr.sin_addr; /* Client IP address */
    rbp->bp_siaddr        = saddr.sin_addr; /* Server IP address */
    rbp->bp_giaddr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_GATEWAY); /* Gateway IP address */
    strcpy((char*)rbp->bp_sname, NAME_NFSD); /* Server namne */

    char path[TFTP_FILENAME_MAX];
    
    snprintf(path, sizeof(path), "%s%s", tftp_root, kernel_next);
    
    if (memcmp(bp->bp_file, path, strlen(path)+1)) {
        if (bp->bp_file[0]) {
            snprintf(path, sizeof(path), "%s%s", tftp_root, bp->bp_file);
        }
    }
    memcpy(rbp->bp_file, path, strlen(path)+1);

    q = rbp->bp_vend;

    if (memcmp(bp->bp_vend, magic_next, 4) == 0) { /* NeXT */
        memcpy(q, magic_next, 4);
        q += 4;
        *q++ = 1; /* Version */
        *q++ = 0; /* Opcode */
        *q++ = 0; /* Transaction ID */
        memset(q, 0, 56); /* Text */
        q += 56;
        *q++ = 0; /* Null terminator */
        m->m_len = sizeof(struct bootp_t) -
        DHCP_OPT_LEN + BOOTP_VENDOR_LEN -
        sizeof(struct ip) - sizeof(struct udphdr);
    } else if (memcmp(bp->bp_vend, rfc1533_cookie, 4) == 0) { /* DHCP */
        memcpy(q, rfc1533_cookie, 4);
        q += 4;
        if (dhcp_msg_type == DHCPDISCOVER) {
            *q++ = RFC2132_MSG_TYPE;
            *q++ = 1;
            *q++ = DHCPOFFER;
        } else if (dhcp_msg_type == DHCPREQUEST) {
            *q++ = RFC2132_MSG_TYPE;
            *q++ = 1;
            *q++ = DHCPACK;
        }
        
        if (dhcp_msg_type == DHCPDISCOVER ||
            dhcp_msg_type == DHCPREQUEST) {
            *q++ = RFC2132_SRV_ID;
            *q++ = 4;
            memcpy(q, &saddr.sin_addr, 4);
            q += 4;
            
            *q++ = RFC1533_NETMASK;
            *q++ = 4;
            val = htonl(CTL_NET_MASK);
            memcpy(q, &val, 4);
            q += 4;
            
            *q++ = RFC1533_GATEWAY;
            *q++ = 4;
            val = htonl(ntohl(special_addr.s_addr) | CTL_GATEWAY);
            memcpy(q, &val, 4);
            q += 4;
            
            *q++ = RFC1533_DNS;
            *q++ = 4;
            dns_addr.s_addr = htonl(ntohl(special_addr.s_addr) | CTL_DNS);
            memcpy(q, &dns_addr, 4);
            q += 4;
            
            *q++ = RFC2132_LEASE_TIME;
            *q++ = 4;
            val = htonl(LEASE_TIME);
            memcpy(q, &val, 4);
            q += 4;
            
            val = strlen(hostname);
            *q++ = RFC1533_HOSTNAME;
            *q++ = val;
            memcpy(q, hostname, val);
            q += val;
            
            val = strlen(root_path);
            *q++ = RFC1533_ROOTPATH;
            *q++ = val;
            memcpy(q, root_path, val);
            q += val;
        }
        *q++ = RFC1533_END;
        m->m_len = sizeof(struct bootp_t) -
        sizeof(struct ip) - sizeof(struct udphdr);
    } else { /* Not supported */
        m->m_len = sizeof(struct bootp_t) -
        DHCP_OPT_LEN + BOOTP_VENDOR_LEN -
        sizeof(struct ip) - sizeof(struct udphdr);
    }
    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);
}

void bootp_input(struct mbuf *m)
{
    struct bootp_t *bp = mtod(m, struct bootp_t *);

    if (bp->bp_op == BOOTP_REQUEST) {
        bootp_reply(bp);
    }
}
