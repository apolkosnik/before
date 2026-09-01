/*
  Previous - enet_pcap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Send and receive Ethernet packets using the PCAP library.
*/
const char Enet_pcap_fileid[] = "Previous enet_pcap.c";

#include "main.h"

#if HAVE_PCAP
#include <pcap.h>

#include "configuration.h"
#include "log.h"
#include "ethernet.h"
#include "enet_pcap.h"
#include "queue.h"
#include "host.h"

#define LOG_EN_PCAP_LEVEL LOG_DEBUG

/****************/
/* --- PCAP --- */


/* PCAP prototypes */
pcap_t *pcap_handle;

/* queue prototypes */
queueADT pcapq;

int pcap_started;
static mutex_t *pcap_mutex = NULL;
thread_t *pcap_tick_func_handle;

/* This function is to be called periodically *
 * to keep the internal packet state flowing. */
static void pcap_tick(void)
{
    struct pcap_pkthdr h;
    const unsigned char *data;
    
    if (pcap_started) {
        host_mutex_lock(pcap_mutex);
        data = pcap_next(pcap_handle,&h);
        host_mutex_unlock(pcap_mutex);

        if (data && h.caplen > 0) {
            if (h.caplen > 1516)
                h.caplen = 1516;
            
            struct queuepacket *p;
            p=(struct queuepacket *)malloc(sizeof(struct queuepacket));
            host_mutex_lock(pcap_mutex);
            p->len=h.caplen;
            memcpy(p->data,data,h.caplen);
            QueueEnter(pcapq,p);
            host_mutex_unlock(pcap_mutex);
            Log_Printf(LOG_EN_PCAP_LEVEL, "[PCAP] Output packet with %i bytes to queue",h.caplen);
        }
    }
}

static int tick_func(void *arg)
{
    while (pcap_started)
    {
        host_sleep_us(1230);
        pcap_tick();
    }
    return 0;
}


void enet_pcap_queue_poll(void)
{
    if (pcap_started) {
        host_mutex_lock(pcap_mutex);
        if (QueuePeek(pcapq)>0)
        {
            struct queuepacket *qp;
            qp=QueueDelete(pcapq);
            Log_Printf(LOG_EN_PCAP_LEVEL, "[PCAP] Getting packet from queue");
            enet_receive(qp->data,qp->len);
            free(qp);
        }
        host_mutex_unlock(pcap_mutex);
    }
}

void enet_pcap_input(uint8_t *pkt, int pkt_len) {
    if (pcap_started) {
        Log_Printf(LOG_EN_PCAP_LEVEL, "[PCAP] Input packet with %i bytes",enet_tx_buffer.size);
        host_mutex_lock(pcap_mutex);
        if (pcap_sendpacket(pcap_handle, pkt, pkt_len) < 0) {
            Log_Printf(LOG_WARN, "[PCAP] Error: Couldn't transmit packet!");
        }
        host_mutex_unlock(pcap_mutex);
    }
}

void enet_pcap_stop(void) {
    if (pcap_started) {
        Log_Printf(LOG_WARN, "Stopping PCAP");
        pcap_started=0;
        host_thread_wait(pcap_tick_func_handle);
        host_mutex_destroy(pcap_mutex);
        while (QueuePeek(pcapq)>0) {
            Log_Printf(LOG_WARN, "Flushing PCAP queue");
            free(QueueDelete(pcapq));
        }
        QueueDestroy(pcapq);
        pcap_close(pcap_handle);
    }
}

void enet_pcap_uninit(void) {
    enet_pcap_stop();
}

void enet_pcap_start(uint8_t *mac) {
    char errbuf[PCAP_ERRBUF_SIZE];
    char *dev;
    struct bpf_program fp;
    char filter_exp[255];
    bpf_u_int32 net = 0xffffffff;

    if (!pcap_started) {
        Log_Printf(LOG_WARN, "Starting PCAP (%02x:%02x:%02x:%02x:%02x:%02x)",
                   mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        
#if 0
        dev = pcap_lookupdev(errbuf);
#else
        dev = ConfigureParams.Ethernet.szInterfaceName;
#endif
        if (dev == NULL) {
            Log_Printf(LOG_WARN, "[PCAP] Error: Couldn't find device: %s", errbuf);
            return;
        }
        Log_Printf(LOG_WARN, "Device: %s", dev);
        
        pcap_handle = pcap_open_live(dev, 1518, 1, 1, errbuf);
        
        if (pcap_handle == NULL) {
            Log_Printf(LOG_WARN, "[PCAP] Error: Couldn't open device %s: %s", dev, errbuf);
            return;
        }
        
        pcap_set_immediate_mode(pcap_handle, 1);
        
        if (pcap_getnonblock(pcap_handle, errbuf) == 0) {
            Log_Printf(LOG_WARN, "[PCAP] Setting interface to non-blocking mode.");
            if (pcap_setnonblock(pcap_handle, 1, errbuf) != 0) {
                Log_Printf(LOG_WARN, "[PCAP] Error: Couldn't set interface to non-blocking mode: %s", errbuf);
                return;
            }
        } else {
            Log_Printf(LOG_WARN, "[PCAP] Error: Unexpected error: %s", errbuf);
            return;
        }
        
#if 1 /* TODO: Check if we need to take care of RXMODE_ADDR_SIZE and RX_PROMISCUOUS/RX_ANY */
        snprintf(filter_exp, sizeof(filter_exp),
                 "(((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x) or (ether[0] & 0x01 = 0x01)))",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        if (pcap_compile(pcap_handle, &fp, filter_exp, 0, net) == -1) {
            Log_Printf(LOG_WARN, "[PCAP] Warning: Couldn't parse filter %s: %s", filter_exp, pcap_geterr(pcap_handle));
        } else {
            if (pcap_setfilter(pcap_handle, &fp) == -1) {
                Log_Printf(LOG_WARN, "[PCAP] Warning: Couldn't install filter %s: %s", filter_exp, pcap_geterr(pcap_handle));
            }
        }
#endif
        pcap_started=1;
        pcapq = QueueCreate();
        pcap_mutex=host_mutex_create();
        pcap_tick_func_handle=host_thread_create(tick_func,"PCAPTickThread", (void *)NULL);
    }
}
#endif
