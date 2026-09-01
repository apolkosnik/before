/*
  Previous - dma.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef PREV_DMA_H
#define PREV_DMA_H

/* DMA Registers */
extern void DMA_CSR_Read(void);
extern void DMA_CSR_Write(void);

extern void DMA_Saved_Next_Read(void);
extern void DMA_Saved_Next_Write(void);
extern void DMA_Saved_Limit_Read(void);
extern void DMA_Saved_Limit_Write(void);
extern void DMA_Saved_Start_Read(void);
extern void DMA_Saved_Start_Write(void);
extern void DMA_Saved_Stop_Read(void);
extern void DMA_Saved_Stop_Write(void);

extern void DMA_Next_Read(void);
extern void DMA_Next_Write(void);
extern void DMA_Limit_Read(void);
extern void DMA_Limit_Write(void);
extern void DMA_Start_Read(void);
extern void DMA_Start_Write(void);
extern void DMA_Stop_Read(void);
extern void DMA_Stop_Write(void);

extern void DMA_Init_Read(void);
extern void DMA_Init_Write(void);

/* Turbo DMA functions */
extern void TDMA_CSR_Read(void);
extern void TDMA_CSR_Write(void);
extern void TDMA_Saved_Limit_Read(void);
extern void tdma_esp_flush_buffer(void);

/* Reset function */
extern void DMA_Reset(void);

/* Device functions */
extern void dma_esp_write_memory(void);
extern void dma_esp_read_memory(void);
extern void dma_esp_flush_buffer(void);

extern void dma_mo_write_memory(void);
extern void dma_mo_read_memory(void);

extern void dma_enet_write_memory(bool eop);
extern bool dma_enet_read_memory(void);

extern void dma_dsp_write_memory(uint8_t val);
extern uint8_t dma_dsp_read_memory(void);
extern bool dma_dsp_ready(void);

extern void dma_m2m(void);
extern void dma_m2m_write_memory(void);

extern uint8_t dma_scc_read_memory(void);
extern bool dma_scc_ready(void);

extern void dma_sndout_read_memory(void);
extern void dma_sndout_intr(void);
extern bool dma_sndin_write_memory(uint32_t val);
extern bool dma_sndin_intr(void);

extern void dma_printer_read_memory(void);

/* M2M DMA IO handler */
extern void DMA_M2M_IO_Handler(void);

/* Function for video interrupt */
extern void dma_video_interrupt(void);

#endif /* PREV_DMA_H */
