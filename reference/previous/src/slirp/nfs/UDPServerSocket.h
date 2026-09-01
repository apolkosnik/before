#ifndef _DATAGRAMSOCKET_H_
#define _DATAGRAMSOCKET_H_

#include "SocketListener.h"
#include "CSocket.h"
#include <map>
#include "host.h"

class UDPServerSocket {
public:
	UDPServerSocket(ISocketListener* pListener);
	~UDPServerSocket();
	bool open(int porgNum, uint16_t nPort = 0);
	void close(void);
	int  getPort(void);
	void run(void);

    static void      portMap(int progNum, uint16_t src, uint16_t local);
    static void      portUnmap(uint16_t src);
    static uint16_t  toLocalPort(uint16_t src);
    static uint16_t  fromLocalPort(uint16_t local);
private:
	int              m_nPort;
	int              m_Socket;
	CSocket*         m_pSocket;
	bool             m_bClosed;
	ISocketListener* m_pListener;
    
    static lock_t                       natLock;
    static std::map<uint16_t, uint16_t> toLocal;
    static std::map<uint16_t, uint16_t> fromLocal;
};

#endif
