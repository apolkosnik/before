/*
 * Remote Procedure Call
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

#include "rpc.h"
#include "portmap.h"
#include "mount.h"
#include "nfs.h"
#include "bootparam.h"
#include "netinfobind.h"
#include "csocket.h"
#include "udpsocket.h"
#include "tcpsocket.h"
#include "dns.h"

#include "configuration.h"


#define DBG 0

const struct rpc_prog_t rpc_prog_table_template[] = 
{
    { PORTMAPPROG,   PORTMAPVERS,   IPPROTO_UDP, PORT_RPC, portmap_prog,   1, "PORTMAP"    , NULL, NULL },
    { PORTMAPPROG,   PORTMAPVERS,   IPPROTO_TCP, PORT_RPC, portmap_prog,   1, "PORTMAP"    , NULL, NULL },
    { BOOTPARAMPROG, BOOTPARAMVERS, IPPROTO_UDP, 0,        bootparam_prog, 1, "BOOTPARAM"  , NULL, NULL },
    { MOUNTPROG,     MOUNTVERS,     IPPROTO_UDP, 0,        mount_prog,     1, "MOUNT"      , NULL, NULL },
    { NFSPROG,       NFSVERS,       IPPROTO_UDP, PORT_NFS, nfs_prog,       1, "NFS"        , NULL, NULL },
    { NFSPROG,       NFSVERS,       IPPROTO_TCP, PORT_NFS, nfs_prog,       1, "NFS"        , NULL, NULL },
    { NIBINDPROG,    NIBINDVERS,    IPPROTO_UDP, 0,        nibind_prog,    1, "NETINFOBIND", NULL, NULL },
    { NIBINDPROG,    NIBINDVERS,    IPPROTO_TCP, 0,        nibind_prog,    1, "NETINFOBIND", NULL, NULL }
};


int rpc_match_prog(struct rpc_t* rpc, struct rpc_prog_t* prog) {
    if (prog->prog == rpc->prog && prog->prot == rpc->prot) {
        rpc->name = prog->name;
        rpc->log  = prog->log;
        if (prog->vers == rpc->vers) {
            return 1;
        }
        return -1;
    }
    return 0;
}

static int rpc_call(struct rpc_t* rpc) {
    int result, mismatch;
    struct rpc_prog_t* prog = rpc->prog_list;
    
    mismatch  = 0;
    rpc->low = ~0;
    rpc->high = 0;
    
    while (prog) {
        if (prog->port == rpc->port) {
            result = rpc_match_prog(rpc, prog);
            if (result > 0) {
                return prog->run(rpc);
            }
            if (result < 0) {
                mismatch = 1;
                if (prog->vers < rpc->low)  rpc->low  = prog->vers;
                if (prog->vers > rpc->high) rpc->high = prog->vers;
            }
        }
        prog = prog->next;
    }
    
    return mismatch ? RPC_PROG_MISMATCH : RPC_PROG_UNAVAIL;
}

static void rpc_read_auth(struct rpc_t* rpc) {
    struct xdr_t* m_in = rpc->m_in;
    int len = rpc->auth.length;
    
    if (rpc->auth.flavor == RPC_AUTH_UNIX) {
        uint32_t i;
        struct auth_unix_t* auth = &rpc->auth_unix;
        len -= 5 * 4;
        auth->time = xdr_read_long(m_in);
        len -= xdr_read_string(m_in, auth->machine, sizeof(auth->machine));
        len &= ~3; /* align */
        auth->uid  = xdr_read_long(m_in);
        auth->gid  = xdr_read_long(m_in);
        auth->len  = xdr_read_long(m_in);
        len -= auth->len * 4;
        for (i = 0; i < auth->len && i < NUM_GROUPS; i++) {
            auth->gids[i] = xdr_read_long(m_in);
        }
#if DBG
        printf("UNIX TIME:   %d\n", auth->time);
        printf("UNIX NAME:   %s\n", auth->machine);
        printf("UNIX UID:    %d\n", auth->uid);
        printf("UNIX GID:    %d\n", auth->gid);
        printf("UNIX LEN:    %d\n", auth->len);
        printf("UNIX GIDS:   [");
        for (i = 0; i < auth->len; i++) {
            printf("%d%s", auth->gids[i], i == auth->len - 1 ? "" : ", ");
        }
        printf("]\n");
#endif
        vfs_set_process_uid_gid(rpc->ft->vfs, auth->uid, auth->gid);
    } else {
        if (rpc->auth.flavor != RPC_AUTH_NONE) {
            printf("[RPC] Authentication type %d not supported.\n", rpc->auth.flavor);
        }
        len = xdr_read_skip(m_in, len);
        vfs_set_process_uid_gid(rpc->ft->vfs, 0, 0);
    }
    if (len) {
        printf("[RPC] Authentication decode error.\n");
    }
}

static void rpc_input(struct csocket_t* cs) {
    uint32_t status;
    uint8_t* status_ptr;
    
    struct xdr_t* m_in;
    struct xdr_t* m_out;
    
    struct rpc_t* rpc = (struct rpc_t*)cs->m_pServer;
    
    host_mutex_lock(rpc->lock);
    
    rpc->m_in  = m_in  = cs->m_Input;
    rpc->m_out = m_out = cs->m_Output;
    rpc->port  = cs->m_serverPort;
    rpc->prot  = (cs->m_nType == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
    
#if DBG
    printf("RPC LEN = %d, DATA:\n", m_in->size);
    for (int i = 0; i < m_in->size; i++) {
        printf("%02x ", m_in->data[i]);
    }
    printf("\n");
#endif
    
    rpc->xid = xdr_read_long(m_in);
    rpc->msg = xdr_read_long(m_in);
    if (rpc->msg == RPC_CALL) {
        xdr_write_long(m_out, rpc->xid);
        xdr_write_long(m_out, RPC_REPLY); /* Message type */
        rpc->rpcvers = xdr_read_long(m_in);
        if (rpc->rpcvers == RPCVERS) {
            rpc->prog = xdr_read_long(m_in);
            rpc->vers = xdr_read_long(m_in);
            rpc->proc = xdr_read_long(m_in);
            rpc->auth.flavor = xdr_read_long(m_in);
            rpc->auth.length = xdr_read_long(m_in);
#if DBG
            printf("RPC XID:     %08x\n", rpc->xid);
            printf("RPC MSG:     %d\n",   rpc->msg);
            printf("RPC VERSION: %d\n",   rpc->rpcvers);
            printf("RPC PROG:    %d\n",   rpc->prog);
            printf("RPC PROGVER: %d\n",   rpc->vers);
            printf("RPC PROC:    %d\n",   rpc->proc);
            printf("RPC AUTH:    %d\n",   rpc->auth.flavor);
            printf("RPC AUTHLEN: %d\n",   rpc->auth.length);
#endif
            rpc_read_auth(rpc);
            
            rpc->verif.flavor = xdr_read_long(m_in);
            rpc->verif.length = xdr_read_long(m_in);
            xdr_read_skip(m_in, rpc->verif.length);
#if DBG
            printf("RPC VERIF:   %d\n", rpc->verif.flavor);
            printf("RPC VERLEN:  %d\n", rpc->verif.length);
#endif
            /* RPC Reply */    
            xdr_write_long(m_out, RPC_MSG_ACCEPTED); /* Message */
            xdr_write_long(m_out, rpc->verif.flavor);
            xdr_write_long(m_out, rpc->verif.length);
            xdr_write_zero(m_out, rpc->verif.length);
            status_ptr = xdr_get_pointer(m_out);
            xdr_write_skip(m_out, 4); /* Status will be updated later */
            
            status = rpc_call(rpc);
            
            xdr_write_long_at(status_ptr, status); /* Status */
            
            if (status == RPC_PROG_MISMATCH) {
                rpc_log(rpc, "Version mismatch: req %d, min %d, max %d", rpc->vers, rpc->low, rpc->high);
                xdr_write_long(m_out, rpc->low);
                xdr_write_long(m_out, rpc->high);
            } else if (status == RPC_PROG_UNAVAIL) {
                printf("[%s:RPC:%d:%d] Program not registered\n", rpc->hostname, rpc->prog, rpc->proc);
            } else if (status == RPC_GARBAGE_ARGS) {
                rpc_log(rpc, "Procedure cannot decode input (garbage args)");
            } else if (status == RPC_PROC_UNAVAIL) {
                rpc_log(rpc, "Procedure not available");
            } else if (m_in->size > 0) {
                rpc_log(rpc, "Unused data in buffer (%d bytes)", m_in->size);
            }
        } else { /* RPC version is not 2 */
            printf("[%s:RPC] Version mismatch (%d)\n", rpc->hostname, rpc->rpcvers);
            xdr_write_long(m_out, RPC_MSG_DENIED); /* Message */
            xdr_write_long(m_out, RPC_MISMATCH); /* Status */
            xdr_write_long(m_out, 2); /* Min version */
            xdr_write_long(m_out, 2); /* Max version */
        }
#if DBG
        printf("RPC OUT = %d, DATA:\n", m_out->size);
        for (int i = 0; i < m_out->size; i++) {
            printf("%02x ", (m_out->data - m_out->size)[i]);
        }
        printf("\n");
#endif
        csocket_send(cs);
    } else { /* Do not reply if message type is not CALL */
        printf("[%s:RPC] %s received\n", rpc->hostname, rpc->msg == RPC_REPLY ? "Reply" : "Unknown message");
    }
    
    host_mutex_unlock(rpc->lock);
}

int proc_null(struct rpc_t* rpc) {
    rpc_log(rpc, "NULL");
    return RPC_SUCCESS;
}


static void rpc_udp_port_map(struct rpc_t* rpc, uint16_t src, uint16_t local) {
    rpc->udp_to_local[src] = local;
    rpc->udp_from_local[local] = src;
}

static void rpc_udp_port_unmap(struct rpc_t* rpc, uint16_t src) {
    uint16_t local = rpc->udp_to_local[src];
    rpc->udp_to_local[src] = 0;
    rpc->udp_from_local[local] = 0;
}

static uint16_t rpc_udp_to_local(struct rpc_t* rpc, uint16_t src) {
    return rpc ? rpc->udp_to_local[src] : 0;
}

static uint16_t rpc_udp_from_local(struct rpc_t* rpc, uint16_t local) {
    return rpc ? rpc->udp_from_local[local] : 0;
}

static void rpc_tcp_port_map(struct rpc_t* rpc, uint16_t src, uint16_t local) {
    rpc->tcp_to_local[src] = local;
    rpc->tcp_from_local[local] = src;
}

static void rpc_tcp_port_unmap(struct rpc_t* rpc, uint16_t src) {
    uint16_t local = rpc->tcp_to_local[src];
    rpc->tcp_to_local[src] = 0;
    rpc->tcp_from_local[local] = 0;
}

static uint16_t rpc_tcp_to_local(struct rpc_t* rpc, uint16_t src) {
    return rpc ? rpc->tcp_to_local[src] : 0;
}


static void print_about(void) {
    static int show = 1;
    
    if (show) {
        char hostname[NAME_HOST_MAX];
        gethostname(hostname, sizeof(hostname));
        hostname[NAME_HOST_MAX-1] = '\0';
        printf("[NFSD] Starting local NFS daemon on '%s':\n", hostname);
        
        printf("[NFSD] Network File System server\n");
        printf("[NFSD] Copyright (C) 2005 Ming-Yang Kao\n");
        printf("[NFSD] Edited in 2011 by ZeWaren\n");
        printf("[NFSD] Edited in 2013 by Alexander Schneider (Jankowfsky AG)\n");
        printf("[NFSD] Edited in 2014 2015 by Yann Schepens\n");
        printf("[NFSD] Edited in 2016 by Peter Philipp (Cando Image GmbH), Marc Harding\n");
        printf("[NFSD] Mostly rewritten in 2019-2021 by Simon Schubiger for Previous NeXT emulator\n");
        printf("[NFSD] Rewritten in C in 2025 by Andreas Grabher\n");
    }
    show = 0;
}

void rpc_add_program(struct rpc_t* rpc, struct rpc_prog_t* prog) {
    uint16_t local_port = 0;
    struct rpc_prog_t** entry = &rpc->prog_list;
    
    while (*entry) {
        entry = &(*entry)->next;
    }
    
    *entry = prog;
    (*entry)->next = NULL;
    
    if (prog->prot == IPPROTO_TCP) {
        prog->sock = tcpsocket_init(rpc_input, rpc);
        if (prog->sock) {
            local_port = rpc_tcp_to_local(rpc, prog->port);
            if (local_port) {
                printf("[RPC] %s daemon stopping (TCP: %d -> %d).\n", prog->name, prog->port, local_port);
                rpc_tcp_port_unmap(rpc, prog->port);
            }
            local_port = tcpsocket_open(prog->sock, prog->port);
            if (local_port) {
                if (prog->port == 0) {
                    prog->port = local_port;
                }
                rpc_tcp_port_map(rpc, prog->port, local_port);
                printf("[RPC] %s daemon started (TCP: %d -> %d).\n", prog->name, prog->port, local_port);
            } else {
                printf("[RPC] %s daemon start failed.\n", prog->name);
                tcpsocket_close(prog->sock);
                prog->sock = tcpsocket_uninit(prog->sock);
            }
        } else {
            printf("[RPC] Socket initialisation failed.");
        }
    } else {
        prog->sock = udpsocket_init(rpc_input, rpc);
        if (prog->sock) {
            local_port = rpc_udp_to_local(rpc, prog->port);
            if (local_port) {
                printf("[RPC] %s daemon stopping (UDP: %d -> %d).\n", prog->name, prog->port, local_port);
                rpc_udp_port_unmap(rpc, prog->port);
            }
            local_port = udpsocket_open(prog->sock, prog->port);
            if (local_port) {
                if (prog->port == 0) {
                    prog->port = local_port;
                }
                rpc_udp_port_map(rpc, prog->port, local_port);
                printf("[RPC] %s daemon started (UDP: %d -> %d).\n", prog->name, prog->port, local_port);
            } else {
                printf("[RPC] %s daemon start failed.\n", prog->name);
                udpsocket_close(prog->sock);
                prog->sock = udpsocket_uninit(prog->sock);
            }
        } else {
            printf("[RPC] Socket initialisation failed.");
        }
    }
}

static void rpc_remove_all_programs(struct rpc_t* rpc) {
    struct rpc_prog_t* next;
    
    while (rpc->prog_list) {
        if (rpc->prog_list->sock) {
            if (rpc->prog_list->prot == IPPROTO_TCP) {
                rpc_tcp_port_unmap(rpc, rpc->prog_list->port);
                rpc->prog_list->sock = tcpsocket_uninit(rpc->prog_list->sock);
            } else {
                rpc_udp_port_unmap(rpc, rpc->prog_list->port);
                rpc->prog_list->sock = udpsocket_uninit(rpc->prog_list->sock);
            }
        }
        next = rpc->prog_list->next;
        free(rpc->prog_list);
        rpc->prog_list = next;
    }
}

static int char_is_ascii_lower(char c) {
    return c >= 'a' && c <= 'z';
}

static int char_is_ascii_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

static int char_is_ascii_other(char c) {
    return (c >= '0' && c <= '9') || c == '-';
}

static int rpc_copy_hostname(char* dst, const char* src, int maxlen) {
    int len = 0;
    
    while (*src && len + 1 < maxlen) {
        if (char_is_ascii_other(*src) && len > 0) {
            dst[len++] = *src;
        } else if (char_is_ascii_lower(*src)) {
            dst[len++] = *src;
        } else if (char_is_ascii_upper(*src)) {
            dst[len++] = *src + 32;
        }
        src++;
    }
    if (len > 0 && dst[len - 1] == '-') {
        len--;
    }
    dst[len] = '\0';
    return len;
}

static struct rpc_t* rpc_server[EN_MAX_SHARES];

static void rpc_start_server(struct rpc_t* rpc, const char* path, const char* name, uint32_t addr) {
    if (rpc->ft) {
        printf("[RPC] '%s' already running.\n", rpc->hostname);
        return;
    }
    if (rpc_copy_hostname(rpc->hostname, name, sizeof(rpc->hostname)) == 0) {
        printf("[RPC] Startup failed for '%s' (no valid host name).\n", name);
        return;
    }
    
    rpc->ip_addr  = addr;
    
    rpc->ft = ft_init(path, "/");
    if (rpc->ft) {
        size_t i;
        char addrstr[16];
        struct rpc_prog_t* prog;
        
        printf("[RPC] Starting '%s' at %s, exporting '%s'.\n", rpc->hostname, 
               rpc_ip_str(addrstr, addr, 4, sizeof(addrstr)), path);
        
        rpc->lock = host_mutex_create();
        netinfo_add_host(rpc->hostname, rpc->ip_addr);
        vdns_add_rec(rpc->hostname, rpc->ip_addr);
        
        for (i = 0; i < TBL_SIZE(rpc_prog_table_template); i++) {
            prog = (struct rpc_prog_t*)malloc(sizeof(struct rpc_prog_t));
            memcpy(prog, &rpc_prog_table_template[i], sizeof(struct rpc_prog_t));
            rpc_add_program(rpc, prog);
        }
        nibind_init(rpc);
    } else {
        printf("[RPC] Startup failed for '%s', exporting '%s'.\n", rpc->hostname, path);
    }
}

static void rpc_stop_server(struct rpc_t* rpc) {
    if (rpc) {
        if (rpc->ft) {
            printf("[RPC] Stopping '%s'.\n", rpc->hostname);

            rpc_remove_all_programs(rpc);
            nibind_uninit(rpc);
            mount_uninit(rpc);
            
            netinfo_remove_host(rpc->hostname);
            vdns_remove_rec(rpc->ip_addr);
            host_mutex_destroy(rpc->lock);
            rpc->ft = ft_uninit(rpc->ft);
        }
    }
}

static struct rpc_broadcast_t {
    struct udpsocket_t* udp;
    uint16_t            udp_port;
} broadcasthost;

static void rpc_broadcast(struct csocket_t* cs) {
    int i;
    uint32_t saved_size;
    sock_t saved_sock;
    
    saved_sock = cs->m_Socket;
    saved_size = cs->m_Input->size;
    
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i] && rpc_server[i]->ft) {
            struct rpc_prog_t* prog = rpc_server[i]->prog_list;
            while (prog) {
                if (prog->port == PORT_RPC && prog->prot == IPPROTO_UDP) {
                    struct udpsocket_t* us = (struct udpsocket_t*)prog->sock;
                    cs->m_Socket       = us->m_pSocket->m_Socket;
                    cs->m_pServer      = (void*)rpc_server[i];
                    cs->m_Input->size  = saved_size;
                    cs->m_Input->data  = cs->m_Input->head;
                    cs->m_Output->size = 0;
                    cs->m_Output->data = cs->m_Output->head;
                    rpc_input(cs);
                    break;
                }
                prog = prog->next;
            }
        }
    }
    cs->m_Socket = saved_sock;
}

static void rpc_broadcast_stop(void) {
    if (broadcasthost.udp) {
        broadcasthost.udp_port = 0;
        udpsocket_close(broadcasthost.udp);
        broadcasthost.udp = udpsocket_uninit(broadcasthost.udp);
    }
}

static void rpc_broadcast_start(void) {
    if (broadcasthost.udp_port == 0) {
        broadcasthost.udp  = udpsocket_init(rpc_broadcast, NULL);
        if (broadcasthost.udp) {
            broadcasthost.udp_port = udpsocket_open(broadcasthost.udp, PORT_RPC);
            if (broadcasthost.udp_port) {
                printf("[RPC] Broadcast enabled (UDP: %d -> %d).\n", PORT_RPC, broadcasthost.udp_port);
            } else {
                printf("[RPC] Broadcast startup failed.\n");
                rpc_broadcast_stop();
            }
        } else {
            printf("[RPC] Broadcast UDP socket initialisation failed.\n");
        }
    }
}

static int rpc_check_nfs(struct rpc_t* rpc, const char* path, const char* name) {
    if (access(path, F_OK | R_OK) < 0) {
        printf("[RPC] Cannot access directory '%s'. NFS startup canceled for '%s'.\n", path, name);
        return -1;
    } else if (ft_is_inited(rpc->ft)) {
        if (ft_path_changed(rpc->ft, path)) {
            return 1;
        }
    } else {
        return 1;
    }
    return 0;
}

void rpc_reset(void) {
    int i;
    
    print_about();
    
    netinfo_build_nidb();
    vdns_init();
    
    for (i = 0; i < EN_MAX_SHARES; i++) {
        int needreset;
        const char* name = i ? ConfigureParams.Ethernet.nfs[i].szHostName : NAME_NFSD;
        const char* path = ConfigureParams.Ethernet.nfs[i].szPathName;
        
        if (strlen(name) > 0 && strlen(path) > 0) {
            if (rpc_server[i] == NULL) {
                rpc_server[i] = calloc(1, sizeof(struct rpc_t));
            }
            needreset = rpc_check_nfs(rpc_server[i], path, name);
            if (needreset != 0) {
                rpc_stop_server(rpc_server[i]);
            }
            if (needreset > 0) {
                rpc_start_server(rpc_server[i], path, name, ntohl(special_addr.s_addr) | (CTL_NFSD - i));
            }
            if (rpc_server[i]->ft) {
                continue;
            }
        }
        rpc_stop_server(rpc_server[i]);
        free(rpc_server[i]);
        rpc_server[i] = NULL;
    }
    
    rpc_broadcast_start();
}

void rpc_uninit(void) {
    int i;
    
    rpc_broadcast_stop();
    
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i]) {
            rpc_stop_server(rpc_server[i]);
            free(rpc_server[i]);
            rpc_server[i] = NULL;
        }
    }
    
    netinfo_delete_nidb();
    vdns_uninit();
}

static struct rpc_t* rpc_find_server(uint32_t addr) {
    int i;
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i] && rpc_server[i]->ft) {
            if (rpc_server[i]->ip_addr == addr) {
                return rpc_server[i];
            }
        }
    }
    return NULL;
}

static struct rpc_t* rpc_find_server_by_port(uint16_t local_port) {
    int i;
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i] && rpc_server[i]->ft) {
            if (rpc_udp_from_local(rpc_server[i], local_port)) {
                return rpc_server[i];
            }
        }
    }
    return NULL;
}

int rpc_read_file(const char* vfs_path, uint32_t offset, uint8_t* data, uint32_t len) {
    if (rpc_server[0] && rpc_server[0]->ft) {
        struct path_t path;
        vfscpy(path.vfs, vfs_path, sizeof(path.vfs));
        vfs_to_host_path(rpc_server[0]->ft->vfs, &path);
        if (vfs_read(&path, offset, data, &len) >= 0)
            return (int)len;
    }
    return -1;
}

int rpc_match_arp(uint8_t byte) {
    int i;
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i] && rpc_server[i]->ft) {
            if ((rpc_server[i]->ip_addr & 0xFF) == byte) {
                return 1;
            }
        }
    }
    return 0;
}

int rpc_match_icmp(uint32_t addr) {
    int i;
    for (i = 0; i < EN_MAX_SHARES; i++) {
        if (rpc_server[i] && rpc_server[i]->ft) {
            if (rpc_server[i]->ip_addr == addr) {
                return 1;
            }
        }
    }
    return 0;
}

int rpc_match_addr(uint32_t addr) {
    if ((addr &  CTL_NET_MASK) == CTL_NET && 
        (addr & ~CTL_NET_MASK) > (CTL_NFSD - EN_MAX_SHARES)) {
        return 1;
    }
    /* NS kernel broadcasts on 10.255.255.255 */
    if (addr == (CTL_NET | ~(uint32_t)CTL_CLASS_MASK(CTL_NET))) {
        return 1;
    }
    return 0;
}

void rpc_udp_map_to_local_port(struct in_addr* ipNBO, uint16_t* dportNBO) {
    uint16_t dport = ntohs(*dportNBO);
    uint16_t port  = 0;
    if (dport == PORT_RPC && (ipNBO->s_addr == htonl(CTL_NET | ~(uint32_t)CTL_NET_MASK) ||
                              ipNBO->s_addr == htonl(CTL_NET | ~(uint32_t)CTL_CLASS_MASK(CTL_NET)))) {
        char addrstr[INET_ADDRSTRLEN];
        printf("[RPC] Broadcast to %s, port %d\n", inet_ntop(AF_INET, ipNBO, addrstr, sizeof(addrstr)), dport);
        port = broadcasthost.udp_port;
    } else {
        struct rpc_t* rpc = rpc_find_server(htonl(ipNBO->s_addr));
        port = rpc_udp_to_local(rpc, dport);
    }
    if (port) {
        *dportNBO = htons(port);
        *ipNBO    = loopback_addr;
    }
}

void rpc_tcp_map_to_local_port(uint32_t addr, uint16_t port, uint16_t* sin_portNBO) {
    struct rpc_t* rpc = rpc_find_server(addr);
    uint16_t localPort = rpc_tcp_to_local(rpc, port);
    if (localPort)
        *sin_portNBO = htons(localPort);
}

void rpc_udp_map_from_local_port(struct in_addr* saddrNBO, uint16_t* sin_portNBO) {
    uint16_t port = ntohs(*sin_portNBO);
    struct rpc_t* rpc = rpc_find_server_by_port(port);
    uint16_t srcPort = rpc_udp_from_local(rpc, port);
    if (srcPort) {
        *sin_portNBO = htons(srcPort);
        saddrNBO->s_addr = htonl(rpc->ip_addr);
    }
}

const char* rpc_ip_str(char* buf, uint32_t addr, int count, size_t maxsize) {
    switch (count) {
        case 3:
            snprintf(buf, maxsize, "%u.%u.%u", (addr>>24)&0xFF, (addr>>16)&0xFF, (addr>>8)&0xFF);
            break;
        case 4:
            snprintf(buf, maxsize, "%u.%u.%u.%u", (addr>>24)&0xFF, (addr>>16)&0xFF, (addr>>8)&0xFF, addr&0xFF);
            break;
        default:
            buf[0] = '\0';
            break;
    }
    return buf;
}

void rpc_log(struct rpc_t* rpc, const char *format, ...) {
    va_list vargs;
    
    if (rpc->log)
    {
        va_start(vargs, format);
        printf("[%s:RPC:%s:%d] ", rpc->hostname, rpc->name, rpc->proc);
        vprintf(format, vargs);
        printf("\n");
        va_end(vargs);
    }
}
