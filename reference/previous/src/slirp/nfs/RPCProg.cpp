#include <stdarg.h>
#include <stdio.h>

#include "RPCProg.h"
#include "TCPServerSocket.h"
#include "UDPServerSocket.h"

using namespace std;

CRPCProg::CRPCProg(int progNum, int version, const string& name) : m_bLogOn(true), m_progNum(progNum), m_version(version), m_name(name), m_portTCP(0), m_portUDP(0) {
    #define RPC_PROG_CLASS CRPCProg
    SET_PROC(0, NULL);
}

CRPCProg::~CRPCProg() {}

void CRPCProg::init(uint16_t portTCP, uint16_t portUDP) {
    m_portTCP = portTCP;
    m_portUDP = portUDP;
    log(" init tcp:%d->%d udp:%d->%d",
        getPortTCP(), TCPServerSocket::toLocalPort(getPortTCP()),
        getPortUDP(), UDPServerSocket::toLocalPort(getPortUDP()));
}

void CRPCProg::setup(XDRInput* xin, XDROutput* xout, ProcessParam* param) {
    m_in     = xin;
    m_out    = xout;
    m_param = param;
}

int CRPCProg::process(void) {
    PPROC  proc = &CRPCProg::procedureNOTIMPL;
    string name("NOTIMPL");
    if(m_param->proc < m_procs.size() && m_procs[m_param->proc]) {
        proc = m_procs[m_param->proc]->m_proc;
        name = m_procs[m_param->proc]->m_name;
    }
    int result = (this->*proc)();
    if(result == PRC_NOTIMP) log(" %d(...) = %d", m_param->proc, result);
    else                     log(" %s(...) = %d", name.c_str(),  result);
    return result;
}

void CRPCProg::setProc(int procNum, const string& name, PPROC proc) {
    while(m_procs.size() <= procNum)
        m_procs.push_back(nullptr);
    m_procs[procNum] = new RPCProc(proc, name);
}

void CRPCProg::setLogOn(bool bLogOn) {
	m_bLogOn = bLogOn;
}

int CRPCProg::getProgNum(void) const {
    return m_progNum;
}

int CRPCProg::getVersion(void) const {
    return m_version;
}

string CRPCProg::getName(void) const {
    return m_name;
}

uint16_t CRPCProg::getPortTCP(void) const {
    return m_portTCP;
}

uint16_t CRPCProg::getPortUDP(void) const {
    return m_portUDP;
}

int CRPCProg::procedureNULL(void) {
    return PRC_OK;
}

int CRPCProg::procedureNOTIMPL(void) {
    return PRC_NOTIMP;
}

size_t CRPCProg::log(const char *format, ...) const {
	va_list vargs;
	int nResult;

	nResult = 0;
	if (m_bLogOn)
	{
		va_start(vargs, format);
        printf("[NFSD:%s:%d] ", m_name.c_str(), getProgNum());
		nResult = vprintf(format, vargs);
        printf("\n");
		va_end(vargs);
	}
	return nResult;
}
