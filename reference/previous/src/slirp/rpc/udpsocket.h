/* UDP Server Socket */

#ifndef _UDPSOCKET_H_
#define _UDPSOCKET_H_

#include "csocket.h"

struct udpsocket_t {
    uint16_t           m_nPort;
    void*              m_Server;
    sock_t             m_Socket;
    struct csocket_t*  m_pSocket;
    int                m_nClosed;
    socket_listener_t* m_pListener;
};

struct udpsocket_t* udpsocket_init(socket_listener_t* pListener, void* pServer);
struct udpsocket_t* udpsocket_uninit(struct udpsocket_t* us);

uint16_t udpsocket_open(struct udpsocket_t* us, uint16_t nPort);
void udpsocket_close(struct udpsocket_t* us);
void udpspcket_run(struct udpsocket_t* us);

#endif /* _UDPSOCKET_H_ */
