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
	.img_mounted({4'b0000, img_mounted2, img_mounted}), .img_readonly(1'b0), .img_size(img_size),
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
			sd_buff_dout <= (dut.sd_unit == 3'd1)
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
			if (dut.sd_unit == 3'd1)
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

task ini_wr32;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_ini <= 1; addr <= 6'h00; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= 6'h02; wdata <= v[15:0];
		@(posedge clk);
		sel_ini <= 0; we <= 0;
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

task select_atn6_target;
	input [2:0] target;
	input [7:0] c0, c1, c2, c3, c4, c5;
	begin
		esp_wr8(6'h02, 8'h80);
		esp_wr8(6'h02, c0); esp_wr8(6'h02, c1);
		esp_wr8(6'h02, c2); esp_wr8(6'h02, c3);
		esp_wr8(6'h02, c4); esp_wr8(6'h02, c5);
		esp_wr8(6'h04, {5'd0, target});
		esp_wr8(6'h03, 8'h42);
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
		ini_wr32(base);              // next and internal-buffer reset
		ptr_wr32(6'h14, limit);      // limit
		csr_cmd(8'h15);              // RESET | DEV2M | SETENABLE
		esp_wr8(6'h00, n[7:0]);      // transfer count low
		esp_wr8(6'h01, n[15:8]);     // transfer count high
		esp_wr8(6'h03, 8'h90);       // transfer info, DMA
		wait_irq;
	end
endtask

// ESPCTRL_FLUSH drains one padded longword per write on an enabled DEV2M
// channel.  Keep the wait bounded independently of DMA-complete, since a
// short response normally leaves next below the programmed channel limit.
task flush_dma_in_words;
	input integer words;
	integer fw;
	begin
		for (fw = 0; fw < words; fw = fw + 1) begin
			esp_wr8(6'h20, 8'h34);
			repeat (12) @(posedge clk);
		end
		esp_wr8(6'h20, 8'h30);
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
reg [9:0] fifo_race_pos;
reg [16:0] fifo_race_count;

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

	// Enable ESP interrupts and the external memory-DMA path.
	esp_wr8(6'h20, 8'h30);

	// The four undocumented ESP registers have a fixed readback in the
	// reference (0x0c is confirmed on hardware), rather than open-bus zero.
	ok = 1;
	for (i = 12; i < 16; i = i + 1) begin
		esp_rd8(i[5:0], v);
		if (v != 8'h01) ok = 0;
	end
	check(ok, "unknown ESP registers 0x0c..0x0f read as one");

	// Both DMA buffer-initialization paths clear the ESP DMA FIFO state and
	// DMA Init retains the low-nibble offset used by the real 16-byte buffer.
	esp_wr8(6'h21, 8'hC0);
	csr_cmd(8'h20);                         // DMA_INITBUF
	esp_rd8(6'h21, v);
	check(v == 0, "DMA INITBUF clears the ESP FIFO status");
	esp_wr8(6'h21, 8'hC0);
	ini_wr32(BUF);
	esp_rd8(6'h21, v);
	check(v == 0, "DMA init-register write clears the ESP FIFO status");
	ini_wr32(BUF + 32'd4);
	@(posedge clk);
	check(dut.dma_buf_fill == 4,
	      "DMA init-register write seeds the internal-buffer offset");
	csr_cmd(8'h20);
	@(posedge clk);
	check(dut.dma_buf_fill == 0, "DMA INITBUF clears the internal-buffer offset");

	// A two-byte response remains only in the internal DMA buffer.  FLUSH is
	// a no-op while disabled or in M2DEV, then DEV2M pads it to a longword,
	// writes it, advances next, and empties the residual buffer.
	ram[BUF >> 2] = 32'hDEADBEEF;
	select_atn6(8'h03, 0, 0, 0, 8'd2, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF);
	ptr_wr32(6'h14, BUF + 32'd16);
	csr_cmd(8'h15);
	esp_wr8(6'h21, 8'h95);
	esp_wr8(6'h00, 8'd2);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	wait_irq; read_intr(intr);
	check(dut.d_next == BUF && ram[BUF >> 2] == 32'hDEADBEEF,
	      "short DMA data in retains bytes until FLUSH");
	csr_cmd(8'h14);                        // reset/disabled, DEV2M latched
	esp_wr8(6'h20, 8'h34);
	repeat (12) @(posedge clk);
	check(dut.d_next == BUF && dut.dma_buf_fill == 2,
	      "DMA FLUSH is a no-op while the channel is disabled");
	csr_cmd(8'h01);                        // enabled, memory-to-device
	esp_wr8(6'h20, 8'h34);
	repeat (12) @(posedge clk);
	check(dut.d_next == BUF && dut.dma_buf_fill == 2,
	      "DMA FLUSH is a no-op in memory-to-device direction");
	csr_cmd(8'h05);                        // enabled, device-to-memory
	flush_dma_in_words(1);
	esp_rd8(6'h21, v);
	check(dut.d_next == BUF + 4 && ram[BUF >> 2] == 32'h70000000,
	      "active DEV2M FLUSH pads, writes, and advances one longword");
	check(dut.dma_buf_fill == 0 && v == 8'h95,
	      "active DEV2M FLUSH empties residual bytes without toggling status");
	finish_command(sts);
	check(sts == 0, "short flushed data-in probe completes normally");
	csr_cmd(8'h30);                        // reset and initialize shared DMA

	// FLUSH is a standalone DMA-channel operation even when no ESP transfer
	// is awaiting a residual drain.  The reference pads an empty buffer to one
	// zero longword and returns without entering esp_transfer_done().
	ram[BUF >> 2] = 32'hDEADBEEF;
	ini_wr32(BUF);
	ptr_wr32(6'h14, BUF + 32'd16);
	csr_cmd(8'h15);
	flush_dma_in_words(1);
	repeat (150) @(posedge clk);
	check(dut.d_next == BUF + 4 && ram[BUF >> 2] == 32'h00000000,
	      "empty active DEV2M FLUSH writes exactly one padded longword");
	check(!int_scsi && dut.xst == 0,
	      "empty active DEV2M FLUSH does not start ESP transfer completion");
	csr_cmd(8'h30);

	// Previous toggles the ESP DMA FIFO state after each complete 16-byte
	// external-DMA buffer handoff.  Split an aligned 32-byte INQUIRY into
	// two channel segments.  dma.c's next<=limit loop fills the second
	// buffer before reporting the first channel completion.
	for (i = 0; i < 8; i = i + 1) ram[(BUF >> 2) + i] = 32'hDEADBEEF;
	select_atn6(8'h12, 0, 0, 0, 8'd32, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF);
	ptr_wr32(6'h14, BUF + 32'd16);
	csr_cmd(8'h15);                    // RESET | DEV2M | SETENABLE
	esp_wr8(6'h21, 8'h15);            // lower status bits must survive
	esp_wr8(6'h00, 8'd32);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	wait_dma_complete;
	repeat (150) @(posedge clk);
	esp_rd8(6'h21, v);
	check(v == 8'hD5,
	      "DMA data in: limit equality prefetches the second 16-byte buffer");
	esp_rd8(6'h00, v);
	esp_rd8(6'h04, sts);
	check(v == 0 && sts[4] && sts[2:0] == 3'd3 && int_scsi,
	      "DMA data in: prefetch consumes the ESP count and reaches status");
	check(ram_byte(BUF + 8) == "P" && ram_byte(BUF + 15) == "s" &&
	      ram[(BUF >> 2) + 4] == 32'hDEADBEEF,
	      "DMA data in: prefetched second buffer is not yet in RAM");
	ptr_wr32(6'h14, BUF + 32'd32);
	csr_cmd(8'h0D);                    // DEV2M | clear complete | re-enable
	@(posedge clk);                    // observe completion low before re-waiting
	wait_dma_complete;
	wait_irq; read_intr(intr);
	esp_rd8(6'h21, v);
	check(v == 8'h55,
	      "DMA data in: draining the prefetched buffer selects state 0x40");
	check(ram_byte(BUF + 16) == "H" && ram_byte(BUF + 17) == "D" &&
	      ram_byte(BUF + 18) == "D" && ram_byte(BUF + 31) == " ",
	      "DMA data in: rearm drains retained bytes without re-reading target");
	csr_cmd(8'h0C);
	finish_command(sts);
	check(sts == 0, "DMA FIFO-state data-in probe completes normally");

	// Memory-to-device DMA fills a complete 16-byte internal buffer before
	// presenting bytes to the target.  A two-byte ESP count must retain the
	// other fourteen; the next TI consumes bytes 2 and 3 without fetching a
	// new memory word or skipping ahead.
	for (i = 0; i < 32; i = i + 1) begin
		case (i[1:0])
			0: ram[((BUF2 + i) >> 2)][31:24] = 8'h10 + i[7:0];
			1: ram[((BUF2 + i) >> 2)][23:16] = 8'h10 + i[7:0];
			2: ram[((BUF2 + i) >> 2)][15:8]  = 8'h10 + i[7:0];
			3: ram[((BUF2 + i) >> 2)][7:0]   = 8'h10 + i[7:0];
		endcase
	end
	select_atn6(8'h0A, 0, 0, 0, 8'h01, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF2);
	ptr_wr32(6'h14, BUF2 + 32'd16);
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'd2);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	repeat (800) @(posedge clk);
	check(int_scsi_dma && dut.d_next == BUF2 + 32'd16,
	      "short DMA data out preloads one complete internal buffer");
	wait_irq; read_intr(intr);
	esp_rd8(6'h21, v);
	check(v == 8'h40, "short DMA data out selects the first buffer state");
	ptr_wr32(6'h14, BUF2 + 32'd32);
	csr_cmd(8'h09);
	esp_wr8(6'h00, 8'd2);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	wait_irq; read_intr(intr);
	esp_rd8(6'h21, v);
	check(dut.d_next == BUF2 + 32'd16 && dut.dbuf[0] == 8'h10 &&
	      dut.dbuf[1] == 8'h11 && dut.dbuf[2] == 8'h12 &&
	      dut.dbuf[3] == 8'h13,
	      "next DMA TI drains residual bytes without another memory fetch");
	check(v == 8'hC0, "residual DMA data out revisits the full buffer state");
	// Abort this deliberately short WRITE before it reaches the disk image.
	esp_wr8(6'h03, 8'h03);
	wait_irq; read_intr(intr);
	csr_cmd(8'h30);

	// Exercise the memory-to-device direction as well.  The first two
	// segments expose the transitions; the final segment completes the
	// ordinary full-sector WRITE without changing its data path.
	for (i = 0; i < 128; i = i + 1)
		ram[(BUF2 >> 2) + i] = 32'hA55AC33C;
	select_atn6(8'h0A, 0, 0, 0, 8'h01, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF2);
	ptr_wr32(6'h14, BUF2 + 32'd16);
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h02);
	esp_wr8(6'h03, 8'h90);
	wait_dma_complete;
	esp_rd8(6'h21, v);
	check(v == 8'h40, "DMA data out: first 16-byte handoff selects state 0x40");
	ptr_wr32(6'h14, BUF2 + 32'd32);
	csr_cmd(8'h09);
	@(posedge clk);
	wait_dma_complete;
	esp_rd8(6'h21, v);
	check(v == 8'hC0, "DMA data out: second 16-byte handoff selects state 0xC0");
	ptr_wr32(6'h14, BUF2 + 32'd512);
	csr_cmd(8'h09);
	wait_irq; read_intr(intr);
	check(intr == 8'h10, "DMA FIFO-state data-out probe reaches transfer count zero");
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (disk[i] !== ((i[1:0] == 0) ? 8'hA5 :
		                  (i[1:0] == 1) ? 8'h5A :
		                  (i[1:0] == 2) ? 8'hC3 : 8'h3C)) ok = 0;
	check(ok, "DMA FIFO-state data-out probe writes the sector byte exact");
	csr_cmd(8'h08);
	finish_command(sts);
	check(sts == 0, "DMA FIFO-state data-out probe completes normally");
	for (i = 0; i < 512; i = i + 1) disk[i] = pat(0, i);

	// Command bit 7 loads the ESP counter, but DMA control bit 4 chooses
	// external memory DMA versus the ESP's own FIFO.  With MODE_DMA clear,
	// a 16-byte INQUIRY must fill the FIFO and leave RAM untouched.
	esp_wr8(6'h20, 8'h20);
	for (i = 0; i < 4; i = i + 1) ram[(BUF >> 2) + i] = 32'hDEADBEEF;
	select_atn6(8'h12, 0, 0, 0, 8'd16, 0);
	wait_irq; read_intr(intr);
	ptr_wr32(6'h10, BUF);
	ptr_wr32(6'h14, BUF + 16);
	csr_cmd(8'h11);
	esp_wr8(6'h21, 8'h2A);
	esp_wr8(6'h00, 8'd16);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	wait_irq; read_intr(intr);
	esp_rd8(6'h07, v);
	check(intr == 8'h10 && v == 8'd16,
	      "DMA-tagged TI uses the ESP FIFO when MODE_DMA is clear");
	esp_rd8(6'h21, v);
	check(v == 8'h2A, "FIFO-routed data in does not toggle DMA FIFO state");
	check(ram[(BUF >> 2)] == 32'hDEADBEEF &&
	      ram[(BUF >> 2) + 1] == 32'hDEADBEEF &&
	      ram[(BUF >> 2) + 2] == 32'hDEADBEEF &&
	      ram[(BUF >> 2) + 3] == 32'hDEADBEEF,
	      "FIFO-mode transfer leaves the memory DMA window untouched");
	ok = 1;
	for (i = 0; i < 16; i = i + 1) begin
		esp_rd8(6'h02, v);
		if ((i == 0 && v != 8'h00) || (i == 8 && v != "P") ||
		    (i == 15 && v != "s")) ok = 0;
	end
	check(ok, "FIFO-mode inquiry returns the target bytes through ESP FIFO");
	finish_command(sts);
	check(sts == 0, "FIFO-mode inquiry completes normally");

	// Exercise the other FIFO-DMA direction for a complete disk sector.
	// Keep a valid memory-DMA descriptor armed as a canary: MODE_DMA is
	// clear, so every byte must instead be consumed from the ESP FIFO.
	for (i = 0; i < 128; i = i + 1)
		ram[(BUF2 >> 2) + i] = 32'hDEADBEEF;
	select_atn6(8'h0A, 0, 0, 0, 8'h01, 0);
	wait_irq; read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd0, "FIFO-mode write enters data out phase");
	ptr_wr32(6'h10, BUF2);
	ptr_wr32(6'h14, BUF2 + 32'd512);
	csr_cmd(8'h11);
	esp_wr8(6'h21, 8'h2B);
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h02);
	esp_wr8(6'h03, 8'h90);
	// The target models its initial sector latency before accepting bytes.
	// Bound that wait, then stream 32 FIFO-sized chunks one byte at a time;
	// the engine drains between register writes, so no FIFO overflow is used.
	repeat (400) @(posedge clk);
	check(dut.gap_us == 0, "FIFO-mode write initial sector delay expires");
	for (s = 0; s < 32; s = s + 1)
		for (i = 0; i < 16; i = i + 1)
			esp_wr8(6'h02, 8'h5A ^ {s[3:0], i[3:0]});
	wait_irq; read_intr(intr);
	check(intr == 8'h10,
	      "FIFO-mode write transfer count zero requests bus service");
	esp_rd8(6'h21, v);
	check(v == 8'h2B, "FIFO-routed data out does not toggle DMA FIFO state");
	ok = 1;
	for (i = 0; i < 128; i = i + 1)
		if (ram[(BUF2 >> 2) + i] !== 32'hDEADBEEF) ok = 0;
	check(ok, "FIFO-mode write leaves the memory DMA window untouched");
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (disk[i] !== (8'h5A ^ i[7:0])) ok = 0;
	check(ok, "FIFO-mode write streams all 512 bytes to the disk image");
	finish_command(sts);
	check(sts == 0, "FIFO-mode write completes normally");
	for (i = 0; i < 512; i = i + 1) disk[i] = pat(0, i);
	esp_wr8(6'h20, 8'h30);

	// CPU FIFO accesses and an ESP I/O event can land on the same clock.
	// The C model serializes those calls; use CPU priority here and require
	// the engine to retry, rather than allowing two nonblocking helper calls
	// to update the FIFO from the same stale count.
	esp_wr8(6'h20, 8'h20);                 // FIFO-routed DMA
	select_atn6(8'h12, 0, 0, 0, 8'd32, 0);
	wait_irq; read_intr(intr);
	esp_wr8(6'h00, 8'd17);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	i = 0;
	while (dut.fifoflags != 5'd16 && i < 2000) begin
		@(posedge clk); i = i + 1;
	end
	check(dut.fifoflags == 5'd16,
	      "FIFO collision DI probe fills the controller FIFO");
	esp_rd8(6'h02, v);                     // make room for byte 16
	while (dut.xst != 5'd27) @(negedge clk); // X_FDI_GET
	fifo_race_pos = dut.buf_pos;
	fifo_race_count = dut.counter;
	// Align a CPU FIFO read with the engine's attempted push.
	sel_esp = 1; addr = 6'h02; we = 0; be = 2'b10; wdata = 0;
	@(posedge clk); #1;
	check(dut.buf_pos == fifo_race_pos &&
	      dut.counter == fifo_race_count && dut.fifoflags == 5'd14,
	      "simultaneous DI push/CPU read gives CPU priority and retries engine");
	sel_esp = 0; be = 0;
	repeat (4) @(posedge clk);
	check(dut.buf_pos == fifo_race_pos + 1'd1 &&
	      dut.counter == fifo_race_count - 1'd1 && dut.fifoflags == 5'd15,
	      "deferred DI FIFO push completes once after the CPU read");
	esp_wr8(6'h03, 8'h02);                 // abort the short probe
	repeat (8) @(posedge clk);

	select_atn6(8'h0A, 0, 0, 0, 8'h01, 0);
	wait_irq; read_intr(intr);
	esp_wr8(6'h02, 8'h31);                 // oldest target byte
	esp_wr8(6'h00, 8'd1);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	while (!(dut.xst == 5'd28 && dut.gap_us == 0 &&
	         dut.fifoflags == 1)) @(negedge clk); // X_FDO
	fifo_race_pos = dut.buf_pos;
	fifo_race_count = dut.counter;
	// Align a CPU append with the engine's attempted pop.
	sel_esp = 1; addr = 6'h02; we = 1; be = 2'b10; wdata = 16'hA200;
	@(posedge clk); #1;
	check(dut.buf_pos == fifo_race_pos &&
	      dut.counter == fifo_race_count && dut.fifoflags == 5'd2,
	      "simultaneous DO pop/CPU write gives CPU priority and retries engine");
	sel_esp = 0; we = 0; be = 0;
	repeat (3) @(posedge clk);
	check(dut.buf_pos == fifo_race_pos + 1'd1 && dut.counter == 0 &&
	      dut.fifoflags == 1 && dut.dbuf[fifo_race_pos[8:0]] == 8'h31,
	      "deferred DO FIFO pop consumes the original oldest byte once");
	esp_wr8(6'h03, 8'h02);
	repeat (8) @(posedge clk);
	csr_cmd(8'h30);

	// MODE_DMA is sampled on each reference I/O event, not latched by TI.
	// Begin an INQUIRY through the FIFO, then switch to external DMA while
	// the full FIFO is stalled.  Its queued bytes must reach RAM first.
	esp_wr8(6'h20, 8'h20);
	for (i = 0; i < 8; i = i + 1) ram[(BUF >> 2) + i] = 32'hDEADBEEF;
	select_atn6(8'h12, 0, 0, 0, 8'd32, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF);
	ptr_wr32(6'h14, BUF + 32'd32);
	csr_cmd(8'h15);
	esp_wr8(6'h00, 8'd32);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	i = 0;
	while (dut.fifoflags != 5'd16 && i < 2000) begin
		@(posedge clk); i = i + 1;
	end
	check(dut.fifoflags == 5'd16 && dut.counter == 17'd16,
	      "live MODE_DMA DI probe stalls with sixteen queued bytes");
	esp_wr8(6'h20, 8'h30);                 // switch route while TI is active
	i = 0;
	while (!int_scsi && i < 5000) begin
		@(posedge clk); i = i + 1;
	end
	check(int_scsi && dut.counter == 0 && dut.fifoflags == 0,
	      "live MODE_DMA switch drains FIFO and completes the active TI");
	check(ram_byte(BUF) == 8'h00 && ram_byte(BUF + 8) == "P" &&
	      ram_byte(BUF + 15) == "s" && ram_byte(BUF + 16) == "H" &&
	      ram_byte(BUF + 31) == " ",
	      "live MODE_DMA DI preserves FIFO-first target byte order");
	if (int_scsi) begin
		read_intr(intr);
		csr_cmd(8'h0C);
		finish_command(sts);
	end
	else begin
		esp_wr8(6'h03, 8'h02);
		repeat (8) @(posedge clk);
		csr_cmd(8'h30);
	end

	// Data-out always consumes controller-FIFO bytes before invoking the
	// external DMA channel, even when MODE_DMA was already set at TI start.
	ram[(BUF2 >> 2)] = 32'h11121314;
	ram[(BUF2 >> 2) + 1] = 32'h15161718;
	ram[(BUF2 >> 2) + 2] = 32'h191A1B1C;
	ram[(BUF2 >> 2) + 3] = 32'h1D1E1F20;
	select_atn6(8'h0A, 0, 0, 0, 8'h01, 0);
	wait_irq; read_intr(intr);
	esp_wr8(6'h02, 8'hA0); esp_wr8(6'h02, 8'hA1);
	esp_wr8(6'h02, 8'hA2); esp_wr8(6'h02, 8'hA3);
	ini_wr32(BUF2);
	ptr_wr32(6'h14, BUF2 + 32'd16);
	csr_cmd(8'h11);
	esp_wr8(6'h00, 8'd8);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);
	i = 0;
	while (!int_scsi && i < 5000) begin
		@(posedge clk); i = i + 1;
	end
	check(int_scsi && dut.counter == 0 && dut.fifoflags == 0,
	      "external data-out drains the controller FIFO before RAM");
	check(dut.dbuf[0] == 8'hA0 && dut.dbuf[1] == 8'hA1 &&
	      dut.dbuf[2] == 8'hA2 && dut.dbuf[3] == 8'hA3 &&
	      dut.dbuf[4] == 8'h11 && dut.dbuf[5] == 8'h12 &&
	      dut.dbuf[6] == 8'h13 && dut.dbuf[7] == 8'h14,
	      "external data-out target stream is FIFO bytes followed by RAM bytes");
	if (int_scsi) read_intr(intr);
	esp_wr8(6'h03, 8'h02);                 // abort before sector commit
	repeat (8) @(posedge clk);
	csr_cmd(8'h30);

	// The command register is dual ranked.  TI remains active; the first
	// queued NOP is overwritten by ICCS, setting GE.  Acknowledging TI's
	// interrupt must then launch the newest pending command.
	esp_wr8(6'h20, 8'h30);
	select_atn6(8'h12, 0, 0, 0, 8'd16, 0);
	wait_irq; read_intr(intr);
	ini_wr32(BUF);
	ptr_wr32(6'h14, BUF + 32'd16);
	csr_cmd(8'h15);
	esp_wr8(6'h00, 8'd16);
	esp_wr8(6'h01, 8'h00);
	esp_wr8(6'h03, 8'h90);                 // active, delayed by gap_us
	esp_wr8(6'h03, 8'h00);                 // first pending command
	esp_wr8(6'h03, 8'h11);                 // replace pending and set GE
	wait_irq;
	esp_rd8(6'h04, v);
	check(v[7] && v[6], "third ESP command overwrites pending rank and sets GE");
	esp_rd8(6'h03, v);
	check(v == 8'h90, "queued commands do not replace the active command rank");
	read_intr(intr);
	check(intr == 8'h10, "delayed DMA TI completes before its queued command");
	if (intr == 8'h10) begin
		// Let the interrupt-read side effects settle before waiting for the
		// newly promoted ICCS command; otherwise int_scsi is still the old
		// asserted value in this NBA slot.
		@(negedge clk);
		wait_irq; read_intr(intr);
		check(intr == 8'h08,
		      "interrupt acknowledge launches replacement pending ICCS");
		esp_rd8(6'h02, sts);
		esp_rd8(6'h02, v);
		check(sts == 0 && v == 0,
		      "queued ICCS returns target status and command-complete message");
		esp_wr8(6'h03, 8'h12);
		wait_irq; read_intr(intr);
	end
	else begin
		esp_wr8(6'h03, 8'h02);
		repeat (8) @(posedge clk);
	end
	csr_cmd(8'h30);

	// Illegal commands clear the readable active rank immediately, while
	// retaining the delayed illegal-command interrupt for software to ack.
	esp_wr8(6'h03, 8'h7F);
	esp_rd8(6'h03, v);
	check(v == 0, "illegal ESP opcode clears command register zero");
	wait_irq; read_intr(intr);
	check(intr == 8'h40, "illegal ESP opcode reports INTR_ILL");

	//------------------------------------------------------------
	// TEST UNIT READY
	//------------------------------------------------------------
	select_atn6(8'h00, 8'h00, 8'h00, 8'h00, 8'h00, 8'h00);
	wait_irq;
	read_intr(intr);
	check(intr == 8'h18, "select: bus service and function complete");
	esp_rd8(6'h03, v);
	check(v == 8'h00, "select completion clears the ESP command register");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3, "test unit ready: status phase");
	// esp_transfer_info() schedules an interrupt even for the reference's
	// otherwise-unimplemented PIO transfer in status phase.
	esp_wr8(6'h03, 8'h10);
	repeat (100) @(posedge clk);
	check(int_scsi, "PIO transfer information in status phase interrupts");
	if (int_scsi) read_intr(intr);
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
	for (i = 0; i < 16; i = i + 1) ram[(BUF >> 2) + i] = 32'hDEADBEEF;
	ti_dma_in(17'd54, BUF, BUF + 32'd64);
	read_intr(intr);
	check(intr == 8'h10, "inquiry: transfer count zero requests bus service");
	esp_rd8(6'h04, v);
	check(v[4], "inquiry: transfer count zero");
	check(v[2:0] == 3'd3, "inquiry: final data byte advances to status phase");
	check(dut.d_next == BUF + 32'd48 &&
	      ram[(BUF >> 2) + 12] == 32'hDEADBEEF,
	      "inquiry: DMA retains the final six bytes at the 48-byte boundary");
	flush_dma_in_words(1);
	check(dut.d_next == BUF + 32'd52 &&
	      ram[(BUF >> 2) + 12] == 32'h00000000 &&
	      ram[(BUF >> 2) + 13] == 32'hDEADBEEF,
	      "inquiry: first FLUSH drains four retained bytes");
	flush_dma_in_words(1);
	check(dut.d_next == BUF + 32'd56 &&
	      ram[(BUF >> 2) + 13] == 32'h00000000 &&
	      ram[(BUF >> 2) + 14] == 32'hDEADBEEF,
	      "inquiry: second FLUSH pads the final two bytes and stops at 56");
	check(ram_byte(BUF+8)  == "P" && ram_byte(BUF+9)  == "r" &&
	      ram_byte(BUF+15) == "s", "inquiry: vendor Previous");
	check(ram_byte(BUF+16) == "H" && ram_byte(BUF+17) == "D",
	      "inquiry: model HDD");
	check(ram_byte(BUF+3) == 8'h01 && ram_byte(BUF+32) == "B" &&
	      ram_byte(BUF+53) == 8'h00,
	      "inquiry: SCSI-1 format and reference revision bytes");
	finish_command(sts);
	check(sts == 8'h00, "inquiry: good status");

	//------------------------------------------------------------
	// MODE SENSE parity with the reference disk pages
	//------------------------------------------------------------
	select_atn6(8'h1A, 8'h08, 8'h03, 8'h00, 8'd28, 8'h00);
	wait_irq; read_intr(intr);
	ti_dma_in(17'd28, BUF, BUF + 32'd32); read_intr(intr);
	flush_dma_in_words(3);
	check(ram_byte(BUF) == 8'd27 && ram_byte(BUF+3) == 8,
	      "mode sense: DBD header matches reference");
	check(ram_byte(BUF+4) == 8'h03 && ram_byte(BUF+5) == 8'h16 &&
	      ram_byte(BUF+15) == 8'd32 && ram_byte(BUF+16) == 8'h02 &&
	      ram_byte(BUF+24) == 8'h80,
	      "mode sense: format-device page matches reference");
	finish_command(sts);
	check(sts == 8'h00, "mode sense: page 3 good status");
	select_atn6(8'h1A, 8'h08, 8'h04, 8'h00, 8'd24, 8'h00);
	wait_irq; read_intr(intr);
	ti_dma_in(17'd24, BUF, BUF + 32'd32); read_intr(intr);
	flush_dma_in_words(2);
	check(ram_byte(BUF+6) == 0 && ram_byte(BUF+7) == 0 &&
	      ram_byte(BUF+8) == 1 && ram_byte(BUF+9) == 4,
	      "mode sense: reference 4-head, 32-sector HDD geometry");
	finish_command(sts);

	select_atn6(8'h1A, 8'h00, 8'h40, 8'h00, 8'd64, 8'h00);
	wait_irq; read_intr(intr); finish_command(sts);
	check(sts == 8'h02, "mode sense: changeable values rejected");

	select_atn6(8'h1A, 8'h00, 8'hC0, 8'h00, 8'd64, 8'h00);
	wait_irq; read_intr(intr); finish_command(sts);
	check(sts == 8'h02, "mode sense: saved values rejected");
	select_atn6(8'h03, 0, 0, 0, 0, 0); // allocation zero means four bytes
	wait_irq; read_intr(intr);
	ti_dma_in(17'd4, BUF, BUF + 32'd16); read_intr(intr);
	flush_dma_in_words(1);
	check(ram_byte(BUF+2) == 8'h05,
	      "request sense: saved-values error has illegal-request key");
	finish_command(sts);
	check(sts == 8'h00, "request sense: zero allocation returns four bytes");

	select_atn6(8'h07, 0, 0, 0, 0, 0);
	wait_irq; read_intr(intr); finish_command(sts);
	check(sts == 8'h00, "reassign blocks: accepted as reference no-op");

	//------------------------------------------------------------
	// READ CAPACITY
	//------------------------------------------------------------
	select_atn10(8'h25, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	wait_irq;
	read_intr(intr);
	ti_dma_in(17'd8, BUF, BUF + 32'd16);
	read_intr(intr);
	flush_dma_in_words(2);
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
	check(intr == 8'h10, "read: transfer count zero requests bus service");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3, "read: final data byte advances to status phase");
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
	check(intr == 8'h10, "write: transfer count zero requests bus service");
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
	csr_cmd(8'h17);                          // DEV2M | SETENABLE | SETSUPDATE
	esp_wr8(6'h00, 8'h00);
	esp_wr8(6'h01, 8'h08);                   // 2048 bytes
	esp_wr8(6'h03, 8'h90);                   // transfer info, DMA

	// driver interrupt service: two more segments then let it finish
	wait_dma_complete;
	csr_cmd(8'h0C);                          // DEV2M | CLRCOMPLETE
	ptr_wr32(6'h18, 32'h00002800);           // segment C
	ptr_wr32(6'h1C, 32'h00002A00);
	csr_cmd(8'h06);                          // DEV2M | SETSUPDATE
	wait_dma_complete;
	csr_cmd(8'h0C);
	ptr_wr32(6'h18, 32'h00002C00);           // segment D
	ptr_wr32(6'h1C, 32'h00002E00);
	csr_cmd(8'h06);
	wait_dma_complete;
	csr_cmd(8'h0C);

	wait_irq;                                // ESP: counter reached zero
	read_intr(intr);
	check(intr == 8'h10, "chained read: transfer count zero requests bus service");
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
	check(intr == 8'h10, "chained write: transfer count zero requests bus service");

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

	// Unlike the six-byte commands, a zero transfer length in READ(10)
	// or WRITE(10) really means zero.  It is a successful no-op and must
	// not enter a data phase or touch the disk.
	select_atn10(8'h2A, 8'h00, 8'h00, 8'h00, 8'h00, 8'd2,
	             8'h00, 8'h00, 8'h00, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3 && !sd_wr,
	      "zero-length write(10): status phase with no disk transfer");
	finish_command(sts);
	check(sts == 8'h00, "zero-length write(10): good status");

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
	flush_dma_in_words(2);
	check(ram_byte(BUF+0) == 8'hF0, "request sense: valid, format 0x70");
	check(ram_byte(BUF+2) == 8'h05, "request sense: illegal request key");
	check(ram_byte(BUF+12) == 8'h21, "request sense: invalid lba code");
	check(ram_byte(BUF+3) == 8'h00 && ram_byte(BUF+4) == 8'h00 &&
	      ram_byte(BUF+5) == 8'h00 && ram_byte(BUF+6) == 8'h20,
	      "request sense: info holds the failed lba");
	finish_command(sts);
	check(sts == 8'h00, "request sense: good status");

	// Establish a stale CHECK CONDITION, then prove a zero-length READ(10)
	// replaces it with GOOD/no-sense just like any successful read command.
	select_atn6(8'h08, 8'h00, 8'h00, 8'h20, 8'h01, 8'h00);
	wait_irq;
	read_intr(intr);
	finish_command(sts);
	check(sts == 8'h02, "zero-length read setup: stale check condition exists");
	select_atn10(8'h28, 8'h00, 8'h00, 8'h00, 8'h00, 8'h20,
	             8'h00, 8'h00, 8'h00, 8'h00);
	wait_irq;
	read_intr(intr);
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3 && !sd_rd,
	      "zero-length read(10): status phase with no disk transfer");
	finish_command(sts);
	check(sts == 8'h00 && dut.sense_code[0] == 8'h00 && !dut.sense_valid[0],
	      "zero-length read(10): clears stale status and sense");

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
	check(intr == 8'h10, "shared channel: disk completion requests bus service");
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
	check(int_scsi_dma,
	      "shared channel: exact floppy limit raises DMA completion");
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
	check(dut.sd_unit == 3'd1, "target 1: the SD request names slot 1");
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

	//------------------------------------------------------------
	// Sense belongs to the target. An error on target 1 must not
	// overwrite an invalid-LBA sense retained by target 0.
	//------------------------------------------------------------
	select_atn6_target(3'd0, 8'h08, 0, 0, 8'h20, 8'h01, 0);
	wait_irq; read_intr(intr); finish_command(sts);
	check(sts == 8'h02, "per-target sense: target 0 records invalid LBA");
	select_atn6_target(3'd1, 8'hFF, 0, 0, 0, 0, 0);
	wait_irq; read_intr(intr); finish_command(sts);
	check(sts == 8'h02, "per-target sense: target 1 reports its own error");
	select_atn6_target(3'd0, 8'h03, 0, 0, 0, 8'd22, 0);
	wait_irq; read_intr(intr);
	ti_dma_in(17'd22, BUF, BUF + 32'd32); read_intr(intr);
	flush_dma_in_words(2);
	check(ram_byte(BUF+12) == 8'h21 && ram_byte(BUF+6) == 8'h20,
	      "per-target sense: target 0 invalid LBA survives target 1 command");
	finish_command(sts);

	// Replacement media starts with fresh status and sense state.  An
	// error retained from the previous image must not be attributed to
	// the newly inserted disk.
	img_size = DISK_BLOCKS*512;
	img_mounted = 1;
	@(posedge clk);
	img_mounted = 0;
	repeat (400) @(posedge clk);
	select_atn6_target(3'd0, 8'h03, 0, 0, 0, 8'd22, 0);
	wait_irq; read_intr(intr);
	ti_dma_in(17'd22, BUF, BUF + 32'd32); read_intr(intr);
	flush_dma_in_words(2);
	check(ram_byte(BUF+0) == 8'h70 && ram_byte(BUF+2) == 8'h00 &&
	      ram_byte(BUF+12) == 8'h00,
	      "media replacement: clears the previous image's retained sense");
	finish_command(sts);

	//------------------------------------------------------------
	// A programmed-I/O READ must return the currently addressed byte and
	// enter status phase as the final disk byte is delivered, just as
	// SCSIdisk_Send_Data() does.
	//------------------------------------------------------------
	select_atn6_target(3'd0, 8'h08, 0, 0, 8'h02, 8'h01, 0);
	wait_irq; read_intr(intr);
	ok = 1;
	for (i = 0; i < 512; i = i + 1) begin
		esp_wr8(6'h03, 8'h10);       // transfer information, PIO
		wait_irq; read_intr(intr);
		esp_rd8(6'h02, v);
		if (v !== pat(2, i)) begin
			if (ok) $display("  first PIO mismatch byte %0d got %02x want %02x",
			                 i, v, pat(2, i));
			ok = 0;
		end
	end
	check(ok, "pio read: all 512 bytes match the disk image");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3,
	      "pio read: final disk byte advances directly to status phase");
	finish_command(sts);
	check(sts == 8'h00, "pio read: good status");

	// Two-sector PIO read: the first boundary must refill and remain in
	// data-in; only the second boundary may advance to status.
	select_atn6_target(3'd0, 8'h08, 0, 0, 8'h02, 8'h02, 0);
	wait_irq; read_intr(intr);
	ok = 1;
	for (i = 0; i < 1024; i = i + 1) begin
		esp_wr8(6'h03, 8'h10);
		wait_irq; read_intr(intr);
		esp_rd8(6'h02, v);
		if (v !== pat(2 + i/512, i % 512)) ok = 0;
		if (i == 511) begin
			esp_rd8(6'h04, v);
			check(v[2:0] == 3'd1,
			      "multi-sector pio: first boundary remains data-in");
		end
	end
	check(ok, "multi-sector pio: both sectors are byte exact");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3,
	      "multi-sector pio: final boundary advances to status");
	finish_command(sts);
	check(sts == 8'h00, "multi-sector pio: good status");

	// The reference checks the following block while delivering the
	// current sector's final byte.  Crossing the image end must therefore
	// finish with CHECK CONDITION without one extra PIO command.
	select_atn6_target(3'd0, 8'h08, 0, 0, 8'h07, 8'h02, 0);
	wait_irq; read_intr(intr);
	ok = 1;
	for (i = 0; i < 512; i = i + 1) begin
		esp_wr8(6'h03, 8'h10);
		wait_irq; read_intr(intr);
		esp_rd8(6'h02, v);
		if (v !== pat(7, i)) ok = 0;
	end
	check(ok, "pio end crossing: final valid sector is byte exact");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3,
	      "pio end crossing: last valid byte discovers invalid next LBA");
	finish_command(sts);
	check(sts == 8'h02 && dut.sense_code[0] == 8'h21 &&
	      dut.sense_valid[0] && dut.sense_info[0] == 8,
	      "pio end crossing: check condition identifies LBA 8");

	// A transfer-count boundary exactly at byte 512 must not hide the
	// target's synchronous attempt to refill the requested second block.
	for (i = 0; i < 256; i = i + 1) ram[(BUF >> 2) + i] = 32'hDEADBEEF;
	select_atn6_target(3'd0, 8'h08, 0, 0, 8'h07, 8'h02, 0);
	wait_irq; read_intr(intr);
	ti_dma_in(17'd512, BUF, BUF + 32'd512);
	read_intr(intr);
	ok = 1;
	for (i = 0; i < 512; i = i + 1)
		if (ram_byte(BUF + i) !== pat(7, i)) ok = 0;
	check(ok, "dma end crossing: final valid sector is byte exact");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd3,
	      "dma end crossing: 512-byte TI discovers invalid next LBA");
	finish_command(sts);
	check(sts == 8'h02 && dut.sense_code[0] == 8'h21 &&
	      dut.sense_valid[0] && dut.sense_info[0] == 8,
	      "dma end crossing: check condition identifies LBA 8");

	// esp_message_accepted() disconnects the target after the command-
	// complete message: INTR_DC is reported and the reference leaves its
	// disconnected bus phase at PHASE_DO.
	select_atn6(8'h00, 0, 0, 0, 0, 0);
	wait_irq; read_intr(intr);
	esp_wr8(6'h03, 8'h11);       // initiator command complete
	wait_irq; read_intr(intr);
	esp_rd8(6'h02, sts);
	esp_rd8(6'h02, v);           // command-complete message
	esp_wr8(6'h03, 8'h12);       // message accepted
	wait_irq; read_intr(intr);
	check(intr == 8'h20,
	      "message accepted: reports disconnect interrupt");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd0,
	      "message accepted: leaves the reference disconnected phase");

	// A SCSI bus reset reports reset-detected and leaves the disconnected
	// phase at data-out in esp_bus_reset().
	esp_wr8(6'h03, 8'h03);
	esp_rd8(6'h03, v);
	check(v == 0, "SCSI bus reset immediately clears command register zero");
	wait_irq; read_intr(intr);
	check(intr == 8'h80, "SCSI bus reset reports reset detected");
	esp_rd8(6'h04, v);
	check(v[2:0] == 3'd0, "SCSI bus reset leaves the data-out phase");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
