#ifndef _CSOCKET_H_
#define _CSOCKET_H_

#ifdef _WIN32
#include <Winsock2.h>
#else
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#endif
#include <unistd.h>

#include "SocketListener.h"
#include "XDRStream.h"
#include "host.h"
#include "slirpmain.h"

class CSocket
{
public:
	CSocket(int nType, int serverPort);
	virtual       ~CSocket();
	int            getType(void);
	void           open(int socket, ISocketListener *pListener, struct sockaddr_in *pRemoteAddr = NULL);
	void           close(void);
	void           send(void);
	bool           active(void);
	const char*    getRemoteAddress(void);
	int            getRemotePort(void);
	XDRInput*      getInputStream(void);
	XDROutput*     getOutputStream(void);
	void           run(void);
    int            getServerPort(void);
    
    static uint16_t map_and_htons(int type, uint16_t port);
    static void     unmapTCP(uint16_t port);
    static void     unmapUDP(uint16_t port);    
private:
	int                m_nType;
	int                m_Socket;
	struct sockaddr_in m_RemoteAddr;
    ISocketListener*   m_pListener;
	bool               m_bActive;
	thread_t*          m_hThread;
    XDRInput           m_Input;
    XDROutput          m_Output;
    int                m_serverPort;
};

#ifndef _WIN32
const int INVALID_SOCKET = -1;
#endif

#endif
