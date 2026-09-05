//============================================================================
//  Floppy controller test: drives the real next_floppy through the
//  82077 command protocol a driver uses, against an SD model holding a
//  patterned 1.44 MB image:
//    - the reset state advertises itself ready for a command
//    - SPECIFY and CONFIGURE are accepted and leave the controller ready
//    - RECALIBRATE interrupts, and SENSE INTERRUPT STATUS returns
//      status register 0 and the present cylinder, clearing the request
//    - SEEK moves the head and reports it
//    - READ hands a sector to the shared DMA channel, byte for byte
//      from the image, and ends with the seven byte result the NeXT
//      controller produces (abnormal termination with end of cylinder)
//    - WRITE takes a sector from the channel and puts it on the image
//    - an invalid opcode reports invalid command
//============================================================================

`timescale 1ns/1ps

module tb_next_floppy;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg        sel = 0;
reg  [3:0] addr = 0;
reg        we = 0;
reg  [1:0] be = 0;
reg [15:0] wdata = 0;
wire [15:0] rdata;

wire        int_floppy, flp_select;
wire  [9:0] buf_addr_o;
wire        dma_req, dma_wr;
wire [10:0] buf_len;
wire  [7:0] buf_q;

reg  [9:0] buf_addr = 0;
reg        buf_we = 0;
reg  [7:0] buf_wdata = 0;
reg        dma_done = 0;

reg   [1:0] img_mounted = 0;
reg         img_readonly = 0;
reg  [63:0] img_size = 0;
wire [31:0] sd_lba;
wire        sd_rd, sd_wr;
reg         sd_ack = 0;
reg   [8:0] sd_buff_addr = 0;
reg   [7:0] sd_buff_dout = 0;
wire  [7:0] sd_buff_din;
reg         sd_buff_wr = 0;

next_floppy #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel(sel), .addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.int_floppy(int_floppy), .flp_select(flp_select), .mo_gpo(1'b0),
	.buf_addr(buf_addr), .buf_we(buf_we), .buf_wdata(buf_wdata),
	.buf_q(buf_q), .buf_len(buf_len),
	.dma_req(dma_req), .dma_wr(dma_wr), .dma_done(dma_done),
	.img_mounted(img_mounted), .img_readonly(img_readonly), .img_size(img_size),
	.sd_unit(),
	.sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack),
	.sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
	.sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr)
);

//----------------------------------------------------------------------------
// SD model: a 1.44 MB image, each byte encoding its own block and offset
//----------------------------------------------------------------------------

localparam BLOCKS = 2880;

reg [7:0] disk [0:BLOCKS*512-1];

function [7:0] pat;
	input [31:0] blk;
	input [31:0] off;
	begin
		pat = blk[7:0] ^ {off[7:0]} ^ {blk[10:8], 5'd0};
	end
endfunction

integer di;
initial for (di = 0; di < BLOCKS*512; di = di + 1)
	disk[di] = pat(di/512, di%512);

reg sd_rd_act = 0, sd_wr_act = 0, sd_ph = 0;
integer sd_reads = 0, sd_writes = 0;
reg [31:0] last_lba = 0;

always @(posedge clk) begin
	// Match hps_io: the final registered buffer-write pulse trails ack.
	sd_buff_wr <= 0;
	if (sd_rd && !sd_ack && !sd_rd_act) begin
		sd_ack <= 1; sd_rd_act <= 1; sd_buff_addr <= 0;
		last_lba <= sd_lba;
		sd_reads = sd_reads + 1;
	end
	else if (sd_ack && sd_rd_act) begin
		if (!sd_buff_wr) begin
			sd_buff_dout <= disk[{sd_lba[11:0], 9'd0} + {23'd0, sd_buff_addr}];
			sd_buff_wr <= 1;
			if (sd_buff_addr == 9'd511) begin sd_ack <= 0; sd_rd_act <= 0; end
		end
		else begin
			if (sd_buff_addr != 9'd511) sd_buff_addr <= sd_buff_addr + 1'd1;
		end
	end
	else if (sd_wr && !sd_ack && !sd_wr_act) begin
		sd_ack <= 1; sd_wr_act <= 1; sd_buff_addr <= 0; sd_ph <= 0;
		last_lba <= sd_lba;
		sd_writes = sd_writes + 1;
	end
	else if (sd_ack && sd_wr_act) begin
		if (sd_ph) begin
			disk[{sd_lba[11:0], 9'd0} + {23'd0, sd_buff_addr}] <= sd_buff_din;
			sd_ph <= 0;
			if (sd_buff_addr == 9'd511) begin sd_ack <= 0; sd_wr_act <= 0; end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
		end
		else sd_ph <= 1;
	end
end

//----------------------------------------------------------------------------
// register access
//----------------------------------------------------------------------------

task wr8;
	input [3:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel <= 0; we <= 0;
	end
endtask

task rd8;
	input [3:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel <= 0;
		@(posedge clk);
	end
endtask

// the shared DMA channel: move one sector between the buffer and here
reg [7:0] host [0:1023];
integer   moved = 0;

task run_channel;
	integer k;
	begin
		// wait for any previous request to be retired first, or a
		// multi-sector transfer reads the buffer while the next sector
		// is still being loaded into it
		while (dma_req) @(posedge clk);
		while (!dma_req) @(posedge clk);
		if (dma_wr) begin
			for (k = 0; k < buf_len; k = k + 1) begin
				@(posedge clk);
				buf_addr <= k[9:0];
				@(posedge clk);
				@(posedge clk);
				host[k] = buf_q;
			end
		end
		else begin
			for (k = 0; k < buf_len; k = k + 1) begin
				@(posedge clk);
				buf_addr <= k[9:0];
				buf_we <= 1;
				buf_wdata <= host[k];
				@(posedge clk);
				buf_we <= 0;
			end
		end
		moved = moved + 1;
		@(posedge clk);
		dma_done <= 1;
		@(posedge clk);
		dma_done <= 0;
	end
endtask

// Abort an intentionally wedged fail-first transfer without leaving the
// one-second DSR reset poll armed.  DSR reset preserves the mounted medium
// and the drive's current cylinder, just as the controller-only reset does.
task reset_controller_no_poll;
	begin
		wr8(4'h4, 8'h80);
		wr8(4'h5, 8'h13); wr8(4'h5, 8'h00);
		wr8(4'h5, 8'h10); wr8(4'h5, 8'h00);
	end
endtask

integer errors = 0;

task check;
	input cond;
	input [639:0] name;
	begin
		if (cond) $display("PASS: %0s", name);
		else begin $display("FAIL: %0s", name); errors = errors + 1; end
	end
endtask

reg [7:0] v, msr;
wire dor_written = (dut.dor != 8'h00);
integer i, bad;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// Before anything is mounted the machine still has a drive.  The
	// reference's control register starts at zero: CTRL_DRV_ID clear
	// says the drive is there, and the media bits say it is empty.
	// Coming up with CTRL_DRV_ID set tells the ROM the machine has no
	// floppy drive at all, and no disk will ever be found in it.
	rd8(4'h8, v);
	check(!v[2], "an empty machine still reports a floppy drive");
	check(v[1:0] == 2'd0, "an empty drive reports no medium");

	img_size = BLOCKS*512;          // 1.44 MB
	img_mounted = 1;
	@(posedge clk);
	img_mounted = 0;
	repeat (20) @(posedge clk);

	// Putting a disk in has to show in the status register straight
	// away.  The ROM may look before it next writes the DOR, and
	// would otherwise be told the drive is still empty.
	rd8(4'h8, v);
	check(!v[2] && v[1:0] == 2'd2, "a disk put in is reported at once");

	// ... without the driver having written the DOR, and without the
	// motor running: the media ID is off the disk, not off the motor
	check(dor_written == 1'b0, "no DOR write was needed to see the disk");

	// and it survives the RESET instruction, which reaches every
	// device: the reference never clears this register on a reset
	reset = 1;
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);
	rd8(4'h8, v);
	check(!v[2] && v[1:0] == 2'd2, "the medium survives a device reset");

	rd8(4'h4, msr);
	check(msr[7] && !msr[6], "reset leaves the controller ready for a command");

	// The ROM decides whether there is a drive and a disk from the
	// external control register, not from anything in the 82077: bit 2
	// clear means a drive is connected, and the low two bits are the
	// media ID that appears when the motor spins up.  Zero there is
	// what "No Floppy Disk Present" means.
	wr8(4'h2, 8'h1C);            // motor 0 on, reset released, drive 0
	rd8(4'h8, v);
	$display("  control register = %02x", v);
	check(v[2] == 1'b0, "a drive is reported as connected");
	check(v[1:0] == 2'd2, "a 1.44 MB medium reports media id 2");

	// The control register is a command on write and status on read:
	// selecting the DMA channel must not erase the published medium.
	wr8(4'h8, 8'h40);            // CTRL_82077, the channel select
	rd8(4'h8, v);
	check(v[1:0] == 2'd2, "a channel select does not erase the media id");
	check(flp_select, "the channel is selected");

	// a drive that does not exist reports no drive and no medium
	wr8(4'h2, 8'h1D);            // select drive 1, motor on
	rd8(4'h8, v);
	check(v[2] == 1'b1, "an absent drive reports no drive");
	check(v[1:0] == 2'd0, "an absent drive reports no medium");

	// floppy_recalibrate() ignores a drive that is not connected: it
	// neither marks it busy nor schedules a completion interrupt.
	wr8(4'h5, 8'h07); wr8(4'h5, 8'h01);
	rd8(4'h4, msr);
	check(!msr[1], "an absent drive does not become busy on recalibrate");
	repeat (400) @(posedge clk);
	check(!int_floppy, "an absent drive does not interrupt on recalibrate");
	// Clear any state left by a broken implementation so later cases are
	// independent; controller reset does not eject the mounted medium.
	reset = 1;
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);
	wr8(4'h2, 8'h1C);            // back to drive 0

	// The medium is still there with the motor stopped.  The
	// reference clears the ID when the motor stops and marks that
	// doubtful in a comment of its own; the ID comes off the disk's
	// density sense holes, which do not care whether it is spinning.
	wr8(4'h2, 8'h0C);            // motor off
	rd8(4'h8, v);
	check(v[1:0] == 2'd2, "the medium is still reported with the motor off");
	wr8(4'h2, 8'h1C);            // motor back on for the rest of the test

	// SPECIFY: two parameters, no result
	wr8(4'h5, 8'h03); wr8(4'h5, 8'hDF); wr8(4'h5, 8'h02);
	rd8(4'h4, msr);
	check(msr[7] && !msr[6], "specify accepted, still command ready");

	// CONFIGURE: three parameters
	wr8(4'h5, 8'h13); wr8(4'h5, 8'h00); wr8(4'h5, 8'h57); wr8(4'h5, 8'h00);
	rd8(4'h4, msr);
	check(msr[7], "configure accepted");

	// RECALIBRATE, then SENSE INTERRUPT STATUS
	wr8(4'h5, 8'h07); wr8(4'h5, 8'h00);
	@(posedge clk);
	rd8(4'h4, msr);
	check(msr[0], "the drive reports busy while it seeks");
	repeat (400) @(posedge clk);      // the seek takes time, as a seek does
	check(int_floppy, "recalibrate raises the interrupt");
	rd8(4'h4, msr);
	check(!msr[0], "the busy bit is cleared when the seek completes");
	wr8(4'h5, 8'h08);
	rd8(4'h5, v);
	check(v == 8'h20, "sense interrupt: seek end, normal termination");
	rd8(4'h5, v);
	check(v == 8'h00, "sense interrupt: present cylinder 0");
	check(!int_floppy, "sense interrupt clears the request");

	// SENSE DRIVE STATUS uses the bit positions defined by floppy.c:
	// WP=0x40, T0=0x10, HD=0x04 and DS=0x03.  Bits which the C model
	// does not define must remain clear.
	wr8(4'h5, 8'h04); wr8(4'h5, 8'h04);       // drive 0, head 1
	rd8(4'h5, v);
	check(v == 8'h14, "writable track-zero ST3 is exactly T0|HD");

	// A seek completion has no FIFO result.  Reading that empty FIFO still
	// returns zero and acknowledges the interrupt in the C model.  Prime the
	// otherwise stale storage and DIO bit so both effects are deterministic.
	wr8(4'h5, 8'h07); wr8(4'h5, 8'h00);
	repeat (400) @(posedge clk);
	check(int_floppy, "recalibrate supplies an interrupt for empty-FIFO acknowledgement");
	dut.res[0] = 8'hA5;
	dut.msr = 8'h40;
	rd8(4'h5, v);
	check(v == 8'h00, "an empty FIFO returns zero instead of stale result storage");
	check(!int_floppy, "an empty FIFO read acknowledges the pending interrupt");
	rd8(4'h4, msr);
	check(msr[7] && !msr[6], "an empty FIFO read sets RQM and clears DIO");

	// SEEK to cylinder 5
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08);
	rd8(4'h5, v);
	rd8(4'h5, v);
	check(v == 8'd5, "seek reports the new cylinder");

	// READ one sector: cylinder 0, head 0, sector 1
	wr8(4'h5, 8'h46);       // read, MFM
	wr8(4'h5, 8'h00);       // drive 0, head 0
	wr8(4'h5, 8'h00);       // C
	wr8(4'h5, 8'h00);       // H
	wr8(4'h5, 8'h01);       // R
	wr8(4'h5, 8'h02);       // N = 512 bytes
	wr8(4'h5, 8'h01);       // EOT
	wr8(4'h5, 8'h1B);       // GPL
	wr8(4'h5, 8'hFF);       // DTL

	run_channel;
	check(sd_reads > 0, "read fetched a sector from the image");
	check(last_lba == 32'd0, "read addressed logical sector 0");
	bad = 0;
	for (i = 0; i < 512; i = i + 1)
		if (host[i] !== pat(0, i)) bad = bad + 1;
	check(bad == 0, "the sector reached the channel byte for byte");

	// the transfer ends with the seven byte result
	repeat (200) @(posedge clk);
	check(int_floppy, "read completion interrupts");
	rd8(4'h5, v);
	check(v[7:6] == 2'b01, "result: abnormal termination, as NeXT reports");
	rd8(4'h5, v);
	check(v[7], "result: end of cylinder");
	for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	check(!int_floppy, "reading the result clears the interrupt");

	// A driver reads whole tracks, not single sectors: R=1 through
	// EOT=18, which is the path that reports a sector number when it
	// goes wrong.
	wr8(4'h5, 8'h46);
	wr8(4'h5, 8'h00);       // drive 0, head 0
	wr8(4'h5, 8'h00);       // C
	wr8(4'h5, 8'h00);       // H
	wr8(4'h5, 8'h01);       // R = 1
	wr8(4'h5, 8'h02);       // N = 512
	wr8(4'h5, 8'd18);       // EOT = 18
	wr8(4'h5, 8'h1B);
	wr8(4'h5, 8'hFF);

	bad = 0;
	for (i = 0; i < 18; i = i + 1) begin
		run_channel;
		if (host[0] !== pat(i, 0) || host[511] !== pat(i, 511)) begin
			if (bad < 3)
				$display("  track sector %0d: first %02x want %02x, last %02x want %02x",
				         i + 1, host[0], pat(i, 0), host[511], pat(i, 511));
			bad = bad + 1;
		end
	end
	check(bad == 0, "a whole track reads back, all 18 sectors in order");

	repeat (400) @(posedge clk);
	check(int_floppy, "the track read finishes with an interrupt");
	rd8(4'h5, v);
	$display("  track result ST0 = %02x", v);
	for (i = 0; i < 6; i = i + 1) rd8(4'h5, v);

	// WRITE one sector to cylinder 0, head 1, sector 3
	for (i = 0; i < 512; i = i + 1) host[i] = 8'hA0 + i[7:0];
	wr8(4'h5, 8'h45);       // write, MFM
	wr8(4'h5, 8'h04);       // drive 0, head 1
	wr8(4'h5, 8'h00);       // C
	wr8(4'h5, 8'h01);       // H
	wr8(4'h5, 8'h03);       // R
	wr8(4'h5, 8'h02);       // N
	wr8(4'h5, 8'h03);       // EOT
	wr8(4'h5, 8'h1B);
	wr8(4'h5, 8'hFF);

	run_channel;
	// the sector goes out to the card two cycles a byte: wait for the
	// transfer to begin and then to drain
	while (sd_writes == 0) @(posedge clk);
	while (sd_wr_act) @(posedge clk);
	repeat (20) @(posedge clk);
	check(sd_writes > 0, "write put a sector back on the image");
	$display("  write LBA = %0d", last_lba);
	// head 1, sector 3 on cylinder 0 with 18 sectors per track is LBA 20
	bad = 0;
	for (i = 0; i < 512; i = i + 1)
		if (disk[20*512 + i] !== (8'hA0 + i[7:0])) bad = bad + 1;
	check(bad == 0, "the written sector landed at the right offset");

	// Protection is checked before DMA.  Consuming a sector and silently
	// discarding it made a protected write appear to succeed.
	img_readonly = 1;
	img_mounted = 1; @(posedge clk); img_mounted = 0;
	wr8(4'h5, 8'h04); wr8(4'h5, 8'h00);       // drive 0, head 0
	rd8(4'h5, v);
	check(v == 8'h50, "read-only track-zero ST3 is exactly WP|T0");
	wr8(4'h5, 8'h45); wr8(4'h5, 8'h00); wr8(4'h5, 8'h00);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'h04); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h04); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	repeat (20) @(posedge clk);
	check(int_floppy && !dma_req, "a protected write fails before requesting DMA");
	rd8(4'h5, v);                 // ST0
	rd8(4'h5, v);                 // ST1
	check(v[1], "a protected write reports ST1 not-writable");
	for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	img_readonly = 0;
	img_mounted = 1; @(posedge clk); img_mounted = 0;

	// Disable implied seek, move to cylinder 5, then request cylinder 0.
	// The reference reports wrong-cylinder instead of moving implicitly.
	wr8(4'h5, 8'h13); wr8(4'h5, 8'h00); wr8(4'h5, 8'h17); wr8(4'h5, 8'h00);
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08); rd8(4'h5, v); rd8(4'h5, v);
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h00); wr8(4'h5, 8'h00);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'h01); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	repeat (20) @(posedge clk);
	check(int_floppy && !dma_req, "wrong-cylinder read fails before DMA");
	rd8(4'h5, v); rd8(4'h5, v); rd8(4'h5, v);
	check(v[4], "wrong-cylinder read reports ST2 wrong-cylinder");
	for (i = 0; i < 4; i = i + 1) rd8(4'h5, v);

	// READ ID starts with fresh command status.  It must not leak the
	// preceding wrong-cylinder failure into its own seven-byte result.
	wr8(4'h5, 8'h4A); wr8(4'h5, 8'h00);
	repeat (20) @(posedge clk);
	rd8(4'h5, v);
	check(v[7:6] == 2'b00, "read id clears stale ST0 termination status");
	rd8(4'h5, v);
	check(v == 0, "read id clears stale ST1 status");
	rd8(4'h5, v);
	check(v == 0, "read id clears stale ST2 status");
	for (i = 0; i < 4; i = i + 1) rd8(4'h5, v);

	// Every software reset path must abandon a partial FIFO command.
	wr8(4'h5, 8'h03); wr8(4'h5, 8'hDF);
	wr8(4'h4, 8'h80);             // DSR software reset
	wr8(4'h5, 8'h10);             // VERSION is now a fresh command
	rd8(4'h5, v);
	check(v == 8'h90, "DSR reset abandons a partial command");

	// FORMAT consumes C/H/R/N descriptors and zeroes their sectors.
	wr8(4'h7, 8'h00);             // 500 kbit/s for 1.44 MB
	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	host[0] = 8'd5; host[1] = 0; host[2] = 1; host[3] = 2;
	run_channel;
	while (sd_writes < 2) @(posedge clk);
	while (sd_wr_act) @(posedge clk);
	repeat (30) @(posedge clk);
	bad = 0;
	for (i = 0; i < 512; i = i + 1)
		if (disk[180*512 + i] !== 0) bad = bad + 1;
	check(bad == 0, "format zeroes the described sector");
	$display("  format LBA = %0d", last_lba);
	for (i = 0; i < 7; i = i + 1) rd8(4'h5, v);

	// get_logical_sec() validates every sector, not only the command's
	// starting R byte.  A transfer which reaches sector 19 on an 18-sector
	// track must stop before it accesses the first sector of the next head.
	bad = sd_reads;
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'd18); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'd19); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	run_channel;
	repeat (1600) @(posedge clk);
	check(sd_reads == bad + 1,
	      "read stops before fetching a sector beyond the track");
	check(int_floppy && !dma_req,
	      "read past EOT terminates instead of requesting another DMA sector");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[7], "read past EOT reports ST1 end-of-cylinder");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	reset_controller_no_poll;

	for (i = 0; i < 512; i = i + 1) host[i] = 8'hC0 ^ i[7:0];
	bad = sd_writes;
	wr8(4'h5, 8'h45); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'd18); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'd19); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	run_channel;
	while (sd_writes == bad) @(posedge clk);
	while (sd_wr_act) @(posedge clk);
	repeat (20) @(posedge clk);
	check(sd_writes == bad + 1,
	      "write commits only the valid last sector of the track");
	check(int_floppy && !dma_req,
	      "write past EOT terminates instead of requesting cross-track data");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[7], "write past EOT reports ST1 end-of-cylinder");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	reset_controller_no_poll;

	// FORMAT differs from READ/WRITE in the C model: it consumes the next
	// four-byte C/H/R/N descriptor, then rejects its sector before zeroing or
	// writing it.  Exercise all 18 valid descriptors plus the rejected 19th.
	bad = sd_writes;
	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'd19); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	for (i = 1; i <= 19; i = i + 1) begin
		host[0] = 8'd5; host[1] = 0; host[2] = i[7:0]; host[3] = 2;
		run_channel;
	end
	repeat (2000) @(posedge clk);
	check(sd_writes == bad + 18,
	      "format rejects the first descriptor beyond the track before writing");
	check(int_floppy && !dma_req, "format past the track terminates cleanly");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[7], "format past the track reports ST1 end-of-cylinder");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	reset_controller_no_poll;

	// If a medium disappears after FORMAT has begun, its terminal no-media
	// result must retire FORMAT mode.  Otherwise the next ordinary READ uses
	// a four-byte DMA length and interprets sector data as a format descriptor.
	bad = sd_writes;
	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h02); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	host[0] = 8'd5; host[1] = 0; host[2] = 1; host[3] = 2;
	run_channel;
	wr8(4'h8, 8'h80);             // eject while the first sector is retiring
	repeat (2000) @(posedge clk);
	check(int_floppy, "an ejected in-flight format terminates");
	check(sd_writes == bad, "an ejected in-flight format does not write the old slot");
	check(!dut.format_mode, "a terminal format abort clears format mode");
	if (int_floppy)
		for (i = 0; i < 7; i = i + 1) rd8(4'h5, v);

	img_size = BLOCKS*512;
	img_readonly = 0;
	img_mounted = 1; @(posedge clk); img_mounted = 0;
	repeat (20) @(posedge clk);
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h00); wr8(4'h5, 8'h00);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'h01); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	while (!dma_req) @(posedge clk);
	check(buf_len == 11'd512,
	      "ordinary read after aborted format retains a full sector DMA length");
	reset_controller_no_poll;
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08); rd8(4'h5, v); rd8(4'h5, v);

	// FORMAT also calls get_logical_sec() before starting its descriptor
	// loop.  A seek can leave the drive at cylinder 80, but formatting there
	// must fail before requesting descriptor DMA or touching the image.
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h00); wr8(4'h5, 8'd80);
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08); rd8(4'h5, v); rd8(4'h5, v);
	bad = sd_writes;
	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	repeat (20) @(posedge clk);
	check(int_floppy && !dma_req,
	      "format rejects an out-of-range cylinder before descriptor DMA");
	check(sd_writes == bad, "out-of-range cylinder format does not touch the image");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[2], "out-of-range cylinder format reports ST1 no-data");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	reset_controller_no_poll;
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08); rd8(4'h5, v); rd8(4'h5, v);

	// A channel which never acknowledges the sector must not wedge the
	// controller forever; floppy.c reports an overrun/underrun result.
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'h02); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h02); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	while (!dma_req) @(posedge clk);
	repeat (100010) @(posedge clk);
	check(int_floppy && !dma_req, "a stalled DMA transfer times out");
	rd8(4'h5, v); rd8(4'h5, v);
	check(v[4], "a stalled DMA transfer reports ST1 overrun");
	for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);

	// ST0 encodes the selected drive in bits 1:0 and the head in bit 2.
	// Exercise drive 1/head 1 through an immediate wrong-cylinder result.
	img_size = BLOCKS*512;
	img_mounted = 2'b10;
	@(posedge clk);
	img_mounted = 0;
	repeat (20) @(posedge clk);

	// DOR reads reconstruct all motor bits from the per-drive spinning
	// flags.  Selecting drive 1 and starting its motor does not implicitly
	// stop drive 0 merely because the raw byte no longer contains MOT0EN.
	wr8(4'h2, 8'h0C);             // force a drive 0 motor transition
	wr8(4'h2, 8'h1C);             // drive 0 spinning
	wr8(4'h2, 8'h2D);             // drive 1 spinning, selected
	rd8(4'h2, v);
	check(v == 8'h3D, "DOR read reconstructs both drives' running motors");
	wr8(4'h2, 8'h0D);             // stop selected drive 1 only
	rd8(4'h2, v);
	check(v == 8'h1D, "DOR read retains drive 0 motor after stopping drive 1");
	wr8(4'h2, 8'h1C);             // restore drive 0 selection

	wr8(4'h7, 8'h00);
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h05); wr8(4'h5, 8'h01);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h01); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	repeat (20) @(posedge clk);
	rd8(4'h5, v);
	check(v == 8'h45, "drive 1/head 1 result encodes ST0 as 0x45");
	for (i = 0; i < 6; i = i + 1) rd8(4'h5, v);

	// SEEK keeps its completion cylinder for the drive that owned the
	// command, even while the DOR remains on drive 0.  The reference's
	// seek status contains only SEEK END; drive/head bits are added only
	// to the seven-byte read/write result path.
	wr8(4'h5, 8'h0F); wr8(4'h5, 8'h05); wr8(4'h5, 8'd7);
	repeat (400) @(posedge clk);
	check(int_floppy, "drive 1 seek raises its completion interrupt");
	wr8(4'h5, 8'h08);
	rd8(4'h5, v);
	check(v == 8'h20, "drive 1 seek status matches the reference");
	rd8(4'h5, v);
	check(v == 8'd7, "sense interrupt returns the command drive cylinder");

	wr8(4'h5, 8'h07); wr8(4'h5, 8'h01);       // recalibrate drive 1
	repeat (400) @(posedge clk);
	wr8(4'h5, 8'h08);
	rd8(4'h5, v);
	check(v == 8'h20, "drive 1 recalibrate status matches the reference");
	rd8(4'h5, v);
	check(v == 8'd0, "drive 1 recalibrate reports cylinder zero");

	// floppy.c validates the complete eight-bit N byte.  Folding it to
	// three bits accepts malformed values such as 0x82 as ordinary 512-byte
	// transfers and lets them reach the disk/DMA path.
	wr8(4'h5, 8'h46); wr8(4'h5, 8'h00); wr8(4'h5, 8'd5);
	wr8(4'h5, 8'h00); wr8(4'h5, 8'h01); wr8(4'h5, 8'h82);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'hFF);
	repeat (20) @(posedge clk);
	check(int_floppy && !dma_req, "read rejects a high-bit alias of its block size");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[2], "bad read block size reports ST1 no-data");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	wr8(4'h4, 8'h80);             // abandon a falsely accepted command

	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h82);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	repeat (20) @(posedge clk);
	check(int_floppy && !dma_req, "format rejects a high-bit alias of its block size");
	if (int_floppy) begin
		rd8(4'h5, v); rd8(4'h5, v);
		check(v[2], "bad format block size reports ST1 no-data");
		for (i = 0; i < 5; i = i + 1) rd8(4'h5, v);
	end
	wr8(4'h4, 8'h80);             // abandon a falsely accepted command

	// The same full-byte comparison applies to each C/H/R/N descriptor.
	// A bad descriptor stops FORMAT without writing the addressed sector.
	bad = sd_writes;
	wr8(4'h5, 8'h0D); wr8(4'h5, 8'h00); wr8(4'h5, 8'h02);
	wr8(4'h5, 8'h01); wr8(4'h5, 8'h1B); wr8(4'h5, 8'h00);
	host[0] = 8'd5; host[1] = 0; host[2] = 1; host[3] = 8'h82;
	run_channel;
	repeat (2000) @(posedge clk);
	check(int_floppy, "bad format descriptor completes with a result");
	check(sd_writes == bad, "bad format descriptor never reaches the disk");
	if (int_floppy) begin
		rd8(4'h5, v);
		check(v == 8'h00, "bad format descriptor preserves normal ST0");
		rd8(4'h5, v);
		check(v == 8'h00, "bad format descriptor preserves clear ST1");
		rd8(4'h5, v);
		check(v == 8'h00, "bad format descriptor preserves clear ST2");
		for (i = 0; i < 4; i = i + 1) rd8(4'h5, v);
	end
	wr8(4'h4, 8'h80);

	// an opcode the controller does not implement
	wr8(4'h5, 8'h1F);
	repeat (20) @(posedge clk);
	check(int_floppy, "invalid command raises its completion interrupt");
	rd8(4'h5, v);
	check(v == 8'h80, "invalid command reports invalid opcode");

	// CTRL_RESET runs before CTRL_EJECT in floppy.c: reset selects drive 0,
	// then eject invalidates that medium's size and block-size geometry.  A
	// stale DOR selects/ejects drive 1 instead and lets READ ID falsely pass.
	wr8(4'h2, 8'h2D);             // select drive 1, motor 1 on
	wr8(4'h8, 8'hA0);             // controller reset and eject together
	rd8(4'h2, v);
	check(v == 8'h00, "external controller reset clears DOR before eject");
	wr8(4'h7, 8'h00);
	wr8(4'h5, 8'h4A); wr8(4'h5, 8'h00);
	repeat (20) @(posedge clk);
	rd8(4'h5, v);
	check(v == 8'h40, "read id after eject reports abnormal termination");
	rd8(4'h5, v);
	check(v == 8'h04, "read id after eject reports no data");
	for (i = 0; i < 4; i = i + 1) rd8(4'h5, v);
	rd8(4'h5, v);
	check(v == 8'h00, "read id after eject exposes the cleared block size");
	wr8(4'h2, 8'h2D);             // drive 1 was not the ejection target
	rd8(4'h8, v);
	check(v[1:0] == 2'd2, "reset plus eject preserves the other drive's medium");

	// A drive and the removable medium in it are separate things.  Once
	// drive 1 has proved that it is fitted, ejecting its disk must leave a
	// fitted, empty drive: CTRL_DRV_ID stays clear, the SRA fitted bit stays
	// clear, and commands which require only a drive still run.
	wr8(4'h8, 8'h80);             // eject drive 1 selected by the DOR
	repeat (2) @(posedge clk);     // let the command retire before reset
	reset = 1;                    // a system reset must not forget the fixture
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);
	wr8(4'h2, 8'h0D);             // select drive 1 after reset
	rd8(4'h8, v);
	check(!v[2] && v[1:0] == 2'd0,
	      "eject leaves drive 1 fitted but empty");
	rd8(4'h0, v);
	check(!v[6], "eject does not turn a fitted drive 1 into no drive");
	wr8(4'h5, 8'h07); wr8(4'h5, 8'h01);
	rd8(4'h4, msr);
	check(msr[1], "an empty fitted drive becomes busy on recalibrate");
	repeat (400) @(posedge clk);
	check(int_floppy, "an empty fitted drive completes recalibrate");
	if (int_floppy) begin
		wr8(4'h5, 8'h08);
		rd8(4'h5, v);
		check(v == 8'h20, "empty drive recalibrate reports seek end");
		rd8(4'h5, v);
		check(v == 8'h00, "empty drive recalibrate reports cylinder zero");
	end

	// Leaving reset through DOR RESET_N produces one poll interrupt after
	// exactly one second.  It has no result phase of its own; SENSE
	// INTERRUPT STATUS observes the clean reset ST0/PCN and acknowledges it.
	wr8(4'h2, 8'h08);             // enter reset
	wr8(4'h2, 8'h0C);             // leave reset and arm the poll
	repeat (999900) @(posedge clk);
	check(!int_floppy, "reset poll does not interrupt before one second");
	repeat (200) @(posedge clk);
	rd8(4'h4, msr);
	check(int_floppy && msr == 8'h80,
	      "DOR reset release raises the one-second poll interrupt");
	wr8(4'h5, 8'h08);
	rd8(4'h5, v);
	check(v == 8'h00, "reset poll sense returns clean ST0");
	rd8(4'h5, v);
	check(v == 8'h00, "reset poll sense returns cylinder zero");
	check(!int_floppy, "reset poll sense clears the request");

	// DSR bit 7 is the other 82077 stop/start reset path and arms the same
	// poll.  The external NeXT CTRL_RESET is a generic controller reset and
	// deliberately does not.
	wr8(4'h4, 8'h80);
	repeat (1000100) @(posedge clk);
	check(int_floppy, "DSR reset raises the one-second poll interrupt");
	wr8(4'h5, 8'h08); rd8(4'h5, v); rd8(4'h5, v);

	// The ROM sends exactly 13 00 58 00 after reset; bit 4 of 0x58 disables
	// polling and must cancel the pending event before its deadline.
	wr8(4'h2, 8'h08); wr8(4'h2, 8'h0C);
	wr8(4'h5, 8'h13); wr8(4'h5, 8'h00);
	wr8(4'h5, 8'h58); wr8(4'h5, 8'h00);
	repeat (1000100) @(posedge clk);
	check(!int_floppy, "ROM CONFIGURE disables the pending reset poll");
	wr8(4'h8, 8'h20);             // CTRL_RESET, not a reset release
	repeat (1000100) @(posedge clk);
	check(!int_floppy, "external controller reset does not arm a poll");

	// In floppy.c an interrupt-producing command replaces the single reset
	// event.  These commands complete immediately in this RTL, so consuming
	// their result must also discard the old poll state.
	wr8(4'h2, 8'h08); wr8(4'h2, 8'h0C);
	wr8(4'h5, 8'h1F);
	repeat (2) @(posedge clk);
	check(int_floppy, "invalid command replaces a pending reset poll");
	rd8(4'h5, v);
	repeat (1000100) @(posedge clk);
	check(!int_floppy, "invalid result leaves no stale reset poll");

	wr8(4'h2, 8'h08); wr8(4'h2, 8'h0C);
	wr8(4'h5, 8'h4A); wr8(4'h5, 8'h00); // empty drive 0: immediate no-data
	repeat (2) @(posedge clk);
	check(int_floppy, "read id replaces a pending reset poll");
	for (i = 0; i < 7; i = i + 1) rd8(4'h5, v);
	repeat (1000100) @(posedge clk);
	check(!int_floppy, "read id result leaves no stale reset poll");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
