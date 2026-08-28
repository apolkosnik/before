//============================================================================
//  NeXT DMA controller stub (Fujitsu MB610313)
//
//  Address layout from Previous src/ioMemTabNEXT.c and src/dma.c:
//    0x02000000-0x020003FF  channel CSRs (one long per channel, video
//                           channel CSR at offset 0x180)
//    0x02004000-0x02004FFF  channel next/limit/start/stop/init registers
//
//  No DMA engine yet: the pointer registers are plain storage so the ROM
//  can write and read them back, the CSRs read as 0 (all channels idle).
//
//  The one piece of real behavior is the frame interrupt path from
//  Previous dma.c dma_video_interrupt(): when the video channel limit
//  register (0x02004184) holds the magic value 0xEA, the vertical blank
//  raises INT_VIDEO; any write to the video channel CSR (0x02000180)
//  releases it.
//============================================================================

module next_dma_stub
(
	input         clk,
	input         reset,

	input         sel,
	input  [14:0] addr,          // offset within 0x02000000-0x02004FFF
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	input         vbl,           // frame pulse from the video timing
	output reg    vid_int_set,   // raise INT_VIDEO
	output reg    vid_int_clr    // release INT_VIDEO
);

// pointer register storage, 0x4000-0x4FFF, split into byte arrays so
// each maps onto block RAM (byte lane writes on a single 16-bit array
// do not infer)
reg [7:0] regs_hi [0:2047];
reg [7:0] regs_lo [0:2047];

wire is_regs = addr[14];         // 0x4000 and up
wire [10:0] rindex = addr[11:1];

reg [7:0] q_hi, q_lo;
always @(posedge clk) begin
	if (sel & we & is_regs & be[1]) begin
		regs_hi[rindex] <= wdata[15:8];
		q_hi <= wdata[15:8];
	end
	else q_hi <= regs_hi[rindex];
end
always @(posedge clk) begin
	if (sel & we & is_regs & be[0]) begin
		regs_lo[rindex] <= wdata[7:0];
		q_lo <= wdata[7:0];
	end
	else q_lo <= regs_lo[rindex];
end

assign rdata = is_regs ? {q_hi, q_lo} : 16'h0000;

// video channel limit register (long at 0x4184)
reg [31:0] vid_limit;

always @(posedge clk) begin
	vid_int_set <= 0;
	vid_int_clr <= 0;

	if (reset) begin
		vid_limit <= 32'd0;
	end
	else begin
		if (sel & we & is_regs & (addr[13:2] == 12'h061)) begin // 0x184>>2
			if (!addr[1]) begin
				if (be[1]) vid_limit[31:24] <= wdata[15:8];
				if (be[0]) vid_limit[23:16] <= wdata[7:0];
			end
			else begin
				if (be[1]) vid_limit[15:8] <= wdata[15:8];
				if (be[0]) vid_limit[7:0]  <= wdata[7:0];
			end
		end

		// dma_video_interrupt() in Previous src/dma.c
		if (vbl && vid_limit == 32'h000000EA) vid_int_set <= 1;

		// any write to the video channel CSR releases the interrupt
		if (sel & we & !is_regs & (addr[13:0] == 14'h0180)) vid_int_clr <= 1;
	end
end

endmodule
