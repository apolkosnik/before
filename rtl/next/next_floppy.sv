//============================================================================
//  NeXT floppy drive: Intel 82077AA controller, modeled on floppy.c of
//  the Previous emulator.
//
//  Registers at 0x02014100:
//    +0 status A (ro)      +1 status B (ro)     +2 digital output
//    +4 main status (ro) / data rate (wo)       +5 data FIFO
//    +7 digital input (ro) / configuration (wo) +8 external control
//
//  A command is written to the FIFO as an opcode plus a fixed number of
//  parameter bytes (cmd_data_size[] in floppy.c); the controller then
//  executes and, for most commands, presents result bytes through the
//  same FIFO with DIO set.  The interrupt is INT_PHONE.
//
//  Sector data does not pass through the FIFO: the drive shares the
//  SCSI DMA channel, which is switched to it by the external control
//  register (CTRL_82077) or by the optical disk's general purpose
//  output.  That is why this module has no memory master of its own -
//  it presents a sector buffer, and next_scsi's channel moves it.
//
//  The medium is a disk image on the MiSTer SD card.  Geometry follows
//  the image size the way floppy.c derives it: 80 cylinders, 2 heads,
//  and sectors per track from the size, so 720K, 1440K and 2880K images
//  all work without being told which they are.
//============================================================================

module next_floppy #(parameter CLK_HZ = 50000000)
(
	input         clk,
	input         reset,

	// register access (0x02014100-0x0201410F)
	input         sel,
	input   [3:0] addr,
	input         we,
	input   [1:0] be,            // [1] = even byte, [0] = odd byte
	input  [15:0] wdata,
	output [15:0] rdata,

	output        int_floppy,    // level, for INT_PHONE
	output        flp_select,    // the DMA channel belongs to the floppy
	input         mo_gpo,        // optical disk general purpose output

	// sector buffer, read and written by the shared DMA channel
	input   [9:0] buf_addr,
	input         buf_we,
	input   [7:0] buf_wdata,
	output  [7:0] buf_q,
	output [10:0] buf_len,       // bytes in the current sector
	output        dma_req,       // a sector is ready / wanted
	output        dma_wr,        // 1 = floppy to memory
	input         dma_done,      // the channel moved buf_len bytes

	// MiSTer SD block interface (image slot 1)
	input   [1:0] img_mounted,     // one per drive (fd0, fd1)
	input         img_readonly,
	input  [63:0] img_size,
	output        sd_unit,         // which drive's slot sd_* addresses
	output [31:0] sd_lba,
	output reg    sd_rd,
	output reg    sd_wr,
	input         sd_ack,
	input   [8:0] sd_buff_addr,
	input   [7:0] sd_buff_dout,
	output  [7:0] sd_buff_din,
	input         sd_buff_wr
);

//----------------------------------------------------------------------------
// registers
//----------------------------------------------------------------------------

localparam SRA_INT     = 8'h80, SRA_DRV1_N = 8'h40, SRA_TRK0_N = 8'h10,
           SRA_WP_N    = 8'h02;
localparam STAT_RQM    = 8'h80, STAT_DIO   = 8'h40, STAT_NONDMA = 8'h20,
           STAT_CMDBSY = 8'h10;
localparam CTRL_EJECT  = 8'h80, CTRL_82077 = 8'h40, CTRL_DRV_ID = 8'h04;

localparam IC_NORMAL   = 8'h00, IC_ABNORMAL = 8'h40, IC_INV_CMD = 8'h80;
localparam ST0_SE      = 8'h20;
localparam ST1_EN      = 8'h80, ST1_OR      = 8'h10,
           ST1_ND      = 8'h04, ST1_NW      = 8'h02;
localparam ST2_WC      = 8'h10;
localparam ST3_WP      = 8'h40, ST3_T0      = 8'h10,
           ST3_HD      = 8'h04;

reg  [7:0] dor;              // digital output
reg  [7:0] msr;              // main status
reg  [7:0] dsr;              // data rate select (write-only)
reg  [7:0] ccr;              // configuration control (data rate)
// The external control register is not one register.  Writing it is a
// command - select the DMA channel, eject - and FLP_Select_Write never
// stores the value.  Reading it returns status that floppy_dor_write
// maintains: whether a drive is connected, and what medium is in it.
// Keeping one register for both let a channel-select write erase the
// media ID and leave the ROM seeing an empty drive.
reg        sel_82077;        // written at +8, selects the DMA channel
reg  [7:0] ctrl_st = 8'h00;  // read at +8: drive and media status
reg  [7:0] st0, st1, st2;
reg  [7:0] pcn_v [0:1];      // present cylinder, per drive
reg        eis;              // implied seek enabled
reg        int_pend;

reg  [7:0] cyl_v [0:1];
reg  [7:0] head_v [0:1];
reg  [7:0] sector_v [0:1];
reg  [2:0] blocksize_v [0:1]; // 0x80 << blocksize
reg        io_ds;             // command-selected drive during a transfer

// The controller takes two drives, as the reference does
// (FLP_MAX_DRIVES 2).  The DOR's drive select says which one the
// registers, the medium and the sector addressing refer to, so
// per-drive state behind a selected view leaves their users unchanged.
reg  [1:0] present_v = 2'b00; // an image is mounted
// There is no separate drive-present input on this interface.  Drive 0 is
// fitted by default; a valid mount proves that the corresponding optional
// drive is fitted, and ejecting its removable medium does not undo that.
reg  [1:0] connected_v = 2'b01;
reg  [1:0] readonly_v = 2'b00;
reg  [1:0] spinning_v = 2'b00;
reg [31:0] img_bytes_v [0:1];
reg  [7:0] spt_v [0:1];      // sectors per track, from the image size

wire        ds        = dor[0];          // DOR drive select
wire  [7:0] pcn       = pcn_v[ds];
wire  [7:0] cyl       = cyl_v[ds];
wire  [7:0] head      = head_v[ds];
wire  [7:0] sector    = sector_v[ds];
wire  [2:0] blocksize = blocksize_v[ds];
wire        present   = present_v[ds];
wire        readonly  = readonly_v[ds];
wire [31:0] img_bytes = img_bytes_v[ds];
wire  [7:0] spt       = spt_v[ds];
assign      sd_unit   = io_ds;

assign int_floppy = int_pend;
assign flp_select = sel_82077 | mo_gpo;    // CTRL_82077 or the MO's GPO

wire [7:0] sra = (int_pend ? SRA_INT : 8'h00) |
                 (connected_v[1] ? 8'h00 : SRA_DRV1_N) |
                 ((cyl == 0) ? 8'h00 : SRA_TRK0_N) |
                 (readonly ? 8'h00 : SRA_WP_N);
wire [7:0] srb = 8'hC0;

// The drive and its medium are reported through the NeXT external
// control register, not through anything in the 82077: bit 2 clear
// says a drive is connected, and the low two bits carry the media ID
// that floppy_dor_write() publishes when the motor is turned on.
// Leaving them at zero is what "No Floppy Disk Present" means.
localparam [1:0] MEDIA_NONE = 2'd0, MEDIA_2880 = 2'd1,
                 MEDIA_1440 = 2'd2, MEDIA_720  = 2'd3;

// The drive is a fixture of the machine; the medium comes and goes.
// The reference keeps them apart - connected is configuration, and
// inserted is whether there is a disk in it - and reports "no drive"
// only for a drive that is not there at all.  Drive 0 is always there.
wire [1:0] drv_connected = connected_v;

// What this register reports is physical: the drive is either fitted
// or it is not, and the media ID comes off the disk's density sense
// holes.  Neither depends on the motor, or on when the driver last
// wrote the DOR - the reference recomputes it only on that write, and
// flags its own doubt about clearing the ID when the motor stops.
// Reporting the drive as it is removes the ordering entirely: a disk
// put in is seen by the next read, whenever that comes.
wire [7:0] ctrl_rd = {ctrl_st[7:3], ~drv_connected[ds], media_of(ds)};

function [1:0] media_of;
	input d;
	media_of = !present_v[d]        ? MEDIA_NONE :
	           (spt_v[d] == 8'd9)   ? MEDIA_720  :
	           (spt_v[d] == 8'd36)  ? MEDIA_2880 : MEDIA_1440;
endfunction

function rate_ok;
	input d;
	begin
		rate_ok = ((spt_v[d] == 8'd9)  && (ccr[1:0] == 2'b10)) ||
		          ((spt_v[d] == 8'd18) && (ccr[1:0] == 2'b00)) ||
		          ((spt_v[d] == 8'd36) && (ccr[1:0] == 2'b11));
	end
endfunction
wire [7:0] dir = present ? 8'h00 : 8'h80;  // disk change when empty

//----------------------------------------------------------------------------
// command and result FIFO
//----------------------------------------------------------------------------

reg  [7:0] cmd;
reg  [7:0] cmd_data [0:8];
reg  [3:0] cmd_want;         // parameter bytes still expected
reg  [3:0] cmd_got;
reg        cmd_phase;

reg  [7:0] res [0:6];
reg  [3:0] res_size;
reg  [3:0] res_pos;

// parameter counts, cmd_data_size[] in floppy.c
function automatic [3:0] cmd_bytes;
	input [4:0] op;
	begin
		case (op)
			5'h02: cmd_bytes = 4'd8;   // read track
			5'h03: cmd_bytes = 4'd2;   // specify
			5'h04: cmd_bytes = 4'd1;   // drive status
			5'h05: cmd_bytes = 4'd8;   // write
			5'h06: cmd_bytes = 4'd8;   // read
			5'h07: cmd_bytes = 4'd1;   // recalibrate
			5'h09: cmd_bytes = 4'd8;   // write deleted
			5'h0A: cmd_bytes = 4'd1;   // read id
			5'h0C: cmd_bytes = 4'd8;   // read deleted
			5'h0D: cmd_bytes = 4'd5;   // format
			5'h0F: cmd_bytes = 4'd2;   // seek
			5'h11: cmd_bytes = 4'd8;   // scan equal
			5'h12: cmd_bytes = 4'd1;   // perpendicular
			5'h13: cmd_bytes = 4'd3;   // configure
			5'h16: cmd_bytes = 4'd8;   // verify
			5'h19: cmd_bytes = 4'd8;   // scan low or equal
			5'h1D: cmd_bytes = 4'd8;   // scan high or equal
			default: cmd_bytes = 4'd0; // interrupt status, version, invalid
		endcase
	end
endfunction

//----------------------------------------------------------------------------
// register read
//----------------------------------------------------------------------------

wire [3:0] a_even = {addr[3:1], 1'b0};
wire [3:0] a_odd  = {addr[3:1], 1'b1};
// floppy_dor_read() discards the motor bits in the last byte written and
// reconstructs them from each drive's independent spinning flag.
wire [7:0] dor_rd  = {2'b00, spinning_v[1], spinning_v[0], dor[3:0]};
wire [7:0] fifo_rd = (res_size != 0) ? res[0] : 8'h00;

`define FLP_READ(a) ( \
	((a) == 4'h0) ? sra : \
	((a) == 4'h1) ? srb : \
	((a) == 4'h2) ? dor_rd : \
	((a) == 4'h4) ? msr : \
	((a) == 4'h5) ? fifo_rd : \
	((a) == 4'h7) ? dir : \
	((a) == 4'h8) ? ctrl_rd : 8'h00 )

assign rdata = {`FLP_READ(a_even), `FLP_READ(a_odd)};

//----------------------------------------------------------------------------
// sector buffer, shared with the DMA channel and the SD card
//----------------------------------------------------------------------------

reg  [7:0] sbuf [0:1023];
reg  [7:0] sbuf_q;
reg        sd_rd_act;

// hps_io's block-write strobe is pipelined and can remain asserted for the
// final byte after sd_ack falls.  sd_rd_act is ownership latched from this
// drive's ack, so it admits that tail without accepting another slot's data.
wire        in_sd_read = ((est == E_RD_GO) || (est == E_RD_ACK)) &&
                         (sd_rd_act || sd_ack);
wire        in_sd  = (sd_rd_act | sd_rd | sd_wr | sd_ack);
wire  [9:0] s_addr = in_sd ? {1'b0, sd_buff_addr} : buf_addr;
wire        s_we   = in_sd ? (sd_buff_wr & in_sd_read) : buf_we;
wire  [7:0] s_wd   = in_sd ? sd_buff_dout : buf_wdata;

assign buf_q       = sbuf_q;
assign sd_buff_din = sbuf_q;

//----------------------------------------------------------------------------
// geometry: 80 cylinders, 2 heads, sectors per track from the size
//----------------------------------------------------------------------------

localparam CYLS = 8'd80, HEADS = 8'd2;

wire [10:0] sec_size = 11'd128 << blocksize_v[io_ds];
wire [31:0] lba = (({24'd0, cyl_v[io_ds]} * HEADS) +
                   {24'd0, head_v[io_ds]}) * {24'd0, spt_v[io_ds]}
                  + {24'd0, sector_v[io_ds]} - 32'd1;

assign sd_lba  = lba;

//----------------------------------------------------------------------------
// execution engine
//----------------------------------------------------------------------------

localparam E_SEEKINT = 4'd11;   // a seek in progress, drive busy
localparam E_IDLE  = 4'd0,  E_EXEC   = 4'd1,  E_RESULT = 4'd2,
           E_RD_GO = 4'd3,  E_RD_ACK = 4'd4,  E_XFER   = 4'd5,
           E_WR_GO = 4'd6,  E_WR_ACK = 4'd7,  E_NEXT   = 4'd8,
           E_INT   = 4'd9,  E_SEEKDLY= 4'd10,
           E_FMT_ZERO = 4'd12, E_RESETPOLL = 4'd13;
reg  [3:0] est;

reg  [7:0] sec_left;         // sectors still to transfer
reg        is_write;
reg        format_mode;
reg  [9:0] zero_pos;
reg [24:0] dly;

// sbuf has one procedural writer.  FORMAT's zero fill is selected ahead of
// the otherwise shared SD/DMA write port while the engine owns the buffer.
wire fmt_zero_we = (est == E_FMT_ZERO);
always @(posedge clk) begin
	if (fmt_zero_we) sbuf[zero_pos] <= 8'h00;
	else if (s_we) sbuf[s_addr] <= s_wd;
	sbuf_q <= sbuf[s_addr];
end

reg        dma_req_r, dma_wr_r;
assign dma_req = dma_req_r;
assign dma_wr  = dma_wr_r;
assign buf_len = (format_mode && (est == E_XFER)) ? 11'd4 : sec_size;

localparam TICK = CLK_HZ / 1000000;
localparam DMA_TIMEOUT_US = 25'd100000;
localparam RESET_POLL_US = 25'd1000000;
localparam TICK_W = (TICK <= 1) ? 1 : $clog2(TICK);
reg [TICK_W-1:0] tickcnt;
wire tick = (tickcnt == TICK-1);

integer i;

task automatic finish_int;
	begin
		int_pend <= 1;
		msr <= STAT_RQM | STAT_DIO;
		est <= E_IDLE;
	end
endtask

// 82077 reset paths (DOR RESET_N, DSR RESET and NeXT CTRL_RESET) all
// reset the controller, but do not eject or otherwise alter the medium.
task automatic controller_reset;
	begin
		msr <= STAT_RQM;
		st0 <= 0; st1 <= 0; st2 <= 0;
		pcn_v[0] <= 0; pcn_v[1] <= 0;
		eis <= 0;
		int_pend <= 0;
		cmd_want <= 0; cmd_got <= 0; cmd_phase <= 0;
		res_size <= 0; res_pos <= 0;
		est <= E_IDLE;
		sd_rd <= 0; sd_wr <= 0; sd_rd_act <= 0;
		dma_req_r <= 0; dma_wr_r <= 0;
		sec_left <= 0;
		format_mode <= 0;
	end
endtask

// The C model has one EVENT_FLOPPY_IO slot.  A command which completes with
// its own interrupt replaces a pending reset poll.  Those commands complete
// immediately here, so explicitly retire the engine's reset-poll state.
task automatic cancel_reset_poll;
	begin
		if (est == E_RESETPOLL) begin
			dly <= 0;
			est <= E_IDLE;
		end
	end
endtask

// the seven byte result of a read or write
task automatic rw_result;
	begin
		res[0] <= st0 | {5'd0, head_v[io_ds][0], 1'b0, io_ds};
		res[1] <= st1;
		res[2] <= st2;
		res[3] <= cyl_v[io_ds];
		res[4] <= head_v[io_ds];
		res[5] <= sector_v[io_ds];
		res[6] <= {5'd0, blocksize_v[io_ds]};
		res_size <= 4'd7;
		res_pos <= 0;
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		dor <= 8'h00;
		spinning_v <= 2'b00;
		msr <= STAT_RQM;
		dsr <= 8'h00;
		ccr <= 8'h00;
		sel_82077 <= 1'b0;
		// flp.ctrl is not touched by a controller reset in the
		// reference, and the 68040's RESET instruction reaches every
		// device here - clearing it there loses the medium every time
		// the ROM resets the machine.  It powers up at zero: drive
		// connected, nothing in it.
		st0 <= 0; st1 <= 0; st2 <= 0;
		pcn_v[0] <= 0;
		pcn_v[1] <= 0;
		eis <= 0;
		int_pend <= 0;
		cyl_v[0] <= 0; cyl_v[1] <= 0;
		head_v[0] <= 0; head_v[1] <= 0;
		sector_v[0] <= 1; sector_v[1] <= 1;
		// Controller reset does not change an inserted medium.  Empty or
		// ejected drives retain N=0, as Floppy_Eject() leaves them.
		blocksize_v[0] <= present_v[0] ? blocksize_v[0] : 3'd0;
		blocksize_v[1] <= present_v[1] ? blocksize_v[1] : 3'd0;
		io_ds <= 0;
		cmd <= 0; cmd_want <= 0; cmd_got <= 0; cmd_phase <= 0;
		res_size <= 0; res_pos <= 0;
		est <= E_IDLE;
		sd_rd <= 0; sd_wr <= 0; sd_rd_act <= 0;
		sec_left <= 0; is_write <= 0;
		format_mode <= 0; zero_pos <= 0;
		dma_req_r <= 0; dma_wr_r <= 0;
		dly <= 0;
		tickcnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;

		//------------------------------------------------------------
		// medium
		//------------------------------------------------------------
		if (img_mounted[0]) begin
			// Only the three formats the drive takes are a medium at
			// all; anything else has no geometry to report, and the
			// reference calls that an empty drive rather than guessing
			// at 1.44 MB.
			// A disk put in while the machine is running has to show
			// up in the status register: the ROM may look before it
			// next writes the DOR, and would be told the drive is
			// empty.  This is the drive noticing the medium.
			if (ds == 1'b0) begin
				ctrl_st[2]   <= 1'b0;
				ctrl_st[1:0] <= (img_size == 32'd737280)  ? MEDIA_720  :
				                (img_size == 32'd2949120) ? MEDIA_2880 :
				                (img_size == 32'd1474560) ? MEDIA_1440 :
				                                            MEDIA_NONE;
			end
			present_v[0] <= (img_size == 32'd737280) ||
			                (img_size == 32'd1474560) ||
			                (img_size == 32'd2949120);
			readonly_v[0] <= ((img_size == 32'd737280) ||
			                  (img_size == 32'd1474560) ||
			                  (img_size == 32'd2949120)) && img_readonly;
			img_bytes_v[0] <= ((img_size == 32'd737280) ||
			                   (img_size == 32'd1474560) ||
			                   (img_size == 32'd2949120)) ? img_size[31:0] : 0;
			spt_v[0] <= (img_size == 32'd737280)  ? 8'd9  :
			            (img_size == 32'd1474560) ? 8'd18 :
			            (img_size == 32'd2949120) ? 8'd36 : 8'd0;
			blocksize_v[0] <= ((img_size == 32'd737280) ||
			                   (img_size == 32'd1474560) ||
			                   (img_size == 32'd2949120)) ? 3'd2 : 3'd0;
			if ((img_size == 32'd737280) || (img_size == 32'd1474560) ||
			    (img_size == 32'd2949120)) begin
				connected_v[0] <= 1'b1;
				spinning_v[0] <= 1'b0;
				cyl_v[0] <= 0; head_v[0] <= 0; sector_v[0] <= 0;
			end
		end
		if (img_mounted[1]) begin
			if (ds == 1'b1) begin
				ctrl_st[2]   <= 1'b0;
				ctrl_st[1:0] <= (img_size == 32'd737280)  ? MEDIA_720  :
				                (img_size == 32'd2949120) ? MEDIA_2880 :
				                (img_size == 32'd1474560) ? MEDIA_1440 :
				                                            MEDIA_NONE;
			end
			present_v[1] <= (img_size == 32'd737280) ||
			                (img_size == 32'd1474560) ||
			                (img_size == 32'd2949120);
			readonly_v[1] <= ((img_size == 32'd737280) ||
			                  (img_size == 32'd1474560) ||
			                  (img_size == 32'd2949120)) && img_readonly;
			img_bytes_v[1] <= ((img_size == 32'd737280) ||
			                   (img_size == 32'd1474560) ||
			                   (img_size == 32'd2949120)) ? img_size[31:0] : 0;
			spt_v[1] <= (img_size == 32'd737280)  ? 8'd9  :
			            (img_size == 32'd1474560) ? 8'd18 :
			            (img_size == 32'd2949120) ? 8'd36 : 8'd0;
			blocksize_v[1] <= ((img_size == 32'd737280) ||
			                   (img_size == 32'd1474560) ||
			                   (img_size == 32'd2949120)) ? 3'd2 : 3'd0;
			if ((img_size == 32'd737280) || (img_size == 32'd1474560) ||
			    (img_size == 32'd2949120)) begin
				connected_v[1] <= 1'b1;
				spinning_v[1] <= 1'b0;
				cyl_v[1] <= 0; head_v[1] <= 0; sector_v[1] <= 0;
			end
		end

		//------------------------------------------------------------
		// execution
		//------------------------------------------------------------
		case (est)
		E_IDLE: ;

		E_SEEKDLY: begin
			if (dly == 0) est <= E_EXEC;
			else if (tick) dly <= dly - 1'd1;
		end

		// start the transfer of one sector
		E_EXEC: begin
			if (sec_left == 0) begin
				// "Strange behavior of NeXT hardware": a completed read
				// reports abnormal termination with end of cylinder.
				// The result bytes carry the new status, not the value
				// these registers held on the way in.
				// FORMAT keeps the status produced while validating its
				// descriptor/sector.  A normal format arrives here with clear
				// status; a boundary failure arrives with IC_ABNORMAL/ST1_EN.
				if (!format_mode) begin
					st0 <= IC_ABNORMAL;
					st1 <= st1 | ST1_EN;
				end
				res[0] <= (format_mode ? st0 : IC_ABNORMAL) |
				          {5'd0, head_v[io_ds][0], 1'b0, io_ds};
				res[1] <= format_mode ? st1 : (st1 | ST1_EN);
				res[2] <= st2;
				res[3] <= cyl_v[io_ds];
				res[4] <= head_v[io_ds];
				res[5] <= sector_v[io_ds];
				res[6] <= {5'd0, blocksize_v[io_ds]};
				res_size <= 4'd7;
				res_pos <= 0;
				format_mode <= 0;
				est <= E_INT;
			end
			else if (!present_v[io_ds]) begin
				st0 <= IC_ABNORMAL;
				st1 <= st1 | ST1_ND;
				res[0] <= IC_ABNORMAL | {5'd0, head_v[io_ds][0], 1'b0, io_ds};
				res[1] <= st1 | ST1_ND;
				res[2] <= st2;
				res[3] <= cyl_v[io_ds]; res[4] <= head_v[io_ds];
				res[5] <= sector_v[io_ds]; res[6] <= {5'd0, blocksize_v[io_ds]};
				res_size <= 4'd7; res_pos <= 0;
				format_mode <= 0;
				est <= E_INT;
			end
			else if (is_write && readonly_v[io_ds]) begin
				st0 <= IC_ABNORMAL;
				st1 <= st1 | ST1_NW;
				res[0] <= IC_ABNORMAL | {5'd0, head_v[io_ds][0], 1'b0, io_ds};
				res[1] <= st1 | ST1_NW;
				res[2] <= st2;
				res[3] <= cyl_v[io_ds]; res[4] <= head_v[io_ds];
				res[5] <= sector_v[io_ds]; res[6] <= {5'd0, blocksize_v[io_ds]};
				res_size <= 4'd7; res_pos <= 0;
				format_mode <= 0;
				est <= E_INT;
			end
			// READ/WRITE call get_logical_sec() before every sector in the
			// C model.  Recheck after E_NEXT increments R so an overlong EOT
			// cannot spill into the next head or beyond the mounted image.
			else if (!format_mode && (sector_v[io_ds] > spt_v[io_ds])) begin
				st0 <= IC_ABNORMAL;
				st1 <= st1 | ST1_EN;
				res[0] <= IC_ABNORMAL |
				          {5'd0, head_v[io_ds][0], 1'b0, io_ds};
				res[1] <= st1 | ST1_EN;
				res[2] <= st2;
				res[3] <= cyl_v[io_ds]; res[4] <= head_v[io_ds];
				res[5] <= sector_v[io_ds]; res[6] <= {5'd0, blocksize_v[io_ds]};
				res_size <= 4'd7; res_pos <= 0;
				format_mode <= 0;
				est <= E_INT;
			end
			else if (is_write) begin
				// the channel fills the buffer first, then it is written
				dma_req_r <= 1;
				dma_wr_r  <= 0;
				dly <= DMA_TIMEOUT_US;
				est <= E_XFER;
			end
			else begin
				sd_rd <= 1;
				est <= E_RD_GO;
			end
		end

		E_RD_GO: if (sd_ack) begin
			sd_rd <= 0;
			sd_rd_act <= 1;
			est <= E_RD_ACK;
		end

		E_RD_ACK: if (!sd_ack) begin
			sd_rd_act <= 0;
			dma_req_r <= 1;
			dma_wr_r  <= 1;
			dly <= DMA_TIMEOUT_US;
			est <= E_XFER;
		end

		// the shared channel moves the sector
		E_XFER: begin
			if (dma_done) begin
				dma_req_r <= 0;
				if (format_mode) begin
					if ((sbuf[0] != cyl_v[io_ds]) || (sbuf[1] != head_v[io_ds]) ||
					    (sbuf[2] != sector_v[io_ds]) ||
					    (sbuf[3] != {5'd0, blocksize_v[io_ds]})) begin
						// floppy_format_sector() stops on a malformed
						// descriptor without adding a controller status error.
						sec_left <= 0;
						est <= E_EXEC;
					end
					else if (sector_v[io_ds] > spt_v[io_ds]) begin
						// FORMAT consumes the descriptor first, then calls
						// get_logical_sec() and rejects the out-of-track sector
						// before zeroing or writing it.
						st0 <= IC_ABNORMAL;
						st1 <= st1 | ST1_EN;
						sec_left <= 0;
						est <= E_EXEC;
					end
					else begin
						zero_pos <= 0;
						est <= E_FMT_ZERO;
					end
				end
				else est <= is_write ? E_WR_GO : E_NEXT;
			end
			else if (dly == 0) begin
				dma_req_r <= 0;
				st0 <= IC_ABNORMAL; st1 <= st1 | ST1_OR;
				res[0] <= IC_ABNORMAL |
				          {5'd0, head_v[io_ds][0], 1'b0, io_ds};
				res[1] <= st1 | ST1_OR; res[2] <= st2;
				res[3] <= cyl_v[io_ds]; res[4] <= head_v[io_ds];
				res[5] <= sector_v[io_ds]; res[6] <= {5'd0, blocksize_v[io_ds]};
				res_size <= 7; res_pos <= 0;
				format_mode <= 0;
				est <= E_INT;
			end
			else if (tick) dly <= dly - 1'd1;
		end

		E_FMT_ZERO: begin
			if (zero_pos == sec_size-1) est <= E_WR_GO;
			else zero_pos <= zero_pos + 1'd1;
		end

		E_WR_GO: begin
			// The C image write becomes a harmless failed File_Write if the
			// medium was ejected after DMA filled the buffer.  Do not send an
			// SD write to a slot which no longer owns writable media.
			if (!present_v[io_ds] || readonly_v[io_ds]) begin
				sd_wr <= 0;
				est <= E_NEXT;
			end
			else begin
				sd_wr <= 1;
				if (sd_ack) begin
					sd_wr <= 0;
					est <= E_WR_ACK;
				end
			end
		end

		E_WR_ACK: if (!sd_ack) est <= E_NEXT;

		E_NEXT: begin
			sector_v[io_ds] <= sector_v[io_ds] + 8'd1;
			sec_left <= sec_left - 8'd1;
			est <= E_EXEC;
		end

		E_INT: begin
			finish_int;
		end

		// A seek holds the drive busy in the main status register while
		// it runs and finishes through the interrupt, which clears the
		// busy bits again.  Reporting the seek complete the instant the
		// command was written never showed a driver the drive working.
		E_SEEKINT: begin
			if (dly == 0) begin
				msr <= STAT_RQM;          // busy bits cleared
				int_pend <= 1;
				est <= E_IDLE;
			end
			else if (tick) dly <= dly - 1'd1;
		end

		// floppy_start(): one poll interrupt exactly one second after an
		// 82077 reset is released, unless CONFIGURE disables it first.
		E_RESETPOLL: if (tick) begin
			if (dly <= 1) begin
				dly <= 0;
				int_pend <= 1;
				msr <= STAT_RQM;
				est <= E_IDLE;
			end
			else dly <= dly - 1'd1;
		end

		default: est <= E_IDLE;
		endcase

		//------------------------------------------------------------
		// register access
		//------------------------------------------------------------
		if (sel && we) begin : wr
			reg [3:0] a;
			reg [7:0] v;
			integer k;
			for (k = 0; k < 2; k = k + 1) begin
				if (k == 0 ? be[1] : be[0]) begin
					a = {addr[3:1], k[0]};
					v = k[0] ? wdata[7:0] : wdata[15:8];
					case (a)
					4'h2: begin
						dor <= v;
						if ((|(v[7:4] ^ dor[7:4])) && !v[1] &&
						    drv_connected[v[0]])
							spinning_v[v[0]] <= v[v[0] ? 5 : 4];
						if (v[2] != dor[2]) begin
							controller_reset;
							if (v[2]) begin
								dly <= RESET_POLL_US;
								tickcnt <= 0;
								est <= E_RESETPOLL;
							end
						end
						// floppy_dor_write(): the status published is
						// that of the drive this write selects - taken
						// from the value being written, not the one it
						// replaces - and the medium appears only while
						// that drive's motor is running (0x10 << sel).
						if (!v[1] && drv_connected[v[0]]) begin
							ctrl_st[2]   <= 1'b0;
							ctrl_st[1:0] <= (v[0] ? v[5] : v[4])
							                ? media_of(v[0]) : MEDIA_NONE;
						end
						else begin
							ctrl_st[2]   <= 1'b1;   // no such drive
							ctrl_st[1:0] <= MEDIA_NONE;
						end
					end
					4'h4: begin               // data rate select
						dsr <= v & 8'h7F;
						if (v[7]) begin
							controller_reset;
							dly <= RESET_POLL_US;
							tickcnt <= 0;
							est <= E_RESETPOLL;
						end
					end
					4'h7: ccr <= v;          // configuration control
					4'h8: begin : ctrl_wr
						reg eject_d;
						// a command, not a stored value
						eject_d = v[5] ? 1'b0 : ds;
						sel_82077 <= v[6];
						if (v[5]) begin
							controller_reset;
							// floppy_reset() clears DOR/drive select before a
							// simultaneous CTRL_EJECT chooses its target.
							dor <= 0;
							spinning_v <= 2'b00;
							io_ds <= 0;
						end
						if (v[7]) begin
							present_v[eject_d] <= 0;
							readonly_v[eject_d] <= 0;
							img_bytes_v[eject_d] <= 0;
							spt_v[eject_d] <= 0;
							blocksize_v[eject_d] <= 0;
							spinning_v[eject_d] <= 1'b0;
						end
					end
					4'h5: begin : fifo_wr
						// command or parameter byte
						if (!cmd_phase) begin
							cmd <= v;
							cmd_want <= cmd_bytes(v[4:0]);
							cmd_got  <= 0;
							cmd_phase <= (cmd_bytes(v[4:0]) != 0);
							msr <= STAT_RQM | STAT_CMDBSY;
							if (cmd_bytes(v[4:0]) == 0) begin
								// commands that take no parameters
								case (v[4:0])
								5'h08: begin      // sense interrupt status
									int_pend <= 0;
									res[0] <= st0;
									res[1] <= pcn_v[io_ds];
									res_size <= 4'd2;
									res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO;
								end
								5'h10: begin      // version
									res[0] <= 8'h90;
									res_size <= 4'd1;
									res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO;
								end
								default: begin    // invalid
									st0 <= IC_INV_CMD;
									res[0] <= IC_INV_CMD;
									res_size <= 4'd1;
									res_pos <= 0;
									int_pend <= 1;
									msr <= STAT_RQM | STAT_DIO;
									cancel_reset_poll;
								end
								endcase
							end
						end
						else begin
							cmd_data[cmd_got] <= v;
							cmd_got <= cmd_got + 1'd1;
							if (cmd_got + 1'd1 == cmd_want) begin
								cmd_phase <= 0;
								// execute: the parameter just latched is
								// not yet in cmd_data, so use it directly
								case (cmd[4:0])
								5'h03: begin      // specify
									msr <= STAT_RQM;
								end
								5'h13: begin      // configure
									eis <= cmd_data[1][6];
									if (cmd_data[1][4]) cancel_reset_poll;
									msr <= STAT_RQM;
								end
								5'h12, 5'h14: msr <= STAT_RQM;   // perpendicular, lock
								5'h07: begin      // recalibrate
									if (drv_connected[v[0]]) begin
										io_ds <= v[0];
										cyl_v[v[0]] <= 0;
										pcn_v[v[0]] <= 0;
										st0 <= IC_NORMAL | ST0_SE;
										st1 <= 0; st2 <= 0;
										// the drive is busy seeking; the
										// controller still takes commands
										msr <= STAT_RQM | (8'h01 << v[0]);
										dly <= 25'd200;
										est <= E_SEEKINT;
									end
									else msr <= STAT_RQM;
								end
								5'h0F: begin      // seek
									io_ds <= cmd_data[0][0];
									cyl_v[cmd_data[0][0]] <= v;
									pcn_v[cmd_data[0][0]] <= v;
									head_v[cmd_data[0][0]] <= {7'd0, cmd_data[0][2]};
									st0 <= IC_NORMAL | ST0_SE;
									msr <= STAT_RQM | (8'h01 << cmd_data[0][0]);
									dly <= 25'd200;
									est <= E_SEEKINT;
								end
								5'h04: begin      // drive status
									res[0] <= (readonly_v[v[0]] ? ST3_WP : 8'h00) |
									          ((cyl_v[v[0]] == 0) ? ST3_T0 : 8'h00) |
									          (v[2] ? ST3_HD : 8'h00) |
									          {6'd0, v[1:0]};
									res_size <= 4'd1;
									res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO;
								end
								5'h0A: begin : read_id
									reg [7:0] id_st0, id_st1;
									id_st0 = {5'd0, v[2], 1'b0, v[0]};
									id_st1 = 0;
									if (!present_v[v[0]] || !rate_ok(v[0])) begin
										id_st0 = id_st0 | IC_ABNORMAL;
										id_st1 = ST1_ND;
									end
									io_ds <= v[0];
									head_v[v[0]] <= {7'd0, v[2]};
									st0 <= id_st0;
									st1 <= id_st1;
									st2 <= 0;
									res[0] <= id_st0;
									res[1] <= id_st1;
									res[2] <= 0;
									res[3] <= cyl_v[v[0]];
									res[4] <= {7'd0, v[2]};
									res[5] <= sector_v[v[0]];
									res[6] <= {5'd0, blocksize_v[v[0]]};
									res_size <= 4'd7;
									res_pos <= 0;
									int_pend <= 1;
									msr <= STAT_RQM | STAT_DIO;
									cancel_reset_poll;
								end
								5'h06, 5'h05: begin : rw_cmd // read, write
									reg d;
									reg [7:0] e1, e2;
									// cmd_data[0]=drive/head, [1]=C,
									// [2]=H, [3]=R, [4]=N, [5]=EOT
									d = cmd_data[0][0];
									e1 = 0; e2 = 0;
									io_ds <= d;
									head_v[d] <= {7'd0, cmd_data[0][2]};
									sector_v[d] <= cmd_data[3];
									if (eis) begin
										cyl_v[d] <= cmd_data[1];
										pcn_v[d] <= cmd_data[1];
									end
									else if (cyl_v[d] != cmd_data[1]) e2 = e2 | ST2_WC;
									if ({7'd0, cmd_data[0][2]} != cmd_data[2]) e2 = e2 | ST2_WC;
									if (cmd_data[4] != {5'd0, blocksize_v[d]}) e1 = e1 | ST1_ND;
									if (!rate_ok(d)) e1 = e1 | ST1_ND;
									if (cmd_data[1] >= CYLS) e1 = e1 | ST1_ND;
									if ((cmd_data[3] == 0) || (cmd_data[3] > spt_v[d])) e1 = e1 | ST1_EN;
									if ((cmd[4:0] == 5'h05) && readonly_v[d]) e1 = e1 | ST1_NW;
								// EOT is parameter 5; the byte just
								// latched is DTL, the last of eight
								sec_left  <= (cmd_data[5] >= cmd_data[3])
								             ? (cmd_data[5] - cmd_data[3] + 8'd1)
								             : 8'd1;
								is_write  <= (cmd[4:0] == 5'h05);
								format_mode <= 0;
								st0 <= ((e1 != 0) || (e2 != 0)) ? IC_ABNORMAL : 0;
									st1 <= e1; st2 <= e2;
									if ((e1 != 0) || (e2 != 0)) begin
										res[0] <= IC_ABNORMAL |
										          {5'd0, cmd_data[0][2], 1'b0, d};
										res[1] <= e1; res[2] <= e2;
										res[3] <= eis ? cmd_data[1] : cyl_v[d];
										res[4] <= {7'd0, cmd_data[0][2]};
										res[5] <= cmd_data[3];
										res[6] <= {5'd0, blocksize_v[d]};
										res_size <= 4'd7; res_pos <= 0;
										msr <= STAT_RQM | STAT_DIO;
										int_pend <= 1;
										cancel_reset_poll;
									end
									else begin
										msr <= STAT_CMDBSY;
										dly <= 25'd2000;
										est <= E_SEEKDLY;
									end
								end
								5'h0D: begin : fmt_cmd // format track
									reg d;
									reg [7:0] e1;
								d = cmd_data[0][0];
								e1 = 0;
								if (cmd_data[1] != {5'd0, blocksize_v[d]}) e1 = e1 | ST1_ND;
								if (!rate_ok(d) || !present_v[d]) e1 = e1 | ST1_ND;
								if (cyl_v[d] >= CYLS) e1 = e1 | ST1_ND;
								if (readonly_v[d]) e1 = e1 | ST1_NW;
									io_ds <= d;
									head_v[d] <= {7'd0, cmd_data[0][2]};
									sector_v[d] <= 1;
									st0 <= (e1 != 0) ? IC_ABNORMAL : 0;
									st1 <= e1; st2 <= 0;
									if (e1 != 0) begin
										res[0] <= IC_ABNORMAL |
										          {5'd0, cmd_data[0][2], 1'b0, d};
										res[1] <= e1; res[2] <= 0;
									res[3] <= cyl_v[d]; res[4] <= {7'd0, cmd_data[0][2]};
									res[5] <= 1; res[6] <= {5'd0, blocksize_v[d]};
									res_size <= 7; res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO; int_pend <= 1;
									format_mode <= 0;
									cancel_reset_poll;
									end
									else begin
										sec_left <= cmd_data[2];
										is_write <= 1;
										format_mode <= 1;
										msr <= STAT_CMDBSY;
										est <= E_EXEC;
									end
								end
								default: begin
									st0 <= IC_INV_CMD;
									res[0] <= IC_INV_CMD;
									res_size <= 4'd1;
									res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO;
								end
								endcase
							end
						end
					end
					default: ;
					endcase
				end
			end
		end

		// FIFO read: hand out the result bytes
		if (sel && !we && ((be[1] && a_even == 4'h5) || (be[0] && a_odd == 4'h5))) begin
			if (res_size != 0) begin
				for (i = 0; i < 6; i = i + 1) res[i] <= res[i+1];
				res[6] <= 8'h00;
				res_size <= res_size - 1'd1;
				if (res_size == 4'd1) begin
					msr <= (msr | STAT_RQM) & ~STAT_DIO;
					int_pend <= 0;
				end
			end
			else begin
				msr <= (msr | STAT_RQM) & ~STAT_DIO;
				int_pend <= 0;
			end
		end
	end
end

endmodule
