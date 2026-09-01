//
//  VDNS.h
//  Previous
//
//  Created by Simon Schubiger on 22.02.19.
//

#ifndef VDNS_h
#define VDNS_h

#ifdef __cplusplus

#include <stdio.h>
#include <string>
#include <vector>

#include "UDPServerSocket.h"
#include "host.h"
#include "mbuf.h"
#include "NetInfoBindProg.h"

typedef enum {
    REC_A     = 1,  // Host address
    REC_CNAME = 5,  // Canonical name for an alias
    REC_MX    = 15, // Mail eXchange
    REC_NS    = 2,  // Name Server
    REC_PTR   = 12, // Pointer
    REC_SOA   = 6,  // Start Of Authority
    REC_SRV   = 33, // location of service
    REC_TXT   = 16, // Descriptive text

    REC_UNKNOWN = -1,
} vdns_rec_type;

typedef struct {
    vdns_rec_type type;
    std::string   key;
    uint8_t       data[1024];
    size_t        size;
    uint32_t      inaddr;
} vdns_record;

class VDNS : public ISocketListener {
    static std::vector<vdns_record> sDB;
    static vdns_record              errNoSuchName;
    mutex_t*                        mMutex;
    UDPServerSocket*                mUDP;
    
    void addRecord(uint32_t addr, const std::string& name);
public:
    VDNS(CNetInfoBindProg* netInfoBind);
    ~VDNS(void);
    
    static vdns_record* query(uint8_t* data, size_t size);
    void   socketReceived(CSocket* pSocket, uint32_t header);
};

extern "C" int vdns_match(struct mbuf *m, uint32_t addr, int dport);
#else
    int  vdns_match(struct mbuf *m, uint32_t addr, int dport);
    void vdns_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO);
#endif /* __cplusplus */

#endif /* VDNS_h */
