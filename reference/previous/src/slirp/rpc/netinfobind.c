/*
 * NetInfo Bind Program
 * 
 * Created by Simon Schubiger on 09.01.2021
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

#include "rpc.h"
#include "netinfobind.h"


struct nidb_t* nidb;

const struct rpc_prog_t ni_rpc_prog_template = 
{
    NETINFOPROG, NETINFOVERS, 0, 0, netinfo_prog, 1, "NETINFO", NULL, NULL
};

const struct nireg_t ni_register_template[] = 
{
/*  { "local",   0,            0,            NULL, NULL, NULL }, */
    { "network", PORT_NETINFO, PORT_NETINFO, NULL, NULL, NULL }
};

#if 0
static void ni_register_program(struct ni_prog_t* prog) {
    prog->udp_port = prog->udp_prog->port;
    prog->tcp_port = prog->tcp_prog->port;
    printf("[NETINFOBIND] Registering '%s' at udp:%d, tcp:%d\n", prog->tag, prog->udp_port, prog->tcp_port);
}
#endif

static void ni_register_add(struct rpc_t* rpc, struct nireg_t* nireg) {
    struct nireg_t** entry = &rpc->nireg;
        
    nireg->udp_prog = (struct rpc_prog_t*)malloc(sizeof(struct rpc_prog_t));
    nireg->tcp_prog = (struct rpc_prog_t*)malloc(sizeof(struct rpc_prog_t));
    
    *(nireg->udp_prog) = ni_rpc_prog_template;
    nireg->udp_prog->prot = IPPROTO_UDP;
    nireg->udp_prog->port = nireg->udp_port;
    
    *(nireg->tcp_prog) = ni_rpc_prog_template;
    nireg->tcp_prog->prot = IPPROTO_TCP;
    nireg->tcp_prog->port = nireg->tcp_port;

    rpc_add_program(rpc, nireg->udp_prog);
    rpc_add_program(rpc, nireg->tcp_prog);
    
    nireg->udp_port = nireg->udp_prog->port;
    nireg->tcp_port = nireg->tcp_prog->port;
    
    printf("[NETINFOBIND] Registering '%s' at udp:%d, tcp:%d\n", nidb->tag, nireg->udp_port, nireg->tcp_port);

    while (*entry) {
        entry = &(*entry)->next;
    }
    
    *entry = nireg;
    (*entry)->next = NULL;
}

void nibind_init(struct rpc_t* rpc) {
    size_t i;
    struct nireg_t* nireg;
    
    for (i = 0; i < TBL_SIZE(ni_register_template); i++) {
        nireg = (struct nireg_t*)malloc(sizeof(struct nireg_t));
        memcpy(nireg, &ni_register_template[i], sizeof(struct nireg_t));
        ni_register_add(rpc, nireg);
    }
}

void nibind_uninit(struct rpc_t* rpc) {
    struct nireg_t* next;
    
    while (rpc->nireg) {
        next = rpc->nireg->next;
        free(rpc->nireg);
        rpc->nireg = next;
    }
}


static int proc_register(struct rpc_t* rpc) {
    rpc_log(rpc, "REGISTER unimplemented");

    return RPC_PROC_UNAVAIL;
}
static int proc_unregister(struct rpc_t* rpc) {
    rpc_log(rpc, "UNREGISTER unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_getregister(struct rpc_t* rpc) {
    char tag[MAXNAMELEN+1];
    struct nireg_t* nireg = rpc->nireg;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (xdr_read_string(m_in, tag, sizeof(tag)) < 0) return RPC_GARBAGE_ARGS;
    
    while (nireg) {
        if (strncmp(nireg->tag, tag, MAXNAMELEN) == 0) {
            break;
        }
        nireg = nireg->next;
    }
    
    if (nireg == NULL) {
        rpc_log(rpc, "GETREGISTER no tag '%s'", tag);
        xdr_write_long(m_out, NI_NOTAG);
    } else {
        rpc_log(rpc, "GETREGISTER '%s' at udp:%d, tcp:%d", tag, nireg->udp_port, nireg->tcp_port);
        xdr_write_long(m_out, NI_OK);
        xdr_write_long(m_out, nireg->udp_port);
        xdr_write_long(m_out, nireg->tcp_port);
    }
    
    return RPC_SUCCESS;
}

static int proc_listreg(struct rpc_t* rpc) {
    struct nireg_t* nireg = rpc->nireg;
    
    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "LISTREG");
    
    xdr_write_long(m_out, NI_OK);
    
    while (nireg) {
        xdr_write_long(m_out, 1);
        xdr_write_string(m_out, nireg->tag, MAXNAMELEN);
        xdr_write_long(m_out, nireg->udp_port);
        xdr_write_long(m_out, nireg->tcp_port);
        nireg = nireg->next;
    }
    
    xdr_write_long(m_out, 0);
    
    return RPC_SUCCESS;
}

static int proc_createmaster(struct rpc_t* rpc) {
    rpc_log(rpc, "CREATEMASTER unimplemented");
    
    return RPC_PROC_UNAVAIL;
}

static int proc_createclone(struct rpc_t* rpc) {
    rpc_log(rpc, "CREATECLONE unimplemented");
    
    return RPC_PROC_UNAVAIL;
}

static int proc_destroydomain(struct rpc_t* rpc) {
    rpc_log(rpc, "DESTROYDOMAIN unimplemented");
    
    return RPC_PROC_UNAVAIL;
}

static int proc_bind(struct rpc_t* rpc) {
    char client[16];
    uint32_t clientAddr;
    char clientTag[MAXNAMELEN+1];
    char serverTag[MAXNAMELEN+1];

    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (m_in->size < 4) return RPC_GARBAGE_ARGS;
    clientAddr = xdr_read_long(m_in);
    
    if (xdr_read_string(m_in, clientTag, sizeof(clientTag)) < 0) return RPC_GARBAGE_ARGS;
    if (xdr_read_string(m_in, serverTag, sizeof(serverTag)) < 0) return RPC_GARBAGE_ARGS;
    
    rpc_log(rpc, "BIND '%s/%s' to '%s'", rpc_ip_str(client, clientAddr, 4, sizeof(client)), clientTag, serverTag);
    
    xdr_write_long(m_out, NI_OK);
    
    return RPC_SUCCESS;
}


int nibind_prog(struct rpc_t* rpc) {
    switch (rpc->proc) {
        case NIBINDPROC_NULL:
            return proc_null(rpc);
            
        case NIBINDPROC_REGISTER:
            return proc_register(rpc);
            
        case NIBINDPROC_UNREGISTER:
            return proc_unregister(rpc);
            
        case NIBINDPROC_GETREGISTER:
            return proc_getregister(rpc);
            
        case NIBINDPROC_LISTREG:
            return proc_listreg(rpc);
            
        case NIBINDPROC_CREATEMASTER:
            return proc_createmaster(rpc);
            
        case NIBINDPROC_CREATECLONE:
            return proc_createclone(rpc);
            
        case NIBINDPROC_DESTROYDOMAIN:
            return proc_destroydomain(rpc);
            
        case NIBINDPROC_BIND:
            return proc_bind(rpc);
            
        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}
