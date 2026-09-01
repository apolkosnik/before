#include <string.h>
#include <socket.h>

#include "PortmapProg.h"
#include "nfsd.h"
#include "XDRStream.h"

CPortmapProg::CPortmapProg() : CRPCProg(PROG_PORTMAP, 2, "portmapd") {
    #define RPC_PROG_CLASS CPortmapProg
    SET_PROC(3, GETPORT);
    SET_PROC(4, DUMP);
    SET_PROC(5, CALLIT);
}

CPortmapProg::~CPortmapProg() {}

void CPortmapProg::Add(CRPCProg* prog) {
    m_nProgTable[prog->getProgNum()] = prog;
}

const CRPCProg* CPortmapProg::GetProg(int prog) {
    if(m_nProgTable.find(prog) == m_nProgTable.end()) {
        log("no program %d", prog);
        return nullptr;
    }
    return m_nProgTable[prog];
}

int CPortmapProg::procedureGETPORT(void) {
    uint32_t prog;
    uint32_t vers;
    uint32_t proto;
    uint32_t port;

    m_in->read(&prog);
    m_in->read(&vers);
    m_in->read(&proto);
    m_in->read(&port);
    
    const CRPCProg* cprog = GetProg(prog);
    if(!(cprog)) return PRC_FAIL;
    
    switch(proto) {
        case IPPROTO_TCP:
            log("GETPORT TCP %d %d", prog, cprog->getPortTCP());
            m_out->write(cprog->getPortTCP());
            return PRC_OK;
        case IPPROTO_UDP:
            log("GETPORT UDP %d %d", prog, cprog->getPortUDP());
            m_out->write(cprog->getPortUDP());
            return PRC_OK;
        default:
            return PRC_FAIL;
    }
}

int CPortmapProg::procedureDUMP(void) {
    for (std::map<int, CRPCProg*>::iterator it = m_nProgTable.begin(); it != m_nProgTable.end(); it++)
        Write(it->second);
    
    m_out->write(0);
    
    return PRC_OK;
}

int CPortmapProg::procedureCALLIT(void) {
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;

    m_in->read(&prog);
    m_in->read(&vers);
    m_in->read(&proc);
    
    XDROpaque in;
    m_in->read(in);
    
    CRPCProg* cprog = (CRPCProg*)GetProg(prog);
    if(!(cprog)) return PRC_FAIL;
    
    ProcessParam param;
    param.proc       = proc;
    param.version    = vers;
    param.remoteAddr = m_param->remoteAddr;

    XDRInput  din(in);
    XDROutput dout;
    
    cprog->setup(&din, &dout, &param);
    int result = cprog->process();
    
    m_out->write(m_param->sockType == SOCK_STREAM ? cprog->getPortTCP() : cprog->getPortUDP());
    XDROpaque out(XDROpaque(dout.data(), dout.size()));
    m_out->write(out);
    return result;
}
 

void CPortmapProg::Write(const CRPCProg* prog) {
    m_out->write(1);
    m_out->write(prog->getProgNum());
    m_out->write(prog->getVersion());
    m_out->write(IPPROTO_TCP);
    m_out->write(prog->getPortTCP());
    
    m_out->write(1);
    m_out->write(prog->getProgNum());
    m_out->write(prog->getVersion());
    m_out->write(IPPROTO_UDP);
    m_out->write(prog->getPortUDP());
}
