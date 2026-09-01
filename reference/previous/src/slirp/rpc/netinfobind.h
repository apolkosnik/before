/* NetInfo Bind Program */

#ifndef _NIBIND_H_
#define _NIBIND_H_

#include "netinfo.h"

#define NIBINDPROG               200100001
#define NIBINDVERS               1

#define NIBINDPROC_NULL          0
#define NIBINDPROC_REGISTER      1
#define NIBINDPROC_UNREGISTER    2
#define NIBINDPROC_GETREGISTER   3
#define NIBINDPROC_LISTREG       4
#define NIBINDPROC_CREATEMASTER  5
#define NIBINDPROC_CREATECLONE   6
#define NIBINDPROC_DESTROYDOMAIN 7
#define NIBINDPROC_BIND          8

extern struct nidb_t* nidb;

int nibind_prog(struct rpc_t* rpc);

void nibind_init(struct rpc_t* rpc);
void nibind_uninit(struct rpc_t* rpc);

#endif /* _NIBIND_H_ */
