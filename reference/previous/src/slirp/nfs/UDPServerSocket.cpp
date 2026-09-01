#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <assert.h>

#include "UDPServerSocket.h"
#include "nfsd.h"

UDPServerSocket::UDPServerSocket(ISocketListener *pListener) : m_nPort(0), m_Socket(0), m_pSocket(NULL), m_bClosed(true), m_pListener(pListener) {}

UDPServerSocket::~UDPServerSocket() {
	close();
}

bool UDPServerSocket::open(int progNum, uint16_t nPort) {
	struct sockaddr_in localAddr;

	close();

    m_Socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_Socket == INVALID_SOCKET)
        return false;
    
    socklen_t size = 64 * 1024;
    socklen_t len  = sizeof(size);
#ifdef _WIN32
    setsockopt(m_Socket, SOL_SOCKET, SO_SNDBUF, (const char*)&size, len);
    setsockopt(m_Socket, SOL_SOCKET, SO_RCVBUF, (const char*)&size, len);
#else
    setsockopt(m_Socket, SOL_SOCKET, SO_SNDBUF, &size, len);
    setsockopt(m_Socket, SOL_SOCKET, SO_RCVBUF, &size, len);
#endif
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(nPort ? UDPServerSocket::toLocalPort(nPort) : nPort);
    localAddr.sin_addr = loopback_addr;
    if (bind(m_Socket, (struct sockaddr *)&localAddr, sizeof(struct sockaddr)) < 0) {
        ::close(m_Socket);
        return false;
    }
    
    size = sizeof(localAddr);
    if(getsockname(m_Socket, (struct sockaddr *)&localAddr, &size) < 0) {
        ::close(m_Socket);
        return false;
    }
    
    m_nPort = nPort == 0 ? ntohs(localAddr.sin_port) : nPort;
    UDPServerSocket::portMap(progNum, m_nPort, ntohs(localAddr.sin_port));

	m_bClosed = false;
	m_pSocket = new CSocket(SOCK_DGRAM, getPort());
	m_pSocket->open(m_Socket, m_pListener);  //wait for receiving data
	return true;
}

void UDPServerSocket::close(void)
{
	if (m_bClosed)
		return;

	m_bClosed = true;
	::close(m_Socket);
    //CSocket::unmapUDP(m_nPort);
    portUnmap(m_nPort);
	delete m_pSocket;
}

int UDPServerSocket::getPort(void) {
	return m_nPort;
}

lock_t                       UDPServerSocket::natLock;
std::map<uint16_t, uint16_t> UDPServerSocket::toLocal;
std::map<uint16_t, uint16_t> UDPServerSocket::fromLocal;

void UDPServerSocket::portMap(int progNum, uint16_t src, uint16_t local) {
    assert(local);
    host_lock(&natLock);
    toLocal[src] = local;
    fromLocal[local] = src;
    host_unlock(&natLock);
}

void UDPServerSocket::portUnmap(uint16_t src) {
    host_lock(&natLock);
    uint16_t local = toLocal[src];
    toLocal.erase(src);
    fromLocal.erase(local);
    host_unlock(&natLock);
}

uint16_t UDPServerSocket::toLocalPort(uint16_t src) {
    assert(src);
    host_lock(&natLock);
    uint16_t result = toLocal[src];
    host_unlock(&natLock);
    return result;
}

uint16_t UDPServerSocket::fromLocalPort(uint16_t local) {
    assert(local);
    host_lock(&natLock);
    uint16_t result = fromLocal[local];
    host_unlock(&natLock);
    return result;
}
