#ifndef _SERVERSOCKET_H_
#define _SERVERSOCKET_H_

#include "SocketListener.h"
#include "CSocket.h"
#include <map>
#include "host.h"

class TCPServerSocket {
public:
	TCPServerSocket(ISocketListener* pListener);
	~TCPServerSocket();
	bool open(int progNum, uint16_t port = 0);
	void close(void);
	int  getPort(void);
	void run(void);

    static void      portMap(int progNum, uint16_t src, uint16_t local);
    static void      portUnmap(uint16_t src);
    static uint16_t  toLocalPort(uint16_t src);
    static uint16_t  fromLocalPort(uint16_t local);
private:
	uint16_t         m_nPort;
	int              m_ServerSocket;
	bool             m_bClosed;
	ISocketListener *m_pListener;
	thread_t*        m_hThread;
	CSocket**        m_pSockets;
    
    static lock_t                       natLock;
    static std::map<uint16_t, uint16_t> toLocal;
    static std::map<uint16_t, uint16_t> fromLocal;
};

#endif
