/*
  Previous - enet_slirp.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Send and receive Ethernet packets using the SLiRP library.
*/
const char Enet_slirp_fileid[] = "Previous enet_slirp.c";

#include "main.h"
#include "log.h"
#include "ethernet.h"
#include "enet_slirp.h"
#include "queue.h"
#include "timing.h"
#include "host.h"
#include "libslirp.h"
#include "rpc/rpc.h"

#define LOG_EN_SLIRP_LEVEL LOG_DEBUG

/****************/
/* -- SLIRP -- */

/* queue prototypes */
queueADT slirpq;

int slirp_inited;
int slirp_started;
static mutex_t *slirp_mutex = NULL;
thread_t *tick_func_handle;

/* This function returns 1 if SLiRP is running and 0 if not. */
int slirp_can_output(void)
{
    return slirp_started;
}

/* This is a callback function for SLiRP that sends a packet *
 * to the calling library by putting it in a queue.          */
void slirp_output (const unsigned char *pkt, int pkt_len)
{
    struct queuepacket *p;
    p=(struct queuepacket *)malloc(sizeof(struct queuepacket));
    host_mutex_lock(slirp_mutex);
    p->len=pkt_len;
    memcpy(p->data,pkt,pkt_len);
    QueueEnter(slirpq,p);
    host_mutex_unlock(slirp_mutex);
    Log_Printf(LOG_EN_SLIRP_LEVEL, "[SLIRP] Output packet with %i bytes to queue",pkt_len);
}

/* This function is to be called periodically *
 * to keep the internal packet state flowing. */
static void slirp_tick(void)
{
    int ret2,nfds;
    struct timeval tv;
    fd_set rfds, wfds, xfds;
    int timeout;
    nfds=-1;
    
    if (slirp_started)
    {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&xfds);
        host_mutex_lock(slirp_mutex);
        timeout=slirp_select_fill(&nfds,&rfds,&wfds,&xfds); /* this can crash */
        host_mutex_unlock(slirp_mutex);
        
        if(timeout<0)
            timeout=500;
        tv.tv_sec=0;
        tv.tv_usec = timeout;    /* basilisk default 10000 */
        
        ret2 = select(nfds + 1, &rfds, &wfds, &xfds, &tv);
        if(ret2>=0){
            host_mutex_lock(slirp_mutex);
            slirp_select_poll(&rfds, &wfds, &xfds);
            host_mutex_unlock(slirp_mutex);
        }
    }
}

/* This function is to be called every 30 seconds *
 * to broadcast a simple routing table.           */
static void slirp_rip_tick(void)
{
    if (slirp_started)
    {
        Log_Printf(LOG_EN_SLIRP_LEVEL, "[SLIRP] Routing table broadcast");
        host_mutex_lock(slirp_mutex);
        slirp_rip_broadcast();
        host_mutex_unlock(slirp_mutex);
    }
}


#define SLIRP_TICK_US   1230
#define SLIRP_RIP_SEC   30

static int tick_func(void *arg)
{
    uint64_t time = Timing_GetSaveTime();
    uint64_t last_time = 0;
    uint64_t next_time = time + SLIRP_RIP_SEC;

    while (slirp_started)
    {
        host_sleep_us(SLIRP_TICK_US);
        slirp_tick();
        
        /* for routing information protocol */
        last_time = time;
        time = Timing_GetSaveTime();
        if (time < last_time) /* if time counter wrapped */
        {
            next_time = time; /* reset next_time */
        }
        if (time >= next_time)
        {
            slirp_rip_tick();
            next_time += SLIRP_RIP_SEC;
        }
    }
    return 0;
}


void enet_slirp_queue_poll(void)
{
    host_mutex_lock(slirp_mutex);
    if (QueuePeek(slirpq)>0)
    {
        struct queuepacket *qp;
        qp=QueueDelete(slirpq);
        Log_Printf(LOG_EN_SLIRP_LEVEL, "[SLIRP] Getting packet from queue");
        enet_receive(qp->data,qp->len);
        free(qp);
    }
    host_mutex_unlock(slirp_mutex);
}

void enet_slirp_input(uint8_t *pkt, int pkt_len) {
    if (slirp_started) {
        Log_Printf(LOG_EN_SLIRP_LEVEL, "[SLIRP] Input packet with %i bytes",enet_tx_buffer.size);
        host_mutex_lock(slirp_mutex);
        slirp_input(pkt,pkt_len);
        host_mutex_unlock(slirp_mutex);
    }
}

void enet_slirp_stop(void) {
    if (slirp_started) {
        Log_Printf(LOG_WARN, "Stopping SLIRP");
        slirp_started=0;
        host_thread_wait(tick_func_handle);
        host_mutex_destroy(slirp_mutex);
        while (QueuePeek(slirpq)>0) {
            Log_Printf(LOG_WARN, "Flushing SLIRP queue");
            free(QueueDelete(slirpq));
        }
        QueueDestroy(slirpq);
    }
}

void enet_slirp_uninit(void) {
    enet_slirp_stop();
    rpc_uninit();
}

void enet_slirp_start(uint8_t *mac) {
    struct in_addr guest_addr;
    
    if (!slirp_inited) {
        Log_Printf(LOG_WARN, "Initializing SLIRP");
        slirp_inited=1;
        slirp_init(&guest_addr);
        slirp_redir(0, 42320, guest_addr, 20); /* ftp data */
        slirp_redir(0, 42321, guest_addr, 21); /* ftp control */
        slirp_redir(0, 42322, guest_addr, 22); /* ssh */
        slirp_redir(0, 42323, guest_addr, 23); /* telnet */
        slirp_redir(0, 42380, guest_addr, 80); /* http */
    }
    if (slirp_inited && !slirp_started) {
        Log_Printf(LOG_WARN, "Starting SLIRP (%02x:%02x:%02x:%02x:%02x:%02x)",
                   mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        memcpy(client_ethaddr, mac, 6);
        slirp_started=1;
        slirpq = QueueCreate();
        slirp_mutex=host_mutex_create();
        tick_func_handle=host_thread_create(tick_func,"SLiRPTickThread", (void *)NULL);
    }
    
    /* (re)start local nfs deamon */
    rpc_reset();
}
