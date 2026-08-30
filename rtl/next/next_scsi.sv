//============================================================================
//  NeXT SCSI subsystem: NCR53C90 (ESP), the SCSI disk target and the
//  NeXT DMA channel for SCSI, modeled after esp.c, scsi.c and dma.c of
//  the Previous emulator.  The disk itself is a MiSTer SD-card image
//  (hps_io block access, 512 byte blocks).
//
//  Registers:
//    sel_esp: ESP + DMA control  0x02014000-0x0201403F
//    sel_csr: DMA CSR            0x02000010-0x02000013
//    sel_ptr: DMA next/limit     0x02004010-0x0200401F
//    sel_ini: DMA init           0x02004210-0x02004213
//
//  The initiator command set is the one Previous emulates: select
//  with/without ATN reads the CDB from the FIFO, the target executes
//  the command, transfer info moves data through the DMA channel,
//  ICCS collects status and message, message accepted disconnects.
//  Selecting an absent target times out with a disconnect interrupt.
//============================================================================

module next_scsi #(parameter CLK_HZ = 50000000)
(
	input         clk,
	input         reset,

	input         sel_esp,
	input         sel_csr,
	input         sel_sptr,      // saved next/limit/start/stop
	input         sel_ptr,
	input         sel_ini,
	input   [5:0] addr,          // byte offset within the selected block
	input         we,
	input   [1:0] be,            // [1] = even byte, [0] = odd byte
	input  [15:0] wdata,
	output [15:0] rdata,

	// DMA master port (32-bit words in main RAM)
	output reg        m_req,
	output reg        m_we,
	output reg [23:0] m_addr,
	output reg  [3:0] m_be,
	output reg [31:0] m_din,
	input      [31:0] m_dout,
	input             m_ack,

	// The floppy shares this DMA channel: when the controller switches
	// it over, the engine moves the floppy's sector buffer instead of
	// its own, exactly as dma_esp_*_memory does on floppy_select.
	input         flp_select,
	input         flp_req,       // the floppy wants the channel
	input         flp_wr,        // 1 = floppy to memory
	input  [10:0] flp_len,       // bytes in the sector
	output  [9:0] flp_addr,
	output        flp_bwe,
	output  [7:0] flp_bwdata,
	input   [7:0] flp_bq,
	output reg    flp_done,

	output        int_scsi,      // level, for INT_SCSI
	output        int_scsi_dma,  // level, for INT_SCSI_DMA

	// MiSTer SD block interface (image slot 0)
	input         img_mounted,
	input         img_readonly,
	input  [63:0] img_size,
	output [31:0] sd_lba,
	output reg    sd_rd,
	output reg    sd_wr,
	input         sd_ack,
	input   [8:0] sd_buff_addr,
	input   [7:0] sd_buff_dout,
	output  [7:0] sd_buff_din,
	input         sd_buff_wr
);

localparam STAT_VGC = 8'h08, STAT_TC = 8'h10, STAT_PE = 8'h20,
           STAT_GE  = 8'h40, STAT_INT = 8'h80;

localparam INTR_SEL = 8'h01, INTR_SELATN = 8'h02, INTR_RESEL = 8'h04,
           INTR_FC  = 8'h08, INTR_BS     = 8'h10, INTR_DC    = 8'h20,
           INTR_ILL = 8'h40, INTR_RST    = 8'h80;

localparam PHASE_DO = 3'd0, PHASE_DI = 3'd1, PHASE_CD = 3'd2,
           PHASE_ST = 3'd3, PHASE_MO = 3'd6, PHASE_MI = 3'd7;

localparam STAT_GOOD = 8'h00, STAT_CHECK_COND = 8'h02;

localparam SC_NO_ERROR      = 8'h00, SC_INVALID_CMD = 8'h20,
           SC_INVALID_LBA   = 8'h21, SC_INVALID_LUN = 8'h25,
           SC_WRITE_PROTECT = 8'h27;

//----------------------------------------------------------------------------
// ESP registers
//----------------------------------------------------------------------------

reg  [7:0] fifo [0:15];
reg  [7:0] fifo_head;            // mirror of fifo[0] for the read mux
reg  [4:0] fifoflags;

reg  [7:0] wr_tcl, wr_tch;       // write staging (not changed by reset)
reg [16:0] counter;

reg  [7:0] command0;
reg  [7:0] status;
reg  [7:0] intstatus;
reg  [7:0] seqstep;
reg  [7:0] syncperiod, syncoffset;
reg  [7:0] configuration, conf2;
reg  [7:0] clockconv;
reg  [7:0] selectbusid, selecttimeout;
reg  [7:0] dma_control, dma_status;
reg        mode_dma;

reg  [2:0] phase;                // SCSIbus.phase, read in status[2:0]

assign int_scsi = dma_control[5] & status[7];   // ESPCTRL_ENABLE_INT & STAT_INT

//----------------------------------------------------------------------------
// SCSI disk target state
//----------------------------------------------------------------------------

reg        disk_present = 0;
reg        disk_ro = 0;
reg [31:0] img_blocks = 0;       // disk size in 512 byte blocks

reg  [7:0] t_status;             // status byte for ICCS
reg  [7:0] t_message;            // message byte for ICCS
reg  [7:0] sense_code;
reg  [3:0] sense_key;
reg        sense_valid;
reg [31:0] sense_info;
reg  [2:0] t_lun;

reg [31:0] lba;
reg [15:0] blockcounter;

// disk geometry for mode sense page 4 (cylinders = blocks / (16*63),
// rounded up; the head/sector fallback geometry of SCSI_GuessGeometry)
reg [23:0] geo_cyl = 0;
reg [31:0] g_num = 0;
reg [31:0] g_rem = 0;
reg [23:0] g_quot = 0;
reg  [5:0] g_step = 0;
reg        g_run = 0;

//----------------------------------------------------------------------------
// DMA channel (CHANNEL_SCSI)
//----------------------------------------------------------------------------

reg  [7:0] d_csr;
reg [31:0] d_next, d_limit, d_start, d_stop;
// saved registers: plain storage on the non-turbo board (the engine
// never writes them, DMA_Saved_*_Read/Write in dma.c)
reg [31:0] s_next, s_limit, s_start, s_stop;

assign int_scsi_dma = d_csr[3];

//----------------------------------------------------------------------------
// data buffer: one 512 byte sector, also carries the command responses.
// Single write site and single registered read (block RAM discipline).
//----------------------------------------------------------------------------

reg  [7:0] dbuf [0:511];
reg  [7:0] db_q;

reg  [9:0] buf_pos, buf_limit;
reg        buf_disk;             // 1 = buffer refills from the disk image

reg  [8:0] fill_idx;
reg  [1:0] fill_kind;
localparam R_INQ = 2'd0, R_CAP = 2'd1, R_SENSE = 2'd2, R_MODE = 2'd3;

// mode sense composition, precomputed at dispatch
reg  [7:0] ms_page;
reg  [1:0] ms_ctl;
reg  [4:0] ms_hdr;               // 4 or 12
reg  [7:0] ms_total;

// engine states
localparam X_IDLE    = 5'd0,  X_SEL_MSG = 5'd1,  X_SEL_CDB = 5'd2,
           X_DISPATCH= 5'd3,  X_FILL    = 5'd4,  X_POSTCMD = 5'd5,
           X_INT_WAIT= 5'd6,  X_RD_SECT = 5'd7,  X_SD_RD_GO= 5'd8,
           X_SD_RD_ACK=5'd9,  X_WR_SECT = 5'd10, X_SD_WR_GO= 5'd11,
           X_SD_WR_ACK=5'd12, X_DI_CHK  = 5'd13, X_DI_RD   = 5'd14,
           X_DI_GET  = 5'd15, X_DI_WR   = 5'd16, X_DO_CHK  = 5'd17,
           X_DO_RD   = 5'd18, X_DO_PUT  = 5'd19, X_ICCS1   = 5'd20,
           X_ICCS2   = 5'd21, X_PIO_RD  = 5'd22, X_PIO_GET = 5'd23;
reg  [4:0] xst;

reg        sel_atn;              // current select has an identify message
reg  [7:0] cdb0, cdb1, cdb2, cdb3, cdb4, cdb5, cdb6, cdb7, cdb8, cdb9;
reg  [3:0] cdb_n;

reg  [1:0] rd_ret;               // X_RD_SECT return: 0 = dispatch, 1 = DMA, 2 = PIO
reg        sd_ret;               // SD op return: 0 = read path, 1 = write path
reg        pad_mode;             // transfer without the memory channel
reg  [1:0] word_cnt;             // bytes gathered in the current DMA word
reg [31:0] word_buf;
reg  [2:0] do_rem;               // bytes left to unpack from a DMA word

reg        flp_active;         // a floppy sector is in flight
reg        flp_req_d;          // for the rising edge of a sector request
reg        flp_pend;           // a request seen but not yet taken
reg [24:0] dly_us;               // interrupt delay countdown
localparam ESP_DELAY_US = 25'd100;

// DMA pacing, matching the reference emulator: the first memory pass
// of a transfer waits a seek/sector time, and after every channel
// limit event (buffer complete, chain swap) the next pass waits the
// ESP_IO tick.  This is what gives the driver time to service the
// complete interrupt and program the next chain segment before data
// flows again; without it the engine outruns the CPU and fills a
// swapped-in buffer whose start/stop the driver has not refreshed yet.
localparam SECTOR_US = 9'd350;    // SCSI_SECTOR_TIME_HD
localparam GAP_US    = 9'd100;    // ESP_IO tick
reg  [8:0] gap_us;

reg [31:0] sd_lba_r;
assign sd_lba = sd_lba_r;

//----------------------------------------------------------------------------
// disk image mount and geometry (cylinders = blocks/1008, rounded up,
// by shift-subtract division).  Outside the reset: the mount pulse
// fires once at OSD time, usually before the user resets the machine
// into the new configuration.
//----------------------------------------------------------------------------

always @(posedge clk) begin
	if (img_mounted) begin
		disk_present <= (img_size != 0);
		disk_ro <= img_readonly;
		img_blocks <= img_size[40:9];
		g_num <= img_size[40:9];
		g_rem <= 0;
		g_quot <= 0;
		g_step <= 6'd32;
		g_run <= 1;
	end
	else if (g_run) begin : geom
		reg [31:0] top;
		top = {g_rem[30:0], g_num[31]};
		if (g_step != 0) begin
			g_num <= {g_num[30:0], 1'b0};
			if (top >= 32'd1008) begin
				g_rem <= top - 32'd1008;
				g_quot <= {g_quot[22:0], 1'b1};
			end
			else begin
				g_rem <= top;
				g_quot <= {g_quot[22:0], 1'b0};
			end
			g_step <= g_step - 1'd1;
		end
		else begin
			geo_cyl <= (g_rem != 0) ? g_quot + 24'd1 : g_quot;
			g_run <= 0;
		end
	end
end

// microsecond tick (a 1 MHz simulation clock degenerates to a tick
// every cycle; keep the counter width legal for that case)
localparam TICK = CLK_HZ / 1000000;
localparam TCW = (TICK > 1) ? $clog2(TICK) : 1;
reg [TCW-1:0] tickcnt;
wire tick = (tickcnt == TICK-1);

//----------------------------------------------------------------------------
// register read mux (byte offsets; each 16-bit word carries two regs).
// Plain nested ternaries over scalar registers: functions or array reads
// in combinational paths break sensitivity in some simulators.
//----------------------------------------------------------------------------

wire [5:0] a_even = {addr[5:1], 1'b0};
wire [5:0] a_odd  = {addr[5:1], 1'b1};

`define ESP_READ(a) ( \
	((a) == 6'h00) ? counter[7:0] : \
	((a) == 6'h01) ? counter[15:8] : \
	((a) == 6'h02) ? fifo_head : \
	((a) == 6'h03) ? command0 : \
	((a) == 6'h04) ? ((status & 8'hF8) | {5'd0, phase}) : \
	((a) == 6'h05) ? intstatus : \
	((a) == 6'h06) ? seqstep : \
	((a) == 6'h07) ? {3'd0, fifoflags} : \
	((a) == 6'h08) ? configuration : \
	((a) == 6'h0B) ? conf2 : \
	((a) == 6'h20) ? dma_control : \
	((a) == 6'h21) ? dma_status : 8'h00 )

wire [31:0] ptr_q = (addr[3:2] == 2'd0) ? d_next :
                    (addr[3:2] == 2'd1) ? d_limit :
                    (addr[3:2] == 2'd2) ? d_start : d_stop;

wire [31:0] sptr_q = (addr[3:2] == 2'd0) ? s_next :
                     (addr[3:2] == 2'd1) ? s_limit :
                     (addr[3:2] == 2'd2) ? s_start : s_stop;

assign rdata = sel_esp ? {`ESP_READ(a_even), `ESP_READ(a_odd)} :
               sel_csr ? (addr[1] ? 16'h0000 : {d_csr, 8'h00}) :
               sel_sptr ? (addr[1] ? sptr_q[15:0] : sptr_q[31:16]) :
               sel_ptr ? (addr[1] ? ptr_q[15:0] : ptr_q[31:16]) :
               sel_ini ? (addr[1] ? d_next[15:0] : d_next[31:16]) : 16'h0000;

//----------------------------------------------------------------------------
// data buffer port
//----------------------------------------------------------------------------

wire in_sd_rd = (xst == X_SD_RD_GO) || (xst == X_SD_RD_ACK);
wire in_sd_wr = (xst == X_SD_WR_GO) || (xst == X_SD_WR_ACK);

reg        eng_we;
reg  [8:0] eng_addr;
reg  [7:0] eng_wd;

wire       db_we   = in_sd_rd ? sd_buff_wr   : eng_we;
wire [8:0] db_addr = in_sd_rd ? sd_buff_addr :
                     in_sd_wr ? sd_buff_addr : eng_addr;
wire [7:0] db_wd   = in_sd_rd ? sd_buff_dout : eng_wd;

// while the floppy owns the channel the engine addresses its buffer
wire fdma = flp_select & flp_active;
assign flp_addr   = {1'b0, eng_addr};
assign flp_bwe    = fdma & eng_we;
assign flp_bwdata = eng_wd;
wire [7:0] eng_q  = fdma ? flp_bq : db_q;

always @(posedge clk) begin
	if (db_we) dbuf[db_addr] <= db_wd;
	db_q <= dbuf[db_addr];
end

assign sd_buff_din = db_q;

//----------------------------------------------------------------------------
// response byte tables
//----------------------------------------------------------------------------

function automatic [7:0] inq_byte;
	input [7:0] i;
	begin
		case (i)
			8'd00: inq_byte = (t_lun != 0) ? 8'h7F : 8'h00; // disk / not present
			8'd02: inq_byte = 8'h01;   // ANSI SCSI-1
			8'd03: inq_byte = 8'h02;   // response format
			8'd04: inq_byte = 8'h31;   // additional length
			8'd07: inq_byte = 8'h1C;   // sync, linked
			8'd08: inq_byte = "P"; 8'd09: inq_byte = "r"; 8'd10: inq_byte = "e";
			8'd11: inq_byte = "v"; 8'd12: inq_byte = "i"; 8'd13: inq_byte = "o";
			8'd14: inq_byte = "u"; 8'd15: inq_byte = "s";
			8'd16: inq_byte = "H"; 8'd17: inq_byte = "D"; 8'd18: inq_byte = "D";
			8'd32, 8'd33, 8'd34, 8'd35, 8'd36, 8'd37, 8'd38: inq_byte = "0";
			8'd39: inq_byte = "1";
			8'd40, 8'd41, 8'd42, 8'd43, 8'd44, 8'd45, 8'd46, 8'd47: inq_byte = "0";
			default: inq_byte = (i >= 8'd16 && i <= 8'd31) ? " " :
			                    (i >= 8'd48) ? " " : 8'h00;
		endcase
	end
endfunction

wire [31:0] last_lba = img_blocks - 32'd1;

function automatic [7:0] cap_byte;
	input [7:0] i;
	begin
		case (i)
			8'd0: cap_byte = last_lba[31:24];
			8'd1: cap_byte = last_lba[23:16];
			8'd2: cap_byte = last_lba[15:8];
			8'd3: cap_byte = last_lba[7:0];
			8'd6: cap_byte = 8'h02;    // blocksize 512
			default: cap_byte = 8'h00;
		endcase
	end
endfunction

function automatic [7:0] sense_byte;
	input [7:0] i;
	begin
		case (i)
			8'd0: sense_byte = sense_valid ? 8'hF0 : 8'h70;
			8'd2: sense_byte = {4'd0, sense_key};
			8'd3: sense_byte = sense_valid ? sense_info[31:24] : 8'h00;
			8'd4: sense_byte = sense_valid ? sense_info[23:16] : 8'h00;
			8'd5: sense_byte = sense_valid ? sense_info[15:8]  : 8'h00;
			8'd6: sense_byte = sense_valid ? sense_info[7:0]   : 8'h00;
			8'd7: sense_byte = 8'd14;
			8'd12: sense_byte = sense_code;
			default: sense_byte = 8'h00;
		endcase
	end
endfunction

function automatic [3:0] key_of;
	input [7:0] code;
	begin
		case (code)
			SC_NO_ERROR:      key_of = 4'h0;  // no sense
			8'h04:            key_of = 4'h2;  // not ready
			8'h03, SC_INVALID_CMD, SC_INVALID_LBA, 8'h24, SC_INVALID_LUN:
			                  key_of = 4'h5;  // illegal request
			SC_WRITE_PROTECT: key_of = 4'h7;  // data protect
			default:          key_of = 4'h4;  // hardware error
		endcase
	end
endfunction

// mode pages the reference emulator provides: 0x00 (4 bytes),
// 0x01 (8 bytes), 0x04 (20 bytes); everything else is empty
function automatic [7:0] page_len;
	input [7:0] p;
	begin
		case (p)
			8'h00: page_len = 8'd4;
			8'h01: page_len = 8'd8;
			8'h04: page_len = 8'd20;
			8'h3F: page_len = 8'd32;   // all pages: 4 + 8 + 20
			default: page_len = 8'd0;
		endcase
	end
endfunction

function automatic [7:0] page0_byte;
	input [7:0] i;
	begin
		case (i)
			8'd1: page0_byte = 8'h02;
			8'd2: page0_byte = 8'h80;
			default: page0_byte = 8'h00;
		endcase
	end
endfunction

function automatic [7:0] page1_byte;
	input [7:0] i;
	begin
		case (i)
			8'd0: page1_byte = 8'h01;
			8'd1: page1_byte = 8'h06;
			8'd3: page1_byte = 8'h1B;
			8'd4: page1_byte = 8'h0B;
			8'd7: page1_byte = 8'hFF;
			default: page1_byte = 8'h00;
		endcase
	end
endfunction

function automatic [7:0] page4_byte;
	input [7:0] i;
	begin
		case (i)
			8'd0: page4_byte = 8'h04;
			8'd1: page4_byte = 8'h12;
			8'd2: page4_byte = geo_cyl[23:16];
			8'd3: page4_byte = geo_cyl[15:8];
			8'd4: page4_byte = geo_cyl[7:0];
			8'd5: page4_byte = 8'd16;  // heads
			default: page4_byte = 8'h00;
		endcase
	end
endfunction

function automatic [7:0] mode_byte;
	input [7:0] i;
	reg [7:0] off;
	begin
		if (i < {3'd0, ms_hdr}) begin
			// header and block descriptor
			case (i)
				8'd0: mode_byte = ms_total - 8'd1;
				8'd2: mode_byte = disk_ro ? 8'h80 : 8'h00;
				8'd3: mode_byte = 8'h08;
				8'd5: mode_byte = img_blocks[23:16];
				8'd6: mode_byte = img_blocks[15:8];
				8'd7: mode_byte = img_blocks[7:0];
				8'd10: mode_byte = 8'h02;  // blocksize 512
				default: mode_byte = 8'h00;
			endcase
		end
		else begin
			off = i - {3'd0, ms_hdr};
			if (ms_ctl == 2'd1) mode_byte = 8'h00;   // changeable values
			else if (ms_page == 8'h3F)
				mode_byte = (off < 8'd4)  ? page0_byte(off) :
				            (off < 8'd12) ? page1_byte(off - 8'd4) :
				                            page4_byte(off - 8'd12);
			else if (ms_page == 8'h00) mode_byte = page0_byte(off);
			else if (ms_page == 8'h01) mode_byte = page1_byte(off);
			else if (ms_page == 8'h04) mode_byte = page4_byte(off);
			else mode_byte = 8'h00;
		end
	end
endfunction

function automatic [7:0] resp_byte;
	input [1:0] kind;
	input [8:0] i;
	begin
		case (kind)
			R_INQ:   resp_byte = inq_byte(i[7:0]);
			R_CAP:   resp_byte = cap_byte(i[7:0]);
			R_SENSE: resp_byte = sense_byte(i[7:0]);
			R_MODE:  resp_byte = mode_byte(i[7:0]);
		endcase
	end
endfunction

//----------------------------------------------------------------------------
// FIFO helpers
//----------------------------------------------------------------------------

integer i;

task automatic fifo_clear;
	begin
		for (i = 0; i < 16; i = i + 1) fifo[i] <= 8'h00;
		fifo_head <= 8'h00;
		fifoflags <= 0;
	end
endtask

task automatic fifo_pop;
	begin
		if (fifoflags != 0) begin
			for (i = 0; i < 15; i = i + 1) fifo[i] <= fifo[i+1];
			fifo[15] <= 8'h00;
			fifo_head <= fifo[1];
			fifoflags <= fifoflags - 1'd1;
		end
	end
endtask

task automatic fifo_push;
	input [7:0] v;
	begin
		if (fifoflags == 5'd16) begin
			fifo[15] <= v;               // overflow overwrites the top
			status[6] <= 1'b1;           // STAT_GE
		end
		else begin
			fifo[fifoflags[3:0]] <= v;
			if (fifoflags == 0) fifo_head <= v;
			fifoflags <= fifoflags + 1'd1;
		end
	end
endtask

task automatic hard_reset;
	begin
		// esp_reset_hard() + esp_reset_soft() in esp.c
		clockconv <= 8'h02;
		configuration <= configuration & 8'h07;
		fifo_clear;
		syncperiod <= 8'h05;
		syncoffset <= 8'h00;
		status <= status & ~(STAT_INT|STAT_VGC|STAT_PE|STAT_GE|STAT_TC);
		intstatus <= 8'h00;
		mode_dma <= 0;
		counter <= 0;
		seqstep <= 8'h00;
		command0 <= 8'h00;
		xst <= X_IDLE;
		m_req <= 0;
		sd_rd <= 0;
		sd_wr <= 0;
	end
endtask

// stage an interrupt: status bits are set now, STAT_INT after the delay
task automatic esp_irq;
	input [24:0] us;
	begin
		dly_us <= us;
		xst <= X_INT_WAIT;
	end
endtask

// scsi_read_sector() entry
task automatic read_sector;
	input [1:0] ret;
	begin
		rd_ret <= ret;
		xst <= X_RD_SECT;
	end
endtask

// selection timeout in microseconds:
// (selecttimeout * 8192 * clockconv) / ESP_CLOCK_FREQ(20) ~ timeout*conv*410
wire [24:0] seltout_us = selecttimeout * clockconv * 25'd410;

//----------------------------------------------------------------------------
// main engine
//----------------------------------------------------------------------------

wire [7:0] csr_or = (be[1] ? wdata[15:8] : 8'h00) | (be[0] ? wdata[7:0] : 8'h00);

// LUN of the current command: from the identify message when present,
// else from the CDB
wire [2:0] cmd_lun = sel_atn ? t_lun : cdb1[7:5];

task automatic reg_write;
	input [5:0] a;
	input [7:0] v;
	begin
		case (a)
			6'h00: wr_tcl <= v;
			6'h01: wr_tch <= v;
			6'h02: fifo_push(v);
			6'h03: begin
				// esp_command_write()/esp_start_command()
				command0 <= v;
				if (v[7]) begin
					counter <= ({wr_tch, wr_tcl} == 16'd0) ? 17'h10000
					                                       : {1'b0, wr_tch, wr_tcl};
					status[4] <= 1'b0;   // clear STAT_TC
					mode_dma <= 1;
				end
				else mode_dma <= 0;
				case (v & 8'h7F)
					7'h00: ;             // NOP
					7'h01: fifo_clear;   // flush FIFO
					7'h02: hard_reset;   // reset chip
					7'h03: begin         // reset SCSI bus
						mode_dma <= 0;
						counter <= 0;
						seqstep <= 8'h00;
						xst <= X_IDLE;
						m_req <= 0;
						sd_rd <= 0;
						sd_wr <= 0;
						if (!(configuration & 8'h40)) begin  // !CFG1_RESREPT
							intstatus <= INTR_RST;
							phase <= PHASE_MI;
							dly_us <= 25'd500;
							xst <= X_INT_WAIT;
						end
					end
					7'h10: begin         // transfer information
						if (v[7]) begin
							pad_mode <= 0;
							word_cnt <= 0;
							gap_us <= SECTOR_US;
							xst <= (phase == PHASE_DI) ? X_DI_CHK :
							       (phase == PHASE_DO) ? X_DO_CHK : X_IDLE;
						end
						else if (phase == PHASE_DI) xst <= X_PIO_RD;
						else if (phase == PHASE_MI) begin
							dly_us <= 25'd1;
							xst <= X_INT_WAIT;
						end
					end
					7'h11: xst <= X_ICCS1;    // initiator command complete
					7'h12: begin              // message accepted
						phase <= PHASE_ST;
						intstatus <= INTR_BS;
						dly_us <= ESP_DELAY_US;
						xst <= X_INT_WAIT;
					end
					7'h18: begin              // transfer pad
						pad_mode <= 1;
						word_cnt <= 0;
						xst <= (phase == PHASE_DI) ? X_DI_CHK :
						       (phase == PHASE_DO) ? X_DO_CHK : X_IDLE;
					end
					7'h41, 7'h42: begin       // select without/with ATN
						// a select aborts whatever transfer state was
						// left behind, including an in-flight memory
						// request
						m_req <= 0;
						sd_rd <= 0;
						sd_wr <= 0;
						seqstep <= 8'h00;
						sel_atn <= v[1];
						cdb_n <= 0;
						if (!disk_present || (selectbusid[2:0] != 3'd0)) begin
							// no disk at this target: selection timeout
							intstatus <= INTR_DC;
							phase <= PHASE_ST;
							dly_us <= seltout_us;
							xst <= X_INT_WAIT;
						end
						else xst <= v[1] ? X_SEL_MSG : X_SEL_CDB;
					end
					7'h44: ;             // enable selection: no reselections
					default: begin       // unimplemented: illegal command
						intstatus <= intstatus | INTR_ILL;
						dly_us <= 25'd20;
						xst <= X_INT_WAIT;
					end
				endcase
			end
			6'h04: selectbusid <= v;
			6'h05: selecttimeout <= v;
			6'h06: syncperiod <= v;
			6'h07: syncoffset <= v;
			6'h08: configuration <= v;
			6'h09: clockconv <= v;
			6'h0A: ;                     // test register
			6'h20: begin
				dma_control <= v;
				if (v[1]) hard_reset;    // ESPCTRL_RESET
				// ESPCTRL_FLUSH: residual bytes are written with byte
				// enables at the end of the transfer, nothing to do
			end
			6'h21: dma_status <= v;
			default: ;
		endcase
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		fifo_clear;
		wr_tcl <= 0; wr_tch <= 0;
		counter <= 0;
		command0 <= 0;
		status <= 0;
		intstatus <= 0;
		seqstep <= 0;
		syncperiod <= 8'h05;
		syncoffset <= 0;
		configuration <= 0;
		conf2 <= 0;
		clockconv <= 8'h02;
		selectbusid <= 0;
		selecttimeout <= 0;
		dma_control <= 0;
		dma_status <= 0;
		mode_dma <= 0;
		phase <= PHASE_DO;
		t_status <= 0; t_message <= 0; t_lun <= 0;
		sense_code <= 0; sense_key <= 0; sense_valid <= 0; sense_info <= 0;
		lba <= 0; blockcounter <= 0;
		d_csr <= 0;
		d_next <= 0; d_limit <= 0; d_start <= 0; d_stop <= 0;
		s_next <= 0; s_limit <= 0; s_start <= 0; s_stop <= 0;
		buf_pos <= 0; buf_limit <= 0; buf_disk <= 0;
		fill_idx <= 0; fill_kind <= R_INQ;
		ms_page <= 0; ms_ctl <= 0; ms_hdr <= 0; ms_total <= 0;
		xst <= X_IDLE;
		sel_atn <= 0;
		cdb0 <= 0; cdb1 <= 0; cdb2 <= 0; cdb3 <= 0; cdb4 <= 0;
		cdb5 <= 0; cdb6 <= 0; cdb7 <= 0; cdb8 <= 0; cdb9 <= 0;
		cdb_n <= 0;
		rd_ret <= 0; sd_ret <= 0; pad_mode <= 0;
		gap_us <= 0;
		flp_active <= 0;
		flp_req_d <= 0;
		flp_pend <= 0;
		flp_done <= 0;
		word_cnt <= 0; word_buf <= 0; do_rem <= 0;
		dly_us <= 0;
		sd_lba_r <= 0;
		sd_rd <= 0; sd_wr <= 0;
		m_req <= 0; m_we <= 0; m_addr <= 0; m_be <= 0; m_din <= 0;
		eng_we <= 0; eng_addr <= 0; eng_wd <= 0;
		tickcnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;
		eng_we <= 0;
		flp_done <= 0;
		if (tick && gap_us != 0) gap_us <= gap_us - 1'd1;

		// The floppy holds its request up until it sees the sector
		// completed, so a level test would restart the channel on the
		// buffer it has just handed over.  An edge test is worse: an
		// edge arriving before the channel is switched over, or while
		// the ESP has it, is consumed and never comes again, and the
		// drive waits for a completion that cannot happen.  Latch the
		// request instead and clear it when the sector is taken.
		if (flp_req && !flp_req_d) flp_pend <= 1;
		flp_req_d <= flp_req;
		if (!flp_req) flp_pend <= 0;

		if (flp_select && flp_pend && !flp_active && (xst == X_IDLE)) begin
			flp_pend <= 0;
			flp_active <= 1;
			buf_pos    <= 0;
			buf_limit  <= flp_len[9:0];
			buf_disk   <= 0;
			word_cnt   <= 0;
			pad_mode   <= 0;
			counter    <= {6'd0, flp_len};
			phase      <= flp_wr ? PHASE_DI : PHASE_DO;
			xst        <= flp_wr ? X_DI_CHK : X_DO_CHK;
		end

		//------------------------------------------------------------
		// execution engine
		//------------------------------------------------------------
		case (xst)
		X_IDLE: ;

		// select: pop the identify message
		X_SEL_MSG: begin
			phase <= PHASE_MO;
			seqstep <= 8'h01;
			if (fifoflags != 0) begin
				t_lun <= fifo_head[2:0];
				fifo_pop;
			end
			else t_lun <= 0;
			xst <= X_SEL_CDB;
		end

		// select: pop the whole CDB from the FIFO
		X_SEL_CDB: begin
			phase <= PHASE_CD;
			seqstep <= 8'h03;
			if (!sel_atn) t_lun <= 0;
			if (fifoflags != 0) begin
				case (cdb_n)
					4'd0: cdb0 <= fifo_head;
					4'd1: cdb1 <= fifo_head;
					4'd2: cdb2 <= fifo_head;
					4'd3: cdb3 <= fifo_head;
					4'd4: cdb4 <= fifo_head;
					4'd5: cdb5 <= fifo_head;
					4'd6: cdb6 <= fifo_head;
					4'd7: cdb7 <= fifo_head;
					4'd8: cdb8 <= fifo_head;
					4'd9: cdb9 <= fifo_head;
					default: ;
				endcase
				fifo_pop;
				if (cdb_n != 4'd15) cdb_n <= cdb_n + 1'd1;
			end
			else xst <= X_DISPATCH;
		end

		// SCSI_Emulate_Command()
		X_DISPATCH: begin : dispatch
			reg [15:0] cnt6;
			reg [15:0] cnt10;
			cnt6  = (cdb4 == 0) ? 16'h0100 : {8'd0, cdb4};
			cnt10 = {cdb7, cdb8};
			// without an identify message the LUN comes from the CDB
			if (!sel_atn) t_lun <= cdb1[7:5];
			t_message <= 8'h00;              // MSG_COMPLETE
			xst <= X_POSTCMD;
			case (cdb0)
				8'h12: begin                 // INQUIRY (lun independent)
					fill_kind <= R_INQ;
					buf_limit <= (cdb4 > 8'd54) ? 10'd54 : {2'd0, cdb4};
					buf_pos <= 0;
					buf_disk <= 0;
					fill_idx <= 0;
					t_status <= STAT_GOOD;
					phase <= PHASE_DI;
					sense_code <= SC_NO_ERROR;
					sense_valid <= 0;
					xst <= X_FILL;
				end
				8'h03: begin                 // REQUEST SENSE (lun independent)
					fill_kind <= R_SENSE;
					buf_limit <= (cdb4 == 0) ? 10'd22 :
					             (cdb4 > 8'd22) ? 10'd22 : {2'd0, cdb4};
					buf_pos <= 0;
					buf_disk <= 0;
					fill_idx <= 0;
					sense_key <= key_of(sense_code);
					t_status <= STAT_GOOD;
					phase <= PHASE_DI;
					xst <= X_FILL;
				end
				default: begin
					if (cmd_lun != 3'd0) begin
						t_status <= STAT_CHECK_COND;
						sense_code <= SC_INVALID_LUN;
						sense_valid <= 0;
						phase <= PHASE_ST;
					end
					else case (cdb0)
						8'h00: begin         // TEST UNIT READY
							t_status <= STAT_GOOD;
							sense_code <= SC_NO_ERROR;
							phase <= PHASE_ST;
						end
						8'h25: begin         // READ CAPACITY
							fill_kind <= R_CAP;
							buf_limit <= 10'd8;
							buf_pos <= 0;
							buf_disk <= 0;
							fill_idx <= 0;
							t_status <= STAT_GOOD;
							phase <= PHASE_DI;
							sense_code <= SC_NO_ERROR;
							sense_valid <= 0;
							xst <= X_FILL;
						end
						8'h08, 8'h28: begin  // READ (6) / READ (10)
							lba <= (cdb0 == 8'h08) ? {11'd0, cdb1[4:0], cdb2, cdb3}
							                       : {cdb2, cdb3, cdb4, cdb5};
							blockcounter <= (cdb0 == 8'h08) ? cnt6 : cnt10;
							buf_disk <= 1;
							buf_pos <= 0;
							buf_limit <= 0;
							phase <= PHASE_DI;
							read_sector(2'd0);
						end
						8'h0A, 8'h2A: begin  // WRITE (6) / WRITE (10)
							lba <= (cdb0 == 8'h0A) ? {11'd0, cdb1[4:0], cdb2, cdb3}
							                       : {cdb2, cdb3, cdb4, cdb5};
							blockcounter <= (cdb0 == 8'h0A) ? cnt6 : cnt10;
							if (disk_ro) begin
								t_status <= STAT_CHECK_COND;
								sense_code <= SC_WRITE_PROTECT;
								sense_valid <= 0;
								phase <= PHASE_ST;
							end
							else begin
								buf_disk <= 1;
								buf_pos <= 0;
								buf_limit <= 10'd512;
								phase <= PHASE_DO;
							end
						end
						8'h1A: begin         // MODE SENSE
							fill_kind <= R_MODE;
							ms_page <= cdb2[5:0] == 6'h3F ? 8'h3F : {2'd0, cdb2[5:0]};
							ms_ctl <= cdb2[3:2];   // quirk of the reference
							ms_hdr <= cdb1[3] ? 5'd4 : 5'd12;
							ms_total <= (cdb1[3] ? 8'd4 : 8'd12)
							          + page_len({2'd0, cdb2[5:0]});
							buf_limit <= {2'd0, cdb4};
							buf_pos <= 0;
							buf_disk <= 0;
							fill_idx <= 0;
							t_status <= STAT_GOOD;
							phase <= PHASE_DI;
							sense_code <= SC_NO_ERROR;
							sense_valid <= 0;
							xst <= X_FILL;
						end
						8'h1B: begin         // START/STOP (ship)
							t_status <= STAT_GOOD;
							phase <= PHASE_ST;
						end
						8'h04: begin         // FORMAT DRIVE
							t_status <= STAT_GOOD;
							phase <= PHASE_ST;
						end
						default: begin       // unknown command
							t_status <= STAT_CHECK_COND;
							sense_code <= SC_INVALID_CMD;
							sense_valid <= 0;
							phase <= PHASE_ST;
						end
					endcase
				end
			endcase
		end

		// fill the buffer with a response table
		X_FILL: begin
			if ({1'b0, fill_idx} < buf_limit) begin
				eng_we <= 1;
				eng_addr <= fill_idx;
				eng_wd <= resp_byte(fill_kind, {1'b0, fill_idx});
				fill_idx <= fill_idx + 1'd1;
			end
			else begin
				// the total length is clamped to the allocation; a zero
				// length answer goes straight to status phase
				if (fill_kind == R_MODE && buf_limit > {2'd0, ms_total})
					buf_limit <= {2'd0, ms_total};
				if (buf_limit == 0) phase <= PHASE_ST;
				xst <= X_POSTCMD;
			end
		end

		// select done: function complete plus bus service
		X_POSTCMD: begin
			seqstep <= 8'h04;
			intstatus <= INTR_BS | INTR_FC;
			dly_us <= ESP_DELAY_US;
			xst <= X_INT_WAIT;
		end

		X_INT_WAIT: begin
			if (dly_us == 0) begin
				status[7] <= 1'b1;
				xst <= X_IDLE;
			end
			else if (tick) dly_us <= dly_us - 1'd1;
		end

		// scsi_read_sector()
		X_RD_SECT: begin
			if (blockcounter == 0) begin
				phase <= PHASE_ST;
				xst <= (rd_ret == 2'd1) ? X_DI_CHK :
				       (rd_ret == 2'd2) ? X_PIO_RD : X_POSTCMD;
			end
			else if (lba < img_blocks) begin
				sd_lba_r <= lba;
				sd_ret <= 0;
				xst <= X_SD_RD_GO;
			end
			else begin
				t_status <= STAT_CHECK_COND;
				sense_code <= SC_INVALID_LBA;
				sense_valid <= 1;
				sense_info <= lba;
				phase <= PHASE_ST;
				xst <= (rd_ret == 2'd1) ? X_DI_CHK :
				       (rd_ret == 2'd2) ? X_PIO_RD : X_POSTCMD;
			end
		end

		X_SD_RD_GO: begin
			sd_rd <= 1;
			if (sd_ack) begin
				sd_rd <= 0;
				xst <= X_SD_RD_ACK;
			end
		end

		X_SD_RD_ACK: begin
			if (!sd_ack) begin
				buf_pos <= 0;
				buf_limit <= 10'd512;
				t_status <= STAT_GOOD;
				sense_code <= SC_NO_ERROR;
				sense_valid <= 0;
				lba <= lba + 1'd1;
				blockcounter <= blockcounter - 1'd1;
				xst <= (rd_ret == 2'd1) ? X_DI_CHK :
				       (rd_ret == 2'd2) ? X_PIO_RD : X_POSTCMD;
			end
		end

		// scsi_write_sector()
		X_WR_SECT: begin
			if (lba < img_blocks) begin
				sd_lba_r <= lba;
				xst <= X_SD_WR_GO;
			end
			else begin
				t_status <= STAT_CHECK_COND;
				sense_code <= SC_INVALID_LBA;
				sense_valid <= 1;
				sense_info <= lba;
				phase <= PHASE_ST;
				xst <= X_DO_CHK;
			end
		end

		X_SD_WR_GO: begin
			sd_wr <= 1;
			if (sd_ack) begin
				sd_wr <= 0;
				xst <= X_SD_WR_ACK;
			end
		end

		X_SD_WR_ACK: begin
			if (!sd_ack) begin
				buf_pos <= 0;
				t_status <= STAT_GOOD;
				sense_code <= SC_NO_ERROR;
				sense_valid <= 0;
				lba <= lba + 1'd1;
				blockcounter <= blockcounter - 1'd1;
				if (blockcounter == 16'd1) phase <= PHASE_ST;
				xst <= X_DO_PUT;    // drain the rest of the DMA word
			end
		end

		//------------------------------------------------------------
		// transfer info, data in (disk to memory)
		//------------------------------------------------------------
		X_DI_CHK: begin
			// a floppy sector ends by handing the channel back
			if (flp_active && (buf_pos >= buf_limit) && (word_cnt == 0)) begin
				flp_active <= 0;
				flp_done   <= 1;
				xst        <= X_IDLE;
			end
			else if (flp_active && (buf_pos >= buf_limit)) xst <= X_DI_WR;
			// esp_transfer_done(): counter end first, then phase change
			else if (counter == 0) begin
				intstatus <= INTR_FC;
				status[4] <= 1'b1;       // STAT_TC
				if (word_cnt != 0) xst <= X_DI_WR;
				else esp_irq(ESP_DELAY_US);
			end
			else if (phase != PHASE_DI) begin
				intstatus <= INTR_BS;
				if (word_cnt != 0) xst <= X_DI_WR;
				else esp_irq(ESP_DELAY_US);
			end
			else if (buf_pos >= buf_limit) begin
				if (buf_disk) read_sector(2'd1);
				else phase <= PHASE_ST;  // response drained
			end
			else begin
				eng_addr <= buf_pos[8:0];
				xst <= X_DI_RD;
			end
		end

		X_DI_RD: xst <= X_DI_GET;        // buffer read settles

		X_DI_GET: begin
			buf_pos <= buf_pos + 1'd1;
			counter <= counter - 1'd1;
			case (word_cnt)
				2'd0: word_buf[31:24] <= eng_q;
				2'd1: word_buf[23:16] <= eng_q;
				2'd2: word_buf[15:8]  <= eng_q;
				2'd3: word_buf[7:0]   <= eng_q;
			endcase
			word_cnt <= word_cnt + 1'd1;
			if (!pad_mode && word_cnt == 2'd3) xst <= X_DI_WR;
			else begin
				if (pad_mode) word_cnt <= 0;
				xst <= X_DI_CHK;
			end
		end

		// write the gathered word to memory; a partial word at the end
		// of the transfer goes out with byte enables
		X_DI_WR: begin
			if (!m_req) begin
				if (d_csr[0] && d_next < d_limit && gap_us == 0) begin
					m_req <= 1;
					m_we <= 1;
					m_addr <= d_next[25:2];
					m_be <= (word_cnt == 2'd0) ? 4'b1111 :
					        (word_cnt == 2'd1) ? 4'b1000 :
					        (word_cnt == 2'd2) ? 4'b1100 : 4'b1110;
					m_din <= word_buf;
				end
				// channel disabled or limit reached: stall until the
				// CPU re-arms the channel (or resets the chip)
			end
			else if (m_ack) begin
				m_req <= 0;
				// A limit that is not a whole number of words above next
				// is a programming error - the reference aborts on it - but
				// it must not leave next above limit, because the driver
				// subtracts the two to count the bytes moved and calls the
				// difference a buffer overflow.  Stop on the limit instead.
				d_next <= (d_next + 3'd4 > d_limit) ? d_limit : (d_next + 3'd4);
				word_cnt <= 0;
				// dma_interrupt(CHANNEL_SCSI): the pause before data
				// flows into the swapped buffer is the driver's window
				// to program the next chain segment
				if (d_next + 3'd4 >= d_limit) begin
					d_csr[3] <= 1;
					gap_us <= GAP_US;
					if (d_csr[1]) begin
						d_next <= d_start;
						d_limit <= d_stop;
						d_csr[1] <= 0;
					end
					else d_csr[0] <= 0;
				end
				// back to the check: it stages the completion
				// interrupt (or the next word)
				xst <= X_DI_CHK;
			end
		end

		//------------------------------------------------------------
		// transfer info, data out (memory to disk)
		//------------------------------------------------------------
		X_DO_CHK: begin
			if (flp_active && (buf_pos >= buf_limit)) begin
				flp_active <= 0;
				flp_done   <= 1;
				xst        <= X_IDLE;
			end
			else if (counter == 0) begin
				intstatus <= INTR_FC;
				status[4] <= 1'b1;       // STAT_TC
				esp_irq(ESP_DELAY_US);
			end
			else if (phase != PHASE_DO) begin
				intstatus <= INTR_BS;
				esp_irq(ESP_DELAY_US);
			end
			else if (pad_mode) begin
				// transfer pad: receive zero bytes
				do_rem <= 3'd1;
				word_buf <= 32'd0;
				xst <= X_DO_PUT;
			end
			else xst <= X_DO_RD;
		end

		X_DO_RD: begin
			if (!m_req) begin
				if (d_csr[0] && d_next < d_limit && gap_us == 0) begin
					m_req <= 1;
					m_we <= 0;
					m_addr <= d_next[25:2];
					m_be <= 4'hF;
				end
			end
			else if (m_ack) begin
				m_req <= 0;
				word_buf <= m_dout;
				do_rem <= 3'd4;
				d_next <= (d_next + 3'd4 > d_limit) ? d_limit : (d_next + 3'd4);
				if (d_next + 3'd4 >= d_limit) begin
					d_csr[3] <= 1;
					gap_us <= GAP_US;
					if (d_csr[1]) begin
						d_next <= d_start;
						d_limit <= d_stop;
						d_csr[1] <= 0;
					end
					else d_csr[0] <= 0;
				end
				xst <= X_DO_PUT;
			end
		end

		X_DO_PUT: begin
			if (do_rem == 0 || counter == 0 || phase != PHASE_DO)
				xst <= X_DO_CHK;
			else begin
				eng_we <= 1;
				eng_addr <= buf_pos[8:0];
				eng_wd <= word_buf[31:24];
				word_buf <= {word_buf[23:0], 8'h00};
				do_rem <= do_rem - 1'd1;
				buf_pos <= buf_pos + 1'd1;
				counter <= counter - 1'd1;
				if (buf_pos + 1'd1 == buf_limit) begin
					if (buf_disk) xst <= X_WR_SECT;
					else begin
						phase <= PHASE_ST;
						xst <= X_DO_CHK;
					end
				end
			end
		end

		//------------------------------------------------------------
		// initiator command complete: status byte, then message byte
		//------------------------------------------------------------
		X_ICCS1: begin
			fifo_push(t_status);
			phase <= PHASE_MI;           // SCSIdisk_Send_Status()
			xst <= X_ICCS2;
		end

		X_ICCS2: begin
			fifo_push(t_message);
			intstatus <= INTR_FC;
			dly_us <= ESP_DELAY_US;
			xst <= X_INT_WAIT;
		end

		//------------------------------------------------------------
		// PIO transfer info, data in: one byte to the FIFO
		//------------------------------------------------------------
		X_PIO_RD: begin
			if (buf_pos >= buf_limit) begin
				if (buf_disk) read_sector(2'd2);
				else begin
					phase <= PHASE_ST;
					esp_irq(25'd1);
				end
			end
			else begin
				eng_addr <= buf_pos[8:0];
				xst <= X_PIO_GET;
			end
		end

		X_PIO_GET: begin
			fifo_push(eng_q);
			buf_pos <= buf_pos + 1'd1;
			if (buf_pos + 1'd1 >= buf_limit && !buf_disk) phase <= PHASE_ST;
			esp_irq(25'd1);
		end

		default: xst <= X_IDLE;
		endcase

		//------------------------------------------------------------
		// register access (after the engine so that resets win)
		//------------------------------------------------------------
		if (sel_esp) begin
			if (we) begin
				if (be[1]) reg_write(a_even, wdata[15:8]);
				if (be[0]) reg_write(a_odd, wdata[7:0]);
			end
			else begin
				// read side effects
				if ((be[1] && a_even == 6'h02) || (be[0] && a_odd == 6'h02))
					fifo_pop;
				if (((be[1] && a_even == 6'h05) || (be[0] && a_odd == 6'h05)) && status[7]) begin
					intstatus <= 8'h00;
					status <= status & ~(STAT_INT|STAT_VGC|STAT_PE|STAT_GE);
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

		if (sel_sptr & we) begin
			case (addr[3:2])
				2'd0: begin if (!addr[1]) begin if (be[1]) s_next[31:24] <= wdata[15:8]; if (be[0]) s_next[23:16] <= wdata[7:0]; end else begin if (be[1]) s_next[15:8] <= wdata[15:8]; if (be[0]) s_next[7:0] <= wdata[7:0]; end end
				2'd1: begin if (!addr[1]) begin if (be[1]) s_limit[31:24] <= wdata[15:8]; if (be[0]) s_limit[23:16] <= wdata[7:0]; end else begin if (be[1]) s_limit[15:8] <= wdata[15:8]; if (be[0]) s_limit[7:0] <= wdata[7:0]; end end
				2'd2: begin if (!addr[1]) begin if (be[1]) s_start[31:24] <= wdata[15:8]; if (be[0]) s_start[23:16] <= wdata[7:0]; end else begin if (be[1]) s_start[15:8] <= wdata[15:8]; if (be[0]) s_start[7:0] <= wdata[7:0]; end end
				2'd3: begin if (!addr[1]) begin if (be[1]) s_stop[31:24] <= wdata[15:8]; if (be[0]) s_stop[23:16] <= wdata[7:0]; end else begin if (be[1]) s_stop[15:8] <= wdata[15:8]; if (be[0]) s_stop[7:0] <= wdata[7:0]; end end
			endcase
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
	end
end

endmodule
