/*
 * UDP Server Socket
 * 
 * Created by Simon Schubiger
 * Rewritten in C by Andreas Grabher
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <slirp.h>
#include <stdlib.h>

#include "udpsocket.h"


struct udpsocket_t* udpsocket_init(socket_listener_t* pListener, void* pServer) {
    struct udpsocket_t* us = (struct udpsocket_t*)malloc(sizeof(struct udpsocket_t));
    if (us) {
        us->m_nPort     = 0;
        us->m_Server    = pServer;
        us->m_Socket    = 0;
        us->m_pSocket   = NULL;
        us->m_nClosed   = 1;
        us->m_pListener = pListener;
    }
    return us;
}

struct udpsocket_t* udpsocket_uninit(struct udpsocket_t* us) {
    udpsocket_close(us);
    free(us);
    return NULL;
}

uint16_t udpsocket_open(struct udpsocket_t* us, uint16_t nPort) {
    struct sockaddr_in localAddr;
    
    udpsocket_close(us);
    
    us->m_Socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (us->m_Socket == INVALID_SOCKET)
        return 0;
    
    socklen_t size = 64 * 1024;
    socklen_t len  = sizeof(size);
#ifdef _WIN32
    setsockopt(us->m_Socket, SOL_SOCKET, SO_SNDBUF, (const char*)&size, len);
    setsockopt(us->m_Socket, SOL_SOCKET, SO_RCVBUF, (const char*)&size, len);
#else
    setsockopt(us->m_Socket, SOL_SOCKET, SO_SNDBUF, &size, len);
    setsockopt(us->m_Socket, SOL_SOCKET, SO_RCVBUF, &size, len);
#endif
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr = loopback_addr;
    if (bind(us->m_Socket, (struct sockaddr *)&localAddr, sizeof(struct sockaddr)) < 0) {
        sock_close(us->m_Socket);
        return 0;
    }
    
    size = sizeof(localAddr);
    if (getsockname(us->m_Socket, (struct sockaddr *)&localAddr, &size) < 0) {
        sock_close(us->m_Socket);
        return 0;
    }
    
    us->m_nPort = nPort ? nPort : ntohs(localAddr.sin_port);
    us->m_nClosed = 0;
    us->m_pSocket = csocket_init(SOCK_DGRAM, us->m_nPort, us->m_Server);
    if (us->m_pSocket) {
        csocket_open(us->m_pSocket, us->m_Socket, us->m_pListener, NULL); /* wait for receiving data */
        return ntohs(localAddr.sin_port);
    }
    return 0;
}

void udpsocket_close(struct udpsocket_t* us) {
    if (us->m_nClosed)
        return;
    
    us->m_nClosed = 1;
    us->m_pSocket = csocket_uninit(us->m_pSocket);
}
