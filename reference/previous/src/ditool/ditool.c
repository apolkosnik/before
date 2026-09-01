/*
 *  ditool.c (former ditool.cpp)
 *  Previous
 *
 *  Created by Simon Schubiger.
 *
 *  Rewritten in C by Andreas Grabher.
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <fcntl.h>
#define __USE_XOPEN_EXTENDED 1 /* required for Linux */
#include <ftw.h>

#include "config.h"
#include "ufs.h"
#include "netboot.h"
#include "rpc/vfs.h"

#ifndef _WIN32

#if !HAVE_STRUCT_STAT_ST_ATIMESPEC
#define st_atimespec st_atim
#endif

#if !HAVE_STRUCT_STAT_ST_MTIMESPEC
#define st_mtimespec st_mtim
#endif

#endif


/* Helper functions */
#define HASH_BITS 10
#define HASH_SIZE (1<<HASH_BITS)
#define HASH_MASK (HASH_SIZE-1)

#define GET_HASH(x) ((x)&HASH_MASK)


struct i2i_t {
    uint32_t inode32;
    uint64_t inode64;
    struct i2i_t* next;
};

static struct i2i_t** i2i_create(void) {
    struct i2i_t** table = (struct i2i_t**)malloc(sizeof(struct i2i_t*) * HASH_SIZE);
    for (int i = 0; i < HASH_SIZE; i++) {
        table[i] = NULL;
    }
    return table;
}

static void i2i_delete(struct i2i_t** table) {
    struct i2i_t* entry;
    struct i2i_t* next;
    for (int i = 0; i < HASH_SIZE; i++) {
        entry = table[i];
        while (entry) {
            next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(table);
}

static void i2i_add(struct i2i_t** table, uint32_t inode32, uint64_t inode64) {
    struct i2i_t** entry = &table[GET_HASH(inode32)];
    while (*entry) {
        if ((*entry)->inode32 == inode32) {
            if ((*entry)->inode64 != inode64) {
                printf("i2i error: value exists with different pairing\n");
            }
            return;
        }
        entry = &(*entry)->next;
    }
    *entry = (struct i2i_t*)malloc(sizeof(struct i2i_t));
    (*entry)->inode32 = inode32;
    (*entry)->inode64 = inode64;
    (*entry)->next = NULL;
}

static uint64_t i2i_find(struct i2i_t** table, uint32_t inode32) {
    struct i2i_t* entry = table[GET_HASH(inode32)];
    while (entry) {
        if (entry->inode32 == inode32) {
            return entry->inode64;
        }
        entry = entry->next;
    }
    return 0;
}


struct i2p_t {
    uint32_t inode32;
    char* path;
    struct i2p_t* next;
};

static struct i2p_t** i2p_create(void) {
    struct i2p_t** table = (struct i2p_t**)malloc(sizeof(struct i2p_t*) * HASH_SIZE);
    for (int i = 0; i < HASH_SIZE; i++) {
        table[i] = NULL;
    }
    return table;
}

static void i2p_delete(struct i2p_t** table) {
    struct i2p_t* entry;
    struct i2p_t* next;
    for (int i = 0; i < HASH_SIZE; i++) {
        entry = table[i];
        while (entry) {
            next = entry->next;
            free(entry->path);
            free(entry);
            entry = next;
        }
    }
    free(table);
}

static void i2p_add(struct i2p_t** table, uint32_t inode32, const char* path) {
    struct i2p_t** entry = &table[GET_HASH(inode32)];
    while (*entry) {
        if ((*entry)->inode32 == inode32) {
            if (strcmp((*entry)->path, path)) {
                printf("i2p error: value exists with different pairing\n");
            }
            return;
        }
        entry = &(*entry)->next;
    }
    *entry = (struct i2p_t*)malloc(sizeof(struct i2p_t));
    (*entry)->inode32 = inode32;
    (*entry)->path = strdup(path);
    (*entry)->next = NULL;
}

static char* i2p_find(struct i2p_t** table, uint32_t inode32) {
    struct i2p_t* entry = table[GET_HASH(inode32)];
    while (entry) {
        if (entry->inode32 == inode32) {
            return entry->path;
        }
        entry = entry->next;
    }
    return NULL;
}


struct skip_t {
    char* path;
    struct skip_t* next;
};

static void skip_add(struct skip_t** skip, const char* path) {
    if (path == NULL) {
        return;
    }
    while (*skip) {
        if (strcmp((*skip)->path, path) == 0) {
            printf("skip error: value exists\n");
            return;
        }
        skip = &(*skip)->next;
    }
    *skip = (struct skip_t*)malloc(sizeof(struct skip_t));
    (*skip)->path = strdup(path);
    (*skip)->next = NULL;
}

static void skip_delete(struct skip_t* skip) {
    struct skip_t* next;
    while (skip) {
        next = skip->next;
        free(skip->path);
        free(skip);
        skip = next;
    }
}

static int skip_find(struct skip_t* skip, const char* path) {
    while (skip) {
        if (strcmp(skip->path, path) == 0) {
            return 1;
        }
        skip = skip->next;
    }
    return 0;
}


/* Disk image tool functions */
static const char* get_option(const char** args, int num_args, const char* opt) {
    int i;
    for (i = 0; i < num_args; i++) {
        if (strcmp(args[i], opt) == 0) {
            if (++i < num_args) {
                return args[i];
            }
            break;
        }
    }
    return NULL;
}

static bool has_option(const char** args, int num_args, const char* opt) {
    int i;
    for (i = 0; i < num_args; i++) {
        if (strcmp(args[i], opt) == 0) {
            return true;
        }
    }
    return false;
}

static int get_valid_partnum(const char* num) {
    if (num) {
        char c = num[0];
        if (c >= '0' && c < '0' + NPART) return c - '0';
        if (c >= 'a' && c < 'a' + NPART) return c - 'a';
        if (c >= 'A' && c < 'A' + NPART) return c - 'A';
    }
    return -1;
}

static void print_version(void) {
    printf("ditool (Previous) 2.0\n");
}

static void print_help(bool intro) {
    if (intro) {
        print_version();
        printf("This is a utility for working with NeXT-formatted disk images. This program is\n");
        printf("useful for extracting files from a disk image into a directory. It can prepare\n");
        printf("the directory for netboot with the NeXT Computer emulator 'Previous'.\n\n");
    }
    printf("usage: ditool -im <disk_image_file> [options]\n");
    printf("options:\n");
    printf("  -h          Print this help and a short introduction.\n");
    printf("  -v          Print version number.\n");
    printf("  -im <file>  Raw disk image file to read from.\n");
    printf("  -lsp        List partitions in disk image.\n");
    printf("  -p <letter> Partition {a|b|c|...} to work on.\n");
    printf("  -ls         List files in disk image.\n");
    printf("  -lst <type> List files of type {FILE|DIR|SLINK|HLINK|FIFO|CHAR|BLOCK|SOCK}.\n");
    printf("  -out <path> Copy files from disk image to <path>.\n");
    printf("  -clean      Delete all files in output directory before copying.\n");
    printf("  -netboot    Prepare files in output directory for netboot.\n");
}

static bool ignore_name(const char* name) {
    return strcmp(".", name) == 0 || strcmp("..", name) == 0;
}

static void make_path(const char* path, const char* name, struct path_t* result, struct vfs_t* ft) {
    if (path && strlen(path) > 0) {
        vfscpy(result->vfs, path, sizeof(result->vfs));
    } else {
        vfscpy(result->vfs, "/", sizeof(result->vfs));
    }
    if (name && strlen(name) > 0) {
        vfs_join(result->vfs, name, sizeof(result->vfs));
    }
    if (strcmp(result->vfs, "/.") == 0) {
        vfscpy(result->vfs, "/", sizeof(result->vfs));
    }
    if (ft) {
        vfs_to_host_path(ft, result);
    }
}

static void copy_attrs(struct stat* fstat, struct icommon* inode, uint32_t rdev) {
    fstat->st_mode              = ntohs(inode->ic_mode);
    fstat->st_uid               = ntohs(inode->ic_uid);
    fstat->st_gid               = ntohs(inode->ic_gid);
    fstat->st_size              = ntohl(inode->ic_size);
#ifdef _WIN32
    fstat->st_atime             = ntohl(inode->ic_atime.tv_sec);
    fstat->st_mtime             = ntohl(inode->ic_mtime.tv_sec);
#else
    fstat->st_atimespec.tv_sec  = ntohl(inode->ic_atime.tv_sec);
    fstat->st_atimespec.tv_nsec = ntohl(inode->ic_atime.tv_usec) * 1000;
    fstat->st_mtimespec.tv_sec  = ntohl(inode->ic_mtime.tv_sec);
    fstat->st_mtimespec.tv_nsec = ntohl(inode->ic_mtime.tv_usec) * 1000;
#endif
    fstat->st_rdev              = rdev;
}

static void set_attrs(struct icommon* inode, uint32_t rdev, struct path_t* dirEntPath, struct vfs_t* ft) {
    struct stat fstat;
    struct sattr_t sattr;
    struct timeval times[2];
    
    copy_attrs(&fstat, inode, rdev);
    vfs_stat_to_sattr(&fstat, &sattr);
    vfs_set_sattr(ft, dirEntPath, &sattr);
    
#ifdef _WIN32
    times[0].tv_sec  = fstat.st_atime;
    times[0].tv_usec = 0;
    times[1].tv_sec  = fstat.st_mtime;
    times[1].tv_usec = 0;
#else
    times[0].tv_sec  = fstat.st_atimespec.tv_sec;
    times[0].tv_usec = (int32_t)(fstat.st_atimespec.tv_nsec / 1000);
    times[1].tv_sec  = fstat.st_mtimespec.tv_sec;
    times[1].tv_usec = (int32_t)(fstat.st_mtimespec.tv_nsec / 1000);
#endif
    
    if (vfs_chmod(dirEntPath, fstat.st_mode & ~IFMT))
        printf("Unable to set mode for %s\n", dirEntPath->vfs);
    if (vfs_utimes(dirEntPath, times))
        printf("Unable to set times for %s\n", dirEntPath->vfs);
}

static void set_attrs_recr(struct ufs_t* ufs, struct skip_t* skip, uint32_t ino, const char* path, struct vfs_t* ft) {
    struct dirlist_t* dirlist = ufs_list(ufs, ino);
    struct dirlist_t* entry = dirlist;
    
    while (entry) {
        struct direct* dirEnt = &entry->dir;
        struct path_t dirEntPath;
        struct icommon inode;
        uint32_t rdev = 0;
        
        make_path(path, dirEnt->d_name, &dirEntPath, ft);
        
        entry = entry->next;
        
        if (ignore_name(dirEnt->d_name)) {
            continue;
        }
        if (skip_find(skip, dirEntPath.vfs)) {
            continue;
        }
        
        if (ufs_readInode(ufs, &inode, ntohl(dirEnt->d_inonum)) == ERR_NO) {
            switch (ntohs(inode.ic_mode) & IFMT) {
                case IFDIR:       /* directory */
                    set_attrs_recr(ufs, skip, ntohl(dirEnt->d_inonum), dirEntPath.vfs, ft);
                    break;
                case IFCHR:       /* character special */
                case IFBLK:       /* block special */
                    rdev = ntohl(inode.ic_db[0]);
                    break;
            }
            set_attrs(&inode, rdev, &dirEntPath, ft);
        }
    }
    
    dirlist_delete(dirlist);
}

static void set_attrs_inode(struct ufs_t* ufs, uint32_t ino, const char* path, struct vfs_t* ft) {
    struct path_t dirEntPath;
    struct icommon inode;
    
    make_path(path, NULL, &dirEntPath, ft);
    
    if (ufs_readInode(ufs, &inode, ino) == ERR_NO) {
        set_attrs(&inode, 0, &dirEntPath, ft);
    }
}

static void verify_attr_recr(struct ufs_t* ufs, struct skip_t* skip, uint32_t ino, const char* path, struct vfs_t* ft) {
#if HAVE_SYS_XATTR_H
    bool ignore = false;
#else
    bool ignore = true;
#endif
    struct dirlist_t* dirlist = ufs_list(ufs, ino);
    struct dirlist_t* entry = dirlist;
    
    while (entry) {
        struct direct* dirEnt = &entry->dir;
        struct path_t dirEntPath;
        struct icommon inode;
        uint32_t rdev = 0;
        
        make_path(path, dirEnt->d_name, &dirEntPath, ft);
        
        entry = entry->next;
        
        if (skip_find(skip, dirEntPath.vfs)) {
            continue;
        }
        
        if (ufs_readInode(ufs, &inode, ntohl(dirEnt->d_inonum)) == ERR_NO) {
            struct stat fstat;
            
            switch (ntohs(inode.ic_mode) & IFMT) {
                case IFDIR:       /* directory */
                    if (!(ignore_name(dirEnt->d_name)))
                        verify_attr_recr(ufs, skip, ntohl(dirEnt->d_inonum), dirEntPath.vfs, ft);
                    break;
                case IFCHR:       /* character special */
                case IFBLK:       /* block special */
                    rdev = ntohl(inode.ic_db[0]);
                    break;
            }
            vfs_get_fstat(ft, &dirEntPath, &fstat);
            
            if (fstat.st_mode != ntohs(inode.ic_mode) && !ignore)
                printf("mode mismatch (act/exp) %o != %o %s\n", fstat.st_mode, ntohs(inode.ic_mode), dirEntPath.vfs);
            if (fstat.st_uid != ntohs(inode.ic_uid) && !ignore)
                printf("uid mismatch (act/exp) %d != %d %s\n", fstat.st_uid, ntohs(inode.ic_uid), dirEntPath.vfs);
            if (fstat.st_gid != ntohs(inode.ic_gid) && !ignore)
                printf("gid mismatch (act/exp) %d != %d %s\n", fstat.st_gid, ntohs(inode.ic_gid), dirEntPath.vfs);
            if ((ntohs(inode.ic_mode) & IFMT) != IFDIR) {
                if (fstat.st_size != ntohl(inode.ic_size))
                    printf("size mismatch (act/exp) %"PRId64" != %d %s\n", fstat.st_size, (int)ntohl(inode.ic_size), dirEntPath.vfs);
#ifdef _WIN32
                if (fstat.st_atime != ntohl(inode.ic_atime.tv_sec) && !ignore)
                    printf("atime_sec mismatch diff: %lld %s\n", fstat.st_atime - ntohl(inode.ic_atime.tv_sec), dirEntPath.vfs);
                if (fstat.st_mtime != ntohl(inode.ic_mtime.tv_sec) && !ignore)
                    printf("mtime_sec mismatch diff: %lld %s\n", fstat.st_mtime - ntohl(inode.ic_mtime.tv_sec), dirEntPath.vfs);
#else
                if (fstat.st_atimespec.tv_sec != ntohl(inode.ic_atime.tv_sec) && !ignore)
                    printf("atime_sec mismatch diff: %ld %s\n", fstat.st_atimespec.tv_sec - ntohl(inode.ic_atime.tv_sec), dirEntPath.vfs);
                if (fstat.st_atimespec.tv_nsec != ntohl(inode.ic_atime.tv_usec) * 1000 && !ignore)
                    printf("atime_nsec mismatch diff: %ld %s\n", fstat.st_atimespec.tv_nsec - (ntohl(inode.ic_atime.tv_usec) * 1000), dirEntPath.vfs);
                if (fstat.st_mtimespec.tv_sec != ntohl(inode.ic_mtime.tv_sec) && !ignore)
                    printf("mtime_sec mismatch diff: %ld %s\n", fstat.st_mtimespec.tv_sec - ntohl(inode.ic_mtime.tv_sec), dirEntPath.vfs);
                if (fstat.st_mtimespec.tv_nsec != ntohl(inode.ic_mtime.tv_usec) * 1000 && !ignore)
                    printf("mtime_nsec mismatch diff: %ld %s\n", fstat.st_mtimespec.tv_nsec - (ntohl(inode.ic_mtime.tv_usec) * 1000), dirEntPath.vfs);
#endif
            }
            if ((uint32_t)fstat.st_rdev != rdev && !ignore)
                printf("rdev mismatch (act/exp) %d != %d %s\n", (int)fstat.st_rdev, rdev, dirEntPath.vfs);
        }
    }
    
    dirlist_delete(dirlist);
}

static void verify_inodes_recr(struct ufs_t* ufs, struct i2i_t** inode2inode, struct skip_t* skip, uint32_t ino, const char* path, struct vfs_t* ft) {
    struct dirlist_t* dirlist = ufs_list(ufs, ino);
    struct dirlist_t* entry = dirlist;
    
    while (entry) {
        struct direct* dirEnt = &entry->dir;
        struct path_t dirEntPath;
        struct icommon inode;
        struct stat fstat;
        uint64_t ino64;
        
        make_path(path, dirEnt->d_name, &dirEntPath, ft);
        
        entry = entry->next;
        
        if (skip_find(skip, dirEntPath.vfs)) {
            continue;
        }
        
        vfs_get_fstat(ft, &dirEntPath, &fstat);
        
        ino64 = i2i_find(inode2inode, dirEnt->d_inonum);
        if (ino64) {
            if (ino64 != fstat.st_ino) {
                printf("inode mismatch (exp/act) %"PRIu64" != %"PRIu64" %s\n", ino64, (uint64_t)fstat.st_ino, dirEntPath.vfs);
            }
        } else {
            i2i_add(inode2inode, dirEnt->d_inonum, fstat.st_ino);
        }
        
        if (ufs_readInode(ufs, &inode, ntohl(dirEnt->d_inonum)) == ERR_NO) {
            switch (ntohs(inode.ic_mode) & IFMT) {
                case IFDIR:       /* directory */
                    if (!(ignore_name(dirEnt->d_name)))
                        verify_inodes_recr(ufs, inode2inode, skip, ntohl(dirEnt->d_inonum), dirEntPath.vfs, ft);
                    break;
            }
        }
    }
    
    dirlist_delete(dirlist);
}

static bool do_print(const char* type, const char* listType, bool doPrint, bool force) {
    if (force) {
        force = false;
        return true;
    }
    if (listType) {
        doPrint = strstr(listType, type) != NULL;
    }
    return doPrint;
}

static int process_inodes_recr(struct ufs_t* ufs, struct i2p_t** inode2path, struct skip_t** skip, uint32_t ino, const char* path, struct vfs_t* ft, bool listFiles, const char* listType) {
    int status = ERR_NO;
    struct dirlist_t* dirlist = ufs_list(ufs, ino);
    struct dirlist_t* entry = dirlist;
    
    while (entry) {
        struct direct* dirEnt = &entry->dir;
        struct path_t dirEntPath;
        struct icommon inode;
        
        make_path(path, dirEnt->d_name, &dirEntPath, ft);
        
        entry = entry->next;
        
        if (ufs_readInode(ufs, &inode, ntohl(dirEnt->d_inonum)) == ERR_NO) {
            char* found_path;
            int err = 0;
            bool doPrint = listFiles;
            bool forcePrint = false;
            
            if (!(ignore_name(dirEnt->d_name)) || strcmp(dirEntPath.vfs, "/") == 0) {
                struct stat fstat;

                if (ft && vfs_stat(&dirEntPath, &fstat) == 0) {
                    if ((ntohs(inode.ic_mode) & IFMT) == IFLNK) {
                        int needfree = 0;
                        char* link = ufs_readlink(ufs, &inode, &needfree);
                        int result = strcasecmp(link, dirEnt->d_name);
                        if (needfree) free(link);
                        if (result == 0) {
                            printf("New file '%s' is link pointing to variant, skipping.\n", dirEntPath.vfs);
                            skip_add(skip, dirEntPath.vfs);
                            continue;
                        }
                    }
#ifndef _WIN32
                    if (S_ISLNK(fstat.st_mode)) {
                        struct path_t link;
                        vfs_readlink(&dirEntPath, &link);
                        vfs_to_vfs_path(ft, &link);
                        if (strcasecmp(link.vfs, dirEnt->d_name) == 0) {
                            struct path_t tmp;
                            printf("Existing file '%s' is link pointing to variant, removing link.\n", dirEntPath.vfs);
                            vfs_remove(&dirEntPath);
                            make_path(path, link.vfs, &tmp, NULL);
                            skip_add(skip, tmp.vfs);
                        }
                    }
#endif
                    if (vfs_stat(&dirEntPath, &fstat) == 0 && strcmp(dirEntPath.vfs, "/")) {
                        printf("WARNING: file '%s' (%s) already exists, skipping.\n", dirEntPath.vfs, dirEntPath.host);
                        skip_add(skip, dirEntPath.vfs);
                        continue;
                    }
                }
                
                found_path = i2p_find(inode2path, ntohl(dirEnt->d_inonum));
                if (found_path) {
                    struct path_t path_from;
                    make_path(found_path, NULL, &path_from, ft);
                    if ((doPrint = do_print("HLINK", listType, doPrint, forcePrint))) printf("[HLINK] %s <- ", found_path);
                    forcePrint = doPrint;
                    if (ft) err = vfs_link(&path_from, &dirEntPath, 0);
                } else {
                    i2p_add(inode2path, ntohl(dirEnt->d_inonum), dirEntPath.vfs);
                }            
            }
            
            switch (ntohs(inode.ic_mode) & IFMT) {
                case IFIFO:       /* named pipe (fifo) */
                    if ((doPrint = do_print("FIFO", listType, doPrint, forcePrint))) printf("[FIFO]  ");
                    if (ft) err = vfs_create(&dirEntPath, NULL, 0);
                    break;
                case IFCHR:       /* character special */
                    if ((doPrint = do_print("CHAR", listType, doPrint, forcePrint))) printf("[CHAR]  ");
                    if (ft) err = vfs_create(&dirEntPath, NULL, 0);
                    break;
                case IFDIR:       /* directory */
                    if ((doPrint = do_print("DIR", listType, doPrint, forcePrint))) printf("[DIR]   ");
                    if (!(ignore_name(dirEnt->d_name))) {
                        if (doPrint) printf("%s\n", dirEntPath.vfs);
                        doPrint = false;
                        if (ft) err = vfs_mkdir(&dirEntPath);
                        status = process_inodes_recr(ufs, inode2path, skip, ntohl(dirEnt->d_inonum), dirEntPath.vfs, ft, listFiles, listType);
                    }
                    break;
                case IFBLK:       /* block special */
                    if ((doPrint = do_print("BLOCK", listType, doPrint, forcePrint))) printf("[BLOCK] ");
                    if (ft) err = vfs_create(&dirEntPath, NULL, 0);
                    break;
                case IFREG:       /* regular */
                    if ((doPrint = do_print("FILE", listType, doPrint, forcePrint))) printf("[FILE]  ");
                    if (ft && vfs_access(&dirEntPath, F_OK) != 0) {
                        uint32_t size = ntohl(inode.ic_size);
                        uint8_t* buffer = (uint8_t*)malloc(size);
                        if (ufs_readFile(ufs, &inode, 0, size, buffer) == ERR_NO) {
                            err = vfs_create(&dirEntPath, buffer, size);
                        }
                        free(buffer);
                    }
                    break;
                case IFLNK: {     /* symbolic link */
                    struct path_t path_from;
                    int needfree = 0;
                    char* link = ufs_readlink(ufs, &inode, &needfree);
                    vfscpy(path_from.vfs, link, sizeof(path_from.vfs));
                    if (needfree) free(link);
                    if ((doPrint = do_print("SLINK", listType, doPrint, forcePrint))) printf("[SLINK] %s <- ", path_from.vfs);
                    if (ft) err = vfs_link(&path_from, &dirEntPath, 1);
                    break;
                }
                case IFSOCK:      /* socket */
                    if ((doPrint = do_print("SOCK", listType, doPrint, forcePrint))) printf("[SOCK]  ");
                    if (ft) err = vfs_create(&dirEntPath, NULL, 0);
                    break;
                default:
                    printf("Unknown format (0%06o) for '%s'.\n", ntohs(inode.ic_mode) & IFMT, dirEntPath.vfs);
                    printf("Suspecting corrupted image. Stopping.\n");
                    status = ERR_FAIL;
                    break;
            }
            
            if (status)
                break;
            
            if (doPrint)
                printf("%s\n", dirEntPath.vfs);
            
            if (err) {
                printf("Can't create '%s' (%s).\n", dirEntPath.vfs, strerror(err));
                skip_add(skip, dirEntPath.vfs);
            }
        }
    }
    
    dirlist_delete(dirlist);
    
    return status;
}

static void dump_part(struct im_t* im, struct part_t* part, const char* outPath, bool listFiles, const char* listType) {
    struct vfs_t* ft           = NULL;
    struct ufs_t* ufs          = NULL;
    struct skip_t* skip        = NULL;
    struct i2i_t** inode2inode = i2i_create();
    struct i2p_t** inode2path  = i2p_create();
    
    ufs = ufs_init(part);
    
    if (ufs) {
        if (outPath) {
            ft = vfs_init(outPath, "/");
        }
        if (ft) {
            printf("---- copying '%s' partition %c to '%s'\n", im->path, part->letter, ft->base_path.host);
            if (process_inodes_recr(ufs, inode2path, &skip, ROOTINO, "", ft, listFiles, listType) == ERR_NO) {
                printf("---- setting file attributes for NFSD\n");
                set_attrs_inode(ufs, ROOTINO, "", ft);
                set_attrs_recr(ufs, skip, ROOTINO, "", ft);
                printf("---- verifying inode structure\n");
                verify_inodes_recr(ufs, inode2inode, skip, ROOTINO, "", ft);
                printf("---- verifying file attributes and sizes\n");
                verify_attr_recr(ufs, skip, ROOTINO, "", ft);
            }
            ft = vfs_uninit(ft);
        } else {
            printf("---- listing '%s' partition %c\n", im->path, part->letter);
            process_inodes_recr(ufs, inode2path, &skip, ROOTINO, "", ft, listFiles, listType);
        }
        ufs_uninit(ufs);
    }
    i2i_delete(inode2inode);
    i2p_delete(inode2path);
    skip_delete(skip);
}

static bool is_mount(const char* path) {
    char pdir[FILENAME_MAX];
    struct stat sdir;  /* inode info */
    struct stat spdir; /* parent inode info */
    int res = stat(path, &sdir);
    if (res < 0) return false;
    vfscpy(pdir, path, sizeof(pdir));
    vfscat(pdir, "/..", sizeof(pdir));
    res = stat(pdir, &spdir);
    if (res < 0) return false;
    return    sdir.st_dev != spdir.st_dev  /* different devices */
           || sdir.st_ino == spdir.st_ino; /* root dir case */
}

static int ditool_remove(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf) {
#ifndef _WIN32
    fchmodat(AT_FDCWD, fpath, ACCESSPERMS, AT_SYMLINK_NOFOLLOW);
    remove(fpath);
#else
    char zzPath[FILENAME_MAX];
    int ret;
    vfscpy(zzPath, fpath, FILENAME_MAX - 1);
    zzPath[strlen(zzPath) + 1] = '\0';
    SHFILEOPSTRUCT file_op = {NULL, FO_DELETE, zzPath, "",
        FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
        false, 0, ""};
    ret = SHFileOperation(&file_op);
    if (ret) {
        return EINVAL;
    }
#endif
    return 0;
}

static void clean_dir(const char* path) {
    printf("---- cleaning '%s'\n", path);
    nftw(path, ditool_remove, 1024, FTW_DEPTH | FTW_PHYS);
    if (is_mount(path)) {
        printf("     - directory %s is a mount point, using as-is\n", path);
    } else if (access(path, F_OK | R_OK | W_OK) == 0) {
        char tmp[32];
        char newName[FILENAME_MAX];
        snprintf(tmp, sizeof(tmp), ".%08X", rand());
        vfscpy(newName, path, sizeof(newName));
        vfscat(newName, tmp, sizeof(newName));
        printf("     - directory %s still exists, trying to rename it to '%s'\n", path, newName);
        rename(path, newName);
    }
}

static bool is_case_insensitive(const char* path) {
    char* p;
    char filename[FILENAME_MAX];
    const char* testFile = ".nfsd__CASE__TEST__";
    FILE* fs;
    vfscpy(filename, path, sizeof(filename));
    vfscat(filename, "/", sizeof(filename));
    vfscat(filename, testFile, sizeof(filename));
    fs = fopen(filename, "wb");
    fclose(fs);
    p = filename + strlen(path) + 1;
    while (*p++) {
        *p = tolower(*p);
    }
    return (access(filename, F_OK) == 0);
}


int main(int argc, const char* argv[]) {
    if (has_option(argv, argc, "-h") || has_option(argv, argc, "--help")) {
        print_help(true);
        return 0;
    }
    if (has_option(argv, argc, "-v") || has_option(argv, argc, "--version")) {
        print_version();
        return 0;
    }
    
    const char* imageFile = get_option(argv, argc, "-im");
    bool        listParts = has_option(argv, argc, "-lsp");
    const char* partNum   = get_option(argv, argc, "-p");
    bool        listFiles = has_option(argv, argc, "-ls");
    const char* listType  = get_option(argv, argc, "-lst");
    const char* outPath   = get_option(argv, argc, "-out");
    bool        clean     = has_option(argv, argc, "-clean");
    bool        netboot   = has_option(argv, argc, "-netboot");
    
    if (imageFile) {
        struct im_t* im = diskimage_init(imageFile);
        if (!diskimage_valid(im)) {
            printf("Can't read '%s' (%s).\n", imageFile, im->error);
            diskimage_uninit(im);
            return 1;
        }
        
        if (listType)
            listFiles = true;
        
        if (listParts)
            diskimage_print(im);
        
        if (listFiles || outPath) {
            struct part_t* parts = im->parts;
            int part = get_valid_partnum(partNum);
            
            if (outPath) {
                if (is_case_insensitive(outPath)) {
                    printf("WARNING: '%s' is on a case insensitive file system.\n", outPath);
                    printf("         NeXTstep requires a case sensitive file system to run properly.\n");
                    printf("         Use i.e. macOS Disk Utility to create a disk image with a\n");
                    printf("         case sensitive file system for your NFS directory.\n");
                }
                if (clean) clean_dir(outPath);
#ifdef _WIN32
                mkdir(outPath);
#else
                mkdir(outPath, DEFAULT_PERM);
#endif
                if (access(outPath, F_OK | R_OK | W_OK) < 0) {
                    printf("Can't access '%s'\n", outPath);
                    return 1;
                }
            }
            
            while (parts) {
                if (part < 0 || part == parts->number) {
                    dump_part(im, parts, outPath, listFiles, listType);
                }
                parts = parts->next;
            }
        }
        diskimage_uninit(im);
        
    } else if (!(netboot)) {
        printf("Missing image file.\n");
        print_help(false);
        return 1;
    }
    
    if (netboot) {
        if (outPath) {
            printf("---- preparing for netboot\n");
            prepare_netboot(outPath);
        } else {
            printf("Missing output path.\n");
            print_help(false);
            return 1;
        }
    }
    
    printf("---- done.\n");
    return 0;
}
