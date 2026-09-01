/*
 * Portmap Program
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

#include "rpc.h"
#include "portmap.h"


static int proc_getport(struct rpc_t* rpc) {
    uint32_t port;
    struct rpc_prog_t* prog = rpc->prog_list;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "GETPORT");
    
    if (m_in->size < 4 * 4) return RPC_GARBAGE_ARGS;
    rpc->prog = xdr_read_long(m_in);
    rpc->vers = xdr_read_long(m_in);
    rpc->prot = xdr_read_long(m_in);
    xdr_read_skip(m_in, 4); /* port field is ignored */
    
    port = 0; /* port 0 means program not registered */
    
    while (prog) {
        if (rpc_match_prog(rpc, prog) != 0) { /* version mismatch accepted */
            port = prog->port;
            break;
        }
        prog = prog->next;
    }
    
    xdr_write_long(m_out, port);
    
    return RPC_SUCCESS;
}

static int proc_dump(struct rpc_t* rpc) {
    struct rpc_prog_t* prog = rpc->prog_list;

    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "DUMP");
    
    while (prog) {
        xdr_write_long(m_out, 1);
        xdr_write_long(m_out, prog->prog);
        xdr_write_long(m_out, prog->vers);
        xdr_write_long(m_out, prog->prot);
        xdr_write_long(m_out, prog->port);
        prog = prog->next;
    }
    
    xdr_write_long(m_out, 0);    
    
    return RPC_SUCCESS;
}

static int proc_callit(struct rpc_t* rpc) {
    int result;
    uint32_t before;
    uint8_t* size_ptr;
    struct rpc_prog_t* prog = rpc->prog_list;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "CALLIT");
    
    if (m_in->size < 4 * 4) return RPC_GARBAGE_ARGS;
    rpc->prog = xdr_read_long(m_in);
    rpc->vers = xdr_read_long(m_in);
    rpc->proc = xdr_read_long(m_in);
    rpc->prot = xdr_read_long(m_in);
    rpc->prot = IPPROTO_UDP; /* protocol is ignored, always UDP */
    
    while (prog) {
        if (rpc_match_prog(rpc, prog) > 0) {
            xdr_write_long(m_out, prog->port);
            size_ptr = xdr_get_pointer(m_out);
            xdr_write_long(m_out, 0); /* write this after the call */
            before = m_out->size;
            result = prog->run(rpc);
            xdr_write_long_at(size_ptr, m_out->size - before);
            return result;
        }
        prog = prog->next;
    }
    
    return RPC_SUCCESS;
}


int portmap_prog(struct rpc_t* rpc) {
    switch (rpc->proc) {
        case PORTMAPPROC_NULL:
            return proc_null(rpc);
            
        case PORTMAPPROC_SET:
            rpc_log(rpc, "SET unimplemented");
            return RPC_PROC_UNAVAIL;
            
        case PORTMAPPROC_UNSET:
            rpc_log(rpc, "UNSET unimplemented");
            return RPC_PROC_UNAVAIL;
            
        case PORTMAPPROC_GETPORT:
            return proc_getport(rpc);
            
        case PORTMAPPROC_DUMP:
            return proc_dump(rpc);
            
        case PORTMAPPROC_CALLIT:
            return proc_callit(rpc);
            
        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}
