/* Mount Program */

#ifndef _MOUNT_H_
#define _MOUNT_H_

#define MOUNTPROG           100005
#define MOUNTVERS           1

#define MOUNTVERS_ORIG      1

#define MOUNTPROC_NULL      0
#define MOUNTPROC_MNT       1
#define MOUNTPROC_DUMP      2
#define MOUNTPROC_UMNT      3
#define MOUNTPROC_UMNTALL   4
#define MOUNTPROC_EXPORT    5
#define MOUNTPROC_EXPORTALL 6

int mount_prog(struct rpc_t* rpc);

void mount_uninit(struct rpc_t* rpc);

#endif /* _MOUNT_H_ */
