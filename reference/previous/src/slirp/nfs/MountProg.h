#ifndef _MOUNTPROG_H_
#define _MOUNTPROG_H_

#include <string>
#include <map>

#include "RPCProg.h"

enum pathFormats {
    FORMAT_PATH = 1,
    FORMAT_PATHALIAS = 2
};

class CMountProg : public CRPCProg
{
public:
	CMountProg();
	~CMountProg();
protected:
    std::string                                      m_exportPath;
	std::map<std::string, std::vector<std::string> > m_mounts;

	int   procedureMNT(void);
	int   procedureUMNT(void);
    int   procedureUMNTALL(void);
    int   procedureEXPORT(void);
};

#endif
