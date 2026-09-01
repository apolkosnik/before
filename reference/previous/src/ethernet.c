/*
  Previous - ethernet.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains simulations of the Fujitsu MB8795 and the AT&T 7213
  Ethernet transceivers.
*/
const char Ethernet_fileid[] = "Previous ethernet.c";

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "configuration.h"
#include "sysReg.h"
#include "dma.h"
#include "bmap.h"
#include "ethernet.h"
#include "enet_slirp.h"
#include "enet_pcap.h"
#include "cycInt.h"
#include "statusbar.h"

#define LOG_EN_LEVEL        LOG_DEBUG
#define LOG_EN_REG_LEVEL    LOG_DEBUG


EthernetBuffer enet_tx_buffer;
EthernetBuffer enet_rx_buffer;

struct {
    uint8_t tx_status;
    uint8_t tx_mask;
    uint8_t tx_mode;
    uint8_t rx_status;
    uint8_t rx_mask;
    uint8_t rx_mode;
    uint8_t reset;
    
    uint8_t mac_addr[6];
} enet;

#define TXSTAT_READY        0x80    /* r */
#define TXSTAT_NET_BUSY     0x40    /* r */
#define TXSTAT_TX_RECVD     0x20    /* r */
#define TXSTAT_SHORTED      0x10    /* r */
#define TXSTAT_UNDERFLOW    0x08    /* rw */
#define TXSTAT_COLL         0x04    /* rw */
#define TXSTAT_16COLLS      0x02    /* rw */
#define TXSTAT_PAR_ERR      0x01    /* rw */

#define TXMASK_PKT_RDY      0x80
#define TXMASK_TX_RECVD     0x20
#define TXMASK_UNDERFLOW    0x08
#define TXMASK_COLL         0x04
#define TXMASK_16COLLS      0x02
#define TXMASK_PAR_ERR      0x01

#define RXSTAT_PKT_OK       0x80    /* rw */
#define RXSTAT_RESET_PKT    0x10    /* r */
#define RXSTAT_SHORT_PKT    0x08    /* rw */
#define RXSTAT_ALIGN_ERR    0x04    /* rw */
#define RXSTAT_CRC_ERR      0x02    /* rw */
#define RXSTAT_OVERFLOW     0x01    /* rw */

#define RXMASK_PKT_OK       0x80
#define RXMASK_RESET_PKT    0x10
#define RXMASK_SHORT_PKT    0x08
#define RXMASK_ALIGN_ERR    0x04
#define RXMASK_CRC_ERR      0x02
#define RXMASK_OVERFLOW     0x01

#define TXMODE_COLL_ATMPT   0xF0    /* r */
#define TXMODE_IGNORE_PAR   0x08    /* rw */
#define TXMODE_TM           0x04    /* rw */
#define TXMODE_DIS_LOOP     0x02    /* rw */
#define TXMODE_DIS_CONTNT   0x01    /* rw */

#define RXMODE_TEST_CRC     0x80
#define RXMODE_ADDR_SIZE    0x10
#define RXMODE_ENA_SHORT    0x08
#define RXMODE_ENA_RST      0x04
#define RXMODE_MATCH_MODE   0x03

#define EN_RESET            0x80    /* w */


void enet_reset(void);
bool new_enet_buserror(void);

static void (*enet_output)(void);
static void (*enet_input)(uint8_t *pkt, int len);
static void (*enet_start)(uint8_t *mac);
static void (*enet_stop)(void);
static void (*enet_uninit)(void);

void print_packet(uint8_t *pkt, int len, int out);


/* Interrupt functions */
static void enet_tx_check_interrupt(void) {
    if (enet.tx_status&enet.tx_mask) {
        set_interrupt(INT_EN_TX, SET_INT);
    } else {
        set_interrupt(INT_EN_TX, RELEASE_INT);
    }
}

static void enet_rx_check_interrupt(void) {
    if (enet.rx_status&enet.rx_mask) {
        set_interrupt(INT_EN_RX, SET_INT);
    } else {
        set_interrupt(INT_EN_RX, RELEASE_INT);
    }
}

static void enet_tx_interrupt(uint8_t intr) {
    enet.tx_status|=intr;
    enet_tx_check_interrupt();
}

static void enet_tx_release(uint8_t intr) {
    enet.tx_status&=~intr;
    enet_tx_check_interrupt();
}

static void enet_rx_interrupt(uint8_t intr) {
    enet.rx_status|=intr;
    enet_rx_check_interrupt();
}


/* Register access functions */
void EN_TX_Status_Read(void) { /* 0x02006000 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.tx_status);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_TX_Status_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter status write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    if (ConfigureParams.System.bTurbo) {
        enet.tx_status&=~(val&0xBE);
    } else {
        enet.tx_status&=~(val&0x0F);
    }
    enet_tx_check_interrupt();
}

void EN_TX_Mask_Read(void) { /* 0x02006001 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.tx_mask);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter masks read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_TX_Mask_Write(void) {
    enet.tx_mask = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter masks write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    enet.tx_mask&=ConfigureParams.System.bTurbo?0xBE:0xAF;
    enet_tx_check_interrupt();
}

void EN_RX_Status_Read(void) { /* 0x02006002 */
    if (new_enet_buserror()) {
        return;
    }
    IoMem_WriteByte(IoAccessCurrentAddress, enet.rx_status);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver status read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_Status_Write(void) {
    uint8_t val = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver status write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    if (ConfigureParams.System.bTurbo) {
        enet.rx_status&=~(val&0xDF);
    } else {
        enet.rx_status&=~(val&0x8F);
    }
    enet_rx_check_interrupt();
}

void EN_RX_Mask_Read(void) { /* 0x02006003 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.rx_mask);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver masks read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_Mask_Write(void) {
    enet.rx_mask = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver masks write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    
    enet.rx_mask&=ConfigureParams.System.bTurbo?0xDF:0x9F;
    enet_rx_check_interrupt();
}

void EN_TX_Mode_Read(void) { /* 0x02006004 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.tx_mode);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter mode read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_TX_Mode_Write(void) {
    enet.tx_mode = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Transmitter mode write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_Mode_Read(void) { /* 0x02006005 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.rx_mode);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver mode read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_Mode_Write(void) {
    enet.rx_mode = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Receiver mode write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_Reset_Write(void) { /* 0x02006006 */
    enet.reset = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Reset write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    enet_reset();
}

void EN_CounterLo_Read(void) { /* 0x02006007 */
    IoMem_WriteByte(IoAccessCurrentAddress, 0&0xFF); /* FIXME: bit counter value */
    Log_Printf(LOG_WARN,"[EN] TDR counter lo read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID0_Read(void) { /* 0x02006008 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[0]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 0 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID0_Write(void) {
    enet.mac_addr[0] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 0 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID1_Read(void) { /* 0x02006009 */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[1]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 1 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID1_Write(void) {
    enet.mac_addr[1] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 1 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID2_Read(void) { /* 0x0200600a */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[2]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 2 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID2_Write(void) {
    enet.mac_addr[2] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 2 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID3_Read(void) { /* 0x0200600b */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[3]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 3 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID3_Write(void) {
    enet.mac_addr[3] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 3 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID4_Read(void) { /* 0x0200600c */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[4]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 4 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID4_Write(void) {
    enet.mac_addr[4] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 4 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID5_Read(void) { /* 0x0200600d */
    IoMem_WriteByte(IoAccessCurrentAddress, enet.mac_addr[5]);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 5 read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_NodeID5_Write(void) {
    enet.mac_addr[5] = IoMem_ReadByte(IoAccessCurrentAddress);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] MAC byte 5 write at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
    /* Make sure the interface has the correct MAC */
    Ethernet_Reset(false);
}

void EN_CounterHi_Read(void) { /* 0x0200600f */
    IoMem_WriteByte(IoAccessCurrentAddress, (0>>8)&0x3F); /* FIXME: bit counter value */
    Log_Printf(LOG_WARN,"[EN] TDR counter hi read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}


/* Functions to find out if we are intended to receive a packet */

/* Non-turbo */
#define RX_NOPACKETS        0   /* Accept no packets */
#define RX_LIMITED          1   /* Accept broadcast/limited */
#define RX_NORMAL           2   /* Accept broadcast/multicast */
#define RX_PROMISCUOUS      3   /* Accept all packets */

/* Turbo */
#define RX_ENABLED      0x80    /* Accept packets */
#define RX_ANY          0x01    /* Accept any packets */
#define RX_OWN          0x02    /* Accept own packets */

/* Ethernet frame size limits */
#define ENET_FRAMESIZE_MIN  64      /* 46 byte data and 14 byte header, 4 byte CRC */
#define ENET_FRAMESIZE_MAX  1518    /* 1500 byte data and 14 byte header, 4 byte CRC */

/* Ethernet periodic check */
#define ENET_IO_DELAY   500     /* use 500 for NeXT hardware test, 20 for status test */
#define ENET_IO_SHORT   40      /* use 40 for 68030 hardware test */

/* Ethernet states */
enum {
    RECV_STATE_WAITING,
    RECV_STATE_RECEIVING
} receiver_state;

static bool tx_done;
static bool rx_chain;
static int old_size;
static int en_state;

#define EN_DISCONNECTED    0
#define EN_LOOPBACK        1
#define EN_THINWIRE        2
#define EN_TWISTEDPAIR     3

static bool recv_multicast(uint8_t *packet) {
    if (packet[0]&0x01)
        return true;
    else
        return false;
}

static bool recv_local_multicast(uint8_t *packet) {
    if (packet[0]&0x01 &&
        (packet[0]&0xFE) == enet.mac_addr[0] &&
        packet[1] == enet.mac_addr[1] &&
        packet[2] == enet.mac_addr[2])
        return true;
    else
        return false;
}

static bool recv_me(uint8_t *packet) {
    if (packet[0] == enet.mac_addr[0] &&
        packet[1] == enet.mac_addr[1] &&
        packet[2] == enet.mac_addr[2] &&
        packet[3] == enet.mac_addr[3] &&
        packet[4] == enet.mac_addr[4] &&
        (packet[5] == enet.mac_addr[5] || (enet.rx_mode&RXMODE_ADDR_SIZE)))
        return true;
    else
        return false;
}

static bool recv_me_turbo(uint8_t *packet) {
    if (packet[0] == enet.mac_addr[0] &&
        packet[1] == enet.mac_addr[1] &&
        packet[2] == enet.mac_addr[2] &&
        packet[3] == enet.mac_addr[3] &&
        packet[4] == enet.mac_addr[4] &&
        packet[5] == enet.mac_addr[5])
        return true;
    else
        return false;
}

static bool recv_broadcast(uint8_t *packet) {
    if (packet[0] == 0xFF &&
        packet[1] == 0xFF &&
        packet[2] == 0xFF &&
        packet[3] == 0xFF &&
        packet[4] == 0xFF &&
        packet[5] == 0xFF)
        return true;
    else
        return false;
}

static bool enet_packet_for_me(uint8_t *packet) {
    
    if (ConfigureParams.System.bTurbo) {
        if (enet.rx_mode&RX_ENABLED) {
            if (enet.rx_mode&RX_ANY) {
                return true;
            } else if (enet.rx_mode&RX_OWN) {
                if (recv_broadcast(packet) || recv_me_turbo(packet)) {
                    return true;
                }
            } else {
                if (recv_broadcast(packet)) {
                    return true;
                }
            }
        }
        return false;
    }
    
    switch (enet.rx_mode&RXMODE_MATCH_MODE) {
        case RX_NOPACKETS:
            return false;
        case RX_LIMITED:
            return recv_broadcast(packet) || recv_me(packet) || recv_local_multicast(packet);
        case RX_NORMAL:
            return recv_broadcast(packet) || recv_me(packet) || recv_multicast(packet);
        case RX_PROMISCUOUS:
            return true;
        default:
            return false;
    }
}

void enet_receive(uint8_t *pkt, int len) {
    if (enet_packet_for_me(pkt)) {
        print_packet(pkt, len, 0);
        memcpy(enet_rx_buffer.data,pkt,len);
        len += 4; /* Checksum */
        if (len < ENET_FRAMESIZE_MIN) { /* Hack for short packets from SLIRP */
            len = ENET_FRAMESIZE_MIN;
        }
        enet_rx_buffer.size=enet_rx_buffer.limit=len;
        enet.tx_status |= TXSTAT_NET_BUSY;
    } else if (en_state != EN_LOOPBACK && en_state != EN_THINWIRE) {
        Log_Printf(LOG_WARN, "[EN] Packet is not for me."); /* Log warning if it is not a self-sent packet */
    }
}

static void enet_send(uint8_t *pkt, int len) {
    print_packet(pkt, len, 1);
    if (en_state == EN_LOOPBACK) {
        /* Loop back */
        Log_Printf(LOG_WARN, "[EN] Loopback packet.");
        enet_receive(pkt, len);
    } else {
        /* Send to real world network */
        enet_input(pkt, len);
        /* Simultaneously receive packet on thin ethernet */
        if (en_state == EN_THINWIRE) {
            enet_receive(pkt, len);
        }
    }
}


/* Fujitsu ethernet controller */
static int enet_state(void) {
    if (ConfigureParams.System.nMachineType == NEXT_CUBE030) {
        if (enet.tx_mode&TXMODE_DIS_LOOP) {
            if (ConfigureParams.Ethernet.bEthernetConnected) {
                return EN_THINWIRE;
            }
        } else {
            return EN_LOOPBACK;
        }
    } else if (bmap_tpe_select) {
        if (ConfigureParams.Ethernet.bEthernetConnected) {
            if (ConfigureParams.Ethernet.bTwistedPair) {
                return EN_TWISTEDPAIR;
            }
        }
    } else {
        if (enet.tx_mode&TXMODE_DIS_LOOP) {
            if (ConfigureParams.Ethernet.bEthernetConnected) {
                if (!ConfigureParams.Ethernet.bTwistedPair) {
                    return EN_THINWIRE;
                }
            }
        } else {
            return EN_LOOPBACK;
        }
    }
    return EN_DISCONNECTED;
}

static void enet_io(void) {
    
    en_state = enet_state();
    
    /* Receive packet */
    switch (receiver_state) {
        case RECV_STATE_WAITING:
            if (enet_rx_buffer.size>0) {
                Statusbar_BlinkLed(DEVICE_LED_ENET);
                Log_Printf(LOG_EN_LEVEL, "[EN] Receiving packet from %02X:%02X:%02X:%02X:%02X:%02X",
                           enet_rx_buffer.data[6], enet_rx_buffer.data[7], enet_rx_buffer.data[8],
                           enet_rx_buffer.data[9], enet_rx_buffer.data[10], enet_rx_buffer.data[11]);
                rx_chain = false;
                if (enet_rx_buffer.size<ENET_FRAMESIZE_MIN && !(enet.rx_mode&RXMODE_ENA_SHORT)) {
                    Log_Printf(LOG_WARN, "[EN] Received packet is short (%i byte)",enet_rx_buffer.size);
                    enet_rx_interrupt(RXSTAT_SHORT_PKT);
                    enet_rx_buffer.size = 0;
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    break; /* Keep on waiting for a good packet */
                } else /* Fall through to receiving state */
                    receiver_state = RECV_STATE_RECEIVING;
            } else if (en_state == EN_THINWIRE || en_state == EN_TWISTEDPAIR) {
                /* Receive from real world network */
                enet_output();
                break;
            } else
                break;
        case RECV_STATE_RECEIVING:
            if (enet_rx_buffer.size>0) {
                old_size = enet_rx_buffer.size;
                dma_enet_write_memory(rx_chain);
                if (enet_rx_buffer.size==old_size) {
                    Log_Printf(LOG_WARN, "[EN] Receiving packet: Error! Receiver overflow (DMA disabled)!");
                    enet_rx_interrupt(RXSTAT_OVERFLOW);
                    rx_chain = false;
                    enet_rx_buffer.size = 0;
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    receiver_state = RECV_STATE_WAITING;
                    break; /* Go back to waiting state */
                }
                if (enet_rx_buffer.size>0) {
                    Log_Printf(LOG_WARN, "[EN] Receiving packet: Transfer not complete!");
                    rx_chain = true;
                    break; /* Loop in receiving state */
                } else { /* done */
                    Log_Printf(LOG_EN_LEVEL, "[EN] Receiving packet: Transfer complete.");
                    rx_chain = false;
                    enet_rx_interrupt(RXSTAT_PKT_OK);
                    if (en_state == EN_LOOPBACK) { /* same for thin wire loopback? */
                        enet_tx_interrupt(TXSTAT_TX_RECVD);
                    }
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    receiver_state = RECV_STATE_WAITING;
                }
            }
            break;
            
        default:
            break;
    }
    
    /* Send packet */
    if (enet.tx_status&TXSTAT_READY) {
        if (enet.tx_status&TXSTAT_NET_BUSY) {
            /* Wait until network is free */
            Log_Printf(LOG_EN_LEVEL, "[EN] Network is busy. Transmission delayed.");
        } else {
            old_size = enet_tx_buffer.size;
            tx_done=dma_enet_read_memory();
            if (enet_tx_buffer.size>0) {
                enet_tx_release(TXSTAT_TX_RECVD|TXSTAT_SHORTED);
                if (enet_tx_buffer.size==old_size && !tx_done) {
                    Log_Printf(LOG_WARN, "[EN] Sending packet: Error! Transmitter underflow (no EOP)!");
                    enet_tx_interrupt(TXSTAT_UNDERFLOW);
                    enet_tx_buffer.size=0;
                } else if (en_state == EN_DISCONNECTED) {
                    Log_Printf(LOG_EN_LEVEL, "[EN] Ethernet disconnected. 16 collisions in a row!");
                    enet_tx_interrupt(TXSTAT_16COLLS);
                    enet_tx_buffer.size=0;
                    tx_done = false;
                } else if (enet_tx_buffer.size>15) {
                    enet_tx_buffer.size-=15;
                } else if (tx_done) {
                    Log_Printf(LOG_WARN, "[EN] Transmitter error: Early EOP!");
                    enet_tx_buffer.size=0;
                    tx_done = false;
                }
            }
            if (tx_done) {
                Statusbar_BlinkLed(DEVICE_LED_ENET);
                Log_Printf(LOG_EN_LEVEL, "[EN] Sending packet to %02X:%02X:%02X:%02X:%02X:%02X",
                           enet_tx_buffer.data[0], enet_tx_buffer.data[1], enet_tx_buffer.data[2],
                           enet_tx_buffer.data[3], enet_tx_buffer.data[4], enet_tx_buffer.data[5]);
                enet_send(enet_tx_buffer.data, enet_tx_buffer.size);
                enet_tx_buffer.size=0;
            }
        }
    }
}

/* AT&T ethernet controller for turbo systems */
#define TXMODE_ENABLE   0x80
#define RXMODE_ENABLE   0x80
#define TXMODE_LOOP     0x02
#define TXMODE_TPE      0x04
#define ENCTRL_BADTPE   0x40

uint8_t saved_nibble = 0;

void EN_Control_Read(void) { /* 0x02006006 */
    uint8_t val = enet.reset&EN_RESET;
    if (enet.tx_mode&TXMODE_TPE) {
        if (!ConfigureParams.Ethernet.bEthernetConnected || !ConfigureParams.Ethernet.bTwistedPair) {
            val |= ENCTRL_BADTPE;
        }
    }
    IoMem_WriteByte(IoAccessCurrentAddress, val);
    Log_Printf(LOG_EN_REG_LEVEL,"[EN] Control read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_SavedNibble_Read(void) { /* 0x02006007 */
    IoMem_WriteByte(IoAccessCurrentAddress, saved_nibble&0x0F);
    Log_Printf(LOG_WARN,"[EN] Receiver saved nibble read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_TX_Seq_Read(void) { /* 0x0200600e */
    IoMem_WriteByte(IoAccessCurrentAddress, 0&0x3F);
    Log_Printf(LOG_WARN,"[EN] Transmitter sequence read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

void EN_RX_Seq_Read(void) { /* 0x0200600f */
    IoMem_WriteByte(IoAccessCurrentAddress, 0&0x7F);
    Log_Printf(LOG_WARN,"[EN] Receiver sequence read at $%08x val=$%02x PC=$%08x\n", IoAccessCurrentAddress, IoMem_ReadByte(IoAccessCurrentAddress), m68k_getpc());
}

bool new_enet_buserror(void) {
    if (ConfigureParams.System.bTurbo) {
        if (!(enet.tx_mode&(TXMODE_TPE|TXMODE_LOOP))) {
            if (!ConfigureParams.Ethernet.bEthernetConnected || ConfigureParams.Ethernet.bTwistedPair) {
                Log_Printf(LOG_WARN,"[EN] Receiver status read bus error!\n");
                M68000_BusError(IoAccessCurrentAddress, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
                return true;
            }
        }
    }
    return false;
}

static int new_enet_state(void) {
    if (enet.tx_mode&TXMODE_LOOP) {
        return EN_LOOPBACK;
    } else if (ConfigureParams.Ethernet.bEthernetConnected) {
        if (enet.tx_mode&TXMODE_TPE) {
            if (ConfigureParams.Ethernet.bTwistedPair) {
                return EN_TWISTEDPAIR;
            }
        } else {
            if (!ConfigureParams.Ethernet.bTwistedPair) {
                return EN_THINWIRE;
            }
        }
    }
    return EN_DISCONNECTED;
}

static void new_enet_io(void) {
    
    en_state = new_enet_state();
    
    /* Receive packet */
    switch (receiver_state) {
        case RECV_STATE_WAITING:
            if (enet_rx_buffer.size>0) {
                Statusbar_BlinkLed(DEVICE_LED_ENET);
                Log_Printf(LOG_EN_LEVEL, "[EN] Receiving packet from %02X:%02X:%02X:%02X:%02X:%02X",
                           enet_rx_buffer.data[6], enet_rx_buffer.data[7], enet_rx_buffer.data[8],
                           enet_rx_buffer.data[9], enet_rx_buffer.data[10], enet_rx_buffer.data[11]);
                if (enet_rx_buffer.size<ENET_FRAMESIZE_MIN && !(enet.rx_mode&RXMODE_ENA_SHORT)) {
                    Log_Printf(LOG_WARN, "[EN] Received packet is short (%i byte)",enet_rx_buffer.size);
                    enet_rx_interrupt(RXSTAT_SHORT_PKT);
                    enet_rx_buffer.size = 0;
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    break; /* Keep on waiting for a good packet */
                } else /* Fall through to receiving state */
                    receiver_state = RECV_STATE_RECEIVING;
            } else if (en_state == EN_THINWIRE || en_state == EN_TWISTEDPAIR) {
                /* Receive from real world network */
                enet_output();
                break;
            } else
                break;
        case RECV_STATE_RECEIVING:
            if (enet_rx_buffer.size>0) {
                old_size = enet_rx_buffer.size;
                dma_enet_write_memory(false);
                if (enet_rx_buffer.size==old_size) {
                    Log_Printf(LOG_WARN, "[EN] Receiving packet: Error! Receiver overflow (DMA disabled)!");
                    enet_rx_interrupt(RXSTAT_OVERFLOW);
                    enet.rx_mode &= ~RXMODE_ENABLE;
                    enet_rx_buffer.size = 0;
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    receiver_state = RECV_STATE_WAITING;
                    break; /* Go back to waiting state */
                }
                if (enet_rx_buffer.size>0) {
                    Log_Printf(LOG_WARN, "[EN] Receiving packet: Transfer not complete!");
                    break; /* Loop in receiving state */
                } else { /* done */
                    Log_Printf(LOG_EN_LEVEL, "[EN] Receiving packet: Transfer complete.");
                    enet_rx_interrupt(RXSTAT_PKT_OK);
                    if (en_state == EN_LOOPBACK) {
                        enet_tx_interrupt(TXSTAT_TX_RECVD);
                    }
                    enet.tx_status &= ~TXSTAT_NET_BUSY;
                    receiver_state = RECV_STATE_WAITING;
                }
            }
            break;
            
        default:
            break;
    }
    
    /* Send packet */
    if (enet.tx_mode&TXMODE_ENABLE) {
        if (enet.tx_status&TXSTAT_NET_BUSY) {
            /* Wait until network is free */
            Log_Printf(LOG_EN_LEVEL, "[EN] Network is busy. Transmission delayed.");
        } else {
            dma_enet_read_memory();
            if (enet_tx_buffer.size>0) {
                if (en_state == EN_DISCONNECTED) {
                    Log_Printf(LOG_EN_LEVEL, "[EN] Ethernet disconnected. 16 collisions in a row!");
                    enet_tx_interrupt(TXSTAT_16COLLS);
                } else {
                    Statusbar_BlinkLed(DEVICE_LED_ENET);
                    Log_Printf(LOG_EN_LEVEL, "[EN] Sending packet to %02X:%02X:%02X:%02X:%02X:%02X",
                               enet_tx_buffer.data[0], enet_tx_buffer.data[1], enet_tx_buffer.data[2],
                               enet_tx_buffer.data[3], enet_tx_buffer.data[4], enet_tx_buffer.data[5]);
                    enet_send(enet_tx_buffer.data, enet_tx_buffer.size);
                }
                enet_tx_buffer.size=0;
                enet_tx_interrupt(TXSTAT_READY);
            }
        }
    }
}

void Ethernet_IO_Handler(void) {
    if (enet.reset&EN_RESET) {
        Log_Printf(LOG_WARN, "Stopping Ethernet Transmitter/Receiver");
        /* Stop SLIRP/PCAP */
        if (ConfigureParams.Ethernet.bEthernetConnected) {
            enet_stop();
        }
        return;
    }
    
    if (ConfigureParams.System.bTurbo) {
        new_enet_io();
    } else {
        enet_io();
    }
    
    CycInt_UpdateTimeEvent(receiver_state==RECV_STATE_WAITING?ENET_IO_DELAY:ENET_IO_SHORT, 0, EVENT_ETHERNET_IO);
}

void enet_reset(void) {
    if (enet.reset&EN_RESET) {
        enet.tx_status=ConfigureParams.System.bTurbo?0:TXSTAT_READY;
    } else {
        /* Start SLIRP/PCAP */
        if (ConfigureParams.Ethernet.bEthernetConnected) {
            enet_start(enet.mac_addr);
        }
        if (!CycInt_EventPending(EVENT_ETHERNET_IO)) {
            Log_Printf(LOG_WARN, "Starting Ethernet Transmitter/Receiver");
            CycInt_AddTimeEvent(ENET_IO_DELAY, 0, EVENT_ETHERNET_IO);
        }
    }
}

void Ethernet_Reset(bool hard) {
    static int init_done = 0;
    
    if (hard) {
        enet.reset=EN_RESET;
        enet_rx_buffer.size=enet_tx_buffer.size=0;
        enet_rx_buffer.limit=enet_tx_buffer.limit=EN_BUF_MAX;
        enet.tx_status=ConfigureParams.System.bTurbo?0:TXSTAT_READY;
        CycInt_RemovePendingEvent(EVENT_ETHERNET_IO);
    }
    
    if (init_done) {
        /* Stop SLIRP/PCAP */
        enet_stop();
        if (hard) {
            enet_uninit();
        }
    }
#if HAVE_PCAP
    if (ConfigureParams.Ethernet.nHostInterface == ENET_PCAP) {
        enet_output = enet_pcap_queue_poll;
        enet_input  = enet_pcap_input;
        enet_start  = enet_pcap_start;
        enet_stop   = enet_pcap_stop;
        enet_uninit = enet_pcap_uninit;
    } else
#endif
    {
        enet_output = enet_slirp_queue_poll;
        enet_input  = enet_slirp_input;
        enet_start  = enet_slirp_start;
        enet_stop   = enet_slirp_stop;
        enet_uninit = enet_slirp_uninit;
    }
    init_done = 1;
    
    enet_reset();
}

void Ethernet_UnInit(void) {
    if (enet_uninit) {
        enet_uninit();
    }
}

/* Packet printer and analyzer */

#define LOG_EN_DATA    0
#define LOG_EN_ANALYZE 0
#define LOG_EN_FILE    ""

void print_packet(uint8_t *buf, int size, int out) {
#if LOG_EN_DATA
    int i, offset = 0;
    
    if (LOG_EN_FILE[0]) {
        static FILE* EnLogFile = NULL;
        if (!(EnLogFile))
            EnLogFile = fopen(LOG_EN_FILE, "w");
        if (EnLogFile) {
            fprintf(EnLogFile, "\n\n000000 ");
            for (i = 0; i < size; i++)
                fprintf(EnLogFile, "%02x ", buf[i]);
            fflush(EnLogFile);
        }
        return;
    }
    
    if (out) {
        printf("<<        Outgoing packet (%d byte)        >>\n", size);
    } else {
        printf(">>        Incoming packet (%d byte)        <<\n", size);
    }
#if LOG_EN_ANALYZE
    uint8_t protocol, ihl, options = 0;
    uint16_t type, length, fragment, padding = 0;
    
    printf("Layer 2 Ethernet frame:\n");
    printf("MAC dst:   %02x:%02x:%02x:%02x:%02x:%02x\n", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
    printf("MAC src:   %02x:%02x:%02x:%02x:%02x:%02x\n", buf[6],buf[7],buf[8],buf[9],buf[10],buf[11]);
    type = (buf[12]<<8) | buf[13];
    offset = 14;
    if (type == 0x8100 || type == 0x88a8) {
        printf("TPID:      %04x\n",type);
        printf("TCI:       %04x\n",(buf[14]<<8) | buf[15]);
        printf("EtherType: %04x\n",(buf[16]<<8) | buf[17]);
        printf("IEEE 802.1Q and 802.1ad tag not supported!\n");
        offset += 4;
        goto print_data;
    }
    if (type < 0x0600) {
        printf("Length:    %04x\n",type);
        printf("IEEE 802.3 length not supported!\n");
        goto print_data;
    }
    printf("EtherType: %04x\n",type);
    offset = 14;
    
    switch (type) {
        case 0x0800:
            printf("Internet Protocol version 4 (IPv4):\n");
            printf("Version:   %d\n",buf[14]>>4);
            ihl = buf[14]&0xF;
            printf("IHL:       %d (%d byte)\n",ihl,ihl<<2);
            printf("DSCP:      %d\n",buf[15]>>2);
            printf("ECN:       %d\n", buf[15]&0x3);
            length = (buf[16]<<8) | buf[17];
            printf("Length:    %d\n", length);
            printf("ID:        %04x\n", (buf[18]<<8) | buf[19]);
            printf("Flags:     %d\n", buf[20]>>5);
            fragment = ((buf[20]&0x1F)<<8) | buf[21];
            printf("Offset:    %d\n", fragment);
            printf("TTL:       %d\n", buf[22]);
            protocol = buf[23];
            printf("Protocol:  %02x\n", protocol);
            printf("Checksum:  %04x\n", (buf[24]<<8) | buf[25]);
            printf("IP src:    %d.%d.%d.%d\n",buf[26],buf[27],buf[28],buf[29]);
            printf("IP dst:    %d.%d.%d.%d\n",buf[30],buf[31],buf[32],buf[33]);
            offset = 34;
            if (ihl > 5) {
                printf("Options not supported!\n");
                goto print_data;
            }
            if (fragment != 0) {
                printf("Fragment data at offset %d (%d byte):\n", fragment, fragment << 3);
                goto print_data;
            }
            
            switch (protocol) {
                case 0x01:
                    printf("Internet Control Message Protocol (ICMP):\n");
                    printf("Type:      %d\n", buf[34]);
                    printf("Code:      %d\n", buf[35]);
                    printf("Checksum:  %04x\n",(buf[36]<<8) | buf[37]);
                    printf("Rest:      %02x %02x %02x %02x\n",buf[38],buf[39],buf[40],buf[41]);
                    offset = 42;
                    break;
                case 0x06:
                    printf("Transmission Control Protocol (TCP):\n");
                    printf("Port src:  %d\n",(buf[34]<<8) | buf[35]);
                    printf("Port dst:  %d\n",(buf[36]<<8) | buf[37]);
                    printf("Seq num:   %u\n",(buf[38]<<24) | (buf[39]<<16) | (buf[40]<<8) | buf[41]);
                    printf("Ack num:   %u\n",(buf[42]<<24) | (buf[43]<<16) | (buf[44]<<8) | buf[45]);
                    options = buf[46]>>4;
                    printf("Offset:    %d (%d byte)\n", options, options << 3);
                    printf("Reserved:  %02x\n",(buf[46]>>1)&0x07);
                    printf("Flags:     ");
                    if (buf[46]&0x01) printf("NS ");
                    if (buf[47]&0x80) printf("CWR ");
                    if (buf[47]&0x40) printf("ECE ");
                    if (buf[47]&0x20) printf("URG ");
                    if (buf[47]&0x10) printf("ACK ");
                    if (buf[47]&0x08) printf("PSH ");
                    if (buf[47]&0x04) printf("RST ");
                    if (buf[47]&0x02) printf("SYN ");
                    if (buf[47]&0x01) printf("FIN ");
                    printf("\n");
                    printf("Window:    %d\n",(buf[48]<<8) | buf[49]);
                    printf("Checksum:  %04x\n",(buf[50]<<8) | buf[51]);
                    printf("Urg ptr:   %04x\n",(buf[52]<<8) | buf[53]);
                    offset = 54;
                    if (options > 5) {
                        options -= 5;
                        options <<= 2;
                        printf("Options:   %d byte", options);
                        for (i=0; i<options; i++) {
                            if (i % 4 == 0) {
                                printf("\n");
                            }
                            printf("%02X ",buf[offset + i]);
                        }
                        printf("\n");
                        offset += options;
                    }
                    break;
                case 0x11:
                    printf("User Datagram Protocol (UDP):\n");
                    printf("Port src:  %d\n",(buf[34]<<8) | buf[35]);
                    printf("Port dst:  %d\n",(buf[36]<<8) | buf[37]);
                    printf("Length:    %d\n",(buf[38]<<8) | buf[39]);
                    printf("Checksum:  %04x\n",(buf[40]<<8) | buf[41]);
                    offset = 42;
                    break;
                    
                default:
                    printf("IP Protocol %02x not supported!\n",protocol);
                    goto print_data;
            }
            break;
        case 0x0806:
            printf("Address Resolution Protocol (ARP):\n");
            printf("HTYPE:     %04x\n",(buf[14]<<8) | buf[15]);
            printf("PTYPE:     %04x\n",(buf[16]<<8) | buf[17]);
            printf("HLEN:      %d\n", buf[18]);
            printf("PLEN:      %d\n", buf[19]);
            printf("OPER:      %04x\n",(buf[20]<<8) | buf[21]);
            if (buf[18] != 6 || buf[19] != 4) {
                printf("HLEN or PLEN not supported!\n");
                offset = 22;
                goto print_data;
            }
            length = 8 + 2 * (6 + 4);
            printf("SHA:       %02x:%02x:%02x:%02x:%02x:%02x\n",buf[22],buf[23],buf[24],buf[25],buf[26],buf[27]);
            printf("SPA:       %d.%d.%d.%d\n",buf[28],buf[29],buf[30],buf[31]);
            printf("THA:       %02x:%02x:%02x:%02x:%02x:%02x\n",buf[32],buf[33],buf[34],buf[35],buf[36],buf[37]);
            printf("TPA:       %d.%d.%d.%d\n",buf[38],buf[39],buf[40],buf[41]);
            offset = 42;
            break;
        default:
            printf("EtherType %04x not supported!\n",type);
            goto print_data;
    }
    
    if (size > (length + 14)) {
        padding = size - (length + 14);
        size = length + 14;
    }
    
print_data:
    if (offset < size) {
        printf("Data:      offset = %d, length = %d:\n", offset, size - offset);
    } else {
        printf("Data:      no additional data");
    }
#endif /* LOG_EN_ANALYZE */
    for (i=offset; i<size; i++) {
        if (i > offset && (i - offset) % 16 == 0) {
            printf("\n");
        }
        printf("%02X ",buf[i]);
    }
    printf("\n");
#if LOG_EN_ANALYZE
    if (padding) {
        printf("Padding:   %d byte at end of packet\n", padding);
    }
#endif /* LOG_EN_ANALYZE */
#endif /* LOG_EN_DATA */
}
