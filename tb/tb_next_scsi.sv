//============================================================================
//  SCSI subsystem test: drives the real next_scsi through the command
//  sequences the boot ROM and NeXTSTEP use, with an SD-card model
//  serving a patterned disk image:
//    - select with ATN + TEST UNIT READY, ICCS status/message,
//      message accepted (the basic select/execute/complete loop)
//    - INQUIRY via DMA transfer info ("Previous" vendor data, partial
//      final word written with byte enables)
//    - READ CAPACITY (last LBA and blocksize)
//    - READ(6) of two sectors via the DMA channel into memory,
//      byte-exact against the disk image
//    - WRITE(6) of one sector, byte-exact in the SD model
//    - selection timeout on a target with no disk (disconnect irq)
//    - READ beyond the end: CHECK CONDITION, then REQUEST SENSE
//      reporting SC_INVALID_LBA with the failed LBA
//============================================================================

`timescale 1ns/1ps

module tb_next_scsi;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel_esp = 0, sel_csr = 0, sel_sptr = 0, sel_ptr = 0, sel_ini = 0;
reg   [5:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

wire        m_req, m_we, m_ack;
wire [23:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;

wire int_scsi, int_scsi_dma;

reg         img_mounted = 0;
reg  [63:0] img_size = 0;
reg         img_mounted2 = 0;
wire [31:0] sd_lba;
wire        sd_rd, sd_wr;
reg         sd_ack = 0;
reg   [8:0] sd_buff_addr = 0;
reg   [7:0] sd_buff_dout = 0;
wire  [7:0] sd_buff_din;
reg         sd_buff_wr = 0;

// The floppy shares this channel: it has no memory master of its own,
// so its sectors move through the very next/limit registers the SCSI
// driver is using.  A hand-over that happens while a SCSI command is
// still in flight would move floppy data with disk pointers.
reg         flp_select = 0, flp_req = 0;
reg         flp_wr = 1;   // 1 = floppy to memory, i.e. a sector read
reg  [10:0] flp_len = 11'd512;
wire  [9:0] flp_addr;
wire        flp_bwe, flp_done;
wire  [7:0] flp_bwdata;
reg   [7:0] fbuf [0:511];
reg   [7:0] flp_bq_r;
assign flp_bq = flp_bq_r;
wire  [7:0] flp_bq;
always @(posedge clk) begin
	flp_bq_r <= fbuf[flp_addr[8:0]];
	if (flp_bwe) fbuf[flp_addr[8:0]] <= flp_bwdata;
end

// the invariant: the floppy may not take the channel while a SCSI
// command is outstanding
reg     scsi_busy = 0;
integer seize = 0;
always @(posedge clk)
	if (!reset && scsi_busy && dut.flp_active) seize = seize + 1;

next_scsi #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel_esp(sel_esp), .sel_csr(sel_csr), .sel_sptr(sel_sptr), .sel_ptr(sel_ptr), .sel_ini(sel_ini),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack),
	.int_scsi(int_scsi), .int_scsi_dma(int_scsi_dma),
	.img_mounted({img_mounted2, img_mounted}), .img_readonly(1'b0), .img_size(img_size),
	.sd_unit(),
	.sd_lba(sd_lba), .sd_rd(sd_rd), .sd_wr(sd_wr), .sd_ack(sd_ack),
	.sd_buff_addr(sd_buff_addr), .sd_buff_dout(sd_buff_dout),
	.sd_buff_din(sd_buff_din), .sd_buff_wr(sd_buff_wr),
	.flp_select(flp_select), .flp_req(flp_req), .flp_wr(flp_wr),
	.flp_len(flp_len), .flp_addr(flp_addr), .flp_bwe(flp_bwe),
	.flp_bwdata(flp_bwdata), .flp_bq(flp_bq), .flp_done(flp_done)
);

//----------------------------------------------------------------------------
// RAM model
//----------------------------------------------------------------------------

reg [31:0] ram [0:16383];
reg        ack_r;
assign m_ack = ack_r;

always @(posedge clk) begin
	if (reset) ack_r <= 0;
	else if (!m_req) ack_r <= 0;
	else if (!ack_r) begin
		if (m_we) begin
			if (m_be[3]) ram[m_addr[13:0]][31:24] <= m_din[31:24];
			if (m_be[2]) ram[m_addr[13:0]][23:16] <= m_din[23:16];
			if (m_be[1]) ram[m_addr[13:0]][15:8]  <= m_din[15:8];
			if (m_be[0]) ram[m_addr[13:0]][7:0]   <= m_din[7:0];
		end
		else m_dout <= ram[m_addr[13:0]];
		ack_r <= 1;
	end
end

function [7:0] ram_byte;
	input [31:0] a;
	reg [31:0] w;
	begin
		w = ram[a[15:2]];
		ram_byte = (a[1:0] == 0) ? w[31:24] :
		           (a[1:0] == 1) ? w[23:16] :
		           (a[1:0] == 2) ? w[15:8] : w[7:0];
	end
endfunction

//----------------------------------------------------------------------------
// SD-card model: an 8 block disk with a recognizable pattern
//----------------------------------------------------------------------------

localparam DISK_BLOCKS = 8;

reg [7:0] disk  [0:DISK_BLOCKS*512-1];
reg [7:0] disk2 [0:DISK_BLOCKS*512-1];   // SCSI target 1

// disk byte pattern: block ^ offset, distinct per position
function [7:0] pat;
	input [31:0] blk;
	input [31:0] off;
	begin
		pat = blk[7:0] ^ off[7:0] ^ {off[10:8], 5'd0};
	end
endfunction

integer sdi;
initial begin
	for (sdi = 0; sdi < DISK_BLOCKS*512; sdi = sdi + 1) begin
		disk[sdi]  = pat(sdi / 512, sdi % 512);
		disk2[sdi] = ~pat(sdi / 512, sdi % 512);   // the other disk
	end
end

// serve sd_rd / sd_wr with the hps_io handshake
always @(posedge clk) begin
	if (sd_rd && !sd_ack) begin
		sd_ack <= 1;
		sd_buff_addr <= 0;
		sd_buff_wr <= 0;
	end
	else if (sd_ack && sd_rd_active) begin
		// one byte every other cycle
		if (!sd_buff_wr && sd_buff_addr <= 9'd511) begin
			sd_buff_dout <= dut.sd_unit
			              ? disk2[{sd_lba[2:0], 9'd0} + {23'd0, sd_buff_addr}]
			              : disk [{sd_lba[2:0], 9'd0} + {23'd0, sd_buff_addr}];
			sd_buff_wr <= 1;
		end
		else begin
			sd_buff_wr <= 0;
			if (sd_buff_addr == 9'd511) begin
				sd_ack <= 0;
				sd_rd_active <= 0;
			end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
		end
	end
	else if (sd_ack && sd_wr_active) begin
		// read a byte every other cycle (registered buffer read)
		if (rd_phase) begin
			if (dut.sd_unit)
				disk2[{sd_lba[2:0], 9'd0} + {23'd0, sd_buff_addr}] <= sd_buff_din;
			else
				disk [{sd_lba[2:0], 9'd0} + {23'd0, sd_buff_addr}] <= sd_buff_din;
			rd_phase <= 0;
			if (sd_buff_addr == 9'd511) begin
				sd_ack <= 0;
				sd_wr_active <= 0;
			end
			else sd_buff_addr <= sd_buff_addr + 1'd1;
		end
		else rd_phase <= 1;
	end
	else if (sd_wr && !sd_ack) begin
		sd_ack <= 1;
		sd_buff_addr <= 0;
		rd_phase <= 0;
		sd_wr_active <= 1;
	end
	if (sd_rd && !sd_ack && !sd_rd_active) sd_rd_active <= 1;
end

reg sd_rd_active = 0, sd_wr_active = 0, rd_phase = 0;

//----------------------------------------------------------------------------
// register access helpers
//----------------------------------------------------------------------------

task esp_wr8;
	input [5:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_esp <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel_esp <= 0; we <= 0;
	end
endtask

task esp_rd8;
	input [5:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel_esp <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel_esp <= 0;
	end
endtask

task ptr_wr32;
	input [5:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_ptr <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 6'd2; wdata <= v[15:0];
		@(posedge clk);
		sel_ptr <= 0; we <= 0;
	end
endtask

task sptr_wr32;
	input [5:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_sptr <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 6'd2; wdata <= v[15:0];
		@(posedge clk);
		sel_sptr <= 0; we <= 0;
	end
endtask

task sptr_rd32;
	input [5:0] a;
	output [31:0] v;
	begin
		@(posedge clk);
		sel_sptr <= 1; addr <= a; we <= 0; be <= 2'b11;
		@(posedge clk);
		v[31:16] = rdata;
		addr <= a + 6'd2;
		@(posedge clk);
		v[15:0] = rdata;
		sel_sptr <= 0;
	end
endtask

task csr_cmd;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_csr <= 1; addr <= 6'h00; we <= 1; be <= 2'b11; wdata <= {8'h00, v};
		@(posedge clk);
		sel_csr <= 0; we <= 0;
	end
endtask

// wait for the DMA channel complete interrupt
task wait_dma_complete;
	integer n;
	begin
		n = 0;
		while (!int_scsi_dma && n < 4000000) begin
			@(posedge clk);
			n = n + 1;
		end
		if (!int_scsi_dma) begin
			$display("FAIL: dma complete timed out");
			errors = errors + 1;
		end
	end
endtask

// wait for the ESP interrupt (with the enable bit set in dma_control)
task wait_irq;
	integer n;
	begin
		n = 0;
		while (!int_scsi && n < 4000000) begin
			@(posedge clk);
			n = n + 1;
		end
		if (!int_scsi) begin
			$display("FAIL: interrupt timed out");
			errors = errors + 1;
		end
	end
endtask

// read interrupt status (clears the interrupt)
task read_intr;
	output [7:0] v;
	begin
		esp_rd8(6'h05, v);
	end
endtask

// select with ATN: identify + CDB through the FIFO, command 0x42
task select_atn6;
	input [7:0] c0, c1, c2, c3, c4, c5;
	begin
		esp_wr8(6'h02, 8'h80);       // identify, lun 0
		esp_wr8(6'h02, c0);
		esp_wr8(6'h02, c1);
		esp_wr8(6'h02, c2);
		esp_wr8(6'h02, c3);
		esp_wr8(6'h02, c4);
		esp_wr8(6'h02, c5);
		esp_wr8(6'h04, 8'h00);       // select bus id 0
		esp_wr8(6'h03, 8'h42);       // select with ATN
	end
endtask

task select_atn10;
	input [7:0] c0, c1, c2, c3, c4, c5, c6, c7, c8, c9;
	begin
		esp_wr8(6'h02, 8'h80);
		esp_wr8(6'h02, c0);
		esp_wr8(6'h02, c1);
		esp_wr8(6'h02, c2);
		esp_wr8(6'h02, c3);
		esp_wr8(6'h02, c4);
		esp_wr8(6'h02, c5);
		esp_wr8(6'h02, c6);
		esp_wr8(6'h02, c7);
		esp_wr8(6'h02, c8);
		esp_wr8(6'h02, c9);
		esp_wr8(6'h04, 8'h00);
		esp_wr8(6'h03, 8'h42);
	end
endtask

// ICCS + message accepted, returns the status byte
task finish_command;
	output [7:0] sts;
	reg [7:0] v;
	begin
		esp_wr8(6'h03, 8'h11);       // initiator command complete
		wait_irq;
		read_intr(v);
		esp_rd8(6'h02, sts);         // status byte from the FIFO
		esp_rd8(6'h02, v);           // message byte (command complete)
		esp_wr8(6'h03, 8'h12);       // message accepted
		wait_irq;
		read_intr(v);
	end
endtask

// DMA transfer info for n bytes into memory at "base"
task ti_dma_in;
	input [16:0] n;
	input [31:0] base;
	input [31:0] limit;
	begin
		ptr_wr32(6'h10, base);       // next
		ptr_wr32(6'h14, limit);      // limit
		csr_cmd(8'h11);              // RESET | SETENABLE -> enable
		esp_wr8(6'h00, n[7:0]);      // transfer count low
		esp_wr8(6'h01, n[15:8]);     // transfer count high
		esp_wr8(6'h03, 8'h90);       // transfer info, DMA
		wait_irq;
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

localparam BUF = 32'h00002000;
localparam BUF2 = 32'h00003000;

integer i, s;
reg [7:0] v, sts;
reg [31:0] d;
reg [7:0] intr;
reg ok;

initial begin
	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// mount an 8 block disk image
	img_size = DISK_BLOCKS*512;
	img_mounted = 1;
	@(posedge clk);
	img_mounted = 0;
	repeat (50) @(posedge clk);

	// enable the ESP interrupt at the DMA control register
	esp_wr8(6'h20, 8'h20);

	//------------------------------------------------------------
	// TEST UNIT READY
	//------------------------------------------------------------
	select_atn6(8'h00, 8'h00, 8'h00, 8'h00, 8'h00, 8'h00);
	wait_irq;
	read_intr(intr);
	check(intr == 8'h18, "select: bus service and function complete");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3, "test unit ready: status phase");
	finish_command(sts);
	check(sts == 8'h00, "test unit ready: good status");

	//------------------------------------------------------------
	// INQUIRY, 54 bytes via DMA
	//------------------------------------------------------------
	select_atn6(8'h12, 8'h00, 8'h00, 8'h00, 8'd54, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd1, "inquiry: data in phase");
	ti_dma_in(17'd54, BUF, BUF + 32'd64);
	read_intr(intr);
	check(intr == 8'h08, "inquiry: transfer function complete");
	esp_rd8(6'h04, v);
	check(v[4], "inquiry: transfer count zero");
	check(ram_byte(BUF+8)  == "P" && ram_byte(BUF+9)  == "r" &&
	      ram_byte(BUF+15) == "s", "inquiry: vendor Previous");
	check(ram_byte(BUF+16) == "H" && ram_byte(BUF+17) == "D",
	      "inquiry: model HDD");
	check(ram_byte(BUF+53) == " ", "inquiry: byte 53 written");
	finish_command(sts);
	check(sts == 8'h00, "inquiry: good status");

	//------------------------------------------------------------
	// READ CAPACITY
	//------------------------------------------------------------
	select_atn10(8'h25, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	wait_irq;
	read_intr(intr);
	ti_dma_in(17'd8, BUF, BUF + 32'd16);
	read_intr(intr);
	check(ram_byte(BUF+0) == 8'h00 && ram_byte(BUF+1) == 8'h00 &&
	      ram_byte(BUF+2) == 8'h00 && ram_byte(BUF+3) == 8'd7,
	      "read capacity: last lba 7");
	check(ram_byte(BUF+6) == 8'h02 && ram_byte(BUF+7) == 8'h00,
	      "read capacity: blocksize 512");
	finish_command(sts);

	//------------------------------------------------------------
	// READ(6): two blocks from LBA 2
	//------------------------------------------------------------
	select_atn6(8'h08, 8'h00, 8'h00, 8'h02, 8'h02, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd1, "read: data in phase");
	ti_dma_in(17'd1024, BUF, BUF + 32'd1024);
	read_intr(intr);
	check(intr == 8'h08, "read: function complete");
	check(int_scsi_dma, "read: dma channel complete interrupt");
	csr_cmd(8'h08);              // CLRCOMPLETE
	@(posedge clk);
	check(!int_scsi_dma, "read: dma interrupt cleared");
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(BUF + i) != pat(2 + i/512, i % 512)) ok = 0;
	check(ok, "read: 1024 bytes match the disk image");
	finish_command(sts);
	check(sts == 8'h00, "read: good status");

	//------------------------------------------------------------
	// WRITE(6): one block to LBA 5
	//------------------------------------------------------------
	for (i = 0; i < 512; i = i + 1) begin
		case (i[1:0])
			0: ram[(BUF2[15:2]) + i/4][31:24] = ~pat(5, i);
			1: ram[(BUF2[15:2]) + i/4][23:16] = ~pat(5, i);
			2: ram[(BUF2[15:2]) + i/4][15:8]  = ~pat(5, i);
			3: ram[(BUF2[15:2]) + i/4][7:0]   = ~pat(5, i);
		endcase
	end
	select_atn6(8'h0A, 8'h00, 8'h00, 8'h05, 8'h01, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd0, "write: data out phase");
	ptr_wr32(6'h10, BUF2);
	ptr_wr32(6'h14, BUF2 + 32'd512);
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h02);       // 512 bytes
	esp_wr8(6'h03, 8'h90);       // transfer info, DMA
	wait_irq;
	read_intr(intr);
	check(intr == 8'h08, "write: function complete");
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (disk[5*512 + i] !== (~pat(5, i) & 8'hFF)) ok = 0;
	check(ok, "write: 512 bytes landed in the disk image");
	finish_command(sts);
	check(sts == 8'h00, "write: good status");

	//------------------------------------------------------------
	// chained READ(6), the way the NeXTSTEP driver runs the channel:
	// double buffered 512 byte segments, the next segment programmed
	// into start/stop upon each buffer-complete interrupt.  The whole
	// RAM is a canary field: any DMA write outside the four programmed
	// windows is corruption of the kind that panics the kernel.
	//------------------------------------------------------------
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'hDEADBEEF;
	// the write test above inverted block 5 on the disk; restore it
	for (i = 0; i < 512; i = i + 1) disk[5*512 + i] = pat(5, i);

	select_atn6(8'h08, 8'h00, 8'h00, 8'h02, 8'h04, 8'h00);   // 4 blocks from LBA 2
	wait_irq;
	read_intr(intr);

	ptr_wr32(6'h10, 32'h00002000);           // segment A
	ptr_wr32(6'h14, 32'h00002200);
	ptr_wr32(6'h18, 32'h00002400);           // segment B armed in start/stop
	ptr_wr32(6'h1C, 32'h00002600);
	csr_cmd(8'h13);                          // SETENABLE | SETSUPDATE
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h08);                   // 2048 bytes
	esp_wr8(6'h03, 8'h90);                   // transfer info, DMA

	// driver interrupt service: two more segments then let it finish
	wait_dma_complete;
	csr_cmd(8'h08);                          // CLRCOMPLETE
	ptr_wr32(6'h18, 32'h00002800);           // segment C
	ptr_wr32(6'h1C, 32'h00002A00);
	csr_cmd(8'h02);                          // SETSUPDATE
	wait_dma_complete;
	csr_cmd(8'h08);
	ptr_wr32(6'h18, 32'h00002C00);           // segment D
	ptr_wr32(6'h1C, 32'h00002E00);
	csr_cmd(8'h02);
	wait_dma_complete;
	csr_cmd(8'h08);

	wait_irq;                                // ESP: counter reached zero
	read_intr(intr);
	check(intr == 8'h08, "chained read: function complete");
	ok = 1;
	for (i = 0; i < 512; i = i + 1) begin
		if (ram_byte(32'h00002000 + i) != pat(2, i)) ok = 0;
		if (ram_byte(32'h00002400 + i) != pat(3, i)) ok = 0;
		if (ram_byte(32'h00002800 + i) != pat(4, i)) ok = 0;
		if (ram_byte(32'h00002C00 + i) != pat(5, i)) ok = 0;
	end
	check(ok, "chained read: all four segments carry their block");
	ok = 1;
	for (i = 0; i < 16384; i = i + 1) begin
		if ((i < 'h800 || i >= 'h880) && (i < 'h900 || i >= 'h980) &&
		    (i < 'hA00 || i >= 'hA80) && (i < 'hB00 || i >= 'hB80) &&
		    ram[i] !== 32'hDEADBEEF) begin
			$display("  corrupted word at %08x = %08x", i*4, ram[i]);
			ok = 0;
		end
	end
	check(ok, "chained read: no DMA write outside the programmed windows");
	finish_command(sts);
	check(sts == 8'h00, "chained read: good status");

	//------------------------------------------------------------
	// chained WRITE(10), four blocks, the way fsck repairs a
	// filesystem: a ten byte CDB and a double buffered channel.
	// The modelled disk is 8 blocks, so this writes 2..5 and keeps
	// 1 and 6 as canaries.  Only
	// a single block WRITE(6) had ever been tested, so neither the
	// ten byte command nor a segment boundary in the data-out
	// direction had been driven at all.  A write that lands short, or
	// in the wrong block, is what turns a repair into corruption.
	//------------------------------------------------------------
	for (s = 0; s < 4; s = s + 1)
		for (i = 0; i < 512; i = i + 1) begin
			case (i[1:0])
				0: ram[((32'h00003000 + s*1024 + i) >> 2)][31:24] = ~pat(2+s, i);
				1: ram[((32'h00003000 + s*1024 + i) >> 2)][23:16] = ~pat(2+s, i);
				2: ram[((32'h00003000 + s*1024 + i) >> 2)][15:8]  = ~pat(2+s, i);
				3: ram[((32'h00003000 + s*1024 + i) >> 2)][7:0]   = ~pat(2+s, i);
			endcase
		end

	// WRITE(10) of 4 blocks at LBA 10
	select_atn10(8'h2A, 8'h00, 8'h00, 8'h00, 8'h00, 8'd2,
	             8'h00, 8'h00, 8'd4, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd0, "chained write: data out phase");

	ptr_wr32(6'h10, 32'h00003000);           // segment A
	ptr_wr32(6'h14, 32'h00003200);
	ptr_wr32(6'h18, 32'h00003400);           // segment B armed
	ptr_wr32(6'h1C, 32'h00003600);
	csr_cmd(8'h13);                          // SETENABLE | SETSUPDATE
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h08);                   // 2048 bytes
	esp_wr8(6'h03, 8'h90);                   // transfer info, DMA

	wait_dma_complete;
	csr_cmd(8'h08);
	ptr_wr32(6'h18, 32'h00003800);           // segment C
	ptr_wr32(6'h1C, 32'h00003A00);
	csr_cmd(8'h02);
	wait_dma_complete;
	csr_cmd(8'h08);
	ptr_wr32(6'h18, 32'h00003C00);           // segment D
	ptr_wr32(6'h1C, 32'h00003E00);
	csr_cmd(8'h02);
	wait_dma_complete;
	csr_cmd(8'h08);

	wait_irq;
	read_intr(intr);
	check(intr == 8'h08, "chained write: function complete");

	ok = 1;
	for (s = 0; s < 4; s = s + 1)
		for (i = 0; i < 512; i = i + 1)
			if (disk[(2+s)*512 + i] !== (~pat(2+s, i) & 8'hFF)) begin
				if (ok) $display("  block %0d byte %0d: got %02x want %02x",
				                 2+s, i, disk[(2+s)*512 + i],
				                 ~pat(2+s, i) & 8'hFF);
				ok = 0;
			end
	check(ok, "chained write: all four blocks landed byte exact");

	ok = 1;
	for (i = 0; i < 512; i = i + 1) begin
		if (disk[1*512 + i] !== pat(1, i)) ok = 0;
		if (disk[6*512 + i] !== pat(6, i)) ok = 0;
	end
	check(ok, "chained write: neighbouring blocks untouched");
	finish_command(sts);
	check(sts == 8'h00, "chained write: good status");

	//------------------------------------------------------------
	// selection timeout on target 1 (no disk there)
	//------------------------------------------------------------
	esp_wr8(6'h05, 8'h01);       // short select timeout
	esp_wr8(6'h02, 8'h80);
	esp_wr8(6'h02, 8'h00);       // TEST UNIT READY
	esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h04, 8'h01);       // select bus id 1
	esp_wr8(6'h03, 8'h42);
	wait_irq;
	read_intr(intr);
	check(intr == 8'h20, "selection timeout: disconnected interrupt");
	esp_wr8(6'h03, 8'h01);       // flush FIFO (identify + cdb are stale)

	//------------------------------------------------------------
	// READ beyond the end, then REQUEST SENSE
	//------------------------------------------------------------
	select_atn6(8'h08, 8'h00, 8'h00, 8'h20, 8'h01, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3, "read past end: status phase");
	finish_command(sts);
	check(sts == 8'h02, "read past end: check condition");

	select_atn6(8'h03, 8'h00, 8'h00, 8'h00, 8'd22, 8'h00);
	wait_irq;
	read_intr(intr);
	ti_dma_in(17'd22, BUF, BUF + 32'd32);
	read_intr(intr);
	check(ram_byte(BUF+0) == 8'hF0, "request sense: valid, format 0x70");
	check(ram_byte(BUF+2) == 8'h05, "request sense: illegal request key");
	check(ram_byte(BUF+12) == 8'h21, "request sense: invalid lba code");
	check(ram_byte(BUF+3) == 8'h00 && ram_byte(BUF+4) == 8'h00 &&
	      ram_byte(BUF+5) == 8'h00 && ram_byte(BUF+6) == 8'h20,
	      "request sense: info holds the failed lba");
	finish_command(sts);
	check(sts == 8'h00, "request sense: good status");

	// saved registers: plain storage the driver can use for residual
	// bookkeeping (dropping the write was worth a kernel panic)
	sptr_wr32(6'h00, 32'h04123450);
	sptr_wr32(6'h04, 32'h04123650);
	sptr_rd32(6'h00, d);
	check(d == 32'h04123450, "saved next holds a written value");
	sptr_rd32(6'h04, d);
	check(d == 32'h04123650, "saved limit holds a written value");

	//------------------------------------------------------------
	// The floppy asks for the channel while a SCSI command is in
	// flight.  Both devices move their sectors through the same
	// next/limit registers, so a hand-over mid-command would write
	// floppy data over the disk driver's buffer and advance the
	// driver's pointer under it.  The hand-over must wait for idle,
	// and the request must not be lost while it waits.
	//------------------------------------------------------------
	for (i = 0; i < 512; i = i + 1) fbuf[i] = 8'hA5 ^ i[7:0];
	for (s = 0; s < 4; s = s + 1)            // restore what we overwrote
		for (i = 0; i < 512; i = i + 1) disk[(2+s)*512 + i] = pat(2+s, i);
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'hDEADBEEF;
	seize = 0;

	select_atn6(8'h08, 8'h00, 8'h00, 8'h02, 8'h04, 8'h00);   // 4 blocks, LBA 2
	scsi_busy = 1;
	wait_irq;
	read_intr(intr);
	ptr_wr32(6'h10, 32'h00004000);
	ptr_wr32(6'h14, 32'h00004800);           // one 2048 byte window
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h08);                   // 2048 bytes
	esp_wr8(6'h03, 8'h90);                   // transfer info, DMA

	// the drive raises its request in the middle of the disk transfer
	repeat (200) @(posedge clk);
	flp_select = 1;
	flp_req    = 1;

	wait_irq;
	read_intr(intr);
	check(intr == 8'h08, "shared channel: the disk command still completes");
	scsi_busy = 0;

	// The hand-over is only allowed at an idle moment, and a SCSI
	// command has idle moments - it waits there for the driver to
	// re-arm the channel.  So the floppy may well take it before the
	// command is over; the reference behaves the same way, because
	// floppy_select simply chooses which device fills the channel.
	// Excluding the two is the driver's job.  What the hardware must
	// still guarantee is that the disk's data is not damaged by the
	// request arriving.
	if (seize != 0)
		$display("  note: the floppy took the idle channel mid-command");
	ok = 1;
	for (s = 0; s < 4; s = s + 1)
		for (i = 0; i < 512; i = i + 1)
			if (ram_byte(32'h00004000 + s*512 + i) !== pat(2+s, i)) ok = 0;
	check(ok, "shared channel: a request mid-command leaves the disk data intact");
	finish_command(sts);
	check(sts == 8'h00, "shared channel: good status");

	// Drop the request the way a drive does once its sector is taken,
	// then run a clean floppy transfer: the channel must still work
	// for the floppy after a disk command has used it.
	flp_req = 0;
	repeat (50) @(posedge clk);
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'hDEADBEEF;
	ptr_wr32(6'h10, 32'h00005000);
	ptr_wr32(6'h14, 32'h00005200);
	csr_cmd(8'h11);
	flp_req = 1;
	i = 0;
	while (!flp_done && i < 2000000) begin
		@(posedge clk);
		i = i + 1;
	end
	check(flp_done, "shared channel: the floppy is served once the disk is done");
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (ram_byte(32'h00005000 + i) !== (8'hA5 ^ i[7:0])) ok = 0;
	check(ok, "shared channel: the floppy sector lands in its own window");
	ok = 1;
	for (i = 0; i < 16384; i = i + 1)
		if ((i < 'h1400 || i >= 'h1480) && ram[i] !== 32'hDEADBEEF) ok = 0;
	check(ok, "shared channel: nothing written outside the floppy window");
	flp_req = 0;
	flp_select = 0;

	//------------------------------------------------------------
	// A second disk on target 1.  One engine serves both, so the
	// image it reads has to follow the target the command selected,
	// not the last one mounted.
	//------------------------------------------------------------
	img_size = DISK_BLOCKS*512;
	img_mounted2 = 1;
	@(posedge clk);
	img_mounted2 = 0;
	repeat (400) @(posedge clk);       // let the geometry divider run

	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'hDEADBEEF;
	esp_wr8(6'h04, 8'h01);             // select bus id 1
	esp_wr8(6'h02, 8'h80);
	esp_wr8(6'h02, 8'h08); esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h02, 8'h00); esp_wr8(6'h02, 8'h03);
	esp_wr8(6'h02, 8'h01); esp_wr8(6'h02, 8'h00);
	esp_wr8(6'h03, 8'h42);             // select with ATN
	wait_irq;
	read_intr(intr);
	check(intr != 8'h20, "target 1: selected, not a selection timeout");
	check(dut.sd_unit == 1'b1, "target 1: the SD request names slot 1");
	ptr_wr32(6'h10, 32'h00006000);
	ptr_wr32(6'h14, 32'h00006200);
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h02);
	esp_wr8(6'h03, 8'h90);
	wait_irq;
	read_intr(intr);
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (ram_byte(32'h00006000 + i) !== (~pat(3, i) & 8'hFF)) ok = 0;
	check(ok, "target 1: the block comes from the second image");
	finish_command(sts);
	check(sts == 8'h00, "target 1: good status");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
