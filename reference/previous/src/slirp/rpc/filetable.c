/*
 * File Table
 * 
 * Created by Simon Schubiger on 04.03.2019
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


static size_t ft_hash(uint64_t fhandle) {
    return fhandle & HASH_MASK;
}

static void ft_add(struct ft_t* ft, uint64_t fhandle, const char* path) {
    struct ft_entry_t** entry;
    size_t index = ft_hash(fhandle);
    
    entry = &ft->table[index];
    
    while (*entry) {
        if ((*entry)->fhandle == fhandle) {
            if (strcmp((*entry)->path, path)) {
                printf("FILE TABLE ENTRY PATH CHANGED: %s->%s\n", (*entry)->path, path);
                free((*entry)->path);
                (*entry)->path = strdup(path);
            }
            return;
        }
        entry = &(*entry)->next;
    }
    *entry = (struct ft_entry_t*)malloc(sizeof(struct ft_entry_t));
    (*entry)->fhandle = fhandle;
    (*entry)->path    = strdup(path);
    (*entry)->next    = NULL;
}

static void ft_erase(struct ft_t* ft, uint64_t fhandle) {
    struct ft_entry_t** entry;
    struct ft_entry_t* next;
    size_t index = ft_hash(fhandle);
    
    entry = &ft->table[index];
    
    while (*entry) {
        if ((*entry)->fhandle == fhandle) {
            free((*entry)->path);
            next = (*entry)->next;
            free(*entry);
            *entry = next;
            return;
        }
        entry = &(*entry)->next;
    }
}

static void ft_delete(struct ft_t* ft, size_t index) {
    struct ft_entry_t** entry;
    struct ft_entry_t* next;
    
    entry = &ft->table[index];
    
    while (*entry) {
        free((*entry)->path);
        next = (*entry)->next;
        free((*entry));
        *entry = next;
    }
}

static char* ft_find(struct ft_t* ft, uint64_t fhandle) {
    struct ft_entry_t* entry;
    size_t index = ft_hash(fhandle);
    
    entry = ft->table[index];
    
    while (entry) {
        if (entry->fhandle == fhandle) {
            return entry->path;
        }
        entry = entry->next;
    }
    return NULL;
}


struct ft_t* ft_init(const char* host_path, const char* vfs_path_alias) {
    int i;
    struct ft_t* ft = NULL;
    struct vfs_t* vfs = vfs_init(host_path, vfs_path_alias);
    if (vfs) {
        ft = (struct ft_t*)malloc(sizeof(struct ft_t));
        if (ft) {
            for (i = 0; i < HASH_SIZE; i++) {
                ft->table[i] = NULL;
            }
            ft->vfs = vfs;
        } else {
            vfs_uninit(vfs);
        }
    }
    return ft;
}

struct ft_t* ft_uninit(struct ft_t* ft) {
    int i;
    if (ft) {
        for (i = 0; i < HASH_SIZE; i++) {
            ft_delete(ft, i);
        }
        ft->vfs = vfs_uninit(ft->vfs);
        free(ft);
    }
    return NULL;
}

int ft_is_inited(struct ft_t* ft) {
    if (ft) {
        return 1;
    }
    return 0;
}

int ft_path_changed(struct ft_t* ft, const char* host_path) {
    if (strcmp(ft->vfs->base_path.host, host_path)) {
        return 1;
    }
    return 0;
}

int ft_get_canonical_path(struct ft_t* ft, uint64_t fhandle, char* vfs_path) {
    char* result;
    
    result = ft_find(ft, fhandle);
    if (result == NULL) {
        strcpy(vfs_path, "");
        return 0;
    } else {
        vfscpy(vfs_path, result, MAXPATHLEN);
        return 1;
    }
}

int ft_stat(struct ft_t* ft, const struct path_t* path, struct stat* fstat) {
    return vfs_get_fstat(ft->vfs, path, fstat);
}

void ft_move(struct ft_t* ft, uint64_t fhandle_from, struct path_t* path_to) {
    char vfs_path[MAXPATHLEN];
    
    vfscpy(vfs_path, path_to->vfs, sizeof(vfs_path));
    vfs_path_canonicalize(vfs_path);
    
    ft_erase(ft, fhandle_from);
    ft_add(ft, vfs_get_fhandle(path_to), vfs_path);
}

void ft_remove(struct ft_t* ft, uint64_t fhandle) {
    ft_erase(ft, fhandle);
}

uint64_t ft_get_fhandle(struct ft_t* ft, const struct path_t* path) {
    char vfs_path[MAXPATHLEN];
    uint64_t fhandle;
    
    vfscpy(vfs_path, path->vfs, sizeof(vfs_path));
    vfs_path_canonicalize(vfs_path);
    
    fhandle = vfs_get_fhandle(path);
    ft_add(ft, fhandle, vfs_path);
    
    return fhandle;
}

void ft_set_sattr(struct ft_t* ft, struct path_t* path, struct sattr_t* sattr) {
    vfs_set_sattr(ft->vfs, path, sattr);
}

void ft_get_sattr(struct ft_t* ft, struct path_t* path, struct sattr_t* sattr) {
    vfs_get_sattr(ft->vfs, path, sattr);
}
