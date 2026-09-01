/*
 * TCP Server Socket
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

#include "tcpsocket.h"


#define BACKLOG 16

static int ThreadProc(void *lpParameter) {
    tcpsocket_run((struct tcpsocket_t*)lpParameter);
    return 0;
}

struct tcpsocket_t* tcpsocket_init(socket_listener_t* pListener, void* pServer) {
    struct tcpsocket_t* ts = (struct tcpsocket_t*)malloc(sizeof(struct tcpsocket_t));
    if (ts) {
        ts->m_nPort        = 0;
        ts->m_Server       = pServer;
        ts->m_ServerSocket = 0;
        ts->m_pSockets     = NULL;
        ts->m_nClosed      = 1;
        ts->m_pListener    = pListener;
        ts->m_hThread      = NULL;
    }
    return ts;
}

struct tcpsocket_t* tcpsocket_uninit(struct tcpsocket_t* ts) {
    tcpsocket_close(ts);
    free(ts);
    return NULL;
}

uint16_t tcpsocket_open(struct tcpsocket_t* ts, uint16_t nPort) {
    struct sockaddr_in localAddr;
    int i;
    
    tcpsocket_close(ts);
    
    ts->m_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ts->m_ServerSocket == INVALID_SOCKET)
        return 0;
    
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr = loopback_addr;
    if (bind(ts->m_ServerSocket, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) {
        sock_close(ts->m_ServerSocket);
        return 0;
    }
    
    socklen_t size = sizeof(localAddr);
    if(getsockname(ts->m_ServerSocket,  (struct sockaddr *)&localAddr, &size) < 0) {
        sock_close(ts->m_ServerSocket);
        return 0;
    }
    ts->m_nPort = nPort ? nPort : ntohs(localAddr.sin_port);
    
    if (listen(ts->m_ServerSocket, BACKLOG) < 0) {
        sock_close(ts->m_ServerSocket);
        return 0;
    }
    
    ts->m_pSockets = (struct csocket_t**)malloc(sizeof(struct csocket_t*[BACKLOG]));
    for (i = 0; i < BACKLOG; i++)
        ts->m_pSockets[i] = csocket_init(SOCK_STREAM, ts->m_nPort, ts->m_Server);
    
    ts->m_nClosed = 0;
    ts->m_hThread = host_thread_create(ThreadProc, "ServerSocket", (void*)ts);
    return ntohs(localAddr.sin_port);
}

void tcpsocket_close(struct tcpsocket_t* ts) {
    int i;
    
    if (ts->m_nClosed)
        return;
    
    ts->m_nClosed = 1;
    sock_close(ts->m_ServerSocket);
    
    if (ts->m_hThread != NULL) {
        host_thread_wait(ts->m_hThread);
    }
    
    if (ts->m_pSockets != NULL) {
        for (i = 0; i < BACKLOG; i++)
            ts->m_pSockets[i] = csocket_uninit(ts->m_pSockets[i]);
        free(ts->m_pSockets);
        ts->m_pSockets = NULL;
    }
}

void tcpsocket_run(struct tcpsocket_t* ts) {
    int i;
    socklen_t nSize;
    struct sockaddr_in remoteAddr;
    sock_t socket;
    
    nSize = sizeof(remoteAddr);
    while (!(ts->m_nClosed)) {
        socket = accept(ts->m_ServerSocket, (struct sockaddr *)&remoteAddr, &nSize); /* accept connection */
        if (socket != INVALID_SOCKET) {
            for (i = 0; i < BACKLOG; i++) {
                if (!(ts->m_pSockets[i]->m_nActive)) { /* find an inactive CSocket */
                    csocket_open(ts->m_pSockets[i], socket, ts->m_pListener, &remoteAddr); /* receive input data */
                    break;
                }
            }
        }
    }
}
