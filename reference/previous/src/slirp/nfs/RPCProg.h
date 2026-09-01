#ifndef _RPCPROG_H_
#define _RPCPROG_H_

#include <stdint.h>
#include <stddef.h>

/* The maximum number of bytes in a pathname argument. */
#define MAXPATHLEN 1024

/* The maximum number of bytes in a file name argument. */
#define MAXNAMELEN 255

/* The size in bytes of the opaque file handle. */
#define FHSIZE      32
#define FHSIZE_NFS3 64

enum
{
	PRC_OK,
	PRC_FAIL,
	PRC_NOTIMP
};

typedef struct
{
	uint32_t    version;
	uint32_t    proc;
    int         sockType;
	const char *remoteAddr;
} ProcessParam;

#ifdef __cplusplus

#include "XDRStream.h"
#include <string>
#include <vector>

class CRPCProg;

typedef int (CRPCProg::*PPROC)(void);

struct RPCProc {
    PPROC       m_proc;
    std::string m_name;
    RPCProc(PPROC proc, const std::string& name) : m_proc(proc), m_name(name) {}
};

class CRPCProg {
public:
    CRPCProg(int progNum, int version, const std::string& name);
    virtual     ~CRPCProg();
    
    void         init(uint16_t portTCP, uint16_t portUDP);
    void         setup(XDRInput* xin, XDROutput* xout, ProcessParam* param);
	virtual int  process(void);
	virtual void setLogOn(bool bLogOn);
    int          getProgNum(void) const;
    int          getVersion(void) const;
    std::string  getName(void)    const;
    uint16_t     getPortTCP(void) const;
    uint16_t     getPortUDP(void) const;
    
    int          procedureNULL(void);
    int          procedureNOTIMPL(void);
    
protected:
    std::vector<RPCProc*>    m_procs;
    bool                     m_bLogOn;
    uint32_t                 m_progNum;
    uint32_t                 m_version;
    std::string              m_name;
    uint16_t                 m_portTCP;
    uint16_t                 m_portUDP;
    ProcessParam*            m_param;
    XDRInput*                m_in;
    XDROutput*               m_out;

    void           setProc(int procNum, const std::string& name, PPROC proc);
	size_t         log(const char *format, ...) const;
};

#define SET_PROC(num, proc) setProc(num, #proc, (PPROC)&RPC_PROG_CLASS::procedure##proc)

#endif

#endif
