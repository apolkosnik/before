/*
 * Mount Program
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
#include <inttypes.h>

#include "rpc.h"
#include "mount.h"


enum {
    MNT_OK        = 0,
    MNTERR_PERM   = 1,
    MNTERR_NOENT  = 2,
    MNTERR_IO     = 5,
    MNTERR_ACCESS = 13,
    MNTERR_NOTDIR = 20,
    MNTERR_INVAL  = 22
};

struct mount_t {
    char* name;
    char* path;
    struct mount_t* next;
};


static void mnt_add(struct mount_t** entry, char* name, char* path) {
    while (*entry) {
        if (strncmp((*entry)->name, name, MAXNAMELEN) || 
            strncmp((*entry)->path, path, MAXPATHLEN)) {
            entry = &(*entry)->next;
            continue;
        }
        printf("[RPC] Note: rmtab duplicate entry for '%s' from %s.\n", path, name);
        return;
    }
    *entry = (struct mount_t*)malloc(sizeof(struct mount_t));
    (*entry)->name = strdup(name);
    (*entry)->path = strdup(path);
    (*entry)->next = NULL;
}

static void mnt_remove(struct mount_t** entry, char* name, char* path) {
    struct mount_t* next = NULL;
    
    while (*entry) {
        if (strncmp((*entry)->name, name, MAXNAMELEN) || 
            strncmp((*entry)->path, path, MAXPATHLEN)) {
            entry = &(*entry)->next;
            continue;
        }
        free((*entry)->name);
        free((*entry)->path);
        next = (*entry)->next;
        free((*entry));
        *entry = next;
    }
}

static void mnt_remove_all(struct mount_t** entry, char* name) {
    struct mount_t* next = NULL;
    
    while (*entry) {
        if (strncmp((*entry)->name, name, MAXNAMELEN)) {
            entry = &(*entry)->next;
        } else {
            free((*entry)->name);
            free((*entry)->path);
            next = (*entry)->next;
            free((*entry));
            *entry = next;
        }
    }
}

static void mnt_delete(struct mount_t** entry) {
    struct mount_t* next = NULL;

    while (*entry) {
        free((*entry)->name);
        free((*entry)->path);
        next = (*entry)->next;
        free((*entry));
        *entry = next;
    }
}


static int proc_mnt(struct rpc_t* rpc) {
    struct path_t path;
    char name[MAXNAMELEN+1];
    uint64_t handle;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    vfscpy(name, NAME_HOST, sizeof(name));
    
    if (xdr_read_string(m_in, path.vfs, sizeof(path.vfs)) < 0) return RPC_GARBAGE_ARGS;
    vfs_to_host_path(rpc->ft->vfs, &path);
    
    handle = ft_get_fhandle(rpc->ft, &path);
    if (handle) {
        struct stat fstat;
        
        ft_stat(rpc->ft, &path, &fstat);
        if (S_ISDIR(fstat.st_mode)) {
            uint64_t data[4] = {handle, 0, 0, 0};
            
            rpc_log(rpc, "MNT from %s for '%s' = %"PRIu64" (OK)", name, path.vfs, handle);
            
            xdr_write_long(m_out, MNT_OK);
            xdr_write_data(m_out, data, FHSIZE);
            
            mnt_add(&rpc->rmtab, name, path.vfs);
        } else {
            rpc_log(rpc, "MNT '%s' is not a directory (NOTDIR)", path.vfs);
            xdr_write_long(m_out, MNTERR_NOTDIR);
        }
    } else {
        rpc_log(rpc, "MNT '%s' not found (NOENT)", path.vfs);
        xdr_write_long(m_out, MNTERR_NOENT);
    }
    
    return RPC_SUCCESS;
}

static int proc_dump(struct rpc_t* rpc) {
    struct mount_t* entry = rpc->rmtab;
    
    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "DUMP");
    
    while (entry) {
        xdr_write_long(m_out, 1);
        xdr_write_string(m_out, entry->name, MAXNAMELEN);
        xdr_write_string(m_out, entry->path, MAXPATHLEN);
        entry = entry->next;
    }
    
    xdr_write_long(m_out, 0);
    
    return RPC_SUCCESS;
}

static int proc_umnt(struct rpc_t* rpc) {
    char path[MAXPATHLEN];
    char name[MAXNAMELEN+1];
    
    struct xdr_t* m_in  = rpc->m_in;
    
    vfscpy(name, NAME_HOST, sizeof(name));
    
    if (xdr_read_string(m_in, path, sizeof(path)) < 0) return RPC_GARBAGE_ARGS;
    
    rpc_log(rpc, "UMNT from %s for '%s'", name, path);
    
    mnt_remove(&rpc->rmtab, name, path);
    
    return RPC_SUCCESS;
}

static int proc_umntall(struct rpc_t* rpc) {
    char name[MAXNAMELEN+1];
    
    vfscpy(name, NAME_HOST, sizeof(name));
    
    rpc_log(rpc, "UMNTALL from %s", name);
    
    mnt_remove_all(&rpc->rmtab, name);
    
    return RPC_SUCCESS;
}

static int proc_export(struct rpc_t* rpc) {
    char path[MAXPATHLEN];
    
    struct xdr_t* m_out = rpc->m_out;
    
    rpc_log(rpc, "EXPORT");
    
    /* root filesystem */
    vfs_get_basepath_alias(rpc->ft->vfs, path, sizeof(path));
    
    xdr_write_long(m_out, 1);
    xdr_write_string(m_out, path, sizeof(path));
    xdr_write_long(m_out, 0); /* groups (no group entry means everyone) */
    
    /* private filesystem */
    vfs_join(path, "private", sizeof(path));
    
    xdr_write_long(m_out, 1);
    xdr_write_string(m_out, path, sizeof(path));
    xdr_write_long(m_out, 0);
    
    xdr_write_long(m_out, 0); /* no more filesystems */
    
    return RPC_SUCCESS;
}


int mount_prog(struct rpc_t* rpc) {
    switch (rpc->proc) {
        case MOUNTPROC_NULL:
            return proc_null(rpc);
            
        case MOUNTPROC_MNT:
            return proc_mnt(rpc);
            
        case MOUNTPROC_DUMP:
            return proc_dump(rpc);
            
        case MOUNTPROC_UMNT:
            return proc_umnt(rpc);
            
        case MOUNTPROC_UMNTALL:
            return proc_umntall(rpc);
            
        case MOUNTPROC_EXPORT:
            return proc_export(rpc);
            
        case MOUNTPROC_EXPORTALL:
            return proc_export(rpc);
            
        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}

void mount_uninit(struct rpc_t* rpc) {
    mnt_delete(&rpc->rmtab);
}
