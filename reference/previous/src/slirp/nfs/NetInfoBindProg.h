//
//  NetInfoBindProg.hpp
//  Slirp
//
//  Created by Simon Schubiger on 9/1/21.
//

#ifndef NetInfoBindProg_h
#define NetInfoBindProg_h


#include <stdio.h>
#include <map>
#include <string>

#include "RPCProg.h"
#include "NetInfoProg.h"

struct nibind_addrinfo {
    uint32_t udp_port;
    uint32_t tcp_port;
};

class CNetInfoBindProg : public CRPCProg
{
    int Null(void);
    
    int procedureREGISTER(void);
    int procedureUNREGISTER(void);
    int procedureGETREGISTER(void);
    int procedureLISTREG(void);
    int procedureCREATEMASTER(void);
    int procedureCREATECLONE(void);
    int procedureDESTROYDOMAIN(void);
    int procedureBIND(void);
    void doRegister(const std::string& tag, uint32_t udpPort, uint32_t tcpPort);
    int  tryRead(const std::string& path, void* dst, size_t dstSize);
    void addHost(CNetInfoProg& host, const std::string& name, const std::string& systemType);

    std::map<std::string, nibind_addrinfo> m_Register;
public:
    CNetInfoBindProg();
    virtual ~CNetInfoBindProg();
    
    CNetInfoProg  m_Local;
    CNetInfoProg  m_Network;
    
    void configure(bool ntp);
};

#endif /* NetInfoBindProg_h */
