/* Socket */

#ifndef _CSOCKET_H_
#define _CSOCKET_H_

#include "host.h"
#include "xdr.h"

struct csocket_t;

typedef void (socket_listener_t)(struct csocket_t* pSocket);

#ifdef _WIN32
typedef SOCKET sock_t;
#else
typedef int sock_t;
#define INVALID_SOCKET -1
#endif

void sock_close(sock_t socket);

struct csocket_t {
    int                m_nType;
    sock_t             m_Socket;
    struct sockaddr_in m_RemoteAddr;
    socket_listener_t* m_pListener;
    int                m_nActive;
    thread_t*          m_hThread;
    struct xdr_t*      m_Input;
    struct xdr_t*      m_Output;
    int                m_serverPort;
    void*              m_pServer;
};

struct csocket_t* csocket_init(int nType, int serverPort, void* server);
struct csocket_t* csocket_uninit(struct csocket_t* cs);

void csocket_run(struct csocket_t* cs);
void csocket_open(struct csocket_t* cs, sock_t socket, socket_listener_t* pListener, struct sockaddr_in *pRemoteAddr);
void csocket_close(struct csocket_t* cs);
void csocket_send(struct csocket_t* cs);

#endif /* _CSOCKET_H_ */
