/*
  Previous - ioMemTabTurbo.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Table with hardware I/O handlers for Turbo machines.
*/
const char IoMemTabTurbo_fileid[] = "Previous ioMemTabTurbo.c";

#include "main.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "video.h"
#include "configuration.h"
#include "m68000.h"
#include "esp.h"
#include "ethernet.h"
#include "sysReg.h"
#include "dma.h"
#include "scc.h"
#include "mo.h"
#include "kms.h"
#include "printer.h"
#include "ramdac.h"
#include "floppy.h"
#include "dsp.h"



/*-----------------------------------------------------------------------*/
/*
 List of functions to handle read/write hardware interceptions.
 */
const INTERCEPT_ACCESS_FUNC IoMemTable_Turbo[] =
{
	/* DMA Controller (Motorola) (writes MUST be 32-bit) */
	{ 0x02000010, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x02000040, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x02000080, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x02000090, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x020000d0, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x02000110, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	{ 0x02000150, 0x0001efff, SIZE_LONG, TDMA_CSR_Read, TDMA_CSR_Write },
	
	/* Channel SCSI */
	{ 0x02004010, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004014, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004018, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200401c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Channel Sound out */
	{ 0x02004040, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004044, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004048, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200404c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Ethernet Saved Limit */
	{ 0x02004050, 0x0001efff, SIZE_LONG, TDMA_Saved_Limit_Read, IoMem_WriteWithoutInterceptionButTrace },

	/* Channel Sound in */
	{ 0x02004080, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004084, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004088, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200408c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Channel Printer */
	{ 0x02004090, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004094, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004098, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200409c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Channel DSP */
	{ 0x020040d0, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x020040d4, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x020040d8, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x020040dc, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Channel Ethernet Transmit */
	{ 0x02004100, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004104, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004108, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0200410c, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004110, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004114, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004118, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200411c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* Channel Ethernet Receive */
	{ 0x02004140, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004144, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004148, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0200414c, 0x0001efff, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02004150, 0x0001efff, SIZE_LONG, DMA_Next_Read, DMA_Next_Write },
	{ 0x02004154, 0x0001efff, SIZE_LONG, DMA_Limit_Read, DMA_Limit_Write },
	{ 0x02004158, 0x0001efff, SIZE_LONG, DMA_Start_Read, DMA_Start_Write },
	{ 0x0200415c, 0x0001efff, SIZE_LONG, DMA_Stop_Read, DMA_Stop_Write },
	
	/* DMA Init */
	{ 0x02004210, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x02004240, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x02004280, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x02004290, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x020042d0, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x02004310, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	{ 0x02004350, 0x0001efff, SIZE_LONG, DMA_Init_Read, DMA_Init_Write },
	
	/* Network Adapter (AT&T 7213) */
	{ 0x02006000, 0x0001f00f, SIZE_BYTE, EN_TX_Status_Read, EN_TX_Status_Write },
	{ 0x02006001, 0x0001f00f, SIZE_BYTE, EN_TX_Mask_Read, EN_TX_Mask_Write },
	{ 0x02006002, 0x0001f00f, SIZE_BYTE, EN_RX_Status_Read, EN_RX_Status_Write },
	{ 0x02006003, 0x0001f00f, SIZE_BYTE, EN_RX_Mask_Read, EN_RX_Mask_Write },
	{ 0x02006004, 0x0001f00f, SIZE_BYTE, EN_TX_Mode_Read, EN_TX_Mode_Write },
	{ 0x02006005, 0x0001f00f, SIZE_BYTE, EN_RX_Mode_Read, EN_RX_Mode_Write },
	{ 0x02006006, 0x0001f00f, SIZE_BYTE, EN_Control_Read, EN_Reset_Write },
	{ 0x02006007, 0x0001f00f, SIZE_BYTE, EN_RX_SavedNibble_Read, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02006008, 0x0001f00f, SIZE_BYTE, EN_NodeID0_Read, EN_NodeID0_Write },
	{ 0x02006009, 0x0001f00f, SIZE_BYTE, EN_NodeID1_Read, EN_NodeID1_Write },
	{ 0x0200600a, 0x0001f00f, SIZE_BYTE, EN_NodeID2_Read, EN_NodeID2_Write },
	{ 0x0200600b, 0x0001f00f, SIZE_BYTE, EN_NodeID3_Read, EN_NodeID3_Write },
	{ 0x0200600c, 0x0001f00f, SIZE_BYTE, EN_NodeID4_Read, EN_NodeID4_Write },
	{ 0x0200600d, 0x0001f00f, SIZE_BYTE, EN_NodeID5_Read, EN_NodeID5_Write },
	{ 0x0200600e, 0x0001f00f, SIZE_BYTE, EN_TX_Seq_Read, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0200600f, 0x0001f00f, SIZE_BYTE, EN_RX_Seq_Read, IoMem_WriteWithoutInterceptionButTrace },
	
	/* Interrupt Status and Mask Registers */
	{ 0x02007000, 0x0001f803, SIZE_LONG, IntRegStatRead, IntRegStatWrite },
	{ 0x02007800, 0x0001f803, SIZE_LONG, IntRegMaskRead, IntRegMaskWrite },
	
	/* DSP (Motorola XSP56001) */
	{ 0x02008000, 0x0001e007, SIZE_BYTE, DSP_ICR_Read, DSP_ICR_Write },
	{ 0x02008001, 0x0001e007, SIZE_BYTE, DSP_CVR_Read, DSP_CVR_Write },
	{ 0x02008002, 0x0001e007, SIZE_BYTE, DSP_ISR_Read, DSP_ISR_Write },
	{ 0x02008003, 0x0001e007, SIZE_BYTE, DSP_IVR_Read, DSP_IVR_Write },
	{ 0x02008004, 0x0001e007, SIZE_BYTE, DSP_Data0_Read, DSP_Data0_Write },
	{ 0x02008005, 0x0001e007, SIZE_BYTE, DSP_Data1_Read, DSP_Data1_Write },
	{ 0x02008006, 0x0001e007, SIZE_BYTE, DSP_Data2_Read, DSP_Data2_Write },
	{ 0x02008007, 0x0001e007, SIZE_BYTE, DSP_Data3_Read, DSP_Data3_Write },
	
	/* System Control Register 1 */
	{ 0x0200c000, 0x0001f803, SIZE_LONG, SCR1_Read, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0200c800, 0x0001f803, SIZE_LONG, SCR1_Read, IoMem_WriteWithoutInterceptionButTrace },

	/* System Control Register 2 */
	{ 0x0200d000, 0x0001f003, SIZE_BYTE, SCR2_Read0, SCR2_Write0 },
	{ 0x0200d001, 0x0001f003, SIZE_BYTE, SCR2_Read1, SCR2_Write1 },
	{ 0x0200d002, 0x0001f003, SIZE_BYTE, SCR2_Read2, SCR2_Write2 },
	{ 0x0200d003, 0x0001f003, SIZE_BYTE, SCR2_Read3, SCR2_Write3 },

	/* Monitor/Soundbox (Keyboard, Mouse, Sound) */
	{ 0x0200e000, 0x0001f00f, SIZE_BYTE, KMS_Stat_Snd_Read, KMS_Ctrl_Snd_Write },
	{ 0x0200e001, 0x0001f00f, SIZE_BYTE, KMS_Stat_KM_Read, KMS_Ctrl_KM_Write },
	{ 0x0200e002, 0x0001f00f, SIZE_BYTE, KMS_Stat_TX_Read, KMS_Ctrl_TX_Write },
	{ 0x0200e003, 0x0001f00f, SIZE_BYTE, KMS_Stat_Cmd_Read, KMS_Ctrl_Cmd_Write },
	{ 0x0200e004, 0x0001f00f, SIZE_LONG, KMS_Data_Read, KMS_Data_Write },
	{ 0x0200e008, 0x0001f00f, SIZE_LONG, KMS_KM_Data_Read, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0200e00c, 0x0001f00f, SIZE_LONG, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	
	/* Printer */
	{ 0x0200f000, 0x0001f00f, SIZE_BYTE, LP_CSR0_Read, LP_CSR0_Write },
	{ 0x0200f001, 0x0001f00f, SIZE_BYTE, LP_CSR1_Read, LP_CSR1_Write },
	{ 0x0200f002, 0x0001f00f, SIZE_BYTE, LP_CSR2_Read, LP_CSR2_Write },
	{ 0x0200f003, 0x0001f00f, SIZE_BYTE, LP_CSR3_Read, LP_CSR3_Write },
	{ 0x0200f004, 0x0001f00f, SIZE_LONG, LP_Data_Read, LP_Data_Write },
	
	/* Brightness */
	{ 0x02010000, 0x0001e003, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, Brightness_Write },

	/* GPIO Register */
	{ 0x02012000, 0x0001e000, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02012001, 0x0001e000, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02012002, 0x0001e000, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x02012003, 0x0001e000, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, IoMem_WriteWithoutInterceptionButTrace },

	/* SCSI Controller (NCR53C90A) */
	{ 0x02014000, 0x0001e1ff, SIZE_BYTE, ESP_TransCountL_Read, ESP_TransCountL_Write },
	{ 0x02014001, 0x0001e1ff, SIZE_BYTE, ESP_TransCountH_Read, ESP_TransCountH_Write },
	{ 0x02014002, 0x0001e1ff, SIZE_BYTE, ESP_FIFO_Read, ESP_FIFO_Write },
	{ 0x02014003, 0x0001e1ff, SIZE_BYTE, ESP_Command_Read, ESP_Command_Write },
	{ 0x02014004, 0x0001e1ff, SIZE_BYTE, ESP_Status_Read, ESP_SelectBusID_Write },
	{ 0x02014005, 0x0001e1ff, SIZE_BYTE, ESP_IntStatus_Read, ESP_SelectTimeout_Write },
	{ 0x02014006, 0x0001e1ff, SIZE_BYTE, ESP_SeqStep_Read, ESP_SyncPeriod_Write },
	{ 0x02014007, 0x0001e1ff, SIZE_BYTE, ESP_FIFOflags_Read, ESP_SyncOffset_Write },
	{ 0x02014008, 0x0001e1ff, SIZE_BYTE, ESP_Configuration_Read, ESP_Configuration_Write },
	{ 0x02014009, 0x0001e1ff, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, ESP_ClockConv_Write },
	{ 0x0201400a, 0x0001e1ff, SIZE_BYTE, IoMem_ReadWithoutInterceptionButTrace, ESP_Test_Write },
	{ 0x0201400b, 0x0001e1ff, SIZE_BYTE, ESP_Conf2_Read, ESP_Conf2_Write },
	{ 0x0201400c, 0x0001e1ff, SIZE_BYTE, ESP_Unknown_Read, ESP_Unknown_Write },
	{ 0x0201400d, 0x0001e1ff, SIZE_BYTE, ESP_Unknown_Read, ESP_Unknown_Write },
	{ 0x0201400e, 0x0001e1ff, SIZE_BYTE, ESP_Unknown_Read, ESP_Unknown_Write },
	{ 0x0201400f, 0x0001e1ff, SIZE_BYTE, ESP_Unknown_Read, ESP_Unknown_Write },
	
	/* SCSI DMA Control/Status Registers */
	{ 0x02014020, 0x0001e1ff, SIZE_BYTE, ESP_DMA_CTRL_Read, ESP_DMA_CTRL_Write },
	{ 0x02014021, 0x0001e1ff, SIZE_BYTE, ESP_DMA_FIFO_STAT_Read, ESP_DMA_FIFO_STAT_Write },
	
	/* Floppy Controller (Intel 82077AA) */
	{ 0x02014100, 0x0001e1ff, SIZE_BYTE, FLP_StatA_Read, FLP_Reserved_Write },
	{ 0x02014101, 0x0001e1ff, SIZE_BYTE, FLP_StatB_Read, FLP_Reserved_Write },
	{ 0x02014102, 0x0001e1ff, SIZE_BYTE, FLP_DataOut_Read, FLP_DataOut_Write },
	{ 0x02014103, 0x0001e1ff, SIZE_BYTE, FLP_Reserved_Read, FLP_Reserved_Write },
	{ 0x02014104, 0x0001e1ff, SIZE_BYTE, FLP_MainStatus_Read, FLP_DataRate_Write },
	{ 0x02014105, 0x0001e1ff, SIZE_BYTE, FLP_FIFO_Read, FLP_FIFO_Write },
	{ 0x02014106, 0x0001e1ff, SIZE_BYTE, FLP_Reserved_Read, FLP_Reserved_Write },
	{ 0x02014107, 0x0001e1ff, SIZE_BYTE, FLP_DataIn_Read, FLP_Configuration_Write },
	
	/* Floppy External Control */
	{ 0x02014108, 0x0001e1ff, SIZE_BYTE, FLP_Status_Read, FLP_Control_Write },
	
	/* Internal Hardclock */
	{ 0x02016000, 0x0001e007, SIZE_BYTE, HardclockRead0, HardclockWrite0 },
	{ 0x02016001, 0x0001e007, SIZE_BYTE, HardclockRead1, HardclockWrite1 },
	{ 0x02016004, 0x0001e007, SIZE_BYTE, HardclockReadCSR, HardclockWriteCSR },
	
	/* Serial Communication Controller (AMD Z8530H) */
	{ 0x02018000, 0x0001e007, SIZE_BYTE, SCC_ControlB_Read, SCC_ControlB_Write },
	{ 0x02018001, 0x0001e007, SIZE_BYTE, SCC_ControlA_Read, SCC_ControlA_Write },
	{ 0x02018002, 0x0001e007, SIZE_BYTE, SCC_DataB_Read, SCC_DataB_Write },
	{ 0x02018003, 0x0001e007, SIZE_BYTE, SCC_DataA_Read, SCC_DataA_Write },
	/* Serial Interface Clock */
	{ 0x02018004, 0x0001e007, SIZE_LONG, SCC_Clock_Read, SCC_Clock_Write },
	
	/* Event Counter */
	{ 0x0201a000, 0x0001e003, SIZE_BYTE, System_Timer_Read, System_Timer_Write },
	{ 0x0201a001, 0x0001e003, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0201a002, 0x0001e003, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterceptionButTrace },
	{ 0x0201a003, 0x0001e003, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterceptionButTrace },
	
	/* RAMDAC (Brooktree Bt463) */
	{ 0x0201c000, 0x0001e003, SIZE_BYTE, RAMDAC_Read, RAMDAC_Write },
	{ 0x0201c001, 0x0001e003, SIZE_BYTE, RAMDAC_Read, RAMDAC_Write },
	{ 0x0201c002, 0x0001e003, SIZE_BYTE, RAMDAC_Read, RAMDAC_Write },
	{ 0x0201c003, 0x0001e003, SIZE_BYTE, RAMDAC_Read, RAMDAC_Write },
	
	{ 0, 0, 0, NULL, NULL }
};
