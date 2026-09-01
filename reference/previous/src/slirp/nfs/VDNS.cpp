//
//  VDNS.c
//  Previous
//
//  Created by Simon Schubiger on 22.02.19.
//

#include "VDNS.h"
#include "nfsd.h"

#include "compat.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <Winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace std;

vector<vdns_record> VDNS::sDB;
vdns_record         VDNS::errNoSuchName;

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

static string ip_addr_str(uint32_t addr, string suffix) {
    stringstream ss;
    ss << (0xFF&(addr >> 24)) << "." << (0xFF&(addr >> 16)) << "." <<  (0xFF&(addr >> 8)) << "." << (0xFF&(addr)) << suffix;
    return ss.str();
}

void VDNS::addRecord(uint32_t addr, const string& _name) {
    size_t size = _name.size();
    char name[size + 2];
    for(size_t i = 0; i < size; i++)
        name[i] = tolower(_name[i]);
    name[size] = '\0';
    
    vdns_record rec;
    rec.type   = REC_A;
    rec.inaddr = addr;
    rec.key    = ip_addr_str(addr, ".");
    uint32_t inaddr = htonl(addr);
    rec.size = 4;
    memcpy(rec.data, &inaddr, rec.size);
    sDB.push_back(rec);
    
    rec.type   = REC_A;
    rec.inaddr = addr;
    rec.key    = name;
    rec.key    += ".";
    inaddr = htonl(addr);
    rec.size = 4;
    memcpy(rec.data, &inaddr, rec.size);
    sDB.push_back(rec);

    rec.type   = REC_PTR;
    rec.inaddr = addr;
    rec.key    = ip_addr_str(SDL_Swap32(addr), ".in-addr.arpa.");
    rec.size   = domain_name(rec.data , name);
    sDB.push_back(rec);
}

VDNS::VDNS(CNetInfoBindProg* netInfoBind)
: mMutex(host_mutex_create())
{
    mUDP = new UDPServerSocket(this);
    mUDP->open(PROG_VDNS, PORT_DNS);
    
    vector<NetInfoNode*> machines = netInfoBind->m_Network.mRoot.find("name", "machines")[0]->mChildren;
    string domain(NAME_DOMAIN);
    for(size_t i = 0; i < machines.size(); i++) {
        string name = machines[i]->getPropValue("name");
        if(name.size() <= domain.size() || name.compare(name.size() - domain.size(), domain.size(), domain))
            name += domain;
        string ip   = machines[i]->getPropValues(machines[i]->mProps, "ip_address")[0];
        in_addr addr;
#ifdef _WIN32
        inet_pton(AF_INET, ip.c_str(), &addr);
#else
        inet_aton(ip.c_str(), &addr);
#endif
        addRecord(ntohl(addr.s_addr), name);
    }
    addRecord(0x7F000001, "localhost");
}

VDNS::~VDNS(void) {
    mUDP->close();
    delete mUDP;
    host_mutex_destroy(mMutex);
}

static vdns_rec_type to_dot(string& dst, const uint8_t* src, size_t size) {
    const uint8_t* end   = &src[size];
    uint8_t        count = 0;
    int            result = REC_UNKNOWN;
    while(*src) {
        if(src >= end) goto error;
        count = *src++;
        if(count > 63) goto error;
        for(int j = 0; j < count; j++) {
            if(src >= end) goto error;
            dst.push_back(tolower(*src++));
        }
        dst.push_back('.');
    }
    src++;
    result = *src++;
    result <<= 8;
    result |= *src;
error:
    return static_cast<vdns_rec_type>(result);
}

vdns_record* VDNS::query(uint8_t* data, size_t size) {
    string  qname;
    vdns_rec_type qtype = to_dot(qname, data, size);
    std::cout << "[VDNS] query(" << qtype << ") '" << qname << "'" << std::endl;
    
    if(qtype < 0) return nullptr;
    
    for(size_t n = 0; n < sDB.size(); n++)
        if(qname ==sDB[n].key && sDB[n].type == qtype)
            return &sDB[n];
    
    string domain(NAME_DOMAIN + string("."));
    if(qname.rfind(domain) == qname.size() -  domain.size())
        return &errNoSuchName;
    
    return nullptr;
}


extern "C" int vdns_match(struct mbuf *m, uint32_t addr, int dport) {
    if(m->m_len > 40 &&
       dport == PORT_DNS &&
       addr == (CTL_NET | CTL_DNS))
        return VDNS::query(reinterpret_cast<uint8_t*>(&m->m_data[40]), m->m_len-40) != NULL;
    else
        return false;
}

extern "C" void vdns_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO) {
    switch(ntohs(*dportNBO)) {
        case PORT_DNS:
            // map port & address for virtual DNS
            *dportNBO = htons(UDPServerSocket::toLocalPort(PORT_DNS));
            *ipNBO    = loopback_addr;
            break;
        default:
            break;
    }
}

void VDNS::socketReceived(CSocket* pSocket, uint32_t header) {
    NFSDLock lock(mMutex);
    
    XDRInput*    in  = pSocket->getInputStream();
    XDROutput*   out = pSocket->getOutputStream();
    uint8_t*     msg = &in->data()[in->getPosition()];
    int          n   = static_cast<int>(in->size());
    size_t       off = 12;
    vdns_record* rec = query(&msg[off], in->size()-(in->getPosition()+off));

    if(rec == &errNoSuchName) {
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
        msg[2]=0x84;
        msg[3]=0x13;
 
        // Change Opcode and flags
        msg[6]=0;msg[7]   = 0; // No answers
        msg[8]=0;msg[9]   = 0;   // NSCOUNT
        msg[10]=0;msg[11] = 0; // ARCOUNT
        
        printf("[VDNS] no record found.\n");
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

        msg[2]=0x84;
        msg[3]=0x10;
        // Change Opcode and flags
        msg[8]=0;msg[9]=0; // NSCOUNT
        msg[10]=0;msg[11]=0; // ARCOUNT

        if(rec) {
            // Keep request in message and add answer
            msg[n++]=0xC0; msg[n++]=off; // Offset to the domain name

            msg[n++]=0x00;
            msg[n++]=rec->type;  // Type
            
            msg[n++]=0x00;msg[n++]=0x01; // Class 1
            msg[n++]=0x00;msg[n++]=0x00;msg[n++]=0x00;msg[n++]=0x3c; // TTL
            
            msg[6]=0;msg[7] = 1; // Num answers
            uint32_t inaddr = rec->inaddr;
            printf("[VDNS] reply '%s' -> %d.%d.%d.%d\n", rec->key.c_str(), 0xFF&(inaddr >> 24), 0xFF&(inaddr >> 16), 0xFF&(inaddr >> 8), 0xFF&(inaddr));
            switch(rec->type) {
                case REC_A:
                case REC_PTR:
                    msg[n++]=0x00;msg[n++]=rec->size;
                    memcpy(&msg[n], rec->data, rec->size);
                    n += rec->size;
                    break;
                default:
                    printf("[VDNS] unknown query:%d ('%s')\n", rec->type, rec->key.c_str());
                    break;
            }
        } else {
            msg[6]=0;msg[7] = 0; // Num answers
            printf("[VDNS] no record found.\n");
        }
    }
    
    // Send the answer
    out->write(msg, n);
    pSocket->send();  //send response
}
