//============================================================================
//  KMS (keyboard/mouse/sound station) interface and the sound-out DMA
//  channel
//
//  KMS registers 0x0200E000-0x0200E00F, sound out DMA channel CSR
//  0x02000040, saved pointers 0x02004030-0x0200403C, pointers
//  0x02004040-0x0200404C, init 0x02004240.
//
//  Modeled on Previous src/kms.c, src/snd.c and the sound channel parts
//  of src/dma.c:
//  - the four KMS status/control bytes with their write-one-to-clear
//    and enable-latch semantics
//  - the command/data pair: a write to the data register executes the
//    command in the command byte (KMS_Data_Write -> KMS_command);
//    implemented commands are the sound out enable/disable family
//    (KMSCMD_SND_OUT with SIO_ENABLE), the volume/control accesses as
//    no-ops, and reset
//  - the sound out engine: while enabled, a pending DMA buffer is
//    consumed from memory (the samples go nowhere yet, there is no
//    audio path), then the channel completes (COMPLETE, chain reload or
//    disable, INT_SND_OUT_DMA) after roughly len/4 microseconds, the
//    pacing snd.c uses; with no buffer pending, the underrun status
//    bits are raised with INT_SOUND_OVRUN, as kms_sndout_underrun()
//
//  Keyboard and mouse input and real audio output are TODO
//  (docs/PORTING.md).
//============================================================================

module next_kms_snd #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel_kms,       // 0x0E000-0x0E00F
	input         sel_csr,       // 0x00040-0x00043
	input         sel_sptr,      // 0x04030-0x0403F (saved)
	input         sel_ptr,       // 0x04040-0x0404F
	input         sel_ini,       // 0x04240-0x04243
	input   [3:0] addr,
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	// RAM master port
	output reg        m_req,
	output reg        m_we,
	output reg [23:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,

	output        int_snd_ovrun,   // INT_SOUND_OVRUN level
	output        int_snd_out_dma  // channel complete level
);

localparam SNDOUT_DMA_ENABLE   = 8'h80, SNDOUT_DMA_REQUEST = 8'h40,
           SNDOUT_DMA_UNDERRUN = 8'h20,
           SNDIN_DMA_ENABLE    = 8'h08, SNDIN_DMA_REQUEST  = 8'h04,
           SNDIN_DMA_OVERRUN   = 8'h02;
localparam KBD_INT = 8'h80, KBD_RECEIVED = 8'h40, KBD_OVERRUN = 8'h20,
           NMI_RECEIVED = 8'h10, KMS_INT = 8'h08, KMS_RECEIVED = 8'h04,
           KMS_OVERRUN = 8'h02;
localparam KMS_ENABLE = 8'h02, TX_LOOP = 8'h01;

//----------------------------------------------------------------------------
// KMS registers
//----------------------------------------------------------------------------

reg [7:0] st_snd, st_km, st_tx, st_cmd;
reg [31:0] kms_data;

reg       sndout_active;
reg       snd_underrun;

assign int_snd_ovrun = snd_underrun;

//----------------------------------------------------------------------------
// sound out DMA channel
//----------------------------------------------------------------------------

reg  [7:0] s_csr;
reg [31:0] s_next, s_limit, s_start, s_stop;
reg [31:0] s_snext, s_slimit, s_sstart, s_sstop;

assign int_snd_out_dma = s_csr[3];

//----------------------------------------------------------------------------
// read mux
//----------------------------------------------------------------------------

`define KMS_READ(a) ( \
	((a) == 4'h0) ? st_snd : \
	((a) == 4'h1) ? st_km : \
	((a) == 4'h2) ? st_tx : \
	((a) == 4'h3) ? st_cmd : \
	((a) == 4'h4) ? kms_data[31:24] : \
	((a) == 4'h5) ? kms_data[23:16] : \
	((a) == 4'h6) ? kms_data[15:8] : \
	((a) == 4'h7) ? kms_data[7:0] : 8'h00 )

wire [31:0] sptr_q = (addr[3:2] == 2'd0) ? s_snext :
                     (addr[3:2] == 2'd1) ? s_slimit :
                     (addr[3:2] == 2'd2) ? s_sstart : s_sstop;
wire [31:0] ptr_q  = (addr[3:2] == 2'd0) ? s_next :
                     (addr[3:2] == 2'd1) ? s_limit :
                     (addr[3:2] == 2'd2) ? s_start : s_stop;

assign rdata = sel_kms  ? {`KMS_READ({addr[3:1], 1'b0}), `KMS_READ({addr[3:1], 1'b1})} :
               sel_csr  ? (addr[1] ? 16'h0000 : {s_csr, 8'h00}) :
               sel_sptr ? (addr[1] ? sptr_q[15:0] : sptr_q[31:16]) :
               sel_ptr  ? (addr[1] ? ptr_q[15:0] : ptr_q[31:16]) :
               sel_ini  ? (addr[1] ? s_next[15:0] : s_next[31:16]) : 16'h0000;

//----------------------------------------------------------------------------
// engine
//----------------------------------------------------------------------------

localparam US_DIV = CLK_HZ / 1000000;
reg [$clog2(US_DIV)-1:0] uspresc;
wire us_tick = (uspresc == US_DIV-1);

localparam E_IDLE = 3'd0, E_RD = 3'd1, E_ACK = 3'd2, E_PACE = 3'd3;
reg  [2:0] est;
reg [17:0] pace;                 // microseconds until the completion intr
reg [15:0] poll;                 // polling interval while idle

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

// KMS command execution, KMS_command() in kms.c
task automatic kms_command;
	input [7:0] cmd;
	begin
		if ((cmd & 8'hC7) == 8'h07) begin
			// sound out
			if (cmd & 8'h08) begin       // SIO_ENABLE
				sndout_active <= 1;
			end
			else begin
				sndout_active <= 0;
				st_snd <= st_snd & ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
				snd_underrun <= 0;
			end
		end
		// 0xC4/0xC2 volume control, 0xC7 analog sound out, 0xFF reset:
		// nothing to do yet
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		st_snd <= 0; st_km <= 0; st_tx <= 0; st_cmd <= 0;
		kms_data <= 0;
		sndout_active <= 0;
		snd_underrun <= 0;
		s_csr <= 0;
		s_next <= 0; s_limit <= 0; s_start <= 0; s_stop <= 0;
		s_snext <= 0; s_slimit <= 0; s_sstart <= 0; s_sstop <= 0;
		est <= E_IDLE;
		pace <= 0;
		poll <= 0;
		uspresc <= 0;
		m_req <= 0;
	end
	else begin
		uspresc <= us_tick ? 1'd0 : uspresc + 1'd1;

		//------------------------------------------------------------
		// register writes
		//------------------------------------------------------------
		if (sel_kms & we) begin : kms_wr
			reg [3:0] a;
			reg [7:0] v;
			integer k;
			for (k = 0; k < 2; k = k + 1) begin
				if (k == 0 ? be[1] : be[0]) begin
					a = {addr[3:1], k[0]};
					v = k[0] ? wdata[7:0] : wdata[15:8];
					case (a)
						4'h0: begin
							// KMS_Ctrl_Snd_Write
							st_snd <= (st_snd & ~(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE))
							        | (v & (SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE));
							if ((v & SNDOUT_DMA_UNDERRUN) && !sndout_active) begin
								st_snd <= ((st_snd & ~(SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE))
								        | (v & (SNDOUT_DMA_ENABLE|SNDIN_DMA_ENABLE)))
								        & ~(SNDOUT_DMA_UNDERRUN|SNDOUT_DMA_REQUEST);
								snd_underrun <= 0;
							end
						end
						4'h1: begin
							// KMS_Ctrl_KM_Write: write-one-to-clear groups
							if (v & KBD_OVERRUN) st_km <= st_km & ~(KBD_RECEIVED|KBD_OVERRUN|KBD_INT);
							if (v & NMI_RECEIVED) st_km <= st_km & ~NMI_RECEIVED;
							if (v & KMS_OVERRUN) st_km <= st_km & ~(KMS_RECEIVED|KMS_OVERRUN|KMS_INT);
						end
						4'h2: st_tx <= (st_tx & ~(KMS_ENABLE|TX_LOOP)) | (v & (KMS_ENABLE|TX_LOOP));
						4'h3: st_cmd <= v;
						4'h4: kms_data[31:24] <= v;
						4'h5: kms_data[23:16] <= v;
						4'h6: kms_data[15:8] <= v;
						4'h7: begin
							kms_data[7:0] <= v;
							// KMS_Data_Write executes the pending command
							kms_command(st_cmd);
						end
						default: ;
					endcase
				end
			end
		end

		if (sel_csr & we & (csr_or != 0)) begin
			if (csr_or[4]) s_csr <= s_csr & ~8'b00001011;
			if (csr_or[1]) s_csr[1] <= 1;
			if (csr_or[0]) s_csr[0] <= 1;
			if (csr_or[3]) s_csr[3] <= 0;
		end

		if (sel_sptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) s_snext[31:24] <= wdata[15:8]; if (be[0]) s_snext[23:16] <= wdata[7:0]; end else begin if (be[1]) s_snext[15:8] <= wdata[15:8]; if (be[0]) s_snext[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) s_slimit[31:24] <= wdata[15:8]; if (be[0]) s_slimit[23:16] <= wdata[7:0]; end else begin if (be[1]) s_slimit[15:8] <= wdata[15:8]; if (be[0]) s_slimit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) s_sstart[31:24] <= wdata[15:8]; if (be[0]) s_sstart[23:16] <= wdata[7:0]; end else begin if (be[1]) s_sstart[15:8] <= wdata[15:8]; if (be[0]) s_sstart[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) s_sstop[31:24] <= wdata[15:8]; if (be[0]) s_sstop[23:16] <= wdata[7:0]; end else begin if (be[1]) s_sstop[15:8] <= wdata[15:8]; if (be[0]) s_sstop[7:0] <= wdata[7:0]; end end
			endcase
		end
		if (sel_ptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) s_next[31:24] <= wdata[15:8]; if (be[0]) s_next[23:16] <= wdata[7:0]; end else begin if (be[1]) s_next[15:8] <= wdata[15:8]; if (be[0]) s_next[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) s_limit[31:24] <= wdata[15:8]; if (be[0]) s_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) s_limit[15:8] <= wdata[15:8]; if (be[0]) s_limit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) s_start[31:24] <= wdata[15:8]; if (be[0]) s_start[23:16] <= wdata[7:0]; end else begin if (be[1]) s_start[15:8] <= wdata[15:8]; if (be[0]) s_start[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) s_stop[31:24] <= wdata[15:8]; if (be[0]) s_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) s_stop[15:8] <= wdata[15:8]; if (be[0]) s_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		// DMA_Init_Write: a write to the init register loads next
		if (sel_ini & we) begin
			if (!addr[1]) begin if (be[1]) s_next[31:24] <= wdata[15:8]; if (be[0]) s_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) s_next[15:8] <= wdata[15:8]; if (be[0]) s_next[7:0] <= wdata[7:0]; end
		end

		//------------------------------------------------------------
		// sound out engine, SND_Out_Handler() in snd.c
		//------------------------------------------------------------
		case (est)
		E_IDLE: begin
			if (sndout_active && us_tick) begin
				if (poll != 0) poll <= poll - 1'd1;
				else if (s_csr[0]) begin
					if (s_next < s_limit) begin
						pace <= {2'd0, (s_limit[17:0] - s_next[17:0])} >> 2;
						est <= E_RD;
					end
					else begin
						// nothing to play: underrun
						st_snd <= st_snd | SNDOUT_DMA_UNDERRUN | SNDOUT_DMA_REQUEST;
						snd_underrun <= 1;
						poll <= 16'd100;
					end
				end
				else begin
					st_snd <= st_snd | SNDOUT_DMA_UNDERRUN | SNDOUT_DMA_REQUEST;
					snd_underrun <= 1;
					poll <= 16'd100;
				end
			end
		end

		E_RD: begin
			if (s_next >= s_limit) est <= E_PACE;
			else begin
				m_req <= 1;
				m_we <= 0;
				m_be <= 4'hF;
				m_addr <= s_next[25:2];
				est <= E_ACK;
			end
		end

		E_ACK: if (m_ack) begin
			m_req <= 0;
			// samples are consumed; no audio output path yet
			s_next <= (s_next | 32'd3) + 32'd1;   // whole words
			est <= E_RD;
		end

		E_PACE: begin
			// interrupt after roughly len/4 microseconds
			if (us_tick) begin
				if (pace != 0) pace <= pace - 1'd1;
				else begin
					// dma_sndout_intr -> dma_interrupt(CHANNEL_SOUNDOUT)
					if (s_csr[0] && s_next == s_limit) begin
						s_csr[3] <= 1;
						if (s_csr[1]) begin
							s_next <= s_start;
							s_limit <= s_stop;
							s_csr[1] <= 0;
						end
						else s_csr[0] <= 0;
					end
					est <= E_IDLE;
				end
			end
		end

		default: est <= E_IDLE;
		endcase
	end
end

endmodule
