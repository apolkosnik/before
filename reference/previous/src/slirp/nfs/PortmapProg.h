#ifndef _PORTMAPPROG_H_
#define _PORTMAPPROG_H_

#include "RPCProg.h"
#include "RPCProg.h"
#include <map>

class CPortmapProg : public CRPCProg
{
public:
    CPortmapProg();
    virtual ~CPortmapProg();
    void Add(CRPCProg* prog);
    
protected:
    
    const CRPCProg* GetProg(int progNum);
    int procedureNOIMP(void);
    int procedureNULL(void);
    int procedureSET(void);
    int procedureUNSET(void);
    int procedureGETPORT(void);
    int procedureDUMP(void);
    int procedureCALLIT(void);
    
private:
    void Write(const CRPCProg* prog);
    std::map<int, CRPCProg*> m_nProgTable;
};

#endif
