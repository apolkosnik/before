#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <ftw.h>
#include <libgen.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include <sys/time.h>
#include <inttypes.h>

#include "config.h"
#include "NFS2Prog.h"
#include "FileTableNFSD.h"
#include "nfsd.h"

#ifndef _WIN32

#if !HAVE_STRUCT_STAT_ST_ATIMESPEC
#define st_atimespec st_atim
#endif

#if !HAVE_STRUCT_STAT_ST_MTIMESPEC
#define st_mtimespec st_mtim
#endif

#endif

using namespace std;

enum {
	NFS_OK = 0,
	NFSERR_PERM = 1,
	NFSERR_NOENT = 2,
	NFSERR_IO = 5,
	NFSERR_NXIO = 6,
	NFSERR_ACCES = 13,
	NFSERR_EXIST = 17,
	NFSERR_NODEV = 19,
	NFSERR_NOTDIR = 20,
	NFSERR_ISDIR = 21,
	NFSERR_FBIG = 27,
	NFSERR_NOSPC = 28,
	NFSERR_ROFS = 30,
	NFSERR_NAMETOOLONG = 63,
	NFSERR_NOTEMPTY = 66,
	NFSERR_DQUOT = 69,
	NFSERR_STALE = 70,
	NFSERR_WFLUSH = 99,
};

enum NFTYPE { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFSOCK, NFFIFO, NFBAD };

#define NFS_FIFO_DEV 0xFFFFFFFF

CNFS2Prog::CNFS2Prog() : CRPCProg(PROG_NFS, 2, "nfsd") {
    #define RPC_PROG_CLASS CNFS2Prog
    SET_PROC(1,  GETATTR);
    SET_PROC(2,  SETATTR);
    SET_PROC(4,  LOOKUP);
    SET_PROC(5,  READLINK);
    SET_PROC(6,  READ);
    SET_PROC(7,  WRITECACHE);
    SET_PROC(8,  WRITE);
    SET_PROC(9,  CREATE);
    SET_PROC(10, REMOVE);
    SET_PROC(11, RENAME);
    SET_PROC(12, LINK);
    SET_PROC(13, SYMLINK);
    SET_PROC(14, MKDIR);
    SET_PROC(15, RMDIR);
    SET_PROC(16, READDIR);
    SET_PROC(17, STATFS);
}

CNFS2Prog::~CNFS2Prog() { }

void CNFS2Prog::setUserID(uint32_t uid, uint32_t gid) {
    nfsd_fts[0]->setDefaultUID_GID(uid, gid);
}

int CNFS2Prog::procedureGETATTR(void) {
    string   path;
    
	getPath(path);
        
    log("GETATTR %s", path.c_str());
	if (!(checkFile(path)))
		return PRC_OK;

	m_out->write(NFS_OK);
	writeFileAttributes(path);
    return PRC_OK;
}

static void set_attrs(const string& path, const FileAttrs& fstat) {
    FileAttrs newAttrs = nfsd_fts[0]->getFileAttrs(path);

    if(FileAttrs::valid16(fstat.mode)) {
        newAttrs.mode &= S_IFMT;
        newAttrs.mode |= fstat.mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        nfsd_fts[0]->vfsChmod(path, newAttrs.mode);
        if(fstat.mode & S_IFMT)
            newAttrs.mode &= ~S_IFMT;
        newAttrs.mode |= fstat.mode;
    }
    if(FileAttrs::valid16(fstat.uid))
        newAttrs.uid = fstat.uid;
    if(FileAttrs::valid16(fstat.gid))
        newAttrs.gid = fstat.gid;
    if(FileAttrs::valid16(fstat.rdev))
        newAttrs.rdev = fstat.rdev;

    timeval times[2];
    timeval now;
    gettimeofday(&now, NULL);
    times[0].tv_sec  = FileAttrs::valid32(fstat.atime_sec)  ? fstat.atime_sec  : now.tv_sec;
    times[0].tv_usec = FileAttrs::valid32(fstat.atime_usec) ? fstat.atime_usec : now.tv_usec;
    times[1].tv_sec  = FileAttrs::valid32(fstat.mtime_sec)  ? fstat.mtime_sec  : now.tv_sec;
    times[1].tv_usec = FileAttrs::valid32(fstat.mtime_usec) ? fstat.mtime_usec : now.tv_usec;
    if(FileAttrs::valid32(fstat.atime_sec) || FileAttrs::valid32(fstat.mtime_sec))
        nfsd_fts[0]->vfsUtimes(path, times);
    nfsd_fts[0]->setFileAttrs(path, newAttrs);
}

static struct stat read_stat(XDRInput* xin) {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime_sec;
    uint32_t atime_usec;
    uint32_t mtime_sec;
    uint32_t mtime_usec;
    
    xin->read(&mode);
    xin->read(&uid);
    xin->read(&gid);
    xin->read(&size);
    xin->read(&atime_sec);
    xin->read(&atime_usec);
    xin->read(&mtime_sec);
    xin->read(&mtime_usec);
    
    struct stat result;
    result.st_mode              = mode;
    result.st_uid               = uid;
    result.st_gid               = gid;
    result.st_size              = size;
#ifdef _WIN32
    result.st_atime             = atime_sec;
    result.st_mtime             = mtime_sec;
#else
    result.st_atimespec.tv_sec  = atime_sec;
    result.st_atimespec.tv_nsec = atime_usec * 1000;
    result.st_mtimespec.tv_sec  = mtime_sec;
    result.st_mtimespec.tv_nsec = mtime_usec * 1000;
    result.st_rdev              = FATTR_INVALID;
#endif
    return result;
}

int CNFS2Prog::procedureSETATTR(void) {
    string   path;
    
	getPath(path);
    
    log("SETATTR %s", path.c_str());
    
	if (!(checkFile(path)))
		return PRC_OK;

    set_attrs(path, FileAttrs(read_stat(m_in)));
    
    m_out->write(NFS_OK);
	writeFileAttributes(path);
    return PRC_OK;
}

static void write_handle(XDROutput* xout, uint64_t handle) {
    uint64_t data[4] = {handle,0,0,0};
    xout->write(data, FHSIZE);
}

int CNFS2Prog::procedureLOOKUP(void) {
    string path;

	getFullPath(path);
	if (!(checkFile(path)))
		return PRC_OK;

    uint64_t handle = nfsd_fts[0]->getFileHandle(path);
    if(handle) {
        m_out->write(NFS_OK);
        log("LOOKUP %s=%" PRIu64, path.c_str(), handle);
        write_handle(m_out, handle);
        writeFileAttributes(path);
    } else {
        m_out->write(NFSERR_NOENT);
    }
    return PRC_OK;
}

int nfs_err(int error) {
    switch (error) {
        case 0:      return NFS_OK;
        case ENOENT: return NFSERR_NOENT;
        case EACCES: return NFSERR_ACCES;
        case EINVAL: return NFSERR_IO;
        default:
            return NFSERR_IO;
    }
}

int CNFS2Prog::procedureREADLINK(void) {
    string   path;
    
    getPath(path);
    log("READLINK %s", path.c_str());
    if (!(checkFile(path)))
        return PRC_OK;
    
    VFSPath result;
    if(int err = nfsd_fts[0]->vfsReadlink(path, result)) {
        m_out->write(nfs_err(err));
    } else {
        m_out->write(NFS_OK);
        XDRString data(result.string());
        m_out->write(data);
    }
    
    return PRC_OK;
}

int CNFS2Prog::procedureREAD(void) {
    string   path;
    uint32_t nOffset;
    uint32_t nCount;
    uint32_t nTotalCount;

	getPath(path);
    log("READ %s", path.c_str());
	if (!(checkFile(path)))
		return PRC_OK;

	m_in->read(&nOffset);
	m_in->read(&nCount);
	m_in->read(&nTotalCount);
    
    XDROpaque buffer(nCount);
    VFSFile file(*nfsd_fts[0], path, "rb");
    if(file.isOpen()) {
        nCount = file.read(nOffset, buffer.m_data, buffer.m_size);
        buffer.resize(nCount);
        m_out->write(NFS_OK);
    } else {
        buffer.resize(0);
        m_out->write(nfs_err(errno));
    }
	writeFileAttributes(path);
    m_out->write(buffer);

    return PRC_OK;
}

int CNFS2Prog::procedureWRITECACHE(void) {
    m_out->write(NFS_OK);
    return PRC_OK;
}

int CNFS2Prog::procedureWRITE(void) {
    string   path;
    uint32_t nBeginOffset;
    uint32_t nOffset;
    uint32_t nTotalCount;

	getPath(path);
    log("WRITE %s", path.c_str());
	if (!(checkFile(path)))
		return PRC_OK;

	m_in->read(&nBeginOffset);
	m_in->read(&nOffset);
	m_in->read(&nTotalCount);

    XDROpaque buffer;
	m_in->read(buffer);

    FileAttrs attrs = nfsd_fts[0]->getFileAttrs(path);
    if((attrs.mode & S_IFMT) == S_IFREG) {
        VFSFile file(*nfsd_fts[0], path, "r+b");
        if(file.isOpen()) {
            file.write(nOffset, buffer.m_data, buffer.m_size);
            m_out->write(NFS_OK);
        } else {
            m_out->write(nfs_err(errno));
        }
    } else {
        m_out->write(NFSERR_ISDIR);
    }

	writeFileAttributes(path);

    return PRC_OK;
}

int CNFS2Prog::procedureCREATE(void) {
    string path;
    
	if(!(getFullPath(path)))
		return PRC_OK;
    log("CREATE %s", path.c_str());
    
    FileAttrs fstat(read_stat(m_in));
    
    if(!(FileAttrs::valid16(fstat.uid))) fstat.uid = nfsd_fts[0]->vfsGetUID(path, false);
    if(!(FileAttrs::valid16(fstat.gid))) fstat.gid = nfsd_fts[0]->vfsGetGID(path, true);
    
    // size field is used to set device numbers for special devices over NFS
    if(S_ISCHR(fstat.mode)) {
        if(fstat.size == NFS_FIFO_DEV) {
            fstat.mode = (fstat.mode & ~S_IFMT) | S_IFIFO;
        } else {
            fstat.rdev = fstat.size;
        }
        fstat.size = 0;
    } else if(S_ISBLK(fstat.mode)) {
        fstat.rdev = fstat.size;
        fstat.size = 0;
    }
    
    if(nfsd_fts[0]->vfsAccess(path, F_OK) == 0) {
        if(!(FileAttrs::valid32(fstat.size)) || fstat.size) {
            set_attrs(path, fstat);
            m_out->write(NFS_OK);
            write_handle(m_out, nfsd_fts[0]->getFileHandle(path));
            writeFileAttributes(path);
            
            return PRC_OK;
        }
    }
    // file does not exist or must be truncated (fstat.size == 0),
    VFSFile file(*nfsd_fts[0], path, "wb");
    if(file.isOpen()) {
        set_attrs(path, fstat);
        m_out->write(NFS_OK);
        write_handle(m_out, nfsd_fts[0]->getFileHandle(path));
        writeFileAttributes(path);
    } else {
        nfs_err(errno);
    }
    return PRC_OK;
}

int CNFS2Prog::procedureREMOVE(void) {
    string path;

	getFullPath(path);
    log("REMOVE %s", path.c_str());
	if (!(checkFile(path)))
		return PRC_OK;

    uint64_t fileHandle(nfsd_fts[0]->getFileHandle(path));
    int err = nfs_err(nfsd_fts[0]->vfsRemove(path));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->remove(fileHandle);

    return PRC_OK;
}

int CNFS2Prog::procedureRENAME(void) {
    string pathFrom;
    string pathTo;

	getFullPath(pathFrom);
	if (!(checkFile(pathFrom)))
		return PRC_OK;
	getFullPath(pathTo);
    log("RENAME %s->%s", pathFrom.c_str(), pathTo.c_str());

    uint64_t fileHandleFrom(nfsd_fts[0]->getFileHandle(pathFrom));
    int err = nfs_err(nfsd_fts[0]->vfsRename(pathFrom, pathTo));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->move(fileHandleFrom, pathTo);
    
    return PRC_OK;
}

int CNFS2Prog::procedureLINK(void) {
    string   to;
    string   from;

    getPath(from);
    getFullPath(to);
    log("LINK %s->%s", from.c_str(), to.c_str());
    
    m_out->write(nfs_err(nfsd_fts[0]->vfsLink(from, to, false)));
    
    return PRC_OK;
}

int CNFS2Prog::procedureSYMLINK(void) {
    string to;
    
    getFullPath(to);
    XDRString from;
    m_in->read(from);
    log("SYMLINK %s->%s", from.c_str(), to.c_str());
    
    FileAttrs fstat(read_stat(m_in));
    int err = nfsd_fts[0]->vfsLink(from.c_str(), to, true);
    if(!(err)) set_attrs(to, fstat);
    m_out->write(nfs_err(err));
    
    return PRC_OK;
}

int CNFS2Prog::procedureMKDIR(void) {
    string path;

	log("MKDIR");
	if(!(getFullPath(path)))
		return PRC_OK;

    FileAttrs fstat(read_stat(m_in));
    if(int err = nfsd_fts[0]->vfsMkdir(path, DEFAULT_PERM)) {
        nfs_err(err);
    } else {
        set_attrs(path, fstat);
        m_out->write(NFS_OK);
        write_handle(m_out, nfsd_fts[0]->getFileHandle(path));
        writeFileAttributes(path);
    }
    
    return PRC_OK;
}

int CNFS2Prog::procedureRMDIR(void) {
    string path;

	log("RMDIR");
	getFullPath(path);
	if (!(checkFile(path)))
		return PRC_OK;
    
    uint64_t fileHandle(nfsd_fts[0]->getFileHandle(path));
    int err = nfs_err(nfsd_fts[0]->vfsNftw(path, VirtualFS::remove, 3, FTW_DEPTH | FTW_PHYS));
    m_out->write(err);
    if(!(err)) nfsd_fts[0]->remove(fileHandle);
    
    return PRC_OK;
}

int CNFS2Prog::procedureREADDIR(void) {
    string   path;
    DIR*     handle;
    uint32_t cookie;
    uint32_t count;
    uint64_t fhandle;
    
	getPath(path, &fhandle);
	if (!(checkFile(path)))
		return PRC_OK;
    m_in->read(&cookie);
    m_in->read(&count);
    
    log("READDIR %s", path.c_str());
    
    uint32_t eof = 1;
    handle          = nfsd_fts[0]->vfsOpendir(path);
	if (handle) {
        m_out->write(NFS_OK);
        int skip = cookie;
        for(struct dirent* fileinfo = readdir(handle); fileinfo; fileinfo = readdir(handle)) {
#if HAVE_STRUCT_DIRENT_D_NAMELEN
            size_t namelen = fileinfo->d_namlen;
#else
            size_t namelen = strlen(fileinfo->d_name);
#endif
            if(--skip >= 0) continue;
            
#ifndef _WIN32
            m_out->write(1);  //value follows
            m_out->write(nfsd_fts[0]->fileId(fileinfo->d_ino));
#endif
            string dname(fileinfo->d_name, namelen);
            XDRString name(dname);
            log("%d %s %s", cookie, path.c_str(), name.c_str());
#ifdef _WIN32
            const VFSPath pth = VFSPath(path) / VFSPath(dname);
            const uint64_t fileno = nfsd_fts[0]->getFileHandle(pth);
            m_out->write(1);  //value follows
            m_out->write(nfsd_fts[0]->fileId(fileno));
#endif
            m_out->write(name);
            m_out->write(cookie+1);
            cookie++;
            if(m_out->size() >= count - 128) { // 128: give some space for XDR data
                eof = 0;
                break;
            }
		};
		closedir(handle);
        m_out->write(0);  //no value follows
        m_out->write(eof);
    } else {
        m_out->write(nfs_err(errno));
    }

    return PRC_OK;
}

static const int BLOCK_SIZE = 4096;

static uint32_t nfs_blocks(const struct statvfs* fsstat, uint32_t fsblocks) {
    uint64_t result = fsblocks;
    // take minimum as block size, looks like every filesystem uses these fields somwhat different
    result *= (uint64_t)min(fsstat->f_frsize, fsstat->f_bsize);
    result /= BLOCK_SIZE;
    if(result >= 0x7FFFFFFF) result = 0x7FFFFFFF; // fix size for signed 32bit
    return static_cast<uint32_t>(result);
}

int CNFS2Prog::procedureSTATFS(void) {
    string path;
	struct statvfs fsstat;

	log("STATFS");
	getPath(path);
	if(!(checkFile(path)))
		return PRC_OK;

    if(int err = nfsd_fts[0]->vfsStatvfs(path, fsstat)) {
        m_out->write(nfs_err(err));
    } else {
        m_out->write(NFS_OK);
        m_out->write(BLOCK_SIZE*2);  //transfer size
        m_out->write(BLOCK_SIZE);  //block size
        m_out->write(nfs_blocks(&fsstat, fsstat.f_blocks));  //total blocks
        m_out->write(nfs_blocks(&fsstat, fsstat.f_bfree));  //free blocks
        m_out->write(nfs_blocks(&fsstat, fsstat.f_bavail));  //available blocks
    }
    
    return PRC_OK;
}

bool CNFS2Prog::getPath(string& result, uint64_t* handle) {
    uint64_t data[4];
    m_in->read((void*)data, FHSIZE);
    if(handle) *handle = data[0];
    return nfsd_fts[0]->getCanonicalPath(data[0], result);
}

bool CNFS2Prog::getFullPath(string& result) {
    if(!(getPath(result)))
        return false;
    
    XDRString path;
    m_in->read(path);
    if(result[result.length()-1] != '/') result += "/";
    result += path.c_str();
    return true;
}

bool CNFS2Prog::checkFile(const string& path) {
	if (path.length() == 0) {
		m_out->write(NFSERR_STALE);
		return false;
	}
    
#ifndef _WIN32
    // links always pass (will be resolved on the client side via readlink)
    struct stat fstat;
    if(nfsd_fts[0]->stat(path, fstat) == 0 && (fstat.st_mode & S_IFMT) == S_IFLNK)
        return true;
#endif
    
    if(nfsd_fts[0]->vfsAccess(path, F_OK)) {
		m_out->write(NFSERR_NOENT);
		return false;
	}

    return true;
}

bool CNFS2Prog::writeFileAttributes(const string& path) {
	struct stat fstat;

	if (nfsd_fts[0]->stat(path, fstat) != 0)
		return false;

    uint32_t type = NFNON;
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

	m_out->write(type);  //type
	m_out->write(fstat.st_mode & 0xFFFF);  //mode
	m_out->write(fstat.st_nlink);  //nlink
	m_out->write(fstat.st_uid);  //uid
	m_out->write(fstat.st_gid);  //gid
	m_out->write(static_cast<uint32_t>(fstat.st_size));  //size
	m_out->write(fstat.st_blksize);  //blocksize
	m_out->write(fstat.st_rdev);  //rdev
	m_out->write(static_cast<uint32_t>(fstat.st_blocks));  //blocks
	m_out->write(fstat.st_dev);  //fsid
    m_out->write(nfsd_fts[0]->fileId(fstat.st_ino));
	m_out->write(static_cast<uint32_t>(fstat.st_atimespec.tv_sec));  //atime
	m_out->write(static_cast<uint32_t>(fstat.st_atimespec.tv_nsec / 1000));  //atime
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_sec));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_nsec / 1000));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_sec));  //ctime -- ignored, we use mtime instead
	m_out->write(static_cast<uint32_t>(fstat.st_mtimespec.tv_nsec / 1000));  //ctime
#else
	if (type == NFDIR && fstat.st_size == 0) {
		fstat.st_size = BLOCK_SIZE;
	}
	m_out->write(type);  //type
	m_out->write(static_cast<uint32_t>(fstat.st_mode & 0xFFFF));  //mode
	m_out->write(static_cast<uint32_t>(fstat.st_nlink));  //nlink
	m_out->write(static_cast<uint32_t>(fstat.st_uid));  //uid
	m_out->write(static_cast<uint32_t>(fstat.st_gid));  //gid
	m_out->write(static_cast<uint32_t>(fstat.st_size));  //size
	m_out->write(static_cast<uint32_t>(BLOCK_SIZE));  //blocksize
	m_out->write(static_cast<uint32_t>(fstat.st_rdev));  //rdev
	m_out->write(static_cast<uint32_t>((fstat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE));  //blocks
	m_out->write(static_cast<uint32_t>(fstat.st_dev));  //fsid
	m_out->write(nfsd_fts[0]->fileId(nfsd_fts[0]->getFileHandle(path)));
	m_out->write(static_cast<uint32_t>(fstat.st_atime));  //atime
	m_out->write(static_cast<uint32_t>(0));  //atime
	m_out->write(static_cast<uint32_t>(fstat.st_mtime));  //mtime
	m_out->write(static_cast<uint32_t>(0));  //mtime
	m_out->write(static_cast<uint32_t>(fstat.st_mtime));  //ctime -- ignored, we use mtime instead
	m_out->write(static_cast<uint32_t>(0));  //ctime
#endif
	return true;
}
