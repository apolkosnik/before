/*
  Previous - esp.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_ESP_H
#define PREV_ESP_H

typedef struct {
    uint8_t control;
    uint8_t status;
} ESPDMASTATUS;

extern ESPDMASTATUS esp_dma;
extern uint32_t     esp_counter;

#define ESPCTRL_CLKMASK     0xc0    /* clock selection bits */
#define ESPCTRL_CLK10MHz    0x00
#define ESPCTRL_CLK12MHz    0x40
#define ESPCTRL_CLK16MHz    0xc0
#define ESPCTRL_CLK20MHz    0x80
#define ESPCTRL_ENABLE_INT  0x20    /* enable ESP interrupt */
#define ESPCTRL_MODE_DMA    0x10    /* select mode: 1 = dma, 0 = pio */
#define ESPCTRL_DMA_READ    0x08    /* select direction: 1 = scsi>mem, 0 = mem>scsi */
#define ESPCTRL_FLUSH       0x04    /* flush DMA buffer */
#define ESPCTRL_RESET       0x02    /* hard reset ESP */
#define ESPCTRL_CHIP_TYPE   0x01    /* select chip type: 1 = WD33C92, 0 = NCR53C90(A) */

#define ESPSTAT_STATE_MASK  0xc0
#define ESPSTAT_STATE_D0S0  0x00    /* DMA ready   for buffer 0, SCSI in buffer 0 */
#define ESPSTAT_STATE_D0S1  0x40    /* DMA request for buffer 0, SCSI in buffer 1 */
#define ESPSTAT_STATE_D1S1  0x80    /* DMA ready   for buffer 1, SCSI in buffer 1 */
#define ESPSTAT_STATE_D1S0  0xc0    /* DMA request for buffer 1, SCSI in buffer 0 */
#define ESPSTAT_OUTFIFO_MSK 0x38    /* output fifo byte (inverted) */
#define ESPSTAT_INFIFO_MSK  0x07    /* input fifo byte (inverted) */


extern void ESP_DMA_CTRL_Read(void);
extern void ESP_DMA_CTRL_Write(void);
extern void ESP_DMA_FIFO_STAT_Read(void);
extern void ESP_DMA_FIFO_STAT_Write(void);

extern void ESP_DMA_set_status(void);


extern void ESP_TransCountL_Read(void); 
extern void ESP_TransCountL_Write(void); 
extern void ESP_TransCountH_Read(void);
extern void ESP_TransCountH_Write(void); 
extern void ESP_FIFO_Read(void);
extern void ESP_FIFO_Write(void); 
extern void ESP_Command_Read(void); 
extern void ESP_Command_Write(void); 
extern void ESP_Status_Read(void);
extern void ESP_SelectBusID_Write(void); 
extern void ESP_IntStatus_Read(void);
extern void ESP_SelectTimeout_Write(void); 
extern void ESP_SeqStep_Read(void);
extern void ESP_SyncPeriod_Write(void); 
extern void ESP_FIFOflags_Read(void);
extern void ESP_SyncOffset_Write(void); 
extern void ESP_Configuration_Read(void); 
extern void ESP_Configuration_Write(void); 
extern void ESP_ClockConv_Write(void);
extern void ESP_Test_Write(void);

extern void ESP_Conf2_Read(void);
extern void ESP_Conf2_Write(void);
extern void ESP_Unknown_Read(void);
extern void ESP_Unknown_Write(void);

extern void esp_reset_hard(void);
extern void esp_reset_soft(void);
extern void esp_bus_reset(void);
extern void esp_flush_fifo(void);

extern void esp_message_accepted(void);
extern void esp_initiator_command_complete(void);
extern void esp_transfer_info(void);
extern void esp_transfer_pad(void);
extern void esp_select(bool atn);

extern void esp_raise_irq(void);
extern void esp_lower_irq(void);

extern bool esp_transfer_done(bool write);


extern void ESP_Interrupt_Handler(void);
extern void ESP_IO_Handler(void);

extern bool ESP_Send_Ready(void);
extern uint8_t ESP_Send_Data(void);
extern bool ESP_Receive_Ready(void);
extern void ESP_Receive_Data(uint8_t val);

extern void ESP_Reset(void);

#endif /* PREV_ESP_H */
