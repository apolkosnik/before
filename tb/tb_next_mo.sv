//============================================================================
//  MO/ECC test: replays the full boot ROM ECC system test flow (the
//  routine at ROM offset 0x381a) against the real next_mo with its
//  Reed-Solomon codec:
//    1. ECC Write fill of 1024 pattern bytes (csr2 = ECC_DIS)
//    2. ECC Read drain of the encoded 1296 byte sector
//    3. corrupt bytes in memory with the ROM's exact pattern
//    4. ECC Write fill of the corrupted sector with decode
//       (csr2 = ECC_DIS|ECC_MODE), expect ecc_cnt = 0x24 (36)
//    5. ECC Read passthrough drain of the corrected 1024 bytes,
//       compare with the original pattern
//============================================================================

`timescale 1ns/1ps

module tb_next_mo;

reg clk = 0;
always #5 clk = ~clk;

reg reset = 1;

reg         sel_osp = 0, sel_csr = 0, sel_ptr = 0, sel_ini = 0;
reg   [4:0] addr = 0;
reg         we = 0;
reg   [1:0] be = 0;
reg  [15:0] wdata = 0;
wire [15:0] rdata;

wire        m_req, m_we, m_ack;
wire [29:0] m_addr;
wire  [3:0] m_be;
wire [31:0] m_din;
reg  [31:0] m_dout;
wire        m_addr_valid = (m_addr[29:24] == 6'd0) ||
                           (m_addr[29:24] == 6'd1);
wire        m_err = m_req && !m_addr_valid;
integer     m_err_count = 0;

wire int_disk, int_disk_dma, mo_gpo;

// short tick for simulation: 50 "us" = 50 cycles
next_mo #(.CLK_HZ(1000000)) dut
(
	.clk(clk), .reset(reset),
	.sel_osp(sel_osp), .sel_csr(sel_csr), .sel_ptr(sel_ptr), .sel_ini(sel_ini),
	.addr(addr), .we(we), .be(be), .wdata(wdata), .rdata(rdata),
	.m_req(m_req), .m_we(m_we), .m_addr(m_addr), .m_be(m_be),
	.m_din(m_din), .m_dout(m_dout), .m_ack(m_ack), .m_err(m_err),
	.int_disk(int_disk), .int_disk_dma(int_disk_dma),
	.mo_gpo(mo_gpo),
	.img_mounted(oimg_mounted), .img_readonly(oimg_readonly), .img_size(oimg_size),
	.sd_unit(osd_unit), .sd_lba(osd_lba), .sd_rd(osd_rd), .sd_wr(osd_wr),
	.sd_ack(osd_ack), .sd_buff_addr(osd_buff_addr),
	.sd_buff_dout(osd_buff_dout), .sd_buff_din(osd_buff_din_w), .sd_buff_wr(osd_buff_wr)
);

//----------------------------------------------------------------------------
// An optical cartridge.  The image holds the encoded 1296 byte sectors
// the formatter reads, so a sector straddles the card's 512 byte
// blocks - which is the part worth testing.
//----------------------------------------------------------------------------
localparam MO_SECT = 1296;
localparam MO_TRACKS_MODELLED = 4;      // enough for a few sectors
reg  [1:0] oimg_mounted = 0;
reg        oimg_readonly = 0;
reg [63:0] oimg_size = 0;
wire       osd_unit, osd_rd, osd_wr;
wire [31:0] osd_lba;
reg        osd_ack = 0;
reg  [8:0] osd_buff_addr = 0;
reg  [7:0] osd_buff_dout = 0;
reg        osd_buff_wr = 0;
wire [7:0] osd_buff_din_w;

// a byte of the disk, derived from its absolute offset
function [7:0] dbyte;
	input [31:0] off;
	dbyte = off[7:0] ^ off[15:8] ^ 8'h5A;
endfunction

localparam CART_BYTES = MO_TRACKS_MODELLED * 16 * MO_SECT;
reg [7:0] cart [0:CART_BYTES-1];
reg [7:0] first_codeword [0:MO_SECT-1];
integer ci;
initial for (ci = 0; ci < CART_BYTES; ci = ci + 1) cart[ci] = dbyte(ci);

integer osd_reads = 0, osd_writes = 0;
reg     osd_active = 0, osd_wact = 0, osd_rdph = 0;
reg     sd_owner_watch = 0, sd_owner_expected = 0, sd_owner_mismatch = 0;

// The card reads and writes whole blocks; a sector straddles them, so
// putting one back has to leave the bytes around it alone.
always @(posedge clk) begin
	if (sd_owner_watch && dut.dsk_active && osd_unit != sd_owner_expected)
		sd_owner_mismatch <= 1;
	// Match hps_io: data/strobe are registered after the bus transaction,
	// so the last buffer write reaches the device after ack has fallen.
	osd_buff_wr <= 0;
	if (osd_rd && !osd_ack && !osd_active && !osd_wact) begin
		osd_ack <= 1;
		osd_active <= 1;
		osd_buff_addr <= 0;
		osd_reads = osd_reads + 1;
	end
	else if (osd_wr && !osd_ack && !osd_active && !osd_wact) begin
		osd_ack <= 1;
		osd_wact <= 1;
		osd_buff_addr <= 0;
		osd_rdph <= 0;
		osd_writes = osd_writes + 1;
	end
	else if (osd_ack && osd_active) begin
		if (!osd_buff_wr) begin
			osd_buff_dout <= cart[{osd_lba, 9'd0} + {23'd0, osd_buff_addr}];
			osd_buff_wr <= 1;
			if (osd_buff_addr == 9'd511) begin
				osd_ack <= 0;
				osd_active <= 0;
			end
		end
		else begin
			if (osd_buff_addr != 9'd511) osd_buff_addr <= osd_buff_addr + 1'd1;
		end
	end
	else if (osd_ack && osd_wact) begin
		// one byte every other cycle: the buffer read is registered
		if (osd_rdph) begin
			cart[{osd_lba, 9'd0} + {23'd0, osd_buff_addr}] <= osd_buff_din_w;
			osd_rdph <= 0;
			if (osd_buff_addr == 9'd511) begin
				osd_ack <= 0;
				osd_wact <= 0;
			end
			else osd_buff_addr <= osd_buff_addr + 1'd1;
		end
		else osd_rdph <= 1;
	end
end

localparam SRC  = 32'h04002000;
localparam DST  = 32'h04003000;
localparam DST2 = 32'h04004000;

// RAM model
reg [31:0] ram [0:16383];
reg        ack_r;
assign m_ack = ack_r;

always @(posedge clk) begin
	if (m_err) m_err_count <= m_err_count + 1;
	if (reset) ack_r <= 0;
	else if (!m_req || !m_addr_valid) ack_r <= 0;
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

task ram_set_byte;
	input [31:0] a;
	input [7:0] v;
	begin
		case (a[1:0])
			0: ram[a[15:2]][31:24] = v;
			1: ram[a[15:2]][23:16] = v;
			2: ram[a[15:2]][15:8]  = v;
			3: ram[a[15:2]][7:0]   = v;
		endcase
	end
endtask

// the corruption pattern of the ROM ECC test (raises exactly 36
// correctable byte errors)
task ram_corrupt;
	integer k;
	reg [7:0] b;
	begin
		ram_set_byte(DST + 'h001, ~ram_byte(DST + 'h001));
		ram_set_byte(DST + 'h032, ram_byte(DST + 'h032) + 8'd1);
		ram_set_byte(DST + 'h064, (~ram_byte(DST + 'h064)) + 8'h9c);
		ram_set_byte(DST + 'h0c8, ram_byte(DST + 'h0c8) + 8'hff);
		ram_set_byte(DST + 'h309, ~ram_byte(DST + 'h309));
		ram_set_byte(DST + 'h37a, ram_byte(DST + 'h37a) + 8'h17);
		ram_set_byte(DST + 'h457, ram_byte(DST + 'h456) + 8'h27);
		ram_set_byte(DST + 'h50a, ram_byte(DST + 'h50a) + 8'h16);
		for (k = 0; k < 32; k = k + 1) begin
			b = ~ram_byte(DST + 'h215 + k);
			ram_set_byte(DST + 'h215 + k, (b % 8'h2f) + 8'h25);
		end
	end
endtask

task osp_wr8;
	input [4:0] a;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_osp <= 1; addr <= a; we <= 1;
		be <= a[0] ? 2'b01 : 2'b10;
		wdata <= a[0] ? {8'h00, v} : {v, 8'h00};
		@(posedge clk);
		sel_osp <= 0; we <= 0;
	end
endtask

// A 680x0 word access invokes the even-byte handler first and the odd-byte
// handler second.  Several OSP register pairs deliberately depend on that
// ordering, so exercise a real single bus transaction as well as byte writes.
task osp_wr16;
	input [4:0] a;
	input [15:0] v;
	begin
		@(posedge clk);
		sel_osp <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v;
		@(posedge clk);
		sel_osp <= 0; we <= 0;
	end
endtask

task osp_rd8;
	input [4:0] a;
	output [7:0] v;
	begin
		@(posedge clk);
		sel_osp <= 1; addr <= a; we <= 0;
		be <= a[0] ? 2'b01 : 2'b10;
		@(posedge clk);
		v = a[0] ? rdata[7:0] : rdata[15:8];
		sel_osp <= 0;
	end
endtask

task formatter_response;
	output [15:0] response;
	reg [7:0] hi, lo;
	begin
		osp_wr8(5'h07, 8'h20);             // FMT_RD_STAT exposes drive response
		repeat (2) @(posedge clk);
		osp_rd8(5'h08, hi);
		osp_rd8(5'h09, lo);
		response = {hi, lo};
	end
endtask

task ptr_wr32;
	input [4:0] a;
	input [31:0] v;
	begin
		@(posedge clk);
		sel_ptr <= 1; addr <= a; we <= 1; be <= 2'b11; wdata <= v[31:16];
		@(posedge clk);
		addr <= a + 5'd2; wdata <= v[15:0];
		@(posedge clk);
		sel_ptr <= 0; we <= 0;
	end
endtask

task csr_cmd;
	input [7:0] v;
	begin
		@(posedge clk);
		sel_csr <= 1; addr <= 5'h00; we <= 1; be <= 2'b11; wdata <= {8'h00, v};
		@(posedge clk);
		addr <= 5'h02; wdata <= 16'h0000;
		@(posedge clk);
		sel_csr <= 0; we <= 0;
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

integer i, waited;
reg [7:0] v, v2, id_first, id_second;
reg [15:0] response;
reg [31:0] next_before;
reg ok, op1, op2, overlap_seen;

// Bounded standalone helpers used by the two-bank regressions.  They exercise
// only software-visible DMA/formatter behavior, so the tests fail cleanly on
// the former single-buffer implementation without depending on RTL internals.
task ecc_fill_from_memory;
	input [7:0] mode;
	input [31:0] first;
	input [31:0] last;
	output completed;
	integer n;
	begin
		osp_wr8(5'h06, mode);
		ptr_wr32(5'h00, first);
		ptr_wr32(5'h04, last);
		csr_cmd(8'h11);
		osp_wr8(5'h07, 8'h40);
		@(posedge clk);
		n = 0;
		while (dut.ecc_state != 3'd0 && n < 600000) begin
			@(posedge clk);
			n = n + 1;
		end
		completed = (dut.ecc_state == 3'd0 && dut.d_next == last);
		osp_wr8(5'h07, 8'h00);
		osp_wr8(5'h04, 8'hFC);
	end
endtask

task ecc_drain_to_memory;
	input [7:0] mode;
	input [31:0] first;
	input [31:0] last;
	output completed;
	integer n;
	begin
		osp_wr8(5'h06, mode);
		ptr_wr32(5'h00, first);
		ptr_wr32(5'h04, last);
		csr_cmd(8'h11);
		osp_wr8(5'h07, 8'h80);
		@(posedge clk);
		n = 0;
		while (dut.ecc_state != 3'd0 && n < 600000) begin
			@(posedge clk);
			n = n + 1;
		end
		completed = (dut.ecc_state == 3'd0);
		osp_wr8(5'h07, 8'h00);
		osp_wr8(5'h04, 8'hFC);
	end
endtask

task expect_wrong_head_rejected;
	input [7:0] cmd;
	input [639:0] name;
	integer reads_before, writes_before;
	reg [7:0] status;
	begin
		osp_wr8(5'h07, 8'h00);
		osp_wr8(5'h04, 8'hFC);
		osp_wr8(5'h00, 8'h00);             // modeled head is still on track zero
		osp_wr8(5'h01, 8'h00);
		osp_wr8(5'h02, {4'd0, dut.sec_offset[0]});
		osp_wr8(5'h03, 8'h01);
		reads_before = osd_reads;
		writes_before = osd_writes;
		osp_wr8(5'h07, cmd);
		repeat (1500) @(posedge clk);      // at least one formatter sector event
		osp_rd8(5'h04, status);
		check(dut.fmt_mode == 3'd0 && dut.sector_counter == 9'd1 &&
		      osd_reads == reads_before && osd_writes == writes_before &&
		      ((status & 8'hFC) == 0), name);
		osp_wr8(5'h07, 8'h00);
	end
endtask


initial begin
	for (i = 0; i < 16384; i = i + 1) ram[i] = 32'h00000000;
	for (i = 0; i < 1024; i = i + 1) begin
		case (i[1:0])
			0: ram[(SRC[15:2]) + i/4][31:24] = i[7:0];
			1: ram[(SRC[15:2]) + i/4][23:16] = i[7:0];
			2: ram[(SRC[15:2]) + i/4][15:8]  = i[7:0];
			3: ram[(SRC[15:2]) + i/4][7:0]   = i[7:0];
		endcase
	end

	repeat (10) @(posedge clk);
	reset = 0;
	repeat (10) @(posedge clk);

	// formatter reset
	osp_wr8(5'h07, 8'h00);
	// ECC_DIS mode: standalone buffer operation
	osp_wr8(5'h06, 8'h40);
	// interrupt mask: ECC_DONE
	osp_wr8(5'h05, 8'h08);

	// fill: channel at the source, enable, ECC Write command
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 32'd1024);
	csr_cmd(8'h11);              // RESET | SETENABLE
	osp_wr8(5'h07, 8'h40);       // FMT_ECC_WRITE

	repeat (80000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after ECC write (fill)");
	check(int_disk, "INT_DISK raised (masked in)");
	osp_wr8(5'h04, 8'h08);       // clear
	repeat (2) @(posedge clk);
	check(!int_disk, "INT_DISK released after status clear");

	// drain: channel at the destination, ECC Read encodes 1296 bytes
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 32'd1296);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);       // FMT_ECC_READ

	repeat (140000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after ECC read (encode and drain)");

	// the data bytes sit in the interleaved sector layout: byte i of
	// the input is at 36*(i/32) + (i%32)
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST + (i/32)*36 + (i%32)) != i[7:0]) ok = 0;
	check(ok, "encoded sector carries the data in interleaved layout");

	// corrupt the sector in memory with the ROM test's exact pattern
	ram_corrupt;

	// decode: fill the corrupted 1296 bytes with ECC_MODE set
	osp_wr8(5'h04, 8'hfc);       // clear interrupt status
	osp_wr8(5'h06, 8'h60);       // ECC_DIS | ECC_MODE
	osp_wr8(5'h07, 8'h00);       // formatter reset (clears ecc_cnt)
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 32'd1296);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);       // FMT_ECC_WRITE (fill + decode)

	repeat (200000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after decode");
	osp_rd8(5'h0b, v);
	$display("ecc_cnt = %0d (ROM expects 36)", v);
	check(v == 8'h24, "ecc_cnt reads 0x24 corrected errors");
	osp_rd8(5'h0a, v);
	check(v == 8'h00, "no ERRSTAT_ECC");

	// passthrough drain of the corrected data
	osp_wr8(5'h04, 8'hfc);
	ptr_wr32(5'h00, DST2);
	ptr_wr32(5'h04, DST2 + 32'd1024);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);       // FMT_ECC_READ (passthrough, ECC_MODE)

	repeat (120000) @(posedge clk);

	osp_rd8(5'h04, v);
	check(v[3], "MOINT_ECC_DONE after passthrough drain");
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST2 + i) != i[7:0]) ok = 0;
	check(ok, "corrected data matches the original pattern");

	// The 8-bit formatter count follows DMA convention: zero represents
	// a full 256-sector operation, not an already-complete command.
	osp_wr8(5'h03, 8'h00);
	osp_wr8(5'h07, 8'h02);
	@(posedge clk);
	check(dut.sector_counter == 9'd256,
	      "sector count: register value zero expands to 256 sectors");
	osp_wr8(5'h07, 8'h00);

	// An ECC write without an enabled DMA input is a STARVE data error,
	// rather than a command that remains busy indefinitely.
	csr_cmd(8'h10);                    // reset/disable disk DMA
	osp_wr8(5'h06, 8'h40);            // diagnostic ECC mode
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h07, 8'h40);            // ECC write with no source
	repeat (200) @(posedge clk);
	osp_rd8(5'h0A, v);
	osp_rd8(5'h04, v2);
	check(v == 8'h08 && v2[7], "ECC starvation raises data-error status");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);

	// ECC_BLOCKS repeats one standalone operation before completing.
	// Both 1024-byte inputs must be consumed before ECC_DONE is raised.
	for (i = 0; i < 2048; i = i + 1)
		ram_set_byte(SRC + i, 8'h69 ^ i[7:0]);
	osp_wr8(5'h06, 8'h50);            // ECC_DIS | ECC_BLOCKS
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 2048);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);
	waited = 0;
	v = 0;
	while (!v[3] && waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[3] && dut.d_next == SRC + 2048,
	      "ECC blocks: repeat consumes two buffers before completion");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);

	//------------------------------------------------------------
	// dma_interrupt() runs at every MO DMA service boundary.  A descriptor
	// that ends halfway through an ECC buffer must therefore reload START/STOP
	// under SUPDATE and continue, in both memory-to-buffer and buffer-to-memory
	// directions, rather than starving at the first LIMIT.
	//------------------------------------------------------------
	for (i = 0; i < 1024; i = i + 1)
		ram_set_byte(SRC + i, 8'hA6 ^ i[7:0]);
	osp_wr8(5'h06, 8'h40);
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 512);
	ptr_wr32(5'h08, SRC + 512);
	ptr_wr32(5'h0C, SRC + 1024);
	csr_cmd(8'h10);
	csr_cmd(8'h03);                   // SUPDATE | ENABLE
	osp_wr8(5'h07, 8'h40);
	@(posedge clk);                   // observe the command's state update
	waited = 0;
	while (dut.ecc_state != 3'd0 && waited < 300000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	if (!(dut.ecc_state == 3'd0 && dut.d_next == SRC + 1024 &&
	      dut.d_csr[3] && !dut.d_csr[1] && !dut.d_csr[0]))
		$display("  chained fill debug: state=%0d next=%08x limit=%08x size=%0d csr=%02x waited=%0d",
		         dut.ecc_state, dut.d_next, dut.d_limit, dut.ecc_size,
		         dut.d_csr, waited);
	check(dut.ecc_state == 3'd0 && dut.d_next == SRC + 1024 &&
	      dut.d_csr[3] && !dut.d_csr[1] && !dut.d_csr[0],
	      "DMA chained fill crosses LIMIT and completes the ECC input buffer");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);

	for (i = 0; i < 1296; i = i + 1)
		ram_set_byte(DST + i, 8'h00);
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 512);
	ptr_wr32(5'h08, DST + 512);
	ptr_wr32(5'h0C, DST + 1296);
	csr_cmd(8'h10);
	csr_cmd(8'h03);
	osp_wr8(5'h07, 8'h80);            // encode, then chained drain
	@(posedge clk);                   // observe the command's state update
	waited = 0;
	while (dut.ecc_state != 3'd0 && waited < 500000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	if (!(dut.ecc_state == 3'd0 && dut.d_next == DST + 1296 &&
	      dut.d_csr[3] && !dut.d_csr[1] && !dut.d_csr[0]))
		$display("  chained drain debug: state=%0d next=%08x limit=%08x size=%0d drain=%0d csr=%02x waited=%0d",
		         dut.ecc_state, dut.d_next, dut.d_limit, dut.ecc_size,
		         dut.drain_pos, dut.d_csr, waited);
	check(dut.ecc_state == 3'd0 && dut.d_next == DST + 1296 &&
	      dut.d_csr[3] && !dut.d_csr[1] && !dut.d_csr[0],
	      "DMA chained drain crosses LIMIT and completes the ECC output buffer");

	// A completed drain consumes the buffer.  A second passthrough read has
	// zero bytes to expose and must not replay stale data into a fresh DMA range.
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h06, 8'h60);
	ptr_wr32(5'h00, DST2);
	ptr_wr32(5'h04, DST2 + 128);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);
	@(posedge clk);                   // observe the command's state update
	waited = 0;
	while (dut.ecc_state != 3'd0 && waited < 10000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(dut.ecc_state == 3'd0 && dut.ecc_size == 0 && dut.d_next == DST2,
	      "completed ECC drain consumes size and cannot replay stale payload");

	//------------------------------------------------------------
	// Formatter reset is an authoritative abort, including requests already
	// presented to RAM and work already running in the RS engine.  It does not
	// acknowledge or clear a pre-existing interrupt-status bit.  Use a CSR2/CSR1
	// word write for the first abort to cover reset ordering with the lane shadow.
	//------------------------------------------------------------
	osp_rd8(5'h04, v);
	check(v[3], "formatter-reset setup retains an ECC completion interrupt");
	osp_wr8(5'h06, 8'h40);
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 1024);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);
	waited = 0;
	while (!m_req && waited < 10000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(m_req, "FMT_RESET RAM-abort setup reaches an active request");
	osp_wr16(5'h06, 16'h4000);         // CSR2 then FMT_RESET in one word access
	next_before = dut.d_next;
	repeat (20) @(posedge clk);
	osp_rd8(5'h04, v);
	check(dut.csr2 == 8'h40 && dut.fmt_mode == 3'd0 &&
	      dut.ecc_state == 3'd0 && dut.mst == 3'd0 && !m_req && !dut.mo_we &&
	      dut.d_next == next_before && v[3],
	      "FMT_RESET aborts RAM handshake after word-lane CSR2 update without clearing IRQ");

	// Run far enough to enter the codec, then reset it synchronously as well.
	// ECC WRITE only invokes the decoder when ECC_MODE is set, and therefore
	// consumes a full 1296-byte codeword before the RS engine becomes active.
	osp_wr8(5'h06, 8'h60);
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 1296);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);
	waited = 0;
	while (dut.ecc_state != 3'd4 && waited < 300000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(dut.ecc_state == 3'd4, "FMT_RESET codec-abort setup reaches active RS state");
	osp_wr8(5'h07, 8'h00);
	repeat (4) @(posedge clk);
	osp_rd8(5'h04, v);
	check(dut.ecc_state == 3'd0 && dut.rs.st == 6'd0 &&
	      !dut.rs_start_enc && !dut.rs_start_dec && v[3],
	      "FMT_RESET aborts RS engine and preserves pending interrupt status");
	osp_wr8(5'h04, 8'hFC);

	// C dispatches formatter bits in order: ECC READ takes ownership of an
	// idle engine, then the simultaneous ECC WRITE sees it busy and is refused.
	// Both nonblocking RTL tests used to see the same old DONE state, allowing
	// WRITE to overwrite READ in the very same command byte.
	osp_wr8(5'h06, 8'h60);
	osp_wr8(5'h07, 8'hC0);             // ECC_READ | ECC_WRITE
	repeat (4) @(posedge clk);
	check(dut.ecc_is_read && dut.ecc_state != 3'd1,
	      "combined ECC read/write: read wins and write is rejected");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);

	//------------------------------------------------------------
	// The controller has two alternating 1296-byte buffers.  DMA/disk fills
	// the input bank, DMA/disk drains the output bank, and CSR2 BUF_TOGGLE
	// swaps their roles without copying data.
	//------------------------------------------------------------
	for (i = 0; i < 1024; i = i + 1) begin
		ram_set_byte(SRC + i,        8'h2A ^ i[7:0]);
		ram_set_byte(SRC + 1024 + i, 8'hD5 ^ i[7:0]);
		ram_set_byte(DST + i,        8'h00);
		ram_set_byte(DST2 + i,       8'h00);
	end
	ecc_fill_from_memory(8'h40, SRC, SRC + 1024, op1);
	osp_wr8(5'h06, 8'h44);             // toggle roles after filling A
	ecc_fill_from_memory(8'h40, SRC + 1024, SRC + 2048, op2);
	check(op1 && op2, "two-bank manual fill setup completes both input banks");
	ecc_drain_to_memory(8'h60, DST, DST + 1024, op1);
	ok = op1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST + i) !== (8'h2A ^ i[7:0])) ok = 0;
	check(ok, "two-bank manual order drains A from the original output bank");
	osp_wr8(5'h06, 8'h64);             // toggle to the bank holding B
	ecc_drain_to_memory(8'h60, DST2, DST2 + 1024, op1);
	ok = op1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST2 + i) !== (8'hD5 ^ i[7:0])) ok = 0;
	check(ok, "two-bank manual order drains B after BUF_TOGGLE");

	// CLR_BUFP clears both physical banks, not just whichever role happens to
	// be selected.  Refill both, clear once, and prove neither can be replayed.
	ecc_fill_from_memory(8'h40, SRC, SRC + 1024, op1);
	osp_wr8(5'h06, 8'h44);
	ecc_fill_from_memory(8'h40, SRC + 1024, SRC + 2048, op2);
	osp_wr8(5'h06, 8'h68);             // ECC_DIS | ECC_MODE | CLR_BUFP
	ecc_drain_to_memory(8'h60, DST, DST + 1024, op1);
	ok = op1 && dut.d_next == DST;
	osp_wr8(5'h06, 8'h64);
	ecc_drain_to_memory(8'h60, DST2, DST2 + 1024, op2);
	ok = ok && op2 && dut.d_next == DST2;
	check(ok, "CLR_BUFP clears both ECC banks across a role toggle");

	// ECC_BLOCKS automates the same ping-pong: the first bypassed decode is
	// made output while the second fill proceeds into the opposite input bank.
	ecc_fill_from_memory(8'h50, SRC, SRC + 2048, op1);
	check(op1, "ECC_BLOCKS write consumes both source buffers");
	ecc_drain_to_memory(8'h60, DST, DST + 1024, op1);
	ok = op1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST + i) !== (8'h2A ^ i[7:0])) ok = 0;
	check(ok, "ECC_BLOCKS exposes A through the output bank first");
	osp_wr8(5'h06, 8'h64);
	ecc_drain_to_memory(8'h60, DST2, DST2 + 1024, op1);
	ok = op1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST2 + i) !== (8'hD5 ^ i[7:0])) ok = 0;
	check(ok, "ECC_BLOCKS preserves B in the opposite bank");
	if ($test$plusargs("bank_debug")) $finish;

	//------------------------------------------------------------
	// The drive.  Nothing here worked before: the controller answered
	// every status request with "no drive attached".
	//------------------------------------------------------------
	// The drive is fitted whether or not a cartridge is in it.  An
	// empty drive still answers - and says it is empty; a drive that
	// is not there answers nothing at all.  Reading the command byte
	// back unchanged is what "did not answer" looks like, so the
	// status has to replace it: DS_EMPTY | DS_STOPPED.
	osp_wr8(5'h06, 8'h00);            // CSR2: select drive 0
	osp_wr8(5'h04, 8'hFC);            // clear status without RESET/GPO

	// A single word write must make its high/even byte visible to the low/odd
	// byte handler.  With the stale CSRH value this decodes as a seek instead
	// of Return Version Information.
	osp_wr16(5'h08, 16'h3F00);         // DRV_RVI in one bus transaction
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h0880,
	      "word write CSRH/CSRL: low lane executes the new high-byte command");
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // clear any fail-first attention
	repeat (200) @(posedge clk);

	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	osp_rd8(5'h08, v); osp_rd8(5'h09, v2);
	check({v, v2} == 16'h2000,
	      "drive response remains hidden until formatter RD_STAT");
	formatter_response(response);
	check(response == 16'h4200, "an empty drive answers, and reports no cartridge");

	// A seek with no cartridge must not move the head.  Carrying on
	// regardless leaves the drive reporting a track nobody asked for,
	// and the driver reads that back as the drive's position.
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);   // high order seek 1
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // seek -> track 0x1000
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h22); osp_wr8(5'h09, 8'h00);   // DRV_RCA
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h0000, "an empty drive does not move its head");
	osp_rd8(5'h04, v);
	check(v[1], "an empty drive raises attention");
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // DRV_RID
	repeat (200) @(posedge clk);
	osp_rd8(5'h04, v);
	check(!v[1], "reset attention clears it");

	oimg_size = MO_TRACKS_MODELLED * 16 * MO_SECT;
	oimg_mounted = 2'b01;
	@(posedge clk);
	oimg_mounted = 2'b00;
	repeat (20) @(posedge clk);
	osp_rd8(5'h04, v);
	check(!v[1], "cartridge mount does not invent drive attention");

	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(!response[14], "cartridge in: no longer empty");
	check(response[2], "cartridge in: load completed");
	check(response[9], "cartridge in: still stopped");
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // RID clears sticky status
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(!response[2], "reset status clears sticky load-completed bit");

	// Drive 0 now reports an inserted/stopped cartridge; drive 1 is empty.
	// In a word write to CSR2/CSR1, RD_STAT must use the just-selected drive 1,
	// not the old drive selected before the bus cycle.
	osp_wr8(5'h06, 8'h01);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // latch drive 1 status
	repeat (200) @(posedge clk);
	osp_wr8(5'h06, 8'h00);
	osp_wr16(5'h06, 16'h0120);         // CSR2=drive 1, CSR1=RD_STAT
	repeat (4) @(posedge clk);
	osp_rd8(5'h08, v); osp_rd8(5'h09, v2);
	check({v, v2} == 16'h4200 && dut.csr2[0],
	      "word write CSR2/CSR1: RD_STAT observes the new drive selection");
	osp_wr8(5'h06, 8'h00);

	// The cartridge is not ejected by a reset: dev_reset carries the
	// RESET instruction the ROM runs during start-up, and a drive
	// emptied there is empty for every boot that follows.
	reset = 1;
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h06, 8'h00);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(!response[14], "the cartridge survives a device reset");
	osp_wr8(5'h08, 8'h80); osp_wr8(5'h09, 8'h00);   // self diagnostic
	repeat (200) @(posedge clk);
	osp_rd8(5'h04, v);
	check(v[0] && !v[1], "self diagnostic completes without attention");

	// Interrupt-status low bits are controls, not W1C interrupt bits:
	// RESET stops the selected drive, and GPO is the Cube030 floppy mux.
	osp_wr8(5'h04, 8'h01);            // RESET, GPO low
	osp_rd8(5'h04, v);
	check(!v[0] && !mo_gpo, "OSP reset stops the selected drive");
	osp_wr8(5'h04, 8'h02);            // start, GPO high
	osp_rd8(5'h04, v);
	check(!v[0] && mo_gpo, "OSP start is delayed and drives GPO high");
	waited = 0;
	while (!v[0] && waited < 600000) begin
		@(posedge clk);
		if (waited[9:0] == 0) osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[0], "OSP start completes after reset delay");
	osp_wr8(5'h04, 8'h00);            // keep started, GPO low
	@(posedge clk);
	check(!mo_gpo, "OSP GPO clears independently of start state");

	// Starting a command on the other drive flushes the outstanding
	// delayed completion to the drive that originated it.
	osp_wr8(5'h04, 8'h01);            // stop drive 0
	osp_wr8(5'h04, 8'h00);            // begin delayed restart of drive 0
	repeat (100) @(posedge clk);
	osp_wr8(5'h06, 8'h01);            // select drive 1
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // RDS on drive 1
	repeat (200) @(posedge clk);
	check(dut.drv_compl[0], "drive switch preserves originating completion");
	osp_wr8(5'h08, 8'hDE); osp_wr8(5'h09, 8'hAD);   // invalid on drive 1
	repeat (200) @(posedge clk);
	check(dut.drv_attn[1], "drive 1 records attention from its own command");
	osp_wr8(5'h06, 8'h00);            // return to drive 0
	osp_wr8(5'h06, 8'h01);            // reselecting drive 1 releases attention
	@(posedge clk);
	check(!dut.drv_attn[1], "drive selection releases selected-drive attention");
	osp_wr8(5'h06, 8'h00);

	// A delayed OSP restart uses the same single interrupt timer as drive
	// commands.  Starting the other drive must first deliver the pending
	// completion to its original owner, just as mo_set_signals_delayed does.
	osp_wr8(5'h04, 8'h01);            // stop drive 0
	osp_wr8(5'h04, 8'h00);            // delayed restart of drive 0
	repeat (100) @(posedge clk);
	osp_wr8(5'h06, 8'h01);
	osp_wr8(5'h04, 8'h01);            // stop drive 1
	osp_wr8(5'h04, 8'h00);            // restart drive 1 before drive 0 completes
	repeat (100) @(posedge clk);
	check(dut.drv_compl[0],
	      "cross-drive restart preserves the original completion");
	// Return to a clean controller state for the remaining drive/disk tests.
	reset = 1;
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h06, 8'h00);

	// The head cannot be moved against a disk that is not turning.
	// The reference refuses it as an unimplemented command - DS_CMD,
	// with a completion and an attention - rather than doing it.
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // DRV_RID: clear status
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h01); osp_wr8(5'h09, 8'h23);   // seek, motor stopped
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h22); osp_wr8(5'h09, 8'h00);   // DRV_RCA
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h0000, "a stopped drive does not move its head");
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response[5], "a seek with the disk stopped is an invalid command");
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // DRV_RID
	repeat (200) @(posedge clk);

	osp_wr8(5'h08, 8'h53); osp_wr8(5'h09, 8'h00);   // DRV_STM, start motor

	// Spinning a cartridge up takes real time - the reference waits
	// 1.6 seconds before answering - and the driver waits for that
	// completion.  Answering at once says the disk is at speed when
	// it is not.
	repeat (2000) @(posedge clk);
	osp_rd8(5'h04, v);
	check(!v[0], "the motor does not report itself up at once");
	while (!v[0]) begin
		repeat (1000) @(posedge clk);
		osp_rd8(5'h04, v);
	end
	check(v[0], "the motor reports up once it has spun");

	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(!response[9], "motor started: no longer stopped");

	osp_rd8(5'h04, v);
	check(v[0], "the drive signals command complete");

	// READ, WRITE, ERASE, and VERIFY each require their matching optical
	// head.  The reference aborts before doing I/O or reporting formatter
	// status; fail closed in synthesizable RTL.  READ ID is deliberately
	// excluded and must continue working with no selected head.
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on, head remains none
	repeat (200) @(posedge clk);
	check(dut.drv_head[0] == 3'd0, "wrong-head setup has no selected head");
	expect_wrong_head_rejected(8'h02,
	      "wrong-head READ has no I/O, status, count, or completion side effect");
	expect_wrong_head_rejected(8'h01,
	      "wrong-head WRITE has no I/O, status, count, or completion side effect");
	expect_wrong_head_rejected(8'h04,
	      "wrong-head ERASE has no I/O, status, count, or completion side effect");
	expect_wrong_head_rejected(8'h08,
	      "wrong-head VERIFY has no I/O, status, count, or completion side effect");
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h07, 8'h10);             // READ ID is head-independent
	repeat (1500) @(posedge clk);
	osp_rd8(5'h04, v);
	check(v[2] && dut.fmt_mode == 3'd1,
	      "READ ID remains legal with no selected head");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);   // spiral off
	repeat (200) @(posedge clk);

	// seek, then ask where the head is
	osp_wr8(5'h08, 8'h01); osp_wr8(5'h09, 8'h23);   // seek track 0x123
	repeat (200) @(posedge clk);
	osp_rd8(5'h04, v);
	check(!v[0], "seek timing: completion is not reported immediately");
	repeat (6000) @(posedge clk);
	osp_wr8(5'h08, 8'h22); osp_wr8(5'h09, 8'h00);   // DRV_RCA
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h0123, "seek: the drive returns the track it moved to");
	formatter_response(response);
	check(response == 16'h0123,
	      "formatter status: preserves the selected drive's last response");
	osp_wr8(5'h08, 8'h51); osp_wr8(5'h09, 8'h71);   // relative jump, invalid head 7
	repeat (2000) @(posedge clk);
	check(dut.drv_head[0] == 0, "relative jump: invalid head selects no head");

	osp_wr8(5'h08, 8'h3F); osp_wr8(5'h09, 8'h00);   // DRV_RVI
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h0880, "the drive returns its version");

	//------------------------------------------------------------
	// Erase a sector.  Sector 1 of the first track starts 1296 bytes
	// in, so it begins part way through a block and ends part way
	// through another: the bytes around it must survive.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);   // recalibrate
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);   // high order seek, 1
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // seek -> track 0x1000
	repeat (10000) @(posedge clk);
	osp_wr8(5'h08, 8'h22); osp_wr8(5'h09, 8'h00);   // DRV_RCA
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response == 16'h1000, "high order seek reaches the first real track");
	osp_wr8(5'h08, 8'h44); osp_wr8(5'h09, 8'h00);   // select erase head
	repeat (200) @(posedge clk);

	// Abort a live read/modify/write SD transaction.  Seed OPER_COMPL with a
	// READ-ID event first: FMT_RESET must leave that interrupt latched while
	// dropping every disk/RAM/codec request and ignoring the card's late ack.
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	osp_wr8(5'h07, 8'h10);
	waited = 0;
	v = 0;
	while (!v[2] && waited < 50000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[2], "FMT_RESET disk-abort setup has a pending formatter interrupt");
	osp_wr8(5'h07, 8'h00);             // reset READ ID, preserve its IRQ
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h0F);             // sacrificial sector 15
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h07, 8'h04);
	waited = 0;
	while (!(osd_ack && dut.dsk_active) && waited < 50000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(osd_ack && dut.dsk_active,
	      "FMT_RESET disk-abort setup reaches an active SD transaction");
	osp_wr8(5'h07, 8'h00);             // abort while the SD card still owns ack
	repeat (4) @(posedge clk);
	osp_rd8(5'h04, v);
	check(dut.fmt_mode == 3'd0 && dut.ecc_state == 3'd0 &&
	      dut.dst == 3'd0 && !dut.dsk_active && !osd_rd && !osd_wr &&
	      dut.mst == 3'd0 && !m_req && !dut.dsk_we && v[2],
	      "FMT_RESET aborts disk and handshake engines without clearing IRQ");
	waited = 0;
	while ((osd_ack || dut.dsk_active) && waited < 20000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	repeat (20) @(posedge clk);
	check(!dut.dsk_active && !osd_rd && !osd_wr &&
	      dut.sector_counter == 9'd1,
	      "late SD acknowledgement cannot resurrect a formatter-reset transaction");
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h07, 8'h00);

	// The formatter reset releases the modeled platter while the card drains
	// its already-acknowledged block.  Reposition explicitly before the owner
	// regression; otherwise that deliberately late acknowledgement can advance
	// beyond track 0x1000 and make the following sector-1 request unreachable.
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);   // spiral off
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);   // recalibrate
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);   // high order seek, 1
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // seek -> track 0x1000
	repeat (10000) @(posedge clk);
	osp_wr8(5'h08, 8'h44); osp_wr8(5'h09, 8'h00);   // select erase head
	repeat (200) @(posedge clk);

	osp_wr8(5'h00, 8'h10);            // track high
	osp_wr8(5'h01, 8'h00);            // track low
	osp_wr8(5'h02, 8'h01);            // sector 1, increment 0
	osp_wr8(5'h03, 8'h01);            // one sector
	osp_wr8(5'h04, 8'hFC);            // clear status without RESET/GPO
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	repeat (100) @(posedge clk);
	sd_owner_mismatch = 0;
	sd_owner_expected = 0;
	sd_owner_watch = 1;
	osp_wr8(5'h07, 8'h04);            // FMT_ERASE
	waited = 0;
	while (!(osd_ack && osd_buff_addr >= 9'd32) && waited < 50000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	if (osd_ack) begin
		osp_wr8(5'h06, 8'h01);        // change selected drive during block service
		waited = 0;
		while (osd_ack && waited < 2000) begin
			@(posedge clk);
			waited = waited + 1;
		end
		check(!sd_owner_mismatch && !osd_ack,
		      "SD transaction keeps its initiating drive across a mid-burst selection change");
	end
	else check(1'b0, "SD owner regression reaches a mid-burst selection point");
	osp_wr8(5'h06, 8'h00);
	sd_owner_watch = 0;

	waited = 0;
	v = 0;
	while (!v[2] && waited < 400000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[2], "erase: the operation completes");
	osp_rd8(5'h03, v2);
	check(v2 == 0, "sector count: completed formatter operation reads zero");
	$display("  cartridge blocks: %0d read, %0d written", osd_reads, osd_writes);

	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[MO_SECT + i] !== 8'hFF) ok = 0;
	check(ok, "erase: the whole sector is erased");
	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[i] !== dbyte(i)) ok = 0;
	check(ok, "erase: the sector before it is untouched");
	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[2*MO_SECT + i] !== dbyte(2*MO_SECT + i)) ok = 0;
	check(ok, "erase: the sector after it is untouched");

	// The reference completes each sector operation before the modeled
	// platter advances.  A consecutive request therefore must not lose
	// sector two while the first SD transaction is still in flight.
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);
	osp_wr8(5'h08, 8'h44); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h80);            // keep the sector-miss timer enabled
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h12);            // sector 2, increment 1
	osp_wr8(5'h03, 8'h02);            // two sectors
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h04);
	waited = 0;
	v = 0;
	while (!v[2] && waited < 200000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	ok = v[2] && !v[4];
	for (i = 2*MO_SECT; i < 4*MO_SECT; i = i + 1)
		if (cart[i] !== 8'hFF) ok = 0;
	check(ok, "two-sector erase completes both consecutive sectors");

	//------------------------------------------------------------
	// Write a sector out of memory through the real cartridge path:
	// the formatter gathers 1024 data bytes, encodes them to 1296, then
	// commits the codeword when the sector comes round. Sector 5 starts 6480
	// bytes in, part way through a block as ever.  What lands on the
	// disk is the buffer, so that is what it is checked against - the
	// codec has had its say by then.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);   // spiral off
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);   // recalibrate
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);   // high order seek 1
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // seek -> track 0x1000
	repeat (10000) @(posedge clk);

	for (i = 0; i < 1024; i = i + 1)
		ram_set_byte(DST2 + i, 8'hC3 ^ i[7:0] ^ {i[10:8], 5'd0});

	osp_wr8(5'h07, 8'h00);            // formatter reset
	osp_wr8(5'h06, 8'h00);            // real disk codec path
	ptr_wr32(5'h00, DST2);
	ptr_wr32(5'h04, DST2 + 1024);
	csr_cmd(8'h11);                   // RESET | SETENABLE
	osp_wr8(5'h03, 8'h01);            // final sector: ECC completion is due
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h07, 8'h40);            // pre-stage and encode for disk write
	repeat (100) @(posedge clk);      // let the command enter ECC_FILL
	waited = 0;
	while (dut.ecc_state != 3'd5 && waited < 900000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	check(dut.ecc_state == 3'd5 && dut.ecc_size == MO_SECT,
	      "write: real disk path encodes 1024 bytes to 1296");
	osp_rd8(5'h04, v);
	check(v[3], "write: final real-disk encode raises ECC complete");
	// Keep the formatter-owned WAITING buffer in passthrough mode while
	// committing it to the cartridge.
	osp_wr8(5'h06, 8'h60);

	osp_wr8(5'h08, 8'h43); osp_wr8(5'h09, 8'h00);   // select write head
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);            // track high
	osp_wr8(5'h01, 8'h00);            // track low
	osp_wr8(5'h02, 8'h05);            // sector 5
	osp_wr8(5'h03, 8'h01);            // one sector
	osp_wr8(5'h04, 8'hFC);            // clear status without RESET/GPO
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h01);            // FMT_WRITE

	waited = 0;
	v = 0;
	while (!v[2] && waited < 400000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[2], "write: the operation completes");
	repeat (100) @(posedge clk);
	osp_rd8(5'h04, v);
	osp_rd8(5'h0A, v2);
	check(dut.ecc_state == 3'd0 && !v[7] && v2 == 0,
	      "write: final commit does not start a phantom DMA/ECC refill");
	$display("  cartridge blocks: %0d read, %0d written", osd_reads, osd_writes);

	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[5*MO_SECT + i] !==
		    (dut.eccout ? dut.eccbuf1[i] : dut.eccbuf0[i])) begin
			if (ok) $display("  byte %0d: disk %02x buffer %02x",
			                 i, cart[5*MO_SECT + i],
			                 dut.eccout ? dut.eccbuf1[i] : dut.eccbuf0[i]);
			ok = 0;
		end
	check(ok, "write: the sector on the disk is the buffer that was committed");
	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[4*MO_SECT + i] !== dbyte(4*MO_SECT + i)) ok = 0;
	check(ok, "write: the sector before it is untouched");
	ok = 1;
	for (i = 0; i < MO_SECT; i = i + 1)
		if (cart[6*MO_SECT + i] !== dbyte(6*MO_SECT + i)) ok = 0;
	check(ok, "write: the sector after it is untouched");

	//------------------------------------------------------------
	// Combined ECC_WRITE|FMT_WRITE with a two-sector DMA range exposes the
	// formatter's WAITING ownership rule.  Once sector 7 is encoded, the
	// still-enabled channel must not overwrite that codeword with sector 8
	// before the first disk commit consumes it.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);
	osp_wr8(5'h08, 8'h43); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	for (i = 0; i < 1024; i = i + 1) begin
		ram_set_byte(SRC + i,        8'h31 ^ i[7:0]);
		ram_set_byte(SRC + 1024 + i, 8'hC7 ^ i[7:0]);
	end
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h00);
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 2048);
	csr_cmd(8'h11);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h17);             // sector 7, increment 1
	osp_wr8(5'h03, 8'h02);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	osp_wr8(5'h07, 8'h41);             // ECC_WRITE | FMT_WRITE
	waited = 0;
	ok = 0;
	while (!ok && waited < 500000) begin
		@(posedge clk);
		waited = waited + 1;
		if (dut.ecc_state == 3'd5 && dut.ecc_size == MO_SECT &&
		    dut.d_next == SRC + 1024) begin
			for (i = 0; i < MO_SECT; i = i + 1)
				first_codeword[i] = dut.eccout ? dut.eccbuf1[i] : dut.eccbuf0[i];
			ok = 1;
		end
	end
	check(ok, "encoded-write WAITING setup captures the first sector buffer");
	waited = 0;
	v = 0;
	while ((!v[2] || dut.ecc_state != 3'd0) && waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	ok = v[2] && dut.ecc_state == 3'd0 && dut.d_next == SRC + 2048;
	for (i = 0; i < MO_SECT; i = i + 1) begin
		if (cart[7*MO_SECT + i] !== first_codeword[i]) ok = 0;
		if (cart[8*MO_SECT + i] !==
		    (dut.eccout ? dut.eccbuf1[i] : dut.eccbuf0[i])) ok = 0;
	end
	check(ok,
	      "encoded-write WAITING preserves each buffer until its matching disk commit");
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h04, 8'hFC);

	//------------------------------------------------------------
	// Read that codeword back through the real cartridge path.  This
	// exercises the opposite codec direction and the post-decode DMA drain.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);   // spiral off
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);   // recalibrate
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);   // high order seek 1
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // seek -> track 0x1000
	repeat (10000) @(posedge clk);

	for (i = 0; i < 1024; i = i + 1)
		ram_set_byte(DST + i, 8'h5A);
	osp_wr8(5'h07, 8'h00);            // formatter reset
	osp_wr8(5'h06, 8'h00);            // real disk codec path
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 1024);
	csr_cmd(8'h11);                   // RESET | SETENABLE
	osp_wr8(5'h08, 8'h41); osp_wr8(5'h09, 8'h00);   // select read head
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h05);
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h02);            // FMT_READ
	waited = 0;
	v = 0;
	while ((!v[2] || dut.ecc_state != 3'd0 || dut.d_next != DST + 1024) &&
	       waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[2] && dut.ecc_state == 3'd0 && dut.d_next == DST + 1024,
	      "read: formatter, decoder, and 1024-byte DMA drain complete");
	check(v[3], "read: final real-disk decode raises ECC complete");
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(DST + i) !== (8'hC3 ^ i[7:0] ^ {i[10:8], 5'd0})) ok = 0;
	check(ok, "read: decoded payload exactly matches the original 1024 bytes");

	// Exercise the same consecutive-sector sequencing through the ECC
	// pipeline.  Give sector 6 the same valid codeword as sector 5.
	for (i = 0; i < MO_SECT; i = i + 1)
		cart[6*MO_SECT + i] = cart[5*MO_SECT + i];
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);
	for (i = 0; i < 2048; i = i + 1)
		ram_set_byte(DST + i, 8'h5A);
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h90);            // real codec, BLOCKS, sector timer
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 2048);
	csr_cmd(8'h11);
	osp_wr8(5'h08, 8'h41); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h15);            // sector 5, increment 1
	osp_wr8(5'h03, 8'h02);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h02);
	waited = 0;
	v = 0;
	overlap_seen = 0;
	while ((!v[2] || dut.ecc_state != 3'd0 || dut.d_next != DST + 2048) &&
	       waited < 1800000) begin
		osp_rd8(5'h04, v);
		if (dut.dsk_active && dut.ecc_state == 3'd3) overlap_seen = 1;
		waited = waited + 1;
	end
	ok = v[2] && v[3] && !v[4] && dut.d_next == DST + 2048 && overlap_seen;
	for (i = 0; i < 2048; i = i + 1)
		if (ram_byte(DST + i) !== (8'hC3 ^ i[7:0] ^ {1'b0, i[9:8], 5'd0})) ok = 0;
	check(ok, "ECC_BLOCKS two-sector read overlaps next fill with prior drain");

	//------------------------------------------------------------
	// VERIFY reads and decodes the same codeword, but must not drain the
	// decoded payload through DMA.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);

	for (i = 0; i < 1024; i = i + 1)
		ram_set_byte(SRC + i, 8'hA5);
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h00);
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 1024);
	csr_cmd(8'h10);                   // RESET, leave DMA disabled
	osp_wr8(5'h08, 8'h42); osp_wr8(5'h09, 8'h00);   // select verify head
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h05);
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h08);            // FMT_VERIFY
	waited = 0;
	v = 0;
	while ((!v[2] || dut.ecc_state != 3'd0) && waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[2] && dut.ecc_state == 3'd0 && !v[7],
	      "verify: cartridge codeword decodes without a data error");
	check(v[3], "verify: final real-disk decode raises ECC complete");
	check(dut.d_next == SRC, "verify: decoded data is not drained through DMA");
	ok = 1;
	for (i = 0; i < 1024; i = i + 1)
		if (ram_byte(SRC + i) !== 8'hA5) ok = 0;
	check(ok, "verify: system memory is untouched");

	// Previous clears both ECC buffers after VERIFY.  A following
	// passthrough ECC read therefore has no stale payload to drain.
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h06, 8'h60);            // ECC_DIS | ECC_MODE passthrough
	ptr_wr32(5'h00, SRC);
	ptr_wr32(5'h04, SRC + 1024);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h80);            // standalone ECC read
	waited = 0;
	v = 0;
	while ((!v[3] || dut.ecc_state != 3'd0) && waited < 200000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[3] && dut.ecc_state == 3'd0 &&
	      dut.d_next == SRC && ram_byte(SRC) == 8'hA5,
	      "verify: clears ECC payload before a later standalone drain");

	//------------------------------------------------------------
	// Three bad symbols in each of three rows and columns exceed the
	// RS(36,32) correction limit deterministically.  Even a failed final
	// decode completes the ECC sequence while also reporting DATA_ERR.
	//------------------------------------------------------------
	for (i = 0; i < 3; i = i + 1) begin
		cart[5*MO_SECT + i]      = cart[5*MO_SECT + i]      ^ 8'hFF;
		cart[5*MO_SECT + 36 + i] = cart[5*MO_SECT + 36 + i] ^ 8'hFF;
		cart[5*MO_SECT + 72 + i] = cart[5*MO_SECT + 72 + i] ^ 8'hFF;
	end
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h00);
	csr_cmd(8'h10);
	osp_wr8(5'h08, 8'h42); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h05);
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h08);
	waited = 0;
	v = 0;
	while ((!v[7] || dut.ecc_state != 3'd0) && waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	osp_rd8(5'h0A, v2);
	check(v[2] && v[3] && v[7] && v2 == 8'h01,
	      "uncorrectable verify: completes ECC and reports the data error");
	check(dut.d_next == SRC, "uncorrectable verify: never drains DMA");
	check(dut.ecc_size == 0,
	      "uncorrectable verify: clears the decoded buffer");

	// A failed READ carries the same completion/error status, but unlike
	// VERIFY it still drains 1024 bytes so the DMA channel can terminate.
	osp_wr8(5'h08, 8'h5A); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h10); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h01);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);
	repeat (10000) @(posedge clk);
	osp_wr8(5'h07, 8'h00);
	osp_wr8(5'h06, 8'h00);
	ptr_wr32(5'h00, DST);
	ptr_wr32(5'h04, DST + 1024);
	csr_cmd(8'h11);
	osp_wr8(5'h08, 8'h41); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h00, 8'h10);
	osp_wr8(5'h01, 8'h00);
	osp_wr8(5'h02, 8'h05);
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);
	repeat (100) @(posedge clk);
	osp_wr8(5'h07, 8'h02);
	waited = 0;
	v = 0;
	while ((!v[7] || dut.ecc_state != 3'd0 || dut.d_next != DST + 1024) &&
	       waited < 900000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	osp_rd8(5'h0A, v2);
	check(v[2] && v[3] && v[7] && v2 == 8'h01 &&
	      dut.d_next == DST + 1024,
	      "uncorrectable read: reports the error and completes its DMA drain");

	//------------------------------------------------------------
	// READ ID is a live formatter mode, not a one-shot command.  Each
	// sector passing under the head updates the ID registers and raises
	// operation-complete again, until software resets the formatter.
	//------------------------------------------------------------
	osp_wr8(5'h07, 8'h00);
	// The failed READ above deliberately leaves DATA_ERR/ECC_DONE/OPER_COMPL
	// pending.  Clear them on the exact edge that READ ID raises a fresh
	// OPER_COMPL: old W1C bits must stay clear while the new event survives.
	osp_rd8(5'h04, v);
	check(v[7], "W1C collision setup has a pending old data error");
	osp_wr8(5'h06, 8'h80);            // sector timer must not gate READ ID
	osp_wr8(5'h07, 8'h10);            // FMT_ID_READ
	@(negedge clk);
	while (!dut.sec_tick) @(negedge clk);
	sel_osp = 1; addr = 5'h04; we = 1; be = 2'b10;
	wdata = 16'hFC00;
	@(posedge clk);                    // W1C and fresh READ-ID event coincide
	@(negedge clk);
	sel_osp = 0; we = 0;
	osp_rd8(5'h04, v);
	check(v[2] && v[7:3] == 0,
	      "W1C clears old bits while preserving a same-cycle new event");
	osp_rd8(5'h02, id_first);
	osp_wr8(5'h04, 8'h04);            // acknowledge this sector ID
	waited = 0;
	v = 0;
	while (!v[2] && waited < 100000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	osp_rd8(5'h02, id_second);
	check(v[2] && !v[4] && id_second[3:0] != id_first[3:0],
	      "read ID: sector timer is ignored and successive IDs are reported");
	osp_wr8(5'h07, 8'h00);

	//------------------------------------------------------------
	// With the sector timer enabled, passing the requested ID must end
	// the formatter command with MOINT_TIMEOUT instead of waiting forever.
	//------------------------------------------------------------
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h06, 8'h80);            // sector timer, drive 0
	osp_wr8(5'h00, 8'h0F);            // an ID behind the current track
	osp_wr8(5'h01, 8'hFF);
	osp_wr8(5'h02, 8'h00);
	osp_wr8(5'h03, 8'h01);
	osp_wr8(5'h07, 8'h02);            // FMT_READ
	waited = 0;
	v = 0;
	while (!v[4] && waited < 100000) begin
		osp_rd8(5'h04, v);
		waited = waited + 1;
	end
	check(v[4], "sector timer: missed ID reports formatter timeout");

	//------------------------------------------------------------
	// Every forced drive completion goes through mo_set_signals() in the
	// reference, which also ends any seek owned by that drive.  Exercise
	// all three ways the single delayed-command timer can be displaced:
	// a command collision on the same drive, a command on the other drive,
	// and a restart of the other drive.
	//------------------------------------------------------------
	osp_wr8(5'h07, 8'h00);            // formatter reset
	osp_wr8(5'h04, 8'hFC);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h04);   // high order seek, 4
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // long seek to 0x4000
	repeat (20) @(posedge clk);
	check(dut.drv_seeking[0] && !dut.drv_compl[0],
	      "same-drive collision setup has a seek in flight");
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // collide with RDS on drive 0
	repeat (20) @(posedge clk);
	check(dut.drv_compl[0] && !dut.drv_seeking[0],
	      "same-drive busy rejection ends the interrupted seek");

	// Clear the deliberate DS_BUSY attention, restore spiral motion, and
	// start another long seek before drive 1 takes the command timer.
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // RID
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h59); osp_wr8(5'h09, 8'h00);   // spiral on
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h00);   // high order seek, 0
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // long seek back to track 0
	repeat (20) @(posedge clk);
	check(dut.drv_seeking[0] && !dut.drv_compl[0],
	      "cross-drive command setup has a seek in flight");
	osp_wr8(5'h06, 8'h01);                         // select drive 1
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // RDS takes timer ownership
	repeat (20) @(posedge clk);
	check(dut.drv_compl[0] && !dut.drv_seeking[0],
	      "cross-drive command ends the old owner's seek");
	repeat (200) @(posedge clk);

	// Repeat the ownership transfer through the OSP restart path.
	osp_wr8(5'h06, 8'h00);
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h04);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // long seek to 0x4000
	repeat (20) @(posedge clk);
	osp_wr8(5'h06, 8'h01);
	osp_wr8(5'h04, 8'h01);                         // stop drive 1
	osp_wr8(5'h04, 8'h00);                         // restart takes timer ownership
	repeat (20) @(posedge clk);
	check(dut.drv_compl[0] && !dut.drv_seeking[0],
	      "cross-drive restart ends the old owner's seek");

	// Forwarding an outstanding command attention to its original owner
	// also invokes mo_set_signals(), so it must stop that drive's spiral.
	osp_wr8(5'h06, 8'h00);
	osp_wr8(5'h08, 8'hDE); osp_wr8(5'h09, 8'hAD);   // invalid command, attention pending
	osp_wr8(5'h06, 8'h01);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // displace it with drive 1 RDS
	repeat (20) @(posedge clk);
	check(dut.drv_attn[0] && !dut.drv_spiraling[0],
	      "cross-drive attention delivery stops the old owner's spiral");

	// Leave the media mounted but return controller state to a clean baseline.
	reset = 1;
	repeat (4) @(posedge clk);
	reset = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h06, 8'h00);

	//------------------------------------------------------------
	// A host-side cartridge removal is a physical eject.  It stops both
	// spindle and spiral immediately; an empty drive cannot remain running.
	//------------------------------------------------------------
	oimg_size = 0;
	oimg_mounted = 2'b01;
	@(posedge clk);
	oimg_mounted = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(response[14] && response[9], "host eject: drive reports empty and stopped");
	check(!dut.drv_spiraling[0], "host eject: spiral motion stops");

	//------------------------------------------------------------
	// An ordinary motor-start can race cartridge removal/insertion.  The
	// empty condition is live status, not a sticky fault: after a later
	// mount, RDS must stop reporting DS_EMPTY even without an intervening RID.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h53); osp_wr8(5'h09, 8'h00);   // DRV_STM while empty
	repeat (200) @(posedge clk);
	osp_rd8(5'h04, v);
	check(v[1], "empty motor-start raises attention");

	oimg_size = MO_TRACKS_MODELLED * 16 * MO_SECT;
	oimg_mounted = 2'b01;
	@(posedge clk);
	oimg_mounted = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h08, 8'h20); osp_wr8(5'h09, 8'h00);   // DRV_RDS, no RID first
	repeat (200) @(posedge clk);
	formatter_response(response);
	check(!response[14] && response[9] && response[2],
	      "post-insert status is inserted/stopped, not sticky empty");
	osp_rd8(5'h04, v);
	check(v[1], "insertion preserves the pending attention until RID");

	//------------------------------------------------------------
	// Insertion calls mo_set_signals immediately: it completes an in-flight
	// seek and clears the seeking indication, but never manufactures or clears
	// attention.  Reinsert the same image while a long seek owns the timer.
	//------------------------------------------------------------
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);   // RID
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h53); osp_wr8(5'h09, 8'h00);   // spin up again
	repeat (20) @(posedge clk);                     // observe command taking busy
	waited = 0;
	while (!dut.drv_compl[0] && waited < 1700000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	osp_wr8(5'h08, 8'hA0); osp_wr8(5'h09, 8'h04);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h00); osp_wr8(5'h09, 8'h00);   // long seek to 0x4000
	repeat (20) @(posedge clk);
	check(dut.drv_seeking[0] && !dut.drv_compl[0] && !dut.drv_attn[0],
	      "insertion-signal setup has an attention-free seek in flight");
	oimg_mounted = 2'b01;
	@(posedge clk);
	oimg_mounted = 0;
	repeat (4) @(posedge clk);
	check(dut.drv_compl[0] && !dut.drv_seeking[0] && !dut.drv_attn[0],
	      "cartridge insertion completes seek immediately without changing attention");

	// mo_protected() schedules CMD_DELAY and returns before the ordinary
	// 1600-us relative-jump delay.  The late assignment used to overwrite
	// that protected-path completion, leaving software waiting needlessly.
	oimg_readonly = 1;
	oimg_mounted = 2'b01;
	@(posedge clk);
	oimg_mounted = 0;
	repeat (20) @(posedge clk);
	osp_wr8(5'h08, 8'h50); osp_wr8(5'h09, 8'h00);
	repeat (200) @(posedge clk);
	osp_wr8(5'h08, 8'h53); osp_wr8(5'h09, 8'h00);
	repeat (20) @(posedge clk);
	waited = 0;
	while (!dut.drv_compl[0] && waited < 1700000) begin
		@(posedge clk);
		waited = waited + 1;
	end
	osp_wr8(5'h08, 8'h51); osp_wr8(5'h09, 8'h30);   // relative jump, write head
	repeat (100) @(posedge clk);
	check(dut.drv_compl[0] && dut.drv_attn[0] &&
	      !dut.drv_seeking[0] && dut.drv_head[0] == 3'd0,
	      "write-protected relative jump completes on CMD_DELAY and selects no head");
	oimg_readonly = 0;

	//------------------------------------------------------------
	// DMA validation: bad alignment is rejected at enable, and a memory
	// fault preserves the full bad address until the arbiter rejects it.
	//------------------------------------------------------------
	osp_wr8(5'h07, 8'h00);
	ptr_wr32(5'h00, SRC + 32'd1);
	ptr_wr32(5'h04, SRC + 32'd1024);
	csr_cmd(8'h11);
	repeat (4) @(posedge clk);
	check(!dut.d_csr[0] && dut.d_csr[3] && dut.d_csr[4] &&
	      dut.d_next == SRC + 32'd1,
	      "misaligned MO DMA window completes with BUSEXC without moving next");

	csr_cmd(8'h10);
	ptr_wr32(5'h00, 32'h08000000);
	ptr_wr32(5'h04, 32'h08000400);
	csr_cmd(8'h11);
	osp_wr8(5'h07, 8'h40);
	repeat (200) @(posedge clk);
	check(m_err_count != 0 && m_addr == 30'h02000000,
	      "MO DMA preserves invalid high address bits through its master port");
	check(!dut.d_csr[0] && dut.d_csr[3] && dut.d_csr[4] &&
	      dut.d_next == 32'h08000000,
	      "rejected MO DMA access completes with BUSEXC and cannot advance");

	if (errors == 0) $display("ALL PASS");
	else             $display("%0d FAILURES", errors);
	$finish;
end

endmodule
