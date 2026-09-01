/*
 * Socket
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

#include "csocket.h"

#ifdef _WIN32
typedef char recv_data_t;
#else
typedef void recv_data_t;
#endif


void sock_close(sock_t socket) {
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#else
    shutdown(socket, SHUT_RDWR);
    close(socket);
#endif
}

static int ThreadProc(void *lpParameter) {
    csocket_run((struct csocket_t*)lpParameter);
    return 0;
}

struct csocket_t* csocket_init(int nType, int serverPort, void* server) {
    struct csocket_t* cs = (struct csocket_t*)malloc(sizeof(struct csocket_t));
    cs->m_nType  = nType;
    cs->m_Socket = INVALID_SOCKET;
    cs->m_pListener = NULL;
    cs->m_nActive = 0;
    cs->m_hThread = NULL;
    cs->m_serverPort = serverPort;
    cs->m_pServer = server;
    memset(&cs->m_RemoteAddr, 0, sizeof(cs->m_RemoteAddr));
    cs->m_Input  = xdr_init();
    cs->m_Output = xdr_init();
    return cs;
}

struct csocket_t* csocket_uninit(struct csocket_t* cs) {
    csocket_close(cs);
    xdr_uninit(cs->m_Input);
    xdr_uninit(cs->m_Output);
    free(cs);
    return NULL;
}

void csocket_open(struct csocket_t* cs, sock_t socket, socket_listener_t* pListener, struct sockaddr_in *pRemoteAddr) {
    csocket_close(cs);
    
    cs->m_Socket = socket;
    cs->m_pListener = pListener;
    if (pRemoteAddr != NULL)
        cs->m_RemoteAddr = *pRemoteAddr;
    if (cs->m_Socket != INVALID_SOCKET) {
        cs->m_nActive = 1;
        cs->m_hThread = host_thread_create(ThreadProc, "CSocket", (void*)cs);
    }
}

void csocket_close(struct csocket_t* cs) {
    if (cs->m_Socket != INVALID_SOCKET) {
        sock_close(cs->m_Socket);
        cs->m_Socket = INVALID_SOCKET;
    }
    if (cs->m_hThread) {
        host_thread_wait(cs->m_hThread);
        cs->m_hThread = NULL;
    }
}

void csocket_send(struct csocket_t* cs) {
    ssize_t nBytes = 0;
    
    if (cs->m_Socket == INVALID_SOCKET)
        return;
    
    if (cs->m_nType == SOCK_STREAM) {
        xdr_write_long_at(cs->m_Output->head, 0x80000000 | cs->m_Output->size); /* output header */
        cs->m_Output->size += 4;
        nBytes = send(cs->m_Socket, (const char *)cs->m_Output->head, cs->m_Output->size, 0);
    } else if (cs->m_nType == SOCK_DGRAM)
        nBytes = sendto(cs->m_Socket, (const char *)cs->m_Output->head, cs->m_Output->size, 0, (struct sockaddr *)&cs->m_RemoteAddr, sizeof(struct sockaddr));
    
    if (nBytes < 0)
        perror("[RPC] Socket send");
    else if (nBytes != cs->m_Output->size)
        perror("[RPC] Socket send, size mismatch");
}

void csocket_run(struct csocket_t* cs) {
    socklen_t nSize;
    ssize_t   nBytes = 0;
    
    for (;;) {
        if (cs->m_nType == SOCK_STREAM)
            nBytes = recv(cs->m_Socket, (recv_data_t*)cs->m_Input->head, cs->m_Input->capacity, 0);
        else if (cs->m_nType == SOCK_DGRAM) {
            nSize = sizeof(cs->m_RemoteAddr);
            nBytes = recvfrom(cs->m_Socket, (recv_data_t*)cs->m_Input->head, cs->m_Input->capacity, 0, (struct sockaddr *)&cs->m_RemoteAddr, &nSize);
        }
        if (nBytes == 0) {
            perror("[RPC] Socket closed");
            break;
        }
        else if (nBytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        else if (nBytes > 0) {
            cs->m_Input->data  = cs->m_Input->head;
            cs->m_Output->data = cs->m_Output->head;
            if (cs->m_nType == SOCK_STREAM) {
                ssize_t  nExtra;
                uint32_t nLen = ~0x80000000 & xdr_read_long_at(cs->m_Input->head); /* input header */
                cs->m_Input->data  += 4; /* offset to data */
                cs->m_Output->data += 4; /* reserve room for output header */
                nBytes -= 4; /* subtract header size */
                if (nBytes < nLen) {
                    do {
                        nExtra = recv(cs->m_Socket, (recv_data_t*)(cs->m_Input->head+nBytes+4), cs->m_Input->capacity-nBytes-4, 0);
                        if (nExtra > 0) {
                            nBytes += nExtra;
                        }
                    } while (nBytes < nLen && (nExtra > 0 || (nExtra == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))));
                    
                    if (nExtra <= 0) {
                        perror("[RPC] Missing data");
                        break;
                    }
                }
            }
            cs->m_Input->size  = (uint32_t)nBytes;
            cs->m_Output->size = 0;
            
            if (cs->m_pListener != NULL)
                cs->m_pListener(cs); /* notify listener */
        } else {
            if (errno != EBADF) {
                perror("[RPC] Socket receive");
            }
            break;
        }
    }
    cs->m_nActive = 0;
}
