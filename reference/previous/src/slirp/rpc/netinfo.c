/*
 * NetInfo Program
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
#include "ctl.h"

#include "configuration.h"


#define DBG 1

#if DBG
#define DBGMAX 1024 /* max length of debug string */

static const char* status_to_string(enum ni_status status) {
    switch(status) {
        case NI_OK:              return "NI_OK";
        case NI_BADID:           return "NI_BADID";
        case NI_STALE:           return "NI_STALE";
        case NI_NOSPACE:         return "NI_NOSPACE";
        case NI_PERM:            return "NI_PERM";
        case NI_NODIR:           return "NI_NODIR";
        case NI_NOPROP:          return "NI_NOPROP";
        case NI_NONAME:          return "NI_NONAME";
        case NI_NOTEMPTY:        return "NI_NOTEMPTY";
        case NI_UNRELATED:       return "NI_UNRELATED";
        case NI_SERIAL:          return "NI_SERIAL";
        case NI_NETROOT:         return "NI_NETROOT";
        case NI_NORESPONSE:      return "NI_NORESPONSE";
        case NI_RDONLY:          return "NI_RDONLY";
        case NI_SYSTEMERR:       return "NI_SYSTEMERR";
        case NI_ALIVE:           return "NI_ALIVE";
        case NI_NOTMASTER:       return "NI_NOTMASTER";
        case NI_CANTFINDADDRESS: return "NI_CANTFINDADDRESS";
        case NI_DUPTAG:          return "NI_DUPTAG";
        case NI_NOTAG:           return "NI_NOTAG";
        case NI_AUTHERROR:       return "NI_AUTHERROR";
        case NI_NOUSER:          return "NI_NOUSER";
        case NI_MASTERBUSY:      return "NI_MASTERBUSY";
        case NI_INVALIDDOMAIN:   return "NI_INVALIDDOMAIN";
        case NI_BADOP:           return "NI_BADOP";
        case NI_FAILED:          return "NI_FAILED";
        default:                 return "<unknown>";
    }
}

static void val_to_string(char* dbg, struct ni_val_t* vals) {
    while (vals) {
        snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " '%s'", vals->val);
        vals = vals->next;
    }
}

static void prop_to_string(char* dbg, struct ni_prop_t* prop) {
    while (prop) {
        snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " [%s] =", prop->key);
        val_to_string(dbg, prop->val);
        prop = prop->next;
    }
}

static struct ni_prop_t* ni_prop_find(struct ni_prop_t* map, const char* key);

static void node_path_to_string(char* dbg, struct ni_node_t* node) {
    struct ni_prop_t* prop;
    
    if (node->parent) {
        node_path_to_string(dbg, node->parent);
        prop = ni_prop_find(node->props, "name");
        vfscat(dbg, prop->val->val, DBGMAX);
    }
    vfscat(dbg, "/", DBGMAX);
}
#endif


/* Property values */
static int ni_val_add(struct ni_val_t** vals, const char* val) {
    if (val == NULL) {
        return -1;
    }
    while (*vals) {
        if (strncmp((*vals)->val, val, MAXNAMELEN)) {
            vals = &(*vals)->next;
            continue;
        }
        return 1;
    }
    *vals = (struct ni_val_t*)malloc(sizeof(struct ni_val_t));
    (*vals)->val = strdup(val);
    (*vals)->next = NULL;
    return 0;
}

static int ni_val_find(struct ni_val_t* vals, const char* val) {
    while (vals) {
        if (strncmp(vals->val, val, MAXNAMELEN) == 0) {
            return 1;
        }
        vals = vals->next;
    }
    return 0;
}

static int ni_val_count(struct ni_val_t* vals) {
    int result = 0;
    
    while (vals) {
        result++;
        vals = vals->next;
    }
    return result;
}

#if 0 /* unused */
static int ni_val_remove(struct ni_val_t** vals, char* val) {
    struct ni_val_t* next;
    
    while (*vals) {
        if (strncmp((*vals)->val, val, MAXNAMELEN)) {
            vals = &(*vals)->next;
            continue;
        }
        free((*vals)->val);
        next = (*vals)->next;
        free((*vals));
        *vals = next;
        return 1;
    }
    return 0;
}
#endif

static void ni_val_delete(struct ni_val_t** vals) {
    struct ni_val_t* next;
    
    while (*vals) {
        free((*vals)->val);
        next = (*vals)->next;
        free((*vals));
        *vals = next;
    }
}


/* Properties */
static int ni_prop_add(struct ni_prop_t** props, const char* key, const char* val) {
    if (key == NULL) {
        return -1;
    }
    while (*props) {
        if (strncmp((*props)->key, key, MAXNAMELEN)) {
            props = &(*props)->next;
            continue;
        }
        ni_val_add(&(*props)->val, val);
        return 1;
    }
    *props = (struct ni_prop_t*)malloc(sizeof(struct ni_prop_t));
    (*props)->key = strdup(key);
    (*props)->val = NULL;
    ni_val_add(&(*props)->val, val);
    (*props)->next = NULL;
    return 0;
}

static struct ni_prop_t* ni_prop_find(struct ni_prop_t* props, const char* key) {
    while (props) {
        if (strncmp(props->key, key, MAXNAMELEN) == 0) {
            return props;
        }
        props = props->next;
    }
    return NULL;
}

static int ni_prop_count(struct ni_prop_t* props) {
    int result = 0;
    
    while (props) {
        result++;
        props = props->next;
    }
    return result;
}

#if 0 /* unused */
static int ni_prop_remove(struct ni_prop_t** props, const char* key) {
    struct ni_prop_t* next;
    
    while (*props) {
        if (strncmp((*props)->key, key, MAXNAMELEN)) {
            props = &(*props)->next;
            continue;
        }
        free((*props)->key);
        ni_val_delete(&(*props)->val);
        next = (*props)->next;
        free((*props));
        *props = next;
        return 1;
    }
    return 0;
}
#endif

static void ni_prop_delete(struct ni_prop_t** props) {
    struct ni_prop_t* next;
    
    while (*props) {
        free((*props)->key);
        ni_val_delete(&(*props)->val);
        next = (*props)->next;
        free((*props));
        *props = next;
    }
}


/* Object map */
static void ni_id_add_object(struct ni_id_t* ni_id, uint32_t object) {
    ni_id->object   = object;
    ni_id->instance = 1;
}

static uint32_t id_map_add(struct ni_id_map_t** idmap, struct ni_node_t* node) {
    struct ni_id_map_t* new = malloc(sizeof(struct ni_id_map_t));
    new->id   = 0;
    new->node = node;
    new->next = NULL;
    
    while (*idmap) {
        new->id++;
        idmap = &(*idmap)->next;
    }
    *idmap = new;
    return new->id;
}

static struct ni_id_map_t* id_map_find(struct ni_id_map_t** idmap, uint32_t id) {
    while (*idmap) {
        if ((*idmap)->id == id) {
            return *idmap;
        }
        idmap = &(*idmap)->next;
    }
    return NULL;
}

static int id_map_remove(struct ni_id_map_t** idmap, uint32_t id) {
    struct ni_id_map_t* next;
    int found = 0;
    
    while (*idmap) {
        if ((*idmap)->id == id) {
            next = (*idmap)->next;
            free(*idmap);
            *idmap = next;
            found = 1;
        } else {
            if (found) {
                (*idmap)->id--;
                ni_id_add_object(&(*idmap)->node->id, (*idmap)->id);
            }
            idmap = &(*idmap)->next;
        }
    }
    return found;
}

static void id_map_delete(struct ni_id_map_t** idmap) {
    struct ni_id_map_t* next;
    
    while (*idmap) {
        next = (*idmap)->next;
        free(*idmap);
        *idmap = next;
    }
}


/* Nodes */
static int ni_node_add(struct ni_node_t** children, struct ni_node_t* node) {
    while (*children) {
        children = &(*children)->next;
    }
    *children = node;
    (*children)->next = NULL;
    return 0;
}

static void ni_node_delete(struct ni_node_t** node) {
    struct ni_node_t* next;
    
    while (*node) {
        id_map_remove((*node)->id_map, (*node)->id.object);
        ni_prop_delete(&(*node)->props);
        ni_node_delete(&(*node)->children);
        next = (*node)->next;
        free((*node));
        *node = next;
    }
}

static struct ni_node_t* ni_node_create(struct ni_id_map_t** idmap, struct ni_node_t* parent) {
    uint32_t object;
    struct ni_node_t* new = (struct ni_node_t*)malloc(sizeof(struct ni_node_t));
    object = id_map_add(idmap, new);
    ni_id_add_object(&new->id, object);
    new->id_map   = idmap;
    new->parent   = parent;
    new->props    = NULL;
    new->children = NULL;
    new->next     = NULL;
    return new;
}

static void ni_node_remove_child(struct ni_node_t** child, const char* key, const char* val) {
    struct ni_node_t* next;
    
    while (*child) {
        if (ni_val_find(ni_prop_find((*child)->props, key)->val, val)) {
            next = (*child)->next;
            (*child)->next = NULL;
            ni_node_delete(child);
            *child = next;
            break;
        }
        child = &(*child)->next;
    }
}

static struct ni_node_t* ni_node_add_child(struct nidb_t* ni, struct ni_node_t* node) {
    struct ni_node_t* result = ni_node_create(&ni->id_map, node);
    ni_node_add(&(node->children), result);
    return result;
}

static void ni_node_add_prop(struct ni_node_t* node, const char* key, const char* val) {
    ni_prop_add(&node->props, key, val);
}

static int ni_node_count(struct ni_node_t* node) {
    int result = 0;
    while (node) {
        result++;
        node = node->next;
    }
    return result;
}


/* NetInfo database */
static struct ni_node_t* ni_find_from_key_val(struct ni_node_t* node, const char* key, const char* val) {
    struct ni_prop_t* props = NULL;
    struct ni_val_t*  vals  = NULL;

    while (node) {
        props = node->props;
        while (props) {
            if (strcmp(props->key, key) == 0) {
                vals = props->val;
                while (vals) {
                    if (strcmp(vals->val, val) == 0) {
                        return node;
                    }
                    vals = vals->next;
                }
            }
            props = props->next;
        }
        node = node->next;
    }
    return NULL;
}

void netinfo_add_host(const char* name, uint32_t ip_addr) {
    char ip_str[16];
    char mount[MAXNAMELEN+1];

    struct ni_node_t* node;
    struct ni_node_t* child;
    
    struct nidb_t* network = nidb;
    
    snprintf(mount, sizeof(mount), "%s:/", name);
    
    printf("[NETINFO] Adding '%s' to NetInfo database '%s'.\n", name, network->tag);
    
    /* Add child network:/machines/name */
    node = ni_find_from_key_val(network->root->children, "name", "machines");
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", name);
    ni_node_add_prop(child, "ip_address", rpc_ip_str(ip_str, ip_addr, 4, sizeof(ip_str)));
    ni_node_add_prop(child, "serves", "./network");
    ni_node_add_prop(child, "serves", "../network");
    
    /* Add child network:/mounts/mount */
    node = ni_find_from_key_val(network->root->children, "name", "mounts");
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", mount);
    ni_node_add_prop(child, "dir", "/Net");
    ni_node_add_prop(child, "opts", "rw");
    ni_node_add_prop(child, "opts", "net");
}

void netinfo_remove_host(const char* name) {
    char mount[MAXNAMELEN+1];
    
    struct ni_node_t* node;
    
    struct nidb_t* network = nidb;
    
    snprintf(mount, sizeof(mount), "%s:/", name);
    
    printf("[NETINFO] Removing '%s' from NetInfo database '%s'.\n", name, network->tag);
    
    node = ni_find_from_key_val(network->root->children, "name", "machines");
    if (node) ni_node_remove_child(&node->children, "name", name);
    
    node = ni_find_from_key_val(network->root->children, "name", "mounts");
    if (node) ni_node_remove_child(&node->children, "name", mount);
}

void netinfo_build_nidb(void) {
    char ip_str[16];
    char hostname[NAME_HOST_MAX];
    char system_type[24];
    const char* domain;
    
    struct nidb_t* network;
    
    struct ni_node_t* node  = NULL;
    struct ni_node_t* child = NULL;
    
    if (nidb) return;
    
    network = nidb = (struct nidb_t*)malloc(sizeof(struct nidb_t));
    
    network->tag    = "network";
    network->id_map = NULL;
    network->root   = NULL;
    
    printf("[NETINFO] Creating NetInfo database '%s'.\n", nidb->tag);
    
    /* Configure some strings */
    vfscpy(system_type, "NeXT", sizeof(system_type));
    if (ConfigureParams.System.nMachineType == NEXT_STATION) {
        vfscat(system_type, "station", sizeof(system_type));
        if (ConfigureParams.System.bColor) {
            vfscat(system_type, " Color", sizeof(system_type));
        }
    } else {
        vfscat(system_type, "cube", sizeof(system_type));
    }
    gethostname(hostname, sizeof(hostname));
    hostname[NAME_HOST_MAX-1] = '\0';
    domain = (NAME_DOMAIN[0] == '.' ? &NAME_DOMAIN[1] : &NAME_DOMAIN[0]);
    
    /* Create root network:/ */
    network->root = ni_node_create(&network->id_map, NULL);
    ni_node_add_prop(network->root, "master", NAME_NFSD"/network");
    ni_node_add_prop(network->root, "trusted_networks", rpc_ip_str(ip_str, CTL_NET, 3, sizeof(ip_str)));
    
    /* Add child network:/machines */
    node = ni_node_add_child(network, network->root);
    ni_node_add_prop(node, "name", "machines");
    
    /* Add child network:/machines/host */
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", hostname);
    ni_node_add_prop(child, "ip_address", rpc_ip_str(ip_str, CTL_NET|CTL_ALIAS, 4, sizeof(ip_str)));
    
    /* Add child network:/machines/previous */
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", NAME_HOST);
    ni_node_add_prop(child, "ip_address", rpc_ip_str(ip_str, CTL_NET|CTL_HOST, 4, sizeof(ip_str)));
    ni_node_add_prop(child, "serves", NAME_HOST"/local");
    ni_node_add_prop(child, "netgroups", NULL);
    ni_node_add_prop(child, "system_type", system_type);
    
    /* Add child network:/machines/dns */
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", NAME_DNS);
    ni_node_add_prop(child, "ip_address", rpc_ip_str(ip_str, CTL_NET|CTL_DNS, 4, sizeof(ip_str)));
    
    /* Create network:/mounts */
    node = ni_node_add_child(network, network->root);
    ni_node_add_prop(node, "name", "mounts");
    
    /* Create network:/locations */
    node = ni_node_add_child(network, network->root);
    ni_node_add_prop(node, "name", "locations");
    
    /* Add child network:/locations/resolver */
    child = ni_node_add_child(network, node);
    ni_node_add_prop(child, "name", "resolver");
    ni_node_add_prop(child, "nameserver", rpc_ip_str(ip_str, CTL_NET|CTL_DNS, 4, sizeof(ip_str)));
    ni_node_add_prop(child, "domain", domain);
    ni_node_add_prop(child, "search", domain);
    
    /* Add child network:/locations/ntp */
    if (ConfigureParams.Ethernet.bNetworkTime) {
        child = ni_node_add_child(network, node);
        ni_node_add_prop(child, "name", "ntp");
        ni_node_add_prop(child, "server", hostname);
        ni_node_add_prop(child, "host", hostname);
    }
}

void netinfo_delete_nidb(void) {
    if (nidb) {
        printf("[NETINFO] Deleting NetInfo database '%s'.\n", nidb->tag);
        ni_node_delete(&nidb->root);
        free(nidb);
        nidb = NULL;
    }
}

/* Helpers */
static uint32_t checksum(char* str) {
    size_t i;
    uint32_t result = 0;
    for (i = 0; i < strlen(str); ++i)
        result = result * 31 + (int)(str[i]);
    return result;
}

static uint32_t ni_node_checksum(struct ni_node_t* node) {
    uint32_t result = 0;
    struct ni_node_t* child = node->children;
    struct ni_prop_t* props = node->props;
    struct ni_val_t*  vals  = NULL;
    
    while (child) {
        result += ni_node_checksum(child);
        child = child->next;
    }
    while (props) {
        result += checksum(props->key);
        vals = props->val;
        while (vals) {
            result += checksum(vals->val);
            vals = vals->next;
        }
        props = props->next;
    }
    return result;
}

static struct ni_val_t* ni_node_get_prop_names(struct ni_node_t* node) {
    struct ni_val_t* names = NULL;
    struct ni_prop_t* props = node->props;
    
    while (props) {
        ni_val_add(&names, props->key);
        props = props->next;
    }
    return names;
}

static struct ni_val_t* ni_prop_get_vals(struct ni_prop_t* props, char* key) {
    struct ni_prop_t* prop;
    
    prop = ni_prop_find(props, key);
    if (prop) {
        return prop->val;
    }
    return NULL;
}

static struct ni_val_t* ni_prop_get_vals_by_index(struct ni_prop_t* props, uint32_t index) {
    while (props) {
        if (index == 0) {
            return props->val;
        }
        index--;
        props = props->next;
    }
    return NULL;
}

static int ni_node_find_by_prop(struct ni_node_t* node, struct ni_id_map_t** idmap, char* key, char* val) {
    int count = 0;
    struct ni_node_t* child;
    struct ni_prop_t* props;
    
    child = node->children;
    
    while (child) {
        props = ni_prop_find(child->props, key);
        if (props && ni_val_find(props->val, val)) {
            count = id_map_add(idmap, child) + 1;
        }
        child = child->next;
    }
    return count;
}

static struct ni_node_t* ni_node_find(struct ni_node_t* node, struct ni_id_t* ni_id, enum ni_status* status, int forWrite) {
    struct ni_node_t* result;
    struct ni_id_map_t* id_map;
    
    id_map = id_map_find(node->id_map, ni_id->object);
    if (id_map == NULL) {
        *status = NI_BADID;
        return NULL;
    }
    
    result = id_map->node;
    *status = (!forWrite || result->id.instance == ni_id->instance) ? NI_OK : NI_STALE;
    ni_id->instance = result->id.instance;
    
    return result;
}


/* Log */
static void ni_log(struct rpc_t* rpc, struct nidb_t* ni, const char *format, ...) {
    va_list vargs;
    
    if (rpc->log)
    {
        va_start(vargs, format);
        printf("[%s:RPC:%s:%d:%s] ", rpc->hostname, rpc->name, rpc->proc, ni->tag);
        vprintf(format, vargs);
        printf("\n");
        va_end(vargs);
    }
}


/* XDR read and write */
static int read_ni_id(struct xdr_t* m_in, struct ni_id_t* ni_id) {
    if (m_in->size < 2 * 4) {
        return -1;
    }
    ni_id->object   = xdr_read_long(m_in);
    ni_id->instance = xdr_read_long(m_in);
    return 0;
}

static void write_ni_id(struct xdr_t* m_out, struct ni_id_t* ni_id) {
    xdr_write_long(m_out, ni_id->object);
    xdr_write_long(m_out, ni_id->instance);
}

static void write_ni_namelist(struct xdr_t* m_out, struct ni_val_t* names) {
    xdr_write_long(m_out, ni_val_count(names));
    while (names) {
        xdr_write_string(m_out, names->val, (uint32_t)strlen(names->val));
        names = names->next;
    }
}

static void write_ni_proplist(struct xdr_t* m_out, struct ni_prop_t* props) {
    xdr_write_long(m_out, ni_prop_count(props));
    while (props) {
        xdr_write_string(m_out, props->key, (uint32_t)strlen(props->key));
        write_ni_namelist(m_out, ni_prop_get_vals(props, props->key));
        props = props->next;
    }
}


/* Processes */
static int proc_ping(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "PING");
    return RPC_SUCCESS;
}

static int proc_statistics(struct rpc_t* rpc, struct nidb_t* ni) {
    char checksum[32];
    struct ni_prop_t* props = NULL;

    struct xdr_t* m_out = rpc->m_out;

    snprintf(checksum, sizeof(checksum), "%u", ni_node_checksum(ni->root));
    ni_prop_add(&props, "checksum", checksum);
    
    ni_log(rpc, ni, "STATISTICS checksum=%s", checksum);
    
    write_ni_proplist(m_out, props);
    
    ni_prop_delete(&props);
    
    return RPC_SUCCESS;
}

static int proc_root(struct rpc_t* rpc, struct nidb_t* ni) {
    struct xdr_t* m_out = rpc->m_out;
    
    ni_log(rpc, ni, "ROOT");
    
    xdr_write_long(m_out, NI_OK);
        
    write_ni_id(m_out, &ni->root->id);
    
    return RPC_SUCCESS;
}

static int proc_self(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    ni_log(rpc, ni, "SELF obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    xdr_write_long(m_out, status);
    if (status == NI_OK)
        write_ni_id(m_out, &node->id);
    
    return RPC_SUCCESS;
}

static int proc_parent(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;

    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "PARENT obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "PARENT: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    vfscat(dbg, " = ", DBGMAX);
#endif
    
    if (node && node->parent == NULL)
        status = NI_NETROOT;
    

    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        xdr_write_long(m_out, node->parent->id.object);
        write_ni_id(m_out, &node->id);
#if DBG
        snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), "%d", node->parent->id.object);
#endif
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    return RPC_SUCCESS;
}

static int proc_create(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "CREATE unimplemented");
    
    return RPC_PROC_UNAVAIL;
}

static int proc_destroy(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "DESTROY unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_read(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "READ obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "READ: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
#endif
    
    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        write_ni_id(m_out, &node->id);
        write_ni_proplist(m_out, node->props);
#if DBG
        prop_to_string(dbg, node->props);
#endif
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    return RPC_SUCCESS;
}

static int proc_write(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "WRITE unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_children(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_node_t* child;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "CHILDREN obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "CHILDREN: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    vfscat(dbg, " =", DBGMAX);
#endif
    
    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        child = node->children;
        xdr_write_long(m_out, ni_node_count(child));
        while (child) {
            xdr_write_long(m_out, child->id.object);
#if DBG
            snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " %d", child->id.object);
#endif
            child = child->next;
        }
        
        write_ni_id(m_out, &node->id);
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    return RPC_SUCCESS;
}

static int proc_lookup(struct rpc_t* rpc, struct nidb_t* ni) {
    char key[MAXNAMELEN+1];
    char val[MAXNAMELEN+1];
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_id_map_t* idmap = NULL;
    struct ni_id_map_t* list = NULL;
    int count = 0;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    if (xdr_read_string(m_in, key, sizeof(key)) < 0) return RPC_GARBAGE_ARGS;
    if (xdr_read_string(m_in, val, sizeof(val)) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "LOOKUP obj=%d, inst=%d, key='%s', val='%s'", ni_id.object, ni_id.instance, key, val);

#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "LOOKUP: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " %s:%s =", key, val);
#endif
    
    if (node) {
        count = ni_node_find_by_prop(node, &idmap, key, val);
        list = idmap;
        if (count == 0) {
            status = NI_NODIR;
        }
    }
    
    xdr_write_long(m_out, status);

    if (status == NI_OK) {
        xdr_write_long(m_out, count);
        while (list) {
            xdr_write_long(m_out, list->node->id.object);
#if DBG
            snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " %d", list->node->id.object);
#endif
            list = list->next;
        }
        write_ni_id(m_out, &node->id);
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    
    id_map_delete(&idmap);
    
    return RPC_SUCCESS;
}

static int proc_list(struct rpc_t* rpc, struct nidb_t* ni) {
    char name[MAXNAMELEN+1];
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_node_t* child;
    struct ni_val_t* values;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    if (xdr_read_string(m_in, name, sizeof(name)) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "LIST obj=%d, inst=%d, name='%s'", ni_id.object, ni_id.instance, name);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "LIST: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " %s", name);
#endif
    
    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        child = node->children;
        xdr_write_long(m_out, ni_node_count(child));
        while (child) {
            xdr_write_long(m_out, child->id.object);
            values = ni_prop_get_vals(child->props, name);
            if (values == NULL) {
                xdr_write_long(m_out, 0);
            } else {
                xdr_write_long(m_out, 1);
                write_ni_namelist(m_out, values);
#if DBG
                snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " [%d] =", child->id.object);
                val_to_string(dbg, values);
#endif
            }
            child = child->next;
        }
        
        write_ni_id(m_out, &node->id);
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    
    return RPC_SUCCESS;
}

static int proc_createprop(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "CREATEPROP unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_destroyprop(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "DESTROYPROP unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_readprop(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_val_t* values;
    uint32_t index;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    if (m_in->size < 4) return RPC_GARBAGE_ARGS;
    index = xdr_read_long(m_in);
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "READPROP obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "READPROP: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " [%d] =", index);
#endif
    
    if (node) {
        values = ni_prop_get_vals_by_index(node->props, index);
        if (values == NULL) {
            status = NI_NOPROP;
        }
    }
    
    xdr_write_long(m_out, status);
    
    if (status == NI_OK) {
        write_ni_namelist(m_out, values);
        write_ni_id(m_out, &node->id);
#if DBG
        val_to_string(dbg, values);
#endif
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    
    return RPC_SUCCESS;
}

static int proc_writeprop(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "WRITEPROP unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_renameprop(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "RENAMEPROP unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_listprops(struct rpc_t* rpc, struct nidb_t* ni) {
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_val_t* names = NULL;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "LISTPROPS obj=%d, inst=%d", ni_id.object, ni_id.instance);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "LISTPROPS: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    vfscat(dbg, " =", DBGMAX);
#endif
    
    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        names = ni_node_get_prop_names(node);
        write_ni_namelist(m_out, names);
        write_ni_id(m_out, &node->id);
#if DBG
        val_to_string(dbg, names);
#endif
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    ni_val_delete(&names);

    return RPC_SUCCESS;
}

static int proc_createname(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "CREATENAME unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_destroyname(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "DESTROYNAME unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_readname(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "READNAME unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_writename(struct rpc_t* rpc, struct nidb_t* ni) {
    return RPC_PROC_UNAVAIL;
}

static int proc_rparent(struct rpc_t* rpc, struct nidb_t* ni) {
    struct xdr_t* m_out = rpc->m_out;
    
    xdr_write_long(m_out, NI_NETROOT);
    
    ni_log(rpc, ni, "RPARENT");
    
    return RPC_SUCCESS;
}

static int proc_listall(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "LISTALL unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_bind(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "BIND unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_readall(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "READALL unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_crashed(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "CRASHED unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_resync(struct rpc_t* rpc, struct nidb_t* ni) {
    ni_log(rpc, ni, "RESYNC unimplemented");

    return RPC_PROC_UNAVAIL;
}

static int proc_lookupread(struct rpc_t* rpc, struct nidb_t* ni) {
    char key[MAXNAMELEN+1];
    char val[MAXNAMELEN+1];
    int count;
    struct ni_id_t ni_id;
    struct ni_node_t* node;
    struct ni_id_map_t* idmap = NULL;
    struct ni_id_map_t* list = NULL;
    struct ni_prop_t* result = NULL;
    struct ni_prop_t* props;
    enum ni_status status = NI_OK;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if (read_ni_id(m_in, &ni_id) < 0) return RPC_GARBAGE_ARGS;
    
    if (xdr_read_string(m_in, key, sizeof(key)) < 0) return RPC_GARBAGE_ARGS;
    if (xdr_read_string(m_in, val, sizeof(val)) < 0) return RPC_GARBAGE_ARGS;
    
    node = ni_node_find(ni->root, &ni_id, &status, 0);
    
    ni_log(rpc, ni, "LOOKUPREAD obj=%d, inst=%d, key='%s', val='%s'", ni_id.object, ni_id.instance, key, val);
    
#if DBG
    char dbg[DBGMAX];
    vfscpy(dbg, "LOOKUPREAD: ", DBGMAX);
    if (node) node_path_to_string(dbg, node);
    snprintf(dbg + strlen(dbg), DBGMAX - strlen(dbg), " %s:%s", key, val);
#endif
    
    if (node) { 
        count = ni_node_find_by_prop(node, &idmap, key, val);
        list = idmap;
        if (count == 0) {
            status = NI_NODIR;
        }
    }
    
    xdr_write_long(m_out, status);
    if (status == NI_OK) {
        while (list) {
            props = list->node->props;
            while (props) {
                struct ni_val_t* vals = props->val;
                while (vals) {
                    ni_prop_add(&result, props->key, vals->val);
                    vals = vals->next;
                }
                props = props->next;
            }
            list = list->next;
        }
        
        write_ni_id(m_out, &node->id);
        write_ni_proplist(m_out, result);
#if DBG
        prop_to_string(dbg, result);
#endif
    }
    
#if DBG
    printf("%s (%s)\n", dbg, status_to_string(status));
#endif
    
    id_map_delete(&idmap);
    ni_prop_delete(&result);
    
    return RPC_SUCCESS;
}

#if 0
static struct ni_prog_t* ni_prog_find(struct rpc_t* rpc) {
    struct ni_prog_t* ni = nidb;
    
    while (ni) {
        if (rpc->prot == IPPROTO_UDP && rpc->port == ni->udp_port) {
            return ni;
        }
        if (rpc->prot == IPPROTO_TCP && rpc->port == ni->tcp_port) {
            return ni;
        }
        ni = ni->next;
    }
    return ni;
}
#endif
int netinfo_prog(struct rpc_t* rpc) {
#if 0
    struct ni_prog_t* ni;
    
    ni = ni_prog_find(rpc);
#else
    struct nidb_t* ni = nidb;
#endif
    switch (rpc->proc) {
        case NETINFOPROC_PING:
            return proc_ping(rpc, ni);
            
        case NETINFOPROC_STATISTICS:
            return proc_statistics(rpc, ni);
            
        case NETINFOPROC_ROOT:
            return proc_root(rpc, ni);
            
        case NETINFOPROC_SELF:
            return proc_self(rpc, ni);
            
        case NETINFOPROC_PARENT:
            return proc_parent(rpc, ni);
            
        case NETINFOPROC_CREATE:
            return proc_create(rpc, ni);
            
        case NETINFOPROC_DESTROY:
            return proc_destroy(rpc, ni);
            
        case NETINFOPROC_READ:
            return proc_read(rpc, ni);
            
        case NETINFOPROC_WRITE:
            return proc_write(rpc, ni);
            
        case NETINFOPROC_CHILDREN:
            return proc_children(rpc, ni);
            
        case NETINFOPROC_LOOKUP:
            return proc_lookup(rpc, ni);
            
        case NETINFOPROC_LIST:
            return proc_list(rpc, ni);
            
        case NETINFOPROC_CREATEPROP:
            return proc_createprop(rpc, ni);
            
        case NETINFOPROC_DESTROYPROP:
            return proc_destroyprop(rpc, ni);
            
        case NETINFOPROC_READPROP:
            return proc_readprop(rpc, ni);
            
        case NETINFOPROC_WRITEPROP:
            return proc_writeprop(rpc, ni);
            
        case NETINFOPROC_RENAMEPROP:
            return proc_renameprop(rpc, ni);
            
        case NETINFOPROC_LISTPROPS:
            return proc_listprops(rpc, ni);
            
        case NETINFOPROC_CREATENAME:
            return proc_createname(rpc, ni);
            
        case NETINFOPROC_DESTROYNAME:
            return proc_destroyname(rpc, ni);
            
        case NETINFOPROC_READNAME:
            return proc_readname(rpc, ni);
            
        case NETINFOPROC_WRITENAME:
            return proc_writename(rpc, ni);
            
        case NETINFOPROC_RPARENT:
            return proc_rparent(rpc, ni);
            
        case NETINFOPROC_LISTALL:
            return proc_listall(rpc, ni);
            
        case NETINFOPROC_BIND:
            return proc_bind(rpc, ni);
            
        case NETINFOPROC_READALL:
            return proc_readall(rpc, ni);
            
        case NETINFOPROC_CRASHED:
            return proc_crashed(rpc, ni);
            
        case NETINFOPROC_RESYNC:
            return proc_resync(rpc, ni);
            
        case NETINFOPROC_LOOKUPREAD:
            return proc_lookupread(rpc, ni);
            
        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}
