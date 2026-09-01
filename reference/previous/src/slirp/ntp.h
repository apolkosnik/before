/* Network time protocol defines */


#define NTP_SERVER      123

#define NTP_LEAP        0
#define NTP_VERSION     1
#define NTP_MODE_CLIENT 3
#define NTP_MODE_SEVER  4
#define NTP_STRATUM	    1
#define NTP_POLL        3
#define NTP_PRECISION   0xEE
#define NTP_IDENTITY    0x50524556 /* PREV */


#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(1)
#endif

struct ntp_t {
    struct ip ip;
    struct udphdr udp;
    uint8_t status;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint16_t distance[2];
    uint16_t drift[2];
    uint32_t refid;
    uint32_t reftime[2];
    uint32_t org[2];
    uint32_t rec[2];
    uint32_t xmt[2];
} PACKED__;

#ifdef PRAGMA_PACK_SUPPORTED
#pragma pack(PACK_RESET)
#endif

void ntp_input(struct mbuf *m);
