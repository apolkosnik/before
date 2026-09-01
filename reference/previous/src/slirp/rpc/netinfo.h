/* NetInfo Program */

#ifndef _NETINFO_H_
#define _NETINFO_H_

#define PORT_NETINFO            1043

#define NETINFOPROG             200100000
#define NETINFOVERS             2

#define NETINFOPROC_PING        0
#define NETINFOPROC_STATISTICS  1
#define NETINFOPROC_ROOT        2
#define NETINFOPROC_SELF        3
#define NETINFOPROC_PARENT      4
#define NETINFOPROC_CREATE      5
#define NETINFOPROC_DESTROY     6
#define NETINFOPROC_READ        7
#define NETINFOPROC_WRITE       8
#define NETINFOPROC_CHILDREN    9
#define NETINFOPROC_LOOKUP      10
#define NETINFOPROC_LIST        11
#define NETINFOPROC_CREATEPROP  12
#define NETINFOPROC_DESTROYPROP 13
#define NETINFOPROC_READPROP    14
#define NETINFOPROC_WRITEPROP   15
#define NETINFOPROC_RENAMEPROP  16
#define NETINFOPROC_LISTPROPS   17
#define NETINFOPROC_CREATENAME  18
#define NETINFOPROC_DESTROYNAME 19
#define NETINFOPROC_READNAME    20
#define NETINFOPROC_WRITENAME   21
#define NETINFOPROC_RPARENT     22
#define NETINFOPROC_LISTALL     23
#define NETINFOPROC_BIND        24
#define NETINFOPROC_READALL     25
#define NETINFOPROC_CRASHED     26
#define NETINFOPROC_RESYNC      27
#define NETINFOPROC_LOOKUPREAD  28


enum ni_status {
    NI_OK              = 0,
    NI_BADID           = 1,
    NI_STALE           = 2,
    NI_NOSPACE         = 3,
    NI_PERM            = 4,
    NI_NODIR           = 5,
    NI_NOPROP          = 6,
    NI_NONAME          = 7,
    NI_NOTEMPTY        = 8,
    NI_UNRELATED       = 9,
    NI_SERIAL          = 10,
    NI_NETROOT         = 11,
    NI_NORESPONSE      = 12,
    NI_RDONLY          = 13,
    NI_SYSTEMERR       = 14,
    NI_ALIVE           = 15,
    NI_NOTMASTER       = 16,
    NI_CANTFINDADDRESS = 17,
    NI_DUPTAG          = 18,
    NI_NOTAG           = 19,
    NI_AUTHERROR       = 20,
    NI_NOUSER          = 21,
    NI_MASTERBUSY      = 22,
    NI_INVALIDDOMAIN   = 23,
    NI_BADOP           = 24,
    NI_FAILED          = 9999
};

struct ni_id_t {
    uint32_t object;
    uint32_t instance;
};

struct ni_val_t {
    char* val;
    struct ni_val_t* next;
};

struct ni_prop_t {
    char* key;
    struct ni_val_t* val;
    struct ni_prop_t* next;
};

struct ni_id_map_t {
    uint32_t id;
    struct ni_node_t* node;
    
    struct ni_id_map_t* next;
};

struct ni_node_t {
    struct ni_id_t       id;
    struct ni_id_map_t** id_map;
    struct ni_node_t*    parent;
    struct ni_prop_t*    props;
    struct ni_node_t*    children;
    
    struct ni_node_t*    next;
};

struct nidb_t {
    const char* tag;
    struct ni_id_map_t* id_map;
    struct ni_node_t*   root;
};

struct nireg_t {
    const char* tag;
    
    uint32_t udp_port;
    uint32_t tcp_port;
    
    struct rpc_prog_t* udp_prog;
    struct rpc_prog_t* tcp_prog;
    
    struct nireg_t* next;
};

void netinfo_build_nidb(void);
void netinfo_delete_nidb(void);
void netinfo_add_host(const char* name, uint32_t ip_addr);
void netinfo_remove_host(const char* name);

int netinfo_prog(struct rpc_t* rpc);

#endif /* _NETINFO_H_ */
