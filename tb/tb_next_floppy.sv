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

reg         img_mounted = 0;
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
	.img_mounted(img_mounted), .img_readonly(1'b0), .img_size(img_size),
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
	if (sd_rd && !sd_ack && !sd_rd_act) begin
		sd_ack <= 1; sd_rd_act <= 1; sd_buff_addr <= 0; sd_buff_wr <= 0;
		last_lba <= sd_lba;
		sd_reads = sd_reads + 1;
	end
	else if (sd_ack && sd_rd_act) begin
		if (!sd_buff_wr) begin
			sd_buff_dout <= disk[{sd_lba[11:0], 9'd0} + {23'd0, sd_buff_addr}];
			sd_buff_wr <= 1;
		end
		else begin
			sd_buff_wr <= 0;
			if (sd_buff_addr == 9'd511) begin sd_ack <= 0; sd_rd_act <= 0; end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
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
integer i, bad;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	img_size = BLOCKS*512;          // 1.44 MB
	img_mounted = 1;
	@(posedge clk);
	img_mounted = 0;
	repeat (20) @(posedge clk);

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
	wr8(4'h2, 8'h1C);            // back to drive 0

	wr8(4'h2, 8'h0C);            // motor off
	rd8(4'h8, v);
	check(v[1:0] == 2'd0, "no media id while the motor is stopped");
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
	// head 1, sector 3 on cylinder 0 with 18 sectors per track is LBA 20
	bad = 0;
	for (i = 0; i < 512; i = i + 1)
		if (disk[20*512 + i] !== (8'hA0 + i[7:0])) bad = bad + 1;
	check(bad == 0, "the written sector landed at the right offset");

	// an opcode the controller does not implement
	wr8(4'h5, 8'h1F);
	repeat (20) @(posedge clk);
	rd8(4'h5, v);
	check(v == 8'h80, "invalid command reports invalid opcode");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
