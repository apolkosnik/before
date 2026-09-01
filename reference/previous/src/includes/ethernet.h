/*
  Previous - ethernet.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_ETHERNET_H
#define PREV_ETHERNET_H

extern void EN_TX_Status_Read(void);
extern void EN_TX_Status_Write(void);
extern void EN_TX_Mask_Read(void);
extern void EN_TX_Mask_Write(void);
extern void EN_RX_Status_Read(void);
extern void EN_RX_Status_Write(void);
extern void EN_RX_Mask_Read(void);
extern void EN_RX_Mask_Write(void);
extern void EN_TX_Mode_Read(void);
extern void EN_TX_Mode_Write(void);
extern void EN_RX_Mode_Read(void);
extern void EN_RX_Mode_Write(void);
extern void EN_Reset_Write(void);
extern void EN_NodeID0_Read(void);
extern void EN_NodeID0_Write(void);
extern void EN_NodeID1_Read(void);
extern void EN_NodeID1_Write(void);
extern void EN_NodeID2_Read(void);
extern void EN_NodeID2_Write(void);
extern void EN_NodeID3_Read(void);
extern void EN_NodeID3_Write(void);
extern void EN_NodeID4_Read(void);
extern void EN_NodeID4_Write(void);
extern void EN_NodeID5_Read(void);
extern void EN_NodeID5_Write(void);
extern void EN_CounterLo_Read(void);
extern void EN_CounterHi_Read(void);

extern uint8_t saved_nibble;

#define EN_BUF_MAX (64*1024)

typedef struct {
    uint8_t data[EN_BUF_MAX];
    int size;
    int limit;
} EthernetBuffer;

extern EthernetBuffer enet_tx_buffer;
extern EthernetBuffer enet_rx_buffer;

extern void Ethernet_IO_Handler(void);
extern void Ethernet_Reset(bool hard);
extern void Ethernet_UnInit(void);
extern void enet_receive(uint8_t *pkt, int len);

/* Turbo ethernet controller */
extern void EN_Control_Read(void);
extern void EN_RX_SavedNibble_Read(void);
extern void EN_TX_Seq_Read(void);
extern void EN_RX_Seq_Read(void);

#endif /* PREV_ETHERNET_H */
