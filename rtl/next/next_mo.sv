//============================================================================
//  Magneto-optical drive controller (Fujitsu MB600310 "OSP") with its
//  DMA channel and the ECC buffer engine
//
//  OSP registers 0x02012000-0x02012016, disk DMA channel CSR
//  0x02000050, pointers 0x02004050-0x0201405C, init 0x02004250.
//
//  Modeled on Previous src/mo.c and the disk channel parts of
//  src/dma.c.  Implemented: the register set, the formatter command
//  register with reset and the standalone ECC commands, and the ECC
//  buffer engine in the MOCSR2_ECC_DIS ("disabled", buffer only) mode
//  the boot ROM ECC system test uses: ECC Write fills the 1024 or 1296
//  byte buffer from memory through the disk DMA channel, ECC Read
//  drains it back, MOINT_ECC_DONE is raised when a sequence completes.
//  Data passes through unchanged; the Reed-Solomon encoder that would
//  compute the 272 parity bytes is a TODO (docs/PORTING.md), so the
//  parity area reads back as buffer content.
//
//  No drive is attached: disk operations are a TODO.
//============================================================================

module next_mo #(parameter CLK_HZ = 100000000)
(
	input         clk,
	input         reset,

	// register access
	input         sel_osp,       // 0x12000-0x1201F
	input         sel_csr,       // 0x00050-0x00053
	input         sel_ptr,       // 0x04050-0x0405F
	input         sel_ini,       // 0x04250-0x04253
	input   [4:0] addr,          // byte offset within the block
	input         we,
	input   [1:0] be,
	input  [15:0] wdata,
	output [15:0] rdata,

	// RAM master port (32-bit, level req / level ack)
	output reg        m_req,
	output reg        m_we,
	output reg [23:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,

	output        int_disk,      // OSP interrupt level
	output        int_disk_dma   // DMA channel complete level
);

localparam MOINT_ECC_DONE   = 8'h08;
localparam MOINT_OPER_COMPL = 8'h04;
localparam MOCSR2_ECC_CMP   = 8'h02;
localparam MOCSR2_ECC_BLOCKS= 8'h10;
localparam MOCSR2_ECC_MODE  = 8'h20;
localparam MOCSR2_ECC_DIS   = 8'h40;

localparam FMT_ECC_READ  = 8'h80;
localparam FMT_ECC_WRITE = 8'h40;
localparam FMT_RD_STAT   = 8'h20;

localparam SECT_DATA = 12'd1024;   // MO_SECTORSIZE_DATA
localparam SECT_DISK = 12'd1296;   // MO_SECTORSIZE_DISK

//----------------------------------------------------------------------------
// OSP registers
//----------------------------------------------------------------------------

reg [7:0] tracknumh, tracknuml;
reg [3:0] sector_num;
reg [3:0] sector_incr;
reg [7:0] sector_cnt;
reg [7:0] intstatus, intmask;
reg [7:0] csr2, csr1;
reg [7:0] csrh, csrl;
reg [7:0] err_stat, ecc_cnt;
reg [7:0] r_init, r_format, r_mark;
reg [7:0] flag0, flag1, flag2, flag3, flag4, flag5, flag6;

assign int_disk = |(intstatus & intmask);

//----------------------------------------------------------------------------
// disk DMA channel
//----------------------------------------------------------------------------

reg  [7:0] d_csr;
reg [31:0] d_next, d_limit, d_start, d_stop;

assign int_disk_dma = d_csr[3];

//----------------------------------------------------------------------------
// register read mux
//----------------------------------------------------------------------------

`define MO_READ(a) ( \
	((a) == 5'h00) ? tracknumh : \
	((a) == 5'h01) ? tracknuml : \
	((a) == 5'h02) ? {4'd0, sector_num} : \
	((a) == 5'h03) ? sector_cnt : \
	((a) == 5'h04) ? intstatus : \
	((a) == 5'h05) ? intmask : \
	((a) == 5'h06) ? csr2 : \
	((a) == 5'h07) ? csr1 : \
	((a) == 5'h08) ? csrh : \
	((a) == 5'h09) ? csrl : \
	((a) == 5'h0A) ? err_stat : \
	((a) == 5'h0B) ? ecc_cnt : \
	((a) == 5'h10) ? flag0 : \
	((a) == 5'h11) ? flag1 : \
	((a) == 5'h12) ? flag2 : \
	((a) == 5'h13) ? flag3 : \
	((a) == 5'h14) ? flag4 : \
	((a) == 5'h15) ? flag5 : \
	((a) == 5'h16) ? flag6 : 8'h00 )

wire [31:0] ptr_q = (addr[3:2] == 2'd0) ? d_next :
                    (addr[3:2] == 2'd1) ? d_limit :
                    (addr[3:2] == 2'd2) ? d_start : d_stop;

assign rdata = sel_osp ? {`MO_READ({addr[4:1], 1'b0}), `MO_READ({addr[4:1], 1'b1})} :
               sel_csr ? (addr[1] ? 16'h0000 : {d_csr, 8'h00}) :
               sel_ptr ? (addr[1] ? ptr_q[15:0] : ptr_q[31:16]) :
               sel_ini ? (addr[1] ? d_next[15:0] : d_next[31:16]) : 16'h0000;

//----------------------------------------------------------------------------
// ECC buffer and engine
//----------------------------------------------------------------------------

reg  [7:0] eccbuf [0:1295];
reg [11:0] ecc_size, ecc_limit;
reg [11:0] drain_pos;

localparam ECC_DONE = 3'd0, ECC_FILL = 3'd1, ECC_ECCING = 3'd2,
           ECC_DRAIN = 3'd3, ECC_RS = 3'd4;
reg  [2:0] ecc_state;
reg        ecc_is_read;          // current mode: 1 = ECC read (drain)
reg        ecc_repeat;

localparam M_IDLE = 3'd0, M_REQ = 3'd1, M_WAIT = 3'd2, M_PRE = 3'd3;
reg  [2:0] mst;

// Reed-Solomon codec sharing the ECC buffer
reg   [7:0] ecc_q;
wire        rs_done, rs_fail;
wire  [7:0] rs_count;
wire [10:0] rs_addr;
wire  [7:0] rs_wdata;
wire        rs_we;
reg         rs_start_enc, rs_start_dec;
wire        rs_active = (ecc_state == ECC_RS);

next_rs rs
(
	.clk(clk),
	.reset(reset),
	.start_encode(rs_start_enc),
	.start_decode(rs_start_dec),
	.done(rs_done),
	.fail(rs_fail),
	.err_count(rs_count),
	.b_addr(rs_addr),
	.b_rdata(ecc_q),
	.b_wdata(rs_wdata),
	.b_we(rs_we)
);

// single buffer port, muxed between the RS codec and the fill/drain
// engine; read data is registered (one cycle of latency)
reg  [10:0] mo_addr;
reg   [7:0] mo_wdata;
reg         mo_we;

wire [10:0] ecc_addr = rs_active ? rs_addr  : mo_addr;
wire  [7:0] ecc_wd   = rs_active ? rs_wdata : mo_wdata;
wire        ecc_we   = rs_active ? rs_we    : mo_we;

always @(posedge clk) begin
	if (ecc_we) eccbuf[ecc_addr] <= ecc_wd;
	ecc_q <= eccbuf[ecc_addr];
end

// engine step delay, roughly the ECC_DELAY pacing in mo.c
localparam TICK = CLK_HZ / 1000000 * 50;    // 50 us
reg [$clog2(TICK)-1:0] tickcnt;
wire tick = (tickcnt == TICK-1);

integer i;

task automatic osp_int;
	input [7:0] bits;
	begin
		intstatus <= intstatus | bits;
	end
endtask

// dma_interrupt(CHANNEL_DISK): complete only when next reached limit
task automatic dma_done_disk;
	begin
		if (d_next == d_limit) begin
			d_csr[3] <= 1;
			if (d_csr[1]) begin
				d_next <= d_start;
				d_limit <= d_stop;
				d_csr[1] <= 0;
			end
			else d_csr[0] <= 0;
		end
	end
endtask

task automatic ecc_sequence_done;
	begin
		ecc_state <= ECC_DONE;
		if (csr2 & MOCSR2_ECC_DIS) osp_int(MOINT_ECC_DONE);
	end
endtask

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

always @(posedge clk) begin
	if (reset) begin
		tracknumh <= 0; tracknuml <= 0;
		sector_num <= 0; sector_incr <= 0; sector_cnt <= 0;
		intstatus <= 0; intmask <= 0;
		csr2 <= 0; csr1 <= 0; csrh <= 0; csrl <= 0;
		err_stat <= 0; ecc_cnt <= 0;
		r_init <= 0; r_format <= 0; r_mark <= 0;
		flag0 <= 0; flag1 <= 0; flag2 <= 0; flag3 <= 0;
		flag4 <= 0; flag5 <= 0; flag6 <= 0;
		d_csr <= 0;
		d_next <= 0; d_limit <= 0; d_start <= 0; d_stop <= 0;
		ecc_state <= ECC_DONE;
		ecc_size <= 0; ecc_limit <= 0; drain_pos <= 0;
		ecc_is_read <= 0; ecc_repeat <= 0;
		mst <= M_IDLE;
		m_req <= 0;
		mo_we <= 0;
		rs_start_enc <= 0;
		rs_start_dec <= 0;
		tickcnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;
		mo_we <= 0;
		rs_start_enc <= 0;
		rs_start_dec <= 0;

		//------------------------------------------------------------
		// OSP register writes
		//------------------------------------------------------------
		if (sel_osp & we) begin : osp_wr
			reg [4:0] a;
			reg [7:0] v;
			integer k;
			for (k = 0; k < 2; k = k + 1) begin
				if (k == 0 ? be[1] : be[0]) begin
					a = {addr[4:1], k[0]};
					v = k[0] ? wdata[7:0] : wdata[15:8];
					case (a)
						5'h00: tracknumh <= v;
						5'h01: tracknuml <= v;
						5'h02: begin sector_num <= v[3:0]; sector_incr <= v[7:4]; end
						5'h03: sector_cnt <= v;
						5'h04: intstatus <= intstatus & ~v;
						5'h05: intmask <= v;
						5'h06: csr2 <= v;
						5'h07: begin
							// mo_formatter_cmd()
							csr1 <= v;
							if (v == 8'h00) begin
								ecc_state <= ECC_DONE;
								ecc_cnt <= 0;
								err_stat <= 0;
							end
							else begin
								if (v & FMT_ECC_READ) begin
									if (ecc_state == ECC_DONE) begin
										ecc_is_read <= 1;
										ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
										ecc_state <= ECC_ECCING;
									end
								end
								if (v & FMT_ECC_WRITE) begin
									if (ecc_state == ECC_DONE) begin
										ecc_is_read <= 0;
										ecc_repeat <= |(csr2 & MOCSR2_ECC_BLOCKS);
										ecc_size <= 0;
										ecc_limit <= SECT_DATA;
										ecc_state <= ECC_FILL;
									end
								end
								if (v & FMT_RD_STAT) begin
									csrh <= 8'h00;   // no drive attached
									csrl <= 8'h00;
								end
							end
						end
						5'h08: csrh <= v;
						5'h09: csrl <= v;
						5'h0C: r_init <= v;
						5'h0D: r_format <= v;
						5'h0E: r_mark <= v;
						5'h10: flag0 <= v;
						5'h11: flag1 <= v;
						5'h12: flag2 <= v;
						5'h13: flag3 <= v;
						5'h14: flag4 <= v;
						5'h15: flag5 <= v;
						5'h16: flag6 <= v;
						default: ;
					endcase
				end
			end
		end

		// DMA CSR command (OR of byte lanes, like DMA_CSR_Write)
		if (sel_csr & we & (csr_or != 0)) begin
			if (csr_or[4]) d_csr <= d_csr & ~8'b00001011;   // RESET
			if (csr_or[1]) d_csr[1] <= 1;                   // SETSUPDATE
			if (csr_or[0]) d_csr[0] <= 1;                   // SETENABLE
			if (csr_or[3]) d_csr[3] <= 0;                   // CLRCOMPLETE
		end

		if (sel_ptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) d_next[31:24] <= wdata[15:8]; if (be[0]) d_next[23:16] <= wdata[7:0]; end else begin if (be[1]) d_next[15:8] <= wdata[15:8]; if (be[0]) d_next[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) d_limit[31:24] <= wdata[15:8]; if (be[0]) d_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) d_limit[15:8] <= wdata[15:8]; if (be[0]) d_limit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) d_start[31:24] <= wdata[15:8]; if (be[0]) d_start[23:16] <= wdata[7:0]; end else begin if (be[1]) d_start[15:8] <= wdata[15:8]; if (be[0]) d_start[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) d_stop[31:24] <= wdata[15:8]; if (be[0]) d_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) d_stop[15:8] <= wdata[15:8]; if (be[0]) d_stop[7:0] <= wdata[7:0]; end end
			endcase
		end
		// DMA_Init_Write: a write to the init register loads next
		if (sel_ini & we) begin
			if (!addr[1]) begin if (be[1]) d_next[31:24] <= wdata[15:8]; if (be[0]) d_next[23:16] <= wdata[7:0]; end
			else begin if (be[1]) d_next[15:8] <= wdata[15:8]; if (be[0]) d_next[7:0] <= wdata[7:0]; end
		end

		//------------------------------------------------------------
		// ECC engine
		//------------------------------------------------------------
		case (ecc_state)
		ECC_FILL: begin
			// dma_mo_read_memory: memory to buffer, 32-bit words
			if (csr2 & MOCSR2_ECC_MODE) ecc_limit <= SECT_DISK;
			if (ecc_size >= ecc_limit) begin
				dma_done_disk;
				ecc_state <= ECC_ECCING;
				mst <= M_IDLE;
				m_req <= 0;
			end
			else if (d_csr[0] && d_next < d_limit) begin
				case (mst)
				M_IDLE: if (tick) mst <= M_REQ;
				M_REQ: begin
					m_req <= 1;
					m_we <= 0;
					m_be <= 4'hF;
					m_addr <= d_next[25:2];
					mst <= M_WAIT;
				end
				M_WAIT: if (m_ack) begin
					m_req <= 0;
					mo_addr <= ecc_size[10:0];
					mo_wdata <=
						(d_next[1:0] == 2'd0) ? m_dout[31:24] :
						(d_next[1:0] == 2'd1) ? m_dout[23:16] :
						(d_next[1:0] == 2'd2) ? m_dout[15:8] : m_dout[7:0];
					mo_we <= 1;
					ecc_size <= ecc_size + 12'd1;
					d_next <= d_next + 32'd1;
					mst <= M_REQ;
				end
				default: mst <= M_IDLE;
				endcase
			end
			// else: starving, wait for the CPU to enable the channel
		end

		ECC_ECCING: begin
			if (ecc_is_read) begin
				// ecc_encode(): with ECC_DIS and ECC_MODE both set the
				// encoder is bypassed and the buffer drains as it is;
				// otherwise the codec builds the 1296 byte sector
				if ((csr2 & MOCSR2_ECC_DIS) && (csr2 & MOCSR2_ECC_MODE)) begin
					drain_pos <= 0;
					ecc_state <= ECC_DRAIN;
					mst <= M_IDLE;
				end
				else begin
					rs_start_enc <= 1;
					ecc_state <= ECC_RS;
				end
			end
			else begin
				// ecc_decode(): with ECC_DIS and no ECC_MODE the decoder
				// is bypassed; otherwise the codec corrects the sector
				if ((csr2 & MOCSR2_ECC_DIS) && !(csr2 & MOCSR2_ECC_MODE)) begin
					ecc_sequence_done;
				end
				else begin
					rs_start_dec <= 1;
					ecc_state <= ECC_RS;
				end
			end
		end

		ECC_RS: begin
			if (rs_done) begin
				if (ecc_is_read) begin
					// encoded: 1024 -> 1296, drain follows
					ecc_size <= SECT_DISK;
					ecc_limit <= SECT_DISK;
					drain_pos <= 0;
					ecc_state <= ECC_DRAIN;
					mst <= M_IDLE;
				end
				else begin
					// decoded: 1296 -> 1024
					ecc_size <= SECT_DATA;
					ecc_limit <= SECT_DATA;
					if (rs_fail) begin
						err_stat <= 8'h01;              // ERRSTAT_ECC
						osp_int(8'h80);                 // MOINT_DATA_ERR
						ecc_state <= ECC_DONE;
					end
					else begin
						if (ecc_cnt == 0) ecc_cnt <= rs_count;
						ecc_sequence_done;
					end
				end
			end
		end

		ECC_DRAIN: begin
			// dma_mo_write_memory: buffer to memory
			if (drain_pos >= ecc_size) begin
				dma_done_disk;
				ecc_sequence_done;
				mst <= M_IDLE;
				m_req <= 0;
			end
			else if (d_csr[0] && d_next < d_limit) begin
				case (mst)
				M_IDLE: if (tick) begin
					mo_addr <= drain_pos[10:0];
					mst <= M_PRE;
				end
				M_PRE: mst <= M_REQ;    // buffer read data settles
				M_REQ: begin
					m_req <= 1;
					m_we <= 1;
					m_be <= 4'b1000 >> d_next[1:0];
					m_addr <= d_next[25:2];
					m_din <= {4{ecc_q}};
					mst <= M_WAIT;
				end
				M_WAIT: if (m_ack) begin
					m_req <= 0;
					drain_pos <= drain_pos + 12'd1;
					d_next <= d_next + 32'd1;
					mo_addr <= drain_pos[10:0] + 11'd1;
					mst <= M_PRE;
				end
				default: mst <= M_IDLE;
				endcase
			end
		end

		default: ;  // ECC_DONE: idle
		endcase
	end
end

endmodule
