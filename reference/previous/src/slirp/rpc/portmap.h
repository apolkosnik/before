/* Portmap Program */

#ifndef _PORTMAP_H_
#define _PORTMAP_H_

#define PORTMAPPORT         111
#define PORTMAPPROG         100000
#define PORTMAPVERS         2

#define PORTMAPVERS_PROTO   2
#define PORTMAPVERS_ORIG    1

#define PORTMAPPROC_NULL    0
#define PORTMAPPROC_SET     1
#define PORTMAPPROC_UNSET   2
#define PORTMAPPROC_GETPORT 3
#define PORTMAPPROC_DUMP    4
#define PORTMAPPROC_CALLIT  5

int portmap_prog(struct rpc_t* rpc);

#endif /* _PORTMAP_H_ */
