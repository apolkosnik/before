#include <assert.h>

#include "TCPServerSocket.h"
#include "nfsd.h"

static const int BACKLOG = 16;

static int ThreadProc(void *lpParameter) {
	((TCPServerSocket *)lpParameter)->run();
	return 0;
}

TCPServerSocket::TCPServerSocket(ISocketListener *pListener) : m_nPort(0), m_ServerSocket(0), m_bClosed(true), m_pListener(pListener), m_hThread(NULL), m_pSockets(NULL) {}

TCPServerSocket::~TCPServerSocket() {
	close();
}

bool TCPServerSocket::open(int progNum, uint16_t nPort) {
	struct sockaddr_in localAddr;
	int i;

	close();

    m_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_ServerSocket == INVALID_SOCKET)
        return false;
    
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(nPort ? TCPServerSocket::toLocalPort(nPort) : nPort);
    localAddr.sin_addr = loopback_addr;
    if (bind(m_ServerSocket, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) {
        ::close(m_ServerSocket);
        return false;
    }
    
    socklen_t size = sizeof(localAddr);
    if(getsockname(m_ServerSocket,  (struct sockaddr *)&localAddr, &size) < 0) {
        ::close(m_ServerSocket);
        return false;
    }
    m_nPort = nPort == 0 ? ntohs(localAddr.sin_port) : nPort;
    TCPServerSocket::portMap(progNum, m_nPort, ntohs(localAddr.sin_port));

    if (listen(m_ServerSocket, BACKLOG) < 0) {
        ::close(m_ServerSocket);
        return false;
    }
    
	m_pSockets = new CSocket* [BACKLOG];
	for (i = 0; i < BACKLOG; i++)
		m_pSockets[i] = new CSocket(SOCK_STREAM, getPort());
	
	m_bClosed = false;
    m_hThread = host_thread_create(ThreadProc, "ServerSocket", this);
	return true;
}

void TCPServerSocket::close(void)
{
	int i;

	if (m_bClosed)
		return;
	m_bClosed = true;

	::close(m_ServerSocket);

    //CSocket::unmapTCP(m_nPort);
    TCPServerSocket::portUnmap(m_nPort);
    
	if (m_hThread != NULL)
	{
		host_thread_wait(m_hThread);
	}

	if (m_pSockets != NULL)
	{
		for (i = 0; i < BACKLOG; i++)
			delete m_pSockets[i];
		delete[] m_pSockets;
		m_pSockets = NULL;
	}
}

int TCPServerSocket::getPort(void) {
	return m_nPort;
}

void TCPServerSocket::run(void) {
    int i;
    socklen_t nSize;
	struct sockaddr_in remoteAddr;
	int socket;

	nSize = sizeof(remoteAddr);
	while (!m_bClosed) {
        socket = accept(m_ServerSocket, (struct sockaddr *)&remoteAddr, &nSize);  //accept connection
		if (socket != INVALID_SOCKET) {
			for (i = 0; i < BACKLOG; i++)
				if (!m_pSockets[i]->active()) {   //find an inactive CSocket
					m_pSockets[i]->open(socket, m_pListener, &remoteAddr);  //receive input data
					break;
				}
		}
	}
}

lock_t                       TCPServerSocket::natLock;
std::map<uint16_t, uint16_t> TCPServerSocket::toLocal;
std::map<uint16_t, uint16_t> TCPServerSocket::fromLocal;


void TCPServerSocket::portMap(int progNum, uint16_t src, uint16_t local) {
    assert(local);
    host_lock(&natLock);
    toLocal[src] = local;
    fromLocal[local] = src;
    host_unlock(&natLock);
}

void TCPServerSocket::portUnmap(uint16_t src) {
    host_lock(&natLock);
    uint16_t local = toLocal[src];
    toLocal.erase(src);
    fromLocal.erase(local);
    host_unlock(&natLock);
}

uint16_t TCPServerSocket::toLocalPort(uint16_t src) {
    assert(src);
    host_lock(&natLock);
    uint16_t result = toLocal[src];
    host_unlock(&natLock);
    return result;
}

uint16_t TCPServerSocket::fromLocalPort(uint16_t local) {
    assert(local);
    host_lock(&natLock);
    uint16_t result = fromLocal[local];
    host_unlock(&natLock);
    return result;
}
