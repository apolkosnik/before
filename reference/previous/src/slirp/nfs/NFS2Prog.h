#ifndef _NFS2PROG_H_
#define _NFS2PROG_H_

#include <string>

#include "RPCProg.h"

class CNFS2Prog : public CRPCProg
{
public:
	CNFS2Prog();
	~CNFS2Prog();
	void setUserID(unsigned int nUID, unsigned int nGID);

protected:
	int procedureGETATTR(void);
	int procedureSETATTR(void);
	int procedureLOOKUP(void);
    int procedureREADLINK(void);
	int procedureREAD(void);
    int procedureWRITECACHE(void);
	int procedureWRITE(void);
	int procedureCREATE(void);
	int procedureREMOVE(void);
	int procedureRENAME(void);
    int procedureLINK(void);
    int procedureSYMLINK(void);
	int procedureMKDIR(void);
	int procedureRMDIR(void);
	int procedureREADDIR(void);
	int procedureSTATFS(void);

private:
    bool getPath(std::string& result, uint64_t* handle = NULL);
    bool getFullPath(std::string& result);
	bool checkFile(const std::string& path);
    bool writeFileAttributes(const std::string& path);    
};

#endif
