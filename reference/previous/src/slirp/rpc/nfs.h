/* Network File System */

#ifndef _NFS_H_
#define _NFS_H_

#define PORT_NFS           2049

#define NFSPROG            100003
#define NFSVERS            2

#define NFSPROC_NULL       0
#define NFSPROC_GETATTR    1
#define NFSPROC_SETATTR    2
#define NFSPROC_ROOT       3
#define NFSPROC_LOOKUP     4
#define NFSPROC_READLINK   5
#define NFSPROC_READ       6
#define NFSPROC_WRITECACHE 7
#define NFSPROC_WRITE      8
#define NFSPROC_CREATE     9
#define NFSPROC_REMOVE     10
#define NFSPROC_RENAME     11
#define NFSPROC_LINK       12
#define NFSPROC_SYMLINK    13
#define NFSPROC_MKDIR      14
#define NFSPROC_RMDIR      15
#define NFSPROC_READDIR    16
#define NFSPROC_STATFS     17

int nfs_prog(struct rpc_t* rpc);

#endif /* _NFS_H_ */
