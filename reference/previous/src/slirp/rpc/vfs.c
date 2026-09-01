/*
 * Virtual File System
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
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#if HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef _WIN32
#include <Winsock2.h>
#else

#if !HAVE_STRUCT_STAT_ST_ATIMESPEC
#define st_atimespec st_atim
#endif

#if !HAVE_STRUCT_STAT_ST_MTIMESPEC
#define st_mtimespec st_mtim
#endif

#endif

#include "vfs.h"

#ifdef _WIN32
#define HOST_SEPARATOR "\\"
#else
#define HOST_SEPARATOR "/"
#endif


/* ----- Helpers */
size_t vfscpy(char* dst, const char* src, size_t size) {
    size_t srclen, retval;
    
    srclen = strlen(src);
    retval = srclen;
    
    if (size > 0) {
        if (srclen >= size) {
            srclen = size - 1;
        }
        memcpy(dst, src, srclen);
        dst[srclen] = '\0';
    }
    return retval;
}

size_t vfscat(char* dst, const char* src, size_t size) {
    size_t dstlen, srclen, retval;
    
    dstlen = strlen(dst);
    srclen = strlen(src);
    retval = dstlen + srclen;
    
    if (size > 0) {
        if (dstlen >= size) {
            dstlen = size - 1;
        }
        if (srclen >= size - dstlen) {
            srclen = size - dstlen - 1;
        }
        memcpy(dst + dstlen, src, srclen);
        dst[dstlen + srclen] = '\0';
    }
    return retval;
}

size_t vfs_join(char* path, const char* name, size_t size) {
    int needsep;
    size_t pathlen, namelen, retval;
    
    pathlen = strlen(path);
    namelen = strlen(name);
    needsep = pathlen && namelen && path[pathlen - 1] != '/';
    retval  = pathlen + namelen + needsep;

    if (size > pathlen + needsep + 1) {
        if (needsep) {
            path[pathlen] = '/';
            pathlen += needsep;
        }
        if (namelen >= size - pathlen) {
            namelen = size - pathlen - 1;
        }
        memcpy(path + pathlen, name, namelen);
        path[pathlen + namelen] = '\0';
    }
    return retval;
}

/* ----- VFS and host path */
void vfs_path_canonicalize(char* vfs_path) {
    char* vfsPath = vfs_path;
    char* slashslashptr;
    char* dotdotptr;
    char* slashdotptr;
    char* slashptr;
    
    /* step 1 : replace '//' sequences by a single '/' */
    slashslashptr = vfsPath;
    while(1) {
        slashslashptr = strstr(slashslashptr,"//");
        if(NULL == slashslashptr)
            break;
        /* remove extra '/' */
        memmove(slashslashptr,slashslashptr+1,strlen(slashslashptr+1)+1);
    }
    
    /* step 2 : replace '/./' sequences by a single '/' */
    slashdotptr = vfsPath;
    while(1) {
        slashdotptr = strstr(slashdotptr,"/./");
        if(NULL == slashdotptr)
            break;
        /* strip the extra '/.' */
        memmove(slashdotptr,slashdotptr+2,strlen(slashdotptr+2)+1);
    }
    
    /* step 3 : replace '/<name>/../' sequences by a single '/' */
    
    while(1) {
        dotdotptr = strstr(vfsPath,"/../");
        if(NULL == dotdotptr)
            break;
        if(dotdotptr == vfsPath) {
            /* special case : '/../' at the beginning of the path are replaced
             by a single '/' */
            memmove(vfsPath, vfsPath+3,strlen(vfsPath+3)+1);
            continue;
        }
        
        /* null-terminate the string before the '/../', so that strrchr will
         start looking right before it */
        *dotdotptr = '\0';
        slashptr = strrchr(vfsPath,'/');
        if(NULL == slashptr) {
            /* this happens if this function was called with a relative path.
             don't do that. */
            assert("can't find leading '/' before '/../ sequence\n");
            break;
        }
        memmove(slashptr,dotdotptr+3,strlen(dotdotptr+3)+1);
    }
    
    /* step 4 : remove a trailing '/..' */
    
    dotdotptr = strstr(vfsPath,"/..");
    if(dotdotptr == vfsPath)
    /* if the full path is simply '/..', replace it by '/' */
        vfsPath[1] = '\0';
    else if(NULL != dotdotptr && '\0' == dotdotptr[3]) {
        *dotdotptr = '\0';
        slashptr = strrchr(vfsPath,'/');
        if(NULL != slashptr) {
            /* make sure the last slash isn't the root */
            if (slashptr == vfsPath)
                vfsPath[1] = '\0';
            else
                *slashptr = '\0';
        }
    }
    
    /* step 5 : remove a trailing '/.' */
    slashdotptr = vfsPath;
    while(1) {
        slashdotptr = strstr(slashdotptr,"/.");
        if (NULL == slashdotptr)
            break;
        if (slashdotptr[2] == '\0') {
            if(slashdotptr == vfsPath)
            /* if the full path is simply '/.', replace it by '/' */
                vfsPath[1] = '\0';
            else
                *slashdotptr = '\0';
            break;
        } else {
            /* if '/.' is in the middle of the path as with invisible files,
             skip it and continue */
            slashdotptr += 2;
        }
    }
    
    assert(strstr(vfsPath, "/./") == NULL);
    assert(strstr(vfsPath, "/../") == NULL);
}

static void vfs_get_parent_path(const char* vfs_path, char* parent_path) {
    char* sep;
    strcpy(parent_path, vfs_path);
    sep = strrchr(parent_path, '/');
    if (sep == parent_path) sep[1] = '\0'; /* root directory */
    else if (sep)           sep[0] = '\0';
    else            parent_path[0] = '\0';
}

static int vfs_path_is_absolute(const char* path) {
    return strlen(path) > 0 && path[0] == '/';
}

static char* path_relative(char* path, const char* base_path) {
    if (strstr(path, base_path) == path) {
        return path+strlen(base_path);
    } else {
        return path;
    }
}

static size_t make_host_path(const char* host_base, char* vfs_path, char* host_path) {
    char* p;
        
    if (strchr(vfs_path, '/') == vfs_path) {
        vfs_path++;  /* skip leading '/' */
    }
    if ((p = strrchr(vfs_path, '/')) && p[1] == '\0') {
        p[0] = '\0'; /* remove trailing '/' */
    }
    
    vfscpy(host_path, host_base, FILENAME_MAX);
    
    if (vfs_path[0] != '\0') {
        vfscat(host_path, HOST_SEPARATOR, FILENAME_MAX);
    }
    
    while ((p = strchr(vfs_path, '/'))) {
        p[0] = '\0';
        vfscat(host_path, vfs_path, FILENAME_MAX);
        vfscat(host_path, HOST_SEPARATOR, FILENAME_MAX);
        vfs_path = p + 1;
    }
    return vfscat(host_path, vfs_path, FILENAME_MAX);
}

size_t vfs_to_host_path(struct vfs_t* vfs, struct path_t* path) {
    char vfs_path[MAXPATHLEN];
    
    if (!vfs_path_is_absolute(path->vfs)) {
        printf("path is not absolute\n");
        strcpy(path->host, "");
        return 0;
    }
    strcpy(vfs_path, "/");
    vfscat(vfs_path, path_relative(path->vfs, vfs->base_path.vfs), sizeof(vfs_path));
    vfs_path_canonicalize(vfs_path);
    
    return make_host_path(vfs->base_path.host, vfs_path, path->host);
}

static size_t make_vfs_path(const char* vfs_base, char* host_path, char* vfs_path, int relative) {
    char* p;
    
    if (relative) {
        vfs_path[0] = '\0';
    } else {
        size_t len = vfscpy(vfs_path, vfs_base, MAXPATHLEN);
        if (len > 0 && len < MAXPATHLEN && vfs_path[len - 1] != '/') {
            vfscat(vfs_path, "/", MAXPATHLEN);
        }
        if (strncmp(host_path, HOST_SEPARATOR, strlen(HOST_SEPARATOR)) == 0) {
            host_path += strlen(HOST_SEPARATOR); /* skip leading separator */
        }
    }
    
    while ((p = strstr(host_path, HOST_SEPARATOR))) {
        p[0] = '\0';
        vfscat(vfs_path, host_path, MAXPATHLEN);
        vfscat(vfs_path, "/", MAXPATHLEN);
        host_path = p + strlen(HOST_SEPARATOR);
    }
    return vfscat(vfs_path, host_path, MAXPATHLEN);
}

size_t vfs_to_vfs_path(struct vfs_t* vfs, struct path_t* path) {
    char* host_path = path_relative(path->host, vfs->base_path.host);
    
    return make_vfs_path(vfs->base_path.vfs, host_path, path->vfs, host_path == path->host);
}

static int host_path_is_directory(const char* host_path) {
    struct stat fstat;
    if (stat(host_path, &fstat) == 0)
        return S_ISDIR(fstat.st_mode);
    return 0;
}


/*----- file io */
struct file_t {
    struct stat fstat;
    int restore_stat;
    FILE* file;
};

static struct file_t* file_open(const struct path_t* path, const char* mode) {
    struct file_t* file = (struct file_t*)malloc(sizeof(struct file_t));
    
    file->restore_stat = 0;
    file->file = fopen(path->host, mode);
    if (file->file == NULL && errno == EACCES) {
        file->restore_stat = 1;
        vfs_stat(path, &file->fstat);
        vfs_chmod(path, file->fstat.st_mode);
        file->file = fopen(path->host, mode);
    }
    return file;
}

static void file_close(const struct path_t* path, struct file_t* file) {
    if (file->restore_stat) {
        vfs_chmod(path, file->fstat.st_mode);
        struct timeval times[2];
#ifdef _WIN32
        times[0].tv_sec  = file->fstat.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec  = file->fstat.st_mtime;
        times[1].tv_usec = 0;
#else
        times[0].tv_sec  = file->fstat.st_atimespec.tv_sec;
        times[0].tv_usec = (int32_t)(file->fstat.st_atimespec.tv_nsec / 1000);
        times[1].tv_sec  = file->fstat.st_mtimespec.tv_sec;
        times[1].tv_usec = (int32_t)(file->fstat.st_mtimespec.tv_nsec / 1000);
#endif
        vfs_utimes(path, times);
    }
    if (file->file) fclose(file->file);
    free(file);
}

static int file_is_open(struct file_t* file) {
    return file->file == NULL ? errno : 0;
}

static int file_seek(struct file_t* file, long offset) {
    return fseek(file->file, offset, SEEK_SET) < 0 ? errno : 0;
}

static size_t file_read(struct file_t* file, void* dst, size_t count) {
    return fread(dst, sizeof(uint8_t), count, file->file);
}

static size_t file_write(struct file_t* file, void* src, size_t count) {
    return fwrite(src, sizeof(uint8_t), count, file->file);
}

/*----- file attributes */
int valid32(uint32_t statval) { return statval != 0xFFFFFFFF; }
int valid16(uint32_t statval) { return (statval & 0x0000FFFF) != 0x0000FFFF; }

uint32_t vfs_file_id(uint64_t ino) {
    uint32_t result = (uint32_t)ino;
    return (result ^ (ino >> 32)) & 0x7FFFFFFF;
}

uint32_t vfs_get_parent_gid(struct vfs_t* vfs, const struct path_t* path) {
    struct path_t parent_path;
    
    if (vfs_path_is_absolute(path->vfs)) {
        vfs_get_parent_path(path->vfs, parent_path.vfs);
        vfs_to_host_path(vfs, &parent_path);
        if (host_path_is_directory(parent_path.host)) {
            struct sattr_t sattr;
            vfs_get_sattr(vfs, &parent_path, &sattr);
            return sattr.gid;
        }
    }
    return vfs->gid;
}

int vfs_get_fstat(struct vfs_t* vfs, const struct path_t* path, struct stat* fstat) {
    struct sattr_t sattr;
    int result;
    
    result = vfs_stat(path, fstat);
    vfs_get_sattr(vfs, path, &sattr);
    
    if (valid16(sattr.mode)) {
        /* copy permissions from attributes */
        fstat->st_mode &= S_IFMT;
        fstat->st_mode |= sattr.mode & ~S_IFMT; 
        /* mode heuristics: if file is empty we map it to the various special formats (CHAR, BLOCK, FIFO, etc.) from stored attributes */
        if (S_ISREG(fstat->st_mode) && fstat->st_size == 0 && (sattr.mode & S_IFMT)) {
            fstat->st_mode &= ~S_IFMT;             /* clear format */
            fstat->st_mode |= sattr.mode & S_IFMT; /* copy format from attributes */
        }
    }
    fstat->st_uid  = valid16(sattr.uid)  ? sattr.uid  : fstat->st_uid;
    fstat->st_gid  = valid16(sattr.gid)  ? sattr.gid  : fstat->st_gid;
    fstat->st_rdev = valid16(sattr.rdev) ? sattr.rdev : fstat->st_rdev;
    
    return result;
}

void vfs_stat_to_sattr(const struct stat* stat, struct sattr_t* sattr) {
    sattr->mode       = (uint32_t)stat->st_mode;
    sattr->uid        = (uint32_t)stat->st_uid;
    sattr->gid        = (uint32_t)stat->st_gid;
    sattr->size       = (uint32_t)stat->st_size;
#ifdef _WIN32
    sattr->atime.sec  = (uint32_t)stat->st_atime;
    sattr->atime.usec = 0;
    sattr->mtime.sec  = (uint32_t)stat->st_mtime;
    sattr->mtime.usec = 0;
#else
    sattr->atime.sec  = (uint32_t)stat->st_atimespec.tv_sec;
    sattr->atime.usec = (uint32_t)stat->st_atimespec.tv_nsec / 1000;
    sattr->mtime.sec  = (uint32_t)stat->st_mtimespec.tv_sec;
    sattr->mtime.usec = (uint32_t)stat->st_mtimespec.tv_nsec / 1000;
#endif
    sattr->rdev       = (uint32_t)stat->st_rdev;
}

static int get_error(int result) {
    return result < 0 ? errno : result;
}

int vfs_chmod(const struct path_t* path, mode_t mode) {
#ifdef _WIN32
    return 0; /* not supported */
#else
    return get_error(fchmodat(AT_FDCWD, path->host, mode | S_IWUSR | S_IRUSR, AT_SYMLINK_NOFOLLOW));
#endif
}

int vfs_utimes(const struct path_t* path, struct timeval times[2]) {
#ifdef _WIN32
    return 0; /* not supported */
#else
    return get_error(lutimes(path->host, times));
#endif
}

int vfs_stat(const struct path_t* path, struct stat* fstat) {
#ifdef _WIN32
    return get_error(stat(path->host, fstat));
#else
    return get_error(lstat(path->host, fstat));
#endif
}

#if HAVE_SYS_XATTR_H
const char* NFSD_ATTRS = ".nfsd_fattrs";

static void deserialize(const char* buffer, struct sattr_t* sattr) {
    sscanf(buffer, "0%o:%d:%d:%d", &sattr->mode, &sattr->uid, &sattr->gid, &sattr->rdev);
}

static void serialize(const struct sattr_t* sattr, char* buffer) {
    snprintf(buffer, 128, "0%o:%d:%d:%d", sattr->mode, sattr->uid, sattr->gid, sattr->rdev);
}

static const char* vfs_get_filename(const char* vfs_path) {
    char* sep = strrchr(vfs_path, '/');
    return sep ? (sep + 1) : vfs_path;
}
#endif

void vfs_set_sattr(struct vfs_t* vfs, const struct path_t* path, struct sattr_t* sattr) {
#if HAVE_SYS_XATTR_H
    char buffer[128];
    const char* fname = vfs_get_filename(path->vfs);
    
    assert(strcmp(fname, ".") && strcmp(fname, ".."));
    (void)fname; /* may be unused */
    
    serialize(sattr, buffer);
#if HAVE_LXETXATTR
    if (lsetxattr(path->host, NFSD_ATTRS, buffer, strlen(buffer), 0) != 0)
#else
    if (setxattr(path->host, NFSD_ATTRS, buffer, strlen(buffer), 0, XATTR_NOFOLLOW) != 0)
#endif
        printf("setxattr(%s) failed\n", path->host);
#endif
}

void vfs_get_sattr(struct vfs_t* vfs, const struct path_t* path, struct sattr_t* sattr) {
#if HAVE_SYS_XATTR_H
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
#if HAVE_LXETXATTR
    if (lgetxattr(path->host, NFSD_ATTRS, buffer, sizeof(buffer)) == 0)
#else
    if (getxattr(path->host, NFSD_ATTRS, buffer, sizeof(buffer), 0, XATTR_NOFOLLOW) > 0)
#endif
        deserialize(buffer, sattr);
    else
#endif
    {
        struct stat fstat;
#ifdef _WIN32
        stat(path->host, &fstat);
#else
        lstat(path->host, &fstat);
#endif
        fstat.st_uid = vfs->uid;
        fstat.st_gid = vfs->gid;
        fstat.st_mode |= S_ISDIR(fstat.st_mode) ? 0755 : 0644; /* strcmp(vfs->base_path.vfs, path->vfs) ? 0 : 0755; */
        vfs_stat_to_sattr(&fstat, sattr);
    }
}

/* ----- file handle */
#ifndef _WIN32
static uint64_t rotl(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

static uint64_t make_file_handle(struct stat* fstat) {
    uint64_t result = fstat->st_dev;
    result = rotl(result, 32) ^ fstat->st_ino;
    if (result == 0) result = ~result;
    return result;
}
#endif

uint64_t vfs_get_fhandle(const struct path_t* path) {
    struct stat fstat;
    uint64_t result = 0;
    
    if (vfs_stat(path, &fstat) == 0) {
#ifndef _WIN32
        result = make_file_handle(&fstat);
#else
        HANDLE fhandle;
        fhandle = CreateFileA(path->host,
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (fhandle) {
            BY_HANDLE_FILE_INFORMATION finfo;
            if (GetFileInformationByHandle(fhandle, &finfo)) {
                result = ((uint64_t)(finfo.nFileIndexHigh) << 32) | (uint64_t)(finfo.nFileIndexLow);
            }
            CloseHandle(fhandle);
        }
#endif
    } else {
        printf("No file handle for %s\n", path->vfs);
    }
    
    return result;
}

/* ----- VFS functions */
int vfs_readlink(const struct path_t* path, struct path_t* result) {
#ifdef _WIN32
    return EACCES; /* not supported */
#else
    struct stat sb;
    size_t  bufsiz;
    ssize_t nbytes;
    
    if (lstat(path->host, &sb) == -1)
        return errno;
    
    /* Add one to the link size, so that we can determine whether
     the buffer returned by readlink() was truncated. */
    
    bufsiz = sb.st_size + 1;
    
    /* Some magic symlinks under (for example) /proc and /sys
     report 'st_size' as zero. In that case, take PATH_MAX as
     a "good enough" estimate. */
    
    if (sb.st_size == 0)
        bufsiz = sizeof(result->host);
    
    if (bufsiz > sizeof(result->host))
        return ENAMETOOLONG;
    
    nbytes = readlink(path->host, result->host, bufsiz - 1);
    if (nbytes == -1)
        return errno;
    
    result->host[nbytes] = '\0';
    
    return 0;
#endif
}

int vfs_read(const struct path_t* path, uint32_t offset, uint8_t* data, uint32_t* len) {
    int err = 0;
    struct file_t* file = file_open(path, "rb");
    err = file_is_open(file);
    if (err == 0) {
        err = file_seek(file, offset);
        if (err == 0) {
            *len = (uint32_t)file_read(file, data, *len);
        }
    }
    file_close(path, file);
    return err;
}

int vfs_write(const struct path_t* path, uint32_t offset, uint8_t* data, uint32_t len) {
    int err;
    struct file_t* file = file_open(path, "r+b");
    err = file_is_open(file);
    if (err == 0) {
        err = file_seek(file, offset);
        if (err == 0) {
            err = (file_write(file, data, len) < len) ? ENOSPC : 0;
        }
    }
    file_close(path, file);
    return err;
}

int vfs_create(const struct path_t* path, uint8_t* data, uint32_t len) {
    int err = 0;
    struct file_t* file = file_open(path, "wb");
    err = file_is_open(file);
    if (data && err == 0) {
        err = (file_write(file, data, len) < len) ? ENOSPC : 0;
    }
    file_close(path, file);
    return err;
}

int vfs_remove(const struct path_t* path) {
    return get_error(remove(path->host));
}

int vfs_rename(const struct path_t* path_from, const struct path_t* path_to) {
    return get_error(rename(path_from->host, path_to->host));
}

int vfs_link(const struct path_t* path_from, const struct path_t* path_to, int soft) {
#ifdef _WIN32
    return EACCES; /* not supported */
#else
    const char* from = soft ? path_from->vfs : path_from->host;
    const char* to   = path_to->host;
    
    if(soft) return get_error(symlink(from, to));
    else     return get_error(link   (from, to));
#endif
}

int vfs_mkdir(const struct path_t* path) {
#ifdef _WIN32
    return get_error(mkdir(path->host));
#else
    return get_error(mkdir(path->host, DEFAULT_PERM));
#endif
}

int vfs_rmdir(const struct path_t* path) {
    return get_error(rmdir(path->host));
}

DIR* vfs_opendir(const struct path_t* path) {
    return opendir(path->host);
}

int vfs_statfs(const struct path_t* path, struct statvfs* fsstat) {
#ifndef _WIN32
    return get_error(statvfs(path->host, fsstat));
#else
    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    BOOL res = GetDiskFreeSpaceA(path->host, &sectorsPerCluster,
                                 &bytesPerSector, &freeClusters, &totalClusters);
    if (!res) {
        return GetLastError();
    }
    fsstat->f_bsize  = sectorsPerCluster * bytesPerSector; /* BLOCK_SIZE */
    fsstat->f_frsize = sectorsPerCluster * bytesPerSector;
    fsstat->f_blocks = totalClusters;
    fsstat->f_bfree  = freeClusters;
    fsstat->f_bavail = freeClusters;
    return 0;
#endif
}

int vfs_access(const struct path_t* path, int mode) {
    return get_error(access(path->host, mode));
}

/*----- VFS */
struct vfs_t* vfs_init(const char* host_path, const char* vfs_path_alias) {
    struct vfs_t* vfs = NULL;
    if (host_path && vfs_path_alias && strlen(host_path) && strlen(vfs_path_alias)) {
        vfs = (struct vfs_t*)malloc(sizeof(struct vfs_t));
        if (vfs) {
            char* p;
            vfscpy(vfs->base_path.vfs, vfs_path_alias, sizeof(vfs->base_path.vfs));
            vfscpy(vfs->base_path.host, host_path, sizeof(vfs->base_path.host));
            vfs->uid = 20;
            vfs->gid = 20;
            /* Make sure there is no trailing separator in host path */
            p = vfs->base_path.host + strlen(vfs->base_path.host) - strlen(HOST_SEPARATOR);
            if (strcmp(p, HOST_SEPARATOR) == 0 && p != vfs->base_path.host) {
                p[0] = '\0';
            }
        }
    }
    return vfs;
}

struct vfs_t* vfs_uninit(struct vfs_t* vfs) {
    free(vfs);
    return NULL;
}

void vfs_set_process_uid_gid(struct vfs_t* vfs, uint32_t uid, uint32_t gid) {
    vfs->uid = uid;
    vfs->gid = gid;
}

void vfs_get_basepath_alias(struct vfs_t* vfs, char* path, size_t maxlen) {
    vfscpy(path, vfs->base_path.vfs, maxlen);
}
