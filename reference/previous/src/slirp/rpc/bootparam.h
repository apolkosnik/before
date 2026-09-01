/* Bootparam Program */

#ifndef _BOOTPARAM_H_
#define _BOOTPARAM_H_

#define BOOTPARAMPROG         100026
#define BOOTPARAMVERS         1

#define BOOTPARAMPROC_NULL    0
#define BOOTPARAMPROC_WHOAMI  1
#define BOOTPARAMPROC_GETFILE 2

int bootparam_prog(struct rpc_t* rpc);

#endif /* _BOOTPARAM_H_ */
