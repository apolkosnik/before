/* routing information protocol defines */


#define RIP_ROUTER	520

#define RIP_VERSION 1

#define RIP_REQUEST	1
#define RIP_REPLY	2

#define RIP_HOP_DEFAULT 1
#define RIP_HOP_MAX 15
#define RIP_HOP_UNREACH 16

#define RIP_ENTRIES 25


#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(1)
#endif

struct rtentry_t {
    uint16_t af;
    uint16_t pad;
    struct in_addr rt_addr;
    uint32_t unused[2];
    uint32_t metric;
} PACKED__;

#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(PACK_RESET)
#endif

#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(1)
#endif

struct rip_t {
    struct ip ip;
    struct udphdr udp;
    uint8_t rp_cmd;
    uint8_t rp_vers;
    uint8_t pad[2];
    struct rtentry_t rp_rt[1];
} PACKED__;

#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(PACK_RESET)
#endif

void rip_input(struct mbuf *m);
void rip_broadcast(void);
