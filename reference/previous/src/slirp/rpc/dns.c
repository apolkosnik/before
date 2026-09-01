/*
 * Domain Name System
 * 
 * Created by Simon Schubiger on 22.02.2019
 * Rewritten in C by Andreas Grabher
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
#include <stdlib.h>
#include <ctype.h>

#include "rpc.h"
#include "dns.h"
#include "ctl.h"


#define DBG 0

typedef enum {
    REC_A     = 1,  /* Host address */
    REC_CNAME = 5,  /* Canonical name for an alias */
    REC_MX    = 15, /* Mail eXchange */
    REC_NS    = 2,  /* Name Server */
    REC_PTR   = 12, /* Pointer */
    REC_SOA   = 6,  /* Start Of Authority */
    REC_SRV   = 33, /* Location of service */
    REC_TXT   = 16, /* Descriptive text */
    
    REC_UNKNOWN = -1,
} vdns_rec_type;

struct vdns_record_t {
    vdns_rec_type type;
    char*         key;
    uint8_t       data[1024];
    size_t        size;
    uint32_t      inaddr;
};

struct vdns_entry_t {
    struct vdns_record_t* rec;
    uint32_t addr;
    struct vdns_entry_t* next;
};

static struct vdns_t {
    struct vdns_entry_t* db;
    struct vdns_record_t errNoSuchName;
    mutex_t*             mutex;
    struct udpsocket_t*  udp;
    uint16_t             local_port;
} vdns;

static void vdns_add_record(struct vdns_record_t* rec, uint32_t addr) {
    struct vdns_entry_t** entry = &vdns.db;

    while (*entry) {
        if (((*entry)->rec->type != rec->type) || 
            strcmp((*entry)->rec->key, rec->key)) {
            entry = &(*entry)->next;
            continue;
        }
        printf("[DNS] Duplicate record (%d) %s\n", rec->type, rec->key);
        free((*entry)->rec->key);
        free((*entry)->rec);
        (*entry)->rec  = rec;
        (*entry)->addr = addr;
        return;
    }
    *entry = (struct vdns_entry_t*)malloc(sizeof(struct vdns_entry_t));
    (*entry)->rec  = rec;
    (*entry)->addr = addr;
    (*entry)->next = NULL;
}

static void vdns_remove_records(uint32_t addr) {
    struct vdns_entry_t** entry = &vdns.db;
    struct vdns_entry_t* next = NULL;

    while (*entry) {
        if ((*entry)->addr == addr) {
            next = (*entry)->next;
            free((*entry)->rec->key);
            free((*entry)->rec);
            free(*entry);
            *entry = next;
        } else {
            entry = &(*entry)->next;
        }
    }
}

static struct vdns_record_t* vdns_find_record(char* key, vdns_rec_type type) {
    struct vdns_entry_t** entry = &vdns.db;
    
    while (*entry) {
        if (((*entry)->rec->type == type) &&  
            (strcmp((*entry)->rec->key, key) == 0)) {
            return (*entry)->rec;
        }
        entry = &(*entry)->next;
    }
    return NULL;
}

static void vdns_delete_db(void) {
    struct vdns_entry_t** entry = &vdns.db;
    struct vdns_entry_t* next = NULL;
    
    while (*entry) {
        free((*entry)->rec->key);
        free((*entry)->rec);
        next = (*entry)->next;
        free((*entry));
        *entry = next;
    }
}

static uint32_t swap_uint32(uint32_t val) {
    val = ((val << 8) & 0xFF00FF00 ) | ((val >> 8) & 0xFF00FF ); 
    return (val << 16) | (val >> 16);
}

static size_t domain_name(uint8_t* dst, const char* src) {
    size_t   result = strlen(src) + 2;
    uint8_t* len    = dst++;
    *len            = 0;
    while(*src) {
        if(*src == '.') {
            len = dst++;
            *len = 0;
            src++;
            continue;
        }
        *dst++ = tolower(*src++);
        *len = *len + 1;
    }
    *dst++ = '\0';
    return result;
}

static char* alloc_ip_addr_str(uint32_t addr, const char* suffix) {
    char addrstr[16];
    size_t len = sizeof(addrstr) + strlen(suffix);
    char* result = (char*)malloc(len);
    snprintf(result, len, "%s%s", rpc_ip_str(addrstr, addr, 4, sizeof(addrstr)), suffix);
    return result;
}

static char* alloc_lowercase_name(const char* name, const char* suffix) {
    size_t i;
    size_t namelen = strlen(name);
    size_t suffixlen = strlen(suffix);
    char* result = (char*)malloc(namelen + suffixlen + 1);
    for (i = 0; i < namelen; i++) {
        result[i] = tolower(name[i]);
    }
    memcpy(result + namelen, suffix, suffixlen);
    result[namelen + suffixlen] = '\0';
    return result;
}

static void addRecord(uint32_t addr, const char* name) {
    struct vdns_record_t* rec;
    uint32_t inaddr = htonl(addr);

    rec = (struct vdns_record_t*)malloc(sizeof(struct vdns_record_t));
    rec->type   = REC_A;
    rec->inaddr = addr;
    rec->key    = alloc_ip_addr_str(addr, ".");
    rec->size = 4;
    memcpy(rec->data, &inaddr, rec->size);
    vdns_add_record(rec, addr);
    
    rec = (struct vdns_record_t*)malloc(sizeof(struct vdns_record_t));
    rec->type   = REC_A;
    rec->inaddr = addr;
    rec->key    = alloc_lowercase_name(name, ".");
    inaddr = htonl(addr);
    rec->size = 4;
    memcpy(rec->data, &inaddr, rec->size);
    vdns_add_record(rec, addr);
    
    rec = (struct vdns_record_t*)malloc(sizeof(struct vdns_record_t));
    rec->type   = REC_PTR;
    rec->inaddr = addr;
    rec->key    = alloc_ip_addr_str(swap_uint32(addr), ".in-addr.arpa.");
    rec->size   = domain_name(rec->data , name);
    vdns_add_record(rec, addr);
}

static vdns_rec_type to_dot(char* dst, const uint8_t* src, size_t size) {
    int j;
    const uint8_t* end    = &src[size];
    uint8_t        count  = 0;
    uint16_t       result = REC_UNKNOWN;
    while (*src) {
        if (src >= end) return REC_UNKNOWN;
        count = *src++;
        if (count > 63) return REC_UNKNOWN;
        for (j = 0; j < count; j++) {
            if (src >= end) return REC_UNKNOWN;
            *dst++ = tolower(*src++);
        }
        *dst++ = '.';
    }
    *dst = '\0';
    src++;
    result = *src++;
    result <<= 8;
    result |= *src;
    return (vdns_rec_type)result;
}

static struct vdns_record_t* vdns_query(uint8_t* data, size_t size) {
    struct vdns_record_t* rec;
    size_t offset;
    char qname[1024];
    char domain[256];
    if (size > sizeof(qname)) {
        printf("[DNS] query too long (%d)\n", (int)size);
        return NULL;
    }
    vdns_rec_type qtype = to_dot(qname, data, size);
    printf("[DNS] query(%d) '%s'\n", qtype, qname);
    
    if (qtype < 0) return NULL;
    
    rec = vdns_find_record(qname, qtype);
    if (rec) {
        return rec;
    }
    
    snprintf(domain, sizeof(domain), "%s.", NAME_DOMAIN);
    offset = strlen(qname) - strlen(domain);
    if (offset >= 0) {
        if (strncmp(qname + offset, domain, strlen(domain)) == 0) {
            return &vdns.errNoSuchName;
        }
    }
    return NULL;
}

static void msg_write_word(uint8_t* msg, int offset, uint16_t val) {
    *(uint16_t*)(msg + offset) = htons(val);
}

static void msg_write_long(uint8_t* msg, int offset, uint32_t val) {
    *(uint32_t*)(msg + offset) = htonl(val);
}

static void vdns_input(struct csocket_t* pSocket) {
    struct vdns_record_t* rec;
    
    struct xdr_t* m_in  = pSocket->m_Input;
    struct xdr_t* m_out = pSocket->m_Output;
    
    uint8_t*      msg   = m_in->data;
    uint32_t      n     = m_in->size;
    size_t        off   = 12;
    
    host_mutex_lock(vdns.mutex);
    
    rec = vdns_query(msg + off, n - off);
    
    if (rec == &vdns.errNoSuchName) {
        /*
         1... .... .... .... = Response: Message is a response
         .000 0... .... .... = Opcode: Standard query (0)
         .... .1.. .... .... = Authoritative: Server is an authority for domain
         .... ..0. .... .... = Truncated: Message is not truncated
         .... ...0 .... .... = Recursion desired: Do not query recursively
         .... .... 0... .... = Recursion available: Server can not do recursive queries
         .... .... .0.. .... = Z: reserved (0)
         .... .... ..0. .... = Answer authenticated: Answer/authority portion was authenticated by the server
         .... .... ...1 .... = Non-authenticated data: Acceptable
         .... .... .... 0011 = Reply code: No such name (3)
         */
        msg_write_word(msg,  2, 0x8413);
        
        /* Change Opcode and flags */
        msg_write_word(msg,  6, 0); /* no answers */
        msg_write_word(msg,  8, 0); /* NSCOUNT */
        msg_write_word(msg, 10, 0); /* ARCOUNT */
        
        printf("[DNS] no record found.\n");
    } else {
        /*
         1... .... .... .... = Response: Message is a response
         .000 0... .... .... = Opcode: Standard query (0)
         .... .1.. .... .... = Authoritative: Server is an authority for domain
         .... ..0. .... .... = Truncated: Message is not truncated
         .... ...0 .... .... = Recursion desired: Do not query recursively
         .... .... 0... .... = Recursion available: Server can not do recursive queries
         .... .... .0.. .... = Z: reserved (0)
         .... .... ..0. .... = Answer authenticated: Answer/authority portion was authenticated by the server
         .... .... ...1 .... = Non-authenticated data: Acceptable
         .... .... .... 0000 = Reply code: No error (0)
         */
        msg_write_word(msg,  2, 0x8410);
        
        /* Change Opcode and flags */
        msg_write_word(msg,  8, 0); /* NSCOUNT */
        msg_write_word(msg, 10, 0); /* ARCOUNT */
        
        if (rec) {
            char addrstr[16];
            msg_write_word(msg, 6, 1); /* Num answers */
            
            /* Keep request in message and add answer */
            msg_write_word(msg, n, 0xc000 | off); /* Offset to the domain name */
            n += 2;
            msg_write_word(msg, n, rec->type);    /* Type */
            n += 2;
            msg_write_word(msg, n, 1);            /* Class 1 */
            n += 2;
            msg_write_long(msg, n, 60);           /* TTL */
            n += 4;
            
            printf("[DNS] reply '%s' -> %s\n", rec->key, rpc_ip_str(addrstr, rec->inaddr, 4, sizeof(addrstr)));
            switch(rec->type) {
                case REC_A:
                case REC_PTR:
                    msg_write_word(msg, n, rec->size);
                    n += 2;
                    memcpy(&msg[n], rec->data, rec->size);
                    n += rec->size;
                    break;
                default:
                    printf("[DNS] unknown query:%d ('%s')\n", rec->type, rec->key);
                    break;
            }
        } else {
            msg_write_word(msg, 6, 0); /* no answers */
            printf("[DNS] no record found.\n");
        }
    }
    
    /* Send the answer */
    memcpy(m_out->data, msg, n);
    m_out->size = n;
    
#if DBG
    for (int i = 0; i < n; i++) {
        printf("%02x ", msg[i]);
    }
    printf("\n");
    for (int i = 0; i < m_out->size; i++) {
        printf("%02x ", m_out->data[i]);
    }
    printf("\n");
#endif
    
    csocket_send(pSocket);
    
    host_mutex_unlock(vdns.mutex);
}


void vdns_init(void) {
    if (vdns.local_port) return;
    vdns.mutex = host_mutex_create();
    vdns.udp   = udpsocket_init(vdns_input, NULL);
    if (vdns.udp) {
        vdns.local_port = udpsocket_open(vdns.udp, PORT_DNS);
        if (vdns.local_port) {
            printf("[DNS] started (UDP: %d -> %d).\n", PORT_DNS, vdns.local_port);
        } else {
            printf("[DNS] start failed.\n");
            vdns_uninit();
        }
    } else {
        printf("[DNS] Socket initialisation failed.\n");
        host_mutex_destroy(vdns.mutex);
    }

    if (vdns.local_port) {
        char hostname[NAME_HOST_MAX];
        gethostname(hostname, sizeof(hostname));
        hostname[NAME_HOST_MAX-1] = '\0';
        vfscat(hostname, NAME_DOMAIN, sizeof(hostname));
        
        printf("[DNS] Creating database.\n");
        
        addRecord(0x7F000001,        "localhost");
        addRecord(CTL_NET|CTL_ALIAS, hostname);
        addRecord(CTL_NET|CTL_HOST,  FQDN_HOST);
        addRecord(CTL_NET|CTL_DNS,   FQDN_DNS);
    }
}

void vdns_uninit(void) {
    if (vdns.udp) {
        vdns.local_port = 0;
        udpsocket_close(vdns.udp);
        vdns.udp = udpsocket_uninit(vdns.udp);
        host_mutex_destroy(vdns.mutex);
    }
    printf("[DNS] Deleting database.\n");
    vdns_delete_db();
}

void vdns_add_rec(const char* name, uint32_t addr) {
    char addrstr[16];
    char hostname[NAME_HOST_MAX];
    printf("[DNS] Adding record for %s: '%s'.\n", rpc_ip_str(addrstr, addr, 4, sizeof(addrstr)), name);
    vfscpy(hostname, name, sizeof(hostname));
    vfscat(hostname, NAME_DOMAIN, sizeof(hostname));
    addRecord(addr, hostname);
}

void vdns_remove_rec(uint32_t addr) {
    char addrstr[16];
    printf("[DNS] Removing record for %s.\n", rpc_ip_str(addrstr, addr, 4, sizeof(addrstr)));
    vdns_remove_records(addr);
}

int vdns_match(struct mbuf *m, uint32_t addr, int dport) {
    if(m->m_len > 40 &&
       dport == PORT_DNS &&
       addr == (CTL_NET | CTL_DNS))
        return vdns_query((uint8_t*)(&m->m_data[40]), m->m_len-40) != NULL;
    else
        return 0;
}

void vdns_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO) {
    switch(ntohs(*dportNBO)) {
        case PORT_DNS:
            /* map port and address for virtual DNS */
            *dportNBO = htons(vdns.local_port);
            *ipNBO    = loopback_addr;
            break;
        default:
            break;
    }
}

void vdns_udp_map_from_local_port(uint16_t port, struct in_addr* saddrNBO, uint16_t* sin_portNBO) {
    if (port == vdns.local_port) {
        *sin_portNBO = htons(PORT_DNS);
        saddrNBO->s_addr = special_addr.s_addr | htonl(CTL_DNS);
    }
}
