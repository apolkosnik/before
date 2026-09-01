/* TCP Server Socket */

#ifndef _TCPSOCKET_H_
#define _TCPSOCKET_H_

#include "csocket.h"

struct tcpsocket_t {
    uint16_t           m_nPort;
    void*              m_Server;
    sock_t             m_ServerSocket;
    struct csocket_t** m_pSockets;
    int                m_nClosed;
    socket_listener_t* m_pListener;
    thread_t*          m_hThread;
};

struct tcpsocket_t* tcpsocket_init(socket_listener_t* pListener, void* pServer);
struct tcpsocket_t* tcpsocket_uninit(struct tcpsocket_t* ts);

uint16_t tcpsocket_open(struct tcpsocket_t* ts, uint16_t nPort);
void tcpsocket_close(struct tcpsocket_t* ts);
void tcpsocket_run(struct tcpsocket_t* ts);

#endif /* _TCPSOCKET_H_ */
