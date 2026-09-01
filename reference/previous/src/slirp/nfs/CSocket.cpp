#ifdef _WIN32
#include <Winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "CSocket.h"
#include "nfsd.h"

using namespace std;

#ifdef _WIN32
typedef char recv_data_t;
#else
typedef void recv_data_t;
#endif

static int ThreadProc(void *lpParameter)
{
	CSocket *pSocket;

	pSocket = (CSocket *)lpParameter;
	pSocket->run();
	return 0;
}

CSocket::CSocket(int nType, int serverPort)
  : m_nType(nType)
  , m_Socket(-1)
  , m_pListener(NULL)
  , m_bActive(false)
  , m_hThread(NULL)
  , m_serverPort(serverPort)
{
	memset(&m_RemoteAddr, 0, sizeof(m_RemoteAddr));
}

CSocket::~CSocket() {
	close();
}

int CSocket::getType() {
	return m_nType;  //socket type
}

int CSocket::getServerPort() {
    return m_serverPort;
}

void CSocket::open(int socket, ISocketListener *pListener, struct sockaddr_in *pRemoteAddr) {
	close();

	m_Socket = socket;  //socket
	m_pListener = pListener;  //listener
	if (pRemoteAddr != NULL)
		m_RemoteAddr = *pRemoteAddr;  //remote address
	if (m_Socket != INVALID_SOCKET)
	{
		m_bActive = true;
		m_hThread = host_thread_create(ThreadProc, "CSocket", this);  //begin thread
	}
}

void CSocket::close(void) {
	if (m_Socket != INVALID_SOCKET) {
		::close(m_Socket);
		m_Socket = INVALID_SOCKET;
	}

    m_hThread = NULL;
}

void CSocket::send(void) {
	if (m_Socket == INVALID_SOCKET)
		return;

    ssize_t nBytes = 0;
	if (m_nType == SOCK_STREAM)
		nBytes = ::send(m_Socket, (const char *)m_Output.data(), m_Output.size(), 0);
	else if (m_nType == SOCK_DGRAM)
		nBytes = sendto(m_Socket, (const char *)m_Output.data(), m_Output.size(), 0, (struct sockaddr *)&m_RemoteAddr, sizeof(struct sockaddr));
    
    if(nBytes < 0)
        perror("[NFSD] Socket send");
    else if(nBytes != m_Output.size())
        perror("[NFSD] Socket send, size mismatch");
    m_Output.reset();  //clear output buffer
}

bool CSocket::active(void) {
	return m_bActive;  //thread is active or not
}

const char* CSocket::getRemoteAddress(void) {
    return inet_ntoa(m_RemoteAddr.sin_addr);
}

int CSocket::getRemotePort(void) {
    return htons(m_RemoteAddr.sin_port);
}

XDRInput* CSocket::getInputStream(void) {
	return &m_Input;
}

XDROutput* CSocket::getOutputStream(void) {
	return &m_Output;
}

void CSocket::run(void) {
    socklen_t nSize;

    ssize_t nBytes = 0;
    ssize_t nExtra = 0;

	for (;;) {
        uint32_t header = 0;
		if (m_nType == SOCK_STREAM)
			nBytes = recv(m_Socket, (recv_data_t*)m_Input.data(), m_Input.getCapacity(), 0);
        else if (m_nType == SOCK_DGRAM) {
            nSize = sizeof(m_RemoteAddr);
			nBytes = recvfrom(m_Socket, (recv_data_t*)m_Input.data(), m_Input.getCapacity(), 0, (struct sockaddr *)&m_RemoteAddr, &nSize);
        }
        if(nBytes == 0) {
            perror("[NFSD] Socket closed");
            break;
        }
        else if(nBytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
		else if (nBytes > 0) {
			m_Input.resize(nBytes);  //bytes received
            if (m_nType == SOCK_STREAM) {
                m_Input.read(&header);
                uint32_t nLen = (header & ~0x80000000) + 4;
                if (nBytes < nLen) {
                    do {
                        nExtra = recv(m_Socket, (recv_data_t*)(m_Input.data()+nBytes), m_Input.getCapacity()-nBytes, 0);
                        if (nExtra > 0) {
                            nBytes += nExtra;
                            m_Input.resize(nBytes);
                            m_Input.skip(4);
                        }
                    } while (nBytes < nLen && (nExtra > 0 || (nExtra == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))));
                    
                    if (nExtra <= 0) {
                        perror("[NFSD] Missing data");
                        break;
                    }
                }
            }
			if (m_pListener != NULL)
				m_pListener->socketReceived(this, header);  //notify listener
        } else {
            perror("[NFSD] Socket recv");
            break;
        }
	}
	m_bActive = false;
}
