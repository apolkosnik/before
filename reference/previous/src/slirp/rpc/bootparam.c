/*
 * Bootparam Program
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

#include "rpc.h"
#include "bootparam.h"


#define IP_ADDR_TYPE 1

static uint32_t read_in_addr(struct xdr_t* m_in) {
    uint32_t addr = 0;
    addr |= (xdr_read_long(m_in) & 0xFF) << 24;
    addr |= (xdr_read_long(m_in) & 0xFF) << 16;
    addr |= (xdr_read_long(m_in) & 0xFF) << 8;
    addr |= (xdr_read_long(m_in) & 0xFF) << 0;
    return addr;
}

static void write_in_addr(struct xdr_t* m_out, uint32_t addr) {
    xdr_write_long(m_out, (addr >> 24) & 0xFF);
    xdr_write_long(m_out, (addr >> 16) & 0xFF);
    xdr_write_long(m_out, (addr >> 8)  & 0xFF);
    xdr_write_long(m_out, (addr >> 0)  & 0xFF);
}

static int proc_whoami(struct rpc_t* rpc) {
    uint32_t addr, addr_type;
    char hostname[NAME_HOST_MAX];
    char domain[NAME_DOMAIN_MAX];
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (m_in->size < 5 * 4) return RPC_GARBAGE_ARGS;
    
    rpc_log(rpc, "WHOAMI");
    
    addr_type = xdr_read_long(m_in);
    
    switch (addr_type) {
        case IP_ADDR_TYPE:
            addr = read_in_addr(m_in);
            break;
        default:
            return RPC_GARBAGE_ARGS;
    }
    
    if (addr == (ntohl(special_addr.s_addr) | CTL_HOST)) {
        vfscpy(hostname, NAME_HOST, sizeof(hostname));
    } else {
        vfscpy(hostname, "", sizeof(hostname));
    }
    vfscpy(domain, "", sizeof(domain)); /* No NIS domain */
    xdr_write_string(m_out, hostname, sizeof(hostname));
    xdr_write_string(m_out, domain, sizeof(domain));
    xdr_write_long(m_out, IP_ADDR_TYPE);
    write_in_addr(m_out, ntohl(special_addr.s_addr) | CTL_GATEWAY);
    return RPC_SUCCESS;
}

static int proc_getfile(struct rpc_t* rpc) {
    int client_len;
    int key_len;
    char client[MAXNAMELEN+1];
    char key[MAXNAMELEN+1];
    char path[MAXPATHLEN];
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    client_len = xdr_read_string(m_in, client, sizeof(client));
    key_len    = xdr_read_string(m_in, key, sizeof(key));
    
    if (client_len < 0 || key_len < 0) return RPC_GARBAGE_ARGS;
    
    rpc_log(rpc, "GETFILE client='%s', key='%s'", client, key);
    
    vfs_get_basepath_alias(rpc->ft->vfs, path, sizeof(path));
    if (strncmp("root", key, sizeof(key))) {
        vfs_join(path, key, sizeof(path));
        if (strncmp("private", key, sizeof(key))) {
            rpc_log(rpc, "GETFILE unknown key '%s'", key);
        }
    }
    xdr_write_string(m_out, rpc->hostname, MAXNAMELEN);
    xdr_write_long(m_out, IP_ADDR_TYPE);
    write_in_addr(m_out, rpc->ip_addr);
    xdr_write_string(m_out, path, sizeof(path));
    return RPC_SUCCESS;
}


int bootparam_prog(struct rpc_t* rpc) {
    switch (rpc->proc) {
        case BOOTPARAMPROC_NULL:
            return proc_null(rpc);
            
        case BOOTPARAMPROC_WHOAMI:
            return proc_whoami(rpc);
            
        case BOOTPARAMPROC_GETFILE:
            return proc_getfile(rpc);
            
        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}
