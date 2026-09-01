#include <stdio.h>

#include "RPCServer.h"
#include "TCPServerSocket.h"

#include "compat.h"

using namespace std;

enum
{
	CALL = 0,
	REPLY = 1
};

enum
{
	MSG_ACCEPTED = 0,
	MSG_DENIED = 1
};

enum
{
	SUCCESS       = 0,
	PROG_UNAVAIL  = 1,
	PROG_MISMATCH = 2,
	PROC_UNAVAIL  = 3,
	GARBAGE_ARGS  = 4
};

enum
{
	AUTH_NONE  = 0,
	AUTH_UNIX  = 1,
	AUTH_SHORT = 2,
	AUTH_DES   = 3
};

typedef struct
{
	uint32_t flavor;
	uint32_t length;
} OPAQUE_AUTH;

typedef struct
{
	uint32_t header;
	uint32_t XID;
	uint32_t msg;
	uint32_t rpcvers;
	uint32_t prog;
	uint32_t vers;
	uint32_t proc;
	OPAQUE_AUTH cred;
	OPAQUE_AUTH verf;
} RPC_HEADER;

typedef struct {
	uint32_t time;
	XDRString machine;
	uint32_t uid;
	uint32_t gid;
	uint32_t len;
	uint32_t gids[16];
} RPC_AUTH_UNIX;

CRPCServer::CRPCServer() : m_hMutex(host_mutex_create()) {
}

CRPCServer::~CRPCServer() {
	host_mutex_destroy(m_hMutex);
}

void CRPCServer::set(int nProg, CRPCProg *pRPCProg) {
	m_pProgTable[nProg].push_back(pRPCProg);  //set program handler
}

void CRPCServer::setLogOn(bool bLogOn) {
	for (std::map<int, std::vector<CRPCProg*> >::iterator it = m_pProgTable.begin(); it != m_pProgTable.end(); it++)
		for (size_t i = 0; i < it->second.size(); i++)
			it->second[i]->setLogOn(bLogOn);
}

void CRPCServer::socketReceived(CSocket *pSocket, uint32_t header) {
	NFSDLock lock(m_hMutex);

	XDRInput* pInStream = pSocket->getInputStream();
	while (pInStream->hasData()) {
		int nResult = process(pSocket->getType(), pSocket->getServerPort(), pInStream, pSocket->getOutputStream(), header, pSocket->getRemoteAddress());  //process input data
		pSocket->send();  //send response
		if (nResult != PRC_OK || pSocket->getType() == SOCK_DGRAM)
			break;
	}
}

int CRPCServer::process(int sockType, int port, XDRInput* pInStream, XDROutput* pOutStream, uint32_t headerIn, const char* pRemoteAddr) {
	RPC_HEADER header;
	RPC_AUTH_UNIX auth;
	ProcessParam param;

	size_t headerPos = 0;
	int nResult = PRC_OK;

	header.header = headerIn;
	pInStream->read(&header.XID);
	pInStream->read(&header.msg);
	pInStream->read(&header.rpcvers);  //rpc version
	pInStream->read(&header.prog);  //program
	pInStream->read(&header.vers);  //program version
	pInStream->read(&header.proc);  //procedure
	pInStream->read(&header.cred.flavor);
	pInStream->read(&header.cred.length);
	if (header.cred.flavor == AUTH_UNIX) {
		pInStream->read(&auth.time);
		pInStream->read(auth.machine);
		pInStream->read(&auth.uid);
		pInStream->read(&auth.gid);
		pInStream->read(&auth.len);
		pInStream->skip(auth.len * 4);
	} else {
		pInStream->skip(header.cred.length);
	}
	pInStream->read(&header.verf.flavor);  //vefifier
	if (pInStream->read(&header.verf.length) < sizeof(header.verf.length))
		nResult = PRC_FAIL;
	if (pInStream->skip(header.verf.length) < header.verf.length)
		nResult = PRC_FAIL;

	if (sockType == SOCK_STREAM)
	{
		headerPos = pOutStream->getPosition();  //remember current position
		pOutStream->write(header.header);  //this value will be updated later
	}
	pOutStream->write(header.XID);
	pOutStream->write(REPLY);
	pOutStream->write(MSG_ACCEPTED);
	pOutStream->write(header.verf.flavor);
	pOutStream->write(header.verf.length);
	if (nResult == PRC_FAIL)  //input data is truncated
		pOutStream->write(GARBAGE_ARGS);
	else if (m_pProgTable.find(header.prog) == m_pProgTable.end() || m_pProgTable[header.prog].empty())  //program is unavailable
		pOutStream->write(PROG_UNAVAIL);
	else
	{
		pOutStream->write(SUCCESS);  //this value may be modified later if process failed
		param.version    = header.vers;
		param.proc       = header.proc;
		param.remoteAddr = pRemoteAddr;
		param.sockType   = sockType;

		CRPCProg* prog = m_pProgTable[header.prog][0];
		for(int i = 0; i < m_pProgTable[header.prog].size(); i++) {
			if (sockType == SOCK_STREAM) {
				if(m_pProgTable[header.prog][i]->getPortTCP() == port)
					prog = m_pProgTable[header.prog][i];
			} else {
				if(m_pProgTable[header.prog][i]->getPortUDP() == port)
					prog = m_pProgTable[header.prog][i];
			}
		}
		prog->setup(pInStream, pOutStream, &param);

		if (prog->getVersion() == 0 || prog->getVersion() >= param.version)
		{
			nResult = prog->process();
			
			if (nResult == PRC_NOTIMP)  //procedure is not implemented
			{
				pOutStream->seek(-4, SEEK_CUR);
				pOutStream->write(PROC_UNAVAIL);
			}
			else if (nResult == PRC_FAIL)  //input data is truncated
			{
				pOutStream->seek(-4, SEEK_CUR);
				pOutStream->write(GARBAGE_ARGS);
			}
		}
		else //program version mismatch
		{
			pOutStream->seek(-4, SEEK_CUR);
			pOutStream->write(PROG_MISMATCH);
			pOutStream->write(1);                  //lowest accepted version
			pOutStream->write(prog->getVersion()); //highest accepted version
		}
	}

	if (sockType == SOCK_STREAM)
	{
		size_t endPos = pOutStream->getPosition();  //remember current position
		pOutStream->seek(headerPos, SEEK_SET);  //seek to the position of head
		header.header = 0x80000000 + (endPos - (headerPos + 4));  //size of output data
		pOutStream->write(header.header);  //update header
	}
	return nResult;
}
