//
//  BootparamProg.cpp
//  Previous
//
//  Created by Simon Schubiger on 18.01.19.
//

#include <string>
#include <string.h>
#if HAVE_SYS_SYSLIMITS_H
    #include <sys/syslimits.h>
#endif
#if HAVE_LIMITS_H
    #include <limits.h>
#endif
#ifdef _WIN32
#include <Winsock2.h>
#else
#include <netinet/in.h>
#endif

#include "nfsd.h"
#include "BootparamProg.h"
#include "FileTableNFSD.h"

using namespace std;

CBootparamProg::CBootparamProg() : CRPCProg(PROG_BOOTPARAM, 1, "bootparamd") {
    #define RPC_PROG_CLASS CBootparamProg
    SET_PROC(1, WHOAMI);
    SET_PROC(2, GETFILE);
}

CBootparamProg::~CBootparamProg() {}

const int IP_ADDR_TYPE    = 1;

static void WriteInAddr(XDROutput* out, uint32_t inAddr) {
    out->write(IP_ADDR_TYPE);
    out->write(0xFF&(inAddr >> 24));
    out->write(0xFF&(inAddr >> 16));
    out->write(0xFF&(inAddr >> 8));
    out->write(0xFF&(inAddr));
}

extern "C" struct in_addr special_addr;

int CBootparamProg::procedureWHOAMI(void) {
    uint32_t address_type;
    uint32_t address;
    
    m_in->read(&address_type);
    switch (address_type) {
        case IP_ADDR_TYPE:
            m_in->read(&address);
            break;
        default:
            return PRC_FAIL;
    }
    char hostname[NAME_HOST_MAX];
    strcpy(hostname, NAME_HOST);
    char domain[NAME_DOMAIN_MAX];
    strcpy(domain, ""); // no NIS domain
    m_out->write(NAME_HOST_MAX, hostname);
    m_out->write(NAME_DOMAIN_MAX, &domain[domain[0] == '.' ? 1 : 0]);
    WriteInAddr(m_out, ntohl(special_addr.s_addr) | CTL_GATEWAY);
    return PRC_OK;
}

int CBootparamProg::procedureGETFILE(void) {
    XDRString client;
    XDRString key;
    m_in->read(client);
    m_in->read(key);
    VFSPath path = nfsd_fts[0]->getBasePathAlias();
    if(strcmp("root", key.c_str())) {
        path /= VFSPath(key.c_str());
    }

    if(path.length()) {
        m_out->write(NAME_HOST_MAX, NAME_NFSD);
        WriteInAddr(m_out, ntohl(special_addr.s_addr) | CTL_NFSD);
        m_out->write(PATH_MAX, path.c_str());
        return PRC_OK;
    } else {
        log("Unknown key: %s", key.c_str());
        return PRC_FAIL;
    }
}
