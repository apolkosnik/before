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
localparam ST1_EN      = 8'h80, ST1_ND      = 8'h04;
localparam ST2_WC      = 8'h10;

reg  [7:0] dor;              // digital output
reg  [7:0] msr;              // main status
reg  [7:0] ccr;              // configuration control (data rate)
// The external control register is not one register.  Writing it is a
// command - select the DMA channel, eject - and FLP_Select_Write never
// stores the value.  Reading it returns status that floppy_dor_write
// maintains: whether a drive is connected, and what medium is in it.
// Keeping one register for both let a channel-select write erase the
// media ID and leave the ROM seeing an empty drive.
reg        sel_82077;        // written at +8, selects the DMA channel
reg  [7:0] ctrl_st;          // read at +8: drive and media status
reg  [7:0] st0, st1, st2;
reg  [7:0] pcn;              // present cylinder
reg        eis;              // implied seek enabled
reg        int_pend;

reg  [7:0] cyl, head, sector;
reg  [2:0] blocksize;        // 0x80 << blocksize

reg        present;          // an image is mounted
reg        readonly;
reg [31:0] img_bytes;
reg  [7:0] spt;              // sectors per track, from the image size

assign int_floppy = int_pend;
assign flp_select = sel_82077 | mo_gpo;    // CTRL_82077 or the MO's GPO

wire [7:0] sra = (int_pend ? SRA_INT : 8'h00) |
                 SRA_DRV1_N |                       // only drive 0 exists
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

wire [1:0] media_id = !present            ? MEDIA_NONE :
                      (spt == 8'd9)       ? MEDIA_720  :
                      (spt == 8'd36)      ? MEDIA_2880 : MEDIA_1440;
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

`define FLP_READ(a) ( \
	((a) == 4'h0) ? sra : \
	((a) == 4'h1) ? srb : \
	((a) == 4'h2) ? dor : \
	((a) == 4'h4) ? msr : \
	((a) == 4'h5) ? res[0] : \
	((a) == 4'h7) ? dir : \
	((a) == 4'h8) ? ctrl_st : 8'h00 )

assign rdata = {`FLP_READ(a_even), `FLP_READ(a_odd)};

//----------------------------------------------------------------------------
// sector buffer, shared with the DMA channel and the SD card
//----------------------------------------------------------------------------

reg  [7:0] sbuf [0:1023];
reg  [7:0] sbuf_q;

wire        in_sd  = (sd_rd | sd_wr | sd_ack);
wire  [9:0] s_addr = in_sd ? {1'b0, sd_buff_addr} : buf_addr;
wire        s_we   = in_sd ? (sd_buff_wr & sd_rd_act) : buf_we;
wire  [7:0] s_wd   = in_sd ? sd_buff_dout : buf_wdata;

always @(posedge clk) begin
	if (s_we) sbuf[s_addr] <= s_wd;
	sbuf_q <= sbuf[s_addr];
end

assign buf_q       = sbuf_q;
assign sd_buff_din = sbuf_q;

//----------------------------------------------------------------------------
// geometry: 80 cylinders, 2 heads, sectors per track from the size
//----------------------------------------------------------------------------

localparam CYLS = 8'd80, HEADS = 8'd2;

wire [10:0] sec_size = 11'd128 << blocksize;
wire [31:0] lba = (({24'd0, cyl} * HEADS) + {24'd0, head}) * {24'd0, spt}
                  + {24'd0, sector} - 32'd1;

assign buf_len = sec_size;
assign sd_lba  = lba;

//----------------------------------------------------------------------------
// execution engine
//----------------------------------------------------------------------------

localparam E_SEEKINT = 4'd11;   // a seek in progress, drive busy
localparam E_IDLE  = 4'd0,  E_EXEC   = 4'd1,  E_RESULT = 4'd2,
           E_RD_GO = 4'd3,  E_RD_ACK = 4'd4,  E_XFER   = 4'd5,
           E_WR_GO = 4'd6,  E_WR_ACK = 4'd7,  E_NEXT   = 4'd8,
           E_INT   = 4'd9,  E_SEEKDLY= 4'd10;
reg  [3:0] est;

reg        sd_rd_act;
reg  [7:0] sec_left;         // sectors still to transfer
reg        is_write;
reg [24:0] dly;

reg        dma_req_r, dma_wr_r;
assign dma_req = dma_req_r;
assign dma_wr  = dma_wr_r;

localparam TICK = CLK_HZ / 1000000;
reg [$clog2(TICK)-1:0] tickcnt;
wire tick = (tickcnt == TICK-1);

integer i;

task automatic finish_int;
	begin
		int_pend <= 1;
		msr <= STAT_RQM | STAT_DIO;
		est <= E_IDLE;
	end
endtask

// the seven byte result of a read or write
task automatic rw_result;
	begin
		res[0] <= st0 | {5'd0, head[0], 2'd0};
		res[1] <= st1;
		res[2] <= st2;
		res[3] <= cyl;
		res[4] <= head;
		res[5] <= sector;
		res[6] <= {5'd0, blocksize};
		res_size <= 4'd7;
		res_pos <= 0;
	end
endtask

always @(posedge clk) begin
	if (reset) begin
		dor <= 8'h00;
		msr <= STAT_RQM;
		ccr <= 8'h00;
		sel_82077 <= 1'b0;
		ctrl_st <= 8'h04;        // no drive selected yet
		st0 <= 0; st1 <= 0; st2 <= 0;
		pcn <= 0;
		eis <= 0;
		int_pend <= 0;
		cyl <= 0; head <= 0; sector <= 1;
		blocksize <= 3'd2;              // 512 bytes
		cmd <= 0; cmd_want <= 0; cmd_got <= 0; cmd_phase <= 0;
		res_size <= 0; res_pos <= 0;
		est <= E_IDLE;
		sd_rd <= 0; sd_wr <= 0; sd_rd_act <= 0;
		sec_left <= 0; is_write <= 0;
		dma_req_r <= 0; dma_wr_r <= 0;
		dly <= 0;
		tickcnt <= 0;
	end
	else begin
		tickcnt <= tick ? 1'd0 : tickcnt + 1'd1;

		//------------------------------------------------------------
		// medium
		//------------------------------------------------------------
		if (img_mounted) begin
			// Only the three formats the drive takes are a medium at
			// all; anything else has no geometry to report, and the
			// reference calls that an empty drive rather than guessing
			// at 1.44 MB.
			present  <= (img_size == 32'd737280) ||
			            (img_size == 32'd1474560) ||
			            (img_size == 32'd2949120);
			readonly <= img_readonly;
			img_bytes <= img_size[31:0];
			spt <= (img_size == 32'd737280)  ? 8'd9  :
			       (img_size == 32'd2949120) ? 8'd36 : 8'd18;
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
				st0 <= IC_ABNORMAL;
				st1 <= st1 | ST1_EN;
				res[0] <= IC_ABNORMAL | {5'd0, head[0], 2'd0};
				res[1] <= st1 | ST1_EN;
				res[2] <= st2;
				res[3] <= cyl;
				res[4] <= head;
				res[5] <= sector;
				res[6] <= {5'd0, blocksize};
				res_size <= 4'd7;
				res_pos <= 0;
				est <= E_INT;
			end
			else if (!present) begin
				st0 <= IC_ABNORMAL;
				st1 <= st1 | ST1_ND;
				rw_result;
				est <= E_INT;
			end
			else if (is_write) begin
				// the channel fills the buffer first, then it is written
				dma_req_r <= 1;
				dma_wr_r  <= 0;
				est <= E_XFER;
			end
			else begin
				sd_rd <= 1;
				sd_rd_act <= 1;
				est <= E_RD_GO;
			end
		end

		E_RD_GO: if (sd_ack) begin
			sd_rd <= 0;
			est <= E_RD_ACK;
		end

		E_RD_ACK: if (!sd_ack) begin
			sd_rd_act <= 0;
			dma_req_r <= 1;
			dma_wr_r  <= 1;
			est <= E_XFER;
		end

		// the shared channel moves the sector
		E_XFER: if (dma_done) begin
			dma_req_r <= 0;
			est <= is_write ? E_WR_GO : E_NEXT;
		end

		E_WR_GO: begin
			if (readonly) est <= E_NEXT;
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
			sector   <= sector + 8'd1;
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
						// floppy_dor_write(): the status published is
						// that of the drive the write selects, and only
						// drive 0 exists here, so selecting any other
						// reports no drive at all.  The medium appears
						// when that drive's motor is running.
						if (v[1:0] == 2'd0) begin
							ctrl_st[2]   <= 1'b0;
							ctrl_st[1:0] <= v[4] ? media_id : MEDIA_NONE;
						end
						else begin
							ctrl_st[2]   <= 1'b1;   // no such drive
							ctrl_st[1:0] <= MEDIA_NONE;
						end
					end
					4'h4: ccr <= v;          // data rate select
					4'h7: ccr <= v;          // configuration control
					4'h8: begin
						// a command, not a stored value
						sel_82077 <= v[6];
						if (v[7]) present <= 0;   // eject
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
									res[1] <= pcn;
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
									msr <= STAT_RQM | STAT_DIO;
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
									msr <= STAT_RQM;
								end
								5'h12, 5'h14: msr <= STAT_RQM;   // perpendicular, lock
								5'h07: begin      // recalibrate
									cyl <= 0;
									pcn <= 0;
									st0 <= IC_NORMAL | ST0_SE;
									st1 <= 0; st2 <= 0;
									// the drive is busy seeking; the
									// controller still takes commands
									msr <= STAT_RQM | 8'h01;
									dly <= 25'd200;
									est <= E_SEEKINT;
								end
								5'h0F: begin      // seek
									cyl <= (v > CYLS-1) ? CYLS-8'd1 : v;
									pcn <= (v > CYLS-1) ? CYLS-8'd1 : v;
									head <= {7'd0, cmd_data[0][2]};
									st0 <= IC_NORMAL | ST0_SE;
									msr <= STAT_RQM | 8'h01;
									dly <= 25'd200;
									est <= E_SEEKINT;
								end
								5'h04: begin      // drive status
									res[0] <= {2'd0, readonly, 1'b1,
									           (cyl == 0), 1'b0, 1'b0, 1'b0};
									res_size <= 4'd1;
									res_pos <= 0;
									msr <= STAT_RQM | STAT_DIO;
								end
								5'h0A: begin      // read id
									res[0] <= st0;
									res[1] <= st1;
									res[2] <= st2;
									res[3] <= cyl;
									res[4] <= head;
									res[5] <= sector;
									res[6] <= {5'd0, blocksize};
									res_size <= 4'd7;
									res_pos <= 0;
									int_pend <= 1;
									msr <= STAT_RQM | STAT_DIO;
								end
								5'h06, 5'h05: begin   // read, write
									// cmd_data[0]=drive/head, [1]=C,
									// [2]=H, [3]=R, [4]=N, [5]=EOT
									head      <= {7'd0, cmd_data[0][2]};
									cyl       <= cmd_data[1];
									sector    <= cmd_data[3];
									blocksize <= cmd_data[4][2:0];
									// EOT is parameter 5; the byte just
									// latched is DTL, the last of eight
									sec_left  <= (cmd_data[5] >= cmd_data[3])
									             ? (cmd_data[5] - cmd_data[3] + 8'd1)
									             : 8'd1;
									is_write  <= (cmd[4:0] == 5'h05);
									st0 <= 0; st1 <= 0; st2 <= 0;
									msr <= STAT_CMDBSY;
									dly <= 25'd2000;      // seek and sector time
									est <= E_SEEKDLY;
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
					msr <= STAT_RQM;
					int_pend <= 0;
				end
			end
		end
	end
end

endmodule
