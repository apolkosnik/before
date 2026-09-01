/*
 * Network File System Program
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
#ifndef _WIN32
#include <sys/statvfs.h>
#endif

#include "rpc.h"
#include "nfs.h"


#ifndef _WIN32

#if !HAVE_STRUCT_STAT_ST_ATIMESPEC
#define st_atimespec st_atim
#endif

#if !HAVE_STRUCT_STAT_ST_MTIMESPEC
#define st_mtimespec st_mtim
#endif

#endif


enum {
    NFS_OK             = 0,
    NFSERR_PERM        = 1,
    NFSERR_NOENT       = 2,
    NFSERR_IO          = 5,
    NFSERR_NXIO        = 6,
    NFSERR_ACCES       = 13,
    NFSERR_EXIST       = 17,
    NFSERR_NODEV       = 19,
    NFSERR_NOTDIR      = 20,
    NFSERR_ISDIR       = 21,
    NFSERR_FBIG        = 27,
    NFSERR_NOSPC       = 28,
    NFSERR_ROFS        = 30,
    NFSERR_NAMETOOLONG = 63,
    NFSERR_NOTEMPTY    = 66,
    NFSERR_DQUOT       = 69,
    NFSERR_STALE       = 70,
    NFSERR_WFLUSH      = 99
};

static const char* status_str(int status) {
    switch (status) {
        case NFS_OK:             return "OK";
        case NFSERR_PERM:        return "PERM";
        case NFSERR_NOENT:       return "NOENT";
        case NFSERR_IO:          return "IO";
        case NFSERR_NXIO:        return "NXIO";
        case NFSERR_ACCES:       return "ACCES";
        case NFSERR_EXIST:       return "EXIST";
        case NFSERR_NODEV:       return "NODEV";
        case NFSERR_NOTDIR:      return "NOTDIR";
        case NFSERR_ISDIR:       return "ISDIR";
        case NFSERR_FBIG:        return "FBIG";
        case NFSERR_NOSPC:       return "NOSPC";
        case NFSERR_ROFS:        return "ROFS";
        case NFSERR_NAMETOOLONG: return "NAMETOOLONG";
        case NFSERR_NOTEMPTY:    return "NOTEMPTY";
        case NFSERR_DQUOT:       return "DQUOT";
        case NFSERR_STALE:       return "STALE";
        case NFSERR_WFLUSH:      return "WFLUSH";
        default:                 return "unknown";
    }
}

static int nfs_err(int error) {
    switch (error) {
        case 0:            return NFS_OK;
        case EPERM:        return NFSERR_PERM;
        case ENOENT:       return NFSERR_NOENT;
        case EACCES:       return NFSERR_ACCES;
        case EEXIST:       return NFSERR_EXIST;
        case ENOTDIR:      return NFSERR_NOTDIR;
        case EISDIR:       return NFSERR_ISDIR;
        case ENOSPC:       return NFSERR_NOSPC;
        case EROFS:        return NFSERR_ROFS;
        case ENAMETOOLONG: return NFSERR_NAMETOOLONG;
        case ENOTEMPTY:    return NFSERR_NOTEMPTY;
        default:           return NFSERR_IO;
    }
}

enum NFTYPE {
    NFNON  = 0,
    NFREG  = 1,
    NFDIR  = 2,
    NFBLK  = 3,
    NFCHR  = 4,
    NFLNK  = 5,
    NFSOCK = 6,
    NFFIFO = 7,
    NFBAD  = 8
};

#define NFS_FIFO_DEV 0xFFFFFFFF

static const int BLOCK_SIZE = 4096;


static int check_file(struct ft_t* ft, const struct path_t* path, int lookup) {
    int err;
#ifndef _WIN32
    /* links always pass (will be resolved on the client side via readlink) */
    struct stat fstat;
    if (ft_stat(ft, path, &fstat) == 0 && (fstat.st_mode & S_IFMT) == S_IFLNK) {
        return NFS_OK;
    }
#endif
    
    if ((err = vfs_access(path, F_OK))) {
        return lookup ? nfs_err(err) : NFSERR_STALE;
    }
    
    return NFS_OK;
}

static int read_fhandle(struct ft_t* ft, struct xdr_t* m_in, char* vfs_path) {
    uint64_t data[4];
    
    if (m_in->size < FHSIZE) return -1;
    xdr_read_data(m_in, (void*)data, FHSIZE);
    return ft_get_canonical_path(ft, data[0], vfs_path);
}

static int get_path(struct ft_t* ft, struct xdr_t* m_in, struct path_t* path) {
    int found = read_fhandle(ft, m_in, path->vfs);
    
    if (found < 0) {
        return -1;
    }
    if (!found) {
        return NFSERR_NOENT;
    }

    vfs_to_host_path(ft->vfs, path);
    
    return check_file(ft, path, 0);
}

static int read_path(struct ft_t* ft, struct xdr_t* m_in, struct path_t* path, int create) {
    char vfs_path[MAXPATHLEN];
    
    int found = read_fhandle(ft, m_in, path->vfs);
    int len   = xdr_read_string(m_in, vfs_path, sizeof(vfs_path));
    
    if (found < 0 || len < 0) {
        return -1;
    }
    if (found == 0) {
        return NFSERR_NOENT;
    }
    if (len > MAXNAMELEN) {
        return NFSERR_NAMETOOLONG;
    }
    if (vfs_join(path->vfs, vfs_path, sizeof(path->vfs)) >= sizeof(path->vfs)) {
        return NFSERR_NAMETOOLONG;
    }
    if (vfs_to_host_path(ft->vfs, path) >= (int)sizeof(path->host)) {
        return NFSERR_NAMETOOLONG;
    }
    return create ? NFS_OK : check_file(ft, path, 0);
}


static int write_fattr(struct ft_t* ft, struct xdr_t* m_out, const struct path_t* path) {
    struct stat fstat;
    uint32_t type = NFNON;

    if (ft_stat(ft, path, &fstat) != 0) {
        return 0;
    }
    
    if     (S_ISREG (fstat.st_mode)) type = NFREG;
    else if(S_ISDIR (fstat.st_mode)) type = NFDIR;
    else if(S_ISBLK (fstat.st_mode)) type = NFBLK;
    else if(S_ISCHR (fstat.st_mode)) type = NFCHR;
#ifndef _WIN32
    else if(S_ISLNK (fstat.st_mode)) type = NFLNK;
    else if(S_ISSOCK(fstat.st_mode)) type = NFSOCK;
    else if(S_ISFIFO(fstat.st_mode)) {
        type = NFCHR;
        fstat.st_rdev = NFS_FIFO_DEV;
        fstat.st_mode = (fstat.st_mode & ~S_IFMT) | S_IFCHR;
    }
    
    xdr_write_long(m_out, type);
    xdr_write_long(m_out, (uint32_t)(fstat.st_mode & 0xFFFF));
    xdr_write_long(m_out, (uint32_t)(fstat.st_nlink));
    xdr_write_long(m_out, (uint32_t)(fstat.st_uid));
    xdr_write_long(m_out, (uint32_t)(fstat.st_gid));
    xdr_write_long(m_out, (uint32_t)(fstat.st_size));
    xdr_write_long(m_out, (uint32_t)(fstat.st_blksize));
    xdr_write_long(m_out, (uint32_t)(fstat.st_rdev));
    xdr_write_long(m_out, (uint32_t)(fstat.st_blocks));
    xdr_write_long(m_out, (uint32_t)(fstat.st_dev)); /* fsid */
    xdr_write_long(m_out, vfs_file_id(fstat.st_ino));
    xdr_write_long(m_out, (uint32_t)(fstat.st_atimespec.tv_sec));
    xdr_write_long(m_out, (uint32_t)(fstat.st_atimespec.tv_nsec / 1000));
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtimespec.tv_sec));
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtimespec.tv_nsec / 1000));
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtimespec.tv_sec)); /* ctime ignored, we use mtime instead */
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtimespec.tv_nsec / 1000));
#else
    if (type == NFDIR && fstat.st_size == 0) {
        fstat.st_size = BLOCK_SIZE;
    }
    xdr_write_long(m_out, type);
    xdr_write_long(m_out, (uint32_t)(fstat.st_mode & 0xFFFF));
    xdr_write_long(m_out, (uint32_t)(fstat.st_nlink));
    xdr_write_long(m_out, (uint32_t)(fstat.st_uid));
    xdr_write_long(m_out, (uint32_t)(fstat.st_gid));
    xdr_write_long(m_out, (uint32_t)(fstat.st_size));
    xdr_write_long(m_out, (uint32_t)(BLOCK_SIZE));
    xdr_write_long(m_out, (uint32_t)(fstat.st_rdev));
    xdr_write_long(m_out, (uint32_t)((fstat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE));
    xdr_write_long(m_out, (uint32_t)(fstat.st_dev)); /* fsid */
    xdr_write_long(m_out, vfs_file_id(ft_get_fhandle(ft, path)));
    xdr_write_long(m_out, (uint32_t)(fstat.st_atime));
    xdr_write_long(m_out, (uint32_t)(0));
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtime));
    xdr_write_long(m_out, (uint32_t)(0));
    xdr_write_long(m_out, (uint32_t)(fstat.st_mtime)); /* ctime ignored, we use mtime instead */
    xdr_write_long(m_out, (uint32_t)(0));
#endif
    return 1;
}

static int read_sattr(struct xdr_t* m_in, struct sattr_t* sattr) {
    if (m_in->size < 8 * 4) {
        return -1;
    }
    sattr->mode       = xdr_read_long(m_in);
    sattr->uid        = xdr_read_long(m_in);
    sattr->gid        = xdr_read_long(m_in);
    sattr->size       = xdr_read_long(m_in);
    sattr->atime.sec  = xdr_read_long(m_in);
    sattr->atime.usec = xdr_read_long(m_in);
    sattr->mtime.sec  = xdr_read_long(m_in);
    sattr->mtime.usec = xdr_read_long(m_in);
    sattr->rdev       = FATTR_INVALID;
    return 0;
}

static void set_sattr(struct ft_t* ft, struct path_t* path, struct sattr_t* sattr, int create) {
    struct sattr_t new;
    
    ft_get_sattr(ft, path, &new);
    
    if (create) {
        sattr->uid = ft->vfs->uid;
        sattr->gid = vfs_get_parent_gid(ft->vfs, path);
    }
    if (valid16(sattr->mode)) {
        new.mode &= S_IFMT;
        new.mode |= sattr->mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        vfs_chmod(path, new.mode);
        if (sattr->mode & S_IFMT)
            new.mode &= ~S_IFMT;
        new.mode |= sattr->mode;
    }
    if (valid16(sattr->uid))
        new.uid = sattr->uid;
    if (valid16(sattr->gid))
        new.gid = sattr->gid;
    if (valid16(sattr->rdev))
        new.rdev = sattr->rdev;
    
    if (valid32(sattr->atime.sec) || valid32(sattr->mtime.sec)) {
        struct timeval times[2];
        struct timeval now;
        gettimeofday(&now, NULL);
        times[0].tv_sec  = valid32(sattr->atime.sec)  ? sattr->atime.sec  : now.tv_sec;
        times[0].tv_usec = valid32(sattr->atime.usec) ? sattr->atime.usec : now.tv_usec;
        times[1].tv_sec  = valid32(sattr->mtime.sec)  ? sattr->mtime.sec  : now.tv_sec;
        times[1].tv_usec = valid32(sattr->mtime.usec) ? sattr->mtime.usec : now.tv_usec;
        vfs_utimes(path, times);
    }
    ft_set_sattr(ft, path, &new);
}

static void write_handle(struct xdr_t* m_out, uint64_t handle) {
    uint64_t data[4] = {handle,0,0,0};
    xdr_write_data(m_out, (void*)data, FHSIZE);
}


static uint32_t nfs_blocks(const struct statvfs* fsstat, uint64_t fsblocks) {
    /* take minimum as block size, looks like every filesystem uses these fields somewhat different */
    fsblocks *= fsstat->f_frsize > 0 ? fsstat->f_frsize : fsstat->f_bsize;
    if (fsblocks > 0x7FFFFFFF) fsblocks = 0x7FFFFFFF; /* limit size to signed 32-bit integer */
    return (uint32_t)(fsblocks / BLOCK_SIZE);
}


static int proc_getattr(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
        
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "GETATTR %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_setattr(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    struct sattr_t sattr;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    if (read_sattr(m_in, &sattr) < 0) return RPC_GARBAGE_ARGS;
    
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        set_sattr(rpc->ft, &path, &sattr, 0);
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "SETATTR %s (%s)", path.vfs, status_str(status));

    return RPC_SUCCESS;
}

static int proc_root(struct rpc_t* rpc) {
    rpc_log(rpc, "ROOT");
    return RPC_PROC_UNAVAIL;
}

static int proc_lookup(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    uint64_t fhandle = 0;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path, 1)) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        status = check_file(rpc->ft, &path, 1);
    }
    if (status == NFS_OK) {
        fhandle = ft_get_fhandle(rpc->ft, &path);
        if (fhandle == 0) {
            rpc_log(rpc, "LOOKUP %s not found", path);
            status = NFSERR_NOENT;
        }
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        write_handle(m_out, fhandle);
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "LOOKUP %s=%"PRIu64" (%s)", path.vfs, fhandle, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_readlink(struct rpc_t* rpc) {
    struct path_t path;
    struct path_t link_path;
    int status;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        status = nfs_err(vfs_readlink(&path, &link_path));
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        vfs_to_vfs_path(rpc->ft->vfs, &link_path);
        xdr_write_string(m_out, link_path.vfs, sizeof(link_path.vfs));
    }
    rpc_log(rpc, "READLINK %s->%s (%s)", path.vfs, link_path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_read(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    uint8_t* data;
    uint32_t skip;
    
    uint32_t offset;
    uint32_t count;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    if (m_in->size < 3 * 4) return RPC_GARBAGE_ARGS;
    
    offset = xdr_read_long(m_in);
    count  = xdr_read_long(m_in);
    xdr_read_skip(m_in, 4); /* totalcount unused */
    
    if (status == NFS_OK) {
        data = xdr_get_pointer(m_out);
        skip = (1 + 17 + 1) * 4; /* status + fattr + count */
        if (xdr_write_check(m_out, skip + count) < 0) {
            count = 0;
        } else {
            status = nfs_err(vfs_read(&path, offset, data + skip, &count));
        }
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        write_fattr(rpc->ft, m_out, &path);
        xdr_write_long(m_out, count);
        xdr_write_skip(m_out, count); /* written before by vfs_read() */
    }
    rpc_log(rpc, "READ %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_writecache(struct rpc_t* rpc) {
    rpc_log(rpc, "WRITECACHE");
    return RPC_PROC_UNAVAIL;
}

static int proc_write(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    struct sattr_t sattr;
    uint8_t* data;
    uint32_t len;
    
    uint32_t offset;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    if (m_in->size < 4 * 4) return RPC_GARBAGE_ARGS;
    
    xdr_read_skip(m_in, 4); /* beginoffset unused */
    offset = xdr_read_long(m_in);
    xdr_read_skip(m_in, 4); /* totalcount unused */
    
    len = xdr_read_long(m_in);
    data = xdr_get_pointer(m_in); /* read later by vfs_write() */
    if (xdr_read_skip(m_in, len) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        ft_get_sattr(rpc->ft, &path, &sattr);
        if ((sattr.mode & S_IFMT) == S_IFREG) {
            status = nfs_err(vfs_write(&path, offset, data, len));
        } else {
            status = NFSERR_ISDIR;
        }
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "WRITE %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_create(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    struct sattr_t sattr;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path, 1)) < 0) return RPC_GARBAGE_ARGS;
    if (read_sattr(m_in, &sattr) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        /* size field is used to set device numbers for special devices over NFS */
        if (S_ISCHR(sattr.mode)) {
            if (sattr.size == NFS_FIFO_DEV) {
                sattr.mode = (sattr.mode & ~S_IFMT) | S_IFIFO;
            } else {
                sattr.rdev = sattr.size;
            }
            sattr.size = 0;
        } else if (S_ISBLK(sattr.mode)) {
            sattr.rdev = sattr.size;
            sattr.size = 0;
        }
        
        /* if file does not exist or must be truncated (sattr.size == 0) */
        if (vfs_access(&path, F_OK) != 0 || sattr.size == 0) {
            status = nfs_err(vfs_create(&path, NULL, 0));
        }
    }
    
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        set_sattr(rpc->ft, &path, &sattr, 1);
        write_handle(m_out, ft_get_fhandle(rpc->ft, &path));
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "CREATE %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_remove(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    uint64_t fhandle;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path, 0)) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        fhandle = ft_get_fhandle(rpc->ft, &path);
        status = nfs_err(vfs_remove(&path));
        if (status == NFS_OK) ft_remove(rpc->ft, fhandle);
    }
    xdr_write_long(m_out, status);
    rpc_log(rpc, "REMOVE %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_rename(struct rpc_t* rpc) {
    struct path_t path_from;
    struct path_t path_to;
    int status;
    int status_to;
    uint64_t fhandle;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path_from, 0)) < 0) return RPC_GARBAGE_ARGS;
    if ((status_to = read_path(rpc->ft, m_in, &path_to, 1)) < 0) return RPC_GARBAGE_ARGS;
        
    if (status == NFS_OK) {
        if (status_to == NFS_OK) {
            fhandle = ft_get_fhandle(rpc->ft, &path_from);
            status = nfs_err(vfs_rename(&path_from, &path_to));
            if (status == NFS_OK) ft_move(rpc->ft, fhandle, &path_to);
        } else {
            status = status_to;
        }
    }
    xdr_write_long(m_out, status);
    rpc_log(rpc, "RENAME %s->%s (%s)", path_from.vfs, path_to.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_link(struct rpc_t* rpc) {
    struct path_t path_from;
    struct path_t path_to;
    int status;
    int status_to;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path_from)) < 0) return RPC_GARBAGE_ARGS;
    if ((status_to = read_path(rpc->ft, m_in, &path_to, 1)) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        if (status_to == NFS_OK) {
            status = nfs_err(vfs_link(&path_from, &path_to, 0));
        } else {
            status = status_to;
        }
    }
    xdr_write_long(m_out, status);
    rpc_log(rpc, "LINK %s->%s (%s)", path_from.vfs, path_to.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_symlink(struct rpc_t* rpc) {
    struct path_t path_from;
    struct path_t path_to;
    int status;
    struct sattr_t sattr;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path_to, 1)) < 0) return RPC_GARBAGE_ARGS;
    if (xdr_read_string(m_in, path_from.vfs, sizeof(path_from.vfs)) < 0) return RPC_GARBAGE_ARGS;
    if (read_sattr(m_in, &sattr) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        status = nfs_err(vfs_link(&path_from, &path_to, 1));
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        set_sattr(rpc->ft, &path_to, &sattr, 1);
    }
    rpc_log(rpc, "SYMLINK %s->%s (%s)", path_from.vfs, path_to.vfs, status_str(status));

    return RPC_SUCCESS;
}

static int proc_mkdir(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    struct sattr_t sattr;

    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;

    if ((status = read_path(rpc->ft, m_in, &path, 1)) < 0) return RPC_GARBAGE_ARGS;
    if (read_sattr(m_in, &sattr) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        status = nfs_err(vfs_mkdir(&path));
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        set_sattr(rpc->ft, &path, &sattr, 1);
        write_handle(m_out, ft_get_fhandle(rpc->ft, &path));
        write_fattr(rpc->ft, m_out, &path);
    }
    rpc_log(rpc, "MKDIR %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}

static int proc_rmdir(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    uint64_t fhandle;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = read_path(rpc->ft, m_in, &path, 0)) < 0) return RPC_GARBAGE_ARGS;
        
    if (status == NFS_OK) {
        fhandle = ft_get_fhandle(rpc->ft, &path);
        status = nfs_err(vfs_rmdir(&path));
        if (status == NFS_OK) ft_remove(rpc->ft, fhandle);
    }
    xdr_write_long(m_out, status);
    rpc_log(rpc, "RMDIR %s (%s)", path.vfs, status_str(status));

    return RPC_SUCCESS;
}

static int proc_readdir(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    DIR* handle;
    uint32_t cookie;
    uint32_t count;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;
    
    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    if (m_in->size < 2 * 4) return RPC_GARBAGE_ARGS;
    
    cookie = xdr_read_long(m_in);
    count  = xdr_read_long(m_in);
    
    if (status == NFS_OK) {
        handle = vfs_opendir(&path);
        if (!handle) {
            status = nfs_err(errno);
        }
    }
    xdr_write_long(m_out, status);
    rpc_log(rpc, "READDIR %s (%s)", path.vfs, status_str(status));
    
    if (status == NFS_OK && handle) {
        struct dirent* fileinfo;
        uint32_t fileid;
        size_t namelen;
        int skip = cookie;
        int eof  = 1;
        while ((fileinfo = readdir(handle))) {
            if (--skip >= 0) continue;
            /* We assume that d_name is null-terminated */
            if ((namelen = strlen(fileinfo->d_name)) > MAXNAMELEN) {
                rpc_log(rpc, "file name too long: %s", fileinfo->d_name);
                cookie++;
                continue;
            }
            namelen = (namelen + 3) & ~3;    /* must match xdr_write_string() */
            if (count < 4 * 4 + namelen + 4) {  /* includes final valid false */
                eof = 0;
                break;
            }
            count -= 4 * 4 + namelen; /* valid, fileid, namelen, name, cookie */
            rpc_log(rpc, "%d %s %s", cookie, path.vfs, fileinfo->d_name);
            cookie++;
#ifdef _WIN32
            struct path_t file_path;
            vfscpy(file_path.vfs, path.vfs, sizeof(file_path.vfs));
            vfs_join(file_path.vfs, fileinfo->d_name, sizeof(file_path.vfs));
            vfs_to_host_path(rpc->ft->vfs, &file_path);
            fileid = vfs_file_id(ft_get_fhandle(rpc->ft, &file_path));
#else
            fileid = vfs_file_id(fileinfo->d_ino);
#endif
            xdr_write_long(m_out, 1); /* valid entry follows */
            xdr_write_long(m_out, fileid);
            xdr_write_string(m_out, fileinfo->d_name, (uint32_t)namelen);
            xdr_write_long(m_out, cookie);
        }
        xdr_write_long(m_out, 0);  /* no valid entry follows */
        xdr_write_long(m_out, eof);
        closedir(handle);
    }
    
    return RPC_SUCCESS;
}

static int proc_statfs(struct rpc_t* rpc) {
    struct path_t path;
    int status;
    struct statvfs fsstat;
    
    struct xdr_t* m_in  = rpc->m_in;
    struct xdr_t* m_out = rpc->m_out;

    if ((status = get_path(rpc->ft, m_in, &path)) < 0) return RPC_GARBAGE_ARGS;
    
    if (status == NFS_OK) {
        status = nfs_err(vfs_statfs(&path, &fsstat));
    }
    xdr_write_long(m_out, status);
    if (status == NFS_OK) {
        xdr_write_long(m_out, BLOCK_SIZE * 2);                       /* transfer size */
        xdr_write_long(m_out, BLOCK_SIZE);                           /* block size */
        xdr_write_long(m_out, nfs_blocks(&fsstat, fsstat.f_blocks)); /* total blocks */
        xdr_write_long(m_out, nfs_blocks(&fsstat, fsstat.f_bfree));  /* free blocks */
        xdr_write_long(m_out, nfs_blocks(&fsstat, fsstat.f_bavail)); /* available blocks */
    }
    rpc_log(rpc, "STATFS %s (%s)", path.vfs, status_str(status));
    
    return RPC_SUCCESS;
}


int nfs_prog(struct rpc_t* rpc) {
    switch (rpc->proc) {
        case NFSPROC_NULL:
            return proc_null(rpc);
            
        case NFSPROC_GETATTR:
            return proc_getattr(rpc);
            
        case NFSPROC_SETATTR:
            return proc_setattr(rpc);
            
        case NFSPROC_ROOT:
            return proc_root(rpc);
            
        case NFSPROC_LOOKUP:
            return proc_lookup(rpc);
            
        case NFSPROC_READLINK:
            return proc_readlink(rpc);
            
        case NFSPROC_READ:
            return proc_read(rpc);
            
        case NFSPROC_WRITECACHE:
            return proc_writecache(rpc);
            
        case NFSPROC_WRITE:
            return proc_write(rpc);
            
        case NFSPROC_CREATE:
            return proc_create(rpc);
            
        case NFSPROC_REMOVE:
            return proc_remove(rpc);
            
        case NFSPROC_RENAME:
            return proc_rename(rpc);
            
        case NFSPROC_LINK:
            return proc_link(rpc);
            
        case NFSPROC_SYMLINK:
            return proc_symlink(rpc);
            
        case NFSPROC_MKDIR:
            return proc_mkdir(rpc);
            
        case NFSPROC_RMDIR:
            return proc_rmdir(rpc);
            
        case NFSPROC_READDIR:
            return proc_readdir(rpc);
            
        case NFSPROC_STATFS:
            return proc_statfs(rpc);

        default:
            break;
    }
    rpc_log(rpc, "Process unavailable");
    return RPC_PROC_UNAVAIL;
}
