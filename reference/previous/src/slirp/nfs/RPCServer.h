#ifndef _RPCSERVER_H_
#define _RPCSERVER_H_

#include "SocketListener.h"
#include "CSocket.h"
#include "RPCProg.h"
#include "host.h"
#include <map>
#include <vector>

class CRPCServer : public ISocketListener {
public:
	CRPCServer();
	virtual ~CRPCServer();
	void set(int nProg, CRPCProg* pRPCProg);
	void setLogOn(bool bLogOn);
	void socketReceived(CSocket* pSocket, uint32_t header);
protected:
    std::map<int, std::vector<CRPCProg*> > m_pProgTable;
	mutex_t*                               m_hMutex;
    
    int process(int nType, int port, XDRInput* pInStream, XDROutput* pOutStream, uint32_t headerIn, const char* pRemoteAddr);
};

#endif
