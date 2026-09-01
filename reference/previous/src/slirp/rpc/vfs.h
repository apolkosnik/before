/* Virtual File System */

#ifndef _VFS_H_
#define _VFS_H_

#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifdef _WIN32
typedef uint32_t fsblkcnt_t;
typedef uint32_t fsfilcnt_t;
struct statvfs
{
    unsigned long int f_bsize;
    unsigned long int f_frsize;
    fsblkcnt_t f_blocks;
    fsblkcnt_t f_bfree;
    fsblkcnt_t f_bavail;
    fsfilcnt_t f_files;
    fsfilcnt_t f_ffree;
    fsfilcnt_t f_favail;
    unsigned long int f_fsid;
    unsigned long int f_flag;
    unsigned long int f_namemax;
};
#else
#include <sys/statvfs.h>
#endif

#define DEFAULT_PERM 0755
#define FATTR_INVALID ~0

struct timeval_t {
    uint32_t sec;
    uint32_t usec;
};

struct sattr_t {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    struct timeval_t atime;
    struct timeval_t mtime;
    
    uint32_t rdev; /* FIXME: used for CREATE but does not belong here */
};

int valid16(uint32_t statval);
int valid32(uint32_t statval);

struct fattr_t {
    uint32_t type;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t blocksize;
    uint32_t rdev;
    uint32_t blocks;
    uint32_t fsid;
    uint32_t fileid;
    struct timeval_t atime;
    struct timeval_t mtime;
    struct timeval_t ctime;
};


/* The maximum number of bytes in a pathname argument. */
#define MAXPATHLEN 1024

/* The maximum number of bytes in a filename argument. */
#define MAXNAMELEN 255

struct path_t {
    char vfs[MAXPATHLEN];
    char host[FILENAME_MAX];
};

struct vfs_t {
    struct path_t base_path;
    
    uint32_t uid;
    uint32_t gid;
};

size_t vfscpy(char* dst, const char* src, size_t size);
size_t vfscat(char* dst, const char* src, size_t size);

size_t vfs_join(char* path, const char* name, size_t size);

size_t vfs_to_host_path(struct vfs_t* vfs, struct path_t* path);
size_t vfs_to_vfs_path(struct vfs_t* vfs, struct path_t* path);

void vfs_path_canonicalize(char* vfs_path);
uint32_t vfs_file_id(uint64_t ino);

uint32_t vfs_get_parent_gid(struct vfs_t* vfs, const struct path_t* path);
int vfs_get_fstat(struct vfs_t* vfs, const struct path_t* path, struct stat* fstat);
void vfs_get_sattr(struct vfs_t* vfs, const struct path_t* path, struct sattr_t* sattr);
void vfs_set_sattr(struct vfs_t* vfs, const struct path_t* path, struct sattr_t* sattr);
void vfs_stat_to_sattr(const struct stat* fstat, struct sattr_t* sattr);

int vfs_chmod(const struct path_t* path, mode_t mode);
int vfs_utimes(const struct path_t* path, struct timeval times[2]);
int vfs_stat(const struct path_t* path, struct stat* fstat);

uint64_t vfs_get_fhandle(const struct path_t* path);
int vfs_readlink(const struct path_t* path, struct path_t* result);
int vfs_read(const struct path_t* path, uint32_t offset, uint8_t* data, uint32_t* len);
int vfs_write(const struct path_t* path, uint32_t offset, uint8_t* data, uint32_t len);
int vfs_create(const struct path_t* path, uint8_t* data, uint32_t len);
int vfs_remove(const struct path_t* path);
int vfs_rename(const struct path_t* path_from, const struct path_t* path_to);
int vfs_link(const struct path_t* path_from, const struct path_t* path_to, int soft);
int vfs_mkdir(const struct path_t* path);
int vfs_rmdir(const struct path_t* path);
DIR* vfs_opendir(const struct path_t* path);
int vfs_statfs(const struct path_t* path, struct statvfs* fsstat);
int vfs_access(const struct path_t* path, int mode);

void vfs_set_process_uid_gid(struct vfs_t* vfs, uint32_t uid, uint32_t gid);
void vfs_get_basepath_alias(struct vfs_t* vfs, char* path, size_t maxlen);

struct vfs_t* vfs_init(const char* host_path, const char* vfs_path_alias);
struct vfs_t* vfs_uninit(struct vfs_t* vfs);

#endif /* _VFS_H_ */
